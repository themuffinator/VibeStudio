#include "app/model_assembly_recovery_dialog.h"
#include "app/model_document_work.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
class RecoveryDialog final : public QDialog
{
  public:
	RecoveryDialog(QWidget *parent, QString directory) : QDialog(parent), m_directory(std::move(directory))
	{
		setObjectName(QStringLiteral("assemblyRecoveryDialog"));
		setWindowTitle(QCoreApplication::translate("ModelAssemblyRecovery", "Assembly Recoveries"));
		setAccessibleName(windowTitle());
		resize(720, 560);
		auto *layout = new QVBoxLayout(this);
		m_status = new QLabel;
		m_status->setTextFormat(Qt::PlainText);
		m_status->setWordWrap(true);
		m_status->setAccessibleName(QCoreApplication::translate("ModelAssemblyRecovery", "Recovery status"));
		layout->addWidget(m_status);
		m_list = new QListWidget;
		m_list->setWordWrap(true);
		m_list->setResizeMode(QListView::Adjust);
		m_list->setTextElideMode(Qt::ElideNone);
		m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		m_list->setObjectName(QStringLiteral("assemblyRecoveryList"));
		m_list->setAccessibleName(QCoreApplication::translate("ModelAssemblyRecovery", "Assembly recovery copies"));
		layout->addWidget(m_list, 2);
		m_details = new QPlainTextEdit;
		m_details->setObjectName(QStringLiteral("assemblyRecoveryDetails"));
		m_details->setReadOnly(true);
		m_details->setAccessibleName(QCoreApplication::translate("ModelAssemblyRecovery", "Recovery details"));
		layout->addWidget(m_details, 1);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
		m_restore =
			buttons->addButton(QCoreApplication::translate("ModelAssemblyRecovery", "Restore as Draft"), QDialogButtonBox::ActionRole);
		m_restore->setObjectName(QStringLiteral("assemblyRecoveryRestore"));
		m_discard = buttons->addButton(QCoreApplication::translate("ModelAssemblyRecovery", "Discard Copy…"), QDialogButtonBox::ActionRole);
		m_discard->setObjectName(QStringLiteral("assemblyRecoveryDiscard"));
		m_refresh = buttons->addButton(QCoreApplication::translate("ModelAssemblyRecovery", "Refresh"), QDialogButtonBox::ActionRole);
		m_refresh->setObjectName(QStringLiteral("assemblyRecoveryRefresh"));
		layout->addWidget(buttons);
		connect(buttons, &QDialogButtonBox::rejected, this, [this] { reject(); });
		connect(m_list, &QListWidget::currentRowChanged, this, [this] { selection(); });
		connect(m_refresh, &QPushButton::clicked, this, [this] { refresh(); });
		connect(m_restore, &QPushButton::clicked, this, [this] { restore(); });
		connect(m_discard, &QPushButton::clicked, this, [this] { discard(); });
		selection();
		QTimer::singleShot(0, this, [this] { refresh(); });
	}
	std::optional<RecoveredAssemblyDraft> result;

  protected:
	void reject() override
	{
		if (m_working)
		{
			m_closing = true;
			if (m_cancel)
			{
				m_cancel();
			}
		}
		else
		{
			QDialog::reject();
		}
	}
	void closeEvent(QCloseEvent *event) override
	{
		if (m_working)
		{
			reject();
			event->ignore();
		}
		else
		{
			event->accept();
		}
	}

