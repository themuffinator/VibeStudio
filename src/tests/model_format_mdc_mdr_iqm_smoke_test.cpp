// MDC, MDR and IQM decoding, and IQM writing, on fixtures built byte by byte.

#include "core/model_iqm.h"
#include "core/model_mesh.h"
#include "core/model_skeleton.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace vibestudio;

namespace {

int failures = 0;

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cout << "FAIL: " << message << "\n";
		++failures;
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

void putU8(QByteArray& bytes, int value)
{
	bytes.append(char(quint8(value)));
}

void putU16(QByteArray& bytes, int value)
{
	const quint16 raw = quint16(value);
	bytes.append(char(raw & 0xFF));
	bytes.append(char((raw >> 8) & 0xFF));
}

void putI16(QByteArray& bytes, int value)
{
	putU16(bytes, int(quint16(qint16(value))));
}

void putI32(QByteArray& bytes, qint32 value)
{
	const quint32 raw = quint32(value);
	for (int shift = 0; shift < 32; shift += 8) {
		bytes.append(char((raw >> shift) & 0xFF));
	}
}

void putF32(QByteArray& bytes, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	putI32(bytes, qint32(raw));
}

void putVec(QByteArray& bytes, float x, float y, float z)
{
	putF32(bytes, x);
	putF32(bytes, y);
	putF32(bytes, z);
}

void putFixed(QByteArray& bytes, const char* name, int length)
{
	QByteArray padded = QByteArray(name).left(length);
	padded.append(QByteArray(length - padded.size(), '\0'));
	bytes.append(padded);
}

void patchI32(QByteArray& bytes, qint64 offset, qint32 value)
{
	const quint32 raw = quint32(value);
	for (int index = 0; index < 4; ++index) {
		bytes[qsizetype(offset + index)] = char((raw >> (index * 8)) & 0xFF);
	}
}

void patchI16(QByteArray& bytes, qint64 offset, int value)
{
	const quint16 raw = quint16(qint16(value));
	bytes[qsizetype(offset)] = char(raw & 0xFF);
	bytes[qsizetype(offset + 1)] = char((raw >> 8) & 0xFF);
}

ModelVec3 faceNormal(const ModelSurface& surface, int frame, int triangle)
{
	const ModelTriangle& t = surface.triangles.at(triangle);
	const QVector<ModelVec3>& p = surface.frames.at(frame).positions;
	const ModelVec3 u{p.at(t.b).x - p.at(t.a).x, p.at(t.b).y - p.at(t.a).y, p.at(t.b).z - p.at(t.a).z};
	const ModelVec3 v{p.at(t.c).x - p.at(t.a).x, p.at(t.c).y - p.at(t.a).y, p.at(t.c).z - p.at(t.a).z};
	return {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
}

// A failed decode publishes nothing; a successful one keeps every surface
// parallel with the frame list.
bool consistent(const ModelMesh& mesh)
{
	if (!mesh.error.isEmpty()) {
		return !mesh.geometryAvailable && mesh.surfaces.isEmpty() && mesh.frames.isEmpty() && mesh.skeleton.isEmpty();
	}
	if (mesh.geometryAvailable == mesh.surfaces.isEmpty()) {
		return false;
	}
	for (const ModelSurface& surface : mesh.surfaces) {
		if (surface.frames.size() != mesh.frames.size() || surface.texCoords.size() != surface.vertexCount) {
			return false;
		}
		for (const ModelFrameGeometry& geometry : surface.frames) {
			if (geometry.positions.size() != surface.vertexCount || geometry.normals.size() != surface.vertexCount) {
				return false;
			}
		}
		for (const ModelTriangle& t : surface.triangles) {
			if (t.a < 0 || t.b < 0 || t.c < 0 || t.a >= surface.vertexCount || t.b >= surface.vertexCount || t.c >= surface.vertexCount) {
				return false;
			}
		}
	}
	return true;
}

quint32 g_seed = 0x1234567u;

quint32 nextRandom()
{
	g_seed = g_seed * 1664525u + 1013904223u;
	return g_seed >> 8;
}

// Truncation at every byte and a few hundred seeded byte flips: never a
// crash, always an error or a consistent mesh.
void fuzz(const QString& path, const QByteArray& valid, const char* truncatedMessage, const char* flippedMessage)
{
	bool truncatedOk = true;
	for (int length = 0; length < valid.size(); ++length) {
		const ModelMesh mesh = decodeModelMesh(path, valid.left(length));
		truncatedOk = truncatedOk && !mesh.error.isEmpty() && !mesh.geometryAvailable && consistent(mesh);
	}
	expect(truncatedOk, truncatedMessage);
	bool flippedOk = true;
	for (int round = 0; round < 300; ++round) {
		QByteArray bytes = valid;
		const int flips = 1 + int(nextRandom() % 4);
		for (int flip = 0; flip < flips; ++flip) {
			// Keep the identifier so the right decoder runs.
			const int at = 4 + int(nextRandom() % quint32(bytes.size() - 4));
			bytes[at] = char(bytes.at(at) ^ char(1 + (nextRandom() % 255)));
		}
		flippedOk = flippedOk && consistent(decodeModelMesh(path, bytes));
	}
	expect(flippedOk, flippedMessage);
}

// ---------------------------------------------------------------------------
// MDC
// ---------------------------------------------------------------------------

constexpr int kMdcSurface = 400;
constexpr int kMdcBaseTable = kMdcSurface + 364;
constexpr int kMdcCompTable = kMdcSurface + 368;
const int kMdcBase[4][3] = {{0, 0, 0}, {1024, 0, 0}, {1024, 1024, 0}, {0, 1024, 0}};
const int kMdcComp[4][4] = {{147, 127, 87, 0}, {127, 137, 127, 8}, {0, 255, 127, 255}, {127, 127, 127, 144}};
const float kMdcSt[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
// 90 degrees in MDC_TAG_ANGLE_SCALE steps.
constexpr int kMdcQuarterTurn = 8175;

QByteArray buildMdc()
{
	QByteArray b;
	b.append("IDPC", 4);
	putI32(b, 2);
	putFixed(b, "models/test/thing.mdc", 64);
	putI32(b, 0);   // flags
	putI32(b, 2);   // numFrames
	putI32(b, 2);   // numTags
	putI32(b, 1);   // numSurfaces
	putI32(b, 1);   // numSkins
	putI32(b, 112); // ofsFrames
	putI32(b, 224); // ofsTagNames
	putI32(b, 352); // ofsTags
	putI32(b, kMdcSurface);
	putI32(b, 0); // ofsEnd, patched
	const char* frameNames[2] = {"idle01", "idle02"};
	for (const char* name : frameNames) {
		putVec(b, -8.0f, -1.0f, -3.0f);
		putVec(b, 17.0f, 23.0f, 1.0f);
		putVec(b, 0.0f, 0.0f, 0.0f);
		putF32(b, 20.0f);
		putFixed(b, name, 16);
	}
	putFixed(b, "tag_head", 64);
	putFixed(b, "tag_weapon", 64);
	// Frame 0: head at (0, 0, 16) turned 90 degrees of yaw; weapon at
	// (1, 0, 0) pitched 90 degrees. Frame 1: head at (0, 0, 17), unturned;
	// weapon at (2, 0, 0) with -90 degrees of yaw.
	const int tags[2][2][6] = {{{0, 0, 1024, 0, kMdcQuarterTurn, 0}, {64, 0, 0, kMdcQuarterTurn, 0, 0}},
		{{0, 0, 1088, 0, 0, 0}, {128, 0, 0, 0, -kMdcQuarterTurn, 0}}};
	for (const auto& frame : tags) {
		for (const auto& tag : frame) {
			for (const int value : tag) {
				putI16(b, value);
			}
		}
	}
	// mdcSurface_t
	b.append("IDPC", 4);
	putFixed(b, "body", 64);
	putI32(b, 0);   // flags
	putI32(b, 1);   // numCompFrames
	putI32(b, 1);   // numBaseFrames
	putI32(b, 2);   // numShaders
	putI32(b, 4);   // numVerts
	putI32(b, 2);   // numTriangles
	putI32(b, 260); // ofsTriangles
	putI32(b, 124); // ofsShaders
	putI32(b, 284); // ofsSt
	putI32(b, 316); // ofsXyzNormals
	putI32(b, 348); // ofsXyzCompressed
	putI32(b, 364); // ofsFrameBaseFrames
	putI32(b, 368); // ofsFrameCompFrames
	putI32(b, 372); // ofsEnd
	putFixed(b, "models/test/body", 64);
	putI32(b, 0);
	putFixed(b, "models/test/body_alt", 64);
	putI32(b, 0);
	// A square facing +Z, clockwise in the file as MD3 stores it.
	const int triangles[2][3] = {{0, 2, 1}, {0, 3, 2}};
	for (const auto& triangle : triangles) {
		for (const int index : triangle) {
			putI32(b, index);
		}
	}
	for (const auto& st : kMdcSt) {
		putF32(b, st[0]);
		putF32(b, st[1]);
	}
	for (const auto& base : kMdcBase) {
		putI16(b, base[0]);
		putI16(b, base[1]);
		putI16(b, base[2]);
		putU16(b, 0); // lat/lng 0: +Z
	}
	for (const auto& comp : kMdcComp) {
		putU8(b, comp[0]);
		putU8(b, comp[1]);
		putU8(b, comp[2]);
		putU8(b, comp[3]);
	}
	putI16(b, 0); // frameBaseFrames
	putI16(b, 0);
	putI16(b, -1); // frameCompFrames: frame 0 is base only
	putI16(b, 0);
	patchI32(b, 108, b.size());
	return b;
}

void runMdc()
{
	const QByteArray bytes = buildMdc();
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/thing.mdc"), bytes);
	if (!expect(mesh.error.isEmpty() && mesh.geometryAvailable, "The MDC fixture should decode with geometry.")) {
		std::cout << "  " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::Mdc && mesh.version == 2, "MDC format and version should be reported.");
	expect(mesh.frameCount == 2 && mesh.surfaceCount == 1 && mesh.vertexCount == 4 && mesh.triangleCount == 2, "MDC counts should decode.");
	expect(mesh.warnings.isEmpty(), "A clean MDC should not warn.");
	const ModelSurface& surface = mesh.surfaces.first();
	expect(surface.name == QStringLiteral("body"), "The MDC surface name should decode.");
	expect(surface.skinPaths == QStringList({QStringLiteral("models/test/body"), QStringLiteral("models/test/body_alt")}),
		"Both MDC shaders should become surface skin paths.");
	expect(mesh.skinPaths.size() == 2 && mesh.skinCount == 2, "MDC shaders should be collected as model skin paths.");
	bool uvs = true;
	for (int v = 0; v < 4; ++v) {
		uvs = uvs && nearly(surface.texCoords.at(v).u, kMdcSt[v][0]) && nearly(surface.texCoords.at(v).v, kMdcSt[v][1]);
	}
	expect(uvs, "MDC texture coordinates should decode as stored.");
	bool basePositions = true;
	bool compressedPositions = true;
	for (int v = 0; v < 4; ++v) {
		const float bx = float(kMdcBase[v][0]) / 64.0f, by = float(kMdcBase[v][1]) / 64.0f, bz = float(kMdcBase[v][2]) / 64.0f;
		basePositions = basePositions && nearVec(surface.frames.at(0).positions.at(v), bx, by, bz, 1.0e-6f);
		const float ox = float((kMdcComp[v][0] - 127) * 0.05), oy = float((kMdcComp[v][1] - 127) * 0.05), oz = float((kMdcComp[v][2] - 127) * 0.05);
		compressedPositions = compressedPositions && nearVec(surface.frames.at(1).positions.at(v), bx + ox, by + oy, bz + oz, 1.0e-5f);
	}
	expect(basePositions, "A base-only MDC frame should hold the base positions at 1/64 scale.");
	expect(compressedPositions, "A compressed MDC frame should hold base + offsets * MDC_DIST_SCALE.");
	expect(nearVec(surface.frames.at(1).positions.at(2), 9.65f, 22.4f, 0.0f, 1.0e-5f), "The extreme MDC offsets should decode to -127 and +128 steps.");
	bool baseNormals = true;
	for (int v = 0; v < 4; ++v) {
		baseNormals = baseNormals && nearVec(surface.frames.at(0).normals.at(v), 0.0f, 0.0f, 1.0f, 1.0e-6f);
	}
	expect(baseNormals, "A base-only MDC frame should use the latitude/longitude normals.");
	const QVector<ModelVec3>& normals = surface.frames.at(1).normals;
	expect(nearVec(normals.at(0), 1.0f, 0.0f, 0.0f, 2.0e-6f), "Compressed normal 0 should be +X, as in anorms256.h.");
	expect(nearVec(normals.at(1), 0.0f, 1.0f, 0.0f, 2.0e-6f), "Compressed normal 8 should be +Y, as in anorms256.h.");
	expect(nearVec(normals.at(2), 0.0f, -0.195090f, 0.980785f, 2.0e-6f), "Compressed normal 255 should match anorms256.h.");
	expect(nearVec(normals.at(3), 0.980785f, 0.0f, 0.195090f, 2.0e-6f), "Compressed normal 144 should start the northern rings.");
	const ModelVec3 front = faceNormal(surface, 0, 0);
	expect(front.z > 0.0f && nearly(front.x, 0.0f) && nearly(front.y, 0.0f), "MDC triangles should come out counter-clockwise (outward +Z).");
	expect(surface.triangles.at(0).a == 0 && surface.triangles.at(0).b == 1 && surface.triangles.at(0).c == 2, "MDC triangle order should be reversed.");

	expect(mesh.frames.at(0).name == QStringLiteral("idle01") && mesh.frames.at(1).name == QStringLiteral("idle02"), "MDC frame names should decode.");
	expect(nearly(mesh.frames.at(0).radius, 20.0f) && nearVec(mesh.frames.at(0).maxs, 17.0f, 23.0f, 1.0f), "MDC frame bounds should decode.");
	expect(mesh.animations.size() == 1 && mesh.animations.first().name == QStringLiteral("idle") && mesh.animations.first().frameCount == 2,
		"MDC frame names should infer one animation.");
	expect(mesh.tags.size() == 4 && mesh.tagCount == 2, "MDC tags should decode for every frame.");
	if (mesh.tags.size() == 4) {
		const ModelTag& head = mesh.tags.at(0);
		expect(head.name == QStringLiteral("tag_head") && head.frameIndex == 0 && nearVec(head.origin, 0.0f, 0.0f, 16.0f),
			"The MDC tag origin should decode at 1/64 scale.");
		expect(nearly(head.axis[0], 0.0f) && nearly(head.axis[1], 1.0f) && nearly(head.axis[2], 0.0f) && nearly(head.axis[3], -1.0f)
				&& nearly(head.axis[4], 0.0f) && nearly(head.axis[8], 1.0f),
			"90 degrees of MDC tag yaw should turn forward to +Y and left to -X.");
		const ModelTag& weapon = mesh.tags.at(1);
		expect(weapon.name == QStringLiteral("tag_weapon") && nearVec(weapon.origin, 1.0f, 0.0f, 0.0f)
				&& nearly(weapon.axis[0], 0.0f) && nearly(weapon.axis[1], 0.0f) && nearly(weapon.axis[2], -1.0f) && nearly(weapon.axis[6], 1.0f),
			"90 degrees of MDC tag pitch should point forward down and up forward.");
		const ModelTag& weapon1 = mesh.tags.at(3);
		expect(weapon1.frameIndex == 1 && nearVec(weapon1.origin, 2.0f, 0.0f, 0.0f) && nearly(weapon1.axis[1], -1.0f) && nearly(weapon1.axis[3], 1.0f),
			"Second-frame MDC tags should decode from their own record.");
	}
	bool saysCompressed = false;
	for (const QString& line : mesh.detailLines) {
		saysCompressed = saysCompressed || line.contains(QStringLiteral("1 of 2"));
	}
	expect(saysCompressed, "The MDC detail lines should count the compressed frames.");

	// Hostile values.
	QByteArray badVersion = bytes;
	patchI32(badVersion, 4, 3);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), badVersion).error.isEmpty(), "A wrong MDC version should be rejected.");
	QByteArray badBase = bytes;
	patchI16(badBase, kMdcBaseTable + 2, 1);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), badBase).error.isEmpty(), "An MDC base frame index past the base frames should be rejected.");
	QByteArray badComp = bytes;
	patchI16(badComp, kMdcCompTable, 5);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), badComp).error.isEmpty(), "An MDC compressed frame index past the compressed frames should be rejected.");
	QByteArray negativeComp = bytes;
	patchI16(negativeComp, kMdcCompTable, -2);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), negativeComp).error.isEmpty(), "An MDC compressed frame index below -1 should be rejected.");
	QByteArray hugeFrames = bytes;
	patchI32(hugeFrames, 76, 1 << 30);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), hugeFrames).error.isEmpty(), "A huge MDC frame count should be rejected.");
	QByteArray negativeSurfaces = bytes;
	patchI32(negativeSurfaces, 84, -1);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), negativeSurfaces).error.isEmpty(), "A negative MDC surface count should be rejected.");
	QByteArray farFrames = bytes;
	patchI32(farFrames, 92, 1 << 24);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), farFrames).error.isEmpty(), "An MDC frame block outside the file should be rejected.");
	QByteArray hugeVerts = bytes;
	patchI32(hugeVerts, kMdcSurface + 84, 1 << 22);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), hugeVerts).error.isEmpty(), "An MDC surface with a huge vertex count should be rejected.");
	QByteArray outsideBlock = bytes;
	patchI32(outsideBlock, kMdcSurface + 108, 1 << 20);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), outsideBlock).error.isEmpty(), "An MDC block outside its surface record should be rejected.");
	QByteArray backwards = bytes;
	patchI32(backwards, kMdcSurface + 120, -16);
	expect(!decodeModelMesh(QStringLiteral("a.mdc"), backwards).error.isEmpty(), "A backwards MDC surface end should be rejected.");
	QByteArray longEnd = bytes;
	patchI32(longEnd, 108, bytes.size() + 4);
	const ModelMesh longEndMesh = decodeModelMesh(QStringLiteral("a.mdc"), longEnd);
	expect(!longEndMesh.error.isEmpty() && !longEndMesh.geometryAvailable, "An MDC end offset past the file should be rejected.");
	QByteArray trailing = bytes;
	trailing.append(QByteArray(8, '\x7f'));
	const ModelMesh trailingMesh = decodeModelMesh(QStringLiteral("a.mdc"), trailing);
	expect(trailingMesh.error.isEmpty() && trailingMesh.geometryAvailable && !trailingMesh.warnings.isEmpty(),
		"Bytes after the MDC end offset should be ignored with a warning.");
	QByteArray badIndex = bytes;
	patchI32(badIndex, kMdcSurface + 260, 9);
	const ModelMesh badIndexMesh = decodeModelMesh(QStringLiteral("a.mdc"), badIndex);
	expect(badIndexMesh.error.isEmpty() && badIndexMesh.surfaces.first().triangles.size() == 1 && !badIndexMesh.surfaces.first().warnings.isEmpty(),
		"An out-of-range MDC triangle index should drop that triangle with a warning.");
	fuzz(QStringLiteral("models/fuzz.mdc"), bytes, "Every truncated MDC should report an error.", "Flipped MDC bytes should give an error or a consistent mesh.");
}

