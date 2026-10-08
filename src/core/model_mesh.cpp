#include "core/model_mesh.h"
#include "core/model_archive.h"
#include "core/model_document.h"
#include "core/model_formats_p.h"
#include "core/model_skeleton.h"

#include "core/model_mdl.h"
#include "core/model_obj.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QRgb>

#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace vibestudio {

namespace {

// ---------------------------------------------------------------------------
// Format references
//
// Quake MDL (IDPO 6): the Quake Specifications chapter 5 "MDL files"
// (https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_5.htm) and the
// released Quake source `modelgen.h` (`mdl_t`, `stvert_t`, `dtriangle_t`,
// `daliasframe_t`, `daliasgroup_t`, `trivertx_t`,
// https://github.com/id-Software/Quake/blob/master/WinQuake/modelgen.h).
//
// Quake II MD2 (IDP2 8): the released Quake II source `qfiles.h` (`dmdl_t`,
// `dstvert_t`, `dtriangle_t`, `daliasframe_t`, `dtrivertx_t`,
// https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h).
//
// Quake III MD3 (IDP3 15): the released Quake III Arena source `md3.h`
// (`md3Header_t`, `md3Frame_t`, `md3Tag_t`, `md3Surface_t`, `md3Shader_t`,
// `md3Triangle_t`, `md3St_t`, `md3XyzNormal_t`, `MD3_XYZ_SCALE`,
// https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_types.h
// and .../code/qcommon/qfiles.h).
//
// MDC header: the Return to Castle Wolfenstein source `qfiles.h`
// (`mdcHeader_t`). MDR header: the Elite Force / ioquake3 `qfiles.h`
// (`mdrHeader_t`). IQM header: the Inter-Quake Model specification
// (http://sauerbraten.org/iqm/, `iqmheader`). Only their headers are read here;
// their geometry layouts are deliberately not guessed at.
//
// Everything below is implemented from those specifications. No code is copied
// from those projects, and no commercial model, skin, palette or animation data
// is embedded.
// ---------------------------------------------------------------------------

// Sanity caps. A malformed or hostile file must fail cleanly instead of
// allocating gigabytes or looping forever.
constexpr int kMaxFrames = 8192;
constexpr int kMaxSurfaces = 2048;
constexpr int kMaxSurfaceVertices = 1048576;
constexpr int kMaxSurfaceTriangles = 1048576;
constexpr int kMaxSkins = 1024;
constexpr int kMaxTags = 8192;
constexpr int kMaxSkinDimension = 8192;
constexpr qint64 kMaxSkinPixels = 64LL * 1024LL * 1024LL;
// positions + normals are 24 bytes per slot, so this caps decoded geometry at
// roughly 100 MB per model.
constexpr qint64 kMaxVertexSlots = 4LL * 1024LL * 1024LL;
constexpr int kMaxSkinGroupFrames = 1024;

constexpr double kTwoPi = 6.28318530717958647692;

// The 162 vertex normals shared by Quake MDL and Quake II MD2. This is id
// Software's published `anorms.h` table: a fixed mathematical constant of the
// two formats (the byte stored per vertex is an index into it), not game
// content. Source: the released Quake II tools source
// https://github.com/id-Software/Quake-2/blob/master/ref_gl/anorms.h
// Copyright (C) 1997-2001 Id Software, Inc. GPL-2.0-or-later; reviewed
// 2026-10-04. The original table is also used for MD2 export normal matching.
constexpr float kAliasNormals[162][3] = {
	{-0.525731f, 0.000000f, 0.850651f},
	{-0.442863f, 0.238856f, 0.864188f},
	{-0.295242f, 0.000000f, 0.955423f},
	{-0.309017f, 0.500000f, 0.809017f},
	{-0.162460f, 0.262866f, 0.951056f},
	{0.000000f, 0.000000f, 1.000000f},
	{0.000000f, 0.850651f, 0.525731f},
	{-0.147621f, 0.716567f, 0.681718f},
	{0.147621f, 0.716567f, 0.681718f},
	{0.000000f, 0.525731f, 0.850651f},
	{0.309017f, 0.500000f, 0.809017f},
	{0.525731f, 0.000000f, 0.850651f},
	{0.295242f, 0.000000f, 0.955423f},
	{0.442863f, 0.238856f, 0.864188f},
	{0.162460f, 0.262866f, 0.951056f},
	{-0.681718f, 0.147621f, 0.716567f},
	{-0.809017f, 0.309017f, 0.500000f},
	{-0.587785f, 0.425325f, 0.688191f},
	{-0.850651f, 0.525731f, 0.000000f},
	{-0.864188f, 0.442863f, 0.238856f},
	{-0.716567f, 0.681718f, 0.147621f},
	{-0.688191f, 0.587785f, 0.425325f},
	{-0.500000f, 0.809017f, 0.309017f},
	{-0.238856f, 0.864188f, 0.442863f},
	{-0.425325f, 0.688191f, 0.587785f},
	{-0.716567f, 0.681718f, -0.147621f},
	{-0.500000f, 0.809017f, -0.309017f},
	{-0.525731f, 0.850651f, 0.000000f},
	{0.000000f, 0.850651f, -0.525731f},
	{-0.238856f, 0.864188f, -0.442863f},
	{0.000000f, 0.955423f, -0.295242f},
	{-0.262866f, 0.951056f, -0.162460f},
	{0.000000f, 1.000000f, 0.000000f},
	{0.000000f, 0.955423f, 0.295242f},
	{-0.262866f, 0.951056f, 0.162460f},
	{0.238856f, 0.864188f, 0.442863f},
	{0.262866f, 0.951056f, 0.162460f},
	{0.500000f, 0.809017f, 0.309017f},
	{0.238856f, 0.864188f, -0.442863f},
	{0.262866f, 0.951056f, -0.162460f},
	{0.500000f, 0.809017f, -0.309017f},
	{0.850651f, 0.525731f, 0.000000f},
	{0.716567f, 0.681718f, 0.147621f},
	{0.716567f, 0.681718f, -0.147621f},
	{0.525731f, 0.850651f, 0.000000f},
	{0.425325f, 0.688191f, 0.587785f},
	{0.864188f, 0.442863f, 0.238856f},
	{0.688191f, 0.587785f, 0.425325f},
	{0.809017f, 0.309017f, 0.500000f},
	{0.681718f, 0.147621f, 0.716567f},
	{0.587785f, 0.425325f, 0.688191f},
	{0.955423f, 0.295242f, 0.000000f},
	{1.000000f, 0.000000f, 0.000000f},
	{0.951056f, 0.162460f, 0.262866f},
	{0.850651f, -0.525731f, 0.000000f},
	{0.955423f, -0.295242f, 0.000000f},
	{0.864188f, -0.442863f, 0.238856f},
	{0.951056f, -0.162460f, 0.262866f},
	{0.809017f, -0.309017f, 0.500000f},
	{0.681718f, -0.147621f, 0.716567f},
	{0.850651f, 0.000000f, 0.525731f},
	{0.864188f, 0.442863f, -0.238856f},
	{0.809017f, 0.309017f, -0.500000f},
	{0.951056f, 0.162460f, -0.262866f},
	{0.525731f, 0.000000f, -0.850651f},
	{0.681718f, 0.147621f, -0.716567f},
	{0.681718f, -0.147621f, -0.716567f},
	{0.850651f, 0.000000f, -0.525731f},
	{0.809017f, -0.309017f, -0.500000f},
	{0.864188f, -0.442863f, -0.238856f},
	{0.951056f, -0.162460f, -0.262866f},
	{0.147621f, 0.716567f, -0.681718f},
	{0.309017f, 0.500000f, -0.809017f},
	{0.425325f, 0.688191f, -0.587785f},
	{0.442863f, 0.238856f, -0.864188f},
	{0.587785f, 0.425325f, -0.688191f},
	{0.688191f, 0.587785f, -0.425325f},
	{-0.147621f, 0.716567f, -0.681718f},
	{-0.309017f, 0.500000f, -0.809017f},
	{0.000000f, 0.525731f, -0.850651f},
	{-0.525731f, 0.000000f, -0.850651f},
	{-0.442863f, 0.238856f, -0.864188f},
	{-0.295242f, 0.000000f, -0.955423f},
	{-0.162460f, 0.262866f, -0.951056f},
	{0.000000f, 0.000000f, -1.000000f},
	{0.295242f, 0.000000f, -0.955423f},
	{0.162460f, 0.262866f, -0.951056f},
	{-0.442863f, -0.238856f, -0.864188f},
	{-0.309017f, -0.500000f, -0.809017f},
	{-0.162460f, -0.262866f, -0.951056f},
	{0.000000f, -0.850651f, -0.525731f},
	{-0.147621f, -0.716567f, -0.681718f},
	{0.147621f, -0.716567f, -0.681718f},
	{0.000000f, -0.525731f, -0.850651f},
	{0.309017f, -0.500000f, -0.809017f},
	{0.442863f, -0.238856f, -0.864188f},
	{0.162460f, -0.262866f, -0.951056f},
	{0.238856f, -0.864188f, -0.442863f},
	{0.500000f, -0.809017f, -0.309017f},
	{0.425325f, -0.688191f, -0.587785f},
	{0.716567f, -0.681718f, -0.147621f},
	{0.688191f, -0.587785f, -0.425325f},
	{0.587785f, -0.425325f, -0.688191f},
	{0.000000f, -0.955423f, -0.295242f},
	{0.000000f, -1.000000f, 0.000000f},
	{0.262866f, -0.951056f, -0.162460f},
	{0.000000f, -0.850651f, 0.525731f},
	{0.000000f, -0.955423f, 0.295242f},
	{0.238856f, -0.864188f, 0.442863f},
	{0.262866f, -0.951056f, 0.162460f},
	{0.500000f, -0.809017f, 0.309017f},
	{0.716567f, -0.681718f, 0.147621f},
	{0.525731f, -0.850651f, 0.000000f},
	{-0.238856f, -0.864188f, -0.442863f},
	{-0.500000f, -0.809017f, -0.309017f},
	{-0.262866f, -0.951056f, -0.162460f},
	{-0.850651f, -0.525731f, 0.000000f},
	{-0.716567f, -0.681718f, -0.147621f},
	{-0.716567f, -0.681718f, 0.147621f},
	{-0.525731f, -0.850651f, 0.000000f},
	{-0.500000f, -0.809017f, 0.309017f},
	{-0.238856f, -0.864188f, 0.442863f},
	{-0.262866f, -0.951056f, 0.162460f},
	{-0.864188f, -0.442863f, 0.238856f},
	{-0.809017f, -0.309017f, 0.500000f},
	{-0.688191f, -0.587785f, 0.425325f},
	{-0.681718f, -0.147621f, 0.716567f},
	{-0.442863f, -0.238856f, 0.864188f},
	{-0.587785f, -0.425325f, 0.688191f},
	{-0.309017f, -0.500000f, 0.809017f},
	{-0.147621f, -0.716567f, 0.681718f},
	{-0.425325f, -0.688191f, 0.587785f},
	{-0.162460f, -0.262866f, 0.951056f},
	{0.442863f, -0.238856f, 0.864188f},
	{0.162460f, -0.262866f, 0.951056f},
	{0.309017f, -0.500000f, 0.809017f},
	{0.147621f, -0.716567f, 0.681718f},
	{0.000000f, -0.525731f, 0.850651f},
	{0.425325f, -0.688191f, 0.587785f},
	{0.587785f, -0.425325f, 0.688191f},
	{0.688191f, -0.587785f, 0.425325f},
	{-0.955423f, 0.295242f, 0.000000f},
	{-0.951056f, 0.162460f, 0.262866f},
	{-1.000000f, 0.000000f, 0.000000f},
	{-0.850651f, 0.000000f, 0.525731f},
	{-0.955423f, -0.295242f, 0.000000f},
	{-0.951056f, -0.162460f, 0.262866f},
	{-0.864188f, 0.442863f, -0.238856f},
	{-0.951056f, 0.162460f, -0.262866f},
	{-0.809017f, 0.309017f, -0.500000f},
	{-0.864188f, -0.442863f, -0.238856f},
	{-0.951056f, -0.162460f, -0.262866f},
	{-0.809017f, -0.309017f, -0.500000f},
	{-0.681718f, 0.147621f, -0.716567f},
	{-0.681718f, -0.147621f, -0.716567f},
	{-0.850651f, 0.000000f, -0.525731f},
	{-0.688191f, 0.587785f, -0.425325f},
	{-0.587785f, 0.425325f, -0.688191f},
	{-0.425325f, 0.688191f, -0.587785f},
	{-0.425325f, -0.688191f, -0.587785f},
	{-0.587785f, -0.425325f, -0.688191f},
	{-0.688191f, -0.587785f, -0.425325f},
};

// ---------------------------------------------------------------------------
// Bounds-checked readers
// ---------------------------------------------------------------------------

bool rangeOk(const QByteArray& bytes, qint64 offset, qint64 length)
{
	if (offset < 0 || length < 0) {
		return false;
	}
	if (length > static_cast<qint64>(bytes.size())) {
		return false;
	}
	return offset <= static_cast<qint64>(bytes.size()) - length;
}

quint8 readU8(const QByteArray& bytes, qint64 offset)
{
	if (!rangeOk(bytes, offset, 1)) {
		return 0;
	}
	return static_cast<quint8>(bytes.at(static_cast<qsizetype>(offset)));
}

quint32 readU32(const QByteArray& bytes, qint64 offset)
{
	if (!rangeOk(bytes, offset, 4)) {
		return 0;
	}
	return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

qint32 readI32(const QByteArray& bytes, qint64 offset)
{
	return static_cast<qint32>(readU32(bytes, offset));
}

quint16 readU16(const QByteArray& bytes, qint64 offset)
{
	if (!rangeOk(bytes, offset, 2)) {
		return 0;
	}
	return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

qint16 readI16(const QByteArray& bytes, qint64 offset)
{
	return static_cast<qint16>(readU16(bytes, offset));
}

float readF32(const QByteArray& bytes, qint64 offset)
{
	const quint32 raw = readU32(bytes, offset);
	float value = 0.0f;
	std::memcpy(&value, &raw, sizeof(value));
	return value;
}

QString readFixedName(const QByteArray& bytes, qint64 offset, qint64 length)
{
	QByteArray name;
	for (qint64 i = 0; i < length; ++i) {
		if (!rangeOk(bytes, offset + i, 1)) {
			break;
		}
		const char ch = bytes.at(static_cast<qsizetype>(offset + i));
		if (ch == '\0') {
			break;
		}
		name.append(ch);
	}
	return QString::fromLatin1(name).trimmed();
}

bool readVec3(const QByteArray& bytes, qint64 offset, ModelVec3* out)
{
	if (!rangeOk(bytes, offset, 12)) {
		return false;
	}
	out->x = readF32(bytes, offset);
	out->y = readF32(bytes, offset + 4);
	out->z = readF32(bytes, offset + 8);
	return true;
}

bool isFinite(const ModelVec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

ModelVec3 makeVec3(float x, float y, float z)
{
	ModelVec3 value;
	value.x = x;
	value.y = y;
	value.z = z;
	return value;
}

ModelVec3 aliasNormal(int index)
{
	if (index < 0 || index >= 162) {
		return makeVec3(0.0f, 0.0f, 1.0f);
	}
	return makeVec3(kAliasNormals[index][0], kAliasNormals[index][1], kAliasNormals[index][2]);
}

// MD3 stores a normal as a packed latitude/longitude byte pair. See `md3.h`
// and the Quake III renderer's normal reconstruction.
ModelVec3 md3Normal(quint16 packed)
{
	const double lat = static_cast<double>((packed >> 8) & 0xFF) * kTwoPi / 256.0;
	const double lng = static_cast<double>(packed & 0xFF) * kTwoPi / 256.0;
	return makeVec3(static_cast<float>(std::cos(lat) * std::sin(lng)),
		static_cast<float>(std::sin(lat) * std::sin(lng)),
		static_cast<float>(std::cos(lng)));
}

QString sanitizedName(const QString& name)
{
	QString cleaned;
	cleaned.reserve(name.size());
	for (const QChar ch : name) {
		if (ch.isSpace()) {
			cleaned.append(QLatin1Char('_'));
		} else if (ch.isPrint()) {
			cleaned.append(ch);
		}
	}
	return cleaned;
}

QString pathStem(const QString& virtualPath)
{
	QString name = virtualPath;
	const int slash = std::max(name.lastIndexOf(QLatin1Char('/')), name.lastIndexOf(QLatin1Char('\\')));
	if (slash >= 0) {
		name = name.mid(slash + 1);
	}
	const int dot = name.lastIndexOf(QLatin1Char('.'));
	if (dot > 0) {
		name = name.left(dot);
	}
	return sanitizedName(name);
}

QString pathSuffix(const QString& virtualPath)
{
	const int slash = std::max(virtualPath.lastIndexOf(QLatin1Char('/')), virtualPath.lastIndexOf(QLatin1Char('\\')));
	const QString name = slash >= 0 ? virtualPath.mid(slash + 1) : virtualPath;
	const int dot = name.lastIndexOf(QLatin1Char('.'));
	if (dot <= 0) {
		return {};
	}
	return name.mid(dot + 1).toLower();
}

QString formatNumber(double value)
{
	// QString::number uses the C locale, so exported and reported numbers stay
	// identical regardless of the user's locale.
	if (!std::isfinite(value)) {
		return QStringLiteral("0");
	}
	return QString::number(value, 'f', 6);
}

QString formatCoordinate(double value)
{
	if (!std::isfinite(value)) {
		return QStringLiteral("0.00");
	}
	return QString::number(value, 'f', 2);
}

QString formatVector(const ModelVec3& value)
{
	return QStringLiteral("%1 %2 %3").arg(formatCoordinate(value.x), formatCoordinate(value.y), formatCoordinate(value.z));
}

// ---------------------------------------------------------------------------
// Shared post-processing
// ---------------------------------------------------------------------------

QString animationStem(const QString& name)
{
	QString stem = name.trimmed();
	while (!stem.isEmpty() && stem.back().isDigit()) {
		stem.chop(1);
	}
	while (!stem.isEmpty() && (stem.back() == QLatin1Char('_') || stem.back() == QLatin1Char('-') || stem.back().isSpace())) {
		stem.chop(1);
	}
	if (stem.isEmpty()) {
		stem = name.trimmed();
	}
	return stem;
}

// MDL and MD2 encode animations as consecutive frames sharing a name stem with
// a trailing number ("stand1".."stand5").
QVector<ModelAnimation> inferAnimations(const QVector<ModelFrameInfo>& frames, ModelWorkProgress& work)
{
	QVector<ModelAnimation> animations;
	for (int index = 0; index < frames.size(); ++index) {
		if (!work.step()) { return {}; }
		QString stem = animationStem(frames.at(index).name);
		if (stem.isEmpty()) {
			stem = QStringLiteral("frame");
		}
		if (!animations.isEmpty() && animations.last().name == stem
			&& animations.last().firstFrame + animations.last().frameCount == index) {
			animations.last().frameCount += 1;
			continue;
		}
		ModelAnimation animation;
		animation.name = stem;
		animation.firstFrame = index;
		animation.frameCount = 1;
		animations.append(animation);
	}
	return animations;
}

void finalizeMesh(ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	mesh->frameCount = mesh->frames.size();
	mesh->surfaceCount = mesh->surfaces.size();
	mesh->tagCount = 0;
	QSet<QString> tagNames;
	for (const ModelTag& tag : mesh->tags) {
		if (!work.step()) { return; }
		if (!tagNames.contains(tag.name)) {
			tagNames.insert(tag.name);
		}
	}
	mesh->tagCount = tagNames.size();

	mesh->vertexCount = 0;
	mesh->triangleCount = 0;
	bool haveBounds = false;
	ModelVec3 mins = makeVec3(0.0f, 0.0f, 0.0f);
	ModelVec3 maxs = makeVec3(0.0f, 0.0f, 0.0f);
	for (const ModelSurface& surface : mesh->surfaces) {
		if (!work.step()) { return; }
		mesh->vertexCount += surface.vertexCount;
		mesh->triangleCount += surface.triangles.size();
		for (const ModelFrameGeometry& geometry : surface.frames) {
			if (!work.step()) { return; }
			for (const ModelVec3& position : geometry.positions) {
				if (!work.step()) { return; }
				if (!isFinite(position)) {
					continue;
				}
				if (!haveBounds) {
					mins = position;
					maxs = position;
					haveBounds = true;
					continue;
				}
				mins.x = std::min(mins.x, position.x);
				mins.y = std::min(mins.y, position.y);
				mins.z = std::min(mins.z, position.z);
				maxs.x = std::max(maxs.x, position.x);
				maxs.y = std::max(maxs.y, position.y);
				maxs.z = std::max(maxs.z, position.z);
			}
		}
	}
	if (!haveBounds) {
		for (const ModelFrameInfo& frame : mesh->frames) {
			if (!work.step()) { return; }
			if (!isFinite(frame.mins) || !isFinite(frame.maxs)) {
				continue;
			}
			if (!haveBounds) {
				mins = frame.mins;
				maxs = frame.maxs;
				haveBounds = true;
				continue;
			}
			mins.x = std::min(mins.x, frame.mins.x);
			mins.y = std::min(mins.y, frame.mins.y);
			mins.z = std::min(mins.z, frame.mins.z);
			maxs.x = std::max(maxs.x, frame.maxs.x);
			maxs.y = std::max(maxs.y, frame.maxs.y);
			maxs.z = std::max(maxs.z, frame.maxs.z);
		}
	}
	mesh->mins = mins;
	mesh->maxs = maxs;
}

// ---------------------------------------------------------------------------
// Quake MDL (IDPO 6)
// ---------------------------------------------------------------------------

constexpr qint64 kMdlHeaderBytes = 84;
constexpr qint64 kMdlStVertBytes = 12;
constexpr qint64 kMdlTriangleBytes = 16;
constexpr qint64 kMdlVertexBytes = 4;
constexpr qint64 kMdlFrameNameBytes = 16;

QImage decodeIndexedSkin(const QByteArray& bytes, qint64 offset, int width, int height, const IdTechPalette& palette, ModelWorkProgress& work)
{
	if (width <= 0 || height <= 0) {
		return {};
	}
	const qint64 pixels = static_cast<qint64>(width) * static_cast<qint64>(height);
	if (!rangeOk(bytes, offset, pixels)) {
		return {};
	}
	QImage image(width, height, QImage::Format_ARGB32);
	if (image.isNull()) {
		return {};
	}
	const uchar* source = reinterpret_cast<const uchar*>(bytes.constData() + offset);
	for (int y = 0; y < height; ++y) {
		QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
		const uchar* sourceRow = source + (static_cast<qint64>(y) * width);
		for (int x = 0; x < width; ++x) {
			if (!work.step()) { return {}; }
			const QRgb color = palette.colorAt(static_cast<int>(sourceRow[x]));
			row[x] = qRgb(qRed(color), qGreen(color), qBlue(color));
		}
	}
	return image;
}

struct MdlPose {
	QString name;
	qint64 vertexOffset = 0;
	ModelVec3 mins;
	ModelVec3 maxs;
};

void decodeQuakeMdl(const QByteArray& bytes, const IdTechPalette& palette, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(bytes, 0, kMdlHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 6) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MDL version %1; only IDPO version 6 is decoded.").arg(mesh->version);
		return;
	}

	ModelVec3 scale;
	ModelVec3 translate;
	ModelVec3 eyePosition;
	readVec3(bytes, 8, &scale);
	readVec3(bytes, 20, &translate);
	const float declaredRadius = readF32(bytes, 32);
	readVec3(bytes, 36, &eyePosition);
	const int skinCount = readI32(bytes, 48);
	const int skinWidth = readI32(bytes, 52);
	const int skinHeight = readI32(bytes, 56);
	const int vertexCount = readI32(bytes, 60);
	const int triangleCount = readI32(bytes, 64);
	const int frameGroupCount = readI32(bytes, 68);
	const int syncType = readI32(bytes, 72);
	const int flags = readI32(bytes, 76);
	const float declaredSize = readF32(bytes, 80);

	if (!isFinite(scale) || !isFinite(translate)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL header has a non-finite scale or translation.");
		return;
	}
	if (skinCount < 0 || skinCount > kMaxSkins
		|| vertexCount <= 0 || vertexCount > kMaxSurfaceVertices
		|| triangleCount <= 0 || triangleCount > kMaxSurfaceTriangles
		|| frameGroupCount <= 0 || frameGroupCount > kMaxFrames) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL header declares counts outside the supported range.");
		return;
	}
	if (skinCount > 0 && (skinWidth <= 0 || skinHeight <= 0 || skinWidth > kMaxSkinDimension || skinHeight > kMaxSkinDimension)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL skin size is outside the supported range.");
		return;
	}
	const qint64 skinPixels = static_cast<qint64>(std::max(0, skinWidth)) * static_cast<qint64>(std::max(0, skinHeight));
	if (skinCount > 0 && skinPixels > kMaxSkinPixels) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL skin size is outside the supported range.");
		return;
	}
	if (static_cast<qint64>(vertexCount) * static_cast<qint64>(frameGroupCount) > kMaxVertexSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL declares more frame vertices than can be decoded.");
		return;
	}

	mesh->skinCount = skinCount;
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Scale: %1").arg(formatVector(scale));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Translation: %1").arg(formatVector(translate));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Eye position: %1").arg(formatVector(eyePosition));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Declared bounding radius: %1").arg(formatCoordinate(declaredRadius));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Declared size: %1").arg(formatCoordinate(declaredSize));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Sync type: %1").arg(syncType);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Flags: 0x%1").arg(QString::number(static_cast<uint>(flags), 16));
	// Preserve native groups, exact indices and header semantics alongside the
	// flattened geometry. Layout: id Software Quake/WinQuake/modelgen.h and
	// model.c, master reviewed 2026-10-04, GPL-2.0-or-later; compatible with GPL-3.0.
	mesh->mdl.enabled = true;
	mesh->mdl.skinSize = {skinWidth, skinHeight};
	mesh->mdl.palette = modelMdlPaletteBytes(palette);
	mesh->mdl.paletteGenerated = palette.generated;
	mesh->mdl.eyePosition = eyePosition;
	mesh->mdl.flags = static_cast<quint32>(flags);
	mesh->mdl.syncType = syncType;
	mesh->mdl.size = declaredSize;

	qint64 offset = kMdlHeaderBytes;
	qint64 totalSkinPixels = 0;

	// Skins. A group skin stores a count, one interval per member, then the
	// member pixel blocks back to back.
	for (int index = 0; index < skinCount; ++index) {
		if (!work.step()) { return; }
		if (!rangeOk(bytes, offset, 4)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL skin data is truncated.");
			return;
		}
		const int group = readI32(bytes, offset);
		offset += 4;
		if (group != 0 && group != 1) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL skin type is neither a single image nor a group.");
			return;
		}
		int groupFrames = 1;
		ModelEmbeddedSkin skin;
		if (group != 0) {
			if (!rangeOk(bytes, offset, 4)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL skin data is truncated.");
				return;
			}
			groupFrames = readI32(bytes, offset);
			offset += 4;
			if (groupFrames <= 0 || groupFrames > kMaxSkinGroupFrames) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "An MDL skin group declares an unsupported frame count.");
				return;
			}
			if (!rangeOk(bytes, offset, static_cast<qint64>(groupFrames) * 4)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL skin data is truncated.");
				return;
			}
			float previous = 0;
			for (int member = 0; member < groupFrames; ++member) {
				if (!work.step()) { return; }
				const float interval = readF32(bytes, offset + member * 4);
				if (!std::isfinite(interval) || interval <= previous) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDL skin group end times must be finite, positive and strictly increasing.");
					return;
				}
				skin.intervals.append(interval);
				previous = interval;
			}
			offset += static_cast<qint64>(groupFrames) * 4;
		}
		const qint64 block = skinPixels * static_cast<qint64>(groupFrames);
		if (!rangeOk(bytes, offset, block)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL skin data is truncated.");
			return;
		}
		if (block > kMaxSkinPixels - totalSkinPixels) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDL skin members exceed the 64-megapixel preview limit.");
			return;
		}
		totalSkinPixels += block;
		skin.index = index;
		skin.name = QStringLiteral("skin%1").arg(index);
		skin.groupFrameCount = groupFrames;
		for (int member = 0; member < groupFrames; ++member) {
			if (!work.step()) { return; }
			skin.indexedFrames.append(bytes.mid(offset + member * skinPixels, skinPixels));
		}
		skin.image = decodeIndexedSkin(bytes, offset, skinWidth, skinHeight, palette, work);
		if (!mesh->error.isEmpty()) { return; }
		if (skin.image.isNull()) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Embedded skin %1 could not be decoded.").arg(index);
		}
		mesh->embeddedSkins.append(skin);
		offset += block;
	}

	// Texture coordinates.
	if (!rangeOk(bytes, offset, static_cast<qint64>(vertexCount) * kMdlStVertBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL texture coordinates are truncated.");
		return;
	}
	QVector<int> seamFlags(vertexCount, 0);
	QVector<int> sCoords(vertexCount, 0);
	QVector<int> tCoords(vertexCount, 0);
	for (int index = 0; index < vertexCount; ++index) {
		if (!work.step()) { return; }
		const qint64 base = offset + (static_cast<qint64>(index) * kMdlStVertBytes);
		// `onseam` carries ALIAS_ONSEAM (0x0020) from modelgen.h; treat any
		// non-zero value as "on the seam".
		seamFlags[index] = readI32(bytes, base) != 0 ? 1 : 0;
		sCoords[index] = readI32(bytes, base + 4);
		tCoords[index] = readI32(bytes, base + 8);
	}
	offset += static_cast<qint64>(vertexCount) * kMdlStVertBytes;

	// Triangles.
	if (!rangeOk(bytes, offset, static_cast<qint64>(triangleCount) * kMdlTriangleBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL triangle list is truncated.");
		return;
	}

	ModelSurface surface;
	surface.index = 0;
	surface.name = mesh->sourcePath.isEmpty() ? QStringLiteral("surface0") : pathStem(mesh->sourcePath);
	if (surface.name.isEmpty()) {
		surface.name = QStringLiteral("surface0");
	}

	// Combined vertex list: the base vertices first, then a duplicate for every
	// vertex that needs the back-face S offset, so texture coordinates stay
	// per-vertex.
	QVector<int> baseForCombined;
	QVector<int> backForCombined;
	baseForCombined.reserve(vertexCount);
	backForCombined.reserve(vertexCount);
	for (int index = 0; index < vertexCount; ++index) {
		if (!work.step()) { return; }
		baseForCombined.append(index);
		backForCombined.append(0);
	}
	QHash<int, int> backCopyForBase;

	int skippedTriangles = 0;
	for (int index = 0; index < triangleCount; ++index) {
		if (!work.step()) { return; }
		const qint64 base = offset + (static_cast<qint64>(index) * kMdlTriangleBytes);
		const bool facesFront = readI32(bytes, base) != 0;
		int corners[3] = {0, 0, 0};
		bool valid = true;
		for (int corner = 0; corner < 3; ++corner) {
			if (!work.step()) { return; }
			const int vertexIndex = readI32(bytes, base + 4 + (corner * 4));
			if (vertexIndex < 0 || vertexIndex >= vertexCount) {
				valid = false;
				break;
			}
			int combined = vertexIndex;
			if (!facesFront && seamFlags.at(vertexIndex) != 0) {
				const auto existing = backCopyForBase.constFind(vertexIndex);
				if (existing != backCopyForBase.constEnd()) {
					combined = existing.value();
				} else {
					combined = baseForCombined.size();
					baseForCombined.append(vertexIndex);
					backForCombined.append(1);
					backCopyForBase.insert(vertexIndex, combined);
				}
			}
			corners[corner] = combined;
		}
		if (!valid) {
			++skippedTriangles;
			continue;
		}
		ModelTriangle triangle;
		triangle.a = corners[0];
		triangle.b = corners[1];
		triangle.c = corners[2];
		surface.triangles.append(triangle);
	}
	if (skippedTriangles > 0) {
		surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced vertices outside the model and were dropped.").arg(skippedTriangles);
	}
	offset += static_cast<qint64>(triangleCount) * kMdlTriangleBytes;

	const int combinedCount = baseForCombined.size();
	if (static_cast<qint64>(combinedCount) * static_cast<qint64>(frameGroupCount) > kMaxVertexSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL declares more frame vertices than can be decoded.");
		return;
	}
	surface.vertexCount = combinedCount;
	surface.texCoords.reserve(combinedCount);
	const float skinWidthF = skinWidth > 0 ? static_cast<float>(skinWidth) : 1.0f;
	const float skinHeightF = skinHeight > 0 ? static_cast<float>(skinHeight) : 1.0f;
	for (int index = 0; index < combinedCount; ++index) {
		if (!work.step()) { return; }
		const int base = baseForCombined.at(index);
		// GLQuake samples texel centres, and adds half the skin width for the
		// back-facing copy of an on-seam vertex.
		float s = static_cast<float>(sCoords.at(base));
		if (backForCombined.at(index) != 0) {
			s += static_cast<float>(skinWidth / 2);
		}
		ModelTexCoord coord;
		coord.u = (s + 0.5f) / skinWidthF;
		coord.v = (static_cast<float>(tCoords.at(base)) + 0.5f) / skinHeightF;
		surface.texCoords.append(coord);
	}

	// Frames. A group frame expands into one pose per member.
	QVector<MdlPose> poses;
	for (int group = 0; group < frameGroupCount; ++group) {
		if (!work.step()) { return; }
		if (!rangeOk(bytes, offset, 4)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL frame data is truncated.");
			return;
		}
		const int type = readI32(bytes, offset);
		offset += 4;
		if (type != 0 && type != 1) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL frame type is neither a single pose nor a group.");
			return;
		}
		ModelMdlFrameGroup nativeGroup;
		nativeGroup.firstFrame = poses.size();
		int memberCount = 1;
		if (type != 0) {
			if (!rangeOk(bytes, offset, 4)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL frame data is truncated.");
				return;
			}
			memberCount = readI32(bytes, offset);
			offset += 4;
			if (memberCount <= 0 || memberCount > kMaxFrames || poses.size() + memberCount > kMaxFrames) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "An MDL frame group declares an unsupported frame count.");
				return;
			}
			// Group bounding box followed by one interval per member.
			if (!rangeOk(bytes, offset, 8 + (static_cast<qint64>(memberCount) * 4))) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL frame data is truncated.");
				return;
			}
			offset += 8;
			float previous = 0;
			for (int member = 0; member < memberCount; ++member) {
				if (!work.step()) { return; }
				const float interval = readF32(bytes, offset + member * 4);
				if (!std::isfinite(interval) || interval <= previous) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDL frame group end times must be finite, positive and strictly increasing.");
					return;
				}
				nativeGroup.intervals.append(interval);
				previous = interval;
			}
			offset += static_cast<qint64>(memberCount) * 4;
		}
		mesh->mdl.frameGroups.append(nativeGroup);
		for (int member = 0; member < memberCount; ++member) {
			if (!work.step()) { return; }
			const qint64 poseBytes = 8 + kMdlFrameNameBytes + (static_cast<qint64>(vertexCount) * kMdlVertexBytes);
			if (!rangeOk(bytes, offset, poseBytes)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL frame data is truncated.");
				return;
			}
			MdlPose pose;
			pose.mins = makeVec3((scale.x * static_cast<float>(readU8(bytes, offset))) + translate.x,
				(scale.y * static_cast<float>(readU8(bytes, offset + 1))) + translate.y,
				(scale.z * static_cast<float>(readU8(bytes, offset + 2))) + translate.z);
			pose.maxs = makeVec3((scale.x * static_cast<float>(readU8(bytes, offset + 4))) + translate.x,
				(scale.y * static_cast<float>(readU8(bytes, offset + 5))) + translate.y,
				(scale.z * static_cast<float>(readU8(bytes, offset + 6))) + translate.z);
			pose.name = readFixedName(bytes, offset + 8, kMdlFrameNameBytes);
			pose.vertexOffset = offset + 8 + kMdlFrameNameBytes;
			poses.append(pose);
			offset += poseBytes;
			if (poses.size() > kMaxFrames) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL declares more frames than can be decoded.");
				return;
			}
		}
	}

	if (qint64(combinedCount) * poses.size() > kMaxVertexSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDL declares more frame vertices than can be decoded.");
		return;
	}

	int invalidNormals = 0;
	surface.frames.reserve(poses.size());
	for (int poseIndex = 0; poseIndex < poses.size(); ++poseIndex) {
		if (!work.step()) { return; }
		const MdlPose& pose = poses.at(poseIndex);
		ModelFrameGeometry geometry;
		geometry.positions.resize(combinedCount);
		geometry.normals.resize(combinedCount);
		for (int index = 0; index < combinedCount; ++index) {
			if (!work.step()) { return; }
			const qint64 vertex = pose.vertexOffset + (static_cast<qint64>(baseForCombined.at(index)) * kMdlVertexBytes);
			// Packed byte positions are decompressed with the header scale and
			// translation: position = scale * raw + translate.
			geometry.positions[index] = makeVec3((scale.x * static_cast<float>(readU8(bytes, vertex))) + translate.x,
				(scale.y * static_cast<float>(readU8(bytes, vertex + 1))) + translate.y,
				(scale.z * static_cast<float>(readU8(bytes, vertex + 2))) + translate.z);
			const int normalIndex = readU8(bytes, vertex + 3);
			if (normalIndex >= 162) { ++invalidNormals; }
			geometry.normals[index] = aliasNormal(normalIndex);
		}
		surface.frames.append(geometry);

		ModelFrameInfo info;
		info.index = poseIndex;
		info.name = pose.name;
		info.mins = pose.mins;
		info.maxs = pose.maxs;
		info.origin = makeVec3((pose.mins.x + pose.maxs.x) * 0.5f, (pose.mins.y + pose.maxs.y) * 0.5f, (pose.mins.z + pose.maxs.z) * 0.5f);
		info.radius = declaredRadius;
		mesh->frames.append(info);
	}

	if (invalidNormals > 0) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MDL contains invalid normal indices; preview uses fallback normals.");
	}
	mesh->surfaces.append(surface);
	mesh->animations = inferAnimations(mesh->frames, work);
	mesh->geometryAvailable = true;
}

