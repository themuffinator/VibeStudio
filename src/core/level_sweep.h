#pragma once

#include "core/level_primitive.h"
#include <QJsonObject>

namespace vibestudio
{
enum class LevelSweepPath { Arc, Straight, SBend };
struct LevelSweepRequest {
	QVector<LevelMapComponentSet> faces;
	LevelMapVec3 translation{0, 0, 0, true}, rotationAxis{0, 0, 1, true}, scale{1, 1, 1, true};
	double angle = 0; // Degrees; shortest turn, as TB's effective quaternion rotation.
	int segments = 8, iterations = 1;
	LevelSweepPath path = LevelSweepPath::Arc;
	bool integerAlignment = true;
	QString texture;
};
struct LevelSweepProfile {
	int brushId = -1, faceId = -1;
	QVector<LevelMapVec3> vertices;
};
struct LevelSweepSource {
	QVector<LevelSweepProfile> profiles;
	LevelMapVec3 centre, normal, scaleArm;
};
struct LevelSweepSkipped {
	int brushId = -1, faceId = -1, iteration = 0, segment = 0; // One-based section numbers.
	QString reason;
};
struct LevelSweepReport {
	int sourceFaces = 0, attempted = 0;
	QVector<int> brushIds;
	QVector<LevelSweepSkipped> skipped;
};
struct LevelSweepGeometry {
	LevelSweepSource source;
	QVector<LevelBrushHullInsertion> hulls;
	QVector<QVector<LevelMapVec3>> destinationCaps;
	LevelSweepReport report;
};
// TB v2026.2 sweep semantics: combined face-bounds centre, averaged normal,
// current material/default UVs, independent profile hulls, cumulative affine
// iterations and integer stations (the original source station stays exact).
// Degenerate sections are omitted and explicitly reported, as in TB. No valid
// sections is a failure. Limit: 64 faces, 64 segments, 8 iterations, 4096 hulls.
// These services do not alter source faces or textures. See docs/CREDITS.md.
bool levelSweepSource(const LevelMapDocument& document, const QVector<LevelMapComponentSet>& faces, LevelSweepSource* source,
                      QString* error = nullptr);
bool buildLevelSweep(const LevelMapDocument& document, const LevelSweepRequest& request, LevelSweepGeometry* geometry,
                     QString* error = nullptr);
bool sweepLevelMap(LevelMapDocument* document, const LevelSweepRequest& request, LevelSweepReport* report = nullptr,
                   QString* error = nullptr);
QJsonObject levelSweepReportJson(const LevelSweepReport& report);
} // namespace vibestudio
