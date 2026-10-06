#pragma once

#include "core/level_materials.h"
#include "core/map_geometry_cache.h"
#include <QObject>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio
{
class PackageStagingModel;

struct LevelPreviewRequest {
	LevelMapDocument document;
	std::shared_ptr<const PackageArchiveReader> archive;
	std::shared_ptr<const PackageStagingModel> staging;
	LevelPreviewAssetOptions options;
	QString assetKey;
	QString sourceKey;
	quint64 loadSerial = 0;
};
struct LevelPreviewResult {
	LevelPreviewAssets assets;
	LevelMapPreviewMesh preview;
	MapBrushGeometryCacheStatistics geometryCache;
	QString sourceKey;
	quint64 loadSerial = 0;
	quint64 revision = 0;
	QString error;
};

// One bounded worker per camera. Coalesces edits and invalidates retired results
// immediately. Unchanged asset requests reuse decoded images and static models;
// unchanged brush planes reuse solved polygons; UVs, asset dimensions, ownership
// and model placement still rebuild from the latest document snapshot.
class LevelPreviewWorker final : public QObject
{
  public:
	explicit LevelPreviewWorker(QObject *parent = nullptr);
	~LevelPreviewWorker() override;
	void request(LevelPreviewRequest request);
	void reset();
	void cancel();
	bool busy() const;
	std::function<void()> started;
	std::function<void(int, int)> progress;
	std::function<void(const LevelPreviewResult &)> completed;

  private:
	struct Work;
	void startPending();
	QThread *m_thread = nullptr;
	QTimer *m_timer = nullptr;
	QTimer *m_debounce = nullptr;
	std::optional<LevelPreviewRequest> m_pending;
	std::shared_ptr<Work> m_work;
	quint64 m_serial = 0;
	QString m_cacheKey;
	LevelPreviewAssets m_cachedAssets;
	MapBrushGeometryCache m_cachedGeometry;
};
} // namespace vibestudio
