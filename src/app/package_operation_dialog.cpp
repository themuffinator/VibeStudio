#include "app/package_operation_dialog.h"
#include "app/package_progress.h"
#include "core/package_draft.h"
#include "core/package_draft_storage.h"
#include "core/package_recovery.h"
#include "core/package_import_store.h"
#include "core/studio_settings.h"
#include "core/document_watch.h"
#include "core/idtech_image.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <atomic>
#include <memory>

namespace vibestudio {
namespace {

struct WorkState {
	std::atomic_bool cancel {false};
	std::atomic_int completed {0};
	std::atomic_int total {0};
	std::atomic<quint64> bytesRead {0};
	std::atomic<quint64> sourceBytes {0};
	std::atomic_bool reopening {false};
	std::atomic_bool checkingStagedView {false};
	bool checkingSourceProtections = false; // Guarded with sourcePath by pathMutex.
	QMutex pathMutex;
	QString sourcePath;
	PackageWritePhase writePhase = PackageWritePhase::VerifySources;
	PackageCompareResult comparison;
	PackageSaveResult save;
	PackageValidationReport validation;
	PackageExtractionReport extraction;
	PackageCopyResult copies;
	PackageLoadResult load;
	PackageStageFilesResult stage;
	PackageEditResult edit;
	IdTechPaletteResolution palette;
	bool paletteCancelled = false;
};

PackageReadControl readControl(const std::shared_ptr<WorkState>& state, const PackageReadControl& previous = {})
{
	PackageReadControl control; control.importOptions = previous.importOptions; control.draftLimits = previous.draftLimits;
	control.isCancelled = [state, previous]() {
		if (state->cancel.load()) { return true; }
		if (previous.isCancelled && previous.isCancelled()) { state->cancel = true; }
		return state->cancel.load();
	};
	control.progress = [state, previous](const QString& path, qint64 completed, qint64 total) {
		{
			QMutexLocker lock(&state->pathMutex);
			state->sourcePath = path;
			state->checkingSourceProtections = path == QCoreApplication::translate("VibeStudioPackageStaging", "Retaining package source protections")
				|| path == QCoreApplication::translate("VibeStudioPackageStaging", "Checking package source protections");
			state->bytesRead = static_cast<quint64>(completed);
			state->sourceBytes = static_cast<quint64>(total);
		}
		if (previous.progress) { previous.progress(path, completed, total); }
	};
	return control;
}

class PackageOperationDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioPackageDialog)
public:
	explicit PackageOperationDialog(QWidget* parent) : QDialog(parent)
	{
		setAttribute(Qt::WA_DeleteOnClose);
		m_reducedMotion = StudioSettings().accessibilityPreferences().reducedMotion;
		resize(940, 620);
		m_layout = new QVBoxLayout(this);
		m_summary = new QLabel;
		m_summary->setObjectName(QStringLiteral("packageOperationSummary"));
		m_summary->setTextFormat(Qt::PlainText);
		m_summary->setWordWrap(true);
		m_summary->setAccessibleName(tr("Package operation status"));
		m_layout->addWidget(m_summary);
		m_progress = new QProgressBar(this);
		// Native sizing follows live text-scale changes as well as the initial font.
		m_progress->setAccessibleName(tr("Package operation progress"));
		m_progress->setRange(0, m_reducedMotion ? 1 : 0);
		m_layout->addWidget(m_progress);
		m_details = new QPlainTextEdit;
		m_details->setObjectName(QStringLiteral("packageOperationDetails"));
		m_details->setReadOnly(true);
		m_details->setAccessibleName(tr("Package operation details"));
		m_layout->addWidget(m_details, 1);
		m_buttons = new QDialogButtonBox(QDialogButtonBox::Close);
		m_buttons->button(QDialogButtonBox::Close)->setAccessibleName(tr("Close package operation"));
		m_cancel = m_buttons->addButton(tr("Cancel"), QDialogButtonBox::ActionRole);
		m_cancel->setObjectName(QStringLiteral("cancelPackageOperation"));
		m_cancel->setAccessibleName(tr("Cancel package operation"));
		m_cancel->setToolTip(tr("Stop after the current file. A save already committing may finish."));
		connect(m_cancel, &QPushButton::clicked, this, [this]() { cancel(); });
		connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
		m_layout->addWidget(m_buttons);
		m_timer = new QTimer(this);
		connect(m_timer, &QTimer::timeout, this, [this]() {
			if (m_writing && !m_state->reopening.load()) {
				QMutexLocker lock(&m_state->pathMutex);
				if (!m_state->cancel.load()) {
					switch (m_state->writePhase) {
					case PackageWritePhase::CheckIndex:
						m_summary->setText(tr("Checking package output limits…") + QLatin1Char('\n')
							+ tr("Records checked: %1").arg(locale().toString(m_state->bytesRead.load())));
						break;
					case PackageWritePhase::VerifySources: m_summary->setText(tr("Verifying package sources…")); break;
					case PackageWritePhase::Measure: m_summary->setText(tr("Measuring compression…")); break;
					case PackageWritePhase::Write: m_summary->setText(tr("Writing package…")); break;
					case PackageWritePhase::VerifyDeterminism: m_summary->setText(tr("Checking reproducible output…")); break;
					case PackageWritePhase::Manifest: m_summary->setText(tr("Preparing package manifest…")); break;
					case PackageWritePhase::Publish: m_summary->setText(tr("Verifying and publishing package…")); break;
					}
				}
				const auto done = m_state->bytesRead.load();
				const auto total = m_state->sourceBytes.load();
				setProgressRange(done, total);
				if (m_state->writePhase == PackageWritePhase::CheckIndex) {
					setProgressText(tr("Records checked: %1").arg(locale().toString(done)));
				} else {
					const auto quantity = packageByteProgressText(done, total, locale());
					setProgressText(tr("%1 files · %2")
						.arg(packageProgressPair(locale().toString(m_state->completed.load()), locale().toString(m_state->total.load())),
							quantity.compact), quantity.exact);
				}
				if (!m_state->sourcePath.isEmpty()) m_details->setPlainText(m_writeContext + QStringLiteral("\n\n") + m_state->sourcePath);
				return;
			}
			if (m_reading || m_state->reopening.load()) {
				if (m_state->reopening.load()) {
					m_summary->setText(tr("Package saved. Opening the saved package…"));
					m_cancel->setEnabled(false);
				}
				QMutexLocker lock(&m_state->pathMutex);
				const quint64 totalBytes = m_state->sourceBytes.load();
				const quint64 completedBytes = m_state->bytesRead.load();
				setProgressRange(completedBytes, totalBytes);
				if (m_state->checkingSourceProtections && !m_showingSourceProtections) {
					m_previousReadSummary = m_summary->text(); m_showingSourceProtections = true;
				} else if (!m_state->checkingSourceProtections && m_showingSourceProtections) {
					if (!m_state->cancel.load() && !m_state->reopening.load() && !m_state->checkingStagedView.load()) { m_summary->setText(m_previousReadSummary); }
					m_showingSourceProtections = false;
				}
				if (m_state->checkingStagedView.load() || m_state->checkingSourceProtections) {
					const auto records = tr("Records checked: %1").arg(locale().toString(completedBytes));
					if (!m_state->cancel.load()) {
						m_summary->setText((m_state->checkingSourceProtections ? m_previousReadSummary : tr("Checking staged package…")) + QLatin1Char('\n') + records);
					}
					setProgressText(records);
				} else {
					const auto quantity = packageByteProgressText(completedBytes, totalBytes, locale());
					if (m_comparing) {
						if (m_state->total.load() > 0 && !m_state->cancel.load() && !m_state->sourcePath.isEmpty()) {
							m_summary->setText(tr("Comparing %1").arg(m_state->sourcePath));
						}
						setProgressText(tr("%1 files · %2")
							.arg(packageProgressPair(locale().toString(m_state->completed.load()), locale().toString(m_state->total.load())),
								quantity.compact), quantity.exact);
					} else { setProgressText(tr("%1 read").arg(quantity.compact), quantity.exact); }
				}
				if (!m_state->sourcePath.isEmpty()) { m_details->setPlainText(m_comparing
					? m_compareContext + QStringLiteral("\n\n") + m_state->sourcePath : m_state->sourcePath); }
				return;
			}
			QMutexLocker lock(&m_state->pathMutex);
			const int total = m_state->total.load(), completed = m_state->completed.load();
			m_progress->setRange(0, total > 0 ? total : m_reducedMotion ? 1 : 0);
			m_progress->setValue(completed);
			if (m_validating) {
				const auto quantity = packageByteProgressText(m_state->bytesRead.load(), 0, locale());
				setProgressText(tr("%1 files · %2 read")
					.arg(packageProgressPair(locale().toString(completed), locale().toString(total)), quantity.compact), quantity.exact);
			}
		});
	}

