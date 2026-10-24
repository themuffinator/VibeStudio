// Independent oracle for the MD5, IQM and ASE writers.
//
// The fixtures here are defined by numbers: joint translations, rotations
// about stated axes, scales, vertex bind positions, weights, UVs and
// counter-clockwise faces. Each fixture is built through the editor's model
// API, exported with exportEditableModel, and written next to a
// definition.json holding those numbers. model_skeletal_writer_oracle.py then
// reads every exported file with its own parser, written from the public
// formats and the engines' loaders (no VibeStudio decoder is used), poses it
// the way the engine would, and compares the result with poses it computes
// from the definition alone. It also runs negative controls (flipped winding,
// a wrong quaternion sign, swapped joints) that must fail.
//
// argv[1] is the oracle script and argv[2] the Python interpreter; with no
// interpreter the test exits 77, which meson reports as skipped. Set
// VIBESTUDIO_MODELLER_EVIDENCE to keep the fixtures, files and receipt.

#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <numbers>

using namespace vibestudio;

namespace
{
bool failed = false;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		failed = true;
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}

// One joint's parent-relative transform: translation, then a rotation of
// `degrees` about `axis` (right-handed), then `scale`, applied to column
// vectors as T * R * S.
struct Local
{
	ModelVec3 translation{};
	ModelVec3 axis{0, 0, 1};
	double degrees = 0;
	ModelVec3 scale{1, 1, 1};
};

ModelQuat quaternion(const Local &local)
{
	const double length = std::sqrt(double(local.axis.x) * local.axis.x + double(local.axis.y) * local.axis.y + double(local.axis.z) * local.axis.z);
	const double half = local.degrees * std::numbers::pi / 360.0, s = std::sin(half) / length;
	return {float(local.axis.x * s), float(local.axis.y * s), float(local.axis.z * s), float(std::cos(half))};
}

QJsonArray vec(const ModelVec3 &v) { return {double(v.x), double(v.y), double(v.z)}; }

QJsonObject localJson(const Local &local)
{
	return {{QStringLiteral("translation"), vec(local.translation)},
			{QStringLiteral("axis"), vec(local.axis)},
			{QStringLiteral("degrees"), local.degrees},
			{QStringLiteral("scale"), vec(local.scale)}};
}

struct Clip
{
	QString name;
	int framesPerSecond = 24;
	QVector<QVector<Local>> frames;
};

struct Fixture
{
	QString name;
	QStringList joints;
	QVector<int> parents;
	QVector<Local> bind;
	QVector<Clip> clips;
	QString material;
	// Bind positions, UVs and up to two influences per vertex.
	QVector<ModelVec3> positions;
	QVector<ModelTexCoord> uvs;
	QVector<int> influenceJoints;
	QVector<float> influenceWeights;
	QVector<ModelTriangle> triangles;
};

// A strip of quads two rows high along +X, facing +Z, with columns 8 units
// apart. Faces are counter-clockwise seen from +Z.
void strip(Fixture *fixture, int columns)
{
	for (int row = 0; row < 2; ++row)
		for (int column = 0; column < columns; ++column)
		{
			fixture->positions.append({float(column * 8), float(row * 8), 0.0f});
			fixture->uvs.append({float(column) / float(columns - 1), float(row)});
		}
	for (int column = 0; column + 1 < columns; ++column)
	{
		const int a = column, b = column + 1, c = columns + column + 1, d = columns + column;
		fixture->triangles.append({a, b, c});
		fixture->triangles.append({a, c, d});
	}
}

// Root at the origin, mid at x = 8 and tip at x = 16 along a strip from x = 0
// to 24. "idle" bends mid; "wave" lifts the root and turns the tip. The
// vertices at x = 8 are shared by root (0.25) and mid (0.75).
Fixture armFixture(const QString &name, const ModelVec3 &bendAxis, const ModelVec3 &tipScale)
{
	Fixture fixture;
	fixture.name = name;
	fixture.joints = {QStringLiteral("root"), QStringLiteral("mid"), QStringLiteral("tip")};
	fixture.parents = {-1, 0, 1};
	fixture.material = QStringLiteral("models/oracle/%1").arg(name);
	const Local root{}, mid{{8, 0, 0}}, tip{{8, 0, 0}, {0, 0, 1}, 0, tipScale};
	fixture.bind = {root, mid, tip};
	Clip idle{QStringLiteral("idle"), 24, {}};
	for (int frame = 0; frame < 3; ++frame)
		idle.frames.append({root, Local{{8, 0, 0}, bendAxis, 30.0 * frame}, tip});
	Clip wave{QStringLiteral("wave"), 30, {}};
	for (int frame = 0; frame < 4; ++frame)
		wave.frames.append({Local{{0, 0, float(frame)}}, mid, Local{{8, 0, 0}, {0, 0, 1}, 20.0 * frame, tipScale}});
	fixture.clips = {idle, wave};
	strip(&fixture, 4);
	for (const auto &p : std::as_const(fixture.positions))
	{
		const int first = p.x < 4 ? 0 : p.x < 20 ? (p.x < 12 ? 0 : 1) : 2;
		const bool shared = p.x > 4 && p.x < 12;
		fixture.influenceJoints << first << (shared ? 1 : 0);
		fixture.influenceWeights << (shared ? 0.25f : 1.0f) << (shared ? 0.75f : 0.0f);
	}
	return fixture;
}

