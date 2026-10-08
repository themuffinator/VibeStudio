// Smoke tests for the static-model interchange formats: LightWave LWO2/LWOB,
// ASCII Scene Export (decode and export) and Build/ZDoom KVX voxels. Every
// fixture is built in code; no game data is used.
#include "core/model_ase.h"
#include "core/model_mesh.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <random>

using namespace vibestudio;

namespace {

int g_failures = 0;

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << "\n";
		++g_failures;
	}
	return condition;
}

bool nearly(float value, float expected, float epsilon = 1.0e-4f)
{
	return std::fabs(value - expected) <= epsilon;
}

bool nearVec(const ModelVec3& value, float x, float y, float z, float epsilon = 1.0e-4f)
{
	return nearly(value.x, x, epsilon) && nearly(value.y, y, epsilon) && nearly(value.z, z, epsilon);
}

ModelVec3 sub(const ModelVec3& a, const ModelVec3& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

ModelVec3 cross(const ModelVec3& a, const ModelVec3& b)
{
	return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}

float dot(const ModelVec3& a, const ModelVec3& b)
{
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

ModelVec3 faceCross(const ModelSurface& surface, const ModelTriangle& triangle)
{
	const QVector<ModelVec3>& p = surface.frames.constFirst().positions;
	return cross(sub(p.at(triangle.b), p.at(triangle.a)), sub(p.at(triangle.c), p.at(triangle.a)));
}

float faceArea(const ModelSurface& surface, const ModelTriangle& triangle)
{
	const ModelVec3 c = faceCross(surface, triangle);
	return 0.5f * std::sqrt(dot(c, c));
}

const ModelSurface* findSurface(const ModelMesh& mesh, const QString& name)
{
	for (const ModelSurface& surface : mesh.surfaces) {
		if (surface.name == name) {
			return &surface;
		}
	}
	return nullptr;
}

int countVertex(const ModelSurface& surface, const ModelVec3& position, float u, float v)
{
	int count = 0;
	for (int index = 0; index < surface.vertexCount; ++index) {
		const ModelVec3& p = surface.frames.constFirst().positions.at(index);
		const ModelTexCoord& uv = surface.texCoords.at(index);
		if (nearVec(p, position.x, position.y, position.z) && nearly(uv.u, u) && nearly(uv.v, v)) {
			++count;
		}
	}
	return count;
}

bool hasLineContaining(const QStringList& lines, const QString& needle)
{
	for (const QString& line : lines) {
		if (line.contains(needle)) {
			return true;
		}
	}
	return false;
}

// Either an error with nothing published, or a self-consistent single-frame mesh.
bool consistent(const ModelMesh& mesh)
{
	if (!mesh.error.isEmpty()) {
		return !mesh.geometryAvailable && mesh.surfaces.isEmpty() && mesh.embeddedSkins.isEmpty();
	}
	if (!mesh.geometryAvailable || mesh.surfaces.isEmpty() || mesh.frames.size() != 1) {
		return false;
	}
	for (const ModelSurface& surface : mesh.surfaces) {
		if (surface.frames.size() != 1 || surface.frames.constFirst().positions.size() != surface.vertexCount
			|| surface.frames.constFirst().normals.size() != surface.vertexCount || surface.texCoords.size() != surface.vertexCount
			|| surface.triangles.isEmpty()) {
			return false;
		}
		for (const ModelTriangle& t : surface.triangles) {
			if (t.a < 0 || t.b < 0 || t.c < 0 || t.a >= surface.vertexCount || t.b >= surface.vertexCount || t.c >= surface.vertexCount) {
				return false;
			}
		}
		for (const ModelVec3& p : surface.frames.constFirst().positions) {
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
				return false;
			}
		}
	}
	return true;
}

// Truncates at every length and flips bytes with a fixed seed; every decode
// must come back consistent. `patch` may repair a header after truncation.
void fuzz(const char* label, const QString& path, const QByteArray& valid, bool truncationFails, const std::function<void(QByteArray&)>& patch = {},
	int stride = 1)
{
	bool ok = true;
	for (int length = 0; length < valid.size(); length += stride) {
		QByteArray truncated = valid.left(length);
		if (patch) {
			patch(truncated);
		}
		const ModelMesh mesh = decodeModelMesh(path, truncated);
		ok &= consistent(mesh);
		if (truncationFails && length > 0) {
			ok &= !mesh.error.isEmpty();
		}
	}
	std::mt19937 random(20261008u);
	for (int iteration = 0; iteration < 400; ++iteration) {
		QByteArray damaged = valid;
		const int flips = 1 + int(random() % 4);
		for (int flip = 0; flip < flips; ++flip) {
			const int at = int(random() % quint32(damaged.size()));
			damaged[at] = char(quint8(damaged.at(at)) ^ quint8(1u << (random() % 8)));
		}
		ok &= consistent(decodeModelMesh(path, damaged));
	}
	if (!ok) {
		std::cerr << "FAIL: " << label << " fuzzing produced an inconsistent result\n";
		++g_failures;
	}
}

// --- LightWave ----------------------------------------------------------------

void putU16BE(QByteArray& bytes, quint32 value)
{
	bytes.append(char((value >> 8) & 0xFF));
	bytes.append(char(value & 0xFF));
}

void putU32BE(QByteArray& bytes, quint32 value)
{
	for (int shift = 24; shift >= 0; shift -= 8) {
		bytes.append(char((value >> shift) & 0xFF));
	}
}

void putF32BE(QByteArray& bytes, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	putU32BE(bytes, raw);
}

void putS0(QByteArray& bytes, const QByteArray& text)
{
	bytes.append(text);
	bytes.append('\0');
	if ((text.size() + 1) % 2 != 0) {
		bytes.append('\0');
	}
}

void putVX(QByteArray& bytes, quint32 value, bool longForm = false)
{
	if (value < 0xFF00 && !longForm) {
		putU16BE(bytes, value);
	} else {
		putU32BE(bytes, 0xFF000000u | value);
	}
}

QByteArray chunk(const char* id, const QByteArray& data)
{
	QByteArray bytes(id, 4);
	putU32BE(bytes, quint32(data.size()));
	bytes.append(data);
	if (data.size() % 2 != 0) {
		bytes.append('\0');
	}
	return bytes;
}

QByteArray form(const char* type, const QByteArray& chunks)
{
	QByteArray bytes("FORM");
	putU32BE(bytes, quint32(4 + chunks.size()));
	bytes.append(type, 4);
	bytes.append(chunks);
	return bytes;
}

struct LwPoint {
	float x;
	float y;
	float z;
	float u;
	float v;
};

// LightWave space: x right, y up, z forward. A floor quad facing +Y, a wall
// triangle facing -Z, and a concave pentagon facing -Z whose notch defeats a
// fan from its first corner. The triangle reaches its third point with a
// four-byte index; the pentagon overrides that shared point's UV with a VMAD.
const LwPoint kLwPoints[10] = {
	{0, 0, 0, 0.0f, 0.0f}, {0, 0, 1, 0.0f, 1.0f}, {1, 0, 1, 1.0f, 1.0f}, {1, 0, 0, 1.0f, 0.0f},
	{1, 1, 0, 0.25f, 0.5f}, {2, 0, 0, 0.5f, 0.25f},
	{2, 2, 0, 0.1f, 0.2f}, {4, 2, 0, 0.3f, 0.2f}, {4, 0, 0, 0.3f, 0.4f}, {3, 1.5f, 0, 0.2f, 0.3f},
};

QByteArray buildLwo(bool secondLayer, bool dropPointUv, int badIndex = -1)
{
	QByteArray tags;
	putS0(tags, "floor");
	putS0(tags, "wall");

	QByteArray layer;
	putU16BE(layer, 0);
	putU16BE(layer, 0);
	putF32BE(layer, 0.0f);
	putF32BE(layer, 0.0f);
	putF32BE(layer, 0.0f);
	putS0(layer, "main");

	QByteArray points;
	for (const LwPoint& p : kLwPoints) {
		putF32BE(points, p.x);
		putF32BE(points, p.y);
		putF32BE(points, p.z);
	}

	QByteArray vmap("TXUV", 4);
	putU16BE(vmap, 2);
	putS0(vmap, "uv");
	for (int index = 0; index < 10; ++index) {
		if (dropPointUv && index == 9) {
			continue;
		}
		putVX(vmap, quint32(index));
		putF32BE(vmap, kLwPoints[index].u);
		putF32BE(vmap, kLwPoints[index].v);
	}

	QByteArray pols("FACE", 4);
	putU16BE(pols, 4);
	for (int index : {0, 1, 2, 3}) {
		putVX(pols, quint32(index));
	}
	putU16BE(pols, 3);
	putVX(pols, 3, true);
	putVX(pols, 4);
	putVX(pols, badIndex >= 0 ? quint32(badIndex) : 5u);
	putU16BE(pols, 5);
	for (int index : {5, 6, 7, 8, 9}) {
		putVX(pols, quint32(index));
	}

	QByteArray ptag("SURF", 4);
	for (int polygon = 0; polygon < 3; ++polygon) {
		putVX(ptag, quint32(polygon));
		putU16BE(ptag, polygon == 0 ? 0 : 1);
	}

	QByteArray vmad("TXUV", 4);
	putU16BE(vmad, 2);
	putS0(vmad, "uv");
	putVX(vmad, 5);
	putVX(vmad, 2);
	putF32BE(vmad, 0.9f);
	putF32BE(vmad, 0.1f);

	QByteArray floor;
	putS0(floor, "floor");
	putS0(floor, "");
	floor.append("SMAN", 4);
	putU16BE(floor, 4);
	putF32BE(floor, 1.0f);
	floor.append("COLR", 4);
	putU16BE(floor, 14);
	putF32BE(floor, 0.5f);
	putF32BE(floor, 0.5f);
	putF32BE(floor, 0.5f);
	putU16BE(floor, 0);

	QByteArray wall;
	putS0(wall, "wall");
	putS0(wall, "");

	QByteArray chunks;
	chunks += chunk("TAGS", tags);
	chunks += chunk("LAYR", layer);
	chunks += chunk("PNTS", points);
	chunks += chunk("VMAP", vmap);
	chunks += chunk("POLS", pols);
	chunks += chunk("PTAG", ptag);
	chunks += chunk("VMAD", vmad);
	if (secondLayer) {
		QByteArray layer2;
		putU16BE(layer2, 1);
		putU16BE(layer2, 0);
		putF32BE(layer2, 0.0f);
		putF32BE(layer2, 0.0f);
		putF32BE(layer2, 0.0f);
		putS0(layer2, "extra");
		QByteArray points2;
		for (float value : {9.0f, 9.0f, 9.0f, 10.0f, 9.0f, 9.0f, 9.0f, 10.0f, 9.0f}) {
			putF32BE(points2, value);
		}
		QByteArray pols2("FACE", 4);
		putU16BE(pols2, 3);
		putVX(pols2, 0);
		putVX(pols2, 1);
		putVX(pols2, 2);
		chunks += chunk("LAYR", layer2);
		chunks += chunk("PNTS", points2);
		chunks += chunk("POLS", pols2);
	}
	chunks += chunk("SURF", floor);
	chunks += chunk("SURF", wall);
	return form("LWO2", chunks);
}

void runLightWaveSmoke()
{
	const QByteArray bytes = buildLwo(false, false);
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/room.lwo"), bytes);
	if (!expect(mesh.error.isEmpty(), "LWO2 fixture decodes")) {
		std::cerr << "  " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::LightWave && mesh.geometryAvailable, "LWO2 is detected and has geometry");
	expect(mesh.version == 2 && mesh.frames.size() == 1 && mesh.frames.constFirst().name == QStringLiteral("frame0"), "LWO2 has one frame");
	expect(mesh.surfaces.size() == 2, "LWO2 has one surface per SURF tag used");
	expect(mesh.skinPaths == QStringList({QStringLiteral("floor"), QStringLiteral("wall")}), "LWO2 skin paths are the surface names");
	expect(hasLineContaining(mesh.warnings, QStringLiteral("triangulated")), "LWO2 warns that Doom 3 drops polygons over three corners");
	const ModelSurface* floor = findSurface(mesh, QStringLiteral("floor"));
	const ModelSurface* wall = findSurface(mesh, QStringLiteral("wall"));
	if (!expect(floor && wall, "LWO2 surfaces are named after their tags")) {
		return;
	}
	expect(mesh.surfaces.constFirst().name == QStringLiteral("floor"), "LWO2 surfaces follow the SURF chunk order");
	expect(floor->skinPaths == QStringList(QStringLiteral("floor")), "LWO2 floor skin path");
	expect(floor->vertexCount == 4 && floor->triangles.size() == 2, "LWO2 quad becomes two triangles over four vertices");
	// Y and Z swap; V inverts.
	expect(countVertex(*floor, {0, 0, 0}, 0.0f, 1.0f) == 1, "LWO2 point 0 at the origin with UV (0, 1)");
	expect(countVertex(*floor, {0, 1, 0}, 0.0f, 0.0f) == 1, "LWO2 point 1 swaps Y and Z");
	expect(countVertex(*floor, {1, 1, 0}, 1.0f, 0.0f) == 1, "LWO2 point 2 swaps Y and Z");
	expect(countVertex(*floor, {1, 0, 0}, 1.0f, 1.0f) == 1, "LWO2 point 3 with inverted V");
	for (const ModelTriangle& t : floor->triangles) {
		const ModelVec3 c = faceCross(*floor, t);
		expect(c.z > 0.0f && nearly(c.x, 0.0f) && nearly(c.y, 0.0f), "LWO2 floor faces up (+Z) counter-clockwise");
	}
	for (const ModelVec3& n : floor->frames.constFirst().normals) {
		expect(nearVec(n, 0, 0, 1), "LWO2 floor normals point up");
	}

	expect(wall->vertexCount == 8 && wall->triangles.size() == 4, "LWO2 wall: triangle plus ear-clipped pentagon");
	expect(countVertex(*wall, {2, 0, 0}, 0.5f, 0.75f) == 1, "LWO2 shared point keeps its VMAP UV on the triangle");
	expect(countVertex(*wall, {2, 0, 0}, 0.9f, 0.9f) == 1, "LWO2 VMAD gives the pentagon corner its own UV");
	expect(countVertex(*wall, {1, 0, 0}, 1.0f, 1.0f) == 1, "LWO2 four-byte VX index reaches point 3");
	expect(countVertex(*wall, {3, 0, 1.5f}, 0.2f, 0.7f) == 1, "LWO2 pentagon notch point");
	float area = 0.0f;
	for (const ModelTriangle& t : wall->triangles) {
		const ModelVec3 c = faceCross(*wall, t);
		expect(c.y < 0.0f && nearly(c.x, 0.0f) && nearly(c.z, 0.0f), "LWO2 wall triangles all face -Y (no inverted fan triangle)");
		area += faceArea(*wall, t);
	}
	expect(nearly(area, 3.0f), "LWO2 wall area is the triangle (0.5) plus the concave pentagon (2.5)");
	expect(nearVec(mesh.mins, 0, 0, 0) && nearVec(mesh.maxs, 4, 1, 2), "LWO2 bounds in game axes");

	// A second layer is ignored, and a corner without a UV takes the first UV.
	const ModelMesh layered = decodeModelMesh(QStringLiteral("models/test/room.lwo"), buildLwo(true, true));
	expect(layered.error.isEmpty() && layered.surfaces.size() == 2, "LWO2 with two layers decodes the first");
	expect(hasLineContaining(layered.warnings, QStringLiteral("first of 2")), "LWO2 warns about the ignored layer");
	expect(hasLineContaining(layered.warnings, QStringLiteral("1 polygon corner(s) have no UV")), "LWO2 warns about the corner without a UV");
	if (const ModelSurface* layeredWall = findSurface(layered, QStringLiteral("wall"))) {
		expect(layeredWall->triangles.size() == 4, "LWO2 later layer's triangle is not added");
		expect(countVertex(*layeredWall, {3, 0, 1.5f}, 0.0f, 1.0f) == 1, "LWO2 unmapped corner takes the first TXUV value");
	}

	// Malformed input.
	const ModelMesh badIndex = decodeModelMesh(QStringLiteral("models/test/room.lwo"), buildLwo(false, false, 42));
	expect(!badIndex.error.isEmpty() && !badIndex.geometryAvailable, "LWO2 VX index beyond PNTS is an error");
	QByteArray oversize = bytes;
	oversize[16] = char(0x7F);
	expect(!decodeModelMesh(QStringLiteral("models/test/room.lwo"), oversize).error.isEmpty(), "LWO2 chunk size past the file is an error");
	QByteArray badForm = bytes;
	badForm[4] = char(0x7F);
	expect(!decodeModelMesh(QStringLiteral("models/test/room.lwo"), badForm).error.isEmpty(), "LWO2 FORM size past the file is an error");
	fuzz("LWO2 raw", QStringLiteral("models/test/room.lwo"), bytes, true);
	fuzz("LWO2 resized", QStringLiteral("models/test/room.lwo"), bytes, false, [](QByteArray& truncated) {
		if (truncated.size() >= 8) {
			const quint32 size = quint32(truncated.size() - 8);
			for (int index = 0; index < 4; ++index) {
				truncated[4 + index] = char((size >> (24 - 8 * index)) & 0xFF);
			}
		}
	});
	fuzz("LWO2 layered", QStringLiteral("models/test/room.lwo"), buildLwo(true, true), true);
}

void runLwobSmoke()
{
	// LightWave 5: one triangle facing -Z, surfaces named by SRFS and counted from 1.
	QByteArray points;
	for (float value : {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f}) {
		putF32BE(points, value);
	}
	QByteArray srfs;
	putS0(srfs, "textures/test/plain.tga");
	QByteArray pols;
	putU16BE(pols, 3);
	putU16BE(pols, 0);
	putU16BE(pols, 1);
	putU16BE(pols, 2);
	putU16BE(pols, 1);
	QByteArray surf;
	putS0(surf, "textures/test/plain.tga");
	surf.append("FLAG", 4);
	putU16BE(surf, 2);
	putU16BE(surf, 4);
	const QByteArray bytes = form("LWOB", chunk("PNTS", points) + chunk("SRFS", srfs) + chunk("POLS", pols) + chunk("SURF", surf));
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/old.lwo"), bytes);
	if (!expect(mesh.error.isEmpty() && mesh.surfaces.size() == 1, "LWOB triangle decodes")) {
		std::cerr << "  " << mesh.error.toStdString() << "\n";
		return;
	}
	const ModelSurface& surface = mesh.surfaces.constFirst();
	expect(mesh.version == 1, "LWOB reports version 1");
	expect(surface.skinPaths == QStringList(QStringLiteral("textures/test/plain")), "LWOB material drops the extension as Doom 3 does");
	expect(surface.triangles.size() == 1 && faceCross(surface, surface.triangles.constFirst()).y < 0.0f, "LWOB triangle faces -Y");
	expect(hasLineContaining(mesh.warnings, QStringLiteral("no UV map")), "LWOB warns that it has no UVs");
	fuzz("LWOB", QStringLiteral("models/test/old.lwo"), bytes, true);
}

// --- ASE ------------------------------------------------------------------------

const char kAseFixture[] = R"ASE(*3DSMAX_ASCIIEXPORT	200
*COMMENT "AsciiExport Version  2,00 - test fixture"
*SCENE {
	*SCENE_FILENAME "fixture.max"
	*SCENE_FIRSTFRAME 0
	*SCENE_LASTFRAME 100
}
*MATERIAL_LIST {
	*MATERIAL_COUNT 2
	*MATERIAL 0 {
		*MATERIAL_NAME "crate"
		*MATERIAL_CLASS "Standard"
		*MAP_DIFFUSE {
			*MAP_NAME "Map #1"
			*MAP_CLASS "Bitmap"
			*BITMAP "C:\Doom3\base\textures\test\crate.tga"
			*UVW_U_OFFSET 0.0000
			*UVW_V_OFFSET 0.0000
			*UVW_U_TILING 1.0000
			*UVW_V_TILING 1.0000
			*UVW_ANGLE 0.0000
		}
		*MAP_BUMP {
			*BITMAP "C:\Doom3\base\textures\test\crate_local.tga"
		}
	}
	*MATERIAL 1 {
		*MATERIAL_NAME "panel_multi"
		*MATERIAL_CLASS "Multi/Sub-Object"
		*NUMSUBMTLS 2
		*SUBMATERIAL 0 {
			*MATERIAL_NAME "models/panel/front"
			*MAP_DIFFUSE {
				*BITMAP "//server/doom/base/models/panel/front.tga"
			}
		}
		*SUBMATERIAL 1 {
			*MATERIAL_NAME "back"
			*MAP_DIFFUSE {
				*BITMAP "\\server\share\q4base\models\panel\back.tga"
			}
		}
	}
}
*GEOMOBJECT {
	*NODE_NAME "Box"
	*NODE_TM {
		*NODE_NAME "Box"
		*TM_ROW0 1.0000	0.0000	0.0000
		*TM_ROW1 0.0000	1.0000	0.0000
		*TM_ROW2 0.0000	0.0000	1.0000
		*TM_ROW3 0.0000	0.0000	0.0000
	}
	*MESH {
		*TIMEVALUE 0
		*MESH_NUMVERTEX 4
		*MESH_NUMFACES 2
		*MESH_VERTEX_LIST {
			*MESH_VERTEX    0	0.0000	0.0000	0.0000
			*MESH_VERTEX    1	64.0000	0.0000	0.0000
			*MESH_VERTEX    2	64.0000	32.0000	0.0000
			*MESH_VERTEX    3	0.0000	32.0000	0.0000
		}
		*MESH_FACE_LIST {
			*MESH_FACE    0:    A:    0 B:    1 C:    2 AB:    1 BC:    1 CA:    0	 *MESH_SMOOTHING 1 	*MESH_MTLID 0
			*MESH_FACE    1:    A:    0 B:    2 C:    3 AB:    0 BC:    1 CA:    1	 *MESH_SMOOTHING 1 	*MESH_MTLID 0
		}
		*MESH_NUMTVERTEX 4
		*MESH_TVERTLIST {
			*MESH_TVERT 0	0.0000	0.0000	0.0000
			*MESH_TVERT 1	1.0000	0.0000	0.0000
			*MESH_TVERT 2	1.0000	0.5000	0.0000
			*MESH_TVERT 3	0.0000	0.5000	0.0000
		}
		*MESH_NUMTVFACES 2
		*MESH_TFACELIST {
			*MESH_TFACE 0	0	1	2
			*MESH_TFACE 1	0	2	3
		}
		*MESH_NUMCVERTEX 0
		*MESH_NORMALS {
			*MESH_FACENORMAL 0	0.0000	0.0000	1.0000
				*MESH_VERTEXNORMAL 0	0.0000	0.0000	1.0000
				*MESH_VERTEXNORMAL 1	0.0000	0.0000	1.0000
				*MESH_VERTEXNORMAL 2	0.0000	0.0000	1.0000
			*MESH_FACENORMAL 1	0.0000	0.0000	1.0000
				*MESH_VERTEXNORMAL 0	0.0000	0.0000	1.0000
				*MESH_VERTEXNORMAL 2	0.0000	0.0000	1.0000
				*MESH_VERTEXNORMAL 3	0.0000	0.0000	1.0000
		}
	}
	*MESH_ANIMATION {
		*MESH {
			*TIMEVALUE 160
			*MESH_NUMVERTEX 0
			*MESH_NUMFACES 0
		}
	}
	*PROP_MOTIONBLUR 0
	*PROP_CASTSHADOW 1
	*PROP_RECVSHADOW 1
	*MATERIAL_REF 0
}
*GEOMOBJECT {
	*NODE_NAME "Panel"
	*NODE_TM {
		*TM_ROW0 1 0 0
		*TM_ROW1 0 1 0
		*TM_ROW2 0 0 1
	}
	*MESH {
		*MESH_NUMVERTEX 4
		*MESH_NUMFACES 2
		*MESH_VERTEX_LIST {
			*MESH_VERTEX 0 0 0 0
			*MESH_VERTEX 1 0 0 16
			*MESH_VERTEX 2 16 0 16
			*MESH_VERTEX 3 16 0 0
		}
		*MESH_FACE_LIST {
			*MESH_FACE 0: A: 0 B: 3 C: 2 AB: 1 BC: 1 CA: 0 *MESH_SMOOTHING 1 *MESH_MTLID 0
			*MESH_FACE 1: A: 0 B: 2 C: 1 AB: 0 BC: 1 CA: 1 *MESH_SMOOTHING 1 *MESH_MTLID 1
		}
		*MESH_NUMTVERTEX 4
		*MESH_TVERTLIST {
			*MESH_TVERT 0 0 0 0
			*MESH_TVERT 1 0 1 0
			*MESH_TVERT 2 1 1 0
			*MESH_TVERT 3 1 0 0
		}
		*MESH_NUMTVFACES 2
		*MESH_TFACELIST {
			*MESH_TFACE 0 0 3 2
			*MESH_TFACE 1 0 2 1
		}
	}
	*MATERIAL_REF 1
}
)ASE";