	~PackageOperationDialog() override
	{
		m_state->cancel = true;
		// Destruction during application shutdown still joins the writer: no
		// detached thread may commit after the application has gone away.
		if (m_worker) { m_worker->wait(); }
	}

	void load(const QString& path, const PackageReadControl& control, std::function<void(const PackageLoadResult&)> finished)
	{
		m_reading = true;
		setObjectName(QStringLiteral("packageLoadDialog"));
		setWindowTitle(tr("Open Package"));
		setAccessibleName(windowTitle());
		setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Checking package sources…"));
		m_details->setPlainText(path);
		m_cancel->setToolTip(tr("Cancel while reading the current file. The open package and staged changes are kept."));
		const auto state = m_state;
		const auto workerControl = readControl(state, control);
		auto* worker = QThread::create([state, path, workerControl]() {
			if (path.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) {
				if (PackageDraft::load(path, &state->load.staging, &state->load.error, workerControl)) {
					state->load.archive = PackageDraft::baseArchive(state->load.staging, &state->load.error, workerControl);
				}
			} else if (state->load.archive.load(path, &state->load.error, workerControl)) {
				{
					QMutexLocker lock(&state->pathMutex);
					state->bytesRead = 0; state->sourceBytes = 0; state->sourcePath.clear(); state->checkingStagedView = true;
				}
				state->load.staging.loadBaseArchive(state->load.archive, &state->load.error, workerControl);
			}
			if (state->load.staging.isLoaded() && state->load.error.isEmpty()) {
				{
					QMutexLocker lock(&state->pathMutex);
					state->bytesRead = 0; state->sourceBytes = 0; state->sourcePath.clear(); state->checkingStagedView = true;
				}
				// Prepare cached status/composition before the UI adopts the document.
				// A legacy blocked plan remains open for Undo and recovery.
				state->load.staging.preparePlan(nullptr, workerControl);
			}
			state->load.cancelled = workerControl.isCancelled();
			if (state->load.cancelled || !state->load.error.isEmpty()) { state->load.archive.clear(); state->load.staging.clear(); }
		});
		connect(worker, &QThread::finished, this, [this, state, finished]() {
			if (state->cancel.load() && !state->load.cancelled) { state->load = PackageLoadResult(); state->load.cancelled = true; }
			finish(state->load.ready()); finished(state->load);
		});
		begin(worker);
	}

	void saveDraft(const PackageStagingModel& plan, const QString& path, bool overwrite, const PackageReadControl& control,
		std::function<void()> finished)
	{
		m_reading = true;
		setObjectName(QStringLiteral("packageDraftSaveDialog"));
		setWindowTitle(tr("Save Package Draft")); setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Preserving package content and edit history…"));
		m_details->setPlainText(path);
		m_cancel->setToolTip(tr("Cancel before the draft is committed. The previous draft remains available."));
		const auto state = m_state;
		auto workerControl = readControl(state, control);
		if (!workerControl.draftLimits) { workerControl.draftLimits = std::make_shared<PackageDraftSaveLimits>(StudioSettings().packageDraftSaveLimits()); }
		auto* worker = QThread::create([state, plan, path, overwrite, workerControl]() {
			state->load.staging = plan;
			const bool saved = PackageDraft::save(path, &state->load.staging, overwrite, &state->load.error, workerControl);
			if (saved) {
				state->load.archive = PackageDraft::baseArchive(state->load.staging, &state->load.error);
				state->load.staging.preparePlan();
			}
			state->load.cancelled = !saved && workerControl.isCancelled();
		});
		connect(worker, &QThread::finished, this, [this, state, finished]() { finish(state->load.ready()); finished(); });
		begin(worker);
	}

