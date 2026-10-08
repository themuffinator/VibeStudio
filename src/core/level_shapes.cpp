#include "core/level_shapes.h"

#include "core/level_primitive.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio {

namespace {

struct ShapeInfo {
	const char* id;
	const char* label;
	const char* description;
	const char* icon;
	bool compound;
	const char* parameters;
};

// The shapes in the order the Shapes tab shows them.
const ShapeInfo kShapes[] = {
	{"box", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Box"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "One six-sided brush filling the box."), "cube", false, ""},
	{"wedge", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Wedge"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "One brush sloping up across the box, for ramps and roofs."), "shape-wedge", false, "axis"},
	{"cylinder", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Cylinder"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "One brush with round sides along the depth axis, for pillars and pipes."), "shape-cylinder", false, "axis,sides"},
	{"cone", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Cone"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "One brush narrowing to a point along the depth axis, for spikes and spires."), "shape-cone", false, "axis,sides"},
	{"sphere", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Sphere"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "One brush rounded on every side, with its poles on the depth axis."), "shape-sphere", false, "axis,sides,bands"},
	{"arch", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Arch"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Wall segments following a curve, one brush each, for doorways, bridges and curved walls."), "shape-arch", true,
		"axis,sides,thickness,arc,start"},
	{"ring", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Ring"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "A full circle of wall segments, for towers, wells and round rooms."), "shape-ring", true, "axis,sides,thickness"},
	{"stairs", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Stairs"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Solid steps climbing across the box, one brush each."), "shape-stairs", true, "steps,rise"},
	{"room", QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Room"),
		QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Six brushes walling in the box with its inside left hollow."), "shape-room", true, "thickness"},
};

const ShapeInfo* shapeInfo(const QString& shape)
{
	for (const ShapeInfo& info : kShapes) {
		if (shape == QLatin1String(info.id)) {
			return &info;
		}
	}
	return nullptr;
}

bool fail(QString* error, const char* message)
{
	if (error) {
		*error = QCoreApplication::translate("VibeStudioLevelShapes", message);
	}
	return false;
}

double component(const LevelMapVec3& point, int axis)
{
	return axis == 0 ? point.x : (axis == 1 ? point.y : point.z);
}

// Whole units, so the brushes of one shape meet exactly; never -0.
double whole(double value)
{
	return std::round(value) + 0.0;
}

// The axes across a depth axis, as that view's right and up.
int rightAxis(int axis)
{
	return axis == 0 ? 1 : 0;
}

int upAxis(int axis)
{
	return axis == 2 ? 1 : 2;
}

LevelMapVec3 pointAt(int axis, double right, double up, double depth)
{
	double coordinates[3] = {0.0, 0.0, 0.0};
	coordinates[rightAxis(axis)] = whole(right);
	coordinates[upAxis(axis)] = whole(up);
	coordinates[axis] = whole(depth);
	return {coordinates[0], coordinates[1], coordinates[2], true};
}

QVector<LevelMapVec3> boxPoints(double x0, double y0, double z0, double x1, double y1, double z1)
{
	QVector<LevelMapVec3> points;
	for (const double x : {x0, x1}) {
		for (const double y : {y0, y1}) {
			for (const double z : {z0, z1}) {
				points.push_back({whole(x), whole(y), whole(z), true});
			}
		}
	}
	return points;
}

void appendUnique(QVector<LevelMapVec3>* points, const LevelMapVec3& point)
{
	for (const LevelMapVec3& existing : std::as_const(*points)) {
		if (existing.x == point.x && existing.y == point.y && existing.z == point.z) {
			return;
		}
	}
	points->push_back(point);
}

bool validMaterial(const QString& texture)
{
	const QString name = texture.trimmed();
	return !name.isEmpty() && name.size() <= 1024 && !name.contains(QStringLiteral("//")) && !name.contains(QStringLiteral("/*"))
		&& !name.contains(QStringLiteral("*/")) && std::none_of(name.cbegin(), name.cend(), [](QChar c) {
			   return c.isSpace() || c.isNull() || c.category() == QChar::Other_Control || QStringLiteral("\"{}()[]\\").contains(c);
		   });
}

bool arcHulls(const LevelShapeRequest& request, QVector<QVector<LevelMapVec3>>* hulls, QString* error)
{
	const bool ring = request.shape == QStringLiteral("ring");
	const int segments = request.sides;
	if (segments < (ring ? 3 : 1) || segments > 64) {
		return fail(error, ring ? QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "A ring needs 3 to 64 segments.")
								: QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "An arch needs 1 to 64 segments."));
	}
	const double sweep = ring ? 360.0 : request.arc;
	if (!std::isfinite(sweep) || sweep < 1.0 || sweep > 360.0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "An arch sweeps between 1 and 360 degrees."));
	}
	if (sweep / segments >= 180.0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Each segment must turn less than 180 degrees; use more segments."));
	}
	const double start = ring ? 0.0 : request.startAngle;
	if (!std::isfinite(start) || std::abs(start) > 360.0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Start the arch between -360 and 360 degrees."));
	}
	if (!std::isfinite(request.thickness) || request.thickness < 1.0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Walls need to be at least one unit thick."));
	}
	QVector<double> cosines;
	QVector<double> sines;
	for (int step = 0; step <= segments; ++step) {
		const double angle = (start + sweep * step / segments) * std::numbers::pi / 180.0;
		cosines.push_back(std::cos(angle));
		sines.push_back(std::sin(angle));
	}
	// A full circle closes on its first corner exactly.
	if (sweep >= 360.0) {
		cosines.back() = cosines.front();
		sines.back() = sines.front();
	}
	// The outer curve fills the box, so the box drawn is the shape's extent.
	const auto [lowCos, highCos] = std::minmax_element(cosines.cbegin(), cosines.cend());
	const auto [lowSin, highSin] = std::minmax_element(sines.cbegin(), sines.cend());
	if (*highCos - *lowCos < 1e-3 || *highSin - *lowSin < 1e-3) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "The arch is too narrow to fill the box; widen its sweep."));
	}
	const int axis = request.axis;
	const double right0 = component(request.mins, rightAxis(axis));
	const double right1 = component(request.maxs, rightAxis(axis));
	const double up0 = component(request.mins, upAxis(axis));
	const double up1 = component(request.maxs, upAxis(axis));
	const double depth0 = component(request.mins, axis);
	const double depth1 = component(request.maxs, axis);
	const double radiusRight = (right1 - right0) / (*highCos - *lowCos);
	const double radiusUp = (up1 - up0) / (*highSin - *lowSin);
	const double centreRight = right0 - *lowCos * radiusRight;
	const double centreUp = up0 - *lowSin * radiusUp;
	const double innerRight = radiusRight - request.thickness;
	const double innerUp = radiusUp - request.thickness;
	// Walls as thick as the radius meet in the middle, as slices of a pie.
	const bool solid = innerRight < 1.0 || innerUp < 1.0;
	for (int segment = 0; segment < segments; ++segment) {
		QVector<LevelMapVec3> points;
		for (const double depth : {depth0, depth1}) {
			for (const int corner : {segment, segment + 1}) {
				appendUnique(&points, pointAt(axis, centreRight + radiusRight * cosines.at(corner), centreUp + radiusUp * sines.at(corner), depth));
			}
			for (const int corner : {segment + 1, segment}) {
				appendUnique(&points, solid ? pointAt(axis, centreRight, centreUp, depth)
											: pointAt(axis, centreRight + innerRight * cosines.at(corner), centreUp + innerUp * sines.at(corner), depth));
			}
		}
		hulls->push_back(points);
	}
	return true;
}

