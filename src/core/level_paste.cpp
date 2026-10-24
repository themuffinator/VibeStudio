/*
 Copyright (C) 2010 Kristian Duske

 The plane-corner placement below is adapted from TrenchBroom's Grid::
 moveDeltaForBounds and snapTowards, revision 4c756be1a54a28d921d18c77716a5f170bdc31d0.
 https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbMdlLib/src/Grid.cpp

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <https://www.gnu.org/licenses/>.
*/
#include "core/level_paste.h"
#include "core/level_visibility.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/map_geometry.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
using V = LevelMapVec3;
constexpr double worldLimit = 32768;
bool finite(V p)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
std::array<double, 3> xyz(V p)
{
	return {p.x, p.y, p.z};
}
V vec(const std::array<double, 3>& p)
{
	return {p[0], p[1], p[2], true};
}
bool bounded(V p)
{
	return finite(p) && std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) <= worldLimit;
}
double dot(V a, V b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
bool unit(V* p)
{
	if (!finite(*p)) {
		return false;
	}
	const double length = std::hypot(p->x, p->y, p->z);
	if (!std::isfinite(length) || length < 1e-12) {
		return false;
	}
	p->x /= length;
	p->y /= length;
	p->z /= length;
	// Exact orbit poles still acquire tiny sin/cos components. Treat those as
	// zero before choosing a box corner or a grid direction.
	if (std::abs(p->x) < 1e-12) { p->x = 0; }
	if (std::abs(p->y) < 1e-12) { p->y = 0; }
	if (std::abs(p->z) < 1e-12) { p->z = 0; }
	return true;
}
bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

// Convex half-space ray clipping. Source brushes only; models and patches do
// not obscure a brush hit, matching TB's brush-only paste target query.
bool brushTarget(const LevelMapDocument& document, const LevelPastePlacement& placement, V direction, V* point, V* normal)
{
	const auto hidden = levelMapHiddenObjects(document);
	double closest = std::numeric_limits<double>::infinity();
	for (const auto& brush : document.brushes) {
		detail::placementCancellationCheckpoint();
		if ((placement.filterBrushes && !placement.visibleBrushes.contains(brush.id)) ||
		    hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id}))) {
			continue;
		}
		if (brush.faces.size() < 4 || brush.faces.size() > 512) {
			continue;
		}
		double enter = -std::numeric_limits<double>::infinity(), leave = std::numeric_limits<double>::infinity();
		V entryNormal, exitNormal;
		bool valid = true;
		for (const auto& face : brush.faces) {
			detail::placementCancellationCheckpoint();
			const auto plane = planeFromPoints(face.p0, face.p1, face.p2, MapGeometryPrecision::PreserveCoordinates);
			if (!plane.valid) {
				valid = false;
				break;
			}
			const V n{plane.normalX, plane.normalY, plane.normalZ, true};
			const double distance = plane.distance - dot(n, placement.rayOrigin);
			const double speed = dot(n, direction);
			if (std::abs(speed) < 1e-12) {
				if (distance < -1e-7) {
					valid = false;
					break;
				}
			} else if (speed < 0) {
				const double t = distance / speed;
				if (t > enter) {
					enter = t;
					entryNormal = n;
				}
			} else {
				const double t = distance / speed;
				if (t < leave) {
					leave = t;
					exitNormal = n;
				}
			}
			if (leave < enter) {
				valid = false;
				break;
			}
		}
		const bool entry = !placement.rayForwardOnly || enter >= 0;
		const double t = entry ? enter : leave;
		if (valid && (!placement.rayForwardOnly || t >= 0) && t < closest && std::isfinite(t)) {
			closest = t;
			*point = {placement.rayOrigin.x + t * direction.x, placement.rayOrigin.y + t * direction.y,
			          placement.rayOrigin.z + t * direction.z, true};
			*normal = entry ? entryNormal : exitNormal;
		}
	}
	return std::isfinite(closest);
}
} // namespace

