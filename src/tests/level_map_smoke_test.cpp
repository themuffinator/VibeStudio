#include "core/compiler_profiles.h"
#include "core/level_map.h"
#include "core/map_geometry.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPoint>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace vibestudio;

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool nearly(double value, double expected, double tolerance = 0.01)
{
	return std::abs(value - expected) <= tolerance;
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

QByteArray fixedName(const QByteArray& name, int size)
{
	QByteArray bytes = name.left(size);
	while (bytes.size() < size) {
		bytes.append('\0');
	}
	return bytes;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

// Saves over a file the test wrote a moment ago. On Windows the replace can
// fail while something, such as a virus scan, still has the old file open,
// so a failed save is tried again after a short wait.
bool saveMapOverwriting(const LevelMapDocument& document, const QString& path)
{
	for (int attempt = 0; attempt < 5; ++attempt) {
		if (saveLevelMapAs(document, path, false, true).succeeded()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	return false;
}

struct Lump {
	QByteArray name;
	QByteArray bytes;
};

QByteArray buildWad(const QByteArray& magic, const QVector<Lump>& lumps)
{
	QByteArray data;
	data.append(magic);
	appendLe32(&data, lumps.size());
	appendLe32(&data, 0);
	QVector<int> offsets;
	for (const Lump& lump : lumps) {
		offsets.push_back(data.size());
		data.append(lump.bytes);
	}
	const int directoryOffset = data.size();
	for (int index = 0; index < lumps.size(); ++index) {
		appendLe32(&data, offsets.at(index));
		appendLe32(&data, lumps.at(index).bytes.size());
		data.append(fixedName(lumps.at(index).name, 8));
	}
	for (int shift = 0; shift < 4; ++shift) {
		data[8 + shift] = static_cast<char>((directoryOffset >> (shift * 8)) & 0xff);
	}
	return data;
}

QByteArray doomThings()
{
	QByteArray bytes;
	appendLe16(&bytes, 32);
	appendLe16(&bytes, 32);
	appendLe16(&bytes, 90);
	appendLe16(&bytes, 1);
	appendLe16(&bytes, 7);
	return bytes;
}

QByteArray doomVertices()
{
	QByteArray bytes;
	for (const QPoint& point : {QPoint(0, 0), QPoint(128, 0), QPoint(128, 128), QPoint(0, 128)}) {
		appendLe16(&bytes, static_cast<qint16>(point.x()));
		appendLe16(&bytes, static_cast<qint16>(point.y()));
	}
	return bytes;
}

QByteArray doomLinedefs()
{
	QByteArray bytes;
	const QVector<QPair<int, int>> lines = {
		{0, 1},
		{1, 2},
		{2, 3},
		{3, 0},
	};
	for (const QPair<int, int>& line : lines) {
		appendLe16(&bytes, static_cast<qint16>(line.first));
		appendLe16(&bytes, static_cast<qint16>(line.second));
		appendLe16(&bytes, 0);
		appendLe16(&bytes, 0);
		appendLe16(&bytes, 0);
		appendLe16(&bytes, 0);
		appendLe16(&bytes, -1);
	}
	return bytes;
}

QByteArray doomSidedefs()
{
	QByteArray bytes;
	appendLe16(&bytes, 4);
	appendLe16(&bytes, 8);
	bytes.append(fixedName("WALLUP", 8));
	bytes.append(fixedName("WALLLOW", 8));
	bytes.append(fixedName("WALLMID", 8));
	appendLe16(&bytes, 0);
	return bytes;
}

QByteArray doomSectors()
{
	QByteArray bytes;
	appendLe16(&bytes, 0);
	appendLe16(&bytes, 128);
	bytes.append(fixedName("FLOOR1", 8));
	bytes.append(fixedName("CEIL1", 8));
	appendLe16(&bytes, 160);
	appendLe16(&bytes, 0);
	appendLe16(&bytes, 0);
	return bytes;
}

QVector<Lump> classicMapLumps(const QByteArray& marker)
{
	return {
		{marker, {}},
		{"THINGS", doomThings()},
		{"LINEDEFS", doomLinedefs()},
		{"SIDEDEFS", doomSidedefs()},
		{"VERTEXES", doomVertices()},
		{"SECTORS", doomSectors()},
	};
}

QByteArray wadFixture()
{
	QVector<Lump> lumps = {{"PLAYPAL", "palette"}};
	lumps += classicMapLumps("MAP01");
	lumps.push_back({"NODES", QByteArray(28, '\0')});
	lumps.push_back({"BLOCKMAP", QByteArray(16, '\0')});
	return buildWad("PWAD", lumps);
}

// Hexen-format map: BEHAVIOR present, 16-byte linedefs and 20-byte things.
// https://doomwiki.org/wiki/Hexen_map_format
QByteArray hexenThings()
{
	QByteArray bytes;
	appendLe16(&bytes, 17);   // tid
	appendLe16(&bytes, 64);   // x
	appendLe16(&bytes, -48);  // y
	appendLe16(&bytes, 24);   // z
	appendLe16(&bytes, 270);  // angle
	appendLe16(&bytes, 1);    // type (player 1 start)
	appendLe16(&bytes, 2023); // flags
	appendByte(&bytes, 80);   // special
	appendByte(&bytes, 1);
	appendByte(&bytes, 2);
	appendByte(&bytes, 3);
	appendByte(&bytes, 4);
	appendByte(&bytes, 5);
	return bytes;
}

QByteArray hexenLinedefs()
{
	QByteArray bytes;
	const QVector<QPair<int, int>> lines = {
		{0, 1},
		{1, 2},
		{2, 3},
		{3, 0},
	};
	int argBase = 10;
	for (const QPair<int, int>& line : lines) {
		appendLe16(&bytes, static_cast<qint16>(line.first));
		appendLe16(&bytes, static_cast<qint16>(line.second));
		appendLe16(&bytes, 0);
		appendByte(&bytes, 12); // special
		appendByte(&bytes, argBase);
		appendByte(&bytes, argBase + 1);
		appendByte(&bytes, argBase + 2);
		appendByte(&bytes, argBase + 3);
		appendByte(&bytes, argBase + 4);
		appendLe16(&bytes, 0);  // front sidedef
		appendLe16(&bytes, -1); // back sidedef (0xffff)
		++argBase;
	}
	return bytes;
}

QByteArray hexenWadFixture()
{
	QVector<Lump> lumps = {
		{"MAP02", {}},
		{"THINGS", hexenThings()},
		{"LINEDEFS", hexenLinedefs()},
		{"SIDEDEFS", doomSidedefs()},
		{"VERTEXES", doomVertices()},
		{"SECTORS", doomSectors()},
		{"BEHAVIOR", QByteArray("ACS\0", 4)},
	};
	return buildWad("PWAD", lumps);
}

QByteArray udmfWadFixture()
{
	const QVector<Lump> lumps = {
		{"MAP03", {}},
		{"TEXTMAP", "namespace = \"zdoom\";\nvertex { x = 0.0; y = 0.0; }\n"},
		{"ZNODES", QByteArray(8, '\0')},
		{"ENDMAP", {}},
	};
	return buildWad("PWAD", lumps);
}

QString propertyValueForTest(const LevelMapDocument& document, int entityId, const QString& key)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id != entityId) {
			continue;
		}
		for (const LevelMapProperty& property : entity.properties) {
			if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
				return property.value;
			}
		}
	}
	return {};
}

bool hasIssueCode(const LevelMapDocument& document, const QString& code)
{
	for (const LevelMapIssue& issue : document.issues) {
		if (issue.code == code) {
			return true;
		}
	}
	return false;
}

const LevelMapBrush* brushForTest(const LevelMapDocument& document, int id)
{
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == id) {
			return &brush;
		}
	}
	return nullptr;
}

QByteArray quakeMapFixture()
{
	return R"MAP({
"classname" "worldspawn"
"wad" "C:\Users\Mapper\quake\id1\gfx.wad"
{
( 0 0 0 ) ( 128 0 0 ) ( 128 128 0 ) WALL1 0 0 0 1 1
( 0 0 16 ) ( 128 128 16 ) ( 128 0 16 ) CEIL1 0 0 0 1 1
}
}
{
"classname" "light"
"origin" "32 32 64"
"targetname" "lamp"
}
)MAP";
}

// A 64-unit cube written with braces on the same line as content, comments in
// both styles, scientific notation and a bare `.5` scale.
QByteArray compactMapFixture()
{
	return R"MAP(// Game: Quake
/* block comment
   spanning lines */
{ "classname" "worldspawn" "light" "3e2"
{ ( 1 0 6.4e1 ) ( 0 0 6.4e1 ) ( 0 1 6.4e1 ) TOP 0 0 0 1 1
( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOTTOM 0 0 0 1 1 // trailing comment
( 64 1 0 ) ( 64 0 0 ) ( 64 0 1 ) EAST .5 -.5 0 .5 1
( 0 0 1 ) ( 0 0 0 ) ( 0 1 0 ) WEST 0 0 0 1 1
( 0 64 1 ) ( 0 64 0 ) ( 1 64 0 ) NORTH 0 0 0 1 1
( 1 0 0 ) ( 0 0 0 ) ( 0 0 1 ) SOUTH 0 0 0 1 1 } }
)MAP";
}

// Valve 220: the texture name sits between the third plane point and the `[`
// that opens the explicit U axis.
QByteArray valve220MapFixture()
{
	return R"MAP({
"classname" "worldspawn"
"mapversion" "220"
{
( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOPTEX [ 1 0 0 16 ] [ 0 -1 0 -8 ] 0 1 1
( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOTTOMTEX [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1
( 64 1 0 ) ( 64 0 0 ) ( 64 0 1 ) EASTTEX [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 0 0 1 ) ( 0 0 0 ) ( 0 1 0 ) WESTTEX [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 0 64 1 ) ( 0 64 0 ) ( 1 64 0 ) NORTHTEX [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
( 1 0 0 ) ( 0 0 0 ) ( 0 0 1 ) SOUTHTEX [ 1 0 0 0 ] [ 0 0 -1 0 ] 0 1 1
}
}
)MAP";
}

// Quake III brush primitives, a brushDef3 brush and a patchDef2 mesh.
QByteArray quake3MapFixture()
{
	return R"MAP(// entity 0
// Generated by GtkRadiant
{
"classname" "worldspawn"
"_q3map_lightmapsize" "256 256"
{
brushDef
{
( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0.25 ) ) textures/common/caulk 0 4 0
( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) textures/base_floor/tile 0 0 0
( 64 1 0 ) ( 64 0 0 ) ( 64 0 1 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) textures/base_wall/wall 0 0 0
( 0 0 1 ) ( 0 0 0 ) ( 0 1 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) textures/base_wall/wall 0 0 0
( 0 64 1 ) ( 0 64 0 ) ( 1 64 0 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) textures/base_wall/wall 0 0 0
( 1 0 0 ) ( 0 0 0 ) ( 0 0 1 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) textures/base_wall/wall 0 0 0
}
}
{
brushDef3
{
( 0 0 1 -64 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) "textures/common/caulk" 0 0 0
( 0 0 -1 0 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) "textures/common/caulk" 0 0 0
( 1 0 0 -64 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) "textures/common/caulk" 0 0 0
( -1 0 0 0 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) "textures/common/caulk" 0 0 0
( 0 1 0 -64 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) "textures/common/caulk" 0 0 0
( 0 -1 0 0 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) "textures/common/caulk" 0 0 0
}
}
{
patchDef2
{
textures/base_wall/curve
( 3 3 0 0 0 )
(
( ( 0 0 0 0 0 ) ( 0 16 0 0 .5 ) ( 0 32 0 0 1 ) )
( ( 16 0 8 .5 0 ) ( 16 16 8 .5 .5 ) ( 16 32 8 .5 1 ) )
( ( 32 0 -8 1 0 ) ( 32 16 0 1 .5 ) ( 32 32 0 1 1 ) )
)
}
}
{
terrainDef
{
( 0 0 0 ) ( 1 1 1 )
}
}
}
)MAP";
}

// A non-square `patchDef2`. The `.map` format lays a patch out width-major:
// five parenthesised source groups of three points each, one group per grid
// column. Within a group x is constant and y advances, exactly as the shipped
// sample at samples/projects/quake3-minimal/maps/sample_q3.map does.
QByteArray nonSquarePatchFixture()
{
	return R"MAP({
"classname" "worldspawn"
{
patchDef2
{
textures/base_wall/wide
( 5 3 0 0 0 )
(
( ( 0 0 0 0 0 ) ( 0 32 0 0 0.5 ) ( 0 64 0 0 1 ) )
( ( 32 0 0 0.25 0 ) ( 32 32 0 0.25 0.5 ) ( 32 64 0 0.25 1 ) )
( ( 64 0 0 0.5 0 ) ( 64 32 16 0.5 0.5 ) ( 64 64 0 0.5 1 ) )
( ( 96 0 0 0.75 0 ) ( 96 32 0 0.75 0.5 ) ( 96 64 0 0.75 1 ) )
( ( 128 0 0 1 0 ) ( 128 32 0 1 0.5 ) ( 128 64 0 1 1 ) )
)
}
}
}
)MAP";
}

// A 45-degree rotated box (diamond prism) whose axis-aligned bounds can only be
// produced by actually solving the face planes.
QByteArray rotatedBrushMapFixture()
{
	return R"MAP({
"classname" "worldspawn"
{
( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOP 0 0 0 1 1
( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOTTOM 0 0 0 1 1
( 0 32 0 ) ( 32 0 0 ) ( 32 0 64 ) SIDE1 0 0 0 1 1
( -32 0 0 ) ( 0 32 0 ) ( 0 32 64 ) SIDE2 0 0 0 1 1
( 0 -32 0 ) ( -32 0 0 ) ( -32 0 64 ) SIDE3 0 0 0 1 1
( 32 0 0 ) ( 0 -32 0 ) ( 0 -32 64 ) SIDE4 0 0 0 1 1
}
}
)MAP";
}

// Tab-indented keys and CRLF line endings, both of which must survive a save.
QByteArray crlfMapFixture()
{
	const QString text = QStringLiteral(
		"{\r\n"
		"\t\"classname\" \"worldspawn\"\r\n"
		"\t\"message\" \"hello\"\r\n"
		"\t{\r\n"
		"\t\t( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOP 0 0 0 1 1\r\n"
		"\t\t( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOTTOM 0 0 0 1 1\r\n"
		"\t}\r\n"
		"}\r\n"
		"{\r\n"
		"\t\"classname\" \"info_player_start\"\r\n"
		"\t\"origin\" \"0 0 24\"\r\n"
		"}\r\n");
	return text.toUtf8();
}

bool runQuakeMapSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString sourcePath = root.filePath(QStringLiteral("start.map"));
	ok &= expect(writeFile(sourcePath, quakeMapFixture()), "Quake map fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({sourcePath, {}, QStringLiteral("idtech2")}, &document, &error), "Quake map should load.");
	ok &= expect(document.format == LevelMapFormat::QuakeMap, "Quake map format mismatch.");
	ok &= expect(document.entities.size() == 2, "Quake map should expose entities.");
	ok &= expect(document.brushes.size() == 1, "Quake map should expose brushes.");
	ok &= expect(document.brushes.at(0).faceCount == 2, "Quake brush face count should come from real faces.");
	ok &= expect(levelMapTextureLines(document).contains(QStringLiteral("WALL1")), "Quake map should expose texture references.");
	ok &= expect(levelMapValidationLines(document).join('\n').contains(QStringLiteral("wad-absolute-path")), "Quake map should surface compiler preflight health warnings.");
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("entity:1"), &error), "Quake entity selection should work.");
	ok &= expect(levelMapPropertyLines(document).join('\n').contains(QStringLiteral("targetname")), "Quake property inspector should show entity keys.");
	ok &= expect(setLevelMapEntityProperty(&document, 1, QStringLiteral("targetname"), QStringLiteral("lamp_edited"), &error), "Quake entity key edit should work.");
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("entity"), 1, 16.0, 0.0, 0.0, &error), "Quake entity move should work.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("48 32 64"), "Quake entity origin should move.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Quake move undo should work.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("32 32 64"), "Quake undo should restore origin.");
	ok &= expect(redoLevelMapEdit(&document, &error), "Quake move redo should work.");

	const QString dryRunPath = root.filePath(QStringLiteral("dry-run.map"));
	const LevelMapSaveReport dryRun = saveLevelMapAs(document, dryRunPath, true);
	ok &= expect(dryRun.succeeded() && !QFileInfo::exists(dryRunPath), "Quake dry-run save-as should not write.");

	const QString outputPath = root.filePath(QStringLiteral("start-edited.map"));
	const LevelMapSaveReport save = saveLevelMapAs(document, outputPath);
	ok &= expect(save.succeeded() && QFileInfo::exists(outputPath), "Quake save-as should write.");
	const QString savedText = QString::fromUtf8(readFile(outputPath));
	ok &= expect(savedText.contains(QStringLiteral("\"targetname\" \"lamp_edited\"")), "Quake save-as should persist entity key edits.");
	ok &= expect(savedText.contains(QStringLiteral("\"origin\" \"48 32 64\"")), "Quake save-as should persist moved entity origin.");

	const CompilerCommandRequest request = compilerRequestForLevelMap(document, QString(), QString());
	ok &= expect(request.profileId == QStringLiteral("ericw-qbsp"), "Quake map should default to ericw-qbsp compile plan.");
	return ok;
}

// Object queries: key=value, key:value, key!=value, numeric comparisons, and
// bare words, over a Quake map's entities and brushes and a Doom map's things,
// linedefs, sectors, and vertices.
bool runQuerySmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString quakePath = root.filePath(QStringLiteral("query.map"));
	ok &= expect(writeFile(quakePath, quakeMapFixture()), "Query map fixture should be written.");
	LevelMapDocument quake;
	ok &= expect(loadLevelMap({quakePath, {}, QStringLiteral("idtech2")}, &quake, &error), "Query map should load.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("class=light")) == QStringList {QStringLiteral("entity:1")},
		"class=light should find the light entity.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("targetname:LAM")) == QStringList {QStringLiteral("entity:1")},
		"key:value should find a value containing the text, ignoring case.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("texture=ceil1")) == QStringList {QStringLiteral("brush:0")},
		"texture= should test each of a brush's face textures.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("class!=light")) == QStringList {QStringLiteral("entity:0"), QStringLiteral("brush:0")},
		"key!=value should hold for objects with no such value, and for objects without the key.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("lamp")) == QStringList {QStringLiteral("entity:1")}, "A bare word should match a property value.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("entity:1")) == QStringList {QStringLiteral("entity:1")}
			&& levelMapObjectsMatchingQuery(quake, QStringLiteral("brush:0")) == QStringList {QStringLiteral("brush:0")}
			&& levelMapObjectsMatchingQuery(quake, QStringLiteral("entity=0")) == QStringList {QStringLiteral("brush:0")},
		"A selector as map find prints it should name that object; entity=0 should still find worldspawn's brush.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("kind=entity")) == QStringList {QStringLiteral("entity:0"), QStringLiteral("entity:1")},
		"kind= should keep the objects of one kind.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("class=ligh")).isEmpty()
			&& levelMapObjectsMatchingQuery(quake, QStringLiteral("class:ligh")) == QStringList {QStringLiteral("entity:1")},
		"key=value should match whole values only, where key:value matches part of one.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("class=light targetname=other")).isEmpty(), "Every term should have to hold.");
	ok &= expect(levelMapObjectsMatchingQuery(quake, QStringLiteral("origin:\"32 32\"")) == QStringList {QStringLiteral("entity:1")},
		"A quoted value should keep its spaces.");
	const LevelMapQuery words = parseLevelMapQuery(QStringLiteral("brick wall"));
	const LevelMapQuery mixed = parseLevelMapQuery(QStringLiteral("tag>=3 brick"));
	ok &= expect(!words.testsProperties() && words.terms.size() == 2 && mixed.testsProperties() && mixed.terms.value(0).op == QStringLiteral(">=")
			&& mixed.terms.value(0).key == QStringLiteral("tag") && mixed.terms.value(0).value == QStringLiteral("3"),
		"Words and property terms should parse apart, with two-character operators read whole.");

	const QString wadPath = root.filePath(QStringLiteral("query.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Query WAD fixture should be written.");
	LevelMapDocument doom;
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Query WAD should load.");
	ok &= expect(levelMapObjectsMatchingQuery(doom, QStringLiteral("type=1")) == QStringList {QStringLiteral("thing:0")}, "type=1 should find the player start.");
	ok &= expect(levelMapObjectsMatchingQuery(doom, QStringLiteral("name:player")) == QStringList {QStringLiteral("thing:0")},
		"name: should find a thing by what its type places.");
	ok &= expect(levelMapObjectsMatchingQuery(doom, QStringLiteral("light>100 ceiling<=128")) == QStringList {QStringLiteral("sector:0")},
		"Numeric comparisons should find the sector.");
	ok &= expect(levelMapObjectsMatchingQuery(doom, QStringLiteral("light>200")).isEmpty(), "A comparison that fails should find nothing.");
	ok &= expect(levelMapObjectsMatchingQuery(doom, QStringLiteral("ceiling<128")).isEmpty() && levelMapObjectsMatchingQuery(doom, QStringLiteral("ceiling>128")).isEmpty(),
		"Strict comparisons should leave out an equal number.");
	ok &= expect(levelMapObjectsMatchingQuery(doom, QStringLiteral("texture=wallmid")).size() == 4, "texture= should find every linedef whose side shows it.");
	ok &= expect(levelMapObjectsMatchingQuery(doom, QStringLiteral("x>=128 y=0")) == QStringList {QStringLiteral("vertex:1")},
		"Vertices should answer to x and y.");
	ok &= expect(levelMapObjectsMatchingQuery(doom, QStringLiteral("tag>abc")).isEmpty(), "A comparison with a word for a number should find nothing.");
	return ok;
}

bool runTokenizerSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("compact.map"));
	ok &= expect(writeFile(path, compactMapFixture()), "Compact map fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Compact map should load.");
	ok &= expect(document.entities.size() == 1, "Braces on the same line should still produce one entity.");
	ok &= expect(document.brushes.size() == 1, "Braces on the same line should still produce one brush.");
	const LevelMapBrush* brush = brushForTest(document, 0);
	ok &= expect(brush != nullptr && brush->faceCount == 6, "Compact brush should have six faces.");
	if (brush) {
		ok &= expect(nearly(brush->faces.at(0).p0.z, 64.0), "Scientific notation should parse (6.4e1).");
		ok &= expect(nearly(brush->faces.at(2).shiftX, 0.5) && nearly(brush->faces.at(2).shiftY, -0.5), "Bare .5 and -.5 should parse.");
		ok &= expect(nearly(brush->faces.at(2).scaleX, 0.5), "Bare .5 scale should parse.");
		ok &= expect(brush->faces.at(1).textureName == QStringLiteral("BOTTOM"), "A trailing // comment should not join the face.");
		ok &= expect(brush->boundsSolved, "Compact cube should solve.");
		ok &= expect(nearly(brush->mins.x, 0.0) && nearly(brush->maxs.x, 64.0), "Compact cube bounds should come from the solver.");
	}
	ok &= expect(propertyValueForTest(document, 0, QStringLiteral("light")) == QStringLiteral("3e2"), "Keys on the same line as the brace should parse.");
	ok &= expect(document.format == LevelMapFormat::QuakeMap, "A plain Quake map must not be detected as Quake III.");
	return ok;
}

bool runValve220Smoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("valve220.map"));
	ok &= expect(writeFile(path, valve220MapFixture()), "Valve 220 fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Valve 220 map should load.");
	const LevelMapBrush* brush = brushForTest(document, 0);
	ok &= expect(brush != nullptr, "Valve 220 map should expose a brush.");
	if (!brush) {
		return false;
	}
	ok &= expect(brush->faceCount == 6, "Valve 220 brush should have six faces.");
	ok &= expect(brush->primitiveKind == QStringLiteral("valve220"), "Valve 220 brush kind should be recorded.");
	const LevelMapBrushFace& face = brush->faces.at(0);
	ok &= expect(face.textureName == QStringLiteral("TOPTEX"), "Valve 220 texture name should parse.");
	ok &= expect(face.explicitTextureAxes, "Valve 220 faces should report explicit texture axes.");
	ok &= expect(nearly(face.uAxis.x, 1.0) && nearly(face.uOffset, 16.0), "Valve 220 U axis should parse.");
	ok &= expect(nearly(face.vAxis.y, -1.0) && nearly(face.vOffset, -8.0), "Valve 220 V axis should parse.");
	ok &= expect(nearly(face.scaleX, 1.0) && nearly(face.scaleY, 1.0), "Valve 220 scale should parse.");
	ok &= expect(levelMapTextureLines(document).contains(QStringLiteral("SOUTHTEX")), "Valve 220 textures should be collected.");
	ok &= expect(brush->boundsSolved && nearly(brush->maxs.z, 64.0), "Valve 220 cube should solve.");
	return ok;
}

bool runQuake3Smoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("q3dm.map"));
	ok &= expect(writeFile(path, quake3MapFixture()), "Quake III fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QString()}, &document, &error), "Quake III map should load.");
	ok &= expect(document.format == LevelMapFormat::Quake3Map, "Quake III evidence should select the Quake III format.");
	ok &= expect(document.engineFamily == QStringLiteral("idTech3"), "Quake III engine family mismatch.");
	ok &= expect(document.brushes.size() == 2, "brushDef and brushDef3 should both produce brushes.");
	ok &= expect(document.patches.size() == 1, "patchDef2 should produce a patch, not an empty brush.");
	ok &= expect(hasIssueCode(document, QStringLiteral("terraindef-skipped")), "terrainDef should be skipped with an informational issue.");

	const LevelMapBrush* primitive = brushForTest(document, 0);
	ok &= expect(primitive != nullptr, "brushDef brush should exist.");
	if (primitive) {
		ok &= expect(primitive->faceCount == 6, "brushDef texture matrices must not inflate the face count.");
		ok &= expect(primitive->primitiveKind == QStringLiteral("brushDef"), "brushDef kind should be recorded.");
		ok &= expect(primitive->faces.at(0).explicitTextureMatrix, "brushDef faces should record their texture matrix.");
		ok &= expect(nearly(primitive->faces.at(0).textureMatrix[5], 0.25), "brushDef texture matrix values should parse.");
		ok &= expect(primitive->faces.at(0).textureName == QStringLiteral("textures/common/caulk"), "brushDef shader name should parse.");
		ok &= expect(primitive->faces.at(0).surfaceFlags == 4, "brushDef surface flags should parse.");
		ok &= expect(primitive->boundsSolved, "brushDef cube should solve.");
		ok &= expect(nearly(primitive->mins.x, 0.0) && nearly(primitive->maxs.x, 64.0), "brushDef bounds must not include texture matrix numbers.");
		ok &= expect(nearly(primitive->maxs.z, 64.0), "brushDef bounds should reach the top plane.");
	}

	const LevelMapBrush* def3 = brushForTest(document, 1);
	ok &= expect(def3 != nullptr, "brushDef3 brush should exist.");
	if (def3) {
		ok &= expect(def3->faceCount == 6, "brushDef3 should produce six faces.");
		ok &= expect(def3->primitiveKind == QStringLiteral("brushDef3"), "brushDef3 kind should be recorded.");
		const LevelMapBrushFace& face = def3->faces.at(0);
		ok &= expect(face.explicitPlane, "brushDef3 faces should record an explicit plane.");
		ok &= expect(nearly(face.planeNormal.z, 1.0) && nearly(face.planeDistance, -64.0), "brushDef3 plane should be recorded faithfully.");
		ok &= expect(face.p0.valid && face.p1.valid && face.p2.valid, "brushDef3 faces should synthesize three points.");
		ok &= expect(nearly(face.p0.z, 64.0) && nearly(face.p1.z, 64.0) && nearly(face.p2.z, 64.0), "Synthesized brushDef3 points should lie on the plane.");
		ok &= expect(face.textureName == QStringLiteral("textures/common/caulk"), "brushDef3 quoted shader should parse.");
		ok &= expect(def3->boundsSolved, "brushDef3 cube should solve.");
		ok &= expect(nearly(def3->mins.x, 0.0) && nearly(def3->maxs.y, 64.0), "brushDef3 solved bounds mismatch.");
	}

	const LevelMapPatch& patch = document.patches.at(0);
	ok &= expect(patch.width == 3 && patch.height == 3, "patchDef2 grid size should parse.");
	ok &= expect(patch.controlPoints.size() == 9, "patchDef2 should expose nine control points.");
	// The file stores the grid width-major; the parser normalises it to row major,
	// so index 1 is row 0 / column 1 - the first point of the SECOND source group.
	ok &= expect(patch.controlGridNormalized, "A well-formed patchDef2 grid should be normalised to row-major order.");
	ok &= expect(nearly(patch.controlU.value(1), 0.5) && nearly(patch.controlV.value(1), 0.0), "patchDef2 u/v should parse in normalised order.");
	ok &= expect(nearly(patch.controlPoints.at(1).x, 16.0) && nearly(patch.controlPoints.at(1).y, 0.0), "Normalised index 1 should be the first point of the second source group.");
	ok &= expect(nearly(patch.mins.z, -8.0) && nearly(patch.maxs.z, 8.0), "patchDef2 bounds should come from control points.");
	ok &= expect(patch.textureName == QStringLiteral("textures/base_wall/curve"), "patchDef2 shader should parse.");
	ok &= expect(levelMapPatchLines(document).join('\n').contains(QStringLiteral("patch:0")), "Patch reporting should list the patch.");

	const LevelMapStatistics stats = levelMapStatistics(document);
	ok &= expect(stats.patchCount == 1 && stats.brushCount == 2, "Statistics should count patches and brushes separately.");
	ok &= expect(stats.solvedBrushCount == 2 && stats.degenerateBrushCount == 0, "Both Quake III brushes should solve.");

	ok &= expect(moveLevelMapObject(&document, QStringLiteral("brush"), 0, 8.0, 0.0, 0.0, &error), "Brush move should work.");
	ok &= expect(nearly(document.brushes.at(0).faces.at(2).p0.x, 72.0), "Brush move should translate every face point.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Brush move undo should work.");
	ok &= expect(nearly(document.brushes.at(0).faces.at(2).p0.x, 64.0), "Brush move undo should restore face points.");

	ok &= expect(moveLevelMapObject(&document, QStringLiteral("patch"), 0, 0.0, 0.0, 4.0, &error), "Patch move should work.");
	ok &= expect(nearly(document.patches.at(0).controlPoints.at(0).z, 4.0), "Patch move should translate control points.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Patch move undo should work.");
	ok &= expect(nearly(document.patches.at(0).controlPoints.at(0).z, 0.0), "Patch move undo should restore control points.");

	// An explicit engine hint stays authoritative even against Quake III evidence.
	LevelMapDocument hinted;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &hinted, &error), "Quake III map should load with an idTech2 hint.");
	ok &= expect(hinted.format == LevelMapFormat::QuakeMap, "Explicit engine hint should stay authoritative.");
	return ok;
}

// A square control grid hides a row/column mix-up because the transpose of a
// square grid is the same surface. This pins the layout with a 5 x 3 grid, and
// checks that a moved patch is still written back into its own source groups.
bool runNonSquarePatchSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("wide-patch.map"));
	ok &= expect(writeFile(path, nonSquarePatchFixture()), "Non-square patch fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QString()}, &document, &error), "Non-square patch map should load.");
	ok &= expect(document.patches.size() == 1, "The fixture should produce one patch.");
	if (document.patches.isEmpty()) {
		return false;
	}
	const LevelMapPatch& patch = document.patches.at(0);
	ok &= expect(patch.width == 5 && patch.height == 3, "A ( 5 3 ) header should parse as five columns by three rows.");
	ok &= expect(patch.controlPoints.size() == 15, "Five source groups of three points should yield fifteen control points.");
	ok &= expect(patch.controlGridNormalized, "A rectangular grid should be normalised to row-major order.");
	ok &= expect(patch.controlRowLines.size() == 5, "One source line should be recorded per grid column.");
	if (patch.controlPoints.size() == 15) {
		// Row major: index = row * width + column.
		ok &= expect(nearly(patch.controlPoints.at(1).x, 32.0) && nearly(patch.controlPoints.at(1).y, 0.0),
			"Index 1 should be row 0 column 1, the first point of the second source group.");
		ok &= expect(nearly(patch.controlPoints.at(5).x, 0.0) && nearly(patch.controlPoints.at(5).y, 32.0),
			"Index 5 should be row 1 column 0, the second point of the first source group.");
		ok &= expect(nearly(patch.controlPoints.at(7).x, 64.0) && nearly(patch.controlPoints.at(7).y, 32.0) && nearly(patch.controlPoints.at(7).z, 16.0),
			"Index 7 should be the raised centre control point.");
		ok &= expect(nearly(patch.controlPoints.at(14).x, 128.0) && nearly(patch.controlPoints.at(14).y, 64.0),
			"The last index should be the far corner of the grid.");
		ok &= expect(nearly(patch.controlU.value(1), 0.25) && nearly(patch.controlV.value(1), 0.0),
			"u/v must be normalised alongside the control points.");
	}

	// A moved patch is written back into its own source groups, so a save and
	// reload must reproduce the same normalised grid, shifted.
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("patch"), 0, 0.0, 0.0, 8.0, &error), "Patch move should work.");
	const QString outputPath = root.filePath(QStringLiteral("wide-patch-out.map"));
	ok &= expect(saveLevelMapAs(document, outputPath).succeeded(), "Saving a moved non-square patch should succeed.");

	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({outputPath, {}, QString()}, &reloaded, &error), "The saved non-square patch should reload.");
	ok &= expect(reloaded.patches.size() == 1, "The saved map should still hold one patch.");
	if (!reloaded.patches.isEmpty() && reloaded.patches.at(0).controlPoints.size() == 15) {
		const LevelMapPatch& saved = reloaded.patches.at(0);
		ok &= expect(saved.width == 5 && saved.height == 3, "The saved grid should keep its shape.");
		bool gridMatches = true;
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 5; ++column) {
				const LevelMapVec3& point = saved.controlPoints.at((row * 5) + column);
				const double expectedZ = (row == 1 && column == 2) ? 24.0 : 8.0;
				gridMatches = gridMatches && nearly(point.x, column * 32.0) && nearly(point.y, row * 32.0) && nearly(point.z, expectedZ);
			}
		}
		ok &= expect(gridMatches, "A moved patch must be written back into its own source groups without scrambling the grid.");
	} else {
		ok &= expect(false, "The saved non-square patch should still expose fifteen control points.");
	}
	return ok;
}

