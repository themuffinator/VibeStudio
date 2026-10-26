#pragma once

#include "core/level_brush.h"

#include <QJsonObject>

namespace vibestudio {
// Create Brush from Points and Copy Vertex Coordinates: TrenchBroom's debug
// Create Brush… and Print Vertices as ordinary commands. A typed point list
// becomes the convex brush that encloses it, through the shared envelope
// builder and hull insertion (core/level_brush.h, core/level_primitive.h);
// brushes, faces, edges and picked vertices list their corners as text that
// reads back as points.

inline constexpr int kLevelBrushPointsMinimum = 4;
inline constexpr int kLevelBrushPointsMaximum = 4096;

// Reads typed points. Accepts `x,y,z` groups separated by spaces, semicolons
// or new lines; `x y z`, one or more whole points per line; and TrenchBroom's
// `(x y z) (x y z) …`. Each line takes one form, so lines may differ.
// Everything after `//` on a line is a comment, so the
// text levelVertexListingText() writes reads back. Numbers use a full stop as
// the decimal mark. Fails, leaving `points` empty, on a malformed point or
// more than 4096 points; an empty text gives no points and no error.
bool parseLevelBrushPoints(const QString& text, QVector<LevelMapVec3>* points, QString* error = nullptr);

// The convex envelope of 4 to 4096 finite points within ±32768 units that do
// not all lie on one plane. Duplicate and interior points, and points inside a
// face or on an edge, are absorbed; the topology lists the corners the brush
// will have, within the 128-face / 256-vertex component limits. No document.
bool levelBrushPointsHull(const QVector<LevelMapVec3>& points, const QString& texture, LevelBrushTopology* topology,
	QString* error = nullptr);

struct LevelBrushPointsRequest {
	QVector<LevelMapVec3> points;
	QString texture;
};
struct LevelBrushPointsReport {
	int brushId = -1;
	int inputPoints = 0;
	// Points that became corners, and those the envelope absorbed.
	int corners = 0;
	int unusedPoints = 0;
	int faces = 0;
	QString faceDialect;
};
// Adds the convex brush enclosing the points, with `texture` on every face,
// in the face format of the map's first brush, to worldspawn and the current
// creation destination (the open group or the active layer), selected, as one
// undo step. Scene locks and the open group are honoured, a linked group's
// copies follow, and a failure leaves the map unchanged.
// Workflow reference: TB MapWindow::debugCreateBrush at 4c756be1
// (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.
bool addLevelMapBrushFromPoints(LevelMapDocument* document, const LevelBrushPointsRequest& request,
	LevelBrushPointsReport* report = nullptr, QString* error = nullptr);
QJsonObject levelBrushPointsReportJson(const LevelBrushPointsReport& report);

// The shortest decimal text that reads back as exactly this double; -0 is 0.
QString levelMapCoordinateText(double value);
// "(x y z)" with levelMapCoordinateText() components.
QString levelMapPointText(const LevelMapVec3& point);

struct LevelVertexListing {
	LevelMapSelectionRef object;
	// Whole: every corner of a brush, in its vertex ID order. Otherwise the
	// face's winding, the edge's two ends, or the picked vertices or patch
	// control points in the order given.
	bool whole = true;
	LevelBrushComponent kind = LevelBrushComponent::Vertex;
	QVector<int> components;
	QString texture; // A face's material.
	QVector<LevelMapVec3> points;
};
// One listing per brush in `brushIds`, then one per picked face or edge and
// one per brush or patch with picked vertices or control points, in the order
// given. Positions are the solved corners at full precision. Fails on a
// missing object or component, an unsolvable brush, or more than 4096
// listings; nothing is changed.
// Workflow reference: TB MapWindow::debugPrintVertices at 4c756be1
// (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.
bool levelMapVertexListings(const LevelMapDocument& document, const QVector<int>& brushIds,
	const QVector<LevelMapComponentSet>& components, QVector<LevelVertexListing>* listings, QString* error = nullptr);
// Each listing as a `// brush:3`, `// brush:3 face:2 base/metal`,
// `// brush:3 edge:5`, `// brush:3 vertex:0,4` or `// patch:1 control:0,2`
// line followed by its points as `(x y z) (x y z) …`, ending with a new line.
QString levelVertexListingText(const QVector<LevelVertexListing>& listings);
int levelVertexListingPointCount(const QVector<LevelVertexListing>& listings);
} // namespace vibestudio
