#include "core/model_assembly_animation.h"
#include "core/model_recovery.h"
#include "tests/model_assembly_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
bool write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool same(ModelVec3 a, ModelVec3 b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc < 2 || root.isEmpty() || !QDir().mkpath(root))
	{
		return 1;
	}
	QTemporaryDir temporary(QDir(root).filePath("assembly-animation-XXXXXX"));
	if (!temporary.isValid())
	{
		return 1;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	auto input = tests::assemblyModel();
	input.surfaces[0].uvSeams = {{0, 1}};
	input.surfaces[0].skinPaths = {QStringLiteral("textures/base.pcx"), QStringLiteral("textures/alternate.pcx")};
	const auto original = bytes(input);
	auto recipe = tests::assemblyRecipe(path("part.mesh.json"));
	ModelAssemblyDocument source;
	QString error;
	bool ok = expect(write(path("part.mesh.json"), original) && source.setAssembly(recipe, temporary.path(), &error) &&
						 source.save(path("source.assembly.json"), false, &error),
					 "prepare immutable linked fixture");
	ModelAssemblyResolved resolved;
	ok &= expect(resolveModelAssembly(recipe, {temporary.path(), {}, {}}, &resolved, &error), "resolve original animation inputs");
	ModelAssemblyAnimationOptions options{.25, 5, 2.5, QStringLiteral("composed")};
	ModelAssemblyAnimation animation;
	ok &= expect(bakeModelAssemblyAnimation(recipe, resolved, options, &animation, &error), "bake fractional-rate linked animation");
	if (!ok)
	{
		std::cerr << error.toStdString();
		return 1;
	}
	ok &= expect(animation.mesh.frames.size() == 5 && animation.mesh.animations.size() == 1 &&
					 animation.mesh.animations[0].framesPerSecond == 2.5 && animation.mesh.animations[0].frameCount == 5 &&
					 animation.mesh.tags.isEmpty() && animation.mesh.frames[4].name == "bake0004",
				 "bake carries exact count, name and durable timing");
	ok &=
		expect(animation.mesh.surfaces[1].frames[0].positions[0].x == 12.5f && animation.mesh.surfaces[1].frames[0].positions[0].z == .5f && animation.mesh.surfaces[0].frames[0].positions[0].z == .25f,
			   "independent root and child animation compose analytically");
	for (int frame = 0; frame < options.frameCount; ++frame)
	{
		ModelAssemblyPose sample;
		bool exact = sampleModelAssembly(recipe, resolved, options.startSeconds + frame / options.framesPerSecond, &sample, &error);
		for (int s = 0; s < animation.mesh.surfaces.size() && exact; ++s)
		{
			const auto &surface = animation.mesh.surfaces[s];
			exact &= surface.uvSeams == input.surfaces[0].uvSeams && surface.skinPaths == input.surfaces[0].skinPaths;
			for (int v = 0; v < surface.vertexCount; ++v)
			{
				exact &= same(surface.frames[frame].positions[v], sample.mesh.surfaces[s].frames[0].positions[v]) &&
						 same(surface.frames[frame].normals[v], sample.mesh.surfaces[s].frames[0].normals[v]);
			}
		}
		ok &= expect(exact, "sampled positions, normals, seams and materials agree at every output time");
	}
	const auto baked = bytes(animation.mesh);
	ModelMesh parsed;
	ok &= expect(editableModelJson(animation.mesh).value("version").toInt() == 6 && parseEditableModel(baked, &parsed, &error) &&
					 bytes(parsed) == baked,
				 "mesh v6 preserves animation timing exactly");
	ok &= expect(editableModelJson(input).value("version").toInt() == 3, "untimed sources retain existing schema");
	auto withCollision = animation.mesh;
	ModelCollisionBox bounds;
	bounds.name = QStringLiteral("bounds");
	withCollision.collisionBoxes = {bounds};
	ok &= expect(parseEditableModel(bytes(withCollision), &parsed, &error) && parsed.collisionBoxes.size() == 1 &&
					 parsed.animations[0].framesPerSecond == 2.5,
				 "v6 retains optional collision");
	auto native = decodeModelMesh("fixture.mdl", tests::groupedMdlFixture().bytes);
	native.animations = {{QStringLiteral("clip"), 0, native.frameCount, 23.976}};
	native.collisionBoxes = {bounds};
	ok &= expect(parseEditableModel(bytes(native), &parsed, &error) && parsed.mdl.enabled && parsed.collisionBoxes.size() == 1 &&
					 parsed.animations[0].framesPerSecond == 23.976,
				 "v6 preserves native MDL metadata and collision alongside clip timing");
	for (const QJsonValue &rate : {QJsonValue(0), QJsonValue(-1), QJsonValue(.0001), QJsonValue(1001), QJsonValue("24"), QJsonValue()})
	{
		auto json = editableModelJson(animation.mesh);
		auto clips = json.value("animations").toArray();
		auto clip = clips[0].toObject();
		clip.insert("framesPerSecond", rate);
		clips[0] = clip;
		json.insert("animations", clips);
		ok &= expect(!parseEditableModel(QJsonDocument(json).toJson(), &parsed, &error), "malformed saved rate rejected");
	}
	auto old = editableModelJson(animation.mesh);
	old.insert("version", 3);
	ok &= expect(!parseEditableModel(QJsonDocument(old).toJson(), &parsed, &error), "rate cannot silently downgrade into old schema");
	ModelDocument document;
	ok &= expect(document.setMesh(animation.mesh, &error), "adopt animation in normal authoring document");
	ModelEdit edit;
	edit.kind = ModelEditKind::SetAnimationRate;
	edit.animationIndex = 0;
	edit.animationRate = 23.976;
	ok &= expect(document.edit(edit, &error) && document.mesh().animations[0].framesPerSecond == 23.976 && document.undo() &&
					 bytes(document.mesh()) == baked && document.redo(),
				 "rate edit is selection-safe and undoable");
	edit.animationRate = std::numeric_limits<double>::quiet_NaN();
	const auto prior = document.revisionFingerprint();
	ok &= expect(!document.edit(edit, &error) && document.revisionFingerprint() == prior, "nonfinite rate is atomic");
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = document.mesh();
	snapshot.title = "Timing recovery";
	const auto recovery = writeModelRecovery(snapshot, path("recoveries"), "b42ec757-fae8-43c5-b3d8-1e07d717a408", &error);
	ModelRecoverySnapshot restored;
	ok &=
		expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && bytes(restored.mesh) == bytes(document.mesh()),
			   "recovery retains exact saved timing");
	edit.animationRate = 0;
	ok &= expect(document.edit(edit, &error) && editableModelJson(document.mesh()).value("version").toInt() == 3,
				 "clearing final saved rate returns losslessly to untimed schema");
	ModelWorkControl cancellation;
	bool cancel = false;
	cancellation.cancelled = [&] { return cancel; };
	cancellation.progress = [&](ModelWorkPhase phase, qint64 done, qint64 total) {
		if (phase == ModelWorkPhase::Editing && total == 5 && done == 2)
			cancel = true;
	};
	ok &= expect(!bakeModelAssemblyAnimation(recipe, resolved, options, &animation, &error, cancellation) && bytes(animation.mesh) == baked,
				 "mid-sequence cancellation leaves caller output intact");
	for (int n = 0; n < 8; ++n)
	{
		auto bad = options;
		if (n == 0)
			bad.frameCount = 0;
		if (n == 1)
			bad.frameCount = 1025;
		if (n == 2)
			bad.framesPerSecond = 0;
		if (n == 3)
			bad.framesPerSecond = std::numeric_limits<double>::infinity();
		if (n == 4)
			bad.startSeconds = -1;
		if (n == 5)
			bad.startSeconds = 1000000;
		if (n == 6)
			bad.clipName = " name ";
		if (n == 7)
			bad.clipName = QString(129, 'x');
		ok &= expect(!bakeModelAssemblyAnimation(recipe, resolved, bad, &animation, &error) && bytes(animation.mesh) == baked,
					 "invalid bake settings reject without publishing partial geometry");
	}
	auto oversized = resolved;
	for (auto &part : oversized.inputs)
	{
		part.mesh.surfaces[0].vertexCount = 2049;
	}
	auto maximum = options;
	maximum.frameCount = 1024;
	ok &= expect(!bakeModelAssemblyAnimation(recipe, oversized, maximum, &animation, &error) && error.contains("1,048,576"),
				 "preflight refuses excessive storage before sampling");
	for (auto &part : recipe.parts)
		part.interpolate = false;
	auto reflected = resolved;
	reflected.recipeSha256 = modelAssemblyFingerprint(recipe);
	reflected.inputs[0].mesh.tags[1].axis[0] = -1;
	auto winding = options;
	winding.startSeconds = 0;
	winding.framesPerSecond = 1;
	winding.frameCount = 2;
	ok &= expect(!bakeModelAssemblyAnimation(recipe, reflected, winding, &animation, &error) && error.contains("winding") &&
					 bytes(animation.mesh) == baked,
				 "reflected tag winding changes reject whole sequence");
	recipe = source.assembly();
	ModelExportReport report;
	ok &= expect(
		exportModelAssemblyAnimation(recipe, resolved, options, path("out.mesh.json"), source.path(), false, true, &error, {}, &report) &&
			!QFileInfo::exists(path("out.mesh.json")),
		"dry run validates complete sequence without writing");
	ok &= expect(exportModelAssemblyAnimation(recipe, resolved, options, path("out.mesh.json"), source.path(), false, false, &error) &&
					 read(path("out.mesh.json")) == baked,
				 "editable export preserves every pose and saved timing");
	ok &= expect(!exportModelAssemblyAnimation(recipe, resolved, options, path("out.mesh.json"), source.path(), false, false, &error),
				 "existing output needs overwrite");
	ok &= expect(!exportModelAssemblyAnimation(recipe, resolved, options, path("part.mesh.json"), source.path(), true, false, &error) &&
					 read(path("part.mesh.json")) == original,
				 "bake protects original model");
	ok &= expect(!exportModelAssemblyAnimation(recipe, resolved, options, path("out.obj"), source.path(), false, false, &error),
				 "single-pose OBJ cannot silently truncate animation");
	ok &=
		expect(exportModelAssemblyAnimation(recipe, resolved, options, path("out.md3"), source.path(), false, false, &error, {}, &report) &&
				   decodeModelMesh("out.md3", read(path("out.md3"))).frameCount == 5 && !report.notes.isEmpty(),
			   "MD3 emits complete sequence with omission report");
	ok &= expect(!exportModelAssemblyAnimation(recipe, resolved, options, path("multi.md2"), source.path(), false, false, &error),
				 "MD2 surface restriction remains explicit");
	auto single = recipe;
	single.parts.resize(1);
	auto singleResolved = resolved;
	singleResolved.inputs.resize(1);
	singleResolved.recipeSha256 = modelAssemblyFingerprint(single);
	ok &= expect(exportModelAssemblyAnimation(single, singleResolved, options, path("out.md2"), source.path(), false, false, &error) &&
					 decodeModelMesh("out.md2", read(path("out.md2"))).frameCount == 5,
				 "compatible MD2 exports all poses");
	bool raced = false;
	ModelWorkControl race;
	race.progress = [&](ModelWorkPhase phase, qint64 done, qint64 total) {
		if (!raced && phase == ModelWorkPhase::Editing && total == options.frameCount && done == 2)
		{
			raced = write(path("raced.mesh.json"), "external writer");
		}
	};
	ok &= expect(
		!exportModelAssemblyAnimation(recipe, resolved, options, path("raced.mesh.json"), source.path(), true, false, &error, race) &&
			raced && read(path("raced.mesh.json")) == "external writer",
		"destination race cannot replace a newly created file even with overwrite");
	cancel = false;
	ok &= expect(
		!exportModelAssemblyAnimation(recipe, resolved, options, path("out.mesh.json"), source.path(), true, false, &error, cancellation) &&
			read(path("out.mesh.json")) == baked,
		"cancelled overwrite retains complete existing output");
	const bool coreOnly = QString::fromLocal8Bit(argv[1]) == QStringLiteral("--core-only");
	if (!coreOnly)
	{
		QJsonObject output;
		const auto run = [&](QStringList args, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{"--cli", "--settings-file", path("settings.ini")} + args + QStringList{"--json"});
			if (!process.waitForFinished(30000))
			{
				process.kill();
				process.waitForFinished();
				return false;
			}
			const auto stdoutBytes = process.readAllStandardOutput();
			output = QJsonDocument::fromJson(stdoutBytes).object();
			if (process.exitCode() != code)
				std::cerr << stdoutBytes.toStdString() << process.readAllStandardError().toStdString();
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == code && !output.isEmpty();
		};
		const QStringList bake{"model",		   "assembly", "source.assembly.json", "--operation", "bake-animation", "--time",	".25",
							   "--frames",	   "5",		   "--sample-fps",		   "2.5",		  "--clip-name",	"composed", "--output",
							   "cli.mesh.json"};
		ok &=
			expect(run(bake + QStringList{"--dry-run"}) && !output.value("written").toBool() && !QFileInfo::exists(path("cli.mesh.json")) &&
					   output.value("sampling").toObject().value("durationSeconds").toDouble() == 2,
				   "CLI dry run reports exact endpoint-exclusive duration");
		ok &= expect(run(bake) && read(path("cli.mesh.json")) == baked, "CLI bake matches shared service exactly");
		ok &= expect(run(bake, 4), "CLI protects existing derivatives");
		ok &= expect(run(bake + QStringList{"--overwrite"}), "CLI explicit overwrite accepted");
		for (const auto &extra :
			 {QStringList{"--sample-fps", "3"}, QStringList{"--fps", "3"}, QStringList{"--frames=6"}, QStringList{"--unknown", "x"}})
			ok &= expect(run(bake + extra, 2), "CLI rejects repeated and inapplicable settings");
		ok &= expect(run({"model", "assembly", "source.assembly.json", "--operation", "bake-animation", "--output", "missing.md3"}, 2),
					 "CLI requires explicit count and rate");
		ok &=
			expect(run({"model", "edit", "cli.mesh.json", "--operation", "set-clip-fps", "--clip", "0", "--clip-fps", "23.976", "--output",
						"rate.mesh.json"}) &&
					   parseEditableModel(read(path("rate.mesh.json")), &parsed, &error) && parsed.animations[0].framesPerSecond == 23.976,
				   "CLI fractional timing edit persists");
		ok &= expect(run({"model", "animations", "rate.mesh.json"}) &&
						 output.value("clips").toArray()[0].toObject().value("framesPerSecond").toDouble() == 23.976,
					 "CLI inspection reports saved rate");
	}
	else
	{
		std::cout << "CLI checks skipped (--core-only)\n";
	}
	ok &= expect(read(path("part.mesh.json")) == original, "all exports and edits preserve linked originals");
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " assembly animation core/CLI checks\n";
	return ok ? 0 : 1;
}