bool runRotatedBrushSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("rotated.map"));
	ok &= expect(writeFile(path, rotatedBrushMapFixture()), "Rotated brush fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Rotated brush map should load.");
	const LevelMapBrush* brush = brushForTest(document, 0);
	ok &= expect(brush != nullptr, "Rotated brush should exist.");
	if (!brush) {
		return false;
	}
	ok &= expect(brush->boundsSolved, "Rotated brush should solve into a closed volume.");
	ok &= expect(nearly(brush->mins.x, -32.0) && nearly(brush->maxs.x, 32.0), "Rotated brush X bounds mismatch.");
	ok &= expect(nearly(brush->mins.y, -32.0) && nearly(brush->maxs.y, 32.0), "Rotated brush Y bounds mismatch.");
	ok &= expect(nearly(brush->mins.z, 0.0) && nearly(brush->maxs.z, 64.0), "Rotated brush Z bounds mismatch.");
	const LevelMapStatistics stats = levelMapStatistics(document);
	ok &= expect(stats.solvedBrushCount == 1 && stats.brushFaceCount == 6, "Rotated brush statistics mismatch.");
	return ok;
}

bool runTextSaveSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("crlf.map"));
	ok &= expect(writeFile(path, crlfMapFixture()), "CRLF fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "CRLF map should load.");
	ok &= expect(document.lineEnding == QStringLiteral("\r\n"), "CRLF line endings should be detected.");
	ok &= expect(setLevelMapEntityProperty(&document, 1, QStringLiteral("origin"), QStringLiteral("64 0 24"), &error), "Origin edit should work.");
	ok &= expect(setLevelMapEntityProperty(&document, 0, QStringLiteral("_tb_type"), QStringLiteral("worldspawn"), &error), "Adding a key should work.");

	const QString outputPath = root.filePath(QStringLiteral("crlf-out.map"));
	const LevelMapSaveReport save = saveLevelMapAs(document, outputPath);
	ok &= expect(save.succeeded(), "CRLF save should succeed.");
	const QByteArray savedBytes = readFile(outputPath);
	const QString savedText = QString::fromUtf8(savedBytes);
	ok &= expect(savedText.contains(QStringLiteral("\t\"origin\" \"64 0 24\"\r\n")), "Edited key should keep its tab indentation and CRLF ending.");
	ok &= expect(!savedText.contains(QStringLiteral("\r\r\n")), "Save must not double the carriage returns.");
	ok &= expect(savedText.count(QLatin1Char('\n')) == savedText.count(QStringLiteral("\r\n")), "Every newline should stay a CRLF.");

	// New keys must land after the entity's last key, above the brush body.
	const int newKeyLine = savedText.indexOf(QStringLiteral("\"_tb_type\""));
	const int brushLine = savedText.indexOf(QStringLiteral("( 1 0 64 )"));
	ok &= expect(newKeyLine > 0 && brushLine > 0 && newKeyLine < brushLine, "A new key should be inserted above the brush body.");
	ok &= expect(savedText.contains(QStringLiteral("\t\"_tb_type\" \"worldspawn\"")), "A new key should adopt the entity's indentation.");

	// Key removal, and undo of it.
	ok &= expect(removeLevelMapEntityProperty(&document, 0, QStringLiteral("message"), &error), "Key removal should work.");
	ok &= expect(propertyValueForTest(document, 0, QStringLiteral("message")).isEmpty(), "Removed key should be gone.");
	const QString removedPath = root.filePath(QStringLiteral("crlf-removed.map"));
	const LevelMapSaveReport removedSave = saveLevelMapAs(document, removedPath);
	ok &= expect(removedSave.succeeded(), "Save after key removal should succeed.");
	ok &= expect(!QString::fromUtf8(readFile(removedPath)).contains(QStringLiteral("\"message\"")), "Removed key should not be written back.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Key removal undo should work.");
	ok &= expect(propertyValueForTest(document, 0, QStringLiteral("message")) == QStringLiteral("hello"), "Key removal undo should restore the value.");
	return ok;
}

// Radiant-style comments head each object. The last entity shares its opening
// brace's line with a key, so it must be refused for deletion.
QByteArray editableMapFixture()
{
	return R"MAP(// Game: Quake
// entity 0
{
"classname" "worldspawn"
// brush 0
{
( 0 0 0 ) ( 128 0 0 ) ( 128 128 0 ) WALL1 0 0 0 1 1
( 0 0 16 ) ( 128 128 16 ) ( 128 0 16 ) CEIL1 0 0 0 1 1
}
// brush 1
{
( 0 0 32 ) ( 128 0 32 ) ( 128 128 32 ) WALL2 0 0 0 1 1
( 0 0 48 ) ( 128 128 48 ) ( 128 0 48 ) CEIL1 0 0 0 1 1
}
}
// entity 1
{
"classname" "light"
"origin" "32 32 64"
}
// entity 2
{
"classname" "func_door"
"targetname" "gate"
// brush 0
{
( 0 0 64 ) ( 64 0 64 ) ( 64 64 64 ) DOOR1 0 0 0 1 1
( 0 0 96 ) ( 64 64 96 ) ( 64 0 96 ) DOOR1 0 0 0 1 1
}
}
{ "classname" "info_null"
"origin" "0 0 0"
}
)MAP";
}

bool runAddDeleteSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("editable.map"));
	ok &= expect(writeFile(path, editableMapFixture()), "Editable fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Editable map should load.");
	const QString original = document.originalText;
	const int entityCount = static_cast<int>(document.entities.size());
	const int brushCount = static_cast<int>(document.brushes.size());
	ok &= expect(entityCount == 4 && brushCount == 3, "Editable map should hold four entities and three brushes.");

	// Adding: a new id past the last, selected, with its extra keys.
	int added = -1;
	ok &= expect(addLevelMapEntity(&document, QStringLiteral("info_player_deathmatch"), {64.0, 0.0, 24.0, true},
		{{QStringLiteral("angle"), QStringLiteral("90"), 0}, {QStringLiteral("origin"), QStringLiteral("9 9 9"), 0}}, &added, &error),
		"Adding a point entity should work.");
	ok &= expect(added == 4 && document.selectionKind == LevelMapSelectionKind::Entity && document.selectedObjectId == 4,
		"The new entity should take the next id and become the selection.");
	ok &= expect(propertyValueForTest(document, 4, QStringLiteral("origin")) == QStringLiteral("64 0 24")
			&& propertyValueForTest(document, 4, QStringLiteral("angle")) == QStringLiteral("90"),
		"The origin argument should win over an origin key, and extra keys should be kept.");
	ok &= expect(!addLevelMapEntity(&document, QStringLiteral("light\"x"), {0.0, 0.0, 0.0, true}, {}, nullptr, &error),
		"A class name holding a double quote should be refused.");
	ok &= expect(!setLevelMapEntityProperty(&document, 1, QStringLiteral("message"), QStringLiteral("say \"hi\""), &error),
		"A value holding a double quote should be refused for a text map.");

	// Deleting: worldspawn is refused; an entity takes its brushes; a lone brush
	// goes by itself; a brace sharing a line with a key is refused.
	ok &= expect(!deleteLevelMapObjects(&document, {{LevelMapSelectionKind::Entity, 0}}, &error), "worldspawn should never be deleted.");
	ok &= expect(!deleteLevelMapObjects(&document, {{LevelMapSelectionKind::Entity, 3}}, &error) && error.contains(QStringLiteral("shares")),
		"An entity whose brace shares a line should be refused.");
	ok &= expect(document.undoStack.size() == 1, "Refused deletions should not push undo commands.");
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("entity:1"), &error), "The light should be selectable.");
	ok &= expect(levelMapSelectionIsDeletable(document), "A selected light should be deletable.");
	ok &= expect(deleteLevelMapSelection(&document, &error), "Deleting the selected light should work.");
	ok &= expect(document.selection.isEmpty() && document.selectionKind == LevelMapSelectionKind::None,
		"Deleting the selection should clear it.");
	ok &= expect(deleteLevelMapObjects(&document, {{LevelMapSelectionKind::Entity, 2}, {LevelMapSelectionKind::QuakeBrush, 1}}, &error),
		"Deleting the door and a worldspawn brush together should work.");
	ok &= expect(document.entities.size() == entityCount - 1 && document.brushes.size() == brushCount - 2,
		"The door should take its brush with it.");
	ok &= expect(document.undoStack.last().description == QStringLiteral("Delete 2 objects"), "A multi-object delete should be one undo step.");

	const QString deletedPath = root.filePath(QStringLiteral("editable-deleted.map"));
	ok &= expect(saveLevelMapAs(document, deletedPath).succeeded(), "Saving after deletions should work.");
	const QString deletedText = QString::fromUtf8(readFile(deletedPath));
	ok &= expect(!deletedText.contains(QStringLiteral("\"light\"")) && !deletedText.contains(QStringLiteral("func_door"))
			&& !deletedText.contains(QStringLiteral("WALL2")) && !deletedText.contains(QStringLiteral("DOOR1")),
		"Deleted objects should not be written back.");
	ok &= expect(!deletedText.contains(QStringLiteral("// entity 1")) && !deletedText.contains(QStringLiteral("// brush 1"))
			&& deletedText.contains(QStringLiteral("// brush 0")),
		"A deleted object's heading comment should go with it, and others should stay.");
	ok &= expect(deletedText.endsWith(QStringLiteral("\"angle\" \"90\"\n}\n")), "The added entity should close the file, before its final line break.");
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({deletedPath, {}, QStringLiteral("idtech2")}, &reloaded, &error), "The saved map should load again.");
	ok &= expect(reloaded.entities.size() == 3 && reloaded.brushes.size() == 1 && !hasIssueCode(reloaded, QStringLiteral("unexpected-token")),
		"The saved map should hold worldspawn, info_null, and the added entity, with one brush and no stray tokens.");
	ok &= expect(deletedText.contains(QStringLiteral("{ \"classname\" \"info_null\"")), "A compact line should keep its brace when saved.");
	ok &= expect(reloaded.entities.size() == 3 && reloaded.entities.last().className == QStringLiteral("info_player_deathmatch"),
		"The added entity should be read back last.");

	// Removing the key that shares the compact line leaves the brace behind.
	ok &= expect(removeLevelMapEntityProperty(&document, 3, QStringLiteral("classname"), &error), "Removing a compact line's key should work.");
	const QString compactPath = root.filePath(QStringLiteral("editable-compact.map"));
	ok &= expect(saveLevelMapAs(document, compactPath).succeeded(), "Saving after removing a compact key should work.");
	const QString compactText = QString::fromUtf8(readFile(compactPath));
	ok &= expect(compactText.contains(QStringLiteral("}\n{\n\"origin\" \"0 0 0\"\n}")), "The brace on a removed key's line should stay.");

	// Undo everything: the save is byte-identical to the source.
	while (!document.undoStack.isEmpty()) {
		ok &= expect(undoLevelMapEdit(&document, &error), "Each add or delete should undo.");
	}
	ok &= expect(document.entities.size() == entityCount && document.brushes.size() == brushCount && document.deletedLineRanges.isEmpty(),
		"Undo should restore every object and drop every deleted range.");
	const QString restoredPath = root.filePath(QStringLiteral("editable-restored.map"));
	ok &= expect(saveLevelMapAs(document, restoredPath).succeeded(), "Saving after undo should work.");
	ok &= expect(QString::fromUtf8(readFile(restoredPath)) == original, "After undoing every edit the save should match the source exactly.");
	bool inOrder = true;
	for (int index = 0; index < document.entities.size(); ++index) {
		inOrder = inOrder && document.entities.at(index).id == index;
	}
	ok &= expect(inOrder, "Undo should put entities back in their original order.");

	// Redo replays the same edits.
	ok &= expect(redoLevelMapEdit(&document, &error) && redoLevelMapEdit(&document, &error) && redoLevelMapEdit(&document, &error)
			&& redoLevelMapEdit(&document, &error),
		"Add, deletes, and the key removal should redo.");
	ok &= expect(document.entities.size() == entityCount - 1 && document.brushes.size() == brushCount - 2, "Redo should delete the same objects.");

	// A Doom vertex between two linedefs dissolves, joining them; entities
	// are refused.
	LevelMapDocument doom;
	const QString wadPath = root.filePath(QStringLiteral("delete.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Doom fixture should be written.");
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Doom fixture should load.");
	ok &= expect(deleteLevelMapObjects(&doom, {{LevelMapSelectionKind::DoomVertex, 0}}, &error) && doom.doomVertices.size() == 3
			&& doom.doomLinedefs.size() == 3,
		"Deleting a Doom vertex between two linedefs should join them into one.");
	ok &= expect(!addLevelMapEntity(&doom, QStringLiteral("light"), {0.0, 0.0, 0.0, true}, {}, nullptr, &error), "Doom maps should not take entities.");
	return ok;
}

// A trigger fires a door; a relay lights a lamp and removes the door; one key
// names nothing, and one entity names itself.
QByteArray linkedMapFixture()
{
	return R"MAP({
"classname" "worldspawn"
}
{
"classname" "trigger_once"
"target" "door1"
{
( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) TRIGGER 0 0 0 1 1
( 64 0 0 ) ( 64 0 1 ) ( 64 1 0 ) TRIGGER 0 0 0 1 1
( 0 0 0 ) ( 0 0 1 ) ( 1 0 0 ) TRIGGER 0 0 0 1 1
( 0 64 0 ) ( 1 64 0 ) ( 0 64 1 ) TRIGGER 0 0 0 1 1
( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) TRIGGER 0 0 0 1 1
( 0 0 64 ) ( 0 1 64 ) ( 1 0 64 ) TRIGGER 0 0 0 1 1
}
}
{
"classname" "func_door"
"targetname" "door1"
{
( 128 0 0 ) ( 128 1 0 ) ( 128 0 1 ) DOOR 0 0 0 1 1
( 192 0 0 ) ( 192 0 1 ) ( 192 1 0 ) DOOR 0 0 0 1 1
( 128 0 0 ) ( 128 0 1 ) ( 129 0 0 ) DOOR 0 0 0 1 1
( 128 64 0 ) ( 129 64 0 ) ( 128 64 1 ) DOOR 0 0 0 1 1
( 128 0 0 ) ( 129 0 0 ) ( 128 1 0 ) DOOR 0 0 0 1 1
( 128 0 128 ) ( 128 1 128 ) ( 129 0 128 ) DOOR 0 0 0 1 1
}
}
{
"classname" "light"
"origin" "256 256 64"
"targetname" "lamp"
}
{
"classname" "trigger_relay"
"origin" "256 0 16"
"target" "lamp"
"killtarget" "door1"
"pathtarget" "nowhere"
}
{
"classname" "info_notnull"
"origin" "-64 -64 0"
"targetname" "self"
"target" "self"
}
)MAP";
}

bool runTargetLinksSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("linked.map"));
	ok &= expect(writeFile(path, linkedMapFixture()), "Linked fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Linked map should load.");

	LevelMapVec3 door;
	ok &= expect(levelMapEntityAnchor(document, 2, &door) && nearly(door.x, 160.0) && nearly(door.y, 32.0) && nearly(door.z, 64.0),
		"A brush entity should be anchored at the centre of its brushes.");
	ok &= expect(!levelMapEntityAnchor(document, 0), "worldspawn with no brushes has no anchor.");

	const QVector<LevelMapTargetLink> links = levelMapTargetLinks(document);
	ok &= expect(links.size() == 3, "Expected three links: trigger to door, relay to lamp, relay kills door.");
	ok &= expect(levelMapStatisticsLines(document).contains(QStringLiteral("Target links: 3")), "Statistics should count the target links.");
	if (links.size() == 3) {
		ok &= expect(links.at(0).sourceEntityId == 1 && links.at(0).targetEntityId == 2 && links.at(0).key == QStringLiteral("target"),
			"The trigger should link to the door.");
		ok &= expect(links.at(1).sourceEntityId == 4 && links.at(1).targetEntityId == 3 && nearly(links.at(1).to.x, 256.0),
			"The relay should link to the lamp at its origin.");
		ok &= expect(links.at(2).key == QStringLiteral("killtarget") && links.at(2).targetEntityId == 2 && links.at(2).name == QStringLiteral("door1"),
			"The relay's killtarget should link to the door.");
	}

	// Links follow edits: renaming the lamp breaks the relay's link.
	ok &= expect(setLevelMapEntityProperty(&document, 3, QStringLiteral("targetname"), QStringLiteral("lamp2"), &error), "Renaming should work.");
	ok &= expect(levelMapTargetLinks(document).size() == 2, "A renamed target should drop its link.");

	LevelMapDocument doom;
	const QString wadPath = root.filePath(QStringLiteral("links.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Doom fixture should be written.");
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Doom fixture should load.");
	ok &= expect(levelMapTargetLinks(doom).isEmpty(), "Doom maps have no target links.");
	return ok;
}

bool runDuplicateSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("duplicate.map"));
	// Preserve the old source-line assertions, but close the fixture's slabs:
	// placement now validates the resulting solids through the move service.
	auto fixture = editableMapFixture();
	const auto sides = [](int width) {
		return QStringLiteral("( %1 1 0 ) ( %1 0 0 ) ( %1 0 1 ) SIDE 0 0 0 1 1\n"
			"( 0 0 1 ) ( 0 0 0 ) ( 0 1 0 ) SIDE 0 0 0 1 1\n"
			"( 0 %1 1 ) ( 0 %1 0 ) ( 1 %1 0 ) SIDE 0 0 0 1 1\n"
			"( 1 0 0 ) ( 0 0 0 ) ( 0 0 1 ) SIDE 0 0 0 1 1\n").arg(width).toUtf8();
	};
	fixture.replace("CEIL1 0 0 0 1 1\n}", QByteArray("CEIL1 0 0 0 1 1\n") + sides(128) + "}");
	const QByteArray doorTop("( 0 0 96 ) ( 64 64 96 ) ( 64 0 96 ) DOOR1 0 0 0 1 1\n");
	fixture.replace(doorTop + "}", doorTop + sides(64) + "}");
	ok &= expect(writeFile(path, fixture), "Duplicate fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Duplicate fixture should load.");
	const QString original = document.originalText;

	// A light, a worldspawn brush, and a door with its brush, in one step.
	ok &= expect(duplicateLevelMapObjects(&document,
				 {{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::QuakeBrush, 1}, {LevelMapSelectionKind::Entity, 2}},
				 64.0, 0.0, 0.0, &error),
		"Duplicating a light, a brush, and a door should work.");
	ok &= expect(document.entities.size() == 6 && document.brushes.size() == 5 && document.undoStack.size() == 1,
		"The copies should add two entities and two brushes as one undo step.");
	ok &= expect(document.selection.size() == 3 && document.selectionKind == LevelMapSelectionKind::QuakeBrush,
		"The copies should become the selection.");
	ok &= expect(propertyValueForTest(document, 4, QStringLiteral("origin")) == QStringLiteral("96 32 64"), "The light's copy should move by the delta.");
	ok &= expect(!duplicateLevelMapObjects(&document, {{LevelMapSelectionKind::Entity, 0}}, 0.0, 0.0, 0.0, &error),
		"worldspawn should never be copied.");

	const QString copiedPath = root.filePath(QStringLiteral("duplicate-copied.map"));
	ok &= expect(saveLevelMapAs(document, copiedPath).succeeded(), "Saving copies should work.");
	const QString copiedText = QString::fromUtf8(readFile(copiedPath));
	const qsizetype brushCopy = copiedText.indexOf(QStringLiteral("( 64 0 32 ) ( 192 0 32 ) ( 192 128 32 ) WALL2"));
	ok &= expect(brushCopy > 0 && brushCopy < copiedText.indexOf(QStringLiteral("// entity 1")),
		"A copied worldspawn brush should be written inside worldspawn.");
	ok &= expect(copiedText.contains(QStringLiteral("( 0 0 32 ) ( 128 0 32 ) ( 128 128 32 ) WALL2")), "The original brush should stay.");
	ok &= expect(copiedText.contains(QStringLiteral("{\n\"classname\" \"light\"\n\"origin\" \"96 32 64\"\n}")),
		"The light's copy should be appended with its moved origin.");
	ok &= expect(copiedText.contains(QStringLiteral("\"targetname\" \"gate\"\n{\n( 64 0 64 ) ( 128 0 64 ) ( 128 64 64 ) DOOR1")),
		"The door's copy should carry its own moved brush.");
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({copiedPath, {}, QStringLiteral("idtech2")}, &reloaded, &error), "The saved copies should load.");
	ok &= expect(reloaded.entities.size() == 6 && reloaded.brushes.size() == 5 && !hasIssueCode(reloaded, QStringLiteral("unexpected-token")),
		"The saved copies should read back as whole entities and brushes.");

	// Moving a copy rewrites the copy, never the brush it came from; a copy of
	// a copy starts from where the first copy is now.
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("brush:3"), &error), "The brush copy should be selectable.");
	ok &= expect(moveLevelMapSelection(&document, 0.0, 0.0, 16.0, &error), "Moving the brush copy should work.");
	ok &= expect(duplicateLevelMapSelection(&document, 64.0, 0.0, 0.0, &error), "Copying the copy should work.");
	const QString movedPath = root.filePath(QStringLiteral("duplicate-moved.map"));
	ok &= expect(saveLevelMapAs(document, movedPath).succeeded(), "Saving moved copies should work.");
	const QString movedText = QString::fromUtf8(readFile(movedPath));
	ok &= expect(movedText.contains(QStringLiteral("( 64 0 48 ) ( 192 0 48 ) ( 192 128 48 ) WALL2"))
			&& movedText.contains(QStringLiteral("( 128 0 48 ) ( 256 0 48 ) ( 256 128 48 ) WALL2"))
			&& movedText.contains(QStringLiteral("( 0 0 32 ) ( 128 0 32 ) ( 128 128 32 ) WALL2")),
		"Moves should land on the copies alone.");

	while (!document.undoStack.isEmpty()) {
		ok &= expect(undoLevelMapEdit(&document, &error), "Each duplicate and move should undo.");
	}
	const QString restoredPath = root.filePath(QStringLiteral("duplicate-restored.map"));
	ok &= expect(saveLevelMapAs(document, restoredPath).succeeded(), "Saving after undo should work.");
	ok &= expect(QString::fromUtf8(readFile(restoredPath)) == original, "Undoing the copies should restore the source exactly.");
	return ok;
}

bool runDoomLinedefEditSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int count = 0;
	const QString wadPath = root.filePath(QStringLiteral("lines.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Linedef fixture should be written.");
	LevelMapDocument doom;
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Linedef fixture should load.");

	// Splitting the bottom edge of the square puts a vertex at (64, 0), ends
	// the linedef there, and adds its second half with its own copy of the
	// side the four edges share.
	ok &= expect(selectLevelMapObject(&doom, QStringLiteral("linedef:0"), &error) && splitLevelMapLinedefs(&doom, &count, &error) && count == 1,
		"Splitting linedef 0 should work.");
	ok &= expect(doom.doomVertices.size() == 5 && doom.doomVertices.last().x == 64.0 && doom.doomVertices.last().y == 0.0
			&& doom.doomLinedefs.size() == 5 && doom.doomLinedefs.first().endVertex == 4 && doom.doomLinedefs.last().startVertex == 4
			&& doom.doomLinedefs.last().endVertex == 1 && doom.doomSidedefs.size() == 2 && doom.doomLinedefs.last().frontSidedef == 1
			&& doom.doomSidedefs.last().sector == 0 && doom.selection.size() == 2,
		"The split should add the middle vertex, the second half, and its own side, both halves selected.");
	ok &= expect(doom.doomSidedefs.at(1).offsetX == 4 + 64, "The second half's side should carry the texture on from the first half.");
	ok &= expect(doom.textureReferences.size() == 8, "The new side's textures should join the map's texture list.");
	ok &= expect(doom.doomGeometryChanged, "A split should mark the node lumps stale.");
	ok &= expect(doom.undoStack.last().description == QStringLiteral("Split linedef:0"), "The history should name the split linedef.");
	const QString splitPath = root.filePath(QStringLiteral("lines-split.wad"));
	LevelMapDocument reloaded;
	ok &= expect(saveLevelMapAs(doom, splitPath).succeeded() && loadLevelMap({splitPath, QStringLiteral("MAP01"), {}}, &reloaded, &error)
			&& reloaded.doomVertices.size() == 5 && reloaded.doomLinedefs.size() == 5 && reloaded.doomSidedefs.size() == 2,
		"The split map should be written and read back with its new records.");
	const QVector<DoomSectorOutline> outlines = buildDoomSectorOutlines(reloaded);
	ok &= expect(!outlines.isEmpty() && outlines.first().openEdgeCount == 0, "The sector should still close after the split.");

	// Undo takes the new records off again.
	ok &= expect(undoLevelMapEdit(&doom, &error) && doom.doomVertices.size() == 4 && doom.doomLinedefs.size() == 4 && doom.doomSidedefs.size() == 1
			&& doom.doomLinedefs.first().endVertex == 1,
		"Undo should join the linedef again.");
	ok &= expect(!doom.doomGeometryChanged && doom.textureReferences.size() == 5,
		"Undoing the split should leave the node lumps current and the texture list as loaded.");
	ok &= expect(redoLevelMapEdit(&doom, &error) && doom.doomLinedefs.size() == 5 && undoLevelMapEdit(&doom, &error), "Redo should split it again.");

	// Flipping swaps the ends of a one-sided line and keeps its only side.
	ok &= expect(selectLevelMapObject(&doom, QStringLiteral("linedef:2"), &error) && flipLevelMapLinedefs(&doom, &count, &error)
			&& doom.doomLinedefs.at(2).startVertex == 3 && doom.doomLinedefs.at(2).endVertex == 2 && doom.doomLinedefs.at(2).frontSidedef == 0
			&& doom.doomLinedefs.at(2).backSidedef == -1,
		"Flipping linedef 2 should swap its ends and keep its side in front.");
	ok &= expect(undoLevelMapEdit(&doom, &error) && doom.doomLinedefs.at(2).startVertex == 2, "The flip should undo.");

	// Things are not linedefs.
	ok &= expect(selectLevelMapObject(&doom, QStringLiteral("thing:0"), &error) && !splitLevelMapLinedefs(&doom, &count, &error)
			&& error.contains(QStringLiteral("linedefs")),
		"Splitting with no linedef selected should say what to select.");
	return ok;
}

bool runDoomThingEditSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("things.wad"));
	ok &= expect(writeFile(path, wadFixture()), "Doom fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, QStringLiteral("MAP01"), {}}, &document, &error), "Doom fixture should load.");
	ok &= expect(document.doomThings.size() == 1 && document.doomThings.first().type == 1, "The fixture should hold one player start.");
	ok &= expect(!levelMapDoomThingTypes(LevelMapDoomFormat::Doom).isEmpty()
			&& levelMapDoomThingTypes(LevelMapDoomFormat::Hexen).size() < levelMapDoomThingTypes(LevelMapDoomFormat::Doom).size(),
		"Doom should offer more thing types than the ones every format shares.");

	// Adding: a new id, all skills, selected, mirrored for the inspector.
	int added = -1;
	ok &= expect(addLevelMapDoomThing(&document, 3004, 64.4, 96.0, 450, &added, &error), "Adding a Zombieman should work.");
	ok &= expect(added == 1 && document.selectionKind == LevelMapSelectionKind::DoomThing && document.selectedObjectId == 1,
		"The new thing should take the next id and become the selection.");
	ok &= expect(document.doomThings.size() == 2 && document.doomThings.last().x == 64.0 && document.doomThings.last().angle == 90
			&& document.doomThings.last().flags == 7,
		"The new thing should sit on whole units, face a normalized angle, and appear on every skill.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("type")) == QStringLiteral("3004"), "The new thing should be mirrored as an entity.");
	ok &= expect(!addLevelMapDoomThing(&document, 3004, 40000.0, 0.0, 0, nullptr, &error), "A thing outside the 16-bit range should be refused.");
	ok &= expect(!addLevelMapDoomThing(&document, 0, 0.0, 0.0, 0, nullptr, &error), "Type 0 should be refused.");

	// Duplicating and deleting, then editing a thing whose id is past the
	// vector's end: ids are not positions once things come and go.
	ok &= expect(duplicateLevelMapSelection(&document, 32.0, 0.0, 0.0, &error), "Duplicating the new thing should work.");
	ok &= expect(document.doomThings.size() == 3 && document.selectedObjectId == 2 && document.doomThings.last().x == 96.0,
		"The copy should be offset and selected.");
	ok &= expect(deleteLevelMapObjects(&document, {{LevelMapSelectionKind::DoomThing, 0}}, &error), "Deleting the player start should work.");
	ok &= expect(document.doomThings.size() == 2 && document.entities.size() == 2, "A deleted thing should take its mirror with it.");
	ok &= expect(setLevelMapEntityProperty(&document, 2, QStringLiteral("angle"), QStringLiteral("180"), &error)
			&& document.doomThings.last().angle == 180,
		"Editing a thing by id should reach the right record after a deletion.");
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("entity"), 2, 0.0, 32.0, 0.0, &error) && document.doomThings.last().y == 128.0,
		"Moving a thing's mirror should move the right record after a deletion.");

	const QString savedPath = root.filePath(QStringLiteral("things-edited.wad"));
	ok &= expect(saveLevelMapAs(document, savedPath).succeeded(), "Saving the edited things should work.");
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({savedPath, QStringLiteral("MAP01"), {}}, &reloaded, &error), "The edited WAD should load.");
	ok &= expect(reloaded.doomThings.size() == 2 && reloaded.doomThings.at(0).type == 3004 && reloaded.doomThings.at(1).angle == 180
			&& reloaded.doomThings.at(1).y == 128.0,
		"The saved THINGS lump should hold the added and copied Zombiemen with their edits.");

	while (!document.undoStack.isEmpty()) {
		ok &= expect(undoLevelMapEdit(&document, &error), "Each thing edit should undo.");
	}
	ok &= expect(document.doomThings.size() == 1 && document.doomThings.first().type == 1 && document.doomThings.first().id == 0,
		"Undo should bring back the player start alone.");
	return ok;
}

bool runSnapToGridSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("snap.map"));
	ok &= expect(writeFile(path, editableMapFixture()), "Snap fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Snap fixture should load.");
	int alignedBrush = -1;
	ok &= expect(addLevelMapBoxBrush(&document, {0,0,0,true}, {32,32,32,true}, QStringLiteral("WALL1"), &alignedBrush, &error),
		"Snapping requires an actual closed brush.");
	ok &= expect(setLevelMapEntityProperty(&document, 1, QStringLiteral("origin"), QStringLiteral("35 29 20"), &error), "Moving the light off the grid should work.");
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, alignedBrush}, {LevelMapSelectionKind::Entity, 1}}, &error),
		"Selecting a brush and the light should work.");
	const int before = static_cast<int>(document.undoStack.size());

	// Each object snaps by its own amount: the light to the nearest grid
	// point, the brush, already on the grid, not at all.
	ok &= expect(snapLevelMapSelectionToGrid(&document, 16.0, &error), "Snapping to the grid should work.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("32 32 16"), "The light should land on the grid.");
	ok &= expect(document.undoStack.size() == before + 1 && document.undoStack.last().moveSteps.size() == 1,
		"Only the light should move, as one undo step.");
	ok &= expect(document.selection.size() == 2, "Snapping should keep the selection.");
	ok &= expect(!snapLevelMapSelectionToGrid(&document, 16.0, &error) && error.contains(QStringLiteral("already")),
		"Snapping again should say the selection is already on the grid.");
	LevelMapDocument brushEntity = document;
	ok &= expect(setLevelMapSelection(&brushEntity, {{LevelMapSelectionKind::Entity, 2}}, &error)
			&& !snapLevelMapSelectionToGrid(&brushEntity, 16.0, &error) && error.contains(QStringLiteral("solved bounds")),
		"The old open-plane brush-entity fixture cannot be snapped without solved bounds.");
	ok &= expect(undoLevelMapEdit(&document, &error) && propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("35 29 20"),
		"Undo should put the light back off the grid.");
	ok &= expect(redoLevelMapEdit(&document, &error) && propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("32 32 16"),
		"Redo should snap it again by its own amount.");
	return ok;
}

