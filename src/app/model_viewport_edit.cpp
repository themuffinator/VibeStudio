#include "app/model_viewport.h"
#include "core/model_collision.h"
#include "core/model_tags.h"
#include "core/model_surface_selection.h"

#include <QApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio
{
namespace
{
constexpr double trackballRadius = 86;
QPointF unavailable() { return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()}; }
bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
double squared(QPointF point) { return QPointF::dotProduct(point, point); }
ModelVec3 add(ModelVec3 a, ModelVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
ModelVec3 axisTip(ModelVec3 centre, int axis, double distance)
{
	if (axis == 0)
	{
		centre.x += float(distance);
	}
	else if (axis == 1)
	{
		centre.y += float(distance);
	}
	else
	{
		centre.z += float(distance);
	}
	return centre;
}
} // namespace

void ModelViewport::setEditSelection(int surface, const QSet<int> &vertices)
{
	if (surface == m_editSurface && vertices == m_editVertices && m_editSurfaces.isEmpty())
	{
		return;
	}
	QSet<int> valid;
	if (surface >= 0 && surface < m_mesh.surfaces.size())
	{
		const int count = m_mesh.surfaces.at(surface).vertexCount;
		const auto accepted = [count](int vertex) { return vertex >= 0 && vertex < count; };
		if (std::all_of(vertices.cbegin(), vertices.cend(), accepted))
		{
			valid = vertices;
		}
		else
		{
			for (int vertex : vertices)
			{
				if (accepted(vertex))
				{
					valid.insert(vertex);
				}
			}
		}
	}
	else
	{
		surface = -1;
	}
	if (surface == m_editSurface && valid == m_editVertices && m_editSurfaces.isEmpty())
	{
		return;
	}
	finishEditTransform(false);
	if (surface != m_editSurface || m_vertexPicking)
	{
		invalidateVertexOverlay();
	}
	m_editSurfaces.clear();
	m_editSurface = surface;
	m_editVertices = std::move(valid);
	if (!m_editVertices.isEmpty())
	{
		m_editTag.clear();
		m_editCollision.clear();
	}
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setEditSurfaces(int active, const QSet<int> &surfaces)
{
	ModelSelection selected;
	selected.surface = active;
	selected.surfaces = surfaces;
	if (!validModelSurfaceSelection(m_mesh, selected) || active < 0 || active >= m_mesh.surfaces.size())
		return;
	if (active == m_editSurface && surfaces == m_editSurfaces && m_editVertices.isEmpty())
		return;
	setEditSelection(active, {});
	m_editSurfaces = surfaces;
	if (!surfaces.isEmpty())
	{
		m_editTag.clear();
		m_editCollision.clear();
	}
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setVertexPicking(bool enabled)
{
	if (m_vertexPicking == enabled)
	{
		return;
	}
	finishEditTransform(false);
	m_vertexPicking = enabled;
	invalidateVertexOverlay();
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setShowTags(bool show)
{
	if (m_showTags == show)
	{
		return;
	}
	finishEditTransform(false);
	m_showTags = show;
	invalidateRaster();
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setTagPicking(bool enabled)
{
	if (m_tagPicking == enabled)
	{
		return;
	}
	finishEditTransform(false);
	m_tagPicking = enabled;
	if (enabled)
	{
		setShowTags(true);
	}
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setEditTag(const QString &name)
{
	const auto valid = findModelTag(m_mesh, name, m_frame) ? name : QString{};
	if (m_editTag == valid)
	{
		return;
	}
	finishEditTransform(false);
	m_editTag = valid;
	if (!valid.isEmpty())
	{
		m_editCollision.clear();
		m_editVertices.clear();
		m_editSurfaces.clear();
		if (m_vertexPicking)
		{
			invalidateVertexOverlay();
		}
	}
	setAccessibleDescription(accessibleSummary());
	update();
}
bool ModelViewport::tagPose(const QString &name, ModelTag *result) const
{
	const auto tag = findModelTag(m_mesh, name, m_frame);
	if (!tag || !result)
	{
		return false;
	}
	*result = m_editMoveActive && name == m_editTag ? transformedModelTag(*tag, m_editTransform) : *tag;
	if (m_frameBlend > 0)
	{
		const auto next = findModelTag(m_mesh, name, m_blendFrame);
		return next && interpolateModelTag(*tag, *next, m_frameBlend, result);
	}
	return true;
}
QPointF ModelViewport::tagScreenPosition(const QString &name)
{
	ensureProjection();
	ModelTag tag;
	return tagPose(name, &tag) ? projectPoint(m_camera, tag.origin, nullptr) : unavailable();
}
QString ModelViewport::tagAt(const QPointF &point, double tolerance)
{
	if (!m_showTags || !m_tagPicking || !finite(point) || !std::isfinite(tolerance) || tolerance <= 0 || tolerance > 64 || isRendering() ||
		m_editMoveActive)
	{
		return {};
	}
	QString result;
	double nearest = tolerance * tolerance;
	for (const auto &tag : m_mesh.tags)
	{
		if (tag.frameIndex != m_frame)
		{
			continue;
		}
		const auto at = tagScreenPosition(tag.name);
		if (finite(at) && rect().contains(at.toPoint()) && squared(at - point) < nearest)
		{
			nearest = squared(at - point);
			result = tag.name;
		}
	}
	return result;
}
void ModelViewport::setXrayVertices(bool enabled)
{
	if (m_xrayVertices == enabled)
	{
		return;
	}
	m_xrayVertices = enabled;
	if (m_vertexPicking)
	{
		invalidateVertexOverlay();
	}
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setMoveGizmo(bool enabled, double grid) { setTransformGizmo(enabled, ModelTransformTool::Move, grid); }
void ModelViewport::setTransformGizmo(bool enabled, ModelTransformTool tool, double grid, double angleGrid, double scaleGrid)
{
	ModelVec3 checked;
	if (int(tool) < 0 || int(tool) > 2 || !snapModelTranslation({}, grid, &checked) || !snapModelRotation({}, angleGrid, &checked) ||
		!snapModelScale({1, 1, 1}, scaleGrid, &checked))
	{
		return;
	}
	if (enabled == m_moveGizmo && grid == m_editGrid && tool == m_transformTool && angleGrid == m_editRotationGrid &&
		scaleGrid == m_editScaleGrid)
	{
		return;
	}
	finishEditTransform(false);
	m_moveGizmo = enabled;
	m_editGrid = grid;
	m_transformTool = tool;
	m_editRotationGrid = angleGrid;
	m_editScaleGrid = scaleGrid;
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setTransformPivot(ModelTransformPivot mode, ModelVec3 custom)
{
	if (int(mode) < 0 || int(mode) > 2 || !std::isfinite(custom.x) || !std::isfinite(custom.y) || !std::isfinite(custom.z))
	{
		return;
	}
	if (mode == m_transformPivot && custom.x == m_customTransformPivot.x && custom.y == m_customTransformPivot.y &&
		custom.z == m_customTransformPivot.z)
	{
		return;
	}
	finishEditTransform(false);
	m_transformPivot = mode;
	m_customTransformPivot = custom;
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setTransformAxes(const ModelTransformBasis &basis, const QString &name, bool available)
{
	if (available && !validModelTransformBasis(basis))
		return;
	bool same = basis.world == m_transformBasis.world;
	for (int i = 0; i < 3; ++i)
		same = same && basis.axes[i].x == m_transformBasis.axes[i].x && basis.axes[i].y == m_transformBasis.axes[i].y &&
			basis.axes[i].z == m_transformBasis.axes[i].z;
	if (same && name == m_transformAxesName && available == m_transformAxesAvailable)
		return;
	finishEditTransform(false);
	m_transformBasis = basis;
	m_transformAxesName = name;
	m_transformAxesAvailable = available;
	setAccessibleDescription(accessibleSummary());
	update();
}
ModelVec3 ModelViewport::editVertexPosition(int surface, int vertex) const
{
	const auto &part = m_mesh.surfaces.at(surface);
	const auto &pose = part.frames[std::min(m_frame, int(part.frames.size()) - 1)];
	auto point = pose.positions[vertex];
	if (m_frameBlend > 0 && m_blendFrame < part.frames.size() && part.frames[m_blendFrame].positions.size() == pose.positions.size())
	{
		point = interpolateModelPosition(point, part.frames[m_blendFrame].positions[vertex], m_frameBlend);
	}
	return m_editMoveActive && (m_editSurfaces.contains(surface) || (surface == m_editSurface && m_editVertices.contains(vertex))) ? transformModelPoint(point, m_editTransform)
																						   : point;
}
QPointF ModelViewport::vertexScreenPosition(int surface, int vertex)
{
	ensureProjection();
	if (surface < 0 || surface >= m_mesh.surfaces.size())
	{
		return unavailable();
	}
	const auto &part = m_mesh.surfaces.at(surface);
	if (part.frames.isEmpty() || vertex < 0 || vertex >= part.frames[std::min(m_frame, int(part.frames.size()) - 1)].positions.size())
	{
		return unavailable();
	}
	return projectPoint(m_camera, editVertexPosition(surface, vertex), nullptr);
}
ModelViewportVertexHit ModelViewport::vertexAt(const QPointF &point, double tolerance)
{
	ensureProjection();
	if (isRendering() || m_editMoveActive || !m_rasterVertices || m_rasterVertices->surface != m_editSurface)
	{
		return {};
	}
	const int vertex = pickModelVertex(*m_rasterVertices, point, tolerance, m_xrayVertices);
	return vertex < 0 ? ModelViewportVertexHit{} : ModelViewportVertexHit{true, m_editSurface, vertex};
}
bool ModelViewport::editPivot(ModelVec3 *pivot) const
{
	if (!m_transformAxesAvailable)
		return false;
	if (m_playing && (m_interpolateAnimation || m_nativeMdl))
	{
		return false;
	}
	if (pivot && !m_editCollision.isEmpty())
	{
		const auto box = findModelCollisionBox(m_mesh, m_editCollision);
		ModelCollisionBox pose;
		if (!box || !m_showCollision || !sampleModelCollisionBox(*box, m_frame, m_frame, 0, &pose))
		{
			return false;
		}
		if (m_editMoveActive)
		{
			*pivot = add(m_editTransform.pivot, modelBasisToWorld(m_editTransform.translation, m_editTransform.basis));
			return true;
		}
		return modelTransformPivot({pose.centre}, {0}, m_transformPivot, m_customTransformPivot, pivot);
	}
	if (pivot && !m_editTag.isEmpty())
	{
		const auto tag = findModelTag(m_mesh, m_editTag, m_frame);
		if (!tag || !m_showTags || m_transformTool == ModelTransformTool::Scale)
		{
			return false;
		}
		if (m_editMoveActive)
		{
			*pivot = add(m_editTransform.pivot, modelBasisToWorld(m_editTransform.translation, m_editTransform.basis));
			return true;
		}
		return modelTransformPivot({tag->origin}, {0}, m_transformPivot, m_customTransformPivot, pivot);
	}
	if (pivot && !m_editSurfaces.isEmpty())
	{
		if (m_editMoveActive)
		{
			*pivot = add(m_editTransform.pivot, modelBasisToWorld(m_editTransform.translation, m_editTransform.basis));
			return true;
		}
		if (m_editPivotCache.surfaces.constData() != m_mesh.surfaces.constData() ||
			m_editPivotCache.wholeSurfaces != m_editSurfaces || m_editPivotCache.frame != m_frame)
		{
			m_editPivotCache = {};
			m_editPivotCache.surfaces = m_mesh.surfaces;
			m_editPivotCache.wholeSurfaces = m_editSurfaces;
			m_editPivotCache.frame = m_frame;
			m_editPivotCache.valid = modelSurfaceSelectionPivot(m_mesh, m_editSurfaces, m_frame,
				ModelTransformPivot::SelectionCentre, {}, &m_editPivotCache.centre);
		}
		if (!m_editPivotCache.valid)
			return false;
		*pivot = m_transformPivot == ModelTransformPivot::Origin ? ModelVec3{} :
			m_transformPivot == ModelTransformPivot::Custom ? m_customTransformPivot : m_editPivotCache.centre;
		return true;
	}
	if (!pivot || m_editSurface < 0 || m_editSurface >= m_mesh.surfaces.size() || m_editVertices.isEmpty())
	{
		return false;
	}
	const auto &part = m_mesh.surfaces[m_editSurface];
	if (part.frames.isEmpty())
	{
		return false;
	}
	const auto &positions = part.frames[std::min(m_frame, int(part.frames.size()) - 1)].positions;
	if (m_editMoveActive)
	{
		*pivot = add(m_editTransform.pivot, modelBasisToWorld(m_editTransform.translation, m_editTransform.basis));
		return true;
	}
	if (m_editPivotCache.positions.constData() != positions.constData() || m_editPivotCache.vertices != m_editVertices)
	{
		// Retain the implicitly shared inputs so replacement data cannot reuse
		// their addresses. Painting and handle queries share one validated centre.
		m_editPivotCache = {};
		m_editPivotCache.positions = positions;
		m_editPivotCache.vertices = m_editVertices;
		m_editPivotCache.valid =
			modelTransformPivot(positions, m_editVertices, ModelTransformPivot::SelectionCentre, {}, &m_editPivotCache.centre);
	}
	if (!m_editPivotCache.valid)
	{
		return false;
	}
	*pivot = m_transformPivot == ModelTransformPivot::Origin   ? ModelVec3{}
			 : m_transformPivot == ModelTransformPivot::Custom ? m_customTransformPivot
															   : m_editPivotCache.centre;
	return true;
}
double ModelViewport::editGizmoRadius(ModelVec3 pivot) const
{
	double scale = m_camera.scale;
	if (m_perspective)
	{
		double x, y, depth;
		toView(pivot, &x, &y, &depth);
		if (depth <= 0)
		{
			return 0;
		}
		scale = m_camera.focal / depth;
	}
	return std::isfinite(scale) && scale > 1e-9 ? 72.0 / scale : 0;
}
std::array<QPolygonF, 3> ModelViewport::rotationGizmoRings()
{
	ensureProjection();
	std::array<QPolygonF, 3> rings;
	ModelVec3 pivot;
	if (!m_moveGizmo || m_transformTool != ModelTransformTool::Rotate || !editPivot(&pivot))
	{
		return rings;
	}
	const auto origin = projectPoint(m_camera, pivot, nullptr);
	const double radius = editGizmoRadius(pivot);
	if (!finite(origin) || !rect().contains(origin.toPoint()) || radius <= 0)
	{
		return rings;
	}
	for (int axis = 0; axis < 3; ++axis)
	{
		for (int step = 0; step <= 96; ++step)
		{
			const double angle = step * 2 * std::numbers::pi / 96;
			const auto u = editGizmoAxis((axis + 1) % 3), v = editGizmoAxis((axis + 2) % 3);
			const auto point = add(pivot, {float(radius * (u.x * std::cos(angle) + v.x * std::sin(angle))),
				float(radius * (u.y * std::cos(angle) + v.y * std::sin(angle))),
				float(radius * (u.z * std::cos(angle) + v.z * std::sin(angle)))});
			rings[axis] << projectPoint(m_camera, point, nullptr);
		}
	}
	return rings;
}
std::array<QPointF, 4> ModelViewport::transformGizmoPoints()
{
	ensureProjection();
	std::array<QPointF, 4> result{unavailable(), unavailable(), unavailable(), unavailable()};
	ModelVec3 centre;
	if (!m_moveGizmo || !editPivot(&centre))
	{
		return result;
	}
	const auto origin = projectPoint(m_camera, centre, nullptr);
	if (!finite(origin) || !rect().contains(origin.toPoint()))
	{
		return result;
	}
	result[3] = origin;
	if (m_transformTool == ModelTransformTool::Rotate)
	{
		const auto rings = rotationGizmoRings();
		for (int axis = 0; axis < 3; ++axis)
		{
			if (rings[axis].size() > 12)
			{
				result[axis] = rings[axis][12];
			}
		}
		return result;
	}
	const double radius = editGizmoRadius(centre);
	if (radius <= 0)
	{
		return result;
	}
	for (int axis = 0; axis < 3; ++axis)
	{
		const auto direction = editGizmoAxis(axis);
		const auto endpoint = projectPoint(
			m_camera, add(centre, {float(direction.x * radius), float(direction.y * radius), float(direction.z * radius)}), nullptr);
		if (finite(endpoint) && squared(endpoint - origin) >= 144)
		{
			result[axis] = endpoint;
		}
	}
	return result;
}
QRectF ModelViewport::trackballGizmoRect()
{
	if (m_transformTool != ModelTransformTool::Rotate) return {};
	const auto centre = transformGizmoPoints()[3];
	return finite(centre) ? QRectF(centre - QPointF(trackballRadius, trackballRadius), QSizeF(2 * trackballRadius, 2 * trackballRadius))
		: QRectF{};
}
int ModelViewport::transformGizmoAt(const QPointF &point)
{
	// Handles use current camera/document geometry and do not read raster depth.
	// A pending image must not prevent an explicit edit after pausing playback.
	if (!m_moveGizmo || !finite(point) || m_looking || m_editMoveActive)
	{
		return -1;
	}
	const auto handles = transformGizmoPoints();
	const auto centre = handles[3];
	if (!finite(centre))
	{
		return -1;
	}
	if (m_transformTool == ModelTransformTool::Rotate)
	{
		// A visible centre target stays usable even when an edge-on ring
		// crosses it. Elsewhere the three constrained rings retain priority.
		const double distanceToCentre = squared(point - centre);
		if (distanceToCentre <= 64) return 3;
		const auto rings = rotationGizmoRings();
		double nearest = 49;
		int handle = -1;
		for (int axis = 0; axis < 3; ++axis)
		{
			for (int i = 1; i < rings[axis].size(); ++i)
			{
				const auto a = rings[axis][i - 1], b = rings[axis][i], delta = b - a;
				if (!finite(a) || !finite(b) || squared(delta) < 1e-8)
				{
					continue;
				}
				const double t = std::clamp(QPointF::dotProduct(point - a, delta) / squared(delta), 0.0, 1.0);
				const double distance = squared(point - (a + delta * t));
				if (distance < nearest)
				{
					nearest = distance;
					handle = axis;
				}
			}
		}
		return handle >= 0 ? handle : distanceToCentre <= trackballRadius * trackballRadius ? 3 : -1;
	}
	if (squared(point - centre) <= 64)
	{
		return 3;
	}
	double nearest = 49;
	int result = -1;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (!finite(handles[axis]))
		{
			continue;
		}
		const auto delta = handles[axis] - centre;
		const double length = squared(delta);
		const double t = std::clamp(QPointF::dotProduct(point - centre, delta) / length, 0.18, 1.0);
		const double distance = squared(point - (centre + delta * t));
		if (distance < nearest)
		{
			nearest = distance;
			result = axis;
		}
	}
	return result;
}
ModelPickRay ModelViewport::editRay(const QPointF &point) const
{
	if (m_perspective)
	{
		return {m_eye, pickDirection(point), true};
	}
	const double x = (point.x() - m_camera.origin.x()) / m_camera.scale;
	const double y = (m_camera.origin.y() - point.y()) / m_camera.scale;
	return {{float(m_center.x + m_camera.right.x * x + m_camera.up.x * y), float(m_center.y + m_camera.right.y * x + m_camera.up.y * y),
			 float(m_center.z + m_camera.right.z * x + m_camera.up.z * y)},
			{-m_camera.eye.x, -m_camera.eye.y, -m_camera.eye.z}};
}
ModelVec3 ModelViewport::editGizmoAxis(int axis) const
{
	const auto box = findModelCollisionBox(m_mesh, m_editCollision);
	ModelCollisionBox pose;
	return box && m_transformTool == ModelTransformTool::Scale && sampleModelCollisionBox(*box, m_frame, m_frame, 0, &pose)
		? modelCollisionAxes(pose)[axis] : m_transformBasis.axes[axis];
}
ModelVec3 ModelViewport::editGizmoCoordinates(ModelVec3 vector) const
{
	const auto box = findModelCollisionBox(m_mesh, m_editCollision);
	ModelCollisionBox pose;
	if (!box || m_transformTool != ModelTransformTool::Scale || !sampleModelCollisionBox(*box, m_frame, m_frame, 0, &pose))
	{
		return modelBasisFromWorld(vector, m_transformBasis);
	}
	const auto axes = modelCollisionAxes(pose);
	const auto dot = [vector](ModelVec3 axis) { return vector.x * axis.x + vector.y * axis.y + vector.z * axis.z; };
	return {dot(axes[0]), dot(axes[1]), dot(axes[2])};
}
ModelPickRay ModelViewport::editGizmoRay(const QPointF &point) const
{
	const auto ray = editRay(point);
	return {editGizmoCoordinates(ray.origin), editGizmoCoordinates(ray.direction), ray.forwardOnly};
}
bool ModelViewport::beginEditTransform(const QPointF &point)
{
	// Semantic gesture requests retain their pause-before-edit contract. Pointer
	// navigation skips this entry point while smooth playback hides the handles.
	if (m_playing && (m_interpolateAnimation || m_nativeMdl) && m_moveGizmo && !m_looking && finite(point) &&
		(!m_editSurfaces.isEmpty() || !m_editVertices.isEmpty() || !m_editTag.isEmpty() || !m_editCollision.isEmpty()))
	{
		pause();
	}
	const int handle = transformGizmoAt(point);
	if (handle < 0)
	{
		return false;
	}
	pause();
	ensureProjection();
	ModelVec3 pivot;
	if (!m_moveGizmo || !editPivot(&pivot))
	{
		return false;
	}
	const ModelVec3 view{-m_camera.eye.x, -m_camera.eye.y, -m_camera.eye.z};
	if (m_transformTool == ModelTransformTool::Rotate && handle == 3)
	{
		m_trackballCentre = projectPoint(m_camera, pivot, nullptr);
		m_trackballPoint = point;
		ModelTransformBasis viewBasis;
		viewBasis.world = false;
		viewBasis.axes = {m_camera.right, m_camera.up, m_camera.eye};
		if (!beginModelTrackballDrag((point.x() - m_trackballCentre.x()) / trackballRadius,
			(m_trackballCentre.y() - point.y()) / trackballRadius, viewBasis, m_transformBasis, m_editRotationGrid, &m_trackballDrag))
			return false;
	}
	else if (m_transformTool == ModelTransformTool::Rotate)
	{
		const auto localView = editGizmoCoordinates(view);
		const double direction[]{localView.x, localView.y, localView.z};
		m_rotationLinear =
			std::abs(direction[handle]) < 0.15 || !beginModelRotateDrag(editGizmoCoordinates(pivot), handle, editGizmoRay(point), m_editRotationGrid, &m_rotationDrag);
		if (m_rotationLinear)
		{
			// An edge-on rotation plane has no stable ray intersection. Use the
			// projected tangent at the grabbed ring location, fixed for this drag.
			const auto ring = rotationGizmoRings()[handle];
			double nearest = std::numeric_limits<double>::infinity();
			m_rotationTangent = {};
			for (int i = 0; i < 96 && ring.size() == 97; ++i)
			{
				const auto tangent = (ring[(i + 1) % 96] - ring[(i + 95) % 96]) / (4 * std::numbers::pi / 96);
				if (!finite(ring[i]) || !finite(tangent) || squared(tangent) < 1)
				{
					continue;
				}
				const double distance = squared(ring[i] - point);
				if (distance < nearest)
				{
					nearest = distance;
					m_rotationTangent = tangent;
				}
			}
			if (squared(m_rotationTangent) < 1)
			{
				return false;
			}
		}
	}
	else if (m_transformTool == ModelTransformTool::Move || handle < 3)
	{
		if (!beginModelMoveDrag(editGizmoCoordinates(pivot), editGizmoCoordinates(view), ModelMoveConstraint(handle), editGizmoRay(point),
								m_transformTool == ModelTransformTool::Move ? m_editGrid : 0, &m_editDrag))
		{
			return false;
		}
	}
	m_scaleReference = editGizmoRadius(pivot);
	if (m_scaleReference <= 0)
	{
		return false;
	}
	m_editMoveActive = true;
	m_editMoveTravelled = false;
	m_editTransformValid = true;
	m_transformHandle = handle;
	m_editTransform = {};
	m_editTransform.pivot = pivot;
	m_editTransform.basis = m_transformBasis;
	m_editMovePress = point;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT editTransformActiveChanged(true);
	Q_EMIT editMoveActiveChanged(true);
	return true;
}
bool ModelViewport::updateEditTransform(const QPointF &point)
{
	if (!m_editMoveActive)
	{
		return false;
	}
	if (!finite(point))
	{
		m_editTransformValid = false;
		setAccessibleDescription(accessibleSummary());
		update();
		Q_EMIT editTransformPreviewChanged();
		return false;
	}
	if (!m_editMoveTravelled && std::sqrt(squared(point - m_editMovePress)) < QApplication::startDragDistance())
	{
		return true;
	}
	auto candidate = m_editTransform;
	bool valid = true;
	if (m_transformTool == ModelTransformTool::Move)
	{
		valid = modelMoveDragDelta(m_editDrag, editGizmoRay(point), &candidate.translation);
	}
	else if (m_transformTool == ModelTransformTool::Rotate && m_transformHandle == 3)
	{
		valid = modelTrackballRotation(m_trackballDrag, (point.x() - m_trackballCentre.x()) / trackballRadius,
			(m_trackballCentre.y() - point.y()) / trackballRadius, &candidate.rotation);
		if (valid) m_trackballPoint = point;
	}
	else if (m_transformTool == ModelTransformTool::Rotate)
	{
		double angle = 0;
		if (m_rotationLinear)
		{
			angle = QPointF::dotProduct(point - m_editMovePress, m_rotationTangent) / squared(m_rotationTangent) * 180 / std::numbers::pi;
		}
		else
		{
			valid = modelRotateDragAngle(&m_rotationDrag, editGizmoRay(point), &angle);
		}
		ModelVec3 rotation;
		if (m_transformHandle == 0)
		{
			rotation.x = float(angle);
		}
		else if (m_transformHandle == 1)
		{
			rotation.y = float(angle);
		}
		else
		{
			rotation.z = float(angle);
		}
		valid = valid && snapModelRotation(rotation, m_editRotationGrid, &candidate.rotation);
	}
	else
	{
		double factor = 1;
		if (m_transformHandle == 3)
		{
			const auto delta = point - m_editMovePress;
			factor = std::exp2((delta.x() - delta.y()) / 96.0);
		}
		else
		{
			ModelVec3 offset;
			valid = modelMoveDragDelta(m_editDrag, editGizmoRay(point), &offset);
			const double values[]{offset.x, offset.y, offset.z};
			factor = 1 + values[m_transformHandle] / m_scaleReference;
		}
		ModelVec3 scale{1, 1, 1};
		if (m_transformHandle == 3)
		{
			scale = {float(factor), float(factor), float(factor)};
		}
		else if (m_transformHandle == 0)
		{
			scale.x = float(factor);
		}
		else if (m_transformHandle == 1)
		{
			scale.y = float(factor);
		}
		else
		{
			scale.z = float(factor);
		}
		valid = valid && factor > 0 && snapModelScale(scale, m_editScaleGrid, &candidate.scale) && candidate.scale.x > 0 &&
				candidate.scale.y > 0 && candidate.scale.z > 0;
	}
	if (valid && !m_editCollision.isEmpty())
	{
		const auto box = findModelCollisionBox(m_mesh, m_editCollision);
		ModelCollisionBox pose, transformed;
		valid = box && sampleModelCollisionBox(*box, m_frame, m_frame, 0, &pose) && transformModelCollisionBox(pose, candidate, &transformed);
	}
	m_editMoveTravelled = true;
	m_editTransformValid = valid;
	if (valid)
	{
		m_editTransform = candidate;
		invalidateProjection();
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT editTransformPreviewChanged();
	if (valid && m_transformTool == ModelTransformTool::Move)
	{
		const auto delta = modelBasisToWorld(candidate.translation, candidate.basis);
		Q_EMIT editMovePreviewChanged(delta.x, delta.y, delta.z);
	}
	return valid;
}
void ModelViewport::finishEditTransform(bool commit)
{
	if (!m_editMoveActive)
	{
		return;
	}
	const auto transform = m_editTransform;
	const auto tool = m_transformTool;
	const bool apply = commit && m_editTransformValid && m_editMoveTravelled &&
					   (transform.translation.x != 0 || transform.translation.y != 0 || transform.translation.z != 0 ||
						transform.rotation.x != 0 || transform.rotation.y != 0 || transform.rotation.z != 0 || transform.scale.x != 1 ||
						transform.scale.y != 1 || transform.scale.z != 1);
	m_editMoveActive = false;
	m_editMoveTravelled = false;
	m_editTransform = {};
	m_pressButton = Qt::NoButton;
	m_dragAction = DragAction::None;
	m_dragStarted = false;
	unsetCursor();
	invalidateProjection();
	invalidateRaster(true);
	m_raster.clear();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT editTransformActiveChanged(false);
	Q_EMIT editMoveActiveChanged(false);
	if (apply)
	{
		Q_EMIT editTransformRequested(transform);
		if (tool == ModelTransformTool::Move)
		{
			const auto delta = modelBasisToWorld(transform.translation, transform.basis);
			Q_EMIT editMoveRequested(delta.x, delta.y, delta.z);
		}
	}
}
ModelTransform ModelViewport::editTransform() const { return m_editTransform; }
bool ModelViewport::editTransformValid() const { return m_editTransformValid; }
bool ModelViewport::editingMove() const { return m_editMoveActive; }
ModelVec3 ModelViewport::editMoveDelta() const { return modelBasisToWorld(m_editTransform.translation, m_editTransform.basis); }
std::array<QPointF, 4> ModelViewport::moveGizmoPoints() { return transformGizmoPoints(); }
int ModelViewport::moveGizmoAt(const QPointF &point) { return transformGizmoAt(point); }
bool ModelViewport::beginEditMove(const QPointF &point) { return beginEditTransform(point); }
bool ModelViewport::updateEditMove(const QPointF &point) { return updateEditTransform(point); }
void ModelViewport::finishEditMove(bool commit) { finishEditTransform(commit); }

QVector<ModelViewport::TagOverlay> ModelViewport::projectTagOverlays() const
{
	QVector<TagOverlay> result;
	if (!m_showTags)
	{
		return result;
	}
	for (const auto &source : m_mesh.tags)
	{
		if (source.frameIndex != m_frame)
		{
			continue;
		}
		ModelTag tag;
		if (!tagPose(source.name, &tag))
		{
			continue;
		}
		TagOverlay projected;
		projected.name = source.name;
		projected.centre = projectPoint(m_camera, tag.origin, nullptr);
		if (!finite(projected.centre))
		{
			continue;
		}
		const double size = editGizmoRadius(tag.origin);
		for (int selected = 0; selected < 2; ++selected)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				projected.tips[selected * 3 + axis] =
					projectPoint(m_camera, modelTagPoint(tag, axisTip({}, axis, size * (selected ? .55 : .3))), nullptr);
			}
		}
		result << projected;
	}
	return result;
}

void ModelViewport::paintEditOverlays(QPainter &painter)
{
	struct Annotation
	{
		QPointF anchor;
		QString text;
		int priority;
	};
	QVector<Annotation> annotations;
	QVector<QRectF> occupied;
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, false);
	const QColor accent = palette().color(QPalette::Highlight);
	if (m_vertexPicking && !(m_playing && (m_interpolateAnimation || m_nativeMdl)) && !m_vertexOverlay.isNull())
	{
		// Markers and the mesh use the same completed camera/pose snapshot.
		painter.drawImage(QRectF(rect()), m_vertexOverlay);
	}
	if (m_showTags)
	{
		painter.setRenderHint(QPainter::Antialiasing, true);
		const QColor colors[]{QColor(255, 100, 100), QColor(100, 230, 130), QColor(115, 165, 255)};
		// A coalesced raster can lag the clock or camera. Its attachment markers
		// must use that same projected pose, including during resize and wireframe.
		const bool snapshot = !m_raster.image.isNull();
		const auto tags = snapshot ? m_rasterTags : projectTagOverlays();
		const double sx = snapshot ? double(width()) / std::max(1, m_rasterLogicalSize.width()) : 1;
		const double sy = snapshot ? double(height()) / std::max(1, m_rasterLogicalSize.height()) : 1;
		const auto scaled = [sx, sy](QPointF point) { return QPointF(point.x() * sx, point.y() * sy); };
		for (const auto &tag : tags)
		{
			const auto centre = scaled(tag.centre);
			if (!finite(centre) || !rect().contains(centre.toPoint()))
			{
				continue;
			}
			const bool selected = tag.name == m_editTag;
			for (int axis = 0; axis < 3; ++axis)
			{
				const auto tip = scaled(tag.tips[axis + (selected ? 3 : 0)]);
				if (!finite(tip))
				{
					continue;
				}
				painter.setPen(QPen(Qt::black, 4));
				painter.drawLine(centre, tip);
				painter.setPen(QPen(colors[axis], 2, Qt::DashLine));
				painter.drawLine(centre, tip);
				if (selected)
				{
					annotations << Annotation{tip, tr("Local %1").arg(QChar('X' + axis)), 2};
				}
			}
			painter.setPen(QPen(Qt::black, 2));
			painter.setBrush(selected ? accent : QColor(Qt::white));
			painter.drawPolygon(
				QPolygonF{centre + QPointF(0, -6), centre + QPointF(6, 0), centre + QPointF(0, 6), centre + QPointF(-6, 0)});
			annotations << Annotation{centre, tag.name, selected ? 0 : 1};
			occupied << QRectF(centre - QPointF(8, 8), QSizeF(16, 16));
		}
	}
	const auto handles = transformGizmoPoints();
	if (finite(handles[3]))
	{
		painter.setRenderHint(QPainter::Antialiasing, true);
		const QColor colors[]{QColor(255, 100, 100), QColor(100, 230, 130), QColor(115, 165, 255)};
		const auto rings = rotationGizmoRings();
		if (m_transformTool == ModelTransformTool::Rotate)
		{
			const bool freeRotation = m_editMoveActive && m_transformHandle == 3;
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(Qt::black, 4));
			painter.drawEllipse(trackballGizmoRect());
			painter.setPen(QPen(freeRotation ? accent : QColor(Qt::white), 2, Qt::DashLine));
			painter.drawEllipse(trackballGizmoRect());
			if (freeRotation)
			{
				// Keep the guide bounded even while a captured drag leaves the
				// viewport; the solver projects those positions onto the equator.
				auto offset = m_trackballPoint - m_trackballCentre;
				const double extent = std::max(std::abs(offset.x()), std::abs(offset.y()));
				if (extent > trackballRadius) offset *= trackballRadius / extent;
				const double length = std::hypot(offset.x(), offset.y());
				if (length > trackballRadius) offset *= trackballRadius / length;
				const auto end = m_trackballCentre + offset;
				painter.setPen(QPen(Qt::black, 4)); painter.drawLine(m_editMovePress, end);
				painter.setPen(QPen(accent, 2, Qt::DotLine)); painter.drawLine(m_editMovePress, end);
				painter.setBrush(accent); painter.drawEllipse(end, 4, 4);
			}
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			if (!finite(handles[axis]))
			{
				continue;
			}
			const auto color = m_editMoveActive && axis == m_transformHandle ? accent : colors[axis];
			for (int pass = 0; pass < 2; ++pass)
			{
				painter.setPen(QPen(pass == 0 ? QColor(Qt::black) : color, pass == 0 ? 5 : 2));
				if (m_transformTool == ModelTransformTool::Rotate)
				{
					for (int i = 1; i < rings[axis].size(); ++i)
					{
						if (finite(rings[axis][i - 1]) && finite(rings[axis][i]))
						{
							painter.drawLine(rings[axis][i - 1], rings[axis][i]);
						}
					}
				}
				else
				{
					painter.drawLine(handles[3], handles[axis]);
				}
			}
			const auto direction = handles[axis] - handles[3];
			const double length = std::sqrt(squared(direction));
			if (length < 1)
			{
				continue;
			}
			const auto unit = direction / length, side = QPointF(-unit.y(), unit.x());
			painter.setBrush(colors[axis]);
			painter.setPen(QPen(Qt::black, 1));
			if (m_transformTool == ModelTransformTool::Move)
			{
				painter.drawPolygon(QPolygonF{handles[axis], handles[axis] - unit * 10 + side * 4, handles[axis] - unit * 10 - side * 4});
			}
			else if (m_transformTool == ModelTransformTool::Scale)
			{
				painter.drawRect(QRectF(handles[axis] - QPointF(4, 4), QSizeF(8, 8)));
			}
			const auto label = !m_editCollision.isEmpty() && m_transformTool == ModelTransformTool::Scale
								   ? tr("Local %1").arg(QChar('X' + axis))
								   : QString(QChar('X' + axis));
			const auto at = handles[axis] + unit * 10;
			const QFontMetricsF metrics(painter.font());
			const QSizeF labelSize(metrics.horizontalAdvance(label) + 8, metrics.height() + 4);
			const QRectF bounds = QRectF(rect()).adjusted(3, 3, -3, -3);
			if (labelSize.width() > bounds.width() || labelSize.height() > bounds.height())
			{
				continue;
			}
			QRectF box;
			bool displaced = false;
			// Local-axis names and translated/scaled labels can be wider than
			// the fixed-size handles. Keep every label separate and inside the
			// viewport; connect displaced labels to their original handle.
			for (int attempt = 0; attempt < 24; ++attempt)
			{
				const auto offset = attempt < 8 ? unit * (attempt * (labelSize.height() + 8))
												: QPointF(0, ((attempt - 8) / 2 + 1) * (labelSize.height() + 8) * (attempt % 2 ? 1 : -1));
				const auto centre = at + offset;
				QRectF candidate(
					QPointF(std::clamp(centre.x() - labelSize.width() / 2, bounds.left(), bounds.right() - labelSize.width()),
							std::clamp(centre.y() - labelSize.height() / 2, bounds.top(), bounds.bottom() - labelSize.height())),
					labelSize);
				if (std::none_of(occupied.cbegin(), occupied.cend(), [&](const auto &other) { return other.intersects(candidate); }))
				{
					box = candidate;
					displaced = attempt != 0;
					break;
				}
			}
			if (box.isEmpty())
			{
				continue;
			}
			if (displaced)
			{
				painter.setPen(QPen(Qt::white, 1));
				painter.drawLine(handles[axis], QPointF(std::clamp(handles[axis].x(), box.left(), box.right()),
														std::clamp(handles[axis].y(), box.top(), box.bottom())));
			}
			occupied << box.adjusted(-3, -3, 3, 3);
			painter.fillRect(box, Qt::black);
			painter.setPen(Qt::white);
			painter.drawText(box, Qt::AlignCenter, label);
		}
		if (m_transformTool == ModelTransformTool::Rotate)
		{
			painter.setBrush(accent);
			painter.setPen(QPen(Qt::black, 4)); painter.drawEllipse(handles[3], 6, 6);
			painter.setPen(QPen(Qt::white, 1)); painter.drawEllipse(handles[3], 6, 6);
			occupied << QRectF(handles[3] - QPointF(9, 9), QSizeF(18, 18));
			annotations.append({handles[3], tr("Free"), 0});
		}
		else
		{
			painter.setBrush(accent);
			painter.setPen(QPen(Qt::white, 1));
			painter.drawRect(QRectF(handles[3].x() - 4, handles[3].y() - 4, 8, 8));
			if (m_transformTool == ModelTransformTool::Scale)
			{
				painter.setBrush(Qt::NoBrush);
				painter.drawRect(QRectF(handles[3].x() - 7, handles[3].y() - 7, 14, 14));
			}
		}
		if (!m_transformBasis.world && !m_transformAxesName.isEmpty())
			annotations.append({handles[3], m_editCollision.isEmpty() || m_transformTool != ModelTransformTool::Scale
				? m_transformAxesName : QCoreApplication::translate("VibeStudioModelViewport", "Box local"), 0});
	}
	// Reserve gizmo labels first, then place tag names and local-axis labels.
	// Co-located attachments remain distinguishable; names never overprint one
	// another. The component table always exposes full, selectable identities.
	std::stable_sort(annotations.begin(), annotations.end(), [](const auto &a, const auto &b) { return a.priority < b.priority; });
	for (const auto &annotation : annotations)
	{
		const auto name = painter.fontMetrics().elidedText(annotation.text, Qt::ElideMiddle, std::max(40, width() / 3));
		const QSizeF extent(painter.fontMetrics().horizontalAdvance(name) + 8, painter.fontMetrics().height() + 4);
		const QRectF bounds = QRectF(rect()).adjusted(3, 3, -3, -3);
		if (extent.width() > bounds.width() || extent.height() > bounds.height())
		{
			continue;
		}
		QRectF box;
		for (int attempt = 0; attempt < 64; ++attempt)
		{
			const int row = attempt / 4;
			const double x = annotation.anchor.x() + (attempt % 2 ? -extent.width() - 12 : 12);
			const double y =
				annotation.anchor.y() + (attempt % 4 < 2 ? 12 + row * (extent.height() + 4) : -12 - (row + 1) * (extent.height() + 4));
			QRectF candidate(QPointF(std::clamp(x, bounds.left(), bounds.right() - extent.width()),
									 std::clamp(y, bounds.top(), bounds.bottom() - extent.height())),
							 extent);
			if (std::none_of(occupied.cbegin(), occupied.cend(), [&](const auto &other) { return other.intersects(candidate); }))
			{
				box = candidate;
				break;
			}
		}
		if (box.isEmpty())
		{
			continue;
		}
		occupied << box.adjusted(-2, -2, 2, 2);
		painter.setPen(QPen(Qt::white, 1, Qt::DotLine));
		painter.drawLine(annotation.anchor, QPointF(std::clamp(annotation.anchor.x(), box.left(), box.right()),
													std::clamp(annotation.anchor.y(), box.top(), box.bottom())));
		painter.fillRect(box, Qt::black);
		painter.setPen(QPen(annotation.priority == 0 ? accent : QColor(Qt::white), annotation.priority == 0 ? 2 : 1));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(box);
		painter.setPen(Qt::white);
		painter.drawText(box, Qt::AlignCenter, name);
	}
	painter.restore();
}
} // namespace vibestudio