bool stairHulls(const LevelShapeRequest& request, QVector<QVector<LevelMapVec3>>* hulls, QString* error)
{
	if (request.steps < 2 || request.steps > 64) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Stairs need 2 to 64 steps."));
	}
	if (!levelShapeRiseIds().contains(request.rise)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Stairs climb towards auto, +x, -x, +y or -y."));
	}
	const double x0 = whole(request.mins.x);
	const double y0 = whole(request.mins.y);
	const double z0 = whole(request.mins.z);
	const double x1 = whole(request.maxs.x);
	const double y1 = whole(request.maxs.y);
	const double z1 = whole(request.maxs.z);
	const bool alongX = request.rise == QStringLiteral("auto") ? x1 - x0 >= y1 - y0 : request.rise.endsWith(QLatin1Char('x'));
	const bool backwards = request.rise.startsWith(QLatin1Char('-'));
	const double run0 = alongX ? x0 : y0;
	const double run1 = alongX ? x1 : y1;
	const int steps = request.steps;
	if (run1 - run0 < steps || z1 - z0 < steps) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Each step needs at least one unit of run and of rise; use fewer steps or a bigger box."));
	}
	for (int step = 0; step < steps; ++step) {
		const double near = backwards ? whole(run1 - (run1 - run0) * (step + 1) / steps) : whole(run0 + (run1 - run0) * step / steps);
		const double far = backwards ? whole(run1 - (run1 - run0) * step / steps) : whole(run0 + (run1 - run0) * (step + 1) / steps);
		const double top = whole(z0 + (z1 - z0) * (step + 1) / steps);
		hulls->push_back(alongX ? boxPoints(near, y0, z0, far, y1, top) : boxPoints(x0, near, z0, x1, far, top));
	}
	return true;
}