QByteArray aseWithBitmap(const QByteArray& bitmap)
{
	QByteArray text = "*3DSMAX_ASCIIEXPORT 200\n*MATERIAL_LIST {\n*MATERIAL 0 {\n*MATERIAL_NAME \"crate\"\n*MAP_DIFFUSE {\n*BITMAP \"";
	text += bitmap;
	text += "\"\n}\n}\n}\n*GEOMOBJECT {\n*NODE_NAME \"One\"\n*MESH {\n*MESH_NUMVERTEX 3\n*MESH_NUMFACES 1\n"
		"*MESH_VERTEX_LIST {\n*MESH_VERTEX 0 0 0 0\n*MESH_VERTEX 1 1 0 0\n*MESH_VERTEX 2 0 1 0\n}\n"
		"*MESH_FACE_LIST {\n*MESH_FACE 0: A: 0 B: 1 C: 2 *MESH_MTLID 0\n}\n}\n*MATERIAL_REF 0\n}\n";
	return text;
}

void runAseSmoke()
{
	const QByteArray bytes(kAseFixture);
	const QString path = QStringLiteral("models/test/fixture.ase");
	const ModelMesh mesh = decodeModelMesh(path, bytes);
	if (!expect(mesh.error.isEmpty(), "ASE fixture decodes")) {
		std::cerr << "  " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::Ase && mesh.version == 200 && mesh.frames.size() == 1, "ASE is detected with one frame");
	expect(mesh.surfaces.size() == 3, "ASE splits the multi-material object by sub-material");
	const ModelSurface* box = findSurface(mesh, QStringLiteral("Box"));
	const ModelSurface* front = findSurface(mesh, QStringLiteral("Panel_0"));
	const ModelSurface* back = findSurface(mesh, QStringLiteral("Panel_1"));
	if (!expect(box && front && back, "ASE surfaces are named after objects and sub-materials")) {
		return;
	}
	expect(box->skinPaths == QStringList(QStringLiteral("textures/test/crate")), "ASE bitmap after base/ becomes the material");
	expect(front->skinPaths == QStringList(QStringLiteral("models/panel/front")), "ASE UNC bitmap path after base/");
	expect(back->skinPaths == QStringList(QStringLiteral("models/panel/back")), "ASE Quake 4 q4base folder is recognised");
	expect(box->vertexCount == 4 && box->triangles.size() == 2, "ASE box has four vertices and two triangles");
	expect(countVertex(*box, {0, 0, 0}, 0.0f, 1.0f) == 1, "ASE TVERT v is inverted");
	expect(countVertex(*box, {64, 0, 0}, 1.0f, 1.0f) == 1, "ASE vertex 1 UV");
	expect(countVertex(*box, {64, 32, 0}, 1.0f, 0.5f) == 1, "ASE vertex 2 UV");
	for (const ModelTriangle& t : box->triangles) {
		const ModelVec3 c = faceCross(*box, t);
		expect(c.z > 0.0f && nearly(c.x, 0.0f) && nearly(c.y, 0.0f), "ASE box faces +Z counter-clockwise");
	}
	expect(box->triangles.constFirst().a == 0 && box->triangles.constFirst().b == 1 && box->triangles.constFirst().c == 2, "ASE keeps A B C order");
	for (const ModelVec3& n : box->frames.constFirst().normals) {
		expect(nearVec(n, 0, 0, 1), "ASE explicit normals are kept");
	}
	expect(front->triangles.size() == 1 && back->triangles.size() == 1, "ASE sub-material faces are split");
	for (const ModelSurface* panel : {front, back}) {
		const ModelVec3 c = faceCross(*panel, panel->triangles.constFirst());
		expect(c.y < 0.0f && nearly(c.x, 0.0f) && nearly(c.z, 0.0f), "ASE panel faces -Y");
		for (const ModelVec3& n : panel->frames.constFirst().normals) {
			expect(nearVec(n, 0, -1, 0), "ASE rebuilt normals face -Y");
		}
	}
	expect(countVertex(*front, {16, 0, 16}, 1.0f, 0.0f) == 1, "ASE panel UV");
	expect(hasLineContaining(mesh.warnings, QStringLiteral("multi-material")), "ASE warns that Doom 3 draws a multi-material object with one material");
	expect(hasLineContaining(mesh.detailLines, QStringLiteral("animation mesh sample")), "ASE notes the ignored animation samples");
	expect(mesh.skinPaths.size() == 3, "ASE lists each material once");

	// Doom 3 stops a bitmap path at its first space and needs a base folder;
	// q3map2 puts a bare name in the model's folder.
	const ModelMesh spaced = decodeModelMesh(QStringLiteral("models/test/one.ase"), aseWithBitmap("D:\\art\\my crate.tga"));
	if (expect(spaced.error.isEmpty() && spaced.surfaces.size() == 1, "ASE single object decodes")) {
		expect(spaced.surfaces.constFirst().skinPaths == QStringList(QStringLiteral("art/my")), "ASE bitmap is cut at its space, as Doom 3 reads it");
		expect(hasLineContaining(spaced.warnings, QStringLiteral("space")), "ASE warns about the space");
		expect(hasLineContaining(spaced.warnings, QStringLiteral("no base folder")), "ASE warns about the missing base folder");
		expect(hasLineContaining(spaced.detailLines, QStringLiteral("models/test/my crate")), "ASE reports q3map2's shader name");
		const ModelVec3 c = faceCross(spaced.surfaces.constFirst(), spaced.surfaces.constFirst().triangles.constFirst());
		expect(c.z > 0.0f, "ASE file without UVs still faces +Z");
	}
	const ModelMesh pk4 = decodeModelMesh(QStringLiteral("models/test/one.ase"), aseWithBitmap("C:/games/base/pak000.pk4/textures/x/y.tga"));
	expect(pk4.error.isEmpty() && pk4.surfaces.constFirst().skinPaths == QStringList(QStringLiteral("textures/x/y")), "ASE skips a .pk4 component after base/");

	// Export round trip.
	QString error;
	const QByteArray exported = exportModelAse(mesh, 0, &error);
	expect(error.isEmpty() && !exported.isEmpty(), "ASE export of the decoded fixture");
	expect(exported.contains("*BITMAP \"/base/textures/test/crate.tga\""), "ASE export writes the bitmap under base/");
	const ModelMesh again = decodeModelMesh(QStringLiteral("models/test/roundtrip.ase"), exported);
	if (expect(again.error.isEmpty() && again.surfaces.size() == mesh.surfaces.size(), "ASE export decodes again")) {
		bool same = true;
		for (int index = 0; index < mesh.surfaces.size(); ++index) {
			const ModelSurface& a = mesh.surfaces.at(index);
			const ModelSurface& b = again.surfaces.at(index);
			same &= a.name == b.name && a.skinPaths == b.skinPaths && a.triangles.size() == b.triangles.size() && a.vertexCount == b.vertexCount;
			for (int t = 0; same && t < a.triangles.size(); ++t) {
				const int ac[3] = {a.triangles.at(t).a, a.triangles.at(t).b, a.triangles.at(t).c};
				const int bc[3] = {b.triangles.at(t).a, b.triangles.at(t).b, b.triangles.at(t).c};
				for (int corner = 0; corner < 3; ++corner) {
					const ModelVec3& pa = a.frames.constFirst().positions.at(ac[corner]);
					const ModelVec3& pb = b.frames.constFirst().positions.at(bc[corner]);
					same &= pa.x == pb.x && pa.y == pb.y && pa.z == pb.z;
					same &= a.texCoords.at(ac[corner]).u == b.texCoords.at(bc[corner]).u && a.texCoords.at(ac[corner]).v == b.texCoords.at(bc[corner]).v;
					same &= dot(a.frames.constFirst().normals.at(ac[corner]), b.frames.constFirst().normals.at(bc[corner])) > 0.999f;
				}
			}
		}
		expect(same, "ASE round trip keeps names, skins, positions, UVs, winding and normals");
	}

	// A mesh built in code with awkward floats round-trips bit for bit.
	ModelMesh source;
	source.geometryAvailable = true;
	ModelFrameInfo frame;
	frame.index = 0;
	frame.name = QStringLiteral("frame0");
	source.frames.append(frame);
	ModelSurface surface;
	surface.index = 0;
	surface.name = QStringLiteral("thing");
	surface.skinPaths = QStringList(QStringLiteral("models/mapobjects/thing/skin"));
	surface.vertexCount = 4;
	ModelFrameGeometry geometry;
	geometry.positions = {{-1.0f / 3.0f, 2.5e-3f, 1000.125f}, {17.1f, -0.1f, 3.0f}, {17.1f, 9.9f, 3.0f}, {-1.0f / 3.0f, 9.9f, 1000.125f}};
	geometry.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
	surface.frames.append(geometry);
	surface.texCoords = {{0.1f, 0.3f}, {1.0f / 3.0f, 0.7f}, {0.999f, 1.0e-5f}, {-2.25f, 3.125f}};
	surface.triangles = {{0, 1, 2}, {0, 2, 3}};
	source.surfaces.append(surface);
	const QByteArray built = exportModelAse(source, 0, &error);
	const ModelMesh builtBack = decodeModelMesh(QStringLiteral("models/mapobjects/thing/thing.ase"), built);
	if (expect(builtBack.error.isEmpty() && builtBack.surfaces.size() == 1, "ASE export of a built mesh decodes")) {
		const ModelSurface& back2 = builtBack.surfaces.constFirst();
		bool exact = back2.vertexCount == 4 && back2.triangles.size() == 2 && back2.skinPaths == surface.skinPaths && back2.name == surface.name;
		for (int t = 0; exact && t < 2; ++t) {
			const int sc[3] = {surface.triangles.at(t).a, surface.triangles.at(t).b, surface.triangles.at(t).c};
			const int bc[3] = {back2.triangles.at(t).a, back2.triangles.at(t).b, back2.triangles.at(t).c};
			for (int corner = 0; corner < 3; ++corner) {
				const ModelVec3& ps = geometry.positions.at(sc[corner]);
				const ModelVec3& pb = back2.frames.constFirst().positions.at(bc[corner]);
				exact &= ps.x == pb.x && ps.y == pb.y && ps.z == pb.z;
				exact &= surface.texCoords.at(sc[corner]).u == back2.texCoords.at(bc[corner]).u;
				exact &= surface.texCoords.at(sc[corner]).v == back2.texCoords.at(bc[corner]).v;
			}
		}
		expect(exact, "ASE round trip of a built mesh is exact");
	}
	expect(exportModelAse(source, 3, &error).isEmpty() && !error.isEmpty(), "ASE export refuses a missing frame");

	// Malformed input.
	QByteArray unbalanced = bytes;
	unbalanced.chop(3);
	expect(!decodeModelMesh(path, unbalanced).error.isEmpty(), "ASE unbalanced braces are an error");
	expect(!decodeModelMesh(path, QByteArray("*3DSMAX_ASCIIEXPORT 200\n}\n")).error.isEmpty(), "ASE stray closing brace is an error");
	QByteArray oversize = bytes;
	oversize.replace("*MESH_NUMVERTEX 4", "*MESH_NUMVERTEX 999999999");
	expect(!decodeModelMesh(path, oversize).error.isEmpty(), "ASE oversize vertex count is an error");
	QByteArray badIndex = bytes;
	badIndex.replace("A: 0 B: 3 C: 2", "A: 0 B: 9 C: 2");
	expect(badIndex != bytes && !decodeModelMesh(path, badIndex).error.isEmpty(), "ASE face index beyond the vertex list is an error");
	QByteArray badTFaces = bytes;
	badTFaces.replace("*MESH_NUMTVFACES 2\n\t\t*MESH_TFACELIST {\n\t\t\t*MESH_TFACE 0 0 3 2", "*MESH_NUMTVFACES 1\n\t\t*MESH_TFACELIST {\n\t\t\t*MESH_TFACE 0 0 3 2");
	expect(badTFaces != bytes && !decodeModelMesh(path, badTFaces).error.isEmpty(), "ASE texture face count unlike the face count is an error");
	QByteArray deep("*3DSMAX_ASCIIEXPORT 200\n*GEOMOBJECT {\n");
	for (int level = 0; level < 200; ++level) {
		deep += "*MESH_ANIMATION {\n*X {\n";
	}
	expect(!decodeModelMesh(path, deep).error.isEmpty(), "ASE deep unbalanced nesting is an error");
	fuzz("ASE", path, bytes, false);
	fuzz("ASE export", QStringLiteral("models/test/roundtrip.ase"), exported, false, {}, 7);
}

