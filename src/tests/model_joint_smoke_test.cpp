#include "cli/model_tools.h"
#include "core/model_joint_tools.h"
#include "core/model_recovery.h"
#include "core/model_tags.h"
#include "core/model_transform_axes.h"
#include "tests/model_joint_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
		++failures;
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("joint-core-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto mesh = jointFixture();
	QString error;
	if (!expect(validateEditableModel(mesh).isEmpty() && validateModelSkeleton(mesh, &error), "joint fixture validates"))
		return 1;
	ModelJointFrame frame;
	expect(modelJointFrame(mesh, 0, &frame) && frame.clip == -1 && modelJointFrame(mesh, 2, &frame) && frame.clip == 0 && frame.local == 1,
		   "bind and clip sample mapping");
	auto independent = mesh;
	independent.skeleton.bakedClipForFrame[2] = -2;
	expect(!modelJointFrame(independent, 2, &frame, &error) && !modelJointFrame(mesh, 30, &frame) && !modelJointFrame(mesh, -2, &frame),
		   "unretained and nonexistent poses rejected");
	ModelTransform transform;
	transform.translation = {2, -3, 4};
	transform.rotation = {10, 30, -15};
	transform.scale = {2, .5, 1.5};
	transform.pivot = {8, 4, 0};
	transform.basis.world = false;
	for (auto &axis : transform.basis.axes)
		axis = rotateModelVector(axis, {0, 0, 45});
	ModelJointMatrix delta;
	expect(modelJointTransformDelta(transform, &delta, &error), "construct oriented affine joint delta");
	for (const ModelVec3 point : {ModelVec3{0, 0, 0}, {8, 4, 0}, {-20, 10, 30}})
		expect(jointNear(modelJointTransformPoint(delta, point), transformModelPoint(point, transform)),
			   "delta agrees with independent point transform including pivot");
	ModelJointPreview preview;
	expect(!previewModelJointTransform(mesh, 2, "tip", delta, true, &preview, &error) && error.contains("orthonormal"),
		   "sheared rigid attachment rejects preview through the source validator");
	transform.basis = {};
	expect(modelJointTransformDelta(transform, &delta, &error), "world nonuniform delta retains orthogonal attachment axes");
	for (bool descendants : {false, true})
	{
		expect(previewModelJointTransform(mesh, 2, "tip", delta, descendants, &preview, &error), "pose preview accepted");
		expect(jointNear(modelJointTranslation(preview.pose[1]), transformModelPoint({8, 4, 0}, transform)),
			   "joint origin matches transform");
		expect(
			jointNear(modelJointTranslation(preview.pose[2]), descendants ? transformModelPoint({8, 4, 2}, transform) : ModelVec3{8, 4, 2}),
			"descendant scope retains or moves child in model space");
		expect(jointNear(preview.surfaces[0].positions[1], transformModelPoint(mesh.surfaces[0].frames[2].positions[1], transform)) &&
				   jointNear(preview.surfaces[0].positions[0], {0, 0, 0}),
			   "weighted vertex moves while unweighted vertex stays");
		ModelEdit edit;
		edit.kind = ModelEditKind::TransformSkeletalPose;
		edit.selection.joint = "tip";
		edit.skeletal.joint = "tip";
		edit.skeletal.clip = 0;
		edit.skeletal.first = edit.skeletal.last = 1;
		edit.skeletal.descendants = descendants;
		edit.skeletal.matrixTransform = true;
		edit.skeletal.delta = delta;
		auto candidate = mesh;
		expect(applyModelEdit(&candidate, edit, nullptr, &error), "same pose delta commits through shared document service");
		for (int vertex = 0; vertex < 3; ++vertex)
			expect(jointNear(preview.surfaces[0].positions[vertex], candidate.surfaces[0].frames[2].positions[vertex]) &&
					   jointNear(preview.surfaces[0].normals[vertex], candidate.surfaces[0].frames[2].normals[vertex]),
				   "skin preview positions and normals equal commit");
		expect(jointNear(preview.tags[0].origin, findModelTag(candidate, "tag_tip", 2)->origin), "attachment preview equals commit");
		expect(jointNear(candidate.surfaces[0].frames[1].positions[1], mesh.surfaces[0].frames[1].positions[1]),
			   "other animation sample preserved");
		const auto iqm = decodeModelMesh("posed.iqm", exportEditableModel(candidate, "iqm", 0, &error));
		expect(iqm.isValid() && jointNear(iqm.surfaces[0].frames[2].positions[1], candidate.surfaces[0].frames[2].positions[1], .003),
			   "IQM native handoff retains matrix-authored skin deformation");
		const auto md3 = decodeModelMesh("posed.md3", exportEditableModel(candidate, "md3", 0, &error));
		expect(md3.isValid() && jointNear(md3.surfaces[0].frames[2].positions[1], candidate.surfaces[0].frames[2].positions[1], .02),
			   "MD3 baked handoff retains matrix-authored pose within native quantisation");
	}
	expect(previewModelJointTransform(mesh, 0, "tip", delta, true, &preview), "bind preview accepted");
	expect(jointNear(preview.surfaces[0].positions[1], mesh.surfaces[0].frames[0].positions[1]) &&
			   jointNear(preview.tags[0].origin, mesh.tags[0].origin),
		   "bind preview preserves geometry and attachment");
	auto mixed = mesh;
	mixed.surfaces[0].frames[2].positions[1].x += 1;
	const auto oldPose = preview.pose;
	expect(!previewModelJointTransform(mixed, 2, "tip", delta, true, &preview, &error) && preview.pose.size() == oldPose.size() &&
			   modelJointMatrixDistance(preview.pose[1], oldPose[1]) == 0 && error.contains("independent"),
		   "independent mesh edit refuses atomic preview");
	mixed = mesh;
	mixed.tags.last().origin.x += 1;
	expect(!previewModelJointTransform(mixed, 2, "tip", delta, true, &preview, &error), "independent attachment refuses preview");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	expect(!previewModelJointTransform(mesh, 2, "tip", delta, true, &preview, &error, cancelled) &&
			   modelJointMatrixDistance(preview.pose[1], oldPose[1]) == 0,
		   "cancellation preserves output");
	for (float value : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1000001.f})
	{
		auto invalid = modelJointIdentity();
		invalid.m[0] = value;
		expect(!validModelJointDelta(invalid), "invalid matrix orientation, range or finiteness refused");
	}
	ModelSelection selected;
	selected.joint = "tip";
	ModelDocument document;
	expect(document.setMesh(mesh, &error), "open document");
	document.setSelection(selected);
	expect(document.selection() == selected, "joint selection retained");
	selected.vertices = {0};
	document.setSelection(selected);
	expect(document.selection().vertices.isEmpty() && document.selection().joint == "tip", "mixed joint/component selection rejected");
	ModelEdit axes;
	axes.selection = document.selection();
	axes.transformSpace = ModelTransformSpace::Selection;
	axes.axesFrame = 2;
	ModelTransformBasis basis;
	expect(resolveModelTransformAxes(mesh, axes, &basis, &error) && !basis.world, "joint supplies selection axes");
	ModelEdit edit;
	edit.kind = ModelEditKind::RenameJoint;
	edit.selection = document.selection();
	edit.rig.joint = "tip";
	edit.rig.name = "hand";
	expect(document.edit(edit, &error) && document.selection().joint == "hand", "renaming selected joint remaps selection");
	expect(document.undo() && document.selection().joint == "tip" && document.redo() && document.selection().joint == "hand",
		   "joint selection preserved across undo and redo");
	ModelRecoverySnapshot snapshot{document.mesh(), document.selection(), 2};
	const auto path = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	expect(!path.isEmpty() && inspectModelRecovery(path, &restored).isValid() && restored.selection.joint == "hand" && restored.frame == 2,
		   "recovery round trips selected joint and frame");
	selected = {};
	selected.joint = "hidden";
	document.setSelection(selected);
	edit = {};
	edit.kind = ModelEditKind::DeleteJoint;
	edit.selection = selected;
	edit.rig.joint = "hidden";
	expect(document.edit(edit, &error) && document.selection().joint.isEmpty() && document.undo() && document.selection().joint == "hidden",
		   "deleting selected unused joint clears selection and undo restores it");
	const auto input = QDir(temporary.path()).filePath("source.mesh.json");
	const auto output = QDir(temporary.path()).filePath("result.mesh.json");
	QFile source(input);
	if (!source.open(QIODevice::WriteOnly) || source.write(rigBytes(mesh)) <= 0)
		return 1;
	source.close();
	const QStringList command{"vibestudio",
							  "--cli",
							  "model",
							  "pose",
							  input,
							  "--operation",
							  "transform",
							  "--joint",
							  "tip",
							  "--clip",
							  "0",
							  "--first",
							  "1",
							  "--last",
							  "1",
							  "--matrix",
							  "1,0,0,3,0,1,0,-2,0,0,1,1",
							  "--output",
							  output};
	const auto result = cli::runModelPose(command);
	expect(result.exitCode == 0, "CLI writes affine joint delta");
	ModelDocument loaded;
	expect(loaded.load(output, &error) && jointNear(loaded.mesh().surfaces[0].frames[2].positions[1], {11, 2, 1}),
		   "CLI delta has expected analytic deformation");
	for (const QStringList &extra :
		 {QStringList{"--translate", "0,0,0"}, {"--space", "model"}, {"--rotate", "0,0,0"}, {"--scale", "1,1,1"}})
		expect(cli::runModelPose(command + extra).exitCode == 2, "matrix and numeric options cannot silently mix");
	for (const QString value : {"1,0,0", "nan,0,0,0,0,1,0,0,0,0,1,0", "-1,0,0,0,0,1,0,0,0,0,1,0"})
	{
		auto invalid = command;
		invalid[invalid.indexOf("--matrix") + 1] = value;
		expect(cli::runModelPose(invalid).exitCode == 2, "CLI matrix shape and numeric validation");
	}
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), command.mid(1) + QStringList{"--dry-run", "--overwrite", "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 && process.readAllStandardOutput().contains("dryRun"),
			   "executable routes affine pose CLI");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
