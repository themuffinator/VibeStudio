#include "core/level_map.h"

#include "core/ericw_map_preflight.h"

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

bool isDoomMapMarker(const QString& name)
{
	static const QRegularExpression marker(QStringLiteral(R"(^(E[1-9]M[1-9]|MAP[0-9][0-9])$)"), QRegularExpression::CaseInsensitiveOption);
	return marker.match(name.trimmed()).hasMatch();
}

bool isDoomMapLumpName(const QString& name)
{
	const QString upper = name.trimmed().toUpper();
	return QStringList {
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
	}.contains(upper);
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

struct WadLump {
	QString name;
	QByteArray bytes;
};

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
		if (!isDoomMapMarker(allLumps.at(index).name)) {
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
	for (int index = markerIndex; index < allLumps.size(); ++index) {
		if (index != markerIndex && isDoomMapMarker(allLumps.at(index).name)) {
			break;
		}
		if (index == markerIndex || isDoomMapLumpName(allLumps.at(index).name)) {
			document->doomLumpOrder.push_back(allLumps.at(index).name);
			document->doomLumps.insert(allLumps.at(index).name, allLumps.at(index).bytes);
		}
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
	if (vertexBytes.size() % 4 != 0) {
		addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("trailing-vertex-bytes"), mapText("VERTEXES lump has trailing bytes."), QStringLiteral("VERTEXES"));
	}

	const QByteArray linedefBytes = document->doomLumps.value(QStringLiteral("LINEDEFS"));
	for (qsizetype offset = 0; offset + 14 <= linedefBytes.size(); offset += 14) {
		LevelMapDoomLinedef linedef;
		linedef.id = document->doomLinedefs.size();
		linedef.startVertex = readLe16(linedefBytes, offset);
		linedef.endVertex = readLe16(linedefBytes, offset + 2);
		linedef.flags = readLe16(linedefBytes, offset + 4);
		linedef.special = readLe16(linedefBytes, offset + 6);
		linedef.tag = readLe16(linedefBytes, offset + 8);
		linedef.frontSidedef = readLe16(linedefBytes, offset + 10);
		linedef.backSidedef = readLe16(linedefBytes, offset + 12);
		if (linedef.backSidedef == 0xffff) {
			linedef.backSidedef = -1;
		}
		if (linedef.startVertex < 0 || linedef.startVertex >= document->doomVertices.size() || linedef.endVertex < 0 || linedef.endVertex >= document->doomVertices.size()) {
			addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("linedef-invalid-vertex"), mapText("Linedef references a vertex outside the VERTEXES lump."), linedefObjectId(linedef.id));
		}
		document->doomLinedefs.push_back(linedef);
	}

	const QByteArray thingBytes = document->doomLumps.value(QStringLiteral("THINGS"));
	for (qsizetype offset = 0; offset + 10 <= thingBytes.size(); offset += 10) {
		LevelMapDoomThing thing;
		thing.id = document->doomThings.size();
		thing.x = readLe16Signed(thingBytes, offset);
		thing.y = readLe16Signed(thingBytes, offset + 2);
		thing.angle = readLe16(thingBytes, offset + 4);
		thing.type = readLe16(thingBytes, offset + 6);
		thing.flags = readLe16(thingBytes, offset + 8);
		document->doomThings.push_back(thing);

		LevelMapEntity entity;
		entity.id = thing.id;
		entity.className = QStringLiteral("thing:%1").arg(thing.type);
		entity.origin = {thing.x, thing.y, 0.0, true};
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

	const QByteArray sidedefBytes = document->doomLumps.value(QStringLiteral("SIDEDEFS"));
	for (qsizetype offset = 0; offset + 30 <= sidedefBytes.size(); offset += 30) {
		LevelMapDoomSidedef sidedef;
		sidedef.id = document->doomSidedefs.size();
		sidedef.upperTexture = fixedLatin1(sidedefBytes, offset + 4, 8);
		sidedef.lowerTexture = fixedLatin1(sidedefBytes, offset + 12, 8);
		sidedef.middleTexture = fixedLatin1(sidedefBytes, offset + 20, 8);
		sidedef.sector = readLe16(sidedefBytes, offset + 28);
		for (const QString& texture : {sidedef.upperTexture, sidedef.lowerTexture, sidedef.middleTexture}) {
			if (!texture.isEmpty() && texture != QStringLiteral("-")) {
				document->textureReferences.push_back(texture);
			}
		}
		document->doomSidedefs.push_back(sidedef);
	}

	const QByteArray sectorBytes = document->doomLumps.value(QStringLiteral("SECTORS"));
	for (qsizetype offset = 0; offset + 26 <= sectorBytes.size(); offset += 26) {
		LevelMapDoomSector sector;
		sector.id = document->doomSectors.size();
		sector.floorHeight = readLe16Signed(sectorBytes, offset);
		sector.ceilingHeight = readLe16Signed(sectorBytes, offset + 2);
		sector.floorTexture = fixedLatin1(sectorBytes, offset + 4, 8);
		sector.ceilingTexture = fixedLatin1(sectorBytes, offset + 12, 8);
		sector.lightLevel = readLe16(sectorBytes, offset + 20);
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

	Q_UNUSED(magic);
	return true;
}

struct ParseEntityState {
	LevelMapEntity entity;
	QVector<LevelMapBrush> brushes;
	LevelMapBrush currentBrush;
	bool inEntity = false;
	bool inBrush = false;
	int braceDepth = 0;
};

void parseFaceLine(LevelMapDocument* document, ParseEntityState* state, const QString& line)
{
	if (!document || !state) {
		return;
	}
	static const QRegularExpression pointPattern(QStringLiteral(R"vs(\(\s*([-+]?\d+(?:\.\d+)?)\s+([-+]?\d+(?:\.\d+)?)\s+([-+]?\d+(?:\.\d+)?)\s*\))vs"));
	static const QRegularExpression texturePattern(QStringLiteral(R"vs(\)\s+([^\s()]+)\s+[-+]?\d)vs"));
	QRegularExpressionMatchIterator points = pointPattern.globalMatch(line);
	int pointCount = 0;
	while (points.hasNext()) {
		const QRegularExpressionMatch match = points.next();
		const LevelMapVec3 point {match.captured(1).toDouble(), match.captured(2).toDouble(), match.captured(3).toDouble(), true};
		includePoint(&state->currentBrush.mins, &state->currentBrush.maxs, point);
		++pointCount;
	}
	if (pointCount >= 3) {
		++state->currentBrush.faceCount;
		const QRegularExpressionMatch textureMatch = texturePattern.match(line);
		if (textureMatch.hasMatch()) {
			const QString texture = textureMatch.captured(1).trimmed();
			if (!texture.isEmpty()) {
				state->currentBrush.textureNames.push_back(texture);
				document->textureReferences.push_back(texture);
			}
		}
	}
}

bool parseQuakeMap(const LevelMapLoadRequest& request, LevelMapDocument* document, QString* error)
{
	QFile file(request.path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		if (error) {
			*error = mapText("Unable to open .map file.");
		}
		return false;
	}
	const QString text = QString::fromUtf8(file.readAll());
	document->sourcePath = QFileInfo(request.path).absoluteFilePath();
	document->mapName = QFileInfo(request.path).fileName();
	document->originalText = text;
	document->textLines = text.split('\n');
	const QString hint = normalizedId(request.engineHint);
	const bool q3Hint = hint.contains(QStringLiteral("3")) || text.contains(QStringLiteral("textures/"), Qt::CaseInsensitive) || text.contains(QStringLiteral("q3map"), Qt::CaseInsensitive);
	document->format = q3Hint ? LevelMapFormat::Quake3Map : LevelMapFormat::QuakeMap;
	document->engineFamily = q3Hint ? QStringLiteral("idTech3") : QStringLiteral("idTech2");
	document->editState = QStringLiteral("clean");

	ParseEntityState state;
	static const QRegularExpression keyValuePattern(QStringLiteral(R"vs(^\s*"([^"]+)"\s+"([^"]*)")vs"));
	int lineNumber = 0;
	for (const QString& line : document->textLines) {
		++lineNumber;
		const QString trimmed = line.trimmed();

		if (!state.inEntity && trimmed == QStringLiteral("{")) {
			state = {};
			state.inEntity = true;
			state.braceDepth = 1;
			state.entity.id = document->entities.size();
			state.entity.startLine = lineNumber;
			continue;
		}

		if (state.inEntity && state.braceDepth == 1) {
			const QRegularExpressionMatch keyMatch = keyValuePattern.match(line);
			if (keyMatch.hasMatch()) {
				LevelMapProperty property {keyMatch.captured(1), keyMatch.captured(2), lineNumber};
				state.entity.properties.push_back(property);
				if (property.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
					state.entity.className = property.value;
				} else if (property.key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
					state.entity.origin = parseVec3(property.value);
				}
			}
		}

		if (state.inEntity && state.braceDepth >= 2 && state.inBrush) {
			parseFaceLine(document, &state, line);
		}

		int opens = 0;
		int closes = 0;
		bool inQuote = false;
		for (int index = 0; index < line.size(); ++index) {
			const QChar ch = line.at(index);
			if (ch == '"' && (index == 0 || line.at(index - 1) != '\\')) {
				inQuote = !inQuote;
			}
			if (inQuote) {
				continue;
			}
			if (ch == '{') {
				++opens;
			} else if (ch == '}') {
				++closes;
			}
		}

		if (state.inEntity && state.braceDepth == 1 && opens > 0 && trimmed == QStringLiteral("{")) {
			state.inBrush = true;
			state.currentBrush = {};
			state.currentBrush.id = document->brushes.size() + state.brushes.size();
			state.currentBrush.entityId = state.entity.id;
		}

		state.braceDepth += opens;
		state.braceDepth -= closes;

		if (state.inEntity && state.inBrush && state.braceDepth == 1) {
			state.brushes.push_back(state.currentBrush);
			state.currentBrush = {};
			state.inBrush = false;
		}

		if (state.inEntity && state.braceDepth <= 0) {
			state.entity.endLine = lineNumber;
			if (state.entity.className.isEmpty()) {
				state.entity.className = mapText("entity");
				addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("entity-missing-classname"), mapText("Entity is missing a classname."), entityObjectId(state.entity.id), state.entity.startLine);
			}
			document->entities.push_back(state.entity);
			for (LevelMapBrush brush : state.brushes) {
				document->brushes.push_back(brush);
			}
			state = {};
		}
	}

	if (state.inEntity) {
		addIssue(document, LevelMapIssueSeverity::Error, QStringLiteral("unterminated-entity"), mapText("Map ended before an entity closed."), entityObjectId(state.entity.id), state.entity.startLine);
	}
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
	for (const LevelMapBrush& brush : document->brushes) {
		if (brush.faceCount <= 0) {
			addIssue(document, LevelMapIssueSeverity::Warning, QStringLiteral("empty-brush"), mapText("Brush has no parsed faces."), brushObjectId(brush.id));
		}
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

QByteArray doomVertexBytes(const QVector<LevelMapDoomVertex>& vertices)
{
	QByteArray bytes;
	for (const LevelMapDoomVertex& vertex : vertices) {
		appendLe16(&bytes, static_cast<qint16>(std::lround(vertex.x)));
		appendLe16(&bytes, static_cast<qint16>(std::lround(vertex.y)));
	}
	return bytes;
}

QByteArray doomLinedefBytes(const QVector<LevelMapDoomLinedef>& linedefs)
{
	QByteArray bytes;
	for (const LevelMapDoomLinedef& linedef : linedefs) {
		appendLe16(&bytes, static_cast<qint16>(linedef.startVertex));
		appendLe16(&bytes, static_cast<qint16>(linedef.endVertex));
		appendLe16(&bytes, static_cast<qint16>(linedef.flags));
		appendLe16(&bytes, static_cast<qint16>(linedef.special));
		appendLe16(&bytes, static_cast<qint16>(linedef.tag));
		appendLe16(&bytes, static_cast<qint16>(linedef.frontSidedef));
		appendLe16(&bytes, static_cast<qint16>(linedef.backSidedef < 0 ? 0xffff : linedef.backSidedef));
	}
	return bytes;
}

QByteArray doomThingBytes(const QVector<LevelMapDoomThing>& things)
{
	QByteArray bytes;
	for (const LevelMapDoomThing& thing : things) {
		appendLe16(&bytes, static_cast<qint16>(std::lround(thing.x)));
		appendLe16(&bytes, static_cast<qint16>(std::lround(thing.y)));
		appendLe16(&bytes, static_cast<qint16>(thing.angle));
		appendLe16(&bytes, static_cast<qint16>(thing.type));
		appendLe16(&bytes, static_cast<qint16>(thing.flags));
	}
	return bytes;
}

bool writeDoomWad(const LevelMapDocument& document, const QString& outputPath, QString* error)
{
	QVector<WadLump> originalLumps = readWadLumps(document.sourcePath, nullptr, error);
	if (originalLumps.isEmpty()) {
		return false;
	}
	bool inRequestedMap = false;
	for (WadLump& lump : originalLumps) {
		if (isDoomMapMarker(lump.name)) {
			inRequestedMap = lump.name.compare(document.mapName, Qt::CaseInsensitive) == 0;
		}
		if (!inRequestedMap) {
			continue;
		}
		if (lump.name == QStringLiteral("VERTEXES")) {
			lump.bytes = doomVertexBytes(document.doomVertices);
		} else if (lump.name == QStringLiteral("LINEDEFS")) {
			lump.bytes = doomLinedefBytes(document.doomLinedefs);
		} else if (lump.name == QStringLiteral("THINGS")) {
			lump.bytes = doomThingBytes(document.doomThings);
		}
	}

	QByteArray data;
	data.append("PWAD");
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
		if (error) {
			*error = mapText("Unable to open output WAD.");
		}
		return false;
	}
	if (file.write(data) != data.size() || !file.commit()) {
		if (error) {
			*error = mapText("Unable to write output WAD.");
		}
		return false;
	}
	return true;
}

QString serializeMapText(const LevelMapDocument& document)
{
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return document.originalText;
	}
	QStringList lines = document.textLines;
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.startLine <= 0 || entity.endLine <= 0 || entity.startLine > lines.size()) {
			continue;
		}
		QSet<QString> seenKeys;
		for (const LevelMapProperty& property : entity.properties) {
			if (property.line > 0 && property.line <= lines.size()) {
				lines[property.line - 1] = QStringLiteral("\"%1\" \"%2\"").arg(property.key, property.value);
				seenKeys.insert(property.key.toCaseFolded());
			}
		}
		int insertAt = std::max(0, std::min(entity.endLine - 1, static_cast<int>(lines.size())));
		for (const LevelMapProperty& property : entity.properties) {
			if (property.line <= 0 && !seenKeys.contains(property.key.toCaseFolded())) {
				lines.insert(insertAt, QStringLiteral("\"%1\" \"%2\"").arg(property.key, property.value));
				++insertAt;
			}
		}
	}
	return lines.join('\n');
}

