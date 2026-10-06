#include "app/package_operation_dialog.h"
#include "app/studio_theme.h"
#include "core/idtech_image.h"
#include "core/package_copy_store.h"
#include "core/package_draft.h"
#include "core/package_draft_access.h"
#include "core/package_import_store.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QTranslator>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
QByteArray bytes(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
template<class Predicate> bool until(Predicate predicate)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!predicate() && elapsed.elapsed() < 15000) { QApplication::processEvents(); QThread::msleep(1); }
	return predicate();
}

// Hold the first worker progress callback until the GUI timer owns the event
// loop, then join the worker without dispatching its queued finished callback.
// Cancel/Close therefore occurs after computation but before UI adoption.
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		return QByteArray(context) == "VibeStudioPackageDialog" ? QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source)) : QString();
	}
};

class HandoffGate final {
public:
	HandoffGate(const QString& name, bool close, bool cancel = true) : m_name(name), m_close(close), m_cancel(cancel)
	{
		QObject::connect(&m_poll, &QTimer::timeout, [&] {
			QDialog* dialog = nullptr;
			for (auto* widget : QApplication::topLevelWidgets()) {
				if (widget->objectName() == m_name) { dialog = qobject_cast<QDialog*>(widget); break; }
			}
			if (!dialog) { return; }
			if (!dialog->property("operationRunning").toBool()) { dialog->close(); return; }
			if (m_seen) { if (duringFinalization) { duringFinalization(dialog); } return; }
			if (!m_reached.load()) { return; }
			m_seen = true;
			auto* worker = dialog->findChild<QThread*>(QString(), Qt::FindDirectChildrenOnly);
			auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
			{
				std::lock_guard lock(m_mutex); m_release = true;
			}
			m_condition.notify_all();
			m_finishedBeforeAction = worker && worker->wait(10000) && worker->isFinished()
				&& dialog->property("operationRunning").toBool();
			m_actionable = cancel && cancel->isEnabled() && !cancel->accessibleName().isEmpty();
			if (m_finishedBeforeAction && afterWorker) { afterWorker(dialog); }
			if (m_close) { m_actionable &= !dialog->close(); }
			else if (m_cancel && cancel) { cancel->click(); }
		});
		m_poll.start(1);
	}
	~HandoffGate() { m_poll.stop(); }
	std::function<void(QDialog*)> afterWorker, duringFinalization;
	PackageReadControl control()
	{
		PackageReadControl result;
		result.progress = [this](const QString&, qint64, qint64) { progress(); };
		return result;
	}
	void progress()
	{
		if (QThread::currentThread() == qApp->thread()) { m_workerOnly = false; }
		if (m_reached.exchange(true)) { return; }
		std::unique_lock lock(m_mutex);
		if (!m_condition.wait_for(lock, std::chrono::seconds(15), [&] { return m_release; })) { m_timedOut = true; }
	}
	bool checked() const
	{
		const bool valid = m_seen && m_finishedBeforeAction && m_actionable && m_workerOnly && !m_timedOut;
		std::cout << m_name.toStdString() << (m_close ? " Close" : m_cancel ? " Cancel" : " Accept")
			<< ": observed=" << m_seen << " finished-before-action=" << m_finishedBeforeAction
			<< " actionable=" << m_actionable << " worker-only=" << m_workerOnly.load()
			<< " gate-timeout=" << m_timedOut.load() << '\n';
		return valid;
	}
private:
	QString m_name;
	bool m_close = false, m_cancel = true, m_seen = false, m_finishedBeforeAction = false, m_actionable = false;
	std::atomic_bool m_reached{false}, m_timedOut{false}, m_workerOnly{true};
	std::mutex m_mutex;
	std::condition_variable m_condition;
	bool m_release = false;
	QTimer m_poll;
};
}

