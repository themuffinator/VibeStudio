// Smoke tests for the GoldSrc studio (Half-Life MDL), Hexen II RAPO and
// Heretic II FM decoders. Every fixture is built byte by byte here; no game
// data is used.
#include "core/idtech_image.h"
#include "core/model_mesh.h"
#include "core/model_skeleton.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace vibestudio;

namespace {

int g_failures = 0;

bool expect(bool condition, const char* message)
{
	if (!condition) {
		++g_failures;
		std::cerr << "FAIL: " << message << "\n";
	}
	return condition;
}

bool nearly(float value, float expected, float epsilon = 1.0e-3f)
{
	return std::fabs(value - expected) <= epsilon;
}

bool nearlyVec(const ModelVec3& value, float x, float y, float z, float epsilon = 1.0e-3f)
{
	return nearly(value.x, x, epsilon) && nearly(value.y, y, epsilon) && nearly(value.z, z, epsilon);
}

ModelVec3 faceNormal(const ModelSurface& surface, int frame, const ModelTriangle& triangle)
{
	const QVector<ModelVec3>& p = surface.frames.at(frame).positions;
	const ModelVec3 a = p.at(triangle.a), b = p.at(triangle.b), c = p.at(triangle.c);
	const ModelVec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
	const ModelVec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
	return ModelVec3{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
}

class Writer {
public:
	QByteArray data;
	int size() const { return int(data.size()); }
	void u8(int value) { data.append(char(quint8(value))); }
	void i16(int value)
	{
		const quint16 raw = quint16(qint16(value));
		data.append(char(raw & 0xFF));
		data.append(char((raw >> 8) & 0xFF));
	}
	void i32(qint32 value)
	{
		const quint32 raw = quint32(value);
		for (int shift = 0; shift < 32; shift += 8) {
			data.append(char((raw >> shift) & 0xFF));
		}
	}
	void f32(float value)
	{
		quint32 raw = 0;
		std::memcpy(&raw, &value, sizeof(raw));
		i32(qint32(raw));
	}
	void vec(float x, float y, float z)
	{
		f32(x);
		f32(y);
		f32(z);
	}
	void fixed(const char* text, int length)
	{
		QByteArray bytes(text);
		bytes = bytes.left(length);
		bytes.append(QByteArray(length - bytes.size(), '\0'));
		data.append(bytes);
	}
	void zeros(int count) { data.append(QByteArray(count, '\0')); }
	void align4()
	{
		while (data.size() % 4 != 0) {
			u8(0);
		}
	}
	void patchI32(int offset, qint32 value)
	{
		const quint32 raw = quint32(value);
		for (int index = 0; index < 4; ++index) {
			data[offset + index] = char((raw >> (index * 8)) & 0xFF);
		}
	}
	void patchF32(int offset, float value)
	{
		quint32 raw = 0;
		std::memcpy(&raw, &value, sizeof(raw));
		patchI32(offset, qint32(raw));
	}
};

void patchI32(QByteArray& bytes, int offset, qint32 value)
{
	const quint32 raw = quint32(value);
	for (int index = 0; index < 4; ++index) {
		bytes[offset + index] = char((raw >> (index * 8)) & 0xFF);
	}
}

void patchI16(QByteArray& bytes, int offset, int value)
{
	const quint16 raw = quint16(qint16(value));
	bytes[offset] = char(raw & 0xFF);
	bytes[offset + 1] = char((raw >> 8) & 0xFF);
}

ModelCompanionSource fakeCompanions(const QHash<QString, QByteArray>& files)
{
	ModelCompanionSource source;
	source.read = [files](const QString& path, QByteArray* bytes, QString* error) {
		for (auto it = files.cbegin(); it != files.cend(); ++it) {
			if (it.key().compare(path, Qt::CaseInsensitive) == 0) {
				*bytes = it.value();
				return true;
			}
		}
		if (error) {
			*error = QStringLiteral("not found: %1").arg(path);
		}
		return false;
	};
	return source;
}

// A decoded mesh is either an error with nothing published, or internally
// consistent.
bool consistent(const ModelMesh& mesh)
{
	if (!mesh.error.isEmpty()) {
		return !mesh.geometryAvailable && mesh.surfaces.isEmpty() && mesh.frames.isEmpty();
	}
	if (mesh.geometryAvailable && mesh.surfaces.isEmpty()) {
		return false;
	}
	for (const ModelSurface& surface : mesh.surfaces) {
		if (surface.texCoords.size() != surface.vertexCount || surface.frames.size() != mesh.frames.size()) {
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

quint32 g_seed = 0x2545F491u;
quint32 nextRandom()
{
	g_seed = g_seed * 1664525u + 1013904223u;
	return g_seed >> 8;
}

// Truncates `bytes` at every offset and flips bytes with a fixed seed; the
// decoder must never crash and must return an error or a consistent mesh.
// With `truncationMustFail`, every truncation must be an error.
void fuzz(const char* label, const QString& path, const QByteArray& bytes, bool truncationMustFail, const ModelCompanionSource& companions = {})
{
	int inconsistent = 0;
	int acceptedTruncations = 0;
	for (int length = 0; length < bytes.size(); ++length) {
		const ModelMesh mesh = decodeModelMesh(path, bytes.left(length), nullptr, {}, companions);
		if (!consistent(mesh)) {
			++inconsistent;
		}
		if (mesh.error.isEmpty()) {
			++acceptedTruncations;
		}
	}
	for (int iteration = 0; iteration < 400; ++iteration) {
		QByteArray damaged = bytes;
		const int flips = 1 + int(nextRandom() % 4);
		for (int flip = 0; flip < flips; ++flip) {
			const int at = int(nextRandom() % quint32(damaged.size()));
			damaged[at] = char(nextRandom() & 0xFF);
		}
		const ModelMesh mesh = decodeModelMesh(path, damaged, nullptr, {}, companions);
		if (!consistent(mesh)) {
			++inconsistent;
		}
	}
	if (inconsistent > 0) {
		std::cerr << label << ": " << inconsistent << " inconsistent decode(s)\n";
	}
	expect(inconsistent == 0, "fuzzed decodes return an error or a consistent mesh");
	if (truncationMustFail) {
		if (acceptedTruncations > 0) {
			std::cerr << label << ": " << acceptedTruncations << " truncation(s) decoded without error\n";
		}
		expect(acceptedTruncations == 0, "every truncation of the fixture is an error");
	}
}

// ---------------------------------------------------------------------------
// Half-Life studio fixtures
// ---------------------------------------------------------------------------

constexpr float kPi = 3.14159265358979323846f;
// Animation values of 16384 make a quarter turn.
constexpr float kAngleScale = (kPi * 0.5f) / 16384.0f;

struct StudioLayout {
	int sequenceOffset = 0;
	int boneOffset = 0;
	int bodyPartOffset = 0;
	int modelOffset = 0;
	int meshOffset = 0;
	int textureOffset = 0;
	int stripCommandOffset = 0;
};

void writeBone(Writer& w, const char* name, int parent, const float value[6])
{
	w.fixed(name, 32);
	w.i32(parent);
	w.i32(0);
	for (int index = 0; index < 6; ++index) {
		w.i32(-1);
	}
	for (int index = 0; index < 6; ++index) {
		w.f32(value[index]);
	}
	for (int index = 0; index < 6; ++index) {
		w.f32(index < 3 ? 0.5f : kAngleScale);
	}
}

void writeSequence(Writer& w, const char* label, float fps, int flags, int frames, int group, int animIndex)
{
	w.fixed(label, 32);
	w.f32(fps);
	w.i32(flags);
	w.i32(0); // activity
	w.i32(0); // actweight
	w.i32(0); // numevents
	w.i32(0); // eventindex
	w.i32(frames);
	w.i32(0); // numpivots
	w.i32(0); // pivotindex
	w.i32(0); // motiontype
	w.i32(0); // motionbone
	w.vec(0, 0, 0); // linearmovement
	w.i32(0); // automoveposindex
	w.i32(0); // automoveangleindex
	w.vec(-8, -8, 0); // bbmin
	w.vec(8, 8, 8); // bbmax
	w.i32(1); // numblends
	w.i32(animIndex);
	w.i32(0); w.i32(0); // blendtype
	w.f32(0); w.f32(0); // blendstart
	w.f32(0); w.f32(0); // blendend
	w.i32(0); // blendparent
	w.i32(group);
	w.i32(0); w.i32(0); w.i32(0); w.i32(0); // entrynode, exitnode, nodeflags, nextseq
}

void writeCommand(Writer& w, int vertex, int normal, int s, int t)
{
	w.i16(vertex);
	w.i16(normal);
	w.i16(s);
	w.i16(t);
}

// The model: two bones (root and child at x = 10), one body part whose model
// 0 has a strip mesh (a 10 x 10 square on bone 0) and a fan mesh (a square in
// the child's YZ plane, plus a strip reusing a vertex with other texture
// coordinates), one 4 x 4 texture, an attachment on the child, sequence
// "idle" (2 frames: the root rises by 2 and the child yaws a quarter turn in
// frame 1) and, with `groups`, sequence "walk" in group 1 ("<name>01.mdl").
// The idle animation data closes the file.
QByteArray buildStudio(bool geometry, bool textures, bool groups, StudioLayout* layout = nullptr)
{
	Writer w;
	w.zeros(244);
	StudioLayout local;
	int groupOffset = 0;
	int skinOffset = 0;
	int attachmentOffset = 0;
	int sequenceCount = 0;
	int idleAnimPatch = 0;
	if (geometry) {
		local.boneOffset = w.size();
		const float rootValue[6] = {0, 0, 0, 0, 0, 0};
		const float childValue[6] = {10, 0, 0, 0, 0, 0};
		writeBone(w, "root", -1, rootValue);
		writeBone(w, "child", 0, childValue);
		groupOffset = w.size();
		w.fixed("default", 32);
		w.fixed("", 64);
		w.i32(0);
		w.i32(0);
		if (groups) {
			w.fixed("walk", 32);
			w.fixed("models/hl01.mdl", 64);
			w.i32(0);
			w.i32(0);
		}
		local.sequenceOffset = w.size();
		idleAnimPatch = w.size() + 124;
		writeSequence(w, "idle", 10.0f, 1, 2, 0, 0);
		sequenceCount = 1;
		if (groups) {
			writeSequence(w, "walk", 15.0f, 0, 3, 1, 76);
			sequenceCount = 2;
		}
	}
	int textureDataOffset = 0;
	if (textures) {
		local.textureOffset = w.size();
		w.fixed("skin.bmp", 64);
		w.i32(0); // flags
		w.i32(4);
		w.i32(4);
		const int indexPatch = w.size();
		w.i32(0);
		skinOffset = w.size();
		w.i16(0);
		w.align4();
		textureDataOffset = w.size();
		w.patchI32(indexPatch, textureDataOffset);
		for (int index = 0; index < 16; ++index) {
			w.u8(index);
		}
		for (int index = 0; index < 256; ++index) {
			w.u8((index * 10) & 0xFF);
			w.u8((index * 5) & 0xFF);
			w.u8(255 - index);
		}
	}
	if (geometry) {
		local.bodyPartOffset = w.size();
		w.fixed("body", 64);
		w.i32(1);
		w.i32(1);
		const int modelIndexPatch = w.size();
		w.i32(0);
		local.modelOffset = w.size();
		w.patchI32(modelIndexPatch, local.modelOffset);
		w.fixed("body_ref", 64);
		w.i32(0);
		w.f32(20.0f);
		w.i32(2); // nummesh
		const int meshIndexPatch = w.size();
		w.i32(0);
		w.i32(8); // numverts
		const int vertInfoPatch = w.size();
		w.i32(0);
		const int vertPatch = w.size();
		w.i32(0);
		w.i32(2); // numnorms
		const int normInfoPatch = w.size();
		w.i32(0);
		const int normPatch = w.size();
		w.i32(0);
		w.i32(0);
		w.i32(0);

		local.meshOffset = w.size();
		w.patchI32(meshIndexPatch, local.meshOffset);
		const int mesh0 = w.size();
		w.i32(2); w.i32(0); w.i32(0); w.i32(1); w.i32(0);
		const int mesh1 = w.size();
		w.i32(3); w.i32(0); w.i32(0); w.i32(1); w.i32(1);

		w.patchI32(vertInfoPatch, w.size());
		for (int index = 0; index < 8; ++index) {
			w.u8(index < 4 ? 0 : 1);
		}
		w.align4();
		w.patchI32(vertPatch, w.size());
		w.vec(0, 0, 0);
		w.vec(10, 0, 0);
		w.vec(0, 10, 0);
		w.vec(10, 10, 0);
		w.vec(0, 0, 0);
		w.vec(0, 0, 5);
		w.vec(0, 5, 5);
		w.vec(0, 5, 0);
		w.patchI32(normInfoPatch, w.size());
		w.u8(0);
		w.u8(1);
		w.align4();
		w.patchI32(normPatch, w.size());
		w.vec(0, 0, -1);
		w.vec(1, 0, 0);

		local.stripCommandOffset = w.size();
		w.patchI32(mesh0 + 4, w.size());
		w.i16(4);
		writeCommand(w, 0, 0, 0, 0);
		writeCommand(w, 1, 0, 4, 0);
		writeCommand(w, 2, 0, 0, 4);
		writeCommand(w, 3, 0, 4, 4);
		w.i16(0);
		w.patchI32(mesh1 + 4, w.size());
		w.i16(-4);
		writeCommand(w, 4, 1, 1, 1);
		writeCommand(w, 5, 1, 2, 1);
		writeCommand(w, 6, 1, 2, 2);
		writeCommand(w, 7, 1, 1, 2);
		w.i16(3);
		writeCommand(w, 4, 1, 3, 3);
		writeCommand(w, 5, 1, 2, 1);
		writeCommand(w, 7, 1, 1, 2);
		w.i16(0);
		w.align4();

		attachmentOffset = w.size();
		w.fixed("muzzle", 32);
		w.i32(0);
		w.i32(1);
		w.vec(1, 2, 3);
		w.zeros(36);

		// Idle animation: mstudioanim_t for both bones, then value runs.
		const int anim = w.size();
		w.patchI32(idleAnimPatch, anim);
		// Bone 0: channel 2 (z) -> run {valid 1, total 2} [4].
		w.i16(0); w.i16(0); w.i16(24); w.i16(0); w.i16(0); w.i16(0);
		// Bone 1: channel 5 (yaw) -> runs {1, 1} [0] and {1, 1} [16384].
		w.i16(0); w.i16(0); w.i16(0); w.i16(0); w.i16(0); w.i16(16);
		w.u8(1); w.u8(2); w.i16(4);
		w.u8(1); w.u8(1); w.i16(0);
		w.u8(1); w.u8(1); w.i16(16384);
	}

	w.data.replace(0, 4, "IDST");
	w.patchI32(4, 10);
	QByteArray name("hl.mdl");
	w.data.replace(8, name.size(), name);
	w.patchI32(72, w.size());
	w.patchF32(76, 0.0f);
	w.patchF32(80, 0.0f);
	w.patchF32(84, 24.0f); // eye position
	w.patchI32(140, geometry ? 2 : 0);
	w.patchI32(144, local.boneOffset);
	w.patchI32(164, sequenceCount);
	w.patchI32(168, local.sequenceOffset);
	w.patchI32(172, geometry ? (groups ? 2 : 1) : 0);
	w.patchI32(176, groupOffset);
	w.patchI32(180, textures ? 1 : 0);
	w.patchI32(184, local.textureOffset);
	w.patchI32(188, textureDataOffset);
	w.patchI32(192, textures ? 1 : 0);
	w.patchI32(196, textures ? 1 : 0);
	w.patchI32(200, skinOffset);
	w.patchI32(204, geometry ? 1 : 0);
	w.patchI32(208, local.bodyPartOffset);
	w.patchI32(212, geometry ? 1 : 0);
	w.patchI32(216, attachmentOffset);
	if (layout) {
		*layout = local;
	}
	return w.data;
}

// Sequence group 1: an IDSQ header and the walk animation at offset 76,
// where the child rolls a quarter turn about X in frame 1 and holds it in
// frame 2 (a run of three frames with two values repeats the last).
QByteArray buildStudioGroup()
{
	Writer w;
	w.fixed("IDSQ", 4);
	w.i32(10);
	w.fixed("hl01.mdl", 64);
	w.i32(0);
	// Bone 0: no animated channels.
	w.zeros(12);
	// Bone 1: channel 3 (roll) -> run {valid 2, total 3} [0, 16384].
	w.i16(0); w.i16(0); w.i16(0); w.i16(12); w.i16(0); w.i16(0);
	w.u8(2); w.u8(3); w.i16(0); w.i16(16384);
	w.patchI32(72, w.size());
	return w.data;
}

int cornerWithUv(const ModelSurface& surface, float u, float v)
{
	for (int index = 0; index < surface.texCoords.size(); ++index) {
		if (nearly(surface.texCoords.at(index).u, u) && nearly(surface.texCoords.at(index).v, v)) {
			return index;
		}
	}
	return -1;
}

const ModelTag* tagAt(const ModelMesh& mesh, const QString& name, int frame)
{
	for (const ModelTag& tag : mesh.tags) {
		if (tag.name == name && tag.frameIndex == frame) {
			return &tag;
		}
	}
	return nullptr;
}

void runHalfLifeGeometry()
{
	StudioLayout layout;
	const QByteArray model = buildStudio(true, true, true, &layout);
	const QByteArray group = buildStudioGroup();
	const ModelCompanionSource companions = fakeCompanions({{QStringLiteral("models/hl01.mdl"), group}});
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/hl.mdl"), model, nullptr, {}, companions);
	if (!expect(mesh.error.isEmpty(), "the Half-Life fixture decodes")) {
		std::cerr << qPrintable(mesh.error) << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::HalfLifeMdl, "IDST is detected as a Half-Life model");
	expect(mesh.version == 10, "the Half-Life version is 10");
	expect(mesh.geometryAvailable, "the Half-Life model has geometry");
	expect(mesh.companionPaths.contains(QStringLiteral("models/hl01.mdl")), "the sequence group file is read as a companion");

	// Skeleton.
	expect(mesh.skeleton.sourceFormat == QStringLiteral("studio"), "the skeleton is a studio skeleton");
	if (expect(mesh.skeleton.joints.size() == 2, "two joints")) {
		expect(mesh.skeleton.joints.at(0).name == QStringLiteral("root") && mesh.skeleton.joints.at(0).parent == -1, "joint 0 is the root");
		expect(mesh.skeleton.joints.at(1).name == QStringLiteral("child") && mesh.skeleton.joints.at(1).parent == 0, "joint 1 is the child");
		const ModelVec3 childBind = modelJointTranslation(mesh.skeleton.joints.at(1).bind);
		expect(nearlyVec(childBind, 10, 0, 0), "the child's bind pose is its rest position");
	}

	// Clips and baked frames: bind pose, idle (2), walk (2).
	if (expect(mesh.skeleton.clips.size() == 2, "two sequences become clips")) {
		const ModelSkeletalClip& idle = mesh.skeleton.clips.at(0);
		const ModelSkeletalClip& walk = mesh.skeleton.clips.at(1);
		expect(idle.name == QStringLiteral("idle") && idle.frames.size() == 2, "the idle clip has two frames");
		expect(walk.name == QStringLiteral("walk") && walk.frames.size() == 3, "the walk clip has three frames");
		expect(idle.loops && !walk.loops, "looping follows STUDIO_LOOPING");
		expect(nearly(float(idle.framesPerSecond), 10.0f) && nearly(float(walk.framesPerSecond), 15.0f), "clip rates come from the sequences");
		expect(walk.sourcePath == QStringLiteral("models/hl01.mdl"), "the walk clip comes from its group file");
		expect(idle.sourcePath.isEmpty(), "the idle clip comes from the model");
	}
	expect(mesh.frames.size() == 6, "six baked frames");
	if (expect(mesh.animations.size() == 2, "two animations")) {
		expect(mesh.animations.at(0).name == QStringLiteral("idle") && mesh.animations.at(0).firstFrame == 1 && mesh.animations.at(0).frameCount == 2,
			"idle follows the bind pose");
		expect(mesh.animations.at(1).name == QStringLiteral("walk") && mesh.animations.at(1).firstFrame == 3 && mesh.animations.at(1).frameCount == 3,
			"walk follows idle");
		expect(nearly(float(mesh.animations.at(0).framesPerSecond), 10.0f), "the idle animation keeps its rate");
	}

	// Surfaces.
	if (!expect(mesh.surfaces.size() == 2, "one surface per mesh of model 0")) {
		return;
	}
	const ModelSurface& strip = mesh.surfaces.at(0);
	const ModelSurface& fan = mesh.surfaces.at(1);
	expect(strip.name == QStringLiteral("body/body_ref/0") && fan.name == QStringLiteral("body/body_ref/1"), "surfaces are named part/model/mesh");
	expect(strip.skinPaths == QStringList{QStringLiteral("skin.bmp")}, "the strip mesh names its texture");
	expect(fan.skinPaths == QStringList{QStringLiteral("skin.bmp")}, "the fan mesh names its texture");
	expect(strip.vertexCount == 4 && strip.triangles.size() == 2, "the strip yields 4 corners and 2 triangles");
	expect(fan.vertexCount == 5 && fan.triangles.size() == 3, "the fan and strip yield 5 corners (one vertex split by UV) and 3 triangles");
	for (const ModelTriangle& triangle : strip.triangles) {
		const ModelVec3 normal = faceNormal(strip, 0, triangle);
		expect(normal.z < -1.0f && nearly(normal.x, 0.0f) && nearly(normal.y, 0.0f), "both strip triangles face -Z (alternation and reversal)");
	}
	for (const ModelTriangle& triangle : fan.triangles) {
		const ModelVec3 normal = faceNormal(fan, 0, triangle);
		expect(normal.x > 1.0f, "fan triangles face +X in the bind pose");
	}
	const int v1 = cornerWithUv(strip, 1.0f, 0.0f);
	const int v3 = cornerWithUv(strip, 1.0f, 1.0f);
	expect(v1 >= 0 && v3 >= 0, "strip UVs are texels over the texture size");
	if (v1 >= 0) {
		expect(nearlyVec(strip.frames.at(0).positions.at(v1), 10, 0, 0), "strip vertex 1 sits at x = 10");
		expect(nearlyVec(strip.frames.at(0).normals.at(v1), 0, 0, -1), "strip normals come from the file");
		expect(nearlyVec(strip.frames.at(2).positions.at(v1), 10, 0, 2), "the root rises by 2 in idle frame 1");
	}
	expect(cornerWithUv(fan, 0.75f, 0.75f) >= 0 && cornerWithUv(fan, 0.25f, 0.25f) >= 0, "the reused fan vertex keeps both UVs");
	const int v6 = cornerWithUv(fan, 0.5f, 0.5f);
	if (expect(v6 >= 0, "fan vertex 6 has UV (0.5, 0.5)")) {
		expect(nearlyVec(fan.frames.at(0).positions.at(v6), 10, 5, 5), "bind pose: the child carries vertex 6 to (10, 5, 5)");
		expect(nearlyVec(fan.frames.at(1).positions.at(v6), 10, 5, 2 + 5), "idle frame 0: only the root has risen");
		expect(nearlyVec(fan.frames.at(2).positions.at(v6), 5, 0, 7), "idle frame 1: the child's quarter yaw moves vertex 6");
		expect(nearlyVec(fan.frames.at(2).normals.at(v6), 0, 1, 0), "idle frame 1: the child's normal turns to +Y");
		expect(nearlyVec(fan.frames.at(3).positions.at(v6), 10, 5, 5), "walk frame 0: the first run value leaves the child at rest");
		expect(nearlyVec(fan.frames.at(4).positions.at(v6), 10, -5, 5), "walk frame 1: the child's quarter roll moves vertex 6");
		expect(nearlyVec(fan.frames.at(5).positions.at(v6), 10, -5, 5), "walk frame 2: frames past a run's values repeat the last one");
	}
	if (expect(fan.skinning.influences.size() == 5, "one influence per corner")) {
		expect(fan.skinning.influences.at(0).joint == 1 && nearly(fan.skinning.influences.at(0).weight, 1.0f), "fan corners follow the child with weight 1");
	}

	// Attachment.
	const ModelTag* bindTag = tagAt(mesh, QStringLiteral("muzzle"), 0);
	const ModelTag* idleTag = tagAt(mesh, QStringLiteral("muzzle"), 2);
	if (expect(bindTag && idleTag, "the attachment becomes a tag in every frame")) {
		expect(nearlyVec(bindTag->origin, 11, 2, 3), "the tag sits at the child's origin plus org");
		expect(nearlyVec(idleTag->origin, 8, 1, 5), "the tag follows the rotated child");
	}

	// Embedded texture.
	if (expect(mesh.embeddedSkins.size() == 1, "one embedded texture")) {
		const ModelEmbeddedSkin& skin = mesh.embeddedSkins.at(0);
		expect(skin.name == QStringLiteral("skin.bmp"), "the embedded skin keeps the texture name");
		expect(skin.image.width() == 4 && skin.image.height() == 4, "the texture is 4 x 4");
		expect(skin.image.pixel(1, 0) == qRgba(10, 5, 254, 255), "texture pixels use the model's palette");
		expect(skin.image.pixel(3, 3) == qRgba(150, 75, 240, 255), "the last texture pixel uses palette entry 15");
		expect(skin.indexedFrames.size() == 1 && skin.indexedFrames.at(0).size() == 16 && skin.indexedFrames.at(0).at(5) == 5,
			"indexed pixels are kept");
	}
	expect(mesh.skinPaths.contains(QStringLiteral("skin.bmp")), "the model lists its texture");
	bool sawEye = false;
	for (const QString& line : mesh.detailLines) {
		sawEye = sawEye || line.contains(QStringLiteral("24.00"));
	}
	expect(sawEye, "the eye position is reported");

	// Without companions, the walk sequence is left out with a warning.
	const ModelMesh alone = decodeModelMesh(QStringLiteral("models/hl.mdl"), model);
	expect(alone.error.isEmpty() && alone.skeleton.clips.size() == 1, "without its group file only the idle clip decodes");
	expect(!alone.warnings.isEmpty(), "the missing group file is reported");

	// A damaged group file loses only its own sequences.
	QByteArray brokenGroup = group;
	brokenGroup[brokenGroup.size() - 5] = char(0); // the walk run's frame total
	const ModelMesh damagedGroup = decodeModelMesh(QStringLiteral("models/hl.mdl"), model, nullptr, {},
		fakeCompanions({{QStringLiteral("models/hl01.mdl"), brokenGroup}}));
	expect(damagedGroup.error.isEmpty() && damagedGroup.skeleton.clips.size() == 1 && !damagedGroup.warnings.isEmpty(),
		"a damaged sequence group drops its sequences with a warning");

	// The sequence group file opened directly shows its model.
	const ModelCompanionSource modelSource = fakeCompanions({{QStringLiteral("models/hl.mdl"), model}});
	const ModelMesh fromGroup = decodeModelMesh(QStringLiteral("models/hl01.mdl"), group, nullptr, {}, modelSource);
	expect(fromGroup.error.isEmpty() && fromGroup.geometryAvailable && fromGroup.skeleton.clips.size() == 2,
		"an IDSQ file decodes its model with both clips");
	const ModelMesh groupAlone = decodeModelMesh(QStringLiteral("models/hl01.mdl"), group);
	expect(!groupAlone.error.isEmpty(), "an IDSQ file without its model is an error");
}

void runHalfLifeTextureFile()
{
	const QByteArray model = buildStudio(true, false, false);
	const QByteArray textures = buildStudio(false, true, false);
	const ModelCompanionSource companions = fakeCompanions({{QStringLiteral("models/hlT.mdl"), textures}});
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/hl.mdl"), model, nullptr, {}, companions);
	if (!expect(mesh.error.isEmpty(), "the texture-less model decodes")) {
		std::cerr << qPrintable(mesh.error) << "\n";
		return;
	}
	expect(mesh.companionPaths.contains(QStringLiteral("models/hlT.mdl")), "the texture file is read as a companion");
	expect(mesh.embeddedSkins.size() == 1 && mesh.embeddedSkins.at(0).name == QStringLiteral("skin.bmp"), "textures come from the T file");
	if (expect(mesh.surfaces.size() == 2, "the texture-less model has its surfaces")) {
		expect(mesh.surfaces.at(0).skinPaths == QStringList{QStringLiteral("skin.bmp")}, "surfaces name the T file's texture");
		expect(cornerWithUv(mesh.surfaces.at(0), 1.0f, 1.0f) >= 0, "UVs use the T file's texture size");
	}

	const ModelMesh untextured = decodeModelMesh(QStringLiteral("models/hl.mdl"), model);
	expect(untextured.error.isEmpty() && untextured.geometryAvailable, "without its T file the model still decodes");
	expect(!untextured.warnings.isEmpty(), "the missing texture file is reported");
	if (!untextured.surfaces.isEmpty()) {
		expect(cornerWithUv(untextured.surfaces.at(0), 4.0f, 4.0f) >= 0, "without textures UVs stay in texels");
	}

	const ModelMesh textureOnly = decodeModelMesh(QStringLiteral("models/hlT.mdl"), textures);
	expect(textureOnly.error.isEmpty() && !textureOnly.geometryAvailable && textureOnly.embeddedSkins.size() == 1,
		"a T file on its own decodes to textures only");
}

void runHalfLifeMalformed()
{
	StudioLayout layout;
	const QByteArray model = buildStudio(true, true, true, &layout);
	const QString path = QStringLiteral("models/hl.mdl");
	const auto fails = [&](QByteArray bytes, const char* message) {
		const ModelMesh mesh = decodeModelMesh(path, bytes);
		expect(!mesh.error.isEmpty() && !mesh.geometryAvailable && mesh.surfaces.isEmpty(), message);
	};
	QByteArray bytes = model;
	patchI32(bytes, 4, 11);
	fails(bytes, "Half-Life: a bad version is an error");
	bytes = model;
	patchI32(bytes, 72, model.size() + 1);
	fails(bytes, "Half-Life: a length past the file is an error");
	bytes = model;
	patchI32(bytes, 140, 100000);
	fails(bytes, "Half-Life: a huge bone count is an error");
	bytes = model;
	patchI32(bytes, 144, model.size() - 8);
	fails(bytes, "Half-Life: a bone table past the file is an error");
	bytes = model;
	patchI32(bytes, 144, -50);
	fails(bytes, "Half-Life: a negative bone offset is an error");
	bytes = model;
	patchI32(bytes, layout.boneOffset + 112 + 32, 5);
	fails(bytes, "Half-Life: a parent after its child is an error");
	bytes = model;
	patchI32(bytes, layout.modelOffset + 72, 9999);
	fails(bytes, "Half-Life: a huge mesh count is an error");
	bytes = model;
	patchI32(bytes, layout.modelOffset + 80, 4);
	fails(bytes, "Half-Life: a triangle command naming a missing vertex is an error");
	bytes = model;
	patchI32(bytes, layout.meshOffset + 4, model.size() - 2);
	fails(bytes, "Half-Life: triangle commands running off the file are an error");
	bytes = model;
	patchI16(bytes, layout.stripCommandOffset, 30000);
	fails(bytes, "Half-Life: a triangle command longer than the file is an error");
	bytes = model;
	patchI32(bytes, layout.textureOffset + 68, 0);
	fails(bytes, "Half-Life: a zero texture width is an error");
	bytes = model;
	patchI32(bytes, layout.textureOffset + 76, model.size() - 10);
	fails(bytes, "Half-Life: texture pixels past the file are an error");
	bytes = model;
	patchI32(bytes, layout.sequenceOffset + 56, 3);
	fails(bytes, "Half-Life: animation runs that end before the frame count are an error");
	bytes = model;
	patchI32(bytes, layout.sequenceOffset + 56, 1000000);
	fails(bytes, "Half-Life: a huge frame count is an error");
	bytes = model;
	patchI32(bytes, layout.sequenceOffset + 124, model.size() - 4);
	fails(bytes, "Half-Life: an animation block past the file is an error");
	bytes = model;
	bytes[bytes.size() - 3] = char(0);
	fails(bytes, "Half-Life: a value run covering no frames is an error");
	bytes = model;
	patchI32(bytes, layout.bodyPartOffset + 72, model.size());
	fails(bytes, "Half-Life: a model table past the file is an error");
	fuzz("Half-Life", path, model, true);
	fuzz("Half-Life group", QStringLiteral("models/hl01.mdl"), buildStudioGroup(), false,
		fakeCompanions({{QStringLiteral("models/hl.mdl"), model}}));
}

// ---------------------------------------------------------------------------
// Hexen II RAPO fixture
// ---------------------------------------------------------------------------

void writeTrivert(Writer& w, int x, int y, int z, int normal)
{
	w.u8(x);
	w.u8(y);
	w.u8(z);
	w.u8(normal);
}

void writeHexenPose(Writer& w, const char* name, int z, int normal)
{
	writeTrivert(w, 0, 0, z, 0);
	writeTrivert(w, 10, 10, z, 0);
	w.fixed(name, 16);
	writeTrivert(w, 0, 0, z, normal);
	writeTrivert(w, 10, 0, z, normal);
	writeTrivert(w, 0, 10, z, normal);
	writeTrivert(w, 10, 10, z, normal);
}

// Four positions, five texture coordinates, two triangles sharing position 1
// through different texture coordinates (the second faces back, so its
// on-seam coordinate shifts by half the skin), a single and a group skin, a
// single frame and a group of two.
QByteArray buildHexen2(int upNormal)
{
	Writer w;
	w.fixed("RAPO", 4);
	w.i32(50);
	w.vec(1, 1, 1);
	w.vec(0, 0, 0);
	w.f32(16.0f);
	w.vec(0, 0, 20);
	w.i32(2);   // numskins
	w.i32(4);   // skinwidth
	w.i32(4);   // skinheight
	w.i32(4);   // numverts
	w.i32(2);   // numtris
	w.i32(2);   // numframes
	w.i32(0);   // synctype
	w.i32(1 << 14); // EF_HOLEY
	w.f32(1.0f);
	w.i32(5);   // num_st_verts
	// Skin 0: single.
	w.i32(0);
	for (int index = 0; index < 16; ++index) {
		w.u8(index);
	}
	// Skin 1: a group of two.
	w.i32(1);
	w.i32(2);
	w.f32(0.1f);
	w.f32(0.2f);
	for (int index = 0; index < 16; ++index) {
		w.u8(3);
	}
	for (int index = 0; index < 16; ++index) {
		w.u8(4);
	}
	// stvert_t: onseam, s, t.
	w.i32(0); w.i32(0); w.i32(0);
	w.i32(0); w.i32(3); w.i32(0);
	w.i32(0); w.i32(0); w.i32(3);
	w.i32(0x20); w.i32(3); w.i32(3);
	w.i32(0); w.i32(1); w.i32(1);
	// dnewtriangle_t: facesfront, vertindex[3], stindex[3].
	w.i32(1); w.i16(0); w.i16(1); w.i16(2); w.i16(0); w.i16(1); w.i16(2);
	w.i32(0); w.i16(1); w.i16(3); w.i16(2); w.i16(4); w.i16(3); w.i16(2);
	// Frame 0: single "stand1".
	w.i32(0);
	writeHexenPose(w, "stand1", 0, upNormal);
	// Frame 1: a group of "run1" and "run2".
	w.i32(1);
	w.i32(2);
	writeTrivert(w, 0, 0, 1, 0);
	writeTrivert(w, 10, 10, 2, 0);
	w.f32(0.1f);
	w.f32(0.2f);
	writeHexenPose(w, "run1", 1, upNormal);
	writeHexenPose(w, "run2", 2, upNormal);
	return w.data;
}

void runHexen2()
{
	const int up = modelAliasNormalIndex(ModelVec3{0.0f, 0.0f, 1.0f});
	const QByteArray bytes = buildHexen2(up);
	const QString path = QStringLiteral("models/h2.mdl");
	const ModelMesh mesh = decodeModelMesh(path, bytes);
	if (!expect(mesh.error.isEmpty(), "the Hexen II fixture decodes")) {
		std::cerr << qPrintable(mesh.error) << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::Hexen2Mdl && mesh.version == 50, "RAPO 50 is detected");
	expect(mesh.geometryAvailable && mesh.surfaces.size() == 1, "one Hexen II surface");
	expect(mesh.frames.size() == 3, "a single frame and a group of two make three frames");
	if (expect(mesh.animations.size() == 2, "two Hexen II animations")) {
		expect(mesh.animations.at(0).name == QStringLiteral("stand") && mesh.animations.at(0).frameCount == 1, "stand is one frame");
		expect(mesh.animations.at(1).name == QStringLiteral("run") && mesh.animations.at(1).firstFrame == 1 && mesh.animations.at(1).frameCount == 2,
			"run spans the frame group");
	}
	if (mesh.surfaces.isEmpty()) {
		return;
	}
	const ModelSurface& surface = mesh.surfaces.at(0);
	expect(surface.vertexCount == 5, "a position used with two texture coordinates splits into two corners");
	expect(surface.triangles.size() == 2, "two Hexen II triangles");
	const ModelVec3 normal = faceNormal(surface, 0, surface.triangles.at(0));
	expect(normal.z < -1.0f, "the first triangle is reversed from the file's clockwise order");
	const int st1 = cornerWithUv(surface, 3.5f / 4.0f, 0.5f / 4.0f);
	const int st4 = cornerWithUv(surface, 1.5f / 4.0f, 1.5f / 4.0f);
	const int seam = cornerWithUv(surface, 5.5f / 4.0f, 3.5f / 4.0f);
	expect(st1 >= 0 && st4 >= 0, "texel-centre UVs for both texture coordinates of position 1");
	expect(seam >= 0, "a back-facing on-seam coordinate shifts by half the skin width");
	if (st1 >= 0 && st4 >= 0) {
		expect(nearlyVec(surface.frames.at(0).positions.at(st1), 10, 0, 0) && nearlyVec(surface.frames.at(0).positions.at(st4), 10, 0, 0),
			"both corners of position 1 share its position");
		expect(nearlyVec(surface.frames.at(2).positions.at(st1), 10, 0, 2), "group member run2 lifts the model by 2");
		expect(nearlyVec(surface.frames.at(0).normals.at(st1), 0, 0, 1, 0.01f), "normals come from the shared table");
	}
	expect(mesh.frames.at(1).name == QStringLiteral("run1"), "frame names are kept");
	if (expect(mesh.embeddedSkins.size() == 2, "two Hexen II skins")) {
		const IdTechPalette palette = generatedIdTechPalette(QStringLiteral("quake"));
		const ModelEmbeddedSkin& single = mesh.embeddedSkins.at(0);
		const ModelEmbeddedSkin& group = mesh.embeddedSkins.at(1);
		expect(qAlpha(single.image.pixel(0, 0)) == 0, "EF_HOLEY cuts out index 0");
		const QRgb one = palette.colorAt(1);
		expect(single.image.pixel(1, 0) == qRgba(qRed(one), qGreen(one), qBlue(one), 255), "skin pixels use the palette");
		expect(group.groupFrameCount == 2 && group.indexedFrames.size() == 2 && group.indexedFrames.at(1).at(0) == 4, "group skin members are kept");
		expect(group.intervals.size() == 2 && nearly(group.intervals.at(1), 0.2f), "group skin intervals are kept");
	}

	const auto fails = [&](QByteArray damaged, const char* message) {
		const ModelMesh broken = decodeModelMesh(path, damaged);
		expect(!broken.error.isEmpty() && !broken.geometryAvailable, message);
	};
	QByteArray damaged = bytes;
	patchI32(damaged, 4, 6);
	fails(damaged, "Hexen II: a bad version is an error");
	damaged = bytes;
	patchI32(damaged, 84, 0);
	fails(damaged, "Hexen II: no texture coordinates is an error");
	damaged = bytes;
	patchI32(damaged, 60, 100000000);
	fails(damaged, "Hexen II: a huge vertex count is an error");
	damaged = bytes;
	patchI32(damaged, 68, -1);
	fails(damaged, "Hexen II: a negative frame count is an error");
	damaged = bytes;
	patchI32(damaged, 48, 1000);
	fails(damaged, "Hexen II: skins past the file are an error");
	fuzz("Hexen II", path, bytes, true);
}

// ---------------------------------------------------------------------------
// Heretic II FM fixture
// ---------------------------------------------------------------------------

void writeChunk(Writer& w, const char* ident, int version, const QByteArray& data)
{
	w.fixed(ident, 32);
	w.i32(version);
	w.i32(int(data.size()));
	w.data.append(data);
}

QByteArray buildFm(int upNormal, int meshVersion = 3, bool withSkins = true)
{
	Writer header;
	header.i32(8);  // skinwidth
	header.i32(8);  // skinheight
	header.i32(40 + 4 * 4); // framesize
	header.i32(2);  // num_skins
	header.i32(4);  // num_xyz
	header.i32(4);  // num_st
	header.i32(2);  // num_tris
	header.i32(1);  // num_glcmds
	header.i32(2);  // num_frames
	header.i32(2);  // num_mesh_nodes
	Writer skins;
	skins.fixed("models/test/a.m8", 64);
	skins.fixed("models/test/b.m8", 64);
	Writer st;
	st.i16(0); st.i16(0);
	st.i16(8); st.i16(0);
	st.i16(0); st.i16(8);
	st.i16(8); st.i16(8);
	Writer tris;
	tris.i16(0); tris.i16(1); tris.i16(2); tris.i16(0); tris.i16(1); tris.i16(2);
	tris.i16(1); tris.i16(3); tris.i16(2); tris.i16(1); tris.i16(3); tris.i16(2);
	Writer frames;
	for (int frame = 0; frame < 2; ++frame) {
		frames.vec(1, 1, 1);
		frames.vec(0, 0, float(frame * 4));
		frames.fixed(frame == 0 ? "walk1" : "walk2", 16);
		writeTrivert(frames, 0, 0, 0, upNormal);
		writeTrivert(frames, 8, 0, 0, upNormal);
		writeTrivert(frames, 0, 8, 0, upNormal);
		writeTrivert(frames, 8, 8, 0, upNormal);
	}
	Writer glcmds;
	glcmds.i32(0);
	Writer nodes;
	for (int node = 0; node < 2; ++node) {
		QByteArray mask(256, '\0');
		mask[0] = char(1 << node);
		nodes.data.append(mask);
		QByteArray verts(256, '\0');
		verts[0] = char(node == 0 ? 0x07 : 0x0E);
		nodes.data.append(verts);
		nodes.i16(0);
		nodes.i16(1);
	}
	Writer skeleton;
	skeleton.i32(0);
	skeleton.i32(0);

	Writer w;
	writeChunk(w, "header", 2, header.data);
	if (withSkins) {
		writeChunk(w, "skin", 1, skins.data);
	}
	writeChunk(w, "st coord", 1, st.data);
	writeChunk(w, "tris", 1, tris.data);
	writeChunk(w, "frames", 1, frames.data);
	writeChunk(w, "glcmds", 1, glcmds.data);
	writeChunk(w, "mesh nodes", meshVersion, nodes.data);
	writeChunk(w, "skeleton", 1, skeleton.data);
	return w.data;
}

void runHereticFm()
{
	const int up = modelAliasNormalIndex(ModelVec3{0.0f, 0.0f, 1.0f});
	const QByteArray bytes = buildFm(up);
	const QString path = QStringLiteral("models/test/tris.fm");
	const ModelMesh mesh = decodeModelMesh(path, bytes);
	if (!expect(mesh.error.isEmpty(), "the Heretic II fixture decodes")) {
		std::cerr << qPrintable(mesh.error) << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::HereticFm && mesh.version == 2, "the FM header chunk is detected");
	expect(mesh.skinPaths == QStringList({QStringLiteral("models/test/a.m8"), QStringLiteral("models/test/b.m8")}), "FM skin names are kept");
	expect(mesh.frames.size() == 2 && mesh.frames.at(1).name == QStringLiteral("walk2"), "two FM frames");
	if (expect(mesh.animations.size() == 1, "one FM animation")) {
		expect(mesh.animations.at(0).name == QStringLiteral("walk") && mesh.animations.at(0).frameCount == 2, "walk spans both frames");
	}
	if (!expect(mesh.surfaces.size() == 2, "one surface per mesh node")) {
		return;
	}
	const ModelSurface& node0 = mesh.surfaces.at(0);
	const ModelSurface& node1 = mesh.surfaces.at(1);
	expect(node0.name == QStringLiteral("node0") && node1.name == QStringLiteral("node1"), "surfaces are named after their nodes");
	expect(node0.triangles.size() == 1 && node1.triangles.size() == 1 && node0.vertexCount == 3 && node1.vertexCount == 3,
		"each node draws one triangle");
	expect(node0.skinPaths == mesh.skinPaths, "node surfaces name the skins");
	expect(faceNormal(node0, 0, node0.triangles.at(0)).z < -1.0f, "FM triangles are reversed from the file's clockwise order");
	const int corner = cornerWithUv(node0, 1.0f, 0.0f);
	if (expect(corner >= 0, "FM UVs are s / skinwidth")) {
		expect(nearlyVec(node0.frames.at(0).positions.at(corner), 8, 0, 0), "FM positions are scaled bytes");
		expect(nearlyVec(node0.frames.at(1).positions.at(corner), 8, 0, 4), "the second FM frame is translated");
		expect(nearlyVec(node0.frames.at(0).normals.at(corner), 0, 0, 1, 0.01f), "FM normals come from the shared table");
	}
	bool sawSkeleton = false;
	for (const QString& line : mesh.detailLines) {
		sawSkeleton = sawSkeleton || line.contains(QStringLiteral("skeleton"));
	}
	expect(sawSkeleton, "undecoded chunks are listed");

	const auto fails = [&](QByteArray damaged, const char* message) {
		const ModelMesh broken = decodeModelMesh(path, damaged);
		expect(!broken.error.isEmpty() && !broken.geometryAvailable, message);
	};
	QByteArray damaged = bytes;
	patchI32(damaged, 32, 3);
	fails(damaged, "FM: a bad header version is an error");
	damaged = bytes;
	patchI32(damaged, 36, 100000);
	fails(damaged, "FM: a chunk size past the file is an error");
	damaged = bytes;
	patchI32(damaged, 36, -4);
	fails(damaged, "FM: a negative chunk size is an error");
	damaged = bytes;
	patchI32(damaged, 40 + 16, 0);
	fails(damaged, "FM: no positions is an error");
	damaged = bytes;
	patchI32(damaged, 40 + 24, 100000000);
	fails(damaged, "FM: a huge triangle count is an error");
	damaged = bytes;
	patchI32(damaged, 40 + 8, 44);
	fails(damaged, "FM: a frame size below the vertex count is an error");
	damaged = bytes;
	patchI32(damaged, 40 + 32, 50);
	fails(damaged, "FM: more frames than the frame chunk holds is an error");
	fails(buildFm(up, 2), "FM: a bad mesh node version is an error");
	const ModelMesh skinless = decodeModelMesh(path, buildFm(up, 3, false));
	expect(skinless.error.isEmpty() && skinless.geometryAvailable && skinless.skinPaths.isEmpty() && !skinless.warnings.isEmpty(),
		"FM: a missing skin chunk is a warning, as in the game");
	fuzz("Heretic II", path, bytes, false);
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	runHalfLifeGeometry();
	runHalfLifeTextureFile();
	runHalfLifeMalformed();
	runHexen2();
	runHereticFm();
	if (g_failures > 0) {
		std::cerr << g_failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "model_format_studio_hexen2_fm_smoke_test passed\n";
	return 0;
}