bool writeTextMap(const LevelMapDocument& document, const QString& outputPath, QString* error)
{
	QSaveFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		if (error) {
			*error = mapText("Unable to open output map.");
		}
		return false;
	}
	const QByteArray bytes = serializeMapText(document).toUtf8();
	if (file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = mapText("Unable to write output map.");
		}
		return false;
	}
	return true;
}

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

void syncDoomThingEntity(LevelMapDocument* document, int thingId)
{
	LevelMapDoomThing* thing = thingById(document, thingId);
	LevelMapEntity* entity = entityById(document, thingId);
	if (!thing || !entity) {
		return;
	}
	entity->className = QStringLiteral("thing:%1").arg(thing->type);
	entity->origin = {thing->x, thing->y, 0.0, true};
	upsertEntityProperty(entity, QStringLiteral("type"), QString::number(thing->type));
	upsertEntityProperty(entity, QStringLiteral("angle"), QString::number(thing->angle));
	upsertEntityProperty(entity, QStringLiteral("flags"), QString::number(thing->flags));
	upsertEntityProperty(entity, QStringLiteral("origin"), serializeVec3(entity->origin));
	upsertEntityProperty(entity, QStringLiteral("x"), QString::number(static_cast<int>(std::lround(thing->x))));
	upsertEntityProperty(entity, QStringLiteral("y"), QString::number(static_cast<int>(std::lround(thing->y))));
}

