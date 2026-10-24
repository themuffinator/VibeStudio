#include "cli/model_tools.h"
#include "core/model_recovery.h"
#include "tests/model_curve_transfer_test_helpers.h"
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
#include <random>
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
bool near(double a, double b, double tolerance = 1e-6)
{
	return std::abs(a - b) <= tolerance;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("curve-transfer-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;
	QVector<ModelPoseCurveKey> keys{curveKey(0, 0, ModelCurveInterpolation::Step), curveKey(4, 10, ModelCurveInterpolation::StepNext),
									curveKey(8, 20)};
	expect(evaluateModelCurve(keys, 3.999) == 0 && evaluateModelCurve(keys, 4) == 10 && evaluateModelCurve(keys, 4.001) == 20,
		   "step directions preserve exact knot values and both limits");
	auto other = keys;
	other[1].value = 99;
	double bound = 0;
	expect(modelCurveDifferenceBound(keys, other, 0, 8, &bound, &error) && bound == 89,
		   "continuous comparison includes isolated internal knot values");
	QVector<ModelPoseCurveKey> reversed;
	expect(sliceModelCurve(keys, 0, 8, 0, 1, true, 9, &reversed, &error) && reversed[0].interpolation == ModelCurveInterpolation::Step &&
			   reversed[1].interpolation == ModelCurveInterpolation::StepNext,
		   "reversal swaps segment directions and preserves isolated values");
	bool equivalent = true;
	for (int i = 0; i <= 8000; ++i)
		equivalent = equivalent && near(evaluateModelCurve(keys, i * .001), evaluateModelCurve(reversed, 8 - i * .001));
	expect(equivalent, "reversed step curve equals the source at knots and between samples");
	std::mt19937 random(0x43545246);
	std::uniform_real_distribution<double> value(-3, 3), slope(-2, 2);
	bool allSlices = true, allBounds = true;
	for (int trial = 0; trial < 96; ++trial)
	{
		QVector<ModelPoseCurveKey> original;
		for (int k = 0; k < 4; ++k)
		{
			auto key = curveKey(k * 3, value(random), ModelCurveInterpolation(random() % 4), ModelCurveTangent(random() % 4));
			key.inSlope = slope(random);
			key.outSlope = slope(random);
			original.append(key);
		}
		for (bool reverse : {false, true})
		{
			QVector<ModelPoseCurveKey> slice;
			allSlices = allSlices && sliceModelCurve(original, 1, 8, 2, 2, reverse, 19, &slice, &error);
			for (int k = 0; k <= 1400; ++k)
			{
				const double t = k * .01;
				allSlices = allSlices &&
							near(evaluateModelCurve(slice, 2 + t), evaluateModelCurve(original, reverse ? 8 - t / 2 : 1 + t / 2), 1e-9);
			}
			allBounds = allBounds && modelCurveDifferenceBound(original, original, 0, 9, &bound, &error) && bound == 0;
		}
	}
	expect(allSlices, "cropped affine/reversed curves preserve mixed modes and tangents including non-key boundaries");
	expect(allBounds, "mixed one-sided holds have zero self-difference");
	QVector<ModelPoseCurveKey> constant{curveKey(3, 4)};
	expect(sliceModelCurve(constant, 0, 8, 0, 1, true, 9, &reversed, &error) && reversed.size() == 3 &&
			   evaluateModelCurve(reversed, 4) == 4,
		   "crop includes held endpoint extrapolation");
	const auto saved = reversed;
	for (double factor : {0.0, -1.0, .3, std::numeric_limits<double>::infinity()})
		expect(!sliceModelCurve(keys, 0, 8, 0, factor, true, 9, &reversed, &error) && reversed == saved,
			   "invalid mapping refuses atomically");
	const auto mesh = curveTransferFixture();
	const auto bytes = rigBytes(mesh);
	auto edit = curveTransferEdit();
	ModelDocument document;
	if (!expect(mesh.skeleton.clips.size() == 2 && document.setMesh(mesh, &error) && document.edit(edit, &error),
				"transfer reversed range into an unkeyed clip"))
	{
		std::cerr << error.toStdString();
		return 1;
	}
	const auto moved = document.mesh();
	const int first = moved.skeleton.bakedClipForFrame.indexOf(1);
	expect(near(moved.skeleton.clips[1].frames[4][1].m[7], 6) && near(moved.skeleton.clips[1].frames[8][2].m[7], 4),
		   "parent-local channels combine in hierarchy order");
	expect(near(moved.surfaces[0].frames[first + 4].positions[2].y, 6) && near(moved.tags[first + 4].origin.y, 6),
		   "one rebake updates destination skin and linked attachments");
	expect(moved.skeleton.clips[0].curves == mesh.skeleton.clips[0].curves && moved.skeleton.clips[1].framesPerSecond == 48,
		   "source curves and destination rate remain unchanged");
	bool untouched = true;
	for (int f = 0; f < mesh.skeleton.clips[0].frames.size(); ++f)
		for (int j = 0; j < mesh.skeleton.joints.size(); ++j)
			untouched =
				untouched && modelJointMatrixDistance(mesh.skeleton.clips[0].frames[f][j], moved.skeleton.clips[0].frames[f][j]) == 0;
	expect(untouched, "source sampled poses are exact after transfer");
	expect(near(moved.skeleton.clips[1].frames[3][1].m[7], 0) && near(moved.skeleton.clips[1].frames[13][1].m[7], 0),
		   "destination keys outside the replaced interval retain their values");
	ModelCurveKeyReport report;
	expect(describeModelCurveTransfer(mesh, moved, edit.curveTransfer, &report, &error) && report.channels.size() == 2 &&
			   report.channels[0].selected == 5 && report.channels[0].before == 17,
		   "report includes captured destination and boundary key counts");
	expect(report.channels[0].maximumChange == 6, "report measures full destination channel change");
	expect(document.undo() && rigBytes(document.mesh()) == bytes && document.redo() && rigBytes(document.mesh()) == rigBytes(moved),
		   "transfer is one exact undo and redo transaction");
	auto identityEdit = edit;
	identityEdit.curveTransfer.targetClip = 0;
	identityEdit.curveTransfer.targetFirst = 2;
	identityEdit.curveTransfer.factor = 1;
	identityEdit.curveTransfer.reverse = false;
	document.setMesh(mesh, &error);
	const auto revision = document.revisionFingerprint();
	expect(document.edit(identityEdit, &error) && document.revisionFingerprint() == revision && !document.canUndo(),
		   "identity transfer does not create curves or history");
	auto overlap = identityEdit;
	overlap.curveTransfer.first = 0;
	overlap.curveTransfer.last = 4;
	expect(document.edit(overlap, &error) && near(document.mesh().skeleton.clips[0].frames[4][1].m[7], 2),
		   "overlapping same-clip copy reads the original source snapshot");
	auto dense = mesh;
	dense.skeleton.clips[0].curves.clear();
	expect(applyModelEdit(&dense, edit, nullptr, &error) && near(dense.skeleton.clips[1].frames[4][1].m[7], 6),
		   "unkeyed source motion is captured for transfer");
	auto all = edit;
	all.curveTransfer.joints.clear();
	all.curveTransfer.channels.clear();
	auto candidate = mesh;
	expect(applyModelEdit(&candidate, all, nullptr, &error) && candidate.skeleton.clips[1].curves.size() == mesh.skeleton.joints.size(),
		   "empty selectors transfer every joint and channel");
	auto reject = [&](const ModelMesh &source, ModelEdit invalid, const char *label)
	{
		auto copy = source;
		ModelSelection selection;
		selection.joint = "upper";
		expect(!applyModelEdit(&copy, invalid, &selection, &error) && !error.isEmpty() && rigBytes(copy) == rigBytes(source) &&
				   selection.joint == "upper",
			   label);
	};
	for (int n = 0; n < 12; ++n)
	{
		auto bad = edit;
		if (n == 0)
			bad.curveTransfer.sourceClip = -1;
		if (n == 1)
			bad.curveTransfer.targetClip = 9;
		if (n == 2)
			bad.curveTransfer.first = 7;
		if (n == 3)
			bad.curveTransfer.last = 9;
		if (n == 4)
			bad.curveTransfer.targetFirst = 12;
		if (n == 5)
			bad.curveTransfer.factor = .75;
		if (n == 6)
			bad.curveTransfer.factor = 0;
		if (n == 7)
			bad.curveTransfer.factor = std::numeric_limits<double>::quiet_NaN();
		if (n == 8)
			bad.curveTransfer.joints = {"missing"};
		if (n == 9)
			bad.curveTransfer.joints = {"upper", "upper"};
		if (n == 10)
			bad.curveTransfer.channels = {1, 1};
		if (n == 11)
			bad.curveTransfer.channels = {9};
		reject(mesh, bad, "invalid transfer settings preserve the complete source and selection");
	}
	auto mixed = edit;
	mixed.skeletal.joint = "upper";
	reject(mesh, mixed, "inapplicable sampled-pose settings refuse");
	mixed = edit;
	mixed.kind = ModelEditKind::EditCurveKeys;
	reject(mesh, mixed, "transfer settings cannot leak into another edit");
	auto tampered = mesh;
	tampered.surfaces[0].frames[first + 4].positions[2].z += 1;
	reject(tampered, edit, "independent mesh edits remain protected");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	candidate = mesh;
	expect(!applyModelEdit(&candidate, edit, nullptr, &error, cancelled) && rigBytes(candidate) == bytes,
		   "cancelled transfer preserves source");
	auto shear = mesh;
	shear.skeleton.clips[0].curves.clear();
	for (auto &pose : shear.skeleton.clips[0].frames)
		pose[1].m[1] = .5f;
	expect(bakeModelSkeleton(&shear), "prepare sampled shear fixture");
	updateEditableModelMetadata(&shear);
	reject(shear, edit, "automatic capture refuses local shear without discarding it");
	auto longClip = ikFixture();
	longClip.skeleton.clips[0].frames.fill(modelSkeletonBindPose(longClip.skeleton), 1023);
	for (int f = 0; f < 1023; ++f)
		for (auto &matrix : longClip.skeleton.clips[0].frames[f])
			matrix.m[7] += float(f) / 128;
	expect(bakeModelSkeleton(&longClip), "prepare long unkeyed animation fixture");
	updateEditableModelMetadata(&longClip);
	ModelEdit longEdit;
	longEdit.kind = ModelEditKind::TransferAnimationCurves;
	longEdit.curveTransfer = {0, 0, 0, 1022, 0, 1, true, {}, {}};
	const auto longBytes = rigBytes(longClip);
	bool editingReached = false;
	ModelWorkControl during;
	during.progress = [&](ModelWorkPhase phase, qint64 completed, qint64)
	{
		if (phase == ModelWorkPhase::Editing && completed >= 256)
			editingReached = true;
	};
	during.cancelled = [&] { return editingReached; };
	candidate = longClip;
	expect(!applyModelEdit(&candidate, longEdit, nullptr, &error, during) && editingReached && rigBytes(candidate) == longBytes,
		   "cancellation during curve work preserves long source and metadata");
	QElapsedTimer timer;
	timer.start();
	expect(applyModelEdit(&longClip, longEdit, nullptr, &error) && near(longClip.skeleton.clips[0].frames[0][0].m[7], 1022.0 / 128) &&
			   near(longClip.skeleton.clips[0].frames.last()[0].m[7], 0),
		   "long unkeyed clip reverses all joint channels");
	std::cout << "1,023-sample seven-joint transfer: " << timer.elapsed() << " ms\n";
	auto stepped = mesh;
	auto set = captureCurves();
	set.curves.action = ModelCurveAction::Replace;
	set.curves.group = stepped.skeleton.clips[0].curves[0];
	set.curves.group.channels[1] = {curveKey(0, 0, ModelCurveInterpolation::Step), curveKey(4, 8), curveKey(8, 0)};
	expect(applyModelEdit(&stepped, set, nullptr, &error), "prepare stepped source motion");
	auto reverseAll = edit;
	reverseAll.curveTransfer.first = 0;
	reverseAll.curveTransfer.last = 8;
	reverseAll.curveTransfer.targetFirst = 0;
	reverseAll.curveTransfer.factor = 1;
	reverseAll.curveTransfer.joints = {"upper"};
	expect(applyModelEdit(&stepped, reverseAll, nullptr, &error) && modelAnimationCurveVersion(stepped) == 10,
		   "step reversal uses schema 10 hold-next curves");
	for (const auto &format : QStringList{"iqm", "md3"})
	{
		const auto encoded = exportEditableModel(stepped, format, 0, &error);
		const auto decoded = decodeModelMesh("reversed." + format, encoded);
		expect(!encoded.isEmpty() && decoded.isValid() && near(decoded.surfaces[0].frames[first + 5].positions[2].y, 0, .01),
			   "native export retains the correct side of a reversed step");
	}
	auto heldGroups = mesh.skeleton.clips[0].curves;
	heldGroups[0].channels[1] = keys;
	ModelCurveKeyOptions reduce;
	reduce.action = ModelCurveKeyAction::Reduce;
	reduce.selection = {{"upper", 1, {0, 4, 8}}};
	reduce.tolerance = {1000000, 1000000, 1000000};
	expect(editModelCurveKeys(&heldGroups, 9, reduce, nullptr, &error) && heldGroups[0].channels[1] == keys,
		   "reduction keeps both hold directions and their isolated knot values");
	auto json = editableModelJson(stepped);
	ModelMesh parsed;
	expect(json["version"].toInt() == 10 && parseEditableModel(QJsonDocument(json).toJson(), &parsed, &error) &&
			   rigBytes(parsed) == rigBytes(stepped),
		   "schema 10 survives exact source round trip");
	json["version"] = 9;
	expect(!parseEditableModel(QJsonDocument(json).toJson(), &parsed, &error), "legacy version cannot silently accept hold-next data");
	expect(editableModelJson(moved)["version"].toInt() == 9, "ordinary transferred curves retain schema 9 compatibility");
	const auto path = QDir(tmp.path()).filePath("source.mesh.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	document.setMesh(stepped, &error);
	expect(document.save(out, false, &error), "save reversed source");
	ModelDocument reopened;
	expect(reopened.load(out, &error) && rigBytes(reopened.mesh()) == rigBytes(stepped), "reopen reversed curve source");
	ModelRecoverySnapshot snapshot;
	const auto recovery = writeModelRecovery({stepped, {}}, tmp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &snapshot).isValid() && rigBytes(snapshot.mesh) == rigBytes(stepped),
		   "recovery retains hold-next and transferred poses");
	for (const auto &format : QStringList{"iqm", "md3"})
	{
		const auto data = exportEditableModel(moved, format, 0, &error);
		const auto decoded = decodeModelMesh("transfer." + format, data);
		expect(!data.isEmpty() && decoded.isValid() && near(decoded.surfaces[0].frames[first + 4].positions[2].y, 6, .01),
			   "native export contains transferred sampled motion");
	}
	expect(!exportEditableModel(moved, "md5anim", first, &error).isEmpty(), "MD5 exports destination animation");
	document.setMesh(mesh, &error);
	expect(document.save(path, false, &error), "save CLI source");
	const QStringList base{
		"vibestudio", "--cli",	   "model",	  "curve-transfer", path,	   "--source-clip",	 "0",		  "--target-clip",
		"1",		  "--first",   "2",		  "--last",			"6",	   "--target-first", "4",		  "--factor",
		"2",		  "--reverse", "--joint", "upper",			"--joint", "lower",			 "--channel", "ty",
		"--output",	  out};
	expect(cli::runModelCurveTransfer(base).exitCode == 1, "CLI overwrite remains explicit");
	const auto dry = QDir(tmp.path()).filePath("dry.mesh.json");
	auto args = base;
	args[args.indexOf("--output") + 1] = dry;
	expect(cli::runModelCurveTransfer(args + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(dry),
		   "CLI dry-run validates without writing");
	expect(cli::runModelCurveTransfer(base + QStringList{"--overwrite"}).exitCode == 0 && reopened.load(out, &error) &&
			   rigBytes(reopened.mesh()) == rigBytes(moved),
		   "CLI uses the shared transfer transaction");
	auto rate = base;
	rate.removeAt(rate.indexOf("--factor") + 1);
	rate.removeAt(rate.indexOf("--factor"));
	rate << "--match-rate" << "--overwrite";
	expect(cli::runModelCurveTransfer(rate).exitCode == 0, "CLI can preserve duration using known clip rates");
	for (const auto &bad : QList<QStringList>{{"--source-clip", "0"},
											  {"--snap"},
											  {"--reverse=true"},
											  {"--match-rate"},
											  {"--joint", "upper"},
											  {"--channel", "ty"},
											  {"--channel", "w"}})
		expect(cli::runModelCurveTransfer(base + bad).exitCode != 0, "CLI rejects repeated, malformed and inapplicable settings");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), base.mid(1) + QStringList{"--overwrite", "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["reverse"].toBool(),
			   "application dispatches curve-transfer CLI");
	}
	auto sameSource = base;
	sameSource[sameSource.indexOf("--output") + 1] = path;
	expect(cli::runModelCurveTransfer(sameSource + QStringList{"--overwrite"}).exitCode != 0, "transfer refuses overwriting its input");
	expect(cli::runModelCurveTransfer(sameSource + QStringList{"--overwrite", "--dry-run"}).exitCode != 0, "transfer dry-run enforces source protection");
	sameSource[sameSource.indexOf("--output") + 1] = QDir(tmp.path()).filePath("./source.mesh.json");
	expect(cli::runModelCurveTransfer(sameSource + QStringList{"--overwrite"}).exitCode != 0, "transfer refuses source path aliases");
	expect(reopened.load(path, &error) && rigBytes(reopened.mesh()) == rigBytes(mesh), "transfer preserves source bytes on refusal");
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