	void restoreRecovery(const QString& directory, const QString& id, const QByteArray& manifestSha256,
		const QString& destination, const PackageReadControl& control, std::function<void()> finished)
	{
		m_reading = true;
		setObjectName(QStringLiteral("packageRecoveryRestoreDialog"));
		setWindowTitle(tr("Restore Package Draft")); setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Verifying recovery content and preserving edit history…"));
		m_details->setPlainText(destination);
		m_cancel->setToolTip(tr("Cancel before the recovered draft is committed."));
		const auto state = m_state; auto workerControl = readControl(state, control);
		if (!workerControl.draftLimits) { workerControl.draftLimits = std::make_shared<PackageDraftSaveLimits>(StudioSettings().packageDraftSaveLimits()); }
		auto* worker = QThread::create([state, directory, id, manifestSha256, destination, workerControl]() {
			const bool saved = restorePackageRecovery(directory, id, manifestSha256, destination, &state->load.staging, &state->load.error, workerControl);
			if (saved) {
				state->load.archive = PackageDraft::baseArchive(state->load.staging, &state->load.error);
				state->load.staging.preparePlan();
			}
			state->load.cancelled = !saved && workerControl.isCancelled();
		});
		connect(worker, &QThread::finished, this, [this, state, finished]() { finish(state->load.ready()); finished(); });
		begin(worker);
	}

	PackageLoadResult draftResult()
	{
		// Return the commit result even if shutdown ended the modal event loop.
		if (m_worker && m_worker->isRunning()) { m_state->cancel = true; m_worker->wait(); }
		return m_state->load;
	}

	void edit(const PackageStagingModel& plan, const QString& label, const PackageEditOperation& operation,
		const PackageReadControl& control, std::function<void(const PackageEditResult&)> finished)
	{
		m_reading = true; m_state->checkingStagedView = true;
		setObjectName(QStringLiteral("packageEditDialog"));
		setWindowTitle(label); setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Checking staged package…"));
		m_cancel->setToolTip(tr("Cancel this edit and keep the current package and edit history."));
		const auto state = m_state;
		const auto workerControl = readControl(state, control);
		auto* worker = QThread::create([state, plan, operation, workerControl]() {
			state->edit.staging = plan;
			if (!workerControl.isCancelled()) {
				state->edit.applied = operation(state->edit.staging, &state->edit.error, workerControl);
				if (state->edit.applied && state->edit.error.isEmpty() && !workerControl.isCancelled()) {
					// Undo/Redo may intentionally revisit an unavailable legacy view.
					// Retain its diagnostic snapshot instead of retrying on the UI thread.
					state->edit.staging.preparePlan(nullptr, workerControl);
					if (!workerControl.isCancelled()) { state->edit.view = packagePlannedArchive(state->edit.staging, nullptr, workerControl); }
				}
			}
			state->edit.cancelled = workerControl.isCancelled();
			if (!state->edit.ready()) {
				state->edit.staging = plan; state->edit.view.clear(); state->edit.applied = false;
				if (state->edit.cancelled) { state->edit.error.clear(); }
				else if (state->edit.error.isEmpty()) { state->edit.error = tr("Package edit could not be applied."); }
			}
		});
		connect(worker, &QThread::finished, this, [this, state, plan, finished]() {
			// Cancel may be dispatched after the worker exits but before this
			// queued handoff. No document mutation has been published yet.
			if (state->cancel.load()) {
				state->edit = PackageEditResult(); state->edit.staging = plan; state->edit.cancelled = true;
			}
			finish(state->edit.ready()); finished(state->edit);
		});
		begin(worker);
	}

	void stage(const PackageStagingModel& plan, const QVector<PackageStageFileRequest>& files,
		const PackageReadControl& control, std::function<void(const PackageStageFilesResult&)> finished)
	{
		m_reading = true;
		setObjectName(QStringLiteral("packageStageFilesDialog"));
		setWindowTitle(tr("Stage Package Files"));
		setAccessibleName(windowTitle());
		setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Retaining %n imported file(s)…", nullptr, static_cast<int>(files.size())));
		m_cancel->setToolTip(tr("Cancel while reading the current file. This group of files will not be staged."));
		const auto state = m_state;
		auto importControl = control;
		if (!importControl.importOptions) {
			const StudioSettings settings; auto options = std::make_shared<PackageImportOptions>();
			options->maximumBytes = static_cast<qint64>(settings.packageImportMaximumMiB()) * 1024 * 1024;
			options->maximumFiles = settings.packageImportMaximumFiles(); importControl.importOptions = options;
		}
		const auto workerControl = readControl(state, importControl);
		auto* worker = QThread::create([state, plan, files, workerControl]() {
			state->stage.staging = plan;
			QString groupError;
			if (!state->stage.staging.beginOperationGroup(tr("Stage package files"), &groupError)) { state->stage.errors << groupError; return; }
			for (const auto& file : files) {
				if (workerControl.isCancelled()) { state->stage.cancelled = true; break; }
				QString error;
				const bool accepted = file.replace
					? (file.sourceOrdinal >= 0 ? state->stage.staging.replaceOccurrence(file.sourceOrdinal, file.sourcePath, &error, workerControl)
						: state->stage.staging.replaceFile(file.virtualPath, file.sourcePath, &error, workerControl))
					: state->stage.staging.addFile(file.sourcePath, file.virtualPath, &error, file.resolution, workerControl);
				if (workerControl.isCancelled()) { state->stage.cancelled = true; break; }
				if (accepted) { ++state->stage.accepted; state->stage.acceptedPaths << file.virtualPath; }
				else { state->stage.errors << tr("%1: %2").arg(file.virtualPath, error); }
			}
			{
				QMutexLocker lock(&state->pathMutex);
				state->bytesRead = 0; state->sourceBytes = 0; state->sourcePath.clear(); state->checkingStagedView = true;
			}
			const bool committed = state->stage.staging.endOperationGroup(!state->stage.cancelled, &groupError, workerControl);
			state->stage.cancelled = state->stage.cancelled || workerControl.isCancelled();
			if (!committed || state->stage.cancelled) {
				state->stage.staging = plan;
				state->stage.accepted = 0;
				state->stage.acceptedPaths.clear();
				if (state->stage.cancelled) { state->stage.errors.clear(); }
				else { state->stage.errors << groupError; }
			}
		});
		connect(worker, &QThread::finished, this, [this, state, plan, finished]() {
			if (state->cancel.load()) {
				state->stage = PackageStageFilesResult(); state->stage.staging = plan; state->stage.cancelled = true;
			}
			finish(!state->stage.cancelled && state->stage.errors.isEmpty()); finished(state->stage);
		});
		begin(worker);
	}

