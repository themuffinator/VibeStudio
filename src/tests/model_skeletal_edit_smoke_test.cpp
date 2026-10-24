#include "cli/model_tools.h"
#include "core/model_collision.h"
#include "core/model_recovery.h"
#include "core/model_tags.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_rig_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return value;
}
bool near(double a, double b)
{
	return std::abs(a - b) < .0002;
}
ModelEdit operation(ModelEditKind kind, int clip = -1)
{
	ModelEdit result;
	result.kind = kind;
	result.skeletal.clip = clip;
	return result;
}
ModelMesh fixture()
{
	auto mesh = rigFixture();
	ModelEdit weight;
	weight.kind = ModelEditKind::SetJointWeight;
	weight.rig.joint = "tip";
	weight.selection.vertices = {1};
	if (!applyModelEdit(&mesh, weight))
		return {};
	for (int frame = 0; frame < mesh.frames.size(); ++frame)
	{
		ModelTag tag;
		tag.name = "manual";
		tag.frameIndex = frame;
		tag.origin = {float(100 + frame), 0, 0};
		mesh.tags.append(tag);
	}
	ModelCollisionBox box;
	box.name = "body";
	setModelCollisionFrames(&box, {{{0, 0, 0}, {4, 4, 4}, {}}, {{1, 0, 0}, {4, 4, 4}, {}}, {{2, 0, 0}, {4, 4, 4}, {}}});
	mesh.collisionBoxes = {box};
	mesh.frames[1].name = "kept";
	mesh.frames[2].origin = {2, 3, 4};
	mesh.animations.append({"custom", 1, 2, 7});
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("skeletal-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	QString error;
	const auto original = fixture();
	if (!expect(validateEditableModel(original).isEmpty(), "fixture validates"))
		return 1;
	ModelDocument doc;
	expect(doc.setMesh(original, &error), "admit fixture");
	const auto apply = [&](const ModelEdit &edit, const char *message) {
		const bool ok = doc.edit(edit, &error);
		if (!ok)
			std::cerr << error.toStdString() << '\n';
		return expect(ok, message);
	};
	const auto reject = [&](const ModelMesh &source, const ModelEdit &edit, const char *message) {
		auto target = source;
		const auto bytes = rigBytes(target);
		ModelSelection selected;
		selected.vertices = {2};
		const auto oldSelection = selected;
		expect(!applyModelEdit(&target, edit, &selected, &error) && !error.isEmpty() && rigBytes(target) == bytes &&
				   selected == oldSelection,
			   message);
	};
	auto edit = operation(ModelEditKind::TransformBindJoint);
	edit.skeletal.joint = "tip";
	edit.skeletal.translation = {2, 0, 0};
	apply(edit, "move bind pivot");
	expect(near(doc.mesh().skeleton.joints[1].bind.m[3], 10) && near(doc.mesh().skeleton.clips[0].frames[1][1].m[3], 10),
		   "bind pivot rebased through animation");
	const auto &skinning = doc.mesh().surfaces[0].skinning;
	expect(near(skinning.influences[skinning.first[1]].offset.x, -2), "influence rebased into moved pivot");
	for (int frame = 0; frame < 3; ++frame)
	{
		expect(near(doc.mesh().surfaces[0].frames[frame].positions[1].x, original.surfaces[0].frames[frame].positions[1].x) &&
				   near(doc.mesh().surfaces[0].frames[frame].positions[1].y, original.surfaces[0].frames[frame].positions[1].y),
			   "bind edit preserves deformation");
		expect(near(findModelTag(doc.mesh(), "tag_tip", frame)->origin.x, findModelTag(original, "tag_tip", frame)->origin.x),
			   "bind edit preserves attachment");
	}
	expect(doc.undo() && rigBytes(doc.mesh()) == rigBytes(original) && doc.redo(), "bind edit undo and redo");
	doc.setMesh(original, &error);
	edit.skeletal.joint = "root";
	edit.skeletal.rotation = {10, 20, 30};
	edit.skeletal.scale = {1.5f, .75f, 2};
	apply(edit, "rebase rotated and scaled bind branch");
	expect(near(doc.mesh().surfaces[0].frames[2].positions[1].x, 8) && near(doc.mesh().surfaces[0].frames[2].positions[1].y, 4),
		   "branch compensation preserves skinned animation");
	const auto scaledRoundTrip = decodeModelMesh("scaled.iqm", exportEditableModel(doc.mesh(), "iqm", 0, &error));
	expect(scaledRoundTrip.isValid() && near(scaledRoundTrip.surfaces[0].frames[0].normals[0].z, 1),
		   "IQM rotated scaled bind retains bind normal direction");
	auto rotated = original;
	edit.skeletal.scale = {1, 1, 1};
	expect(applyModelEdit(&rotated, edit, nullptr, &error), "rebase rotated bind for MD5");
	const auto rotatedRoundTrip = decodeModelMesh("rotated.md5mesh", exportEditableModel(rotated, "md5mesh", 0, &error));
	expect(rotatedRoundTrip.isValid() && near(rotatedRoundTrip.surfaces[0].frames[0].normals[0].z, 1),
		   "MD5 rotated bind retains geometric normal direction");
	auto omitted = original;
	auto retainedClip = omitted.skeleton.clips[0];
	retainedClip.name = "hidden";
	omitted.skeleton.clips.append(retainedClip);
	auto duplicateHidden = operation(ModelEditKind::DuplicateSkeletalClip, 1);
	duplicateHidden.skeletal.name = "visible_copy";
	expect(applyModelEdit(&omitted, duplicateHidden, nullptr, &error) && omitted.frames.size() == 5 &&
			   omitted.skeleton.bakedClipForFrame.last() == 2,
		   "duplicate materialises a retained clip outside the preview budget");
	expect(near(omitted.surfaces[0].frames[4].positions[1].y, 4) && near(findModelTag(omitted, "manual", 4)->origin.x, 100),
		   "unbaked duplicate skins its pose and explicitly inherits first-frame auxiliary data");
	auto noBind = rigFixture();
	ModelSkeletonBakeOptions noBindOptions;
	noBindOptions.includeBindPose = false;
	expect(bakeModelSkeleton(&noBind, noBindOptions, &error), "fixture without explicit bind frame");
	auto removeOnly = operation(ModelEditKind::DeleteSkeletalClip, 0);
	expect(applyModelEdit(&noBind, removeOnly, nullptr, &error) && noBind.frames.size() == 1 && noBind.skeleton.clips.isEmpty() &&
			   noBind.skeleton.bakedClipForFrame[0] == -1,
		   "delete only clip retains an editable bind pose");
	doc.setMesh(original, &error);
	edit = operation(ModelEditKind::AddSkeletalClip);
	edit.skeletal.name = "turn";
	edit.skeletal.frameCount = 3;
	edit.skeletal.rate = 20;
	edit.skeletal.loops = false;
	apply(edit, "create clip from bind");
	expect(doc.mesh().frames.size() == 6 && doc.mesh().skeleton.clips.size() == 2 && doc.mesh().animations.last().firstFrame == 3,
		   "created clip is present in preview");
	expect(doc.mesh().frames[1].name == "kept" && near(doc.mesh().frames[2].origin.y, 3), "existing frame metadata retained");
	expect(near(findModelTag(doc.mesh(), "manual", 5)->origin.x, 100) && near(doc.mesh().collisionBoxes[0].framePoses[5].centre.x, 0),
		   "new clip inherits bind auxiliary tracks");
	edit = operation(ModelEditKind::TransformSkeletalPose, 1);
	edit.skeletal.joint = "root";
	edit.skeletal.first = edit.skeletal.last = 2;
	edit.skeletal.rotation = {0, 0, 90};
	apply(edit, "pose root and descendants");
	expect(near(doc.mesh().surfaces[0].frames[5].positions[1].x, 0) && near(doc.mesh().surfaces[0].frames[5].positions[1].y, 8),
		   "rotated child influence has analytic position");
	expect(near(findModelTag(doc.mesh(), "tag_tip", 5)->origin.y, 8) && near(findModelTag(doc.mesh(), "manual", 5)->origin.x, 100),
		   "skeletal attachment follows, manual attachment stays");
	edit = operation(ModelEditKind::InterpolateSkeletalPoses, 1);
	edit.skeletal.last = 2;
	apply(edit, "interpolate skeletal poses");
	expect(near(doc.mesh().surfaces[0].frames[4].positions[1].x, std::sqrt(32.0)) &&
			   near(doc.mesh().surfaces[0].frames[4].positions[1].y, std::sqrt(32.0)),
		   "midpoint follows rotational arc, not vertex chord");
	expect(near(modelJointTranslation(doc.mesh().skeleton.clips[1].frames[1][1]).x, std::sqrt(32.0)),
		   "child local offset preserved by interpolation");
	const auto animated = doc.mesh();
	auto nativeMdl = decodeModelMesh("synthetic.mdl", groupedMdlFixture().bytes);
	auto mdl = original;
	mdl.mdl = nativeMdl.mdl;
	mdl.embeddedSkins = nativeMdl.embeddedSkins;
	auto mdlEdit = operation(ModelEditKind::AddSkeletalClip);
	mdlEdit.skeletal.name = "mdl_anim";
	mdlEdit.skeletal.frameCount = 2;
	expect(validateEditableModel(mdl).isEmpty() && applyModelEdit(&mdl, mdlEdit, nullptr, &error),
		   "skeletal authoring retains native MDL metadata");
	expect(mdl.mdl.frameGroups.size() == 4 && mdl.mdl.frameGroups[0].intervals == nativeMdl.mdl.frameGroups[0].intervals &&
			   mdl.mdl.frameGroups.last().intervals.size() == 1 && near(mdl.mdl.frameGroups.last().intervals[0], .1),
		   "MDL existing groups and inherited sample duration retained");
	edit = operation(ModelEditKind::TransformSkeletalPose, 1);
	edit.skeletal.joint = "root";
	edit.skeletal.first = edit.skeletal.last = 2;
	edit.skeletal.translation = {2, 0, 0};
	edit.skeletal.local = true;
	apply(edit, "local pose translation");
	expect(near(doc.mesh().skeleton.clips[1].frames[2][0].m[3], 0) && near(doc.mesh().skeleton.clips[1].frames[2][0].m[7], 2),
		   "local translation follows rotated axes");
	expect(doc.undo() && rigBytes(doc.mesh()) == rigBytes(animated), "undo local transform");
	edit.skeletal.local = false;
	edit.skeletal.descendants = false;
	edit.skeletal.translation = {2, 0, 0};
	apply(edit, "model pose translation without descendants");
	expect(near(doc.mesh().skeleton.clips[1].frames[2][0].m[3], 2) && near(doc.mesh().skeleton.clips[1].frames[2][1].m[3], 0),
		   "child model-space pose stays when propagation disabled");
	doc.undo();
	edit = operation(ModelEditKind::ResizeSkeletalClip, 0);
	edit.skeletal.frameCount = 4;
	apply(edit, "extend first clip");
	expect(doc.mesh().frames.size() == 8 && doc.mesh().animations.last().firstFrame == 5 &&
			   near(doc.mesh().surfaces[0].frames[4].positions[1].y, 4),
		   "following ranges shift and added poses hold last");
	expect(near(findModelTag(doc.mesh(), "manual", 4)->origin.x, 102) && near(doc.mesh().collisionBoxes[0].framePoses[4].centre.x, 2),
		   "extended poses inherit final auxiliary tracks");
	expect(doc.mesh().animations[1].name == "custom" && doc.mesh().animations[1].frameCount == 2, "custom subrange preserved");
	edit.skeletal.frameCount = 1;
	apply(edit, "shrink clip tail");
	expect(doc.mesh().frames.size() == 5 && doc.mesh().animations[1].frameCount == 1 && doc.mesh().animations.last().firstFrame == 2,
		   "custom range shrinks and following range remaps");
	edit = operation(ModelEditKind::DuplicateSkeletalClip, 1);
	edit.skeletal.name = "turn_copy";
	apply(edit, "duplicate clip");
	expect(doc.mesh().frames.size() == 8 && near(doc.mesh().surfaces[0].frames[6].positions[1].x, std::sqrt(32.0)),
		   "duplicate preserves poses");
	edit = operation(ModelEditKind::RenameSkeletalClip, 2);
	edit.skeletal.name = "swing";
	apply(edit, "rename clip");
	expect(doc.mesh().animations.last().name == "swing", "matching preview range follows name");
	edit = operation(ModelEditKind::SetSkeletalClipTiming, 2);
	edit.skeletal.rate = 30;
	edit.skeletal.loops = false;
	apply(edit, "set clip timing");
	expect(near(doc.mesh().animations.last().framesPerSecond, 30) && !doc.mesh().skeleton.clips[2].loops,
		   "timing shared by preview and retained clip");
	edit = operation(ModelEditKind::CopySkeletalPose, 2);
	edit.skeletal.last = 2;
	edit.skeletal.sourceClip = 1;
	edit.skeletal.sourceFrame = 1;
	apply(edit, "copy whole skeletal pose across range");
	expect(near(doc.mesh().surfaces[0].frames[5].positions[1].x, std::sqrt(32.0)) &&
			   near(doc.mesh().surfaces[0].frames[7].positions[1].y, std::sqrt(32.0)),
		   "all copied samples match source pose");
	edit.skeletal.sourceClip = -1;
	edit.skeletal.sourceFrame = 0;
	apply(edit, "reset pose range to bind");
	expect(near(doc.mesh().surfaces[0].frames[7].positions[1].y, 0), "reset uses bind matrices");
	edit = operation(ModelEditKind::DeleteSkeletalClip, 2);
	apply(edit, "delete duplicate clip");
	expect(doc.mesh().skeleton.clips.size() == 2 && doc.mesh().frames.size() == 5, "clip and preview frames deleted together");
	edit = operation(ModelEditKind::DeleteSkeletalClip, 0);
	reject(doc.mesh(), edit, "delete protects custom range from emptying");
	const auto path = QDir(temporary.path()).filePath("animation.mesh.json");
	expect(doc.save(path, false, &error), "save authored animation");
	ModelDocument reopened;
	expect(reopened.load(path, &error) && rigBytes(reopened.mesh()) == rigBytes(doc.mesh()), "save/reopen retains exact authoring state");
	ModelRecoverySnapshot snapshot{doc.mesh(), doc.selection()}, recovered;
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &recovered).isValid() && rigBytes(recovered.mesh) == rigBytes(doc.mesh()),
		   "recovery retains animation authoring");
	const auto iqm = exportEditableModel(animated, "iqm", 0, &error);
	const auto decoded = decodeModelMesh("authored.iqm", iqm);
	expect(!iqm.isEmpty() && decoded.isValid() && decoded.skeleton.clips.size() == 2, "IQM exports authored clips");
	if (decoded.skeleton.clips.size() == 2)
	{
		expect(!decoded.skeleton.clips[1].loops && near(decoded.skeleton.clips[1].framesPerSecond, 20), "IQM keeps loop and rate metadata");
		ModelFrameGeometry geometry;
		expect(skinModelSurface(decoded.surfaces[0], decoded.skeleton.clips[1].frames[1], &geometry, &error) &&
				   std::abs(geometry.positions[1].x - std::sqrt(32.0)) < .001,
			   "IQM interpolated pose round trip matches arc");
	}
	expect(!exportEditableModel(animated, "md5anim", 4, &error).isEmpty(), "MD5 animation exports authored clip");
	expect(!exportEditableModel(animated, "md3", 0, &error).isEmpty(), "baked animation exports MD3");
	edit = operation(ModelEditKind::TransformSkeletalPose, 1);
	edit.skeletal.joint = "root";
	edit.skeletal.first = edit.skeletal.last = 2;
	edit.skeletal.translation = {1, 0, 0};
	auto dirty = animated;
	dirty.surfaces[0].frames[5].positions[0].z = 3;
	reject(dirty, edit, "pose edit protects independently edited geometry");
	dirty = animated;
	dirty.tags[0].origin.z = 9;
	expect(applyModelEdit(&dirty, edit, nullptr, &error) && near(dirty.tags[0].origin.z, 9), "unaffected independent tag pose preserved");
	dirty = animated;
	for (auto &tag : dirty.tags)
		if (tag.name == "tag_tip" && tag.frameIndex == 5)
			tag.origin.z = 9;
	reject(dirty, edit, "pose edit protects affected independent attachment");
	dirty = animated;
	dirty.surfaces[0].frames[0].positions[0].z = 3;
	expect(applyModelEdit(&dirty, edit, nullptr, &error) && near(dirty.surfaces[0].frames[0].positions[0].z, 3),
		   "unaffected independent frame edit preserved");
	dirty = animated;
	dirty.skeleton.bakedClipForFrame[2] = -2;
	reject(dirty, edit, "independent frame layout refused");
	dirty = animated;
	dirty.skeleton.joints[1].parent = 2;
	reject(dirty, edit, "invalid hierarchy refused before indexing");
	dirty = animated;
	dirty.surfaces[0].skinning.influences[0].joint = 99;
	reject(dirty, edit, "invalid influence refused before indexing");
	dirty = animated;
	dirty.skeleton.clips[1].frames[2].clear();
	reject(dirty, edit, "incomplete pose refused before indexing");
	auto tiny = animated;
	edit.skeletal.translation = {0.00001f, 0, 0};
	const float oldTiny = tiny.surfaces[0].frames[5].positions[0].x;
	expect(applyModelEdit(&tiny, edit, nullptr, &error) && tiny.surfaces[0].frames[5].positions[0].x > oldTiny,
		   "small intentional transforms update baked geometry");
	edit.skeletal.translation = {1, 0, 0};
	edit.skeletal.scale.x = 0;
	reject(animated, edit, "zero scale refused");
	edit.skeletal.scale.x = std::numeric_limits<float>::quiet_NaN();
	reject(animated, edit, "nonfinite scale refused");
	edit.skeletal.scale.x = 1;
	edit.skeletal.joint = "Root";
	reject(animated, edit, "joint names match exactly");
	edit = operation(ModelEditKind::InterpolateSkeletalPoses, 1);
	edit.skeletal.last = 1;
	reject(animated, edit, "interpolation requires interior sample");
	edit.skeletal.last = 2;
	dirty = animated;
	dirty.skeleton.clips[1].frames[2][0].m[0] += .3f;
	reject(dirty, edit, "interpolation refuses shear");
	edit = operation(ModelEditKind::SetSkeletalClipTiming, 0);
	edit.skeletal.rate = 0;
	reject(animated, edit, "authored timing must be positive");
	edit = operation(ModelEditKind::RenameSkeletalClip, 1);
	edit.skeletal.name = "LIFT";
	reject(animated, edit, "case insensitive duplicate names refused");
	edit.skeletal.name = "../walk";
	reject(animated, edit, "unsafe new clip name refused");
	edit = operation(ModelEditKind::AddSkeletalClip);
	edit.skeletal.name = "large";
	edit.skeletal.frameCount = 1024;
	reject(animated, edit, "frame budget enforced atomically");
	edit = operation(ModelEditKind::DeleteSkeletalClip, 99);
	reject(animated, edit, "invalid clip index refused");
	edit = operation(ModelEditKind::RenameFrame);
	edit.frame = 0;
	edit.text = "renamed";
	edit.skeletal.clip = 0;
	reject(animated, edit, "skeletal options refused for unrelated operation");
	edit = operation(ModelEditKind::TransformSkeletalPose, 1);
	edit.skeletal.joint = "root";
	edit.skeletal.translation = {1, 0, 0};
	auto cancelled = animated;
	const auto cancelBefore = rigBytes(cancelled);
	int polls = 0;
	ModelWorkControl control;
	control.cancelled = [&] { return ++polls > 5; };
	expect(!applyModelEdit(&cancelled, edit, nullptr, &error, control) && rigBytes(cancelled) == cancelBefore, "cancellation atomic");
	const auto output = QDir(temporary.path()).filePath("out.mesh.json");
	const auto run = [&](QStringList args) { return cli::runModelPose(QStringList{"vibestudio", "--cli", "model", "pose", path} + args); };
	expect(run({"--json"}).payload["clips"].toArray().size() == 2, "CLI lists clips");
	const auto inspected = run({"--clip", "1", "--first", "1"}).payload;
	expect(inspected["pose"].toArray().size() == 2 && inspected["poseClip"].toInt() == 1 && inspected["poseFrame"].toInt() == 1,
		   "CLI identifies inspected model-space joint matrices");
	const QStringList request{"--operation", "rename", "--clip", "1", "--name", "renamed", "--output", output};
	expect(run(request + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(output), "CLI dry run writes nothing");
	expect(run(request).exitCode == 0 && QFile::exists(output), "CLI writes shared authoring service");
	expect(run(request).exitCode == 1, "CLI guards output overwrite");
	expect(run(request + QStringList{"--overwrite"}).exitCode == 0, "CLI explicit overwrite");
	for (const auto &args : QList<QStringList>{{"--clip", "0"},
											   {"--first", "0"},
											   {"--json=true"},
											   {"--json", "--json"},
											   {"--unknown", "1"},
											   {"--operation", "add", "--name", "idle"},
											   {"--source-frame", "0"},
											   {"--clip", "-1", "--first", "0"}})
		expect(run(args).exitCode == 2, "CLI refuses malformed or inapplicable options");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "pose", path, "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["clips"].toArray().size() == 2,
			   "application dispatches pose command");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