// ---------------------------------------------------------------------------
// Quake II MD2 (IDP2 8)
// ---------------------------------------------------------------------------

constexpr qint64 kMd2HeaderBytes = 68;
constexpr qint64 kMd2SkinNameBytes = 64;
constexpr qint64 kMd2StBytes = 4;
constexpr qint64 kMd2TriangleBytes = 12;
constexpr qint64 kMd2FrameHeaderBytes = 40;
constexpr qint64 kMd2FrameNameBytes = 16;
constexpr qint64 kMd2VertexBytes = 4;

void decodeQuake2Md2(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(bytes, 0, kMd2HeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD2 header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 8) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MD2 version %1; only IDP2 version 8 is decoded.").arg(mesh->version);
		return;
	}

	const int skinWidth = readI32(bytes, 8);
	const int skinHeight = readI32(bytes, 12);
	mesh->md2SkinSize = {skinWidth, skinHeight};
	const int frameSize = readI32(bytes, 16);
	const int skinCount = readI32(bytes, 20);
	const int positionCount = readI32(bytes, 24);
	const int stCount = readI32(bytes, 28);
	const int triangleCount = readI32(bytes, 32);
	const int glCommandCount = readI32(bytes, 36);
	const int frameCount = readI32(bytes, 40);
	const int skinOffset = readI32(bytes, 44);
	const int stOffset = readI32(bytes, 48);
	const int triangleOffset = readI32(bytes, 52);
	const int frameOffset = readI32(bytes, 56);

	if (skinCount < 0 || skinCount > kMaxSkins
		|| positionCount <= 0 || positionCount > kMaxSurfaceVertices
		|| stCount <= 0 || stCount > kMaxSurfaceVertices
		|| triangleCount <= 0 || triangleCount > kMaxSurfaceTriangles
		|| frameCount <= 0 || frameCount > kMaxFrames) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD2 header declares counts outside the supported range.");
		return;
	}
	if (static_cast<qint64>(positionCount) * static_cast<qint64>(frameCount) > kMaxVertexSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD2 declares more frame vertices than can be decoded.");
		return;
	}
	const qint64 minimumFrameBytes = kMd2FrameHeaderBytes + (static_cast<qint64>(positionCount) * kMd2VertexBytes);
	if (frameSize < minimumFrameBytes || frameSize > (minimumFrameBytes + 4096)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD2 frame size does not match the declared vertex count.");
		return;
	}
	if (!rangeOk(bytes, skinOffset, static_cast<qint64>(skinCount) * kMd2SkinNameBytes)
		|| !rangeOk(bytes, stOffset, static_cast<qint64>(stCount) * kMd2StBytes)
		|| !rangeOk(bytes, triangleOffset, static_cast<qint64>(triangleCount) * kMd2TriangleBytes)
		|| !rangeOk(bytes, frameOffset, static_cast<qint64>(frameCount) * static_cast<qint64>(frameSize))) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "An MD2 data block lies outside the file.");
		return;
	}

	mesh->skinCount = skinCount;
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Positions: %1").arg(positionCount);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Texture coordinates: %1").arg(stCount);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "GL commands: %1").arg(glCommandCount);

	for (int index = 0; index < skinCount; ++index) {
		if (!work.step()) { return; }
		const QString skin = readFixedName(bytes, skinOffset + (static_cast<qint64>(index) * kMd2SkinNameBytes), kMd2SkinNameBytes);
		// Skin slots are indexed by the engine. Preserve duplicates and empty
		// slots so authoring validation can refuse invalid paths without shifting slots.
		mesh->skinPaths.append(skin);
	}

	ModelSurface surface;
	surface.index = 0;
	surface.name = mesh->sourcePath.isEmpty() ? QStringLiteral("surface0") : pathStem(mesh->sourcePath);
	if (surface.name.isEmpty()) {
		surface.name = QStringLiteral("surface0");
	}
	surface.skinPaths = mesh->skinPaths;

	const float skinWidthF = skinWidth > 0 ? static_cast<float>(skinWidth) : 1.0f;
	const float skinHeightF = skinHeight > 0 ? static_cast<float>(skinHeight) : 1.0f;
	if (skinWidth <= 0 || skinHeight <= 0) {
		surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MD2 skin size is zero; texture coordinates are left unscaled.");
	}

	// MD2 indexes positions and texture coordinates separately, so a combined
	// vertex list has to be built from the (position, st) pairs each triangle
	// actually uses.
	QVector<int> positionForCombined;
	QHash<qint64, int> combinedForPair;
	int skippedTriangles = 0;
	for (int index = 0; index < triangleCount; ++index) {
		if (!work.step()) { return; }
		const qint64 base = triangleOffset + (static_cast<qint64>(index) * kMd2TriangleBytes);
		int corners[3] = {0, 0, 0};
		bool valid = true;
		for (int corner = 0; corner < 3; ++corner) {
			if (!work.step()) { return; }
			const int positionIndex = static_cast<int>(readU16(bytes, base + (corner * 2)));
			const int stIndex = static_cast<int>(readU16(bytes, base + 6 + (corner * 2)));
			if (positionIndex < 0 || positionIndex >= positionCount || stIndex < 0 || stIndex >= stCount) {
				valid = false;
				break;
			}
			const qint64 key = (static_cast<qint64>(positionIndex) * static_cast<qint64>(stCount)) + static_cast<qint64>(stIndex);
			const auto existing = combinedForPair.constFind(key);
			int combined = -1;
			if (existing != combinedForPair.constEnd()) {
				combined = existing.value();
			} else {
				combined = positionForCombined.size();
				positionForCombined.append(positionIndex);
				combinedForPair.insert(key, combined);
				ModelTexCoord coord;
				// MD2's GL command convention samples the centre of each ST texel.
				coord.u = (static_cast<float>(readI16(bytes, stOffset + (static_cast<qint64>(stIndex) * kMd2StBytes))) + 0.5f) / skinWidthF;
				coord.v = (static_cast<float>(readI16(bytes, stOffset + (static_cast<qint64>(stIndex) * kMd2StBytes) + 2)) + 0.5f) / skinHeightF;
				surface.texCoords.append(coord);
			}
			corners[corner] = combined;
		}
		if (!valid) {
			++skippedTriangles;
			continue;
		}
		ModelTriangle triangle;
		triangle.a = corners[0];
		triangle.b = corners[1];
		triangle.c = corners[2];
		surface.triangles.append(triangle);
	}
	if (skippedTriangles > 0) {
		surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.").arg(skippedTriangles);
	}

	const int combinedCount = positionForCombined.size();
	if (combinedCount <= 0) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD2 contains no usable triangles.");
		return;
	}
	if (static_cast<qint64>(combinedCount) * static_cast<qint64>(frameCount) > kMaxVertexSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD2 declares more frame vertices than can be decoded.");
		return;
	}
	surface.vertexCount = combinedCount;

	surface.frames.reserve(frameCount);
	int invalidNormalIndices = 0;
	for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
		if (!work.step()) { return; }
		const qint64 base = frameOffset + (static_cast<qint64>(frameIndex) * static_cast<qint64>(frameSize));
		ModelVec3 scale;
		ModelVec3 translate;
		readVec3(bytes, base, &scale);
		readVec3(bytes, base + 12, &translate);
		if (!isFinite(scale) || !isFinite(translate)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "An MD2 frame has a non-finite scale or translation.");
			return;
		}
		const QString name = readFixedName(bytes, base + 24, kMd2FrameNameBytes);
		const qint64 vertexBase = base + kMd2FrameHeaderBytes;

		ModelFrameGeometry geometry;
		geometry.positions.resize(combinedCount);
		geometry.normals.resize(combinedCount);
		for (int index = 0; index < combinedCount; ++index) {
			if (!work.step()) { return; }
			const qint64 vertex = vertexBase + (static_cast<qint64>(positionForCombined.at(index)) * kMd2VertexBytes);
			geometry.positions[index] = makeVec3((scale.x * static_cast<float>(readU8(bytes, vertex))) + translate.x,
				(scale.y * static_cast<float>(readU8(bytes, vertex + 1))) + translate.y,
				(scale.z * static_cast<float>(readU8(bytes, vertex + 2))) + translate.z);
			const int normalIndex = static_cast<int>(readU8(bytes, vertex + 3));
			invalidNormalIndices += normalIndex >= 162;
			geometry.normals[index] = aliasNormal(normalIndex);
		}
		surface.frames.append(geometry);

		ModelFrameInfo info;
		info.index = frameIndex;
		info.name = name;
		info.mins = translate;
		info.maxs = makeVec3((scale.x * 255.0f) + translate.x, (scale.y * 255.0f) + translate.y, (scale.z * 255.0f) + translate.z);
		info.origin = makeVec3((info.mins.x + info.maxs.x) * 0.5f, (info.mins.y + info.maxs.y) * 0.5f, (info.mins.z + info.maxs.z) * 0.5f);
		mesh->frames.append(info);
	}

	if (invalidNormalIndices > 0) {
		surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MD2 has %1 invalid normal indices; preview substitutes +Z. Editable import requires valid normals.").arg(invalidNormalIndices);
	}
	QString commandError;
	if (!validateModelMd2Commands(bytes, &commandError, control)) { surface.warnings << commandError; }
	mesh->surfaces.append(surface);
	mesh->animations = inferAnimations(mesh->frames, work);
	mesh->geometryAvailable = true;
}