void pushUndo(LevelMapDocument* document, const LevelMapUndoCommand& command)
{
	if (!document) {
		return;
	}
	document->undoStack.push_back(command);
	document->redoStack.clear();
	document->editState = QStringLiteral("modified");
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
	case LevelMapSelectionKind::QuakeBrush:
		return QStringLiteral("brush");
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
	stats.doomThingCount = document.doomThings.size();
	stats.doomVertexCount = document.doomVertices.size();
	stats.doomLinedefCount = document.doomLinedefs.size();
	stats.doomSidedefCount = document.doomSidedefs.size();
	stats.doomSectorCount = document.doomSectors.size();
	stats.textureReferenceCount = document.textureReferences.size();
	stats.uniqueTextureCount = uniqueTextureReferences(document).size();
	for (const LevelMapBrush& brush : document.brushes) {
		stats.brushFaceCount += brush.faceCount;
		includePoint(&stats.mins, &stats.maxs, brush.mins);
		includePoint(&stats.mins, &stats.maxs, brush.maxs);
	}
	for (const LevelMapDoomVertex& vertex : document.doomVertices) {
		includePoint(&stats.mins, &stats.maxs, {vertex.x, vertex.y, 0.0, true});
	}
	for (const LevelMapDoomThing& thing : document.doomThings) {
		includePoint(&stats.mins, &stats.maxs, {thing.x, thing.y, 0.0, true});
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
	return {
		mapText("Format: %1").arg(levelMapFormatDisplayName(document.format)),
		mapText("Engine: %1").arg(document.engineFamily.isEmpty() ? mapText("unknown") : document.engineFamily),
		mapText("Map: %1").arg(document.mapName.isEmpty() ? mapText("unknown") : document.mapName),
		mapText("Entities: %1").arg(stats.entityCount),
		mapText("Brushes: %1").arg(stats.brushCount),
		mapText("Brush faces: %1").arg(stats.brushFaceCount),
		mapText("Doom things: %1").arg(stats.doomThingCount),
		mapText("Doom vertices: %1").arg(stats.doomVertexCount),
		mapText("Doom linedefs: %1").arg(stats.doomLinedefCount),
		mapText("Doom sectors: %1").arg(stats.doomSectorCount),
		mapText("Texture references: %1 / %2 unique").arg(stats.textureReferenceCount).arg(stats.uniqueTextureCount),
		mapText("Bounds min: %1").arg(vecText(stats.mins)),
		mapText("Bounds max: %1").arg(vecText(stats.maxs)),
		mapText("Issues: %1 warnings, %2 errors").arg(stats.warningCount).arg(stats.errorCount),
	};
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
		lines << QStringLiteral("%1 %2 %3%4")
			.arg(levelMapIssueSeverityId(issue.severity).toUpper(), issue.code, issue.message, issue.line > 0 ? mapText(" at line %1").arg(issue.line) : QString());
	}
	if (lines.isEmpty()) {
		lines << mapText("No validation problems.");
	}
	return lines;
}