bool runClipboardSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString sourcePath = root.filePath(QStringLiteral("clip-source.map"));
	const QString targetPath = root.filePath(QStringLiteral("clip-target.map"));
	ok &= expect(writeFile(sourcePath, editableMapFixture()) && writeFile(targetPath, quakeMapFixture()), "Clipboard fixtures should be written.");
	LevelMapDocument source;
	LevelMapDocument target;
	ok &= expect(loadLevelMap({sourcePath, {}, QStringLiteral("idtech2")}, &source, &error)
			&& loadLevelMap({targetPath, {}, QStringLiteral("idtech2")}, &target, &error),
		"Clipboard fixtures should load.");
	const QString targetOriginal = target.originalText;
	const int targetEntities = static_cast<int>(target.entities.size());
	const int targetBrushes = static_cast<int>(target.brushes.size());

	// Copy: a worldspawn brush bare, and the door whole with its brush.
	ok &= expect(setLevelMapSelection(&source, {{LevelMapSelectionKind::QuakeBrush, 1}, {LevelMapSelectionKind::Entity, 2}}, &error),
		"Selecting a brush and the door should work.");
	const QString copied = levelMapSelectionText(source);
	ok &= expect(copied.startsWith(QStringLiteral("{\n( 0 0 32 ) ( 128 0 32 ) ( 128 128 32 ) WALL2")),
		"A worldspawn brush should be copied as a bare block, in its source format.");
	ok &= expect(copied.contains(QStringLiteral("{\n\"classname\" \"func_door\"\n\"targetname\" \"gate\"\n{\n( 0 0 64 )")),
		"The door should be copied whole, with its keys and brush.");

	// Paste into another map: the bare brush joins worldspawn, the door comes
	// in as a new entity, and both are selected.
	ok &= expect(pasteLevelMapText(&target, copied, &error), "Pasting into another map should work.");
	ok &= expect(target.entities.size() == targetEntities + 1 && target.brushes.size() == targetBrushes + 2 && target.selection.size() == 2
			&& target.undoStack.size() == 1,
		"Paste should add the door and two brushes, select them, and make one undo step.");
	const QString pastedPath = root.filePath(QStringLiteral("clip-pasted.map"));
	ok &= expect(saveLevelMapAs(target, pastedPath).succeeded(), "Saving the pasted map should work.");
	const QString pastedText = QString::fromUtf8(readFile(pastedPath));
	const qsizetype pastedBrush = pastedText.indexOf(QStringLiteral("( 0 0 32 ) ( 128 0 32 ) ( 128 128 32 ) WALL2"));
	ok &= expect(pastedBrush > 0 && pastedBrush < pastedText.indexOf(QStringLiteral("\"classname\" \"light\"")),
		"The bare brush should land inside the target's worldspawn.");
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({pastedPath, {}, QStringLiteral("idtech2")}, &reloaded, &error)
			&& reloaded.entities.size() == targetEntities + 1 && reloaded.brushes.size() == targetBrushes + 2,
		"The pasted map should read back whole.");

	// TrenchBroom puts worldspawn brushes on the clipboard bare, with comments.
	ok &= expect(pasteLevelMapText(&target,
				 QStringLiteral("// brush 0\n{\n( 0 0 0 ) ( 64 0 0 ) ( 64 64 0 ) TB_TEX 0 0 0 1 1\n( 0 0 8 ) ( 64 64 8 ) ( 64 0 8 ) TB_TEX 0 0 0 1 1\n}\n"), &error)
			&& target.brushes.size() == targetBrushes + 3 && target.selectionKind == LevelMapSelectionKind::QuakeBrush,
		"A TrenchBroom-style bare brush should paste into worldspawn.");
	ok &= expect(!pasteLevelMapText(&target, QStringLiteral("hello world"), &error), "Text that is not a map should be refused.");
	ok &= expect(!pasteLevelMapText(&target, QStringLiteral("{\n\"classname\" \"light\"\n"), &error), "An unclosed block should be refused.");

	while (!target.undoStack.isEmpty()) {
		ok &= expect(undoLevelMapEdit(&target, &error), "Each paste should undo.");
	}
	const QString restoredPath = root.filePath(QStringLiteral("clip-restored.map"));
	ok &= expect(saveLevelMapAs(target, restoredPath).succeeded() && QString::fromUtf8(readFile(restoredPath)) == targetOriginal,
		"Undoing the pastes should restore the map exactly.");
	return ok;
}

bool runReplaceTextureSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int replaced = 0;

	// Classic faces: across the map, then only in the selection.
	const QString path = root.filePath(QStringLiteral("textures.map"));
	ok &= expect(writeFile(path, editableMapFixture()), "Texture fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Texture fixture should load.");
	const QString original = document.originalText;
	const QVector<LevelMapTextureUse> usage = levelMapTextureUsage(document);
	int ceilings = 0;
	for (const LevelMapTextureUse& use : usage) {
		ceilings += use.name == QStringLiteral("CEIL1") ? use.count : 0;
	}
	ok &= expect(ceilings == 2, "CEIL1 should be counted on both worldspawn brushes.");
	ok &= expect(replaceLevelMapTexture(&document, QStringLiteral("ceil1"), QStringLiteral("SKY1"), false, &replaced, &error) && replaced == 2,
		"Replacing CEIL1, whatever its case, should change both faces.");
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("brush:1"), &error), "The second brush should be selectable.");
	ok &= expect(!replaceLevelMapTexture(&document, QStringLiteral("DOOR1"), QStringLiteral("METAL"), true, &replaced, &error),
		"The selection does not use DOOR1, so nothing should change.");
	ok &= expect(replaceLevelMapTexture(&document, QStringLiteral("WALL2"), QStringLiteral("METAL"), true, &replaced, &error) && replaced == 1,
		"Replacing within the selection should change its one face.");
	ok &= expect(!replaceLevelMapTexture(&document, QStringLiteral("WALL1"), QStringLiteral("bad name"), false, &replaced, &error),
		"A texture name with a space should be refused.");
	ok &= expect(document.undoStack.size() == 2, "Each replacement should be one undo step.");
	const QString savedPath = root.filePath(QStringLiteral("textures-replaced.map"));
	ok &= expect(saveLevelMapAs(document, savedPath).succeeded(), "Saving replaced textures should work.");
	const QString savedText = QString::fromUtf8(readFile(savedPath));
	ok &= expect(!savedText.contains(QStringLiteral("CEIL1")) && savedText.contains(QStringLiteral("( 0 0 16 ) ( 128 128 16 ) ( 128 0 16 ) SKY1 0 0 0 1 1"))
			&& savedText.contains(QStringLiteral("( 0 0 32 ) ( 128 0 32 ) ( 128 128 32 ) METAL 0 0 0 1 1")),
		"Replaced names should be written in place, the rest of each face line untouched.");
	while (!document.undoStack.isEmpty()) {
		ok &= expect(undoLevelMapEdit(&document, &error), "Each replacement should undo.");
	}
	const QString restoredPath = root.filePath(QStringLiteral("textures-restored.map"));
	ok &= expect(saveLevelMapAs(document, restoredPath).succeeded() && QString::fromUtf8(readFile(restoredPath)) == original,
		"Undoing the replacements should restore the map exactly.");

	// Applying a texture puts it on every face of the selection, as one undo
	// step, and leaves other brushes alone.
	int applied = 0;
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}}, &error)
			&& applyLevelMapTexture(&document, QStringLiteral("METAL1"), &applied, &error) && applied == 2,
		"Applying METAL1 to the door should change both of its faces.");
	ok &= expect(levelMapObjectsUsingTexture(document, QStringLiteral("METAL1")).size() == 1
			&& levelMapObjectsUsingTexture(document, QStringLiteral("DOOR1")).isEmpty(),
		"Only the door should use METAL1 now.");
	ok &= expect(!applyLevelMapTexture(&document, QStringLiteral("METAL1"), &applied, &error) && error.contains(QStringLiteral("already")),
		"Applying the texture the selection already uses should change nothing.");
	ok &= expect(undoLevelMapEdit(&document, &error) && levelMapObjectsUsingTexture(document, QStringLiteral("DOOR1")).size() == 1,
		"Undo should put the door's own texture back.");

	// Objects using a texture: both worldspawn brushes use CEIL1; only the
	// door uses DOOR1.
	const QVector<LevelMapSelectionRef> ceilingBrushes = levelMapObjectsUsingTexture(document, QStringLiteral("ceil1"));
	ok &= expect(ceilingBrushes.size() == 2 && ceilingBrushes.first().kind == LevelMapSelectionKind::QuakeBrush,
		"Both brushes using CEIL1 should be found, whatever the case.");
	ok &= expect(levelMapObjectsUsingTexture(document, QStringLiteral("DOOR1")).size() == 1, "Only the door brush uses DOOR1.");

	// Quake III: a brushDef face after its texture matrix, a quoted brushDef3
	// name, and a patch's shader line.
	const QString q3Path = root.filePath(QStringLiteral("textures-q3.map"));
	ok &= expect(writeFile(q3Path, quake3MapFixture()), "Quake III fixture should be written.");
	LevelMapDocument q3;
	ok &= expect(loadLevelMap({q3Path, {}, QStringLiteral("idtech3")}, &q3, &error), "Quake III fixture should load.");
	ok &= expect(replaceLevelMapTexture(&q3, QStringLiteral("textures/common/caulk"), QStringLiteral("textures/common/nodraw"), false, &replaced, &error)
			&& replaced == 7,
		"caulk should be replaced on the brushDef face and all six brushDef3 faces.");
	ok &= expect(replaceLevelMapTexture(&q3, QStringLiteral("textures/base_wall/curve"), QStringLiteral("textures/base_wall/pipe"), false, &replaced, &error)
			&& replaced == 1,
		"The patch's shader should be replaceable.");
	const QString q3Saved = root.filePath(QStringLiteral("textures-q3-saved.map"));
	ok &= expect(saveLevelMapAs(q3, q3Saved).succeeded(), "Saving the Quake III map should work.");
	const QString q3Text = QString::fromUtf8(readFile(q3Saved));
	ok &= expect(q3Text.contains(QStringLiteral("( ( 0.0078125 0 0 ) ( 0 0.0078125 0.25 ) ) textures/common/nodraw 0 4 0"))
			&& q3Text.contains(QStringLiteral("( 0 0 1 -64 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) \"textures/common/nodraw\" 0 0 0"))
			&& q3Text.contains(QStringLiteral("\ntextures/base_wall/pipe\n( 3 3 0 0 0 )")) && !q3Text.contains(QStringLiteral("caulk")),
		"Quake III names should be replaced after the texture matrix, inside their quotes, and on the patch's shader line.");
	LevelMapDocument q3Reloaded;
	ok &= expect(loadLevelMap({q3Saved, {}, QStringLiteral("idtech3")}, &q3Reloaded, &error) && q3Reloaded.patches.size() == 1
			&& q3Reloaded.patches.first().textureName == QStringLiteral("textures/base_wall/pipe"),
		"The replaced patch shader should read back.");

	// Doom: flats and wall textures in the binary records, eight characters at most.
	const QString wadPath = root.filePath(QStringLiteral("textures.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Doom fixture should be written.");
	LevelMapDocument doom;
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Doom fixture should load.");
	ok &= expect(!applyLevelMapTexture(&doom, QStringLiteral("FLAT5"), nullptr, &error) && error.contains(QStringLiteral("Replace Texture")),
		"Applying a texture should point a Doom map at Replace Texture.");
	// Every fixture linedef faces sidedef 0, whose middle texture is WALLMID.
	ok &= expect(levelMapObjectsUsingTexture(doom, QStringLiteral("WALLMID")).size() == 4
			&& levelMapObjectsUsingTexture(doom, QStringLiteral("FLOOR1")).first().kind == LevelMapSelectionKind::DoomSector,
		"A Doom wall texture should find the linedefs whose sides use it, and a flat its sector.");
	ok &= expect(replaceLevelMapTexture(&doom, QStringLiteral("FLOOR1"), QStringLiteral("FLAT5"), false, &replaced, &error) && replaced == 1
			&& replaceLevelMapTexture(&doom, QStringLiteral("WALLMID"), QStringLiteral("STARTAN2"), false, &replaced, &error) && replaced == 1,
		"A Doom flat and a wall texture should be replaceable.");
	ok &= expect(!replaceLevelMapTexture(&doom, QStringLiteral("CEIL1"), QStringLiteral("TOOLONGNAME"), false, &replaced, &error),
		"A Doom texture name longer than eight characters should be refused.");
	const QString wadSaved = root.filePath(QStringLiteral("textures-saved.wad"));
	ok &= expect(saveLevelMapAs(doom, wadSaved).succeeded(), "Saving the Doom map should work.");
	LevelMapDocument doomReloaded;
	ok &= expect(loadLevelMap({wadSaved, QStringLiteral("MAP01"), {}}, &doomReloaded, &error)
			&& doomReloaded.doomSectors.first().floorTexture == QStringLiteral("FLAT5")
			&& doomReloaded.doomSidedefs.first().middleTexture == QStringLiteral("STARTAN2"),
		"The replaced Doom textures should read back.");
	return ok;
}

bool runRotateSmoke(const QDir& root)
{
	bool ok = true;
	QString error;

	// A brush entity turns about its own centre; a light turns its angle.
	const QString path = root.filePath(QStringLiteral("rotate.map"));
	ok &= expect(writeFile(path, linkedMapFixture()), "Rotation fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Rotation fixture should load.");
	const QString original = document.originalText;
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}}, &error)
			&& rotateLevelMapSelection(&document, 2, 1, &error),
		"Turning the door a quarter turn should work.");
	const QString doorPath = root.filePath(QStringLiteral("rotate-door.map"));
	ok &= expect(saveLevelMapAs(document, doorPath).succeeded(), "Saving the turned door should work.");
	const QString doorText = QString::fromUtf8(readFile(doorPath));
	ok &= expect(doorText.contains(QStringLiteral("( 192 0 0 ) ( 191 0 0 ) ( 192 0 1 ) DOOR ")),
		"The door's face points should turn about the door's centre.");
	ok &= expect(undoLevelMapEdit(&document, &error), "The turn should undo.");
	ok &= expect(setLevelMapEntityProperty(&document, 3, QStringLiteral("angle"), QStringLiteral("45"), &error), "Giving the light an angle should work.");
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 3}}, &error)
			&& rotateLevelMapSelection(&document, 2, -1, &error)
			&& propertyValueForTest(document, 3, QStringLiteral("angle")) == QStringLiteral("315")
			&& propertyValueForTest(document, 3, QStringLiteral("origin")) == QStringLiteral("256 256 64"),
		"A clockwise quarter turn should take 45 to 315 and keep a lone light in place.");
	ok &= expect(undoLevelMapEdit(&document, &error) && undoLevelMapEdit(&document, &error), "The angle edits should undo.");

	// Four quarter turns of the door and the light together give back the
	// very same file: quarter turns are exact.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}, {LevelMapSelectionKind::Entity, 3}}, &error),
		"Selecting the door and the light should work.");
	for (int turn = 0; turn < 4; ++turn) {
		ok &= expect(rotateLevelMapSelection(&document, 2, 1, &error), "Each quarter turn should work.");
	}
	ok &= expect(document.undoStack.size() == 4, "Each turn should be one undo step.");
	const QString roundPath = root.filePath(QStringLiteral("rotate-round.map"));
	ok &= expect(saveLevelMapAs(document, roundPath).succeeded() && QString::fromUtf8(readFile(roundPath)) == original,
		"Four quarter turns should write the source back exactly.");

	// Valve 220 texture axes turn with their faces.
	const QString valvePath = root.filePath(QStringLiteral("rotate-valve.map"));
	ok &= expect(writeFile(valvePath, valve220MapFixture()), "Valve fixture should be written.");
	LevelMapDocument valve;
	ok &= expect(loadLevelMap({valvePath, {}, QStringLiteral("idtech2")}, &valve, &error), "Valve fixture should load.");
	ok &= expect(setLevelMapSelection(&valve, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error) && rotateLevelMapSelection(&valve, 2, 1, &error),
		"Turning the Valve brush should work.");
	const QString valveSaved = root.filePath(QStringLiteral("rotate-valve-saved.map"));
	ok &= expect(saveLevelMapAs(valve, valveSaved).succeeded()
			&& QString::fromUtf8(readFile(valveSaved)).contains(QStringLiteral("( 63 64 0 ) ( 64 64 0 ) ( 64 64 1 ) EASTTEX [ -1 0 0 64 ] [ 0 0 -1 0 ] 0 1 1")),
		"A Valve face should turn its points and its texture axes, and shift its offset so the texture stays put.");
	for (int turn = 0; turn < 3; ++turn) {
		ok &= expect(rotateLevelMapSelection(&valve, 2, 1, &error), "Each further Valve turn should work.");
	}
	const QString valveRound = root.filePath(QStringLiteral("rotate-valve-round.map"));
	ok &= expect(saveLevelMapAs(valve, valveRound).succeeded() && QString::fromUtf8(readFile(valveRound)) == QString::fromUtf8(valve220MapFixture()),
		"Four Valve turns should give back the same axes and offsets.");

	// A brushDef3 plane turns its normal and keeps its distance to the brush.
	const QString q3Path = root.filePath(QStringLiteral("rotate-q3.map"));
	ok &= expect(writeFile(q3Path, quake3MapFixture()), "Quake III fixture should be written.");
	LevelMapDocument q3;
	ok &= expect(loadLevelMap({q3Path, {}, QStringLiteral("idtech3")}, &q3, &error), "Quake III fixture should load.");
	ok &= expect(setLevelMapSelection(&q3, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error) && rotateLevelMapSelection(&q3, 2, 1, &error),
		"Turning the brushDef3 brush should work.");
	const QString q3Saved = root.filePath(QStringLiteral("rotate-q3-saved.map"));
	ok &= expect(saveLevelMapAs(q3, q3Saved).succeeded()
			&& QString::fromUtf8(readFile(q3Saved)).contains(QStringLiteral("( 0 1 0 -64 ) ( (")),
		"The east plane should face north after a quarter turn about the brush's centre.");

	// Doom things turn their angle, about z only.
	const QString wadPath = root.filePath(QStringLiteral("rotate.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Doom fixture should be written.");
	LevelMapDocument doom;
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Doom fixture should load.");
	ok &= expect(selectLevelMapObject(&doom, QStringLiteral("thing:0"), &error) && !rotateLevelMapSelection(&doom, 0, 1, &error),
		"A Doom thing should not turn about x.");
	ok &= expect(rotateLevelMapSelection(&doom, 2, 1, &error) && doom.doomThings.first().angle == 180
			&& propertyValueForTest(doom, 0, QStringLiteral("angle")) == QStringLiteral("180"),
		"A Doom thing should turn its angle, its mirror included.");
	return ok;
}

bool runFlipSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("flip.map"));
	ok &= expect(writeFile(path, linkedMapFixture()), "Flip fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Flip fixture should load.");
	const QString original = document.originalText;

	// Mirroring x about the door's centre swaps two points of each face, so
	// the brush still closes when it is read back.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}}, &error) && flipLevelMapSelection(&document, 0, &error),
		"Flipping the door along x should work.");
	const QString flippedPath = root.filePath(QStringLiteral("flip-door.map"));
	ok &= expect(saveLevelMapAs(document, flippedPath).succeeded(), "Saving the flipped door should work.");
	ok &= expect(QString::fromUtf8(readFile(flippedPath)).contains(QStringLiteral("( 192 0 0 ) ( 192 0 1 ) ( 192 1 0 ) DOOR 0 0 0 1 1")),
		"A flipped face should mirror its points and swap the last two.");
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({flippedPath, {}, QStringLiteral("idtech2")}, &reloaded, &error) && !hasIssueCode(reloaded, QStringLiteral("brush-degenerate"))
			&& reloaded.brushes.size() == document.brushes.size() && reloaded.brushes.at(1).boundsSolved,
		"The flipped door should still be a closed brush.");
	ok &= expect(flipLevelMapSelection(&document, 0, &error), "Flipping back should work.");
	const QString backPath = root.filePath(QStringLiteral("flip-back.map"));
	ok &= expect(saveLevelMapAs(document, backPath).succeeded() && QString::fromUtf8(readFile(backPath)) == original,
		"Two flips should write the source back exactly.");

	// Headings mirror: x reflects about north, y about east.
	ok &= expect(setLevelMapEntityProperty(&document, 3, QStringLiteral("angle"), QStringLiteral("45"), &error)
			&& setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 3}}, &error)
			&& flipLevelMapSelection(&document, 0, &error) && propertyValueForTest(document, 3, QStringLiteral("angle")) == QStringLiteral("135")
			&& flipLevelMapSelection(&document, 1, &error) && propertyValueForTest(document, 3, QStringLiteral("angle")) == QStringLiteral("225"),
		"Flipping x should take 45 to 135, and flipping y then 135 to 225.");

	// A Quake III brushDef3 brush and a patch still read back whole.
	const QString q3Path = root.filePath(QStringLiteral("flip-q3.map"));
	ok &= expect(writeFile(q3Path, quake3MapFixture()), "Quake III fixture should be written.");
	LevelMapDocument q3;
	ok &= expect(loadLevelMap({q3Path, {}, QStringLiteral("idtech3")}, &q3, &error), "Quake III fixture should load.");
	ok &= expect(setLevelMapSelection(&q3, {{LevelMapSelectionKind::QuakeBrush, 1}, {LevelMapSelectionKind::QuakePatch, 0}}, &error)
			&& flipLevelMapSelection(&q3, 0, &error),
		"Flipping the brushDef3 brush and the patch should work.");
	const QString q3Saved = root.filePath(QStringLiteral("flip-q3-saved.map"));
	LevelMapDocument q3Reloaded;
	ok &= expect(saveLevelMapAs(q3, q3Saved).succeeded() && loadLevelMap({q3Saved, {}, QStringLiteral("idtech3")}, &q3Reloaded, &error)
			&& q3Reloaded.brushes.size() == 2 && q3Reloaded.brushes.at(1).boundsSolved && q3Reloaded.patches.size() == 1
			&& q3Reloaded.patches.first().controlGridNormalized,
		"The flipped brushDef3 brush should still close and the patch should keep its grid.");

	// Doom things flip their heading, and never along z.
	const QString wadPath = root.filePath(QStringLiteral("flip.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Doom fixture should be written.");
	LevelMapDocument doom;
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Doom fixture should load.");
	ok &= expect(selectLevelMapObject(&doom, QStringLiteral("thing:0"), &error) && !flipLevelMapSelection(&doom, 2, &error),
		"A Doom thing should not flip along z.");
	ok &= expect(flipLevelMapSelection(&doom, 1, &error) && doom.doomThings.first().angle == 270, "Flipping y should turn north into south.");
	return ok;
}

bool runResizeSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("resize.map"));
	ok &= expect(writeFile(path, linkedMapFixture()), "Resize fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Resize fixture should load.");
	const QString original = document.originalText;

	// The door spans 128-192 by 0-64 by 0-128; stretching it to 256 along x
	// moves its east face and keeps its west face.
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}}, &error)
			&& levelMapSelectionBounds(document, &mins, &maxs) && mins.x == 128.0 && maxs.x == 192.0 && maxs.z == 128.0,
		"The door's bounds should be those of its brush.");
	ok &= expect(!resizeLevelMapSelection(&document, {128.0, 0.0, 0.0, true}, {128.0, 64.0, 128.0, true}, &error)
			&& error.contains(QStringLiteral("along x")),
		"A resize that flattens the door should be refused, naming the axis.");
	ok &= expect(!resizeLevelMapSelection(&document, mins, maxs, &error), "A resize to the same bounds should change nothing.");
	ok &= expect(resizeLevelMapSelection(&document, {128.0, 0.0, 0.0, true}, {256.0, 64.0, 128.0, true}, &error),
		"Stretching the door along x should work.");
	ok &= expect(document.undoStack.last().description == QStringLiteral("Resize entity:2 to 128 x 64 x 128"),
		"A one-object resize should name the object and its new size in the history.");
	const QString stretchedPath = root.filePath(QStringLiteral("resize-door.map"));
	ok &= expect(saveLevelMapAs(document, stretchedPath).succeeded(), "Saving the stretched door should work.");
	const QString stretched = QString::fromUtf8(readFile(stretchedPath));
	ok &= expect(stretched.contains(QStringLiteral("( 256 0 0 ) ( 256 0 1 ) ( 256 1 0 ) DOOR 0 0 0 1 1"))
			&& stretched.contains(QStringLiteral("( 128 0 0 ) ( 128 1 0 ) ( 128 0 1 ) DOOR 0 0 0 1 1")),
		"The east face should move out to 256 and the west face stay at 128.");
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({stretchedPath, {}, QStringLiteral("idtech2")}, &reloaded, &error) && reloaded.brushes.at(1).boundsSolved
			&& reloaded.brushes.at(1).maxs.x == 256.0 && !hasIssueCode(reloaded, QStringLiteral("brush-degenerate")),
		"The stretched door should read back as a closed brush reaching 256.");
	ok &= expect(undoLevelMapEdit(&document, &error), "The resize should undo.");
	const QString undonePath = root.filePath(QStringLiteral("resize-undone.map"));
	ok &= expect(saveLevelMapAs(document, undonePath).succeeded() && QString::fromUtf8(readFile(undonePath)) == original,
		"Undoing the resize should write the source back exactly.");

	// A key a transform leaves as it was keeps its text: resizing does not
	// turn anything, so an unusual `angles` spacing survives.
	ok &= expect(setLevelMapEntityProperty(&document, 3, QStringLiteral("angles"), QStringLiteral("0  90 0"), &error)
			&& setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}, {LevelMapSelectionKind::Entity, 3}}, &error)
			&& resizeLevelMapSelection(&document, {128.0, 0.0, 0.0, true}, {384.0, 256.0, 128.0, true}, &error)
			&& propertyValueForTest(document, 3, QStringLiteral("angles")) == QStringLiteral("0  90 0"),
		"Resizing should leave an angles key it does not turn exactly as written.");
	ok &= expect(undoLevelMapEdit(&document, &error) && undoLevelMapEdit(&document, &error), "The resize and the key should undo.");

	// A point entity in the selection keeps its place relative to the rest.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}, {LevelMapSelectionKind::Entity, 3}}, &error)
			&& resizeLevelMapSelection(&document, {128.0, 0.0, 0.0, true}, {384.0, 256.0, 128.0, true}, &error)
			&& propertyValueForTest(document, 3, QStringLiteral("origin")) == QStringLiteral("384 256 64"),
		"The light at the far corner should move with the far corner.");

	// Valve 220 textures stay put in the world: the axes and offsets keep
	// their text, and only the points move.
	const QString valvePath = root.filePath(QStringLiteral("resize-valve.map"));
	ok &= expect(writeFile(valvePath, valve220MapFixture()), "Valve fixture should be written.");
	LevelMapDocument valve;
	ok &= expect(loadLevelMap({valvePath, {}, QStringLiteral("idtech2")}, &valve, &error), "Valve fixture should load.");
	ok &= expect(setLevelMapSelection(&valve, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
			&& resizeLevelMapSelection(&valve, {0.0, 0.0, 0.0, true}, {128.0, 64.0, 64.0, true}, &error),
		"Stretching the Valve brush should work.");
	const QString valveSaved = root.filePath(QStringLiteral("resize-valve-saved.map"));
	const QString valveText = saveLevelMapAs(valve, valveSaved).succeeded() ? QString::fromUtf8(readFile(valveSaved)) : QString();
	ok &= expect(valveText.contains(QStringLiteral("( 128 1 0 ) ( 128 0 0 ) ( 128 0 1 ) EASTTEX [ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1"))
			&& valveText.contains(QStringLiteral("( 2 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOPTEX [ 1 0 0 16 ] [ 0 -1 0 -8 ] 0 1 1")),
		"A stretched Valve face should move its points and keep its texture axes and offsets.");

	// A brushDef3 plane keeps its normal and moves its distance; a patch
	// stretches its control points.
	const QString q3Path = root.filePath(QStringLiteral("resize-q3.map"));
	ok &= expect(writeFile(q3Path, quake3MapFixture()), "Quake III fixture should be written.");
	LevelMapDocument q3;
	ok &= expect(loadLevelMap({q3Path, {}, QStringLiteral("idtech3")}, &q3, &error), "Quake III fixture should load.");
	ok &= expect(setLevelMapSelection(&q3, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error)
			&& resizeLevelMapSelection(&q3, {0.0, 0.0, 0.0, true}, {128.0, 64.0, 64.0, true}, &error),
		"Stretching the brushDef3 brush should work.");
	ok &= expect(setLevelMapSelection(&q3, {{LevelMapSelectionKind::QuakePatch, 0}}, &error)
			&& resizeLevelMapSelection(&q3, {0.0, 0.0, -8.0, true}, {64.0, 32.0, 8.0, true}, &error),
		"Stretching the patch should work.");
	const QString q3Saved = root.filePath(QStringLiteral("resize-q3-saved.map"));
	LevelMapDocument q3Reloaded;
	ok &= expect(saveLevelMapAs(q3, q3Saved).succeeded() && QString::fromUtf8(readFile(q3Saved)).contains(QStringLiteral("( 1 0 0 -128 ) ( ( 0.03125 0 0 )"))
			&& loadLevelMap({q3Saved, {}, QStringLiteral("idtech3")}, &q3Reloaded, &error) && q3Reloaded.brushes.at(1).boundsSolved
			&& q3Reloaded.brushes.at(1).maxs.x == 128.0 && q3Reloaded.patches.size() == 1 && q3Reloaded.patches.first().controlGridNormalized
			&& q3Reloaded.patches.first().maxs.x == 64.0,
		"The east plane should move to 128 and the patch should reach 64.");

	// A lone Doom thing has no size, so a resize only moves it.
	const QString wadPath = root.filePath(QStringLiteral("resize.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Doom fixture should be written.");
	LevelMapDocument doom;
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Doom fixture should load.");
	LevelMapVec3 thingMins;
	LevelMapVec3 thingMaxs;
	ok &= expect(selectLevelMapObject(&doom, QStringLiteral("thing:0"), &error) && levelMapSelectionBounds(doom, &thingMins, &thingMaxs),
		"A selected thing should have bounds.");
	const LevelMapVec3 shifted {thingMins.x + 32.0, thingMins.y, 0.0, true};
	ok &= expect(resizeLevelMapSelection(&doom, shifted, shifted, &error) && doom.doomThings.first().x == thingMins.x + 32.0,
		"A resize of a lone thing should move it.");
	return ok;
}

// Three points on the plane `axis` = `at` whose normal points up that axis.
void axisPlanePoints(int axis, double at, LevelMapVec3* a, LevelMapVec3* b, LevelMapVec3* c)
{
	switch (axis) {
	case 0:
		*a = {at, 0.0, 0.0, true};
		*b = {at, 1.0, 0.0, true};
		*c = {at, 0.0, 1.0, true};
		break;
	case 1:
		*a = {0.0, at, 0.0, true};
		*b = {0.0, at, 1.0, true};
		*c = {1.0, at, 0.0, true};
		break;
	default:
		*a = {0.0, 0.0, at, true};
		*b = {1.0, 0.0, at, true};
		*c = {0.0, 1.0, at, true};
		break;
	}
}

bool runClipSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int clipped = 0;
	LevelMapVec3 a;
	LevelMapVec3 b;
	LevelMapVec3 c;
	const QString path = root.filePath(QStringLiteral("clip.map"));
	ok &= expect(writeFile(path, linkedMapFixture()), "Clip fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Clip fixture should load.");
	const QString original = document.originalText;

	// The door, x 128-192, cut at x 160 keeping the back: the new face sits on
	// the plane facing +x, and the east face, left without an edge, goes.
	axisPlanePoints(0, 160.0, &a, &b, &c);
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}}, &error)
			&& clipLevelMapSelection(&document, a, b, c, LevelMapClipKeep::Back, &clipped, &error) && clipped == 1,
		"Clipping the door's brush at x 160 should work.");
	ok &= expect(document.brushes.size() == 2 && document.brushes.last().id == 2 && document.brushes.last().entityId == 2
			&& document.brushes.last().maxs.x == 160.0 && document.selection.size() == 1 && document.selection.first().objectId == 2,
		"The door should keep one brush, 128 to 160 wide, selected.");
	ok &= expect(document.undoStack.last().description == QStringLiteral("Clip brush:1"), "The history should name the clipped brush.");
	const QString clippedPath = root.filePath(QStringLiteral("clip-door.map"));
	ok &= expect(saveLevelMapAs(document, clippedPath).succeeded(), "Saving the clipped door should work.");
	const QString clippedText = QString::fromUtf8(readFile(clippedPath));
	ok &= expect(clippedText.contains(QStringLiteral("( 160 1 0 ) ( 160 0 0 ) ( 160 0 1 ) DOOR 0 0 0 1 1"))
			&& !clippedText.contains(QStringLiteral("( 192 0 0 ) ( 192 0 1 ) ( 192 1 0 ) DOOR")),
		"The clipped door should gain a face on the plane and lose its east face.");
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({clippedPath, {}, QStringLiteral("idtech2")}, &reloaded, &error) && reloaded.brushes.size() == 2
			&& reloaded.brushes.at(1).boundsSolved && reloaded.brushes.at(1).maxs.x == 160.0 && !hasIssueCode(reloaded, QStringLiteral("brush-degenerate")),
		"The clipped door should read back as a closed brush inside the door entity.");
	ok &= expect(undoLevelMapEdit(&document, &error) && document.brushes.size() == 2 && document.brushes.at(1).id == 1,
		"Undo should put the door's brush back whole.");
	const QString undonePath = root.filePath(QStringLiteral("clip-undone.map"));
	ok &= expect(saveLevelMapAs(document, undonePath).succeeded() && QString::fromUtf8(readFile(undonePath)) == original,
		"Undoing the clip should write the source back exactly.");
	ok &= expect(redoLevelMapEdit(&document, &error) && document.brushes.last().id == 2 && document.brushes.last().maxs.x == 160.0,
		"Redo should cut the door again.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Undoing again should work.");

	// Both parts: two brushes meeting at the plane, both selected.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error)
			&& clipLevelMapSelection(&document, a, b, c, LevelMapClipKeep::Both, &clipped, &error) && document.brushes.size() == 3
			&& document.selection.size() == 2 && document.brushes.at(1).maxs.x == 160.0 && document.brushes.at(2).mins.x == 160.0
			&& document.undoStack.last().description == QStringLiteral("Split brush:1 in two"),
		"Splitting the door should make two brushes meeting at x 160.");
	ok &= expect(undoLevelMapEdit(&document, &error), "The split should undo.");

	// A plane that misses every selected brush changes nothing.
	axisPlanePoints(0, 500.0, &a, &b, &c);
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error)
			&& !clipLevelMapSelection(&document, a, b, c, LevelMapClipKeep::Back, &clipped, &error) && error.contains(QStringLiteral("does not pass")),
		"A plane beside the brush should be refused.");

	// Valve 220: the new face gets paraxial axes; the texture comes from the
	// first of the most used faces.
	const QString valvePath = root.filePath(QStringLiteral("clip-valve.map"));
	ok &= expect(writeFile(valvePath, valve220MapFixture()), "Valve fixture should be written.");
	LevelMapDocument valve;
	ok &= expect(loadLevelMap({valvePath, {}, QStringLiteral("idtech2")}, &valve, &error), "Valve fixture should load.");
	axisPlanePoints(2, 32.0, &a, &b, &c);
	ok &= expect(setLevelMapSelection(&valve, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
			&& clipLevelMapSelection(&valve, a, b, c, LevelMapClipKeep::Back, &clipped, &error),
		"Clipping the Valve brush at z 32 should work.");
	const QString valveSaved = root.filePath(QStringLiteral("clip-valve-saved.map"));
	const QString valveText = saveLevelMapAs(valve, valveSaved).succeeded() ? QString::fromUtf8(readFile(valveSaved)) : QString();
	ok &= expect(valveText.contains(QStringLiteral("( 1 0 32 ) ( 0 0 32 ) ( 0 1 32 ) TOPTEX [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1"))
			&& !valveText.contains(QStringLiteral("( 1 0 64 ) ( 0 0 64 )")),
		"The Valve brush should gain a Valve face at z 32 and lose its top.");

	// brushDef3: the new face is a plane, keeping the front of the cut.
	const QString q3Path = root.filePath(QStringLiteral("clip-q3.map"));
	ok &= expect(writeFile(q3Path, quake3MapFixture()), "Quake III fixture should be written.");
	LevelMapDocument q3;
	ok &= expect(loadLevelMap({q3Path, {}, QStringLiteral("idtech3")}, &q3, &error), "Quake III fixture should load.");
	axisPlanePoints(0, 32.0, &a, &b, &c);
	ok &= expect(setLevelMapSelection(&q3, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error)
			&& clipLevelMapSelection(&q3, a, b, c, LevelMapClipKeep::Front, &clipped, &error),
		"Clipping the brushDef3 brush at x 32 should work.");
	const QString q3Saved = root.filePath(QStringLiteral("clip-q3-saved.map"));
	LevelMapDocument q3Reloaded;
	ok &= expect(saveLevelMapAs(q3, q3Saved).succeeded()
			&& QString::fromUtf8(readFile(q3Saved)).contains(QStringLiteral("( -1 0 0 32 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) \"textures/common/caulk\" 0 0 0"))
			&& loadLevelMap({q3Saved, {}, QStringLiteral("idtech3")}, &q3Reloaded, &error) && q3Reloaded.brushes.size() == 2
			&& q3Reloaded.brushes.last().boundsSolved && q3Reloaded.brushes.last().mins.x == 32.0,
		"The brushDef3 brush should keep 32 to 64, closed by a plane facing -x.");

	// With a clean brush and one on shared lines selected together, the clean
	// one is cut and the other is left whole, with its reason reported.
	const QString mixedPath = root.filePath(QStringLiteral("clip-mixed.map"));
	ok &= expect(writeFile(mixedPath, QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n{\n"
				"( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOP 0 0 0 1 1\n( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOTTOM 0 0 0 1 1\n"
				"( 64 1 0 ) ( 64 0 0 ) ( 64 0 1 ) EAST 0 0 0 1 1\n( 0 0 1 ) ( 0 0 0 ) ( 0 1 0 ) WEST 0 0 0 1 1\n"
				"( 0 64 1 ) ( 0 64 0 ) ( 1 64 0 ) NORTH 0 0 0 1 1\n( 1 0 0 ) ( 0 0 0 ) ( 0 0 1 ) SOUTH 0 0 0 1 1\n}\n"
				"{ ( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOP 0 0 0 1 1\n( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOTTOM 0 0 0 1 1\n"
				"( 64 1 0 ) ( 64 0 0 ) ( 64 0 1 ) EAST 0 0 0 1 1\n( 0 0 1 ) ( 0 0 0 ) ( 0 1 0 ) WEST 0 0 0 1 1\n"
				"( 0 64 1 ) ( 0 64 0 ) ( 1 64 0 ) NORTH 0 0 0 1 1\n( 1 0 0 ) ( 0 0 0 ) ( 0 0 1 ) SOUTH 0 0 0 1 1 }\n}\n")),
		"Mixed clip fixture should be written.");
	LevelMapDocument mixed;
	QStringList skipped;
	axisPlanePoints(0, 32.0, &a, &b, &c);
	ok &= expect(loadLevelMap({mixedPath, {}, QStringLiteral("idtech2")}, &mixed, &error)
			&& setLevelMapSelection(&mixed, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}}, &error)
			&& clipLevelMapSelection(&mixed, a, b, c, LevelMapClipKeep::Back, &clipped, &error, &skipped) && clipped == 1
			&& skipped.size() == 1 && skipped.first().contains(QStringLiteral("Brush 1")),
		"Clipping a clean brush beside one on shared lines should cut the first and report the second.");

	// A brush written on shared lines is left alone, with a reason.
	const QString compactPath = root.filePath(QStringLiteral("clip-compact.map"));
	ok &= expect(writeFile(compactPath, compactMapFixture()), "Compact fixture should be written.");
	LevelMapDocument compact;
	ok &= expect(loadLevelMap({compactPath, {}, QStringLiteral("idtech2")}, &compact, &error), "Compact fixture should load.");
	axisPlanePoints(0, 32.0, &a, &b, &c);
	ok &= expect(setLevelMapSelection(&compact, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
			&& !clipLevelMapSelection(&compact, a, b, c, LevelMapClipKeep::Back, &clipped, &error) && error.contains(QStringLiteral("shared")),
		"A brush on shared lines should be refused rather than rewritten.");
	return ok;
}

