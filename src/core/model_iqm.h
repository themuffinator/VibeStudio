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

struct ModelExportReport;
struct ModelTargetLimits;

// The model as an IQM file. With a skeleton it writes joints, blend indexes
// and weights (at most four influences per vertex, strongest first,
// renormalized), poses and one animation per clip. Without one it writes a
// static mesh from the first frame. Positions, normals and texture
// coordinates come from the bind pose (the first baked frame). Surfaces
// without influences get zero weights, which keep them in the bind pose as
// the editor does. The report counts the vertices that lost influences past
// the fourth (an omission), the largest weight and pose channel changes from
// 8-bit weights and 16-bit channels, and any empty clip left out.
[[nodiscard]] QByteArray exportModelIqm(const ModelMesh& mesh, QString* error = nullptr, const ModelWorkControl& control = {},
	ModelExportReport* report = nullptr);
// The same, refusing a model over a delivery target's joint and per-mesh
// vertex and triangle limits (core/model_targets.h).
[[nodiscard]] QByteArray exportModelIqm(const ModelMesh& mesh, const ModelTargetLimits& limits, QString* error = nullptr,
	const ModelWorkControl& control = {}, ModelExportReport* report = nullptr);

} // namespace vibestudio