bool levelPasteBounds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects,
                      const QHash<QString, ResizeBox>& definitions, V* mins, V* maxs, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!mins || !maxs) {
		return fail(error, QCoreApplication::translate("LevelPaste", "Paste bounds need an output box."));
	}
	QSet<int> entities, brushOwners, brushes, patches;
	for (const auto& object : objects) {
		detail::placementCancellationCheckpoint();
		if (object.kind == LevelMapSelectionKind::Entity) {
			entities.insert(object.objectId);
		} else if (object.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes.insert(object.objectId);
		} else if (object.kind == LevelMapSelectionKind::QuakePatch) {
			patches.insert(object.objectId);
		}
	}
	QVector<LevelMapSelectionRef> primitives;
	for (const auto& brush : document.brushes) {
		brushOwners.insert(brush.entityId);
		if (brushes.contains(brush.id) || entities.contains(brush.entityId)) {
			primitives << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, brush.id};
		}
	}
	for (const auto& patch : document.patches) {
		brushOwners.insert(patch.entityId);
		if (patches.contains(patch.id) || entities.contains(patch.entityId)) {
			primitives << LevelMapSelectionRef{LevelMapSelectionKind::QuakePatch, patch.id};
		}
	}
	V low, high;
	bool haveBounds = !primitives.isEmpty() && levelMapObjectsBounds(document, primitives, &low, &high);
	const auto include = [&](V p) {
		if (!haveBounds) {
			low = high = p;
			haveBounds = true;
		} else {
			low.x = std::min(low.x, p.x);
			low.y = std::min(low.y, p.y);
			low.z = std::min(low.z, p.z);
			high.x = std::max(high.x, p.x);
			high.y = std::max(high.y, p.y);
			high.z = std::max(high.z, p.z);
		}
	};
	for (const auto& entity : document.entities) {
		detail::placementCancellationCheckpoint();
		if (!entities.contains(entity.id) || brushOwners.contains(entity.id) || entity.className == QLatin1String("worldspawn")) {
			continue;
		}
		if (!finite(entity.origin)) {
			return fail(error, QCoreApplication::translate("LevelPaste", "A pasted point entity has no finite origin."));
		}
		const auto box = definitions.value(entity.className.toCaseFolded(), ResizeBox{{-8, -8, -8}, {8, 8, 8}});
		const auto origin = xyz(entity.origin);
		for (int axis = 0; axis < 3; ++axis) {
			if (!std::isfinite(box.mins[axis]) || !std::isfinite(box.maxs[axis]) || box.mins[axis] > box.maxs[axis]) {
				return fail(error, QCoreApplication::translate("LevelPaste", "A point entity definition has invalid bounds."));
			}
		}
		include({origin[0] + box.mins[0], origin[1] + box.mins[1], origin[2] + box.mins[2], true});
		include({origin[0] + box.maxs[0], origin[1] + box.maxs[1], origin[2] + box.maxs[2], true});
	}
	if (!haveBounds || !bounded(low) || !bounded(high)) {
		return fail(error, QCoreApplication::translate("LevelPaste", "The clipboard has no finite bounds within the editable map range."));
	}
	*mins = low;
	*maxs = high;
	return true;
}

