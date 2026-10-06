#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_transform.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
bool equal(ModelVec3 a, ModelVec3 b)
{
	return std::abs(a.x - b.x) < 0.0001f && std::abs(a.y - b.y) < 0.0001f && std::abs(a.z - b.z) < 0.0001f;
}
ModelMesh fixture()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	mesh.surfaces[0].frames << mesh.surfaces[0].frames[0];
	for (auto &point : mesh.surfaces[0].frames[1].positions)
	{
		point.z += 8;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	ModelVec3 result;
	ok &= expect(snapModelTranslation({0.49f, 0.5f, -0.5f}, 1, &result, &error) && equal(result, {0, 1, -1}),
				 "grid rounds delta components symmetrically with ties away from zero");
	ok &= expect(snapModelTranslation({0.125f, 2.5f, -4.75f}, 0, &result) && equal(result, {0.125f, 2.5f, -4.75f}),
				 "disabled grid preserves fractional movement");
	for (double grid : {-1., 1e-7, 1000001., std::numeric_limits<double>::infinity()})
	{
		result = {99, 98, 97};
		ok &= expect(!snapModelTranslation({}, grid, &result, &error) && equal(result, {99, 98, 97}) && !error.isEmpty(),
					 "invalid snap step fails without modifying output");
	}
	ModelMoveDrag drag;
	ok &= expect(beginModelMoveDrag({}, {0, 0, -1}, ModelMoveConstraint::X, {{0, 0, 10}, {0, 0, -1}}, 2, &drag) &&
					 modelMoveDragDelta(drag, {{3.1f, 50, 10}, {0, 0, -1}}, &result) && equal(result, {4, 0, 0}),
				 "axis drag projects onto its constraint and uses the shared snap step");
	ok &= expect(beginModelMoveDrag({2, 3, 4}, {0, 0, -1}, ModelMoveConstraint::ViewPlane, {{2, 3, 10}, {0, 0, -1}}, 0, &drag) &&
					 modelMoveDragDelta(drag, {{7, 1, 10}, {0, 0, -1}}, &result) && equal(result, {5, -2, 0}),
				 "view-plane drag starts at the grabbed ray, preserving its pivot offset");
	ok &= expect(beginModelMoveDrag({0, 0, 10}, {0, 0, 1}, ModelMoveConstraint::X, {{0, 0, 0}, {0.1f, 0, 1}}, 0, &drag) &&
					 modelMoveDragDelta(drag, {{0, 0, 0}, {0.3f, 0.7f, 1}}, &result) && equal(result, {2, 0, 0}),
				 "perspective rays intersect the same fixed drag plane");
	const auto prior = drag;
	ok &=
		expect(!beginModelMoveDrag({}, {1, 0, 0}, ModelMoveConstraint::X, {{10, 0, 0}, {-1, 0, 0}}, 0, &drag) && drag.start == prior.start,
			   "an axis aligned with the view cannot create an unstable drag plane");
	result = {9, 8, 7};
	ok &= expect(!modelMoveDragDelta(drag, {{0, 0, 0}, {1, 0, 0}}, &result) && equal(result, {9, 8, 7}),
				 "parallel rays reject updates without losing the last valid delta");
	ok &= expect(!beginModelMoveDrag({}, {}, ModelMoveConstraint::ViewPlane, {{}, {0, 0, 1}}, 0, &drag),
				 "invalid camera direction is rejected");
	ok &= expect(snapModelRotation({7.5f, -7.5f, 49}, 15, &result) && equal(result, {15, -15, 45}),
				 "angle snapping treats signed ties symmetrically");
	ok &= expect(snapModelScale({1, 1.25f, 0.75f}, 0.1, &result) && equal(result, {1, 1.3f, 0.7f}),
				 "scale snapping uses distance from identity and leaves neutral axes unchanged");
	result = {9, 8, 7};
	ok &= expect(!snapModelScale({0.04f, 1, 1}, 0.1, &result, &error) && equal(result, {9, 8, 7}) && !error.isEmpty(),
				 "snapping to zero scale fails atomically");
	ok &= expect(snapModelScale({-1, 1, 1}, 0, &result) && equal(result, {-1, 1, 1}), "numeric mirroring remains supported");
	result = {9, 8, 7};
	ok &= expect(!snapModelRotation({1000000, 0, 0}, 180, &result, &error) && equal(result, {9, 8, 7}) && !error.isEmpty(),
				 "snapping beyond the angle limit reports a diagnostic without changing output");
	for (double step : {-1., 0.0000001, 181., std::numeric_limits<double>::infinity()})
	{
		result = {9, 8, 7};
		ok &= expect(!snapModelRotation({}, step, &result, &error) && equal(result, {9, 8, 7}),
					 "invalid rotation snap leaves output unchanged");
	}
	ModelTransform transform;
	transform.pivot = {1, 0, 0};
	transform.scale = {2, 1, 1};
	transform.rotation = {0, 0, 90};
	transform.translation = {0, 1, 0};
	ok &= expect(equal(transformModelPoint({3, 0, 0}, transform), {1, 5, 0}) &&
					 equal(transformModelNormal({1, 1, 0}, transform), {-0.8944272f, 0.4472136f, 0}),
				 "preview and authoring helpers apply pivoted scale then rotation, with inverse-transpose normals");
	const QVector<ModelVec3> pivotPoints{{0, 0, 0}, {10, 2, 4}, {4, 12, 8}};
	ok &= expect(modelTransformPivot(pivotPoints, {0, 1, 2}, ModelTransformPivot::SelectionCentre, {}, &result) && equal(result, {5, 6, 4}),
				 "selection pivot uses bounds rather than vertex-count weighting");
	result = {9, 8, 7};
	ok &= expect(!modelTransformPivot(pivotPoints, {5}, ModelTransformPivot::SelectionCentre, {}, &result) && equal(result, {9, 8, 7}),
				 "stale pivot selection is rejected atomically");
	ModelRotateDrag rotationDrag;
	ok &= expect(beginModelRotateDrag({}, 2, {{1, 0, 10}, {0, 0, -1}}, 0, &rotationDrag), "begin rotation on a world-axis plane");
	double degrees = 0;
	for (double angle : {175., 185., 270., 360.})
	{
		const double radians = angle * std::numbers::pi / 180;
		ok &=
			expect(modelRotateDragAngle(&rotationDrag, {{float(std::cos(radians)), float(std::sin(radians)), 10}, {0, 0, -1}}, &degrees) &&
					   std::abs(degrees - angle) < 0.0001,
				   "rotation unwraps across both signed-angle boundaries");
	}
	const auto beforeRotation = rotationDrag;
	degrees = 999;
	ok &= expect(!modelRotateDragAngle(&rotationDrag, {{0, 0, 10}, {0, 0, -1}}, &degrees) && degrees == 999 &&
					 rotationDrag.previous == beforeRotation.previous && rotationDrag.degrees == beforeRotation.degrees,
				 "dragging through the pivot cannot corrupt the accumulated angle");
	ok &= expect(!beginModelRotateDrag({}, 0, {{0, 0, 10}, {0, 0, -1}}, 0, &rotationDrag) && rotationDrag.axis == beforeRotation.axis,
				 "edge-on rotation is refused by the plane solver for the viewport's tangent fallback");
	const auto original = fixture();
	auto mesh = original;
	ModelEdit edit;
	edit.selection.faces = {0, 1};
	edit.translation = {0.49f, 0.51f, -0.5f};
	edit.translationGrid = 1;
	ok &= expect(applyModelEdit(&mesh, edit, nullptr, &error), "document transform accepts shared delta snapping");
	for (int frame = 0; frame < 2; ++frame)
	{
		const auto a = original.surfaces[0].frames[frame].positions[0], b = mesh.surfaces[0].frames[frame].positions[0];
		ok &= expect(equal(b, {a.x, a.y + 1, a.z - 1}), "snapped translation spans every selected animation pose");
	}
	mesh = original;
	edit.frame = 1;
	ok &= expect(applyModelEdit(&mesh, edit, nullptr, &error) &&
					 equal(mesh.surfaces[0].frames[0].positions[0], original.surfaces[0].frames[0].positions[0]) &&
					 mesh.surfaces[0].frames[1].positions[0].z == 7,
				 "frame-local snapping leaves the other pose intact");
	mesh = original;
	edit.kind = ModelEditKind::Extrude;
	ok &= expect(!applyModelEdit(&mesh, edit, nullptr, &error) && editableModelJson(mesh) == editableModelJson(original),
				 "unsupported snapped operations fail atomically");
	mesh = original;
	edit = {};
	edit.selection.faces = {0, 1};
	edit.rotation = {179, 0, 0};
	edit.rotationGrid = 15;
	edit.scale = {1, 1, 1.26f};
	edit.scaleGrid = 0.1;
	edit.pivotMode = ModelTransformPivot::SelectionCentre;
	edit.pivotFrame = 1;
	ok &= expect(applyModelEdit(&mesh, edit, nullptr, &error) && std::abs(mesh.surfaces[0].frames[0].positions[0].z - 18.4f) < 0.0001 &&
					 std::abs(mesh.surfaces[0].frames[1].positions[0].z - 8) < 0.0001 &&
					 equal(mesh.surfaces[0].frames[0].normals[0], {0, 0, -1}),
				 "all-frame snapped rotation and scale share the fixed reference-pose pivot and transformed normals");
	ModelDocument history;
	ok &= expect(history.setMesh(original, &error) && history.edit(edit, &error) && history.undo() &&
					 editableModelJson(history.mesh()) == editableModelJson(original) && history.redo() &&
					 editableModelJson(history.mesh()) == editableModelJson(mesh),
				 "combined transforms form one reversible document edit");
	mesh = original;
	edit.frame = 0;
	ok &= expect(applyModelEdit(&mesh, edit, nullptr, &error) && mesh.surfaces[0].frames[1].positions[0].z == 8 &&
					 std::abs(mesh.surfaces[0].frames[0].positions[0].z) < 0.0001,
				 "frame-local edits resolve their pivot in the edited pose");
	mesh = original;
	edit.frame = -1;
	edit.pivotFrame = 99;
	ok &= expect(!applyModelEdit(&mesh, edit, nullptr, &error) && editableModelJson(mesh) == editableModelJson(original),
				 "invalid reference poses preserve the source");
	if (app.arguments().size() > 1)
	{
		const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
		if (root.isEmpty() || !QDir().mkpath(root))
		{
			return EXIT_FAILURE;
		}
		QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-transform-XXXXXX")));
		if (!temporary.isValid())
		{
			return EXIT_FAILURE;
		}
		const auto input = QDir(temporary.path()).filePath(QStringLiteral("source.mesh.json"));
		const auto output = QDir(temporary.path()).filePath(QStringLiteral("moved.mesh.json"));
		ModelDocument document;
		ok &= expect(document.setMesh(original, &error) && document.save(input, false, &error), "save CLI source fixture");
		const auto cli = [&](QStringList arguments, int expected)
		{
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			arguments.prepend(QStringLiteral("--cli"));
			arguments << "--json";
			process.start(app.arguments()[1], arguments);
			const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			if (!finished || process.exitCode() != expected)
			{
				std::cerr << bytes.constData() << process.readAllStandardError().constData();
			}
			return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected &&
				   QJsonDocument::fromJson(bytes).isObject();
		};
		const QStringList command{"model",	  "edit",			input,		   "--operation", "transform", "--faces", "all",
								  "--offset", "0.49,0.51,-0.5", "--snap-grid", "1",			  "--output",  output};
		ok &= expect(cli(command + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(output),
					 "CLI snap dry-run validates without publishing");
		ok &= expect(cli(command, 0) && document.load(output, &error) && document.mesh().surfaces[0].frames[0].positions[0].z == -1 &&
						 document.mesh().surfaces[0].frames[1].positions[0].z == 7,
					 "CLI commits the same snapped all-frame transform");
		ok &=
			expect(cli({"model", "edit", input, "--operation", "transform", "--faces", "all", "--snap-grid", "nan", "--output", output}, 2),
				   "CLI nonfinite grid is a usage error");
		ok &= expect(cli({"model", "edit", input, "--operation", "weld", "--vertices", "all", "--snap-grid", "1", "--output", output}, 2),
					 "CLI refuses snapping for a non-transform operation");
		ok &= expect(cli({"model", "edit", input, "--operation", "weld", "--vertices", "all", "--pivot", "0,0,0", "--output", output}, 2),
					 "CLI refuses an ignored pivot on a non-transform operation");
		const auto rotated = QDir(temporary.path()).filePath(QStringLiteral("rotated.mesh.json"));
		const QStringList rotateCommand{"model", "edit",		 input,		"--operation",	"transform", "--faces",
										"all",	 "--rotate",	 "179,0,0", "--scale",		"1,1,1.26",	 "--snap-angle",
										"15",	 "--snap-scale", "0.1",		"--pivot-mode", "selection", "--pivot-frame",
										"1",	 "--output",	 rotated};
		ok &= expect(cli(rotateCommand + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(rotated),
					 "rotation/scale dry-run leaves outputs absent");
		ok &= expect(cli(rotateCommand, 0) && document.load(rotated, &error) &&
						 std::abs(document.mesh().surfaces[0].frames[0].positions[0].z - 18.4f) < 0.0001 &&
						 document.mesh().surfaces[0].frames[1].positions[0].z == 8,
					 "CLI shares pivot reference and transform snapping with authoring");
		for (const QStringList &options : QList<QStringList>{{"--snap-angle", "181"},
															 {"--snap-scale", "nan"},
															 {"--pivot-mode", "wrong"},
															 {"--pivot-mode", "selection", "--pivot", "0,0,0"},
															 {"--pivot-mode", "origin", "--pivot-frame", "1"}})
		{
			ok &= expect(
				cli(QStringList{"model", "edit", input, "--operation", "transform", "--faces", "all", "--output", rotated} + options, 2),
				"CLI refuses invalid or contradictory pivot and snap flags");
		}
	}
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
