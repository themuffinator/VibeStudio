#include "core/level_map.h"

#include "core/ericw_map_preflight.h"
#include "core/map_geometry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

QString mapText(const char* source)
{
	return QCoreApplication::translate("VibeStudioLevelMap", source);
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

struct WadLump {
	QString name;
	QByteArray bytes;
};

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
		return mapText("unknown");
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

QStringList uniqueTextureReferences(const LevelMapDocument& document)
{
	QStringList values;
	for (const QString& texture : document.textureReferences) {
		const QString trimmed = texture.trimmed();
		if (!trimmed.isEmpty() && trimmed != QStringLiteral("-") && !values.contains(trimmed, Qt::CaseInsensitive)) {
			values.push_back(trimmed);
		}
	}
	std::sort(values.begin(), values.end(), [](const QString& left, const QString& right) {
		return left.compare(right, Qt::CaseInsensitive) < 0;
	});
	return values;
}

QVector<WadLump> readWadLumps(const QString& path, QString* magicOut, QString* error)
{
	if (error) {
		error->clear();
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = mapText("Unable to open WAD file.");
		}
		return {};
	}
	const QByteArray header = file.read(12);
	if (header.size() != 12) {
		if (error) {
			*error = mapText("WAD header is incomplete.");
		}
		return {};
	}
	const QString magic = QString::fromLatin1(header.constData(), 4);
	if (magic != QStringLiteral("IWAD") && magic != QStringLiteral("PWAD")) {
		if (error) {
			*error = mapText("WAD file must start with IWAD or PWAD.");
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
			*error = mapText("WAD directory is invalid.");
		}
		return {};
	}

	QVector<WadLump> lumps;
	if (!file.seek(directoryOffset)) {
		if (error) {
			*error = mapText("Unable to seek to WAD directory.");
		}
		return {};
	}
	for (int index = 0; index < lumpCount; ++index) {
		if (!file.seek(directoryOffset + (index * 16))) {
			if (error) {
				*error = mapText("Unable to seek to WAD directory record.");
			}
			return {};
		}
		const QByteArray record = file.read(16);
		const qint32 offset = readLe32Signed(record, 0);
		const qint32 size = readLe32Signed(record, 4);
		const QString name = fixedLatin1(record, 8, 8).toUpper();
		if (offset < 0 || size < 0 || static_cast<qint64>(offset) + size > file.size()) {
			if (error) {
				*error = mapText("WAD lump has invalid offset or size.");
			}
			return {};
		}
		if (!file.seek(offset)) {
			if (error) {
				*error = mapText("Unable to seek to WAD lump.");
			}
			return {};
		}
		lumps.push_back({name.isEmpty() ? QStringLiteral("LUMP%1").arg(index) : name, file.read(size)});
	}
	return lumps;
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

void validateDoomGeometry(LevelMapDocument* document)
{
	if (!document) {
		return;
	}
	// idTech1 map health checks derived from the Doom map format pages at
	// https://doomwiki.org/wiki/Linedef , https://doomwiki.org/wiki/Sidedef and
	// https://doomwiki.org/wiki/Thing .
	QSet<int> referencedSectors;
	for (const LevelMapDoomLinedef& linedef : document->doomLinedefs) {
		const bool startValid = linedef.startVertex >= 0 && linedef.startVertex < document->doomVertices.size();
		const bool endValid = linedef.endVertex >= 0 && linedef.endVertex < document->doomVertices.size();
		if (startValid && endValid) {
			const LevelMapDoomVertex& start = document->doomVertices.at(linedef.startVertex);
			const LevelMapDoomVertex& end = document->doomVertices.at(linedef.endVertex);
			if (std::abs(start.x - end.x) < 0.0001 && std::abs(start.y - end.y) < 0.0001) {
				addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-zero-length"), mapText("Linedef has zero length."), linedefObjectId(linedef.id));
			}
		}
		const bool twoSidedFlag = (linedef.flags & kLinedefTwoSided) != 0;
		if (twoSidedFlag && linedef.backSidedef < 0) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-twosided-no-back"), mapText("Linedef is flagged two-sided but has no back sidedef."), linedefObjectId(linedef.id));
		}
		if (!twoSidedFlag && linedef.backSidedef >= 0) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("linedef-back-without-twosided"), mapText("Linedef has a back sidedef but is not flagged two-sided."), linedefObjectId(linedef.id));
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
				addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("linedef-missing-middle"), mapText("One-sided linedef has no middle texture."), linedefObjectId(linedef.id));
			}
		}
		if (frontValid && backValid) {
			const LevelMapDoomSector* frontSector = sectorForSidedef(*document, linedef.frontSidedef);
			const LevelMapDoomSector* backSector = sectorForSidedef(*document, linedef.backSidedef);
			if (frontSector && backSector) {
				if (frontSector->ceilingHeight != backSector->ceilingHeight) {
					const int higherSide = frontSector->ceilingHeight > backSector->ceilingHeight ? linedef.frontSidedef : linedef.backSidedef;
					if (isEmptyTextureName(document->doomSidedefs.at(higherSide).upperTexture)) {
						addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("linedef-missing-upper"), mapText("Two-sided linedef has no upper texture where ceiling heights differ."), linedefObjectId(linedef.id));
					}
				}
				if (frontSector->floorHeight != backSector->floorHeight) {
					const int lowerSide = frontSector->floorHeight < backSector->floorHeight ? linedef.frontSidedef : linedef.backSidedef;
					if (isEmptyTextureName(document->doomSidedefs.at(lowerSide).lowerTexture)) {
						addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("linedef-missing-lower"), mapText("Two-sided linedef has no lower texture where floor heights differ."), linedefObjectId(linedef.id));
					}
				}
			}
		}
	}

	for (const LevelMapDoomSector& sector : document->doomSectors) {
		if (!referencedSectors.contains(sector.id)) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("sector-no-linedefs"), mapText("Sector is not referenced by any linedef sidedef."), sectorObjectId(sector.id));
		}
	}

	bool hasPlayerStart = false;
	for (const LevelMapDoomThing& thing : document->doomThings) {
		hasPlayerStart = hasPlayerStart || thing.type == 1;
	}
	if (!hasPlayerStart) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("missing-player-start"), mapText("Map has no player 1 start (thing type 1)."), document->mapName);
	}

	QSet<QString> seenVertices;
	for (const LevelMapDoomVertex& vertex : document->doomVertices) {
		const QString key = QStringLiteral("%1/%2").arg(std::lround(vertex.x)).arg(std::lround(vertex.y));
		if (seenVertices.contains(key)) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("duplicate-vertex"), mapText("Vertex shares coordinates with an earlier vertex."), vertexObjectId(vertex.id));
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
	addIssue(document, LevelMapIssueSeverity::Warning, code, mapText("%1 lump has trailing bytes.").arg(lump), lump);
}

