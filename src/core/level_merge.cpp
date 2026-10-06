#include "core/level_merge.h"
#include "core/level_brush.h"
#include "core/level_texture_mapping.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
constexpr double tolerance = 1e-7;
using Polygon = QVector<LevelMapVec3>;
using Solid = QVector<Polygon>;
bool fail(QString *error, const char *message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelMerge", message);
	}
	return false;
}
LevelMapVec3 sub(const LevelMapVec3 &a, const LevelMapVec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z, true}; }
double dot(const LevelMapVec3 &a, const LevelMapVec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 cross(const LevelMapVec3 &a, const LevelMapVec3 &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
bool close(const LevelMapVec3 &a, const LevelMapVec3 &b) { return dot(sub(a, b), sub(a, b)) <= tolerance * tolerance; }
void appendUnique(Polygon *points, const LevelMapVec3 &p)
{
	if (std::none_of(points->cbegin(), points->cend(), [&](const auto &v) { return close(v, p); })) {
		points->append(p);
	}
}
bool samePlane(const MapPlane &a, const MapPlane &b)
{
	return std::abs(a.normalX - b.normalX) < 1e-9 && std::abs(a.normalY - b.normalY) < 1e-9 && std::abs(a.normalZ - b.normalZ) < 1e-9 &&
		   std::abs(a.distance - b.distance) <= tolerance;
}
Solid solid(const MapBrushGeometry &geometry)
{
	Solid result;
	for (const auto &face : geometry.faces) {
		result << face.points;
	}
	return result;
}
bool preciseTopology(const LevelMapBrush &brush, LevelBrushTopology *topology, QString *error)
{
	if (!levelBrushTopology(brush, topology, error)) {
		return false;
	}
	topology->geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId, MapGeometryPrecision::PreserveCoordinates);
	topology->vertices.clear();
	if (!topology->geometry.solved || !topology->geometry.warnings.isEmpty()) {
		return fail(
			error,
			QT_TRANSLATE_NOOP("LevelMerge", "This brush cannot be solved without coordinate cleanup. Repair its geometry before merging."));
	}
	for (const auto &face : topology->geometry.faces) {
		if (!face.isValid()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "A merge source has an empty exterior face."));
		}
		for (const auto &p : face.points) {
			if (std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) > 32768 + tolerance) {
				return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "Merged geometry must stay within ±32768 map units."));
			}
			appendUnique(&topology->vertices, p);
		}
	}
	if (topology->vertices.size() > kLevelBrushComponentMaxVertices) {
		return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "A merge brush exceeds 256 precise vertices."));
	}
	return true;
}
double volume(const Solid &polygons)
{
	LevelMapVec3 center{0, 0, 0, true};
	int count = 0;
	for (const auto &polygon : polygons) {
		for (const auto &p : polygon) {
			center.x += p.x;
			center.y += p.y;
			center.z += p.z;
			++count;
		}
	}
	if (count == 0) {
		return 0;
	}
	center.x /= count;
	center.y /= count;
	center.z /= count;
	double result = 0;
	for (const auto &polygon : polygons) {
		for (int i = 1; i + 1 < polygon.size(); ++i) {
			result += std::abs(dot(sub(polygon[0], center), cross(sub(polygon[i], center), sub(polygon[i + 1], center)))) / 6;
		}
	}
	return result;
}
// Original polygon clipping implementation. Closed convex polygons are split
// against a plane; the shared intersection ring closes both resulting solids.
bool split(const Solid &input, const MapPlane &plane, Solid *inside, Solid *outside, qint64 *work)
{
	Polygon cap;
	for (const auto &polygon : input) {
		Polygon in, out;
		for (int i = 0; i < polygon.size(); ++i) {
			if (++*work > 8000000) {
				return false;
			}
			const auto &a = polygon[i], &b = polygon[(i + 1) % polygon.size()];
			const double da = planeDistanceToPoint(plane, a), db = planeDistanceToPoint(plane, b);
			if (da <= tolerance) {
				in << a;
			}
			if (da >= -tolerance) {
				out << a;
			}
			if (std::abs(da) <= tolerance) {
				appendUnique(&cap, a);
			}
			if ((da < -tolerance && db > tolerance) || (da > tolerance && db < -tolerance)) {
				const double t = da / (da - db);
				LevelMapVec3 p{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z), true};
				in << p;
				out << p;
				appendUnique(&cap, p);
			}
		}
		if (in.size() >= 3) {
			inside->append(in);
		}
		if (out.size() >= 3) {
			outside->append(out);
		}
	}
	if (cap.size() < 3) {
		return false;
	}
	LevelMapVec3 center{0, 0, 0, true};
	for (const auto &p : cap) {
		center.x += p.x;
		center.y += p.y;
		center.z += p.z;
	}
	center.x /= cap.size();
	center.y /= cap.size();
	center.z /= cap.size();
	const LevelMapVec3 normal{plane.normalX, plane.normalY, plane.normalZ, true};
	const auto u = sub(cap.first(), center), v = cross(normal, u);
	std::sort(cap.begin(), cap.end(), [&](const auto &a, const auto &b) {
		return std::atan2(dot(sub(a, center), v), dot(sub(a, center), u)) < std::atan2(dot(sub(b, center), v), dot(sub(b, center), u));
	});
	inside->append(cap);
	outside->append(cap);
	return true;
}
bool sameSurface(const LevelMapBrushFace &a, const LevelMapBrushFace &b, const Polygon &points)
{
	if (a.textureName != b.textureName || a.contentFlags != b.contentFlags || a.surfaceFlags != b.surfaceFlags ||
		a.surfaceValue != b.surfaceValue) {
		return false;
	}
	const auto x = levelTextureProjection(a), y = levelTextureProjection(b);
	if (!x.valid || !y.valid || x.normalizedCoordinates != y.normalizedCoordinates) {
		return false;
	}
	return std::all_of(points.cbegin(), points.cend(), [&](const auto &p) {
		const auto delta = x.at(p) - y.at(p);
		return std::abs(delta.x()) < 1e-6 && std::abs(delta.y()) < 1e-6;
	});
}
} // namespace