bool roomHulls(const LevelShapeRequest& request, QVector<QVector<LevelMapVec3>>* hulls, QString* error)
{
	const double wall = whole(request.thickness);
	const double x0 = whole(request.mins.x);
	const double y0 = whole(request.mins.y);
	const double z0 = whole(request.mins.z);
	const double x1 = whole(request.maxs.x);
	const double y1 = whole(request.maxs.y);
	const double z1 = whole(request.maxs.z);
	if (!std::isfinite(request.thickness) || wall < 1.0) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Walls need to be at least one unit thick."));
	}
	if (x1 - x0 < 2 * wall + 1 || y1 - y0 < 2 * wall + 1 || z1 - z0 < 2 * wall + 1) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "The room is too small for walls that thick; leave at least one unit inside."));
	}
	// Floor and ceiling span the box; the walls stand between them, the
	// east and west ones full length, so no two brushes overlap.
	hulls->push_back(boxPoints(x0, y0, z0, x1, y1, z0 + wall));
	hulls->push_back(boxPoints(x0, y0, z1 - wall, x1, y1, z1));
	hulls->push_back(boxPoints(x0, y0, z0 + wall, x0 + wall, y1, z1 - wall));
	hulls->push_back(boxPoints(x1 - wall, y0, z0 + wall, x1, y1, z1 - wall));
	hulls->push_back(boxPoints(x0 + wall, y0, z0 + wall, x1 - wall, y0 + wall, z1 - wall));
	hulls->push_back(boxPoints(x0 + wall, y1 - wall, z0 + wall, x1 - wall, y1, z1 - wall));
	return true;
}

} // namespace

QStringList levelShapeIds()
{
	QStringList ids;
	for (const ShapeInfo& info : kShapes) {
		ids.push_back(QString::fromLatin1(info.id));
	}
	return ids;
}

bool isLevelShape(const QString& shape)
{
	return shapeInfo(shape) != nullptr;
}

QString levelShapeLabel(const QString& shape)
{
	const ShapeInfo* info = shapeInfo(shape);
	return info ? QCoreApplication::translate("VibeStudioLevelShapes", info->label) : shape;
}

QString levelShapeDescription(const QString& shape)
{
	const ShapeInfo* info = shapeInfo(shape);
	return info ? QCoreApplication::translate("VibeStudioLevelShapes", info->description) : QString();
}

QString levelShapeIconName(const QString& shape)
{
	const ShapeInfo* info = shapeInfo(shape);
	return info ? QString::fromLatin1(info->icon) : QStringLiteral("cube");
}

bool levelShapeIsCompound(const QString& shape)
{
	const ShapeInfo* info = shapeInfo(shape);
	return info && info->compound;
}

QStringList levelShapeParameters(const QString& shape)
{
	const ShapeInfo* info = shapeInfo(shape);
	return info ? QString::fromLatin1(info->parameters).split(QLatin1Char(','), Qt::SkipEmptyParts) : QStringList();
}

QStringList levelShapeRiseIds()
{
	return {QStringLiteral("auto"), QStringLiteral("+x"), QStringLiteral("-x"), QStringLiteral("+y"), QStringLiteral("-y")};
}

bool levelShapeHulls(const LevelShapeRequest& request, QVector<QVector<LevelMapVec3>>* hulls, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!hulls) {
		return false;
	}
	hulls->clear();
	if (!levelShapeIsCompound(request.shape)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Arches, rings, stairs and rooms are the shapes made of several brushes."));
	}
	const auto finite = [](const LevelMapVec3& p) {
		return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) <= 32768;
	};
	if (!finite(request.mins) || !finite(request.maxs) || request.maxs.x - request.mins.x < 1 || request.maxs.y - request.mins.y < 1
		|| request.maxs.z - request.mins.z < 1) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Use finite bounds within ±32768 units, with at least one unit of size on every axis."));
	}
	if (request.axis < 0 || request.axis > 2) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "The shape's axis must be X, Y or Z."));
	}
	if (!validMaterial(request.texture)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Use a material path without spaces, control characters, quotes, brackets, backslashes or comment markers."));
	}
	bool built = false;
	if (request.shape == QStringLiteral("arch") || request.shape == QStringLiteral("ring")) {
		built = arcHulls(request, hulls, error);
	} else if (request.shape == QStringLiteral("stairs")) {
		built = stairHulls(request, hulls, error);
	} else {
		built = roomHulls(request, hulls, error);
	}
	if (!built) {
		hulls->clear();
	}
	return built;
}

