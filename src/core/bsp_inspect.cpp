#include "core/bsp_inspect.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <limits>

namespace vibestudio {

namespace {

// ---------------------------------------------------------------------------
// Format references
//
// Quake BSP29 lump order and record layouts: the Quake Specifications, chapter
// 4 "BSP files" (Olivier Montanuy / Quake Standards Group,
// https://www.gamers.org/dEngine/quake/spec/quake-spec34/qkspec_4.htm) and the
// released Quake tools sources (`bspfile.h`).
//
// BSP2 ("BSP2") and BSP2-RMQ ("2PSB") widened node/leaf/clipnode/edge/
// marksurface records: the BSP2 format notes shipped with ericw-tools
// (https://ericwa.github.io/ericw-tools/) and the QuakeSpasm `bspfile.h`
// definitions of `dnode2_t`, `dnode2rmq_t`, `dface2_t`, `dclipnode2_t`.
//
// Quake II IBSP v38: the released Quake II source `qfiles.h`
// (https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h).
// Qbism "QBSP" extended Quake II: the q2tools-220 / qbism `qfiles.h` widened
// `dqnode_t`, `dqleaf_t`, `dqface_t`, `dqbrushside_t`, `dqedge_t` records.
//
// Quake III IBSP v46: the released Quake III Arena source `qfiles.h`
// (https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h).
// Raven RBSP v1: the q3map2 sources imported under external/compilers
// (`game_*.h` / `bspfile_rbsp.c`, `rbspDrawSurface_t`, `rbspDrawVert_t`).
//
// Portal (.prt) and leak point (.pts/.lin) text files: qbsp/vis output as
// documented by ericw-tools and q3map2.
//
// Everything below is implemented from those specifications; no code is copied
// from those projects.
// ---------------------------------------------------------------------------

constexpr qsizetype kLumpEntryBytes = 8;
constexpr qsizetype kQuakeLumpCount = 15;
constexpr qsizetype kQuake2LumpCount = 19;
constexpr qsizetype kQuake3LumpCount = 17;
constexpr qsizetype kRavenLumpCount = 18;
constexpr int kQuake3LightmapBytes = 128 * 128 * 3;
constexpr int kMaxTextureEntries = 65536;
constexpr int kMaxEntityEntries = 262144;

// Classic engine limits worth calling out; see the Quake Specifications and
// the vanilla `bspfile.h` MAX_MAP_* values.
constexpr int kQuakeMaxMarksurfaces = 32767;
constexpr int kQuakeMaxClipnodes = 65535;
constexpr int kQuakeMaxFaces = 32767;
constexpr int kQuakeMaxVertices = 65535;
constexpr int kQuakeMaxLeafs = 32767;

QString nativePath(const QString& path)
{
	return QDir::toNativeSeparators(QDir::cleanPath(path));
}

quint32 readU32(const QByteArray& bytes, qsizetype offset)
{
	if (offset < 0 || offset + 4 > bytes.size()) {
		return 0;
	}
	return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

qint32 readI32(const QByteArray& bytes, qsizetype offset)
{
	return static_cast<qint32>(readU32(bytes, offset));
}

float readF32(const QByteArray& bytes, qsizetype offset)
{
	const quint32 raw = readU32(bytes, offset);
	float value = 0.0f;
	std::memcpy(&value, &raw, sizeof(value));
	return value;
}

QString readFixedName(const QByteArray& bytes, qsizetype offset, qsizetype length)
{
	QByteArray name;
	for (qsizetype i = 0; i < length && offset + i < bytes.size(); ++i) {
		const char ch = bytes.at(offset + i);
		if (ch == '\0') {
			break;
		}
		name.append(ch);
	}
	return QString::fromLatin1(name).trimmed();
}

QJsonArray stringArrayJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QString formatCoordinate(double value)
{
	return QString::number(value, 'f', 2);
}

QString formatVector(const double values[3])
{
	return QStringLiteral("%1 %2 %3").arg(formatCoordinate(values[0]), formatCoordinate(values[1]), formatCoordinate(values[2]));
}

// ---------------------------------------------------------------------------
// Family layout tables
// ---------------------------------------------------------------------------

struct LumpPlan {
	QString name;
	int entrySize = 0;    // 0 = variable-size or raw byte stream
	bool required = false;
};

struct FamilyLayout {
	BspFamily family = BspFamily::Unknown;
	QString variantId;
	QString variantName;
	QString magic;
	int version = 0;
	qsizetype headerBytes = 0;
	qsizetype lumpTableOffset = 0;
	QVector<LumpPlan> plans;

	int entitiesLump = -1;
	int planesLump = -1;
	int verticesLump = -1;
	int nodesLump = -1;
	int leafsLump = -1;
	int facesLump = -1;
	int modelsLump = -1;
	int brushesLump = -1;
	int visLump = -1;
	int lightLump = -1;
	int textureLump = -1;     // miptex (Quake) / texinfo (Quake II) / shaders (Quake III)
	int texinfoLump = -1;     // Quake only: texinfo records that reference miptex entries
	int marksurfacesLump = -1;
	int clipnodesLump = -1;

	int vertexStride = 12;
	int faceStride = 0;
	int faceLightofsOffset = -1;
	int texinfoStride = 0;
	int texinfoMiptexOffset = -1;   // Quake
	int texinfoNameOffset = -1;     // Quake II
	int shaderStride = 0;           // Quake III
	int surfaceShaderOffset = -1;   // Quake III draw surface -> shader index
	int lightmapEntrySize = 0;      // Quake III
	bool modelHasBounds = false;
	bool checkQuakeLimits = false;
};

LumpPlan plan(const char* name, int entrySize, bool required = false)
{
	LumpPlan entry;
	entry.name = QString::fromLatin1(name);
	entry.entrySize = entrySize;
	entry.required = required;
	return entry;
}

// Quake / Half-Life / BSP2 / 2PSB share the 15-lump order:
// entities, planes, miptex, vertices, visibility, nodes, texinfo, faces,
// lighting, clipnodes, leafs, marksurfaces, edges, surfedges, models.
FamilyLayout makeQuakeLayout(const QString& variantId, const QString& variantName, const QString& magic, int version)
{
	const bool bsp2 = variantId == QStringLiteral("bsp2");
	const bool bsp2rmq = variantId == QStringLiteral("bsp2rmq");
	const bool widened = bsp2 || bsp2rmq;

	const int nodeSize = bsp2 ? 44 : bsp2rmq ? 32 : 24;
	const int leafSize = bsp2 ? 44 : bsp2rmq ? 32 : 28;
	const int faceSize = widened ? 28 : 20;
	const int clipnodeSize = widened ? 12 : 8;
	const int marksurfaceSize = widened ? 4 : 2;
	const int edgeSize = widened ? 8 : 4;

	FamilyLayout layout;
	layout.family = BspFamily::Quake;
	layout.variantId = variantId;
	layout.variantName = variantName;
	layout.magic = magic;
	layout.version = version;
	layout.lumpTableOffset = 4;
	layout.headerBytes = 4 + kQuakeLumpCount * kLumpEntryBytes;
	layout.plans = {
		plan("entities", 0, true),
		plan("planes", 20, true),
		plan("textures", 0),
		plan("vertices", 12, true),
		plan("visibility", 0),
		plan("nodes", nodeSize, true),
		plan("texinfo", 40),
		plan("faces", faceSize, true),
		plan("lighting", 0),
		plan("clipnodes", clipnodeSize),
		plan("leafs", leafSize, true),
		plan("marksurfaces", marksurfaceSize),
		plan("edges", edgeSize),
		plan("surfedges", 4),
		plan("models", 64, true),
	};
	layout.entitiesLump = 0;
	layout.planesLump = 1;
	layout.textureLump = 2;
	layout.verticesLump = 3;
	layout.visLump = 4;
	layout.nodesLump = 5;
	layout.texinfoLump = 6;
	layout.facesLump = 7;
	layout.lightLump = 8;
	layout.clipnodesLump = 9;
	layout.leafsLump = 10;
	layout.marksurfacesLump = 11;
	layout.modelsLump = 14;
	layout.vertexStride = 12;
	layout.faceStride = faceSize;
	layout.faceLightofsOffset = widened ? 24 : 16;
	layout.texinfoStride = 40;
	layout.texinfoMiptexOffset = 32;
	layout.modelHasBounds = true;
	layout.checkQuakeLimits = !widened;
	return layout;
}

// Quake II IBSP v38 and the Qbism "QBSP" extension share the 19-lump order.
FamilyLayout makeQuake2Layout(const QString& variantId, const QString& variantName, const QString& magic, int version)
{
	const bool qbism = variantId == QStringLiteral("qbism");
	const int nodeSize = qbism ? 44 : 28;
	const int leafSize = qbism ? 52 : 28;
	const int faceSize = qbism ? 28 : 20;
	const int leafIndexSize = qbism ? 4 : 2;
	const int edgeSize = qbism ? 8 : 4;
	const int brushSideSize = qbism ? 8 : 4;

	FamilyLayout layout;
	layout.family = BspFamily::Quake2;
	layout.variantId = variantId;
	layout.variantName = variantName;
	layout.magic = magic;
	layout.version = version;
	layout.lumpTableOffset = 8;
	layout.headerBytes = 8 + kQuake2LumpCount * kLumpEntryBytes;
	layout.plans = {
		plan("entities", 0, true),
		plan("planes", 20, true),
		plan("vertices", 12, true),
		plan("visibility", 0),
		plan("nodes", nodeSize, true),
		plan("texinfo", 76, true),
		plan("faces", faceSize, true),
		plan("lighting", 0),
		plan("leafs", leafSize, true),
		plan("leaffaces", leafIndexSize),
		plan("leafbrushes", leafIndexSize),
		plan("edges", edgeSize),
		plan("surfedges", 4),
		plan("models", 48, true),
		plan("brushes", 12),
		plan("brushsides", brushSideSize),
		plan("pop", 0),
		plan("areas", 8),
		plan("areaportals", 8),
	};
	layout.entitiesLump = 0;
	layout.planesLump = 1;
	layout.verticesLump = 2;
	layout.visLump = 3;
	layout.nodesLump = 4;
	layout.textureLump = 5;
	layout.facesLump = 6;
	layout.lightLump = 7;
	layout.leafsLump = 8;
	layout.modelsLump = 13;
	layout.brushesLump = 14;
	layout.vertexStride = 12;
	layout.faceStride = faceSize;
	layout.faceLightofsOffset = qbism ? 24 : 16;
	layout.texinfoStride = 76;
	layout.texinfoNameOffset = 40;
	layout.modelHasBounds = true;
	return layout;
}

// Quake III IBSP v46/v47 (17 lumps) and Raven RBSP v1 (18 lumps).
FamilyLayout makeQuake3Layout(const QString& variantId, const QString& variantName, const QString& magic, int version)
{
	const bool raven = variantId == QStringLiteral("rbsp1");
	const int drawVertSize = raven ? 80 : 44;
	const int surfaceSize = raven ? 148 : 104;
	const int brushSideSize = raven ? 12 : 8;
	const int lightGridSize = raven ? 30 : 8;

	FamilyLayout layout;
	layout.family = BspFamily::Quake3;
	layout.variantId = variantId;
	layout.variantName = variantName;
	layout.magic = magic;
	layout.version = version;
	layout.lumpTableOffset = 8;
	layout.headerBytes = 8 + (raven ? kRavenLumpCount : kQuake3LumpCount) * kLumpEntryBytes;
	layout.plans = {
		plan("entities", 0, true),
		plan("shaders", 72, true),
		plan("planes", 16, true),
		plan("nodes", 36, true),
		plan("leafs", 48, true),
		plan("leafsurfaces", 4),
		plan("leafbrushes", 4),
		plan("models", 40, true),
		plan("brushes", 12),
		plan("brushsides", brushSideSize),
		plan("drawverts", drawVertSize, true),
		plan("drawindexes", 4),
		plan("fogs", 72),
		plan("surfaces", surfaceSize, true),
		plan("lightmaps", kQuake3LightmapBytes),
		plan("lightgrid", lightGridSize),
		plan("visibility", 0),
	};
	if (raven) {
		layout.plans.push_back(plan("lightarray", 2));
	}
	layout.entitiesLump = 0;
	layout.textureLump = 1;
	layout.planesLump = 2;
	layout.nodesLump = 3;
	layout.leafsLump = 4;
	layout.modelsLump = 7;
	layout.brushesLump = 8;
	layout.verticesLump = 10;
	layout.facesLump = 13;
	layout.lightLump = 14;
	layout.visLump = 16;
	layout.vertexStride = drawVertSize;
	layout.faceStride = surfaceSize;
	layout.shaderStride = 72;
	layout.surfaceShaderOffset = 0;
	layout.lightmapEntrySize = kQuake3LightmapBytes;
	layout.modelHasBounds = true;
	return layout;
}

// Family/version sniffing. Kept deliberately consistent with
// src/core/compiler_artifact_validation.cpp so both agree on what a file is.
bool detectLayout(const QByteArray& bytes, FamilyLayout* layout)
{
	if (!layout || bytes.size() < 4) {
		return false;
	}
	const QByteArray ident = bytes.left(4);
	const qint32 firstWord = readI32(bytes, 0);

	if (ident == "IBSP" && bytes.size() >= 8) {
		const qint32 version = readI32(bytes, 4);
		if (version == 46) {
			*layout = makeQuake3Layout(QStringLiteral("ibsp46"), QCoreApplication::translate("VibeStudioBspInspect", "Quake III IBSP v46"), QStringLiteral("IBSP"), version);
			return true;
		}
		if (version == 47) {
			*layout = makeQuake3Layout(QStringLiteral("ibsp47"), QCoreApplication::translate("VibeStudioBspInspect", "Quake Live IBSP v47"), QStringLiteral("IBSP"), version);
			return true;
		}
		*layout = makeQuake2Layout(QStringLiteral("ibsp%1").arg(version), QCoreApplication::translate("VibeStudioBspInspect", "Quake II IBSP v%1").arg(version), QStringLiteral("IBSP"), version);
		return true;
	}
	if (ident == "RBSP" && bytes.size() >= 8) {
		const qint32 version = readI32(bytes, 4);
		*layout = makeQuake3Layout(QStringLiteral("rbsp1"), QCoreApplication::translate("VibeStudioBspInspect", "Raven RBSP v%1").arg(version), QStringLiteral("RBSP"), version);
		return true;
	}
	if (ident == "QBSP" && bytes.size() >= 8) {
		const qint32 version = readI32(bytes, 4);
		*layout = makeQuake2Layout(QStringLiteral("qbism"), QCoreApplication::translate("VibeStudioBspInspect", "Qbism extended Quake II BSP v%1").arg(version), QStringLiteral("QBSP"), version);
		return true;
	}
	if (ident == "BSP2") {
		*layout = makeQuakeLayout(QStringLiteral("bsp2"), QCoreApplication::translate("VibeStudioBspInspect", "Quake BSP2"), QStringLiteral("BSP2"), 0);
		return true;
	}
	if (ident == "2PSB") {
		*layout = makeQuakeLayout(QStringLiteral("bsp2rmq"), QCoreApplication::translate("VibeStudioBspInspect", "Quake BSP2-RMQ (2PSB)"), QStringLiteral("2PSB"), 0);
		return true;
	}
	if (firstWord == 29) {
		*layout = makeQuakeLayout(QStringLiteral("bsp29"), QCoreApplication::translate("VibeStudioBspInspect", "Quake BSP v29"), QStringLiteral("BSP"), 29);
		return true;
	}
	if (firstWord == 30) {
		*layout = makeQuakeLayout(QStringLiteral("bsp30"), QCoreApplication::translate("VibeStudioBspInspect", "Half-Life BSP v30"), QStringLiteral("BSP"), 30);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Entity lump text
// ---------------------------------------------------------------------------

struct EntityToken {
	QString text;
	bool valid = false;
	bool quoted = false;
	bool brace = false;
};

bool isEntityWhitespace(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v' || ch == '\0';
}

EntityToken nextEntityToken(const QByteArray& raw, qsizetype* cursor)
{
	EntityToken token;
	qsizetype pos = *cursor;
	while (pos < raw.size()) {
		while (pos < raw.size() && isEntityWhitespace(raw.at(pos))) {
			++pos;
		}
		// qbsp keeps `//` comments out of the lump, but tolerate them anyway.
		if (pos + 1 < raw.size() && raw.at(pos) == '/' && raw.at(pos + 1) == '/') {
			while (pos < raw.size() && raw.at(pos) != '\n') {
				++pos;
			}
			continue;
		}
		break;
	}
	if (pos >= raw.size()) {
		*cursor = pos;
		return token;
	}

	const char ch = raw.at(pos);
	if (ch == '{' || ch == '}') {
		token.valid = true;
		token.brace = true;
		token.text = QString(QLatin1Char(ch));
		*cursor = pos + 1;
		return token;
	}
	if (ch == '"') {
		++pos;
		QByteArray value;
		while (pos < raw.size() && raw.at(pos) != '"') {
			value.append(raw.at(pos));
			++pos;
		}
		if (pos < raw.size()) {
			++pos;    // consume the closing quote
		}
		token.valid = true;
		token.quoted = true;
		token.text = QString::fromUtf8(value);
		*cursor = pos;
		return token;
	}

	QByteArray value;
	while (pos < raw.size() && !isEntityWhitespace(raw.at(pos)) && raw.at(pos) != '{' && raw.at(pos) != '}') {
		value.append(raw.at(pos));
		++pos;
	}
	token.valid = !value.isEmpty();
	token.text = QString::fromUtf8(value);
	*cursor = pos;
	return token;
}

QVector<BspEntitySummary> parseEntityLump(const QByteArray& raw, QString* worldspawnMessage, QStringList* warnings)
{
	QVector<BspEntitySummary> entities;
	qsizetype cursor = 0;
	bool sawWorldspawn = false;
	bool truncatedBlock = false;

	while (cursor < raw.size() && entities.size() < kMaxEntityEntries) {
		EntityToken token = nextEntityToken(raw, &cursor);
		if (!token.valid) {
			break;
		}
		if (!token.brace || token.text != QStringLiteral("{")) {
			continue;
		}

		BspEntitySummary entity;
		entity.index = static_cast<int>(entities.size());
		bool closed = false;
		while (cursor < raw.size()) {
			EntityToken key = nextEntityToken(raw, &cursor);
			if (!key.valid) {
				break;
			}
			if (key.brace && key.text == QStringLiteral("}")) {
				closed = true;
				break;
			}
			if (key.brace) {
				// A nested brush block inside the entity lump should not
				// happen in a compiled BSP; skip it rather than misreading.
				continue;
			}
			EntityToken value = nextEntityToken(raw, &cursor);
			if (!value.valid || (value.brace && value.text == QStringLiteral("}"))) {
				closed = value.valid;
				break;
			}
			BspEntityKeyValue pair;
			pair.key = key.text;
			pair.value = value.text;
			entity.properties.push_back(pair);
			if (pair.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0 && entity.className.isEmpty()) {
				entity.className = pair.value;
			}
		}
		if (!closed) {
			truncatedBlock = true;
		}
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			sawWorldspawn = true;
			if (worldspawnMessage) {
				for (const BspEntityKeyValue& pair : entity.properties) {
					if (pair.key.compare(QStringLiteral("message"), Qt::CaseInsensitive) == 0) {
						*worldspawnMessage = pair.value;
						break;
					}
				}
			}
		}
		entities.push_back(entity);
	}

	if (warnings) {
		if (truncatedBlock) {
			warnings->push_back(QCoreApplication::translate("VibeStudioBspInspect", "Entity lump ends inside an unterminated entity block; the lump may be truncated."));
		}
		if (!entities.isEmpty() && !sawWorldspawn) {
			warnings->push_back(QCoreApplication::translate("VibeStudioBspInspect", "Entity lump contains no worldspawn entity; most engines refuse to load this map."));
		}
	}
	return entities;
}

// ---------------------------------------------------------------------------
// Texture decoding
// ---------------------------------------------------------------------------

void sortTextures(QVector<BspTextureSummary>* textures)
{
	if (!textures) {
		return;
	}
	std::sort(textures->begin(), textures->end(), [](const BspTextureSummary& lhs, const BspTextureSummary& rhs) {
		const int compared = lhs.name.compare(rhs.name, Qt::CaseInsensitive);
		if (compared != 0) {
			return compared < 0;
		}
		return lhs.name < rhs.name;
	});
}

// Quake miptex directory: int32 nummiptex, int32 dataofs[nummiptex] relative to
// the lump start, each pointing at { char name[16]; uint32 width, height;
// uint32 offsets[4]; }. A dataofs of -1 marks an externally supplied texture
// (Half-Life WAD references).
QVector<BspTextureSummary> decodeQuakeTextures(const QByteArray& bytes, const BspLumpInfo& lump, const QByteArray& texinfoData, int texinfoStride, int miptexOffset, QStringList* warnings)
{
	QVector<BspTextureSummary> textures;
	if (!lump.withinFile || lump.length < 4) {
		return textures;
	}
	const qsizetype base = static_cast<qsizetype>(lump.offset);
	const qint32 count = readI32(bytes, base);
	if (count <= 0 || count > kMaxTextureEntries) {
		if (warnings && count != 0) {
			warnings->push_back(QCoreApplication::translate("VibeStudioBspInspect", "Texture lump declares an implausible miptex count (%1); textures were not decoded.").arg(count));
		}
		return textures;
	}
	const qsizetype directoryBytes = 4 + static_cast<qsizetype>(count) * 4;
	if (directoryBytes > static_cast<qsizetype>(lump.length)) {
		if (warnings) {
			warnings->push_back(QCoreApplication::translate("VibeStudioBspInspect", "Texture lump directory extends past the lump; textures were not decoded."));
		}
		return textures;
	}

	QVector<int> referenceCounts(count, 0);
	if (texinfoStride > 0 && miptexOffset >= 0 && !texinfoData.isEmpty()) {
		const qsizetype records = texinfoData.size() / texinfoStride;
		for (qsizetype i = 0; i < records; ++i) {
			const qint32 index = readI32(texinfoData, i * texinfoStride + miptexOffset);
			if (index >= 0 && index < count) {
				referenceCounts[index] += 1;
			}
		}
	}

	int missingEntries = 0;
	for (qint32 i = 0; i < count; ++i) {
		const qint32 dataOffset = readI32(bytes, base + 4 + static_cast<qsizetype>(i) * 4);
		BspTextureSummary texture;
		texture.embedded = true;
		texture.referenceCount = referenceCounts.at(i);
		if (dataOffset < 0) {
			// Half-Life style external texture reference: the name is not
			// stored here, so there is nothing safe to report.
			++missingEntries;
			continue;
		}
		const qsizetype entryOffset = base + dataOffset;
		if (dataOffset > static_cast<qint32>(lump.length) || entryOffset + 24 > bytes.size()) {
			++missingEntries;
			continue;
		}
		texture.name = readFixedName(bytes, entryOffset, 16);
		texture.width = static_cast<int>(readU32(bytes, entryOffset + 16));
		texture.height = static_cast<int>(readU32(bytes, entryOffset + 20));
		if (texture.name.isEmpty()) {
			++missingEntries;
			continue;
		}
		if (readU32(bytes, entryOffset + 24) == 0) {
			// No mip level payload: the engine sources this texture from a WAD.
			texture.embedded = false;
		}
		if (texture.referenceCount == 0) {
			texture.referenceCount = 1;
		}
		textures.push_back(texture);
	}
	if (missingEntries > 0 && warnings) {
		warnings->push_back(QCoreApplication::translate("VibeStudioBspInspect", "%1 miptex entries are missing or point outside the texture lump.").arg(missingEntries));
	}
	sortTextures(&textures);
	return textures;
}

// Quake II texinfo: { float vecs[2][4]; int flags; int value; char texture[32];
// int nexttexinfo; } == 76 bytes, texture name at byte 40.
QVector<BspTextureSummary> decodeQuake2Textures(const QByteArray& texinfoData, int stride, int nameOffset)
{
	QVector<BspTextureSummary> textures;
	if (stride <= 0 || nameOffset < 0 || texinfoData.isEmpty()) {
		return textures;
	}
	QMap<QString, BspTextureSummary> byName;
	const qsizetype records = texinfoData.size() / stride;
	for (qsizetype i = 0; i < records; ++i) {
		const qsizetype recordOffset = i * stride;
		const QString name = readFixedName(texinfoData, recordOffset + nameOffset, 32);
		if (name.isEmpty()) {
			continue;
		}
		auto it = byName.find(name);
		if (it == byName.end()) {
			BspTextureSummary texture;
			texture.name = name;
			texture.embedded = false;
			texture.referenceCount = 1;
			// texinfo_t is: float vecs[2][4] (0-31), int flags (32), int value (36),
			// char texture[32] (40), int nexttexinfo (72) -- released Quake II
			// qfiles.h. Offset 36 is the light/surface value, not content flags;
			// Quake II keeps contents on brushes, so they stay zero here.
			texture.surfaceFlags = readU32(texinfoData, recordOffset + 32);
			texture.surfaceValue = static_cast<qint32>(readU32(texinfoData, recordOffset + 36));
			byName.insert(name, texture);
		} else {
			it->referenceCount += 1;
			it->surfaceFlags |= readU32(texinfoData, recordOffset + 32);
		}
	}
	for (const BspTextureSummary& texture : byName) {
		textures.push_back(texture);
	}
	sortTextures(&textures);
	return textures;
}

// Quake III dshader_t: { char shader[64]; int surfaceFlags; int contentFlags; }.
QVector<BspTextureSummary> decodeQuake3Textures(const QByteArray& shaderData, int stride, const QByteArray& surfaceData, int surfaceStride, int shaderIndexOffset)
{
	QVector<BspTextureSummary> textures;
	if (stride <= 0 || shaderData.isEmpty()) {
		return textures;
	}
	const qsizetype records = shaderData.size() / stride;
	QVector<int> referenceCounts(static_cast<qsizetype>(records), 0);
	if (surfaceStride > 0 && shaderIndexOffset >= 0 && !surfaceData.isEmpty()) {
		const qsizetype surfaces = surfaceData.size() / surfaceStride;
		for (qsizetype i = 0; i < surfaces; ++i) {
			const qint32 index = readI32(surfaceData, i * surfaceStride + shaderIndexOffset);
			if (index >= 0 && static_cast<qsizetype>(index) < records) {
				referenceCounts[index] += 1;
			}
		}
	}

	QMap<QString, BspTextureSummary> byName;
	for (qsizetype i = 0; i < records; ++i) {
		const qsizetype recordOffset = i * stride;
		const QString name = readFixedName(shaderData, recordOffset, 64);
		if (name.isEmpty()) {
			continue;
		}
		const int references = referenceCounts.at(i) > 0 ? referenceCounts.at(i) : 1;
		auto it = byName.find(name);
		if (it == byName.end()) {
			BspTextureSummary texture;
			texture.name = name;
			texture.embedded = false;
			texture.referenceCount = references;
			texture.surfaceFlags = readU32(shaderData, recordOffset + 64);
			texture.contentFlags = readU32(shaderData, recordOffset + 68);
			byName.insert(name, texture);
		} else {
			it->referenceCount += references;
			it->surfaceFlags |= readU32(shaderData, recordOffset + 64);
			it->contentFlags |= readU32(shaderData, recordOffset + 68);
		}
	}
	for (const BspTextureSummary& texture : byName) {
		textures.push_back(texture);
	}
	sortTextures(&textures);
	return textures;
}

// ---------------------------------------------------------------------------
// Inspection assembly
// ---------------------------------------------------------------------------

const BspLumpInfo* findLump(const BspInspection& inspection, int index)
{
	if (index < 0 || index >= inspection.lumps.size()) {
		return nullptr;
	}
	return &inspection.lumps.at(index);
}

QByteArray lumpData(const QByteArray& bytes, const BspInspection& inspection, int index)
{
	const BspLumpInfo* lump = findLump(inspection, index);
	if (!lump || !lump->withinFile || lump->length == 0) {
		return {};
	}
	return bytes.mid(static_cast<qsizetype>(lump->offset), static_cast<qsizetype>(lump->length));
}

int lumpEntryCount(const BspInspection& inspection, int index)
{
	const BspLumpInfo* lump = findLump(inspection, index);
	if (!lump || !lump->withinFile) {
		return 0;
	}
	return lump->entryCount;
}

bool lumpHasData(const BspInspection& inspection, int index)
{
	const BspLumpInfo* lump = findLump(inspection, index);
	return lump != nullptr && lump->withinFile && lump->length > 0;
}

void computeBounds(const QByteArray& bytes, const FamilyLayout& layout, BspInspection* inspection)
{
	if (!inspection) {
		return;
	}
	if (layout.modelHasBounds) {
		const QByteArray models = lumpData(bytes, *inspection, layout.modelsLump);
		if (models.size() >= 24) {
			for (int axis = 0; axis < 3; ++axis) {
				inspection->mins[axis] = static_cast<double>(readF32(models, axis * 4));
				inspection->maxs[axis] = static_cast<double>(readF32(models, 12 + axis * 4));
			}
			return;
		}
	}

	const QByteArray vertices = lumpData(bytes, *inspection, layout.verticesLump);
	const int stride = layout.vertexStride;
	if (stride <= 0 || vertices.size() < stride) {
		return;
	}
	double mins[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
	double maxs[3] = {-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
	const qsizetype count = vertices.size() / stride;
	for (qsizetype i = 0; i < count; ++i) {
		for (int axis = 0; axis < 3; ++axis) {
			const double value = static_cast<double>(readF32(vertices, i * stride + axis * 4));
			mins[axis] = std::min(mins[axis], value);
			maxs[axis] = std::max(maxs[axis], value);
		}
	}
	for (int axis = 0; axis < 3; ++axis) {
		inspection->mins[axis] = mins[axis];
		inspection->maxs[axis] = maxs[axis];
	}
}

// Quake/Quake II store one lightmap offset per face; a negative offset means
// the face is unlit. Quake III stores whole 128x128 lightmap pages instead.
int countLitFaces(const QByteArray& faceData, int stride, int lightofsOffset)
{
	if (stride <= 0 || lightofsOffset < 0 || faceData.isEmpty()) {
		return 0;
	}
	int lit = 0;
	const qsizetype count = faceData.size() / stride;
	for (qsizetype i = 0; i < count; ++i) {
		if (readI32(faceData, i * stride + lightofsOffset) >= 0) {
			++lit;
		}
	}
	return lit;
}

void appendLimitWarning(BspInspection* inspection, int value, int limit, const QString& label)
{
	if (!inspection || value <= limit) {
		return;
	}
	inspection->warnings.push_back(QCoreApplication::translate("VibeStudioBspInspect", "%1 count %2 exceeds the classic engine limit of %3; vanilla engines and tools may reject this map.").arg(label).arg(value).arg(limit));
}

void buildDetailLines(BspInspection* inspection, const QString& variantName)
{
	if (!inspection) {
		return;
	}
	QStringList details;
	details << QCoreApplication::translate("VibeStudioBspInspect", "Format: %1").arg(variantName.isEmpty() ? bspFamilyDisplayName(inspection->family) : variantName);
	details << QCoreApplication::translate("VibeStudioBspInspect", "Ident: %1").arg(inspection->magic.isEmpty() ? QCoreApplication::translate("VibeStudioBspInspect", "none") : inspection->magic);
	details << QCoreApplication::translate("VibeStudioBspInspect", "Version: %1").arg(inspection->version);
	details << QCoreApplication::translate("VibeStudioBspInspect", "File size: %1 bytes").arg(inspection->fileSizeBytes);
	details << QCoreApplication::translate("VibeStudioBspInspect", "Lumps: %1").arg(inspection->lumps.size());
	details << QCoreApplication::translate("VibeStudioBspInspect", "Entities: %1").arg(inspection->entityCount);
	if (!inspection->worldspawnMessage.isEmpty()) {
		details << QCoreApplication::translate("VibeStudioBspInspect", "Worldspawn message: %1").arg(inspection->worldspawnMessage);
	}
	details << QCoreApplication::translate("VibeStudioBspInspect", "Geometry: %1 models, %2 faces, %3 vertices").arg(inspection->modelCount).arg(inspection->faceCount).arg(inspection->vertexCount);
	details << QCoreApplication::translate("VibeStudioBspInspect", "Tree: %1 nodes, %2 leafs, %3 planes").arg(inspection->nodeCount).arg(inspection->leafCount).arg(inspection->planeCount);
	details << QCoreApplication::translate("VibeStudioBspInspect", "Brushes: %1").arg(inspection->brushCount);
	details << QCoreApplication::translate("VibeStudioBspInspect", "Textures: %1").arg(inspection->textures.size());
	details << QCoreApplication::translate("VibeStudioBspInspect", "Lightmaps: %1").arg(inspection->lightmapCount);
	details << QCoreApplication::translate("VibeStudioBspInspect", "Bounds: %1 to %2").arg(formatVector(inspection->mins), formatVector(inspection->maxs));
	details << QCoreApplication::translate("VibeStudioBspInspect", "Visibility data: %1").arg(inspection->hasVisData ? QCoreApplication::translate("VibeStudioBspInspect", "present") : QCoreApplication::translate("VibeStudioBspInspect", "absent"));
	details << QCoreApplication::translate("VibeStudioBspInspect", "Light data: %1").arg(inspection->hasLightData ? QCoreApplication::translate("VibeStudioBspInspect", "present") : QCoreApplication::translate("VibeStudioBspInspect", "absent"));
	inspection->detailLines = details;
}

// ---------------------------------------------------------------------------
// Text helpers for the auxiliary compiler files
// ---------------------------------------------------------------------------

QStringList splitTextLines(const QByteArray& raw)
{
	QString text = QString::fromUtf8(raw);
	text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
	return text.split(QLatin1Char('\n'));
}

QStringList whitespaceTokens(const QString& line)
{
	QStringList tokens;
	QString current;
	for (const QChar ch : line) {
		if (ch.isSpace()) {
			if (!current.isEmpty()) {
				tokens.push_back(current);
				current.clear();
			}
			continue;
		}
		current.append(ch);
	}
	if (!current.isEmpty()) {
		tokens.push_back(current);
	}
	return tokens;
}

// QString::toDouble always parses with the C locale, so leak/portal files
// written on a comma-decimal machine are still read consistently.
bool parseNumber(const QString& token, double* value)
{
	bool ok = false;
	QString cleaned = token;
	while (!cleaned.isEmpty() && (cleaned.startsWith(QLatin1Char('(')) || cleaned.startsWith(QLatin1Char('[')))) {
		cleaned.remove(0, 1);
	}
	while (!cleaned.isEmpty() && (cleaned.endsWith(QLatin1Char(')')) || cleaned.endsWith(QLatin1Char(']')) || cleaned.endsWith(QLatin1Char(',')))) {
		cleaned.chop(1);
	}
	const double parsed = cleaned.toDouble(&ok);
	if (ok && value) {
		*value = parsed;
	}
	return ok;
}

bool parseCount(const QString& line, int* value)
{
	const QStringList tokens = whitespaceTokens(line);
	if (tokens.isEmpty()) {
		return false;
	}
	bool ok = false;
	const int parsed = tokens.first().toInt(&ok);
	if (ok && value) {
		*value = parsed;
	}
	return ok;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

OperationState BspInspection::state() const
{
	if (!valid || !errors.isEmpty() || !error.isEmpty()) {
		return OperationState::Failed;
	}
	if (!warnings.isEmpty()) {
		return OperationState::Warning;
	}
	return OperationState::Completed;
}

QString bspFamilyId(BspFamily family)
{
	switch (family) {
	case BspFamily::Quake:
		return QStringLiteral("quake");
	case BspFamily::Quake2:
		return QStringLiteral("quake2");
	case BspFamily::Quake3:
		return QStringLiteral("quake3");
	case BspFamily::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString bspFamilyDisplayName(BspFamily family)
{
	switch (family) {
	case BspFamily::Quake:
		return QCoreApplication::translate("VibeStudioBspInspect", "Quake (BSP29/BSP2)");
	case BspFamily::Quake2:
		return QCoreApplication::translate("VibeStudioBspInspect", "Quake II (IBSP 38)");
	case BspFamily::Quake3:
		return QCoreApplication::translate("VibeStudioBspInspect", "Quake III (IBSP 46/RBSP)");
	case BspFamily::Unknown:
		break;
	}
	return QCoreApplication::translate("VibeStudioBspInspect", "Unknown BSP family");
}

BspInspection inspectBspBytes(const QString& sourcePath, const QByteArray& bytes)
{
	BspInspection inspection;
	inspection.sourcePath = QDir::cleanPath(sourcePath);
	inspection.fileSizeBytes = bytes.size();
	inspection.familyId = bspFamilyId(BspFamily::Unknown);

	FamilyLayout layout;
	if (!detectLayout(bytes, &layout)) {
		inspection.error = QCoreApplication::translate("VibeStudioBspInspect", "%1 does not start with a recognized idTech BSP header.").arg(nativePath(inspection.sourcePath));
		inspection.errors.push_back(inspection.error);
		buildDetailLines(&inspection, QString());
		return inspection;
	}

	inspection.family = layout.family;
	inspection.familyId = bspFamilyId(layout.family);
	inspection.magic = layout.magic;
	inspection.version = layout.version;

	if (bytes.size() < layout.headerBytes) {
		inspection.error = QCoreApplication::translate("VibeStudioBspInspect", "%1 is truncated: %2 bytes cannot hold the %3 byte lump table.")
			.arg(nativePath(inspection.sourcePath))
			.arg(bytes.size())
			.arg(layout.headerBytes);
		inspection.errors.push_back(inspection.error);
		buildDetailLines(&inspection, layout.variantName);
		return inspection;
	}

	// Lump table. Nothing read from here is trusted before it is bounds
	// checked against the real file size.
	for (qsizetype i = 0; i < layout.plans.size(); ++i) {
		const LumpPlan& entry = layout.plans.at(i);
		const qsizetype tableOffset = layout.lumpTableOffset + i * kLumpEntryBytes;
		BspLumpInfo info;
		info.index = static_cast<int>(i);
		info.name = entry.name;
		info.offset = readU32(bytes, tableOffset);
		info.length = readU32(bytes, tableOffset + 4);
		info.entrySize = entry.entrySize;

		const quint64 end = static_cast<quint64>(info.offset) + static_cast<quint64>(info.length);
		info.withinFile = end <= static_cast<quint64>(bytes.size())
			&& (info.length == 0 || info.offset >= static_cast<quint32>(layout.headerBytes));
		info.aligned = entry.entrySize <= 0 || info.length % static_cast<quint32>(entry.entrySize) == 0;
		info.entryCount = entry.entrySize > 0 ? static_cast<int>(info.length / static_cast<quint32>(entry.entrySize)) : 0;

		if (!info.withinFile) {
			inspection.errors.push_back(QCoreApplication::translate("VibeStudioBspInspect", "Lump %1 (%2) spans bytes %3..%4, which is outside the %5 byte file; it was not read.")
				.arg(info.index)
				.arg(entry.name)
				.arg(info.offset)
				.arg(end)
				.arg(bytes.size()));
		} else if (!info.aligned) {
			inspection.warnings.push_back(QCoreApplication::translate("VibeStudioBspInspect", "Lump %1 (%2) has length %3, which is not a multiple of its %4 byte record size.")
				.arg(info.index)
				.arg(entry.name)
				.arg(info.length)
				.arg(entry.entrySize));
		}
		if (entry.required && info.withinFile && info.length == 0) {
			inspection.warnings.push_back(QCoreApplication::translate("VibeStudioBspInspect", "Required lump %1 (%2) is empty.").arg(info.index).arg(entry.name));
		}
		inspection.lumps.push_back(info);
	}

	// Entities.
	const QByteArray entityData = lumpData(bytes, inspection, layout.entitiesLump);
	if (!entityData.isEmpty()) {
		inspection.entities = parseEntityLump(entityData, &inspection.worldspawnMessage, &inspection.warnings);
	}
	inspection.entityCount = static_cast<int>(inspection.entities.size());

	// Counts.
	const QByteArray faceData = lumpData(bytes, inspection, layout.facesLump);
	inspection.modelCount = lumpEntryCount(inspection, layout.modelsLump);
	inspection.faceCount = lumpEntryCount(inspection, layout.facesLump);
	inspection.vertexCount = lumpEntryCount(inspection, layout.verticesLump);
	inspection.leafCount = lumpEntryCount(inspection, layout.leafsLump);
	inspection.nodeCount = lumpEntryCount(inspection, layout.nodesLump);
	inspection.planeCount = lumpEntryCount(inspection, layout.planesLump);
	inspection.brushCount = lumpEntryCount(inspection, layout.brushesLump);
	inspection.hasVisData = lumpHasData(inspection, layout.visLump);
	inspection.hasLightData = lumpHasData(inspection, layout.lightLump);
	if (layout.lightmapEntrySize > 0) {
		inspection.lightmapCount = lumpEntryCount(inspection, layout.lightLump);
	} else {
		inspection.lightmapCount = countLitFaces(faceData, layout.faceStride, layout.faceLightofsOffset);
	}

	// Textures.
	if (layout.family == BspFamily::Quake) {
		const BspLumpInfo* textureLump = findLump(inspection, layout.textureLump);
		const QByteArray texinfoData = lumpData(bytes, inspection, layout.texinfoLump);
		if (textureLump) {
			inspection.textures = decodeQuakeTextures(bytes, *textureLump, texinfoData, layout.texinfoStride, layout.texinfoMiptexOffset, &inspection.warnings);
		}
	} else if (layout.family == BspFamily::Quake2) {
		const QByteArray texinfoData = lumpData(bytes, inspection, layout.textureLump);
		inspection.textures = decodeQuake2Textures(texinfoData, layout.texinfoStride, layout.texinfoNameOffset);
	} else if (layout.family == BspFamily::Quake3) {
		const QByteArray shaderData = lumpData(bytes, inspection, layout.textureLump);
		inspection.textures = decodeQuake3Textures(shaderData, layout.shaderStride, faceData, layout.faceStride, layout.surfaceShaderOffset);
	}

	computeBounds(bytes, layout, &inspection);

	if (layout.checkQuakeLimits) {
		appendLimitWarning(&inspection, lumpEntryCount(inspection, layout.marksurfacesLump), kQuakeMaxMarksurfaces, QCoreApplication::translate("VibeStudioBspInspect", "Marksurface"));
		appendLimitWarning(&inspection, lumpEntryCount(inspection, layout.clipnodesLump), kQuakeMaxClipnodes, QCoreApplication::translate("VibeStudioBspInspect", "Clipnode"));
		appendLimitWarning(&inspection, inspection.faceCount, kQuakeMaxFaces, QCoreApplication::translate("VibeStudioBspInspect", "Face"));
		appendLimitWarning(&inspection, inspection.vertexCount, kQuakeMaxVertices, QCoreApplication::translate("VibeStudioBspInspect", "Vertex"));
		appendLimitWarning(&inspection, inspection.leafCount, kQuakeMaxLeafs, QCoreApplication::translate("VibeStudioBspInspect", "Leaf"));
	}

	inspection.valid = inspection.errors.isEmpty();
	if (!inspection.valid && inspection.error.isEmpty()) {
		inspection.error = inspection.errors.first();
	}
	inspection.warnings.removeDuplicates();
	inspection.errors.removeDuplicates();
	buildDetailLines(&inspection, layout.variantName);
	return inspection;
}

BspInspection inspectBspFile(const QString& path)
{
	BspInspection inspection;
	inspection.sourcePath = QDir::cleanPath(path);
	inspection.familyId = bspFamilyId(BspFamily::Unknown);

	const QFileInfo info(inspection.sourcePath);
	if (!info.exists() || !info.isFile()) {
		inspection.error = QCoreApplication::translate("VibeStudioBspInspect", "BSP file does not exist: %1").arg(nativePath(inspection.sourcePath));
		inspection.errors.push_back(inspection.error);
		buildDetailLines(&inspection, QString());
		return inspection;
	}
	QFile file(inspection.sourcePath);
	if (!file.open(QIODevice::ReadOnly)) {
		inspection.error = QCoreApplication::translate("VibeStudioBspInspect", "BSP file cannot be opened for reading: %1").arg(nativePath(inspection.sourcePath));
		inspection.errors.push_back(inspection.error);
		inspection.fileSizeBytes = info.size();
		buildDetailLines(&inspection, QString());
		return inspection;
	}
	const QByteArray bytes = file.readAll();
	file.close();
	return inspectBspBytes(inspection.sourcePath, bytes);
}

LeakPointFile loadLeakPointFile(const QString& path)
{
	LeakPointFile leak;
	leak.sourcePath = QDir::cleanPath(path);

	QFile file(leak.sourcePath);
	if (!file.exists()) {
		leak.error = QCoreApplication::translate("VibeStudioBspInspect", "Leak point file does not exist: %1").arg(nativePath(leak.sourcePath));
		return leak;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		leak.error = QCoreApplication::translate("VibeStudioBspInspect", "Leak point file cannot be opened for reading: %1").arg(nativePath(leak.sourcePath));
		return leak;
	}
	const QByteArray raw = file.readAll();
	file.close();

	double mins[3] = {std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
	double maxs[3] = {-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};

	// qbsp writes one "x y z" triple per line. Junk lines (headers, blank
	// lines, trailing notes) are skipped rather than treated as an error.
	const QStringList lines = splitTextLines(raw);
	for (const QString& line : lines) {
		const QStringList tokens = whitespaceTokens(line);
		if (tokens.size() < 3) {
			continue;
		}
		double values[3] = {0.0, 0.0, 0.0};
		bool parsed = true;
		for (int axis = 0; axis < 3; ++axis) {
			if (!parseNumber(tokens.at(axis), &values[axis])) {
				parsed = false;
				break;
			}
		}
		if (!parsed) {
			continue;
		}
		for (int axis = 0; axis < 3; ++axis) {
			leak.pointsXyz.push_back(values[axis]);
			mins[axis] = std::min(mins[axis], values[axis]);
			maxs[axis] = std::max(maxs[axis], values[axis]);
		}
	}

	leak.pointCount = static_cast<int>(leak.pointsXyz.size() / 3);
	if (leak.pointCount == 0) {
		leak.error = QCoreApplication::translate("VibeStudioBspInspect", "Leak point file contains no readable coordinate triples: %1").arg(nativePath(leak.sourcePath));
		return leak;
	}
	for (int axis = 0; axis < 3; ++axis) {
		leak.mins[axis] = mins[axis];
		leak.maxs[axis] = maxs[axis];
	}
	leak.valid = true;
	return leak;
}

PortalFileSummary inspectPortalFile(const QString& path)
{
	PortalFileSummary summary;
	summary.sourcePath = QDir::cleanPath(path);

	QFile file(summary.sourcePath);
	if (!file.exists()) {
		summary.error = QCoreApplication::translate("VibeStudioBspInspect", "Portal file does not exist: %1").arg(nativePath(summary.sourcePath));
		return summary;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		summary.error = QCoreApplication::translate("VibeStudioBspInspect", "Portal file cannot be opened for reading: %1").arg(nativePath(summary.sourcePath));
		return summary;
	}
	const QByteArray raw = file.readAll();
	file.close();
	summary.fileSizeBytes = raw.size();

	QStringList lines = splitTextLines(raw);
	// Drop trailing blank lines so the "lines after the header" check is not
	// skewed by the final newline.
	while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
		lines.removeLast();
	}

	qsizetype cursor = 0;
	while (cursor < lines.size() && lines.at(cursor).trimmed().isEmpty()) {
		++cursor;
	}
	if (cursor >= lines.size()) {
		summary.error = QCoreApplication::translate("VibeStudioBspInspect", "Portal file is empty: %1").arg(nativePath(summary.sourcePath));
		return summary;
	}

	summary.magic = lines.at(cursor).trimmed();
	++cursor;

	// qbsp/vis portal file headers: PRT1 and PORTALFILE carry a leaf count and
	// a portal count; PRT2 and PRT1-AM add a cluster count.
	const bool hasClusters = summary.magic == QStringLiteral("PRT2") || summary.magic == QStringLiteral("PRT1-AM");
	const bool knownMagic = hasClusters
		|| summary.magic == QStringLiteral("PRT1")
		|| summary.magic == QStringLiteral("PORTALFILE");
	if (!knownMagic) {
		summary.error = QCoreApplication::translate("VibeStudioBspInspect", "Portal file does not begin with a known qbsp portal magic (found \"%1\"): %2")
			.arg(summary.magic, nativePath(summary.sourcePath));
		return summary;
	}

	const int expectedCountLines = hasClusters ? 3 : 2;
	QVector<int> counts;
	while (counts.size() < expectedCountLines && cursor < lines.size()) {
		const QString line = lines.at(cursor);
		++cursor;
		if (line.trimmed().isEmpty()) {
			continue;
		}
		int value = 0;
		if (!parseCount(line, &value)) {
			summary.error = QCoreApplication::translate("VibeStudioBspInspect", "Portal file header is malformed; expected a count but found \"%1\": %2")
				.arg(line.trimmed(), nativePath(summary.sourcePath));
			return summary;
		}
		counts.push_back(value);
	}
	if (counts.size() < expectedCountLines) {
		summary.error = QCoreApplication::translate("VibeStudioBspInspect", "Portal file header is incomplete; expected %1 count lines after \"%2\": %3")
			.arg(expectedCountLines)
			.arg(summary.magic, nativePath(summary.sourcePath));
		return summary;
	}

	summary.leafCount = counts.at(0);
	if (hasClusters) {
		summary.clusterCount = counts.at(1);
		summary.portalCount = counts.at(2);
	} else {
		summary.clusterCount = counts.at(0);
		summary.portalCount = counts.at(1);
	}

	if (summary.leafCount < 0 || summary.clusterCount < 0 || summary.portalCount < 0) {
		summary.error = QCoreApplication::translate("VibeStudioBspInspect", "Portal file declares a negative leaf, cluster or portal count: %1").arg(nativePath(summary.sourcePath));
		return summary;
	}

	// Portal geometry is deliberately not parsed; only the declared counts are
	// checked against how much data actually follows the header.
	int remainingLines = 0;
	for (qsizetype i = cursor; i < lines.size(); ++i) {
		if (!lines.at(i).trimmed().isEmpty()) {
			++remainingLines;
		}
	}
	if (remainingLines < summary.portalCount) {
		summary.warnings.push_back(QCoreApplication::translate("VibeStudioBspInspect", "Portal file declares %1 portals but only %2 data lines follow the header; the file looks truncated.")
			.arg(summary.portalCount)
			.arg(remainingLines));
	}
	if (summary.portalCount == 0) {
		summary.warnings.push_back(QCoreApplication::translate("VibeStudioBspInspect", "Portal file declares zero portals; visibility data cannot be computed from it."));
	}
	if (summary.leafCount == 0) {
		summary.warnings.push_back(QCoreApplication::translate("VibeStudioBspInspect", "Portal file declares zero leafs."));
	}
	summary.valid = true;
	return summary;
}

CompiledMapArtifacts inspectCompiledMapArtifacts(const QString& bspPath)
{
	CompiledMapArtifacts artifacts;
	artifacts.bspPath = QDir::cleanPath(bspPath);
	artifacts.bsp = inspectBspFile(artifacts.bspPath);

	const QFileInfo bspInfo(artifacts.bspPath);
	const QDir folder(bspInfo.absolutePath());
	const QString base = bspInfo.completeBaseName();

	QStringList candidates;
	for (const QString& suffix : {QStringLiteral("pts"), QStringLiteral("lin"), QStringLiteral("prt"), QStringLiteral("lit"), QStringLiteral("log"), QStringLiteral("srf")}) {
		candidates.push_back(folder.filePath(QStringLiteral("%1.%2").arg(base, suffix)));
	}
	// qbsp variants and some wrappers write a plain `<base>.leak` or a
	// numbered `<base>.leak1`; match the whole family.
	const QStringList leakMatches = folder.entryList(QStringList{QStringLiteral("%1.leak*").arg(base)}, QDir::Files, QDir::Name);
	for (const QString& match : leakMatches) {
		candidates.push_back(folder.filePath(match));
	}

	QString leakPath;
	QString portalPath;
	for (const QString& candidate : candidates) {
		const QFileInfo info(candidate);
		if (!info.isFile()) {
			continue;
		}
		const QString cleaned = QDir::cleanPath(info.absoluteFilePath());
		if (!artifacts.relatedPaths.contains(cleaned)) {
			artifacts.relatedPaths.push_back(cleaned);
		}
		const QString suffix = info.suffix().toLower();
		if (suffix == QStringLiteral("prt") && portalPath.isEmpty()) {
			portalPath = cleaned;
		}
		const bool leakSuffix = suffix == QStringLiteral("pts") || suffix == QStringLiteral("lin") || suffix.startsWith(QStringLiteral("leak"));
		if (leakSuffix) {
			artifacts.hasLeakFile = true;
			if (leakPath.isEmpty()) {
				leakPath = cleaned;
			}
		}
	}
	std::sort(artifacts.relatedPaths.begin(), artifacts.relatedPaths.end());

	if (!leakPath.isEmpty()) {
		artifacts.leak = loadLeakPointFile(leakPath);
		// A leaked compile still exits 0, so the presence of this file is the
		// only reliable signal that the map is not sealed.
		artifacts.warnings.push_back(QCoreApplication::translate("VibeStudioBspInspect", "Leak file present: %1. The compile leaked - the map is not sealed, so visibility and lighting results cannot be trusted.")
			.arg(nativePath(leakPath)));
		if (artifacts.leak.valid) {
			artifacts.warnings.push_back(QCoreApplication::translate("VibeStudioBspInspect", "Leak line has %1 points from %2 to %3.")
				.arg(artifacts.leak.pointCount)
				.arg(formatVector(artifacts.leak.mins), formatVector(artifacts.leak.maxs)));
		} else if (!artifacts.leak.error.isEmpty()) {
			artifacts.warnings.push_back(artifacts.leak.error);
		}
	}

	if (!portalPath.isEmpty()) {
		artifacts.portals = inspectPortalFile(portalPath);
		artifacts.hasPortalFile = true;
		if (!artifacts.portals.valid && !artifacts.portals.error.isEmpty()) {
			artifacts.warnings.push_back(artifacts.portals.error);
		}
		artifacts.warnings += artifacts.portals.warnings;
	}

	if (!artifacts.bsp.valid && !artifacts.bsp.error.isEmpty()) {
		artifacts.warnings.push_back(artifacts.bsp.error);
	}
	artifacts.warnings.removeDuplicates();
	return artifacts;
}

QStringList bspInspectionLines(const BspInspection& inspection)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioBspInspect", "BSP inspection: %1").arg(nativePath(inspection.sourcePath));
	lines << QCoreApplication::translate("VibeStudioBspInspect", "Family: %1").arg(bspFamilyDisplayName(inspection.family));
	lines += inspection.detailLines;

	if (!inspection.lumps.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBspInspect", "Lump table:");
		for (const BspLumpInfo& lump : inspection.lumps) {
			QString status;
			if (!lump.withinFile) {
				status = QCoreApplication::translate("VibeStudioBspInspect", " [outside file]");
			} else if (!lump.aligned) {
				status = QCoreApplication::translate("VibeStudioBspInspect", " [misaligned]");
			}
			lines << QCoreApplication::translate("VibeStudioBspInspect", "  %1 %2: offset %3, length %4, record %5, entries %6%7")
				.arg(lump.index, 2)
				.arg(lump.name)
				.arg(lump.offset)
				.arg(lump.length)
				.arg(lump.entrySize)
				.arg(lump.entryCount)
				.arg(status);
		}
	}

	if (!inspection.textures.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBspInspect", "Textures:");
		for (const BspTextureSummary& texture : inspection.textures) {
			if (texture.width > 0 && texture.height > 0) {
				lines << QCoreApplication::translate("VibeStudioBspInspect", "  %1 (%2x%3, %4 references, %5)")
					.arg(texture.name)
					.arg(texture.width)
					.arg(texture.height)
					.arg(texture.referenceCount)
					.arg(texture.embedded ? QCoreApplication::translate("VibeStudioBspInspect", "embedded") : QCoreApplication::translate("VibeStudioBspInspect", "external"));
			} else {
				lines << QCoreApplication::translate("VibeStudioBspInspect", "  %1 (%2 references, %3)")
					.arg(texture.name)
					.arg(texture.referenceCount)
					.arg(texture.embedded ? QCoreApplication::translate("VibeStudioBspInspect", "embedded") : QCoreApplication::translate("VibeStudioBspInspect", "external"));
			}
		}
	}

	for (const QString& warning : inspection.warnings) {
		lines << QCoreApplication::translate("VibeStudioBspInspect", "Warning: %1").arg(warning);
	}
	for (const QString& error : inspection.errors) {
		lines << QCoreApplication::translate("VibeStudioBspInspect", "Error: %1").arg(error);
	}
	return lines;
}

QString bspInspectionText(const BspInspection& inspection)
{
	return bspInspectionLines(inspection).join(QLatin1Char('\n'));
}

QJsonObject bspInspectionJson(const BspInspection& inspection)
{
	QJsonObject object;
	object.insert(QStringLiteral("source"), inspection.sourcePath);
	object.insert(QStringLiteral("family"), inspection.familyId);
	object.insert(QStringLiteral("magic"), inspection.magic);
	object.insert(QStringLiteral("version"), inspection.version);
	object.insert(QStringLiteral("fileSizeBytes"), QString::number(inspection.fileSizeBytes));
	object.insert(QStringLiteral("valid"), inspection.valid);
	object.insert(QStringLiteral("state"), operationStateId(inspection.state()));

	QJsonObject counts;
	counts.insert(QStringLiteral("entities"), inspection.entityCount);
	counts.insert(QStringLiteral("models"), inspection.modelCount);
	counts.insert(QStringLiteral("faces"), inspection.faceCount);
	counts.insert(QStringLiteral("vertices"), inspection.vertexCount);
	counts.insert(QStringLiteral("leafs"), inspection.leafCount);
	counts.insert(QStringLiteral("nodes"), inspection.nodeCount);
	counts.insert(QStringLiteral("planes"), inspection.planeCount);
	counts.insert(QStringLiteral("brushes"), inspection.brushCount);
	counts.insert(QStringLiteral("lightmaps"), inspection.lightmapCount);
	counts.insert(QStringLiteral("textures"), static_cast<int>(inspection.textures.size()));
	object.insert(QStringLiteral("counts"), counts);

	QJsonArray minsArray;
	QJsonArray maxsArray;
	for (int axis = 0; axis < 3; ++axis) {
		minsArray.append(inspection.mins[axis]);
		maxsArray.append(inspection.maxs[axis]);
	}
	QJsonObject bounds;
	bounds.insert(QStringLiteral("mins"), minsArray);
	bounds.insert(QStringLiteral("maxs"), maxsArray);
	object.insert(QStringLiteral("bounds"), bounds);

	object.insert(QStringLiteral("hasVisData"), inspection.hasVisData);
	object.insert(QStringLiteral("hasLightData"), inspection.hasLightData);
	object.insert(QStringLiteral("worldspawnMessage"), inspection.worldspawnMessage);

	QJsonArray lumps;
	for (const BspLumpInfo& lump : inspection.lumps) {
		QJsonObject entry;
		entry.insert(QStringLiteral("index"), lump.index);
		entry.insert(QStringLiteral("name"), lump.name);
		entry.insert(QStringLiteral("offset"), static_cast<double>(lump.offset));
		entry.insert(QStringLiteral("length"), static_cast<double>(lump.length));
		entry.insert(QStringLiteral("entrySize"), lump.entrySize);
		entry.insert(QStringLiteral("entryCount"), lump.entryCount);
		entry.insert(QStringLiteral("withinFile"), lump.withinFile);
		entry.insert(QStringLiteral("aligned"), lump.aligned);
		lumps.append(entry);
	}
	object.insert(QStringLiteral("lumps"), lumps);

	QJsonArray entities;
	for (const BspEntitySummary& entity : inspection.entities) {
		QJsonObject entry;
		entry.insert(QStringLiteral("index"), entity.index);
		entry.insert(QStringLiteral("classname"), entity.className);
		QJsonArray properties;
		for (const BspEntityKeyValue& pair : entity.properties) {
			QJsonObject property;
			property.insert(QStringLiteral("key"), pair.key);
			property.insert(QStringLiteral("value"), pair.value);
			properties.append(property);
		}
		entry.insert(QStringLiteral("properties"), properties);
		entities.append(entry);
	}
	object.insert(QStringLiteral("entities"), entities);

	QJsonArray textures;
	for (const BspTextureSummary& texture : inspection.textures) {
		QJsonObject entry;
		entry.insert(QStringLiteral("name"), texture.name);
		entry.insert(QStringLiteral("width"), texture.width);
		entry.insert(QStringLiteral("height"), texture.height);
		entry.insert(QStringLiteral("references"), texture.referenceCount);
		entry.insert(QStringLiteral("embedded"), texture.embedded);
		entry.insert(QStringLiteral("surfaceFlags"), static_cast<double>(texture.surfaceFlags));
		entry.insert(QStringLiteral("contentFlags"), static_cast<double>(texture.contentFlags));
		textures.append(entry);
	}
	object.insert(QStringLiteral("textures"), textures);

	object.insert(QStringLiteral("details"), stringArrayJson(inspection.detailLines));
	object.insert(QStringLiteral("warnings"), stringArrayJson(inspection.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(inspection.errors));
	object.insert(QStringLiteral("error"), inspection.error);
	return object;
}

QJsonObject compiledMapArtifactsJson(const CompiledMapArtifacts& artifacts)
{
	QJsonObject object;
	object.insert(QStringLiteral("bspPath"), artifacts.bspPath);
	object.insert(QStringLiteral("bsp"), bspInspectionJson(artifacts.bsp));
	object.insert(QStringLiteral("hasLeakFile"), artifacts.hasLeakFile);
	object.insert(QStringLiteral("hasPortalFile"), artifacts.hasPortalFile);

	QJsonObject leak;
	leak.insert(QStringLiteral("source"), artifacts.leak.sourcePath);
	leak.insert(QStringLiteral("valid"), artifacts.leak.valid);
	leak.insert(QStringLiteral("pointCount"), artifacts.leak.pointCount);
	QJsonArray leakMins;
	QJsonArray leakMaxs;
	for (int axis = 0; axis < 3; ++axis) {
		leakMins.append(artifacts.leak.mins[axis]);
		leakMaxs.append(artifacts.leak.maxs[axis]);
	}
	leak.insert(QStringLiteral("mins"), leakMins);
	leak.insert(QStringLiteral("maxs"), leakMaxs);
	leak.insert(QStringLiteral("error"), artifacts.leak.error);
	object.insert(QStringLiteral("leak"), leak);

	QJsonObject portals;
	portals.insert(QStringLiteral("source"), artifacts.portals.sourcePath);
	portals.insert(QStringLiteral("valid"), artifacts.portals.valid);
	portals.insert(QStringLiteral("magic"), artifacts.portals.magic);
	portals.insert(QStringLiteral("portalCount"), artifacts.portals.portalCount);
	portals.insert(QStringLiteral("leafCount"), artifacts.portals.leafCount);
	portals.insert(QStringLiteral("clusterCount"), artifacts.portals.clusterCount);
	portals.insert(QStringLiteral("fileSizeBytes"), QString::number(artifacts.portals.fileSizeBytes));
	portals.insert(QStringLiteral("warnings"), stringArrayJson(artifacts.portals.warnings));
	portals.insert(QStringLiteral("error"), artifacts.portals.error);
	object.insert(QStringLiteral("portals"), portals);

	object.insert(QStringLiteral("relatedPaths"), stringArrayJson(artifacts.relatedPaths));
	object.insert(QStringLiteral("warnings"), stringArrayJson(artifacts.warnings));
	return object;
}

QString compiledMapArtifactsText(const CompiledMapArtifacts& artifacts)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioBspInspect", "Compiled map artifacts: %1").arg(nativePath(artifacts.bspPath));
	lines += bspInspectionLines(artifacts.bsp);

	if (!artifacts.relatedPaths.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioBspInspect", "Related files:");
		for (const QString& path : artifacts.relatedPaths) {
			lines << QCoreApplication::translate("VibeStudioBspInspect", "  %1").arg(nativePath(path));
		}
	}

	if (artifacts.hasLeakFile) {
		lines << QCoreApplication::translate("VibeStudioBspInspect", "Leak file: %1").arg(nativePath(artifacts.leak.sourcePath));
		if (artifacts.leak.valid) {
			lines << QCoreApplication::translate("VibeStudioBspInspect", "  Leak line points: %1").arg(artifacts.leak.pointCount);
			lines << QCoreApplication::translate("VibeStudioBspInspect", "  Leak line bounds: %1 to %2").arg(formatVector(artifacts.leak.mins), formatVector(artifacts.leak.maxs));
		}
	}

	if (artifacts.hasPortalFile) {
		lines << QCoreApplication::translate("VibeStudioBspInspect", "Portal file: %1").arg(nativePath(artifacts.portals.sourcePath));
		lines << QCoreApplication::translate("VibeStudioBspInspect", "  Magic: %1").arg(artifacts.portals.magic);
		lines << QCoreApplication::translate("VibeStudioBspInspect", "  Leafs: %1, clusters: %2, portals: %3")
			.arg(artifacts.portals.leafCount)
			.arg(artifacts.portals.clusterCount)
			.arg(artifacts.portals.portalCount);
	}

	for (const QString& warning : artifacts.warnings) {
		lines << QCoreApplication::translate("VibeStudioBspInspect", "Warning: %1").arg(warning);
	}
	return lines.join(QLatin1Char('\n'));
}

} // namespace vibestudio