QVector<ModelJointMatrix> modelSpace(const Fixture &fixture, const QVector<Local> &locals)
{
	QVector<ModelJointMatrix> matrices;
	for (const auto &local : locals)
		matrices.append(modelJointMatrix(quaternion(local), local.translation, local.scale));
	return modelJointsToModelSpace(fixture.parents, matrices);
}

bool build(const Fixture &fixture, ModelMesh *mesh, QString *error)
{
	ModelDesign design;
	design.parts.append(ModelDesignPart{});
	*mesh = buildModelDesignMesh(design);
	ModelSurface surface;
	surface.name = fixture.name;
	surface.texCoords = fixture.uvs;
	surface.triangles = fixture.triangles;
	surface.vertexCount = int(fixture.positions.size());
	surface.skinPaths = {fixture.material};
	ModelFrameGeometry frame;
	frame.positions = fixture.positions;
	frame.normals.fill({0, 0, 1}, frame.positions.size());
	surface.frames = {frame};
	mesh->surfaces = {surface};
	mesh->frames.resize(1);
	mesh->frames[0].name = QStringLiteral("base");
	auto &skeleton = mesh->skeleton;
	skeleton.sourceFormat = QStringLiteral("oracle");
	const auto bind = modelSpace(fixture, fixture.bind);
	for (int joint = 0; joint < fixture.joints.size(); ++joint)
		skeleton.joints.append({fixture.joints[joint], fixture.parents[joint], bind[joint]});
	for (const auto &clip : fixture.clips)
	{
		ModelSkeletalClip baked;
		baked.name = clip.name;
		baked.framesPerSecond = clip.framesPerSecond;
		for (const auto &locals : clip.frames)
			baked.frames.append(modelSpace(fixture, locals));
		skeleton.clips.append(baked);
	}
	auto &target = mesh->surfaces[0];
	if (!modelSkinningFromBindPose(skeleton, target.frames[0].positions, target.frames[0].normals, fixture.influenceJoints,
								   fixture.influenceWeights, 2, &target.skinning, error) ||
		!bakeModelSkeleton(mesh, {}, error))
		return false;
	updateEditableModelMetadata(mesh);
	return true;
}

QJsonObject definition(const Fixture &fixture, const QJsonObject &files)
{
	QJsonArray joints, bind, clips, vertices, triangles;
	for (int joint = 0; joint < fixture.joints.size(); ++joint)
	{
		joints.append(QJsonObject{{QStringLiteral("name"), fixture.joints[joint]}, {QStringLiteral("parent"), fixture.parents[joint]}});
		bind.append(localJson(fixture.bind[joint]));
	}
	for (const auto &clip : fixture.clips)
	{
		QJsonArray frames;
		for (const auto &locals : clip.frames)
		{
			QJsonArray frame;
			for (const auto &local : locals)
				frame.append(localJson(local));
			frames.append(frame);
		}
		clips.append(QJsonObject{{QStringLiteral("name"), clip.name}, {QStringLiteral("framesPerSecond"), clip.framesPerSecond}, {QStringLiteral("frames"), frames}});
	}
	for (int v = 0; v < fixture.positions.size(); ++v)
	{
		QJsonArray weights;
		for (int slot = 0; slot < 2; ++slot)
		{
			if (fixture.influenceWeights[v * 2 + slot] > 0)
				weights.append(QJsonArray{fixture.influenceJoints[v * 2 + slot], double(fixture.influenceWeights[v * 2 + slot])});
		}
		vertices.append(QJsonObject{{QStringLiteral("position"), vec(fixture.positions[v])},
									{QStringLiteral("uv"), QJsonArray{double(fixture.uvs[v].u), double(fixture.uvs[v].v)}},
									{QStringLiteral("weights"), weights}});
	}
	for (const auto &triangle : fixture.triangles)
		triangles.append(QJsonArray{triangle.a, triangle.b, triangle.c});
	return {{QStringLiteral("name"), fixture.name},
			{QStringLiteral("joints"), joints},
			{QStringLiteral("bind"), bind},
			{QStringLiteral("clips"), clips},
			{QStringLiteral("vertices"), vertices},
			{QStringLiteral("triangles"), triangles},
			{QStringLiteral("material"), fixture.material},
			{QStringLiteral("files"), files}};
}

bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// Exports every format of one fixture into `directory`, with its definition.
bool exportFixture(const Fixture &fixture, const QString &directory)
{
	ModelMesh mesh;
	QString error;
	if (!expect(build(fixture, &mesh, &error), "the fixture builds"))
	{
		std::cerr << "  " << fixture.name.toStdString() << ": " << error.toStdString() << '\n';
		return false;
	}
	QDir().mkpath(directory);
	QJsonObject files, animations;
	const auto exportTo = [&](const QString &format, const ModelExportOptions &options, const QString &name)
	{
		const auto bytes = exportEditableModel(mesh, format, options, &error);
		if (!expect(!bytes.isEmpty() && write(QDir(directory).filePath(name), bytes), "the fixture exports"))
		{
			std::cerr << "  " << fixture.name.toStdString() << ' ' << format.toStdString() << ": " << error.toStdString() << '\n';
			return false;
		}
		return true;
	};
	bool ok = exportTo(QStringLiteral("md5mesh"), {}, fixture.name + QStringLiteral(".md5mesh")) &&
			  exportTo(QStringLiteral("iqm"), {}, fixture.name + QStringLiteral(".iqm"));
	// The ASE holds the bind pose, which baking writes as frame 0.
	ok = ok && expect(modelBindPoseFrame(mesh) == 0, "the bind pose is frame 0") &&
		 exportTo(QStringLiteral("ase"), {}, fixture.name + QStringLiteral(".ase"));
	for (int clip = 0; ok && clip < fixture.clips.size(); ++clip)
	{
		ModelExportOptions options;
		options.clip = clip;
		const auto name = QStringLiteral("%1_%2.md5anim").arg(fixture.name, fixture.clips[clip].name);
		ok = exportTo(QStringLiteral("md5anim"), options, name);
		animations.insert(fixture.clips[clip].name, name);
	}
	files = {{QStringLiteral("md5mesh"), fixture.name + QStringLiteral(".md5mesh")},
			 {QStringLiteral("md5anim"), animations},
			 {QStringLiteral("iqm"), fixture.name + QStringLiteral(".iqm")},
			 {QStringLiteral("ase"), fixture.name + QStringLiteral(".ase")}};
	return ok && expect(write(QDir(directory).filePath(QStringLiteral("definition.json")),
							  QJsonDocument(definition(fixture, files)).toJson(QJsonDocument::Indented)),
						"the definition is written");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto script = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
	const auto python = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
	if (python.isEmpty() || !QFileInfo::exists(script))
	{
		std::cout << "SKIP: the writer oracle needs Python and model_skeletal_writer_oracle.py.\n";
		return 77;
	}
	QTemporaryDir temporary;
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	const auto root = evidence.isEmpty() ? temporary.path() : QDir(evidence).filePath(QStringLiteral("skeletal-writer-oracle"));
	if (!evidence.isEmpty())
		QDir(root).removeRecursively();
	// "arm" is rigid; "stretched" bends mid about an oblique axis and gives the
	// tip a mirrored, non-uniform scale in its bind and every pose.
	const QVector<Fixture> fixtures{armFixture(QStringLiteral("arm"), {0, 0, 1}, {1, 1, 1}),
									armFixture(QStringLiteral("stretched"), {0.3f, 0.5f, 0.81f}, {-1.0f, 1.5f, 1.0f})};
	for (const auto &fixture : fixtures)
	{
		if (!exportFixture(fixture, QDir(root).filePath(fixture.name)))
			return 1;
	}
	QProcess oracle;
	oracle.setProcessChannelMode(QProcess::ForwardedChannels);
	oracle.start(python, {QStringLiteral("-I"), script, QStringLiteral("--fixtures"), root, QStringLiteral("--receipt"),
						  QDir(root).filePath(QStringLiteral("receipt.json"))});
	if (!expect(oracle.waitForStarted(30000), "the Python oracle starts"))
		return 77;
	if (!expect(oracle.waitForFinished(120000), "the Python oracle finishes"))
	{
		oracle.kill();
		return 1;
	}
	expect(oracle.exitStatus() == QProcess::NormalExit && oracle.exitCode() == 0, "the independent oracle accepts every exported file");
	if (!failed)
		std::cout << "model skeletal writer oracle passed\n";
	return failed ? 1 : 0;
}
