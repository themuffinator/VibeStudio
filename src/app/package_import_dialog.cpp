#include "app/package_import_dialog.h"
#include "app/package_action_button.h"
#include "core/studio_settings.h"

#include <QDialogButtonBox>
#include <QComboBox>
#include <QDir>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio {

PackageImportDialog::PackageImportDialog(QString directory, QWidget* parent)
	: QDialog(parent), m_directory(std::move(directory)), m_cancel(std::make_shared<std::atomic_bool>(false))
{
	setObjectName(QStringLiteral("packageImportDialog")); setWindowTitle(tr("Package Working Imports"));
	setAccessibleName(tr("Package working import storage")); setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal);
	if (parent) { setLayoutDirection(parent->layoutDirection()); }
	auto* outer = new QVBoxLayout(this); auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setAccessibleName(tr("Working import limits and sessions"));
	auto* body = new QWidget; auto* layout = new QVBoxLayout(body); layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	scroll->setWidget(body); outer->addWidget(scroll, 1);
	const StudioSettings settings;
	auto* bytesLabel = new QLabel(tr("Working import limit (MiB)")); bytesLabel->setWordWrap(true);
	auto* bytes = new QSpinBox; bytes->setObjectName(QStringLiteral("packageImportMaximumMiB"));
	bytes->setRange(128, 131072); bytes->setValue(settings.packageImportMaximumMiB()); bytes->setGroupSeparatorShown(true);
	bytes->setAccessibleName(bytesLabel->text()); bytesLabel->setBuddy(bytes);
	bytes->setToolTip(tr("Reserves payload bytes before copying. Live documents and readers keep their working files when the limit is lowered."));
	layout->addWidget(bytesLabel); layout->addWidget(bytes);
	auto* filesLabel = new QLabel(tr("Maximum working files")); filesLabel->setWordWrap(true);
	auto* files = new QSpinBox; files->setObjectName(QStringLiteral("packageImportMaximumFiles"));
	files->setRange(1, PackageImportFileLimit); files->setValue(settings.packageImportMaximumFiles()); files->setGroupSeparatorShown(true);
	files->setAccessibleName(filesLabel->text()); filesLabel->setBuddy(files); layout->addWidget(filesLabel); layout->addWidget(files);
	connect(bytes, &QSpinBox::valueChanged, this, [this](int value) { StudioSettings().setPackageImportMaximumMiB(value); updateUsage(); });
	connect(files, &QSpinBox::valueChanged, this, [this](int value) { StudioSettings().setPackageImportMaximumFiles(value); updateUsage(); });
	m_usage = new QLabel; m_usage->setObjectName(QStringLiteral("packageImportUsage")); m_usage->setTextFormat(Qt::PlainText);
	m_usage->setWordWrap(true); m_usage->setAccessibleName(tr("Working import storage usage")); layout->addWidget(m_usage);
	m_status = new QLabel; m_status->setObjectName(QStringLiteral("packageImportStatus")); m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true); m_status->setAccessibleName(tr("Working import operation status")); layout->addWidget(m_status);
	m_progress = new QProgressBar; m_progress->setRange(0, 0); m_progress->setAccessibleName(tr("Working import scan progress")); layout->addWidget(m_progress);
	m_table = new QTableWidget(0, 4); m_table->setObjectName(QStringLiteral("packageImportSessions")); m_table->setAccessibleName(tr("Working import sessions"));
	m_table->setHorizontalHeaderLabels({tr("Session"), tr("Created (local time)"), tr("Reserved MiB"), tr("Status")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setWordWrap(false);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->setMinimumHeight(fontMetrics().height() * 7); layout->addWidget(m_table);
	m_details = new QPlainTextEdit; m_details->setObjectName(QStringLiteral("packageImportDetails")); m_details->setReadOnly(true);
	m_details->setAccessibleName(tr("Selected working import paths and storage checksum")); m_details->setMinimumHeight(fontMetrics().height() * 6); layout->addWidget(m_details);
	m_discard = new PackageActionButton(tr("Discard Selected Session…")); m_discard->setObjectName(QStringLiteral("packageImportDiscard"));
	m_refresh = new PackageActionButton(tr("Refresh")); m_refresh->setObjectName(QStringLiteral("packageImportRefresh"));
	m_unlock = new PackageActionButton(tr("Review Lock Files…")); m_unlock->setObjectName(QStringLiteral("packageImportUnlock"));
	m_unlock->setToolTip(tr("Review an interrupted operation's lock. Release checks that the file is unchanged and no process still holds it."));
	for (auto* button : {m_discard, m_unlock, m_refresh}) { button->setAccessibleName(button->text()); button->setAutoDefault(false); layout->addWidget(button); }
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); buttons->button(QDialogButtonBox::Close)->setAccessibleName(tr("Close working import storage")); outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &PackageImportDialog::reject);
	connect(m_table, &QTableWidget::currentCellChanged, this, &PackageImportDialog::updateSelection);
	connect(m_refresh, &QPushButton::clicked, this, [this]() { reload(); });
	connect(m_discard, &QPushButton::clicked, this, &PackageImportDialog::discardSelected);
	connect(m_unlock, &QPushButton::clicked, this, &PackageImportDialog::releaseLock);
	resize(QSize(880, 740).boundedTo(screen()->availableGeometry().size() - QSize(40, 40)));
	updateSelection(); QTimer::singleShot(0, this, [this]() { reload(); });
}

