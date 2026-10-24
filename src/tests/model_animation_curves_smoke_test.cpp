#include "cli/model_tools.h"
#include "tests/model_output_test_helpers.h"
#include "core/model_recovery.h"
#include "tests/model_animation_curves_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
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
bool expect(bool ok, const char *label)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
	return ok;
}
bool close(double a, double b, double tol = .0001)
{
	return std::abs(a - b) <= tol;
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("curves-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;
	QVector<ModelPoseCurveKey> keys{curveKey(0, 0), curveKey(4, 8)};
	expect(close(evaluateModelCurve(keys, 1), 2) && close(evaluateModelCurve(keys, -1), 0) && close(evaluateModelCurve(keys, 8), 8),
		   "linear evaluation and constant extrapolation");
	keys[0].interpolation = ModelCurveInterpolation::Step;
	expect(evaluateModelCurve(keys, 3.99) == 0 && evaluateModelCurve(keys, 4) == 8, "step transitions exactly at key");
	keys[0].interpolation = ModelCurveInterpolation::Cubic;
	keys[0].tangent = keys[1].tangent = ModelCurveTangent::Flat;
	expect(close(evaluateModelCurve(keys, 1), 1.25) && close(evaluateModelCurve(keys, 2), 4), "flat-tangent Hermite analytic values");
	keys[0].tangent = ModelCurveTangent::Free;
	keys[0].outSlope = 8;
	keys[1].tangent = ModelCurveTangent::Free;
	keys[1].inSlope = -8;
	expect(modelCurveRange(keys).second > 8, "cubic extrema detect between-key overshoot");
	keys = {curveKey(0, 0, ModelCurveInterpolation::Cubic), curveKey(1, 1, ModelCurveInterpolation::Cubic),
			curveKey(9, 2, ModelCurveInterpolation::Cubic)};
	bool monotone = true;
	double last = -1;
	for (int i = 0; i <= 900; ++i)
	{
		const double value = evaluateModelCurve(keys, i / 100.0);
		monotone = monotone && value >= last - 1e-9 && value >= 0 && value <= 2;
		last = value;
	}
	expect(monotone, "clamped unequal-spacing tangents remain monotone");
	const auto mesh = curveFixture();
	const auto beforeBytes = rigBytes(mesh);
	auto edit = captureCurves();
	ModelDocument doc;
	if (!expect(doc.setMesh(mesh, &error) && doc.edit(edit, &error), "capture joint animation"))
	{
		std::cerr << error.toStdString();
		return 1;
	}
	const auto captured = doc.mesh();
	expect(captured.skeleton.clips[0].curves.size() == 1 && captured.skeleton.clips[0].curves[0].channels[0].size() == 5,
		   "capture creates nine densely keyed channels");
	bool unchanged = true;
	for (int f = 0; f < mesh.frames.size(); ++f)
		for (int v = 0; v < mesh.surfaces[0].vertexCount; ++v)
			unchanged = unchanged && close(captured.surfaces[0].frames[f].positions[v].x, mesh.surfaces[0].frames[f].positions[v].x, 0) &&
						close(captured.surfaces[0].frames[f].positions[v].y, mesh.surfaces[0].frames[f].positions[v].y, 0);
	expect(unchanged, "capture preserves every existing baked position byte");
	expect(editableModelJson(captured)["version"].toInt() == 9 && editableModelJson(mesh)["version"].toInt() == 8,
		   "curves require source schema nine and retain legacy schema without curves");
	expect(doc.undo() && rigBytes(doc.mesh()) == beforeBytes && doc.redo() && rigBytes(doc.mesh()) == rigBytes(captured),
		   "capture undo and redo preserve source");
	auto reject = [&](const ModelMesh &source, ModelEdit request, const char *label)
	{
		auto candidate = source;
		const auto bytes = rigBytes(source);
		ModelSelection selection;
		selection.joint = "upper";
		expect(!applyModelEdit(&candidate, request, &selection, &error) && !error.isEmpty() && rigBytes(candidate) == bytes &&
				   selection.joint == "upper",
			   label);
	};
	reject(captured, edit, "capture refuses replacing existing keys implicitly");
	edit.curves.action = ModelCurveAction::Replace;
	edit.curves.group = captured.skeleton.clips[0].curves[0];
	edit.curves.group.channels[1] = {curveKey(0, 0), curveKey(4, 8)};
	if (!expect(doc.edit(edit, &error), "sparse translation curve rebakes clip"))
	{
		std::cerr << error.toStdString();
		return 1;
	}
	const auto animated = doc.mesh();
	expect(close(animated.surfaces[0].frames[3].positions[2].y, 4) && close(animated.tags[3].origin.y, 4),
		   "analytic midpoint moves skinned vertices and attachments");
	expect(close(animated.skeleton.clips[0].frames[2][0].m[7], 0) && close(animated.skeleton.clips[0].frames[2][6].m[7], 6),
		   "ancestor stays fixed and descendant follows curve");
	auto layered = captured;
	auto childCapture = captureCurves();
	childCapture.curves.joint = "lower";
	expect(applyModelEdit(&layered, childCapture, nullptr, &error), "capture a child alongside parent curves");
	const auto childCurves = layered.skeleton.clips[0].curves[1];
	expect(applyModelEdit(&layered, edit, nullptr, &error) && layered.skeleton.clips[0].curves[1] == childCurves &&
			   close(layered.skeleton.clips[0].frames[2][2].m[7], 4),
		   "parent curve edits preserve child local curves while moving its world pose");
	const auto revision = doc.revisionFingerprint();
	expect(doc.edit(edit, &error) && doc.revisionFingerprint() == revision, "same curve replacement is a true no-op");
	expect(doc.undo() && rigBytes(doc.mesh()) == rigBytes(captured) && doc.redo(), "no-op consumes no history step");
	auto command = captureCurves();
	command.curves.action = ModelCurveAction::SetKey;
	command.curves.channel = 1;
	command.curves.frame = 2;
	command.curves.key = curveKey(2, 10, ModelCurveInterpolation::Cubic, ModelCurveTangent::Flat);
	expect(doc.edit(command, &error) && close(doc.mesh().surfaces[0].frames[3].positions[2].y, 10), "set key edits exact sampled pose");
	command.curves = {};
	command.curves.clip = 0;
	command.curves.joint = "upper";
	command.curves.action = ModelCurveAction::MoveKey;
	command.curves.channel = 1;
	command.curves.frame = 2;
	command.curves.newFrame = 1;
	expect(doc.edit(command, &error) && doc.mesh().skeleton.clips[0].curves[0].channels[1][1].frame == 1,
		   "move key retains values and tangents");
	command.curves.frame = 1;
	command.curves.newFrame = 4;
	reject(doc.mesh(), command, "occupied destination never silently merges keys");
	command.curves.action = ModelCurveAction::RemoveKey;
	command.curves.newFrame = 0;
	expect(doc.edit(command, &error) && rigBytes(doc.mesh()) == rigBytes(animated), "remove interior key restores sparse motion");
	auto rotated = edit;
	rotated.curves.group.channels[5] = {curveKey(0, 0), curveKey(4, 450)};
	auto candidate = captured;
	expect(applyModelEdit(&candidate, rotated, nullptr, &error) && close(candidate.surfaces[0].frames[5].positions[2].x, 0) &&
			   close(candidate.surfaces[0].frames[5].positions[2].y, 16),
		   "unwrapped 450-degree channel retains complete turn and final orientation");
	const double diagonal = -8 / std::sqrt(2.0);
	expect(close(candidate.surfaces[0].frames[3].positions[2].x, diagonal) &&
			   close(candidate.surfaces[0].frames[3].positions[2].y, 4 + diagonal),
		   "multi-turn midpoint follows Euler channel rather than shortest quaternion arc");
	auto scale = edit;
	scale.curves.group.channels[6] = {curveKey(0, 1), curveKey(4, 2)};
	candidate = captured;
	expect(applyModelEdit(&candidate, scale, nullptr, &error) && close(candidate.surfaces[0].frames[5].positions[2].x, 16),
		   "scale curve transforms descendants and skin");
	scale.curves.group.channels[6][0].interpolation = ModelCurveInterpolation::Cubic;
	scale.curves.group.channels[6][0].tangent = ModelCurveTangent::Free;
	scale.curves.group.channels[6][0].outSlope = -4;
	reject(captured, scale, "between-key negative scale is rejected even when keys are positive");
	auto stored = animated;
	auto rename = ModelEdit{};
	rename.kind = ModelEditKind::RenameJoint;
	rename.rig.joint = "upper";
	rename.rig.name = "renamed";
	expect(applyModelEdit(&stored, rename, nullptr, &error) && stored.skeleton.clips[0].curves[0].joint == "renamed",
		   "joint rename preserves named curves");
	auto pose = ModelEdit{};
	pose.kind = ModelEditKind::TransformSkeletalPose;
	pose.skeletal.clip = 0;
	pose.skeletal.joint = "upper";
	pose.skeletal.translation = {1, 0, 0};
	reject(animated, pose, "sampled pose edits cannot stale authoritative curves");
	auto resize = ModelEdit{};
	resize.kind = ModelEditKind::ResizeSkeletalClip;
	resize.skeletal.clip = 0;
	resize.skeletal.frameCount = 3;
	reject(animated, resize, "clip shrink cannot discard keys implicitly");
	auto bake = captureCurves();
	bake.curves.action = ModelCurveAction::Bake;
	candidate = animated;
	expect(applyModelEdit(&candidate, bake, nullptr, &error) && !hasModelAnimationCurves(candidate) &&
			   close(candidate.surfaces[0].frames[3].positions[2].y, 4),
		   "bake drops editable curves and preserves dense motion");
	expect(applyModelEdit(&candidate, pose, nullptr, &error), "baked curves permit later pose edits");
	auto uv = ModelEdit{};
	uv.kind = ModelEditKind::TransformUv;
	uv.selection.vertices = {0};
	uv.uvOffset = {.1f, .2f};
	candidate = animated;
	expect(applyModelEdit(&candidate, uv, nullptr, &error) && candidate.skeleton.clips[0].curves == animated.skeleton.clips[0].curves,
		   "UV workflow preserves curve metadata");
	auto malformed = animated;
	malformed.skeleton.clips[0].frames[0][1].m[3] += 1;
	expect(!validateEditableModel(malformed).isEmpty(), "stale curves are refused by general document validation");
	malformed = captured;
	malformed.surfaces[0].frames[3].positions[1].z = 2;
	reject(malformed, edit, "curve rebuild protects independent geometry");
	malformed = captured;
	malformed.surfaces[0].frames[3].normals[1] = {0, 1, 0};
	reject(malformed, edit, "curve rebuild protects independent normals");
	malformed = captured;
	malformed.tags[3].origin.z = 2;
	reject(malformed, edit, "curve rebuild protects independent linked attachments");
	malformed = captured;
	malformed.surfaces[0].frames[0].positions[0].z = 2;
	expect(applyModelEdit(&malformed, edit, nullptr, &error) && malformed.surfaces[0].frames[0].positions[0].z == 2,
		   "unaffected independent bind geometry survives");
	for (int bad = 0; bad < 7; ++bad)
	{
		auto invalid = edit;
		if (bad == 0)
			invalid.curves.clip = 99;
		if (bad == 1)
			invalid.curves.group.joint = "missing";
		if (bad == 2)
			invalid.curves.group.channels[0].clear();
		if (bad == 3)
			invalid.curves.group.channels[0][1].frame = 0;
		if (bad == 4)
			invalid.curves.group.channels[0][0].value = std::numeric_limits<double>::quiet_NaN();
		if (bad == 5)
			invalid.curves.group.channels[0][0].outSlope = 1000001;
		if (bad == 6)
			invalid.curves.group.channels[0][0].interpolation = ModelCurveInterpolation(9);
		reject(captured, invalid, "malformed curve edit refuses transaction");
	}
	auto unrelated = edit;
	unrelated.kind = ModelEditKind::Transform;
	reject(captured, unrelated, "curve options cannot leak into unrelated operations");
	auto shear = mesh;
	shear.skeleton.tags.clear();
	for (auto &frame : shear.skeleton.clips[0].frames)
		frame[1].m[1] = .4f;
	expect(bakeModelSkeleton(&shear), "prepare affine capture fixture");
	updateEditableModelMetadata(&shear);
	ModelPoseCurves group;
	expect(!captureModelPoseCurves(shear, 0, "upper", &group, &error), "capture refuses lossy shear decomposition");
	auto turning = mesh;
	turning.skeleton.tags.clear();
	for (int f = 0; f < 5; ++f)
	{
		ModelPoseCurves rotation;
		rotation.joint = "upper";
		const double values[9] = {0, 0, 0, 20, 80.0 + f * 5, 170.0 + f * 10, 1, 1, 1};
		for (int c = 0; c < 9; ++c)
			rotation.channels[c] = {curveKey(0, values[c])};
		turning.skeleton.clips[0].frames[f][1] = evaluateModelPoseCurves(rotation, 0);
	}
	expect(bakeModelSkeleton(&turning), "prepare gimbal-crossing sampled motion");
	updateEditableModelMetadata(&turning);
	expect(captureModelPoseCurves(turning, 0, "upper", &group, &error), "capture crosses gimbal pose without losing sampled rotations");
	if (!group.channels[5].isEmpty())
		expect(std::abs(group.channels[5].last().value - group.channels[5].first().value) < 180,
			   "Euler capture selects continuous nearby branches");
	for (bool quaternionRoundTrip : {false, true})
		for (double sign : {-1.0, 1.0})
		{
			for (int f = 0; f < 5; ++f)
			{
				ModelPoseCurves rotation;
				rotation.joint = "upper";
				const double pitches[5] = {89.99, 89.999, 90, 90.001, 90.01};
				const double values[9] = {0, 0, 0, 20, sign * pitches[f], 170.0 + f * 10, 1, 1, 1};
				for (int c = 0; c < 9; ++c)
					rotation.channels[c] = {curveKey(0, values[c])};
				turning.skeleton.clips[0].frames[f][1] = evaluateModelPoseCurves(rotation, 0);
				if (quaternionRoundTrip)
				{
					auto &matrix = turning.skeleton.clips[0].frames[f][1];
					matrix = modelJointMatrix(modelJointRotation(matrix), modelJointTranslation(matrix), modelJointScale(matrix));
				}
			}
			expect(bakeModelSkeleton(&turning), "prepare near-gimbal sampled motion");
			updateEditableModelMetadata(&turning);
			expect(captureModelPoseCurves(turning, 0, "upper", &group, &error), "capture retains rotations close to either gimbal pole");
			bool faithful = true;
			for (int f = 0; f < 5; ++f)
			{
				const auto parent = modelJointInverse(turning.skeleton.clips[0].frames[f][0]);
				const auto expected = modelJointMultiply(parent, turning.skeleton.clips[0].frames[f][1]);
				const auto actual = evaluateModelPoseCurves(group, f);
				for (int n = 0; n < 12; ++n)
					faithful = faithful && close(actual.m[n], expected.m[n], .000002);
			}
			expect(faithful, "near-gimbal capture reconstructs parent-local matrices precisely");
		}
	const auto json = editableModelJson(animated);
	ModelMesh parsed;
	expect(parseEditableModel(QJsonDocument(json).toJson(), &parsed, &error) && rigBytes(parsed) == rigBytes(animated),
		   "schema nine round trip preserves all curve metadata");
	auto badJson = json;
	badJson["version"] = 8;
	expect(!parseEditableModel(QJsonDocument(badJson).toJson(), &parsed, &error), "legacy schema cannot carry hidden curve data");
	ModelPoseCurves fromJson;
	auto groupJson = modelPoseCurvesJson(animated.skeleton.clips[0].curves[0]);
	expect(modelPoseCurvesFromJson(groupJson, 5, &fromJson, &error) && fromJson == animated.skeleton.clips[0].curves[0],
		   "standalone joint curve JSON round trip");
	auto object = groupJson["channels"].toObject();
	auto badKeys = object["ty"].toArray();
	auto badKey = badKeys[0].toArray();
	badKey[0] = .5;
	badKeys[0] = badKey;
	object["ty"] = badKeys;
	groupJson["channels"] = object;
	expect(!modelPoseCurvesFromJson(groupJson, 5, &fromJson, &error), "fractional source sample indices refused");
	const auto source = QDir(tmp.path()).filePath("source.mesh.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	expect(doc.setMesh(animated, &error) && doc.save(source, false, &error), "save curve source");
	ModelDocument reopened;
	expect(reopened.load(source, &error) && rigBytes(reopened.mesh()) == rigBytes(animated), "saved curves reopen exactly");
	ModelRecoverySnapshot recovered;
	const auto recovery = writeModelRecovery({animated, {}}, tmp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &recovered).isValid() && rigBytes(recovered.mesh) == rigBytes(animated),
		   "recovery retains curve metadata and sampled motion");
	for (const auto &format : QStringList{"iqm", "md3"})
	{
		const auto bytes = exportEditableModel(animated, format, 0, &error);
		const auto decoded = decodeModelMesh("curves." + format, bytes);
		expect(!bytes.isEmpty() && decoded.isValid() && close(decoded.surfaces[0].frames[3].positions[2].y, 4),
			   "native export consumes validated curve samples");
	}
	expect(!exportEditableModel(animated, "md5anim", 1, &error).isEmpty(), "MD5 clip exports keyed motion");
	int polls = 0;
	ModelWorkControl control;
	control.cancelled = [&] { return ++polls > 5; };
	candidate = captured;
	expect(!applyModelEdit(&candidate, edit, nullptr, &error, control) && rigBytes(candidate) == rigBytes(captured),
		   "cancellation is atomic");
	auto longClip = mesh;
	longClip.skeleton.clips[0].frames.fill(modelSkeletonBindPose(mesh.skeleton), 1023);
	expect(bakeModelSkeleton(&longClip), "prepare long curve fixture");
	updateEditableModelMetadata(&longClip);
	QElapsedTimer timer;
	timer.start();
	candidate = longClip;
	expect(applyModelEdit(&candidate, captureCurves(), nullptr, &error), "capture full preview frame budget");
	std::cout << "1023-sample capture: " << timer.elapsed() << " ms\n";
	const QStringList base{"vibestudio", "--cli", "model", "curves", source, "--clip", "0", "--joint", "upper"};
	auto result = cli::runModelCurves(base);
	expect(result.exitCode == 0 && result.payload["curves"].toArray().size() == 1, "CLI inspects full stored channels");
	const QStringList keyArgs{"--operation", "set-key",			"--channel", "ty",		  "--frame", "2",		 "--value",
							  "10",			 "--interpolation", "cubic",	 "--tangent", "flat",	 "--output", out};
	expect(cli::runModelCurves(base + keyArgs + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(out),
		   "CLI dry-run writes nothing");
	expect(cli::runModelCurves(base + keyArgs).exitCode == 0 && reopened.load(out, &error) &&
			   close(reopened.mesh().surfaces[0].frames[3].positions[2].y, 10),
		   "CLI key edit shares sampled-pose service");
	expect(cli::runModelCurves(base + keyArgs).exitCode == 1 &&
			   cli::runModelCurves(base + keyArgs + QStringList{"--overwrite"}).exitCode == 0,
		   "CLI overwrite policy is explicit");
	for (const auto &bad : QList<QStringList>{{"--output", out}, {"--frame", "0"}, {"--clip", "0"}, {"--json=true"}, {"--unknown", "1"}})
		expect(cli::runModelCurves(base + bad).exitCode == 2, "CLI rejects malformed or inapplicable inspect options");
	auto badNumber = base + keyArgs;
	badNumber[badNumber.indexOf("--value") + 1] = "nan";
	expect(cli::runModelCurves(badNumber).exitCode == 2, "CLI rejects nonfinite key values");
	expect(cli::runModelCurves(base + keyArgs + QStringList{"--in-slope", "1", "--overwrite"}).exitCode == 2,
		   "CLI slopes require explicit free tangents");
	const auto groupPath = QDir(tmp.path()).filePath("group.json");
	expect(write(groupPath, QJsonDocument(modelPoseCurvesJson(animated.skeleton.clips[0].curves[0])).toJson()),
		   "write replacement fixture");
	checkModelAuxiliaryOutput(base + QStringList{"--operation", "replace", "--curves", groupPath, "--output", out}, "--curves",
							  cli::runModelCurves, expect);
	expect(
		cli::runModelCurves(base + QStringList{"--operation", "replace", "--curves", groupPath, "--output", out, "--overwrite"}).exitCode ==
			0,
		"CLI accepts complete curve-group replacement");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), base.mid(1) + QStringList{"--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["curves"].toArray().size() == 1,
			   "application dispatches curves command");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
