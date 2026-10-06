#pragma once
#include "core/model_uv_atlas.h"

namespace vibestudio
{
struct ModelUvObstacleOptions
{
	ModelUvAtlasOptions atlas;
	bool preserveScale = false;
	quint64 workLimit = 250000000;
};
struct ModelUvObstacleReport
{
	int charts = 0, fixedFaces = 0, relatedSurfaces = 0;
	double scale = 1;
	quint64 workUnits = 0;
	qsizetype rasterBytes = 0;
};
// Pack complete selected islands around unselected faces and other surfaces
// sharing a normalized nonempty material slot. Fixed UVs must lie in [0,1].
// A conservative texel mask preserves gaps of at least padding pixels; chart
// orientation and relative scale remain fixed. Fit is a bounded deterministic
// heuristic, not a proof of maximum density. Preserve-scale either fits at the
// authored scale or fails. Input must be a validated editable mesh.
// Results, report and document are unchanged on failure or cancellation.
bool packModelUvAround(const ModelMesh &mesh, int surface, const QSet<int> &faces, const ModelUvObstacleOptions &options,
					   ModelSurface *result, QString *error = nullptr, const ModelWorkControl &control = {},
					   ModelUvObstacleReport *report = nullptr);
} // namespace vibestudio
