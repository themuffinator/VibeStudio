#include "core/level_map.h"
#include "core/level_doom_selection.h"
#include "core/level_doom_nodes.h"
#include "core/level_udmf.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_patch.h"
#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_texture_mapping.h"
#include "core/level_surface.h"
#include "core/level_material_paint.h"
#include "core/level_primitive.h"
#include "core/level_merge.h"
#include "core/level_placement_control_p.h"

#include "core/ericw_map_preflight.h"
#include "core/map_geometry.h"
#include "core/map_geometry_cache.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>

namespace vibestudio {

namespace {

// Builds translated native records without publishing them or touching history.
// Duplication and paste use the same geometry/UV validation as ordinary moves.
bool prepareLevelMapPlacement(const LevelMapDocument& source, const QVector<LevelMapSelectionRef>& objects,
	const LevelMapVec3& offset, const LevelMapTextureLockOptions& textures, LevelMapUndoCommand* prepared, QString* error);

struct MapLoadCancelled {};
void loadCancellationCheckpoint(const LevelMapLoadRequest* request)
{
	detail::placementCancellationCheckpoint();
	if (request && request->isCancelled && request->isCancelled()) { throw MapLoadCancelled {}; }
}
void loadCheckpoint(const LevelMapLoadRequest* request, LevelMapLoadPhase phase, qint64 done = 0, qint64 total = 0)
{
	detail::placementCancellationCheckpoint();
	if (!request) { return; }
	const auto cancelled = [&] { return request->isCancelled && request->isCancelled(); };
	if (cancelled()) { throw MapLoadCancelled {}; }
	if (request->progress) { request->progress(phase, done, total); }
	if (cancelled()) { throw MapLoadCancelled {}; }
}

QStringList loadTextLines(const QString& text, const LevelMapLoadRequest* request)
{
	QStringList lines;
	qsizetype start = 0;
	while (true) {
		if ((lines.size() & 255) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Indexing, start, text.size()); }
		const auto end = text.indexOf(QLatin1Char('\n'), start);
		auto line = end < 0 ? text.mid(start) : text.mid(start, end - start);
		if (line.endsWith(QLatin1Char('\r'))) { line.chop(1); }
		lines.append(std::move(line));
		if (end < 0) { break; }
		start = end + 1;
	}
	return lines;
}

QString normalizedId(QString value)
{
	value = value.trimmed().toLower().replace('_', '-');
	while (value.contains(QStringLiteral("--"))) {
		value.replace(QStringLiteral("--"), QStringLiteral("-"));
	}
	return value;
}

qint16 readLe16Signed(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 2 > data.size()) {
		return 0;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	return static_cast<qint16>(bytes[0] | (bytes[1] << 8));
}

quint16 readLe16(const QByteArray& data, qsizetype offset)
{
	return static_cast<quint16>(readLe16Signed(data, offset));
}

quint8 readByte(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset >= data.size()) {
		return 0;
	}
	return static_cast<quint8>(data.at(offset));
}

qint32 readLe32Signed(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 4 > data.size()) {
		return 0;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	return static_cast<qint32>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
}

void appendLe16(QByteArray* data, qint16 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendByte(QByteArray* data, int value)
{
	data->append(static_cast<char>(value & 0xff));
}

void appendLe32(QByteArray* data, qint32 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
	data->append(static_cast<char>((value >> 16) & 0xff));
	data->append(static_cast<char>((value >> 24) & 0xff));
}

QString fixedLatin1(const QByteArray& data, qsizetype offset, qsizetype length)
{
	if (offset < 0 || offset >= data.size() || length <= 0) {
		return {};
	}
	const qsizetype available = std::min(length, data.size() - offset);
	qsizetype size = 0;
	while (size < available && data.at(offset + size) != '\0') {
		++size;
	}
	return QString::fromLatin1(data.constData() + offset, size).trimmed();
}

QByteArray fixedLatin1Bytes(const QString& value, qsizetype length)
{
	QByteArray bytes = value.toLatin1().left(length);
	while (bytes.size() < length) {
		bytes.append('\0');
	}
	return bytes;
}

bool isDoomMapLumpName(const QString& name)
{
	const QString upper = name.trimmed().toUpper();
	// Classic Doom map lumps plus the ZDoom/UDMF additions that must survive a
	// round trip. See https://doomwiki.org/wiki/WAD and
	// https://doomwiki.org/wiki/UDMF
	static const QStringList known = {
		QStringLiteral("THINGS"),
		QStringLiteral("LINEDEFS"),
		QStringLiteral("SIDEDEFS"),
		QStringLiteral("VERTEXES"),
		QStringLiteral("SEGS"),
		QStringLiteral("SSECTORS"),
		QStringLiteral("NODES"),
		QStringLiteral("SECTORS"),
		QStringLiteral("REJECT"),
		QStringLiteral("BLOCKMAP"),
		QStringLiteral("BEHAVIOR"),
		QStringLiteral("SCRIPTS"),
		QStringLiteral("TEXTMAP"),
		QStringLiteral("ENDMAP"),
		QStringLiteral("ZNODES"),
		QStringLiteral("DIALOGUE"),
		QStringLiteral("LIGHTS"),
		QStringLiteral("VS_SCENE"),
	};
	if (known.contains(upper)) {
		return true;
	}
	return upper.startsWith(QStringLiteral("GL_"));
}

// `E1M1`-style and `MAP01`-style markers, but without the one-digit and
// two-digit restrictions that hid `E1M10` and `MAP100`.
bool isConventionalDoomMapMarker(const QString& name)
{
	static const QRegularExpression marker(QStringLiteral(R"(^(E\d+M\d+|MAP\d+)$)"), QRegularExpression::CaseInsensitiveOption);
	return marker.match(name.trimmed()).hasMatch();
}

using WadLump = LevelMapWadSourceLump;

// A named marker is accepted when the lumps that follow it look like a map:
// either the classic `THINGS` opening lump or the UDMF `TEXTMAP` lump. This is
// what lets ZDoom-style named maps ("TITLEMAP", "HUB01") load.
bool isDoomMapMarkerAt(const QVector<WadLump>& lumps, int index)
{
	if (index < 0 || index >= lumps.size()) {
		return false;
	}
	if (isConventionalDoomMapMarker(lumps.at(index).name)) {
		return true;
	}
	if (!lumps.at(index).bytes.isEmpty()) {
		return false;
	}
	const QString next = index + 1 < lumps.size() ? lumps.at(index + 1).name.toUpper() : QString();
	if (next == QStringLiteral("TEXTMAP")) {
		return true;
	}
	if (next != QStringLiteral("THINGS")) {
		return false;
	}
	// Require the rest of the classic sequence so that a stray empty lump named
	// "THINGS" elsewhere in the WAD cannot start a map.
	QSet<QString> following;
	for (int probe = index + 1; probe < lumps.size() && probe <= index + 11; ++probe) {
		following.insert(lumps.at(probe).name.toUpper());
	}
	return following.contains(QStringLiteral("LINEDEFS")) && following.contains(QStringLiteral("SIDEDEFS"))
		&& following.contains(QStringLiteral("VERTEXES")) && following.contains(QStringLiteral("SECTORS"));
}

void addIssue(LevelMapDocument* document, LevelMapIssueSeverity severity, const QString& code, const QString& message, const QString& objectId = QString(), int line = 0)
{
	if (!document) {
		return;
	}
	document->issues.push_back({severity, code, message, objectId, line});
}

void includePoint(LevelMapVec3* mins, LevelMapVec3* maxs, const LevelMapVec3& point)
{
	if (!mins || !maxs || !point.valid) {
		return;
	}
	if (!mins->valid || !maxs->valid) {
		*mins = point;
		*maxs = point;
		return;
	}
	mins->x = std::min(mins->x, point.x);
	mins->y = std::min(mins->y, point.y);
	mins->z = std::min(mins->z, point.z);
	maxs->x = std::max(maxs->x, point.x);
	maxs->y = std::max(maxs->y, point.y);
	maxs->z = std::max(maxs->z, point.z);
}

QString vecText(const LevelMapVec3& value)
{
	if (!value.valid) {
		return QCoreApplication::translate("VibeStudioLevelMap", "unknown");
	}
	return QStringLiteral("%1, %2, %3").arg(value.x, 0, 'f', 1).arg(value.y, 0, 'f', 1).arg(value.z, 0, 'f', 1);
}

QString doomPointText(double x, double y)
{
	return QStringLiteral("%1, %2").arg(x, 0, 'f', 1).arg(y, 0, 'f', 1);
}

QString propertyValue(const LevelMapEntity& entity, const QString& key)
{
	for (const LevelMapProperty& property : entity.properties) {
		if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
			return property.value;
		}
	}
	return {};
}

int propertyIndex(const LevelMapEntity& entity, const QString& key)
{
	for (int index = 0; index < entity.properties.size(); ++index) {
		if (entity.properties.at(index).key.compare(key, Qt::CaseInsensitive) == 0) {
			return index;
		}
	}
	return -1;
}

LevelMapVec3 parseVec3(const QString& value)
{
	const QStringList parts = value.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
	if (parts.size() != 3) {
		return {};
	}
	bool okX = false;
	bool okY = false;
	bool okZ = false;
	const double x = parts.value(0).toDouble(&okX);
	const double y = parts.value(1).toDouble(&okY);
	const double z = parts.value(2).toDouble(&okZ);
	if (!okX || !okY || !okZ) {
		return {};
	}
	return {x, y, z, true};
}

QString serializeVec3(const LevelMapVec3& value)
{
	if (!value.valid) {
		return QStringLiteral("0 0 0");
	}
	return QStringLiteral("%1 %2 %3").arg(value.x, 0, 'f', value.x == std::floor(value.x) ? 0 : 3).arg(value.y, 0, 'f', value.y == std::floor(value.y) ? 0 : 3).arg(value.z, 0, 'f', value.z == std::floor(value.z) ? 0 : 3);
}

QString entityObjectId(int id)
{
	return QStringLiteral("entity:%1").arg(id);
}

QString vertexObjectId(int id)
{
	return QStringLiteral("vertex:%1").arg(id);
}

QString linedefObjectId(int id)
{
	return QStringLiteral("linedef:%1").arg(id);
}

QString thingObjectId(int id)
{
	return QStringLiteral("thing:%1").arg(id);
}

QString brushObjectId(int id)
{
	return QStringLiteral("brush:%1").arg(id);
}

QString patchObjectId(int id)
{
	return QStringLiteral("patch:%1").arg(id);
}

QString sectorObjectId(int id)
{
	return QStringLiteral("sector:%1").arg(id);
}

QString sidedefObjectId(int id)
{
	return QStringLiteral("sidedef:%1").arg(id);
}

QVector<WadLump> readWadLumpsBytes(const QByteArray& bytes, QString* magicOut, QString* error, const LevelMapLoadRequest* request = nullptr)
{
	if (error) {
		error->clear();
	}
	QBuffer file;
	file.setData(bytes);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Unable to open WAD file.");
		}
		return {};
	}
	const QByteArray header = file.read(12);
	if (header.size() != 12) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "WAD header is incomplete.");
		}
		return {};
	}
	const QString magic = QString::fromLatin1(header.constData(), 4);
	if (magic != QStringLiteral("IWAD") && magic != QStringLiteral("PWAD")) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "WAD file must start with IWAD or PWAD.");
		}
		return {};
	}
	if (magicOut) {
		*magicOut = magic;
	}
	const qint32 lumpCount = readLe32Signed(header, 4);
	const qint32 directoryOffset = readLe32Signed(header, 8);
	if (lumpCount < 0 || directoryOffset < 12 || static_cast<qint64>(directoryOffset) + (static_cast<qint64>(lumpCount) * 16) > file.size()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "WAD directory is invalid.");
		}
		return {};
	}
	// An empty directory is structurally legal but holds no map. Returning an
	// empty list without an error left every caller failing with nothing to
	// tell the user, because "no lumps" and "could not read" looked identical.
	if (lumpCount == 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "WAD directory is empty; this file holds no lumps.");
		}
		return {};
	}

	// Check expansion before copying payloads: overlapping directory entries
	// must not amplify a small input into an unbounded document allocation.
	qint64 expandedBytes = 0;
	for (int index = 0; index < lumpCount; ++index) {
		if ((index & 255) == 0) { loadCancellationCheckpoint(request); }
		const qint32 size = readLe32Signed(bytes, directoryOffset + qint64(index) * 16 + 4);
		if (size >= 0) { expandedBytes += size; }
		if (expandedBytes > kLevelMapMaxDocumentBytes) {
			if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Expanded WAD lump data exceeds the 512 MiB document limit."); }
			return {};
		}
	}
	QVector<WadLump> lumps;
	if (!file.seek(directoryOffset)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Unable to seek to WAD directory.");
		}
		return {};
	}
	for (int index = 0; index < lumpCount; ++index) {
		if ((index & 255) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Indexing, index, lumpCount); }
		if (!file.seek(directoryOffset + (index * 16))) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Unable to seek to WAD directory record.");
			}
			return {};
		}
		const QByteArray record = file.read(16);
		const qint32 offset = readLe32Signed(record, 0);
		const qint32 size = readLe32Signed(record, 4);
		const QString name = fixedLatin1(record, 8, 8).toUpper();
		if (offset < 0 || size < 0 || static_cast<qint64>(offset) + size > file.size()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "WAD lump has invalid offset or size.");
			}
			return {};
		}
		if (!file.seek(offset)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Unable to seek to WAD lump.");
			}
			return {};
		}
		lumps.push_back({name.isEmpty() ? QStringLiteral("LUMP%1").arg(index) : name, file.read(size)});
	}
	return lumps;
}

QVector<WadLump> readWadLumps(const QString& path, QString* magicOut, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > kLevelMapMaxDocumentBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Unable to read the WAD, or it exceeds the 512 MiB document limit."); }
		return {};
	}
	const auto bytes = file.read(kLevelMapMaxDocumentBytes + 1);
	if (file.error() != QFileDevice::NoError || bytes.size() > kLevelMapMaxDocumentBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "The WAD could not be read completely."); }
		return {};
	}
	return readWadLumpsBytes(bytes, magicOut, error);
}

// Doom linedef flag bit 2 marks a two-sided line.
// https://doomwiki.org/wiki/Linedef
constexpr int kLinedefTwoSided = 0x0004;
constexpr int kDoomCoordinateMin = -32768;
constexpr int kDoomCoordinateMax = 32767;

bool isEmptyTextureName(const QString& value)
{
	const QString trimmed = value.trimmed();
	return trimmed.isEmpty() || trimmed == QStringLiteral("-");
}

const LevelMapDoomSector* sectorForSidedef(const LevelMapDocument& document, int sidedefId)
{
	if (sidedefId < 0 || sidedefId >= document.doomSidedefs.size()) {
		return nullptr;
	}
	const int sectorId = document.doomSidedefs.at(sidedefId).sector;
	if (sectorId < 0 || sectorId >= document.doomSectors.size()) {
		return nullptr;
	}
	return &document.doomSectors.at(sectorId);
}

void validateDoomGeometry(LevelMapDocument* document, const LevelMapLoadRequest* request = nullptr)
{
	if (!document) {
		return;
	}
	// idTech1 map health checks derived from the Doom map format pages at
	// https://doomwiki.org/wiki/Linedef , https://doomwiki.org/wiki/Sidedef and
	// https://doomwiki.org/wiki/Thing .
	qint64 checked = 0;
	QSet<int> referencedSectors;
	for (const LevelMapDoomLinedef& linedef : document->doomLinedefs) {
		if ((checked++ & 255) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Validating, checked); }
		const bool startValid = linedef.startVertex >= 0 && linedef.startVertex < document->doomVertices.size();
		const bool endValid = linedef.endVertex >= 0 && linedef.endVertex < document->doomVertices.size();
		if (startValid && endValid) {
			const LevelMapDoomVertex& start = document->doomVertices.at(linedef.startVertex);
			const LevelMapDoomVertex& end = document->doomVertices.at(linedef.endVertex);
			if (std::abs(start.x - end.x) < 0.0001 && std::abs(start.y - end.y) < 0.0001) {
				addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-zero-length"), QCoreApplication::translate("VibeStudioLevelMap", "Linedef has zero length."), linedefObjectId(linedef.id));
			}
		}
		const bool twoSidedFlag = (linedef.flags & kLinedefTwoSided) != 0;
		if (twoSidedFlag && linedef.backSidedef < 0) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-twosided-no-back"), QCoreApplication::translate("VibeStudioLevelMap", "Linedef is flagged two-sided but has no back sidedef."), linedefObjectId(linedef.id));
		}
		if (!twoSidedFlag && linedef.backSidedef >= 0) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("linedef-back-without-twosided"), QCoreApplication::translate("VibeStudioLevelMap", "Linedef has a back sidedef but is not flagged two-sided."), linedefObjectId(linedef.id));
		}
		for (const int sidedefId : {linedef.frontSidedef, linedef.backSidedef}) {
			if (sidedefId >= 0 && sidedefId < document->doomSidedefs.size()) {
				referencedSectors.insert(document->doomSidedefs.at(sidedefId).sector);
			}
		}
		const bool frontValid = linedef.frontSidedef >= 0 && linedef.frontSidedef < document->doomSidedefs.size();
		const bool backValid = linedef.backSidedef >= 0 && linedef.backSidedef < document->doomSidedefs.size();
		if (linedef.backSidedef < 0 && frontValid) {
			if (isEmptyTextureName(document->doomSidedefs.at(linedef.frontSidedef).middleTexture)) {
				addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("linedef-missing-middle"), QCoreApplication::translate("VibeStudioLevelMap", "One-sided linedef has no middle texture."), linedefObjectId(linedef.id));
			}
		}
		if (frontValid && backValid) {
			const LevelMapDoomSector* frontSector = sectorForSidedef(*document, linedef.frontSidedef);
			const LevelMapDoomSector* backSector = sectorForSidedef(*document, linedef.backSidedef);
			if (frontSector && backSector) {
				if (frontSector->ceilingHeight != backSector->ceilingHeight) {
					const int higherSide = frontSector->ceilingHeight > backSector->ceilingHeight ? linedef.frontSidedef : linedef.backSidedef;
					if (isEmptyTextureName(document->doomSidedefs.at(higherSide).upperTexture)) {
						addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("linedef-missing-upper"), QCoreApplication::translate("VibeStudioLevelMap", "Two-sided linedef has no upper texture where ceiling heights differ."), linedefObjectId(linedef.id));
					}
				}
				if (frontSector->floorHeight != backSector->floorHeight) {
					const int lowerSide = frontSector->floorHeight < backSector->floorHeight ? linedef.frontSidedef : linedef.backSidedef;
					if (isEmptyTextureName(document->doomSidedefs.at(lowerSide).lowerTexture)) {
						addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("linedef-missing-lower"), QCoreApplication::translate("VibeStudioLevelMap", "Two-sided linedef has no lower texture where floor heights differ."), linedefObjectId(linedef.id));
					}
				}
			}
		}
	}

	for (const LevelMapDoomSector& sector : document->doomSectors) {
		if ((checked++ & 255) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Validating, checked); }
		if (!referencedSectors.contains(sector.id)) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("sector-no-linedefs"), QCoreApplication::translate("VibeStudioLevelMap", "Sector is not referenced by any linedef sidedef."), sectorObjectId(sector.id));
		}
	}

	bool hasPlayerStart = false;
	for (const LevelMapDoomThing& thing : document->doomThings) {
		if ((checked++ & 255) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Validating, checked); }
		hasPlayerStart = hasPlayerStart || thing.type == 1;
	}
	if (!hasPlayerStart) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("missing-player-start"), QCoreApplication::translate("VibeStudioLevelMap", "Map has no player 1 start (thing type 1)."), document->mapName);
	}

	QSet<QString> seenVertices;
	for (const LevelMapDoomVertex& vertex : document->doomVertices) {
		if ((checked++ & 255) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Validating, checked); }
		const QString key = QStringLiteral("%1/%2").arg(std::lround(vertex.x)).arg(std::lround(vertex.y));
		if (seenVertices.contains(key)) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("duplicate-vertex"), QCoreApplication::translate("VibeStudioLevelMap", "Vertex shares coordinates with an earlier vertex."), vertexObjectId(vertex.id));
			continue;
		}
		seenVertices.insert(key);
	}
}

void warnTrailingBytes(LevelMapDocument* document, const QString& lump, qsizetype size, qsizetype stride, const QString& code)
{
	if (stride <= 0 || size <= 0 || size % stride == 0) {
		return;
	}
	addIssue(document, LevelMapIssueSeverity::Warning, code, QCoreApplication::translate("VibeStudioLevelMap", "%1 lump has trailing bytes.").arg(lump), lump);
}

} // namespace

QStringList levelMapNamesInWad(const QString& path, QString* error)
{
	if (error) { error->clear(); }
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > kLevelMapMaxDocumentBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Unable to read the WAD, or it exceeds the 512 MiB document limit."); }
		return {};
	}
	const auto header = file.read(12);
	if (header.size() != 12) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "WAD header is incomplete."); }
		return {};
	}
	if (header.first(4) != "IWAD" && header.first(4) != "PWAD") {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "WAD file must start with IWAD or PWAD."); }
		return {};
	}
	const qint32 count = readLe32Signed(header, 4), offset = readLe32Signed(header, 8);
	if (count < 0 || offset < 12 || qint64(offset) + qint64(count) * 16 > file.size() || !file.seek(offset)) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "WAD directory is invalid."); }
		return {};
	}
	if (count == 0) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "WAD directory is empty; this file holds no lumps."); }
		return {};
	}
	// Routing needs names and empty/nonempty state. Retain only the marker
	// predicate's eleven-record lookahead and never read archive payloads.
	QVector<WadLump> window;
	QStringList names;
	qint64 expanded = 0;
	for (int index = 0; index < count; ++index) {
		const auto record = file.read(16);
		const qint32 start = readLe32Signed(record, 0), size = readLe32Signed(record, 4);
		if (record.size() != 16 || start < 0 || size < 0 || qint64(start) + size > file.size()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "WAD lump has invalid offset or size."); }
			return {};
		}
		expanded += size;
		if (expanded > kLevelMapMaxDocumentBytes) {
			if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Expanded WAD lump data exceeds the 512 MiB document limit."); }
			return {};
		}
		auto name = fixedLatin1(record, 8, 8).toUpper();
		if (name.isEmpty()) { name = QStringLiteral("LUMP%1").arg(index); }
		window.append({name, size ? QByteArray(1, 'x') : QByteArray()});
		if (window.size() == 12) {
			if (isDoomMapMarkerAt(window, 0)) { names.append(window.front().name); }
			window.removeFirst();
		}
	}
	for (int index = 0; index < window.size(); ++index) {
		if (isDoomMapMarkerAt(window, index)) { names.append(window[index].name); }
	}
	return names;
}

QStringList levelMapNamesInWadDocument(const LevelMapDocument& document)
{
	QStringList names;
	for (int index = 0; index < document.doomArchiveLumps.size(); ++index) {
		if (isDoomMapMarkerAt(document.doomArchiveLumps, index)) {
			names.push_back(document.doomArchiveLumps.at(index).name);
		}
	}
	return names;
}

namespace {

// The entity a Doom thing is mirrored into, so the entity inspector and the
// key/value edit path work for things too. It shares the thing's id.
LevelMapEntity doomThingMirror(const LevelMapDoomThing& thing)
{
	LevelMapEntity entity;
	entity.id = thing.id;
	entity.className = QStringLiteral("thing:%1").arg(thing.type);
	entity.origin = {thing.x, thing.y, thing.z, true};
	entity.properties = {
		{QStringLiteral("type"), QString::number(thing.type), 0},
		{QStringLiteral("angle"), QString::number(thing.angle), 0},
		{QStringLiteral("flags"), QString::number(thing.flags), 0},
		{QStringLiteral("origin"), serializeVec3(entity.origin), 0},
		{QStringLiteral("x"), QString::number(thing.x, 'g', 17), 0},
		{QStringLiteral("y"), QString::number(thing.y, 'g', 17), 0},
	};
	return entity;
}

bool adoptUdmfText(LevelMapDocument* document, const QByteArray& bytes, QString* error = nullptr,
	const std::function<bool()>& cancel = {})
{
	if (!readLevelUdmfText(bytes, document, error, cancel)) { return false; }
	document->entities.clear();
	for (const auto& thing : document->doomThings) { document->entities << doomThingMirror(thing); }
	setLevelMapSelection(document, document->selection);
	return true;
}

bool parseDoomWadGroup(const LevelMapLoadRequest& request, const QVector<WadLump>& allLumps, const QString& magic,
	int markerIndex, LevelMapDocument* document, QString* error);

bool parseDoomWad(const LevelMapLoadRequest& request, const QByteArray& bytes, LevelMapDocument* document, QString* error)
{
	QString magic;
	const QVector<WadLump> allLumps = readWadLumpsBytes(bytes, &magic, error, &request);
	if (allLumps.isEmpty()) {
		return false;
	}

	int markerIndex = -1;
	const QString requestedMap = request.mapName.trimmed().toUpper();
	for (int index = 0; index < allLumps.size(); ++index) {
		if ((index & 255) == 0) { loadCheckpoint(&request, LevelMapLoadPhase::Parsing, index, allLumps.size()); }
		if (!isDoomMapMarkerAt(allLumps, index)) {
			continue;
		}
		if (requestedMap.isEmpty() || allLumps.at(index).name.compare(requestedMap, Qt::CaseInsensitive) == 0) {
			markerIndex = index;
			break;
		}
	}
	if (markerIndex < 0) {
		if (error) {
			*error = requestedMap.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "No Doom map marker was found in the WAD.") : QCoreApplication::translate("VibeStudioLevelMap", "Requested Doom map marker was not found.");
		}
		return false;
	}

	return parseDoomWadGroup(request, allLumps, magic, markerIndex, document, error);
}

bool parseDoomWadGroup(const LevelMapLoadRequest& request, const QVector<WadLump>& allLumps, const QString& magic,
	int markerIndex, LevelMapDocument* document, QString* error)
{
	document->sourcePath = QFileInfo(request.path).absoluteFilePath();
	document->mapName = allLumps.at(markerIndex).name;
	document->format = LevelMapFormat::DoomWad;
	document->engineFamily = QStringLiteral("idTech1");
	document->editState = QStringLiteral("clean");
	document->doomWadMagic = magic.isEmpty() ? QStringLiteral("PWAD") : magic;
	document->doomArchiveLumps = allLumps;
	const bool udmf = markerIndex + 1 < allLumps.size() && allLumps[markerIndex + 1].name == QStringLiteral("TEXTMAP");
	bool ended = false;
	for (int index = markerIndex; index < allLumps.size(); ++index) {
		if ((index & 255) == 0) { loadCancellationCheckpoint(&request); }
		if (index != markerIndex && isDoomMapMarkerAt(allLumps, index)) {
			break;
		}
		if (index == markerIndex || udmf || isDoomMapLumpName(allLumps.at(index).name)) {
			const auto& name = allLumps.at(index).name;
			if (udmf && document->doomLumps.contains(name) && (name == QStringLiteral("TEXTMAP") || isDoomNodeProduct(name))) {
				if (error) { *error = QCoreApplication::translate("LevelUdmf", "The UDMF map contains ambiguous duplicate records: %1.").arg(name); }
				return false;
			}
			document->doomLumpOrder.push_back(allLumps.at(index).name);
			document->doomLumps.insert(allLumps.at(index).name, allLumps.at(index).bytes);
			if (udmf && name == QStringLiteral("ENDMAP")) { ended = true; break; }
			continue;
		}
		break;
	}

	if (document->doomLumps.contains(QStringLiteral("TEXTMAP"))) {
		document->doomFormat = LevelMapDoomFormat::Udmf;
		if (!udmf || !ended) {
			if (error) { *error = QCoreApplication::translate("LevelUdmf", "A UDMF map requires TEXTMAP directly after its marker and a closing ENDMAP."); }
			return false;
		}
		return adoptUdmfText(document, document->doomLumps.value(QStringLiteral("TEXTMAP")), error, request.isCancelled);
	}

	// A BEHAVIOR lump means the map was compiled in Hexen format, which widens
	// linedef records to 16 bytes and thing records to 20 bytes.
	// https://doomwiki.org/wiki/Hexen_map_format
	document->doomFormat = document->doomLumps.contains(QStringLiteral("BEHAVIOR")) ? LevelMapDoomFormat::Hexen : LevelMapDoomFormat::Doom;
	if (document->doomFormat == LevelMapDoomFormat::Hexen) {
		addIssue(document, LevelMapIssueSeverity::Info, QStringLiteral("hexen-map-format"), QCoreApplication::translate("VibeStudioLevelMap", "Map uses the Hexen linedef/thing layout."), document->mapName);
	}

	const QStringList required = {QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"), QStringLiteral("VERTEXES"), QStringLiteral("SECTORS")};
	for (const QString& lump : required) {
		if (!document->doomLumps.contains(lump)) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("missing-doom-lump"), QCoreApplication::translate("VibeStudioLevelMap", "Required Doom map lump is missing: %1").arg(lump), lump);
		}
	}

	const QByteArray vertexBytes = document->doomLumps.value(QStringLiteral("VERTEXES"));
	for (qsizetype offset = 0; offset + 4 <= vertexBytes.size(); offset += 4) {
		if ((offset / 4 & 255) == 0) { loadCheckpoint(&request, LevelMapLoadPhase::Parsing); }
		LevelMapDoomVertex vertex;
		vertex.id = document->doomVertices.size();
		vertex.x = readLe16Signed(vertexBytes, offset);
		vertex.y = readLe16Signed(vertexBytes, offset + 2);
		document->doomVertices.push_back(vertex);
	}
	warnTrailingBytes(document, QStringLiteral("VERTEXES"), vertexBytes.size(), 4, QStringLiteral("trailing-vertex-bytes"));

	const bool hexen = document->doomFormat == LevelMapDoomFormat::Hexen;
	const qsizetype linedefStride = hexen ? 16 : 14;
	const QByteArray linedefBytes = document->doomLumps.value(QStringLiteral("LINEDEFS"));
	for (qsizetype offset = 0; offset + linedefStride <= linedefBytes.size(); offset += linedefStride) {
		if ((offset / linedefStride & 255) == 0) { loadCheckpoint(&request, LevelMapLoadPhase::Parsing); }
		LevelMapDoomLinedef linedef;
		linedef.id = document->doomLinedefs.size();
		linedef.startVertex = readLe16(linedefBytes, offset);
		linedef.endVertex = readLe16(linedefBytes, offset + 2);
		linedef.flags = readLe16(linedefBytes, offset + 4);
		if (hexen) {
			linedef.special = readByte(linedefBytes, offset + 6);
			for (int arg = 0; arg < 5; ++arg) {
				linedef.args[static_cast<size_t>(arg)] = readByte(linedefBytes, offset + 7 + arg);
			}
			linedef.tag = linedef.args[0];
			linedef.frontSidedef = readLe16(linedefBytes, offset + 12);
			linedef.backSidedef = readLe16(linedefBytes, offset + 14);
		} else {
			linedef.special = readLe16(linedefBytes, offset + 6);
			linedef.tag = readLe16(linedefBytes, offset + 8);
			linedef.frontSidedef = readLe16(linedefBytes, offset + 10);
			linedef.backSidedef = readLe16(linedefBytes, offset + 12);
		}
		if (linedef.frontSidedef == 0xffff) {
			linedef.frontSidedef = -1;
		}
		if (linedef.backSidedef == 0xffff) {
			linedef.backSidedef = -1;
		}
		if (linedef.startVertex < 0 || linedef.startVertex >= document->doomVertices.size() || linedef.endVertex < 0 || linedef.endVertex >= document->doomVertices.size()) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-invalid-vertex"), QCoreApplication::translate("VibeStudioLevelMap", "Linedef references a vertex outside the VERTEXES lump."), linedefObjectId(linedef.id));
		}
		document->doomLinedefs.push_back(linedef);
	}
	warnTrailingBytes(document, QStringLiteral("LINEDEFS"), linedefBytes.size(), linedefStride, QStringLiteral("trailing-linedef-bytes"));

	const qsizetype thingStride = hexen ? 20 : 10;
	const QByteArray thingBytes = document->doomLumps.value(QStringLiteral("THINGS"));
	for (qsizetype offset = 0; offset + thingStride <= thingBytes.size(); offset += thingStride) {
		if ((offset / thingStride & 255) == 0) { loadCheckpoint(&request, LevelMapLoadPhase::Parsing); }
		LevelMapDoomThing thing;
		thing.id = document->doomThings.size();
		if (hexen) {
			thing.tid = readLe16(thingBytes, offset);
			thing.x = readLe16Signed(thingBytes, offset + 2);
			thing.y = readLe16Signed(thingBytes, offset + 4);
			thing.z = readLe16Signed(thingBytes, offset + 6);
			thing.angle = readLe16(thingBytes, offset + 8);
			thing.type = readLe16(thingBytes, offset + 10);
			thing.flags = readLe16(thingBytes, offset + 12);
			thing.special = readByte(thingBytes, offset + 14);
			for (int arg = 0; arg < 5; ++arg) {
				thing.args[static_cast<size_t>(arg)] = readByte(thingBytes, offset + 15 + arg);
			}
		} else {
			thing.x = readLe16Signed(thingBytes, offset);
			thing.y = readLe16Signed(thingBytes, offset + 2);
			thing.angle = readLe16(thingBytes, offset + 4);
			thing.type = readLe16(thingBytes, offset + 6);
			thing.flags = readLe16(thingBytes, offset + 8);
		}
		document->doomThings.push_back(thing);
		document->entities.push_back(doomThingMirror(thing));
	}
	warnTrailingBytes(document, QStringLiteral("THINGS"), thingBytes.size(), thingStride, QStringLiteral("trailing-thing-bytes"));

	const QByteArray sidedefBytes = document->doomLumps.value(QStringLiteral("SIDEDEFS"));
	for (qsizetype offset = 0; offset + 30 <= sidedefBytes.size(); offset += 30) {
		if ((offset / 30 & 255) == 0) { loadCheckpoint(&request, LevelMapLoadPhase::Parsing); }
		LevelMapDoomSidedef sidedef;
		sidedef.id = document->doomSidedefs.size();
		sidedef.offsetX = readLe16Signed(sidedefBytes, offset);
		sidedef.offsetY = readLe16Signed(sidedefBytes, offset + 2);
		sidedef.upperTexture = fixedLatin1(sidedefBytes, offset + 4, 8);
		sidedef.lowerTexture = fixedLatin1(sidedefBytes, offset + 12, 8);
		sidedef.middleTexture = fixedLatin1(sidedefBytes, offset + 20, 8);
		sidedef.sector = readLe16Signed(sidedefBytes, offset + 28);
		for (const QString& texture : {sidedef.upperTexture, sidedef.lowerTexture, sidedef.middleTexture}) {
			if (!texture.isEmpty() && texture != QStringLiteral("-")) {
				document->textureReferences.push_back(texture);
			}
		}
		document->doomSidedefs.push_back(sidedef);
	}
	warnTrailingBytes(document, QStringLiteral("SIDEDEFS"), sidedefBytes.size(), 30, QStringLiteral("trailing-sidedef-bytes"));

	const QByteArray sectorBytes = document->doomLumps.value(QStringLiteral("SECTORS"));
	for (qsizetype offset = 0; offset + 26 <= sectorBytes.size(); offset += 26) {
		if ((offset / 26 & 255) == 0) { loadCheckpoint(&request, LevelMapLoadPhase::Parsing); }
		LevelMapDoomSector sector;
		sector.id = document->doomSectors.size();
		sector.floorHeight = readLe16Signed(sectorBytes, offset);
		sector.ceilingHeight = readLe16Signed(sectorBytes, offset + 2);
		sector.floorTexture = fixedLatin1(sectorBytes, offset + 4, 8);
		sector.ceilingTexture = fixedLatin1(sectorBytes, offset + 12, 8);
		sector.lightLevel = readLe16Signed(sectorBytes, offset + 20);
		sector.special = readLe16(sectorBytes, offset + 22);
		sector.tag = readLe16(sectorBytes, offset + 24);
		if (!sector.floorTexture.isEmpty()) {
			document->textureReferences.push_back(sector.floorTexture);
		}
		if (!sector.ceilingTexture.isEmpty()) {
			document->textureReferences.push_back(sector.ceilingTexture);
		}
		document->doomSectors.push_back(sector);
	}
	warnTrailingBytes(document, QStringLiteral("SECTORS"), sectorBytes.size(), 26, QStringLiteral("trailing-sector-bytes"));

	for (const LevelMapDoomSidedef& sidedef : document->doomSidedefs) {
		if ((sidedef.id & 255) == 0) { loadCancellationCheckpoint(&request); }
		if (sidedef.sector < 0 || sidedef.sector >= document->doomSectors.size()) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("sidedef-invalid-sector"), QCoreApplication::translate("VibeStudioLevelMap", "Sidedef references a sector outside SECTORS."), sidedefObjectId(sidedef.id));
		}
	}

	if (document->doomVertices.isEmpty() || document->doomLinedefs.isEmpty()) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("empty-doom-geometry"), QCoreApplication::translate("VibeStudioLevelMap", "Doom map has no vertices or linedefs to preview."), document->mapName);
	}
	for (const LevelMapDoomLinedef& linedef : document->doomLinedefs) {
		if ((linedef.id & 255) == 0) { loadCancellationCheckpoint(&request); }
		if (linedef.frontSidedef >= document->doomSidedefs.size()) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-invalid-sidedef"), QCoreApplication::translate("VibeStudioLevelMap", "Linedef front sidedef is outside SIDEDEFS."), linedefObjectId(linedef.id));
		}
		if (linedef.backSidedef >= document->doomSidedefs.size()) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-invalid-sidedef"), QCoreApplication::translate("VibeStudioLevelMap", "Linedef back sidedef is outside SIDEDEFS."), linedefObjectId(linedef.id));
		}
	}
	validateDoomGeometry(document, &request);
	return true;
}

// ---------------------------------------------------------------------------
// Quake-family `.map` tokenizer
//
// The grammar is the one shared by Quake, Quake II, Valve 220, Quake III
// (`brushDef`, `patchDef2`, `patchDef3`) and idTech4 (`brushDef3`). See the
// Quake Wiki map format page (https://quakewiki.org/wiki/Quake_Map_Format),
// the Valve 220 description (https://developer.valvesoftware.com/wiki/MAP_(file_format))
// and the Quake III / GtkRadiant brush primitive notes
// (https://icculus.org/gtkradiant/documentation/q3radiant_manual/).
// ---------------------------------------------------------------------------

enum class MapTokenType {
	End,
	Punct,
	String,
	Word,
};

struct MapToken {
	MapTokenType type = MapTokenType::End;
	QString text;
	int line = 0;
	// Where on its line the token starts: its opening quote, for a string.
	int column = 0;
};

bool isMapPunct(QChar ch)
{
	return ch == QLatin1Char('{') || ch == QLatin1Char('}') || ch == QLatin1Char('(')
		|| ch == QLatin1Char(')') || ch == QLatin1Char('[') || ch == QLatin1Char(']');
}

QVector<MapToken> tokenizeMapText(const QString& text, QStringList* comments, const LevelMapLoadRequest* request = nullptr)
{
	QVector<MapToken> tokens;
	const int size = text.size();
	int line = 1;
	int lineStart = 0;
	int index = 0;
	int lastCheckpoint = -4096;
	const auto checkpoint = [&] {
		if (index - lastCheckpoint >= 4096) {
			loadCheckpoint(request, LevelMapLoadPhase::Tokenizing, index, size);
			lastCheckpoint = index;
		}
	};
	while (index < size) {
		checkpoint();
		const QChar ch = text.at(index);
		if (ch == QLatin1Char('\n')) {
			++line;
			++index;
			lineStart = index;
			continue;
		}
		if (ch.isSpace()) {
			++index;
			continue;
		}
		if (ch == QLatin1Char('/') && index + 1 < size && text.at(index + 1) == QLatin1Char('/')) {
			const int start = index;
			while (index < size && text.at(index) != QLatin1Char('\n')) {
				checkpoint();
				++index;
			}
			if (comments) {
				comments->push_back(text.mid(start, index - start));
			}
			continue;
		}
		if (ch == QLatin1Char('/') && index + 1 < size && text.at(index + 1) == QLatin1Char('*')) {
			const int start = index;
			index += 2;
			while (index + 1 < size && !(text.at(index) == QLatin1Char('*') && text.at(index + 1) == QLatin1Char('/'))) {
				checkpoint();
				if (text.at(index) == QLatin1Char('\n')) {
					++line;
					lineStart = index + 1;
				}
				++index;
			}
			if (comments) {
				comments->push_back(text.mid(start, std::min(size, index + 2) - start));
			}
			index = std::min(size, index + 2);
			continue;
		}
		if (ch == QLatin1Char('"')) {
			const int column = index - lineStart;
			++index;
			const int start = index;
			while (index < size && text.at(index) != QLatin1Char('"') && text.at(index) != QLatin1Char('\n')) {
				checkpoint();
				++index;
			}
			tokens.push_back({MapTokenType::String, text.mid(start, index - start), line, column});
			if (index < size && text.at(index) == QLatin1Char('"')) {
				++index;
			}
			continue;
		}
		if (isMapPunct(ch)) {
			tokens.push_back({MapTokenType::Punct, QString(ch), line, index - lineStart});
			++index;
			continue;
		}
		const int start = index;
		while (index < size) {
			checkpoint();
			const QChar current = text.at(index);
			if (current.isSpace() || current == QLatin1Char('"') || isMapPunct(current)) {
				break;
			}
			if (current == QLatin1Char('/') && index + 1 < size
				&& (text.at(index + 1) == QLatin1Char('/') || text.at(index + 1) == QLatin1Char('*'))) {
				break;
			}
			++index;
		}
		if (index == start) {
			++index;
			continue;
		}
		tokens.push_back({MapTokenType::Word, text.mid(start, index - start), line, start - lineStart});
	}
	return tokens;
}

const MapToken& tokenAt(const QVector<MapToken>& tokens, int index)
{
	static const MapToken endToken;
	if (index < 0 || index >= tokens.size()) {
		return endToken;
	}
	return tokens.at(index);
}

bool tokenIsPunct(const MapToken& token, QLatin1Char value)
{
	return token.type == MapTokenType::Punct && token.text.size() == 1 && token.text.at(0) == value;
}

bool tokenNumber(const MapToken& token, double* out)
{
	if (token.type != MapTokenType::Word) {
		return false;
	}
	bool ok = false;
	// QString::toDouble always parses with the C locale, so `1.5e3` and `.5`
	// behave the same regardless of the user's system locale.
	const double value = token.text.toDouble(&ok);
	if (!ok) {
		return false;
	}
	if (out) {
		*out = value;
	}
	return true;
}

int matchingBraceIndex(const QVector<MapToken>& tokens, int openIndex, const LevelMapLoadRequest* request = nullptr)
{
	int depth = 0;
	for (int index = openIndex; index < tokens.size(); ++index) {
		if ((index & 1023) == 0) { loadCancellationCheckpoint(request); }
		if (tokenIsPunct(tokens.at(index), QLatin1Char('{'))) {
			++depth;
		} else if (tokenIsPunct(tokens.at(index), QLatin1Char('}'))) {
			--depth;
			if (depth == 0) {
				return index;
			}
		}
	}
	return -1;
}

// Reads `( n n ... )` as a flat number list.
bool readNumberGroup(const QVector<MapToken>& tokens, int* index, int limit, QVector<double>* values, const LevelMapLoadRequest* request = nullptr)
{
	if (!index || !values) {
		return false;
	}
	if (!tokenIsPunct(tokenAt(tokens, *index), QLatin1Char('('))) {
		return false;
	}
	++(*index);
	while (*index < limit) {
		if ((*index & 1023) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Parsing, *index, tokens.size()); }
		const MapToken& token = tokenAt(tokens, *index);
		if (tokenIsPunct(token, QLatin1Char(')'))) {
			++(*index);
			return true;
		}
		double value = 0.0;
		if (!tokenNumber(token, &value)) {
			return false;
		}
		values->push_back(value);
		++(*index);
	}
	return false;
}

// Reads `[ n n n n ]`, the Valve 220 texture axis form.
bool readBracketGroup(const QVector<MapToken>& tokens, int* index, int limit, QVector<double>* values, const LevelMapLoadRequest* request = nullptr)
{
	if (!index || !values) {
		return false;
	}
	if (!tokenIsPunct(tokenAt(tokens, *index), QLatin1Char('['))) {
		return false;
	}
	++(*index);
	while (*index < limit) {
		if ((*index & 1023) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Parsing, *index, tokens.size()); }
		const MapToken& token = tokenAt(tokens, *index);
		if (tokenIsPunct(token, QLatin1Char(']'))) {
			++(*index);
			return true;
		}
		double value = 0.0;
		if (!tokenNumber(token, &value)) {
			return false;
		}
		values->push_back(value);
		++(*index);
	}
	return false;
}

// Reads `( ( a b c ) ( d e f ) )`, the brush-primitive texture matrix.
bool readTextureMatrix(const QVector<MapToken>& tokens, int* index, int limit, std::array<double, 6>* matrix, const LevelMapLoadRequest* request = nullptr)
{
	if (!index || !matrix) {
		return false;
	}
	if (!tokenIsPunct(tokenAt(tokens, *index), QLatin1Char('('))) {
		return false;
	}
	++(*index);
	QVector<double> first;
	QVector<double> second;
	if (!readNumberGroup(tokens, index, limit, &first, request) || first.size() != 3) {
		return false;
	}
	if (!readNumberGroup(tokens, index, limit, &second, request) || second.size() != 3) {
		return false;
	}
	if (!tokenIsPunct(tokenAt(tokens, *index), QLatin1Char(')'))) {
		return false;
	}
	++(*index);
	for (int slot = 0; slot < 3; ++slot) {
		(*matrix)[static_cast<size_t>(slot)] = first.at(slot);
		(*matrix)[static_cast<size_t>(slot + 3)] = second.at(slot);
	}
	return true;
}

bool readShaderName(const QVector<MapToken>& tokens, int* index, int limit, QString* name)
{
	if (!index || !name || *index >= limit) {
		return false;
	}
	const MapToken& token = tokenAt(tokens, *index);
	if (token.type != MapTokenType::Word && token.type != MapTokenType::String) {
		return false;
	}
	*name = token.text;
	++(*index);
	return true;
}

void readOptionalFlags(const QVector<MapToken>& tokens, int* index, int limit, LevelMapBrushFace* face)
{
	// Quake II and Quake III append `contents surface value` after the texture
	// placement numbers. https://quakewiki.org/wiki/Quake_2_Map_Format
	QVector<double> extras;
	while (*index < limit && extras.size() < 3) {
		double value = 0.0;
		if (!tokenNumber(tokenAt(tokens, *index), &value)) {
			break;
		}
		extras.push_back(value);
		++(*index);
	}
	if (extras.size() >= 1) {
		face->contentFlags = static_cast<qint64>(std::llround(extras.at(0)));
	}
	if (extras.size() >= 2) {
		face->surfaceFlags = static_cast<qint64>(std::llround(extras.at(1)));
	}
	if (extras.size() >= 3) {
		face->surfaceValue = static_cast<qint64>(std::llround(extras.at(2)));
	}
}

bool readFacePoints(const QVector<MapToken>& tokens, int* index, int limit, LevelMapBrushFace* face, const LevelMapLoadRequest* request = nullptr)
{
	LevelMapVec3* targets[3] = {&face->p0, &face->p1, &face->p2};
	for (auto& target : targets) {
		QVector<double> values;
		if (!readNumberGroup(tokens, index, limit, &values, request) || values.size() != 3) {
			return false;
		}
		*target = {values.at(0), values.at(1), values.at(2), true};
	}
	return true;
}

struct QuakeParseState {
	const LevelMapLoadRequest* request = nullptr;
	int lastCheckpoint = -1024;
	LevelMapDocument* document = nullptr;
	QVector<MapToken> tokens;
	int index = 0;
	bool sawBrushPrimitive = false;
	bool sawPatchPrimitive = false;
	bool sawShaderPath = false;
	bool sawQ3Key = false;
	bool sawRadiantHeader = false;
};

void recordTexture(QuakeParseState* state, LevelMapBrush* brush, const QString& name)
{
	if (name.trimmed().isEmpty()) {
		return;
	}
	if (brush) {
		brush->textureNames.push_back(name);
	}
	state->document->textureReferences.push_back(name);
	if (name.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
		state->sawShaderPath = true;
	}
}

// Classic Quake/Quake II faces and Valve 220 faces live in the same block and
// are distinguished by the `[` that opens the explicit U axis.
bool parseClassicBrushBody(QuakeParseState* state, LevelMapBrush* brush, int limit)
{
	bool ok = true;
	while (state->index < limit) {
		if (state->index - state->lastCheckpoint >= 1024) {
			loadCheckpoint(state->request, LevelMapLoadPhase::Parsing, state->index, state->tokens.size());
			state->lastCheckpoint = state->index;
		}
		const MapToken& token = tokenAt(state->tokens, state->index);
		if (tokenIsPunct(token, QLatin1Char('}'))) {
			break;
		}
		LevelMapBrushFace face;
		face.id = brush->faces.size();
		face.line = token.line;
		if (!readFacePoints(state->tokens, &state->index, limit, &face, state->request)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brush-face-parse"), QCoreApplication::translate("VibeStudioLevelMap", "Brush face could not be parsed."), brushObjectId(brush->id), token.line);
			ok = false;
			break;
		}
		if (!readShaderName(state->tokens, &state->index, limit, &face.textureName)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brush-face-texture"), QCoreApplication::translate("VibeStudioLevelMap", "Brush face has no texture name."), brushObjectId(brush->id), token.line);
			ok = false;
			break;
		}
		if (tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('['))) {
			QVector<double> uValues;
			QVector<double> vValues;
			if (!readBracketGroup(state->tokens, &state->index, limit, &uValues, state->request) || uValues.size() != 4
				|| !readBracketGroup(state->tokens, &state->index, limit, &vValues, state->request) || vValues.size() != 4) {
				addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brush-face-valve220"), QCoreApplication::translate("VibeStudioLevelMap", "Valve 220 texture axes could not be parsed."), brushObjectId(brush->id), token.line);
				ok = false;
				break;
			}
			face.explicitTextureAxes = true;
			face.uAxis = {uValues.at(0), uValues.at(1), uValues.at(2), true};
			face.uOffset = uValues.at(3);
			face.vAxis = {vValues.at(0), vValues.at(1), vValues.at(2), true};
			face.vOffset = vValues.at(3);
			face.shiftX = face.uOffset;
			face.shiftY = face.vOffset;
			if (brush->primitiveKind.isEmpty() || brush->primitiveKind == QStringLiteral("classic")) {
				brush->primitiveKind = QStringLiteral("valve220");
			}
			QVector<double> placement;
			while (state->index < limit && placement.size() < 3) {
				if (state->index - state->lastCheckpoint >= 1024) {
					loadCheckpoint(state->request, LevelMapLoadPhase::Parsing, state->index, state->tokens.size());
					state->lastCheckpoint = state->index;
				}
				double value = 0.0;
				if (!tokenNumber(tokenAt(state->tokens, state->index), &value)) {
					break;
				}
				placement.push_back(value);
				++state->index;
			}
			if (placement.size() >= 1) {
				face.rotation = placement.at(0);
			}
			if (placement.size() >= 2) {
				face.scaleX = placement.at(1);
			}
			if (placement.size() >= 3) {
				face.scaleY = placement.at(2);
			}
		} else {
			QVector<double> placement;
			while (state->index < limit && placement.size() < 5) {
				if (state->index - state->lastCheckpoint >= 1024) {
					loadCheckpoint(state->request, LevelMapLoadPhase::Parsing, state->index, state->tokens.size());
					state->lastCheckpoint = state->index;
				}
				double value = 0.0;
				if (!tokenNumber(tokenAt(state->tokens, state->index), &value)) {
					break;
				}
				placement.push_back(value);
				++state->index;
			}
			if (placement.size() < 5) {
				addIssue(state->document, LevelMapIssueSeverity::Warning, QStringLiteral("brush-face-placement"), QCoreApplication::translate("VibeStudioLevelMap", "Brush face is missing texture placement numbers."), brushObjectId(brush->id), token.line);
			}
			face.shiftX = placement.value(0, 0.0);
			face.shiftY = placement.value(1, 0.0);
			face.rotation = placement.value(2, 0.0);
			face.scaleX = placement.value(3, 1.0);
			face.scaleY = placement.value(4, 1.0);
			if (brush->primitiveKind.isEmpty()) {
				brush->primitiveKind = QStringLiteral("classic");
			}
		}
		readOptionalFlags(state->tokens, &state->index, limit, &face);
		recordTexture(state, brush, face.textureName);
		brush->faces.push_back(face);
	}
	if (brush->primitiveKind.isEmpty()) {
		brush->primitiveKind = QStringLiteral("classic");
	}
	return ok;
}

// Quake III brush primitives: three points, a 2x3 texture matrix, then the
// shader and its `contents surface value` integers. The matrix is texture data,
// so it must never be folded into the brush bounds.
bool parseBrushDefBody(QuakeParseState* state, LevelMapBrush* brush, int limit)
{
	brush->primitiveKind = QStringLiteral("brushDef");
	while (state->index < limit) {
		if (state->index - state->lastCheckpoint >= 1024) {
			loadCheckpoint(state->request, LevelMapLoadPhase::Parsing, state->index, state->tokens.size());
			state->lastCheckpoint = state->index;
		}
		const MapToken& token = tokenAt(state->tokens, state->index);
		if (tokenIsPunct(token, QLatin1Char('}'))) {
			break;
		}
		LevelMapBrushFace face;
		face.id = brush->faces.size();
		face.line = token.line;
		if (!readFacePoints(state->tokens, &state->index, limit, &face, state->request)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef-face-parse"), QCoreApplication::translate("VibeStudioLevelMap", "brushDef face points could not be parsed."), brushObjectId(brush->id), token.line);
			return false;
		}
		if (!readTextureMatrix(state->tokens, &state->index, limit, &face.textureMatrix, state->request)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef-texture-matrix"), QCoreApplication::translate("VibeStudioLevelMap", "brushDef texture matrix could not be parsed."), brushObjectId(brush->id), token.line);
			return false;
		}
		face.explicitTextureMatrix = true;
		if (!readShaderName(state->tokens, &state->index, limit, &face.textureName)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef-shader"), QCoreApplication::translate("VibeStudioLevelMap", "brushDef face has no shader name."), brushObjectId(brush->id), token.line);
			return false;
		}
		readOptionalFlags(state->tokens, &state->index, limit, &face);
		recordTexture(state, brush, face.textureName);
		brush->faces.push_back(face);
	}
	return true;
}

// idTech4 / GtkRadiant `brushDef3`: `( nx ny nz d )` is the plane
// nx*x + ny*y + nz*z + d = 0 with an outward-facing normal. VibeStudio keeps the
// plane verbatim and synthesizes three points on it so that the shared
// convex-polytope solver (core/map_geometry) can still build the brush.
bool parseBrushDef3Body(QuakeParseState* state, LevelMapBrush* brush, int limit)
{
	brush->primitiveKind = QStringLiteral("brushDef3");
	while (state->index < limit) {
		if (state->index - state->lastCheckpoint >= 1024) {
			loadCheckpoint(state->request, LevelMapLoadPhase::Parsing, state->index, state->tokens.size());
			state->lastCheckpoint = state->index;
		}
		const MapToken& token = tokenAt(state->tokens, state->index);
		if (tokenIsPunct(token, QLatin1Char('}'))) {
			break;
		}
		QVector<double> plane;
		if (!readNumberGroup(state->tokens, &state->index, limit, &plane, state->request) || plane.size() != 4) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-plane"), QCoreApplication::translate("VibeStudioLevelMap", "brushDef3 plane could not be parsed."), brushObjectId(brush->id), token.line);
			return false;
		}
		LevelMapBrushFace face;
		face.id = brush->faces.size();
		face.line = token.line;
		face.explicitPlane = true;
		const double length = std::sqrt((plane.at(0) * plane.at(0)) + (plane.at(1) * plane.at(1)) + (plane.at(2) * plane.at(2)));
		if (length < 1e-9) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-degenerate-plane"), QCoreApplication::translate("VibeStudioLevelMap", "brushDef3 plane has a zero normal."), brushObjectId(brush->id), token.line);
			return false;
		}
		const double nx = plane.at(0) / length;
		const double ny = plane.at(1) / length;
		const double nz = plane.at(2) / length;
		face.planeNormal = {nx, ny, nz, true};
		face.planeDistance = plane.at(3) / length;
		if (!readTextureMatrix(state->tokens, &state->index, limit, &face.textureMatrix, state->request)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-texture-matrix"), QCoreApplication::translate("VibeStudioLevelMap", "brushDef3 texture matrix could not be parsed."), brushObjectId(brush->id), token.line);
			return false;
		}
		face.explicitTextureMatrix = true;
		if (!readShaderName(state->tokens, &state->index, limit, &face.textureName)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-shader"), QCoreApplication::translate("VibeStudioLevelMap", "brushDef3 face has no shader name."), brushObjectId(brush->id), token.line);
			return false;
		}
		readOptionalFlags(state->tokens, &state->index, limit, &face);

		// Build an orthonormal basis (a, b) on the plane with a x b == normal, so
		// that (p0 - p1) x (p2 - p1) reproduces the same outward normal that
		// ericw-tools `PlaneFromPoints` expects.
		const double absX = std::abs(nx);
		const double absY = std::abs(ny);
		const double absZ = std::abs(nz);
		LevelMapVec3 reference {0.0, 0.0, 1.0, true};
		if (absZ <= absX && absZ <= absY) {
			reference = {0.0, 0.0, 1.0, true};
		} else if (absY <= absX) {
			reference = {0.0, 1.0, 0.0, true};
		} else {
			reference = {1.0, 0.0, 0.0, true};
		}
		LevelMapVec3 axisA {
			(ny * reference.z) - (nz * reference.y),
			(nz * reference.x) - (nx * reference.z),
			(nx * reference.y) - (ny * reference.x),
			true,
		};
		const double axisLength = std::sqrt((axisA.x * axisA.x) + (axisA.y * axisA.y) + (axisA.z * axisA.z));
		if (axisLength < 1e-9) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-degenerate-plane"), QCoreApplication::translate("VibeStudioLevelMap", "brushDef3 plane basis could not be built."), brushObjectId(brush->id), token.line);
			return false;
		}
		axisA.x /= axisLength;
		axisA.y /= axisLength;
		axisA.z /= axisLength;
		const LevelMapVec3 axisB {
			(ny * axisA.z) - (nz * axisA.y),
			(nz * axisA.x) - (nx * axisA.z),
			(nx * axisA.y) - (ny * axisA.x),
			true,
		};
		const double offset = -face.planeDistance;
		const LevelMapVec3 center {nx * offset, ny * offset, nz * offset, true};
		constexpr double kSpan = 64.0;
		face.p1 = center;
		face.p0 = {center.x + (axisA.x * kSpan), center.y + (axisA.y * kSpan), center.z + (axisA.z * kSpan), true};
		face.p2 = {center.x + (axisB.x * kSpan), center.y + (axisB.y * kSpan), center.z + (axisB.z * kSpan), true};

		recordTexture(state, brush, face.textureName);
		brush->faces.push_back(face);
	}
	return true;
}

bool parsePatchBody(QuakeParseState* state, LevelMapPatch* patch, bool patchDef3, int limit)
{
	if (!tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('{'))) {
		return false;
	}
	++state->index;
	patch->textureLine = tokenAt(state->tokens, state->index).line;
	patch->textureColumn = tokenAt(state->tokens, state->index).column;
	if (!readShaderName(state->tokens, &state->index, limit, &patch->textureName)) {
		return false;
	}
	QVector<double> header;
	if (!readNumberGroup(state->tokens, &state->index, limit, &header, state->request) || header.size() < 2) {
		return false;
	}
	patch->width = static_cast<int>(std::lround(header.at(0)));
	patch->height = static_cast<int>(std::lround(header.at(1)));
	patch->headerTail = header.mid(patchDef3 ? 4 : 2);
	if (patchDef3 && header.size() >= 4) {
		patch->fixedSubdivisions = true;
		patch->subdivisionsX = static_cast<int>(std::lround(header.at(2)));
		patch->subdivisionsY = static_cast<int>(std::lround(header.at(3)));
	}
	if (!tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('('))) {
		return false;
	}
	++state->index;
	// A `.map` patch body is laid out width-major: `width` parenthesised source
	// groups of `height` points each. q3map2's ParsePatch walks
	// `for (j < m.width) { for (i < m.height) ... m[i][j] }` and `mesh_t`
	// strides by width, so the reference parser transposes the file into a
	// row-major `[height][width]` grid. See
	// external/compilers/q3map2-nrc/tools/quake3/q3map2/patch.cpp (ParsePatch)
	// and https://quakewiki.org/wiki/Quake_Map_Format#Patches. Collect each
	// source group as a grid column and transpose so controlPoints matches the
	// `[row * width + column]` layout tessellatePatchMesh expects.
	QVector<QVector<LevelMapVec3>> columns;
	QVector<QVector<double>> columnsU;
	QVector<QVector<double>> columnsV;
	while (state->index < limit && tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('('))) {
		if (state->index - state->lastCheckpoint >= 1024) {
			loadCheckpoint(state->request, LevelMapLoadPhase::Parsing, state->index, state->tokens.size());
			state->lastCheckpoint = state->index;
		}
		const int rowStart = state->index;
		// Remember which source line opened this control group so a moved patch
		// can be written back into the same line rather than re-emitted. One
		// entry per grid column, in file order.
		patch->controlRowLines.push_back(tokenAt(state->tokens, rowStart).line);
		++state->index;
		bool rowIsPointList = tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('('));
		if (!rowIsPointList) {
			patch->controlRowLines.removeLast();
			state->index = rowStart;
			break;
		}
		QVector<LevelMapVec3> column;
		QVector<double> columnU;
		QVector<double> columnV;
		while (state->index < limit && tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('('))) {
			if (state->index - state->lastCheckpoint >= 1024) {
				loadCheckpoint(state->request, LevelMapLoadPhase::Parsing, state->index, state->tokens.size());
				state->lastCheckpoint = state->index;
			}
			QVector<double> values;
			if (!readNumberGroup(state->tokens, &state->index, limit, &values, state->request) || values.size() < 3) {
				return false;
			}
			const LevelMapVec3 point {values.at(0), values.at(1), values.at(2), true};
			column.push_back(point);
			columnU.push_back(values.value(3, 0.0));
			columnV.push_back(values.value(4, 0.0));
			includePoint(&patch->mins, &patch->maxs, point);
		}
		if (!tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char(')'))) {
			return false;
		}
		++state->index;
		columns.push_back(column);
		columnsU.push_back(columnU);
		columnsV.push_back(columnV);
	}
	if (!tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char(')'))) {
		return false;
	}
	++state->index;

	const int width = patch->width;
	const int height = patch->height;
	bool rectangular = width > 0 && height > 0 && columns.size() == static_cast<qsizetype>(width);
	if (rectangular) {
		for (const QVector<LevelMapVec3>& column : columns) {
			if (column.size() != static_cast<qsizetype>(height)) {
				rectangular = false;
				break;
			}
		}
	}
	patch->controlGridNormalized = rectangular;
	if (rectangular) {
		const qsizetype total = static_cast<qsizetype>(width) * static_cast<qsizetype>(height);
		patch->controlPoints.resize(total);
		patch->controlU.resize(total);
		patch->controlV.resize(total);
		for (int column = 0; column < width; ++column) {
			for (int row = 0; row < height; ++row) {
				const qsizetype target = (static_cast<qsizetype>(row) * width) + column;
				patch->controlPoints[target] = columns.at(column).at(row);
				patch->controlU[target] = columnsU.at(column).at(row);
				patch->controlV[target] = columnsV.at(column).at(row);
			}
		}
	} else {
		// Ragged or header-mismatched grid: keep file order so nothing is lost.
		// The `patch-control-count` warning raised by the caller reports it.
		for (qsizetype index = 0; index < columns.size(); ++index) {
			patch->controlPoints += columns.at(index);
			patch->controlU += columnsU.at(index);
			patch->controlV += columnsV.at(index);
		}
	}
	return true;
}

void parsePrimitiveBlock(QuakeParseState* state, LevelMapEntity* entity, QVector<LevelMapBrush>* brushes, QVector<LevelMapPatch>* patches)
{
	const int openIndex = state->index;
	const int closeIndex = matchingBraceIndex(state->tokens, openIndex, state->request);
	const int startLine = tokenAt(state->tokens, openIndex).line;
	const int limit = closeIndex >= 0 ? closeIndex : state->tokens.size();
	++state->index;

	const MapToken& first = tokenAt(state->tokens, state->index);
	const QString keyword = first.type == MapTokenType::Word ? first.text.toLower() : QString();

	if (keyword == QStringLiteral("patchdef2") || keyword == QStringLiteral("patchdef3")) {
		state->sawPatchPrimitive = true;
		++state->index;
		LevelMapPatch patch;
		patch.id = state->document->patches.size() + patches->size();
		patch.entityId = entity->id;
		patch.startLine = startLine;
		patch.endLine = closeIndex >= 0 ? tokenAt(state->tokens, closeIndex).line : startLine;
		if (parsePatchBody(state, &patch, keyword == QStringLiteral("patchdef3"), limit)) {
			if (!patch.textureName.trimmed().isEmpty()) {
				state->document->textureReferences.push_back(patch.textureName);
				if (patch.textureName.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)) {
					state->sawShaderPath = true;
				}
			}
			const int expected = patch.width * patch.height;
			if (expected > 0 && patch.controlPoints.size() != expected) {
				addIssue(state->document, LevelMapIssueSeverity::Warning, QStringLiteral("patch-control-count"),
					QCoreApplication::translate("VibeStudioLevelMap", "Patch declares %1x%2 control points but %3 were parsed.").arg(patch.width).arg(patch.height).arg(patch.controlPoints.size()),
					patchObjectId(patch.id), startLine);
			}
			patches->push_back(patch);
		} else {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("patch-parse"), QCoreApplication::translate("VibeStudioLevelMap", "Patch definition could not be parsed."), patchObjectId(patch.id), startLine);
		}
	} else if (keyword == QStringLiteral("terraindef")) {
		++state->index;
		addIssue(state->document, LevelMapIssueSeverity::Info, QStringLiteral("terraindef-skipped"),
			QCoreApplication::translate("VibeStudioLevelMap", "terrainDef block was skipped; VibeStudio does not edit terrain primitives yet."), entityObjectId(entity->id), startLine);
	} else {
		LevelMapBrush brush;
		brush.id = state->document->brushes.size() + brushes->size();
		brush.entityId = entity->id;
		brush.startLine = startLine;
		brush.endLine = closeIndex >= 0 ? tokenAt(state->tokens, closeIndex).line : startLine;
		bool parsed = false;
		if (keyword == QStringLiteral("brushdef")) {
			state->sawBrushPrimitive = true;
			++state->index;
			if (tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('{'))) {
				++state->index;
				parsed = parseBrushDefBody(state, &brush, limit);
			}
		} else if (keyword == QStringLiteral("brushdef3")) {
			state->sawBrushPrimitive = true;
			++state->index;
			if (tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('{'))) {
				++state->index;
				parsed = parseBrushDef3Body(state, &brush, limit);
			}
		} else {
			parsed = parseClassicBrushBody(state, &brush, limit);
		}
		Q_UNUSED(parsed);
		brush.faceCount = brush.faces.size();
		brushes->push_back(brush);
	}

	state->index = closeIndex >= 0 ? closeIndex + 1 : state->tokens.size();
}

void parseEntityBlock(QuakeParseState* state)
{
	const int openIndex = state->index;
	const int closeIndex = matchingBraceIndex(state->tokens, openIndex, state->request);
	LevelMapEntity entity;
	entity.id = state->document->entities.size();
	entity.startLine = tokenAt(state->tokens, openIndex).line;
	entity.endLine = closeIndex >= 0 ? tokenAt(state->tokens, closeIndex).line : 0;
	const int limit = closeIndex >= 0 ? closeIndex : state->tokens.size();
	++state->index;

	QVector<LevelMapBrush> brushes;
	QVector<LevelMapPatch> patches;
	while (state->index < limit) {
		if (state->index - state->lastCheckpoint >= 1024) {
			loadCheckpoint(state->request, LevelMapLoadPhase::Parsing, state->index, state->tokens.size());
			state->lastCheckpoint = state->index;
		}
		const MapToken& token = tokenAt(state->tokens, state->index);
		if (token.type == MapTokenType::String) {
			const QString key = token.text;
			const int line = token.line;
			++state->index;
			QString value;
			const MapToken& valueToken = tokenAt(state->tokens, state->index);
			if (valueToken.type == MapTokenType::String) {
				value = valueToken.text;
				++state->index;
			} else {
				addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("entity-key-missing-value"), QCoreApplication::translate("VibeStudioLevelMap", "Entity key has no value."), entityObjectId(entity.id), line);
			}
			entity.properties.push_back({key, value, line});
			if (key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
				entity.className = value;
			} else if (key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
				entity.origin = parseVec3(value);
			}
			if (key.startsWith(QStringLiteral("_q3map"), Qt::CaseInsensitive)) {
				state->sawQ3Key = true;
			}
			continue;
		}
		if (tokenIsPunct(token, QLatin1Char('{'))) {
			parsePrimitiveBlock(state, &entity, &brushes, &patches);
			continue;
		}
		addIssue(state->document, LevelMapIssueSeverity::Warning, QStringLiteral("unexpected-token"), QCoreApplication::translate("VibeStudioLevelMap", "Unexpected token in entity body: %1").arg(token.text), entityObjectId(entity.id), token.line);
		++state->index;
	}

	if (closeIndex < 0) {
		addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("unterminated-entity"), QCoreApplication::translate("VibeStudioLevelMap", "Map ended before an entity closed."), entityObjectId(entity.id), entity.startLine);
	}
	if (entity.className.isEmpty()) {
		entity.className = QCoreApplication::translate("VibeStudioLevelMap", "entity");
		addIssue(state->document, LevelMapIssueSeverity::Warning, QStringLiteral("entity-missing-classname"), QCoreApplication::translate("VibeStudioLevelMap", "Entity is missing a classname."), entityObjectId(entity.id), entity.startLine);
	}
	state->document->entities.push_back(entity);
	for (const LevelMapBrush& brush : brushes) {
		state->document->brushes.push_back(brush);
	}
	for (const LevelMapPatch& patch : patches) {
		state->document->patches.push_back(patch);
	}
	state->index = closeIndex >= 0 ? closeIndex + 1 : state->tokens.size();
}

void solveBrushBounds(LevelMapDocument* document, const LevelMapLoadRequest* request = nullptr)
{
	auto* cache = request ? request->brushGeometryCache : nullptr;
	if (cache) { cache->beginBuild(*document); }
	const auto cancel = request ? request->isCancelled : std::function<bool()>();
	qint64 completed = 0;
	for (LevelMapBrush& brush : document->brushes) {
		if ((completed++ & 15) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Solving, completed - 1, document->brushes.size()); }
		brush.faceCount = brush.faces.size();
		if (brush.faces.isEmpty()) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("empty-brush"), QCoreApplication::translate("VibeStudioLevelMap", "Brush has no parsed faces."), brushObjectId(brush.id), brush.startLine);
			continue;
		}
		const MapBrushGeometry geometry = cache ? cache->resolve(brush, MapGeometryPrecision::CompilerCompatible, cancel)
			: solveBrushGeometry(brush.faces, brush.id, brush.entityId, MapGeometryPrecision::CompilerCompatible, cancel);
		if (geometry.cancelled) { throw MapLoadCancelled {}; }
		if (geometry.solved && geometry.mins.valid && geometry.maxs.valid) {
			brush.mins = geometry.mins;
			brush.maxs = geometry.maxs;
			brush.boundsSolved = true;
			continue;
		}
		// Fall back to the raw plane-point cloud so the viewport still has a hint,
		// but leave `boundsSolved` false so callers know it is not a real volume.
		brush.boundsSolved = false;
		for (const LevelMapBrushFace& face : brush.faces) {
			includePoint(&brush.mins, &brush.maxs, face.p0);
			includePoint(&brush.mins, &brush.maxs, face.p1);
			includePoint(&brush.mins, &brush.maxs, face.p2);
		}
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("brush-degenerate"),
			QCoreApplication::translate("VibeStudioLevelMap", "Brush could not be solved into a closed convex volume."), brushObjectId(brush.id), brush.startLine);
	}
}

void parseQuakeMapText(const QString& text, const QString& sourcePath, const QString& engineHint, LevelMapDocument* document, const LevelMapLoadRequest* request = nullptr);

// Parses .map text, from a file or the clipboard, into `document`.
void parseQuakeMapText(const QString& text, const QString& sourcePath, const QString& engineHint, LevelMapDocument* document, const LevelMapLoadRequest* request)
{
	document->sourcePath = QFileInfo(sourcePath).absoluteFilePath();
	document->mapName = QFileInfo(sourcePath).fileName();
	document->originalText = text;
	document->lineEnding = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
	document->textLines = loadTextLines(text, request);
	document->editState = QStringLiteral("clean");

	QStringList comments;
	QuakeParseState state;
	state.document = document;
	state.request = request;
	state.tokens = tokenizeMapText(text, &comments, request);
	loadCheckpoint(request, LevelMapLoadPhase::Parsing, 0, state.tokens.size());
	for (const QString& comment : comments) {
		if (comment.contains(QStringLiteral("Q3Radiant"), Qt::CaseInsensitive)
			|| comment.contains(QStringLiteral("GtkRadiant"), Qt::CaseInsensitive)
			|| comment.contains(QStringLiteral("NetRadiant"), Qt::CaseInsensitive)) {
			state.sawRadiantHeader = true;
		}
	}

	while (state.index < state.tokens.size()) {
		if ((state.index & 1023) == 0) { loadCheckpoint(request, LevelMapLoadPhase::Parsing, state.index, state.tokens.size()); }
		const MapToken& token = tokenAt(state.tokens, state.index);
		if (tokenIsPunct(token, QLatin1Char('{'))) {
			parseEntityBlock(&state);
			continue;
		}
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("unexpected-token"), QCoreApplication::translate("VibeStudioLevelMap", "Unexpected token outside an entity: %1").arg(token.text), document->mapName, token.line);
		++state.index;
	}

	// Engine family: an explicit hint wins, otherwise use real evidence rather
	// than a substring guess over the whole file.
	const QString hint = normalizedId(engineHint);
	bool quake3 = false;
	if (!hint.isEmpty()) {
		quake3 = hint.contains(QStringLiteral("3"));
	} else {
		quake3 = text.section(QLatin1Char('\n'), 0, 0).trimmed() != QString::fromLatin1(kQuake2MapTargetHeader).trimmed()
			&& (state.sawBrushPrimitive || state.sawPatchPrimitive || state.sawQ3Key || state.sawRadiantHeader || state.sawShaderPath);
	}
	document->format = quake3 ? LevelMapFormat::Quake3Map : LevelMapFormat::QuakeMap;
	document->engineFamily = quake3 ? QStringLiteral("idTech3") : QStringLiteral("idTech2");

	loadCheckpoint(request, LevelMapLoadPhase::Solving, 0, document->brushes.size());
	solveBrushBounds(document, request);
	loadCheckpoint(request, LevelMapLoadPhase::Validating);

	if (document->entities.isEmpty()) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("empty-map"), QCoreApplication::translate("VibeStudioLevelMap", ".map file contains no parsed entities."), document->mapName);
	}
	bool hasWorldspawn = false;
	for (const LevelMapEntity& entity : document->entities) {
		hasWorldspawn = hasWorldspawn || entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
	}
	if (!hasWorldspawn) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("missing-worldspawn"), QCoreApplication::translate("VibeStudioLevelMap", "Map has no worldspawn entity."), document->mapName);
	}

	EricwMapPreflightOptions preflightOptions;
	preflightOptions.mapPath = document->sourcePath;
	if (request) {
		preflightOptions.isCancelled = request->isCancelled;
		preflightOptions.progress = [request](qint64 done, qint64 total) { loadCheckpoint(request, LevelMapLoadPhase::Validating, done, total); };
	}
	const EricwMapPreflightResult preflight = validateEricwMapPreflightText(text, preflightOptions);
	if (preflight.cancelled) { throw MapLoadCancelled {}; }
	for (const EricwMapPreflightWarning& warning : preflight.warnings) {
		// Fractional vertices are normal for q3map2 radial brushes. This
		// particular upstream advisory concerns qbsp, not the Quake III target.
		if (quake3 && warning.code == QStringLiteral("non-integer-brush-coordinate")) { continue; }
		LevelMapIssueSeverity severity = LevelMapIssueSeverity::Warning;
		if (warning.severity == EricwMapPreflightSeverity::Info) {
			severity = LevelMapIssueSeverity::Info;
		} else if (warning.severity == EricwMapPreflightSeverity::Error) {
			severity = LevelMapIssueSeverity::Error;
		}
		QString objectId;
		if (warning.entityIndex >= 0) {
			objectId = entityObjectId(warning.entityIndex);
		} else if (!warning.classname.isEmpty()) {
			objectId = warning.classname;
		}
		addIssue(document, severity, warning.code.isEmpty() ? QStringLiteral("compiler-preflight") : warning.code, warning.message, objectId, warning.line);
	}
}

// ---------------------------------------------------------------------------
// Write-back
// ---------------------------------------------------------------------------

bool fitsDoomCoordinate(double value)
{
	const long rounded = std::lround(value);
	return rounded >= kDoomCoordinateMin && rounded <= kDoomCoordinateMax;
}

QByteArray doomVertexBytes(const QVector<LevelMapDoomVertex>& vertices)
{
	QByteArray bytes;
	for (const LevelMapDoomVertex& vertex : vertices) {
		appendLe16(&bytes, static_cast<qint16>(std::lround(vertex.x)));
		appendLe16(&bytes, static_cast<qint16>(std::lround(vertex.y)));
	}
	return bytes;
}

QByteArray doomLinedefBytes(const QVector<LevelMapDoomLinedef>& linedefs, bool hexen)
{
	QByteArray bytes;
	for (const LevelMapDoomLinedef& linedef : linedefs) {
		appendLe16(&bytes, static_cast<qint16>(linedef.startVertex));
		appendLe16(&bytes, static_cast<qint16>(linedef.endVertex));
		appendLe16(&bytes, static_cast<qint16>(linedef.flags));
		if (hexen) {
			appendByte(&bytes, linedef.special);
			for (int arg = 0; arg < 5; ++arg) {
				appendByte(&bytes, linedef.args[static_cast<size_t>(arg)]);
			}
		} else {
			appendLe16(&bytes, static_cast<qint16>(linedef.special));
			appendLe16(&bytes, static_cast<qint16>(linedef.tag));
		}
		appendLe16(&bytes, static_cast<qint16>(linedef.frontSidedef < 0 ? 0xffff : linedef.frontSidedef));
		appendLe16(&bytes, static_cast<qint16>(linedef.backSidedef < 0 ? 0xffff : linedef.backSidedef));
	}
	return bytes;
}

QByteArray doomThingBytes(const QVector<LevelMapDoomThing>& things, bool hexen)
{
	QByteArray bytes;
	for (const LevelMapDoomThing& thing : things) {
		if (hexen) {
			appendLe16(&bytes, static_cast<qint16>(thing.tid));
			appendLe16(&bytes, static_cast<qint16>(std::lround(thing.x)));
			appendLe16(&bytes, static_cast<qint16>(std::lround(thing.y)));
			appendLe16(&bytes, static_cast<qint16>(std::lround(thing.z)));
			appendLe16(&bytes, static_cast<qint16>(thing.angle));
			appendLe16(&bytes, static_cast<qint16>(thing.type));
			appendLe16(&bytes, static_cast<qint16>(thing.flags));
			appendByte(&bytes, thing.special);
			for (int arg = 0; arg < 5; ++arg) {
				appendByte(&bytes, thing.args[static_cast<size_t>(arg)]);
			}
			continue;
		}
		appendLe16(&bytes, static_cast<qint16>(std::lround(thing.x)));
		appendLe16(&bytes, static_cast<qint16>(std::lround(thing.y)));
		appendLe16(&bytes, static_cast<qint16>(thing.angle));
		appendLe16(&bytes, static_cast<qint16>(thing.type));
		appendLe16(&bytes, static_cast<qint16>(thing.flags));
	}
	return bytes;
}

QByteArray doomSidedefBytes(const QVector<LevelMapDoomSidedef>& sidedefs)
{
	QByteArray bytes;
	for (const LevelMapDoomSidedef& sidedef : sidedefs) {
		appendLe16(&bytes, static_cast<qint16>(sidedef.offsetX));
		appendLe16(&bytes, static_cast<qint16>(sidedef.offsetY));
		bytes.append(fixedLatin1Bytes(sidedef.upperTexture.isEmpty() ? QStringLiteral("-") : sidedef.upperTexture, 8));
		bytes.append(fixedLatin1Bytes(sidedef.lowerTexture.isEmpty() ? QStringLiteral("-") : sidedef.lowerTexture, 8));
		bytes.append(fixedLatin1Bytes(sidedef.middleTexture.isEmpty() ? QStringLiteral("-") : sidedef.middleTexture, 8));
		appendLe16(&bytes, static_cast<qint16>(sidedef.sector));
	}
	return bytes;
}

QByteArray doomSectorBytes(const QVector<LevelMapDoomSector>& sectors)
{
	QByteArray bytes;
	for (const LevelMapDoomSector& sector : sectors) {
		appendLe16(&bytes, static_cast<qint16>(sector.floorHeight));
		appendLe16(&bytes, static_cast<qint16>(sector.ceilingHeight));
		bytes.append(fixedLatin1Bytes(sector.floorTexture, 8));
		bytes.append(fixedLatin1Bytes(sector.ceilingTexture, 8));
		appendLe16(&bytes, static_cast<qint16>(sector.lightLevel));
		appendLe16(&bytes, static_cast<qint16>(sector.special));
		appendLe16(&bytes, static_cast<qint16>(sector.tag));
	}
	return bytes;
}

bool validateDoomWriteRanges(const LevelMapDocument& document, QStringList* errors)
{
	bool ok = true;
	const auto nativeInteger = [](double value) { return fitsDoomCoordinate(value) && value == std::trunc(value); };
	for (const LevelMapDoomVertex& vertex : document.doomVertices) {
		if (!fitsDoomCoordinate(vertex.x) || !fitsDoomCoordinate(vertex.y)) {
			errors->push_back(QCoreApplication::translate("VibeStudioLevelMap", "Vertex %1 is outside the Doom 16-bit coordinate range.").arg(vertex.id));
			ok = false;
		}
	}
	for (const LevelMapDoomThing& thing : document.doomThings) {
		if (!fitsDoomCoordinate(thing.x) || !fitsDoomCoordinate(thing.y) || !fitsDoomCoordinate(thing.z)) {
			errors->push_back(QCoreApplication::translate("VibeStudioLevelMap", "Thing %1 is outside the Doom 16-bit coordinate range.").arg(thing.id));
			ok = false;
		}
	}
	for (const LevelMapDoomSector& sector : document.doomSectors) {
		if (!nativeInteger(sector.floorHeight) || !nativeInteger(sector.ceilingHeight)) {
			errors->push_back(QCoreApplication::translate("VibeStudioLevelMap", "Sector %1 heights must be integers in the Doom 16-bit range.").arg(sector.id));
			ok = false;
		}
	}
	for (const auto& side : document.doomSidedefs) {
		if (!nativeInteger(side.offsetX) || !nativeInteger(side.offsetY)) {
			errors->push_back(QCoreApplication::translate("VibeStudioLevelMap", "Sidedef %1 offsets must be integers in the Doom 16-bit range.").arg(side.id));
			ok = false;
		}
	}
	return ok;
}

bool serializeDoomWad(const LevelMapDocument& document, QByteArray* output, QStringList* errors, QStringList* warnings, QStringList* staleLumps)
{
	QString readError;
	QVector<WadLump> originalLumps = document.doomArchiveLumps;
	if (originalLumps.isEmpty() && !document.sourcePath.isEmpty()) {
		originalLumps = readWadLumps(document.sourcePath, nullptr, &readError);
	}
	if (originalLumps.isEmpty()) {
		errors->push_back(readError.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "Unable to read the source WAD.") : readError);
		return false;
	}
	if (document.doomFormat != LevelMapDoomFormat::Udmf && !validateDoomWriteRanges(document, errors)) {
		return false;
	}

	const bool hexen = document.doomFormat == LevelMapDoomFormat::Hexen;
	const bool udmf = document.doomFormat == LevelMapDoomFormat::Udmf;
	bool inRequestedMap = false;
	QStringList presentStale;
	// The map's own data lumps as they are found, so any it lacks can be put
	// in afterwards: a map that held only things keeps the sectors drawn in it.
	const QStringList dataLumps {QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"), QStringLiteral("VERTEXES"),
		QStringLiteral("SECTORS")};
	int markerIndex = -1;
	QHash<QString, int> foundAt;
	for (int lumpIndex = 0; lumpIndex < originalLumps.size(); ++lumpIndex) {
		WadLump& lump = originalLumps[lumpIndex];
		if (isDoomMapMarkerAt(originalLumps, lumpIndex)) {
			inRequestedMap = lump.name.compare(document.mapName, Qt::CaseInsensitive) == 0;
			if (inRequestedMap) {
				if (markerIndex >= 0) {
					errors->push_back(QCoreApplication::translate("VibeStudioLevelMap", "The WAD contains duplicate map markers; saving would be ambiguous."));
					return false;
				}
				markerIndex = lumpIndex;
			}
			continue;
		}
		if (!inRequestedMap) {
			continue;
		}
		if (udmf) {
			if (lump.name == QStringLiteral("TEXTMAP")) { lump.bytes = document.doomLumps.value(QStringLiteral("TEXTMAP")); }
			if (lump.name == QStringLiteral("ENDMAP")) { inRequestedMap = false; }
			continue;
		}
		if (!isDoomMapLumpName(lump.name)) { inRequestedMap = false; continue; }
		if (dataLumps.contains(lump.name)) {
			foundAt.insert(lump.name, lumpIndex);
		}
		if (lump.name == QStringLiteral("VERTEXES")) {
			lump.bytes = doomVertexBytes(document.doomVertices);
		} else if (lump.name == QStringLiteral("LINEDEFS")) {
			lump.bytes = doomLinedefBytes(document.doomLinedefs, hexen);
		} else if (lump.name == QStringLiteral("THINGS")) {
			lump.bytes = doomThingBytes(document.doomThings, hexen);
		} else if (lump.name == QStringLiteral("SIDEDEFS")) {
			lump.bytes = doomSidedefBytes(document.doomSidedefs);
		} else if (lump.name == QStringLiteral("SECTORS")) {
			lump.bytes = doomSectorBytes(document.doomSectors);
		} else if (QStringList {QStringLiteral("SEGS"), QStringLiteral("SSECTORS"), QStringLiteral("NODES"), QStringLiteral("BLOCKMAP"), QStringLiteral("REJECT"), QStringLiteral("ZNODES")}.contains(lump.name)) {
			presentStale.push_back(lump.name);
		}
	}
	if (markerIndex < 0) {
		errors->push_back(QCoreApplication::translate("VibeStudioLevelMap", "The source archive does not contain the selected map."));
		return false;
	}
	if (!udmf) {
		int after = markerIndex;
		for (const QString& name : dataLumps) {
			if (foundAt.contains(name)) {
				after = std::max(after, foundAt.value(name));
				continue;
			}
			WadLump added;
			added.name = name;
			if (name == QStringLiteral("THINGS")) {
				added.bytes = doomThingBytes(document.doomThings, hexen);
			} else if (name == QStringLiteral("LINEDEFS")) {
				added.bytes = doomLinedefBytes(document.doomLinedefs, hexen);
			} else if (name == QStringLiteral("SIDEDEFS")) {
				added.bytes = doomSidedefBytes(document.doomSidedefs);
			} else if (name == QStringLiteral("VERTEXES")) {
				added.bytes = doomVertexBytes(document.doomVertices);
			} else {
				added.bytes = doomSectorBytes(document.doomSectors);
			}
			originalLumps.insert(after + 1, added);
			++after;
			for (auto found = foundAt.begin(); found != foundAt.end(); ++found) {
				if (found.value() >= after) {
					++found.value();
				}
			}
		}
	}
	// Scene data is map-local, after the contiguous native map records. Never
	// touch an identically named resource elsewhere in the archive.
	int sceneEnd = markerIndex + 1;
	QMap<QString, QByteArray> sceneLumps;
	QVector<int> sceneRecords;
	while (sceneEnd < originalLumps.size() && !isDoomMapMarkerAt(originalLumps, sceneEnd)
		&& (udmf ? originalLumps[sceneEnd].name != QStringLiteral("ENDMAP") : isDoomMapLumpName(originalLumps[sceneEnd].name))) {
		const auto& lump = originalLumps[sceneEnd];
		sceneLumps.insert(lump.name, lump.bytes);
		if (lump.name == QStringLiteral("VS_SCENE")) { sceneRecords << sceneEnd; }
		++sceneEnd;
	}
	if (sceneRecords.size() > 1) {
		errors->push_back(QCoreApplication::translate("LevelScene", "The selected map has duplicate VS_SCENE records; saving would be ambiguous."));
		return false;
	}
	QHash<QString, QString> emitted;
	const auto recordOrder = [&](const auto& records, LevelMapSelectionKind kind) {
		for (int index = 0; index < records.size(); ++index) { emitted.insert(levelMapSelectionRefId({kind, records[index].id}), levelMapSelectionRefId({kind, index})); }
	};
	recordOrder(document.doomThings, LevelMapSelectionKind::DoomThing);
	recordOrder(document.doomVertices, LevelMapSelectionKind::DoomVertex);
	recordOrder(document.doomLinedefs, LevelMapSelectionKind::DoomLinedef);
	recordOrder(document.doomSectors, LevelMapSelectionKind::DoomSector);
	QString sceneError;
	const auto metadata = encodeLevelScene(document, levelSceneDoomHash(sceneLumps), emitted, &sceneError);
	if (!sceneError.isEmpty()) { errors->push_back(sceneError); return false; }
	if (!sceneRecords.isEmpty()) { originalLumps.removeAt(sceneRecords.first()); --sceneEnd; }
	if (!metadata.isEmpty() || !document.scene.problem.isEmpty()) { originalLumps.insert(sceneEnd, {QStringLiteral("VS_SCENE"), metadata}); }
	if (!document.scene.problem.isEmpty()) { warnings->push_back(document.scene.problem); }
	if (document.doomGeometryChanged) {
		QString invalidationError;
		if (!invalidateLevelDoomNodeProducts(&originalLumps, markerIndex, &presentStale, &invalidationError)) {
			errors->push_back(invalidationError); return false;
		}
	}
	if (inspectLevelDoomNodes(document).needsBuild()) {
		*staleLumps = presentStale.isEmpty() ? (udmf ? QStringList{"ZNODES"} : QStringList{"SEGS", "SSECTORS", "NODES", "BLOCKMAP", "REJECT"}) : presentStale;
		warnings->push_back(QCoreApplication::translate("VibeStudioLevelMap", "Runtime nodes require rebuilding: %1. Obsolete derived node data is cleared when geometry is saved. Run a node builder before testing the map.").arg(staleLumps->join(QStringLiteral(", "))));
	}

	QByteArray data;
	const QString magic = document.doomWadMagic == QStringLiteral("IWAD") ? QStringLiteral("IWAD") : QStringLiteral("PWAD");
	data.append(magic.toLatin1());
	appendLe32(&data, originalLumps.size());
	appendLe32(&data, 0);
	QVector<qint32> offsets;
	for (const WadLump& lump : originalLumps) {
		if (lump.bytes.size() > kLevelMapMaxDocumentBytes - data.size()) {
			errors->push_back(QCoreApplication::translate("VibeStudioLevelMap", "The serialized WAD exceeds the 512 MiB document limit."));
			return false;
		}
		offsets.push_back(data.size());
		data.append(lump.bytes);
	}
	const qint32 directoryOffset = data.size();
	for (int index = 0; index < originalLumps.size(); ++index) {
		appendLe32(&data, offsets.at(index));
		appendLe32(&data, originalLumps.at(index).bytes.size());
		data.append(fixedLatin1Bytes(originalLumps.at(index).name, 8));
	}
	for (int i = 0; i < 4; ++i) {
		data[8 + i] = static_cast<char>((directoryOffset >> (i * 8)) & 0xff);
	}

	*output = std::move(data);
	return true;
}

QString leadingWhitespace(const QString& line)
{
	int index = 0;
	while (index < line.size() && (line.at(index) == QLatin1Char(' ') || line.at(index) == QLatin1Char('\t'))) {
		++index;
	}
	return line.left(index);
}

QString formatKeyLine(const QString& indent, const QString& key, const QString& value)
{
	return QStringLiteral("%1\"%2\" \"%3\"").arg(indent, key, value);
}

// The quoted strings on one source line, as the positions of their opening
// and closing quotes, up to any `//` comment.
struct QuotedSpan {
	qsizetype open = 0;
	qsizetype close = 0;
};

QVector<QuotedSpan> quotedSpansOnLine(const QString& line)
{
	QVector<QuotedSpan> spans;
	qsizetype index = 0;
	while (index < line.size()) {
		const QChar ch = line.at(index);
		if (ch == QLatin1Char('/') && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('/')) {
			break;
		}
		if (ch == QLatin1Char('"')) {
			const qsizetype close = line.indexOf(QLatin1Char('"'), index + 1);
			if (close < 0) {
				break;
			}
			spans.push_back({index, close});
			index = close + 1;
			continue;
		}
		++index;
	}
	return spans;
}

QString quotedText(const QString& line, const QuotedSpan& span)
{
	return line.mid(span.open + 1, span.close - span.open - 1);
}

// Sets the value of the `"key" "value"` pair on a source line in place,
// leaving the rest of the line alone: braces, other pairs, comments, and
// spacing. Compact files put a brace, or several pairs, on one line, and
// rewriting the whole line would drop them. Returns false when the line holds
// no such pair, for example when the value was written on the next line.
bool setQuotedValueOnLine(QString* line, const QString& key, const QString& value)
{
	const QVector<QuotedSpan> spans = quotedSpansOnLine(*line);
	// Pairs run key, value, key, value from the first string on the line.
	for (int pair = 0; pair + 1 < spans.size(); pair += 2) {
		const QuotedSpan& keySpan = spans.at(pair);
		const QuotedSpan& valueSpan = spans.at(pair + 1);
		if (quotedText(*line, keySpan) != key) {
			continue;
		}
		if (!line->mid(keySpan.close + 1, valueSpan.open - keySpan.close - 1).trimmed().isEmpty()) {
			return false;
		}
		if (quotedText(*line, valueSpan) != value) {
			line->replace(valueSpan.open + 1, valueSpan.close - valueSpan.open - 1, value);
		}
		return true;
	}
	return false;
}

// Removes every `"key" "value"` pair on a source line whose key is not in
// `kept`, with the spacing that separated it, so a removed key leaves a brace
// or a neighbouring pair on the same line intact.
void removeQuotedPairsExcept(QString* line, const QSet<QString>& kept)
{
	const QVector<QuotedSpan> spans = quotedSpansOnLine(*line);
	QVector<QPair<qsizetype, qsizetype>> removals;
	for (int pair = 0; pair + 1 < spans.size(); pair += 2) {
		if (!kept.contains(quotedText(*line, spans.at(pair)))) {
			removals.push_back({spans.at(pair).open, spans.at(pair + 1).close + 1});
		}
	}
	// Last first, so earlier positions stay valid.
	for (auto it = removals.crbegin(); it != removals.crend(); ++it) {
		qsizetype start = it->first;
		qsizetype end = it->second;
		while (end < line->size() && line->at(end).isSpace()) {
			++end;
		}
		if (end == line->size()) {
			while (start > 0 && line->at(start - 1).isSpace()) {
				--start;
			}
		}
		line->remove(start, end - start);
	}
}

// Writes a coordinate the way `.map` files do: an integral value stays an
// integer so an unmoved axis is byte-identical to what the file already had.
QString mapCoordinateText(double value)
{
	if (std::abs(value - std::round(value)) < 1.0e-6) {
		return QString::number(static_cast<long long>(std::llround(value)));
	}
	QString text = QString::number(value, 'f', 6);
	while (text.endsWith(QLatin1Char('0'))) {
		text.chop(1);
	}
	if (text.endsWith(QLatin1Char('.'))) {
		text.chop(1);
	}
	return text;
}

// Keep adjacent transformed planes coplanar when persisted. Six fractional
// digits can turn a rotated shared edge into a tiny gap or concavity. Surface
// controls still use their established six-digit mapCoordinateText policy.
QString mapGeometryCoordinateText(double value)
{
	if (std::abs(value - std::round(value)) < 1e-10) {
		return QString::number(static_cast<long long>(std::llround(value)));
	}
	QString text = QString::number(value, 'f', 12);
	while (text.endsWith(QLatin1Char('0'))) { text.chop(1); }
	if (text.endsWith(QLatin1Char('.'))) { text.chop(1); }
	return text;
}

// Replaces the first `count` parenthesised number groups in `line`, leaving the
// texture name, offsets, flags, and any brush-primitives matrix untouched. This
// keeps a moved brush's line as close to the original as the move allows.
QString replaceLeadingPointGroups(const QString& line, const QVector<LevelMapVec3>& points)
{
	static const QRegularExpression groupPattern(QStringLiteral(R"vs(\(\s*[-+0-9.eE]+\s+[-+0-9.eE]+\s+[-+0-9.eE]+\s*\))vs"));
	QString result = line;
	int searchFrom = 0;
	for (const LevelMapVec3& point : points) {
		const QRegularExpressionMatch match = groupPattern.match(result, searchFrom);
		if (!match.hasMatch()) {
			return line;
		}
		const QString replacement = QStringLiteral("( %1 %2 %3 )")
			.arg(mapGeometryCoordinateText(point.x), mapGeometryCoordinateText(point.y), mapGeometryCoordinateText(point.z));
		result.replace(match.capturedStart(), match.capturedLength(), replacement);
		searchFrom = match.capturedStart() + replacement.size();
	}
	return result;
}

// `brushDef3` faces are `( nx ny nz dist )`: translation leaves the normal alone
// and shifts the distance.
QString replacePlaneGroup(const QString& line, const LevelMapVec3& normal, double distance)
{
	static const QRegularExpression planePattern(QStringLiteral(R"vs(\(\s*[-+0-9.eE]+\s+[-+0-9.eE]+\s+[-+0-9.eE]+\s+[-+0-9.eE]+\s*\))vs"));
	const QRegularExpressionMatch match = planePattern.match(line);
	if (!match.hasMatch()) {
		return line;
	}
	QString result = line;
	const QString replacement = QStringLiteral("( %1 %2 %3 %4 )")
		.arg(mapGeometryCoordinateText(normal.x), mapGeometryCoordinateText(normal.y), mapGeometryCoordinateText(normal.z), mapGeometryCoordinateText(distance));
	result.replace(match.capturedStart(), match.capturedLength(), replacement);
	return result;
}

// Patch control rows are `( x y z u v )` groups; only the position changes.
QString replacePatchRow(const QString& line, const QVector<LevelMapVec3>& points, const QVector<double>& u, const QVector<double>& v)
{
	static const QRegularExpression rowPattern(QStringLiteral(R"vs(\(\s*[-+0-9.eE]+\s+[-+0-9.eE]+\s+[-+0-9.eE]+\s+[-+0-9.eE]+\s+[-+0-9.eE]+\s*\))vs"));
	QString result = line;
	int searchFrom = 0;
	for (int index = 0; index < points.size(); ++index) {
		const QRegularExpressionMatch match = rowPattern.match(result, searchFrom);
		if (!match.hasMatch()) {
			return line;
		}
		const QString replacement = QStringLiteral("( %1 %2 %3 %4 %5 )")
			.arg(mapCoordinateText(points.at(index).x), mapCoordinateText(points.at(index).y), mapCoordinateText(points.at(index).z),
				mapCoordinateText(index < u.size() ? u.at(index) : 0.0), mapCoordinateText(index < v.size() ? v.at(index) : 0.0));
		result.replace(match.capturedStart(), match.capturedLength(), replacement);
		searchFrom = match.capturedStart() + replacement.size();
	}
	return result;
}

// Replaces the two Valve 220 texture axis groups, `[ x y z offset ]`, on a
// face line with the face's current axes.
QString replaceValveAxes(const QString& line, const LevelMapBrushFace& face)
{
	static const QRegularExpression axisPattern(
		QStringLiteral(R"vs(\[\s*[-+0-9.eE]+\s+[-+0-9.eE]+\s+[-+0-9.eE]+\s+[-+0-9.eE]+\s*\])vs"));
	QString result = line;
	int searchFrom = 0;
	for (int group = 0; group < 2; ++group) {
		const QRegularExpressionMatch match = axisPattern.match(result, searchFrom);
		if (!match.hasMatch()) {
			return line;
		}
		const LevelMapVec3& axis = group == 0 ? face.uAxis : face.vAxis;
		const double offset = group == 0 ? face.uOffset : face.vOffset;
		const QString replacement = QStringLiteral("[ %1 %2 %3 %4 ]")
			.arg(mapCoordinateText(axis.x), mapCoordinateText(axis.y), mapCoordinateText(axis.z), mapCoordinateText(offset));
		result.replace(match.capturedStart(), match.capturedLength(), replacement);
		searchFrom = match.capturedStart() + static_cast<int>(replacement.size());
	}
	return result;
}

bool faceTextureSpan(const QString& line, qsizetype* start, qsizetype* end);

// Rewrites the numbers after a face's texture name to the face's own: `shift
// shift rotation scale scale` on a classic face, and on a Valve 220 face its
// two axis groups, offsets included, then `rotation scale scale`. The name,
// existing flags, comments and trailing braces retain their spelling unless a
// copied flag value changes. Missing nonzero flags are inserted before trailing
// comments. Comments between numbers are stepped over. The line comes back
// unchanged, with `rewritten` false, when it
// cannot be read that way: numbers missing, or carried on to the next line.
QString replaceFaceTextureParameters(const QString& line, const LevelMapBrushFace& face, bool* rewritten = nullptr, bool validateOnly = false)
{
	if (rewritten) { *rewritten = false; }
	const auto tokens = tokenizeMapText(line, nullptr);
	int cursor = 0;
	while (cursor < tokens.size() && !tokenIsPunct(tokens[cursor], QLatin1Char('('))) { ++cursor; }
	const auto punct = [&](char c) { return cursor < tokens.size() && tokenIsPunct(tokens[cursor], QLatin1Char(c)) && (++cursor, true); };
	struct Replacement { int column; qsizetype size; QString text; };
	QVector<Replacement> replacements;
	const auto number = [&](const QString* text = nullptr) {
		double value = 0;
		if (cursor >= tokens.size() || !tokenNumber(tokens[cursor], &value) || !std::isfinite(value) || tokens[cursor].line != 1) { return false; }
		if (text) { replacements.push_back({tokens[cursor].column, tokens[cursor].text.size(), *text}); }
		++cursor;
		return true;
	};
	const auto replaceNumber = [&](double value) {
		if (!std::isfinite(value)) { return false; }
		if (validateOnly) { return number(); }
		const QString text = QString::number(value == 0 ? 0.0 : value, 'g', 15);
		return number(&text);
	};
	for (int group = 0; group < (face.explicitPlane ? 1 : 3); ++group) {
		if (!punct('(')) { return line; }
		for (int i = 0; i < (face.explicitPlane ? 4 : 3); ++i) { if (!number()) { return line; } }
		if (!punct(')')) { return line; }
	}
	if (face.explicitTextureMatrix) {
		if (!punct('(')) { return line; }
		for (int row = 0; row < 2; ++row) {
			if (!punct('(')) { return line; }
			for (int col = 0; col < 3; ++col) { if (!replaceNumber(face.textureMatrix[row*3+col])) { return line; } }
			if (!punct(')')) { return line; }
		}
		if (!punct(')')) { return line; }
	} else {
		// Skip the material token. Token offsets preserve quoted names and
		// comments between every number, including during classic conversion.
		if (cursor >= tokens.size() || tokens[cursor].type == MapTokenType::Punct) { return line; }
		++cursor;
		const bool sourceAxes = cursor < tokens.size() && tokenIsPunct(tokens[cursor], QLatin1Char('['));
		if (sourceAxes) {
			if (!face.explicitTextureAxes) { return line; }
			for (int group = 0; group < 2; ++group) {
				const auto& axis = group == 0 ? face.uAxis : face.vAxis;
				if (!punct('[') || !replaceNumber(axis.x) || !replaceNumber(axis.y) || !replaceNumber(axis.z)
					|| !replaceNumber(group == 0 ? face.uOffset : face.vOffset) || !punct(']')) { return line; }
			}
		} else if (face.explicitTextureAxes) {
			for (int group = 0; group < 2; ++group) {
				const auto& axis = group == 0 ? face.uAxis : face.vAxis;
				if (validateOnly) { if (!number()) { return line; } continue; }
				const QString text = QStringLiteral("[ %1 %2 %3 %4 ]")
					.arg(QString::number(axis.x, 'g', 15), QString::number(axis.y, 'g', 15), QString::number(axis.z, 'g', 15),
						QString::number(group == 0 ? face.uOffset : face.vOffset, 'g', 15));
				if (!number(&text)) { return line; }
			}
		} else if (!replaceNumber(face.shiftX) || !replaceNumber(face.shiftY)) { return line; }
		if (!replaceNumber(face.rotation) || !replaceNumber(face.scaleX) || !replaceNumber(face.scaleY)) { return line; }
	}
	// Optional contents/surface/value fields remain byte-for-byte unchanged
	// unless the face's values differ. Surface paste can also add absent flags
	// before trailing comments, without rewriting geometry or unknown text.
	int insertColumn = cursor > 0 ? tokens[cursor - 1].column + static_cast<int>(tokens[cursor - 1].text.size()) : 0;
	if (face.explicitTextureMatrix) {
		if (cursor >= tokens.size() || tokens[cursor].type == MapTokenType::Punct) { return line; }
		qsizetype start = 0, end = 0;
		if (!faceTextureSpan(line, &start, &end)) { return line; }
		insertColumn = static_cast<int>(end + (end < line.size() && line[end] == QLatin1Char('"') ? 1 : 0));
		++cursor;
	}
	const std::array<qint64, 3> flags{face.contentFlags, face.surfaceFlags, face.surfaceValue};
	int flag = 0;
	for (; flag < 3 && cursor < tokens.size(); ++flag) {
		double old = 0;
		if (!tokenNumber(tokens[cursor], &old)) { break; }
		const QString text = QString::number(flags[flag]);
		insertColumn = tokens[cursor].column + static_cast<int>(tokens[cursor].text.size());
		if (!number(!validateOnly && std::round(old) != static_cast<double>(flags[flag]) ? &text : nullptr)) { return line; }
	}
	bool missingFlags = false;
	for (int i = flag; i < 3; ++i) { missingFlags |= flags[i] != 0; }
	if (missingFlags) {
		QString text;
		for (; flag < 3; ++flag) { text += QLatin1Char(' ') + QString::number(flags[flag]); }
		if (!validateOnly) { replacements.append({insertColumn, 0, text}); }
	}
	if (validateOnly) { if (rewritten) { *rewritten = true; } return line; }
	QString result = line;
	for (auto it = replacements.crbegin(); it != replacements.crend(); ++it) { result.replace(it->column, it->size, it->text); }
	if (rewritten) { *rewritten = true; }
	return result;
}

// Finds the texture name on a brush face line: the first token after the
// face's parenthesised groups (points or plane, and for brushDef the texture
// matrix). Compact files may open the line with braces or a brushDef keyword,
// which are skipped; anything else before the first group means the line is
// not one this can read, and false is returned. `start` and `end` bound the
// name, inside its quotes when it has them.
bool faceTextureSpan(const QString& line, qsizetype* start, qsizetype* end)
{
	const qsizetype firstGroup = line.indexOf(QLatin1Char('('));
	if (firstGroup < 0) {
		return false;
	}
	QString prefix = line.left(firstGroup);
	prefix.remove(QLatin1Char('{')).remove(QLatin1Char('}'));
	prefix.remove(QStringLiteral("brushDef3"), Qt::CaseInsensitive).remove(QStringLiteral("brushDef"), Qt::CaseInsensitive);
	if (!prefix.trimmed().isEmpty()) {
		return false;
	}
	qsizetype index = firstGroup;
	const auto skipSpace = [&line, &index]() {
		while (index < line.size() && line.at(index).isSpace()) {
			++index;
		}
	};
	while (index < line.size() && line.at(index) == QLatin1Char('(')) {
		int depth = 0;
		do {
			if (line.at(index) == QLatin1Char('(')) {
				++depth;
			} else if (line.at(index) == QLatin1Char(')')) {
				--depth;
			}
			++index;
		} while (index < line.size() && depth > 0);
		skipSpace();
	}
	if (index >= line.size()) {
		return false;
	}
	if (line.at(index) == QLatin1Char('"')) {
		const qsizetype close = line.indexOf(QLatin1Char('"'), index + 1);
		if (close < 0) {
			return false;
		}
		*start = index + 1;
		*end = close;
		return true;
	}
	qsizetype stop = index;
	while (stop < line.size() && !line.at(stop).isSpace()) {
		++stop;
	}
	*start = index;
	*end = stop;
	return true;
}

// Replaces the texture name on a brush face line, leaving the line untouched
// when it already names `name` or cannot be read.
QString replaceFaceTexture(const QString& line, const QString& name)
{
	qsizetype start = 0;
	qsizetype end = 0;
	if (!faceTextureSpan(line, &start, &end) || line.mid(start, end - start) == name) {
		return line;
	}
	return line.left(start) + name + line.mid(end);
}

// Where a patch's shader name sits on its line: at `column`, where the parser
// found it, quoted or bare. The name can share its line with `patchDef2 {` or
// with other primitives, so it is never looked for by position among tokens.
// False when the text there is not a name.
bool patchShaderSpan(const QString& line, int column, qsizetype* start, qsizetype* end)
{
	if (column < 0 || column >= line.size()) {
		return false;
	}
	if (line.at(column) == QLatin1Char('"')) {
		const qsizetype close = line.indexOf(QLatin1Char('"'), column + 1);
		if (close < 0) {
			return false;
		}
		*start = column + 1;
		*end = close;
		return true;
	}
	qsizetype stop = column;
	while (stop < line.size() && !line.at(stop).isSpace() && !isMapPunct(line.at(stop)) && line.at(stop) != QLatin1Char('"')) {
		++stop;
	}
	if (stop == column) {
		return false;
	}
	*start = column;
	*end = stop;
	return true;
}

// Replaces a patch's shader name where the parser found it, leaving the line
// untouched when it already names `name` or holds no name there.
QString replacePatchTexture(const QString& line, const QString& name, int column)
{
	qsizetype start = 0;
	qsizetype end = 0;
	if (!patchShaderSpan(line, column, &start, &end) || line.mid(start, end - start) == name) {
		return line;
	}
	return line.left(start) + name + line.mid(end);
}

// The points of one control column of a patch, as its file group lists them.
// controlPoints is row major, so the column is walked with a `width` stride.
void patchColumn(const LevelMapPatch& patch, int column, QVector<LevelMapVec3>* points, QVector<double>* u, QVector<double>* v)
{
	for (int row = 0; row < patch.height; ++row) {
		const int index = (row * patch.width) + column;
		if (index >= patch.controlPoints.size()) {
			break;
		}
		points->push_back(patch.controlPoints.at(index));
		u->push_back(index < patch.controlU.size() ? patch.controlU.at(index) : 0.0);
		v->push_back(index < patch.controlV.size() ? patch.controlV.at(index) : 0.0);
	}
}

// The text of a brush copied in the editor: its template with the copy's own
// face points.
QStringList copiedBrushLines(const LevelMapBrush& brush)
{
	QStringList lines = brush.sourceLines;
	for (const LevelMapBrushFace& face : brush.faces) {
		const int index = face.line - brush.sourceFirstLine;
		if (index < 0 || index >= lines.size()) {
			continue;
		}
		lines[index] = face.explicitPlane
			? replacePlaneGroup(lines.at(index), face.planeNormal, face.planeDistance)
			: replaceLeadingPointGroups(lines.at(index), {face.p0, face.p1, face.p2});
		if (brush.textureAxesDirty && face.explicitTextureAxes) {
			lines[index] = replaceValveAxes(lines.at(index), face);
		}
		if (face.textureDirty) {
			lines[index] = replaceFaceTexture(lines.at(index), face.textureName);
		}
		if (face.textureParametersDirty) {
			lines[index] = replaceFaceTextureParameters(lines.at(index), face);
		}
	}
	return lines;
}

QStringList copiedPatchLines(const LevelMapPatch& patch)
{
	if (patch.definitionDirty) {
		return levelPatchDefinition(patch);
	}
	QStringList lines = patch.sourceLines;
	const int textureIndex = patch.textureLine - patch.sourceFirstLine;
	if (patch.textureDirty && textureIndex >= 0 && textureIndex < lines.size()) {
		lines[textureIndex] = replacePatchTexture(lines.at(textureIndex), patch.textureName, patch.textureColumn);
	}
	for (int column = 0; column < patch.controlRowLines.size() && column < patch.width; ++column) {
		const int index = patch.controlRowLines.at(column) - patch.sourceFirstLine;
		if (index < 0 || index >= lines.size()) {
			continue;
		}
		QVector<LevelMapVec3> points;
		QVector<double> u;
		QVector<double> v;
		patchColumn(patch, column, &points, &u, &v);
		lines[index] = replacePatchRow(lines.at(index), points, u, v);
	}
	return lines;
}

QString serializeMapText(const LevelMapDocument& document, QHash<QString, QString>* emitted = nullptr)
{
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return document.originalText;
	}
	QStringList lines = document.textLines;
	QSet<int> deletedLines;
	QMap<int, QStringList> insertions;
	QMap<int, QVector<LevelMapSelectionRef>> sourceObjects;
	QMap<int, QVector<LevelMapSelectionRef>> insertedObjects;
	QHash<int, int> emittedCounts;
	const auto recordObject = [&](LevelMapSelectionKind kind, int id) {
		if (emitted) { emitted->insert(levelMapSelectionRefId({kind, id}), levelMapSelectionRefId({kind, emittedCounts[static_cast<int>(kind)]++})); }
	};
	if (emitted) {
		emitted->clear();
		const auto sourceOrder = [&](const auto& records, LevelMapSelectionKind kind) {
			for (const auto& record : records) { if (record.startLine > 0) { sourceObjects[record.startLine].push_back({kind, record.id}); } }
		};
		sourceOrder(document.entities, LevelMapSelectionKind::Entity);
		sourceOrder(document.brushes, LevelMapSelectionKind::QuakeBrush);
		for (const auto& patch : document.patches) {
			if (patch.startLine > 0 && !patch.definitionDirty) { sourceObjects[patch.startLine].push_back({LevelMapSelectionKind::QuakePatch, patch.id}); }
		}
	}
	for (const LevelMapLineRange& range : document.deletedLineRanges) {
		for (int line = std::max(1, range.first); line <= std::min(range.last, static_cast<int>(lines.size())); ++line) {
			deletedLines.insert(line);
		}
	}
	// Patch shaders first, while the columns the parser found them at still
	// hold: a rewrite earlier on a shared line would shift them.
	for (const LevelMapPatch& patch : document.patches) {
		if (!patch.definitionDirty && patch.textureDirty && patch.startLine > 0 && patch.textureLine > 0 && patch.textureLine <= lines.size()) {
			lines[patch.textureLine - 1] = replacePatchTexture(lines.at(patch.textureLine - 1), patch.textureName, patch.textureColumn);
		}
	}

	// Brush and patch geometry lives in the source text, not in a key/value pair,
	// so a moved brush has to be written back through its own face lines. Without
	// this the move is applied in memory and silently lost on save.
	for (const LevelMapBrush& brush : document.brushes) {
		// A copy's face lines are its template's, not the file's: it is written
		// in full further down.
		if (!brush.geometryDirty || brush.startLine <= 0) {
			continue;
		}
		for (const LevelMapBrushFace& face : brush.faces) {
			if (face.line <= 0 || face.line > lines.size()) {
				continue;
			}
			const QString& original = lines.at(face.line - 1);
			lines[face.line - 1] = face.explicitPlane
				? replacePlaneGroup(original, face.planeNormal, face.planeDistance)
				: replaceLeadingPointGroups(original, {face.p0, face.p1, face.p2});
			if (brush.textureAxesDirty && face.explicitTextureAxes) {
				lines[face.line - 1] = replaceValveAxes(lines.at(face.line - 1), face);
			}
		}
	}
	// Replaced textures, after the points, since both can touch one line.
	for (const LevelMapBrush& brush : document.brushes) {
		if (!brush.texturesDirty || brush.startLine <= 0) {
			continue;
		}
		for (const LevelMapBrushFace& face : brush.faces) {
			if (face.textureDirty && face.line > 0 && face.line <= lines.size()) {
				lines[face.line - 1] = replaceFaceTexture(lines.at(face.line - 1), face.textureName);
			}
		}
	}
	// Edited shifts, turns, and scales, found again after the names.
	for (const LevelMapBrush& brush : document.brushes) {
		if (!brush.textureParametersDirty || brush.startLine <= 0) {
			continue;
		}
		for (const LevelMapBrushFace& face : brush.faces) {
			if (face.textureParametersDirty && face.line > 0 && face.line <= lines.size()) {
				lines[face.line - 1] = replaceFaceTextureParameters(lines.at(face.line - 1), face);
			}
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (patch.definitionDirty && patch.startLine > 0) {
			insertions[patch.startLine - 1].append(levelPatchDefinition(patch));
			insertedObjects[patch.startLine - 1].push_back({LevelMapSelectionKind::QuakePatch, patch.id});
			for (int line = patch.startLine; line <= patch.endLine; ++line) {
				deletedLines.insert(line);
			}
			continue;
		}
		if (!patch.geometryDirty || patch.startLine <= 0 || patch.controlRowLines.isEmpty() || !patch.controlGridNormalized) {
			continue;
		}
		// controlRowLines holds one source line per parenthesised file group, and
		// a file group is a grid COLUMN of `height` points (see parsePatchBody).
		for (int column = 0; column < patch.controlRowLines.size() && column < patch.width; ++column) {
			const int line = patch.controlRowLines.at(column);
			if (line <= 0 || line > lines.size()) {
				continue;
			}
			QVector<LevelMapVec3> points;
			QVector<double> u;
			QVector<double> v;
			patchColumn(patch, column, &points, &u, &v);
			lines[line - 1] = replacePatchRow(lines.at(line - 1), points, u, v);
		}
	}

	for (const LevelMapEntity& entity : document.entities) {
		if (entity.startLine <= 0 || entity.startLine > lines.size()) {
			continue;
		}
		for (const int removed : entity.removedPropertyLines) {
			if (removed <= 0 || removed > lines.size()) {
				continue;
			}
			// A removed key's line can hold a brace or other pairs as well: only
			// the removed pairs go, and the line itself only once it is empty.
			QSet<QString> kept;
			for (const LevelMapProperty& property : entity.properties) {
				if (property.line == removed) {
					kept.insert(property.key);
				}
			}
			removeQuotedPairsExcept(&lines[removed - 1], kept);
			if (lines.at(removed - 1).trimmed().isEmpty()) {
				deletedLines.insert(removed);
			}
		}
		int anchor = entity.startLine;
		for (const LevelMapProperty& property : entity.properties) {
			if (property.line <= 0 || property.line > lines.size()) {
				continue;
			}
			// Only the value changes; an unedited pair stays byte-identical. A
			// pair split across lines is left as written rather than risk
			// damaging the file.
			setQuotedValueOnLine(&lines[property.line - 1], property.key, property.value);
			anchor = std::max(anchor, property.line);
		}
		const QString indent = anchor > 0 && anchor <= lines.size() && anchor != entity.startLine
			? leadingWhitespace(lines.at(anchor - 1))
			: QString();
		for (const LevelMapProperty& property : entity.properties) {
			if (property.line > 0) {
				continue;
			}
			// New keys go after the last existing key so that brush entities keep
			// their keys above their brush bodies.
			insertions[anchor].push_back(formatKeyLine(indent, property.key, property.value));
		}
	}

	// Brushes and patches copied in the editor go inside their entity, just
	// ahead of its closing brace; an added entity's go in its own block below.
	const auto sourceEntity = [&document, &lines](int entityId) -> const LevelMapEntity* {
		for (const LevelMapEntity& entity : document.entities) {
			if (entity.id == entityId) {
				return entity.startLine > 0 && entity.endLine > 1 && entity.endLine <= lines.size() ? &entity : nullptr;
			}
		}
		return nullptr;
	};
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.startLine <= 0) {
			if (const LevelMapEntity* owner = sourceEntity(brush.entityId)) {
				insertions[owner->endLine - 1].append(copiedBrushLines(brush));
				insertedObjects[owner->endLine - 1].push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
			}
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (patch.startLine <= 0) {
			if (const LevelMapEntity* owner = sourceEntity(patch.entityId)) {
				insertions[owner->endLine - 1].append(copiedPatchLines(patch));
				insertedObjects[owner->endLine - 1].push_back({LevelMapSelectionKind::QuakePatch, patch.id});
			}
		}
	}

	QStringList output;
	output.reserve(lines.size() + insertions.size());
	for (int index = 1; index <= lines.size(); ++index) {
		if (!deletedLines.contains(index)) {
			output.push_back(lines.at(index - 1));
			for (const auto& ref : sourceObjects.value(index)) { recordObject(ref.kind, ref.objectId); }
		}
		const auto found = insertions.constFind(index);
		if (found != insertions.constEnd()) {
			output.append(found.value());
			for (const auto& ref : insertedObjects.value(index)) { recordObject(ref.kind, ref.objectId); }
		}
	}

	// Entities added in the editor have no source lines. They follow the last
	// entity, ahead of the file's final line break.
	QStringList added;
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.startLine > 0) {
			continue;
		}
		added.push_back(QStringLiteral("{"));
		recordObject(LevelMapSelectionKind::Entity, entity.id);
		for (const LevelMapProperty& property : entity.properties) {
			added.push_back(formatKeyLine(QString(), property.key, property.value));
		}
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.entityId == entity.id && brush.startLine <= 0) {
				added.append(copiedBrushLines(brush));
				recordObject(LevelMapSelectionKind::QuakeBrush, brush.id);
			}
		}
		for (const LevelMapPatch& patch : document.patches) {
			if (patch.entityId == entity.id && patch.startLine <= 0) {
				added.append(copiedPatchLines(patch));
				recordObject(LevelMapSelectionKind::QuakePatch, patch.id);
			}
		}
		added.push_back(QStringLiteral("}"));
	}
	if (!added.isEmpty()) {
		const bool finalBreak = !output.isEmpty() && output.constLast().isEmpty();
		if (finalBreak) {
			output.removeLast();
		}
		output.append(added);
		if (finalBreak) {
			output.push_back(QString());
		}
	}
	return output.join(document.lineEnding.isEmpty() ? QStringLiteral("\n") : document.lineEnding);
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

LevelMapEntity* entityById(LevelMapDocument* document, int entityId)
{
	if (!document) {
		return nullptr;
	}
	for (LevelMapEntity& entity : document->entities) {
		if (entity.id == entityId) {
			return &entity;
		}
	}
	return nullptr;
}

const LevelMapEntity* entityById(const LevelMapDocument* document, int entityId)
{
	if (!document) { return nullptr; }
	for (const auto& entity : document->entities) {
		if (entity.id == entityId) { return &entity; }
	}
	return nullptr;
}

LevelMapDoomVertex* vertexById(LevelMapDocument* document, int objectId)
{
	if (!document) {
		return nullptr;
	}
	for (LevelMapDoomVertex& vertex : document->doomVertices) {
		if (vertex.id == objectId) {
			return &vertex;
		}
	}
	return nullptr;
}

LevelMapDoomLinedef* linedefById(LevelMapDocument* document, int objectId)
{
	if (!document) {
		return nullptr;
	}
	for (LevelMapDoomLinedef& linedef : document->doomLinedefs) {
		if (linedef.id == objectId) {
			return &linedef;
		}
	}
	return nullptr;
}

LevelMapDoomThing* thingById(LevelMapDocument* document, int objectId)
{
	if (!document) {
		return nullptr;
	}
	for (LevelMapDoomThing& thing : document->doomThings) {
		if (thing.id == objectId) {
			return &thing;
		}
	}
	return nullptr;
}

LevelMapBrush* brushById(LevelMapDocument* document, int objectId)
{
	if (!document) {
		return nullptr;
	}
	for (LevelMapBrush& brush : document->brushes) {
		if (brush.id == objectId) {
			return &brush;
		}
	}
	return nullptr;
}

const LevelMapBrush* brushById(const LevelMapDocument* document, int objectId)
{
	if (!document) { return nullptr; }
	for (const auto& brush : document->brushes) {
		if (brush.id == objectId) { return &brush; }
	}
	return nullptr;
}

// Whether a face of `brush` shares its line with another face, of this brush
// or of another one. Save-back finds a face's points, axes, and numbers as the
// first of their kind on its line, so rewriting such a face would write it
// over the other; clip, hollow, and carve refuse these brushes the same way.
bool brushFaceLinesShared(const LevelMapDocument& document, const LevelMapBrush& brush)
{
	QSet<int> own;
	for (const LevelMapBrushFace& face : brush.faces) {
		if (face.line <= 0) {
			continue;
		}
		if (own.contains(face.line)) {
			return true;
		}
		own.insert(face.line);
	}
	// A copy's lines are its own; a brush from the file can share a line with
	// the next brush in a compact file.
	if (brush.startLine <= 0) {
		return false;
	}
	for (const LevelMapBrush& other : document.brushes) {
		if (other.id == brush.id || other.startLine <= 0 || other.endLine < brush.startLine || other.startLine > brush.endLine) {
			continue;
		}
		for (const LevelMapBrushFace& face : other.faces) {
			if (own.contains(face.line)) {
				return true;
			}
		}
	}
	return false;
}

// Bulk edits must inspect source-line ownership once, rather than scan every
// other brush for each selected brush. Editor copies have private source text.
class LevelMapBrushLineConflicts {
public:
	explicit LevelMapBrushLineConflicts(const LevelMapDocument& document)
	{
		QHash<int, int> owners;
		for (const auto& brush : document.brushes) {
			detail::placementCancellationCheckpoint();
			QSet<int> own;
			for (const auto& face : brush.faces) {
				if (face.line <= 0) { continue; }
				if (own.contains(face.line)) { m_conflicts.insert(brush.id); }
				own.insert(face.line);
				if (brush.startLine <= 0) { continue; }
				const auto previous = owners.constFind(face.line);
				if (previous == owners.cend()) { owners.insert(face.line, brush.id); }
				else if (*previous != brush.id) {
					m_conflicts.insert(*previous);
					m_conflicts.insert(brush.id);
				}
			}
		}
	}
	bool contains(int id) const { return m_conflicts.contains(id); }

private:
	QSet<int> m_conflicts;
};

// Where a Doom thing may stand, as the WAD holds it: whole map units within
// 16 bits on each axis, and a height only on a Hexen map. Empty when the
// place will do.
QString doomThingPlaceProblem(const LevelMapDocument& document, double x, double y, double z)
{
	const auto whole = [](double value) {
		return std::isfinite(value) && std::abs(value - std::round(value)) < 1e-9;
	};
	const auto fits = [](double value) {
		return value >= -32768.0 && value <= 32767.0;
	};
	if (!whole(x) || !whole(y) || !whole(z)) {
		return QCoreApplication::translate("VibeStudioLevelMap", "A Doom thing stands on whole map units.");
	}
	if (!fits(x) || !fits(y)) {
		return QCoreApplication::translate("VibeStudioLevelMap", "A thing must lie within -32768 to 32767 on each axis.");
	}
	if (document.doomFormat == LevelMapDoomFormat::Hexen ? !fits(z) : z != 0.0) {
		return document.doomFormat == LevelMapDoomFormat::Hexen ? QCoreApplication::translate("VibeStudioLevelMap", "A Hexen thing's height is within -32768 to 32767.")
									: QCoreApplication::translate("VibeStudioLevelMap", "A Doom-format thing has no height; its z stays 0.");
	}
	return {};
}

QString sharedFaceLinesText(int brushId)
{
	return QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 writes two faces on one line, so VibeStudio cannot rewrite its geometry without writing one face over the other; "
		       "put each face on a line of its own first.")
		.arg(brushId);
}

LevelMapPatch* patchById(LevelMapDocument* document, int objectId)
{
	if (!document) {
		return nullptr;
	}
	for (LevelMapPatch& patch : document->patches) {
		if (patch.id == objectId) {
			return &patch;
		}
	}
	return nullptr;
}

LevelMapDoomSector* sectorById(LevelMapDocument* document, int objectId)
{
	if (!document) {
		return nullptr;
	}
	for (LevelMapDoomSector& sector : document->doomSectors) {
		if (sector.id == objectId) {
			return &sector;
		}
	}
	return nullptr;
}

LevelMapDoomSidedef* sidedefById(LevelMapDocument* document, int objectId)
{
	if (!document) {
		return nullptr;
	}
	for (LevelMapDoomSidedef& sidedef : document->doomSidedefs) {
		if (sidedef.id == objectId) {
			return &sidedef;
		}
	}
	return nullptr;
}

void upsertEntityProperty(LevelMapEntity* entity, const QString& key, const QString& value)
{
	if (!entity) {
		return;
	}
	const int index = propertyIndex(*entity, key);
	if (index >= 0) {
		entity->properties[index].value = value;
		return;
	}
	entity->properties.push_back({key, value, 0});
}

void removeEntityPropertyAt(LevelMapEntity* entity, int index)
{
	if (!entity || index < 0 || index >= entity->properties.size()) {
		return;
	}
	const int line = entity->properties.at(index).line;
	if (line > 0) {
		entity->removedPropertyLines.push_back(line);
	}
	entity->properties.remove(index);
}

void syncDoomThingEntity(LevelMapDocument* document, int thingId)
{
	LevelMapDoomThing* thing = thingById(document, thingId);
	LevelMapEntity* entity = entityById(document, thingId);
	if (!thing || !entity) {
		return;
	}
	entity->className = QStringLiteral("thing:%1").arg(thing->type);
	entity->origin = {thing->x, thing->y, thing->z, true};
	upsertEntityProperty(entity, QStringLiteral("type"), QString::number(thing->type));
	upsertEntityProperty(entity, QStringLiteral("angle"), QString::number(thing->angle));
	upsertEntityProperty(entity, QStringLiteral("flags"), QString::number(thing->flags));
	upsertEntityProperty(entity, QStringLiteral("origin"), serializeVec3(entity->origin));
	upsertEntityProperty(entity, QStringLiteral("x"), QString::number(static_cast<int>(std::lround(thing->x))));
	upsertEntityProperty(entity, QStringLiteral("y"), QString::number(static_cast<int>(std::lround(thing->y))));
	if (document->doomFormat == LevelMapDoomFormat::Hexen) {
		upsertEntityProperty(entity, QStringLiteral("z"), QString::number(static_cast<int>(std::lround(thing->z))));
		upsertEntityProperty(entity, QStringLiteral("tid"), QString::number(thing->tid));
		upsertEntityProperty(entity, QStringLiteral("special"), QString::number(thing->special));
	}
}

void refreshEditState(LevelMapDocument* document)
{
	if (!document) {
		return;
	}
	if (document->savedUndoDepth >= 0) {
		document->editState = document->undoStack.size() == document->savedUndoDepth
			? QStringLiteral("saved")
			: QStringLiteral("modified");
		return;
	}
	document->editState = document->undoStack.isEmpty() ? QStringLiteral("clean") : QStringLiteral("modified");
}

// Whether a command changes what a Doom map's node, blockmap, and reject lumps
// were built from: where vertices are, which vertices and sides a linedef
// joins, or which sector a side faces. Heights, light, textures, offsets, and
// things leave those lumps as good as they were.
bool commandChangesDoomGeometry(const LevelMapUndoCommand& command)
{
	// Extension fields can influence the builder in namespace-specific ways.
	if (command.commandKind == QStringLiteral("udmf-properties")) { return true; }
	if (command.commandKind == QStringLiteral("udmf-transform")) {
		return command.udmfNodeInputsChanged;
	}
	// A topology step that only changed sector fields (heights, light) moves
	// no line, so the nodes still match.
	if (command.commandKind == QStringLiteral("doom-topology") && command.key == QStringLiteral("sector-fields")) {
		return false;
	}
	if (command.commandKind == QStringLiteral("doom-geometry") || command.commandKind == QStringLiteral("doom-topology")) {
		return true;
	}
	if (command.commandKind == QStringLiteral("transform-objects") && !command.vertexResults.isEmpty()) { return true; }
	if (command.commandKind == QStringLiteral("set-sidedef-property")) {
		return command.oldSidedef.sector != command.newSidedef.sector;
	}
	if (command.commandKind == QStringLiteral("set-side-property")) {
		return command.key == QStringLiteral("sector");
	}
	const auto movesGeometry = [](const QString& kind) {
		return kind == QStringLiteral("vertex") || kind == QStringLiteral("linedef");
	};
	if (command.commandKind == QStringLiteral("move")) {
		return movesGeometry(command.objectKind);
	}
	if (command.commandKind == QStringLiteral("move-selection")) {
		return std::any_of(command.moveSteps.cbegin(), command.moveSteps.cend(), [&movesGeometry](const LevelMapMoveStep& step) {
			return movesGeometry(step.objectKind);
		});
	}
	return false;
}

// Counts `command` in or out of the edits that leave the node lumps stale:
// +1 as it is done or redone, -1 as it is undone.
void countDoomGeometryEdit(LevelMapDocument* document, const LevelMapUndoCommand& command, int direction)
{
	if (commandChangesDoomGeometry(command)) {
		document->doomGeometryEdits = std::max(0, document->doomGeometryEdits + direction);
	}
	document->doomGeometryChanged = document->doomGeometryEdits > 0;
}

void pushUndo(LevelMapDocument* document, LevelMapUndoCommand command)
{
	if (!document) {
		return;
	}
	detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
	const bool structural = command.commandKind == QStringLiteral("add-objects") || command.commandKind == QStringLiteral("add-entity")
		|| command.commandKind == QStringLiteral("delete-objects") || command.commandKind == QStringLiteral("replace-objects")
		|| command.commandKind == QStringLiteral("doom-topology") || command.commandKind == QStringLiteral("doom-geometry")
		|| command.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0;
	if (!command.hasSceneSnapshot && structural && !document->scene.nodes.isEmpty()) {
		if (command.commandKind == QStringLiteral("add-objects") || command.commandKind == QStringLiteral("add-entity")) {
			const auto added = [&](const auto& records, LevelMapSelectionKind kind) {
				for (const auto& record : records) { command.sceneAddedObjects << levelMapSelectionRefId({kind, record.id}); }
			};
			if (document->format != LevelMapFormat::DoomWad) { added(command.entitySnapshots, LevelMapSelectionKind::Entity); }
			added(command.brushSnapshots, LevelMapSelectionKind::QuakeBrush);
			added(command.patchSnapshots, LevelMapSelectionKind::QuakePatch);
			added(command.thingSnapshots, LevelMapSelectionKind::DoomThing);
		}
		command.sceneBefore = document->scene;
		command.sceneAfter = reconcileLevelScene(*document, command);
		command.hasSceneSnapshot = command.sceneBefore != command.sceneAfter;
		document->scene = command.sceneAfter;
	}
	recordLevelSceneEdit(document, command);
	++document->revision;
	countDoomGeometryEdit(document, command, 1);
	// Test reachability BEFORE the push: this edit discards the redo branch, so
	// a save point deeper than the current position is about to be destroyed.
	// Testing afterwards would compare against the already-regrown stack and
	// miss the single-undo case by exactly one. Equality is still reachable -
	// savedUndoDepth == size here means the save point is the current position,
	// which undoing the new command returns to.
	if (document->savedUndoDepth > document->undoStack.size()) {
		document->savedUndoDepth = -1;
	}
	document->undoStack.push_back(command);
	document->redoStack.clear();
	const int limit = document->undoLimit > 0 ? document->undoLimit : 200;
	while (document->undoStack.size() > limit) {
		document->undoStack.removeFirst();
		if (document->savedUndoDepth > 0) {
			--document->savedUndoDepth;
		} else if (document->savedUndoDepth == 0) {
			document->savedUndoDepth = -1;
		}
	}
	refreshEditState(document);
}

void translateBrush(LevelMapBrush* brush, double dx, double dy, double dz)
{
	if (!brush) {
		return;
	}
	for (LevelMapBrushFace& face : brush->faces) {
		for (LevelMapVec3* point : {&face.p0, &face.p1, &face.p2}) {
			if (!point->valid) {
				continue;
			}
			point->x += dx;
			point->y += dy;
			point->z += dz;
		}
		if (face.explicitPlane && face.planeNormal.valid) {
			// n.p + d = 0 -> translating the plane moves d by -n.delta.
			face.planeDistance -= (face.planeNormal.x * dx) + (face.planeNormal.y * dy) + (face.planeNormal.z * dz);
		}
	}
	if (brush->mins.valid) {
		brush->mins.x += dx;
		brush->mins.y += dy;
		brush->mins.z += dz;
	}
	if (brush->maxs.valid) {
		brush->maxs.x += dx;
		brush->maxs.y += dy;
		brush->maxs.z += dz;
	}
}

void translatePatch(LevelMapPatch* patch, double dx, double dy, double dz)
{
	if (!patch) {
		return;
	}
	for (LevelMapVec3& point : patch->controlPoints) {
		if (!point.valid) {
			continue;
		}
		point.x += dx;
		point.y += dy;
		point.z += dz;
	}
	for (LevelMapVec3* bound : {&patch->mins, &patch->maxs}) {
		if (!bound->valid) {
			continue;
		}
		bound->x += dx;
		bound->y += dy;
		bound->z += dz;
	}
}

bool applyMoveDelta(LevelMapDocument* document, const QString& kind, int objectId, double dx, double dy, double dz)
{
	if (kind == QStringLiteral("vertex")) {
		LevelMapDoomVertex* vertex = vertexById(document, objectId);
		if (!vertex) {
			return false;
		}
		vertex->x += dx;
		vertex->y += dy;
		return true;
	}
	if (kind == QStringLiteral("linedef")) {
		LevelMapDoomLinedef* linedef = linedefById(document, objectId);
		if (!linedef) {
			return false;
		}
		QSet<int> moved;
		for (const int vertexId : {linedef->startVertex, linedef->endVertex}) {
			if (moved.contains(vertexId)) {
				continue;
			}
			moved.insert(vertexId);
			if (LevelMapDoomVertex* vertex = vertexById(document, vertexId)) {
				vertex->x += dx;
				vertex->y += dy;
			}
		}
		return true;
	}
	if (kind == QStringLiteral("thing")) {
		LevelMapDoomThing* thing = thingById(document, objectId);
		if (!thing) {
			return false;
		}
		thing->x += dx;
		thing->y += dy;
		thing->z += dz;
		syncDoomThingEntity(document, objectId);
		return true;
	}
	if (kind == QStringLiteral("brush")) {
		LevelMapBrush* brush = brushById(document, objectId);
		if (!brush) {
			return false;
		}
		translateBrush(brush, dx, dy, dz);
		brush->geometryDirty = true;
		return true;
	}
	if (kind == QStringLiteral("patch")) {
		LevelMapPatch* patch = patchById(document, objectId);
		if (!patch) {
			return false;
		}
		translatePatch(patch, dx, dy, dz);
		patch->geometryDirty = true;
		return true;
	}
	return false;
}

// Applies a recorded command in either direction. Undo and redo route through
// here so that neither of them touches the undo/redo stacks as a side effect.
void pruneLevelMapSelection(LevelMapDocument* document);
void applySelectionFlags(LevelMapDocument* document);

bool isTextMapFormat(LevelMapFormat format)
{
	return format == LevelMapFormat::QuakeMap || format == LevelMapFormat::Quake3Map;
}

// Sets one texture a replace-texture command changed, forward to its new
// name or back to its old one.
bool applyTextureChange(LevelMapDocument* document, const LevelMapTextureChange& change, bool forward)
{
	const QString name = forward ? change.newName : change.oldName;
	if (change.kind == QStringLiteral("face")) {
		LevelMapBrush* brush = brushById(document, change.objectId);
		if (!brush || change.part < 0 || change.part >= brush->faces.size()) {
			return false;
		}
		brush->faces[change.part].textureName = name;
		brush->faces[change.part].textureDirty = true;
		brush->texturesDirty = true;
		brush->textureNames.clear();
		for (const LevelMapBrushFace& face : brush->faces) {
			if (!face.textureName.trimmed().isEmpty()) {
				brush->textureNames.push_back(face.textureName);
			}
		}
		return true;
	}
	if (change.kind == QStringLiteral("patch")) {
		LevelMapPatch* patch = patchById(document, change.objectId);
		if (!patch) {
			return false;
		}
		patch->textureName = name;
		patch->textureDirty = true;
		return true;
	}
	if (change.kind == QStringLiteral("sidedef")) {
		LevelMapDoomSidedef* sidedef = sidedefById(document, change.objectId);
		if (!sidedef) {
			return false;
		}
		(change.part == 0 ? sidedef->upperTexture : change.part == 1 ? sidedef->lowerTexture : sidedef->middleTexture) = name;
		return true;
	}
	if (change.kind == QStringLiteral("sector")) {
		LevelMapDoomSector* sector = sectorById(document, change.objectId);
		if (!sector) {
			return false;
		}
		(change.part == 0 ? sector->floorTexture : sector->ceilingTexture) = name;
		return true;
	}
	return false;
}

// Re-derives the map's texture list after textures change, in the order the
// parser records them.
void rebuildTextureReferences(LevelMapDocument* document)
{
	document->textureReferences.clear();
	if (document->format == LevelMapFormat::DoomWad) {
		for (const LevelMapDoomSidedef& sidedef : document->doomSidedefs) {
			for (const QString& texture : {sidedef.upperTexture, sidedef.lowerTexture, sidedef.middleTexture}) {
				if (!texture.isEmpty() && texture != QStringLiteral("-")) {
					document->textureReferences.push_back(texture);
				}
			}
		}
		for (const LevelMapDoomSector& sector : document->doomSectors) {
			for (const QString& texture : {sector.floorTexture, sector.ceilingTexture}) {
				if (!texture.isEmpty()) {
					document->textureReferences.push_back(texture);
				}
			}
		}
		return;
	}
	for (const LevelMapBrush& brush : document->brushes) {
		for (const LevelMapBrushFace& face : brush.faces) {
			if (!face.textureName.trimmed().isEmpty()) {
				document->textureReferences.push_back(face.textureName);
			}
		}
	}
	for (const LevelMapPatch& patch : document->patches) {
		if (!patch.textureName.trimmed().isEmpty()) {
			document->textureReferences.push_back(patch.textureName);
		}
	}
}

bool isWorldspawnEntity(const LevelMapEntity& entity)
{
	return entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
}

// A double quote or a line break would end the quoted key or value early, and
// every tool after it would misread the rest of the file.
bool isWritableMapString(const QString& value)
{
	return !value.contains(QLatin1Char('"')) && !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r'));
}

template <typename Item>
int indexOfObjectId(const QVector<Item>& items, int id)
{
	for (int index = 0; index < items.size(); ++index) {
		if (items.at(index).id == id) {
			return index;
		}
	}
	return -1;
}

template <typename Item>
QHash<int, int> objectIndexes(const QVector<Item>& items)
{
	QHash<int, int> indexes;
	indexes.reserve(items.size());
	for (int index = 0; index < items.size(); ++index) {
		if ((index & 255) == 0) { detail::placementCancellationCheckpoint(); }
		indexes.insert(items[index].id, index);
	}
	return indexes;
}

template <typename Item>
bool snapshotsPresent(const QVector<Item>& items, const QVector<Item>& snapshots, bool present)
{
	if (snapshots.isEmpty()) { return true; }
	const auto indexes = objectIndexes(items);
	for (const Item& snapshot : snapshots) {
		if (indexes.contains(snapshot.id) != present) {
			return false;
		}
	}
	return true;
}

template <typename Item>
void removeObjectSnapshots(QVector<Item>* items, const QVector<Item>& snapshots)
{
	if (snapshots.isEmpty()) { return; }
	const auto indexes = objectIndexes(snapshots);
	items->removeIf([&indexes](const Item& item) { return indexes.contains(item.id); });
}

// Puts snapshots back at the positions they were taken from, lowest first:
// each insert then lands where the object was, because everything ahead of it
// is already back.
template <typename Item>
void restoreAtIndexes(QVector<Item>* items, const QVector<Item>& snapshots, const QVector<int>& indexes)
{
	QVector<int> order(snapshots.size());
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [&indexes](int left, int right) {
		return indexes.value(left) < indexes.value(right);
	});
	for (const int position : order) {
		if ((position & 255) == 0) { detail::placementCancellationCheckpoint(); }
		const int size = static_cast<int>(items->size());
		items->insert(std::clamp(indexes.value(position, size), 0, size), snapshots.at(position));
	}
}

// Radiant and TrenchBroom head each entity and brush with a "// entity N" or
// "// brush N" comment; deleting the object takes its comment along.
int deletionFirstLine(const LevelMapDocument& document, int startLine)
{
	static const QRegularExpression marker(QStringLiteral(R"(^\s*//\s*(entity|brush|patch)\s+\d+\s*$)"),
		QRegularExpression::CaseInsensitiveOption);
	if (startLine > 1 && startLine - 1 <= document.textLines.size() && marker.match(document.textLines.at(startLine - 2)).hasMatch()) {
		return startLine - 1;
	}
	return startLine;
}

// True when the object's opening and closing braces sit on lines of their
// own, so dropping its lines removes nothing else.
bool objectOwnsItsLines(const LevelMapDocument& document, int startLine, int endLine)
{
	if (startLine <= 0) {
		// Added in the editor: nothing in the source to drop.
		return true;
	}
	if (endLine < startLine || endLine > document.textLines.size()) {
		return false;
	}
	return document.textLines.at(startLine - 1).trimmed() == QStringLiteral("{")
		&& document.textLines.at(endLine - 1).trimmed() == QStringLiteral("}");
}

// Where each record of an edit's middle stage ends up once the removed ones
// are out, -1 for those: a delta's removals as a map.
template <typename Record>
QVector<int> deltaRemovalMap(const LevelMapRecordDelta<Record>& delta)
{
	const int size = delta.originalSize + static_cast<int>(delta.appended.size());
	QVector<int> map(size, -1);
	int removed = 0;
	int next = 0;
	for (int index = 0; index < size; ++index) {
		if (removed < delta.removedIndexes.size() && delta.removedIndexes.at(removed) == index) {
			++removed;
			continue;
		}
		map[index] = next++;
	}
	return map;
}

// The same map read the other way: where each record after the edit stood in
// its middle stage.
QVector<int> invertedDeltaMap(const QVector<int>& map)
{
	QVector<int> inverse;
	for (int index = 0; index < map.size(); ++index) {
		if (map.at(index) >= 0) {
			inverse.push_back(index);
		}
	}
	return inverse;
}

// A reference through a map; one past the map's end, or -1, stays as it is.
int throughDeltaMap(const QVector<int>& map, int id)
{
	return id >= 0 && id < map.size() ? map.at(id) : id;
}

// Forward to the middle stage: the changed records' new content, then the
// appended ones.
template <typename Record>
bool deltaToMiddle(QVector<Record>* records, const LevelMapRecordDelta<Record>& delta)
{
	if (records->size() != delta.originalSize) {
		return false;
	}
	for (int index = 0; index < delta.changedIndexes.size(); ++index) {
		(*records)[delta.changedIndexes.at(index)] = delta.changedAfter.at(index);
	}
	*records += delta.appended;
	return true;
}

// From the middle stage to after the edit: the removed records out.
template <typename Record>
void deltaMiddleToAfter(QVector<Record>* records, const LevelMapRecordDelta<Record>& delta)
{
	for (int index = static_cast<int>(delta.removedIndexes.size()) - 1; index >= 0; --index) {
		records->remove(delta.removedIndexes.at(index));
	}
	for (int index = 0; index < records->size(); ++index) {
		(*records)[index].id = index;
	}
}

// Back from after the edit to the middle stage: the removed records in again.
template <typename Record>
void deltaAfterToMiddle(QVector<Record>* records, const LevelMapRecordDelta<Record>& delta)
{
	for (int index = 0; index < delta.removedIndexes.size(); ++index) {
		records->insert(delta.removedIndexes.at(index), delta.removed.at(index));
	}
	for (int index = 0; index < records->size(); ++index) {
		(*records)[index].id = index;
	}
}

// Back from the middle stage to before the edit: the appended records off
// and the changed ones as they were.
template <typename Record>
bool deltaMiddleToBefore(QVector<Record>* records, const LevelMapRecordDelta<Record>& delta)
{
	if (records->size() != delta.originalSize + delta.appended.size()) {
		return false;
	}
	records->resize(delta.originalSize);
	for (int index = 0; index < delta.changedIndexes.size(); ++index) {
		(*records)[delta.changedIndexes.at(index)] = delta.changedBefore.at(index);
	}
	return true;
}

// The references a Doom map's records hold, taken through the maps given.
void renumberDoomReferences(LevelMapDocument* document, const QVector<int>& vertices, const QVector<int>& sidedefs, const QVector<int>& sectors)
{
	for (LevelMapDoomLinedef& linedef : document->doomLinedefs) {
		linedef.startVertex = throughDeltaMap(vertices, linedef.startVertex);
		linedef.endVertex = throughDeltaMap(vertices, linedef.endVertex);
		linedef.frontSidedef = throughDeltaMap(sidedefs, linedef.frontSidedef);
		linedef.backSidedef = throughDeltaMap(sidedefs, linedef.backSidedef);
	}
	for (LevelMapDoomSidedef& sidedef : document->doomSidedefs) {
		sidedef.sector = throughDeltaMap(sectors, sidedef.sector);
	}
}

// Puts one entity of a `set-properties` command back as it was (forward
// false) or as the command left it, and its Doom thing with it.
bool applyPropertyStep(LevelMapDocument* document, const QString& key, const LevelMapPropertyStep& step, bool forward)
{
	LevelMapEntity* entity = entityById(document, step.entityId);
	if (!entity) {
		return false;
	}
	const bool present = forward ? !step.removed : step.keyExisted;
	const QString value = forward ? step.newValue : step.oldValue;
	if (present && !forward && step.removed) {
		// A key the command took off goes back on its own line.
		entity->properties.push_back({key, value, step.propertyLine});
		entity->removedPropertyLines.removeOne(step.propertyLine);
	} else if (present) {
		upsertEntityProperty(entity, key, value);
	} else {
		const int index = propertyIndex(*entity, key);
		if (index >= 0) {
			removeEntityPropertyAt(entity, index);
		}
	}
	if (key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
		entity->className = present ? value : QString();
	} else if (key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
		entity->origin = present ? parseVec3(value) : LevelMapVec3 {};
	}
	if (step.hasThingSnapshot) {
		if (LevelMapDoomThing* thing = thingById(document, step.entityId)) {
			*thing = forward ? step.newThing : step.oldThing;
			syncDoomThingEntity(document, step.entityId);
		}
	}
	return true;
}

bool applyLevelMapCommand(LevelMapDocument* document, const LevelMapUndoCommand& command, bool forward);

bool applyLevelMapGeometryCommand(LevelMapDocument* document, const LevelMapUndoCommand& command, bool forward)
{
	if (!document) {
		return false;
	}
	if (command.commandKind == QStringLiteral("udmf-properties") || command.commandKind == QStringLiteral("udmf-transform")) {
		return adoptUdmfText(document, forward ? command.udmfAfter : command.udmfBefore);
	}
	if (command.commandKind == QStringLiteral("transform-objects") || command.commandKind == QStringLiteral("edit-patch")
		|| command.commandKind == QStringLiteral("edit-brush")) {
		const auto& entities = forward ? command.entityResults : command.entitySnapshots;
		const auto& brushes = forward ? command.brushResults : command.brushSnapshots;
		const auto& patches = forward ? command.patchResults : command.patchSnapshots;
		const auto& things = forward ? command.thingResults : command.thingSnapshots;
		const auto& vertices = forward ? command.vertexResults : command.vertexSnapshots;
		const auto& lines = forward ? command.linedefResults : command.linedefSnapshots;
		if (!snapshotsPresent(document->entities, entities, true) || !snapshotsPresent(document->brushes, brushes, true)
			|| !snapshotsPresent(document->patches, patches, true) || !snapshotsPresent(document->doomThings, things, true)
			|| !snapshotsPresent(document->doomVertices, vertices, true) || !snapshotsPresent(document->doomLinedefs, lines, true)) {
			return false;
		}
		const auto replaceAll = [](auto* items, const auto& with) {
			if (with.isEmpty()) { return; }
			const auto indexes = objectIndexes(*items);
			for (const auto& item : with) {
				(*items)[indexes.value(item.id)] = item;
			}
		};
		replaceAll(&document->entities, entities);
		replaceAll(&document->brushes, brushes);
		replaceAll(&document->patches, patches);
		replaceAll(&document->doomThings, things);
		replaceAll(&document->doomVertices, vertices);
		replaceAll(&document->doomLinedefs, lines);
		if (command.commandKind == QStringLiteral("edit-brush")) {
			if (forward) { document->deletedLineRanges += command.deletedLineRanges; }
			else { for (const auto& range : command.deletedLineRanges) { document->deletedLineRanges.removeOne(range); } }
		}
		if (command.commandKind == QStringLiteral("edit-patch") || command.commandKind == QStringLiteral("edit-brush")) {
			rebuildTextureReferences(document);
		}
		applySelectionFlags(document);
		return true;
	}
	if (command.commandKind == QStringLiteral("set-linedef-property")) {
		for (const LevelMapDoomLinedef& record : forward ? command.linedefResults : command.linedefSnapshots) {
			const int index = indexOfObjectId(document->doomLinedefs, record.id);
			if (index < 0) {
				return false;
			}
			document->doomLinedefs[index] = record;
		}
		applySelectionFlags(document);
		return true;
	}
	if (command.commandKind == QStringLiteral("doom-geometry") || command.commandKind == QStringLiteral("set-side-property")) {
		// Forward, each result replaces the record with its id or is appended;
		// back, a record with a snapshot is restored and an appended one comes
		// off again, newest first.
		const auto apply = [forward](auto* records, const auto& snapshots, const auto& results) {
			if (forward) {
				for (const auto& record : results) {
					const int index = indexOfObjectId(*records, record.id);
					if (index >= 0) {
						(*records)[index] = record;
					} else {
						records->push_back(record);
					}
				}
				return;
			}
			for (auto result = results.crbegin(); result != results.crend(); ++result) {
				const int index = indexOfObjectId(*records, result->id);
				if (index < 0) {
					continue;
				}
				const auto snapshot = std::find_if(snapshots.cbegin(), snapshots.cend(), [&result](const auto& before) {
					return before.id == result->id;
				});
				if (snapshot != snapshots.cend()) {
					(*records)[index] = *snapshot;
				} else {
					records->remove(index);
				}
			}
		};
		apply(&document->doomVertices, command.vertexSnapshots, command.vertexResults);
		apply(&document->doomLinedefs, command.linedefSnapshots, command.linedefResults);
		apply(&document->doomSidedefs, command.sidedefSnapshots, command.sidedefResults);
		// New sides bring their textures into the map's list.
		rebuildTextureReferences(document);
		pruneLevelMapSelection(document);
		applySelectionFlags(document);
		return true;
	}
	if (command.commandKind == QStringLiteral("doom-topology")) {
		// The records go through their deltas: forward, the changed and
		// appended ones in, then the removed ones out with every reference
		// renumbered; back, the same in reverse. The issues and selection,
		// which name records by id, are kept whole.
		const QVector<int> vertexMap = deltaRemovalMap(command.vertexDelta);
		const QVector<int> sidedefMap = deltaRemovalMap(command.sidedefDelta);
		const QVector<int> sectorMap = deltaRemovalMap(command.sectorDelta);
		if (forward) {
			if (!deltaToMiddle(&document->doomVertices, command.vertexDelta) || !deltaToMiddle(&document->doomLinedefs, command.linedefDelta)
				|| !deltaToMiddle(&document->doomSidedefs, command.sidedefDelta) || !deltaToMiddle(&document->doomSectors, command.sectorDelta)) {
				return false;
			}
			deltaMiddleToAfter(&document->doomVertices, command.vertexDelta);
			deltaMiddleToAfter(&document->doomLinedefs, command.linedefDelta);
			deltaMiddleToAfter(&document->doomSidedefs, command.sidedefDelta);
			deltaMiddleToAfter(&document->doomSectors, command.sectorDelta);
			renumberDoomReferences(document, vertexMap, sidedefMap, sectorMap);
		} else {
			renumberDoomReferences(document, invertedDeltaMap(vertexMap), invertedDeltaMap(sidedefMap), invertedDeltaMap(sectorMap));
			deltaAfterToMiddle(&document->doomVertices, command.vertexDelta);
			deltaAfterToMiddle(&document->doomLinedefs, command.linedefDelta);
			deltaAfterToMiddle(&document->doomSidedefs, command.sidedefDelta);
			deltaAfterToMiddle(&document->doomSectors, command.sectorDelta);
			if (!deltaMiddleToBefore(&document->doomVertices, command.vertexDelta) || !deltaMiddleToBefore(&document->doomLinedefs, command.linedefDelta)
				|| !deltaMiddleToBefore(&document->doomSidedefs, command.sidedefDelta)
				|| !deltaMiddleToBefore(&document->doomSectors, command.sectorDelta)) {
				return false;
			}
		}
		if (command.hasThingSnapshot) {
			document->doomThings = forward ? command.thingResults : command.thingSnapshots;
			document->entities = forward ? command.entityResults : command.entitySnapshots;
		}
		document->issues = forward ? command.issueResults : command.issueSnapshots;
		document->selection = forward ? command.selectionResult : command.selectionSnapshot;
		++document->doomTopologyRevision;
		rebuildTextureReferences(document);
		pruneLevelMapSelection(document);
		return true;
	}
	if (command.commandKind == QStringLiteral("replace-objects")) {
		// Brushes out, their replacements in, and back again on undo.
		const QVector<LevelMapBrush>& gone = forward ? command.brushSnapshots : command.brushResults;
		const QVector<LevelMapBrush>& come = forward ? command.brushResults : command.brushSnapshots;
		const QVector<int>& indexes = forward ? command.brushResultIndexes : command.brushIndexes;
		if (!snapshotsPresent(document->brushes, gone, true) || !snapshotsPresent(document->brushes, come, false)
			|| !snapshotsPresent(document->entities, command.entitySnapshots, forward)) {
			return false;
		}
		removeObjectSnapshots(&document->brushes, gone);
		restoreAtIndexes(&document->brushes, come, indexes);
		if (!command.selectionResult.isEmpty() || !command.selectionSnapshot.isEmpty()) {
			document->selection = forward ? command.selectionResult : command.selectionSnapshot;
		}
		// A brush entity the replacement emptied goes with its brushes.
		if (forward) {
			removeObjectSnapshots(&document->entities, command.entitySnapshots);
		} else {
			restoreAtIndexes(&document->entities, command.entitySnapshots, command.entityIndexes);
		}
		if (forward) {
			QSet<QString> objectIds;
			for (const LevelMapBrush& brush : command.brushSnapshots) {
				objectIds.insert(brushObjectId(brush.id));
			}
			for (const LevelMapEntity& entity : command.entitySnapshots) {
				objectIds.insert(entityObjectId(entity.id));
			}
			for (int index = static_cast<int>(document->issues.size()) - 1; index >= 0; --index) {
				if (objectIds.contains(document->issues.at(index).objectId)) {
					document->issues.remove(index);
				}
			}
			document->deletedLineRanges += command.deletedLineRanges;
		} else {
			restoreAtIndexes(&document->issues, command.issueSnapshots, command.issueIndexes);
			for (const LevelMapLineRange& range : command.deletedLineRanges) {
				document->deletedLineRanges.removeOne(range);
			}
		}
		rebuildTextureReferences(document);
		pruneLevelMapSelection(document);
		return true;
	}
	if (command.commandKind == QStringLiteral("replace-texture")) {
		for (const LevelMapTextureChange& change : command.textureChanges) {
			if (!applyTextureChange(document, change, forward)) {
				return false;
			}
		}
		rebuildTextureReferences(document);
		return true;
	}
	if (command.commandKind == QStringLiteral("add-entity") || command.commandKind == QStringLiteral("add-objects")
		|| command.commandKind == QStringLiteral("delete-objects")) {
		const bool deletion = command.commandKind == QStringLiteral("delete-objects");
		const bool removing = deletion == forward;
		// Check every object before touching any, so a stale command cannot
		// leave the map half changed.
		if (!snapshotsPresent(document->entities, command.entitySnapshots, removing)
			|| !snapshotsPresent(document->brushes, command.brushSnapshots, removing)
			|| !snapshotsPresent(document->patches, command.patchSnapshots, removing)
			|| !snapshotsPresent(document->doomThings, command.thingSnapshots, removing)) {
			return false;
		}
		if (removing) {
			removeObjectSnapshots(&document->entities, command.entitySnapshots);
			removeObjectSnapshots(&document->brushes, command.brushSnapshots);
			removeObjectSnapshots(&document->patches, command.patchSnapshots);
			removeObjectSnapshots(&document->doomThings, command.thingSnapshots);
		} else {
			restoreAtIndexes(&document->entities, command.entitySnapshots, command.entityIndexes);
			restoreAtIndexes(&document->brushes, command.brushSnapshots, command.brushIndexes);
			restoreAtIndexes(&document->patches, command.patchSnapshots, command.patchIndexes);
			restoreAtIndexes(&document->doomThings, command.thingSnapshots, command.thingIndexes);
		}
		if (deletion && forward) {
			QSet<QString> objectIds;
			for (const LevelMapDoomThing& thing : command.thingSnapshots) {
				objectIds.insert(thingObjectId(thing.id));
			}
			for (const LevelMapEntity& entity : command.entitySnapshots) {
				objectIds.insert(entityObjectId(entity.id));
			}
			for (const LevelMapBrush& brush : command.brushSnapshots) {
				objectIds.insert(brushObjectId(brush.id));
			}
			for (const LevelMapPatch& patch : command.patchSnapshots) {
				objectIds.insert(patchObjectId(patch.id));
			}
			for (int index = static_cast<int>(document->issues.size()) - 1; index >= 0; --index) {
				if (objectIds.contains(document->issues.at(index).objectId)) {
					document->issues.remove(index);
				}
			}
			document->deletedLineRanges += command.deletedLineRanges;
		} else if (deletion) {
			restoreAtIndexes(&document->issues, command.issueSnapshots, command.issueIndexes);
			for (const LevelMapLineRange& range : command.deletedLineRanges) {
				document->deletedLineRanges.removeOne(range);
			}
		}
		// Assemblies can restore the caller's old selection and select their
		// inserted objects on redo. Legacy commands without snapshots retain
		// their existing prune-only behavior.
		if (!command.selectionSnapshot.isEmpty() || !command.selectionResult.isEmpty()) {
			document->selection = forward ? command.selectionResult : command.selectionSnapshot;
		}
		pruneLevelMapSelection(document);
		rebuildTextureReferences(document);
		return true;
	}
	if (command.commandKind == QStringLiteral("set-property")) {
		LevelMapEntity* entity = entityById(document, command.entityId);
		if (!entity) {
			return false;
		}
		if (forward) {
			upsertEntityProperty(entity, command.key, command.newValue);
		} else if (command.keyExisted) {
			upsertEntityProperty(entity, command.key, command.oldValue);
		} else {
			const int index = propertyIndex(*entity, command.key);
			if (index >= 0) {
				removeEntityPropertyAt(entity, index);
			}
		}
		if (command.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
			entity->className = forward ? command.newValue : command.oldValue;
		}
		if (command.key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
			entity->origin = parseVec3(forward ? command.newValue : command.oldValue);
		}
		if (command.hasThingSnapshot) {
			LevelMapDoomThing* thing = thingById(document, command.objectId);
			if (thing) {
				// Restoring the record itself is what the old undo path missed: it
				// reverted the entity key and then copied the unreverted thing back.
				*thing = forward ? command.newThing : command.oldThing;
				syncDoomThingEntity(document, command.objectId);
			}
		}
		return true;
	}
	if (command.commandKind == QStringLiteral("set-properties")) {
		bool applied = true;
		for (const LevelMapPropertyStep& step : command.propertySteps) {
			applied = applyPropertyStep(document, command.key, step, forward) && applied;
		}
		return applied;
	}
	if (command.commandKind == QStringLiteral("remove-property")) {
		LevelMapEntity* entity = entityById(document, command.entityId);
		if (!entity) {
			return false;
		}
		if (forward) {
			const int index = propertyIndex(*entity, command.key);
			if (index >= 0) {
				removeEntityPropertyAt(entity, index);
			}
		} else {
			entity->properties.push_back({command.key, command.oldValue, command.propertyLine});
			// One entry per removal: another key removed from the same line
			// must stay removed.
			entity->removedPropertyLines.removeOne(command.propertyLine);
			if (command.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
				entity->className = command.oldValue;
			}
			if (command.key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
				entity->origin = parseVec3(command.oldValue);
			}
		}
		return true;
	}
	if (command.commandKind == QStringLiteral("set-sector-property")) {
		LevelMapDoomSector* sector = sectorById(document, command.objectId);
		if (!sector || !command.hasSectorSnapshot) {
			return false;
		}
		*sector = forward ? command.newSector : command.oldSector;
		return true;
	}
	if (command.commandKind == QStringLiteral("set-sidedef-property")) {
		LevelMapDoomSidedef* sidedef = sidedefById(document, command.objectId);
		if (!sidedef || !command.hasSidedefSnapshot) {
			return false;
		}
		*sidedef = forward ? command.newSidedef : command.oldSidedef;
		return true;
	}
	if (command.commandKind == QStringLiteral("move")) {
		const double sign = forward ? 1.0 : -1.0;
		if (command.objectKind == QStringLiteral("entity")) {
			LevelMapEntity* entity = entityById(document, command.objectId);
			if (!entity) {
				return false;
			}
			if (!forward && command.originSynthesized) {
				const int index = propertyIndex(*entity, QStringLiteral("origin"));
				if (index >= 0) {
					removeEntityPropertyAt(entity, index);
				}
				entity->origin = {};
			} else {
				upsertEntityProperty(entity, QStringLiteral("origin"), forward ? command.newValue : command.oldValue);
				entity->origin = parseVec3(forward ? command.newValue : command.oldValue);
			}
			if (document->format == LevelMapFormat::DoomWad) {
				LevelMapDoomThing* thing = thingById(document, command.objectId);
				if (thing) {
					thing->x += sign * command.delta.x;
					thing->y += sign * command.delta.y;
					thing->z += sign * command.delta.z;
					syncDoomThingEntity(document, command.objectId);
				}
			}
			return true;
		}
		return applyMoveDelta(document, command.objectKind, command.objectId, sign * command.delta.x, sign * command.delta.y, sign * command.delta.z);
	}
	if (command.commandKind == QStringLiteral("move-selection")) {
		// A compound move is one history entry holding one step per object.
		// Redo replays the steps in recording order and undo reverses them, so a
		// selection whose members overlap (a linedef and one of its own
		// vertices, say) ends exactly where it started.
		const int count = static_cast<int>(command.moveSteps.size());
		const auto stepCommand = [&command](const LevelMapMoveStep& step) {
			LevelMapUndoCommand child;
			child.commandKind = QStringLiteral("move");
			child.objectKind = step.objectKind;
			child.objectId = step.objectId;
			child.key = QStringLiteral("origin");
			child.oldValue = step.oldValue;
			child.newValue = step.newValue;
			child.originSynthesized = step.originSynthesized;
			child.delta = step.delta.valid ? step.delta : command.delta;
			return child;
		};
		const auto stepAt = [&command, count, forward](int position) {
			return command.moveSteps.at(forward ? position : count - 1 - position);
		};
		int applied = 0;
		while (applied < count) {
			if (!applyLevelMapCommand(document, stepCommand(stepAt(applied)), forward)) {
				break;
			}
			++applied;
		}
		if (applied < count) {
			// Roll the partial application back so a vanished target can never
			// leave the map half moved.
			for (int position = applied - 1; position >= 0; --position) {
				applyLevelMapCommand(document, stepCommand(stepAt(position)), !forward);
			}
			return false;
		}
		return true;
	}
	return false;
}

bool applyLevelMapCommand(LevelMapDocument* document, const LevelMapUndoCommand& command, bool forward)
{
	if (!document) { return false; }
	if (command.commandKind != QStringLiteral("scene") && !applyLevelMapGeometryCommand(document, command, forward)) { return false; }
	if (command.hasSceneSnapshot) { document->scene = forward ? command.sceneAfter : command.sceneBefore; }
	if (command.commandKind == QStringLiteral("scene")) {
		setLevelMapSelection(document, forward ? command.selectionResult : command.selectionSnapshot);
		document->issues.removeIf([](const LevelMapIssue& issue) { return issue.code == QStringLiteral("scene-metadata"); });
		if (!document->scene.problem.isEmpty()) { addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("scene-metadata"), document->scene.problem); }
	}
	return true;
}

void clearSelectionFlags(LevelMapDocument* document)
{
	for (LevelMapEntity& entity : document->entities) {
		entity.selected = false;
	}
	for (LevelMapBrush& brush : document->brushes) {
		brush.selected = false;
	}
	for (LevelMapPatch& patch : document->patches) {
		patch.selected = false;
	}
	for (LevelMapDoomVertex& vertex : document->doomVertices) {
		vertex.selected = false;
	}
	for (LevelMapDoomLinedef& linedef : document->doomLinedefs) {
		linedef.selected = false;
	}
	for (LevelMapDoomThing& thing : document->doomThings) {
		thing.selected = false;
	}
	for (LevelMapDoomSector& sector : document->doomSectors) {
		sector.selected = false;
	}
	for (LevelMapDoomSidedef& sidedef : document->doomSidedefs) {
		sidedef.selected = false;
	}
}

bool selectionObjectExists(LevelMapDocument* document, LevelMapSelectionKind kind, int objectId)
{
	if (!document || objectId < 0) {
		return false;
	}
	switch (kind) {
	case LevelMapSelectionKind::None:
		return false;
	case LevelMapSelectionKind::Entity:
		return entityById(document, objectId) != nullptr;
	case LevelMapSelectionKind::DoomVertex:
		return vertexById(document, objectId) != nullptr;
	case LevelMapSelectionKind::DoomLinedef:
		return linedefById(document, objectId) != nullptr;
	case LevelMapSelectionKind::DoomThing:
		return thingById(document, objectId) != nullptr;
	case LevelMapSelectionKind::DoomSector:
		return sectorById(document, objectId) != nullptr;
	case LevelMapSelectionKind::QuakeBrush:
		return brushById(document, objectId) != nullptr;
	case LevelMapSelectionKind::QuakePatch:
		return patchById(document, objectId) != nullptr;
	}
	return false;
}

// Whether references name objects that exist, gathering each kind's ids
// once: a search of the document for each reference made selecting, or
// pruning, every brush of a large map quadratic.
class LevelMapObjectExistence {
public:
	explicit LevelMapObjectExistence(const LevelMapDocument* document)
		: m_document(document)
	{
	}

	bool contains(const LevelMapSelectionRef& ref)
	{
		detail::placementCancellationCheckpoint();
		if (!m_document || ref.kind == LevelMapSelectionKind::None || ref.objectId < 0) {
			return false;
		}
		const int kind = static_cast<int>(ref.kind);
		auto found = m_ids.find(kind);
		if (found == m_ids.end()) {
			found = m_ids.insert(kind, idsOfKind(ref.kind));
		}
		return found->contains(ref.objectId);
	}

private:
	QSet<int> idsOfKind(LevelMapSelectionKind kind) const
	{
		QSet<int> ids;
		const auto gather = [&ids](const auto& objects) {
			ids.reserve(objects.size());
			for (const auto& object : objects) {
				detail::placementCancellationCheckpoint();
				ids.insert(object.id);
			}
		};
		switch (kind) {
		case LevelMapSelectionKind::None:
			break;
		case LevelMapSelectionKind::Entity:
			gather(m_document->entities);
			break;
		case LevelMapSelectionKind::DoomVertex:
			gather(m_document->doomVertices);
			break;
		case LevelMapSelectionKind::DoomLinedef:
			gather(m_document->doomLinedefs);
			break;
		case LevelMapSelectionKind::DoomThing:
			gather(m_document->doomThings);
			break;
		case LevelMapSelectionKind::DoomSector:
			gather(m_document->doomSectors);
			break;
		case LevelMapSelectionKind::QuakeBrush:
			gather(m_document->brushes);
			break;
		case LevelMapSelectionKind::QuakePatch:
			gather(m_document->patches);
			break;
		}
		return ids;
	}

	const LevelMapDocument* m_document = nullptr;
	QHash<int, QSet<int>> m_ids;
};

QString selectionNotFoundText(LevelMapSelectionKind kind)
{
	switch (kind) {
	case LevelMapSelectionKind::None:
		return QCoreApplication::translate("VibeStudioLevelMap", "Unknown selection kind.");
	case LevelMapSelectionKind::Entity:
		return QCoreApplication::translate("VibeStudioLevelMap", "Entity selection was not found.");
	case LevelMapSelectionKind::DoomVertex:
		return QCoreApplication::translate("VibeStudioLevelMap", "Vertex selection was not found.");
	case LevelMapSelectionKind::DoomLinedef:
		return QCoreApplication::translate("VibeStudioLevelMap", "Linedef selection was not found.");
	case LevelMapSelectionKind::DoomThing:
		return QCoreApplication::translate("VibeStudioLevelMap", "Thing selection was not found.");
	case LevelMapSelectionKind::DoomSector:
		return QCoreApplication::translate("VibeStudioLevelMap", "Sector selection was not found.");
	case LevelMapSelectionKind::QuakeBrush:
		return QCoreApplication::translate("VibeStudioLevelMap", "Brush selection was not found.");
	case LevelMapSelectionKind::QuakePatch:
		return QCoreApplication::translate("VibeStudioLevelMap", "Patch selection was not found.");
	}
	return QCoreApplication::translate("VibeStudioLevelMap", "Unknown selection kind.");
}

// Re-derives everything that hangs off the selection set: the per-record
// `selected` flags and the primary single-selection fields. Every selection
// mutator ends here, which is what keeps the old single-selection API honest.
void applySelectionFlags(LevelMapDocument* document)
{
	if (!document) {
		return;
	}
	clearSelectionFlags(document);
	// One pass over each kind of object for the ids selected, rather than a
	// search of the document for each selected object, which made selecting
	// every brush of a large map quadratic.
	QHash<int, QSet<int>> selectedIds;
	for (const LevelMapSelectionRef& ref : document->selection) {
		selectedIds[static_cast<int>(ref.kind)].insert(ref.objectId);
	}
	const auto mark = [&selectedIds](auto& objects, LevelMapSelectionKind kind) {
		const QSet<int> ids = selectedIds.value(static_cast<int>(kind));
		if (ids.isEmpty()) {
			return;
		}
		for (auto& object : objects) {
			if (ids.contains(object.id)) {
				object.selected = true;
			}
		}
	};
	mark(document->entities, LevelMapSelectionKind::Entity);
	mark(document->doomVertices, LevelMapSelectionKind::DoomVertex);
	mark(document->doomLinedefs, LevelMapSelectionKind::DoomLinedef);
	mark(document->doomThings, LevelMapSelectionKind::DoomThing);
	mark(document->doomSectors, LevelMapSelectionKind::DoomSector);
	mark(document->brushes, LevelMapSelectionKind::QuakeBrush);
	mark(document->patches, LevelMapSelectionKind::QuakePatch);
	if (document->selection.isEmpty()) {
		document->selectionKind = LevelMapSelectionKind::None;
		document->selectedObjectId = -1;
		return;
	}
	const LevelMapSelectionRef primary = document->selection.back();
	document->selectionKind = primary.kind;
	document->selectedObjectId = primary.kind == LevelMapSelectionKind::None ? -1 : primary.objectId;
}

// Moves one object and fills in everything undo needs to reverse it, without
// touching the undo stacks or the selection. The single-object move command and
// the compound selection move both go through here, so they cannot drift apart.
bool applyMoveToObject(LevelMapDocument* document, const QString& kind, int objectId, double dx, double dy, double dz,
	LevelMapUndoCommand* command, QString* error)
{
	if (!document || !command) {
		return false;
	}
	command->commandKind = QStringLiteral("move");
	command->objectKind = kind;
	command->objectId = objectId;
	command->delta = {dx, dy, dz, true};

	if (kind == QStringLiteral("entity")) {
		LevelMapEntity* entity = entityById(document, objectId);
		if (!entity) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Entity not found.");
			}
			return false;
		}
		const int index = propertyIndex(*entity, QStringLiteral("origin"));
		command->originSynthesized = index < 0;
		LevelMapVec3 origin = entity->origin.valid ? entity->origin : parseVec3(propertyValue(*entity, QStringLiteral("origin")));
		if (!origin.valid) {
			origin = {0.0, 0.0, 0.0, true};
		}
		command->oldValue = serializeVec3(origin);
		origin.x += dx;
		origin.y += dy;
		origin.z += dz;
		command->newValue = serializeVec3(origin);
		command->key = QStringLiteral("origin");
		if (index >= 0) {
			entity->properties[index].value = command->newValue;
		} else {
			entity->properties.push_back({QStringLiteral("origin"), command->newValue, 0});
		}
		entity->origin = origin;
		if (LevelMapDoomThing* thing = document->format == LevelMapFormat::DoomWad ? thingById(document, objectId) : nullptr) {
			thing->x = origin.x;
			thing->y = origin.y;
			thing->z = origin.z;
			syncDoomThingEntity(document, objectId);
		}
		return true;
	}

	if (document->format == LevelMapFormat::DoomWad && (kind == QStringLiteral("thing") || kind == QStringLiteral("entity"))) {
		if (const LevelMapDoomThing* thing = thingById(document, objectId)) {
			if (const QString problem = doomThingPlaceProblem(*document, thing->x + dx, thing->y + dy, thing->z + dz); !problem.isEmpty()) {
				if (error) {
					*error = QCoreApplication::translate("VibeStudioLevelMap", "Thing %1 cannot move there: %2").arg(objectId).arg(problem);
				}
				return false;
			}
		}
	}
	if (kind == QStringLiteral("brush")) {
		if (const LevelMapBrush* brush = brushById(document, objectId); brush && brushFaceLinesShared(*document, *brush)) {
			if (error) {
				*error = sharedFaceLinesText(objectId);
			}
			return false;
		}
	}
	if (kind == QStringLiteral("vertex") || kind == QStringLiteral("linedef") || kind == QStringLiteral("thing")
		|| kind == QStringLiteral("brush") || kind == QStringLiteral("patch")) {
		if (!applyMoveDelta(document, kind, objectId, dx, dy, dz)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Move target was not found.");
			}
			return false;
		}
		return true;
	}

	if (error) {
		*error = QCoreApplication::translate("VibeStudioLevelMap", "Move supports entity, vertex, linedef, thing, brush or patch.");
	}
	return false;
}

// Drops selection members whose objects are gone and re-derives the flags, so
// an add or a delete never leaves the selection naming a missing object.
void pruneLevelMapSelection(LevelMapDocument* document)
{
	LevelMapObjectExistence existing(document);
	QVector<LevelMapSelectionRef> kept;
	for (const LevelMapSelectionRef& ref : document->selection) {
		if (existing.contains(ref)) {
			kept.push_back(ref);
		}
	}
	document->selection = kept;
	applySelectionFlags(document);
}

QStringList truncatedLines(const QStringList& source, int limit, const QString& label, qsizetype sourceCount = -1)
{
	const auto total = sourceCount < 0 ? source.size() : sourceCount;
	if (total <= limit) {
		return source;
	}
	QStringList lines = source.mid(0, limit);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "... showing %1 of %2 %3.").arg(limit).arg(total).arg(label);
	return lines;
}

} // namespace

bool LevelMapSaveReport::succeeded() const
{
	return errors.isEmpty() && (dryRun || written);
}

QString levelMapFormatId(LevelMapFormat format)
{
	switch (format) {
	case LevelMapFormat::Unknown:
		return QStringLiteral("unknown");
	case LevelMapFormat::DoomWad:
		return QStringLiteral("doom-wad");
	case LevelMapFormat::QuakeMap:
		return QStringLiteral("quake-map");
	case LevelMapFormat::Quake3Map:
		return QStringLiteral("quake3-map");
	}
	return QStringLiteral("unknown");
}

QString levelMapFormatDisplayName(LevelMapFormat format)
{
	switch (format) {
	case LevelMapFormat::Unknown:
		return QCoreApplication::translate("VibeStudioLevelMap", "Unknown");
	case LevelMapFormat::DoomWad:
		return QCoreApplication::translate("VibeStudioLevelMap", "Doom WAD map");
	case LevelMapFormat::QuakeMap:
		return QCoreApplication::translate("VibeStudioLevelMap", "Quake-family MAP");
	case LevelMapFormat::Quake3Map:
		return QCoreApplication::translate("VibeStudioLevelMap", "Quake III MAP");
	}
	return QCoreApplication::translate("VibeStudioLevelMap", "Unknown");
}

QString levelMapDoomFormatId(LevelMapDoomFormat format)
{
	switch (format) {
	case LevelMapDoomFormat::Doom:
		return QStringLiteral("doom");
	case LevelMapDoomFormat::Hexen:
		return QStringLiteral("hexen");
	case LevelMapDoomFormat::Udmf:
		return QStringLiteral("udmf");
	}
	return QStringLiteral("doom");
}

QString levelMapDoomFormatDisplayName(LevelMapDoomFormat format)
{
	switch (format) {
	case LevelMapDoomFormat::Doom:
		return QCoreApplication::translate("VibeStudioLevelMap", "Doom binary");
	case LevelMapDoomFormat::Hexen:
		return QCoreApplication::translate("VibeStudioLevelMap", "Hexen binary");
	case LevelMapDoomFormat::Udmf:
		return QCoreApplication::translate("VibeStudioLevelMap", "UDMF text");
	}
	return QCoreApplication::translate("VibeStudioLevelMap", "Doom binary");
}

QString levelMapIssueSeverityId(LevelMapIssueSeverity severity)
{
	switch (severity) {
	case LevelMapIssueSeverity::Info:
		return QStringLiteral("info");
	case LevelMapIssueSeverity::Warning:
		return QStringLiteral("warning");
	case LevelMapIssueSeverity::Error:
		return QStringLiteral("error");
	}
	return QStringLiteral("warning");
}

QString levelMapSelectionKindId(LevelMapSelectionKind kind)
{
	switch (kind) {
	case LevelMapSelectionKind::None:
		return QStringLiteral("none");
	case LevelMapSelectionKind::Entity:
		return QStringLiteral("entity");
	case LevelMapSelectionKind::DoomVertex:
		return QStringLiteral("vertex");
	case LevelMapSelectionKind::DoomLinedef:
		return QStringLiteral("linedef");
	case LevelMapSelectionKind::DoomThing:
		return QStringLiteral("thing");
	case LevelMapSelectionKind::DoomSector:
		return QStringLiteral("sector");
	case LevelMapSelectionKind::QuakeBrush:
		return QStringLiteral("brush");
	case LevelMapSelectionKind::QuakePatch:
		return QStringLiteral("patch");
	}
	return QStringLiteral("none");
}

LevelMapSelectionKind levelMapSelectionKindFromId(const QString& id)
{
	const QString kind = normalizedId(id);
	if (kind == QStringLiteral("entity")) {
		return LevelMapSelectionKind::Entity;
	}
	if (kind == QStringLiteral("vertex")) {
		return LevelMapSelectionKind::DoomVertex;
	}
	if (kind == QStringLiteral("linedef")) {
		return LevelMapSelectionKind::DoomLinedef;
	}
	if (kind == QStringLiteral("thing")) {
		return LevelMapSelectionKind::DoomThing;
	}
	if (kind == QStringLiteral("sector")) {
		return LevelMapSelectionKind::DoomSector;
	}
	if (kind == QStringLiteral("brush")) {
		return LevelMapSelectionKind::QuakeBrush;
	}
	if (kind == QStringLiteral("patch")) {
		return LevelMapSelectionKind::QuakePatch;
	}
	return LevelMapSelectionKind::None;
}

QString levelMapSelectionRefId(const LevelMapSelectionRef& ref)
{
	return QStringLiteral("%1:%2").arg(levelMapSelectionKindId(ref.kind)).arg(ref.objectId);
}

bool levelMapSelectionKindIsMovable(LevelMapSelectionKind kind)
{
	// Sectors are defined by the linedefs around them and carry no position of
	// their own, so they are selectable but not directly movable.
	return kind != LevelMapSelectionKind::None && kind != LevelMapSelectionKind::DoomSector;
}

double snapLevelMapCoordinate(double value, double gridSize)
{
	if (!std::isfinite(value) || !std::isfinite(gridSize) || gridSize <= 0.0) {
		return value;
	}
	// std::round breaks ties away from zero, which makes the grid symmetric
	// about the origin: -24 and 24 on a 16-unit grid land on -32 and 32.
	const double snapped = std::round(value / gridSize) * gridSize;
	// Fold -0.0 back to 0.0 so serialized coordinates never read as "-0".
	return snapped == 0.0 ? 0.0 : snapped;
}

LevelMapVec3 snapLevelMapPosition(const LevelMapVec3& position, double gridSize)
{
	if (!position.valid) {
		return position;
	}
	LevelMapVec3 result = position;
	result.x = snapLevelMapCoordinate(position.x, gridSize);
	result.y = snapLevelMapCoordinate(position.y, gridSize);
	result.z = snapLevelMapCoordinate(position.z, gridSize);
	return result;
}

LevelMapVec3 snapLevelMapDelta(const LevelMapVec3& delta, double gridSize)
{
	return snapLevelMapPosition(delta, gridSize);
}

bool isLevelDoomMapMarkerAt(const QVector<LevelMapWadSourceLump>& lumps, int index)
{
	return isDoomMapMarkerAt(lumps, index);
}

LevelDoomWadNodeReport inspectLevelDoomWadNodes(const QString& path, const QStringList& maps,
	const std::function<bool()>& isCancelled)
{
	LevelDoomWadNodeReport result;
	LevelMapLoadRequest request;
	request.path = path;
	request.isCancelled = isCancelled;
	try {
		loadCancellationCheckpoint(&request);
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly) || file.size() > kLevelMapMaxDocumentBytes) {
			result.errors << QCoreApplication::translate("LevelDoomNodes", "Unable to read the WAD, or it exceeds the 512 MiB document limit.");
			return result;
		}
		const QFileInfo before(file);
		const auto size = file.size();
		const auto modified = before.lastModified();
		QByteArray bytes;
		QCryptographicHash hash(QCryptographicHash::Sha256);
		while (!file.atEnd()) {
			loadCancellationCheckpoint(&request);
			const auto chunk = file.read(1024 * 1024);
			if (file.error() != QFileDevice::NoError || chunk.isEmpty() || bytes.size() > kLevelMapMaxDocumentBytes - chunk.size()) {
				result.errors << QCoreApplication::translate("LevelDoomNodes", "The WAD could not be read completely.");
				return result;
			}
			hash.addData(chunk);
			bytes.append(chunk);
		}
		QString magic, error;
		const auto lumps = readWadLumpsBytes(bytes, &magic, &error, &request);
		if (lumps.isEmpty()) { result.errors << error; return result; }
		QStringList requested;
		for (const auto& name : maps) { requested << name.trimmed().toUpper(); }
		requested.removeDuplicates();
		QMap<QString, int> markers;
		for (int i = 0; i < lumps.size(); ++i) {
			if ((i & 255) == 0) { loadCancellationCheckpoint(&request); }
			if (!isDoomMapMarkerAt(lumps, i) || (!requested.isEmpty() && !requested.contains(lumps[i].name))) { continue; }
			if (markers.contains(lumps[i].name)) {
				result.errors << QCoreApplication::translate("LevelDoomNodes", "Map label %1 is duplicated; select an unambiguous WAD before testing.").arg(lumps[i].name);
				return result;
			}
			markers.insert(lumps[i].name, i);
		}
		for (const auto& name : requested) {
			if (!markers.contains(name)) { result.errors << QCoreApplication::translate("LevelDoomNodes", "Requested map %1 was not found in the WAD.").arg(name); }
		}
		if (markers.isEmpty() && result.errors.isEmpty()) { result.errors << QCoreApplication::translate("LevelDoomNodes", "No Doom maps were found in the WAD."); }
		if (!result.errors.isEmpty()) { return result; }
		for (auto it = markers.cbegin(); it != markers.cend(); ++it) {
			loadCancellationCheckpoint(&request);
			LevelMapDocument document;
			if (!parseDoomWadGroup(request, lumps, magic, it.value(), &document, &error)) { result.errors << error; return result; }
			const auto report = inspectLevelDoomNodes(document, isCancelled);
			loadCancellationCheckpoint(&request);
			result.maps.insert(it.key(), report);
			const auto message = it.key() + QStringLiteral(": ") + report.message;
			if (report.needsBuild()) { result.errors << message; }
			else if (report.state == LevelDoomNodeState::Unsupported) { result.warnings << message; }
			for (const auto& warning : report.warnings) { result.warnings << it.key() + QStringLiteral(": ") + warning; }
		}
		const QFileInfo after(path);
		if (!after.isFile() || after.size() != size || after.lastModified() != modified || bytes.size() != size) {
			result.errors << QCoreApplication::translate("LevelDoomNodes", "The WAD changed during validation. Validate the current file again.");
			return result;
		}
		result.sourceHash = hash.result();
	} catch (const MapLoadCancelled&) {
		result.cancelled = true;
		result.errors << QCoreApplication::translate("LevelDoomNodes", "Node validation cancelled.");
	}
	return result;
}

bool loadLevelMap(const LevelMapLoadRequest& request, LevelMapDocument* document, QString* error)
{
	if (error) { error->clear(); }
	if (!document || request.path.trimmed().isEmpty()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Map path and document output are required."); }
		return false;
	}
	try {
		loadCheckpoint(&request, LevelMapLoadPhase::Reading);
		QFile file(request.path);
		if (!file.open(QIODevice::ReadOnly)) {
			if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Unable to open the map: %1").arg(file.errorString()); }
			return false;
		}
		const QFileInfo before(file);
		const QDateTime sourceModified = before.lastModified();
		const qint64 sourceSize = file.size();
		if (sourceSize > kLevelMapMaxDocumentBytes) {
			if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "The map exceeds the 512 MiB document limit."); }
			return false;
		}
		QByteArray bytes;
		while (!file.atEnd()) {
			loadCheckpoint(&request, LevelMapLoadPhase::Reading, bytes.size(), sourceSize);
			const auto chunk = file.read(1024 * 1024);
			if (file.error() != QFileDevice::NoError) { if (error) { *error = file.errorString(); } return false; }
			if (chunk.isEmpty()) { break; }
			if (bytes.size() > kLevelMapMaxDocumentBytes - chunk.size()) {
				if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "The map exceeds the 512 MiB document limit."); }
				return false;
			}
			bytes.append(chunk);
		}
		loadCheckpoint(&request, LevelMapLoadPhase::Reading, bytes.size(), sourceSize);
		LevelMapDocument candidate;
		if (!loadLevelMapBytes(request, bytes, &candidate, error)) { return false; }
		const QFileInfo after(request.path);
		if (!after.exists() || sourceSize != after.size() || sourceModified != after.lastModified()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "The source changed while the map was opening. Open it again to read the current file."); }
			return false;
		}
		loadCheckpoint(&request, LevelMapLoadPhase::Complete, 1, 1);
		*document = std::move(candidate);
		return true;
	} catch (const MapLoadCancelled&) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Map loading cancelled."); }
		return false;
	}
}

bool loadLevelMapBytes(const LevelMapLoadRequest& request, const QByteArray& bytes, LevelMapDocument* document, QString* error)
{
	if (error) { error->clear(); }
	if (!document || bytes.size() > kLevelMapMaxDocumentBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Map output is unavailable, or the input exceeds 512 MiB."); }
		return false;
	}
	try {
		loadCheckpoint(&request, LevelMapLoadPhase::Indexing);
		const QFileInfo info(request.path);
		const QString suffix = info.suffix().toLower();
		const QString hint = normalizedId(request.engineHint);
		LevelMapDocument parsed;
		if (suffix == QStringLiteral("wad") || hint.contains(QStringLiteral("doom")) || hint.contains(QStringLiteral("idtech1"))) {
			if (!parseDoomWad(request, bytes, &parsed, error)) { return false; }
			parsed.doomNodeReport = std::make_shared<const LevelDoomNodeReport>(inspectLevelDoomNodes(parsed, request.isCancelled));
			loadCancellationCheckpoint(&request);
			if (parsed.doomLumps.contains(QStringLiteral("VS_SCENE"))) {
				parsed.scene = decodeLevelScene(parsed, parsed.doomLumps.value(QStringLiteral("VS_SCENE")), levelSceneDoomHash(parsed.doomLumps));
			}
			if (request.brushGeometryCache) { request.brushGeometryCache->beginBuild(parsed); }
		} else if (suffix == QStringLiteral("map")) {
			const QByteArray marker("\n// VibeStudioScene: ");
			const auto trailer = bytes.lastIndexOf(marker);
			QByteArray body = bytes;
			QByteArray metadata;
			bool hasMetadata = false;
			if (trailer >= 0) {
				auto tail = bytes.mid(trailer + marker.size());
				if (tail.endsWith('\n')) { tail.chop(1); }
				if (tail.endsWith('\r')) { tail.chop(1); }
				if (!tail.contains('\n') && !tail.contains('\r')) { body = bytes.left(trailer); metadata = tail; hasMetadata = true; }
			}
			parseQuakeMapText(QString::fromUtf8(body), request.path, request.engineHint, &parsed, &request);
			if (hasMetadata) { parsed.scene = decodeLevelScene(parsed, metadata, QCryptographicHash::hash(body, QCryptographicHash::Sha256)); }
		} else {
			if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Supported map inputs are Doom WAD and Quake-family .map files."); }
			return false;
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		for (qsizetype offset = 0; offset < bytes.size(); offset += 1024 * 1024) {
			loadCheckpoint(&request, LevelMapLoadPhase::Hashing, offset, bytes.size());
			hash.addData(QByteArrayView(bytes).sliced(offset, std::min<qsizetype>(1024 * 1024, bytes.size() - offset)));
		}
		parsed.sourceContentHash = hash.result();
		if (!parsed.scene.problem.isEmpty()) { addIssue(&parsed, LevelMapIssueSeverity::Warning, QStringLiteral("scene-metadata"), parsed.scene.problem); }
		parsed.savedUndoDepth = 0;
		loadCheckpoint(&request, LevelMapLoadPhase::Complete, 1, 1);
		*document = std::move(parsed);
		return true;
	} catch (const MapLoadCancelled&) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "Map loading cancelled."); }
		return false;
	}
}

QStringList levelMapTextureNames(const LevelMapDocument& document)
{
	QStringList values;
	QSet<QString> spellings, folded;
	for (const QString& texture : document.textureReferences) {
		// Most surfaces repeat a small material palette. Normalize each spelling
		// once, and avoid a linear scan of every name already found.
		if (spellings.contains(texture)) { continue; }
		spellings.insert(texture);
		const QString trimmed = texture.trimmed();
		if (trimmed.isEmpty() || trimmed == QStringLiteral("-")) { continue; }
		const QString key = trimmed.toCaseFolded();
		if (folded.contains(key)) { continue; }
		folded.insert(key);
		values.push_back(trimmed);
	}
	std::sort(values.begin(), values.end(), [](const QString& left, const QString& right) {
		return left.compare(right, Qt::CaseInsensitive) < 0;
	});
	return values;
}

namespace {
LevelMapStatistics statisticsWithTextureCount(const LevelMapDocument& document, int uniqueTextureCount)
{
	LevelMapStatistics stats;
	stats.entityCount = document.entities.size();
	stats.brushCount = document.brushes.size();
	stats.patchCount = document.patches.size();
	stats.doomThingCount = document.doomThings.size();
	stats.doomVertexCount = document.doomVertices.size();
	stats.doomLinedefCount = document.doomLinedefs.size();
	stats.doomSidedefCount = document.doomSidedefs.size();
	stats.doomSectorCount = document.doomSectors.size();
	stats.textureReferenceCount = document.textureReferences.size();
	stats.uniqueTextureCount = uniqueTextureCount;
	for (const LevelMapBrush& brush : document.brushes) {
		stats.brushFaceCount += brush.faces.isEmpty() ? brush.faceCount : brush.faces.size();
		if (brush.boundsSolved) {
			++stats.solvedBrushCount;
		} else {
			++stats.degenerateBrushCount;
		}
		includePoint(&stats.mins, &stats.maxs, brush.mins);
		includePoint(&stats.mins, &stats.maxs, brush.maxs);
	}
	for (const LevelMapPatch& patch : document.patches) {
		includePoint(&stats.mins, &stats.maxs, patch.mins);
		includePoint(&stats.mins, &stats.maxs, patch.maxs);
	}
	for (const LevelMapDoomVertex& vertex : document.doomVertices) {
		includePoint(&stats.mins, &stats.maxs, {vertex.x, vertex.y, 0.0, true});
	}
	for (const LevelMapDoomThing& thing : document.doomThings) {
		includePoint(&stats.mins, &stats.maxs, {thing.x, thing.y, thing.z, true});
	}
	for (const LevelMapEntity& entity : document.entities) {
		includePoint(&stats.mins, &stats.maxs, entity.origin);
	}
	stats.issueCount = document.issues.size();
	const auto nodeReport = inspectLevelDoomNodes(document);
	if (nodeReport.needsBuild() || nodeReport.state == LevelDoomNodeState::Unsupported) { ++stats.issueCount; ++stats.warningCount; }
	stats.issueCount += nodeReport.warnings.size();
	stats.warningCount += nodeReport.warnings.size();
	for (const LevelMapIssue& issue : document.issues) {
		if (issue.severity == LevelMapIssueSeverity::Error) {
			++stats.errorCount;
		} else if (issue.severity == LevelMapIssueSeverity::Warning) {
			++stats.warningCount;
		}
	}
	return stats;
}
} // namespace

LevelMapStatistics levelMapStatistics(const LevelMapDocument& document)
{
	return statisticsWithTextureCount(document, levelMapTextureNames(document).size());
}

LevelMapInspectionSummary levelMapInspectionSummary(const LevelMapDocument& document)
{
	LevelMapInspectionSummary summary;
	summary.textureNames = levelMapTextureNames(document);
	summary.statistics = statisticsWithTextureCount(document, summary.textureNames.size());
	return summary;
}

QStringList levelMapStatisticsLines(const LevelMapDocument& document)
{
	return levelMapStatisticsLines(document, levelMapStatistics(document));
}

QStringList levelMapStatisticsLines(const LevelMapDocument& document, const LevelMapStatistics& stats)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Format: %1").arg(levelMapFormatDisplayName(document.format));
	if (document.format == LevelMapFormat::DoomWad) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Map format: %1").arg(levelMapDoomFormatDisplayName(document.doomFormat));
		lines << QCoreApplication::translate("VibeStudioLevelMap", "WAD kind: %1").arg(document.doomWadMagic);
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Engine: %1").arg(document.engineFamily.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "unknown") : document.engineFamily);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Map: %1").arg(document.mapName.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "unknown") : document.mapName);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Entities: %1").arg(stats.entityCount);
	if (isTextMapFormat(document.format)) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Target links: %1").arg(levelMapTargetLinks(document).size());
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Brushes: %1 (%2 solved, %3 degenerate)").arg(stats.brushCount).arg(stats.solvedBrushCount).arg(stats.degenerateBrushCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Brush faces: %1").arg(stats.brushFaceCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Patches: %1").arg(stats.patchCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Doom things: %1").arg(stats.doomThingCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Doom vertices: %1").arg(stats.doomVertexCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Doom linedefs: %1").arg(stats.doomLinedefCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Doom sidedefs: %1").arg(stats.doomSidedefCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Doom sectors: %1").arg(stats.doomSectorCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Texture references: %1 / %2 unique").arg(stats.textureReferenceCount).arg(stats.uniqueTextureCount);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Bounds min: %1").arg(vecText(stats.mins));
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Bounds max: %1").arg(vecText(stats.maxs));
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Issues: %1 warnings, %2 errors").arg(stats.warningCount).arg(stats.errorCount);
	return lines;
}

QStringList levelMapEntityLines(const LevelMapDocument& document)
{
	QStringList lines;
	for (const LevelMapEntity& entity : document.entities) {
		lines << QStringLiteral("%1 %2 origin=%3 props=%4")
			.arg(entityObjectId(entity.id), entity.className.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "entity") : entity.className, vecText(entity.origin))
			.arg(entity.properties.size());
	}
	for (const LevelMapDoomThing& thing : document.doomThings) {
		lines << QStringLiteral("%1 type=%2 at %3 angle=%4 flags=%5")
			.arg(thingObjectId(thing.id))
			.arg(thing.type)
			.arg(doomPointText(thing.x, thing.y))
			.arg(thing.angle)
			.arg(thing.flags);
	}
	if (lines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "No entities or things parsed.");
	}
	return lines;
}

QStringList levelMapTextureLines(const LevelMapDocument& document)
{
	const QStringList unique = levelMapTextureNames(document);
	if (unique.isEmpty()) {
		return {QCoreApplication::translate("VibeStudioLevelMap", "No texture or material references parsed.")};
	}
	QStringList lines;
	for (const QString& texture : unique) {
		lines << texture;
	}
	return lines;
}

QStringList levelMapValidationLines(const LevelMapDocument& document)
{
	QStringList lines;
	const auto nodes = inspectLevelDoomNodes(document);
	if (nodes.needsBuild() || nodes.state == LevelDoomNodeState::Unsupported) {
		lines << QStringLiteral("WARNING doom-nodes-%1 %2").arg(levelDoomNodeStateId(nodes.state), nodes.message);
	}
	for (const auto& warning : nodes.warnings) { lines << QStringLiteral("WARNING doom-node-runtime %1").arg(warning); }
	for (const LevelMapIssue& issue : document.issues) {
		lines << QStringLiteral("%1 %2 %3%4%5")
			.arg(levelMapIssueSeverityId(issue.severity).toUpper(), issue.code, issue.message,
				issue.objectId.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(issue.objectId),
				issue.line > 0 ? QCoreApplication::translate("VibeStudioLevelMap", " at line %1").arg(issue.line) : QString());
	}
	if (lines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "No validation problems.");
	}
	return lines;
}

QStringList levelMapSectorLines(const LevelMapDocument& document)
{
	QStringList lines;
	for (const LevelMapDoomSector& sector : document.doomSectors) {
		lines << QStringLiteral("%1 floor=%2 ceiling=%3 flats=%4/%5 light=%6 special=%7 tag=%8")
			.arg(sectorObjectId(sector.id))
			.arg(sector.floorHeight)
			.arg(sector.ceilingHeight)
			.arg(sector.floorTexture, sector.ceilingTexture)
			.arg(sector.lightLevel)
			.arg(sector.special)
			.arg(sector.tag);
	}
	if (lines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "No Doom sectors parsed.");
	}
	return lines;
}

QStringList levelMapSidedefLines(const LevelMapDocument& document)
{
	QStringList lines;
	for (const LevelMapDoomSidedef& sidedef : document.doomSidedefs) {
		lines << QStringLiteral("%1 sector=%2 offset=%3,%4 upper=%5 lower=%6 middle=%7")
			.arg(sidedefObjectId(sidedef.id))
			.arg(sidedef.sector)
			.arg(sidedef.offsetX)
			.arg(sidedef.offsetY)
			.arg(sidedef.upperTexture.isEmpty() ? QStringLiteral("-") : sidedef.upperTexture,
				sidedef.lowerTexture.isEmpty() ? QStringLiteral("-") : sidedef.lowerTexture,
				sidedef.middleTexture.isEmpty() ? QStringLiteral("-") : sidedef.middleTexture);
	}
	if (lines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "No Doom sidedefs parsed.");
	}
	return lines;
}

QStringList levelMapPatchLines(const LevelMapDocument& document)
{
	QStringList lines;
	for (const LevelMapPatch& patch : document.patches) {
		lines << QStringLiteral("%1 entity=%2 shader=%3 grid=%4x%5 points=%6 mins=%7 maxs=%8")
			.arg(patchObjectId(patch.id))
			.arg(patch.entityId)
			.arg(patch.textureName)
			.arg(patch.width)
			.arg(patch.height)
			.arg(patch.controlPoints.size())
			.arg(vecText(patch.mins), vecText(patch.maxs));
	}
	if (lines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "No Quake III patches parsed.");
	}
	return lines;
}

QStringList levelMapViewLines(const LevelMapDocument& document)
{
	// This summary is shared by the workbench, selection drawer and CLI. Format
	// only the visible prefix; constructing every discarded line stalls large
	// maps on each refresh, even though the display itself is bounded.
	QStringList lines;
	if (document.format == LevelMapFormat::DoomWad) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "[2D Doom map view]");
		QStringList linedefLines;
		for (const LevelMapDoomLinedef& linedef : document.doomLinedefs) {
			if (linedefLines.size() == 24) { break; }
			const LevelMapDoomVertex a = linedef.startVertex >= 0 && linedef.startVertex < document.doomVertices.size() ? document.doomVertices.at(linedef.startVertex) : LevelMapDoomVertex {};
			const LevelMapDoomVertex b = linedef.endVertex >= 0 && linedef.endVertex < document.doomVertices.size() ? document.doomVertices.at(linedef.endVertex) : LevelMapDoomVertex {};
			linedefLines << QStringLiteral("%1 %2 -> %3 sides=%4/%5 tag=%6 special=%7")
				.arg(linedefObjectId(linedef.id), doomPointText(a.x, a.y), doomPointText(b.x, b.y))
				.arg(linedef.frontSidedef)
				.arg(linedef.backSidedef)
				.arg(linedef.tag)
				.arg(linedef.special);
		}
		lines << truncatedLines(linedefLines, 24, QCoreApplication::translate("VibeStudioLevelMap", "linedefs"), document.doomLinedefs.size());
		QStringList thingLines;
		for (const LevelMapDoomThing& thing : document.doomThings) {
			if (thingLines.size() == 16) { break; }
			thingLines << QStringLiteral("%1 thing type=%2 at %3").arg(thingObjectId(thing.id)).arg(thing.type).arg(doomPointText(thing.x, thing.y));
		}
		lines << truncatedLines(thingLines, 16, QCoreApplication::translate("VibeStudioLevelMap", "things"), document.doomThings.size());
		QStringList sectorLines;
		for (const LevelMapDoomSector& sector : document.doomSectors) {
			if (sectorLines.size() == 12) { break; }
			sectorLines << QStringLiteral("%1 floor=%2 ceiling=%3 light=%4").arg(sectorObjectId(sector.id)).arg(sector.floorHeight).arg(sector.ceilingHeight).arg(sector.lightLevel);
		}
		lines << truncatedLines(sectorLines, 12, QCoreApplication::translate("VibeStudioLevelMap", "sectors"), document.doomSectors.size());
	} else {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "[orthographic brush/entity preview]");
		QStringList brushLines;
		for (const LevelMapBrush& brush : document.brushes) {
			if (brushLines.size() == 24) { break; }
			brushLines << QStringLiteral("%1 entity=%2 kind=%3 faces=%4 solved=%5 mins=%6 maxs=%7")
				.arg(brushObjectId(brush.id))
				.arg(brush.entityId)
				.arg(brush.primitiveKind.isEmpty() ? QStringLiteral("classic") : brush.primitiveKind)
				.arg(brush.faceCount)
				.arg(brush.boundsSolved ? QCoreApplication::translate("VibeStudioLevelMap", "yes") : QCoreApplication::translate("VibeStudioLevelMap", "no"))
				.arg(vecText(brush.mins), vecText(brush.maxs));
		}
		lines << truncatedLines(brushLines, 24, QCoreApplication::translate("VibeStudioLevelMap", "brushes"), document.brushes.size());
		QStringList patchLines;
		for (const LevelMapPatch& patch : document.patches) {
			if (patchLines.size() == 12) { break; }
			patchLines << QStringLiteral("%1 shader=%2 grid=%3x%4").arg(patchObjectId(patch.id), patch.textureName).arg(patch.width).arg(patch.height);
		}
		if (!document.patches.isEmpty()) {
			lines << truncatedLines(patchLines, 12, QCoreApplication::translate("VibeStudioLevelMap", "patches"), document.patches.size());
		}
		QStringList entityLines;
		for (const LevelMapEntity& entity : document.entities) {
			if (entityLines.size() == 16) { break; }
			entityLines << QStringLiteral("%1 %2 origin=%3").arg(entityObjectId(entity.id), entity.className, vecText(entity.origin));
		}
		lines << truncatedLines(entityLines, 16, QCoreApplication::translate("VibeStudioLevelMap", "entities"), document.entities.size());
	}
	if (lines.size() == 1) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "No geometry available for the current map preview.");
	}
	return lines;
}

QStringList levelMapSelectionLines(const LevelMapDocument& document)
{
	if (document.selectionKind == LevelMapSelectionKind::None || document.selectedObjectId < 0) {
		return {QCoreApplication::translate("VibeStudioLevelMap", "Selection: none")};
	}
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Selection: %1:%2").arg(levelMapSelectionKindId(document.selectionKind)).arg(document.selectedObjectId);
	// Single selection prints exactly what it always did; the extra lines only
	// appear once a set is actually holding more than the primary member.
	if (document.selection.size() > 1) {
		lines << levelMapSelectionSetLines(document);
	}
	if (document.selectionKind == LevelMapSelectionKind::QuakeBrush) {
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id != document.selectedObjectId) {
				continue;
			}
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Brush kind: %1").arg(brush.primitiveKind.isEmpty() ? QStringLiteral("classic") : brush.primitiveKind);
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Faces: %1").arg(brush.faceCount);
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Bounds: %1 .. %2 (%3)").arg(vecText(brush.mins), vecText(brush.maxs), brush.boundsSolved ? QCoreApplication::translate("VibeStudioLevelMap", "solved") : QCoreApplication::translate("VibeStudioLevelMap", "unsolved"));
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Textures: %1").arg(brush.textureNames.join(QStringLiteral(", ")));
			// Each face as `map edit` names its fields: faceN.texture and so on.
			for (const LevelMapBrushFace& face : brush.faces) {
				lines << QCoreApplication::translate("VibeStudioLevelMap", "face%1: texture=%2 shiftx=%3 shifty=%4 rotation=%5 scalex=%6 scaley=%7")
						 .arg(face.id + 1)
						 .arg(face.textureName,
							 mapCoordinateText(face.explicitTextureAxes ? face.uOffset : face.shiftX),
							 mapCoordinateText(face.explicitTextureAxes ? face.vOffset : face.shiftY),
							 mapCoordinateText(face.rotation), mapCoordinateText(face.scaleX), mapCoordinateText(face.scaleY));
			}
		}
	} else if (document.selectionKind == LevelMapSelectionKind::QuakePatch) {
		for (const LevelMapPatch& patch : document.patches) {
			if (patch.id != document.selectedObjectId) {
				continue;
			}
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Patch shader: %1").arg(patch.textureName);
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Control grid: %1 x %2 (%3 points)").arg(patch.width).arg(patch.height).arg(patch.controlPoints.size());
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Bounds: %1 .. %2").arg(vecText(patch.mins), vecText(patch.maxs));
		}
	} else if (document.selectionKind == LevelMapSelectionKind::DoomSector) {
		for (const LevelMapDoomSector& sector : document.doomSectors) {
			if (sector.id != document.selectedObjectId) {
				continue;
			}
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Sector heights: %1 .. %2").arg(sector.floorHeight).arg(sector.ceilingHeight);
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Flats: %1 / %2").arg(sector.floorTexture, sector.ceilingTexture);
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Light/special/tag: %1 / %2 / %3").arg(sector.lightLevel).arg(sector.special).arg(sector.tag);
		}
	}
	return lines;
}

QStringList levelMapPropertyLines(const LevelMapDocument& document)
{
	QStringList lines;
	if (document.selectionKind == LevelMapSelectionKind::Entity) {
		for (const LevelMapEntity& entity : document.entities) {
			if (entity.id != document.selectedObjectId) {
				continue;
			}
			lines << QCoreApplication::translate("VibeStudioLevelMap", "Entity: %1").arg(entity.className);
			for (const LevelMapProperty& property : entity.properties) {
				lines << QStringLiteral("%1 = %2").arg(property.key, property.value);
			}
			return lines;
		}
	}
	if (document.selectionKind == LevelMapSelectionKind::DoomVertex && document.selectedObjectId >= 0 && document.selectedObjectId < document.doomVertices.size()) {
		const LevelMapDoomVertex& vertex = document.doomVertices.at(document.selectedObjectId);
		return {QCoreApplication::translate("VibeStudioLevelMap", "Vertex: %1").arg(vertexObjectId(vertex.id)), QCoreApplication::translate("VibeStudioLevelMap", "Position: %1").arg(doomPointText(vertex.x, vertex.y))};
	}
	if (document.selectionKind == LevelMapSelectionKind::DoomLinedef && document.selectedObjectId >= 0 && document.selectedObjectId < document.doomLinedefs.size()) {
		const LevelMapDoomLinedef& linedef = document.doomLinedefs.at(document.selectedObjectId);
		QStringList result;
		result << QCoreApplication::translate("VibeStudioLevelMap", "Linedef: %1").arg(linedefObjectId(linedef.id));
		result << QCoreApplication::translate("VibeStudioLevelMap", "Vertices: %1 -> %2").arg(linedef.startVertex).arg(linedef.endVertex);
		result << QCoreApplication::translate("VibeStudioLevelMap", "Sidedefs: %1 / %2").arg(linedef.frontSidedef).arg(linedef.backSidedef);
		result << QCoreApplication::translate("VibeStudioLevelMap", "Special/tag: %1 / %2").arg(linedef.special).arg(linedef.tag);
		if (document.doomFormat == LevelMapDoomFormat::Hexen) {
			result << QCoreApplication::translate("VibeStudioLevelMap", "Args: %1 %2 %3 %4 %5").arg(linedef.args[0]).arg(linedef.args[1]).arg(linedef.args[2]).arg(linedef.args[3]).arg(linedef.args[4]);
		}
		return result;
	}
	const auto selectedThing = std::find_if(document.doomThings.cbegin(), document.doomThings.cend(),
		[&document](const LevelMapDoomThing& thing) { return thing.id == document.selectedObjectId; });
	if (document.selectionKind == LevelMapSelectionKind::DoomThing && selectedThing != document.doomThings.cend()) {
		const LevelMapDoomThing& thing = *selectedThing;
		QStringList result;
		result << QCoreApplication::translate("VibeStudioLevelMap", "Thing: %1").arg(thingObjectId(thing.id));
		result << QCoreApplication::translate("VibeStudioLevelMap", "Type: %1").arg(thing.type);
		result << QCoreApplication::translate("VibeStudioLevelMap", "Position: %1").arg(doomPointText(thing.x, thing.y));
		if (document.doomFormat == LevelMapDoomFormat::Hexen) {
			result << QCoreApplication::translate("VibeStudioLevelMap", "Height: %1").arg(thing.z, 0, 'f', 1);
			result << QCoreApplication::translate("VibeStudioLevelMap", "Thing id: %1").arg(thing.tid);
			result << QCoreApplication::translate("VibeStudioLevelMap", "Special: %1").arg(thing.special);
			result << QCoreApplication::translate("VibeStudioLevelMap", "Args: %1 %2 %3 %4 %5").arg(thing.args[0]).arg(thing.args[1]).arg(thing.args[2]).arg(thing.args[3]).arg(thing.args[4]);
		}
		return result;
	}
	if (document.selectionKind == LevelMapSelectionKind::DoomSector && document.selectedObjectId >= 0 && document.selectedObjectId < document.doomSectors.size()) {
		const LevelMapDoomSector& sector = document.doomSectors.at(document.selectedObjectId);
		return {
			QCoreApplication::translate("VibeStudioLevelMap", "Sector: %1").arg(sectorObjectId(sector.id)),
			QCoreApplication::translate("VibeStudioLevelMap", "floorheight = %1").arg(sector.floorHeight),
			QCoreApplication::translate("VibeStudioLevelMap", "ceilingheight = %1").arg(sector.ceilingHeight),
			QCoreApplication::translate("VibeStudioLevelMap", "floortexture = %1").arg(sector.floorTexture),
			QCoreApplication::translate("VibeStudioLevelMap", "ceilingtexture = %1").arg(sector.ceilingTexture),
			QCoreApplication::translate("VibeStudioLevelMap", "lightlevel = %1").arg(sector.lightLevel),
			QCoreApplication::translate("VibeStudioLevelMap", "special = %1").arg(sector.special),
			QCoreApplication::translate("VibeStudioLevelMap", "tag = %1").arg(sector.tag),
		};
	}
	if (document.selectionKind == LevelMapSelectionKind::QuakeBrush || document.selectionKind == LevelMapSelectionKind::QuakePatch) {
		return levelMapSelectionLines(document);
	}
	return {QCoreApplication::translate("VibeStudioLevelMap", "No property inspector data for the current selection.")};
}

QStringList levelMapUndoLines(const LevelMapDocument& document)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Edit state: %1").arg(document.editState);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Undo commands: %1").arg(document.undoStack.size());
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Redo commands: %1").arg(document.redoStack.size());
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Undo depth limit: %1").arg(document.undoLimit);
	if (!document.undoStack.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Next undo: %1").arg(document.undoStack.back().undoDescription);
	}
	if (!document.redoStack.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Next redo: %1").arg(document.redoStack.back().description);
	}
	return lines;
}

QString levelMapReportText(const LevelMapDocument& document)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Level map");
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Source: %1").arg(document.sourcePath);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Format: %1").arg(levelMapFormatId(document.format));
	if (document.format == LevelMapFormat::DoomWad) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Doom map format: %1").arg(levelMapDoomFormatId(document.doomFormat));
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Map: %1").arg(document.mapName);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Statistics:");
	for (const QString& line : levelMapStatisticsLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "View:");
	for (const QString& line : levelMapViewLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Selection:");
	for (const QString& line : levelMapSelectionLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Properties:");
	for (const QString& line : levelMapPropertyLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	if (!document.patches.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Patches:");
		for (const QString& line : levelMapPatchLines(document)) {
			lines << QStringLiteral("- %1").arg(line);
		}
	}
	if (!document.doomSectors.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Sectors:");
		for (const QString& line : truncatedLines(levelMapSectorLines(document), 24, QCoreApplication::translate("VibeStudioLevelMap", "sectors"))) {
			lines << QStringLiteral("- %1").arg(line);
		}
	}
	if (!document.doomSidedefs.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Sidedefs:");
		for (const QString& line : truncatedLines(levelMapSidedefLines(document), 24, QCoreApplication::translate("VibeStudioLevelMap", "sidedefs"))) {
			lines << QStringLiteral("- %1").arg(line);
		}
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Textures/materials:");
	for (const QString& line : levelMapTextureLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Validation:");
	for (const QString& line : levelMapValidationLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Undo/redo:");
	for (const QString& line : levelMapUndoLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	return lines.join('\n');
}

bool selectLevelMapObject(LevelMapDocument* document, const QString& selector, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	const QStringList parts = selector.trimmed().split(':', Qt::SkipEmptyParts);
	if (parts.size() != 2) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Selection must be formatted as kind:id.");
		}
		return false;
	}
	const QString kind = normalizedId(parts.value(0));
	bool ok = false;
	const int id = parts.value(1).toInt(&ok);
	if (!ok || id < 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Selection id must be a non-negative integer.");
		}
		return false;
	}
	const LevelMapSelectionKind selectionKind = levelMapSelectionKindFromId(kind);
	if (selectionKind == LevelMapSelectionKind::None || !selectionObjectExists(document, selectionKind, id)) {
		// A failed selection leaves nothing selected, so the set, the per-record
		// flags and the primary fields all agree on "none".
		document->selection.clear();
		applySelectionFlags(document);
		if (error) {
			*error = selectionKind == LevelMapSelectionKind::None ? QCoreApplication::translate("VibeStudioLevelMap", "Unknown selection kind.")
									     : selectionNotFoundText(selectionKind);
		}
		return false;
	}
	document->selection = {LevelMapSelectionRef {selectionKind, id}};
	applySelectionFlags(document);
	return true;
}

int levelMapSelectionCount(const LevelMapDocument& document)
{
	return static_cast<int>(document.selection.size());
}

bool levelMapSelectionContains(const LevelMapDocument& document, LevelMapSelectionKind kind, int objectId)
{
	return document.selection.contains(LevelMapSelectionRef {kind, objectId});
}

LevelMapSelectionRef levelMapPrimarySelection(const LevelMapDocument& document)
{
	if (document.selection.isEmpty()) {
		return {};
	}
	return document.selection.back();
}

QStringList levelMapSelectionSetLines(const LevelMapDocument& document)
{
	if (document.selection.isEmpty()) {
		return {QCoreApplication::translate("VibeStudioLevelMap", "Selection set: empty")};
	}
	QStringList ids;
	ids.reserve(document.selection.size());
	for (const LevelMapSelectionRef& ref : document.selection) {
		ids << levelMapSelectionRefId(ref);
	}
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Selection set: %1 objects").arg(document.selection.size());
	lines << truncatedLines(ids, 24, QCoreApplication::translate("VibeStudioLevelMap", "selected objects"));
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Primary: %1").arg(levelMapSelectionRefId(levelMapPrimarySelection(document)));
	return lines;
}

bool addLevelMapSelection(LevelMapDocument* document, LevelMapSelectionKind kind, int objectId, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	if (kind == LevelMapSelectionKind::None || !selectionObjectExists(document, kind, objectId)) {
		if (error) {
			*error = selectionNotFoundText(kind);
		}
		return false;
	}
	const LevelMapSelectionRef ref {kind, objectId};
	// Re-adding an existing member promotes it to primary rather than
	// duplicating it, which is what a click on an already-selected object means.
	document->selection.removeAll(ref);
	document->selection.push_back(ref);
	applySelectionFlags(document);
	return true;
}

bool removeLevelMapSelection(LevelMapDocument* document, LevelMapSelectionKind kind, int objectId, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	const qsizetype removed = document->selection.removeAll(LevelMapSelectionRef {kind, objectId});
	if (removed <= 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "That object is not in the selection.");
		}
		return false;
	}
	applySelectionFlags(document);
	return true;
}

bool toggleLevelMapSelection(LevelMapDocument* document, LevelMapSelectionKind kind, int objectId, QString* error)
{
	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	if (levelMapSelectionContains(*document, kind, objectId)) {
		return removeLevelMapSelection(document, kind, objectId, error);
	}
	return addLevelMapSelection(document, kind, objectId, error);
}

void clearLevelMapSelection(LevelMapDocument* document)
{
	if (!document) {
		return;
	}
	document->selection.clear();
	applySelectionFlags(document);
}

bool setLevelMapSelection(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& selection, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	// Each object once, where the caller last named it, so their final entry
	// is primary: walk backwards keeping first sightings, then turn around.
	// Searching the document and the set so far for each reference made
	// selecting every brush of a large map quadratic.
	LevelMapObjectExistence existing(document);
	QVector<LevelMapSelectionRef> resolved;
	resolved.reserve(selection.size());
	QSet<QPair<int, int>> seen;
	bool skipped = false;
	for (auto it = selection.crbegin(); it != selection.crend(); ++it) {
		const LevelMapSelectionRef& ref = *it;
		if (!existing.contains(ref)) {
			skipped = true;
			continue;
		}
		const QPair<int, int> key(static_cast<int>(ref.kind), ref.objectId);
		if (seen.contains(key)) {
			continue;
		}
		seen.insert(key);
		resolved.push_back(ref);
	}
	std::reverse(resolved.begin(), resolved.end());
	document->selection = resolved;
	applySelectionFlags(document);
	if (skipped && error) {
		*error = QCoreApplication::translate("VibeStudioLevelMap", "Some objects in the requested selection were not found.");
	}
	return !skipped;
}

namespace {

// A Doom thing field's value as the WAD can hold it: a whole number in the
// range of its 16-bit field, or of the byte Hexen gives a special. Empty when
// the value will do or the key is not a thing field; z, tid and special are
// Hexen's alone.
QString doomThingFieldProblem(const QString& key, const QString& value, bool hexen)
{
	struct Range {
		const char* key;
		int low;
		int high;
		bool hexenOnly;
	};
	static const Range ranges[] = {
		{"type", 1, 65535, false},
		{"angle", -32768, 32767, false},
		{"flags", 0, 65535, false},
		{"x", -32768, 32767, false},
		{"y", -32768, 32767, false},
		{"z", -32768, 32767, true},
		{"tid", 0, 65535, true},
		{"special", 0, 255, true},
	};
	for (const Range& range : ranges) {
		if (key.compare(QLatin1String(range.key), Qt::CaseInsensitive) != 0) {
			continue;
		}
		if (range.hexenOnly && !hexen) {
			return QCoreApplication::translate("VibeStudioLevelMap", "A Doom-format thing has no %1; it belongs to Hexen-format maps.").arg(key.toLower());
		}
		bool ok = false;
		const int number = value.trimmed().toInt(&ok);
		if (!ok || number < range.low || number > range.high) {
			return QCoreApplication::translate("VibeStudioLevelMap", "A thing's %1 is a whole number from %2 to %3.").arg(key.toLower()).arg(range.low).arg(range.high);
		}
		return {};
	}
	// A thing has no other keys to write: anything else would be kept in
	// memory and lost on save. The origin is checked as a place by the caller.
	if (key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
		return {};
	}
	return hexen ? QCoreApplication::translate("VibeStudioLevelMap", "A Hexen thing's fields are type, angle, x, y, z, flags, tid, and special.")
		     : QCoreApplication::translate("VibeStudioLevelMap", "A Doom thing's fields are type, angle, x, y, and flags.");
}

} // namespace

namespace {

// Why `key` = `value` cannot be written on the entity, or empty when it can.
QString entityPropertyProblem(LevelMapDocument* document, int entityId, const QString& key, const QString& value)
{
	if (!entityById(document, entityId)) {
		return QCoreApplication::translate("VibeStudioLevelMap", "Entity not found.");
	}
	if (key.isEmpty()) {
		return QCoreApplication::translate("VibeStudioLevelMap", "Entity property key is required.");
	}
	if (isTextMapFormat(document->format) && (!isWritableMapString(key) || !isWritableMapString(value))) {
		return QCoreApplication::translate("VibeStudioLevelMap", "Map keys and values cannot contain double quotes or line breaks.");
	}
	if (document->format == LevelMapFormat::DoomWad && thingById(document, entityId)) {
		QString problem = doomThingFieldProblem(key, value, document->doomFormat == LevelMapDoomFormat::Hexen);
		if (problem.isEmpty() && key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
			const LevelMapVec3 place = parseVec3(value);
			problem = place.valid ? doomThingPlaceProblem(*document, place.x, place.y, place.z) : QCoreApplication::translate("VibeStudioLevelMap", "A thing's origin is three numbers, x y z.");
		}
		return problem;
	}
	return {};
}

// Writes `key` = `value` on the entity, and on the Doom thing it mirrors, and
// says what the key and the thing were before.
LevelMapPropertyStep writeEntityProperty(LevelMapDocument* document, LevelMapEntity* entity, const QString& key, const QString& value)
{
	LevelMapPropertyStep step;
	step.entityId = entity->id;
	const int index = propertyIndex(*entity, key);
	step.keyExisted = index >= 0;
	step.oldValue = index >= 0 ? entity->properties.at(index).value : QString();
	step.newValue = value;
	LevelMapDoomThing* mirroredThing = document->format == LevelMapFormat::DoomWad ? thingById(document, entity->id) : nullptr;
	if (mirroredThing) {
		step.hasThingSnapshot = true;
		step.oldThing = *mirroredThing;
	}
	if (index >= 0) {
		entity->properties[index].value = value;
	} else {
		entity->properties.push_back({key, value, 0});
	}
	if (key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
		entity->className = value;
	} else if (key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
		entity->origin = parseVec3(value);
	}
	if (mirroredThing) {
		LevelMapDoomThing& thing = *mirroredThing;
		bool ok = false;
		if (key.compare(QStringLiteral("type"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.type = parsed;
				entity->className = QStringLiteral("thing:%1").arg(parsed);
			}
		} else if (key.compare(QStringLiteral("angle"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.angle = parsed;
			}
		} else if (key.compare(QStringLiteral("flags"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.flags = parsed;
			}
		} else if (key.compare(QStringLiteral("tid"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.tid = parsed;
			}
		} else if (key.compare(QStringLiteral("special"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.special = parsed;
			}
		} else if (key.compare(QStringLiteral("x"), Qt::CaseInsensitive) == 0) {
			const double parsed = value.toDouble(&ok);
			if (ok) {
				thing.x = parsed;
				entity->origin = {thing.x, thing.y, thing.z, true};
			}
		} else if (key.compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0) {
			const double parsed = value.toDouble(&ok);
			if (ok) {
				thing.y = parsed;
				entity->origin = {thing.x, thing.y, thing.z, true};
			}
		} else if (key.compare(QStringLiteral("z"), Qt::CaseInsensitive) == 0) {
			const double parsed = value.toDouble(&ok);
			if (ok) {
				thing.z = parsed;
				entity->origin = {thing.x, thing.y, thing.z, true};
			}
		} else if (key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0 && entity->origin.valid) {
			thing.x = entity->origin.x;
			thing.y = entity->origin.y;
			thing.z = entity->origin.z;
		}
		syncDoomThingEntity(document, entity->id);
		step.newThing = *mirroredThing;
	}
	return step;
}

} // namespace

namespace {
bool commitUdmfBytes(LevelMapDocument* document, const QByteArray& bytes, LevelMapUndoCommand command, QString* error,
	const std::function<bool()>& cancel)
{
	if (bytes == document->doomUdmf->source) { return true; }
	auto candidate = *document;
	if (!adoptUdmfText(&candidate, bytes, error, cancel)) { return false; }
	QSet<QString> existingErrors;
	for (const auto& issue : document->issues) {
		if (issue.severity == LevelMapIssueSeverity::Error) { existingErrors.insert(issue.code + '/' + issue.objectId); }
	}
	for (const auto& issue : candidate.issues) {
		if (issue.severity == LevelMapIssueSeverity::Error && !existingErrors.contains(issue.code + '/' + issue.objectId)) {
			if (error) { *error = issue.message; } return false;
		}
	}
	if (cancel && cancel()) { if (error) { *error = QCoreApplication::translate("LevelUdmf", "UDMF operation cancelled."); } return false; }
	command.udmfBefore = document->doomUdmf->source; command.udmfAfter = bytes;
	// The active scene-lock transaction is attached to this document's address.
	// Publish history through that identity, rolling back even if a worker is
	// cancelled at pushUndo's final checkpoint.
	std::swap(*document, candidate);
	try { pushUndo(document, std::move(command)); }
	catch (...) { std::swap(*document, candidate); throw; }
	return true;
}
} // namespace

bool editLevelMapUdmfProperties(LevelMapDocument* document, const QVector<LevelUdmfPropertyEdit>& edits, QString* error,
	const std::function<bool()>& cancel)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || document->doomFormat != LevelMapDoomFormat::Udmf || !document->doomUdmf) {
		return fail(QCoreApplication::translate("LevelUdmf", "Open a UDMF map before editing TEXTMAP properties."));
	}
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return editLevelMapUdmfProperties(candidate, edits, error, cancel);
	}); guarded.has_value()) { return *guarded; }
	QString problem;
	const auto bytes = prepareLevelUdmfProperties(*document->doomUdmf, edits, &problem, cancel);
	if (!problem.isEmpty()) { return fail(problem); }
	if (bytes == document->doomUdmf->source) { return true; }
	// Native record comparisons cannot see namespace extension properties.
	const auto locked = levelSceneLockedObjects(*document);
	for (const auto& edit : edits) {
		const auto object = edit.object.trimmed().toLower();
		bool protectedObject = locked.contains(object);
		if (object.startsWith(QStringLiteral("sidedef:"))) {
			const int id = object.mid(8).toInt();
			for (const auto& line : document->doomLinedefs) {
				if ((line.frontSidedef == id || line.backSidedef == id) && locked.contains(linedefObjectId(line.id))) { protectedObject = true; break; }
			}
		} else if (!object.startsWith(QStringLiteral("vertex:")) && !object.startsWith(QStringLiteral("linedef:"))
			&& !object.startsWith(QStringLiteral("sector:")) && !object.startsWith(QStringLiteral("thing:"))) {
			protectedObject = !locked.isEmpty();
		}
		if (protectedObject) { return fail(QCoreApplication::translate("LevelUdmf", "UDMF property editing would change locked scene content: %1.").arg(object)); }
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("udmf-properties");
	command.description = QCoreApplication::translate("LevelUdmf", "Edit UDMF properties");
	command.undoDescription = QCoreApplication::translate("LevelUdmf", "Restore UDMF properties");
	return commitUdmfBytes(document, bytes, command, error, cancel);
}

bool setLevelMapEntityProperty(LevelMapDocument* document, int entityId, const QString& key, const QString& value, QString* error)
{
	if (document && document->doomUdmf) { if (error) { *error = QCoreApplication::translate("LevelUdmf", "Use the UDMF property editor for this map."); } return false; }
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return setLevelMapEntityProperty(candidate, entityId, key, value, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const QString trimmedKey = key.trimmed();
	if (const QString problem = entityPropertyProblem(document, entityId, trimmedKey, value); !problem.isEmpty()) {
		if (error) {
			*error = problem;
		}
		return false;
	}
	const LevelMapPropertyStep step = writeEntityProperty(document, entityById(document, entityId), trimmedKey, value);
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("set-property");
	command.objectKind = QStringLiteral("entity");
	command.objectId = entityId;
	command.entityId = entityId;
	command.key = trimmedKey;
	command.keyExisted = step.keyExisted;
	command.oldValue = step.oldValue;
	command.newValue = value;
	command.hasThingSnapshot = step.hasThingSnapshot;
	command.oldThing = step.oldThing;
	command.newThing = step.newThing;
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Set %1 on entity %2").arg(trimmedKey).arg(entityId);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 on entity %2").arg(trimmedKey).arg(entityId);
	pushUndo(document, command);
	// A thing is edited through its mirror but stays selected as the thing the
	// viewport draws.
	selectLevelMapObject(document, step.hasThingSnapshot ? thingObjectId(entityId) : entityObjectId(entityId));
	return true;
}

bool setLevelMapEntitiesProperty(LevelMapDocument* document, const QVector<int>& entityIds, const QString& key, const QStringList& values, QString* error)
{
	if (document && document->doomUdmf) { if (error) { *error = QCoreApplication::translate("LevelUdmf", "Use the UDMF property editor for this map."); } return false; }
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return setLevelMapEntitiesProperty(candidate, entityIds, key, values, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (!document || entityIds.isEmpty() || (values.size() != 1 && values.size() != entityIds.size())) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Give one value, or one value for each entity.");
		}
		return false;
	}
	const QString trimmedKey = key.trimmed();
	const auto valueFor = [&values](qsizetype index) {
		return values.size() == 1 ? values.first() : values.at(index);
	};
	// Each entity once, checked before any is changed.
	QVector<int> ids;
	QStringList idValues;
	QSet<int> seen;
	for (qsizetype index = 0; index < entityIds.size(); ++index) {
		const int id = entityIds.at(index);
		if (seen.contains(id)) {
			continue;
		}
		seen.insert(id);
		if (const QString problem = entityPropertyProblem(document, id, trimmedKey, valueFor(index)); !problem.isEmpty()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Entity %1: %2").arg(id).arg(problem);
			}
			return false;
		}
		ids << id;
		idValues << valueFor(index);
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("set-properties");
	command.objectKind = QStringLiteral("entity");
	command.key = trimmedKey;
	for (qsizetype index = 0; index < ids.size(); ++index) {
		LevelMapEntity* entity = entityById(document, ids.at(index));
		const int existing = propertyIndex(*entity, trimmedKey);
		if (existing >= 0 && entity->properties.at(existing).value == idValues.at(index)) {
			continue;
		}
		command.propertySteps.push_back(writeEntityProperty(document, entity, trimmedKey, idValues.at(index)));
	}
	if (command.propertySteps.isEmpty()) {
		return true;
	}
	command.objectId = command.propertySteps.constLast().entityId;
	command.entityId = command.objectId;
	command.newValue = values.size() == 1 ? values.first() : QString();
	const qsizetype changed = command.propertySteps.size();
	command.description = changed == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Set %1 on entity %2").arg(trimmedKey).arg(command.objectId)
					    : QCoreApplication::translate("VibeStudioLevelMap", "Set %1 on %2 entities").arg(trimmedKey).arg(changed);
	command.undoDescription = changed == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 on entity %2").arg(trimmedKey).arg(command.objectId)
						: QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 on %2 entities").arg(trimmedKey).arg(changed);
	pushUndo(document, command);
	return true;
}

bool removeLevelMapEntityProperty(LevelMapDocument* document, int entityId, const QString& key, QString* error)
{
	if (document && document->doomUdmf) { if (error) { *error = QCoreApplication::translate("LevelUdmf", "Use the UDMF property editor for this map."); } return false; }
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return removeLevelMapEntityProperty(candidate, entityId, key, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	LevelMapEntity* entity = entityById(document, entityId);
	if (!entity) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Entity not found.");
		}
		return false;
	}
	const QString trimmedKey = key.trimmed();
	const int index = propertyIndex(*entity, trimmedKey);
	if (index < 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Entity does not have that key.");
		}
		return false;
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("remove-property");
	command.objectKind = QStringLiteral("entity");
	command.objectId = entityId;
	command.entityId = entityId;
	command.key = entity->properties.at(index).key;
	command.oldValue = entity->properties.at(index).value;
	command.propertyLine = entity->properties.at(index).line;
	command.keyExisted = true;
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Remove %1 from entity %2").arg(trimmedKey).arg(entityId);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 on entity %2").arg(trimmedKey).arg(entityId);

	removeEntityPropertyAt(entity, index);
	if (trimmedKey.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
		entity->origin = {};
	}
	if (trimmedKey.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
		entity->className.clear();
	}
	pushUndo(document, command);
	selectLevelMapObject(document, entityObjectId(entityId));
	return true;
}

bool removeLevelMapEntitiesProperty(LevelMapDocument* document, const QVector<int>& entityIds, const QString& key, QString* error)
{
	if (document && document->doomUdmf) { if (error) { *error = QCoreApplication::translate("LevelUdmf", "Use the UDMF property editor for this map."); } return false; }
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return removeLevelMapEntitiesProperty(candidate, entityIds, key, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const QString trimmedKey = key.trimmed();
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("set-properties");
	command.objectKind = QStringLiteral("entity");
	command.key = trimmedKey;
	QSet<int> seen;
	for (const int id : entityIds) {
		LevelMapEntity* entity = entityById(document, id);
		if (!entity) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 not found.").arg(id);
			}
			return false;
		}
		const int index = propertyIndex(*entity, trimmedKey);
		if (index < 0 || seen.contains(id)) {
			continue;
		}
		seen.insert(id);
		LevelMapPropertyStep step;
		step.entityId = id;
		step.keyExisted = true;
		step.oldValue = entity->properties.at(index).value;
		step.removed = true;
		step.propertyLine = entity->properties.at(index).line;
		command.propertySteps.push_back(step);
	}
	if (command.propertySteps.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "None of the entities has that key.");
		}
		return false;
	}
	// Checked first, taken off after: the same step undo replays.
	for (const LevelMapPropertyStep& step : std::as_const(command.propertySteps)) {
		applyPropertyStep(document, trimmedKey, step, true);
	}
	command.objectId = command.propertySteps.constLast().entityId;
	command.entityId = command.objectId;
	const qsizetype removed = command.propertySteps.size();
	command.description = removed == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Remove %1 from entity %2").arg(trimmedKey).arg(command.objectId)
					    : QCoreApplication::translate("VibeStudioLevelMap", "Remove %1 from %2 entities").arg(trimmedKey).arg(removed);
	command.undoDescription = removed == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 on entity %2").arg(trimmedKey).arg(command.objectId)
						: QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 on %2 entities").arg(trimmedKey).arg(removed);
	pushUndo(document, command);
	return true;
}

bool setLevelMapSectorProperty(LevelMapDocument* document, int sectorId, const QString& key, const QString& value, QString* error)
{
	if (document && document->doomUdmf) { if (error) { *error = QCoreApplication::translate("LevelUdmf", "Use the UDMF property editor for this map."); } return false; }
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return setLevelMapSectorProperty(candidate, sectorId, key, value, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	LevelMapDoomSector* sector = sectorById(document, sectorId);
	if (!sector) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Sector not found.");
		}
		return false;
	}
	const QString normalizedKey = key.trimmed().toLower();
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("set-sector-property");
	command.objectKind = QStringLiteral("sector");
	command.objectId = sectorId;
	command.key = normalizedKey;
	command.oldValue = value;
	command.newValue = value;
	command.hasSectorSnapshot = true;
	command.oldSector = *sector;
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Set %1 on sector %2").arg(normalizedKey).arg(sectorId);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 on sector %2").arg(normalizedKey).arg(sectorId);

	bool ok = false;
	const int intValue = value.trimmed().toInt(&ok);
	if (normalizedKey == QStringLiteral("floorheight") || normalizedKey == QStringLiteral("heightfloor")) {
		if (!ok) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Sector height must be an integer.");
			}
			return false;
		}
		sector->floorHeight = intValue;
	} else if (normalizedKey == QStringLiteral("ceilingheight") || normalizedKey == QStringLiteral("heightceiling")) {
		if (!ok) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Sector height must be an integer.");
			}
			return false;
		}
		sector->ceilingHeight = intValue;
	} else if (normalizedKey == QStringLiteral("floortexture") || normalizedKey == QStringLiteral("texturefloor")) {
		sector->floorTexture = value.trimmed().left(8);
	} else if (normalizedKey == QStringLiteral("ceilingtexture") || normalizedKey == QStringLiteral("textureceiling")) {
		sector->ceilingTexture = value.trimmed().left(8);
	} else if (normalizedKey == QStringLiteral("lightlevel") || normalizedKey == QStringLiteral("light")) {
		if (!ok) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Sector light level must be an integer.");
			}
			return false;
		}
		sector->lightLevel = intValue;
	} else if (normalizedKey == QStringLiteral("special")) {
		if (!ok) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Sector special must be an integer.");
			}
			return false;
		}
		sector->special = intValue;
	} else if (normalizedKey == QStringLiteral("tag") || normalizedKey == QStringLiteral("id")) {
		if (!ok) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Sector tag must be an integer.");
			}
			return false;
		}
		sector->tag = intValue;
	} else {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Sector keys are floorheight, ceilingheight, floortexture, ceilingtexture, lightlevel, special and tag.");
		}
		return false;
	}
	command.newSector = *sector;
	pushUndo(document, command);
	selectLevelMapObject(document, sectorObjectId(sectorId));
	return true;
}

bool setLevelMapSidedefProperty(LevelMapDocument* document, int sidedefId, const QString& key, const QString& value, QString* error)
{
	if (document && document->doomUdmf) { if (error) { *error = QCoreApplication::translate("LevelUdmf", "Use the UDMF property editor for this map."); } return false; }
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return setLevelMapSidedefProperty(candidate, sidedefId, key, value, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	LevelMapDoomSidedef* sidedef = sidedefById(document, sidedefId);
	if (!sidedef) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Sidedef not found.");
		}
		return false;
	}
	const QString normalizedKey = key.trimmed().toLower();
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("set-sidedef-property");
	command.objectKind = QStringLiteral("sidedef");
	command.objectId = sidedefId;
	command.key = normalizedKey;
	command.oldValue = value;
	command.newValue = value;
	command.hasSidedefSnapshot = true;
	command.oldSidedef = *sidedef;
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Set %1 on sidedef %2").arg(normalizedKey).arg(sidedefId);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 on sidedef %2").arg(normalizedKey).arg(sidedefId);

	bool ok = false;
	const int intValue = value.trimmed().toInt(&ok);
	if (normalizedKey == QStringLiteral("sector")) {
		if (!ok || intValue < 0 || intValue >= document->doomSectors.size()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Sidedef sector must be an existing sector index.");
			}
			return false;
		}
		sidedef->sector = intValue;
	} else if (normalizedKey == QStringLiteral("offsetx")) {
		if (!ok) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Sidedef offset must be an integer.");
			}
			return false;
		}
		sidedef->offsetX = intValue;
	} else if (normalizedKey == QStringLiteral("offsety")) {
		if (!ok) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Sidedef offset must be an integer.");
			}
			return false;
		}
		sidedef->offsetY = intValue;
	} else if (normalizedKey == QStringLiteral("upper") || normalizedKey == QStringLiteral("uppertexture")) {
		sidedef->upperTexture = value.trimmed().left(8);
	} else if (normalizedKey == QStringLiteral("lower") || normalizedKey == QStringLiteral("lowertexture")) {
		sidedef->lowerTexture = value.trimmed().left(8);
	} else if (normalizedKey == QStringLiteral("middle") || normalizedKey == QStringLiteral("middletexture")) {
		sidedef->middleTexture = value.trimmed().left(8);
	} else {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Sidedef keys are sector, offsetx, offsety, upper, lower and middle.");
		}
		return false;
	}
	command.newSidedef = *sidedef;
	pushUndo(document, command);
	return true;
}

namespace {

// What moving `objects` moves: each object as it is, except a brush entity,
// which moves through its brushes and patches, and its origin only where it
// has one. Quake places a brush model at its entity's origin, so an origin
// added to a brush entity would move it a second time in the game.
QVector<LevelMapSelectionRef> movedObjects(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects)
{
	QVector<LevelMapSelectionRef> moved;
	QSet<quint64> seen;
	const auto add = [&moved, &seen](LevelMapSelectionKind kind, int id) {
		const quint64 key = (static_cast<quint64>(static_cast<quint32>(kind)) << 32) | static_cast<quint32>(id);
		if (!seen.contains(key)) {
			seen.insert(key);
			moved.push_back({kind, id});
		}
	};
	QHash<int, QVector<LevelMapSelectionRef>> primitives;
	bool primitivesKnown = false;
	for (const LevelMapSelectionRef& ref : objects) {
		if (!levelMapSelectionKindIsMovable(ref.kind)) {
			continue;
		}
		if (ref.kind == LevelMapSelectionKind::Entity && document.format != LevelMapFormat::DoomWad) {
			if (!primitivesKnown) {
				for (const LevelMapBrush& brush : document.brushes) {
					primitives[brush.entityId].push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
				}
				for (const LevelMapPatch& patch : document.patches) {
					primitives[patch.entityId].push_back({LevelMapSelectionKind::QuakePatch, patch.id});
				}
				primitivesKnown = true;
			}
			const QVector<LevelMapSelectionRef> held = primitives.value(ref.objectId);
			if (!held.isEmpty()) {
				for (const LevelMapSelectionRef& primitive : held) {
					add(primitive.kind, primitive.objectId);
				}
				const int index = indexOfObjectId(document.entities, ref.objectId);
				if (index < 0 || propertyIndex(document.entities.at(index), QStringLiteral("origin")) < 0) {
					continue;
				}
			}
		}
		add(ref.kind, ref.objectId);
	}
	return moved;
}

// Moves `objects` by one delta as one `move-selection` command, described by
// the objects as given rather than by the brushes a brush entity moves by.
bool moveObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, double dx, double dy, double dz, QString* error)
{
	if (document && document->doomUdmf) { if (error) { *error = QCoreApplication::translate("LevelUdmf", "Use the UDMF property editor for this map."); } return false; }
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("move-selection");
	command.objectKind = QStringLiteral("selection");
	command.delta = {dx, dy, dz, true};
	for (const LevelMapSelectionRef& ref : movedObjects(*document, objects)) {
		const QString kind = levelMapSelectionKindId(ref.kind);
		LevelMapUndoCommand child;
		QString childError;
		if (!applyMoveToObject(document, kind, ref.objectId, dx, dy, dz, &child, &childError)) {
			// Undo the members that already moved, so a stale selection entry
			// cannot leave half of the map displaced.
			applyLevelMapCommand(document, command, false);
			if (error) {
				*error = childError;
			}
			return false;
		}
		LevelMapMoveStep step;
		step.objectKind = kind;
		step.objectId = ref.objectId;
		step.oldValue = child.oldValue;
		step.newValue = child.newValue;
		step.originSynthesized = child.originSynthesized;
		command.moveSteps.push_back(step);
	}
	if (command.moveSteps.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The selection contains nothing that can be moved.");
		}
		return false;
	}
	QVector<LevelMapSelectionRef> named;
	for (const LevelMapSelectionRef& ref : objects) {
		if (levelMapSelectionKindIsMovable(ref.kind)) {
			named.push_back(ref);
		}
	}
	command.objectId = named.size() == 1 ? named.first().objectId : -1;
	if (named.size() == 1) {
		const QString moved = QStringLiteral("%1:%2").arg(levelMapSelectionKindId(named.first().kind)).arg(named.first().objectId);
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Move %1 by %2,%3,%4").arg(moved).arg(dx).arg(dy).arg(dz);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Move %1 back").arg(moved);
	} else {
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Move %1 objects by %2,%3,%4").arg(named.size()).arg(dx).arg(dy).arg(dz);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Move %1 objects back").arg(named.size());
	}
	pushUndo(document, command);
	return true;
}

} // namespace

bool moveLevelMapObject(LevelMapDocument* document, const QString& objectKind, int objectId, double dx, double dy, double dz, QString* error)
{
	if (document && document->doomUdmf) {
		auto candidate = *document;
		if (!selectLevelMapObject(&candidate, objectKind + ':' + QString::number(objectId), error)
			|| !moveLevelMapSelection(&candidate, dx, dy, dz, LevelMapTextureLockOptions{}, error)) { return false; }
		*document = std::move(candidate); return true;
	}
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return moveLevelMapObject(candidate, objectKind, objectId, dx, dy, dz, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	const QString kind = normalizedId(objectKind);
	if (kind == QStringLiteral("entity") && document->format != LevelMapFormat::DoomWad) {
		// A brush entity moves through its brushes, as it does in the editor.
		const QVector<LevelMapSelectionRef> entity {{LevelMapSelectionKind::Entity, objectId}};
		if (movedObjects(*document, entity) != entity) {
			if (!moveObjects(document, entity, dx, dy, dz, error)) {
				return false;
			}
			selectLevelMapObject(document, entityObjectId(objectId));
			return true;
		}
	}
	LevelMapUndoCommand command;
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Move %1:%2 by %3,%4,%5").arg(kind).arg(objectId).arg(dx).arg(dy).arg(dz);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Move %1:%2 back").arg(kind).arg(objectId);
	if (!applyMoveToObject(document, kind, objectId, dx, dy, dz, &command, error)) {
		return false;
	}
	pushUndo(document, command);
	selectLevelMapObject(document, QStringLiteral("%1:%2").arg(kind).arg(objectId));
	return true;
}

bool moveLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz, QString* error)
{
	if (document && document->doomUdmf) { return moveLevelMapSelection(document, dx, dy, dz, LevelMapTextureLockOptions{}, error); }
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return moveLevelMapSelection(candidate, dx, dy, dz, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	if (document->selection.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Nothing is selected.");
		}
		return false;
	}
	const QVector<LevelMapSelectionRef> selection = document->selection;
	if (!moveObjects(document, selection, dx, dy, dz, error)) {
		return false;
	}
	// The selection survives the move: a drag leaves what you dragged selected.
	applySelectionFlags(document);
	return true;
}

bool moveLevelMapSelectionSnapped(LevelMapDocument* document, double dx, double dy, double dz, double gridSize, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return moveLevelMapSelectionSnapped(candidate, dx, dy, dz, gridSize, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	const double snappedX = snapLevelMapCoordinate(dx, gridSize);
	const double snappedY = snapLevelMapCoordinate(dy, gridSize);
	const double snappedZ = snapLevelMapCoordinate(dz, gridSize);
	if (snappedX == 0.0 && snappedY == 0.0 && snappedZ == 0.0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The snapped move is zero, so nothing changed.");
		}
		return false;
	}
	return moveLevelMapSelection(document, snappedX, snappedY, snappedZ, error);
}

namespace {

bool deleteDoomThings(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, QString* error);
bool deleteDoomGeometry(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, QString* error);

} // namespace

bool addLevelMapEntity(LevelMapDocument* document, const QString& className, const LevelMapVec3& origin,
	const QVector<LevelMapProperty>& properties, int* entityId, QString* error)
{
	int sceneResult_entityId = -1;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return addLevelMapEntity(candidate, className, origin, properties, entityId ? &sceneResult_entityId : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && entityId) { *entityId = std::move(sceneResult_entityId); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (!isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entities can be added to Quake-family .map files only."));
	}
	const QString name = className.trimmed();
	if (name.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "An entity class name is required."));
	}
	if (!origin.valid) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A new entity needs an origin."));
	}
	LevelMapEntity entity;
	entity.id = 0;
	for (const LevelMapEntity& existing : document->entities) {
		entity.id = std::max(entity.id, existing.id + 1);
	}
	entity.className = name;
	entity.origin = origin;
	entity.properties.push_back({QStringLiteral("classname"), name, 0});
	entity.properties.push_back({QStringLiteral("origin"), serializeVec3(origin), 0});
	for (const LevelMapProperty& property : properties) {
		const QString key = property.key.trimmed();
		if (key.isEmpty() || key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0
			|| key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
			continue;
		}
		upsertEntityProperty(&entity, key, property.value);
	}
	for (const LevelMapProperty& property : entity.properties) {
		if (!isWritableMapString(property.key) || !isWritableMapString(property.value)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Map keys and values cannot contain double quotes or line breaks."));
		}
	}

	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("add-entity");
	command.objectKind = QStringLiteral("entity");
	command.objectId = entity.id;
	command.entityId = entity.id;
	command.entitySnapshots.push_back(entity);
	command.entityIndexes.push_back(static_cast<int>(document->entities.size()));
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Add %1 as entity %2").arg(name).arg(entity.id);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Remove added entity %1").arg(entity.id);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The new entity could not be added."));
	}
	pushUndo(document, command);
	selectLevelMapObject(document, entityObjectId(entity.id));
	if (entityId) {
		*entityId = entity.id;
	}
	return true;
}

bool deleteLevelMapObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return deleteLevelMapObjects(candidate, objects, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (objects.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing is selected."));
	}
	if (document->format == LevelMapFormat::DoomWad) {
		// Things alone keep their own lighter command; geometry renumbers.
		const bool geometry = std::any_of(objects.cbegin(), objects.cend(), [](const LevelMapSelectionRef& ref) {
			return ref.kind == LevelMapSelectionKind::DoomVertex || ref.kind == LevelMapSelectionKind::DoomLinedef
				|| ref.kind == LevelMapSelectionKind::DoomSector;
		});
		return geometry ? deleteDoomGeometry(document, objects, error) : deleteDoomThings(document, objects, error);
	}
	if (!isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Only entities, brushes, and patches in Quake-family .map files can be deleted."));
	}
	QSet<int> entityIds;
	QSet<int> brushIds;
	QSet<int> patchIds;
	for (const LevelMapSelectionRef& ref : objects) {
		switch (ref.kind) {
		case LevelMapSelectionKind::Entity: {
			const LevelMapEntity* entity = entityById(document, ref.objectId);
			if (!entity) {
				return fail(selectionNotFoundText(ref.kind));
			}
			if (isWorldspawnEntity(*entity)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "The worldspawn entity holds the world and cannot be deleted. Delete its brushes instead."));
			}
			entityIds.insert(ref.objectId);
			break;
		}
		case LevelMapSelectionKind::QuakeBrush:
			if (!brushById(document, ref.objectId)) {
				return fail(selectionNotFoundText(ref.kind));
			}
			brushIds.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::QuakePatch:
			if (!patchById(document, ref.objectId)) {
				return fail(selectionNotFoundText(ref.kind));
			}
			patchIds.insert(ref.objectId);
			break;
		default:
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Only entities, brushes, and patches can be deleted."));
		}
	}
	const int requested = static_cast<int>(entityIds.size() + brushIds.size() + patchIds.size());
	// A brush entity the deletion leaves with no brushes or patches goes too,
	// as Radiant and TrenchBroom remove it, instead of staying behind empty for
	// the compiler to trip over; one sharing lines with other text stays.
	for (const LevelMapEntity& entity : document->entities) {
		if (entityIds.contains(entity.id) || isWorldspawnEntity(entity) || !objectOwnsItsLines(*document, entity.startLine, entity.endLine)) {
			continue;
		}
		bool holds = false;
		bool keeps = false;
		for (const LevelMapBrush& brush : document->brushes) {
			if (brush.entityId == entity.id) {
				holds = true;
				keeps = keeps || !brushIds.contains(brush.id);
			}
		}
		for (const LevelMapPatch& patch : document->patches) {
			if (patch.entityId == entity.id) {
				holds = true;
				keeps = keeps || !patchIds.contains(patch.id);
			}
		}
		if (holds && !keeps) {
			entityIds.insert(entity.id);
		}
	}

	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("delete-objects");
	command.objectKind = QStringLiteral("selection");
	const auto takeLines = [document, &command](int startLine, int endLine) {
		if (startLine > 0) {
			command.deletedLineRanges.push_back({deletionFirstLine(*document, startLine), endLine});
		}
	};
	QSet<QString> objectIds;
	for (int index = 0; index < document->entities.size(); ++index) {
		const LevelMapEntity& entity = document->entities.at(index);
		if (!entityIds.contains(entity.id)) {
			continue;
		}
		if (!objectOwnsItsLines(*document, entity.startLine, entity.endLine)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 shares a source line with other map text, so deleting it would damage the file.").arg(entity.id));
		}
		takeLines(entity.startLine, entity.endLine);
		LevelMapEntity snapshot = entity;
		snapshot.selected = false;
		command.entitySnapshots.push_back(snapshot);
		command.entityIndexes.push_back(index);
		objectIds.insert(entityObjectId(entity.id));
	}
	// An entity's brushes and patches go with it, inside its own lines; the
	// ones deleted on their own need lines of their own.
	for (int index = 0; index < document->brushes.size(); ++index) {
		const LevelMapBrush& brush = document->brushes.at(index);
		const bool withEntity = entityIds.contains(brush.entityId);
		if (!withEntity && !brushIds.contains(brush.id)) {
			continue;
		}
		if (!withEntity) {
			if (!objectOwnsItsLines(*document, brush.startLine, brush.endLine)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 shares a source line with other map text, so deleting it would damage the file.").arg(brush.id));
			}
			takeLines(brush.startLine, brush.endLine);
		}
		LevelMapBrush snapshot = brush;
		snapshot.selected = false;
		command.brushSnapshots.push_back(snapshot);
		command.brushIndexes.push_back(index);
		objectIds.insert(brushObjectId(brush.id));
	}
	for (int index = 0; index < document->patches.size(); ++index) {
		const LevelMapPatch& patch = document->patches.at(index);
		const bool withEntity = entityIds.contains(patch.entityId);
		if (!withEntity && !patchIds.contains(patch.id)) {
			continue;
		}
		if (!withEntity) {
			if (!objectOwnsItsLines(*document, patch.startLine, patch.endLine)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 shares a source line with other map text, so deleting it would damage the file.").arg(patch.id));
			}
			takeLines(patch.startLine, patch.endLine);
		}
		LevelMapPatch snapshot = patch;
		snapshot.selected = false;
		command.patchSnapshots.push_back(snapshot);
		command.patchIndexes.push_back(index);
		objectIds.insert(patchObjectId(patch.id));
	}
	for (int index = 0; index < document->issues.size(); ++index) {
		if (objectIds.contains(document->issues.at(index).objectId)) {
			command.issueSnapshots.push_back(document->issues.at(index));
			command.issueIndexes.push_back(index);
		}
	}

	const QString first = levelMapSelectionRefId(objects.first());
	command.objectId = requested == 1 ? objects.first().objectId : -1;
	command.description = requested == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Delete %1").arg(first) : QCoreApplication::translate("VibeStudioLevelMap", "Delete %1 objects").arg(requested);
	command.undoDescription = requested == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Restore %1").arg(first) : QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 deleted objects").arg(requested);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The objects to delete are no longer in the map."));
	}
	pushUndo(document, command);
	return true;
}

bool deleteLevelMapSelection(LevelMapDocument* document, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return deleteLevelMapSelection(candidate, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	// A copy: the deletion prunes the live selection as it runs.
	const QVector<LevelMapSelectionRef> selection = document->selection;
	return deleteLevelMapObjects(document, selection, error);
}

namespace {

bool fitsDoomThingCoordinate(double value)
{
	return std::isfinite(value) && value >= -32768.0 && value <= 32767.0;
}

// Things a selection names, directly or through their mirrored entities. False
// names the first member that is not a thing.
bool selectedDoomThings(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, QSet<int>* thingIds, QString* error)
{
	for (const LevelMapSelectionRef& ref : objects) {
		if ((ref.kind == LevelMapSelectionKind::DoomThing || ref.kind == LevelMapSelectionKind::Entity) && thingById(document, ref.objectId)) {
			thingIds->insert(ref.objectId);
			continue;
		}
		if (error) {
			*error = ref.kind == LevelMapSelectionKind::DoomThing || ref.kind == LevelMapSelectionKind::Entity
				? selectionNotFoundText(ref.kind)
				: QCoreApplication::translate("VibeStudioLevelMap", "Only things can be copied in a Doom map; its vertices, linedefs, and sectors are drawn instead.");
		}
		return false;
	}
	return true;
}

bool deleteDoomThings(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, QString* error)
{
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet.");
		}
		return false;
	}
	QSet<int> thingIds;
	if (!selectedDoomThings(document, objects, &thingIds, error)) {
		return false;
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("delete-objects");
	command.objectKind = QStringLiteral("selection");
	QSet<QString> objectIds;
	for (int index = 0; index < document->doomThings.size(); ++index) {
		const LevelMapDoomThing& thing = document->doomThings.at(index);
		if (!thingIds.contains(thing.id)) {
			continue;
		}
		LevelMapDoomThing snapshot = thing;
		snapshot.selected = false;
		command.thingSnapshots.push_back(snapshot);
		command.thingIndexes.push_back(index);
		objectIds.insert(thingObjectId(thing.id));
	}
	for (int index = 0; index < document->entities.size(); ++index) {
		const LevelMapEntity& entity = document->entities.at(index);
		if (thingIds.contains(entity.id)) {
			LevelMapEntity snapshot = entity;
			snapshot.selected = false;
			command.entitySnapshots.push_back(snapshot);
			command.entityIndexes.push_back(index);
			objectIds.insert(entityObjectId(entity.id));
		}
	}
	for (int index = 0; index < document->issues.size(); ++index) {
		if (objectIds.contains(document->issues.at(index).objectId)) {
			command.issueSnapshots.push_back(document->issues.at(index));
			command.issueIndexes.push_back(index);
		}
	}
	const int count = static_cast<int>(thingIds.size());
	const QString first = thingObjectId(command.thingSnapshots.isEmpty() ? -1 : command.thingSnapshots.first().id);
	command.objectId = count == 1 ? command.thingSnapshots.first().id : -1;
	command.description = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Delete %1").arg(first) : QCoreApplication::translate("VibeStudioLevelMap", "Delete %1 objects").arg(count);
	command.undoDescription = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Restore %1").arg(first) : QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 deleted objects").arg(count);
	if (!applyLevelMapCommand(document, command, true)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The objects to delete are no longer in the map.");
		}
		return false;
	}
	pushUndo(document, command);
	return true;
}

int nextDoomThingId(const LevelMapDocument& document)
{
	// Things and their mirrored entities share ids, so a new id is past both.
	int next = 0;
	for (const LevelMapDoomThing& thing : document.doomThings) {
		next = std::max(next, thing.id + 1);
	}
	for (const LevelMapEntity& entity : document.entities) {
		next = std::max(next, entity.id + 1);
	}
	return next;
}

bool duplicateDoomThings(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, double dx, double dy, double dz, QString* error)
{
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet.");
		}
		return false;
	}
	QSet<int> thingIds;
	if (!selectedDoomThings(document, objects, &thingIds, error)) {
		return false;
	}
	const bool hexen = document->doomFormat == LevelMapDoomFormat::Hexen;
	int next = nextDoomThingId(*document);
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("add-objects");
	command.objectKind = QStringLiteral("selection");
	QVector<LevelMapSelectionRef> copies;
	for (const LevelMapDoomThing& thing : document->doomThings) {
		detail::placementCancellationCheckpoint();
		if (!thingIds.contains(thing.id)) {
			continue;
		}
		LevelMapDoomThing copy = thing;
		copy.id = next++;
		copy.selected = false;
		copy.x = std::round(thing.x + dx);
		copy.y = std::round(thing.y + dy);
		if (hexen) {
			copy.z = std::round(thing.z + dz);
		}
		if (!fitsDoomThingCoordinate(copy.x) || !fitsDoomThingCoordinate(copy.y) || !fitsDoomThingCoordinate(copy.z)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "A copy of %1 would land outside the range a Doom map can store.").arg(thingObjectId(thing.id));
			}
			return false;
		}
		command.thingIndexes.push_back(static_cast<int>(document->doomThings.size() + command.thingSnapshots.size()));
		command.thingSnapshots.push_back(copy);
		command.sceneObjectOrigins.insert(thingObjectId(copy.id), thingObjectId(thing.id));
		command.entityIndexes.push_back(static_cast<int>(document->entities.size() + command.entitySnapshots.size()));
		command.entitySnapshots.push_back(doomThingMirror(copy));
		copies.push_back({LevelMapSelectionKind::DoomThing, copy.id});
	}
	command.selectionSnapshot = document->selection;
	command.selectionResult = copies;
	const int count = static_cast<int>(copies.size());
	command.description = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Duplicate as %1").arg(levelMapSelectionRefId(copies.first()))
					 : QCoreApplication::translate("VibeStudioLevelMap", "Duplicate %1 objects").arg(count);
	command.undoDescription = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Remove copy %1").arg(levelMapSelectionRefId(copies.first()))
					     : QCoreApplication::translate("VibeStudioLevelMap", "Remove %1 copies").arg(count);
	if (!applyLevelMapCommand(document, command, true)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The copies could not be added.");
		}
		return false;
	}
	pushUndo(document, command);
	setLevelMapSelection(document, copies);
	return true;
}

} // namespace

QVector<LevelMapDoomThingType> levelMapDoomThingTypes(LevelMapDoomFormat format)
{
	const QString starts = QCoreApplication::translate("VibeStudioLevelMap", "Starts");
	QVector<LevelMapDoomThingType> types {
		{1, QCoreApplication::translate("VibeStudioLevelMap", "Player 1 start"), starts},
		{2, QCoreApplication::translate("VibeStudioLevelMap", "Player 2 start"), starts},
		{3, QCoreApplication::translate("VibeStudioLevelMap", "Player 3 start"), starts},
		{4, QCoreApplication::translate("VibeStudioLevelMap", "Player 4 start"), starts},
		{11, QCoreApplication::translate("VibeStudioLevelMap", "Deathmatch start"), starts},
		{14, QCoreApplication::translate("VibeStudioLevelMap", "Teleport destination"), starts},
	};
	if (format != LevelMapDoomFormat::Doom) {
		return types;
	}
	// Doom and Doom II numbers; the Doom II-only ones are marked.
	const QString monsters = QCoreApplication::translate("VibeStudioLevelMap", "Monsters");
	const QString weapons = QCoreApplication::translate("VibeStudioLevelMap", "Weapons");
	const QString ammo = QCoreApplication::translate("VibeStudioLevelMap", "Ammunition");
	const QString health = QCoreApplication::translate("VibeStudioLevelMap", "Health and armor");
	const QString powerups = QCoreApplication::translate("VibeStudioLevelMap", "Power-ups");
	const QString keys = QCoreApplication::translate("VibeStudioLevelMap", "Keys");
	const QString obstacles = QCoreApplication::translate("VibeStudioLevelMap", "Obstacles and lights");
	types += {
		{3004, QCoreApplication::translate("VibeStudioLevelMap", "Zombieman"), monsters},
		{9, QCoreApplication::translate("VibeStudioLevelMap", "Shotgun guy"), monsters},
		{65, QCoreApplication::translate("VibeStudioLevelMap", "Heavy weapon dude (Doom II)"), monsters},
		{3001, QCoreApplication::translate("VibeStudioLevelMap", "Imp"), monsters},
		{3002, QCoreApplication::translate("VibeStudioLevelMap", "Demon"), monsters},
		{58, QCoreApplication::translate("VibeStudioLevelMap", "Spectre"), monsters},
		{3006, QCoreApplication::translate("VibeStudioLevelMap", "Lost soul"), monsters},
		{3005, QCoreApplication::translate("VibeStudioLevelMap", "Cacodemon"), monsters},
		{69, QCoreApplication::translate("VibeStudioLevelMap", "Hell knight (Doom II)"), monsters},
		{3003, QCoreApplication::translate("VibeStudioLevelMap", "Baron of Hell"), monsters},
		{68, QCoreApplication::translate("VibeStudioLevelMap", "Arachnotron (Doom II)"), monsters},
		{71, QCoreApplication::translate("VibeStudioLevelMap", "Pain elemental (Doom II)"), monsters},
		{66, QCoreApplication::translate("VibeStudioLevelMap", "Revenant (Doom II)"), monsters},
		{67, QCoreApplication::translate("VibeStudioLevelMap", "Mancubus (Doom II)"), monsters},
		{64, QCoreApplication::translate("VibeStudioLevelMap", "Arch-vile (Doom II)"), monsters},
		{7, QCoreApplication::translate("VibeStudioLevelMap", "Spiderdemon"), monsters},
		{16, QCoreApplication::translate("VibeStudioLevelMap", "Cyberdemon"), monsters},
		{2005, QCoreApplication::translate("VibeStudioLevelMap", "Chainsaw"), weapons},
		{2001, QCoreApplication::translate("VibeStudioLevelMap", "Shotgun"), weapons},
		{82, QCoreApplication::translate("VibeStudioLevelMap", "Super shotgun (Doom II)"), weapons},
		{2002, QCoreApplication::translate("VibeStudioLevelMap", "Chaingun"), weapons},
		{2003, QCoreApplication::translate("VibeStudioLevelMap", "Rocket launcher"), weapons},
		{2004, QCoreApplication::translate("VibeStudioLevelMap", "Plasma gun"), weapons},
		{2006, QCoreApplication::translate("VibeStudioLevelMap", "BFG9000"), weapons},
		{2007, QCoreApplication::translate("VibeStudioLevelMap", "Clip"), ammo},
		{2048, QCoreApplication::translate("VibeStudioLevelMap", "Box of bullets"), ammo},
		{2008, QCoreApplication::translate("VibeStudioLevelMap", "4 shotgun shells"), ammo},
		{2049, QCoreApplication::translate("VibeStudioLevelMap", "Box of shotgun shells"), ammo},
		{2010, QCoreApplication::translate("VibeStudioLevelMap", "Rocket"), ammo},
		{2046, QCoreApplication::translate("VibeStudioLevelMap", "Box of rockets"), ammo},
		{2047, QCoreApplication::translate("VibeStudioLevelMap", "Energy cell"), ammo},
		{17, QCoreApplication::translate("VibeStudioLevelMap", "Energy cell pack"), ammo},
		{8, QCoreApplication::translate("VibeStudioLevelMap", "Backpack"), ammo},
		{2011, QCoreApplication::translate("VibeStudioLevelMap", "Stimpack"), health},
		{2012, QCoreApplication::translate("VibeStudioLevelMap", "Medikit"), health},
		{2014, QCoreApplication::translate("VibeStudioLevelMap", "Health bonus"), health},
		{2015, QCoreApplication::translate("VibeStudioLevelMap", "Armor bonus"), health},
		{2018, QCoreApplication::translate("VibeStudioLevelMap", "Armor"), health},
		{2019, QCoreApplication::translate("VibeStudioLevelMap", "Megaarmor"), health},
		{2013, QCoreApplication::translate("VibeStudioLevelMap", "Supercharge"), powerups},
		{83, QCoreApplication::translate("VibeStudioLevelMap", "Megasphere (Doom II)"), powerups},
		{2022, QCoreApplication::translate("VibeStudioLevelMap", "Invulnerability"), powerups},
		{2023, QCoreApplication::translate("VibeStudioLevelMap", "Berserk"), powerups},
		{2024, QCoreApplication::translate("VibeStudioLevelMap", "Partial invisibility"), powerups},
		{2025, QCoreApplication::translate("VibeStudioLevelMap", "Radiation shielding suit"), powerups},
		{2026, QCoreApplication::translate("VibeStudioLevelMap", "Computer area map"), powerups},
		{2045, QCoreApplication::translate("VibeStudioLevelMap", "Light amplification visor"), powerups},
		{5, QCoreApplication::translate("VibeStudioLevelMap", "Blue keycard"), keys},
		{6, QCoreApplication::translate("VibeStudioLevelMap", "Yellow keycard"), keys},
		{13, QCoreApplication::translate("VibeStudioLevelMap", "Red keycard"), keys},
		{40, QCoreApplication::translate("VibeStudioLevelMap", "Blue skull key"), keys},
		{39, QCoreApplication::translate("VibeStudioLevelMap", "Yellow skull key"), keys},
		{38, QCoreApplication::translate("VibeStudioLevelMap", "Red skull key"), keys},
		{2035, QCoreApplication::translate("VibeStudioLevelMap", "Exploding barrel"), obstacles},
		{2028, QCoreApplication::translate("VibeStudioLevelMap", "Floor lamp"), obstacles},
		{34, QCoreApplication::translate("VibeStudioLevelMap", "Candle"), obstacles},
		{35, QCoreApplication::translate("VibeStudioLevelMap", "Candelabra"), obstacles},
		{44, QCoreApplication::translate("VibeStudioLevelMap", "Tall blue firestick"), obstacles},
		{46, QCoreApplication::translate("VibeStudioLevelMap", "Tall red firestick"), obstacles},
		{48, QCoreApplication::translate("VibeStudioLevelMap", "Tall techno pillar"), obstacles},
	};
	return types;
}

bool addLevelMapDoomThing(LevelMapDocument* document, int type, double x, double y, int angle, int* thingId, QString* error)
{
	int sceneResult_thingId = -1;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return addLevelMapDoomThing(candidate, type, x, y, angle, thingId ? &sceneResult_thingId : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && thingId) { *thingId = std::move(sceneResult_thingId); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || document->format != LevelMapFormat::DoomWad) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Things can be added to Doom and Hexen maps only."));
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	if (type <= 0 || type > 65535) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A thing type is a DoomEd number from 1 to 65535."));
	}
	if (!fitsDoomThingCoordinate(x) || !fitsDoomThingCoordinate(y)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A thing must lie within -32768 to 32767 on each axis."));
	}
	LevelMapDoomThing thing;
	thing.id = nextDoomThingId(*document);
	thing.x = std::round(x);
	thing.y = std::round(y);
	thing.angle = ((angle % 360) + 360) % 360;
	thing.type = type;
	// Every skill; for Hexen also every class and game mode.
	// https://doomwiki.org/wiki/Thing#Flags
	thing.flags = document->doomFormat == LevelMapDoomFormat::Hexen ? 0x7E7 : 0x0007;

	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("add-objects");
	command.objectKind = QStringLiteral("thing");
	command.objectId = thing.id;
	command.thingSnapshots.push_back(thing);
	command.thingIndexes.push_back(static_cast<int>(document->doomThings.size()));
	command.entitySnapshots.push_back(doomThingMirror(thing));
	command.entityIndexes.push_back(static_cast<int>(document->entities.size()));
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Add type %1 as %2").arg(type).arg(thingObjectId(thing.id));
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Remove added %1").arg(thingObjectId(thing.id));
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The new thing could not be added."));
	}
	pushUndo(document, command);
	selectLevelMapObject(document, thingObjectId(thing.id));
	if (thingId) {
		*thingId = thing.id;
	}
	return true;
}

namespace {

// A copied or pasted primitive that joins an entity from the file goes ahead
// of the entity's closing brace, so that brace needs a line of its own.
bool entityAcceptsCopies(const LevelMapDocument* document, int entityId)
{
	const LevelMapEntity* owner = entityById(document, entityId);
	return owner && (owner->startLine <= 0
		|| (owner->endLine > 1 && owner->endLine <= document->textLines.size()
			&& document->textLines.at(owner->endLine - 1).trimmed() == QStringLiteral("}")));
}

// A primitive's text as it would be saved now: its source lines untouched
// while it has not moved, or its source or copy template with its current
// points.
QStringList brushTextNow(const LevelMapDocument& document, const LevelMapBrush& brush)
{
	if (brush.startLine <= 0) {
		return copiedBrushLines(brush);
	}
	const QStringList source = document.textLines.mid(brush.startLine - 1, brush.endLine - brush.startLine + 1);
	if (!brush.geometryDirty && !brush.texturesDirty && !brush.textureParametersDirty) {
		return source;
	}
	LevelMapBrush current = brush;
	current.sourceLines = source;
	current.sourceFirstLine = brush.startLine;
	return copiedBrushLines(current);
}

QStringList patchTextNow(const LevelMapDocument& document, const LevelMapPatch& patch)
{
	if (patch.definitionDirty) {
		return levelPatchDefinition(patch);
	}
	if (patch.startLine <= 0) {
		return copiedPatchLines(patch);
	}
	const QStringList source = document.textLines.mid(patch.startLine - 1, patch.endLine - patch.startLine + 1);
	if ((!patch.geometryDirty || !patch.controlGridNormalized) && !patch.textureDirty) {
		return source;
	}
	LevelMapPatch current = patch;
	current.sourceLines = source;
	current.sourceFirstLine = patch.startLine;
	return copiedPatchLines(current);
}

} // namespace

QString levelMapSelectionText(const LevelMapDocument& document, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!isTextMapFormat(document.format) || document.selection.isEmpty()) {
		return {};
	}
	QSet<int> entityIds;
	QSet<int> brushIds;
	QSet<int> patchIds;
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::Entity) {
			entityIds.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushIds.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			patchIds.insert(ref.objectId);
		}
	}
	// A selected worldspawn contributes its brushes and patches, bare; any
	// other selected entity is copied whole.
	QSet<int> wholeEntities;
	for (const LevelMapEntity& entity : document.entities) {
		if (entityIds.contains(entity.id)) {
			if (isWorldspawnEntity(entity)) {
				for (const LevelMapBrush& brush : document.brushes) {
					if (brush.entityId == entity.id) {
						brushIds.insert(brush.id);
					}
				}
				for (const LevelMapPatch& patch : document.patches) {
					if (patch.entityId == entity.id) {
						patchIds.insert(patch.id);
					}
				}
			} else {
				wholeEntities.insert(entity.id);
			}
		}
	}
	for (const LevelMapBrush& brush : document.brushes) {
		if ((brushIds.contains(brush.id) || wholeEntities.contains(brush.entityId)) && !objectOwnsItsLines(document, brush.startLine, brush.endLine)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 shares a source line with other map text, so it cannot be copied without that text.").arg(brush.id);
			}
			return {};
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if ((patchIds.contains(patch.id) || wholeEntities.contains(patch.entityId)) && !objectOwnsItsLines(document, patch.startLine, patch.endLine)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 shares a source line with other map text, so it cannot be copied without that text.").arg(patch.id);
			}
			return {};
		}
	}
	QStringList lines;
	for (const LevelMapBrush& brush : document.brushes) {
		if (brushIds.contains(brush.id) && !wholeEntities.contains(brush.entityId)) {
			lines << brushTextNow(document, brush);
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (patchIds.contains(patch.id) && !wholeEntities.contains(patch.entityId)) {
			lines << patchTextNow(document, patch);
		}
	}
	for (const LevelMapEntity& entity : document.entities) {
		if (!wholeEntities.contains(entity.id)) {
			continue;
		}
		lines << QStringLiteral("{");
		for (const LevelMapProperty& property : entity.properties) {
			lines << formatKeyLine(QString(), property.key, property.value);
		}
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.entityId == entity.id) {
				lines << brushTextNow(document, brush);
			}
		}
		for (const LevelMapPatch& patch : document.patches) {
			if (patch.entityId == entity.id) {
				lines << patchTextNow(document, patch);
			}
		}
		lines << QStringLiteral("}");
	}
	return lines.isEmpty() ? QString() : lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool pasteLevelMapText(LevelMapDocument* document, const QString& text, QString* error)
{
	return pasteLevelMapText(document, text, {0, 0, 0, true}, {false, false}, error);
}

bool pasteLevelMapText(LevelMapDocument* document, const QString& text, const LevelMapVec3& offset,
	const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return pasteLevelMapText(candidate, text, offset, textures, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Paste works on Quake-family .map files."));
	}
	if (text.size() > kLevelMapClipboardMaxCharacters || text.contains(QChar::Null)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The pasted map text contains NUL or exceeds 4,194,304 characters. Split large assemblies into smaller parts."));
	}
	if (!offset.valid || !std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Placement needs a finite offset on each axis."));
	}
	// Sort the top-level blocks: an entity block opens with a key, a bare
	// brush or patch block with a plane or a primitive keyword.
	detail::placementCheckpoint(LevelPlacementPhase::Parsing);
	QStringList comments;
	const QVector<MapToken> tokens = tokenizeMapText(text, &comments);
	// Tokenization has already excluded quoted values and comments. Bound the
	// solver's input before parsing any brush, including malformed clipboards.
	QVector<int> roots{0};
	int parens = 0, total = 0, blocks = 0;
	for (const auto& token : tokens) {
		detail::placementCancellationCheckpoint();
		if (tokenIsPunct(token, QLatin1Char('{'))) {
			if (parens || roots.size() >= 8 || ++blocks > 8192) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "Pasted map nesting or block count exceeds the supported limit."));
			}
			roots << 0;
		} else if (tokenIsPunct(token, QLatin1Char('}'))) {
			if (parens || roots.size() == 1) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "The pasted map text has unbalanced geometry blocks."));
			}
			roots.removeLast();
		} else if (tokenIsPunct(token, QLatin1Char('('))) {
			if ((parens == 0 && ++roots.last() > 512) || ++parens > 64 || ++total > 250000) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "Pasted geometry exceeds the bounded parser limits."));
			}
		} else if (tokenIsPunct(token, QLatin1Char(')')) && --parens < 0) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The pasted map text has unbalanced parentheses."));
		}
	}
	if (parens || roots.size() != 1) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The pasted map text ends inside a geometry block."));
	}
	QStringList textLines = text.split(QLatin1Char('\n'));
	for (QString& line : textLines) {
		if (line.endsWith(QLatin1Char('\r'))) {
			line.chop(1);
		}
	}
	QStringList primitives;
	QStringList entities;
	int lastLine = 0;
	for (int index = 0; index < tokens.size();) {
		const MapToken& open = tokens.at(index);
		if (!tokenIsPunct(open, QLatin1Char('{'))) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The clipboard does not hold .map text."));
		}
		const int close = matchingBraceIndex(tokens, index);
		if (close < 0) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The .map text on the clipboard ends before a block closes."));
		}
		const int startLine = open.line;
		const int endLine = tokens.at(close).line;
		if (startLine <= lastLine) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The .map text on the clipboard puts two blocks on one line."));
		}
		const QStringList block = textLines.mid(startLine - 1, endLine - startLine + 1);
		if (tokenAt(tokens, index + 1).type == MapTokenType::String) {
			entities << block;
		} else {
			primitives << block;
		}
		lastLine = endLine;
		index = close + 1;
	}
	if (primitives.isEmpty() && entities.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The clipboard does not hold .map text."));
	}
	QStringList normalized;
	if (!primitives.isEmpty()) {
		normalized << QStringLiteral("{") << QStringLiteral("\"classname\" \"worldspawn\"") << primitives << QStringLiteral("}");
	}
	normalized << entities;
	LevelMapDocument pasted;
	parseQuakeMapText(normalized.join(QLatin1Char('\n')), document->sourcePath, document->engineFamily, &pasted);
	for (const LevelMapIssue& issue : pasted.issues) {
		detail::placementCancellationCheckpoint();
		if (issue.severity == LevelMapIssueSeverity::Error) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The .map text on the clipboard could not be read: %1").arg(issue.message));
		}
	}
	QVector<LevelMapSelectionRef> placementObjects;
	const auto sourceDialect = levelMapBrushDialect(pasted), destinationDialect = levelMapBrushDialect(*document);
	if (sourceDialect == QStringLiteral("mixed") || (!sourceDialect.isEmpty() && !destinationDialect.isEmpty() && sourceDialect != destinationDialect)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Pasted brushes and the destination use different texture dialects. Convert them to the same format before insertion."));
	}
	if (!pasted.patches.isEmpty() && document->format != LevelMapFormat::Quake3Map) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Pasted patches require a Quake III-family map."));
	}
	for (const auto& entity : pasted.entities) {
		detail::placementCancellationCheckpoint();
		if (!isWorldspawnEntity(entity)) { placementObjects << LevelMapSelectionRef{LevelMapSelectionKind::Entity, entity.id}; }
	}
	for (const auto& brush : pasted.brushes) {
		detail::placementCancellationCheckpoint(); placementObjects << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, brush.id}; }
	for (const auto& patch : pasted.patches) {
		detail::placementCancellationCheckpoint(); placementObjects << LevelMapSelectionRef{LevelMapSelectionKind::QuakePatch, patch.id}; }
	LevelMapUndoCommand placement;
	if (!prepareLevelMapPlacement(pasted, placementObjects, offset, textures, &placement, error)) { return false; }
	// Apply only to the isolated parsed payload. The destination receives one
	// fully validated add-objects command, including its scene assignment.
	for (const auto& value : placement.entityResults) {
		detail::placementCancellationCheckpoint(); *entityById(&pasted, value.id) = value; }
	for (const auto& value : placement.brushResults) {
		detail::placementCancellationCheckpoint(); *brushById(&pasted, value.id) = value; }
	for (const auto& value : placement.patchResults) {
		detail::placementCancellationCheckpoint(); *patchById(&pasted, value.id) = value; }

	int worldspawnId = -1;
	for (const LevelMapEntity& entity : document->entities) {
		detail::placementCancellationCheckpoint();
		if (isWorldspawnEntity(entity)) {
			worldspawnId = entity.id;
			break;
		}
	}
	int nextEntity = 0;
	for (const LevelMapEntity& entity : document->entities) {
		detail::placementCancellationCheckpoint();
		nextEntity = std::max(nextEntity, entity.id + 1);
	}
	int nextBrush = 0;
	for (const LevelMapBrush& brush : document->brushes) {
		detail::placementCancellationCheckpoint();
		nextBrush = std::max(nextBrush, brush.id + 1);
	}
	int nextPatch = 0;
	for (const LevelMapPatch& patch : document->patches) {
		detail::placementCancellationCheckpoint();
		nextPatch = std::max(nextPatch, patch.id + 1);
	}
	detail::placementCheckpoint(LevelPlacementPhase::Inserting);
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("add-objects");
	command.objectKind = QStringLiteral("selection");
	QVector<LevelMapSelectionRef> added;
	QHash<int, int> owners;
	for (const LevelMapEntity& entity : pasted.entities) {
		detail::placementCancellationCheckpoint();
		if (isWorldspawnEntity(entity)) {
			if (worldspawnId < 0) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "The open map has no worldspawn to hold the pasted brushes."));
			}
			if (!entityAcceptsCopies(document, worldspawnId)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "worldspawn closes on a line shared with other map text, so brushes cannot be pasted into it."));
			}
			owners.insert(entity.id, worldspawnId);
			continue;
		}
		LevelMapEntity copy = entity;
		copy.id = nextEntity++;
		copy.startLine = 0;
		copy.endLine = 0;
		copy.selected = false;
		for (LevelMapProperty& property : copy.properties) {
			property.line = 0;
		}
		owners.insert(entity.id, copy.id);
		command.entityIndexes.push_back(static_cast<int>(document->entities.size() + command.entitySnapshots.size()));
		command.entitySnapshots.push_back(copy);
		added.push_back({LevelMapSelectionKind::Entity, copy.id});
	}
	// Each pasted primitive becomes a template of its own lines, so those lines
	// must hold it alone; a patch also needs a grid that read cleanly.
	for (const LevelMapBrush& brush : pasted.brushes) {
		detail::placementCancellationCheckpoint();
		if (!objectOwnsItsLines(pasted, brush.startLine, brush.endLine)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The .map text on the clipboard puts a brush's brace on a line with other text."));
		}
	}
	for (const LevelMapPatch& patch : pasted.patches) {
		detail::placementCancellationCheckpoint();
		if (!objectOwnsItsLines(pasted, patch.startLine, patch.endLine) || !patch.controlGridNormalized) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "A patch in the .map text on the clipboard could not be read cleanly."));
		}
	}
	for (const LevelMapBrush& brush : pasted.brushes) {
		detail::placementCancellationCheckpoint();
		LevelMapBrush copy = brush;
		copy.sourceLines = pasted.textLines.mid(brush.startLine - 1, brush.endLine - brush.startLine + 1);
		copy.sourceFirstLine = brush.startLine;
		copy.id = nextBrush++;
		copy.entityId = owners.value(brush.entityId, worldspawnId);
		copy.startLine = 0;
		copy.endLine = 0;
		copy.selected = false;
		command.brushIndexes.push_back(static_cast<int>(document->brushes.size() + command.brushSnapshots.size()));
		command.brushSnapshots.push_back(copy);
		if (copy.entityId == worldspawnId) {
			added.push_back({LevelMapSelectionKind::QuakeBrush, copy.id});
		}
	}
	for (const LevelMapPatch& patch : pasted.patches) {
		detail::placementCancellationCheckpoint();
		LevelMapPatch copy = patch;
		copy.sourceLines = pasted.textLines.mid(patch.startLine - 1, patch.endLine - patch.startLine + 1);
		copy.sourceFirstLine = patch.startLine;
		copy.id = nextPatch++;
		copy.entityId = owners.value(patch.entityId, worldspawnId);
		copy.startLine = 0;
		copy.endLine = 0;
		copy.selected = false;
		command.patchIndexes.push_back(static_cast<int>(document->patches.size() + command.patchSnapshots.size()));
		command.patchSnapshots.push_back(copy);
		if (copy.entityId == worldspawnId) {
			added.push_back({LevelMapSelectionKind::QuakePatch, copy.id});
		}
	}
	if (added.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The .map text on the clipboard holds nothing to paste."));
	}
	const int count = static_cast<int>(added.size());
	command.description = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Paste %1").arg(levelMapSelectionRefId(added.first())) : QCoreApplication::translate("VibeStudioLevelMap", "Paste %1 objects").arg(count);
	command.undoDescription = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Remove pasted %1").arg(levelMapSelectionRefId(added.first()))
					     : QCoreApplication::translate("VibeStudioLevelMap", "Remove %1 pasted objects").arg(count);
	command.selectionSnapshot = document->selection;
	command.selectionResult = added;
	detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The pasted objects could not be added."));
	}
	pushUndo(document, command);
	setLevelMapSelection(document, added);
	return true;
}

namespace {

// How a brush's faces are written: its kind (classic, valve220, brushDef, or
// brushDef3) and, for the first two, "-flags" when `faceLine` carries the
// three Quake II and III flag numbers after the scale.
QString brushFaceStyle(const LevelMapBrush& brush, const QString& faceLine)
{
	QString kind = brush.primitiveKind.isEmpty() ? QStringLiteral("classic") : brush.primitiveKind;
	if (kind == QStringLiteral("classic") || kind == QStringLiteral("valve220")) {
		QString tail = faceLine;
		qsizetype start = 0;
		qsizetype end = 0;
		if (faceTextureSpan(tail, &start, &end)) {
			tail = tail.mid(end);
			tail = tail.left(tail.indexOf(QStringLiteral("//")) >= 0 ? tail.indexOf(QStringLiteral("//")) : tail.size());
			tail.remove(QRegularExpression(QStringLiteral(R"(\[[^\]]*\])")));
			tail.remove(QLatin1Char('}'));
			const int numbers = static_cast<int>(tail.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts).size());
			if (numbers >= (kind == QStringLiteral("classic") ? 8 : 6)) {
				kind += QStringLiteral("-flags");
			}
		}
	}
	return kind;
}

// A face to write: three points wound so (p0 - p1) x (p2 - p1) is `normal`,
// its outward normal, the way Quake's compilers read a face.
struct NewBrushFace {
	LevelMapVec3 p0;
	LevelMapVec3 p1;
	LevelMapVec3 p2;
	LevelMapVec3 normal;
	QString texture;
	qint64 contents = 0;
	qint64 surface = 0;
	qint64 value = 0;
};

// One face line in `style` (see brushFaceStyle()). Valve 220 faces get the
// paraxial axes TrenchBroom gives a new face, and brushDef faces the matrix
// Radiant gives one.
QString brushFaceLine(const QString& style, const NewBrushFace& face)
{
	const auto point = [](const LevelMapVec3& p) {
		return QStringLiteral("( %1 %2 %3 )").arg(mapCoordinateText(p.x), mapCoordinateText(p.y), mapCoordinateText(p.z));
	};
	const QString points = QStringLiteral("%1 %2 %3").arg(point(face.p0), point(face.p1), point(face.p2));
	const QString matrix = QStringLiteral("( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) )");
	const QString flags = QStringLiteral("%1 %2 %3").arg(face.contents).arg(face.surface).arg(face.value);
	if (style == QStringLiteral("brushDef3")) {
		const LevelMapVec3& n = face.normal;
		const double distance = -((n.x * face.p1.x) + (n.y * face.p1.y) + (n.z * face.p1.z));
		return QStringLiteral("( %1 %2 %3 %4 ) %5 \"%6\" %7")
			.arg(mapCoordinateText(n.x), mapCoordinateText(n.y), mapCoordinateText(n.z), mapCoordinateText(distance), matrix, face.texture, flags);
	}
	if (style == QStringLiteral("brushDef")) {
		return QStringLiteral("%1 %2 %3 %4").arg(points, matrix, face.texture, flags);
	}
	const QString tail = style.endsWith(QStringLiteral("-flags")) ? QLatin1Char(' ') + flags : QString();
	if (style.startsWith(QStringLiteral("valve220"))) {
		const double ax = std::abs(face.normal.x);
		const double ay = std::abs(face.normal.y);
		const double az = std::abs(face.normal.z);
		const bool floor = az >= ax && az >= ay;
		const QString u = floor || ay > ax ? QStringLiteral("1 0 0") : QStringLiteral("0 1 0");
		const QString v = floor ? QStringLiteral("0 -1 0") : QStringLiteral("0 0 -1");
		return QStringLiteral("%1 %2 [ %3 0 ] [ %4 0 ] 0 1 1%5").arg(points, face.texture, u, v, tail);
	}
	return QStringLiteral("%1 %2 0 0 0 1 1%3").arg(points, face.texture, tail);
}

// Reads brush text back with the map parser, so a new brush's faces, bounds,
// and kind come from the same code as every loaded brush. Null when the text
// does not make one closed brush.
std::optional<LevelMapBrush> parsedNewBrush(const LevelMapDocument& document, const QStringList& lines)
{
	LevelMapDocument parsed;
	QStringList wrapped {QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\"")};
	wrapped << lines << QStringLiteral("}");
	parseQuakeMapText(wrapped.join(QLatin1Char('\n')), document.sourcePath,
		document.format == LevelMapFormat::Quake3Map ? QStringLiteral("idtech3") : QStringLiteral("idtech2"), &parsed);
	if (parsed.brushes.size() != 1 || !parsed.brushes.first().boundsSolved) {
		return std::nullopt;
	}
	LevelMapBrush brush = parsed.brushes.first();
	brush.sourceLines = lines;
	brush.sourceFirstLine = brush.startLine;
	brush.startLine = 0;
	brush.endLine = 0;
	brush.selected = false;
	return brush;
}

} // namespace

bool addLevelMapBoxBrush(LevelMapDocument* document, const LevelMapVec3& mins, const LevelMapVec3& maxs, const QString& texture,
	int* brushId, QString* error)
{
	int sceneResult_brushId = -1;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return addLevelMapBoxBrush(candidate, mins, maxs, texture, brushId ? &sceneResult_brushId : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && brushId) { *brushId = std::move(sceneResult_brushId); }
		return *guarded;
	}

	LevelBrushPrimitiveRequest request;
	request.mins = mins;
	request.maxs = maxs;
	request.texture = texture;
	return addLevelMapBrushPrimitive(document, request, brushId, error);
}

bool addLevelMapBrushPrimitive(LevelMapDocument* document, const LevelBrushPrimitiveRequest& request, int* brushId, QString* error)
{
	int sceneResult_brushId = -1;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return addLevelMapBrushPrimitive(candidate, request, brushId ? &sceneResult_brushId : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && brushId) { *brushId = std::move(sceneResult_brushId); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brushes can be added to Quake-family .map files only."));
	}
	LevelMapBrush geometryDraft;
	detail::placementCheckpoint(LevelPlacementPhase::Building);
	if (!createLevelBrushPrimitive(request, &geometryDraft, error)) { return false; }
	const auto& mins = request.mins;
	const auto& maxs = request.maxs;
	const QString name = request.texture.trimmed();
	int worldspawnId = -1;
	for (const LevelMapEntity& entity : document->entities) {
		detail::placementCancellationCheckpoint();
		if (isWorldspawnEntity(entity)) {
			worldspawnId = entity.id;
			break;
		}
	}
	if (worldspawnId < 0) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The map has no worldspawn to hold a new brush."));
	}
	if (!entityAcceptsCopies(document, worldspawnId)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "worldspawn closes on a line shared with other map text, so a brush cannot be added to it."));
	}

	// Write the faces the way the map's own brushes are written: the kind of
	// the first brush in the file, and whether its face lines carry the three
	// Quake II and III flag numbers after the scale.
	QString kind = document->format == LevelMapFormat::Quake3Map ? QStringLiteral("classic-flags") : QStringLiteral("classic");
	for (const LevelMapBrush& brush : document->brushes) {
		detail::placementCancellationCheckpoint();
		if (brush.faces.isEmpty()) {
			continue;
		}
		const auto lines = brushTextNow(*document, brush);
		const int firstLine = brush.startLine > 0 ? brush.startLine : brush.sourceFirstLine;
		kind = brushFaceStyle(brush, lines.value(brush.faces.first().line - firstLine));
		break;
	}

	// Each face as three points wound so (p0 - p1) x (p2 - p1) points out of
	// the box, the way Quake's compilers read a face.
	const double x0 = mins.x;
	const double y0 = mins.y;
	const double z0 = mins.z;
	const double x1 = maxs.x;
	const double y1 = maxs.y;
	const double z1 = maxs.z;
	QVector<NewBrushFace> faces {
		{{x0 + 1, y0, z1, true}, {x0, y0, z1, true}, {x0, y0 + 1, z1, true}, {0, 0, 1, true}, name},
		{{x0, y0 + 1, z0, true}, {x0, y0, z0, true}, {x0 + 1, y0, z0, true}, {0, 0, -1, true}, name},
		{{x1, y0 + 1, z0, true}, {x1, y0, z0, true}, {x1, y0, z0 + 1, true}, {1, 0, 0, true}, name},
		{{x0, y0, z0 + 1, true}, {x0, y0, z0, true}, {x0, y0 + 1, z0, true}, {-1, 0, 0, true}, name},
		{{x0, y1, z0 + 1, true}, {x0, y1, z0, true}, {x0 + 1, y1, z0, true}, {0, 1, 0, true}, name},
		{{x0 + 1, y0, z0, true}, {x0, y0, z0, true}, {x0, y0, z0 + 1, true}, {0, -1, 0, true}, name},
	};
	if (request.shape != QStringLiteral("box")) {
		faces.clear();
		for (const auto& face : geometryDraft.faces) {
			const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
			faces << NewBrushFace{face.p0, face.p1, face.p2, {plane.normalX, plane.normalY, plane.normalZ, true}, name};
		}
	}
	QStringList lines {QStringLiteral("{")};
	if (kind == QStringLiteral("brushDef") || kind == QStringLiteral("brushDef3")) {
		lines << kind << QStringLiteral("{");
	}
	for (const NewBrushFace& face : faces) {
		lines << brushFaceLine(kind, face);
	}
	if (kind == QStringLiteral("brushDef") || kind == QStringLiteral("brushDef3")) {
		lines << QStringLiteral("}");
	}
	lines << QStringLiteral("}");

	const std::optional<LevelMapBrush> built = parsedNewBrush(*document, lines);
	if (!built) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The new brush could not be built."));
	}
	LevelMapBrush brush = *built;
	LevelBrushTopology topology;
	if (!levelBrushTopology(brush, &topology, error)) { return false; }
	const auto close = [](const LevelMapVec3& a, const LevelMapVec3& b) {
		return std::max({std::abs(a.x-b.x), std::abs(a.y-b.y), std::abs(a.z-b.z)}) < 0.02;
	};
	if (!close(brush.mins, mins) || !close(brush.maxs, maxs)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The primitive's bounds cannot be saved accurately in this face dialect. Reduce detail or move it closer to the origin."));
	}
	brush.entityId = worldspawnId;
	brush.id = 0;
	for (const LevelMapBrush& existing : document->brushes) {
		detail::placementCancellationCheckpoint();
		brush.id = std::max(brush.id, existing.id + 1);
	}

	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("add-objects");
	command.objectKind = QStringLiteral("brush");
	command.objectId = brush.id;
	command.brushSnapshots.push_back(brush);
	command.brushIndexes.push_back(static_cast<int>(document->brushes.size()));
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Add %1 brush (%2 faces)").arg(levelBrushPrimitiveLabel(request.shape)).arg(brush.faceCount);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Remove added brush %1").arg(brush.id);
	command.selectionSnapshot = document->selection;
	command.selectionResult = {{LevelMapSelectionKind::QuakeBrush, brush.id}};
	detail::placementCheckpoint(LevelPlacementPhase::Inserting);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The new brush could not be added."));
	}
	pushUndo(document, command);
	setLevelMapSelection(document, {{LevelMapSelectionKind::QuakeBrush, brush.id}});
	if (brushId) {
		*brushId = brush.id;
	}
	return true;
}

bool addLevelMapPatch(LevelMapDocument* document, const LevelMapPatch& source, int* patchId, QString* error)
{
	int sceneResult_patchId = -1;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return addLevelMapPatch(candidate, source, patchId ? &sceneResult_patchId : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && patchId) { *patchId = std::move(sceneResult_patchId); }
		return *guarded;
	}

	int owner = -1;
	if (document) {
		for (const auto& entity : document->entities) {
			if (isWorldspawnEntity(entity)) { owner = entity.id; break; }
		}
	}
	QVector<int> ids;
	if (!addLevelMapPatches(document, {source}, owner, &ids, error)) { return false; }
	if (patchId) { *patchId = ids.first(); }
	return true;
}

bool addLevelMapPatches(LevelMapDocument* document, const QVector<LevelMapPatch>& sources, int owner, QVector<int>* patchIds, QString* error)
{
	QVector<int> sceneResult_patchIds = patchIds ? *patchIds : QVector<int>{};
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return addLevelMapPatches(candidate, sources, owner, patchIds ? &sceneResult_patchIds : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && patchIds) { *patchIds = std::move(sceneResult_patchIds); }
		return *guarded;
	}

	if (error) { error->clear(); }
	const auto fail = [error](const char* message) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", message); }
		return false;
	};
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "Patches can be added to Quake III-family maps only."));
	}
	if (sources.isEmpty() || sources.size() > 64) {
		return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "Add between one and 64 patches at a time."));
	}
	if (owner < 0 || !entityById(document, owner) || !entityAcceptsCopies(document, owner)) {
		return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "Choose an existing patch owner with its closing brace on a separate line."));
	}
	qint64 nextId = 0;
	for (const auto& patch : document->patches) { nextId = std::max(nextId, static_cast<qint64>(patch.id) + 1); }
	if (nextId + sources.size() - 1 > std::numeric_limits<int>::max()) {
		return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The map has exhausted its patch identifiers."));
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("add-objects");
	command.objectKind = QStringLiteral("patch");
	QVector<int> ids;
	QVector<LevelMapSelectionRef> selection;
	for (const auto& source : sources) {
		if (!validateLevelPatch(source, error)) { return false; }
		const QStringList lines = levelPatchDefinition(source);
		QStringList wrapped {QStringLiteral("{"), QStringLiteral("\"classname\" \"worldspawn\"")};
		wrapped << lines << QStringLiteral("}");
		LevelMapDocument parsed;
		parseQuakeMapText(wrapped.join(QLatin1Char('\n')), document->sourcePath, QStringLiteral("idtech3"), &parsed);
		if (parsed.patches.size() != 1 || !validateLevelPatch(parsed.patches.first(), error)) { return false; }
		LevelMapPatch added = parsed.patches.first();
		added.sourceLines = lines;
		added.sourceFirstLine = added.startLine;
		added.startLine = added.endLine = 0;
		added.entityId = owner;
		added.id = static_cast<int>(nextId++);
		command.objectId = added.id;
		command.patchIndexes << static_cast<int>(document->patches.size() + command.patchSnapshots.size());
		command.patchSnapshots << added;
		ids << added.id;
		selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakePatch, added.id};
	}
	if (sources.size() == 1) {
		const auto& added = command.patchSnapshots.first();
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Add patch %1 (%2 × %3)").arg(added.id).arg(added.width).arg(added.height);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Remove added patch %1").arg(added.id);
	} else {
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Add %1 patches").arg(sources.size());
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Remove %1 added patches").arg(sources.size());
	}
	if (!applyLevelMapCommand(document, command, true)) { return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The patch could not be added.")); }
	pushUndo(document, command);
	setLevelMapSelection(document, selection);
	if (patchIds) { *patchIds = ids; }
	return true;
}

bool replaceLevelMapBrushGeometry(LevelMapDocument* document, int brushId, const LevelMapBrush& replacement, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return replaceLevelMapBrushGeometry(candidate, brushId, replacement, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) { error->clear(); }
	const auto fail = [error](const char* message) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", message); }
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "Open a Quake-family map to edit brush components."));
	}
	const auto* before = brushById(document,brushId);
	if (!before) { return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The brush no longer exists.")); }
	LevelBrushTopology topology;
	if (!levelBrushTopology(*before,&topology,error) || !levelBrushTopology(replacement,&topology,error)) { return false; }
	if (!objectOwnsItsLines(*document,before->startLine,before->endLine) || !entityAcceptsCopies(document,before->entityId)) {
		return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "Put this brush's and its entity's braces on separate lines before editing components."));
	}
	const auto source = brushTextNow(*document,*before);
	const int first = before->startLine > 0 ? before->startLine : before->sourceFirstLine;
	const auto withoutComments = [](QString line) {
		// The map lexer distinguishes quoted material paths from comments.
		QStringList comments;
		tokenizeMapText(line,&comments);
		for (const auto& comment : comments) { line.remove(comment); }
		return line;
	};
	QMap<int,QStringList> rewritten;
	for (const auto& face : before->faces) {
		const int index = face.line-first;
		if (index <= 0 || index >= source.size()-1 || rewritten.contains(index)) {
			return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "Put each brush face on a separate line before editing components."));
		}
		rewritten.insert(index,{});
	}
	for (const auto& face : replacement.faces) {
		const int index = face.line-first;
		const auto donor = std::find_if(before->faces.cbegin(),before->faces.cend(),[&](const auto& f) { return f.line == face.line; });
		if (!rewritten.contains(index) || donor == before->faces.cend() || donor->explicitPlane != face.explicitPlane) {
			return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The geometry draft has a stale face binding. Reopen the brush editor."));
		}
		QString line = face.explicitPlane ? replacePlaneGroup(source[index],face.planeNormal,face.planeDistance)
			: replaceLeadingPointGroups(source[index],{face.p0,face.p1,face.p2});
		// Keep a source face's comment once, even when bending splits it.
		if (!rewritten[index].isEmpty()) {
			line = withoutComments(line);
		}
		rewritten[index] << line;
	}
	QStringList lines;
	for (int i = 0; i < source.size(); ++i) {
		if (!rewritten.contains(i)) { lines << source[i]; continue; }
		if (rewritten[i].isEmpty()) {
			QStringList comments;
			tokenizeMapText(source[i],&comments);
			lines << comments;
		} else { lines << rewritten[i]; }
	}
	if (lines == source) { return true; }
	auto parsed = parsedNewBrush(*document,lines);
	if (!parsed || parsed->faces.size() != replacement.faces.size() || !levelBrushTopology(*parsed,&topology,error)) {
		return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The brush could not round-trip its edited geometry. Put complete face definitions on separate lines."));
	}
	for (const auto& face : replacement.faces) {
		const auto plane = planeFromPoints(face.p0,face.p1,face.p2);
		if (std::none_of(topology.geometry.faces.cbegin(),topology.geometry.faces.cend(),[&](const auto& f) {
			return std::abs(f.plane.normalX-plane.normalX) < 1e-5 && std::abs(f.plane.normalY-plane.normalY) < 1e-5
				&& std::abs(f.plane.normalZ-plane.normalZ) < 1e-5 && std::abs(f.plane.distance-plane.distance) < 0.02;
		})) { return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The map dialect could not preserve the requested brush planes.")); }
	}
	if (before->faces.size() == parsed->faces.size() && std::all_of(before->faces.cbegin(),before->faces.cend(),[&](const auto& face) {
		const auto plane = planeFromPoints(face.p0,face.p1,face.p2);
		return std::any_of(topology.geometry.faces.cbegin(),topology.geometry.faces.cend(),[&](const auto& f) {
			return std::abs(f.plane.normalX-plane.normalX) < 1e-9 && std::abs(f.plane.normalY-plane.normalY) < 1e-9
				&& std::abs(f.plane.normalZ-plane.normalZ) < 1e-9 && std::abs(f.plane.distance-plane.distance) < 1e-7;
		});
	})) { return true; }
	parsed->id = before->id; parsed->entityId = before->entityId; parsed->selected = before->selected;
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("edit-brush");
	command.objectKind = QStringLiteral("brush"); command.objectId = brushId;
	command.brushSnapshots << *before; command.brushResults << *parsed;
	if (before->startLine > 0) { command.deletedLineRanges << LevelMapLineRange{before->startLine,before->endLine}; }
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Edit brush %1 components").arg(brushId);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Restore brush %1 components").arg(brushId);
	if (!applyLevelMapCommand(document,command,true)) { return false; }
	pushUndo(document,command);
	return true;
}

bool replaceLevelMapPatch(LevelMapDocument* document, int patchId, const LevelMapPatch& replacement, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return replaceLevelMapPatch(candidate, patchId, replacement, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	return replaceLevelMapPatches(document, {{patchId, replacement}}, error);
}

bool replaceLevelMapPatches(LevelMapDocument* document, const QMap<int, LevelMapPatch>& replacements, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return replaceLevelMapPatches(candidate, replacements, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) { error->clear(); }
	const auto fail = [error](const char* message) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", message); }
		return false;
	};
	if (!document || document->format != LevelMapFormat::Quake3Map) { return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "Open a Quake III-family map to edit a patch.")); }
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("edit-patch");
	command.objectKind = QStringLiteral("patch");
	for (auto entry = replacements.cbegin(); entry != replacements.cend(); ++entry) {
		const int patchId = entry.key();
		const auto& replacement = entry.value();
		const LevelMapPatch* before = patchById(document, patchId);
		if (!before) { return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The patch no longer exists.")); }
		if (!validateLevelPatch(*before, error) || !validateLevelPatch(replacement, error)) { return false; }
		if (!objectOwnsItsLines(*document, before->startLine, before->endLine)) {
			return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The patch shares its opening or closing line with other map text. Put its braces on separate lines before editing its control grid."));
		}
		LevelMapPatch after = *before;
		after.width = replacement.width; after.height = replacement.height;
		after.controlPoints = replacement.controlPoints;
		after.controlU = replacement.controlU; after.controlV = replacement.controlV;
		after.textureName = replacement.textureName;
		after.fixedSubdivisions = replacement.fixedSubdivisions;
		after.subdivisionsX = replacement.subdivisionsX; after.subdivisionsY = replacement.subdivisionsY;
		// Header extensions, comments and ownership belong to the loaded document.
		if (before->startLine > 0) {
			after.sourceLines = document->textLines.mid(before->startLine - 1, before->endLine - before->startLine + 1);
			after.sourceFirstLine = before->startLine;
		}
		LevelMapPatch baseline = *before;
		baseline.sourceLines = after.sourceLines;
		if (levelPatchDefinition(after) == levelPatchDefinition(baseline)) { continue; }
		refreshLevelPatchBounds(&after);
		after.definitionDirty = after.geometryDirty = true;
		command.objectId = patchId;
		command.patchSnapshots << *before;
		command.patchResults << after;
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Edit patch %1").arg(patchId);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Restore patch %1").arg(patchId);
	}
	if (command.patchResults.isEmpty()) { return true; }
	if (command.patchResults.size() > 1) {
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Edit %1 patches").arg(command.patchResults.size());
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 patches").arg(command.patchResults.size());
	}
	if (!applyLevelMapCommand(document, command, true)) { return fail(QT_TRANSLATE_NOOP("VibeStudioLevelMap", "The patch could not be updated.")); }
	pushUndo(document, command);
	return true;
}

bool duplicateLevelMapObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects,
	double dx, double dy, double dz, QString* error)
{
	return duplicateLevelMapObjects(document, objects, dx, dy, dz, {false, false}, error);
}

bool duplicateLevelMapObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects,
	double dx, double dy, double dz, const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return duplicateLevelMapObjects(candidate, objects, dx, dy, dz, textures, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (objects.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing is selected."));
	}
	if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Placement needs a finite offset on each axis."));
	}
	if (document->format == LevelMapFormat::DoomWad) {
		return duplicateDoomThings(document, objects, dx, dy, dz, error);
	}
	if (!isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Only entities, brushes, and patches in Quake-family .map files can be duplicated."));
	}
	LevelMapObjectExistence existing(document);
	const auto entitiesById = objectIndexes(document->entities);
	const LevelMapBrushLineConflicts sharedLines(*document);
	QSet<int> entityIds;
	QSet<int> brushIds;
	QSet<int> patchIds;
	for (const LevelMapSelectionRef& ref : objects) {
		detail::placementCancellationCheckpoint();
		if (!existing.contains(ref)) { return fail(selectionNotFoundText(ref.kind)); }
		switch (ref.kind) {
		case LevelMapSelectionKind::Entity: {
			const auto& entity = document->entities.at(entitiesById.value(ref.objectId));
			if (isWorldspawnEntity(entity)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "The worldspawn entity cannot be copied. Select its brushes instead."));
			}
			entityIds.insert(ref.objectId);
			break;
		}
		case LevelMapSelectionKind::QuakeBrush:
			brushIds.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::QuakePatch:
			patchIds.insert(ref.objectId);
			break;
		default:
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Only entities, brushes, and patches can be duplicated."));
		}
	}
	LevelMapUndoCommand placement;
	if (!prepareLevelMapPlacement(*document, objects, {dx, dy, dz, true}, textures, &placement, error)) { return false; }
	const auto placedEntities = objectIndexes(placement.entityResults);
	const auto placedBrushes = objectIndexes(placement.brushResults);
	const auto placedPatches = objectIndexes(placement.patchResults);
	const auto placed = [](const auto& records, const auto& indexes, const auto& original) {
		const int index = indexes.value(original.id, -1);
		return index < 0 ? original : records[index];
	};

	int nextEntity = 0;
	for (const LevelMapEntity& entity : document->entities) {
		detail::placementCancellationCheckpoint();
		nextEntity = std::max(nextEntity, entity.id + 1);
	}
	int nextBrush = 0;
	for (const LevelMapBrush& brush : document->brushes) {
		detail::placementCancellationCheckpoint();
		nextBrush = std::max(nextBrush, brush.id + 1);
	}
	int nextPatch = 0;
	for (const LevelMapPatch& patch : document->patches) {
		detail::placementCancellationCheckpoint();
		nextPatch = std::max(nextPatch, patch.id + 1);
	}
	// A copy that joins an entity from the file goes ahead of its closing
	// brace, so that brace needs a line of its own.
	const auto acceptsCopies = [document](int entityId) {
		return entityAcceptsCopies(document, entityId);
	};
	const auto sourceText = [document](int startLine, int endLine) {
		return startLine > 0 && endLine >= startLine && endLine <= document->textLines.size()
			? document->textLines.mid(startLine - 1, endLine - startLine + 1)
			: QStringList();
	};

	detail::placementCheckpoint(LevelPlacementPhase::Inserting);
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("add-objects");
	command.objectKind = QStringLiteral("selection");
	QHash<int, int> entityCopies;
	QVector<LevelMapSelectionRef> copies;
	for (const LevelMapEntity& entity : document->entities) {
		detail::placementCancellationCheckpoint();
		if (!entityIds.contains(entity.id)) {
			continue;
		}
		LevelMapEntity copy = placed(placement.entityResults, placedEntities, entity);
		copy.id = nextEntity++;
		copy.startLine = 0;
		copy.endLine = 0;
		copy.selected = false;
		copy.removedPropertyLines.clear();
		for (LevelMapProperty& property : copy.properties) {
			property.line = 0;
		}
		entityCopies.insert(entity.id, copy.id);
		command.sceneObjectOrigins.insert(entityObjectId(copy.id), entityObjectId(entity.id));
		command.entityIndexes.push_back(static_cast<int>(document->entities.size() + command.entitySnapshots.size()));
		command.entitySnapshots.push_back(copy);
		copies.push_back({LevelMapSelectionKind::Entity, copy.id});
	}
	for (const LevelMapBrush& brush : document->brushes) {
		detail::placementCancellationCheckpoint();
		const bool withEntity = entityCopies.contains(brush.entityId);
		if (!withEntity && !brushIds.contains(brush.id)) {
			continue;
		}
		if (!withEntity && !acceptsCopies(brush.entityId)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 closes on a line shared with other map text, so a copy of brush %2 cannot be added to it.")
				.arg(brush.entityId).arg(brush.id));
		}
		if (!objectOwnsItsLines(*document, brush.startLine, brush.endLine)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 shares a source line with other map text, so it cannot be copied without that text.").arg(brush.id));
		}
		if (sharedLines.contains(brush.id)) {
			return fail(sharedFaceLinesText(brush.id));
		}
		LevelMapBrush copy = placed(placement.brushResults, placedBrushes, brush);
		if (brush.startLine > 0) {
			copy.sourceLines = sourceText(brush.startLine, brush.endLine);
			copy.sourceFirstLine = brush.startLine;
		}
		if (copy.sourceLines.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 has no source text to copy.").arg(brush.id));
		}
		copy.id = nextBrush++;
		copy.entityId = withEntity ? entityCopies.value(brush.entityId) : brush.entityId;
		copy.startLine = 0;
		copy.endLine = 0;
		copy.selected = false;
		command.sceneObjectOrigins.insert(brushObjectId(copy.id), brushObjectId(brush.id));
		command.brushIndexes.push_back(static_cast<int>(document->brushes.size() + command.brushSnapshots.size()));
		command.brushSnapshots.push_back(copy);
		if (!withEntity) {
			copies.push_back({LevelMapSelectionKind::QuakeBrush, copy.id});
		}
	}
	for (const LevelMapPatch& patch : document->patches) {
		detail::placementCancellationCheckpoint();
		const bool withEntity = entityCopies.contains(patch.entityId);
		if (!withEntity && !patchIds.contains(patch.id)) {
			continue;
		}
		if (!withEntity && !acceptsCopies(patch.entityId)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 closes on a line shared with other map text, so a copy of patch %2 cannot be added to it.")
				.arg(patch.entityId).arg(patch.id));
		}
		if (!patch.controlGridNormalized || patch.controlRowLines.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Patch %1's control grid could not be read, so it cannot be copied.").arg(patch.id));
		}
		if (!objectOwnsItsLines(*document, patch.startLine, patch.endLine)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 shares a source line with other map text, so it cannot be copied without that text.").arg(patch.id));
		}
		LevelMapPatch copy = placed(placement.patchResults, placedPatches, patch);
		if (patch.startLine > 0) {
			copy.sourceLines = sourceText(patch.startLine, patch.endLine);
			copy.sourceFirstLine = patch.startLine;
		}
		if (copy.sourceLines.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 has no source text to copy.").arg(patch.id));
		}
		copy.id = nextPatch++;
		copy.entityId = withEntity ? entityCopies.value(patch.entityId) : patch.entityId;
		copy.startLine = 0;
		copy.endLine = 0;
		copy.selected = false;
		command.sceneObjectOrigins.insert(patchObjectId(copy.id), patchObjectId(patch.id));
		command.patchIndexes.push_back(static_cast<int>(document->patches.size() + command.patchSnapshots.size()));
		command.patchSnapshots.push_back(copy);
		if (!withEntity) {
			copies.push_back({LevelMapSelectionKind::QuakePatch, copy.id});
		}
	}
	if (copies.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing in the selection can be copied."));
	}
	const int count = static_cast<int>(copies.size());
	command.description = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Duplicate as %1").arg(levelMapSelectionRefId(copies.first()))
					 : QCoreApplication::translate("VibeStudioLevelMap", "Duplicate %1 objects").arg(count);
	command.undoDescription = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Remove copy %1").arg(levelMapSelectionRefId(copies.first()))
					     : QCoreApplication::translate("VibeStudioLevelMap", "Remove %1 copies").arg(count);
	command.selectionSnapshot = document->selection;
	command.selectionResult = copies;
	detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The copies could not be added."));
	}
	pushUndo(document, command);
	setLevelMapSelection(document, copies);
	return true;
}

bool duplicateLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz, QString* error)
{
	return duplicateLevelMapSelection(document, dx, dy, dz, {false, false}, error);
}

bool duplicateLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz,
	const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return duplicateLevelMapSelection(candidate, dx, dy, dz, textures, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (!document) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	const QVector<LevelMapSelectionRef> selection = document->selection;
	return duplicateLevelMapObjects(document, selection, dx, dy, dz, textures, error);
}

bool levelMapObjectExists(const LevelMapDocument& document, const LevelMapSelectionRef& ref)
{
	switch (ref.kind) {
	case LevelMapSelectionKind::None:
		return false;
	case LevelMapSelectionKind::Entity:
		return indexOfObjectId(document.entities, ref.objectId) >= 0;
	case LevelMapSelectionKind::DoomVertex:
		return indexOfObjectId(document.doomVertices, ref.objectId) >= 0;
	case LevelMapSelectionKind::DoomLinedef:
		return indexOfObjectId(document.doomLinedefs, ref.objectId) >= 0;
	case LevelMapSelectionKind::DoomThing:
		return indexOfObjectId(document.doomThings, ref.objectId) >= 0;
	case LevelMapSelectionKind::DoomSector:
		return indexOfObjectId(document.doomSectors, ref.objectId) >= 0;
	case LevelMapSelectionKind::QuakeBrush:
		return indexOfObjectId(document.brushes, ref.objectId) >= 0;
	case LevelMapSelectionKind::QuakePatch:
		return indexOfObjectId(document.patches, ref.objectId) >= 0;
	}
	return false;
}

QVector<LevelMapSelectionRef> levelMapSelectAllObjects(const LevelMapDocument& document, bool invert,
	const std::function<bool(const LevelMapSelectionRef&)>& shown)
{
	QVector<LevelMapSelectionRef> all;
	const auto offer = [&all, &shown](LevelMapSelectionKind kind, int id) {
		const LevelMapSelectionRef ref {kind, id};
		if (!shown || shown(ref)) {
			all.push_back(ref);
		}
	};
	if (document.format == LevelMapFormat::DoomWad) {
		// Things are what a Doom map moves and copies; its geometry is picked
		// piece by piece.
		for (const LevelMapDoomThing& thing : document.doomThings) {
			offer(LevelMapSelectionKind::DoomThing, thing.id);
		}
	} else {
		QSet<int> holders;
		for (const LevelMapBrush& brush : document.brushes) {
			holders.insert(brush.entityId);
		}
		for (const LevelMapPatch& patch : document.patches) {
			holders.insert(patch.entityId);
		}
		// Point entities, and brush entities left with nothing in them.
		for (const LevelMapEntity& entity : document.entities) {
			if (!isWorldspawnEntity(entity) && !holders.contains(entity.id)) {
				offer(LevelMapSelectionKind::Entity, entity.id);
			}
		}
		for (const LevelMapBrush& brush : document.brushes) {
			offer(LevelMapSelectionKind::QuakeBrush, brush.id);
		}
		for (const LevelMapPatch& patch : document.patches) {
			offer(LevelMapSelectionKind::QuakePatch, patch.id);
		}
	}
	if (!invert) {
		return all;
	}
	const auto key = [](LevelMapSelectionKind kind, int id) {
		return (static_cast<quint64>(static_cast<quint32>(kind)) << 32) | static_cast<quint32>(id);
	};
	QSet<quint64> current;
	QSet<int> currentEntities;
	for (const LevelMapSelectionRef& ref : document.selection) {
		current.insert(key(ref.kind, ref.objectId));
		if (ref.kind == LevelMapSelectionKind::Entity) {
			currentEntities.insert(ref.objectId);
		}
	}
	QHash<quint64, int> owners;
	for (const LevelMapBrush& brush : document.brushes) {
		owners.insert(key(LevelMapSelectionKind::QuakeBrush, brush.id), brush.entityId);
	}
	for (const LevelMapPatch& patch : document.patches) {
		owners.insert(key(LevelMapSelectionKind::QuakePatch, patch.id), patch.entityId);
	}
	all.removeIf([&](const LevelMapSelectionRef& ref) {
		const quint64 id = key(ref.kind, ref.objectId);
		return current.contains(id) || (owners.contains(id) && currentEntities.contains(owners.value(id)));
	});
	return all;
}

bool levelMapSelectionIsDuplicable(const LevelMapDocument& document)
{
	if (document.format == LevelMapFormat::DoomWad) {
		return document.doomFormat != LevelMapDoomFormat::Udmf && !document.selection.isEmpty()
			&& std::all_of(document.selection.cbegin(), document.selection.cend(), [](const LevelMapSelectionRef& ref) {
				   return ref.kind == LevelMapSelectionKind::DoomThing;
			   });
	}
	return levelMapSelectionIsDeletable(document);
}

bool levelMapSelectionIsDeletable(const LevelMapDocument& document)
{
	if (document.format == LevelMapFormat::DoomWad && document.doomFormat != LevelMapDoomFormat::Udmf) {
		return !document.selection.isEmpty()
			&& std::all_of(document.selection.cbegin(), document.selection.cend(), [](const LevelMapSelectionRef& ref) {
				   return ref.kind == LevelMapSelectionKind::DoomThing || ref.kind == LevelMapSelectionKind::DoomVertex
					   || ref.kind == LevelMapSelectionKind::DoomLinedef || ref.kind == LevelMapSelectionKind::DoomSector;
			   });
	}
	if (!isTextMapFormat(document.format) || document.selection.isEmpty()) {
		return false;
	}
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch) {
			continue;
		}
		if (ref.kind != LevelMapSelectionKind::Entity) {
			return false;
		}
		for (const LevelMapEntity& entity : document.entities) {
			if (entity.id == ref.objectId && isWorldspawnEntity(entity)) {
				return false;
			}
		}
	}
	return true;
}

namespace {

// Anchors for every entity in one pass over the brushes and patches, so a map
// with thousands of entities is not rescanned once per entity.
QHash<int, LevelMapVec3> entityAnchors(const LevelMapDocument& document)
{
	QHash<int, LevelMapVec3> mins;
	QHash<int, LevelMapVec3> maxs;
	const auto include = [&mins, &maxs](int entityId, const LevelMapVec3& low, const LevelMapVec3& high) {
		includePoint(&mins[entityId], &maxs[entityId], low);
		includePoint(&mins[entityId], &maxs[entityId], high);
	};
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.boundsSolved) {
			include(brush.entityId, brush.mins, brush.maxs);
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (patch.mins.valid && patch.maxs.valid) {
			include(patch.entityId, patch.mins, patch.maxs);
		}
	}
	QHash<int, LevelMapVec3> anchors;
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.origin.valid) {
			anchors.insert(entity.id, entity.origin);
			continue;
		}
		const LevelMapVec3 low = mins.value(entity.id);
		const LevelMapVec3 high = maxs.value(entity.id);
		if (low.valid && high.valid) {
			anchors.insert(entity.id, {(low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (low.z + high.z) * 0.5, true});
		}
	}
	return anchors;
}

} // namespace

bool levelMapEntityAnchor(const LevelMapDocument& document, int entityId, LevelMapVec3* anchor)
{
	const QHash<int, LevelMapVec3> anchors = entityAnchors(document);
	const auto found = anchors.constFind(entityId);
	if (found == anchors.constEnd()) {
		return false;
	}
	if (anchor) {
		*anchor = found.value();
	}
	return true;
}

const QStringList& levelMapTargetPropertyKeys()
{
	static const QStringList keys {QStringLiteral("target"), QStringLiteral("killtarget"), QStringLiteral("pathtarget"),
		QStringLiteral("combattarget"), QStringLiteral("deathtarget")};
	return keys;
}

QVector<LevelMapTargetLink> levelMapTargetLinks(const LevelMapDocument& document)
{
	QVector<LevelMapTargetLink> links;
	if (!isTextMapFormat(document.format)) {
		return links;
	}
	// The keys Quake, Quake II, and Quake III use to name another entity.
	const auto& targetKeys = levelMapTargetPropertyKeys();
	const QHash<int, LevelMapVec3> anchors = entityAnchors(document);
	QHash<QString, QVector<int>> named;
	for (const LevelMapEntity& entity : document.entities) {
		const QString name = propertyValue(entity, QStringLiteral("targetname"));
		if (!name.isEmpty() && anchors.contains(entity.id)) {
			named[name].push_back(entity.id);
		}
	}
	for (const LevelMapEntity& entity : document.entities) {
		const auto source = anchors.constFind(entity.id);
		if (source == anchors.constEnd()) {
			continue;
		}
		for (const LevelMapProperty& property : entity.properties) {
			const QString key = property.key.toLower();
			if (property.value.isEmpty() || !targetKeys.contains(key)) {
				continue;
			}
			QVector<int> targets = named.value(property.value);
			std::sort(targets.begin(), targets.end());
			for (const int target : targets) {
				if (target != entity.id) {
					links.push_back({entity.id, target, key, property.value, source.value(), anchors.value(target)});
				}
			}
		}
	}
	return links;
}

namespace {

// Shared traversal for read-only summaries and undoable texture edits. Summaries
// need only names/counts, so do not allocate an edit record for every surface.
template <typename Visitor>
void visitTextureUsesInScope(const LevelMapDocument& document, bool selectionOnly, Visitor&& visit)
{
	QSet<int> entityIds;
	QSet<int> brushIds;
	QSet<int> patchIds;
	QSet<int> linedefIds;
	QSet<int> sidedefIds;
	QSet<int> sectorIds;
	if (selectionOnly) {
		for (const LevelMapSelectionRef& ref : document.selection) {
			switch (ref.kind) {
			case LevelMapSelectionKind::Entity:
				entityIds.insert(ref.objectId);
				break;
			case LevelMapSelectionKind::QuakeBrush:
				brushIds.insert(ref.objectId);
				break;
			case LevelMapSelectionKind::QuakePatch:
				patchIds.insert(ref.objectId);
				break;
			case LevelMapSelectionKind::DoomLinedef:
				linedefIds.insert(ref.objectId);
				break;
			case LevelMapSelectionKind::DoomSector:
				sectorIds.insert(ref.objectId);
				break;
			default:
				break;
			}
		}
		if (!linedefIds.isEmpty()) {
			for (const auto& line : document.doomLinedefs) {
				if (!linedefIds.contains(line.id)) { continue; }
				sidedefIds.insert(line.frontSidedef);
				sidedefIds.insert(line.backSidedef);
			}
		}
	}
	for (const LevelMapBrush& brush : document.brushes) {
		if (selectionOnly && !brushIds.contains(brush.id) && !entityIds.contains(brush.entityId)) {
			continue;
		}
		for (int face = 0; face < brush.faces.size(); ++face) {
			visit(QStringLiteral("face"), brush.id, face, brush.faces.at(face).textureName);
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (!selectionOnly || patchIds.contains(patch.id) || entityIds.contains(patch.entityId)) {
			visit(QStringLiteral("patch"), patch.id, 0, patch.textureName);
		}
	}
	for (const LevelMapDoomSidedef& sidedef : document.doomSidedefs) {
		if (!selectionOnly || sidedefIds.contains(sidedef.id)) {
			visit(QStringLiteral("sidedef"), sidedef.id, 0, sidedef.upperTexture);
			visit(QStringLiteral("sidedef"), sidedef.id, 1, sidedef.lowerTexture);
			visit(QStringLiteral("sidedef"), sidedef.id, 2, sidedef.middleTexture);
		}
	}
	for (const LevelMapDoomSector& sector : document.doomSectors) {
		if (!selectionOnly || sectorIds.contains(sector.id)) {
			visit(QStringLiteral("sector"), sector.id, 0, sector.floorTexture);
			visit(QStringLiteral("sector"), sector.id, 1, sector.ceilingTexture);
		}
	}
}

QVector<LevelMapTextureChange> textureUsesInScope(const LevelMapDocument& document, bool selectionOnly)
{
	QVector<LevelMapTextureChange> uses;
	visitTextureUsesInScope(document, selectionOnly, [&](const QString& kind, int object, int part, const QString& name) {
		uses.push_back({kind, object, part, name, QString()});
	});
	return uses;
}

} // namespace

QVector<LevelMapSelectionRef> levelMapObjectsUsingTexture(const LevelMapDocument& document, const QString& texture)
{
	QVector<LevelMapSelectionRef> objects;
	const QString name = texture.trimmed();
	if (name.isEmpty()) {
		return objects;
	}
	const auto same = [&name](const QString& value) {
		return value.trimmed().compare(name, Qt::CaseInsensitive) == 0;
	};
	for (const LevelMapBrush& brush : document.brushes) {
		if (std::any_of(brush.faces.cbegin(), brush.faces.cend(), [&same](const LevelMapBrushFace& face) { return same(face.textureName); })) {
			objects.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (same(patch.textureName)) {
			objects.push_back({LevelMapSelectionKind::QuakePatch, patch.id});
		}
	}
	if (document.format == LevelMapFormat::DoomWad) {
		QSet<int> sidedefs;
		for (const LevelMapDoomSidedef& sidedef : document.doomSidedefs) {
			if (same(sidedef.upperTexture) || same(sidedef.lowerTexture) || same(sidedef.middleTexture)) {
				sidedefs.insert(sidedef.id);
			}
		}
		for (const LevelMapDoomLinedef& linedef : document.doomLinedefs) {
			if (sidedefs.contains(linedef.frontSidedef) || sidedefs.contains(linedef.backSidedef)) {
				objects.push_back({LevelMapSelectionKind::DoomLinedef, linedef.id});
			}
		}
		for (const LevelMapDoomSector& sector : document.doomSectors) {
			if (same(sector.floorTexture) || same(sector.ceilingTexture)) {
				objects.push_back({LevelMapSelectionKind::DoomSector, sector.id});
			}
		}
	}
	return objects;
}

QHash<QString, LevelMapObjectProperties> levelMapQueryProperties(const LevelMapDocument& document, const std::function<bool()>& isCancelled)
{
	QHash<QString, LevelMapObjectProperties> objects;
	const auto number = [](double value) {
		return QString::number(value, 'g', 12);
	};
	const bool doom = document.format == LevelMapFormat::DoomWad;
	if (!doom) {
		for (const LevelMapEntity& entity : document.entities) {
			if (isCancelled && isCancelled()) { return {}; }
			LevelMapObjectProperties& properties = objects[QStringLiteral("entity:%1").arg(entity.id)];
			properties.insert(QStringLiteral("kind"), QStringLiteral("entity"));
			properties.insert(QStringLiteral("selector"), QStringLiteral("entity:%1").arg(entity.id));
			properties.insert(QStringLiteral("class"), entity.className);
			for (const LevelMapProperty& property : entity.properties) {
				if (isCancelled && isCancelled()) { return {}; }
				properties.insert(property.key.trimmed().toLower(), property.value);
			}
		}
	}
	for (const LevelMapBrush& brush : document.brushes) {
		if (isCancelled && isCancelled()) { return {}; }
		LevelMapObjectProperties& properties = objects[QStringLiteral("brush:%1").arg(brush.id)];
		properties.insert(QStringLiteral("kind"), QStringLiteral("brush"));
		properties.insert(QStringLiteral("selector"), QStringLiteral("brush:%1").arg(brush.id));
		properties.insert(QStringLiteral("entity"), QString::number(brush.entityId));
		QSet<QString> textures;
		for (const LevelMapBrushFace& face : brush.faces) {
			if (isCancelled && isCancelled()) { return {}; }
			if (!textures.contains(face.textureName.toLower())) {
				textures.insert(face.textureName.toLower());
				properties.insert(QStringLiteral("texture"), face.textureName);
			}
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (isCancelled && isCancelled()) { return {}; }
		LevelMapObjectProperties& properties = objects[QStringLiteral("patch:%1").arg(patch.id)];
		properties.insert(QStringLiteral("kind"), QStringLiteral("patch"));
		properties.insert(QStringLiteral("selector"), QStringLiteral("patch:%1").arg(patch.id));
		properties.insert(QStringLiteral("entity"), QString::number(patch.entityId));
		properties.insert(QStringLiteral("texture"), patch.textureName);
	}
	if (!doom) {
		return objects;
	}
	QHash<int, QString> typeNames;
	for (const LevelMapDoomThingType& type : levelMapDoomThingTypes(document.doomFormat)) {
		if (isCancelled && isCancelled()) { return {}; }
		typeNames.insert(type.type, type.name);
	}
	const bool hexen = document.doomFormat == LevelMapDoomFormat::Hexen;
	for (const LevelMapDoomThing& thing : document.doomThings) {
		if (isCancelled && isCancelled()) { return {}; }
		LevelMapObjectProperties& properties = objects[QStringLiteral("thing:%1").arg(thing.id)];
		properties.insert(QStringLiteral("kind"), QStringLiteral("thing"));
		properties.insert(QStringLiteral("selector"), QStringLiteral("thing:%1").arg(thing.id));
		properties.insert(QStringLiteral("type"), QString::number(thing.type));
		if (typeNames.contains(thing.type)) {
			properties.insert(QStringLiteral("name"), typeNames.value(thing.type));
		}
		properties.insert(QStringLiteral("angle"), QString::number(thing.angle));
		properties.insert(QStringLiteral("flags"), QString::number(thing.flags));
		properties.insert(QStringLiteral("x"), number(thing.x));
		properties.insert(QStringLiteral("y"), number(thing.y));
		if (hexen) {
			properties.insert(QStringLiteral("tid"), QString::number(thing.tid));
			properties.insert(QStringLiteral("special"), QString::number(thing.special));
			properties.insert(QStringLiteral("action"), QString::number(thing.special));
		}
	}
	QHash<int, const LevelMapDoomSidedef*> sidedefs;
	for (const LevelMapDoomSidedef& sidedef : document.doomSidedefs) {
		if (isCancelled && isCancelled()) { return {}; }
		sidedefs.insert(sidedef.id, &sidedef);
	}
	for (const LevelMapDoomLinedef& linedef : document.doomLinedefs) {
		if (isCancelled && isCancelled()) { return {}; }
		LevelMapObjectProperties& properties = objects[QStringLiteral("linedef:%1").arg(linedef.id)];
		properties.insert(QStringLiteral("kind"), QStringLiteral("linedef"));
		properties.insert(QStringLiteral("selector"), QStringLiteral("linedef:%1").arg(linedef.id));
		properties.insert(QStringLiteral("special"), QString::number(linedef.special));
		properties.insert(QStringLiteral("action"), QString::number(linedef.special));
		properties.insert(QStringLiteral("tag"), QString::number(linedef.tag));
		properties.insert(QStringLiteral("flags"), QString::number(linedef.flags));
		properties.insert(QStringLiteral("front"), QString::number(linedef.frontSidedef));
		properties.insert(QStringLiteral("back"), QString::number(linedef.backSidedef));
		QSet<QString> textures;
		for (const int side : {linedef.frontSidedef, linedef.backSidedef}) {
			if (isCancelled && isCancelled()) { return {}; }
			if (const LevelMapDoomSidedef* sidedef = sidedefs.value(side, nullptr)) {
				for (const QString& texture : {sidedef->upperTexture, sidedef->middleTexture, sidedef->lowerTexture}) {
					if (isCancelled && isCancelled()) { return {}; }
					if (!texture.isEmpty() && texture != QStringLiteral("-") && !textures.contains(texture.toLower())) {
						textures.insert(texture.toLower());
						properties.insert(QStringLiteral("texture"), texture);
					}
				}
			}
		}
	}
	for (const LevelMapDoomSector& sector : document.doomSectors) {
		if (isCancelled && isCancelled()) { return {}; }
		LevelMapObjectProperties& properties = objects[QStringLiteral("sector:%1").arg(sector.id)];
		properties.insert(QStringLiteral("kind"), QStringLiteral("sector"));
		properties.insert(QStringLiteral("selector"), QStringLiteral("sector:%1").arg(sector.id));
		properties.insert(QStringLiteral("floor"), QString::number(sector.floorHeight));
		properties.insert(QStringLiteral("ceiling"), QString::number(sector.ceilingHeight));
		properties.insert(QStringLiteral("floortex"), sector.floorTexture);
		properties.insert(QStringLiteral("ceiltex"), sector.ceilingTexture);
		properties.insert(QStringLiteral("texture"), sector.floorTexture);
		if (sector.ceilingTexture.compare(sector.floorTexture, Qt::CaseInsensitive) != 0) {
			properties.insert(QStringLiteral("texture"), sector.ceilingTexture);
		}
		properties.insert(QStringLiteral("light"), QString::number(sector.lightLevel));
		properties.insert(QStringLiteral("special"), QString::number(sector.special));
		properties.insert(QStringLiteral("effect"), QString::number(sector.special));
		properties.insert(QStringLiteral("tag"), QString::number(sector.tag));
	}
	for (const LevelMapDoomVertex& vertex : document.doomVertices) {
		if (isCancelled && isCancelled()) { return {}; }
		LevelMapObjectProperties& properties = objects[QStringLiteral("vertex:%1").arg(vertex.id)];
		properties.insert(QStringLiteral("kind"), QStringLiteral("vertex"));
		properties.insert(QStringLiteral("selector"), QStringLiteral("vertex:%1").arg(vertex.id));
		properties.insert(QStringLiteral("x"), number(vertex.x));
		properties.insert(QStringLiteral("y"), number(vertex.y));
	}
	return objects;
}

LevelMapQuery parseLevelMapQuery(const QString& text)
{
	LevelMapQuery query = parseStudioQuery(text);
	// "entity:3", as map find prints an object, names that object rather than
	// asking for an entity key holding 3.
	static const QStringList kinds = {QStringLiteral("entity"), QStringLiteral("brush"), QStringLiteral("patch"), QStringLiteral("thing"),
		QStringLiteral("linedef"), QStringLiteral("sector"), QStringLiteral("vertex")};
	for (LevelMapQueryTerm& term : query.terms) {
		bool number = false;
		term.value.toInt(&number);
		if (term.op == QStringLiteral(":") && number && kinds.contains(term.key)) {
			term.value = QStringLiteral("%1:%2").arg(term.key, term.value);
			term.key = QStringLiteral("selector");
			term.op = QStringLiteral("=");
		}
	}
	return query;
}

bool levelMapQueryMatches(const LevelMapQuery& query, const LevelMapObjectProperties& properties, const QString& description)
{
	return studioQueryMatches(query, properties, description);
}

QStringList levelMapObjectsMatchingQuery(const LevelMapDocument& document, const QString& query)
{
	const LevelMapQuery parsed = parseLevelMapQuery(query);
	const QHash<QString, LevelMapObjectProperties> objects = levelMapQueryProperties(document);
	QStringList matching;
	// Map order: entities, brushes, patches, then Doom things, linedefs,
	// sectors, and vertices, each by id.
	QStringList selectors = objects.keys();
	const auto rank = [](const QString& selector) {
		static const QStringList kinds = {QStringLiteral("entity"), QStringLiteral("brush"), QStringLiteral("patch"), QStringLiteral("thing"),
			QStringLiteral("linedef"), QStringLiteral("sector"), QStringLiteral("vertex")};
		const qsizetype colon = selector.indexOf(QLatin1Char(':'));
		return std::make_pair(kinds.indexOf(selector.left(colon)), selector.mid(colon + 1).toInt());
	};
	std::sort(selectors.begin(), selectors.end(), [&rank](const QString& left, const QString& right) {
		return rank(left) < rank(right);
	});
	for (const QString& selector : std::as_const(selectors)) {
		const LevelMapObjectProperties& properties = objects[selector];
		const QString description = selector + QLatin1Char(' ') + QStringList(properties.values()).join(QLatin1Char(' '));
		if (levelMapQueryMatches(parsed, properties, description)) {
			matching << selector;
		}
	}
	return matching;
}

QVector<LevelMapTextureUse> levelMapTextureUsage(const LevelMapDocument& document, bool selectionOnly)
{
	// Retain encounter order so the first spelling is still the displayed name.
	QHash<QString, qsizetype> indexes;
	QVector<LevelMapTextureUse> spellings;
	visitTextureUsesInScope(document, selectionOnly, [&](const QString&, int, int, const QString& name) {
		const auto found = indexes.constFind(name);
		if (found != indexes.cend()) { ++spellings[found.value()].count; return; }
		indexes.insert(name, spellings.size());
		spellings.push_back({name, 1});
	});
	QMap<QString, LevelMapTextureUse> byName;
	for (const auto& use : std::as_const(spellings)) {
		const QString name = use.name.trimmed();
		if (name.isEmpty() || name == QStringLiteral("-")) {
			continue;
		}
		LevelMapTextureUse& entry = byName[name.toLower()];
		entry.name = entry.name.isEmpty() ? name : entry.name;
		entry.count += use.count;
	}
	return byName.values().toVector();
}

namespace {

// Why `name` cannot be written as a texture, or empty when it can: anything
// the map tokenizer would split on cannot be read back.
QString textureNameProblem(const LevelMapDocument& document, const QString& name)
{
	for (const QChar ch : name) {
		if (ch.isSpace() || QStringLiteral("\"{}()[]").contains(ch)) {
			return QCoreApplication::translate("VibeStudioLevelMap", "A texture name cannot contain spaces, quotes, braces, brackets, or parentheses.");
		}
	}
	if (name.contains(QStringLiteral("//"))) {
		return QCoreApplication::translate("VibeStudioLevelMap", "A texture name cannot contain //, which starts a comment.");
	}
	if (document.format == LevelMapFormat::DoomWad && name.size() > 8) {
		return QCoreApplication::translate("VibeStudioLevelMap", "Doom texture names are at most eight characters.");
	}
	return {};
}

// Why the face changes cannot be written, or empty when they can: a face's
// name is rewritten on its own line, so that line has to hold just that face,
// in a form the rewrite can read.
QString textureChangesProblem(const LevelMapDocument& document, const QVector<LevelMapTextureChange>& changes)
{
	for (const LevelMapTextureChange& change : changes) {
		if (change.kind == QStringLiteral("patch")) {
			const LevelMapPatch* patch = nullptr;
			for (const LevelMapPatch& candidate : document.patches) {
				if (candidate.id == change.objectId) {
					patch = &candidate;
					break;
				}
			}
			if (!patch) {
				return QCoreApplication::translate("VibeStudioLevelMap", "The textures to change are no longer in the map.");
			}
			const QString line = patch->startLine > 0
				? document.textLines.value(patch->textureLine - 1)
				: patch->sourceLines.value(patch->textureLine - patch->sourceFirstLine);
			qsizetype start = 0;
			qsizetype end = 0;
			if (!patchShaderSpan(line, patch->textureColumn, &start, &end)) {
				return QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 names its shader in a form that cannot be replaced in place.").arg(patch->id);
			}
			for (const LevelMapPatch& other : document.patches) {
				if (patch->startLine > 0 && other.id != patch->id && other.startLine > 0 && other.textureLine == patch->textureLine) {
					return QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 names its shader on a line it shares with patch %2, so the shader cannot be replaced in place.")
						.arg(patch->id)
						.arg(other.id);
				}
			}
			continue;
		}
		if (change.kind != QStringLiteral("face")) {
			continue;
		}
		const LevelMapBrush* brush = nullptr;
		for (const LevelMapBrush& candidate : document.brushes) {
			if (candidate.id == change.objectId) {
				brush = &candidate;
				break;
			}
		}
		if (!brush || change.part < 0 || change.part >= brush->faces.size()) {
			return QCoreApplication::translate("VibeStudioLevelMap", "The textures to change are no longer in the map.");
		}
		const LevelMapBrushFace& face = brush->faces.at(change.part);
		for (int other = 0; other < brush->faces.size(); ++other) {
			if (other != change.part && brush->faces.at(other).line == face.line) {
				return QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 writes more than one face on a line, so its textures cannot be replaced in place.").arg(brush->id);
			}
		}
		// Another brush's face on the same line would be the one rewritten:
		// save-back finds a face's name and numbers as the first on its line.
		if (brush->startLine > 0) {
			for (const LevelMapBrush& other : document.brushes) {
				if (other.id == brush->id || other.startLine <= 0 || other.endLine < face.line || other.startLine > face.line) {
					continue;
				}
				for (const LevelMapBrushFace& otherFace : other.faces) {
					if (otherFace.line == face.line) {
						return QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 writes a face on a line it shares with brush %2, so its textures cannot be replaced in place.")
							.arg(brush->id)
							.arg(other.id);
					}
				}
			}
		}
		const QString line = brush->startLine > 0
			? document.textLines.value(face.line - 1)
			: brush->sourceLines.value(face.line - brush->sourceFirstLine);
		qsizetype start = 0;
		qsizetype end = 0;
		if (!faceTextureSpan(line, &start, &end)) {
			return QCoreApplication::translate("VibeStudioLevelMap", "A face of brush %1 is written in a form whose texture cannot be replaced in place.").arg(brush->id);
		}
	}
	return {};
}

} // namespace

namespace {

// Sets one field of a sidedef from text, as the inspector and `map edit` give
// it. False, with the reason, when the key or the value will not do.
bool setDoomSidedefField(const LevelMapDocument& document, LevelMapDoomSidedef* sidedef, const QString& key, const QString& value, QString* error)
{
	const QString field = key.trimmed().toLower();
	const QString text = value.trimmed();
	bool ok = false;
	const int number = text.toInt(&ok);
	if (field == QStringLiteral("sector")) {
		if (!ok || indexOfObjectId(document.doomSectors, number) < 0) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "A side's sector must be one the map has.");
			return false;
		}
		sidedef->sector = number;
		return true;
	}
	if (field == QStringLiteral("offsetx") || field == QStringLiteral("offsety")) {
		if (!ok || number < kDoomCoordinateMin || number > kDoomCoordinateMax) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "A texture offset is a whole number from -32768 to 32767.");
			return false;
		}
		(field == QStringLiteral("offsetx") ? sidedef->offsetX : sidedef->offsetY) = number;
		return true;
	}
	QString* texture = nullptr;
	if (field == QStringLiteral("upper") || field == QStringLiteral("uppertexture")) {
		texture = &sidedef->upperTexture;
	} else if (field == QStringLiteral("middle") || field == QStringLiteral("middletexture")) {
		texture = &sidedef->middleTexture;
	} else if (field == QStringLiteral("lower") || field == QStringLiteral("lowertexture")) {
		texture = &sidedef->lowerTexture;
	}
	if (!texture) {
		*error = QCoreApplication::translate("VibeStudioLevelMap", "Side fields are sector, offsetx, offsety, upper, middle, and lower.");
		return false;
	}
	const QString name = text.isEmpty() ? QStringLiteral("-") : text.toUpper();
	if (const QString problem = textureNameProblem(document, name); !problem.isEmpty()) {
		*error = problem;
		return false;
	}
	*texture = name;
	return true;
}

} // namespace

bool setLevelMapLinedefProperty(LevelMapDocument* document, int linedefId, const QString& key, const QString& value, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return setLevelMapLinedefProperty(candidate, linedefId, key, value, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	const QString field = key.trimmed().toLower();
	if (field.startsWith(QStringLiteral("front.")) || field.startsWith(QStringLiteral("back."))) {
		const bool front = field.startsWith(QStringLiteral("front."));
		return setLevelMapLinedefSideProperty(document, linedefId, front, field.mid(front ? 6 : 5), value, error);
	}
	if (!document || document->format != LevelMapFormat::DoomWad) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Linedefs belong to Doom and Hexen maps."));
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	const int index = indexOfObjectId(document->doomLinedefs, linedefId);
	if (index < 0) {
		return fail(selectionNotFoundText(LevelMapSelectionKind::DoomLinedef));
	}
	const bool hexen = document->doomFormat == LevelMapDoomFormat::Hexen;
	bool ok = false;
	const int number = value.trimmed().toInt(&ok);
	const LevelMapDoomLinedef before = document->doomLinedefs.at(index);
	LevelMapDoomLinedef after = before;
	int* target = nullptr;
	int highest = 0xffff;
	if (field == QStringLiteral("flags")) {
		target = &after.flags;
	} else if (field == QStringLiteral("special") || field == QStringLiteral("action")) {
		target = &after.special;
		highest = hexen ? 255 : 0xffff;
	} else if (!hexen && field == QStringLiteral("tag")) {
		target = &after.tag;
	} else if (hexen && field.size() == 4 && field.startsWith(QStringLiteral("arg")) && field.at(3) >= QLatin1Char('0') && field.at(3) <= QLatin1Char('4')) {
		target = &after.args[static_cast<size_t>(field.at(3).unicode() - '0')];
		highest = 255;
	}
	if (!target) {
		return fail(hexen ? QCoreApplication::translate("VibeStudioLevelMap", "Hexen linedef fields are flags, special, and arg0 to arg4, and front. or back. with a side field.")
				  : QCoreApplication::translate("VibeStudioLevelMap", "Linedef fields are flags, special, and tag, and front. or back. with a side field."));
	}
	if (!ok || number < 0 || number > highest) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A linedef's %1 is a whole number from 0 to %2.").arg(field).arg(highest));
	}
	const int previous = *target;
	*target = number;
	if (previous == number) {
		return true;
	}
	// A Hexen line's tag is its first argument, as the loader reads it: Merge
	// Sectors and the object list go by the tag.
	if (hexen) {
		after.tag = after.args[0];
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("set-linedef-property");
	command.objectKind = QStringLiteral("linedef");
	command.objectId = linedefId;
	command.key = field;
	command.oldValue = QString::number(previous);
	command.newValue = QString::number(number);
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Set linedef:%1 %2 to %3").arg(linedefId).arg(field).arg(number);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Set linedef:%1 %2 back to %3").arg(linedefId).arg(field).arg(previous);
	command.linedefSnapshots = {before};
	command.linedefResults = {after};
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(selectionNotFoundText(LevelMapSelectionKind::DoomLinedef));
	}
	pushUndo(document, command);
	return true;
}

bool setLevelMapLinedefSideProperty(LevelMapDocument* document, int linedefId, bool front, const QString& key, const QString& value, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return setLevelMapLinedefSideProperty(candidate, linedefId, front, key, value, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || document->format != LevelMapFormat::DoomWad) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Linedefs belong to Doom and Hexen maps."));
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	const int index = indexOfObjectId(document->doomLinedefs, linedefId);
	if (index < 0) {
		return fail(selectionNotFoundText(LevelMapSelectionKind::DoomLinedef));
	}
	const LevelMapDoomLinedef& linedef = document->doomLinedefs.at(index);
	const int sideId = front ? linedef.frontSidedef : linedef.backSidedef;
	const int sideIndex = sideId >= 0 ? indexOfObjectId(document->doomSidedefs, sideId) : -1;
	if (sideIndex < 0) {
		return fail(front ? QCoreApplication::translate("VibeStudioLevelMap", "Linedef %1 has no front side.").arg(linedefId) : QCoreApplication::translate("VibeStudioLevelMap", "Linedef %1 has no back side; it is one-sided.").arg(linedefId));
	}
	const LevelMapDoomSidedef& before = document->doomSidedefs.at(sideIndex);
	LevelMapDoomSidedef after = before;
	after.selected = false;
	QString why;
	if (!setDoomSidedefField(*document, &after, key, value, &why)) {
		return fail(why);
	}
	if (after.sector == before.sector && after.offsetX == before.offsetX && after.offsetY == before.offsetY && after.upperTexture == before.upperTexture
		&& after.middleTexture == before.middleTexture && after.lowerTexture == before.lowerTexture) {
		return true;
	}
	int users = 0;
	for (const LevelMapDoomLinedef& other : document->doomLinedefs) {
		users += (other.frontSidedef == sideId ? 1 : 0) + (other.backSidedef == sideId ? 1 : 0);
	}
	const QString field = key.trimmed().toLower();
	const QString side = front ? QStringLiteral("front") : QStringLiteral("back");
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("set-side-property");
	command.objectKind = QStringLiteral("linedef");
	command.objectId = linedefId;
	command.key = field;
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Set linedef:%1 %2 %3 to %4").arg(linedefId).arg(side, field, value.trimmed().isEmpty() ? QStringLiteral("-") : value.trimmed());
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Set linedef:%1 %2 %3 back").arg(linedefId).arg(side, field);
	if (users > 1) {
		// Other lines share the sidedef: this line takes its own copy.
		after.id = static_cast<int>(document->doomSidedefs.size());
		LevelMapDoomLinedef relinked = linedef;
		relinked.selected = false;
		(front ? relinked.frontSidedef : relinked.backSidedef) = after.id;
		command.linedefSnapshots = {linedef};
		command.linedefResults = {relinked};
	} else {
		command.sidedefSnapshots = {before};
	}
	command.sidedefResults = {after};
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(selectionNotFoundText(LevelMapSelectionKind::DoomLinedef));
	}
	pushUndo(document, command);
	return true;
}

bool connectLevelMapEntities(LevelMapDocument* document, QString* name, QString* error)
{
	QString sceneResult_name = name ? *name : QString{};
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return connectLevelMapEntities(candidate, name ? &sceneResult_name : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && name) { *name = std::move(sceneResult_name); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entities are connected in Quake-family .map files."));
	}
	// Each selected object's entity, in the order picked; a brush or patch
	// stands for the entity it belongs to.
	QVector<int> entityIds;
	for (const LevelMapSelectionRef& ref : document->selection) {
		int id = -1;
		if (ref.kind == LevelMapSelectionKind::Entity) {
			id = ref.objectId;
		} else if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			const int index = indexOfObjectId(document->brushes, ref.objectId);
			id = index >= 0 ? document->brushes.at(index).entityId : -1;
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			const int index = indexOfObjectId(document->patches, ref.objectId);
			id = index >= 0 ? document->patches.at(index).entityId : -1;
		}
		const int index = id >= 0 ? indexOfObjectId(document->entities, id) : -1;
		if (index < 0 || isWorldspawnEntity(document->entities.at(index))) {
			continue;
		}
		entityIds.removeAll(id);
		entityIds.push_back(id);
	}
	if (entityIds.size() < 2) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select the entities to link, the one they should target last; worldspawn cannot take part."));
	}
	const int targetId = entityIds.takeLast();
	const LevelMapEntity& target = document->entities.at(indexOfObjectId(document->entities, targetId));
	QString linkName = propertyValue(target, QStringLiteral("targetname")).trimmed();
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("transform-objects");
	command.objectKind = QStringLiteral("entity");
	if (linkName.isEmpty()) {
		// A source that already targets a name gives it to the target, so the
		// link it had keeps firing: Radiant's "prioritize existing target key".
		for (const int sourceId : entityIds) {
			const QString existing = propertyValue(document->entities.at(indexOfObjectId(document->entities, sourceId)), QStringLiteral("target")).trimmed();
			if (!existing.isEmpty()) {
				linkName = existing;
				break;
			}
		}
	}
	if (!linkName.isEmpty() && propertyValue(target, QStringLiteral("targetname")).trimmed().isEmpty()) {
		LevelMapEntity named = target;
		upsertEntityProperty(&named, QStringLiteral("targetname"), linkName);
		command.entitySnapshots.push_back(target);
		command.entityResults.push_back(named);
	} else if (linkName.isEmpty()) {
		// Radiant's t1, t2, ...: the lowest number no entity names or targets.
		QSet<QString> used;
		for (const LevelMapEntity& entity : document->entities) {
			for (const LevelMapProperty& property : entity.properties) {
				used.insert(property.value.trimmed().toLower());
			}
		}
		int number = 1;
		while (used.contains(QStringLiteral("t%1").arg(number))) {
			++number;
		}
		linkName = QStringLiteral("t%1").arg(number);
		LevelMapEntity named = target;
		upsertEntityProperty(&named, QStringLiteral("targetname"), linkName);
		command.entitySnapshots.push_back(target);
		command.entityResults.push_back(named);
	}
	for (const int sourceId : entityIds) {
		const LevelMapEntity& source = document->entities.at(indexOfObjectId(document->entities, sourceId));
		if (propertyValue(source, QStringLiteral("target")) == linkName) {
			continue;
		}
		LevelMapEntity linked = source;
		upsertEntityProperty(&linked, QStringLiteral("target"), linkName);
		command.entitySnapshots.push_back(source);
		command.entityResults.push_back(linked);
	}
	if (command.entityResults.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Those entities already target %1.").arg(linkName));
	}
	command.objectId = targetId;
	command.description = entityIds.size() == 1
		? QCoreApplication::translate("VibeStudioLevelMap", "Connect entity:%1 to entity:%2 as %3").arg(entityIds.first()).arg(targetId).arg(linkName)
		: QCoreApplication::translate("VibeStudioLevelMap", "Connect %1 entities to entity:%2 as %3").arg(entityIds.size()).arg(targetId).arg(linkName);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Disconnect them from entity:%1 again").arg(targetId);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The entities to connect are no longer in the map."));
	}
	pushUndo(document, command);
	if (name) {
		*name = linkName;
	}
	return true;
}

namespace {

// A door used by hand acts on the sector behind its line, never on tagged
// sectors, whatever tag the line carries: vanilla Doom's DR and D1 doors
// (EV_VerticalDoor in p_doors.c: specials 1, 26-28, 31-34, 117 and 118), and
// Boom's generalized types whose trigger, the low three bits, is D1 or DR
// (boomref.txt, "Generalized Linedef Types").
bool doomSpecialActsBehindLine(int special)
{
	switch (special) {
	case 1:
	case 26:
	case 27:
	case 28:
	case 31:
	case 32:
	case 33:
	case 34:
	case 117:
	case 118:
		return true;
	default:
		break;
	}
	return special >= 0x2F80 && special <= 0x7FFF && (special & 7) >= 6;
}

} // namespace

QVector<LevelMapTagLink> levelMapTagLinks(const LevelMapDocument& document)
{
	QVector<LevelMapTagLink> links;
	if (document.format != LevelMapFormat::DoomWad || document.doomFormat != LevelMapDoomFormat::Doom) {
		return links;
	}
	QHash<int, QVector<int>> sectorsByTag;
	for (const LevelMapDoomSector& sector : document.doomSectors) {
		if (sector.tag != 0) {
			sectorsByTag[sector.tag].push_back(sector.id);
		}
	}
	for (const LevelMapDoomLinedef& linedef : document.doomLinedefs) {
		if (linedef.special == 0 || linedef.tag == 0 || doomSpecialActsBehindLine(linedef.special)) {
			continue;
		}
		for (const int sectorId : sectorsByTag.value(linedef.tag)) {
			links.push_back({linedef.id, sectorId, linedef.tag});
		}
	}
	return links;
}

QVector<LevelMapSelectionRef> levelMapLinkedEntities(const LevelMapDocument& document, bool targets)
{
	// On a Doom map the links are tags: from a line to the sectors it acts
	// on, or from a sector to the lines that act on it.
	// Sets, not searches: this runs on every command refresh, and a selection
	// can hold the whole map.
	if (document.format == LevelMapFormat::DoomWad) {
		QSet<int> lines;
		QSet<int> sectors;
		for (const LevelMapSelectionRef& ref : document.selection) {
			if (ref.kind == LevelMapSelectionKind::DoomLinedef) {
				lines.insert(ref.objectId);
			} else if (ref.kind == LevelMapSelectionKind::DoomSector) {
				sectors.insert(ref.objectId);
			}
		}
		QVector<LevelMapSelectionRef> linked;
		QSet<int> added;
		for (const LevelMapTagLink& link : levelMapTagLinks(document)) {
			if (!(targets ? lines.contains(link.linedefId) : sectors.contains(link.sectorId))) {
				continue;
			}
			const int id = targets ? link.sectorId : link.linedefId;
			if (!added.contains(id)) {
				added.insert(id);
				linked.push_back({targets ? LevelMapSelectionKind::DoomSector : LevelMapSelectionKind::DoomLinedef, id});
			}
		}
		return linked;
	}
	QSet<int> selected;
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::Entity) {
			selected.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			const int index = indexOfObjectId(document.brushes, ref.objectId);
			if (index >= 0) {
				selected.insert(document.brushes.at(index).entityId);
			}
		}
	}
	QVector<LevelMapSelectionRef> linked;
	QSet<int> added;
	for (const LevelMapTargetLink& link : levelMapTargetLinks(document)) {
		const int from = targets ? link.sourceEntityId : link.targetEntityId;
		const int to = targets ? link.targetEntityId : link.sourceEntityId;
		if (selected.contains(from) && !added.contains(to)) {
			added.insert(to);
			linked.push_back({LevelMapSelectionKind::Entity, to});
		}
	}
	return linked;
}

bool commitLevelSurfaceEdit(LevelMapDocument* document, const LevelSurfaceEditPlan& plan, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return commitLevelSurfaceEdit(candidate, plan, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	if (!document || !plan.m_ready || document->revision != plan.m_revision || document->sourcePath != plan.m_sourcePath
		|| document->sourceContentHash != plan.m_sourceHash || document->format != plan.m_format
		|| (plan.m_bindSelection && document->selection != plan.m_selection)) {
		return fail(QCoreApplication::translate("LevelSurface", "The map changed after the surface preview. Prepare the edit again."));
	}
	LevelMapUndoCommand command;
	command.commandKind = plan.m_paste ? QStringLiteral("edit-brush") : QStringLiteral("transform-objects");
	command.objectKind = QStringLiteral("brush");
	for (int b = 0; b < plan.m_after.size(); ++b) {
		const auto& after = plan.m_after[b];
		const auto& before = plan.m_before[b];
		const int index = indexOfObjectId(document->brushes, before.id);
		if (index < 0 || document->brushes[index].faces.size() != before.faces.size()
			|| document->brushes[index].sourceLines != before.sourceLines || document->brushes[index].startLine != before.startLine) {
			return fail(QCoreApplication::translate("LevelSurface", "A brush changed after the surface preview. Prepare the edit again."));
		}
		const auto samePoint = [](const LevelMapVec3& a, const LevelMapVec3& b) {
			return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z;
		};
		// New in-memory documents can share an empty path/hash and revision.
		// Verify face bindings as well, so a plan cannot cross those documents.
		for (int f = 0; f < before.faces.size(); ++f) {
			const auto& a = before.faces[f];
			const auto& c = document->brushes[index].faces[f];
			if (a.line != c.line || a.textureName != c.textureName || !samePoint(a.p0, c.p0) || !samePoint(a.p1, c.p1) || !samePoint(a.p2, c.p2)
				|| a.explicitPlane != c.explicitPlane || !samePoint(a.planeNormal, c.planeNormal) || a.planeDistance != c.planeDistance
				|| a.explicitTextureMatrix != c.explicitTextureMatrix || a.textureMatrix != c.textureMatrix
				|| a.explicitTextureAxes != c.explicitTextureAxes || !samePoint(a.uAxis, c.uAxis) || !samePoint(a.vAxis, c.vAxis)
				|| a.uOffset != c.uOffset || a.vOffset != c.vOffset || a.shiftX != c.shiftX || a.shiftY != c.shiftY
				|| a.scaleX != c.scaleX || a.scaleY != c.scaleY || a.rotation != c.rotation
				|| a.contentFlags != c.contentFlags || a.surfaceFlags != c.surfaceFlags || a.surfaceValue != c.surfaceValue) {
				return fail(QCoreApplication::translate("LevelSurface", "A face changed after the surface preview. Prepare the edit again."));
			}
		}
		for (int f = 0; f < after.faces.size(); ++f) {
			if (!after.faces[f].textureParametersDirty) { continue; }
			LevelMapTextureChange change;
			change.kind = QStringLiteral("face");
			change.objectId = before.id;
			change.part = f;
			change.oldName = before.faces[f].textureName;
			change.newName = after.faces[f].textureName;
			if (const auto problem = textureNameProblem(*document, change.newName); !problem.isEmpty()) { return fail(problem); }
			if (const auto problem = textureChangesProblem(*document, {change}); !problem.isEmpty()) { return fail(problem); }
			const QString line = before.startLine > 0 ? document->textLines.value(before.faces[f].line - 1)
				: before.sourceLines.value(before.faces[f].line - before.sourceFirstLine);
			bool readable = false;
			replaceFaceTextureParameters(line, after.faces[f], &readable);
			if (!readable) {
				return fail(QCoreApplication::translate("LevelSurface", "Brush %1, face %2 cannot preserve its surface fields in the source layout.").arg(before.id).arg(f + 1));
			}
		}
		command.brushSnapshots.append(document->brushes[index]);
		command.brushResults.append(after);
	}
	for (int p = 0; p < plan.m_patchAfter.size(); ++p) {
		const auto& before = plan.m_patchBefore[p]; const auto& after = plan.m_patchAfter[p];
		const auto* current = patchById(document, before.id);
		if (!current || current->entityId != before.entityId || current->startLine != before.startLine || current->endLine != before.endLine
			|| current->sourceFirstLine != before.sourceFirstLine || current->sourceLines != before.sourceLines
			|| current->textureLine != before.textureLine || current->textureColumn != before.textureColumn
			|| levelPatchDefinition(*current) != levelPatchDefinition(before)) {
			return fail(QCoreApplication::translate("LevelSurface", "A patch changed after the surface preview. Prepare the edit again."));
		}
		if (after.definitionDirty) {
			if (!validateLevelPatch(after, error)) { return false; }
			if (!objectOwnsItsLines(*document, before.startLine, before.endLine)) {
				return fail(QCoreApplication::translate("LevelSurface", "The patch shares its boundary lines with other map text. Put its braces on separate lines before projecting UVs."));
			}
		}
		LevelMapTextureChange change; change.kind = QStringLiteral("patch"); change.objectId = before.id;
		change.oldName = before.textureName; change.newName = after.textureName;
		if (const auto problem = textureNameProblem(*document, change.newName); !problem.isEmpty()) { return fail(problem); }
		if (const auto problem = textureChangesProblem(*document, {change}); !problem.isEmpty()) { return fail(problem); }
		command.patchSnapshots << *current; command.patchResults << after;
	}
	if (command.brushResults.isEmpty() && command.patchResults.isEmpty()) { return true; }
	command.description = plan.m_paste ? QCoreApplication::translate("LevelSurface", "Paste %n surface(s)", nullptr, plan.m_faceCount + plan.patchCount())
		: QCoreApplication::translate("LevelSurface", "Align %n brush face(s)", nullptr, plan.m_faceCount);
	command.undoDescription = plan.m_paste ? QCoreApplication::translate("LevelSurface", "Restore pasted surfaces")
		: QCoreApplication::translate("LevelSurface", "Restore surface alignment");
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("LevelSurface", "The surface edit could not be applied."));
	}
	pushUndo(document, command);
	return true;
}

bool setLevelMapBrushFaceProperty(LevelMapDocument* document, int brushId, int faceIndex, const QString& field, const QString& value, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return setLevelMapBrushFaceProperty(candidate, brushId, faceIndex, field, value, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush faces belong to Quake-family .map files."));
	}
	const int index = indexOfObjectId(document->brushes, brushId);
	if (index < 0) {
		return fail(selectionNotFoundText(LevelMapSelectionKind::QuakeBrush));
	}
	const LevelMapBrush& brush = document->brushes.at(index);
	if (faceIndex < 0 || faceIndex >= brush.faces.size()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 has no face %2.").arg(brushId).arg(faceIndex + 1));
	}
	const LevelMapBrushFace& face = brush.faces.at(faceIndex);
	const QString key = field.trimmed().toLower();
	// The face is rewritten on its own line, which has to hold just this face
	// in a form the rewrite reads.
	LevelMapTextureChange change;
	change.kind = QStringLiteral("face");
	change.objectId = brushId;
	change.part = faceIndex;
	change.oldName = face.textureName;
	change.newName = face.textureName;
	if (const QString problem = textureChangesProblem(*document, {change}); !problem.isEmpty()) {
		return fail(problem);
	}
	if (key == QStringLiteral("texture")) {
		const QString name = value.trimmed();
		if (name.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "A face needs a texture name."));
		}
		if (const QString problem = textureNameProblem(*document, name); !problem.isEmpty()) {
			return fail(problem);
		}
		if (name == face.textureName) {
			return true;
		}
		change.newName = name;
		LevelMapUndoCommand command;
		command.commandKind = QStringLiteral("replace-texture");
		command.objectKind = QStringLiteral("brush");
		command.objectId = brushId;
		command.textureChanges = {change};
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Set brush:%1 face %2 texture to %3").arg(brushId).arg(faceIndex + 1).arg(name);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Set brush:%1 face %2 texture back to %3").arg(brushId).arg(faceIndex + 1).arg(face.textureName);
		if (!applyLevelMapCommand(document, command, true)) {
			return fail(selectionNotFoundText(LevelMapSelectionKind::QuakeBrush));
		}
		pushUndo(document, command);
		return true;
	}
	bool ok = false;
	const bool wholeMatrix = face.explicitTextureMatrix && key == QStringLiteral("matrix");
	const double number = wholeMatrix ? 0 : value.trimmed().toDouble(&ok);
	if (wholeMatrix) { ok = true; }
	if (!ok || !std::isfinite(number)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A face's %1 is a number.").arg(key));
	}
	LevelMapBrush after = brush;
	LevelMapBrushFace& changed = after.faces[faceIndex];
	double* target = nullptr;
	if (face.explicitTextureMatrix) {
		const QStringList fields {QStringLiteral("matrix00"), QStringLiteral("matrix01"), QStringLiteral("matrix02"),
			QStringLiteral("matrix10"), QStringLiteral("matrix11"), QStringLiteral("matrix12")};
		const int element = fields.indexOf(key);
		if (element >= 0) { target = &changed.textureMatrix[element]; }
		else if (wholeMatrix) {
			const auto values = value.trimmed().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
			if (values.size() != 6) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "A texture matrix needs six row-major numbers: Ux Uy Uoffset Vx Vy Voffset."));
			}
			for (int i = 0; i < 6; ++i) {
				const double n = values[i].toDouble(&ok);
				if (!ok || !std::isfinite(n) || std::abs(n) > 1e6) {
					return fail(QCoreApplication::translate("VibeStudioLevelMap", "Texture matrix values must be finite and within a million."));
				}
				changed.textureMatrix[i] = mapCoordinateText(n).toDouble();
			}
		} else {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush-primitive fields are texture, matrix, and matrix00 through matrix12 (two rows of three)."));
		}
	} else if (key == QStringLiteral("shiftx")) {
		target = face.explicitTextureAxes ? &changed.uOffset : &changed.shiftX;
	} else if (key == QStringLiteral("shifty")) {
		target = face.explicitTextureAxes ? &changed.vOffset : &changed.shiftY;
	} else if (key == QStringLiteral("rotation")) {
		target = &changed.rotation;
	} else if (key == QStringLiteral("scalex")) {
		target = &changed.scaleX;
	} else if (key == QStringLiteral("scaley")) {
		target = &changed.scaleY;
	}
	if (!target && !wholeMatrix) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Face fields are texture, shiftx, shifty, rotation, scalex, and scaley."));
	}
	if ((key == QStringLiteral("scalex") || key == QStringLiteral("scaley")) && number == 0.0) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A face's scale cannot be 0."));
	}
	// The number goes into the file as mapCoordinateText writes it, to six
	// places: it has to come back as itself, and a scale as more than 0.
	if (std::abs(number) > 1.0e6) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A face's %1 of %2 is too large to write; keep it within a million.").arg(key, value.trimmed()));
	}
	const double written = mapCoordinateText(number).toDouble();
	if ((key == QStringLiteral("scalex") || key == QStringLiteral("scaley")) && written == 0.0) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A face's scale of %1 is too small to write; the smallest a map file holds here is 0.000001.").arg(value.trimmed()));
	}
	if (target && *target == written) {
		return true;
	}
	if (key == QStringLiteral("rotation") && face.explicitTextureAxes) {
		// A Valve 220 face's axes carry its orientation, so they turn about the
		// face by the change, as TrenchBroom turns them (negated degrees).
		const MapPlane plane = planeFromPoints(face.p0, face.p1, face.p2);
		const double length = std::hypot(plane.normalX, plane.normalY, plane.normalZ);
		if (length <= 0.0) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Face %1 of brush %2 has no plane to turn its texture about.").arg(faceIndex + 1).arg(brushId));
		}
		const LevelMapVec3 axis {plane.normalX / length, plane.normalY / length, plane.normalZ / length, true};
		const auto trig = rotateLevelVector({1, 0, 0, true}, 2, -(written - face.rotation));
		const auto turn = [&axis, trig](const LevelMapVec3& vector) {
			// Rodrigues: v cos t + (k x v) sin t + k (k . v)(1 - cos t).
			const double cosine = trig.x;
			const double sine = trig.y;
			const double along = axis.x * vector.x + axis.y * vector.y + axis.z * vector.z;
			return LevelMapVec3 {vector.x * cosine + (axis.y * vector.z - axis.z * vector.y) * sine + axis.x * along * (1.0 - cosine),
				vector.y * cosine + (axis.z * vector.x - axis.x * vector.z) * sine + axis.y * along * (1.0 - cosine),
				vector.z * cosine + (axis.x * vector.y - axis.y * vector.x) * sine + axis.z * along * (1.0 - cosine), true};
		};
		changed.uAxis = turn(face.uAxis);
		changed.vAxis = turn(face.vAxis);
	}
	if (target) { *target = written; }
	if (face.explicitTextureMatrix) {
		const auto& m = changed.textureMatrix;
		if (std::abs(m[0] * m[4] - m[1] * m[3]) <= 1e-14) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The texture matrix collapses at map precision. Set all six matrix values together to change orientation."));
		}
		if (m == face.textureMatrix) { return true; }
	}
	// Save-back has to find the face's numbers after its name on its line;
	// a face that writes them some other way is refused rather than left as
	// it was with the edit reported as done.
	const QString faceLine = brush.startLine > 0 ? document->textLines.value(face.line - 1)
						     : brush.sourceLines.value(face.line - brush.sourceFirstLine);
	bool readable = false;
	replaceFaceTextureParameters(faceLine, changed, &readable);
	if (!readable) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Face %1 of brush %2 does not write all of its texture numbers after its name on its line, so they cannot be rewritten in place.")
				.arg(faceIndex + 1)
				.arg(brushId));
	}
	changed.textureParametersDirty = true;
	after.textureParametersDirty = true;
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("transform-objects");
	command.objectKind = QStringLiteral("brush");
	command.objectId = brushId;
	command.brushSnapshots = {brush};
	command.brushResults = {after};
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Set brush:%1 face %2 %3 to %4").arg(brushId).arg(faceIndex + 1).arg(key, wholeMatrix ? value.trimmed() : mapCoordinateText(number));
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Set brush:%1 face %2 %3 back").arg(brushId).arg(faceIndex + 1).arg(key);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(selectionNotFoundText(LevelMapSelectionKind::QuakeBrush));
	}
	pushUndo(document, command);
	return true;
}

bool prepareLevelMaterialPaint(const LevelMapDocument& document, const QVector<LevelMaterialTarget>& targets,
	const QString& material, LevelMaterialPaintPlan* plan, QString* error)
{
	if (error) { error->clear(); }
	if (plan) { *plan = {}; }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!plan || document.format == LevelMapFormat::Unknown ||
		(document.format == LevelMapFormat::DoomWad && document.doomFormat == LevelMapDoomFormat::Udmf)) {
		return fail(QCoreApplication::translate("LevelMaterialPaint", "Open an editable Quake, Doom or Hexen map to paint materials."));
	}
	if (targets.isEmpty() || targets.size() > 16384) {
		return fail(QCoreApplication::translate("LevelMaterialPaint", "Choose between 1 and 16384 material targets per stroke."));
	}
	const auto name = document.format == LevelMapFormat::DoomWad ? material.trimmed().toUpper() : material.trimmed();
	if (name.isEmpty() || name.size() > 1024) {
		return fail(QCoreApplication::translate("LevelMaterialPaint", "A material name must contain between 1 and 1024 characters."));
	}
	for (const auto ch : name) {
		if (!ch.isPrint() || (document.format == LevelMapFormat::DoomWad && ch.unicode() > 126)) {
			return fail(QCoreApplication::translate("LevelMaterialPaint", "Material names must be printable; Doom and Hexen names must use ASCII."));
		}
	}
	if (const auto problem = textureNameProblem(document, name); !problem.isEmpty()) { return fail(problem); }
	if (name.contains(QStringLiteral("/*")) || name.contains(QLatin1Char('\\'))) {
		return fail(QCoreApplication::translate("LevelMaterialPaint", "Use forward slashes in material paths; block-comment delimiters are not allowed."));
	}
	LevelMaterialPaintPlan result;
	QSet<LevelMaterialTarget> seen;
	for (const auto& target : targets) {
		if (seen.contains(target)) { continue; }
		seen.insert(target);
		QString before;
		if (!sampleLevelMaterial(document, target, &before, error)) { return false; }
		if (before == name) { continue; }
		LevelMapTextureChange change;
		change.objectId = target.objectId;
		change.oldName = before;
		change.newName = name;
		switch (target.kind) {
		case LevelMaterialKind::BrushFace: change.kind = QStringLiteral("face"); change.part = target.faceIndex; break;
		case LevelMaterialKind::Patch: change.kind = QStringLiteral("patch"); break;
		case LevelMaterialKind::WallUpper: change.kind = QStringLiteral("sidedef"); change.part = 0; break;
		case LevelMaterialKind::WallLower: change.kind = QStringLiteral("sidedef"); change.part = 1; break;
		case LevelMaterialKind::WallMiddle: change.kind = QStringLiteral("sidedef"); change.part = 2; break;
		case LevelMaterialKind::SectorFloor: change.kind = QStringLiteral("sector"); change.part = 0; break;
		case LevelMaterialKind::SectorCeiling: change.kind = QStringLiteral("sector"); change.part = 1; break;
		default: return fail(QCoreApplication::translate("LevelMaterialPaint", "This preview surface cannot receive a map material."));
		}
		result.m_changes << change;
	}
	if (const auto problem = textureChangesProblem(document, result.m_changes); !problem.isEmpty()) { return fail(problem); }
	result.m_revision = document.revision;
	result.m_sourcePath = document.sourcePath;
	result.m_sourceHash = document.sourceContentHash;
	result.m_format = document.format;
	result.m_doomFormat = document.doomFormat;
	result.m_mapName = document.mapName;
	result.m_material = name;
	result.m_targetCount = static_cast<int>(seen.size());
	result.m_ready = true;
	*plan = result;
	return true;
}

bool commitLevelMaterialPaint(LevelMapDocument* document, const LevelMaterialPaintPlan& plan, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return commitLevelMaterialPaint(candidate, plan, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || !plan.m_ready || document->revision != plan.m_revision || document->sourcePath != plan.m_sourcePath ||
		document->sourceContentHash != plan.m_sourceHash || document->format != plan.m_format ||
		document->doomFormat != plan.m_doomFormat || document->mapName != plan.m_mapName) {
		return fail(QCoreApplication::translate("LevelMaterialPaint", "The map changed. Start a new material stroke."));
	}
	// Validate the complete batch before applying it; stale or invalid targets
	// never leave an earlier target changed or insert a partial undo record.
	for (const auto& change : plan.m_changes) {
		LevelMaterialTarget target;
		target.objectId = change.objectId;
		if (change.kind == QStringLiteral("face")) { target.kind = LevelMaterialKind::BrushFace; target.faceIndex = change.part; }
		else if (change.kind == QStringLiteral("patch")) { target.kind = LevelMaterialKind::Patch; }
		else if (change.kind == QStringLiteral("sidedef")) {
			target.kind = change.part == 0 ? LevelMaterialKind::WallUpper : change.part == 1 ? LevelMaterialKind::WallLower : LevelMaterialKind::WallMiddle;
		} else { target.kind = change.part == 0 ? LevelMaterialKind::SectorFloor : LevelMaterialKind::SectorCeiling; }
		QString current;
		if (!sampleLevelMaterial(*document, target, &current, error)) { return false; }
		if (current != change.oldName) { return fail(QCoreApplication::translate("LevelMaterialPaint", "A material changed. Start a new material stroke.")); }
	}
	if (const auto problem = textureChangesProblem(*document, plan.m_changes); !problem.isEmpty()) { return fail(problem); }
	if (plan.m_changes.isEmpty()) { return true; }
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("replace-texture");
	command.objectKind = QStringLiteral("texture");
	command.textureChanges = plan.m_changes;
	command.description = QCoreApplication::translate("LevelMaterialPaint", "Paint %n surface(s) with %1", nullptr, plan.changedCount()).arg(plan.m_material);
	command.undoDescription = QCoreApplication::translate("LevelMaterialPaint", "Restore painted materials");
	if (!applyLevelMapCommand(document, command, true)) { return fail(QCoreApplication::translate("LevelMaterialPaint", "The material stroke could not be applied.")); }
	pushUndo(document, command);
	return true;
}

bool applyLevelMapTexture(LevelMapDocument* document, const QString& texture, int* applied, QString* error, bool allowUnchanged)
{
	int sceneResult_applied = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return applyLevelMapTexture(candidate, texture, applied ? &sceneResult_applied : nullptr, error, allowUnchanged);
	}); guarded.has_value()) {
		if (*guarded && applied) { *applied = std::move(sceneResult_applied); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (applied) {
		*applied = 0;
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Textures are applied to Quake-family brushes and patches; a Doom map's walls and flats take Replace Texture."));
	}
	const QString name = texture.trimmed();
	if (name.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Name the texture to apply."));
	}
	if (const QString problem = textureNameProblem(*document, name); !problem.isEmpty()) {
		return fail(problem);
	}
	if (document->selection.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing is selected."));
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("replace-texture");
	command.objectKind = QStringLiteral("texture");
	bool any = false;
	for (LevelMapTextureChange use : textureUsesInScope(*document, true)) {
		if (use.kind != QStringLiteral("face") && use.kind != QStringLiteral("patch")) {
			continue;
		}
		any = true;
		if (use.oldName != name) {
			use.newName = name;
			command.textureChanges.push_back(use);
		}
	}
	if (!any) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select brushes, patches, or brush entities to texture."));
	}
	if (command.textureChanges.isEmpty()) {
		if (allowUnchanged) { return true; }
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The selection already uses %1 everywhere.").arg(name));
	}
	if (const QString problem = textureChangesProblem(*document, command.textureChanges); !problem.isEmpty()) {
		return fail(problem);
	}
	const int count = static_cast<int>(command.textureChanges.size());
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Apply %1 to %2 faces and patches").arg(name).arg(count);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Put back the textures of %1 faces and patches").arg(count);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The textures to change are no longer in the map."));
	}
	pushUndo(document, command);
	if (applied) {
		*applied = count;
	}
	return true;
}

bool replaceLevelMapTexture(LevelMapDocument* document, const QString& from, const QString& to, bool selectionOnly, int* replaced, QString* error)
{
	int sceneResult_replaced = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return replaceLevelMapTexture(candidate, from, to, selectionOnly, replaced ? &sceneResult_replaced : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && replaced) { *replaced = std::move(sceneResult_replaced); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (replaced) {
		*replaced = 0;
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || document->format == LevelMapFormat::Unknown) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (document->format == LevelMapFormat::DoomWad && document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	const QString source = from.trimmed();
	const QString target = to.trimmed();
	if (source.isEmpty() || target.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Name the texture to replace and the texture to use instead."));
	}
	if (const QString problem = textureNameProblem(*document, target); !problem.isEmpty()) {
		return fail(problem);
	}
	if (selectionOnly && document->selection.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing is selected."));
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("replace-texture");
	command.objectKind = QStringLiteral("texture");
	for (LevelMapTextureChange use : textureUsesInScope(*document, selectionOnly)) {
		if (use.oldName.trimmed().compare(source, Qt::CaseInsensitive) == 0 && use.oldName != target) {
			use.newName = target;
			command.textureChanges.push_back(use);
		}
	}
	if (command.textureChanges.isEmpty()) {
		return fail(selectionOnly ? QCoreApplication::translate("VibeStudioLevelMap", "The selection does not use %1.").arg(source) : QCoreApplication::translate("VibeStudioLevelMap", "The map does not use %1.").arg(source));
	}
	if (const QString problem = textureChangesProblem(*document, command.textureChanges); !problem.isEmpty()) {
		return fail(problem);
	}
	const int count = static_cast<int>(command.textureChanges.size());
	command.description = QCoreApplication::translate("VibeStudioLevelMap", "Replace %1 with %2 (%3 uses)").arg(source, target).arg(count);
	command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Put %1 back (%2 uses)").arg(source).arg(count);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The textures to replace are no longer in the map."));
	}
	pushUndo(document, command);
	if (replaced) {
		*replaced = count;
	}
	return true;
}

namespace {

// How a transform moves things: points (about the selection's centre, which
// it is handed), directions such as texture axes, plane normals (as
// directions when `normal` is empty), and yaw angles in degrees (nullopt keeps
// an angle as written). A mirror reverses handedness, so faces swap two points
// and patches reverse their columns to keep facing outward.
struct SelectionTransform {
	std::function<LevelMapVec3(const LevelMapVec3& point, const LevelMapVec3& centre)> point;
	std::function<LevelMapVec3(const LevelMapVec3& direction)> direction;
	std::function<LevelMapVec3(const LevelMapVec3& normal)> normal;
	std::function<std::optional<double>(double yaw)> yaw;
	std::optional<LevelMapRotationRequest> rotation;
	LevelMapTextureLockOptions textures;
	// Snap supplies an independent translation for each native record. Missing
	// records stay untouched; owner/child and shared-vertex overlap is resolved
	// before validation. Ordinary affine transforms leave this disabled.
	bool individualOffsets = false;
	QHash<QString, LevelMapVec3> offsets;
	bool mirror = false;
	QString description;
	QString undoDescription;
};

// What a transform of the selection moves: selected entities with their
// brushes and patches, selected brushes and patches, and selected things. A
// Doom thing's mirror entity moves as the thing, not as an entity.
struct TransformSet {
	QSet<int> entityIds;
	QSet<int> brushIds;
	QSet<int> patchIds;
	QSet<int> thingIds;
	QSet<int> vertexIds;

	[[nodiscard]] bool isEmpty() const
	{
		return entityIds.isEmpty() && brushIds.isEmpty() && patchIds.isEmpty() && thingIds.isEmpty() && vertexIds.isEmpty();
	}
};

TransformSet transformSetFor(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects)
{
	const bool doom = document.format == LevelMapFormat::DoomWad;
	TransformSet set;
	QSet<int> lines, sectors;
	for (const LevelMapSelectionRef& ref : objects) {
		detail::placementCancellationCheckpoint();
		switch (ref.kind) {
		case LevelMapSelectionKind::Entity:
			if (!doom) {
				set.entityIds.insert(ref.objectId);
			} else { set.thingIds.insert(ref.objectId); }
			break;
		case LevelMapSelectionKind::QuakeBrush:
			set.brushIds.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::QuakePatch:
			set.patchIds.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::DoomThing:
			set.thingIds.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::DoomVertex: set.vertexIds.insert(ref.objectId); break;
		case LevelMapSelectionKind::DoomLinedef: lines.insert(ref.objectId); break;
		case LevelMapSelectionKind::DoomSector: sectors.insert(ref.objectId); break;
		default:
			break;
		}
	}
	QSet<int> sectorSides;
	if (!sectors.isEmpty()) {
		for (const auto& side : document.doomSidedefs) {
		detail::placementCancellationCheckpoint();
			if (sectors.contains(side.sector)) { sectorSides.insert(side.id); }
		}
	}
	for (const auto& line : document.doomLinedefs) {
		detail::placementCancellationCheckpoint();
		if (lines.contains(line.id) || sectorSides.contains(line.frontSidedef) || sectorSides.contains(line.backSidedef)) {
			set.vertexIds.insert(line.startVertex); set.vertexIds.insert(line.endVertex);
		}
	}
	for (const LevelMapBrush& brush : document.brushes) {
		detail::placementCancellationCheckpoint();
		if (set.entityIds.contains(brush.entityId)) {
			set.brushIds.insert(brush.id);
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		detail::placementCancellationCheckpoint();
		if (set.entityIds.contains(patch.entityId)) {
			set.patchIds.insert(patch.id);
		}
	}
	return set;
}

// The box around a transform set: entity origins, solved brush bounds, patch
// bounds, and thing positions. False when none of them has a position.
bool transformSetBounds(const LevelMapDocument& document, const TransformSet& set, LevelMapVec3* mins, LevelMapVec3* maxs)
{
	LevelMapVec3 low;
	LevelMapVec3 high;
	for (const LevelMapEntity& entity : document.entities) {
		if (set.entityIds.contains(entity.id) && entity.origin.valid) {
			includePoint(&low, &high, entity.origin);
		}
	}
	for (const LevelMapBrush& brush : document.brushes) {
		if (set.brushIds.contains(brush.id) && brush.boundsSolved) {
			includePoint(&low, &high, brush.mins);
			includePoint(&low, &high, brush.maxs);
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (set.patchIds.contains(patch.id)) {
			includePoint(&low, &high, patch.mins);
			includePoint(&low, &high, patch.maxs);
		}
	}
	for (const LevelMapDoomThing& thing : document.doomThings) {
		if (set.thingIds.contains(thing.id)) {
			includePoint(&low, &high, {thing.x, thing.y, document.doomUdmf ? thing.z : 0.0, true});
		}
	}
	for (const auto& vertex : document.doomVertices) {
		if (set.vertexIds.contains(vertex.id)) { includePoint(&low, &high, {vertex.x, vertex.y, 0, true}); }
	}
	if (!low.valid) {
		return false;
	}
	if (mins) {
		*mins = low;
	}
	if (maxs) {
		*maxs = high;
	}
	return true;
}

// Adds degrees to an angle written as a number, keeping it in 0-359, through
// `yaw`. The Quake up and down markers, -1 and -2, stay as they are.
QString transformedAngle(const QString& value, const std::function<std::optional<double>(double)>& yaw)
{
	bool ok = false;
	const double angle = value.trimmed().toDouble(&ok);
	if (!ok || angle == -1.0 || angle == -2.0) {
		return value;
	}
	const std::optional<double> turned = yaw(angle);
	if (!turned) {
		return value;
	}
	double normalized = std::fmod(*turned, 360.0);
	if (normalized < 0.0) {
		normalized += 360.0;
	}
	return mapCoordinateText(normalized);
}

bool udmfPolyobjectControl(const LevelMapDocument& document, int thingType)
{
	if (!document.doomUdmf) { return false; }
	const auto nameSpace = document.doomUdmf->nameSpace.toLower();
	// Format facts: ZDBSP processor.cpp, GetPolySpots, revision
	// bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659 (GPL-2.0-or-later).
	// https://github.com/rheit/zdbsp/blob/bcb9bdbcaf8ad296242c03cf3f9bff7ee732f659/processor.cpp
	// These controls use angle as a polyobject ID and XY as node-builder inputs.
	return (nameSpace == QStringLiteral("zdoom") || nameSpace == QStringLiteral("hexen") || nameSpace == QStringLiteral("vavoom"))
		&& ((thingType >= 3000 && thingType <= 3002) || (thingType >= 9300 && thingType <= 9303));
}

bool transformLevelMapSelection(LevelMapDocument* document, const SelectionTransform& transform, bool doomAllowed, QString* error,
	LevelMapUndoCommand* prepared = nullptr)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	const bool doom = document->format == LevelMapFormat::DoomWad;
	const bool udmf = doom && document->doomFormat == LevelMapDoomFormat::Udmf;
	if (doom && !doomAllowed) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A Doom map turns and flips in the top view."));
	}
	if (udmf && !document->doomUdmf) {
		return fail(QCoreApplication::translate("LevelUdmf", "Reload the UDMF map before transforming its geometry."));
	}

	LevelMapObjectExistence existing(document);
	for (const auto& ref : document->selection) {
		if (!existing.contains(ref)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The selected object %1 no longer exists.").arg(levelMapSelectionRefId(ref)));
		}
	}
	const LevelMapBrushLineConflicts sharedLines(*document);
	const TransformSet set = transformSetFor(*document, document->selection);
	const QSet<int>& entityIds = set.entityIds;
	const QSet<int>& brushIds = set.brushIds;
	const QSet<int>& patchIds = set.patchIds;
	const QSet<int>& thingIds = set.thingIds;
	if (set.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing selected can be transformed."));
	}

	// The centre of everything that moves.
	LevelMapVec3 low;
	LevelMapVec3 high;
	if (!transformSetBounds(*document, set, &low, &high)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing selected has a position to transform about."));
	}
	const LevelMapVec3 centre = transform.rotation && transform.rotation->pivot.valid ? transform.rotation->pivot
		: LevelMapVec3 {(low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (low.z + high.z) * 0.5, true};
	const auto pointsFor = [&transform, &centre](const QString& kind, int id) {
		const auto offset = transform.offsets.value(QStringLiteral("%1:%2").arg(kind).arg(id));
		return [&transform, &centre, offset](const LevelMapVec3& point) {
			if (!point.valid) { return point; }
			return transform.individualOffsets
				? LevelMapVec3{point.x + offset.x, point.y + offset.y, point.z + offset.z, true}
				: transform.point(point, centre);
		};
	};
	const auto included = [&transform](const QString& kind, int id, const QSet<int>& ids) {
		return ids.contains(id) && (!transform.individualOffsets || transform.offsets.contains(QStringLiteral("%1:%2").arg(kind).arg(id)));
	};

	qint64 visited = 0;
	const qint64 total = document->entities.size() + document->brushes.size() + document->patches.size()
		+ document->doomThings.size() + document->doomVertices.size();
	detail::placementCheckpoint(LevelPlacementPhase::Transforming, 0, total);
	const auto checkpoint = [&] {
		if ((visited++ & 31) == 0) { detail::placementCheckpoint(LevelPlacementPhase::Transforming, visited-1, total); }
	};
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("transform-objects");
	command.objectKind = QStringLiteral("selection");
	for (const LevelMapEntity& entity : document->entities) {
		checkpoint();
		if (!included(QStringLiteral("entity"), entity.id, entityIds)) {
			continue;
		}
		const auto movePoint = pointsFor(QStringLiteral("entity"), entity.id);
		LevelMapEntity moved = entity;
		// A key whose numbers do not change keeps its text as written.
		if (moved.origin.valid) {
			moved.origin = movePoint(entity.origin);
			if ((!moved.origin.valid || !std::isfinite(moved.origin.x) || !std::isfinite(moved.origin.y) || !std::isfinite(moved.origin.z)
				|| std::max({std::abs(moved.origin.x), std::abs(moved.origin.y), std::abs(moved.origin.z)}) > 32768)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 would leave the supported world bounds of -32768 to 32768.").arg(entity.id));
			}
			if (moved.origin.x != entity.origin.x || moved.origin.y != entity.origin.y || moved.origin.z != entity.origin.z) {
				const QString origin = transform.rotation ? QStringLiteral("%1 %2 %3").arg(mapCoordinateText(moved.origin.x), mapCoordinateText(moved.origin.y), mapCoordinateText(moved.origin.z)) : serializeVec3(moved.origin);
				upsertEntityProperty(&moved, QStringLiteral("origin"), origin);
			}
		}
		for (LevelMapProperty& property : moved.properties) {
			if (property.key.compare(QStringLiteral("angle"), Qt::CaseInsensitive) == 0) {
				if (transform.rotation && transform.rotation->axis != 2) {
					bool valid = false;
					const double angle = property.value.toDouble(&valid);
					const auto heading = angle == -1 ? LevelMapVec3 {0, 0, 1, true} : angle == -2 ? LevelMapVec3 {0, 0, -1, true}
						: rotateLevelVector({1, 0, 0, true}, 2, angle);
					const auto direction = transform.direction(heading);
					if (!valid || !direction.valid || (std::abs(direction.z) > 1e-9 && std::hypot(direction.x, direction.y) > 1e-9)) {
						return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 uses a scalar angle that cannot represent this rotation. Use an angles property only if its game definition supports it.").arg(entity.id));
					}
					if (std::abs(direction.z) > 1e-9) { property.value = direction.z > 0 ? QStringLiteral("-1") : QStringLiteral("-2"); }
					else { property.value = mapCoordinateText(std::fmod(std::atan2(direction.y, direction.x)*180.0/3.14159265358979323846 + 360, 360)); }
					continue;
				}
				property.value = transformedAngle(property.value, transform.yaw);
			} else if (property.key.compare(QStringLiteral("angles"), Qt::CaseInsensitive) == 0) {
				if (transform.rotation) {
					const auto angles = rotateLevelAngles(parseVec3(property.value), transform.rotation->axis, transform.rotation->degrees);
					if (!angles.valid) { return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 has invalid angles.").arg(entity.id)); }
					property.value = QStringLiteral("%1 %2 %3").arg(mapCoordinateText(angles.x), mapCoordinateText(angles.y), mapCoordinateText(angles.z));
					continue;
				}
				QStringList parts = property.value.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
				if (parts.size() == 3) {
					const QString yaw = transformedAngle(parts.at(1), transform.yaw);
					if (yaw != parts.at(1)) {
						parts[1] = yaw;
						property.value = parts.join(QLatin1Char(' '));
					}
				}
			}
		}
		command.entitySnapshots.push_back(entity);
		command.entityResults.push_back(moved);
	}
	for (const LevelMapBrush& brush : document->brushes) {
		checkpoint();
		if (!included(QStringLiteral("brush"), brush.id, brushIds)) {
			continue;
		}
		const auto movePoint = pointsFor(QStringLiteral("brush"), brush.id);
		LevelTextureAffine textureTransform;
		for (auto& column : textureTransform.columns) { column = transform.direction(column); }
		textureTransform.translation = movePoint({0, 0, 0, true});
		if (sharedLines.contains(brush.id)) {
			return fail(sharedFaceLinesText(brush.id));
		}
		LevelMapBrush moved = brush;
		// Validate the original binding syntax; generating transformed text here
		// only to discard it repeated tokenization/formatting for every face.
		const auto source = brush.startLine > 0 ? document->textLines.mid(brush.startLine-1, brush.endLine-brush.startLine+1) : brush.sourceLines;
		int faceIndex = 0;
		for (LevelMapBrushFace& face : moved.faces) {
			detail::placementCancellationCheckpoint();
			const auto before = face;
			face.p0 = movePoint(face.p0);
			face.p1 = movePoint(face.p1);
			face.p2 = movePoint(face.p2);
			if (transform.mirror) {
				// A mirror turns the winding inside out; swapping two points
				// turns it back, so the face still faces out of the brush.
				std::swap(face.p1, face.p2);
			}
			if (face.explicitPlane && face.planeNormal.valid) {
				// n.p + d = 0: move the normal and a point of the plane, then
				// read the distance back from them.
				const double lengthSquared = face.planeNormal.x * face.planeNormal.x + face.planeNormal.y * face.planeNormal.y
					+ face.planeNormal.z * face.planeNormal.z;
				if (lengthSquared > 0.0) {
					const double scale = -face.planeDistance / lengthSquared;
					const LevelMapVec3 onPlane = movePoint({face.planeNormal.x * scale, face.planeNormal.y * scale, face.planeNormal.z * scale, true});
					face.planeNormal = transform.normal ? transform.normal(face.planeNormal) : transform.direction(face.planeNormal);
					face.planeDistance = -(face.planeNormal.x * onPlane.x + face.planeNormal.y * onPlane.y + face.planeNormal.z * onPlane.z);
				}
			}
			if (transform.rotation || transform.textures.enabled) {
				if (transform.textures.enabled) {
					QString problem;
					if (!lockTransformedLevelTexture(before, &face, textureTransform, transform.textures.allowValve220, &problem)) {
						return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1, face %2: %3").arg(brush.id).arg(faceIndex+1).arg(problem));
					}
					moved.textureParametersDirty = true;
					if (face.explicitTextureAxes && !before.explicitTextureAxes) { moved.primitiveKind = QStringLiteral("valve220"); }
				}
				bool readable = false;
				const int line = face.line - (brush.startLine > 0 ? brush.startLine : brush.sourceFirstLine);
				replaceFaceTextureParameters(source.value(line), face, &readable, true);
				if (!readable) { return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1, face %2 must be on one complete source line before transforming.").arg(brush.id).arg(faceIndex+1)); }
			}
			++faceIndex;
		}
		{
			const auto solved = solveBrushGeometry(moved.faces, moved.id, moved.entityId, MapGeometryPrecision::CompilerCompatible, [] {
				detail::placementCancellationCheckpoint(); return false;
			});
			if (!solved.solved) { return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 cannot be reconstructed after the transform.").arg(brush.id)); }
			if (std::max({std::abs(solved.mins.x), std::abs(solved.mins.y), std::abs(solved.mins.z), std::abs(solved.maxs.x), std::abs(solved.maxs.y), std::abs(solved.maxs.z)}) > 32768) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 would leave the supported world bounds of -32768 to 32768.").arg(brush.id));
			}
			moved.mins = solved.mins; moved.maxs = solved.maxs; moved.boundsSolved = true;
		}
		moved.geometryDirty = true;
		command.brushSnapshots.push_back(brush);
		command.brushResults.push_back(moved);
	}
	for (const LevelMapPatch& patch : document->patches) {
		checkpoint();
		if (!included(QStringLiteral("patch"), patch.id, patchIds)) {
			continue;
		}
		const auto movePoint = pointsFor(QStringLiteral("patch"), patch.id);
		if (!patch.controlGridNormalized) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Patch %1's control grid could not be read, so it cannot be transformed.").arg(patch.id));
		}
		LevelMapPatch moved = patch;
		for (LevelMapVec3& point : moved.controlPoints) {
			detail::placementCancellationCheckpoint();
			point = movePoint(point);
			if ((!point.valid || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)
				|| std::max({std::abs(point.x), std::abs(point.y), std::abs(point.z)}) > 32768)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 would leave the supported world bounds of -32768 to 32768.").arg(patch.id));
			}
		}
		if (transform.mirror) {
			// Reversing the columns keeps the patch facing the way it did; the
			// texture coordinates travel with their points.
			for (int row = 0; row < moved.height; ++row) {
				for (int column = 0; column < moved.width / 2; ++column) {
					const int left = row * moved.width + column;
					const int right = row * moved.width + (moved.width - 1 - column);
					if (right >= moved.controlPoints.size()) {
						continue;
					}
					std::swap(moved.controlPoints[left], moved.controlPoints[right]);
					if (right < moved.controlU.size() && right < moved.controlV.size()) {
						std::swap(moved.controlU[left], moved.controlU[right]);
						std::swap(moved.controlV[left], moved.controlV[right]);
					}
				}
			}
		}
		moved.mins = {}; moved.maxs = {};
		for (const auto& point : moved.controlPoints) { includePoint(&moved.mins, &moved.maxs, point); }
		moved.geometryDirty = true;
		command.patchSnapshots.push_back(patch);
		command.patchResults.push_back(moved);
	}
	for (const LevelMapDoomThing& thing : document->doomThings) {
		checkpoint();
		if (!included(QStringLiteral("thing"), thing.id, thingIds)) {
			continue;
		}
		const auto movePoint = pointsFor(QStringLiteral("thing"), thing.id);
		LevelMapDoomThing moved = thing;
		const bool polyobjectControl = udmf && udmfPolyobjectControl(*document, thing.type);
		const LevelMapVec3 position = movePoint({thing.x, thing.y, udmf ? thing.z : 0.0, true});
		// Whole units, as the WAD stores them, so what is shown is what saves.
		moved.x = udmf ? position.x : std::round(position.x);
		moved.y = udmf ? position.y : std::round(position.y);
		if (udmf) { moved.z = position.z; }
		if (udmf && (!std::isfinite(moved.x) || !std::isfinite(moved.y) || !std::isfinite(moved.z)
			|| std::max({std::abs(moved.x), std::abs(moved.y), std::abs(moved.z)}) > 1e7)) {
			return fail(QCoreApplication::translate("LevelUdmf", "Thing %1 would leave the finite UDMF editor coordinate range.").arg(thing.id));
		}
		if (const QString problem = udmf ? QString() : doomThingPlaceProblem(*document, moved.x, moved.y, moved.z); !problem.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Thing %1 would leave the map: %2").arg(thing.id).arg(problem));
		}
		if (const std::optional<double> yaw = transform.yaw(thing.angle); yaw && !polyobjectControl) {
			// UDMF accepts signed 32-bit angles; normalize before narrowing so a
			// reflected/rotated extreme value cannot overflow the native integer.
			moved.angle = ((static_cast<int>(std::lround(std::fmod(*yaw, 360.0))) % 360) + 360) % 360;
		}
		command.udmfNodeInputsChanged |= polyobjectControl && (moved.x != thing.x || moved.y != thing.y);
		command.thingSnapshots.push_back(thing);
		command.thingResults.push_back(moved);
		if (const LevelMapEntity* mirror = entityById(document, thing.id)) {
			command.entitySnapshots.push_back(*mirror);
			command.entityResults.push_back(doomThingMirror(moved));
		}
	}
	QHash<int, LevelMapDoomVertex> movedVertices;
	for (const auto& vertex : document->doomVertices) {
		checkpoint();
		if (!included(QStringLiteral("vertex"), vertex.id, set.vertexIds)) { continue; }
		const auto movePoint = pointsFor(QStringLiteral("vertex"), vertex.id);
		auto moved = vertex;
		const auto position = movePoint({vertex.x, vertex.y, 0, true});
		moved.x = udmf ? position.x : std::round(position.x); moved.y = udmf ? position.y : std::round(position.y);
		if (udmf && position.z != 0) {
			return fail(QCoreApplication::translate("LevelUdmf", "UDMF vertices move in XY. Edit sector heights to change level elevation."));
		}
		const double minimum = udmf ? -1e7 : -32768, maximum = udmf ? 1e7 : 32767;
		if (!std::isfinite(moved.x) || !std::isfinite(moved.y) || moved.x < minimum || moved.y < minimum || moved.x > maximum || moved.y > maximum) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Vertex %1 would leave the WAD coordinate range.").arg(vertex.id));
		}
		movedVertices.insert(vertex.id, moved);
		command.udmfNodeInputsChanged |= udmf && (moved.x != vertex.x || moved.y != vertex.y);
		command.vertexSnapshots.push_back(vertex); command.vertexResults.push_back(moved);
	}
	if (!movedVertices.isEmpty()) {
		const auto vertexAt = [&](int id) { return movedVertices.contains(id) ? movedVertices[id] : document->doomVertices.value(indexOfObjectId(document->doomVertices, id)); };
		for (const auto& line : document->doomLinedefs) {
			if (!movedVertices.contains(line.startVertex) && !movedVertices.contains(line.endVertex)) { continue; }
			const auto a = vertexAt(line.startVertex), b = vertexAt(line.endVertex);
			if (a.id < 0 || b.id < 0 || (a.x == b.x && a.y == b.y)) {
				return fail(udmf ? QCoreApplication::translate("LevelUdmf", "The transform would collapse linedef %1.").arg(line.id)
					: QCoreApplication::translate("VibeStudioLevelMap", "The transform would collapse linedef %1 after rounding to WAD coordinates.").arg(line.id));
			}
		}
	}
	detail::placementCheckpoint(LevelPlacementPhase::Transforming, total, total);
	if (transform.mirror && !movedVertices.isEmpty()) {
		QString message;
		if (!prepareLevelMapDoomMirrorLines(*document, set.vertexIds, &command.linedefSnapshots, &command.linedefResults, &message)) {
			return fail(message);
		}
		command.udmfNodeInputsChanged |= udmf && !command.linedefResults.isEmpty();
	}
	command.description = transform.description;
	// q3map2 chooses Quake/Valve syntax from the first face in the map. A
	// single converted face therefore requires a consistent map-wide dialect.
	// Conversion changes projection representation, never unselected geometry.
	bool convertsClassic = false;
	for (int b = 0; b < command.brushResults.size(); ++b) {
		const auto& before = command.brushSnapshots[b];
		const auto& after = command.brushResults[b];
		for (int f = 0; f < before.faces.size(); ++f) {
			convertsClassic |= !before.faces[f].explicitTextureAxes && after.faces[f].explicitTextureAxes;
		}
	}
	if (convertsClassic) {
		for (const auto& brush : document->brushes) {
			for (const auto& face : brush.faces) {
				if (face.explicitTextureMatrix) { return fail(QCoreApplication::translate("VibeStudioLevelMap", "Valve 220 conversion cannot be mixed with brush-primitive matrices in the same map.")); }
			}
			int index = indexOfObjectId(command.brushResults, brush.id);
			auto converted = index < 0 ? brush : command.brushResults[index];
			const auto source = brushTextNow(*document, brush);
			bool changed = false;
			for (auto& face : converted.faces) {
				if (face.explicitTextureAxes) { continue; }
				const auto projection = levelTextureProjection(face);
				if (!projection.valid) { return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 has an invalid projection and cannot be converted to Valve 220.").arg(brush.id)); }
				face.explicitTextureAxes = true;
				face.uAxis = projection.u; face.vAxis = projection.v;
				face.uOffset = projection.offsetU; face.vOffset = projection.offsetV;
				face.scaleX = 1; face.scaleY = 1; face.rotation = 0;
				face.textureParametersDirty = true;
				const int line = face.line - (brush.startLine > 0 ? brush.startLine : brush.sourceFirstLine);
				bool readable = false;
				replaceFaceTextureParameters(source.value(line), face, &readable);
				if (!readable || sharedLines.contains(brush.id)) {
					return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 must have one complete face per source line before map-wide Valve 220 conversion.").arg(brush.id));
				}
				changed = true;
			}
			if (!changed) { continue; }
			converted.textureParametersDirty = true;
			converted.primitiveKind = QStringLiteral("valve220");
			if (index < 0) { command.brushSnapshots.push_back(brush); command.brushResults.push_back(converted); }
			else { command.brushResults[index] = converted; }
		}
	}
	command.undoDescription = transform.undoDescription;
	if (transform.individualOffsets) {
		// Reporting only: transform-objects replays exact native snapshots.
		auto keys = transform.offsets.keys();
		std::sort(keys.begin(), keys.end());
		for (const auto& key : keys) {
			LevelMapMoveStep step;
			step.objectKind = key.section(':', 0, 0);
			step.objectId = key.section(':', 1).toInt();
			step.delta = transform.offsets.value(key);
			command.moveSteps << step;
		}
	}
	if (prepared) { *prepared = std::move(command); return true; }
	detail::placementCheckpoint(LevelPlacementPhase::Finalizing);
	if (udmf) {
		const auto cancel = [] { detail::placementCancellationCheckpoint(); return false; };
		QString problem;
		const auto bytes = prepareLevelUdmfTransform(*document->doomUdmf, command, &problem, cancel);
		if (!problem.isEmpty()) { return fail(problem); }
		if (bytes == document->doomUdmf->source) {
			return fail(QCoreApplication::translate("LevelUdmf", "The transform leaves the selected objects unchanged."));
		}
		command.commandKind = QStringLiteral("udmf-transform");
		return commitUdmfBytes(document, bytes, command, error, cancel);
	}
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The objects to move are no longer in the map."));
	}
	pushUndo(document, command);
	return true;
}

bool prepareLevelMapPlacement(const LevelMapDocument& source, const QVector<LevelMapSelectionRef>& objects,
	const LevelMapVec3& offset, const LevelMapTextureLockOptions& textures, LevelMapUndoCommand* prepared, QString* error)
{
	if (offset.x == 0 && offset.y == 0 && offset.z == 0) { return true; }
	auto draft = source;
	draft.selection = objects;
	SelectionTransform transform;
	transform.textures = textures;
	transform.point = [offset](const auto& p, const auto&) { return LevelMapVec3{p.x+offset.x, p.y+offset.y, p.z+offset.z, p.valid}; };
	transform.direction = [](const auto& p) { return p; };
	transform.yaw = [](double) -> std::optional<double> { return std::nullopt; };
	// No command is applied here: scene protection is checked when the outer
	// operation publishes the copies, using their actual destination identities.
	return transformLevelMapSelection(&draft, transform, false, error, prepared);
}

} // namespace

namespace {

// A brush's own text, ready to be rebuilt into pieces: its lines as they would
// be saved now, the line of each face among them, the last face's line, and
// how its faces are written.
struct BrushPieceText {
	QStringList lines;
	QVector<int> faceLines;
	int lastFace = -1;
	QString style;
};

enum class BrushPieceProblem {
	None,
	// A face shares its line with another face or with a brace.
	SharedLines,
	// The owning entity closes on a line shared with other text, so pieces
	// cannot go in ahead of its closing brace.
	EntityShared,
};

BrushPieceProblem brushPieceText(LevelMapDocument* document, const LevelMapBrush& brush, BrushPieceText* text)
{
	text->lines = brushTextNow(*document, brush);
	const QStringList& lines = text->lines;
	const int firstLine = brush.startLine > 0 ? brush.startLine : brush.sourceFirstLine;
	bool ownLines = (brush.startLine <= 0 || objectOwnsItsLines(*document, brush.startLine, brush.endLine)) && lines.size() >= 3
		&& lines.first().trimmed() == QStringLiteral("{") && lines.last().trimmed() == QStringLiteral("}");
	text->faceLines.clear();
	for (const LevelMapBrushFace& face : brush.faces) {
		const int line = face.line - firstLine;
		ownLines = ownLines && line > 0 && line < lines.size() - 1 && !text->faceLines.contains(line);
		text->faceLines << line;
	}
	text->lastFace = text->faceLines.isEmpty() ? -1 : *std::max_element(text->faceLines.cbegin(), text->faceLines.cend());
	if (!ownLines || text->lastFace < 0 || lines.at(text->lastFace).trimmed().endsWith(QLatin1Char('}'))) {
		return BrushPieceProblem::SharedLines;
	}
	if (!entityAcceptsCopies(document, brush.entityId)) {
		return BrushPieceProblem::EntityShared;
	}
	text->style = brushFaceStyle(brush, lines.at(text->faceLines.first()));
	return BrushPieceProblem::None;
}

// The brush with `added` as one more face: re-solved, the faces it leaves
// without an edge dropped, written in the brush's own format, and read back by
// the parser. Null when that is not a closed brush.
std::optional<LevelMapBrush> brushWithAddedFace(const LevelMapDocument& document, const LevelMapBrush& brush, const BrushPieceText& text,
	const NewBrushFace& added)
{
	LevelMapBrushFace plane;
	plane.p0 = added.p0;
	plane.p1 = added.p1;
	plane.p2 = added.p2;
	QVector<LevelMapBrushFace> faces = brush.faces;
	faces.push_back(plane);
	const MapBrushGeometry solved = solveBrushGeometry(faces);
	if (!solved.solved || solved.faces.size() != faces.size() || !solved.faces.last().isValid()) {
		return std::nullopt;
	}
	QStringList lines = text.lines;
	lines.insert(text.lastFace + 1, brushFaceLine(text.style, added));
	// Faces the new one leaves without an edge go, from the bottom up so the
	// line numbers still hold.
	QVector<int> drop;
	for (int face = 0; face < brush.faces.size(); ++face) {
		if (!solved.faces.at(face).isValid()) {
			drop << text.faceLines.at(face);
		}
	}
	std::sort(drop.begin(), drop.end(), std::greater<int>());
	for (const int line : drop) {
		lines.removeAt(line);
	}
	std::optional<LevelMapBrush> made = parsedNewBrush(document, lines);
	if (made) {
		made->entityId = brush.entityId;
	}
	return made;
}

// A new face on a solved face's plane, moved `shift` units along `outward`,
// its points wound so `outward` is its outward normal, and textured like
// `like`. Its points are the polygon's own corners, so they sit on the plane.
NewBrushFace faceOnPolygon(const MapFacePolygon& polygon, const LevelMapVec3& outward, const LevelMapBrushFace& like, double shift = 0.0)
{
	const auto moved = [&outward, shift](const LevelMapVec3& point) {
		return LevelMapVec3 {point.x + outward.x * shift, point.y + outward.y * shift, point.z + outward.z * shift, true};
	};
	LevelMapVec3 q0 = moved(polygon.points.at(0));
	const LevelMapVec3 q1 = moved(polygon.points.at(1));
	LevelMapVec3 q2 = moved(polygon.points.at(2));
	// (q0 - q1) x (q2 - q1) has to point along `outward`.
	const LevelMapVec3 u {q0.x - q1.x, q0.y - q1.y, q0.z - q1.z, true};
	const LevelMapVec3 v {q2.x - q1.x, q2.y - q1.y, q2.z - q1.z, true};
	const LevelMapVec3 cross {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x, true};
	if (cross.x * outward.x + cross.y * outward.y + cross.z * outward.z < 0.0) {
		std::swap(q0, q2);
	}
	return NewBrushFace {q0, q1, q2, outward, like.textureName, like.contentFlags, like.surfaceFlags, like.surfaceValue};
}

// The first of the faces that use the brush's most used texture.
const LevelMapBrushFace& mostUsedFace(const LevelMapBrush& brush)
{
	QHash<QString, int> uses;
	for (const LevelMapBrushFace& face : brush.faces) {
		++uses[face.textureName];
	}
	const LevelMapBrushFace* like = &brush.faces.first();
	for (const LevelMapBrushFace& face : brush.faces) {
		if (uses.value(face.textureName) > uses.value(like->textureName)) {
			like = &face;
		}
	}
	return *like;
}

// The brushes a selection reaches: selected brushes and those of selected
// entities.
QSet<int> selectedBrushIds(const LevelMapDocument& document)
{
	QSet<int> entityIds;
	QSet<int> brushIds;
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::Entity) {
			entityIds.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushIds.insert(ref.objectId);
		}
	}
	for (const LevelMapBrush& brush : document.brushes) {
		if (entityIds.contains(brush.entityId)) {
			brushIds.insert(brush.id);
		}
	}
	return brushIds;
}

// Records, in a `replace-objects` command, that `brush` at `index` gives way to
// `pieces`.
void replaceBrushInCommand(const LevelMapDocument& document, LevelMapUndoCommand* command, const LevelMapBrush& brush, int index,
	const QVector<LevelMapBrush>& pieces)
{
	if (brush.startLine > 0) {
		command->deletedLineRanges.push_back({deletionFirstLine(document, brush.startLine), brush.endLine});
	}
	LevelMapBrush snapshot = brush;
	snapshot.selected = false;
	command->brushSnapshots.push_back(snapshot);
	command->brushIndexes.push_back(index);
	command->brushResults += pieces;
	for (const auto& piece : pieces) { command->sceneObjectOrigins.insert(brushObjectId(piece.id), brushObjectId(brush.id)); }
}

// Finishes a `replace-objects` command: the replaced brushes' issues, where the
// pieces go, and then applies it, records it, and selects the pieces unless
// told to leave the selection alone.
bool commitBrushReplacement(LevelMapDocument* document, LevelMapUndoCommand* command, bool selectPieces = true)
{
	QSet<QString> objectIds;
	for (const LevelMapBrush& brush : command->brushSnapshots) {
		objectIds.insert(brushObjectId(brush.id));
	}
	for (const LevelMapEntity& entity : command->entitySnapshots) {
		objectIds.insert(entityObjectId(entity.id));
	}
	for (int index = 0; index < document->issues.size(); ++index) {
		if (objectIds.contains(document->issues.at(index).objectId)) {
			command->issueSnapshots.push_back(document->issues.at(index));
			command->issueIndexes.push_back(index);
		}
	}
	// The pieces go after the brushes that stay, in the order they were made.
	const int remaining = static_cast<int>(document->brushes.size() - command->brushSnapshots.size());
	for (int index = 0; index < command->brushResults.size(); ++index) {
		command->brushResultIndexes.push_back(remaining + index);
	}
	if (!applyLevelMapCommand(document, *command, true)) {
		return false;
	}
	pushUndo(document, *command);
	if (!selectPieces) {
		return true;
	}
	QVector<LevelMapSelectionRef> pieces;
	for (const LevelMapBrush& brush : command->brushResults) {
		pieces.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
	}
	setLevelMapSelection(document, pieces);
	return true;
}

} // namespace

bool prepareLevelBrushMerge(const LevelMapDocument& document, const LevelBrushMergeRequest& request, LevelBrushMergePlan* plan,
	QString* error, const std::function<bool()>& cancelled)
{
	if (error) { error->clear(); }
	if (plan) { *plan = {}; }
	const auto failBrushMerge = [error](const char* message) {
		if (error) { *error = QCoreApplication::translate("LevelMerge", message); }
		return false;
	};
	if (!plan || !isTextMapFormat(document.format)) {
		return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "Open a Quake-family map to merge brushes."));
	}
	for (const auto& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			if (brushById(&document, ref.objectId)) { continue; }
		} else if (ref.kind == LevelMapSelectionKind::Entity && entityById(&document, ref.objectId)) {
			const bool hasBrush = std::any_of(document.brushes.cbegin(), document.brushes.cend(), [&](const auto& b) { return b.entityId == ref.objectId; });
			const bool hasPatch = std::any_of(document.patches.cbegin(), document.patches.cend(), [&](const auto& p) { return p.entityId == ref.objectId; });
			if (hasBrush && !hasPatch) { continue; }
		}
		return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "Select only brushes, or brush entities without patches, for this merge."));
	}
	const auto ids = selectedBrushIds(document);
	if (ids.size() < 2 || ids.size() > 64) {
		return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "Select between 2 and 64 brushes to merge."));
	}
	LevelBrushMergePlan draft;
	for (const auto& brush : document.brushes) {
		if (ids.contains(brush.id)) { draft.m_before << brush; }
	}
	std::sort(draft.m_before.begin(), draft.m_before.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
	QVector<LevelMapBrush> canonical;
	QMap<int, QStringList> faceLines;
	QStringList comments;
	for (const auto& brush : draft.m_before) {
		if (cancelled && cancelled()) { return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "Brush merge cancelled.")); }
		if (!objectOwnsItsLines(document, brush.startLine, brush.endLine) || !entityAcceptsCopies(&document, brush.entityId)) {
			return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "Put the brushes' and their entity's braces on separate lines before merging."));
		}
		const auto source = brushTextNow(document, brush);
		draft.m_sources << source;
		// Pending transforms may have more precision than the map writer. Merge
		// the exact planes/mappings that these brushes would persist on Save.
		auto current = parsedNewBrush(document, source);
		if (!current || current->faces.size() != brush.faces.size()) {
			return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "A source brush could not round-trip its complete face definitions."));
		}
		current->id = brush.id; current->entityId = brush.entityId;
		canonical << *current;
		const int first = brush.startLine > 0 ? brush.startLine : brush.sourceFirstLine;
		QMap<int,int> faceAt;
		for (int f = 0; f < brush.faces.size(); ++f) {
			const int line = brush.faces[f].line - first + 1;
			if (line <= 1 || line >= source.size() || faceAt.contains(line)) {
				return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "Put each complete brush face on a separate line before merging."));
			}
			faceAt.insert(line, f);
		}
		if (brush.startLine > 0 && deletionFirstLine(document, brush.startLine) < brush.startLine) {
			comments << document.textLines[brush.startLine - 2];
		}
		const auto tokens = tokenizeMapText(source.join('\n'), &comments);
		QStringList wrapper, lines;
		lines.resize(brush.faces.size());
		for (const auto& token : tokens) {
			const QString text = token.type == MapTokenType::String ? '"' + token.text + '"' : token.text;
			if (faceAt.contains(token.line)) { lines[faceAt[token.line]] += text + ' '; }
			else { wrapper << text; }
		}
		QStringList expected{QStringLiteral("{")};
		if (brush.primitiveKind.startsWith(QStringLiteral("brushDef"))) { expected << brush.primitiveKind << QStringLiteral("{") << QStringLiteral("}"); }
		expected << QStringLiteral("}");
		if (wrapper != expected) {
			return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "This brush has multiline faces or unsupported header fields. Put complete faces on separate lines before merging."));
		}
		faceLines.insert(brush.id, lines);
	}
	if (!solveLevelBrushMerge(canonical, request, &draft.m_geometry, error, cancelled)) { return false; }
	QStringList lines{QStringLiteral("{")};
	lines += comments; // Keep every source comment once, including removed internal faces.
	const auto kind = draft.m_geometry.brush.primitiveKind;
	if (kind.startsWith(QStringLiteral("brushDef"))) { lines << kind << QStringLiteral("{"); }
	for (const auto& face : draft.m_geometry.faces) { lines << faceLines[face.chosen.brushId][face.chosen.faceIndex].trimmed(); }
	if (kind.startsWith(QStringLiteral("brushDef"))) { lines << QStringLiteral("}"); }
	lines << QStringLiteral("}");
	auto parsed = parsedNewBrush(document, lines);
	LevelBrushTopology topology;
	if (!parsed || parsed->faces.size() != draft.m_geometry.faces.size() || !levelBrushTopology(*parsed, &topology, error)) {
		return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "The merged brush could not preserve its complete face definitions. Review the source layout."));
	}
	for (int f = 0; f < parsed->faces.size(); ++f) {
		const auto& before = draft.m_geometry.brush.faces[f];
		const auto& after = parsed->faces[f];
		const auto a = planeFromPoints(before.p0, before.p1, before.p2), b = topology.geometry.faces[f].plane;
		const auto uvA = levelTextureProjection(before), uvB = levelTextureProjection(after);
		if (std::abs(a.normalX-b.normalX)>1e-9 || std::abs(a.normalY-b.normalY)>1e-9 || std::abs(a.normalZ-b.normalZ)>1e-9
			|| std::abs(a.distance-b.distance)>1e-7 || before.textureName != after.textureName || before.contentFlags != after.contentFlags
			|| before.surfaceFlags != after.surfaceFlags || before.surfaceValue != after.surfaceValue || !uvA.valid || !uvB.valid
			|| uvA.normalizedCoordinates != uvB.normalizedCoordinates
			|| std::any_of(topology.geometry.faces[f].points.cbegin(), topology.geometry.faces[f].points.cend(), [&](const auto& p) {
				const auto d = uvA.at(p)-uvB.at(p); return std::abs(d.x())>1e-6 || std::abs(d.y())>1e-6;
			})) {
			return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "A merged face could not preserve its geometry, material, mapping or flags."));
		}
	}
	int id = 0;
	for (const auto& brush : document.brushes) {
		if (brush.id == std::numeric_limits<int>::max()) { return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "The map has exhausted its brush IDs.")); }
		id = std::max(id, brush.id + 1);
	}
	parsed->id = id; parsed->entityId = draft.m_before.first().entityId;
	draft.m_geometry.brush = *parsed;
	draft.m_canonical = std::move(canonical);
	draft.m_revision = document.revision; draft.m_sourcePath = document.sourcePath;
	draft.m_sourceHash = document.sourceContentHash; draft.m_format = document.format;
	draft.m_selection = document.selection; draft.m_prepared = true;
	*plan = std::move(draft);
	return true;
}

bool commitLevelBrushMerge(LevelMapDocument* document, const LevelBrushMergePlan& plan, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return commitLevelBrushMerge(candidate, plan, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) { error->clear(); }
	const auto failBrushMerge = [error](const char* message) {
		if (error) { *error = QCoreApplication::translate("LevelMerge", message); }
		return false;
	};
	if (!plan.ready()) { return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "Choose a source for each conflicting surface before merging.")); }
	if (!document || document->revision != plan.m_revision || document->sourcePath != plan.m_sourcePath
		|| document->sourceContentHash != plan.m_sourceHash || document->format != plan.m_format || document->selection != plan.m_selection
		|| brushById(document, plan.brush().id) || !entityAcceptsCopies(document, plan.brush().entityId)) {
		return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "The map or selection changed after the merge preview. Prepare the merge again."));
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("replace-objects");
	command.objectKind = QStringLiteral("brush");
	command.selectionSnapshot = document->selection;
	command.selectionResult = {{LevelMapSelectionKind::QuakeBrush, plan.brush().id}};
	for (int b = 0; b < plan.m_before.size(); ++b) {
		const auto& before = plan.m_before[b];
		const int index = indexOfObjectId(document->brushes, before.id);
		if (index < 0 || document->brushes[index].entityId != before.entityId || document->brushes[index].startLine != before.startLine
			|| document->brushes[index].endLine != before.endLine || brushTextNow(*document, document->brushes[index]) != plan.m_sources[b]) {
			return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "A source brush changed after the merge preview. Prepare the merge again."));
		}
		replaceBrushInCommand(*document, &command, document->brushes[index], index, {});
	}
	command.brushResults << plan.brush();
	// A merge retains the first source's scene membership. Other memberships
	// deliberately collapse with their geometry into this one destination.
	if (!plan.m_before.isEmpty()) { command.sceneObjectOrigins.insert(brushObjectId(plan.brush().id), brushObjectId(plan.m_before.first().id)); }
	command.description = QCoreApplication::translate("LevelMerge", "Merge %1 brushes").arg(plan.brushCount());
	command.undoDescription = QCoreApplication::translate("LevelMerge", "Restore merged brushes");
	if (!commitBrushReplacement(document, &command, false)) { return failBrushMerge(QT_TRANSLATE_NOOP("LevelMerge", "The brush merge could not be applied.")); }
	return true;
}

bool clipLevelMapSelection(LevelMapDocument* document, const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& c,
	LevelMapClipKeep keep, int* clipped, QString* error, QStringList* skipped)
{
	int sceneResult_clipped = 0;
	QStringList sceneResult_skipped = skipped ? *skipped : QStringList{};
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return clipLevelMapSelection(candidate, a, b, c, keep, clipped ? &sceneResult_clipped : nullptr, error, skipped ? &sceneResult_skipped : nullptr);
	}); guarded.has_value()) {
		if (*guarded && clipped) { *clipped = std::move(sceneResult_clipped); }
		if (*guarded && skipped) { *skipped = std::move(sceneResult_skipped); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (clipped) {
		*clipped = 0;
	}
	if (skipped) {
		skipped->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brushes can be clipped in Quake-family .map files only."));
	}
	// Wound (b, a, c), the plane's normal is (b - a) x (c - a): its front.
	const MapPlane cut = planeFromPoints(b, a, c);
	if (!cut.valid) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The clip plane needs three points that are not in a line."));
	}
	const QSet<int> brushIds = selectedBrushIds(*document);
	if (brushIds.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select brushes, or brush entities, to clip."));
	}

	// A brush only counts as cut when some of it lies this far on each side.
	constexpr double kSide = 0.01;
	const LevelMapVec3 front {cut.normalX, cut.normalY, cut.normalZ, true};
	const LevelMapVec3 back {-cut.normalX, -cut.normalY, -cut.normalZ, true};
	int nextId = 0;
	for (const LevelMapBrush& brush : document->brushes) {
		nextId = std::max(nextId, brush.id + 1);
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("replace-objects");
	command.objectKind = QStringLiteral("brush");
	QStringList refusals;
	for (int index = 0; index < document->brushes.size(); ++index) {
		const LevelMapBrush& brush = document->brushes.at(index);
		if (!brushIds.contains(brush.id)) {
			continue;
		}
		const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
		if (!geometry.solved) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 is not a closed brush, so it cannot be clipped.").arg(brush.id);
			continue;
		}
		double lowest = std::numeric_limits<double>::max();
		double highest = std::numeric_limits<double>::lowest();
		for (const MapFacePolygon& polygon : geometry.faces) {
			for (const LevelMapVec3& point : polygon.points) {
				const double side = planeDistanceToPoint(cut, point);
				lowest = std::min(lowest, side);
				highest = std::max(highest, side);
			}
		}
		if (highest <= kSide || lowest >= -kSide) {
			// The plane misses this brush.
			continue;
		}

		// The pieces are written from the brush's own text.
		BrushPieceText text;
		const BrushPieceProblem problem = brushPieceText(document, brush, &text);
		if (problem == BrushPieceProblem::SharedLines) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 writes its faces on lines shared with other text, so it cannot be clipped.").arg(brush.id);
			continue;
		}
		if (problem == BrushPieceProblem::EntityShared) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 closes on a line shared with other map text, so brush %2 cannot be clipped.")
					    .arg(brush.entityId)
					    .arg(brush.id);
			continue;
		}
		// The new face takes the texture and flags of the brush's most used face,
		// and faces out of the part kept: the front keeps the back.
		const LevelMapBrushFace& like = mostUsedFace(brush);
		QVector<LevelMapBrush> made;
		bool built = true;
		for (const bool keepBack : {true, false}) {
			if ((keepBack && keep == LevelMapClipKeep::Front) || (!keepBack && keep == LevelMapClipKeep::Back)) {
				continue;
			}
			const NewBrushFace added {keepBack ? b : c, a, keepBack ? c : b, keepBack ? front : back, like.textureName, like.contentFlags,
				like.surfaceFlags, like.surfaceValue};
			std::optional<LevelMapBrush> part = brushWithAddedFace(*document, brush, text, added);
			built = built && part.has_value();
			if (part) {
				part->id = nextId++;
				made << *part;
			}
		}
		if (!built) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 could not be cut into closed brushes.").arg(brush.id);
			continue;
		}
		replaceBrushInCommand(*document, &command, brush, index, made);
	}
	if (command.brushSnapshots.isEmpty()) {
		return fail(refusals.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "The clip plane does not pass through any selected brush.") : refusals.first());
	}
	const int count = static_cast<int>(command.brushSnapshots.size());
	const bool both = keep == LevelMapClipKeep::Both;
	if (count == 1) {
		const int id = command.brushSnapshots.first().id;
		command.objectId = id;
		command.description = both ? QCoreApplication::translate("VibeStudioLevelMap", "Split brush:%1 in two").arg(id) : QCoreApplication::translate("VibeStudioLevelMap", "Clip brush:%1").arg(id);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Put brush:%1 back whole").arg(id);
	} else {
		command.description = both ? QCoreApplication::translate("VibeStudioLevelMap", "Split %1 brushes in two").arg(count) : QCoreApplication::translate("VibeStudioLevelMap", "Clip %1 brushes").arg(count);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Put %1 brushes back whole").arg(count);
	}
	if (!commitBrushReplacement(document, &command)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The brushes to clip are no longer in the map."));
	}
	if (clipped) {
		*clipped = count;
	}
	if (skipped) {
		*skipped = refusals;
	}
	return true;
}

bool hollowLevelMapSelection(LevelMapDocument* document, double thickness, int* hollowed, QString* error, QStringList* skipped)
{
	int sceneResult_hollowed = 0;
	QStringList sceneResult_skipped = skipped ? *skipped : QStringList{};
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return hollowLevelMapSelection(candidate, thickness, hollowed ? &sceneResult_hollowed : nullptr, error, skipped ? &sceneResult_skipped : nullptr);
	}); guarded.has_value()) {
		if (*guarded && hollowed) { *hollowed = std::move(sceneResult_hollowed); }
		if (*guarded && skipped) { *skipped = std::move(sceneResult_skipped); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (hollowed) {
		*hollowed = 0;
	}
	if (skipped) {
		skipped->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brushes can be hollowed in Quake-family .map files only."));
	}
	if (!std::isfinite(thickness) || thickness <= 0.0) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The walls need a thickness above zero."));
	}
	const QSet<int> brushIds = selectedBrushIds(*document);
	if (brushIds.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select brushes, or brush entities, to hollow."));
	}
	int nextId = 0;
	for (const LevelMapBrush& brush : document->brushes) {
		nextId = std::max(nextId, brush.id + 1);
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("replace-objects");
	command.objectKind = QStringLiteral("brush");
	QStringList refusals;
	for (int index = 0; index < document->brushes.size(); ++index) {
		const LevelMapBrush& brush = document->brushes.at(index);
		if (!brushIds.contains(brush.id)) {
			continue;
		}
		const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
		if (!geometry.solved) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 is not a closed brush, so it cannot be hollowed.").arg(brush.id);
			continue;
		}
		// Two walls must fit across the brush along every face's normal.
		QVector<LevelMapVec3> corners;
		for (const MapFacePolygon& polygon : geometry.faces) {
			corners += polygon.points;
		}
		bool thickEnough = true;
		for (const MapFacePolygon& polygon : geometry.faces) {
			if (!polygon.isValid()) {
				continue;
			}
			double lowest = std::numeric_limits<double>::max();
			double highest = std::numeric_limits<double>::lowest();
			for (const LevelMapVec3& corner : corners) {
				const double along = planeDistanceToPoint(polygon.plane, corner);
				lowest = std::min(lowest, along);
				highest = std::max(highest, along);
			}
			thickEnough = thickEnough && highest - lowest > 2.0 * thickness;
		}
		if (!thickEnough) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 is too thin for two walls %2 units thick.").arg(brush.id).arg(mapCoordinateText(thickness));
			continue;
		}
		BrushPieceText text;
		const BrushPieceProblem problem = brushPieceText(document, brush, &text);
		if (problem == BrushPieceProblem::SharedLines) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 writes its faces on lines shared with other text, so it cannot be hollowed.").arg(brush.id);
			continue;
		}
		if (problem == BrushPieceProblem::EntityShared) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 closes on a line shared with other map text, so brush %2 cannot be hollowed.")
					    .arg(brush.entityId)
					    .arg(brush.id);
			continue;
		}
		// One wall per face: the face pulled in by the thickness, turned to face
		// the inside, and textured like the face it grew from.
		QVector<LevelMapBrush> walls;
		bool built = true;
		for (int face = 0; face < geometry.faces.size() && face < brush.faces.size() && built; ++face) {
			const MapFacePolygon& polygon = geometry.faces.at(face);
			if (!polygon.isValid()) {
				continue;
			}
			const LevelMapVec3 inward {-polygon.plane.normalX, -polygon.plane.normalY, -polygon.plane.normalZ, true};
			const NewBrushFace added = faceOnPolygon(polygon, inward, brush.faces.at(face), thickness);
			std::optional<LevelMapBrush> wall = brushWithAddedFace(*document, brush, text, added);
			built = wall.has_value();
			if (wall) {
				wall->id = nextId++;
				walls << *wall;
			}
		}
		if (!built || walls.isEmpty()) {
			refusals << QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 could not be made into closed walls.").arg(brush.id);
			continue;
		}
		replaceBrushInCommand(*document, &command, brush, index, walls);
	}
	if (command.brushSnapshots.isEmpty()) {
		return fail(refusals.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "Select brushes, or brush entities, to hollow.") : refusals.first());
	}
	const int count = static_cast<int>(command.brushSnapshots.size());
	if (count == 1) {
		const int id = command.brushSnapshots.first().id;
		command.objectId = id;
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Hollow brush:%1 into %2 walls").arg(id).arg(command.brushResults.size());
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Fill brush:%1 back in").arg(id);
	} else {
		command.description = QCoreApplication::translate("VibeStudioLevelMap", "Hollow %1 brushes into %2 walls").arg(count).arg(command.brushResults.size());
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Fill %1 brushes back in").arg(count);
	}
	if (!commitBrushReplacement(document, &command)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The brushes to hollow are no longer in the map."));
	}
	if (hollowed) {
		*hollowed = count;
	}
	if (skipped) {
		*skipped = refusals;
	}
	return true;
}

namespace {

struct CarvingBrush {
	const LevelMapBrush* brush = nullptr;
	MapBrushGeometry geometry;
};

// The corners of a brush as its faces solve them.
QVector<LevelMapVec3> brushCorners(const MapBrushGeometry& geometry)
{
	QVector<LevelMapVec3> corners;
	for (const MapFacePolygon& polygon : geometry.faces) {
		corners += polygon.points;
	}
	return corners;
}

// Cuts `piece` by one carving brush: split along each carving face in turn,
// keeping what lies outside it and going on with what lies inside, which goes
// once every face is used. `pieces` is the piece itself when the carver
// misses it. False when a split cannot be built, with the reason in `why`.
bool carveOneBrush(LevelMapDocument* document, const LevelMapBrush& piece, const CarvingBrush& carver, QVector<LevelMapBrush>* pieces,
	bool* changed, QString* why)
{
	constexpr double kSide = 0.01;
	*changed = false;
	const MapBrushGeometry own = solveBrushGeometry(piece.faces);
	if (!own.solved) {
		pieces->push_back(piece);
		return true;
	}
	// Any plane of either brush with the other wholly on its outer side keeps
	// them apart.
	const QVector<LevelMapVec3> ownCorners = brushCorners(own);
	const QVector<LevelMapVec3> carverCorners = brushCorners(carver.geometry);
	const auto outside = [](const MapFacePolygon& polygon, const QVector<LevelMapVec3>& corners) {
		return std::all_of(corners.cbegin(), corners.cend(), [&polygon](const LevelMapVec3& corner) {
			return planeDistanceToPoint(polygon.plane, corner) >= -kSide;
		});
	};
	for (const MapFacePolygon& polygon : carver.geometry.faces) {
		if (polygon.isValid() && outside(polygon, ownCorners)) {
			pieces->push_back(piece);
			return true;
		}
	}
	for (const MapFacePolygon& polygon : own.faces) {
		if (polygon.isValid() && outside(polygon, carverCorners)) {
			pieces->push_back(piece);
			return true;
		}
	}

	LevelMapBrush remaining = piece;
	QVector<LevelMapBrush> kept;
	for (int face = 0; face < carver.geometry.faces.size(); ++face) {
		const MapFacePolygon& polygon = carver.geometry.faces.at(face);
		if (!polygon.isValid()) {
			continue;
		}
		const MapBrushGeometry current = solveBrushGeometry(remaining.faces);
		double lowest = std::numeric_limits<double>::max();
		double highest = std::numeric_limits<double>::lowest();
		for (const LevelMapVec3& corner : brushCorners(current)) {
			const double side = planeDistanceToPoint(polygon.plane, corner);
			lowest = std::min(lowest, side);
			highest = std::max(highest, side);
		}
		if (lowest >= -kSide) {
			// What is left lies outside this face, so outside the carver: the
			// two only met along an edge. The brush stays whole, as Radiant's
			// subtract keeps it, rather than cut into pieces for nothing.
			pieces->push_back(piece);
			*changed = false;
			return true;
		}
		if (highest <= kSide) {
			continue;
		}
		BrushPieceText text;
		const BrushPieceProblem problem = brushPieceText(document, remaining, &text);
		if (problem == BrushPieceProblem::EntityShared) {
			*why = QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 closes on a line shared with other map text, so brush %2 cannot be carved.").arg(piece.entityId).arg(piece.id);
			return false;
		}
		if (problem != BrushPieceProblem::None) {
			*why = QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 writes its faces on lines shared with other text, so it cannot be carved.").arg(piece.id);
			return false;
		}
		const LevelMapBrushFace& like = carver.brush->faces.at(face);
		const LevelMapVec3 normal {polygon.plane.normalX, polygon.plane.normalY, polygon.plane.normalZ, true};
		const LevelMapVec3 reversed {-normal.x, -normal.y, -normal.z, true};
		// Outside the carving face the new face looks back at the carver; the
		// part inside is closed by the face as the carver has it.
		std::optional<LevelMapBrush> out = brushWithAddedFace(*document, remaining, text, faceOnPolygon(polygon, reversed, like));
		std::optional<LevelMapBrush> in = brushWithAddedFace(*document, remaining, text, faceOnPolygon(polygon, normal, like));
		if (!out || !in) {
			*why = QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 could not be carved into closed brushes.").arg(piece.id);
			return false;
		}
		kept.push_back(*out);
		remaining = *in;
		*changed = true;
	}
	// What is left lies inside every carving face: inside the carver, so it
	// goes, and a piece wholly inside goes altogether, lines and all.
	if (kept.isEmpty() && !objectOwnsItsLines(*document, piece.startLine, piece.endLine)) {
		*why = QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 shares a source line with other map text, so carving it away would damage the file.").arg(piece.id);
		return false;
	}
	*changed = true;
	*pieces += kept;
	return true;
}

} // namespace

bool carveLevelMapSelection(LevelMapDocument* document, int* carved, QString* error, QStringList* skipped, const QVector<int>& spared)
{
	int sceneResult_carved = 0;
	QStringList sceneResult_skipped = skipped ? *skipped : QStringList{};
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return carveLevelMapSelection(candidate, carved ? &sceneResult_carved : nullptr, error, skipped ? &sceneResult_skipped : nullptr, spared);
	}); guarded.has_value()) {
		if (*guarded && carved) { *carved = std::move(sceneResult_carved); }
		if (*guarded && skipped) { *skipped = std::move(sceneResult_skipped); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (carved) {
		*carved = 0;
	}
	if (skipped) {
		skipped->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || !isTextMapFormat(document->format)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brushes can be carved in Quake-family .map files only."));
	}
	const QSet<int> carverIds = selectedBrushIds(*document);
	if (carverIds.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select the brushes to carve with."));
	}
	QVector<CarvingBrush> carvers;
	for (const LevelMapBrush& brush : document->brushes) {
		if (!carverIds.contains(brush.id)) {
			continue;
		}
		CarvingBrush carver {&brush, solveBrushGeometry(brush.faces, brush.id, brush.entityId)};
		if (carver.geometry.solved) {
			carvers.push_back(carver);
		}
	}
	if (carvers.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The selected brushes are not closed brushes, so they cannot carve."));
	}
	int nextId = 0;
	for (const LevelMapBrush& brush : document->brushes) {
		nextId = std::max(nextId, brush.id + 1);
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("replace-objects");
	command.objectKind = QStringLiteral("brush");
	QStringList refusals;
	QSet<int> swallowed;
	for (int index = 0; index < document->brushes.size(); ++index) {
		const LevelMapBrush& target = document->brushes.at(index);
		if (carverIds.contains(target.id) || !target.boundsSolved || spared.contains(target.id)) {
			continue;
		}
		// Each carver cuts every piece the ones before it left.
		QVector<LevelMapBrush> pieces {target};
		bool changed = false;
		QString why;
		bool failed = false;
		for (const CarvingBrush& carver : carvers) {
			QVector<LevelMapBrush> next;
			for (const LevelMapBrush& piece : pieces) {
				bool cut = false;
				if (!carveOneBrush(document, piece, carver, &next, &cut, &why)) {
					failed = true;
					break;
				}
				changed = changed || cut;
			}
			if (failed) {
				break;
			}
			pieces = next;
		}
		if (failed) {
			refusals << why;
			continue;
		}
		if (!changed) {
			continue;
		}
		if (pieces.isEmpty()) {
			swallowed.insert(target.id);
		}
		for (LevelMapBrush& piece : pieces) {
			piece.id = nextId++;
			piece.entityId = target.entityId;
		}
		replaceBrushInCommand(*document, &command, target, index, pieces);
	}
	if (command.brushSnapshots.isEmpty()) {
		return fail(refusals.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "The selected brushes do not overlap any other brush.") : refusals.first());
	}
	// A brush entity whose every brush the carve took goes with them, as
	// Radiant removes it, unless it still holds patches or shares its lines.
	for (int index = 0; index < document->entities.size() && !swallowed.isEmpty(); ++index) {
		const LevelMapEntity& entity = document->entities.at(index);
		if (isWorldspawnEntity(entity) || !objectOwnsItsLines(*document, entity.startLine, entity.endLine)) {
			continue;
		}
		bool emptied = false;
		bool keeps = false;
		for (const LevelMapBrush& brush : document->brushes) {
			if (brush.entityId == entity.id) {
				emptied = emptied || swallowed.contains(brush.id);
				keeps = keeps || !swallowed.contains(brush.id);
			}
		}
		for (const LevelMapPatch& patch : document->patches) {
			keeps = keeps || patch.entityId == entity.id;
		}
		if (!emptied || keeps) {
			continue;
		}
		if (entity.startLine > 0) {
			command.deletedLineRanges.push_back({deletionFirstLine(*document, entity.startLine), entity.endLine});
		}
		LevelMapEntity snapshot = entity;
		snapshot.selected = false;
		command.entitySnapshots.push_back(snapshot);
		command.entityIndexes.push_back(index);
	}
	const int count = static_cast<int>(command.brushSnapshots.size());
	const QString with = carvers.size() == 1 ? QStringLiteral("brush:%1").arg(carvers.first().brush->id) : QString();
	if (count == 1) {
		command.objectId = command.brushSnapshots.first().id;
		command.description = with.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "Carve brush:%1 with %2 brushes").arg(command.objectId).arg(carvers.size())
						      : QCoreApplication::translate("VibeStudioLevelMap", "Carve brush:%1 with %2").arg(command.objectId).arg(with);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Put brush:%1 back whole").arg(command.objectId);
	} else {
		command.description = with.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMap", "Carve %1 brushes with %2 brushes").arg(count).arg(carvers.size())
						      : QCoreApplication::translate("VibeStudioLevelMap", "Carve %1 brushes with %2").arg(count).arg(with);
		command.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Put %1 carved brushes back").arg(count);
	}
	// The carving brushes stay selected, ready to be deleted or moved on.
	if (!commitBrushReplacement(document, &command, false)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The brushes to carve are no longer in the map."));
	}
	if (carved) {
		*carved = count;
	}
	if (skipped) {
		*skipped = refusals;
	}
	return true;
}

namespace {

// The linedefs a Doom topology edit acts on: the selected ones, on a map that
// can be written back. False, with the reason, otherwise.
bool selectedDoomLinedefs(const LevelMapDocument& document, QVector<int>* linedefIds, QString* why)
{
	if (document.format != LevelMapFormat::DoomWad) {
		*why = QCoreApplication::translate("VibeStudioLevelMap", "Linedefs belong to Doom and Hexen maps.");
		return false;
	}
	if (document.doomFormat == LevelMapDoomFormat::Udmf) {
		*why = QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet.");
		return false;
	}
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::DoomLinedef && !linedefIds->contains(ref.objectId)) {
			linedefIds->push_back(ref.objectId);
		}
	}
	if (linedefIds->isEmpty()) {
		*why = QCoreApplication::translate("VibeStudioLevelMap", "Select linedefs first.");
		return false;
	}
	return true;
}

} // namespace

bool splitLevelMapLinedefs(LevelMapDocument* document, int* split, QString* error)
{
	int sceneResult_split = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return splitLevelMapLinedefs(candidate, split ? &sceneResult_split : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && split) { *split = std::move(sceneResult_split); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (split) {
		*split = 0;
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	QVector<int> linedefIds;
	QString why;
	if (!selectedDoomLinedefs(*document, &linedefIds, &why)) {
		return fail(why);
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("doom-geometry");
	command.objectKind = QStringLiteral("linedef");
	// New records take the next positions, which the lumps use as their ids.
	int nextVertex = static_cast<int>(document->doomVertices.size());
	int nextLinedef = static_cast<int>(document->doomLinedefs.size());
	int nextSidedef = static_cast<int>(document->doomSidedefs.size());
	QVector<LevelMapSelectionRef> halves;
	for (const int linedefId : linedefIds) {
		const int index = indexOfObjectId(document->doomLinedefs, linedefId);
		if (index < 0) {
			return fail(selectionNotFoundText(LevelMapSelectionKind::DoomLinedef));
		}
		const LevelMapDoomLinedef& linedef = document->doomLinedefs.at(index);
		const int start = indexOfObjectId(document->doomVertices, linedef.startVertex);
		const int end = indexOfObjectId(document->doomVertices, linedef.endVertex);
		if (start < 0 || end < 0) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Linedef %1 refers to a vertex the map does not have.").arg(linedef.id));
		}
		const LevelMapDoomVertex& from = document->doomVertices.at(start);
		const LevelMapDoomVertex& to = document->doomVertices.at(end);
		LevelMapDoomVertex middle;
		middle.id = nextVertex++;
		middle.x = static_cast<double>(std::lround((from.x + to.x) * 0.5));
		middle.y = static_cast<double>(std::lround((from.y + to.y) * 0.5));
		if ((middle.x == from.x && middle.y == from.y) || (middle.x == to.x && middle.y == to.y)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Linedef %1 is too short to split.").arg(linedef.id));
		}
		LevelMapDoomLinedef first = linedef;
		first.endVertex = middle.id;
		first.selected = false;
		LevelMapDoomLinedef second = linedef;
		second.id = nextLinedef++;
		second.startVertex = middle.id;
		second.selected = false;
		// A front texture runs from the start vertex and a back one from the
		// end, so the second half's front and the first half's back start
		// further along it by the other half's length.
		const int firstLength = static_cast<int>(std::lround(std::hypot(middle.x - from.x, middle.y - from.y)));
		const int secondLength = static_cast<int>(std::lround(std::hypot(to.x - middle.x, to.y - middle.y)));
		// Each half gets its own sides, copied, so editing one later leaves the
		// other alone even where the source shared a sidedef between lines.
		for (int* side : {&second.frontSidedef, &second.backSidedef}) {
			const int sideIndex = *side >= 0 ? indexOfObjectId(document->doomSidedefs, *side) : -1;
			if (sideIndex < 0) {
				*side = -1;
				continue;
			}
			LevelMapDoomSidedef copy = document->doomSidedefs.at(sideIndex);
			copy.id = nextSidedef++;
			copy.selected = false;
			if (side == &second.frontSidedef) {
				copy.offsetX += firstLength;
			}
			command.sidedefResults.push_back(copy);
			*side = copy.id;
		}
		const int backIndex = first.backSidedef >= 0 ? indexOfObjectId(document->doomSidedefs, first.backSidedef) : -1;
		if (backIndex >= 0 && secondLength != 0) {
			// The first half's back changes in place when no other line uses
			// it, and is copied when one does, so that line keeps its offset.
			const LevelMapDoomSidedef& back = document->doomSidedefs.at(backIndex);
			int users = 0;
			for (const LevelMapDoomLinedef& other : document->doomLinedefs) {
				users += (other.frontSidedef == back.id ? 1 : 0) + (other.backSidedef == back.id ? 1 : 0);
			}
			LevelMapDoomSidedef shifted = back;
			shifted.selected = false;
			shifted.offsetX += secondLength;
			if (users > 1) {
				shifted.id = nextSidedef++;
				first.backSidedef = shifted.id;
			} else {
				command.sidedefSnapshots.push_back(back);
			}
			command.sidedefResults.push_back(shifted);
		}
		command.linedefSnapshots.push_back(linedef);
		command.linedefResults.push_back(first);
		command.linedefResults.push_back(second);
		command.vertexResults.push_back(middle);
		command.sceneObjectOrigins.insert(linedefObjectId(second.id), linedefObjectId(linedef.id));
		command.sceneObjectOrigins.insert(vertexObjectId(middle.id), linedefObjectId(linedef.id));
		halves.push_back({LevelMapSelectionKind::DoomLinedef, linedef.id});
		halves.push_back({LevelMapSelectionKind::DoomLinedef, second.id});
	}
	const int count = static_cast<int>(linedefIds.size());
	command.description = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Split linedef:%1").arg(linedefIds.first()) : QCoreApplication::translate("VibeStudioLevelMap", "Split %1 linedefs").arg(count);
	command.undoDescription = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Join linedef:%1 again").arg(linedefIds.first()) : QCoreApplication::translate("VibeStudioLevelMap", "Join %1 linedefs again").arg(count);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The linedefs to split are no longer in the map."));
	}
	pushUndo(document, command);
	setLevelMapSelection(document, halves);
	if (split) {
		*split = count;
	}
	return true;
}

bool flipLevelMapLinedefs(LevelMapDocument* document, int* flipped, QString* error)
{
	int sceneResult_flipped = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return flipLevelMapLinedefs(candidate, flipped ? &sceneResult_flipped : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && flipped) { *flipped = std::move(sceneResult_flipped); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (flipped) {
		*flipped = 0;
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	QVector<int> linedefIds;
	QString why;
	if (!selectedDoomLinedefs(*document, &linedefIds, &why)) {
		return fail(why);
	}
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("doom-geometry");
	command.objectKind = QStringLiteral("linedef");
	for (const int linedefId : linedefIds) {
		const int index = indexOfObjectId(document->doomLinedefs, linedefId);
		if (index < 0) {
			return fail(selectionNotFoundText(LevelMapSelectionKind::DoomLinedef));
		}
		const LevelMapDoomLinedef& linedef = document->doomLinedefs.at(index);
		LevelMapDoomLinedef turned = linedef;
		std::swap(turned.startVertex, turned.endVertex);
		if (turned.backSidedef >= 0) {
			std::swap(turned.frontSidedef, turned.backSidedef);
		}
		command.linedefSnapshots.push_back(linedef);
		command.linedefResults.push_back(turned);
	}
	const int count = static_cast<int>(linedefIds.size());
	command.objectId = count == 1 ? linedefIds.first() : -1;
	command.description = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Flip linedef:%1").arg(linedefIds.first()) : QCoreApplication::translate("VibeStudioLevelMap", "Flip %1 linedefs").arg(count);
	command.undoDescription = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Flip linedef:%1 back").arg(linedefIds.first()) : QCoreApplication::translate("VibeStudioLevelMap", "Flip %1 linedefs back").arg(count);
	if (!applyLevelMapCommand(document, command, true)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The linedefs to flip are no longer in the map."));
	}
	pushUndo(document, command);
	if (flipped) {
		*flipped = count;
	}
	return true;
}

namespace {

// Doom linedef flag bit 1 blocks players and monsters; every one-sided line
// has it. https://doomwiki.org/wiki/Linedef#Linedef_flags
constexpr int kLinedefImpassable = 0x0001;

// A working copy of a Doom map's geometry for an edit that adds, removes, or
// renumbers records. Nothing is taken out while the edit runs, so an index is
// an id throughout; records are marked gone instead, and
// compactDoomTopology() drops them, with what the edit left unused, and
// renumbers the rest so ids equal lump positions again.
struct DoomTopology {
	QVector<LevelMapDoomVertex> vertices;
	QVector<LevelMapDoomLinedef> linedefs;
	QVector<LevelMapDoomSidedef> sidedefs;
	QVector<LevelMapDoomSector> sectors;
	QVector<bool> vertexGone;
	QVector<bool> linedefGone;
	QVector<bool> sectorGone;
	// The map's linedef each working linedef is, or was cut from, for naming.
	QVector<int> linedefOrigin;
	QSet<int> thingsGone;

	// How many sides of live linedefs each sidedef is, kept as the edit goes
	// by addLinedef(), setSide(), and dropLinedef(), so a copy-on-write check
	// never has to walk every linedef.
	QVector<int> sideUses;

	explicit DoomTopology(const LevelMapDocument& document)
		: vertices(document.doomVertices)
		, linedefs(document.doomLinedefs)
		, sidedefs(document.doomSidedefs)
		, sectors(document.doomSectors)
	{
		vertexGone.fill(false, vertices.size());
		linedefGone.fill(false, linedefs.size());
		sectorGone.fill(false, sectors.size());
		sideUses.fill(0, sidedefs.size());
		for (int index = 0; index < linedefs.size(); ++index) {
			linedefOrigin.push_back(index);
			countSides(linedefs.at(index), 1);
		}
	}

	void countSides(const LevelMapDoomLinedef& linedef, int delta)
	{
		for (const int side : {linedef.frontSidedef, linedef.backSidedef}) {
			if (side >= 0 && side < sideUses.size()) {
				sideUses[side] += delta;
			}
		}
	}

	// Points a live linedef's side at another sidedef, or at none (-1).
	void setSide(int linedefId, bool front, int sidedefId)
	{
		LevelMapDoomLinedef& linedef = linedefs[linedefId];
		int& side = front ? linedef.frontSidedef : linedef.backSidedef;
		if (side == sidedefId) {
			return;
		}
		if (!linedefGone.at(linedefId)) {
			if (side >= 0 && side < sideUses.size()) {
				--sideUses[side];
			}
			if (sidedefId >= 0 && sidedefId < sideUses.size()) {
				++sideUses[sidedefId];
			}
		}
		side = sidedefId;
	}

	// Takes a linedef out of the edit's map; compaction drops it.
	void dropLinedef(int linedefId)
	{
		if (linedefId < 0 || linedefId >= linedefs.size() || linedefGone.at(linedefId)) {
			return;
		}
		linedefGone[linedefId] = true;
		countSides(linedefs.at(linedefId), -1);
	}

	[[nodiscard]] bool linedefLive(int id) const
	{
		return id >= 0 && id < linedefs.size() && !linedefGone.at(id);
	}

	[[nodiscard]] QPointF point(int vertexId) const
	{
		if (vertexId < 0 || vertexId >= vertices.size()) {
			return {};
		}
		return {vertices.at(vertexId).x, vertices.at(vertexId).y};
	}

	int addVertex(const QPointF& at)
	{
		LevelMapDoomVertex vertex;
		vertex.id = static_cast<int>(vertices.size());
		vertex.x = at.x();
		vertex.y = at.y();
		vertices.push_back(vertex);
		vertexGone.push_back(false);
		return vertex.id;
	}

	int addSidedef(LevelMapDoomSidedef sidedef)
	{
		sidedef.id = static_cast<int>(sidedefs.size());
		sidedef.selected = false;
		sidedefs.push_back(sidedef);
		sideUses.push_back(0);
		return sidedef.id;
	}

	int addLinedef(LevelMapDoomLinedef linedef, int origin)
	{
		linedef.id = static_cast<int>(linedefs.size());
		linedef.selected = false;
		linedefs.push_back(linedef);
		linedefGone.push_back(false);
		linedefOrigin.push_back(origin);
		countSides(linedef, 1);
		return linedef.id;
	}

	int addSector(LevelMapDoomSector sector)
	{
		sector.id = static_cast<int>(sectors.size());
		sector.selected = false;
		sectors.push_back(sector);
		sectorGone.push_back(false);
		return sector.id;
	}

	// How many sides of live linedefs a sidedef is.
	[[nodiscard]] int sidedefUsers(int sidedefId) const
	{
		return sideUses.value(sidedefId);
	}

	// One side of a linedef, ready to change without touching another line:
	// the sidedef itself when only this side uses it, else a copy. Null when
	// the linedef has no such side. The pointer lasts until the next add.
	LevelMapDoomSidedef* ownSide(int linedefId, bool front)
	{
		const int side = front ? linedefs.at(linedefId).frontSidedef : linedefs.at(linedefId).backSidedef;
		if (side < 0 || side >= sidedefs.size()) {
			return nullptr;
		}
		if (sidedefUsers(side) <= 1) {
			return &sidedefs[side];
		}
		const int copy = addSidedef(sidedefs.at(side));
		setSide(linedefId, front, copy);
		return &sidedefs[copy];
	}
};

// The old-to-new ids a topology edit leaves, -1 for records that went, and
// what it did to each kind of record, as the undo command keeps it.
struct DoomRenumbering {
	QVector<int> vertices;
	QVector<int> linedefs;
	QVector<int> sidedefs;
	QVector<int> sectors;
	LevelMapRecordDelta<LevelMapDoomVertex> vertexDelta;
	LevelMapRecordDelta<LevelMapDoomLinedef> linedefDelta;
	LevelMapRecordDelta<LevelMapDoomSidedef> sidedefDelta;
	LevelMapRecordDelta<LevelMapDoomSector> sectorDelta;
};

// Whether two Doom records say the same, their selection aside.
bool sameDoomRecord(const LevelMapDoomVertex& left, const LevelMapDoomVertex& right)
{
	return left.x == right.x && left.y == right.y;
}

bool sameDoomRecord(const LevelMapDoomLinedef& left, const LevelMapDoomLinedef& right)
{
	return left.startVertex == right.startVertex && left.endVertex == right.endVertex && left.flags == right.flags && left.special == right.special
		&& left.tag == right.tag && left.frontSidedef == right.frontSidedef && left.backSidedef == right.backSidedef && left.args == right.args;
}

bool sameDoomRecord(const LevelMapDoomSidedef& left, const LevelMapDoomSidedef& right)
{
	return left.sector == right.sector && left.offsetX == right.offsetX && left.offsetY == right.offsetY && left.upperTexture == right.upperTexture
		&& left.lowerTexture == right.lowerTexture && left.middleTexture == right.middleTexture;
}

bool sameDoomRecord(const LevelMapDoomSector& left, const LevelMapDoomSector& right)
{
	return left.floorHeight == right.floorHeight && left.ceilingHeight == right.ceilingHeight && left.floorTexture == right.floorTexture
		&& left.ceilingTexture == right.ceilingTexture && left.lightLevel == right.lightLevel && left.special == right.special && left.tag == right.tag;
}

// The records an edit changed and appended, the working copy against the map.
template <typename Record>
LevelMapRecordDelta<Record> changedDoomRecords(const QVector<Record>& before, const QVector<Record>& working)
{
	LevelMapRecordDelta<Record> delta;
	delta.originalSize = static_cast<int>(before.size());
	for (int index = 0; index < before.size() && index < working.size(); ++index) {
		if (!sameDoomRecord(before.at(index), working.at(index))) {
			Record was = before.at(index);
			Record now = working.at(index);
			was.selected = false;
			now.selected = false;
			delta.changedIndexes.push_back(index);
			delta.changedBefore.push_back(was);
			delta.changedAfter.push_back(now);
		}
	}
	for (int index = static_cast<int>(before.size()); index < working.size(); ++index) {
		Record added = working.at(index);
		added.selected = false;
		delta.appended.push_back(added);
	}
	return delta;
}

// Drops the records an edit marked gone and those it left unused, and
// renumbers the rest in order. A record the map did not use before the edit
// stays: tidying after other tools is not this edit's business.
DoomRenumbering compactDoomTopology(DoomTopology* topology, const LevelMapDocument& before)
{
	DoomRenumbering renumbering;
	renumbering.vertexDelta = changedDoomRecords(before.doomVertices, topology->vertices);
	renumbering.linedefDelta = changedDoomRecords(before.doomLinedefs, topology->linedefs);
	renumbering.sidedefDelta = changedDoomRecords(before.doomSidedefs, topology->sidedefs);
	renumbering.sectorDelta = changedDoomRecords(before.doomSectors, topology->sectors);
	const auto mark = [](QVector<bool>* flags, int index) {
		if (index >= 0 && index < flags->size()) {
			(*flags)[index] = true;
		}
	};
	QVector<bool> vertexUsedBefore(before.doomVertices.size(), false);
	QVector<bool> sidedefUsedBefore(before.doomSidedefs.size(), false);
	QVector<bool> sectorUsedBefore(before.doomSectors.size(), false);
	for (const LevelMapDoomLinedef& linedef : before.doomLinedefs) {
		mark(&vertexUsedBefore, linedef.startVertex);
		mark(&vertexUsedBefore, linedef.endVertex);
		mark(&sidedefUsedBefore, linedef.frontSidedef);
		mark(&sidedefUsedBefore, linedef.backSidedef);
	}
	for (const LevelMapDoomSidedef& sidedef : before.doomSidedefs) {
		mark(&sectorUsedBefore, sidedef.sector);
	}
	QVector<bool> vertexUsed(topology->vertices.size(), false);
	QVector<bool> sidedefUsed(topology->sidedefs.size(), false);
	for (int index = 0; index < topology->linedefs.size(); ++index) {
		if (topology->linedefGone.at(index)) {
			continue;
		}
		const LevelMapDoomLinedef& linedef = topology->linedefs.at(index);
		mark(&vertexUsed, linedef.startVertex);
		mark(&vertexUsed, linedef.endVertex);
		mark(&sidedefUsed, linedef.frontSidedef);
		mark(&sidedefUsed, linedef.backSidedef);
	}
	// A sidedef no line used before stays, as not this edit's business, unless
	// the sector it names is being deleted; the sectors kept sidedefs name
	// stay with them, so no sidedef is left naming a sector that is gone.
	QVector<bool> sidedefKeep(topology->sidedefs.size(), false);
	for (int index = 0; index < topology->sidedefs.size(); ++index) {
		const int sector = topology->sidedefs.at(index).sector;
		const bool sectorGone = sector >= 0 && sector < topology->sectorGone.size() && topology->sectorGone.at(sector);
		sidedefKeep[index] = sidedefUsed.at(index)
			|| (index < sidedefUsedBefore.size() && !sidedefUsedBefore.at(index) && !sectorGone);
	}
	QVector<bool> sectorUsed(topology->sectors.size(), false);
	for (int index = 0; index < topology->sidedefs.size(); ++index) {
		if (sidedefKeep.at(index)) {
			mark(&sectorUsed, topology->sidedefs.at(index).sector);
		}
	}
	const auto keep = [](bool gone, bool usedNow, int index, const QVector<bool>& usedBefore) {
		if (gone) {
			return false;
		}
		// New records, and ones in use before, stay only while used.
		return usedNow || (index < usedBefore.size() && !usedBefore.at(index));
	};
	const auto pack = [](auto* records, const auto& kept, QVector<int>* ids, auto* delta) {
		std::remove_reference_t<decltype(*records)> packed;
		for (int index = 0; index < records->size(); ++index) {
			if (!kept(index)) {
				ids->push_back(-1);
				auto gone = records->at(index);
				gone.selected = false;
				delta->removedIndexes.push_back(index);
				delta->removed.push_back(gone);
				continue;
			}
			ids->push_back(static_cast<int>(packed.size()));
			packed.push_back(records->at(index));
			packed.last().id = static_cast<int>(packed.size()) - 1;
		}
		*records = packed;
	};
	pack(&topology->vertices, [&](int index) {
		return keep(topology->vertexGone.at(index), vertexUsed.at(index), index, vertexUsedBefore);
	}, &renumbering.vertices, &renumbering.vertexDelta);
	pack(&topology->linedefs, [&](int index) {
		return !topology->linedefGone.at(index);
	}, &renumbering.linedefs, &renumbering.linedefDelta);
	pack(&topology->sidedefs, [&](int index) {
		return sidedefKeep.at(index);
	}, &renumbering.sidedefs, &renumbering.sidedefDelta);
	pack(&topology->sectors, [&](int index) {
		return keep(topology->sectorGone.at(index), sectorUsed.at(index), index, sectorUsedBefore);
	}, &renumbering.sectors, &renumbering.sectorDelta);
	const auto moved = [](const QVector<int>& ids, int id) {
		return id >= 0 && id < ids.size() ? ids.at(id) : -1;
	};
	for (LevelMapDoomLinedef& linedef : topology->linedefs) {
		linedef.startVertex = moved(renumbering.vertices, linedef.startVertex);
		linedef.endVertex = moved(renumbering.vertices, linedef.endVertex);
		linedef.frontSidedef = moved(renumbering.sidedefs, linedef.frontSidedef);
		linedef.backSidedef = moved(renumbering.sidedefs, linedef.backSidedef);
	}
	for (LevelMapDoomSidedef& sidedef : topology->sidedefs) {
		sidedef.sector = moved(renumbering.sectors, sidedef.sector);
	}
	topology->vertexGone.fill(false, topology->vertices.size());
	topology->linedefGone.fill(false, topology->linedefs.size());
	topology->sectorGone.fill(false, topology->sectors.size());
	return renumbering;
}

// Records a compacted topology edit as one `doom-topology` command and
// applies it: the whole geometry before and after, the issues carried to the
// renumbered ids (and dropped with their objects), and `select`, named by
// working ids, as the selection after.
bool recordDoomTopology(LevelMapDocument* document, const DoomTopology& topology, const DoomRenumbering& renumbering,
	const QVector<LevelMapSelectionRef>& select, const QString& objectKind, const QString& description, const QString& undoDescription,
	bool sectorFieldsOnly = false)
{
	const auto moved = [](const QVector<int>& ids, int id) {
		return id >= 0 && id < ids.size() ? ids.at(id) : -1;
	};
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("doom-topology");
	command.objectKind = objectKind;
	command.description = description;
	command.undoDescription = undoDescription;
	if (sectorFieldsOnly) {
		command.key = QStringLiteral("sector-fields");
	}
	command.vertexDelta = renumbering.vertexDelta;
	command.linedefDelta = renumbering.linedefDelta;
	command.sidedefDelta = renumbering.sidedefDelta;
	command.sectorDelta = renumbering.sectorDelta;
	const auto carryScene = [&](const QVector<int>& ids, int originalSize, const QString& kind) {
		for (int id = 0; id < ids.size(); ++id) {
			const auto before = QStringLiteral("%1:%2").arg(kind).arg(id);
			const auto after = ids[id] < 0 ? QString() : QStringLiteral("%1:%2").arg(kind).arg(ids[id]);
			if (id < originalSize) { command.sceneObjectRemap.insert(before, after); }
			else if (!after.isEmpty()) { command.sceneAddedObjects << after; }
		}
	};
	carryScene(renumbering.vertices, document->doomVertices.size(), QStringLiteral("vertex"));
	carryScene(renumbering.linedefs, document->doomLinedefs.size(), QStringLiteral("linedef"));
	carryScene(renumbering.sectors, document->doomSectors.size(), QStringLiteral("sector"));
	// Sides are not assignable scene members, but their identities are needed
	// when a lock compares shared Doom records across topology compaction.
	for (int id = 0; id < document->doomSidedefs.size(); ++id) {
		const int now = renumbering.sidedefs.value(id, -1);
		command.sceneObjectRemap.insert(QStringLiteral("sidedef:%1").arg(id), now < 0 ? QString() : QStringLiteral("sidedef:%1").arg(now));
	}
	for (int id = document->doomLinedefs.size(); id < topology.linedefOrigin.size(); ++id) {
		const int origin = topology.linedefOrigin[id], now = renumbering.linedefs.value(id, -1);
		if (origin >= 0 && now >= 0) { command.sceneObjectOrigins.insert(linedefObjectId(now), linedefObjectId(origin)); }
	}
	if (!topology.thingsGone.isEmpty()) {
		command.hasThingSnapshot = true;
		command.thingSnapshots = document->doomThings;
		command.entitySnapshots = document->entities;
		for (const LevelMapDoomThing& thing : document->doomThings) {
			if (!topology.thingsGone.contains(thing.id)) {
				command.thingResults.push_back(thing);
			}
		}
		for (const LevelMapEntity& entity : document->entities) {
			if (!topology.thingsGone.contains(entity.id)) {
				command.entityResults.push_back(entity);
			}
		}
	}
	command.issueSnapshots = document->issues;
	for (const LevelMapIssue& issue : document->issues) {
		LevelMapIssue carried = issue;
		const qsizetype colon = issue.objectId.indexOf(QLatin1Char(':'));
		bool numbered = false;
		const int id = colon > 0 ? issue.objectId.mid(colon + 1).toInt(&numbered) : -1;
		if (numbered) {
			const QString kind = issue.objectId.left(colon);
			int now = id;
			if (kind == QStringLiteral("vertex")) {
				now = moved(renumbering.vertices, id);
			} else if (kind == QStringLiteral("linedef")) {
				now = moved(renumbering.linedefs, id);
			} else if (kind == QStringLiteral("sidedef")) {
				now = moved(renumbering.sidedefs, id);
			} else if (kind == QStringLiteral("sector")) {
				now = moved(renumbering.sectors, id);
			} else if ((kind == QStringLiteral("thing") || kind == QStringLiteral("entity")) && topology.thingsGone.contains(id)) {
				now = -1;
			}
			if (now < 0) {
				continue;
			}
			carried.objectId = QStringLiteral("%1:%2").arg(kind).arg(now);
		}
		command.issueResults.push_back(carried);
	}
	command.selectionSnapshot = document->selection;
	for (const LevelMapSelectionRef& ref : select) {
		int id = ref.objectId;
		if (ref.kind == LevelMapSelectionKind::DoomVertex) {
			id = moved(renumbering.vertices, id);
		} else if (ref.kind == LevelMapSelectionKind::DoomLinedef) {
			id = moved(renumbering.linedefs, id);
		} else if (ref.kind == LevelMapSelectionKind::DoomSector) {
			id = moved(renumbering.sectors, id);
		}
		if (id >= 0) {
			command.selectionResult.push_back({ref.kind, id});
		}
	}
	if (!applyLevelMapCommand(document, command, true)) {
		return false;
	}
	pushUndo(document, command);
	return true;
}

// The wall texture a map uses most, for walls an edit has to make up, or a
// stock one of the map's format when it has none.
QString defaultDoomWallTexture(const QVector<LevelMapDoomSidedef>& sidedefs, LevelMapDoomFormat format)
{
	QHash<QString, int> uses;
	QString best;
	for (const LevelMapDoomSidedef& sidedef : sidedefs) {
		if (isEmptyTextureName(sidedef.middleTexture)) {
			continue;
		}
		const int count = ++uses[sidedef.middleTexture];
		if (count > uses.value(best)) {
			best = sidedef.middleTexture;
		}
	}
	if (!best.isEmpty()) {
		return best;
	}
	return format == LevelMapDoomFormat::Hexen ? QStringLiteral("FOREST01") : QStringLiteral("STARTAN3");
}

// A sector for a map that has none to copy: the floor and ceiling flats it
// uses most, 128 units high.
LevelMapDoomSector defaultDoomSector(const QVector<LevelMapDoomSector>& sectors)
{
	LevelMapDoomSector sector;
	sector.floorHeight = 0;
	sector.ceilingHeight = 128;
	sector.lightLevel = 160;
	sector.floorTexture = QStringLiteral("FLOOR4_8");
	sector.ceilingTexture = QStringLiteral("CEIL3_5");
	QHash<QString, int> floors;
	QHash<QString, int> ceilings;
	for (const LevelMapDoomSector& other : sectors) {
		if (!other.floorTexture.isEmpty() && ++floors[other.floorTexture] > floors.value(sector.floorTexture)) {
			sector.floorTexture = other.floorTexture;
		}
		if (!other.ceilingTexture.isEmpty() && ++ceilings[other.ceilingTexture] > ceilings.value(sector.ceilingTexture)) {
			sector.ceilingTexture = other.ceilingTexture;
		}
	}
	return sector;
}

// Turns a wall into a two-sided line: the flags, and each side's middle
// texture moved to its upper and lower, where a two-sided line shows walls,
// so nothing is drawn across the opening.
void openDoomLinedef(DoomTopology* topology, int linedefId)
{
	LevelMapDoomLinedef& linedef = topology->linedefs[linedefId];
	linedef.flags = (linedef.flags | kLinedefTwoSided) & ~kLinedefImpassable;
	for (const bool front : {true, false}) {
		LevelMapDoomSidedef* side = topology->ownSide(linedefId, front);
		if (!side || isEmptyTextureName(side->middleTexture)) {
			continue;
		}
		if (isEmptyTextureName(side->upperTexture)) {
			side->upperTexture = side->middleTexture;
		}
		if (isEmptyTextureName(side->lowerTexture)) {
			side->lowerTexture = side->middleTexture;
		}
		side->middleTexture = QStringLiteral("-");
	}
}

// A line left with one side: turned, if need be, so that side is its front,
// flagged one-sided and impassable, and given a middle texture from its
// upper or lower so the wall is not left blank.
void closeDoomLinedef(DoomTopology* topology, int linedefId, const QString& wall)
{
	LevelMapDoomLinedef& linedef = topology->linedefs[linedefId];
	if (linedef.frontSidedef < 0) {
		std::swap(linedef.startVertex, linedef.endVertex);
		std::swap(linedef.frontSidedef, linedef.backSidedef);
	}
	topology->setSide(linedefId, false, -1);
	linedef.flags = (linedef.flags | kLinedefImpassable) & ~kLinedefTwoSided;
	LevelMapDoomSidedef* front = topology->ownSide(linedefId, true);
	if (!front || !isEmptyTextureName(front->middleTexture)) {
		return;
	}
	if (!isEmptyTextureName(front->upperTexture)) {
		front->middleTexture = front->upperTexture;
	} else if (!isEmptyTextureName(front->lowerTexture)) {
		front->middleTexture = front->lowerTexture;
	} else {
		front->middleTexture = wall;
	}
}

// Cuts a linedef at a vertex on it, as Split Linedefs does: the linedef ends
// there and a new one, with the same flags and special and its own copies of
// the sides, goes on to the old end, the offsets carried on. Returns the new
// linedef.
int splitDoomLinedefAt(DoomTopology* topology, int linedefId, int vertexId)
{
	LevelMapDoomLinedef second = topology->linedefs.at(linedefId);
	const QPointF from = topology->point(second.startVertex);
	const QPointF to = topology->point(second.endVertex);
	const QPointF middle = topology->point(vertexId);
	const int firstLength = static_cast<int>(std::lround(std::hypot(middle.x() - from.x(), middle.y() - from.y())));
	const int secondLength = static_cast<int>(std::lround(std::hypot(to.x() - middle.x(), to.y() - middle.y())));
	second.startVertex = vertexId;
	for (const bool front : {true, false}) {
		int& side = front ? second.frontSidedef : second.backSidedef;
		if (side < 0 || side >= topology->sidedefs.size()) {
			side = -1;
			continue;
		}
		LevelMapDoomSidedef copy = topology->sidedefs.at(side);
		if (front) {
			copy.offsetX += firstLength;
		}
		side = topology->addSidedef(copy);
	}
	topology->linedefs[linedefId].endVertex = vertexId;
	if (LevelMapDoomSidedef* back = topology->ownSide(linedefId, false)) {
		back->offsetX += secondLength;
	}
	return topology->addLinedef(second, topology->linedefOrigin.at(linedefId));
}

// Which side of the line through a and b a point is on: positive to the
// left, negative to the right, zero on it. Exact for whole-unit points.
double sideOfLine(const QPointF& a, const QPointF& b, const QPointF& point)
{
	return (b.x() - a.x()) * (point.y() - a.y()) - (b.y() - a.y()) * (point.x() - a.x());
}

// Where a point lies along a segment, as a fraction, when it is on the
// segment strictly between its ends; negative otherwise.
double fractionAlongSegment(const QPointF& a, const QPointF& b, const QPointF& point)
{
	const QPointF direction = b - a;
	const double lengthSquared = QPointF::dotProduct(direction, direction);
	if (lengthSquared <= 0.0 || std::abs(sideOfLine(a, b, point)) > 1.0e-6 * std::sqrt(lengthSquared)) {
		return -1.0;
	}
	const double along = QPointF::dotProduct(point - a, direction) / lengthSquared;
	return along > 1.0e-9 && along < 1.0 - 1.0e-9 ? along : -1.0;
}

// Whether two segments cross at a point inside both or overlap along a
// stretch; touching only where one ends does not count.
bool segmentsCross(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d)
{
	const double abc = sideOfLine(a, b, c);
	const double abd = sideOfLine(a, b, d);
	const double cda = sideOfLine(c, d, a);
	const double cdb = sideOfLine(c, d, b);
	if (abc == 0.0 && abd == 0.0) {
		const QPointF direction = b - a;
		const double lengthSquared = QPointF::dotProduct(direction, direction);
		if (lengthSquared <= 0.0) {
			return false;
		}
		double first = QPointF::dotProduct(c - a, direction) / lengthSquared;
		double last = QPointF::dotProduct(d - a, direction) / lengthSquared;
		if (first > last) {
			std::swap(first, last);
		}
		return std::min(1.0, last) - std::max(0.0, first) > 1.0e-9;
	}
	return ((abc > 0.0 && abd < 0.0) || (abc < 0.0 && abd > 0.0)) && ((cda > 0.0 && cdb < 0.0) || (cda < 0.0 && cdb > 0.0));
}

// Whether two segments share any point at all, their ends included.
bool segmentsMeet(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d)
{
	const double abc = sideOfLine(a, b, c);
	const double abd = sideOfLine(a, b, d);
	const double cda = sideOfLine(c, d, a);
	const double cdb = sideOfLine(c, d, b);
	if (abc == 0.0 && abd == 0.0) {
		const QPointF direction = b - a;
		const double lengthSquared = QPointF::dotProduct(direction, direction);
		double first = QPointF::dotProduct(c - a, direction) / lengthSquared;
		double last = QPointF::dotProduct(d - a, direction) / lengthSquared;
		if (first > last) {
			std::swap(first, last);
		}
		return std::min(1.0, last) >= std::max(0.0, first);
	}
	return ((abc <= 0.0 && abd >= 0.0) || (abc >= 0.0 && abd <= 0.0)) && ((cda <= 0.0 && cdb >= 0.0) || (cda >= 0.0 && cdb <= 0.0));
}

double distanceToSegment(const QPointF& point, const QPointF& a, const QPointF& b)
{
	const QPointF direction = b - a;
	const double lengthSquared = QPointF::dotProduct(direction, direction);
	const double along = lengthSquared > 0.0 ? std::clamp(QPointF::dotProduct(point - a, direction) / lengthSquared, 0.0, 1.0) : 0.0;
	const QPointF nearest = a + direction * along;
	return std::hypot(point.x() - nearest.x(), point.y() - nearest.y());
}

// How many sector outline loops hold a point, and the sector holding it (the
// smallest where outlines nest badly), -1 in the void.
int doomSectorAtPoint(const QVector<DoomSectorOutline>& outlines, const QPointF& point, int* depth = nullptr)
{
	int found = -1;
	double foundArea = 0.0;
	int loops = 0;
	for (const DoomSectorOutline& outline : outlines) {
		if (!outline.bounds.contains(point)) {
			continue;
		}
		bool inside = false;
		for (const QPolygonF& loop : outline.loops) {
			if (loop.containsPoint(point, Qt::OddEvenFill)) {
				inside = !inside;
				++loops;
			}
		}
		const double area = outline.bounds.width() * outline.bounds.height();
		if (inside && (found < 0 || area < foundArea)) {
			found = outline.sectorId;
			foundArea = area;
		}
	}
	if (depth) {
		*depth = loops;
	}
	return found;
}

// Joins linedefs left between the same two vertices into one, the way Doom
// Builder stitches lines drawn over each other: two one-sided lines facing
// apart become one two-sided line; otherwise the first stays and takes a
// missing back from the second.
void joinDuplicateDoomLinedefs(DoomTopology* topology, int atVertex)
{
	QHash<quint64, int> byEnds;
	for (int index = 0; index < topology->linedefs.size(); ++index) {
		if (topology->linedefGone.at(index)) {
			continue;
		}
		const LevelMapDoomLinedef linedef = topology->linedefs.at(index);
		// Only lines at the vertex an edit moved can have come to lie over each
		// other; the rest of the map is left as it is.
		if (linedef.startVertex != atVertex && linedef.endVertex != atVertex) {
			continue;
		}
		const quint64 key = (static_cast<quint64>(static_cast<quint32>(std::min(linedef.startVertex, linedef.endVertex))) << 32)
			| static_cast<quint32>(std::max(linedef.startVertex, linedef.endVertex));
		const auto found = byEnds.constFind(key);
		if (found == byEnds.cend()) {
			byEnds.insert(key, index);
			continue;
		}
		const int kept = found.value();
		LevelMapDoomLinedef& first = topology->linedefs[kept];
		const bool facingApart = first.startVertex == linedef.endVertex;
		if (first.backSidedef < 0) {
			const int taken = facingApart ? linedef.frontSidedef : linedef.backSidedef;
			if (taken >= 0) {
				topology->dropLinedef(index);
				topology->setSide(kept, false, taken);
				openDoomLinedef(topology, kept);
				continue;
			}
		}
		topology->dropLinedef(index);
	}
}

} // namespace

bool drawLevelMapDoomSector(LevelMapDocument* document, const QVector<LevelMapVec3>& corners, int* sectorId, QString* error, int* newLinedefs)
{
	int sceneResult_sectorId = -1;
	int sceneResult_newLinedefs = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return drawLevelMapDoomSector(candidate, corners, sectorId ? &sceneResult_sectorId : nullptr, error, newLinedefs ? &sceneResult_newLinedefs : nullptr);
	}); guarded.has_value()) {
		if (*guarded && sectorId) { *sectorId = std::move(sceneResult_sectorId); }
		if (*guarded && newLinedefs) { *newLinedefs = std::move(sceneResult_newLinedefs); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (sectorId) {
		*sectorId = -1;
	}
	if (newLinedefs) {
		*newLinedefs = 0;
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (document->format != LevelMapFormat::DoomWad) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Sectors belong to Doom and Hexen maps."));
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	// Corners on whole units, without repeats; a shape closed on its first
	// corner is the same shape.
	// Vertices some linedef uses; the ones nothing uses, such as those a node
	// builder adds where it splits segs, are passed over.
	QVector<bool> usedVertex(document->doomVertices.size(), false);
	for (const LevelMapDoomLinedef& linedef : document->doomLinedefs) {
		for (const int end : {linedef.startVertex, linedef.endVertex}) {
			if (end >= 0 && end < usedVertex.size()) {
				usedVertex[end] = true;
			}
		}
	}
	QVector<QPointF> shape;
	for (const LevelMapVec3& corner : corners) {
		if (!corner.valid || !std::isfinite(corner.x) || !std::isfinite(corner.y)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "A corner has no position."));
		}
		// A corner on a used vertex keeps the vertex's own position, so it joins
		// it even off the whole-unit grid; the rest go on whole units.
		QPointF point(std::round(corner.x), std::round(corner.y));
		for (int index = 0; index < document->doomVertices.size(); ++index) {
			const LevelMapDoomVertex& vertex = document->doomVertices.at(index);
			if (usedVertex.at(index) && std::abs(vertex.x - corner.x) < 1.0e-6 && std::abs(vertex.y - corner.y) < 1.0e-6) {
				point = QPointF(vertex.x, vertex.y);
				break;
			}
		}
		if (point.x() < kDoomCoordinateMin || point.x() > kDoomCoordinateMax || point.y() < kDoomCoordinateMin || point.y() > kDoomCoordinateMax) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Corner %1, %2 is outside the map's coordinate range.").arg(point.x()).arg(point.y()));
		}
		if (shape.isEmpty() || shape.last() != point) {
			shape.push_back(point);
		}
	}
	if (shape.size() > 1 && shape.first() == shape.last()) {
		shape.removeLast();
	}
	const int count = static_cast<int>(shape.size());
	if (count < 3) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A sector needs three corners or more."));
	}
	for (int first = 0; first < count; ++first) {
		for (int second = first + 1; second < count; ++second) {
			if (shape.at(first) == shape.at(second)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "The shape passes %1, %2 twice.").arg(shape.at(first).x()).arg(shape.at(first).y()));
			}
			const QPointF a = shape.at(first);
			const QPointF b = shape.at((first + 1) % count);
			const QPointF c = shape.at(second);
			const QPointF d = shape.at((second + 1) % count);
			if (second == first + 1 || (first == 0 && second == count - 1)) {
				// Neighbours meet at one corner; they must not fold back along
				// each other from it.
				const QPointF shared = second == first + 1 ? b : a;
				const QPointF before = second == first + 1 ? a : c;
				const QPointF after = second == first + 1 ? d : b;
				if (sideOfLine(before, shared, after) == 0.0 && QPointF::dotProduct(before - shared, after - shared) > 0.0) {
					return fail(QCoreApplication::translate("VibeStudioLevelMap", "The shape doubles back on itself at %1, %2.").arg(shared.x()).arg(shared.y()));
				}
			} else if (segmentsMeet(a, b, c, d)) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "The shape crosses itself; draw its corners in order around it."));
			}
		}
	}
	double doubledArea = 0.0;
	for (int index = 0; index < count; ++index) {
		const QPointF& a = shape.at(index);
		const QPointF& b = shape.at((index + 1) % count);
		doubledArea += a.x() * b.y() - b.x() * a.y();
	}
	if (std::abs(doubledArea) < 1.0) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The shape has no area."));
	}
	// Doom puts a line's front on its right. Clockwise, with y up, keeps the
	// inside on the right of every edge.
	if (doubledArea > 0.0) {
		std::reverse(shape.begin(), shape.end());
	}

	DoomTopology topology(*document);
	const QString wall = defaultDoomWallTexture(document->doomSidedefs, document->doomFormat);
	// Each corner joins a vertex there, or splits a linedef it lies on, or
	// makes a vertex of its own.
	QVector<int> cornerVertices;
	for (const QPointF& point : shape) {
		int vertex = -1;
		for (int index = 0; index < topology.vertices.size() && vertex < 0; ++index) {
			if (!topology.vertexGone.at(index) && usedVertex.value(index) && topology.point(index) == point) {
				vertex = index;
			}
		}
		for (int index = 0; index < topology.linedefs.size() && vertex < 0; ++index) {
			if (!topology.linedefLive(index)) {
				continue;
			}
			const LevelMapDoomLinedef& linedef = topology.linedefs.at(index);
			if (fractionAlongSegment(topology.point(linedef.startVertex), topology.point(linedef.endVertex), point) > 0.0) {
				vertex = topology.addVertex(point);
				usedVertex.push_back(true);
				splitDoomLinedefAt(&topology, index, vertex);
			}
		}
		if (vertex < 0) {
			vertex = topology.addVertex(point);
			usedVertex.push_back(false);
		}
		cornerVertices.push_back(vertex);
	}
	// Each edge, broken at the vertices on it, shares a linedef that runs
	// between the same two vertices or becomes a new one, which may not cross
	// any other.
	struct Edge {
		int from = -1;
		int to = -1;
		int shared = -1;
		bool sameWay = true;
	};
	QVector<Edge> edges;
	for (int corner = 0; corner < count; ++corner) {
		const int from = cornerVertices.at(corner);
		const int to = cornerVertices.at((corner + 1) % count);
		QVector<QPair<double, int>> stops;
		for (int index = 0; index < topology.vertices.size(); ++index) {
			if (index == from || index == to || topology.vertexGone.at(index) || !usedVertex.value(index)) {
				continue;
			}
			const double along = fractionAlongSegment(topology.point(from), topology.point(to), topology.point(index));
			if (along > 0.0) {
				stops.push_back({along, index});
			}
		}
		std::sort(stops.begin(), stops.end());
		stops.push_back({1.0, to});
		int previous = from;
		for (const QPair<double, int>& stop : stops) {
			edges.push_back({previous, stop.second, -1, true});
			previous = stop.second;
		}
	}
	for (Edge& edge : edges) {
		for (int index = 0; index < topology.linedefs.size() && edge.shared < 0; ++index) {
			if (!topology.linedefLive(index)) {
				continue;
			}
			const LevelMapDoomLinedef& linedef = topology.linedefs.at(index);
			if (linedef.startVertex == edge.from && linedef.endVertex == edge.to) {
				edge.shared = index;
				edge.sameWay = true;
			} else if (linedef.startVertex == edge.to && linedef.endVertex == edge.from) {
				edge.shared = index;
				edge.sameWay = false;
			}
		}
		if (edge.shared >= 0) {
			continue;
		}
		for (int index = 0; index < topology.linedefs.size(); ++index) {
			if (!topology.linedefLive(index)) {
				continue;
			}
			const LevelMapDoomLinedef& linedef = topology.linedefs.at(index);
			if (segmentsCross(topology.point(edge.from), topology.point(edge.to), topology.point(linedef.startVertex), topology.point(linedef.endVertex))) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "The shape crosses linedef %1; draw along it, or split it where they meet.").arg(topology.linedefOrigin.at(index)));
			}
		}
	}

	const QVector<DoomSectorOutline> outlines = buildDoomSectorOutlines(*document);
	const auto justInside = [&topology](const Edge& edge) {
		const QPointF from = topology.point(edge.from);
		const QPointF to = topology.point(edge.to);
		const QPointF middle = (from + to) / 2.0;
		const double length = std::hypot(to.x() - from.x(), to.y() - from.y());
		// Right of the edge, with y up, and nearer it than any other line.
		const QPointF inward((to.y() - from.y()) / length, -(to.x() - from.x()) / length);
		double reach = 0.5;
		for (int index = 0; index < topology.linedefs.size(); ++index) {
			// The line a shared edge runs along is the edge itself.
			if (topology.linedefLive(index) && index != edge.shared) {
				const LevelMapDoomLinedef& linedef = topology.linedefs.at(index);
				reach = std::min(reach, 0.5 * distanceToSegment(middle, topology.point(linedef.startVertex), topology.point(linedef.endVertex)));
			}
		}
		return middle + inward * reach;
	};
	// The area the shape is drawn in, seen from inside every edge: the side a
	// shared line turns to it, or what lies just inside a new edge. Edges that
	// look into different areas mean a shape over more than one, which is
	// refused rather than drawn wrong.
	int enclosing = -2;
	int enclosingDepth = 0;
	const auto areaName = [](int area) {
		return area >= 0 ? QCoreApplication::translate("VibeStudioLevelMap", "sector %1").arg(area) : QCoreApplication::translate("VibeStudioLevelMap", "the void");
	};
	for (const Edge& edge : edges) {
		int area = -1;
		int depth = 0;
		if (edge.shared >= 0) {
			const LevelMapDoomLinedef& linedef = topology.linedefs.at(edge.shared);
			const int side = edge.sameWay ? linedef.frontSidedef : linedef.backSidedef;
			area = side >= 0 && side < topology.sidedefs.size() ? topology.sidedefs.at(side).sector : -1;
			if (area < 0) {
				doomSectorAtPoint(outlines, justInside(edge), &depth);
			}
		} else {
			area = doomSectorAtPoint(outlines, justInside(edge), &depth);
		}
		if (area >= 0) {
			depth = 0;
		}
		if (enclosing == -2) {
			enclosing = area;
			enclosingDepth = depth;
		} else if (area != enclosing || depth != enclosingDepth) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The shape covers more than one area, %1 and %2; draw it within one area at a time.")
					.arg(areaName(enclosing), areaName(area)));
		}
	}
	if (enclosing < -1 || enclosing >= topology.sectors.size()) {
		enclosing = -1;
	}

	// The new sector copies the one it was drawn in or, drawn in the void, the
	// first room it shares a line with, and failing both the map's usual
	// flats. Its tag and special stay its own to set.
	LevelMapDoomSector sector;
	if (enclosing >= 0) {
		sector = topology.sectors.at(enclosing);
	} else {
		int neighbour = -1;
		for (const Edge& edge : edges) {
			if (edge.shared >= 0 && neighbour < 0) {
				const LevelMapDoomLinedef& linedef = topology.linedefs.at(edge.shared);
				const int outer = edge.sameWay ? linedef.backSidedef : linedef.frontSidedef;
				if (outer >= 0 && outer < topology.sidedefs.size()) {
					neighbour = topology.sidedefs.at(outer).sector;
				}
			}
		}
		sector = neighbour >= 0 && neighbour < topology.sectors.size() ? topology.sectors.at(neighbour) : defaultDoomSector(topology.sectors);
	}
	sector.special = 0;
	sector.tag = 0;
	const int newSector = topology.addSector(sector);

	const int firstNewLinedef = static_cast<int>(topology.linedefs.size());
	QSet<int> boundary;
	int made = 0;
	for (const Edge& edge : edges) {
		if (edge.shared >= 0) {
			boundary.insert(edge.shared);
			if (LevelMapDoomSidedef* inner = topology.ownSide(edge.shared, edge.sameWay)) {
				inner->sector = newSector;
				continue;
			}
			// The line faced nothing on this side: it opens onto the new sector,
			// which takes the wall's texture above and below.
			const LevelMapDoomLinedef& linedef = topology.linedefs.at(edge.shared);
			const int other = edge.sameWay ? linedef.backSidedef : linedef.frontSidedef;
			const QString texture = other >= 0 && other < topology.sidedefs.size() && !isEmptyTextureName(topology.sidedefs.at(other).middleTexture)
				? topology.sidedefs.at(other).middleTexture
				: wall;
			LevelMapDoomSidedef side;
			side.sector = newSector;
			side.upperTexture = texture;
			side.lowerTexture = texture;
			side.middleTexture = QStringLiteral("-");
			const int sideId = topology.addSidedef(side);
			topology.setSide(edge.shared, edge.sameWay, sideId);
			if (topology.linedefs.at(edge.shared).frontSidedef >= 0 && topology.linedefs.at(edge.shared).backSidedef >= 0) {
				openDoomLinedef(&topology, edge.shared);
			} else {
				closeDoomLinedef(&topology, edge.shared, wall);
			}
			continue;
		}
		LevelMapDoomLinedef linedef;
		linedef.startVertex = edge.from;
		linedef.endVertex = edge.to;
		LevelMapDoomSidedef front;
		front.sector = newSector;
		if (enclosing >= 0) {
			// Inside a sector the line is an opening, walls above and below
			// ready for when the heights part.
			front.upperTexture = wall;
			front.lowerTexture = wall;
			front.middleTexture = QStringLiteral("-");
			LevelMapDoomSidedef back = front;
			back.sector = enclosing;
			linedef.frontSidedef = topology.addSidedef(front);
			linedef.backSidedef = topology.addSidedef(back);
			linedef.flags = kLinedefTwoSided;
		} else {
			front.upperTexture = QStringLiteral("-");
			front.lowerTexture = QStringLiteral("-");
			front.middleTexture = wall;
			linedef.frontSidedef = topology.addSidedef(front);
			linedef.flags = kLinedefImpassable;
		}
		topology.addLinedef(linedef, -1);
		++made;
	}

	// Lines the shape surrounds: their sides that faced the area it was drawn
	// in face the new sector now. In the void only a side off which lies that
	// same stretch of void, not a hole inside another sector, is opened.
	const QPolygonF ring(shape);
	for (int index = 0; index < firstNewLinedef; ++index) {
		if (!topology.linedefLive(index) || boundary.contains(index)) {
			continue;
		}
		const QPointF start = topology.point(topology.linedefs.at(index).startVertex);
		const QPointF end = topology.point(topology.linedefs.at(index).endVertex);
		if (!ring.containsPoint((start + end) / 2.0, Qt::OddEvenFill)) {
			continue;
		}
		if (enclosing >= 0) {
			for (const bool front : {true, false}) {
				const int side = front ? topology.linedefs.at(index).frontSidedef : topology.linedefs.at(index).backSidedef;
				if (side >= 0 && side < topology.sidedefs.size() && topology.sidedefs.at(side).sector == enclosing) {
					topology.ownSide(index, front)->sector = newSector;
				}
			}
			continue;
		}
		const LevelMapDoomLinedef& linedef = topology.linedefs.at(index);
		if (linedef.backSidedef >= 0 || linedef.frontSidedef < 0) {
			continue;
		}
		const double length = std::hypot(end.x() - start.x(), end.y() - start.y());
		if (length <= 0.0) {
			continue;
		}
		// Behind the line: its left, with y up.
		const QPointF behind = (start + end) / 2.0 + QPointF(-(end.y() - start.y()) / length, (end.x() - start.x()) / length) * 0.25;
		int depth = 0;
		if (doomSectorAtPoint(outlines, behind, &depth) >= 0 || depth != enclosingDepth) {
			continue;
		}
		const QString texture = !isEmptyTextureName(topology.sidedefs.at(linedef.frontSidedef).middleTexture)
			? topology.sidedefs.at(linedef.frontSidedef).middleTexture
			: wall;
		LevelMapDoomSidedef side;
		side.sector = newSector;
		side.upperTexture = texture;
		side.lowerTexture = texture;
		side.middleTexture = QStringLiteral("-");
		const int sideId = topology.addSidedef(side);
		topology.setSide(index, false, sideId);
		openDoomLinedef(&topology, index);
	}

	// Drawn exactly over a sector, the new one takes its place, so its tag and
	// special come across and the lines that find it by tag still do.
	if (enclosing >= 0) {
		bool stays = false;
		for (int index = 0; index < topology.linedefs.size() && !stays; ++index) {
			if (!topology.linedefLive(index)) {
				continue;
			}
			for (const int side : {topology.linedefs.at(index).frontSidedef, topology.linedefs.at(index).backSidedef}) {
				stays = stays || (side >= 0 && side < topology.sidedefs.size() && topology.sidedefs.at(side).sector == enclosing);
			}
		}
		if (!stays) {
			topology.sectors[newSector].tag = topology.sectors.at(enclosing).tag;
			topology.sectors[newSector].special = topology.sectors.at(enclosing).special;
		}
	}
	const DoomRenumbering renumbering = compactDoomTopology(&topology, *document);
	const int sectorNow = renumbering.sectors.value(newSector, -1);
	const QString description = made == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Draw sector:%1 with 1 new linedef").arg(sectorNow)
					       : QCoreApplication::translate("VibeStudioLevelMap", "Draw sector:%1 with %2 new linedefs").arg(sectorNow).arg(made);
	if (!recordDoomTopology(document, topology, renumbering, {{LevelMapSelectionKind::DoomSector, newSector}}, QStringLiteral("sector"), description,
		    QCoreApplication::translate("VibeStudioLevelMap", "Take sector:%1 back out").arg(sectorNow))) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The map changed under the drawing."));
	}
	if (sectorId) {
		*sectorId = sectorNow;
	}
	if (newLinedefs) {
		*newLinedefs = made;
	}
	return true;
}

bool mergeLevelMapVertices(LevelMapDocument* document, int* merged, QString* error)
{
	int sceneResult_merged = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return mergeLevelMapVertices(candidate, merged ? &sceneResult_merged : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && merged) { *merged = std::move(sceneResult_merged); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (merged) {
		*merged = 0;
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (document->format != LevelMapFormat::DoomWad) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Vertices belong to Doom and Hexen maps."));
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	QVector<int> vertexIds;
	for (const LevelMapSelectionRef& ref : document->selection) {
		if (ref.kind == LevelMapSelectionKind::DoomVertex && !vertexIds.contains(ref.objectId)
			&& indexOfObjectId(document->doomVertices, ref.objectId) >= 0) {
			vertexIds.push_back(ref.objectId);
		}
	}
	if (vertexIds.size() < 2) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select two or more vertices to merge."));
	}
	// Into the primary vertex, the last one picked.
	const int target = document->selectionKind == LevelMapSelectionKind::DoomVertex && vertexIds.contains(document->selectedObjectId)
		? document->selectedObjectId
		: vertexIds.last();
	DoomTopology topology(*document);
	for (const int vertexId : vertexIds) {
		if (vertexId == target) {
			continue;
		}
		topology.vertexGone[vertexId] = true;
		for (LevelMapDoomLinedef& linedef : topology.linedefs) {
			if (linedef.startVertex == vertexId) {
				linedef.startVertex = target;
			}
			if (linedef.endVertex == vertexId) {
				linedef.endVertex = target;
			}
		}
	}
	// A line whose ends the merge brought together goes; lines elsewhere that
	// were already odd are not the merge's to change.
	for (int index = 0; index < topology.linedefs.size(); ++index) {
		if (topology.linedefs.at(index).startVertex == target && topology.linedefs.at(index).endVertex == target) {
			topology.dropLinedef(index);
		}
	}
	joinDuplicateDoomLinedefs(&topology, target);
	const int gone = static_cast<int>(vertexIds.size()) - 1;
	const DoomRenumbering renumbering = compactDoomTopology(&topology, *document);
	const int targetNow = renumbering.vertices.value(target, -1);
	const QString description = gone == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Merge 1 vertex into vertex:%1").arg(targetNow)
					      : QCoreApplication::translate("VibeStudioLevelMap", "Merge %1 vertices into vertex:%2").arg(gone).arg(targetNow);
	if (!recordDoomTopology(document, topology, renumbering, {{LevelMapSelectionKind::DoomVertex, target}}, QStringLiteral("vertex"), description,
		    QCoreApplication::translate("VibeStudioLevelMap", "Separate the merged vertices again"))) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The vertices to merge are no longer in the map."));
	}
	if (merged) {
		*merged = gone;
	}
	return true;
}

bool joinLevelMapSectors(LevelMapDocument* document, bool merge, int* joined, QString* error)
{
	int sceneResult_joined = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return joinLevelMapSectors(candidate, merge, joined ? &sceneResult_joined : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && joined) { *joined = std::move(sceneResult_joined); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (joined) {
		*joined = 0;
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (document->format != LevelMapFormat::DoomWad) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Sectors belong to Doom and Hexen maps."));
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	QVector<int> sectorIds;
	for (const LevelMapSelectionRef& ref : document->selection) {
		if (ref.kind == LevelMapSelectionKind::DoomSector && !sectorIds.contains(ref.objectId)
			&& indexOfObjectId(document->doomSectors, ref.objectId) >= 0) {
			sectorIds.push_back(ref.objectId);
		}
	}
	if (sectorIds.size() < 2) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select two or more sectors to join."));
	}
	// Into the primary sector, the last one picked.
	const int target = document->selectionKind == LevelMapSelectionKind::DoomSector && sectorIds.contains(document->selectedObjectId)
		? document->selectedObjectId
		: sectorIds.last();
	DoomTopology topology(*document);
	const QSet<int> joining(sectorIds.cbegin(), sectorIds.cend());
	if (merge) {
		// The lines between two of the sectors, found before they become one.
		for (int index = 0; index < topology.linedefs.size(); ++index) {
			const LevelMapDoomLinedef& linedef = topology.linedefs.at(index);
			if (linedef.frontSidedef < 0 || linedef.backSidedef < 0 || linedef.special != 0 || linedef.tag != 0
				|| std::any_of(linedef.args.cbegin(), linedef.args.cend(), [](int arg) {
					   return arg != 0;
				   })) {
				continue;
			}
			const int front = topology.sidedefs.value(linedef.frontSidedef).sector;
			const int back = topology.sidedefs.value(linedef.backSidedef).sector;
			if (front != back && joining.contains(front) && joining.contains(back)) {
				topology.dropLinedef(index);
			}
		}
	}
	for (LevelMapDoomSidedef& sidedef : topology.sidedefs) {
		if (joining.contains(sidedef.sector)) {
			sidedef.sector = target;
		}
	}
	for (const int sectorId : sectorIds) {
		if (sectorId != target) {
			topology.sectorGone[sectorId] = true;
		}
	}
	const int gone = static_cast<int>(sectorIds.size()) - 1;
	const DoomRenumbering renumbering = compactDoomTopology(&topology, *document);
	const int targetNow = renumbering.sectors.value(target, -1);
	const QString description = merge ? QCoreApplication::translate("VibeStudioLevelMap", "Merge %1 sectors into sector:%2").arg(gone + 1).arg(targetNow)
					  : QCoreApplication::translate("VibeStudioLevelMap", "Join %1 sectors into sector:%2").arg(gone + 1).arg(targetNow);
	if (!recordDoomTopology(document, topology, renumbering, {{LevelMapSelectionKind::DoomSector, target}}, QStringLiteral("sector"), description,
		    QCoreApplication::translate("VibeStudioLevelMap", "Separate the joined sectors again"))) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The sectors to join are no longer in the map."));
	}
	if (joined) {
		*joined = gone;
	}
	return true;
}

namespace {

// The selected Doom sectors in the order they were picked, or why there are
// none to change.
bool selectedEditableSectors(const LevelMapDocument* document, QVector<int>* sectorIds, QString* why)
{
	if (!document) {
		*why = QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		return false;
	}
	if (document->format != LevelMapFormat::DoomWad) {
		*why = QCoreApplication::translate("VibeStudioLevelMap", "Sectors belong to Doom and Hexen maps.");
		return false;
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		*why = QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet.");
		return false;
	}
	for (const LevelMapSelectionRef& ref : document->selection) {
		if (ref.kind == LevelMapSelectionKind::DoomSector && !sectorIds->contains(ref.objectId)
			&& indexOfObjectId(document->doomSectors, ref.objectId) >= 0) {
			sectorIds->push_back(ref.objectId);
		}
	}
	if (sectorIds->isEmpty()) {
		*why = QCoreApplication::translate("VibeStudioLevelMap", "Select the sectors to change.");
		return false;
	}
	return true;
}

double sectorFieldValue(const LevelMapDoomSector* sector, LevelMapSectorField field)
{
	switch (field) {
	case LevelMapSectorField::Floor:
		return sector->floorHeight;
	case LevelMapSectorField::Ceiling:
		return sector->ceilingHeight;
	case LevelMapSectorField::Light:
		return sector->lightLevel;
	}
	return 0;
}

void setSectorFieldValue(LevelMapDoomSector* sector, LevelMapSectorField field, double value)
{
	switch (field) {
	case LevelMapSectorField::Floor:
		sector->floorHeight = value; break;
	case LevelMapSectorField::Ceiling:
		sector->ceilingHeight = value; break;
	case LevelMapSectorField::Light:
		sector->lightLevel = static_cast<int>(value); break;
	}
}

QPair<int, int> sectorFieldRange(LevelMapSectorField field)
{
	return field == LevelMapSectorField::Light ? QPair<int, int> {0, 255} : QPair<int, int> {-32768, 32767};
}

QString sectorFieldNoun(LevelMapSectorField field, int count)
{
	switch (field) {
	case LevelMapSectorField::Floor:
		return count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "floor") : QCoreApplication::translate("VibeStudioLevelMap", "floors");
	case LevelMapSectorField::Ceiling:
		return count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "ceiling") : QCoreApplication::translate("VibeStudioLevelMap", "ceilings");
	case LevelMapSectorField::Light:
		return count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "light level") : QCoreApplication::translate("VibeStudioLevelMap", "light levels");
	}
	return {};
}

} // namespace

QString levelMapSectorFieldId(LevelMapSectorField field)
{
	switch (field) {
	case LevelMapSectorField::Floor:
		return QStringLiteral("floor");
	case LevelMapSectorField::Ceiling:
		return QStringLiteral("ceiling");
	case LevelMapSectorField::Light:
		return QStringLiteral("light");
	}
	return {};
}

bool levelMapSectorFieldFromId(const QString& id, LevelMapSectorField* field)
{
	const QString key = id.trimmed().toLower();
	for (const LevelMapSectorField candidate : {LevelMapSectorField::Floor, LevelMapSectorField::Ceiling, LevelMapSectorField::Light}) {
		if (key == levelMapSectorFieldId(candidate)) {
			if (field) {
				*field = candidate;
			}
			return true;
		}
	}
	return false;
}

bool shiftLevelMapSectors(LevelMapDocument* document, LevelMapSectorField field, int delta, int* changed, QString* error)
{
	int sceneResult_changed = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return shiftLevelMapSectors(candidate, field, delta, changed ? &sceneResult_changed : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && changed) { *changed = std::move(sceneResult_changed); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (changed) {
		*changed = 0;
	}
	QVector<int> sectorIds;
	QString why;
	if (!selectedEditableSectors(document, &sectorIds, &why)) {
		if (error) {
			*error = why;
		}
		return false;
	}
	if (delta == 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "A shift of 0 changes nothing.");
		}
		return false;
	}
	const auto [low, high] = sectorFieldRange(field);
	DoomTopology topology(*document);
	int moved = 0;
	for (const int sectorId : sectorIds) {
		const double value = sectorFieldValue(&topology.sectors[sectorId], field);
		const double next = std::clamp(value + delta, static_cast<double>(low), static_cast<double>(high));
		if (next != value) {
			setSectorFieldValue(&topology.sectors[sectorId], field, next);
			++moved;
		}
	}
	if (moved == 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The %1 are already at their limit.").arg(sectorFieldNoun(field, 2));
		}
		return false;
	}
	const DoomRenumbering renumbering = compactDoomTopology(&topology, *document);
	QVector<LevelMapSelectionRef> select;
	for (const int sectorId : sectorIds) {
		select.push_back({LevelMapSelectionKind::DoomSector, sectorId});
	}
	const QString noun = sectorFieldNoun(field, moved);
	const QString description = delta > 0 ? QCoreApplication::translate("VibeStudioLevelMap", "Raise %1 %2 by %3").arg(moved).arg(noun).arg(delta)
					       : QCoreApplication::translate("VibeStudioLevelMap", "Lower %1 %2 by %3").arg(moved).arg(noun).arg(-delta);
	if (!recordDoomTopology(document, topology, renumbering, select, QStringLiteral("sector"), description,
		    QCoreApplication::translate("VibeStudioLevelMap", "Put the %1 back").arg(noun), true)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The sectors to change are no longer in the map.");
		}
		return false;
	}
	if (changed) {
		*changed = moved;
	}
	return true;
}

bool gradientLevelMapSectors(LevelMapDocument* document, LevelMapSectorField field, int* changed, QString* error)
{
	int sceneResult_changed = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return gradientLevelMapSectors(candidate, field, changed ? &sceneResult_changed : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && changed) { *changed = std::move(sceneResult_changed); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (changed) {
		*changed = 0;
	}
	QVector<int> sectorIds;
	QString why;
	if (!selectedEditableSectors(document, &sectorIds, &why)) {
		if (error) {
			*error = why;
		}
		return false;
	}
	if (sectorIds.size() < 3) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Select three or more sectors, in the order the gradient runs.");
		}
		return false;
	}
	DoomTopology topology(*document);
	const double first = sectorFieldValue(&topology.sectors[sectorIds.first()], field);
	const double last = sectorFieldValue(&topology.sectors[sectorIds.last()], field);
	const int steps = static_cast<int>(sectorIds.size()) - 1;
	int moved = 0;
	for (int step = 1; step < steps; ++step) {
		const double value = sectorFieldValue(&topology.sectors[sectorIds.at(step)], field);
		// Rounded to the nearest whole unit, as the lumps hold whole numbers.
		const int next = static_cast<int>(std::lround(first + (last - first) * static_cast<double>(step) / steps));
		if (next != value) {
			setSectorFieldValue(&topology.sectors[sectorIds.at(step)], field, next);
			++moved;
		}
	}
	if (moved == 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The %1 already run evenly from the first sector to the last.").arg(sectorFieldNoun(field, 2));
		}
		return false;
	}
	const DoomRenumbering renumbering = compactDoomTopology(&topology, *document);
	QVector<LevelMapSelectionRef> select;
	for (const int sectorId : sectorIds) {
		select.push_back({LevelMapSelectionKind::DoomSector, sectorId});
	}
	const QString description = QCoreApplication::translate("VibeStudioLevelMap", "Grade the %1 of %2 sectors from %3 to %4").arg(sectorFieldNoun(field, 2)).arg(sectorIds.size()).arg(first).arg(last);
	if (!recordDoomTopology(document, topology, renumbering, select, QStringLiteral("sector"), description,
		    QCoreApplication::translate("VibeStudioLevelMap", "Put the %1 back").arg(sectorFieldNoun(field, 2)), true)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The sectors to change are no longer in the map.");
		}
		return false;
	}
	if (changed) {
		*changed = moved;
	}
	return true;
}

bool makeLevelMapDoors(LevelMapDocument* document, const LevelMapDoorOptions& options, int* doors, QString* error)
{
	int sceneResult_doors = 0;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return makeLevelMapDoors(candidate, options, doors ? &sceneResult_doors : nullptr, error);
	}); guarded.has_value()) {
		if (*guarded && doors) { *doors = std::move(sceneResult_doors); }
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (doors) {
		*doors = 0;
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (document->format != LevelMapFormat::DoomWad) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Doors are made on Doom and Hexen maps."));
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	QVector<int> sectorIds;
	for (const LevelMapSelectionRef& ref : document->selection) {
		if (ref.kind == LevelMapSelectionKind::DoomSector && !sectorIds.contains(ref.objectId)
			&& indexOfObjectId(document->doomSectors, ref.objectId) >= 0) {
			sectorIds.push_back(ref.objectId);
		}
	}
	if (sectorIds.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select the sectors to make doors."));
	}
	// Doom names textures and flats in eight characters or fewer.
	const auto lumpName = [](const QString& name) {
		return name.trimmed().toUpper();
	};
	const QString doorTexture = lumpName(options.doorTexture);
	const QString trackTexture = lumpName(options.trackTexture);
	const QString ceilingFlat = lumpName(options.ceilingFlat);
	for (const QString& name : {doorTexture, trackTexture}) {
		if (name.isEmpty() || name.size() > 8 || name.contains(QLatin1Char(' '))) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "A door's textures are names of 1 to 8 characters without spaces; \"%1\" will not do.").arg(name));
		}
	}
	if (ceilingFlat.size() > 8 || ceilingFlat.contains(QLatin1Char(' '))) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "A flat's name is 1 to 8 characters without spaces; \"%1\" will not do.").arg(ceilingFlat));
	}
	// https://doomwiki.org/wiki/Linedef#Linedef_flags and, for Hexen's
	// activation bits and repeat flag, https://doomwiki.org/wiki/Hexen_map_format
	constexpr int kUpperUnpegged = 0x0008;
	constexpr int kLowerUnpegged = 0x0010;
	constexpr int kHexenRepeat = 0x0200;
	constexpr int kHexenActivation = 0x1c00;
	constexpr int kHexenOnUse = 0x0400;
	const bool hexen = document->doomFormat == LevelMapDoomFormat::Hexen;

	DoomTopology topology(*document);
	const QSet<int> doorSectors(sectorIds.cbegin(), sectorIds.cend());
	for (const int sectorId : sectorIds) {
		LevelMapDoomSector& sector = topology.sectors[sectorId];
		sector.ceilingHeight = sector.floorHeight;
		if (!ceilingFlat.isEmpty()) {
			sector.ceilingTexture = ceilingFlat;
		}
	}
	const auto resetOffsets = [&options](LevelMapDoomSidedef* side) {
		if (side && options.resetOffsets) {
			side->offsetX = 0;
			side->offsetY = 0;
		}
	};
	for (int index = 0; index < topology.linedefs.size(); ++index) {
		const LevelMapDoomLinedef& before = topology.linedefs.at(index);
		const int frontSector = before.frontSidedef >= 0 ? topology.sidedefs.value(before.frontSidedef).sector : -1;
		const int backSector = before.backSidedef >= 0 ? topology.sidedefs.value(before.backSidedef).sector : -1;
		const bool frontDoor = doorSectors.contains(frontSector);
		const bool backDoor = doorSectors.contains(backSector);
		if (!frontDoor && !backDoor) {
			continue;
		}
		if (before.backSidedef < 0) {
			// A wall of the door's own: a track that stays put as the door rises.
			if (LevelMapDoomSidedef* track = topology.ownSide(index, true)) {
				track->upperTexture = QStringLiteral("-");
				track->middleTexture = trackTexture;
				track->lowerTexture = QStringLiteral("-");
				resetOffsets(track);
			}
			LevelMapDoomLinedef& line = topology.linedefs[index];
			line.flags = (line.flags & ~kUpperUnpegged) | kLowerUnpegged;
			continue;
		}
		if (frontDoor && backDoor) {
			continue;
		}
		// Used from outside: the front side is the one out of the door.
		if (frontDoor) {
			LevelMapDoomLinedef& line = topology.linedefs[index];
			std::swap(line.startVertex, line.endVertex);
			std::swap(line.frontSidedef, line.backSidedef);
		}
		if (LevelMapDoomSidedef* outside = topology.ownSide(index, true)) {
			outside->upperTexture = doorTexture;
			resetOffsets(outside);
		}
		if (options.resetOffsets) {
			resetOffsets(topology.ownSide(index, false));
		}
		LevelMapDoomLinedef& line = topology.linedefs[index];
		line.flags &= ~(kUpperUnpegged | kLowerUnpegged);
		if (hexen) {
			// Door_Raise(tag 0: the sector behind the line, speed, delay).
			line.special = 12;
			line.args = {0, 16, 150, 0, 0};
			line.tag = 0;
			line.flags = (line.flags & ~kHexenActivation) | kHexenOnUse | kHexenRepeat;
		} else {
			// DR Door Open Wait Close; a door used by hand needs no tag, and
			// an old one left behind would read as a link it does not have.
			line.special = 1;
			line.tag = 0;
		}
	}
	const DoomRenumbering renumbering = compactDoomTopology(&topology, *document);
	QVector<LevelMapSelectionRef> select;
	for (const int sectorId : sectorIds) {
		select.push_back({LevelMapSelectionKind::DoomSector, sectorId});
	}
	const int count = static_cast<int>(sectorIds.size());
	const QString description = count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Make sector:%1 a door").arg(sectorIds.first()) : QCoreApplication::translate("VibeStudioLevelMap", "Make %1 sectors doors").arg(count);
	if (!recordDoomTopology(document, topology, renumbering, select, QStringLiteral("sector"), description,
		    count == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Make sector:%1 a room again").arg(sectorIds.first()) : QCoreApplication::translate("VibeStudioLevelMap", "Make %1 doors rooms again").arg(count))) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The sectors to make doors are no longer in the map."));
	}
	if (doors) {
		*doors = count;
	}
	return true;
}

namespace {

bool deleteDoomGeometry(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& objects, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (document->doomFormat == LevelMapDoomFormat::Udmf) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Use UDMF Properties to edit this map; this native editing operation is not supported yet."));
	}
	QSet<int> thingIds;
	QSet<int> vertexIds;
	QSet<int> linedefIds;
	QSet<int> sectorIds;
	for (const LevelMapSelectionRef& ref : objects) {
		switch (ref.kind) {
		case LevelMapSelectionKind::DoomThing:
		case LevelMapSelectionKind::Entity:
			if (!thingById(document, ref.objectId)) {
				return fail(selectionNotFoundText(ref.kind));
			}
			thingIds.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::DoomVertex:
			if (indexOfObjectId(document->doomVertices, ref.objectId) < 0) {
				return fail(selectionNotFoundText(ref.kind));
			}
			vertexIds.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::DoomLinedef:
			if (indexOfObjectId(document->doomLinedefs, ref.objectId) < 0) {
				return fail(selectionNotFoundText(ref.kind));
			}
			linedefIds.insert(ref.objectId);
			break;
		case LevelMapSelectionKind::DoomSector:
			if (indexOfObjectId(document->doomSectors, ref.objectId) < 0) {
				return fail(selectionNotFoundText(ref.kind));
			}
			sectorIds.insert(ref.objectId);
			break;
		default:
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Only things, vertices, linedefs, and sectors can be deleted from a Doom map."));
		}
	}
	DoomTopology topology(*document);
	const QString wall = defaultDoomWallTexture(document->doomSidedefs, document->doomFormat);
	// A sector takes its sides with it: a line left with one side becomes a
	// wall facing the sector that stays, and a line left with none goes.
	for (const int sectorId : sectorIds) {
		topology.sectorGone[sectorId] = true;
	}
	if (!sectorIds.isEmpty()) {
		for (int index = 0; index < topology.linedefs.size(); ++index) {
			const auto facesGone = [&topology, &sectorIds](int side) {
				return side >= 0 && side < topology.sidedefs.size() && sectorIds.contains(topology.sidedefs.at(side).sector);
			};
			const bool frontGone = facesGone(topology.linedefs.at(index).frontSidedef);
			const bool backGone = facesGone(topology.linedefs.at(index).backSidedef);
			if (!frontGone && !backGone) {
				continue;
			}
			if (frontGone) {
				topology.setSide(index, true, -1);
			}
			if (backGone) {
				topology.setSide(index, false, -1);
			}
			if (topology.linedefs.at(index).frontSidedef < 0 && topology.linedefs.at(index).backSidedef < 0) {
				topology.dropLinedef(index);
			} else {
				closeDoomLinedef(&topology, index, wall);
			}
		}
	}
	for (const int linedefId : linedefIds) {
		topology.dropLinedef(linedefId);
	}
	// A vertex between exactly two linedefs dissolves, the first taking the
	// second's far end, as Doom Builder joins them; any other vertex goes with
	// its linedefs.
	QVector<int> vertices(vertexIds.cbegin(), vertexIds.cend());
	std::sort(vertices.begin(), vertices.end());
	for (const int vertexId : vertices) {
		QVector<int> attached;
		for (int index = 0; index < topology.linedefs.size(); ++index) {
			const LevelMapDoomLinedef& linedef = topology.linedefs.at(index);
			if (!topology.linedefGone.at(index) && (linedef.startVertex == vertexId || linedef.endVertex == vertexId)) {
				attached.push_back(index);
			}
		}
		topology.vertexGone[vertexId] = true;
		if (attached.size() == 2) {
			const LevelMapDoomLinedef& joined = topology.linedefs.at(attached.at(1));
			const LevelMapDoomLinedef& kept = topology.linedefs.at(attached.at(0));
			const int far = joined.startVertex == vertexId ? joined.endVertex : joined.startVertex;
			const int keptFar = kept.startVertex == vertexId ? kept.endVertex : kept.startVertex;
			if (far != keptFar && far != vertexId && !topology.vertexGone.at(far)) {
				LevelMapDoomLinedef& joining = topology.linedefs[attached.at(0)];
				if (joining.startVertex == vertexId) {
					joining.startVertex = far;
				} else {
					joining.endVertex = far;
				}
				topology.dropLinedef(attached.at(1));
				continue;
			}
		}
		for (const int index : attached) {
			topology.dropLinedef(index);
		}
	}
	topology.thingsGone = thingIds;
	const int requested = static_cast<int>(thingIds.size() + vertexIds.size() + linedefIds.size() + sectorIds.size());
	const QString first = levelMapSelectionRefId(objects.first());
	const DoomRenumbering renumbering = compactDoomTopology(&topology, *document);
	if (!recordDoomTopology(document, topology, renumbering, {}, QStringLiteral("selection"),
		    requested == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Delete %1").arg(first) : QCoreApplication::translate("VibeStudioLevelMap", "Delete %1 objects").arg(requested),
		    requested == 1 ? QCoreApplication::translate("VibeStudioLevelMap", "Restore %1").arg(first) : QCoreApplication::translate("VibeStudioLevelMap", "Restore %1 deleted objects").arg(requested))) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The objects to delete are no longer in the map."));
	}
	return true;
}

} // namespace

bool levelMapSelectionBounds(const LevelMapDocument& document, LevelMapVec3* mins, LevelMapVec3* maxs)
{
	return transformSetBounds(document, transformSetFor(document, document.selection), mins, maxs);
}

bool levelMapObjectsBounds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects, LevelMapVec3* mins,
	LevelMapVec3* maxs)
{
	return transformSetBounds(document, transformSetFor(document, objects), mins, maxs);
}

bool moveLevelMapSelection(LevelMapDocument* document, double dx, double dy, double dz,
	const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return moveLevelMapSelection(candidate, dx, dy, dz, textures, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || document->selection.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select map objects before moving them."));
	}
	if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz) || (dx == 0 && dy == 0 && dz == 0)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The move needs a finite, nonzero delta."));
	}
	// Doom's move path includes thing heights and the existing topology rules.
	if (document->format == LevelMapFormat::DoomWad && !document->doomUdmf) { return moveLevelMapSelection(document, dx, dy, dz, error); }
	SelectionTransform transform;
	transform.textures = textures;
	transform.point = [dx,dy,dz](const auto& p, const auto&) { return LevelMapVec3 {p.x+dx,p.y+dy,p.z+dz,p.valid}; };
	transform.direction = [](const auto& p) { return p; };
	transform.yaw = [](double) -> std::optional<double> { return std::nullopt; };
	transform.description = QCoreApplication::translate("VibeStudioLevelMap", "Move selection by %1,%2,%3").arg(dx).arg(dy).arg(dz);
	transform.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Undo selection move");
	return transformLevelMapSelection(document, transform, true, error);
}

bool moveLevelMapObject(LevelMapDocument* document, const QString& objectKind, int objectId, double dx, double dy, double dz,
	const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return moveLevelMapObject(candidate, objectKind, objectId, dx, dy, dz, textures, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (!document || document->format == LevelMapFormat::DoomWad) {
		return moveLevelMapObject(document, objectKind, objectId, dx, dy, dz, error);
	}
	// Build an isolated selection so failure cannot change the live selection,
	// dirty state, revision or history. Qt containers share their untouched data.
	auto candidate = *document;
	if (!selectLevelMapObject(&candidate, QStringLiteral("%1:%2").arg(objectKind).arg(objectId), error)
		|| !moveLevelMapSelection(&candidate, dx, dy, dz, textures, error)) { return false; }
	*document = std::move(candidate);
	return true;
}

bool moveLevelMapSelectionSnapped(LevelMapDocument* document, double dx, double dy, double dz, double gridSize,
	const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return moveLevelMapSelectionSnapped(candidate, dx, dy, dz, gridSize, textures, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (!std::isfinite(gridSize)) {
		if (error) { *error = QCoreApplication::translate("VibeStudioLevelMap", "The grid size must be finite."); }
		return false;
	}
	return moveLevelMapSelection(document, snapLevelMapCoordinate(dx, gridSize), snapLevelMapCoordinate(dy, gridSize),
		snapLevelMapCoordinate(dz, gridSize), textures, error);
}

bool resizeLevelMapSelection(LevelMapDocument* document, const LevelMapVec3& mins, const LevelMapVec3& maxs, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return resizeLevelMapSelection(candidate, mins, maxs, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	return resizeLevelMapSelection(document, mins, maxs, {false, false}, error);
}

bool resizeLevelMapSelection(LevelMapDocument* document, const LevelMapVec3& mins, const LevelMapVec3& maxs,
	const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return resizeLevelMapSelection(candidate, mins, maxs, textures, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document || document->selection.isEmpty()) {
		return fail(document ? QCoreApplication::translate("VibeStudioLevelMap", "Nothing is selected.") : QCoreApplication::translate("VibeStudioLevelMap", "Missing map document."));
	}
	if (!mins.valid || !maxs.valid || !std::isfinite(mins.x) || !std::isfinite(mins.y) || !std::isfinite(mins.z)
		|| !std::isfinite(maxs.x) || !std::isfinite(maxs.y) || !std::isfinite(maxs.z)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The new bounds need two finite corners."));
	}
	LevelMapVec3 low;
	LevelMapVec3 high;
	if (!levelMapSelectionBounds(*document, &low, &high)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing selected has a size to change."));
	}
	constexpr double kSmallest = 1.0e-6;
	const std::array<double, 3> from {low.x, low.y, low.z};
	const std::array<double, 3> fromSize {high.x - low.x, high.y - low.y, high.z - low.z};
	const std::array<double, 3> to {mins.x, mins.y, mins.z};
	const std::array<double, 3> toSize {maxs.x - mins.x, maxs.y - mins.y, maxs.z - mins.z};
	std::array<double, 3> scale {1.0, 1.0, 1.0};
	bool changes = false;
	for (int axis = 0; axis < 3; ++axis) {
		if (fromSize[axis] > kSmallest) {
			if (toSize[axis] <= kSmallest) {
				return fail(QCoreApplication::translate("VibeStudioLevelMap", "The new size along %1 must be above zero.").arg(QString(QLatin1Char("xyz"[axis]))));
			}
			scale[axis] = toSize[axis] / fromSize[axis];
		}
		changes = changes || std::abs(scale[axis] - 1.0) > kSmallest || std::abs(to[axis] - from[axis]) > kSmallest;
	}
	if (!changes) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The selection already has those bounds."));
	}
	SelectionTransform transform;
	transform.point = [from, to, scale](const LevelMapVec3& point, const LevelMapVec3&) {
		return LevelMapVec3 {to[0] + (point.x - from[0]) * scale[0], to[1] + (point.y - from[1]) * scale[1],
			to[2] + (point.z - from[2]) * scale[2], true};
	};
	transform.textures = textures;
	transform.direction = [scale](const LevelMapVec3& direction) {
		return LevelMapVec3 {direction.x * scale[0], direction.y * scale[1], direction.z * scale[2], direction.valid};
	};
	// Stretching by S takes a plane's normal n to S^-1 n, kept at unit length.
	transform.normal = [scale](const LevelMapVec3& normal) {
		LevelMapVec3 result {normal.x / scale[0], normal.y / scale[1], normal.z / scale[2], normal.valid};
		const double length = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);
		if (length > 0.0) {
			result.x /= length;
			result.y /= length;
			result.z /= length;
		}
		return result;
	};
	transform.yaw = [](double) -> std::optional<double> {
		return std::nullopt;
	};
	const QString size = QStringLiteral("%1 x %2 x %3").arg(mapCoordinateText(toSize[0]), mapCoordinateText(toSize[1]), mapCoordinateText(toSize[2]));
	if (document->selection.size() == 1) {
		const QString only = levelMapSelectionRefId(document->selection.first());
		transform.description = QCoreApplication::translate("VibeStudioLevelMap", "Resize %1 to %2").arg(only, size);
		transform.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Resize %1 back").arg(only);
	} else {
		const int count = static_cast<int>(document->selection.size());
		transform.description = QCoreApplication::translate("VibeStudioLevelMap", "Resize %1 objects to %2").arg(count).arg(size);
		transform.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Resize %1 objects back").arg(count);
	}
	return transformLevelMapSelection(document, transform, true, error);
}

bool rotateLevelMapSelection(LevelMapDocument* document, const LevelMapRotationRequest& request, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return rotateLevelMapSelection(candidate, request, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || document->selection.isEmpty()) { return fail(QCoreApplication::translate("VibeStudioLevelMap", "Nothing is selected.")); }
	if (request.axis < 0 || request.axis > 2) { return fail(QCoreApplication::translate("VibeStudioLevelMap", "The axis must be x, y, or z.")); }
	if (!std::isfinite(request.degrees) || std::abs(request.degrees) > 360000) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Rotation must be a finite number between -360000 and 360000 degrees."));
	}
	const double degrees = std::remainder(request.degrees, 360.0);
	if (std::abs(degrees) < 1e-9) { return fail(QCoreApplication::translate("VibeStudioLevelMap", "A whole number of full turns changes nothing.")); }
	const double pivotLimit = document->doomUdmf ? 1e7 : 32768;
	if (request.pivot.valid && (!std::isfinite(request.pivot.x) || !std::isfinite(request.pivot.y) || !std::isfinite(request.pivot.z)
		|| std::max({std::abs(request.pivot.x), std::abs(request.pivot.y), std::abs(request.pivot.z)}) > pivotLimit)) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The rotation pivot must be finite and within %1 to %2 on each axis.")
			.arg(-pivotLimit, 0, 'g', 10).arg(pivotLimit, 0, 'g', 10));
	}
	SelectionTransform transform;
	transform.rotation = request;
	transform.textures = {request.textureLock, request.allowValve220};
	transform.rotation->degrees = degrees;
	transform.point = [axis = request.axis, degrees](const auto& point, const auto& centre) { return rotateLevelPoint(point, centre, axis, degrees); };
	transform.direction = [axis = request.axis, degrees](const auto& vector) { return rotateLevelVector(vector, axis, degrees); };
	transform.yaw = [axis = request.axis, degrees](double yaw) -> std::optional<double> { return axis == 2 ? std::optional<double>(yaw + degrees) : std::nullopt; };
	transform.description = QCoreApplication::translate("VibeStudioLevelMap", "Rotate selection %1 degrees about %2").arg(mapCoordinateText(degrees), QString(QLatin1Char("xyz"[request.axis])));
	transform.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Undo selection rotation");
	return transformLevelMapSelection(document, transform, request.axis == 2, error);
}

bool rotateLevelMapSelection(LevelMapDocument* document, int axis, int quarterTurns, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return rotateLevelMapSelection(candidate, axis, quarterTurns, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	LevelMapRotationRequest request;
	request.axis = axis;
	request.degrees = 90.0 * (quarterTurns % 4);
	return rotateLevelMapSelection(document, request, error);
}

bool flipLevelMapSelection(LevelMapDocument* document, int axis, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return flipLevelMapSelection(candidate, axis, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	return flipLevelMapSelection(document, axis, LevelMapTextureLockOptions{}, error);
}

bool flipLevelMapSelection(LevelMapDocument* document, int axis, const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return flipLevelMapSelection(candidate, axis, textures, error);
	}); guarded.has_value()) {
		return *guarded;
	}

	if (error) {
		error->clear();
	}
	if (!document || document->selection.isEmpty()) {
		if (error) {
			*error = document ? QCoreApplication::translate("VibeStudioLevelMap", "Nothing is selected.") : QCoreApplication::translate("VibeStudioLevelMap", "Missing map document.");
		}
		return false;
	}
	if (axis < 0 || axis > 2) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "The axis must be x, y, or z.");
		}
		return false;
	}
	const auto mirrored = [axis](LevelMapVec3 vector) {
		(axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z) *= -1.0;
		return vector;
	};
	SelectionTransform transform;
	transform.point = [axis](const LevelMapVec3& point, const LevelMapVec3& centre) {
		LevelMapVec3 flipped = point;
		double& value = axis == 0 ? flipped.x : axis == 1 ? flipped.y : flipped.z;
		const double middle = axis == 0 ? centre.x : axis == 1 ? centre.y : centre.z;
		value = 2.0 * middle - value;
		return flipped;
	};
	transform.direction = mirrored;
	// Mirroring x reflects a heading about north (180 - a), mirroring y about
	// east (-a); a heading has no height to mirror.
	transform.yaw = [axis](double yaw) -> std::optional<double> {
		if (axis == 0) {
			return 180.0 - yaw;
		}
		if (axis == 1) {
			return -yaw;
		}
		return std::nullopt;
	};
	transform.mirror = true;
	transform.textures = textures;
	const QString axisName(QLatin1Char("xyz"[axis]));
	if (document->selection.size() == 1) {
		const QString only = levelMapSelectionRefId(document->selection.first());
		transform.description = QCoreApplication::translate("VibeStudioLevelMap", "Flip %1 along %2").arg(only, axisName);
		transform.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Flip %1 back").arg(only);
	} else {
		const int count = static_cast<int>(document->selection.size());
		transform.description = QCoreApplication::translate("VibeStudioLevelMap", "Flip %1 objects along %2").arg(count).arg(axisName);
		transform.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Flip %1 objects back").arg(count);
	}
	return transformLevelMapSelection(document, transform, axis != 2, error);
}

bool snapLevelMapSelectionToGrid(LevelMapDocument* document, double gridSize, QString* error)
{
	return snapLevelMapSelectionToGrid(document, gridSize, {false, false}, error);
}

bool snapLevelMapSelectionToGrid(LevelMapDocument* document, double gridSize,
	const LevelMapTextureLockOptions& textures, QString* error)
{
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument* candidate) {
		return snapLevelMapSelectionToGrid(candidate, gridSize, textures, error);
	}); guarded.has_value()) { return *guarded; }
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!document || document->selection.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select map objects before snapping them to the grid."));
	}
	if (!std::isfinite(gridSize) || gridSize <= 0) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The grid size must be finite and positive."));
	}
	const bool doom = document->format == LevelMapFormat::DoomWad;
	if (doom && !document->doomUdmf && std::floor(gridSize) != gridSize) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Doom and Hexen snapping requires a whole-unit grid."));
	}
	LevelMapObjectExistence existing(document);
	for (const auto& ref : document->selection) {
		detail::placementCancellationCheckpoint();
		if (!existing.contains(ref)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "The selected object %1 no longer exists.").arg(levelMapSelectionRefId(ref)));
		}
	}
	const auto set = transformSetFor(*document, document->selection);
	SelectionTransform transform;
	transform.textures = textures;
	transform.individualOffsets = true;
	transform.direction = [](const auto& p) { return p; };
	transform.yaw = [](double) -> std::optional<double> { return std::nullopt; };
	const auto deltaFor = [gridSize, doom](const LevelMapVec3& point) {
		const auto snapped = snapLevelMapPosition(point, gridSize);
		return LevelMapVec3{snapped.x-point.x, snapped.y-point.y, doom ? 0 : snapped.z-point.z, true};
	};
	const auto add = [&](const QString& kind, int id, const LevelMapVec3& delta) {
		if (std::max({std::abs(delta.x),std::abs(delta.y),std::abs(delta.z)}) > 1e-9) {
			transform.offsets.insert(QStringLiteral("%1:%2").arg(kind).arg(id), delta);
		}
	};
	QHash<int, LevelMapVec3> ownerLow, ownerHigh, ownerOffsets;
	for (const auto& brush : document->brushes) {
		detail::placementCancellationCheckpoint();
		if (!set.brushIds.contains(brush.id)) { continue; }
		if (!brush.boundsSolved || !brush.mins.valid) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Brush %1 has no solved bounds to snap.").arg(brush.id));
		}
		includePoint(&ownerLow[brush.entityId], &ownerHigh[brush.entityId], brush.mins);
		includePoint(&ownerLow[brush.entityId], &ownerHigh[brush.entityId], brush.maxs);
	}
	for (const auto& patch : document->patches) {
		detail::placementCancellationCheckpoint();
		if (!set.patchIds.contains(patch.id)) { continue; }
		includePoint(&ownerLow[patch.entityId], &ownerHigh[patch.entityId], patch.mins);
		includePoint(&ownerLow[patch.entityId], &ownerHigh[patch.entityId], patch.maxs);
	}
	for (const auto& entity : document->entities) {
		detail::placementCancellationCheckpoint();
		if (!set.entityIds.contains(entity.id)) { continue; }
		if (isWorldspawnEntity(entity)) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Select worldspawn's brushes or patches to snap them."));
		}
		const auto anchor = entity.origin.valid ? entity.origin : ownerLow.value(entity.id);
		if (!anchor.valid) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Entity %1 has neither an origin nor primitive bounds to snap.").arg(entity.id));
		}
		const auto delta = deltaFor(anchor);
		ownerOffsets.insert(entity.id, delta);
		// No synthesized origin: originless brush entities retain their semantics.
		if (entity.origin.valid) { add(QStringLiteral("entity"), entity.id, delta); }
	}
	for (const auto& brush : document->brushes) {
		detail::placementCancellationCheckpoint();
		if (!set.brushIds.contains(brush.id)) { continue; }
		add(QStringLiteral("brush"), brush.id, ownerOffsets.contains(brush.entityId) ? ownerOffsets.value(brush.entityId) : deltaFor(brush.mins));
	}
	for (const auto& patch : document->patches) {
		detail::placementCancellationCheckpoint();
		if (!set.patchIds.contains(patch.id)) { continue; }
		if (!patch.mins.valid) {
			return fail(QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 has no bounds to snap.").arg(patch.id));
		}
		add(QStringLiteral("patch"), patch.id, ownerOffsets.contains(patch.entityId) ? ownerOffsets.value(patch.entityId) : deltaFor(patch.mins));
	}
	for (const auto& thing : document->doomThings) {
		detail::placementCancellationCheckpoint();
		if (set.thingIds.contains(thing.id)) { add(QStringLiteral("thing"), thing.id, deltaFor({thing.x,thing.y,0,true})); }
	}
	for (const auto& vertex : document->doomVertices) {
		detail::placementCancellationCheckpoint();
		if (set.vertexIds.contains(vertex.id)) { add(QStringLiteral("vertex"), vertex.id, deltaFor({vertex.x,vertex.y,0,true})); }
	}
	if (transform.offsets.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The selection is already on the %1-unit grid.").arg(gridSize));
	}
	transform.description = QCoreApplication::translate("VibeStudioLevelMap", "Snap selection to the %1-unit grid").arg(gridSize);
	transform.undoDescription = QCoreApplication::translate("VibeStudioLevelMap", "Restore selection before snapping");
	return transformLevelMapSelection(document, transform, true, error);
}

bool commitLevelSceneState(LevelMapDocument* document, const LevelSceneState& state, const QString& description, QString* error)
{
	if (!document || document->format == LevelMapFormat::Unknown || !validateLevelScene(*document, state, error)) { return false; }
	if (state == document->scene) { return true; }
	if (!validateLevelSceneOrganizationEdit(*document, state, error)) { return false; }
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("scene");
	command.description = description;
	command.undoDescription = QCoreApplication::translate("LevelScene", "Restore scene organization");
	command.hasSceneSnapshot = true;
	command.sceneBefore = document->scene; command.sceneAfter = state;
	command.selectionSnapshot = document->selection;
	auto candidate = *document; candidate.scene = state;
	const auto hidden = levelSceneHiddenObjects(candidate);
	for (const auto& ref : document->selection) {
		if (!hidden.contains(levelSceneCanonicalObject(*document, levelMapSelectionRefId(ref)))) { command.selectionResult << ref; }
	}
	if (!applyLevelMapCommand(document, command, true)) { return false; }
	pushUndo(document, command);
	return true;
}

bool undoLevelMapEdit(LevelMapDocument* document, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || document->undoStack.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "No undo command is available.");
		}
		return false;
	}
	const LevelMapUndoCommand command = document->undoStack.takeLast();
	if (!applyLevelMapCommand(document, command, false)) {
		// Put the command back so the stacks stay consistent.
		document->undoStack.push_back(command);
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Undo target is no longer present in the map.");
		}
		return false;
	}
	document->redoStack.push_back(command);
	++document->revision;
	countDoomGeometryEdit(document, command, -1);
	refreshEditState(document);
	return true;
}

bool redoLevelMapEdit(LevelMapDocument* document, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || document->redoStack.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "No redo command is available.");
		}
		return false;
	}
	const LevelMapUndoCommand command = document->redoStack.takeLast();
	if (!applyLevelMapCommand(document, command, true)) {
		document->redoStack.push_back(command);
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Redo target is no longer present in the map.");
		}
		return false;
	}
	document->undoStack.push_back(command);
	++document->revision;
	countDoomGeometryEdit(document, command, 1);
	refreshEditState(document);
	return true;
}

void markLevelMapSaved(LevelMapDocument* document)
{
	if (!document) {
		return;
	}
	document->savedUndoDepth = document->undoStack.size();
	document->editState = QStringLiteral("saved");
}

LevelMapSaveReport saveLevelMapAs(const LevelMapDocument& document, const QString& outputPath, bool dryRun, bool overwriteExisting)
{
	LevelDocumentSaveRequest request;
	request.path = outputPath;
	request.dryRun = dryRun;
	request.overwrite = overwriteExisting;
	return writeLevelDocument(document, request);
}

LevelMapSerialized serializeLevelMap(const LevelMapDocument& document)
{
	LevelMapSerialized result;
	if (document.format == LevelMapFormat::DoomWad) {
		serializeDoomWad(document, &result.bytes, &result.errors, &result.warnings, &result.staleLumps);
	} else if (isTextMapFormat(document.format)) {
		for (const auto& patch : document.patches) {
			QString error;
			if (patch.definitionDirty && !validateLevelPatch(patch, &error)) {
				result.errors << QCoreApplication::translate("VibeStudioLevelMap", "Patch %1 cannot be saved: %2").arg(patch.id).arg(error);
				return result;
			}
		}
		QHash<QString, QString> emitted;
		result.bytes = serializeMapText(document, document.scene.nodes.isEmpty() ? nullptr : &emitted).toUtf8();
		QString error;
		const auto metadata = encodeLevelScene(document, QCryptographicHash::hash(result.bytes, QCryptographicHash::Sha256), emitted, &error);
		if (!error.isEmpty()) { result.errors << error; result.bytes.clear(); return result; }
		if (!metadata.isEmpty() || !document.scene.problem.isEmpty()) { result.bytes += QByteArray("\n// VibeStudioScene: ") + metadata + '\n'; }
		if (!document.scene.problem.isEmpty()) { result.warnings << document.scene.problem; }
	} else {
		result.errors << QCoreApplication::translate("VibeStudioLevelMap", "No supported level document is open.");
	}
	if (result.bytes.size() > kLevelMapMaxDocumentBytes) {
		result.bytes.clear();
		result.errors << QCoreApplication::translate("VibeStudioLevelMap", "The serialized map exceeds the 512 MiB document limit.");
	}
	return result;
}

QString levelMapSaveReportText(const LevelMapSaveReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Level map save");
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Source: %1").arg(report.sourcePath);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Output: %1").arg(report.outputPath);
	if (!report.backupPath.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Backup: %1").arg(report.backupPath);
	}
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Map: %1").arg(report.mapName);
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Format: %1").arg(levelMapFormatId(report.format));
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Mode: %1").arg(report.dryRun ? QCoreApplication::translate("VibeStudioLevelMap", "dry run") : QCoreApplication::translate("VibeStudioLevelMap", "write"));
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Written: %1").arg(report.written ? QCoreApplication::translate("VibeStudioLevelMap", "yes") : QCoreApplication::translate("VibeStudioLevelMap", "no"));
	lines << QCoreApplication::translate("VibeStudioLevelMap", "Edit state: %1").arg(report.editState);
	for (const QString& line : report.summaryLines) {
		lines << QStringLiteral("- %1").arg(line);
	}
	if (!report.staleLumps.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Stale lumps needing a node build: %1").arg(report.staleLumps.join(QStringLiteral(", ")));
	}
	for (const QString& warning : report.warnings) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Warning: %1").arg(warning);
	}
	for (const QString& error : report.errors) {
		lines << QCoreApplication::translate("VibeStudioLevelMap", "Error: %1").arg(error);
	}
	return lines.join('\n');
}

CompilerCommandRequest compilerRequestForLevelMap(const LevelMapDocument& document, const QString& profileId, const QString& outputPath)
{
	CompilerCommandRequest request;
	request.profileId = profileId.trimmed().isEmpty()
		? (document.format == LevelMapFormat::DoomWad ? QStringLiteral("zdbsp-nodes") : (document.format == LevelMapFormat::Quake3Map ? QStringLiteral("q3map2-bsp") : QStringLiteral("ericw-qbsp")))
		: profileId.trimmed();
	request.inputPath = document.outputPath.trimmed().isEmpty() ? document.sourcePath : document.outputPath;
	if (request.profileId == QStringLiteral("ericw-qbsp") && document.originalText.section(QLatin1Char('\n'), 0, 0).trimmed() == QString::fromLatin1(kQuake2MapTargetHeader).trimmed()) {
		request.extraArguments << QStringLiteral("-q2bsp");
	}
	request.outputPath = outputPath;
	request.workingDirectory = QFileInfo(request.inputPath).absolutePath();
	request.workspaceRootPath = request.workingDirectory;
	return request;
}

} // namespace vibestudio
