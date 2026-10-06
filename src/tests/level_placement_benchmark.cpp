#include "core/level_placement.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

using namespace vibestudio;
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QJsonArray rows;
	QVector<int> counts{100, 1000, 10000};
	int repetitions = 3;
	if (argc > 1) {
		counts = {QString::fromLocal8Bit(argv[1]).toInt()};
	}
	if (argc > 2) {
		repetitions = QString::fromLocal8Bit(argv[2]).toInt();
	}
	if (argc > 3 || repetitions < 1 || repetitions > 10 || counts.first() < 1 || counts.first() > 10000) {
		std::cerr << "Usage: level_placement_benchmark [1..10000 brushes [1..10 repetitions]]\n";
		return 1;
	}
	for (int count : counts) {
		LevelMapDocument base;
		QString error;
		if (!tests::createGeometryFixture(count, &base, &error)) {
			std::cerr << error.toStdString();
			return 1;
		}
		QVector<LevelMapSelectionRef> selected;
		for (auto& brush : base.brushes) {
			for (auto& face : brush.faces) {
				for (auto* p : {&face.p0, &face.p1, &face.p2}) {
					p->x += 3;
					p->y += 3;
					p->z += 3;
				}
			}
			brush.mins.x += 3;
			brush.mins.y += 3;
			brush.mins.z += 3;
			brush.maxs.x += 3;
			brush.maxs.y += 3;
			brush.maxs.z += 3;
			selected << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, brush.id};
		}
		setLevelMapSelection(&base, selected);
		for (int operation = 0; operation < 2; ++operation) {
			QJsonArray timings, phases;
			for (int iteration = 0; iteration < repetitions; ++iteration) {
				auto map = base;
				QElapsedTimer timer;
				timer.start();
				LevelPlacementRequest request;
				request.operation = operation == 0 ? LevelPlacementOperation::Snap : LevelPlacementOperation::Duplicate;
				request.offset = {16, 0, 0, true};
				QJsonArray phaseTimes;
				int phase = -1;
				qint64 phaseStart = 0;
				const auto record = [&](int next) {
					const auto now = timer.nsecsElapsed();
					if (phase >= 0) {
						phaseTimes << QJsonObject{{"phase", phase}, {"milliseconds", (now - phaseStart) / 1000000.0}};
					}
					phase = next;
					phaseStart = now;
				};
				LevelPlacementControl control;
				control.progress = [&](const auto& p) {
					if (phase != static_cast<int>(p.phase)) {
						record(static_cast<int>(p.phase));
					}
				};
				const auto result = prepareLevelPlacement(map, request, control);
				const bool valid = result.succeeded;
				error = result.error;
				record(-1);
				phases << phaseTimes;
				if (!valid) {
					std::cerr << error.toStdString();
					return 1;
				}
				timings << timer.nsecsElapsed() / 1000000.0;
				std::cerr << count << " brushes, " << (operation == 0 ? "snap" : "duplicate") << ": " << timings.last().toDouble()
						  << " ms\n";
			}
			rows << QJsonObject{{"brushes", count},
								{"operation", operation == 0 ? "snap" : "duplicate"},
								{"textureLock", true},
								{"milliseconds", timings},
								{"phases", phases}};
		}
	}
	std::cout << QJsonDocument(QJsonObject{{"fixture", "synthetic boxes; service timing only"}, {"results", rows}}).toJson().toStdString();
	return 0;
}
