#pragma once

#include "core/level_brush.h"

namespace vibestudio
{
// How a cylinder, cone or sphere sits in its box. Box stretches the corners
// to the box's sides, with a corner on the first axis across the depth axis,
// so the shape fills the box exactly (an ellipsoid for an oblong sphere).
// Circle builds it as NetRadiant Custom's Brush > Prism, Cone and Sphere do:
// the sides touch a circle centred in the box and as wide as its larger
// extent across the depth axis, with a face square to the first axis; a
// sphere is a true sphere on the box's largest extent, its corners on the
// sphere. A Circle shape may overhang the box's shorter sides. Boxes and
// wedges always fill the box.
enum class LevelBrushPrimitiveFit { Box, Circle };

struct LevelBrushPrimitiveRequest {
	QString shape = QStringLiteral("box"); // box, wedge, cylinder, cone, sphere
	LevelMapVec3 mins{-64, -64, -64, true};
	LevelMapVec3 maxs{64, 64, 64, true};
	int axis = 2; // Cylinder/cone axis, sphere poles, wedge height.
	int sides = 8;
	int bands = 4; // Sphere latitude bands including the two pole fans.
	QString texture = QStringLiteral("common/caulk");
	LevelBrushPrimitiveFit fit = LevelBrushPrimitiveFit::Box;
};

QString levelBrushPrimitiveLabel(const QString &shape);
// "box" and "circle", as the CLI's --fit and saved settings spell them.
QString levelBrushPrimitiveFitId(LevelBrushPrimitiveFit fit);
bool levelBrushPrimitiveFitFromId(const QString &id, LevelBrushPrimitiveFit *fit);
// The most sides a radial primitive takes within the component editor's
// 128-face / 256-vertex bound: 126 for a cylinder (two caps), 127 for a cone
// and 64 for a sphere, whose sides × bands is at most 128 faces. Zero for
// boxes, wedges and unknown shapes.
int levelBrushPrimitiveMaxSides(const QString &shape);

// The corners a primitive's hull is built from, in world coordinates rounded
// to 1e-6, for the request's bounds, axis, detail and fit. No assets are read.
bool levelBrushPrimitivePoints(const LevelBrushPrimitiveRequest &request, QVector<LevelMapVec3> *points, QString *error = nullptr);

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
struct LevelBrushHullRebuild {
	int brushId = -1;
	QVector<LevelMapVec3> vertices;
};
// Rebuilds each brush in place as the convex hull of its points, every point
// a corner, as one undo step, as NetRadiant Custom's Brush > Prism rebuilds the
// selected brush: it keeps its id, owner (worldspawn or a brush entity), layer
// or group, place in the file, comments and face format, and the selection
// stays as it was. New faces take `texture` with that format's default
// mapping and the brush's most used content flags. All or nothing, for 1 to
// 256 distinct brushes; scene locks and an open group's bounds are honoured.
bool rebuildLevelMapBrushHulls(LevelMapDocument* document, const QVector<LevelBrushHullRebuild>& rebuilds, const QString& texture,
	const QString& description, const QString& undoDescription, QString* error = nullptr);
} // namespace vibestudio
