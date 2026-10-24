#pragma once

#include "core/level_brush.h"

namespace vibestudio
{
struct LevelBrushPrimitiveRequest {
	QString shape = QStringLiteral("box"); // box, wedge, cylinder, cone, sphere
	LevelMapVec3 mins{-64, -64, -64, true};
	LevelMapVec3 maxs{64, 64, 64, true};
	int axis = 2; // Cylinder/cone axis, sphere poles, wedge height.
	int sides = 8;
	int bands = 4; // Sphere latitude bands including the two pole fans.
	QString texture = QStringLiteral("common/caulk");
};

QString levelBrushPrimitiveLabel(const QString &shape);

// Vertices fit the requested world-aligned bounds, including odd-sided shapes.
// Wedges rise along local X and extrude along local Y; local XYZ maps to
// YZX, ZXY or XYZ for an X, Y or Z axis respectively. No assets are read.
// Bounded by the shared component editor's 128 faces / 256 vertices.
bool createLevelBrushPrimitive(const LevelBrushPrimitiveRequest &request, LevelMapBrush *brush, QString *error = nullptr);

// Adds one closed brush to worldspawn using the map's current face dialect.
// A single undo step restores the source exactly. Failure leaves it unchanged.
bool addLevelMapBrushPrimitive(LevelMapDocument *document, const LevelBrushPrimitiveRequest &request, int *brushId = nullptr,
							   QString *error = nullptr);
// Adds one convex brush per point set to worldspawn, in the face style of
// the map's first brush, as a single undo step that selects them all. Every
// point must be a corner of its hull. Failure leaves the map unchanged.
struct LevelHullInsertionOptions {
	// None uses worldspawn/current creation context. A brush, patch or brush
	// entity chooses its native owner and scene membership; brushes also choose
	// their face dialect. Sources remain intact.
	LevelMapSelectionRef destination;
	bool keepSelection = false;
};
bool addLevelMapBrushHulls(LevelMapDocument *document, const QVector<QVector<LevelMapVec3>> &hulls, const QString &texture,
						   const QString &description, const QString &undoDescription, QVector<int> *brushIds = nullptr,
						   QString *error = nullptr, const LevelHullInsertionOptions& options = {});
struct LevelBrushHullInsertion {
	QVector<LevelMapVec3> vertices;
	LevelMapSelectionRef destination;
};
// One transaction across different native owners, scene groups and face dialects.
// Used by sweep profiles; all sources remain intact. Up to 4096 generated hulls.
bool addLevelMapBrushHullBatch(LevelMapDocument* document, const QVector<LevelBrushHullInsertion>& hulls,
	const QString& texture, const QString& description, const QString& undoDescription,
	QVector<int>* brushIds = nullptr, QString* error = nullptr, bool keepSelection = false);
} // namespace vibestudio