// --- KVX ------------------------------------------------------------------------

void putI32LE(QByteArray& bytes, qint32 value)
{
	const quint32 raw = quint32(value);
	for (int shift = 0; shift < 32; shift += 8) {
		bytes.append(char((raw >> shift) & 0xFF));
	}
}

void putU16LE(QByteArray& bytes, quint32 value)
{
	bytes.append(char(value & 0xFF));
	bytes.append(char((value >> 8) & 0xFF));
}

struct KvxSlab {
	int top;
	int length;
	int visible;
	QVector<int> colours;
};

// A 2x2x2 block missing its top (z = 0) voxel at x = 1, y = 1. Top layer
// colour 17, bottom 34, except voxel (1, 0, 1), colour 200. Visibility bits as
// a voxel editor sets them: 1 -x, 2 +x, 4 -y, 8 +y, 16 top, 32 bottom.
bool kvxSolid(int x, int y, int z)
{
	if (x < 0 || y < 0 || z < 0 || x > 1 || y > 1 || z > 1) {
		return false;
	}
	return !(x == 1 && y == 1 && z == 0);
}

using KvxColumns = QVector<KvxSlab>[2][2];

const KvxColumns kHoleBlock = {
	{{{0, 2, 1 | 4 | 16 | 32, {17, 34}}}, {{0, 1, 1 | 2 | 8 | 16, {17}}, {1, 1, 1 | 8 | 32, {34}}}},
	{{{0, 1, 2 | 4 | 8 | 16, {17}}, {1, 1, 2 | 4 | 32, {200}}}, {{1, 1, 2 | 8 | 16 | 32, {34}}}},
};

