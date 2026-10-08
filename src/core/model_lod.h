#pragma once

// Level-of-detail models for Quake III.
//
// Quake III loads `name_1.md3` and `name_2.md3` beside `name.md3` as lower
// detail versions, chosen by distance and r_lodbias (see `R_LoadMD3` and
// `R_ComputeLOD` in the released Quake III Arena renderer, code/renderer/
// tr_model.c and tr_mesh.c). Each level here is a quadric decimation of the
// previous one, surface by surface, through the same mesh tool as the editor,
// so UV seams, borders and every animation pose are respected.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QStringList>
#include <QVector>

namespace vibestudio
{
struct ModelLodLevel
{
	ModelMesh mesh;
	int triangles = 0;
	// Surfaces that could not be reduced further keep their previous detail.
	QStringList notes;
};
// Builds `levels` (1-3) reductions, each keeping `ratio` (0.05-0.95) of the
// previous level's triangles.
bool buildModelLods(const ModelMesh &source, int levels, double ratio, QVector<ModelLodLevel> *result, QString *error = nullptr,
					const ModelWorkControl &control = {});
// `models/prop.md3` with level 2 becomes `models/prop_2.md3`.
QString modelLodPath(const QString &basePath, int level);
} // namespace vibestudio
