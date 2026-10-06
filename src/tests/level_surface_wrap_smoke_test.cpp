#include "core/level_surface_clipboard.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << message << ": " << detail.toStdString() << '\n'; }
	return value;
}
double dot(LevelMapVec3 a, LevelMapVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
bool near(double a, double b) { return std::abs(a - b) < 1e-7 * std::max({1.0, std::abs(a), std::abs(b)}); }
bool near(QPointF a, QPointF b) { return near(a.x(), b.x()) && near(a.y(), b.y()); }
LevelTextureProjection texels(const LevelMapBrushFace& face)
{
	auto p = levelTextureProjection(face);
	if (p.normalizedCoordinates) {
		p.u = {p.u.x * 128, p.u.y * 128, p.u.z * 128, true}; p.offsetU *= 128;
		p.v = {p.v.x * 64, p.v.y * 64, p.v.z * 64, true}; p.offsetV *= 64;
	}
	return p;
}
std::array<double, 3> metric(const LevelTextureProjection& uv, const MapPlane& plane)
{
	const LevelMapVec3 n{plane.normalX, plane.normalY, plane.normalZ, true};
	const double u = dot(uv.u, n), v = dot(uv.v, n);
	return {dot(uv.u, uv.u) - u * u, dot(uv.u, uv.v) - u * v, dot(uv.v, uv.v) - v * v};
}
bool load(const QByteArray& bytes, LevelMapDocument* document, QString* error)
{
	return loadLevelMapBytes({"wrap.map", {}, "idtech3"}, bytes, document, error);
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true; QString error;
	// Independent analytic oracle: a displaced hinge parallel to Y. Source
	// covectors include a normal component, shear and a reflected V direction.
	LevelMapBrushFace source; source.explicitPlane = true; source.planeNormal = {0, 0, 1, true}; source.planeDistance = -12;
	source.explicitTextureAxes = true; source.uAxis = {2, 0, 5, true}; source.vAxis = {0.5, -3, 7, true};
	source.uOffset = 17; source.vOffset = -9; source.scaleX = source.scaleY = 1;
	for (double degrees : {-173.0, -90.0, -31.0, 31.0, 90.0, 173.0}) {
		const double radians = degrees * std::numbers::pi / 180, c = std::cos(radians), s = std::sin(radians);
		auto target = source; target.planeNormal = {s, 0, c, true}; target.planeDistance = -(s * 11 + c * 12);
		const auto wrapped = wrappedLevelTextureProjection(source, target), original = levelTextureProjection(source);
		ok &= expect(wrapped.valid, "finite analytic wrap");
		for (double x : {-51.0, 11.0, 94.0}) {
			for (double y : {-37.0, -7.0, 83.0}) {
				const LevelMapVec3 before{x, y, 12, true}, after{11 + c * (x - 11), y, 12 - s * (x - 11), true};
				ok &= expect(near(original.at(before), wrapped.at(after)), "rigid transport preserves UV at corresponding points", QString::number(degrees));
			}
		}
		// A common arbitrary orientation exercises an intersection in all axes.
		auto tiltedSource = source, tiltedTarget = target;
		for (auto* face : {&tiltedSource, &tiltedTarget}) {
			face->planeNormal = rotateLevelVector(face->planeNormal, 0, 27);
			face->uAxis = rotateLevelVector(face->uAxis, 0, 27); face->vAxis = rotateLevelVector(face->vAxis, 0, 27);
		}
		const auto tilted = wrappedLevelTextureProjection(tiltedSource, tiltedTarget);
		const LevelMapVec3 sample{11 + c * 48, 29, 12 - s * 48, true};
		ok &= expect(near(tilted.at(rotateLevelVector(sample, 0, 27)), original.at({59, 29, 12, true})), "oblique hinge transports UVs");
	}
	for (double sign : {-1.0, 1.0}) {
		auto parallel = source; parallel.planeNormal = {0, 0, sign, true}; parallel.planeDistance = -91 * sign;
		const auto wrapped = wrappedLevelTextureProjection(source, parallel), original = levelTextureProjection(source);
		ok &= expect(wrapped.valid && near(wrapped.at({34, 51, 91, true}), original.at({34, 51, 91, true})), "parallel/opposite planes use world projection");
	}
	auto invalid = source; invalid.planeNormal = {};
	ok &= expect(!wrappedLevelTextureProjection(source, invalid).valid, "invalid target plane cannot produce a wrap");
	const QStringList dialects{"classic", "valve220", "brushDef", "brushDef3"};
	int seams = 0;
	for (const auto& fromKind : dialects) {
		LevelMapDocument from; ok &= load(tests::surfaceFixture(fromKind).replace("0.0078125", "0.015625"), &from, &error);
		const auto sourceGeometry = solveBrushGeometry(from.brushes[0].faces);
		for (const auto& toKind : dialects) {
			LevelMapDocument pristine; ok &= load(tests::surfaceFixture(toKind), &pristine, &error);
			const auto targetGeometry = solveBrushGeometry(pristine.brushes[0].faces);
			for (int a = 0; a < 6; ++a) {
				for (int b = 0; b < 6; ++b) {
					const auto& ap = sourceGeometry.faces[a].plane; const auto& bp = targetGeometry.faces[b].plane;
					if (std::abs(ap.normalX * bp.normalX + ap.normalY * bp.normalY + ap.normalZ * bp.normalZ) > 0.9) { continue; }
					auto target = pristine; const auto bytes = serializeLevelMap(target).bytes;
					LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(from, {0, a}, &clipboard, &error);
					LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Seamless;
					options.textureSize = {128, 64}; options.allowValve220 = true;
					LevelSurfaceEditPlan plan;
					if (!expect(prepareLevelSurfacePaste(target, {{0, b}}, clipboard, options, &plan, &error)
						&& commitLevelSurfaceEdit(&target, plan, &error), "wrap across formats", fromKind + " -> " + toKind + ": " + error)) { return 1; }
					LevelMapDocument reopened; ok &= load(serializeLevelMap(target).bytes, &reopened, &error);
					const auto before = texels(from.brushes[0].faces[a]), after = texels(reopened.brushes[0].faces[b]);
					int shared = 0;
					for (const auto& point : targetGeometry.faces[b].points) {
						if (std::abs(point.x * ap.normalX + point.y * ap.normalY + point.z * ap.normalZ - ap.distance) < 1e-6) {
							++shared; ok &= expect(near(before.at(point), after.at(point)), "saved UVs agree along the complete shared edge", fromKind + " -> " + toKind);
						}
					}
					ok &= expect(shared == 2, "test exercised an actual brush edge"); ++seams;
					const auto oldMetric = metric(before, ap), newMetric = metric(after, bp);
					for (int i = 0; i < 3; ++i) { ok &= expect(near(oldMetric[i], newMetric[i]), "wrap preserves scale and shear on the face", fromKind + " -> " + toKind); }
					ok &= expect(undoLevelMapEdit(&target, &error) && serializeLevelMap(target).bytes == bytes, "wrap/conversion has exact undo", error);
					if (fromKind.startsWith("brushDef") != toKind.startsWith("brushDef")) {
						options.textureSize = {};
						ok &= expect(!prepareLevelSurfacePaste(target, {{0, b}}, clipboard, options, &plan, &error) && !plan.ready(), "wrap never guesses image dimensions");
					}
				}
			}
		}
	}
	{
		LevelMapDocument map, shear; ok &= load(tests::surfaceFixture("classic"), &map, &error)
			&& load(tests::surfaceFixture("brushDef").replace("0.0078125", "0.015625"), &shear, &error);
		LevelSurfaceClipboard clipboard; ok &= copyLevelSurface(shear, {0, 0}, &clipboard, &error);
		LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Seamless; options.textureSize = {128, 64};
		LevelSurfaceEditPlan plan; const auto original = serializeLevelMap(map).bytes;
		ok &= expect(!prepareLevelSurfacePaste(map, {{0, 2}}, clipboard, options, &plan, &error) && !plan.ready(), "sheared wrap requires explicit conversion consent");
		options.allowValve220 = true;
		ok &= expect(prepareLevelSurfacePaste(map, {{0, 2}}, clipboard, options, &plan, &error) && plan.convertedFaceCount() == 6, "wrap conversion covers the whole map", error);
		int calls = 0;
		ok &= expect(!prepareLevelSurfacePaste(map, {{0, 2}}, clipboard, options, &plan, &error, [&] { return ++calls > 4; })
			&& !plan.ready() && serializeLevelMap(map).bytes == original, "cancelled wrap cannot expose a partial plan");
		QString layer; ok &= createLevelSceneNode(&map, LevelSceneNodeKind::Layer, "Protected", {}, &layer, &error)
			&& assignLevelSceneObjects(&map, layer, {"brush:0"}, &error) && setLevelSceneLocked(&map, layer, true, &error);
		ok &= expect(prepareLevelSurfacePaste(map, {{0, 2}}, clipboard, options, &plan, &error) && !commitLevelSurfaceEdit(&map, plan, &error), "wrapped paste respects scene locks");
		options.mode = static_cast<LevelSurfacePasteMode>(99);
		ok &= expect(!prepareLevelSurfacePaste(map, {{0, 2}}, clipboard, options, &plan, &error) && !plan.ready(), "unknown paste mode is rejected");
	}
	std::cout << seams << " serialized cross-format seams verified.\n"; return ok ? 0 : 1;
}
