#include "package_entry_test_helpers.h"
#include "app/package_operation_dialog.h"
#include "app/application_shell.h"
#include "app/package_folder_view.h"
#include "app/studio_theme.h"
#include "core/package_publication.h"
#include "core/package_draft.h"
#include "package_legacy_test_helpers.h"
#include "core/package_import_store.h"
#include "core/idtech_image.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <utility>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}

bool exactByteProgress(QProgressBar* progress, quint64 completed, quint64 total)
{
	return progress && !progress->toolTip().isEmpty()
		&& progress->toolTip().contains(progress->locale().toString(completed))
		&& (total == 0 || progress->toolTip().contains(progress->locale().toString(total)))
		&& progress->accessibleDescription() == progress->format() + QLatin1Char('\n') + progress->toolTip();
}

bool waitForPreview(QPlainTextEdit* preview, const QString& text)
{
	QElapsedTimer timer; timer.start();
	while (preview->toPlainText() != text && timer.elapsed() < 10000) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return preview->toPlainText() == text;
}

bool waitForOperation(QDialog* dialog)
{
	QEventLoop loop;
	QTimer poll;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (!dialog->property("operationRunning").toBool()) { loop.quit(); }
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(15000);
	loop.exec();
	return !dialog->property("operationRunning").toBool();
}

class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "VibeStudioPackageDialog"
			&& !(QByteArray(context) == "VibeStudioPackageStaging" && (QByteArray(source).startsWith("Package plan exceeds")
				|| QByteArray(source) == "Retaining package source protections"))) { return {}; }
		return QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source));
	}
};

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