bool runHollowSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int hollowed = 0;
	const QString path = root.filePath(QStringLiteral("hollow.map"));
	ok &= expect(writeFile(path, valve220MapFixture()), "Hollow fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Hollow fixture should load.");
	const QString original = document.originalText;

	// Walls thicker than half the cube cannot fit.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
			&& !hollowLevelMapSelection(&document, 40.0, &hollowed, &error) && error.contains(QStringLiteral("too thin")),
		"Walls 40 units thick should not fit in a 64-unit cube.");

	// The 64-unit cube becomes six walls 8 units thick, one per face.
	ok &= expect(hollowLevelMapSelection(&document, 8.0, &hollowed, &error) && hollowed == 1 && document.brushes.size() == 6
			&& document.selection.size() == 6,
		"Hollowing the cube should make six selected walls.");
	ok &= expect(document.undoStack.last().description == QStringLiteral("Hollow brush:0 into 6 walls"), "The history should name the hollowed brush.");
	const QString savedPath = root.filePath(QStringLiteral("hollow-saved.map"));
	LevelMapDocument reloaded;
	ok &= expect(saveLevelMapAs(document, savedPath).succeeded() && loadLevelMap({savedPath, {}, QStringLiteral("idtech2")}, &reloaded, &error)
			&& reloaded.brushes.size() == 6 && !hasIssueCode(reloaded, QStringLiteral("brush-degenerate")),
		"The walls should read back as six closed brushes.");
	bool top = false;
	bool west = false;
	for (const LevelMapBrush& wall : reloaded.brushes) {
		top = top || (wall.mins.z == 56.0 && wall.maxs.z == 64.0 && wall.mins.x == 0.0 && wall.maxs.x == 64.0);
		west = west || (wall.mins.x == 0.0 && wall.maxs.x == 8.0 && wall.maxs.z == 64.0);
	}
	ok &= expect(top && west, "The top wall should span z 56-64 and the west wall x 0-8.");
	const QString savedText = QString::fromUtf8(readFile(savedPath));
	ok &= expect(savedText.contains(QStringLiteral(" 56 ) TOPTEX [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1"))
			&& savedText.contains(QStringLiteral("( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOPTEX [ 1 0 0 16 ] [ 0 -1 0 -8 ] 0 1 1")),
		"The top wall should keep its outer face as written and face its inside with the same texture.");
	ok &= expect(undoLevelMapEdit(&document, &error) && document.brushes.size() == 1, "Undo should fill the cube back in.");
	const QString undonePath = root.filePath(QStringLiteral("hollow-undone.map"));
	ok &= expect(saveLevelMapAs(document, undonePath).succeeded() && QString::fromUtf8(readFile(undonePath)) == original,
		"Undoing the hollow should write the source back exactly.");

	// A brush entity hollows its brush and keeps the walls.
	const QString doorPath = root.filePath(QStringLiteral("hollow-door.map"));
	ok &= expect(writeFile(doorPath, linkedMapFixture()), "Door fixture should be written.");
	LevelMapDocument doors;
	ok &= expect(loadLevelMap({doorPath, {}, QStringLiteral("idtech2")}, &doors, &error), "Door fixture should load.");
	ok &= expect(setLevelMapSelection(&doors, {{LevelMapSelectionKind::Entity, 2}}, &error)
			&& hollowLevelMapSelection(&doors, 16.0, &hollowed, &error) && doors.brushes.size() == 7
			&& std::all_of(doors.brushes.cbegin() + 1, doors.brushes.cend(), [](const LevelMapBrush& wall) { return wall.entityId == 2; }),
		"The door's brush should become six walls that stay in the door.");
	return ok;
}

bool runCarveSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int carved = 0;

	// A 256-unit wall, a doorway brush through it from the floor, and a small
	// brush wholly inside the doorway, written by Add Brush and read back so
	// all three come from the file.
	const QString emptyPath = root.filePath(QStringLiteral("carve-empty.map"));
	ok &= expect(writeFile(emptyPath, QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n}\n")), "Carve fixture should be written.");
	LevelMapDocument building;
	ok &= expect(loadLevelMap({emptyPath, {}, QStringLiteral("idtech2")}, &building, &error)
			&& addLevelMapBoxBrush(&building, {0.0, 0.0, 0.0, true}, {256.0, 16.0, 128.0, true}, QStringLiteral("WALL"), nullptr, &error)
			&& addLevelMapBoxBrush(&building, {96.0, -8.0, 0.0, true}, {160.0, 24.0, 96.0, true}, QStringLiteral("DOORCUT"), nullptr, &error)
			&& addLevelMapBoxBrush(&building, {100.0, 4.0, 8.0, true}, {120.0, 12.0, 24.0, true}, QStringLiteral("INNER"), nullptr, &error)
			&& addLevelMapBoxBrush(&building, {1000.0, 0.0, 0.0, true}, {1016.0, 16.0, 16.0, true}, QStringLiteral("FAR"), nullptr, &error),
		"The wall, doorway, inner, and far brushes should be added.");
	const QString path = root.filePath(QStringLiteral("carve.map"));
	ok &= expect(saveLevelMapAs(building, path).succeeded(), "The carve fixture should be saved.");
	const QString original = QString::fromUtf8(readFile(path));
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error) && document.brushes.size() == 4,
		"The carve fixture should read back with four brushes.");

	// A carver that touches nothing is refused.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 3}}, &error)
			&& !carveLevelMapSelection(&document, &carved, &error) && error.contains(QStringLiteral("overlap")),
		"A carver away from every brush should be refused.");

	// Carving with the doorway leaves the wall's two sides and lintel, drops
	// the brush inside it, and keeps the doorway selected.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error)
			&& carveLevelMapSelection(&document, &carved, &error) && carved == 2,
		"Carving with the doorway should cut the wall and the brush inside it.");
	ok &= expect(document.selection.size() == 1 && document.selection.first().objectId == 1 && document.brushes.size() == 5,
		"The doorway should stay, selected, beside three pieces of wall.");
	double volume = 0.0;
	bool clear = true;
	for (const LevelMapBrush& brush : document.brushes) {
		if (brush.id == 1 || brush.id == 3) {
			continue;
		}
		volume += (brush.maxs.x - brush.mins.x) * (brush.maxs.y - brush.mins.y) * (brush.maxs.z - brush.mins.z);
		const double overlapX = std::min(brush.maxs.x, 160.0) - std::max(brush.mins.x, 96.0);
		const double overlapY = std::min(brush.maxs.y, 16.0) - std::max(brush.mins.y, 0.0);
		const double overlapZ = std::min(brush.maxs.z, 96.0) - std::max(brush.mins.z, 0.0);
		clear = clear && !(overlapX > 0.01 && overlapY > 0.01 && overlapZ > 0.01);
	}
	ok &= expect(volume == 256.0 * 16.0 * 128.0 - 64.0 * 16.0 * 96.0 && clear,
		"The pieces should be the whole wall less the doorway, with nothing left in the opening.");
	const QString carvedPath = root.filePath(QStringLiteral("carve-saved.map"));
	LevelMapDocument reloaded;
	ok &= expect(saveLevelMapAs(document, carvedPath).succeeded() && loadLevelMap({carvedPath, {}, QStringLiteral("idtech2")}, &reloaded, &error)
			&& reloaded.brushes.size() == 5 && !hasIssueCode(reloaded, QStringLiteral("brush-degenerate")),
		"The carved wall should read back as closed brushes.");
	// Five new faces: three facing into the opening, and two where the sides
	// meet the lintel, hidden between pieces as Radiant's subtract leaves them.
	ok &= expect(QString::fromUtf8(readFile(carvedPath)).count(QStringLiteral("DOORCUT")) == 6 + 5,
		"The faces the carve made should take the doorway's texture, beside its own six.");
	ok &= expect(undoLevelMapEdit(&document, &error), "The carve should undo.");
	const QString undonePath = root.filePath(QStringLiteral("carve-undone.map"));
	ok &= expect(saveLevelMapAs(document, undonePath).succeeded() && QString::fromUtf8(readFile(undonePath)) == original,
		"Undoing the carve should write the file back exactly.");
	return ok;
}

// A classic-format box brush, each face naming three points ordered so the
// face looks out, the way the parser's planeFromPoints reads them.
QString testBoxBrush(double x0, double y0, double z0, double x1, double y1, double z1, const QString& texture)
{
	const auto n = [](double value) {
		return QString::number(value);
	};
	QStringList lines;
	lines << QStringLiteral("{");
	lines << QStringLiteral("( 1 0 %1 ) ( 0 0 %1 ) ( 0 1 %1 ) %2 0 0 0 1 1").arg(n(z1), texture);
	lines << QStringLiteral("( 0 1 %1 ) ( 0 0 %1 ) ( 1 0 %1 ) %2 0 0 0 1 1").arg(n(z0), texture);
	lines << QStringLiteral("( %1 1 0 ) ( %1 0 0 ) ( %1 0 1 ) %2 0 0 0 1 1").arg(n(x1), texture);
	lines << QStringLiteral("( %1 0 1 ) ( %1 0 0 ) ( %1 1 0 ) %2 0 0 0 1 1").arg(n(x0), texture);
	lines << QStringLiteral("( 0 %1 1 ) ( 0 %1 0 ) ( 1 %1 0 ) %2 0 0 0 1 1").arg(n(y1), texture);
	lines << QStringLiteral("( 1 %1 0 ) ( 0 %1 0 ) ( 0 %1 1 ) %2 0 0 0 1 1").arg(n(y0), texture);
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// A four-cornered brush, each face ordered to look away from the corner it
// leaves out.
QString testTetraBrush(const QVector<LevelMapVec3>& corners, const QString& texture)
{
	const auto minus = [](const LevelMapVec3& left, const LevelMapVec3& right) {
		return LevelMapVec3 {left.x - right.x, left.y - right.y, left.z - right.z, true};
	};
	QStringList lines {QStringLiteral("{")};
	for (int skip = 0; skip < 4; ++skip) {
		QVector<LevelMapVec3> face;
		for (int index = 0; index < 4; ++index) {
			if (index != skip) {
				face.push_back(corners.at(index));
			}
		}
		const LevelMapVec3 u = minus(face.at(0), face.at(1));
		const LevelMapVec3 v = minus(face.at(2), face.at(1));
		const LevelMapVec3 normal {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x, true};
		const LevelMapVec3 away = minus(corners.at(skip), face.at(1));
		if (normal.x * away.x + normal.y * away.y + normal.z * away.z > 0.0) {
			std::swap(face[0], face[2]);
		}
		QStringList points;
		for (const LevelMapVec3& point : face) {
			points << QStringLiteral("( %1 %2 %3 )").arg(point.x).arg(point.y).arg(point.z);
		}
		lines << points.join(QLatin1Char(' ')) + QStringLiteral(" %1 0 0 0 1 1").arg(texture);
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// Doom records for a WAD a test writes: a linedef names its vertices and
// sides by index, and a side its sector.
struct TestDoomLine {
	int start = 0;
	int end = 0;
	int flags = 0;
	int front = -1;
	int back = -1;
	int special = 0;
};

struct TestDoomSide {
	int sector = 0;
	int offsetX = 0;
	QByteArray middle = "-";
};

QByteArray doomWadOf(const QVector<QPoint>& vertices, const QVector<TestDoomLine>& lines, const QVector<TestDoomSide>& sides, int sectors)
{
	QByteArray vertexBytes;
	for (const QPoint& point : vertices) {
		appendLe16(&vertexBytes, static_cast<qint16>(point.x()));
		appendLe16(&vertexBytes, static_cast<qint16>(point.y()));
	}
	QByteArray lineBytes;
	for (const TestDoomLine& line : lines) {
		for (const int value : {line.start, line.end, line.flags, line.special, 0, line.front, line.back}) {
			appendLe16(&lineBytes, static_cast<qint16>(value));
		}
	}
	QByteArray sideBytes;
	for (const TestDoomSide& side : sides) {
		appendLe16(&sideBytes, static_cast<qint16>(side.offsetX));
		appendLe16(&sideBytes, 0);
		sideBytes.append(fixedName("WALLUP", 8));
		sideBytes.append(fixedName("WALLLOW", 8));
		sideBytes.append(fixedName(side.middle, 8));
		appendLe16(&sideBytes, static_cast<qint16>(side.sector));
	}
	QByteArray sectorBytes;
	for (int sector = 0; sector < sectors; ++sector) {
		appendLe16(&sectorBytes, 0);
		appendLe16(&sectorBytes, 128);
		sectorBytes.append(fixedName("FLOOR1", 8));
		sectorBytes.append(fixedName("CEIL1", 8));
		appendLe16(&sectorBytes, 160);
		appendLe16(&sectorBytes, 0);
		appendLe16(&sectorBytes, 0);
	}
	return buildWad("PWAD", {{"MAP01", {}}, {"THINGS", doomThings()}, {"LINEDEFS", lineBytes}, {"SIDEDEFS", sideBytes},
		{"VERTEXES", vertexBytes}, {"SECTORS", sectorBytes}});
}

// Two 128-unit rooms side by side, joined by a two-sided linedef from
// (128, 128) down to (128, 0): room 0 on its front (offset 7), room 1 on its
// back (offset 9). `packed` shares that back side with room 1's bottom wall,
// the way sidedef packing leaves maps.
QByteArray twoRoomWad(bool packed)
{
	const QVector<QPoint> vertices {{0, 0}, {128, 0}, {128, 128}, {0, 128}, {256, 0}, {256, 128}};
	const QVector<TestDoomLine> lines {
		{0, 3, 1, 0, -1},
		{3, 2, 1, 1, -1},
		{2, 1, 4, 3, 4},
		{1, 0, 1, 2, -1},
		{2, 5, 1, 5, -1},
		{5, 4, 1, 6, -1},
		{4, 1, 1, packed ? 4 : 7, -1},
	};
	const QVector<TestDoomSide> sides {
		{0, 0, "WALL0"},
		{0, 0, "WALL0"},
		{0, 0, "WALL0"},
		{0, 7, "-"},
		{1, 9, "-"},
		{1, 0, "WALL1"},
		{1, 0, "WALL1"},
		{1, 0, "WALL1"},
	};
	return doomWadOf(vertices, lines, sides, 2);
}

// Drawing, deleting, and merging Doom geometry. A room drawn beside another
// shares its wall, one drawn inside a room sits within it, a corner on a wall
// splits it, a room drawn around others surrounds them, and every edit undoes
// to the map byte for byte.
bool runDoomTopologySmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int count = 0;
	int sector = -1;
	const QString path = root.filePath(QStringLiteral("topology.wad"));
	LevelMapDocument doom;
	ok &= expect(writeFile(path, twoRoomWad(false)) && loadLevelMap({path, QStringLiteral("MAP01"), {}}, &doom, &error),
		"The topology fixture should load.");
	const auto corners = [](const QVector<QPoint>& points) {
		QVector<LevelMapVec3> list;
		for (const QPoint& point : points) {
			list.push_back({static_cast<double>(point.x()), static_cast<double>(point.y()), 0.0, true});
		}
		return list;
	};
	const auto closed = [](const LevelMapDocument& document) {
		const QVector<DoomSectorOutline> outlines = buildDoomSectorOutlines(document);
		return !outlines.isEmpty() && std::all_of(outlines.cbegin(), outlines.cend(), [](const DoomSectorOutline& outline) {
			return outline.openEdgeCount == 0;
		});
	};
	const auto bytes = [&root](const LevelMapDocument& document, const QString& name) {
		const QString written = root.filePath(name);
		return saveMapOverwriting(document, written) ? readFile(written) : QByteArray();
	};
	const QByteArray original = bytes(doom, QStringLiteral("topology-original.wad"));
	// Each check writes a file of its own: replacing the one just written can
	// fail on Windows while something, such as a virus scan, still has it open.
	int undoneChecks = 0;
	const auto undoesToOriginal = [&](const char* message) {
		while (!doom.undoStack.isEmpty()) {
			if (!undoLevelMapEdit(&doom, &error)) {
				break;
			}
		}
		const QString name = QStringLiteral("topology-undone-%1.wad").arg(++undoneChecks);
		ok &= expect(!original.isEmpty() && bytes(doom, name) == original && !doom.doomGeometryChanged, message);
	};

	// Beside room 1, in the void: the new room shares room 1's right wall,
	// which opens onto it, and its other three edges are new walls.
	ok &= expect(drawLevelMapDoomSector(&doom, corners({{256, 0}, {256, 128}, {384, 128}, {384, 0}}), &sector, &error) && sector == 2,
		"Drawing a room beside room 1 should work.");
	ok &= expect(doom.doomSectors.size() == 3 && doom.doomVertices.size() == 8 && doom.doomLinedefs.size() == 10 && doom.doomSidedefs.size() == 12,
		"The new room should add a sector, two vertices, three walls, and four sides.");
	const LevelMapDoomLinedef opened = doom.doomLinedefs.at(5);
	ok &= expect(opened.backSidedef >= 0 && (opened.flags & 0x0004) != 0 && (opened.flags & 0x0001) == 0
			&& doom.doomSidedefs.at(opened.backSidedef).sector == 2 && doom.doomSidedefs.at(opened.backSidedef).upperTexture == QStringLiteral("WALL1")
			&& doom.doomSidedefs.at(opened.frontSidedef).middleTexture == QStringLiteral("-"),
		"The shared wall should open onto the new room, its texture taken above and below.");
	ok &= expect(doom.doomSectors.at(2).floorTexture == QStringLiteral("FLOOR1") && doom.doomSectors.at(2).ceilingHeight == 128,
		"The new room should copy the room beside it.");
	ok &= expect(closed(doom), "Every sector should close after the drawing.");
	ok &= expect(doom.selection.size() == 1 && doom.selectionKind == LevelMapSelectionKind::DoomSector && doom.selectedObjectId == 2
			&& doom.doomGeometryChanged && doom.undoStack.last().description == QStringLiteral("Draw sector:2 with 3 new linedefs"),
		"The new sector should be selected, the node lumps marked stale, and the history name it.");
	LevelMapDocument reloaded;
	ok &= expect(!bytes(doom, QStringLiteral("topology-drawn.wad")).isEmpty()
			&& loadLevelMap({root.filePath(QStringLiteral("topology-drawn.wad")), QStringLiteral("MAP01"), {}}, &reloaded, &error)
			&& reloaded.doomSectors.size() == 3 && reloaded.doomLinedefs.size() == 10 && closed(reloaded),
		"The drawn room should be written and read back closed.");

	// Deleting the new sector takes its walls, vertices, and sides, and turns
	// the shared line back into a wall.
	ok &= expect(setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, 2}}, &error) && deleteLevelMapSelection(&doom, &error)
			&& doom.doomSectors.size() == 2 && doom.doomLinedefs.size() == 7 && doom.doomVertices.size() == 6 && doom.doomSidedefs.size() == 8,
		"Deleting the new sector should take what only it used.");
	ok &= expect(doom.doomLinedefs.at(5).backSidedef < 0 && (doom.doomLinedefs.at(5).flags & 0x0001) != 0
			&& doom.doomSidedefs.at(doom.doomLinedefs.at(5).frontSidedef).middleTexture != QStringLiteral("-") && closed(doom),
		"The shared line should be a wall again, with a texture.");
	ok &= expect(undoLevelMapEdit(&doom, &error) && doom.doomSectors.size() == 3 && doom.selectedObjectId == 2 && closed(doom),
		"Undoing the delete should bring the room back, selected.");
	undoesToOriginal("Undoing the drawing should give back the map byte for byte.");

	// Inside room 0, drawn anticlockwise: a room within it, joined by
	// two-sided lines that face it in front and room 0 behind.
	ok &= expect(drawLevelMapDoomSector(&doom, corners({{32, 32}, {96, 32}, {96, 96}, {32, 96}}), &sector, &error) && sector == 2,
		"Drawing inside room 0 should work.");
	bool inner = doom.doomLinedefs.size() == 11;
	for (int index = 7; inner && index < doom.doomLinedefs.size(); ++index) {
		const LevelMapDoomLinedef& linedef = doom.doomLinedefs.at(index);
		inner = linedef.backSidedef >= 0 && doom.doomSidedefs.at(linedef.frontSidedef).sector == 2 && doom.doomSidedefs.at(linedef.backSidedef).sector == 0
			&& (linedef.flags & 0x0004) != 0;
	}
	ok &= expect(inner && closed(doom), "The inner room's four lines should face it in front and room 0 behind, both rooms closed.");
	undoesToOriginal("Undoing the inner room should give back the map.");

	// Against room 0's left wall: two corners on the wall split it, and the
	// piece between them turns to face the new room.
	ok &= expect(drawLevelMapDoomSector(&doom, corners({{0, 32}, {0, 96}, {64, 96}, {64, 32}}), &sector, &error) && sector == 2,
		"Drawing against room 0's wall should work.");
	int piece = -1;
	for (int index = 0; index < doom.doomLinedefs.size(); ++index) {
		const LevelMapDoomLinedef& linedef = doom.doomLinedefs.at(index);
		if (doom.doomVertices.at(linedef.startVertex).y == 32.0 && doom.doomVertices.at(linedef.endVertex).y == 96.0
			&& doom.doomVertices.at(linedef.startVertex).x == 0.0 && doom.doomVertices.at(linedef.endVertex).x == 0.0) {
			piece = index;
		}
	}
	ok &= expect(doom.doomVertices.size() == 10 && doom.doomLinedefs.size() == 12 && piece >= 0
			&& doom.doomSidedefs.at(doom.doomLinedefs.at(piece).frontSidedef).sector == 2 && closed(doom),
		"The wall should split twice, its middle piece facing the new room, three lines drawn, and both rooms closed.");
	undoesToOriginal("Undoing the wall room should give back the map.");

	// Around both rooms, in the void: their outer walls open onto the new
	// sector, which surrounds them.
	ok &= expect(drawLevelMapDoomSector(&doom, corners({{-64, -64}, {-64, 192}, {320, 192}, {320, -64}}), &sector, &error) && sector == 2,
		"Drawing around both rooms should work.");
	int surrounded = 0;
	for (int index = 0; index < 7; ++index) {
		const LevelMapDoomLinedef& linedef = doom.doomLinedefs.at(index);
		surrounded += index != 2 && linedef.backSidedef >= 0 && doom.doomSidedefs.at(linedef.backSidedef).sector == 2 ? 1 : 0;
	}
	ok &= expect(surrounded == 6 && closed(doom), "Every outer wall of the two rooms should open onto the sector around them.");
	undoesToOriginal("Undoing the surrounding sector should give back the map.");

	// Shapes that cannot be a sector are refused, and leave no history.
	ok &= expect(!drawLevelMapDoomSector(&doom, corners({{100, 32}, {100, 96}, {160, 96}, {160, 32}}), &sector, &error)
			&& error.contains(QStringLiteral("crosses linedef 2")),
		"A shape across the rooms' shared line should be refused, naming it.");
	ok &= expect(!drawLevelMapDoomSector(&doom, corners({{300, 0}, {400, 100}, {400, 0}, {300, 100}}), &sector, &error)
			&& error.contains(QStringLiteral("crosses itself")),
		"A shape that crosses itself should be refused.");
	ok &= expect(!drawLevelMapDoomSector(&doom, corners({{300, 0}, {400, 0}}), &sector, &error) && error.contains(QStringLiteral("three corners")),
		"Two corners should be refused.");
	ok &= expect(!drawLevelMapDoomSector(&doom, corners({{300, 0}, {350, 0}, {400, 0}}), &sector, &error) && doom.undoStack.isEmpty(),
		"A flat shape should be refused, and refusals should leave no history.");

	// A vertex between two lines dissolves into one line; room 0 closes as a
	// triangle.
	ok &= expect(setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomVertex, 3}}, &error) && deleteLevelMapSelection(&doom, &error)
			&& doom.doomVertices.size() == 5 && doom.doomLinedefs.size() == 6 && doom.doomLinedefs.at(0).endVertex == 2 && closed(doom),
		"Deleting a vertex between two lines should join them.");
	undoesToOriginal("Undoing the vertex delete should give back the map.");

	// A linedef, and a thing with it, go in one step.
	ok &= expect(setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomLinedef, 6}, {LevelMapSelectionKind::DoomThing, 0}}, &error)
			&& deleteLevelMapSelection(&doom, &error) && doom.doomLinedefs.size() == 6 && doom.doomSidedefs.size() == 7 && doom.doomVertices.size() == 6
			&& doom.doomThings.isEmpty() && doom.entities.isEmpty() && doom.undoStack.size() == 1,
		"Deleting a linedef and a thing should be one step.");
	ok &= expect(undoLevelMapEdit(&doom, &error) && doom.doomThings.size() == 1 && doom.entities.size() == 1, "Undo should bring the thing back.");
	undoesToOriginal("Undoing the linedef delete should give back the map.");

	// Merging a vertex into another drops the line between them.
	ok &= expect(setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomVertex, 5}, {LevelMapSelectionKind::DoomVertex, 2}}, &error)
			&& mergeLevelMapVertices(&doom, &count, &error) && count == 1 && doom.doomVertices.size() == 5 && doom.doomLinedefs.size() == 6
			&& doom.selection.size() == 1 && doom.selectedObjectId == 2,
		"Merging vertex 5 into vertex 2 should drop the line between them and select vertex 2.");
	undoesToOriginal("Undoing the merge should give back the map.");

	// Joining the two rooms makes them one sector with the line between them
	// kept, two-sided; merging takes that line away and the rooms are one room.
	ok &= expect(setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, 1}, {LevelMapSelectionKind::DoomSector, 0}}, &error)
			&& joinLevelMapSectors(&doom, false, &count, &error) && count == 1 && doom.doomSectors.size() == 1 && doom.doomLinedefs.size() == 7
			&& doom.doomSidedefs.at(doom.doomLinedefs.at(2).backSidedef).sector == 0 && doom.selectedObjectId == 0 && closed(doom),
		"Joining room 1 into room 0 should leave one sector with the line between them.");
	undoesToOriginal("Undoing the join should give back the map.");
	ok &= expect(setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, 0}, {LevelMapSelectionKind::DoomSector, 1}}, &error)
			&& joinLevelMapSectors(&doom, true, &count, &error) && doom.doomSectors.size() == 1 && doom.doomLinedefs.size() == 6
			&& doom.doomVertices.size() == 6 && doom.undoStack.last().description == QStringLiteral("Merge 2 sectors into sector:0") && closed(doom),
		"Merging the rooms should take away the line between them and keep the rest.");
	undoesToOriginal("Undoing the merge should give back the map.");
	ok &= expect(setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, 0}}, &error) && !joinLevelMapSectors(&doom, false, &count, &error)
			&& error.contains(QStringLiteral("two or more")),
		"Joining a single sector should be refused.");

	// Two rooms drawn apart, each with its own vertices along x = 128: merging
	// those stitches their walls into one two-sided line.
	const QString apartPath = root.filePath(QStringLiteral("topology-apart.wad"));
	LevelMapDocument apart;
	const QVector<QPoint> apartVertices {{0, 0}, {128, 0}, {128, 128}, {0, 128}, {128, 0}, {128, 128}, {256, 0}, {256, 128}};
	const QVector<TestDoomLine> apartLines {
		{0, 3, 1, 0, -1},
		{3, 2, 1, 0, -1},
		{2, 1, 1, 0, -1},
		{1, 0, 1, 0, -1},
		{4, 5, 1, 1, -1},
		{5, 7, 1, 1, -1},
		{7, 6, 1, 1, -1},
		{6, 4, 1, 1, -1},
	};
	ok &= expect(writeFile(apartPath, doomWadOf(apartVertices, apartLines, {{0, 0, "WALL0"}, {1, 0, "WALL1"}}, 2))
			&& loadLevelMap({apartPath, QStringLiteral("MAP01"), {}}, &apart, &error),
		"The rooms-apart fixture should load.");
	ok &= expect(setLevelMapSelection(&apart, {{LevelMapSelectionKind::DoomVertex, 4}, {LevelMapSelectionKind::DoomVertex, 1}}, &error)
			&& mergeLevelMapVertices(&apart, &count, &error)
			&& setLevelMapSelection(&apart, {{LevelMapSelectionKind::DoomVertex, 4}, {LevelMapSelectionKind::DoomVertex, 2}}, &error)
			&& mergeLevelMapVertices(&apart, &count, &error),
		"Merging the rooms' vertices along x = 128 should work.");
	const LevelMapDoomLinedef stitched = apart.doomLinedefs.at(2);
	ok &= expect(apart.doomVertices.size() == 6 && apart.doomLinedefs.size() == 7 && stitched.backSidedef >= 0 && (stitched.flags & 0x0004) != 0
			&& apart.doomSidedefs.at(stitched.backSidedef).sector == 1 && closed(apart),
		"The two walls on x = 128 should become one two-sided line between the rooms.");
	return ok;
}