	void palette(const QStringList& sources, const QString& paletteId, const PackageReadControl& control,
		std::function<void(const IdTechPaletteResolution&, bool)> finished)
	{
		m_reading = true;
		setObjectName(QStringLiteral("packagePaletteDialog"));
		setWindowTitle(tr("Load Installation Palette"));
		setAccessibleName(windowTitle());
		setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Reading game packages for the %1 palette…").arg(paletteId));
		m_cancel->setToolTip(tr("Cancel palette loading and use the fallback colors."));
		const auto state = m_state;
		const auto workerControl = readControl(state, control);
		auto* worker = QThread::create([state, sources, paletteId, workerControl]() {
			for (const auto& path : sources) {
				if (workerControl.isCancelled()) { break; }
				PackageArchive archive;
				QString error;
				if (!archive.load(path, &error, workerControl)) { continue; }
				const auto resolution = resolveIdTechPalette(archive, paletteId);
				if (resolution.fromPackage) {
					state->palette = resolution;
					state->palette.sourcePackagePath = path;
					break;
				}
			}
			state->paletteCancelled = workerControl.isCancelled();
			if (state->paletteCancelled) { state->palette = {}; }
		});
		connect(worker, &QThread::finished, this, [this, state, finished]() {
			if (state->cancel.load()) { state->palette = {}; state->paletteCancelled = true; }
			finish(!state->paletteCancelled);
			finished(state->palette, state->paletteCancelled);
		});
		begin(worker);
	}

	void extract(std::shared_ptr<const PackageArchiveReader> source, const PackageExtractionRequest& request,
		std::function<void()> finished)
	{
		m_reading = true;
		setObjectName(QStringLiteral("packageExtractionDialog"));
		setWindowTitle(tr("Extract Package"));
		setAccessibleName(windowTitle());
		setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Extracting package entries…"));
		m_details->setPlainText(request.targetDirectory);
		m_cancel->setToolTip(tr("Cancel the current file. Files already extracted are kept."));
		const auto state = m_state;
		auto workerRequest = request;
		workerRequest.control = readControl(state, request.control);
		auto* worker = QThread::create([state, source, workerRequest]() {
			state->extraction = extractPackageEntries(*source, workerRequest);
		});
		connect(worker, &QThread::finished, this, [this, state, finished]() {
			finish(state->extraction.succeeded());
			if (finished) { finished(); }
		});
		begin(worker);
	}

	PackageExtractionReport extractionResult()
	{
		// Also handles application shutdown interrupting the modal event loop:
		// join before returning the authoritative count of committed files.
		if (m_running) { m_state->cancel = true; m_worker->wait(); }
		return m_state->extraction;
	}

	void copy(std::shared_ptr<const PackageArchiveReader> source, const PackageCopyRequest& request,
		std::function<void()> finished)
	{
		m_reading = true;
		setObjectName(QStringLiteral("packageCopyDialog"));
		setWindowTitle(tr("Prepare Package Copies"));
		setAccessibleName(windowTitle());
		setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Preparing package copies…"));
		m_details->setPlainText(tr("Temporary copies are handed off only after every selected entry is verified."));
		m_cancel->setToolTip(tr("Cancel while reading the current file. This temporary batch will be discarded."));
		const auto state = m_state;
		auto workerRequest = request;
		workerRequest.control = readControl(state, request.control);
		auto* worker = QThread::create([state, source, workerRequest] {
			if (source) { state->copies = preparePackageCopyEntries(*source, workerRequest); }
			else { state->copies.error = tr("No package is open."); }
		});
		const auto complete = [this, state, finished] {
			finish(state->copies.succeeded());
			if (!state->copies.succeeded() && (!state->copies.cancelled || !state->copies.error.isEmpty())) {
				m_summary->setText(tr("Unable to prepare package copies. No files were handed off."));
				m_details->setPlainText(state->copies.error);
			} else if (finished) { finished(); }
		};
		connect(worker, &QThread::finished, this, [this, state, complete] {
			if (!state->copies.prepared()) { complete(); return; }
			// The GUI decides adoption only after any earlier Cancel/Close events.
			// Reservation publication or batch disposal stays on a worker.
			const bool discard = state->cancel.load(); m_finalizingCopies = true; m_cancel->setEnabled(false);
			m_summary->setText(discard ? tr("Discarding prepared package copies…") : tr("Finishing prepared package copies…"));
			auto* finalizer = QThread::create([state, discard] {
				if (discard) { discardPreparedPackageCopies(&state->copies); }
				else { publishPreparedPackageCopies(&state->copies); }
			});
			connect(finalizer, &QThread::finished, this, complete); begin(finalizer);
		});
		begin(worker);
	}

