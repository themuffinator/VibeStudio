// The skeletal working set and the editable frame budget: oversized skeletal
// imports are re-baked to fit instead of refused, the working set changes as
// one undoable edit, and clips outside it stay editable through the skeletal
// services and exported in full.
#include "cli/model_tools.h"
#include "core/model_joint_tools.h"
#include "core/model_recovery.h"
#include "core/model_skeletal_export.h"
#include "core/model_skeletal_working_set.h"
#include "tests/model_rig_test_helpers.h"
#include "tests/model_skeletal_budget_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	++checks;
	if (!value)
	{
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty())
			std::cerr << ": " << qPrintable(detail);
		std::cerr << '\n';
		++failures;
	}
	return value;
}
qint64 frameVertices(const ModelMesh &mesh)
{
	qint64 vertices = 0;
	for (const auto &surface : mesh.surfaces)
		vertices += surface.vertexCount;
	return vertices * mesh.frames.size();
}
QVector<int> bakedClips(const ModelMesh &mesh)
{
	QVector<int> clips;
	for (int clip : mesh.skeleton.bakedClipForFrame)
		if (clip >= 0 && !clips.contains(clip))
			clips.append(clip);
	return clips;
}
QByteArray clipBytes(const ModelMesh &mesh, int clip)
{
	ModelMesh only;
	only.skeleton.joints = mesh.skeleton.joints;
	only.skeleton.clips = {mesh.skeleton.clips[clip]};
	return QJsonDocument(modelSkeletonJson(only).value("clips").toArray()).toJson(QJsonDocument::Compact);
}
bool samePositions(const QVector<ModelVec3> &a, const QVector<ModelVec3> &b)
{
	if (a.size() != b.size())
		return false;
	for (int i = 0; i < a.size(); ++i)
		if (std::abs(a[i].x - b[i].x) > 1e-4 || std::abs(a[i].y - b[i].y) > 1e-4 || std::abs(a[i].z - b[i].z) > 1e-4)
			return false;
	return true;
}
ModelEdit workingSet(QVector<int> clips)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::SetSkeletalWorkingSet;
	edit.skeletal.workingSet = std::move(clips);
	return edit;
}
bool writeFile(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QJsonObject runCli(const QString &program, const QStringList &arguments, int *exitCode)
{
	QProcess process;
	process.start(program, arguments);
	const bool finished = process.waitForStarted(10000) && process.waitForFinished(120000);
	*exitCode = finished ? process.exitCode() : -1;
	return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("working-set-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const QDir folder(temporary.path());
	QString error;

	// --- An oversized IQM import is re-baked to fit -----------------------
	// 1,200 vertices and two 500-frame clips: 1,001 frames with the bind pose,
	// which is under 1,024 frames but 1,201,200 frame vertices.
	const auto bindOnly = denseSkeletalFixture(1200, {{"walk", 500}, {"run", 500}}, {});
	if (!expect(validateEditableModel(bindOnly).isEmpty() && bindOnly.frames.size() == 1, "dense fixture holds only the bind pose"))
		return 1;
	const auto iqm = exportEditableModel(bindOnly, "iqm", 0, &error);
	expect(!iqm.isEmpty(), "dense fixture writes an IQM with every clip");
	const auto decoded = decodeModelMesh("dense.iqm", iqm);
	expect(decoded.frames.size() == 1001 && decoded.skeleton.clips.size() == 2, "the decoder bakes every clip");
	const auto refusals = validateEditableModel(decoded).join('\n');
	expect(refusals.contains("1,048,576 frame vertices"), "without a re-bake the decoded frames exceed the frame-vertex budget");

	ModelMesh imported;
	ModelImportReport report;
	expect(importEditableModel("dense.iqm", iqm, &imported, ModelImportOptions{}, &report, &error), "oversized import opens");
	expect(report.rebaked && imported.frames.size() == 501 && frameVertices(imported) <= modelSkeletalAutomaticBudget().frameVertices,
		   "import re-bakes within the automatic budget");
	expect(imported.skeleton.clips.size() == 2 && bakedClips(imported) == QVector<int>{0}, "the skeleton keeps both clips");
	expect(report.workingSet.names(ModelSkeletalClipState::OverBudget) == QStringList{"run"} &&
			   report.workingSet.names(ModelSkeletalClipState::Baked) == QStringList{"walk"},
		   "the receipt names what was baked and what was left out");
	const auto lines = modelSkeletalWorkingSetLines(report.workingSet).join('\n');
	std::cout << lines.toStdString() << '\n';
	expect(lines.contains("Left out to fit the budget: run.") && lines.contains("Frames used: 501 of 1024.") &&
			   lines.contains("Frame vertices used: 601200 of 1048576 (57%)."),
		   "the receipt explains the budget, measured against the whole editable budget");
	expect(validateEditableModel(imported).isEmpty(), "the re-baked import validates");
	const auto json = modelSkeletalWorkingSetJson(report.workingSet);
	expect(json["clipIndexes"].toArray() == QJsonArray{0} && json["clips"].toArray()[1].toObject()["state"].toString() == "over-budget" &&
			   json["budget"].toObject()["frameVertices"].toDouble() == 1048576 &&
			   report.workingSet.budget == modelSkeletalWorkingSet(imported).budget &&
			   report.workingSet.vertexRoom() == modelSkeletalWorkingSet(imported).vertexRoom(),
		   "the receipt JSON lists every clip with its state, against the budget every later view uses");

	ModelImportOptions choose;
	choose.chooseWorkingSet = true;
	choose.workingSet = {"run"};
	ModelMesh chosen;
	expect(importEditableModel("dense.iqm", iqm, &chosen, choose, &report, &error) && bakedClips(chosen) == QVector<int>{1} &&
			   chosen.frames.size() == 501,
		   "an import can choose its working set by name");
	choose.workingSet = {"1", "walk"};
	ModelMesh untouched = bindOnly;
	expect(!importEditableModel("dense.iqm", iqm, &untouched, choose, &report, &error) && error.contains("over the budget") &&
			   untouched.frames.size() == 1,
		   "an explicit working set over the budget is refused atomically", error);
	choose.workingSet = {"jump"};
	expect(!importEditableModel("dense.iqm", iqm, &untouched, choose, &report, &error) && error.contains("jump") && report.unresolvedClips,
		   "an unknown clip is refused by name, as a usage error");
	choose.workingSet = {"run"};
	expect(importEditableModel("dense.iqm", iqm, &chosen, choose, &report, &error) && report.chosen && !report.unresolvedClips,
		   "the receipt says the clips were chosen");
	choose.workingSet = {};
	expect(importEditableModel("dense.iqm", iqm, &chosen, choose, &report, &error) && chosen.frames.size() == 1 &&
			   chosen.skeleton.bakedClipForFrame == QVector<int>{-1},
		   "an empty working set keeps the bind pose alone");
	choose.workingSet = {"walk"};
	expect(!importEditableModel("prop.mesh.json", rigBytes(rigFixture()), &untouched, choose, &report, &error) && !error.isEmpty(),
		   "choosing clips applies only to skeletal imports");

	// A model within the budget keeps the decoder's frames.
	const auto smallIqm = exportEditableModel(rigFixture(), "iqm", 0, &error);
	ModelMesh small;
	expect(importEditableModel("small.iqm", smallIqm, &small, ModelImportOptions{}, &report, &error) && !report.rebaked &&
			   small.frames.size() == 3,
		   "a skeletal import within the budget is not re-baked");
	// More than 1,024 frames on a light mesh still fits by frame count.
	const auto longBind = denseSkeletalFixture(4, {{"long", 700}, {"longer", 700}}, {});
	ModelMesh longImport;
	expect(importEditableModel("long.iqm", exportEditableModel(longBind, "iqm", 0, &error), &longImport, ModelImportOptions{}, &report,
							   &error) &&
			   report.rebaked && longImport.frames.size() == 701 && bakedClips(longImport) == QVector<int>{0},
		   "an import over 1,024 frames keeps the clips that fit");

	// --- A 2,100-vertex MD5 character with a 600-frame animation ----------
	const auto character = denseSkeletalFixture(2100, {{"attack", 600}, {"idle", 40}}, {});
	ModelExportOptions md5Options;
	const auto meshPath = folder.filePath("character.md5mesh");
	expect(writeFile(meshPath, exportEditableModel(character, "md5mesh", md5Options, &error)), "write md5mesh");
	for (int clip = 0; clip < 2; ++clip)
	{
		md5Options.clip = clip;
		expect(writeFile(folder.filePath(modelMd5ClipFileName(character.skeleton, clip)), exportEditableModel(character, "md5anim", md5Options, &error)),
			   "write md5anim");
	}
	QFile meshFile(meshPath);
	expect(meshFile.open(QIODevice::ReadOnly), "read md5mesh");
	const auto md5Bytes = meshFile.readAll();
	const auto md5Decoded = decodeModelMesh(meshPath, md5Bytes, nullptr, {}, modelCompanionsFromFileSystem(meshPath));
	expect(md5Decoded.skeleton.clips.size() == 2 && !validateEditableModel(md5Decoded).isEmpty(),
		   "the loose MD5 character decodes with both animations and exceeds the budget");
	ModelMesh md5Imported;
	expect(importEditableModel(meshPath, md5Bytes, &md5Imported, ModelImportOptions{}, &report, &error), "the MD5 character opens");
	expect(report.workingSet.names(ModelSkeletalClipState::TooLarge) == QStringList{"attack"} && bakedClips(md5Imported) == QVector<int>{1} &&
			   md5Imported.frames.size() == 41,
		   "a clip too long for the budget stays in the skeleton, and the shorter clip is baked");
	expect(modelSkeletalWorkingSetLines(report.workingSet).join('\n').contains("edit them through the skeletal tools"),
		   "the receipt says how to edit a clip too long to bake");
	ModelEdit attack;
	attack.kind = ModelEditKind::TransformSkeletalPose;
	attack.skeletal.clip = 0;
	attack.skeletal.joint = "tip";
	attack.skeletal.first = attack.skeletal.last = 599;
	attack.skeletal.translation = {0, 3, 0};
	auto posedCharacter = md5Imported;
	expect(applyModelEdit(&posedCharacter, attack, nullptr, &error) && posedCharacter.frames.size() == 41 &&
			   std::abs(modelJointTranslation(posedCharacter.skeleton.clips[0].frames[599][1]).y -
						modelJointTranslation(md5Imported.skeleton.clips[0].frames[599][1]).y - 3) < 1e-4,
		   "a clip too long to bake is posed through the skeletal service");
	ModelExportOptions attackOptions;
	attackOptions.clip = 0;
	const auto attackAnim = exportEditableModel(posedCharacter, "md5anim", attackOptions, &error);
	const auto attackDecoded = decodeModelMesh("attack.md5anim", attackAnim);
	expect(!attackAnim.isEmpty() && attackDecoded.skeleton.clips.size() == 1 && attackDecoded.skeleton.clips[0].frames.size() == 600,
		   "the clip outside the working set exports as a whole md5anim");

	// --- Changing the working set ----------------------------------------
	ModelDocument doc;
	expect(doc.setMesh(imported, &error), "document admits the re-baked import");
	const auto before = rigBytes(doc.mesh());
	const auto walkBytes = clipBytes(doc.mesh(), 0), runBytes = clipBytes(doc.mesh(), 1);
	expect(doc.edit(workingSet({1}), &error), "switch the working set to the other clip");
	const auto &switched = doc.mesh();
	expect(bakedClips(switched) == QVector<int>{1} && switched.frames.size() == 501 && switched.skeleton.bakedClipForFrame[0] == -1,
		   "the chosen clip is baked after the bind frame");
	expect(clipBytes(switched, 0) == walkBytes && clipBytes(switched, 1) == runBytes, "both clips stay exact in the skeleton");
	ModelFrameGeometry expected;
	expect(skinModelSurface(switched.surfaces[0], switched.skeleton.clips[1].frames[123], &expected) &&
			   samePositions(switched.surfaces[0].frames[124].positions, expected.positions),
		   "newly baked frames follow their skeletal pose");
	expect(switched.animations.size() == 1 && switched.animations[0].name == "run" && switched.animations[0].firstFrame == 1 &&
			   switched.animations[0].frameCount == 500,
		   "the preview range follows the working set");
	const auto plan = modelSkeletalWorkingSet(switched);
	expect(plan.clips == QVector<int>{1} && plan.fits && plan.costs[0].state == ModelSkeletalClipState::Outside, "the working set reads back");
	expect(doc.undo() && rigBytes(doc.mesh()) == before, "undo restores the previous working set");
	expect(doc.redo() && bakedClips(doc.mesh()) == QVector<int>{1}, "redo applies the working set again");
	const auto switchedBytes = rigBytes(doc.mesh());
	expect(!doc.edit(workingSet({0, 1}), &error) && error.contains("over the budget") && rigBytes(doc.mesh()) == switchedBytes,
		   "an over-budget working set is refused atomically", error);
	expect(!doc.edit(workingSet({1, 1}), &error) && !doc.edit(workingSet({2}), &error), "repeated or missing clips are refused");
	ModelEdit stray = workingSet({1});
	stray.skeletal.clip = 0;
	expect(!doc.edit(stray, &error), "working-set edits take no clip index");
	ModelEdit misplaced;
	misplaced.kind = ModelEditKind::TransformSkeletalPose;
	misplaced.skeletal.clip = 1;
	misplaced.skeletal.joint = "tip";
	misplaced.skeletal.workingSet = {0};
	expect(!doc.edit(misplaced, &error), "other skeletal edits take no working set");

	// Clips outside the working set stay editable through the skeletal services.
	ModelEdit pose;
	pose.kind = ModelEditKind::TransformSkeletalPose;
	pose.skeletal.clip = 0;
	pose.skeletal.joint = "tip";
	pose.skeletal.first = pose.skeletal.last = 10;
	pose.skeletal.translation = {0, 2, 0};
	const auto walkBefore = doc.mesh().skeleton.clips[0].frames[10][1];
	expect(doc.edit(pose, &error) && doc.mesh().frames.size() == 501 &&
			   std::abs(modelJointTranslation(doc.mesh().skeleton.clips[0].frames[10][1]).y - modelJointTranslation(walkBefore).y - 2) < 1e-4,
		   "pose edits reach a clip outside the working set");
	ModelEdit ik;
	ik.kind = ModelEditKind::SolveTwoBoneIk;
	ik.ik.clip = 0;
	ik.ik.first = ik.ik.last = 20;
	ik.ik.endJoint = "hand";
	ik.ik.target = {500, 120, 0};
	ik.ik.pole = {300, 400, 0};
	ik.ik.nearestReachable = true;
	const auto handBefore = modelJointTranslation(doc.mesh().skeleton.clips[0].frames[20][2]);
	if (!doc.edit(ik, &error))
		std::cerr << error.toStdString() << '\n';
	const auto handAfter = modelJointTranslation(doc.mesh().skeleton.clips[0].frames[20][2]);
	expect(std::abs(handAfter.y - handBefore.y) > 1, "two-bone IK solves a clip outside the working set");
	ModelEdit curves;
	curves.kind = ModelEditKind::EditAnimationCurves;
	curves.curves.clip = 0;
	curves.curves.joint = "tip";
	curves.curves.action = ModelCurveAction::Capture;
	expect(doc.edit(curves, &error) && !doc.mesh().skeleton.clips[0].curves.isEmpty() && bakedClips(doc.mesh()) == QVector<int>{1},
		   "curves are captured on a clip outside the working set");
	const auto curvedWalk = clipBytes(doc.mesh(), 0);
	expect(doc.edit(workingSet({0}), &error) && bakedClips(doc.mesh()) == QVector<int>{0} && clipBytes(doc.mesh(), 0) == curvedWalk,
		   "an edited clip joins the working set with its poses and curves");
	expect(skinModelSurface(doc.mesh().surfaces[0], doc.mesh().skeleton.clips[0].frames[10], &expected) &&
			   samePositions(doc.mesh().surfaces[0].frames[11].positions, expected.positions),
		   "the joining clip shows the pose edited outside the working set");

	// Frames that hold their own data cannot leave the working set.
	auto dirty = doc.mesh();
	dirty.surfaces[0].frames[5].positions[3].z += 1;
	auto refused = dirty;
	expect(!applyModelEdit(&refused, workingSet({1}), nullptr, &error) && error.contains("independent geometry") &&
			   rigBytes(refused) == rigBytes(dirty),
		   "an independent frame edit blocks taking its clip out of the working set");
	dirty = doc.mesh();
	for (int frame = 0; frame < dirty.frames.size(); ++frame)
	{
		ModelTag tag;
		tag.name = "manual";
		tag.frameIndex = frame;
		tag.origin = {frame == 7 ? 9.0f : 1.0f, 0, 0};
		dirty.tags.append(tag);
	}
	expect(!applyModelEdit(&dirty, workingSet({1}), nullptr, &error) && error.contains("manual"),
		   "a manual attachment with its own pose blocks the change");
	dirty = doc.mesh();
	dirty.animations.append({"custom", 3, 4, 10});
	expect(!applyModelEdit(&dirty, workingSet({1}), nullptr, &error) && error.contains("custom"),
		   "a custom range over leaving frames blocks the change");
	dirty = doc.mesh();
	dirty.frames[9].origin = {1, 2, 3};
	expect(!applyModelEdit(&dirty, workingSet({1}), nullptr, &error) && error.contains("origin"), "a frame origin blocks the change");
	dirty = doc.mesh();
	for (int frame = 0; frame < dirty.frames.size(); ++frame)
	{
		ModelTag tag;
		tag.name = "manual";
		tag.frameIndex = frame;
		dirty.tags.append(tag);
	}
	expect(applyModelEdit(&dirty, workingSet({1}), nullptr, &error) &&
			   std::count_if(dirty.tags.cbegin(), dirty.tags.cend(), [](const ModelTag &tag) { return tag.name == "manual"; }) == dirty.frames.size(),
		   "manual attachments matching the bind frame follow the working set");

	// A joint-following attachment posed on its own in a leaving frame. The
	// small fixture keeps its skeletal tag, which IQM does not store.
	const auto two = denseSkeletalFixture(64, {{"a", 5}, {"b", 5}}, {0});
	dirty = two;
	int posed = 0;
	for (auto &tag : dirty.tags)
		if (tag.name == "tag_tip" && tag.frameIndex == 3 && ++posed)
			tag.origin.z += 5;
	expect(posed == 1 && !applyModelEdit(&dirty, workingSet({1}), nullptr, &error) && error.contains("tag_tip") && error.contains("own pose"),
		   "a joint-following attachment posed on its own blocks the change", error);
	dirty = two;
	expect(applyModelEdit(&dirty, workingSet({1}), nullptr, &error) &&
			   std::count_if(dirty.tags.cbegin(), dirty.tags.cend(), [](const ModelTag &tag) { return tag.name == "tag_tip"; }) == 6,
		   "a joint-following attachment that follows its joint moves with the working set", error);
	// A collision box posed on its own in a leaving frame.
	dirty = doc.mesh();
	{
		ModelCollisionBox box;
		box.name = "hit";
		box.size = {8, 8, 8};
		for (int frame = 0; frame < dirty.frames.size(); ++frame)
			box.framePoses.append(ModelCollisionPose{{0, 0, frame == 30 ? 4.0f : 0.0f}, {8, 8, 8}, {0, 0, 0}});
		dirty.collisionBoxes = {box};
	}
	auto boxes = dirty;
	expect(!applyModelEdit(&dirty, workingSet({1}), nullptr, &error) && error.contains("Collision box hit"),
		   "a collision box posed on its own blocks the change");
	boxes.collisionBoxes[0].framePoses[30].centre.z = 0;
	expect(applyModelEdit(&boxes, workingSet({1}), nullptr, &error) && boxes.collisionBoxes[0].framePoses.size() == boxes.frames.size(),
		   "a collision box that only follows the bind frame moves with the working set");
	// Quake MDL timing that groups two frames of a leaving clip.
	dirty = two;
	dirty.mdl.enabled = true;
	dirty.mdl.palette = QByteArray(768, '\0');
	for (int frame = 0; frame < dirty.frames.size(); ++frame)
		if (frame != 3)
			dirty.mdl.frameGroups.append(ModelMdlFrameGroup{frame, frame == 2 ? QVector<float>{0.1f, 0.2f} : QVector<float>{}});
	auto timed = dirty;
	expect(!applyModelEdit(&dirty, workingSet({1}), nullptr, &error) && error.contains("Quake MDL group timing"),
		   "Quake MDL group timing of a leaving frame blocks the change", error);
	timed.mdl.frameGroups = {};
	for (int frame = 0; frame < timed.frames.size(); ++frame)
		timed.mdl.frameGroups.append(ModelMdlFrameGroup{frame, {}});
	expect(applyModelEdit(&timed, workingSet({1}), nullptr, &error) && timed.mdl.frameGroups.size() == timed.frames.size(),
		   "single Quake MDL frames follow the working set", error);
	// A custom range spanning two clips that stay would grow if a clip joined
	// between them; one inside a single clip stays as it is.
	{
		auto three = denseSkeletalFixture(64, {{"idle", 40}, {"mid", 5}, {"tail", 5}}, {0, 2});
		three.animations.append({"combo", 38, 6, 0});
		auto grown = three;
		expect(!applyModelEdit(&grown, workingSet({0, 1, 2}), nullptr, &error) && error.contains("combo") && error.contains("mid") &&
				   rigBytes(grown) == rigBytes(three),
			   "a clip joining inside a custom range is refused, naming both");
		three.animations.last() = {"combo", 3, 6, 0};
		expect(applyModelEdit(&three, workingSet({0, 1, 2}), nullptr, &error) && bakedClips(three) == QVector<int>{0, 1, 2} &&
				   std::any_of(three.animations.cbegin(), three.animations.cend(),
							   [](const ModelAnimation &clip) { return clip.name == "combo" && clip.firstFrame == 3 && clip.frameCount == 6; }),
			   "a custom range inside one clip keeps its frames when another clip joins");
	}
	// A format whose bind pose is the first clip frame (Ghoul 2, MDR).
	{
		auto noBind = denseSkeletalFixture(64, {{"a", 5}, {"b", 5}}, {});
		ModelSkeletonBakeOptions options;
		options.byIndex = true;
		options.clipIndexes = {0};
		options.includeBindPose = false;
		expect(bakeModelSkeleton(&noBind, options) && noBind.skeleton.bakedClipForFrame == QVector<int>(5, 0), "bake a model without a bind frame");
		updateEditableModelMetadata(&noBind);
		expect(applyModelEdit(&noBind, workingSet({0, 1}), nullptr, &error) && noBind.frames.size() == 10 &&
				   noBind.skeleton.bakedClipForFrame == QVector<int>{0, 0, 0, 0, 0, 1, 1, 1, 1, 1},
			   "a clip joins a working set without a bind frame");
		expect(applyModelEdit(&noBind, workingSet({1}), nullptr, &error) && noBind.skeleton.bakedClipForFrame == QVector<int>(5, 1),
			   "the first clip can leave a working set without a bind frame");
		expect(applyModelEdit(&noBind, workingSet({}), nullptr, &error) && noBind.skeleton.bakedClipForFrame == QVector<int>{-1} &&
				   validateEditableModel(noBind).isEmpty(),
			   "an empty working set leaves an editable bind frame");
	}

	// Save, reopen and recovery keep the working set.
	const auto sourcePath = folder.filePath("dense.mesh.json");
	expect(doc.save(sourcePath, false, &error), "save the working set");
	ModelDocument reopened;
	expect(reopened.load(sourcePath, &error) && reopened.mesh().skeleton.bakedClipForFrame == doc.mesh().skeleton.bakedClipForFrame &&
			   reopened.mesh().skeleton.clips.size() == 2,
		   "reopening keeps the working set and every clip");
	ModelRecoverySnapshot snapshot;
	snapshot.mesh = doc.mesh();
	snapshot.title = "dense";
	const auto recovery = writeModelRecovery(snapshot, folder.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot recovered;
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &recovered).isValid() && rigBytes(recovered.mesh) == rigBytes(doc.mesh()),
		   "recovery keeps the working set");

	// Exports read the skeleton, so every clip is written.
	const auto exported = decodeModelMesh("exported.iqm", exportEditableModel(doc.mesh(), "iqm", 0, &error));
	expect(exported.skeleton.clips.size() == 2 && exported.skeleton.clips[1].frames.size() == 500, "IQM export writes every clip");
	// Vertex formats write only baked frames, and name the clips they leave out.
	ModelExportReport md3Report;
	expect(!exportEditableModel(two, "md3", ModelExportOptions{}, &error, {}, &md3Report).isEmpty() &&
			   std::any_of(md3Report.omissions.cbegin(), md3Report.omissions.cend(),
						   [](const QString &note) { return note.contains("not in the file: b.") && note.contains("Working Set"); }),
		   "an MD3 export names the clips outside the working set it cannot write", error);
	ModelExportOptions runOptions;
	runOptions.clip = 1;
	const auto runAnim = decodeModelMesh("run.md5anim", exportEditableModel(doc.mesh(), "md5anim", runOptions, &error));
	expect(runAnim.skeleton.clips.size() == 1 && runAnim.skeleton.clips[0].frames.size() == 500, "md5anim writes a clip outside the working set");

	// Tool previews skin clips outside the working set one sample at a time.
	int frame = -1;
	const auto shown = modelSkeletalSamplePreview(doc.mesh(), 1, 42, &frame);
	expect(frame == 0 && shown.frames.size() == 1 && skinModelSurface(doc.mesh().surfaces[0], doc.mesh().skeleton.clips[1].frames[42], &expected) &&
			   samePositions(shown.surfaces[0].frames[0].positions, expected.positions),
		   "a sample outside the working set previews in a one-frame copy");
	ModelJointFrame shownJoints;
	expect(modelJointFrame(shown, 0, &shownJoints) && shownJoints.pose.size() == 3, "the preview copy carries the sample's joint pose");
	const auto inside = modelSkeletalSamplePreview(doc.mesh(), 0, 42, &frame);
	expect(frame == 43 && inside.frames.size() == doc.mesh().frames.size(), "a working-set sample previews its baked frame");
	const auto missing = modelSkeletalSamplePreview(doc.mesh(), 1, 500, &frame);
	expect(frame == -1 && missing.frames.size() == doc.mesh().frames.size(), "a missing sample has no preview frame");

	// Automatic plans fill the budget in clip order and skip what does not fit.
	const auto three = denseSkeletalFixture(1000, {{"a", 400}, {"b", 500}, {"c", 300}, {"empty", 0}}, {0});
	const auto automatic = planModelSkeletalWorkingSet(three, nullptr, modelSkeletalAutomaticBudget());
	expect(automatic.clips == QVector<int>{0, 2} && automatic.costs[1].state == ModelSkeletalClipState::OverBudget &&
			   automatic.costs[3].state == ModelSkeletalClipState::Empty && automatic.frames == 701,
		   "automatic plans skip a clip that does not fit and keep later ones");
	expect(automatic.vertexRoom() == 786432 / 701 - 1000 && automatic.changes(), "plans report room and change");

	// --- CLI parity --------------------------------------------------------
	const auto run = [&](QStringList args) { return cli::runModelPose(QStringList{"vibestudio", "--cli", "model", "pose", sourcePath} + args); };
	const auto listing = run({"--json"});
	expect(listing.exitCode == 0 && listing.payload["workingSet"].toObject()["clipIndexes"].toArray() == QJsonArray{0} &&
			   listing.payload["clips"].toArray()[1].toObject()["inWorkingSet"].toBool() == false,
		   "pose listing reports the working set");
	expect(listing.lines.join('\n').contains("Skeletal clips baked into editable frames: 1 of 2."), "pose listing explains the working set");
	std::cout << listing.lines.join('\n').toStdString() << '\n';
	const auto output = folder.filePath("run.mesh.json");
	const auto chosenRun = run({"--operation", "working-set", "--clips", "run", "--output", output, "--json"});
	ModelDocument written;
	expect(chosenRun.exitCode == 0 && written.load(output, &error) && bakedClips(written.mesh()) == QVector<int>{1} &&
			   chosenRun.payload["workingSet"].toObject()["clipIndexes"].toArray() == QJsonArray{1},
		   "CLI changes the working set through the shared edit");
	expect(run({"--operation", "working-set", "--clips", "walk,1", "--output", output, "--overwrite"}).exitCode == 4,
		   "CLI refuses an over-budget working set");
	expect(run({"--operation", "working-set", "--clips", "jump", "--output", output, "--overwrite"}).exitCode == 2, "CLI refuses an unknown clip");
	expect(run({"--clips", "run"}).exitCode == 2, "--clips applies only to working-set");
	expect(run({"--operation", "working-set", "--clips", "-", "--output", output, "--overwrite"}).exitCode == 0 && written.load(output, &error) &&
			   written.mesh().frames.size() == 1,
		   "CLI keeps the bind pose alone");
	expect(run({"--operation", "working-set", "--output", output, "--overwrite"}).exitCode == 0 && written.load(output, &error) &&
			   bakedClips(written.mesh()) == QVector<int>{0},
		   "CLI fits the working set automatically without --clips");
	if (argc > 1)
	{
		const auto program = QString::fromLocal8Bit(argv[1]);
		const auto iqmPath = folder.filePath("dense.iqm");
		expect(writeFile(iqmPath, iqm), "write the dense IQM");
		int code = -1;
		const auto receipt = runCli(program, {"--cli", "model", "import", iqmPath, "--output", folder.filePath("imported.mesh.json"), "--json"}, &code);
		expect(code == 0 && receipt["rebaked"].toBool() && receipt["frames"].toInt() == 501 &&
				   receipt["workingSet"].toObject()["clips"].toArray()[1].toObject()["state"].toString() == "over-budget",
			   "model import re-bakes an oversized skeletal model and reports it");
		const auto picked = runCli(program, {"--cli", "model", "import", "--clips", "run", iqmPath, "--output", folder.filePath("picked.mesh.json"), "--json"}, &code);
		expect(code == 0 && picked["workingSet"].toObject()["clipIndexes"].toArray() == QJsonArray{1}, "model import --clips chooses the working set");
		runCli(program, {"--cli", "model", "import", iqmPath, "--clips", "jump", "--output", folder.filePath("bad.mesh.json"), "--json"}, &code);
		expect(code == 2 && !QFile::exists(folder.filePath("bad.mesh.json")),
			   "model import refuses an unknown clip as a usage error, as model pose does, and writes nothing");
		// A model named relative to the working folder still finds its loose
		// md5anim companions.
		const auto relative = [&](const QStringList &arguments, int *exitCode, QByteArray *text = nullptr)
		{
			QProcess process;
			process.setWorkingDirectory(folder.path());
			process.start(program, arguments);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(120000);
			*exitCode = finished ? process.exitCode() : -1;
			const auto output = process.readAllStandardOutput();
			if (text)
				*text = output;
			return QJsonDocument::fromJson(output).object();
		};
		const auto fitted = relative({"--cli", "model", "import", "./character.md5mesh", "--output", "fitted.mesh.json", "--json"}, &code);
		expect(code == 0 && fitted["frames"].toInt() == 41 && fitted["rebaked"].toBool() &&
				   fitted["workingSet"].toObject()["clips"].toArray().size() == 2 &&
				   fitted["workingSet"].toObject()["clips"].toArray()[0].toObject()["state"].toString() == "too-large",
			   "model import of a relative md5mesh loads its animations and fits them");
		const auto chosenIdle =
			relative({"--cli", "model", "import", "./character.md5mesh", "--clips", "idle", "--output", "idle.mesh.json", "--json"}, &code);
		expect(code == 0 && chosenIdle["frames"].toInt() == 41 && chosenIdle["workingSet"].toObject()["clipIndexes"].toArray() == QJsonArray{1},
			   "model import --clips names a clip of a relative md5mesh");
		QByteArray text;
		relative({"--cli", "model", "import", "character.md5mesh", "--clips", "idle", "--output", "idle-text.mesh.json"}, &code, &text);
		expect(code == 0 && text.contains("Baked the clips chosen with --clips") && !text.contains("to fit the editable budget") &&
				   text.contains("Vertices mesh tools can still add to every frame:") && !text.contains("(ex)"),
			   "the import text says the clips were chosen, and its counts read without plural markup");
	}

	// --- Latency at the budget ---------------------------------------------
	// The largest working set an automatic choice fills: 768 frames of 1,024
	// vertices, re-baked from an import and then switched to other clips.
	const auto large = denseSkeletalFixture(1024, {{"one", 383}, {"two", 384}, {"three", 383}, {"four", 384}}, {});
	QElapsedTimer timer;
	timer.start();
	ModelMesh fitted;
	expect(importEditableModel("large.iqm", exportEditableModel(large, "iqm", 0, &error), &fitted, ModelImportOptions{}, &report, &error) &&
			   fitted.frames.size() == 768 && frameVertices(fitted) == 786432,
		   "the automatic budget is filled exactly");
	const auto importMs = timer.restart();
	ModelDocument latency;
	expect(latency.setMesh(fitted, &error) && latency.edit(workingSet({2, 3}), &error) && latency.mesh().frames.size() == 768,
		   "switching a full working set");
	const auto switchMs = timer.elapsed();
	std::cout << "fit " << importMs << " ms, switch " << switchMs << " ms\n";
	expect(importMs < 60000 && switchMs < 60000, "working-set re-bakes at the budget finish within a minute");

	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
