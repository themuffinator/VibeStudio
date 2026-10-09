#include "app/viewport_image.h"

#include <cmath>

namespace vibestudio {

ViewportImageDevice viewportImageDevice(const QTransform& transform)
{
	if (!transform.isAffine() || transform.m12() != 0 || transform.m21() != 0 || transform.m11() != transform.m22()
		|| !std::isfinite(transform.m11()) || transform.m11() <= 0 || !std::isfinite(transform.dx()) || !std::isfinite(transform.dy())) { return {}; }
	return {transform.m11(),{transform.dx() - std::floor(transform.dx()),transform.dy() - std::floor(transform.dy())}};
}

QSize viewportImageSize(QSize viewport, double pixelRatio, QPointF pixelPhase)
{
	if (viewport.isEmpty() || !std::isfinite(pixelRatio) || pixelRatio <= 0
		|| !std::isfinite(pixelPhase.x()) || !std::isfinite(pixelPhase.y())
		|| pixelPhase.x() < 0 || pixelPhase.x() >= 1 || pixelPhase.y() < 0 || pixelPhase.y() >= 1
		|| !std::isfinite(pixelPhase.x() / pixelRatio) || !std::isfinite(pixelPhase.y() / pixelRatio)) { return {}; }
	const double width = std::ceil(viewport.width() * pixelRatio + pixelPhase.x());
	const double height = std::ceil(viewport.height() * pixelRatio + pixelPhase.y());
	if (width <= 0 || height <= 0 || !std::isfinite(width * height) || width * height > viewportImageMaxPixels) { return {}; }
	return QSize(int(width),int(height));
}

} // namespace vibestudio
