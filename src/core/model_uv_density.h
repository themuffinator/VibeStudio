#pragma once

#include "core/model_uv.h"
#include <QJsonObject>
#include <QSize>

namespace vibestudio
{
struct ModelEdit;
struct ModelSelection;

struct ModelUvDensityOptions
{
	QSize textureSize{512, 512};
	int referenceFrame = 0;
	double target = 1;		// pixels per model unit, using square-root area ratio
	bool individual = true; // false applies one factor, preserving relative density
	bool operator==(const ModelUvDensityOptions &) const = default;
};
struct ModelUvDensityIsland
{
	int island = -1;
	QVector<int> faces;
	double modelArea = 0, pixelArea = 0, density = 0, minimum = 0, maximum = 0;
	int collapsedGeometry = 0, collapsedUvs = 0, mirroredFaces = 0;
	bool scalable() const { return modelArea > 0 && pixelArea > 0 && !collapsedGeometry && !collapsedUvs; }
};
struct ModelUvDensityReport
{
	QSize textureSize;
	int referenceFrame = 0;
	QVector<ModelUvDensityIsland> islands;
	double modelArea = 0, pixelArea = 0, density = 0;
	QSet<int> faces;
};

// Empty faces inspect the whole surface; otherwise expand to complete indexed
// UV islands. Areas count every face, including overlapping or mirrored UVs.
// Texture dimensions are the original image size, never a downsampled preview.
// Outputs remain unchanged on failure/cancellation.
bool inspectModelUvDensity(const ModelSurface &surface, const QSet<int> &faces, QSize textureSize, int referenceFrame,
						   ModelUvDensityReport *report, QString *error = nullptr, const ModelWorkControl &control = {});
QJsonObject modelUvDensityJson(const ModelUvDensityReport &report);
// Candidate-only document operation. Selected components expand to islands;
// an empty selection refuses. Per-island centres, seams and every pose survive.
bool applyModelUvDensity(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error,
						 const ModelWorkControl &control = {});
} // namespace vibestudio