// Editing a Doom linedef's fields and its sides' fields: each a single undo
// step; a side other lines share is copied first, so only the line edited
// changes; and only a new sector marks the node lumps stale.
bool runDoomFieldsSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("fields.wad"));
	LevelMapDocument doom;
	ok &= expect(writeFile(path, twoRoomWad(true)) && loadLevelMap({path, QStringLiteral("MAP01"), {}}, &doom, &error),
		"The packed two-room fixture should load.");

	// Linedef fields.
	ok &= expect(setLevelMapLinedefProperty(&doom, 2, QStringLiteral("special"), QStringLiteral("1"), &error) && doom.doomLinedefs.at(2).special == 1
			&& setLevelMapLinedefProperty(&doom, 2, QStringLiteral("tag"), QStringLiteral("7"), &error) && doom.doomLinedefs.at(2).tag == 7
			&& setLevelMapLinedefProperty(&doom, 2, QStringLiteral("flags"), QStringLiteral("68"), &error) && doom.doomLinedefs.at(2).flags == 68,
		"A linedef's special, tag, and flags should be settable.");
	ok &= expect(doom.undoStack.size() == 3 && doom.undoStack.last().description == QStringLiteral("Set linedef:2 flags to 68") && !doom.doomGeometryChanged,
		"Each field should be one step, and none should stale the node lumps.");
	ok &= expect(setLevelMapLinedefProperty(&doom, 2, QStringLiteral("special"), QStringLiteral("1"), &error) && doom.undoStack.size() == 3,
		"Setting a value the line already has should record nothing.");
	ok &= expect(!setLevelMapLinedefProperty(&doom, 2, QStringLiteral("special"), QStringLiteral("door"), &error)
			&& !setLevelMapLinedefProperty(&doom, 2, QStringLiteral("arg0"), QStringLiteral("1"), &error) && error.contains(QStringLiteral("tag")),
		"A word for a number, or a Hexen argument on a Doom map, should be refused.");
	ok &= expect(undoLevelMapEdit(&doom, &error) && doom.doomLinedefs.at(2).flags == 4, "Undo should put the flags back.");

	// A side of its own changes in place; a shared one is copied first.
	const int sides = static_cast<int>(doom.doomSidedefs.size());
	ok &= expect(setLevelMapLinedefSideProperty(&doom, 2, true, QStringLiteral("middle"), QStringLiteral("grate"), &error)
			&& doom.doomSidedefs.size() == sides && doom.doomSidedefs.at(doom.doomLinedefs.at(2).frontSidedef).middleTexture == QStringLiteral("GRATE"),
		"A side only this line uses should change in place, its texture name in capitals.");
	ok &= expect(setLevelMapLinedefProperty(&doom, 2, QStringLiteral("back.offsetx"), QStringLiteral("32"), &error) && doom.doomSidedefs.size() == sides + 1
			&& doom.doomLinedefs.at(2).backSidedef == sides && doom.doomSidedefs.at(sides).offsetX == 32 && doom.doomSidedefs.at(4).offsetX == 9
			&& doom.doomLinedefs.at(6).frontSidedef == 4,
		"A back side the bottom wall shares should be copied for this line, the wall keeping its own.");
	ok &= expect(!doom.doomGeometryChanged, "Texture and offset edits should leave the node lumps current.");
	ok &= expect(setLevelMapLinedefSideProperty(&doom, 2, false, QStringLiteral("sector"), QStringLiteral("0"), &error) && doom.doomGeometryChanged,
		"A side facing another sector should stale the node lumps.");
	ok &= expect(!setLevelMapLinedefSideProperty(&doom, 0, false, QStringLiteral("middle"), QStringLiteral("WALL1"), &error)
			&& error.contains(QStringLiteral("one-sided")),
		"A one-sided line's back should be refused, saying why.");
	ok &= expect(!setLevelMapLinedefSideProperty(&doom, 2, true, QStringLiteral("upper"), QStringLiteral("NINECHARS"), &error),
		"A texture name longer than eight characters should be refused.");
	ok &= expect(setLevelMapLinedefSideProperty(&doom, 2, true, QStringLiteral("upper"), QString(), &error)
			&& doom.doomSidedefs.at(doom.doomLinedefs.at(2).frontSidedef).upperTexture == QStringLiteral("-"),
		"An empty texture should be written as -.");
	while (!doom.undoStack.isEmpty()) {
		undoLevelMapEdit(&doom, &error);
	}
	ok &= expect(doom.doomSidedefs.size() == sides && doom.doomLinedefs.at(2).backSidedef == 4 && !doom.doomGeometryChanged
			&& doom.doomSidedefs.at(3).middleTexture == QStringLiteral("-"),
		"Undoing every field edit should give back the sides as loaded.");

	// Hexen lines take five arguments instead of a tag.
	LevelMapDocument hexen;
	const QString hexenPath = root.filePath(QStringLiteral("fields-hexen.wad"));
	ok &= expect(writeFile(hexenPath, hexenWadFixture()) && loadLevelMap({hexenPath, QStringLiteral("MAP02"), {}}, &hexen, &error)
			&& hexen.doomFormat == LevelMapDoomFormat::Hexen,
		"The Hexen fixture should load.");
	ok &= expect(setLevelMapLinedefProperty(&hexen, 0, QStringLiteral("arg2"), QStringLiteral("200"), &error) && hexen.doomLinedefs.at(0).args.at(2) == 200
			&& !setLevelMapLinedefProperty(&hexen, 0, QStringLiteral("arg2"), QStringLiteral("300"), &error)
			&& !setLevelMapLinedefProperty(&hexen, 0, QStringLiteral("tag"), QStringLiteral("1"), &error),
		"A Hexen line should take arguments from 0 to 255, and no tag.");
	return ok;
}

// Editing a brush face's texture and alignment: each field rewritten on the
// face's own line and nowhere else, one undo step each, and undo giving the
// file back byte for byte.
bool runFaceFieldsSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const auto load = [&root, &error](const QString& name, const QByteArray& text, LevelMapDocument* document,
				  const QString& engine = QStringLiteral("idtech2")) {
		const QString path = root.filePath(name);
		return writeFile(path, text) && loadLevelMap({path, {}, engine}, document, &error);
	};
	const auto saved = [&root](const LevelMapDocument& document, const QString& name) {
		const QString path = root.filePath(name);
		return saveMapOverwriting(document, path) ? QString::fromUtf8(readFile(path)) : QString();
	};

	// Classic faces: the first face's line takes every edit, the other none.
	LevelMapDocument classic;
	ok &= expect(load(QStringLiteral("faces-classic.map"), quakeMapFixture(), &classic), "The classic face fixture should load.");
	const QString original = saved(classic, QStringLiteral("faces-classic-original.map"));
	ok &= expect(setLevelMapBrushFaceProperty(&classic, 0, 0, QStringLiteral("shiftx"), QStringLiteral("16"), &error)
			&& setLevelMapBrushFaceProperty(&classic, 0, 0, QStringLiteral("scalex"), QStringLiteral("0.5"), &error)
			&& setLevelMapBrushFaceProperty(&classic, 0, 0, QStringLiteral("rotation"), QStringLiteral("90"), &error)
			&& setLevelMapBrushFaceProperty(&classic, 0, 0, QStringLiteral("texture"), QStringLiteral("METAL"), &error),
		"Editing the first face's fields should work.");
	const QString edited = saved(classic, QStringLiteral("faces-classic-edited.map"));
	ok &= expect(edited.contains(QStringLiteral("( 0 0 0 ) ( 128 0 0 ) ( 128 128 0 ) METAL 16 0 90 0.5 1\n")) && edited.contains(QStringLiteral("CEIL1 0 0 0 1 1\n")),
		"The first face's line should carry its new texture and numbers, and the other face stay as written.");
	ok &= expect(classic.undoStack.size() == 4 && classic.undoStack.last().description == QStringLiteral("Set brush:0 face 1 texture to METAL"),
		"Each field should be one step, named for its face.");
	ok &= expect(setLevelMapBrushFaceProperty(&classic, 0, 0, QStringLiteral("shiftx"), QStringLiteral("16"), &error) && classic.undoStack.size() == 4,
		"A value the face already has should record nothing.");
	ok &= expect(!setLevelMapBrushFaceProperty(&classic, 0, 0, QStringLiteral("scaley"), QStringLiteral("0"), &error)
			&& !setLevelMapBrushFaceProperty(&classic, 0, 5, QStringLiteral("shiftx"), QStringLiteral("1"), &error)
			&& !setLevelMapBrushFaceProperty(&classic, 0, 0, QStringLiteral("tilt"), QStringLiteral("1"), &error),
		"A zero scale, a face the brush lacks, and an unknown field should be refused.");
	while (!classic.undoStack.isEmpty()) {
		undoLevelMapEdit(&classic, &error);
	}
	ok &= expect(saved(classic, QStringLiteral("faces-classic-undone.map")) == original, "Undoing every face edit should give the file back byte for byte.");

	// Compact lines: a face ending in a comment, and one ending in the brush's
	// and its entity's closing braces, keep them.
	LevelMapDocument compact;
	ok &= expect(load(QStringLiteral("faces-compact.map"), compactMapFixture(), &compact), "The compact face fixture should load.");
	ok &= expect(setLevelMapBrushFaceProperty(&compact, 0, 1, QStringLiteral("shifty"), QStringLiteral("8"), &error)
			&& setLevelMapBrushFaceProperty(&compact, 0, 5, QStringLiteral("shiftx"), QStringLiteral("4"), &error),
		"Editing faces on compact lines should work.");
	const QString compactText = saved(compact, QStringLiteral("faces-compact-edited.map"));
	ok &= expect(compactText.contains(QStringLiteral("BOTTOM 0 8 0 1 1 // trailing comment\n")) && compactText.contains(QStringLiteral("SOUTH 4 0 0 1 1 } }\n")),
		"A trailing comment and closing braces on a face's line should survive its edit.");

	// Valve 220: a shift moves the axis offset, and a turn turns the axes about
	// the face, as TrenchBroom does.
	LevelMapDocument valve;
	ok &= expect(load(QStringLiteral("faces-valve.map"), valve220MapFixture(), &valve), "The Valve 220 face fixture should load.");
	ok &= expect(setLevelMapBrushFaceProperty(&valve, 0, 0, QStringLiteral("shiftx"), QStringLiteral("32"), &error)
			&& setLevelMapBrushFaceProperty(&valve, 0, 0, QStringLiteral("rotation"), QStringLiteral("90"), &error),
		"Shifting and turning a Valve 220 face should work.");
	const QString valveText = saved(valve, QStringLiteral("faces-valve-edited.map"));
	ok &= expect(valveText.contains(QStringLiteral("TOPTEX [ 0 -1 0 32 ] [ -1 0 0 -8 ] 90 1 1\n")) && valveText.contains(QStringLiteral("BOTTOMTEX [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1\n")),
		"The top face's axes should turn a quarter about it with their offsets, and the bottom face stay as written.");
	LevelMapDocument reread;
	ok &= expect(load(QStringLiteral("faces-valve-reread.map"), valveText.toUtf8(), &reread) && reread.brushes.first().faces.first().rotation == 90.0
			&& reread.brushes.first().faces.first().uOffset == 32.0,
		"The turned face should read back with its rotation and offset.");

	// A line holding two faces cannot be rewritten for one of them, and a
	// brushDef face uses explicit matrix fields rather than classic texel fields.
	QString sharedText = QStringLiteral("{\n\"classname\" \"worldspawn\"\n") + testBoxBrush(0, 0, 0, 64, 64, 64, QStringLiteral("LINED")) + QStringLiteral("}\n");
	sharedText.replace(QStringLiteral("LINED 0 0 0 1 1\n( 0 1 0 )"), QStringLiteral("LINED 0 0 0 1 1 ( 0 1 0 )"));
	LevelMapDocument shared;
	ok &= expect(load(QStringLiteral("faces-shared.map"), sharedText.toUtf8(), &shared)
			&& !setLevelMapBrushFaceProperty(&shared, 0, 0, QStringLiteral("shiftx"), QStringLiteral("1"), &error)
			&& error.contains(QStringLiteral("more than one face on a line")),
		"A face sharing its line with another should be refused, saying why.");
	LevelMapDocument primitives;
	ok &= expect(load(QStringLiteral("faces-q3.map"), quake3MapFixture(), &primitives, QStringLiteral("idtech3"))
			&& !setLevelMapBrushFaceProperty(&primitives, 0, 0, QStringLiteral("shiftx"), QStringLiteral("1"), &error)
			&& error.contains(QStringLiteral("matrix00"))
			&& setLevelMapBrushFaceProperty(&primitives, 0, 0, QStringLiteral("matrix02"), QStringLiteral("0.5"), &error)
			&& primitives.brushes.first().faces.first().textureMatrix[2] == 0.5
			&& setLevelMapBrushFaceProperty(&primitives, 0, 0, QStringLiteral("texture"), QStringLiteral("textures/base_wall/metal"), &error),
		"A brushDef face should edit its matrix and texture while refusing classic shift fields.");
	return ok;
}

// Regressions from review 5: shapes over more than one area, vertices only
// node builders use, corners on vertices off the whole-unit grid, merging
// that reached past its vertex, unused sides left naming no sector, redraws
// that dropped a sector's tag, drawn-line counts, the edit revision, and
// undo steps that kept whole copies of the map.
bool runReview5Smoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int count = 0;
	int sector = -1;
	const auto corners = [](const QVector<QPointF>& points) {
		QVector<LevelMapVec3> list;
		for (const QPointF& point : points) {
			list.push_back({point.x(), point.y(), 0.0, true});
		}
		return list;
	};
	const auto closed = [](const LevelMapDocument& document) {
		const QVector<DoomSectorOutline> outlines = buildDoomSectorOutlines(document);
		return !outlines.isEmpty() && std::all_of(outlines.cbegin(), outlines.cend(), [](const DoomSectorOutline& outline) {
			return outline.openEdgeCount == 0;
		});
	};
	const auto load = [&root, &error](const QString& name, const QByteArray& bytes, LevelMapDocument* document) {
		const QString path = root.filePath(name);
		return writeFile(path, bytes) && loadLevelMap({path, QStringLiteral("MAP01"), {}}, document, &error);
	};

	// A shape whose edges look into different areas is refused: around both
	// rooms along room 0's wall, over both rooms, and through the wall's
	// corners half in the void.
	LevelMapDocument rooms;
	ok &= expect(load(QStringLiteral("r5-rooms.wad"), twoRoomWad(false), &rooms), "The two-room fixture should load.");
	ok &= expect(!drawLevelMapDoomSector(&rooms, corners({{0, -64}, {0, 192}, {320, 192}, {320, -64}}), &sector, &error)
			&& error.contains(QStringLiteral("more than one area")),
		"A shape around both rooms that runs along room 0's wall should be refused.");
	ok &= expect(!drawLevelMapDoomSector(&rooms, corners({{0, 0}, {0, 128}, {256, 128}, {256, 0}}), &sector, &error)
			&& error.contains(QStringLiteral("sector 0")) && error.contains(QStringLiteral("sector 1")),
		"A shape over both rooms should be refused, naming both.");
	ok &= expect(!drawLevelMapDoomSector(&rooms, corners({{-64, 32}, {0, 32}, {64, 32}, {64, 96}, {0, 96}, {-64, 96}}), &sector, &error)
			&& error.contains(QStringLiteral("more than one area")) && rooms.undoStack.isEmpty(),
		"A shape through the wall's corners, half in the void, should be refused and leave no history.");
	ok &= expect(drawLevelMapDoomSector(&rooms, corners({{-1, -64}, {-1, 192}, {320, 192}, {320, -64}}), &sector, &error) && closed(rooms),
		"The same ring clear of the wall should be drawn, every sector closed.");

	// Vertices no linedef uses, as node builders leave on walls, neither stop
	// an edge along the wall nor take a corner.
	const QVector<QPoint> seams {{0, 0}, {128, 0}, {128, 128}, {0, 128}, {256, 0}, {256, 128}, {0, 64}, {0, 96}};
	const QVector<TestDoomLine> lines {{0, 3, 1, 0, -1}, {3, 2, 1, 1, -1}, {2, 1, 4, 3, 4}, {1, 0, 1, 2, -1}, {2, 5, 1, 5, -1}, {5, 4, 1, 6, -1}, {4, 1, 1, 7, -1}};
	const QVector<TestDoomSide> sides {{0, 0, "WALL0"}, {0, 0, "WALL0"}, {0, 0, "WALL0"}, {0, 7, "-"}, {1, 9, "-"}, {1, 0, "WALL1"}, {1, 0, "WALL1"},
		{1, 0, "WALL1"}};
	LevelMapDocument seamed;
	ok &= expect(load(QStringLiteral("r5-seams.wad"), doomWadOf(seams, lines, sides, 2), &seamed), "The fixture with unused wall vertices should load.");
	int drawn = 0;
	ok &= expect(drawLevelMapDoomSector(&seamed, corners({{0, 32}, {0, 96}, {-64, 96}, {-64, 32}}), &sector, &error, &drawn) && drawn == 3
			&& closed(seamed),
		"A room drawn against a wall with an unused vertex on it should share the wall, drawing three lines.");
	int wallPieces = 0;
	for (const LevelMapDoomLinedef& linedef : seamed.doomLinedefs) {
		const LevelMapDoomVertex& start = seamed.doomVertices.at(linedef.startVertex);
		const LevelMapDoomVertex& end = seamed.doomVertices.at(linedef.endVertex);
		wallPieces += start.x == 0.0 && end.x == 0.0 ? 1 : 0;
	}
	ok &= expect(wallPieces == 3, "The wall should be split at the corners only, into three pieces, not at the unused vertices.");

	// A corner on a vertex off the whole-unit grid joins it.
	LevelMapDocument shifted;
	ok &= expect(load(QStringLiteral("r5-shifted.wad"), twoRoomWad(false), &shifted)
			&& moveLevelMapObject(&shifted, QStringLiteral("vertex"), 4, 0.486, 0.0, 0.0, &error),
		"Moving a vertex off the whole-unit grid should work.");
	ok &= expect(drawLevelMapDoomSector(&shifted, corners({{256.486, 0.0}, {256, 128}, {384, 128}, {384, 0}}), &sector, &error)
			&& shifted.doomVertices.size() == 8 && shifted.doomLinedefs.size() == 10 && closed(shifted),
		"A corner on the moved vertex should join it, sharing the wall rather than splitting it beside the vertex.");

	// Merging reaches only the lines at the vertex merged into.
	QVector<TestDoomLine> doubled = lines;
	doubled.push_back({5, 4, 1, 6, -1, 11});
	LevelMapDocument merging;
	ok &= expect(load(QStringLiteral("r5-merge.wad"), doomWadOf(seams, doubled, sides, 2), &merging)
			&& setLevelMapSelection(&merging, {{LevelMapSelectionKind::DoomVertex, 3}, {LevelMapSelectionKind::DoomVertex, 0}}, &error)
			&& mergeLevelMapVertices(&merging, &count, &error),
		"Merging room 0's top-left corner into its bottom-left should work.");
	ok &= expect(merging.doomLinedefs.size() == 7 && std::any_of(merging.doomLinedefs.cbegin(), merging.doomLinedefs.cend(), [](const LevelMapDoomLinedef& linedef) {
		return linedef.special == 11;
	}),
		"Only the line whose ends met should go; the line with a special over room 1's wall stays.");

	// Deleting a sector drops unused sides that name it, and no side is left
	// naming a sector that is gone.
	LevelMapDocument packed;
	ok &= expect(load(QStringLiteral("r5-packed.wad"), twoRoomWad(true), &packed)
			&& setLevelMapSelection(&packed, {{LevelMapSelectionKind::DoomSector, 1}}, &error) && deleteLevelMapSelection(&packed, &error),
		"Deleting room 1 of the packed fixture should work.");
	ok &= expect(std::all_of(packed.doomSidedefs.cbegin(), packed.doomSidedefs.cend(), [&packed](const LevelMapDoomSidedef& side) {
		return side.sector >= 0 && side.sector < packed.doomSectors.size();
	}),
		"Every side left should name a sector the map has.");

	// Drawn exactly over a sector, the new one keeps its tag and special.
	LevelMapDocument tagged;
	ok &= expect(load(QStringLiteral("r5-tagged.wad"), twoRoomWad(false), &tagged)
			&& setLevelMapSectorProperty(&tagged, 1, QStringLiteral("tag"), QStringLiteral("7"), &error)
			&& setLevelMapSectorProperty(&tagged, 1, QStringLiteral("special"), QStringLiteral("9"), &error)
			&& drawLevelMapDoomSector(&tagged, corners({{128, 0}, {128, 128}, {256, 128}, {256, 0}}), &sector, &error),
		"Redrawing room 1 exactly should work.");
	ok &= expect(tagged.doomSectors.size() == 2 && tagged.doomSectors.at(sector).tag == 7 && tagged.doomSectors.at(sector).special == 9,
		"The sector drawn over room 1 should take its place, tag and special included.");

	// A map that held only things keeps the sector drawn in it when saved:
	// the geometry lumps it lacked are written too.
	LevelMapDocument bare;
	const QString barePath = root.filePath(QStringLiteral("r5-bare-drawn.wad"));
	LevelMapDocument bareReloaded;
	ok &= expect(load(QStringLiteral("r5-bare.wad"), buildWad("PWAD", {{"MAP01", {}}, {"THINGS", doomThings()}}), &bare)
			&& drawLevelMapDoomSector(&bare, corners({{0, 0}, {0, 128}, {128, 128}, {128, 0}}), &sector, &error)
			&& saveLevelMapAs(bare, barePath).succeeded() && loadLevelMap({barePath, QStringLiteral("MAP01"), {}}, &bareReloaded, &error)
			&& bareReloaded.doomSectors.size() == 1 && bareReloaded.doomLinedefs.size() == 4 && bareReloaded.doomThings.size() == 1,
		"A sector drawn in a map with only things should be written and read back.");

	// Every edit, undo, and redo moves the revision on, and a topology step
	// keeps a delta the size of the edit, not copies of the map.
	LevelMapDocument counted;
	ok &= expect(load(QStringLiteral("r5-counted.wad"), twoRoomWad(false), &counted), "The revision fixture should load.");
	const quint64 start = counted.revision;
	ok &= expect(drawLevelMapDoomSector(&counted, corners({{256, 0}, {256, 128}, {384, 128}, {384, 0}}), &sector, &error)
			&& counted.revision == start + 1 && undoLevelMapEdit(&counted, &error) && counted.revision == start + 2
			&& redoLevelMapEdit(&counted, &error) && counted.revision == start + 3,
		"Each edit, undo, and redo should move the revision on.");
	const LevelMapUndoCommand& step = counted.undoStack.last();
	ok &= expect(step.commandKind == QStringLiteral("doom-topology") && step.sidedefDelta.appended.size() == 4 && step.linedefDelta.appended.size() == 3
			&& step.linedefDelta.changedIndexes.size() == 1 && step.vertexSnapshots.isEmpty() && step.sidedefSnapshots.isEmpty(),
		"The drawing's undo step should hold only what it changed and added.");
	return ok;
}

// Connect Entities links as Radiant does: each selected entity but the last
// targets the last, whose targetname is kept or made up as t<N>, in one undo
// step; Select Targets and Sources follow the links either way.
bool runConnectEntitiesSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	QString name;
	const QString path = root.filePath(QStringLiteral("connect.map"));
	LevelMapDocument document;
	int added = -1;
	ok &= expect(writeFile(path, quakeMapFixture()) && loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error)
			&& addLevelMapEntity(&document, QStringLiteral("info_null"), {64.0, 64.0, 0.0, true}, {}, &added, &error) && added == 2,
		"The connect fixture should load and take an info_null.");
	const auto saved = [&root](const LevelMapDocument& map, const QString& file) {
		const QString written = root.filePath(file);
		return saveMapOverwriting(map, written) ? QString::fromUtf8(readFile(written)) : QString();
	};
	const QString before = saved(document, QStringLiteral("connect-before.map"));

	// The light is named lamp already: the info_null targets it by that name.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}, {LevelMapSelectionKind::Entity, 1}}, &error)
			&& connectLevelMapEntities(&document, &name, &error) && name == QStringLiteral("lamp")
			&& propertyValueForTest(document, 2, QStringLiteral("target")) == QStringLiteral("lamp") && document.undoStack.size() == 2
			&& levelMapTargetLinks(document).size() == 1,
		"Connecting the info_null to the light should target lamp, as one step.");
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 2}}, &error)
			&& levelMapLinkedEntities(document, true) == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::Entity, 1}}
			&& setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 1}}, &error)
			&& levelMapLinkedEntities(document, false) == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::Entity, 2}},
		"Select Targets and Select Sources should follow the link either way.");

	// The other way round the info_null has no name, so it is given t1.
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::Entity, 2}}, &error)
			&& connectLevelMapEntities(&document, &name, &error) && name == QStringLiteral("t1")
			&& propertyValueForTest(document, 2, QStringLiteral("targetname")) == QStringLiteral("t1")
			&& propertyValueForTest(document, 1, QStringLiteral("target")) == QStringLiteral("t1")
			&& document.undoStack.last().description == QStringLiteral("Connect entity:1 to entity:2 as t1"),
		"Connecting the light to the unnamed info_null should name it t1.");
	const QString linked = saved(document, QStringLiteral("connect-linked.map"));
	ok &= expect(linked.contains(QStringLiteral("\"targetname\" \"t1\"")) && linked.contains(QStringLiteral("\"target\" \"t1\"")),
		"The saved map should carry the new name and target.");
	ok &= expect(undoLevelMapEdit(&document, &error) && undoLevelMapEdit(&document, &error) && saved(document, QStringLiteral("connect-undone.map")) == before,
		"Undoing both connections should give the file back byte for byte.");
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 0}, {LevelMapSelectionKind::Entity, 1}}, &error)
			&& !connectLevelMapEntities(&document, &name, &error) && error.contains(QStringLiteral("worldspawn")),
		"Worldspawn and one entity should be refused, saying why.");
	return ok;
}

