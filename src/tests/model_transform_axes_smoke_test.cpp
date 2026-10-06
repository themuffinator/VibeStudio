#include "core/model_collision.h"
#include "core/model_recovery.h"
#include "core/model_tags.h"
#include "core/model_transform_axes.h"
#include "tests/model_transform_axes_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value) std::cerr << "FAIL: " << message << '\n';
	return value;
}
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root) || argc != 2) return 1;
	QTemporaryDir temporary(QDir(root).filePath("transform-axes-XXXXXX"));
	if (!temporary.isValid()) return 1;
	bool ok = true;
	QString error;
	const auto original = transformAxesFixture();
	ok &= expect(validateEditableModel(original).isEmpty(), "animated axes fixture validates");
	ModelEdit edit;
	edit.kind = ModelEditKind::Transform;
	edit.selection.faces = {0};
	edit.transformSpace = ModelTransformSpace::Selection;
	ModelTransformBasis basis;
	ok &= expect(resolveModelTransformAxes(original, edit, &basis, &error) && !basis.world &&
		nearAxesVector(basis.axes[0], {0, 1, 0}) && nearAxesVector(basis.axes[1], {0, 0, 1}) && nearAxesVector(basis.axes[2], {1, 0, 0}),
		"face axes use first edge, winding normal and a right-handed tangent");
	const auto selectedBasis = basis;
	{
		auto oblique = edit; oblique.transformSpace = ModelTransformSpace::Custom; oblique.axisRotation = {90, 0, 53.1301024f};
		ok &= expect(resolveModelTransformAxes(original, oblique, &basis, &error) && nearAxesVector(basis.axes[0], {.6f, .8f, 0}) &&
			nearAxesVector(basis.axes[1], {0, 0, 1}) && nearAxesVector(basis.axes[2], {.8f, -.6f, 0}), "custom oblique axes match a 3-4-5 triangle basis");
		ModelTransform compound; compound.basis = basis; compound.pivot = {1, 2, 3}; compound.translation = {2, 3, 4};
		compound.scale = {2, 3, 4}; compound.rotation = {0, 0, 90};
		ok &= expect(nearAxesVector(transformModelPoint({10, 20, 30}, compound), {-54.72f, -54.96f, 45.6f}), "oblique nonuniform compound transform matches independent arithmetic");
		basis = selectedBasis;
	}
	ModelTransform transform;
	transform.basis = basis;
	transform.pivot = {3, 4, 5}; transform.translation = {2, 3, 4}; transform.scale = {2, 3, 4}; transform.rotation = {0, 0, 90};
	ok &= expect(nearAxesVector(transformModelPoint({3, 14, 5}, transform), {7, 6, 28}), "scale then rotate then translate in selected coordinates about a world pivot");
	const auto normal = transformModelNormal({1, 2, 3}, transform);
	const double normalLength = std::sqrt(.25 * .25 + 2);
	ok &= expect(nearAxesVector(normal, {float(.25 / normalLength), float(-1 / normalLength), float(1 / normalLength)}),
		"normal uses the inverse transpose in the chosen basis");
	transform = {}; transform.basis = basis; transform.translation = {2, 3, 4}; transform.pivot = {1000000, 1000000, 1000000};
	ok &= expect(nearAxesVector(transformModelPoint({.125f, .25f, .5f}, transform), {4.125f, 2.25f, 3.5f}), "local translation retains precision beside a distant pivot");
	for (int selection = 0; selection < 3; ++selection)
	{
		auto alternate = edit; alternate.selection.faces.clear();
		if (selection == 0) alternate.selection.vertices = {1};
		if (selection == 1) alternate.selection.edges = {modelEdge(1, 2)};
		if (selection == 2) alternate.selection.surfaces = {0};
		ok &= expect(resolveModelTransformAxes(original, alternate, &basis, &error) && nearAxesVector(basis.axes[0], {0, 1, 0}), "vertex, edge and whole-surface axes use a deterministic incident face");
	}
	edit.translation = {2, 0, 0};
	for (int reference : {0, 1})
	{
		auto mesh = original;
		edit.axesFrame = reference;
		ok &= expect(applyModelEdit(&mesh, edit, nullptr, &error), "apply selected axes to all poses");
		for (int frame = 0; frame < 2; ++frame)
		{
			const auto p = original.surfaces[0].frames[frame].positions[0];
			ok &= expect(nearAxesVector(mesh.surfaces[0].frames[frame].positions[0], {p.x + (reference ? 2 : 0), p.y + (reference ? 0 : 2), p.z}),
				"all poses share axes from one reference pose");
			ok &= expect(nearAxesVector(mesh.surfaces[0].frames[frame].positions[3], original.surfaces[0].frames[frame].positions[3]), "unselected vertex stays unchanged");
		}
		ok &= expect(editableModelJson(mesh).value("tags") == editableModelJson(original).value("tags") &&
			editableModelJson(mesh).value("collisionBoxes") == editableModelJson(original).value("collisionBoxes"), "component transform retains tags and collision");
	}
	edit.axesFrame = -1; edit.frame = 1;
	auto mesh = original;
	ok &= expect(applyModelEdit(&mesh, edit, nullptr, &error) && nearAxesVector(mesh.surfaces[0].frames[1].positions[0], {25, 4, 5}) &&
		nearAxesVectors(mesh.surfaces[0].frames[0].positions, original.surfaces[0].frames[0].positions), "current-frame axes default to that pose and retain other frames");
	edit.frame = -1; edit.pivotFrame = 1;
	ok &= expect(resolveModelTransformAxes(original, edit, &basis) && nearAxesVector(basis.axes[0], {1, 0, 0}), "all-frame reference defaults to pivot reference pose");
	edit.pivotFrame = 0;
	{
		mesh = original; auto tangent = edit; tangent.kind = ModelEditKind::Extrude; tangent.translation = {0, 0, 2};
		ok &= expect(!applyModelEdit(&mesh, tangent, nullptr, &error) && axesMeshBytes(mesh) == axesMeshBytes(original),
			"fixed-axis extrusion that collapses walls in another pose fails atomically");
	}
	for (auto kind : {ModelEditKind::Extrude, ModelEditKind::DuplicateFaces})
	{
		mesh = original; auto topology = edit; topology.kind = kind; topology.translation = {1, 2, 3};
		ModelSelection selected;
		ok &= expect(applyModelEdit(&mesh, topology, &selected, &error) && !selected.faces.isEmpty(), "topology edit accepts selected axes");
		for (int frame = 0; frame < 2; ++frame)
		{
			bool displaced = false;
			const auto p = original.surfaces[0].frames[frame].positions[0];
			for (int v = 4; v < mesh.surfaces[0].frames[frame].positions.size(); ++v)
				displaced |= nearAxesVector(mesh.surfaces[0].frames[frame].positions[v], {p.x + 3, p.y + 1, p.z + 2});
			ok &= expect(displaced, "extrusion and duplication use the same fixed world offset in every pose");
		}
	}
	{
		auto surfaces = original; surfaces.surfaces << surfaces.surfaces[0]; surfaces.surfaces[1].name = "second";
		for (auto &pose : surfaces.surfaces[1].frames) for (auto &p : pose.positions) p.x += 30;
		updateEditableModelMetadata(&surfaces);
		auto all = edit; all.selection.faces.clear(); all.selection.surfaces = {0, 1};
		ok &= expect(applyModelEdit(&surfaces, all, nullptr, &error) && nearAxesVector(surfaces.surfaces[1].frames[1].positions[0], {53, 6, 5}), "whole surfaces share active surface axes across every pose");
	}
	{
		auto tag = edit; tag.kind = ModelEditKind::TransformTag; tag.selection.faces.clear(); tag.selection.tag = "tag_mount";
		mesh = original;
		ok &= expect(applyModelEdit(&mesh, tag, nullptr, &error) && nearAxesVector(mesh.tags[0].origin, {1, 4, 3}) &&
			nearAxesVector(mesh.tags[1].origin, {1, 4, 11}), "tag translation uses reference axes across all poses");
		tag.translation = {}; tag.rotation = {0, 0, 90}; tag.pivotMode = ModelTransformPivot::SelectionCentre;
		mesh = original;
		ok &= expect(applyModelEdit(&mesh, tag, nullptr, &error) && nearAxesVector({mesh.tags[0].axis[0], mesh.tags[0].axis[1], mesh.tags[0].axis[2]}, {0, 0, 1}), "tag orientation rotates around its selected local Z");
		auto reflected = original; reflected.tags[0].axis[6] = -1;
		ok &= expect(resolveModelTransformAxes(reflected, tag, &basis, &error) && nearAxesVector(basis.axes[2], {-1, 0, 0}), "tag axes preserve native reflected handedness");
	}
	{
		auto collision = edit; collision.kind = ModelEditKind::TransformCollisionBox; collision.selection.faces.clear(); collision.selection.collision = "body";
		collision.pivotMode = ModelTransformPivot::SelectionCentre;
		mesh = original;
		ok &= expect(applyModelEdit(&mesh, collision, nullptr, &error) && nearAxesVector(mesh.collisionBoxes[0].centre, {8, 11, 10}), "collision translation follows selected box axes");
		collision.translation = {}; collision.scale = {2, 3, 4}; collision.transformSpace = ModelTransformSpace::Custom; collision.axisRotation = {35, 20, 15};
		mesh = original;
		ok &= expect(applyModelEdit(&mesh, collision, nullptr, &error) && nearAxesVector(mesh.collisionBoxes[0].size, {4, 12, 24}) &&
			nearAxesVector(mesh.collisionBoxes[0].rotation, original.collisionBoxes[0].rotation), "collision size stays intrinsic and introduces no custom-basis shear");
		collision.transformSpace = ModelTransformSpace::Selection; collision.axisRotation = {}; collision.scale = {1, 1, 1}; collision.rotation = {90, 0, 0};
		mesh = original;
		ok &= expect(applyModelEdit(&mesh, collision, nullptr, &error) && nearAxesVector(modelCollisionAxes(mesh.collisionBoxes[0])[1], {0, 0, 1}), "collision rotation uses its selected X axis");
	}
	{
		auto custom = edit; custom.transformSpace = ModelTransformSpace::Custom; custom.axisRotation = {90, 0, 90};
		ok &= expect(resolveModelTransformAxes(original, custom, &basis, &error) && nearAxesVector(basis.axes[0], {0, 1, 0}) &&
			nearAxesVector(basis.axes[1], {0, 0, 1}) && nearAxesVector(basis.axes[2], {1, 0, 0}), "custom Euler XYZ orientation matches independently constructed cyclic basis");
		custom.translation = {}; custom.scale = {-1, 1, 1}; custom.selection.faces = {0, 1};
		mesh = original;
		ok &= expect(applyModelEdit(&mesh, custom, nullptr, &error) && mesh.surfaces[0].triangles[0].b == original.surfaces[0].triangles[0].c &&
			nearAxesVector(mesh.surfaces[0].frames[0].positions[1], {3, -14, 5}), "custom-axis reflection repairs selected winding once across poses");
	}
	for (int invalid = 0; invalid < 8; ++invalid)
	{
		auto bad = edit;
		if (invalid == 0) bad.transformSpace = ModelTransformSpace(9);
		if (invalid == 1) bad.axesFrame = 2;
		if (invalid == 2) bad.axesFrame = -2;
		if (invalid == 3) bad.axisRotation = {1, 0, 0};
		if (invalid == 4) bad.kind = ModelEditKind::Subdivide;
		if (invalid == 5) bad.axisRotation = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
		if (invalid == 6) { bad.transformSpace = ModelTransformSpace::Custom; bad.axisRotation = {1000001, 0, 0}; }
		if (invalid == 7) { bad.transformSpace = ModelTransformSpace::World; bad.axesFrame = 0; }
		mesh = original; ModelSelection output; output.vertices = {3};
		ok &= expect(!applyModelEdit(&mesh, bad, &output, &error) && !error.isEmpty() && axesMeshBytes(mesh) == axesMeshBytes(original) && output.vertices == QSet<int>{3}, "invalid axes edit fails atomically with diagnostic");
	}
	{
		auto degenerate = original;
		degenerate.surfaces[0].frames[0].positions[1] = degenerate.surfaces[0].frames[0].positions[0];
		basis = selectedBasis;
		ok &= expect(!resolveModelTransformAxes(degenerate, edit, &basis, &error) && !error.isEmpty() && nearAxesVectors(basis.axes, selectedBasis.axes), "degenerate selected face refuses axes without changing result");
		auto unused = original; unused.surfaces[0].texCoords << ModelTexCoord{};
		for (auto &pose : unused.surfaces[0].frames) { pose.positions << ModelVec3{9, 9, 9}; pose.normals << ModelVec3{0, 0, 1}; }
		updateEditableModelMetadata(&unused);
		auto isolated = edit; isolated.selection.faces.clear(); isolated.selection.vertices = {4};
		ok &= expect(!applyModelEdit(&unused, isolated, nullptr, &error) && error.contains("touching"), "isolated vertices give an actionable axes diagnostic");
	}
	{
		auto dense = original;
		dense.surfaces[0].triangles.fill({0, 1, 2}, 100000);
		auto last = edit; last.selection.faces = {99999};
		int checkpoints = 0;
		ModelWorkControl control; control.cancelled = [&] { return ++checkpoints > 3; };
		basis = selectedBasis;
		ok &= expect(!resolveModelTransformAxes(dense, last, &basis, &error, control) && checkpoints == 4 && nearAxesVectors(basis.axes, selectedBasis.axes), "long face search polls cancellation without partial publication");
	}
	ModelDocument document;
	ok &= expect(document.setMesh(original, &error), "open source document");
	document.setSelection(edit.selection);
	ok &= expect(document.edit(edit, &error), "document commits axes transform");
	const auto transformed = axesMeshBytes(document.mesh());
	ok &= expect(document.undo() && axesMeshBytes(document.mesh()) == axesMeshBytes(original) && !document.canUndo() && document.redo() &&
		axesMeshBytes(document.mesh()) == transformed, "axes edit is one exact undo and redo step");
	const auto source = QDir(temporary.path()).filePath("source.mesh.json"), output = QDir(temporary.path()).filePath("output.mesh.json");
	ModelDocument sourceDocument; sourceDocument.setMesh(original);
	ok &= expect(sourceDocument.save(source, false, &error), "save CLI source");
	ok &= expect(document.save(output, false, &error), "save transformed source");
	ModelDocument reopened;
	ok &= expect(reopened.load(output, &error) && axesMeshBytes(reopened.mesh()) == transformed, "chosen axes result survives source round trip");
	ModelRecoverySnapshot snapshot; snapshot.mesh = document.mesh(); snapshot.selection = document.selection(); snapshot.frame = 1;
	snapshot.title = "Transform axes";
	const auto recoveryPath = writeModelRecovery(snapshot, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot recovered;
	ok &= expect(!recoveryPath.isEmpty() && inspectModelRecovery(recoveryPath, &recovered).isValid() && axesMeshBytes(recovered.mesh) == transformed &&
		recovered.selection == snapshot.selection && recovered.frame == 1, "recovery retains transformed geometry, selection and displayed pose");
	auto encoded = exportEditableModel(document.mesh(), "md3", 0, &error);
	ModelMesh native;
	ok &= expect(!encoded.isEmpty() && importEditableModel("axes.md3", encoded, &native, &error) &&
		nearAxesVector(native.surfaces[0].frames[1].positions[0], document.mesh().surfaces[0].frames[1].positions[0], .02), "native export retains fixed-axis animation geometry");
	const QStringList command{"--cli", "model", "edit", source, "--operation", "transform", "--faces", "0", "--offset", "2,0,0", "--output", output, "--json"};
	const auto run = [&](QStringList arguments, int code)
	{
		QProcess process; process.setWorkingDirectory(temporary.path()); process.start(QString::fromLocal8Bit(argv[1]), arguments);
		const bool finished = process.waitForFinished(30000);
		const auto result = QJsonDocument::fromJson(process.readAllStandardOutput());
		const bool passed = finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code && result.isObject();
		if (!passed) std::cerr << arguments.join(' ').toStdString() << " expected " << code << " got " << process.exitCode() << '\n' << result.toJson().toStdString();
		return expect(passed, "CLI returns expected status and structured result");
	};
	ok &= run(command + QStringList{"--transform-space", "selection", "--dry-run", "--overwrite"}, 0);
	ok &= expect(reopened.load(output) && axesMeshBytes(reopened.mesh()) == transformed, "dry run leaves existing file unchanged");
	ok &= run(command + QStringList{"--transform-space=selection", "--axes-frame=0", "--overwrite"}, 0);
	ok &= expect(reopened.load(output) && axesMeshBytes(reopened.mesh()) == transformed, "CLI selection axes match core serialized result");
	ok &= run(command + QStringList{"--transform-space", "selection"}, 1);
	const QList<QStringList> invalidOptions{
		{"--transform-space", "bogus"}, {"--transform-space", "selection", "--transform-space", "world"},
		{"--transform-space=selection", "--transform-space", "world"},
		{"--transform-space=selection", "--axes-frame=0", "--axes-frame", "1"},
		{"--transform-space=custom", "--axis-rotation=0,0,0", "--axis-rotation", "0,0,0"},
		{"--axis-rotation", "0,0,0"}, {"--transform-space", "custom", "--axis-rotation", "1,2"},
		{"--transform-space", "custom", "--axis-rotation", "nan,0,0"}, {"--transform-space", "custom", "--axis-rotation", "1000001,0,0"},
		{"--transform-space", "selection", "--axes-frame", "2"}, {"--transform-space", "selection", "--axes-frame", "+1"},
		{"--transform-space", "custom", "--axes-frame", "0"}, {"--transform-space"}};
	for (const auto &options : invalidOptions) ok &= run(command + QStringList{"--overwrite"} + options, 2);
	auto irrelevant = command; irrelevant[irrelevant.indexOf("--operation") + 1] = "subdivide";
	ok &= run(irrelevant + QStringList{"--transform-space", "world", "--overwrite"}, 2);
	ok &= run(command + QStringList{"--transform-space", "custom", "--axis-rotation", "90,0,90", "--overwrite"}, 0);
	ok &= expect(reopened.load(output) && nearAxesVector(reopened.mesh().surfaces[0].frames[1].positions[0], {23, 6, 5}), "CLI custom XYZ axes match independent expectation");
	const QStringList collisionCommand{"--cli", "model", "collision", source, "--operation", "transform", "--box", "body", "--offset", "2,0,0",
		"--transform-space", "selection", "--output", output, "--overwrite", "--json"};
	ok &= run(collisionCommand, 0);
	ok &= expect(reopened.load(output) && nearAxesVector(reopened.mesh().collisionBoxes[0].centre, {8, 11, 10}), "collision CLI uses the same selected axes");
	ok &= run(collisionCommand + QStringList{"--transform-space", "world"}, 2);
	ok &= expect(sourceDocument.load(source) && axesMeshBytes(sourceDocument.mesh()) == axesMeshBytes(original), "CLI workflows preserve input source");
	if (!ok) std::cerr << error.toStdString() << '\n';
	std::cout << checks << " transform axes core/CLI checks\n";
	return ok ? 0 : 1;
}