QStringList levelMapViewLines(const LevelMapDocument& document)
{
	QStringList lines;
	if (document.format == LevelMapFormat::DoomWad) {
		lines << mapText("[2D Doom map view]");
		const int maxLines = std::min(12, static_cast<int>(document.doomLinedefs.size()));
		for (int index = 0; index < maxLines; ++index) {
			const LevelMapDoomLinedef& linedef = document.doomLinedefs.at(index);
			const LevelMapDoomVertex a = linedef.startVertex >= 0 && linedef.startVertex < document.doomVertices.size() ? document.doomVertices.at(linedef.startVertex) : LevelMapDoomVertex {};
			const LevelMapDoomVertex b = linedef.endVertex >= 0 && linedef.endVertex < document.doomVertices.size() ? document.doomVertices.at(linedef.endVertex) : LevelMapDoomVertex {};
			lines << QStringLiteral("%1 %2 -> %3 tag=%4 special=%5")
				.arg(linedefObjectId(linedef.id), doomPointText(a.x, a.y), doomPointText(b.x, b.y))
				.arg(linedef.tag)
				.arg(linedef.special);
		}
		for (const LevelMapDoomThing& thing : document.doomThings.mid(0, 8)) {
			lines << QStringLiteral("%1 thing type=%2 at %3").arg(thingObjectId(thing.id)).arg(thing.type).arg(doomPointText(thing.x, thing.y));
		}
	} else {
		lines << mapText("[orthographic brush/entity preview]");
		for (const LevelMapBrush& brush : document.brushes.mid(0, 12)) {
			lines << QStringLiteral("%1 entity=%2 faces=%3 mins=%4 maxs=%5")
				.arg(brushObjectId(brush.id))
				.arg(brush.entityId)
				.arg(brush.faceCount)
				.arg(vecText(brush.mins), vecText(brush.maxs));
		}
		for (const LevelMapEntity& entity : document.entities.mid(0, 8)) {
			lines << QStringLiteral("%1 %2 origin=%3").arg(entityObjectId(entity.id), entity.className, vecText(entity.origin));
		}
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
	return {mapText("Selection: %1:%2").arg(levelMapSelectionKindId(document.selectionKind)).arg(document.selectedObjectId)};
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
		return {mapText("Linedef: %1").arg(linedefObjectId(linedef.id)), mapText("Vertices: %1 -> %2").arg(linedef.startVertex).arg(linedef.endVertex), mapText("Special/tag: %1 / %2").arg(linedef.special).arg(linedef.tag)};
	}
	if (document.selectionKind == LevelMapSelectionKind::DoomThing && document.selectedObjectId >= 0 && document.selectedObjectId < document.doomThings.size()) {
		const LevelMapDoomThing& thing = document.doomThings.at(document.selectedObjectId);
		return {mapText("Thing: %1").arg(thingObjectId(thing.id)), mapText("Type: %1").arg(thing.type), mapText("Position: %1").arg(doomPointText(thing.x, thing.y))};
	}
	return {mapText("No property inspector data for the current selection.")};
}

