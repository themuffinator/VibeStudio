#pragma once

#include "core/level_map.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// The brush shapes the Levels Shapes tab and `map add-shape` build, each
// fitted to a box: the one-brush primitives (box, wedge, cylinder, cone,
// sphere) and the shapes made of several brushes (arch, ring, stairs, room),
// in the manner of the object tools in Hammer, J.A.C.K. and Sledge.
struct LevelShapeRequest {
	QString shape = QStringLiteral("box");
	LevelMapVec3 mins {-64, -64, -64, true};
	LevelMapVec3 maxs {64, 64, 64, true};
	// The depth axis: a cylinder's or cone's length, a sphere's poles, a
	// wedge's height, and the axis arches and rings turn about.
	int axis = 2;
	// Sides of cylinders, cones and spheres; segments of arches and rings.
	int sides = 8;
	// Latitude bands of a sphere.
	int bands = 4;
	// Wall thickness of arches, rings and rooms, in map units.
	double thickness = 16;
	// An arch's sweep and where it starts, in degrees. Angles are measured in
	// the plane across the depth axis from that view's right towards its up:
	// +X towards +Y about Z, +X towards +Z about Y, +Y towards +Z about X. So
	// the default arch drawn in a front or side view is a doorway.
	double arc = 180;
	double startAngle = 0;
	// Stairs: how many steps, and which way they climb: "auto" (towards the
	// far end of the longer side), "+x", "-x", "+y" or "-y". Stairs always
	// rise along Z.
	int steps = 8;
	QString rise = QStringLiteral("auto");
	QString texture = QStringLiteral("common/caulk");
};

[[nodiscard]] QStringList levelShapeIds();
[[nodiscard]] bool isLevelShape(const QString& shape);
[[nodiscard]] QString levelShapeLabel(const QString& shape);
[[nodiscard]] QString levelShapeDescription(const QString& shape);
// The glyph that stands for the shape in the studio's icon set.
[[nodiscard]] QString levelShapeIconName(const QString& shape);
// True for the shapes made of more than one brush.
[[nodiscard]] bool levelShapeIsCompound(const QString& shape);
// The settings a shape reads: any of "axis", "sides", "bands", "thickness",
// "arc", "start", "steps" and "rise".
[[nodiscard]] QStringList levelShapeParameters(const QString& shape);
[[nodiscard]] QStringList levelShapeRiseIds();

// The convex point sets of a compound shape's brushes, rounded to whole
// units so neighbouring brushes share their corners exactly.
bool levelShapeHulls(const LevelShapeRequest& request, QVector<QVector<LevelMapVec3>>* hulls, QString* error = nullptr);

// Adds the shape to worldspawn in the map's own face style as one undo step
// and selects its brushes. Failure leaves the map unchanged.
bool addLevelMapShape(LevelMapDocument* document, const LevelShapeRequest& request, QVector<int>* brushIds = nullptr, QString* error = nullptr);

// The combined bounds of world brushes, for a shape to fill. False when a
// brush is missing, unsolved, or belongs to a brush entity.
bool levelMapBrushesBounds(const LevelMapDocument& document, const QVector<int>& brushIds, LevelMapVec3* mins, LevelMapVec3* maxs,
	QString* error = nullptr);

// Replaces world brushes with one shape filling their combined bounds, in
// the manner of Radiant's arbitrary-sided brush commands, as one undo step
// that selects the new brushes. The request's own bounds are ignored.
bool replaceLevelMapBrushesWithShape(LevelMapDocument* document, const QVector<int>& brushIds, const LevelShapeRequest& request,
	QVector<int>* newBrushIds = nullptr, QString* error = nullptr);

} // namespace vibestudio
