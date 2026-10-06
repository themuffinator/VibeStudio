#include "tests/level_object_test_helpers.h"
using vibestudio::tests::objectIndex;
using vibestudio::tests::setObjectCurrentRow;
using vibestudio::tests::setObjectSelected;
#include "package_entry_test_helpers.h"
// Drives the real studio window the way a user would: opens generated
// fixtures, switches surfaces, selects rows, presses keys, and checks that the
// shell reacts. Every fixture is built here from published format layouts, so
// no game data is involved.

#include "app/application_shell.h"
#include "app/level_ai_edit_dialog.h"
#include "app/level_generation_dialog.h"
#include "app/sound_generation_dialog.h"
#include "app/texture_generation_dialog.h"
#include "app/package_folder_view.h"
#include "app/asset_views.h"
#include "app/code_editor.h"
#include "app/studio_charts.h"
#include "app/map_viewport.h"
#include "app/studio_layout.h"
#include "app/studio_icons.h"
#include "app/syntax_highlight.h"
#include "app/studio_runtime.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/deflate.h"
#include "core/game_installation.h"
#include "core/level_generation.h"
#include "core/project_manifest.h"
#include "core/studio_settings.h"
#include "tests/fake_ai_provider.h"
#include "vibestudio_config.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QRadioButton>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCompleter>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QDropEvent>
#include <QMimeData>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QKeySequenceEdit>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

#include <numbers>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <chrono>
#include <memory>
#include <thread>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;

namespace {

int g_failures = 0;

void check(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		++g_failures;
	}
}

// ---------------------------------------------------------------------------
// Byte helpers
// ---------------------------------------------------------------------------

void appendLe16(QByteArray* data, quint16 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* data, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8) {
		data->append(static_cast<char>((value >> shift) & 0xff));
	}
}

void appendI32(QByteArray* data, qint32 value)
{
	appendLe32(data, static_cast<quint32>(value));
}

void appendI16(QByteArray* data, int value)
{
	appendLe16(data, static_cast<quint16>(static_cast<qint16>(value)));
}

void appendF32(QByteArray* data, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	appendLe32(data, raw);
}

void appendFixed(QByteArray* data, const QByteArray& text, int length)
{
	QByteArray padded = text.left(length);
	padded.append(QByteArray(length - padded.size(), '\0'));
	data->append(padded);
}

bool writeFile(const QString& path, const QByteArray& data)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// 24-bit uncompressed Targa, top-left origin (Truevision TGA 2.0 spec).
QByteArray buildTarga(int width, int height, const std::function<QRgb(int, int)>& pixel)
{
	QByteArray bytes;
	bytes.append('\0');
	bytes.append('\0');
	bytes.append('\2');
	appendLe16(&bytes, 0);
	appendLe16(&bytes, 0);
	bytes.append('\0');
	appendLe16(&bytes, 0);
	appendLe16(&bytes, 0);
	appendLe16(&bytes, static_cast<quint16>(width));
	appendLe16(&bytes, static_cast<quint16>(height));
	bytes.append(static_cast<char>(24));
	bytes.append(static_cast<char>(0x20));
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const QRgb rgb = pixel(x, y);
			bytes.append(static_cast<char>(qBlue(rgb)));
			bytes.append(static_cast<char>(qGreen(rgb)));
			bytes.append(static_cast<char>(qRed(rgb)));
		}
	}
	return bytes;
}

// 16-bit mono PCM WAV (RIFF WAVE, format tag 1).
QByteArray buildWav(int frames, int rate)
{
	QByteArray samples;
	for (int index = 0; index < frames; ++index) {
		const double value = 0.5 * std::sin(2.0 * 3.14159265358979 * 220.0 * index / rate);
		appendI16(&samples, static_cast<int>(value * 32000.0));
	}
	QByteArray data;
	data.append("RIFF");
	appendLe32(&data, static_cast<quint32>(36 + samples.size()));
	data.append("WAVE");
	data.append("fmt ");
	appendLe32(&data, 16);
	appendLe16(&data, 1);
	appendLe16(&data, 1);
	appendLe32(&data, static_cast<quint32>(rate));
	appendLe32(&data, static_cast<quint32>(rate * 2));
	appendLe16(&data, 2);
	appendLe16(&data, 16);
	data.append("data");
	appendLe32(&data, static_cast<quint32>(samples.size()));
	data.append(samples);
	return data;
}

// A one-frame MD3 cube (released Quake III Arena source, md3.h): one surface
// of 24 vertices and 12 triangles naming models/crate/skin.tga.
QByteArray buildMd3Cube()
{
	struct Face {
		int normal[3];
		int corners[4][3];
	};
	const Face faces[6] = {
		{{1, 0, 0}, {{32, -32, -32}, {32, 32, -32}, {32, 32, 32}, {32, -32, 32}}},
		{{-1, 0, 0}, {{-32, 32, -32}, {-32, -32, -32}, {-32, -32, 32}, {-32, 32, 32}}},
		{{0, 1, 0}, {{32, 32, -32}, {-32, 32, -32}, {-32, 32, 32}, {32, 32, 32}}},
		{{0, -1, 0}, {{-32, -32, -32}, {32, -32, -32}, {32, -32, 32}, {-32, -32, 32}}},
		{{0, 0, 1}, {{-32, -32, 32}, {32, -32, 32}, {32, 32, 32}, {-32, 32, 32}}},
		{{0, 0, -1}, {{-32, 32, -32}, {32, 32, -32}, {32, -32, -32}, {-32, -32, -32}}},
	};
	const float uvs[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
	const int vertexCount = 24;
	const int triangleCount = 12;

	auto packedNormal = [](const int normal[3]) {
		const double lng = std::acos(std::clamp(static_cast<double>(normal[2]), -1.0, 1.0));
		const double lat = std::atan2(static_cast<double>(normal[1]), static_cast<double>(normal[0]));
		const int latByte = static_cast<int>(std::lround(lat * 256.0 / (2.0 * 3.14159265358979))) & 0xff;
		const int lngByte = static_cast<int>(std::lround(lng * 256.0 / (2.0 * 3.14159265358979))) & 0xff;
		return (latByte << 8) | lngByte;
	};

	const int surfaceHeader = 108;
	const int ofsShaders = surfaceHeader;
	const int ofsTriangles = ofsShaders + 68;
	const int ofsSt = ofsTriangles + triangleCount * 12;
	const int ofsXyz = ofsSt + vertexCount * 8;
	const int surfaceEnd = ofsXyz + vertexCount * 8;

	QByteArray surface;
	surface.append("IDP3", 4);
	appendFixed(&surface, QByteArrayLiteral("crate"), 64);
	for (const int value : {0, 1, 1, vertexCount, triangleCount, ofsTriangles, ofsShaders, ofsSt, ofsXyz, surfaceEnd}) {
		appendI32(&surface, value);
	}
	appendFixed(&surface, QByteArrayLiteral("models/crate/skin.tga"), 64);
	appendI32(&surface, 0);
	for (int face = 0; face < 6; ++face) {
		const int base = face * 4;
		for (const int index : {base, base + 1, base + 2, base, base + 2, base + 3}) {
			appendI32(&surface, index);
		}
	}
	for (int face = 0; face < 6; ++face) {
		for (const auto& uv : uvs) {
			appendF32(&surface, uv[0]);
			appendF32(&surface, uv[1]);
		}
	}
	for (const Face& face : faces) {
		for (const auto& corner : face.corners) {
			appendI16(&surface, corner[0] * 64);
			appendI16(&surface, corner[1] * 64);
			appendI16(&surface, corner[2] * 64);
			appendLe16(&surface, static_cast<quint16>(packedNormal(face.normal)));
		}
	}

	const int ofsFrames = 108;
	const int ofsTags = ofsFrames + 56;
	const int ofsSurfaces = ofsTags;
	const int ofsEnd = ofsSurfaces + static_cast<int>(surface.size());
	QByteArray bytes;
	bytes.append("IDP3", 4);
	appendI32(&bytes, 15);
	appendFixed(&bytes, QByteArrayLiteral("models/crate/crate"), 64);
	for (const int value : {0, 1, 0, 1, 0, ofsFrames, ofsTags, ofsSurfaces, ofsEnd}) {
		appendI32(&bytes, value);
	}
	for (const float value : {-32.0f, -32.0f, -32.0f, 32.0f, 32.0f, 32.0f, 0.0f, 0.0f, 0.0f, 55.5f}) {
		appendF32(&bytes, value);
	}
	appendFixed(&bytes, QByteArrayLiteral("idle01"), 16);
	bytes.append(surface);
	return bytes;
}

struct ZipInput {
	QString name;
	QByteArray data;
};

// Stored-only ZIP (PKWARE APPNOTE.TXT sections 4.3.7, 4.3.12, 4.3.16).
bool buildStoredZip(const QString& path, const QVector<ZipInput>& inputs)
{
	QByteArray data;
	QByteArray central;
	for (const ZipInput& input : inputs) {
		const QByteArray name = input.name.toUtf8();
		const quint32 localOffset = static_cast<quint32>(data.size());
		const quint32 size = static_cast<quint32>(input.data.size());
		const quint32 crc = crc32Bytes(input.data);
		const quint16 fixedDate = static_cast<quint16>((1 << 5) | 1);

		appendLe32(&data, 0x04034b50);
		for (const quint16 value : {quint16(20), quint16(0), quint16(0), quint16(0), fixedDate}) {
			appendLe16(&data, value);
		}
		appendLe32(&data, crc);
		appendLe32(&data, size);
		appendLe32(&data, size);
		appendLe16(&data, static_cast<quint16>(name.size()));
		appendLe16(&data, 0);
		data.append(name);
		data.append(input.data);

		appendLe32(&central, 0x02014b50);
		for (const quint16 value : {quint16(20), quint16(20), quint16(0), quint16(0), quint16(0), fixedDate}) {
			appendLe16(&central, value);
		}
		appendLe32(&central, crc);
		appendLe32(&central, size);
		appendLe32(&central, size);
		appendLe16(&central, static_cast<quint16>(name.size()));
		for (int index = 0; index < 4; ++index) {
			appendLe16(&central, 0);
		}
		appendLe32(&central, 0);
		appendLe32(&central, localOffset);
		central.append(name);
	}
	const quint32 centralOffset = static_cast<quint32>(data.size());
	data.append(central);
	appendLe32(&data, 0x06054b50);
	appendLe16(&data, 0);
	appendLe16(&data, 0);
	appendLe16(&data, static_cast<quint16>(inputs.size()));
	appendLe16(&data, static_cast<quint16>(inputs.size()));
	appendLe32(&data, static_cast<quint32>(central.size()));
	appendLe32(&data, centralOffset);
	appendLe16(&data, 0);
	return writeFile(path, data);
}

// A Doom resource WAD with no maps (https://doomwiki.org/wiki/WAD): DSTONE, a
// second of sawtooth as a DMX lump with its pad bytes (https://doomwiki.org/wiki/Sound);
// DPBEEP, a PC speaker sound; a text lump that is neither; a PLAYPAL whose
// first palette maps index i to (i, 255 - i, 128); FLOOR0, a flat whose pixel
// at x, y is index x + y; and TROOA1, a 4x4 sprite patch of index 7, each in
// its namespace.
bool buildSoundWad(const QString& path)
{
	QByteArray tone;
	appendLe16(&tone, 3);
	appendLe16(&tone, 11025);
	appendLe32(&tone, 11025 + 32);
	tone.append(QByteArray(16, static_cast<char>(0x80)));
	for (int index = 0; index < 11025; ++index) {
		tone.append(static_cast<char>((index * 4) % 256));
	}
	tone.append(QByteArray(16, static_cast<char>(0x80)));
	QByteArray beep;
	appendLe16(&beep, 0);
	appendLe16(&beep, 4);
	beep.append("\x20\x30\x40\x00", 4);
	QByteArray playpal;
	for (int index = 0; index < 256; ++index) {
		playpal.append(static_cast<char>(index));
		playpal.append(static_cast<char>(255 - index));
		playpal.append(static_cast<char>(128));
	}
	playpal.append(QByteArray(768 * 13, '\0'));
	QByteArray flat;
	for (int y = 0; y < 64; ++y) {
		for (int x = 0; x < 64; ++x) {
			flat.append(static_cast<char>((x + y) % 256));
		}
	}
	// A patch: width, height, offsets, a column table, then one post per column.
	QByteArray sprite;
	appendLe16(&sprite, 4);
	appendLe16(&sprite, 4);
	appendLe16(&sprite, 2);
	appendLe16(&sprite, 4);
	for (int column = 0; column < 4; ++column) {
		appendLe32(&sprite, static_cast<quint32>(8 + 16 + column * 9));
	}
	for (int column = 0; column < 4; ++column) {
		sprite.append(QByteArray("\x00\x04\x00\x07\x07\x07\x07\x00\xff", 9));
	}
	const QVector<std::pair<QByteArray, QByteArray>> lumps {{"DSTONE", tone}, {"DPBEEP", beep}, {"TEXTLUMP", QByteArray("hello")},
		{"PLAYPAL", playpal}, {"F_START", {}}, {"FLOOR0", flat}, {"F_END", {}}, {"S_START", {}}, {"TROOA1", sprite}, {"S_END", {}}};
	QByteArray body;
	QByteArray directory;
	for (const auto& lump : lumps) {
		appendLe32(&directory, static_cast<quint32>(12 + body.size()));
		appendLe32(&directory, static_cast<quint32>(lump.second.size()));
		appendFixed(&directory, lump.first, 8);
		body.append(lump.second);
	}
	QByteArray wad("PWAD");
	appendLe32(&wad, static_cast<quint32>(lumps.size()));
	appendLe32(&wad, static_cast<quint32>(12 + body.size()));
	wad.append(body);
	wad.append(directory);
	return writeFile(path, wad);
}

// A PWAD holding one small square room per map marker, in the Doom lump
// layouts (https://doomwiki.org/wiki/WAD, https://doomwiki.org/wiki/Thing,
// https://doomwiki.org/wiki/Linedef, https://doomwiki.org/wiki/Sidedef,
// https://doomwiki.org/wiki/Vertex, https://doomwiki.org/wiki/Sector).
bool buildDoomWad(const QString& path, const QStringList& maps)
{
	struct Lump {
		QByteArray name;
		QByteArray data;
	};
	QVector<Lump> lumps;
	for (const QString& map : maps) {
		QByteArray things;
		for (const int value : {0, 0, 90, 1, 7}) {
			appendI16(&things, value);
		}
		QByteArray vertexes;
		for (const auto& corner : {std::pair {-128, -128}, std::pair {128, -128}, std::pair {128, 128}, std::pair {-128, 128}}) {
			appendI16(&vertexes, corner.first);
			appendI16(&vertexes, corner.second);
		}
		QByteArray linedefs;
		QByteArray sidedefs;
		for (int edge = 0; edge < 4; ++edge) {
			for (const int value : {edge, (edge + 1) % 4, 1, 0, 0, edge, -1}) {
				appendI16(&linedefs, value);
			}
			appendI16(&sidedefs, 0);
			appendI16(&sidedefs, 0);
			appendFixed(&sidedefs, "-", 8);
			appendFixed(&sidedefs, "-", 8);
			appendFixed(&sidedefs, "STARTAN3", 8);
			appendI16(&sidedefs, 0);
		}
		QByteArray sectors;
		appendI16(&sectors, 0);
		appendI16(&sectors, 128);
		appendFixed(&sectors, "FLOOR4_8", 8);
		appendFixed(&sectors, "CEIL3_5", 8);
		for (const int value : {160, 0, 0}) {
			appendI16(&sectors, value);
		}
		lumps.push_back({map.toLatin1(), {}});
		lumps.push_back({"THINGS", things});
		lumps.push_back({"LINEDEFS", linedefs});
		lumps.push_back({"SIDEDEFS", sidedefs});
		lumps.push_back({"VERTEXES", vertexes});
		lumps.push_back({"SECTORS", sectors});
	}
	QByteArray body;
	QByteArray directory;
	for (const Lump& lump : lumps) {
		appendLe32(&directory, static_cast<quint32>(12 + body.size()));
		appendLe32(&directory, static_cast<quint32>(lump.data.size()));
		appendFixed(&directory, lump.name, 8);
		body.append(lump.data);
	}
	QByteArray wad("PWAD");
	appendLe32(&wad, static_cast<quint32>(lumps.size()));
	appendLe32(&wad, static_cast<quint32>(12 + body.size()));
	wad.append(body);
	wad.append(directory);
	return writeFile(path, wad);
}

struct Fixtures {
	QString project;
	QString doomWad;
	QString looseTexture;
	// A Quake installation whose "engine" is this program (see main()).
	QString gameRoot;
	QString package;
	QString map;
	QString shader;
	QString definitions;
	QString script;
	QString quakeC;
	// A Doom resource WAD of sounds and no maps.
	QString soundWad;
};

Fixtures buildFixtures(const QString& root)
{
	auto brick = [](int x, int y) {
		const bool mortar = (y % 8) < 1 || ((x + ((y / 8) % 2) * 8) % 16) < 1;
		return mortar ? qRgb(120, 116, 108) : qRgb(150, 70, 52);
	};
	auto metal = [](int x, int y) {
		return qRgb(104 + (x * 3) % 20, 110 + (y * 5) % 16, 118);
	};
	auto skin = [](int x, int y) {
		return (x % 16 < 1 || y % 16 < 1) ? qRgb(60, 44, 30) : qRgb(170, 126, 70);
	};
	const QByteArray shaderText = QByteArrayLiteral(
		"textures/test/wall\n{\n\tqer_editorimage textures/base/brick_wall.tga\n\t{\n\t\tmap textures/base/brick_wall.tga\n\t}\n}\n"
		"textures/test/missing\n{\n\t{\n\t\tmap textures/test/not_in_package.tga\n\t\tblendFunc GL_ONE GL_ONE\n\t}\n}\n");

	Fixtures fixtures;
	fixtures.looseTexture = QDir(root).filePath(QStringLiteral("loose/stone.tga"));
	writeFile(fixtures.looseTexture, buildTarga(16, 16, [](int x, int y) {
		return ((x / 4 + y / 4) % 2) ? qRgb(120, 120, 120) : qRgb(90, 90, 90);
	}));
	fixtures.doomWad = QDir(root).filePath(QStringLiteral("maps/rooms.wad"));
	buildDoomWad(fixtures.doomWad, {QStringLiteral("MAP01"), QStringLiteral("MAP02")});
	fixtures.gameRoot = QDir(root).filePath(QStringLiteral("game"));
	QDir().mkpath(QDir(fixtures.gameRoot).filePath(QStringLiteral("id1")));
	fixtures.project = QDir(root).filePath(QStringLiteral("project"));
	QDir().mkpath(fixtures.project);
	saveProjectManifest(defaultProjectManifest(fixtures.project, QStringLiteral("Interaction Test")));
	fixtures.package = QDir(root).filePath(QStringLiteral("assets.pk3"));
	buildStoredZip(fixtures.package, {
		{QStringLiteral("textures/base/brick_wall.tga"), buildTarga(32, 32, brick)},
		{QStringLiteral("textures/base/metal_panel.tga"), buildTarga(32, 32, metal)},
		{QStringLiteral("models/crate/crate.md3"), buildMd3Cube()},
		{QStringLiteral("models/crate/skin.tga"), buildTarga(32, 32, skin)},
		{QStringLiteral("sound/ambience/hum.wav"), buildWav(4410, 22050)},
		{QStringLiteral("scripts/test.shader"), shaderText},
		{QStringLiteral("scripts/autoexec.cfg"), QByteArrayLiteral("// test config\nseta r_mode \"-1\"\n")},
	});

	fixtures.map = QDir(root).filePath(QStringLiteral("maps/test.map"));
	writeFile(fixtures.map, QByteArrayLiteral(
		"{\n\"classname\" \"worldspawn\"\n"
		"{\n( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) base/wall 0 0 0 1 1\n"
		"( 64 -64 -16 ) ( 64 -64 -15 ) ( 64 -63 -16 ) base/wall 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) base/wall 0 0 0 1 1\n"
		"( -64 64 -16 ) ( -63 64 -16 ) ( -64 64 -15 ) base/wall 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) base/floor 0 0 0 1 1\n"
		"( -64 -64 0 ) ( -64 -63 0 ) ( -63 -64 0 ) base/floor 0 0 0 1 1\n}\n}\n"
		"{\n\"classname\" \"info_player_start\"\n\"origin\" \"0 0 24\"\n\"angle\" \"90\"\n}\n"
		"{\n\"classname\" \"light\"\n\"origin\" \"0 0 96\"\n\"light\" \"300\"\n}\n"
		"{\n\"classname\" \"item_not_defined\"\n\"origin\" \"32 32 24\"\n}\n"));

	fixtures.shader = QDir(root).filePath(QStringLiteral("scripts/test.shader"));
	writeFile(fixtures.shader, shaderText);

	fixtures.definitions = QDir(root).filePath(QStringLiteral("defs/test.def"));
	writeFile(fixtures.definitions, QByteArrayLiteral(
		"/*QUAKED info_player_start (1 0 0) (-16 -16 -24) (16 16 32) SUSPENDED\nPlayer spawn point.\n*/\n"
		"/*QUAKED light (0 1 0) (-8 -8 -8) (8 8 8) START_OFF\nA light.\n\"light\" brightness\n*/\n"
		"/*QUAKED worldspawn (0 0 0) ?\nThe world.\n*/\n"));

	fixtures.quakeC = QDir(root).filePath(QStringLiteral("progs/player.qc"));
	writeFile(fixtures.quakeC, QByteArrayLiteral(
		"// player movement\n"
		"void() player_stand = {\n\tself.frame = 0;\n};\n"
		"\n"
		"void() player_run = {\n\tself.frame = 6;\n};\n"));
	fixtures.script = QDir(root).filePath(QStringLiteral("scripts/autoexec.cfg"));
	writeFile(fixtures.script, QByteArrayLiteral("// test config\nseta r_mode \"-1\"\nseta com_hunkmegs \"128\"\n"));
	fixtures.soundWad = QDir(root).filePath(QStringLiteral("sounds/doomsnd.wad"));
	buildSoundWad(fixtures.soundWad);
	return fixtures;
}

// ---------------------------------------------------------------------------
// Shell helpers
// ---------------------------------------------------------------------------

void settle()
{
	for (int pass = 0; pass < 3; ++pass) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
	}
}

void settleImage(ImagePreviewView* preview)
{
	QElapsedTimer timer; timer.start();
	while (preview && !preview->hasImage() && timer.elapsed() < 5000) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

// Modal prompts would block a scripted run. This answers the next modal
// dialog once it is actually showing, retrying for a few seconds so it works
// whether the dialog opens straight away or after some work.
void whenDialogShows(std::function<void(QWidget*)> answer, int attempts = 300)
{
	QTimer::singleShot(10, [answer, attempts]() {
		if (QWidget* modal = QApplication::activeModalWidget(); modal && modal->isVisible()) {
			answer(modal);
			return;
		}
		if (attempts > 0) {
			whenDialogShows(answer, attempts - 1);
		}
	});
}

// Answers the modal dialog with this object name once it shows, for a dialog
// opened from inside another one, which whenDialogShows would answer first.
void whenNamedDialogShows(const QString& name, std::function<void(QWidget*)> answer, int attempts = 300)
{
	QTimer::singleShot(10, [name, answer, attempts]() {
		if (QWidget* modal = QApplication::activeModalWidget(); modal && modal->isVisible() && modal->objectName() == name) {
			answer(modal);
			return;
		}
		if (attempts > 0) {
			whenNamedDialogShows(name, answer, attempts - 1);
		}
	});
}

// A context menu runs its own event loop, like a modal dialog. This answers
// the next popup menu once it is showing.
void whenMenuShows(std::function<void(QMenu*)> answer, int attempts = 300)
{
	QTimer::singleShot(10, [answer, attempts]() {
		if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()); menu && menu->isVisible()) {
			answer(menu);
			return;
		}
		if (attempts > 0) {
			whenMenuShows(answer, attempts - 1);
		}
	});
}

QAction* menuAction(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions()) {
		if (action->text() == text) {
			return action;
		}
		if (action->menu()) {
			if (QAction* nested = menuAction(action->menu(), text)) {
				return nested;
			}
		}
	}
	return nullptr;
}

// Every action in a menu and its submenus, in order.
QList<QAction*> menuActions(QMenu* menu)
{
	QList<QAction*> actions;
	for (QAction* action : menu->actions()) {
		actions << action;
		if (action->menu()) {
			actions << menuActions(action->menu());
		}
	}
	return actions;
}

// Chooses an action the way a keyboard user would, so the menu's exec()
// returns it.
void chooseMenuAction(QMenu* menu, QAction* action)
{
	menu->setActiveAction(action);
	QTest::keyClick(menu, Qt::Key_Return);
}

// A dialog nobody expected would block the run forever. This fails the run
// instead: any modal dialog left open for several seconds is reported with its
// title and text, then closed.
void installDialogWatchdog()
{
	auto* timer = new QTimer(qApp);
	auto* seen = new QPointer<QWidget>();
	auto* since = new QElapsedTimer();
	QObject::connect(timer, &QTimer::timeout, [seen, since]() {
		QWidget* modal = QApplication::activeModalWidget();
		if (!modal || !modal->isVisible()) {
			*seen = nullptr;
			return;
		}
		if (seen->data() != modal) {
			*seen = modal;
			since->start();
			return;
		}
		if (since->elapsed() < 4000) {
			return;
		}
		QString text = modal->windowTitle();
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			text += QStringLiteral(": ") + box->text();
		}
		std::cerr << "FAIL: an unexpected dialog stayed open: " << text.toStdString() << "\n";
		++g_failures;
		if (auto* dialog = qobject_cast<QDialog*>(modal)) {
			dialog->reject();
		} else {
			modal->close();
		}
		*seen = nullptr;
	});
	timer->start(250);
}

QString withoutMnemonic(QString text)
{
	text.remove(QLatin1Char('&'));
	return text;
}

// Clicks the dialog button whose text reads `text` (mnemonics ignored).
void clickButton(QWidget* dialog, const QString& text)
{
	for (QAbstractButton* button : dialog->findChildren<QAbstractButton*>()) {
		if (withoutMnemonic(button->text()).compare(text, Qt::CaseInsensitive) == 0) {
			button->click();
			return;
		}
	}
	std::cerr << "No dialog button reads " << text.toStdString() << "\n";
	if (auto* box = qobject_cast<QDialog*>(dialog)) {
		box->reject();
	}
}

void wait(int milliseconds)
{
	QElapsedTimer clock;
	clock.start();
	while (clock.elapsed() < milliseconds) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QTest::qSleep(10);
	}
}

template <typename T>
T* child(ApplicationShell& shell, const char* name)
{
	return shell.findChild<T*>(QString::fromLatin1(name));
}

int currentPage(ApplicationShell& shell)
{
	auto* stack = child<QStackedWidget>(shell, "modeStack");
	return stack ? stack->currentIndex() : -1;
}

QWidget* page(ApplicationShell& shell, int index)
{
	auto* stack = child<QStackedWidget>(shell, "modeStack");
	return stack ? stack->widget(index) : nullptr;
}

void checkLevelLinks(ApplicationShell& shell, const Fixtures& fixtures);
void unstageAll(ApplicationShell& shell);
void checkListsAndRecents(ApplicationShell& shell, const Fixtures& fixtures);
void checkViewerControls(ApplicationShell& shell);
void checkMapContextMenu(ApplicationShell& shell);
void checkMapAddDelete(ApplicationShell& shell);
void checkTargetLinks(ApplicationShell& shell);
void checkReplaceTexture(ApplicationShell& shell);
void checkHideShow(ApplicationShell& shell);
void checkMapSelectionAndResize(ApplicationShell& shell);
void checkMapClip(ApplicationShell& shell);
void checkCloseMap(ApplicationShell& shell, const Fixtures& fixtures);
void checkWadMaps(ApplicationShell& shell, const Fixtures& fixtures);
void checkQuickOpen(ApplicationShell& shell, const Fixtures& fixtures);
void checkCodeLineEditing(ApplicationShell& shell);
void checkCodeTabs(ApplicationShell& shell, const Fixtures& fixtures);
void checkGoToSymbol(ApplicationShell& shell, const Fixtures& fixtures);
void checkLooseAssetsAndStagedGuard(ApplicationShell& shell, const Fixtures& fixtures);
void checkAudioBrowser(ApplicationShell& shell, const Fixtures& fixtures);
void checkWadTextures(ApplicationShell& shell);
void checkPaletteRecents(ApplicationShell& shell);
void checkPackageDropStaging(ApplicationShell& shell, const Fixtures& fixtures);
void checkCrashRecovery(ApplicationShell& shell, const Fixtures& fixtures);
void checkCodeZoom(ApplicationShell& shell, const Fixtures& fixtures);
void checkCodeTabFixes(ApplicationShell& shell, const Fixtures& fixtures);
void checkCodeTreeFollowsTab(ApplicationShell& shell, const Fixtures& fixtures);
void checkNavigationRail(ApplicationShell& shell);
void checkEditorProfiles(ApplicationShell& shell, const Fixtures& fixtures);
void checkAssistant(ApplicationShell& shell, const Fixtures& fixtures);
void checkCameraDepth(ApplicationShell& shell, const Fixtures& fixtures);
void checkExternalChanges(ApplicationShell& shell, const Fixtures& fixtures);
void checkInstallationPalette(ApplicationShell& shell, const Fixtures& fixtures);
void checkDoomTagLinks(const Fixtures& fixtures);
void checkSessionRestore(ApplicationShell& shell, const Fixtures& fixtures);
void checkShortcutReference(ApplicationShell& shell);
void checkCustomShortcuts(ApplicationShell& shell);
void checkPanelTabFitting();
void checkHeaderFolding();
void checkReflowGrid();
void checkShortcutRules(ApplicationShell& shell);
void checkCrashReportPreference(ApplicationShell& shell);
void checkLevelTextures(ApplicationShell& shell, const Fixtures& fixtures);
void checkObjectQuery(ApplicationShell& shell, const Fixtures& fixtures);
void checkPackageQuery(ApplicationShell& shell, const Fixtures& fixtures);
void checkShaderQuery(ApplicationShell& shell, const Fixtures& fixtures);
void checkGoToDefinition(ApplicationShell& shell, const Fixtures& fixtures);
void checkSettingsSearch(ApplicationShell& shell);
void checkToolBarLabels(ApplicationShell& shell);
void checkStatusBarFolding();
void checkCodeFolding(ApplicationShell& shell, const Fixtures& fixtures);
void checkCodeBreadcrumb(ApplicationShell& shell, const Fixtures& fixtures);
void checkNextProblem(ApplicationShell& shell, const Fixtures& fixtures);
void checkUseHighlights(ApplicationShell& shell, const Fixtures& fixtures);
void checkStickyHeaders(ApplicationShell& shell, const Fixtures& fixtures);
void checkSecondaryInstance(ApplicationShell& shell, const Fixtures& fixtures);
void checkDropNames(ApplicationShell& shell, const Fixtures& fixtures);
void checkBuildLoop(ApplicationShell& shell, const Fixtures& fixtures);
void checkLargeSelection(ApplicationShell& shell, const Fixtures& fixtures);
void checkDiagnosticHighlightCost();
void checkPlaceHistory(ApplicationShell& shell, const Fixtures& fixtures);
void checkMultiEntityKeys(ApplicationShell& shell, const Fixtures& fixtures);

// The Settings page's stack of category pages.
QStackedWidget* settingsCategoryPages(ApplicationShell& shell)
{
	for (QStackedWidget* stack : shell.findChildren<QStackedWidget*>()) {
		if (stack->accessibleName() == QStringLiteral("Settings category pages")) {
			return stack;
		}
	}
	return nullptr;
}

// Page indices, in rail order (StudioMode).
enum Page { Workspace, Levels, Models, Textures, Audio, Packages, Code, Shaders, Build, Settings };

QAction* action(ApplicationShell& shell, const char* commandId)
{
	return shell.findChild<QAction*>(QString::fromLatin1(commandId));
}

// Runs a command the way its menu item would.
bool trigger(ApplicationShell& shell, const char* commandId)
{
	QAction* target = action(shell, commandId);
	if (!target || !target->isEnabled()) {
		std::cerr << "Command unavailable: " << commandId << "\n";
		return false;
	}
	target->trigger();
	settle();
	return true;
}

// Sends a key to whatever has keyboard focus, through Qt's shortcut handling,
// as a real key press would.
void press(ApplicationShell& shell, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QWidget* target = QApplication::focusWidget();
	if (!target) {
		target = &shell;
	}
	QTest::keyClick(target, static_cast<Qt::Key>(key), modifiers);
	settle();
}

bool focusIsOn(ApplicationShell& shell, int pageIndex)
{
	QWidget* focus = QApplication::focusWidget();
	QWidget* host = page(shell, pageIndex);
	return focus && host && host->isAncestorOf(focus);
}

QAbstractButton* chip(ApplicationShell& shell, const QString& accessibleName)
{
	for (QAbstractButton* button : shell.statusBar()->findChildren<QAbstractButton*>()) {
		if (button->accessibleName() == accessibleName) {
			return button;
		}
	}
	return nullptr;
}

QString chipText(ApplicationShell& shell, const QString& accessibleName)
{
	QAbstractButton* button = chip(shell, accessibleName);
	return button ? button->text() : QString();
}

QString currentPath(LevelObjectList* list)
{
	return list ? list->currentIndex().data(Qt::UserRole).toString() : QString();
}

void activateRow(LevelObjectList* list, int row)
{
	setObjectCurrentRow(list, row);
	Q_EMIT list->activated(objectIndex(list, row));
	settle();
}

int rowWithData(LevelObjectList* list, int role, const QVariant& value)
{
	for (int row = 0; list && row < list->model()->rowCount(); ++row) {
		if (objectIndex(list, row).data(role) == value) { return row; }
	}
	return -1;
}

QString currentPath(QListWidget* list)
{
	QListWidgetItem* item = list ? list->currentItem() : nullptr;
	return item ? item->data(Qt::UserRole).toString() : QString();
}

// Presses Enter on a list row the way a keyboard user activates it.
void activateRow(QListWidget* list, int row)
{
	list->setCurrentRow(row);
	Q_EMIT list->itemActivated(list->item(row));
	settle();
}

bool sameFile(const QString& left, const QString& right)
{
	const QString a = QFileInfo(left).canonicalFilePath();
	return !a.isEmpty() && a.compare(QFileInfo(right).canonicalFilePath(), Qt::CaseInsensitive) == 0;
}

int rowWithData(QListWidget* list, int role, const QVariant& value)
{
	for (int row = 0; list && row < list->count(); ++row) {
		if (list->item(row)->data(role) == value) {
			return row;
		}
	}
	return -1;
}

QTreeWidgetItem* findTreeItem(QTreeWidget* tree, int role, const QString& value)
{
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		if ((*it)->data(0, role).toString() == value) {
			return *it;
		}
	}
	return nullptr;
}

// Set VIBESTUDIO_TEST_SNAPSHOTS to a folder to save a picture of the window at
// points only interaction reaches (a finished build, a located tool), for
// visual review. Normal runs save nothing.
void snapshot(ApplicationShell& shell, const char* name)
{
	const QString directory = qEnvironmentVariable("VIBESTUDIO_TEST_SNAPSHOTS");
	if (directory.isEmpty()) {
		return;
	}
	QDir().mkpath(directory);
	settle();
	shell.grab().save(QDir(directory).filePath(QString::fromLatin1(name) + QStringLiteral(".png")));
}

// The same for a dialog or other top-level widget.
void snapshotWidget(QWidget* widget, const char* name)
{
	const QString directory = qEnvironmentVariable("VIBESTUDIO_TEST_SNAPSHOTS");
	if (directory.isEmpty() || !widget) {
		return;
	}
	QDir().mkpath(directory);
	settle();
	widget->grab().save(QDir(directory).filePath(QString::fromLatin1(name) + QStringLiteral(".png")));
}

// A dialog closing can leave no window active for a moment, and window-wide
// shortcuts only fire in the active window, so every check starts here.
// VIBESTUDIO_TEST_ONLY=checkA,checkB runs only the named checks, for quick
// iteration on one feature; unset, every check runs. A check that relies on
// an earlier one's state needs that one named too.
bool checkSelected(const char* name)
{
	static const QStringList only = qEnvironmentVariable("VIBESTUDIO_TEST_ONLY").split(QLatin1Char(','), Qt::SkipEmptyParts);
	return only.isEmpty() || only.contains(QLatin1String(name));
}

#define RUN_CHECK(name, ...) \
	do { \
		if (checkSelected(#name)) { \
			name(__VA_ARGS__); \
		} \
	} while (false)

void ensureActive(ApplicationShell& shell, const char* stage)
{
	std::cout << "-- " << stage << "\n" << std::flush;
	shell.activateWindow();
	shell.raise();
	if (!QTest::qWaitForWindowActive(&shell, 2000)) {
		std::cerr << "The shell window did not become active for " << stage << "\n";
	}
	settle();
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

void checkCommandRegistry(ApplicationShell& shell)
{
	ensureActive(shell, "checkCommandRegistry");
	QAction* run = action(shell, "build.runPipeline");
	check(run && run->shortcut() == QKeySequence(Qt::Key_F7), "Run Build Pipeline should be bound to F7.");
	QAction* buildAndLaunch = action(shell, "build.runAndLaunch");
	check(buildAndLaunch && buildAndLaunch->shortcut() == QKeySequence(Qt::Key_F5), "Build and Launch should be bound to F5.");
	QAction* detect = action(shell, "game.detectInstallations");
	check(detect && detect->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+G")), "Detect Game Installations should be bound to Ctrl+Shift+G.");
	QAction* save = action(shell, "code.save");
	check(save && save->shortcut() == QKeySequence(QStringLiteral("Ctrl+S")), "Save File should be bound to Ctrl+S.");
	check(save && save->shortcutContext() == Qt::WidgetWithChildrenShortcut, "Ctrl+S should only act on the Code page.");
	QAction* undo = action(shell, "map.undo");
	check(undo && undo->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z")) && undo->shortcutContext() == Qt::WidgetWithChildrenShortcut,
		"Map undo should be Ctrl+Z, scoped to the Levels page.");
	QAction* stageDelete = action(shell, "package.stageDelete");
	check(stageDelete && stageDelete->shortcutContext() == Qt::WidgetWithChildrenShortcut, "Del should only stage deletions from the Packages page.");
	// Surface keys may share a sequence: Ctrl+Z is map undo on Levels and takes
	// a staged change back on Packages.
	QAction* unstageLast = action(shell, "package.unstageLast");
	check(unstageLast && unstageLast->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z"))
			&& unstageLast->shortcutContext() == Qt::WidgetWithChildrenShortcut,
		"Undo Staged Change should share Ctrl+Z with map undo, scoped to the Packages page.");
	for (const char* id : {"project.copyManifest", "project.validate", "localization.report", "ai.review", "code.goToLine", "code.findNext"}) {
		check(action(shell, id) != nullptr, "Every documented palette command should be registered.");
	}

	// The palette lists each command once, without menu mnemonics.
	check(trigger(shell, "shell.commandPalette"), "The command palette should open.");
	auto* list = shell.findChild<QListWidget*>(QStringLiteral("commandPaletteList"));
	check(list != nullptr && list->count() > 20, "The palette should list the registry.");
	if (list) {
		int runRows = 0;
		bool mnemonic = false;
		for (int row = 0; row < list->count(); ++row) {
			const QString text = list->item(row)->text();
			mnemonic = mnemonic || text.contains(QLatin1Char('&'));
			runRows += text.startsWith(QStringLiteral("Run Build Pipeline")) ? 1 : 0;
		}
		check(!mnemonic, "Palette labels should not show menu mnemonics.");
		check(runRows == 1, "Run Build Pipeline should appear once in the palette, not as a working and a dimmed copy.");
	}
	if (auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("commandPaletteFilter"))) {
		QTest::keyClick(filter, Qt::Key_Escape);
		settle();
	}
}

void checkFindAndEscape(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkFindAndEscape");
	shell.openPathFromCommandLine(fixtures.package);
	settle();
	check(currentPage(shell) == Packages, "Opening a package should show the Packages page.");
	check(chipText(shell, QStringLiteral("Package status")).contains(QStringLiteral("assets.pk3")), "The package chip should name the open package.");

	check(trigger(shell, "shell.mode.textures"), "Ctrl+4 should switch to Textures.");
	check(focusIsOn(shell, Textures), "Switching surfaces should move keyboard focus onto the new page.");
	press(shell, Qt::Key_F, Qt::ControlModifier);
	auto* filter = child<QLineEdit>(shell, "textureFilter");
	check(filter && QApplication::focusWidget() == filter, "Ctrl+F on Textures should focus the texture filter.");
	if (filter) {
		QTest::keyClicks(filter, QStringLiteral("brick"));
		settle();
		press(shell, Qt::Key_Escape);
		check(filter->text().isEmpty(), "Escape should clear a filter field.");
	}
}

void checkPackageKeysStayOnPackages(ApplicationShell& shell)
{
	ensureActive(shell, "checkPackageKeysStayOnPackages");
	trigger(shell, "shell.mode.packages");
	auto* filter = child<QLineEdit>(shell, "packageFilter");
	auto entries = tests::PackageRows(child<PackageEntryView>(shell, "packageEntries"));
	QAction* saveAs = action(shell, "package.saveAs");
	check(filter && entries && saveAs, "The package page should expose its filter, entry list, and Save As.");
	if (!filter || !entries || !saveAs) {
		return;
	}
	filter->setText(QStringLiteral("metal_panel"));
	settle();
	check(entries->count() >= 1, "Filtering should find the metal panel texture.");
	entries->setCurrentRow(0);
	settle();
	check(!saveAs->isEnabled(), "Nothing is staged yet.");

	// Del on another page must leave the package alone.
	trigger(shell, "shell.mode.levels");
	press(shell, Qt::Key_Delete);
	check(!saveAs->isEnabled(), "Del on the Levels page must not stage a package deletion.");

	trigger(shell, "shell.mode.packages");
	entries->setFocus();
	press(shell, Qt::Key_Delete);
	check(saveAs->isEnabled(), "Del in the package entry list should stage the deletion.");
	check(chipText(shell, QStringLiteral("Package status")).contains(QStringLiteral("staged")), "The package chip should report staged changes.");

	// The Preview tab shows pixels for an image and text for a script.
	auto* previews = child<QStackedWidget>(shell, "packagePreviewStack");
	filter->setText(QStringLiteral("brick_wall"));
	settle();
	entries->setCurrentRow(0);
	settle();
	QElapsedTimer imageWait; imageWait.start();
	while (previews && previews->currentIndex() != 1 && imageWait.elapsed() < 5000) { settle(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
	check(previews && previews->currentIndex() == 1, "Selecting a texture should preview its pixels.");
	filter->setText(QStringLiteral("autoexec"));
	settle();
	entries->setCurrentRow(0);
	settle();
	QElapsedTimer textWait; textWait.start();
	while (previews && previews->currentIndex() != 2 && textWait.elapsed() < 5000) { settle(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
	check(previews && previews->currentIndex() == 2, "Selecting a text entry should preview its text.");
	filter->clear();
	settle();
}

void checkCodeEditor(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCodeEditor");
	shell.openPathFromCommandLine(fixtures.script);
	settle();
	check(currentPage(shell) == Code, "Opening a script should show the Code page.");
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	check(editor && editor->toPlainText().contains(QStringLiteral("seta r_mode")), "The editor should show the script.");
	if (!editor) {
		return;
	}

	// Ctrl+S saves without the watcher mistaking the save for an outside edit.
	editor->setFocus();
	editor->moveCursor(QTextCursor::End);
	// QTest::keyClicks has no key for "\n", so the line break is its own key.
	QTest::keyClicks(editor, QStringLiteral("// saved from the test"));
	QTest::keyClick(editor, Qt::Key_Return);
	settle();
	press(shell, Qt::Key_S, Qt::ControlModifier);
	QFile saved(fixtures.script);
	check(saved.open(QIODevice::ReadOnly) && saved.readAll().contains("// saved from the test"), "Ctrl+S should write the edit to disk.");
	saved.close();
	wait(1800);
	check(editor->document()->isUndoAvailable(), "The studio's own save must not reload the editor and wipe its undo history.");

	// Opening the file that is already open keeps unsaved edits.
	QTest::keyClicks(editor, QStringLiteral("unsaved"));
	settle();
	shell.openPathFromCommandLine(fixtures.script);
	settle();
	check(editor->toPlainText().contains(QStringLiteral("unsaved")), "Reopening the open file must not discard unsaved edits.");
	editor->undo();
	settle();

	// Ctrl+F finds in the file; Enter moves to the next match.
	editor->moveCursor(QTextCursor::Start);
	press(shell, Qt::Key_F, Qt::ControlModifier);
	auto* findField = shell.findChild<QLineEdit*>(QStringLiteral("codeFindField"));
	check(findField && findField->isVisible() && QApplication::focusWidget() == findField, "Ctrl+F on the Code page should open the find bar.");
	if (findField) {
		QTest::keyClicks(findField, QStringLiteral("seta"));
		settle();
		check(editor->textCursor().selectedText() == QStringLiteral("seta"), "Typing in the find bar should select the first match.");
		const int first = editor->textCursor().selectionStart();
		QTest::keyClick(findField, Qt::Key_Return);
		settle();
		check(editor->textCursor().selectedText() == QStringLiteral("seta") && editor->textCursor().selectionStart() != first,
			"Enter should move to the next match.");
		QTest::keyClick(findField, Qt::Key_Escape);
		settle();
		check(!findField->isVisible() && QApplication::focusWidget() == editor, "Escape should close the find bar and return to the editor.");
	}

	// Ctrl+L asks for a line and moves the caret there.
	whenDialogShows([](QWidget* dialog) {
		if (auto* spin = dialog->findChild<QSpinBox*>()) {
			spin->setValue(3);
		}
		if (auto* input = qobject_cast<QDialog*>(dialog)) {
			input->accept();
		}
	});
	press(shell, Qt::Key_L, Qt::ControlModifier);
	check(editor->textCursor().blockNumber() == 2, "Go To Line should put the caret on the chosen line.");
}

void checkNavigation(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkNavigation");
	auto entries = tests::PackageRows(child<PackageEntryView>(shell, "packageEntries"));
	auto* filter = child<QLineEdit>(shell, "packageFilter");
	auto* textures = child<QListWidget>(shell, "textureEntries");
	check(entries && filter && textures, "The package and texture lists should exist.");
	if (!entries || !filter || !textures) {
		return;
	}

	// Status chips are buttons that open the surface behind them.
	QAbstractButton* packageChip = chip(shell, QStringLiteral("Package status"));
	check(packageChip != nullptr, "The package chip should be a button.");
	trigger(shell, "shell.mode.workspace");
	snapshot(shell, "tour-workspace");
	if (packageChip) {
		packageChip->click();
		settle();
	}
	check(currentPage(shell) == Packages, "Clicking the package chip should open Packages.");
	if (QAbstractButton* aiChip = chip(shell, QStringLiteral("AI status"))) {
		aiChip->click();
		settle();
		auto* assistant = shell.findChild<QDockWidget*>(QStringLiteral("assistantDock"));
		check(assistant && assistant->isVisible(), "The AI chip should open the Assistant.");
		// Its Connection button leads on to the AI settings.
		if (auto* connection = child<QToolButton>(shell, "assistantSettings")) {
			connection->click();
			settle();
		}
		auto* categories = child<QListWidget>(shell, "settingsCategories");
		check(currentPage(shell) == Settings && categories && categories->currentRow() == 2, "The Assistant's Connection button should open the AI settings.");
		snapshot(shell, "tour-settings");
		if (assistant) {
			assistant->hide();
			settle();
		}
	}
	if (QAbstractButton* compilerChip = chip(shell, QStringLiteral("Compiler status"))) {
		compilerChip->click();
		settle();
		auto* sections = child<QTabWidget>(shell, "buildSections");
		check(currentPage(shell) == Build && sections && sections->currentIndex() == 1, "The compiler chip should open the Build toolchain tab.");
	}

	// A texture entry in the package opens on the Textures surface.
	trigger(shell, "shell.mode.packages");
	filter->setText(QStringLiteral("brick_wall"));
	settle();
	check(entries->count() == 1, "The filter should find the one brick texture.");
	snapshot(shell, "tour-packages");
	if (entries->count() == 1) {
		activateRow(entries, 0);
		snapshot(shell, "tour-textures");
		check(currentPage(shell) == Textures && currentPath(textures) == QStringLiteral("textures/base/brick_wall.tga"),
			"Opening a texture entry should select it on the Textures surface.");
	}

	// A script entry opens as a read-only copy in the code editor.
	trigger(shell, "shell.mode.packages");
	filter->setText(QStringLiteral("autoexec"));
	settle();
	if (entries->count() >= 1) {
		activateRow(entries, 0);
		auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
		check(currentPage(shell) == Code && editor && editor->isReadOnly() && editor->toPlainText().contains(QStringLiteral("seta r_mode")),
			"Opening a script entry should show a read-only copy in the code editor.");
		snapshot(shell, "tour-code");
	}
	trigger(shell, "shell.mode.packages");
	filter->clear();
	settle();

	// Shader texture references lead to the texture; shader rows to the script.
	shell.openPathFromCommandLine(fixtures.shader);
	settle();
	auto* graph = child<QTreeWidget>(shell, "shaderGraph");
	check(currentPage(shell) == Shaders && graph, "Opening a shader script should show the Shaders surface.");
	snapshot(shell, "tour-shaders");
	if (graph) {
		QTreeWidgetItem* found = findTreeItem(graph, Qt::UserRole + 2, QStringLiteral("textures/base/brick_wall.tga"));
		check(found != nullptr, "The shader tree should list the brick texture reference.");
		if (found) {
			graph->setCurrentItem(found);
			Q_EMIT graph->itemActivated(found, 0);
			settle();
			check(currentPage(shell) == Textures && currentPath(textures) == QStringLiteral("textures/base/brick_wall.tga"),
				"Activating a found texture reference should show it on the Textures surface.");
		}
		trigger(shell, "shell.mode.shaders");
		QTreeWidgetItem* shaderRow = findTreeItem(graph, Qt::UserRole, QStringLiteral("textures/test/missing"));
		if (shaderRow) {
			graph->setCurrentItem(shaderRow);
			Q_EMIT graph->itemActivated(shaderRow, 0);
			settle();
			auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
			check(currentPage(shell) == Code && editor && editor->textCursor().blockNumber() > 0,
				"Activating a shader should open its script at the shader's line.");
		} else {
			check(false, "The shader tree should list textures/test/missing.");
		}
	}

	// A model's skin row leads to the skin texture.
	trigger(shell, "shell.mode.models");
	snapshot(shell, "tour-models");
	auto* details = child<QTreeWidget>(shell, "modelDetails");
	QTreeWidgetItem* skinRow = details ? findTreeItem(details, Qt::UserRole + 7, QStringLiteral("models/crate/skin.tga")) : nullptr;
	check(skinRow != nullptr, "The model inspector should list the crate's skin.");
	if (details && skinRow) {
		Q_EMIT details->itemActivated(skinRow, 1);
		settle();
		check(currentPage(shell) == Textures && currentPath(textures) == QStringLiteral("models/crate/skin.tga"),
			"Activating a skin should show it on the Textures surface.");
	}

	// Preferences opens on the appearance settings.
	trigger(shell, "app.preferences");
	auto* categories = child<QListWidget>(shell, "settingsCategories");
	check(currentPage(shell) == Settings && categories && categories->currentRow() == 1, "Preferences should open the appearance settings.");
}

void checkMapGuards(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkMapGuards");
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	check(currentPage(shell) == Levels, "Opening a map should show the Levels page.");
	auto* viewport = child<QWidget>(shell, "mapViewport");
	QAction* undo = action(shell, "map.undo");
	check(viewport && undo, "The Levels page should expose its viewport and undo.");
	if (!viewport || !undo) {
		return;
	}
	viewport->setFocus();
	press(shell, Qt::Key_Tab);
	press(shell, Qt::Key_Right);
	check(undo->isEnabled(), "Nudging a selected object should record an undoable edit.");
	press(shell, Qt::Key_Z, Qt::ControlModifier);
	check(!undo->isEnabled(), "Ctrl+Z on the Levels page should undo the map edit.");
	press(shell, Qt::Key_Right);
	check(undo->isEnabled(), "A second nudge should be undoable again.");

	// Reloading with unsaved edits asks first; Cancel keeps them.
	auto* path = child<QLineEdit>(shell, "levelMapPath");
	check(path != nullptr, "The map path field should exist.");
	if (!path) {
		return;
	}
	whenDialogShows([](QWidget* dialog) {
		clickButton(dialog, QStringLiteral("Cancel"));
	});
	path->setFocus();
	QTest::keyClick(path, Qt::Key_Return);
	settle();
	check(undo->isEnabled(), "Cancelling the unsaved-edits prompt should keep the edits.");
	whenDialogShows([](QWidget* dialog) {
		clickButton(dialog, QStringLiteral("Discard Edits"));
	});
	QTest::keyClick(path, Qt::Key_Return);
	settle();
	check(!undo->isEnabled(), "Discarding should reload the map from disk.");
}

int visibleRows(QListWidget* list)
{
	int count = 0;
	for (int row = 0; list && row < list->count(); ++row) {
		count += list->item(row)->isHidden() ? 0 : 1;
	}
	return count;
}

void checkListsAndRecents(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkListsAndRecents");

	// Open Recent lists what this session opened.
	auto* recent = shell.findChild<QMenu*>(QStringLiteral("menu-recent"));
	check(recent != nullptr, "File should have an Open Recent menu.");
	if (recent) {
		Q_EMIT recent->aboutToShow();
		QStringList names;
		for (QAction* action : recent->actions()) {
			names << action->text();
		}
		check(names.contains(QStringLiteral("assets.pk3")), "Open Recent should list the package.");
		check(names.contains(QStringLiteral("test.map")), "Open Recent should list the map.");
		check(names.contains(QStringLiteral("autoexec.cfg")), "Open Recent should list the script.");
	}

	// Close Project closes the project and leaves it among the recents.
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	check(chipText(shell, QStringLiteral("Project status")).contains(QStringLiteral("project")), "Opening the project folder should show it on the project chip.");
	check(trigger(shell, "project.close"), "Close Project should be available with a project open.");
	check(chipText(shell, QStringLiteral("Project status")).contains(QStringLiteral("No project")), "Close Project should leave no project open.");

	// The new filters narrow their lists as the user types.
	trigger(shell, "shell.mode.models");
	auto* modelFilter = child<QLineEdit>(shell, "modelFilter");
	auto* models = child<QListWidget>(shell, "modelEntries");
	check(modelFilter && models, "The Models surface should have a filter.");
	if (modelFilter && models) {
		press(shell, Qt::Key_F, Qt::ControlModifier);
		check(QApplication::focusWidget() == modelFilter, "Ctrl+F on Models should focus the model filter.");
		QTest::keyClicks(modelFilter, QStringLiteral("nothing-matches-this"));
		settle();
		check(visibleRows(models) == 0, "A filter that matches nothing should hide every model.");
		press(shell, Qt::Key_Escape);
		check(visibleRows(models) == 1, "Clearing the filter should bring the model back.");
	}
	trigger(shell, "shell.mode.shaders");
	auto* shaderFilter = child<QLineEdit>(shell, "shaderFilter");
	auto* graph = child<QTreeWidget>(shell, "shaderGraph");
	if (shaderFilter && graph) {
		shaderFilter->setText(QStringLiteral("not_in_package"));
		settle();
		int shown = 0;
		for (int index = 0; index < graph->topLevelItemCount(); ++index) {
			shown += graph->topLevelItem(index)->isHidden() ? 0 : 1;
		}
		check(shown == 1, "The shader filter should keep only the shader that references the missing texture.");
		shaderFilter->clear();
		settle();
	}

	// Delete in the staging list takes a change back out of the plan.
	trigger(shell, "shell.mode.packages");
	auto staging = tests::StagingRows(child<PackageStagingView>(shell, "packageStagingSummary"));
	QAction* saveAs = action(shell, "package.saveAs");
	check(staging && saveAs && saveAs->isEnabled(), "The earlier staged deletion should still be in the plan.");
	if (staging && saveAs) {
		int operationRow = -1;
		for (int row = 0; row < staging->count(); ++row) {
			if (!staging->item(row)->data(Qt::UserRole + 5).toString().isEmpty()) {
				operationRow = row;
			}
		}
		check(operationRow >= 0, "The staging list should show the staged deletion.");
		if (operationRow >= 0) {
			// The staging list is a tab of the package inspector; show it first,
			// as the user would.
			for (QWidget* parent = staging->parentWidget(); parent; parent = parent->parentWidget()) {
				if (auto* tabs = qobject_cast<QTabWidget*>(parent)) {
					tabs->setCurrentWidget(staging->parentWidget());
					break;
				}
			}
			settle();
			staging->setFocus();
			staging->setCurrentRow(operationRow);
			press(shell, Qt::Key_Delete);
			check(!saveAs->isEnabled(), "Delete in the staging list should unstage the change.");
		}
	}
}

void checkLevelLinks(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkLevelLinks");
	// Loading definitions gives the inspector its "Defined in" row and the
	// Health tab its entity issues.
	shell.openPathFromCommandLine(fixtures.definitions);
	settle();
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* inspector = child<QTreeWidget>(shell, "entityInspector");
	auto* health = child<QListWidget>(shell, "levelMapHealth");
	check(currentPage(shell) == Levels && objects && inspector && health, "Loading definitions should show the Levels surface.");
	if (!objects || !inspector || !health) {
		return;
	}
	int playerRow = -1;
	for (int row = 0; row < objects->model()->rowCount(); ++row) {
		if (objectIndex(objects, row).data(Qt::UserRole).toString() == QStringLiteral("entity:1")) {
			playerRow = row;
		}
	}
	check(playerRow >= 0, "The objects list should hold the player start.");
	if (playerRow >= 0) {
		setObjectCurrentRow(objects, playerRow);
		settle();
		QTreeWidgetItem* defined = nullptr;
		for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
			if (!(*it)->data(0, Qt::UserRole + 5).toString().isEmpty()) {
				defined = *it;
			}
		}
		check(defined != nullptr, "The inspector should show where the class is defined.");
		if (defined) {
			Q_EMIT inspector->itemDoubleClicked(defined, 1);
			settle();
			auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
			check(currentPage(shell) == Code && editor && editor->toPlainText().contains(QStringLiteral("QUAKED info_player_start")),
				"Opening \"Defined in\" should show the definition file.");
		}
	}

	// An entity issue in Health selects that entity.
	trigger(shell, "shell.mode.levels");
	int issueRow = -1;
	QString selector;
	for (int row = 0; row < health->count(); ++row) {
		const QString value = health->item(row)->data(Qt::UserRole).toString();
		if (value.startsWith(QStringLiteral("entity:"))) {
			issueRow = row;
			selector = value;
			break;
		}
	}
	check(issueRow >= 0, "Health should list an issue for the entity whose class is not defined.");
	if (issueRow >= 0) {
		objects->clearSelection();
		activateRow(health, issueRow);
		const QModelIndex current = objects->currentIndex();
		check(current.isValid() && objects->selectionModel()->isSelected(current) && current.data(Qt::UserRole).toString() == selector,
			"Activating an entity issue should select that entity.");
	}
}

void checkMapContextMenu(ApplicationShell& shell)
{
	ensureActive(shell, "checkMapContextMenu");
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* zoomSelection = child<QAbstractButton>(shell, "levelMapZoomSelection");
	if (!map || !objects || !zoomSelection) {
		check(false, "The Levels surface should expose its viewport, objects list, and zoom buttons.");
		return;
	}
	// Frame the undefined item so it sits under the viewport's centre, then
	// select something else: a right press on the item should select it before
	// the menu opens, so the menu acts on what is under the pointer.
	const int item = rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3"));
	const int player = rowWithData(objects, Qt::UserRole, QStringLiteral("entity:1"));
	check(item >= 0 && player >= 0, "The objects list should hold the player start and the undefined item.");
	if (item < 0 || player < 0) {
		return;
	}
	setObjectCurrentRow(objects, item);
	settle();
	zoomSelection->click();
	settle();
	objects->clearSelection();
	setObjectCurrentRow(objects, player);
	settle();
	check(map->selectionSet().size() == 1, "Only the player start should be selected before the right press.");
	const QPoint centre = map->rect().center();
	QTest::mousePress(map, Qt::RightButton, Qt::NoModifier, centre);
	QTest::mouseRelease(map, Qt::RightButton, Qt::NoModifier, centre);
	settle();
	check(objects->currentIndex().isValid() && objects->currentIndex().data(Qt::UserRole).toString() == QStringLiteral("entity:3"),
		"A right press on an object should select it.");

	auto seen = std::make_shared<QStringList>();
	whenMenuShows([seen](QMenu* menu) {
		for (QAction* action : menuActions(menu)) {
			if (!action->isSeparator()) {
				seen->push_back(action->text() + (action->isEnabled() ? QString() : QStringLiteral(" (disabled)")));
			}
		}
		if (QAction* copy = menuAction(menu, QStringLiteral("Copy Selector"))) {
			chooseMenuAction(menu, copy);
		} else {
			menu->close();
		}
	});
	QGuiApplication::clipboard()->clear();
	Q_EMIT map->customContextMenuRequested(centre);
	settle();
	check(seen->contains(QStringLiteral("Zoom to Selection")) && seen->contains(QStringLiteral("Edit Key…"))
			&& seen->contains(QStringLiteral("Move…")),
		"The map menu should offer framing and the inspector's edits for a selected entity.");
	check(!seen->contains(QStringLiteral("Frame Leak Trail")), "Leak actions should only appear while a leak trail is shown.");
	check(QGuiApplication::clipboard()->text() == QStringLiteral("entity:3"), "Copy Selector should copy the selected object's selector.");

	// With nothing selected only the whole-map actions are live.
	objects->clearSelection();
	objects->setCurrentIndex({});
	settle();
	auto idle = std::make_shared<QStringList>();
	whenMenuShows([idle](QMenu* menu) {
		for (QAction* action : menu->actions()) {
			if (!action->isSeparator() && action->isEnabled()) {
				idle->push_back(action->text());
			}
		}
		menu->close();
	});
	Q_EMIT map->customContextMenuRequested(QPoint(4, 4));
	settle();
	check(*idle == QStringList({QStringLiteral("Add Entity Here…"), QStringLiteral("Add Brush Here…"), QStringLiteral("Replace Texture…"), QStringLiteral("Zoom to Fit")}),
		"With nothing selected the map menu should only offer its whole-map actions.");

	// The objects list offers the same menu for its selection.
	setObjectCurrentRow(objects, player);
	settle();
	whenMenuShows([](QMenu* menu) {
		if (QAction* copy = menuAction(menu, QStringLiteral("Copy Selector")); copy && copy->isEnabled()) {
			chooseMenuAction(menu, copy);
		} else {
			menu->close();
		}
	});
	QGuiApplication::clipboard()->clear();
	const QRect playerRect = objects->visualRect(objectIndex(objects, player));
	Q_EMIT objects->customContextMenuRequested(playerRect.center());
	settle();
	check(QGuiApplication::clipboard()->text() == QStringLiteral("entity:1"), "The objects list menu should act on the selected row.");
}

QTreeWidgetItem* inspectorKeyRow(QTreeWidget* inspector, const QString& key)
{
	for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
		if ((*it)->data(0, Qt::UserRole + 2).toBool() && (*it)->data(0, Qt::UserRole).toString() == key) {
			return *it;
		}
	}
	return nullptr;
}

void checkMapAddDelete(ApplicationShell& shell)
{
	ensureActive(shell, "checkMapAddDelete");
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* inspector = child<QTreeWidget>(shell, "entityInspector");
	if (!map || !objects || !inspector) {
		check(false, "The Levels surface should expose its viewport, objects list, and inspector.");
		return;
	}
	const int before = objects->model()->rowCount();

	// Add Entity Here from the viewport's menu asks for a class and selects
	// the new entity.
	whenDialogShows([](QWidget* dialog) {
		if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
			input->setTextValue(QStringLiteral("info_null"));
			input->accept();
		} else {
			static_cast<QDialog*>(dialog)->reject();
		}
	});
	whenMenuShows([](QMenu* menu) {
		QAction* add = menuAction(menu, QStringLiteral("Add Entity Here…"));
		if (add && add->isEnabled()) {
			chooseMenuAction(menu, add);
		} else {
			menu->close();
		}
	});
	Q_EMIT map->customContextMenuRequested(QPoint(40, 40));
	settle();
	check(objects->model()->rowCount() == before + 1 && currentPath(objects) == QStringLiteral("entity:4"),
		"Add Entity Here should add the entity and select it.");
	check(shell.statusBar()->currentMessage().contains(QStringLiteral("info_null")), "The status bar should name the added class.");

	// Delete in the viewport removes the selection.
	ensureActive(shell, "checkMapAddDelete: delete");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Delete);
	check(objects->model()->rowCount() == before, "Delete in the viewport should delete the selected entity.");

	// worldspawn cannot be deleted, so the command is off while it is selected.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:0")));
	settle();
	check(action(shell, "map.deleteSelection") && !action(shell, "map.deleteSelection")->isEnabled(),
		"Delete Selection should be off while worldspawn is selected.");

	// Delete in the inspector removes the key on the current row, not the entity.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:1")));
	settle();
	QTreeWidgetItem* angle = inspectorKeyRow(inspector, QStringLiteral("angle"));
	check(angle != nullptr, "The inspector should list the player start's angle key.");
	if (angle) {
		inspector->setFocus(Qt::OtherFocusReason);
		inspector->setCurrentItem(angle);
		settle();
		press(shell, Qt::Key_C, Qt::ControlModifier);
		check(QGuiApplication::clipboard()->text() == QStringLiteral("\"angle\" \"90\""), "Ctrl+C in the inspector should copy the key and its value.");
		press(shell, Qt::Key_Delete);
		check(inspectorKeyRow(inspector, QStringLiteral("angle")) == nullptr && objects->model()->rowCount() == before,
			"Delete in the inspector should remove the key and keep the entity.");
	}

	// Ctrl+D in the viewport copies the selection one grid step over and
	// selects the copy; Ctrl+Z on Levels takes it back out.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
	settle();
	ensureActive(shell, "checkMapAddDelete: duplicate");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_D, Qt::ControlModifier);
	check(objects->model()->rowCount() == before + 1 && currentPath(objects) == QStringLiteral("entity:4"),
		"Ctrl+D on Levels should copy the selected entity and select the copy.");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Z, Qt::ControlModifier);
	check(objects->model()->rowCount() == before, "Ctrl+Z on Levels should take the copy back out.");

	// Add Brush Here asks for a size and texture and adds a selected box.
	whenDialogShows([](QWidget* dialog) {
		static_cast<QDialog*>(dialog)->accept();
	});
	whenMenuShows([](QMenu* menu) {
		QAction* add = menuAction(menu, QStringLiteral("Add Brush Here…"));
		if (add && add->isEnabled()) {
			chooseMenuAction(menu, add);
		} else {
			menu->close();
		}
	});
	Q_EMIT map->customContextMenuRequested(QPoint(60, 60));
	settle();
	check(objects->model()->rowCount() == before + 1 && currentPath(objects).startsWith(QStringLiteral("brush:")),
		"Add Brush Here should add a box brush and select it.");
	ensureActive(shell, "checkMapAddDelete: brush undo");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Z, Qt::ControlModifier);
	check(objects->model()->rowCount() == before, "Ctrl+Z should take the new brush out.");

	// Ctrl+C puts the item on the clipboard as .map text; Ctrl+V pastes a copy
	// at the same place and selects it; Ctrl+Z takes it out again.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
	settle();
	map->setFocus(Qt::OtherFocusReason);
	settle();
	QGuiApplication::clipboard()->clear();
	press(shell, Qt::Key_C, Qt::ControlModifier);
	check(QGuiApplication::clipboard()->text().contains(QStringLiteral("\"classname\" \"item_not_defined\"")),
		"Ctrl+C on Levels should copy the entity as .map text.");
	press(shell, Qt::Key_V, Qt::ControlModifier);
	check(objects->model()->rowCount() == before + 1 && currentPath(objects) == QStringLiteral("entity:4"), "Ctrl+V should paste the copy and select it.");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Z, Qt::ControlModifier);
	check(objects->model()->rowCount() == before, "Ctrl+Z should take the pasted copy out.");

	// Rotate 90° Left turns the selection as one undoable step.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0")));
	settle();
	check(trigger(shell, "map.rotateLeft") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Turned the selection 90")),
		"Rotate 90° Left should turn the selected brush.");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Z, Qt::ControlModifier);
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0")));
	settle();
	check(trigger(shell, "map.flipHorizontal") && shell.statusBar()->currentMessage() == QStringLiteral("Flipped the selection horizontally."),
		"Flip Horizontal should mirror the selected brush.");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Z, Qt::ControlModifier);

	// Snap to Grid moves the item onto the view's grid as one undoable step.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
	settle();
	check(trigger(shell, "map.snapToGrid") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Snapped 1 object")),
		"Snap Selection to Grid should move the off-grid item.");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Z, Qt::ControlModifier);

	// Undo walks back the key removal, the delete, and the add.
	for (int step = 0; step < (angle ? 3 : 2); ++step) {
		trigger(shell, "map.undo");
	}
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:1")));
	settle();
	check(objects->model()->rowCount() == before && inspectorKeyRow(inspector, QStringLiteral("angle")) != nullptr,
		"Undo should restore the key and take the added entity back out.");
	check(action(shell, "map.undo") && !action(shell, "map.undo")->isEnabled(), "Undo should return the map to where it started.");
}

// Sets a key on one entity through Edit Key, which asks for the key and then
// the value in two dialogs.
void setEntityKey(ApplicationShell& shell, LevelObjectList* objects, const QString& selector, const QString& key, const QString& value)
{
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, selector));
	settle();
	whenDialogShows([key, value](QWidget* dialog) {
		auto* keyInput = qobject_cast<QInputDialog*>(dialog);
		if (!keyInput) {
			static_cast<QDialog*>(dialog)->reject();
			return;
		}
		keyInput->setTextValue(key);
		whenDialogShows([value](QWidget* next) {
			if (auto* valueInput = qobject_cast<QInputDialog*>(next)) {
				valueInput->setTextValue(value);
				valueInput->accept();
			} else {
				static_cast<QDialog*>(next)->reject();
			}
		});
		keyInput->accept();
	});
	trigger(shell, "map.editProperty");
	settle();
}

void checkTargetLinks(ApplicationShell& shell)
{
	ensureActive(shell, "checkTargetLinks");
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* links = shell.findChild<QAction*>(QStringLiteral("levelMapShowLinks"));
	auto* zoomFit = child<QAbstractButton>(shell, "levelMapZoomFit");
	if (!map || !objects || !links || !zoomFit) {
		check(false, "The Levels surface should expose its viewport, objects list, Links switch, and zoom buttons.");
		return;
	}
	check(links->isVisible() && links->isChecked() && map->showTargetLinks(), "A Quake map should offer Target Links in the Show menu, on.");
	check(zoomFit->isVisible() && child<QAbstractButton>(shell, "levelMapShowButton") && child<QAbstractButton>(shell, "levelMapShowButton")->isVisible(),
		"The view bar should keep Show and Zoom to Fit in view at this width.");
	check(map->targetLinkCount() == 0, "The test map starts with no target links.");

	// Naming the light and pointing the undefined item at it draws one link.
	setEntityKey(shell, objects, QStringLiteral("entity:2"), QStringLiteral("targetname"), QStringLiteral("lamp"));
	setEntityKey(shell, objects, QStringLiteral("entity:3"), QStringLiteral("target"), QStringLiteral("lamp"));
	check(map->targetLinkCount() == 1, "A target naming the light's targetname should make one link.");
	const int lampRow = rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3"));
	check(lampRow >= 0 && objectIndex(objects, lampRow).data(Qt::DisplayRole).toString().contains(QStringLiteral("fires lamp")),
		"The objects list should show what an entity fires.");

	// A target key suggests the map's targetnames: in the inspector's value
	// editor (Qt::UserRole + 9 holds a row's suggestions) and in Edit Key.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
	settle();
	auto* inspector = child<QTreeWidget>(shell, "entityInspector");
	QTreeWidgetItem* targetRow = inspector ? inspectorKeyRow(inspector, QStringLiteral("target")) : nullptr;
	check(targetRow && targetRow->data(1, Qt::UserRole + 9).toStringList() == QStringList {QStringLiteral("lamp")},
		"The target row should suggest the light's targetname.");
	auto offered = std::make_shared<QStringList>();
	whenDialogShows([offered](QWidget* dialog) {
		auto* keyInput = qobject_cast<QInputDialog*>(dialog);
		if (!keyInput) {
			static_cast<QDialog*>(dialog)->reject();
			return;
		}
		keyInput->setTextValue(QStringLiteral("target"));
		whenDialogShows([offered](QWidget* next) {
			if (auto* valueInput = qobject_cast<QInputDialog*>(next)) {
				*offered = valueInput->comboBoxItems();
			}
			static_cast<QDialog*>(next)->reject();
		});
		keyInput->accept();
	});
	trigger(shell, "map.editProperty");
	settle();
	check(offered->contains(QStringLiteral("lamp")), "Edit Key should offer the map's targetnames for a target's value.");
	ensureActive(shell, "checkTargetLinks: after Edit Key");

	// Edit Key starts from the value the entity has for whichever key is
	// picked, not only for the preset one.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:2")));
	settle();
	QTreeWidgetItem* originRow = inspector ? inspectorKeyRow(inspector, QStringLiteral("origin")) : nullptr;
	const QString origin = originRow ? originRow->data(1, Qt::UserRole).toString() : QString();
	auto started = std::make_shared<QString>();
	whenDialogShows([started](QWidget* dialog) {
		auto* keyInput = qobject_cast<QInputDialog*>(dialog);
		if (!keyInput) {
			static_cast<QDialog*>(dialog)->reject();
			return;
		}
		keyInput->setTextValue(QStringLiteral("origin"));
		whenDialogShows([started](QWidget* next) {
			if (auto* valueInput = qobject_cast<QInputDialog*>(next)) {
				*started = valueInput->textValue();
			}
			static_cast<QDialog*>(next)->reject();
		});
		keyInput->accept();
	});
	trigger(shell, "map.editProperty");
	settle();
	check(!origin.isEmpty() && *started == origin, "Edit Key should start from the light's own origin when origin is picked.");
	ensureActive(shell, "checkTargetLinks: after Edit Key origin");


	// Select All of This Class picks every entity of the class, keeping the
	// picked one primary.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:2")));
	settle();
	check(trigger(shell, "map.selectSimilar") && map->selectionSet().size() == 1 && map->selectedObjectId() == 2,
		"Select All of This Class should select the only light, and keep it primary.");
	check(map->accessibleDescription().contains(QStringLiteral("target link")), "The viewport's description should mention the link.");
	zoomFit->click();
	settle();
	snapshot(shell, "levels-target-link");

	links->setChecked(false);
	settle();
	check(!map->showTargetLinks(), "The Links switch should drive the viewport.");
	links->setChecked(true);
	settle();

	// History lists both edits after the map as opened; activating its first
	// row undoes both, and the last row redoes them.
	auto* history = child<QListWidget>(shell, "levelMapHistory");
	check(history && history->count() == 3 && history->currentRow() == 2, "History should list the opened map and both key edits, the last current.");
	if (history && history->count() == 3) {
		QTabWidget* inspectorTabs = nullptr;
		for (QWidget* widget = history; widget && !inspectorTabs; widget = widget->parentWidget()) {
			inspectorTabs = qobject_cast<QTabWidget*>(widget);
		}
		if (inspectorTabs) {
			inspectorTabs->setCurrentWidget(history);
			settle();
			snapshot(shell, "levels-history");
			inspectorTabs->setCurrentIndex(0);
		}
		activateRow(history, 0);
		check(map->targetLinkCount() == 0 && history->currentRow() == 0, "Going back to the opened map should undo both edits.");
		activateRow(history, 2);
		check(map->targetLinkCount() == 1, "Going to the last step should redo both edits.");
		// Select Targets and Sources follow the link; Connect Entities makes one.
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
		settle();
		check(trigger(shell, "map.selectTargets") && map->selectionSet().size() == 1 && map->selectedObjectId() == 2,
			"Select Targets from the entity firing lamp should select the light.");
		check(trigger(shell, "map.selectSources") && map->selectionSet().size() == 1 && map->selectedObjectId() == 3,
			"Select Sources from the light should select the entity that fires it.");
		objects->clearSelection();
		setObjectSelected(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:1")), true);
		setObjectSelected(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:2")), true);
		settle();
		const int linksBefore = map->targetLinkCount();
		check(trigger(shell, "map.connectEntities") && map->targetLinkCount() == linksBefore + 1
				&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Connected: the selected entities now target lamp")),
			"Connect Entities should make the player start target the light.");
		trigger(shell, "map.undo");
		check(map->targetLinkCount() == linksBefore, "Undo should take the link back.");

		activateRow(history, 0);
	} else {
		trigger(shell, "map.undo");
		trigger(shell, "map.undo");
	}
	check(map->targetLinkCount() == 0, "Undoing both key edits should remove the link.");
	check(action(shell, "map.undo") && !action(shell, "map.undo")->isEnabled(), "Undo should return the map to where it started.");
}

void checkReplaceTexture(ApplicationShell& shell)
{
	ensureActive(shell, "checkReplaceTexture");
	trigger(shell, "shell.mode.levels");
	// The dialog counts the uses before anything changes, and Replace makes
	// one undoable edit.
	auto counted = std::make_shared<QString>();
	whenDialogShows([counted](QWidget* dialog) {
		auto* from = dialog->findChild<QComboBox*>(QStringLiteral("replaceTextureFrom"));
		auto* to = dialog->findChild<QComboBox*>(QStringLiteral("replaceTextureTo"));
		auto* count = dialog->findChild<QLabel*>(QStringLiteral("replaceTextureCount"));
		if (!from || !to || !count) {
			static_cast<QDialog*>(dialog)->reject();
			return;
		}
		from->setCurrentText(QStringLiteral("base/floor"));
		to->setCurrentText(QStringLiteral("base/metal"));
		*counted = count->text();
		static_cast<QDialog*>(dialog)->accept();
	});
	check(trigger(shell, "map.replaceTexture"), "Replace Texture should be available for the open map.");
	settle();
	check(counted->startsWith(QStringLiteral("2 ")), "The dialog should count both floor faces before replacing.");
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Replaced base/floor with base/metal")),
		"Replace should report what it changed.");
	trigger(shell, "map.undo");
	check(action(shell, "map.undo") && !action(shell, "map.undo")->isEnabled(), "Undo should put the floor texture back.");

	// From Textures, Use in Open Map pre-fills the replacement in the map's own
	// naming: this map writes names without the textures/ folder.
	trigger(shell, "shell.mode.textures");
	auto* textures = child<QListWidget>(shell, "textureEntries");
	const int brick = textures ? rowWithData(textures, Qt::UserRole, QStringLiteral("textures/base/brick_wall.tga")) : -1;
	check(brick >= 0, "The Textures list should hold the brick texture.");
	if (brick < 0) {
		return;
	}
	auto prefilled = std::make_shared<QString>();
	whenDialogShows([prefilled](QWidget* dialog) {
		auto* from = dialog->findChild<QComboBox*>(QStringLiteral("replaceTextureFrom"));
		auto* to = dialog->findChild<QComboBox*>(QStringLiteral("replaceTextureTo"));
		if (!from || !to) {
			static_cast<QDialog*>(dialog)->reject();
			return;
		}
		*prefilled = to->currentText();
		from->setCurrentText(QStringLiteral("base/wall"));
		static_cast<QDialog*>(dialog)->accept();
	});
	whenMenuShows([](QMenu* menu) {
		QAction* use = menuAction(menu, QStringLiteral("Use in Open Map…"));
		if (use && use->isEnabled()) {
			chooseMenuAction(menu, use);
		} else {
			menu->close();
		}
	});
	Q_EMIT textures->customContextMenuRequested(textures->visualItemRect(textures->item(brick)).center());
	settle();
	check(*prefilled == QStringLiteral("base/brick_wall"), "Use in Open Map should name the texture the way the map does.");
	check(currentPage(shell) == Levels && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Replaced base/wall with base/brick_wall")),
		"Use in Open Map should replace the chosen texture and show Levels.");

	// In open map lists only the textures the map now uses, with a count.
	trigger(shell, "shell.mode.textures");
	auto* inMap = child<QCheckBox>(shell, "textureInMapOnly");
	const int metal = rowWithData(textures, Qt::UserRole, QStringLiteral("textures/base/metal_panel.tga"));
	const int brickRow = rowWithData(textures, Qt::UserRole, QStringLiteral("textures/base/brick_wall.tga"));
	check(inMap && inMap->isEnabled() && metal >= 0 && brickRow >= 0, "The Textures surface should offer In open map.");
	if (inMap && metal >= 0 && brickRow >= 0) {
		inMap->setChecked(true);
		settle();
		check(!textures->item(brickRow)->isHidden() && textures->item(metal)->isHidden()
				&& textures->item(brickRow)->toolTip().contains(QStringLiteral("Used 4 time")),
			"In open map should keep the brick texture, with its four uses, and hide the unused one.");
		inMap->setChecked(false);
		settle();
	}

	// Apply to Map Selection puts the texture on every face of the brush
	// selected in Levels that does not use it yet, and shows Levels.
	trigger(shell, "shell.mode.levels");
	if (auto* objects = child<LevelObjectList>(shell, "levelMapObjects"); objects && brickRow >= 0) {
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0")));
		settle();
		trigger(shell, "shell.mode.textures");
		whenMenuShows([](QMenu* menu) {
			QAction* apply = menuAction(menu, QStringLiteral("Apply to Map Selection"));
			if (apply && apply->isEnabled()) {
				chooseMenuAction(menu, apply);
			} else {
				menu->close();
			}
		});
		Q_EMIT textures->customContextMenuRequested(textures->visualItemRect(textures->item(brickRow)).center());
		settle();
		check(currentPage(shell) == Levels && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Applied base/brick_wall to 2 face")),
			"Apply to Map Selection should texture the brush's two floor faces and show Levels.");
		trigger(shell, "map.undo");
	}
	trigger(shell, "shell.mode.levels");
	trigger(shell, "map.undo");
}

// The Create palette places what the map can take: Enter on a class adds it
// in the middle of the view, and a class dropped on the view lands where it
// drops; each is one undo step.
QTreeWidgetItem* paletteItem(QTreeWidget* palette, const QString& text)
{
	for (int group = 0; palette && group < palette->topLevelItemCount(); ++group) {
		QTreeWidgetItem* parent = palette->topLevelItem(group);
		for (int child = 0; child < parent->childCount(); ++child) {
			if (parent->child(child)->text(0) == text) {
				return parent->child(child);
			}
		}
	}
	return nullptr;
}

void checkMapPalette(ApplicationShell& shell)
{
	ensureActive(shell, "checkMapPalette");
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* palette = child<QTreeWidget>(shell, "levelMapPalette");
	auto* filter = child<QLineEdit>(shell, "levelMapPaletteFilter");
	if (!map || !objects || !palette || !filter) {
		check(false, "The Levels surface should offer its Create palette and filter.");
		return;
	}
	const int rows = objects->model()->rowCount();
	for (QTabWidget* tabs : shell.findChildren<QTabWidget*>()) {
		for (int tab = 0; tab < tabs->count(); ++tab) {
			if (tabs->tabText(tab) == QStringLiteral("Create") && tabs->isVisible()) {
				tabs->setCurrentIndex(tab);
			}
		}
	}
	settle();
	snapshot(shell, "levels-create-palette");
	filter->setText(QStringLiteral("light"));
	settle();
	QTreeWidgetItem* light = paletteItem(palette, QStringLiteral("light"));
	check(light && !light->isHidden() && light->flags().testFlag(Qt::ItemIsDragEnabled)
			&& (!paletteItem(palette, QStringLiteral("info_player_start")) || paletteItem(palette, QStringLiteral("info_player_start"))->isHidden()),
		"Filtering the palette by light should leave the light class, draggable.");
	if (light) {
		Q_EMIT palette->itemActivated(light, 0);
		settle();
	}
	check(objects->model()->rowCount() == rows + 1 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Added light as entity:")),
		"Enter on a palette class should add it in the middle of the view.");
	trigger(shell, "map.undo");
	filter->clear();
	settle();

	// A drop lands where it drops, on the grid.
	QMimeData payload;
	payload.setData(QString::fromLatin1(kMapPaletteMimeType), QByteArrayLiteral("entity:info_player_start"));
	const QPointF at = map->viewPointFor(40.0, -8.0);
	// Qt hands a drop only to the widget that accepted the drag's entry.
	QDragEnterEvent moveEnter(at.toPoint(), Qt::CopyAction | Qt::MoveAction, &payload, Qt::LeftButton, Qt::ShiftModifier);
	QCoreApplication::sendEvent(map, &moveEnter);
	check(moveEnter.isAccepted() && moveEnter.dropAction() == Qt::CopyAction, "A Shift drag from the palette should still be taken as a copy, so the palette keeps its row.");
	QDragLeaveEvent leave;
	QCoreApplication::sendEvent(map, &leave);
	QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, &payload, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(map, &enter);
	check(enter.isAccepted(), "The map view should accept a palette drag.");
	QDropEvent drop(at, Qt::CopyAction, &payload, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(map, &drop);
	settle();
	check(objects->model()->rowCount() == rows + 1 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Added info_player_start as entity:")),
		"A class dropped on the view should be added where it drops.");
	trigger(shell, "map.undo");
	check(objects->model()->rowCount() == rows, "Undo should take the dropped entity back out.");
}

void checkHideShow(ApplicationShell& shell)
{
	ensureActive(shell, "checkHideShow");
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	if (!map || !objects) {
		check(false, "The Levels surface should expose its viewport and objects list.");
		return;
	}
	const auto itemRow = [objects]() {
		const int row = rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3"));
		return objectIndex(objects, row);
	};
	// H hides the selection from the view; the list keeps it, marked.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
	settle();
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_H);
	check(map->hiddenCount() == 1 && map->selectionSet().isEmpty(), "H should hide the selected entity and clear the selection.");
	check(itemRow().isValid() && itemRow().data(Qt::DisplayRole).toString().contains(QStringLiteral("hidden")), "The objects list should mark the hidden entity.");
	check(action(shell, "map.undo") && !action(shell, "map.undo")->isEnabled(), "Hiding should not edit the map.");

	// Selecting it from the list shows it again.
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
	settle();
	check(map->hiddenCount() == 0 && map->selectedObjectId() == 3, "Selecting a hidden entity in the list should show and select it.");
	check(itemRow().isValid() && !itemRow().data(Qt::DisplayRole).toString().contains(QStringLiteral("hidden"))
			&& !itemRow().data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("hidden")),
		"A row shown again from the list should lose its hidden mark.");

	// Shift+H brings everything back.
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_H);
	check(map->hiddenCount() == 1, "H should hide the entity again.");
	press(shell, Qt::Key_H, Qt::ShiftModifier);
	check(map->hiddenCount() == 0 && itemRow().isValid() && !itemRow().data(Qt::DisplayRole).toString().contains(QStringLiteral("hidden")), "Shift+H should show everything.");

	// A hidden object an edit takes out of the map stops counting as hidden:
	// hide a copy, undo the copy, and nothing is left to show.
	objects->clearSelection();
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
	settle();
	check(trigger(shell, "map.duplicateSelection"), "Duplicate should copy the entity.");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_H);
	check(map->hiddenCount() == 1 && action(shell, "map.showAll")->isEnabled(), "H should hide the copy.");
	trigger(shell, "map.undo");
	check(map->hiddenCount() == 0 && !action(shell, "map.showAll")->isEnabled(),
		"Undoing the copy should leave nothing hidden and Show All off.");

	// Hiding everything, and showing it again, leaves the camera where it
	// was. Snap goes off so the point under the middle is read unrounded.
	QCheckBox* snap = nullptr;
	for (QCheckBox* box : shell.findChildren<QCheckBox*>()) {
		if (box->accessibleName() == QStringLiteral("Snap drags and nudges to the grid")) {
			snap = box;
		}
	}
	if (snap) {
		snap->setChecked(false);
		settle();
		// Away from the origin first, where a reset would be plain to see.
		map->setFocus(Qt::OtherFocusReason);
		settle();
		for (int step = 0; step < 3; ++step) {
			press(shell, Qt::Key_Right, Qt::ControlModifier);
			press(shell, Qt::Key_Up, Qt::ControlModifier);
		}
		const QPointF middle = QPointF(map->rect().center()) + QPointF(13.0, 7.0);
		const LevelMapVec3 before = map->worldPositionAt(middle, 0.0);
		for (int row = 0; row < objects->model()->rowCount(); ++row) {
			setObjectSelected(objects, row, true);
		}
		settle();
		map->setFocus(Qt::OtherFocusReason);
		settle();
		press(shell, Qt::Key_H);
		const LevelMapVec3 hidden = map->worldPositionAt(middle, 0.0);
		press(shell, Qt::Key_H, Qt::ShiftModifier);
		const LevelMapVec3 shown = map->worldPositionAt(middle, 0.0);
		check(map->hiddenCount() == 0 && hidden.x == before.x && hidden.y == before.y && shown.x == before.x && shown.y == before.y,
			"Hiding everything and showing it again should not move the camera.");
		snap->setChecked(true);
		settle();
	}
}

// A set built in the viewport is the set the map's commands act on; Move…
// moves all of it; the selection's box resizes from Resize… and from its
// handles; a right press calls a drag off; and Ctrl+C in the read-only
// Details text copies that text rather than map objects.
void checkMapSelectionAndResize(ApplicationShell& shell)
{
	ensureActive(shell, "checkMapSelectionAndResize");
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* history = child<QListWidget>(shell, "levelMapHistory");
	if (!map || !objects || !history) {
		check(false, "The Levels surface should expose its viewport, objects list, and history.");
		return;
	}
	const auto select = [objects](const QString& selector) {
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, selector));
		settle();
	};
	const auto rowText = [objects](const QString& selector) {
		const int row = rowWithData(objects, Qt::UserRole, selector);
		return row >= 0 ? objectIndex(objects, row).data(Qt::DisplayRole).toString() : QString();
	};
	const auto historyHas = [history](const QString& text) {
		for (int row = 0; row < history->count(); ++row) {
			if (history->item(row)->text().contains(text)) {
				return true;
			}
		}
		return false;
	};

	// A colour key shows its colour, and a double-click picks another, written
	// back in the key's own form; both edits then undo.
	setEntityKey(shell, objects, QStringLiteral("entity:2"), QStringLiteral("_color"), QStringLiteral("1 0.5 0"));
	auto* inspector = child<QTreeWidget>(shell, "entityInspector");
	QTreeWidgetItem* colorRow = inspector ? inspectorKeyRow(inspector, QStringLiteral("_color")) : nullptr;
	check(colorRow && colorRow->data(1, Qt::DecorationRole).value<QColor>() == QColor(255, 128, 0), "A colour key should show its colour.");
	if (colorRow) {
		whenDialogShows([](QWidget* dialog) {
			if (auto* picker = qobject_cast<QColorDialog*>(dialog)) {
				picker->setCurrentColor(QColor(0, 0, 255));
				picker->accept();
			} else {
				static_cast<QDialog*>(dialog)->reject();
			}
		});
		Q_EMIT inspector->itemDoubleClicked(colorRow, 1);
		settle();
		colorRow = inspectorKeyRow(inspector, QStringLiteral("_color"));
		check(colorRow && colorRow->text(1) == QStringLiteral("0 0 1"), "Picking blue should write it back as fractions, as the key had it.");
	}
	ensureActive(shell, "checkMapSelectionAndResize: after the colour");
	trigger(shell, "map.undo");
	trigger(shell, "map.undo");

	// Ctrl+Tab in the viewport grows the set, and the map keeps all of it:
	// Copy puts every member on the clipboard, not only the primary.
	select(QStringLiteral("entity:1"));
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Tab, Qt::ControlModifier);
	press(shell, Qt::Key_Tab, Qt::ControlModifier);
	check(map->selectionSet().size() == 3, "Ctrl+Tab twice should grow the viewport's set to three objects.");
	QGuiApplication::clipboard()->clear();
	press(shell, Qt::Key_C, Qt::ControlModifier);
	const QString copied = QGuiApplication::clipboard()->text();
	check(copied.contains(QStringLiteral("info_player_start")) && copied.contains(QStringLiteral("\"classname\" \"light\""))
			&& copied.contains(QStringLiteral("item_not_defined")),
		"Copy after building a set in the viewport should copy every member of it.");

	// Move… moves the whole set by what is typed, as one step.
	whenDialogShows([](QWidget* dialog) {
		if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
			input->setTextValue(QStringLiteral("0,0,16"));
			input->accept();
		} else {
			static_cast<QDialog*>(dialog)->reject();
		}
	});
	check(trigger(shell, "map.moveSelection") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Moved 3 object")),
		"Move… should move all three selected entities.");
	check(rowText(QStringLiteral("entity:1")).contains(QStringLiteral("Origin 0,0,40"))
			&& rowText(QStringLiteral("entity:3")).contains(QStringLiteral("Origin 32,32,40")),
		"Every selected entity should be 16 units higher.");
	trigger(shell, "map.undo");

	// Resize… fits the brush to the width typed, from its lower corner.
	select(QStringLiteral("brush:0"));
	check(map->selectionExtent() == QSizeF(128.0, 128.0), "The floor brush should be 128 units square from the top.");
	whenDialogShows([](QWidget* dialog) {
		auto* width = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("resizeSizex"));
		if (!width) {
			static_cast<QDialog*>(dialog)->reject();
			return;
		}
		width->setValue(192.0);
		static_cast<QDialog*>(dialog)->accept();
	});
	check(trigger(shell, "map.resizeSelection") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Resized 1 object")),
		"Resize… should resize the selected brush.");
	select(QStringLiteral("brush:0"));
	check(map->selectionExtent() == QSizeF(192.0, 128.0) && historyHas(QStringLiteral("Resize brush:0 to 192 x 128 x 16")),
		"The brush should be 192 wide, and History should name the resize.");
	trigger(shell, "map.undo");

	// Dragging the box's right-hand handle stretches the brush on the grid.
	select(QStringLiteral("brush:0"));
	check(map->selectionExtent() == QSizeF(128.0, 128.0), "Undo should have put the brush back.");
	check(map->projection() == MapViewportProjection::TopXY && map->hasResizeHandles(), "A selected brush should show resize handles.");
	const auto mouse = [map](QEvent::Type type, const QPoint& at, Qt::MouseButton button, Qt::MouseButtons buttons) {
		QMouseEvent event(type, QPointF(at), map->mapToGlobal(QPointF(at)), button, buttons, Qt::NoModifier);
		QCoreApplication::sendEvent(map, &event);
	};
	snapshot(shell, "levels-resize-handles");
	const QPoint grip = map->resizeHandlePosition(MapViewport::ResizeMaxHorizontal).toPoint();
	const QPoint stretched = grip + QPoint(qRound(64.0 * map->zoom()), 0);
	mouse(QEvent::MouseButtonPress, grip, Qt::LeftButton, Qt::LeftButton);
	check(map->isResizing(), "A press on a resize handle should start a resize.");
	mouse(QEvent::MouseMove, stretched, Qt::NoButton, Qt::LeftButton);
	snapshot(shell, "levels-resize-drag");
	mouse(QEvent::MouseButtonRelease, stretched, Qt::LeftButton, Qt::NoButton);
	settle();
	select(QStringLiteral("brush:0"));
	check(!map->isResizing() && map->selectionExtent() == QSizeF(192.0, 128.0) && historyHas(QStringLiteral("Resize brush:0")),
		"Dragging the right-hand handle 64 units should make the brush 192 wide in one step.");
	trigger(shell, "map.undo");

	// A right press in the middle of a drag calls it off, and nothing changes.
	select(QStringLiteral("brush:0"));
	const int steps = history->count();
	const QPoint handle = map->resizeHandlePosition(MapViewport::ResizeMaxHorizontal).toPoint();
	mouse(QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton);
	mouse(QEvent::MouseMove, handle + QPoint(qRound(64.0 * map->zoom()), 0), Qt::NoButton, Qt::LeftButton);
	mouse(QEvent::MouseButtonPress, handle, Qt::RightButton, Qt::LeftButton | Qt::RightButton);
	check(!map->isResizing(), "A right press should call the resize off.");
	mouse(QEvent::MouseButtonRelease, handle, Qt::RightButton, Qt::LeftButton);
	mouse(QEvent::MouseButtonRelease, handle, Qt::LeftButton, Qt::NoButton);
	settle();
	check(history->count() == steps && map->selectionExtent() == QSizeF(128.0, 128.0), "A cancelled resize should leave the map as it was.");

	// [ and ] halve and double the grid.
	const int grid = map->gridSize();
	ensureActive(shell, "checkMapSelectionAndResize: grid");
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_BracketLeft);
	check(map->gridSize() == grid / 2 && shell.statusBar()->currentMessage() == QStringLiteral("Grid: %1 units.").arg(grid / 2),
		"[ should halve the grid.");
	check(StudioSettings().shellLayoutState(QStringLiteral("levelMapGrid")) == QByteArray::number(grid / 2),
		"The grid size should be kept for the next session.");
	press(shell, Qt::Key_BracketRight);
	check(map->gridSize() == grid, "] should double it again.");

	// Select by Texture selects what uses the texture chosen.
	select(QStringLiteral("entity:1"));
	whenDialogShows([](QWidget* dialog) {
		if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
			input->setTextValue(QStringLiteral("base/floor"));
			input->accept();
		} else {
			static_cast<QDialog*>(dialog)->reject();
		}
	});
	check(trigger(shell, "map.selectByTexture") && map->selectionSet().size() == 1
			&& map->selectionSet().first().kind == LevelMapSelectionKind::QuakeBrush && currentPath(objects) == QStringLiteral("brush:0"),
		"Select by Texture should select the brush that uses base/floor.");

	// Ctrl+C in the read-only Details text copies the text selected there.
	QTextEdit* details = nullptr;
	for (QTextEdit* content : shell.findChildren<QTextEdit*>(QStringLiteral("detailContent"))) {
		for (QWidget* parent = content; parent; parent = parent->parentWidget()) {
			if (parent->accessibleName() == QStringLiteral("Level map detail drawer")) {
				details = content;
			}
		}
	}
	check(details != nullptr, "The Levels Details tab should hold a read-only text view.");
	if (details) {
		for (QWidget* parent = details; parent; parent = parent->parentWidget()) {
			if (auto* tabs = qobject_cast<QTabWidget*>(parent->parentWidget() ? parent->parentWidget()->parentWidget() : nullptr);
				tabs && tabs->indexOf(parent) >= 0) {
				tabs->setCurrentIndex(tabs->indexOf(parent));
				break;
			}
		}
		ensureActive(shell, "checkMapSelectionAndResize: details");
		details->setFocus(Qt::OtherFocusReason);
		details->selectAll();
		settle();
		QGuiApplication::clipboard()->clear();
		press(shell, Qt::Key_C, Qt::ControlModifier);
		const QString text = QGuiApplication::clipboard()->text();
		check(!text.isEmpty() && text == details->toPlainText(), "Ctrl+C in the Details text should copy the selected text, not map objects.");
	}
}

// The clip tool: X turns it on, a drag draws the line, Enter cuts, Tab
// chooses what stays, Escape leaves; Clip Selection… does it from the keyboard.
void checkMapClip(ApplicationShell& shell)
{
	ensureActive(shell, "checkMapClip");
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	if (!map || !objects) {
		check(false, "The Levels surface should expose its viewport and objects list.");
		return;
	}
	const auto selectFloor = [objects]() {
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0")));
		settle();
	};
	selectFloor();
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_X);
	check(map->clipMode() && action(shell, "map.clipTool") && action(shell, "map.clipTool")->isChecked(),
		"X should turn the clip tool on and check its menu item.");

	// A drag draws the line, snapped, and draws rather than selects.
	const auto mouse = [map](QEvent::Type type, const QPoint& at, Qt::MouseButton button, Qt::MouseButtons buttons) {
		QMouseEvent event(type, QPointF(at), map->mapToGlobal(QPointF(at)), button, buttons, Qt::NoModifier);
		QCoreApplication::sendEvent(map, &event);
	};
	// Far enough apart, in map units, that the ends land on different grid lines.
	const QPoint centre = map->rect().center();
	const QPoint reach(0, qRound(96.0 * map->zoom()));
	mouse(QEvent::MouseButtonPress, centre + reach, Qt::LeftButton, Qt::LeftButton);
	mouse(QEvent::MouseMove, centre - reach, Qt::NoButton, Qt::LeftButton);
	mouse(QEvent::MouseButtonRelease, centre - reach, Qt::LeftButton, Qt::NoButton);
	settle();
	check(map->hasClipLine() && map->selectionSet().size() == 1, "A drag in clip mode should draw a line and leave the selection alone.");
	snapshot(shell, "levels-clip-line");

	// A line up through x = 0: its right, +x, is the front, so the default
	// keeps the -x half of the floor.
	map->setClipLine(QPointF(0.0, -100.0), QPointF(0.0, 100.0));
	press(shell, Qt::Key_Return);
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Clipped 1 brush")) && map->clipMode() && !map->hasClipLine(),
		"Enter should clip the floor, keep the tool on, and clear the line.");
	check(map->selectionExtent() == QSizeF(64.0, 128.0), "The clipped floor should be the 64-wide half.");
	trigger(shell, "map.undo");

	// Tab twice keeps both parts: two brushes, both selected.
	selectFloor();
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Tab);
	press(shell, Qt::Key_Tab);
	check(map->clipKeep() == LevelMapClipKeep::Both, "Tab twice should keep both parts.");
	map->setClipLine(QPointF(0.0, -100.0), QPointF(0.0, 100.0));
	press(shell, Qt::Key_Return);
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Split 1 brush")) && map->selectionSet().size() == 2,
		"Enter should split the floor in two and select both halves.");
	trigger(shell, "map.undo");

	// A line is drawn in the view's own axes, so another view drops it.
	map->setClipLine(QPointF(0.0, -100.0), QPointF(0.0, 100.0));
	QComboBox* projection = nullptr;
	for (QComboBox* combo : shell.findChildren<QComboBox*>()) {
		if (combo->accessibleName() == QStringLiteral("Map projection")) {
			projection = combo;
		}
	}
	if (projection) {
		projection->setCurrentIndex(1);
		settle();
		check(map->clipMode() && !map->hasClipLine(), "Changing the view should drop the clip line and keep the tool on.");
		projection->setCurrentIndex(0);
		settle();
	}

	// Escape leaves the tool.
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_Escape);
	check(!map->clipMode() && !action(shell, "map.clipTool")->isChecked(), "Escape should leave the clip tool.");

	// Clip Selection… cuts along an axis from the keyboard.
	selectFloor();
	whenDialogShows([](QWidget* dialog) {
		auto* axis = dialog->findChild<QComboBox*>(QStringLiteral("clipAxis"));
		auto* position = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("clipPosition"));
		auto* keep = dialog->findChild<QComboBox*>(QStringLiteral("clipKeep"));
		if (!axis || !position || !keep) {
			static_cast<QDialog*>(dialog)->reject();
			return;
		}
		axis->setCurrentIndex(1);
		position->setValue(32.0);
		keep->setCurrentIndex(0);
		static_cast<QDialog*>(dialog)->accept();
	});
	check(trigger(shell, "map.clipSelection") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Clipped 1 brush"))
			&& map->selectionExtent() == QSizeF(128.0, 96.0),
		"Clip Selection… at y 32 keeping below should leave the floor 96 deep.");
	trigger(shell, "map.undo");

	// Hollow… turns the floor slab into six walls of the thickness asked for.
	selectFloor();
	whenDialogShows([](QWidget* dialog) {
		if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
			input->setIntValue(4);
			input->accept();
		} else {
			static_cast<QDialog*>(dialog)->reject();
		}
	});
	check(trigger(shell, "map.hollowSelection") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Hollowed 1 brush"))
			&& map->selectionSet().size() == 6,
		"Hollow… should turn the floor into six selected walls.");
	trigger(shell, "map.undo");

	// Carve says so when the selection overlaps nothing to carve.
	selectFloor();
	check(trigger(shell, "map.carve") && shell.statusBar()->currentMessage().contains(QStringLiteral("do not overlap")),
		"Carve with a lone brush should say it overlaps nothing.");

	// 3D Preview shows the map's brushes in place of the 2D view, and the
	// same command brings the 2D view back.
	auto* preview = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
	check(preview && trigger(shell, "map.toggle3D") && preview->isVisible() && !map->isVisible() && preview->hasMesh()
			&& preview->mesh().triangleCount == 12 && action(shell, "map.toggle3D")->isChecked(),
		"3D Preview should show the floor brush's twelve triangles in place of the 2D view.");
	snapshot(shell, "levels-3d-preview");
	check(child<QAbstractButton>(shell, "levelMapShowButton") && !child<QAbstractButton>(shell, "levelMapShowButton")->isEnabled()
			&& child<QAbstractButton>(shell, "levelMapZoomSelection") && !child<QAbstractButton>(shell, "levelMapZoomSelection")->isEnabled(),
		"The 2D view's Show menu and Zoom to Selection should wait while the 3D preview shows.");
	if (preview) {
		// The selected floor is lit in 3D; selecting something else unlights it,
		// and a click on the floor in the preview selects it again.
		check(preview->highlightedTriangleCount() == 12, "The selected floor brush should be lit in the 3D preview.");
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:1")));
		settle();
		check(preview->highlightedTriangleCount() == 0, "Selecting an entity should leave no brush lit.");
		const QPointF middle = QPointF(preview->rect().center());
		QMouseEvent pressEvent(QEvent::MouseButtonPress, middle, preview->mapToGlobal(middle), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(preview, &pressEvent);
		QMouseEvent releaseEvent(QEvent::MouseButtonRelease, middle, preview->mapToGlobal(middle), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QCoreApplication::sendEvent(preview, &releaseEvent);
		settle();
		check(currentPath(objects) == QStringLiteral("brush:0") && preview->highlightedTriangleCount() == 12
				&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Selected brush:0, face ")),
			"A click on the floor in the 3D preview should select it, light it, and name the face.");
		// The face clicked opens in the Inspector at its texture.
		auto* picked = child<QTreeWidget>(shell, "entityInspector");
		QTreeWidgetItem* pickedRow = picked ? picked->currentItem() : nullptr;
		check(pickedRow && pickedRow->text(0) == QStringLiteral("Texture") && pickedRow->parent()
				&& pickedRow->parent()->text(0).startsWith(QStringLiteral("Face ")) && pickedRow->parent()->isExpanded() && picked->isVisible(),
			"The clicked face should open in the Inspector, at its texture.");
		// W switches the preview to edges and back.
		ensureActive(shell, "checkMapClip: 3D wireframe");
		preview->setFocus(Qt::OtherFocusReason);
		settle();
		press(shell, Qt::Key_W);
		check(preview->renderMode() == ModelViewportRenderMode::Wireframe && action(shell, "map.previewWireframe")->isChecked(),
			"W should draw the 3D preview as edges.");
		press(shell, Qt::Key_W);
		check(preview->renderMode() == ModelViewportRenderMode::FlatShaded, "W again should shade it.");
	}
	check(trigger(shell, "map.toggle3D") && map->isVisible() && preview && !preview->isVisible() && !action(shell, "map.toggle3D")->isChecked(),
		"3D Preview again should bring the 2D view back.");

	// With a brush selected the Inspector lists its faces by the way they
	// face; a face's shift edits in place, as one undo step, and the row stays
	// current for the next edit.
	selectFloor();
	auto* faceInspector = child<QTreeWidget>(shell, "entityInspector");
	QTreeWidgetItem* westFace = nullptr;
	for (int group = 0; faceInspector && group < faceInspector->topLevelItemCount(); ++group) {
		if (faceInspector->topLevelItem(group)->text(0).startsWith(QStringLiteral("Face 1, west (-x): base/wall"))) {
			westFace = faceInspector->topLevelItem(group);
		}
	}
	QTreeWidgetItem* shift = nullptr;
	for (int child = 0; westFace && child < westFace->childCount(); ++child) {
		if (westFace->child(child)->text(0) == QStringLiteral("Shift X")) {
			shift = westFace->child(child);
		}
	}
	check(shift && shift->flags().testFlag(Qt::ItemIsEditable) && shift->text(1) == QStringLiteral("0"),
		"A selected brush should list its faces, the west one with an editable shift.");
	if (shift) {
		faceInspector->setCurrentItem(shift, 1);
		shift->setText(1, QStringLiteral("16"));
		settle();
	}
	QTreeWidgetItem* shifted = faceInspector ? faceInspector->currentItem() : nullptr;
	check(shifted && shifted->text(0) == QStringLiteral("Shift X") && shifted->text(1) == QStringLiteral("16")
			&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Shift X on brush 0 face 1 set to 16")),
		"The face's shift should change, and the edited row stay current.");
	trigger(shell, "map.undo");

	// Ctrl+A selects every row's object but worldspawn's, Ctrl+I flips the
	// selection, and Ctrl+Shift+A clears it.
	ensureActive(shell, "checkMapClip: select all");
	const int everything = objects->model()->rowCount() - 1;
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_A, Qt::ControlModifier);
	check(map->selectionSet().size() == everything, "Ctrl+A should select every object but worldspawn.");
	press(shell, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
	check(map->selectionSet().isEmpty(), "Ctrl+Shift+A should clear the selection.");
	selectFloor();
	map->setFocus(Qt::OtherFocusReason);
	settle();
	press(shell, Qt::Key_I, Qt::ControlModifier);
	check(map->selectionSet().size() == everything - 1
			&& !map->selectionSet().contains(LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, 0}),
		"Ctrl+I should select everything but the floor instead of the floor.");
	press(shell, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
}

void checkCloseMap(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCloseMap");
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* recent = child<QListWidget>(shell, "levelsRecentMaps");
	if (!map || !recent) {
		check(false, "The Levels surface should expose its viewport and recent maps.");
		return;
	}
	// In open map left ticked has nothing to filter by once the map closes.
	auto* inMap = child<QCheckBox>(shell, "textureInMapOnly");
	if (inMap) {
		inMap->setChecked(true);
		settle();
	}
	// Close Map shows the empty page, which lists the map just closed.
	// The clip tool goes off with the map, and its menu item says so.
	trigger(shell, "map.clipTool");
	check(map->clipMode() && action(shell, "map.clipTool")->isChecked(), "The clip tool should be on before the map closes.");
	check(trigger(shell, "map.close") && !map->hasDocument(), "Close Map should close the open map.");
	check(!map->clipMode() && !action(shell, "map.clipTool")->isChecked(), "Closing the map should turn the clip tool off and uncheck it.");
	trigger(shell, "shell.mode.textures");
	auto* textures = child<QListWidget>(shell, "textureEntries");
	int shown = 0;
	for (int row = 0; textures && row < textures->count(); ++row) {
		shown += textures->item(row)->isHidden() ? 0 : 1;
	}
	check(inMap && !inMap->isChecked() && !inMap->isEnabled() && shown > 0,
		"With no map open, In open map should be off and every texture listed.");
	trigger(shell, "shell.mode.levels");
	check(recent->isVisible() && rowWithData(recent, Qt::UserRole, QFileInfo(fixtures.map).absoluteFilePath()) >= 0,
		"The empty Levels page should list the map just closed.");
	check(action(shell, "map.close") && !action(shell, "map.close")->isEnabled(), "Close Map should be off with no map open.");
	// Enter on it opens it again.
	activateRow(recent, rowWithData(recent, Qt::UserRole, QFileInfo(fixtures.map).absoluteFilePath()));
	check(map->hasDocument() && currentPage(shell) == Levels, "A recent map should open from the empty page.");
}

void checkViewerControls(ApplicationShell& shell)
{
	ensureActive(shell, "checkViewerControls");

	// Levels: a Quake map has no sectors, and its point objects are entities.
	trigger(shell, "shell.mode.levels");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* things = shell.findChild<QAction*>(QStringLiteral("levelMapShowThings"));
	auto* sectors = shell.findChild<QAction*>(QStringLiteral("levelMapShowSectors"));
	auto* grid = shell.findChild<QAction*>(QStringLiteral("levelMapShowGrid"));
	auto* vertices = shell.findChild<QAction*>(QStringLiteral("levelMapShowVertices"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* zoomFit = child<QAbstractButton>(shell, "levelMapZoomFit");
	auto* zoomSelection = child<QAbstractButton>(shell, "levelMapZoomSelection");
	check(map && things && sectors && grid && vertices && objects && zoomFit && zoomSelection,
		"The map viewport toolbar should expose its display switches and zoom buttons.");
	if (map && things && sectors && grid && vertices && objects && zoomFit && zoomSelection) {
		check(things->text() == QStringLiteral("Entities"), "A Quake map's point objects should be called entities.");
		check(!sectors->isVisible(), "Sector fill only applies to Doom maps and should be hidden for a Quake map.");
		grid->setChecked(false);
		vertices->setChecked(true);
		settle();
		check(!map->showGrid() && map->showVertices(), "The Grid and Vertices switches should drive the viewport.");
		grid->setChecked(true);
		vertices->setChecked(false);
		settle();

		zoomFit->click();
		settle();
		const double fitted = map->zoom();
		const int player = rowWithData(objects, Qt::UserRole, QStringLiteral("entity:1"));
		check(player >= 0, "The objects list should hold the player start.");
		setObjectCurrentRow(objects, player);
		settle();
		zoomSelection->click();
		settle();
		check(std::abs(map->zoom() - fitted) > 1e-6, "Zoom to Selection should frame the selected entity rather than the whole map.");

		// Enter on a row frames the object without leaving the list.
		zoomFit->click();
		settle();
		objects->setFocus();
		activateRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:3")));
		check(std::abs(map->zoom() - fitted) > 1e-6 && QApplication::focusWidget() == objects,
			"Activating an object row should frame it and keep focus in the list.");

		// A brush reports its size in the view plane.
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0")));
		settle();
		check(map->statusLines().contains(QStringLiteral("Selection size: 128 by 128 units")), "The selected brush's size should be reported.");
	}

	// Textures: the zoom buttons and overlay switches drive the image view.
	trigger(shell, "shell.mode.textures");
	auto* textures = child<QListWidget>(shell, "textureEntries");
	auto* image = shell.findChild<ImagePreviewView*>(QStringLiteral("texturePreview"));
	auto* frame = child<QComboBox>(shell, "textureFrame");
	auto* actual = child<QAbstractButton>(shell, "textureActualSize");
	auto* zoomIn = child<QAbstractButton>(shell, "textureZoomIn");
	auto* pixelGrid = child<QCheckBox>(shell, "texturePixelGrid");
	auto* checkerboard = child<QCheckBox>(shell, "textureCheckerboard");
	check(textures && image && frame && actual && zoomIn && pixelGrid && checkerboard,
		"The texture view bar should expose frames, zoom, and overlay switches.");
	if (textures && image && frame && actual && zoomIn && pixelGrid && checkerboard) {
		textures->setCurrentRow(rowWithData(textures, Qt::UserRole, QStringLiteral("textures/base/brick_wall.tga")));
		settle();
		settleImage(image);
		check(image->hasImage(), "Selecting a texture should show it.");
		check(!frame->isEnabled() && frame->count() == 1, "A single-frame image should offer no frame choice.");
		actual->click();
		settle();
		check(std::abs(image->zoom() - 1.0) < 1e-6, "Actual Size should show one texel per screen pixel.");
		zoomIn->click();
		settle();
		check(image->zoom() > 1.0, "Zoom In should magnify the image.");
		pixelGrid->setChecked(true);
		checkerboard->setChecked(false);
		settle();
		check(image->showPixelGrid() && !image->showCheckerboard(), "The overlay switches should drive the image view.");
		pixelGrid->setChecked(false);
		checkerboard->setChecked(true);
	}

	// Models: the display switches and the playback speed drive the viewport.
	trigger(shell, "shell.mode.models");
	auto* models = child<QListWidget>(shell, "modelEntries");
	auto* model = shell.findChild<ModelViewport*>(QStringLiteral("modelViewport"));
	auto* modelGrid = child<QCheckBox>(shell, "modelShowGrid");
	auto* edges = child<QCheckBox>(shell, "modelShowEdges");
	auto* culling = child<QCheckBox>(shell, "modelCullBackfaces");
	auto* speed = child<QSpinBox>(shell, "modelFramesPerSecond");
	check(models && model && modelGrid && edges && culling && speed, "The model view bar should expose its display switches and speed.");
	if (models && model && modelGrid && edges && culling && speed) {
		models->setCurrentRow(0);
		settle();
		check(model->hasMesh(), "Selecting the crate should show its mesh.");
		modelGrid->setChecked(!modelGrid->isChecked());
		edges->setChecked(!edges->isChecked());
		culling->setChecked(!culling->isChecked());
		speed->setValue(24);
		settle();
		check(model->showGrid() == modelGrid->isChecked() && model->showEdges() == edges->isChecked()
				&& model->backfaceCulling() == culling->isChecked(),
			"The model display switches should drive the viewport.");
		check(model->framesPerSecond() == 24, "The speed box should set the playback rate.");
	}
}

void checkWadMaps(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkWadMaps");
	// A WAD opens on its first map, and the chooser lists every map in it.
	shell.openPathFromCommandLine(fixtures.doomWad);
	settle();
	auto* chooser = child<QComboBox>(shell, "levelMapName");
	auto* sectors = shell.findChild<QAction*>(QStringLiteral("levelMapShowSectors"));
	auto* things = shell.findChild<QAction*>(QStringLiteral("levelMapShowThings"));
	check(currentPage(shell) == Levels && chooser, "Opening a WAD should show Levels with its map chooser.");
	if (!chooser) {
		return;
	}
	check(chooser->isVisible() && chooser->count() == 2 && chooser->itemText(1) == QStringLiteral("MAP02"),
		"The chooser should list both maps of the WAD.");
	check(chooser->currentText() == QStringLiteral("MAP01"), "A WAD should open on its first map.");
	check(sectors && sectors->isVisible() && things && things->text() == QStringLiteral("Things"),
		"A Doom map should show Sector fill and call its markers things.");

	// Choosing another map opens it.
	chooser->setCurrentIndex(1);
	Q_EMIT chooser->activated(1);
	settle();
	check(chooser->currentText() == QStringLiteral("MAP02"), "Choosing MAP02 should open it.");
	auto* statistics = child<QListWidget>(shell, "levelMapStatistics");
	bool reportsMap02 = false;
	for (int row = 0; statistics && row < statistics->count(); ++row) {
		reportsMap02 = reportsMap02 || statistics->item(row)->text() == QStringLiteral("Map: MAP02");
	}
	check(reportsMap02, "Levels should now describe MAP02.");

	// Things: Add Thing Here asks for a DoomEd number, Ctrl+D copies, Del
	// deletes, and undo takes all three back before another map opens.
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	if (map && objects) {
		const int rows = objects->model()->rowCount();
		whenDialogShows([](QWidget* dialog) {
			if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
				input->setTextValue(QStringLiteral("3004  Zombieman"));
				input->accept();
			} else {
				static_cast<QDialog*>(dialog)->reject();
			}
		});
		whenMenuShows([](QMenu* menu) {
			QAction* add = menuAction(menu, QStringLiteral("Add Thing Here…"));
			if (add && add->isEnabled()) {
				chooseMenuAction(menu, add);
			} else {
				menu->close();
			}
		});
		Q_EMIT map->customContextMenuRequested(QPoint(40, 40));
		settle();
		check(objects->model()->rowCount() == rows + 1 && currentPath(objects).startsWith(QStringLiteral("thing:")),
			"Add Thing Here should add a thing to a Doom map and select it.");
		auto* inspector = child<QTreeWidget>(shell, "entityInspector");
		QTreeWidgetItem* typeRow = nullptr;
		QTreeWidgetItem* ambush = nullptr;
		for (QTreeWidgetItemIterator it(inspector); inspector && *it; ++it) {
			if ((*it)->text(0) == QStringLiteral("Type")) {
				typeRow = *it;
			}
			if ((*it)->text(1).startsWith(QStringLiteral("Ambush"))) {
				ambush = *it;
			}
		}
		check(typeRow && typeRow->text(1) == QStringLiteral("3004") && ambush && ambush->checkState(0) == Qt::Unchecked,
			"The inspector should show a selected thing's type and its flags as boxes.");
		if (ambush) {
			ambush->setCheckState(0, Qt::Checked);
			settle();
		}
		check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Ambush: waits until it sees you set on thing")),
			"Ticking Ambush should set the thing's flag and say so.");
		trigger(shell, "map.undo");
		ensureActive(shell, "checkWadMaps: things");
		map->setFocus(Qt::OtherFocusReason);
		settle();
		press(shell, Qt::Key_D, Qt::ControlModifier);
		check(objects->model()->rowCount() == rows + 2, "Ctrl+D should copy the selected thing.");
		map->setFocus(Qt::OtherFocusReason);
		settle();
		press(shell, Qt::Key_Delete);
		check(objects->model()->rowCount() == rows + 1, "Del should delete the selected thing.");
		for (int step = 0; step < 3; ++step) {
			trigger(shell, "map.undo");
		}
		check(objects->model()->rowCount() == rows && action(shell, "map.undo") && !action(shell, "map.undo")->isEnabled(),
			"Undo should take every thing edit back.");

		// Split Linedefs adds a vertex and the second half, each with a row;
		// Flip Linedefs turns both halves; undo takes both edits back.
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("linedef:0")));
		settle();
		check(trigger(shell, "map.splitLinedefs") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Split 1 linedef"))
				&& objects->model()->rowCount() == rows + 2,
			"Split Linedefs should add the middle vertex and the second half.");
		check(trigger(shell, "map.flipLinedefs") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Flipped 2 linedef")),
			"Flip Linedefs should turn both selected halves.");
		trigger(shell, "map.undo");
		trigger(shell, "map.undo");
		check(objects->model()->rowCount() == rows && action(shell, "map.undo") && !action(shell, "map.undo")->isEnabled(),
			"Undo should take the flip and the split back.");

		// The Create palette lists Doom things by kind; Enter on one adds it.
		auto* palette = child<QTreeWidget>(shell, "levelMapPalette");
		QTreeWidgetItem* imp = paletteItem(palette, QStringLiteral("3001  Imp"));
		check(imp && imp->parent() && imp->parent()->text(0) == QStringLiteral("Monsters"), "The palette should list the imp under Monsters.");
		if (auto* paletteFilter = child<QLineEdit>(shell, "levelMapPaletteFilter"); paletteFilter && imp && imp->parent()) {
			paletteFilter->setText(QStringLiteral("Monsters"));
			settle();
			QTreeWidgetItem* monsters = imp->parent();
			int shown = 0;
			for (int index = 0; index < monsters->childCount(); ++index) {
				shown += monsters->child(index)->isHidden() ? 0 : 1;
			}
			check(!monsters->isHidden() && shown == monsters->childCount() && shown > 1, "Filtering by a group's name should show everything in it.");
			paletteFilter->clear();
			settle();
		}
		if (imp) {
			Q_EMIT palette->itemActivated(imp, 0);
			settle();
		}
		check(objects->model()->rowCount() == rows + 1 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Added type 3001 as thing:")),
			"Enter on a palette thing should add it to the Doom map.");
		trigger(shell, "map.undo");

		// A click inside the room, without a drag, picks its sector.
		QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, map->viewPointFor(-40.0, 30.0).toPoint());
		settle();
		check(map->selectionKind() == LevelMapSelectionKind::DoomSector && map->selectedObjectId() == 0 && currentPath(objects) == QStringLiteral("sector:0"),
			"A click inside the room should select its sector.");

		// The inspector edits a Doom sector's and linedef's fields in place:
		// a height, a flag box, and a side's texture, each one undo step.
		// A field by its name, or a flag by what it does.
		const auto doomRow = [inspector](const QString& label) -> QTreeWidgetItem* {
			for (int group = 0; inspector && group < inspector->topLevelItemCount(); ++group) {
				QTreeWidgetItem* parent = inspector->topLevelItem(group);
				for (int child = 0; child < parent->childCount(); ++child) {
					QTreeWidgetItem* row = parent->child(child);
					if (row->text(0) == label || (row->flags().testFlag(Qt::ItemIsUserCheckable) && row->text(1) == label)) {
						return row;
					}
				}
			}
			return nullptr;
		};
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("sector:0")));
		settle();
		QTreeWidgetItem* floor = doomRow(QStringLiteral("Floor height"));
		check(floor && floor->flags().testFlag(Qt::ItemIsEditable) && floor->text(1) == QStringLiteral("0"),
			"A selected sector should show its floor height, editable, in the inspector.");
		if (floor) {
			floor->setText(1, QStringLiteral("24"));
			settle();
		}
		floor = doomRow(QStringLiteral("Floor height"));
		check(floor && floor->text(1) == QStringLiteral("24") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Floor height on sector 0 set to 24")),
			"Editing the floor height should raise the floor and say so.");
		objects->clearSelection();
		setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("linedef:1")));
		settle();
		QTreeWidgetItem* sound = doomRow(QStringLiteral("Blocks sound"));
		check(sound && sound->checkState(0) == Qt::Unchecked && sound->flags().testFlag(Qt::ItemIsUserCheckable),
			"A selected linedef should show its flags as boxes.");
		if (sound) {
			sound->setCheckState(0, Qt::Checked);
			settle();
		}
		sound = doomRow(QStringLiteral("Blocks sound"));
		check(sound && sound->checkState(0) == Qt::Checked && doomRow(QStringLiteral("Flags")) && doomRow(QStringLiteral("Flags"))->text(1) == QStringLiteral("65"),
			"Ticking Blocks sound should set the linedef's 0x40 bit.");
		// Space toggles the box from the keyboard, again and again: the row the
		// inspector restores after each edit keeps its check column current.
		inspector->setFocus();
		inspector->setCurrentItem(doomRow(QStringLiteral("Blocks sound")), 1);
		press(shell, Qt::Key_Space);
		check(doomRow(QStringLiteral("Blocks sound"))->checkState(0) == Qt::Unchecked && doomRow(QStringLiteral("Flags"))->text(1) == QStringLiteral("1"),
			"Space on a flag row, even in its value column, should clear the flag.");
		press(shell, Qt::Key_Space);
		check(doomRow(QStringLiteral("Blocks sound"))->checkState(0) == Qt::Checked && doomRow(QStringLiteral("Flags"))->text(1) == QStringLiteral("65"),
			"Space again should set it again.");
		inspector->setCurrentItem(doomRow(QStringLiteral("Flags")), 1);
		QGuiApplication::clipboard()->clear();
		press(shell, Qt::Key_C, Qt::ControlModifier);
		check(QGuiApplication::clipboard()->text() == QStringLiteral("65") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Copied Flags: 65")),
			"Ctrl+C on a Doom field should copy its value.");
		// The two Space presses were two edits; the checks below count on the
		// three before them.
		trigger(shell, "map.undo");
		trigger(shell, "map.undo");
		for (QTabWidget* tabs : shell.findChildren<QTabWidget*>()) {
			for (int tab = 0; tab < tabs->count(); ++tab) {
				if (tabs->tabText(tab) == QStringLiteral("Inspector") && tabs->isVisible()) {
					tabs->setCurrentIndex(tab);
				}
			}
		}
		settle();
		snapshot(shell, "levels-doom-inspector");
		bool staleClip = false;
		for (QLabel* readout : shell.findChildren<QLabel*>(QStringLiteral("viewportReadout"))) {
			const auto* elided = dynamic_cast<ElidedLabel*>(readout);
			staleClip = staleClip || (readout->isVisible() && elided && elided->fullText().startsWith(QStringLiteral("Clip")));
		}
		check(!staleClip, "The readout under the view should not speak of the last map's clip tool.");
		QTreeWidgetItem* middle = doomRow(QStringLiteral("Middle"));
		check(middle && middle->text(1) == QStringLiteral("STARTAN3"), "The front side's middle texture should show.");
		if (middle) {
			middle->setText(1, QStringLiteral("brick1"));
			settle();
		}
		middle = doomRow(QStringLiteral("Middle"));
		check(middle && middle->text(1) == QStringLiteral("BRICK1"), "Editing the side's texture should set it, in capitals.");
		for (int step = 0; step < 3; ++step) {
			trigger(shell, "map.undo");
		}
		check(action(shell, "map.undo") && !action(shell, "map.undo")->isEnabled(), "Undo should take the three field edits back.");

		// Draw Sector: D turns the tool on; clicks put corners down on the
		// grid, Backspace takes one back, and a click on the first corner
		// closes the shape into a new sector, selected, with the tool left on
		// for the next. Escape leaves; undo takes the sector back.
		ensureActive(shell, "checkWadMaps: draw sector");
		map->setFocus(Qt::OtherFocusReason);
		settle();
		press(shell, Qt::Key_D);
		check(map->drawMode() && action(shell, "map.drawSector")->isChecked(), "D should turn Draw Sector on for a Doom map.");
		const auto clickAt = [map](double x, double y) {
			const QPoint at = map->viewPointFor(x, y).toPoint();
			QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, at);
			settle();
		};
		clickAt(-64.0, -64.0);
		clickAt(-64.0, 64.0);
		clickAt(64.0, 64.0);
		clickAt(80.0, -80.0);
		check(map->drawCorners().size() == 4, "Four clicks should put four corners down.");
		map->setFocus(Qt::OtherFocusReason);
		press(shell, Qt::Key_Backspace);
		check(map->drawCorners().size() == 3, "Backspace should take the last corner back.");
		clickAt(80.0, -80.0);
		map->setFocus(Qt::OtherFocusReason);
		press(shell, Qt::Key_Delete);
		check(map->drawCorners().size() == 3 && objects->model()->rowCount() == rows, "Delete while drawing should take the last corner back, not delete objects.");
		clickAt(64.0, -64.0);
		QTest::mouseMove(map, map->viewPointFor(0.0, -100.0).toPoint());
		settle();
		snapshot(shell, "levels-draw-sector");
		clickAt(-64.0, -64.0);
		check(map->drawMode() && map->drawCorners().isEmpty() && currentPath(objects) == QStringLiteral("sector:1")
				&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Drew sector 1 with 4 new linedef")),
			"Clicking the first corner should close the shape into sector 1, selected, and leave the tool on.");
		map->setFocus(Qt::OtherFocusReason);
		press(shell, Qt::Key_Escape);
		check(!map->drawMode() && !action(shell, "map.drawSector")->isChecked(), "Escape with no corners down should leave Draw Sector.");
		// With the ring round it selected, a right press inside the drawn room
		// picks the room's sector, as its hover outline shows, for the menu.
		QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, map->viewPointFor(-100.0, -100.0).toPoint());
		settle();
		check(map->selectionKind() == LevelMapSelectionKind::DoomSector && map->selectedObjectId() == 0, "A click in the ring should select sector 0.");
		const QPoint inner = map->viewPointFor(40.0, 40.0).toPoint();
		QTest::mousePress(map, Qt::RightButton, Qt::NoModifier, inner);
		QTest::mouseRelease(map, Qt::RightButton, Qt::NoModifier, inner);
		settle();
		check(map->selectionKind() == LevelMapSelectionKind::DoomSector && map->selectedObjectId() == 1 && map->selectionSet().size() == 1,
			"A right press inside the drawn room should select its sector, so the menu acts on it.");
		// Make Door asks for its textures, then makes the room a door in one step.
		QString doorDialog;
		whenDialogShows([&doorDialog](QWidget* dialog) {
			doorDialog = dialog->windowTitle();
			clickButton(dialog, QStringLiteral("Make Door"));
		});
		check(trigger(shell, "map.makeDoor") && doorDialog == QStringLiteral("Make Door")
				&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Made 1 door")),
			"Make Door should ask for the textures and make the drawn room a door.");
		trigger(shell, "map.undo");
		// Page Up and Down raise and lower from the map view, by 8 or with Shift
		// by 1, and Ctrl moves the ceiling; the objects list keeps its paging.
		// The door dialog can leave no window active for a moment.
		ensureActive(shell, "checkWadMaps: sector keys");
		map->setFocus(Qt::OtherFocusReason);
		check(action(shell, "map.raiseFloor") && action(shell, "map.raiseFloor")->isEnabled()
				&& action(shell, "map.raiseFloor")->shortcuts().contains(QKeySequence(Qt::Key_PageUp)),
			"Raise Floor should be enabled with a sector selected, on Page Up.");
		press(shell, Qt::Key_PageUp);
		check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Raise 1 floor by 8")), "Page Up on the map view should raise the floor by 8.");
		press(shell, Qt::Key_PageUp, Qt::ShiftModifier);
		check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Raise 1 floor by 1")), "Shift+Page Up should raise it by 1.");
		press(shell, Qt::Key_PageDown, Qt::ControlModifier);
		check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Lower 1 ceiling by 8")), "Ctrl+Page Down should lower the ceiling by 8.");
		for (int step = 0; step < 3; ++step) {
			trigger(shell, "map.undo");
		}
		objects->setFocus();
		shell.statusBar()->clearMessage();
		press(shell, Qt::Key_PageUp);
		check(!shell.statusBar()->currentMessage().startsWith(QStringLiteral("Raise")), "Page Up in the objects list should page, not raise a floor.");
		map->setFocus(Qt::OtherFocusReason);
		// The 3D preview turns the tool off, as it does the clip tool.
		check(trigger(shell, "map.drawSector") && map->drawMode() && trigger(shell, "map.toggle3D") && !map->drawMode()
				&& !action(shell, "map.drawSector")->isChecked(),
			"Switching to the 3D preview should turn Draw Sector off.");
		trigger(shell, "map.toggle3D");
		// Sectors are drawn from above: the tool turns the view to Top, and a
		// side view turns the tool off.
		QComboBox* projection = nullptr;
		for (QComboBox* combo : shell.findChildren<QComboBox*>()) {
			if (combo->accessibleName() == QStringLiteral("Map projection")) {
				projection = combo;
			}
		}
		if (projection) {
			projection->setCurrentIndex(1);
			settle();
			check(trigger(shell, "map.drawSector") && map->drawMode() && map->projection() == MapViewportProjection::TopXY,
				"Draw Sector should turn a side view back to Top.");
			projection->setCurrentIndex(2);
			settle();
			check(!map->drawMode() && !action(shell, "map.drawSector")->isChecked(), "A side view should turn Draw Sector off.");
			QMimeData thingPayload;
			thingPayload.setData(QString::fromLatin1(kMapPaletteMimeType), QByteArrayLiteral("thing:3001"));
			QDragEnterEvent sideEnter(map->rect().center(), Qt::CopyAction, &thingPayload, Qt::LeftButton, Qt::NoModifier);
			QCoreApplication::sendEvent(map, &sideEnter);
			check(!sideEnter.isAccepted(), "A Doom map's side view should not take a dropped thing: things are placed in plan.");
			projection->setCurrentIndex(0);
			settle();
		}
		trigger(shell, "map.undo");
		check(objects->model()->rowCount() == rows && !action(shell, "map.undo")->isEnabled(), "Undo should take the drawn sector back.");

		// Add Sector… takes corners typed as x,y pairs, for the keyboard.
		whenDialogShows([](QWidget* dialog) {
			if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
				input->setTextValue(QStringLiteral("512,0 512,128 640,128 640,0"));
				input->accept();
			} else {
				static_cast<QDialog*>(dialog)->reject();
			}
		});
		check(trigger(shell, "map.addSector") && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Drew sector 1 with 4 new linedef")),
			"Add Sector… should draw the typed corners.");
		trigger(shell, "map.undo");
		check(objects->model()->rowCount() == rows, "Undo should take the typed sector back.");
	}

	// A .map path has no maps to choose, so the chooser goes away.
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	check(!chooser->isVisible(), "The map chooser should hide for a .map file.");
}

void checkQuickOpen(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkQuickOpen");
	QAction* goToFile = action(shell, "shell.goToFile");
	check(goToFile && goToFile->shortcut() == QKeySequence(QStringLiteral("Ctrl+P")), "Go to File should be bound to Ctrl+P.");

	// A package entry opens on the surface that shows it.
	press(shell, Qt::Key_P, Qt::ControlModifier);
	auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("quickOpenFilter"));
	auto* list = shell.findChild<QListWidget*>(QStringLiteral("quickOpenList"));
	check(filter && filter->isVisible() && QApplication::focusWidget() == filter, "Ctrl+P should open Go to File with the cursor in its filter.");
	if (!filter || !list) {
		return;
	}
	QTest::keyClicks(filter, QStringLiteral("brick"));
	settle();
	check(list->count() >= 1 && list->item(0)->text() == QStringLiteral("brick_wall.tga"), "Typing part of a name should find the package's texture.");
	QTest::keyClick(filter, Qt::Key_Return);
	settle();
	auto* textures = child<QListWidget>(shell, "textureEntries");
	check(!filter->isVisible() && currentPage(shell) == Textures && currentPath(textures) == QStringLiteral("textures/base/brick_wall.tga"),
		"Enter should open the texture on the Textures surface.");

	// Recent files lead, so the script opened earlier comes before the
	// package's copy of it and opens for editing.
	ensureActive(shell, "checkQuickOpen: recent");
	press(shell, Qt::Key_P, Qt::ControlModifier);
	filter->clear();
	QTest::keyClicks(filter, QStringLiteral("autoexec"));
	settle();
	check(list->count() == 2 && list->item(0)->data(Qt::UserRole + 4).toString() == QDir::cleanPath(fixtures.script),
		"A recent file should come before a package entry of the same name.");
	QTest::keyClick(filter, Qt::Key_Return);
	settle();
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	check(currentPage(shell) == Code && editor && !editor->isReadOnly() && editor->toPlainText().contains(QStringLiteral("seta r_mode")),
		"Opening the recent script should show it, editable, in the code editor.");

	// Escape closes without opening anything.
	ensureActive(shell, "checkQuickOpen: escape");
	press(shell, Qt::Key_P, Qt::ControlModifier);
	QTest::keyClick(filter, Qt::Key_Escape);
	settle();
	check(!filter->isVisible() && currentPage(shell) == Code, "Escape should close Go to File and leave the page as it was.");
}

void checkGoToSymbol(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkGoToSymbol");
	shell.openPathFromCommandLine(fixtures.quakeC);
	settle();
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	check(currentPage(shell) == Code && editor && editor->toPlainText().contains(QStringLiteral("player_run")),
		"A QuakeC file should open in the code editor.");
	if (!editor) {
		return;
	}
	editor->setFocus();
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(0)));
	settle();
	// Ctrl+T lists the file's functions; typing narrows them and Enter goes.
	press(shell, Qt::Key_T, Qt::ControlModifier);
	auto* picker = shell.findChild<QDialog*>(QStringLiteral("symbolPicker"));
	auto* filter = picker ? picker->findChild<QLineEdit*>(QStringLiteral("quickOpenFilter")) : nullptr;
	auto* list = picker ? picker->findChild<QListWidget*>(QStringLiteral("quickOpenList")) : nullptr;
	check(picker && picker->isVisible() && filter && list && list->count() == 2, "Ctrl+T should list both QuakeC functions.");
	if (!picker || !filter || !list) {
		return;
	}
	QTest::keyClicks(filter, QStringLiteral("run"));
	settle();
	check(list->count() == 1 && list->item(0)->text() == QStringLiteral("player_run"), "Typing should narrow the symbols.");
	QTest::keyClick(filter, Qt::Key_Return);
	settle();
	check(!picker->isVisible() && editor->textCursor().blockNumber() + 1 == 6, "Enter should move the caret to the symbol's line.");
}

// The Code page keeps a tab per open file: each keeps its own text, undo
// history, and caret; a dot marks unsaved changes; Ctrl+Page Up and Down move
// between tabs; closing a changed one asks first; a middle click closes one.
void checkCodeTabs(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCodeTabs");
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	check(tabs && editor, "The Code page should have its tab bar and editor.");
	if (!tabs || !editor) {
		return;
	}
	// A tab by its file's full path, which leads its tooltip: two open files
	// can share a name.
	const auto tabFor = [tabs, &fixtures](const QString& name) {
		const QString path = name == QStringLiteral("player.qc") ? fixtures.quakeC : fixtures.script;
		for (int index = 0; index < tabs->count(); ++index) {
			if (tabs->tabToolTip(index).section(QLatin1Char('\n'), 0, 0) == QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath())) {
				return index;
			}
		}
		return -1;
	};
	shell.openPathFromCommandLine(fixtures.script);
	settle();
	shell.openPathFromCommandLine(fixtures.quakeC);
	settle();
	const int script = tabFor(QStringLiteral("autoexec.cfg"));
	const int quakeC = tabFor(QStringLiteral("player.qc"));
	check(script >= 0 && quakeC >= 0 && tabs->isVisible() && tabs->currentIndex() == quakeC && editor->toPlainText().contains(QStringLiteral("player_stand")),
		"Opening two files should give each a tab, with the second in front.");
	const int both = tabs->count();
	shell.openPathFromCommandLine(fixtures.script);
	settle();
	check(tabs->currentIndex() == script && tabs->count() == both && editor->toPlainText().contains(QStringLiteral("seta r_mode")),
		"Opening a file that is already open should bring its tab to the front, not add another.");

	// An edit in one tab survives a trip to the other, caret and undo included.
	shell.openPathFromCommandLine(fixtures.quakeC);
	settle();
	editor->setFocus();
	editor->moveCursor(QTextCursor::End);
	QTest::keyClicks(editor, QStringLiteral("// tab edit"));
	settle();
	const int caret = editor->textCursor().position();
	check(tabs->tabText(tabs->currentIndex()).endsWith(QString(QChar(0x2022))) && tabs->accessibleTabName(tabs->currentIndex()).contains(QStringLiteral("unsaved")),
		"An edited file's tab should carry a dot, and say so to a screen reader.");
	press(shell, Qt::Key_PageUp, Qt::ControlModifier);
	check(tabs->currentIndex() != tabFor(QStringLiteral("player.qc")) && !editor->toPlainText().contains(QStringLiteral("// tab edit")),
		"Ctrl+Page Up should move to the previous tab and show its file.");
	check(action(shell, "code.save") && !action(shell, "code.save")->isEnabled(), "Save should follow the tab in front, which has nothing to save.");
	press(shell, Qt::Key_PageDown, Qt::ControlModifier);
	check(tabs->currentIndex() == tabFor(QStringLiteral("player.qc")) && editor->toPlainText().contains(QStringLiteral("// tab edit"))
			&& editor->textCursor().position() == caret && editor->document()->isUndoAvailable() && action(shell, "code.save")->isEnabled(),
		"Coming back should find the edit, the caret where it was, and undo still there.");

	// Closing the changed tab asks first; Cancel keeps it, Discard closes it.
	const auto answer = [](QMessageBox::StandardButton which, const QString& text) {
		whenDialogShows([which, text](QWidget* dialog) {
			if (auto* box = qobject_cast<QMessageBox*>(dialog); box && box->button(which)) {
				box->button(which)->click();
			} else {
				clickButton(dialog, text);
			}
		});
	};
	answer(QMessageBox::Cancel, QStringLiteral("Cancel"));
	editor->setFocus();
	press(shell, Qt::Key_F4, Qt::ControlModifier);
	check(tabFor(QStringLiteral("player.qc")) >= 0, "Cancelling the unsaved-changes prompt should keep the tab.");
	// A dialog closing can leave no window active for a moment, and the key
	// only acts while focus is on the Code page.
	ensureActive(shell, "checkCodeTabs: discard");
	editor->setFocus();
	answer(QMessageBox::Discard, QStringLiteral("Discard"));
	press(shell, Qt::Key_F4, Qt::ControlModifier);
	check(tabFor(QStringLiteral("player.qc")) < 0 && !editor->toPlainText().contains(QStringLiteral("// tab edit")),
		"Discarding should close the tab and show another open file.");
	QFile quakeFile(fixtures.quakeC);
	check(quakeFile.open(QIODevice::ReadOnly) && !quakeFile.readAll().contains("// tab edit"), "A discarded edit should never reach the disk.");

	// A middle click closes a tab with nothing unsaved, without asking.
	const int open = tabs->count();
	const int target = tabFor(QStringLiteral("autoexec.cfg"));
	if (target >= 0) {
		const QPoint centre = tabs->tabRect(target).center();
		QTest::mouseClick(tabs, Qt::MiddleButton, Qt::NoModifier, centre);
		settle();
	}
	check(target >= 0 && tabs->count() == open - 1 && tabFor(QStringLiteral("autoexec.cfg")) < 0, "A middle click should close a tab.");
	snapshot(shell, "code-tabs");
}

void checkCodeLineEditing(ApplicationShell& shell)
{
	ensureActive(shell, "checkCodeLineEditing");
	trigger(shell, "shell.mode.code");
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	check(editor && !editor->isReadOnly(), "The script from Go to File should be open for editing.");
	if (!editor || editor->isReadOnly()) {
		return;
	}
	auto line = [editor](int number) {
		return editor->document()->findBlockByNumber(number).text();
	};
	const QString original = editor->toPlainText();
	const QString second = line(1);
	editor->setFocus();
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(1)));

	press(shell, Qt::Key_Slash, Qt::ControlModifier);
	check(line(1) == QStringLiteral("// ") + second, "Ctrl+/ should comment the caret's line with the file type's comment.");
	press(shell, Qt::Key_Slash, Qt::ControlModifier);
	check(line(1) == second, "Ctrl+/ again should take the comment away.");

	press(shell, Qt::Key_D, Qt::ControlModifier);
	check(line(1) == second && line(2) == second && editor->textCursor().blockNumber() == 2,
		"Ctrl+D should copy the line below itself and move onto the copy.");
	const QString third = line(3);
	press(shell, Qt::Key_Down, Qt::AltModifier);
	check(line(2) == third && line(3) == second && editor->textCursor().blockNumber() == 3,
		"Alt+Down should swap the line with the one below and keep the caret on it.");
	press(shell, Qt::Key_Up, Qt::AltModifier);
	check(line(2) == second && line(3) == third, "Alt+Up should move it back.");

	for (int step = 0; step < 5; ++step) {
		editor->undo();
	}
	settle();
	check(editor->toPlainText() == original, "Each line operation should undo in one step.");

	// Enter keeps indentation and goes one deeper after an opening brace; a
	// closing brace on the blank line steps back out.
	QTextCursor end(editor->document()->findBlockByNumber(0));
	end.movePosition(QTextCursor::EndOfBlock);
	editor->setTextCursor(end);
	QTest::keyClicks(editor, QStringLiteral(" {"));
	press(shell, Qt::Key_Return);
	check(line(1) == QStringLiteral("\t"), "Enter after an opening brace should indent the new line by one tab.");
	QTest::keyClicks(editor, QStringLiteral("(a)"));
	// The band and the pair are worked out once per turn of the event loop.
	settle();
	check(editor->extraSelections().size() == 3, "A bracket beside the caret should be highlighted with its partner.");
	press(shell, Qt::Key_Return);
	check(line(2) == QStringLiteral("\t"), "Enter should keep the line's indentation.");
	QTest::keyClicks(editor, QStringLiteral("}"));
	check(line(2) == QStringLiteral("}"), "A closing brace on a blank indented line should step back out.");
	for (int step = 0; step < 12 && editor->toPlainText() != original; ++step) {
		editor->undo();
	}
	check(editor->toPlainText() == original, "The typing should undo back to the file.");

	// Tab and Shift+Tab indent and outdent the selected lines together.
	QTextCursor lines(editor->document()->findBlockByNumber(0));
	lines.setPosition(editor->document()->findBlockByNumber(1).position() + 3, QTextCursor::KeepAnchor);
	editor->setTextCursor(lines);
	press(shell, Qt::Key_Tab);
	check(line(0).startsWith(QLatin1Char('\t')) && line(1).startsWith(QLatin1Char('\t')) && editor->textCursor().hasSelection(),
		"Tab should indent both selected lines and keep them selected.");
	press(shell, Qt::Key_Backtab, Qt::ShiftModifier);
	check(editor->toPlainText() == original, "Shift+Tab should outdent them again.");

	// With nothing selected, Shift+Tab outdents the caret's line and leaves a
	// caret in the same text, so the next key types rather than replacing the
	// line; Enter from the start of an indented line adds no indent.
	QTextCursor indent(editor->document()->findBlockByNumber(1));
	indent.insertText(QStringLiteral("\t\t"));
	QTextCursor caret(editor->document()->findBlockByNumber(1));
	caret.setPosition(caret.block().position() + 6);
	editor->setTextCursor(caret);
	press(shell, Qt::Key_Backtab, Qt::ShiftModifier);
	check(line(1) == QStringLiteral("\t") + second && !editor->textCursor().hasSelection() && editor->textCursor().positionInBlock() == 5,
		"Shift+Tab with nothing selected should outdent the line and keep the caret in its text.");
	QTest::keyClicks(editor, QStringLiteral("x"));
	const QString typed = QStringLiteral("\t") + second.left(4) + QStringLiteral("x") + second.mid(4);
	check(line(1) == typed, "The key after Shift+Tab should type, not replace the line.");
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(1)));
	press(shell, Qt::Key_Return);
	check(line(1).isEmpty() && line(2) == typed, "Enter at the start of an indented line should move it down without adding indent.");
	for (int step = 0; step < 8 && editor->toPlainText() != original; ++step) {
		editor->undo();
	}
	check(editor->toPlainText() == original, "The indent edits should undo back to the file.");
	editor->document()->setModified(false);

	// Ctrl+H shows the find bar with its replace row. Enter in the replace
	// field replaces the selected match and moves on; Ctrl+Enter replaces the
	// rest as one undo step.
	editor->setFocus();
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(0)));
	settle();
	press(shell, Qt::Key_H, Qt::ControlModifier);
	auto* findField = child<QLineEdit>(shell, "codeFindField");
	auto* replaceField = child<QLineEdit>(shell, "codeReplaceField");
	check(findField && findField->isVisible() && replaceField && replaceField->isVisible(), "Ctrl+H should show the find bar with its replace row.");
	if (findField && replaceField && line(1).startsWith(QStringLiteral("seta ")) && line(2).startsWith(QStringLiteral("seta "))) {
		findField->setText(QStringLiteral("seta"));
		replaceField->setText(QStringLiteral("set"));
		replaceField->setFocus();
		settle();
		check(editor->textCursor().selectedText() == QStringLiteral("seta"), "The match Enter will replace should be selected.");
		press(shell, Qt::Key_Return);
		check(line(1).startsWith(QStringLiteral("set r_mode")) && line(2).startsWith(QStringLiteral("seta ")),
			"Enter in the replace field should replace the selected match only.");
		press(shell, Qt::Key_Return, Qt::ControlModifier);
		check(!editor->toPlainText().contains(QStringLiteral("seta")), "Ctrl+Enter should replace every remaining match.");
		editor->undo();
		check(line(1).startsWith(QStringLiteral("set r_mode")) && line(2).startsWith(QStringLiteral("seta ")), "Replace All should undo in one step.");
		editor->undo();
		check(editor->toPlainText() == original, "Undoing the single replace should give back the file.");
		replaceField->setFocus();
		settle();
		press(shell, Qt::Key_Escape);
		check(!replaceField->isVisible() && QApplication::focusWidget() == editor, "Escape in the replace field should close the bar.");

		// A file that turns read-only under an open replace row keeps the row
		// in view, disabled, saying why, rather than taking Enter and doing
		// nothing.
		press(shell, Qt::Key_H, Qt::ControlModifier);
		editor->setReadOnly(true);
		settle();
		check(replaceField->isVisible() && !replaceField->isEnabled() && replaceField->placeholderText() == QStringLiteral("Read-only file"),
			"A read-only file should keep the replace row showing, disabled, saying it is read-only.");
		editor->setReadOnly(false);
		settle();
		check(replaceField->isEnabled() && replaceField->placeholderText() == QStringLiteral("Replace with"),
			"The replace row should come back once the file can be edited again.");
		replaceField->setFocus();
		settle();
		press(shell, Qt::Key_Escape);
	}
	editor->document()->setModified(false);

	// Ctrl+Tab leaves the editor, since Tab indents.
	editor->setFocus();
	settle();
	press(shell, Qt::Key_Tab, Qt::ControlModifier);
	check(QApplication::focusWidget() != editor, "Ctrl+Tab should move focus out of the code editor.");
}

// The Audio surface lists a resource WAD's Doom sounds, draws their waveform,
// and moves the playhead from the mouse and the keyboard. Playback itself is
// not started: a test machine may have no audio device, and a passing run
// should not make a sound.
void checkAudioBrowser(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkAudioBrowser");
	shell.openPathFromCommandLine(fixtures.soundWad);
	settle();
	check(currentPage(shell) == Packages, "A Doom WAD with no maps should open as a package, not a map.");
	check(trigger(shell, "shell.mode.audio"), "The Audio surface should open.");
	auto* entries = child<QListWidget>(shell, "audioEntries");
	auto* waveform = child<WaveformView>(shell, "waveformView");
	auto* time = child<QLabel>(shell, "audioTime");
	QAction* play = action(shell, "audio.playPause");
	QAction* stop = action(shell, "audio.stop");
	check(entries && waveform && time && play && stop, "The Audio surface should have its list, waveform, time readout, and transport.");
	if (!entries || !waveform || !time || !play || !stop) {
		return;
	}
	const int tone = rowWithData(entries, Qt::UserRole, QStringLiteral("DSTONE"));
	const int beep = rowWithData(entries, Qt::UserRole, QStringLiteral("DPBEEP"));
	check(tone >= 0 && beep >= 0 && rowWithData(entries, Qt::UserRole, QStringLiteral("TEXTLUMP")) < 0,
		"The WAD's DS and DP lumps, and nothing else, should be listed as sounds.");
	entries->setCurrentRow(tone);
	settle();
	check(waveform->hasPeaks() && waveform->durationMs() == 1000, "A DMX sound should draw a one-second waveform.");
	check(time->text() == QStringLiteral("0:00.00 / 0:01.00"), "The time readout should show the playhead and the length.");
	check(play->isEnabled() == (VIBESTUDIO_HAVE_AUDIO_PLAYBACK != 0) && !stop->isEnabled(),
		"Play should be offered when the build can play, and Stop only once something plays.");
	check(play->shortcut() == QKeySequence(Qt::Key_Space), "Space should play and pause.");

	// A click halfway along moves the playhead there, and the keyboard steps it.
	QTest::mouseClick(waveform, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(waveform->xForTime(500)), waveform->height() / 3));
	settle();
	check(std::abs(waveform->playhead() - 500) <= 8 && waveform->hasFocus(), "A click in the waveform should move the playhead and focus it.");
	check(time->text().endsWith(QStringLiteral(" / 0:01.00")) && time->text() != QStringLiteral("0:00.00 / 0:01.00"),
		"The time readout should follow the playhead.");
	press(shell, Qt::Key_End);
	check(waveform->playhead() == 1000 && time->text() == QStringLiteral("0:01.00 / 0:01.00"), "End should move the playhead to the end.");
	press(shell, Qt::Key_Home);
	press(shell, Qt::Key_Right);
	check(waveform->playhead() == 20, "Right should step the playhead a fiftieth of the sound.");
	press(shell, Qt::Key_PageDown);
	check(waveform->playhead() == 120 && time->text() == QStringLiteral("0:00.12 / 0:01.00"), "Page Down should step it a tenth.");
	check(waveform->accessibleDescription().contains(QStringLiteral("playhead at")), "The waveform should tell a screen reader where the playhead is.");
	snapshot(shell, "audio-dmx");

	// Space presses a focused transport button; it plays only from the list
	// or the waveform.
	if (auto* loopButton = child<QToolButton>(shell, "audioLoop"); loopButton && action(shell, "audio.loop")->isEnabled()) {
		loopButton->setFocus(Qt::TabFocusReason);
		press(shell, Qt::Key_Space);
		check(action(shell, "audio.loop")->isChecked(), "Space on the focused Loop button should turn loop on, not play the sound.");
		press(shell, Qt::Key_Space);
		check(!action(shell, "audio.loop")->isChecked(), "Space again should turn it off.");
	}
	// A PC speaker sound has nothing to draw or play; back on the tone, the
	// playhead starts from the top again.
	entries->setCurrentRow(beep);
	settle();
	check(!waveform->hasPeaks() && !play->isEnabled(), "A PC speaker sound should have no waveform and nothing to play.");
	entries->setCurrentRow(tone);
	settle();
	check(waveform->playhead() == 0 && time->text() == QStringLiteral("0:00.00 / 0:01.00"), "Another sound should start from the top.");

	// Real playback needs an audio device and makes a sound, so it runs only
	// when VIBESTUDIO_TEST_PLAYBACK is set, and then at zero volume.
	if (VIBESTUDIO_HAVE_AUDIO_PLAYBACK == 0 || !qEnvironmentVariableIsSet("VIBESTUDIO_TEST_PLAYBACK")) {
		return;
	}
	if (auto* volume = child<QSlider>(shell, "audioVolume")) {
		volume->setValue(0);
	}
	waveform->setFocus();
	press(shell, Qt::Key_End);
	press(shell, Qt::Key_PageUp);
	const qint64 cursor = waveform->playhead();
	check(trigger(shell, "audio.playPause"), "Play should start the tone.");
	QElapsedTimer clock;
	clock.start();
	while (clock.elapsed() < 3000 && !(withoutMnemonic(play->text()) == QStringLiteral("Pause Sound") && waveform->playhead() > cursor)) {
		wait(20);
	}
	check(withoutMnemonic(play->text()) == QStringLiteral("Pause Sound") && stop->isEnabled(), "Play should turn into Pause, with Stop offered.");
	check(waveform->playhead() > cursor, "The playhead should move while the tone plays.");
	check(trigger(shell, "audio.stop"), "Stop should be available while playing.");
	check(waveform->playhead() == cursor && withoutMnemonic(play->text()) == QStringLiteral("Play Sound") && !stop->isEnabled(),
		"Stop should put the playhead back where playback started.");
	// From the end, Play starts at the top, and the tone runs out on its own.
	press(shell, Qt::Key_End);
	check(trigger(shell, "audio.playPause"), "Play should start again from the end.");
	clock.restart();
	while (clock.elapsed() < 4000 && withoutMnemonic(play->text()) == QStringLiteral("Pause Sound")) {
		wait(20);
	}
	check(withoutMnemonic(play->text()) == QStringLiteral("Play Sound") && waveform->playhead() == 1000,
		"A tone that runs out should stop by itself, with the playhead back at the cursor.");
}

// The resource WAD checkAudioBrowser opened: its flat and sprite, found by
// their namespace markers, and its PLAYPAL are listed on the Textures surface,
// and the Automatic palette decodes them with the WAD's own PLAYPAL.
void checkWadTextures(ApplicationShell& shell)
{
	ensureActive(shell, "checkWadTextures");
	check(trigger(shell, "shell.mode.textures"), "The Textures surface should open.");
	auto* entries = child<QListWidget>(shell, "textureEntries");
	auto* preview = child<ImagePreviewView>(shell, "texturePreview");
	check(entries && preview, "The Textures surface should have its list and preview.");
	if (!entries || !preview) {
		return;
	}
	const int flat = rowWithData(entries, Qt::UserRole, QStringLiteral("FLOOR0"));
	const int sprite = rowWithData(entries, Qt::UserRole, QStringLiteral("TROOA1"));
	check(flat >= 0 && sprite >= 0 && rowWithData(entries, Qt::UserRole, QStringLiteral("PLAYPAL")) >= 0,
		"A Doom WAD's flat, sprite, and PLAYPAL should be listed as images.");
	check(rowWithData(entries, Qt::UserRole, QStringLiteral("F_START")) < 0 && rowWithData(entries, Qt::UserRole, QStringLiteral("DSTONE")) < 0,
		"Namespace markers and sounds should not be listed as images.");
	entries->setCurrentRow(flat);
	settle();
	settleImage(preview);
	check(preview->hasImage() && preview->image().size() == QSize(64, 64), "A WAD flat should decode as a 64x64 flat.");
	check(preview->hasImage() && preview->image().pixel(3, 2) == qRgb(5, 250, 128),
		"The Automatic palette should decode the flat with the WAD's own PLAYPAL.");
	entries->setCurrentRow(sprite);
	settle();
	settleImage(preview);
	check(preview->hasImage() && preview->image().size() == QSize(4, 4) && qAlpha(preview->image().pixel(0, 0)) == 255
			&& (preview->image().pixel(1, 1) & 0xffffff) == (qRgb(7, 248, 128) & 0xffffff),
		"A WAD sprite should decode as a 4x4 patch in PLAYPAL colours.");
	bool fromPackage = false;
	for (QLabel* label : page(shell, Textures)->findChildren<QLabel*>()) {
		fromPackage = fromPackage || label->text() == QStringLiteral("Palette read from the open package: PLAYPAL");
	}
	check(fromPackage, "The palette readout should say PLAYPAL came from the open package.");
	snapshot(shell, "textures-doom-wad");
}

void checkLooseAssetsAndStagedGuard(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkLooseAssetsAndStagedGuard");
	// Stage a deletion in the open package.
	shell.openPathFromCommandLine(fixtures.package);
	settle();
	auto entries = tests::PackageRows(child<PackageEntryView>(shell, "packageEntries"));
	auto* filter = child<QLineEdit>(shell, "packageFilter");
	QAction* saveAs = action(shell, "package.saveAs");
	check(entries && filter && saveAs, "The Packages page should be available.");
	if (!entries || !filter || !saveAs) {
		return;
	}
	filter->setText(QStringLiteral("metal_panel"));
	settle();
	entries->setCurrentRow(0);
	entries->setFocus();
	press(shell, Qt::Key_Delete);
	filter->clear();
	settle();
	check(saveAs->isEnabled(), "Del should stage a deletion to test the guard with.");
	entries->setFocus();
	press(shell, Qt::Key_Z, Qt::ControlModifier);
	check(!saveAs->isEnabled(), "Ctrl+Z on the Packages page should take the staged change back out of the plan.");
	filter->setText(QStringLiteral("metal_panel"));
	settle();
	entries->setCurrentRow(0);
	entries->setFocus();
	press(shell, Qt::Key_Delete);
	filter->clear();
	settle();
	check(saveAs->isEnabled(), "Del should stage the deletion again.");

	// Opening something that replaces the package asks first; Cancel keeps it.
	auto asked = std::make_shared<bool>(false);
	whenDialogShows([asked](QWidget* dialog) {
		*asked = withoutMnemonic(dialog->windowTitle()) == QStringLiteral("Discard Staged Changes");
		clickButton(dialog, QStringLiteral("Cancel"));
	});
	shell.openPathFromCommandLine(fixtures.looseTexture);
	settle();
	check(*asked, "Opening a loose texture's folder should ask before discarding staged changes.");
	check(saveAs->isEnabled() && chipText(shell, QStringLiteral("Package status")).contains(QStringLiteral("assets.pk3")),
		"Cancel should keep the package and its staged changes.");

	// Discard goes ahead: the texture's folder opens and the texture shows.
	ensureActive(shell, "checkLooseAssetsAndStagedGuard: discard");
	whenDialogShows([](QWidget* dialog) {
		clickButton(dialog, QStringLiteral("Discard"));
	});
	shell.openPathFromCommandLine(fixtures.looseTexture);
	settle();
	auto* textures = child<QListWidget>(shell, "textureEntries");
	check(currentPage(shell) == Textures && currentPath(textures) == QStringLiteral("stone.tga"),
		"A loose texture should open on the Textures surface, selected.");
	check(!saveAs->isEnabled(), "The discarded plan should be gone.");
	auto* preview = shell.findChild<ImagePreviewView*>(QStringLiteral("texturePreview"));
	settleImage(preview);
	check(preview && preview->hasImage(), "The loose texture should be decoded and shown.");
}

// Takes every staged change back out of the plan the way a user would: Delete
// on each change in the Packages staging list.
void unstageAll(ApplicationShell& shell)
{
	// Keys need an active window, and a dialog may just have closed.
	ensureActive(shell, "unstageAll");
	trigger(shell, "shell.mode.packages");
	auto staging = tests::StagingRows(child<PackageStagingView>(shell, "packageStagingSummary"));
	if (!staging) {
		return;
	}
	for (QWidget* parent = staging->parentWidget(); parent; parent = parent->parentWidget()) {
		if (auto* tabs = qobject_cast<QTabWidget*>(parent)) {
			tabs->setCurrentWidget(staging->parentWidget());
			break;
		}
	}
	settle();
	for (int guard = 0; guard < 8; ++guard) {
		int operationRow = -1;
		for (int row = 0; row < staging->count(); ++row) {
			if (!staging->item(row)->data(Qt::UserRole + 5).toString().isEmpty()) {
				operationRow = row;
				break;
			}
		}
		if (operationRow < 0) {
			return;
		}
		staging->setFocus();
		staging->setCurrentRow(operationRow);
		press(shell, Qt::Key_Delete);
	}
}

void checkShortcutReference(ApplicationShell& shell)
{
	ensureActive(shell, "checkShortcutReference");
	// The reference lists each key with the surface it works on, and filters.
	auto found = std::make_shared<QString>();
	auto shownAfterFilter = std::make_shared<int>(-1);
	whenDialogShows([found, shownAfterFilter](QWidget* dialog) {
		auto* table = dialog->findChild<QTreeWidget*>(QStringLiteral("shortcutTable"));
		auto* filter = dialog->findChild<QLineEdit*>(QStringLiteral("shortcutFilter"));
		if (table && filter) {
			filter->setText(QStringLiteral("duplicate"));
			int shown = 0;
			for (int index = 0; index < table->topLevelItemCount(); ++index) {
				const QTreeWidgetItem* row = table->topLevelItem(index);
				if (row->isHidden()) {
					continue;
				}
				++shown;
				if (row->text(0) == QStringLiteral("Duplicate Selection")) {
					*found = QStringLiteral("%1 | %2").arg(row->text(1), row->text(2));
				}
			}
			*shownAfterFilter = shown;
		}
		static_cast<QDialog*>(dialog)->reject();
	});
	check(trigger(shell, "help.keyboardShortcuts"), "Keyboard Shortcuts should open from the Help menu.");
	settle();
	check(*found == QStringLiteral("Ctrl+D | Levels"), "Duplicate Selection should read as Ctrl+D on Levels.");
	check(*shownAfterFilter >= 2, "Filtering by \"duplicate\" should keep the map and code duplicate commands.");

	// The view's own keys are listed too.
	auto frame = std::make_shared<QString>();
	whenDialogShows([frame](QWidget* dialog) {
		auto* table = dialog->findChild<QTreeWidget*>(QStringLiteral("shortcutTable"));
		for (int index = 0; table && index < table->topLevelItemCount(); ++index) {
			if (table->topLevelItem(index)->text(0) == QStringLiteral("Frame the selection")) {
				*frame = QStringLiteral("%1 | %2").arg(table->topLevelItem(index)->text(1), table->topLevelItem(index)->text(2));
			}
		}
		static_cast<QDialog*>(dialog)->reject();
	});
	trigger(shell, "help.keyboardShortcuts");
	settle();
	check(*frame == QStringLiteral("F | Levels view"), "The map view's F key should be listed.");
}

// Keys can be changed: Change Keys records new ones, says which command a
// key would be taken from and takes it, Reset puts the defaults back on
// both, a key the studio relies on cannot be changed, and the user's keys
// are kept in settings.
void checkCustomShortcuts(ApplicationShell& shell)
{
	ensureActive(shell, "checkCustomShortcuts");
	QAction* zoomReset = action(shell, "code.zoomReset");
	QAction* detect = action(shell, "game.detectInstallations");
	const QKeySequence detectKeys(QStringLiteral("Ctrl+Shift+G"));
	check(zoomReset && detect && zoomReset->shortcut().isEmpty() && detect->shortcut() == detectKeys,
		"Reset Editor Zoom should start with no keys, and Detect Game Installations with Ctrl+Shift+G.");
	if (!zoomReset || !detect) {
		return;
	}
	const auto selectRow = [](QTreeWidget* table, const QString& label) -> QTreeWidgetItem* {
		for (int index = 0; table && index < table->topLevelItemCount(); ++index) {
			if (table->topLevelItem(index)->text(0) == label) {
				table->setCurrentItem(table->topLevelItem(index));
				return table->topLevelItem(index);
			}
		}
		return nullptr;
	};
	auto clash = std::make_shared<QString>();
	auto cancelLocked = std::make_shared<bool>(false);
	auto rowMarked = std::make_shared<bool>(false);
	whenNamedDialogShows(QStringLiteral("keyboardShortcuts"), [=](QWidget* dialog) {
		auto* table = dialog->findChild<QTreeWidget*>(QStringLiteral("shortcutTable"));
		auto* change = dialog->findChild<QPushButton*>(QStringLiteral("shortcutChange"));
		selectRow(table, QStringLiteral("Cancel Selected Task"));
		*cancelLocked = change && !change->isEnabled();
		QTreeWidgetItem* row = selectRow(table, QStringLiteral("Reset Editor Zoom"));
		whenNamedDialogShows(QStringLiteral("shortcutCaptureDialog"), [clash](QWidget* capture) {
			if (auto* edit = capture->findChild<QKeySequenceEdit*>(QStringLiteral("shortcutCapture"))) {
				edit->setFocus();
				QTest::keyClick(edit, Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
			}
			if (auto* label = capture->findChild<QLabel*>(QStringLiteral("shortcutClash"))) {
				*clash = label->text();
			}
			if (auto* assign = capture->findChild<QPushButton*>(QStringLiteral("shortcutAssign"))) {
				assign->click();
			}
		});
		if (change && change->isEnabled()) {
			change->click();
		}
		*rowMarked = row && row->font(1).bold() && row->text(1) == QKeySequence(QStringLiteral("Ctrl+Shift+G")).toString(QKeySequence::NativeText);
		static_cast<QDialog*>(dialog)->reject();
	});
	trigger(shell, "help.keyboardShortcuts");
	check(*cancelLocked, "The Escape key that cancels a task should not be changeable.");
	check(clash->contains(QStringLiteral("Detect Game Installations")), "Taking a key another command has should say which command.");
	check(zoomReset->shortcut() == detectKeys && detect->shortcut().isEmpty(),
		"The key should move to Reset Editor Zoom, taken from Detect Game Installations.");
	check(*rowMarked, "The table should show the new key, marked as the user's own.");
	check(StudioSettings().userShortcuts().value(QStringLiteral("code.zoomReset")) == QStringList {QStringLiteral("Ctrl+Shift+G")},
		"The user's key should be kept in settings.");

	whenNamedDialogShows(QStringLiteral("keyboardShortcuts"), [=](QWidget* dialog) {
		selectRow(dialog->findChild<QTreeWidget*>(QStringLiteral("shortcutTable")), QStringLiteral("Reset Editor Zoom"));
		if (auto* reset = dialog->findChild<QPushButton*>(QStringLiteral("shortcutReset")); reset && reset->isEnabled()) {
			reset->click();
		}
		static_cast<QDialog*>(dialog)->reject();
	});
	trigger(shell, "help.keyboardShortcuts");
	check(zoomReset->shortcut().isEmpty() && detect->shortcut() == detectKeys && StudioSettings().userShortcuts().isEmpty(),
		"Reset should give both commands their default keys back and forget the user's.");
}

// With the preference on, the last session comes back: its package, the map
// it had open in a WAD, and its code files with the same one in front, on the
// page the studio was on; a file gone since is counted, not opened. Off, it
// does nothing.
void checkSessionRestore(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkSessionRestore");
	trigger(shell, "shell.mode.workspace");
	StudioSettings settings;
	StudioSession session;
	session.packagePath = fixtures.package;
	session.mapPath = fixtures.doomWad;
	session.mapName = QStringLiteral("MAP02");
	session.codeFiles = {fixtures.quakeC, fixtures.script, QDir(fixtures.project).filePath(QStringLiteral("gone.cfg"))};
	session.currentCodeFile = fixtures.quakeC;
	settings.setLastSession(session);
	settings.setRestoreSession(true);
	settings.sync();
	shell.restoreLastSession();
	settle();
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	auto* mapName = child<QComboBox>(shell, "levelMapName");
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	check(currentPage(shell) == Workspace, "Reopening the session should leave the studio on the page it was on.");
	check(mapName && mapName->currentText() == QStringLiteral("MAP02"), "The WAD should reopen on the map that was open, MAP02.");
	check(tabs && editor && editor->toPlainText().contains(QStringLiteral("player_stand")), "The code file that was in front should be in front again.");
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Reopened the last session: assets.pk3, MAP02 in rooms.wad"))
			&& shell.statusBar()->currentMessage().contains(QStringLiteral("could not be found")),
		"The status bar should say what came back, and that a file was missing.");
	// Off, nothing is reopened.
	settings.setRestoreSession(false);
	settings.sync();
	shell.statusBar()->clearMessage();
	shell.restoreLastSession();
	settle();
	check(shell.statusBar()->currentMessage().isEmpty(), "With the preference off, nothing should be reopened.");
	settings.setRestoreSession(true);
	settings.sync();
}

// The map view draws Doom tag links touching the selection, or all of them
// while links are shown, and paints them without trouble.
void checkDoomTagLinks(const Fixtures& fixtures)
{
	LevelMapDocument document;
	QString error;
	check(loadLevelMap({fixtures.doomWad, QStringLiteral("MAP01"), {}}, &document, &error), "The Doom fixture should load for tag links.");
	int sector = -1;
	check(drawLevelMapDoomSector(&document, {{-64.0, -64.0, 0.0, true}, {-64.0, 64.0, 0.0, true}, {64.0, 64.0, 0.0, true}, {64.0, -64.0, 0.0, true}}, &sector, &error)
			&& setLevelMapSectorProperty(&document, sector, QStringLiteral("tag"), QStringLiteral("7"), &error)
			&& setLevelMapLinedefProperty(&document, 0, QStringLiteral("special"), QStringLiteral("62"), &error)
			&& setLevelMapLinedefProperty(&document, 0, QStringLiteral("tag"), QStringLiteral("7"), &error),
		"A drawn room tagged 7 and a lift line tagged 7 should be set up.");
	MapViewport view;
	view.resize(480, 360);
	view.setDocument(document);
	// Links are shown by default; hidden, only the selection's are drawn. The
	// drawn room comes back selected, so the selection is cleared first.
	view.setShowTargetLinks(false);
	view.setSelectionSet({});
	check(view.tagLinkCount() == 1 && view.drawnTagLinkCount() == 0, "With links hidden and nothing selected, the view should know the link but not draw it.");
	view.setSelectionSet({{LevelMapSelectionKind::DoomLinedef, 0}});
	check(view.drawnTagLinkCount() == 1, "Selecting the line should draw its link to the tagged room.");
	if (const QString directory = qEnvironmentVariable("VIBESTUDIO_TEST_SNAPSHOTS"); !directory.isEmpty()) {
		QDir().mkpath(directory);
		view.grab().save(QDir(directory).filePath(QStringLiteral("doom-tag-links.png")));
	}
	view.setSelectionSet({});
	view.setShowTargetLinks(true);
	check(view.drawnTagLinkCount() == 1 && !view.grab().isNull(), "With links shown, every tag link should be drawn.");
}

// A PWAD that ships no PLAYPAL takes the selected Doom installation's: its
// flats show in the IWAD's colours, and the readout says where they came from.
void checkInstallationPalette(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkInstallationPalette");
	const QDir root(QFileInfo(fixtures.soundWad).dir().filePath(QStringLiteral("doomgame")));
	QDir().mkpath(root.path());
	const auto wadOf = [](const QVector<std::pair<QByteArray, QByteArray>>& lumps) {
		QByteArray body;
		QByteArray directory;
		for (const auto& lump : lumps) {
			appendLe32(&directory, static_cast<quint32>(12 + body.size()));
			appendLe32(&directory, static_cast<quint32>(lump.second.size()));
			appendFixed(&directory, lump.first, 8);
			body.append(lump.second);
		}
		QByteArray wad("IWAD");
		appendLe32(&wad, static_cast<quint32>(lumps.size()));
		appendLe32(&wad, static_cast<quint32>(12 + body.size()));
		return wad + body + directory;
	};
	// The IWAD's PLAYPAL maps index i to (255 - i, i, 64).
	QByteArray playpal;
	for (int index = 0; index < 256; ++index) {
		playpal.append(static_cast<char>(255 - index));
		playpal.append(static_cast<char>(index));
		playpal.append(static_cast<char>(64));
	}
	playpal.append(QByteArray(768 * 13, '\0'));
	const QString iwad = root.filePath(QStringLiteral("doom2.wad"));
	writeFile(iwad, wadOf({{"PLAYPAL", playpal}}));
	QByteArray flat;
	for (int y = 0; y < 64; ++y) {
		for (int x = 0; x < 64; ++x) {
			flat.append(static_cast<char>((x + y) % 256));
		}
	}
	QByteArray pwadBytes = wadOf({{"F_START", {}}, {"FLOOR9", flat}, {"F_END", {}}});
	pwadBytes.replace(0, 4, "PWAD");
	const QString pwad = root.filePath(QStringLiteral("floors.wad"));
	writeFile(pwad, pwadBytes);

	StudioSettings settings;
	GameInstallationProfile doom;
	doom.gameKey = QStringLiteral("doom2");
	doom.engineFamily = GameEngineFamily::IdTech1;
	doom.displayName = QStringLiteral("Test Doom");
	doom.rootPath = root.path();
	doom.executablePath = QCoreApplication::applicationFilePath();
	doom.basePackagePaths = {iwad};
	doom.readOnly = true;
	settings.upsertGameInstallation(doom);
	QString previous = settings.selectedGameInstallationId();
	for (const GameInstallationProfile& profile : settings.gameInstallations()) {
		if (profile.displayName == QStringLiteral("Test Doom")) {
			settings.setSelectedGameInstallation(profile.id);
		}
	}
	settings.sync();

	shell.openPathFromCommandLine(pwad);
	settle();
	check(trigger(shell, "shell.mode.textures"), "The Textures surface should open.");
	auto* entries = child<QListWidget>(shell, "textureEntries");
	auto* preview = child<ImagePreviewView>(shell, "texturePreview");
	const int row = entries ? rowWithData(entries, Qt::UserRole, QStringLiteral("FLOOR9")) : -1;
	check(row >= 0 && preview, "The PWAD's flat should be listed.");
	if (row >= 0 && preview) {
		entries->setCurrentRow(row);
		settle();
		settleImage(preview);
		check(preview->hasImage() && preview->image().pixel(3, 2) == qRgb(250, 5, 64),
			"A PWAD with no PLAYPAL should decode with the selected installation's IWAD palette.");
		bool labelled = false;
		for (QLabel* label : page(shell, Textures)->findChildren<QLabel*>()) {
			labelled = labelled || label->text() == QStringLiteral("Palette read from the game installation: PLAYPAL in doom2.wad");
		}
		check(labelled, "The readout should say the palette came from the game installation.");
	}
	settings.setSelectedGameInstallation(previous);
	settings.sync();
}

// Files dropped on an open package's entries are staged into the folder the
// list shows, a dropped folder keeping its layout; the staging list shows them.
void checkPackageDropStaging(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkPackageDropStaging");
	shell.openPathFromCommandLine(fixtures.package);
	settle();
	auto entries = tests::PackageRows(child<PackageEntryView>(shell, "packageEntries"));
	auto* tree = child<PackageFolderView>(shell, "packageTree");
	auto staging = tests::StagingRows(child<PackageStagingView>(shell, "packageStagingSummary"));
	check(currentPage(shell) == Packages && entries && tree && staging, "The package should open on the Packages page.");
	if (!entries || !tree || !staging) {
		return;
	}
	tests::waitForPackageEntries(entries); tree->selectFolder(QStringLiteral("textures"));
	settle();
	const QDir source(QFileInfo(fixtures.soundWad).dir().filePath(QStringLiteral("dropped")));
	QDir().mkpath(source.filePath(QStringLiteral("decals/sub")));
	writeFile(source.filePath(QStringLiteral("decals/one.txt")), QByteArrayLiteral("one"));
	writeFile(source.filePath(QStringLiteral("decals/sub/two.txt")), QByteArrayLiteral("two"));
	writeFile(source.filePath(QStringLiteral("three.cfg")), QByteArrayLiteral("three"));
	QMimeData data;
	data.setUrls({QUrl::fromLocalFile(source.filePath(QStringLiteral("decals"))), QUrl::fromLocalFile(source.filePath(QStringLiteral("three.cfg")))});
	const QPoint at = entries->mapTo(&shell, entries->rect().center());
	QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(&shell, &enter);
	QDropEvent drop(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(&shell, &drop);
	settle();
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Staged 3 file(s) into textures")),
		"Dropping a folder and a file on the list should stage three files into the folder it shows.");
	check(rowWithData(staging, Qt::UserRole, QStringLiteral("textures/decals/sub/two.txt")) >= 0
			&& rowWithData(staging, Qt::UserRole, QStringLiteral("textures/three.cfg")) >= 0,
		"The staging list should show the dropped folder's layout under textures/.");

	// Dragging entries out gives the desktop files copied out of the package:
	// a file as itself, a folder with its layout.
	QModelIndexList dragged;
	for (int row = 0; row < entries->count(); ++row) {
		const QString path = entries->item(row)->data(Qt::UserRole).toString();
		if (path == QStringLiteral("textures/base")) {
			dragged << entries->model()->index(row, 0);
		}
	}
	std::unique_ptr<QMimeData> payload(dragged.isEmpty() ? nullptr : entries->model()->mimeData(dragged));
	const QList<QUrl> urls = payload ? payload->urls() : QList<QUrl>();
	const QString folder = urls.isEmpty() ? QString() : urls.first().toLocalFile();
	check(urls.size() == 1 && QFileInfo(folder).isDir() && QFileInfo::exists(QDir(folder).filePath(QStringLiteral("brick_wall.tga"))),
		"Dragging the base folder out should hand the desktop a copy of it with its textures inside.");
	std::unique_ptr<QMimeData> again(dragged.isEmpty() ? nullptr : entries->model()->mimeData(dragged));
	const QString second = again && !again->urls().isEmpty() ? again->urls().first().toLocalFile() : QString();
	check(!second.isEmpty() && second != folder && QFileInfo::exists(QDir(second).filePath(QStringLiteral("brick_wall.tga")))
			&& QFileInfo(second).fileName() == QStringLiteral("base"),
		"Each drag should get a folder of its own, named as the dragged folder is.");
}

// A session that ended without shutting down is offered back, not reopened:
// the notice bar says what happened, shows the kept report, and reopens the
// session's files only when asked. From then on what is open is recorded as
// it changes, so a later crash would lose none of it.
void checkCrashRecovery(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCrashRecovery");
	// What a real crash leaves behind: a marker whose process is gone (process
	// id 0 is never running) and the report that session wrote.
	const QDir crashes(QDir(fixtures.project).filePath(QStringLiteral("crashes")));
	const QString sessionId = QStringLiteral("0123456789abcdef0123456789abcdef");
	writeFile(crashes.filePath(QStringLiteral("session-0-%1.marker").arg(sessionId)),
		QStringLiteral("VibeStudio session marker\nformatVersion: 1\nversion: 0.9.9-test\nsessionId: %1\nprocessId: 0\nsessionStarted: %2\n")
			.arg(sessionId, QDateTime::currentDateTimeUtc().addSecs(-7200).toString(Qt::ISODate))
			.toUtf8());
	CrashReportInfo crash;
	crash.formatVersion = 1;
	crash.version = QStringLiteral("0.9.9-test");
	crash.sessionId = sessionId;
	crash.sessionStarted = QDateTime::currentDateTimeUtc().addSecs(-7200);
	crash.crashedAt = QDateTime::currentDateTimeUtc().addSecs(-3600);
	crash.reasonId = QStringLiteral("signal");
	crash.reasonDetail = QStringLiteral("SIGSEGV");
	crash.logLines = {QStringLiteral("warning: the last thing the session logged")};
	writeFile(crashes.filePath(QStringLiteral("crash-20260901T100500-0-%1.txt").arg(sessionId)), formatCrashReportText(crash));
	CrashHandlerOptions options;
	options.enabled = false;
	options.installOsHandlers = false;
	options.directory = crashes.path();
	installCrashHandling(options);
	check(previousSessionCrashed() && !previousSessionCrashReport().path.isEmpty(),
		"The seeded crash should read as the previous session's, with its report.");

	// The file that was open when it crashed.
	const QString crashedFile = QDir(fixtures.project).filePath(QStringLiteral("crashed_session.cfg"));
	writeFile(crashedFile, QByteArrayLiteral("// open when the studio crashed\n"));
	{
		StudioSettings settings;
		StudioSession session;
		session.codeFiles = {crashedFile};
		session.currentCodeFile = crashedFile;
		session.mapPath = fixtures.map;
		settings.setLastSession(session);
		settings.setRestoreSession(true);
		settings.sync();
	}
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	const QString crashedNative = QDir::toNativeSeparators(QFileInfo(crashedFile).absoluteFilePath());
	const auto crashedFileOpen = [tabs, crashedNative]() {
		for (int index = 0; tabs && index < tabs->count(); ++index) {
			if (tabs->tabToolTip(index).section(QLatin1Char('\n'), 0, 0) == crashedNative) {
				return true;
			}
		}
		return false;
	};
	shell.beginSession(true);
	settle();
	auto* notice = child<NoticeBar>(shell, "noticeBar");
	check(notice && notice->isVisible() && notice->stateId() == QStringLiteral("failed")
			&& notice->title() == QStringLiteral("The studio closed unexpectedly last time."),
		"After a crash the notice bar should say the studio closed unexpectedly.");
	check(!crashedFileOpen(), "The crashed session's files should be offered, not reopened.");
	snapshot(shell, "crash-recovery-notice");

	// Tab reaches the notice's actions in the order they are drawn, then its
	// close button.
	auto* reopenButton = child<QPushButton>(shell, "noticeReopenSession");
	auto* viewButton = child<QPushButton>(shell, "noticeViewReport");
	if (reopenButton && viewButton) {
		reopenButton->setFocus();
		settle();
		QTest::keyClick(reopenButton, Qt::Key_Tab);
		settle();
		const bool toView = QApplication::focusWidget() == viewButton;
		QTest::keyClick(viewButton, Qt::Key_Tab);
		settle();
		const bool toClose = QApplication::focusWidget() && QApplication::focusWidget()->objectName() == QStringLiteral("noticeClose");
		check(toView && toClose, "Tab should go from Reopen Last Session to View Report to the notice's close button.");
	}
	// Until the offer is answered, the crashed session stays recorded: a second
	// crash, or a close, must not lose it.
	QTest::qWait(1300);
	check(StudioSettings().lastSession().codeFiles == QStringList {crashedFile}, "While the crash offer is up, the crashed session should stay recorded.");

	// View Report shows the kept report in full.
	QString shown;
	bool listShown = true;
	whenDialogShows([&shown, &listShown](QWidget* modal) {
		if (auto* text = modal->findChild<QPlainTextEdit*>(QStringLiteral("crashReportText"))) {
			shown = text->toPlainText();
		}
		if (auto* list = modal->findChild<QListWidget*>(QStringLiteral("crashReportList"))) {
			listShown = list->isVisible();
		}
		if (auto* dialog = qobject_cast<QDialog*>(modal)) {
			dialog->reject();
		}
	});
	if (auto* view = child<QPushButton>(shell, "noticeViewReport")) {
		view->click();
	}
	settle();
	check(shown.contains(QStringLiteral("SIGSEGV")) && shown.contains(QStringLiteral("the last thing the session logged")) && !listShown,
		"View Report should show the one kept report, its reason and log lines, without a list to choose from.");
	// So does Help > Crash Reports, from the reports on disk.
	shown.clear();
	whenDialogShows([&shown](QWidget* modal) {
		if (auto* text = modal->findChild<QPlainTextEdit*>(QStringLiteral("crashReportText"))) {
			shown = text->toPlainText();
		}
		if (auto* dialog = qobject_cast<QDialog*>(modal)) {
			dialog->reject();
		}
	});
	trigger(shell, "diagnostics.crashReports");
	check(shown.contains(QStringLiteral("SIGSEGV")), "Help > Crash Reports should list the report kept on disk.");
	check(notice && notice->isVisible(), "Viewing the report should leave the notice up.");

	// Reopen Last Session brings the files back and clears the notice.
	// Work in progress is asked about before the offer replaces it: a map
	// edited since start keeps its edits when the question is cancelled.
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	if (objects && objects->model()->rowCount() > 1) {
		trigger(shell, "shell.mode.levels");
		setObjectCurrentRow(objects, objects->model()->rowCount() - 1);
		settle();
		trigger(shell, "map.deleteSelection");
	}
	const bool edited = action(shell, "map.undo") && action(shell, "map.undo")->isEnabled();
	auto mapQuestion = std::make_shared<QString>();
	whenDialogShows([mapQuestion](QWidget* modal) {
		*mapQuestion = modal->windowTitle();
		if (auto* box = qobject_cast<QMessageBox*>(modal); box && box->button(QMessageBox::Cancel)) {
			box->button(QMessageBox::Cancel)->click();
		}
	});
	ensureActive(shell, "checkCrashRecovery reopen");
	if (auto* reopen = child<QPushButton>(shell, "noticeReopenSession")) {
		reopen->click();
	}
	settle();
	check(notice && !notice->isVisible() && crashedFileOpen(), "Reopen Last Session should reopen the crashed session's files and clear the notice.");
	check(edited && *mapQuestion == QStringLiteral("Unsaved Map Edits") && action(shell, "map.undo")->isEnabled()
			&& shell.statusBar()->currentMessage().contains(QStringLiteral("left closed")),
		"Reopening over unsaved map edits should ask first, and keep them when the question is cancelled.");

	// Recording has begun: within a beat the settings hold what is open now,
	// the package included, which the crashed session did not have.
	QTest::qWait(1300);
	const StudioSession recorded = StudioSettings().lastSession();
	const bool recordedFile = std::any_of(recorded.codeFiles.cbegin(), recorded.codeFiles.cend(), [&crashedNative](const QString& path) {
		return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()) == crashedNative;
	});
	check(recordedFile && !recorded.packagePath.isEmpty(), "What is open should be recorded as the session while the studio runs.");

	// Esc while focus is in the bar dismisses a notice.
	if (notice) {
		notice->showNotice(QStringLiteral("warning"), QStringLiteral("Test notice"), QStringLiteral("Dismiss this with Esc."));
		QPushButton* act = notice->addAction(QStringLiteral("Act"));
		settle();
		act->setFocus();
		QTest::keyClick(act, Qt::Key_Escape);
		settle();
		check(!notice->isVisible(), "Esc in the notice bar should dismiss the notice.");
	}
}

// Ctrl+= and Ctrl+- step the code editor's zoom and Ctrl+wheel does too; the
// readout shows the level and a click on it resets it; a tab brought to the
// front takes the zoomed text; and the level is kept in settings.
void checkCodeZoom(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCodeZoom");
	shell.openPathFromCommandLine(fixtures.script);
	settle();
	shell.openPathFromCommandLine(fixtures.quakeC);
	settle();
	auto* editor = dynamic_cast<StudioCodeEditor*>(child<QPlainTextEdit>(shell, "codeEditor"));
	auto* readout = child<QToolButton>(shell, "codeZoomReadout");
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	check(editor && readout && tabs && tabs->count() >= 2 && currentPage(shell) == Code,
		"The Code page should show the editor, two tabs, and the zoom readout.");
	if (!editor || !readout || !tabs || tabs->count() < 2) {
		return;
	}
	const qreal base = editor->font().pointSizeF();
	check(editor->zoomPercent() == 100 && !readout->isVisible(), "The editor should start at 100%, with no zoom readout.");
	editor->setFocus();
	settle();
	press(shell, Qt::Key_Equal, Qt::ControlModifier);
	check(editor->zoomPercent() == 110 && qAbs(editor->font().pointSizeF() - base * 1.1) < 0.01,
		"Ctrl+= should zoom the editor one step, to 110%.");
	check(readout->isVisible() && readout->text() == QStringLiteral("110%") && StudioSettings().codeZoomPercent() == 110,
		"The readout should show 110%, and the level should be kept.");
	press(shell, Qt::Key_Minus, Qt::ControlModifier);
	press(shell, Qt::Key_Minus, Qt::ControlModifier);
	check(editor->zoomPercent() == 90, "Ctrl+- twice should step down to 90%.");
	// Ctrl with the wheel zooms instead of scrolling.
	const QPointF inside(40.0, 40.0);
	QWheelEvent wheel(inside, editor->viewport()->mapToGlobal(inside), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
		Qt::NoScrollPhase, false);
	QCoreApplication::sendEvent(editor->viewport(), &wheel);
	settle();
	check(editor->zoomPercent() == 100, "Ctrl+wheel up should zoom one step, back to 100%.");
	// Another tab takes the zoomed text when it comes to the front.
	press(shell, Qt::Key_Equal, Qt::ControlModifier);
	tabs->setCurrentIndex((tabs->currentIndex() + 1) % tabs->count());
	settle();
	check(qAbs(editor->document()->defaultFont().pointSizeF() - base * 1.1) < 0.01, "A tab brought to the front should show the zoomed text.");
	// The readout puts it back.
	readout->click();
	settle();
	check(editor->zoomPercent() == 100 && !readout->isVisible() && StudioSettings().codeZoomPercent() == 100,
		"Clicking the readout should reset the zoom to 100% and hide it.");
}

// Pumps events until `done` holds or `timeoutMs` passes. The document watch
// polls once a second and holds a change back a quarter second to settle.
bool waitFor(const std::function<bool()>& done, int timeoutMs = 6000)
{
	QElapsedTimer timer;
	timer.start();
	while (!done()) {
		if (timer.elapsed() > timeoutMs) {
			return false;
		}
		QTest::qWait(50);
	}
	return true;
}

// The code tab showing `path`, by the full path that leads its tooltip.
int codeTabFor(QTabBar* tabs, const QString& path)
{
	const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
	for (int index = 0; tabs && index < tabs->count(); ++index) {
		if (tabs->tabToolTip(index).section(QLatin1Char('\n'), 0, 0) == native) {
			return index;
		}
	}
	return -1;
}

// Closes every code tab, discarding changes, so a check starts with none.
void closeAllCodeTabs(ApplicationShell& shell)
{
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	for (int guard = 0; tabs && tabs->count() > 0 && guard < 50; ++guard) {
		tabs->setCurrentIndex(0);
		settle();
		if (tabs->tabText(0).contains(QChar(0x2022))) {
			whenDialogShows([](QWidget* modal) {
				if (auto* box = qobject_cast<QMessageBox*>(modal); box && box->button(QMessageBox::Discard)) {
					box->button(QMessageBox::Discard)->click();
				}
			});
		}
		trigger(shell, "code.closeFile");
	}
}

// Answers the next question with No, and records its title.
void answerNextQuestionNo(const std::shared_ptr<QString>& title)
{
	whenDialogShows([title](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			*title = box->windowTitle();
			if (QAbstractButton* no = box->button(QMessageBox::No)) {
				no->click();
			}
		}
	}, 800);
}

// A nearer face hides what is behind it in the camera: a selected brush
// behind a wall leaves no highlight or hatch on the wall, and with edges
// shown, its edges stay hidden too.
void checkCameraDepth(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCameraDepth");
	auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
	auto* combo = child<QComboBox>(shell, "editorProfileCombo");
	auto* pathField = child<QLineEdit>(shell, "levelMapPath");
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	check(camera && combo && pathField && objects, "The shell should have the camera, the profile choice, and the objects list.");
	if (!camera || !combo || !pathField || !objects) {
		return;
	}
	const QString previousMap = pathField->text();
	// Classic plane-format boxes, as the fixture map writes them.
	const auto box = [](int x0, int y0, int z0, int x1, int y1, int z1) {
		const auto face = [](int ax, int ay, int az, int bx, int by, int bz, int cx, int cy, int cz) {
			return QStringLiteral("( %1 %2 %3 ) ( %4 %5 %6 ) ( %7 %8 %9 ) base/wall 0 0 0 1 1\n")
				.arg(ax).arg(ay).arg(az).arg(bx).arg(by).arg(bz).arg(cx).arg(cy).arg(cz);
		};
		return QStringLiteral("{\n") + face(x0, y0, z0, x0, y0 + 1, z0, x0, y0, z0 + 1) + face(x1, y0, z0, x1, y0, z0 + 1, x1, y0 + 1, z0)
			+ face(x0, y0, z0, x0, y0, z0 + 1, x0 + 1, y0, z0) + face(x0, y1, z0, x0 + 1, y1, z0, x0, y1, z0 + 1)
			+ face(x0, y0, z0, x0 + 1, y0, z0, x0, y0 + 1, z0) + face(x0, y0, z1, x0, y0 + 1, z1, x0 + 1, y0, z1) + QStringLiteral("}\n");
	};
	const QString path = QFileInfo(fixtures.map).dir().filePath(QStringLiteral("depth.map"));
	writeFile(path, (QStringLiteral("{\n\"classname\" \"worldspawn\"\n") + box(100, -200, 0, 116, 200, 256) + box(300, -32, 64, 364, 32, 128)
		+ QStringLiteral("}\n")).toUtf8());
	combo->setCurrentIndex(combo->findData(QStringLiteral("trenchbroom")));
	settle();
	shell.openPathFromCommandLine(path);
	settle();
	trigger(shell, "shell.mode.levels");
	objects->clearSelection();
	const int far = rowWithData(objects, Qt::UserRole, QStringLiteral("brush:1"));
	if (far >= 0) {
		setObjectCurrentRow(objects, far);
		setObjectSelected(objects, far, true);
	}
	settle();
	check(far >= 0 && camera->isVisible() && camera->highlightedTriangleCount() > 0, "The box behind the wall should be selected and lit in the camera.");
	// From the origin, looking down +X: the wall fills the middle of the view
	// and the box sits squarely behind it.
	camera->setShowEdges(false);
	camera->setCameraView(ModelVec3 {0.0f, 0.0f, 96.0f}, 0.0, 0.0);
	settle();
	const auto colours = [camera](const QRect& area) {
		const QImage image = camera->grab().toImage();
		QSet<QRgb> seen;
		for (int y = area.top(); y <= area.bottom(); ++y) {
			for (int x = area.left(); x <= area.right(); ++x) {
				seen.insert(image.pixel(x, y));
			}
		}
		return seen.size();
	};
	const QPoint centre = camera->rect().center();
	check(colours(QRect(centre - QPoint(10, 10), QSize(21, 21))) == 1, "A selected brush behind a wall should not show through it.");
	camera->setShowEdges(true);
	settle();
	check(colours(QRect(centre.x() - 60, centre.y(), 121, 1)) == 1, "The edges of a brush behind a wall should not show through it.");
	camera->setShowEdges(false);

	combo->setCurrentIndex(combo->findData(QStringLiteral("vibestudio-default")));
	settle();
	if (!previousMap.isEmpty()) {
		shell.openPathFromCommandLine(previousMap);
		settle();
	}
	ensureActive(shell, "checkCameraDepth: done");
}

// The Assistant asks a model about the open work. AI-free mode keeps it shut;
// a local runtime (here a fake provider on 127.0.0.1) answers with the
// context the user ticked, paths shortened; the conversation carries on;
// Cancel and failures leave the question marked and out of the history; an
// endpoint off this machine shows the exact request and asks first; Preview
// sends nothing; and Settings' Test Connection asks for a one-word reply.
void checkAssistant(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkAssistant");
	auto* dock = shell.findChild<QDockWidget*>(QStringLiteral("assistantDock"));
	auto* connection = child<QLabel>(shell, "assistantConnection");
	auto* context = child<QListWidget>(shell, "assistantContext");
	auto* transcript = shell.findChild<QTextBrowser*>(QStringLiteral("assistantTranscript"));
	auto* prompt = child<QPlainTextEdit>(shell, "assistantPrompt");
	auto* send = child<QPushButton>(shell, "assistantSend");
	auto* cancel = child<QPushButton>(shell, "assistantCancel");
	auto* status = child<QLabel>(shell, "assistantStatus");
	check(dock && connection && context && transcript && prompt && send && cancel && status, "The shell should have the Assistant dock and its parts.");
	if (!dock || !connection || !context || !transcript || !prompt || !send || !cancel || !status) {
		return;
	}
	FakeAiProvider provider;
	check(provider.listening(), "The fake provider should listen.");
	const auto setAi = [](const std::function<void(AiAutomationPreferences&)>& change) {
		StudioSettings settings;
		AiAutomationPreferences preferences = settings.aiAutomationPreferences();
		change(preferences);
		settings.setAiAutomationPreferences(preferences);
		settings.sync();
	};
	const auto ask = [prompt](const QString& question) {
		prompt->setPlainText(question);
		prompt->setFocus(Qt::OtherFocusReason);
		QTest::keyClick(prompt, Qt::Key_Return, Qt::ControlModifier);
		settle();
	};
	const auto lastBody = [&provider]() {
		return provider.exchanges().isEmpty() ? QJsonObject() : QJsonDocument::fromJson(provider.exchanges().last().body).object();
	};
	const auto lastUserText = [&lastBody]() {
		const QJsonArray messages = lastBody().value(QStringLiteral("messages")).toArray();
		return messages.isEmpty() ? QString() : messages.last().toObject().value(QStringLiteral("content")).toString();
	};
	auto* pathField = child<QLineEdit>(shell, "levelMapPath");
	const QString previousMap = pathField ? pathField->text() : QString();

	// AI-free mode, the default, keeps the Assistant shut and says where to
	// open it.
	setAi([](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = true;
	});
	trigger(shell, "ai.assistant");
	check(dock->isVisible() && !send->isEnabled() && connection->text().contains(QStringLiteral("AI-free")),
		"In AI-free mode the Assistant opens but cannot send, and says why.");

	// A local runtime, set up through Settings' Assistant Model fields.
	setAi([](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = false;
		preferences.cloudConnectorsEnabled = false;
		preferences.preferredReasoningConnectorId.clear();
		preferences.preferredLocalConnectorId = QStringLiteral("local-offline");
		preferences.connectorModels.clear();
		preferences.connectorEndpoints.clear();
	});
	auto* textConnector = child<QComboBox>(shell, "aiTextConnector");
	auto* textModel = child<QLineEdit>(shell, "aiTextModel");
	auto* textEndpoint = child<QLineEdit>(shell, "aiTextEndpoint");
	check(textConnector && textModel && textEndpoint, "Settings should have the Assistant Model fields.");
	if (textConnector && textModel && textEndpoint) {
		textConnector->setCurrentIndex(textConnector->findData(QStringLiteral("local-offline")));
		textModel->setText(QStringLiteral("test-model"));
		emit textModel->editingFinished();
		textEndpoint->setText(provider.baseUrl(QStringLiteral("/v1")));
		emit textEndpoint->editingFinished();
		settle();
	}
	const AiAutomationPreferences saved = StudioSettings().aiAutomationPreferences();
	check(saved.connectorModels.value(QStringLiteral("local-offline")) == QStringLiteral("test-model")
			&& saved.connectorEndpoints.value(QStringLiteral("local-offline")) == provider.baseUrl(QStringLiteral("/v1")),
		"The Assistant Model fields should save the connector's model and endpoint.");
	// Saving any other preference keeps what the form does not show. Opening
	// Settings shows what is stored, the AI mode set above included.
	trigger(shell, "shell.mode.settings");
	for (QCheckBox* box : shell.findChildren<QCheckBox*>()) {
		if (box->accessibleName() == QStringLiteral("Text to speech")) {
			box->click();
			settle();
			box->click();
			settle();
			break;
		}
	}
	check(StudioSettings().aiAutomationPreferences().connectorModels.value(QStringLiteral("local-offline")) == QStringLiteral("test-model"),
		"Saving the other preferences should keep each connector's model.");
	check(send->isEnabled() && connection->text().contains(QStringLiteral("test-model")) && connection->text().contains(QStringLiteral("on this machine")),
		"With a local runtime set up, the Assistant should be ready and say nothing leaves the machine.");

	// On the Levels page the open map goes with the question by default.
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	trigger(shell, "shell.mode.levels");
	trigger(shell, "ai.assistant");
	int mapRow = -1;
	for (int row = 0; row < context->count(); ++row) {
		if (context->item(row)->data(Qt::UserRole).toString() == QStringLiteral("map")) {
			mapRow = row;
		}
	}
	check(mapRow >= 0 && context->item(mapRow)->checkState() == Qt::Checked, "On the Levels page the map summary should be ticked to go.");
	provider.answer(200, FakeAiProvider::openAiAnswer(QStringLiteral("Your map has **one brush**, `brush:0`.")));
	ask(QStringLiteral("How many brushes are there?"));
	check(waitFor([transcript]() { return transcript->toPlainText().contains(QStringLiteral("one brush")); }, 8000),
		"The runtime's answer should appear in the conversation.");
	check(provider.exchanges().size() == 1 && provider.exchanges().last().path == "/v1/chat/completions"
			&& !provider.exchanges().last().headers.contains("authorization"),
		"The question should reach the local runtime, without a key.");
	const QString sent = lastUserText();
	const QString home = QDir::homePath();
	check(sent.contains(QStringLiteral("How many brushes are there?")) && sent.contains(QStringLiteral("## Map"))
			&& !sent.contains(home, Qt::CaseInsensitive) && !sent.contains(QDir::toNativeSeparators(home), Qt::CaseInsensitive),
		"The question should go with the map summary, the home folder never named.");
	check(lastBody().value(QStringLiteral("model")).toString() == QStringLiteral("test-model")
			&& lastBody().value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("role")).toString() == QStringLiteral("system"),
		"The request should name the model and lead with the assistant's instructions.");
	check(status->text().contains(QStringLiteral("Answered")) && status->text().contains(QStringLiteral("42 tokens in")),
		"The status should say the question was answered, with the token counts.");
	// Code in an answer is set at the text's size, not the smaller system
	// fixed font.
	double codeSize = -1.0;
	for (QTextBlock block = transcript->document()->begin(); block.isValid(); block = block.next()) {
		for (auto it = block.begin(); !it.atEnd(); ++it) {
			if (it.fragment().text() == QStringLiteral("brush:0")) {
				codeSize = it.fragment().charFormat().fontPointSize();
			}
		}
	}
	check(codeSize > 0.0 && std::abs(codeSize - transcript->fontInfo().pointSizeF()) < 0.01, "Code in an answer should be set at the text's size.");
	snapshot(shell, "assistant-answered");

	// The conversation carries on: the next question sees the first exchange.
	provider.answer(200, FakeAiProvider::openAiAnswer(QStringLiteral("Add a light.")));
	ask(QStringLiteral("What next?"));
	check(waitFor([transcript]() { return transcript->toPlainText().contains(QStringLiteral("Add a light.")); }, 8000)
			&& lastBody().value(QStringLiteral("messages")).toArray().size() == 4,
		"A follow-up should carry the earlier question and answer.");

	// Cancel: the question stays in view, marked, and leaves the history.
	provider.hang();
	ask(QStringLiteral("Will this ever come?"));
	check(waitFor([cancel]() { return cancel->isVisible() && cancel->isEnabled(); }, 3000), "While waiting, Cancel should be offered.");
	cancel->click();
	check(waitFor([status]() { return status->text().contains(QStringLiteral("Cancelled")); }, 5000) && send->isEnabled(),
		"Cancel should stop the question and say so.");
	check(transcript->toPlainText().contains(QStringLiteral("Not answered")), "The cancelled question should stay in view, marked.");
	provider.answer(500, R"({"error":{"message":"The runtime ran out of memory."}})");
	ask(QStringLiteral("Try again?"));
	check(waitFor([status]() { return status->text().contains(QStringLiteral("ran out of memory")); }, 8000),
		"A provider failure should be reported in its own words.");
	check(lastBody().value(QStringLiteral("messages")).toArray().size() == 6,
		"Unanswered questions should not go to the model with the next one.");

	// Preview sends nothing.
	const qsizetype asked = provider.exchanges().size();
	prompt->setPlainText(QStringLiteral("Preview this"));
	whenNamedDialogShows(QStringLiteral("assistantPreviewDialog"), [](QWidget* dialog) {
		auto* text = dialog->findChild<QPlainTextEdit*>(QStringLiteral("assistantPreviewText"));
		check(text && text->toPlainText().contains(QStringLiteral("=== user ===")) && text->toPlainText().contains(QStringLiteral("Preview this"))
				&& text->toPlainText().startsWith(QStringLiteral("POST http://127.0.0.1")),
			"Preview should show the request Send would make, each message's text as written.");
		// The raw JSON is a tick away.
		if (auto* raw = dialog->findChild<QCheckBox*>(QStringLiteral("assistantPreviewTextRaw")); raw && text) {
			raw->setChecked(true);
			check(text->toPlainText().contains(QStringLiteral("\"messages\": [")), "Show the raw JSON should show the body as it goes.");
		}
		qobject_cast<QDialog*>(dialog)->reject();
	});
	if (auto* preview = child<QPushButton>(shell, "assistantPreview")) {
		preview->click();
	}
	settle();
	check(provider.exchanges().size() == asked, "Preview should send nothing.");

	// Off this machine: the exact request is shown and nothing goes without a
	// yes; a yes for the project is remembered.
	setAi([](AiAutomationPreferences& preferences) {
		preferences.cloudConnectorsEnabled = true;
		preferences.preferredReasoningConnectorId = QStringLiteral("custom-http");
		preferences.connectorModels.insert(QStringLiteral("custom-http"), QStringLiteral("team-model"));
		preferences.connectorEndpoints.insert(QStringLiteral("custom-http"), QStringLiteral("http://llm.example.invalid/v1"));
	});
	{
		StudioSettings settings;
		settings.clearAiContextConsent();
		settings.sync();
	}
	trigger(shell, "ai.assistant");
	check(connection->text().contains(QStringLiteral("llm.example.invalid")), "The Assistant should say where a cloud question goes.");
	bool consentShown = false;
	whenNamedDialogShows(QStringLiteral("assistantConsentDialog"), [&consentShown](QWidget* dialog) {
		auto* request = dialog->findChild<QPlainTextEdit*>(QStringLiteral("assistantConsentRequest"));
		consentShown = request && request->toPlainText().contains(QStringLiteral("llm.example.invalid")) && request->toPlainText().contains(QStringLiteral("Off the machine?"));
		qobject_cast<QDialog*>(dialog)->reject();
	});
	ask(QStringLiteral("Off the machine?"));
	settle();
	check(consentShown && status->text().contains(QStringLiteral("Not sent")), "A first question off the machine should show the request and wait for a yes.");
	const QString project = StudioSettings().currentProjectPath();
	whenNamedDialogShows(QStringLiteral("assistantConsentDialog"), [](QWidget* dialog) {
		if (auto* remember = dialog->findChild<QCheckBox*>(QStringLiteral("assistantConsentRemember"))) {
			remember->setChecked(true);
		}
		if (auto* yes = dialog->findChild<QPushButton*>(QStringLiteral("assistantConsentSend"))) {
			yes->click();
		}
	});
	ask(QStringLiteral("Off the machine, yes."));
	check(waitFor([status]() { return status->text().contains(QStringLiteral("Not answered")) && !status->text().contains(QStringLiteral("Waiting")); }, 15000),
		"The unreachable endpoint should fail with a reason.");
	check(StudioSettings().aiContextConsentGiven(project, QStringLiteral("custom-http@llm.example.invalid")),
		"Consent given for the project should be remembered.");

	// Ask About This Code narrows the context to the selection.
	setAi([](AiAutomationPreferences& preferences) {
		preferences.cloudConnectorsEnabled = false;
		preferences.preferredReasoningConnectorId.clear();
	});
	shell.openPathFromCommandLine(fixtures.quakeC);
	settle();
	if (auto* editor = child<QPlainTextEdit>(shell, "codeEditor")) {
		QTextCursor cursor = editor->textCursor();
		cursor.movePosition(QTextCursor::Start);
		cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
		editor->setTextCursor(cursor);
		trigger(shell, "ai.askAboutCode");
		QStringList ticked;
		for (int row = 0; row < context->count(); ++row) {
			if (context->item(row)->checkState() == Qt::Checked) {
				ticked << context->item(row)->data(Qt::UserRole).toString();
			}
		}
		check(ticked == QStringList {QStringLiteral("code-selection")} && prompt->toPlainText().contains(QStringLiteral("this code")),
			"Ask About This Code should tick the selection alone and propose the question.");
	}

	// Settings' Test Connection asks for a one-word reply.
	provider.answer(200, FakeAiProvider::openAiAnswer(QStringLiteral("OK"), QStringLiteral("test-model")));
	auto* test = child<QPushButton>(shell, "aiTextTest");
	auto* result = child<QLabel>(shell, "aiTextTestResult");
	if (textConnector && test && result) {
		textConnector->setCurrentIndex(textConnector->findData(QStringLiteral("local-offline")));
		test->click();
		check(waitFor([result]() { return result->text().contains(QStringLiteral("Answered")) && result->text().contains(QStringLiteral("OK")); }, 8000),
			"Test Connection should report the model's reply.");
		check(!provider.exchanges().isEmpty() && QString::fromUtf8(provider.exchanges().last().body).contains(QStringLiteral("Reply with the single word OK.")),
			"Test Connection should send its fixed question, nothing from the project.");
	}

	// Back to AI-free, with no connector set up, and the map that was open.
	setAi([](AiAutomationPreferences& preferences) {
		preferences = AiAutomationPreferences();
	});
	if (auto* fresh = child<QPushButton>(shell, "assistantClear")) {
		fresh->click();
	}
	dock->hide();
	if (!previousMap.isEmpty()) {
		shell.openPathFromCommandLine(previousMap);
		settle();
	}
	ensureActive(shell, "checkAssistant: done");
}

// Editor profiles make the Levels page work like the editor a user knows:
// TrenchBroom's one 3D-first view, right-drag look, WASD flying, and a drag
// that draws a brush; NetRadiant Custom's camera beside the 2D view, Shift
// to select, clicks that step down a stack, and a right click that toggles
// mouse look; each with its own keys. The VibeStudio controls come back as
// they were.
void checkEditorProfiles(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkEditorProfiles");
	auto* combo = child<QComboBox>(shell, "editorProfileCombo");
	auto* map = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
	auto* controlsButton = child<QToolButton>(shell, "levelControlsButton");
	check(combo && map && camera && controlsButton, "The shell should have its profile choice, both Levels views, and the Controls button.");
	if (!combo || !map || !camera || !controlsButton) {
		return;
	}
	const auto choose = [&shell, combo](const char* id) {
		combo->setCurrentIndex(combo->findData(QString::fromLatin1(id)));
		settle();
		ensureActive(shell, "checkEditorProfiles: profile");
	};
	const auto keysOf = [&shell](const char* commandId) {
		QAction* command = action(shell, commandId);
		return command ? QKeySequence::listToString(command->shortcuts(), QKeySequence::PortableText) : QStringLiteral("(missing)");
	};
	const auto brushCount = [&shell]() {
		int count = 0;
		if (auto* objects = child<LevelObjectList>(shell, "levelMapObjects")) {
			for (int row = 0; row < objects->model()->rowCount(); ++row) {
				count += objectIndex(objects, row).data(Qt::UserRole).toString().startsWith(QStringLiteral("brush:")) ? 1 : 0;
			}
		}
		return count;
	};
	const auto drag = [](QWidget* view, Qt::MouseButton button, Qt::KeyboardModifiers keys, QPoint from, QPoint to) {
		QTest::mousePress(view, button, keys, from);
		for (int step = 1; step <= 6; ++step) {
			const QPoint at = from + (to - from) * step / 6;
			QMouseEvent move(QEvent::MouseMove, QPointF(at), view->mapToGlobal(QPointF(at)), Qt::NoButton, button, keys);
			QCoreApplication::sendEvent(view, &move);
		}
		QTest::mouseRelease(view, button, keys, to);
		settle();
	};

	// Whatever map was open goes back at the end: later checks expect it.
	auto* pathField = child<QLineEdit>(shell, "levelMapPath");
	auto* nameField = child<QComboBox>(shell, "levelMapName");
	const QString previousMap = pathField ? pathField->text() : QString();
	const QString previousName = nameField ? nameField->currentText() : QString();
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	trigger(shell, "shell.mode.levels");

	// TrenchBroom.
	choose("trenchbroom");
	check(controlsButton->text() == QStringLiteral("TrenchBroom") && map->gridSize() == 16,
		"Choosing TrenchBroom should name it on the Levels bar and bring its 16-unit grid.");
	check(camera->isVisible() && !map->isVisible() && camera->isPerspective() && camera->hasMesh(),
		"TrenchBroom's layout should be one view, the 3D camera first.");
	// With no map the camera folds away, and the next map brings it back.
	if (trigger(shell, "map.close")) {
		settle();
		check(!camera->isVisible(), "With the map closed there is no camera to show.");
		shell.openPathFromCommandLine(fixtures.map);
		settle();
		trigger(shell, "shell.mode.levels");
		check(camera->isVisible() && !map->isVisible() && camera->hasMesh(), "Opening a map should bring TrenchBroom's camera back.");
	}
	// A pick in the objects list lights up in the camera at once, and
	// clearing the list darkens it again.
	if (auto* objects = child<LevelObjectList>(shell, "levelMapObjects")) {
		objects->clearSelection();
		settle();
		const int brush = rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0"));
		if (brush >= 0) {
			setObjectCurrentRow(objects, brush);
			setObjectSelected(objects, brush, true);
			settle();
		}
		const int lit = camera->highlightedTriangleCount();
		objects->clearSelection();
		settle();
		check(brush >= 0 && lit > 0 && camera->highlightedTriangleCount() == 0,
			"A brush picked in the objects list should light up in the camera, and go dark when the list is cleared.");
	}
	check(keysOf("map.clipTool") == QStringLiteral("C") && keysOf("map.previewWireframe").isEmpty() && keysOf("map.cycleView") == QStringLiteral("Space")
			&& keysOf("map.grid16") == QStringLiteral("5") && keysOf("map.gridLarger").contains(QStringLiteral("+")),
		"TrenchBroom's keys should replace the defaults: C clips, W is free, Space cycles, 5 sets a 16-unit grid.");
	snapshot(shell, "profile-trenchbroom-3d");
	// A screen reader hears the controls with each view, however the view
	// has changed since.
	camera->zoomIn();
	check(camera->accessibleDescription().contains(QStringLiteral("Look around: Right drag"))
			&& map->accessibleDescription().contains(QStringLiteral("Pan: Right drag, Middle drag")),
		"Each Levels view should tell a screen reader its TrenchBroom controls.");

	// The Controls reference lists every gesture and key, grouped by view,
	// and the filter narrows it.
	if (QAction* reference = shell.findChild<QAction*>(QStringLiteral("levelControlsReference"))) {
		reference->trigger();
		settle();
		auto* dialog = shell.findChild<QDialog*>(QStringLiteral("levelControlsDialog"));
		auto* tree = dialog ? dialog->findChild<QTreeWidget*>(QStringLiteral("levelControlsTree")) : nullptr;
		auto* filter = dialog ? dialog->findChild<QLineEdit*>(QStringLiteral("levelControlsFilter")) : nullptr;
		check(dialog && dialog->isVisible() && tree && filter && tree->topLevelItemCount() == 4, "Show Every Gesture and Key should list the layout, both views, and the keys.");
		if (tree && filter) {
			const auto rowFor = [tree](const QString& action) -> QTreeWidgetItem* {
				for (QTreeWidgetItemIterator it(tree); *it; ++it) {
					if ((*it)->parent() && (*it)->text(0) == action) {
						return *it;
					}
				}
				return nullptr;
			};
			QTreeWidgetItem* look = rowFor(QStringLiteral("Look around"));
			check(look && look->text(1) == QStringLiteral("Right drag"), "The reference should say how the TrenchBroom camera looks around.");
			filter->setText(QStringLiteral("fly"));
			settle();
			int shown = 0;
			for (QTreeWidgetItemIterator it(tree); *it; ++it) {
				shown += (*it)->parent() && !(*it)->isHidden() ? 1 : 0;
			}
			check(shown >= 1 && look && look->isHidden(), "Filtering the reference should keep only the matching rows.");
			if (const QString directory = qEnvironmentVariable("VIBESTUDIO_TEST_SNAPSHOTS"); !directory.isEmpty()) {
				filter->clear();
				settle();
				QDir().mkpath(directory);
				dialog->grab().save(QDir(directory).filePath(QStringLiteral("profile-controls-reference.png")));
			}
		}
		if (dialog) {
			dialog->hide();
		}
		ensureActive(shell, "checkEditorProfiles: reference");
	} else {
		check(false, "The Controls button should offer the full reference.");
	}

	// The camera: right drag looks, Alt+right orbits, the wheel moves, keys fly.
	camera->setFocus(Qt::OtherFocusReason);
	const QPoint middle = camera->rect().center();
	ModelVec3 before = camera->cameraPosition();
	double yaw = camera->cameraYaw();
	drag(camera, Qt::RightButton, Qt::NoModifier, middle, middle + QPoint(60, 0));
	check(std::abs(camera->cameraYaw() - yaw) > 5.0 && std::abs(camera->cameraPosition().x - before.x) < 1e-3,
		"A right drag should turn the TrenchBroom camera where it stands.");
	before = camera->cameraPosition();
	QTest::keyPress(camera, Qt::Key_W);
	wait(250);
	QTest::keyRelease(camera, Qt::Key_W);
	settle();
	const ModelVec3 flown = camera->cameraPosition();
	check(std::hypot(flown.x - before.x, flown.y - before.y, flown.z - before.z) > 8.0, "Holding W should fly the TrenchBroom camera forward.");
	before = camera->cameraPosition();
	yaw = camera->cameraYaw();
	drag(camera, Qt::RightButton, Qt::AltModifier, middle, middle + QPoint(-50, 0));
	check(std::abs(camera->cameraYaw() - yaw) > 5.0 && std::hypot(camera->cameraPosition().x - before.x, camera->cameraPosition().y - before.y) > 1.0,
		"Alt+right drag should orbit: the camera moves and turns together.");
	const double fov = camera->fieldOfView();
	QWheelEvent zoom(QPointF(middle), camera->mapToGlobal(QPointF(middle)), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
	QCoreApplication::sendEvent(camera, &zoom);
	check(camera->fieldOfView() < fov, "Shift+wheel should narrow the TrenchBroom camera's view.");

	// Space steps the one view through 3D, Top, Front, Side, and back.
	press(shell, Qt::Key_Space);
	check(map->isVisible() && !camera->isVisible() && map->projection() == MapViewportProjection::TopXY,
		"Space in the 3D view should step to the top view.");
	press(shell, Qt::Key_Space);
	press(shell, Qt::Key_Space);
	check(map->projection() == MapViewportProjection::SideZY, "Space should step on through the front and side views.");
	press(shell, Qt::Key_Space);
	check(camera->isVisible() && !map->isVisible(), "Space past the side view should come back to 3D.");
	// The profile's Space belongs to the Levels page: elsewhere it is left to
	// whatever has focus.
	trigger(shell, "shell.mode.workspace");
	if (auto* recent = child<QWidget>(shell, "recentProjects")) {
		recent->setFocus(Qt::OtherFocusReason);
	}
	press(shell, Qt::Key_Space);
	trigger(shell, "shell.mode.levels");
	check(camera->isVisible() && !map->isVisible(), "Space on another page should not cycle the Levels view.");
	camera->setFocus(Qt::OtherFocusReason);
	press(shell, Qt::Key_Space);

	// In the 2D view: a drag over empty space draws a brush, right drag pans.
	check(map->isVisible(), "The top view should be showing for the 2D checks.");
	map->zoomToFit();
	settle();
	const int brushes = brushCount();
	const QPoint corner(12, map->height() - 12);
	drag(map, Qt::LeftButton, Qt::NoModifier, corner, corner + QPoint(80, -60));
	check(brushCount() == brushes + 1 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Drew brush:")),
		"A left drag over empty space should draw a brush, as TrenchBroom does.");
	// Hiding in TrenchBroom's one view hides in the camera too, and the clip
	// tool brings the 2D view, where its line is drawn.
	trigger(shell, "map.toggle3D");
	const int shownTriangles = camera->mesh().triangleCount;
	trigger(shell, "map.hideSelection");
	check(camera->isVisible() && camera->mesh().triangleCount < shownTriangles, "Hiding the drawn brush should take it out of the 3D view.");
	trigger(shell, "map.showAll");
	check(camera->mesh().triangleCount == shownTriangles, "Show All Hidden should bring it back in the 3D view.");
	trigger(shell, "map.clipTool");
	check(map->isVisible() && !camera->isVisible() && map->clipMode(), "The clip tool should bring the 2D view forward from the 3D one.");
	trigger(shell, "map.clipTool");
	// A left drag on the selection in the camera moves it, one undo step.
	if (auto* objects = child<LevelObjectList>(shell, "levelMapObjects")) {
		objects->clearSelection();
		const int floor = rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0"));
		if (floor >= 0) {
			setObjectCurrentRow(objects, floor);
			setObjectSelected(objects, floor, true);
		}
		settle();
	}
	trigger(shell, "map.toggle3D");
	trigger(shell, "map.frameSelection");
	settle();
	check(camera->isVisible(), "The camera should be showing for the move check.");
	drag(camera, Qt::LeftButton, Qt::NoModifier, camera->rect().center(), camera->rect().center() + QPoint(90, 0));
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Moved")) && !camera->isMovingSelection(),
		"A left drag on the selection in TrenchBroom's camera should move it.");
	trigger(shell, "map.undo");
	press(shell, Qt::Key_Space);
	const LevelMapVec3 centreBefore = map->worldPositionAt(QPointF(map->rect().center()), 0.0);
	drag(map, Qt::RightButton, Qt::NoModifier, map->rect().center(), map->rect().center() + QPoint(50, 30));
	const LevelMapVec3 centreAfter = map->worldPositionAt(QPointF(map->rect().center()), 0.0);
	check(std::abs(centreAfter.x - centreBefore.x) > 1.0, "A right drag should pan TrenchBroom's 2D view.");
	auto menuShown = std::make_shared<bool>(false);
	whenMenuShows([menuShown](QMenu* menu) {
		*menuShown = true;
		menu->close();
	});
	QTest::mouseClick(map, Qt::RightButton, Qt::NoModifier, map->rect().center());
	settle();
	check(*menuShown, "A right click that does not drag should still open the map menu.");
	ensureActive(shell, "checkEditorProfiles: menu");

	// NetRadiant Custom.
	choose("netradiant-custom");
	check(camera->isVisible() && map->isVisible() && camera->isPerspective() && map->cameraMarkerVisible(),
		"NetRadiant Custom's layout should put the camera beside the 2D view, which shows where the camera is.");
	check(keysOf("map.duplicateSelection").startsWith(QStringLiteral("Space")) && keysOf("map.clipTool") == QStringLiteral("X")
			&& keysOf("map.selectNone") == QStringLiteral("C") && keysOf("map.nextProjection") == QStringLiteral("Ctrl+Tab"),
		"NetRadiant Custom's keys should be its own: Space clones, X clips, C deselects, Ctrl+Tab steps the 2D view.");
	snapshot(shell, "profile-netradiant-split");
	map->zoomToFit();
	settle();
	// A plain click selects what is under the pointer; Shift+click adds.
	trigger(shell, "map.selectNone");
	const QVector<LevelMapSelectionRef> stack = map->objectsAt(map->viewPointFor(0.0, 0.0));
	if (!stack.isEmpty()) {
		const QPoint on = map->viewPointFor(0.0, 0.0).toPoint();
		QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, on);
		settle();
		check(map->selectionSet().size() == 1 && map->selectionSet().first().objectId == stack.first().objectId,
			"A plain click should select the object nearest the viewer under the pointer.");
		if (stack.size() > 1) {
			QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, on);
			settle();
			check(map->selectionSet().size() == 1 && map->selectionSet().first().objectId == stack.at(1).objectId,
				"Clicking again in the same place should step to the next object down.");
		}
	}
	// The middle button aims the camera at a point in the top view, turning
	// only its heading.
	// Unsnapped, so the point asked for is the point aimed at.
	trigger(shell, "map.toggleSnap");
	const ModelVec3 at = camera->cameraPosition();
	const double pitchBefore = camera->cameraPitch();
	const QPoint target = map->rect().center() + QPoint(40, -30);
	const LevelMapVec3 aimed = map->worldPositionAt(QPointF(target), 0.0);
	double expectedYaw = std::atan2(aimed.y - at.y, aimed.x - at.x) * 180.0 / std::numbers::pi;
	if (expectedYaw < 0.0) {
		expectedYaw += 360.0;
	}
	QTest::mouseClick(map, Qt::MiddleButton, Qt::NoModifier, target);
	settle();
	check(std::abs(camera->cameraYaw() - expectedYaw) < 2.0 && std::abs(camera->cameraPitch() - pitchBefore) < 1e-6,
		"A middle click in the top view should turn the camera's heading toward the point.");
	trigger(shell, "map.toggleSnap");
	// A right click in the camera toggles mouse look; a right drag strafes.
	// A menu opening instead is answered, so the check fails rather than
	// waiting on it.
	auto strayMenu = std::make_shared<bool>(false);
	whenMenuShows([strayMenu](QMenu* menu) {
		*strayMenu = true;
		menu->close();
	});
	QTest::mouseClick(camera, Qt::RightButton, Qt::NoModifier, camera->rect().center());
	settle();
	check(camera->isLooking() && !*strayMenu, "A right click in NetRadiant Custom's camera should start mouse look, not open a menu.");
	QTest::mouseClick(camera, Qt::RightButton, Qt::NoModifier, camera->rect().center());
	settle();
	check(!camera->isLooking(), "Another right click should stop it.");
	// A drag with nothing selected draws a brush, in the 2D view.
	trigger(shell, "map.selectNone");
	const int brushesBefore = brushCount();
	const QPoint start(14, map->height() - 14);
	drag(map, Qt::LeftButton, Qt::NoModifier, start, start + QPoint(60, -40));
	check(brushCount() == brushesBefore + 1, "A drag with nothing selected should draw a brush, as Radiant does.");
	// Space clones the new brush.
	map->setFocus(Qt::OtherFocusReason);
	press(shell, Qt::Key_Space);
	check(brushCount() == brushesBefore + 2, "Space should clone the selection, as Radiant does.");

	// GtkRadiant 1.6.0: a plain click selects nothing, Shift+click toggles,
	// Shift+Alt+click drills; Delete and Insert zoom and Backspace deletes;
	// the camera selects only with Shift too.
	choose("gtkradiant-1-6");
	check(controlsButton->text() == QStringLiteral("GtkRadiant 1.6.0") && camera->isVisible() && map->isVisible() && map->gridSize() == 8,
		"GtkRadiant's layout should put the camera beside the 2D view, on an 8-unit grid.");
	check(keysOf("map.zoomIn") == QStringLiteral("Del") && keysOf("map.zoomOut") == QStringLiteral("Ins")
			&& keysOf("map.deleteSelection") == QStringLiteral("Backspace") && keysOf("map.duplicateSelection") == QStringLiteral("Space"),
		"GtkRadiant's keys should be its own: Delete and Insert zoom, Backspace deletes, Space clones.");
	snapshot(shell, "profile-gtkradiant-split");
	map->zoomToFit();
	settle();
	trigger(shell, "map.selectNone");
	const QVector<LevelMapSelectionRef> gtkStack = map->objectsAt(map->viewPointFor(0.0, 0.0));
	check(!gtkStack.isEmpty(), "Something should lie under the map's origin.");
	if (!gtkStack.isEmpty()) {
		const QPoint on = map->viewPointFor(0.0, 0.0).toPoint();
		QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, on);
		settle();
		check(map->selectionSet().isEmpty(), "A plain click in GtkRadiant's 2D view should select nothing.");
		QTest::mouseClick(map, Qt::LeftButton, Qt::ShiftModifier, on);
		settle();
		check(map->selectionSet().size() == 1, "Shift+click should select the object under the pointer.");
		QTest::mouseClick(map, Qt::LeftButton, Qt::ShiftModifier, on);
		settle();
		check(map->selectionSet().isEmpty(), "Shift+click again should deselect it.");
		QTest::mouseClick(map, Qt::LeftButton, Qt::ShiftModifier | Qt::AltModifier, on);
		settle();
		check(map->selectionSet().size() == 1 && map->selectionSet().first().objectId == gtkStack.first().objectId,
			"Shift+Alt+click should pick the object nearest the viewer.");
		if (gtkStack.size() > 1) {
			QTest::mouseClick(map, Qt::LeftButton, Qt::ShiftModifier | Qt::AltModifier, on);
			settle();
			check(map->selectionSet().size() == 1 && map->selectionSet().first().objectId == gtkStack.at(1).objectId,
				"Shift+Alt+click again should drill to the next object down.");
		}
	}
	map->setFocus(Qt::OtherFocusReason);
	const double zoomBefore = map->zoom();
	press(shell, Qt::Key_Delete);
	const double zoomedIn = map->zoom();
	press(shell, Qt::Key_Insert);
	check(zoomedIn > zoomBefore && map->zoom() < zoomedIn, "Delete should zoom GtkRadiant's 2D view in, and Insert out.");
	if (auto* objects = child<LevelObjectList>(shell, "levelMapObjects")) {
		objects->clearSelection();
		const int brushRow = rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0"));
		if (brushRow >= 0) {
			setObjectCurrentRow(objects, brushRow);
			setObjectSelected(objects, brushRow, true);
		}
		settle();
		// Its camera selects only with Shift as well.
		trigger(shell, "map.frameSelection");
		trigger(shell, "map.selectNone");
		settle();
		QTest::mouseClick(camera, Qt::LeftButton, Qt::NoModifier, camera->rect().center());
		settle();
		check(map->selectionSet().isEmpty(), "A plain click in GtkRadiant's camera should select nothing.");
		QTest::mouseClick(camera, Qt::LeftButton, Qt::ShiftModifier, camera->rect().center());
		settle();
		check(map->selectionSet().size() == 1, "Shift+click in GtkRadiant's camera should select what is under the pointer.");
		// Backspace deletes, as Delete zooms.
		const int brushesHere = brushCount();
		const bool brushPicked = !map->selectionSet().isEmpty() && map->selectionSet().first().kind == LevelMapSelectionKind::QuakeBrush;
		map->setFocus(Qt::OtherFocusReason);
		press(shell, Qt::Key_Backspace);
		check(!brushPicked || brushCount() == brushesHere - 1, "Backspace should delete the selection in GtkRadiant.");
		trigger(shell, "map.undo");
	}

	// Back to the VibeStudio controls, as they were.
	choose("vibestudio-default");
	check(map->isVisible() && !camera->isVisible() && !camera->isPerspective() && map->gridSize() == 64 && keysOf("map.clipTool") == QStringLiteral("X")
			&& keysOf("map.previewWireframe") == QStringLiteral("W") && keysOf("map.cycleView").isEmpty(),
		"The VibeStudio profile should bring back its 2D-first view, orbit preview, grid, and keys.");
	// A middle-button pan leaves the ordinary pointer behind, so the next
	// left click selects rather than pans.
	drag(map, Qt::MiddleButton, Qt::NoModifier, map->rect().center(), map->rect().center() + QPoint(30, 20));
	check(map->cursor().shape() != Qt::OpenHandCursor, "A middle-button pan should not leave the view in hand mode.");
	trigger(shell, "map.undo");
	trigger(shell, "map.undo");
	trigger(shell, "map.undo");
	if (!previousMap.isEmpty() && !sameFile(previousMap, fixtures.map)) {
		if (nameField) {
			nameField->setCurrentText(previousName);
		}
		shell.openPathFromCommandLine(previousMap);
		settle();
	}
}

// The navigation rail rests as icons and opens over the page, labels and all,
// while the pointer rests on it or the keyboard is in it; the page never
// moves. A page picked with the pointer folds it; the pin keeps it open; a
// narrow window folds even a pinned rail; icons-only never opens.
void checkNavigationRail(ApplicationShell& shell)
{
	ensureActive(shell, "checkNavigationRail");
	auto* rail = shell.findChild<ModeRail*>(QStringLiteral("modeRail"));
	auto* work = child<QWidget>(shell, "workArea");
	auto* pin = child<QToolButton>(shell, "railToggle");
	auto* combo = child<QComboBox>(shell, "railBehaviourCombo");
	check(rail && work && pin && combo, "The shell should have its rail, pin, page area, and rail setting.");
	if (!rail || !work || !pin || !combo) {
		return;
	}
	QToolButton* textures = nullptr;
	QToolButton* workspace = nullptr;
	for (QToolButton* button : rail->findChildren<QToolButton*>(QStringLiteral("modeButton"))) {
		if (button->accessibleName() == QStringLiteral("Textures")) {
			textures = button;
		} else if (button->accessibleName() == QStringLiteral("Workspace")) {
			workspace = button;
		}
	}
	check(textures && workspace, "The rail should list Workspace and Textures.");
	if (!textures || !workspace) {
		return;
	}
	const auto hover = [rail](bool inside) {
		if (inside) {
			QEnterEvent enter(QPointF(12, 12), QPointF(12, 12), rail->mapToGlobal(QPointF(12, 12)));
			QCoreApplication::sendEvent(rail, &enter);
		} else {
			QEvent leave(QEvent::Leave);
			QCoreApplication::sendEvent(rail, &leave);
		}
	};
	const auto shade = [&shell]() {
		QWidget* strip = child<QWidget>(shell, "railShade");
		return strip && strip->isVisible();
	};
	// Open and folded, slide finished. What must happen is waited for; what
	// must not is given the open delay and the slide, and then checked.
	const auto opened = [rail]() {
		return rail->isOpen() && rail->showsLabels() && rail->width() == rail->expandedWidth();
	};
	const auto folded = [rail]() {
		return !rail->isOpen() && !rail->showsLabels() && rail->width() == rail->compactWidth();
	};
	const int longerThanOpening = ModeRail::kOpenDelayMsecs + 400;

	trigger(shell, "shell.mode.workspace");
	const int pageLeft = work->geometry().left();
	check(rail->behaviour() == RailBehaviour::Automatic && folded() && pageLeft == rail->compactWidth() && textures->text().isEmpty(),
		"By default the rail should rest as icons, and the page start where the icons end.");
	check(textures->toolTip().startsWith(QStringLiteral("Textures")) && textures->accessibleName() == QStringLiteral("Textures"),
		"A folded icon should still be named by its tooltip and to a screen reader.");
	snapshot(shell, "rail-resting");

	// Resting the pointer opens it over the page; the page stays put.
	hover(true);
	check(waitFor(opened, 3000) && textures->text() == QStringLiteral("Textures") && work->geometry().left() == pageLeft && shade(),
		"Resting the pointer on the rail should open its labels over the page without moving the page.");
	snapshot(shell, "rail-open");
	hover(false);
	check(waitFor(folded, 3000) && !shade(), "Moving the pointer away should fold the rail back to icons.");

	// A pointer passing over quickly does not open it.
	hover(true);
	wait(ModeRail::kOpenDelayMsecs / 3);
	check(!rail->isOpen(), "The rail should wait for the pointer to rest before opening.");
	hover(false);
	wait(longerThanOpening);
	check(folded(), "A pointer only passing over the rail should not open it.");

	// Picking a page with the pointer folds the rail, and it stays folded
	// under the pointer until the pointer has left.
	hover(true);
	check(waitFor(opened, 3000), "The rail should open under a resting pointer.");
	QTest::mouseClick(textures, Qt::LeftButton);
	check(waitFor(folded, 3000) && currentPage(shell) == static_cast<int>(StudioMode::Textures),
		"Clicking a page in the open rail should show the page and fold the rail.");
	// Qt enters the rail again when a dialog closes over it.
	hover(true);
	wait(longerThanOpening);
	check(folded(), "The rail should stay folded under the pointer that just picked a page.");
	hover(false);
	// A click before the rail has opened picks the page, and the rail does
	// not open after it.
	hover(true);
	QTest::mouseClick(workspace, Qt::LeftButton);
	wait(longerThanOpening);
	check(folded() && currentPage(shell) == static_cast<int>(StudioMode::Workspace),
		"A click before the rail opens should show the page and leave the rail folded.");
	hover(false);
	QTest::mouseClick(textures, Qt::LeftButton);
	hover(true);
	check(waitFor(opened, 3000), "Once the pointer has left and come back, the rail should open again.");
	hover(false);
	check(waitFor(folded, 3000), "The rail should fold once the pointer leaves.");

	// The keyboard opens it at once, walks it with the arrow keys, and
	// Escape hands focus back to the page.
	check(textures->isChecked(), "The Textures entry should be the current one.");
	textures->setFocus(Qt::TabFocusReason);
	check(waitFor(opened, 3000), "Keyboard focus in the rail should open its labels.");
	press(shell, Qt::Key_Down);
	wait(ModeRail::kCloseDelayMsecs + 300);
	QWidget* focus = QApplication::focusWidget();
	check(currentPage(shell) != static_cast<int>(StudioMode::Textures) && opened() && focus && rail->isAncestorOf(focus),
		"Down should move to the next page and keep the keyboard, and the labels, in the rail.");
	press(shell, Qt::Key_Escape);
	check(waitFor(folded, 3000), "Escape in the rail should fold it.");
	focus = QApplication::focusWidget();
	check(focus && !rail->isAncestorOf(focus) && focusIsOn(shell, currentPage(shell)), "Escape in the rail should hand the keyboard to the page.");

	// The pin keeps the labels open beside the page, and is remembered.
	pin->click();
	check(waitFor([rail, work]() { return rail->showsLabels() && work->geometry().left() == rail->expandedWidth(); }, 3000)
			&& rail->behaviour() == RailBehaviour::Expanded && pin->isChecked() && !shade()
			&& StudioSettings().shellModeRailBehaviour() == QStringLiteral("expanded")
			&& combo->currentData().toString() == QStringLiteral("expanded"),
		"The pin should keep the labels open beside the page, and remember it.");
	snapshot(shell, "rail-pinned");
	// A narrow window folds even a pinned rail, which then opens on hover.
	const QSize before = shell.size();
	shell.resize(rail->expandedWidth() * 5, before.height());
	settle();
	check(rail->effectiveBehaviour() == RailBehaviour::Automatic && work->geometry().left() == rail->compactWidth(),
		"In a narrow window a pinned rail should fold to icons.");
	shell.resize(before);
	settle();
	check(rail->effectiveBehaviour() == RailBehaviour::Expanded && work->geometry().left() == rail->expandedWidth(),
		"Widening the window again should bring the pinned labels back.");
	hover(true);
	pin->click();
	check(waitFor([rail, work]() { return !rail->showsLabels() && work->geometry().left() == rail->compactWidth(); }, 3000)
			&& rail->behaviour() == RailBehaviour::Automatic && !pin->isChecked(),
		"Unpinning should fold the rail back to icons.");
	wait(longerThanOpening);
	check(folded(), "The rail should not open again under the pointer that just unpinned it.");
	hover(false);

	// Icons only never opens; the setting and Reset Layout change it.
	combo->setCurrentIndex(combo->findData(QStringLiteral("compact")));
	settle();
	hover(true);
	wait(longerThanOpening);
	check(rail->behaviour() == RailBehaviour::Compact && folded(), "With icons only chosen, resting the pointer should not open the rail.");
	hover(false);
	trigger(shell, "shell.resetLayout");
	check(rail->behaviour() == RailBehaviour::Automatic && combo->currentData().toString() == QStringLiteral("automatic")
			&& StudioSettings().shellModeRailBehaviour() == QStringLiteral("automatic"),
		"Reset Layout should bring back the automatic rail.");

	// With motion reduced, the rail opens and folds in one step.
	rail->setReducedMotion(true);
	rail->setOpen(true);
	check(opened(), "With motion reduced the rail should open without sliding.");
	rail->setOpen(false);
	check(folded(), "With motion reduced the rail should fold without sliding.");
	rail->setReducedMotion(false);
}

// The Files tree marks the file in the editor, whichever way the tab changes;
// a file the tree does not list, and no file at all, leave no row marked.
void checkCodeTreeFollowsTab(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCodeTreeFollowsTab");
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	closeAllCodeTabs(shell);
	const QString first = QDir(fixtures.project).filePath(QStringLiteral("follow_first.cfg"));
	const QString second = QDir(fixtures.project).filePath(QStringLiteral("follow_second.cfg"));
	writeFile(first, QByteArrayLiteral("// first\n"));
	writeFile(second, QByteArrayLiteral("// second\n"));
	shell.openPathFromCommandLine(first);
	settle();
	shell.openPathFromCommandLine(second);
	settle();
	auto* tree = child<QTreeWidget>(shell, "codeTree");
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	check(tree && tabs, "The Code page should have its Files tree and tabs.");
	if (!tree || !tabs) {
		return;
	}
	const auto marked = [tree]() {
		const QTreeWidgetItem* item = tree->currentItem();
		return item && item->isSelected() ? item->data(0, Qt::UserRole).toString() : QString();
	};
	check(sameFile(marked(), second), "The Files tree should mark the file just opened.");
	tabs->setCurrentIndex(codeTabFor(tabs, first));
	settle();
	check(sameFile(marked(), first), "Clicking another tab should move the tree's mark to its file.");
	if (auto* editor = child<QPlainTextEdit>(shell, "codeEditor")) {
		editor->setFocus();
		press(shell, Qt::Key_PageDown, Qt::ControlModifier);
		check(sameFile(marked(), second), "Ctrl+Page Down should move the tree's mark with the tab.");
	}
	// A file from outside the project has no row, so none stays marked.
	shell.openPathFromCommandLine(fixtures.script);
	settle();
	check(marked().isEmpty(), "A file the tree does not list should leave no row marked.");
	closeAllCodeTabs(shell);
	settle();
	check(marked().isEmpty() && !tree->currentItem(), "With no file open, no row should be marked.");
	QFile::remove(first);
	QFile::remove(second);
}

// Review fixes on the Code page: with no file open there is nothing to type
// into; a file opened is highlighted at once; closing a changed tab in the
// background returns to the tab the user was on; and a project search's
// matches stay listed while one is opened.
void checkCodeTabFixes(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCodeTabFixes");
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	closeAllCodeTabs(shell);
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	check(editor && tabs && tabs->count() == 0, "Every code tab should close.");
	if (!editor || !tabs) {
		return;
	}
	trigger(shell, "shell.mode.code");
	editor->setFocus();
	QTest::keyClicks(editor, QStringLiteral("stray"));
	settle();
	check(editor->isReadOnly() && editor->toPlainText().isEmpty(), "With no file open the editor should take no typing.");

	shell.openPathFromCommandLine(fixtures.quakeC);
	settle();
	const QTextBlock first = editor->document()->firstBlock();
	check(first.isValid() && first.layout() && !first.layout()->formats().isEmpty(), "A file should be syntax-highlighted as soon as it opens.");

	const QString third = QDir(fixtures.project).filePath(QStringLiteral("review_third.cfg"));
	writeFile(third, QByteArrayLiteral("// third\n"));
	shell.openPathFromCommandLine(fixtures.script);
	settle();
	shell.openPathFromCommandLine(third);
	settle();
	tabs->setCurrentIndex(codeTabFor(tabs, fixtures.script));
	settle();
	editor->moveCursor(QTextCursor::End);
	editor->insertPlainText(QStringLiteral("// changed\n"));
	tabs->setCurrentIndex(codeTabFor(tabs, fixtures.quakeC));
	settle();
	whenDialogShows([](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal); box && box->button(QMessageBox::Discard)) {
			box->button(QMessageBox::Discard)->click();
		}
	});
	if (auto* close = qobject_cast<QAbstractButton*>(tabs->tabButton(codeTabFor(tabs, fixtures.script), QTabBar::RightSide))) {
		close->click();
	}
	settle();
	check(codeTabFor(tabs, fixtures.script) < 0 && tabs->currentIndex() == codeTabFor(tabs, fixtures.quakeC),
		"Closing a changed tab in the background should come back to the tab the user was on.");

	writeFile(QDir(fixtures.project).filePath(QStringLiteral("find_one.cfg")), QByteArrayLiteral("set needle_review 1\n"));
	writeFile(QDir(fixtures.project).filePath(QStringLiteral("find_two.cfg")), QByteArrayLiteral("set needle_review 2\n"));
	for (QLineEdit* field : shell.findChildren<QLineEdit*>()) {
		if (field->accessibleName() == QStringLiteral("Replace text")) {
			field->clear();
		}
	}
	auto* find = child<QLineEdit>(shell, "codeProjectFind");
	auto* results = child<QListWidget>(shell, "codeSearchResults");
	check(find && results, "The Code page should have its project find field and Search Results list.");
	if (!find || !results) {
		return;
	}
	find->setText(QStringLiteral("needle_review"));
	QTest::keyClick(find, Qt::Key_Return);
	check(waitFor([results]() { return results->count() == 2; }), "The background project search should finish.");
	const int matches = results->count();
	check(matches == 2 && results->isVisible(), "Find in the project should list both matches under Search Results.");
	if (matches > 0) {
		emit results->itemActivated(results->item(0));
		settle();
	}
	check(results->count() == matches && tabs->count() >= 3, "Opening a match should open its file and keep the other matches listed.");
}

// Files changed outside the studio (review fixes): a deleted file's tab keeps
// the text as unsaved; after one change is declined and the file saved, the
// next change outside is noticed; a read-only package copy stays read-only
// when it reloads; and a map open in Levels and a code tab keeps its Levels
// watch when the tab closes.
void checkExternalChanges(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkExternalChanges");
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	if (!editor || !tabs) {
		check(false, "The Code page should have its editor and tabs.");
		return;
	}

	const QString doomed = QDir(fixtures.project).filePath(QStringLiteral("doomed.cfg"));
	writeFile(doomed, QByteArrayLiteral("// soon gone\n"));
	shell.openPathFromCommandLine(doomed);
	settle();
	QFile::remove(doomed);
	check(waitFor([tabs, &doomed]() {
		const int tab = codeTabFor(tabs, doomed);
		return tab >= 0 && tabs->tabText(tab).contains(QChar(0x2022));
	}),
		"A file deleted outside the studio should leave its tab holding the text as unsaved changes.");
	tabs->setCurrentIndex(codeTabFor(tabs, doomed));
	settle();
	whenDialogShows([](QWidget* dialog) {
		if (auto* recreate = dialog->findChild<QPushButton*>(QStringLiteral("recreateCodeFile"))) { recreate->click(); }
	});
	trigger(shell, "code.save");
	check(QFileInfo::exists(doomed), "A reviewed save should recreate the deleted file.");

	const QString edited = QDir(fixtures.project).filePath(QStringLiteral("edited.cfg"));
	writeFile(edited, QByteArrayLiteral("// first\n"));
	shell.openPathFromCommandLine(edited);
	settle();
	editor->moveCursor(QTextCursor::End);
	editor->insertPlainText(QStringLiteral("// typed\n"));
	auto asked = std::make_shared<QString>();
	answerNextQuestionNo(asked);
	writeFile(edited, QByteArrayLiteral("// changed outside\n"));
	check(waitFor([asked]() { return !asked->isEmpty(); }), "A change outside to a tab with unsaved edits should ask first.");
	settle();
	whenDialogShows([](QWidget* dialog) {
		if (auto* overwrite = dialog->findChild<QPushButton*>(QStringLiteral("overwriteCodeFile"))) { overwrite->click(); }
	});
	trigger(shell, "code.save");
	writeFile(edited, QByteArrayLiteral("// changed again outside\n"));
	check(waitFor([editor]() { return editor->toPlainText() == QStringLiteral("// changed again outside\n"); }),
		"After a save, the next change outside should be noticed and reloaded.");

	// The package is still open from the drop check: open one of its text
	// entries, which comes up as a read-only copy.
	auto* packageTree = child<PackageFolderView>(shell, "packageTree");
	auto entries = tests::PackageRows(child<PackageEntryView>(shell, "packageEntries"));
	tests::waitForPackageEntries(entries);
	if (packageTree) { packageTree->selectFolder(QStringLiteral("scripts")); }
	settle();
	const int row = entries ? rowWithData(entries, Qt::UserRole, QStringLiteral("scripts/autoexec.cfg")) : -1;
	check(row >= 0, "The package should list scripts/autoexec.cfg.");
	if (row >= 0) {
		emit entries->itemActivated(entries->item(row));
		settle();
		const QString copy = QDir::fromNativeSeparators(tabs->tabToolTip(tabs->currentIndex()).section(QLatin1Char('\n'), 0, 0));
		check(editor->isReadOnly() && QFileInfo::exists(copy), "A package entry should open as a read-only copy.");
		writeFile(copy, QByteArrayLiteral("// the copy changed\n"));
		check(waitFor([editor]() { return editor->toPlainText() == QStringLiteral("// the copy changed\n"); }), "The copy should reload after a change outside.");
		check(editor->isReadOnly() && tabs->tabToolTip(tabs->currentIndex()).contains(QStringLiteral("Read-only")),
			"A reloaded package copy should stay read-only.");
	}

	const QString sharedMap = QDir(fixtures.project).filePath(QStringLiteral("maps/shared.map"));
	QDir().mkpath(QFileInfo(sharedMap).absolutePath());
	QFile::remove(sharedMap);
	QFile::copy(fixtures.map, sharedMap);
	shell.openPathFromCommandLine(sharedMap);
	settle();
	trigger(shell, "shell.mode.code");
	for (QToolButton* button : shell.findChildren<QToolButton*>()) {
		if (button->accessibleName() == QStringLiteral("Refresh source tree")) {
			button->click();
		}
	}
	settle();
	auto* codeTree = child<QTreeWidget>(shell, "codeTree");
	QTreeWidgetItem* mapItem = nullptr;
	for (QTreeWidgetItemIterator it(codeTree); codeTree && *it; ++it) {
		if (QFileInfo((*it)->data(0, Qt::UserRole).toString()) == QFileInfo(sharedMap)) {
			mapItem = *it;
		}
	}
	check(mapItem != nullptr, "Project files should list the map.");
	if (mapItem) {
		codeTree->setCurrentItem(mapItem);
		emit codeTree->itemActivated(mapItem, 0);
		settle();
		check(codeTabFor(tabs, sharedMap) >= 0, "The map should open as text in a code tab too.");
		tabs->setCurrentIndex(codeTabFor(tabs, sharedMap));
		settle();
		trigger(shell, "code.closeFile");
		auto question = std::make_shared<QString>();
		answerNextQuestionNo(question);
		QFile file(sharedMap);
		if (file.open(QIODevice::Append)) {
			file.write("// touched outside\n");
			file.close();
		}
		check(waitFor([question]() { return !question->isEmpty(); }) && *question == QStringLiteral("Map Changed On Disk"),
			"With its code tab closed, a change outside should still ask about the map in Levels.");
	}
	settle();
	trigger(shell, "project.close");
}

// A reflowing grid uses as many columns as fit, up to its maximum, and asks
// for no more than one column's width.
void checkReflowGrid()
{
	auto* host = new QWidget;
	auto* hostLayout = new QVBoxLayout(host);
	hostLayout->setContentsMargins(0, 0, 0, 0);
	auto* grid = new ReflowGrid(4);
	for (int index = 0; index < 8; ++index) {
		auto* tile = new QPushButton(QStringLiteral("Surface %1").arg(index));
		tile->setMinimumWidth(150);
		grid->addItem(tile);
	}
	hostLayout->addWidget(grid);
	host->resize(1000, 200);
	host->show();
	settle();
	check(grid->columns() == 4, "With room for four, the grid should lay out four columns.");
	host->resize(500, 400);
	settle();
	check(grid->columns() == 3 || grid->columns() == 2, "Narrower, the grid should wrap to fewer columns.");
	host->resize(160, 800);
	settle();
	check(grid->columns() == 1 && grid->minimumSizeHint().width() < 200, "At one item's width the grid should be a single column, and ask for no more.");
	delete host;

	// Cards whose own hints say little are laid out by a given column width.
	auto* cardHost = new QWidget;
	auto* cardLayout = new QVBoxLayout(cardHost);
	cardLayout->setContentsMargins(0, 0, 0, 0);
	auto* cards = new ReflowGrid(2);
	cards->setColumnWidth(300);
	cards->setColumnStretches({3, 2});
	cards->addItem(new QPlainTextEdit);
	cards->addItem(new QPlainTextEdit);
	cardLayout->addWidget(cards);
	cardHost->resize(700, 300);
	cardHost->show();
	settle();
	const bool two = cards->columns() == 2;
	cardHost->resize(500, 300);
	settle();
	check(two && cards->columns() == 1, "A card grid should keep two columns while two given widths fit, and stack the cards when not.");
	delete cardHost;
}

// Keys that cannot be given, and clashes named in full: a plain key (typed
// in fields, used by the views) is not offered; a key a surface inside the
// command's own surface holds is a clash (the map view is inside Levels);
// every command a key would be taken from is named; and a command's keys are
// listed with "; " between them, so a chord never reads as two keys.
void checkShortcutRules(ApplicationShell& shell)
{
	ensureActive(shell, "checkShortcutRules");
	const auto capture = [&shell](const QString& row, const std::function<void(QKeySequenceEdit*)>& press) {
		auto clash = std::make_shared<QString>();
		auto assignable = std::make_shared<bool>(false);
		whenNamedDialogShows(QStringLiteral("keyboardShortcuts"), [=](QWidget* dialog) {
			auto* table = dialog->findChild<QTreeWidget*>(QStringLiteral("shortcutTable"));
			for (int index = 0; table && index < table->topLevelItemCount(); ++index) {
				if (table->topLevelItem(index)->text(0) == row) {
					table->setCurrentItem(table->topLevelItem(index));
				}
			}
			whenNamedDialogShows(QStringLiteral("shortcutCaptureDialog"), [=](QWidget* dialogCapture) {
				if (auto* edit = dialogCapture->findChild<QKeySequenceEdit*>(QStringLiteral("shortcutCapture"))) {
					edit->setFocus();
					press(edit);
				}
				if (auto* label = dialogCapture->findChild<QLabel*>(QStringLiteral("shortcutClash"))) {
					*clash = label->text();
				}
				if (auto* assign = dialogCapture->findChild<QPushButton*>(QStringLiteral("shortcutAssign"))) {
					*assignable = assign->isEnabled();
				}
				static_cast<QDialog*>(dialogCapture)->reject();
			});
			if (auto* change = dialog->findChild<QPushButton*>(QStringLiteral("shortcutChange")); change && change->isEnabled()) {
				change->click();
			}
			static_cast<QDialog*>(dialog)->reject();
		});
		trigger(shell, "help.keyboardShortcuts");
		return std::make_pair(*clash, *assignable);
	};
	const auto plain = capture(QStringLiteral("Reset Editor Zoom"), [](QKeySequenceEdit* edit) {
		QTest::keyClick(edit, Qt::Key_F);
	});
	check(!plain.second && plain.first.contains(QStringLiteral("without Ctrl or Alt")),
		"A plain key should not be offered: it is typed in fields and used by the views.");
	const auto both = capture(QStringLiteral("Reset Editor Zoom"), [](QKeySequenceEdit* edit) {
		QTest::keyClick(edit, Qt::Key_Z, Qt::ControlModifier);
	});
	check(both.second && both.first.contains(QStringLiteral("Undo Staged Change")) && both.first.contains(QStringLiteral("Undo Map Edit")),
		"Ctrl+Z for a window-wide command should name both page commands it would be taken from.");
	const auto nested = capture(QStringLiteral("Undo Map Edit"), [](QKeySequenceEdit* edit) {
		QTest::keyClick(edit, Qt::Key_PageUp, Qt::ControlModifier);
	});
	check(nested.first.contains(QStringLiteral("Raise Ceiling")),
		"A key the map view holds should clash with a Levels command, the view being inside the page.");

	auto zoomKeys = std::make_shared<QString>();
	whenNamedDialogShows(QStringLiteral("keyboardShortcuts"), [zoomKeys](QWidget* dialog) {
		auto* table = dialog->findChild<QTreeWidget*>(QStringLiteral("shortcutTable"));
		for (int index = 0; table && index < table->topLevelItemCount(); ++index) {
			if (table->topLevelItem(index)->text(0) == QStringLiteral("Zoom In Editor")) {
				*zoomKeys = table->topLevelItem(index)->text(1);
			}
		}
		static_cast<QDialog*>(dialog)->reject();
	});
	trigger(shell, "help.keyboardShortcuts");
	check(zoomKeys->contains(QStringLiteral("; ")) && action(shell, "code.zoomIn") && action(shell, "code.zoomIn")->toolTip().contains(QStringLiteral("; ")),
		"A command's two keys should be listed, and named in its tooltip, with \"; \" between them.");
}

// Turning crash reports off or on says what happened, rather than only
// "Preferences saved".
void checkCrashReportPreference(ApplicationShell& shell)
{
	ensureActive(shell, "checkCrashReportPreference");
	auto* keep = child<QCheckBox>(shell, "crashReports");
	check(keep && keep->isChecked(), "Settings should offer the crash report switch, on.");
	if (!keep) {
		return;
	}
	keep->setChecked(false);
	settle();
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Crash reports are off")), "Turning crash reports off should say so.");
	keep->setChecked(true);
	settle();
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Crash reports are on")), "Turning crash reports on should say so.");
}

// A second studio started while another runs does not reopen the session the
// other owns, nor record over it; once the other has gone it takes over.
void checkSecondaryInstance(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkSecondaryInstance");
	closeAllCodeTabs(shell);
	QProcess other;
	other.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--linger")});
	check(other.waitForStarted(5000), "A second process should start to own the session.");
	{
		StudioSettings settings;
		StudioSession owned;
		owned.codeFiles = {fixtures.script};
		owned.currentCodeFile = fixtures.script;
		owned.ownerProcessId = other.processId();
		settings.setLastSession(owned);
		settings.sync();
	}
	shell.beginSession(true);
	settle();
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	check(codeTabFor(tabs, fixtures.script) < 0, "A studio started while another runs should not reopen the session the other owns.");
	QTest::qWait(1300);
	check(StudioSettings().lastSession().ownerProcessId == other.processId(), "Nor should it record over that session.");
	other.kill();
	other.waitForFinished(5000);
	check(waitFor([]() { return StudioSettings().lastSession().ownerProcessId == QCoreApplication::applicationPid(); }),
		"Once the other studio has gone, this one should take the session over.");
}

// Drops onto a package that would not save: two files landing on one name
// ask first, like any clash; onto a Doom WAD, a file whose name is longer
// than a lump name is left out and said so.



// With several entities selected, the inspector lists every key any of them
// sets, marks a value they do not share, and an edit or a Delete there acts
// on all of them as one undo step, keeping the selection.
void checkMultiEntityKeys(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkMultiEntityKeys");
	whenDialogShows([](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			for (QAbstractButton* button : box->buttons()) {
				if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
					button->click();
					return;
				}
			}
		}
	});
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	ensureActive(shell, "checkMultiEntityKeys: opened");
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* inspector = child<QTreeWidget>(shell, "entityInspector");
	auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	check(objects && inspector && view, "Levels should have its objects list, inspector, and view.");
	if (!objects || !inspector || !view) {
		return;
	}
	const auto row = [inspector](const QString& key) -> QTreeWidgetItem* {
		for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
			if ((*it)->data(0, Qt::UserRole).toString() == key && (*it)->parent()
				&& (*it)->parent()->text(0).startsWith(QStringLiteral("Keys"))) {
				return *it;
			}
		}
		return nullptr;
	};
	const auto differs = [](const QTreeWidgetItem* item) {
		return item && item->data(1, Qt::AccessibleTextRole).toString().contains(QStringLiteral("differs"));
	};
	// The player start (origin 0 0 24, angle 90) and the undefined item
	// (origin 32 32 24, no angle).
	trigger(shell, "shell.mode.levels");
	for (int index = 0; index < objects->model()->rowCount(); ++index) {
		const QString selector = objectIndex(objects, index).data(Qt::UserRole).toString();
		setObjectSelected(objects, index, selector == QStringLiteral("entity:1") || selector == QStringLiteral("entity:3"));
	}
	settle();
	QTreeWidgetItem* selectedRow = nullptr;
	for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
		if ((*it)->text(0) == QStringLiteral("Selected")) {
			selectedRow = *it;
		}
	}
	check(selectedRow && selectedRow->text(1).startsWith(QStringLiteral("2 ")) && inspector->accessibleDescription().contains(QStringLiteral("set on all")),
		"Two selected entities should be shown as two, with edits said to reach both.");
	check(row(QStringLiteral("angle")) && differs(row(QStringLiteral("angle"))) && differs(row(QStringLiteral("origin"))),
		"A key only one of them sets, and one they set differently, should be listed and marked as differing.");

	// An edit sets it on both, as one step, and the selection stays.
	if (QTreeWidgetItem* angle = row(QStringLiteral("angle"))) {
		inspector->setCurrentItem(angle, 1);
		angle->setText(1, QStringLiteral("180"));
		settle();
	}
	check(row(QStringLiteral("angle")) && row(QStringLiteral("angle"))->text(1) == QStringLiteral("180") && !differs(row(QStringLiteral("angle")))
			&& view->selectionSet().size() == 2 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("angle set to 180 on 2 ")),
		"Editing a key should set it on both entities, keep both selected, and say so.");
	trigger(shell, "map.undo");
	settle();
	check(differs(row(QStringLiteral("angle"))), "One undo should take the edit off both entities.");

	// Delete takes it off every one that sets it.
	if (QTreeWidgetItem* origin = row(QStringLiteral("origin"))) {
		inspector->setFocus();
		inspector->setCurrentItem(origin, 0);
		QTest::keyClick(inspector, Qt::Key_Delete);
		settle();
	}
	check(!row(QStringLiteral("origin")) && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Removed origin from 2 ")),
		"Delete on a key should remove it from both entities.");
	trigger(shell, "map.undo");
	settle();
	check(row(QStringLiteral("origin")) && differs(row(QStringLiteral("origin"))), "Undo should put the key back on both.");

	// Brushes are still shown one at a time, and the inspector says so.
	// The brush current, so the last one selected, and the entity added.
	setObjectCurrentRow(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("brush:0")));
	settle();
	setObjectSelected(objects, rowWithData(objects, Qt::UserRole, QStringLiteral("entity:1")), true);
	settle();
	check(inspector->topLevelItemCount() > 0 && inspector->topLevelItem(0)->text(0).startsWith(QStringLiteral("The last of 2 selected objects"))
			&& inspector->accessibleDescription().startsWith(QStringLiteral("The last of 2")),
		"A brush shown out of several selected should say that an edit there changes it alone.");

	// One entity again: its own inspector, described as such.
	for (int index = 0; index < objects->model()->rowCount(); ++index) {
		setObjectSelected(objects, index, objectIndex(objects, index).data(Qt::UserRole).toString() == QStringLiteral("entity:1"));
	}
	settle();
	check(row(QStringLiteral("angle")) && row(QStringLiteral("angle"))->text(1) == QStringLiteral("90") && !differs(row(QStringLiteral("angle")))
			&& !inspector->accessibleDescription().contains(QStringLiteral("set on all")),
		"With one entity selected, the inspector should show it alone again.");
}

// Go Back and Go Forward step through the pages left and the jumps made,
// with Alt+Left and Alt+Right or the mouse's side buttons.
void checkPlaceHistory(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkPlaceHistory");
	// Changes earlier checks staged are let go first, so opening the package
	// asks nothing.
	unstageAll(shell);
	// Edits left on whatever map is open are let go.
	whenDialogShows([](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			for (QAbstractButton* button : box->buttons()) {
				if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
					button->click();
					return;
				}
			}
		}
	});
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	shell.openPathFromCommandLine(fixtures.package);
	settle();
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	const QString path = QDir(fixtures.project).filePath(QStringLiteral("progs/place_sample.qc"));
	// A function at the top and a call to it on line 35, the rest comments.
	QByteArray text = "void() place_target = {\n};\n";
	for (int line = 3; line <= 40; ++line) {
		text += line == 35 ? QByteArray("place_target();\n") : "// line " + QByteArray::number(line) + "\n";
	}
	writeFile(path, text);
	shell.openPathFromCommandLine(path);
	settle();
	// A prompt the opens raised may have left no window active, and keys need one.
	ensureActive(shell, "checkPlaceHistory: opened");
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	check(editor && objects && view, "The Code and Levels pages should be there.");
	if (!editor || !objects || !view) {
		return;
	}
	const auto back = [&shell]() {
		press(shell, Qt::Key_Left, Qt::AltModifier);
	};
	const auto forward = [&shell]() {
		press(shell, Qt::Key_Right, Qt::AltModifier);
	};
	const auto selected = [view]() {
		QStringList ids;
		for (const LevelMapSelectionRef& ref : view->selectionSet()) {
			ids << levelMapSelectionRefId(ref);
		}
		return ids;
	};
	const auto selectOnly = [objects](const QString& selector) {
		for (int row = 0; row < objects->model()->rowCount(); ++row) {
			setObjectSelected(objects, row, objectIndex(objects, row).data(Qt::UserRole).toString() == selector);
		}
		settle();
	};

	// A page left is a place to come back to, with the caret where it was.
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(11)));
	editor->setFocus();
	settle();
	trigger(shell, "shell.mode.levels");
	back();
	check(currentPage(shell) == Code && editor->textCursor().blockNumber() == 11, "Alt+Left should go back to the Code page with the caret on its line.");
	forward();
	check(currentPage(shell) == Levels, "Alt+Right should go forward to Levels again.");

	// A jump inside a file is one too: Go to Line leaves a way back.
	trigger(shell, "shell.mode.code");
	whenDialogShows([](QWidget* dialog) {
		if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
			input->setIntValue(30);
			input->accept();
		} else {
			static_cast<QDialog*>(dialog)->reject();
		}
	});
	trigger(shell, "code.goToLine");
	ensureActive(shell, "checkPlaceHistory: after Go To Line");
	check(editor->textCursor().blockNumber() == 29, "Go to Line should move the caret to line 30.");
	back();
	check(currentPage(shell) == Code && editor->textCursor().blockNumber() == 11, "Alt+Left after Go to Line should put the caret back on line 12.");
	forward();
	check(editor->textCursor().blockNumber() == 29, "Alt+Right should take the caret to line 30 again.");
	// So is a jump within the file, as Go to Definition makes one.
	QTextCursor onCall(editor->document()->findBlockByNumber(34));
	onCall.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 3);
	editor->setTextCursor(onCall);
	editor->setFocus();
	settle();
	press(shell, Qt::Key_F12);
	check(editor->textCursor().blockNumber() == 0, "F12 on the call should go to the definition at the top of the file.");
	back();
	check(editor->textCursor().blockNumber() == 34, "Alt+Left after a jump within the file should come back to the call.");
	// A jump that goes nowhere leaves no step that goes nowhere: Go to Line
	// on the caret's own line, then Alt+Left, leaves the page.
	trigger(shell, "shell.mode.levels");
	trigger(shell, "shell.mode.code");
	whenDialogShows([](QWidget* dialog) {
		if (auto* input = qobject_cast<QInputDialog*>(dialog)) {
			input->setIntValue(35);
			input->accept();
		} else {
			static_cast<QDialog*>(dialog)->reject();
		}
	});
	trigger(shell, "code.goToLine");
	ensureActive(shell, "checkPlaceHistory: Go To Line in place");
	back();
	check(currentPage(shell) == Levels, "Alt+Left after a jump to where the caret already was should go back past it.");
	forward();

	// Coming back to Levels brings back the selection it had, while the map is
	// unchanged.
	trigger(shell, "shell.mode.levels");
	selectOnly(QStringLiteral("entity:1"));
	trigger(shell, "shell.mode.textures");
	trigger(shell, "shell.mode.levels");
	selectOnly(QStringLiteral("entity:2"));
	back();
	check(currentPage(shell) == Textures, "Alt+Left from Levels should go back to Textures.");
	back();
	check(currentPage(shell) == Levels && selected() == QStringList {QStringLiteral("entity:1")},
		"Alt+Left again should come back to Levels with the selection it had then.");
	forward();
	forward();
	check(currentPage(shell) == Levels && selected() == QStringList {QStringLiteral("entity:2")},
		"Alt+Right twice should come forward to the later selection.");

	// The toolbar's arrows say where they go, and go there.
	auto* backButton = child<QToolButton>(shell, "navigateBackButton");
	auto* forwardButton = child<QToolButton>(shell, "navigateForwardButton");
	check(backButton && forwardButton && backButton->isEnabled() && !forwardButton->isEnabled()
			&& backButton->toolTip().startsWith(QStringLiteral("Go back to Textures")),
		"The toolbar's back arrow should say where it goes, and forward be off with nowhere to go.");
	if (backButton && forwardButton) {
		backButton->click();
		settle();
		check(currentPage(shell) == Textures && forwardButton->isEnabled()
				&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Back to Textures")),
			"The back arrow should go back and say where to.");
		forwardButton->click();
		settle();
		check(currentPage(shell) == Levels && !forwardButton->isEnabled(), "The forward arrow should come forward again.");
	}

	// The mouse's back button does as Alt+Left, from anywhere in the window.
	QTest::mouseClick(objects->viewport(), Qt::BackButton, Qt::NoModifier, QPoint(4, 4));
	settle();
	check(currentPage(shell) == Textures, "The mouse's back button should go back.");
	QTest::mouseClick(child<QWidget>(shell, "modeStack"), Qt::ForwardButton, Qt::NoModifier, QPoint(4, 4));
	settle();
	check(currentPage(shell) == Levels, "The mouse's forward button should go forward.");

	// Coming back to Textures brings back the image that was current.
	auto* textures = child<QListWidget>(shell, "textureEntries");
	trigger(shell, "shell.mode.textures");
	QVector<int> imageRows;
	for (int row = 0; textures && row < textures->count(); ++row) {
		if (textures->item(row)->flags().testFlag(Qt::ItemIsSelectable) && !textures->item(row)->isHidden()) {
			imageRows << row;
		}
	}
	check(imageRows.size() >= 2, "Textures should list the package's images.");
	if (imageRows.size() >= 2) {
		textures->setCurrentRow(imageRows.at(0));
		const QString first = textures->item(imageRows.at(0))->data(Qt::UserRole).toString();
		trigger(shell, "shell.mode.levels");
		trigger(shell, "shell.mode.textures");
		textures->setCurrentRow(imageRows.at(1));
		back();
		back();
		check(currentPage(shell) == Textures && textures->currentItem() && textures->currentItem()->data(Qt::UserRole).toString() == first,
			"Alt+Left back to Textures should bring back the image that was current then.");
	}

	// A move of the user's own leaves nothing to go forward to.
	trigger(shell, "shell.mode.levels");
	back();
	check(forwardButton && forwardButton->isEnabled(), "Going back should leave the way forward on the toolbar.");
	trigger(shell, "shell.mode.build");
	check(forwardButton && !forwardButton->isEnabled(), "A new move should turn the toolbar's forward arrow off.");
	forward();
	check(currentPage(shell) == Build && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Nowhere to go forward to")),
		"After a new move, Alt+Right should say there is nowhere to go forward to.");

	// A map opened again is a new load: a place from before it keeps its page
	// but not its selection, whose ids may name other objects now.
	trigger(shell, "shell.mode.levels");
	selectOnly(QStringLiteral("entity:1"));
	trigger(shell, "shell.mode.textures");
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	ensureActive(shell, "checkPlaceHistory: map reopened");
	selectOnly(QStringLiteral("entity:2"));
	back();
	back();
	check(currentPage(shell) == Levels && selected() == QStringList {QStringLiteral("entity:2")},
		"Going back past a reload of the map should not bring back a selection from before it.");

	// A quick second press of the back button, a double-click to Qt, is a
	// second step.
	trigger(shell, "shell.mode.levels");
	trigger(shell, "shell.mode.textures");
	trigger(shell, "shell.mode.code");
	if (QWidget* stack = child<QWidget>(shell, "modeStack")) {
		QTest::mousePress(stack, Qt::BackButton, Qt::NoModifier, QPoint(4, 4));
		QTest::mouseRelease(stack, Qt::BackButton, Qt::NoModifier, QPoint(4, 4));
		QMouseEvent twice(QEvent::MouseButtonDblClick, QPointF(4, 4), stack->mapToGlobal(QPointF(4, 4)), Qt::BackButton, Qt::BackButton, Qt::NoModifier);
		QApplication::sendEvent(stack, &twice);
		QTest::mouseRelease(stack, Qt::BackButton, Qt::NoModifier, QPoint(4, 4));
		settle();
	}
	check(currentPage(shell) == Levels, "A quick double press of the mouse's back button should go back two places.");

	// A file reloaded after a change outside is shown again, not moved to,
	// so the way forward stays.
	shell.openPathFromCommandLine(path);
	settle();
	trigger(shell, "shell.mode.levels");
	back();
	check(currentPage(shell) == Code && forwardButton && forwardButton->isEnabled(), "Going back to Code should leave a way forward.");
	writeFile(path, text + "// changed outside\n");
	check(waitFor([editor]() { return editor->toPlainText().contains(QStringLiteral("changed outside")); }), "The file should reload after a change outside.");
	check(forwardButton && forwardButton->isEnabled(), "A reload should not be taken for a move, which would drop the way forward.");
	forward();
	check(currentPage(shell) == Levels, "Alt+Right after the reload should still go forward.");

	// A read-only copy of a package entry is not brought back as a file once
	// its tab is closed: edits to it would never reach the package.
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	auto* packageTree = child<PackageFolderView>(shell, "packageTree");
	auto entries = tests::PackageRows(child<PackageEntryView>(shell, "packageEntries"));
	trigger(shell, "shell.mode.packages");
	tests::waitForPackageEntries(entries);
	if (packageTree) { packageTree->selectFolder(QStringLiteral("scripts")); }
	settle();
	const int configRow = entries ? rowWithData(entries, Qt::UserRole, QStringLiteral("scripts/autoexec.cfg")) : -1;
	check(tabs && configRow >= 0, "The package should list scripts/autoexec.cfg.");
	if (tabs && configRow >= 0) {
		emit entries->itemActivated(entries->item(configRow));
		settle();
		const QString copy = QDir::fromNativeSeparators(tabs->tabToolTip(tabs->currentIndex()).section(QLatin1Char('\n'), 0, 0));
		shell.openPathFromCommandLine(path);
		settle();
		tabs->setCurrentIndex(codeTabFor(tabs, copy));
		settle();
		trigger(shell, "code.closeFile");
		settle();
		check(codeTabFor(tabs, copy) < 0, "Closing the copy's tab should close it.");
		back();
		check(codeTabFor(tabs, copy) < 0, "Going back should pass over a closed package copy rather than reopen it as a file.");
	}

	// At the start of the history, Alt+Left says so.
	for (int step = 0; step < 60 && !shell.statusBar()->currentMessage().startsWith(QStringLiteral("Nowhere to go back to")); ++step) {
		back();
	}
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Nowhere to go back to")),
		"Past the oldest place, Alt+Left should say there is nowhere to go back to.");
}

// A problem's mark that moves is highlighted again on the line it leaves
// and the line it reaches, not across the whole file: in a large file every
// pause in typing stalled for seconds while all of it was highlighted again.
void checkDiagnosticHighlightCost()
{
	QTextDocument document;
	QString text;
	for (int line = 1; line <= 20000; ++line) {
		text += QStringLiteral("void() fn_%1 = { local float x = %1; x = x + 1; };\n").arg(line);
	}
	document.setPlainText(text);
	StudioSyntaxHighlighter highlighter(&document);
	highlighter.setLanguage(StudioLanguage::QuakeC);
	highlighter.rehighlight();
	const auto marked = [&document](int line) {
		const QTextBlock block = document.findBlockByNumber(line - 1);
		for (const QTextLayout::FormatRange& range : block.layout()->formats()) {
			if (range.format.underlineStyle() == QTextCharFormat::SpellCheckUnderline) {
				return true;
			}
		}
		return false;
	};
	StudioDiagnosticMarker marker;
	marker.line = 19000;
	marker.severity = QStringLiteral("error");
	marker.message = QStringLiteral("Stand-in problem");
	highlighter.setDiagnostics({marker});
	check(marked(19000), "A problem's line should be marked.");
	QElapsedTimer clock;
	clock.start();
	marker.line = 19001;
	highlighter.setDiagnostics({marker});
	const qint64 moved = clock.elapsed();
	check(marked(19001) && !marked(19000), "A moved problem's mark should leave its old line for the new one.");
	check(moved < 100, QStringLiteral("Moving one mark in a 20,000-line file should not highlight it all again (%1 ms).").arg(moved).toStdString().c_str());
	highlighter.clearDiagnostics();
	check(!marked(19001), "Clearing the problems should take their marks away.");
	// A line an edit above has moved keeps its mark until the problem goes,
	// and then loses it wherever it is.
	marker.line = 3;
	highlighter.setDiagnostics({marker});
	QTextCursor top(&document);
	top.insertText(QStringLiteral("// a new first line\n"));
	check(marked(4), "A marked line moved down by an edit above should keep its mark.");
	highlighter.clearDiagnostics();
	check(!marked(4) && !marked(3), "The mark should go when the problem goes, though the line moved.");
}

// Selecting many objects selects their rows in one go. A row at a time made
// the Objects list lay out every row it had just been given again for each
// one: Select All on a map of 500 brushes took ten seconds.
void checkLargeSelection(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkLargeSelection");
	constexpr int brushCount = 150;
	QByteArray text = "{\n\"classname\" \"worldspawn\"\n";
	for (int index = 0; index < brushCount; ++index) {
		const int x = (index % 15) * 256;
		const int y = (index / 15) * 256;
		const auto point = [x, y](int px, int py, int pz) {
			return QByteArray("( ") + QByteArray::number(px + x) + ' ' + QByteArray::number(py + y) + ' ' + QByteArray::number(pz) + " )";
		};
		text += "{\n";
		text += point(-64, -64, -16) + ' ' + point(-64, -63, -16) + ' ' + point(-64, -64, -15) + " base/wall 0 0 0 1 1\n";
		text += point(64, -64, -16) + ' ' + point(64, -64, -15) + ' ' + point(64, -63, -16) + " base/wall 0 0 0 1 1\n";
		text += point(-64, -64, -16) + ' ' + point(-64, -64, -15) + ' ' + point(-63, -64, -16) + " base/wall 0 0 0 1 1\n";
		text += point(-64, 64, -16) + ' ' + point(-63, 64, -16) + ' ' + point(-64, 64, -15) + " base/wall 0 0 0 1 1\n";
		text += point(-64, -64, -16) + ' ' + point(-63, -64, -16) + ' ' + point(-64, -63, -16) + " base/floor 0 0 0 1 1\n";
		text += point(-64, -64, 0) + ' ' + point(-64, -63, 0) + ' ' + point(-63, -64, 0) + " base/floor 0 0 0 1 1\n}\n";
	}
	text += "}\n";
	const QString path = QFileInfo(fixtures.map).absoluteDir().filePath(QStringLiteral("many.map"));
	writeFile(path, text);
	// Edits left on whatever map is open are let go.
	whenDialogShows([](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			for (QAbstractButton* button : box->buttons()) {
				if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
					button->click();
					return;
				}
			}
		}
	});
	shell.openPathFromCommandLine(path);
	settle();
	trigger(shell, "shell.mode.levels");
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	check(objects && view && objects->model()->rowCount() == brushCount + 1, "The many-brush map should list its brushes and its worldspawn.");
	if (!objects || !view) {
		return;
	}
	int changes = 0;
	const QMetaObject::Connection counting = QObject::connect(objects->selectionModel(), &QItemSelectionModel::selectionChanged, [&changes]() {
		++changes;
	});
	trigger(shell, "map.selectAll");
	check(objects->selectionModel()->selectedRows().size() == brushCount && changes <= 2,
		QStringLiteral("Select All should select every brush's row at once (%1 selection changes).").arg(changes).toStdString().c_str());
	// A selection made in the view reaches the list the same way.
	trigger(shell, "map.selectNone");
	QVector<LevelMapSelectionRef> everyBrush;
	for (int index = 0; index < brushCount; ++index) {
		everyBrush.push_back({LevelMapSelectionKind::QuakeBrush, index});
	}
	changes = 0;
	view->setSelectionSet(everyBrush);
	Q_EMIT view->selectionSetChanged(everyBrush);
	settle();
	check(objects->selectionModel()->selectedRows().size() == brushCount && changes <= 2,
		QStringLiteral("A selection made in the view should reach the list at once (%1 selection changes).").arg(changes).toStdString().c_str());
	QObject::disconnect(counting);
	// The view keeps each object once, where it was named last, so the last
	// named is primary, as the map document does.
	view->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 3}, {LevelMapSelectionKind::QuakeBrush, 5}, {LevelMapSelectionKind::QuakeBrush, 3}});
	check(view->selectionSet() == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::QuakeBrush, 5}, {LevelMapSelectionKind::QuakeBrush, 3}}
			&& view->selectedObjectId() == 3,
		"The view should keep an object named twice once, where it was named last, and make it primary.");
}



void checkDropNames(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkDropNames");
	// Drops stage only onto the Packages page's entries.
	trigger(shell, "shell.mode.packages");
	auto entries = tests::PackageRows(child<PackageEntryView>(shell, "packageEntries"));
	auto staging = tests::StagingRows(child<PackageStagingView>(shell, "packageStagingSummary"));
	if (!entries || !staging) {
		check(false, "The Packages page should have its entries and staging list.");
		return;
	}
	const QDir source(QFileInfo(fixtures.soundWad).dir().filePath(QStringLiteral("named")));
	QDir().mkpath(source.filePath(QStringLiteral("one")));
	QDir().mkpath(source.filePath(QStringLiteral("two")));
	writeFile(source.filePath(QStringLiteral("one/same.cfg")), QByteArrayLiteral("one"));
	writeFile(source.filePath(QStringLiteral("two/same.cfg")), QByteArrayLiteral("two"));
	const auto dropOnEntries = [&shell, entries](const QList<QUrl>& urls) {
		QMimeData data;
		data.setUrls(urls);
		const QPoint at = entries->mapTo(&shell, entries->rect().center());
		QDragEnterEvent enter(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(&shell, &enter);
		QDropEvent drop(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(&shell, &drop);
		settle();
	};
	auto asked = std::make_shared<QString>();
	whenDialogShows([asked](QWidget* modal) {
		*asked = modal->windowTitle();
		if (auto* input = qobject_cast<QInputDialog*>(modal)) {
			input->setTextValue(QStringLiteral("Skip conflicting operation"));
			input->accept();
		} else if (auto* dialog = qobject_cast<QDialog*>(modal)) {
			dialog->reject();
		}
	});
	dropOnEntries({QUrl::fromLocalFile(source.filePath(QStringLiteral("one/same.cfg"))), QUrl::fromLocalFile(source.filePath(QStringLiteral("two/same.cfg")))});
	check(*asked == QStringLiteral("Dropped Files Already In The Package"), "Two dropped files landing on one name should ask about the clash first.");

	// Onto a Doom WAD: the open package's staged changes are let go first.
	whenDialogShows([](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			for (QAbstractButton* button : box->buttons()) {
				if (box->buttonRole(button) == QMessageBox::AcceptRole || box->buttonRole(button) == QMessageBox::DestructiveRole
					|| box->buttonRole(button) == QMessageBox::YesRole) {
					button->click();
					return;
				}
			}
		}
	});
	shell.openPathFromCommandLine(fixtures.soundWad);
	settle();
	writeFile(source.filePath(QStringLiteral("longtexturename.lmp")), QByteArrayLiteral("long"));
	writeFile(source.filePath(QStringLiteral("short.lmp")), QByteArrayLiteral("short"));
	dropOnEntries({QUrl::fromLocalFile(source.filePath(QStringLiteral("longtexturename.lmp"))), QUrl::fromLocalFile(source.filePath(QStringLiteral("short.lmp")))});
	check(shell.statusBar()->currentMessage().contains(QStringLiteral("at most 8 characters"))
			&& rowWithData(staging, Qt::UserRole, QStringLiteral("SHORT")) >= 0 && rowWithData(staging, Qt::UserRole, QStringLiteral("LONGTEXTURENAME")) < 0,
		"Onto a Doom WAD, a name longer than a lump name should be left out and said so, the rest staged.");
}

// The Levels Textures tab lists the open map's textures, most used first,
// with those applied lately ahead of them; Enter puts the chosen one on the
// selection; and Select Objects Using It selects what shows it.
void checkLevelTextures(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkLevelTextures");
	// Edits left on whatever map is open are let go.
	whenDialogShows([](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			for (QAbstractButton* button : box->buttons()) {
				if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
					button->click();
					return;
				}
			}
		}
	});
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	auto* textures = child<QListWidget>(shell, "levelMapTextures");
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	check(textures && objects, "Levels should have its Textures tab and objects list.");
	if (!textures || !objects) {
		return;
	}
	const auto names = [textures]() {
		QStringList listed;
		for (int row = 0; row < textures->count(); ++row) {
			listed << textures->item(row)->data(Qt::UserRole).toString();
		}
		return listed;
	};
	// Textures applied earlier in the run lead; the map's own follow by use.
	const QStringList before = names();
	check(before.contains(QStringLiteral("base/wall")) && before.indexOf(QStringLiteral("base/wall")) < before.indexOf(QStringLiteral("base/floor")),
		"The map's textures should be listed, the most used first.");
	// The room's brush takes base/floor on every face.
	for (int row = 0; row < objects->model()->rowCount(); ++row) {
		if (objectIndex(objects, row).data(Qt::DisplayRole).toString().startsWith(QStringLiteral("Brush 0"))) {
			setObjectCurrentRow(objects, row);
		}
	}
	settle();
	if (QListWidgetItem* floor = textures->item(names().indexOf(QStringLiteral("base/floor")))) {
		emit textures->itemActivated(floor);
		settle();
	}
	check(names().first() == QStringLiteral("base/floor") && !names().contains(QStringLiteral("base/wall"))
			&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Applied base/floor")),
		"Enter on a texture should put it on the selection, and the one it covered, used nowhere now, should go."); 
	// Undone, the walls come back, and the texture applied lately still leads.
	trigger(shell, "map.undo");
	check(names().first() == QStringLiteral("base/floor") && names().indexOf(QStringLiteral("base/floor")) < names().indexOf(QStringLiteral("base/wall"))
			&& textures->item(0)->toolTip().contains(QStringLiteral("applied lately")),
		"A texture applied lately should lead the list, ahead of one used more."); 
	if (!qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_SNAPSHOTS")) {
		for (QWidget* parent = textures->parentWidget(); parent; parent = parent->parentWidget()) {
			if (auto* tabs = qobject_cast<QTabWidget*>(parent)) {
				tabs->setCurrentIndex(tabs->indexOf(textures->parentWidget()) >= 0 ? tabs->indexOf(textures->parentWidget()) : tabs->currentIndex());
				break;
			}
		}
		snapshot(shell, "level-textures");
	}
	// Select Objects Using It, from the texture's menu.
	objects->clearSelection();
	whenMenuShows([](QMenu* menu) {
		if (QAction* select = menuAction(menu, QStringLiteral("Select Objects Using It"))) {
			chooseMenuAction(menu, select);
		}
	});
	if (QListWidgetItem* wall = textures->item(names().indexOf(QStringLiteral("base/wall")))) {
		emit textures->customContextMenuRequested(textures->visualItemRect(wall).center());
		settle();
	}
	check(shell.statusBar()->currentMessage() == QStringLiteral("Selected the 1 object(s) that use base/wall."),
		"Select Objects Using It should select what shows the texture.");
	// Hidden, the brush stays out, and the status bar says why.
	trigger(shell, "map.hideSelection");
	objects->clearSelection();
	whenMenuShows([](QMenu* menu) {
		if (QAction* select = menuAction(menu, QStringLiteral("Select Objects Using It"))) {
			chooseMenuAction(menu, select);
		}
	});
	if (QListWidgetItem* wall = textures->item(names().indexOf(QStringLiteral("base/wall")))) {
		emit textures->customContextMenuRequested(textures->visualItemRect(wall).center());
		settle();
	}
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Only hidden objects use base/wall")) && objects->selectionModel()->selectedRows().isEmpty(),
		"Select Objects Using It should leave hidden objects out, and say so.");
	trigger(shell, "map.showAll");
}

// F12 opens where the name at the caret is defined, in another project
// file; Alt+Left comes back to where the caret was; Ctrl+click does as F12.
void checkGoToDefinition(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkGoToDefinition");
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	const QString defs = QDir(fixtures.project).filePath(QStringLiteral("progs/defs_helper.qc"));
	const QString uses = QDir(fixtures.project).filePath(QStringLiteral("progs/uses_helper.qc"));
	writeFile(defs, QByteArrayLiteral("// helpers\nvoid() review_helper = {\n\tself.frame = 1;\n};\n"));
	writeFile(uses, QByteArrayLiteral("void() caller = {\n\tlocal float unused;\n\treview_helper();\n\treview_helper_extra();\n};\n"));
	shell.openPathFromCommandLine(uses);
	settle();
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	auto* tabs = child<QTabBar>(shell, "codeTabs");
	if (!editor || !tabs) {
		check(false, "The Code page should have its editor and tabs.");
		return;
	}
	QTextCursor onName(editor->document()->findBlockByNumber(2));
	onName.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 4);
	editor->setTextCursor(onName);
	editor->setFocus();
	settle();
	press(shell, Qt::Key_F12);
	check(tabs->currentIndex() == codeTabFor(tabs, defs) && editor->textCursor().blockNumber() == 1,
		"F12 on a name should open the file that defines it, at the definition.");
	press(shell, Qt::Key_Left, Qt::AltModifier);
	check(tabs->currentIndex() == codeTabFor(tabs, uses) && editor->textCursor().blockNumber() == 2,
		"Alt+Left should come back to where the caret was.");
	// Ctrl+click on the name does as F12.
	const QRect nameRect = editor->cursorRect(onName);
	QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::ControlModifier, nameRect.center() + QPoint(2, 0));
	settle();
	check(tabs->currentIndex() == codeTabFor(tabs, defs), "Ctrl+click on a name should go to where it is defined.");
	// Shift+F12 lists every use as a whole word: the definition and the call,
	// not review_helper_extra.
	press(shell, Qt::Key_Left, Qt::AltModifier);
	editor->setTextCursor(onName);
	press(shell, Qt::Key_F12, Qt::ShiftModifier);
	auto* results = child<QListWidget>(shell, "codeSearchResults");
	check(results && waitFor([results]() { return results->count() == 2; }) && results->isVisible(),
		"Shift+F12 should list the name's two whole-word uses under Search Results.");
	// A name defined nowhere says so.
	QTextCursor onKeyword(editor->document()->findBlockByNumber(1));
	onKeyword.movePosition(QTextCursor::NextWord, QTextCursor::MoveAnchor, 2);
	editor->setTextCursor(onKeyword);
	press(shell, Qt::Key_F12);
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("No definition of")), "A name defined nowhere should say so.");
	settle();

	// Ctrl+Space offers the names that start with what is typed, whatever its
	// case; Enter puts the first in, in its own case.
	shell.openPathFromCommandLine(uses);
	settle();
	QTextCursor lineEnd(editor->document()->findBlockByNumber(1));
	lineEnd.movePosition(QTextCursor::EndOfBlock);
	editor->setTextCursor(lineEnd);
	editor->setFocus();
	settle();
	QTest::keyClicks(editor, QStringLiteral(" REVI"));
	press(shell, Qt::Key_Space, Qt::ControlModifier);
	auto* studioEditor = dynamic_cast<StudioCodeEditor*>(editor);
	QAbstractItemView* popup = studioEditor && studioEditor->completer() ? studioEditor->completer()->popup() : nullptr;
	check(popup && popup->isVisible() && studioEditor->completer()->completionCount() == 2,
		"Ctrl+Space should offer the two names that start with REVI.");
	if (popup && popup->isVisible()) {
		QTest::keyClick(popup, Qt::Key_Return);
		settle();
	}
	check(editor->document()->findBlockByNumber(1).text().endsWith(QStringLiteral(" review_helper")),
		"Enter should put the first completion in, in its own case.");
	// The outline lists the open file's symbols, follows the caret, and moves
	// it to a symbol on Enter; a symbol typed in shows up a moment later.
	shell.openPathFromCommandLine(defs);
	settle();
	auto* outline = child<QListWidget>(shell, "codeOutline");
	check(outline && outline->count() == 1 && outline->item(0)->text() == QStringLiteral("review_helper"),
		"The outline should list the open file's function.");
	if (outline) {
		QTextCursor inside(editor->document()->findBlockByNumber(2));
		editor->setTextCursor(inside);
		settle();
		check(outline->currentRow() == 0, "The function holding the caret should be current in the outline.");
		QTextCursor end(editor->document());
		end.movePosition(QTextCursor::End);
		editor->setTextCursor(end);
		editor->insertPlainText(QStringLiteral("void() added_later = {\n};\n"));
		QTest::qWait(600);
		check(outline->count() == 2 && outline->item(1)->text() == QStringLiteral("added_later"),
			"A function typed in should join the outline a moment later.");
		emit outline->itemActivated(outline->item(0));
		settle();
		check(editor->textCursor().blockNumber() == 1, "Enter on a symbol should move the caret to it.");
		if (auto* sideTabs = child<QTabWidget>(shell, "codeSideTabs"); sideTabs && !qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_SNAPSHOTS")) {
			sideTabs->setCurrentIndex(1);
			snapshot(shell, "code-outline");
			sideTabs->setCurrentIndex(0);
		}
	}
	trigger(shell, "project.close");
}

// The tool bar's work buttons keep their glyphs, and the command search its
// label and key, when their actions change, as a key rebind changes them.
void checkToolBarLabels(ApplicationShell& shell)
{
	ensureActive(shell, "checkToolBarLabels");
	auto* bar = shell.findChild<QToolBar*>(QStringLiteral("studioToolBar"));
	auto* search = shell.findChild<QToolButton*>(QStringLiteral("commandSearchButton"));
	QAction* run = action(shell, "build.runPipeline");
	QAction* palette = action(shell, "shell.commandPalette");
	auto* runButton = bar && run ? qobject_cast<QToolButton*>(bar->widgetForAction(run)) : nullptr;
	check(search && runButton && palette, "The tool bar should have its build button and command search.");
	if (!search || !runButton || !palette) {
		return;
	}
	// Glyphs compared as drawn: the icons are made afresh on each request.
	const auto drawn = [](const QIcon& icon) {
		return icon.pixmap(QSize(18, 18)).toImage();
	};
	const QImage hammer = drawn(studioIcon(QStringLiteral("hammer"), StudioIconTone::Normal, StudioIconAlignment::Leading));
	const QImage magnifier = drawn(studioIcon(QStringLiteral("search"), StudioIconTone::Muted, StudioIconAlignment::Leading));
	check(search->text().startsWith(QStringLiteral("Search commands")) && drawn(runButton->icon()) == hammer && drawn(search->icon()) == magnifier,
		"The build button should show the hammer, and the command search its magnifier and label.");
	for (QAction* changed : {run, palette}) {
		const QString text = changed->text();
		changed->setText(text + QStringLiteral(" "));
		changed->setText(text);
	}
	settle();
	check(drawn(runButton->icon()) == hammer && drawn(search->icon()) == magnifier && search->text().startsWith(QStringLiteral("Search commands")),
		"A change to their actions should leave the build glyph and the command search's glyph and label alone.");
	check(search->text().endsWith(palette->shortcuts().value(0).toString(QKeySequence::NativeText)), "The command search should name its key.");
}

// Settings search keeps the categories whose settings mention the text,
// shows the first of them, says when nothing matches, and Ctrl+F reaches it.
void checkSettingsSearch(ApplicationShell& shell)
{
	ensureActive(shell, "checkSettingsSearch");
	trigger(shell, "shell.mode.settings");
	auto* search = child<QLineEdit>(shell, "settingsSearch");
	auto* categories = child<QListWidget>(shell, "settingsCategories");
	auto* noMatch = child<QLabel>(shell, "settingsNoMatch");
	check(search && categories && noMatch, "Settings should have its search, categories, and no-match line.");
	if (!search || !categories || !noMatch) {
		return;
	}
	trigger(shell, "shell.focusSearch");
	check(QApplication::focusWidget() == search, "Ctrl+F on Settings should reach the settings search.");
	// From another category, on a page too short to show the match without
	// scrolling. The page, not the window, is made short: the window cannot
	// always shrink that far.
	auto* keep = child<QCheckBox>(shell, "crashReports");
	QScrollArea* scroll = nullptr;
	for (QWidget* parent = keep ? keep->parentWidget() : nullptr; parent && !scroll; parent = parent->parentWidget()) {
		scroll = qobject_cast<QScrollArea*>(parent);
	}
	check(keep && scroll, "The crash report switch should sit on a scrolling Settings page.");
	if (!keep || !scroll) {
		return;
	}
	scroll->setMaximumHeight(160);
	scroll->verticalScrollBar()->setValue(0);
	categories->setCurrentRow(0);
	settle();
	search->setText(QStringLiteral("crash report"));
	settle();
	QStringList shown;
	for (int row = 0; row < categories->count(); ++row) {
		if (!categories->item(row)->isHidden()) {
			shown << categories->item(row)->text();
		}
	}
	check(shown == QStringList {QStringLiteral("Appearance and Language")} && categories->currentItem()
			&& categories->currentItem()->text() == QStringLiteral("Appearance and Language"),
		"Searching for crash report should keep only the category that holds it, and show it.");
	// The match is scrolled into view, and Enter goes to it.
	const QRect inViewport(keep->mapTo(scroll->viewport(), QPoint(0, 0)), keep->size());
	check(scroll->verticalScrollBar()->value() > 0 && scroll->viewport()->rect().contains(inViewport),
		"The first matching setting should be scrolled into view.");
	QTest::keyClick(search, Qt::Key_Return);
	settle();
	check(QApplication::focusWidget() == keep, "Enter in the settings search should go to the first matching setting.");
	search->setFocus(Qt::OtherFocusReason);
	scroll->setMaximumHeight(QWIDGETSIZE_MAX);
	// "setup" first matches the setup panel, a frame; Enter goes to a
	// control inside it, not nowhere.
	search->setText(QStringLiteral("setup"));
	settle();
	categories->setCurrentRow(0);
	settle();
	QTest::keyClick(search, Qt::Key_Return);
	settle();
	QWidget* reached = QApplication::focusWidget();
	check(reached && reached != search && (reached->focusPolicy() & Qt::TabFocus) && settingsCategoryPages(shell)
			&& settingsCategoryPages(shell)->currentWidget()->isAncestorOf(reached),
		"Enter should reach a control on the page even when a group of settings matched first.");
	search->setFocus(Qt::OtherFocusReason);
	search->setText(QStringLiteral("no such setting anywhere"));
	settle();
	check(noMatch->isVisible() && noMatch->text().contains(QStringLiteral("no such setting anywhere")), "A search nothing mentions should say so.");
	search->clear();
	settle();
	bool allShown = !noMatch->isVisible();
	for (int row = 0; row < categories->count(); ++row) {
		allShown = allShown && !categories->item(row)->isHidden();
	}
	check(allShown, "Clearing the search should bring every category back.");
}

// The status bar keeps room for a message: short of it, the panel toggles fold
// to their icons, then the chips to their icon and state mark, and they come
// back when room returns.
void checkStatusBarFolding()
{
	QWidget host;
	auto* hostLayout = new QVBoxLayout(&host);
	hostLayout->setContentsMargins(0, 0, 0, 0);
	auto* bar = new QStatusBar;
	bar->setSizeGripEnabled(false);
	hostLayout->addWidget(bar);
	auto* toggle = new QToolButton;
	toggle->setIcon(studioIcon(QStringLiteral("activity")));
	toggle->setText(QStringLiteral("Activity"));
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	bar->addPermanentWidget(toggle);
	QToolButton* chip = createStatusChip(QStringLiteral("Project status"), QStringLiteral("project"));
	bar->addPermanentWidget(chip);
	setStatusChip(chip, QStringLiteral("completed"), QStringLiteral("A long project name"), QStringLiteral("The project folder is open."));
	fitStatusBarToMessages(bar, {toggle, chip});
	host.resize(1600, 40);
	host.show();
	settle();
	const int message = bar->fontMetrics().averageCharWidth() * 40;
	const int chipFull = chip->sizeHint().width();
	const int toggleFull = toggle->sizeHint().width();
	toggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
	const int toggleFolded = toggle->sizeHint().width();
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	check(!statusBarButtonFolded(toggle) && !statusBarButtonFolded(chip) && chip->text().endsWith(QStringLiteral("A long project name")),
		"With room, the status bar should keep every label.");
	check(host.minimumSizeHint().width() < toggleFull + chipFull,
		"Unfolded, the bar should still let the window shrink as far as everything folded, or it could never fold.");
	// Room for the message once the toggle folds, but not before: the bar keeps
	// 18 pixels of margin and 6 between widgets.
	host.resize(message + 30 + chipFull + toggleFolded + (toggleFull - toggleFolded) / 2, 40);
	settle();
	check(statusBarButtonFolded(toggle) && toggle->toolButtonStyle() == Qt::ToolButtonIconOnly && !statusBarButtonFolded(chip),
		"A little short of room, the toggle should fold to its icon first.");
	host.resize(message, 40);
	settle();
	check(toggle->toolTip().startsWith(QStringLiteral("Activity")), "A folded toggle should keep its label in its tooltip.");
	setStatusBarButtonText(toggle, QStringLiteral("Activity (2 running)"));
	check(toggle->toolTip().startsWith(QStringLiteral("Activity (2 running)")), "A folded toggle's tooltip should follow its label.");
	setStatusBarButtonText(toggle, QStringLiteral("Activity"));
	check(statusBarButtonFolded(chip) && chip->text() == studioStateGlyph(OperationState::Completed)
			&& chip->toolTip().startsWith(QStringLiteral("A long project name")) && chip->accessibleName() == QStringLiteral("Project status"),
		"Shorter still, the chip should fold to its icon and state mark, its text in the tooltip.");
	setStatusChip(chip, QStringLiteral("failed"), QStringLiteral("Another name"), QString());
	settle();
	check(statusBarButtonFolded(chip) && chip->text() == studioStateGlyph(OperationState::Failed) && chip->toolTip() == QStringLiteral("Another name"),
		"A folded chip should show a new state's mark and keep the new text in its tooltip.");
	check(host.minimumSizeHint().width() < toggleFull + chipFull, "The bar should let the window shrink as far as everything folded.");
	host.resize(1600, 40);
	settle();
	check(!statusBarButtonFolded(toggle) && !statusBarButtonFolded(chip) && toggle->toolButtonStyle() == Qt::ToolButtonTextBesideIcon
			&& chip->text().endsWith(QStringLiteral("Another name")),
		"With room again, every label should come back.");
	// A chip whose text grows makes the bar fit itself again.
	setStatusChip(chip, QStringLiteral("completed"), QStringLiteral("Name"), QString());
	settle();
	host.resize(message + 30 + toggle->sizeHint().width() + chip->sizeHint().width() + 4, 40);
	settle();
	check(!statusBarButtonFolded(toggle), "Just enough room should keep the toggle's label.");
	setStatusChip(chip, QStringLiteral("completed"), QStringLiteral("A much longer project name than before"), QString());
	settle();
	check(statusBarButtonFolded(toggle), "A chip whose text grows should make room by folding the toggle.");
}

// Folding hides the inside of a brace pair of three lines or more, keeps a
// fold through edits elsewhere, opens it when its braces stop pairing or the
// caret goes inside, and folds or unfolds from the gutter and the badge.
void checkCodeFolding(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCodeFolding");
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	const QString path = QDir(fixtures.project).filePath(QStringLiteral("progs/fold_sample.qc"));
	// A closing brace in a string or a comment, counted, would end the outer
	// pair early.
	writeFile(path, QByteArrayLiteral("// fold sample\n"
					  "void() outer = {\n"
					  "\tif (self.health) {\n"
					  "\t\tself.frame = 1;\n"
					  "\t\tself.frame = 2;\n"
					  "\t}\n"
					  "\tself.message = \"} not a brace\"; // } nor this\n"
					  "\t/* } nor this */\n"
					  "};\n"
					  "void() tiny = {\n"
					  "};\n"));
	shell.openPathFromCommandLine(path);
	settle();
	auto* editor = dynamic_cast<StudioCodeEditor*>(child<QPlainTextEdit>(shell, "codeEditor"));
	check(editor && !editor->isReadOnly(), "The fold sample should open for editing.");
	if (!editor) {
		return;
	}
	const auto visible = [editor](int line) {
		return editor->document()->findBlockByNumber(line - 1).isVisible();
	};
	const auto caretTo = [editor](int line) {
		editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(line - 1)));
		settle();
	};
	editor->setFocus();
	check(editor->foldMarkerToolTip(2) == QStringLiteral("Fold lines 3 to 8") && editor->foldMarkerToolTip(3) == QStringLiteral("Fold lines 4 to 5")
			&& editor->foldMarkerToolTip(10).isEmpty(),
		"Pairs of three lines or more should fold, and braces in strings and comments should not count.");

	// Fold, from inside the inner pair, folds that pair; again, the outer one.
	caretTo(4);
	press(shell, Qt::Key_BracketLeft, Qt::ControlModifier | Qt::ShiftModifier);
	check(editor->foldedLines() == QVector<int> {3} && !visible(4) && !visible(5) && visible(6) && editor->textCursor().blockNumber() == 2
			&& shell.statusBar()->currentMessage() == QStringLiteral("Lines 4 to 5 folded."),
		"Ctrl+Shift+[ should fold the pair holding the caret, keep its closing line, and move the caret to its first line.");
	trigger(shell, "code.fold");
	check(editor->foldedLines() == QVector<int> {2, 3} && !visible(3) && !visible(8) && visible(9) && editor->textCursor().blockNumber() == 1,
		"Fold again should fold the pair around the folded one.");
	// The arrow keys step over a fold.
	press(shell, Qt::Key_Down);
	check(editor->textCursor().blockNumber() == 8, "Down from a folded line should land on the fold's closing line.");
	trigger(shell, "code.unfold");
	check(shell.statusBar()->currentMessage() == QStringLiteral("Nothing is folded on the caret's line."), "Unfold where nothing is folded should say so.");
	caretTo(2);
	press(shell, Qt::Key_BracketRight, Qt::ControlModifier | Qt::ShiftModifier);
	check(editor->foldedLines() == QVector<int> {3} && visible(3) && !visible(4) && visible(8),
		"Ctrl+Shift+] should unfold the outer pair and leave the inner one folded.");
	// The caret moving inside opens the fold that hides it.
	caretTo(4);
	check(editor->foldedLines().isEmpty() && visible(4) && visible(5), "The caret moving into a fold should open it.");

	// Fold All and Unfold All; the caret waits on the outermost fold's line.
	trigger(shell, "code.foldAll");
	check(editor->foldedLines() == QVector<int> {2, 3} && editor->textCursor().blockNumber() == 1
			&& shell.statusBar()->currentMessage() == QStringLiteral("2 block(s) folded."),
		"Fold All should fold every pair, with the caret on the outermost fold's line.");
	trigger(shell, "code.unfoldAll");
	check(editor->foldedLines().isEmpty() && visible(4) && visible(8), "Unfold All should open every fold.");

	// An edit that unpairs a fold's braces opens it; undo pairs them again.
	caretTo(3);
	trigger(shell, "code.fold");
	check(editor->foldedLines() == QVector<int> {3}, "Fold on the pair's own line should fold it.");
	QTextCursor brace(editor->document()->findBlockByNumber(2));
	brace.movePosition(QTextCursor::EndOfBlock);
	brace.deletePreviousChar();
	settle();
	check(editor->foldedLines().isEmpty() && visible(4) && visible(5), "A fold whose braces no longer pair should open.");
	editor->undo();
	settle();
	check(editor->document()->findBlockByNumber(2).text().endsWith(QLatin1Char('{')), "Undo should put the brace back.");

	// Moving a folded line opens its fold first, so no hidden line moves unseen.
	caretTo(3);
	trigger(shell, "code.fold");
	trigger(shell, "code.moveLinesDown");
	check(editor->foldedLines().isEmpty() && editor->document()->findBlockByNumber(3).text() == QStringLiteral("\tif (self.health) {"),
		"Moving a folded line should unfold it, then move it.");
	editor->undo();
	settle();
	// So does moving a fold's closing line up past its hidden lines.
	caretTo(2);
	trigger(shell, "code.fold");
	caretTo(9);
	trigger(shell, "code.moveLinesUp");
	check(editor->foldedLines().isEmpty() && visible(8) && editor->document()->findBlockByNumber(7).text() == QStringLiteral("};"),
		"Moving a fold's closing line up should unfold it first.");
	editor->undo();
	settle();

	// The gutter's chevron folds and unfolds with a click; so does the badge.
	QWidget* gutter = nullptr;
	for (QWidget* candidate : editor->findChildren<QWidget*>()) {
		if (candidate->accessibleName() == QStringLiteral("Line numbers")) {
			gutter = candidate;
		}
	}
	check(gutter != nullptr, "The editor should have its gutter.");
	if (gutter) {
		const int y = editor->cursorRect(QTextCursor(editor->document()->findBlockByNumber(1))).center().y();
		const QPoint chevron(gutter->width() - 6, y);
		check(editor->foldMarkerLineAt(chevron) == 2 && editor->foldMarkerLineAt(QPoint(2, y)) == 0,
			"The chevron should sit in the gutter's fold column, beside the line numbers.");
		QTest::mouseClick(gutter, Qt::LeftButton, Qt::NoModifier, chevron);
		settle();
		check(editor->foldedLines() == QVector<int> {2}, "A click on a chevron should fold its pair.");
		QTextCursor lineEnd(editor->document()->findBlockByNumber(1));
		lineEnd.movePosition(QTextCursor::EndOfBlock);
		const QRect end = editor->cursorRect(lineEnd);
		QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(end.right() + editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')) + 8, end.center().y()));
		settle();
		check(editor->foldedLines().isEmpty() && visible(3), "A click on a folded line's badge should unfold it.");
	}

	// Two folds, one below the other: the lower badge answers a click even
	// when only the caret's rectangle was painted since; Backspace at the start
	// of a fold's closing line leaves the fold below alone; Enter at the start
	// of a folded line keeps the fold with its line.
	const QString pair = QDir(fixtures.project).filePath(QStringLiteral("progs/fold_pair.qc"));
	writeFile(pair, QByteArrayLiteral("void() a = {\n\tself.x = 1;\n\tself.y = 2;\n};\nvoid() b = {\n\tself.x = 3;\n\tself.y = 4;\n};\n"));
	shell.openPathFromCommandLine(pair);
	settle();
	editor->setFocus();
	caretTo(1);
	trigger(shell, "code.foldAll");
	check(editor->foldedLines() == QVector<int> {1, 5}, "Fold All should fold both functions.");
	editor->viewport()->repaint(editor->cursorRect());
	QTextCursor secondEnd(editor->document()->findBlockByNumber(4));
	secondEnd.movePosition(QTextCursor::EndOfBlock);
	const QRect secondRect = editor->cursorRect(secondEnd);
	QTest::mouseClick(editor->viewport(), Qt::LeftButton, Qt::NoModifier,
		QPoint(secondRect.right() + editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')) + 8, secondRect.center().y()));
	settle();
	check(editor->foldedLines() == QVector<int> {1}, "The badge of a fold below the caret should unfold it, painted or not.");
	trigger(shell, "code.foldAll");
	caretTo(4);
	press(shell, Qt::Key_Backspace);
	check(editor->foldedLines().contains(4), "Backspace at the start of a fold's closing line should leave the fold below folded.");
	editor->undo();
	settle();
	trigger(shell, "code.unfoldAll");
	caretTo(5);
	trigger(shell, "code.fold");
	check(editor->foldedLines() == QVector<int> {5}, "Fold on the second function's line should fold it.");
	caretTo(5);
	press(shell, Qt::Key_Return);
	check(editor->foldedLines() == QVector<int> {6} && editor->document()->findBlockByNumber(5).text().startsWith(QStringLiteral("void() b")),
		"Enter at the start of a folded line should keep the fold with its line.");
	editor->undo();
	settle();
	closeAllCodeTabs(shell);
}

// Sticky headers pin the opening lines of the blocks the top of the view is
// inside, outermost first, with a lone brace stood for by the line before it;
// a click on one goes there; the View menu turns them off and on.
void checkStickyHeaders(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkStickyHeaders");
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	const QString path = QDir(fixtures.project).filePath(QStringLiteral("progs/sticky_sample.qc"));
	QByteArray text = QByteArrayLiteral("void() long_fn =\n{\n");
	for (int line = 0; line < 20; ++line) {
		text += QByteArrayLiteral("\tself.frame = 1;\n");
	}
	text += QByteArrayLiteral("\tif (self.health)\n\t{\n");
	for (int line = 0; line < 60; ++line) {
		text += QByteArrayLiteral("\t\tself.frame = 2;\n");
	}
	text += QByteArrayLiteral("\t}\n};\n");
	for (int line = 0; line < 60; ++line) {
		text += QByteArrayLiteral("// after\n");
	}
	text += QByteArray("// ") + QByteArray(300, 'x') + QByteArrayLiteral("\n");
	writeFile(path, text);
	shell.openPathFromCommandLine(path);
	settle();
	auto* editor = dynamic_cast<StudioCodeEditor*>(child<QPlainTextEdit>(shell, "codeEditor"));
	auto* strip = editor ? editor->findChild<QWidget*>(QStringLiteral("codeStickyHeader")) : nullptr;
	check(editor && strip, "The code editor should have its sticky header strip.");
	if (!editor || !strip) {
		return;
	}
	editor->verticalScrollBar()->setValue(0);
	settle();
	check(editor->stickyHeaderLines().isEmpty() && strip->isHidden(), "At the top of the file nothing should be pinned.");
	editor->verticalScrollBar()->setValue(10);
	settle();
	check(editor->stickyHeaderLines() == QVector<int> {1} && strip->isVisible(),
		"Inside the function, its name line should be pinned in place of its lone brace.");
	editor->verticalScrollBar()->setValue(39);
	settle();
	check(editor->stickyHeaderLines() == QVector<int> {1, 23}, "Inside the if, both opening lines should be pinned, outermost first.");
	check(strip->accessibleDescription().contains(QStringLiteral("void() long_fn =")), "The strip should give its pinned lines to screen readers.");
	// A caret put on the top line, under the strip, is brought out below it.
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(39)));
	settle();
	check(editor->cursorRect().top() >= strip->height(), "A caret under the strip should be scrolled out below it.");
	// A click in the gutter beside the strip goes to the pinned line.
	editor->verticalScrollBar()->setValue(39);
	settle();
	QWidget* gutter = nullptr;
	for (QWidget* candidate : editor->findChildren<QWidget*>()) {
		if (candidate->accessibleName() == QStringLiteral("Line numbers")) {
			gutter = candidate;
		}
	}
	if (gutter) {
		QTest::mouseClick(gutter, Qt::LeftButton, Qt::NoModifier, QPoint(gutter->width() - 6, editor->fontMetrics().lineSpacing() + 2));
		settle();
		check(editor->textCursor().blockNumber() == 22 && editor->foldedLines().isEmpty(),
			"A click beside a pinned line in the gutter should go to that line, not fold the one under it.");
	}
	// A sideways scroll leaves the strip where it belongs.
	editor->verticalScrollBar()->setValue(39);
	settle();
	editor->horizontalScrollBar()->setValue(60);
	settle();
	check(strip->x() == 0 && strip->width() == editor->viewport()->width(), "A sideways scroll should leave the strip in place over the text.");
	editor->horizontalScrollBar()->setValue(0);
	settle();
	snapshot(shell, "code-sticky-headers");
	// A click on the second pinned line goes to it.
	QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier, QPoint(20, editor->fontMetrics().lineSpacing() + 2));
	settle();
	check(editor->textCursor().blockNumber() == 22, "A click on a pinned line should move the caret to it.");
	// Where the blocks close under the strip, the text below it is outside
	// them, so nothing is pinned.
	editor->verticalScrollBar()->setValue(84);
	settle();
	check(editor->stickyHeaderLines().isEmpty(), "Blocks that close under the strip should not be pinned over the text after them.");
	editor->verticalScrollBar()->setValue(39);
	settle();
	trigger(shell, "code.stickyHeaders");
	check(editor->stickyHeaderLines().isEmpty() && strip->isHidden() && !action(shell, "code.stickyHeaders")->isChecked(),
		"Sticky Headers off should unpin them.");
	trigger(shell, "code.stickyHeaders");
	check(editor->stickyHeaderLines() == QVector<int> {1, 23} && action(shell, "code.stickyHeaders")->isChecked(), "Sticky Headers on should pin them again.");
	// Coming back to a tab keeps its view. Its caret, left above the view,
	// is not under the strip and is not brought out.
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(0)));
	settle();
	editor->verticalScrollBar()->setValue(60);
	settle();
	const QString other = QDir(fixtures.project).filePath(QStringLiteral("progs/sticky_other.qc"));
	writeFile(other, QByteArrayLiteral("void() other_fn = {};\n"));
	shell.openPathFromCommandLine(other);
	settle();
	shell.openPathFromCommandLine(path);
	settle();
	check(editor->verticalScrollBar()->value() == 60 && editor->textCursor().blockNumber() == 0,
		"Coming back to a tab should keep its view and caret, with the caret above the view left there.");
	closeAllCodeTabs(shell);
}

// Resting the caret on a name shades each use of it, as a whole word in its
// own case, once it is used more than once; a selection turns that off.
void checkUseHighlights(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkUseHighlights");
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	const QString path = QDir(fixtures.project).filePath(QStringLiteral("progs/uses_sample.qc"));
	writeFile(path, QByteArrayLiteral("float counter;\n"
					  "float counter_max;\n"
					  "void() tick = {\n"
					  "\tcounter = counter + 1;\n"
					  "\tCounter = 0;\n"
					  "\tlonely = 1;\n"
					  "};\n"));
	shell.openPathFromCommandLine(path);
	settle();
	auto* editor = dynamic_cast<StudioCodeEditor*>(child<QPlainTextEdit>(shell, "codeEditor"));
	check(editor != nullptr, "The uses sample should open in the code editor.");
	if (!editor) {
		return;
	}
	const auto caretAt = [editor](int line, int column) {
		QTextCursor cursor(editor->document()->findBlockByNumber(line - 1));
		cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
		editor->setTextCursor(cursor);
		settle();
	};
	caretAt(1, 8);
	const QVector<int> uses = editor->highlightedUses();
	bool allCounter = uses.size() == 3;
	for (const int position : uses) {
		QTextCursor word(editor->document());
		word.setPosition(position);
		word.setPosition(position + 7, QTextCursor::KeepAnchor);
		allCounter = allCounter && word.selectedText() == QStringLiteral("counter");
	}
	check(allCounter, "The caret on a name should shade its three uses, not counter_max or Counter.");
	caretAt(6, 3);
	check(editor->highlightedUses().isEmpty(), "A name used once should not be shaded.");
	caretAt(1, 8);
	QTextCursor selected = editor->textCursor();
	selected.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
	editor->setTextCursor(selected);
	settle();
	check(editor->highlightedUses().isEmpty(), "A selection should turn the shading off.");
	closeAllCodeTabs(shell);
}

// The Code page's breadcrumb names the open file's folders from the project
// root, the file, and the symbol holding the caret; a folder opens a menu of
// its files, and the symbol opens Go to Symbol.
void checkCodeBreadcrumb(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkCodeBreadcrumb");
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	const QString sample = QDir(fixtures.project).filePath(QStringLiteral("progs/crumb_sample.qc"));
	const QString other = QDir(fixtures.project).filePath(QStringLiteral("progs/crumb_other.qc"));
	writeFile(sample, QByteArrayLiteral("// crumbs\nvoid() first_fn = {\n\tself.frame = 1;\n};\nvoid() second_fn = {\n\tself.frame = 2;\n};\n"));
	writeFile(other, QByteArrayLiteral("void() other_fn = {\n};\n"));
	shell.openPathFromCommandLine(sample);
	settle();
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	auto* bar = child<QWidget>(shell, "codeBreadcrumb");
	check(editor && bar, "The Code page should have its editor and breadcrumb.");
	if (!editor || !bar) {
		return;
	}
	const auto crumbs = [bar]() {
		QStringList texts;
		for (QToolButton* crumb : bar->findChildren<QToolButton*>(QStringLiteral("breadcrumbButton"))) {
			if (!crumb->isHidden()) {
				texts << crumb->text();
			}
		}
		return texts;
	};
	const auto crumbNamed = [bar](const QString& text) -> QToolButton* {
		for (QToolButton* crumb : bar->findChildren<QToolButton*>(QStringLiteral("breadcrumbButton"))) {
			if (!crumb->isHidden() && crumb->text() == text) {
				return crumb;
			}
		}
		return nullptr;
	};
	const auto caretTo = [editor](int line) {
		editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(line - 1)));
		settle();
	};
	// The Files tree reads queries too: ext=qc keeps the QuakeC files and
	// their folder.
	if (auto* treeFilter = child<QLineEdit>(shell, "codeTreeFilter")) {
		auto* tree = child<QTreeWidget>(shell, "codeTree");
		treeFilter->setText(QStringLiteral("name:crumb_ ext=qc"));
		settle();
		QStringList keptFiles;
		for (QTreeWidgetItemIterator it(tree); tree && *it; ++it) {
			if (!(*it)->isHidden() && !(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
				keptFiles << (*it)->text(0);
			}
		}
		keptFiles.sort();
		check(keptFiles == QStringList {QStringLiteral("crumb_other.qc"), QStringLiteral("crumb_sample.qc")},
			"name:crumb_ ext=qc should keep the two crumb files in the Files tree.");
		treeFilter->setText(QStringLiteral("language=shader name:crumb_"));
		settle();
		bool anyFile = false;
		for (QTreeWidgetItemIterator it(tree); tree && *it; ++it) {
			anyFile = anyFile || (!(*it)->isHidden() && !(*it)->data(0, Qt::UserRole).toString().isEmpty());
		}
		check(!anyFile, "language=shader should keep no QuakeC file.");
		// Words beside the terms search the path, as they do alone, so a
		// folder's name keeps the files in it.
		treeFilter->setText(QStringLiteral("progs name:crumb_"));
		settle();
		QStringList inProgs;
		for (QTreeWidgetItemIterator it(tree); tree && *it; ++it) {
			if (!(*it)->isHidden() && !(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
				inProgs << (*it)->text(0);
			}
		}
		inProgs.sort();
		check(inProgs == QStringList {QStringLiteral("crumb_other.qc"), QStringLiteral("crumb_sample.qc")},
			"A folder's name beside a query term should keep the files in that folder.");
		treeFilter->clear();
		settle();
	}
	const QString project = QFileInfo(fixtures.project).fileName();
	caretTo(3);
	check(bar->isVisible() && crumbs() == QStringList {project, QStringLiteral("progs"), QStringLiteral("crumb_sample.qc"), QStringLiteral("first_fn")},
		"The breadcrumb should name the project, the folder, the file, and the function holding the caret.");
	caretTo(6);
	check(crumbs().value(3) == QStringLiteral("second_fn"), "Moving the caret into another function should name it.");
	caretTo(1);
	check(crumbs().size() == 3 && crumbs().value(2) == QStringLiteral("crumb_sample.qc"), "Outside every function, the file should end the breadcrumb.");
	// A folder's menu lists its files; choosing one opens it.
	if (QToolButton* folder = crumbNamed(QStringLiteral("progs"))) {
		bool listed = false;
		whenMenuShows([&listed](QMenu* menu) {
			QAction* choice = nullptr;
			for (QAction* entry : menu->actions()) {
				listed = listed || entry->text() == QStringLiteral("crumb_sample.qc");
				if (entry->text() == QStringLiteral("crumb_other.qc")) {
					choice = entry;
				}
			}
			if (choice) {
				chooseMenuAction(menu, choice);
			} else {
				menu->close();
			}
		});
		folder->click();
		settle();
		auto* tabs = child<QTabBar>(shell, "codeTabs");
		check(listed && tabs && tabs->currentIndex() == codeTabFor(tabs, other), "A folder crumb should list the folder's files and open the one chosen.");
	} else {
		check(false, "The folder crumb should be there.");
	}
	// The symbol crumb opens Go to Symbol.
	shell.openPathFromCommandLine(sample);
	settle();
	caretTo(3);
	if (QToolButton* symbol = crumbNamed(QStringLiteral("first_fn"))) {
		symbol->click();
		settle();
		auto* picker = shell.findChild<QDialog*>(QStringLiteral("symbolPicker"));
		check(picker && picker->isVisible(), "The symbol crumb should open Go to Symbol.");
		if (picker) {
			picker->reject();
			settle();
		}
	} else {
		check(false, "The symbol crumb should be there.");
	}
	// A file saved bigger answers a size query by its new size.
	if (auto* treeFilter = child<QLineEdit>(shell, "codeTreeFilter")) {
		auto* tree = child<QTreeWidget>(shell, "codeTree");
		const auto sampleKept = [tree]() {
			for (QTreeWidgetItemIterator it(tree); tree && *it; ++it) {
				if ((*it)->text(0) == QStringLiteral("crumb_sample.qc")) {
					return !(*it)->isHidden();
				}
			}
			return false;
		};
		treeFilter->setText(QStringLiteral("name:crumb_sample size>2kb"));
		settle();
		check(!sampleKept(), "A small file should not pass size>2kb.");
		QTextCursor end(editor->document());
		end.movePosition(QTextCursor::End);
		end.insertText(QStringLiteral("// ") + QString(3000, QLatin1Char('x')) + QLatin1Char('\n'));
		trigger(shell, "code.save");
		settle();
		treeFilter->setText(QStringLiteral("name:crumb_sample  size>2kb"));
		settle();
		check(sampleKept(), "Saved past 2 kB, the file should pass size>2kb when the query is next applied.");
		treeFilter->clear();
		settle();
	}
	closeAllCodeTabs(shell);
	check(!bar->isVisible(), "With no file open, the breadcrumb should step aside.");
}

// F8 and Shift+F8 step through the open file's problems, wrapping at either
// end, keep the diagnostics list's row in step, and say what each reports.
void checkNextProblem(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkNextProblem");
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	const QString path = QDir(fixtures.project).filePath(QStringLiteral("progs/problem_sample.qc"));
	writeFile(path, QByteArrayLiteral("void() first = {\n"
					  "\tlocal string s = \"open;\n"
					  "};\n"
					  "// WARNING marker here\n"
					  "void() second = {\n"
					  "\tself.message = \"open again;\n"
					  "};\n"));
	shell.openPathFromCommandLine(path);
	settle();
	auto* editor = child<QPlainTextEdit>(shell, "codeEditor");
	auto* diagnostics = child<QListWidget>(shell, "codeDiagnostics");
	check(editor && diagnostics, "The Code page should have its editor and diagnostics.");
	if (!editor || !diagnostics) {
		return;
	}
	editor->setFocus();
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(0)));
	settle();
	const auto caretLine = [editor]() {
		return editor->textCursor().blockNumber() + 1;
	};
	// The readout counts them; a click on the count goes to the next, as F8.
	auto* count = child<QToolButton>(shell, "codeProblems");
	check(count && count->isVisible() && count->text() == QStringLiteral("3 problem(s)"), "The readout should count the file's problems.");
	if (count) {
		count->click();
		settle();
		check(caretLine() == 2, "A click on the count should go to the first problem.");
		editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(0)));
		settle();
	}
	press(shell, Qt::Key_F8);
	check(caretLine() == 2 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Problem 1 of 3: line 2:"))
			&& diagnostics->currentItem() && diagnostics->currentItem()->data(Qt::UserRole).toInt() == 2,
		"F8 should go to the first problem after the caret, say which it is, and select its row.");
	press(shell, Qt::Key_F8);
	press(shell, Qt::Key_F8);
	check(caretLine() == 6, "F8 again should step through the problems in order.");
	press(shell, Qt::Key_F8);
	check(caretLine() == 2 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Problem 1 of 3")), "F8 on the last problem should wrap to the first.");
	press(shell, Qt::Key_F8, Qt::ShiftModifier);
	check(caretLine() == 6 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Problem 3 of 3")), "Shift+F8 on the first problem should wrap to the last.");
	press(shell, Qt::Key_F8, Qt::ShiftModifier);
	check(caretLine() == 4, "Shift+F8 should step back.");
	// A file without problems says so.
	const QString clean = QDir(fixtures.project).filePath(QStringLiteral("progs/problem_free.qc"));
	writeFile(clean, QByteArrayLiteral("void() fine = {\n};\n"));
	shell.openPathFromCommandLine(clean);
	settle();
	editor->setFocus();
	press(shell, Qt::Key_F8);
	check(shell.statusBar()->currentMessage() == QStringLiteral("No problems in this file."), "F8 in a file without problems should say so.");
	check(count && count->isHidden(), "A file without problems should show no count.");
	// A problem typed in joins the list a moment after typing stops.
	QTextCursor typing(editor->document()->findBlockByNumber(0));
	typing.movePosition(QTextCursor::EndOfBlock);
	editor->setTextCursor(typing);
	editor->insertPlainText(QStringLiteral("\n\tself.message = \"WARNING open;"));
	QTest::qWait(700);
	editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(0)));
	settle();
	press(shell, Qt::Key_F8);
	check(editor->textCursor().blockNumber() == 1 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Problem 1 of 2")),
		"F8 should find a problem typed in a moment ago.");
	// That line holds two problems: F8 steps to the second on the same line,
	// then wraps.
	press(shell, Qt::Key_F8);
	check(editor->textCursor().blockNumber() == 1 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Problem 2 of 2")),
		"F8 should step to a second problem on the same line.");
	press(shell, Qt::Key_F8);
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Problem 1 of 2")), "F8 should then wrap to the first.");
	editor->undo();
	settle();
	closeAllCodeTabs(shell);
}

// The Shaders filter reads property terms as a query over each shader,
// keeping a matching shader's whole tree of stages and textures.
void checkShaderQuery(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkShaderQuery");
	// Changes left staged in whatever package is open are let go.
	whenDialogShows([](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			for (QAbstractButton* button : box->buttons()) {
				if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
					button->click();
					return;
				}
			}
		}
	});
	shell.openPathFromCommandLine(fixtures.package);
	settle();
	shell.openPathFromCommandLine(fixtures.shader);
	settle();
	auto* graph = child<QTreeWidget>(shell, "shaderGraph");
	auto* filter = child<QLineEdit>(shell, "shaderFilter");
	check(graph && filter, "Shaders should have its stage graph and filter.");
	if (!graph || !filter) {
		return;
	}
	const auto shown = [graph]() {
		QStringList names;
		for (int index = 0; index < graph->topLevelItemCount(); ++index) {
			const QTreeWidgetItem* item = graph->topLevelItem(index);
			if (!item->isHidden() && !item->data(0, Qt::UserRole).toString().isEmpty()) {
				names << item->data(0, Qt::UserRole).toString();
			}
		}
		names.sort();
		return names;
	};
	// A word matching nothing hides every row, stages included, first.
	filter->setText(QStringLiteral("nothing_matches_this"));
	settle();
	filter->setText(QStringLiteral("missing>0"));
	settle();
	check(shown() == QStringList {QStringLiteral("textures/test/missing")}, "missing>0 should keep the shader whose texture is not in the package.");
	QTreeWidgetItem* missingShader = nullptr;
	for (int index = 0; index < graph->topLevelItemCount(); ++index) {
		if (graph->topLevelItem(index)->data(0, Qt::UserRole).toString() == QStringLiteral("textures/test/missing")) {
			missingShader = graph->topLevelItem(index);
		}
	}
	check(missingShader && missingShader->childCount() > 0 && !missingShader->child(0)->isHidden(), "A matching shader should keep its stages in view.");
	filter->setText(QStringLiteral("qer_editorimage:brick"));
	settle();
	check(shown() == QStringList {QStringLiteral("textures/test/wall")}, "A directive should be a key: qer_editorimage:brick keeps the wall shader.");
	filter->setText(QStringLiteral("blend:gl_one stages=1"));
	settle();
	check(shown() == QStringList {QStringLiteral("textures/test/missing")}, "A stage's blend should be a key too.");
	filter->clear();
	settle();
	check(shown().size() == 2, "Clearing the filter should bring both shaders back.");
}

// The Packages filter reads property terms as a query over every entry, as
// the Levels Objects filter does over map objects.
void checkPackageQuery(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkPackageQuery");
	shell.openPathFromCommandLine(fixtures.package);
	settle();
	auto* filter = child<QLineEdit>(shell, "packageFilter");
	auto entries = tests::PackageRows(child<PackageEntryView>(shell, "packageEntries"));
	check(filter && entries, "Packages should have its filter and entry list.");
	if (!filter || !entries) {
		return;
	}
	const auto listed = [entries]() {
		QStringList paths;
		for (int row = 0; row < entries->count(); ++row) {
			const auto* item = entries->item(row);
			if (!item->isHidden() && item->flags().testFlag(Qt::ItemIsSelectable)) {
				paths << item->data(Qt::UserRole).toString();
			}
		}
		paths.sort();
		return paths;
	};
	filter->setText(QStringLiteral("ext=wav"));
	settle();
	check(listed() == QStringList {QStringLiteral("sound/ambience/hum.wav")}, "ext=wav should list only the package's sound.");
	filter->setText(QStringLiteral("extension=wav"));
	settle();
	check(listed().isEmpty() && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Nothing here has extension")),
		"A key no entry has should be named in the status bar.");
	filter->setText(QStringLiteral("ext=tga size>1kb folder:textures"));
	settle();
	check(listed() == QStringList {QStringLiteral("textures/base/brick_wall.tga"), QStringLiteral("textures/base/metal_panel.tga")},
		"ext=, size> and folder: together should list the texture images only.");
	// Enter selects every entry kept and hands focus to the list.
	filter->setFocus(Qt::OtherFocusReason);
	QTest::keyClick(filter, Qt::Key_Return);
	settle();
	QStringList selected;
	for (const auto* item : entries->selectedItems()) {
		selected << item->data(Qt::UserRole).toString();
	}
	selected.sort();
	check(selected == listed() && selected.size() == 2 && QApplication::focusWidget() == entries
			&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("2 matching")),
		"Enter in the filter should select every entry it keeps and move focus to the list.");
	filter->setText(QStringLiteral("brick"));
	settle();
	check(listed() == QStringList {QStringLiteral("textures/base/brick_wall.tga")}, "A plain word should still search paths.");
	filter->clear();
	settle();

	// The Textures filter adds what decoding finds: w, h, and format, filled
	// in as the thumbnails are made.
	trigger(shell, "shell.mode.textures");
	auto* textureFilter = child<QLineEdit>(shell, "textureFilter");
	auto* textures = child<QListWidget>(shell, "textureEntries");
	check(textureFilter && textures, "Textures should have its filter and list.");
	if (!textureFilter || !textures) {
		return;
	}
	const auto images = [textures]() {
		QStringList paths;
		for (int row = 0; row < textures->count(); ++row) {
			const QListWidgetItem* item = textures->item(row);
			if (!item->isHidden() && !item->data(Qt::UserRole).toString().isEmpty()) {
				paths << item->data(Qt::UserRole).toString();
			}
		}
		paths.sort();
		return paths;
	};
	textureFilter->setText(QStringLiteral("w=32 h=32 folder:textures"));
	check(waitFor([&images]() { return images().size() == 2; }), "w=32 h=32 should keep the two 32-pixel texture images once decoded.");
	textureFilter->setText(QStringLiteral("w>32"));
	settle();
	check(images().isEmpty(), "w>32 should keep none of them.");
	// Models and Sounds read the same queries over their entries.
	const auto kept = [&shell](const char* listName) {
		QStringList paths;
		if (auto* list = child<QListWidget>(shell, listName)) {
			for (int row = 0; row < list->count(); ++row) {
				const QListWidgetItem* item = list->item(row);
				if (!item->isHidden() && item->flags().testFlag(Qt::ItemIsSelectable)) {
					paths << item->data(Qt::UserRole).toString();
				}
			}
		}
		return paths;
	};
	trigger(shell, "shell.mode.models");
	if (auto* modelFilter = child<QLineEdit>(shell, "modelFilter")) {
		modelFilter->setText(QStringLiteral("ext=md3 folder:models"));
		settle();
		check(kept("modelEntries") == QStringList {QStringLiteral("models/crate/crate.md3")}, "ext=md3 should keep the crate model.");
		modelFilter->setText(QStringLiteral("ext=md3 size>1mb"));
		settle();
		check(kept("modelEntries").isEmpty(), "size>1mb should keep no model.");
		modelFilter->clear();
	}
	trigger(shell, "shell.mode.audio");
	if (auto* audioFilter = child<QLineEdit>(shell, "audioFilter")) {
		audioFilter->setText(QStringLiteral("ext=wav size>1kb"));
		settle();
		check(kept("audioEntries") == QStringList {QStringLiteral("sound/ambience/hum.wav")}, "ext=wav size>1kb should keep the hum.");
		audioFilter->setText(QStringLiteral("ext=wav size<1kb"));
		settle();
		check(kept("audioEntries").isEmpty(), "size<1kb should keep no sound.");
		audioFilter->clear();
	}
	trigger(shell, "shell.mode.textures");
	// Typed before a package's images are decoded, the filter keeps them as
	// their thumbnails are made.
	const QString fresh = QDir(fixtures.project).filePath(QStringLiteral("fresh.pk3"));
	const auto flat = [](int, int) {
		return qRgb(90, 90, 90);
	};
	buildStoredZip(fresh, {{QStringLiteral("textures/wide.tga"), buildTarga(64, 32, flat)}, {QStringLiteral("textures/small.tga"), buildTarga(16, 16, flat)}});
	textureFilter->setText(QStringLiteral("w=64"));
	shell.openPathFromCommandLine(fresh);
	check(waitFor([&images]() { return images() == QStringList {QStringLiteral("textures/wide.tga")}; }),
		"A filter on width should keep an image once its thumbnail is made.");
	textureFilter->clear();
	settle();
}

// The Levels Objects filter reads property terms as a query, keeps plain
// words as a name filter, and Enter selects every object it keeps.
void checkObjectQuery(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkObjectQuery");
	// Edits left on whatever map is open are let go.
	whenDialogShows([](QWidget* modal) {
		if (auto* box = qobject_cast<QMessageBox*>(modal)) {
			for (QAbstractButton* button : box->buttons()) {
				if (box->buttonRole(button) == QMessageBox::DestructiveRole) {
					button->click();
					return;
				}
			}
		}
	});
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* filter = child<QLineEdit>(shell, "levelObjectFilter");
	check(objects && filter, "Levels should have its objects list and filter.");
	if (!objects || !filter) {
		return;
	}
	const auto kept = [objects](bool selectedOnly) {
		QStringList selectors;
		for (int row = 0; row < objects->model()->rowCount(); ++row) {
			const QModelIndex item = objectIndex(objects, row);
			if (!objects->isRowHidden(row) && item.flags().testFlag(Qt::ItemIsSelectable) && (!selectedOnly || objects->selectionModel()->isSelected(item))) {
				selectors << item.data(Qt::UserRole).toString();
			}
		}
		return selectors;
	};
	const auto query = [filter, objects](const QString& text) {
		filter->setText(text);
		QElapsedTimer timer; timer.start();
		do { settle(); } while (objects->isFiltering() && timer.elapsed() < 10000);
	};
	query(QStringLiteral("class=light"));
	check(kept(false) == QStringList {QStringLiteral("entity:2")}, "class=light should keep only the light.");
	// The Objects tab counts what the filter keeps.
	QTabWidget* outliner = nullptr;
	for (QWidget* parent = objects->parentWidget(); parent && !outliner; parent = parent->parentWidget()) {
		outliner = qobject_cast<QTabWidget*>(parent);
	}
	const auto objectsTabText = [outliner, objects]() {
		for (int index = 0; outliner && index < outliner->count(); ++index) {
			if (outliner->widget(index)->isAncestorOf(objects)) {
				return outliner->tabBar()->tabData(index).toMap().value(QStringLiteral("text"), outliner->tabText(index)).toString();
			}
		}
		return QString();
	};
	check(objectsTabText() == QStringLiteral("Objects (1 of 5)"), "The Objects tab should count what the filter keeps.");
	query(QStringLiteral("origin:24"));
	check(kept(false) == QStringList {QStringLiteral("entity:1"), QStringLiteral("entity:3")}, "origin:24 should keep the two objects standing at 24.");
	query(QStringLiteral("light>200"));
	check(kept(false) == QStringList {QStringLiteral("entity:2")}, "light>200 should compare the light's number.");
	query(QStringLiteral("texture=base/floor"));
	check(kept(false) == QStringList {QStringLiteral("brush:0")}, "texture= should keep the brush with that texture on a face.");
	query(QStringLiteral("entity:3"));
	check(kept(false) == QStringList {QStringLiteral("entity:3")}, "entity:3, as map find prints it, should keep that object.");
	query(QStringLiteral("claass=light"));
	check(kept(false).isEmpty() && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Nothing here has claass")),
		"A key no object has should be named in the status bar.");
	query(QString());
	shell.statusBar()->showMessage(QStringLiteral("Something else"));
	query(QStringLiteral("claass=light"));
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Nothing here has claass")),
		"The same slip typed again after clearing the filter should be named again.");
	query(QStringLiteral("player"));
	check(kept(false) == QStringList {QStringLiteral("entity:1")}, "A plain word should still narrow the list by name.");
	// Enter selects every object kept, and the view's selection follows.
	query(QStringLiteral("origin:24"));
	filter->setFocus(Qt::OtherFocusReason);
	QTest::keyClick(filter, Qt::Key_Return);
	settle();
	check(kept(true) == QStringList {QStringLiteral("entity:1"), QStringLiteral("entity:3")}
			&& shell.statusBar()->currentMessage() == QStringLiteral("2 matching object(s) selected."),
		"Enter in the filter should select every object it keeps, and say how many.");
	if (auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"))) {
		QStringList inView;
		for (const LevelMapSelectionRef& ref : view->selectionSet()) {
			inView << levelMapSelectionRefId(ref);
		}
		inView.sort();
		check(inView == QStringList {QStringLiteral("entity:1"), QStringLiteral("entity:3")}, "The map view should select the same objects.");
		// The count lives in the view's corner, and the message about it goes
		// once the selection changes: it would describe a selection that is gone.
		check(view->hudTags().value(1).startsWith(QStringLiteral("2 selected")), "The map view's corner should count the selection.");
		objects->clearSelection();
		settle();
		check(shell.statusBar()->currentMessage().isEmpty() && !view->hudTags().value(1).contains(QStringLiteral("selected")),
			"Clearing the selection should take down the message that counted it, and the corner count with it.");
	}
	// An object hidden in the view is left out, as Select All leaves it.
	for (int row = 0; row < objects->model()->rowCount(); ++row) {
		setObjectSelected(objects, row, objectIndex(objects, row).data(Qt::UserRole).toString() == QStringLiteral("entity:3"));
	}
	settle();
	trigger(shell, "map.hideSelection");
	query(QStringLiteral("origin:24"));
	trigger(shell, "map.selectMatching");
	check(kept(true) == QStringList {QStringLiteral("entity:1")}
			&& shell.statusBar()->currentMessage().contains(QStringLiteral("1 hidden one(s) left out")),
		"Select Matching should leave hidden objects out, and say so.");
	trigger(shell, "map.showAll");
	// An inspector row's menu finds the other objects with its value.
	query(QString());
	for (int row = 0; row < objects->model()->rowCount(); ++row) {
		setObjectSelected(objects, row, objectIndex(objects, row).data(Qt::UserRole).toString() == QStringLiteral("entity:2"));
	}
	settle();
	auto* inspector = child<QTreeWidget>(shell, "entityInspector");
	QTreeWidgetItem* lightRow = nullptr;
	for (QTreeWidgetItemIterator it(inspector); inspector && *it; ++it) {
		if ((*it)->data(0, Qt::UserRole).toString() == QStringLiteral("light")) {
			lightRow = *it;
		}
	}
	check(lightRow != nullptr, "The inspector should list the light's light key.");
	if (inspector && lightRow) {
		whenMenuShows([](QMenu* menu) {
			QAction* find = menuAction(menu, QStringLiteral("Find Objects With This Value"));
			if (find && find->isEnabled()) {
				chooseMenuAction(menu, find);
			} else {
				menu->close();
			}
		});
		// From another outliner tab: Find brings the Objects tab forward.
		if (outliner) {
			outliner->setCurrentIndex((outliner->indexOf(outliner->currentWidget()) + 1) % outliner->count());
		}
		emit inspector->customContextMenuRequested(inspector->visualItemRect(lightRow).center());
		settle();
		check(outliner && outliner->currentWidget()->isAncestorOf(objects), "Find Objects With This Value should bring the Objects tab forward.");
		// Offscreen, the window may not be active again once the menu has gone;
		// the focus asked for arrives when it is.
		shell.activateWindow();
		(void)QTest::qWaitForWindowActive(&shell, 2000);
		check(filter->text() == QStringLiteral("kind=entity light=300") && kept(false) == QStringList {QStringLiteral("entity:2")}
				&& waitFor([filter]() { return filter->hasFocus(); }),
			"Find Objects With This Value should filter the objects by light=300 and put focus in the filter.");
	}
	// A query used is offered again: Down in the empty field lists it.
	query(QString());
	filter->setFocus(Qt::OtherFocusReason);
	QTest::keyClick(filter, Qt::Key_Down);
	settle();
	QAbstractItemView* recent = filter->completer() ? filter->completer()->popup() : nullptr;
	bool offered = false;
	for (int row = 0; recent && row < recent->model()->rowCount(); ++row) {
		offered = offered || recent->model()->index(row, 0).data().toString() == QStringLiteral("origin:24");
	}
	check(recent && recent->isVisible() && offered, "Down in the empty filter should list the queries used before.");
	if (recent) {
		recent->hide();
	}
	// With nothing kept, the selection stays as it was.
	for (int row = 0; row < objects->model()->rowCount(); ++row) {
		setObjectSelected(objects, row, objectIndex(objects, row).data(Qt::UserRole).toString() == QStringLiteral("entity:2"));
	}
	settle();
	query(QStringLiteral("class=nothing_like_this"));
	trigger(shell, "map.selectMatching");
	auto* mapView = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	check(shell.statusBar()->currentMessage() == QStringLiteral("No object matches the filter.") && mapView && mapView->selectionSet().size() == 1
			&& levelMapSelectionRefId(mapView->selectionSet().first()) == QStringLiteral("entity:2"),
		"Select Matching with nothing kept should say so and leave the selection alone.");
	// A key nothing has is named once for the query typed, not again when an
	// edit refreshes the list under it and says what it did.
	query(QStringLiteral("claass=light"));
	check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Nothing here has claass")), "The unknown key should be named as it is typed.");
	QTreeWidgetItem* lightValue = nullptr;
	if (auto* inspector = child<QTreeWidget>(shell, "entityInspector")) {
		for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
			if ((*it)->data(0, Qt::UserRole).toString() == QStringLiteral("light")) {
				lightValue = *it;
			}
		}
	}
	check(lightValue != nullptr, "The selected light should be in the inspector.");
	if (lightValue) {
		lightValue->setText(1, QStringLiteral("250"));
		settle();
		check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("light set to 250")),
			"An edit's message should stay rather than give way to the filter's unknown key again.");
		trigger(shell, "map.undo");
	}
	// Another map's objects are queried, not the last map's.
	shell.openPathFromCommandLine(fixtures.doomWad);
	settle();
	query(QStringLiteral("type=1"));
	check(kept(false) == QStringList {QStringLiteral("thing:0")}, "A query after opening another map should read that map's objects.");
	// A Doom field row's menu queries by the field's own key.
	query(QString());
	for (int row = 0; row < objects->model()->rowCount(); ++row) {
		setObjectSelected(objects, row, objectIndex(objects, row).data(Qt::UserRole).toString() == QStringLiteral("sector:0"));
	}
	settle();
	if (auto* inspector = child<QTreeWidget>(shell, "entityInspector")) {
		QTreeWidgetItem* lightLevel = nullptr;
		for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
			if ((*it)->data(0, Qt::UserRole + 11).toString().endsWith(QStringLiteral(":lightlevel"))) {
				lightLevel = *it;
			}
		}
		check(lightLevel != nullptr, "The inspector should show the sector's light level.");
		if (lightLevel) {
			whenMenuShows([](QMenu* menu) {
				QAction* find = menuAction(menu, QStringLiteral("Find Objects With This Value"));
				if (find && find->isEnabled()) {
					chooseMenuAction(menu, find);
				} else {
					menu->close();
				}
			});
			emit inspector->customContextMenuRequested(inspector->visualItemRect(lightLevel).center());
			settle();
			check(filter->text() == QStringLiteral("kind=sector light=%1").arg(lightLevel->text(1)) && kept(false) == QStringList {QStringLiteral("sector:0")},
				"A sector's light level should query its kind and light, and keep only the sector.");
		}
	}
	filter->clear();
	settle();
	check(kept(false).size() == objects->model()->rowCount() && objectsTabText() == QStringLiteral("Objects"),
		"Clearing the filter should bring every object back, and the tab its plain name.");
}

// A page header folds its actions to their glyphs when its row runs short of
// room, the primary action last, keeping each label as the button's tooltip,
// and never asks for more width than the folded actions need.
void checkHeaderFolding()
{
	// Hosted, as a page hosts it: a window's layout would pin its minimum.
	auto* host = new QWidget;
	auto* hostLayout = new QVBoxLayout(host);
	hostLayout->setContentsMargins(0, 0, 0, 0);
	auto* header = new PageHeader(QStringLiteral("build"), QStringLiteral("Folding test"));
	hostLayout->addWidget(header);
	QPushButton* inspect = createButton(QStringLiteral("Inspect Artifacts"), QStringLiteral("info"));
	inspect->setToolTip(QStringLiteral("Look at what the last build wrote."));
	QPushButton* launch = createButton(QStringLiteral("Build and Launch"), QStringLiteral("gamepad"));
	QPushButton* run = createButton(QStringLiteral("Run Pipeline"), QStringLiteral("run"), QStringLiteral("primary"));
	for (QPushButton* button : {inspect, launch, run}) {
		header->addActionWidget(button);
	}
	host->resize(1400, 60);
	host->show();
	settle();
	check(inspect->text() == QStringLiteral("Inspect Artifacts") && run->text() == QStringLiteral("Run Pipeline"), "With room, every header action should show its label.");
	const int full = header->layout()->minimumSize().width();
	check(header->minimumSizeHint().width() < full, "The header should ask for no more width than its folded actions need.");
	// Just short of room: the secondary actions fold first.
	bool secondaryFolded = false;
	bool allFolded = false;
	for (int width = full - 1; width > 120 && !allFolded; width -= 10) {
		host->resize(width, 60);
		QCoreApplication::processEvents();
		if (inspect->text().isEmpty() && launch->text().isEmpty() && !run->text().isEmpty()) {
			secondaryFolded = true;
		}
		allFolded = run->text().isEmpty();
	}
	check(secondaryFolded, "Short of room, the secondary actions should fold to their glyphs while the primary keeps its label.");
	check(allFolded && inspect->toolTip().startsWith(QStringLiteral("Inspect Artifacts")) && inspect->toolTip().contains(QStringLiteral("what the last build wrote"))
			&& inspect->accessibleName() == QStringLiteral("Inspect Artifacts"),
		"Folded further, every action should show its glyph alone, its label kept as its tooltip and name.");
	host->resize(1400, 60);
	QCoreApplication::processEvents();
	check(inspect->text() == QStringLiteral("Inspect Artifacts") && inspect->toolTip() == QStringLiteral("Look at what the last build wrote.")
			&& run->text() == QStringLiteral("Run Pipeline"),
		"With room again, every label and tooltip should come back.");
	delete host;
}

// A panel's tab strip keeps its labels readable in the width it has: shrunk
// step by step it shows every label with its glyph, then every label alone,
// then only the current tab's, then glyphs alone, each label kept as the
// tab's tooltip and spoken name. Widths come from the fonts, so the order is
// what is checked, not where each step falls.
void checkPanelTabFitting()
{
	QTabWidget* tabs = createPanelTabs(QStringLiteral("Fitting test"));
	const QStringList names {QStringLiteral("Objects"), QStringLiteral("Create"), QStringLiteral("Statistics"), QStringLiteral("History"), QStringLiteral("Details")};
	const QStringList icons {QStringLiteral("list"), QStringLiteral("add"), QStringLiteral("report"), QStringLiteral("history"), QStringLiteral("details")};
	for (int index = 0; index < names.size(); ++index) {
		tabs->addTab(new QWidget, studioIcon(icons.at(index)), names.at(index));
	}
	tabs->setCurrentIndex(2);
	QTabBar* bar = tabs->tabBar();
	tabs->resize(1400, 200);
	tabs->show();
	settle();
	QStringList seen;
	bool consistent = true;
	for (int width = 1400; width >= 60; width -= 10) {
		tabs->resize(width, 200);
		QCoreApplication::processEvents();
		const QString mode = tabs->property("tabLabels").toString();
		if (seen.isEmpty() || seen.last() != mode) {
			seen << mode;
		}
		for (int index = 0; index < bar->count(); ++index) {
			const bool labelled = mode == QStringLiteral("all") || mode == QStringLiteral("text") || (mode == QStringLiteral("current") && index == 2);
			const bool glyph = mode != QStringLiteral("text");
			consistent = consistent && bar->tabText(index) == (labelled ? names.at(index) : QString())
				&& bar->tabIcon(index).isNull() != glyph && bar->accessibleTabName(index) == names.at(index)
				&& (labelled || bar->tabToolTip(index) == names.at(index));
		}
	}
	check(seen == QStringList {QStringLiteral("all"), QStringLiteral("text"), QStringLiteral("current"), QStringLiteral("icons")},
		"Shrinking a panel's tabs should go from labels with glyphs, to labels alone, to the current label, to glyphs alone.");
	check(consistent, "Each arrangement should show what it says, and keep every label as the tab's spoken name and, when hidden, its tooltip.");
	tabs->resize(1400, 200);
	QCoreApplication::processEvents();
	check(tabs->property("tabLabels").toString() == QStringLiteral("all") && bar->tabText(0) == names.first() && bar->tabToolTip(0).isEmpty(),
		"With room again, every label should come back.");
	setPanelTabText(tabs, 1, QStringLiteral("Create (2)"));
	check(bar->tabText(1) == QStringLiteral("Create (2)") && bar->accessibleTabName(1) == QStringLiteral("Create (2)"),
		"Relabelling a panel tab should show the new label.");
	delete tabs;
}

void checkPaletteRecents(ApplicationShell& shell)
{
	ensureActive(shell, "checkPaletteRecents");
	// A command run from the palette leads the list the next time it opens.
	check(trigger(shell, "shell.commandPalette"), "The command palette should open.");
	auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("commandPaletteFilter"));
	auto* list = shell.findChild<QListWidget*>(QStringLiteral("commandPaletteList"));
	if (!filter || !list) {
		check(false, "The palette should expose its filter and list.");
		return;
	}
	QTest::keyClicks(filter, QStringLiteral("Models"));
	settle();
	const QString chosen = list->currentItem() ? list->currentItem()->data(Qt::UserRole + 4).toString() : QString();
	check(chosen == QStringLiteral("shell.mode.models"), "Typing Models should highlight the Models surface command.");
	QTest::keyClick(filter, Qt::Key_Return);
	settle();
	check(currentPage(shell) == Models, "Enter should run the highlighted command.");

	ensureActive(shell, "checkPaletteRecents: reopen");
	trigger(shell, "shell.commandPalette");
	filter->clear();
	settle();
	check(list->count() > 0 && list->item(0)->data(Qt::UserRole + 4).toString() == QStringLiteral("shell.mode.models"),
		"The command just run should lead the palette when its filter is empty.");
	QTest::keyClick(filter, Qt::Key_Escape);
	settle();
}

// Stands in for qbsp when the test points the studio at its own program: it
// reports two warnings on lines of the input map, the way ericw-tools reports
// "WARNING: <line>: ...", and one naming no line, and writes a minimal valid
// BSP29 header where qbsp writes its output.
int runStandInCompiler(int argc, char** argv)
{
	QString mapPath;
	for (int index = 1; index < argc; ++index) {
		const QString argument = QString::fromLocal8Bit(argv[index]);
		if (argument.endsWith(QStringLiteral(".map"), Qt::CaseInsensitive)) {
			mapPath = argument;
		}
	}
	// Lines 3-10 of the fixture map are the worldspawn brush, 12-16 the player start.
	std::cout << "qbsp stand-in for " << mapPath.toStdString() << "\n";
	std::cout << "WARNING: 6: Brush plane has no normal\n";
	std::cout << "WARNING: 14: Entity info_player_start is inside a solid\n";
	std::cout << "WARNING: Texture axis perpendicular to face\n";
	// And the map leaks: qbsp names the entity the outside reached and writes
	// the trail from it into the void as one "x y z" line per point.
	std::cout << "Reached occupant \"info_player_start\" at (0 0 24), no filling performed.\n" << std::flush;
	const QFileInfo source(mapPath);
	writeFile(source.absoluteDir().filePath(source.completeBaseName() + QStringLiteral(".pts")),
		QByteArrayLiteral("0 0 24\n0 96 24\n0 200 24\n"));
	QByteArray header;
	appendI32(&header, 29);
	for (int lump = 0; lump < 15; ++lump) {
		appendLe32(&header, 124);
		appendLe32(&header, 0);
	}
	const QFileInfo map(mapPath);
	return writeFile(map.absoluteDir().filePath(map.completeBaseName() + QStringLiteral(".bsp")), header) ? 0 : 1;
}

// Activity rows reporting a file changed outside the studio, counted so a
// check can tell whether a step added one.
int outsideChangeRows(ApplicationShell& shell)
{
	int rows = 0;
	if (auto* activity = child<QListWidget>(shell, "activityTasks")) {
		for (int row = 0; row < activity->count(); ++row) {
			rows += activity->item(row)->text().contains(QStringLiteral("outside the studio")) ? 1 : 0;
		}
	}
	return rows;
}

void checkBuildLoop(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkBuildLoop");
	// A project is open, so the run registers its outputs and writes its
	// stage manifests into the project's output folder.
	shell.openPathFromCommandLine(fixtures.project);
	settle();
	const int outsideBefore = outsideChangeRows(shell);

	// The build input follows the map open in Levels.
	trigger(shell, "shell.mode.build");
	auto* input = child<QLineEdit>(shell, "buildPipelineInput");
	auto* pipeline = child<QComboBox>(shell, "buildPipelineChoice");
	auto* sections = child<QTabWidget>(shell, "buildSections");
	auto* run = child<QAbstractButton>(shell, "buildPipelineRun");
	auto* results = child<QTabWidget>(shell, "buildResultTabs");
	auto* problems = child<QListWidget>(shell, "buildProblems");
	auto* tools = child<QTreeWidget>(shell, "compilerTools");
	auto* locate = child<QAbstractButton>(shell, "compilerLocate");
	auto* automatic = child<QAbstractButton>(shell, "compilerUseAutomatic");
	check(input && pipeline && sections && run && results && problems && tools && locate && automatic,
		"The Build page should expose its input, pipeline, problems, and compiler tools.");
	if (!input || !pipeline || !sections || !run || !results || !problems || !tools || !locate || !automatic) {
		return;
	}
	check(sameFile(input->text(), fixtures.map), "The build input should follow the map open in Levels.");
	const int bspOnly = pipeline->findData(QStringLiteral("quake-bsp-only"));
	check(bspOnly >= 0, "The Quake BSP-only pipeline should be listed.");
	pipeline->setCurrentIndex(bspOnly);
	settle();

	// Before a run of this input, F4 says nothing has been built yet.
	if (problems->count() > 0 && !problems->item(0)->flags().testFlag(Qt::ItemIsEnabled)) {
		problems->setFocus();
		press(shell, Qt::Key_F4);
		check(shell.statusBar()->currentMessage().startsWith(QStringLiteral("Nothing has been built")),
			"F4 before any run of the input should say nothing has been built yet.");
	}

	// Locate points qbsp at this program, which stands in for it.
	sections->setCurrentIndex(1);
	settle();
	QTreeWidgetItem* qbsp = findTreeItem(tools, Qt::UserRole, QStringLiteral("ericw-qbsp"));
	check(qbsp != nullptr, "The tool list should include ericw-tools qbsp.");
	if (!qbsp) {
		return;
	}
	tools->setCurrentItem(qbsp);
	settle();
	const QString standIn = QCoreApplication::applicationFilePath();
	whenDialogShows([standIn](QWidget* dialog) {
		if (auto* files = qobject_cast<QFileDialog*>(dialog)) {
			files->selectFile(standIn);
			// QFileDialog::accept() is protected; QDialog's is the public entry.
			static_cast<QDialog*>(files)->accept();
		} else if (auto* other = qobject_cast<QDialog*>(dialog)) {
			other->reject();
		}
	});
	locate->click();
	settle();
	qbsp = findTreeItem(tools, Qt::UserRole, QStringLiteral("ericw-qbsp"));
	check(qbsp && qbsp->text(3) == QStringLiteral("Chosen path") && sameFile(qbsp->text(2), standIn),
		"Locate should make qbsp run the chosen program.");
	check(automatic->isEnabled(), "Use Automatic should be offered once a path is chosen.");
	snapshot(shell, "build-toolchain-located");

	// The run lists the compiler's warnings, and each one leads to its object.
	sections->setCurrentIndex(0);
	settle();
	check(run->isEnabled(), "Run Pipeline should be available with an input map.");
	run->click();
	auto located = [problems]() {
		int count = 0;
		for (int row = 0; row < problems->count(); ++row) {
			count += problems->item(row)->data(Qt::UserRole + 4).toString().isEmpty() ? 0 : 1;
		}
		return count;
	};
	QElapsedTimer clock;
	clock.start();
	while (clock.elapsed() < 30000 && (located() == 0 || !run->isEnabled())) {
		wait(50);
	}
	check(located() == 3, "The Problems list should show the three warnings the compiler printed.");
	// With a project open, each stage's manifest goes to the project's own
	// output folder, not to wherever the studio was started.
	check(QFileInfo::exists(QDir(fixtures.project).filePath(QStringLiteral("build/quake-bsp-only.qbsp.json"))),
		"A build's stage manifest should be written to the project's output folder.");
	// The run registered its outputs in the project manifest. That write is
	// the studio's own, so two beats of the watch timer later nothing may call
	// it a change made outside.
	{
		ProjectManifest written;
		check(loadProjectManifest(fixtures.project, &written) && !written.registeredOutputPaths.isEmpty(),
			"The run should register its outputs in the project manifest.");
		wait(2300);
		check(outsideChangeRows(shell) == outsideBefore && !shell.statusBar()->currentMessage().contains(QStringLiteral("external change")),
			"The studio's own manifest write after a build should not be reported as a change made outside.");
	}
	check(results->currentWidget() == problems, "A run with problems should bring the Problems list forward.");

	// A problem's menu copies the line as the compiler printed it, or all of them.
	const int warningRow = rowWithData(problems, Qt::UserRole + 3, 6);
	if (warningRow >= 0) {
		QGuiApplication::clipboard()->clear();
		whenMenuShows([](QMenu* menu) {
			QAction* copy = menuAction(menu, QStringLiteral("Copy Message"));
			if (copy && copy->isEnabled()) {
				chooseMenuAction(menu, copy);
			} else {
				menu->close();
			}
		});
		Q_EMIT problems->customContextMenuRequested(problems->visualItemRect(problems->item(warningRow)).center());
		settle();
		check(QGuiApplication::clipboard()->text().startsWith(QStringLiteral("WARNING: 6:")), "Copy Message should copy the compiler's own line.");
		whenMenuShows([](QMenu* menu) {
			QAction* copyAll = menuAction(menu, QStringLiteral("Copy All Problems"));
			if (copyAll && copyAll->isEnabled()) {
				chooseMenuAction(menu, copyAll);
			} else {
				menu->close();
			}
		});
		Q_EMIT problems->customContextMenuRequested(problems->visualItemRect(problems->item(warningRow)).center());
		settle();
		check(QGuiApplication::clipboard()->text().count(QLatin1Char('\n')) == 3, "Copy All Problems should copy the leak and the three warnings.");
	} else {
		check(false, "The Problems list should hold the line 6 warning.");
	}
	snapshot(shell, "build-problems");
	const QString bsp = QFileInfo(fixtures.map).absoluteDir().filePath(QStringLiteral("test.bsp"));
	check(QFileInfo::exists(bsp), "The stand-in compiler should have written the BSP.");
	// The leak leads the Problems list, and the build of the open map drew its
	// trail over the map without being asked.
	auto* mapView = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	const int leakRow = rowWithData(problems, Qt::UserRole + 5, QStringLiteral("leak"));
	check(leakRow == 0, "A leak should be the first problem listed.");
	check(mapView && mapView->hasLeakTrail(), "A leaking build of the open map should draw its leak trail.");
	if (leakRow >= 0 && mapView) {
		trigger(shell, "shell.mode.levels");
		mapView->zoomToFit();
		const double fitted = mapView->zoom();
		trigger(shell, "shell.mode.build");
		activateRow(problems, leakRow);
		check(currentPage(shell) == Levels && std::abs(mapView->zoom() - fitted) > 1e-6,
			"Activating the leak should frame its trail in Levels.");
		snapshot(shell, "build-leak-trail");
		auto* health = child<QListWidget>(shell, "levelMapHealth");
		check(health && rowWithData(health, Qt::UserRole, QStringLiteral("leak")) == 0, "Health should lead with the leak.");
		trigger(shell, "shell.mode.build");
	}

	// The built map can go straight into the open package for Save As.
	auto* addToPackage = child<QAbstractButton>(shell, "buildAddToPackage");
	auto staging = tests::StagingRows(child<PackageStagingView>(shell, "packageStagingSummary"));
	check(addToPackage && addToPackage->isEnabled(), "Add to Package should be available with a package open and a built map.");
	if (addToPackage && staging) {
		addToPackage->click();
		settle();
		bool stagedMap = false;
		for (int row = 0; row < staging->count(); ++row) {
			stagedMap = stagedMap || staging->item(row)->text().contains(QStringLiteral("maps/test.bsp"));
		}
		check(stagedMap && action(shell, "package.saveAs") && action(shell, "package.saveAs")->isEnabled(),
			"Add to Package should stage maps/test.bsp for Save As.");
		unstageAll(shell);
		check(!action(shell, "package.saveAs")->isEnabled(), "Unstaging should leave nothing to save.");
		trigger(shell, "shell.mode.build");
	}

	auto* reveal = child<QAbstractButton>(shell, "buildRevealOutput");
	check(reveal && reveal->isEnabled(), "Show Output should be available once the BSP exists.");
	auto* mapName = child<QLineEdit>(shell, "launchMapName");
	check(mapName && mapName->placeholderText().startsWith(QStringLiteral("test")), "An empty launch map name should default to the compiled map's name.");

	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	const int brushRow = rowWithData(problems, Qt::UserRole + 3, 6);
	const int entityRow = rowWithData(problems, Qt::UserRole + 3, 14);
	check(brushRow >= 0 && entityRow >= 0, "Each warning should keep the line it names.");
	if (objects && brushRow >= 0 && entityRow >= 0) {
		activateRow(problems, brushRow);
		check(currentPage(shell) == Levels && objects->currentIndex().isValid() && objects->currentIndex().data(Qt::UserRole).toString() == QStringLiteral("brush:0"),
			"A warning on a brush's line should select that brush in Levels.");
		snapshot(shell, "build-problem-brush");
		trigger(shell, "shell.mode.build");
		activateRow(problems, entityRow);
		check(currentPage(shell) == Levels && objects->currentIndex().isValid() && objects->currentIndex().data(Qt::UserRole).toString() == QStringLiteral("entity:1"),
			"A warning on an entity's line should select that entity in Levels.");
	}

	// Launch copies the build into the game's maps folder, asking once because
	// the installation is read-only, then starts the game: this program again,
	// which exits at once when it is given a map to load.
	ensureActive(shell, "checkBuildLoop: launch");
	trigger(shell, "shell.mode.build");
	sections->setCurrentIndex(2);
	settle();
	auto* deploy = child<QCheckBox>(shell, "launchDeploy");
	auto* launch = child<QAbstractButton>(shell, "launchGame");
	auto* summary = child<QTreeWidget>(shell, "launchSummary");
	check(deploy && deploy->isEnabled() && deploy->isChecked(), "A Quake launch should offer to copy the build into the game.");
	check(launch && launch->isEnabled(), "Launch should be available with a built map and an installation.");
	auto* copyCommand = child<QAbstractButton>(shell, "launchCopyCommand");
	check(copyCommand && copyCommand->isEnabled(), "Copy Command Line should be available for a runnable launch.");
	bool mentionsTarget = false;
	for (QTreeWidgetItemIterator it(summary); summary && *it; ++it) {
		mentionsTarget = mentionsTarget || (*it)->text(1).contains(QStringLiteral("read-only"));
	}
	check(mentionsTarget, "The launch plan should say that the read-only installation will be asked about.");
	snapshot(shell, "build-launch-tab");
	const QString deployed = QDir(fixtures.gameRoot).filePath(QStringLiteral("id1/maps/test.bsp"));
	if (launch) {
		auto allowed = std::make_shared<bool>(false);
		auto confirmed = std::make_shared<bool>(false);
		whenDialogShows([allowed, confirmed](QWidget* dialog) {
			*allowed = withoutMnemonic(dialog->windowTitle()) == QStringLiteral("Allow Test Maps");
			whenDialogShows([confirmed](QWidget* next) {
				*confirmed = true;
				clickButton(next, QStringLiteral("Launch"));
			});
			clickButton(dialog, QStringLiteral("Allow Test Maps"));
		});
		launch->click();
		settle();
		check(*allowed, "The first copy into a read-only installation should ask first.");
		check(*confirmed, "The launch should be confirmed with the copy listed.");
		check(QFileInfo::exists(deployed), "Launching should copy the built map into the game's maps folder.");
		StudioSettings settings;
		const QVector<GameInstallationProfile> installations = settings.gameInstallations();
		check(!installations.isEmpty() && !installations.first().readOnly, "Allowing test maps should be remembered on the installation.");
	}

	// The Workspace installation list shows the permission and can take it back.
	trigger(shell, "shell.mode.workspace");
	auto* testMaps = child<QCheckBox>(shell, "installTestMaps");
	check(testMaps && testMaps->isEnabled() && testMaps->isChecked(), "The installation list should show that test maps are allowed.");
	if (testMaps) {
		testMaps->click();
		settle();
		StudioSettings readOnlyAgain;
		check(!readOnlyAgain.gameInstallations().isEmpty() && readOnlyAgain.gameInstallations().first().readOnly,
			"Clearing Allow test maps should make the installation read-only again.");
		testMaps->click();
		settle();
	}
	trigger(shell, "shell.mode.build");

	// F5 builds, then launches the map it built; now only the launch is asked.
	ensureActive(shell, "checkBuildLoop: build and launch");
	check(child<QAbstractButton>(shell, "buildAndLaunch") != nullptr, "The Build page should offer Build and Launch.");
	auto launchedAgain = std::make_shared<bool>(false);
	whenDialogShows([launchedAgain](QWidget* dialog) {
		*launchedAgain = withoutMnemonic(dialog->windowTitle()) == QStringLiteral("Launch Game");
		clickButton(dialog, QStringLiteral("Launch"));
	}, 3000);
	press(shell, Qt::Key_F5);
	QElapsedTimer launchClock;
	launchClock.start();
	while (launchClock.elapsed() < 30000 && !*launchedAgain) {
		wait(50);
	}
	check(*launchedAgain, "Build and Launch should build and then ask to launch the game.");

	// The game folder is remembered for the installation, so a mod's folder is
	// typed once.
	if (auto* gameFolder = child<QLineEdit>(shell, "launchGameDirectory")) {
		gameFolder->setText(QStringLiteral("mymod"));
		Q_EMIT gameFolder->editingFinished();
		settle();
		StudioSettings settings;
		const QVector<GameInstallationProfile> installations = settings.gameInstallations();
		check(!installations.isEmpty() && settings.launchGameDirectory(installations.first().id) == QStringLiteral("mymod"),
			"The launch game folder should be remembered for the installation.");
		gameFolder->clear();
		Q_EMIT gameFolder->editingFinished();
		settle();
	}

	// With unsaved map edits the build asks first; Cancel starts nothing.
	auto* viewport = child<QWidget>(shell, "mapViewport");
	if (viewport) {
		// The file dialog above can leave no window active, and keys need one.
		ensureActive(shell, "checkBuildLoop: unsaved edits");
		trigger(shell, "shell.mode.levels");
		viewport->setFocus();
		press(shell, Qt::Key_Right);
		check(action(shell, "map.undo") && action(shell, "map.undo")->isEnabled(), "Nudging the selection should leave an unsaved edit.");
		trigger(shell, "shell.mode.build");
		whenDialogShows([](QWidget* dialog) {
			clickButton(dialog, QStringLiteral("Cancel"));
		});
		run->click();
		settle();
		check(run->isEnabled(), "Cancelling the unsaved-edits prompt should not start a build.");
		trigger(shell, "map.undo");
	}

	// Clear Leak Trail takes the trail away again.
	if (auto* mapView2 = shell.findChild<MapViewport*>(QStringLiteral("mapViewport")); mapView2 && mapView2->hasLeakTrail()) {
		trigger(shell, "map.clearLeakTrail");
		check(!mapView2->hasLeakTrail(), "Clear Leak Trail should remove the trail.");
	}

	// Use Automatic forgets the chosen path.
	sections->setCurrentIndex(1);
	settle();
	qbsp = findTreeItem(tools, Qt::UserRole, QStringLiteral("ericw-qbsp"));
	if (qbsp) {
		tools->setCurrentItem(qbsp);
		settle();
		automatic->click();
		settle();
		qbsp = findTreeItem(tools, Qt::UserRole, QStringLiteral("ericw-qbsp"));
		check(qbsp && qbsp->text(3) != QStringLiteral("Chosen path"), "Use Automatic should forget the chosen path.");
	}

	// F4 steps through the last build's problems from any page, wrapping, and
	// Shift+F4 back; each is shown where it points.
	int problemRows = 0;
	for (int row = 0; row < problems->count(); ++row) {
		problemRows += problems->item(row)->flags().testFlag(Qt::ItemIsSelectable) ? 1 : 0;
	}
	ensureActive(shell, "checkBuildLoop: build problems");
	const int brushWarning = rowWithData(problems, Qt::UserRole + 3, 6);
	problems->setCurrentRow(-1);
	trigger(shell, "shell.mode.code");
	press(shell, Qt::Key_F4);
	check(problems->currentRow() == 0 && currentPage(shell) == Levels
			&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Build problem 1 of %1").arg(problemRows)),
		"F4 from another page should show the first build problem, the leak, in Levels and say which it is.");
	trigger(shell, "shell.mode.code");
	press(shell, Qt::Key_F4);
	check(brushWarning == 1 && problems->currentRow() == brushWarning && currentPage(shell) == Levels && objects && objects->currentIndex().isValid()
			&& objects->currentIndex().data(Qt::UserRole).toString() == QStringLiteral("brush:0")
			&& shell.statusBar()->currentMessage().startsWith(QStringLiteral("Build problem 2 of")),
		"F4 again should show the next build problem: the brush its warning names.");
	// A look at the Build page, which lists the problems again, loses no place.
	trigger(shell, "shell.mode.build");
	trigger(shell, "shell.mode.code");
	press(shell, Qt::Key_F4);
	check(problems->currentRow() == 2 && shell.statusBar()->currentMessage().startsWith(QStringLiteral("Build problem 3 of")),
		"F4 after a visit to the Build page should go on from the problem shown last.");
	// A problem naming no line is shown where it is: its stage's output, on
	// the Build page.
	auto* stages = child<QListWidget>(shell, "buildPipelineStages");
	press(shell, Qt::Key_F4);
	check(problemRows == 4 && problems->currentRow() == 3 && currentPage(shell) == Build && stages && results->currentWidget() == stages
			&& shell.statusBar()->currentMessage() == QStringLiteral("Build problem 4 of 4: Texture axis perpendicular to face"),
		"F4 to a problem naming no line should show its stage on the Build page, and name it without the compiler's prefix.");
	press(shell, Qt::Key_F4);
	check(problems->currentRow() == 0, "F4 past the last build problem should wrap to the first.");
	press(shell, Qt::Key_F4, Qt::ShiftModifier);
	check(problems->currentRow() == problemRows - 1, "Shift+F4 past the first build problem should wrap to the last.");
	problems->setCurrentRow(-1);
	press(shell, Qt::Key_F4, Qt::ShiftModifier);
	check(problems->currentRow() == problemRows - 1, "Shift+F4 with no build problem shown yet should start from the last.");

	// A focused combo box keeps F4 for opening its list.
	ensureActive(shell, "checkBuildLoop: combo box F4");
	trigger(shell, "shell.mode.build");
	sections->setCurrentIndex(0);
	settle();
	const int shownProblem = problems->currentRow();
	pipeline->setFocus();
	press(shell, Qt::Key_F4);
	check(pipeline->view()->isVisible() && problems->currentRow() == shownProblem && currentPage(shell) == Build,
		"F4 on a focused combo box should open its list rather than show a build problem.");
	pipeline->hidePopup();
	settle();
}

} // namespace

// The Level and Texture Generators: by the rules and from a picture with no
// AI, then through a fake text model and a fake Stable Diffusion web UI. The
// level opens in Levels as a new map, the texture is written into the
// project, and every control has an accessible name.
void checkGenerators(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkGenerators");
	const auto setAi = [](const std::function<void(AiAutomationPreferences&)>& change) {
		StudioSettings settings;
		AiAutomationPreferences preferences = settings.aiAutomationPreferences();
		change(preferences);
		settings.setAiAutomationPreferences(preferences);
		settings.sync();
	};
	{
		StudioSettings settings;
		settings.setCurrentProjectPath(fixtures.project);
		settings.sync();
	}
	setAi([](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = true;
	});
	// Controls a screen reader cannot name; a button's text names it.
	const auto unnamedControls = [](QWidget* root) {
		QStringList unnamed;
		for (QWidget* widget : root->findChildren<QWidget*>()) {
			const bool control = qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)
				|| qobject_cast<QLineEdit*>(widget) || qobject_cast<QPlainTextEdit*>(widget) || qobject_cast<QListWidget*>(widget);
			const bool inner = qobject_cast<QAbstractSpinBox*>(widget->parentWidget()) || qobject_cast<QComboBox*>(widget->parentWidget());
			const auto* button = qobject_cast<QAbstractButton*>(widget);
			if (control && !inner && widget->accessibleName().isEmpty() && (!button || button->text().isEmpty())) {
				unnamed << QStringLiteral("%1 %2").arg(QString::fromLatin1(widget->metaObject()->className()), widget->objectName());
			}
		}
		return unnamed;
	};

	// The Level Generator, by the rules.
	trigger(shell, "map.generate");
	settle();
	// The dialogs have no Q_OBJECT, so they are found as dialogs.
	auto* levelDialog = static_cast<LevelGenerationDialog*>(shell.findChild<QDialog*>(QStringLiteral("levelGenerationDialog")));
	check(levelDialog && levelDialog->isVisible(), "Generate Level should open the Level Generator.");
	if (!levelDialog) {
		return;
	}
	auto* prompt = child<QPlainTextEdit>(shell, "levelGenerationPrompt");
	auto* generate = child<QPushButton>(shell, "levelGenerationGenerate");
	auto* open = child<QPushButton>(shell, "levelGenerationOpen");
	auto* status = child<QLabel>(shell, "levelGenerationStatus");
	auto* preview = child<QLabel>(shell, "levelGenerationPreview");
	auto* modelPlanner = child<QRadioButton>(shell, "levelGenerationModelPlanner");
	auto* rulesPlanner = child<QRadioButton>(shell, "levelGenerationRulesPlanner");
	auto* plannerStatus = child<QLabel>(shell, "levelGenerationPlannerStatus");
	check(prompt && generate && open && status && preview && modelPlanner && rulesPlanner && plannerStatus, "The Level Generator should have its parts.");
	if (!prompt || !generate || !open || !status || !preview || !modelPlanner || !rulesPlanner || !plannerStatus) {
		return;
	}
	const QStringList unnamedLevel = unnamedControls(levelDialog);
	check(unnamedLevel.isEmpty(), qPrintable(QStringLiteral("Every Level Generator control should have an accessible name: %1").arg(unnamedLevel.join(QStringLiteral(", ")))));
	prompt->setPlainText(QStringLiteral("gothic castle with lava pits, 5 rooms, seed 12"));
	rulesPlanner->setChecked(true);
	prompt->setFocus(Qt::OtherFocusReason);
	QTest::keyClick(prompt, Qt::Key_Return, Qt::ControlModifier);
	check(QTest::qWaitFor([&] { return !levelDialog->busy() && open->isEnabled(); }, 30000), "Ctrl+Enter should build a level by the rules.");
	check(levelDialog->result().ok && levelDialog->result().layout.rooms.size() >= 4 && levelDialog->result().plan.planner == QStringLiteral("rules/v1")
			&& !preview->pixmap().isNull() && !preview->accessibleDescription().isEmpty(),
		"The level should be built, drawn in the preview, and described for screen readers.");
	snapshotWidget(levelDialog, "level-generator");
	open->click();
	settle();
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* mapPath = child<QLineEdit>(shell, "levelMapPath");
	check(currentPage(shell) == Levels && objects && objects->model()->rowCount() > 20 && mapPath && mapPath->text().isEmpty(),
		"Open in Levels should show the generated level as a new, unsaved map with its brushes and entities.");

	// AI-free mode keeps the text model from being asked.
	modelPlanner->setChecked(true);
	settle();
	check(plannerStatus->text().contains(QStringLiteral("AI-free")), "In AI-free mode the planner status should say why the model cannot plan.");
	generate->click();
	settle();
	check(status->text().contains(QStringLiteral("AI-free")) && levelDialog->result().plan.planner == QStringLiteral("rules/v1"),
		"In AI-free mode Generate should not ask the model.");

	// A local text model plans it, asked for structured output.
	FakeAiProvider provider;
	check(provider.listening(), "The fake provider should listen.");
	setAi([&provider](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = false;
		preferences.cloudConnectorsEnabled = false;
		preferences.preferredReasoningConnectorId.clear();
		preferences.preferredLocalConnectorId = QStringLiteral("local-offline");
		preferences.connectorModels.insert(QStringLiteral("local-offline"), QStringLiteral("plan-model"));
		preferences.connectorEndpoints.insert(QStringLiteral("local-offline"), provider.baseUrl(QStringLiteral("/v1")));
		preferences.connectorImageModels.clear();
		preferences.connectorImageEndpoints.insert(QStringLiteral("local-offline"), provider.baseUrl());
	});
	LevelGenerationSpec spec;
	spec.prompt = QStringLiteral("gothic castle with lava pits, 5 rooms, seed 12");
	QJsonObject plan = levelSemanticPlanJson(rulesLevelPlan(spec));
	plan.insert(QStringLiteral("title"), QStringLiteral("Fake Model Keep"));
	provider.answer(200, FakeAiProvider::openAiAnswer(QString::fromUtf8(QJsonDocument(plan).toJson(QJsonDocument::Compact))));
	levelDialog->refreshPlannerStatus();
	check(plannerStatus->text().contains(QStringLiteral("plan-model")), "The planner status should name the local model.");
	generate->click();
	check(QTest::qWaitFor([&] { return !levelDialog->busy() && levelDialog->result().plan.planner.startsWith(QStringLiteral("ai:local-offline")); }, 30000),
		"The text model should plan the level.");
	check(levelDialog->result().ok && levelDialog->result().plan.title == QStringLiteral("Fake Model Keep"), "The model's plan should be built, its title kept.");
	const QJsonObject sent = provider.exchanges().isEmpty() ? QJsonObject() : QJsonDocument::fromJson(provider.exchanges().last().body).object();
	check(sent.value(QStringLiteral("response_format")).toObject().value(QStringLiteral("type")).toString() == QStringLiteral("json_schema"),
		"The plan request should ask for structured output.");
	levelDialog->close();
	settle();
	// The generated level is still open, unsaved; later checks open other
	// maps, so it is discarded here rather than left to prompt them.
	whenDialogShows([](QWidget* dialog) {
		if (dialog->windowTitle() == QStringLiteral("Unsaved Map Edits")) {
			clickButton(dialog, QStringLiteral("Discard Edits"));
		}
	});
	shell.openPathFromCommandLine(fixtures.map);
	settle();
	check(mapPath && QFileInfo(mapPath->text()) == QFileInfo(fixtures.map), "Discarding the generated level should open the next map.");

	// The Texture Generator, from a picture: no AI.
	ensureActive(shell, "checkGenerators texture");
	trigger(shell, "texture.generate");
	settle();
	auto* textureDialog = static_cast<TextureGenerationDialog*>(shell.findChild<QDialog*>(QStringLiteral("textureGenerationDialog")));
	check(textureDialog && textureDialog->isVisible(), "Generate Texture should open the Texture Generator.");
	if (!textureDialog) {
		return;
	}
	auto* textureGame = child<QComboBox>(shell, "textureGenerationGame");
	auto* picture = child<QLineEdit>(shell, "textureGenerationPicture");
	auto* pictureSource = child<QRadioButton>(shell, "textureGenerationPictureSource");
	auto* modelSource = child<QRadioButton>(shell, "textureGenerationModelSource");
	auto* variantCount = child<QSpinBox>(shell, "textureGenerationVariantCount");
	auto* texturePrompt = child<QPlainTextEdit>(shell, "textureGenerationPrompt");
	auto* textureGenerate = child<QPushButton>(shell, "textureGenerationGenerate");
	auto* variants = child<QListWidget>(shell, "textureGenerationVariants");
	auto* save = child<QPushButton>(shell, "textureGenerationSave");
	check(textureGame && picture && pictureSource && modelSource && variantCount && texturePrompt && textureGenerate && variants && save,
		"The Texture Generator should have its parts.");
	if (!textureGame || !picture || !pictureSource || !modelSource || !variantCount || !texturePrompt || !textureGenerate || !variants || !save) {
		return;
	}
	const QStringList unnamedTexture = unnamedControls(textureDialog);
	check(unnamedTexture.isEmpty(), qPrintable(QStringLiteral("Every Texture Generator control should have an accessible name: %1").arg(unnamedTexture.join(QStringLiteral(", ")))));
	textureGame->setCurrentIndex(textureGame->findData(QStringLiteral("quake")));
	QImage photo(256, 256, QImage::Format_ARGB32);
	for (int y = 0; y < photo.height(); ++y) {
		for (int x = 0; x < photo.width(); ++x) {
			photo.setPixel(x, y, qRgb(90 + x / 4, 70 + y / 5, 60));
		}
	}
	const QString photoPath = QDir(fixtures.project).filePath(QStringLiteral("photo.png"));
	photo.save(photoPath);
	pictureSource->setChecked(true);
	picture->setText(photoPath);
	texturePrompt->setPlainText(QStringLiteral("riveted bronze plate"));
	textureGenerate->click();
	check(QTest::qWaitFor([&] { return !textureDialog->busy() && variants->count() == 1; }, 30000), "A picture should become one game-ready variant.");
	save->click();
	settle();
	check(QFileInfo::exists(QDir(fixtures.project).filePath(QStringLiteral("wads/vibestudio_generated.wad"))),
		"Save to Project should put the Quake texture into the project's generated WAD.");

	// A local Stable Diffusion web UI draws two variants, tiling on its side.
	QByteArray tile;
	{
		QBuffer buffer(&tile);
		buffer.open(QIODevice::WriteOnly);
		photo.scaled(512, 512).save(&buffer, "PNG");
	}
	const QString encoded = QString::fromLatin1(tile.toBase64());
	provider.clear();
	provider.answer(200, QStringLiteral(R"({"images":["%1","%1"],"info":"{\"all_seeds\":[3,4]}"})").arg(encoded).toUtf8());
	textureDialog->refreshSourceStatus();
	modelSource->setChecked(true);
	variantCount->setValue(2);
	textureGenerate->click();
	check(QTest::qWaitFor([&] { return !textureDialog->busy() && variants->count() == 2; }, 30000), "The local web UI should draw two variants.");
	snapshotWidget(textureDialog, "texture-generator");
	const QJsonObject drawn = provider.exchanges().isEmpty() ? QJsonObject() : QJsonDocument::fromJson(provider.exchanges().last().body).object();
	check(!provider.exchanges().isEmpty() && provider.exchanges().last().path == "/sdapi/v1/txt2img" && drawn.value(QStringLiteral("tiling")).toBool()
			&& drawn.value(QStringLiteral("batch_size")).toInt() == 2,
		"The texture request should go to the web UI's txt2img, tiling, for two pictures.");
	// A texture the open map is missing is made under the map's own name.
	auto* missing = child<QComboBox>(shell, "textureGenerationMissing");
	auto* textureName = child<QLineEdit>(shell, "textureGenerationName");
	check(missing && textureName && !missing->accessibleName().isEmpty(), "The Texture Generator should offer the open map's missing textures.");
	if (missing && textureName && missing->count() > 1) {
		const QString wanted = missing->itemData(1).toString();
		missing->setCurrentIndex(1);
		emit missing->activated(1);
		check(textureName->text() == wanted.section(QLatin1Char('/'), -1) && variantCount->value() == 1,
			"Choosing a missing texture should take its name and make one variant, so the name stays exact.");
		textureName->clear();
	}
	textureDialog->close();
	settle();
	setAi([](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = true;
		preferences.connectorModels.clear();
		preferences.connectorEndpoints.clear();
		preferences.connectorImageEndpoints.clear();
	});
}

void checkMapAiEdit(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkMapAiEdit");
	const auto setAi = [](const std::function<void(AiAutomationPreferences&)>& change) {
		StudioSettings settings;
		AiAutomationPreferences preferences = settings.aiAutomationPreferences();
		change(preferences);
		settings.setAiAutomationPreferences(preferences);
		settings.sync();
	};
	setAi([](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = true;
	});
	// A copy of the fixture map, so the edits never touch the shared one. An
	// unsaved map an earlier check left open is discarded.
	const QString path = QFileInfo(fixtures.map).dir().filePath(QStringLiteral("ai-edit.map"));
	QFile::remove(path);
	check(QFile::copy(fixtures.map, path), "The map to edit should be copied.");
	whenDialogShows([](QWidget* dialog) {
		if (dialog->windowTitle() == QStringLiteral("Unsaved Map Edits")) {
			clickButton(dialog, QStringLiteral("Discard Edits"));
		}
	});
	shell.openPathFromCommandLine(path);
	settle();
	trigger(shell, "shell.mode.levels");
	auto* objects = child<LevelObjectList>(shell, "levelMapObjects");
	auto* pathField = child<QLineEdit>(shell, "levelMapPath");
	check(objects && pathField && QFileInfo(pathField->text()).fileName() == QStringLiteral("ai-edit.map"), "The map to edit should be open in Levels.");
	if (!objects || !pathField) {
		return;
	}
	const int rows = objects->model()->rowCount();

	trigger(shell, "map.aiEdit");
	settle();
	// The dialog has no Q_OBJECT, so it is found as a dialog.
	auto* dialog = static_cast<LevelAiEditDialog*>(shell.findChild<QDialog*>(QStringLiteral("levelAiEditDialog")));
	check(dialog && dialog->isVisible(), "Edit Map with AI should open its dialog.");
	auto* instruction = child<QPlainTextEdit>(shell, "levelAiEditInstruction");
	auto* ask = child<QPushButton>(shell, "levelAiEditAsk");
	auto* apply = child<QPushButton>(shell, "levelAiEditApply");
	auto* status = child<QLabel>(shell, "levelAiEditStatus");
	auto* mapStatus = child<QLabel>(shell, "levelAiEditMapStatus");
	auto* connection = child<QLabel>(shell, "levelAiEditConnection");
	auto* actions = child<QTreeWidget>(shell, "levelAiEditActions");
	check(dialog && instruction && ask && apply && status && mapStatus && connection && actions, "Edit Map with AI should have its parts.");
	if (!dialog || !instruction || !ask || !apply || !status || !mapStatus || !connection || !actions) {
		return;
	}
	QStringList unnamed;
	for (QWidget* widget : dialog->findChildren<QWidget*>()) {
		const auto* button = qobject_cast<QAbstractButton*>(widget);
		const bool control = button || qobject_cast<QPlainTextEdit*>(widget) || qobject_cast<QTreeWidget*>(widget);
		if (control && widget->accessibleName().isEmpty() && (!button || button->text().isEmpty())) {
			unnamed << QStringLiteral("%1 %2").arg(QString::fromLatin1(widget->metaObject()->className()), widget->objectName());
		}
	}
	check(unnamed.isEmpty(), qPrintable(QStringLiteral("Every Edit Map with AI control should have an accessible name: %1").arg(unnamed.join(QStringLiteral(", ")))));
	check(mapStatus->text().contains(QStringLiteral("ai-edit.map")) && mapStatus->text().contains(QStringLiteral("entities: 4")),
		"The dialog should say which map it edits.");

	// AI-free mode: asking sends nothing and says why.
	check(connection->text().contains(QStringLiteral("AI-free")), "In AI-free mode the dialog should say the model cannot be asked.");
	instruction->setPlainText(QStringLiteral("Make the light brighter"));
	ask->click();
	settle();
	check(status->text().contains(QStringLiteral("AI-free")) && actions->topLevelItemCount() == 0, "In AI-free mode Ask should send nothing.");

	// A saved proposal needs no model: two ready edits, one blocked, and one
	// the reviewer unchecks with the keyboard.
	const auto edit = [](const QString& kind, const QStringList& targets) {
		return QJsonObject {{QStringLiteral("kind"), kind}, {QStringLiteral("reason"), QStringLiteral("Test.")}, {QStringLiteral("targets"), QJsonArray::fromStringList(targets)},
			{QStringLiteral("classname"), QString()}, {QStringLiteral("origin"), QJsonArray()}, {QStringLiteral("keys"), QJsonArray()}, {QStringLiteral("key"), QString()},
			{QStringLiteral("value"), QString()}, {QStringLiteral("mins"), QJsonArray()}, {QStringLiteral("maxs"), QJsonArray()}, {QStringLiteral("texture"), QString()},
			{QStringLiteral("delta"), QJsonArray()}};
	};
	QJsonObject brighter = edit(QStringLiteral("set-key"), {QStringLiteral("entity:2")});
	brighter.insert(QStringLiteral("key"), QStringLiteral("light"));
	brighter.insert(QStringLiteral("value"), QStringLiteral("600"));
	QJsonObject spawn = edit(QStringLiteral("add-entity"), {});
	spawn.insert(QStringLiteral("classname"), QStringLiteral("info_player_deathmatch"));
	spawn.insert(QStringLiteral("origin"), QJsonArray {32, -32, 24});
	QJsonObject box = edit(QStringLiteral("add-box"), {});
	box.insert(QStringLiteral("mins"), QJsonArray {-64, -64, -48});
	box.insert(QStringLiteral("maxs"), QJsonArray {64, 64, -32});
	box.insert(QStringLiteral("texture"), QStringLiteral("base/floor"));
	const QJsonObject saved {{QStringLiteral("summary"), QStringLiteral("Saved edits.")},
		{QStringLiteral("actions"), QJsonArray {brighter, spawn, edit(QStringLiteral("delete"), {QStringLiteral("entity:0")}), box}}};
	const QString proposalPath = QFileInfo(fixtures.map).dir().filePath(QStringLiteral("ai-edit-proposal.json"));
	writeFile(proposalPath, QJsonDocument(saved).toJson());
	check(dialog->loadProposal(proposalPath), "A saved proposal should load with no model.");
	check(actions->topLevelItemCount() == 4 && actions->topLevelItem(0)->checkState(0) == Qt::Checked && actions->topLevelItem(0)->text(1) == QStringLiteral("Ready")
			&& !(actions->topLevelItem(2)->flags() & Qt::ItemIsUserCheckable) && actions->topLevelItem(2)->text(1) == QStringLiteral("Blocked")
			&& actions->topLevelItem(2)->text(2).contains(QStringLiteral("worldspawn")),
		"The proposal should list ready edits checked and say in words why the blocked one cannot run.");
	actions->setFocus(Qt::OtherFocusReason);
	actions->setCurrentItem(actions->topLevelItem(3));
	QTest::keyClick(actions, Qt::Key_Space);
	check(actions->topLevelItem(3)->checkState(0) == Qt::Unchecked, "Space should uncheck the current edit.");
	check(apply->isEnabled(), "Apply Checked should be ready while an edit is checked.");
	apply->click();
	settle();
	check(status->text().startsWith(QStringLiteral("Applied 2 edit")) && objects->model()->rowCount() == rows + 1,
		"Apply Checked should make the two checked edits and leave the rest.");
	check(actions->topLevelItem(0)->text(1) == QStringLiteral("Applied") && actions->topLevelItem(3)->text(1) == QStringLiteral("Left out")
			&& actions->topLevelItem(2)->text(1) == QStringLiteral("Blocked") && !apply->isEnabled(),
		"Each edit should say what became of it, and a proposal applies once.");
	trigger(shell, "map.undo");
	trigger(shell, "map.undo");
	settle();
	check(objects->model()->rowCount() == rows, "Each applied edit should be its own undo step.");

	// A local text model proposes the edit, asked for the proposal schema.
	FakeAiProvider provider;
	check(provider.listening(), "The fake provider should listen.");
	setAi([&provider](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = false;
		preferences.cloudConnectorsEnabled = false;
		preferences.preferredReasoningConnectorId.clear();
		preferences.preferredLocalConnectorId = QStringLiteral("local-offline");
		preferences.connectorModels.insert(QStringLiteral("local-offline"), QStringLiteral("edit-model"));
		preferences.connectorEndpoints.insert(QStringLiteral("local-offline"), provider.baseUrl(QStringLiteral("/v1")));
	});
	brighter.insert(QStringLiteral("value"), QStringLiteral("750"));
	const QJsonObject answer {{QStringLiteral("summary"), QStringLiteral("Brighter.")}, {QStringLiteral("actions"), QJsonArray {brighter}}};
	provider.answer(200, FakeAiProvider::openAiAnswer(QString::fromUtf8(QJsonDocument(answer).toJson(QJsonDocument::Compact))));
	dialog->refreshStatus();
	check(connection->text().contains(QStringLiteral("edit-model")), "The dialog should name the local model.");
	instruction->setFocus(Qt::OtherFocusReason);
	QTest::keyClick(instruction, Qt::Key_Return, Qt::ControlModifier);
	check(QTest::qWaitFor([&] { return !dialog->busy() && actions->topLevelItemCount() == 1; }, 30000), "Ctrl+Enter should ask the model and list its edit.");
	const QByteArray sent = provider.exchanges().isEmpty() ? QByteArray() : provider.exchanges().last().body;
	check(sent.contains("map_edit_proposal") && sent.contains("entity:2 light") && sent.contains("Make the light brighter"),
		"The request should carry the instruction and the map summary, and ask for the proposal schema.");
	snapshotWidget(dialog, "map-ai-edit");
	apply->click();
	settle();
	check(status->text().startsWith(QStringLiteral("Applied 1 edit")), "The model's edit should apply.");
	trigger(shell, "map.undo");
	settle();
	dialog->close();
	settle();
	setAi([](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = true;
		preferences.connectorModels.clear();
		preferences.connectorEndpoints.clear();
	});
}

void checkSoundGenerator(ApplicationShell& shell, const Fixtures& fixtures)
{
	ensureActive(shell, "checkSoundGenerator");
	const auto setAi = [](const std::function<void(AiAutomationPreferences&)>& change) {
		StudioSettings settings;
		AiAutomationPreferences preferences = settings.aiAutomationPreferences();
		change(preferences);
		settings.setAiAutomationPreferences(preferences);
		settings.sync();
	};
	{
		StudioSettings settings;
		settings.setCurrentProjectPath(fixtures.project);
		settings.sync();
	}
	setAi([](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = true;
	});
	trigger(shell, "audio.generate");
	settle();
	// The dialog has no Q_OBJECT, so it is found as a dialog.
	auto* dialog = static_cast<SoundGenerationDialog*>(shell.findChild<QDialog*>(QStringLiteral("soundGenerationDialog")));
	check(dialog && dialog->isVisible(), "Generate Sound should open the Sound Generator.");
	auto* prompt = child<QPlainTextEdit>(shell, "soundGenerationPrompt");
	auto* game = child<QComboBox>(shell, "soundGenerationGame");
	auto* kind = child<QComboBox>(shell, "soundGenerationKind");
	auto* loop = child<QCheckBox>(shell, "soundGenerationLoop");
	auto* variants = child<QSpinBox>(shell, "soundGenerationVariantCount");
	auto* synth = child<QRadioButton>(shell, "soundGenerationSynthSource");
	auto* model = child<QRadioButton>(shell, "soundGenerationModelSource");
	auto* sourceStatus = child<QLabel>(shell, "soundGenerationSourceStatus");
	auto* status = child<QLabel>(shell, "soundGenerationStatus");
	auto* list = child<QListWidget>(shell, "soundGenerationVariantList");
	auto* save = child<QPushButton>(shell, "soundGenerationSave");
	auto* play = child<QPushButton>(shell, "soundGenerationPlay");
	check(dialog && prompt && game && kind && loop && variants && synth && model && sourceStatus && status && list && save && play,
		"The Sound Generator should have its parts.");
	if (!dialog || !prompt || !game || !kind || !loop || !variants || !synth || !model || !sourceStatus || !status || !list || !save || !play) {
		return;
	}
	QStringList unnamed;
	for (QWidget* widget : dialog->findChildren<QWidget*>()) {
		const auto* button = qobject_cast<QAbstractButton*>(widget);
		const bool control = button || qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QLineEdit*>(widget)
			|| qobject_cast<QPlainTextEdit*>(widget) || qobject_cast<QListWidget*>(widget);
		const bool inner = qobject_cast<QAbstractSpinBox*>(widget->parentWidget()) || qobject_cast<QComboBox*>(widget->parentWidget());
		if (control && !inner && widget->accessibleName().isEmpty() && (!button || button->text().isEmpty())) {
			unnamed << QStringLiteral("%1 %2").arg(QString::fromLatin1(widget->metaObject()->className()), widget->objectName());
		}
	}
	check(unnamed.isEmpty(), qPrintable(QStringLiteral("Every Sound Generator control should have an accessible name: %1").arg(unnamed.join(QStringLiteral(", ")))));

	// The synthesizer needs no AI: two variants of a Quake door.
	game->setCurrentIndex(game->findData(QStringLiteral("quake")));
	synth->setChecked(true);
	variants->setValue(2);
	prompt->setPlainText(QStringLiteral("heavy metal door slam"));
	prompt->setFocus(Qt::OtherFocusReason);
	QTest::keyClick(prompt, Qt::Key_Return, Qt::ControlModifier);
	check(QTest::qWaitFor([&] { return !dialog->busy() && list->count() == 2; }, 30000), "Ctrl+Enter should synthesize two variants.");
	check(dialog->sounds().size() == 2 && dialog->sounds().first().ok && !list->item(0)->icon().isNull() && play->isEnabled()
			&& list->item(0)->text().startsWith(QStringLiteral("door_slam_1:")),
		"Each variant should be listed with its waveform, name, and format, ready to play.");
	snapshotWidget(dialog, "sound-generator");
	save->click();
	settle();
	const QString door = QDir(fixtures.project).filePath(QStringLiteral("sound/vibestudio/door_slam_1.wav"));
	check(QFileInfo::exists(door) && QFileInfo::exists(QDir(fixtures.project).filePath(QStringLiteral(".vibestudio/generated/sounds/door_slam_1.json")))
			&& status->text().contains(QStringLiteral("vibestudio/door_slam_1.wav")),
		"Save to Project should write the Quake WAV and its record into the project.");

	// Ambience loops by default; a Doom sound goes into a PWAD.
	kind->setCurrentIndex(kind->findData(QStringLiteral("ambience")));
	check(loop->isChecked(), "Choosing ambience should tick Loop.");
	kind->setCurrentIndex(kind->findData(QStringLiteral("shot")));
	check(!loop->isChecked(), "Choosing a gunshot should untick it again.");
	game->setCurrentIndex(game->findData(QStringLiteral("doom")));
	variants->setValue(1);
	prompt->setPlainText(QStringLiteral("shotgun blast"));
	dialog->generate();
	check(QTest::qWaitFor([&] { return !dialog->busy() && list->count() == 1; }, 30000), "The Doom variant should be made.");
	check(dialog->saveSelected(false) && QFileInfo::exists(QDir(fixtures.project).filePath(QStringLiteral("wads/vibestudio_sounds.wad"))),
		"A Doom sound should be saved as a lump in the project's PWAD.");

	// In AI-free mode the model is not asked.
	model->setChecked(true);
	settle();
	check(sourceStatus->text().contains(QStringLiteral("AI-free")), "In AI-free mode the source status should say why the model cannot be asked.");
	dialog->generate();
	settle();
	check(status->text().contains(QStringLiteral("AI-free")), "Generate should not ask the model in AI-free mode.");

	// A sound model on this machine, through the custom connector.
	FakeAiProvider provider;
	check(provider.listening(), "The fake provider should listen.");
	AudioClip tone;
	tone.channels = 1;
	tone.sampleRate = 22050;
	for (int frame = 0; frame < 11025; ++frame) {
		tone.samples << float(0.5 * std::sin(2.0 * 3.14159265 * 330.0 * frame / 22050.0));
	}
	provider.answerInTurn(200, encodeAudioWav(tone), QByteArrayLiteral("audio/wav"));
	setAi([&provider](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = false;
		preferences.cloudConnectorsEnabled = false;
		preferences.preferredAudioConnectorId = QStringLiteral("custom-http");
		preferences.connectorAudioEndpoints.insert(QStringLiteral("custom-http"), provider.baseUrl());
	});
	dialog->refreshSourceStatus();
	check(sourceStatus->text().startsWith(QStringLiteral("Ready")), "The source status should say the local sound model is ready.");
	game->setCurrentIndex(game->findData(QStringLiteral("quake2")));
	prompt->setPlainText(QStringLiteral("reactor hum"));
	dialog->generate();
	check(QTest::qWaitFor([&] { return !dialog->busy() && list->count() == 1 && dialog->sounds().first().source.startsWith(QStringLiteral("ai:custom-http")); }, 30000),
		"The sound model should make the variant.");
	check(!provider.exchanges().isEmpty() && provider.exchanges().last().path.startsWith("/v1/sound-generation")
			&& dialog->sounds().first().delivery.plan.sampleRate == 22050,
		"The request should go to the sound-generation path and come back as a Quake II WAV.");
	dialog->close();
	settle();
	setAi([](AiAutomationPreferences& preferences) {
		preferences.aiFreeMode = true;
		preferences.preferredAudioConnectorId.clear();
		preferences.connectorAudioEndpoints.clear();
	});
}

int main(int argc, char** argv)
{
	// The build check runs this program as a stand-in compiler (given the map
	// to compile) and as a stand-in game (given a map to load); a normal test
	// run is given neither.
	for (int index = 1; index < argc; ++index) {
		const QByteArray argument(argv[index]);
		if (argument.endsWith(".map")) {
			return runStandInCompiler(argc, argv);
		}
		if (argument == "+map" || argument == "+devmap" || argument == "-warp") {
			return 0;
		}
		// Stands in for another running studio: stays up until it is killed.
		if (argument == "--linger") {
			std::this_thread::sleep_for(std::chrono::seconds(60));
			return 0;
		}
	}
#if defined(_MSC_VER) && defined(_DEBUG)
	// A debug-runtime assertion would otherwise raise a dialog nobody can see
	// and end the run as a bare breakpoint; send its text to stderr instead.
	for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& message) {
		if (type == QtFatalMsg || type == QtCriticalMsg) {
			std::cerr << "Qt: " << message.toStdString() << " (" << (context.file ? context.file : "?") << ":" << context.line << ")" << std::endl;
		}
		if (type == QtFatalMsg) {
			std::abort();
		}
	});
	QTemporaryDir settingsDir;
	QTemporaryDir fixtureDir;
	if (!settingsDir.isValid() || !fixtureDir.isValid()) {
		std::cerr << "Temporary directories could not be created.\n";
		return 1;
	}
	StudioSettings::setOverrideFilePath(settingsDir.filePath(QStringLiteral("settings.ini")));

	// Invisible on every platform: no window flashes up during a test run, and
	// nothing depends on a desktop session. The offscreen plugin has no system
	// font directory on Windows, so point it at the installed fonts.
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
#ifdef Q_OS_WIN
	if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && !qEnvironmentVariableIsEmpty("SystemRoot")) {
		qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
	}
#endif

	QApplication app(argc, argv);
	app.setOrganizationName(QStringLiteral("DarkMatterProductions"));
	app.setApplicationName(QStringLiteral("VibeStudioTest"));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));

	const Fixtures fixtures = buildFixtures(fixtureDir.path());
	{
		// Read-only, as a newly added installation is, so the launch check can
		// see the studio ask before its first copy into it.
		StudioSettings settings;
		GameInstallationProfile quake;
		quake.gameKey = QStringLiteral("quake");
		quake.engineFamily = GameEngineFamily::IdTech2;
		quake.displayName = QStringLiteral("Test Quake");
		quake.rootPath = fixtures.gameRoot;
		quake.executablePath = QCoreApplication::applicationFilePath();
		quake.readOnly = true;
		settings.upsertGameInstallation(quake);
		// VIBESTUDIO_TEST_THEME=high-contrast runs the same checks in the
		// high-visibility theme, for reviewing snapshots there.
		if (qEnvironmentVariable("VIBESTUDIO_TEST_THEME") == QStringLiteral("high-contrast")) {
			AccessibilityPreferences preferences = settings.accessibilityPreferences();
			preferences.theme = StudioTheme::HighContrastDark;
			settings.setAccessibilityPreferences(preferences);
			applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 100));
		}
		settings.sync();
	}

	installDialogWatchdog();
	ApplicationShell shell;
	shell.resize(1400, 900);
	shell.show();
	settle();

	check(currentPage(shell) >= 0, "The shell should build its page stack.");
	shell.activateWindow();
	check(QTest::qWaitForWindowActive(&shell, 2000), "The shell window should become active.");
	settle();
	if (auto* editor = child<QPlainTextEdit>(shell, "codeEditor")) {
		check(editor->isReadOnly(), "Before any file opens, the code editor should take no typing.");
	}

	RUN_CHECK(checkCommandRegistry, shell);
	RUN_CHECK(checkFindAndEscape, shell, fixtures);
	RUN_CHECK(checkPackageKeysStayOnPackages, shell);
	RUN_CHECK(checkCodeEditor, shell, fixtures);
	RUN_CHECK(checkNavigation, shell, fixtures);
	RUN_CHECK(checkMapGuards, shell, fixtures);
	RUN_CHECK(checkLevelLinks, shell, fixtures);
	RUN_CHECK(checkListsAndRecents, shell, fixtures);
	RUN_CHECK(checkViewerControls, shell);
	RUN_CHECK(checkMapContextMenu, shell);
	RUN_CHECK(checkMapAddDelete, shell);
	RUN_CHECK(checkTargetLinks, shell);
	RUN_CHECK(checkReplaceTexture, shell);
	RUN_CHECK(checkHideShow, shell);
	RUN_CHECK(checkMapPalette, shell);
	RUN_CHECK(checkMapSelectionAndResize, shell);
	RUN_CHECK(checkMapClip, shell);
	RUN_CHECK(checkCloseMap, shell, fixtures);
	RUN_CHECK(checkBuildLoop, shell, fixtures);
	RUN_CHECK(checkWadMaps, shell, fixtures);
	RUN_CHECK(checkQuickOpen, shell, fixtures);
	RUN_CHECK(checkCodeLineEditing, shell);
	RUN_CHECK(checkCodeTabs, shell, fixtures);
	RUN_CHECK(checkGoToSymbol, shell, fixtures);
	RUN_CHECK(checkAudioBrowser, shell, fixtures);
	RUN_CHECK(checkWadTextures, shell);
	RUN_CHECK(checkLooseAssetsAndStagedGuard, shell, fixtures);
	RUN_CHECK(checkPaletteRecents, shell);
	RUN_CHECK(checkShortcutReference, shell);
	RUN_CHECK(checkCustomShortcuts, shell);
	RUN_CHECK(checkPanelTabFitting);
	RUN_CHECK(checkHeaderFolding);
	RUN_CHECK(checkReflowGrid);
	RUN_CHECK(checkStatusBarFolding);
	RUN_CHECK(checkShortcutRules, shell);
	RUN_CHECK(checkCrashReportPreference, shell);
	RUN_CHECK(checkSettingsSearch, shell);
	RUN_CHECK(checkToolBarLabels, shell);
	RUN_CHECK(checkLevelTextures, shell, fixtures);
	RUN_CHECK(checkObjectQuery, shell, fixtures);
	RUN_CHECK(checkPackageQuery, shell, fixtures);
	RUN_CHECK(checkShaderQuery, shell, fixtures);
	RUN_CHECK(checkSessionRestore, shell, fixtures);
	RUN_CHECK(checkDoomTagLinks, fixtures);
	RUN_CHECK(checkInstallationPalette, shell, fixtures);
	RUN_CHECK(checkPackageDropStaging, shell, fixtures);
	RUN_CHECK(checkCodeTabFixes, shell, fixtures);
	RUN_CHECK(checkCodeTreeFollowsTab, shell, fixtures);
	RUN_CHECK(checkExternalChanges, shell, fixtures);
	RUN_CHECK(checkCodeZoom, shell, fixtures);
	RUN_CHECK(checkGoToDefinition, shell, fixtures);
	RUN_CHECK(checkCodeFolding, shell, fixtures);
	RUN_CHECK(checkCodeBreadcrumb, shell, fixtures);
	RUN_CHECK(checkNextProblem, shell, fixtures);
	RUN_CHECK(checkUseHighlights, shell, fixtures);
	RUN_CHECK(checkStickyHeaders, shell, fixtures);
	RUN_CHECK(checkDropNames, shell, fixtures);
	RUN_CHECK(checkMultiEntityKeys, shell, fixtures);
	RUN_CHECK(checkPlaceHistory, shell, fixtures);
	RUN_CHECK(checkLargeSelection, shell, fixtures);
	RUN_CHECK(checkDiagnosticHighlightCost);
	RUN_CHECK(checkNavigationRail, shell);
	RUN_CHECK(checkEditorProfiles, shell, fixtures);
	RUN_CHECK(checkAssistant, shell, fixtures);
	RUN_CHECK(checkGenerators, shell, fixtures);
	RUN_CHECK(checkMapAiEdit, shell, fixtures);
	RUN_CHECK(checkSoundGenerator, shell, fixtures);
	RUN_CHECK(checkCameraDepth, shell, fixtures);
	// Last: these start session recording, which earlier checks do not expect.
	RUN_CHECK(checkSecondaryInstance, shell, fixtures);
	RUN_CHECK(checkCrashRecovery, shell, fixtures);

	if (g_failures > 0) {
		std::cerr << g_failures << " shell interaction check(s) failed.\n";
		return 1;
	}
	std::cout << "Shell interaction checks passed.\n";
	return 0;
}