bool parseDoomWad(const LevelMapLoadRequest& request, LevelMapDocument* document, QString* error)
{
	QString magic;
	const QVector<WadLump> allLumps = readWadLumps(request.path, &magic, error);
	if (allLumps.isEmpty()) {
		return false;
	}

	int markerIndex = -1;
	const QString requestedMap = request.mapName.trimmed().toUpper();
	for (int index = 0; index < allLumps.size(); ++index) {
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
			*error = requestedMap.isEmpty() ? mapText("No Doom map marker was found in the WAD.") : mapText("Requested Doom map marker was not found.");
		}
		return false;
	}

	document->sourcePath = QFileInfo(request.path).absoluteFilePath();
	document->mapName = allLumps.at(markerIndex).name;
	document->format = LevelMapFormat::DoomWad;
	document->engineFamily = QStringLiteral("idTech1");
	document->editState = QStringLiteral("clean");
	document->doomWadMagic = magic.isEmpty() ? QStringLiteral("PWAD") : magic;
	for (int index = markerIndex; index < allLumps.size(); ++index) {
		if (index != markerIndex && isDoomMapMarkerAt(allLumps, index)) {
			break;
		}
		if (index == markerIndex || isDoomMapLumpName(allLumps.at(index).name)) {
			document->doomLumpOrder.push_back(allLumps.at(index).name);
			document->doomLumps.insert(allLumps.at(index).name, allLumps.at(index).bytes);
			continue;
		}
		break;
	}

	if (document->doomLumps.contains(QStringLiteral("TEXTMAP"))) {
		document->doomFormat = LevelMapDoomFormat::Udmf;
		addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("udmf-unsupported"),
			mapText("Map uses the textual UDMF format (TEXTMAP). VibeStudio detects it but cannot edit it yet."), document->mapName);
		return true;
	}

	// A BEHAVIOR lump means the map was compiled in Hexen format, which widens
	// linedef records to 16 bytes and thing records to 20 bytes.
	// https://doomwiki.org/wiki/Hexen_map_format
	document->doomFormat = document->doomLumps.contains(QStringLiteral("BEHAVIOR")) ? LevelMapDoomFormat::Hexen : LevelMapDoomFormat::Doom;
	if (document->doomFormat == LevelMapDoomFormat::Hexen) {
		addIssue(document, LevelMapIssueSeverity::Info, QStringLiteral("hexen-map-format"), mapText("Map uses the Hexen linedef/thing layout."), document->mapName);
	}

	const QStringList required = {QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"), QStringLiteral("VERTEXES"), QStringLiteral("SECTORS")};
	for (const QString& lump : required) {
		if (!document->doomLumps.contains(lump)) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("missing-doom-lump"), mapText("Required Doom map lump is missing: %1").arg(lump), lump);
		}
	}

	const QByteArray vertexBytes = document->doomLumps.value(QStringLiteral("VERTEXES"));
	for (qsizetype offset = 0; offset + 4 <= vertexBytes.size(); offset += 4) {
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
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-invalid-vertex"), mapText("Linedef references a vertex outside the VERTEXES lump."), linedefObjectId(linedef.id));
		}
		document->doomLinedefs.push_back(linedef);
	}
	warnTrailingBytes(document, QStringLiteral("LINEDEFS"), linedefBytes.size(), linedefStride, QStringLiteral("trailing-linedef-bytes"));

	const qsizetype thingStride = hexen ? 20 : 10;
	const QByteArray thingBytes = document->doomLumps.value(QStringLiteral("THINGS"));
	for (qsizetype offset = 0; offset + thingStride <= thingBytes.size(); offset += thingStride) {
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

		LevelMapEntity entity;
		entity.id = thing.id;
		entity.className = QStringLiteral("thing:%1").arg(thing.type);
		entity.origin = {thing.x, thing.y, thing.z, true};
		entity.properties = {
			{QStringLiteral("type"), QString::number(thing.type), 0},
			{QStringLiteral("angle"), QString::number(thing.angle), 0},
			{QStringLiteral("flags"), QString::number(thing.flags), 0},
			{QStringLiteral("origin"), serializeVec3(entity.origin), 0},
			{QStringLiteral("x"), QString::number(static_cast<int>(std::lround(thing.x))), 0},
			{QStringLiteral("y"), QString::number(static_cast<int>(std::lround(thing.y))), 0},
		};
		document->entities.push_back(entity);
	}
	warnTrailingBytes(document, QStringLiteral("THINGS"), thingBytes.size(), thingStride, QStringLiteral("trailing-thing-bytes"));

	const QByteArray sidedefBytes = document->doomLumps.value(QStringLiteral("SIDEDEFS"));
	for (qsizetype offset = 0; offset + 30 <= sidedefBytes.size(); offset += 30) {
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
		if (sidedef.sector < 0 || sidedef.sector >= document->doomSectors.size()) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("sidedef-invalid-sector"), mapText("Sidedef references a sector outside SECTORS."), sidedefObjectId(sidedef.id));
		}
	}

	if (document->doomVertices.isEmpty() || document->doomLinedefs.isEmpty()) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("empty-doom-geometry"), mapText("Doom map has no vertices or linedefs to preview."), document->mapName);
	}
	for (const LevelMapDoomLinedef& linedef : document->doomLinedefs) {
		if (linedef.frontSidedef >= document->doomSidedefs.size()) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-invalid-sidedef"), mapText("Linedef front sidedef is outside SIDEDEFS."), linedefObjectId(linedef.id));
		}
		if (linedef.backSidedef >= document->doomSidedefs.size()) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-invalid-sidedef"), mapText("Linedef back sidedef is outside SIDEDEFS."), linedefObjectId(linedef.id));
		}
	}
	validateDoomGeometry(document);
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
};

bool isMapPunct(QChar ch)
{
	return ch == QLatin1Char('{') || ch == QLatin1Char('}') || ch == QLatin1Char('(')
		|| ch == QLatin1Char(')') || ch == QLatin1Char('[') || ch == QLatin1Char(']');
}

