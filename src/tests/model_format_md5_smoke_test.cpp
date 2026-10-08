// MD5 (Doom 3, Quake 4, Prey, ETQW) decoding, writing and .def declarations.
// Every fixture is synthetic text written here; no game data is used.
#include "core/model_md5.h"
#include "core/model_mesh.h"
#include "core/model_skeleton.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>

using namespace vibestudio;

namespace {

int g_checks = 0;
int g_failures = 0;

bool expect(bool condition, const char* message)
{
	++g_checks;
	if (!condition) {
		++g_failures;
		std::cerr << "FAIL: " << message << "\n";
	}
	return condition;
}

bool nearly(float value, float expected, float epsilon = 1.0e-4f)
{
	return std::fabs(value - expected) <= epsilon;
}

bool nearVec(const ModelVec3& value, const ModelVec3& expected, float epsilon = 1.0e-4f)
{
	return nearly(value.x, expected.x, epsilon) && nearly(value.y, expected.y, epsilon) && nearly(value.z, expected.z, epsilon);
}

bool containsText(const QStringList& lines, const QString& text)
{
	for (const QString& line : lines) {
		if (line.contains(text, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

void printLines(const char* label, const QStringList& lines)
{
	for (const QString& line : lines) {
		std::cerr << "  " << label << ": " << line.toStdString() << "\n";
	}
}

ModelVec3 crossOf(const ModelVec3& a, const ModelVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

ModelVec3 minus(const ModelVec3& a, const ModelVec3& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

QByteArray replaced(const QByteArray& source, const char* from, const char* to)
{
	QByteArray copy = source;
	const qsizetype at = copy.indexOf(from);
	if (at < 0) {
		std::cerr << "FIXTURE: \"" << from << "\" not found\n";
		++g_failures;
		return copy;
	}
	copy.replace(at, qsizetype(std::strlen(from)), to);
	return copy;
}

// A three-joint chain: origin at the model origin, hip 10 units up, knee 20
// units up and turned +90 degrees about Z. The stored quaternion components
// are Doom 3's: (0, 0, -sin 45) rebuilds w = +cos 45, and Doom 3 applies the
// transpose of idQuat::ToMat3, which turns +X to +Y.
const char kChainMesh[] = R"MD5(MD5Version 10
commandline "test fixture"
// A three-joint chain.
numJoints 3
numMeshes 2

joints {
	"origin"	-1 ( 0 0 0 ) ( 0 0 0 )		//
	"hip"	0 ( 0 0 10 ) ( 0 0 0 )		// origin
	"knee"	1 ( 0 0 20 ) ( 0 0 -0.7071067812 )		// hip
}

/* The body: vertex 2 shares hip and knee. */
mesh {
	shader "models/test/body"

	numverts 4
	vert 0 ( 0 0 ) 0 1
	vert 1 ( 1 0 ) 1 1
	vert 2 ( 0 1 ) 2 2
	vert 3 ( 1 1 ) 4 1

	numtris 2
	tri 0 0 1 2
	tri 1 1 3 2

	numweights 5
	weight 0 1 1 ( 1 0 0 )
	weight 1 1 1 ( 0 1 0 )
	weight 2 1 0.25 ( 2 0 0 )
	weight 3 2 0.75 ( 1 0 0 )
	weight 4 2 1 ( 0 0 2 )
}

mesh {
	// A plate under the model; Doom 3's face plane cross(c - a, b - a) of
	// these clockwise triangles points down (-Z).
	shader "models/test/plate"

	numverts 4
	vert 0 ( 0 0 ) 0 1
	vert 1 ( 1 0 ) 1 1
	vert 2 ( 1 1 ) 2 1
	vert 3 ( 0 1 ) 3 1

	numtris 2
	tri 0 0 1 2
	tri 1 0 2 3

	numweights 4
	weight 0 0 1 ( -1 -1 0 )
	weight 1 0 1 ( 1 -1 0 )
	weight 2 0 1 ( 1 1 0 )
	weight 3 0 1 ( -1 1 0 )
}
)MD5";

// Frame 0 is the bind pose; frame 1 turns the hip +90 degrees about X (stored
// as -sin 45 on X). The knee animates its X translation (kept at 0).
const char kWalkAnim[] = R"MD5(MD5Version 10
commandline "walk fixture"

numFrames 2
numJoints 3
frameRate 30
numAnimatedComponents 4

hierarchy {
	"origin"	-1 0 0	//
	"hip"	0 56 0	// origin ( Qx Qy Qz )
	"knee"	1 1 3	// hip ( Tx )
}

bounds {
	( -1 -1 0 ) ( 1 1 22 )
	( -1 -12 0 ) ( 1 1 11 )
}

baseframe {
	( 0 0 0 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 -0.7071067812 )
}

frame 0 {
	 0 0 0 0
}

frame 1 {
	 -0.7071067812 0 0 0
}
)MD5";

const char kIdleAnim[] = R"MD5(MD5Version 10
commandline ""

numFrames 1
numJoints 3
frameRate 24
numAnimatedComponents 0

hierarchy {
	"origin"	-1 0 0
	"hip"	0 0 0
	"knee"	1 0 0
}

bounds {
	( -1 -1 0 ) ( 1 1 22 )
}

baseframe {
	( 0 0 0 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 -0.7071067812 )
}

frame 0 {
}
)MD5";

// Valid on its own, but its third joint is not the mesh's.
const char kBrokenAnim[] = R"MD5(MD5Version 10
commandline ""

numFrames 1
numJoints 3
frameRate 24
numAnimatedComponents 0

hierarchy {
	"origin"	-1 0 0
	"hip"	0 0 0
	"shin"	1 0 0
}

bounds {
	( -1 -1 0 ) ( 1 1 22 )
}

baseframe {
	( 0 0 0 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 0 )
	( 0 0 10 ) ( 0 0 0 )
}

frame 0 {
}
)MD5";

// Line numbers matter: "model test_chain" is on line 8, the variant on 21 and
// "model unrelated" on 26.
const char kDefText[] = R"DEF(// Declarations for the test chain.
entityDef monster_test {
	"inherit"		"monster_default"
	"model"			"test_chain"
	"editor_usage"	"braces { inside } strings"
	"nested" { "deeper" { } }
}
model test_chain {
	mesh		models/md5/test/chain.md5mesh
	channel torso ( *hip knee )
	offset ( 0 0 4 )
	skin		skins/test/chain.skin
	anim run	models/md5/test/walk.md5anim {
		frame 1 sound_body snd_footstep
	}
	anim idle	models/md5/other/idle.md5anim, models/md5/other/idle_b.md5anim
}

/* A variant that inherits the mesh
   and names an anim already read. */
model test_chain_variant {
	inherit		test_chain
	anim jump	models/md5/other/idle.md5anim
}

model unrelated {
	mesh models/md5/elsewhere/other.md5mesh
	anim walk models/md5/test/walk.md5anim
}
)DEF";

const QString kMeshPath = QStringLiteral("models/md5/test/chain.md5mesh");

struct FakeFiles {
	QStringList paths;
	QHash<QString, QByteArray> bytes;
	void add(const QString& path, const QByteArray& data)
	{
		paths.append(path);
		bytes.insert(path.toLower(), data);
	}
};

ModelCompanionSource fakeSource(const std::shared_ptr<FakeFiles>& files, int maxFiles = 128)
{
	ModelCompanionSource source;
	source.read = [files](const QString& path, QByteArray* bytes, QString* error) {
		auto found = files->bytes.constFind(path.toLower());
		if (found == files->bytes.constEnd() && !path.contains(QLatin1Char(':'))) {
			// A game-relative path resolves against a folder above, as the
			// file-system source tries each one.
			for (auto entry = files->bytes.constBegin(); entry != files->bytes.constEnd(); ++entry) {
				if (entry.key().endsWith(QLatin1Char('/') + path.toLower())) {
					found = entry;
					break;
				}
			}
		}
		if (found == files->bytes.constEnd()) {
			if (error) {
				*error = QStringLiteral("missing %1").arg(path);
			}
			return false;
		}
		*bytes = found.value();
		return true;
	};
	source.list = [files](const QString& directory, const QStringList& suffixes) {
		QString folder = directory;
		while (folder.endsWith(QLatin1Char('/'))) {
			folder.chop(1);
		}
		QStringList out;
		for (const QString& path : files->paths) {
			const int slash = int(path.lastIndexOf(QLatin1Char('/')));
			const QString parent = slash < 0 ? QString() : path.left(slash);
			const QString suffix = path.mid(path.lastIndexOf(QLatin1Char('.')) + 1).toLower();
			if (parent.compare(folder, Qt::CaseInsensitive) == 0 && suffixes.contains(suffix)) {
				out.append(path);
			}
		}
		std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
		return out;
	};
	source.maxFiles = maxFiles;
	return source;
}

std::shared_ptr<FakeFiles> standardFiles()
{
	auto files = std::make_shared<FakeFiles>();
	files->add(QStringLiteral("models/md5/test/walk.md5anim"), QByteArray(kWalkAnim));
	files->add(QStringLiteral("models/md5/test/broken.md5anim"), QByteArray(kBrokenAnim));
	files->add(QStringLiteral("models/md5/other/idle.md5anim"), QByteArray(kIdleAnim));
	files->add(QStringLiteral("def/test.def"), QByteArray(kDefText));
	return files;
}

int clipIndexNamed(const ModelMesh& mesh, const QString& name)
{
	for (int index = 0; index < mesh.skeleton.clips.size(); ++index) {
		if (mesh.skeleton.clips.at(index).name == name) {
			return index;
		}
	}
	return -1;
}

const ModelAnimation* animationNamed(const ModelMesh& mesh, const QString& name)
{
	for (const ModelAnimation& animation : mesh.animations) {
		if (animation.name == name) {
			return &animation;
		}
	}
	return nullptr;
}

// Rx(+90): (x, y, z) -> (x, -z, y).
ModelVec3 rotateX90(const ModelVec3& v)
{
	return {v.x, -v.z, v.y};
}

void testMeshDecode()
{
	const ModelCompanionSource source = fakeSource(standardFiles());
	const ModelMesh mesh = decodeModelMesh(kMeshPath, QByteArray(kChainMesh), nullptr, {}, source);
	if (!expect(mesh.error.isEmpty(), "chain md5mesh decodes")) {
		std::cerr << "  error: " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::Md5Mesh, "md5mesh format detected");
	expect(mesh.version == 10, "md5mesh version 10");
	expect(mesh.geometryAvailable, "md5mesh has geometry");
	expect(mesh.skeleton.sourceFormat == QStringLiteral("md5"), "md5 skeleton source format");

	// Joints.
	const ModelSkeleton& skeleton = mesh.skeleton;
	if (expect(skeleton.joints.size() == 3, "three joints")) {
		expect(skeleton.joints.at(0).name == QStringLiteral("origin") && skeleton.joints.at(1).name == QStringLiteral("hip")
				&& skeleton.joints.at(2).name == QStringLiteral("knee"),
			"joint names");
		expect(skeleton.joints.at(0).parent == -1 && skeleton.joints.at(1).parent == 0 && skeleton.joints.at(2).parent == 1, "joint parents");
		expect(nearVec(modelJointTranslation(skeleton.joints.at(1).bind), {0, 0, 10}), "hip bind position");
		expect(nearVec(modelJointTranslation(skeleton.joints.at(2).bind), {0, 0, 20}), "knee bind position");
		expect(nearVec(modelJointTransformVector(skeleton.joints.at(2).bind, {1, 0, 0}), {0, 1, 0}),
			"knee bind turns +X to +Y as Doom 3's transposed idQuat::ToMat3 does");
	}

	if (!expect(mesh.surfaces.size() == 2, "two surfaces")) {
		return;
	}
	const ModelSurface& body = mesh.surfaces.at(0);
	const ModelSurface& plate = mesh.surfaces.at(1);
	expect(body.name == QStringLiteral("body") && plate.name == QStringLiteral("plate"), "surface names from the shader's last component");
	expect(body.skinPaths == QStringList{QStringLiteral("models/test/body")}, "body shader is its skin path");
	expect(mesh.skinPaths.size() == 2, "mesh skin paths");
	expect(body.vertexCount == 4 && body.triangles.size() == 2, "body counts");
	expect(nearly(body.texCoords.at(1).u, 1) && nearly(body.texCoords.at(1).v, 0) && nearly(plate.texCoords.at(2).u, 1)
			&& nearly(plate.texCoords.at(2).v, 1),
		"texture coordinates kept as written");

	// Bind-pose positions are the weighted sums of the weights.
	const ModelFrameGeometry& bind = body.frames.at(0);
	expect(nearVec(bind.positions.at(0), {1, 0, 10}), "vertex 0 on the hip");
	expect(nearVec(bind.positions.at(1), {0, 1, 10}), "vertex 1 on the hip");
	expect(nearVec(bind.positions.at(2), {0.5f, 0.75f, 17.5f}), "vertex 2 shares hip and knee");
	expect(nearVec(bind.positions.at(3), {0, 0, 22}), "vertex 3 on the knee");
	if (expect(body.skinning.count.size() == 4 && body.skinning.count.at(2) == 2, "vertex 2 has two influences")) {
		const ModelJointInfluence& hip = body.skinning.influences.at(body.skinning.first.at(2));
		const ModelJointInfluence& knee = body.skinning.influences.at(body.skinning.first.at(2) + 1);
		expect(hip.joint == 1 && nearly(hip.weight, 0.25f) && nearVec(hip.offset, {2, 0, 0}), "first influence of vertex 2");
		expect(knee.joint == 2 && nearly(knee.weight, 0.75f) && nearVec(knee.offset, {1, 0, 0}), "second influence of vertex 2");
	}

	// Winding: Doom 3 shows the plate's face toward -Z; decoded triangles are
	// counter-clockwise, so cross(b - a, c - a) must point there too.
	const ModelFrameGeometry& plateBind = plate.frames.at(0);
	const ModelTriangle& face = plate.triangles.at(0);
	expect(face.a == 0 && face.b == 2 && face.c == 1, "file winding swapped to counter-clockwise");
	const ModelVec3 normal = crossOf(minus(plateBind.positions.at(face.b), plateBind.positions.at(face.a)),
		minus(plateBind.positions.at(face.c), plateBind.positions.at(face.a)));
	expect(normal.z < 0 && nearly(normal.x, 0) && nearly(normal.y, 0), "the plate's front faces down");
	bool plateNormals = true;
	for (const ModelVec3& n : plateBind.normals) {
		plateNormals = plateNormals && nearVec(n, {0, 0, -1});
	}
	expect(plateNormals, "plate normals face down");

	// Clips: walk (renamed run by the .def), broken left out, idle read from
	// the .def.
	if (expect(skeleton.clips.size() == 2, "two clips")) {
		const int run = clipIndexNamed(mesh, QStringLiteral("run"));
		const int idle = clipIndexNamed(mesh, QStringLiteral("idle"));
		if (expect(run == 0 && idle == 1, "clips renamed and added by the .def")) {
			const ModelSkeletalClip& runClip = skeleton.clips.at(run);
			expect(runClip.sourcePath == QStringLiteral("models/md5/test/walk.md5anim"), "run clip source");
			expect(runClip.frames.size() == 2 && nearly(float(runClip.framesPerSecond), 30), "run clip frames and rate");
			expect(runClip.frameMins.size() == 2 && nearVec(runClip.frameMins.at(1), {-1, -12, 0}) && nearVec(runClip.frameMaxs.at(1), {1, 1, 11}),
				"run clip bounds");
			const ModelSkeletalClip& idleClip = skeleton.clips.at(idle);
			expect(idleClip.sourcePath == QStringLiteral("models/md5/other/idle.md5anim"), "idle clip source");
			expect(idleClip.frames.size() == 1 && nearly(float(idleClip.framesPerSecond), 24), "idle clip frames and rate");
		}
	}
	expect(containsText(mesh.warnings, QStringLiteral("broken.md5anim")), "mismatched hierarchy warned");
	expect(mesh.companionPaths.contains(QStringLiteral("def/test.def")), "the .def was read");
	expect(mesh.companionPaths.contains(QStringLiteral("models/md5/other/idle.md5anim")), "the .def's anim was read");
	expect(containsText(mesh.detailLines, QStringLiteral("test fixture")), "command line detail");
	expect(containsText(mesh.detailLines, QStringLiteral("\"test_chain\"")), "declaration detail");
	expect(containsText(mesh.detailLines, QStringLiteral("\"test_chain_variant\"")), "inherited declaration detail");
	expect(!containsText(mesh.detailLines, QStringLiteral("\"unrelated\"")), "unrelated declaration ignored");
	expect(containsText(mesh.detailLines, QStringLiteral("Animations read: 2")), "animation count detail");

	// Baked frames: bind pose, run 0, run 1, idle 0.
	expect(mesh.frames.size() == 4 && mesh.frameCount == 4, "four baked frames");
	if (mesh.frames.size() == 4) {
		expect(mesh.frames.at(0).name == QStringLiteral("bindpose"), "bind pose frame");
	}
	const ModelAnimation* runAnimation = animationNamed(mesh, QStringLiteral("run"));
	const ModelAnimation* idleAnimation = animationNamed(mesh, QStringLiteral("idle"));
	expect(runAnimation && runAnimation->firstFrame == 1 && runAnimation->frameCount == 2 && nearly(float(runAnimation->framesPerSecond), 30),
		"run animation range");
	expect(idleAnimation && idleAnimation->firstFrame == 3 && idleAnimation->frameCount == 1 && nearly(float(idleAnimation->framesPerSecond), 24),
		"idle animation range");

	if (body.frames.size() == 4) {
		// Run frame 1: the hip turns +90 degrees about X.
		const ModelFrameGeometry& turned = body.frames.at(2);
		expect(nearVec(turned.positions.at(0), {1, 0, 10}), "baked vertex 0");
		expect(nearVec(turned.positions.at(1), {0, 0, 11}), "baked vertex 1 follows the hip");
		expect(nearVec(turned.positions.at(2), {0.5f, -7.5f, 10.75f}), "baked vertex 2 blends both joints");
		expect(nearVec(turned.positions.at(3), {0, -12, 10}), "baked vertex 3 follows the knee");
		const ModelVec3 before = bind.normals.at(3);
		expect(std::fabs(before.x) + std::fabs(before.y) + std::fabs(before.z) > 0.5f, "vertex 3 has a bind normal");
		expect(nearVec(turned.normals.at(3), rotateX90(before), 1.0e-3f), "baked normal follows the rotated joint");
		expect(nearVec(plate.frames.at(2).normals.at(0), {0, 0, -1}), "plate normal stays with the still origin");
	}
}

void testBudgetAndSources()
{
	{
		const ModelCompanionSource source = fakeSource(standardFiles(), 1);
		const ModelMesh mesh = decodeModelMesh(kMeshPath, QByteArray(kChainMesh), nullptr, {}, source);
		expect(mesh.error.isEmpty(), "budget-limited decode still succeeds");
		expect(mesh.skeleton.clips.isEmpty(), "budget leaves the walk out");
		expect(containsText(mesh.warnings, QStringLiteral("companion file")), "budget exhaustion warned");
		expect(mesh.frames.size() == 1, "only the bind pose is baked");
		expect(!mesh.companionPaths.contains(QStringLiteral("def/test.def")), "no .def read past the budget");
	}
	{
		const ModelMesh mesh = decodeModelMesh(kMeshPath, QByteArray(kChainMesh));
		expect(mesh.error.isEmpty() && mesh.skeleton.clips.isEmpty() && mesh.frames.size() == 1, "no companions: bind pose only");
	}
	{
		// Without listing nothing beside the mesh is found.
		ModelCompanionSource source = fakeSource(standardFiles());
		source.list = nullptr;
		const ModelMesh mesh = decodeModelMesh(kMeshPath, QByteArray(kChainMesh), nullptr, {}, source);
		expect(mesh.error.isEmpty() && mesh.skeleton.clips.isEmpty(), "read-only companions add no clips");
	}
	{
		// An optional mesh name wins over the shader.
		const QByteArray named = replaced(QByteArray(kChainMesh), "shader \"models/test/plate\"", "name \"floor\"\n\tshader \"models/test/plate\"");
		const ModelMesh mesh = decodeModelMesh(kMeshPath, named);
		expect(mesh.error.isEmpty() && mesh.surfaces.size() == 2 && mesh.surfaces.at(1).name == QStringLiteral("floor"), "mesh name keyword");
	}
	{
		// A triangle naming a missing vertex is dropped with a warning.
		const QByteArray bad = replaced(QByteArray(kChainMesh), "tri 1 1 3 2", "tri 1 1 3 9");
		const ModelMesh mesh = decodeModelMesh(kMeshPath, bad);
		expect(mesh.error.isEmpty() && mesh.surfaces.size() == 2 && mesh.surfaces.at(0).triangles.size() == 1
				&& !mesh.surfaces.at(0).warnings.isEmpty(),
			"out-of-range triangle dropped");
	}
	{
		ModelWorkControl control;
		control.cancelled = [] { return true; };
		const ModelMesh mesh = decodeModelMesh(kMeshPath, QByteArray(kChainMesh), nullptr, control);
		expect(!mesh.error.isEmpty() && !mesh.geometryAvailable, "cancelled decode reports an error");
	}
}

// Loose files: absolute paths, the .def found through the game folder above
// "models/", and a declaration that spells the mesh path differently.
void testFileSystemLayout()
{
	auto files = std::make_shared<FakeFiles>();
	files->add(QStringLiteral("C:/game/base/models/md5/test/walk.md5anim"), QByteArray(kWalkAnim));
	files->add(QStringLiteral("C:/game/base/models/md5/other/idle.md5anim"), QByteArray(kIdleAnim));
	files->add(QStringLiteral("C:/game/base/def/test.def"),
		replaced(QByteArray(kDefText), "mesh\t\tmodels/md5/test/chain.md5mesh", "mesh\t\tModels\\MD5\\test\\Chain.md5mesh"));
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("C:/game/base/models/md5/test/chain.md5mesh"), QByteArray(kChainMesh), nullptr, {},
		fakeSource(files));
	if (!expect(mesh.error.isEmpty(), "loose-file md5mesh decodes")) {
		std::cerr << "  error: " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.skeleton.clips.size() == 2 && clipIndexNamed(mesh, QStringLiteral("run")) == 0 && clipIndexNamed(mesh, QStringLiteral("idle")) == 1,
		"loose-file clips named by the game folder's .def");
	expect(containsText(mesh.detailLines, QStringLiteral("\"test_chain\" in C:/game/base/def/test.def")), "loose-file declaration detail");
	expect(mesh.companionPaths.contains(QStringLiteral("models/md5/other/idle.md5anim")), "the .def's anim read by its game path");
}

void testAnimDecode()
{
	const ModelMesh mesh = decodeModelMesh(QStringLiteral("models/md5/test/walk.md5anim"), QByteArray(kWalkAnim));
	if (!expect(mesh.error.isEmpty(), "walk md5anim decodes")) {
		std::cerr << "  error: " << mesh.error.toStdString() << "\n";
		return;
	}
	expect(mesh.format == ModelMeshFormat::Md5Anim, "md5anim format detected");
	expect(!mesh.geometryAvailable && mesh.surfaces.isEmpty(), "md5anim has no geometry");
	const ModelSkeleton& skeleton = mesh.skeleton;
	if (expect(skeleton.joints.size() == 3, "md5anim joints")) {
		expect(skeleton.joints.at(1).flags == 56 && skeleton.joints.at(2).flags == 1, "anim flags kept on joints");
		expect(skeleton.joints.at(2).parent == 1, "md5anim parents");
		expect(nearVec(modelJointTranslation(skeleton.joints.at(2).bind), {0, 0, 20}), "baseframe concatenated to model space");
	}
	if (expect(skeleton.clips.size() == 1, "one md5anim clip")) {
		const ModelSkeletalClip& clip = skeleton.clips.at(0);
		expect(clip.name == QStringLiteral("walk") && clip.sourcePath.isEmpty(), "clip named after the file");
		expect(nearly(float(clip.framesPerSecond), 30) && clip.frames.size() == 2, "clip rate and frames");
		if (clip.frames.size() == 2 && clip.frames.at(1).size() == 3) {
			const ModelJointMatrix& knee = clip.frames.at(1).at(2);
			expect(nearVec(modelJointTranslation(knee), {0, -10, 10}), "knee follows the turned hip");
			expect(nearVec(modelJointTransformVector(knee, {1, 0, 0}), {0, 0, 1}), "knee rotation concatenates parent first");
		}
	}
	expect(mesh.frames.size() == 2, "no bind pose frame for an animation");
	if (mesh.frames.size() == 2) {
		expect(nearVec(mesh.frames.at(1).mins, {-1, -12, 0}) && nearVec(mesh.frames.at(1).maxs, {1, 1, 11}), "frame bounds from the file");
	}
	expect(mesh.animations.size() == 1 && nearly(float(mesh.animations.at(0).framesPerSecond), 30), "baked animation");
}

bool sameClipFrames(const ModelSkeletalClip& a, const ModelSkeletalClip& b, double tolerance)
{
	if (a.frames.size() != b.frames.size()) {
		return false;
	}
	for (int frame = 0; frame < a.frames.size(); ++frame) {
		if (a.frames.at(frame).size() != b.frames.at(frame).size()) {
			return false;
		}
		for (int joint = 0; joint < a.frames.at(frame).size(); ++joint) {
			if (modelJointMatrixDistance(a.frames.at(frame).at(joint), b.frames.at(frame).at(joint)) > tolerance) {
				return false;
			}
		}
	}
	return true;
}

void testRoundTrips()
{
	const ModelCompanionSource source = fakeSource(standardFiles());
	const ModelMesh mesh = decodeModelMesh(kMeshPath, QByteArray(kChainMesh), nullptr, {}, source);
	if (!expect(mesh.error.isEmpty() && mesh.skeleton.clips.size() == 2, "round-trip source decodes")) {
		return;
	}

	// md5mesh.
	ModelMd5ExportOptions options;
	options.commandLine = QStringLiteral("vibestudio \"test\" export");
	QString error;
	const QByteArray meshText = exportModelMd5Mesh(mesh, options, &error);
	if (expect(!meshText.isEmpty() && error.isEmpty(), "md5mesh exports")) {
		const ModelMesh back = decodeModelMesh(QStringLiteral("rt/chain.md5mesh"), meshText);
		if (expect(back.error.isEmpty(), "exported md5mesh decodes")) {
			expect(back.skeleton.joints.size() == 3, "round-trip joint count");
			bool joints = back.skeleton.joints.size() == mesh.skeleton.joints.size();
			for (int index = 0; joints && index < back.skeleton.joints.size(); ++index) {
				const ModelJoint& a = mesh.skeleton.joints.at(index);
				const ModelJoint& b = back.skeleton.joints.at(index);
				joints = a.name == b.name && a.parent == b.parent && modelJointMatrixDistance(a.bind, b.bind) < 1.0e-5;
			}
			expect(joints, "round-trip joints");
			bool surfaces = back.surfaces.size() == mesh.surfaces.size();
			for (int index = 0; surfaces && index < back.surfaces.size(); ++index) {
				const ModelSurface& a = mesh.surfaces.at(index);
				const ModelSurface& b = back.surfaces.at(index);
				surfaces = a.vertexCount == b.vertexCount && a.triangles.size() == b.triangles.size() && a.skinPaths == b.skinPaths
					&& a.skinning.influences.size() == b.skinning.influences.size();
				for (int t = 0; surfaces && t < a.triangles.size(); ++t) {
					surfaces = a.triangles.at(t).a == b.triangles.at(t).a && a.triangles.at(t).b == b.triangles.at(t).b
						&& a.triangles.at(t).c == b.triangles.at(t).c;
				}
				for (int v = 0; surfaces && v < a.vertexCount; ++v) {
					surfaces = nearly(a.texCoords.at(v).u, b.texCoords.at(v).u, 1.0e-6f) && nearly(a.texCoords.at(v).v, b.texCoords.at(v).v, 1.0e-6f)
						&& nearVec(a.frames.at(0).positions.at(v), b.frames.at(0).positions.at(v), 1.0e-5f)
						&& a.skinning.count.at(v) == b.skinning.count.at(v);
				}
				for (int i = 0; surfaces && i < a.skinning.influences.size(); ++i) {
					const ModelJointInfluence& x = a.skinning.influences.at(i);
					const ModelJointInfluence& y = b.skinning.influences.at(i);
					surfaces = x.joint == y.joint && nearly(x.weight, y.weight, 1.0e-6f) && nearVec(x.offset, y.offset, 1.0e-6f);
				}
			}
			expect(surfaces, "round-trip surfaces: positions, UVs, triangles and weights");
			expect(containsText(back.detailLines, QStringLiteral("vibestudio 'test' export")), "command line written with safe quotes");
		}
	}

	// md5anim, every frame in model space.
	error.clear();
	const QByteArray animText = exportModelMd5Anim(mesh, 0, options, &error);
	if (expect(!animText.isEmpty() && error.isEmpty(), "md5anim exports")) {
		const ModelMesh back = decodeModelMesh(QStringLiteral("rt/run.md5anim"), animText);
		if (expect(back.error.isEmpty() && back.skeleton.clips.size() == 1, "exported md5anim decodes")) {
			const ModelSkeletalClip& clip = back.skeleton.clips.at(0);
			expect(sameClipFrames(clip, mesh.skeleton.clips.at(0), 1.0e-4), "round-trip clip frames within 1e-4");
			expect(nearly(float(clip.framesPerSecond), 30), "round-trip frame rate");
			expect(clip.frameMins.size() == 2 && nearVec(clip.frameMins.at(1), {-1, -12, 0}), "round-trip bounds from the clip");
			expect(back.skeleton.joints.at(2).flags == 63, "every component animated");
		}
	}

	// Bounds computed by skinning when the clip has none.
	ModelMesh unbounded = mesh;
	unbounded.skeleton.clips[0].frameMins.clear();
	unbounded.skeleton.clips[0].frameMaxs.clear();
	unbounded.skeleton.clips[0].framesPerSecond = 0.0;
	options.defaultFrameRate = 15;
	const QByteArray computedText = exportModelMd5Anim(unbounded, 0, options, &error);
	{
		const ModelMesh back = decodeModelMesh(QStringLiteral("rt/computed.md5anim"), computedText);
		if (expect(back.error.isEmpty() && back.skeleton.clips.size() == 1, "computed-bounds md5anim decodes")) {
			const ModelSkeletalClip& clip = back.skeleton.clips.at(0);
			expect(clip.frameMins.size() == 2 && nearly(clip.frameMins.at(1).y, -12) && nearly(clip.frameMaxs.at(0).z, 22)
					&& nearly(clip.frameMins.at(0).z, 0),
				"bounds from skinned geometry");
			expect(nearly(float(clip.framesPerSecond), 15), "default frame rate used");
		}
	}

	// A standalone animation round-trips too.
	{
		const ModelMesh walk = decodeModelMesh(QStringLiteral("models/md5/test/walk.md5anim"), QByteArray(kWalkAnim));
		const QByteArray text = exportModelMd5Anim(walk, 0, {}, &error);
		const ModelMesh back = decodeModelMesh(QStringLiteral("rt/walk.md5anim"), text);
		expect(back.error.isEmpty() && back.skeleton.clips.size() == 1 && sameClipFrames(back.skeleton.clips.at(0), walk.skeleton.clips.at(0), 1.0e-4),
			"standalone md5anim round trip");
	}

	// A skeletal mesh with one unskinned surface binds it to the first joint.
	{
		ModelMesh mixed = mesh;
		mixed.surfaces[1].skinning = {};
		const QByteArray text = exportModelMd5Mesh(mixed, {}, &error);
		const ModelMesh back = decodeModelMesh(QStringLiteral("rt/mixed.md5mesh"), text);
		bool same = back.error.isEmpty() && back.surfaces.size() == 2;
		for (int v = 0; same && v < 4; ++v) {
			same = nearVec(back.surfaces.at(1).frames.at(0).positions.at(v), mesh.surfaces.at(1).frames.at(0).positions.at(v), 1.0e-5f);
		}
		expect(same, "unskinned surface rides on joint 0");
	}

	// A static mesh gets one origin joint.
	{
		ModelMesh slab;
		slab.geometryAvailable = true;
		ModelSurface surface;
		surface.index = 0;
		surface.name = QStringLiteral("slab");
		surface.vertexCount = 4;
		surface.texCoords = {{0.0f, 0.0f}, {0.5f, 0.0f}, {0.5f, 0.25f}, {0.0f, 0.25f}};
		surface.triangles = {{0, 1, 2}, {0, 2, 3}};
		ModelFrameGeometry geometry;
		geometry.positions = {{-4.0f, -2.0f, 1.0f}, {4.0f, -2.0f, 1.0f}, {4.0f, 2.0f, 1.5f}, {-4.125f, 2.0f, 1.25f}};
		geometry.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
		surface.frames = {geometry};
		surface.skinPaths = {QStringLiteral("textures/test/slab")};
		slab.surfaces = {surface};
		const QByteArray text = exportModelMd5Mesh(slab, {}, &error);
		const ModelMesh back = decodeModelMesh(QStringLiteral("rt/slab.md5mesh"), text);
		if (expect(back.error.isEmpty() && back.surfaces.size() == 1, "static md5mesh decodes")) {
			expect(back.skeleton.joints.size() == 1 && back.skeleton.joints.at(0).name == QStringLiteral("origin")
					&& back.skeleton.joints.at(0).parent == -1,
				"static mesh has one origin joint");
			const ModelSurface& b = back.surfaces.at(0);
			bool same = b.vertexCount == 4 && b.triangles.size() == 2 && b.name == QStringLiteral("slab")
				&& b.skinPaths == QStringList{QStringLiteral("textures/test/slab")};
			for (int v = 0; same && v < 4; ++v) {
				same = nearVec(b.frames.at(0).positions.at(v), geometry.positions.at(v), 1.0e-6f) && nearly(b.texCoords.at(v).u, surface.texCoords.at(v).u)
					&& nearly(b.texCoords.at(v).v, surface.texCoords.at(v).v);
			}
			for (int t = 0; same && t < 2; ++t) {
				same = b.triangles.at(t).a == surface.triangles.at(t).a && b.triangles.at(t).b == surface.triangles.at(t).b
					&& b.triangles.at(t).c == surface.triangles.at(t).c;
			}
			expect(same, "static mesh round trip");
		}
		error.clear();
		expect(exportModelMd5Anim(slab, 0, {}, &error).isEmpty() && !error.isEmpty(), "no md5anim without a skeleton");
	}

	error.clear();
	expect(exportModelMd5Anim(mesh, 5, {}, &error).isEmpty() && !error.isEmpty(), "missing clip refused");
	error.clear();
	ModelMesh empty;
	expect(exportModelMd5Mesh(empty, {}, &error).isEmpty() && !error.isEmpty(), "no md5mesh without geometry");
}

void expectBroken(const char* label, const QString& path, const QByteArray& bytes)
{
	const ModelMesh mesh = decodeModelMesh(path, bytes);
	if (!expect(!mesh.error.isEmpty() && !mesh.geometryAvailable && mesh.surfaces.isEmpty() && mesh.skeleton.isEmpty(), label)) {
		printLines("warning", mesh.warnings);
	}
}

void testMalformed()
{
	const QByteArray meshText(kChainMesh);
	const QString meshPath = QStringLiteral("bad/chain.md5mesh");
	expectBroken("bad md5mesh version", meshPath, replaced(meshText, "MD5Version 10", "MD5Version 11"));
	expectBroken("vertex count beyond the data", meshPath, replaced(meshText, "numverts 4", "numverts 9"));
	expectBroken("huge joint count", meshPath, replaced(meshText, "numJoints 3", "numJoints 99999999"));
	expectBroken("negative mesh count", meshPath, replaced(meshText, "numMeshes 2", "numMeshes -1"));
	expectBroken("vertex count past the cap", meshPath, replaced(meshText, "numverts 4", "numverts 2000000"));
	expectBroken("parent pointing forward", meshPath, replaced(meshText, "\"hip\"\t0 (", "\"hip\"\t2 ("));
	expectBroken("parent pointing at itself", meshPath, replaced(meshText, "\"hip\"\t0 (", "\"hip\"\t1 ("));
	expectBroken("NaN position", meshPath, replaced(meshText, "( 0 0 10 )", "( 0 nan 10 )"));
	expectBroken("infinite position", meshPath, replaced(meshText, "( 0 0 10 )", "( 0 inf 10 )"));
	expectBroken("overflowing number", meshPath, replaced(meshText, "( 0 0 10 )", "( 0 1e999 10 )"));
	expectBroken("number beyond float", meshPath, replaced(meshText, "( 0 0 10 )", "( 0 1e300 10 )"));
	expectBroken("weight on a missing joint", meshPath, replaced(meshText, "weight 4 2 1", "weight 4 7 1"));
	expectBroken("vertex without weights", meshPath, replaced(meshText, "vert 3 ( 1 1 ) 4 1", "vert 3 ( 1 1 ) 4 0"));
	expectBroken("vertex weights past the list", meshPath, replaced(meshText, "vert 3 ( 1 1 ) 4 1", "vert 3 ( 1 1 ) 4 2"));
	expectBroken("fractional count", meshPath, replaced(meshText, "numtris 2", "numtris 2.5"));
	expectBroken("unterminated string", meshPath, replaced(meshText, "\"knee\"", "\"knee"));
	expectBroken("unterminated comment", meshPath, replaced(meshText, "hip and knee. */", "hip and knee."));
	expectBroken("duplicate joint names", meshPath, replaced(meshText, "\"knee\"", "\"HIP\""));

	const QByteArray animText(kWalkAnim);
	const QString animPath = QStringLiteral("bad/walk.md5anim");
	expectBroken("bad md5anim version", animPath, replaced(animText, "MD5Version 10", "MD5Version 9"));
	expectBroken("zero frames", animPath, replaced(animText, "numFrames 2", "numFrames 0"));
	expectBroken("frames beyond the data", animPath, replaced(animText, "numFrames 2", "numFrames 3"));
	expectBroken("too many components", animPath, replaced(animText, "numAnimatedComponents 4", "numAnimatedComponents 99"));
	expectBroken("start index beyond the components", animPath, replaced(animText, "\"knee\"\t1 1 3", "\"knee\"\t1 1 4"));
	expectBroken("negative start index", animPath, replaced(animText, "\"hip\"\t0 56 0", "\"hip\"\t0 56 -1"));
	expectBroken("undefined flag bits", animPath, replaced(animText, "\"hip\"\t0 56 0", "\"hip\"\t0 120 0"));
	expectBroken("anim parent pointing forward", animPath, replaced(animText, "\"knee\"\t1 1 3", "\"knee\"\t2 1 3"));
	expectBroken("frame numbered out of order", animPath, replaced(animText, "frame 1 {", "frame 5 {"));
	expectBroken("negative frame rate", animPath, replaced(animText, "frameRate 30", "frameRate -1"));
	expectBroken("NaN component", animPath, replaced(animText, "-0.7071067812 0 0 0", "-0.7071067812 0 nan 0"));
	expectBroken("pose count past the cap", animPath, replaced(animText, "numFrames 2\nnumJoints 3", "numFrames 8192\nnumJoints 4096"));
	expectBroken("missing frame values", animPath, replaced(animText, "-0.7071067812 0 0 0", "-0.7071067812 0 0"));

	// Truncation at every byte: an error until the last brace is present.
	const auto truncate = [](const char* label, const QString& path, const QByteArray& text) {
		const qsizetype lastBrace = text.lastIndexOf('}');
		int wrong = 0;
		for (qsizetype cut = 0; cut < text.size(); ++cut) {
			const ModelMesh mesh = decodeModelMesh(path, text.left(cut));
			const bool broken = !mesh.error.isEmpty() && mesh.surfaces.isEmpty() && mesh.skeleton.isEmpty();
			if (cut <= lastBrace ? !broken : !mesh.error.isEmpty()) {
				if (wrong++ == 0) {
					std::cerr << "  first bad cut at " << cut << ": " << mesh.error.toStdString() << "\n";
				}
			}
		}
		expect(wrong == 0, label);
	};
	truncate("md5mesh truncated at every byte", meshPath, meshText);
	truncate("md5anim truncated at every byte", animPath, animText);

	// Random byte damage with a fixed seed: never a crash, and anything that
	// still decodes is consistent.
	quint32 seed = 0x5eed1234u;
	const auto random = [&seed]() {
		seed = seed * 1664525u + 1013904223u;
		return seed >> 8;
	};
	const char noise[] = "0123456789-.e(){}\" \n/*xyz";
	const ModelCompanionSource source = fakeSource(standardFiles());
	int inconsistent = 0;
	for (int iteration = 0; iteration < 400; ++iteration) {
		const bool anim = (iteration & 1) != 0;
		QByteArray damaged = anim ? animText : meshText;
		const int flips = 1 + int(random() % 3);
		for (int flip = 0; flip < flips; ++flip) {
			const qsizetype at = qsizetype(random() % quint32(damaged.size()));
			damaged[at] = (random() & 1) ? char(random() & 0xFF) : noise[random() % (sizeof(noise) - 1)];
		}
		const ModelMesh mesh = anim ? decodeModelMesh(animPath, damaged) : decodeModelMesh(kMeshPath, damaged, nullptr, {}, source);
		if (mesh.error.isEmpty()) {
			QString problem;
			const bool consistent = validateModelSkeleton(mesh, &problem) && mesh.surfaces.size() <= 2 && !mesh.frames.isEmpty()
				&& mesh.geometryAvailable == !mesh.surfaces.isEmpty();
			inconsistent += consistent ? 0 : 1;
		} else if (mesh.geometryAvailable || !mesh.surfaces.isEmpty()) {
			++inconsistent;
		}
	}
	expect(inconsistent == 0, "random damage yields an error or a consistent mesh");
}

void testDeclarations()
{
	QStringList warnings;
	const QVector<Doom3ModelDecl> decls = parseDoom3ModelDecls(QString::fromLatin1(kDefText), &warnings);
	expect(warnings.isEmpty(), "well-formed .def has no warnings");
	printLines("warning", warnings);
	if (expect(decls.size() == 3, "three model declarations")) {
		const Doom3ModelDecl& chain = decls.at(0);
		expect(chain.name == QStringLiteral("test_chain") && chain.line == 8, "declaration name and line");
		expect(chain.meshPath == QStringLiteral("models/md5/test/chain.md5mesh"), "declaration mesh");
		expect(chain.skin == QStringLiteral("skins/test/chain.skin"), "declaration skin");
		expect(nearVec(chain.offset, {0, 0, 4}), "declaration offset");
		expect(chain.anims.size() == 2 && chain.anims.at(0).first == QStringLiteral("run")
				&& chain.anims.at(0).second == QStringLiteral("models/md5/test/walk.md5anim") && chain.anims.at(1).first == QStringLiteral("idle")
				&& chain.anims.at(1).second == QStringLiteral("models/md5/other/idle.md5anim"),
			"declaration anims in order, frame commands and alternatives skipped");
		const Doom3ModelDecl& variant = decls.at(1);
		expect(variant.name == QStringLiteral("test_chain_variant") && variant.line == 21 && variant.inherit == QStringLiteral("test_chain")
				&& variant.meshPath.isEmpty() && variant.anims.size() == 1,
			"inheriting declaration");
		expect(decls.at(2).name == QStringLiteral("unrelated") && decls.at(2).line == 26, "third declaration line");
	}

	// Nested and unbalanced braces.
	const QString unbalanced = QStringLiteral(
		"entityDef a { \"x\" \"{\" { nested { deeper } } }\n"
		"}\n"
		"model after_extra { mesh models/a.md5mesh anim walk }\n"
		"model unclosed { mesh models/b.md5mesh\n");
	warnings.clear();
	const QVector<Doom3ModelDecl> partial = parseDoom3ModelDecls(unbalanced, &warnings);
	expect(partial.size() == 1 && partial.at(0).name == QStringLiteral("after_extra") && partial.at(0).line == 3
			&& partial.at(0).meshPath == QStringLiteral("models/a.md5mesh") && partial.at(0).anims.isEmpty(),
		"declaration after a stray brace still parsed");
	expect(containsText(warnings, QStringLiteral("Line 2:")), "stray closing brace warned with its line");
	expect(containsText(warnings, QStringLiteral("Line 4:")), "unclosed block warned with its line");
	expect(containsText(warnings, QStringLiteral("Line 3:")), "anim without a path warned with its line");

	// Odd shapes never crash.
	const QStringList odd{
		QStringLiteral("model"),
		QStringLiteral("model x"),
		QStringLiteral("model x y { }"),
		QStringLiteral("{ } } } {"),
		QStringLiteral("model m { offset ( 1 2 } mesh a.md5mesh }"),
		QStringLiteral("model m { anim a \"unterminated\n }"),
		QStringLiteral("model m { channel c ( a b } anim x y.md5anim, }"),
		QStringLiteral("model m { /* open comment"),
		QStringLiteral("model m { anim x y.md5anim { frame 1 { nested } } skin s }"),
	};
	int oddDecls = 0;
	for (const QString& text : odd) {
		warnings.clear();
		oddDecls += int(parseDoom3ModelDecls(text, &warnings).size());
	}
	expect(oddDecls >= 2, "odd declarations parse where they can");
	{
		warnings.clear();
		const QVector<Doom3ModelDecl> offsetDecl = parseDoom3ModelDecls(QStringLiteral("model m { offset ( 1 2 } mesh a.md5mesh }"), &warnings);
		expect(offsetDecl.size() == 1 && !warnings.isEmpty(), "a short offset is warned and the block still closes");
	}

	// Every truncation and some damage.
	const QString full = QString::fromLatin1(kDefText);
	for (int cut = 0; cut < full.size(); ++cut) {
		warnings.clear();
		const QVector<Doom3ModelDecl> some = parseDoom3ModelDecls(full.left(cut), &warnings);
		if (some.size() > 3) {
			expect(false, "truncated .def yields no extra declarations");
			break;
		}
	}
	quint32 seed = 77u;
	for (int iteration = 0; iteration < 300; ++iteration) {
		QString damaged = full;
		for (int flip = 0; flip < 3; ++flip) {
			seed = seed * 1664525u + 1013904223u;
			const int at = int((seed >> 8) % quint32(damaged.size()));
			const char choices[] = "{}()\",/* \nmodel";
			damaged[at] = QLatin1Char(choices[(seed >> 4) % (sizeof(choices) - 1)]);
		}
		warnings.clear();
		(void)parseDoom3ModelDecls(damaged, &warnings);
	}
	expect(true, "damaged .def text parsed without crashing");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	testMeshDecode();
	testBudgetAndSources();
	testFileSystemLayout();
	testAnimDecode();
	testRoundTrips();
	testMalformed();
	testDeclarations();
	std::cout << "model_format_md5_smoke_test: " << g_checks << " checks, " << g_failures << " failure(s)\n";
	return g_failures == 0 ? 0 : 1;
}
