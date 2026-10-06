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
} // namespace vibestudio
