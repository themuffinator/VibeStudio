#pragma once

#include "core/model_document.h"

#include <QColor>
#include <QTransform>
#include <memory>

namespace vibestudio
{
enum class ModelUvFit
{
	None,
	All,
	Selection
};
struct ModelUvRenderRequest
{
	ModelSurface surface;
	ModelSelection selection;
	QImage texture;
	// Optional immutable cache produced for this exact triangle/UV/seam layout.
	std::shared_ptr<const ModelUvTopology> topology;
	QSize logicalSize;
	double pixelRatio = 1;
	QTransform camera;
	ModelUvFit fit = ModelUvFit::All;
	ModelTexCoord moveOffset;
	bool moving = false, showVertices = false;
	QColor background, foreground, accent;
};
struct ModelUvRenderResult
{
	QImage image;
	QTransform camera;
	std::shared_ptr<const ModelUvTopology> topology;
	QRectF selectionBounds;
	bool hasSelection = false;
};
// Coordinate-only selection bounds; no frame positions or normals are read.
QRectF modelUvSelectionBounds(const ModelSurface &surface, const ModelSelection &selection, bool *available = nullptr);
// Runs on a value-only worker. Bounded to 4,194,304 pixels even for extreme aspect
// ratios. Selection is a clipped winding union; complete indexed wires share the
// antialiased 2D line painter (app/wire_lines), with seams/selection above ordinary geometry.
// Cancellation never publishes a partial image, camera or topology.
bool renderModelUv(const ModelUvRenderRequest &request, ModelUvRenderResult *result, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
