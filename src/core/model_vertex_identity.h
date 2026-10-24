#pragma once
#include "core/model_mesh.h"
#include "core/model_work.h"
#include <QHash>

namespace vibestudio
{
bool validateModelVertexIdentities(const ModelSurface &surface, QString *error = nullptr, const ModelWorkControl &control = {});
// Freeze inferred all-pose connectivity without changing any vertex data.
bool materialiseModelVertexIdentities(ModelSurface *surface, QString *error = nullptr, const ModelWorkControl &control = {});
// Densely relabel existing identities, retaining their equivalence classes.
// Used before allocation and after vertex compaction/extraction.
void compactModelVertexIdentities(ModelSurface *surface);
// Allocate a separate identity for a copied layer or region, retaining the
// equivalence of its UV/normal copies. Use a fresh map for each separate layer.
void detachModelVertexIdentity(ModelSurface *surface, int vertex, QHash<int, int> *copies);
} // namespace vibestudio
