#pragma once

#include <QFontMetricsF>
#include <QImage>
#include <QLineF>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>
#include <array>
#include <atomic>

namespace vibestudio {
struct MapGridView {
	QSize viewport;
	QPointF center;
	double zoom = 1, pixelRatio = 1;
	double units = 64;
	QRgb minor = 0, major = 0, axis = 0;
	QPointF pixelPhase;
	// The compiler block grid (core/level_block_grid.h): the block size along
	// the drawn horizontal and vertical axes, 0 for none, drawn dashed in
	// `block`, `blockWidth` pixels wide, and kept inside `blockBounds`
	// (drawing-plane units) when it is valid or `clipBlocks` is set, as
	// NetRadiant Custom keeps its block grid inside the region. The explicit
	// flag preserves flat and point regions, which QRectF treats as invalid.
	double blockX = 0, blockY = 0;
	QRgb block = 0;
	double blockWidth = 2;
	QRectF blockBounds;
	bool clipBlocks = false;
	// False leaves the grid lines and axes out, so a block grid can be shown
	// with Show > Grid turned off.
	bool lines = true;
};
struct MapGridFrame { MapGridView view; QImage image; };
// Shared line placement for the image path and complete ordinary fallback.
using MapGridLines = std::array<QVector<QLineF>,3>;
MapGridLines mapGridLines(const MapGridView& view);
// The spacing the lines are drawn at: the grid, doubled until neighbouring
// lines are at least 6 device-independent pixels apart, so a fine grid at
// low zoom thins out instead of filling the view.
double mapGridDrawnSpacing(double units, double zoom);
bool sameMapGridView(const MapGridView& a, const MapGridView& b);
// The block grid's boundaries in widget pixels: every multiple of the block
// size in view along each axis, doubled while neighbouring lines would be
// under 6 pixels apart (so the lines left are still real boundaries), at most
// 1024 a side, clipped to blockBounds. Empty with no block size or when the
// region is out of view.
QVector<QLineF> mapBlockGridLines(const MapGridView& view);
// Dashed, so a block boundary never reads as a grid line or an axis.
QPen mapBlockGridPen(const MapGridView& view);

// Coordinate labels for the 2D grid, as NetRadiant Custom's Show Coordinates
// prints them, placed clear of the views' status tags.
struct MapGridCoordinateLabel {
	// The label's box in widget pixels, padding included.
	QRectF bounds;
	// The number, always read left to right.
	QString text;
	// The coordinate of the grid line it labels.
	double value = 0;
	// True for a value along the horizontal axis (a vertical line, labelled
	// along the bottom edge); false for one up the vertical axis, labelled
	// along the leading edge (left, or right in right-to-left layouts).
	bool across = true;
};
struct MapGridCoordinateLayout {
	QVector<MapGridCoordinateLabel> labels;
	// World units between neighbouring labelled lines on each axis.
	double acrossStep = 0, upStep = 0;
};
// Labels on the grid lines of the drawn spacing, doubled until neighbours are
// at least a label's width (across) or height (up) and 32 pixels apart, at
// most 256 a side. A label that would leave the pane, touch one of
// `obstacles` (the status tags) or the other edge's labels in the corner is
// left out.
MapGridCoordinateLayout mapGridCoordinateLabels(const MapGridView& view, const QFontMetricsF& metrics,
	Qt::LayoutDirection direction, const QVector<QRectF>& obstacles = {});
// A coordinate on lines `step` units apart, with the decimals the step needs
// (three for 0.125), no trailing zeros and never "-0".
QString mapGridCoordinateText(double value, double step);
// Ceil physical extent, bounded to the shared viewport image limit.
QSize mapGridImageSize(const MapGridView& view);
// Cached native Qt grid image, in minor/major/axis overdraw order (left out
// when `lines` is false), then the dashed block grid.
// Invalid/oversized requests leave the previous frame intact and return false.
bool renderMapGrid(const MapGridView& view, MapGridFrame* frame, const std::atomic_bool* cancelled = nullptr);
} // namespace vibestudio
