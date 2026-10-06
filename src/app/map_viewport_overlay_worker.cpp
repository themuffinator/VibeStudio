#include "app/map_viewport_overlay_worker.h"
#include <QThread>
#include <QTimer>
#include <new>

namespace vibestudio {
struct MapViewportOverlayWorker::Work {
	MapViewportOverlayRequest request;
	MapViewportOverlayResult result;
	std::atomic_bool cancelled = false;
};

MapViewportOverlayWorker::MapViewportOverlayWorker(QObject* parent) : QObject(parent) {}
MapViewportOverlayWorker::~MapViewportOverlayWorker()
{
	cancel();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool MapViewportOverlayWorker::busy() const { return m_thread || m_pending.has_value(); }
void MapViewportOverlayWorker::cancel()
{
	m_pending.reset();
	if (m_work) { m_work->cancelled = true; }
}
void MapViewportOverlayWorker::request(MapViewportOverlayRequest request)
{
	if (m_pending && sameMapViewportOverlayKey(m_pending->key,request.key)) { return; }
	if (!m_pending && m_work && !m_work->cancelled && sameMapViewportOverlayKey(m_work->request.key,request.key)) { return; }
	if (m_work) { m_work->cancelled = true; }
	m_pending = std::move(request);
	if (!m_thread && !m_startScheduled) {
		m_startScheduled = true;
		QTimer::singleShot(0,this,[this] { m_startScheduled = false; startNext(); });
	}
}
void MapViewportOverlayWorker::startNext()
{
	if (m_thread || !m_pending) { return; }
	auto work = std::make_shared<Work>(); work->request = std::move(*m_pending); m_pending.reset(); m_work = work;
	work->result.key = work->request.key;
	m_thread = QThread::create([work] {
		try {
			work->result.failed = !renderMapViewportOverlays(work->request,&work->result,&work->cancelled);
		} catch (const std::bad_alloc&) {
			work->result.failed = true;
		}
	});
	m_thread->setParent(this);
	connect(m_thread,&QThread::finished,this,[this,work] {
		m_thread->deleteLater(); m_thread = nullptr; m_work.reset();
		const auto callback = completed;
		startNext();
		if (!work->cancelled && callback) { callback(work->result); }
	});
	m_thread->start();
}
} // namespace vibestudio