// ---------------------------------------------------------------------------
// MDR
// ---------------------------------------------------------------------------

struct MdrLayout {
	int lod = 0;
	int surface = 0;
	int vertices = 0;
	int tags = 0;
};

// Bone 1 sits at (10, 0, 0); in frame 1 it turns 90 degrees about Z.
const float kMdrBones[2][2][12] = {
	{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, {1, 0, 0, 10, 0, 1, 0, 0, 0, 0, 1, 0}},
	{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}, {0, -1, 0, 10, 1, 0, 0, 0, 0, 0, 1, 0}},
};

QByteArray buildMdr(bool compressed, MdrLayout* layout = nullptr)
{
	QByteArray b;
	b.append("RDM5", 4);
	putI32(b, 2);
	putFixed(b, "models/test/thing.mdr", 64);
	putI32(b, 2); // numFrames
	putI32(b, 2); // numBones
	putI32(b, compressed ? -104 : 104);
	putI32(b, 1); // numLODs
	putI32(b, 0); // ofsLODs, patched
	putI32(b, 1); // numTags
	putI32(b, 0); // ofsTags, patched
	putI32(b, 0); // ofsEnd, patched
	const char* frameNames[2] = {"frameA", "frameB"};
	for (int frame = 0; frame < 2; ++frame) {
		putVec(b, -10.0f, -10.0f, -10.0f);
		putVec(b, 20.0f, 20.0f, 20.0f);
		putVec(b, 0.0f, 0.0f, 0.0f);
		putF32(b, 30.0f);
		if (!compressed) {
			putFixed(b, frameNames[frame], 16);
		}
		for (const auto& bone : kMdrBones[frame]) {
			if (!compressed) {
				for (const float value : bone) {
					putF32(b, value);
				}
				continue;
			}
			// MC_UnCompress order: translation, then the 3x3 rows.
			const int order[12] = {3, 7, 11, 0, 1, 2, 4, 5, 6, 8, 9, 10};
			for (int index = 0; index < 12; ++index) {
				const float value = bone[order[index]];
				putU16(b, index < 3 ? 32768 + int(value * 64.0f) : 32768 + int(value * 32766.0f));
			}
		}
	}
	const int lod = b.size();
	patchI32(b, 88, lod);
	putI32(b, 1);  // numSurfaces
	putI32(b, 12); // ofsSurfaces
	putI32(b, 0);  // ofsEnd, patched
	const int surface = b.size();
	b.append("RDM5", 4);
	putFixed(b, "body", 64);
	putFixed(b, "models/test/skin", 64);
	putI32(b, 0);        // shaderIndex
	putI32(b, -surface); // ofsHeader
	putI32(b, 3);        // numVerts
	putI32(b, 168);      // ofsVerts
	putI32(b, 1);        // numTriangles
	putI32(b, 0);        // ofsTriangles, patched
	putI32(b, 0);        // numBoneReferences
	putI32(b, 0);        // ofsBoneReferences
	putI32(b, 0);        // ofsEnd, patched
	const int vertices = b.size();
	// v0 follows bone 0; v1 follows bone 1; v2 splits 0.6 / 0.4, both
	// offsets placing it at (2, 0, 0) in the bind pose.
	putVec(b, 0.0f, 0.0f, 1.0f);
	putF32(b, 0.0f);
	putF32(b, 0.0f);
	putI32(b, 1);
	putI32(b, 0);
	putF32(b, 1.0f);
	putVec(b, 0.0f, 0.0f, 5.0f);
	putVec(b, 0.0f, 0.0f, 1.0f);
	putF32(b, 1.0f);
	putF32(b, 0.0f);
	putI32(b, 1);
	putI32(b, 1);
	putF32(b, 1.0f);
	putVec(b, 4.0f, 0.0f, 0.0f);
	putVec(b, 0.0f, 0.0f, 1.0f);
	putF32(b, 0.5f);
	putF32(b, 1.0f);
	putI32(b, 2);
	putI32(b, 0);
	putF32(b, 0.6f);
	putVec(b, 2.0f, 0.0f, 0.0f);
	putI32(b, 1);
	putF32(b, 0.4f);
	putVec(b, -8.0f, 0.0f, 0.0f);
	patchI32(b, surface + 152, b.size() - surface);
	putI32(b, 0);
	putI32(b, 1);
	putI32(b, 2);
	patchI32(b, surface + 164, b.size() - surface);
	patchI32(b, lod + 8, b.size() - lod);
	const int tags = b.size();
	patchI32(b, 96, tags);
	putI32(b, 1);
	putFixed(b, "tag_hand", 32);
	patchI32(b, 100, b.size());
	if (layout) {
		*layout = MdrLayout{lod, surface, vertices, tags};
	}
	return b;
}