	PackageCopyResult copyResult()
	{
		if (m_running) {
			// A shutdown can end the modal loop before its completion callback.
			// Never begin an external handoff after the caller stopped waiting.
			m_state->cancel = true; m_worker->wait();
			if (m_state->copies.prepared()) {
				auto* cleanup = QThread::create([state = m_state] { discardPreparedPackageCopies(&state->copies); });
				cleanup->start(); cleanup->wait(); delete cleanup;
			}
			m_state->copies.cancelled = true;
			m_state->copies.paths.clear(); m_state->copies.storage.reset(); m_state->copies.session.reset();
		}
		return m_state->copies;
	}

	void compare(const PackageArchive& source, const QString& otherPath, const PackageStagingModel* plan,
		std::function<void(const PackageCompareResult&)> finished, PackageCompareRequest request)
	{
		m_reading = true;
		m_comparing = true;
		m_cancel->setToolTip(tr("Cancel comparison while reading the current file. Completed results remain available."));
		setObjectName(QStringLiteral("packageComparisonDialog"));
		setWindowTitle(plan ? tr("Review Staged Changes") : tr("Compare Packages"));
		setAccessibleName(windowTitle());
		m_summary->setText(tr("Comparing package entries…"));
		m_details->setPlainText(plan ? tr("Source: %1\nCompared with: current staged changes").arg(source.sourcePath())
			: tr("Source: %1\nCompared with: %2").arg(source.sourcePath(), otherPath));
		m_compareContext = m_details->toPlainText();
		m_details->setMaximumHeight(fontMetrics().lineSpacing() * 9 + 20);
		auto* filterRow = new QHBoxLayout;
		auto* filter = new QLineEdit;
		filter->setObjectName(QStringLiteral("packageComparisonFilter"));
		filter->setAccessibleName(tr("Filter package comparison"));
		filter->setPlaceholderText(tr("Filter paths, status, or comparison method"));
		filter->setClearButtonEnabled(true);
		auto* changesOnly = new QCheckBox(tr("Changes and unchecked files only"));
		changesOnly->setObjectName(QStringLiteral("packageComparisonChangesOnly"));
		changesOnly->setAccessibleName(tr("Show changes and unchecked files only"));
		changesOnly->setChecked(true);
		filterRow->addWidget(filter, 1);
		filterRow->addWidget(changesOnly);
		m_layout->insertLayout(2, filterRow);
		auto* tree = new QTreeWidget;
		tree->setObjectName(QStringLiteral("packageComparisonEntries"));
		tree->setAccessibleName(tr("Package comparison entries"));
		tree->setAccessibleDescription(tr("Before and after paths, sizes, and the evidence used to compare each file. Select a row for details."));
		tree->setRootIsDecorated(false);
		tree->setAlternatingRowColors(true);
		tree->setHeaderLabels({tr("Status"), tr("Source path"), tr("Result path"), tr("Before (bytes)"), tr("After (bytes)"), tr("Evidence")});
		tree->header()->setSectionResizeMode(QHeaderView::Interactive);
		tree->header()->setStretchLastSection(false);
		m_layout->insertWidget(3, tree, 3);
		tree->ensurePolished();
		tree->header()->ensurePolished();
		// Size headers from the translated text and actual font. At large text
		// scales the horizontal scrollbar keeps every column reachable.
		for (int column = 0; column < tree->columnCount(); ++column) {
			const int headerWidth = qMax(tree->header()->sectionSizeHint(column),
				tree->header()->fontMetrics().horizontalAdvance(tree->headerItem()->text(column)) + 32);
			const int contentWidth = tree->fontMetrics().horizontalAdvance(column == 1 || column == 2
				? QStringLiteral("textures/folder/asset.ext") : QStringLiteral("Not compared")) + 24;
			tree->setColumnWidth(column, qMax(headerWidth, contentWidth));
		}
		const auto refilter = [tree, filter, changesOnly]() {
			const QString query = filter->text().trimmed();
			for (int row = 0; row < tree->topLevelItemCount(); ++row) {
				auto* item = tree->topLevelItem(row);
				QStringList text;
				for (int column = 0; column < tree->columnCount(); ++column) { text << item->text(column); }
				item->setHidden((changesOnly->isChecked() && item->data(0, Qt::UserRole + 1).toBool())
					|| !text.join(QLatin1Char(' ')).contains(query, Qt::CaseInsensitive));
			}
		};
		connect(filter, &QLineEdit::textChanged, this, refilter);
		connect(changesOnly, &QCheckBox::toggled, this, refilter);
		connect(tree, &QTreeWidget::itemSelectionChanged, this, [this, tree]() {
			if (auto* item = tree->currentItem()) { m_details->setPlainText(item->data(0, Qt::UserRole).toString()); }
		});
		auto* exportButton = m_buttons->addButton(tr("Export JSON…"), QDialogButtonBox::ActionRole);
		exportButton->setObjectName(QStringLiteral("exportPackageComparison"));
		exportButton->setAccessibleName(tr("Export package comparison as JSON"));
		exportButton->setEnabled(false);
		connect(exportButton, &QPushButton::clicked, this, [this]() {
			const QString path = QFileDialog::getSaveFileName(this, tr("Export Comparison"), QString(), tr("JSON Reports (*.json)"));
			if (path.isEmpty()) { return; }
			if (normalizeDocumentWatchPath(path) == normalizeDocumentWatchPath(m_state->comparison.leftSource)
				|| normalizeDocumentWatchPath(path) == normalizeDocumentWatchPath(m_state->comparison.rightSource)) {
				m_summary->setText(tr("Choose a report path separate from the compared packages."));
				return;
			}
			QSaveFile file(path);
			const QByteArray bytes = packageCompareJsonBytes(m_state->comparison);
			if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
				m_summary->setText(tr("Unable to export the report: %1").arg(file.errorString()));
			} else { m_summary->setText(tr("Comparison report saved: %1").arg(path)); }
		});
		const auto state = m_state;
		const auto planSnapshot = plan ? std::make_shared<PackageStagingModel>(*plan) : nullptr;
		if (request.leftLabel.isEmpty()) request.leftLabel = tr("Source");
		if (request.rightLabel.isEmpty()) request.rightLabel = planSnapshot ? tr("Staged result") : tr("Other package");
		const auto previousCancel = request.isCancelled;
		const auto previousProgress = request.progress;
		const auto previousBytes = request.byteProgress;
		request.isCancelled = [state, previousCancel]() { return state->cancel.load() || (previousCancel && previousCancel()); };
		request.progress = [state, previousProgress](int completed, int total) {
			state->completed = completed; state->total = total;
			if (previousProgress) previousProgress(completed, total);
		};
		request.byteProgress = [state, previousBytes, left = request.leftLabel, right = request.rightLabel](PackageCompareSource side, const QString& path, quint64 done, quint64 total) {
			{
				QMutexLocker lock(&state->pathMutex);
				state->sourcePath = QStringLiteral("%1: %2").arg(side == PackageCompareSource::Left ? left : right, path);
				state->bytesRead = done; state->sourceBytes = total;
			}
			if (previousBytes) previousBytes(side, path, done, total);
		};
		auto* worker = QThread::create([state, source, otherPath, planSnapshot, request]() {
			if (planSnapshot) { state->comparison = comparePackageToPlan(source, *planSnapshot, request); }
			else {
				PackageArchive other;
				QString error;
				auto control = readControl(state);
				control.isCancelled = request.isCancelled;
				if (!other.load(otherPath, &error, control)) {
					state->comparison.cancelled = request.isCancelled();
					state->comparison.warnings << error;
					return;
				}
				state->comparison = comparePackages(source, other, request);
			}
		});
		connect(worker, &QThread::finished, this, [this, state, tree, refilter, exportButton, finished]() {
			const auto& result = state->comparison;
			finish(result.completed);
			const int differences = result.summary.addedCount + result.summary.removedCount + result.summary.changedCount + result.summary.caseOnlyCount;
			m_summary->setText(result.cancelled ? tr("Comparison cancelled. Partial results are shown.")
				: !result.completed ? tr("Comparison blocked. Inspect the details before saving.")
				: tr("%1 differences · %2 unchanged · %3 unchecked").arg(differences).arg(result.summary.identicalCount).arg(result.summary.uncomparedCount));
			for (const auto& entry : result.entries) {
				const QString evidence = !entry.hasLeft ? tr("New entry") : !entry.hasRight ? tr("Removed entry")
					: entry.noteId.isEmpty() ? packageCompareContentDisplayName(entry.content) : entry.noteId;
				auto* item = new QTreeWidgetItem(tree, {packageCompareStatusDisplayName(entry.status), entry.leftPath, entry.rightPath,
					entry.hasLeft ? locale().toString(entry.leftBytes) : QString(), entry.hasRight ? locale().toString(entry.rightBytes) : QString(), evidence});
				item->setData(0, Qt::UserRole + 1, entry.status == PackageCompareStatus::Identical && entry.content != PackageCompareContent::NotCompared);
				item->setData(0, Qt::UserRole, tr("Source: %1\nResult: %2\nEvidence: %3\nSource hash: %4\nResult hash: %5\nOccurrence: %6\nSize change: %7 bytes")
					.arg(entry.leftPath, entry.rightPath, evidence, entry.leftHash, entry.rightHash).arg(entry.occurrence + 1).arg(entry.sizeDelta));
				for (int column = 0; column < tree->columnCount(); ++column) { item->setToolTip(column, item->text(column)); }
			}
			refilter();
			m_details->setPlainText(packageCompareText(result));
			exportButton->setEnabled(true);
			if (finished) { finished(result); }
		});
		begin(worker);
	}

	void validate(const PackageArchive& source, std::function<void(const PackageValidationReport&)> finished, PackageValidationRequest request)
	{
		m_validating = true;
		setObjectName(QStringLiteral("packageValidationDialog"));
		setWindowTitle(tr("Validate Package"));
		setAccessibleName(windowTitle());
		m_summary->setText(tr("Verifying package contents…"));
		m_details->setPlainText(tr("Source: %1\nChecks: complete payloads, sizes, CRC-32 where available, and SHA-256.").arg(source.sourcePath()));
		m_cancel->setToolTip(tr("Cancel validation while reading the current file."));
		auto* exportButton = m_buttons->addButton(tr("Export JSON…"), QDialogButtonBox::ActionRole);
		exportButton->setObjectName(QStringLiteral("exportPackageValidation"));
		exportButton->setAccessibleName(tr("Export package validation as JSON"));
		exportButton->setEnabled(false);
		connect(exportButton, &QPushButton::clicked, this, [this]() {
			const QString path = QFileDialog::getSaveFileName(this, tr("Export Validation"), QString(), tr("JSON Reports (*.json)"));
			if (path.isEmpty()) { return; }
			if (normalizeDocumentWatchPath(path) == normalizeDocumentWatchPath(m_state->validation.sourcePath)
				|| (QFileInfo(m_state->validation.sourcePath).isDir() && packagePathIsInsideDirectory(m_state->validation.sourcePath, path))) {
				m_summary->setText(tr("Choose a report path outside the package being validated."));
				return;
			}
			QSaveFile file(path);
			const QByteArray bytes = QJsonDocument(packageValidationJson(m_state->validation)).toJson(QJsonDocument::Indented);
			if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
				m_summary->setText(tr("Unable to export the report: %1").arg(file.errorString()));
			} else { m_summary->setText(tr("Validation report saved: %1").arg(path)); }
		});
		const auto state = m_state;
		const auto previousCancel = request.isCancelled;
		const auto previousProgress = request.progress;
		request.isCancelled = [state, previousCancel]() { return state->cancel.load() || (previousCancel && previousCancel()); };
		request.progress = [state, previousProgress](int completed, int total, quint64 bytes, const QString& path) {
			{
				QMutexLocker lock(&state->pathMutex);
				state->completed = completed; state->total = total; state->bytesRead = bytes;
			}
			if (previousProgress) { previousProgress(completed, total, bytes, path); }
		};
		auto* worker = QThread::create([state, source, request]() { state->validation = validatePackage(source, request); });
		connect(worker, &QThread::finished, this, [this, state, finished, exportButton]() {
			const auto& report = state->validation;
			finish(report.completed);
			m_summary->setText(report.cancelled ? tr("Package validation cancelled.")
				: report.valid() ? tr("Package validation passed. All %1 files verified.").arg(report.verifiedCount)
				: tr("Package validation did not pass. %1 failed, %2 unchecked, %3 warnings.").arg(report.failedCount).arg(report.uncheckedCount).arg(report.warnings.size()));
			m_details->setPlainText(packageValidationText(report));
			exportButton->setEnabled(true);
			if (finished) { finished(report); }
		});
		begin(worker);
	}

	void save(const PackageStagingModel& plan, PackageWriteRequest request, std::function<void(const PackageSaveResult&)> finished)
	{
		m_writing = true;
		m_cancel->setToolTip(tr("Stop reading or compressing the current file. A save already committing may finish."));
		setObjectName(QStringLiteral("packageSaveDialog"));
		setWindowTitle(tr("Save Package"));
		setAccessibleName(tr("Save staged package"));
		// Holds the shell's package context stable until the write and reload
		// finish; other windows and the event loop remain responsive.
		setWindowModality(Qt::WindowModal);
		m_summary->setText(tr("Writing package…"));
		m_writeContext = tr("Source: %1\nOutput: %2").arg(plan.sourcePath(), request.destinationPath);
		m_details->setPlainText(m_writeContext);
		const auto state = m_state;
		const auto previousCancel = request.isCancelled;
		const auto previousProgress = request.progress;
		const auto previousByteProgress = request.byteProgress;
		request.byteProgress = [state, previousByteProgress](PackageWritePhase phase, const QString& path, quint64 done, quint64 total) {
			{
				QMutexLocker lock(&state->pathMutex);
				state->writePhase = phase;
				state->sourcePath = path;
				state->bytesRead = done;
				state->sourceBytes = total;
			}
			if (previousByteProgress) previousByteProgress(phase, path, done, total);
		};
		request.isCancelled = [state, previousCancel]() { return state->cancel.load() || (previousCancel && previousCancel()); };
		request.progress = [state, previousProgress](int completed, int total, const QString& path) {
			state->completed = completed;
			state->total = total;
			if (previousProgress) { previousProgress(completed, total, path); }
		};
		auto* worker = QThread::create([state, plan, request]() {
			state->save.report = plan.writeArchive(request);
			if (state->save.report.succeeded() && !state->save.report.dryRun) {
				state->reopening = true;
				auto control = readControl(state);
				// Publication already completed: finish adoption even if Cancel
				// was pressed during the commit, and report the actual outcome.
				control.isCancelled = {};
				state->save = reopenSavedPackage(state->save.report, control);
			}
		});
		connect(worker, &QThread::finished, this, [this, state, finished]() {
			const auto& result = state->save;
			finish(result.report.succeeded());
			m_summary->setText(result.report.cancelled ? tr("Save cancelled; the destination was left untouched.")
				: !result.report.succeeded() ? tr("Save failed. The staging plan is still available.")
				: result.report.dryRun ? tr("Dry run complete; no package was written.")
				: !result.reloadError.isEmpty() ? tr("Package saved, but could not be reopened: %1").arg(result.reloadError)
				: tr("Package saved: %1").arg(result.report.outputPath));
			m_details->setPlainText(packageWriteReportText(result.report));
			if (!result.reloadError.isEmpty()) { m_details->appendPlainText(result.reloadError); }
			if (finished) { finished(result); }
		});
		begin(worker);
	}