// ---------------------------------------------------------------------------
// Quake III MD3 (IDP3 15)
// ---------------------------------------------------------------------------

constexpr qint64 kMd3HeaderBytes = 108;
constexpr qint64 kMd3FrameBytes = 56;
constexpr qint64 kMd3TagBytes = 112;
constexpr qint64 kMd3SurfaceHeaderBytes = 108;
constexpr qint64 kMd3ShaderBytes = 68;
constexpr qint64 kMd3TriangleBytes = 12;
constexpr qint64 kMd3StBytes = 8;
constexpr qint64 kMd3VertexBytes = 8;
constexpr qint64 kMd3NameBytes = 64;
constexpr float kMd3XyzScale = 1.0f / 64.0f;

void decodeQuake3Md3(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(bytes, 0, kMd3HeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD3 header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 15) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MD3 version %1; only IDP3 version 15 is decoded.").arg(mesh->version);
		return;
	}

	const QString internalName = readFixedName(bytes, 8, kMd3NameBytes);
	const int flags = readI32(bytes, 72);
	const int frameCount = readI32(bytes, 76);
	const int tagCount = readI32(bytes, 80);
	const int surfaceCount = readI32(bytes, 84);
	const int skinCount = readI32(bytes, 88);
	const int frameOffset = readI32(bytes, 92);
	const int tagOffset = readI32(bytes, 96);
	const int surfaceOffset = readI32(bytes, 100);
	const int endOffset = readI32(bytes, 104);

	if (frameCount <= 0 || frameCount > kMaxFrames
		|| tagCount < 0 || tagCount > kMaxTags
		|| surfaceCount < 0 || surfaceCount > kMaxSurfaces
		|| skinCount < 0 || skinCount > kMaxSkins) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD3 header declares counts outside the supported range.");
		return;
	}
	if (static_cast<qint64>(tagCount) * static_cast<qint64>(frameCount) > static_cast<qint64>(kMaxTags) * 16LL) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD3 declares more tags than can be decoded.");
		return;
	}
	if (!rangeOk(bytes, frameOffset, static_cast<qint64>(frameCount) * kMd3FrameBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD3 frame block lies outside the file.");
		return;
	}
	if (tagCount > 0 && !rangeOk(bytes, tagOffset, static_cast<qint64>(tagCount) * static_cast<qint64>(frameCount) * kMd3TagBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD3 tag block lies outside the file.");
		return;
	}

	if (!internalName.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Internal name: %1").arg(internalName);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Flags: 0x%1").arg(QString::number(static_cast<uint>(flags), 16));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Declared skins: %1").arg(skinCount);
	if (endOffset > 0 && endOffset != bytes.size()) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MD3 end offset (%1) does not match the file size (%2).").arg(endOffset).arg(bytes.size());
	}

	for (int index = 0; index < frameCount; ++index) {
		if (!work.step()) { return; }
		const qint64 base = frameOffset + (static_cast<qint64>(index) * kMd3FrameBytes);
		ModelFrameInfo info;
		info.index = index;
		readVec3(bytes, base, &info.mins);
		readVec3(bytes, base + 12, &info.maxs);
		readVec3(bytes, base + 24, &info.origin);
		info.radius = readF32(bytes, base + 36);
		info.name = readFixedName(bytes, base + 40, 16);
		mesh->frames.append(info);
	}

	for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
		if (!work.step()) { return; }
		for (int index = 0; index < tagCount; ++index) {
			if (!work.step()) { return; }
			const qint64 base = tagOffset + ((static_cast<qint64>(frameIndex) * static_cast<qint64>(tagCount) + static_cast<qint64>(index)) * kMd3TagBytes);
			ModelTag tag;
			tag.name = readFixedName(bytes, base, kMd3NameBytes);
			tag.frameIndex = frameIndex;
			readVec3(bytes, base + kMd3NameBytes, &tag.origin);
			for (int row = 0; row < 3; ++row) {
				if (!work.step()) { return; }
				for (int column = 0; column < 3; ++column) {
					if (!work.step()) { return; }
					tag.axis[(row * 3) + column] = readF32(bytes, base + kMd3NameBytes + 12 + (static_cast<qint64>((row * 3) + column) * 4));
				}
			}
			mesh->tags.append(tag);
		}
	}

	// The surface chain is walked by each surface's own `ofsEnd`; every hop is
	// bounds-checked so a backwards or out-of-range offset cannot loop or read
	// past the buffer.
	qint64 offset = surfaceOffset;
	int walked = 0;
	qint64 totalVertexSlots = 0;
	QSet<QString> skinNames;
	for (; walked < surfaceCount; ++walked) {
		if (!work.step()) { return; }
		if (!rangeOk(bytes, offset, kMd3SurfaceHeaderBytes)) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 lies outside the file; the surface chain stops here.").arg(walked);
			break;
		}
		if (bytes.mid(static_cast<qsizetype>(offset), 4) != QByteArrayLiteral("IDP3")) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 has an unexpected identifier; the surface chain stops here.").arg(walked);
			break;
		}

		const QString name = readFixedName(bytes, offset + 4, kMd3NameBytes);
		const int surfaceFlags = readI32(bytes, offset + 68);
		const int surfaceFrameCount = readI32(bytes, offset + 72);
		const int shaderCount = readI32(bytes, offset + 76);
		const int vertexCount = readI32(bytes, offset + 80);
		const int triangleCount = readI32(bytes, offset + 84);
		const int triangleOffset = readI32(bytes, offset + 88);
		const int shaderOffset = readI32(bytes, offset + 92);
		const int stOffset = readI32(bytes, offset + 96);
		const int vertexOffset = readI32(bytes, offset + 100);
		const int surfaceEnd = readI32(bytes, offset + 104);

		if (surfaceEnd < kMd3SurfaceHeaderBytes || !rangeOk(bytes, offset, surfaceEnd)) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 declares an end offset outside the file; the surface chain stops here.").arg(walked);
			break;
		}
		if (surfaceFrameCount <= 0 || surfaceFrameCount > kMaxFrames
			|| shaderCount < 0 || shaderCount > kMaxSkins
			|| vertexCount <= 0 || vertexCount > kMaxSurfaceVertices
			|| triangleCount < 0 || triangleCount > kMaxSurfaceTriangles) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 declares counts outside the supported range and was skipped.").arg(walked);
			offset += surfaceEnd;
			continue;
		}
		const int usableFrames = std::min(surfaceFrameCount, frameCount);
		if (static_cast<qint64>(vertexCount) * static_cast<qint64>(usableFrames) > kMaxVertexSlots) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 declares more frame vertices than can be decoded and was skipped.").arg(walked);
			offset += surfaceEnd;
			continue;
		}
		// Every sub-block offset is relative to the start of the surface and
		// must stay inside the surface record.
		const qint64 shaderBytes = static_cast<qint64>(shaderCount) * kMd3ShaderBytes;
		const qint64 triangleBytes = static_cast<qint64>(triangleCount) * kMd3TriangleBytes;
		const qint64 stBytes = static_cast<qint64>(vertexCount) * kMd3StBytes;
		const qint64 vertexBytes = static_cast<qint64>(vertexCount) * static_cast<qint64>(usableFrames) * kMd3VertexBytes;
		const auto blockOk = [&](qint64 blockOffset, qint64 blockLength) {
			if (blockOffset < 0 || blockLength < 0) {
				return false;
			}
			if (blockLength > surfaceEnd - blockOffset) {
				return false;
			}
			return rangeOk(bytes, offset + blockOffset, blockLength);
		};
		if (!blockOk(shaderOffset, shaderBytes) || !blockOk(triangleOffset, triangleBytes)
			|| !blockOk(stOffset, stBytes) || !blockOk(vertexOffset, vertexBytes)) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 has a data block outside the surface record and was skipped.").arg(walked);
			offset += surfaceEnd;
			continue;
		}

		const qint64 surfaceVertexSlots = qint64(vertexCount) * frameCount;
		if (surfaceVertexSlots > kMaxVertexSlots - totalVertexSlots) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MD3 declares more total frame vertices than can be decoded.");
			return;
		}
		totalVertexSlots += surfaceVertexSlots;

		ModelSurface surface;
		surface.index = mesh->surfaces.size();
		surface.name = name.isEmpty() ? QStringLiteral("surface%1").arg(surface.index) : name;
		surface.vertexCount = vertexCount;
		if (surfaceFlags != 0) {
			surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "Surface flags: 0x%1").arg(QString::number(static_cast<uint>(surfaceFlags), 16));
		}
		if (surfaceFrameCount != frameCount) {
			surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "The surface declares %1 frame(s) while the model declares %2.").arg(surfaceFrameCount).arg(frameCount);
		}

		for (int index = 0; index < shaderCount; ++index) {
			if (!work.step()) { return; }
			const QString shader = readFixedName(bytes, offset + shaderOffset + (static_cast<qint64>(index) * kMd3ShaderBytes), kMd3NameBytes);
			if (shader.isEmpty()) {
				continue;
			}
			surface.skinPaths.append(shader);
			if (!skinNames.contains(shader)) {
				skinNames.insert(shader);
				mesh->skinPaths.append(shader);
			}
		}

		surface.texCoords.reserve(vertexCount);
		for (int index = 0; index < vertexCount; ++index) {
			if (!work.step()) { return; }
			const qint64 base = offset + stOffset + (static_cast<qint64>(index) * kMd3StBytes);
			ModelTexCoord coord;
			coord.u = readF32(bytes, base);
			coord.v = readF32(bytes, base + 4);
			surface.texCoords.append(coord);
		}

		int skippedTriangles = 0;
		surface.triangles.reserve(triangleCount);
		for (int index = 0; index < triangleCount; ++index) {
			if (!work.step()) { return; }
			const qint64 base = offset + triangleOffset + (static_cast<qint64>(index) * kMd3TriangleBytes);
			const int a = readI32(bytes, base);
			const int b = readI32(bytes, base + 4);
			const int c = readI32(bytes, base + 8);
			if (a < 0 || a >= vertexCount || b < 0 || b >= vertexCount || c < 0 || c >= vertexCount) {
				++skippedTriangles;
				continue;
			}
			ModelTriangle triangle;
			triangle.a = a;
			triangle.b = b;
			triangle.c = c;
			surface.triangles.append(triangle);
		}
		if (skippedTriangles > 0) {
			surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.").arg(skippedTriangles);
		}

		surface.frames.reserve(frameCount);
		for (int frameIndex = 0; frameIndex < usableFrames; ++frameIndex) {
			if (!work.step()) { return; }
			ModelFrameGeometry geometry;
			geometry.positions.resize(vertexCount);
			geometry.normals.resize(vertexCount);
			for (int index = 0; index < vertexCount; ++index) {
				if (!work.step()) { return; }
				const qint64 base = offset + vertexOffset
					+ (((static_cast<qint64>(frameIndex) * static_cast<qint64>(vertexCount)) + static_cast<qint64>(index)) * kMd3VertexBytes);
				// int16 positions are stored in 1/64 unit steps.
				geometry.positions[index] = makeVec3(static_cast<float>(readI16(bytes, base)) * kMd3XyzScale,
					static_cast<float>(readI16(bytes, base + 2)) * kMd3XyzScale,
					static_cast<float>(readI16(bytes, base + 4)) * kMd3XyzScale);
				geometry.normals[index] = md3Normal(readU16(bytes, base + 6));
			}
			surface.frames.append(geometry);
		}
		while (surface.frames.size() < frameCount && !surface.frames.isEmpty()) {
			if (!work.step()) { return; }
			// Keep the surface parallel with the model frame list.
			surface.frames.append(surface.frames.last());
		}

		mesh->surfaces.append(surface);
		offset += surfaceEnd;
	}
	if (walked < surfaceCount) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Only %1 of %2 surface(s) could be read.").arg(walked).arg(surfaceCount);
	}

	mesh->skinCount = std::max(skinCount, static_cast<int>(mesh->skinPaths.size()));
	mesh->animations = inferAnimations(mesh->frames, work);
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
	if (!mesh->geometryAvailable) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "No MD3 surface could be decoded.");
	}
}