PackageImportDialog::~PackageImportDialog()
{
	m_cancel->store(true);
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool PackageImportDialog::busy() const { return m_thread != nullptr; }
void PackageImportDialog::reject()
{
	if (busy()) { m_closeRequested = true; m_cancel->store(true); m_status->setText(tr("Stopping the working import operation…")); return; }
	QDialog::reject();
}
void PackageImportDialog::start(std::function<void()> operation, std::function<void()> complete)
{
	if (busy()) { return; }
	m_cancel->store(false); m_thread = QThread::create(std::move(operation)); setProperty("operationRunning", true);
	m_progress->show(); updateSelection();
	connect(m_thread, &QThread::finished, this, [this, complete = std::move(complete)]() {
		m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; setProperty("operationRunning", false); m_progress->hide();
		if (m_closeRequested) { QDialog::reject(); return; }
		complete(); updateSelection();
	});
	m_thread->start();
}
void PackageImportDialog::reload(const QString& notice)
{
	if (busy()) { return; }
	m_status->setText(tr("Reviewing working import storage…"));
	auto inventory = std::make_shared<PackageImportInventory>();
	start([inventory, directory = m_directory, cancel = m_cancel]() {
		*inventory = listPackageImports(directory, {[cancel]() { return cancel->load(); }, {}, {}, {}});
	}, [this, inventory, notice]() {
		m_inventory = *inventory; updateUsage(); m_table->setRowCount(0);
		for (const auto& info : m_inventory.sessions) {
			const int row = m_table->rowCount(); m_table->insertRow(row);
			const QStringList fields{info.id.left(8),
				info.createdUtc.isValid() ? locale().toString(info.createdUtc.toLocalTime(), QLocale::ShortFormat) : tr("Unknown"),
				info.error.isEmpty() ? locale().toString(info.reservedBytes / (1024.0 * 1024.0), 'f', 2) : tr("Unknown"),
				!info.storageError.isEmpty() || !info.error.isEmpty() ? tr("Needs review") : info.leasePresent ? tr("May be in use") : tr("Ready for review")};
			for (int column = 0; column < fields.size(); ++column) {
				auto* item = new QTableWidgetItem(QChar(0x2068) + fields.at(column) + QChar(0x2069));
				const QString spoken = column == 0 ? info.id : fields.at(column);
				item->setData(Qt::AccessibleTextRole, spoken); item->setToolTip(spoken); m_table->setItem(row, column, item);
			}
		}
		QStringList status; if (!notice.isEmpty()) { status << notice; }
		if (!m_inventory.error.isEmpty()) { status << m_inventory.error; }
		else { status << (m_inventory.sessions.isEmpty() ? tr("No retained package working files.") : tr("Live documents and readers protect their working sessions from discard.")); }
		if (!m_inventory.locks.isEmpty()) { status << tr("Lock files are present. If an operation remains blocked after its owner exits, choose Review Lock Files."); }
		m_status->setText(status.join(QLatin1Char('\n')));
		if (m_table->rowCount()) { m_table->setCurrentCell(0, 0); }
	});
}
void PackageImportDialog::updateUsage()
{
	const StudioSettings settings;
	m_usage->setText(m_inventory.complete()
		? tr("Reserved: %1 of %2 MiB · %3 of %4 files. Payloads currently on disk: %5 MiB.")
			.arg(locale().toString(m_inventory.reservedBytes / (1024.0 * 1024.0), 'f', 2)).arg(locale().toString(settings.packageImportMaximumMiB()))
			.arg(locale().toString(m_inventory.reservedFiles)).arg(locale().toString(settings.packageImportMaximumFiles()))
			.arg(locale().toString(m_inventory.bytes / (1024.0 * 1024.0), 'f', 2))
		: tr("Storage usage is incomplete. Review the listed session errors."));
}
void PackageImportDialog::updateSelection()
{
	const int row = m_table->currentRow(); const bool selected = row >= 0 && row < m_inventory.sessions.size();
	m_table->setEnabled(!busy()); m_refresh->setEnabled(!busy()); m_discard->setEnabled(!busy() && selected && m_inventory.sessions.at(row).reviewable());
	m_unlock->setEnabled(!busy() && !m_inventory.locks.isEmpty());
	if (!selected) { m_details->setPlainText(QDir::toNativeSeparators(m_directory)); return; }
	const auto& info = m_inventory.sessions.at(row);
	QStringList details{tr("Session: %1").arg(info.id), tr("Storage: %1").arg(QDir::toNativeSeparators(info.path)),
		tr("Payloads: %1 bytes in %2 files").arg(locale().toString(info.bytes)).arg(locale().toString(info.files)),
		tr("Reserved: %1 bytes in %2 files").arg(locale().toString(info.reservedBytes)).arg(locale().toString(info.reservedFiles)),
		tr("Storage review SHA-256: %1").arg(QString::fromLatin1(info.fingerprint.toHex()))};
	if (info.leasePresent) { details << tr("A session lease is present. Discard verifies that no document or reader is still using it."); }
	if (!info.error.isEmpty()) { details << info.error; } if (!info.storageError.isEmpty()) { details << info.storageError; }
	m_details->setPlainText(details.join(QLatin1Char('\n')));
}
void PackageImportDialog::discardSelected()
{
	const int row = m_table->currentRow(); if (busy() || row < 0 || row >= m_inventory.sessions.size()) { return; }
	const auto info = m_inventory.sessions.at(row);
	if (!info.reviewable() || QMessageBox::question(this, tr("Discard Working Imports"),
		tr("Permanently discard the reviewed temporary files for session %1 (%2 MiB)?").arg(info.id, locale().toString(info.bytes / (1024.0 * 1024.0), 'f', 2)),
		QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) { return; }
	m_status->setText(tr("Discarding the reviewed working files…"));
	auto error = std::make_shared<QString>(); auto removed = std::make_shared<bool>(false);
	start([directory = m_directory, info, error, removed, cancel = m_cancel]() {
		*removed = discardPackageImports(directory, info.id, info.fingerprint, false, error.get(), {[cancel]() { return cancel->load(); }, {}, {}, {}});
	}, [this, error, removed]() { reload(*removed ? tr("Working import session discarded.") : *error); });
}

void PackageImportDialog::releaseLock()
{
	if (busy() || m_inventory.locks.isEmpty()) { return; }
	QStringList choices;
	for (const auto& lock : m_inventory.locks) { choices << (QChar(0x2066) + lock.relativePath + QChar(0x2069)); }
	QInputDialog picker(this); picker.setObjectName(QStringLiteral("packageImportLockPicker")); picker.setLayoutDirection(layoutDirection());
	picker.setWindowTitle(tr("Review Working Import Locks")); picker.setLabelText(tr("Lock file"));
	picker.setComboBoxItems(choices); picker.setComboBoxEditable(false); picker.setAccessibleName(picker.windowTitle());
	// Technical paths retain their punctuation/order inside an RTL dialog.
	if (auto* field = picker.findChild<QComboBox*>()) {
		field->setLayoutDirection(Qt::LeftToRight); field->setAccessibleName(tr("Lock file"));
		for (int row = 0; row < m_inventory.locks.size(); ++row) { field->setItemData(row, m_inventory.locks.at(row).relativePath, Qt::AccessibleTextRole); }
	}
	if (picker.exec() != QDialog::Accepted) { return; }
	const int index = choices.indexOf(picker.textValue()); if (index < 0) { return; }
	const auto lock = m_inventory.locks.at(index);
	if (!lock.reviewable()) { m_status->setText(lock.error); return; }
	QMessageBox review(QMessageBox::Question, tr("Release Working Import Lock"),
		tr("Release the reviewed lock %1? The file must be unchanged and no process may still hold it.").arg(QChar(0x2066) + lock.relativePath + QChar(0x2069)),
		QMessageBox::Yes | QMessageBox::Cancel, this);
	review.setObjectName(QStringLiteral("packageImportLockConfirmation")); review.setTextFormat(Qt::PlainText); review.setLayoutDirection(layoutDirection());
	review.setDefaultButton(QMessageBox::Cancel); review.setAccessibleName(review.windowTitle());
	review.setDetailedText(tr("Storage: %1\nLock review SHA-256: %2\nReleasing a lock keeps all working payloads. Refresh and review any session separately before discarding it.")
		.arg(QChar(0x2066) + QDir::toNativeSeparators(m_directory) + QChar(0x2069),
			QChar(0x2066) + QString::fromLatin1(lock.fingerprint.toHex()) + QChar(0x2069)));
	if (review.exec() != QMessageBox::Yes) { return; }
	m_status->setText(tr("Checking and releasing the reviewed lock…"));
	auto error = std::make_shared<QString>(); auto released = std::make_shared<bool>(false);
	start([directory = m_directory, lock, error, released, cancel = m_cancel]() {
		*released = releasePackageImportLock(directory, lock.relativePath, lock.fingerprint, false, error.get(), {[cancel]() { return cancel->load(); }, {}, {}, {}});
	}, [this, error, released]() { reload(*released ? tr("Reviewed working import lock released.") : *error); });
}

} // namespace vibestudio