protected:
	void reject() override
	{
		if (m_running) { cancel(); return; }
		QDialog::reject();
	}
	void closeEvent(QCloseEvent* event) override
	{
		if (m_running) { cancel(); event->ignore(); return; }
		QDialog::closeEvent(event);
	}

private:
	void setProgressRange(quint64 completed, quint64 total)
	{
		m_progress->setRange(0, total ? 1000 : m_reducedMotion ? 1 : 0);
		m_progress->setValue(packageProgressValue(completed, total));
	}
	void setProgressText(const QString& text, const QString& exact = {})
	{
		m_progress->setFormat(text);
		m_progress->setAccessibleDescription(exact.isEmpty() ? text : text + QLatin1Char('\n') + exact);
		m_progress->setToolTip(exact);
	}
	bool m_comparing = false;
	QString m_compareContext;
	bool m_writing = false;
	QString m_writeContext;
	void cancel()
	{
		if (m_finalizingCopies) { m_cancel->setEnabled(false); return; }
		if (m_state->reopening.load()) {
			m_cancel->setEnabled(false);
			m_summary->setText(tr("The package was saved. Finishing the reload…"));
			return;
		}
		m_state->cancel = true;
		m_cancel->setEnabled(false);
		m_summary->setText(m_validating ? tr("Cancelling validation…") : tr("Cancelling package operation…"));
	}
	void begin(QThread* worker)
	{
		m_worker = worker;
		m_running = true;
		setProperty("operationRunning", true);
		m_timer->start(75);
		connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { m_state->cancel = true; m_worker->wait(); });
		// Parent ownership plus a join in the destructor prevents a dangling
		// worker pointer if its deferred deletion precedes the dialog's.
		worker->setParent(this);
		worker->start();
		show();
	}
	void finish(bool complete)
	{
		m_worker->wait();
		m_running = false;
		setProperty("operationRunning", false);
		m_timer->stop();
		m_cancel->setEnabled(false);
		m_progress->setRange(0, 1);
		m_progress->setFormat(QStringLiteral("%p%"));
		m_progress->setValue(complete ? 1 : 0);
		m_progress->setAccessibleDescription({});
		m_progress->setToolTip({});
	}

	std::shared_ptr<WorkState> m_state = std::make_shared<WorkState>();
	QThread* m_worker = nullptr;
	bool m_running = false;
	bool m_validating = false;
	bool m_reading = false;
	bool m_showingSourceProtections = false;
	QString m_previousReadSummary;
	bool m_finalizingCopies = false;
	bool m_reducedMotion = false;
	QVBoxLayout* m_layout = nullptr;
	QLabel* m_summary = nullptr;
	QProgressBar* m_progress = nullptr;
	QPlainTextEdit* m_details = nullptr;
	QDialogButtonBox* m_buttons = nullptr;
	QPushButton* m_cancel = nullptr;
	QTimer* m_timer = nullptr;
};

} // namespace

