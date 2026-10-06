#include "tests/model_collision_animation_test_helpers.h"
#include "core/model_fingerprint.h"
#include "core/model_recovery.h"
#include "core/model_transform_axes.h"
#include "tests/model_mdl_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message, const QString &error = {})
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << ' ' << error.toStdString() << '\n';
	return value;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
bool near(ModelVec3 a, ModelVec3 b)
{
	return std::hypot(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z) < .0002;
}
bool write(const QString &path, const QByteArray &data)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
bool core(const QString &directory)
{
	bool ok = true;
	QString error;
	const auto original = tests::collisionAnimationFixture();
	const auto source = bytes(original);
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "admit mixed static/animated boxes", error);
	document.setSelection({0, {}, {}, {}, {}, "body"});
	const auto before = document.revisionFingerprint();
	ModelCollisionBox pose;
	pose.name = QStringLiteral("sentinel");
	ok &= expect(sampleModelCollisionBox(original.collisionBoxes[0], 0, 1, .5, &pose) && near(pose.centre, {10, 2, 4}) &&
					 near(pose.size, {20, 28, 36}) && near(modelCollisionAxes(pose)[0], {1, 0, 0}),
				 "linear centres/sizes and shortest rotation through zero, not 180 degrees");
	ok &= expect(sampleModelCollisionBox(original.collisionBoxes[0], 1, 2, 0, &pose) && pose.rotation.z == 10 && pose.framePoses.isEmpty(),
				 "endpoint sampling returns exact static pose");
	for (double amount : {-1., 2., std::numeric_limits<double>::quiet_NaN()})
		ok &= expect(!sampleModelCollisionBox(original.collisionBoxes[0], 0, 1, amount, &pose) && pose.rotation.z == 10,
					 "invalid interpolation refuses without changing output");
	ok &= expect(!sampleModelCollisionBox(original.collisionBoxes[0], 3, 0, 0, &pose), "missing pose refuses");
	ModelEdit edit;
	edit.kind = ModelEditKind::UpdateCollisionBox;
	edit.selection = document.selection();
	edit.collisionBox = {};
	edit.collisionBox.name = QStringLiteral("renamed");
	edit.collisionBox.centre = {42, 3, 6};
	edit.collisionFields = 1;
	edit.frame = 1;
	ok &= expect(document.edit(edit, &error), "edit and rename one collision pose", error);
	const auto &track = document.mesh().collisionBoxes[0];
	ok &= expect(track.name == "renamed" && track.framePoses[1].centre.x == 42 && track.framePoses[0].centre.x == 0 &&
					 track.framePoses[2].centre.x == 40 && track.framePoses[1].rotation.z == 10 &&
					 document.selection().collision == "renamed",
				 "partial current update preserves other values and poses");
	const auto after = document.revisionFingerprint();
	ok &= expect(after != before && document.undo() && document.revisionFingerprint() == before && document.redo() &&
					 document.revisionFingerprint() == after && document.selection().collision == "renamed",
				 "track history and selected name round trip");
	edit.selection = document.selection();
	edit.frame = -1;
	edit.collisionBox.name = "renamed";
	edit.collisionBox.size = {4, 6, 8};
	edit.collisionFields = 2;
	ok &= expect(document.edit(edit, &error) && document.mesh().collisionBoxes[0].framePoses[2].size.x == 4 &&
					 document.mesh().collisionBoxes[0].framePoses[2].centre.x == 40 &&
					 document.mesh().collisionBoxes[0].framePoses[1].centre.x == 42,
				 "partial all-frame update preserves animated centres", error);
	const auto good = document.revisionFingerprint();
	edit.collisionBox.size = {65536, 65536, 65536};
	ok &= expect(!document.edit(edit, &error) && !error.isEmpty() && document.revisionFingerprint() == good,
				 "invalid later pose rolls back all-frame change");
	edit = {};
	edit.kind = ModelEditKind::TransformCollisionBox;
	edit.selection.collision = "renamed";
	edit.frame = 2;
	edit.transformSpace = ModelTransformSpace::Selection;
	edit.translation = {2, 0, 0};
	edit.pivotMode = ModelTransformPivot::SelectionCentre;
	ok &= expect(document.edit(edit, &error) && near(document.mesh().collisionBoxes[0].framePoses[2].centre, {40, 10, 16}),
				 "selected collision axes come from edited pose", error);
	edit.frame = -1;
	edit.axesFrame = 2;
	edit.pivotFrame = 2;
	ok &= expect(document.edit(edit, &error) && near(document.mesh().collisionBoxes[0].framePoses[0].centre, {0, 2, 0}) &&
					 near(document.mesh().collisionBoxes[0].framePoses[2].centre, {40, 12, 16}),
				 "all poses use fixed reference axes", error);
	for (auto kind :
		 {ModelEditKind::DuplicateFrame, ModelEditKind::DeleteFrame, ModelEditKind::CopyFramePose, ModelEditKind::InsertInbetweens})
	{
		ModelDocument frames;
		frames.setMesh(original);
		ModelEdit operation;
		operation.kind = kind;
		operation.frame = kind == ModelEditKind::CopyFramePose ? 0 : 1;
		operation.sourceFrame = 2;
		operation.inbetweenCount = 3;
		if (kind == ModelEditKind::InsertInbetweens)
			operation.frame = 0;
		ok &= expect(frames.edit(operation, &error), "mesh frame operation carries collision track", error);
		const auto &poses = frames.mesh().collisionBoxes[0].framePoses;
		ok &= expect(poses.size() == frames.mesh().frames.size() && frames.mesh().collisionBoxes[1].framePoses.isEmpty() &&
						 frames.mesh().collisionBoxes[1].centre.x == -32,
					 "static boxes stay independent of pose operations");
		if (kind == ModelEditKind::DuplicateFrame)
			ok &= expect(poses.size() == 4 && poses[2].centre.x == 20 && poses[3].centre.x == 40, "duplicate remaps track");
		if (kind == ModelEditKind::DeleteFrame)
			ok &= expect(poses.size() == 2 && poses[1].centre.x == 40, "delete remaps track");
		if (kind == ModelEditKind::CopyFramePose)
			ok &= expect(poses[0].centre.x == 40 && frames.mesh().collisionBoxes[0].centre.x == 40, "copy maintains frame-zero invariant");
		if (kind == ModelEditKind::InsertInbetweens)
			ok &= expect(poses.size() == 6 && near(poses[2].centre, {10, 2, 4}) && std::abs(poses[2].rotation.z) < .0002,
						 "insert follows preview interpolation");
		ok &= expect(frames.undo() && bytes(frames.mesh()) == source && frames.redo(), "frame history restores entire track");
	}
	ModelDocument modes;
	modes.setMesh(original);
	edit = {};
	edit.selection.collision = "body";
	edit.kind = ModelEditKind::FreezeCollisionBox;
	edit.frame = 1;
	ok &= expect(modes.edit(edit, &error) && modes.mesh().collisionBoxes[0].framePoses.isEmpty() &&
					 modes.mesh().collisionBoxes[0].centre.x == 20,
				 "freeze keeps explicit pose", error);
	edit.kind = ModelEditKind::AnimateCollisionBox;
	edit.frame = -1;
	ok &= expect(modes.edit(edit, &error) && modes.mesh().collisionBoxes[0].framePoses.size() == 3 &&
					 modes.mesh().collisionBoxes[0].framePoses[2].centre.x == 20,
				 "animate copies static pose to every frame", error);
	edit.kind = ModelEditKind::FitAnimatedCollisionBox;
	edit.selection = {0, {0}, {}};
	edit.collisionBox.name = "fit";
	ok &= expect(modes.edit(edit, &error) && modes.mesh().collisionBoxes.last().framePoses.size() == 3 &&
					 modes.mesh().collisionBoxes.last().framePoses[2].centre.z == 16 &&
					 modes.mesh().collisionBoxes.last().framePoses[2].size.z == 1,
				 "animated component fit samples each pose and expands thin dimensions", error);
	ModelMesh parsed;
	ok &=
		expect(parseEditableModel(source, &parsed, &error) && bytes(parsed) == source && editableModelJson(parsed)["version"].toInt() == 7,
			   "version seven mixed tracks round trip", error);
	ModelDocument duplicates;
	duplicates.setMesh(original);
	edit = {};
	edit.kind = ModelEditKind::DuplicateCollisionBox;
	edit.selection.collision = "body";
	edit.collisionBox.name = "copy";
	ok &= expect(duplicates.edit(edit, &error) && duplicates.mesh().collisionBoxes.last().framePoses.size() == 3,
				 "duplicate copies complete track", error);
	edit.kind = ModelEditKind::UpdateCollisionBox;
	edit.selection.collision = "copy";
	edit.collisionFields = 1;
	edit.collisionBox.centre = {99, 0, 0};
	edit.frame = 2;
	ok &= expect(duplicates.edit(edit, &error) && duplicates.mesh().collisionBoxes[0].framePoses[2].centre.x == 40,
				 "editing a duplicate cannot mutate the original shared pose array", error);
	ModelDocument firstFrame;
	firstFrame.setMesh(original);
	edit = {};
	edit.kind = ModelEditKind::DeleteFrame;
	edit.frame = 0;
	ok &= expect(firstFrame.edit(edit, &error) && firstFrame.edit(edit, &error) &&
					 firstFrame.mesh().collisionBoxes[0].framePoses.size() == 1 && firstFrame.mesh().collisionBoxes[0].centre.x == 40 &&
					 !firstFrame.edit(edit, &error),
				 "delete frame zero retains animation mode down to one pose");
	for (const auto &format : {QStringLiteral("obj"), QStringLiteral("md2"), QStringLiteral("md3")})
	{
		auto native = original;
		if (format == "md2")
		{
			native.tags.clear();
			native.surfaces.resize(1);
			native.surfaces[0].skinPaths = {QStringLiteral("models/test.pcx")};
			updateEditableModelMetadata(&native);
		}
		ModelExportReport report;
		ok &= expect(!exportEditableModel(native, format, 0, &error, {}, &report).isEmpty() &&
						 report.notes.contains(modelCollisionOmissionNote()),
					 "native and OBJ export retain explicit omission warning for tracks", error);
	}
	ModelMesh mdl;
	ok &= expect(importEditableModel("fixture.mdl", tests::groupedMdlFixture().bytes, &mdl, &error), "MDL animation fixture", error);
	ModelCollisionBox mdlBox;
	mdlBox.name = QStringLiteral("mdl_box");
	setModelCollisionFrames(&mdlBox, QVector<ModelCollisionPose>(mdl.frames.size(), {{1, 2, 3}, {4, 6, 8}, {}}));
	mdl.collisionBoxes = {mdlBox};
	ModelExportReport mdlReport;
	ok &= expect(parseEditableModel(bytes(mdl), &parsed, &error) && bytes(parsed) == bytes(mdl) &&
					 !exportModelMdl(mdl, &error, {}, &mdlReport).isEmpty() && mdlReport.notes.contains(modelCollisionOmissionNote()),
				 "schema seven retains native MDL groups/skins while native output omits collision", error);
	for (int bad = 0; bad < 8; ++bad)
	{
		auto root = editableModelJson(original);
		auto boxes = root["collisionBoxes"].toArray();
		auto box = boxes[0].toObject();
		auto poses = box["poses"].toArray();
		if (bad == 0)
			root["version"] = 6;
		if (bad == 1)
			poses.removeLast();
		if (bad == 2)
			poses = {};
		if (bad == 3)
		{
			auto p = poses[1].toObject();
			p["size"] = QJsonArray{0, 1, 1};
			poses[1] = p;
		}
		if (bad == 4)
		{
			auto p = poses[2].toObject();
			p["unknown"] = true;
			poses[2] = p;
		}
		if (bad == 5)
			box["centre"] = QJsonArray{0, 0, 0};
		if (bad == 6)
		{
			auto p = poses[2].toObject();
			p["centre"] = QJsonArray{32768, 0, 0};
			poses[2] = p;
		}
		box["poses"] = poses;
		boxes[0] = box;
		root["collisionBoxes"] = boxes;
		if (bad == 7)
			root.remove("collisionBoxes");
		parsed = original;
		ok &= expect(!parseEditableModel(QJsonDocument(root).toJson(), &parsed, &error) && bytes(parsed) == source,
					 "malformed track source is atomic");
	}
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = document.mesh();
	snapshot.selection = document.selection();
	snapshot.frame = 2;
	const auto recovery = writeModelRecovery(snapshot, directory + "/recovery", QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() &&
					 bytes(restored.mesh) == bytes(snapshot.mesh) && restored.selection == snapshot.selection && restored.frame == 2,
				 "recovery retains tracks and selected frame", error);
	for (const auto &target : {QStringLiteral("quake"), QStringLiteral("quake2"), QStringLiteral("quake3")})
	{
		ModelCollisionExport request{target, target == "quake3" ? "common/clip" : QString(), {}};
		ModelCollisionMap output;
		output.bytes = "sentinel";
		ok &= expect(!exportModelCollisionMap(original, request, &output, &error) && output.bytes == "sentinel",
					 "animated map requires explicit pose");
		request.frame = 2;
		ok &= expect(exportModelCollisionMap(original, request, &output, &error) && output.brushCount == 2 &&
						 output.notes.join(' ').contains("stored frame 2"),
					 "map reports static sampled collision", error);
		auto frozen = original;
		for (auto &box : frozen.collisionBoxes)
			sampleModelCollisionBox(box, 2, 2, 0, &box);
		ModelCollisionMap reference;
		request.frame = -1;
		ok &= expect(exportModelCollisionMap(frozen, request, &reference, &error) && reference.bytes == output.bytes,
					 "track handoff equals exact explicit static pose");
		LevelMapDocument map;
		ok &= expect(loadLevelMapBytes({directory + "/empty.map", {}, target == "quake3" ? "idtech3" : "idtech2"},
									   "{\n\"classname\" \"worldspawn\"\n}\n", &map, &error),
					 "empty map");
		request.frame = 2;
		auto placement = prepareModelCollisionPlacement(original, request, map);
		ok &= expect(placement.succeeded && placement.document.brushes.size() == 2 && undoLevelMapEdit(&placement.document, &error) &&
						 placement.document.brushes.isEmpty() && redoLevelMapEdit(&placement.document, &error),
					 "sampled map placement is one undo step", placement.error);
	}
	return ok;
}
bool maximum()
{
	auto mesh = tests::collisionMaximumFixture();
	QElapsedTimer elapsed;
	elapsed.start();
	QString error;
	const auto source = bytes(mesh);
	ModelMesh parsed;
	bool ok = expect(validateEditableModel(mesh).isEmpty() && parseEditableModel(source, &parsed, &error) && bytes(parsed) == source,
					 "64 tracks by 1024 poses round trip", error);
	const auto duration = elapsed.elapsed();
	for (auto phase : {ModelWorkPhase::Validating, ModelWorkPhase::Serializing, ModelWorkPhase::Reading})
	{
		bool cancelled = false;
		ModelWorkControl control{[&] { return cancelled; },
								 [&](ModelWorkPhase p, qint64 completed, qint64) {
									 if (p == phase && completed >= 256)
										 cancelled = true;
								 }};
		if (phase == ModelWorkPhase::Validating)
			ok &= expect(!validateModelCollision(mesh, control).isEmpty() && cancelled, "large validation is cancellable");
		else if (phase == ModelWorkPhase::Serializing)
			ok &= expect(modelCollisionJson(mesh, &error, control).isEmpty() && cancelled, "large serialization is cancellable");
		else
		{
			auto sentinel = tests::collisionAnimationFixture();
			const auto before = bytes(sentinel);
			ok &= expect(!parseEditableModel(source, &sentinel, &error, control) && cancelled && bytes(sentinel) == before,
						 "large parser cancellation is atomic");
		}
	}
	std::cout << "Maximum collision tracks: 64 x 1024, source bytes=" << source.size() << ", roundtrip ms=" << duration << '\n';
	ModelDocument document;
	ok &= expect(document.setMesh(mesh, &error), "maximum tracks document", error);
	const auto before = document.revisionFingerprint();
	ModelEdit edit;
	edit.kind = ModelEditKind::UpdateCollisionBox;
	edit.selection.collision = "box_0";
	edit.collisionBox.name = "box_0";
	edit.collisionBox.size = {8, 10, 12};
	edit.collisionFields = 2;
	bool cancelled = false;
	ModelWorkControl cancelEdit{[&] { return cancelled; },
								[&](ModelWorkPhase p, qint64 completed, qint64) {
									if (p == ModelWorkPhase::Editing && completed >= 256)
										cancelled = true;
								}};
	ok &= expect(!document.edit(edit, &error, cancelEdit) && cancelled && document.revisionFingerprint() == before && !document.canUndo(),
				 "cancellation during track editing publishes neither poses nor history");
	ok &= expect(document.edit(edit, &error) && document.historyBytes() >= 64 * 1024 * qint64(sizeof(ModelCollisionPose)) &&
					 document.undo() && document.revisionFingerprint() == before,
				 "history accounts for every pose and undoes maximum-track edit", error);
	return ok;
}
bool cli(const QString &binary, const QString &directory)
{
	bool ok = true;
	QString error;
	const auto path = [&](const char *name) { return QDir(directory).filePath(QString::fromLatin1(name)); };
	ok &= expect(write(path("animated.mesh.json"), bytes(tests::collisionAnimationFixture())), "CLI fixture");
	QJsonObject result;
	const auto run = [&](QStringList arguments, int expected = 0) {
		QProcess process;
		process.setWorkingDirectory(directory);
		process.start(binary, QStringList{"--cli", "--settings-file", path("settings.ini"), "model", "collision", "animated.mesh.json"} +
								  arguments + QStringList{"--json"});
		if (!process.waitForFinished(30000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto output = process.readAllStandardOutput();
		result = QJsonDocument::fromJson(output).object();
		if (process.exitCode() != expected || result.isEmpty())
			std::cerr << output.toStdString() << process.readAllStandardError().toStdString();
		return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && !result.isEmpty();
	};
	ok &= expect(run({"--frame", "2"}) && result["sampledBoxes"].toArray()[0].toObject()["centre"].toArray()[0] == 40,
				 "inspect explicit pose retains complete JSON track");
	ok &= expect(run({"--operation", "update", "--box", "body", "--size", "8,10,12", "--output", "edit.mesh.json"}, 2) &&
					 !QFileInfo::exists(path("edit.mesh.json")),
				 "animated edit needs explicit scope");
	ok &= expect(run({"--operation", "update", "--box", "body", "--frame", "all", "--size", "8,10,12", "--output", "edit.mesh.json"}),
				 "CLI partial all-pose update");
	ModelDocument edited;
	ok &= expect(edited.load(path("edit.mesh.json"), &error) && edited.mesh().collisionBoxes[0].framePoses[2].centre.x == 40 &&
					 edited.mesh().collisionBoxes[0].framePoses[2].size.x == 8,
				 "CLI preserves unmentioned animated fields", error);
	ok &= expect(run({"--operation", "freeze", "--box", "body", "--output", "freeze.mesh.json"}, 2), "freeze requires pose");
	ok &= expect(run({"--operation", "freeze", "--box", "body", "--frame", "1", "--output", "freeze.mesh.json"}) &&
					 edited.load(path("freeze.mesh.json"), &error) && edited.mesh().collisionBoxes[0].framePoses.isEmpty() &&
					 edited.mesh().collisionBoxes[0].centre.x == 20,
				 "CLI freezes selected pose", error);
	ok &= expect(run({"--operation", "fit-animated", "--name", "fitted", "--vertices", "0", "--output", "fitted.mesh.json"}) &&
					 result["collisionBoxes"].toArray()[2].toObject()["poses"].toArray().size() == 3,
				 "CLI fits per-frame volumes");
	ok &= expect(run({"--operation", "animate", "--box", "static", "--output", "animate.mesh.json", "--dry-run"}) &&
					 !QFileInfo::exists(path("animate.mesh.json")) &&
					 result["collisionBoxes"].toArray()[1].toObject()["poses"].toArray().size() == 3,
				 "CLI animate dry run validates complete track");
	ok &= expect(run({"--operation", "export-map", "--target", "quake", "--output", "pose.map"}, 4) && !QFileInfo::exists(path("pose.map")),
				 "CLI map requires pose");
	ok &= expect(run({"--operation", "export-map", "--target", "quake", "--frame", "2", "--output", "pose.map"}) && result["frame"] == 2,
				 "CLI map reports chosen pose");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc < 2 || root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("collision-animation-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	bool ok = core(temporary.path());
	ok &= maximum();
	ok &= cli(QString::fromLocal8Bit(argv[1]), temporary.path());
	std::cout << checks << " animated collision checks: " << (ok ? "passed" : "failed") << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