bool addLevelMapShape(LevelMapDocument* document, const LevelShapeRequest& request, QVector<int>* brushIds, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!isLevelShape(request.shape)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Choose box, wedge, cylinder, cone, sphere, arch, ring, stairs or room."));
	}
	if (!levelShapeIsCompound(request.shape)) {
		LevelBrushPrimitiveRequest primitive;
		primitive.shape = request.shape;
		primitive.mins = request.mins;
		primitive.maxs = request.maxs;
		primitive.axis = request.axis;
		primitive.sides = request.sides;
		primitive.bands = request.bands;
		primitive.texture = request.texture;
		int brushId = -1;
		if (!addLevelMapBrushPrimitive(document, primitive, &brushId, error)) {
			return false;
		}
		if (brushIds) {
			*brushIds = {brushId};
		}
		return true;
	}
	QVector<QVector<LevelMapVec3>> hulls;
	if (!levelShapeHulls(request, &hulls, error)) {
		return false;
	}
	const QString label = levelShapeLabel(request.shape);
	const int count = static_cast<int>(hulls.size());
	return addLevelMapBrushHulls(document, hulls, request.texture.trimmed(),
		QCoreApplication::translate("VibeStudioLevelShapes", "Add %1 (%n brush(es))", nullptr, count).arg(label),
		QCoreApplication::translate("VibeStudioLevelShapes", "Remove %1 (%n brush(es))", nullptr, count).arg(label), brushIds, error);
}

bool levelMapBrushesBounds(const LevelMapDocument& document, const QVector<int>& brushIds, LevelMapVec3* mins, LevelMapVec3* maxs, QString* error)
{
	if (error) {
		error->clear();
	}
	if (brushIds.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Choose the brushes the shape replaces."));
	}
	int worldspawnId = -1;
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			worldspawnId = entity.id;
			break;
		}
	}
	LevelMapVec3 low {0, 0, 0, false};
	LevelMapVec3 high {0, 0, 0, false};
	for (const int id : brushIds) {
		const auto found = std::find_if(document.brushes.cbegin(), document.brushes.cend(), [id](const LevelMapBrush& brush) { return brush.id == id; });
		if (found == document.brushes.cend() || !found->boundsSolved) {
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "A brush to replace is missing or not closed."));
		}
		if (found->entityId != worldspawnId) {
			return fail(error,
				QT_TRANSLATE_NOOP("VibeStudioLevelShapes", "Only world brushes can become a shape; move the brushes to the world first."));
		}
		if (!low.valid) {
			low = found->mins;
			high = found->maxs;
		} else {
			low = {std::min(low.x, found->mins.x), std::min(low.y, found->mins.y), std::min(low.z, found->mins.z), true};
			high = {std::max(high.x, found->maxs.x), std::max(high.y, found->maxs.y), std::max(high.z, found->maxs.z), true};
		}
	}
	low.valid = true;
	high.valid = true;
	if (mins) {
		*mins = low;
	}
	if (maxs) {
		*maxs = high;
	}
	return true;
}

bool replaceLevelMapBrushesWithShape(LevelMapDocument* document, const QVector<int>& brushIds, const LevelShapeRequest& request,
	QVector<int>* newBrushIds, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		return false;
	}
	LevelShapeRequest fitted = request;
	if (!levelMapBrushesBounds(*document, brushIds, &fitted.mins, &fitted.maxs, error)) {
		return false;
	}
	const quint64 revision = document->revision;
	const QVector<LevelMapSelectionRef> selection = document->selection;
	QVector<int> added;
	if (!addLevelMapShape(document, fitted, &added, error)) {
		return false;
	}
	QVector<LevelMapSelectionRef> replaced;
	for (const int id : brushIds) {
		replaced.push_back({LevelMapSelectionKind::QuakeBrush, id});
	}
	if (!deleteLevelMapObjects(document, replaced, error)) {
		// Take the shape back out, so a failure leaves the map as it was.
		QString ignored;
		if (undoLevelMapEdit(document, &ignored) && !document->redoStack.isEmpty()) {
			document->redoStack.removeLast();
		}
		document->selection = selection;
		return false;
	}
	const int count = static_cast<int>(brushIds.size());
	const QString label = levelShapeLabel(fitted.shape);
	collapseLevelMapUndoSteps(document, static_cast<int>(document->revision - revision),
		QCoreApplication::translate("VibeStudioLevelShapes", "Replace %n brush(es) with a shape: %1", nullptr, count).arg(label),
		QCoreApplication::translate("VibeStudioLevelShapes", "Restore %n replaced brush(es)", nullptr, count));
	QVector<LevelMapSelectionRef> chosen;
	for (const int id : std::as_const(added)) {
		chosen.push_back({LevelMapSelectionKind::QuakeBrush, id});
	}
	setLevelMapSelection(document, chosen);
	if (newBrushIds) {
		*newBrushIds = added;
	}
	return true;
}

} // namespace vibestudio
