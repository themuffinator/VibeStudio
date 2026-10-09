#pragma once

// Fixtures shared by the release and asset register tests: generated stock
// packages for a fake Quake III, Quake and Doom II installation, and mod
// projects that mix stock and custom assets. Every byte is generated here; no
// game data is used.

#include "core/game_installation.h"
#include "core/package_staging.h"
#include "core/project_manifest.h"
#include "tests/doom_preview_test_helpers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <cstring>
#include <utility>

namespace vibestudio::tests::release {

inline bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// Writes a PK3, ZIP or PAK through the studio's own package writer.
inline bool writePackage(const QString& path, PackageArchiveFormat format, const QVector<QPair<QString, QByteArray>>& files, QString* error = nullptr)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	// Fixtures replace earlier ones outright; the writer itself never overwrites.
	QFile::remove(path);
	PackageStagingModel model;
	QString failure;
	if (!model.createEmpty(format, QString(), &failure)) {
		if (error) { *error = failure; }
		return false;
	}
	for (const auto& file : files) {
		if (!model.addBytes(file.second, file.first, &failure)) {
			if (error) { *error = failure; }
			return false;
		}
	}
	PackageWriteRequest request;
	request.format = format;
	request.destinationPath = path;
	const PackageWriteReport report = model.writeArchive(request);
	if (!report.succeeded()) {
		if (error) { *error = report.blockedMessages.join(QLatin1Char(' ')); }
		return false;
	}
	return true;
}

// The stock bytes of files the fake Quake III ships, so tests can copy them.
inline QByteArray stockWall() { return QByteArray("stock wall texture pixels ").repeated(40); }
inline QByteArray stockOverridden() { return QByteArray("the game's own crate texture ").repeated(30); }

// <root>/baseq3/pak0.pk3 and pak1.pk3, <root>/missionpack/pak0.pk3.
inline bool makeQuake3Installation(const QString& root, QString* error = nullptr)
{
	const QByteArray baseShaders =
		"textures/base_wall/glass\n{\n\tqer_editorimage textures/base_wall/glass.tga\n\t{\n\t\tmap textures/base_wall/glass.tga\n\t}\n}\n"
		"textures/common/caulk\n{\n\tsurfaceparm nodraw\n}\n"
		"textures/skies/stocksky\n{\n\tskyParms env/stock 512 -\n}\n";
	return writePackage(QDir(root).filePath(QStringLiteral("baseq3/pak0.pk3")), PackageArchiveFormat::Pk3,
			   {{QStringLiteral("textures/base_wall/wall1.tga"), stockWall()},
				   {QStringLiteral("textures/base_wall/crate.tga"), stockOverridden()},
				   {QStringLiteral("textures/base_wall/glass.tga"), QByteArray("glass ").repeated(20)},
				   {QStringLiteral("scripts/base_wall.shader"), baseShaders},
				   {QStringLiteral("models/mapobjects/barrel.md3"), QByteArray("IDP3 stand-in")},
				   {QStringLiteral("sound/world/hum.wav"), QByteArray("RIFF stand-in")},
				   {QStringLiteral("music/fla22k_02.wav"), QByteArray("RIFF music")}},
			   error)
		&& writePackage(QDir(root).filePath(QStringLiteral("baseq3/pak1.pk3")), PackageArchiveFormat::Pk3,
			{{QStringLiteral("textures/base_floor/floor1.tga"), QByteArray("floor ").repeated(10)}}, error)
		&& writePackage(QDir(root).filePath(QStringLiteral("missionpack/pak0.pk3")), PackageArchiveFormat::Pk3,
			{{QStringLiteral("textures/team/flag.tga"), QByteArray("team arena flag")}}, error)
		&& writeFile(QDir(root).filePath(QStringLiteral("quake3.exe")), QByteArray());
}

inline GameInstallationProfile profile(const QString& id, const QString& gameKey, const QString& root)
{
	GameInstallationProfile installation;
	installation.id = id;
	installation.gameKey = gameKey;
	installation.engineFamily = gameDefinitionForKey(gameKey).engineFamily;
	installation.displayName = QStringLiteral("Test %1").arg(gameKey);
	installation.rootPath = root;
	return installation;
}