// ---------------------------------------------------------------------------
// Skin resolution helpers
// ---------------------------------------------------------------------------

QStringList idTechImageExtensions()
{
	return {QStringLiteral("pcx"), QStringLiteral("tga"), QStringLiteral("jpg"),
		QStringLiteral("png"), QStringLiteral("wal"), QStringLiteral("lmp")};
}

bool findArchiveEntryPath(const PackageArchiveReader& archive, const QString& candidate, QString* pathOut)
{
	if (candidate.isEmpty()) {
		return false;
	}
	const QVector<PackageEntry> entries = archive.entries();
	for (const PackageEntry& entry : entries) {
		if (entry.kind == PackageEntryKind::File && entry.virtualPath == candidate) {
			*pathOut = entry.virtualPath;
			return true;
		}
	}
	for (const PackageEntry& entry : entries) {
		if (entry.kind == PackageEntryKind::File && entry.virtualPath.compare(candidate, Qt::CaseInsensitive) == 0) {
			*pathOut = entry.virtualPath;
			return true;
		}
	}
	return false;
}

QStringList skinCandidatePaths(const QString& skinPath)
{
	QStringList candidates;
	QString normalized = skinPath;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	while (normalized.startsWith(QLatin1Char('/'))) {
		normalized.remove(0, 1);
	}
	if (normalized.isEmpty()) {
		return candidates;
	}
	candidates.append(normalized);
	QString stem = normalized;
	const int dot = stem.lastIndexOf(QLatin1Char('.'));
	const int slash = stem.lastIndexOf(QLatin1Char('/'));
	if (dot > slash && dot > 0) {
		stem = stem.left(dot);
	}
	for (const QString& extension : idTechImageExtensions()) {
		const QString candidate = stem + QLatin1Char('.') + extension;
		if (!candidates.contains(candidate, Qt::CaseInsensitive)) {
			candidates.append(candidate);
		}
	}
	return candidates;
}

} // namespace