PackageSaveResult reopenSavedPackage(const PackageWriteReport& report, const PackageReadControl& control)
{
	PackageSaveResult result;
	result.report = report;
	if (!report.succeeded() || report.dryRun) { return result; }
	if (result.archive.load(report.outputPath, &result.reloadError, control)) {
		if (QString::fromLatin1(result.archive.contentId().toHex()) != report.sha256) {
			result.reloadError = QCoreApplication::translate("VibeStudioPackageDialog", "The saved package changed before it could be reopened. Open it again to review the current file.");
			result.archive.clear();
		} else {
			if (result.staging.loadBaseArchive(result.archive, &result.reloadError, control)) { result.staging.preparePlan(&result.reloadError, control); }
		}
	}
	return result;
}

PackageLoadResult runPackageLoadDialog(QWidget* parent, const QString& path, const PackageReadControl& control)
{
	PackageLoadResult result;
	result.cancelled = true;
	PackageOperationDialog dialog(parent);
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.load(path, control, [&](const auto& loaded) { result = loaded; dialog.accept(); });
	if (dialog.property("operationRunning").toBool()) { dialog.exec(); }
	return result;
}

PackageLoadResult runPackageDraftSaveDialog(QWidget* parent, const PackageStagingModel& plan, const QString& path,
	bool overwrite, const PackageReadControl& control)
{
	PackageOperationDialog dialog(parent);
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.saveDraft(plan, path, overwrite, control, [&]() { dialog.accept(); });
	if (dialog.property("operationRunning").toBool()) { dialog.exec(); }
	return dialog.draftResult();
}

