#include "app/model_viewport.h"

#include <cmath>

// Small public helpers for the Mesh Editor's modal tools and selection
// overlays. They reuse the viewport's own camera state and projection.
namespace vibestudio
{
ModelPickRay ModelViewport::viewRay(const QPointF &point)
{
	ensureProjection();
	return editRay(point);
}

bool ModelViewport::projectToView(const ModelVec3 &point, QPointF *screen)
{
	if (!m_hasMesh || !screen)
		return false;
	ensureProjection();
	double depth = 0;
	const QPointF projected = projectPoint(m_camera, point, &depth);
	if (!std::isfinite(projected.x()) || !std::isfinite(projected.y()))
		return false;
	*screen = projected;
	return true;
}

ModelVec3 ModelViewport::viewForward()
{
	ensureProjection();
	if (m_perspective)
		return viewDirection();
	return {-m_camera.eye.x, -m_camera.eye.y, -m_camera.eye.z};
}

void ModelViewport::viewAxes(ModelVec3 *right, ModelVec3 *up)
{
	ensureProjection();
	if (right)
		*right = m_camera.right;
	if (up)
		*up = m_camera.up;
}

void ModelViewport::setOverlayPainter(std::function<void(QPainter &)> painter)
{
	m_overlayPainter = std::move(painter);
	update();
}
} // namespace vibestudio
