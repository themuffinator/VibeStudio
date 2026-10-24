#include "core/level_region_selection.h"
#include "core/level_visibility.h"
#include "core/level_history_p.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/map_geometry.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelRegionSelection) };
using Vec = std::array<double, 3>;
constexpr double tolerance = 1e-6;
Vec vec(const LevelMapVec3& p) { return {p.x, p.y, p.z}; }
Vec subtract(const Vec& a, const Vec& b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
double dot(const Vec& a, const Vec& b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
Vec cross(const Vec& a, const Vec& b) { return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]}; }
bool finite(const Vec& p) { return std::all_of(p.cbegin(), p.cend(), [](double n) { return std::isfinite(n); }); }
bool normalise(Vec* p) {
	const double length = std::sqrt(dot(*p, *p));
	if (length < 1e-10 || !std::isfinite(length)) { return false; }
	for (auto& n : *p) { n /= length; } return true;
}
struct Bounds {
	Vec low{}, high{};
	bool valid = false;
	void add(const Vec& p) {
		if (!finite(p)) { return; }
		if (!valid) { low = high = p; valid = true; return; }
		for (int i = 0; i < 3; ++i) { low[i] = std::min(low[i], p[i]); high[i] = std::max(high[i], p[i]); }
	}
	void add(const Bounds& other) { if (other.valid) { add(other.low); add(other.high); } }
};
bool fits(const Bounds& query, const Bounds& object, bool whole, int skip) {
	if (!query.valid || !object.valid) { return false; }
	for (int i = 0; i < 3; ++i) {
		if (i == skip) { continue; }
		if (whole ? object.low[i] < query.low[i]-tolerance || object.high[i] > query.high[i]+tolerance
			: object.low[i] > query.high[i]+tolerance || object.high[i] < query.low[i]-tolerance) { return false; }
	}
	return true;
}
struct Plane { Vec normal; double distance = 0; };
struct Shape {
	Bounds bounds;
	QVector<Vec> points, edges;
	QVector<Plane> planes;
};
void addDirection(QVector<Vec>* directions, Vec direction) {
	if (!normalise(&direction)) { return; }
	for (const auto& existing : *directions) { const auto c = cross(direction,existing); if (dot(c,c) < 1e-24) { return; } }
	directions->append(direction);
}
Shape boxShape(const Bounds& bounds) {
	Shape shape; shape.bounds = bounds;
	if (!bounds.valid) { return shape; }
	for (int corner = 0; corner < 8; ++corner) {
		shape.points.append(Vec{corner&1 ? bounds.high[0] : bounds.low[0], corner&2 ? bounds.high[1] : bounds.low[1], corner&4 ? bounds.high[2] : bounds.low[2]});
	}
	for (int i = 0; i < 3; ++i) {
		Vec axis{}; axis[i] = 1; shape.edges.append(axis); shape.planes.append({axis, bounds.high[i]});
		axis[i] = -1; shape.planes.append({axis, -bounds.low[i]});
	}
	return shape;
}
Shape brushShape(const LevelMapBrush& brush) {
	const auto geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId, MapGeometryPrecision::PreserveCoordinates,
		[] { detail::placementCancellationCheckpoint(); return false; });
	Shape shape;
	if (!geometry.solved) { return shape; }
	for (const auto& face : geometry.faces) {
		detail::placementCancellationCheckpoint();
		if (!face.isValid()) { continue; }
		shape.planes.append({{face.plane.normalX, face.plane.normalY, face.plane.normalZ}, face.plane.distance});
		for (qsizetype i = 0; i < face.points.size(); ++i) {
			const auto p = vec(face.points[i]); shape.bounds.add(p);
			// Face-corner duplicates are harmless to SAT extrema and avoid a
			// quadratic weld pass on high-sided brushes.
			shape.points.append(p); addDirection(&shape.edges, subtract(vec(face.points[(i+1)%face.points.size()]), p));
		}
	}
	return shape;
}
// Original projected convex hull and SAT implementation. Behaviour references:
// TB v2026.2 Map_Selection.cpp / ModelUtils.cpp / BrushNode.cpp / PatchNode.cpp;
// NRC 449778b select.cpp. See docs/CREDITS.md for links and licences.
Shape projectShape(const Shape& source, int axis) {
	const int u = (axis+1)%3, v = (axis+2)%3;
	auto points = source.points;
	for (auto& p : points) { p[axis] = 0; }
	std::sort(points.begin(), points.end(), [u,v](const auto& a, const auto& b) { return a[u] < b[u] || (a[u] == b[u] && a[v] < b[v]); });
	points.erase(std::unique(points.begin(), points.end()), points.end());
	if (points.size() < 3) { return {}; }
	const auto turn = [u,v](const Vec& a, const Vec& b, const Vec& c) { return (b[u]-a[u])*(c[v]-a[v]) - (b[v]-a[v])*(c[u]-a[u]); };
	QVector<Vec> hull;
	for (const auto& p : points) {
		while (hull.size() >= 2 && turn(hull[hull.size()-2], hull.last(), p) <= 0) { hull.removeLast(); }
		hull.append(p);
	}
	const auto lower = hull.size();
	for (qsizetype i = points.size()-2; i >= 0; --i) {
		while (hull.size() > lower && turn(hull[hull.size()-2], hull.last(), points[i]) <= 0) { hull.removeLast(); }
		hull.append(points[i]);
	}
	hull.removeLast();
	Shape result; result.points = hull;
	for (qsizetype i = 0; i < hull.size(); ++i) {
		result.bounds.add(hull[i]); const auto edge = subtract(hull[(i+1)%hull.size()], hull[i]);
		Vec normal{}; normal[u] = edge[v]; normal[v] = -edge[u];
		if (normalise(&normal)) { result.planes.append({normal, dot(normal, hull[i])}); }
		addDirection(&result.edges, edge);
	}
	return result;
}
bool contains(const Shape& query, const Vec& p) {
	for (const auto& plane : query.planes) { if (dot(plane.normal, p) > plane.distance+tolerance) { return false; } }
	return !query.planes.isEmpty();
}
bool matchesShape(const Shape& query, const Shape& target, bool whole, bool tall) {
	if (query.points.isEmpty() || target.points.isEmpty()) { return false; }
	if (whole) {
		for (const auto& p : target.points) { detail::placementCancellationCheckpoint(); if (!contains(query, p)) { return false; } }
		return true;
	}
	const auto separates = [&](Vec axis) {
		detail::placementCancellationCheckpoint();
		if (!normalise(&axis)) { return false; }
		double aLow = std::numeric_limits<double>::infinity(), aHigh = -aLow, bLow = aLow, bHigh = -aLow;
		for (const auto& p : query.points) { const auto d = dot(axis,p); aLow = std::min(aLow,d); aHigh = std::max(aHigh,d); }
		for (const auto& p : target.points) { const auto d = dot(axis,p); bLow = std::min(bLow,d); bHigh = std::max(bHigh,d); }
		return aLow > bHigh+tolerance || bLow > aHigh+tolerance;
	};
	for (const auto& plane : query.planes) { if (separates(plane.normal)) { return false; } }
	for (const auto& plane : target.planes) { if (separates(plane.normal)) { return false; } }
	if (!tall) {
		for (const auto& a : query.edges) { for (const auto& b : target.edges) { if (separates(cross(a,b))) { return false; } } }
	}
	return true;
}
bool segmentTouches(const Shape& query, const Vec& a, const Vec& b) {
	double enter = 0, leave = 1;
	for (const auto& plane : query.planes) {
		const auto start = dot(plane.normal,a)-plane.distance, end = dot(plane.normal,b)-plane.distance;
		if (start > tolerance && end > tolerance) { return false; }
		if (start <= tolerance && end <= tolerance) { continue; }
		const double t = (tolerance-start)/(end-start);
		if (start > tolerance) { enter = std::max(enter,t); } else { leave = std::min(leave,t); }
		if (enter > leave) { return false; }
	}
	return true;
}
using Grid = QVector<QVector<LevelMapVec3>>;
bool matchesPatch(const Shape& query, const Grid& grid, bool whole, int skip) {
	if (grid.isEmpty()) { return false; }
	const auto point = [skip](const LevelMapVec3& value) { auto p = vec(value); if (skip >= 0) { p[skip] = 0; } return p; };
	for (qsizetype row = 0; row < grid.size(); ++row) {
		detail::placementCancellationCheckpoint();
		for (qsizetype col = 0; col < grid[row].size(); ++col) {
			const auto p = point(grid[row][col]); const bool inside = contains(query,p);
			if (whole && !inside) { return false; }
			if (!whole && (inside || (row > 0 && segmentTouches(query,point(grid[row-1][col]),p))
				|| (col > 0 && segmentTouches(query,point(grid[row][col-1]),p)))) { return true; }
		}
	}
	return whole;
}
struct Object {
	LevelMapSelectionRef ref;
	Bounds bounds;
	Shape shape;
	Grid grid;
	int owner = -1;
};
} // namespace

