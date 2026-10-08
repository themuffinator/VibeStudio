// Smoke tests for the RTCW / Wolfenstein: ET skeletal formats (MDS, MDM with
// its MDX, MDX alone) and the Ghoul 2 formats (GLM with its GLA, GLA alone).
// Every fixture is built byte by byte here; no game data is used.
#include "core/model_mesh.h"
#include "core/model_skeleton.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace vibestudio;

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
	if (!condition) {
		++failures;
		std::cerr << "FAIL: " << message << "\n";
	}
}

void expectNoError(const ModelMesh& mesh, const char* message)
{
	if (!mesh.error.isEmpty()) {
		++failures;
		std::cerr << "FAIL: " << message << ": " << mesh.error.toStdString() << "\n";
	}
}

bool nearly(float value, float expected, float epsilon = 1.0e-3f)
{
	return std::fabs(value - expected) <= epsilon;
}

bool nearVec(const ModelVec3& value, const ModelVec3& expected, float epsilon = 1.0e-3f)
{
	return nearly(value.x, expected.x, epsilon) && nearly(value.y, expected.y, epsilon) && nearly(value.z, expected.z, epsilon);
}

bool containsText(const QStringList& lines, const QString& text)
{
	for (const QString& line : lines) {
		if (line.contains(text)) {
			return true;
		}
	}
	return false;
}

// --- Byte writers -----------------------------------------------------------

void appendI16(QByteArray& bytes, int value)
{
	const quint16 raw = static_cast<quint16>(value);
	bytes.append(static_cast<char>(raw & 0xFF));
	bytes.append(static_cast<char>((raw >> 8) & 0xFF));
}

void appendI32(QByteArray& bytes, qint64 value)
{
	const quint32 raw = static_cast<quint32>(value);
	for (int shift = 0; shift < 32; shift += 8) {
		bytes.append(static_cast<char>((raw >> shift) & 0xFF));
	}
}

void appendF32(QByteArray& bytes, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	appendI32(bytes, raw);
}

void appendVec3(QByteArray& bytes, const ModelVec3& value)
{
	appendF32(bytes, value.x);
	appendF32(bytes, value.y);
	appendF32(bytes, value.z);
}

void appendFixed(QByteArray& bytes, const QByteArray& name, int length)
{
	QByteArray padded = name.left(length);
	padded.append(QByteArray(length - padded.size(), '\0'));
	bytes.append(padded);
}

void appendMatrix(QByteArray& bytes, const ModelJointMatrix& matrix)
{
	for (float value : matrix.m) {
		appendF32(bytes, value);
	}
}

void patchI32(QByteArray& bytes, int offset, qint64 value)
{
	const quint32 raw = static_cast<quint32>(value);
	for (int index = 0; index < 4; ++index) {
		bytes[offset + index] = static_cast<char>((raw >> (index * 8)) & 0xFF);
	}
}

// --- Companions -------------------------------------------------------------

ModelCompanionSource fakeCompanions(const QHash<QString, QByteArray>& files)
{
	ModelCompanionSource source;
	source.read = [files](const QString& path, QByteArray* bytes, QString* error) {
		const auto found = files.constFind(path);
		if (found == files.constEnd()) {
			if (error) { *error = QStringLiteral("missing: ") + path; }
			return false;
		}
		*bytes = found.value();
		return true;
	};
	source.list = [files](const QString& directory, const QStringList& suffixes) {
		QStringList paths;
		for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
			const QString& path = it.key();
			const int slash = path.lastIndexOf(QLatin1Char('/'));
			const QString folder = slash < 0 ? QString() : path.left(slash);
			const QString suffix = path.mid(path.lastIndexOf(QLatin1Char('.')) + 1).toLower();
			if (folder.compare(directory, Qt::CaseInsensitive) == 0 && suffixes.contains(suffix)) {
				paths.append(path);
			}
		}
		std::sort(paths.begin(), paths.end());
		return paths;
	};
	return source;
}

// --- Wolfenstein fixtures ---------------------------------------------------

constexpr int kYaw90 = 16384; // ANGLE2SHORT(90)

struct WolfBoneSpec {
	QByteArray name;
	int parent = -1;
	float torsoWeight = 0.0f;
	float parentDist = 0.0f;
	int flags = 0;
};

struct WolfBoneFrameSpec {
	int pitch = 0;
	int yaw = 0;
	int roll = 0;
	int ofsPitch = 0;
	int ofsYaw = 0;
};

struct WolfFrameSpec {
	ModelVec3 parentOffset;
	QVector<WolfBoneFrameSpec> bones;
};

struct WolfWeightSpec {
	int bone = 0;
	float weight = 1.0f;
	ModelVec3 offset;
};

struct WolfVertexSpec {
	ModelVec3 normal;
	float u = 0.0f;
	float v = 0.0f;
	QVector<WolfWeightSpec> weights;
};

struct WolfSurfaceSpec {
	QByteArray name;
	QByteArray shader;
	int minLod = 0;
	QVector<WolfVertexSpec> vertices;
	QVector<std::array<int, 3>> triangles;
	QVector<int> boneRefs;
};

