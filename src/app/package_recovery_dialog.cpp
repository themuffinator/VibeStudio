#include "app/package_recovery_dialog.h"
#include "app/package_action_button.h"
#include "app/package_import_dialog.h"
#include "app/package_draft_storage_dialog.h"
#include "app/package_publication_dialog.h"
#include "core/studio_settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QStyleOptionButton>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio {
PackageRecoveryDialog::PackageRecoveryDialog(QString directory, QWidget* parent, QStringList publicationDirectories)
	: QDialog(parent), m_directory(std::move(directory)), m_cancel(std::make_shared<std::atomic_bool>(false))
{
	setObjectName(QStringLiteral("packageRecoveryDialog"));
	setWindowTitle(tr("Package Recoveries")); setAccessibleName(tr("Package recovery manager"));
	setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal);
	if (parent) { setLayoutDirection(parent->layoutDirection()); }
	auto* outer = new QVBoxLayout(this);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
	scroll->setAccessibleName(tr("Package recovery list and settings"));
	auto* body = new QWidget; auto* layout = new QVBoxLayout(body);
	layout->setSizeConstraint(QLayout::SetMinAndMaxSize); scroll->setWidget(body); outer->addWidget(scroll, 1);
	StudioSettings settings;
	auto* enabled = new QCheckBox(tr("Save automatic package recovery copies"));
	enabled->setObjectName(QStringLiteral("packageRecoveryEnabled")); enabled->setChecked(settings.packageRecoveryEnabled());
	enabled->setAccessibleName(enabled->text());
	enabled->setToolTip(tr("Checkpoint unsaved package content and edit history locally. Turning this off keeps existing recovery copies."));
	layout->addWidget(enabled);
	auto* intervalLabel = new QLabel(tr("Checkpoint interval (seconds)")); intervalLabel->setWordWrap(true);
	auto* interval = new QSpinBox;
	interval->setObjectName(QStringLiteral("packageRecoveryInterval")); interval->setRange(5, 600); interval->setValue(settings.packageRecoveryIntervalSeconds());
	interval->setAccessibleName(intervalLabel->text()); intervalLabel->setBuddy(interval); interval->setEnabled(enabled->isChecked());
	layout->addWidget(intervalLabel); layout->addWidget(interval);
	connect(enabled, &QCheckBox::toggled, this, [this, interval](bool checked) {
		StudioSettings().setPackageRecoveryEnabled(checked); interval->setEnabled(checked); if (preferencesChanged) { preferencesChanged(); }
	});
	connect(interval, &QSpinBox::valueChanged, this, [this](int seconds) {
		StudioSettings().setPackageRecoveryIntervalSeconds(seconds); if (preferencesChanged) { preferencesChanged(); }
	});
	auto* limitLabel = new QLabel(tr("Recovery storage limit (MiB)")); limitLabel->setWordWrap(true);
	auto* limit = new QSpinBox; limit->setObjectName(QStringLiteral("packageRecoveryMaximumMiB"));
	limit->setRange(128, 131072); limit->setValue(settings.packageRecoveryMaximumMiB()); limit->setGroupSeparatorShown(true);
	limit->setAccessibleName(limitLabel->text()); limitLabel->setBuddy(limit); layout->addWidget(limitLabel); layout->addWidget(limit);
	limit->setToolTip(tr("Bounds logical file sizes, including temporary writes. Older recovery copies are kept when the limit is reached."));
	auto* copiesLabel = new QLabel(tr("Maximum recovery copies")); copiesLabel->setWordWrap(true);
	auto* copies = new QSpinBox; copies->setObjectName(QStringLiteral("packageRecoveryMaximumCopies"));
	copies->setRange(1, 128); copies->setValue(settings.packageRecoveryMaximumCopies());
	copies->setAccessibleName(copiesLabel->text()); copiesLabel->setBuddy(copies); layout->addWidget(copiesLabel); layout->addWidget(copies);
	connect(limit, &QSpinBox::valueChanged, this, [this](int mib) {
		StudioSettings().setPackageRecoveryMaximumMiB(mib); updateUsage(); if (preferencesChanged) { preferencesChanged(); }
	});
	connect(copies, &QSpinBox::valueChanged, this, [this](int count) {
		StudioSettings().setPackageRecoveryMaximumCopies(count); updateUsage(); if (preferencesChanged) { preferencesChanged(); }
	});
	auto* working = new PackageActionButton(tr("Working Import Storage…")); working->setObjectName(QStringLiteral("packageWorkingImports"));
	working->setAccessibleName(working->text()); working->setAutoDefault(false);
	working->setToolTip(tr("Review temporary package imports, storage limits and abandoned sessions.")); layout->addWidget(working);
	connect(working, &QPushButton::clicked, this, [this]() {
		if (auto* existing = findChild<QDialog*>(QStringLiteral("packageImportDialog"))) { existing->show(); existing->raise(); return; }
		auto* dialog = new PackageImportDialog(packageImportDirectory(), this); dialog->show();
	});
	auto* drafts = new PackageActionButton(tr("Saved Draft Storage…")); drafts->setObjectName(QStringLiteral("packageSavedDraftStorage"));
	drafts->setAccessibleName(drafts->text()); drafts->setAutoDefault(false); layout->addWidget(drafts);
	connect(drafts, &QPushButton::clicked, this, [this]() {
		if (auto* existing = findChild<QDialog*>(QStringLiteral("packageDraftStorageDialog"))) { existing->show(); existing->raise(); return; }
		auto* dialog = new PackageDraftStorageDialog({}, this); dialog->show();
	});
	auto* interrupted = new PackageActionButton(tr("Interrupted Saves…")); interrupted->setObjectName(QStringLiteral("packageInterruptedSaves"));
	interrupted->setAccessibleName(interrupted->text()); interrupted->setAutoDefault(false); layout->addWidget(interrupted);
	connect(interrupted, &QPushButton::clicked, this, [this, directories = std::move(publicationDirectories)]() {
		if (auto* existing = findChild<QDialog*>(QStringLiteral("packagePublicationDialog"))) { existing->show(); existing->raise(); return; }
		QStringList roots = StudioSettings().packagePublicationDirectories(); roots += directories; roots.removeDuplicates();
		auto* dialog = new PackagePublicationDialog(roots, this);
		if (openPublicationOutput) { dialog->openOutput = [this](const QString& path) {
			const auto handler = openPublicationOutput; accept(); handler(path);
		}; }
		dialog->recoveryFinished = [this](const PackageRecoveryReport& report) { if (publicationFinished) { publicationFinished(report); } };
		dialog->show();
	});
	m_usage = new QLabel; m_usage->setObjectName(QStringLiteral("packageRecoveryUsage")); m_usage->setTextFormat(Qt::PlainText);
	m_usage->setWordWrap(true); m_usage->setAccessibleName(tr("Recovery storage usage")); layout->addWidget(m_usage);
	m_status = new QLabel; m_status->setObjectName(QStringLiteral("packageRecoveryStatus"));
	m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); m_status->setAccessibleName(tr("Recovery inventory status")); layout->addWidget(m_status);
	m_progress = new QProgressBar; m_progress->setAccessibleName(tr("Recovery scan progress")); m_progress->setRange(0, 0); layout->addWidget(m_progress);
	m_table = new QTableWidget(0, 4); m_table->setObjectName(QStringLiteral("packageRecoveryRecords"));
	m_table->setAccessibleName(tr("Local package recovery copies"));
	m_table->setHorizontalHeaderLabels({tr("Package"), tr("Saved (local time)"), tr("Format"), tr("Status")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setWordWrap(false);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->setMinimumHeight(fontMetrics().height() * 7); layout->addWidget(m_table);
	m_details = new QPlainTextEdit; m_details->setObjectName(QStringLiteral("packageRecoveryDetails"));
	m_details->setReadOnly(true); m_details->setAccessibleName(tr("Selected recovery paths and history"));
	m_details->setMinimumHeight(fontMetrics().height() * 6); layout->addWidget(m_details);
	m_restore = new PackageActionButton(tr("Restore to New Draft…")); m_restore->setObjectName(QStringLiteral("packageRecoveryRestore"));
	m_discard = new PackageActionButton(tr("Discard Selected Copy…")); m_discard->setObjectName(QStringLiteral("packageRecoveryDiscard"));
	m_refresh = new PackageActionButton(tr("Refresh")); m_refresh->setObjectName(QStringLiteral("packageRecoveryRefresh"));
	for (auto* button : {m_restore, m_discard, m_refresh}) { button->setAccessibleName(button->text()); button->setAutoDefault(false); layout->addWidget(button); }
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &PackageRecoveryDialog::reject);
	connect(m_table, &QTableWidget::currentCellChanged, this, &PackageRecoveryDialog::updateSelection);
	connect(m_refresh, &QPushButton::clicked, this, &PackageRecoveryDialog::reload);
	connect(m_discard, &QPushButton::clicked, this, &PackageRecoveryDialog::discardSelected);
	connect(m_restore, &QPushButton::clicked, this, [this]() {
		const int row = m_table->currentRow();
		if (busy() || row < 0 || row >= m_inventory.records.size() || !m_inventory.records.at(row).readable()) { return; }
		const auto info = m_inventory.records.at(row); const auto handler = restore;
		accept(); if (handler) { handler(info); }
	});
	resize(QSize(880, 740).boundedTo(screen()->availableGeometry().size() - QSize(40, 40)));
	updateSelection(); QTimer::singleShot(0, this, &PackageRecoveryDialog::reload);
}

