#include "core/map_geometry_cache.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <source_location>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool same(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z;
}
bool same(const MapBrushGeometry& a, const MapBrushGeometry& b)
{
	if (a.brushId != b.brushId || a.entityId != b.entityId || a.solved != b.solved || a.cancelled != b.cancelled || a.warnings != b.warnings
		|| !same(a.mins, b.mins) || !same(a.maxs, b.maxs) || a.faces.size() != b.faces.size()) { return false; }
	for (int i = 0; i < a.faces.size(); ++i) {
		const auto& x = a.faces[i]; const auto& y = b.faces[i];
		if (x.faceIndex != y.faceIndex || x.textureName != y.textureName || x.points.size() != y.points.size()
			|| x.plane.valid != y.plane.valid || x.plane.distance != y.plane.distance || x.plane.normalX != y.plane.normalX
			|| x.plane.normalY != y.plane.normalY || x.plane.normalZ != y.plane.normalZ) { return false; }
		for (int p = 0; p < x.points.size(); ++p) { if (!same(x.points[p], y.points[p])) { return false; } }
	}
	return true;
}
bool check(MapBrushGeometryCache& cache, const LevelMapDocument& map, int solves, int reuses,
	MapGeometryPrecision precision = MapGeometryPrecision::CompilerCompatible,
	const std::source_location callSite = std::source_location::current())
{
	const auto result = cache.build(map, precision);
	bool ok = expect(result.size() == map.brushes.size(), "every brush preserved in original order");
	for (int i = 0; i < result.size(); ++i) {
		const auto& brush = map.brushes[i];
		ok &= expect(same(result[i], solveBrushGeometry(brush.faces, brush.id, brush.entityId, precision)), "cached geometry equals shared solver");
	}
	const auto statistics = cache.statistics();
	if (statistics.solved != solves || statistics.reused != reuses) {
		std::cerr << "line " << callSite.line() << ": expected " << solves << " solves / " << reuses
			<< " reuses, got " << statistics.solved << " / " << statistics.reused << '\n';
		ok = false;
	}
	return ok;
}
bool samePreview(const LevelMapPreviewMesh& a, const LevelMapPreviewMesh& b)
{
	if (a.triangles != b.triangles || a.owners != b.owners || a.ownerFaces != b.ownerFaces || a.materialTargets != b.materialTargets
		|| a.mesh.surfaces.size() != b.mesh.surfaces.size()) { return false; }
	for (int i = 0; i < a.mesh.surfaces.size(); ++i) {
		const auto& x = a.mesh.surfaces[i]; const auto& y = b.mesh.surfaces[i];
		if (x.name != y.name || x.texCoords.size() != y.texCoords.size() || x.frames.size() != y.frames.size()) { return false; }
		for (int v = 0; v < x.texCoords.size(); ++v) {
			if (x.texCoords[v].u != y.texCoords[v].u || x.texCoords[v].v != y.texCoords[v].v) { return false; }
		}
		for (int f = 0; f < x.frames.size(); ++f) {
			if (x.frames[f].positions.size() != y.frames[f].positions.size()) { return false; }
			for (int v = 0; v < x.frames[f].positions.size(); ++v) {
				const auto p = x.frames[f].positions[v], q = y.frames[f].positions[v];
				if (p.x != q.x || p.y != q.y || p.z != q.z) { return false; }
			}
		}
	}
	return true;
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	LevelMapDocument original; QString error;
	if (!tests::createGeometryFixture(12, &original, &error)) { std::cerr << error.toStdString(); return 1; }
	MapBrushGeometryCache cache;
	bool ok = check(cache, original, 12, 0) && check(cache, original, 0, 12);
	auto map = original;
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 5}}, &error)
		&& moveLevelMapSelection(&map, 8, 0, 0, &error), "move one brush");
	ok &= check(cache, map, 1, 11);
	ok &= expect(undoLevelMapEdit(&map, &error), "undo move") && check(cache, map, 1, 11);
	ok &= expect(redoLevelMapEdit(&map, &error), "redo move") && check(cache, map, 1, 11);
	map.brushes[4].entityId = 91;
	map.brushes[4].selected = true;
	ok &= check(cache, map, 0, 12); // Ownership and selection stay live.
	map.brushes[2].faces[0].textureName = QStringLiteral("studio/other");
	ok &= check(cache, map, 1, 11);
	std::reverse(map.brushes.begin(), map.brushes.end());
	ok &= check(cache, map, 0, 12);
	map.brushes.removeAt(1);
	ok &= check(cache, map, 0, 11);
	ok &= expect(cache.statistics().retainedBrushes == 11, "deleted brush pruned");
	map.brushes[0].id = 1000000;
	ok &= check(cache, map, 1, 10);

	// Keep a mutable alias across the cache query: implicit-sharing identity
	// cannot substitute for checking plane values on the next query.
	auto& alias = map.brushes[0].faces;
	ok &= check(cache, map, 0, 11);
	for (auto& face : alias) { face.p0.z += 16; face.p1.z += 16; face.p2.z += 16; }
	ok &= check(cache, map, 1, 10);
	const auto saved = cache;
	map.brushes[0].faces[0].textureName = QStringLiteral("studio/copied");
	ok &= check(cache, map, 1, 10);
	auto independent = saved;
	ok &= check(independent, map, 1, 10);
	ok &= check(cache, map, 0, 11);
	for (auto& face : map.brushes[0].faces) { face.p0.x += 0.00001; face.p1.x += 0.00001; face.p2.x += 0.00001; }
	ok &= check(cache, map, 11, 0, MapGeometryPrecision::PreserveCoordinates);
	ok &= check(cache, map, 0, 11, MapGeometryPrecision::PreserveCoordinates);
	ok &= check(cache, map, 11, 0);
	const auto snapped = cache.build(map);
	const auto precise = cache.build(map, MapGeometryPrecision::PreserveCoordinates);
	ok &= expect(snapped[0].mins.x != precise[0].mins.x, "precision fixture exercises actual unsnapped coordinates");
	cache.build(map);

	map.brushes[1].faces.clear();
	ok &= check(cache, map, 1, 10) && check(cache, map, 1, 10);
	ok &= expect(cache.statistics().retainedBrushes == 10, "invalid geometry is not retained");
	auto duplicatePlane = original;
	duplicatePlane.brushes[0].faces.append(duplicatePlane.brushes[0].faces[0]);
	MapBrushGeometryCache warned;
	const auto warningGeometry = warned.build(duplicatePlane);
	ok &= expect(!warningGeometry[0].warnings.isEmpty(), "duplicate plane produces diagnostics");
	ok &= check(warned, duplicatePlane, 1, 11);

	MapBrushGeometryCache disabled(0), bounded(4096);
	ok &= check(disabled, original, 12, 0) && check(disabled, original, 12, 0);
	const auto limited = bounded.build(original);
	for (int i = 0; i < limited.size(); ++i) { ok &= expect(same(limited[i], solveBrushGeometry(original.brushes[i].faces, i, original.brushes[i].entityId)), "budget does not omit geometry"); }
	ok &= expect(bounded.statistics().retainedBytes <= 4096 && bounded.statistics().retainedBrushes < 12, "cache payload budget enforced");
	const auto retained = bounded.statistics().retainedBrushes;
	ok &= check(bounded, original, 12 - retained, retained);
	bounded.build({});
	ok &= expect(bounded.statistics().retainedBytes == 0 && bounded.statistics().retainedBrushes == 0, "empty map releases cache");
	auto oversized = original; oversized.brushes[0].faces[0].textureName = QString(600000, QLatin1Char('x'));
	MapBrushGeometryCache keyLimited;
	ok &= check(keyLimited, oversized, 12, 0) && check(keyLimited, oversized, 1, 11);
	ok &= expect(keyLimited.statistics().retainedBrushes == 11, "oversized input keys are not retained");
	// The material alone can fit the per-name check while the complete key
	// exceeds its budget; a slightly smaller name must remain cacheable.
	oversized.brushes[0].faces[0].textureName = QString(512 * 1024, QLatin1Char('x'));
	ok &= check(keyLimited, oversized, 1, 11) && check(keyLimited, oversized, 1, 11);
	oversized.brushes[0].faces[0].textureName = QString(511 * 1024, QLatin1Char('x'));
	ok &= check(keyLimited, oversized, 1, 11) && check(keyLimited, oversized, 0, 12);
	cache.clear();
	ok &= expect(cache.statistics().retainedBytes == 0 && cache.statistics().solved == 0, "explicit reset clears diagnostics and data");
	// The bounded byte key must retain full UTF-16 names, their lengths and
	// explicit plane inputs, including mutations through retained references.
	auto named = original;
	MapBrushGeometryCache names;
	ok &= check(names, named, 12, 0);
	for (const auto& name : QStringList{QString(), QString::fromUtf16(u"a\0b", 3), QString::fromUtf16(u"a\0c", 3),
		QString::fromUtf8("stone/水"), QString::fromUtf8("stone/水2")}) {
		named.brushes[0].faces[0].textureName = name;
		ok &= check(names, named, 1, 11) && check(names, named, 0, 12);
	}
	auto planes = original;
	for (auto& brush : planes.brushes) {
		for (auto& face : brush.faces) {
			const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
			face.explicitPlane = true; face.planeNormal = {plane.normalX, plane.normalY, plane.normalZ, plane.valid};
			face.planeDistance = -plane.distance;
		}
	}
	MapBrushGeometryCache explicitPlanes;
	ok &= check(explicitPlanes, planes, 12, 0) && check(explicitPlanes, planes, 0, 12);
	auto& changedPlane = planes.brushes[4].faces[1];
	changedPlane.planeDistance -= 1;
	ok &= check(explicitPlanes, planes, 1, 11) && check(explicitPlanes, planes, 0, 12);
	changedPlane.planeNormal.x += 0.001;
	ok &= check(explicitPlanes, planes, 1, 11);
	changedPlane.explicitPlane = false;
	ok &= check(explicitPlanes, planes, 1, 11);
	changedPlane.p0.valid = false;
	// The solver uses finite coordinates; changing the flag still invalidates
	// the key, while a genuinely degenerate plane must keep its diagnostics live.
	ok &= check(explicitPlanes, planes, 1, 11) && check(explicitPlanes, planes, 0, 12);
	changedPlane.p1 = changedPlane.p0;
	ok &= check(explicitPlanes, planes, 1, 11) && check(explicitPlanes, planes, 1, 11);

	// Cached polygons must not cache UVs, material provenance or package image
	// dimensions. Compare the actual preview against its uncached core path.
	map = original;
	LevelMapPreviewMeshOptions options; options.brushGeometryCache = &cache;
	options.textureSizes.insert(QStringLiteral("studio/cache"), QSize(64, 64));
	const auto first = buildLevelMapPreviewMesh(map, options);
	for (auto& face : map.brushes[0].faces) { face.shiftX += 8; }
	const auto shifted = buildLevelMapPreviewMesh(map, options);
	ok &= expect(cache.statistics().solved == 0 && cache.statistics().reused == 12, "UV-only change reuses planes");
	options.brushGeometryCache = nullptr;
	ok &= expect(samePreview(shifted, buildLevelMapPreviewMesh(map, options)) && !samePreview(first, shifted), "live UV change reaches preview");
	options.brushGeometryCache = &cache;
	options.textureSizes[QStringLiteral("studio/cache")] = QSize(128, 64);
	const auto resizedTexture = buildLevelMapPreviewMesh(map, options);
	ok &= expect(cache.statistics().solved == 0 && !samePreview(shifted, resizedTexture), "package dimensions refresh UVs without solving");
	options.brushGeometryCache = nullptr;
	ok &= expect(samePreview(resizedTexture, buildLevelMapPreviewMesh(map, options)), "cached dimensions match full build");
	cache.clear(); options.brushGeometryCache = &cache; options.triangleLimit = 2;
	const auto truncated = buildLevelMapPreviewMesh(map, options);
	ok &= expect(truncated.truncated && truncated.triangles == 2 && cache.statistics().solved == 1, "triangle budget does not pre-solve the map");
	options.isCancelled = [] { return true; };
	const auto cancelled = buildLevelMapPreviewMesh(map, options);
	ok &= expect(cancelled.cancelled && cache.statistics().solved == 0 && cache.statistics().reused == 0, "cancel before solve");
	QJsonArray measurements;
	for (const int count : {1000, 10000}) {
		LevelMapDocument large;
		if (!expect(tests::createGeometryFixture(count, &large, &error), "cache scale fixture")) { return 1; }
		MapBrushGeometryCache scaleCache;
		const auto cold = scaleCache.build(large);
		ok &= expect(cold.size() == count && scaleCache.statistics().solved == count &&
			scaleCache.statistics().retainedBrushes == count, "complete scale fixture fits the bounded cache");
		QJsonArray samples;
		for (int sample = 0; sample < 3; ++sample) {
			QElapsedTimer timer; timer.start(); const auto warm = scaleCache.build(large);
			samples.append(timer.nsecsElapsed() / 1e6);
			ok &= expect(warm.size() == count && scaleCache.statistics().solved == 0 && scaleCache.statistics().reused == count,
				"unchanged scale scene reuses every brush without truncation");
		}
		measurements.append(QJsonObject{{"brushes", count}, {"warm_build_ms", samples}, {"retained_bytes", scaleCache.statistics().retainedBytes}});
	}
	std::cout << QJsonDocument(QJsonObject{{"measurements", measurements}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