void checkMdr(const ModelMesh& mesh, bool compressed)
{
	if (!expect(mesh.error.isEmpty() && mesh.geometryAvailable, "The MDR fixture should decode with geometry.")) {
		std::cout << "  " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::Mdr && mesh.version == 2, "MDR format and version should be reported.");
	const ModelSkeleton& skeleton = mesh.skeleton;
	expect(skeleton.sourceFormat == QStringLiteral("mdr") && skeleton.joints.size() == 2, "MDR bones should become joints.");
	if (skeleton.joints.size() == 2) {
		expect(skeleton.joints.at(0).name == QStringLiteral("bone0") && skeleton.joints.at(1).name == QStringLiteral("bone1")
				&& skeleton.joints.at(0).parent == -1 && skeleton.joints.at(1).parent == -1,
			"MDR joints should be roots named after their index.");
		expect(nearly(skeleton.joints.at(1).bind.m[3], 10.0f), "The MDR bind pose should be the first frame.");
	}
	expect(skeleton.clips.size() == 1 && skeleton.clips.first().name == QStringLiteral("all") && skeleton.clips.first().frames.size() == 2
			&& skeleton.clips.first().framesPerSecond == 0.0,
		"MDR frames should form one untimed clip.");
	expect(mesh.frameCount == 2 && mesh.animations.size() == 1 && mesh.animations.first().firstFrame == 0 && mesh.animations.first().frameCount == 2,
		"An MDR should bake its frames without a separate bind pose.");
	expect(mesh.surfaceCount == 1 && mesh.vertexCount == 3 && mesh.triangleCount == 1, "MDR counts should decode.");
	if (mesh.surfaces.isEmpty() || mesh.surfaces.first().frames.size() != 2) {
		return;
	}
	const ModelSurface& surface = mesh.surfaces.first();
	expect(surface.name == QStringLiteral("body") && surface.skinPaths == QStringList({QStringLiteral("models/test/skin")}),
		"The MDR surface name and shader should decode.");
	expect(nearly(surface.texCoords.at(2).u, 0.5f) && nearly(surface.texCoords.at(2).v, 1.0f), "MDR texture coordinates should decode.");
	const QVector<ModelVec3>& bind = surface.frames.at(0).positions;
	const QVector<ModelVec3>& turned = surface.frames.at(1).positions;
	expect(nearVec(bind.at(0), 0.0f, 0.0f, 5.0f) && nearVec(bind.at(1), 14.0f, 0.0f, 0.0f) && nearVec(bind.at(2), 2.0f, 0.0f, 0.0f),
		"MDR first-frame positions should be the weighted bone offsets.");
	expect(nearVec(turned.at(0), 0.0f, 0.0f, 5.0f), "A vertex on the still bone should not move.");
	expect(nearVec(turned.at(1), 10.0f, 4.0f, 0.0f), "A vertex on the turned bone should turn 90 degrees about it.");
	expect(nearVec(turned.at(2), 5.2f, -3.2f, 0.0f), "A split-weight MDR vertex should blend both bones.");
	expect(nearVec(surface.frames.at(1).normals.at(1), 0.0f, 0.0f, 1.0f), "MDR normals should follow the bones.");
	expect(surface.triangles.first().a == 0 && surface.triangles.first().b == 2 && surface.triangles.first().c == 1,
		"MDR triangle order should be reversed for counter-clockwise fronts.");
	expect(faceNormal(surface, 0, 0).y < 0.0f, "The reversed MDR triangle should face -Y.");
	expect(mesh.tags.size() == 2 && mesh.tagCount == 1, "The MDR tag should bake into every frame.");
	if (mesh.tags.size() == 2) {
		const ModelTag& tag = mesh.tags.at(1);
		expect(tag.name == QStringLiteral("tag_hand") && tag.frameIndex == 1 && nearVec(tag.origin, 10.0f, 0.0f, 0.0f),
			"The MDR tag should follow its bone.");
		expect(nearly(tag.axis[1], 1.0f) && nearly(tag.axis[3], -1.0f) && nearly(tag.axis[8], 1.0f),
			"MDR tag axes should be the bone matrix columns.");
	}
	bool saysCompressed = false;
	for (const QString& line : mesh.detailLines) {
		saysCompressed = saysCompressed || line.contains(QStringLiteral("compressed"));
	}
	expect(saysCompressed == compressed, "The MDR detail lines should say whether frames are compressed.");
}