// A Quake III map whose brushes and entities use, in order: a stock image, a
// custom image, a stock shader, a custom shader, an overridden stock image,
// a stock common shader, a custom sound, a stock model and a stock sound.
inline QByteArray quake3Map(const QString& extraTexture = {})
{
	QByteArray text =
		"// Game: Quake 3\n"
		"{\n\"classname\" \"worldspawn\"\n\"message\" \"The Pit\"\n\"music\" \"music/fla22k_02.wav\"\n"
		"{\n"
		"( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) base_wall/wall1 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) mymod/wall 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) base_wall/glass 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) mymod/glow 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) base_wall/crate 0 0 0 0.5 0.5 0 0 0\n"
		"( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) common/caulk 0 0 0 0.5 0.5 0 0 0\n"
		"}\n}\n"
		"{\n\"classname\" \"target_speaker\"\n\"origin\" \"32 32 32\"\n\"noise\" \"sound/mymod/hum.wav\"\n}\n"
		"{\n\"classname\" \"misc_model\"\n\"origin\" \"16 16 16\"\n\"model\" \"models/mapobjects/barrel.md3\"\n}\n"
		"{\n\"classname\" \"target_speaker\"\n\"origin\" \"8 8 8\"\n\"noise\" \"sound/world/hum.wav\"\n}\n"
		"{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"32 32 24\"\n}\n";
	if (!extraTexture.isEmpty()) {
		text += "{\n\"classname\" \"func_group\"\n{\n"
				"( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) " + extraTexture.toUtf8() + " 0 0 0 0.5 0.5 0 0 0\n"
				"( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) common/caulk 0 0 0 0.5 0.5 0 0 0\n"
				"( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) common/caulk 0 0 0 0.5 0.5 0 0 0\n"
				"( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) common/caulk 0 0 0 0.5 0.5 0 0 0\n"
				"}\n}\n";
	}
	return text;
}

inline bool writeManifest(const QString& root, const QString& gameKey, const QJsonObject& release = {})
{
	QDir().mkpath(root);
	ProjectManifest manifest = defaultProjectManifest(root, QStringLiteral("My Mod"));
	manifest.gameKey = gameKey;
	manifest.outputFolder = QStringLiteral("build");
	QString error;
	if (!saveProjectManifest(manifest, &error)) {
		return false;
	}
	if (release.isEmpty()) {
		return true;
	}
	QFile file(projectManifestPath(root));
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
	file.close();
	object.insert(QStringLiteral("release"), release);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(object).toJson()) > 0;
}

// The mod project: the map, its build and companions, custom assets, an
// identical copy of a stock texture, an override, and files that never ship.
inline bool makeQuake3Project(const QString& root)
{
	bool ok = writeManifest(root, QStringLiteral("quake3"),
		QJsonObject {{QStringLiteral("title"), QStringLiteral("The Pit")}, {QStringLiteral("version"), QStringLiteral("1.0.0")},
			{QStringLiteral("authors"), QJsonArray {QStringLiteral("Test Author <test@example.com>")}},
			{QStringLiteral("description"), QStringLiteral("A small arena.")}, {QStringLiteral("license"), QStringLiteral("Free to share unmodified.")},
			{QStringLiteral("packageName"), QStringLiteral("thepit")}});
	const QDir dir(root);
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1.map")), quake3Map());
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1.bsp")), QByteArray("IBSP stand-in compiled map"));
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1.aas")), QByteArray("EAAS bot file"));
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1/lm_0000.tga")), QByteArray("external lightmap"));
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1.prt")), QByteArray("PRT1 portals"));
	ok &= writeFile(dir.filePath(QStringLiteral("levelshots/arena1.jpg")), QByteArray("JFIF shot"));
	ok &= writeFile(dir.filePath(QStringLiteral("scripts/arena1.arena")), QByteArray("{\nmap \"arena1\"\n}\n"));
	ok &= writeFile(dir.filePath(QStringLiteral("scripts/mymod.shader")),
		QByteArray("textures/mymod/glow\n{\n\tqer_editorimage textures/mymod/glow.tga\n\t{\n\t\tmap textures/mymod/glow.tga\n\t}\n}\n"));
	ok &= writeFile(dir.filePath(QStringLiteral("textures/mymod/wall.tga")), QByteArray("my wall ").repeated(64));
	ok &= writeFile(dir.filePath(QStringLiteral("textures/mymod/glow.tga")), QByteArray("my glow ").repeated(64));
	ok &= writeFile(dir.filePath(QStringLiteral("textures/mymod/unused.tga")), QByteArray("unused ").repeated(8));
	ok &= writeFile(dir.filePath(QStringLiteral("textures/base_wall/wall1.tga")), stockWall());
	ok &= writeFile(dir.filePath(QStringLiteral("textures/base_wall/crate.tga")), QByteArray("my own crate ").repeated(30));
	ok &= writeFile(dir.filePath(QStringLiteral("sound/mymod/hum.wav")), QByteArray("RIFF my hum"));
	ok &= writeFile(dir.filePath(QStringLiteral("src/art.psd")), QByteArray("8BPS"));
	ok &= writeFile(dir.filePath(QStringLiteral("notes.bak")), QByteArray("old"));
	ok &= writeFile(dir.filePath(QStringLiteral("readme.txt")), QByteArray("Read me."));
	return ok;
}