QStringList levelMapUndoLines(const LevelMapDocument& document)
{
	QStringList lines;
	lines << mapText("Edit state: %1").arg(document.editState);
	lines << mapText("Undo commands: %1").arg(document.undoStack.size());
	lines << mapText("Redo commands: %1").arg(document.redoStack.size());
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
	for (LevelMapEntity& entity : document->entities) {
		entity.selected = false;
	}
	for (LevelMapBrush& brush : document->brushes) {
		brush.selected = false;
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
	} else if (kind == QStringLiteral("brush")) {
		bool found = false;
		for (LevelMapBrush& brush : document->brushes) {
			if (brush.id == id) {
				brush.selected = true;
				found = true;
				break;
			}
		}
		if (!found) {
			if (error) {
				*error = mapText("Brush selection was not found.");
			}
			return false;
		}
		document->selectionKind = LevelMapSelectionKind::QuakeBrush;
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
	const QString oldValue = index >= 0 ? entity->properties[index].value : QString();
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
	if (document->format == LevelMapFormat::DoomWad && entityId >= 0 && entityId < document->doomThings.size()) {
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
		} else if (trimmedKey.compare(QStringLiteral("x"), Qt::CaseInsensitive) == 0) {
			const double parsed = value.toDouble(&ok);
			if (ok) {
				thing.x = parsed;
				entity->origin = {thing.x, thing.y, 0.0, true};
			}
		} else if (trimmedKey.compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0) {
			const double parsed = value.toDouble(&ok);
			if (ok) {
				thing.y = parsed;
				entity->origin = {thing.x, thing.y, 0.0, true};
			}
		} else if (trimmedKey.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0 && entity->origin.valid) {
			thing.x = entity->origin.x;
			thing.y = entity->origin.y;
		}
		syncDoomThingEntity(document, entityId);
	}
	pushUndo(document, {mapText("Set %1 on entity %2").arg(trimmedKey).arg(entityId), mapText("Restore %1 on entity %2").arg(trimmedKey).arg(entityId), QStringLiteral("set-property"), QStringLiteral("entity"), entityId, entityId, trimmedKey, oldValue, value, {}});
	selectLevelMapObject(document, entityObjectId(entityId));
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
		LevelMapVec3 origin = entity->origin.valid ? entity->origin : parseVec3(propertyValue(*entity, QStringLiteral("origin")));
		if (!origin.valid) {
			origin = {0.0, 0.0, 0.0, true};
		}
		const QString oldValue = serializeVec3(origin);
		origin.x += dx;
		origin.y += dy;
		origin.z += dz;
		const QString newValue = serializeVec3(origin);
		const int index = propertyIndex(*entity, QStringLiteral("origin"));
		if (index >= 0) {
			entity->properties[index].value = newValue;
		} else {
			entity->properties.push_back({QStringLiteral("origin"), newValue, 0});
		}
		entity->origin = origin;
		if (document->format == LevelMapFormat::DoomWad && objectId >= 0 && objectId < document->doomThings.size()) {
			document->doomThings[objectId].x = origin.x;
			document->doomThings[objectId].y = origin.y;
			syncDoomThingEntity(document, objectId);
		}
		command.key = QStringLiteral("origin");
		command.oldValue = oldValue;
		command.newValue = newValue;
		pushUndo(document, command);
		selectLevelMapObject(document, entityObjectId(objectId));
		return true;
	}
	if (kind == QStringLiteral("vertex")) {
		LevelMapDoomVertex* vertex = vertexById(document, objectId);
		if (!vertex) {
			if (error) {
				*error = mapText("Doom vertex not found.");
			}
			return false;
		}
		command.oldValue = doomPointText(vertex->x, vertex->y);
		vertex->x += dx;
		vertex->y += dy;
		command.newValue = doomPointText(vertex->x, vertex->y);
		pushUndo(document, command);
		selectLevelMapObject(document, vertexObjectId(objectId));
		return true;
	}
	if (kind == QStringLiteral("linedef")) {
		LevelMapDoomLinedef* linedef = linedefById(document, objectId);
		if (!linedef) {
			if (error) {
				*error = mapText("Doom linedef not found.");
			}
			return false;
		}
		command.oldValue = QStringLiteral("%1,%2").arg(linedef->startVertex).arg(linedef->endVertex);
		for (const int vertexId : {linedef->startVertex, linedef->endVertex}) {
			if (LevelMapDoomVertex* vertex = vertexById(document, vertexId)) {
				vertex->x += dx;
				vertex->y += dy;
			}
		}
		command.newValue = command.oldValue;
		pushUndo(document, command);
		selectLevelMapObject(document, linedefObjectId(objectId));
		return true;
	}
	if (kind == QStringLiteral("thing")) {
		LevelMapDoomThing* thing = thingById(document, objectId);
		if (!thing) {
			if (error) {
				*error = mapText("Doom thing not found.");
			}
			return false;
		}
		command.oldValue = doomPointText(thing->x, thing->y);
		thing->x += dx;
		thing->y += dy;
		syncDoomThingEntity(document, objectId);
		command.newValue = doomPointText(thing->x, thing->y);
		pushUndo(document, command);
		selectLevelMapObject(document, thingObjectId(objectId));
		return true;
	}
	if (error) {
		*error = mapText("Move supports entity, vertex, linedef, or thing.");
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
	if (command.commandKind == QStringLiteral("set-property")) {
		LevelMapEntity* entity = entityById(document, command.entityId);
		if (!entity) {
			return false;
		}
		const int index = propertyIndex(*entity, command.key);
		if (index >= 0) {
			entity->properties[index].value = command.oldValue;
		}
		if (command.key.compare(QStringLiteral("classname"), Qt::CaseInsensitive) == 0) {
			entity->className = command.oldValue;
		}
		if (command.key.compare(QStringLiteral("origin"), Qt::CaseInsensitive) == 0) {
			entity->origin = parseVec3(command.oldValue);
		}
		if (document->format == LevelMapFormat::DoomWad) {
			syncDoomThingEntity(document, command.entityId);
		}
	} else if (command.commandKind == QStringLiteral("move")) {
		moveLevelMapObject(document, command.objectKind, command.objectId, -command.delta.x, -command.delta.y, -command.delta.z, nullptr);
		if (!document->undoStack.isEmpty()) {
			document->undoStack.takeLast();
		}
	}
	document->redoStack.push_back(command);
	document->editState = document->undoStack.isEmpty() ? QStringLiteral("clean") : QStringLiteral("modified");
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
	if (command.commandKind == QStringLiteral("set-property")) {
		setLevelMapEntityProperty(document, command.entityId, command.key, command.newValue, nullptr);
	} else if (command.commandKind == QStringLiteral("move")) {
		moveLevelMapObject(document, command.objectKind, command.objectId, command.delta.x, command.delta.y, command.delta.z, nullptr);
	}
	return true;
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
		return report;
	}
	if (!QDir().mkpath(outputInfo.absolutePath())) {
		report.errors << mapText("Unable to create output directory.");
		return report;
	}
	QString error;
	bool ok = false;
	if (document.format == LevelMapFormat::DoomWad) {
		ok = writeDoomWad(document, report.outputPath, &error);
	} else {
		ok = writeTextMap(document, report.outputPath, &error);
	}
	if (!ok) {
		report.errors << (error.isEmpty() ? mapText("Unable to save level map.") : error);
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
