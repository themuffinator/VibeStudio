#pragma once

// Internal plumbing shared by the model format decoders that live in their
// own files (model_format_*.cpp): bounds-checked little-endian readers, the
// companion-file budget, and one entry point per format. decodeModelMesh in
// model_mesh.cpp detects the format and calls these.
//
// Contract for every entry point:
// - Fill `mesh` (surfaces, frames, animations, tags, skins, skeleton,
//   detail lines, warnings) and set `mesh->geometryAvailable` when surfaces
//   decoded, or set `mesh->error` and return. decodeModelMesh discards
//   everything on error, so partial results never escape.
// - Triangles come out counter-clockwise: cross(b - a, c - a) points out of
//   the model. decodeModelMesh does not flip these formats (it flips only the
//   legacy MDL/MD2/MD3 decoders).
// - Positions are in the game's own units with Z up. Formats with another
//   up axis are converted the way the game's own loader converts them.
// - Every offset and count is range-checked before use; caps below bound
//   memory. Call `work.step()` (ModelWorkProgress) in loops so decodes stay
//   cancellable.
// - Skeletal formats fill mesh->skeleton and each surface's skinning, then call
//   bakeModelSkeleton (core/model_skeleton.h) so ordinary frames exist.
// - User-visible text goes through QCoreApplication::translate with the
//   context "VibeStudioModelMesh".

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace vibestudio::model_formats {

// Shared caps, matching the legacy decoders in model_mesh.cpp.
constexpr int kMaxFrames = 8192;
constexpr int kMaxSurfaces = 2048;
constexpr int kMaxSurfaceVertices = 1048576;
constexpr int kMaxSurfaceTriangles = 1048576;
constexpr int kMaxSkins = 1024;
constexpr int kMaxTags = 8192;
constexpr int kMaxJoints = 4096;
constexpr int kMaxInfluencesPerVertex = 64;
constexpr qint64 kMaxVertexSlots = 4LL * 1024LL * 1024LL;
constexpr qint64 kMaxTextBytes = 64LL * 1024LL * 1024LL;

inline bool rangeOk(const QByteArray& bytes, qint64 offset, qint64 length)
{
	if (offset < 0 || length < 0 || length > qint64(bytes.size())) {
		return false;
	}
	return offset <= qint64(bytes.size()) - length;
}

inline quint8 readU8(const QByteArray& bytes, qint64 offset)
{
	return rangeOk(bytes, offset, 1) ? quint8(bytes.at(qsizetype(offset))) : quint8(0);
}

inline qint8 readI8(const QByteArray& bytes, qint64 offset)
{
	return qint8(readU8(bytes, offset));
}

inline quint16 readU16(const QByteArray& bytes, qint64 offset)
{
	return rangeOk(bytes, offset, 2) ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(bytes.constData() + offset)) : quint16(0);
}

inline qint16 readI16(const QByteArray& bytes, qint64 offset)
{
	return qint16(readU16(bytes, offset));
}

inline quint32 readU32(const QByteArray& bytes, qint64 offset)
{
	return rangeOk(bytes, offset, 4) ? qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset)) : quint32(0);
}

inline qint32 readI32(const QByteArray& bytes, qint64 offset)
{
	return qint32(readU32(bytes, offset));
}

// Big-endian readers, for the IFF chunks of LightWave objects.
inline quint16 readU16BE(const QByteArray& bytes, qint64 offset)
{
	return rangeOk(bytes, offset, 2) ? qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(bytes.constData() + offset)) : quint16(0);
}

inline quint32 readU32BE(const QByteArray& bytes, qint64 offset)
{
	return rangeOk(bytes, offset, 4) ? qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset)) : quint32(0);
}

inline float readF32(const QByteArray& bytes, qint64 offset)
{
	const quint32 raw = readU32(bytes, offset);
	float value = 0.0f;
	std::memcpy(&value, &raw, sizeof(value));
	return value;
}

