#pragma once

// How editable model sources (.mesh.json) store numbers and bulk arrays.
//
// Every float is written as the shortest decimal that reads back as the same
// float (about 9 characters instead of the 17 of a double), so readable
// sources stay bit-exact and about half the size. A source whose readable
// form would pass the 64 MiB source limit is written as version 12, which
// stores the bulk arrays packed: little-endian 32-bit floats and integers,
// base64-encoded in one JSON string per array. Names, clips, tags, curves,
// collision and settings stay readable JSON in both forms. The format is
// described in docs/MODEL_MESH.md.

#include "core/model_mesh.h"

#include <QJsonValue>
#include <QLatin1StringView>
#include <QVector>

namespace vibestudio
{

enum class ModelSourceEncoding
{
	// Readable while the readable source fits modelDocumentMaxSourceBytes,
	// packed otherwise. Saves, recovery copies and exports to editable
	// sources all use this.
	Automatic,
	Readable,
	Packed,
};

// The source version that holds packed arrays, and the value of its
// "packedArrays" field, which declares their encoding.
inline constexpr int modelSourcePackedVersion = 12;
inline constexpr QLatin1StringView modelSourcePackedArrays{"base64-le32"};

// The double nearest the shortest decimal that reads back as `value`, so a
// JSON writer prints that decimal. Falls back to double(value), which always
// reads back exactly, in the rare case where the shorter decimal would not
// survive the double-to-float step.
[[nodiscard]] double modelSourceNumber(float value);
// The characters the shortest round-trip decimal of `value` takes.
[[nodiscard]] int modelSourceNumberLength(float value);
// The characters base64 takes for `bytes` bytes, padding included.
[[nodiscard]] constexpr qint64 modelSourcePackedLength(qint64 bytes) { return (bytes + 2) / 3 * 4; }

// Packed arrays. Each item is a fixed number of 32-bit words: three floats
// per vector and triangle, two per UV and seam, twelve per joint matrix and
// eight per joint influence (the joint index, then seven floats).
[[nodiscard]] QJsonValue packModelSourceVectors(const QVector<ModelVec3> &values);
[[nodiscard]] QJsonValue packModelSourceTexCoords(const QVector<ModelTexCoord> &values);
[[nodiscard]] QJsonValue packModelSourceTriangles(const QVector<ModelTriangle> &values);
[[nodiscard]] QJsonValue packModelSourceIntegers(const QVector<int> &values);
[[nodiscard]] QJsonValue packModelSourceEdges(const QVector<QPair<int, int>> &values);
[[nodiscard]] QJsonValue packModelSourceMatrices(const QVector<QVector<ModelJointMatrix>> &frames);
[[nodiscard]] QJsonValue packModelSourceInfluences(const QVector<ModelJointInfluence> &values);
// Readers accept only a string whose base64 decodes to whole items. Floats
// must be finite. `limit` bounds the item count.
bool unpackModelSourceVectors(const QJsonValue &value, qsizetype limit, QVector<ModelVec3> *values);
bool unpackModelSourceTexCoords(const QJsonValue &value, qsizetype limit, QVector<ModelTexCoord> *values);
bool unpackModelSourceTriangles(const QJsonValue &value, qsizetype limit, QVector<ModelTriangle> *values);
bool unpackModelSourceIntegers(const QJsonValue &value, qsizetype limit, QVector<int> *values);
bool unpackModelSourceEdges(const QJsonValue &value, qsizetype limit, QVector<QPair<int, int>> *values);
// `joints` matrices per frame.
bool unpackModelSourceMatrices(const QJsonValue &value, int joints, qint64 limit, QVector<QVector<ModelJointMatrix>> *frames);
bool unpackModelSourceInfluences(const QJsonValue &value, qsizetype limit, QVector<ModelJointInfluence> *values);

// The characters the readable bulk arrays take: frame positions and normals,
// UVs, triangles, seams, topology identities, skeletal clip matrices and
// bounds, and joint influences. Stops counting once past `stopAbove`. The
// automatic encoding writes packed arrays when this alone passes the limit.
[[nodiscard]] qint64 modelSourceReadableBulkLength(const ModelMesh &mesh, qint64 stopAbove);
// The characters a surface's triangles, UVs, seams and frames add to a packed
// source over the same surface with those four arrays empty.
[[nodiscard]] qint64 modelSourcePackedSurfaceLength(const ModelSurface &surface);
// How many more characters every packed array takes as readable JSON (less
// when negative): surface geometry, topology identities, influences, clip
// matrices and bounds. Exact, as it measures the numbers with the readable
// writer itself, but without building the readable source. False on
// cancellation.
bool modelSourceReadableBulkDelta(const ModelMesh &mesh, qint64 *delta, QString *error = nullptr, const ModelWorkControl &control = {});

} // namespace vibestudio
