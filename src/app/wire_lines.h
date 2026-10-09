#pragma once

// Antialiased 2D line drawing for the plan views and the UV editor.
//
// These views are flat drawings of lines over QPainter, cached as images so
// panning and zooming stay quick; nothing here projects or rasterizes 3D
// geometry (3D views render on the GPU, see core/render_device.h). Strokes
// are integrated over each square pixel, so thin diagonal edges keep an even
// weight, and selected strokes are dashed and composited after ordinary ones.

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QVector>

#include <atomic>
#include <functional>

namespace vibestudio {

struct WireSegment {
	QPointF a, b;
	bool selected = false;
};

struct WireStyle {
	double pixelRatio = 1, width = 1, selectionWidth = 2.4;
	QRgb wire = qRgb(214, 222, 234), selection = qRgb(255, 170, 60);
	// Selected stroke pattern in stroke-width units; zero length makes it solid.
	// The UV view also uses this for dotted seams and solid contrast outlines.
	double dashLength = 4, dashGap = 2;
};

// Coordinates and stroke widths are logical; pixelRatio maps both to the
// bounded image. Every edge remains visible. Selected edges are dashed and
// composited after ordinary edges.
bool renderWireLines(const QSize& size, const QVector<WireSegment>& segments, const WireStyle& style, QImage* output,
	const std::atomic_bool* cancelled = nullptr);

// Composite another ordered line batch over an initialized, bounded ARGB32
// premultiplied image. Shared by the UV editor and level plan views. Does not
// allocate or clear the image. Discard a partial frame if cancellation returns
// false. Both cancellation routes poll inside long strokes. Opaque tile reuse
// is local to a colour pass and skips only pixels proven unchanged; alpha
// strokes retain ordinary coverage and compositing.
bool paintWireLines(QImage* output, const QVector<WireSegment>& segments, const WireStyle& style, const std::atomic_bool* cancelled = nullptr,
	const std::function<bool()>& cancelledCallback = {});

} // namespace vibestudio