inline float readF32BE(const QByteArray& bytes, qint64 offset)
{
	const quint32 raw = readU32BE(bytes, offset);
	float value = 0.0f;
	std::memcpy(&value, &raw, sizeof(value));
	return value;
}

// A NUL-terminated Latin-1 name in a fixed-size field, trimmed.
inline QString readFixedName(const QByteArray& bytes, qint64 offset, qint64 length)
{
	QByteArray name;
	for (qint64 i = 0; i < length && rangeOk(bytes, offset + i, 1); ++i) {
		const char ch = bytes.at(qsizetype(offset + i));
		if (ch == '\0') {
			break;
		}
		name.append(ch);
	}
	return QString::fromLatin1(name).trimmed();
}

inline bool readVec3(const QByteArray& bytes, qint64 offset, ModelVec3* out)
{
	if (!rangeOk(bytes, offset, 12)) {
		return false;
	}
	out->x = readF32(bytes, offset);
	out->y = readF32(bytes, offset + 4);
	out->z = readF32(bytes, offset + 8);
	return true;
}

inline bool isFinite(const ModelVec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// The folder part of a '/' path, without the trailing slash; empty at the root.
QString pathDirectory(const QString& path);
// The file name without its folder or last suffix.
QString pathStem(const QString& path);
// `directory` + '/' + `name`, or `name` when the directory is empty.
QString joinPath(const QString& directory, const QString& name);

// Reads companions through a ModelCompanionSource within its budget and
// records each path read in mesh->companionPaths.
class Companions final {
public:
	Companions(const ModelCompanionSource* source, ModelMesh* mesh);
	[[nodiscard]] bool canRead() const;
	[[nodiscard]] bool canList() const;
	// False with `error` set when the file is missing, unreadable or over the
	// budget; the budget error says so, so callers can stop looking.
	bool read(const QString& path, QByteArray* bytes, QString* error = nullptr);
	[[nodiscard]] QStringList list(const QString& directory, const QStringList& suffixes) const;
	[[nodiscard]] bool exhausted() const;

private:
	const ModelCompanionSource* m_source = nullptr;
	ModelMesh* m_mesh = nullptr;
	int m_files = 0;
	qint64 m_bytes = 0;
	bool m_exhausted = false;
};

// --- Entry points, one per format file -------------------------------------

// model_format_mdc.cpp: RTCW / ET compressed MD3 (IDPC 2).
void decodeMdc(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_mdr.cpp: ioquake3 / Elite Force skeletal MDR (RDM5 2).
void decodeMdr(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_iqm.cpp: Inter-Quake Model (version 2).
void decodeIqm(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_md5.cpp: Doom 3 / Quake 4 / Prey / ETQW MD5 meshes and animations.
// A mesh reads every .md5anim beside it (and those a Doom 3 .def names, when
// the companions can list "def") whose joints match.
void decodeMd5Mesh(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions);
void decodeMd5Anim(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_mds.cpp: RTCW skeletal MDS (MDSW 4), ET MDM (MDMW 3) with its
// MDX, and ET MDX (MDXW 2) on its own.
void decodeMds(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
void decodeMdm(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions);
void decodeMdx(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_ghoul2.cpp: Ghoul 2 GLM (2LGM 6) with the GLA it names, and GLA alone.
void decodeGlm(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions);
void decodeGla(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_studio.cpp: GoldSrc studio models (IDST 10), reading the
// "<name>T.mdl" texture file and "<name>NN.mdl" sequence groups when present.
void decodeHalfLifeMdl(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions);
// model_format_hexen2.cpp: Hexen II RAPO 50 alias models.
void decodeHexen2Mdl(const QByteArray& bytes, const IdTechPalette& palette, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_lwo.cpp: LightWave LWO2 and LWOB static objects.
void decodeLightWave(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_ase.cpp: ASCII scene export.
void decodeAse(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_fm.cpp: Heretic II flexible models.
void decodeHereticFm(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);
// model_format_kvx.cpp: Build/ZDoom KVX voxels, meshed into faces.
void decodeKvx(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control);

} // namespace vibestudio::model_formats