void runMdr()
{
	MdrLayout layout;
	const QByteArray bytes = buildMdr(false, &layout);
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/thing.mdr"), bytes);
	checkMdr(mesh, false);
	const QByteArray packed = buildMdr(true);
	checkMdr(decodeModelMesh(QStringLiteral("models/test/packed.mdr"), packed), true);

	QByteArray badVersion = bytes;
	patchI32(badVersion, 4, 3);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), badVersion).error.isEmpty(), "A wrong MDR version should be rejected.");
	QByteArray noBones = bytes;
	patchI32(noBones, 76, 0);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), noBones).error.isEmpty(), "An MDR without bones should be rejected.");
	QByteArray noLods = bytes;
	patchI32(noLods, 84, 0);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), noLods).error.isEmpty(), "An MDR without a level of detail should be rejected.");
	QByteArray hugeFrames = bytes;
	patchI32(hugeFrames, 72, 1 << 30);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), hugeFrames).error.isEmpty(), "A huge MDR frame count should be rejected.");
	QByteArray minFrames = bytes;
	patchI32(minFrames, 80, std::numeric_limits<qint32>::min());
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), minFrames).error.isEmpty(), "The most negative MDR frame offset should be rejected.");
	QByteArray badBone = bytes;
	patchI32(badBone, layout.vertices + 24, 2);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), badBone).error.isEmpty(), "An MDR weight on a missing bone should be rejected.");
	QByteArray negativeWeights = bytes;
	patchI32(negativeWeights, layout.vertices + 20, -1);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), negativeWeights).error.isEmpty(), "A negative MDR weight count should be rejected.");
	QByteArray manyWeights = bytes;
	patchI32(manyWeights, layout.vertices + 20, 60);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), manyWeights).error.isEmpty(), "MDR weights past the surface record should be rejected.");
	QByteArray badTag = bytes;
	patchI32(badTag, layout.tags, 7);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), badTag).error.isEmpty(), "An MDR tag on a missing bone should be rejected.");
	QByteArray badLod = bytes;
	patchI32(badLod, 88, 1 << 26);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), badLod).error.isEmpty(), "An MDR level of detail outside the file should be rejected.");
	QByteArray hugeVerts = bytes;
	patchI32(hugeVerts, layout.surface + 140, 1 << 21);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), hugeVerts).error.isEmpty(), "An MDR surface with a huge vertex count should be rejected.");
	QByteArray longEnd = bytes;
	patchI32(longEnd, 100, bytes.size() + 1);
	expect(!decodeModelMesh(QStringLiteral("a.mdr"), longEnd).error.isEmpty(), "An MDR end offset past the file should be rejected.");
	fuzz(QStringLiteral("models/fuzz.mdr"), bytes, "Every truncated MDR should report an error.", "Flipped MDR bytes should give an error or a consistent mesh.");
	fuzz(QStringLiteral("models/fuzz.mdr"), packed, "Every truncated compressed MDR should report an error.",
		"Flipped compressed MDR bytes should give an error or a consistent mesh.");
}

