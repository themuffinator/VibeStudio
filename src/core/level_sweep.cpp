#include "core/level_sweep.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene_locks.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
using V = LevelMapVec3;
V add(V a, V b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z, true};
}
V sub(V a, V b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z, true};
}
V mul(V a, double s)
{
	return {a.x * s, a.y * s, a.z * s, true};
}
double dot(V a, V b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
V cross(V a, V b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
double length(V a)
{
	return std::hypot(a.x, a.y, a.z);
}
bool finite(V a)
{
	return a.valid && std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
bool inWorld(V a)
{
	return finite(a) && std::max({std::abs(a.x), std::abs(a.y), std::abs(a.z)}) <= 32768;
}
bool fail(QString* error, const char* message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelSweep", message);
	}
	return false;
}
V rotate(V p, V axis, double radians)
{
	const double c = std::cos(radians), s = std::sin(radians);
	return add(add(mul(p, c), mul(cross(axis, p), s)), mul(axis, dot(axis, p) * (1 - c)));
}
V linear(const LevelTextureAffine& m, V p)
{
	return add(add(mul(m.columns[0], p.x), mul(m.columns[1], p.y)), mul(m.columns[2], p.z));
}
V point(const LevelTextureAffine& m, V p)
{
	return add(linear(m, p), m.translation);
}
LevelTextureAffine compose(const LevelTextureAffine& a, const LevelTextureAffine& b)
{
	LevelTextureAffine m;
	for (int k = 0; k < 3; ++k) {
		m.columns[k] = linear(a, b.columns[k]);
	}
	m.translation = point(a, b.translation);
	return m;
}

// Independent double-precision implementation of the behavioural contract in
// TB SweepToolUtils.cpp, 4c756be1a54a28d921d18c77716a5f170bdc31d0,
// Jackson Palmer / Kristian Duske, GPL-3.0-or-later. No source is copied.
LevelTextureAffine station(const LevelSweepSource& source, const LevelSweepRequest& r, V axis, double theta, double t)
{
	const auto c = source.centre;
	V centre = add(c, mul(r.translation, t));
	const double rise = dot(r.translation, axis);
	const auto chord = sub(r.translation, mul(axis, rise));
	if (r.path == LevelSweepPath::Arc && std::abs(theta) > 1e-9 && length(chord) > 1e-9) {
		const auto pivot = add(add(c, mul(chord, .5)), mul(cross(axis, chord), .5 / std::tan(theta * .5)));
		centre = add(add(pivot, rotate(sub(c, pivot), axis, theta * t)), mul(axis, rise * t));
	} else if (r.path == LevelSweepPath::SBend && length(source.normal) > 1e-9) {
		const double t2 = t * t, t3 = t2 * t, chordLength = length(r.translation);
		centre = add(c, add(add(mul(source.normal, chordLength * (t3 - 2 * t2 + t)), mul(r.translation, -2 * t3 + 3 * t2)),
		                    mul(rotate(source.normal, axis, theta), chordLength * (t3 - t2))));
	}
	// Explicit endpoints avoid tiny trigonometric drift at iteration joins.
	if (t == 0) {
		centre = c;
	} else if (t == 1) {
		centre = add(c, r.translation);
	}
	LevelTextureAffine m;
	const double scales[]{1 + (r.scale.x - 1) * t, 1 + (r.scale.y - 1) * t, 1 + (r.scale.z - 1) * t};
	for (int k = 0; k < 3; ++k) {
		V basis{k == 0 ? scales[k] : 0, k == 1 ? scales[k] : 0, k == 2 ? scales[k] : 0, true};
		m.columns[k] = rotate(basis, axis, theta * t);
	}
	m.translation = sub(centre, linear(m, c));
	return m;
}
} // namespace

bool levelSweepSource(const LevelMapDocument& document, const QVector<LevelMapComponentSet>& sets, LevelSweepSource* source, QString* error)
{
	if (error) {
		error->clear();
	}
	if (source) {
		*source = {};
	}
	if (!source || (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) || sets.isEmpty() ||
	    sets.size() > 64) {
		return fail(error, QT_TRANSLATE_NOOP("LevelSweep", "Choose up to 64 brush faces in a Quake-family map to sweep."));
	}
	LevelSweepSource result;
	V mins{}, maxs{}, normal{0, 0, 0, true};
	QMap<int, LevelBrushTopology> topologies;
	QSet<QString> seen;
	for (const auto& set : sets) {
		if (set.object.kind != LevelMapSelectionKind::QuakeBrush || set.kind != LevelBrushComponent::Face || set.components.isEmpty() ||
		    set.components.size() > 64) {
			return fail(error, QT_TRANSLATE_NOOP("LevelSweep",
			                                     "Sweep requires brush face selections; vertices, edges and patches cannot be swept."));
		}
		if (!topologies.contains(set.object.objectId)) {
			const auto brush = std::find_if(document.brushes.cbegin(), document.brushes.cend(),
			                                [&](const auto& b) { return b.id == set.object.objectId; });
			LevelBrushTopology topology;
			if (brush == document.brushes.cend()) {
				return fail(error, QT_TRANSLATE_NOOP("LevelSweep", "A sweep source brush no longer exists."));
			}
			if (!levelBrushTopology(*brush, &topology, error)) {
				return false;
			}
			topologies.insert(brush->id, std::move(topology));
		}
		const auto& topology = topologies[set.object.objectId];
		for (const int face : set.components) {
			if (face < 0 || face >= topology.faces.size()) {
				return fail(error, QT_TRANSLATE_NOOP("LevelSweep", "A sweep source face no longer exists."));
			}
			const auto key = QStringLiteral("%1/%2").arg(set.object.objectId).arg(face);
			if (seen.contains(key)) {
				continue;
			}
			seen.insert(key);
			if (seen.size() > 64) {
				return fail(error, QT_TRANSLATE_NOOP("LevelSweep", "Sweep supports up to 64 distinct source faces."));
			}
			LevelSweepProfile profile;
			profile.brushId = set.object.objectId;
			profile.faceId = face;
			for (const int id : topology.faces[face]) {
				const auto p = topology.vertices[id];
				if (!inWorld(p)) {
					return fail(error, QT_TRANSLATE_NOOP("LevelSweep", "Sweep source corners must be finite and within ±32768 units."));
				}
				profile.vertices << p;
				if (!mins.valid) {
					mins = maxs = p;
				}
				mins = {std::min(mins.x, p.x), std::min(mins.y, p.y), std::min(mins.z, p.z), true};
				maxs = {std::max(maxs.x, p.x), std::max(maxs.y, p.y), std::max(maxs.z, p.z), true};
			}
			const auto plane = topology.geometry.faces[face].plane;
			normal = add(normal, {plane.normalX, plane.normalY, plane.normalZ, true});
			result.profiles << std::move(profile);
		}
	}
	result.centre = mul(add(mins, maxs), .5);
	result.normal = length(normal) > 1e-9 ? mul(normal, 1 / length(normal)) : V{0, 0, 0, true};
	result.scaleArm = {0, 0, 0, true};
	for (const auto& profile : result.profiles) {
		for (const auto p : profile.vertices) {
			const auto arm = sub(p, result.centre);
			if (dot(arm, arm) > dot(result.scaleArm, result.scaleArm)) {
				result.scaleArm = arm;
			}
		}
	}
	*source = std::move(result);
	return true;
}

bool buildLevelSweep(const LevelMapDocument& document, const LevelSweepRequest& r, LevelSweepGeometry* geometry, QString* error)
{
	if (error) {
		error->clear();
	}
	if (geometry) {
		*geometry = {};
	}
	if (!geometry || !finite(r.translation) || !finite(r.rotationAxis) || !finite(r.scale) || !std::isfinite(r.angle) ||
	    std::min({r.scale.x, r.scale.y, r.scale.z}) < .01 || r.segments < 1 || r.segments > 64 || r.iterations < 1 || r.iterations > 8 ||
	    (r.path != LevelSweepPath::Arc && r.path != LevelSweepPath::Straight && r.path != LevelSweepPath::SBend) ||
	    !validLevelClipTexture(r.texture)) {
		return fail(
		    error,
		    QT_TRANSLATE_NOOP(
		        "LevelSweep",
		        "Sweep needs finite transforms, positive scales of at least 0.01, 1–64 segments, 1–8 iterations and a valid material."));
	}
	LevelSweepGeometry result;
	if (!levelSweepSource(document, r.faces, &result.source, error)) {
		return false;
	}
	const double theta = std::remainder(r.angle, 360.0) * std::numbers::pi / 180.0, axisLength = length(r.rotationAxis);
	if (std::abs(theta) > 1e-9 && (!std::isfinite(axisLength) || axisLength < 1e-9)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelSweep", "A rotated sweep needs a non-zero rotation axis."));
	}
	const auto axis = std::abs(theta) <= 1e-9 ? V{0, 0, 1, true} : mul(r.rotationAxis, 1 / axisLength);
	result.report.sourceFaces = int(result.source.profiles.size());
	result.report.attempted = result.report.sourceFaces * r.segments * r.iterations;
	if (result.report.attempted > 4096) {
		return fail(error, QT_TRANSLATE_NOOP("LevelSweep", "This sweep would exceed 4096 sections. Reduce faces, segments or iterations."));
	}
	QVector<LevelTextureAffine> transforms;
	LevelTextureAffine power;
	const auto cap = station(result.source, r, axis, theta, 1);
	for (int iteration = 0; iteration < r.iterations; ++iteration) {
		for (int s = 1; s <= r.segments; ++s) {
			transforms << compose(power, station(result.source, r, axis, theta, double(s) / r.segments));
		}
		power = compose(power, cap);
	}
	int completed = 0;
	for (const auto& profile : result.source.profiles) {
		auto previous = profile.vertices;
		for (int section = 0; section < transforms.size(); ++section) {
			detail::placementCheckpoint(LevelPlacementPhase::Building, completed++, result.report.attempted);
			QVector<V> current;
			bool valid = true;
			for (const auto p : profile.vertices) {
				auto v = point(transforms[section], p);
				if (r.integerAlignment) {
					v = {std::round(v.x), std::round(v.y), std::round(v.z), true};
				}
				current << v;
				valid = valid && inWorld(v);
			}
			for (const auto p : previous) {
				valid = valid && inWorld(p);
			}
			LevelMapBrush brush;
			LevelBrushTopology topology;
			QString reason;
			if (valid) {
				valid = createLevelBrushEnvelope(previous + current, r.texture, &brush, &reason) &&
				        levelBrushTopology(brush, &topology, &reason);
			} else {
				reason = QCoreApplication::translate("LevelSweep", "A generated corner is non-finite or outside ±32768 units.");
			}
			if (valid) {
				result.hulls << LevelBrushHullInsertion{topology.vertices, {LevelMapSelectionKind::QuakeBrush, profile.brushId}};
			} else {
				result.report.skipped << LevelSweepSkipped{profile.brushId, profile.faceId, section / r.segments + 1,
				                                           section % r.segments + 1, reason};
			}
			previous = std::move(current);
			if ((section + 1) % r.segments == 0) {
				result.destinationCaps << previous;
			}
		}
	}
	if (result.hulls.isEmpty()) {
		return fail(error,
		            QT_TRANSLATE_NOOP(
		                "LevelSweep",
		                "Sweep produced no valid solid sections. Move, rotate or scale the destination cap, or reduce segmentation."));
	}
	*geometry = std::move(result);
	return true;
}

bool sweepLevelMap(LevelMapDocument* document, const LevelSweepRequest& request, LevelSweepReport* report, QString* error)
{
	if (error) {
		error->clear();
	}
	if (report) {
		*report = {};
	}
	LevelSweepReport made;
	if (const auto guarded =
	        guardLevelSceneEdit(document, error, [&](auto* candidate) { return sweepLevelMap(candidate, request, &made, error); });
	    guarded.has_value()) {
		if (*guarded && report) {
			*report = made;
		}
		return *guarded;
	}
	if (!document) {
		return fail(error, QT_TRANSLATE_NOOP("LevelSweep", "Open a map before sweeping faces."));
	}
	LevelSweepGeometry geometry;
	if (!buildLevelSweep(*document, request, &geometry, error)) {
		return false;
	}
	made = geometry.report;
	if (!addLevelMapBrushHullBatch(document, geometry.hulls, request.texture,
	                               QCoreApplication::translate("LevelSweep", "Sweep selected faces"),
	                               QCoreApplication::translate("LevelSweep", "Remove swept brushes"), &made.brushIds, error)) {
		return false;
	}
	if (report) {
		*report = made;
	}
	return true;
}

QJsonObject levelSweepReportJson(const LevelSweepReport& report)
{
	QJsonArray ids, skipped;
	for (const int id : report.brushIds) {
		ids << id;
	}
	for (const auto& item : report.skipped) {
		skipped << QJsonObject{{"brushId", item.brushId},
		                       {"faceId", item.faceId},
		                       {"iteration", item.iteration},
		                       {"segment", item.segment},
		                       {"reason", item.reason}};
	}
	return {{"operation", "sweep"},
	        {"sourceFaces", report.sourceFaces},
	        {"attempted", report.attempted},
	        {"created", report.brushIds.size()},
	        {"brushIds", ids},
	        {"skipped", skipped},
	        {"sourcesRetained", true}};
}
} // namespace vibestudio