// Regressions from review 6: a brush writing two faces on one line, face
// numbers the rewrite cannot find or the file cannot hold, a Hexen line's tag
// following arg0, Connect keeping a source's target, and thing fields the WAD
// cannot hold.
bool runReview6Smoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const auto load = [&root](const QString& file, const QString& text, LevelMapDocument* document) {
		const QString path = root.filePath(file);
		QString loadError;
		return writeFile(path, text.toUtf8()) && loadLevelMap({path, {}, QStringLiteral("idtech2")}, document, &loadError);
	};
	const auto saved = [&root](const LevelMapDocument& map, const QString& file) {
		const QString written = root.filePath(file);
		return saveMapOverwriting(map, written) ? QString::fromUtf8(readFile(written)) : QString();
	};

	// Faces 1 and 2 share a line. Setting face 3's texture leaves that line
	// alone, and undo gives the file back byte for byte.
	const QString sharedLine = QStringLiteral("( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOPTEX 0 0 0 1 1 ( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOTTEX 0 0 0 1 1");
	const QString shared = QStringLiteral("{\n\"classname\" \"worldspawn\"\n{\n") + sharedLine + QStringLiteral("\n"
		"( 64 1 0 ) ( 64 0 0 ) ( 64 0 1 ) SIDE 0 0 0 1 1\n( 0 0 1 ) ( 0 0 0 ) ( 0 1 0 ) SIDE 0 0 0 1 1\n"
		"( 0 64 1 ) ( 0 64 0 ) ( 1 64 0 ) SIDE 0 0 0 1 1\n( 1 0 0 ) ( 0 0 0 ) ( 0 0 1 ) SIDE 0 0 0 1 1\n}\n}\n");
	LevelMapDocument sharedMap;
	ok &= expect(load(QStringLiteral("shared.map"), shared, &sharedMap) && sharedMap.brushes.size() == 1 && sharedMap.brushes.first().faces.size() == 6,
		"The shared-line brush should load with six faces.");
	const QString sharedBefore = saved(sharedMap, QStringLiteral("shared-before.map"));
	ok &= expect(setLevelMapBrushFaceProperty(&sharedMap, 0, 2, QStringLiteral("texture"), QStringLiteral("NEWTEX"), &error), "Face 3 has a line of its own, so its texture can change.");
	const QString sharedAfter = saved(sharedMap, QStringLiteral("shared-after.map"));
	ok &= expect(sharedAfter.contains(sharedLine) && sharedAfter.contains(QStringLiteral("( 64 1 0 ) ( 64 0 0 ) ( 64 0 1 ) NEWTEX 0 0 0 1 1")),
		"Saving should rewrite face 3 alone and leave the shared line as written.");
	ok &= expect(undoLevelMapEdit(&sharedMap, &error) && saved(sharedMap, QStringLiteral("shared-undone.map")) == sharedBefore,
		"Undoing the texture should give the file back byte for byte.");
	const int depth = static_cast<int>(sharedMap.undoStack.size());
	ok &= expect(setLevelMapSelection(&sharedMap, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
			&& !rotateLevelMapSelection(&sharedMap, 2, 1, &error) && error.contains(QStringLiteral("two faces on one line")),
		"Turning a brush that writes two faces on one line should be refused, saying why.");
	ok &= expect(!moveLevelMapSelection(&sharedMap, 16.0, 0.0, 0.0, &error) && error.contains(QStringLiteral("two faces on one line"))
			&& !duplicateLevelMapSelection(&sharedMap, 16.0, 0.0, 0.0, &error) && error.contains(QStringLiteral("two faces on one line"))
			&& sharedMap.undoStack.size() == depth && saved(sharedMap, QStringLiteral("shared-refused.map")) == sharedBefore,
		"Moving or duplicating it should be refused too, leaving the map as it was.");

	// Face numbers: a comment glued on or between them is stepped over; too
	// few, or on the next line, and the edit is refused rather than dropped.
	const QString numbered = QStringLiteral("{\n\"classname\" \"worldspawn\"\n{\n"
		"( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) TOP 0 0 0 1 1// hi\n"
		"( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOT /* c */ 0 0 0 1 1\n"
		"( 64 1 0 ) ( 64 0 0 ) ( 64 0 1 ) FEW 0 0 0\n"
		"( 0 0 1 ) ( 0 0 0 ) ( 0 1 0 ) NEXT\n0 0 0 1 1\n"
		"( 0 64 1 ) ( 0 64 0 ) ( 1 64 0 ) SIDE 0 0 0 1 1\n( 1 0 0 ) ( 0 0 0 ) ( 0 0 1 ) SIDE 0 0 0 1 1\n}\n}\n");
	LevelMapDocument numbers;
	ok &= expect(load(QStringLiteral("numbers.map"), numbered, &numbers) && numbers.brushes.size() == 1, "The face-number fixture should load.");
	ok &= expect(setLevelMapBrushFaceProperty(&numbers, 0, 0, QStringLiteral("scaley"), QStringLiteral("2"), &error)
			&& setLevelMapBrushFaceProperty(&numbers, 0, 1, QStringLiteral("shiftx"), QStringLiteral("8"), &error),
		"Faces with comments by their numbers should take new numbers.");
	const QString rewritten = saved(numbers, QStringLiteral("numbers-after.map"));
	ok &= expect(rewritten.contains(QStringLiteral("TOP 0 0 0 1 2// hi")) && rewritten.contains(QStringLiteral("BOT /* c */ 8 0 0 1 1")),
		"The numbers should be rewritten around the comments.");
	ok &= expect(!setLevelMapBrushFaceProperty(&numbers, 0, 2, QStringLiteral("shiftx"), QStringLiteral("16"), &error)
			&& error.contains(QStringLiteral("does not write all of its texture numbers"))
			&& !setLevelMapBrushFaceProperty(&numbers, 0, 3, QStringLiteral("scalex"), QStringLiteral("2"), &error),
		"Too few numbers, or numbers on the next line, should be refused, not lost on save.");
	ok &= expect(!setLevelMapBrushFaceProperty(&numbers, 0, 4, QStringLiteral("scalex"), QStringLiteral("0.0000004"), &error) && error.contains(QStringLiteral("too small"))
			&& !setLevelMapBrushFaceProperty(&numbers, 0, 4, QStringLiteral("rotation"), QStringLiteral("1e300"), &error) && error.contains(QStringLiteral("too large"))
			&& setLevelMapBrushFaceProperty(&numbers, 0, 4, QStringLiteral("rotation"), QStringLiteral("0.1234567"), &error)
			&& saved(numbers, QStringLiteral("numbers-rotation.map")).contains(QStringLiteral("SIDE 0 0 0.123457 1 1")),
		"Numbers the file cannot hold as typed should be refused; others written to six places.");

	// A Hexen line's tag follows its first argument, and undo takes both back.
	LevelMapDocument hexen;
	const QString hexenPath = root.filePath(QStringLiteral("review6-hexen.wad"));
	ok &= expect(writeFile(hexenPath, hexenWadFixture()) && loadLevelMap({hexenPath, QStringLiteral("MAP02"), {}}, &hexen, &error), "The Hexen fixture should load.");
	const int tagBefore = hexen.doomLinedefs.isEmpty() ? -1 : hexen.doomLinedefs.first().tag;
	ok &= expect(setLevelMapLinedefProperty(&hexen, 0, QStringLiteral("arg0"), QStringLiteral("9"), &error) && hexen.doomLinedefs.first().tag == 9
			&& undoLevelMapEdit(&hexen, &error) && hexen.doomLinedefs.first().tag == tagBefore,
		"Setting a Hexen line's arg0 should set its tag, and undo should restore both.");

	// Connect keeps the link a source already has: the target takes its name.
	const QString wired = QStringLiteral("{\n\"classname\" \"worldspawn\"\n}\n"
		"{\n\"classname\" \"trigger_once\"\n\"target\" \"door1\"\n\"origin\" \"0 0 0\"\n}\n"
		"{\n\"classname\" \"func_door\"\n\"targetname\" \"door1\"\n\"origin\" \"64 0 0\"\n}\n"
		"{\n\"classname\" \"light\"\n\"origin\" \"128 0 0\"\n}\n");
	LevelMapDocument links;
	QString name;
	ok &= expect(load(QStringLiteral("wired.map"), wired, &links) && setLevelMapSelection(&links, {{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::Entity, 3}}, &error)
			&& connectLevelMapEntities(&links, &name, &error) && name == QStringLiteral("door1")
			&& propertyValueForTest(links, 1, QStringLiteral("target")) == QStringLiteral("door1")
			&& propertyValueForTest(links, 3, QStringLiteral("targetname")) == QStringLiteral("door1"),
		"Connecting a trigger that fires door1 to an unnamed light should name the light door1, keeping the door firing.");

	// Thing fields hold what the WAD can: whole numbers in their ranges.
	LevelMapDocument things;
	const QString thingsPath = root.filePath(QStringLiteral("review6-things.wad"));
	ok &= expect(writeFile(thingsPath, twoRoomWad(false)) && loadLevelMap({thingsPath, QStringLiteral("MAP01"), {}}, &things, &error), "The two-room WAD should load.");
	int thing = -1;
	ok &= expect(addLevelMapDoomThing(&things, 3001, 64.0, 64.0, 90, &thing, &error) && thing >= 0, "An imp should go in.");
	const int thingDepth = static_cast<int>(things.undoStack.size());
	ok &= expect(!setLevelMapEntityProperty(&things, thing, QStringLiteral("flags"), QStringLiteral("7.5"), &error)
			&& !setLevelMapEntityProperty(&things, thing, QStringLiteral("type"), QStringLiteral("70000"), &error)
			&& !setLevelMapEntityProperty(&things, thing, QStringLiteral("x"), QStringLiteral("12.5"), &error)
			&& !setLevelMapEntityProperty(&things, thing, QStringLiteral("targetname"), QStringLiteral("door1"), &error)
			&& !setLevelMapEntityProperty(&things, thing, QStringLiteral("tid"), QStringLiteral("3"), &error)
			&& things.undoStack.size() == thingDepth,
		"A fractional flag or position, a type past 65535, a key a thing has not got, or a Hexen field on a Doom map should be refused.");
	ok &= expect(setLevelMapEntityProperty(&things, thing, QStringLiteral("type"), QStringLiteral("3004"), &error)
			&& setLevelMapEntityProperty(&things, thing, QStringLiteral("x"), QStringLiteral("-96"), &error),
		"Whole numbers in range should be taken.");
	return ok;
}

// Make Door, as Doom Builder's: the ceiling comes down to the floor, a line to
// another room faces out with special 1 and the door texture above it, the
// door's own walls become lower-unpegged tracks, a packed side is copied
// before it changes, and undo gives the WAD back byte for byte. On a Hexen map
// the line takes Door_Raise, repeatable on use.
bool runMakeDoorSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("door.wad"));
	LevelMapDocument map;
	ok &= expect(writeFile(path, twoRoomWad(true)) && loadLevelMap({path, QStringLiteral("MAP01"), {}}, &map, &error), "The packed two-room WAD should load.");
	const auto saved = [&root](const LevelMapDocument& document, const QString& file) {
		const QString written = root.filePath(file);
		return saveMapOverwriting(document, written) ? readFile(written) : QByteArray();
	};
	const auto side = [&map](int id) {
		return map.doomSidedefs.value(id);
	};
	const QByteArray before = saved(map, QStringLiteral("door-before.wad"));
	int doors = 0;
	ok &= expect(!makeLevelMapDoors(&map, {}, &doors, &error) && error.contains(QStringLiteral("Select")), "Make Door with no sector selected should say what to select.");

	// The right room: the line between the rooms already faces out of it.
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomSector, 1}}, &error) && makeLevelMapDoors(&map, {}, &doors, &error) && doors == 1
			&& map.undoStack.last().description == QStringLiteral("Make sector:1 a door"),
		"The right room should become a door in one step.");
	ok &= expect(map.doomSectors.at(1).ceilingHeight == map.doomSectors.at(1).floorHeight, "The door's ceiling should come down to its floor.");
	const LevelMapDoomLinedef shared = map.doomLinedefs.at(2);
	ok &= expect(shared.special == 1 && side(shared.frontSidedef).sector == 0 && side(shared.frontSidedef).upperTexture == QStringLiteral("BIGDOOR2")
			&& side(shared.frontSidedef).offsetX == 0 && (shared.flags & 0x0018) == 0 && (shared.flags & 0x0004) != 0,
		"The line between the rooms should open the door from outside, BIGDOOR2 above it, offsets reset, unpegged bits cleared.");
	bool tracks = true;
	for (const int line : {4, 5, 6}) {
		const LevelMapDoomLinedef& wall = map.doomLinedefs.at(line);
		tracks = tracks && side(wall.frontSidedef).middleTexture == QStringLiteral("DOORTRAK") && (wall.flags & 0x0010) != 0 && (wall.flags & 0x0008) == 0;
	}
	ok &= expect(tracks, "The door's own walls should be DOORTRAK tracks, lower unpegged.");
	ok &= expect(map.doomLinedefs.at(6).frontSidedef != shared.backSidedef && side(shared.backSidedef).middleTexture != QStringLiteral("DOORTRAK"),
		"A sidedef packed between the door's inside and a wall should be copied, not turned into a track for both.");
	ok &= expect(undoLevelMapEdit(&map, &error) && saved(map, QStringLiteral("door-undone.wad")) == before, "Undo should give the WAD back byte for byte.");

	// The left room: the line faces into it, so it is flipped to face out.
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomSector, 0}}, &error) && makeLevelMapDoors(&map, {}, &doors, &error),
		"The left room should become a door.");
	const LevelMapDoomLinedef flipped = map.doomLinedefs.at(2);
	ok &= expect(flipped.startVertex == 1 && flipped.endVertex == 2 && side(flipped.frontSidedef).sector == 1 && flipped.special == 1
			&& side(flipped.frontSidedef).upperTexture == QStringLiteral("BIGDOOR2"),
		"A line facing into the door should be flipped, so it opens the door from outside.");
	ok &= expect(undoLevelMapEdit(&map, &error), "Undo should take the left door back.");

	// Chosen textures, kept offsets, and a new ceiling flat; bad names refused.
	LevelMapDoorOptions options;
	options.doorTexture = QStringLiteral("door3");
	options.trackTexture = QStringLiteral("lite5");
	options.ceilingFlat = QStringLiteral("flat20");
	options.resetOffsets = false;
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomSector, 1}}, &error) && makeLevelMapDoors(&map, options, &doors, &error)
			&& side(map.doomLinedefs.at(2).frontSidedef).upperTexture == QStringLiteral("DOOR3") && side(map.doomLinedefs.at(2).frontSidedef).offsetX == 7
			&& side(map.doomLinedefs.at(4).frontSidedef).middleTexture == QStringLiteral("LITE5") && map.doomSectors.at(1).ceilingTexture == QStringLiteral("FLAT20"),
		"Chosen textures and flat should go on, upper-cased, with the offsets kept.");
	ok &= expect(undoLevelMapEdit(&map, &error), "Undo should take that door back too.");
	options.doorTexture = QStringLiteral("NINECHARS");
	ok &= expect(!makeLevelMapDoors(&map, options, &doors, &error) && error.contains(QStringLiteral("1 to 8 characters")), "A texture name past eight characters should be refused.");

	// Hexen: Door_Raise(0, 16, 150), on use and repeatable.
	map.doomFormat = LevelMapDoomFormat::Hexen;
	ok &= expect(makeLevelMapDoors(&map, {}, &doors, &error), "A Hexen-format map should take a door too.");
	const LevelMapDoomLinedef hexenLine = map.doomLinedefs.at(2);
	ok &= expect(hexenLine.special == 12 && hexenLine.args == std::array<int, 5> {{0, 16, 150, 0, 0}} && hexenLine.tag == 0
			&& (hexenLine.flags & 0x1c00) == 0x0400 && (hexenLine.flags & 0x0200) != 0,
		"On a Hexen map the line should take Door_Raise with speed 16 and delay 150, on use and repeatable.");
	return ok;
}

// Doom Builder's quick sector actions: raise and lower within the lumps'
// ranges, and gradients from the first sector picked to the last. Neither
// moves a line, so the nodes stay current.
bool runSectorFieldsSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("sector-fields.wad"));
	LevelMapDocument map;
	ok &= expect(writeFile(path, twoRoomWad(false)) && loadLevelMap({path, QStringLiteral("MAP01"), {}}, &map, &error), "The two-room WAD should load.");
	int changed = 0;
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomSector, 0}, {LevelMapSelectionKind::DoomSector, 1}}, &error)
			&& shiftLevelMapSectors(&map, LevelMapSectorField::Floor, 8, &changed, &error) && changed == 2
			&& map.doomSectors.at(0).floorHeight == 8 && map.doomSectors.at(1).floorHeight == 8
			&& map.undoStack.last().description == QStringLiteral("Raise 2 floors by 8") && !map.doomGeometryChanged,
		"Raising two floors by 8 should be one step that leaves the nodes current.");
	ok &= expect(undoLevelMapEdit(&map, &error) && map.doomSectors.at(0).floorHeight == 0, "Undo should put the floors back.");
	const int light = map.doomSectors.at(0).lightLevel;
	ok &= expect(shiftLevelMapSectors(&map, LevelMapSectorField::Light, 300, &changed, &error) && map.doomSectors.at(0).lightLevel == 255
			&& !shiftLevelMapSectors(&map, LevelMapSectorField::Light, 16, &changed, &error) && error.contains(QStringLiteral("limit")),
		"Light should stop at 255, and a raise past it be refused, saying why.");
	ok &= expect(undoLevelMapEdit(&map, &error) && map.doomSectors.at(0).lightLevel == light, "Undo should put the light back.");
	ok &= expect(shiftLevelMapSectors(&map, LevelMapSectorField::Ceiling, -40000, &changed, &error) && map.doomSectors.at(1).ceilingHeight == -32768,
		"A ceiling lowered past the lump's range should stop at -32768.");
	ok &= expect(undoLevelMapEdit(&map, &error), "Undo should put the ceilings back.");

	// A gradient needs three sectors; a third is added beside the lumps' two.
	ok &= expect(!gradientLevelMapSectors(&map, LevelMapSectorField::Light, &changed, &error) && error.contains(QStringLiteral("three or more")),
		"A gradient over two sectors should be refused.");
	LevelMapDoomSector third = map.doomSectors.at(1);
	third.id = 2;
	map.doomSectors.push_back(third);
	map.doomSectors[0].lightLevel = 100;
	map.doomSectors[1].lightLevel = 0;
	map.doomSectors[2].lightLevel = 200;
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomSector, 0}, {LevelMapSelectionKind::DoomSector, 1}, {LevelMapSelectionKind::DoomSector, 2}}, &error)
			&& gradientLevelMapSectors(&map, LevelMapSectorField::Light, &changed, &error) && changed == 1 && map.doomSectors.at(1).lightLevel == 150
			&& map.undoStack.last().description == QStringLiteral("Grade the light levels of 3 sectors from 100 to 200"),
		"A gradient should run evenly from the first sector picked to the last.");
	ok &= expect(undoLevelMapEdit(&map, &error) && map.doomSectors.at(1).lightLevel == 0, "Undo should take the gradient back.");
	return ok;
}

// Regressions from review 7: a face sharing its line with another brush's,
// and Doom things put where the WAD cannot hold them through their origin, a
// move, or a turn.
bool runReview7Smoke(const QDir& root)
{
	bool ok = true;
	QString error;
	// Brush 0's last face and brush 1's first face share a line.
	const QString text = QStringLiteral("{\n\"classname\" \"worldspawn\"\n{\n"
		"( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) AAA 0 0 0 1 1\n( 64 -64 -16 ) ( 64 -64 -15 ) ( 64 -63 -16 ) AAA 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) AAA 0 0 0 1 1\n( -64 64 -16 ) ( -63 64 -16 ) ( -64 64 -15 ) AAA 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) AAA 0 0 0 1 1\n"
		"( -64 -64 0 ) ( -64 -63 0 ) ( -63 -64 0 ) AAA 0 0 0 1 1 } { ( 128 -64 -16 ) ( 128 -63 -16 ) ( 128 -64 -15 ) BBB 0 0 0 1 1\n"
		"( 256 -64 -16 ) ( 256 -64 -15 ) ( 256 -63 -16 ) BBB 0 0 0 1 1\n( 128 -64 -16 ) ( 128 -64 -15 ) ( 129 -64 -16 ) BBB 0 0 0 1 1\n"
		"( 128 64 -16 ) ( 129 64 -16 ) ( 128 64 -15 ) BBB 0 0 0 1 1\n( 128 -64 -16 ) ( 129 -64 -16 ) ( 128 -63 -16 ) BBB 0 0 0 1 1\n"
		"( 128 -64 0 ) ( 128 -63 0 ) ( 129 -64 0 ) BBB 0 0 0 1 1\n}\n}\n");
	const QString path = root.filePath(QStringLiteral("review7-shared.map"));
	LevelMapDocument shared;
	ok &= expect(writeFile(path, text.toUtf8()) && loadLevelMap({path, {}, QStringLiteral("idtech2")}, &shared, &error) && shared.brushes.size() == 2,
		"The two brushes sharing a line should load.");
	ok &= expect(!setLevelMapBrushFaceProperty(&shared, 1, 0, QStringLiteral("texture"), QStringLiteral("CCC"), &error) && error.contains(QStringLiteral("brush 0"))
			&& !setLevelMapBrushFaceProperty(&shared, 1, 0, QStringLiteral("shiftx"), QStringLiteral("16"), &error)
			&& !setLevelMapBrushFaceProperty(&shared, 0, 5, QStringLiteral("texture"), QStringLiteral("CCC"), &error) && shared.undoStack.isEmpty(),
		"A face on a line another brush's face shares should refuse texture and number edits, naming the other brush.");
	ok &= expect(setLevelMapBrushFaceProperty(&shared, 1, 1, QStringLiteral("texture"), QStringLiteral("CCC"), &error),
		"A face with a line of its own on the same brush should still take a texture.");

	// Things stay on whole units within 16 bits: through origin, a move, or a turn.
	LevelMapDocument things;
	const QString wadPath = root.filePath(QStringLiteral("review7-things.wad"));
	ok &= expect(writeFile(wadPath, twoRoomWad(false)) && loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &things, &error), "The two-room WAD should load.");
	int first = -1;
	int second = -1;
	ok &= expect(addLevelMapDoomThing(&things, 3001, 32.0, 32.0, 0, &first, &error) && addLevelMapDoomThing(&things, 3001, 33.0, 32.0, 0, &second, &error),
		"Two imps a unit apart should go in.");
	const int depth = static_cast<int>(things.undoStack.size());
	ok &= expect(!setLevelMapEntityProperty(&things, first, QStringLiteral("origin"), QStringLiteral("0.5 700 0"), &error)
			&& !setLevelMapEntityProperty(&things, first, QStringLiteral("origin"), QStringLiteral("0 40000 0"), &error)
			&& !setLevelMapEntityProperty(&things, first, QStringLiteral("origin"), QStringLiteral("0 0 99999"), &error) && error.contains(QStringLiteral("no height"))
			&& !moveLevelMapObject(&things, QStringLiteral("thing"), first, 40000.0, 0.0, 0.0, &error) && things.undoStack.size() == depth,
		"An origin off whole units, past 16 bits, or with a height on a Doom map, and a move past 16 bits, should be refused.");
	ok &= expect(setLevelMapEntityProperty(&things, first, QStringLiteral("origin"), QStringLiteral("64 96 0"), &error),
		"A whole origin in range should be taken.");
	ok &= expect(undoLevelMapEdit(&things, &error), "Undo should put the imp back.");
	ok &= expect(setLevelMapSelection(&things, {{LevelMapSelectionKind::DoomThing, first}, {LevelMapSelectionKind::DoomThing, second}}, &error)
			&& rotateLevelMapSelection(&things, 2, 1, &error),
		"Turning the two imps a quarter should work.");
	const auto whole = [](double value) {
		return value == std::round(value);
	};
	bool onUnits = true;
	for (const LevelMapDoomThing& thing : things.doomThings) {
		onUnits = onUnits && whole(thing.x) && whole(thing.y);
	}
	ok &= expect(onUnits, "A turn should land things on whole units, as the WAD will store them.");
	return ok;
}

// Doom tag links: a line with a special and a tag acts on the sectors that
// carry the tag; Select Targets and Sources follow them either way. Hexen
// lines are left out.
bool runTagLinksSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("tag-links.wad"));
	LevelMapDocument map;
	ok &= expect(writeFile(path, twoRoomWad(false)) && loadLevelMap({path, QStringLiteral("MAP01"), {}}, &map, &error) && levelMapTagLinks(map).isEmpty(),
		"The two-room WAD should load with no tag links.");
	ok &= expect(setLevelMapSectorProperty(&map, 1, QStringLiteral("tag"), QStringLiteral("5"), &error)
			&& setLevelMapLinedefProperty(&map, 0, QStringLiteral("tag"), QStringLiteral("5"), &error),
		"A tag on the right room and on a line of the left should be set.");
	ok &= expect(levelMapTagLinks(map).isEmpty(), "A tagged line with no special acts on nothing.");
	ok &= expect(setLevelMapLinedefProperty(&map, 0, QStringLiteral("special"), QStringLiteral("62"), &error), "The line should take a lift special.");
	const QVector<LevelMapTagLink> links = levelMapTagLinks(map);
	ok &= expect(links.size() == 1 && links.first().linedefId == 0 && links.first().sectorId == 1 && links.first().tag == 5,
		"The line should link to the sector with its tag.");
	ok &= expect(setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomLinedef, 0}}, &error)
			&& levelMapLinkedEntities(map, true) == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::DoomSector, 1}}
			&& setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomSector, 1}}, &error)
			&& levelMapLinkedEntities(map, false) == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::DoomLinedef, 0}},
		"Select Targets from the line should find the sector, and Select Sources from the sector the line.");
	// A door used by hand acts on the sector behind its line, whatever tag the
	// line carries: vanilla's DR and D1 doors, and Boom's generalized ones.
	ok &= expect(setLevelMapLinedefProperty(&map, 0, QStringLiteral("special"), QStringLiteral("1"), &error) && levelMapTagLinks(map).isEmpty()
			&& setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomLinedef, 0}}, &error) && levelMapLinkedEntities(map, true).isEmpty(),
		"A manual door's tag should link to nothing, and Select Targets on it should find nothing.");
	ok &= expect(setLevelMapLinedefProperty(&map, 0, QStringLiteral("special"), QString::number(0x3C07), &error) && levelMapTagLinks(map).isEmpty()
			&& setLevelMapLinedefProperty(&map, 0, QStringLiteral("special"), QString::number(0x3C01), &error) && levelMapTagLinks(map).size() == 1,
		"A Boom generalized door used by hand (DR) should link to nothing, one walked over (WR) to its tagged sector.");
	// Make Door leaves its lines untagged: a door used by hand has no tag.
	int doors = 0;
	ok &= expect(setLevelMapLinedefProperty(&map, 2, QStringLiteral("tag"), QStringLiteral("9"), &error)
			&& setLevelMapSelection(&map, {{LevelMapSelectionKind::DoomSector, 1}}, &error) && makeLevelMapDoors(&map, {}, &doors, &error)
			&& map.doomLinedefs.at(2).special == 1 && map.doomLinedefs.at(2).tag == 0,
		"Make Door should clear the tag its door lines carried.");
	map.doomFormat = LevelMapDoomFormat::Hexen;
	ok &= expect(levelMapTagLinks(map).isEmpty(), "Hexen lines should not be read as tag links.");
	return ok;
}

// Regressions from review 4: carving and deleting brush entities' brushes,
// moving brush entities, Select All with brush entities, a patch shader
// sharing its line, split offsets, and the stale-node flag across undo.
bool runReview4Smoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int count = 0;
	QStringList skipped;
	const auto load = [&root, &error](const QString& name, const QString& text, LevelMapDocument* document,
				  const QString& engine = QStringLiteral("idtech2")) {
		const QString path = root.filePath(name);
		return writeFile(path, text.toUtf8()) && loadLevelMap({path, {}, engine}, document, &error);
	};
	const auto savedText = [&root](const LevelMapDocument& document, const QString& name) {
		const QString path = root.filePath(name);
		return saveMapOverwriting(document, path) ? QString::fromUtf8(readFile(path)) : QString();
	};
	const QString world = QStringLiteral("{\n\"classname\" \"worldspawn\"\n");

	// A brush carved away whole, whose closing brace shares a line with its
	// entity's, is refused rather than cut out with that line.
	{
		QString text = world + testBoxBrush(-16, -16, -16, 80, 80, 80, QStringLiteral("CARVER"))
			+ testBoxBrush(0, 0, 0, 64, 64, 64, QStringLiteral("INNER")) + QStringLiteral("}\n");
		text.replace(QStringLiteral("INNER 0 0 0 1 1\n}\n}\n"), QStringLiteral("INNER 0 0 0 1 1 } }\n"));
		LevelMapDocument document;
		ok &= expect(load(QStringLiteral("r4-shared-close.map"), text, &document) && document.brushes.size() == 2,
			"The shared-close fixture should load.");
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
				&& !carveLevelMapSelection(&document, &count, &error, &skipped) && error.contains(QStringLiteral("shares a source line"))
				&& document.brushes.size() == 2 && document.undoStack.isEmpty(),
			"Carving away a brush that closes on its entity's line should be refused.");
	}

	// A brush that meets the carver only across an edge-to-edge gap stays
	// whole: no face of either brush separates them, the edges' cross does.
	{
		const QVector<LevelMapVec3> corners {{32.0, -10.0, 60.0, true}, {32.0, 10.0, 80.0, true}, {52.0, 0.0, 90.0, true}, {12.0, 0.0, 90.0, true}};
		const QString text = world + testBoxBrush(0, 0, 0, 64, 64, 64, QStringLiteral("CARVER")) + testTetraBrush(corners, QStringLiteral("TETRA"))
			+ QStringLiteral("}\n");
		LevelMapDocument document;
		ok &= expect(load(QStringLiteral("r4-edge.map"), text, &document) && document.brushes.size() == 2 && document.brushes.at(1).boundsSolved,
			"The edge fixture should load with a closed tetrahedron.");
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
				&& !carveLevelMapSelection(&document, &count, &error) && error.contains(QStringLiteral("overlap")) && document.brushes.size() == 2,
			"A brush clear of the carver should be left whole, not cut into pieces.");
	}

	// A brush entity: carving or deleting its only brush takes it too, moving
	// it moves its brush once without adding an origin, and Select All picks
	// it through its brush.
	{
		const QString text = world + testBoxBrush(-16, -16, -16, 80, 80, 80, QStringLiteral("CARVER"))
			+ QStringLiteral("}\n{\n\"classname\" \"func_door\"\n\"targetname\" \"gate\"\n") + testBoxBrush(0, 0, 0, 64, 64, 64, QStringLiteral("DOOR"))
			+ QStringLiteral("}\n");
		LevelMapDocument document;
		ok &= expect(load(QStringLiteral("r4-door.map"), text, &document) && document.entities.size() == 2 && document.brushes.size() == 2,
			"The door fixture should load.");
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
				&& carveLevelMapSelection(&document, &count, &error) && count == 1 && document.entities.size() == 1 && document.brushes.size() == 1,
			"Carving away the door's only brush should take the door entity with it.");
		ok &= expect(!savedText(document, QStringLiteral("r4-door-carved.map")).contains(QStringLiteral("func_door")),
			"The carved map should have no empty door left in it.");
		ok &= expect(undoLevelMapEdit(&document, &error) && document.entities.size() == 2
				&& savedText(document, QStringLiteral("r4-door-uncarved.map")) == text,
			"Undoing the carve should bring the door back exactly.");

		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error) && deleteLevelMapSelection(&document, &error)
				&& document.entities.size() == 1 && document.brushes.size() == 1
				&& document.undoStack.last().description == QStringLiteral("Delete brush:1"),
			"Deleting the door's only brush should delete the door too, named by the brush.");
		ok &= expect(undoLevelMapEdit(&document, &error) && savedText(document, QStringLiteral("r4-door-undeleted.map")) == text,
			"Undoing the delete should bring the door back exactly.");

		const QVector<QVector<LevelMapSelectionRef>> selections {
			{{LevelMapSelectionKind::Entity, 1}},
			{{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::QuakeBrush, 1}},
		};
		for (const QVector<LevelMapSelectionRef>& selection : selections) {
			ok &= expect(setLevelMapSelection(&document, selection, &error) && moveLevelMapSelection(&document, 16.0, 0.0, 0.0, &error)
					&& nearly(document.brushes.at(1).mins.x, 16.0) && propertyValueForTest(document, 1, QStringLiteral("origin")).isEmpty(),
				"Moving the door should move its brush once and give it no origin.");
			ok &= expect(undoLevelMapEdit(&document, &error) && nearly(document.brushes.at(1).mins.x, 0.0), "The door move should undo.");
		}
		ok &= expect(moveLevelMapObject(&document, QStringLiteral("entity"), 1, 0.0, 32.0, 0.0, &error) && nearly(document.brushes.at(1).mins.y, 32.0)
				&& propertyValueForTest(document, 1, QStringLiteral("origin")).isEmpty() && document.selectedObjectId == 1
				&& document.undoStack.last().description == QStringLiteral("Move entity:1 by 0,32,0"),
			"Moving the door by its entity should move its brush and name the door.");
		ok &= expect(undoLevelMapEdit(&document, &error), "The door's entity move should undo.");
		ok &= expect(setLevelMapEntityProperty(&document, 1, QStringLiteral("origin"), QStringLiteral("32 32 32"), &error)
				&& setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 1}}, &error) && moveLevelMapSelection(&document, 16.0, 0.0, 0.0, &error)
				&& propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("48 32 32") && nearly(document.brushes.at(1).mins.x, 16.0),
			"A door that has an origin should move it along with its brush.");
		ok &= expect(undoLevelMapEdit(&document, &error) && undoLevelMapEdit(&document, &error), "The origin edits should undo.");

		const QVector<LevelMapSelectionRef> all = levelMapSelectAllObjects(document, false);
		ok &= expect(all.size() == 2 && all.contains(LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, 1})
				&& !all.contains(LevelMapSelectionRef {LevelMapSelectionKind::Entity, 1}),
			"Select All should pick the door through its brush, not as an entity.");
		const QVector<LevelMapSelectionRef> otherBrush {{LevelMapSelectionKind::QuakeBrush, 0}};
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 1}}, &error)
				&& levelMapSelectAllObjects(document, true) == otherBrush,
			"Inverting from the door's brush should pick only the other brush, not the door.");
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 1}}, &error) && levelMapSelectAllObjects(document, true) == otherBrush,
			"Inverting from the door entity should leave the door's brush out.");
		ok &= expect(levelMapSelectAllObjects(document, false, [](const LevelMapSelectionRef& ref) {
			return ref.objectId != 0;
		}).size() == 1,
			"Select All should leave out what the view hides.");
	}

	// Carving leaves spared (hidden) brushes alone, and names the entity when
	// it is the entity's closing line that is shared.
	{
		const QString text = world + testBoxBrush(0, 0, 0, 64, 64, 64, QStringLiteral("CARVER")) + testBoxBrush(32, 0, 0, 96, 64, 64, QStringLiteral("NEXT"))
			+ QStringLiteral("}\n{\n\"classname\" \"func_wall\"\n") + testBoxBrush(-32, 0, 0, 32, 64, 64, QStringLiteral("WALL")) + QStringLiteral("} // the wall\n");
		LevelMapDocument document;
		ok &= expect(load(QStringLiteral("r4-spared.map"), text, &document) && document.brushes.size() == 3, "The spared fixture should load.");
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
				&& !carveLevelMapSelection(&document, &count, &error, &skipped, {1}) && error.contains(QStringLiteral("Entity 1 closes on a line shared"))
				&& document.brushes.size() == 3,
			"With the brush beside it spared, the carve should cut nothing and name the entity whose line is shared.");
		ok &= expect(carveLevelMapSelection(&document, &count, &error, &skipped) && count == 1 && skipped.size() == 1
				&& skipped.first().contains(QStringLiteral("Entity 1")),
			"With nothing spared, the carve should cut the brush beside it and report the wall left whole.");
	}

	// A patch whose shader sits on the patchDef2 line takes a new texture
	// in place.
	{
		const QString text = world
			+ QStringLiteral("{\npatchDef2 { base/patch\n( 3 3 0 0 0 )\n(\n"
					 "( ( 0 0 0 0 0 ) ( 0 32 0 0 0.5 ) ( 0 64 0 0 1 ) )\n"
					 "( ( 32 0 0 0.5 0 ) ( 32 32 0 0.5 0.5 ) ( 32 64 0 0.5 1 ) )\n"
					 "( ( 64 0 0 1 0 ) ( 64 32 0 1 0.5 ) ( 64 64 0 1 1 ) )\n)\n}\n}\n}\n");
		LevelMapDocument document;
		ok &= expect(load(QStringLiteral("r4-patch.map"), text, &document, QStringLiteral("idtech3")) && document.patches.size() == 1
				&& document.patches.first().textureName == QStringLiteral("base/patch"),
			"The patch fixture should load.");
		ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakePatch, 0}}, &error)
				&& applyLevelMapTexture(&document, QStringLiteral("base/new"), &count, &error) && count == 1,
			"Applying a texture to the patch should work.");
		const QString saved = savedText(document, QStringLiteral("r4-patch-saved.map"));
		LevelMapDocument reloaded;
		ok &= expect(saved.contains(QStringLiteral("patchDef2 { base/new\n")) && !saved.contains(QStringLiteral("base/patch"))
				&& load(QStringLiteral("r4-patch-reloaded.map"), saved, &reloaded, QStringLiteral("idtech3")) && reloaded.patches.size() == 1
				&& reloaded.patches.first().textureName == QStringLiteral("base/new"),
			"The new shader should replace the old one beside patchDef2, and the patch read back whole.");
	}

	// Texture offsets carry on across a split on both sides of a two-sided
	// line; a back side another line shares is copied instead of changed.
	for (const bool packed : {false, true}) {
		const QString path = root.filePath(packed ? QStringLiteral("r4-rooms-packed.wad") : QStringLiteral("r4-rooms.wad"));
		LevelMapDocument doom;
		ok &= expect(writeFile(path, twoRoomWad(packed)) && loadLevelMap({path, QStringLiteral("MAP01"), {}}, &doom, &error)
				&& doom.doomLinedefs.size() == 7,
			"The two-room fixture should load.");
		const int sides = static_cast<int>(doom.doomSidedefs.size());
		ok &= expect(setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomLinedef, 2}}, &error) && splitLevelMapLinedefs(&doom, &count, &error),
			"Splitting the shared line should work.");
		const LevelMapDoomLinedef first = doom.doomLinedefs.at(2);
		const LevelMapDoomLinedef second = doom.doomLinedefs.last();
		ok &= expect(doom.doomSidedefs.at(second.frontSidedef).offsetX == 71 && doom.doomSidedefs.at(second.backSidedef).offsetX == 9
				&& doom.doomSidedefs.at(first.frontSidedef).offsetX == 7 && doom.doomSidedefs.at(first.backSidedef).offsetX == 73,
			"Each half should show the part of its textures it showed before the split.");
		ok &= expect(packed ? (first.backSidedef != 4 && doom.doomSidedefs.at(4).offsetX == 9 && doom.doomSidedefs.size() == sides + 3)
				    : (first.backSidedef == 4 && doom.doomSidedefs.size() == sides + 2),
			"A back side the bottom wall shares should be copied, and one of the line's own changed in place.");
		ok &= expect(doom.doomGeometryChanged, "A split should mark the node lumps stale.");
		ok &= expect(undoLevelMapEdit(&doom, &error) && doom.doomSidedefs.size() == sides && doom.doomSidedefs.at(4).offsetX == 9
				&& !doom.doomGeometryChanged,
			"Undo should restore the sides and leave the node lumps current again.");
	}
	return ok;
}