// ---------------------------------------------------------------------------
// IQM
// ---------------------------------------------------------------------------

struct IqmLayout {
	int meshes = 0;
	int joints = 0;
	int blendIndexes = 0;
	int text = 0;
};

constexpr float kHalfTurnSine = 0.70710678f;
const float kIqmPositions[3][3] = {{0.0f, 0.0f, 0.0f}, {14.0f, 0.0f, 0.0f}, {12.0f, 0.0f, 4.0f}};
const float kIqmUvs[3][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}};

QByteArray buildIqm(bool skeletal, IqmLayout* layout = nullptr)
{
	QByteArray b(124, '\0');
	std::memcpy(b.data(), "INTERQUAKEMODEL\0", 16);
	const auto field = [&b](int index, qint32 value) { patchI32(b, 16 + (index * 4), value); };
	const auto align = [&b] {
		while ((b.size() & 3) != 0) {
			b.append('\0');
		}
	};
	field(0, 2);
	// Text: "", body, material, root, arm, wave.
	const int text = b.size();
	QByteArray strings("\0body\0models/test/iqmskin\0root\0arm\0wave\0", 40);
	b.append(strings);
	field(3, strings.size());
	field(4, text);
	const qint32 nameBody = 1, nameMaterial = 6, nameRoot = 26, nameArm = 31, nameWave = 35;
	align();
	const int meshes = b.size();
	field(5, 1);
	field(6, meshes);
	putI32(b, nameBody);
	putI32(b, nameMaterial);
	putI32(b, 0);
	putI32(b, 3);
	putI32(b, 0);
	putI32(b, 1);
	// Vertex arrays: position, texcoord, normal (float) and ubyte blends.
	const int arrayCount = skeletal ? 5 : 3;
	const int arrays = b.size();
	field(7, arrayCount);
	field(8, 3);
	field(9, arrays);
	const int specs[5][3] = {{0, 7, 3}, {1, 7, 2}, {2, 7, 3}, {4, 1, 4}, {5, 1, 4}};
	for (int index = 0; index < arrayCount; ++index) {
		putI32(b, specs[index][0]);
		putI32(b, 0);
		putI32(b, specs[index][1]);
		putI32(b, specs[index][2]);
		putI32(b, 0);
	}
	patchI32(b, arrays + 16, b.size());
	for (const auto& p : kIqmPositions) {
		putVec(b, p[0], p[1], p[2]);
	}
	patchI32(b, arrays + 36, b.size());
	for (const auto& uv : kIqmUvs) {
		putF32(b, uv[0]);
		putF32(b, uv[1]);
	}
	patchI32(b, arrays + 56, b.size());
	for (int v = 0; v < 3; ++v) {
		putVec(b, 0.0f, -1.0f, 0.0f);
	}
	int blendIndexes = 0;
	if (skeletal) {
		blendIndexes = b.size();
		patchI32(b, arrays + 76, blendIndexes);
		const int indexes[3][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 0}};
		for (const auto& vertex : indexes) {
			for (const int value : vertex) {
				putU8(b, value);
			}
		}
		patchI32(b, arrays + 96, b.size());
		const int weights[3][4] = {{255, 0, 0, 0}, {255, 0, 0, 0}, {153, 102, 0, 0}};
		for (const auto& vertex : weights) {
			for (const int value : vertex) {
				putU8(b, value);
			}
		}
	}
	align();
	// One triangle, clockwise in the file.
	field(10, 1);
	field(11, b.size());
	putI32(b, 0);
	putI32(b, 2);
	putI32(b, 1);
	int joints = 0;
	if (skeletal) {
		joints = b.size();
		field(13, 2);
		field(14, joints);
		putI32(b, nameRoot);
		putI32(b, -1);
		putVec(b, 0.0f, 0.0f, 0.0f);
		putF32(b, 0.0f);
		putF32(b, 0.0f);
		putF32(b, 0.0f);
		putF32(b, 1.0f);
		putVec(b, 1.0f, 1.0f, 1.0f);
		putI32(b, nameArm);
		putI32(b, 0);
		putVec(b, 10.0f, 0.0f, 0.0f);
		putF32(b, 0.0f);
		putF32(b, 0.0f);
		putF32(b, 0.0f);
		putF32(b, 1.0f);
		putVec(b, 1.0f, 1.0f, 1.0f);
		// Poses: the root is constant; the arm animates its rotation's z and w.
		field(15, 2);
		field(16, b.size());
		const float rootOffset[10] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1};
		putI32(b, -1);
		putI32(b, 0);
		for (const float value : rootOffset) {
			putF32(b, value);
		}
		for (int channel = 0; channel < 10; ++channel) {
			putF32(b, 0.0f);
		}
		const float armOffset[10] = {10, 0, 0, 0, 0, 0, 1, 1, 1, 1};
		const float armScale[10] = {0, 0, 0, 0, 0, kHalfTurnSine / 65535.0f, (kHalfTurnSine - 1.0f) / 65535.0f, 0, 0, 0};
		putI32(b, 0);
		putI32(b, 0x60);
		for (const float value : armOffset) {
			putF32(b, value);
		}
		for (const float value : armScale) {
			putF32(b, value);
		}
		field(17, 1);
		field(18, b.size());
		putI32(b, nameWave);
		putI32(b, 0);
		putI32(b, 2);
		putF32(b, 15.0f);
		putI32(b, 1);
		field(19, 2);
		field(20, 2);
		field(21, b.size());
		putU16(b, 0);
		putU16(b, 0);
		putU16(b, 65535);
		putU16(b, 65535);
		field(22, b.size());
		for (int frame = 0; frame < 2; ++frame) {
			putVec(b, 0.0f, -1.0f, 0.0f);
			putVec(b, 14.0f, 4.0f, 4.0f);
			putF32(b, 14.6f);
			putF32(b, 15.0f);
		}
	}
	field(1, b.size());
	if (layout) {
		*layout = IqmLayout{meshes, joints, blendIndexes, text};
	}
	return b;
}

