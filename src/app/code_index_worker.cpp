#include "app/code_index_worker.h"

#include <QThread>
#include <QTimer>

#include <atomic>

namespace vibestudio {

struct CodeIndexWorker::Work {
	std::atomic_bool cancel {false};
	std::atomic_int files {0};
	std::atomic_int symbols {0};
	CodeWorkspaceIndex result;
	quint64 serial = 0;
};

CodeIndexWorker::CodeIndexWorker(QObject* parent) : QObject(parent)
{
	setObjectName(QStringLiteral("codeIndexWorker"));
	m_progressTimer = new QTimer(this);
	m_progressTimer->setInterval(100);
	connect(m_progressTimer, &QTimer::timeout, this, [this]() {
		if (m_work && m_work->serial == m_serial && progress) { progress(m_work->files.load(), m_work->symbols.load()); }
	});
}

CodeIndexWorker::~CodeIndexWorker()
{
	reset();
	if (m_thread) {
		m_thread->disconnect(this);
		m_thread->wait();
		delete m_thread;
	}
}

void CodeIndexWorker::start(CodeWorkspaceIndexRequest request)
{
	reset();
	m_root = request.rootPath;
	m_pending = std::move(request);
	if (started) { started(); }
	startPending();
}

void CodeIndexWorker::cancel()
{
	if (m_pending) {
		// A queued request has not started reading; still publish its explicit
		// cancellation, rather than the old worker's superseded report.
		CodeWorkspaceIndex result;
		result.rootPath = m_root;
		result.complete = false;
		result.cancelled = true;
		result.state = OperationState::Cancelled;
		reset();
		if (completed) { completed(result); }
	} else if (m_work) { m_work->cancel = true; }
}

void CodeIndexWorker::reset()
{
	++m_serial;
	m_pending.reset();
	m_root.clear();
	if (m_work) { m_work->cancel = true; }
	m_progressTimer->stop();
}

void CodeIndexWorker::startPending()
{
	if (m_thread || !m_pending) { return; }
	auto work = std::make_shared<Work>();
	work->serial = m_serial;
	m_work = work;
	CodeWorkspaceIndexRequest request = std::move(*m_pending);
	m_pending.reset();
	request.isCancelled = [work]() { return work->cancel.load(); };
	request.progress = [work](int files, int symbols) { work->files = files; work->symbols = symbols; };
	m_thread = QThread::create([work, request = std::move(request)]() { work->result = indexCodeWorkspace(request); });
	connect(m_thread, &QThread::finished, this, [this, work]() {
		QThread* finishedThread = m_thread;
		m_thread = nullptr;
		finishedThread->wait();
		finishedThread->deleteLater();
		m_progressTimer->stop();
		m_work.reset();
		if (work->serial == m_serial) {
			if (work->cancel) {
				work->result.cancelled = true;
				work->result.complete = false;
				work->result.state = OperationState::Cancelled;
			}
			if (completed) { completed(work->result); }
		}
		startPending();
	});
	m_progressTimer->start();
	m_thread->start();
}

} // namespace vibestudio
