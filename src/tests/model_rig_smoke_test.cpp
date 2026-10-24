#include "cli/model_tools.h"
#include "core/model_recovery.h"
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
bool near(float a, float b)
{
	return std::abs(double(a) - b) < 0.0001;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("rig-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	QString error;
	const auto original = rigFixture();
	expect(validateEditableModel(original).isEmpty(), "fixture is a valid editable skeletal source");
	ModelDocument document;
	if (!expect(document.setMesh(original, &error), "admit fixture"))
		return 1;
	ModelEdit edit;
	edit.kind = ModelEditKind::SetJointWeight;
	edit.rig.joint = QStringLiteral("tip");
	edit.rig.weight = 0.25;
	edit.selection.vertices = {1};
	document.setSelection(edit.selection);
	const auto before = rigBytes(document.mesh());
	if (!expect(document.edit(edit, &error), "assign fractional joint weight"))
		std::cerr << error.toStdString() << '\n';
	expect(near(document.mesh().surfaces[0].frames[2].positions[1].y, 1), "quarter weight produces one unit from a four-unit joint move");
	expect(near(document.mesh().surfaces[0].frames[2].positions[0].y, 0), "unselected vertex stays fixed");
	expect(near(document.mesh().surfaces[0].frames[0].positions[1].x, 8), "bind geometry preserved");
	expect(document.mesh().tags.size() == original.tags.size() && document.mesh().frames.size() == original.frames.size(),
		   "frame and tag layout preserved");
	const auto weighted = rigBytes(document.mesh());
	expect(document.undo() && rigBytes(document.mesh()) == before && document.selection() == edit.selection,
		   "undo restores geometry, weights and selection");
	expect(document.redo() && rigBytes(document.mesh()) == weighted, "redo restores exact weight edit");
	const auto path = QDir(temporary.path()).filePath("weighted.mesh.json");
	expect(document.save(path, false, &error), "save weighted source");
	ModelDocument reopened;
	expect(reopened.load(path, &error) && rigBytes(reopened.mesh()) == weighted, "reopen preserves exact skeletal source");
	ModelRecoverySnapshot snapshot{document.mesh(), document.selection()};
	const auto recovery = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && rigBytes(restored.mesh) == weighted &&
			   restored.selection == document.selection(),
		   "recovery retains influence edits and selected vertices");
	for (const auto &format : {QStringLiteral("md5mesh"), QStringLiteral("iqm")})
	{
		const auto bytes = exportEditableModel(document.mesh(), format, 0, &error);
		const auto decoded = decodeModelMesh("authored." + format, bytes);
		expect(!bytes.isEmpty() && decoded.isValid(), "skeletal native export decodes");
		if (!decoded.skeleton.isEmpty())
		{
			ModelFrameGeometry posed;
			expect(skinModelSurface(decoded.surfaces[0], original.skeleton.clips[0].frames[1], &posed, &error) &&
					   std::abs(double(posed.positions[1].y) - 1) <= (format == "iqm" ? 4.0 / 255 : 0.0001),
				   "native weights reproduce the analytic deformation");
		}
	}
	edit.rig.weight = 0;
	expect(document.edit(edit, &error) && near(document.mesh().surfaces[0].frames[2].positions[1].y, 0),
		   "zero removes target influence and renormalises the remainder");
	edit.rig.weight = 1;
	expect(document.edit(edit, &error) && near(document.mesh().surfaces[0].frames[2].positions[1].y, 4),
		   "one assigns rigidly to target joint");
	const auto reject = [&](const ModelMesh &mesh, const ModelEdit &operation, const char *message) {
		auto candidate = mesh;
		const auto bytes = rigBytes(candidate);
		ModelSelection selection;
		selection.vertices = {2};
		const auto selectionBefore = selection;
		const bool rejected = !applyModelEdit(&candidate, operation, &selection, &error);
		expect(rejected && !error.isEmpty() && rigBytes(candidate) == bytes && selection == selectionBefore, message);
	};
	edit.rig.weight = 0;
	reject(document.mesh(), edit, "cannot remove a vertex's last influence atomically");
	edit.rig.weight = std::numeric_limits<double>::quiet_NaN();
	reject(original, edit, "nonfinite weights rejected atomically");
	edit.rig.weight = 1.01;
	reject(original, edit, "weights above one rejected");
	edit.rig.weight = 0.25;
	edit.selection.vertices = {99};
	reject(original, edit, "invalid selected vertex rejected");
	edit.selection.vertices = {};
	reject(original, edit, "empty weight selection rejected");
	edit.selection.vertices = {1};
	auto dirty = original;
	dirty.surfaces[0].frames[2].positions[1].z = 2;
	reject(dirty, edit, "manual frame edits cannot be silently overwritten");
	dirty = original;
	dirty.surfaces[0].frames[2].normals[1] = {0, 1, 0};
	reject(dirty, edit, "manual normals cannot be silently overwritten");
	dirty = original;
	dirty.skeleton.bakedClipForFrame[2] = -2;
	reject(dirty, edit, "independent frame mappings rejected");
	dirty = original;
	dirty.skeleton.joints[1].bind.m[0] = 0;
	reject(dirty, edit, "singular target bind rejected");
	// MD5-style influences do not carry normals: all neighbouring geometric
	// normals must follow the deformed surface, including on unselected vertices.
	auto geometricNormals = original;
	for (auto &influence : geometricNormals.surfaces[0].skinning.influences)
		influence.normalOffset = {};
	geometricNormals.skeleton.clips[0].frames[1][1] = modelJointMatrix({}, {8, 0, 4});
	expect(bakeModelSkeleton(&geometricNormals, {}, &error), "prepare geometry-derived normals");
	expect(applyModelEdit(&geometricNormals, edit, nullptr, &error), "assign weights with geometry-derived normals");
	const auto geometricExpected = modelFaceNormals(geometricNormals.surfaces[0], geometricNormals.surfaces[0].frames[2].positions);
	expect(near(geometricNormals.surfaces[0].frames[2].normals[0].x, geometricExpected[0].x) &&
		!near(geometricExpected[0].x, 0), "unselected geometric normals follow changed neighbouring positions");
	expect(applyModelEdit(&geometricNormals, edit, nullptr, &error), "derived normals remain consistent on a subsequent weight edit");
	dirty = original;
	dirty.surfaces[0].frames[2].normals[0] = {0, 1, 0};
	reject(dirty, edit, "independent neighbouring normals are protected");
	auto hierarchy = original;
	expect(applyModelEdit(&hierarchy, edit, nullptr, &error), "prepare weighted hierarchy for index remapping");
	ModelEdit add;
	add.kind = ModelEditKind::AddJoint;
	add.rig.name = "later_root";
	add.rig.origin = {4, 5, 6};
	expect(applyModelEdit(&hierarchy, add, nullptr, &error), "append a later root");
	ModelEdit reparent;
	reparent.kind = ModelEditKind::ReparentJoint;
	reparent.rig.joint = "tip";
	reparent.rig.parent = "later_root";
	expect(applyModelEdit(&hierarchy, reparent, nullptr, &error), "parent a joint to a later indexed root");
	expect(hierarchy.skeleton.joints[1].name == "later_root" && hierarchy.skeleton.joints[2].name == "tip" &&
			   hierarchy.skeleton.joints[2].parent == 1 && hierarchy.skeleton.tags[0].joint == 2,
		   "topological reorder remaps hierarchy and skeletal attachments");
	expect(near(modelJointTranslation(hierarchy.skeleton.clips[0].frames[1][2]).y, 4), "reparent preserves model-space animation");
	expect(hierarchy.surfaces[0].skinning.influences[hierarchy.surfaces[0].skinning.first[1] + 1].joint == 2,
		"reparent remaps positive vertex influences to the reordered joint");
	reparent.rig.joint = "later_root";
	reparent.rig.parent = "tip";
	reject(hierarchy, reparent, "cycles rejected without changing arrays");
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameJoint;
	rename.rig.joint = "tip";
	rename.rig.name = "hand";
	expect(applyModelEdit(&hierarchy, rename, nullptr, &error) && hierarchy.skeleton.joints[2].name == "hand",
		   "rename keeps joint identity");
	rename.rig.name = "ROOT";
	rename.rig.joint = "hand";
	reject(hierarchy, rename, "case-insensitive duplicate names rejected");
	rename.rig.name = "bad\"name";
	reject(hierarchy, rename, "unsafe native name rejected");
	add.rig.name = "child";
	add.rig.parent = "hand";
	add.rig.origin = {9, 0, 0};
	expect(applyModelEdit(&hierarchy, add, nullptr, &error) &&
			   near(modelJointTranslation(hierarchy.skeleton.clips[0].frames[1].last()).y, 4),
		   "new child inherits animated parent delta");
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteJoint;
	remove.rig.joint = "child";
	expect(applyModelEdit(&hierarchy, remove, nullptr, &error) && hierarchy.skeleton.joints.size() == 3,
		   "delete unused leaf and its clip channels");
	remove.rig.joint = "hand";
	reject(hierarchy, remove, "joint-driven attachment protects deletion");
	remove.rig.joint = "root";
	reject(hierarchy, remove, "vertex influences protect deletion");
	remove.rig.joint = "later_root";
	reject(hierarchy, remove, "child hierarchy protects deletion");
	// The source decoder permits at most 64 influences, independently of each
	// native exporter's smaller limits. A new influence must not exceed that.
	auto crowded = original;
	for (int i = 2; i < 65; ++i)
	{
		crowded.skeleton.joints.append({QStringLiteral("joint_%1").arg(i), -1, modelJointIdentity()});
		for (auto &pose : crowded.skeleton.clips[0].frames)
			pose.append(modelJointIdentity());
	}
	QVector<int> jointIndices;
	QVector<float> jointWeights;
	for (int vertex = 0; vertex < 3; ++vertex)
		for (int joint = 0; joint < 64; ++joint)
		{
			jointIndices.append(joint);
			jointWeights.append(1.0f / 64);
		}
	expect(modelSkinningFromBindPose(crowded.skeleton, crowded.surfaces[0].frames[0].positions,
		crowded.surfaces[0].frames[0].normals, jointIndices, jointWeights, 64, &crowded.surfaces[0].skinning, &error) &&
		bakeModelSkeleton(&crowded, {}, &error), "construct legal source influence limit");
	edit.rig.joint = "joint_64";
	reject(crowded, edit, "new influences cannot exceed the editable source decoder limit");
	edit.rig.joint = "tip";
	ModelEdit faces = edit;
	faces.selection.vertices.clear();
	faces.selection.faces = {0};
	dirty = original;
	expect(applyModelEdit(&dirty, faces, nullptr, &error) && near(dirty.surfaces[0].frames[2].positions[0].y, 1),
		"face selection expands to its vertex union");
	ModelEdit edges = edit;
	edges.selection.vertices.clear();
	edges.selection.edges = {{0, 1}};
	dirty = original;
	expect(applyModelEdit(&dirty, edges, nullptr, &error) && near(dirty.surfaces[0].frames[2].positions[0].y, 1) &&
		near(dirty.surfaces[0].frames[2].positions[2].y, 8), "edge selection affects only its endpoints");
	// Starting a rig is explicit and bounded to one static authored frame.
	auto staticMesh = original;
	staticMesh.skeleton = {};
	staticMesh.tags.clear();
	staticMesh.animations.clear();
	staticMesh.frames.resize(1);
	staticMesh.surfaces[0].frames.resize(1);
	staticMesh.surfaces[0].skinning = {};
	updateEditableModelMetadata(&staticMesh);
	add.rig = {};
	add.rig.name = "origin";
	add.rig.origin = {3, 2, 1};
	expect(applyModelEdit(&staticMesh, add, nullptr, &error) && staticMesh.surfaces[0].skinning.count == QVector<int>({1, 1, 1}),
		   "first joint binds every static vertex");
	ModelFrameGeometry bound;
	expect(skinModelSurface(staticMesh.surfaces[0], modelSkeletonBindPose(staticMesh.skeleton), &bound, &error) &&
			   near(bound.positions[1].x, 8) && near(bound.positions[1].y, 0),
		   "nonzero root origin retains bind geometry");
	dirty = original;
	dirty.skeleton = {};
	dirty.surfaces[0].skinning = {};
	reject(dirty, add, "vertex animation is not implicitly replaced by a new skeleton");
	ModelWorkControl control;
	bool cancel = false;
	control.progress = [&](ModelWorkPhase phase, qint64, qint64) {
		if (phase == ModelWorkPhase::Editing)
			cancel = true;
	};
	control.cancelled = [&] { return cancel; };
	dirty = original;
	expect(!applyModelEdit(&dirty, edit, nullptr, &error, control) && rigBytes(dirty) == rigBytes(original),
		   "cancelled edit cannot commit");
	const auto output = QDir(temporary.path()).filePath("cli.mesh.json");
	const auto cliRun = [&](QStringList options) {
		return cli::runModelRig(QStringList{"vibestudio", "--cli", "model", "rig", path} + options);
	};
	const auto listing = cliRun({"--surface", "0", "--vertices", "1", "--json"});
	expect(listing.exitCode == 0 && listing.payload["vertices"].toArray().size() == 1, "CLI lists selected vertex weights");
	const QStringList request{"--operation", "rename", "--joint", "tip", "--name", "hand", "--output", output};
	expect(cliRun(request + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(output), "CLI dry run writes nothing");
	expect(cliRun(request).exitCode == 0 && QFile::exists(output), "CLI saves through document service");
	expect(cliRun(request).exitCode == 1, "CLI protects existing output");
	expect(cliRun(request + QStringList{"--overwrite"}).exitCode == 0, "CLI supports explicit overwrite");
	for (const auto &options : QList<QStringList>{{"--weight", "0.5"},
												  {"--vertices", "all"},
												  {"--surface", "0", "--vertices", "1,1"},
												  {"--operation", "add"},
												  {"--operation", "bad"},
												  {"--json=true"},
												  {"--json", "--json"}})
		expect(cliRun(options).exitCode == 2, "CLI rejects malformed or inapplicable options");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), {"--cli", "model", "rig", path, "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["joints"].toArray().size() == 2,
			   "registered application CLI dispatches model rig");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