// A solid 2x2x2 cube of one colour: each side is one 2x2 rectangle.
const KvxColumns kSolidBlock = {
	{{{0, 2, 1 | 4 | 16 | 32, {5, 5}}}, {{0, 2, 1 | 8 | 16 | 32, {5, 5}}}},
	{{{0, 2, 2 | 4 | 16 | 32, {5, 5}}}, {{0, 2, 2 | 8 | 16 | 32, {5, 5}}}},
};

QByteArray buildKvx(bool junkBeforePalette = false, const KvxColumns& columns = kHoleBlock)
{
	const int xsiz = 2;
	const int ysiz = 2;
	const int offsetBytes = (xsiz + 1) * 4 + xsiz * (ysiz + 1) * 2;
	QByteArray slabs;
	QVector<int> xoffset;
	QVector<int> xyoffset;
	for (int x = 0; x < xsiz; ++x) {
		xoffset.append(offsetBytes + int(slabs.size()));
		const int columnStart = int(slabs.size());
		for (int y = 0; y < ysiz; ++y) {
			xyoffset.append(int(slabs.size()) - columnStart);
			for (const KvxSlab& slab : columns[x][y]) {
				slabs.append(char(slab.top));
				slabs.append(char(slab.length));
				slabs.append(char(slab.visible));
				for (int colour : slab.colours) {
					slabs.append(char(colour));
				}
			}
		}
		xyoffset.append(int(slabs.size()) - columnStart);
	}
	xoffset.append(offsetBytes + int(slabs.size()));
	QByteArray mip;
	putI32LE(mip, xsiz);
	putI32LE(mip, ysiz);
	putI32LE(mip, 2);
	putI32LE(mip, 256);
	putI32LE(mip, 256);
	putI32LE(mip, 512);
	for (int value : xoffset) {
		putI32LE(mip, value);
	}
	for (int value : xyoffset) {
		putU16LE(mip, quint32(value));
	}
	mip.append(slabs);
	QByteArray bytes;
	putI32LE(bytes, qint32(mip.size()));
	bytes.append(mip);
	if (junkBeforePalette) {
		bytes.append(char(0));
	}
	for (int index = 0; index < 256; ++index) {
		if (index == 200) {
			bytes.append(char(63));
			bytes.append(char(32));
			bytes.append(char(1));
			continue;
		}
		bytes.append(char(index % 64));
		bytes.append(char((index * 7) % 64));
		bytes.append(char(63 - (index % 64)));
	}
	return bytes;
}