int main(int argc, char** argv)
{
	// Widget rendering and property changes only: no OS capture or input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication application(argc, argv);
#ifdef Q_OS_WIN
	application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	const auto drainCleanup = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup(); });
	if (!temp.isValid()) { return 1; }
	QDir root(temp.path());
	root.mkpath(QStringLiteral("source"));
	PackageArchive folder;
	QString error;
	bool ok = expect(folder.load(root.filePath(QStringLiteral("source")), &error), "open empty source");
	PackageStagingModel initial;
	ok &= expect(initial.loadBaseArchive(folder, &error), "prepare source");
	ok &= expect(initial.addBytes("alpha", QStringLiteral("a.txt"), &error)
		&& initial.addBytes("unchanged payload", QStringLiteral("b.txt"), &error), "stage fixture bytes");
	PackageWriteRequest first;
	first.destinationPath = root.filePath(QStringLiteral("source.pak"));
	ok &= expect(initial.writeArchive(first).succeeded(), "write fixture archive");
	PackageArchive source;
	ok &= expect(source.load(first.destinationPath, &error), "open fixture archive");
	PackageStagingModel plan;
	ok &= expect(plan.loadBaseArchive(source, &error), "load editable plan");
	ok &= expect(plan.addBytes("a much longer replacement", QStringLiteral("a.txt"), &error, PackageStageConflictResolution::ReplaceExisting), "stage changed offsets");
	ok &= expect(plan.addBytes("new file", QStringLiteral("new.cfg"), &error), "stage new entry");
	{
		// Cancel from a UI timer only once the worker reaches each index phase.
		// The gate proves the modal loop keeps dispatching during indexing itself.
		for (const QString& phase : {QStringLiteral("Indexing "), QStringLiteral("Preparing package index"),
			QStringLiteral("Reading package base metadata"), QStringLiteral("Retaining package source protections"), QStringLiteral("Ordering package base entries")}) {
			std::mutex mutex;
			std::condition_variable gate;
			std::atomic_bool reached {false}, cancelled {false}, onWorker {false};
			bool tick = false;
			PackageReadControl control;
			control.isCancelled = [&] { return cancelled.load(); };
			control.progress = [&](const QString& current, qint64, qint64) {
				if (!current.startsWith(phase)) { return; }
				onWorker = QThread::currentThread() != application.thread();
				reached = true;
				std::unique_lock lock(mutex);
				gate.wait_for(lock, std::chrono::seconds(5), [&] { return tick; });
			};
			QTimer timer;
			QObject::connect(&timer, &QTimer::timeout, [&] {
				if (!reached.load()) { return; }
				{ std::lock_guard lock(mutex); tick = true; cancelled = true; }
				gate.notify_all();
			});
			timer.start(5);
			const auto result = runPackageLoadDialog(nullptr, source.sourcePath(), control);
			ok &= expect(tick && onWorker && result.cancelled && !result.ready() && !result.error.isEmpty()
				&& !result.archive.isOpen() && result.archive.entries().isEmpty() && !result.staging.isLoaded(),
				"indexing and finalization remain on a cancellable worker while the modal UI dispatches events");
		}
		QByteArray oversized("PWAD");
		const auto append32 = [&](quint32 value) { for (int shift = 0; shift < 32; shift += 8) { oversized.append(static_cast<char>(value >> shift)); } };
		append32(PackageIndexLimits::entryCeiling + 1); append32(12);
		oversized += QByteArray((PackageIndexLimits::entryCeiling + 1) * 16, '\0');
		const QString path = root.filePath(QStringLiteral("index-limit.wad"));
		{ QFile file(path); ok &= expect(file.open(QIODevice::WriteOnly) && file.write(oversized) == oversized.size(), "create GUI index admission fixture"); }
		const auto rejected = runPackageLoadDialog(nullptr, path);
		ok &= expect(!rejected.ready() && !rejected.cancelled && rejected.error.contains(QStringLiteral("250000"))
			&& !rejected.archive.isOpen() && rejected.archive.entries().isEmpty() && !rejected.staging.isLoaded(),
			"opening an over-limit package returns an actionable error without an adoptable partial document");
	}
	{
		// The modal API must still dispatch UI events while disk work happens
		// on another thread. No native input or screen capture is involved.
		std::mutex readMutex;
		std::condition_variable readGate;
		bool uiTick = false;
		std::atomic_bool readOnWorker {false};
		PackageReadControl control;
		auto importOptions = std::make_shared<PackageImportOptions>();
		importOptions->directory = root.filePath(QStringLiteral("worker-imports"));
		control.importOptions = importOptions;
		control.progress = [&](const QString&, qint64, qint64) {
			readOnWorker = QThread::currentThread() != application.thread();
			std::unique_lock lock(readMutex);
			readGate.wait_for(lock, std::chrono::seconds(5), [&]() { return uiTick; });
		};
		QTimer::singleShot(0, [&]() { { std::lock_guard lock(readMutex); uiTick = true; } readGate.notify_all(); });
		const auto loaded = runPackageLoadDialog(nullptr, source.sourcePath(), control);
		ok &= expect(loaded.ready() && uiTick && readOnWorker && loaded.archive.contentId() == source.contentId(),
			"opening must fingerprint on a worker while the modal event loop remains responsive");
		const QString localFile = root.filePath(QStringLiteral("staged-input.txt"));
		QFile local(localFile);
		ok &= expect(local.open(QIODevice::WriteOnly) && local.write(QByteArray(200000, 'x')) == 200000, "create staging worker fixture");
		local.close();
		const QString secondFile = root.filePath(QStringLiteral("second-input.txt"));
		QFile second(secondFile);
		ok &= expect(second.open(QIODevice::WriteOnly) && second.write(QByteArray(200000, 'x')) == 200000, "create second staging worker fixture"); second.close();
		const QVector<PackageStageFileRequest> files {{localFile, QStringLiteral("first.txt")}, {secondFile, QStringLiteral("second.txt")}};
		{
			PackageStagingMetadataLimits limits; limits.maximumRecords = 0;
			PackageStagingModel bounded({}, limits);
			ok &= expect(bounded.createEmpty(PackageArchiveFormat::Zip, {}, &error), "prepare a document with no edit-record capacity");
			const auto boundedRevision = bounded.revision();
			PackageReadControl admissionControl; admissionControl.importOptions = importOptions;
			std::atomic_int reads {0}, secondReads {0};
			admissionControl.progress = [&](const QString& path, qint64, qint64) { ++reads; if (path == secondFile) { ++secondReads; } };
			const auto refused = runPackageStagingDialog(nullptr, bounded, files, admissionControl);
			ok &= expect(!refused.cancelled && refused.accepted == 0 && refused.acceptedPaths.isEmpty()
				&& refused.errors.size() == 1 && refused.errors.first().contains(QStringLiteral("0-record"))
				&& reads == 0 && refused.staging.operations().isEmpty() && refused.staging.revision() == boundedRevision
				&& !refused.staging.canUndo() && !refused.staging.canRedo(),
				"GUI worker reports group-capacity refusal before source reads without changing document/history");
			limits.maximumRecords = 4;
			bounded = PackageStagingModel({}, limits);
			ok &= expect(bounded.createEmpty(PackageArchiveFormat::Zip, {}, &error), "prepare one-file group capacity");
			auto partial = runPackageStagingDialog(nullptr, bounded, files, admissionControl);
			PackageStagingMetadataUsage retained;
			ok &= expect(!partial.cancelled && partial.accepted == 1 && partial.acceptedPaths == QStringList{QStringLiteral("first.txt")}
				&& partial.errors.size() == 1 && partial.errors.first().contains(QStringLiteral("4-record"))
				&& reads > 0 && secondReads == 0 && bounded.operations().isEmpty() && bounded.revision() == boundedRevision
				&& partial.staging.metadataUsage(&retained, &error) && retained.records == 4,
				"GUI file batch reports its accepted subset and refuses the next file before reading it at capacity");
			ok &= expect(partial.staging.undo() && partial.staging.operations().isEmpty()
				&& partial.staging.redo() && partial.staging.operations().size() == 1,
				"a partially accepted worker batch finishes its group and supports Undo/Redo at capacity");
		}
		for (const int stageScale : {0, 100, 200}) {
			const bool cancelView = stageScale != 0;
			ExpandedTranslator stageTranslator;
			if (stageScale == 200) { application.installTranslator(&stageTranslator); }
			applyStudioTheme(application, studioThemeTokens(stageScale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark,
				UiDensity::Standard, stageScale == 200 ? 200 : 100));
			application.setLayoutDirection(stageScale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			PackageIndexLimits limits; limits.maximumEntries = cancelView ? 4 : 2;
			PackageStagingModel bounded({}, {}, {}, limits); bounded.createEmpty(PackageArchiveFormat::Zip);
			bounded.addBytes({}, "keep"); bounded.addBytes({}, "redo"); bounded.undo();
			const auto revision = bounded.revision(); const auto redo = bounded.redoLabel();
			std::atomic_bool stop{false}, onWorker{false}, reachedView{false};
			std::mutex viewMutex; std::condition_variable viewGate; bool releaseView = false, progressVisible = false;
			QTimer viewPoll;
			QObject::connect(&viewPoll, &QTimer::timeout, &application, [&] {
				if (!cancelView || !reachedView.load()) { return; }
				for (auto* window : QApplication::topLevelWidgets()) {
					if (window->objectName() != "packageStageFilesDialog") { continue; }
					auto* progress = window->findChild<QProgressBar*>();
					auto* summary = window->findChild<QLabel*>("packageOperationSummary");
					auto* cancel = window->findChild<QPushButton*>("cancelPackageOperation");
					if (!progress || !summary || !summary->text().contains("Checking staged package")
						|| !summary->text().contains("Records checked:")) { continue; }
					// The progress timer changes a one-line label to two lines. Its
					// LayoutRequest can still be queued when this polling timer fires.
					window->ensurePolished();
					if (window->layout()) { window->layout()->activate(); }
					progressVisible = progress->format().contains("Records checked:") && progress->accessibleDescription().contains("Records checked:")
						&& summary->height() >= 2 * summary->fontMetrics().height()
						&& cancel && cancel->isEnabled() && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty();
					const auto captureRoot = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
					if (!captureRoot.isEmpty()) {
						QImage image(window->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); window->render(&image);
						ok &= expect(image.save(QDir(captureRoot).filePath(QStringLiteral("package-stage-admission-%1.png").arg(stageScale))),
							"Capture staged-view admission with record progress at both text scales.");
					}
					viewPoll.stop(); stop = true;
					{ std::lock_guard lock(viewMutex); releaseView = true; } viewGate.notify_all();
				}
			});
			viewPoll.start(10);
			PackageReadControl admission; admission.importOptions = importOptions;
			admission.isCancelled = [&] { return stop.load(); };
			admission.progress = [&](const QString& phase, qint64, qint64) {
				if (phase == "Preparing package snapshot") {
					reachedView = true; onWorker = QThread::currentThread() != application.thread();
					if (cancelView) { std::unique_lock lock(viewMutex); viewGate.wait_for(lock, std::chrono::seconds(5), [&] { return releaseView; }); stop = true; }
				}
			};
			const auto refused = runPackageStagingDialog(nullptr, bounded, files, admission);
			const bool rolledBack = refused.cancelled == cancelView && refused.accepted == 0 && refused.acceptedPaths.isEmpty()
				&& refused.staging.operations().size() == 1 && refused.staging.revision() == revision
				&& refused.staging.canRedo() && refused.staging.redoLabel() == redo
				&& (cancelView ? reachedView && onWorker && progressVisible && refused.errors.isEmpty()
					: refused.errors.size() == 1 && refused.errors.first().contains("snapshot"));
			if (!rolledBack) {
				std::cerr << "Staged-view check at " << stageScale << "%: cancelled=" << refused.cancelled
					<< " accepted=" << refused.accepted << " paths=" << refused.acceptedPaths.size()
					<< " operations=" << refused.staging.operations().size() << " revision=" << refused.staging.revision()
					<< '/' << revision << " redo=" << refused.staging.canRedo() << " redo-label=" << (refused.staging.redoLabel() == redo)
					<< " reached=" << reachedView.load() << " worker=" << onWorker.load() << " visible=" << progressVisible
					<< " errors=" << refused.errors.join(QLatin1Char('\n')).toStdString() << '\n';
			}
			ok &= expect(rolledBack, "Worker view refusal or cancellation rolls back every accepted file and preserves the original redo branch.");
			if (stageScale == 200) { application.removeTranslator(&stageTranslator); }
		}
		application.setLayoutDirection(Qt::LeftToRight);
		applyStudioTheme(application, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
		const auto staged = runPackageStagingDialog(nullptr, plan, files, control);
		ok &= expect(!staged.cancelled && staged.accepted == 2 && staged.acceptedPaths.size() == 2
			&& staged.staging.operations().size() == plan.operations().size() + 2 && readOnWorker,
			"a successful worker staging batch returns its captured sources without mutating the caller");
		if (staged.accepted != 2) { std::cerr << staged.errors.join('\n').toStdString() << '\n'; return 1; }
		ok &= expect(staged.staging.operations().last().sourceIdentity->storage && QFile::remove(localFile) && QFile::remove(secondFile),
			"accepted worker imports own copies before original files disappear");
		QByteArray retainedBytes;
		ok &= expect(PackageStagingArchive(staged.staging).readEntryBytes(QStringLiteral("first.txt"), &retainedBytes, &error)
			&& retainedBytes == QByteArray(200000, 'x'), "staged preview reads the independent import after source removal");
		auto grouped = staged.staging;
		ok &= expect(grouped.undo() && grouped.operations().size() == plan.operations().size() && grouped.redo(), "file staging batch is one undo group");
		uiTick = false; readOnWorker = false;
		QTimer::singleShot(0, [&]() { { std::lock_guard lock(readMutex); uiTick = true; } readGate.notify_all(); });
		const QString draftPath = root.filePath(QStringLiteral("worker.vibepackage"));
		const auto draftSaved = runPackageDraftSaveDialog(nullptr, staged.staging, draftPath, false, control);
		ok &= expect(draftSaved.ready() && uiTick && readOnWorker && !draftSaved.staging.isModified(), "draft save streams on a worker while dispatching UI events");
		const auto draftLoaded = runPackageLoadDialog(nullptr, draftPath, control);
		ok &= expect(draftLoaded.ready() && draftLoaded.staging.canUndo() && draftLoaded.staging.operations().size() == staged.staging.operations().size(), "draft worker reopens content and history");
		PackageExtractionRequest extraction;
		extraction.targetDirectory = root.filePath(QStringLiteral("worker-extracted"));
		extraction.virtualPaths = {QStringLiteral("first.txt")};
		std::atomic_bool extractionCancel {false};
		uiTick = false; readOnWorker = false;
		extraction.control.isCancelled = [&]() { return extractionCancel.load(); };
		extraction.control.progress = [&](const QString&, qint64 bytes, qint64) {
			readOnWorker = QThread::currentThread() != application.thread();
			std::unique_lock lock(readMutex);
			readGate.wait_for(lock, std::chrono::seconds(5), [&]() { return uiTick; });
			if (bytes >= 65536) { extractionCancel = true; }
		};
		QTimer::singleShot(0, [&]() { { std::lock_guard lock(readMutex); uiTick = true; } readGate.notify_all(); });
		const auto extracted = runPackageExtractionDialog(nullptr, std::make_shared<PackageStagingArchive>(staged.staging), extraction);
		ok &= expect(extracted.cancelled && extracted.bytesRead == 65536 && extracted.writtenCount == 0 && uiTick && readOnWorker
			&& !QFileInfo::exists(QDir(extraction.targetDirectory).filePath(QStringLiteral("first.txt"))),
			"extraction must remain responsive and cancel within a file without committing partial output");
		extraction.control = {};
		ok &= expect(runPackageExtractionDialog(nullptr, std::make_shared<PackageStagingArchive>(staged.staging), extraction).succeeded()
			&& readFile(QDir(extraction.targetDirectory).filePath(QStringLiteral("first.txt"))) == QByteArray(200000, 'x'),
			"worker extraction can retry the cancelled file from the same staged snapshot");
		ok &= expect(local.open(QIODevice::WriteOnly) && local.write(QByteArray(200000, 'x')) == 200000, "recreate cancellation source"); local.close();
		ok &= expect(second.open(QIODevice::WriteOnly) && second.write(QByteArray(200000, 'x')) == 200000, "recreate second cancellation source"); second.close();
		std::atomic_bool cancelRead {false};
		control.isCancelled = [&]() { return cancelRead.load(); };
		control.progress = [&](const QString& path, qint64 bytes, qint64) {
			if (path == secondFile && bytes > 0) { cancelRead = true; }
		};
		const auto cancelledStage = runPackageStagingDialog(nullptr, plan, files, control);
		ok &= expect(cancelledStage.cancelled && cancelledStage.accepted == 0 && cancelledStage.acceptedPaths.isEmpty()
			&& cancelledStage.staging.operations().size() == plan.operations().size(),
			"cancelling within the second input must roll back the entire batch and preserve the prior plan");
		control.progress = {};
		const auto cancelledLoad = runPackageLoadDialog(nullptr, source.sourcePath(), control);
		ok &= expect(cancelledLoad.cancelled && !cancelledLoad.ready(), "cancelled opening cannot be adopted as a new package");
		const auto failedLoad = runPackageLoadDialog(nullptr, root.filePath(QStringLiteral("missing.pak")));
		ok &= expect(!failedLoad.ready() && !failedLoad.error.isEmpty() && !failedLoad.cancelled, "failed opening must provide a distinct error result");
		bool paletteCancelled = false;
		const auto palette = runPackagePaletteDialog(nullptr, {source.sourcePath()}, QStringLiteral("quake"), &paletteCancelled, control);
		ok &= expect(paletteCancelled && !palette.fromPackage, "installation palette reads share cancellable worker loading");
		PackageStagingModel palettePlan = initial;
		ok &= expect(palettePlan.addBytes(QByteArray(768, '\x7f'), QStringLiteral("gfx/palette.lmp"), &error), "create synthetic installation palette");
		PackageWriteRequest paletteWrite; paletteWrite.destinationPath = root.filePath(QStringLiteral("palette.pak"));
		ok &= expect(palettePlan.writeArchive(paletteWrite).succeeded(), "write synthetic palette package");
		control.isCancelled = {};
		readOnWorker = false;
		control.progress = [&](const QString&, qint64, qint64) { readOnWorker = QThread::currentThread() != application.thread(); };
		const auto loadedPalette = runPackagePaletteDialog(nullptr, {paletteWrite.destinationPath}, QStringLiteral("quake"), &paletteCancelled, control);
		ok &= expect(!paletteCancelled && readOnWorker && loadedPalette.fromPackage && loadedPalette.sourcePackagePath == paletteWrite.destinationPath,
			"installation palette lookup resolves captured content and provenance on its worker");
	}
	// Edits prepare both the mutable plan cache and immutable browser snapshot
	// on the worker. Gate the final projection to exercise real UI cancellation.
	{
		PackageStagingModel editable; editable.createEmpty(PackageArchiveFormat::Zip);
		editable.beginOperationGroup(QStringLiteral("fixture"));
		for (int at = 0; at < 512; ++at) {
			editable.addBytes(QByteArray::number(at), QStringLiteral("source/%1/file.txt").arg(at));
		}
		editable.endOperationGroup();
		editable.createDirectory(QStringLiteral("redo-folder")); editable.undo();
		const auto originalRevision = editable.revision(); const auto originalCount = editable.operations().size();
		for (const int scale : {100, 200}) {
			ExpandedTranslator editTranslator;
			if (scale == 200) { application.installTranslator(&editTranslator); }
			application.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			applyStudioTheme(application, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
			std::atomic_bool operationComplete{false}, projectionReached{false}, released{false}, workerOnly{true};
			std::mutex editMutex; std::condition_variable editGate;
			PackageReadControl editControl;
			editControl.progress = [&](const QString&, qint64, qint64) {
				if (QThread::currentThread() == application.thread()) { workerOnly = false; }
				if (!operationComplete.load() || projectionReached.exchange(true)) { return; }
				std::unique_lock lock(editMutex);
				editGate.wait_for(lock, std::chrono::seconds(5), [&] { return released.load(); });
			};
			int ticks = 0; bool observed = false;
			QElapsedTimer elapsed; elapsed.start(); QTimer poll;
			QObject::connect(&poll, &QTimer::timeout, [&] {
				++ticks;
				if (!projectionReached.load() || observed || elapsed.elapsed() < 180) { return; }
				auto* dialog = qobject_cast<QDialog*>(application.activeModalWidget());
				if (!dialog || dialog->objectName() != QStringLiteral("packageEditDialog")) { return; }
				observed = true;
				auto* summary = dialog->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
				auto* progress = dialog->findChild<QProgressBar*>();
				auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(summary && summary->text().contains("Checking staged package") && summary->text().contains("Records checked:")
					&& progress && progress->accessibleDescription().contains("Records checked:")
					&& cancel && cancel->isEnabled() && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty()
					&& dialog->layoutDirection() == application.layoutDirection(), "Package edits show accessible metadata progress while the UI remains responsive.");
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
				if (!captures.isEmpty()) {
					QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-edit-worker-%1.png").arg(scale))), "render edit worker progress");
				}
				if (scale == 200 && cancel) { cancel->click(); }
				released = true; editGate.notify_all();
			});
			poll.start(10);
			const auto edited = runPackageEditDialog(nullptr, editable, QStringLiteral("Rename folder"),
				[&operationComplete](auto& candidate, QString* failure, const auto& control) {
					const bool renamed = candidate.renameDirectory(QStringLiteral("source"), QStringLiteral("renamed"), failure, control);
					operationComplete = true; return renamed;
				}, editControl);
			poll.stop();
			ok &= expect(observed && ticks > 1 && workerOnly && editable.revision() == originalRevision
				&& editable.operations().size() == originalCount && editable.canRedo(), "Worker preparation never mutates the live document or its redo branch.");
			if (scale == 200) {
				ok &= expect(edited.cancelled && !edited.ready() && edited.error.isEmpty() && !edited.view.isOpen()
					&& edited.staging.revision() == originalRevision && edited.staging.operations().size() == originalCount && edited.staging.canRedo(),
					"Cancelling the final browser projection restores the complete original edit history.");
				application.removeTranslator(&editTranslator);
			} else {
				QByteArray bytes;
				ok &= expect(edited.ready() && edited.view.isOpen() && edited.view.readEntryBytes(QStringLiteral("renamed/0/file.txt"), &bytes, &error) && bytes == "0",
					"A completed edit returns the prepared browser and original payload ownership.");
				bool replayed = false; PackageReadControl cached; cached.progress = [&](const QString&, qint64, qint64) { replayed = true; };
				ok &= expect(edited.staging.preparePlan(&error, cached) && !replayed, "Adopting a prepared edit does not replay its plan on the UI thread.");
				auto undone = runPackageEditDialog(nullptr, edited.staging, QStringLiteral("Undo"), [](auto& candidate, QString*, const auto&) { return candidate.undo(); });
				ok &= expect(undone.ready() && undone.staging.revision() == originalRevision && undone.view.readEntryBytes(QStringLiteral("source/0/file.txt"), &bytes, &error),
					"Undo prepares its restored view on the same worker path.");
				replayed = false; ok &= expect(undone.staging.preparePlan(&error, cached) && !replayed, "Undo returns a prepared plan cache.");
			}
		}
		application.setLayoutDirection(Qt::LeftToRight);
		applyStudioTheme(application, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
		const auto refused = runPackageEditDialog(nullptr, editable, QStringLiteral("Invalid rename"), [](auto& candidate, QString* failure, const auto& control) {
			return candidate.renameDirectory(QStringLiteral("source"), QStringLiteral("source/child"), failure, control);
		});
		ok &= expect(!refused.ready() && !refused.cancelled && !refused.error.isEmpty() && refused.staging.revision() == originalRevision && refused.staging.canRedo(),
			"An invalid edit preserves the current model and redo branch.");
		bool signalled = false; PackageReadControl singleSignal;
		singleSignal.isCancelled = [&] { return !std::exchange(signalled, true); };
		bool invoked = false;
		const auto preCancelled = runPackageEditDialog(nullptr, editable, QStringLiteral("Cancel before edit"),
			[&](auto&, QString*, const auto&) { invoked = true; return true; }, singleSignal);
		ok &= expect(preCancelled.cancelled && !invoked && preCancelled.staging.revision() == originalRevision && preCancelled.staging.canRedo(),
			"A one-shot cancellation remains latched before the edit callback starts.");
		const auto legacyPath = root.filePath(QStringLiteral("edit-history.vibepackage"));
		PackageStagingModel legacy;
		ok &= expect(tests::saveLegacyDeepPackageDraft(legacyPath, &error) && PackageDraft::load(legacyPath, &legacy, &error) && legacy.undo(), "load legacy history for worker recovery");
		const auto legacyRedo = runPackageEditDialog(nullptr, legacy, QStringLiteral("Redo"), [](auto& candidate, QString*, const auto&) { return candidate.redo(); });
		ok &= expect(legacyRedo.ready() && !legacyRedo.view.isOpen() && legacyRedo.view.errorString().contains("depth") && legacyRedo.staging.canUndo(),
			"Redo can revisit an unavailable historical view without losing its diagnostic or Undo.");
	}
	ExpandedTranslator translator;
	{
		PackageValidationReport validation;
		auto* dialog = showPackageValidationDialog(nullptr, source, [&](const auto& result) { validation = result; });
		ok &= expect(waitForOperation(dialog) && validation.valid() && validation.verifiedCount == 2, "background validation must verify every source file");
		auto* details = dialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"));
		auto* exportButton = dialog->findChild<QPushButton*>(QStringLiteral("exportPackageValidation"));
		ok &= expect(details && !details->accessibleName().isEmpty() && exportButton && exportButton->isEnabled() && exportButton->focusPolicy() != Qt::NoFocus, "validation report must be accessible and exportable");
		delete dialog;
		PackageValidationRequest request;
		request.isCancelled = []() { return true; };
		dialog = showPackageValidationDialog(nullptr, source, [&](const auto& result) { validation = result; }, request);
		ok &= expect(waitForOperation(dialog) && validation.cancelled && !validation.valid(), "cancelled validation cannot show success");
		delete dialog;
	}
	for (int scale : {100, 200}) {
		applyStudioTheme(application, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		if (scale == 200) { application.installTranslator(&translator); }
		PackageCompareResult comparison;
		auto* dialog = showPackageComparisonDialog(nullptr, source, QString(), &plan, [&](const auto& result) { comparison = result; });
		if (scale == 200) { dialog->setLayoutDirection(Qt::RightToLeft); }
		ok &= expect(waitForOperation(dialog), "background comparison should complete");
		ok &= expect(comparison.completed && comparison.summary.changedCount == 1 && comparison.summary.addedCount == 1, "review must show actual staged changes");
		auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("packageComparisonEntries"));
		auto* filter = dialog->findChild<QLineEdit*>(QStringLiteral("packageComparisonFilter"));
		auto* changes = dialog->findChild<QCheckBox*>(QStringLiteral("packageComparisonChangesOnly"));
		ok &= expect(tree && tree->topLevelItemCount() == 3 && tree->focusPolicy() != Qt::NoFocus && !tree->accessibleName().isEmpty(), "comparison results must be keyboard-focusable and named");
		ok &= expect(filter->focusPolicy() != Qt::NoFocus && changes->focusPolicy() != Qt::NoFocus, "review controls must support keyboard navigation");
		for (int column = 0; column < tree->columnCount(); ++column) {
			ok &= expect(tree->columnWidth(column) >= tree->header()->sectionSizeHint(column),
				"translated headers must fit their columns at every tested scale");
		}
		changes->setChecked(false);
		filter->setText(QStringLiteral("b.txt"));
		int visible = 0;
		for (int i = 0; i < tree->topLevelItemCount(); ++i) { visible += !tree->topLevelItem(i)->isHidden(); }
		ok &= expect(visible == 1, "review filter should find unchanged files");
		changes->setChecked(true);
		visible = 0;
		for (int i = 0; i < tree->topLevelItemCount(); ++i) { visible += !tree->topLevelItem(i)->isHidden(); }
		ok &= expect(visible == 0, "changes filter should hide known identical contents");
		filter->clear();
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog->render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-review-%1.png").arg(scale))), "save widget render evidence");
		}
		delete dialog;
		PackageValidationReport validation;
		dialog = showPackageValidationDialog(nullptr, source, [&](const auto& result) { validation = result; });
		if (scale == 200) { dialog->setLayoutDirection(Qt::RightToLeft); }
		ok &= expect(waitForOperation(dialog) && validation.valid(), "validation must complete at every tested scale");
		if (!captures.isEmpty()) {
			QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-validation-%1.png").arg(scale))), "save validation widget evidence");
		}
		delete dialog;
		std::mutex loadMutex;
		std::condition_variable loadGate;
		bool releaseLoad = false;
		PackageReadControl loadControl;
		loadControl.progress = [&](const QString&, qint64, qint64) {
			std::unique_lock lock(loadMutex);
			loadGate.wait_for(lock, std::chrono::seconds(5), [&]() { return releaseLoad; });
		};
		QTimer::singleShot(0, [&]() {
			auto* loading = qobject_cast<QDialog*>(QApplication::activeModalWidget());
			ok &= expect(loading && loading->objectName() == QStringLiteral("packageLoadDialog"), "opening exposes a modal progress dialog");
			if (loading) {
				if (scale == 200) { loading->setLayoutDirection(Qt::RightToLeft); }
				auto* cancel = loading->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(cancel && cancel->isEnabled() && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty(),
					"source loading has an accessible keyboard-focusable cancel action at both scales");
				if (!captures.isEmpty()) {
					QImage image(loading->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); loading->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-opening-%1.png").arg(scale))), "save opening widget evidence");
				}
				ok &= expect(!loading->close(), "a close request must keep the loading dialog alive until cancellation finishes");
			}
			{ std::lock_guard lock(loadMutex); releaseLoad = true; }
			loadGate.notify_all();
		});
		const auto closedLoad = runPackageLoadDialog(nullptr, source.sourcePath(), loadControl);
		ok &= expect(closedLoad.cancelled && !closedLoad.ready(), "close cancels the source worker without publishing a partial package");
		releaseLoad = false;
		QTimer::singleShot(0, [&]() {
			auto* staging = qobject_cast<QDialog*>(QApplication::activeModalWidget());
			ok &= expect(staging && staging->objectName() == QStringLiteral("packageStageFilesDialog"), "import retention exposes modal progress");
			if (staging) {
				if (scale == 200) { staging->setLayoutDirection(Qt::RightToLeft); }
				auto* cancel = staging->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				auto* summary = staging->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
				ok &= expect(cancel && cancel->isEnabled() && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty()
					&& summary && summary->wordWrap() && !summary->accessibleName().isEmpty() && summary->text().contains(QStringLiteral("Retaining")),
					"retention status wraps and exposes accessible cancellation at both scales");
				if (!captures.isEmpty()) {
					QImage image(staging->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); staging->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-import-%1.png").arg(scale))), "save import widget evidence");
				}
				ok &= expect(!staging->close(), "closing import retention waits for its worker");
			}
			{ std::lock_guard lock(loadMutex); releaseLoad = true; }
			loadGate.notify_all();
		});
		const auto closedStage = runPackageStagingDialog(nullptr, plan, {{source.sourcePath(), QStringLiteral("snapshot.pak")}}, loadControl);
		ok &= expect(closedStage.cancelled && closedStage.accepted == 0 && closedStage.staging.operations().size() == plan.operations().size(),
			"close cancels import retention without adopting a partial edit");
		releaseLoad = false;
		PackageExtractionRequest extraction; extraction.extractAll = true;
		extraction.targetDirectory = root.filePath(QStringLiteral("extract-close-%1").arg(scale));
		extraction.control = loadControl;
		QTimer::singleShot(0, [&]() {
			auto* extracting = qobject_cast<QDialog*>(QApplication::activeModalWidget());
			ok &= expect(extracting && extracting->objectName() == QStringLiteral("packageExtractionDialog"), "extraction exposes owned modal progress");
			if (extracting) {
				if (scale == 200) { extracting->setLayoutDirection(Qt::RightToLeft); }
				auto* cancel = extracting->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(cancel && cancel->isEnabled() && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty(), "extraction cancel must be accessible at both scales");
				if (!captures.isEmpty()) {
					QImage image(extracting->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); extracting->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-extraction-%1.png").arg(scale))), "save extraction widget evidence");
				}
				ok &= expect(!extracting->close(), "closing extraction must wait for the worker to stop");
			}
			{ std::lock_guard lock(loadMutex); releaseLoad = true; }
			loadGate.notify_all();
		});
		const auto closedExtraction = runPackageExtractionDialog(nullptr, std::make_shared<PackageArchive>(source), extraction);
		ok &= expect(closedExtraction.cancelled && closedExtraction.writtenCount == 0, "closing extraction cancels its current file before commit");
		releaseLoad = false;
		QTimer::singleShot(0, [&]() {
			auto* savingDraft = qobject_cast<QDialog*>(QApplication::activeModalWidget());
			ok &= expect(savingDraft && savingDraft->objectName() == QStringLiteral("packageDraftSaveDialog"), "draft save exposes owned modal progress");
			if (savingDraft) {
				if (scale == 200) { savingDraft->setLayoutDirection(Qt::RightToLeft); }
				auto* cancel = savingDraft->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(cancel && cancel->isEnabled() && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty(), "draft cancel is accessible at both scales");
				if (!captures.isEmpty()) {
					QImage image(savingDraft->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); savingDraft->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-draft-%1.png").arg(scale))), "save draft widget evidence");
				}
				ok &= expect(!savingDraft->close(), "closing draft save waits for cancellation");
			}
			{ std::lock_guard lock(loadMutex); releaseLoad = true; }
			loadGate.notify_all();
		});
		const QString cancelledDraft = root.filePath(QStringLiteral("cancel-%1.vibepackage").arg(scale));
		const auto closedDraft = runPackageDraftSaveDialog(nullptr, plan, cancelledDraft, false, loadControl);
		ok &= expect(closedDraft.cancelled && !closedDraft.ready() && !QFileInfo::exists(QDir(cancelledDraft).filePath(QStringLiteral("document.json"))), "cancelled draft save never commits a partial manifest");
		{
			std::mutex mutex; std::condition_variable gate; std::atomic_bool reached {false}; bool release = false, checked = false;
			PackageReadControl protectionControl;
			protectionControl.progress = [&](const QString& phase, qint64, qint64) {
				if (!phase.contains("Retaining package source protections")) { return; }
				reached = true; std::unique_lock lock(mutex);
				gate.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
			};
			QTimer timer;
			QObject::connect(&timer, &QTimer::timeout, [&] {
				if (!reached.load()) { return; }
				auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
				if (!dialog) { return; }
				auto* progress = dialog->findChild<QProgressBar*>(); auto* details = dialog->findChild<QPlainTextEdit*>();
				if (!progress || !details || !details->toPlainText().contains("Retaining package source protections")) { return; }
				checked = true; timer.stop();
				auto* summary = dialog->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
				ok &= expect(summary && summary->text().contains("Records checked") && summary->wordWrap(), "protection record counts remain visible with an unknown total");
				ok &= expect(progress->format().contains("Records checked") && !progress->format().contains("MiB")
					&& progress->accessibleDescription() == progress->format(), "source protection progress exposes record units and accessible text");
				if (scale == 200) { dialog->setLayoutDirection(Qt::RightToLeft); }
				if (dialog->layout()) { dialog->layout()->activate(); }
				ok &= expect(summary && summary->height() >= summary->heightForWidth(summary->width()), "the settled layout has room for both status and protection record count");
				if (!captures.isEmpty()) {
					QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-source-protections-%1.png").arg(scale))), "save source protection progress evidence");
				}
				ok &= expect(!dialog->close(), "source protection preparation accepts cancellation while its worker is gated");
				{ std::lock_guard lock(mutex); release = true; } gate.notify_all();
			});
			timer.start(5);
			const auto path = root.filePath(QStringLiteral("protected-cancel-%1.vibepackage").arg(scale));
			const auto result = runPackageDraftSaveDialog(nullptr, plan, path, false, protectionControl);
			ok &= expect(checked && result.cancelled && !result.ready() && !QFileInfo::exists(path),
				"cancelling protection preparation keeps the modal loop responsive and creates no draft directory");
		}
		PackageStagingModel largePlan;
		ok &= expect(largePlan.createEmpty(PackageArchiveFormat::Pk3, {}, &error)
			&& largePlan.addBytes(QByteArray(1048576, 'a'), QStringLiteral("large.bin"), &error), "prepare a save with multiple input chunks");
		releaseLoad = false;
		std::atomic_bool reachedChunk {false};
		std::atomic_bool workerProgress {false};
		PackageWriteRequest largeSave;
		largeSave.destinationPath = root.filePath(QStringLiteral("cancel-stream-%1.pk3").arg(scale));
		largeSave.byteProgress = [&](PackageWritePhase phase, const QString&, quint64 done, quint64 total) {
			if (phase != PackageWritePhase::Measure || done != 65536 || total <= done) return;
			workerProgress = QThread::currentThread() != application.thread();
			reachedChunk = true;
			std::unique_lock lock(loadMutex);
			loadGate.wait_for(lock, std::chrono::seconds(5), [&]() { return releaseLoad; });
		};
		PackageSaveResult cancelledStream;
		auto* saving = showPackageSaveDialog(nullptr, largePlan, largeSave, [&](const auto& result) { cancelledStream = result; });
		if (scale == 200) saving->setLayoutDirection(Qt::RightToLeft);
		QTimer savePoll;
		QObject::connect(&savePoll, &QTimer::timeout, saving, [&]() {
			if (!reachedChunk.load()) return;
			savePoll.stop();
			// Allow the dialog's regular progress refresh to display this chunk.
			QTimer::singleShot(150, saving, [&]() {
				auto* progress = saving->findChild<QProgressBar*>();
				auto* summary = saving->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
				auto* cancelButton = saving->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(progress && progress->maximum() == 1000 && progress->value() > 0 && progress->value() < 1000
					&& progress->height() >= progress->fontMetrics().height() + 4
					&& !progress->accessibleName().isEmpty() && summary && summary->wordWrap()
					&& summary->text().contains(QStringLiteral("Measuring compression")), "save must show phase and partial-file progress at both scales");
				ok &= expect(exactByteProgress(progress, 65536, 1048576)
					&& progress->format().contains(progress->locale().toString(0.06, 'f', 2) + " / " + progress->locale().toString(1) + " MiB"),
					"Save progress uses compact binary quantities and exact localized bytes.");
				ok &= expect(cancelButton && cancelButton->isEnabled() && cancelButton->focusPolicy() != Qt::NoFocus
					&& !cancelButton->accessibleName().isEmpty(), "streamed save cancellation must be accessible");
				if (!captures.isEmpty()) {
					QImage image(saving->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); saving->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-stream-save-%1.png").arg(scale))), "save streaming progress widget evidence");
				}
				if (cancelButton) cancelButton->click();
				{ std::lock_guard lock(loadMutex); releaseLoad = true; }
				loadGate.notify_all();
			});
		});
		savePoll.start(10);
		ok &= expect(waitForOperation(saving) && workerProgress && cancelledStream.report.cancelled
			&& !cancelledStream.report.outputCommitted && !QFile::exists(largeSave.destinationPath), "GUI cancellation must interrupt compression measurement within one file while the event loop runs");
		delete saving;
		PackageStagingModel boundedPlan; boundedPlan.createEmpty(PackageArchiveFormat::Zip);
		ok &= expect(boundedPlan.addBytes("payload", "a/b", &error), "prepare output index refusal");
		PackageWriteRequest boundedSave; boundedSave.destinationPath = root.filePath(QStringLiteral("index-refused-%1.zip").arg(scale));
		boundedSave.indexLimits.maximumPathDepth = 1;
		releaseLoad = false; reachedChunk = false; workerProgress = false;
		boundedSave.byteProgress = [&](PackageWritePhase phase, const QString&, quint64 done, quint64) {
			if (phase != PackageWritePhase::CheckIndex || done != 0) { return; }
			workerProgress = QThread::currentThread() != application.thread(); reachedChunk = true;
			std::unique_lock lock(loadMutex);
			loadGate.wait_for(lock, std::chrono::seconds(5), [&]() { return releaseLoad; });
		};
		PackageSaveResult indexRefusal;
		auto* indexDialog = showPackageSaveDialog(nullptr, boundedPlan, boundedSave, [&](const auto& result) { indexRefusal = result; });
		if (scale == 200) { indexDialog->setLayoutDirection(Qt::RightToLeft); }
		QTimer indexPoll;
		QObject::connect(&indexPoll, &QTimer::timeout, indexDialog, [&]() {
			if (!reachedChunk.load()) { return; }
			indexPoll.stop();
			QTimer::singleShot(150, indexDialog, [&]() {
				auto* progress = indexDialog->findChild<QProgressBar*>();
				auto* summary = indexDialog->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
				auto* cancel = indexDialog->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(summary && summary->wordWrap() && summary->text().contains("Checking package output limits")
					&& summary->text().contains("Records checked:") && summary->height() >= 2 * summary->fontMetrics().height()
					&& progress && progress->format().contains("Records checked:") && !progress->format().contains("MiB")
					&& cancel && cancel->isEnabled() && !cancel->accessibleName().isEmpty(), "Index admission has visible progress, correct units and accessible cancellation.");
				if (!captures.isEmpty()) {
					QImage image(indexDialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); indexDialog->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-index-admission-%1.png").arg(scale))), "save output index progress evidence");
				}
				{ std::lock_guard lock(loadMutex); releaseLoad = true; } loadGate.notify_all();
			});
		});
		indexPoll.start(10);
		ok &= expect(waitForOperation(indexDialog) && workerProgress && !indexRefusal.report.succeeded()
			&& !indexRefusal.report.outputCommitted && !indexRefusal.archive.isOpen() && !QFileInfo::exists(boundedSave.destinationPath)
			&& indexRefusal.report.blockedMessages.join(' ').contains("depth") && boundedPlan.canUndo(), "GUI index refusal publishes no output and leaves the document recoverable.");
		auto* refusalDetails = indexDialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"));
		if (refusalDetails && !indexRefusal.report.blockedMessages.isEmpty()) {
			const auto reason = indexRefusal.report.blockedMessages.first();
			const auto position = refusalDetails->toPlainText().indexOf(reason);
			QTextCursor cursor(refusalDetails->document()); cursor.setPosition(position < 0 ? 0 : position + reason.size());
			const auto rect = refusalDetails->cursorRect(cursor);
			ok &= expect(position >= 0 && rect.top() >= 0 && rect.bottom() <= refusalDetails->viewport()->height(),
				"The complete blocking reason is visible before technical output details at both scales.");
		} else { ok &= expect(false, "The failed save has a visible diagnostic report."); }
		if (!captures.isEmpty()) {
			QImage image(indexDialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); indexDialog->render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-index-refused-%1.png").arg(scale))), "save output index refusal evidence");
		}
		delete indexDialog;
		PackageStagingPlanLimits replayLimits; replayLimits.maximumRecords = 1;
		PackageStagingModel legacyPlan; legacyPlan.createEmpty(PackageArchiveFormat::Zip);
		PackageStagingModel refusedPlan({}, {}, replayLimits);
		const auto legacyDraft = root.filePath(QStringLiteral("plan-legacy-%1.vibepackage").arg(scale));
		ok &= expect(legacyPlan.addBytes({}, "a") && legacyPlan.addBytes({}, "b")
			&& PackageDraft::save(legacyDraft, &legacyPlan, false, &error) && PackageDraft::load(legacyDraft, &refusedPlan, &error),
			"prepare worker plan-limit refusal from legacy history");
		const auto refusedRevision = refusedPlan.revision();
		PackageWriteRequest refusedSave; refusedSave.destinationPath = root.filePath(QStringLiteral("plan-refused-%1.zip").arg(scale));
		PackageSaveResult planRefusal;
		auto* refusalDialog = showPackageSaveDialog(nullptr, refusedPlan, refusedSave, [&](const auto& result) { planRefusal = result; });
		if (scale == 200) { refusalDialog->setLayoutDirection(Qt::RightToLeft); }
		ok &= expect(waitForOperation(refusalDialog) && !planRefusal.report.succeeded() && !planRefusal.report.cancelled
			&& !planRefusal.report.outputCommitted && !QFileInfo::exists(refusedSave.destinationPath)
			&& planRefusal.report.blockedMessages.join(' ').contains("1-record")
			&& refusedPlan.revision() == refusedRevision && refusedPlan.canUndo(), "Worker plan refusal preserves the document and creates no output.");
		auto* planDetails = refusalDialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"));
		if (planDetails && !planRefusal.report.blockedMessages.isEmpty()) {
			const auto reason = planRefusal.report.blockedMessages.first();
			const auto position = planDetails->toPlainText().indexOf(reason);
			QTextCursor cursor(planDetails->document()); cursor.setPosition(position < 0 ? 0 : position + reason.size());
			const auto rect = planDetails->cursorRect(cursor);
			ok &= expect(position >= 0 && rect.top() >= 0 && rect.bottom() <= planDetails->viewport()->height()
				&& !planDetails->accessibleName().isEmpty() && planDetails->focusPolicy() != Qt::NoFocus,
				"Complete plan-limit reason is visible, accessible and focusable at both scales.");
		} else { ok &= expect(false, "Plan refusal has an accessible diagnostic."); }
		if (!captures.isEmpty()) {
			QImage image(refusalDialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); refusalDialog->render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-plan-refused-%1.png").arg(scale))), "save plan-limit refusal evidence");
		}
		delete refusalDialog;
		PackageStagingModel preparingPlan; preparingPlan.createEmpty(PackageArchiveFormat::Zip);
		preparingPlan.beginOperationGroup("Plan preparation fixture");
		for (int at = 0; at < 600; ++at) { preparingPlan.addBytes({}, QStringLiteral("folder/%1/child.txt").arg(at)); }
		preparingPlan.endOperationGroup();
		preparingPlan.undo(); preparingPlan.redo(); // Exercise uncached worker replay.
		const auto preparedRevision = preparingPlan.revision();
		PackageWriteRequest prepareSave; prepareSave.destinationPath = root.filePath(QStringLiteral("plan-cancelled-%1.zip").arg(scale));
		releaseLoad = false; reachedChunk = false; workerProgress = false;
		prepareSave.byteProgress = [&](PackageWritePhase phase, const QString& text, quint64 done, quint64) {
			if (phase != PackageWritePhase::CheckIndex || text != "Preparing package plan" || done == 0) { return; }
			workerProgress = QThread::currentThread() != application.thread(); reachedChunk = true;
			std::unique_lock lock(loadMutex);
			loadGate.wait_for(lock, std::chrono::seconds(5), [&]() { return releaseLoad; });
		};
		PackageSaveResult planCancellation;
		auto* prepareDialog = showPackageSaveDialog(nullptr, preparingPlan, prepareSave, [&](const auto& result) { planCancellation = result; });
		if (scale == 200) { prepareDialog->setLayoutDirection(Qt::RightToLeft); }
		QTimer preparePoll;
		QObject::connect(&preparePoll, &QTimer::timeout, prepareDialog, [&]() {
			if (!reachedChunk.load()) { return; }
			preparePoll.stop();
			QTimer::singleShot(150, prepareDialog, [&]() {
				auto* progress = prepareDialog->findChild<QProgressBar*>();
				auto* details = prepareDialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"));
				auto* summary = prepareDialog->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
				auto* cancel = prepareDialog->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(details && details->toPlainText().contains("Preparing package plan") && progress
					&& progress->format().contains("Records checked:") && progress->accessibleDescription().contains("Records checked:")
					&& summary && summary->text().contains("Records checked:") && summary->text().contains("256")
					&& summary->height() >= 2 * summary->fontMetrics().height() && cancel && cancel->isEnabled()
					&& cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty(),
					"Plan replay has visible metadata progress and accessible cancellation while the UI dispatches events.");
				if (!captures.isEmpty()) {
					QImage image(prepareDialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); prepareDialog->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-plan-preparation-%1.png").arg(scale))), "save plan preparation progress evidence");
				}
				if (cancel) { cancel->click(); }
				{ std::lock_guard lock(loadMutex); releaseLoad = true; } loadGate.notify_all();
			});
		});
		preparePoll.start(10);
		ok &= expect(waitForOperation(prepareDialog) && reachedChunk && workerProgress && planCancellation.report.cancelled
			&& !planCancellation.report.outputCommitted && !QFileInfo::exists(prepareSave.destinationPath)
			&& preparingPlan.revision() == preparedRevision && preparingPlan.canUndo(),
			"Cancel reaches internal replay on the worker without committing output or changing document history.");
		delete prepareDialog;
		PackageWriteRequest comparisonSource;
		comparisonSource.destinationPath = root.filePath(QStringLiteral("compare-stream-%1.pk3").arg(scale));
		PackageArchive largeSource;
		ok &= expect(largePlan.writeArchive(comparisonSource).succeeded() && largeSource.load(comparisonSource.destinationPath, &error), "prepare compressed comparison source");
		releaseLoad = false; reachedChunk = false; workerProgress = false;
		PackageCompareRequest compareRequest;
		compareRequest.byteProgress = [&](PackageCompareSource side, const QString&, quint64 done, quint64 total) {
			if (side != PackageCompareSource::Right || done != 65536 || total <= done) return;
			workerProgress = QThread::currentThread() != application.thread();
			reachedChunk = true;
			std::unique_lock lock(loadMutex);
			loadGate.wait_for(lock, std::chrono::seconds(5), [&]() { return releaseLoad; });
		};
		PackageCompareResult cancelledComparison;
		auto* comparing = showPackageComparisonDialog(nullptr, largeSource, {}, &largePlan,
			[&](const auto& result) { cancelledComparison = result; }, compareRequest);
		if (scale == 200) comparing->setLayoutDirection(Qt::RightToLeft);
		QTimer comparePoll;
		QObject::connect(&comparePoll, &QTimer::timeout, comparing, [&]() {
			if (!reachedChunk.load()) return;
			comparePoll.stop();
			QTimer::singleShot(150, comparing, [&]() {
				auto* progress = comparing->findChild<QProgressBar*>();
				auto* details = comparing->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"));
				auto* status = comparing->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
				auto* cancelButton = comparing->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(progress && progress->maximum() == 1000 && progress->value() > 0 && progress->value() < 1000
					&& progress->height() >= progress->fontMetrics().height() + 4
					&& details && details->toPlainText().contains(QStringLiteral("Staged result"))
					&& details->toPlainText().contains(QStringLiteral("large.bin")), "comparison must show which side/file is streaming and readable partial-byte progress");
				ok &= expect(exactByteProgress(progress, 65536, 1048576), "Comparison keeps exact byte progress accessible for its active side.");
				ok &= expect(status && status->isVisible() && status->text().contains(QStringLiteral("Staged result"))
					&& status->text().contains(QStringLiteral("large.bin")) && status->height() >= status->heightForWidth(status->width()),
					"the current comparison side and file must stay visible above scrollable context at enlarged text scales");
				ok &= expect(cancelButton && cancelButton->isEnabled() && cancelButton->focusPolicy() != Qt::NoFocus && !cancelButton->accessibleName().isEmpty(),
					"streamed comparison cancellation must be accessible");
				if (!captures.isEmpty()) {
					QImage image(comparing->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); comparing->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-stream-compare-%1.png").arg(scale))), "save comparison progress widget evidence");
				}
				if (cancelButton) cancelButton->click();
				{ std::lock_guard lock(loadMutex); releaseLoad = true; }
				loadGate.notify_all();
			});
		});
		comparePoll.start(10);
		ok &= expect(waitForOperation(comparing) && workerProgress && cancelledComparison.cancelled && !cancelledComparison.completed
			&& cancelledComparison.entries.isEmpty() && !cancelledComparison.identical(), "staged comparison must remain responsive and cancel while hashing one generated entry");
		delete comparing;

		// Validation reports cumulative bytes without a byte total. Gate one
		// payload chunk to check the unknown-total text before cancellation.
		releaseLoad = false; reachedChunk = false; workerProgress = false;
		PackageValidationRequest validationRequest;
		validationRequest.progress = [&](int, int, quint64 bytes, const QString&) {
			if (bytes != 65536 || reachedChunk.exchange(true)) { return; }
			workerProgress = QThread::currentThread() != application.thread();
			std::unique_lock lock(loadMutex);
			loadGate.wait_for(lock, std::chrono::seconds(5), [&]() { return releaseLoad; });
		};
		PackageValidationReport cancelledValidation;
		auto* validating = showPackageValidationDialog(nullptr, largeSource,
			[&](const auto& result) { cancelledValidation = result; }, validationRequest);
		QTimer validationPoll;
		QObject::connect(&validationPoll, &QTimer::timeout, validating, [&]() {
			if (!reachedChunk.load()) { return; }
			validationPoll.stop();
			QTimer::singleShot(150, validating, [&]() {
				auto* progress = validating->findChild<QProgressBar*>();
				auto* cancelButton = validating->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
				ok &= expect(exactByteProgress(progress, 65536, 0) && progress->format().contains(progress->locale().toString(64) + " KiB")
					&& !progress->format().contains("%v") && !progress->format().contains("%m")
					&& progress->toolTip().contains("total unknown"), "Validation exposes actual file counts and exact bytes without a false byte total.");
				if (!captures.isEmpty()) {
					QImage image(validating->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); validating->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-stream-validation-%1.png").arg(scale))), "Save validation byte-progress evidence.");
				}
				if (cancelButton) { cancelButton->click(); }
				{ std::lock_guard lock(loadMutex); releaseLoad = true; }
				loadGate.notify_all();
			});
		});
		validationPoll.start(10);
		ok &= expect(waitForOperation(validating) && workerProgress && cancelledValidation.cancelled,
			"Validation remains responsive and cancels during the gated payload chunk.");
		auto* finishedProgress = validating->findChild<QProgressBar*>();
		ok &= expect(finishedProgress && finishedProgress->toolTip().isEmpty() && finishedProgress->accessibleDescription().isEmpty(),
			"Completion clears byte progress details from the preceding phase.");
		delete validating;
		application.removeTranslator(&translator);
	}

	PackageWriteRequest save;
	save.destinationPath = source.sourcePath();
	save.allowInPlaceOverwrite = true;
	PackageSaveResult saved;
	const QByteArray original = readFile(source.sourcePath());
	auto* saveDialog = showPackageSaveDialog(nullptr, plan, save, [&](const auto& result) { saved = result; });
	ok &= expect(saveDialog->windowModality() == Qt::WindowModal, "saving must keep package context stable");
	ok &= expect(waitForOperation(saveDialog) && saved.ready(), "save must reopen and rebase the committed archive");
	ok &= expect(saved.staging.operations().isEmpty() && saved.staging.sourcePath() == saved.report.outputPath, "rebased plan must be clean and point at the saved package");
	ok &= expect(readFile(saved.report.backupPath) == original, "in-place save must keep original bytes as backup");
	QByteArray bytes;
	ok &= expect(saved.archive.readEntryBytes(QStringLiteral("b.txt"), &bytes, &error) && bytes == "unchanged payload", "reloaded archive must use updated offsets");
	delete saveDialog;

	// The next edit and save read the freshly adopted archive, not stale offsets.
	ok &= expect(saved.staging.renameEntry(QStringLiteral("new.cfg"), QStringLiteral("renamed.cfg"), &error), "edit the reloaded archive");
	PackageWriteRequest second;
	second.destinationPath = root.filePath(QStringLiteral("second.pk3"));
	PackageSaveResult savedAgain;
	saveDialog = showPackageSaveDialog(nullptr, saved.staging, second, [&](const auto& result) { savedAgain = result; });
	ok &= expect(waitForOperation(saveDialog) && savedAgain.ready(), "second save must complete from the rebased plan");
	ok &= expect(savedAgain.archive.readEntryBytes(QStringLiteral("b.txt"), &bytes, &error) && bytes == "unchanged payload", "second save must preserve untouched bytes");
	ok &= expect(savedAgain.archive.readEntryBytes(QStringLiteral("renamed.cfg"), &bytes, &error) && bytes == "new file", "second save must preserve renamed content");
	delete saveDialog;

	// Cancellation after disk indexing must also stop the saved document's base
	// preparation, while preserving the truthful committed-output report.
	{
		bool cancelled = false, reached = false;
		PackageReadControl control; control.isCancelled = [&] { return cancelled; };
		control.progress = [&](const QString& phase, qint64, qint64) {
			if (phase == "Reading package base metadata") { reached = cancelled = true; }
		};
		const auto beforeReload = readFile(second.destinationPath);
		const auto reopened = reopenSavedPackage(savedAgain.report, control);
		ok &= expect(reached && reopened.report.outputCommitted && reopened.report.succeeded() && !reopened.ready()
			&& !reopened.staging.isLoaded() && reopened.reloadError.contains("cancelled") && readFile(second.destinationPath) == beforeReload,
			"post-save base cancellation leaves the committed output intact and publishes no editable replacement");
	}

	// An external replacement cannot become the shell's just-saved document.
	{
		PackageWriteRequest swapped;
		swapped.destinationPath = root.filePath(QStringLiteral("changed-after-save.pak"));
		const auto written = saved.staging.writeArchive(swapped);
		ok &= expect(written.succeeded(), "commit output before simulated external replacement");
		QFile replacement(swapped.destinationPath);
		ok &= expect(replacement.open(QIODevice::WriteOnly) && replacement.write(original) == original.size(), "replace committed output with another valid archive");
		replacement.close();
		const auto reopened = reopenSavedPackage(written);
		ok &= expect(reopened.report.outputCommitted && reopened.report.succeeded() && !reopened.ready()
			&& !reopened.archive.isOpen() && !reopened.reloadError.isEmpty(),
			"post-save adoption must reject a different valid archive while reporting that the write committed");
	}

	// A failed write leaves the caller's operations available for retry.
	PackageSaveResult failed;
	saveDialog = showPackageSaveDialog(nullptr, saved.staging, second, [&](const auto& result) { failed = result; });
	ok &= expect(waitForOperation(saveDialog) && !failed.report.succeeded() && !saved.staging.operations().isEmpty(), "failure must preserve the live plan");
	delete saveDialog;

	std::atomic_bool cancel {false};
	PackageWriteRequest cancelled = save;
	cancelled.isCancelled = [&]() { return cancel.load(); };
	cancelled.progress = [&](int, int, const QString&) { cancel = true; };
	const QByteArray beforeCancel = readFile(save.destinationPath);
	PackageSaveResult cancelledResult;
	saveDialog = showPackageSaveDialog(nullptr, saved.staging, cancelled, [&](const auto& result) { cancelledResult = result; });
	ok &= expect(waitForOperation(saveDialog) && cancelledResult.report.cancelled, "cancel between entries must stop the worker");
	ok &= expect(readFile(save.destinationPath) == beforeCancel && !cancelledResult.ready(), "cancellation must leave the destination untouched");
	delete saveDialog;

	// Closing a busy writer is a cancellation request, not permission to
	// destroy its owner or let it commit after the dialog disappears.
	std::mutex gateMutex;
	std::condition_variable gate;
	bool released = false;
	PackageWriteRequest closeRequest;
	closeRequest.destinationPath = root.filePath(QStringLiteral("cancel-on-close.pk3"));
	closeRequest.isCancelled = [&]() {
		std::unique_lock lock(gateMutex);
		gate.wait(lock, [&]() { return released; });
		return false;
	};
	PackageSaveResult closed;
	saveDialog = showPackageSaveDialog(nullptr, saved.staging, closeRequest, [&](const auto& result) { closed = result; });
	ok &= expect(!saveDialog->close(), "closing a running save must wait for its worker");
	{ std::lock_guard lock(gateMutex); released = true; }
	gate.notify_all();
	ok &= expect(waitForOperation(saveDialog) && closed.report.cancelled && !QFile::exists(closeRequest.destinationPath), "closing must cancel before commit without creating output");
	delete saveDialog;

	// Exercise the public CLI against the same real fixture, without a GUI.
	if (argc > 1) {
		const QString draftPath = root.filePath(QStringLiteral("cli.vibepackage"));
		const auto draft = [&](const QStringList& args, int expectedExit) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("draft-settings.ini")), QStringLiteral("package")} + args + QStringList{QStringLiteral("--json")});
			const bool ended = process.waitForFinished(15000);
			const QByteArray output = process.readAllStandardOutput();
			if (!ended || process.exitCode() != expectedExit) { std::cerr << args.join(' ').toStdString() << '\n' << output.toStdString() << process.readAllStandardError().toStdString(); }
			ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedExit, "draft CLI reports writes and usage errors accurately");
			return QJsonDocument::fromJson(output).object();
		};
		const auto createdDraft = draft({QStringLiteral("draft-save"), second.destinationPath, draftPath, QStringLiteral("--delete"), QStringLiteral("b.txt"), QStringLiteral("--rename"), QStringLiteral("renamed.cfg"), QStringLiteral("--to"), QStringLiteral("draft.cfg")}, 0);
		ok &= expect(createdDraft.value(QStringLiteral("saved")).toBool() && createdDraft.value(QStringLiteral("operationCount")).toInt() == 2 && createdDraft.value(QStringLiteral("canUndo")).toBool(), "draft CLI persists one edit group");
		const QByteArray draftManifest = readFile(QDir(draftPath).filePath(QStringLiteral("document.json")));
		draft({QStringLiteral("draft-info"), draftPath}, 0);
		ok &= expect(readFile(QDir(draftPath).filePath(QStringLiteral("document.json"))) == draftManifest, "draft inspection is read-only");
		draft({QStringLiteral("draft-info"), draftPath, QStringLiteral("--overwrite")}, 2);
		draft({QStringLiteral("draft-undo"), draftPath, QStringLiteral("extra")}, 2);
		draft({QStringLiteral("draft-save"), second.destinationPath, root.filePath(QStringLiteral("bad.vibepackage")), QStringLiteral("--delete=")}, 2);
		draft({QStringLiteral("draft-save"), second.destinationPath, root.filePath(QStringLiteral("bad.vibepackage")), QStringLiteral("--mystery")}, 2);
		draft({QStringLiteral("draft-save"), second.destinationPath, root.filePath(QStringLiteral("bad.vibepackage")), QStringLiteral("--as"), QStringLiteral("orphan")}, 2);
		ok &= expect(!QFileInfo::exists(root.filePath(QStringLiteral("bad.vibepackage"))), "invalid draft CLI options create no output");
		const auto undoneDraft = draft({QStringLiteral("draft-undo"), draftPath}, 0);
		ok &= expect(undoneDraft.value(QStringLiteral("operationCount")).toInt(-1) == 0 && undoneDraft.value(QStringLiteral("canRedo")).toBool(), "one CLI undo reverses the full staged command");
		const auto redoneDraft = draft({QStringLiteral("draft-redo"), draftPath}, 0);
		ok &= expect(redoneDraft.value(QStringLiteral("operationCount")).toInt() == 2, "CLI redo restores persisted edits");
		draft({QStringLiteral("draft-redo"), draftPath}, 4);
		const QString draftOutput = root.filePath(QStringLiteral("draft-export.pk3"));
		draft({QStringLiteral("save-as"), draftPath, draftOutput}, 0);
		PackageArchive draftArchive;
		ok &= expect(draftArchive.load(draftOutput, &error) && draftArchive.readEntryBytes(QStringLiteral("draft.cfg"), &bytes, &error) && bytes == "new file", "normal package writer consumes a persistent draft");
		const auto draftList = draft({QStringLiteral("list"), draftPath}, 0).value(QStringLiteral("package")).toObject();
		QStringList draftPaths;
		for (const auto& value : draftList.value(QStringLiteral("entries")).toArray()) { draftPaths << value.toObject().value(QStringLiteral("virtualPath")).toString(); }
		ok &= expect(draftPaths.contains(QStringLiteral("draft.cfg")) && !draftPaths.contains(QStringLiteral("renamed.cfg"))
			&& !draftPaths.contains(QStringLiteral("b.txt")), "CLI listing shows the draft's effective paths rather than its object store or original archive");
		const auto draftPreview = draft({QStringLiteral("preview"), draftPath, QStringLiteral("draft.cfg")}, 0).value(QStringLiteral("preview")).toObject();
		ok &= expect(draftPreview.value(QStringLiteral("body")).toString() == QStringLiteral("new file"), "CLI draft preview reads staged bytes");
		const QString draftExtract = root.filePath(QStringLiteral("draft-extracted"));
		draft({QStringLiteral("extract"), draftPath, draftExtract, QStringLiteral("--entry"), QStringLiteral("draft.cfg")}, 0);
		ok &= expect(readFile(QDir(draftExtract).filePath(QStringLiteral("draft.cfg"))) == "new file", "CLI extraction consumes the same planned draft view");
		const auto cli = [&](const QStringList& args, int expectedExit) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")), QStringLiteral("package"), QStringLiteral("compare"), source.sourcePath()} + args + QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(15000);
			ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedExit, "CLI comparison exit status must reflect differences, limits, and usage errors");
			return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		};
		const auto diff = cli({QStringLiteral("--staged"), QStringLiteral("--delete"), QStringLiteral("b.txt")}, 4);
		ok &= expect(diff.value(QStringLiteral("comparison")).toObject().value(QStringLiteral("summary")).toObject().value(QStringLiteral("removedCount")).toInt() == 1, "CLI staged review must expose removals");
		cli({QStringLiteral("--staged")}, 0);
		cli({QStringLiteral("--staged"), QStringLiteral("--max-entry-bytes"), QStringLiteral("1")}, 4);
		cli({QStringLiteral("--staged"), QStringLiteral("--max-entry-bytes"), QStringLiteral("bad")}, 2);
		cli({source.sourcePath(), QStringLiteral("--delete"), QStringLiteral("b.txt")}, 2);
		cli({source.sourcePath(), QStringLiteral("--as"), QStringLiteral("ignored.txt")}, 2);
		cli({source.sourcePath(), QStringLiteral("--staged")}, 2);
		ok &= expect(readFile(save.destinationPath) == beforeCancel, "CLI review must not modify its source");
		const auto validate = [&](const QString& path, const QStringList& args, int expectedExit) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")), QStringLiteral("package"), QStringLiteral("validate"), path} + args + QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(15000);
			ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedExit, "CLI validation must distinguish success, corruption, limits and invalid arguments");
			return QJsonDocument::fromJson(process.readAllStandardOutput()).object().value(QStringLiteral("validation")).toObject();
		};
		const auto valid = validate(second.destinationPath, {}, 0);
		ok &= expect(valid.value(QStringLiteral("valid")).toBool() && valid.value(QStringLiteral("verifiedCount")).toInt() == 3, "CLI validation must include verified payload counts");
		validate(second.destinationPath, {QStringLiteral("--max-entry-bytes"), QStringLiteral("1")}, 4);
		validate(second.destinationPath, {QStringLiteral("--max-entry-bytes"), QStringLiteral("bad")}, 2);
		QByteArray damaged = readFile(second.destinationPath);
		const auto members = savedAgain.archive.entries();
		for (const auto& entry : members) {
			if (entry.kind == PackageEntryKind::File && entry.sizeBytes > 0) { damaged[entry.dataOffset] ^= 1; break; }
		}
		const QString damagedPath = root.filePath(QStringLiteral("damaged.pk3"));
		QFile output(damagedPath);
		ok &= expect(output.open(QIODevice::WriteOnly) && output.write(damaged) == damaged.size(), "create corrupt CLI fixture"); output.close();
		const auto invalid = validate(damagedPath, {}, 4);
		ok &= expect(!invalid.value(QStringLiteral("valid")).toBool() && invalid.value(QStringLiteral("failedCount")).toInt() > 0, "CLI validation must reject corruption hidden by matching directory metadata");

		const QString extractRoot = root.filePath(QStringLiteral("cli-extracted"));
		const auto extract = [&](const QStringList& options, int expectedExit, bool legacy = false) {
			QProcess process;
			const QStringList command = legacy ? QStringList {QStringLiteral("--extract"), second.destinationPath}
				: QStringList {QStringLiteral("package"), QStringLiteral("extract"), second.destinationPath};
			process.start(QString::fromLocal8Bit(argv[1]), QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini"))}
				+ command + QStringList {QStringLiteral("--output"), extractRoot} + options + QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(15000);
			const QByteArray response = process.readAllStandardOutput();
			const bool expected = ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedExit;
			ok &= expect(expected, "CLI extraction must reject invalid selection/options before writing");
			if (!expected) {
				std::cerr << options.join(QLatin1Char(' ')).toStdString() << ": expected " << expectedExit << ", got "
					<< process.exitCode() << '\n' << response.toStdString() << process.readAllStandardError().toStdString();
			}
			return QJsonDocument::fromJson(response).object().value(QStringLiteral("extraction")).toObject();
		};
		extract({QStringLiteral("--entry=")}, 2);
		extract({QStringLiteral("--entry"), QStringLiteral("--overwrite")}, 2);
		extract({QStringLiteral("--entries"), QStringLiteral(";;")}, 2);
		extract({QStringLiteral("--overwrite=no")}, 2);
		extract({QStringLiteral("--unknown")}, 2);
		extract({QStringLiteral("--extract-all"), QStringLiteral("--entry"), QStringLiteral("b.txt")}, 2);
		extract({QStringLiteral("unexpected-position")}, 2);
		extract({QStringLiteral("--entry=-missing.txt")}, 1);
		extract({QStringLiteral("--entry"), QStringLiteral("b.txt"), QStringLiteral("--dry-run")}, 0, true);
		ok &= expect(!QFileInfo::exists(extractRoot), "rejected and dry-run extraction commands cannot create the output root");
		const auto extraction = extract({QStringLiteral("--entry"), QStringLiteral("b.txt")}, 0);
		ok &= expect(extraction.value(QStringLiteral("writtenCount")).toInt() == 1 && extraction.value(QStringLiteral("bytesRead")).toInt() == 17
			&& readFile(QDir(extractRoot).filePath(QStringLiteral("b.txt"))) == "unchanged payload", "CLI extraction returns streamed bytes and the selected content");

		// Leave a committed save unfinished, then inspect/recover through the CLI.
		const QString recoveryOutput = root.filePath(QStringLiteral("recovery.pak"));
		QFile recoveryFile(recoveryOutput);
		ok &= expect(recoveryFile.open(QIODevice::WriteOnly) && recoveryFile.write("old!") == 4, "create CLI recovery original");
		recoveryFile.close();
		QString recoveryJournal;
		{
			PackagePublicationOptions options; options.destinationPath = recoveryOutput; options.allowOverwrite = true;
			PackagePublication publication(options, [](auto step, QString* error) {
				if (step == PackagePublicationStep::OutputCommitted) { *error = QStringLiteral("injected interruption"); return false; }
				return true;
			});
			ok &= expect(publication.begin(&error) && publication.device()->write("new!") == 4, "prepare CLI recovery fixture");
			const auto published = publication.commit(4, QString::fromLatin1(QCryptographicHash::hash("new!", QCryptographicHash::Sha256).toHex()));
			ok &= expect(published.committed && !published.recoveryPaths.isEmpty(), "retain committed recovery journal");
			for (const auto& path : published.recoveryPaths) { if (path.endsWith(QStringLiteral(".json"))) { recoveryJournal = path; } }
		}
		const auto recover = [&](const QStringList& arguments, int expectedExit) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")), QStringLiteral("package"), QStringLiteral("recover"), recoveryJournal} + arguments + QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(15000);
			ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedExit, "CLI recovery exit status must distinguish completion and refused arguments");
			return QJsonDocument::fromJson(process.readAllStandardOutput()).object().value(QStringLiteral("recovery")).toObject();
		};
		const auto inspected = recover({}, 0);
		ok &= expect(inspected.value(QStringLiteral("canFinish")).toBool() && QFile::exists(recoveryJournal), "CLI recovery inspection is read-only");
		recover({QStringLiteral("--overwrite")}, 2);
		recover({QStringLiteral("--finish=no")}, 2);
		recover({QStringLiteral("--backup"), QStringLiteral("somewhere.bak")}, 2);
		recover({QStringLiteral("--backup"), QStringLiteral("--finish")}, 2);
		recover({QStringLiteral("--finish"), QStringLiteral("--finish")}, 2);
		recover({QStringLiteral("unexpected")}, 2);
		const auto completed = recover({QStringLiteral("--finish")}, 0);
		ok &= expect(completed.value(QStringLiteral("finished")).toBool() && !QFile::exists(recoveryJournal)
			&& readFile(recoveryOutput) == "new!" && readFile(recoveryOutput + ".bak") == "old!", "CLI finish retains the new package and verified original backup");
	}
	{
		// Exercise the real shell handoff with an isolated profile. These are
		// direct widget/property calls, never injected mouse or keyboard events.
		StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("shell-settings.ini")));
		applyStudioTheme(application, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
		auto* shell = new ApplicationShell;
		shell->openPathFromCommandLine(second.destinationPath);
		auto entries = tests::PackageRows(shell->findChild<PackageEntryView*>(QStringLiteral("packageEntries")));
		auto stages = tests::StagingRows(shell->findChild<PackageStagingView*>(QStringLiteral("packageStagingSummary")));
		auto* remove = shell->findChild<QAction*>(QStringLiteral("package.stageDelete"));
		ok &= expect(entries && stages && remove && entries->count() >= 3, "worker opening must populate the real package shell");
		if (entries && stages && remove && entries->count() >= 3) {
			entries->setCurrentRow(0);
			const int originalCount = entries->count();
			const QString firstPath = entries->item(0)->data(Qt::UserRole).toString();
			remove->trigger();
			ok &= expect(entries->count() == originalCount - 1 && entries->item(0)->data(Qt::UserRole).toString() != firstPath,
				"staged deletion immediately removes the row from the planned browser");
			const auto operationIds = [&]() {
				QStringList ids;
				for (int index = 0; index < stages->count(); ++index) {
					const QString id = stages->item(index)->data(Qt::UserRole + 5).toString();
					if (!id.isEmpty()) { ids << id; }
				}
				return ids;
			};
			const QStringList stagedIds = operationIds();
			ok &= expect(stagedIds.size() == 1, "prepare an unsaved shell operation");
			auto* undo = shell->findChild<QAction*>(QStringLiteral("package.unstageLast"));
			auto* redo = shell->findChild<QAction*>(QStringLiteral("package.redo"));
			ok &= expect(undo && redo && undo->isEnabled() && !redo->isEnabled(), "shell exposes current history availability");
			if (undo && redo) {
				undo->trigger(); ok &= expect(operationIds().isEmpty() && redo->isEnabled() && entries->count() == originalCount, "shell undo restores the prior plan and browser");
				redo->trigger(); ok &= expect(operationIds() == stagedIds && !redo->isEnabled() && entries->count() == originalCount - 1, "shell redo restores the exact operation and browser");
			}
			const QString brokenPath = root.filePath(QStringLiteral("invalid-open.pak"));
			QFile broken(brokenPath);
			ok &= expect(broken.open(QIODevice::WriteOnly) && broken.write("invalid") == 7, "create invalid replacement package"); broken.close();
			bool acceptedDiscard = false;
			QTimer::singleShot(0, [&]() {
				if (auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
					for (auto* button : confirmation->buttons()) {
						if (confirmation->standardButton(button) == QMessageBox::Discard) { acceptedDiscard = true; button->click(); break; }
					}
				}
			});
			shell->openPathFromCommandLine(brokenPath);
			ok &= expect(acceptedDiscard && operationIds() == stagedIds && entries->count() == originalCount - 1
				&& entries->item(0)->data(Qt::UserRole).toString() != firstPath,
				"failed replacement opening must preserve both the current browser and unsaved staged operations");
		}
		delete shell;
		application.processEvents();
		auto browserPlan = savedAgain.staging;
		browserPlan.addBytes("draft browser text", QStringLiteral("nested/deep/generated.txt"));
		const auto savedDraft = runPackageDraftSaveDialog(nullptr, browserPlan, root.filePath(QStringLiteral("shell.vibepackage")), false);
		ok &= expect(savedDraft.ready(), "prepare a persistent shell document");
		shell = new ApplicationShell;
		shell->openPathFromCommandLine(root.filePath(QStringLiteral("shell.vibepackage")));
		entries = tests::PackageRows(shell->findChild<PackageEntryView*>(QStringLiteral("packageEntries")));
		ok &= expect(entries && entries->count() >= 3, "normal shell opening recognizes a draft directory");
		auto* filter = shell->findChild<QLineEdit*>(QStringLiteral("packageFilter"));
		auto* preview = shell->findChild<QPlainTextEdit*>(QStringLiteral("packageTextPreview"));
		auto* tree = shell->findChild<PackageFolderView*>(QStringLiteral("packageTree"));
		ok &= expect(filter && preview && tree, "planned browser exposes filter, preview and folders");
		if (filter && preview && tree && entries) {
			filter->setText(QStringLiteral("generated.txt"));
			ok &= expect(entries->count() == 1 && entries->item(0)->data(Qt::UserRole).toString() == QStringLiteral("nested/deep/generated.txt")
				&& waitForPreview(preview, QStringLiteral("draft browser text")), "draft browser previews generated content without exporting it first");
			ok &= expect(tree->folderIndex(QStringLiteral("nested/deep")).isValid(), "new parent folders are present in the navigation tree");
		}
		auto* closeDraft = shell->findChild<QAction*>(QStringLiteral("package.close"));
		if (closeDraft) { closeDraft->trigger(); }
		ok &= expect(entries && entries->count() == 1 && !entries->item(0)->flags().testFlag(Qt::ItemIsSelectable), "a saved draft closes without a discard prompt");
		ok &= expect(preview && preview->toPlainText().isEmpty(), "closing the package clears the prior entry preview");
		delete shell;
		application.processEvents();
		StudioSettings::setOverrideFilePath({});
	}
	return ok ? 0 : 1;
}
