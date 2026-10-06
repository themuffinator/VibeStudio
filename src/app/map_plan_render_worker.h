#pragma once

#include "app/map_plan_renderer.h"
#include <QObject>
#include <memory>
#include <optional>

class QThread;
namespace vibestudio {

struct MapPlanRenderResult {
	quint64 sceneRevision = 0, selectionRevision = 0;
	MapViewportProjection projection = MapViewportProjection::TopXY;
	MapPlanWireFrame view, baseFrame, selectionFrame;
	MapPlanWires wires, selectionWires;
	bool wiresComputed = false, selectionWiresComputed = false, failed = false;
};

// GUI-thread controller, one active immutable snapshot and one replaceable
// pending request per pane. Navigation cancels raster work but retains useful
// projection preparation; changed source/selection cancels the affected data.
class MapPlanRenderWorker final : public QObject {
public:
	explicit MapPlanRenderWorker(QObject* parent = nullptr);
	~MapPlanRenderWorker() override;
	void request(MapPlanRenderRequest request);
	void cancel();
	[[nodiscard]] bool busy() const;
	std::function<void(const MapPlanRenderResult&)> completed;
private:
	struct Work;
	std::shared_ptr<Work> m_work;
	std::optional<MapPlanRenderRequest> m_pending;
	QThread* m_thread = nullptr;
	void startNext();
};

} // namespace vibestudio
