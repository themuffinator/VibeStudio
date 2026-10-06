#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/doom_preview_test_helpers.h"
#include "tests/level_geometry_test_helpers.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>

using namespace vibestudio;
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QJsonArray results;
	for (const bool doom : {false, true}) {
		for (int count : {1000, doom ? 5000 : 10000}) {
			LevelMapDocument base;
			QString error;
			if (doom) {
				LevelMapCreateRequest request;
				request.game = "doom";
				request.starterRoom = false;
				if (!createLevelMap(request, &base, &error) || !addLevelMapDoomThing(&base, 1, 0, 0, 0, nullptr, &error)) {
					return 1;
				}
				for (int i = 0; i < count; ++i) {
					LevelMapDoomSector sector;
					sector.id = i;
					sector.ceilingHeight = 128;
					base.doomSectors << sector;
					const double x = (i % 100) * 64, y = (i / 100) * 64;
					tests::doom::loop(base, {{x, y}, {x, y + 32}, {x + 32, y + 32}, {x + 32, y}}, i);
				}
			} else if (!tests::createGeometryFixture(count, &base, &error)) {
				return 1;
			}
			for (int lockedCount : {0, 1, count - 1}) {
				auto map = base;
				if (lockedCount > 0) {
					QString id;
					QStringList objects;
					for (int i = 0; i < lockedCount; ++i) {
						objects << QStringLiteral("%1:%2").arg(doom ? "sector" : "brush").arg(i);
					}
					if (!createLevelSceneNode(&map, LevelSceneNodeKind::Layer, "Locked", {}, &id, &error) ||
						!assignLevelSceneObjects(&map, id, objects, &error) || !setLevelSceneLocked(&map, id, true, &error)) {
						return 1;
					}
				}
				QJsonArray timings;
				for (int repeat = 0; repeat < 5; ++repeat) {
					QElapsedTimer timer;
					timer.start();
					if (!moveLevelMapObject(&map, doom ? "thing" : "brush", doom ? 0 : count - 1, 1, 0, 0, &error)) {
						std::cerr << error.toStdString();
						return 1;
					}
					timings << timer.nsecsElapsed() / 1000000.0;
				}
				results << QJsonObject{
					{"kind", doom ? "sectors" : "brushes"}, {"objects", count}, {"locked", lockedCount}, {"unrelatedMoveMs", timings}};
			}
		}
	}
	std::cout << QJsonDocument(results).toJson().toStdString();
	return 0;
}
