#include "core/bsp_inspect.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>
#include <cstring>
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

void appendLe32(QByteArray* data, qint32 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
	data->append(static_cast<char>((value >> 16) & 0xff));
	data->append(static_cast<char>((value >> 24) & 0xff));
}

void appendFloat(QByteArray* data, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	appendLe32(data, static_cast<qint32>(raw));
}

void appendFixed(QByteArray* data, const QByteArray& name, int size)
{
	QByteArray bytes = name.left(size);
	while (bytes.size() < size) {
		bytes.append('\0');
	}
	data->append(bytes);
}

QByteArray zeros(int count)
{
	return QByteArray(count, '\0');
}

// Assembles a BSP file from a fixed header prefix, a lump table of `lumpCount`
// {offset, length} pairs and the payloads that belong to them.
QByteArray buildBspFile(const QByteArray& headerPrefix, int lumpCount, const QMap<int, QByteArray>& payloads)
{
	const qint32 headerBytes = static_cast<qint32>(headerPrefix.size() + lumpCount * 8);
	QByteArray table;
	QByteArray body;
	for (int i = 0; i < lumpCount; ++i) {
		const QByteArray data = payloads.value(i);
		appendLe32(&table, headerBytes + static_cast<qint32>(body.size()));
		appendLe32(&table, static_cast<qint32>(data.size()));
		body.append(data);
	}
	return headerPrefix + table + body;
}

