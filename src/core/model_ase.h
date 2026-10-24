#pragma once

// ASCII Scene Export output: the static-model interchange Doom 3 and Quake 4
// load directly and q3map2 bakes into Quake III maps as misc_model.
//
// Format knowledge: the released Doom 3 GPL source (neo/renderer/Model_ase.cpp)
// and the q3map2 picomodel ASE loader (NetRadiant Custom,
// libs/picomodel/pm_ase.c); see docs/CREDITS.md. Decoding lives in
// model_format_ase.cpp behind decodeModelMesh.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QString>

namespace vibestudio {

struct ModelExportReport;

// One frame as an ASE scene: one GEOMOBJECT and one material per surface,
// with mapping coordinates and per-face normals. The material's BITMAP is
// the surface's first skin path (else its name) written under a /base/
// folder, because Doom 3 names a model's material from the path after base/;
// q3map2 reads the same name. The report lists surfaces left out because the
// frame holds no drawable geometry for them, and surfaces named as their own
// material.
[[nodiscard]] QByteArray exportModelAse(const ModelMesh& mesh, int frameIndex, QString* error = nullptr, const ModelWorkControl& control = {},
	ModelExportReport* report = nullptr);

} // namespace vibestudio
