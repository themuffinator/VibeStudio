#pragma once

#include <QImage>
#include <QPointF>
#include <QVector>

#include <array>
#include <atomic>
#include <functional>

namespace vibestudio
{

// Renderer inputs contain no editor state or world-space geometry. The camera
// clips/project vertices before this boundary. Depth grows toward the viewer:
// view-space depth for orthographic views, reciprocal distance for perspective.
struct ModelRasterVertex
{
	QPointF screen;
	double depth = 0.0;
	double reciprocalW = 1.0;
	QPointF uv;
};

struct ModelRasterTriangle
{
	std::array<ModelRasterVertex, 3> vertices;
	// Edges opposite each vertex; clipping's internal fan edges stay hidden.
	std::array<bool, 3> edges{true, true, true};
	std::array<bool, 3> selectedEdges{false, false, false};
	int source = -1;
	QRgb color = qRgb(255, 255, 255);
	const QImage *texture = nullptr; // ARGB32 premultiplied; lives for this call.
	bool textureHasAlpha = false;
	double light = 1.0;
	bool highlighted = false;
	bool hovered = false;
};

struct ModelRasterStyle
{
	double pixelRatio = 1.0;
	bool showEdges = false;
	QRgb edge = qRgba(0, 0, 0, 150);
	QRgb hatch = qRgba(0, 0, 0, 110);
	QRgb hover = qRgb(255, 210, 90);
	QRgb selection = qRgb(255, 230, 45);
};

struct ModelRasterFrame
{
	QImage image;
	QVector<double> depth;
	QVector<int> source;
	void clear();
};

// Allocation ceiling covers colour, double-precision depth and picking IDs
// (128 MiB at the ceiling). The viewport scales larger targets to this budget.
constexpr qint64 modelRasterMaxPixels = 8 * 1024 * 1024;

// Original barycentric software renderer. Opaque fragments use a depth buffer;
// translucent fragments are sorted at each pixel in small tiles, so crossing
// transparent faces remain correct without an unbounded per-frame fragment list.
// A transparent output composites over the viewport grid with ordinary QPainter.
bool renderModelRaster(const QSize &size, const QVector<ModelRasterTriangle> &triangles, const ModelRasterStyle &style,
					   ModelRasterFrame *output, const std::atomic_bool *cancelled = nullptr);

// Same coverage, perspective UVs, alpha sampling and nearest-depth rule as the
// renderer, in the coordinates supplied by the caller. Does not drive UI input.
int pickModelRaster(const QPointF &point, const QVector<ModelRasterTriangle> &triangles);

// A conservative screen-space index for repeated continuous picks. Large faces
// use a shared list; cell references have a fixed memory budget. Exact coverage,
// depth and texture alpha still come from the same sampler as an unindexed pick.
// Query it only with the same immutable triangle array used to build it.
struct ModelRasterPickIndex
{
	QSize size;
	QVector<QVector<int>> cells;
	QVector<int> broad;
};
bool buildModelRasterPickIndex(const QSize &size, const QVector<ModelRasterTriangle> &triangles, ModelRasterPickIndex *output,
							   const std::atomic_bool *cancelled = nullptr);
int pickModelRaster(const QPointF &point, const QVector<ModelRasterTriangle> &triangles, const ModelRasterPickIndex &index);

bool modelTextureHasAlpha(const QImage &image);

struct ModelWireSegment
{
	QPointF a, b;
	bool selected = false;
};

struct ModelWireStyle
{
	double pixelRatio = 1, width = 1, selectionWidth = 2.4;
	QRgb wire = qRgb(214, 222, 234), selection = qRgb(255, 170, 60);
	// Selected stroke pattern in stroke-width units; zero length makes it solid.
	// The UV view also uses this for dotted seams and solid contrast outlines.
	double dashLength = 4, dashGap = 2;
};

// Original antialiased line renderer. Coordinates and stroke widths are logical;
// pixelRatio maps both to the bounded image. Every edge remains visible through
// the mesh. Selected edges are dashed and composited after ordinary edges.
bool renderModelWireframe(const QSize &size, const QVector<ModelWireSegment> &segments, const ModelWireStyle &style, QImage *output,
						  const std::atomic_bool *cancelled = nullptr);

// Composite another ordered wire batch over an initialized, bounded ARGB32
// premultiplied image. Shared by model previews and level plan views. Does not
// allocate or clear the image. Discard a partial frame if cancellation returns false.
// Both cancellation routes poll inside long strokes. Opaque tile reuse is local
// to a colour pass and skips only pixels proven unchanged; alpha strokes retain
// ordinary coverage and compositing.
bool paintModelWireframe(QImage *output, const QVector<ModelWireSegment> &segments, const ModelWireStyle &style,
						 const std::atomic_bool *cancelled = nullptr, const std::function<bool()> &cancelledCallback = {});

} // namespace vibestudio