// Regressions from review: compact lines, keys sharing a line, and copies of
// brushes whose braces share lines.
bool runEditFidelitySmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	int replaced = 0;

	// Replacing a texture on a face line that opens with a brace keeps the brace.
	const QString compactPath = root.filePath(QStringLiteral("fidelity-compact.map"));
	ok &= expect(writeFile(compactPath, compactMapFixture()), "Compact fixture should be written.");
	LevelMapDocument compact;
	ok &= expect(loadLevelMap({compactPath, {}, QStringLiteral("idtech2")}, &compact, &error), "Compact fixture should load.");
	ok &= expect(replaceLevelMapTexture(&compact, QStringLiteral("TOP"), QStringLiteral("SKY1"), false, &replaced, &error) && replaced == 1,
		"Replacing the compact brush's top texture should work.");
	const QString compactSaved = root.filePath(QStringLiteral("fidelity-compact-saved.map"));
	ok &= expect(saveLevelMapAs(compact, compactSaved).succeeded(), "Saving the compact map should work.");
	const QString compactText = QString::fromUtf8(readFile(compactSaved));
	ok &= expect(compactText.contains(QStringLiteral("{ ( 1 0 6.4e1 ) ( 0 0 6.4e1 ) ( 0 1 6.4e1 ) SKY1 0 0 0 1 1"))
			&& compactText.contains(QStringLiteral("( 0 1 0 ) ( 0 0 0 ) ( 1 0 0 ) BOTTOM 0 0 0 1 1 // trailing comment")),
		"Only the replaced name should change: the brace stays, and untouched faces keep their lines.");
	LevelMapDocument compactReloaded;
	ok &= expect(loadLevelMap({compactSaved, {}, QStringLiteral("idtech2")}, &compactReloaded, &error) && compactReloaded.brushes.size() == 1
			&& !hasIssueCode(compactReloaded, QStringLiteral("unexpected-token")),
		"The compact map should read back with its brush.");
	ok &= expect(!replaceLevelMapTexture(&compact, QStringLiteral("EAST"), QStringLiteral("{grate"), false, &replaced, &error),
		"A name the map tokenizer would split should be refused.");

	// Two keys removed from one line stay removed when only one comes back.
	const QString pairPath = root.filePath(QStringLiteral("fidelity-pairs.map"));
	ok &= expect(writeFile(pairPath, QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"trigger_relay\"\n\"origin\" \"0 0 0\"\n\"target\" \"t1\" \"killtarget\" \"k1\"\n}\n")),
		"Pair fixture should be written.");
	LevelMapDocument pairs;
	ok &= expect(loadLevelMap({pairPath, {}, QStringLiteral("idtech2")}, &pairs, &error), "Pair fixture should load.");
	ok &= expect(removeLevelMapEntityProperty(&pairs, 1, QStringLiteral("target"), &error)
			&& removeLevelMapEntityProperty(&pairs, 1, QStringLiteral("killtarget"), &error) && undoLevelMapEdit(&pairs, &error),
		"Removing both keys and undoing the second should work.");
	const QString pairSaved = root.filePath(QStringLiteral("fidelity-pairs-saved.map"));
	ok &= expect(saveLevelMapAs(pairs, pairSaved).succeeded(), "Saving the pair map should work.");
	const QString pairText = QString::fromUtf8(readFile(pairSaved));
	ok &= expect(pairText.contains(QStringLiteral("\"killtarget\" \"k1\"")) && !pairText.contains(QStringLiteral("\"target\" \"t1\"")),
		"The key still removed should stay out of the file.");

	// A brush entity whose last brace shares a line cannot be copied as text.
	const QString wallPath = root.filePath(QStringLiteral("fidelity-wall.map"));
	ok &= expect(writeFile(wallPath, QByteArrayLiteral(
				 "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_wall\"\n{\n"
				 "( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) W 0 0 0 1 1\n( 64 0 0 ) ( 64 0 1 ) ( 64 1 0 ) W 0 0 0 1 1\n"
				 "( 0 0 0 ) ( 0 0 1 ) ( 1 0 0 ) W 0 0 0 1 1\n( 0 64 0 ) ( 1 64 0 ) ( 0 64 1 ) W 0 0 0 1 1\n"
				 "( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) W 0 0 0 1 1\n( 0 0 64 ) ( 0 1 64 ) ( 1 0 64 ) W 0 0 0 1 1 } }\n")),
		"Wall fixture should be written.");
	LevelMapDocument wall;
	ok &= expect(loadLevelMap({wallPath, {}, QStringLiteral("idtech2")}, &wall, &error), "Wall fixture should load.");
	ok &= expect(!duplicateLevelMapObjects(&wall, {{LevelMapSelectionKind::Entity, 1}}, 64.0, 0.0, 0.0, &error) && error.contains(QStringLiteral("shares")),
		"Duplicating a brush whose brace shares a line should be refused.");
	ok &= expect(setLevelMapSelection(&wall, {{LevelMapSelectionKind::Entity, 1}}, &error) && levelMapSelectionText(wall, &error).isEmpty()
			&& error.contains(QStringLiteral("shares")),
		"Copying it as text should be refused with the reason.");
	ok &= expect(!pasteLevelMapText(&wall, QString::fromUtf8(readFile(wallPath)).section(QStringLiteral("}\n"), 1), &error),
		"Pasting text whose brush brace shares a line should be refused.");
	return ok;
}

bool runAddBrushSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	// Each map style gets a box in its own face format that reads back as a
	// closed brush with the bounds asked for.
	struct Case {
		const char* file;
		QByteArray text;
		const char* hint;
		const char* marker;
	};
	const QVector<Case> cases {
		{"brush-classic.map", editableMapFixture(), "idtech2", "( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) NEWTEX 0 0 0 1 1"},
		{"brush-valve.map", valve220MapFixture(), "idtech2", "( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) NEWTEX [ 1 0 0 0 ] [ 0 -1 0 0 ] 0 1 1"},
		{"brush-q3.map", quake3MapFixture(), "idtech3", "( 1 0 64 ) ( 0 0 64 ) ( 0 1 64 ) ( ( 0.0078125 0 0 ) ( 0 0.0078125 0 ) ) NEWTEX 0 0 0"},
	};
	for (const Case& entry : cases) {
		const QString path = root.filePath(QString::fromLatin1(entry.file));
		ok &= expect(writeFile(path, entry.text), "Brush fixture should be written.");
		LevelMapDocument document;
		ok &= expect(loadLevelMap({path, {}, QString::fromLatin1(entry.hint)}, &document, &error), "Brush fixture should load.");
		const QString original = document.originalText;
		const int brushes = static_cast<int>(document.brushes.size());
		int added = -1;
		ok &= expect(addLevelMapBoxBrush(&document, {0.0, 0.0, 0.0, true}, {64.0, 32.0, 64.0, true}, QStringLiteral("NEWTEX"), &added, &error),
			"Adding a box brush should work.");
		ok &= expect(document.selectionKind == LevelMapSelectionKind::QuakeBrush && document.selectedObjectId == added,
			"The new brush should be selected.");
		const QString savedPath = root.filePath(QString::fromLatin1(entry.file) + QStringLiteral(".saved.map"));
		ok &= expect(saveLevelMapAs(document, savedPath).succeeded(), "Saving the new brush should work.");
		const QString savedText = QString::fromUtf8(readFile(savedPath));
		ok &= expect(savedText.contains(QString::fromLatin1(entry.marker)), "The new brush should be written in the map's own face format.");
		LevelMapDocument reloaded;
		ok &= expect(loadLevelMap({savedPath, {}, QString::fromLatin1(entry.hint)}, &reloaded, &error) && reloaded.brushes.size() == brushes + 1,
			"The saved map should hold the new brush.");
		// The new brush joins worldspawn, so it is found by its texture rather
		// than by its place in the file.
		const auto found = std::find_if(reloaded.brushes.cbegin(), reloaded.brushes.cend(), [](const LevelMapBrush& brush) {
			return brush.textureNames.contains(QStringLiteral("NEWTEX"));
		});
		ok &= expect(found != reloaded.brushes.cend() && found->boundsSolved && nearly(found->mins.x, 0.0) && nearly(found->maxs.x, 64.0)
				&& nearly(found->maxs.y, 32.0) && nearly(found->maxs.z, 64.0),
			"The new brush should close into the box asked for.");
		ok &= expect(undoLevelMapEdit(&document, &error), "Adding the brush should undo.");
		const QString restoredPath = root.filePath(QString::fromLatin1(entry.file) + QStringLiteral(".restored.map"));
		ok &= expect(saveLevelMapAs(document, restoredPath).succeeded() && QString::fromUtf8(readFile(restoredPath)) == original,
			"Undo should give back the map exactly.");
	}
	LevelMapDocument flat;
	const QString flatPath = root.filePath(QStringLiteral("brush-flat.map"));
	ok &= expect(writeFile(flatPath, editableMapFixture()) && loadLevelMap({flatPath, {}, QStringLiteral("idtech2")}, &flat, &error),
		"Flat-brush fixture should load.");
	ok &= expect(!addLevelMapBoxBrush(&flat, {0.0, 0.0, 0.0, true}, {64.0, 64.0, 0.0, true}, QStringLiteral("NEWTEX"), nullptr, &error),
		"A brush with no height should be refused.");
	return ok;
}

bool runUndoRedoSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("undo.map"));
	ok &= expect(writeFile(path, quakeMapFixture()), "Undo fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Undo fixture should load.");
	ok &= expect(document.editState == QStringLiteral("clean"), "A freshly loaded map should be clean.");

	// Defect 2: two moves then two undos must leave two redo entries.
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("entity"), 1, 8.0, 0.0, 0.0, &error), "First move should work.");
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("entity"), 1, 8.0, 0.0, 0.0, &error), "Second move should work.");
	ok &= expect(document.undoStack.size() == 2, "Two moves should push two undo commands.");
	ok &= expect(undoLevelMapEdit(&document, &error), "First undo should work.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Second undo should work.");
	ok &= expect(document.undoStack.isEmpty(), "Undo should drain the undo stack.");
	ok &= expect(document.redoStack.size() == 2, "Undoing twice should leave two redo commands.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("32 32 64"), "Undo should restore the original origin.");
	ok &= expect(redoLevelMapEdit(&document, &error) && redoLevelMapEdit(&document, &error), "Both redo steps should work.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("48 32 64"), "Redo should reapply both moves.");
	ok &= expect(document.redoStack.isEmpty() && document.undoStack.size() == 2, "Redo should move commands back onto the undo stack.");

	// Defect 3: undoing a brand new key removes it instead of blanking it.
	ok &= expect(setLevelMapEntityProperty(&document, 1, QStringLiteral("wait"), QStringLiteral("2"), &error), "Adding a new key should work.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("wait")) == QStringLiteral("2"), "New key should be set.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Undo of a new key should work.");
	bool hasWait = false;
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id != 1) {
			continue;
		}
		for (const LevelMapProperty& property : entity.properties) {
			hasWait = hasWait || property.key.compare(QStringLiteral("wait"), Qt::CaseInsensitive) == 0;
		}
	}
	ok &= expect(!hasWait, "Undo of a previously-absent key should remove the key entirely.");

	// Defect 4: a move on an entity without an origin key must not leave one
	// behind. A brush entity, worldspawn here, moves through its brushes and
	// gains no origin at all; a point entity gains one, which undo removes.
	const double worldX = document.brushes.first().faces.first().p0.x;
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("entity"), 0, 4.0, 4.0, 4.0, &error), "Move on worldspawn should work.");
	ok &= expect(propertyValueForTest(document, 0, QStringLiteral("origin")).isEmpty() && nearly(document.brushes.first().faces.first().p0.x, worldX + 4.0),
		"Moving worldspawn should move its brush and give it no origin.");
	ok &= expect(undoLevelMapEdit(&document, &error) && nearly(document.brushes.first().faces.first().p0.x, worldX), "The worldspawn move should undo.");
	ok &= expect(removeLevelMapEntityProperty(&document, 1, QStringLiteral("origin"), &error), "Removing the light's origin should work.");
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("entity"), 1, 4.0, 4.0, 4.0, &error), "Move on a point entity without an origin should work.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("4 4 4"), "Move should synthesize an origin.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Undo of a synthesizing move should work.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")).isEmpty(), "Undo should remove a synthesized origin key.");
	ok &= expect(!document.entities.at(1).origin.valid, "Undo should clear the synthesized origin vector.");

	// Defect 5: clean / modified / saved across save-then-undo-then-redo.
	ok &= expect(document.editState == QStringLiteral("modified"), "Pending edits should read as modified.");
	markLevelMapSaved(&document);
	ok &= expect(document.editState == QStringLiteral("saved"), "A saved document should read as saved.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Undo after save should work.");
	ok &= expect(document.editState == QStringLiteral("modified"), "Undoing past a save should read as modified.");
	ok &= expect(redoLevelMapEdit(&document, &error), "Redo back to the save point should work.");
	ok &= expect(document.editState == QStringLiteral("saved"), "Redoing back to the save point should read as saved again.");

	// A new edit made after undoing past the save point clears the redo branch,
	// destroying the saved command. The save point is then unreachable and the
	// document must not read back as saved.
	ok &= expect(undoLevelMapEdit(&document, &error), "Undo before a divergent edit should work.");
	ok &= expect(document.editState == QStringLiteral("modified"), "Undoing past the save point should read as modified.");
	ok &= expect(setLevelMapEntityProperty(&document, 1, QStringLiteral("wait"), QStringLiteral("7"), &error), "A divergent edit should work.");
	ok &= expect(document.savedUndoDepth < 0, "An edit that discards the redo branch should invalidate the saved depth.");
	ok &= expect(document.editState == QStringLiteral("modified"), "A document holding a divergent unsaved edit must not read as saved.");

	// Defect 6: the undo stack is bounded. Re-establish a save point so that the
	// trim below is what invalidates it.
	markLevelMapSaved(&document);
	ok &= expect(document.savedUndoDepth >= 0, "Saving again should record a reachable save point.");
	document.undoLimit = 5;
	for (int index = 0; index < 12; ++index) {
		ok &= expect(setLevelMapEntityProperty(&document, 1, QStringLiteral("wait"), QString::number(index), &error), "Repeated edits should work.");
	}
	ok &= expect(document.undoStack.size() == 5, "The undo stack should be bounded to the configured depth.");
	ok &= expect(document.savedUndoDepth < 0, "Dropping the saved entry should invalidate the saved depth.");
	return ok;
}

bool runDoomMapSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString sourcePath = root.filePath(QStringLiteral("doom.wad"));
	ok &= expect(writeFile(sourcePath, wadFixture()), "Doom WAD fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({sourcePath, QStringLiteral("MAP01"), QStringLiteral("idtech1")}, &document, &error), "Doom WAD map should load.");
	ok &= expect(document.format == LevelMapFormat::DoomWad, "Doom WAD format mismatch.");
	ok &= expect(document.doomFormat == LevelMapDoomFormat::Doom, "Doom WAD should report the Doom lump layout.");
	ok &= expect(document.mapName == QStringLiteral("MAP01"), "Doom WAD map marker mismatch.");
	ok &= expect(document.doomVertices.size() == 4 && document.doomLinedefs.size() == 4, "Doom WAD geometry should parse.");
	ok &= expect(document.entities.size() == 1 && document.doomThings.size() == 1, "Doom things should be inspectable as entities.");
	ok &= expect(levelMapTextureLines(document).join('\n').contains(QStringLiteral("WALLMID")), "Doom WAD should expose wall textures.");
	ok &= expect(document.doomSidedefs.at(0).offsetX == 4 && document.doomSidedefs.at(0).offsetY == 8, "Sidedef texture offsets should parse.");
	ok &= expect(levelMapSidedefLines(document).join('\n').contains(QStringLiteral("offset=4,8")), "Sidedef reporting should surface offsets.");
	ok &= expect(levelMapSectorLines(document).join('\n').contains(QStringLiteral("light=160")), "Sector reporting should surface the light level.");
	ok &= expect(!hasIssueCode(document, QStringLiteral("linedef-invalid-sidedef")), "frontSidedef 0xffff handling should not produce spurious errors.");
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("vertex:0"), &error), "Doom vertex selection should work.");
	ok &= expect(levelMapPropertyLines(document).join('\n').contains(QStringLiteral("Position")), "Doom vertex property inspector should show position.");
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("vertex"), 0, 8.0, 4.0, 0.0, &error), "Doom vertex move should work.");
	ok &= expect(std::lround(document.doomVertices.at(0).x) == 8 && std::lround(document.doomVertices.at(0).y) == 4, "Doom vertex should move.");
	ok &= expect(document.doomGeometryChanged, "A vertex move should flag the node lumps as stale.");
	ok &= expect(setLevelMapEntityProperty(&document, 0, QStringLiteral("type"), QStringLiteral("3004"), &error), "Doom thing entity type edit should work.");
	ok &= expect(document.doomThings.at(0).type == 3004, "Doom thing type should sync from entity edit.");

	// Defect 1: undoing a thing property edit must revert the thing record too.
	ok &= expect(undoLevelMapEdit(&document, &error), "Doom thing edit undo should work.");
	ok &= expect(document.doomThings.at(0).type == 1, "Undo should revert the Doom thing record, not just the entity key.");
	ok &= expect(propertyValueForTest(document, 0, QStringLiteral("type")) == QStringLiteral("1"), "Undo should revert the mirrored entity key.");
	ok &= expect(redoLevelMapEdit(&document, &error), "Doom thing edit redo should work.");
	ok &= expect(document.doomThings.at(0).type == 3004, "Redo should reapply the thing record change.");

	// Sector and sidedef editing.
	ok &= expect(setLevelMapSectorProperty(&document, 0, QStringLiteral("floorheight"), QStringLiteral("32"), &error), "Sector floor edit should work.");
	ok &= expect(setLevelMapSectorProperty(&document, 0, QStringLiteral("lightlevel"), QStringLiteral("96"), &error), "Sector light edit should work.");
	ok &= expect(document.doomSectors.at(0).floorHeight == 32 && document.doomSectors.at(0).lightLevel == 96, "Sector edits should apply.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Sector edit undo should work.");
	ok &= expect(document.doomSectors.at(0).lightLevel == 160, "Sector edit undo should restore the light level.");
	ok &= expect(redoLevelMapEdit(&document, &error), "Sector edit redo should work.");
	ok &= expect(document.doomSectors.at(0).lightLevel == 96, "Sector edit redo should reapply.");
	ok &= expect(setLevelMapSidedefProperty(&document, 0, QStringLiteral("middle"), QStringLiteral("NEWMID"), &error), "Sidedef texture edit should work.");
	ok &= expect(setLevelMapSidedefProperty(&document, 0, QStringLiteral("offsetx"), QStringLiteral("24"), &error), "Sidedef offset edit should work.");
	ok &= expect(document.doomSidedefs.at(0).middleTexture == QStringLiteral("NEWMID") && document.doomSidedefs.at(0).offsetX == 24, "Sidedef edits should apply.");
	ok &= expect(undoLevelMapEdit(&document, &error) && document.doomSidedefs.at(0).offsetX == 4, "Sidedef edit undo should restore the offset.");
	ok &= expect(redoLevelMapEdit(&document, &error) && document.doomSidedefs.at(0).offsetX == 24, "Sidedef edit redo should reapply the offset.");

	const QString outputPath = root.filePath(QStringLiteral("doom-edited.wad"));
	const LevelMapSaveReport save = saveLevelMapAs(document, outputPath);
	ok &= expect(save.succeeded() && QFileInfo::exists(outputPath), "Doom save-as should write.");
	ok &= expect(save.staleLumps.contains(QStringLiteral("NODES")) && save.staleLumps.contains(QStringLiteral("BLOCKMAP")), "Save should list stale node lumps.");
	ok &= expect(save.warnings.join('\n').contains(QStringLiteral("NODES")), "Save should warn that a node build is required.");
	ok &= expect(readFile(outputPath).left(4) == QByteArray("PWAD"), "A PWAD source should stay a PWAD.");

	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({outputPath, QStringLiteral("MAP01"), QStringLiteral("idtech1")}, &reloaded, &error), "Edited Doom WAD should reload.");
	ok &= expect(std::lround(reloaded.doomVertices.at(0).x) == 8 && std::lround(reloaded.doomVertices.at(0).y) == 4, "Reloaded Doom WAD should persist moved vertex.");
	ok &= expect(reloaded.doomThings.at(0).type == 3004, "Reloaded Doom WAD should persist thing type edit.");
	ok &= expect(reloaded.doomSectors.at(0).floorHeight == 32 && reloaded.doomSectors.at(0).lightLevel == 96, "Reloaded Doom WAD should persist sector edits.");
	ok &= expect(reloaded.doomSidedefs.at(0).middleTexture == QStringLiteral("NEWMID") && reloaded.doomSidedefs.at(0).offsetX == 24, "Reloaded Doom WAD should persist sidedef edits.");
	const CompilerCommandRequest request = compilerRequestForLevelMap(reloaded, QString(), QString());
	ok &= expect(request.profileId == QStringLiteral("zdbsp-nodes"), "Doom map should default to zdbsp-nodes compile plan.");

	// Coordinate clamping.
	LevelMapDocument overflow = document;
	overflow.doomVertices[0].x = 70000.0;
	const LevelMapSaveReport overflowSave = saveLevelMapAs(overflow, root.filePath(QStringLiteral("doom-overflow.wad")));
	ok &= expect(!overflowSave.succeeded(), "Out-of-range coordinates should fail the save.");
	ok &= expect(overflowSave.errors.join('\n').contains(QStringLiteral("16-bit")), "Out-of-range coordinates should raise a specific error.");
	return ok;
}

bool runDoomIwadSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString sourcePath = root.filePath(QStringLiteral("base.wad"));
	QVector<Lump> lumps = {{"PLAYPAL", "palette"}};
	lumps += classicMapLumps("E1M10");
	ok &= expect(writeFile(sourcePath, buildWad("IWAD", lumps)), "IWAD fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({sourcePath, QString(), QStringLiteral("idtech1")}, &document, &error), "IWAD map should load.");
	ok &= expect(document.mapName == QStringLiteral("E1M10"), "E1M10 should be recognised as a map marker.");
	ok &= expect(document.doomWadMagic == QStringLiteral("IWAD"), "The source WAD magic should be recorded.");

	// The map list skips lumps that are not maps and keeps directory order.
	const QString listPath = root.filePath(QStringLiteral("episode.wad"));
	QVector<Lump> episode = {{"PLAYPAL", "palette"}};
	episode += classicMapLumps("E1M2");
	episode += classicMapLumps("E1M1");
	episode += classicMapLumps("HUB01");
	ok &= expect(writeFile(listPath, buildWad("PWAD", episode)), "Episode fixture should be written.");
	ok &= expect(levelMapNamesInWad(listPath) == QStringList({QStringLiteral("E1M2"), QStringLiteral("E1M1"), QStringLiteral("HUB01")}),
		"levelMapNamesInWad should list every map marker in directory order.");
	QString listError;
	ok &= expect(levelMapNamesInWad(root.filePath(QStringLiteral("absent.wad")), &listError).isEmpty() && !listError.isEmpty(),
		"Listing a missing WAD should report an error.");
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("thing"), 0, 8.0, 0.0, 0.0, &error), "Thing move should work.");

	const QString outputPath = root.filePath(QStringLiteral("base-edited.wad"));
	const LevelMapSaveReport save = saveLevelMapAs(document, outputPath);
	ok &= expect(save.succeeded(), "IWAD save should succeed.");
	ok &= expect(readFile(outputPath).left(4) == QByteArray("IWAD"), "An IWAD source should stay an IWAD.");
	return ok;
}

bool runEmptyWadDirectorySmoke(const QDir& root)
{
	// A WAD header declaring zero lumps passes every bounds check: the directory
	// offset is in range and offset + 0 * 16 never exceeds the file. The reader
	// used to return an empty list without setting an error, so loadLevelMap
	// failed with nothing at all to tell the user. Found by the parser fuzz
	// harness on several seeds.
	bool ok = true;
	QByteArray wad("PWAD", 4);
	const auto appendLe32 = [&wad](qint32 value) {
		for (int shift = 0; shift < 32; shift += 8) {
			wad.append(static_cast<char>((static_cast<quint32>(value) >> shift) & 0xffu));
		}
	};
	appendLe32(0);   // lump count
	appendLe32(12);  // directory offset, immediately after the header

	const QString sourcePath = root.filePath(QStringLiteral("empty-directory.wad"));
	ok &= expect(writeFile(sourcePath, wad), "Empty-directory WAD fixture should be written.");

	LevelMapDocument document;
	QString error;
	ok &= expect(!loadLevelMap({sourcePath, QString(), QStringLiteral("idtech1")}, &document, &error),
		"A WAD with no lumps should not load.");
	ok &= expect(!error.trimmed().isEmpty(),
		"A WAD with no lumps must fail with a reason, not silently.");
	return ok;
}

bool runDoomMarkerSmoke(const QDir& root)
{
	bool ok = true;
	QString error;

	const QString map100Path = root.filePath(QStringLiteral("map100.wad"));
	ok &= expect(writeFile(map100Path, buildWad("PWAD", classicMapLumps("MAP100"))), "MAP100 fixture should be written.");
	LevelMapDocument map100;
	ok &= expect(loadLevelMap({map100Path, QString(), QStringLiteral("idtech1")}, &map100, &error), "MAP100 should load.");
	ok &= expect(map100.mapName == QStringLiteral("MAP100"), "MAP100 should be recognised as a map marker.");

	const QString namedPath = root.filePath(QStringLiteral("named.wad"));
	ok &= expect(writeFile(namedPath, buildWad("PWAD", classicMapLumps("HUB01"))), "Named marker fixture should be written.");
	LevelMapDocument named;
	ok &= expect(loadLevelMap({namedPath, QString(), QStringLiteral("idtech1")}, &named, &error), "A named ZDoom map marker should load.");
	ok &= expect(named.mapName == QStringLiteral("HUB01"), "Named marker should be used as the map name.");
	ok &= expect(named.doomLinedefs.size() == 4, "Named marker map should parse its geometry.");
	return ok;
}

bool runHexenSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("hexen.wad"));
	ok &= expect(writeFile(path, hexenWadFixture()), "Hexen fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, QStringLiteral("MAP02"), QStringLiteral("idtech1")}, &document, &error), "Hexen map should load.");
	ok &= expect(document.doomFormat == LevelMapDoomFormat::Hexen, "BEHAVIOR should select the Hexen layout.");
	ok &= expect(hasIssueCode(document, QStringLiteral("hexen-map-format")), "Hexen detection should be reported.");
	ok &= expect(document.doomLinedefs.size() == 4, "Hexen linedefs should use the 16-byte stride.");
	ok &= expect(document.doomThings.size() == 1, "Hexen things should use the 20-byte stride.");

	const LevelMapDoomLinedef& linedef = document.doomLinedefs.at(0);
	ok &= expect(linedef.special == 12, "Hexen linedef special should be a single byte.");
	ok &= expect(linedef.args[0] == 10 && linedef.args[4] == 14, "Hexen linedef args should parse.");
	ok &= expect(linedef.frontSidedef == 0 && linedef.backSidedef == -1, "Hexen linedef sidedefs should parse and normalise.");

	const LevelMapDoomThing& thing = document.doomThings.at(0);
	ok &= expect(thing.tid == 17, "Hexen thing id should parse.");
	ok &= expect(std::lround(thing.x) == 64 && std::lround(thing.y) == -48, "Hexen thing position should parse.");
	ok &= expect(std::lround(thing.z) == 24, "Hexen thing height should parse.");
	ok &= expect(thing.type == 1 && thing.angle == 270, "Hexen thing type/angle should parse.");
	ok &= expect(thing.special == 80 && thing.args[4] == 5, "Hexen thing special and args should parse.");

	ok &= expect(moveLevelMapObject(&document, QStringLiteral("thing"), 0, 16.0, 0.0, 0.0, &error), "Hexen thing move should work.");
	const QString outputPath = root.filePath(QStringLiteral("hexen-edited.wad"));
	const LevelMapSaveReport save = saveLevelMapAs(document, outputPath);
	ok &= expect(save.succeeded(), "Hexen save should succeed.");

	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({outputPath, QStringLiteral("MAP02"), QStringLiteral("idtech1")}, &reloaded, &error), "Edited Hexen WAD should reload.");
	ok &= expect(reloaded.doomFormat == LevelMapDoomFormat::Hexen, "Reloaded Hexen WAD should stay Hexen.");
	ok &= expect(reloaded.doomThings.size() == 1 && std::lround(reloaded.doomThings.at(0).x) == 80, "Hexen save should keep the 20-byte thing stride.");
	ok &= expect(reloaded.doomThings.at(0).tid == 17 && reloaded.doomThings.at(0).args[4] == 5, "Hexen save should preserve tid and args.");
	ok &= expect(reloaded.doomLinedefs.at(0).args[0] == 10, "Hexen save should preserve linedef args.");
	return ok;
}

