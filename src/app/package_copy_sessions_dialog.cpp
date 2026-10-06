#include "app/package_copy_sessions_dialog.h"
#include "app/package_action_button.h"
#include "core/studio_settings.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <limits>

namespace vibestudio {
namespace {
QString isolated(const QString& value) { return QChar(0x2068) + value + QChar(0x2069); }
}
PackageCopySessionsDialog::PackageCopySessionsDialog(QString directory, QWidget* parent)
	: QDialog(parent), m_directory(std::move(directory))
{
	setObjectName(QStringLiteral("packageCopySessionsDialog")); setWindowTitle(tr("Retained Temporary Copies"));
	setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
	if (parent) { setLayoutDirection(parent->layoutDirection()); }
	m_reducedMotion = StudioSettings(StudioSettings::AccessMode::ReadOnly).accessibilityPreferences().reducedMotion;
	auto* outer = new QVBoxLayout(this); auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setAccessibleName(tr("Temporary copy sessions and details"));
	auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body); outer->addWidget(scroll, 1);
	m_usage = new QLabel; m_usage->setObjectName(QStringLiteral("packageCopySessionsUsage")); m_usage->setWordWrap(true);
	m_usage->setTextFormat(Qt::PlainText); m_usage->setAccessibleName(tr("Reviewed temporary copy usage")); layout->addWidget(m_usage);
	m_sharedUsage = new QLabel; m_sharedUsage->setObjectName(QStringLiteral("packageCopySharedUsage"));
	m_sharedUsage->setWordWrap(true); m_sharedUsage->setTextFormat(Qt::PlainText);
	QSizePolicy sharedTextPolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); sharedTextPolicy.setHeightForWidth(true); m_sharedUsage->setSizePolicy(sharedTextPolicy); m_sharedUsage->setAccessibleName(tr("Shared copy reservations and limits")); layout->addWidget(m_sharedUsage);
	m_table = new QTableWidget(0, 4); m_table->setObjectName(QStringLiteral("packageCopySessionsTable"));
	m_table->setAccessibleName(tr("Retained copy sessions")); m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection); m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setHorizontalHeaderLabels({tr("Session"), tr("Created"), tr("Payload (MiB)"), tr("State")});
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->setMinimumHeight(fontMetrics().height() * 7); layout->addWidget(m_table);
	m_details = new QPlainTextEdit; m_details->setObjectName(QStringLiteral("packageCopySessionsDetails")); m_details->setReadOnly(true);
	m_details->setAccessibleName(tr("Selected copy session paths, storage checksum and errors"));
	m_details->setMinimumHeight(fontMetrics().height() * 7); layout->addWidget(m_details);
	m_discard = new PackageActionButton(tr("Discard Selected Session…")); m_discard->setObjectName(QStringLiteral("packageCopySessionsDiscard"));
	m_refresh = new PackageActionButton(tr("Refresh")); m_refresh->setObjectName(QStringLiteral("packageCopySessionsRefresh"));
	m_limits = new PackageActionButton(tr("Shared Storage Limits…")); m_limits->setObjectName(QStringLiteral("packageCopySharedLimits"));
	for (auto* button : {m_discard, m_limits, m_refresh}) { button->setAccessibleName(button->text()); button->setAutoDefault(false); layout->addWidget(button); }
	m_status = new QLabel; m_status->setObjectName(QStringLiteral("packageCopySessionsStatus")); m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(tr("Temporary copy operation status")); outer->addWidget(m_status);
	m_progress = new QProgressBar; m_progress->setObjectName(QStringLiteral("packageCopySessionsProgress"));
	m_progress->setAccessibleName(tr("Temporary copy operation progress")); outer->addWidget(m_progress); m_progress->hide();
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Close); outer->addWidget(buttons);
	m_cancel = buttons->button(QDialogButtonBox::Cancel); m_cancel->setObjectName(QStringLiteral("packageCopySessionsCancel"));
	m_cancel->setAccessibleName(tr("Cancel temporary copy operation")); m_cancel->setAutoDefault(false);
	auto* close = buttons->button(QDialogButtonBox::Close); close->setObjectName(QStringLiteral("packageCopySessionsClose"));
	close->setAccessibleName(tr("Close retained temporary copies")); close->setAutoDefault(false);
	connect(close, &QPushButton::clicked, this, &PackageCopySessionsDialog::reject);
	connect(m_cancel, &QPushButton::clicked, this, &PackageCopySessionsDialog::cancelOperation);
	connect(m_table, &QTableWidget::currentCellChanged, this, &PackageCopySessionsDialog::updateSelection);
	connect(m_refresh, &QPushButton::clicked, this, [this] { reload(); });
	connect(m_discard, &QPushButton::clicked, this, &PackageCopySessionsDialog::discardSelected);
	connect(m_limits, &QPushButton::clicked, this, &PackageCopySessionsDialog::editLimits);
	auto* timer = new QTimer(this); timer->setInterval(100); connect(timer, &QTimer::timeout, this, &PackageCopySessionsDialog::updateProgress); timer->start();
	resize(QSize(880, 740).boundedTo(screen()->availableGeometry().size() - QSize(40, 40)));
	updateSelection(); QTimer::singleShot(0, this, [this] { reload(); });
}
PackageCopySessionsDialog::~PackageCopySessionsDialog()
{
	m_work->cancel.store(true);
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool PackageCopySessionsDialog::busy() const { return m_thread != nullptr; }
void PackageCopySessionsDialog::cancelOperation()
{
	if (!busy()) { return; }
	m_work->cancel.store(true); m_cancel->setEnabled(false); m_status->setText(tr("Stopping the copy operation…"));
}
void PackageCopySessionsDialog::reject()
{
	if (busy()) { m_closeRequested = true; cancelOperation(); return; }
	QDialog::reject();
}
void PackageCopySessionsDialog::start(std::function<void(const PackageReadControl&)> operation, std::function<void()> complete)
{
	if (busy()) { return; }
	m_work = std::make_shared<Progress>();
	m_thread = QThread::create([operation = std::move(operation), work = m_work] {
		PackageReadControl control; control.isCancelled = [work] { return work->cancel.load(); };
		control.progress = [work](const QString&, qint64 done, qint64 total) { work->completed.store(done); work->total.store(total); };
		operation(control);
	});
	setProperty("operationRunning", true); m_progress->show(); updateSelection(); updateProgress();
	connect(m_thread, &QThread::finished, this, [this, complete = std::move(complete)] {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; setProperty("operationRunning", false); m_progress->hide();
		if (m_closeRequested) { QDialog::reject(); return; }
		complete(); updateSelection();
	});
	m_thread->start();
}
void PackageCopySessionsDialog::updateProgress()
{
	if (!busy()) { return; }
	const auto total = m_work->total.load(), done = m_work->completed.load();
	if (total > 0 && total <= std::numeric_limits<int>::max()) {
		m_progress->setRange(0, static_cast<int>(total)); m_progress->setValue(static_cast<int>(qBound<qint64>(0, done, total))); m_progress->setTextVisible(true);
	} else {
		m_progress->setRange(0, m_reducedMotion ? 1 : 0); m_progress->setValue(0); m_progress->setTextVisible(false);
	}
}
void PackageCopySessionsDialog::reload(const QString& notice)
{
	if (busy()) { return; }
	m_status->setText(tr("Reviewing retained temporary copies…"));
	auto inventory = std::make_shared<PackageCopyInventory>();
	start([inventory, directory = m_directory](const PackageReadControl& control) { *inventory = listPackageCopies(directory, control); },
		[this, inventory, notice] {
		m_inventory = *inventory; m_inventory.cancelled = m_inventory.cancelled || m_work->cancel.load(); m_table->setRowCount(0);
		m_usage->setText(m_inventory.complete()
			? tr("Sessions: %1 · Payload: %2 MiB · Files: %3").arg(isolated(locale().toString(m_inventory.sessions.size())),
				isolated(locale().toString(m_inventory.bytes / (1024.0 * 1024.0), 'f', 2)), isolated(locale().toString(m_inventory.files)))
			: tr("Storage usage is incomplete. Refresh to review remaining sessions."));
		const auto& quota = m_inventory.quota;
		m_sharedUsage->setText(quota.complete()
			? tr("Shared initial reservations: %1 of %2 MiB\nFiles: %3 of %4 · Entries: %5 of %6\nBatches: %7 of %8 · Pending or interrupted: %9")
				.arg(isolated(locale().toString(quota.reserved.bytes / (1024.0 * 1024.0), 'f', 2)), isolated(locale().toString(quota.limits.maximumBytes / (1024.0 * 1024.0), 'f', 2)),
					isolated(locale().toString(quota.reserved.files)), isolated(locale().toString(quota.limits.maximumFiles)),
					isolated(locale().toString(quota.reserved.entries)), isolated(locale().toString(quota.limits.maximumEntries)),
					isolated(locale().toString(quota.reserved.batches)), isolated(locale().toString(quota.limits.maximumBatches)), isolated(locale().toString(quota.reserved.pendingBatches)))
			: tr("Shared reservation review is incomplete. %1").arg(quota.error));
		for (const auto& info : m_inventory.sessions) {
			const int row = m_table->rowCount(); m_table->insertRow(row);
			const QString state = !info.reviewable() || !info.error.isEmpty() ? tr("Needs review")
				: info.discardAvailable ? tr("Unused") : tr("In use or unavailable");
			const QStringList fields{info.id.left(8), info.createdUtc.isValid() ? locale().toString(info.createdUtc.toLocalTime(), QLocale::ShortFormat) : tr("Unknown"),
				info.reviewable() ? locale().toString(info.bytes / (1024.0 * 1024.0), 'f', 2) : tr("Unknown"), state};
			for (int column = 0; column < fields.size(); ++column) {
				auto* item = new QTableWidgetItem(isolated(fields.at(column))); const QString spoken = column == 0 ? info.id : fields.at(column);
				item->setData(Qt::AccessibleTextRole, spoken); item->setToolTip(spoken); m_table->setItem(row, column, item);
			}
		}
		QStringList status; if (!notice.isEmpty()) { status << notice; }
		if (m_inventory.cancelled) { status << tr("Copy review cancelled. No files were removed."); }
		else if (!m_inventory.error.isEmpty()) { status << m_inventory.error; }
		else { status << (m_inventory.sessions.isEmpty() ? tr("No retained temporary copy sessions.") : tr("Live studio sessions protect their copies. Refresh after closing the owning window.")); }
		if (StudioSettings(StudioSettings::AccessMode::ReadOnly).storedSchemaIsNewer()) { status << tr("Discard is unavailable because the settings were written by a newer version of VibeStudio."); }
		m_status->setText(status.join(QLatin1Char('\n')));
		if (m_table->rowCount()) { m_table->setCurrentCell(0, 0); }
	});
}
void PackageCopySessionsDialog::updateSelection()
{
	const int row = m_table->currentRow(); const bool selected = row >= 0 && row < m_inventory.sessions.size();
	m_table->setEnabled(!busy()); m_refresh->setEnabled(!busy()); m_cancel->setEnabled(busy() && !m_work->cancel.load());
	m_limits->setEnabled(!busy() && m_inventory.quota.policyFingerprint.size() == 32
		&& !StudioSettings(StudioSettings::AccessMode::ReadOnly).storedSchemaIsNewer());
	m_discard->setEnabled(!busy() && selected && m_inventory.sessions.at(row).reviewable() && m_inventory.sessions.at(row).discardAvailable
		&& !StudioSettings(StudioSettings::AccessMode::ReadOnly).storedSchemaIsNewer());
	if (!selected) { m_details->setPlainText(isolated(QDir::toNativeSeparators(m_directory))); return; }
	const auto& info = m_inventory.sessions.at(row);
	QStringList details{tr("Session: %1").arg(isolated(info.id)), tr("Storage: %1").arg(isolated(QDir::toNativeSeparators(info.path))),
		tr("Payload (bytes): %1 · Files: %2 · Batches: %3").arg(isolated(locale().toString(info.bytes)), isolated(locale().toString(info.files)), isolated(locale().toString(info.batches))),
		tr("Storage review SHA-256: %1").arg(isolated(QString::fromLatin1(info.fingerprint.toHex())))};
	details << (info.reservationKnown
		? tr("Shared reserved payload (bytes): %1 · Files: %2 · Entries: %3\nBatches: %4 · Pending or interrupted: %5 · Failed cleanup: %6")
			.arg(isolated(locale().toString(info.reserved.bytes)), isolated(locale().toString(info.reserved.files)), isolated(locale().toString(info.reserved.entries)),
				isolated(locale().toString(info.reserved.batches)), isolated(locale().toString(info.reserved.pendingBatches)), isolated(locale().toString(info.reserved.cleanupFailedBatches)))
		: tr("No valid shared reservation record. Review this session before preparing more copies."));
	for (const auto& error : {info.error, info.storageError, info.leaseError}) { if (!error.isEmpty()) { details << error; } }
	m_details->setPlainText(details.join(QLatin1Char('\n')));
}
void PackageCopySessionsDialog::editLimits()
{
	if (busy() || m_inventory.quota.policyFingerprint.size() != 32 || StudioSettings(StudioSettings::AccessMode::ReadOnly).storedSchemaIsNewer()) { return; }
	const auto reviewed = m_inventory.quota;
	QDialog dialog(this); dialog.setObjectName(QStringLiteral("packageCopySharedLimitsDialog")); dialog.setWindowTitle(tr("Shared Temporary Copy Limits"));
	dialog.setAccessibleName(dialog.windowTitle()); dialog.setLayoutDirection(layoutDirection());
	auto* outer = new QVBoxLayout(&dialog); auto* scroll = new QScrollArea; scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Shared copy limit fields")); scroll->setFrameShape(QFrame::NoFrame);
	auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body); outer->addWidget(scroll, 1);
	auto* path = new QLineEdit(QDir::toNativeSeparators(reviewed.directory)); path->setReadOnly(true); path->setLayoutDirection(Qt::LeftToRight);
	path->setAccessibleName(tr("Shared temporary copy store")); path->setCursorPosition(0); layout->addWidget(path);
	const auto field = [&](const QString& label, const QString& name, int maximum, int value) {
		auto* spin = new QSpinBox; spin->setObjectName(name); spin->setRange(1, maximum); spin->setValue(value); spin->setGroupSeparatorShown(true); spin->setAccessibleName(label);
		auto* caption = new QLabel(label); caption->setWordWrap(true); caption->setBuddy(spin);
		caption->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum); layout->addWidget(caption); layout->addWidget(spin); return spin;
	};
	auto* bytes = field(tr("Shared payload limit (MiB)"), QStringLiteral("packageCopySharedMiB"), PackageCopyMaximumMiB, reviewed.limits.maximumBytes / (1024 * 1024));
	auto* files = field(tr("Shared file limit"), QStringLiteral("packageCopySharedFiles"), PackageCopyMaximumFiles, reviewed.limits.maximumFiles);
	auto* entries = field(tr("Shared entry limit, including implicit folders"), QStringLiteral("packageCopySharedEntries"), PackageCopyStorePayloadEntryLimit, reviewed.limits.maximumEntries);
	auto* batches = field(tr("Shared batch limit"), QStringLiteral("packageCopySharedBatches"), PackageCopyMaximumBatches, reviewed.limits.maximumBatches);
	for (auto* spin : {bytes, files, entries, batches}) {
		spin->setToolTip(tr("Applies to initial reservations across all studio windows using this store. Lowering a limit preserves existing copies. Consumer edits and filesystem overhead are not reserved."));
	}
	layout->addStretch();
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel); outer->addWidget(buttons);
	auto* apply = buttons->button(QDialogButtonBox::Apply); apply->setObjectName(QStringLiteral("packageCopySharedApply")); apply->setAccessibleName(tr("Apply shared copy limits"));
	auto* cancel = buttons->button(QDialogButtonBox::Cancel); cancel->setAccessibleName(tr("Cancel shared copy limit changes"));
	connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.resize(QSize(700, 620).boundedTo(screen()->availableGeometry().size() - QSize(40, 40)));
	if (dialog.exec() != QDialog::Accepted) { return; }
	if (StudioSettings(StudioSettings::AccessMode::ReadOnly).storedSchemaIsNewer()) { reload(tr("Shared limits could not be changed because the settings store is read-only.")); return; }
	const PackageCopyLimits limits{static_cast<quint64>(bytes->value()) * 1024 * 1024, files->value(), entries->value(), batches->value()};
	m_status->setText(tr("Applying shared temporary copy limits…"));
	auto error = std::make_shared<QString>(); auto saved = std::make_shared<bool>(false);
	start([directory = m_directory, reviewed, limits, error, saved](const PackageReadControl& control) {
		*saved = configurePackageCopyQuota(directory, limits, reviewed.policyFingerprint, false, error.get(), control);
	}, [this, error, saved] { reload(*saved ? tr("Shared limits saved. Existing copies are preserved.") : *error); });
}
void PackageCopySessionsDialog::discardSelected()
{
	const int row = m_table->currentRow(); if (busy() || row < 0 || row >= m_inventory.sessions.size()) { return; }
	const auto info = m_inventory.sessions.at(row);
	if (!info.reviewable() || !info.discardAvailable || StudioSettings(StudioSettings::AccessMode::ReadOnly).storedSchemaIsNewer()) { return; }
	QMessageBox review(QMessageBox::Question, tr("Discard Temporary Copies"),
		tr("Permanently discard session %1?").arg(isolated(info.id)), QMessageBox::Discard | QMessageBox::Cancel, this);
	review.setObjectName(QStringLiteral("packageCopySessionsConfirmation")); review.setAccessibleName(review.windowTitle());
	review.setTextFormat(Qt::PlainText); review.setLayoutDirection(layoutDirection()); review.setDefaultButton(QMessageBox::Cancel);
	review.setInformativeText(tr("Files: %1 · Payload: %2 MiB\nCopies can include edits made by a temporary consumer. Save anything you need before discarding them.")
		.arg(isolated(locale().toString(info.files)), isolated(locale().toString(info.bytes / (1024.0 * 1024.0), 'f', 2))));
	review.setDetailedText(tr("Storage: %1\nStorage review SHA-256: %2\nOwnership and file metadata are checked again. Cancellation may leave some files removed; refresh to review what remains.")
		.arg(isolated(QDir::toNativeSeparators(info.path)), isolated(QString::fromLatin1(info.fingerprint.toHex()))));
	if (review.exec() != QMessageBox::Discard) { return; }
	m_status->setText(tr("Discarding reviewed temporary copies…"));
	auto error = std::make_shared<QString>(); auto removed = std::make_shared<bool>(false);
	start([directory = m_directory, info, error, removed](const PackageReadControl& control) {
		*removed = discardPackageCopies(directory, info.id, info.fingerprint, false, error.get(), control);
	}, [this, error, removed] { reload(*removed ? tr("Temporary copy session discarded.") : *error); });
}
} // namespace vibestudio
