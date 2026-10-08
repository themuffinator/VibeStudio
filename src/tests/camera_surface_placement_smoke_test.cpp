#include "core/camera_surface_placement.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/doom_preview_test_helpers.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &detail = {})
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool close(double a, double b)
{
	return std::abs(a - b) < 1e-7;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	const std::array<BoxResizePoint, 3> triangle{{{-128, -128, 0}, {128, -128, 0}, {0, 128, 0}}};
	CameraSurfacePoint hit;
	ok &= expect(cameraSurfacePoint({{0, 0, 100}, {0, 0, -2}, true}, triangle, &hit) && hit.position == BoxResizePoint{0, 0, 0} &&
					 hit.normal == BoxResizePoint{0, 0, 1},
				 "exact surface intersection and unit facing normal");
	ok &= expect(cameraSurfacePoint({{0, 0, -100}, {0, 0, 2}, true}, triangle, &hit) && hit.normal == BoxResizePoint{0, 0, -1},
				 "two-sided surface faces the camera");
	const auto original = hit;
	for (const auto &ray : {BoxResizeRay{{200, 200, 100}, {0, 0, -1}, true}, BoxResizeRay{{0, 0, 100}, {0, 0, 1}, true},
							BoxResizeRay{{0, 0, 100}, {1, 0, 0}, true}, BoxResizeRay{{0, 0, 0}, {0, 0, 0}, true},
							BoxResizeRay{{0, 0, std::numeric_limits<double>::infinity()}, {0, 0, -1}, true}})
	{
		ok &= expect(!cameraSurfacePoint(ray, triangle, &hit) && hit.position == original.position && hit.normal == original.normal,
					 "misses, backwards, parallel and invalid rays preserve result");
	}
	auto degenerate = triangle;
	degenerate[2] = degenerate[0];
	ok &= expect(!cameraSurfacePoint({{0, 0, 100}, {0, 0, -1}, true}, degenerate, &hit), "degenerate triangle rejected");
	const ResizeBox bounds{{-16, -16, -24}, {16, 16, 32}};
	BoxResizePoint origin;
	ok &= expect(cameraSurfacePlacement({{3, 5, 0}, {0, 0, 1}}, bounds, 0, 16, &origin) && origin == BoxResizePoint{0, 0, 32},
				 "snapped origin keeps asymmetric entity bounds outside floor");
	ok &= expect(cameraSurfacePlacement({{3, 5, 100}, {0, 0, -1}}, bounds, 0, 16, &origin) && origin == BoxResizePoint{0, 0, 64},
				 "ceiling placement rounds away from plane");
	ok &= expect(cameraSurfacePlacement({{3, 5, 0}, {0, 0, 1}}, bounds, 7, 0, &origin) && origin == BoxResizePoint{3, 5, 31},
				 "unsnapped clearance retains tangential coordinates");
	int slopeCases = 0;
	for (int x : {-3, -1, 0, 1, 3})
	{
		for (int y : {-2, 0, 2})
		{
			for (int z : {-3, -1, 0, 1, 3})
			{
				if (!x && !y && !z)
				{
					continue;
				}
				const double length = std::hypot(double(x), double(y), double(z));
				CameraSurfacePoint surface{{11.3, -23.7, 37.1}, {x / length, y / length, z / length}};
				ok &= cameraSurfacePlacement(surface, bounds, 3, 16, &origin);
				for (int axis = 0; axis < 3; ++axis)
				{
					ok &= expect(close(std::remainder(origin[axis], 16), 0), "sloped placement preserves grid on every axis");
				}
				for (int corner = 0; corner < 8; ++corner)
				{
					double distance = 0;
					for (int axis = 0; axis < 3; ++axis)
					{
						distance +=
							surface.normal[axis] *
							(origin[axis] + ((corner & (1 << axis)) ? bounds.maxs[axis] : bounds.mins[axis]) - surface.position[axis]);
					}
					ok &= expect(distance >= 3 - 1e-7, "every bound corner remains outside slope with requested clearance");
				}
				++slopeCases;
			}
		}
	}
	const BoxResizePoint saved{1, 2, 3};
	origin = saved;
	ok &= expect(!cameraSurfacePlacement({{32767, 0, 0}, {1, 0, 0}}, bounds, 0, 16, &origin) && origin == saved,
				 "out-of-range placement is atomic");
	ok &= expect(!cameraSurfacePlacement({{0, 0, 0}, {0, 0, 0}}, bounds, 0, 16, &origin), "zero normal rejected");
	ok &= expect(!cameraSurfacePlacement({{0, 0, 0}, {0, 0, 2}}, bounds, 0, 16, &origin), "non-unit normal rejected");
	ok &= expect(!cameraSurfacePlacement({{0, 0, 0}, {0, 0, 1}}, bounds, -1, 16, &origin), "negative clearance rejected");
	ok &= expect(!cameraSurfacePlacement({{0, 0, 0}, {0, 0, 1}}, bounds, 0, std::numeric_limits<double>::quiet_NaN(), &origin),
				 "invalid grid rejected");
	ResizeBox reversed = bounds;
	reversed.mins[0] = 30;
	ok &= expect(!cameraSurfacePlacement({{0, 0, 0}, {0, 0, 1}}, reversed, 0, 16, &origin), "invalid definition bounds rejected");

	for (const auto &game : {QStringLiteral("quake"), QStringLiteral("quake3"), QStringLiteral("doom"), QStringLiteral("hexen")})
	{
		LevelMapCreateRequest create;
		create.game = game;
		LevelMapDocument source;
		if (!expect(createLevelMap(create, &source, &error), "create original placement fixture", error))
		{
			return 1;
		}
		const bool quake = source.format != LevelMapFormat::DoomWad;
		const auto before = serializeLevelMap(source).bytes;
		LevelPlacementRequest request;
		request.operation = quake ? LevelPlacementOperation::AddEntity : LevelPlacementOperation::AddThing;
		request.className = QStringLiteral("light");
		request.thingType = 3004;
		request.thingAngle = 90;
		request.offset = {32, 48, 80, true};
		request.properties = {{QStringLiteral("light"), QStringLiteral("200"), 0}};
		const auto result = prepareLevelPlacement(source, request);
		if (!expect(result.succeeded, "shared placement candidate", game + result.error))
		{
			ok = false;
			continue;
		}
		auto candidate = result.document;
		ok &= expect(serializeLevelMap(source).bytes == before && candidate.undoStack.size() == source.undoStack.size() + 1 &&
						 candidate.revision == source.revision + 1,
					 "preparation preserves source and creates one history entry");
		const auto after = serializeLevelMap(candidate).bytes;
		LevelMapDocument loaded;
		ok &= expect(loadLevelMapBytes({quake ? QStringLiteral("placed.map") : QStringLiteral("placed.wad"),
										quake ? QString() : QStringLiteral("MAP01"), game},
									   after, &loaded, &error),
					 "save/reload placement", error);
		if (quake)
		{
			const auto &entity = loaded.entities.last();
			ok &= expect(entity.className == request.className && entity.origin.x == 32 && entity.origin.y == 48 && entity.origin.z == 80,
						 "saved entity origin");
		}
		else
		{
			const auto &thing = loaded.doomThings.last();
			ok &= expect(thing.type == 3004 && thing.x == 32 && thing.y == 48 && thing.angle == 90, "saved thing type and origin");
		}
		ok &= expect(undoLevelMapEdit(&candidate, &error) && serializeLevelMap(candidate).bytes == before, "exact placement undo", error);
		ok &= expect(redoLevelMapEdit(&candidate, &error) && serializeLevelMap(candidate).bytes == after, "exact placement redo", error);
		LevelPlacementControl cancel;
		cancel.isCancelled = [] { return true; };
		const auto cancelled = prepareLevelPlacement(source, request, cancel);
		ok &= expect(cancelled.cancelled && !cancelled.succeeded && serializeLevelMap(source).bytes == before,
					 "cancelled placement never mutates source");
		QString layer;
		ok &= createLevelSceneNode(&source, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error);
		ok &= setLevelSceneLocked(&source, layer, true, &error);
		source.activeSceneNode = layer;
		const auto blocked = prepareLevelPlacement(source, request);
		ok &= expect(!blocked.succeeded && !blocked.error.isEmpty(), "locked creation destination rejects camera placement");
	}
	std::cout << "Camera surface placement: " << slopeCases << " slope orientations, ray validation, four map formats and history "
			  << (ok ? "passed" : "failed") << '\n';
	return ok ? 0 : 1;
}