PackageLoadResult runPackageRecoveryRestoreDialog(QWidget* parent, const QString& directory, const QString& id,
	const QByteArray& manifestSha256, const QString& destination, const PackageReadControl& control)
{
	PackageOperationDialog dialog(parent);
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.restoreRecovery(directory, id, manifestSha256, destination, control, [&]() { dialog.accept(); });
	if (dialog.property("operationRunning").toBool()) { dialog.exec(); }
	return dialog.draftResult();
}

PackageEditResult runPackageEditDialog(QWidget* parent, const PackageStagingModel& plan, const QString& label,
	const PackageEditOperation& edit, const PackageReadControl& control)
{
	PackageEditResult result; result.staging = plan; result.cancelled = true;
	PackageOperationDialog dialog(parent);
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.edit(plan, label, edit, control, [&](const auto& edited) { result = edited; dialog.accept(); });
	if (dialog.property("operationRunning").toBool()) { dialog.exec(); }
	return result;
}

PackageStageFilesResult runPackageStagingDialog(QWidget* parent, const PackageStagingModel& plan,
	const QVector<PackageStageFileRequest>& files, const PackageReadControl& control)
{
	PackageStageFilesResult result;
	result.staging = plan;
	result.cancelled = true;
	PackageOperationDialog dialog(parent);
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.stage(plan, files, control, [&](const auto& staged) { result = staged; dialog.accept(); });
	if (dialog.property("operationRunning").toBool()) { dialog.exec(); }
	return result;
}

IdTechPaletteResolution runPackagePaletteDialog(QWidget* parent, const QStringList& sources, const QString& paletteId,
	bool* cancelled, const PackageReadControl& control)
{
	IdTechPaletteResolution result;
	if (cancelled) { *cancelled = true; }
	PackageOperationDialog dialog(parent);
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.palette(sources, paletteId, control, [&](const auto& loaded, bool wasCancelled) {
		result = loaded;
		if (cancelled) { *cancelled = wasCancelled; }
		dialog.accept();
	});
	if (dialog.property("operationRunning").toBool()) { dialog.exec(); }
	return result;
}

PackageExtractionReport runPackageExtractionDialog(QWidget* parent, std::shared_ptr<const PackageArchiveReader> source,
	const PackageExtractionRequest& request)
{
	PackageOperationDialog dialog(parent);
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.extract(std::move(source), request, [&]() { dialog.accept(); });
	if (dialog.property("operationRunning").toBool()) { dialog.exec(); }
	return dialog.extractionResult();
}

PackageCopyResult runPackageCopyDialog(QWidget* parent, std::shared_ptr<const PackageArchiveReader> source,
	const PackageCopyRequest& request)
{
	PackageOperationDialog dialog(parent);
	dialog.setAttribute(Qt::WA_DeleteOnClose, false);
	dialog.copy(std::move(source), request, [&] { dialog.accept(); });
	if (dialog.property("operationRunning").toBool()) { dialog.exec(); }
	return dialog.copyResult();
}

QDialog* showPackageComparisonDialog(QWidget* parent, const PackageArchive& source, const QString& otherPath,
	const PackageStagingModel* plan, std::function<void(const PackageCompareResult&)> finished, PackageCompareRequest request)
{
	auto* dialog = new PackageOperationDialog(parent);
	dialog->compare(source, otherPath, plan, std::move(finished), std::move(request));
	return dialog;
}

QDialog* showPackageSaveDialog(QWidget* parent, const PackageStagingModel& plan, const PackageWriteRequest& request,
	std::function<void(const PackageSaveResult&)> finished)
{
	auto* dialog = new PackageOperationDialog(parent);
	dialog->save(plan, request, std::move(finished));
	return dialog;
}

QDialog* showPackageValidationDialog(QWidget* parent, const PackageArchive& source,
	std::function<void(const PackageValidationReport&)> finished, const PackageValidationRequest& request)
{
	auto* dialog = new PackageOperationDialog(parent);
	dialog->validate(source, std::move(finished), request);
	return dialog;
}

} // namespace vibestudio