void patchLe32(QByteArray* data, qsizetype offset, qint32 value)
{
	QByteArray encoded;
	appendLe32(&encoded, value);
	for (qsizetype i = 0; i < encoded.size(); ++i) {
		(*data)[offset + i] = encoded.at(i);
	}
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

bool nearlyEqual(double lhs, double rhs)
{
	return std::fabs(lhs - rhs) < 0.001;
}

// ---------------------------------------------------------------------------
// Quake BSP29 fixture
// ---------------------------------------------------------------------------

QByteArray quakeEntityLump()
{
	QByteArray text =
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"\"message\" \"The Quoted Room\"\n"
		"\"wad\" \"gfx/base.wad\"\n"
		"}\n"
		"{\n"
		"\"classname\" \"info_player_start\"\n"
		"\"origin\" \"0 0 24\"\n"
		"}\n";
	// A NUL in the middle of the lump must not stop the parser: the third
	// entity after it still has to be reported.
	text.append('\0');
	text.append(
		"{\n"
		"\"classname\" \"light\"\n"
		"\"light\" \"300\"\n"
		"}\n");
	return text;
}

QByteArray quakeMiptexLump()
{
	QByteArray lump;
	appendLe32(&lump, 2);
	appendLe32(&lump, 12);
	appendLe32(&lump, 52);
	appendFixed(&lump, "city1_1", 16);
	appendLe32(&lump, 64);
	appendLe32(&lump, 64);
	appendLe32(&lump, 40);    // embedded: mip level 0 payload offset
	appendLe32(&lump, 0);
	appendLe32(&lump, 0);
	appendLe32(&lump, 0);
	appendFixed(&lump, "sky1", 16);
	appendLe32(&lump, 128);
	appendLe32(&lump, 128);
	appendLe32(&lump, 0);     // no embedded payload: sourced from a WAD
	appendLe32(&lump, 0);
	appendLe32(&lump, 0);
	appendLe32(&lump, 0);
	return lump;
}

QByteArray quakeTexinfoLump()
{
	QByteArray lump;
	for (const qint32 miptex : {0, 0, 1}) {
		lump.append(zeros(32));
		appendLe32(&lump, miptex);
		appendLe32(&lump, 0);
	}
	return lump;
}

QByteArray quakeFaceLump()
{
	QByteArray lump;
	for (const qint32 lightofs : {0, -1}) {
		lump.append(zeros(16));
		appendLe32(&lump, lightofs);
	}
	return lump;
}

QByteArray quakeModelLump()
{
	QByteArray lump;
	appendFloat(&lump, -64.0f);
	appendFloat(&lump, -64.0f);
	appendFloat(&lump, -32.0f);
	appendFloat(&lump, 64.0f);
	appendFloat(&lump, 64.0f);
	appendFloat(&lump, 32.0f);
	lump.append(zeros(64 - 24));
	return lump;
}

QByteArray quakeVertexLump()
{
	QByteArray lump;
	const float positions[4][3] = {
		{-64.0f, -64.0f, -32.0f},
		{64.0f, -64.0f, -32.0f},
		{64.0f, 64.0f, 32.0f},
		{-64.0f, 64.0f, 32.0f},
	};
	for (const auto& position : positions) {
		appendFloat(&lump, position[0]);
		appendFloat(&lump, position[1]);
		appendFloat(&lump, position[2]);
	}
	return lump;
}

QByteArray buildQuakeBsp()
{
	QByteArray header;
	appendLe32(&header, 29);

	QMap<int, QByteArray> payloads;
	payloads.insert(0, quakeEntityLump());
	payloads.insert(1, zeros(20 * 2));
	payloads.insert(2, quakeMiptexLump());
	payloads.insert(3, quakeVertexLump());
	payloads.insert(4, zeros(16));
	payloads.insert(5, zeros(24));
	payloads.insert(6, quakeTexinfoLump());
	payloads.insert(7, quakeFaceLump());
	payloads.insert(8, zeros(256));
	payloads.insert(9, zeros(8));
	payloads.insert(10, zeros(28 * 2));
	payloads.insert(11, zeros(2 * 4));
	payloads.insert(12, zeros(4 * 4));
	payloads.insert(13, zeros(4 * 4));
	payloads.insert(14, quakeModelLump());
	return buildBspFile(header, 15, payloads);
}

// ---------------------------------------------------------------------------
// Quake II IBSP 38 fixture
// ---------------------------------------------------------------------------

QByteArray quake2TexinfoLump()
{
	QByteArray lump;
	for (const char* name : {"e1u1/metal1_1", "e1u1/floor3_1"}) {
		lump.append(zeros(32));
		appendLe32(&lump, 8);      // flags
		appendLe32(&lump, 0);      // value
		appendFixed(&lump, name, 32);
		appendLe32(&lump, -1);     // nexttexinfo
	}
	return lump;
}

QByteArray quake2ModelLump()
{
	QByteArray lump;
	appendFloat(&lump, -128.0f);
	appendFloat(&lump, -128.0f);
	appendFloat(&lump, -64.0f);
	appendFloat(&lump, 128.0f);
	appendFloat(&lump, 128.0f);
	appendFloat(&lump, 64.0f);
	lump.append(zeros(48 - 24));
	return lump;
}

QByteArray buildQuake2Bsp()
{
	QByteArray header("IBSP");
	appendLe32(&header, 38);

	QMap<int, QByteArray> payloads;
	payloads.insert(0, QByteArray("{\n\"classname\" \"worldspawn\"\n\"message\" \"Base Unit One\"\n}\n"));
	payloads.insert(1, zeros(20 * 2));
	payloads.insert(2, zeros(12 * 3));
	payloads.insert(3, zeros(32));
	payloads.insert(4, zeros(28));
	payloads.insert(5, quake2TexinfoLump());
	payloads.insert(6, zeros(20 * 2));
	payloads.insert(7, zeros(96));
	payloads.insert(8, zeros(28 * 2));
	payloads.insert(13, quake2ModelLump());
	payloads.insert(14, zeros(12 * 2));
	return buildBspFile(header, 19, payloads);
}

// ---------------------------------------------------------------------------
// Quake III IBSP 46 fixture
// ---------------------------------------------------------------------------

QByteArray quake3ShaderLump()
{
	QByteArray lump;
	for (const char* name : {"textures/base_wall/c_met5_2", "textures/common/caulk"}) {
		appendFixed(&lump, name, 64);
		appendLe32(&lump, 4);
		appendLe32(&lump, 1);
	}
	return lump;
}

QByteArray quake3SurfaceLump()
{
	QByteArray lump;
	for (int i = 0; i < 2; ++i) {
		appendLe32(&lump, 0);    // shaderNum
		lump.append(zeros(100));
	}
	return lump;
}

QByteArray quake3ModelLump()
{
	QByteArray lump;
	appendFloat(&lump, -256.0f);
	appendFloat(&lump, -256.0f);
	appendFloat(&lump, -128.0f);
	appendFloat(&lump, 256.0f);
	appendFloat(&lump, 256.0f);
	appendFloat(&lump, 128.0f);
	lump.append(zeros(40 - 24));
	return lump;
}

QByteArray buildQuake3Bsp()
{
	QByteArray header("IBSP");
	appendLe32(&header, 46);

	QMap<int, QByteArray> payloads;
	payloads.insert(0, QByteArray("{\n\"classname\" \"worldspawn\"\n\"message\" \"Arena Of Death\"\n}\n"));
	payloads.insert(1, quake3ShaderLump());
	payloads.insert(2, zeros(16 * 2));
	payloads.insert(3, zeros(36));
	payloads.insert(4, zeros(48 * 2));
	payloads.insert(7, quake3ModelLump());
	payloads.insert(8, zeros(12 * 2));
	payloads.insert(10, zeros(44 * 3));
	payloads.insert(13, quake3SurfaceLump());
	payloads.insert(14, zeros(128 * 128 * 3));
	payloads.insert(16, zeros(64));
	return buildBspFile(header, 17, payloads);
}

// ---------------------------------------------------------------------------
// Cases
// ---------------------------------------------------------------------------

bool runQuakeBspSmoke(const QDir& root)
{
	bool ok = true;
	const QString path = root.filePath(QStringLiteral("quake29.bsp"));
	ok &= expect(writeFile(path, buildQuakeBsp()), "Quake BSP fixture should be written.");

	const BspInspection inspection = inspectBspFile(path);
	ok &= expect(inspection.valid, "Quake BSP fixture should inspect cleanly.");
	ok &= expect(inspection.errors.isEmpty(), "Quake BSP fixture should produce no errors.");
	ok &= expect(inspection.family == BspFamily::Quake, "Quake BSP family mismatch.");
	ok &= expect(inspection.familyId == QStringLiteral("quake"), "Quake BSP family id mismatch.");
	ok &= expect(inspection.version == 29, "Quake BSP version mismatch.");
	ok &= expect(inspection.lumps.size() == 15, "Quake BSP should expose 15 lumps.");
	ok &= expect(inspection.lumps.at(0).name == QStringLiteral("entities"), "Quake lump 0 should be entities.");
	ok &= expect(inspection.lumps.at(14).name == QStringLiteral("models"), "Quake lump 14 should be models.");
	ok &= expect(inspection.lumps.at(1).entrySize == 20 && inspection.lumps.at(1).entryCount == 2, "Quake plane lump shape mismatch.");
	ok &= expect(inspection.lumps.at(3).entryCount == 4, "Quake vertex lump should hold 4 vertices.");

	ok &= expect(inspection.entityCount == 3, "Quake entity lump should parse past an embedded NUL.");
	ok &= expect(inspection.entities.at(0).className == QStringLiteral("worldspawn"), "First Quake entity should be worldspawn.");
	ok &= expect(inspection.worldspawnMessage == QStringLiteral("The Quoted Room"), "Quoted worldspawn message with spaces should survive parsing.");
	ok &= expect(inspection.entities.at(1).className == QStringLiteral("info_player_start"), "Second Quake entity classname mismatch.");
	ok &= expect(inspection.entities.at(1).properties.size() == 2, "Quake entity property count mismatch.");
	ok &= expect(inspection.entities.at(2).className == QStringLiteral("light"), "Entity after the NUL should still be parsed.");

	ok &= expect(inspection.textures.size() == 2, "Quake miptex directory should yield 2 textures.");
	ok &= expect(inspection.textures.at(0).name == QStringLiteral("city1_1"), "Quake textures should be sorted by name.");
	ok &= expect(inspection.textures.at(0).width == 64 && inspection.textures.at(0).height == 64, "Quake miptex dimensions mismatch.");
	ok &= expect(inspection.textures.at(0).embedded, "Quake miptex with pixel data should be embedded.");
	ok &= expect(inspection.textures.at(0).referenceCount == 2, "Quake texinfo references should be counted.");
	ok &= expect(inspection.textures.at(1).name == QStringLiteral("sky1"), "Second Quake texture name mismatch.");
	ok &= expect(!inspection.textures.at(1).embedded, "Quake miptex without pixel data should be reported as external.");

	ok &= expect(inspection.modelCount == 1, "Quake model count mismatch.");
	ok &= expect(inspection.faceCount == 2, "Quake face count mismatch.");
	ok &= expect(inspection.vertexCount == 4, "Quake vertex count mismatch.");
	ok &= expect(inspection.leafCount == 2, "Quake leaf count mismatch.");
	ok &= expect(inspection.nodeCount == 1, "Quake node count mismatch.");
	ok &= expect(inspection.planeCount == 2, "Quake plane count mismatch.");
	ok &= expect(inspection.lightmapCount == 1, "Only the lit Quake face should be counted.");
	ok &= expect(inspection.hasVisData && inspection.hasLightData, "Quake vis/light lumps should be detected.");
	ok &= expect(nearlyEqual(inspection.mins[0], -64.0) && nearlyEqual(inspection.maxs[2], 32.0), "Quake bounds should come from the first model.");

	const QJsonObject json = bspInspectionJson(inspection);
	ok &= expect(json.value(QStringLiteral("family")).toString() == QStringLiteral("quake"), "JSON family key mismatch.");
	ok &= expect(json.value(QStringLiteral("counts")).toObject().value(QStringLiteral("faces")).toInt() == 2, "JSON face count mismatch.");
	ok &= expect(!bspInspectionText(inspection).isEmpty(), "BSP inspection text should not be empty.");
	ok &= expect(bspInspectionText(inspection) == bspInspectionLines(inspection).join(QLatin1Char('\n')), "BSP inspection text should join its lines.");
	return ok;
}

bool runQuake2BspSmoke(const QDir& root)
{
	bool ok = true;
	const QString path = root.filePath(QStringLiteral("quake2.bsp"));
	ok &= expect(writeFile(path, buildQuake2Bsp()), "Quake II BSP fixture should be written.");

	const BspInspection inspection = inspectBspFile(path);
	ok &= expect(inspection.valid, "Quake II BSP fixture should inspect cleanly.");
	ok &= expect(inspection.family == BspFamily::Quake2, "Quake II BSP family mismatch.");
	ok &= expect(inspection.magic == QStringLiteral("IBSP"), "Quake II BSP ident mismatch.");
	ok &= expect(inspection.version == 38, "Quake II BSP version mismatch.");
	ok &= expect(inspection.lumps.size() == 19, "Quake II BSP should expose 19 lumps.");
	ok &= expect(inspection.lumps.at(5).name == QStringLiteral("texinfo") && inspection.lumps.at(5).entrySize == 76, "Quake II texinfo lump shape mismatch.");

	ok &= expect(inspection.entityCount == 1, "Quake II entity count mismatch.");
	ok &= expect(inspection.worldspawnMessage == QStringLiteral("Base Unit One"), "Quake II worldspawn message mismatch.");
	ok &= expect(inspection.textures.size() == 2, "Quake II texinfo should yield 2 texture names.");
	ok &= expect(inspection.textures.at(0).name == QStringLiteral("e1u1/floor3_1"), "Quake II texture names should be sorted.");
	ok &= expect(!inspection.textures.at(0).embedded, "Quake II textures are never embedded.");
	ok &= expect(inspection.textures.at(0).surfaceFlags == 8u, "Quake II surface flags should be decoded.");
	ok &= expect(inspection.modelCount == 1 && inspection.brushCount == 2, "Quake II model/brush counts mismatch.");
	ok &= expect(inspection.vertexCount == 3 && inspection.faceCount == 2, "Quake II vertex/face counts mismatch.");
	ok &= expect(inspection.leafCount == 2 && inspection.planeCount == 2, "Quake II leaf/plane counts mismatch.");
	ok &= expect(nearlyEqual(inspection.mins[2], -64.0) && nearlyEqual(inspection.maxs[0], 128.0), "Quake II bounds mismatch.");
	return ok;
}

bool runQuake3BspSmoke(const QDir& root)
{
	bool ok = true;
	const QString path = root.filePath(QStringLiteral("quake3.bsp"));
	ok &= expect(writeFile(path, buildQuake3Bsp()), "Quake III BSP fixture should be written.");

	const BspInspection inspection = inspectBspFile(path);
	ok &= expect(inspection.valid, "Quake III BSP fixture should inspect cleanly.");
	ok &= expect(inspection.family == BspFamily::Quake3, "Quake III BSP family mismatch.");
	ok &= expect(inspection.version == 46, "Quake III BSP version mismatch.");
	ok &= expect(inspection.lumps.size() == 17, "Quake III BSP should expose 17 lumps.");
	ok &= expect(inspection.lumps.at(1).name == QStringLiteral("shaders") && inspection.lumps.at(1).entrySize == 72, "Quake III shader lump shape mismatch.");
	ok &= expect(inspection.lumps.at(13).name == QStringLiteral("surfaces") && inspection.lumps.at(13).entrySize == 104, "Quake III surface lump shape mismatch.");

	ok &= expect(inspection.worldspawnMessage == QStringLiteral("Arena Of Death"), "Quake III worldspawn message mismatch.");
	ok &= expect(inspection.textures.size() == 2, "Quake III shader lump should yield 2 shaders.");
	ok &= expect(inspection.textures.at(0).name == QStringLiteral("textures/base_wall/c_met5_2"), "Quake III shader name mismatch.");
	ok &= expect(inspection.textures.at(0).referenceCount == 2, "Quake III draw surfaces should be counted as shader references.");
	ok &= expect(inspection.textures.at(0).surfaceFlags == 4u && inspection.textures.at(0).contentFlags == 1u, "Quake III shader flags mismatch.");
	ok &= expect(!inspection.textures.at(1).embedded, "Quake III shaders are never embedded.");
	ok &= expect(inspection.vertexCount == 3, "Quake III drawvert count mismatch.");
	ok &= expect(inspection.faceCount == 2, "Quake III surface count mismatch.");
	ok &= expect(inspection.brushCount == 2 && inspection.modelCount == 1, "Quake III brush/model counts mismatch.");
	ok &= expect(inspection.planeCount == 2 && inspection.leafCount == 2, "Quake III plane/leaf counts mismatch.");
	ok &= expect(inspection.lightmapCount == 1, "Quake III lightmap page count mismatch.");
	ok &= expect(inspection.hasVisData && inspection.hasLightData, "Quake III vis/light lumps should be detected.");
	ok &= expect(nearlyEqual(inspection.mins[0], -256.0) && nearlyEqual(inspection.maxs[1], 256.0), "Quake III bounds mismatch.");
	return ok;
}

bool runMalformedBspSmoke(const QDir& root)
{
	bool ok = true;

	// Truncated file: the ident is readable but the lump table is not.
	QByteArray truncated;
	appendLe32(&truncated, 29);
	truncated.append(zeros(16));
	const QString truncatedPath = root.filePath(QStringLiteral("truncated.bsp"));
	ok &= expect(writeFile(truncatedPath, truncated), "Truncated BSP fixture should be written.");
	const BspInspection truncatedInspection = inspectBspFile(truncatedPath);
	ok &= expect(!truncatedInspection.valid, "Truncated BSP should not be valid.");
	ok &= expect(!truncatedInspection.errors.isEmpty(), "Truncated BSP should report an error.");
	ok &= expect(truncatedInspection.state() == OperationState::Failed, "Truncated BSP state should be Failed.");
	ok &= expect(truncatedInspection.family == BspFamily::Quake, "Truncated BSP should still be recognised as Quake.");

	// Unrecognised ident.
	const QString garbagePath = root.filePath(QStringLiteral("garbage.bsp"));
	ok &= expect(writeFile(garbagePath, QByteArray("NOTABSPFILEATALL")), "Garbage BSP fixture should be written.");
	const BspInspection garbage = inspectBspFile(garbagePath);
	ok &= expect(!garbage.valid && garbage.family == BspFamily::Unknown, "Garbage BSP should be unknown and invalid.");
	ok &= expect(!garbage.error.isEmpty(), "Garbage BSP should carry an error message.");

	// Missing file.
	const BspInspection missing = inspectBspFile(root.filePath(QStringLiteral("nope.bsp")));
	ok &= expect(!missing.valid && !missing.error.isEmpty(), "Missing BSP should be reported, not crash.");

	// A lump offset past EOF must be reported without stopping the rest of the
	// inspection. Lump 11 (marksurfaces) starts at byte 4 + 11 * 8.
	QByteArray pastEof = buildQuakeBsp();
	patchLe32(&pastEof, 4 + 11 * 8, 0x40000000);
	const QString pastEofPath = root.filePath(QStringLiteral("pasteof.bsp"));
	ok &= expect(writeFile(pastEofPath, pastEof), "Past-EOF BSP fixture should be written.");
	const BspInspection pastEofInspection = inspectBspFile(pastEofPath);
	ok &= expect(!pastEofInspection.valid, "Past-EOF BSP should not be valid.");
	ok &= expect(!pastEofInspection.lumps.at(11).withinFile, "Past-EOF lump should be flagged as outside the file.");
	ok &= expect(!pastEofInspection.errors.isEmpty(), "Past-EOF lump should raise an error.");
	ok &= expect(pastEofInspection.entityCount == 3, "Entities should still parse when another lump is out of range.");
	ok &= expect(pastEofInspection.textures.size() == 2, "Textures should still parse when another lump is out of range.");

	// A lump length that is not a multiple of its record size is a warning, not
	// a crash. Quake II lump 1 (planes) has its length field at 8 + 1 * 8 + 4.
	QByteArray misaligned = buildQuake2Bsp();
	patchLe32(&misaligned, 8 + 1 * 8 + 4, 21);
	const QString misalignedPath = root.filePath(QStringLiteral("misaligned.bsp"));
	ok &= expect(writeFile(misalignedPath, misaligned), "Misaligned BSP fixture should be written.");
	const BspInspection misalignedInspection = inspectBspFile(misalignedPath);
	ok &= expect(!misalignedInspection.lumps.at(1).aligned, "Misaligned plane lump should be flagged.");
	ok &= expect(!misalignedInspection.warnings.isEmpty(), "Misaligned plane lump should raise a warning.");
	ok &= expect(misalignedInspection.valid, "A misaligned lump length is a warning, not a hard failure.");
	ok &= expect(misalignedInspection.state() == OperationState::Warning, "Misaligned BSP state should be Warning.");
	return ok;
}

bool runLeakAndPortalSmoke(const QDir& root)
{
	bool ok = true;

	const QString ptsPath = root.filePath(QStringLiteral("leaky.pts"));
	const QByteArray pts =
		"// leak file written by qbsp\n"
		"   -16 32 8\n"
		"64.5 -12.25 0 trailing junk here\n"
		"not numbers at all\n"
		"\n"
		"128 128 128\n";
	ok &= expect(writeFile(ptsPath, pts), "Leak point fixture should be written.");

	const LeakPointFile leak = loadLeakPointFile(ptsPath);
	ok &= expect(leak.valid, "Leak point file should parse.");
	ok &= expect(leak.pointCount == 3, "Leak point file should skip junk lines and keep 3 points.");
	ok &= expect(leak.pointsXyz.size() == 9, "Leak point file should flatten to 9 coordinates.");
	ok &= expect(nearlyEqual(leak.mins[0], -16.0) && nearlyEqual(leak.mins[1], -12.25) && nearlyEqual(leak.mins[2], 0.0), "Leak point mins mismatch.");
	ok &= expect(nearlyEqual(leak.maxs[0], 128.0) && nearlyEqual(leak.maxs[1], 128.0) && nearlyEqual(leak.maxs[2], 128.0), "Leak point maxs mismatch.");

	const LeakPointFile missingLeak = loadLeakPointFile(root.filePath(QStringLiteral("absent.pts")));
	ok &= expect(!missingLeak.valid && !missingLeak.error.isEmpty(), "Missing leak file should be reported.");

	const QString prtPath = root.filePath(QStringLiteral("leaky.prt"));
	const QByteArray prt =
		"PRT1\n"
		"7\n"
		"3\n"
		"4 0 1 (0 0 0 ) (0 64 0 ) (0 64 64 ) (0 0 64 )\n"
		"4 1 2 (64 0 0 ) (64 64 0 ) (64 64 64 ) (64 0 64 )\n"
		"4 2 3 (128 0 0 ) (128 64 0 ) (128 64 64 ) (128 0 64 )\n";
	ok &= expect(writeFile(prtPath, prt), "Portal fixture should be written.");

	const PortalFileSummary portals = inspectPortalFile(prtPath);
	ok &= expect(portals.valid, "Portal file should parse.");
	ok &= expect(portals.magic == QStringLiteral("PRT1"), "Portal magic mismatch.");
	ok &= expect(portals.leafCount == 7, "Portal leaf count mismatch.");
	ok &= expect(portals.portalCount == 3, "Portal count mismatch.");
	ok &= expect(portals.warnings.isEmpty(), "A complete portal file should not warn.");
	ok &= expect(portals.fileSizeBytes == prt.size(), "Portal file size mismatch.");

	const QString shortPrtPath = root.filePath(QStringLiteral("short.prt"));
	ok &= expect(writeFile(shortPrtPath, QByteArray("PRT1\n4\n9\n4 0 1 (0 0 0 )\n")), "Short portal fixture should be written.");
	const PortalFileSummary shortPortals = inspectPortalFile(shortPrtPath);
	ok &= expect(shortPortals.valid, "A truncated portal file should still report its header.");
	ok &= expect(!shortPortals.warnings.isEmpty(), "A truncated portal file should warn about missing portal lines.");

	const QString prt2Path = root.filePath(QStringLiteral("clustered.prt"));
	ok &= expect(writeFile(prt2Path, QByteArray("PRT2\n12\n5\n1\n4 0 1 (0 0 0 )\n")), "PRT2 fixture should be written.");
	const PortalFileSummary prt2 = inspectPortalFile(prt2Path);
	ok &= expect(prt2.valid && prt2.magic == QStringLiteral("PRT2"), "PRT2 magic should be accepted.");
	ok &= expect(prt2.leafCount == 12 && prt2.clusterCount == 5 && prt2.portalCount == 1, "PRT2 header counts mismatch.");

	const QString badPrtPath = root.filePath(QStringLiteral("bad.prt"));
	ok &= expect(writeFile(badPrtPath, QByteArray("NOTAPORTAL\n1\n2\n")), "Bad portal fixture should be written.");
	const PortalFileSummary badPortals = inspectPortalFile(badPrtPath);
	ok &= expect(!badPortals.valid && !badPortals.error.isEmpty(), "An unknown portal magic should be reported.");
	return ok;
}

bool runCompiledArtifactsSmoke(const QDir& root)
{
	bool ok = true;
	const QString bspPath = root.filePath(QStringLiteral("leaky.bsp"));
	ok &= expect(writeFile(bspPath, buildQuakeBsp()), "Compiled artifact BSP fixture should be written.");
	ok &= expect(writeFile(root.filePath(QStringLiteral("leaky.log")), QByteArray("qbsp log\n")), "Compiler log fixture should be written.");

	const CompiledMapArtifacts artifacts = inspectCompiledMapArtifacts(bspPath);
	ok &= expect(artifacts.bsp.valid, "Compiled artifact BSP should inspect cleanly.");
	ok &= expect(artifacts.hasLeakFile, "Sibling .pts file should be detected as a leak file.");
	ok &= expect(artifacts.leak.valid && artifacts.leak.pointCount == 3, "Sibling leak file should be loaded.");
	ok &= expect(artifacts.hasPortalFile && artifacts.portals.valid, "Sibling .prt file should be loaded.");
	ok &= expect(artifacts.relatedPaths.size() == 3, "Related paths should list the .pts, .prt and .log siblings.");
	ok &= expect(artifacts.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("leak"), Qt::CaseInsensitive), "A leaked compile must be called out in the warnings.");

	QStringList sortedRelated = artifacts.relatedPaths;
	sortedRelated.sort();
	ok &= expect(sortedRelated == artifacts.relatedPaths, "Related paths should be sorted.");

	const QJsonObject json = compiledMapArtifactsJson(artifacts);
	ok &= expect(json.value(QStringLiteral("hasLeakFile")).toBool(), "Artifact JSON should report the leak file.");
	ok &= expect(json.value(QStringLiteral("portals")).toObject().value(QStringLiteral("magic")).toString() == QStringLiteral("PRT1"), "Artifact JSON should carry the portal magic.");
	ok &= expect(!compiledMapArtifactsText(artifacts).isEmpty(), "Artifact text summary should not be empty.");

	// A bare `<base>.leak` sibling must be found too.
	const QString sealedDirPath = root.filePath(QStringLiteral("sealed"));
	ok &= expect(QDir().mkpath(sealedDirPath), "Secondary artifact directory should be created.");
	const QDir sealedDir(sealedDirPath);
	const QString sealedBsp = sealedDir.filePath(QStringLiteral("dm3.bsp"));
	ok &= expect(writeFile(sealedBsp, buildQuakeBsp()), "Secondary BSP fixture should be written.");
	ok &= expect(writeFile(sealedDir.filePath(QStringLiteral("dm3.leak")), QByteArray("0 0 0\n32 32 32\n")), "Bare .leak fixture should be written.");

	const CompiledMapArtifacts sealed = inspectCompiledMapArtifacts(sealedBsp);
	ok &= expect(sealed.hasLeakFile, "A bare .leak sibling should be detected.");
	ok &= expect(sealed.leak.valid && sealed.leak.pointCount == 2, "A bare .leak sibling should be parsed as a leak line.");
	ok &= expect(!sealed.hasPortalFile, "No portal file should be reported when none exists.");
	ok &= expect(sealed.relatedPaths.size() == 1, "Only the .leak sibling should be related here.");

	// No siblings at all: nothing is invented.
	const QString loneDirPath = root.filePath(QStringLiteral("lone"));
	ok &= expect(QDir().mkpath(loneDirPath), "Lone artifact directory should be created.");
	const QString loneBsp = QDir(loneDirPath).filePath(QStringLiteral("dm6.bsp"));
	ok &= expect(writeFile(loneBsp, buildQuake3Bsp()), "Lone BSP fixture should be written.");
	const CompiledMapArtifacts lone = inspectCompiledMapArtifacts(loneBsp);
	ok &= expect(!lone.hasLeakFile && !lone.hasPortalFile, "A sealed compile should report no leak or portal file.");
	ok &= expect(lone.relatedPaths.isEmpty(), "A sealed compile should have no related paths.");
	ok &= expect(lone.warnings.isEmpty(), "A clean compiled artifact set should not warn.");
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
	ok &= runQuakeBspSmoke(root);
	ok &= runQuake2BspSmoke(root);
	ok &= runQuake3BspSmoke(root);
	ok &= runMalformedBspSmoke(root);
	ok &= runLeakAndPortalSmoke(root);
	ok &= runCompiledArtifactsSmoke(root);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
