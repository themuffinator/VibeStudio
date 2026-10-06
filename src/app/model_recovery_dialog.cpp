#include "app/model_recovery_dialog.h"

#include "app/studio_runtime.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace vibestudio
{
namespace
{
class RecoveryDialog final : public QDialog
{
  public:
	RecoveryDialog(QWidget *parent, QString directory) : QDialog(parent), m_directory(std::move(directory))
	{
		setObjectName(QStringLiteral("modelRecoveryDialog"));
		setWindowTitle(QCoreApplication::translate("VibeStudioModelRecovery", "Recover Mesh Documents"));
		setAccessibleName(windowTitle());
		resize(std::max(720, fontMetrics().averageCharWidth() * 70), std::max(480, fontMetrics().lineSpacing() * 24));
		auto *layout = new QVBoxLayout(this);
		auto *location = new QLabel(QDir::toNativeSeparators(m_directory));
		location->setTextFormat(Qt::PlainText);
		location->setWordWrap(true);
		location->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
		location->setAccessibleName(QCoreApplication::translate("VibeStudioModelRecovery", "Local recovery folder"));
		layout->addWidget(location);
		m_list = new QListWidget;
		m_list->setObjectName(QStringLiteral("modelRecoveryList"));
		m_list->setAccessibleName(QCoreApplication::translate("VibeStudioModelRecovery", "Mesh recovery copies"));
		m_list->setWordWrap(true);
		layout->addWidget(m_list, 1);
		m_progress = new QProgressBar;
		m_progress->setRange(0, 0);
		m_progress->setAccessibleName(QCoreApplication::translate("VibeStudioModelRecovery", "Recovery progress"));
		layout->addWidget(m_progress);
		m_details = new QPlainTextEdit;
		m_details->setObjectName(QStringLiteral("modelRecoveryDetails"));
		m_details->setReadOnly(true);
		m_details->setMinimumHeight(fontMetrics().lineSpacing() * 4);
		m_details->setMaximumHeight(fontMetrics().lineSpacing() * 8);
		m_details->setAccessibleName(QCoreApplication::translate("VibeStudioModelRecovery", "Recovery details"));
		layout->addWidget(m_details);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
		m_restore = buttons->button(QDialogButtonBox::Open);
		m_restore->setText(QCoreApplication::translate("VibeStudioModelRecovery", "Restore as Draft"));
		m_restore->setObjectName(QStringLiteral("restoreModelRecovery"));
		m_discard =
			buttons->addButton(QCoreApplication::translate("VibeStudioModelRecovery", "Discard Copy"), QDialogButtonBox::ActionRole);
		m_discard->setObjectName(QStringLiteral("discardModelRecovery"));
		layout->addWidget(buttons);
		connect(buttons, &QDialogButtonBox::rejected, this, [this] { reject(); });
		connect(m_restore, &QPushButton::clicked, this, [this] { restore(); });
		connect(m_discard, &QPushButton::clicked, this, [this] { discard(); });
		connect(m_list, &QListWidget::currentRowChanged, this, [this] { showSelection(); });
		auto result = std::make_shared<ModelRecoveryScan>();
		start(
			QCoreApplication::translate("VibeStudioModelRecovery", "Reading local recovery copies…"),
			[result, directory = m_directory] { *result = listModelRecoveries(directory, interrupted); },
			[this, result]
			{
				m_records = result->records;
				for (int i = 0; i < m_records.size(); ++i)
				{
					const auto &record = m_records.at(i);
					const auto title = record.title.isEmpty() ? QFileInfo(record.path).fileName() : record.title;
					auto *row = new QListWidgetItem(
						QStringLiteral("%1\n%2").arg(title, record.isValid() ? record.writtenUtc.toLocalTime().toString(Qt::ISODate)
																			 : record.error),
						m_list);
					row->setData(Qt::UserRole, i);
					row->setData(Qt::AccessibleTextRole, row->text());
					row->setToolTip(record.path);
				}
				m_details->setPlainText(
					!result->error.isEmpty() ? result->error
					: result->limited		 ? QCoreApplication::translate(
											   "VibeStudioModelRecovery",
											   "The scan reached its limit of 128 copies. Review the recovery folder for additional records.")
					: m_records.isEmpty()
						? QCoreApplication::translate("VibeStudioModelRecovery", "No local mesh recovery copies were found.")
						: QCoreApplication::translate("VibeStudioModelRecovery", "Select a copy to review before restoring."));
			});
	}
	~RecoveryDialog() override
	{
		if (m_thread)
		{
			m_thread->requestInterruption();
			m_thread->wait();
			delete m_thread;
		}
	}
	std::optional<RecoveredModelDraft> recovered;

  protected:
	void reject() override
	{
		if (m_thread)
		{
			m_closing = true;
			m_thread->requestInterruption();
			m_details->setPlainText(QCoreApplication::translate("VibeStudioModelRecovery", "Cancelling recovery…"));
			return;
		}
		QDialog::reject();
	}
	void closeEvent(QCloseEvent *event) override
	{
		if (m_thread)
		{
			reject();
			event->ignore();
		}
		else
		{
			QDialog::closeEvent(event);
		}
	}

  private:
	QString m_directory;
	QVector<ModelRecoveryRecord> m_records;
	QListWidget *m_list = nullptr;
	QPlainTextEdit *m_details = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_restore = nullptr, *m_discard = nullptr;
	QThread *m_thread = nullptr;
	bool m_closing = false;
	static bool interrupted() { return QThread::currentThread()->isInterruptionRequested(); }
	QString discardId(const ModelRecoveryRecord &record) const
	{
		// A damaged header must never redirect deletion to another copy.
		const auto id = QFileInfo(record.path).completeBaseName();
		const auto path = modelRecoveryPath(m_directory, id);
		return !path.isEmpty() && path == QFileInfo(record.path).absoluteFilePath() ? id : QString();
	}
	std::optional<ModelRecoveryRecord> selected() const
	{
		const auto *row = m_list->currentItem();
		if (!row)
		{
			return {};
		}
		return m_records.at(row->data(Qt::UserRole).toInt());
	}
	void start(const QString &status, std::function<void()> work, std::function<void()> done)
	{
		m_details->setPlainText(status);
		m_progress->show();
		m_list->setEnabled(false);
		m_restore->setEnabled(false);
		m_discard->setEnabled(false);
		auto error = std::make_shared<QString>();
		m_thread = QThread::create(
			[work = std::move(work), error]
			{
				try
				{
					work();
				}
				catch (const std::bad_alloc &)
				{
					*error = QCoreApplication::translate("VibeStudioModelRecovery", "Not enough memory to read the recovery copy.");
				}
			});
		m_thread->setParent(this);
		connect(m_thread, &QThread::finished, this,
				[this, done = std::move(done), error]
				{
					m_thread->deleteLater();
					m_thread = nullptr;
					m_progress->hide();
					m_list->setEnabled(true);
					if (m_closing)
					{
						QDialog::reject();
						return;
					}
					showSelection();
					if (error->isEmpty())
					{
						done();
					}
					else
					{
						m_details->setPlainText(*error);
					}
				});
		m_thread->start();
	}
	void showSelection()
	{
		if (m_thread)
		{
			return;
		}
		const auto record = selected();
		const bool live = record && record->ownerProcessId > 0 && studioProcessIsRunning(record->ownerProcessId);
		m_restore->setEnabled(record && record->isValid());
		m_discard->setEnabled(record && !discardId(*record).isEmpty() && !live);
		if (!record)
		{
			return;
		}
		m_details->setPlainText(
			record->isValid()
				? QCoreApplication::translate(
					  "VibeStudioModelRecovery",
					  "%1 bytes · %2\nRestoring opens an unsaved draft. The original source and recovery copy remain unchanged.%3")
					  .arg(record->payloadBytes)
					  .arg(record->sourcePath.isEmpty() ? QCoreApplication::translate("VibeStudioModelRecovery", "Unsaved source")
														: QDir::toNativeSeparators(record->sourcePath),
						   live ? QCoreApplication::translate("VibeStudioModelRecovery",
															  "\nThe owning studio is still running; discarding this copy is disabled.")
								: QString())
				: record->error);
	}
	void restore()
	{
		const auto record = selected();
		if (m_thread || !record || !record->isValid())
		{
			return;
		}
		auto result = std::make_shared<RecoveredModelDraft>();
		auto error = std::make_shared<QString>();
		start(
			QCoreApplication::translate("VibeStudioModelRecovery", "Verifying and restoring the mesh…"),
			[record, result, error]
			{
				ModelRecoverySnapshot snapshot;
				result->record = inspectModelRecovery(record->path, &snapshot, interrupted);
				if (!result->record.isValid())
				{
					*error = result->record.error;
					return;
				}
				result->document.restoreDraft(snapshot.mesh, snapshot.selection, error.get(),
											  {[] { return QThread::currentThread()->isInterruptionRequested(); }, {}});
			},
			[this, result, error]
			{
				if (!error->isEmpty())
				{
					m_details->setPlainText(*error);
					return;
				}
				recovered = std::move(*result);
				accept();
			});
	}
	void discard()
	{
		const auto record = selected();
		if (m_thread || !record || (record->ownerProcessId > 0 && studioProcessIsRunning(record->ownerProcessId)))
		{
			return;
		}
		if (QMessageBox::question(this, QCoreApplication::translate("VibeStudioModelRecovery", "Discard Recovery Copy"),
								  QCoreApplication::translate("VibeStudioModelRecovery",
															  "Permanently discard this recovery copy? The original source is unchanged."),
								  QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard)
		{
			return;
		}
		QString error;
		if (!removeModelRecovery(m_directory, discardId(*record), &error))
		{
			m_details->setPlainText(error);
			return;
		}
		delete m_list->takeItem(m_list->currentRow());
		m_details->setPlainText(QCoreApplication::translate("VibeStudioModelRecovery", "Recovery copy discarded."));
	}
};
} // namespace

std::optional<RecoveredModelDraft> chooseModelRecovery(QWidget *parent, const QString &directory)
{
	RecoveryDialog dialog(parent, directory);
	if (dialog.exec() != QDialog::Accepted)
	{
		return {};
	}
	return std::move(dialog.recovered);
}
} // namespace vibestudio