QStringList modelSkinCandidatePaths(const QString& skinPath) { return skinCandidatePaths(skinPath); }

// Original consistency audit of the MD2 streams defined in Quake II's
// qcommon/qfiles.h and consumed by ref_gl/gl_mesh.c (GPL-2.0-or-later,
// master reviewed 2026-10-04). The preview decoder reads indexed triangles;
// refuse authoring if adopting them would discard different GL geometry/UVs.
bool validateModelMd2Commands(const QByteArray &bytes, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	const auto invalid = [&]
	{
		if (error)
		{
			*error = QCoreApplication::translate(
				"VibeStudioModelMesh",
				"MD2 GL commands are malformed, exceed the authoring audit limit, or differ from the indexed triangles/UVs. Editable "
				"import would lose renderer-specific data; use the original file for preview.");
		}
		return false;
	};
	if (bytes.size() < 68)
	{
		return invalid();
	}
	const auto integer = [&](qint64 offset) { return qFromLittleEndian<qint32>(bytes.constData() + offset); };
	const auto number = [&](qint64 offset)
	{
		quint32 bits = qFromLittleEndian<quint32>(bytes.constData() + offset);
		float value;
		std::memcpy(&value, &bits, 4);
		return value;
	};
	const auto range = [&](qint64 start, qint64 count)
	{ return start >= 68 && count >= 0 && start <= bytes.size() && count <= bytes.size() - start; };
	const int words = integer(36), width = integer(8), height = integer(12), positions = integer(24), sts = integer(28),
			  triangles = integer(32);
	const qint64 commands = integer(60), stOffset = integer(48), triangleOffset = integer(52);
	if (words == 0)
	{
		return work.check();
	} // Software-only MD2; no second representation to lose.
	if (words < 0 || width <= 0 || height <= 0 || triangles <= 0 || triangles > 131072 || sts <= 0 || !range(commands, qint64(words) * 4) ||
		!range(stOffset, qint64(sts) * 4) || !range(triangleOffset, qint64(triangles) * 12))
	{
		return invalid();
	}
	using Corner = std::array<int, 3>; // XYZ index and integer ST coordinate.
	const auto key = [](std::array<Corner, 3> corners)
	{
		int first = 0;
		for (int i = 1; i < 3; ++i)
		{
			if (corners[i] < corners[first])
			{
				first = i;
			}
		}
		QByteArray result(36, '\0');
		for (int c = 0; c < 3; ++c)
		{
			for (int j = 0; j < 3; ++j)
			{
				qToLittleEndian(qint32(corners[(first + c) % 3][j]), result.data() + c * 12 + j * 4);
			}
		}
		return result;
	};
	QHash<QByteArray, int> faces;
	for (int t = 0; t < triangles; ++t)
	{
		if (!work.step())
		{
			return false;
		}
		std::array<Corner, 3> corners;
		for (int c = 0; c < 3; ++c)
		{
			const int p = qFromLittleEndian<quint16>(bytes.constData() + triangleOffset + t * 12 + c * 2);
			const int st = qFromLittleEndian<quint16>(bytes.constData() + triangleOffset + t * 12 + 6 + c * 2);
			if (p >= positions || st >= sts)
			{
				return invalid();
			}
			corners[c] = {p, qFromLittleEndian<qint16>(bytes.constData() + stOffset + st * 4),
						  qFromLittleEndian<qint16>(bytes.constData() + stOffset + st * 4 + 2)};
		}
		++faces[key(corners)];
	}
	int cursor = 0;
	bool terminated = false;
	while (cursor < words)
	{
		if (!work.step())
		{
			return false;
		}
		const int command = integer(commands + qint64(cursor++) * 4);
		if (!command)
		{
			terminated = true;
			break;
		}
		const qint64 count = std::abs(qint64(command));
		if (count < 3 || count > (words - cursor) / 3)
		{
			return invalid();
		}
		Corner first{}, previous{}, beforePrevious{};
		for (int v = 0; v < count; ++v)
		{
			if (!work.step())
			{
				return false;
			}
			const auto offset = commands + qint64(cursor) * 4;
			const double u = number(offset), textureV = number(offset + 4);
			const double s = u * width - 0.5, t = textureV * height - 0.5;
			const int position = integer(offset + 8);
			if (!std::isfinite(s) || !std::isfinite(t) || s < -32768.01 || s > 32767.01 || t < -32768.01 || t > 32767.01 || position < 0 ||
				position >= positions)
			{
				return invalid();
			}
			const int si = int(std::lround(s)), ti = int(std::lround(t));
			if (std::abs(u - (si + 0.5f) / width) > 1e-6 || std::abs(textureV - (ti + 0.5f) / height) > 1e-6)
			{
				return invalid();
			}
			const Corner current{position, si, ti};
			if (v == 0)
			{
				first = current;
			}
			if (v >= 2)
			{
				std::array<Corner, 3> triangle = command < 0 ? std::array<Corner, 3>{first, previous, current}
															 : (v % 2 ? std::array<Corner, 3>{previous, beforePrevious, current}
																	  : std::array<Corner, 3>{beforePrevious, previous, current});
				// Degenerate connectors in strips emit no rendered triangle.
				if (triangle[0][0] != triangle[1][0] && triangle[1][0] != triangle[2][0] && triangle[2][0] != triangle[0][0])
				{
					auto found = faces.find(key(triangle));
					if (found == faces.end())
					{
						return invalid();
					}
					if (--found.value() == 0)
					{
						faces.erase(found);
					}
				}
			}
			beforePrevious = previous;
			previous = current;
			cursor += 3;
		}
	}
	if (!terminated || !faces.isEmpty())
	{
		return invalid();
	}
	while (cursor < words)
	{
		if (!work.step())
		{
			return false;
		}
		if (integer(commands + qint64(cursor++) * 4))
		{
			return invalid();
		}
	}
	return work.check();
}

