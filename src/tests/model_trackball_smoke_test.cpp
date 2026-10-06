#include "core/model_trackball.h"
#include "core/model_transform_axes.h"
#include "core/model_recovery.h"
#include "tests/model_collision_animation_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QQuaternion>
#include <QRandomGenerator>
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
bool near(ModelVec3 a, ModelVec3 b, double epsilon = .0002)
{
	return std::hypot(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z) < epsilon;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
QVector3D vector(ModelVec3 p)
{
	return {p.x, p.y, p.z};
}
ModelVec3 vector(QVector3D p)
{
	return {p.x(), p.y(), p.z()};
}
QVector3D lift(double x, double y)
{
	const double length = std::hypot(x, y);
	return length >= 1 ? QVector3D(float(x / length), float(y / length), 0)
					   : QVector3D(float(x), float(y), float(std::sqrt(1 - length * length)));
}
ModelTransformBasis basis(ModelVec3 angles)
{
	ModelTransformBasis result;
	result.world = false;
	for (auto &axis : result.axes)
		axis = rotateModelVector(axis, angles);
	return result;
}
bool math()
{
	bool ok = true;
	ModelTrackballDrag drag;
	ModelVec3 result;
	ok &= expect(beginModelTrackballDrag(0, 0, {}, {}, 0, &drag) && modelTrackballRotation(drag, .6, 0, &result) &&
					 near(rotateModelVector({0, 0, 1}, result), {.6f, 0, .8f}),
				 "centre-to-right rotation follows visible sphere");
	for (double x : {-1., 1.})
		ok &= expect(modelTrackballRotation(drag, x, 0, &result) && near(rotateModelVector({0, 0, 1}, result), {float(x), 0, 0}) &&
						 near(rotateModelVector({0, 1, 0}, result), {0, 1, 0}),
					 "both Euler Y singularities retain the correct rotation");
	ok &= expect(beginModelTrackballDrag(1, 0, {}, {}, 0, &drag) && modelTrackballRotation(drag, -1, 0, &result) &&
					 near(rotateModelVector({1, 0, 0}, result), {-1, 0, 0}) && near(rotateModelVector({0, 0, 1}, result), {0, 0, 1}),
				 "antipodal rim positions make a deterministic screen-plane half turn");
	ok &=
		expect(modelTrackballRotation(drag, 1e300, 0, &result) && near(result, {}), "distant finite positions project safely onto the rim");
	ok &= expect(beginModelTrackballDrag(0, 0, {}, {}, 15, &drag) && modelTrackballRotation(drag, .4, .3, &result),
				 "oblique snapped rotation resolves");
	const auto axis = QVector3D(-.3f, .4f, 0).normalized();
	const auto snapped = QQuaternion::fromAxisAndAngle(axis, 30);
	for (auto p : {ModelVec3{1, 0, 0}, ModelVec3{0, 1, 0}, ModelVec3{0, 0, 1}})
		ok &= expect(near(rotateModelVector(p, result), vector(snapped.rotatedVector(vector(p)))),
					 "snapping retains oblique rotation axis rather than snapping XYZ");
	ok &= expect(beginModelTrackballDrag(1, 0, {}, {}, 120, &drag) && modelTrackballRotation(drag, -1, 0, &result) &&
					 near(result, {0, 0, 120}),
				 "large snap steps choose the nearest multiple within the shortest-arc interval");
	ok &= expect(beginModelTrackballDrag(0, 0, {}, {}, 180, &drag) && modelTrackballRotation(drag, .2, 0, &result) && near(result, {}),
				 "a small snapped gesture is exactly neutral");
	// An independent Qt quaternion oracle checks screen-space rotations across
	// camera and model bases; it does not use the production Euler decomposition.
	QRandomGenerator random(293847);
	int comparisons = 0;
	for (int sample = 0; sample < 360; ++sample)
	{
		const double sx = random.generateDouble() * 3 - 1.5, sy = random.generateDouble() * 3 - 1.5;
		const double ex = random.generateDouble() * 5 - 2.5, ey = random.generateDouble() * 5 - 2.5;
		const auto view = basis({float(random.generateDouble() * 180), float(random.generateDouble() * 160 - 80), float(sample)});
		auto axes = basis({float(sample * 2), float(sample % 160 - 80), float(sample / 2)});
		if (sample % 3 == 0)
		{
			axes.axes[2].x *= -1;
			axes.axes[2].y *= -1;
			axes.axes[2].z *= -1;
		}
		ok &= expect(beginModelTrackballDrag(sx, sy, view, axes, 0, &drag) && modelTrackballRotation(drag, ex, ey, &result),
					 "arbitrary view and orthonormal model basis resolve");
		const auto quaternion = QQuaternion::rotationTo(lift(sx, sy), lift(ex, ey));
		ModelTransform transform;
		transform.basis = axes;
		transform.rotation = result;
		for (auto p : {ModelVec3{1, 0, 0}, ModelVec3{0, 1, 0}, ModelVec3{0, 0, 1}})
		{
			const auto local = modelBasisFromWorld(p, view);
			const auto expected = modelBasisToWorld(vector(quaternion.rotatedVector(vector(local))), view);
			ok &= expect(near(rotateModelTransformVector(p, transform), expected, .001),
						 "Euler result matches independent quaternion rotation");
			++comparisons;
		}
		const auto first = result;
		modelTrackballRotation(drag, -.15, .2, &result);
		ok &= expect(modelTrackballRotation(drag, ex, ey, &result) && result.x == first.x && result.y == first.y && result.z == first.z,
					 "absolute rotation is independent of intermediate samples");
		ok &= expect(modelTrackballRotation(drag, sx, sy, &result) && near(result, {}), "return to press position is exactly neutral");
	}
	const auto saved = drag;
	for (double grid : {-1., 1e-9, 181., std::numeric_limits<double>::infinity()})
		ok &= expect(!beginModelTrackballDrag(0, 0, {}, {}, grid, &drag) && drag.start == saved.start && drag.grid == saved.grid,
					 "invalid snap refuses without replacing drag state");
	for (double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
	{
		result = {7, 8, 9};
		ok &= expect(!beginModelTrackballDrag(value, 0, {}, {}, 0, &drag) && drag.start == saved.start &&
						 !modelTrackballRotation(drag, 0, value, &result) && near(result, {7, 8, 9}),
					 "nonfinite coordinates leave outputs intact");
	}
	auto invalid = ModelTransformBasis{};
	invalid.axes[2].z = -1;
	invalid.world = false;
	ok &= expect(!beginModelTrackballDrag(0, 0, invalid, {}, 0, &drag), "left-handed camera basis is refused");
	invalid.axes[2].z = 2;
	ok &= expect(!beginModelTrackballDrag(0, 0, {}, invalid, 0, &drag), "nonorthonormal transform basis is refused");
	drag.start = {};
	result = {7, 8, 9};
	ok &= expect(!modelTrackballRotation(drag, .2, .3, &result) && near(result, {7, 8, 9}), "corrupt drag state refuses atomically");
	beginModelTrackballDrag(0, 0, {}, {}, 0, &drag);
	QElapsedTimer timer;
	timer.start();
	for (int i = 0; i < 100000; ++i)
		ok &= modelTrackballRotation(drag, std::sin(i * .001), std::cos(i * .001), &result);
	std::cout << comparisons << " quaternion oracle comparisons; 100000 updates ms=" << timer.elapsed() << '\n';
	return ok;
}
bool workflows(const QString &directory, const QString &executable)
{
	const auto original = tests::collisionAnimationFixture();
	bool ok = true;
	QString error;
	for (int kind = 0; kind < 7; ++kind)
		for (auto space : {ModelTransformSpace::World, ModelTransformSpace::Selection, ModelTransformSpace::Custom})
			for (int frame : {-1, 1})
			{
				if (kind == 5 && frame == 1)
					continue;
				ModelEdit edit;
				edit.frame = frame;
				edit.axesFrame = space == ModelTransformSpace::Selection ? 1 : -1;
				edit.pivotFrame = 1;
				edit.transformSpace = space;
				if (space == ModelTransformSpace::Custom)
					edit.axisRotation = {35, -28, 73};
				edit.pivotMode = ModelTransformPivot::Custom;
				edit.pivot = {2, 3, 4};
				if (kind == 0)
					edit.selection.vertices = {0, 1};
				if (kind == 1)
					edit.selection.edges = {{0, 1}};
				if (kind == 2)
					edit.selection.faces = {0};
				if (kind == 3)
					edit.selection.surfaces = {0, 1};
				if (kind == 4)
				{
					edit.kind = ModelEditKind::TransformTag;
					edit.selection.tag = "tag_mount";
				}
				if (kind >= 5)
				{
					edit.kind = ModelEditKind::TransformCollisionBox;
					edit.selection.collision = kind == 5 ? "static" : "body";
				}
				ModelTransformBasis axes;
				if (!expect(resolveModelTransformAxes(original, edit, &axes, &error), "resolve shared selection axes", error))
				{
					ok = false;
					continue;
				}
				ModelTrackballDrag drag;
				ok &= expect(beginModelTrackballDrag(0, 0, basis({20, 35, 60}), axes, 15, &drag) &&
								 modelTrackballRotation(drag, .6, .3, &edit.rotation),
							 "authoring obtains snapped Euler transform");
				ModelDocument document;
				document.setMesh(original);
				document.setSelection(edit.selection);
				ok &= expect(document.edit(edit, &error) && document.isModified(), "shared document commits free rotation", error);
				const auto changed = bytes(document.mesh());
				const auto transform = ModelTransform{{}, edit.rotation, {1, 1, 1}, edit.pivot, axes};
				if (kind <= 3)
				{
					for (int f = 0; f < 3; ++f)
					{
						const auto before = original.surfaces[0].frames[f].positions[0];
						ok &= expect(near(document.mesh().surfaces[0].frames[f].positions[0],
										  frame < 0 || frame == f ? transformModelPoint(before, transform) : before),
									 "component and surface edits retain frame scope and fixed custom pivot");
					}
				}
				ok &= expect(changed != bytes(original) && document.undo() && bytes(document.mesh()) == bytes(original) &&
								 !document.canUndo() && document.redo() && bytes(document.mesh()) == changed,
							 "free rotation has one complete undo/redo step");
				if (kind == 3 && space == ModelTransformSpace::Custom && frame == -1)
				{
					const auto source = directory + "/before.mesh.json", output = directory + "/after.mesh.json";
					ModelDocument input;
					input.setMesh(original);
					ok &= expect(input.save(source, false, &error) && document.save(output, false, &error),
								 "save original and rotated sources", error);
					ModelDocument reopened;
					ok &= expect(reopened.load(output, &error) && bytes(reopened.mesh()) == changed, "rotation survives source reopen",
								 error);
					ModelRecoverySnapshot snapshot;
					snapshot.mesh = document.mesh();
					snapshot.selection = document.selection();
					snapshot.frame = 1;
					const auto path =
						writeModelRecovery(snapshot, directory + "/recovery", QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
					ModelRecoverySnapshot restored;
					ok &= expect(!path.isEmpty() && inspectModelRecovery(path, &restored).isValid() && bytes(restored.mesh) == changed &&
									 restored.selection.surfaces == edit.selection.surfaces,
								 "recovery retains free-rotated geometry and selection", error);
					const auto angles =
						QString("%1,%2,%3").arg(edit.rotation.x, 0, 'g', 9).arg(edit.rotation.y, 0, 'g', 9).arg(edit.rotation.z, 0, 'g', 9);
					QStringList command{
						"--cli",	"model", "edit",		source,	 "--operation",		  "transform", "--surfaces",	  "0,1",
						"--rotate", angles,	 "--pivot",		"2,3,4", "--transform-space", "custom",	   "--axis-rotation", "35,-28,73",
						"--output", output,	 "--overwrite", "--json"};
					QProcess process;
					process.setWorkingDirectory(directory);
					process.start(executable, command);
					const bool finished = process.waitForFinished(30000);
					const auto report = QJsonDocument::fromJson(process.readAllStandardOutput());
					ok &= expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && report.isObject() &&
									 reopened.load(output, &error) && bytes(reopened.mesh()) == changed,
								 "existing numeric CLI reproduces trackball output byte for byte", QString::fromUtf8(report.toJson()));
					ok &= expect(input.load(source) && bytes(input.mesh()) == bytes(original), "CLI retains input source");
				}
			}
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("trackball-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	bool ok = math();
	ok &= workflows(temporary.path(), QString::fromLocal8Bit(argv[1]));
	std::cout << checks << " trackball core/CLI checks\n";
	return ok ? 0 : 1;
}
