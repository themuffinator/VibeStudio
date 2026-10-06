#include "app/map_plan_render_worker.h"
#include <QThread>
#include <QTimer>
#include <new>

namespace vibestudio {
namespace {
bool sameScene(const MapPlanRenderRequest& a, const MapPlanRenderRequest& b)
{
	return a.sceneRevision == b.sceneRevision && a.projection == b.projection;
}
bool sameRequest(const MapPlanRenderRequest& a, const MapPlanRenderRequest& b)
{
	return sameScene(a,b) && a.selectionRevision == b.selectionRevision && sameMapPlanView(a.view,b.view);
}
}
struct MapPlanRenderWorker::Work {
	MapPlanRenderRequest request;
	MapPlanRenderResult result;
	std::atomic_bool cancelGeometry = false, cancelSelection = false, cancelRaster = false;
};

MapPlanRenderWorker::MapPlanRenderWorker(QObject* parent) : QObject(parent) {}
MapPlanRenderWorker::~MapPlanRenderWorker()
{
	cancel();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}
bool MapPlanRenderWorker::busy() const { return m_thread || m_pending.has_value(); }
void MapPlanRenderWorker::cancel()
{
	m_pending.reset();
	if (m_work) { m_work->cancelGeometry = true; m_work->cancelSelection = true; m_work->cancelRaster = true; }
}
void MapPlanRenderWorker::request(MapPlanRenderRequest request)
{
	if (m_pending && sameRequest(*m_pending,request)) { return; }
	if (!m_pending && m_work && !m_work->cancelRaster && sameRequest(m_work->request,request)) { return; }
	if (m_work) {
		m_work->cancelRaster = true;
		if (!sameScene(m_work->request,request)) { m_work->cancelGeometry = true; m_work->cancelSelection = true; }
		else if (m_work->request.selectionRevision != request.selectionRevision) { m_work->cancelSelection = true; }
	}
	m_pending = std::move(request);
	QTimer::singleShot(0, this, [this] { startNext(); });
}
void MapPlanRenderWorker::startNext()
{
	if (m_thread || !m_pending) { return; }
	auto work = std::make_shared<Work>(); work->request = std::move(*m_pending); m_pending.reset(); m_work = work;
	const auto& request = work->request;
	auto& result = work->result;
	result.sceneRevision = request.sceneRevision; result.selectionRevision = request.selectionRevision; result.projection = request.projection;
	result.view = request.view; result.baseFrame = request.baseFrame; result.selectionFrame = request.selectionFrame;
	result.wires = request.wires; result.selectionWires = request.selectionWires;
	result.wiresComputed = request.wiresComputed; result.selectionWiresComputed = request.selectionWiresComputed;
	m_thread = QThread::create([work] {
		const auto& request = work->request;
		auto& result = work->result;
		try {
			if (!result.wiresComputed && !work->cancelGeometry) {
				result.wires = buildMapPlanWires(request.document, request.brushes, request.index, request.projection,
					request.worldspawnId, request.limits, &work->cancelGeometry);
				result.wiresComputed = !work->cancelGeometry;
			}
			if (!result.selectionWiresComputed && !work->cancelSelection) {
				result.selectionWires = buildMapPlanSelectionWires(request.document, request.brushes, request.index, request.projection,
					request.selection, request.limits, &work->cancelSelection);
				result.selectionWiresComputed = !work->cancelSelection;
			}
			if (work->cancelRaster) { return; }
			const bool base = renderMapPlanFrame(request, false, result.wires, &result.baseFrame, &work->cancelRaster);
			const bool selected = request.selection.isEmpty()
				|| renderMapPlanFrame(request, true, result.selectionWires, &result.selectionFrame, &work->cancelRaster);
			result.failed = !work->cancelRaster && (!base || !selected);
		} catch (const std::bad_alloc&) {
			result.failed = !work->cancelRaster;
		}
	});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread->deleteLater(); m_thread = nullptr; m_work.reset();
		const auto& result = work->result;
		if (m_pending && m_pending->sceneRevision == result.sceneRevision && m_pending->projection == result.projection) {
			if (result.wiresComputed) { m_pending->wires = result.wires; m_pending->wiresComputed = true; }
			if (!result.baseFrame.image.isNull()) { m_pending->baseFrame = result.baseFrame; }
			if (m_pending->selectionRevision == result.selectionRevision) {
				if (result.selectionWiresComputed) { m_pending->selectionWires = result.selectionWires; m_pending->selectionWiresComputed = true; }
				if (!result.selectionFrame.image.isNull()) { m_pending->selectionFrame = result.selectionFrame; }
			}
		}
		const auto callback = completed;
		startNext();
		if (callback) { callback(result); }
	});
	m_thread->start();
}
} // namespace vibestudio