void checkSkeletalIqm(const ModelMesh& mesh, const char* label)
{
	if (!expect(mesh.error.isEmpty() && mesh.geometryAvailable, label)) {
		std::cout << "  " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::Iqm && mesh.version == 2, "IQM format and version should be reported.");
	const ModelSkeleton& skeleton = mesh.skeleton;
	expect(skeleton.sourceFormat == QStringLiteral("iqm") && skeleton.joints.size() == 2, "IQM joints should decode.");
	if (skeleton.joints.size() == 2) {
		expect(skeleton.joints.at(0).name == QStringLiteral("root") && skeleton.joints.at(0).parent == -1
				&& skeleton.joints.at(1).name == QStringLiteral("arm") && skeleton.joints.at(1).parent == 0,
			"IQM joint names and parents should decode.");
		expect(nearly(skeleton.joints.at(1).bind.m[3], 10.0f) && nearly(skeleton.joints.at(1).bind.m[0], 1.0f),
			"The IQM bind pose should concatenate the joint transforms.");
	}
	expect(skeleton.clips.size() == 1 && skeleton.clips.first().name == QStringLiteral("wave") && skeleton.clips.first().loops
			&& std::fabs(skeleton.clips.first().framesPerSecond - 15.0) < 1e-6,
		"The IQM animation should keep its name, rate and loop flag.");
	expect(mesh.frameCount == 3 && mesh.frames.first().name == QStringLiteral("bindpose"), "An IQM should bake the bind pose and its frames.");
	expect(mesh.animations.size() == 1 && mesh.animations.first().name == QStringLiteral("wave") && mesh.animations.first().firstFrame == 1
			&& mesh.animations.first().frameCount == 2 && std::fabs(mesh.animations.first().framesPerSecond - 15.0) < 1e-6,
		"The baked IQM animation range and rate should follow the bind pose.");
	if (mesh.surfaces.size() != 1 || mesh.surfaces.first().frames.size() != 3) {
		expect(false, "The IQM surface should hold three frames.");
		return;
	}
	const ModelSurface& surface = mesh.surfaces.first();
	expect(surface.name == QStringLiteral("body") && surface.skinPaths == QStringList({QStringLiteral("models/test/iqmskin")}),
		"The IQM mesh name and material should decode.");
	expect(mesh.skinPaths == QStringList({QStringLiteral("models/test/iqmskin")}), "The IQM material should be a model skin path.");
	bool uvs = true;
	bool bind = true;
	for (int v = 0; v < 3; ++v) {
		uvs = uvs && nearly(surface.texCoords.at(v).u, kIqmUvs[v][0]) && nearly(surface.texCoords.at(v).v, kIqmUvs[v][1]);
		bind = bind && nearVec(surface.frames.at(0).positions.at(v), kIqmPositions[v][0], kIqmPositions[v][1], kIqmPositions[v][2]);
	}
	expect(uvs, "IQM texture coordinates should decode as stored.");
	expect(bind, "The baked IQM bind pose should hold the stored positions.");
	expect(nearVec(surface.frames.at(1).positions.at(1), 14.0f, 0.0f, 0.0f), "The first IQM animation frame is the bind pose here.");
	expect(nearVec(surface.frames.at(2).positions.at(0), 0.0f, 0.0f, 0.0f, 1.0e-3f), "A vertex on the IQM root should not move.");
	expect(nearVec(surface.frames.at(2).positions.at(1), 10.0f, 4.0f, 0.0f, 1.0e-3f), "A vertex on the turned IQM joint should turn 90 degrees.");
	expect(nearVec(surface.frames.at(2).positions.at(2), 10.8f, 1.2f, 4.0f, 1.0e-3f), "A 0.4 / 0.6 IQM vertex should blend both joints.");
	expect(nearVec(surface.frames.at(2).normals.at(1), 1.0f, 0.0f, 0.0f, 1.0e-3f), "IQM normals should turn with their joint.");
	expect(surface.triangles.first().a == 0 && surface.triangles.first().b == 1 && surface.triangles.first().c == 2,
		"IQM triangle order should be reversed for counter-clockwise fronts.");
	expect(faceNormal(surface, 0, 0).y < 0.0f, "The IQM triangle should face its stored normals (-Y).");
}