int main(int argc, char** argv)
{
	// Direct widget methods and offscreen event loops only; no native input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication application(argc, argv);
#ifdef Q_OS_WIN
	application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const auto cleanup = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup(); waitForPackageCopyCleanup(); });
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	QDir root(temporary.path()); QString error; bool ok = true;
	PackageStagingModel initial;
	ok &= expect(initial.createEmpty(PackageArchiveFormat::Pak, {}, &error)
		&& initial.addBytes("alpha", QStringLiteral("a.txt"), &error)
		&& initial.addBytes("beta", QStringLiteral("b.txt"), &error)
		&& initial.addBytes(QByteArray(768, 'p'), QStringLiteral("gfx/palette.lmp"), &error), "prepare synthetic package and palette");
	PackageWriteRequest write; write.destinationPath = root.filePath(QStringLiteral("source.pak"));
	ok &= expect(initial.writeArchive(write).succeeded(), "write handoff fixture");
	PackageArchive source;
	ok &= expect(source.load(write.destinationPath, &error), "load handoff source");
	QVector<qsizetype> copiedFiles;
	const auto sourceEntries = source.entries();
	for (qsizetype at = 0; at < sourceEntries.size(); ++at) {
		if (sourceEntries.at(at).virtualPath == QStringLiteral("a.txt") || sourceEntries.at(at).virtualPath == QStringLiteral("b.txt")) { copiedFiles << at; }
	}
	if (!expect(copiedFiles.size() == 2, "resolve exact file identities in the package index")) { return 1; }
	PackageStagingModel plan;
	ok &= expect(plan.loadBaseArchive(source, &error) && plan.addBytes("keep", QStringLiteral("keep.txt"), &error)
		&& plan.createDirectory(QStringLiteral("redo-folder"), &error) && plan.undo(), "prepare dirty document with redo history");
	const auto revision = plan.revision(); const auto operations = plan.operations().size();
	const QString imported = root.filePath(QStringLiteral("import.txt"));
	{ QFile file(imported); ok &= expect(file.open(QIODevice::WriteOnly) && file.write("replacement") == 11, "write staging input"); }
	const QVector<PackageStageFileRequest> files {{imported, QStringLiteral("new.txt")},
		{imported, QStringLiteral("a.txt"), PackageStageConflictResolution::Block, true}};
	for (const int scale : {100, 200}) {
		const bool close = scale == 200; Expanded translator;
		if (close) { application.installTranslator(&translator); }
		applyStudioTheme(application, studioThemeTokens(close ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		application.setLayoutDirection(close ? Qt::RightToLeft : Qt::LeftToRight);
		{
			HandoffGate gate(QStringLiteral("packageStageFilesDialog"), close);
			auto control = gate.control(); auto imports = std::make_shared<PackageImportOptions>();
			imports->directory = root.filePath(QStringLiteral("imports-%1").arg(scale)); control.importOptions = imports;
			const auto result = runPackageStagingDialog(nullptr, plan, files, control);
			QByteArray payload; auto redo = result.staging; const bool redone = redo.redo();
			const auto redoneEntries = redo.plannedEntries();
			const bool restored = std::any_of(redoneEntries.begin(), redoneEntries.end(), [](const auto& entry) {
				return entry.kind == PackageEntryKind::Directory && entry.virtualPath == QStringLiteral("redo-folder");
			});
			const bool preserved = gate.checked() && result.cancelled && result.accepted == 0 && result.acceptedPaths.isEmpty()
				&& result.errors.isEmpty() && result.staging.revision() == revision && result.staging.operations().size() == operations
				&& result.staging.canRedo() && PackageStagingArchive(result.staging, PackageStagingReadMode::InspectPlan).readEntryBytes(QStringLiteral("a.txt"), &payload, &error)
				&& payload == "alpha" && redone && restored;
			if (!preserved) {
				std::cerr << "Stage rollback: cancelled=" << result.cancelled << " accepted=" << result.accepted
					<< " revision=" << result.staging.revision() << '/' << revision << " operations=" << result.staging.operations().size() << '/' << operations
					<< " redo=" << redone << " folder=" << restored << " payload=" << payload.toStdString() << " error=" << error.toStdString() << '\n';
			}
			ok &= expect(preserved, "late staging cancellation preserves payload, revision, operation count and redo");
		}
		{
			HandoffGate gate(QStringLiteral("packageLoadDialog"), close);
			const auto result = runPackageLoadDialog(nullptr, source.sourcePath(), gate.control());
			ok &= expect(gate.checked() && result.cancelled && !result.ready() && !result.archive.isOpen() && !result.staging.isLoaded(),
				"late opening cancellation never exposes an adoptable package");
		}
		{
			HandoffGate gate(QStringLiteral("packagePaletteDialog"), close); bool cancelled = false;
			const auto result = runPackagePaletteDialog(nullptr, {source.sourcePath()}, QStringLiteral("quake"), &cancelled, gate.control());
			ok &= expect(gate.checked() && cancelled && !result.fromPackage && result.sourcePackagePath.isEmpty(),
				"late palette cancellation discards the loaded installation palette");
		}
		for (const bool accept : {false, true}) {
			const QString store = root.filePath(QStringLiteral("copies-%1-%2").arg(scale).arg(accept));
			auto session = PackageCopySession::create(store, &error);
			if (!expect(session && session->isValid(), "create managed copy session")) { return 1; }
			auto budget = std::make_shared<PackageCopyBudget>();
			PackageCopyRequest request; request.session = session; request.budget = budget;
			request.entryIndexes = copiedFiles;
			HandoffGate gate(QStringLiteral("packageCopyDialog"), !accept && close, !accept); request.control = gate.control();
			auto barrier = std::make_shared<std::shared_ptr<const PackageDraftAccess>>();
			bool phaseVisible = false, barrierHeld = false;
			gate.afterWorker = [&](QDialog*) {
				*barrier = PackageDraftAccess::acquire(QDir(store).filePath(QStringLiteral(".coordination")), PackageDraftAccess::Mode::Maintain, &error);
				barrierHeld = *barrier != nullptr;
				QTimer::singleShot(180, [barrier] { barrier->reset(); });
			};
			gate.duringFinalization = [&](QDialog* dialog) {
				if (phaseVisible || !*barrier) { return; }
				auto* summary = dialog->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
				auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				if (!summary || !summary->text().contains(accept ? QStringLiteral("Finishing prepared") : QStringLiteral("Discarding prepared"))) { return; }
				dialog->ensurePolished(); dialog->layout()->activate();
				phaseVisible = cancel && !cancel->isEnabled() && !summary->accessibleName().isEmpty()
					&& summary->height() >= summary->fontMetrics().height() && !dialog->close();
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
				if (!captures.isEmpty()) {
					QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-copy-%1-%2.png").arg(accept ? QStringLiteral("finishing") : QStringLiteral("discarding")).arg(scale))),
						"save copy finalization status render");
				}
			};
			auto result = runPackageCopyDialog(nullptr, std::make_shared<PackageArchive>(source), request);
			const auto quota = inspectPackageCopyQuota(store);
			ok &= expect(gate.checked() && barrierHeld && phaseVisible, "copy finalization stays on a worker with visible status while Close waits for its result");
			if (accept) {
				ok &= expect(result.succeeded() && !result.cancelled && !result.prepared() && bytes(result.paths.first()) == "alpha"
					&& budget->usage().batches == 1 && budget->usage().pendingBatches == 0 && quota.complete()
					&& quota.reserved.batches == 1 && quota.reserved.pendingBatches == 0, "accepted copy handoff retains output and committed reservations through finalization");
			} else {
				ok &= expect(result.cancelled && !result.succeeded() && result.paths.isEmpty() && !result.storage
					&& budget->usage().bytes == 0 && budget->usage().batches == 0 && quota.complete() && quota.reserved.batches == 0
					&& QDir(session->path()).entryList({QStringLiteral("package-copy-*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
					"late copy cancellation removes the prepared batch and releases window/shared reservations before returning");
			}
		}
		{
			HandoffGate gate(QStringLiteral("packageEditDialog"), close);
			const auto result = runPackageEditDialog(nullptr, plan, QStringLiteral("Rename"), [](auto& candidate, QString* failure, const auto& control) {
				return candidate.renameEntry(QStringLiteral("a.txt"), QStringLiteral("renamed.txt"), failure, PackageStageConflictResolution::Block, control);
			}, gate.control());
			ok &= expect(gate.checked() && result.cancelled && !result.ready() && !result.view.isOpen()
				&& result.staging.revision() == revision && result.staging.canRedo(), "existing late edit cancellation keeps its rollback guarantee");
		}
		// Committed outputs remain authoritative even when the UI has not yet
		// displayed completion. A late Cancel must not claim they were undone.
		{
			HandoffGate gate(QStringLiteral("packageExtractionDialog"), close);
			PackageExtractionRequest request; request.targetDirectory = root.filePath(QStringLiteral("extract-%1").arg(scale));
			request.virtualPaths = {QStringLiteral("a.txt")}; request.control = gate.control();
			const auto result = runPackageExtractionDialog(nullptr, std::make_shared<PackageArchive>(source), request);
			ok &= expect(gate.checked() && result.succeeded() && result.writtenCount == 1
				&& bytes(QDir(request.targetDirectory).filePath(QStringLiteral("a.txt"))) == "alpha", "late extraction cancellation preserves committed output and accurate counts");
		}
		{
			HandoffGate gate(QStringLiteral("packageDraftSaveDialog"), close);
			const auto destination = root.filePath(QStringLiteral("saved-%1.vibepackage").arg(scale));
			const auto result = runPackageDraftSaveDialog(nullptr, plan, destination, false, gate.control());
			PackageStagingModel reopened;
			ok &= expect(gate.checked() && result.ready() && !result.cancelled && PackageDraft::load(destination, &reopened, &error)
				&& reopened.canRedo(), "late draft cancellation reports the committed draft and retains history");
		}
		{
			HandoffGate gate(QStringLiteral("packageSaveDialog"), close);
			PackageWriteRequest request; request.destinationPath = root.filePath(QStringLiteral("saved-%1.zip").arg(scale));
			request.byteProgress = [&](PackageWritePhase, const QString&, quint64, quint64) { gate.progress(); };
			bool done = false; PackageSaveResult result;
			QPointer<QDialog> dialog = showPackageSaveDialog(nullptr, plan, request, [&](const auto& saved) { result = saved; done = true; });
			ok &= expect(until([&] { return done; }) && gate.checked() && result.ready() && result.report.outputCommitted
				&& !result.report.cancelled && QFileInfo::exists(request.destinationPath), "late save cancellation reports and reopens the committed package");
			if (dialog) { dialog->close(); }
		}
		if (close) { application.removeTranslator(&translator); }
	}
	// Positive controls establish that discarding late results does not turn
	// ordinary successful load/stage/palette/copy operations into cancellation.
	ok &= expect(runPackageLoadDialog(nullptr, source.sourcePath()).ready(), "uncancelled opening still succeeds");
	const auto staged = runPackageStagingDialog(nullptr, plan, files);
	ok &= expect(!staged.cancelled && staged.accepted == 2 && staged.staging.revision() != revision, "uncancelled staging still adopts both operations");
	bool cancelled = false;
	ok &= expect(runPackagePaletteDialog(nullptr, {source.sourcePath()}, QStringLiteral("quake"), &cancelled).fromPackage && !cancelled,
		"uncancelled palette loading retains provenance");
	PackageCopyRequest copy; copy.parentDirectory = root.canonicalPath(); copy.entryIndexes = {copiedFiles.first()};
	const auto copied = runPackageCopyDialog(nullptr, std::make_shared<PackageArchive>(source), copy);
	ok &= expect(copied.succeeded() && bytes(copied.paths.first()) == "alpha", "uncancelled copies retain usable output ownership");
	ok &= expect(plan.revision() == revision && plan.operations().size() == operations && plan.canRedo(), "every worker preserves the caller's original document");
	return ok ? 0 : 1;
}