// The inverse of the placement (x, y, z) -> (y - 1, x - 1, 2 - z) for pivot (1, 1, 2).
bool gameSolid(const ModelVec3& p)
{
	return kvxSolid(int(std::floor(p.y + 1.0f)), int(std::floor(p.x + 1.0f)), int(std::floor(2.0f - p.z)));
}

void runKvxSmoke()
{
	const QByteArray bytes = buildKvx();
	const QString path = QStringLiteral("voxels/block.kvx");
	const ModelMesh mesh = decodeModelMesh(path, bytes);
	if (!expect(mesh.error.isEmpty(), "KVX fixture decodes")) {
		std::cerr << "  " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::Kvx && mesh.geometryAvailable && mesh.frames.size() == 1, "KVX is detected by extension");
	expect(mesh.warnings.isEmpty(), "KVX fixture has no warnings");
	expect(mesh.surfaces.size() == 1 && mesh.skinPaths.isEmpty() && mesh.skinCount == 1, "KVX has one surface and one embedded skin");
	expect(mesh.embeddedSkins.size() == 1 && mesh.embeddedSkins.constFirst().name == QStringLiteral("palette")
		&& mesh.embeddedSkins.constFirst().image.size() == QSize(16, 16), "KVX palette is a 16x16 embedded skin");
	if (mesh.surfaces.isEmpty() || mesh.embeddedSkins.isEmpty()) {
		return;
	}
	const ModelSurface& surface = mesh.surfaces.constFirst();
	const QImage& palette = mesh.embeddedSkins.constFirst().image;
	expect(palette.pixel(8, 12) == qRgb(255, 130, 4), "KVX palette entry 200 scales 6-bit components");
	expect(palette.pixel(1, 1) == qRgb((17 << 2) | (17 >> 4), (55 << 2) | (55 >> 4), (46 << 2) | (46 >> 4)), "KVX palette entry 17");
	// 24 visible unit faces merge into 18 one-colour rectangles.
	expect(surface.triangles.size() == 36, "KVX merges coplanar same-colour faces into 18 quads");
	float area = 0.0f;
	bool outward = true;
	for (const ModelTriangle& t : surface.triangles) {
		const ModelVec3 c = faceCross(surface, t);
		const float length = std::sqrt(dot(c, c));
		area += 0.5f * length;
		const QVector<ModelVec3>& p = surface.frames.constFirst().positions;
		const ModelVec3 centre{(p.at(t.a).x + p.at(t.b).x + p.at(t.c).x) / 3.0f, (p.at(t.a).y + p.at(t.b).y + p.at(t.c).y) / 3.0f,
			(p.at(t.a).z + p.at(t.b).z + p.at(t.c).z) / 3.0f};
		const ModelVec3 n{c.x / length, c.y / length, c.z / length};
		outward &= !gameSolid({centre.x + 0.25f * n.x, centre.y + 0.25f * n.y, centre.z + 0.25f * n.z});
		outward &= gameSolid({centre.x - 0.25f * n.x, centre.y - 0.25f * n.y, centre.z - 0.25f * n.z});
		outward &= dot(surface.frames.constFirst().normals.at(t.a), n) > 0.999f;
	}
	expect(nearly(area, 24.0f), "KVX visible area is 24 unit faces");
	expect(outward, "KVX faces point out of the solid voxels, counter-clockwise");
	expect(nearVec(mesh.mins, -1, -1, 0) && nearVec(mesh.maxs, 1, 1, 2), "KVX pivot places the block's base centre at the origin");
	// Voxel (1, 0, 1) is colour 200: its bottom face lies at game X -1..0, Y 0..1, Z 0.
	bool foundColour = false;
	for (const ModelTriangle& t : surface.triangles) {
		const QVector<ModelVec3>& p = surface.frames.constFirst().positions;
		const ModelVec3 centre{(p.at(t.a).x + p.at(t.b).x + p.at(t.c).x) / 3.0f, (p.at(t.a).y + p.at(t.b).y + p.at(t.c).y) / 3.0f,
			(p.at(t.a).z + p.at(t.b).z + p.at(t.c).z) / 3.0f};
		if (nearly(centre.z, 0.0f) && centre.x < 0.0f && centre.y > 0.0f) {
			const ModelTexCoord& uv = surface.texCoords.at(t.a);
			foundColour |= nearly(uv.u, 8.5f / 16.0f) && nearly(uv.v, 12.5f / 16.0f);
		}
	}
	expect(foundColour, "KVX colour 200 maps to its palette texel centre");
	// The missing voxel leaves the top layer open at game X 0..1, Y 0..1, Z 1..2.
	expect(!gameSolid({0.5f, 0.5f, 1.5f}) && gameSolid({-0.5f, 0.5f, 1.5f}), "KVX fixture hole is where the test expects it");

	const ModelMesh solid = decodeModelMesh(path, buildKvx(false, kSolidBlock));
	if (expect(solid.error.isEmpty() && solid.surfaces.size() == 1, "KVX solid cube decodes")) {
		expect(solid.surfaces.constFirst().triangles.size() == 12, "KVX solid cube merges each side into one rectangle");
		expect(solid.surfaces.constFirst().vertexCount == 24, "KVX solid cube shares corners within each side");
	}

	const ModelMesh junk = decodeModelMesh(path, buildKvx(true));
	expect(junk.error.isEmpty() && hasLineContaining(junk.warnings, QStringLiteral("GZDoom")), "KVX warns when mip levels do not meet the palette");

	// Malformed input.
	QByteArray badColumn = bytes;
	badColumn[4 + 24 + 4] = char(0x70);
	expect(!decodeModelMesh(path, badColumn).error.isEmpty(), "KVX column offset out of range is an error");
	QByteArray badSlab = bytes;
	badSlab[4 + 24 + 12 + 2] = char(0x60);
	expect(!decodeModelMesh(path, badSlab).error.isEmpty(), "KVX slab offset out of range is an error");
	QByteArray badSize = bytes;
	badSize[3] = char(0x40);
	expect(!decodeModelMesh(path, badSize).error.isEmpty(), "KVX oversize mip level is an error");
	QByteArray huge = bytes;
	huge[4 + 3] = char(0x7F);
	expect(!decodeModelMesh(path, huge).error.isEmpty(), "KVX huge dimensions are an error");
	fuzz("KVX", path, bytes, true);
}

