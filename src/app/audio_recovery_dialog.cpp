#include "app/audio_recovery_dialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio
{
AudioRecoveryDialog::AudioRecoveryDialog(QString directory, QWidget *parent)
    : QDialog(parent), m_directory(std::move(directory)), m_cancel(std::make_shared<std::atomic_bool>(false))
{
	setObjectName(QStringLiteral("audioRecoveryDialog"));
	setWindowTitle(tr("Audio Recoveries"));
	setAccessibleName(tr("Audio recovery manager"));
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowModality(Qt::WindowModal);
	if (parent) {
		setLayoutDirection(parent->layoutDirection());
	}
	auto *outer = new QVBoxLayout(this);
	auto *scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("audioRecoveryBody"));
	scroll->setAccessibleName(tr("Recovery list and details"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *body = new QWidget;
	auto *layout = new QVBoxLayout(body);
	layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	layout->setContentsMargins(0, 0, 0, 0);
	scroll->setWidget(body);
	outer->addWidget(scroll, 1);
	m_status = new QLabel;
	m_status->setObjectName(QStringLiteral("audioRecoveryInventoryStatus"));
	m_status->setAccessibleName(tr("Recovery inventory status"));
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	m_table = new QTableWidget(0, 6);
	m_table->setObjectName(QStringLiteral("audioRecoveryRecords"));
	m_table->setAccessibleName(tr("Local audio recovery copies"));
	m_table->setHorizontalHeaderLabels(
	    {tr("Document"), tr("Saved (local time)"), tr("Format"), tr("Size"), tr("Verification"), tr("Editor lease")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->setMinimumHeight(fontMetrics().height() * 8);
	layout->addWidget(m_table);
	m_details = new QTextEdit;
	m_details->setObjectName(QStringLiteral("audioRecoveryDetails"));
	m_details->setAccessibleName(tr("Selected recovery paths and verification details"));
	m_details->setReadOnly(true);
	m_details->setMinimumHeight(fontMetrics().height() * 5);
	layout->addWidget(m_details);
	// Stack actions so expanded translations remain reachable in short windows.
	m_restore = new QPushButton(tr("Restore as Draft"));
	m_restore->setObjectName(QStringLiteral("audioRecoveryRestore"));
	m_discard = new QPushButton(tr("Discard Selected Copy…"));
	m_discard->setObjectName(QStringLiteral("audioRecoveryDiscard"));
	m_refresh = new QPushButton(tr("Refresh"));
	m_refresh->setObjectName(QStringLiteral("audioRecoveryRefresh"));
	for (auto *button : {m_restore, m_discard, m_refresh}) {
		button->setAccessibleName(button->text());
		button->setAutoDefault(false);
		layout->addWidget(button);
	}
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	outer->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_table, &QTableWidget::itemSelectionChanged, this, &AudioRecoveryDialog::updateSelection);
	connect(m_table, &QTableWidget::currentCellChanged, this, &AudioRecoveryDialog::updateSelection);
	connect(m_refresh, &QPushButton::clicked, this, &AudioRecoveryDialog::reload);
	connect(m_discard, &QPushButton::clicked, this, &AudioRecoveryDialog::discardSelected);
	connect(m_restore, &QPushButton::clicked, this, [this]() {
		const int row = m_table->currentRow();
		if (busy() || row < 0 || row >= m_inventory.records.size() || !m_inventory.records[row].verified()) {
			return;
		}
		const auto record = m_inventory.records[row];
		accept();
		if (restore) {
			restore(record.path, record.sha256, record.kind);
		}
	});
	resize(QSize(900, 680).boundedTo(screen()->availableGeometry().size() - QSize(40, 40)));
	updateSelection();
	QTimer::singleShot(0, this, &AudioRecoveryDialog::reload);
}

AudioRecoveryDialog::~AudioRecoveryDialog()
{
	m_cancel->store(true);
	if (m_thread) {
		m_thread->wait();
		delete m_thread;
	}
}

void AudioRecoveryDialog::start(std::function<void()> operation, std::function<void()> complete)
{
	if (busy()) {
		return;
	}
	m_cancel->store(false);
	m_thread = QThread::create(std::move(operation));
	updateSelection();
	connect(m_thread, &QThread::finished, this, [this, complete = std::move(complete)]() {
		m_thread->deleteLater();
		m_thread = nullptr;
		complete();
		updateSelection();
	});
	m_thread->start();
}

void AudioRecoveryDialog::reload()
{
	if (busy()) {
		return;
	}
	m_status->setText(tr("Verifying local recovery copies…"));
	m_status->setAccessibleDescription(m_status->text());
	auto result = std::make_shared<AudioRecoveryInventory>();
	start([result, directory = m_directory,
	       cancel = m_cancel]() { *result = listAudioRecoveries(directory, {[cancel]() { return cancel->load(); }}); },
	      [this, result]() {
		      m_inventory = std::move(*result);
		      m_table->setRowCount(0);
		      for (const auto &record : m_inventory.records) {
			      const int row = m_table->rowCount();
			      m_table->insertRow(row);
			      const QStringList values{
			          record.sourceName.isEmpty() ? record.id : record.sourceName,
			          record.writtenUtc.isValid()
			              ? locale().toString(record.writtenUtc.toLocalTime(), QLocale::ShortFormat)
			              : tr("Unknown"),
			          record.verified()
			              ? (record.kind == AudioRecoveryKind::Session ? tr("Session · %1 Hz · %2 tracks · %3 clips")
			                                                                 .arg(record.sampleRate)
			                                                                 .arg(record.tracks)
			                                                                 .arg(record.clips)
			                                                           : tr("Waveform · %1 Hz · %2 ch · %3 frames")
			                                                                 .arg(record.sampleRate)
			                                                                 .arg(record.channels)
			                                                                 .arg(record.frames))
			              : tr("Unknown"),
			          locale().formattedDataSize(record.bytes),
			          record.verified() ? tr("Verified") : tr("Needs review"),
			          record.sessionFilePresent ? tr("Present") : tr("Absent")};
			      for (int column = 0; column < values.size(); ++column) {
				      const QString displayed =
				          column >= 1 && column <= 3 ? QChar(0x2068) + values[column] + QChar(0x2069) : values[column];
				      auto *item = new QTableWidgetItem(displayed);
				      item->setData(Qt::AccessibleTextRole, values[column]);
				      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
				      m_table->setItem(row, column, item);
			      }
		      }
		      QString status = tr("Local copies: %4 · %1 used. Limits: %2 copies / %3. Existing copies are never "
		                          "removed automatically.")
		                           .arg(locale().formattedDataSize(m_inventory.totalBytes))
		                           .arg(AudioRecoveryCountLimit)
		                           .arg(locale().formattedDataSize(AudioRecoveryStorageLimit))
		                           .arg(m_inventory.records.size());
		      if (m_inventory.truncated) {
			      status += QLatin1Char('\n') + tr("Scan limit reached. Additional files were not inspected.");
		      }
		      if (!m_inventory.error.isEmpty()) {
			      status += QLatin1Char('\n') + m_inventory.error;
		      }
		      m_status->setText(status);
		      m_status->setAccessibleDescription(status);
		      if (m_table->rowCount() > 0) {
			      m_table->setCurrentCell(0, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		      }
	      });
}

void AudioRecoveryDialog::updateSelection()
{
	const int row = m_table->currentRow();
	const bool selected = row >= 0 && row < m_inventory.records.size();
	m_refresh->setEnabled(!busy());
	m_restore->setEnabled(!busy() && selected && m_inventory.records[row].verified());
	m_discard->setEnabled(!busy() && selected && m_inventory.records[row].sha256.size() == 32);
	if (!selected) {
		m_details->clear();
		return;
	}
	const auto &record = m_inventory.records[row];
	m_details->setPlainText(
	    tr("Recovery: %1\nOriginal source (provenance only): %2\nSHA-256: %3\n%4\nSession files may belong to an open "
	       "or interrupted editor. Live editors prevent discard.")
	        .arg(record.path, record.sourcePath, QString::fromLatin1(record.sha256.toHex()),
	             record.error.isEmpty()
	                 ? tr("Checksum and samples verified. Restore creates an unsaved draft; the original copy remains.")
	                 : record.error));
}

void AudioRecoveryDialog::discardSelected()
{
	const int row = m_table->currentRow();
	if (busy() || row < 0 || row >= m_inventory.records.size()) {
		return;
	}
	const auto record = m_inventory.records[row];
	QMessageBox confirm(
	    QMessageBox::Warning, tr("Discard Audio Recovery"),
	    tr("Permanently discard this local recovery copy?\n%1\nThe recorded source will not be changed.")
	        .arg(record.path),
	    QMessageBox::Discard | QMessageBox::Cancel, this);
	confirm.setTextFormat(Qt::PlainText);
	confirm.setDefaultButton(QMessageBox::Cancel);
	if (confirm.exec() != QMessageBox::Discard) {
		return;
	}
	auto error = std::make_shared<QString>();
	m_status->setText(tr("Discarding reviewed recovery copy…"));
	m_status->setAccessibleDescription(m_status->text());
	start([directory = m_directory, record,
	       error]() { discardAudioRecovery(directory, record.id, record.sha256, false, error.get(), record.kind); },
	      [this, error]() {
		      if (!error->isEmpty()) {
			      m_status->setText(*error);
			      m_status->setAccessibleDescription(*error);
		      } else {
			      reload();
		      }
	      });
}
} // namespace vibestudio
