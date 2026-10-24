#include "cli/model_tools.h"
#include "tests/model_output_test_helpers.h"
#include "core/model_recovery.h"
#include "tests/model_rig_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *label)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
	return value;
}
bool same(ModelVec3 a, ModelVec3 b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool same(const QVector<ModelVec3> &a, const QVector<ModelVec3> &b)
{
	if (a.size() != b.size())
		return false;
	for (int i = 0; i < a.size(); ++i)
		if (!same(a[i], b[i]))
			return false;
	return true;
}
bool close(double a, double b)
{
	return std::abs(a - b) < .0001;
}
ModelEdit paint(const QString &joint = "tip", double value = .8, double strength = .5)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::PaintSkinWeights;
	ModelWeightStroke s;
	s.joint = joint;
	s.value = value;
	s.strength = strength;
	s.samples = {{0, 1}, {1, .5}};
	edit.weightStrokes = {s};
	return edit;
}
ModelMesh lockedFixture()
{
	auto mesh = rigFixture();
	mesh.skeleton.joints.append({"fixed", 0, modelJointIdentity()});
	for (auto &clip : mesh.skeleton.clips)
		for (auto &pose : clip.frames)
			pose.append(modelJointIdentity());
	auto &surface = mesh.surfaces[0];
	if (!modelSkinningFromBindPose(mesh.skeleton, surface.frames[0].positions, surface.frames[0].normals, {0, 1, 2, 0, 1, 2, 0, 1, 2},
								   {.4, .4, .2, .4, .4, .2, .4, .4, .2}, 3, &surface.skinning))
		return {};
	if (!bakeModelSkeleton(&mesh))
		return {};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("weight-paint-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto mesh = rigFixture();
	const auto bytes = rigBytes(mesh);
	QString error;
	ModelDocument document;
	auto edit = paint();
	expect(document.setMesh(mesh, &error) && document.edit(edit, &error), "replace stroke validates and commits");
	if (!error.isEmpty())
		std::cerr << error.toStdString() << '\n';
	const auto replaced = document.mesh();
	expect(close(modelVertexJointWeight(replaced.surfaces[0], 0, 1), .4) && close(modelVertexJointWeight(replaced.surfaces[0], 1, 1), .2),
		   "opacity times strength gives analytic weights");
	expect(close(replaced.surfaces[0].frames[2].positions[0].y, 1.6) && close(replaced.surfaces[0].frames[2].positions[1].y, .8),
		   "paint rebakes skeletal clip positions");
	expect(close(modelVertexJointWeight(replaced.surfaces[0], 2, 0), 1) && close(replaced.surfaces[0].frames[2].positions[2].y, 8),
		   "uncovered vertex remains intact");
	expect(same(replaced.surfaces[0].frames[0].positions, mesh.surfaces[0].frames[0].positions) &&
			   same(replaced.surfaces[0].frames[0].normals, mesh.surfaces[0].frames[0].normals),
		   "bind frame stays byte exact");
	expect(document.undo() && rigBytes(document.mesh()) == bytes, "one document undo restores weights and poses");
	expect(document.redo() && rigBytes(document.mesh()) == rigBytes(replaced), "redo restores painted result");
	auto add = paint("tip", .5, .5);
	add.weightStrokes[0].mode = ModelWeightPaintMode::Add;
	expect(document.edit(add, &error) && close(modelVertexJointWeight(document.mesh().surfaces[0], 0, 1), .65),
		   "add scales requested amount by strength");
	add.weightStrokes[0].mode = ModelWeightPaintMode::Subtract;
	expect(document.edit(add, &error) && close(modelVertexJointWeight(document.mesh().surfaces[0], 0, 1), .4),
		   "subtract returns analytic original weight");
	auto smooth = paint("tip", 1, 1);
	smooth.weightStrokes[0].mode = ModelWeightPaintMode::Smooth;
	smooth.weightStrokes[0].samples = {{0, 1}, {1, 1}, {2, 1}};
	const auto smoothSource = document.mesh();
	expect(document.edit(smooth, &error), "smooth stroke commits");
	expect(close(modelVertexJointWeight(document.mesh().surfaces[0], 0, 1), .1) &&
			   close(modelVertexJointWeight(document.mesh().surfaces[0], 1, 1), .2) &&
			   close(modelVertexJointWeight(document.mesh().surfaces[0], 2, 1), .3),
		   "smooth uses all unique edge neighbours from a frozen source");
	auto reversed = smoothSource;
	std::reverse(smooth.weightStrokes[0].samples.begin(), smooth.weightStrokes[0].samples.end());
	expect(applyModelEdit(&reversed, smooth, nullptr, &error) && rigBytes(reversed) == rigBytes(document.mesh()),
		   "smoothing is independent of sample order");
	const auto locked = lockedFixture();
	auto lockEdit = paint("tip", 1, 1);
	lockEdit.weightStrokes[0].lockedJoints = {"fixed"};
	lockEdit.weightStrokes[0].samples = {{0, 1}};
	auto lockResult = locked;
	expect(applyModelEdit(&lockResult, lockEdit, nullptr, &error), "painting respects locked weights");
	expect(close(modelVertexJointWeight(lockResult.surfaces[0], 0, 1), .8) &&
			   close(modelVertexJointWeight(lockResult.surfaces[0], 0, 2), .2),
		   "requested weight clamps to unlocked budget");
	const auto &oldLock = locked.surfaces[0].skinning.influences[2];
	const auto &newLock = lockResult.surfaces[0].skinning.influences[1];
	expect(oldLock.weight == newLock.weight && same(oldLock.offset, newLock.offset) && same(oldLock.normalOffset, newLock.normalOffset),
		   "locked influence and offsets stay exact");
	auto reduce = paint("root", 0, 1);
	reduce.weightStrokes[0].samples = {{0, 1}};
	auto candidate = mesh;
	expect(!applyModelEdit(&candidate, reduce, nullptr, &error) && rigBytes(candidate) == bytes,
		   "reducing sole influence refuses an implicit destination");
	reduce.weightStrokes[0].remainderJoint = "tip";
	expect(applyModelEdit(&candidate, reduce, nullptr, &error) && close(modelVertexJointWeight(candidate.surfaces[0], 0, 1), 1),
		   "explicit remainder joint receives sole influence reduction");
	auto batch = edit;
	batch.weightStrokes.append(add.weightStrokes[0]);
	auto sequential = mesh;
	expect(applyModelEdit(&sequential, edit, nullptr, &error) && applyModelEdit(&sequential, add, nullptr, &error),
		   "prepare sequential comparison");
	candidate = mesh;
	expect(applyModelEdit(&candidate, batch, nullptr, &error) && rigBytes(candidate) == rigBytes(sequential),
		   "batch weights and poses match sequential strokes");
	auto two = mesh;
	auto second = two.surfaces[0];
	second.name = "armour";
	second.index = 1;
	two.surfaces.append(second);
	updateEditableModelMetadata(&two);
	batch = edit;
	auto secondStroke = edit.weightStrokes[0];
	secondStroke.surface = 1;
	secondStroke.value = .2;
	batch.weightStrokes.append(secondStroke);
	expect(applyModelEdit(&two, batch, nullptr, &error) && close(modelVertexJointWeight(two.surfaces[0], 0, 1), .4) &&
			   close(modelVertexJointWeight(two.surfaces[1], 0, 1), .1),
		   "multiple surfaces share one atomic batch");
	const auto unchanged = rigBytes(two);
	batch.weightStrokes.last().joint = "missing";
	expect(!applyModelEdit(&two, batch, nullptr, &error) && rigBytes(two) == unchanged,
		   "later invalid stroke cannot publish earlier edits");
	batch = edit;
	batch.weightStrokes.append(reduce.weightStrokes[0]);
	batch.weightStrokes.last().remainderJoint.clear();
	batch.weightStrokes.last().samples = {{2, 1}};
	candidate = mesh;
	expect(!applyModelEdit(&candidate, batch, nullptr, &error) && rigBytes(candidate) == bytes,
		   "late semantic failure rolls back prior valid stroke");
	for (int variant = 0; variant < 15; ++variant)
	{
		auto bad = edit;
		auto &s = bad.weightStrokes[0];
		switch (variant)
		{
		case 0:
			s.samples.append(s.samples[0]);
			break;
		case 1:
			s.samples[0].vertex = 3;
			break;
		case 2:
			s.samples[0].opacity = -.1;
			break;
		case 3:
			s.samples[0].opacity = std::numeric_limits<double>::quiet_NaN();
			break;
		case 4:
			s.strength = 0;
			break;
		case 5:
			s.value = 2;
			break;
		case 6:
			s.mode = ModelWeightPaintMode(8);
			break;
		case 7:
			s.lockedJoints = {"tip"};
			break;
		case 8:
			s.lockedJoints = {"root", "root"};
			break;
		case 9:
			s.remainderJoint = "tip";
			break;
		case 10:
			s.lockedJoints = {"root"};
			s.remainderJoint = "root";
			break;
		case 11:
			s.surface = -1;
			break;
		case 12:
			bad.frame = 0;
			break;
		case 13:
			bad.selection.vertices = {0};
			break;
		case 14:
			s.samples.clear();
			break;
		}
		candidate = mesh;
		expect(!applyModelEdit(&candidate, bad, nullptr, &error) && rigBytes(candidate) == bytes, "invalid brush parameters are atomic");
	}
	for (int variant = 0; variant < 5; ++variant)
	{
		candidate = mesh;
		if (variant == 0)
			candidate.surfaces[0].frames[2].positions[0].x += 1;
		if (variant == 1)
			candidate.surfaces[0].frames[2].normals[0] = {1, 0, 0};
		if (variant == 2)
			candidate.skeleton.bakedClipForFrame.clear();
		if (variant == 3)
			candidate.surfaces[0].skinning = {};
		if (variant == 4)
		{
			candidate.surfaces[0].skinning.influences[0].weight = .5;
			expect(bakeModelSkeleton(&candidate), "bake invalid-weight fixture");
		}
		updateEditableModelMetadata(&candidate);
		const auto before = rigBytes(candidate);
		expect(!applyModelEdit(&candidate, edit, nullptr, &error) && rigBytes(candidate) == before,
			   "independent or invalid source binding is protected");
	}
	auto noncanonical = locked;
	noncanonical.surfaces[0].skinning.influences[0].offset.x += 1;
	noncanonical.surfaces[0].skinning.influences[1].offset.x -= 1;
	expect(bakeModelSkeleton(&noncanonical), "bake imported-offset fixture");
	const auto nc = rigBytes(noncanonical);
	expect(!applyModelEdit(&noncanonical, edit, nullptr, &error) && rigBytes(noncanonical) == nc,
		   "distinct imported offsets cannot silently change deformation");
	auto geometric = mesh;
	for (auto &w : geometric.surfaces[0].skinning.influences)
		w.normalOffset = {};
	expect(applyModelEdit(&geometric, edit, nullptr, &error), "paint supports geometric-normal bindings");
	for (const auto &w : geometric.surfaces[0].skinning.influences)
		expect(same(w.normalOffset, ModelVec3{}), "new influences preserve geometric normal policy");
	auto extra = edit;
	extra.kind = ModelEditKind::RecalculateNormals;
	candidate = mesh;
	expect(!applyModelEdit(&candidate, extra, nullptr, &error), "other edit kinds refuse brush settings");
	for (int threshold : {0, 10, 25})
	{
		candidate = mesh;
		int polls = 0;
		ModelWorkControl control;
		control.cancelled = [&] { return ++polls > threshold; };
		expect(!applyModelEdit(&candidate, edit, nullptr, &error, control) && rigBytes(candidate) == bytes,
			   "early and late cancellation leaves source unchanged");
	}
	const auto input = QDir(temporary.path()).filePath("input.mesh.json"), output = QDir(temporary.path()).filePath("painted.mesh.json");
	expect(document.setMesh(mesh, &error) && document.save(input, false, &error), "save source fixture");
	expect(document.setMesh(replaced, &error) && document.save(output, false, &error), "save painted mesh");
	ModelDocument reopened;
	expect(reopened.load(output, &error) && rigBytes(reopened.mesh()) == rigBytes(replaced),
		   "source round trip retains painted influences");
	const auto recovery = writeModelRecovery({replaced, {}}, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && rigBytes(restored.mesh) == rigBytes(replaced),
		   "painted state survives recovery");
	for (const auto &format : {QStringLiteral("md5mesh"), QStringLiteral("iqm"), QStringLiteral("md3")})
	{
		const auto encoded = exportEditableModel(replaced, format, 0, &error);
		const auto decoded = decodeModelMesh("painted." + format, encoded);
		expect(!encoded.isEmpty() && decoded.isValid(), "painted model exports to native formats");
		if (format == "iqm")
		{
			ModelFrameGeometry pose;
			expect(skinModelSurface(decoded.surfaces[0], decoded.skeleton.clips[0].frames[1], &pose, &error) &&
					   std::abs(pose.positions[0].y - 1.6) < .02,
				   "IQM weighted animation stays within quantisation error");
		}
	}
	const auto cliOutput = QDir(temporary.path()).filePath("cli.mesh.json");
	const QStringList command{"vibestudio", "--cli", "model", "weight-paint", input};
	const QStringList options{"--surface", "0",	  "--joint",	"tip", "--vertices", "0,1",
							  "--value",   "0.8", "--strength", "0.5", "--output",	 cliOutput};
	expect(cli::runModelWeightPaint(command + options + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(cliOutput),
		   "CLI dry run validates without writing");
	expect(cli::runModelWeightPaint(command + options).exitCode == 0 && reopened.load(cliOutput, &error) &&
			   close(modelVertexJointWeight(reopened.mesh().surfaces[0], 0, 1), .4),
		   "CLI writes analytic painted output");
	expect(cli::runModelWeightPaint(command + options).exitCode != 0, "CLI replacement requires explicit overwrite");
	for (const QStringList &invalid : {QStringList{"--surface", "0"},
									   {"--surface", "0", "--joint", "tip", "--vertices", "0", "--strength", "nan"},
									   {"--surface", "0", "--joint", "tip", "--vertices", "0", "--mode", "smooth", "--value", "1"},
									   {"--surface", "0", "--surface", "0"},
									   {"--unexpected"}})
		expect(cli::runModelWeightPaint(command + invalid + QStringList{"--output", cliOutput}).exitCode == 2,
			   "CLI rejects ambiguous and invalid options");
	const auto recipePath = QDir(temporary.path()).filePath("stroke.json");
	const auto recipeBytes =
		QByteArray(R"({"version":1,"strokes":[{"surface":0,"joint":"tip","value":0.8,"strength":0.5,"samples":[[0,1],[1,0.5]]}]})");
	expect(write(recipePath, recipeBytes), "write deterministic stroke recipe fixture");
	const QStringList replay = command + QStringList{"--stroke-file", recipePath, "--output", cliOutput, "--overwrite"};
	checkModelAuxiliaryOutput(replay, "--stroke-file", cli::runModelWeightPaint, expect);
	expect(cli::runModelWeightPaint(replay).exitCode == 0 && reopened.load(cliOutput, &error) &&
			   rigBytes(reopened.mesh()) == rigBytes(replaced),
		   "recipe replay equals core weighted samples exactly");
	for (const QByteArray &invalid :
		 {QByteArray(R"({"version":2,"strokes":[]})"),
		  QByteArray(R"({"version":1,"strokes":[{"surface":0,"joint":"tip","unknown":1,"samples":[[0,1]]}]})"),
		  QByteArray(R"({"version":1,"strokes":[{"surface":0,"joint":"tip","samples":[[0.5,1]]}]})"),
		  QByteArray(R"({"version":1,"strokes":[{"surface":0,"joint":"tip","strength":"0.5","samples":[[0,1]]}]})")})
	{
		write(recipePath, invalid);
		expect(cli::runModelWeightPaint(replay).exitCode == 2, "recipe rejects wrong versions, fields and types");
	}
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), (command + options + QStringList{"--overwrite", "--dry-run", "--json"}).mid(1));
		expect(process.waitForFinished(30000) && process.exitCode() == 0 && process.readAllStandardOutput().contains("dryRun"),
			   "actual executable routes weight painting CLI");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