bool levelPasteOffset(const LevelMapDocument& destination, const LevelMapDocument& clipboard, const QVector<LevelMapSelectionRef>& objects,
                      const LevelPastePlacement& placement, V* offset, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!offset) {
		return fail(error, QCoreApplication::translate("LevelPaste", "Paste placement needs an output offset."));
	}
	if (placement.mode == LevelPasteMode::Offset) {
		if (!finite(placement.offset)) {
			return fail(error, QCoreApplication::translate("LevelPaste", "Paste placement needs a finite offset."));
		}
		*offset = placement.offset;
		return true;
	}
	if (placement.mode != LevelPasteMode::Camera && placement.mode != LevelPasteMode::Plane && placement.mode != LevelPasteMode::ViewRay) {
		return fail(error, QCoreApplication::translate("LevelPaste", "Unknown paste placement mode."));
	}
	if (!std::isfinite(placement.grid) || placement.grid < 0 || placement.grid > worldLimit) {
		return fail(error, QCoreApplication::translate("LevelPaste", "Paste placement needs a grid between 0 and 32,768 units."));
	}
	V low, high;
	if (!levelPasteBounds(clipboard, objects, placement.entityBounds, &low, &high, error)) {
		return false;
	}
	const auto a = xyz(low), b = xyz(high);
	std::array<double, 3> delta{};
	if (placement.mode == LevelPasteMode::Camera) {
		// NRC select.cpp::TranslateToCamera, revision 449778b3840f8f2062daab15c87cb012f2ac945c
		// (GPL-2.0-or-later): snap the translation, not the camera or box centre.
		if (!bounded(placement.point)) {
			return fail(error, QCoreApplication::translate(
			                       "LevelPaste", "Paste to Camera needs a finite camera position within the editable map range."));
		}
		const auto camera = xyz(placement.point);
		for (int axis = 0; axis < 3; ++axis) {
			delta[axis] = camera[axis] - (a[axis] + b[axis]) * 0.5;
			if (placement.grid > 0) {
				delta[axis] = snapLevelMapCoordinate(delta[axis], placement.grid);
			}
		}
	} else {
		auto point = placement.point, normal = placement.normal, direction = placement.direction;
		if (placement.referenceAxis < -1 || placement.referenceAxis > 2 ||
		    (placement.referenceAxis >= 0 && placement.mode != LevelPasteMode::Plane)) {
			return fail(error, QCoreApplication::translate("LevelPaste", "Paste reference depth needs a plan-view axis."));
		}
		if (placement.referenceAxis >= 0 && !destination.selection.isEmpty()) {
			V referenceLow, referenceHigh;
			if (!levelPasteBounds(destination, destination.selection, placement.entityBounds, &referenceLow, &referenceHigh, error)) {
				return false;
			}
			auto p = xyz(point);
			p[placement.referenceAxis] = placement.referenceAxis == 1 ? referenceHigh.y : xyz(referenceLow)[placement.referenceAxis];
			point = vec(p);
		}
		if (!unit(&direction)) {
			return fail(error, QCoreApplication::translate("LevelPaste", "Cursor paste needs a finite nonzero view ray."));
		}
		if (placement.mode == LevelPasteMode::ViewRay) {
			auto forward = placement.viewDirection;
			if (!bounded(placement.rayOrigin) || !unit(&forward) || dot(forward, direction) <= 1e-6) {
				return fail(error,
				            QCoreApplication::translate("LevelPaste", "Cursor paste needs a finite camera and a forward-facing view ray."));
			}
			if (!brushTarget(destination, placement, direction, &point, &normal)) {
				const double distance = placement.rayForwardOnly ? 256 : 0;
				point = {placement.rayOrigin.x + distance * direction.x, placement.rayOrigin.y + distance * direction.y,
				         placement.rayOrigin.z + distance * direction.z, true};
				if (placement.grid > 0) {
					point = snapLevelMapPosition(point, placement.grid);
				}
				normal = {-forward.x, -forward.y, -forward.z, true};
				// Snapping the fallback plane moves the ray intersection.
				const double t =
				    dot(normal, {point.x - placement.rayOrigin.x, point.y - placement.rayOrigin.y, point.z - placement.rayOrigin.z, true}) /
				    dot(normal, direction);
				point = {placement.rayOrigin.x + t * direction.x, placement.rayOrigin.y + t * direction.y,
				         placement.rayOrigin.z + t * direction.z, true};
			}
		}
		if (!bounded(point) || !unit(&normal) || std::abs(dot(normal, direction)) < 1e-9) {
			return fail(error,
			            QCoreApplication::translate("LevelPaste", "Cursor paste needs a finite target plane crossed by its view ray."));
		}
		const auto p = xyz(point), n = xyz(normal), d = xyz(direction);
		const int dominant =
		    int(std::max_element(n.begin(), n.end(), [](double x, double y) { return std::abs(x) < std::abs(y); }) - n.begin());
		const bool axial = std::abs(n[dominant]) >= 1 - 1e-10;
		for (int axis = 0; axis < 3; ++axis) {
			double corner = p[axis];
			if (placement.grid > 0 && !(axial && axis == dominant)) {
				const double cell = corner / placement.grid;
				corner = (d[axis] < 0 ? std::ceil(cell) : d[axis] > 0 ? std::floor(cell) : std::round(cell)) * placement.grid;
			}
			if (axis == dominant ? n[axis] < 0 : d[axis] < 0) {
				corner -= b[axis] - a[axis];
			}
			delta[axis] = corner - a[axis];
		}
	}
	for (int axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(delta[axis]) || std::abs(a[axis] + delta[axis]) > worldLimit || std::abs(b[axis] + delta[axis]) > worldLimit) {
			return fail(error, QCoreApplication::translate("LevelPaste", "Paste placement would leave the editable map range."));
		}
	}
	*offset = vec(delta);
	return true;
}

// NetRadiant Custom radiant/select.cpp MoveToCamera / TranslateToCamera at
// 449778b3 (GPL-2.0-or-later): Paste to Camera's snapped centre translation,
// applied to the selection in place. Original implementation.
bool levelMoveToCameraOffset(const LevelMapDocument& document, const LevelPastePlacement& placement, V* offset, QString* error)
{
	if (error) {
		error->clear();
	}
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error, QCoreApplication::translate("LevelPaste", "Move to Camera works on Quake-family .map files."));
	}
	if (document.selection.isEmpty()) {
		return fail(error, QCoreApplication::translate("LevelPaste", "Select objects to move to the camera."));
	}
	auto camera = placement;
	camera.mode = LevelPasteMode::Camera;
	if (!bounded(camera.point)) {
		return fail(error, QCoreApplication::translate("LevelPaste", "Move to Camera needs a finite camera position within the editable map range."));
	}
	return levelPasteOffset(document, document, document.selection, camera, offset, error);
}

bool moveLevelMapSelectionToCamera(LevelMapDocument* document, const LevelPastePlacement& placement, const LevelMapTextureLockOptions& textures,
                                   V* appliedOffset, QString* error)
{
	if (!document) {
		return fail(error, QCoreApplication::translate("LevelPaste", "No map is open."));
	}
	V offset;
	if (!levelMoveToCameraOffset(*document, placement, &offset, error)) {
		return false;
	}
	if (offset.x == 0 && offset.y == 0 && offset.z == 0) {
		return fail(error, QCoreApplication::translate("LevelPaste", "The selection is already centred on the camera at this grid size."));
	}
	if (!moveLevelMapSelection(document, offset.x, offset.y, offset.z, textures, error)) {
		return false;
	}
	if (appliedOffset) {
		*appliedOffset = offset;
	}
	return true;
}
} // namespace vibestudio
