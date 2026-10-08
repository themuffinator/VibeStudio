#pragma once

// Inter-Quake Model output (version 2), which ioquake3 and its descendants
// load with skeletal animation.
//
// Format knowledge: the IQM specification and reference exporter by Lee
// Salzman (https://github.com/lsalzman/iqm, MIT licence); see
// docs/CREDITS.md. Decoding lives in model_format_iqm.cpp behind
// decodeModelMesh.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QString>

namespace vibestudio {

// The model as an IQM file. With a skeleton it writes joints, blend indexes
// and weights (at most four influences per vertex, strongest first,
// renormalized), poses and one animation per clip. Without one it writes a
// static mesh from the first frame. Positions, normals and texture
// coordinates come from the bind pose (the first baked frame).
[[nodiscard]] QByteArray exportModelIqm(const ModelMesh& mesh, QString* error = nullptr, const ModelWorkControl& control = {});

} // namespace vibestudio