bool selectLevelMapByRegion(LevelMapDocument* document, const LevelRegionSelectionRequest& request,
	LevelRegionSelectionReport* report, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || document->selection.isEmpty() || request.axis < 0 || request.axis > 2
		|| request.mode < LevelMapRegionSelection::Inside || request.mode > LevelMapRegionSelection::PartialTall
		|| request.policy < LevelRegionSelectionPolicy::Bounds || request.policy > LevelRegionSelectionPolicy::Radiant) {
		return fail(Text::tr("Choose selected objects, a region mode and an axis of X, Y or Z."));
	}
	const bool native = request.policy != LevelRegionSelectionPolicy::Bounds;
	const bool convex = request.policy == LevelRegionSelectionPolicy::TrenchBroom;
	const bool tall = request.mode == LevelMapRegionSelection::CompleteTall || request.mode == LevelMapRegionSelection::PartialTall;
	const bool whole = request.mode == LevelMapRegionSelection::Inside || request.mode == LevelMapRegionSelection::CompleteTall;
	const bool consume = convex || (request.policy == LevelRegionSelectionPolicy::Radiant && whole);
	const int skip = tall ? request.axis : -1;
	if (native && document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map) {
		return fail(Text::tr("Brush-volume selection requires a Quake, Quake II or Quake III text map."));
	}
	const auto context = levelSceneSelectionContext(*document);
	auto excluded = levelMapHiddenObjects(*document) | levelSceneLockedObjects(*document) | request.excludedObjects;
	QSet<QString> excludedGroups;
	for (const auto& key : excluded) {
		const auto group = context.closedGroups.value(key); if (!group.isEmpty()) { excludedGroups.insert(group); }
	}
	QHash<QString,Object> objects;
	QStringList order;
	QSet<int> owners, world;
	for (const auto& entity : document->entities) { if (entity.className.compare("worldspawn",Qt::CaseInsensitive) == 0) { world.insert(entity.id); } }
	const auto append = [&](Object object) { const auto key = levelMapSelectionRefId(object.ref); order.append(key); objects.insert(key,std::move(object)); };
	qint64 completed = 0;
	const qint64 total = document->brushes.size()+document->patches.size()+document->entities.size()+document->doomThings.size();
	for (const auto& brush : document->brushes) {
		detail::placementCheckpoint(LevelPlacementPhase::Selecting, completed++, total);
		Object object; object.ref = {LevelMapSelectionKind::QuakeBrush,brush.id}; object.owner = brush.entityId;
		if (convex) { object.shape = brushShape(brush); object.bounds = object.shape.bounds; }
		else if (brush.boundsSolved && brush.mins.valid && brush.maxs.valid) { object.bounds.add(vec(brush.mins)); object.bounds.add(vec(brush.maxs)); }
		owners.insert(brush.entityId); append(std::move(object));
	}
	for (const auto& patch : document->patches) {
		detail::placementCheckpoint(LevelPlacementPhase::Selecting, completed++, total);
		Object object; object.ref = {LevelMapSelectionKind::QuakePatch,patch.id}; object.owner = patch.entityId;
		if (convex) {
			// TB evaluates each quadratic surface with 2^3 subdivisions. Its
			// logical patch grid is independent of compiler fixed subdivisions.
			auto sampled = patch; sampled.fixedSubdivisions = false;
			object.grid = tessellatePatchMesh(sampled,8);
			for (const auto& row : object.grid) { for (const auto& p : row) { object.bounds.add(vec(p)); } }
		} else if (patch.mins.valid && patch.maxs.valid) { object.bounds.add(vec(patch.mins)); object.bounds.add(vec(patch.maxs)); }
		owners.insert(patch.entityId); append(std::move(object));
	}
	QHash<int,Bounds> ownerBounds;
	for (const auto& object : std::as_const(objects)) { ownerBounds[object.owner].add(object.bounds); }
	for (const auto& entity : document->entities) {
		detail::placementCheckpoint(LevelPlacementPhase::Selecting, completed++, total);
		if (world.contains(entity.id)) { continue; }
		Object object; object.ref = {LevelMapSelectionKind::Entity,entity.id};
		if (owners.contains(entity.id)) { object.bounds = ownerBounds.value(entity.id); }
		else if (native) {
			const auto origin = entity.origin.valid ? vec(entity.origin) : Vec{};
			const auto box = request.entityBounds.value(entity.className.toCaseFolded(),{{-8,-8,-8},{8,8,8}});
			Vec low{}, high{};
			for (int i = 0; i < 3; ++i) { low[i] = origin[i]+box.mins[i]; high[i] = origin[i]+box.maxs[i]; }
			if (!finite(low) || !finite(high) || low[0] > high[0] || low[1] > high[1] || low[2] > high[2]) {
				return fail(Text::tr("An entity definition has invalid selection bounds: %1.").arg(entity.className));
			}
			object.bounds.add(low); object.bounds.add(high);
		} else if (entity.origin.valid) { object.bounds.add(vec(entity.origin)); }
		append(std::move(object));
	}
	if (document->format == LevelMapFormat::DoomWad) {
		for (const auto& thing : document->doomThings) {
			detail::placementCheckpoint(LevelPlacementPhase::Selecting, completed++, total);
			Object object; object.ref = {LevelMapSelectionKind::DoomThing,thing.id}; object.bounds.add(Vec{thing.x,thing.y,0}); append(std::move(object));
		}
	}
	QSet<QString> selected;
	QSet<int> selectedOwners;
	QVector<Shape> queries;
	Bounds combined;
	for (const auto& ref : document->selection) {
		const auto key = levelMapSelectionRefId(ref); const auto found = objects.constFind(key);
		if ((native && (found == objects.cend() || !found->bounds.valid)) || !context.allowedObjects.contains(key) || excluded.contains(key)) {
			return fail(Text::tr("A selection volume is missing, hidden, locked, outside the open group or has invalid bounds: %1.").arg(key));
		}
		if (native && !context.closedGroups.value(key).isEmpty()) { return fail(Text::tr("Open the group before selecting its brushes as volumes.")); }
		if (convex && ref.kind != LevelMapSelectionKind::QuakeBrush) { return fail(Text::tr("TrenchBroom volume selection requires only brushes.")); }
		selected.insert(key); if (ref.kind == LevelMapSelectionKind::Entity) { selectedOwners.insert(ref.objectId); }
		if (!native) { continue; }
		if (native && ref.kind == LevelMapSelectionKind::QuakeBrush) {
			auto query = convex ? found->shape : boxShape(found->bounds);
			queries.append(convex && tall ? projectShape(query,request.axis) : std::move(query));
		}
	}
	if (!native) {
		LevelMapVec3 low, high;
		if (!levelMapSelectionBounds(*document,&low,&high)) { return fail(Text::tr("Select objects with bounds to use as the region.")); }
		combined = {}; combined.add(vec(low)); combined.add(vec(high)); queries.append(boxShape(combined));
	}
	if (queries.isEmpty()) { return fail(Text::tr("Select at least one brush to use as a selection volume.")); }
	LevelRegionSelectionReport prepared;
	QSet<QString> visited, matched;
	for (const auto& key : std::as_const(order)) {
		detail::placementCheckpoint(LevelPlacementPhase::Selecting, completed++, total+order.size());
		const auto& object = objects[key];
		if (selected.contains(key) || selectedOwners.contains(object.owner) || !context.allowedObjects.contains(key) || excluded.contains(key)) { continue; }
		if (object.ref.kind == LevelMapSelectionKind::Entity && (owners.contains(object.ref.objectId) || document->format == LevelMapFormat::DoomWad)) { continue; }
		const auto group = context.closedGroups.value(key);
		const auto unit = group.isEmpty() ? key : group;
		if (visited.contains(unit)) { continue; } visited.insert(unit);
		const auto members = group.isEmpty() ? QVector<LevelMapSelectionRef>{object.ref} : context.groupSelections.value(group);
		Bounds bounds;
		bool eligible = !members.isEmpty();
		for (const auto& member : members) {
			const auto id = levelMapSelectionRefId(member); const auto memberObject = objects.constFind(id);
			if (memberObject == objects.cend() || !memberObject->bounds.valid || excluded.contains(id) || selected.contains(id)) { eligible = false; break; }
			bounds.add(memberObject->bounds);
		}
		// A partially hidden/locked closed group is never promoted by a hit
		// on a visible child; all descendants must be selectable together.
		if (excludedGroups.contains(group)) { eligible = false; }
		if (!eligible) { continue; }
		Shape target;
		if (convex && (group.size() || object.grid.isEmpty())) {
			target = group.isEmpty() && !object.shape.points.isEmpty() ? object.shape : boxShape(bounds);
			if (tall) { target = projectShape(target,request.axis); }
		}
		for (const auto& query : queries) {
			detail::placementCancellationCheckpoint();
			if (!fits(query.bounds,bounds,whole,skip)) { continue; }
			const bool hit = !convex || (!group.isEmpty() || object.grid.isEmpty() ? matchesShape(query,target,whole,tall) : matchesPatch(query,object.grid,whole,skip));
			if (hit) {
				for (const auto& member : members) {
					const auto id = levelMapSelectionRefId(member); if (!matched.contains(id)) { matched.insert(id); prepared.matches.append(member); }
				}
				break;
			}
		}
	}

	auto candidate = *document;
	{
		detail::LevelMapHistoryScope history(candidate);
		if (consume) {
			detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
			if (!deleteLevelMapSelection(&candidate,error)) { return false; }
			prepared.removedObjects = static_cast<int>(document->selection.size());
			// Linked deletion can remove additional copies. Never publish a stale
			// selected ID for a target that disappeared with a linked source.
			const auto surviving = levelSceneSelectionContext(candidate).allowedObjects;
			prepared.matches.removeIf([&](const auto& ref) { return !surviving.contains(levelMapSelectionRefId(ref)); });
		}
		auto selection = prepared.matches;
		// Preserve the default profile's existing empty-query behaviour.
		if (!native && selection.isEmpty()) { selection = document->selection; }
		if (request.policy == LevelRegionSelectionPolicy::Radiant && !consume) {
			selection = document->selection; for (const auto& ref : prepared.matches) { if (!selection.contains(ref)) { selection.append(ref); } }
		}
		if (!setLevelMapSelection(&candidate,selection,error)) { return false; }
		if (consume) {
			if (candidate.undoStack.isEmpty()) { return fail(Text::tr("The selection-volume deletion did not record an undo step.")); }
			auto& command = candidate.undoStack.last();
			command.hasSelectionSnapshot = true; command.selectionSnapshot = document->selection; command.selectionResult = selection;
			command.description = Text::tr("Select by brush volume"); command.undoDescription = Text::tr("Restore selection volumes and selection");
		}
	}
	detail::placementCancellationCheckpoint();
	*document = std::move(candidate);
	if (report) { *report = std::move(prepared); }
	return true;
}
} // namespace vibestudio
