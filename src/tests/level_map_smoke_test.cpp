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
	appendLe16(&bytes, 0);
	appendLe16(&bytes, 0);
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

QByteArray wadFixture()
{
	struct Lump {
		QByteArray name;
		QByteArray bytes;
	};
	const QVector<Lump> lumps = {
		{"PLAYPAL", "palette"},
		{"MAP01", {}},
		{"THINGS", doomThings()},
		{"LINEDEFS", doomLinedefs()},
		{"SIDEDEFS", doomSidedefs()},
		{"VERTEXES", doomVertices()},
		{"SECTORS", doomSectors()},
	};

	QByteArray data;
	data.append("PWAD");
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

bool runDoomMapSmoke(const QDir& root)
{
	bool ok = true;
	QString error;
	const QString sourcePath = root.filePath(QStringLiteral("doom.wad"));
	ok &= expect(writeFile(sourcePath, wadFixture()), "Doom WAD fixture should be written.");

	LevelMapDocument document;
	ok &= expect(loadLevelMap({sourcePath, QStringLiteral("MAP01"), QStringLiteral("idtech1")}, &document, &error), "Doom WAD map should load.");
	ok &= expect(document.format == LevelMapFormat::DoomWad, "Doom WAD format mismatch.");
	ok &= expect(document.mapName == QStringLiteral("MAP01"), "Doom WAD map marker mismatch.");
	ok &= expect(document.doomVertices.size() == 4 && document.doomLinedefs.size() == 4, "Doom WAD geometry should parse.");
	ok &= expect(document.entities.size() == 1 && document.doomThings.size() == 1, "Doom things should be inspectable as entities.");
	ok &= expect(levelMapTextureLines(document).join('\n').contains(QStringLiteral("WALLMID")), "Doom WAD should expose wall textures.");
	ok &= expect(selectLevelMapObject(&document, QStringLiteral("vertex:0"), &error), "Doom vertex selection should work.");
	ok &= expect(levelMapPropertyLines(document).join('\n').contains(QStringLiteral("Position")), "Doom vertex property inspector should show position.");
	ok &= expect(moveLevelMapObject(&document, QStringLiteral("vertex"), 0, 8.0, 4.0, 0.0, &error), "Doom vertex move should work.");
	ok &= expect(std::lround(document.doomVertices.at(0).x) == 8 && std::lround(document.doomVertices.at(0).y) == 4, "Doom vertex should move.");
	ok &= expect(setLevelMapEntityProperty(&document, 0, QStringLiteral("type"), QStringLiteral("3004"), &error), "Doom thing entity type edit should work.");
	ok &= expect(document.doomThings.at(0).type == 3004, "Doom thing type should sync from entity edit.");

	const QString outputPath = root.filePath(QStringLiteral("doom-edited.wad"));
	const LevelMapSaveReport save = saveLevelMapAs(document, outputPath);
	ok &= expect(save.succeeded() && QFileInfo::exists(outputPath), "Doom save-as should write.");

	LevelMapDocument reloaded;
	ok &= expect(loadLevelMap({outputPath, QStringLiteral("MAP01"), QStringLiteral("idtech1")}, &reloaded, &error), "Edited Doom WAD should reload.");
	ok &= expect(std::lround(reloaded.doomVertices.at(0).x) == 8 && std::lround(reloaded.doomVertices.at(0).y) == 4, "Reloaded Doom WAD should persist moved vertex.");
	ok &= expect(reloaded.doomThings.at(0).type == 3004, "Reloaded Doom WAD should persist thing type edit.");
	const CompilerCommandRequest request = compilerRequestForLevelMap(reloaded, QString(), QString());
	ok &= expect(request.profileId == QStringLiteral("zdbsp-nodes"), "Doom map should default to zdbsp-nodes compile plan.");
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
	ok &= runDoomMapSmoke(root);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
