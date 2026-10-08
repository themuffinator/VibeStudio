#include "app/material_tasks.h"

#include <QThread>

namespace vibestudio {

MaterialTaskLane::MaterialTaskLane(QObject* parent)
	: QObject(parent)
{
}

MaterialTaskLane::~MaterialTaskLane()
{
	m_hasQueued = false;
	m_queued = {};
	if (m_stop) {
		m_stop->store(true);
	}
	if (m_thread) {
		disconnect(m_thread, nullptr, this, nullptr);
		m_thread->wait();
		delete m_thread;
		m_thread = nullptr;
	}
}

void MaterialTaskLane::submit(Task task, bool supersede)
{
	m_queued = std::move(task);
	m_hasQueued = true;
	if (supersede && m_stop) {
		m_stop->store(true);
	}
	startNext();
}

void MaterialTaskLane::cancel()
{
	m_hasQueued = false;
	m_queued = {};
	if (m_stop) {
		m_stop->store(true);
	}
}

bool MaterialTaskLane::busy() const
{
	return m_thread != nullptr;
}

bool MaterialTaskLane::pending() const
{
	return m_thread != nullptr || m_hasQueued;
}

void MaterialTaskLane::startNext()
{
	if (m_thread || !m_hasQueued) {
		return;
	}
	Task task = std::move(m_queued);
	m_queued = {};
	m_hasQueued = false;
	auto stop = std::make_shared<std::atomic_bool>(false);
	m_stop = stop;
	auto publish = std::make_shared<std::function<void()>>();
	m_thread = QThread::create([task = std::move(task), stop, publish]() {
		*publish = task([stop]() { return stop->load(std::memory_order_relaxed); });
	});
	m_thread->setObjectName(QStringLiteral("MaterialTask"));
	connect(m_thread, &QThread::finished, this, [this, stop, publish]() {
		QThread* finished = m_thread;
		m_thread = nullptr;
		if (finished) {
			finished->deleteLater();
		}
		if (!stop->load() && *publish) {
			(*publish)();
		}
		startNext();
	});
	m_thread->start(QThread::LowPriority);
}

} // namespace vibestudio