PackageRecoveryDialog::~PackageRecoveryDialog()
{
	m_cancel->store(true);
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}

bool PackageRecoveryDialog::busy() const { return m_thread != nullptr; }

void PackageRecoveryDialog::reject()
{
	if (busy()) { m_closeRequested = true; m_cancel->store(true); m_status->setText(tr("Stopping the recovery operation…")); return; }
	QDialog::reject();
}

void PackageRecoveryDialog::start(std::function<void()> operation, std::function<void()> complete)
{
	if (busy()) { return; }
	m_cancel->store(false); m_thread = QThread::create(std::move(operation));
	setProperty("operationRunning", true); m_progress->show(); updateSelection();
	connect(m_thread, &QThread::finished, this, [this, complete = std::move(complete)]() {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr;
		setProperty("operationRunning", false); m_progress->hide();
		if (m_closeRequested) { QDialog::reject(); return; }
		complete(); updateSelection();
	});
	m_thread->start();
}

void PackageRecoveryDialog::reload()
{
	if (busy()) { return; }
	m_status->setText(tr("Reading local package recovery details…"));
	auto inventory = std::make_shared<PackageRecoveryInventory>();
	start([inventory, directory = m_directory, cancel = m_cancel]() {
		*inventory = listPackageRecoveries(directory, {[cancel]() { return cancel->load(); }, {}, {}, {}});
	}, [this, inventory]() {
		m_inventory = *inventory; updateUsage(); m_table->setRowCount(0);
		for (const auto& info : m_inventory.records) {
			const int row = m_table->rowCount(); m_table->insertRow(row);
			const QStringList fields{info.title.isEmpty() ? info.id : info.title,
				info.writtenUtc.isValid() ? locale().toString(info.writtenUtc.toLocalTime(), QLocale::ShortFormat) : tr("Unknown"),
				packageArchiveFormatId(info.format).toUpper(), info.readable()
					? info.unavailableBaseCount ? tr("Unavailable original content") : tr("Metadata checked")
					: !info.manifestPresent && info.storageSha256.size() == 32 ? tr("Incomplete copy") : tr("Needs review")};
			for (int column = 0; column < fields.size(); ++column) {
				const QString displayed = column == 1 || column == 2 ? QChar(0x2068) + fields.at(column) + QChar(0x2069) : fields.at(column);
				auto* item = new QTableWidgetItem(displayed); item->setData(Qt::AccessibleTextRole, fields.at(column));
				item->setToolTip(fields.at(column)); m_table->setItem(row, column, item);
			}
		}
		m_status->setText(!m_inventory.error.isEmpty() ? m_inventory.error : m_inventory.cancelled ? tr("Recovery scan cancelled.")
			: m_inventory.truncated ? tr("The scan reached its copy, file or metadata limit. Review and discard old copies to show more.")
			: m_inventory.records.isEmpty() ? tr("No package recovery copies found.") : tr("Choose a copy. Restoring verifies stored content and preserves edit history in a new draft."));
		if (m_table->rowCount()) { m_table->setCurrentCell(0, 0); }
	});
}

