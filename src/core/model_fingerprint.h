#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

namespace vibestudio
{
// Opaque content identity for a validated editable model. This is an internal
// revision key, not a file checksum or a substitute for source serialization.
QByteArray modelStateFingerprint(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
// The fingerprint and, from the same pass, the exact size of the mesh as a
// packed editable source (core/model_source_encoding.h): the metadata the
// fingerprint serialises anyway, plus the packed geometry arrays, whose size
// follows from their counts. Both are empty or -1 on cancellation or error.
struct ModelStateSummary
{
	QByteArray fingerprint;
	qint64 packedSourceBytes = -1;
};
ModelStateSummary summarizeModelState(const ModelMesh &mesh, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