struct WolfTagSpec {
	QByteArray name;
	int bone = 0;
	float torsoWeight = 0.0f;
	ModelVec3 axis[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
	ModelVec3 offset;
	QVector<int> boneRefs;
};

struct WolfSpec {
	int version = 0;
	QVector<WolfBoneSpec> bones;
	QVector<WolfFrameSpec> frames;
	QVector<WolfSurfaceSpec> surfaces;
	QVector<WolfTagSpec> tags;
};

QByteArray wolfBoneBytes(const QVector<WolfBoneSpec>& bones)
{
	QByteArray out;
	for (const WolfBoneSpec& bone : bones) {
		appendFixed(out, bone.name, 64);
		appendI32(out, bone.parent);
		appendF32(out, bone.torsoWeight);
		appendF32(out, bone.parentDist);
		appendI32(out, bone.flags);
	}
	return out;
}

QByteArray wolfFrameBytes(const QVector<WolfFrameSpec>& frames)
{
	QByteArray out;
	for (const WolfFrameSpec& frame : frames) {
		appendVec3(out, {-50.0f, -50.0f, -50.0f});
		appendVec3(out, {50.0f, 50.0f, 50.0f});
		appendVec3(out, {0.0f, 0.0f, 0.0f});
		appendF32(out, 87.0f);
		appendVec3(out, frame.parentOffset);
		for (const WolfBoneFrameSpec& bone : frame.bones) {
			appendI16(out, bone.pitch);
			appendI16(out, bone.yaw);
			appendI16(out, bone.roll);
			appendI16(out, 0);
			appendI16(out, bone.ofsPitch);
			appendI16(out, bone.ofsYaw);
		}
	}
	return out;
}

QByteArray wolfSurfaceBytes(const WolfSurfaceSpec& spec, bool mdm, int surfaceStart)
{
	QByteArray triangles;
	for (const auto& triangle : spec.triangles) {
		appendI32(triangles, triangle[0]);
		appendI32(triangles, triangle[1]);
		appendI32(triangles, triangle[2]);
	}
	QByteArray vertices;
	for (const WolfVertexSpec& vertex : spec.vertices) {
		appendVec3(vertices, vertex.normal);
		appendF32(vertices, vertex.u);
		appendF32(vertices, vertex.v);
		appendI32(vertices, vertex.weights.size());
		if (!mdm) {
			appendI32(vertices, 0);     // fixedParent
			appendF32(vertices, 0.0f);  // fixedDist
		}
		for (const WolfWeightSpec& weight : vertex.weights) {
			appendI32(vertices, weight.bone);
			appendF32(vertices, weight.weight);
			appendVec3(vertices, weight.offset);
		}
	}
	QByteArray collapse;
	for (int index = 0; index < spec.vertices.size(); ++index) {
		appendI32(collapse, index);
	}
	QByteArray references;
	for (int bone : spec.boneRefs) {
		appendI32(references, bone);
	}
	const int triangleOffset = 176;
	const int vertexOffset = triangleOffset + triangles.size();
	const int collapseOffset = vertexOffset + vertices.size();
	const int referenceOffset = collapseOffset + collapse.size();
	const int endOffset = referenceOffset + references.size();
	QByteArray out;
	appendI32(out, 0);
	appendFixed(out, spec.name, 64);
	appendFixed(out, spec.shader, 64);
	appendI32(out, 0);
	appendI32(out, spec.minLod);
	appendI32(out, -surfaceStart);
	appendI32(out, spec.vertices.size());
	appendI32(out, vertexOffset);
	appendI32(out, spec.triangles.size());
	appendI32(out, triangleOffset);
	appendI32(out, collapseOffset);
	appendI32(out, spec.boneRefs.size());
	appendI32(out, referenceOffset);
	appendI32(out, endOffset);
	out += triangles + vertices + collapse + references;
	return out;
}

QByteArray wolfSurfacesBytes(const QVector<WolfSurfaceSpec>& surfaces, bool mdm, int start)
{
	QByteArray out;
	for (const WolfSurfaceSpec& surface : surfaces) {
		out += wolfSurfaceBytes(surface, mdm, start + out.size());
	}
	return out;
}

QVector<WolfBoneSpec> chainBones(bool withTagBone)
{
	QVector<WolfBoneSpec> bones{{"root", -1, 0.0f, 0.0f, 0}, {"mid", 0, 0.0f, 10.0f, 0}, {"tip", 1, 0.0f, 8.0f, 0}};
	if (withTagBone) {
		bones.append({"tag_tip", 2, 0.0f, 4.0f, 1});
	}
	return bones;
}

// Frame 0 at rest; frame 1 turns the middle bone 90 degrees about Z, and its
// descendants with it (their own angles and their offset direction).
QVector<WolfFrameSpec> chainFrames(int boneCount)
{
	WolfFrameSpec rest;
	rest.parentOffset = {1.0f, 2.0f, 3.0f};
	rest.bones.resize(boneCount);
	WolfFrameSpec turned = rest;
	turned.bones[1].yaw = kYaw90;
	for (int bone = 2; bone < boneCount; ++bone) {
		turned.bones[bone].yaw = kYaw90;
		turned.bones[bone].ofsYaw = kYaw90;
	}
	return {rest, turned};
}

WolfSurfaceSpec wolfBody()
{
	WolfSurfaceSpec body;
	body.name = "body";
	body.shader = "models/test/body";
	body.minLod = 2;
	body.vertices = {
		{{1.0f, 0.0f, 0.0f}, 0.25f, 0.75f, {{2, 1.0f, {2.0f, 0.0f, 0.0f}}}},
		{{0.0f, 0.0f, 1.0f}, 0.5f, 0.5f, {{1, 0.5f, {1.0f, 0.0f, 0.0f}}, {2, 0.5f, {0.0f, 1.0f, 0.0f}}}},
		{{0.0f, 1.0f, 0.0f}, 1.0f, 0.0f, {{0, 1.0f, {0.0f, 0.0f, 5.0f}}}},
	};
	body.triangles = {{0, 1, 2}};
	body.boneRefs = {0, 1, 2};
	return body;
}

// A triangle on the root bone that the game draws facing +Z: clockwise when
// seen from above, as Quake III-lineage files store front faces.
WolfSurfaceSpec wolfPlate()
{
	WolfSurfaceSpec plate;
	plate.name = "plate";
	plate.shader = "models/test/plate";
	plate.vertices = {
		{{0.0f, 0.0f, 1.0f}, 0.0f, 0.0f, {{0, 1.0f, {0.0f, 0.0f, 0.0f}}}},
		{{0.0f, 0.0f, 1.0f}, 0.0f, 1.0f, {{0, 1.0f, {0.0f, 10.0f, 0.0f}}}},
		{{0.0f, 0.0f, 1.0f}, 1.0f, 0.0f, {{0, 1.0f, {10.0f, 0.0f, 0.0f}}}},
	};
	plate.triangles = {{0, 1, 2}};
	plate.boneRefs = {0};
	return plate;
}

WolfSpec mdsSpec()
{
	WolfSpec spec;
	spec.version = 4;
	spec.bones = chainBones(true);
	spec.frames = chainFrames(spec.bones.size());
	spec.surfaces = {wolfBody(), wolfPlate()};
	WolfTagSpec tip;
	tip.name = "tag_tip";
	tip.bone = 3;
	WolfTagSpec mid;
	mid.name = "tag_mid";
	mid.bone = 1;
	spec.tags = {tip, mid};
	return spec;
}

QByteArray buildMds(const WolfSpec& spec)
{
	const QByteArray frames = wolfFrameBytes(spec.frames);
	const QByteArray bones = wolfBoneBytes(spec.bones);
	const int frameOffset = 120;
	const int boneOffset = frameOffset + frames.size();
	const int surfaceOffset = boneOffset + bones.size();
	const QByteArray surfaces = wolfSurfacesBytes(spec.surfaces, false, surfaceOffset);
	const int tagOffset = surfaceOffset + surfaces.size();
	QByteArray tags;
	for (const WolfTagSpec& tag : spec.tags) {
		appendFixed(tags, tag.name, 64);
		appendF32(tags, tag.torsoWeight);
		appendI32(tags, tag.bone);
	}
	QByteArray out("MDSW");
	appendI32(out, spec.version);
	appendFixed(out, "models/test/body.mds", 64);
	appendF32(out, 1.0f);
	appendF32(out, 0.0f);
	appendI32(out, spec.frames.size());
	appendI32(out, spec.bones.size());
	appendI32(out, frameOffset);
	appendI32(out, boneOffset);
	appendI32(out, 1);
	appendI32(out, spec.surfaces.size());
	appendI32(out, surfaceOffset);
	appendI32(out, spec.tags.size());
	appendI32(out, tagOffset);
	appendI32(out, tagOffset + tags.size());
	out += frames + bones + surfaces + tags;
	return out;
}

WolfSpec mdxSpec()
{
	WolfSpec spec;
	spec.version = 2;
	spec.bones = chainBones(false);
	spec.frames = chainFrames(spec.bones.size());
	return spec;
}

QByteArray buildMdx(const WolfSpec& spec)
{
	const QByteArray frames = wolfFrameBytes(spec.frames);
	const QByteArray bones = wolfBoneBytes(spec.bones);
	QByteArray out("MDXW");
	appendI32(out, spec.version);
	appendFixed(out, "animations/test/body.mdx", 64);
	appendI32(out, spec.frames.size());
	appendI32(out, spec.bones.size());
	appendI32(out, 96);
	appendI32(out, 96 + frames.size());
	appendI32(out, 1);
	appendI32(out, 96 + frames.size() + bones.size());
	out += frames + bones;
	return out;
}

WolfSpec mdmSpec()
{
	WolfSpec spec;
	spec.version = 3;
	spec.surfaces = {wolfBody()};
	WolfTagSpec weapon;
	weapon.name = "tag_weapon";
	weapon.bone = 2;
	weapon.axis[0] = {0.0f, 0.0f, 1.0f};
	weapon.axis[1] = {0.0f, 1.0f, 0.0f};
	weapon.axis[2] = {-1.0f, 0.0f, 0.0f};
	weapon.offset = {1.0f, 2.0f, 3.0f};
	weapon.boneRefs = {0, 1, 2};
	spec.tags = {weapon};
	return spec;
}

QByteArray buildMdm(const WolfSpec& spec)
{
	const int surfaceOffset = 100;
	const QByteArray surfaces = wolfSurfacesBytes(spec.surfaces, true, surfaceOffset);
	const int tagOffset = surfaceOffset + surfaces.size();
	QByteArray tags;
	for (const WolfTagSpec& tag : spec.tags) {
		appendFixed(tags, tag.name, 64);
		for (const ModelVec3& axis : tag.axis) {
			appendVec3(tags, axis);
		}
		appendI32(tags, tag.bone);
		appendVec3(tags, tag.offset);
		appendI32(tags, tag.boneRefs.size());
		appendI32(tags, 128);
		appendI32(tags, 128 + 4 * tag.boneRefs.size());
		for (int bone : tag.boneRefs) {
			appendI32(tags, bone);
		}
	}
	QByteArray out("MDMW");
	appendI32(out, spec.version);
	appendFixed(out, "models/test/body.mdm", 64);
	appendF32(out, 1.0f);
	appendF32(out, 0.0f);
	appendI32(out, spec.surfaces.size());
	appendI32(out, surfaceOffset);
	appendI32(out, spec.tags.size());
	appendI32(out, tagOffset);
	appendI32(out, tagOffset + tags.size());
	out += surfaces + tags;
	return out;
}

// --- Ghoul 2 fixtures -------------------------------------------------------

using CompBone = std::array<quint16, 7>;

quint16 encodeQuatComponent(double value)
{
	return quint16(std::lround((value + 2.0) * 16383.0));
}

quint16 encodeTranslation(double value)
{
	return quint16(std::lround((value + 512.0) * 64.0));
}

CompBone compBone(double w, double x, double y, double z, const ModelVec3& translation)
{
	return {encodeQuatComponent(w), encodeQuatComponent(x), encodeQuatComponent(y), encodeQuatComponent(z),
		encodeTranslation(translation.x), encodeTranslation(translation.y), encodeTranslation(translation.z)};
}

// An independent MC_UnCompressQuat for the expectations.
ModelJointMatrix decodeCompBone(const CompBone& bone)
{
	const double w = bone[0] / 16383.0 - 2.0;
	const double x = bone[1] / 16383.0 - 2.0;
	const double y = bone[2] / 16383.0 - 2.0;
	const double z = bone[3] / 16383.0 - 2.0;
	ModelJointMatrix m;
	m.m[0] = float(1.0 - 2.0 * (y * y + z * z));
	m.m[1] = float(2.0 * (x * y - w * z));
	m.m[2] = float(2.0 * (x * z + w * y));
	m.m[3] = float(bone[4] / 64.0 - 512.0);
	m.m[4] = float(2.0 * (x * y + w * z));
	m.m[5] = float(1.0 - 2.0 * (x * x + z * z));
	m.m[6] = float(2.0 * (y * z - w * x));
	m.m[7] = float(bone[5] / 64.0 - 512.0);
	m.m[8] = float(2.0 * (x * z - w * y));
	m.m[9] = float(2.0 * (y * z + w * x));
	m.m[10] = float(1.0 - 2.0 * (x * x + y * y));
	m.m[11] = float(bone[6] / 64.0 - 512.0);
	return m;
}

ModelJointMatrix rowsMatrix(std::array<float, 12> values)
{
	ModelJointMatrix m;
	std::copy(values.begin(), values.end(), m.m);
	return m;
}

// The game's root turn: x' = -y, y' = x.
ModelVec3 rootTurn(const ModelVec3& v)
{
	return {-v.y, v.x, v.z};
}

struct GlaBoneSpec {
	QByteArray name;
	int parent = -1;
	ModelJointMatrix base;
	ModelJointMatrix inverse;
};

struct GlaSpec {
	int version = 6;
	QVector<GlaBoneSpec> bones;
	QVector<QVector<int>> frames;
	QVector<CompBone> pool;
};

QByteArray buildGla(const GlaSpec& spec)
{
	const int boneCount = spec.bones.size();
	QByteArray skeleton;
	QVector<int> offsets;
	for (int index = 0; index < boneCount; ++index) {
		offsets.append(4 * boneCount + skeleton.size());
		const GlaBoneSpec& bone = spec.bones.at(index);
		QVector<int> children;
		for (int child = 0; child < boneCount; ++child) {
			if (spec.bones.at(child).parent == index) {
				children.append(child);
			}
		}
		appendFixed(skeleton, bone.name, 64);
		appendI32(skeleton, 0);
		appendI32(skeleton, bone.parent);
		appendMatrix(skeleton, bone.base);
		appendMatrix(skeleton, bone.inverse);
		appendI32(skeleton, children.size());
		for (int child : children) {
			appendI32(skeleton, child);
		}
	}
	QByteArray frames;
	for (const QVector<int>& frame : spec.frames) {
		for (int entry : frame) {
			frames.append(char(entry & 0xFF));
			frames.append(char((entry >> 8) & 0xFF));
			frames.append(char((entry >> 16) & 0xFF));
		}
	}
	while (frames.size() % 4 != 0) {
		frames.append('\0');
	}
	QByteArray pool;
	for (const CompBone& bone : spec.pool) {
		for (quint16 value : bone) {
			appendI16(pool, value);
		}
	}
	const int frameOffset = 100 + 4 * boneCount + skeleton.size();
	const int poolOffset = frameOffset + frames.size();
	QByteArray out("2LGA");
	appendI32(out, spec.version);
	appendFixed(out, "models/test/test_anim", 64);
	appendF32(out, 1.0f);
	appendI32(out, spec.frames.size());
	appendI32(out, frameOffset);
	appendI32(out, boneCount);
	appendI32(out, poolOffset);
	appendI32(out, 100);
	appendI32(out, poolOffset + pool.size());
	for (int offset : offsets) {
		appendI32(out, offset);
	}
	out += skeleton + frames + pool;
	return out;
}

const ModelJointMatrix kIdentity = rowsMatrix({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
// The arm's base pose: a quarter turn about Z at (0, 0, 10), and its inverse.
const ModelJointMatrix kArmBase = rowsMatrix({0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 10});
const ModelJointMatrix kArmBaseInverse = rowsMatrix({0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, -10});

// A quarter turn about X around the pivot (0, 0, 10): translation p - R p.
CompBone armTurn()
{
	const double half = std::sqrt(0.5);
	return compBone(half, half, 0.0, 0.0, {0.0f, 10.0f, 10.0f});
}

GlaSpec glaSpec()
{
	GlaSpec spec;
	spec.bones = {{"root", -1, kIdentity, kIdentity}, {"arm", 0, kArmBase, kArmBaseInverse}};
	spec.pool = {compBone(1.0, 0.0, 0.0, 0.0, {0.0f, 0.0f, 0.0f}), armTurn()};
	spec.frames = {{0, 0}, {0, 1}};
	return spec;
}

struct GlmVertexSpec {
	ModelVec3 normal;
	ModelVec3 position;
	float u = 0.0f;
	float v = 0.0f;
	QVector<int> refs;
	// Ten-bit weights for every reference but the last.
	QVector<int> weights;
};

struct GlmSurfaceSpec {
	QByteArray name;
	quint32 flags = 0;
	QByteArray shader;
	int parent = -1;
	QVector<int> children;
	QVector<GlmVertexSpec> vertices;
	QVector<std::array<int, 3>> triangles;
	QVector<int> boneRefs;
};

struct GlmSpec {
	int version = 6;
	QByteArray animName = "models/test/test_anim";
	int numBones = 2;
	int lodOffsetDelta = 0;
	QVector<GlmSurfaceSpec> surfaces;
};

QByteArray glmSurfaceBytes(const GlmSurfaceSpec& spec, int index, int surfaceStart)
{
	QByteArray vertices;
	QByteArray texCoords;
	for (const GlmVertexSpec& vertex : spec.vertices) {
		appendVec3(vertices, vertex.normal);
		appendVec3(vertices, vertex.position);
		quint32 packed = quint32(vertex.refs.size() - 1) << 30;
		quint8 low[4]{};
		for (int weight = 0; weight < vertex.refs.size(); ++weight) {
			packed |= quint32(vertex.refs.at(weight)) << (5 * weight);
		}
		for (int weight = 0; weight < vertex.weights.size(); ++weight) {
			low[weight] = quint8(vertex.weights.at(weight) & 0xFF);
			packed |= quint32((vertex.weights.at(weight) >> 8) & 3) << (20 + 2 * weight);
		}
		appendI32(vertices, packed);
		for (quint8 value : low) {
			vertices.append(char(value));
		}
		appendF32(texCoords, vertex.u);
		appendF32(texCoords, vertex.v);
	}
	QByteArray triangles;
	for (const auto& triangle : spec.triangles) {
		appendI32(triangles, triangle[0]);
		appendI32(triangles, triangle[1]);
		appendI32(triangles, triangle[2]);
	}
	QByteArray references;
	for (int bone : spec.boneRefs) {
		appendI32(references, bone);
	}
	const int vertexOffset = 40;
	const int triangleOffset = vertexOffset + vertices.size() + texCoords.size();
	const int referenceOffset = triangleOffset + triangles.size();
	QByteArray out;
	appendI32(out, 0);
	appendI32(out, index);
	appendI32(out, -surfaceStart);
	appendI32(out, spec.vertices.size());
	appendI32(out, vertexOffset);
	appendI32(out, spec.triangles.size());
	appendI32(out, triangleOffset);
	appendI32(out, spec.boneRefs.size());
	appendI32(out, referenceOffset);
	appendI32(out, referenceOffset + references.size());
	out += vertices + texCoords + triangles + references;
	return out;
}

QByteArray buildGlm(const GlmSpec& spec)
{
	const int surfaceCount = spec.surfaces.size();
	QByteArray hierarchy;
	QVector<int> hierarchyOffsets;
	for (const GlmSurfaceSpec& surface : spec.surfaces) {
		hierarchyOffsets.append(4 * surfaceCount + hierarchy.size());
		appendFixed(hierarchy, surface.name, 64);
		appendI32(hierarchy, surface.flags);
		appendFixed(hierarchy, surface.shader, 64);
		appendI32(hierarchy, 0);
		appendI32(hierarchy, surface.parent);
		appendI32(hierarchy, surface.children.size());
		for (int child : surface.children) {
			appendI32(hierarchy, child);
		}
	}
	const int lodOffset = 164 + 4 * surfaceCount + hierarchy.size();
	QByteArray surfaces;
	QVector<int> surfaceOffsets;
	for (int index = 0; index < surfaceCount; ++index) {
		surfaceOffsets.append(4 * surfaceCount + surfaces.size());
		surfaces += glmSurfaceBytes(spec.surfaces.at(index), index, lodOffset + 4 + 4 * surfaceCount + surfaces.size());
	}
	QByteArray lod;
	appendI32(lod, 4 + 4 * surfaceCount + surfaces.size());
	for (int offset : surfaceOffsets) {
		appendI32(lod, offset);
	}
	lod += surfaces;
	QByteArray out("2LGM");
	appendI32(out, spec.version);
	appendFixed(out, "models/test/test.glm", 64);
	appendFixed(out, spec.animName, 64);
	appendI32(out, 0);
	appendI32(out, spec.numBones);
	appendI32(out, 1);
	appendI32(out, lodOffset + spec.lodOffsetDelta);
	appendI32(out, surfaceCount);
	appendI32(out, 164 + 4 * surfaceCount);
	appendI32(out, lodOffset + lod.size());
	for (int offset : hierarchyOffsets) {
		appendI32(out, offset);
	}
	out += hierarchy + lod;
	return out;
}

constexpr int kHalfWeight = 512; // 512 / 1023, using the two extra precision bits

GlmSpec glmSpec()
{
	GlmSpec spec;
	GlmSurfaceSpec body;
	body.name = "body";
	body.shader = "models/test/glm_body";
	body.children = {1, 2};
	body.vertices = {
		{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 15.0f}, 0.1f, 0.2f, {1}, {}},
		{{0.0f, 0.0f, 1.0f}, {0.0f, 2.0f, 12.0f}, 0.3f, 0.4f, {0, 1}, {kHalfWeight}},
		{{1.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 0.0f}, 0.5f, 0.6f, {0}, {}},
	};
	body.triangles = {{0, 1, 2}};
	body.boneRefs = {0, 1};
	GlmSurfaceSpec caps;
	caps.name = "caps_off";
	caps.flags = 0x2;
	caps.shader = "models/test/caps";
	caps.parent = 0;
	caps.vertices = {
		{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, {0}, {}},
		{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, 1.0f, 0.0f, {0}, {}},
		{{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 0.0f, 1.0f, {0}, {}},
	};
	caps.triangles = {{0, 1, 2}};
	caps.boneRefs = {0};
	GlmSurfaceSpec hand;
	hand.name = "*hand";
	hand.flags = 0x1;
	hand.shader = "[nomaterial]";
	hand.parent = 0;
	hand.vertices = {
		{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 12.0f}, 0.0f, 0.0f, {0}, {}},
		{{0.0f, 0.0f, 1.0f}, {4.0f, 0.0f, 12.0f}, 0.0f, 0.0f, {0}, {}},
		{{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 12.0f}, 0.0f, 0.0f, {0}, {}},
	};
	hand.triangles = {{0, 1, 2}};
	hand.boneRefs = {1};
	spec.surfaces = {body, caps, hand};
	return spec;
}

// G2_ProcessSurfaceBolt2's model-tag bolt, rewritten for the expectations.
ModelJointMatrix expectedBolt(const ModelVec3 p[3])
{
	const auto sub = [](const ModelVec3& a, const ModelVec3& b) { return ModelVec3{a.x - b.x, a.y - b.y, a.z - b.z}; };
	const auto unit = [](const ModelVec3& a) {
		const double length = std::sqrt(double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z);
		return ModelVec3{float(a.x / length), float(a.y / length), float(a.z / length)};
	};
	const ModelVec3 side0 = sub(p[1], p[0]);
	const ModelVec3 side2 = sub(p[0], p[2]);
	ModelVec3 longest = unit(side0);
	const ModelVec3 shortest = unit(side2);
	const float d = longest.x * shortest.x + longest.y * shortest.y + longest.z * shortest.z;
	longest = unit({longest.x - d * shortest.x, longest.y - d * shortest.y, longest.z - d * shortest.z});
	const ModelVec3 normal = unit({side0.y * side2.z - side0.z * side2.y, side0.z * side2.x - side0.x * side2.z, side0.x * side2.y - side0.y * side2.x});
	return rowsMatrix({shortest.x, longest.x, -normal.x, p[2].x, shortest.y, longest.y, -normal.y, p[2].y, shortest.z, longest.z, -normal.z, p[2].z});
}

bool tagMatches(const ModelTag& tag, const ModelJointMatrix& expected)
{
	if (!nearVec(tag.origin, {expected.m[3], expected.m[7], expected.m[11]})) {
		return false;
	}
	for (int axis = 0; axis < 3; ++axis) {
		if (!nearVec({tag.axis[axis * 3], tag.axis[axis * 3 + 1], tag.axis[axis * 3 + 2]},
				{expected.m[axis], expected.m[4 + axis], expected.m[8 + axis]})) {
			return false;
		}
	}
	return true;
}

const ModelTag* findTag(const ModelMesh& mesh, const QString& name, int frame)
{
	for (const ModelTag& tag : mesh.tags) {
		if (tag.name == name && tag.frameIndex == frame) {
			return &tag;
		}
	}
	return nullptr;
}

// --- Robustness -------------------------------------------------------------

bool consistent(const ModelMesh& mesh)
{
	if (!mesh.error.isEmpty()) {
		return !mesh.geometryAvailable && mesh.surfaces.isEmpty();
	}
	for (const ModelSurface& surface : mesh.surfaces) {
		if (surface.frames.size() != mesh.frames.size() || surface.texCoords.size() != surface.vertexCount) {
			return false;
		}
		for (const ModelFrameGeometry& frame : surface.frames) {
			if (frame.positions.size() != surface.vertexCount || frame.normals.size() != surface.vertexCount) {
				return false;
			}
		}
		for (const ModelTriangle& triangle : surface.triangles) {
			if (triangle.a < 0 || triangle.b < 0 || triangle.c < 0 || triangle.a >= surface.vertexCount || triangle.b >= surface.vertexCount
				|| triangle.c >= surface.vertexCount) {
				return false;
			}
		}
	}
	return true;
}

struct Random {
	quint32 state = 0x2468ACE1u;
	quint32 next()
	{
		state = state * 1664525u + 1013904223u;
		return state >> 8;
	}
};

// Truncates at every byte and flips bytes with a fixed seed; `decode` must
// come back with an error or a consistent mesh every time.
template <typename Decode>
void fuzz(const QByteArray& valid, Decode decode, const char* truncatedMessage, const char* flippedMessage)
{
	bool truncatedOk = true;
	for (int length = 0; length < valid.size(); ++length) {
		const ModelMesh mesh = decode(valid.left(length));
		truncatedOk = truncatedOk && consistent(mesh);
	}
	expect(truncatedOk, truncatedMessage);
	Random random;
	bool flippedOk = true;
	for (int iteration = 0; iteration < 300; ++iteration) {
		QByteArray bytes = valid;
		const int flips = 1 + int(random.next() % 4);
		for (int flip = 0; flip < flips; ++flip) {
			const int at = int(random.next() % quint32(bytes.size()));
			bytes[at] = char(bytes.at(at) ^ char(1 + random.next() % 255));
		}
		flippedOk = flippedOk && consistent(decode(bytes));
	}
	expect(flippedOk, flippedMessage);
}

// --- Tests ------------------------------------------------------------------

void testMds()
{
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/body.mds"), buildMds(mdsSpec()));
	expectNoError(mesh, "MDS decodes");
	expect(mesh.format == ModelMeshFormat::Mds && mesh.version == 4, "MDS format and version");
	expect(mesh.geometryAvailable, "MDS has geometry");
	const ModelSkeleton& skeleton = mesh.skeleton;
	expect(skeleton.sourceFormat == QStringLiteral("mds"), "MDS skeleton source format");
	expect(skeleton.joints.size() == 4, "MDS joint count");
	if (skeleton.joints.size() == 4) {
		expect(skeleton.joints.at(0).name == QStringLiteral("root") && skeleton.joints.at(0).parent == -1, "MDS root joint");
		expect(skeleton.joints.at(1).name == QStringLiteral("mid") && skeleton.joints.at(1).parent == 0, "MDS middle joint");
		expect(skeleton.joints.at(2).name == QStringLiteral("tip") && skeleton.joints.at(2).parent == 1, "MDS tip joint");
		expect(skeleton.joints.at(3).name == QStringLiteral("tag_tip") && skeleton.joints.at(3).parent == 2 && skeleton.joints.at(3).flags == 1,
			"MDS tag bone keeps its flags");
		expect(nearVec(modelJointTranslation(skeleton.joints.at(2).bind), {19.0f, 2.0f, 3.0f}), "MDS bind is the first frame");
	}
	expect(skeleton.clips.size() == 1, "MDS has one clip");
	if (skeleton.clips.size() == 1 && skeleton.joints.size() == 4) {
		const ModelSkeletalClip& clip = skeleton.clips.first();
		expect(clip.name == QStringLiteral("all") && clip.frames.size() == 2 && clip.framesPerSecond == 0.0 && clip.sourcePath.isEmpty(),
			"MDS clip holds every frame with no rate");
		if (clip.frames.size() == 2) {
			// Frame 1: the middle bone turned 90 degrees about Z carries the tip
			// to parent + 8 * (0, 1, 0).
			expect(nearVec(modelJointTranslation(clip.frames.at(0).at(1)), {11.0f, 2.0f, 3.0f}), "MDS middle bone at rest");
			expect(nearVec(modelJointTranslation(clip.frames.at(1).at(1)), {11.0f, 2.0f, 3.0f}), "MDS middle bone stays put when turned");
			expect(nearVec(modelJointTranslation(clip.frames.at(1).at(2)), {11.0f, 10.0f, 3.0f}), "MDS child bone follows the turn");
			expect(nearVec(modelJointTranslation(clip.frames.at(1).at(3)), {11.0f, 14.0f, 3.0f}), "MDS tag bone follows the turn");
		}
	}
	expect(mesh.frames.size() == 2, "MDS bakes two frames without a bind pose");
	if (mesh.frames.size() == 2) {
		expect(mesh.frames.at(0).name == QStringLiteral("all0") && mesh.frames.at(1).name == QStringLiteral("all1"), "MDS frame names");
	}
	expect(mesh.animations.size() == 1 && mesh.animations.first().name == QStringLiteral("all") && mesh.animations.first().firstFrame == 0
			&& mesh.animations.first().frameCount == 2 && mesh.animations.first().framesPerSecond == 0.0,
		"MDS animation range");
	expect(mesh.surfaces.size() == 2, "MDS surface count");
	if (mesh.surfaces.size() == 2 && mesh.frames.size() == 2) {
		const ModelSurface& body = mesh.surfaces.at(0);
		expect(body.name == QStringLiteral("body") && body.skinPaths == QStringList{QStringLiteral("models/test/body")}, "MDS surface name and shader");
		expect(body.vertexCount == 3 && body.texCoords.size() == 3 && nearly(body.texCoords.at(0).u, 0.25f) && nearly(body.texCoords.at(0).v, 0.75f),
			"MDS texture coordinates");
		expect(body.triangles.size() == 1 && body.triangles.first().a == 0 && body.triangles.first().b == 2 && body.triangles.first().c == 1,
			"MDS triangles are reversed to counter-clockwise");
		const QVector<ModelVec3>& rest = body.frames.at(0).positions;
		const QVector<ModelVec3>& turned = body.frames.at(1).positions;
		expect(nearVec(rest.at(0), {21.0f, 2.0f, 3.0f}) && nearVec(rest.at(1), {15.5f, 2.5f, 3.0f}) && nearVec(rest.at(2), {1.0f, 2.0f, 8.0f}),
			"MDS vertices at rest");
		// RB_SurfaceAnim: matrix rows are AnglesToAxis(0, 90, 0) = forward (0, 1, 0),
		// left (-1, 0, 0), up (0, 0, 1), so the offset (2, 0, 0) lands at (0, -2, 0)
		// from the tip.
		expect(nearVec(turned.at(0), {11.0f, 8.0f, 3.0f}), "MDS weighted vertex moves as RTCW's bone maths puts it");
		expect(nearVec(turned.at(1), {11.5f, 5.5f, 3.0f}), "MDS two-bone vertex blends both bones");
		expect(nearVec(turned.at(2), {1.0f, 2.0f, 8.0f}), "MDS root vertex stays put");
		expect(nearVec(body.frames.at(1).normals.at(0), {0.0f, -1.0f, 0.0f}), "MDS normal turns with its first bone");
		const ModelSurface& plate = mesh.surfaces.at(1);
		if (plate.triangles.size() == 1) {
			const ModelTriangle& t = plate.triangles.first();
			const ModelVec3 a = plate.frames.at(0).positions.at(t.a);
			const ModelVec3 b = plate.frames.at(0).positions.at(t.b);
			const ModelVec3 c = plate.frames.at(0).positions.at(t.c);
			const float z = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
			expect(z > 0.0f, "MDS front face points out (+Z) after the winding change");
		}
		expect(mesh.skinPaths.contains(QStringLiteral("models/test/body")) && mesh.skinPaths.contains(QStringLiteral("models/test/plate")),
			"MDS skin paths");
	}
	// tag_tip on the BONEFLAG_TAG bone: the game's tag axes are the
	// AnglesToAxis vectors.
	const ModelTag* restTag = findTag(mesh, QStringLiteral("tag_tip"), 0);
	const ModelTag* turnedTag = findTag(mesh, QStringLiteral("tag_tip"), 1);
	expect(restTag && tagMatches(*restTag, rowsMatrix({1, 0, 0, 23, 0, 1, 0, 2, 0, 0, 1, 3})), "MDS tag at rest");
	expect(turnedTag && tagMatches(*turnedTag, rowsMatrix({0, -1, 0, 11, 1, 0, 0, 14, 0, 0, 1, 3})),
		"MDS tag axes are forward (0,1,0), left (-1,0,0), up (0,0,1) after the turn");
	expect(mesh.tagCount == 2, "MDS tag count");
	expect(containsText(mesh.warnings, QStringLiteral("tag_mid")), "MDS warns about a tag on a bone that moves vertices");
	expect(containsText(mesh.detailLines, QStringLiteral("collapse map")), "MDS lists the minimum level of detail");
}

void testMdmWithMdx()
{
	const QByteArray mdm = buildMdm(mdmSpec());
	const QByteArray mdx = buildMdx(mdxSpec());
	const QString meshPath = QStringLiteral("models/test/body.mdm");
	{
		const QHash<QString, QByteArray> files{{QStringLiteral("models/test/body.mdx"), mdx}};
		const ModelMesh mesh = decodeModelMesh(meshPath, mdm, nullptr, {}, fakeCompanions(files));
		expectNoError(mesh, "MDM decodes with the MDX named like it");
		expect(mesh.version == 3 && mesh.geometryAvailable, "MDM version and geometry");
		expect(mesh.companionPaths == QStringList{QStringLiteral("models/test/body.mdx")}, "MDM records the MDX it read");
		expect(mesh.skeleton.sourceFormat == QStringLiteral("mdm") && mesh.skeleton.joints.size() == 3, "MDM skeleton from the MDX");
		expect(mesh.skeleton.clips.size() == 1 && mesh.skeleton.clips.first().sourcePath == QStringLiteral("models/test/body.mdx")
				&& mesh.skeleton.clips.first().frames.size() == 2,
			"MDM clip comes from the MDX");
		expect(mesh.frames.size() == 2 && mesh.surfaces.size() == 1, "MDM baked frames and surfaces");
		if (mesh.surfaces.size() == 1 && mesh.frames.size() == 2) {
			const QVector<ModelVec3>& turned = mesh.surfaces.first().frames.at(1).positions;
			expect(nearVec(turned.at(0), {11.0f, 8.0f, 3.0f}) && nearVec(turned.at(1), {11.5f, 5.5f, 3.0f}), "MDM vertices follow the MDX bones");
			expect(nearVec(mesh.surfaces.first().frames.at(0).positions.at(0), {21.0f, 2.0f, 3.0f}), "MDM vertex at rest");
		}
		// R_MDM_GetBoneTag: origin = matrix * offset + translation, axes = matrix * tag axes.
		const ModelTag* restTag = findTag(mesh, QStringLiteral("tag_weapon"), 0);
		const ModelTag* turnedTag = findTag(mesh, QStringLiteral("tag_weapon"), 1);
		expect(restTag && tagMatches(*restTag, rowsMatrix({0, 0, -1, 20, 0, 1, 0, 4, 1, 0, 0, 6})), "MDM tag at rest");
		expect(turnedTag && tagMatches(*turnedTag, rowsMatrix({0, 1, 0, 13, 0, 0, 1, 9, 1, 0, 0, 6})), "MDM tag after the turn");
	}
	{
		// A folder scan skips an MDX that lacks the bones the mesh uses.
		WolfSpec small = mdxSpec();
		small.bones.resize(2);
		small.frames = chainFrames(2);
		const QHash<QString, QByteArray> files{{QStringLiteral("models/test/a_small.mdx"), buildMdx(small)},
			{QStringLiteral("models/test/zz_anim.mdx"), mdx}};
		const ModelMesh mesh = decodeModelMesh(meshPath, mdm, nullptr, {}, fakeCompanions(files));
		expectNoError(mesh, "MDM decodes with an MDX found in its folder");
		expect(!mesh.skeleton.clips.isEmpty() && mesh.skeleton.clips.first().sourcePath == QStringLiteral("models/test/zz_anim.mdx"),
			"MDM picks the folder MDX that covers its bones");
		expect(containsText(mesh.detailLines, QStringLiteral("a_small.mdx")), "MDM lists the MDX it skipped");
	}
	{
		const QHash<QString, QByteArray> files{{QStringLiteral("animations/human/base/body.mdx"), mdx}};
		const ModelMesh mesh = decodeModelMesh(meshPath, mdm, nullptr, {}, fakeCompanions(files));
		expectNoError(mesh, "MDM decodes with ET's shared human MDX");
		expect(!mesh.skeleton.clips.isEmpty() && mesh.skeleton.clips.first().sourcePath == QStringLiteral("animations/human/base/body.mdx"),
			"MDM falls back to animations/human/base/body.mdx");
	}
	{
		const ModelMesh mesh = decodeModelMesh(meshPath, mdm, nullptr, {}, fakeCompanions({}));
		expect(!mesh.error.isEmpty() && mesh.error.contains(QStringLiteral("MDX")) && !mesh.geometryAvailable, "MDM without its MDX says so");
		const ModelMesh alone = decodeModelMesh(meshPath, mdm);
		expect(!alone.error.isEmpty() && !alone.geometryAvailable, "MDM without companions fails cleanly");
	}
}

void testMdxAlone()
{
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("animations/test/body.mdx"), buildMdx(mdxSpec()));
	expectNoError(mesh, "MDX decodes on its own");
	expect(mesh.format == ModelMeshFormat::Mdx && mesh.version == 2, "MDX format and version");
	expect(!mesh.geometryAvailable && mesh.surfaces.isEmpty(), "MDX has no geometry");
	expect(mesh.skeleton.joints.size() == 3 && mesh.skeleton.clips.size() == 1 && mesh.skeleton.clips.first().frames.size() == 2,
		"MDX skeleton and clip");
	expect(mesh.frames.size() == 2 && mesh.animations.size() == 1 && mesh.animations.first().frameCount == 2, "MDX bakes every frame");
	if (mesh.skeleton.clips.size() == 1 && mesh.skeleton.clips.first().frames.size() == 2) {
		expect(nearVec(modelJointTranslation(mesh.skeleton.clips.first().frames.at(1).at(2)), {11.0f, 10.0f, 3.0f}), "MDX child bone follows the turn");
	}
}

QHash<QString, QByteArray> glmCompanions(const QByteArray& gla)
{
	return {{QStringLiteral("models/test/test_anim.gla"), gla}};
}

void testGlmWithGla()
{
	const QByteArray gla = buildGla(glaSpec());
	const QByteArray glm = buildGlm(glmSpec());
	const QString meshPath = QStringLiteral("models/test/test.glm");
	const ModelMesh mesh = decodeModelMesh(meshPath, glm, nullptr, {}, fakeCompanions(glmCompanions(gla)));
	expectNoError(mesh, "GLM decodes with its GLA");
	expect(mesh.format == ModelMeshFormat::Glm && mesh.version == 6 && mesh.geometryAvailable, "GLM format, version and geometry");
	expect(mesh.companionPaths == QStringList{QStringLiteral("models/test/test_anim.gla")}, "GLM records the GLA it read");
	const ModelSkeleton& skeleton = mesh.skeleton;
	expect(skeleton.sourceFormat == QStringLiteral("ghoul2") && skeleton.joints.size() == 2, "GLM skeleton");
	if (skeleton.joints.size() == 2) {
		expect(skeleton.joints.at(0).name == QStringLiteral("root") && skeleton.joints.at(1).name == QStringLiteral("arm")
				&& skeleton.joints.at(1).parent == 0,
			"GLM joint names and parents");
		// bind = root turn * BasePoseMat: two quarter turns about Z at (0, 0, 10).
		expect(modelJointMatrixDistance(skeleton.joints.at(1).bind, rowsMatrix({-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 10})) < 1e-5,
			"GLM bind pose is the GLA base pose under the game's root turn");
	}
	expect(skeleton.clips.size() == 1 && skeleton.clips.first().frames.size() == 2
			&& skeleton.clips.first().sourcePath == QStringLiteral("models/test/test_anim.gla"),
		"GLM clip comes from the GLA");
	expect(mesh.frames.size() == 3, "GLM bakes the bind pose and both frames");
	if (mesh.frames.size() == 3) {
		expect(mesh.frames.at(0).name == QStringLiteral("bindpose") && mesh.frames.at(2).name == QStringLiteral("all1"), "GLM frame names");
	}
	expect(mesh.animations.size() == 1 && mesh.animations.first().firstFrame == 1 && mesh.animations.first().frameCount == 2, "GLM animation range");
	expect(mesh.surfaces.size() == 2, "GLM keeps the body and the off surface and leaves out the bolt surface");
	if (mesh.surfaces.size() == 2 && mesh.frames.size() == 3) {
		const ModelSurface& body = mesh.surfaces.at(0);
		expect(body.name == QStringLiteral("body") && body.skinPaths == QStringList{QStringLiteral("models/test/glm_body")}, "GLM surface name and shader");
		expect(body.texCoords.size() == 3 && nearly(body.texCoords.at(1).u, 0.3f) && nearly(body.texCoords.at(1).v, 0.4f), "GLM texture coordinates");
		expect(body.triangles.size() == 1 && body.triangles.first().b == 2 && body.triangles.first().c == 1,
			"GLM triangles are reversed to counter-clockwise");
		const QVector<ModelVec3>& bind = body.frames.at(0).positions;
		expect(nearVec(bind.at(0), rootTurn({0.0f, 0.0f, 15.0f})) && nearVec(bind.at(1), rootTurn({0.0f, 2.0f, 12.0f}))
				&& nearVec(bind.at(2), rootTurn({5.0f, 0.0f, 0.0f})),
			"GLM bind pose is the file's vertices under the root turn");
		expect(nearVec(body.frames.at(1).positions.at(0), bind.at(0)), "GLM frame 0 matches the bind pose");
		// Frame 1: final[arm] = root * U[root] * U[arm] with U[arm] the stored quaternion.
		const ModelJointMatrix arm = decodeCompBone(armTurn());
		const ModelVec3 a = rootTurn(modelJointTransformPoint(arm, {0.0f, 0.0f, 15.0f}));
		expect(nearVec(body.frames.at(2).positions.at(0), a) && nearVec(a, {5.0f, 0.0f, 10.0f}, 2e-3f), "GLM vertex follows the turned bone");
		const float w0 = float(kHalfWeight) / 1023.0f;
		const ModelVec3 b0 = rootTurn({0.0f, 2.0f, 12.0f});
		const ModelVec3 b1 = rootTurn(modelJointTransformPoint(arm, {0.0f, 2.0f, 12.0f}));
		const ModelVec3 b{w0 * b0.x + (1 - w0) * b1.x, w0 * b0.y + (1 - w0) * b1.y, w0 * b0.z + (1 - w0) * b1.z};
		expect(nearVec(body.frames.at(2).positions.at(1), b), "GLM two-bone vertex uses the ten-bit weight");
		expect(nearVec(body.frames.at(2).positions.at(2), bind.at(2)), "GLM root vertex stays put");
		expect(nearVec(body.frames.at(0).normals.at(0), {-1.0f, 0.0f, 0.0f}), "GLM bind normal under the root turn");
		expect(nearVec(body.frames.at(2).normals.at(0), {0.0f, 0.0f, 1.0f}, 2e-3f), "GLM normal turns with its bone");
		expect(mesh.surfaces.at(1).name == QStringLiteral("caps_off"), "GLM off surface keeps its geometry");
	}
	expect(containsText(mesh.detailLines, QStringLiteral("caps_off")) && containsText(mesh.detailLines, QStringLiteral("*hand")),
		"GLM lists the off and bolt surfaces");
	expect(!mesh.skinPaths.contains(QStringLiteral("[nomaterial]")), "GLM drops Carcass's no-material placeholder");
	// The bolt surface becomes a tag on the arm, matching the game's bolt.
	expect(mesh.skeleton.tags.size() == 1 && mesh.skeleton.tags.first().name == QStringLiteral("*hand") && mesh.skeleton.tags.first().joint == 1,
		"GLM bolt surface becomes a tag on its bone");
	const ModelVec3 corners[3]{{0.0f, 0.0f, 12.0f}, {4.0f, 0.0f, 12.0f}, {0.0f, 1.0f, 12.0f}};
	ModelVec3 bindCorners[3];
	ModelVec3 turnedCorners[3];
	const ModelJointMatrix arm = decodeCompBone(armTurn());
	for (int corner = 0; corner < 3; ++corner) {
		bindCorners[corner] = rootTurn(corners[corner]);
		turnedCorners[corner] = rootTurn(modelJointTransformPoint(arm, corners[corner]));
	}
	const ModelTag* bindTag = findTag(mesh, QStringLiteral("*hand"), 0);
	const ModelTag* turnedTag = findTag(mesh, QStringLiteral("*hand"), 2);
	expect(bindTag && tagMatches(*bindTag, expectedBolt(bindCorners)), "GLM tag in the bind pose");
	expect(turnedTag && tagMatches(*turnedTag, expectedBolt(turnedCorners)), "GLM tag follows the turned bone like the game's bolt");

	// The mesh folder's own GLA when the named one is missing.
	{
		const QHash<QString, QByteArray> files{{QStringLiteral("models/test/other.gla"), gla}};
		const ModelMesh fallback = decodeModelMesh(meshPath, glm, nullptr, {}, fakeCompanions(files));
		expectNoError(fallback, "GLM decodes with the GLA beside it");
		expect(!fallback.skeleton.clips.isEmpty() && fallback.skeleton.clips.first().sourcePath == QStringLiteral("models/test/other.gla"),
			"GLM falls back to the GLA in its folder");
	}
	{
		const ModelMesh missing = decodeModelMesh(meshPath, glm, nullptr, {}, fakeCompanions({}));
		expect(!missing.error.isEmpty() && missing.error.contains(QStringLiteral("GLA")) && !missing.geometryAvailable, "GLM without its GLA says so");
	}
}

void testGlaAlone()
{
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/test/test_anim.gla"), buildGla(glaSpec()));
	expectNoError(mesh, "GLA decodes on its own");
	expect(mesh.format == ModelMeshFormat::Gla && mesh.version == 6, "GLA format and version");
	expect(!mesh.geometryAvailable && mesh.surfaces.isEmpty(), "GLA has no geometry");
	expect(mesh.skeleton.joints.size() == 2 && mesh.skeleton.clips.size() == 1 && mesh.skeleton.clips.first().frames.size() == 2, "GLA skeleton and clip");
	expect(mesh.frames.size() == 2 && mesh.animations.size() == 1 && mesh.animations.first().firstFrame == 0, "GLA bakes every frame and no bind pose");
	if (mesh.skeleton.clips.size() == 1 && mesh.skeleton.clips.first().frames.size() == 2) {
		// M = root * U[root] * U[arm] * BasePoseMat.
		const ModelJointMatrix root = rowsMatrix({0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0});
		const ModelJointMatrix expected = modelJointMultiply(modelJointMultiply(root, decodeCompBone(armTurn())), kArmBase);
		expect(modelJointMatrixDistance(mesh.skeleton.clips.first().frames.at(1).at(1), expected) < 1e-4, "GLA frame matrix is final * BasePoseMat");
	}
}

void testJediAcademyRemap()
{
	GlaSpec jka;
	for (int bone = 0; bone < 53; ++bone) {
		jka.bones.append({QByteArray("bone") + QByteArray::number(bone), bone - 1, kIdentity, kIdentity});
	}
	jka.pool = {compBone(1.0, 0.0, 0.0, 0.0, {0.0f, 0.0f, 0.0f})};
	jka.frames = {QVector<int>(53, 0)};
	GlmSpec old;
	old.animName = "models/players/_humanoid/_humanoid";
	old.numBones = 72;
	GlmSurfaceSpec surface;
	surface.name = "torso";
	surface.vertices = {
		{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, {0}, {}},
		{{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, 0.0f, 0.0f, {0}, {}},
		{{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, 0.0f, 0.0f, {0}, {}},
	};
	surface.triangles = {{0, 1, 2}};
	surface.boneRefs = {71};
	old.surfaces = {surface};
	const QString path = QStringLiteral("models/players/kyle/model.glm");
	const QString glaPath = QStringLiteral("models/players/_humanoid/_humanoid.gla");
	// Jedi Academy's remap table is GPL-2.0 only and is not reproduced, so a
	// Jedi Outcast mesh refuses a smaller skeleton and says why.
	const ModelMesh refused = decodeModelMesh(path, buildGlm(old), nullptr, {}, fakeCompanions({{glaPath, buildGla(jka)}}));
	expect(!refused.error.isEmpty() && refused.error.contains(QStringLiteral("Jedi Outcast")) && refused.surfaces.isEmpty(),
		"Jedi Outcast mesh names the skeleton it needs instead of a Jedi Academy one");
	GlaSpec jk2 = jka;
	for (int bone = 53; bone < 72; ++bone) {
		jk2.bones.append({QByteArray("bone") + QByteArray::number(bone), bone - 1, kIdentity, kIdentity});
	}
	jk2.frames = {QVector<int>(72, 0)};
	const ModelMesh kept = decodeModelMesh(path, buildGlm(old), nullptr, {}, fakeCompanions({{glaPath, buildGla(jk2)}}));
	expectNoError(kept, "Jedi Outcast mesh decodes against its own skeleton");
	expect(!kept.surfaces.isEmpty() && !kept.surfaces.first().skinning.influences.isEmpty()
			&& kept.surfaces.first().skinning.influences.first().joint == 71,
		"Jedi Outcast bones stay as they are on a 72-bone skeleton");
}

void expectFails(const ModelMesh& mesh, const char* message)
{
	expect(!mesh.error.isEmpty() && !mesh.geometryAvailable && mesh.surfaces.isEmpty(), message);
}

void testMalformed()
{
	const QString mdsPath = QStringLiteral("models/test/body.mds");
	const QByteArray mds = buildMds(mdsSpec());
	const auto decodeMds = [&](const QByteArray& bytes) { return decodeModelMesh(mdsPath, bytes); };
	fuzz(mds, decodeMds, "MDS truncated at every byte stays safe", "MDS with flipped bytes stays safe");
	{
		QByteArray bad = mds;
		patchI32(bad, 4, 5);
		expectFails(decodeMds(bad), "MDS with a bad version fails");
		bad = mds;
		patchI32(bad, 84, 100000);
		expectFails(decodeMds(bad), "MDS with a huge bone count fails");
		bad = mds;
		patchI32(bad, 88, 1 << 30);
		expectFails(decodeMds(bad), "MDS with frames outside the file fails");
		bad = mds;
		patchI32(bad, 100, -1);
		expectFails(decodeMds(bad), "MDS with a negative surface count fails");
		bad = mds;
		patchI32(bad, 104, -500);
		expectFails(decodeMds(bad), "MDS with surfaces before the file fails");
		WolfSpec spec = mdsSpec();
		spec.bones[1].parent = 2;
		expectFails(decodeMds(buildMds(spec)), "MDS with a parent pointing forward fails");
		spec = mdsSpec();
		spec.surfaces[0].vertices[0].weights[0].bone = 9;
		expectFails(decodeMds(buildMds(spec)), "MDS with a weight beyond the skeleton fails");
		spec = mdsSpec();
		spec.surfaces[0].boneRefs.append(9);
		expectFails(decodeMds(buildMds(spec)), "MDS with a bone reference beyond the skeleton fails");
		spec = mdsSpec();
		spec.tags[0].bone = 9;
		expectFails(decodeMds(buildMds(spec)), "MDS with a tag beyond the skeleton fails");
		spec = mdsSpec();
		spec.surfaces[0].vertices[1].weights.clear();
		expectFails(decodeMds(buildMds(spec)), "MDS with a vertex without weights fails");
	}

	const QString mdmPath = QStringLiteral("models/test/body.mdm");
	const QByteArray mdm = buildMdm(mdmSpec());
	const QByteArray mdx = buildMdx(mdxSpec());
	const auto decodeMdmWith = [&](const QByteArray& mesh, const QByteArray& skeleton) {
		return decodeModelMesh(mdmPath, mesh, nullptr, {}, fakeCompanions({{QStringLiteral("models/test/body.mdx"), skeleton}}));
	};
	fuzz(mdm, [&](const QByteArray& bytes) { return decodeMdmWith(bytes, mdx); }, "MDM truncated at every byte stays safe",
		"MDM with flipped bytes stays safe");
	fuzz(mdx, [&](const QByteArray& bytes) { return decodeMdmWith(mdm, bytes); }, "MDM with a truncated MDX stays safe",
		"MDM with a damaged MDX stays safe");
	{
		QByteArray bad = mdm;
		patchI32(bad, 4, 2);
		expectFails(decodeMdmWith(bad, mdx), "MDM with a bad version fails");
		bad = mdm;
		patchI32(bad, 80, 1000000);
		expectFails(decodeMdmWith(bad, mdx), "MDM with a huge surface count fails");
		WolfSpec spec = mdmSpec();
		spec.surfaces[0].vertices[0].weights[0].bone = 7;
		expectFails(decodeMdmWith(buildMdm(spec), mdx), "MDM weighted beyond its MDX's bones fails");
		spec = mdmSpec();
		spec.tags[0].bone = 50;
		expectFails(decodeMdmWith(buildMdm(spec), mdx), "MDM with a tag beyond its MDX's bones fails");
		WolfSpec skeleton = mdxSpec();
		skeleton.bones[1].parent = 2;
		expectFails(decodeMdmWith(mdm, buildMdx(skeleton)), "MDM whose MDX has a parent pointing forward fails");
	}

	const QString mdxPath = QStringLiteral("animations/test/body.mdx");
	const auto decodeMdx = [&](const QByteArray& bytes) { return decodeModelMesh(mdxPath, bytes); };
	fuzz(mdx, decodeMdx, "MDX truncated at every byte stays safe", "MDX with flipped bytes stays safe");
	{
		QByteArray bad = mdx;
		patchI32(bad, 4, 1);
		expectFails(decodeMdx(bad), "MDX with a bad version fails");
		bad = mdx;
		patchI32(bad, 72, 0);
		expectFails(decodeMdx(bad), "MDX without frames fails");
		bad = mdx;
		patchI32(bad, 84, -4);
		expectFails(decodeMdx(bad), "MDX with bones before the file fails");
		WolfSpec spec = mdxSpec();
		spec.bones[2].parent = 2;
		expectFails(decodeMdx(buildMdx(spec)), "MDX with a bone parenting itself fails");
	}

	const QString glaPath = QStringLiteral("models/test/test_anim.gla");
	const QByteArray gla = buildGla(glaSpec());
	const auto decodeGla = [&](const QByteArray& bytes) { return decodeModelMesh(glaPath, bytes); };
	fuzz(gla, decodeGla, "GLA truncated at every byte stays safe", "GLA with flipped bytes stays safe");
	{
		QByteArray bad = gla;
		patchI32(bad, 4, 5);
		expectFails(decodeGla(bad), "GLA with a bad version fails");
		bad = gla;
		patchI32(bad, 84, 0);
		expectFails(decodeGla(bad), "GLA without bones fails");
		bad = gla;
		patchI32(bad, 76, 1 << 28);
		expectFails(decodeGla(bad), "GLA with frames outside the file fails");
		GlaSpec spec = glaSpec();
		spec.bones[0].parent = 1;
		expectFails(decodeGla(buildGla(spec)), "GLA with a parent pointing forward fails");
		spec = glaSpec();
		spec.frames[1][1] = 9;
		expectFails(decodeGla(buildGla(spec)), "GLA pointing outside its bone pool fails");
	}

	const QString glmPath = QStringLiteral("models/test/test.glm");
	const QByteArray glm = buildGlm(glmSpec());
	const auto decodeGlmWith = [&](const QByteArray& mesh, const QByteArray& skeleton) {
		return decodeModelMesh(glmPath, mesh, nullptr, {}, fakeCompanions(glmCompanions(skeleton)));
	};
	fuzz(glm, [&](const QByteArray& bytes) { return decodeGlmWith(bytes, gla); }, "GLM truncated at every byte stays safe",
		"GLM with flipped bytes stays safe");
	fuzz(gla, [&](const QByteArray& bytes) { return decodeGlmWith(glm, bytes); }, "GLM with a truncated GLA stays safe",
		"GLM with a damaged GLA stays safe");
	{
		QByteArray bad = glm;
		patchI32(bad, 4, 5);
		expectFails(decodeGlmWith(bad, gla), "GLM with a bad version fails");
		bad = glm;
		patchI32(bad, 152, 100000);
		expectFails(decodeGlmWith(bad, gla), "GLM with a huge surface count fails");
		GlmSpec spec = glmSpec();
		spec.lodOffsetDelta = -100000;
		expectFails(decodeGlmWith(buildGlm(spec), gla), "GLM with its level of detail outside the file fails");
		spec = glmSpec();
		spec.surfaces[0].boneRefs = {0, 5};
		expectFails(decodeGlmWith(buildGlm(spec), gla), "GLM referring to bones beyond its GLA fails");
		spec = glmSpec();
		spec.surfaces[0].vertices[0].refs = {3};
		expectFails(decodeGlmWith(buildGlm(spec), gla), "GLM vertex beyond its surface's bone references fails");
		spec = glmSpec();
		spec.surfaces[1].parent = 7;
		expectFails(decodeGlmWith(buildGlm(spec), gla), "GLM surface with a parent outside the list fails");
		GlaSpec skeleton = glaSpec();
		skeleton.bones[1].parent = 1;
		expectFails(decodeGlmWith(glm, buildGla(skeleton)), "GLM whose GLA has a bone parenting itself fails");
	}
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	testMds();
	testMdmWithMdx();
	testMdxAlone();
	testGlmWithGla();
	testGlaAlone();
	testJediAcademyRemap();
	testMalformed();
	if (failures > 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "model_format_mds_ghoul2_smoke_test passed\n";
	return 0;
}