int modelAliasNormalIndex(const ModelVec3& normal)
{
	static const auto directions = [] {
		std::array<std::array<double, 3>, 162> result{};
		for (int i = 0; i < 162; ++i) {
			const auto& n = kAliasNormals[i];
			const double length = std::sqrt(double(n[0]) * n[0] + double(n[1]) * n[1] + double(n[2]) * n[2]);
			result[i] = {n[0] / length, n[1] / length, n[2] / length};
		}
		return result;
	}();
	int best = 0;
	double bestDot = -std::numeric_limits<double>::infinity();
	for (int i = 0; i < 162; ++i) {
		const auto& n = directions[i];
		const double dot = double(normal.x) * n[0] + double(normal.y) * n[1] + double(normal.z) * n[2];
		if (dot > bestDot) { bestDot = dot; best = i; }
	}
	return best;
}

ModelVec3 modelAliasNormal(int index) { return aliasNormal(index); }

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool ModelMesh::isValid() const
{
	return error.isEmpty() && format != ModelMeshFormat::Unknown;
}

float ModelMesh::boundingRadius() const
{
	float radius = 0.0f;
	bool havePositions = false;
	for (const ModelSurface& surface : surfaces) {
		for (const ModelFrameGeometry& geometry : surface.frames) {
			for (const ModelVec3& position : geometry.positions) {
				if (!isFinite(position)) {
					continue;
				}
				havePositions = true;
				const float length = std::sqrt((position.x * position.x) + (position.y * position.y) + (position.z * position.z));
				radius = std::max(radius, length);
			}
		}
	}
	if (havePositions) {
		return radius;
	}
	const float corners[6] = {mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z};
	for (int axis = 0; axis < 3; ++axis) {
		const float low = std::fabs(corners[axis]);
		const float high = std::fabs(corners[axis + 3]);
		radius = std::max(radius, std::max(low, high));
	}
	for (const ModelFrameInfo& frame : frames) {
		if (std::isfinite(frame.radius)) {
			radius = std::max(radius, frame.radius);
		}
	}
	return radius;
}

namespace {

struct FormatRow {
	ModelMeshFormat format;
	const char* id;
	const char* name;
	// Suffixes, '|' between them.
	const char* suffixes;
	bool skeletal;
	bool animationOnly;
};

// Stable ids are written to JSON and receipts; display names are product and
// format names, which stay untranslated.
constexpr FormatRow kFormats[] = {
	{ModelMeshFormat::WavefrontObj, "obj", "Wavefront OBJ", "obj", false, false},
	{ModelMeshFormat::QuakeMdl, "mdl", "Quake MDL", "mdl", false, false},
	{ModelMeshFormat::Quake2Md2, "md2", "Quake II MD2", "md2", false, false},
	{ModelMeshFormat::Quake3Md3, "md3", "Quake III MD3", "md3", false, false},
	{ModelMeshFormat::Mdc, "mdc", "MDC", "mdc", false, false},
	{ModelMeshFormat::Mdr, "mdr", "MDR", "mdr", true, false},
	{ModelMeshFormat::Iqm, "iqm", "Inter-Quake Model", "iqm", true, false},
	{ModelMeshFormat::Md5Mesh, "md5mesh", "MD5 mesh", "md5mesh", true, false},
	{ModelMeshFormat::Md5Anim, "md5anim", "MD5 animation", "md5anim", true, true},
	{ModelMeshFormat::Mds, "mds", "MDS", "mds", true, false},
	{ModelMeshFormat::Mdm, "mdm", "MDM", "mdm", true, false},
	{ModelMeshFormat::Mdx, "mdx", "MDX", "mdx", true, true},
	{ModelMeshFormat::Glm, "glm", "Ghoul 2 GLM", "glm", true, false},
	{ModelMeshFormat::Gla, "gla", "Ghoul 2 GLA", "gla", true, true},
	{ModelMeshFormat::HalfLifeMdl, "studio-mdl", "Half-Life MDL", "mdl", true, false},
	{ModelMeshFormat::Hexen2Mdl, "hexen2-mdl", "Hexen II MDL", "mdl", false, false},
	{ModelMeshFormat::LightWave, "lwo", "LightWave LWO", "lwo", false, false},
	{ModelMeshFormat::Ase, "ase", "ASCII Scene Export", "ase", false, false},
	{ModelMeshFormat::HereticFm, "fm", "Heretic II FM", "fm", false, false},
	{ModelMeshFormat::Kvx, "kvx", "KVX voxels", "kvx", false, false},
};

const FormatRow* formatRow(ModelMeshFormat format)
{
	for (const FormatRow& row : kFormats) {
		if (row.format == format) {
			return &row;
		}
	}
	return nullptr;
}

// The first token of a text file after whitespace and // or /* */ comments.
QByteArray firstTextToken(const QByteArray& bytes)
{
	qsizetype index = 0;
	const qsizetype end = std::min<qsizetype>(bytes.size(), 4096);
	while (index < end) {
		const char ch = bytes.at(index);
		if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
			++index;
		} else if (ch == '/' && index + 1 < end && bytes.at(index + 1) == '/') {
			while (index < end && bytes.at(index) != '\n') { ++index; }
		} else if (ch == '/' && index + 1 < end && bytes.at(index + 1) == '*') {
			index += 2;
			while (index + 1 < end && !(bytes.at(index) == '*' && bytes.at(index + 1) == '/')) { ++index; }
			index += 2;
		} else {
			break;
		}
	}
	qsizetype stop = index;
	while (stop < end && bytes.at(stop) != ' ' && bytes.at(stop) != '\t' && bytes.at(stop) != '\r' && bytes.at(stop) != '\n') {
		++stop;
	}
	return bytes.mid(index, stop - index);
}

} // namespace

QString modelMeshFormatId(ModelMeshFormat format)
{
	const FormatRow* row = formatRow(format);
	return row ? QString::fromLatin1(row->id) : QStringLiteral("unknown");
}

QString modelMeshFormatDisplayName(ModelMeshFormat format)
{
	const FormatRow* row = formatRow(format);
	return row ? QString::fromLatin1(row->name) : QCoreApplication::translate("VibeStudioModelMesh", "Unknown model");
}

bool modelMeshFormatIsSkeletal(ModelMeshFormat format)
{
	const FormatRow* row = formatRow(format);
	return row && row->skeletal;
}

bool modelMeshFormatIsAnimationOnly(ModelMeshFormat format)
{
	const FormatRow* row = formatRow(format);
	return row && row->animationOnly;
}

QStringList modelMeshFileSuffixes()
{
	QStringList suffixes;
	for (const FormatRow& row : kFormats) {
		for (const QString& suffix : QString::fromLatin1(row.suffixes).split(QLatin1Char('|'))) {
			if (!suffixes.contains(suffix)) {
				suffixes.append(suffix);
			}
		}
	}
	return suffixes;
}

