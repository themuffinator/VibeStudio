#pragma once

// A level map as a triangle mesh, for the Levels 3D preview.
//
// Brush faces come from the shared solver in core/map_geometry, Quake III
// patches from its tessellator, and Doom walls and sector interiors from the
// shared Doom preview geometry, so the preview agrees with the 2D views and
// SVG renderer. Surfaces are grouped by texture name, which the model
// viewport names under the pointer, and hold one frame of positions with a
// normal for each: a brush face's outward plane normal, so a viewer that culls
// back faces keeps the outside of every brush whichever way its corners wind.

#include "core/level_map.h"
#include "core/level_material_paint.h"
#include "core/model_mesh.h"
#include <QSize>
#include <functional>

namespace vibestudio {

class MapBrushGeometryCache;

struct LevelMapPreviewMeshOptions {
	// Past this many triangles the rest are left out, so a very large map
	// stays responsive to orbit and zoom; the result says when it happened.
	int triangleLimit = 150000;
	// Caller-supplied decoded assets, keyed by case-folded package path. No I/O
	// happens while building geometry. Frame zero is used for static placement.
	QHash<QString, ModelMesh> modelMeshes; // levelModelAppearance().cacheKey -> prepared static pose
	// Original image dimensions, not the downsampled preview size. Classic and
	// Valve texels become repeats; brush-primitive and patch UVs already repeat.
	QHash<QString, QSize> textureSizes;
	// Optional caller-owned cache, exclusively used for this build. Solving is
	// still on demand, respecting cancellation and the triangle budget. UVs and
	// material targets are always rebuilt from the current document and assets.
	MapBrushGeometryCache* brushGeometryCache = nullptr;
	std::function<bool()> isCancelled;
};

struct LevelMapPreviewMesh {
	ModelMesh mesh;
	// The brush, patch, linedef, or sector each triangle came from, indexed as a model
	// viewer flattens the mesh: surface after surface, triangles in order.
	QVector<LevelMapSelectionRef> owners;
	// Beside each owner, the brush face the triangle lies on, counted from 0
	// as the brush lists its faces; -1 for patches and Doom surfaces.
	QVector<int> ownerFaces;
	// Exact paint/sample targets in the same flattened order. Placed model
	// triangles have None: editing their source material belongs to Models.
	QVector<LevelMaterialTarget> materialTargets;
	int brushFaces = 0;
	int patches = 0;
	int walls = 0;
	int floors = 0;
	int ceilings = 0;
	QStringList warnings;
	int modelInstances = 0;
	int triangles = 0;
	bool truncated = false;
	bool cancelled = false;
};

QString levelModelSurfaceMaterialKey(const QString& modelPath, int surfaceIndex);
LevelMapPreviewMesh buildLevelMapPreviewMesh(const LevelMapDocument& document, const LevelMapPreviewMeshOptions& options = {});

} // namespace vibestudio