// A cancelled decode stops with an error and publishes nothing.
void runCancellationSmoke()
{
	ModelWorkControl control;
	int polls = 0;
	control.cancelled = [&polls]() { return ++polls > 2; };
	const struct {
		const char* path;
		QByteArray bytes;
	} cases[] = {
		{"models/test/room.lwo", buildLwo(false, false)},
		{"models/test/fixture.ase", QByteArray(kAseFixture)},
		{"voxels/block.kvx", buildKvx()},
	};
	for (const auto& entry : cases) {
		polls = 0;
		const ModelMesh mesh = decodeModelMesh(QString::fromLatin1(entry.path), entry.bytes, nullptr, control);
		expect(!mesh.error.isEmpty() && !mesh.geometryAvailable && mesh.surfaces.isEmpty(), "A cancelled decode reports an error and no geometry");
	}
	polls = 0;
	QString error;
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/fixture.ase"), QByteArray(kAseFixture));
	expect(exportModelAse(mesh, 0, &error, control).isEmpty() && !error.isEmpty(), "A cancelled ASE export reports an error");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	runLightWaveSmoke();
	runLwobSmoke();
	runAseSmoke();
	runKvxSmoke();
	runCancellationSmoke();
	if (g_failures > 0) {
		std::cerr << g_failures << " check(s) failed.\n";
		return EXIT_FAILURE;
	}
	std::cout << "model_format_static_smoke_test: all checks passed.\n";
	return EXIT_SUCCESS;
}
