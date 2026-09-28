#include "core/model_mesh.h"

#include "core/package_archive.h"

#include <QCoreApplication>
#include <QHash>
#include <QRgb>

#include <QtEndian>

#include <algorithm>
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

QString modelText(const char* source)
{
	return QCoreApplication::translate("VibeStudioModelMesh", source);
}

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
// https://github.com/id-Software/Quake-2/blob/master/qcommon/anorms.h
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
QVector<ModelAnimation> inferAnimations(const QVector<ModelFrameInfo>& frames)
{
	QVector<ModelAnimation> animations;
	for (int index = 0; index < frames.size(); ++index) {
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

void finalizeMesh(ModelMesh* mesh)
{
	mesh->frameCount = mesh->frames.size();
	mesh->surfaceCount = mesh->surfaces.size();
	mesh->tagCount = 0;
	QStringList tagNames;
	for (const ModelTag& tag : mesh->tags) {
		if (!tagNames.contains(tag.name)) {
			tagNames.append(tag.name);
		}
	}
	mesh->tagCount = tagNames.size();

	mesh->vertexCount = 0;
	mesh->triangleCount = 0;
	bool haveBounds = false;
	ModelVec3 mins = makeVec3(0.0f, 0.0f, 0.0f);
	ModelVec3 maxs = makeVec3(0.0f, 0.0f, 0.0f);
	for (const ModelSurface& surface : mesh->surfaces) {
		mesh->vertexCount += surface.vertexCount;
		mesh->triangleCount += surface.triangles.size();
		for (const ModelFrameGeometry& geometry : surface.frames) {
			for (const ModelVec3& position : geometry.positions) {
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

QImage decodeIndexedSkin(const QByteArray& bytes, qint64 offset, int width, int height, const IdTechPalette& palette)
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

void decodeQuakeMdl(const QByteArray& bytes, const IdTechPalette& palette, ModelMesh* mesh)
{
	if (!rangeOk(bytes, 0, kMdlHeaderBytes)) {
		mesh->error = modelText("The MDL header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 6) {
		mesh->error = modelText("Unsupported MDL version %1; only IDPO version 6 is decoded.").arg(mesh->version);
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
		mesh->error = modelText("The MDL header has a non-finite scale or translation.");
		return;
	}
	if (skinCount < 0 || skinCount > kMaxSkins
		|| vertexCount <= 0 || vertexCount > kMaxSurfaceVertices
		|| triangleCount <= 0 || triangleCount > kMaxSurfaceTriangles
		|| frameGroupCount <= 0 || frameGroupCount > kMaxFrames) {
		mesh->error = modelText("The MDL header declares counts outside the supported range.");
		return;
	}
	if (skinCount > 0 && (skinWidth <= 0 || skinHeight <= 0 || skinWidth > kMaxSkinDimension || skinHeight > kMaxSkinDimension)) {
		mesh->error = modelText("The MDL skin size is outside the supported range.");
		return;
	}
	const qint64 skinPixels = static_cast<qint64>(std::max(0, skinWidth)) * static_cast<qint64>(std::max(0, skinHeight));
	if (skinCount > 0 && skinPixels > kMaxSkinPixels) {
		mesh->error = modelText("The MDL skin size is outside the supported range.");
		return;
	}
	if (static_cast<qint64>(vertexCount) * static_cast<qint64>(frameGroupCount) > kMaxVertexSlots) {
		mesh->error = modelText("The MDL declares more frame vertices than can be decoded.");
		return;
	}

	mesh->skinCount = skinCount;
	mesh->detailLines << modelText("Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
	mesh->detailLines << modelText("Scale: %1").arg(formatVector(scale));
	mesh->detailLines << modelText("Translation: %1").arg(formatVector(translate));
	mesh->detailLines << modelText("Eye position: %1").arg(formatVector(eyePosition));
	mesh->detailLines << modelText("Declared bounding radius: %1").arg(formatCoordinate(declaredRadius));
	mesh->detailLines << modelText("Declared size: %1").arg(formatCoordinate(declaredSize));
	mesh->detailLines << modelText("Sync type: %1").arg(syncType);
	mesh->detailLines << modelText("Flags: 0x%1").arg(QString::number(static_cast<uint>(flags), 16));

	qint64 offset = kMdlHeaderBytes;

	// Skins. A group skin stores a count, one interval per member, then the
	// member pixel blocks back to back.
	for (int index = 0; index < skinCount; ++index) {
		if (!rangeOk(bytes, offset, 4)) {
			mesh->error = modelText("The MDL skin data is truncated.");
			return;
		}
		const int group = readI32(bytes, offset);
		offset += 4;
		int groupFrames = 1;
		if (group != 0) {
			if (!rangeOk(bytes, offset, 4)) {
				mesh->error = modelText("The MDL skin data is truncated.");
				return;
			}
			groupFrames = readI32(bytes, offset);
			offset += 4;
			if (groupFrames <= 0 || groupFrames > kMaxSkinGroupFrames) {
				mesh->error = modelText("An MDL skin group declares an unsupported frame count.");
				return;
			}
			if (!rangeOk(bytes, offset, static_cast<qint64>(groupFrames) * 4)) {
				mesh->error = modelText("The MDL skin data is truncated.");
				return;
			}
			offset += static_cast<qint64>(groupFrames) * 4;
		}
		const qint64 block = skinPixels * static_cast<qint64>(groupFrames);
		if (!rangeOk(bytes, offset, block)) {
			mesh->error = modelText("The MDL skin data is truncated.");
			return;
		}
		ModelEmbeddedSkin skin;
		skin.index = index;
		skin.name = QStringLiteral("skin%1").arg(index);
		skin.groupFrameCount = groupFrames;
		skin.image = decodeIndexedSkin(bytes, offset, skinWidth, skinHeight, palette);
		if (skin.image.isNull()) {
			mesh->warnings << modelText("Embedded skin %1 could not be decoded.").arg(index);
		}
		mesh->embeddedSkins.append(skin);
		offset += block;
	}

	// Texture coordinates.
	if (!rangeOk(bytes, offset, static_cast<qint64>(vertexCount) * kMdlStVertBytes)) {
		mesh->error = modelText("The MDL texture coordinates are truncated.");
		return;
	}
	QVector<int> seamFlags(vertexCount, 0);
	QVector<int> sCoords(vertexCount, 0);
	QVector<int> tCoords(vertexCount, 0);
	for (int index = 0; index < vertexCount; ++index) {
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
		mesh->error = modelText("The MDL triangle list is truncated.");
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
		baseForCombined.append(index);
		backForCombined.append(0);
	}
	QHash<int, int> backCopyForBase;

	int skippedTriangles = 0;
	for (int index = 0; index < triangleCount; ++index) {
		const qint64 base = offset + (static_cast<qint64>(index) * kMdlTriangleBytes);
		const bool facesFront = readI32(bytes, base) != 0;
		int corners[3] = {0, 0, 0};
		bool valid = true;
		for (int corner = 0; corner < 3; ++corner) {
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
		surface.warnings << modelText("%1 triangle(s) referenced vertices outside the model and were dropped.").arg(skippedTriangles);
	}
	offset += static_cast<qint64>(triangleCount) * kMdlTriangleBytes;

	const int combinedCount = baseForCombined.size();
	if (static_cast<qint64>(combinedCount) * static_cast<qint64>(frameGroupCount) > kMaxVertexSlots) {
		mesh->error = modelText("The MDL declares more frame vertices than can be decoded.");
		return;
	}
	surface.vertexCount = combinedCount;
	surface.texCoords.reserve(combinedCount);
	const float skinWidthF = skinWidth > 0 ? static_cast<float>(skinWidth) : 1.0f;
	const float skinHeightF = skinHeight > 0 ? static_cast<float>(skinHeight) : 1.0f;
	for (int index = 0; index < combinedCount; ++index) {
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
		if (!rangeOk(bytes, offset, 4)) {
			mesh->error = modelText("The MDL frame data is truncated.");
			return;
		}
		const int type = readI32(bytes, offset);
		offset += 4;
		int memberCount = 1;
		if (type != 0) {
			if (!rangeOk(bytes, offset, 4)) {
				mesh->error = modelText("The MDL frame data is truncated.");
				return;
			}
			memberCount = readI32(bytes, offset);
			offset += 4;
			if (memberCount <= 0 || memberCount > kMaxFrames || poses.size() + memberCount > kMaxFrames) {
				mesh->error = modelText("An MDL frame group declares an unsupported frame count.");
				return;
			}
			// Group bounding box followed by one interval per member.
			if (!rangeOk(bytes, offset, 8 + (static_cast<qint64>(memberCount) * 4))) {
				mesh->error = modelText("The MDL frame data is truncated.");
				return;
			}
			offset += 8 + (static_cast<qint64>(memberCount) * 4);
		}
		for (int member = 0; member < memberCount; ++member) {
			const qint64 poseBytes = 8 + kMdlFrameNameBytes + (static_cast<qint64>(vertexCount) * kMdlVertexBytes);
			if (!rangeOk(bytes, offset, poseBytes)) {
				mesh->error = modelText("The MDL frame data is truncated.");
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
				mesh->error = modelText("The MDL declares more frames than can be decoded.");
				return;
			}
		}
	}

	surface.frames.reserve(poses.size());
	for (int poseIndex = 0; poseIndex < poses.size(); ++poseIndex) {
		const MdlPose& pose = poses.at(poseIndex);
		ModelFrameGeometry geometry;
		geometry.positions.resize(combinedCount);
		geometry.normals.resize(combinedCount);
		for (int index = 0; index < combinedCount; ++index) {
			const qint64 vertex = pose.vertexOffset + (static_cast<qint64>(baseForCombined.at(index)) * kMdlVertexBytes);
			// Packed byte positions are decompressed with the header scale and
			// translation: position = scale * raw + translate.
			geometry.positions[index] = makeVec3((scale.x * static_cast<float>(readU8(bytes, vertex))) + translate.x,
				(scale.y * static_cast<float>(readU8(bytes, vertex + 1))) + translate.y,
				(scale.z * static_cast<float>(readU8(bytes, vertex + 2))) + translate.z);
			geometry.normals[index] = aliasNormal(static_cast<int>(readU8(bytes, vertex + 3)));
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

	mesh->surfaces.append(surface);
	mesh->animations = inferAnimations(mesh->frames);
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

void decodeQuake2Md2(const QByteArray& bytes, ModelMesh* mesh)
{
	if (!rangeOk(bytes, 0, kMd2HeaderBytes)) {
		mesh->error = modelText("The MD2 header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 8) {
		mesh->error = modelText("Unsupported MD2 version %1; only IDP2 version 8 is decoded.").arg(mesh->version);
		return;
	}

	const int skinWidth = readI32(bytes, 8);
	const int skinHeight = readI32(bytes, 12);
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
		mesh->error = modelText("The MD2 header declares counts outside the supported range.");
		return;
	}
	if (static_cast<qint64>(positionCount) * static_cast<qint64>(frameCount) > kMaxVertexSlots) {
		mesh->error = modelText("The MD2 declares more frame vertices than can be decoded.");
		return;
	}
	const qint64 minimumFrameBytes = kMd2FrameHeaderBytes + (static_cast<qint64>(positionCount) * kMd2VertexBytes);
	if (frameSize < minimumFrameBytes || frameSize > (minimumFrameBytes + 4096)) {
		mesh->error = modelText("The MD2 frame size does not match the declared vertex count.");
		return;
	}
	if (!rangeOk(bytes, skinOffset, static_cast<qint64>(skinCount) * kMd2SkinNameBytes)
		|| !rangeOk(bytes, stOffset, static_cast<qint64>(stCount) * kMd2StBytes)
		|| !rangeOk(bytes, triangleOffset, static_cast<qint64>(triangleCount) * kMd2TriangleBytes)
		|| !rangeOk(bytes, frameOffset, static_cast<qint64>(frameCount) * static_cast<qint64>(frameSize))) {
		mesh->error = modelText("An MD2 data block lies outside the file.");
		return;
	}

	mesh->skinCount = skinCount;
	mesh->detailLines << modelText("Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
	mesh->detailLines << modelText("Positions: %1").arg(positionCount);
	mesh->detailLines << modelText("Texture coordinates: %1").arg(stCount);
	mesh->detailLines << modelText("GL commands: %1").arg(glCommandCount);

	for (int index = 0; index < skinCount; ++index) {
		const QString skin = readFixedName(bytes, skinOffset + (static_cast<qint64>(index) * kMd2SkinNameBytes), kMd2SkinNameBytes);
		if (!skin.isEmpty() && !mesh->skinPaths.contains(skin)) {
			mesh->skinPaths.append(skin);
		}
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
		surface.warnings << modelText("The MD2 skin size is zero; texture coordinates are left unscaled.");
	}

	// MD2 indexes positions and texture coordinates separately, so a combined
	// vertex list has to be built from the (position, st) pairs each triangle
	// actually uses.
	QVector<int> positionForCombined;
	QHash<qint64, int> combinedForPair;
	int skippedTriangles = 0;
	for (int index = 0; index < triangleCount; ++index) {
		const qint64 base = triangleOffset + (static_cast<qint64>(index) * kMd2TriangleBytes);
		int corners[3] = {0, 0, 0};
		bool valid = true;
		for (int corner = 0; corner < 3; ++corner) {
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
				coord.u = static_cast<float>(readI16(bytes, stOffset + (static_cast<qint64>(stIndex) * kMd2StBytes))) / skinWidthF;
				coord.v = static_cast<float>(readI16(bytes, stOffset + (static_cast<qint64>(stIndex) * kMd2StBytes) + 2)) / skinHeightF;
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
		surface.warnings << modelText("%1 triangle(s) referenced out-of-range indices and were dropped.").arg(skippedTriangles);
	}

	const int combinedCount = positionForCombined.size();
	if (combinedCount <= 0) {
		mesh->error = modelText("The MD2 contains no usable triangles.");
		return;
	}
	if (static_cast<qint64>(combinedCount) * static_cast<qint64>(frameCount) > kMaxVertexSlots) {
		mesh->error = modelText("The MD2 declares more frame vertices than can be decoded.");
		return;
	}
	surface.vertexCount = combinedCount;

	surface.frames.reserve(frameCount);
	for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
		const qint64 base = frameOffset + (static_cast<qint64>(frameIndex) * static_cast<qint64>(frameSize));
		ModelVec3 scale;
		ModelVec3 translate;
		readVec3(bytes, base, &scale);
		readVec3(bytes, base + 12, &translate);
		if (!isFinite(scale) || !isFinite(translate)) {
			mesh->error = modelText("An MD2 frame has a non-finite scale or translation.");
			return;
		}
		const QString name = readFixedName(bytes, base + 24, kMd2FrameNameBytes);
		const qint64 vertexBase = base + kMd2FrameHeaderBytes;

		ModelFrameGeometry geometry;
		geometry.positions.resize(combinedCount);
		geometry.normals.resize(combinedCount);
		for (int index = 0; index < combinedCount; ++index) {
			const qint64 vertex = vertexBase + (static_cast<qint64>(positionForCombined.at(index)) * kMd2VertexBytes);
			geometry.positions[index] = makeVec3((scale.x * static_cast<float>(readU8(bytes, vertex))) + translate.x,
				(scale.y * static_cast<float>(readU8(bytes, vertex + 1))) + translate.y,
				(scale.z * static_cast<float>(readU8(bytes, vertex + 2))) + translate.z);
			geometry.normals[index] = aliasNormal(static_cast<int>(readU8(bytes, vertex + 3)));
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

	mesh->surfaces.append(surface);
	mesh->animations = inferAnimations(mesh->frames);
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

void decodeQuake3Md3(const QByteArray& bytes, ModelMesh* mesh)
{
	if (!rangeOk(bytes, 0, kMd3HeaderBytes)) {
		mesh->error = modelText("The MD3 header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 15) {
		mesh->error = modelText("Unsupported MD3 version %1; only IDP3 version 15 is decoded.").arg(mesh->version);
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
		mesh->error = modelText("The MD3 header declares counts outside the supported range.");
		return;
	}
	if (static_cast<qint64>(tagCount) * static_cast<qint64>(frameCount) > static_cast<qint64>(kMaxTags) * 16LL) {
		mesh->error = modelText("The MD3 declares more tags than can be decoded.");
		return;
	}
	if (!rangeOk(bytes, frameOffset, static_cast<qint64>(frameCount) * kMd3FrameBytes)) {
		mesh->error = modelText("The MD3 frame block lies outside the file.");
		return;
	}
	if (tagCount > 0 && !rangeOk(bytes, tagOffset, static_cast<qint64>(tagCount) * static_cast<qint64>(frameCount) * kMd3TagBytes)) {
		mesh->error = modelText("The MD3 tag block lies outside the file.");
		return;
	}

	if (!internalName.isEmpty()) {
		mesh->detailLines << modelText("Internal name: %1").arg(internalName);
	}
	mesh->detailLines << modelText("Flags: 0x%1").arg(QString::number(static_cast<uint>(flags), 16));
	mesh->detailLines << modelText("Declared skins: %1").arg(skinCount);
	if (endOffset > 0 && endOffset != bytes.size()) {
		mesh->warnings << modelText("The MD3 end offset (%1) does not match the file size (%2).").arg(endOffset).arg(bytes.size());
	}

	for (int index = 0; index < frameCount; ++index) {
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
		for (int index = 0; index < tagCount; ++index) {
			const qint64 base = tagOffset + ((static_cast<qint64>(frameIndex) * static_cast<qint64>(tagCount) + static_cast<qint64>(index)) * kMd3TagBytes);
			ModelTag tag;
			tag.name = readFixedName(bytes, base, kMd3NameBytes);
			tag.frameIndex = frameIndex;
			readVec3(bytes, base + kMd3NameBytes, &tag.origin);
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
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
	for (; walked < surfaceCount; ++walked) {
		if (!rangeOk(bytes, offset, kMd3SurfaceHeaderBytes)) {
			mesh->warnings << modelText("Surface %1 lies outside the file; the surface chain stops here.").arg(walked);
			break;
		}
		if (bytes.mid(static_cast<qsizetype>(offset), 4) != QByteArrayLiteral("IDP3")) {
			mesh->warnings << modelText("Surface %1 has an unexpected identifier; the surface chain stops here.").arg(walked);
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
			mesh->warnings << modelText("Surface %1 declares an end offset outside the file; the surface chain stops here.").arg(walked);
			break;
		}
		if (surfaceFrameCount <= 0 || surfaceFrameCount > kMaxFrames
			|| shaderCount < 0 || shaderCount > kMaxSkins
			|| vertexCount <= 0 || vertexCount > kMaxSurfaceVertices
			|| triangleCount < 0 || triangleCount > kMaxSurfaceTriangles) {
			mesh->warnings << modelText("Surface %1 declares counts outside the supported range and was skipped.").arg(walked);
			offset += surfaceEnd;
			continue;
		}
		const int usableFrames = std::min(surfaceFrameCount, frameCount);
		if (static_cast<qint64>(vertexCount) * static_cast<qint64>(usableFrames) > kMaxVertexSlots) {
			mesh->warnings << modelText("Surface %1 declares more frame vertices than can be decoded and was skipped.").arg(walked);
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
			mesh->warnings << modelText("Surface %1 has a data block outside the surface record and was skipped.").arg(walked);
			offset += surfaceEnd;
			continue;
		}

		ModelSurface surface;
		surface.index = mesh->surfaces.size();
		surface.name = name.isEmpty() ? QStringLiteral("surface%1").arg(surface.index) : name;
		surface.vertexCount = vertexCount;
		if (surfaceFlags != 0) {
			surface.warnings << modelText("Surface flags: 0x%1").arg(QString::number(static_cast<uint>(surfaceFlags), 16));
		}
		if (surfaceFrameCount != frameCount) {
			surface.warnings << modelText("The surface declares %1 frame(s) while the model declares %2.").arg(surfaceFrameCount).arg(frameCount);
		}

		for (int index = 0; index < shaderCount; ++index) {
			const QString shader = readFixedName(bytes, offset + shaderOffset + (static_cast<qint64>(index) * kMd3ShaderBytes), kMd3NameBytes);
			if (shader.isEmpty()) {
				continue;
			}
			surface.skinPaths.append(shader);
			if (!mesh->skinPaths.contains(shader)) {
				mesh->skinPaths.append(shader);
			}
		}

		surface.texCoords.reserve(vertexCount);
		for (int index = 0; index < vertexCount; ++index) {
			const qint64 base = offset + stOffset + (static_cast<qint64>(index) * kMd3StBytes);
			ModelTexCoord coord;
			coord.u = readF32(bytes, base);
			coord.v = readF32(bytes, base + 4);
			surface.texCoords.append(coord);
		}

		int skippedTriangles = 0;
		surface.triangles.reserve(triangleCount);
		for (int index = 0; index < triangleCount; ++index) {
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
			surface.warnings << modelText("%1 triangle(s) referenced out-of-range indices and were dropped.").arg(skippedTriangles);
		}

		surface.frames.reserve(frameCount);
		for (int frameIndex = 0; frameIndex < usableFrames; ++frameIndex) {
			ModelFrameGeometry geometry;
			geometry.positions.resize(vertexCount);
			geometry.normals.resize(vertexCount);
			for (int index = 0; index < vertexCount; ++index) {
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
			// Keep the surface parallel with the model frame list.
			surface.frames.append(surface.frames.last());
		}

		mesh->surfaces.append(surface);
		offset += surfaceEnd;
	}
	if (walked < surfaceCount) {
		mesh->warnings << modelText("Only %1 of %2 surface(s) could be read.").arg(walked).arg(surfaceCount);
	}

	mesh->skinCount = std::max(skinCount, static_cast<int>(mesh->skinPaths.size()));
	mesh->animations = inferAnimations(mesh->frames);
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
	if (!mesh->geometryAvailable) {
		mesh->warnings << modelText("No MD3 surface could be decoded.");
	}
}

// ---------------------------------------------------------------------------
// Header-only formats
// ---------------------------------------------------------------------------

void decodeMdcHeader(const QByteArray& bytes, ModelMesh* mesh)
{
	// mdcHeader_t: ident, version, name[64], flags, numFrames, numTags,
	// numSurfaces, numSkins, then the block offsets.
	if (!rangeOk(bytes, 0, 108)) {
		mesh->error = modelText("The MDC header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	const QString internalName = readFixedName(bytes, 8, 64);
	mesh->frameCount = std::max(0, readI32(bytes, 76));
	mesh->tagCount = std::max(0, readI32(bytes, 80));
	mesh->surfaceCount = std::max(0, readI32(bytes, 84));
	mesh->skinCount = std::max(0, readI32(bytes, 88));
	if (!internalName.isEmpty()) {
		mesh->detailLines << modelText("Internal name: %1").arg(internalName);
	}
	mesh->detailLines << modelText("Flags: 0x%1").arg(QString::number(static_cast<uint>(readI32(bytes, 72)), 16));
	mesh->detailLines << modelText("Frames: %1").arg(mesh->frameCount);
	mesh->detailLines << modelText("Tags: %1").arg(mesh->tagCount);
	mesh->detailLines << modelText("Surfaces: %1").arg(mesh->surfaceCount);
	mesh->detailLines << modelText("Skins: %1").arg(mesh->skinCount);
	mesh->warnings << modelText("Geometry decoding is not implemented for MDC; only the header was read.");
}

void decodeMdrHeader(const QByteArray& bytes, ModelMesh* mesh)
{
	// mdrHeader_t: ident, version, name[64], numFrames, numBones, ofsFrames,
	// numLODs, ofsLODs, numTags, ofsTags, ofsEnd.
	if (!rangeOk(bytes, 0, 104)) {
		mesh->error = modelText("The MDR header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	const QString internalName = readFixedName(bytes, 8, 64);
	mesh->frameCount = std::max(0, readI32(bytes, 72));
	const int boneCount = std::max(0, readI32(bytes, 76));
	const int lodCount = std::max(0, readI32(bytes, 84));
	mesh->tagCount = std::max(0, readI32(bytes, 92));
	if (!internalName.isEmpty()) {
		mesh->detailLines << modelText("Internal name: %1").arg(internalName);
	}
	mesh->detailLines << modelText("Frames: %1").arg(mesh->frameCount);
	mesh->detailLines << modelText("Bones: %1").arg(boneCount);
	mesh->detailLines << modelText("Levels of detail: %1").arg(lodCount);
	mesh->detailLines << modelText("Tags: %1").arg(mesh->tagCount);
	mesh->warnings << modelText("Geometry decoding is not implemented for MDR; only the header was read.");
}

void decodeIqmHeader(const QByteArray& bytes, ModelMesh* mesh)
{
	// iqmheader: magic[16], version, filesize, flags, then paired count/offset
	// fields. See http://sauerbraten.org/iqm/.
	if (!rangeOk(bytes, 0, 124)) {
		mesh->error = modelText("The IQM header is truncated.");
		return;
	}
	mesh->version = static_cast<int>(readU32(bytes, 16));
	const qint64 fileSize = static_cast<qint64>(readU32(bytes, 20));
	mesh->surfaceCount = static_cast<int>(std::min<quint32>(readU32(bytes, 36), static_cast<quint32>(std::numeric_limits<int>::max())));
	mesh->vertexCount = static_cast<int>(std::min<quint32>(readU32(bytes, 48), static_cast<quint32>(std::numeric_limits<int>::max())));
	mesh->triangleCount = static_cast<int>(std::min<quint32>(readU32(bytes, 56), static_cast<quint32>(std::numeric_limits<int>::max())));
	const int jointCount = static_cast<int>(std::min<quint32>(readU32(bytes, 68), static_cast<quint32>(std::numeric_limits<int>::max())));
	const int animationCount = static_cast<int>(std::min<quint32>(readU32(bytes, 84), static_cast<quint32>(std::numeric_limits<int>::max())));
	mesh->frameCount = static_cast<int>(std::min<quint32>(readU32(bytes, 92), static_cast<quint32>(std::numeric_limits<int>::max())));
	mesh->detailLines << modelText("Declared file size: %1 byte(s)").arg(fileSize);
	mesh->detailLines << modelText("Meshes: %1").arg(mesh->surfaceCount);
	mesh->detailLines << modelText("Vertices: %1").arg(mesh->vertexCount);
	mesh->detailLines << modelText("Triangles: %1").arg(mesh->triangleCount);
	mesh->detailLines << modelText("Joints: %1").arg(jointCount);
	mesh->detailLines << modelText("Animations: %1").arg(animationCount);
	mesh->detailLines << modelText("Frames: %1").arg(mesh->frameCount);
	if (fileSize > 0 && fileSize != bytes.size()) {
		mesh->warnings << modelText("The IQM file size field (%1) does not match the file size (%2).").arg(fileSize).arg(bytes.size());
	}
	mesh->warnings << modelText("Geometry decoding is not implemented for IQM; only the header was read.");
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

QString modelMeshFormatId(ModelMeshFormat format)
{
	switch (format) {
	case ModelMeshFormat::QuakeMdl:
		return QStringLiteral("mdl");
	case ModelMeshFormat::Quake2Md2:
		return QStringLiteral("md2");
	case ModelMeshFormat::Quake3Md3:
		return QStringLiteral("md3");
	case ModelMeshFormat::Mdc:
		return QStringLiteral("mdc");
	case ModelMeshFormat::Mdr:
		return QStringLiteral("mdr");
	case ModelMeshFormat::Iqm:
		return QStringLiteral("iqm");
	case ModelMeshFormat::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString modelMeshFormatDisplayName(ModelMeshFormat format)
{
	switch (format) {
	case ModelMeshFormat::QuakeMdl:
		return QStringLiteral("Quake MDL");
	case ModelMeshFormat::Quake2Md2:
		return QStringLiteral("Quake II MD2");
	case ModelMeshFormat::Quake3Md3:
		return QStringLiteral("Quake III MD3");
	case ModelMeshFormat::Mdc:
		return QStringLiteral("MDC");
	case ModelMeshFormat::Mdr:
		return QStringLiteral("MDR");
	case ModelMeshFormat::Iqm:
		return QStringLiteral("Inter-Quake Model");
	case ModelMeshFormat::Unknown:
		break;
	}
	return modelText("Unknown model");
}

ModelMeshFormat detectModelMeshFormat(const QString& virtualPath, const QByteArray& bytes)
{
	if (bytes.size() >= 16 && bytes.startsWith(QByteArrayLiteral("INTERQUAKEMODEL"))) {
		return ModelMeshFormat::Iqm;
	}
	if (bytes.size() >= 4) {
		const QByteArray magic = bytes.left(4);
		if (magic == QByteArrayLiteral("IDPO")) {
			return ModelMeshFormat::QuakeMdl;
		}
		if (magic == QByteArrayLiteral("IDP2")) {
			return ModelMeshFormat::Quake2Md2;
		}
		if (magic == QByteArrayLiteral("IDP3")) {
			return ModelMeshFormat::Quake3Md3;
		}
		if (magic == QByteArrayLiteral("IDPC")) {
			return ModelMeshFormat::Mdc;
		}
		if (magic == QByteArrayLiteral("RDM5")) {
			return ModelMeshFormat::Mdr;
		}
	}
	// Fall back to the extension so a damaged file still reports which decoder
	// was expected instead of a bare "unknown".
	const QString suffix = pathSuffix(virtualPath);
	if (suffix == QStringLiteral("mdl")) {
		return ModelMeshFormat::QuakeMdl;
	}
	if (suffix == QStringLiteral("md2")) {
		return ModelMeshFormat::Quake2Md2;
	}
	if (suffix == QStringLiteral("md3")) {
		return ModelMeshFormat::Quake3Md3;
	}
	if (suffix == QStringLiteral("mdc")) {
		return ModelMeshFormat::Mdc;
	}
	if (suffix == QStringLiteral("mdr")) {
		return ModelMeshFormat::Mdr;
	}
	if (suffix == QStringLiteral("iqm")) {
		return ModelMeshFormat::Iqm;
	}
	return ModelMeshFormat::Unknown;
}

ModelMesh decodeModelMesh(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette* palette)
{
	ModelMesh mesh;
	mesh.sourcePath = virtualPath;
	mesh.format = detectModelMeshFormat(virtualPath, bytes);
	mesh.formatId = modelMeshFormatId(mesh.format);
	mesh.formatName = modelMeshFormatDisplayName(mesh.format);
	if (mesh.format == ModelMeshFormat::Unknown) {
		mesh.error = modelText("The file is not a recognised idTech model.");
		return mesh;
	}
	if (bytes.isEmpty()) {
		mesh.error = modelText("The model file is empty.");
		return mesh;
	}

	IdTechPalette effectivePalette;
	if (palette && palette->isValid()) {
		effectivePalette = *palette;
	} else {
		effectivePalette = generatedIdTechPalette(QStringLiteral("quake"));
	}

	switch (mesh.format) {
	case ModelMeshFormat::QuakeMdl:
		decodeQuakeMdl(bytes, effectivePalette, &mesh);
		break;
	case ModelMeshFormat::Quake2Md2:
		decodeQuake2Md2(bytes, &mesh);
		break;
	case ModelMeshFormat::Quake3Md3:
		decodeQuake3Md3(bytes, &mesh);
		break;
	case ModelMeshFormat::Mdc:
		decodeMdcHeader(bytes, &mesh);
		break;
	case ModelMeshFormat::Mdr:
		decodeMdrHeader(bytes, &mesh);
		break;
	case ModelMeshFormat::Iqm:
		decodeIqmHeader(bytes, &mesh);
		break;
	case ModelMeshFormat::Unknown:
		break;
	}

	if (!mesh.error.isEmpty()) {
		mesh.geometryAvailable = false;
		mesh.surfaces.clear();
		return mesh;
	}
	if (mesh.geometryAvailable || !mesh.frames.isEmpty()) {
		// Frames still count even when no surface survived the walk.
		finalizeMesh(&mesh);
	}
	return mesh;
}

ModelMesh decodeModelMeshFromArchive(const PackageArchiveReader& archive, const QString& virtualPath, const QString& paletteId)
{
	// Resolve a real package palette first so MDL's embedded indexed skins get
	// the colours the package actually ships.
	const IdTechPaletteResolution resolution = resolveIdTechPalette(archive, paletteId.isEmpty() ? QStringLiteral("quake") : paletteId);

	QByteArray bytes;
	QString error;
	if (!archive.readEntryBytes(virtualPath, &bytes, &error)) {
		ModelMesh mesh;
		mesh.sourcePath = virtualPath;
		mesh.format = ModelMeshFormat::Unknown;
		mesh.formatId = modelMeshFormatId(mesh.format);
		mesh.formatName = modelMeshFormatDisplayName(mesh.format);
		mesh.error = error.isEmpty() ? modelText("Unable to read the package entry.") : error;
		return mesh;
	}

	ModelMesh mesh = decodeModelMesh(virtualPath, bytes, &resolution.palette);
	if (!mesh.embeddedSkins.isEmpty()) {
		mesh.warnings += resolution.warnings;
		if (resolution.fromPackage && !resolution.sourceVirtualPath.isEmpty()) {
			mesh.detailLines << modelText("Skin palette: %1").arg(resolution.sourceVirtualPath);
		} else if (resolution.palette.generated) {
			mesh.detailLines << modelText("Skin palette: generated stand-in (no package palette was found).");
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
		lines << modelText("%1: could not be decoded.").arg(formatName);
		lines << modelText("Error: %1").arg(mesh.error);
		return lines;
	}
	if (!mesh.geometryAvailable) {
		lines << modelText("%1: header only, %2 frame(s), %3 surface(s).").arg(formatName).arg(mesh.frameCount).arg(mesh.surfaceCount);
	} else {
		lines << modelText("%1: %2 surface(s), %3 frame(s), %4 vertices, %5 triangles.")
			.arg(formatName)
			.arg(mesh.surfaceCount)
			.arg(mesh.frameCount)
			.arg(mesh.vertexCount)
			.arg(mesh.triangleCount);
	}
	lines << modelText("Format: %1 (version %2)").arg(mesh.formatId, QString::number(mesh.version));
	lines << modelText("Frames: %1").arg(mesh.frameCount);
	lines << modelText("Surfaces: %1").arg(mesh.surfaceCount);
	lines << modelText("Tags: %1").arg(mesh.tagCount);
	lines << modelText("Skins: %1").arg(mesh.skinCount);
	if (mesh.geometryAvailable) {
		lines << modelText("Bounds: %1 to %2").arg(formatVector(mesh.mins), formatVector(mesh.maxs));
		lines << modelText("Bounding radius: %1").arg(formatCoordinate(mesh.boundingRadius()));
	}
	for (const ModelSurface& surface : mesh.surfaces) {
		lines << modelText("Surface %1 \"%2\": %3 vertices, %4 triangles")
			.arg(surface.index)
			.arg(surface.name)
			.arg(surface.vertexCount)
			.arg(surface.triangles.size());
	}
	for (const ModelAnimation& animation : mesh.animations) {
		lines << modelText("Animation \"%1\": frames %2-%3")
			.arg(animation.name)
			.arg(animation.firstFrame)
			.arg(animation.firstFrame + animation.frameCount - 1);
	}
	for (const ModelEmbeddedSkin& skin : mesh.embeddedSkins) {
		lines << modelText("Embedded skin %1: %2 x %3%4")
			.arg(skin.index)
			.arg(skin.image.width())
			.arg(skin.image.height())
			.arg(skin.groupFrameCount > 1 ? modelText(" (group of %1)").arg(skin.groupFrameCount) : QString());
	}
	for (const QString& skin : mesh.skinPaths) {
		lines << modelText("Skin path: %1").arg(skin);
	}
	lines += mesh.detailLines;
	for (const ModelSurface& surface : mesh.surfaces) {
		for (const QString& warning : surface.warnings) {
			lines << modelText("Surface %1: %2").arg(surface.index).arg(warning);
		}
	}
	for (const QString& warning : mesh.warnings) {
		lines << modelText("Warning: %1").arg(warning);
	}
	return lines;
}

QString modelMeshSummaryText(const ModelMesh& mesh)
{
	return modelMeshSummaryLines(mesh).join(QLatin1Char('\n'));
}

QString exportModelFrameObj(const ModelMesh& mesh, int frameIndex, const QString& materialName)
{
	if (!mesh.geometryAvailable || mesh.surfaces.isEmpty()) {
		return {};
	}
	if (frameIndex < 0 || frameIndex >= mesh.frames.size()) {
		return {};
	}

	QStringList lines;
	lines << QStringLiteral("# Wavefront OBJ exported by VibeStudio");
	if (!mesh.sourcePath.isEmpty()) {
		lines << QStringLiteral("# source %1").arg(mesh.sourcePath);
	}
	const QString frameName = mesh.frames.at(frameIndex).name;
	lines << QStringLiteral("# frame %1%2").arg(QString::number(frameIndex), frameName.isEmpty() ? QString() : QStringLiteral(" %1").arg(sanitizedName(frameName)));

	int emitted = 0;
	QStringList faceLines;
	for (const ModelSurface& surface : mesh.surfaces) {
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
			const ModelVec3& position = geometry.positions.at(index);
			lines << QStringLiteral("v %1 %2 %3").arg(formatNumber(position.x), formatNumber(position.y), formatNumber(position.z));
		}
		for (int index = 0; index < count; ++index) {
			// OBJ measures V from the bottom of the image, idTech from the top.
			const ModelTexCoord coord = index < surface.texCoords.size() ? surface.texCoords.at(index) : ModelTexCoord();
			lines << QStringLiteral("vt %1 %2").arg(formatNumber(coord.u), formatNumber(1.0f - coord.v));
		}
		for (int index = 0; index < count; ++index) {
			const ModelVec3 normal = index < geometry.normals.size() ? geometry.normals.at(index) : makeVec3(0.0f, 0.0f, 1.0f);
			lines << QStringLiteral("vn %1 %2 %3").arg(formatNumber(normal.x), formatNumber(normal.y), formatNumber(normal.z));
		}
		faceLines.clear();
		for (const ModelTriangle& triangle : surface.triangles) {
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
	return lines.join(QLatin1Char('\n'));
}

} // namespace vibestudio