void runIqm()
{
	IqmLayout layout;
	const QByteArray bytes = buildIqm(true, &layout);
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/thing.iqm"), bytes);
	checkSkeletalIqm(mesh, "The skeletal IQM fixture should decode with geometry.");
	expect(mesh.warnings.isEmpty(), "A clean IQM should not warn.");

	const QByteArray still = buildIqm(false);
	const ModelMesh stillMesh = decodeModelMesh(QStringLiteral("models/test/still.iqm"), still);
	if (expect(stillMesh.error.isEmpty() && stillMesh.geometryAvailable, "The static IQM fixture should decode with geometry.")) {
		expect(stillMesh.skeleton.isEmpty() && stillMesh.frameCount == 1 && stillMesh.surfaces.first().frames.size() == 1,
			"A static IQM should decode to one frame without joints.");
		expect(nearVec(stillMesh.surfaces.first().frames.first().positions.at(2), 12.0f, 0.0f, 4.0f), "Static IQM positions should decode.");
		expect(nearVec(stillMesh.surfaces.first().frames.first().normals.at(0), 0.0f, -1.0f, 0.0f), "Static IQM normals should decode.");
		expect(nearVec(stillMesh.frames.first().maxs, 14.0f, 0.0f, 4.0f), "The static IQM frame should carry its bounds.");
	}

	QByteArray badVersion = bytes;
	patchI32(badVersion, 16, 1);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), badVersion).error.isEmpty(), "IQM version 1 should be rejected.");
	QByteArray longSize = bytes;
	patchI32(longSize, 20, bytes.size() + 4);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), longSize).error.isEmpty(), "An IQM declaring more bytes than it holds should be rejected.");
	QByteArray shortSize = bytes;
	patchI32(shortSize, 20, 64);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), shortSize).error.isEmpty(), "An IQM declaring fewer bytes than its header should be rejected.");
	QByteArray trailing = bytes;
	trailing.append(QByteArray(12, '\x55'));
	const ModelMesh trailingMesh = decodeModelMesh(QStringLiteral("a.iqm"), trailing);
	expect(trailingMesh.error.isEmpty() && trailingMesh.geometryAvailable && !trailingMesh.warnings.isEmpty(),
		"Bytes after the declared IQM size should be ignored with a warning.");
	QByteArray badMesh = bytes;
	patchI32(badMesh, layout.meshes + 8, 2);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), badMesh).error.isEmpty(), "An IQM mesh past the vertex list should be rejected.");
	QByteArray badName = bytes;
	patchI32(badName, layout.meshes, 4000);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), badName).error.isEmpty(), "An IQM name outside the text block should be rejected.");
	QByteArray forwardParent = bytes;
	patchI32(forwardParent, layout.joints + 4, 1);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), forwardParent).error.isEmpty(), "An IQM joint naming a later parent should be rejected.");
	QByteArray badJoint = bytes;
	badJoint[layout.blendIndexes + 4] = char(7);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), badJoint).error.isEmpty(), "An IQM blend index past the joints should be rejected.");
	QByteArray badChannels = bytes;
	patchI32(badChannels, 16 + (20 * 4), 3);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), badChannels).error.isEmpty(), "An IQM frame channel count that disagrees with its poses should be rejected.");
	QByteArray hugeVerts = bytes;
	patchI32(hugeVerts, 16 + (8 * 4), -1);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), hugeVerts).error.isEmpty(), "A huge IQM vertex count should be rejected.");
	QByteArray farTriangles = bytes;
	patchI32(farTriangles, 16 + (11 * 4), 1 << 24);
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), farTriangles).error.isEmpty(), "An IQM triangle block outside the file should be rejected.");
	QByteArray badMagic = bytes;
	badMagic[15] = 'X';
	expect(!decodeModelMesh(QStringLiteral("a.iqm"), badMagic).error.isEmpty(), "An IQM magic without its terminator should be rejected.");
	fuzz(QStringLiteral("models/fuzz.iqm"), bytes, "Every truncated IQM should report an error.", "Flipped IQM bytes should give an error or a consistent mesh.");
	fuzz(QStringLiteral("models/fuzz.iqm"), still, "Every truncated static IQM should report an error.",
		"Flipped static IQM bytes should give an error or a consistent mesh.");
}