// Doom II IWAD: the shared Doom test assets (PLAYPAL, flats, patches,
// PNAMES, TEXTURE1) plus a map and a music lump.
inline QByteArray doom2Iwad()
{
	using namespace vibestudio::tests;
	QByteArray music("MUS\x1a", 4);
	music += QByteArray(28, '\0');
	QVector<doom::Lump> lumps = doom::assets();
	lumps << doom::Lump {"D_RUNNIN", music};
	QByteArray bytes = doom::wad(lumps + doom::lumps(doom::fixture()).mid(0, 11));
	bytes.replace(0, 4, "IWAD");
	return bytes;
}

// MD3 fixture layout after src/tests/model_mesh_smoke_test.cpp: one frame,
// one surface per shader.
inline void appendI32(QByteArray& bytes, qint32 value)
{
	char raw[4];
	qToLittleEndian(value, raw);
	bytes.append(raw, 4);
}
inline void appendI16(QByteArray& bytes, qint16 value)
{
	char raw[2];
	qToLittleEndian(value, raw);
	bytes.append(raw, 2);
}
inline void appendF32(QByteArray& bytes, float value)
{
	qint32 raw = 0;
	std::memcpy(&raw, &value, 4);
	appendI32(bytes, raw);
}
inline void appendFixed(QByteArray& bytes, const QByteArray& text, int size)
{
	QByteArray fixed = text.left(size - 1);
	fixed.resize(size, '\0');
	bytes += fixed;
}
inline QByteArray md3(const QByteArrayList& shaders)
{
	constexpr int surfaceBytes = 236;
	const int frameOffset = 108;
	const int tagOffset = frameOffset + 56;
	const int surfaceOffset = tagOffset;
	const int endOffset = surfaceOffset + surfaceBytes * int(shaders.size());
	QByteArray bytes("IDP3", 4);
	appendI32(bytes, 15);
	appendFixed(bytes, "models/test/release", 64);
	appendI32(bytes, 0);
	appendI32(bytes, 1);
	appendI32(bytes, 0);
	appendI32(bytes, int(shaders.size()));
	appendI32(bytes, 0);
	appendI32(bytes, frameOffset);
	appendI32(bytes, tagOffset);
	appendI32(bytes, surfaceOffset);
	appendI32(bytes, endOffset);
	for (float value : {-16.f, -16.f, -16.f, 16.f, 16.f, 16.f, 0.f, 0.f, 0.f, 27.7f}) {
		appendF32(bytes, value);
	}
	appendFixed(bytes, "idle", 16);
	for (int surface = 0; surface < shaders.size(); ++surface) {
		const int shaderOffset = 108;
		const int triangleOffset = shaderOffset + 68;
		const int stOffset = triangleOffset + 12;
		const int vertexOffset = stOffset + 24;
		bytes.append("IDP3", 4);
		appendFixed(bytes, "surface" + QByteArray::number(surface), 64);
		appendI32(bytes, 0);
		appendI32(bytes, 1);
		appendI32(bytes, 1);
		appendI32(bytes, 3);
		appendI32(bytes, 1);
		appendI32(bytes, triangleOffset);
		appendI32(bytes, shaderOffset);
		appendI32(bytes, stOffset);
		appendI32(bytes, vertexOffset);
		appendI32(bytes, vertexOffset + 24);
		appendFixed(bytes, shaders[surface], 64);
		appendI32(bytes, 0);
		appendI32(bytes, 0);
		appendI32(bytes, 1);
		appendI32(bytes, 2);
		for (float value : {0.f, 0.f, 1.f, 0.f, 1.f, 1.f}) {
			appendF32(bytes, value);
		}
		const int raw[3][3] = {{64, 128, -64}, {-128, 0, 320}, {0, -256, 64}};
		for (const auto& vertex : raw) {
			appendI16(bytes, qint16(vertex[0]));
			appendI16(bytes, qint16(vertex[1]));
			appendI16(bytes, qint16(vertex[2]));
			appendI16(bytes, 0);
		}
	}
	return bytes;
}

} // namespace vibestudio::tests::release
