#pragma once

#include "app/map_viewport_overlay.h"
#include <QObject>
#include <functional>
#include <memory>
#include <optional>

class QThread;
namespace vibestudio {

// Independent of the geometry worker: grid/member feedback must not queue
// behind a large brush/patch projection. One active and one latest pending view.
class MapViewportOverlayWorker final : public QObject {
public:
	explicit MapViewportOverlayWorker(QObject* parent = nullptr);
	~MapViewportOverlayWorker() override;
	void request(MapViewportOverlayRequest request);
	void cancel();
	[[nodiscard]] bool busy() const;
	std::function<void(const MapViewportOverlayResult&)> completed;
private:
	struct Work;
	std::shared_ptr<Work> m_work;
	std::optional<MapViewportOverlayRequest> m_pending;
	QThread* m_thread = nullptr;
	bool m_startScheduled = false;
	void startNext();
};

} // namespace vibestudio