int LevelBrushMergeGeometry::unresolvedCount() const
{
	return static_cast<int>(std::count_if(faces.cbegin(), faces.cend(), [](const auto &f) { return !f.resolved; }));
}

bool solveLevelBrushMerge(const QVector<LevelMapBrush> &input, const LevelBrushMergeRequest &request, LevelBrushMergeGeometry *result,
						  QString *error, const std::function<bool()> &cancelled)
{
	if (error) {
		error->clear();
	}
	if (result) {
		*result = {};
	}
	if (!result || input.size() < 2 || input.size() > 64) {
		return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "Select between 2 and 64 brushes to merge."));
	}
	const auto checkCancelled = [&] { return cancelled && cancelled(); };
	auto brushes = input;
	std::sort(brushes.begin(), brushes.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
	QVector<LevelBrushTopology> topologies;
	Polygon points;
	LevelBrushMergeGeometry draft;
	for (int b = 0; b < brushes.size(); ++b) {
		if (checkCancelled()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "Brush merge cancelled."));
		}
		if (brushes[b].entityId != brushes.first().entityId || brushes[b].primitiveKind != brushes.first().primitiveKind) {
			return fail(error,
						QT_TRANSLATE_NOOP("LevelMerge", "Merge brushes belonging to the same entity and using the same face format."));
		}
		if (brushes[b].id < 0 || (b > 0 && brushes[b].id == brushes[b - 1].id)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "The merge selection contains invalid or duplicate brush IDs."));
		}
		LevelBrushTopology topology;
		if (!preciseTopology(brushes[b], &topology, error)) {
			return false;
		}
		points += topology.vertices;
		draft.sourceFaceCount += static_cast<int>(brushes[b].faces.size());
		if (draft.sourceFaceCount > 2048 || points.size() > 4096) {
			return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "Merge at most 2,048 source faces and 4,096 vertices at a time."));
		}
		topologies << std::move(topology);
	}
	QVector<MapPlane> planes;
	for (int b = 0; b < brushes.size(); ++b) {
		for (int f = 0; f < brushes[b].faces.size(); ++f) {
			if (checkCancelled()) {
				return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "Brush merge cancelled."));
			}
			const auto plane = topologies[b].geometry.faces[f].plane;
			if (std::any_of(points.cbegin(), points.cend(), [&](const auto &p) { return planeDistanceToPoint(plane, p) > tolerance; })) {
				continue;
			}
			const auto found = std::find_if(planes.cbegin(), planes.cend(), [&](const auto &p) { return samePlane(p, plane); });
			const int index = static_cast<int>(found - planes.cbegin());
			if (found == planes.cend()) {
				planes << plane;
				draft.faces << LevelBrushMergeFace{};
				draft.brush.faces << brushes[b].faces[f];
			}
			draft.faces[index].sources << LevelBrushMergeSource{brushes[b].id, f};
		}
	}
	LevelBrushTopology merged;
	if (!preciseTopology(draft.brush, &merged, nullptr)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelMerge",
											 "These brushes do not form a closed convex union. Clip or rearrange them before merging."));
	}
	// Subtract every input from the candidate. Any remaining solid proves that
	// merging would fill space, including cavities invisible from its exterior.
	QVector<Solid> remaining{solid(merged.geometry)};
	qint64 work = 0;
	for (const auto &topology : topologies) {
		QVector<Solid> next;
		for (auto region : remaining) {
			for (const auto &face : topology.geometry.faces) {
				if (checkCancelled()) {
					return fail(error, QT_TRANSLATE_NOOP("LevelMerge", "Brush merge cancelled."));
				}
				double low = 1e100, high = -1e100;
				for (const auto &polygon : region) {
					for (const auto &p : polygon) {
						const double d = planeDistanceToPoint(face.plane, p);
						low = std::min(low, d);
						high = std::max(high, d);
						++work;
					}
				}
				if (work > 8000000 || next.size() > 4096) {
					return fail(error, QT_TRANSLATE_NOOP("LevelMerge",
														 "The merge coverage check exceeded its work limit. Merge a smaller selection."));
				}
				if (low >= -tolerance) {
					next << region;
					break;
				}
				if (high <= tolerance) {
					continue;
				}
				Solid inside, outside;
				if (!split(region, face.plane, &inside, &outside, &work)) {
					return fail(error, QT_TRANSLATE_NOOP(
										   "LevelMerge",
										   "The merge coverage check could not split this geometry reliably. Merge a simpler selection."));
				}
				next << std::move(outside);
				region = std::move(inside);
			}
		}
		remaining = std::move(next);
		if (remaining.isEmpty()) {
			break;
		}
	}
	if (!remaining.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP(
							   "LevelMerge",
							   "Merging would fill a gap, cavity or concavity. The selected brushes must already form one convex solid."));
	}
	const auto sourceFace = [&](const LevelBrushMergeSource &ref) -> const LevelMapBrushFace & {
		return std::find_if(brushes.cbegin(), brushes.cend(), [&](const auto &b) { return b.id == ref.brushId; })->faces[ref.faceIndex];
	};
	for (auto choice = request.faceSources.cbegin(); choice != request.faceSources.cend(); ++choice) {
		if (choice.key() < 0 || choice.key() >= draft.faces.size() || !draft.faces[choice.key()].sources.contains(choice.value())) {
			return fail(
				error,
				QT_TRANSLATE_NOOP("LevelMerge", "A surface choice does not belong to that output face. Review the current merge preview."));
		}
	}
	for (int f = 0; f < draft.faces.size(); ++f) {
		auto &face = draft.faces[f];
		face.chosen = request.faceSources.value(f, face.sources.first());
		const auto &donor = sourceFace(face.chosen);
		if (!levelTextureProjection(donor).valid) {
			return fail(error, QT_TRANSLATE_NOOP("LevelMerge",
												 "A retained face has invalid texture coordinates. Repair its mapping before merging."));
		}
		face.conflict = std::any_of(face.sources.cbegin(), face.sources.cend(),
									[&](const auto &ref) { return !sameSurface(donor, sourceFace(ref), merged.geometry.faces[f].points); });
		face.resolved = !face.conflict || request.faceSources.contains(f);
		draft.brush.faces[f] = donor;
		draft.brush.faces[f].id = f;
	}
	draft.volume = volume(solid(merged.geometry));
	draft.brush.primitiveKind = brushes.first().primitiveKind;
	draft.brush.entityId = brushes.first().entityId;
	draft.brush.faceCount = static_cast<int>(draft.faces.size());
	draft.brush.mins = merged.geometry.mins;
	draft.brush.maxs = merged.geometry.maxs;
	draft.brush.boundsSolved = true;
	for (const auto &f : draft.brush.faces) {
		if (!draft.brush.textureNames.contains(f.textureName)) {
			draft.brush.textureNames << f.textureName;
		}
	}
	*result = std::move(draft);
	return true;
}