namespace {

struct CatalogEntry {
	ModelMeshFormat format;
	// '|' between entries.
	const char* engines;
	const char* games;
	const char* companions;
	// The exportEditableModel id that writes this format, or empty.
	const char* exportId;
	const char* notes;
};

// What each format is for and what the studio keeps of it. Notes are
// translated where read.
constexpr CatalogEntry kCatalog[] = {
	{ModelMeshFormat::QuakeMdl, "idtech2", "Quake|Quake mission packs|Quake source ports", "", "mdl",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Every pose, frame group and timing, indexed skins and skin groups, header fields; the palette stays external.")},
	{ModelMeshFormat::Hexen2Mdl, "idtech2", "Hexen II: Portal of Praevus", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Mission-pack alias models with per-corner texture coordinates; read only.")},
	{ModelMeshFormat::Quake2Md2, "idtech2|idtech1", "Quake II|GZDoom and Zandronum (MODELDEF)", "", "md2",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Every pose, ordered skin names and skin size; one surface.")},
	{ModelMeshFormat::HereticFm, "idtech2", "Heretic II", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Flexible models: frames, skins and mesh nodes as surfaces; read only.")},
	{ModelMeshFormat::Quake3Md3, "idtech3|idtech2|idtech1",
		"Quake III Arena|Team Arena|Return to Castle Wolfenstein|Wolfenstein: Enemy Territory|Star Trek: Elite Force|Jedi Outcast|Jedi Academy|Darkplaces|GZDoom (MODELDEF)",
		"", "md3", QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Surfaces, shaders, every frame and tags; detail levels as name_1.md3 and name_2.md3.")},
	{ModelMeshFormat::Mdc, "idtech3", "Return to Castle Wolfenstein|Wolfenstein: Enemy Territory", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Compressed MD3: base and compressed frames, tags and shaders; read only (export MD3, which both games load).")},
	{ModelMeshFormat::Mds, "idtech3", "Return to Castle Wolfenstein", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Skeletal characters: bones, weights, frames and tags, baked to frames for viewing and MD3 export; read only.")},
	{ModelMeshFormat::Mdm, "idtech3", "Wolfenstein: Enemy Territory", "mdx", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Skeletal meshes skinned against the MDX that holds their bones; read only.")},
	{ModelMeshFormat::Mdx, "idtech3", "Wolfenstein: Enemy Territory", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Bones and frames without geometry; read only.")},
	{ModelMeshFormat::Mdr, "idtech3", "Star Trek: Elite Force|ioquake3 games", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Skeletal models with per-frame bone matrices; the first detail level is read.")},
	{ModelMeshFormat::Glm, "idtech3", "Jedi Outcast|Jedi Academy|Soldier of Fortune II", "gla", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Ghoul 2 meshes with the GLA skeleton they name; surface hierarchy and off surfaces are listed; read only.")},
	{ModelMeshFormat::Gla, "idtech3", "Jedi Outcast|Jedi Academy|Soldier of Fortune II", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Ghoul 2 skeletons and their frames without geometry; read only.")},
	{ModelMeshFormat::Iqm, "idtech3|idtech2", "ioquake3|Spearmint|Darkplaces|FTEQW|Xonotic", "", "iqm",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Skeletal meshes with joints, weights and animations; static meshes too.")},
	{ModelMeshFormat::Md5Mesh, "idtech4", "Doom 3|Resurrection of Evil|Quake 4|Prey|Enemy Territory: Quake Wars|The Dark Mod", "md5anim|def",
		"md5mesh", QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Joints, weights and every md5anim beside the mesh or named by a Doom 3 .def model declaration.")},
	{ModelMeshFormat::Md5Anim, "idtech4", "Doom 3|Resurrection of Evil|Quake 4|Prey|Enemy Territory: Quake Wars|The Dark Mod", "", "md5anim",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "One animation: the joint hierarchy, base frame, bounds and frames.")},
	{ModelMeshFormat::LightWave, "idtech4|idtech3", "Doom 3|Quake 4|q3map2 misc_model", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Static objects: points, polygons, UV maps and surface names as materials; read only.")},
	{ModelMeshFormat::Ase, "idtech4|idtech3", "Doom 3|Quake 4|q3map2 misc_model", "", "ase",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Static scenes: objects, materials and mapping; the first mesh of each object.")},
	{ModelMeshFormat::HalfLifeMdl, "goldsrc", "Half-Life|Counter-Strike|Team Fortress Classic|Day of Defeat|Sven Co-op", "T.mdl|01.mdl", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Bones, sequences, body parts, embedded textures and attachments, with texture and sequence group files; read only.")},
	{ModelMeshFormat::Kvx, "idtech1", "GZDoom|ZDoom|Zandronum|Eternity (VOXELDEF)", "", "",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "Voxel sprites meshed into faces, coloured from the palette in the file; read only.")},
	{ModelMeshFormat::WavefrontObj, "interchange", "Any modeller", "", "obj",
		QT_TRANSLATE_NOOP("VibeStudioModelMesh", "One pose of polygon geometry with UVs, normals and material names; no MTL library.")},
};

} // namespace

const ModelEmbeddedSkin* modelEmbeddedSkinForSurface(const ModelMesh& mesh, int surface)
{
	if (mesh.embeddedSkins.isEmpty()) {
		return nullptr;
	}
	const QString wanted = surface >= 0 && surface < mesh.surfaces.size() ? mesh.surfaces.at(surface).skinPaths.value(0) : QString();
	if (!wanted.isEmpty()) {
		for (const ModelEmbeddedSkin& skin : mesh.embeddedSkins) {
			if (skin.name.compare(wanted, Qt::CaseInsensitive) == 0) {
				return &skin;
			}
		}
	}
	return &mesh.embeddedSkins.first();
}

QVector<ModelFormatCapability> modelFormatCapabilities()
{
	const QStringList exports = modelExportFormatIds();
	QVector<ModelFormatCapability> capabilities;
	for (const CatalogEntry& entry : kCatalog) {
		const FormatRow* row = formatRow(entry.format);
		if (!row) {
			continue;
		}
		ModelFormatCapability capability;
		capability.format = entry.format;
		capability.id = QString::fromLatin1(row->id);
		capability.name = QString::fromLatin1(row->name);
		capability.suffixes = QString::fromLatin1(row->suffixes).split(QLatin1Char('|'));
		capability.engines = QString::fromLatin1(entry.engines).split(QLatin1Char('|'), Qt::SkipEmptyParts);
		capability.games = QString::fromUtf8(entry.games).split(QLatin1Char('|'), Qt::SkipEmptyParts);
		capability.skeletal = row->skeletal;
		capability.animationOnly = row->animationOnly;
		capability.companions = QString::fromLatin1(entry.companions).split(QLatin1Char('|'), Qt::SkipEmptyParts);
		capability.writes = *entry.exportId && exports.contains(QString::fromLatin1(entry.exportId));
		capability.notes = QCoreApplication::translate("VibeStudioModelMesh", entry.notes);
		capabilities.append(capability);
	}
	return capabilities;
}

ModelMeshFormat detectModelMeshFormat(const QString& virtualPath, const QByteArray& bytes)
{
	const QString suffix = pathSuffix(virtualPath);
	if (suffix == QStringLiteral("obj")) {
		return ModelMeshFormat::WavefrontObj;
	}
	if (bytes.size() >= 16 && bytes.startsWith(QByteArrayLiteral("INTERQUAKEMODEL"))) {
		return ModelMeshFormat::Iqm;
	}
	if (bytes.size() >= 4) {
		const QByteArray magic = bytes.left(4);
		struct Magic {
			const char* bytes;
			ModelMeshFormat format;
		};
		static const Magic kMagics[] = {
			{"IDPO", ModelMeshFormat::QuakeMdl},
			{"RAPO", ModelMeshFormat::Hexen2Mdl},
			{"IDP2", ModelMeshFormat::Quake2Md2},
			{"IDP3", ModelMeshFormat::Quake3Md3},
			{"IDPC", ModelMeshFormat::Mdc},
			{"RDM5", ModelMeshFormat::Mdr},
			{"MDSW", ModelMeshFormat::Mds},
			{"MDMW", ModelMeshFormat::Mdm},
			{"MDXW", ModelMeshFormat::Mdx},
			{"2LGM", ModelMeshFormat::Glm},
			{"2LGA", ModelMeshFormat::Gla},
			{"IDST", ModelMeshFormat::HalfLifeMdl},
			{"IDSQ", ModelMeshFormat::HalfLifeMdl},
		};
		for (const Magic& entry : kMagics) {
			if (magic == QByteArray(entry.bytes, 4)) {
				return entry.format;
			}
		}
		if (magic == QByteArrayLiteral("FORM") && bytes.size() >= 12) {
			const QByteArray kind = bytes.mid(8, 4);
			if (kind == QByteArrayLiteral("LWO2") || kind == QByteArrayLiteral("LWOB") || kind == QByteArrayLiteral("LWLO")) {
				return ModelMeshFormat::LightWave;
			}
		}
		// Heretic II's chunked flexible model opens with a "header" chunk.
		if (bytes.size() >= 40 && bytes.startsWith(QByteArrayLiteral("header"))) {
			return ModelMeshFormat::HereticFm;
		}
	}
	const QByteArray token = firstTextToken(bytes);
	if (token == QByteArrayLiteral("MD5Version")) {
		if (suffix == QStringLiteral("md5anim")) {
			return ModelMeshFormat::Md5Anim;
		}
		if (suffix == QStringLiteral("md5mesh")) {
			return ModelMeshFormat::Md5Mesh;
		}
		return bytes.contains("numMeshes") ? ModelMeshFormat::Md5Mesh : ModelMeshFormat::Md5Anim;
	}
	if (token == QByteArrayLiteral("*3DSMAX_ASCIIEXPORT")) {
		return ModelMeshFormat::Ase;
	}
	// Fall back to the extension so a damaged file still reports which decoder
	// was expected instead of a bare "unknown". ".mdl" means Quake's format.
	if (suffix == QStringLiteral("mdl")) {
		return ModelMeshFormat::QuakeMdl;
	}
	for (const FormatRow& row : kFormats) {
		if (row.format == ModelMeshFormat::HalfLifeMdl || row.format == ModelMeshFormat::Hexen2Mdl) {
			continue;
		}
		if (QString::fromLatin1(row.suffixes).split(QLatin1Char('|')).contains(suffix)) {
			return row.format;
		}
	}
	return ModelMeshFormat::Unknown;
}

ModelMesh decodeModelMesh(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette* palette, const ModelWorkControl& control)
{
	return decodeModelMesh(virtualPath, bytes, palette, control, ModelCompanionSource{});
}

ModelMesh decodeModelMesh(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette* palette, const ModelWorkControl& control,
	const ModelCompanionSource& companionSource)
{
	ModelMesh mesh;
	mesh.sourcePath = virtualPath;
	mesh.format = detectModelMeshFormat(virtualPath, bytes);
	mesh.formatId = modelMeshFormatId(mesh.format);
	mesh.formatName = modelMeshFormatDisplayName(mesh.format);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, &mesh.error)) { return mesh; }
	if (mesh.format == ModelMeshFormat::Unknown) {
		mesh.error = QCoreApplication::translate("VibeStudioModelMesh", "The file is not a recognised idTech model.");
		return mesh;
	}
	if (bytes.isEmpty()) {
		mesh.error = QCoreApplication::translate("VibeStudioModelMesh", "The model file is empty.");
		return mesh;
	}

	IdTechPalette effectivePalette;
	if (palette && palette->isValid()) {
		effectivePalette = *palette;
	} else {
		effectivePalette = generatedIdTechPalette(QStringLiteral("quake"));
	}

	model_formats::Companions companions(&companionSource, &mesh);
	switch (mesh.format) {
	case ModelMeshFormat::WavefrontObj:
		return decodeModelObj(virtualPath, bytes, control);
	case ModelMeshFormat::QuakeMdl:
		decodeQuakeMdl(bytes, effectivePalette, &mesh, control);
		break;
	case ModelMeshFormat::Quake2Md2:
		decodeQuake2Md2(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Quake3Md3:
		decodeQuake3Md3(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Mdc:
		model_formats::decodeMdc(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Mdr:
		model_formats::decodeMdr(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Iqm:
		model_formats::decodeIqm(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Md5Mesh:
		model_formats::decodeMd5Mesh(virtualPath, bytes, &mesh, control, companions);
		break;
	case ModelMeshFormat::Md5Anim:
		model_formats::decodeMd5Anim(virtualPath, bytes, &mesh, control);
		break;
	case ModelMeshFormat::Mds:
		model_formats::decodeMds(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Mdm:
		model_formats::decodeMdm(virtualPath, bytes, &mesh, control, companions);
		break;
	case ModelMeshFormat::Mdx:
		model_formats::decodeMdx(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Glm:
		model_formats::decodeGlm(virtualPath, bytes, &mesh, control, companions);
		break;
	case ModelMeshFormat::Gla:
		model_formats::decodeGla(bytes, &mesh, control);
		break;
	case ModelMeshFormat::HalfLifeMdl:
		model_formats::decodeHalfLifeMdl(virtualPath, bytes, &mesh, control, companions);
		break;
	case ModelMeshFormat::Hexen2Mdl:
		model_formats::decodeHexen2Mdl(bytes, effectivePalette, &mesh, control);
		break;
	case ModelMeshFormat::LightWave:
		model_formats::decodeLightWave(virtualPath, bytes, &mesh, control);
		break;
	case ModelMeshFormat::Ase:
		model_formats::decodeAse(virtualPath, bytes, &mesh, control);
		break;
	case ModelMeshFormat::HereticFm:
		model_formats::decodeHereticFm(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Kvx:
		model_formats::decodeKvx(bytes, &mesh, control);
		break;
	case ModelMeshFormat::Unknown:
		break;
	}

	const bool legacyWinding = mesh.format == ModelMeshFormat::QuakeMdl || mesh.format == ModelMeshFormat::Quake2Md2
		|| mesh.format == ModelMeshFormat::Quake3Md3;
	if (mesh.error.isEmpty() && mesh.geometryAvailable && legacyWinding) {
		// MDL/MD2/MD3 use clockwise front faces. The editable mesh and OBJ use
		// cross(b-a, c-a) for outward normals. Convert after the native MD2
		// command-stream audit, retaining every corner's UV/normal identity.
		// Original id renderers use GL_FRONT culling; see docs/CREDITS.md.
		// The decoders in model_format_*.cpp emit counter-clockwise faces.
		ModelWorkProgress winding(control, ModelWorkPhase::Validating, &mesh.error);
		for (auto &surface : mesh.surfaces) {
			for (auto &triangle : surface.triangles) {
				if (!winding.step()) { break; }
				std::swap(triangle.b, triangle.c);
			}
			if (!winding.check()) { break; }
		}
	}
	if (mesh.error.isEmpty() && !mesh.skeleton.isEmpty()) {
		QString problem;
		if (!validateModelSkeleton(mesh, &problem)) {
			mesh.error = problem;
		}
	}
	if (mesh.error.isEmpty() && (mesh.geometryAvailable || !mesh.frames.isEmpty())) {
		finalizeMesh(&mesh, control);
	}
	modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, &mesh.error);
	if (!mesh.error.isEmpty()) {
		// Never publish partially decoded geometry, animation or embedded skins.
		ModelMesh failed;
		failed.sourcePath = mesh.sourcePath;
		failed.format = mesh.format;
		failed.formatId = mesh.formatId;
		failed.formatName = mesh.formatName;
		failed.version = mesh.version;
		failed.error = mesh.error;
		return failed;
	}
	return mesh;
}

ModelMesh decodeModelMeshFile(const QString& path, const IdTechPalette* palette, const ModelWorkControl& control)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		ModelMesh mesh;
		mesh.sourcePath = path;
		mesh.error = QCoreApplication::translate("VibeStudioModelMesh", "The model file could not be opened: %1").arg(file.errorString());
		return mesh;
	}
	const QByteArray bytes = file.readAll();
	QString normalized = path;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return decodeModelMesh(normalized, bytes, palette, control, modelCompanionsFromFileSystem(path));
}

ModelMesh decodeModelMeshFromArchive(const PackageArchiveReader& archive, const QString& virtualPath, const QString& paletteId,
	const ModelWorkControl& control)
{
	const ModelArchiveReader reader(archive, control);
	QByteArray bytes;
	QString error;
	if (!reader.readEntryBytes(virtualPath, &bytes, &error)) {
		ModelMesh mesh;
		mesh.sourcePath = virtualPath;
		mesh.error = error;
		return mesh;
	}
	// Only MDL embeds palette indices. Palette reads use the same verified,
	// bounded snapshot as geometry and check cancellation while streaming.
	IdTechPaletteResolution resolution;
	const ModelMeshFormat detected = detectModelMeshFormat(virtualPath, bytes);
	if (detected == ModelMeshFormat::QuakeMdl || detected == ModelMeshFormat::Hexen2Mdl) {
		resolution = resolveIdTechPalette(reader, paletteId.isEmpty() ? QStringLiteral("quake") : paletteId);
	}
	ModelMesh mesh = decodeModelMesh(virtualPath, bytes, &resolution.palette, control, modelCompanionsFromArchive(archive, control));
	if (!mesh.embeddedSkins.isEmpty()) {
		mesh.warnings += resolution.warnings;
		if (resolution.fromPackage && !resolution.sourceVirtualPath.isEmpty()) {
			mesh.detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skin palette: %1").arg(resolution.sourceVirtualPath);
		} else if (resolution.palette.generated) {
			mesh.detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skin palette: generated stand-in (no package palette was found).");
		}
	}
	return mesh;
}

QImage resolveModelSkin(const PackageArchiveReader& archive, const ModelMesh& mesh, const QString& paletteId, QString* resolvedPathOut)
{
	if (resolvedPathOut) {
		resolvedPathOut->clear();
	}

	QStringList requested = mesh.skinPaths;
	for (const ModelSurface& surface : mesh.surfaces) {
		for (const QString& skin : surface.skinPaths) {
			if (!requested.contains(skin, Qt::CaseInsensitive)) {
				requested.append(skin);
			}
		}
	}

	if (!requested.isEmpty() && archive.isOpen()) {
		const IdTechPaletteResolution resolution = resolveIdTechPalette(archive, paletteId.isEmpty() ? QStringLiteral("quake") : paletteId);
		for (const QString& skin : requested) {
			const QStringList candidates = skinCandidatePaths(skin);
			for (const QString& candidate : candidates) {
				QString actualPath;
				if (!findArchiveEntryPath(archive, candidate, &actualPath)) {
					continue;
				}
				QByteArray bytes;
				QString error;
				if (!archive.readEntryBytes(actualPath, &bytes, &error)) {
					continue;
				}
				const IdTechImageDecodeResult decoded = decodeIdTechImage(actualPath, bytes, resolution.palette);
				if (!decoded.decoded || decoded.image.isNull()) {
					continue;
				}
				if (resolvedPathOut) {
					*resolvedPathOut = actualPath;
				}
				return decoded.image;
			}
		}
	}

	for (const ModelEmbeddedSkin& skin : mesh.embeddedSkins) {
		if (!skin.image.isNull()) {
			return skin.image;
		}
	}
	return {};
}

QStringList modelMeshSummaryLines(const ModelMesh& mesh)
{
	QStringList lines;
	const QString formatName = mesh.formatName.isEmpty() ? modelMeshFormatDisplayName(mesh.format) : mesh.formatName;
	if (!mesh.error.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "%1: could not be decoded.").arg(formatName);
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Error: %1").arg(mesh.error);
		return lines;
	}
	if (!mesh.geometryAvailable && !mesh.skeleton.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "%1: animation only, %2 joint(s), %3 frame(s).")
			.arg(formatName).arg(mesh.skeleton.joints.size()).arg(mesh.frameCount);
	} else if (!mesh.geometryAvailable) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "%1: header only, %2 frame(s), %3 surface(s).").arg(formatName).arg(mesh.frameCount).arg(mesh.surfaceCount);
	} else {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "%1: %2 surface(s), %3 frame(s), %4 vertices, %5 triangles.")
			.arg(formatName)
			.arg(mesh.surfaceCount)
			.arg(mesh.frameCount)
			.arg(mesh.vertexCount)
			.arg(mesh.triangleCount);
	}
	lines << QCoreApplication::translate("VibeStudioModelMesh", "Format: %1 (version %2)").arg(mesh.formatId, QString::number(mesh.version));
	lines << QCoreApplication::translate("VibeStudioModelMesh", "Frames: %1").arg(mesh.frameCount);
	lines << QCoreApplication::translate("VibeStudioModelMesh", "Surfaces: %1").arg(mesh.surfaceCount);
	lines << QCoreApplication::translate("VibeStudioModelMesh", "Tags: %1").arg(mesh.tagCount);
	lines << QCoreApplication::translate("VibeStudioModelMesh", "Skins: %1").arg(mesh.skinCount);
	if (mesh.geometryAvailable) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Bounds: %1 to %2").arg(formatVector(mesh.mins), formatVector(mesh.maxs));
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Bounding radius: %1").arg(formatCoordinate(mesh.boundingRadius()));
	}
	for (const ModelSurface& surface : mesh.surfaces) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Surface %1 \"%2\": %3 vertices, %4 triangles")
			.arg(surface.index)
			.arg(surface.name)
			.arg(surface.vertexCount)
			.arg(surface.triangles.size());
	}
	for (const ModelAnimation& animation : mesh.animations) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Animation \"%1\": frames %2-%3")
			.arg(animation.name)
			.arg(animation.firstFrame)
			.arg(animation.firstFrame + animation.frameCount - 1);
	}
	for (const ModelEmbeddedSkin& skin : mesh.embeddedSkins) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Embedded skin %1: %2 x %3%4")
			.arg(skin.index)
			.arg(skin.image.width())
			.arg(skin.image.height())
			.arg(skin.groupFrameCount > 1 ? QCoreApplication::translate("VibeStudioModelMesh", " (group of %1)").arg(skin.groupFrameCount) : QString());
	}
	for (const QString& skin : mesh.skinPaths) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Skin path: %1").arg(skin);
	}
	if (!mesh.skeleton.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Joints: %1").arg(mesh.skeleton.joints.size());
		for (const ModelSkeletalClip& clip : mesh.skeleton.clips) {
			lines << QCoreApplication::translate("VibeStudioModelMesh", "Skeletal clip \"%1\": %2 frame(s) at %3 fps%4")
				.arg(clip.name)
				.arg(clip.frames.size())
				.arg(formatCoordinate(clip.framesPerSecond))
				.arg(clip.sourcePath.isEmpty() ? QString() : QCoreApplication::translate("VibeStudioModelMesh", " from %1").arg(clip.sourcePath));
		}
		for (const ModelSkeletalTag& tag : mesh.skeleton.tags) {
			const QString joint = tag.joint >= 0 && tag.joint < mesh.skeleton.joints.size() ? mesh.skeleton.joints.at(tag.joint).name : QString();
			lines << QCoreApplication::translate("VibeStudioModelMesh", "Joint tag \"%1\" on %2").arg(tag.name, joint);
		}
	}
	for (const QString& companion : mesh.companionPaths) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Companion file: %1").arg(companion);
	}
	lines += mesh.detailLines;
	for (const ModelSurface& surface : mesh.surfaces) {
		for (const QString& warning : surface.warnings) {
			lines << QCoreApplication::translate("VibeStudioModelMesh", "Surface %1: %2").arg(surface.index).arg(warning);
		}
	}
	for (const QString& warning : mesh.warnings) {
		lines << QCoreApplication::translate("VibeStudioModelMesh", "Warning: %1").arg(warning);
	}
	return lines;
}

QString modelMeshSummaryText(const ModelMesh& mesh)
{
	return modelMeshSummaryLines(mesh).join(QLatin1Char('\n'));
}

QString exportModelFrameObj(const ModelMesh& mesh, int frameIndex, const QString& materialName, const ModelWorkControl& control, QString* error)
{
	if (error) { error->clear(); }
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	if (!work.check()) { return {}; }
	if (!mesh.geometryAvailable || mesh.surfaces.isEmpty()) {
		return {};
	}
	if (frameIndex < 0 || frameIndex >= mesh.frames.size()) {
		return {};
	}

	QStringList lines;
	lines << QStringLiteral("# Wavefront OBJ exported by VibeStudio");
	if (!mesh.sourcePath.isEmpty()) {
		lines << QStringLiteral("# source %1").arg(sanitizedName(mesh.sourcePath));
	}
	const QString frameName = mesh.frames.at(frameIndex).name;
	lines << QStringLiteral("# frame %1%2").arg(QString::number(frameIndex), frameName.isEmpty() ? QString() : QStringLiteral(" %1").arg(sanitizedName(frameName)));

	int emitted = 0;
	QStringList faceLines;
	for (const ModelSurface& surface : mesh.surfaces) {
		if (!work.step()) { return {}; }
		if (frameIndex >= surface.frames.size()) {
			continue;
		}
		const ModelFrameGeometry& geometry = surface.frames.at(frameIndex);
		const int count = geometry.positions.size();
		if (count <= 0 || surface.triangles.isEmpty()) {
			continue;
		}
		QString group = sanitizedName(surface.name);
		if (group.isEmpty()) {
			group = QStringLiteral("surface%1").arg(surface.index);
		}
		lines << QStringLiteral("g %1").arg(group);
		if (!materialName.isEmpty()) {
			lines << QStringLiteral("usemtl %1").arg(sanitizedName(materialName));
		}
		for (int index = 0; index < count; ++index) {
			if (!work.step()) { return {}; }
			const ModelVec3& position = geometry.positions.at(index);
			lines << QStringLiteral("v %1 %2 %3").arg(formatNumber(position.x), formatNumber(position.y), formatNumber(position.z));
		}
		for (int index = 0; index < count; ++index) {
			if (!work.step()) { return {}; }
			// OBJ measures V from the bottom of the image, idTech from the top.
			const ModelTexCoord coord = index < surface.texCoords.size() ? surface.texCoords.at(index) : ModelTexCoord();
			lines << QStringLiteral("vt %1 %2").arg(formatNumber(coord.u), formatNumber(1.0f - coord.v));
		}
		for (int index = 0; index < count; ++index) {
			if (!work.step()) { return {}; }
			const ModelVec3 normal = index < geometry.normals.size() ? geometry.normals.at(index) : makeVec3(0.0f, 0.0f, 1.0f);
			lines << QStringLiteral("vn %1 %2 %3").arg(formatNumber(normal.x), formatNumber(normal.y), formatNumber(normal.z));
		}
		faceLines.clear();
		for (const ModelTriangle& triangle : surface.triangles) {
			if (!work.step()) { return {}; }
			if (triangle.a < 0 || triangle.a >= count || triangle.b < 0 || triangle.b >= count || triangle.c < 0 || triangle.c >= count) {
				continue;
			}
			// OBJ indices are 1-based and run across the whole file.
			const int a = emitted + triangle.a + 1;
			const int b = emitted + triangle.b + 1;
			const int c = emitted + triangle.c + 1;
			faceLines << QStringLiteral("f %1/%1/%1 %2/%2/%2 %3/%3/%3")
				.arg(QString::number(a), QString::number(b), QString::number(c));
		}
		lines += faceLines;
		emitted += count;
	}
	if (emitted <= 0) {
		return {};
	}
	lines << QString();
	const auto result = lines.join(QLatin1Char('\n'));
	if (!work.check()) { return {}; }
	return result;
}

} // namespace vibestudio