bool runUdmfSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("udmf.wad"));
	ok &= expect(writeFile(path, udmfWadFixture()), "UDMF fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, QStringLiteral("MAP03"), QStringLiteral("idtech1")}, &document, &error), "UDMF map should be detected without failing the load.");
	ok &= expect(document.doomFormat == LevelMapDoomFormat::Udmf, "TEXTMAP should select the UDMF format.");
	ok &= expect(!hasIssueCode(document, QStringLiteral("udmf-unsupported")), "UDMF should load into the authoring document.");
	ok &= expect(document.doomLinedefs.isEmpty() && document.doomVertices.size() == 1, "UDMF should project the declared vertex.");
	ok &= expect(document.doomLumps.contains(QStringLiteral("TEXTMAP")), "TEXTMAP should survive for save-back.");
	ok &= expect(document.doomLumps.contains(QStringLiteral("ENDMAP")), "ENDMAP should survive for save-back.");
	ok &= expect(document.doomLumps.contains(QStringLiteral("ZNODES")), "ZNODES should survive for save-back.");

	const QString outputPath = root.filePath(QStringLiteral("udmf-copy.wad"));
	const LevelMapSaveReport save = saveLevelMapAs(document, outputPath);
	ok &= expect(save.succeeded(), "UDMF pass-through save should succeed.");
	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({outputPath, QStringLiteral("MAP03"), QStringLiteral("idtech1")}, &reloaded, &error), "UDMF copy should reload.");
	ok &= expect(reloaded.doomLumps.value(QStringLiteral("TEXTMAP")) == document.doomLumps.value(QStringLiteral("TEXTMAP")), "UDMF TEXTMAP should round-trip unchanged.");
	return ok;
}

bool runDoomValidationSmoke(const QDir& root)
{
	bool ok = true;
	QString error;

	QByteArray vertices;
	// Vertex 3 duplicates vertex 2 and vertex 4 sits on vertex 0.
	for (const QPoint& point : {QPoint(0, 0), QPoint(128, 0), QPoint(128, 128), QPoint(128, 128), QPoint(0, 0)}) {
		appendLe16(&vertices, static_cast<qint16>(point.x()));
		appendLe16(&vertices, static_cast<qint16>(point.y()));
	}

	QByteArray linedefs;
	// Linedef 0: zero length and flagged two-sided without a back sidedef.
	appendLe16(&linedefs, 2);
	appendLe16(&linedefs, 3);
	appendLe16(&linedefs, 4); // ML_TWOSIDED
	appendLe16(&linedefs, 0);
	appendLe16(&linedefs, 0);
	appendLe16(&linedefs, 0);
	appendLe16(&linedefs, -1);
	// Linedef 1: one-sided with no middle texture, on sidedef 1.
	appendLe16(&linedefs, 0);
	appendLe16(&linedefs, 1);
	appendLe16(&linedefs, 0);
	appendLe16(&linedefs, 0);
	appendLe16(&linedefs, 0);
	appendLe16(&linedefs, 1);
	appendLe16(&linedefs, -1);

	QByteArray sidedefs;
	// Sidedef 0 has a middle texture, sidedef 1 has none.
	appendLe16(&sidedefs, 0);
	appendLe16(&sidedefs, 0);
	sidedefs.append(fixedName("-", 8));
	sidedefs.append(fixedName("-", 8));
	sidedefs.append(fixedName("WALLMID", 8));
	appendLe16(&sidedefs, 0);
	appendLe16(&sidedefs, 0);
	appendLe16(&sidedefs, 0);
	sidedefs.append(fixedName("-", 8));
	sidedefs.append(fixedName("-", 8));
	sidedefs.append(fixedName("-", 8));
	appendLe16(&sidedefs, 9); // out-of-range sector

	QByteArray sectors = doomSectors();
	sectors.append(doomSectors()); // sector 1 is referenced by nothing

	QByteArray things;
	appendLe16(&things, 0);
	appendLe16(&things, 0);
	appendLe16(&things, 0);
	appendLe16(&things, 3004); // no player 1 start
	appendLe16(&things, 7);
	things.append('\0'); // trailing byte

	const QVector<Lump> lumps = {
		{"MAP07", {}},
		{"THINGS", things},
		{"LINEDEFS", linedefs},
		{"SIDEDEFS", sidedefs},
		{"VERTEXES", vertices},
		{"SECTORS", sectors},
	};
	const QString path = root.filePath(QStringLiteral("validate.wad"));
	ok &= expect(writeFile(path, buildWad("PWAD", lumps)), "Validation fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, QStringLiteral("MAP07"), QStringLiteral("idtech1")}, &document, &error), "Validation fixture should load.");
	ok &= expect(hasIssueCode(document, QStringLiteral("linedef-zero-length")), "Zero-length linedefs should be reported.");
	ok &= expect(hasIssueCode(document, QStringLiteral("linedef-twosided-no-back")), "Two-sided linedefs without a back sidedef should be reported.");
	ok &= expect(hasIssueCode(document, QStringLiteral("linedef-missing-middle")), "One-sided linedefs without a middle texture should be reported.");
	ok &= expect(hasIssueCode(document, QStringLiteral("sidedef-invalid-sector")), "Out-of-range sidedef sectors should be reported.");
	ok &= expect(hasIssueCode(document, QStringLiteral("sector-no-linedefs")), "Unreferenced sectors should be reported.");
	ok &= expect(hasIssueCode(document, QStringLiteral("missing-player-start")), "A missing player 1 start should be reported.");
	ok &= expect(hasIssueCode(document, QStringLiteral("duplicate-vertex")), "Duplicate vertices should be reported.");
	ok &= expect(hasIssueCode(document, QStringLiteral("trailing-thing-bytes")), "Trailing THINGS bytes should be reported.");
	ok &= expect(levelMapReportText(document).contains(QStringLiteral("Sidedefs:")), "The report should surface sidedef data.");
	return ok;
}

// Grid snapping is a core concern so that a viewport drag, an arrow-key nudge
// and a CLI move all agree. Negative coordinates matter: Doom and Quake maps
// straddle the origin.
bool runGridSnapSmoke()
{
	bool ok = true;
	ok &= expect(nearly(snapLevelMapCoordinate(0.0, 16.0), 0.0), "Zero should snap to zero.");
	ok &= expect(nearly(snapLevelMapCoordinate(7.0, 16.0), 0.0), "Below half a grid step should snap down.");
	ok &= expect(nearly(snapLevelMapCoordinate(9.0, 16.0), 16.0), "Above half a grid step should snap up.");
	ok &= expect(nearly(snapLevelMapCoordinate(8.0, 16.0), 16.0), "A tie should snap away from zero.");
	ok &= expect(nearly(snapLevelMapCoordinate(-8.0, 16.0), -16.0), "A negative tie should snap away from zero.");
	ok &= expect(nearly(snapLevelMapCoordinate(-24.0, 16.0), -32.0), "Negative coordinates should mirror positive ones.");
	ok &= expect(nearly(snapLevelMapCoordinate(24.0, 16.0), 32.0), "Positive coordinates should mirror negative ones.");
	ok &= expect(nearly(snapLevelMapCoordinate(-7.0, 16.0), 0.0), "A small negative offset should snap to the origin.");
	ok &= expect(nearly(snapLevelMapCoordinate(100.5, 1.0), 101.0), "A one-unit grid should round to whole units.");
	ok &= expect(nearly(snapLevelMapCoordinate(33.0, 64.0), 64.0), "A coarse grid should still round to the nearest line.");
	ok &= expect(nearly(snapLevelMapCoordinate(-33.0, 64.0), -64.0), "A coarse grid should round negatives symmetrically.");
	ok &= expect(nearly(snapLevelMapCoordinate(-31.0, 64.0), 0.0), "Just under half a coarse step should snap to zero.");
	ok &= expect(nearly(snapLevelMapCoordinate(129.0, 128.0), 128.0), "A 128-unit grid should snap 129 back to 128.");
	ok &= expect(nearly(snapLevelMapCoordinate(37.0, 0.0), 37.0), "A zero grid should disable snapping.");
	ok &= expect(nearly(snapLevelMapCoordinate(37.0, -16.0), 37.0), "A negative grid should disable snapping.");

	const LevelMapVec3 position {-24.0, 9.0, -7.0, true};
	const LevelMapVec3 snapped = snapLevelMapPosition(position, 16.0);
	ok &= expect(snapped.valid, "Snapping a valid position should stay valid.");
	ok &= expect(nearly(snapped.x, -32.0) && nearly(snapped.y, 16.0) && nearly(snapped.z, 0.0),
		"Every component of a position should snap independently.");
	const LevelMapVec3 delta = snapLevelMapDelta({40.0, -40.0, 0.0, true}, 32.0);
	ok &= expect(nearly(delta.x, 32.0) && nearly(delta.y, -32.0) && nearly(delta.z, 0.0),
		"A delta should snap the same way a position does.");
	ok &= expect(!snapLevelMapPosition(LevelMapVec3(), 16.0).valid, "Snapping an invalid vector should keep it invalid.");
	return ok;
}

bool runSelectionSetSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("selection.map"));
	ok &= expect(writeFile(path, quakeMapFixture()), "Selection fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Selection fixture should load.");
	ok &= expect(levelMapSelectionCount(document) == 0, "A freshly loaded map should have an empty selection set.");

	// Single selection still drives the set, and the set still drives the
	// primary fields.
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("entity:1"), &error), "Selecting an entity should work.");
	ok &= expect(levelMapSelectionCount(document) == 1, "A single selection should hold exactly one member.");
	ok &= expect(levelMapSelectionContains(document, LevelMapSelectionKind::Entity, 1), "The selected entity should be in the set.");
	ok &= expect(document.selectionKind == LevelMapSelectionKind::Entity && document.selectedObjectId == 1,
		"The primary fields should mirror the single member.");

	ok &= expect(addLevelMapSelection(&document, LevelMapSelectionKind::QuakeBrush, 0, &error), "Adding a brush should work.");
	ok &= expect(levelMapSelectionCount(document) == 2, "Adding should grow the set.");
	ok &= expect(document.selectionKind == LevelMapSelectionKind::QuakeBrush && document.selectedObjectId == 0,
		"The newest member should become primary.");
	ok &= expect(document.entities.at(1).selected && document.brushes.at(0).selected,
		"Every member of the set should carry its selected flag.");

	// Re-adding promotes instead of duplicating.
	ok &= expect(addLevelMapSelection(&document, LevelMapSelectionKind::Entity, 1, &error), "Re-adding a member should work.");
	ok &= expect(levelMapSelectionCount(document) == 2, "Re-adding a member should not duplicate it.");
	ok &= expect(document.selectionKind == LevelMapSelectionKind::Entity && document.selectedObjectId == 1,
		"Re-adding a member should promote it to primary.");

	ok &= expect(toggleLevelMapSelection(&document, LevelMapSelectionKind::QuakeBrush, 0, &error), "Toggling off should work.");
	ok &= expect(levelMapSelectionCount(document) == 1 && !document.brushes.at(0).selected,
		"Toggling a member off should drop it and clear its flag.");
	ok &= expect(document.selectionKind == LevelMapSelectionKind::Entity && document.selectedObjectId == 1,
		"Dropping the primary should promote the remaining member.");
	ok &= expect(toggleLevelMapSelection(&document, LevelMapSelectionKind::QuakeBrush, 0, &error), "Toggling on should work.");
	ok &= expect(levelMapSelectionCount(document) == 2, "Toggling a missing member on should add it.");

	ok &= expect(removeLevelMapSelection(&document, LevelMapSelectionKind::Entity, 1, &error), "Removing a member should work.");
	ok &= expect(levelMapSelectionCount(document) == 1 && !document.entities.at(1).selected,
		"Removing should shrink the set and clear the flag.");
	ok &= expect(!removeLevelMapSelection(&document, LevelMapSelectionKind::Entity, 1, &error),
		"Removing a member twice should fail.");
	ok &= expect(!addLevelMapSelection(&document, LevelMapSelectionKind::QuakeBrush, 99, &error),
		"Adding a missing object should fail.");
	ok &= expect(levelMapSelectionCount(document) == 1, "A failed add should leave the set alone.");

	clearLevelMapSelection(&document);
	ok &= expect(levelMapSelectionCount(document) == 0, "Clearing should empty the set.");
	ok &= expect(document.selectionKind == LevelMapSelectionKind::None && document.selectedObjectId == -1,
		"Clearing should reset the primary fields.");
	ok &= expect(!document.brushes.at(0).selected, "Clearing should clear every selected flag.");

	// Duplicates collapse to the caller's last occurrence, which becomes primary.
	const QVector<LevelMapSelectionRef> requested = {
		{LevelMapSelectionKind::QuakeBrush, 0},
		{LevelMapSelectionKind::Entity, 1},
		{LevelMapSelectionKind::QuakeBrush, 0},
	};
	ok &= expect(setLevelMapSelection(&document, requested, &error), "Setting a whole selection should work.");
	ok &= expect(levelMapSelectionCount(document) == 2, "Duplicates should collapse.");
	ok &= expect(document.selectionKind == LevelMapSelectionKind::QuakeBrush && document.selectedObjectId == 0,
		"The last occurrence should be primary.");
	ok &= expect(document.selection == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::QuakeBrush, 0}},
		"Members should keep the order of their last occurrences.");
	ok &= expect(document.entities.at(1).selected && document.brushes.at(0).selected && !document.entities.at(0).selected,
		"Exactly the members should carry the selected flag.");
	ok &= expect(levelMapSelectionSetLines(document).size() >= 2, "The selection set should report itself.");
	ok &= expect(levelMapSelectionLines(document).join(QStringLiteral("\n")).contains(QStringLiteral("Selection set")),
		"A multi-selection should be visible in the selection report.");

	// A member that does not exist is skipped, and the call says so.
	const QVector<LevelMapSelectionRef> partly = {
		{LevelMapSelectionKind::Entity, 1},
		{LevelMapSelectionKind::Entity, 42},
	};
	ok &= expect(!setLevelMapSelection(&document, partly, &error), "Setting a selection with a missing object should fail.");
	ok &= expect(levelMapSelectionCount(document) == 1, "The members that do exist should still be selected.");
	ok &= expect(!setLevelMapSelection(&document, {{LevelMapSelectionKind::None, 0}, {LevelMapSelectionKind::Entity, -1}}, &error)
			&& levelMapSelectionCount(document) == 0,
		"References to no object should be skipped, and the call say so.");

	// Single selection collapses the set again, and a failed selection empties it.
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("brush:0"), &error), "Selecting a brush should work.");
	ok &= expect(levelMapSelectionCount(document) == 1, "A single selection should replace the whole set.");
	ok &= expect(!selectLevelMapObject(&document, QStringLiteral("brush:99"), &error), "Selecting a missing brush should fail.");
	ok &= expect(levelMapSelectionCount(document) == 0 && document.selectionKind == LevelMapSelectionKind::None
			&& document.selectedObjectId == -1,
		"A failed selection should leave nothing selected.");
	return ok;
}

// A key set on several entities at once is one undo step, all or nothing,
// and saves as each entity's own edit would.
bool runMultiPropertySmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString path = root.filePath(QStringLiteral("multi-keys.map"));
	ok &= expect(writeFile(path, editableMapFixture()), "Multi-key fixture should be written.");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({path, {}, QStringLiteral("idtech2")}, &document, &error), "Multi-key fixture should load.");
	const auto keyOf = [&document](int entityId, const QString& key) {
		for (const LevelMapEntity& entity : document.entities) {
			if (entity.id != entityId) {
				continue;
			}
			for (const LevelMapProperty& property : entity.properties) {
				if (property.key == key) {
					return property.value;
				}
			}
		}
		return QStringLiteral("(none)");
	};
	const auto saved = [&document, &root](const QString& name) {
		const QString output = root.filePath(name);
		QFile::remove(output);
		saveLevelMapAs(document, output);
		QFile file(output);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	};
	const QByteArray original = saved(QStringLiteral("multi-keys-original.map"));
	ok &= expect(setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::Entity, 2}}, &error),
		"Selecting the light and the door should work.");
	const int depth = static_cast<int>(document.undoStack.size());

	// One value for all: a key new to both entities, as one undo step.
	ok &= expect(setLevelMapEntitiesProperty(&document, {1, 2}, QStringLiteral("light"), {QStringLiteral("300")}, &error),
		"Setting light on two entities should work.");
	ok &= expect(keyOf(1, QStringLiteral("light")) == QStringLiteral("300") && keyOf(2, QStringLiteral("light")) == QStringLiteral("300")
			&& document.undoStack.size() == depth + 1 && document.undoStack.last().description == QStringLiteral("Set light on 2 entities"),
		"Both entities should hold the key, as one undo step named for both.");
	ok &= expect(document.selection.size() == 2, "Setting a key on several entities should keep the selection.");
	ok &= expect(saved(QStringLiteral("multi-keys-set.map")).count("\"light\" \"300\"") == 2, "Both keys should be saved.");
	ok &= expect(undoLevelMapEdit(&document, &error) && keyOf(1, QStringLiteral("light")) == QStringLiteral("(none)")
			&& keyOf(2, QStringLiteral("light")) == QStringLiteral("(none)") && saved(QStringLiteral("multi-keys-undone.map")) == original,
		"Undo should take the key off both again and leave the file as it was.");
	ok &= expect(redoLevelMapEdit(&document, &error) && keyOf(2, QStringLiteral("light")) == QStringLiteral("300"), "Redo should set both again.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Undo should work again.");

	// A value for each: the door's name changes in place, the light gains one.
	ok &= expect(setLevelMapEntitiesProperty(&document, {1, 2}, QStringLiteral("targetname"), {QStringLiteral("lamp"), QStringLiteral("hatch")}, &error)
			&& keyOf(1, QStringLiteral("targetname")) == QStringLiteral("lamp") && keyOf(2, QStringLiteral("targetname")) == QStringLiteral("hatch"),
		"Each entity should take its own value.");
	ok &= expect(undoLevelMapEdit(&document, &error) && keyOf(2, QStringLiteral("targetname")) == QStringLiteral("gate")
			&& keyOf(1, QStringLiteral("targetname")) == QStringLiteral("(none)"),
		"Undo should restore each entity's own value.");

	// All or nothing, and nothing to do is no step.
	const int before = static_cast<int>(document.undoStack.size());
	ok &= expect(!setLevelMapEntitiesProperty(&document, {1, 99}, QStringLiteral("light"), {QStringLiteral("200")}, &error)
			&& error.contains(QStringLiteral("99")) && keyOf(1, QStringLiteral("light")) == QStringLiteral("(none)") && document.undoStack.size() == before,
		"A missing entity should refuse the whole edit and say which.");
	ok &= expect(!setLevelMapEntitiesProperty(&document, {1, 2}, QStringLiteral("message"), {QStringLiteral("say \"hi\"")}, &error)
			&& document.undoStack.size() == before,
		"A value with a double quote should be refused for all of them.");
	ok &= expect(setLevelMapEntitiesProperty(&document, {1, 2}, QStringLiteral("classname"), {QStringLiteral("light"), QStringLiteral("func_door")}, &error)
			&& document.undoStack.size() == before,
		"Values the entities already hold should leave no undo step.");

	// Removing from several: only those that have it, back on their lines on undo.
	ok &= expect(removeLevelMapEntitiesProperty(&document, {1, 2, 3}, QStringLiteral("origin"), &error)
			&& keyOf(1, QStringLiteral("origin")) == QStringLiteral("(none)") && keyOf(3, QStringLiteral("origin")) == QStringLiteral("(none)")
			&& document.undoStack.last().description == QStringLiteral("Remove origin from 2 entities"),
		"Removing a key should take it off every entity that has it, as one step.");
	ok &= expect(undoLevelMapEdit(&document, &error) && keyOf(1, QStringLiteral("origin")) == QStringLiteral("32 32 64")
			&& saved(QStringLiteral("multi-keys-restored.map")) == original,
		"Undo should put each removed key back on its own line.");
	ok &= expect(!removeLevelMapEntitiesProperty(&document, {1, 2}, QStringLiteral("no_such_key"), &error), "Removing a key none has should fail.");
	// A key above others comes back above them, where it was.
	ok &= expect(removeLevelMapEntitiesProperty(&document, {1, 2}, QStringLiteral("classname"), &error) && undoLevelMapEdit(&document, &error)
			&& saved(QStringLiteral("multi-keys-classes.map")) == original,
		"Undo should put a removed first key back on its own line, above the rest.");

	// Doom things change through their mirrors, records and all.
	const QString wadPath = root.filePath(QStringLiteral("multi-things.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Doom fixture should be written.");
	LevelMapDocument doom;
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), {}}, &doom, &error), "Doom fixture should load.");
	int added = -1;
	ok &= expect(addLevelMapDoomThing(&doom, 3004, 64.0, 96.0, 90, &added, &error), "Adding a second thing should work.");
	const int firstAngle = doom.doomThings.first().angle;
	ok &= expect(setLevelMapEntitiesProperty(&doom, {0, added}, QStringLiteral("angle"), {QStringLiteral("180")}, &error)
			&& doom.doomThings.first().angle == 180 && doom.doomThings.last().angle == 180,
		"Setting angle on two things should turn both.");
	ok &= expect(undoLevelMapEdit(&doom, &error) && doom.doomThings.first().angle == firstAngle && doom.doomThings.last().angle == 90,
		"Undo should turn each thing back.");
	ok &= expect(!setLevelMapEntitiesProperty(&doom, {0, added}, QStringLiteral("angle"), {QStringLiteral("north")}, &error)
			&& doom.doomThings.first().angle == firstAngle,
		"A value a thing cannot hold should refuse the whole edit.");
	return ok;
}

bool runMultiMoveSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString mapPath = root.filePath(QStringLiteral("multimove.map"));
	ok &= expect(writeFile(mapPath, quakeMapFixture()), "Multi-move fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({mapPath, {}, QStringLiteral("idtech2")}, &document, &error), "Multi-move fixture should load.");
	ok &= expect(!document.brushes.isEmpty() && !document.brushes.at(0).faces.isEmpty(), "The fixture should have a brush face.");
	const double brushFaceX = document.brushes.at(0).faces.at(0).p0.x;

	const QVector<LevelMapSelectionRef> both = {
		{LevelMapSelectionKind::Entity, 1},
		{LevelMapSelectionKind::QuakeBrush, 0},
	};
	ok &= expect(setLevelMapSelection(&document, both, &error), "Selecting an entity and a brush should work.");
	ok &= expect(moveLevelMapSelection(&document, 64.0, 0.0, 0.0, &error), "Moving the selection should work.");
	ok &= expect(document.undoStack.size() == 1, "A multi-object move should be a single undo command.");
	ok &= expect(document.undoStack.back().commandKind == QStringLiteral("move-selection"),
		"A multi-object move should record a compound command.");
	ok &= expect(document.undoStack.back().moveSteps.size() == 2, "The compound command should carry one step per object.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("96 32 64"),
		"The entity should have moved.");
	ok &= expect(nearly(document.brushes.at(0).faces.at(0).p0.x, brushFaceX + 64.0), "The brush should have moved.");
	ok &= expect(levelMapSelectionCount(document) == 2, "A move should keep the selection.");
	ok &= expect(document.selectionKind == LevelMapSelectionKind::QuakeBrush && document.selectedObjectId == 0,
		"A move should keep the primary member.");

	ok &= expect(undoLevelMapEdit(&document, &error), "One undo should reverse the whole compound move.");
	ok &= expect(document.undoStack.isEmpty() && document.redoStack.size() == 1,
		"A compound move should undo as one command.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("32 32 64"),
		"Undo should restore the entity.");
	ok &= expect(nearly(document.brushes.at(0).faces.at(0).p0.x, brushFaceX), "Undo should restore the brush.");
	ok &= expect(redoLevelMapEdit(&document, &error), "One redo should reapply the whole compound move.");
	ok &= expect(propertyValueForTest(document, 1, QStringLiteral("origin")) == QStringLiteral("96 32 64"),
		"Redo should reapply the entity move.");
	ok &= expect(nearly(document.brushes.at(0).faces.at(0).p0.x, brushFaceX + 64.0), "Redo should reapply the brush move.");

	// Doom vertices: a multi-move must mark the derived node lumps stale exactly
	// the way a single vertex move already does.
	const QString wadPath = root.filePath(QStringLiteral("multimove.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "Doom multi-move fixture should be written.");
	LevelMapDocument wad;
	ok &= expect(loadLevelMap({wadPath, QStringLiteral("MAP01"), QStringLiteral("idtech1")}, &wad, &error),
		"Doom multi-move fixture should load.");
	ok &= expect(wad.doomVertices.size() >= 2, "The Doom fixture should have vertices.");
	wad.doomGeometryChanged = false;

	const QVector<LevelMapSelectionRef> vertices = {
		{LevelMapSelectionKind::DoomVertex, 0},
		{LevelMapSelectionKind::DoomVertex, 1},
	};
	ok &= expect(setLevelMapSelection(&wad, vertices, &error), "Selecting two vertices should work.");
	// 20 and -20 on a 16-unit grid round to 16 and -16.
	ok &= expect(moveLevelMapSelectionSnapped(&wad, 20.0, -20.0, 0.0, 16.0, &error), "A snapped multi-move should work.");
	ok &= expect(wad.undoStack.size() == 1, "A snapped multi-move should be a single undo command.");
	ok &= expect(nearly(wad.doomVertices.at(0).x, 16.0) && nearly(wad.doomVertices.at(0).y, -16.0),
		"The first vertex should move by the snapped delta.");
	ok &= expect(nearly(wad.doomVertices.at(1).x, 144.0) && nearly(wad.doomVertices.at(1).y, -16.0),
		"The second vertex should move by the same snapped delta.");
	ok &= expect(wad.doomGeometryChanged, "Moving several vertices should mark the node lumps stale.");
	ok &= expect(wad.selectionKind == LevelMapSelectionKind::DoomVertex && wad.selectedObjectId == 1,
		"The primary member should survive a multi-move.");

	ok &= expect(undoLevelMapEdit(&wad, &error), "One undo should put every vertex back.");
	ok &= expect(nearly(wad.doomVertices.at(0).x, 0.0) && nearly(wad.doomVertices.at(0).y, 0.0),
		"Undo should restore the first vertex.");
	ok &= expect(nearly(wad.doomVertices.at(1).x, 128.0) && nearly(wad.doomVertices.at(1).y, 0.0),
		"Undo should restore the second vertex.");
	ok &= expect(wad.undoStack.isEmpty(), "Undo should drain the single compound command.");

	// A delta that snaps to nothing is a no-op rather than an empty history entry.
	ok &= expect(!moveLevelMapSelectionSnapped(&wad, 3.0, -2.0, 0.0, 64.0, &error), "A delta that snaps to zero should fail.");
	ok &= expect(wad.undoStack.isEmpty(), "A rejected move should not push an undo command.");

	// Sectors are selectable but have no position of their own.
	ok &= expect(!wad.doomSectors.isEmpty(), "The Doom fixture should have a sector.");
	ok &= expect(selectLevelMapObject(&wad, QStringLiteral("sector:0"), &error), "Selecting a sector should work.");
	ok &= expect(!moveLevelMapSelection(&wad, 16.0, 0.0, 0.0, &error), "Moving a sector-only selection should fail.");
	ok &= expect(wad.undoStack.isEmpty(), "A selection with nothing movable should not push an undo command.");
	clearLevelMapSelection(&wad);
	ok &= expect(!moveLevelMapSelection(&wad, 16.0, 0.0, 0.0, &error), "Moving an empty selection should fail.");
	return ok;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	const QDir root(tempDir.path());
	bool ok = true;
	auto run = [&ok](const char* name, const auto& test) {
		const auto started = std::chrono::steady_clock::now();
		std::cerr << name << " started" << std::endl;
		ok &= test();
		std::cerr << name << ": " << std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count() << " ms" << std::endl;
	};
	run("runQuakeMapSmoke", [&]() { return runQuakeMapSmoke(root); });
	run("runTokenizerSmoke", [&]() { return runTokenizerSmoke(root); });
	run("runValve220Smoke", [&]() { return runValve220Smoke(root); });
	run("runQuake3Smoke", [&]() { return runQuake3Smoke(root); });
	run("runNonSquarePatchSmoke", [&]() { return runNonSquarePatchSmoke(root); });
	run("runRotatedBrushSmoke", [&]() { return runRotatedBrushSmoke(root); });
	run("runTextSaveSmoke", [&]() { return runTextSaveSmoke(root); });
	run("runUndoRedoSmoke", [&]() { return runUndoRedoSmoke(root); });
	run("runAddDeleteSmoke", [&]() { return runAddDeleteSmoke(root); });
	run("runTargetLinksSmoke", [&]() { return runTargetLinksSmoke(root); });
	run("runDuplicateSmoke", [&]() { return runDuplicateSmoke(root); });
	run("runDoomThingEditSmoke", [&]() { return runDoomThingEditSmoke(root); });
	run("runDoomLinedefEditSmoke", [&]() { return runDoomLinedefEditSmoke(root); });
	run("runSnapToGridSmoke", [&]() { return runSnapToGridSmoke(root); });
	run("runClipboardSmoke", [&]() { return runClipboardSmoke(root); });
	run("runReplaceTextureSmoke", [&]() { return runReplaceTextureSmoke(root); });
	run("runRotateSmoke", [&]() { return runRotateSmoke(root); });
	run("runFlipSmoke", [&]() { return runFlipSmoke(root); });
	run("runResizeSmoke", [&]() { return runResizeSmoke(root); });
	run("runClipSmoke", [&]() { return runClipSmoke(root); });
	run("runHollowSmoke", [&]() { return runHollowSmoke(root); });
	run("runCarveSmoke", [&]() { return runCarveSmoke(root); });
	run("runReview4Smoke", [&]() { return runReview4Smoke(root); });
	run("runDoomTopologySmoke", [&]() { return runDoomTopologySmoke(root); });
	run("runDoomFieldsSmoke", [&]() { return runDoomFieldsSmoke(root); });
	run("runFaceFieldsSmoke", [&]() { return runFaceFieldsSmoke(root); });
	run("runReview5Smoke", [&]() { return runReview5Smoke(root); });
	run("runConnectEntitiesSmoke", [&]() { return runConnectEntitiesSmoke(root); });
	run("runReview6Smoke", [&]() { return runReview6Smoke(root); });
	run("runMakeDoorSmoke", [&]() { return runMakeDoorSmoke(root); });
	run("runSectorFieldsSmoke", [&]() { return runSectorFieldsSmoke(root); });
	run("runReview7Smoke", [&]() { return runReview7Smoke(root); });
	run("runTagLinksSmoke", [&]() { return runTagLinksSmoke(root); });
	run("runEditFidelitySmoke", [&]() { return runEditFidelitySmoke(root); });
	run("runAddBrushSmoke", [&]() { return runAddBrushSmoke(root); });
	run("runGridSnapSmoke", [&]() { return runGridSnapSmoke(); });
	run("runSelectionSetSmoke", [&]() { return runSelectionSetSmoke(root); });
	run("runMultiPropertySmoke", [&]() { return runMultiPropertySmoke(root); });
	run("runMultiMoveSmoke", [&]() { return runMultiMoveSmoke(root); });
	run("runDoomMapSmoke", [&]() { return runDoomMapSmoke(root); });
	run("runDoomIwadSmoke", [&]() { return runDoomIwadSmoke(root); });
	run("runEmptyWadDirectorySmoke", [&]() { return runEmptyWadDirectorySmoke(root); });
	run("runDoomMarkerSmoke", [&]() { return runDoomMarkerSmoke(root); });
	run("runHexenSmoke", [&]() { return runHexenSmoke(root); });
	run("runUdmfSmoke", [&]() { return runUdmfSmoke(root); });
	run("runDoomValidationSmoke", [&]() { return runDoomValidationSmoke(root); });
	run("runQuerySmoke", [&]() { return runQuerySmoke(root); });
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
