#include "app/package_publication_dialog.h"
#include "app/package_action_button.h"
#include "core/studio_settings.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio {

struct PackagePublicationDialog::Progress {
	QMutex mutex;
	QString path;
	qint64 done = 0, total = 0;
};

PackagePublicationDialog::PackagePublicationDialog(QStringList directories, QWidget* parent)
	: QDialog(parent), m_directories(std::move(directories)), m_knownDirectories(m_directories),
	  m_cancel(std::make_shared<std::atomic_bool>(false)), m_progressState(std::make_shared<Progress>())
{
	setObjectName(QStringLiteral("packagePublicationDialog")); setWindowTitle(tr("Interrupted Saves"));
	setAccessibleName(tr("Interrupted save recovery manager")); setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal);
	if (parent) { setLayoutDirection(parent->layoutDirection()); }
	auto* outer = new QVBoxLayout(this); auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setAccessibleName(tr("Output folders and interrupted saves"));
	auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body); outer->addWidget(scroll, 1);
	m_folders = new QPlainTextEdit; m_folders->setReadOnly(true); m_folders->setObjectName(QStringLiteral("packagePublicationFolders"));
	m_folders->setAccessibleName(tr("Selected output folders")); m_folders->setMaximumHeight(fontMetrics().height() * 4); layout->addWidget(m_folders);
	const auto button = [&](const QString& text, const char* name) {
		auto* item = new PackageActionButton(text); item->setObjectName(QString::fromLatin1(name)); item->setAccessibleName(text);
		item->setAutoDefault(false); layout->addWidget(item); return item;
	};
	m_choose = button(tr("Choose Output Folder…"), "packagePublicationChoose");
	m_known = button(tr("Use Known Output Folders"), "packagePublicationKnown");
	m_status = new QLabel(tr("Choose an output folder to review interrupted saves.")); m_status->setObjectName(QStringLiteral("packagePublicationStatus"));
	m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); m_status->setAccessibleName(tr("Interrupted save review status")); layout->addWidget(m_status);
	m_progress = new QProgressBar; m_progress->setAccessibleName(tr("Save recovery verification progress")); m_progress->hide(); layout->addWidget(m_progress);
	m_table = new QTableWidget(0, 3); m_table->setObjectName(QStringLiteral("packagePublicationJournals"));
	m_table->setAccessibleName(tr("Interrupted save journals")); m_table->setHorizontalHeaderLabels({tr("Output"), tr("Folder"), tr("Status")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setWordWrap(false);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->horizontalHeader()->setTextElideMode(Qt::ElideRight);
	m_table->setTextElideMode(Qt::ElideMiddle);
	for (int column = 0; column < m_table->columnCount(); ++column) { m_table->horizontalHeaderItem(column)->setToolTip(m_table->horizontalHeaderItem(column)->text()); } m_table->setMinimumHeight(fontMetrics().height() * 7); layout->addWidget(m_table);
	m_details = new QPlainTextEdit; m_details->setObjectName(QStringLiteral("packagePublicationDetails")); m_details->setReadOnly(true);
	m_details->setAccessibleName(tr("Save paths, review checksum and verification details")); m_details->setMinimumHeight(fontMetrics().height() * 7); layout->addWidget(m_details);
	m_verify = button(tr("Verify Selected Save"), "packagePublicationVerify");
	m_backup = button(tr("Choose External Backup…"), "packagePublicationBackup");
	m_finish = button(tr("Finish Reviewed Save…"), "packagePublicationFinish");
	m_open = button(tr("Open Current Output"), "packagePublicationOpen");
	m_refresh = button(tr("Refresh"), "packagePublicationRefresh");
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); buttons->button(QDialogButtonBox::Close)->setAccessibleName(tr("Close interrupted save recovery")); outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &PackagePublicationDialog::reject);
	connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int, int previousRow, int) {
		if (row != previousRow) { updateSelection(); }
	});
	connect(m_refresh, &QPushButton::clicked, this, &PackagePublicationDialog::reload);
	connect(m_known, &QPushButton::clicked, this, [this]() { m_directories = m_knownDirectories; reload(); });
	connect(m_choose, &QPushButton::clicked, this, [this]() {
		const auto path = QFileDialog::getExistingDirectory(this, tr("Choose Output Folder"), m_directories.value(0), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
		if (path.isEmpty()) { return; } StudioSettings().rememberPackagePublicationDirectory(path); m_directories = {path}; reload();
	});
	connect(m_verify, &QPushButton::clicked, this, [this]() { verifySelected(); });
	connect(m_backup, &QPushButton::clicked, this, [this]() {
		if (!selected()) { return; }
		const auto path = QFileDialog::getSaveFileName(this, tr("Select Recorded Backup Destination"), selected()->backupPath, tr("All Files (*)"), nullptr,
			QFileDialog::DontConfirmOverwrite | QFileDialog::DontResolveSymlinks);
		if (!path.isEmpty()) { verifySelected(path); }
	});
	connect(m_finish, &QPushButton::clicked, this, &PackagePublicationDialog::finishSelected);
	connect(m_open, &QPushButton::clicked, this, [this]() {
		if (!m_open->isEnabled() || !openOutput) { return; }
		const auto handler = openOutput; const auto path = m_review.destinationPath; accept(); handler(path);
	});
	auto* timer = new QTimer(this); timer->setInterval(100);
	connect(timer, &QTimer::timeout, this, [this]() {
		if (!busy() || m_closeRequested) { return; }
		QMutexLocker lock(&m_progressState->mutex);
		m_progress->setAccessibleDescription(m_progressState->path);
		m_progress->setRange(0, m_progressState->total > 0 ? 1000 : 0);
		if (m_progressState->total > 0) {
			m_progress->setFormat(tr("%p% of current file"));
			m_progress->setValue(static_cast<int>(qBound(0.0, 1000.0 * m_progressState->done / m_progressState->total, 1000.0)));
		}
	}); timer->start();
	resize(QSize(940, 760).boundedTo(screen()->availableGeometry().size() - QSize(40, 40))); updateControls();
	if (!m_directories.isEmpty()) { QTimer::singleShot(0, this, [this]() { reload(); }); }
}