QVector<MapToken> tokenizeMapText(const QString& text, QStringList* comments)
{
	QVector<MapToken> tokens;
	const int size = text.size();
	int line = 1;
	int index = 0;
	while (index < size) {
		const QChar ch = text.at(index);
		if (ch == QLatin1Char('\n')) {
			++line;
			++index;
			continue;
		}
		if (ch.isSpace()) {
			++index;
			continue;
		}
		if (ch == QLatin1Char('/') && index + 1 < size && text.at(index + 1) == QLatin1Char('/')) {
			const int start = index;
			while (index < size && text.at(index) != QLatin1Char('\n')) {
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
				if (text.at(index) == QLatin1Char('\n')) {
					++line;
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
			++index;
			const int start = index;
			while (index < size && text.at(index) != QLatin1Char('"') && text.at(index) != QLatin1Char('\n')) {
				++index;
			}
			tokens.push_back({MapTokenType::String, text.mid(start, index - start), line});
			if (index < size && text.at(index) == QLatin1Char('"')) {
				++index;
			}
			continue;
		}
		if (isMapPunct(ch)) {
			tokens.push_back({MapTokenType::Punct, QString(ch), line});
			++index;
			continue;
		}
		const int start = index;
		while (index < size) {
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
		tokens.push_back({MapTokenType::Word, text.mid(start, index - start), line});
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

int matchingBraceIndex(const QVector<MapToken>& tokens, int openIndex)
{
	int depth = 0;
	for (int index = openIndex; index < tokens.size(); ++index) {
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
bool readNumberGroup(const QVector<MapToken>& tokens, int* index, int limit, QVector<double>* values)
{
	if (!index || !values) {
		return false;
	}
	if (!tokenIsPunct(tokenAt(tokens, *index), QLatin1Char('('))) {
		return false;
	}
	++(*index);
	while (*index < limit) {
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
bool readBracketGroup(const QVector<MapToken>& tokens, int* index, int limit, QVector<double>* values)
{
	if (!index || !values) {
		return false;
	}
	if (!tokenIsPunct(tokenAt(tokens, *index), QLatin1Char('['))) {
		return false;
	}
	++(*index);
	while (*index < limit) {
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
bool readTextureMatrix(const QVector<MapToken>& tokens, int* index, int limit, std::array<double, 6>* matrix)
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
	if (!readNumberGroup(tokens, index, limit, &first) || first.size() != 3) {
		return false;
	}
	if (!readNumberGroup(tokens, index, limit, &second) || second.size() != 3) {
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

bool readFacePoints(const QVector<MapToken>& tokens, int* index, int limit, LevelMapBrushFace* face)
{
	LevelMapVec3* targets[3] = {&face->p0, &face->p1, &face->p2};
	for (auto& target : targets) {
		QVector<double> values;
		if (!readNumberGroup(tokens, index, limit, &values) || values.size() != 3) {
			return false;
		}
		*target = {values.at(0), values.at(1), values.at(2), true};
	}
	return true;
}

struct QuakeParseState {
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
		const MapToken& token = tokenAt(state->tokens, state->index);
		if (tokenIsPunct(token, QLatin1Char('}'))) {
			break;
		}
		LevelMapBrushFace face;
		face.id = brush->faces.size();
		face.line = token.line;
		if (!readFacePoints(state->tokens, &state->index, limit, &face)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brush-face-parse"), mapText("Brush face could not be parsed."), brushObjectId(brush->id), token.line);
			ok = false;
			break;
		}
		if (!readShaderName(state->tokens, &state->index, limit, &face.textureName)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brush-face-texture"), mapText("Brush face has no texture name."), brushObjectId(brush->id), token.line);
			ok = false;
			break;
		}
		if (tokenIsPunct(tokenAt(state->tokens, state->index), QLatin1Char('['))) {
			QVector<double> uValues;
			QVector<double> vValues;
			if (!readBracketGroup(state->tokens, &state->index, limit, &uValues) || uValues.size() != 4
				|| !readBracketGroup(state->tokens, &state->index, limit, &vValues) || vValues.size() != 4) {
				addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brush-face-valve220"), mapText("Valve 220 texture axes could not be parsed."), brushObjectId(brush->id), token.line);
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
				double value = 0.0;
				if (!tokenNumber(tokenAt(state->tokens, state->index), &value)) {
					break;
				}
				placement.push_back(value);
				++state->index;
			}
			if (placement.size() < 5) {
				addIssue(state->document, LevelMapIssueSeverity::Warning, QStringLiteral("brush-face-placement"), mapText("Brush face is missing texture placement numbers."), brushObjectId(brush->id), token.line);
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
		const MapToken& token = tokenAt(state->tokens, state->index);
		if (tokenIsPunct(token, QLatin1Char('}'))) {
			break;
		}
		LevelMapBrushFace face;
		face.id = brush->faces.size();
		face.line = token.line;
		if (!readFacePoints(state->tokens, &state->index, limit, &face)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef-face-parse"), mapText("brushDef face points could not be parsed."), brushObjectId(brush->id), token.line);
			return false;
		}
		if (!readTextureMatrix(state->tokens, &state->index, limit, &face.textureMatrix)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef-texture-matrix"), mapText("brushDef texture matrix could not be parsed."), brushObjectId(brush->id), token.line);
			return false;
		}
		face.explicitTextureMatrix = true;
		if (!readShaderName(state->tokens, &state->index, limit, &face.textureName)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef-shader"), mapText("brushDef face has no shader name."), brushObjectId(brush->id), token.line);
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
		const MapToken& token = tokenAt(state->tokens, state->index);
		if (tokenIsPunct(token, QLatin1Char('}'))) {
			break;
		}
		QVector<double> plane;
		if (!readNumberGroup(state->tokens, &state->index, limit, &plane) || plane.size() != 4) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-plane"), mapText("brushDef3 plane could not be parsed."), brushObjectId(brush->id), token.line);
			return false;
		}
		LevelMapBrushFace face;
		face.id = brush->faces.size();
		face.line = token.line;
		face.explicitPlane = true;
		const double length = std::sqrt((plane.at(0) * plane.at(0)) + (plane.at(1) * plane.at(1)) + (plane.at(2) * plane.at(2)));
		if (length < 1e-9) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-degenerate-plane"), mapText("brushDef3 plane has a zero normal."), brushObjectId(brush->id), token.line);
			return false;
		}
		const double nx = plane.at(0) / length;
		const double ny = plane.at(1) / length;
		const double nz = plane.at(2) / length;
		face.planeNormal = {nx, ny, nz, true};
		face.planeDistance = plane.at(3) / length;
		if (!readTextureMatrix(state->tokens, &state->index, limit, &face.textureMatrix)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-texture-matrix"), mapText("brushDef3 texture matrix could not be parsed."), brushObjectId(brush->id), token.line);
			return false;
		}
		face.explicitTextureMatrix = true;
		if (!readShaderName(state->tokens, &state->index, limit, &face.textureName)) {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-shader"), mapText("brushDef3 face has no shader name."), brushObjectId(brush->id), token.line);
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
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("brushdef3-degenerate-plane"), mapText("brushDef3 plane basis could not be built."), brushObjectId(brush->id), token.line);
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
	if (!readShaderName(state->tokens, &state->index, limit, &patch->textureName)) {
		return false;
	}
	QVector<double> header;
	if (!readNumberGroup(state->tokens, &state->index, limit, &header) || header.size() < 2) {
		return false;
	}
	patch->width = static_cast<int>(std::lround(header.at(0)));
	patch->height = static_cast<int>(std::lround(header.at(1)));
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
			QVector<double> values;
			if (!readNumberGroup(state->tokens, &state->index, limit, &values) || values.size() < 3) {
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
	const int closeIndex = matchingBraceIndex(state->tokens, openIndex);
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
					mapText("Patch declares %1x%2 control points but %3 were parsed.").arg(patch.width).arg(patch.height).arg(patch.controlPoints.size()),
					patchObjectId(patch.id), startLine);
			}
			patches->push_back(patch);
		} else {
			addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("patch-parse"), mapText("Patch definition could not be parsed."), patchObjectId(patch.id), startLine);
		}
	} else if (keyword == QStringLiteral("terraindef")) {
		++state->index;
		addIssue(state->document, LevelMapIssueSeverity::Info, QStringLiteral("terraindef-skipped"),
			mapText("terrainDef block was skipped; VibeStudio does not edit terrain primitives yet."), entityObjectId(entity->id), startLine);
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
	const int closeIndex = matchingBraceIndex(state->tokens, openIndex);
	LevelMapEntity entity;
	entity.id = state->document->entities.size();
	entity.startLine = tokenAt(state->tokens, openIndex).line;
	entity.endLine = closeIndex >= 0 ? tokenAt(state->tokens, closeIndex).line : 0;
	const int limit = closeIndex >= 0 ? closeIndex : state->tokens.size();
	++state->index;

	QVector<LevelMapBrush> brushes;
	QVector<LevelMapPatch> patches;
	while (state->index < limit) {
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
				addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("entity-key-missing-value"), mapText("Entity key has no value."), entityObjectId(entity.id), line);
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
		addIssue(state->document, LevelMapIssueSeverity::Warning, QStringLiteral("unexpected-token"), mapText("Unexpected token in entity body: %1").arg(token.text), entityObjectId(entity.id), token.line);
		++state->index;
	}

	if (closeIndex < 0) {
		addIssue(state->document, LevelMapIssueSeverity::Error, QStringLiteral("unterminated-entity"), mapText("Map ended before an entity closed."), entityObjectId(entity.id), entity.startLine);
	}
	if (entity.className.isEmpty()) {
		entity.className = mapText("entity");
		addIssue(state->document, LevelMapIssueSeverity::Warning, QStringLiteral("entity-missing-classname"), mapText("Entity is missing a classname."), entityObjectId(entity.id), entity.startLine);
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

void solveBrushBounds(LevelMapDocument* document)
{
	for (LevelMapBrush& brush : document->brushes) {
		brush.faceCount = brush.faces.size();
		if (brush.faces.isEmpty()) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("empty-brush"), mapText("Brush has no parsed faces."), brushObjectId(brush.id), brush.startLine);
			continue;
		}
		const MapBrushGeometry geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId);
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
			mapText("Brush could not be solved into a closed convex volume."), brushObjectId(brush.id), brush.startLine);
	}
}

bool parseQuakeMap(const LevelMapLoadRequest& request, LevelMapDocument* document, QString* error)
{
	QFile file(request.path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = mapText("Unable to open .map file.");
		}
		return false;
	}
	const QByteArray raw = file.readAll();
	const QString text = QString::fromUtf8(raw);
	document->sourcePath = QFileInfo(request.path).absoluteFilePath();
	document->mapName = QFileInfo(request.path).fileName();
	document->originalText = text;
	document->lineEnding = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
	document->textLines = text.split('\n');
	for (QString& line : document->textLines) {
		if (line.endsWith(QLatin1Char('\r'))) {
			line.chop(1);
		}
	}
	document->editState = QStringLiteral("clean");

	QStringList comments;
	QuakeParseState state;
	state.document = document;
	state.tokens = tokenizeMapText(text, &comments);
	for (const QString& comment : comments) {
		if (comment.contains(QStringLiteral("Q3Radiant"), Qt::CaseInsensitive)
			|| comment.contains(QStringLiteral("GtkRadiant"), Qt::CaseInsensitive)
			|| comment.contains(QStringLiteral("NetRadiant"), Qt::CaseInsensitive)) {
			state.sawRadiantHeader = true;
		}
	}

	while (state.index < state.tokens.size()) {
		const MapToken& token = tokenAt(state.tokens, state.index);
		if (tokenIsPunct(token, QLatin1Char('{'))) {
			parseEntityBlock(&state);
			continue;
		}
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("unexpected-token"), mapText("Unexpected token outside an entity: %1").arg(token.text), document->mapName, token.line);
		++state.index;
	}

	// Engine family: an explicit hint wins, otherwise use real evidence rather
	// than a substring guess over the whole file.
	const QString hint = normalizedId(request.engineHint);
	bool quake3 = false;
	if (!hint.isEmpty()) {
		quake3 = hint.contains(QStringLiteral("3"));
	} else {
		quake3 = state.sawBrushPrimitive || state.sawPatchPrimitive || state.sawQ3Key || state.sawRadiantHeader || state.sawShaderPath;
	}
	document->format = quake3 ? LevelMapFormat::Quake3Map : LevelMapFormat::QuakeMap;
	document->engineFamily = quake3 ? QStringLiteral("idTech3") : QStringLiteral("idTech2");

	solveBrushBounds(document);

	if (document->entities.isEmpty()) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("empty-map"), mapText(".map file contains no parsed entities."), document->mapName);
	}
	bool hasWorldspawn = false;
	for (const LevelMapEntity& entity : document->entities) {
		hasWorldspawn = hasWorldspawn || entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
	}
	if (!hasWorldspawn) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("missing-worldspawn"), mapText("Map has no worldspawn entity."), document->mapName);
	}

	EricwMapPreflightOptions preflightOptions;
	preflightOptions.mapPath = document->sourcePath;
	const EricwMapPreflightResult preflight = validateEricwMapPreflightText(text, preflightOptions);
	for (const EricwMapPreflightWarning& warning : preflight.warnings) {
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
	return true;
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
	for (const LevelMapDoomVertex& vertex : document.doomVertices) {
		if (!fitsDoomCoordinate(vertex.x) || !fitsDoomCoordinate(vertex.y)) {
			errors->push_back(mapText("Vertex %1 is outside the Doom 16-bit coordinate range.").arg(vertex.id));
			ok = false;
		}
	}
	for (const LevelMapDoomThing& thing : document.doomThings) {
		if (!fitsDoomCoordinate(thing.x) || !fitsDoomCoordinate(thing.y) || !fitsDoomCoordinate(thing.z)) {
			errors->push_back(mapText("Thing %1 is outside the Doom 16-bit coordinate range.").arg(thing.id));
			ok = false;
		}
	}
	for (const LevelMapDoomSector& sector : document.doomSectors) {
		if (!fitsDoomCoordinate(sector.floorHeight) || !fitsDoomCoordinate(sector.ceilingHeight)) {
			errors->push_back(mapText("Sector %1 height is outside the Doom 16-bit range.").arg(sector.id));
			ok = false;
		}
	}
	return ok;
}

bool writeDoomWad(const LevelMapDocument& document, const QString& outputPath, QStringList* errors, QStringList* warnings, QStringList* staleLumps)
{
	QString readError;
	QVector<WadLump> originalLumps = readWadLumps(document.sourcePath, nullptr, &readError);
	if (originalLumps.isEmpty()) {
		errors->push_back(readError.isEmpty() ? mapText("Unable to read the source WAD.") : readError);
		return false;
	}
	if (!validateDoomWriteRanges(document, errors)) {
		return false;
	}

	const bool hexen = document.doomFormat == LevelMapDoomFormat::Hexen;
	const bool udmf = document.doomFormat == LevelMapDoomFormat::Udmf;
	bool inRequestedMap = false;
	QStringList presentStale;
	for (int lumpIndex = 0; lumpIndex < originalLumps.size(); ++lumpIndex) {
		WadLump& lump = originalLumps[lumpIndex];
		if (isDoomMapMarkerAt(originalLumps, lumpIndex)) {
			inRequestedMap = lump.name.compare(document.mapName, Qt::CaseInsensitive) == 0;
			continue;
		}
		if (!inRequestedMap || udmf) {
			continue;
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
	if (udmf) {
		warnings->push_back(mapText("UDMF maps are copied through unchanged; VibeStudio cannot rewrite TEXTMAP yet."));
	} else if (document.doomGeometryChanged && !presentStale.isEmpty()) {
		*staleLumps = presentStale;
		warnings->push_back(mapText("Geometry changed: %1 no longer match the map. Run a node builder before playing it.").arg(presentStale.join(QStringLiteral(", "))));
	}

	QByteArray data;
	const QString magic = document.doomWadMagic == QStringLiteral("IWAD") ? QStringLiteral("IWAD") : QStringLiteral("PWAD");
	data.append(magic.toLatin1());
	appendLe32(&data, originalLumps.size());
	appendLe32(&data, 0);
	QVector<qint32> offsets;
	for (const WadLump& lump : originalLumps) {
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

	QSaveFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		errors->push_back(mapText("Unable to open output WAD."));
		return false;
	}
	if (file.write(data) != data.size() || !file.commit()) {
		errors->push_back(mapText("Unable to write output WAD."));
		return false;
	}
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
			.arg(mapCoordinateText(point.x), mapCoordinateText(point.y), mapCoordinateText(point.z));
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
		.arg(mapCoordinateText(normal.x), mapCoordinateText(normal.y), mapCoordinateText(normal.z), mapCoordinateText(distance));
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

QString serializeMapText(const LevelMapDocument& document)
{
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return document.originalText;
	}
	QStringList lines = document.textLines;
	QSet<int> deletedLines;
	QMap<int, QStringList> insertions;

	// Brush and patch geometry lives in the source text, not in a key/value pair,
	// so a moved brush has to be written back through its own face lines. Without
	// this the move is applied in memory and silently lost on save.
	for (const LevelMapBrush& brush : document.brushes) {
		if (!brush.geometryDirty) {
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
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (!patch.geometryDirty || patch.controlRowLines.isEmpty() || !patch.controlGridNormalized) {
			continue;
		}
		// controlRowLines holds one source line per parenthesised file group, and
		// a file group is a grid COLUMN of `height` points (see parsePatchBody).
		// controlPoints is row major, so walk the column with a `width` stride.
		for (int column = 0; column < patch.controlRowLines.size() && column < patch.width; ++column) {
			const int line = patch.controlRowLines.at(column);
			if (line <= 0 || line > lines.size()) {
				continue;
			}
			QVector<LevelMapVec3> points;
			QVector<double> u;
			QVector<double> v;
			for (int row = 0; row < patch.height; ++row) {
				const int index = (row * patch.width) + column;
				if (index >= patch.controlPoints.size()) {
					break;
				}
				points.push_back(patch.controlPoints.at(index));
				u.push_back(index < patch.controlU.size() ? patch.controlU.at(index) : 0.0);
				v.push_back(index < patch.controlV.size() ? patch.controlV.at(index) : 0.0);
			}
			lines[line - 1] = replacePatchRow(lines.at(line - 1), points, u, v);
		}
	}

	for (const LevelMapEntity& entity : document.entities) {
		if (entity.startLine <= 0 || entity.startLine > lines.size()) {
			continue;
		}
		for (const int removed : entity.removedPropertyLines) {
			if (removed > 0 && removed <= lines.size()) {
				deletedLines.insert(removed);
			}
		}
		int anchor = entity.startLine;
		for (const LevelMapProperty& property : entity.properties) {
			if (property.line <= 0 || property.line > lines.size()) {
				continue;
			}
			// Keep the original indentation of the line being replaced.
			lines[property.line - 1] = formatKeyLine(leadingWhitespace(lines.at(property.line - 1)), property.key, property.value);
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

	QStringList output;
	output.reserve(lines.size() + insertions.size());
	for (int index = 1; index <= lines.size(); ++index) {
		if (!deletedLines.contains(index)) {
			output.push_back(lines.at(index - 1));
		}
		const auto found = insertions.constFind(index);
		if (found != insertions.constEnd()) {
			output.append(found.value());
		}
	}
	return output.join(document.lineEnding.isEmpty() ? QStringLiteral("\n") : document.lineEnding);
}

bool writeTextMap(const LevelMapDocument& document, const QString& outputPath, QStringList* errors)
{
	// Binary mode: the document already carries the source line-ending style, so
	// QIODevice::Text must not translate anything on top of it.
	QSaveFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		errors->push_back(mapText("Unable to open output map."));
		return false;
	}
	const QByteArray bytes = serializeMapText(document).toUtf8();
	if (file.write(bytes) != bytes.size() || !file.commit()) {
		errors->push_back(mapText("Unable to write output map."));
		return false;
	}
	return true;
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

void pushUndo(LevelMapDocument* document, const LevelMapUndoCommand& command)
{
	if (!document) {
		return;
	}
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
bool applyLevelMapCommand(LevelMapDocument* document, const LevelMapUndoCommand& command, bool forward)
{
	if (!document) {
		return false;
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
			entity->removedPropertyLines.removeAll(command.propertyLine);
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
	return false;
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

QStringList truncatedLines(const QStringList& source, int limit, const QString& label)
{
	if (source.size() <= limit) {
		return source;
	}
	QStringList lines = source.mid(0, limit);
	lines << mapText("... showing %1 of %2 %3.").arg(limit).arg(source.size()).arg(label);
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
		return mapText("Unknown");
	case LevelMapFormat::DoomWad:
		return mapText("Doom WAD map");
	case LevelMapFormat::QuakeMap:
		return mapText("Quake-family MAP");
	case LevelMapFormat::Quake3Map:
		return mapText("Quake III MAP");
	}
	return mapText("Unknown");
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
		return mapText("Doom binary");
	case LevelMapDoomFormat::Hexen:
		return mapText("Hexen binary");
	case LevelMapDoomFormat::Udmf:
		return mapText("UDMF text");
	}
	return mapText("Doom binary");
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

bool loadLevelMap(const LevelMapLoadRequest& request, LevelMapDocument* document, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = mapText("Missing map document output.");
		}
		return false;
	}
	*document = {};
	if (request.path.trimmed().isEmpty()) {
		if (error) {
			*error = mapText("Map path is required.");
		}
		return false;
	}
	const QFileInfo info(request.path);
	if (!info.exists()) {
		if (error) {
			*error = mapText("Map path does not exist.");
		}
		return false;
	}
	const QString suffix = info.suffix().toLower();
	const QString hint = normalizedId(request.engineHint);
	if (suffix == QStringLiteral("wad") || hint.contains(QStringLiteral("doom")) || hint.contains(QStringLiteral("idtech1"))) {
		return parseDoomWad(request, document, error);
	}
	if (suffix == QStringLiteral("map")) {
		return parseQuakeMap(request, document, error);
	}
	if (error) {
		*error = mapText("Supported Milestone 7 map inputs are Doom WAD and Quake-family .map files.");
	}
	return false;
}

LevelMapStatistics levelMapStatistics(const LevelMapDocument& document)
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
	stats.uniqueTextureCount = uniqueTextureReferences(document).size();
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
	for (const LevelMapIssue& issue : document.issues) {
		if (issue.severity == LevelMapIssueSeverity::Error) {
			++stats.errorCount;
		} else if (issue.severity == LevelMapIssueSeverity::Warning) {
			++stats.warningCount;
		}
	}
	return stats;
}

QStringList levelMapStatisticsLines(const LevelMapDocument& document)
{
	const LevelMapStatistics stats = levelMapStatistics(document);
	QStringList lines;
	lines << mapText("Format: %1").arg(levelMapFormatDisplayName(document.format));
	if (document.format == LevelMapFormat::DoomWad) {
		lines << mapText("Map format: %1").arg(levelMapDoomFormatDisplayName(document.doomFormat));
		lines << mapText("WAD kind: %1").arg(document.doomWadMagic);
	}
	lines << mapText("Engine: %1").arg(document.engineFamily.isEmpty() ? mapText("unknown") : document.engineFamily);
	lines << mapText("Map: %1").arg(document.mapName.isEmpty() ? mapText("unknown") : document.mapName);
	lines << mapText("Entities: %1").arg(stats.entityCount);
	lines << mapText("Brushes: %1 (%2 solved, %3 degenerate)").arg(stats.brushCount).arg(stats.solvedBrushCount).arg(stats.degenerateBrushCount);
	lines << mapText("Brush faces: %1").arg(stats.brushFaceCount);
	lines << mapText("Patches: %1").arg(stats.patchCount);
	lines << mapText("Doom things: %1").arg(stats.doomThingCount);
	lines << mapText("Doom vertices: %1").arg(stats.doomVertexCount);
	lines << mapText("Doom linedefs: %1").arg(stats.doomLinedefCount);
	lines << mapText("Doom sidedefs: %1").arg(stats.doomSidedefCount);
	lines << mapText("Doom sectors: %1").arg(stats.doomSectorCount);
	lines << mapText("Texture references: %1 / %2 unique").arg(stats.textureReferenceCount).arg(stats.uniqueTextureCount);
	lines << mapText("Bounds min: %1").arg(vecText(stats.mins));
	lines << mapText("Bounds max: %1").arg(vecText(stats.maxs));
	lines << mapText("Issues: %1 warnings, %2 errors").arg(stats.warningCount).arg(stats.errorCount);
	return lines;
}

QStringList levelMapEntityLines(const LevelMapDocument& document)
{
	QStringList lines;
	for (const LevelMapEntity& entity : document.entities) {
		lines << QStringLiteral("%1 %2 origin=%3 props=%4")
			.arg(entityObjectId(entity.id), entity.className.isEmpty() ? mapText("entity") : entity.className, vecText(entity.origin))
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
		lines << mapText("No entities or things parsed.");
	}
	return lines;
}

QStringList levelMapTextureLines(const LevelMapDocument& document)
{
	const QStringList unique = uniqueTextureReferences(document);
	if (unique.isEmpty()) {
		return {mapText("No texture or material references parsed.")};
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
	for (const LevelMapIssue& issue : document.issues) {
		lines << QStringLiteral("%1 %2 %3%4%5")
			.arg(levelMapIssueSeverityId(issue.severity).toUpper(), issue.code, issue.message,
				issue.objectId.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(issue.objectId),
				issue.line > 0 ? mapText(" at line %1").arg(issue.line) : QString());
	}
	if (lines.isEmpty()) {
		lines << mapText("No validation problems.");
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
		lines << mapText("No Doom sectors parsed.");
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
		lines << mapText("No Doom sidedefs parsed.");
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
		lines << mapText("No Quake III patches parsed.");
	}
	return lines;
}

QStringList levelMapViewLines(const LevelMapDocument& document)
{
	QStringList lines;
	if (document.format == LevelMapFormat::DoomWad) {
		lines << mapText("[2D Doom map view]");
		QStringList linedefLines;
		for (const LevelMapDoomLinedef& linedef : document.doomLinedefs) {
			const LevelMapDoomVertex a = linedef.startVertex >= 0 && linedef.startVertex < document.doomVertices.size() ? document.doomVertices.at(linedef.startVertex) : LevelMapDoomVertex {};
			const LevelMapDoomVertex b = linedef.endVertex >= 0 && linedef.endVertex < document.doomVertices.size() ? document.doomVertices.at(linedef.endVertex) : LevelMapDoomVertex {};
			linedefLines << QStringLiteral("%1 %2 -> %3 sides=%4/%5 tag=%6 special=%7")
				.arg(linedefObjectId(linedef.id), doomPointText(a.x, a.y), doomPointText(b.x, b.y))
				.arg(linedef.frontSidedef)
				.arg(linedef.backSidedef)
				.arg(linedef.tag)
				.arg(linedef.special);
		}
		lines << truncatedLines(linedefLines, 24, mapText("linedefs"));
		QStringList thingLines;
		for (const LevelMapDoomThing& thing : document.doomThings) {
			thingLines << QStringLiteral("%1 thing type=%2 at %3").arg(thingObjectId(thing.id)).arg(thing.type).arg(doomPointText(thing.x, thing.y));
		}
		lines << truncatedLines(thingLines, 16, mapText("things"));
		QStringList sectorLines;
		for (const LevelMapDoomSector& sector : document.doomSectors) {
			sectorLines << QStringLiteral("%1 floor=%2 ceiling=%3 light=%4").arg(sectorObjectId(sector.id)).arg(sector.floorHeight).arg(sector.ceilingHeight).arg(sector.lightLevel);
		}
		lines << truncatedLines(sectorLines, 12, mapText("sectors"));
	} else {
		lines << mapText("[orthographic brush/entity preview]");
		QStringList brushLines;
		for (const LevelMapBrush& brush : document.brushes) {
			brushLines << QStringLiteral("%1 entity=%2 kind=%3 faces=%4 solved=%5 mins=%6 maxs=%7")
				.arg(brushObjectId(brush.id))
				.arg(brush.entityId)
				.arg(brush.primitiveKind.isEmpty() ? QStringLiteral("classic") : brush.primitiveKind)
				.arg(brush.faceCount)
				.arg(brush.boundsSolved ? mapText("yes") : mapText("no"))
				.arg(vecText(brush.mins), vecText(brush.maxs));
		}
		lines << truncatedLines(brushLines, 24, mapText("brushes"));
		QStringList patchLines;
		for (const LevelMapPatch& patch : document.patches) {
			patchLines << QStringLiteral("%1 shader=%2 grid=%3x%4").arg(patchObjectId(patch.id), patch.textureName).arg(patch.width).arg(patch.height);
		}
		if (!document.patches.isEmpty()) {
			lines << truncatedLines(patchLines, 12, mapText("patches"));
		}
		QStringList entityLines;
		for (const LevelMapEntity& entity : document.entities) {
			entityLines << QStringLiteral("%1 %2 origin=%3").arg(entityObjectId(entity.id), entity.className, vecText(entity.origin));
		}
		lines << truncatedLines(entityLines, 16, mapText("entities"));
	}
	if (lines.size() == 1) {
		lines << mapText("No geometry available for the current map preview.");
	}
	return lines;
}

QStringList levelMapSelectionLines(const LevelMapDocument& document)
{
	if (document.selectionKind == LevelMapSelectionKind::None || document.selectedObjectId < 0) {
		return {mapText("Selection: none")};
	}
	QStringList lines;
	lines << mapText("Selection: %1:%2").arg(levelMapSelectionKindId(document.selectionKind)).arg(document.selectedObjectId);
	if (document.selectionKind == LevelMapSelectionKind::QuakeBrush) {
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id != document.selectedObjectId) {
				continue;
			}
			lines << mapText("Brush kind: %1").arg(brush.primitiveKind.isEmpty() ? QStringLiteral("classic") : brush.primitiveKind);
			lines << mapText("Faces: %1").arg(brush.faceCount);
			lines << mapText("Bounds: %1 .. %2 (%3)").arg(vecText(brush.mins), vecText(brush.maxs), brush.boundsSolved ? mapText("solved") : mapText("unsolved"));
			lines << mapText("Textures: %1").arg(brush.textureNames.join(QStringLiteral(", ")));
		}
	} else if (document.selectionKind == LevelMapSelectionKind::QuakePatch) {
		for (const LevelMapPatch& patch : document.patches) {
			if (patch.id != document.selectedObjectId) {
				continue;
			}
			lines << mapText("Patch shader: %1").arg(patch.textureName);
			lines << mapText("Control grid: %1 x %2 (%3 points)").arg(patch.width).arg(patch.height).arg(patch.controlPoints.size());
			lines << mapText("Bounds: %1 .. %2").arg(vecText(patch.mins), vecText(patch.maxs));
		}
	} else if (document.selectionKind == LevelMapSelectionKind::DoomSector) {
		for (const LevelMapDoomSector& sector : document.doomSectors) {
			if (sector.id != document.selectedObjectId) {
				continue;
			}
			lines << mapText("Sector heights: %1 .. %2").arg(sector.floorHeight).arg(sector.ceilingHeight);
			lines << mapText("Flats: %1 / %2").arg(sector.floorTexture, sector.ceilingTexture);
			lines << mapText("Light/special/tag: %1 / %2 / %3").arg(sector.lightLevel).arg(sector.special).arg(sector.tag);
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
			lines << mapText("Entity: %1").arg(entity.className);
			for (const LevelMapProperty& property : entity.properties) {
				lines << QStringLiteral("%1 = %2").arg(property.key, property.value);
			}
			return lines;
		}
	}
	if (document.selectionKind == LevelMapSelectionKind::DoomVertex && document.selectedObjectId >= 0 && document.selectedObjectId < document.doomVertices.size()) {
		const LevelMapDoomVertex& vertex = document.doomVertices.at(document.selectedObjectId);
		return {mapText("Vertex: %1").arg(vertexObjectId(vertex.id)), mapText("Position: %1").arg(doomPointText(vertex.x, vertex.y))};
	}
	if (document.selectionKind == LevelMapSelectionKind::DoomLinedef && document.selectedObjectId >= 0 && document.selectedObjectId < document.doomLinedefs.size()) {
		const LevelMapDoomLinedef& linedef = document.doomLinedefs.at(document.selectedObjectId);
		QStringList result;
		result << mapText("Linedef: %1").arg(linedefObjectId(linedef.id));
		result << mapText("Vertices: %1 -> %2").arg(linedef.startVertex).arg(linedef.endVertex);
		result << mapText("Sidedefs: %1 / %2").arg(linedef.frontSidedef).arg(linedef.backSidedef);
		result << mapText("Special/tag: %1 / %2").arg(linedef.special).arg(linedef.tag);
		if (document.doomFormat == LevelMapDoomFormat::Hexen) {
			result << mapText("Args: %1 %2 %3 %4 %5").arg(linedef.args[0]).arg(linedef.args[1]).arg(linedef.args[2]).arg(linedef.args[3]).arg(linedef.args[4]);
		}
		return result;
	}
	if (document.selectionKind == LevelMapSelectionKind::DoomThing && document.selectedObjectId >= 0 && document.selectedObjectId < document.doomThings.size()) {
		const LevelMapDoomThing& thing = document.doomThings.at(document.selectedObjectId);
		QStringList result;
		result << mapText("Thing: %1").arg(thingObjectId(thing.id));
		result << mapText("Type: %1").arg(thing.type);
		result << mapText("Position: %1").arg(doomPointText(thing.x, thing.y));
		if (document.doomFormat == LevelMapDoomFormat::Hexen) {
			result << mapText("Height: %1").arg(thing.z, 0, 'f', 1);
			result << mapText("Thing id: %1").arg(thing.tid);
			result << mapText("Special: %1").arg(thing.special);
			result << mapText("Args: %1 %2 %3 %4 %5").arg(thing.args[0]).arg(thing.args[1]).arg(thing.args[2]).arg(thing.args[3]).arg(thing.args[4]);
		}
		return result;
	}
	if (document.selectionKind == LevelMapSelectionKind::DoomSector && document.selectedObjectId >= 0 && document.selectedObjectId < document.doomSectors.size()) {
		const LevelMapDoomSector& sector = document.doomSectors.at(document.selectedObjectId);
		return {
			mapText("Sector: %1").arg(sectorObjectId(sector.id)),
			mapText("floorheight = %1").arg(sector.floorHeight),
			mapText("ceilingheight = %1").arg(sector.ceilingHeight),
			mapText("floortexture = %1").arg(sector.floorTexture),
			mapText("ceilingtexture = %1").arg(sector.ceilingTexture),
			mapText("lightlevel = %1").arg(sector.lightLevel),
			mapText("special = %1").arg(sector.special),
			mapText("tag = %1").arg(sector.tag),
		};
	}
	if (document.selectionKind == LevelMapSelectionKind::QuakeBrush || document.selectionKind == LevelMapSelectionKind::QuakePatch) {
		return levelMapSelectionLines(document);
	}
	return {mapText("No property inspector data for the current selection.")};
}

QStringList levelMapUndoLines(const LevelMapDocument& document)
{
	QStringList lines;
	lines << mapText("Edit state: %1").arg(document.editState);
	lines << mapText("Undo commands: %1").arg(document.undoStack.size());
	lines << mapText("Redo commands: %1").arg(document.redoStack.size());
	lines << mapText("Undo depth limit: %1").arg(document.undoLimit);
	if (!document.undoStack.isEmpty()) {
		lines << mapText("Next undo: %1").arg(document.undoStack.back().undoDescription);
	}
	if (!document.redoStack.isEmpty()) {
		lines << mapText("Next redo: %1").arg(document.redoStack.back().description);
	}
	return lines;
}

QString levelMapReportText(const LevelMapDocument& document)
{
	QStringList lines;
	lines << mapText("Level map");
	lines << mapText("Source: %1").arg(document.sourcePath);
	lines << mapText("Format: %1").arg(levelMapFormatId(document.format));
	if (document.format == LevelMapFormat::DoomWad) {
		lines << mapText("Doom map format: %1").arg(levelMapDoomFormatId(document.doomFormat));
	}
	lines << mapText("Map: %1").arg(document.mapName);
	lines << mapText("Statistics:");
	for (const QString& line : levelMapStatisticsLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << mapText("View:");
	for (const QString& line : levelMapViewLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << mapText("Selection:");
	for (const QString& line : levelMapSelectionLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << mapText("Properties:");
	for (const QString& line : levelMapPropertyLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	if (!document.patches.isEmpty()) {
		lines << mapText("Patches:");
		for (const QString& line : levelMapPatchLines(document)) {
			lines << QStringLiteral("- %1").arg(line);
		}
	}
	if (!document.doomSectors.isEmpty()) {
		lines << mapText("Sectors:");
		for (const QString& line : truncatedLines(levelMapSectorLines(document), 24, mapText("sectors"))) {
			lines << QStringLiteral("- %1").arg(line);
		}
	}
	if (!document.doomSidedefs.isEmpty()) {
		lines << mapText("Sidedefs:");
		for (const QString& line : truncatedLines(levelMapSidedefLines(document), 24, mapText("sidedefs"))) {
			lines << QStringLiteral("- %1").arg(line);
		}
	}
	lines << mapText("Textures/materials:");
	for (const QString& line : levelMapTextureLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << mapText("Validation:");
	for (const QString& line : levelMapValidationLines(document)) {
		lines << QStringLiteral("- %1").arg(line);
	}
	lines << mapText("Undo/redo:");
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
			*error = mapText("Missing map document.");
		}
		return false;
	}
	const QStringList parts = selector.trimmed().split(':', Qt::SkipEmptyParts);
	if (parts.size() != 2) {
		if (error) {
			*error = mapText("Selection must be formatted as kind:id.");
		}
		return false;
	}
	const QString kind = normalizedId(parts.value(0));
	bool ok = false;
	const int id = parts.value(1).toInt(&ok);
	if (!ok || id < 0) {
		if (error) {
			*error = mapText("Selection id must be a non-negative integer.");
		}
		return false;
	}
	clearSelectionFlags(document);

	if (kind == QStringLiteral("entity")) {
		LevelMapEntity* entity = entityById(document, id);
		if (!entity) {
			if (error) {
				*error = mapText("Entity selection was not found.");
			}
			return false;
		}
		entity->selected = true;
		document->selectionKind = LevelMapSelectionKind::Entity;
	} else if (kind == QStringLiteral("vertex")) {
		LevelMapDoomVertex* vertex = vertexById(document, id);
		if (!vertex) {
			if (error) {
				*error = mapText("Vertex selection was not found.");
			}
			return false;
		}
		vertex->selected = true;
		document->selectionKind = LevelMapSelectionKind::DoomVertex;
	} else if (kind == QStringLiteral("linedef")) {
		LevelMapDoomLinedef* linedef = linedefById(document, id);
		if (!linedef) {
			if (error) {
				*error = mapText("Linedef selection was not found.");
			}
			return false;
		}
		linedef->selected = true;
		document->selectionKind = LevelMapSelectionKind::DoomLinedef;
	} else if (kind == QStringLiteral("thing")) {
		LevelMapDoomThing* thing = thingById(document, id);
		if (!thing) {
			if (error) {
				*error = mapText("Thing selection was not found.");
			}
			return false;
		}
		thing->selected = true;
		document->selectionKind = LevelMapSelectionKind::DoomThing;
	} else if (kind == QStringLiteral("sector")) {
		LevelMapDoomSector* sector = sectorById(document, id);
		if (!sector) {
			if (error) {
				*error = mapText("Sector selection was not found.");
			}
			return false;
		}
		sector->selected = true;
		document->selectionKind = LevelMapSelectionKind::DoomSector;
	} else if (kind == QStringLiteral("brush")) {
		LevelMapBrush* brush = brushById(document, id);
		if (!brush) {
			if (error) {
				*error = mapText("Brush selection was not found.");
			}
			return false;
		}
		brush->selected = true;
		document->selectionKind = LevelMapSelectionKind::QuakeBrush;
	} else if (kind == QStringLiteral("patch")) {
		LevelMapPatch* patch = patchById(document, id);
		if (!patch) {
			if (error) {
				*error = mapText("Patch selection was not found.");
			}
			return false;
		}
		patch->selected = true;
		document->selectionKind = LevelMapSelectionKind::QuakePatch;
	} else {
		if (error) {
			*error = mapText("Unknown selection kind.");
		}
		return false;
	}
	document->selectedObjectId = id;
	return true;
}

bool setLevelMapEntityProperty(LevelMapDocument* document, int entityId, const QString& key, const QString& value, QString* error)
{
	if (error) {
		error->clear();
	}
	LevelMapEntity* entity = entityById(document, entityId);
	if (!entity) {
		if (error) {
			*error = mapText("Entity not found.");
		}
		return false;
	}
	const QString trimmedKey = key.trimmed();
	if (trimmedKey.isEmpty()) {
		if (error) {
			*error = mapText("Entity property key is required.");
		}
		return false;
	}
	const int index = propertyIndex(*entity, trimmedKey);
	LevelMapUndoCommand command;
	command.commandKind = QStringLiteral("set-property");
	command.objectKind = QStringLiteral("entity");
	command.objectId = entityId;
	command.entityId = entityId;
	command.key = trimmedKey;
	command.keyExisted = index >= 0;
	command.oldValue = index >= 0 ? entity->properties.at(index).value : QString();
	command.newValue = value;
	command.description = mapText("Set %1 on entity %2").arg(trimmedKey).arg(entityId);
	command.undoDescription = mapText("Restore %1 on entity %2").arg(trimmedKey).arg(entityId);

	const bool doomThing = document->format == LevelMapFormat::DoomWad && entityId >= 0 && entityId < document->doomThings.size();
	if (doomThing) {
		command.hasThingSnapshot = true;
		command.oldThing = document->doomThings.at(entityId);
	}

	if (index >= 0) {
		entity->properties[index].value = value;
	} else {
		entity->properties.push_back({trimmedKey, value, 0});
	}
	if (trimmedKey.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
		entity->className = value;
	} else if (trimmedKey.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
		entity->origin = parseVec3(value);
	}
	if (doomThing) {
		LevelMapDoomThing& thing = document->doomThings[entityId];
		bool ok = false;
		if (trimmedKey.compare(QStringLiteral("type"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.type = parsed;
				entity->className = QStringLiteral("thing:%1").arg(parsed);
			}
		} else if (trimmedKey.compare(QStringLiteral("angle"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.angle = parsed;
			}
		} else if (trimmedKey.compare(QStringLiteral("flags"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.flags = parsed;
			}
		} else if (trimmedKey.compare(QStringLiteral("tid"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.tid = parsed;
			}
		} else if (trimmedKey.compare(QStringLiteral("special"), Qt::CaseInsensitive) == 0) {
			const int parsed = value.toInt(&ok);
			if (ok) {
				thing.special = parsed;
			}
		} else if (trimmedKey.compare(QStringLiteral("x"), Qt::CaseInsensitive) == 0) {
			const double parsed = value.toDouble(&ok);
			if (ok) {
				thing.x = parsed;
				entity->origin = {thing.x, thing.y, thing.z, true};
			}
		} else if (trimmedKey.compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0) {
			const double parsed = value.toDouble(&ok);
			if (ok) {
				thing.y = parsed;
				entity->origin = {thing.x, thing.y, thing.z, true};
			}
		} else if (trimmedKey.compare(QStringLiteral("z"), Qt::CaseInsensitive) == 0) {
			const double parsed = value.toDouble(&ok);
			if (ok) {
				thing.z = parsed;
				entity->origin = {thing.x, thing.y, thing.z, true};
			}
		} else if (trimmedKey.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0 && entity->origin.valid) {
			thing.x = entity->origin.x;
			thing.y = entity->origin.y;
			thing.z = entity->origin.z;
		}
		syncDoomThingEntity(document, entityId);
		command.newThing = document->doomThings.at(entityId);
	}
	pushUndo(document, command);
	selectLevelMapObject(document, entityObjectId(entityId));
	return true;
}

bool removeLevelMapEntityProperty(LevelMapDocument* document, int entityId, const QString& key, QString* error)
{
	if (error) {
		error->clear();
	}
	LevelMapEntity* entity = entityById(document, entityId);
	if (!entity) {
		if (error) {
			*error = mapText("Entity not found.");
		}
		return false;
	}
	const QString trimmedKey = key.trimmed();
	const int index = propertyIndex(*entity, trimmedKey);
	if (index < 0) {
		if (error) {
			*error = mapText("Entity does not have that key.");
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
	command.description = mapText("Remove %1 from entity %2").arg(trimmedKey).arg(entityId);
	command.undoDescription = mapText("Restore %1 on entity %2").arg(trimmedKey).arg(entityId);

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

bool setLevelMapSectorProperty(LevelMapDocument* document, int sectorId, const QString& key, const QString& value, QString* error)
{
	if (error) {
		error->clear();
	}
	LevelMapDoomSector* sector = sectorById(document, sectorId);
	if (!sector) {
		if (error) {
			*error = mapText("Sector not found.");
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
	command.description = mapText("Set %1 on sector %2").arg(normalizedKey).arg(sectorId);
	command.undoDescription = mapText("Restore %1 on sector %2").arg(normalizedKey).arg(sectorId);

	bool ok = false;
	const int intValue = value.trimmed().toInt(&ok);
	if (normalizedKey == QStringLiteral("floorheight") || normalizedKey == QStringLiteral("heightfloor")) {
		if (!ok) {
			if (error) {
				*error = mapText("Sector height must be an integer.");
			}
			return false;
		}
		sector->floorHeight = intValue;
	} else if (normalizedKey == QStringLiteral("ceilingheight") || normalizedKey == QStringLiteral("heightceiling")) {
		if (!ok) {
			if (error) {
				*error = mapText("Sector height must be an integer.");
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
				*error = mapText("Sector light level must be an integer.");
			}
			return false;
		}
		sector->lightLevel = intValue;
	} else if (normalizedKey == QStringLiteral("special")) {
		if (!ok) {
			if (error) {
				*error = mapText("Sector special must be an integer.");
			}
			return false;
		}
		sector->special = intValue;
	} else if (normalizedKey == QStringLiteral("tag") || normalizedKey == QStringLiteral("id")) {
		if (!ok) {
			if (error) {
				*error = mapText("Sector tag must be an integer.");
			}
			return false;
		}
		sector->tag = intValue;
	} else {
		if (error) {
			*error = mapText("Sector keys are floorheight, ceilingheight, floortexture, ceilingtexture, lightlevel, special and tag.");
		}
		return false;
	}
	command.newSector = *sector;
	document->doomGeometryChanged = true;
	pushUndo(document, command);
	selectLevelMapObject(document, sectorObjectId(sectorId));
	return true;
}

bool setLevelMapSidedefProperty(LevelMapDocument* document, int sidedefId, const QString& key, const QString& value, QString* error)
{
	if (error) {
		error->clear();
	}
	LevelMapDoomSidedef* sidedef = sidedefById(document, sidedefId);
	if (!sidedef) {
		if (error) {
			*error = mapText("Sidedef not found.");
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
	command.description = mapText("Set %1 on sidedef %2").arg(normalizedKey).arg(sidedefId);
	command.undoDescription = mapText("Restore %1 on sidedef %2").arg(normalizedKey).arg(sidedefId);

	bool ok = false;
	const int intValue = value.trimmed().toInt(&ok);
	if (normalizedKey == QStringLiteral("sector")) {
		if (!ok || intValue < 0 || intValue >= document->doomSectors.size()) {
			if (error) {
				*error = mapText("Sidedef sector must be an existing sector index.");
			}
			return false;
		}
		sidedef->sector = intValue;
	} else if (normalizedKey == QStringLiteral("offsetx")) {
		if (!ok) {
			if (error) {
				*error = mapText("Sidedef offset must be an integer.");
			}
			return false;
		}
		sidedef->offsetX = intValue;
	} else if (normalizedKey == QStringLiteral("offsety")) {
		if (!ok) {
			if (error) {
				*error = mapText("Sidedef offset must be an integer.");
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
			*error = mapText("Sidedef keys are sector, offsetx, offsety, upper, lower and middle.");
		}
		return false;
	}
	command.newSidedef = *sidedef;
	document->doomGeometryChanged = true;
	pushUndo(document, command);
	return true;
}

bool moveLevelMapObject(LevelMapDocument* document, const QString& objectKind, int objectId, double dx, double dy, double dz, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		if (error) {
			*error = mapText("Missing map document.");
		}
		return false;
	}
	const QString kind = normalizedId(objectKind);
	LevelMapUndoCommand command;
	command.description = mapText("Move %1:%2 by %3,%4,%5").arg(kind).arg(objectId).arg(dx).arg(dy).arg(dz);
	command.undoDescription = mapText("Move %1:%2 back").arg(kind).arg(objectId);
	command.commandKind = QStringLiteral("move");
	command.objectKind = kind;
	command.objectId = objectId;
	command.delta = {dx, dy, dz, true};

	if (kind == QStringLiteral("entity")) {
		LevelMapEntity* entity = entityById(document, objectId);
		if (!entity) {
			if (error) {
				*error = mapText("Entity not found.");
			}
			return false;
		}
		const int index = propertyIndex(*entity, QStringLiteral("origin"));
		command.originSynthesized = index < 0;
		LevelMapVec3 origin = entity->origin.valid ? entity->origin : parseVec3(propertyValue(*entity, QStringLiteral("origin")));
		if (!origin.valid) {
			origin = {0.0, 0.0, 0.0, true};
		}
		command.oldValue = serializeVec3(origin);
		origin.x += dx;
		origin.y += dy;
		origin.z += dz;
		command.newValue = serializeVec3(origin);
		command.key = QStringLiteral("origin");
		if (index >= 0) {
			entity->properties[index].value = command.newValue;
		} else {
			entity->properties.push_back({QStringLiteral("origin"), command.newValue, 0});
		}
		entity->origin = origin;
		if (document->format == LevelMapFormat::DoomWad && objectId >= 0 && objectId < document->doomThings.size()) {
			document->doomThings[objectId].x = origin.x;
			document->doomThings[objectId].y = origin.y;
			document->doomThings[objectId].z = origin.z;
			syncDoomThingEntity(document, objectId);
		}
		pushUndo(document, command);
		selectLevelMapObject(document, entityObjectId(objectId));
		return true;
	}

	if (kind == QStringLiteral("vertex") || kind == QStringLiteral("linedef") || kind == QStringLiteral("thing")
		|| kind == QStringLiteral("brush") || kind == QStringLiteral("patch")) {
		if (!applyMoveDelta(document, kind, objectId, dx, dy, dz)) {
			if (error) {
				*error = mapText("Move target was not found.");
			}
			return false;
		}
		if (kind == QStringLiteral("vertex") || kind == QStringLiteral("linedef")) {
			document->doomGeometryChanged = true;
		}
		pushUndo(document, command);
		if (kind == QStringLiteral("vertex")) {
			selectLevelMapObject(document, vertexObjectId(objectId));
		} else if (kind == QStringLiteral("linedef")) {
			selectLevelMapObject(document, linedefObjectId(objectId));
		} else if (kind == QStringLiteral("thing")) {
			selectLevelMapObject(document, thingObjectId(objectId));
		} else if (kind == QStringLiteral("brush")) {
			selectLevelMapObject(document, brushObjectId(objectId));
		} else {
			selectLevelMapObject(document, patchObjectId(objectId));
		}
		return true;
	}

	if (error) {
		*error = mapText("Move supports entity, vertex, linedef, thing, brush or patch.");
	}
	return false;
}

bool undoLevelMapEdit(LevelMapDocument* document, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || document->undoStack.isEmpty()) {
		if (error) {
			*error = mapText("No undo command is available.");
		}
		return false;
	}
	const LevelMapUndoCommand command = document->undoStack.takeLast();
	if (!applyLevelMapCommand(document, command, false)) {
		// Put the command back so the stacks stay consistent.
		document->undoStack.push_back(command);
		if (error) {
			*error = mapText("Undo target is no longer present in the map.");
		}
		return false;
	}
	document->redoStack.push_back(command);
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
			*error = mapText("No redo command is available.");
		}
		return false;
	}
	const LevelMapUndoCommand command = document->redoStack.takeLast();
	if (!applyLevelMapCommand(document, command, true)) {
		document->redoStack.push_back(command);
		if (error) {
			*error = mapText("Redo target is no longer present in the map.");
		}
		return false;
	}
	document->undoStack.push_back(command);
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
	LevelMapSaveReport report;
	report.sourcePath = document.sourcePath;
	report.outputPath = QFileInfo(outputPath).absoluteFilePath();
	report.mapName = document.mapName;
	report.format = document.format;
	report.dryRun = dryRun;
	report.editState = document.editState;
	report.summaryLines = levelMapStatisticsLines(document);
	if (outputPath.trimmed().isEmpty()) {
		report.errors << mapText("Output path is required.");
		return report;
	}
	const QFileInfo outputInfo(report.outputPath);
	if (outputInfo.exists() && !overwriteExisting && !dryRun) {
		report.errors << mapText("Output already exists. Use overwrite to replace it.");
		return report;
	}
	if (dryRun) {
		report.warnings << (outputInfo.exists() ? mapText("Would overwrite map output.") : mapText("Would write map output."));
		if (document.format == LevelMapFormat::DoomWad && document.doomGeometryChanged) {
			report.warnings << mapText("Geometry changed: a node build will be required after saving.");
		}
		return report;
	}
	if (!QDir().mkpath(outputInfo.absolutePath())) {
		report.errors << mapText("Unable to create output directory.");
		return report;
	}
	bool ok = false;
	if (document.format == LevelMapFormat::DoomWad) {
		ok = writeDoomWad(document, report.outputPath, &report.errors, &report.warnings, &report.staleLumps);
	} else {
		ok = writeTextMap(document, report.outputPath, &report.errors);
	}
	if (!ok) {
		if (report.errors.isEmpty()) {
			report.errors << mapText("Unable to save level map.");
		}
		return report;
	}
	report.written = true;
	report.editState = QStringLiteral("saved");
	return report;
}

QString levelMapSaveReportText(const LevelMapSaveReport& report)
{
	QStringList lines;
	lines << mapText("Level map save-as");
	lines << mapText("Source: %1").arg(report.sourcePath);
	lines << mapText("Output: %1").arg(report.outputPath);
	lines << mapText("Map: %1").arg(report.mapName);
	lines << mapText("Format: %1").arg(levelMapFormatId(report.format));
	lines << mapText("Mode: %1").arg(report.dryRun ? mapText("dry run") : mapText("write"));
	lines << mapText("Written: %1").arg(report.written ? mapText("yes") : mapText("no"));
	lines << mapText("Edit state: %1").arg(report.editState);
	for (const QString& line : report.summaryLines) {
		lines << QStringLiteral("- %1").arg(line);
	}
	if (!report.staleLumps.isEmpty()) {
		lines << mapText("Stale lumps needing a node build: %1").arg(report.staleLumps.join(QStringLiteral(", ")));
	}
	for (const QString& warning : report.warnings) {
		lines << mapText("Warning: %1").arg(warning);
	}
	for (const QString& error : report.errors) {
		lines << mapText("Error: %1").arg(error);
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
	request.outputPath = outputPath;
	request.workingDirectory = QFileInfo(request.inputPath).absolutePath();
	request.workspaceRootPath = request.workingDirectory;
	return request;
}

} // namespace vibestudio