void PackageRecoveryDialog::updateUsage()
{
	const StudioSettings settings;
	m_usage->setText(m_inventory.storageComplete
		? tr("Recovery storage: %1 MiB of %2 MiB · %3 of %4 copies. Older copies are kept when a limit is reached.")
			.arg(locale().toString(m_inventory.storageBytes / (1024.0 * 1024.0), 'f', 2)).arg(locale().toString(settings.packageRecoveryMaximumMiB()))
			.arg(m_inventory.records.size()).arg(settings.packageRecoveryMaximumCopies())
		: tr("Recovery storage usage is incomplete. Review the listed storage errors before saving another checkpoint."));
}

void PackageRecoveryDialog::updateSelection()
{
	const int row = m_table->currentRow(); const bool selected = row >= 0 && row < m_inventory.records.size();
	m_table->setEnabled(!busy()); m_refresh->setEnabled(!busy());
	m_restore->setEnabled(!busy() && selected && m_inventory.records.at(row).readable());
	m_discard->setEnabled(!busy() && selected && m_inventory.records.at(row).storageSha256.size() == 32);
	if (!selected) { m_details->setPlainText(QDir::toNativeSeparators(m_directory)); return; }
	const auto& info = m_inventory.records.at(row);
	QStringList details{tr("Recovery: %1").arg(QDir::toNativeSeparators(info.path)),
		tr("Original source: %1").arg(info.sourcePath.isEmpty() ? tr("Untitled package") : QDir::toNativeSeparators(info.sourcePath)),
		tr("Original draft: %1").arg(info.originalDraftPath.isEmpty() ? tr("Not saved as a draft") : QDir::toNativeSeparators(info.originalDraftPath)),
		tr("Revision %1 · %2 staged operations · %3 history steps").arg(info.revision).arg(info.operationCount).arg(info.historyCount),
		tr("Manifest SHA-256: %1").arg(QString::fromLatin1(info.manifestSha256.toHex())),
		tr("Stored: %1 bytes in %2 files · temporary files: %3 bytes").arg(locale().toString(info.storageBytes)).arg(info.storageFiles).arg(locale().toString(info.temporaryBytes)),
		tr("Storage review SHA-256: %1").arg(QString::fromLatin1(info.storageSha256.toHex()))};
	if (info.unavailableBaseCount) { details << tr("Unavailable original entries: %1. Repaired content is preserved; Undo may expose missing bytes.").arg(info.unavailableBaseCount); }
	if (info.sessionFilePresent) { details << tr("A session lock is present. A running editor must release the copy before it can be discarded."); }
	if (!info.error.isEmpty()) { details << info.error; }
	if (!info.storageError.isEmpty()) { details << info.storageError; }
	m_details->setPlainText(details.join(QLatin1Char('\n')));
}

void PackageRecoveryDialog::discardSelected()
{
	const int row = m_table->currentRow();
	if (busy() || row < 0 || row >= m_inventory.records.size()) { return; }
	const auto info = m_inventory.records.at(row);
	if (QMessageBox::question(this, tr("Discard Package Recovery"), tr("Permanently discard the selected recovery copy?"),
		QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) { return; }
	m_status->setText(tr("Discarding the selected recovery copy…"));
	auto error = std::make_shared<QString>(); auto removed = std::make_shared<bool>(false);
	start([directory = m_directory, info, error, removed]() {
		*removed = discardPackageRecoveryStorage(directory, info.id, info.storageSha256, false, error.get());
	}, [this, error, removed]() { if (*removed) { reload(); } else { m_status->setText(*error); } });
}

} // namespace vibestudio
