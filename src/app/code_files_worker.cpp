#include "app/code_files_worker.h"

#include <QThread>
#include <QTimer>
#include <atomic>

namespace vibestudio {

struct CodeFilesWorker::Work {
	std::atomic_bool cancel {false};
	std::atomic_int files {0};
	std::atomic_int entries {0};
	CodeFilesResult result;
	quint64 serial = 0;
};

CodeFilesWorker::CodeFilesWorker(QObject* parent) : QObject(parent)
{
	setObjectName(QStringLiteral("codeFilesWorker"));
	m_timer = new QTimer(this);
	m_timer->setInterval(100);
	connect(m_timer, &QTimer::timeout, this, [this]() {
		if (m_work && m_work->serial == m_serial && progress) { progress(m_work->files, m_work->entries); }
	});
}

CodeFilesWorker::~CodeFilesWorker()
{
	reset();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}

void CodeFilesWorker::start(CodeFilesRequest request)
{
	reset();
	m_pending = std::move(request);
	startPending();
}

void CodeFilesWorker::reset()
{
	++m_serial;
	m_pending.reset();
	if (m_work) { m_work->cancel = true; }
	m_timer->stop();
}

void CodeFilesWorker::cancel()
{
	if (m_pending) {
		CodeFilesResult result;
		result.rootPath = m_pending->rootPath;
		result.complete = false;
		result.cancelled = true;
		result.state = OperationState::Cancelled;
		reset();
		if (completed) { completed(result); }
	} else if (m_work) { m_work->cancel = true; }
}

void CodeFilesWorker::startPending()
{
	if (m_thread || !m_pending) { return; }
	auto work = std::make_shared<Work>();
	work->serial = m_serial;
	m_work = work;
	CodeFilesRequest request = std::move(*m_pending);
	m_pending.reset();
	request.isCancelled = [work]() { return work->cancel.load(); };
	request.progress = [work](int files, int entries) { work->files = files; work->entries = entries; };
	m_thread = QThread::create([work, request = std::move(request)]() { work->result = listCodeFiles(request); });
	connect(m_thread, &QThread::finished, this, [this, work]() {
		QThread* finished = m_thread;
		m_thread = nullptr;
		finished->wait();
		finished->deleteLater();
		m_timer->stop();
		m_work.reset();
		if (work->serial == m_serial) {
			if (work->cancel) { work->result.complete = false; work->result.cancelled = true; work->result.state = OperationState::Cancelled; }
			if (completed) { completed(work->result); }
		}
		startPending();
	});
	m_timer->start();
	m_thread->start();
}

} // namespace vibestudio
