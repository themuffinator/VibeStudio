#pragma once

#include <QCache>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QVector>
#include <algorithm>

class QPainter;
namespace vibestudio {

// Exact physical subpixel phases: no snapping of marker positions. Only the
// repeated ring raster is cached; source identities and picking stay separate.
struct ViewportRingPhase {
	double x = 0, y = 0;
	bool operator==(const ViewportRingPhase&) const = default;
	friend size_t qHash(const ViewportRingPhase& phase, size_t seed = 0) noexcept { return qHashMulti(seed,phase.x,phase.y); }
};

class ViewportRingCache {
public:
	explicit ViewportRingCache(qsizetype budget = 8 * 1024 * 1024) : m_images(std::clamp<qsizetype>(budget,0,8 * 1024 * 1024)) {}
	void clear() { m_images.clear(); }
	[[nodiscard]] qsizetype accountedBytes() const { return m_images.totalCost(); }
	[[nodiscard]] qsizetype entries() const { return m_images.size(); }
	// Unsupported transforms or an exhausted image budget draw ordinary rings.
	// Each marker is painted once, including when an allocation cannot be cached.
	void draw(QPainter& painter, const QVector<QPointF>& centers, QColor color, double width, double radius);
private:
	QCache<ViewportRingPhase,QImage> m_images;
	double m_ratio = 0, m_width = 0, m_radius = 0;
	QRgb m_color = 0;
};

} // namespace vibestudio
