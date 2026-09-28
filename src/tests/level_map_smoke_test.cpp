#include "core/compiler_profiles.h"
#include "core/level_map.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPoint>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <iostream>

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

	// Defect 4: a move on an entity without an origin key must not leave one behind.
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("entity"), 0, 4.0, 4.0, 4.0, &error), "Move on worldspawn should work.");
	ok &= expect(propertyValueForTest(document, 0, QStringLiteral("origin")) == QStringLiteral("4 4 4"), "Move should synthesize an origin.");
	ok &= expect(undoLevelMapEdit(&document, &error), "Undo of a synthesizing move should work.");
	ok &= expect(propertyValueForTest(document, 0, QStringLiteral("origin")).isEmpty(), "Undo should remove a synthesized origin key.");
	ok &= expect(!document.entities.at(0).origin.valid, "Undo should clear the synthesized origin vector.");

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
	ok &= expect(hasIssueCode(document, QStringLiteral("udmf-unsupported")), "UDMF should report a specific unsupported-format issue.");
	ok &= expect(document.doomLinedefs.isEmpty() && document.doomVertices.isEmpty(), "UDMF must not produce a broken Doom parse.");
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

	// Single selection collapses the set again, and a failed selection empties it.
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("brush:0"), &error), "Selecting a brush should work.");
	ok &= expect(levelMapSelectionCount(document) == 1, "A single selection should replace the whole set.");
	ok &= expect(!selectLevelMapObject(&document, QStringLiteral("brush:99"), &error), "Selecting a missing brush should fail.");
	ok &= expect(levelMapSelectionCount(document) == 0 && document.selectionKind == LevelMapSelectionKind::None
			&& document.selectedObjectId == -1,
		"A failed selection should leave nothing selected.");
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
	ok &= runQuakeMapSmoke(root);
	ok &= runTokenizerSmoke(root);
	ok &= runValve220Smoke(root);
	ok &= runQuake3Smoke(root);
	ok &= runNonSquarePatchSmoke(root);
	ok &= runRotatedBrushSmoke(root);
	ok &= runTextSaveSmoke(root);
	ok &= runUndoRedoSmoke(root);
	ok &= runGridSnapSmoke();
	ok &= runSelectionSetSmoke(root);
	ok &= runMultiMoveSmoke(root);
	ok &= runDoomMapSmoke(root);
	ok &= runDoomIwadSmoke(root);
	ok &= runEmptyWadDirectorySmoke(root);
	ok &= runDoomMarkerSmoke(root);
	ok &= runHexenSmoke(root);
	ok &= runUdmfSmoke(root);
	ok &= runDoomValidationSmoke(root);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