  private:
	QString m_directory;
	ModelAssemblyRecoveryScan m_scan;
	QListWidget *m_list = nullptr;
	QPlainTextEdit *m_details = nullptr;
	QLabel *m_status = nullptr;
	QPushButton *m_restore = nullptr, *m_discard = nullptr, *m_refresh = nullptr;
	bool m_working = false, m_closing = false;
	std::function<void()> m_cancel;
	const ModelAssemblyRecoveryRecord *selected() const
	{
		const auto row = m_list->currentRow();
		return row >= 0 && row < m_scan.records.size() ? &m_scan.records[row] : nullptr;
	}
	bool work(const QString &title, ModelTask task, bool durable = false)
	{
		if (m_working)
		{
			return false;
		}
		m_working = true;
		m_list->setEnabled(false);
		m_refresh->setEnabled(false);
		m_restore->setEnabled(false);
		m_discard->setEnabled(false);
		m_status->setText(title);
		QString error;
		const auto success = runModelTask(this, title, std::move(task), &error, durable, &m_cancel);
		m_working = false;
		m_list->setEnabled(true);
		m_refresh->setEnabled(true);
		selection();
		if (m_closing)
		{
			QDialog::reject();
			return false;
		}
		if (!success)
		{
			m_status->setText(error);
		}
		return success;
	}
	void selection()
	{
		const auto *record = selected();
		m_restore->setEnabled(record && record->isValid() && !m_working);
		m_discard->setEnabled(record && record->sha256.size() == 32 && !m_working);
		if (!record)
		{
			m_details->clear();
			return;
		}
		m_details->setPlainText(
			QCoreApplication::translate("ModelAssemblyRecovery", "Source: %1\nCopy: %2\nSHA-256: %3\nParts: %4 · Time: %5 s\n%6\n%7")
				.arg(record->sourcePath, record->path, QString::fromLatin1(record->sha256.toHex()))
				.arg(record->parts)
				.arg(record->seconds)
				.arg(record->sessionFilePresent
						 ? QCoreApplication::translate("ModelAssemblyRecovery", "A session marker exists. Active editors prevent discard.")
						 : QString(),
					 record->isValid() ? QCoreApplication::translate("ModelAssemblyRecovery",
																	 "Restoration creates an unsaved draft. Original inputs and this copy "
																	 "are preserved. Package parts use the current package context.")
									   : record->error));
	}
	void refresh()
	{
		const auto id = selected() ? selected()->id : QString();
		ModelAssemblyRecoveryScan candidate;
		if (!work(QCoreApplication::translate("ModelAssemblyRecovery", "Checking assembly recovery copies…"),
				  [&](QString *error, const ModelWorkControl &control)
				  {
					  candidate = listModelAssemblyRecoveries(m_directory, control);
					  *error = candidate.error;
					  return error->isEmpty();
				  }))
		{
			return;
		}
		m_scan = std::move(candidate);
		m_list->clear();
		int selectionRow = -1;
		for (int i = 0; i < m_scan.records.size(); ++i)
		{
			const auto &record = m_scan.records[i];
			auto *item = new QListWidgetItem(record.isValid()
												 ? QCoreApplication::translate("ModelAssemblyRecovery", "%1 — %2 — %3 parts")
													   .arg(record.title, record.writtenUtc.toLocalTime().toString(Qt::ISODate))
													   .arg(record.parts)
												 : QCoreApplication::translate("ModelAssemblyRecovery", "Unavailable — %1").arg(record.id),
											 m_list);
			item->setToolTip(record.isValid() ? record.sourcePath : record.error);
			if (record.id == id)
			{
				selectionRow = i;
			}
		}
		m_list->setCurrentRow(selectionRow >= 0 ? selectionRow : (m_scan.records.isEmpty() ? -1 : 0));
		m_status->setText(
			m_scan.limited
				? QCoreApplication::translate("ModelAssemblyRecovery", "Recovery scan limit reached. Some copies could not be verified.")
				: QCoreApplication::translate("ModelAssemblyRecovery",
											  "%1 recovery copies. Automatic storage is limited to 32 copies and 32 MiB.")
					  .arg(m_scan.records.size()));
		selection();
	}
	void restore()
	{
		if (!selected() || !selected()->isValid())
		{
			return;
		}
		RecoveredAssemblyDraft candidate;
		candidate.record = *selected();
		if (!work(QCoreApplication::translate("ModelAssemblyRecovery", "Restoring assembly draft…"),
				  [&](QString *error, const ModelWorkControl &control)
				  {
					  return restoreModelAssemblyRecovery(candidate.record.path, candidate.record.sha256, &candidate.document,
														  &candidate.snapshot, error, control);
				  }))
		{
			return;
		}
		result = std::move(candidate);
		accept();
	}
	void discard()
	{
		if (!selected() || selected()->sha256.size() != 32)
		{
			return;
		}
		const auto record = *selected();
		if (QMessageBox::question(
				this, QCoreApplication::translate("ModelAssemblyRecovery", "Discard Recovery Copy"),
				QCoreApplication::translate("ModelAssemblyRecovery",
											"Discard this local recovery copy?\n%1\nOriginal source files remain unchanged.")
					.arg(record.path),
				QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard)
		{
			return;
		}
		if (work(
				QCoreApplication::translate("ModelAssemblyRecovery", "Discarding assembly recovery…"),
				[&](QString *error, const ModelWorkControl &control)
				{ return discardModelAssemblyRecovery(m_directory, record.id, record.sha256, false, error, control); }, true))
		{
			refresh();
		}
	}
};
} // namespace
std::optional<RecoveredAssemblyDraft> chooseModelAssemblyRecovery(QWidget *parent, const QString &directory)
{
	RecoveryDialog dialog(parent, directory);
	return dialog.exec() == QDialog::Accepted ? std::move(dialog.result) : std::nullopt;
}
} // namespace vibestudio