PackagePublicationDialog::~PackagePublicationDialog()
{
	m_cancel->store(true); if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool PackagePublicationDialog::busy() const { return m_thread != nullptr; }
void PackagePublicationDialog::reject()
{
	if (busy()) { m_closeRequested = true; m_cancel->store(true); m_status->setText(tr("Stopping save recovery safely…")); return; } QDialog::reject();
}
PackageReadControl PackagePublicationDialog::readControl() const
{
	PackageReadControl control; control.isCancelled = [cancel = m_cancel]() { return cancel->load(); };
	control.progress = [state = m_progressState](const QString& path, qint64 done, qint64 total) {
		QMutexLocker lock(&state->mutex); state->path = path; state->done = done; state->total = total;
	}; return control;
}
void PackagePublicationDialog::start(std::function<void()> work, std::function<void()> complete)
{
	if (busy()) { return; } m_cancel->store(false);
	{ QMutexLocker lock(&m_progressState->mutex); m_progressState->path.clear(); m_progressState->done = 0; m_progressState->total = 0; }
	m_thread = QThread::create(std::move(work)); m_progress->setRange(0, 0); m_progress->show(); setProperty("operationRunning", true); updateControls();
	connect(m_thread, &QThread::finished, this, [this, complete = std::move(complete)]() {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; m_progress->hide(); setProperty("operationRunning", false);
		complete();
		if (m_closeRequested) { QDialog::reject(); return; } updateControls();
	}); m_thread->start();
}
void PackagePublicationDialog::reload()
{
	if (busy()) { return; } m_review = {}; m_confirmedBackup.clear(); m_needsBackup = false;
	m_folders->setPlainText(m_directories.join(QLatin1Char('\n'))); m_status->setText(tr("Finding interrupted saves in the selected output folders…"));
	auto inventory = std::make_shared<PackagePublicationInventory>(); const auto control = readControl();
	start([inventory, paths = m_directories, control]() { *inventory = listPackagePublicationJournals(paths, control); }, [this, inventory]() {
		m_inventory = *inventory;
		{ const QSignalBlocker block(m_table); m_table->setRowCount(m_inventory.journals.size());
			for (qsizetype row = 0; row < m_inventory.journals.size(); ++row) {
				const auto& info = m_inventory.journals.at(row);
				const QStringList cells{QFileInfo(info.destinationPath.isEmpty() ? info.journalPath : info.destinationPath).fileName(),
					QFileInfo(info.journalPath).absolutePath(), info.metadataValid() ? tr("Metadata checked") : tr("Invalid journal")};
				for (int column = 0; column < cells.size(); ++column) {
					auto* item = new QTableWidgetItem(column < 2 ? QChar(0x2068) + cells.at(column) + QChar(0x2069) : cells.at(column)); item->setToolTip(cells.at(column) + QLatin1Char('\n') + info.journalPath);
					item->setData(Qt::AccessibleTextRole, tr("%1. Journal: %2").arg(cells.at(column), info.journalPath)); m_table->setItem(row, column, item);
				}
			}
			m_table->setCurrentCell(m_inventory.journals.isEmpty() ? -1 : 0, 0);
		}
		updateSelection();
		QStringList status{tr("%n interrupted save journal(s) found. Verify a selection to inspect its content.", nullptr, static_cast<int>(m_inventory.journals.size()))};
		if (m_inventory.cancelled) { status << tr("Folder scan cancelled; this list is incomplete."); }
		if (m_inventory.truncated) { status << tr("Scan limit reached; narrow the output folders."); }
		status += m_inventory.errors; m_status->setText(status.join(QLatin1Char('\n')));
	});
}
const PackagePublicationJournalInfo* PackagePublicationDialog::selected() const
{
	const int row = m_table->currentRow(); return row >= 0 && row < m_inventory.journals.size() ? &m_inventory.journals.at(row) : nullptr;
}
void PackagePublicationDialog::updateSelection()
{
	if (busy()) { return; } m_review = {}; m_confirmedBackup.clear(); m_needsBackup = false;
	if (const auto* info = selected()) { m_status->setText(info->metadataValid() ? tr("Journal metadata checked. Verify the output and retained files before finishing recovery.") : info->error); }
	updateDetails(); updateControls();
}
QString PackagePublicationDialog::stateText() const
{
	if (m_review.finished) { return tr("Recovery completed"); }
	if (m_review.cancelled) { return tr("Recovery cancelled"); }
	if (!m_review.error.isEmpty()) { return tr("Needs review"); }
	if (m_review.requiresBackupConfirmation) { return tr("Select the recorded external backup destination"); }
	if (m_review.canFinish) { return tr("Replacement verified; ready to finish"); }
	if (m_review.state == QStringLiteral("original")) { return tr("Output was not replaced; retained files need review"); }
	if (m_review.state == QStringLiteral("changed")) { return tr("Output changed after the interrupted save"); }
	if (m_review.state == QStringLiteral("missing")) { return tr("Output is missing"); }
	return tr("Content has not been verified");
}
void PackagePublicationDialog::updateDetails()
{
	const auto* info = selected(); if (!info) { m_details->clear(); return; }
	QStringList lines{tr("Journal: %1").arg(info->journalPath), tr("Output: %1").arg(info->destinationPath),
		tr("Retained replacement: %1").arg(info->replacementPath), tr("Retained original: %1").arg(info->originalPath),
		tr("Backup: %1").arg(info->backupPath), tr("Journal SHA-256: %1").arg(QString::fromLatin1(info->journalSha256.toHex()))};
	if (!info->error.isEmpty()) { lines << info->error; }
	if (!m_review.journalSha256.isEmpty()) {
		lines << stateText(); if (m_review.backupVerified) { lines << tr("The backup is verified."); }
		if (!m_review.error.isEmpty()) { lines << m_review.error; }
	} else { lines << tr("Output, backup and retained payloads have not been verified."); }
	m_details->setPlainText(lines.join(QLatin1Char('\n')));
}
void PackagePublicationDialog::updateControls()
{
	const bool idle = !busy(); const auto* info = selected(); const bool valid = info && info->metadataValid();
	m_table->setEnabled(idle); m_choose->setEnabled(idle); m_known->setEnabled(idle && !m_knownDirectories.isEmpty()); m_refresh->setEnabled(idle);
	m_verify->setEnabled(idle && valid && !m_review.finished); m_backup->setVisible(m_needsBackup); m_backup->setEnabled(idle && m_needsBackup);
	m_finish->setEnabled(idle && valid && m_review.canFinish && m_review.error.isEmpty() && !m_review.cancelled && !m_review.finished);
	m_open->setEnabled(idle && bool(openOutput) && !m_review.journalSha256.isEmpty() && m_review.error.isEmpty()
		&& (m_review.state == QStringLiteral("original") || m_review.state == QStringLiteral("replacement") || m_review.state == QStringLiteral("changed")));
}
void PackagePublicationDialog::verifySelected(const QString& backup)
{
	if (busy() || !selected() || !selected()->metadataValid()) { return; }
	const auto info = *selected(); auto report = std::make_shared<PackageRecoveryReport>(); const auto control = readControl();
	m_status->setText(tr("Verifying output, backup and retained content…"));
	start([report, info, backup, control]() { *report = recoverPackagePublication(info.journalPath, false, backup, control, info.journalSha256); }, [this, report, backup]() {
		m_review = *report; m_confirmedBackup = report->error.isEmpty() ? backup : QString();
		if (report->error.isEmpty()) { m_needsBackup = report->requiresBackupConfirmation; }
		m_status->setText(report->error.isEmpty() ? stateText() : report->error); updateDetails();
		if (m_table->currentRow() >= 0) {
			auto* item = m_table->item(m_table->currentRow(), 2); item->setText(stateText());
			item->setToolTip(stateText() + QLatin1Char('\n') + report->journalPath);
			item->setData(Qt::AccessibleTextRole, tr("%1. Journal: %2").arg(stateText(), report->journalPath));
		}
	});
}
void PackagePublicationDialog::finishSelected()
{
	if (!m_finish->isEnabled() || !selected()) { return; }
	QMessageBox question(QMessageBox::Question, tr("Finish Save Recovery"),
		tr("Complete the backup and remove verified transaction files for:\n%1\n\nBackup: %2")
			.arg(m_review.destinationPath, m_review.backupPath.isEmpty() ? tr("Not required for a new output") : m_review.backupPath), QMessageBox::Yes | QMessageBox::Cancel, this);
	question.setTextFormat(Qt::PlainText); question.setDefaultButton(QMessageBox::Cancel); if (question.exec() != QMessageBox::Yes) { return; }
	const auto reviewed = m_review; const auto backup = m_confirmedBackup; const auto control = readControl();
	auto report = std::make_shared<PackageRecoveryReport>(); m_status->setText(tr("Finishing the reviewed save…"));
	start([report, reviewed, backup, control]() { *report = recoverPackagePublication(reviewed.journalPath, true, backup, control, reviewed.journalSha256); }, [this, report]() {
		m_review = *report; m_status->setText(report->finished ? tr("Recovery completed. The output is verified.") : report->error); updateDetails();
		if (m_table->currentRow() >= 0) {
			auto* item = m_table->item(m_table->currentRow(), 2); item->setText(stateText());
			item->setToolTip(stateText() + QLatin1Char('\n') + report->journalPath);
			item->setData(Qt::AccessibleTextRole, tr("%1. Journal: %2").arg(stateText(), report->journalPath));
		}
		if (recoveryFinished) { recoveryFinished(*report); }
	});
}

} // namespace vibestudio
