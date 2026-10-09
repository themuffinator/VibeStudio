#pragma once

#include <QPointF>
#include <QSize>
#include <QTransform>

namespace vibestudio {

// Ceiling for one viewport image in physical pixels: 2D line caches, vertex
// markers and GPU frames read back from the 3D views. Larger windows scale
// their images uniformly to stay within it.
constexpr qint64 viewportImageMaxPixels = 8 * 1024 * 1024;

struct ViewportImageDevice {
	// Zero means that the transform needs ordinary painter drawing.
	double pixelRatio = 0;
	// Fractional physical position of the local origin, each component in [0,1).
	QPointF pixelPhase;
};

// Cached images support positive, uniform, axis-aligned device transforms.
// Translation does not need to land on a physical pixel boundary.
ViewportImageDevice viewportImageDevice(const QTransform& transform);
// Include the leading fractional pixel and ceil-sized trailing coverage. Invalid
// or over-budget requests return an empty size, never reduced resolution.
QSize viewportImageSize(QSize viewport, double pixelRatio, QPointF pixelPhase = {});

} // namespace vibestudio
