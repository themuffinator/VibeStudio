#pragma once

#include <QImage>
#include <QLineF>
#include <QPointF>
#include <QSize>
#include <QVector>
#include <array>
#include <atomic>

namespace vibestudio {
struct MapGridView {
	QSize viewport;
	QPointF center;
	double zoom = 1, pixelRatio = 1;
	int units = 64;
	QRgb minor = 0, major = 0, axis = 0;
	QPointF pixelPhase;
};
struct MapGridFrame { MapGridView view; QImage image; };
// Shared line placement for the image path and complete ordinary fallback.
using MapGridLines = std::array<QVector<QLineF>,3>;
MapGridLines mapGridLines(const MapGridView& view);
bool sameMapGridView(const MapGridView& a, const MapGridView& b);
// Ceil physical extent, bounded to the shared viewport image limit.
QSize mapGridImageSize(const MapGridView& view);
// Cached native Qt grid image, in minor/major/axis overdraw order.
// Invalid/oversized requests leave the previous frame intact and return false.
bool renderMapGrid(const MapGridView& view, MapGridFrame* frame, const std::atomic_bool* cancelled = nullptr);
} // namespace vibestudio