// ---------------------------------------------------------------------------
// IQM writer
// ---------------------------------------------------------------------------

// Frame f of `source` matches frame f + shift of `round` wherever `round` has
// that frame (callers check the frame counts themselves), with the same
// triangles and texture coordinates.
bool sameFrames(const ModelMesh& source, const ModelMesh& round, int shift, float epsilon)
{
	if (source.surfaces.size() != round.surfaces.size()) {
		return false;
	}
	for (int s = 0; s < source.surfaces.size(); ++s) {
		const ModelSurface& a = source.surfaces.at(s);
		const ModelSurface& b = round.surfaces.at(s);
		if (a.vertexCount != b.vertexCount || a.triangles.size() != b.triangles.size() || b.frames.isEmpty()) {
			return false;
		}
		for (int t = 0; t < a.triangles.size(); ++t) {
			if (a.triangles.at(t).a != b.triangles.at(t).a || a.triangles.at(t).b != b.triangles.at(t).b || a.triangles.at(t).c != b.triangles.at(t).c) {
				return false;
			}
		}
		for (int v = 0; v < a.vertexCount; ++v) {
			if (!nearly(a.texCoords.at(v).u, b.texCoords.at(v).u, 1e-6f) || !nearly(a.texCoords.at(v).v, b.texCoords.at(v).v, 1e-6f)) {
				return false;
			}
		}
		for (int f = 0; f < a.frames.size(); ++f) {
			const int g = f + shift;
			if (g < 0 || g >= b.frames.size()) {
				continue;
			}
			for (int v = 0; v < a.vertexCount; ++v) {
				const ModelVec3& p = a.frames.at(f).positions.at(v);
				if (!nearVec(b.frames.at(g).positions.at(v), p.x, p.y, p.z, epsilon)) {
					return false;
				}
			}
		}
	}
	return true;
}

void runIqmWriter()
{
	const ModelMesh source = decodeModelMesh(QStringLiteral("models/test/thing.iqm"), buildIqm(true));
	QString error;
	const QByteArray written = exportModelIqm(source, &error);
	if (expect(!written.isEmpty() && error.isEmpty(), "A skeletal mesh should export as IQM.")) {
		expect(written.startsWith(QByteArray("INTERQUAKEMODEL\0", 16)) && (written.size() % 4) == 0, "The IQM output should start with its magic.");
		const ModelMesh round = decodeModelMesh(QStringLiteral("models/test/round.iqm"), written);
		checkSkeletalIqm(round, "The exported skeletal IQM should decode.");
		expect(sameFrames(source, round, 0, 2.0e-3f), "Every baked frame should survive the IQM round trip.");
		expect(round.warnings.isEmpty(), "The exported IQM should decode without warnings.");
	} else {
		std::cout << "  " << error.toStdString() << "\n";
	}

	// An MDR (no separate bind pose) through IQM: the bind is skinned from
	// the joints' bind matrices and becomes the first baked frame.
	const ModelMesh mdr = decodeModelMesh(QStringLiteral("models/test/thing.mdr"), buildMdr(false));
	error.clear();
	const QByteArray fromMdr = exportModelIqm(mdr, &error);
	if (expect(!fromMdr.isEmpty() && error.isEmpty(), "An MDR should export as IQM.")) {
		const ModelMesh round = decodeModelMesh(QStringLiteral("models/test/mdr.iqm"), fromMdr);
		expect(round.error.isEmpty() && round.frameCount == 3 && round.skeleton.joints.size() == 2 && round.skeleton.clips.size() == 1
				&& round.skeleton.clips.first().name == QStringLiteral("all"),
			"The MDR's IQM should hold the bind pose and its one clip.");
		expect(sameFrames(mdr, round, 1, 2.0e-3f), "MDR frames should survive an IQM round trip.");
		expect(round.surfaces.value(0).skinPaths == QStringList({QStringLiteral("models/test/skin")}), "The MDR shader should become the IQM material.");
	} else {
		std::cout << "  " << error.toStdString() << "\n";
	}

	// Static meshes write frame 0 only.
	const ModelMesh mdc = decodeModelMesh(QStringLiteral("models/test/thing.mdc"), buildMdc());
	error.clear();
	const QByteArray fromMdc = exportModelIqm(mdc, &error);
	if (expect(!fromMdc.isEmpty() && error.isEmpty(), "A static mesh should export as IQM.")) {
		const ModelMesh round = decodeModelMesh(QStringLiteral("models/test/mdc.iqm"), fromMdc);
		expect(round.error.isEmpty() && round.skeleton.isEmpty() && round.frameCount == 1, "A static IQM export should decode to one frame.");
		expect(sameFrames(mdc, round, 0, 1.0e-6f), "The first frame of a static mesh should survive the IQM round trip.");
		expect(round.surfaces.value(0).skinPaths == QStringList({QStringLiteral("models/test/body")}), "The first shader should become the IQM material.");
		expect(nearVec(round.surfaces.value(0).frames.value(0).normals.value(0), 0.0f, 0.0f, 1.0f, 1.0e-6f), "Static IQM export should keep the normals.");
	} else {
		std::cout << "  " << error.toStdString() << "\n";
	}
	const ModelMesh still = decodeModelMesh(QStringLiteral("models/test/still.iqm"), buildIqm(false));
	const ModelMesh stillRound = decodeModelMesh(QStringLiteral("models/test/still2.iqm"), exportModelIqm(still));
	expect(stillRound.error.isEmpty() && sameFrames(still, stillRound, 0, 1.0e-6f), "A static IQM should survive its own round trip.");

	error.clear();
	const QByteArray empty = exportModelIqm(ModelMesh{}, &error);
	expect(empty.isEmpty() && !error.isEmpty(), "A model without geometry or joints should not export as IQM.");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	runMdc();
	runMdr();
	runIqm();
	runIqmWriter();
	if (failures > 0) {
		std::cout << failures << " check(s) failed.\n";
		return 1;
	}
	std::cout << "model_format_mdc_mdr_iqm_smoke_test passed.\n";
	return 0;
}