QJsonObject levelBrushMergeReportJson(const LevelBrushMergePlan &plan)
{
	QJsonArray faces;
	for (int f = 0; f < plan.geometry().faces.size(); ++f) {
		const auto &face = plan.geometry().faces[f];
		const auto &definition = plan.brush().faces[f];
		const auto plane = planeFromPoints(definition.p0, definition.p1, definition.p2, MapGeometryPrecision::PreserveCoordinates);
		const auto ref = [](const LevelBrushMergeSource &source) {
			return QJsonObject{{QStringLiteral("brush"), source.brushId}, {QStringLiteral("face"), source.faceIndex}};
		};
		QJsonArray candidates;
		for (const auto &source : face.sources) {
			auto candidate = ref(source);
			const auto brush = std::find_if(plan.sourceBrushes().cbegin(), plan.sourceBrushes().cend(),
											[&](const auto &b) { return b.id == source.brushId; });
			if (brush != plan.sourceBrushes().cend()) {
				const auto &definition = brush->faces[source.faceIndex];
				const auto uv = levelTextureProjection(definition);
				candidate.insert(QStringLiteral("material"), definition.textureName);
				candidate.insert(QStringLiteral("contents"), definition.contentFlags);
				candidate.insert(QStringLiteral("surfaceFlags"), definition.surfaceFlags);
				candidate.insert(QStringLiteral("value"), definition.surfaceValue);
				candidate.insert(QStringLiteral("uvUnits"),
								 uv.normalizedCoordinates ? QStringLiteral("repeats") : QStringLiteral("texels"));
				candidate.insert(QStringLiteral("u"), QJsonArray{uv.u.x, uv.u.y, uv.u.z, uv.offsetU});
				candidate.insert(QStringLiteral("v"), QJsonArray{uv.v.x, uv.v.y, uv.v.z, uv.offsetV});
			}
			candidates.append(candidate);
		}
		faces.append(QJsonObject{
			{QStringLiteral("face"), f},
			{QStringLiteral("plane"), QJsonObject{{QStringLiteral("normal"), QJsonArray{plane.normalX, plane.normalY, plane.normalZ}},
												  {QStringLiteral("distance"), plane.distance}}},
			{QStringLiteral("conflict"), face.conflict},
			{QStringLiteral("resolved"), face.resolved},
			{QStringLiteral("chosen"), ref(face.chosen)},
			{QStringLiteral("sources"), candidates},
			{QStringLiteral("material"), plan.brush().faces[f].textureName}});
	}
	return {{QStringLiteral("ready"), plan.ready()},
			{QStringLiteral("brushes"), plan.brushCount()},
			{QStringLiteral("sourceFaces"), plan.geometry().sourceFaceCount},
			{QStringLiteral("resultFaces"), plan.brush().faceCount},
			{QStringLiteral("entity"), plan.brush().entityId},
			{QStringLiteral("volume"), plan.geometry().volume},
			{QStringLiteral("unresolved"), plan.geometry().unresolvedCount()},
			{QStringLiteral("faces"), faces}};
}
} // namespace vibestudio
