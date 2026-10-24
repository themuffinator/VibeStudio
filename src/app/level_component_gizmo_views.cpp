#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <algorithm>
#include <QTimer>
#include <cmath>
#include <limits>

namespace vibestudio {
LevelComponentGizmoFrame MapViewport::componentGizmoFrame() const
{
	LevelComponentGizmoFrame frame; frame.radius = 72/std::max(.0001,m_zoom);
	frame.worldAxes = m_controls.componentAxisDrag;
	const auto projection = m_projection; const auto centre = m_worldCenter; const auto zoom = m_zoom;
	const QPointF origin(width()*.5,height()*.5);
	frame.project = [projection,centre,zoom,origin](const LevelMapVec3& point) {
		const auto p = mapViewportProjectPoint(projection,point);
		return origin+QPointF((p.x()-centre.x())*zoom,-(p.y()-centre.y())*zoom);
	};
	frame.viewDirection = m_projection == MapViewportProjection::TopXY ? ModelVec3{0,0,-1} : m_projection == MapViewportProjection::FrontXZ ? ModelVec3{0,-1,0} : ModelVec3{-1,0,0};
	frame.ray = [projection,centre,zoom,origin,direction = frame.viewDirection](const QPointF& point) {
		const auto p = centre+QPointF((point.x()-origin.x())/zoom,-(point.y()-origin.y())/zoom);
		const double x = projection == MapViewportProjection::SideZY ? 0 : p.x();
		const double y = projection == MapViewportProjection::TopXY ? p.y() : projection == MapViewportProjection::SideZY ? p.x() : 0;
		const double z = projection == MapViewportProjection::TopXY ? 0 : p.y();
		return ModelPickRay{{float(x),float(y),float(z)},direction,false};
	};
	return frame;
}
bool MapViewport::beginComponentTransform(const QPointF& point)
{
	if (!isEnabled() || m_componentMode == ComponentMode::Off || m_componentDragging || m_vertexInsertion->active() || m_componentBanding) { return false; }
	return m_componentGizmo->begin(point,componentGizmoFrame(),font());
}
LevelComponentGizmoFrame ModelViewport::componentGizmoFrame(const LevelMapVec3& pivot)
{
	ensureProjection(); LevelComponentGizmoFrame frame;
	frame.perspective = m_perspective; frame.worldAxes = m_controls.componentAxisDrag;
	const auto p = pivot.valid ? pivot : m_componentGizmo->pivot(); double depth = 0;
	(void)projectPoint(m_camera,{float(p.x),float(p.y),float(p.z)},&depth);
	frame.radius = m_perspective ? 72*std::max(1.0,-depth)/m_camera.focal : 72/m_camera.scale;
	frame.viewDirection = {-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z};
	const auto camera = m_camera; const auto centre = m_center; const bool perspective = m_perspective;
	frame.project = [camera,centre,perspective](const LevelMapVec3& point) {
		const auto origin = perspective ? camera.position : centre;
		const double x = point.x-origin.x,y = point.y-origin.y,z = point.z-origin.z;
		const double right = x*camera.right.x+y*camera.right.y+z*camera.right.z;
		const double up = x*camera.up.x+y*camera.up.y+z*camera.up.z;
		const double depth = x*camera.forward.x+y*camera.forward.y+z*camera.forward.z;
		// Same one-unit near plane as the camera renderer. The captured camera
		// keeps a drag stable even when another view requests a repaint.
		if (perspective && depth < 1) { return QPointF(std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()); }
		const double scale = perspective ? camera.focal/depth : camera.scale;
		return camera.origin+QPointF(right*scale,-up*scale);
	};
	frame.ray = [camera,centre,perspective](const QPointF& point) {
		const double x = (point.x()-camera.origin.x())/(perspective ? 1 : camera.scale);
		const double y = (camera.origin.y()-point.y())/(perspective ? 1 : camera.scale);
		if (!perspective) {
			return ModelPickRay{{float(centre.x+camera.right.x*x+camera.up.x*y),float(centre.y+camera.right.y*x+camera.up.y*y),float(centre.z+camera.right.z*x+camera.up.z*y)},
				{-camera.eye.x,-camera.eye.y,-camera.eye.z},false};
		}
		const double dx = camera.forward.x*camera.focal+camera.right.x*x+camera.up.x*y;
		const double dy = camera.forward.y*camera.focal+camera.right.y*x+camera.up.y*y;
		const double dz = camera.forward.z*camera.focal+camera.right.z*x+camera.up.z*y;
		const double length = std::sqrt(dx*dx+dy*dy+dz*dz);
		return ModelPickRay{camera.position,{float(dx/length),float(dy/length),float(dz/length)},true};
	};
	frame.visible = [this,project = frame.project](const LevelMapVec3& point) {
		const auto screen = project(point); if (!QRectF(rect()).contains(screen)) { return false; }
		if (m_componentXray) { return true; }
		CameraSurfacePoint surface; if (!surfacePointAt(screen,&surface)) { return !isRendering(); }
		double depth = 0,surfaceDepth = 0;
		(void)projectPoint(m_camera,{float(point.x),float(point.y),float(point.z)},&depth);
		(void)projectPoint(m_camera,{float(surface.position[0]),float(surface.position[1]),float(surface.position[2])},&surfaceDepth);
		return depth >= surfaceDepth-.15;
	};
	return frame;
}
bool ModelViewport::beginComponentTransform(const QPointF& point)
{
	if (!isEnabled() || !m_componentMode || !m_hasMesh || m_looking || m_componentMoving || m_vertexInsertion->active() || m_componentBanding || m_pressButton != Qt::NoButton) { return false; }
	ensureProjection(); if (isRendering()) { return false; }
	if (!m_componentGizmo->begin(point,componentGizmoFrame(),font())) { return false; }
	m_heldKeys.clear(); m_flyTimer->stop(); return true;
}
} // namespace vibestudio
