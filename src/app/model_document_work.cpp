#include "app/model_document_work.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <memory>

namespace vibestudio
{
namespace
{
struct Work
{
	QString error;
	std::atomic_bool cancelled = false;
	std::atomic<ModelWorkPhase> phase = ModelWorkPhase::Validating;
	std::atomic<qint64> completed = 0, total = 0;
	bool success = false;
};
QString phaseText(ModelWorkPhase phase)
{
	switch (phase)
	{
	case ModelWorkPhase::Reading:
		return QCoreApplication::translate("VibeStudioModelEditor", "Reading model…");
	case ModelWorkPhase::Validating:
		return QCoreApplication::translate("VibeStudioModelEditor", "Validating mesh…");
	case ModelWorkPhase::Editing:
		return QCoreApplication::translate("VibeStudioModelEditor", "Editing mesh…");
	case ModelWorkPhase::Serializing:
		return QCoreApplication::translate("VibeStudioModelEditor", "Preparing model data…");
	case ModelWorkPhase::Writing:
		return QCoreApplication::translate("VibeStudioModelEditor", "Writing model…");
	case ModelWorkPhase::Committing:
		return QCoreApplication::translate("VibeStudioModelEditor", "Committing output…");
	}
	return {};
}
class ProgressDialog final : public QDialog
{
  public:
	ProgressDialog(QWidget *parent, const QString &title, std::shared_ptr<Work> work) : QDialog(parent), m_work(std::move(work))
	{
		setObjectName(QStringLiteral("meshOperationDialog"));
		setWindowTitle(title);
		setAccessibleName(title);
		setWindowModality(Qt::WindowModal);
		resize(std::max(440, fontMetrics().averageCharWidth() * 50), fontMetrics().lineSpacing() * 6);
		auto *layout = new QVBoxLayout(this);
		m_status = new QLabel;
		m_status->setObjectName(QStringLiteral("meshOperationStatus"));
		m_status->setTextFormat(Qt::PlainText);
		m_status->setWordWrap(true);
		m_status->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Model operation status"));
		layout->addWidget(m_status);
		m_progress = new QProgressBar;
		m_progress->setObjectName(QStringLiteral("meshOperationProgress"));
		m_progress->setAccessibleName(QCoreApplication::translate("VibeStudioModelEditor", "Model operation progress"));
		layout->addWidget(m_progress);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
		m_cancel = buttons->button(QDialogButtonBox::Cancel);
		m_cancel->setObjectName(QStringLiteral("cancelMeshOperation"));
		connect(buttons, &QDialogButtonBox::rejected, this, [this] { reject(); });
		layout->addWidget(buttons);
		refresh();
	}
	void refresh()
	{
		const bool cancelled = m_work->cancelled.load(std::memory_order_relaxed);
		const auto status = cancelled ? QCoreApplication::translate("VibeStudioModelEditor", "Cancelling model operation…")
									  : phaseText(m_work->phase.load(std::memory_order_relaxed));
		m_status->setText(status);
		m_status->setAccessibleName(status);
		m_progress->setAccessibleDescription(status);
		setAccessibleDescription(status);
		if (cancelled)
		{
			m_cancel->setEnabled(false);
			return;
		}
		const auto total = m_work->total.load(std::memory_order_relaxed);
		const auto done = m_work->completed.load(std::memory_order_relaxed);
		m_progress->setRange(0, total > 0 ? 1000 : 0);
		if (total > 0)
		{
			m_progress->setValue(int(std::clamp(double(done) / double(total), 0.0, 1.0) * 1000));
		}
	}

  protected:
	void reject() override
	{
		m_work->cancelled.store(true, std::memory_order_relaxed);
		refresh();
	}
	void closeEvent(QCloseEvent *event) override
	{
		reject();
		event->ignore();
	}

  private:
	std::shared_ptr<Work> m_work;
	QLabel *m_status = nullptr;
	QProgressBar *m_progress = nullptr;
	QPushButton *m_cancel = nullptr;
};
} // namespace

bool runModelTask(QWidget *parent, const QString &title, ModelTask task, QString *error, bool durableWrite,
				  std::function<void()> *cancelAction)
{
	if (error)
	{
		error->clear();
	}
	if (!task)
	{
		return false;
	}
	auto work = std::make_shared<Work>();
	if (cancelAction)
	{
		*cancelAction = [work] { work->cancelled.store(true, std::memory_order_relaxed); };
	}
	ProgressDialog progress(parent, title, work);
	QEventLoop loop;
	QTimer delay, updates;
	delay.setSingleShot(true);
	QObject::connect(&delay, &QTimer::timeout, &progress, &QDialog::show);
	QObject::connect(&updates, &QTimer::timeout, &progress, [&progress] { progress.refresh(); });
	auto *thread = QThread::create(
		[work, task = std::move(task)]
		{
			ModelWorkControl control;
			control.cancelled = [work] { return work->cancelled.load(std::memory_order_relaxed); };
			control.progress = [work](ModelWorkPhase phase, qint64 completed, qint64 total)
			{
				work->phase.store(phase, std::memory_order_relaxed);
				work->completed.store(completed, std::memory_order_relaxed);
				work->total.store(total, std::memory_order_relaxed);
			};
			try
			{
				work->success = task(&work->error, control);
			}
			catch (const std::bad_alloc &)
			{
				work->error = QCoreApplication::translate(
					"VibeStudioModelEditor", "Not enough memory to complete the model operation. The current document is unchanged.");
			}
		});
	QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
	delay.start(150);
	updates.start(75);
	thread->start();
	loop.exec();
	delay.stop();
	updates.stop();
	if (thread->isRunning())
	{
		work->cancelled.store(true, std::memory_order_relaxed);
	}
	thread->wait();
	delete thread;
	progress.hide();
	if (cancelAction)
	{
		*cancelAction = {};
	}
	if (work->cancelled.load(std::memory_order_relaxed) && !(durableWrite && work->success))
	{
		work->success = false;
		work->error = QCoreApplication::translate("VibeStudioModelDocument", "Model operation cancelled.");
	}
	if (error)
	{
		*error = work->error;
	}
	if (!work->success)
	{
		return false;
	}
	return true;
}
bool runModelDocumentWork(QWidget *parent, const QString &title, ModelDocument *document, ModelDocumentJob job, QString *error,
						  bool durableWrite, std::function<void()> *cancelAction)
{
	if (!document || !job)
	{
		if (error)
		{
			error->clear();
		}
		return false;
	}
	auto candidate = *document;
	if (!runModelTask(
			parent, title, [&](QString *failure, const ModelWorkControl &control) { return job(candidate, failure, control); }, error,
			durableWrite, cancelAction))
	{
		return false;
	}
	*document = std::move(candidate);
	return true;
}
} // namespace vibestudio
