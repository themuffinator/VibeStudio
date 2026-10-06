#pragma once

#include <QBitArray>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSet>
#include <QSize>
#include <QVector>
#include <array>
#include <atomic>

namespace vibestudio
{
// Immutable projection shared by the render worker and exact vertex picker.
// Each finite vertex occupies one cell; screen-edge cells also retain vertices
// outside the viewport so X-ray queries keep their continuous-coordinate rules.
struct ModelVertexProjection
{
	static constexpr int cellCount = 32;
	int surface = -1;
	QSize logicalSize;
	QVector<QPointF> positions;
	QBitArray visible;
	std::array<QVector<int>, cellCount * cellCount> cells;
};

bool indexModelVertices(ModelVertexProjection *projection, const std::atomic_bool *cancelled = nullptr);
int pickModelVertex(const ModelVertexProjection &projection, QPointF point, double tolerance, bool xray);
bool renderModelVertexOverlay(const ModelVertexProjection &projection, QSize size, double pixelRatio, const QSet<int> &selected, bool xray,
							  bool transforming, QColor accent, QImage *image, const std::atomic_bool *cancelled = nullptr);
} // namespace vibestudio
