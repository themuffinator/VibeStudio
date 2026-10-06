#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
struct ModelUvAtlasOptions
{
	int resolution = 512;
	int padding = 4;
	int vertexLimit = 65536;
	// Callers may lower this ceiling for a constrained worker or a test.
	qsizetype memoryLimit = 256 * 1024 * 1024;
	// Width is resolution; zero height preserves the existing square API.
	int height = 0;
};
struct ModelUvAtlasReport
{
	int charts = 0;
	qsizetype peakBytes = 0;
};

// Unwrap selected faces from one reference pose, or repack existing UV islands.
// All coordinates share one atlas. Unwrap preserves chart shape in pixel space;
// Pack preserves orientation and relative UV scale for the given texture size.
// Unselected corners, materials and all animation geometry are retained.
// Inputs must come from a validated editable model. Output is atomic on failure.
bool atlasModelUv(const ModelSurface &source, const QSet<int> &faces, int frame, bool unwrap, const ModelUvAtlasOptions &options,
				  ModelSurface *result, QString *error = nullptr, const ModelWorkControl &control = {},
				  ModelUvAtlasReport *report = nullptr);
} // namespace vibestudio
