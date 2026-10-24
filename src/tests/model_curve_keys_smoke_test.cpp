#include "cli/model_tools.h"
#include "core/model_recovery.h"
#include "tests/model_curve_keys_test_helpers.h"
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
bool near(double a, double b, double tolerance = .0001)
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
	QTemporaryDir tmp(QDir(root).filePath("curve-keys-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	QString error;
	double bound = 0;
	QVector<ModelPoseCurveKey> a{curveKey(0, 0, ModelCurveInterpolation::Cubic, ModelCurveTangent::Free),
								 curveKey(1, 0, ModelCurveInterpolation::Linear, ModelCurveTangent::Free)};
	a[0].outSlope = 1;
	a[1].inSlope = -1;
	const QVector<ModelPoseCurveKey> zero{curveKey(0, 0), curveKey(1, 0)};
	expect(modelCurveDifferenceBound(a, zero, 0, 1, &bound, &error) && near(bound, .25, 1e-12),
		   "continuous difference finds a peak between integer samples");
	auto tiny = a;
	tiny[0].outSlope = 1e-16;
	tiny[1].inSlope = -1e-16;
	expect(modelCurveDifferenceBound(tiny, zero, 0, 1, &bound, &error) && near(bound, 2.5e-17, 1e-30),
		   "small cubic extrema cannot disappear beneath a derivative cutoff");
	expect(near(modelCurveRange(tiny).second, 2.5e-17, 1e-30), "range validation retains small extrema");
	std::mt19937 random(0x43555256);
	std::uniform_real_distribution<double> values(-10, 10), slopes(-4, 4);
	bool bounded = true, symmetric = true;
	for (int trial = 0; trial < 128; ++trial)
	{
		QVector<ModelPoseCurveKey> first, second;
		for (auto *keys : {&first, &second})
		{
			int time = 0;
			for (int i = 0; i < 9; ++i)
			{
				auto key = curveKey(time, values(random), ModelCurveInterpolation(random() % 3), ModelCurveTangent(random() % 4));
				key.inSlope = slopes(random);
				key.outSlope = slopes(random);
				keys->append(key);
				time += 1 + int(random() % 3);
			}
		}
		double other = 0;
		bounded = bounded && modelCurveDifferenceBound(first, second, -1, 30, &bound, &error);
		symmetric = symmetric && modelCurveDifferenceBound(second, first, -1, 30, &other, &error) && near(bound, other, 1e-10);
		for (int sample = 0; sample <= 3100; ++sample)
		{
			const double time = -1 + sample * .01;
			bounded = bounded && std::abs(evaluateModelCurve(first, time) - evaluateModelCurve(second, time)) <= bound + 1e-10;
		}
	}
	expect(bounded, "analytic bounds contain dense independent samples across mixed knots, modes and tangents");
	expect(symmetric, "continuous difference bounds are symmetric");
	expect(modelCurveDifferenceBound(a, a, -2, 3, &bound, &error) && bound == 0,
		   "identical curves and held endpoints have zero difference");
	a[0].interpolation = ModelCurveInterpolation::Step;
	a[1].value = 3;
	expect(modelCurveDifferenceBound(a, zero, 0, 1, &bound, &error) && bound == 3, "comparison includes the right value of a final step");
	a[0].value = 5;
	a[1].value = 0;
	expect(modelCurveDifferenceBound(a, zero, .1, 1, &bound, &error) && bound == 5, "comparison includes the left limit of a step");
	expect(!modelCurveDifferenceBound(a, zero, 2, 1, &bound, &error), "reversed comparison range refuses");
	const auto mesh = curveKeysFixture();
	const auto bytes = rigBytes(mesh);
	if (!expect(mesh.skeleton.clips.size() == 1 && mesh.skeleton.clips[0].curves.size() == 2, "prepare two animated joint groups"))
		return 1;
	ModelDocument document;
	auto edit = curveKeysEdit(ModelCurveKeyAction::Retime, mesh);
	edit.curveKeys.factor = 2;
	if (!expect(document.setMesh(mesh, &error) && document.edit(edit, &error), "retime parent and child curves atomically"))
	{
		std::cerr << error.toStdString();
		return 1;
	}
	const auto retimed = document.mesh();
	expect(near(retimed.skeleton.clips[0].frames[4][1].m[7], 4) && near(retimed.skeleton.clips[0].frames[4][2].m[7], 6),
		   "both local motions combine in hierarchy order");
	expect(near(retimed.surfaces[0].frames[5].positions[2].y, 6) && near(retimed.tags[5].origin.y, 6),
		   "one rebake updates skin and linked attachments");
	expect(retimed.skeleton.clips[0].curves[0].channels[1][1].frame == 4, "retimed metadata retains new sample positions");
	expect(document.undo() && rigBytes(document.mesh()) == bytes && document.redo() && rigBytes(document.mesh()) == rigBytes(retimed),
		   "batch edit is one exact undo and redo step");
	const auto revision = document.revisionFingerprint();
	auto noop = curveKeysEdit(ModelCurveKeyAction::Retime, retimed);
	expect(document.edit(noop, &error) && document.revisionFingerprint() == revision, "identity retiming does not consume history");
	ModelCurveKeyReport report;
	expect(describeModelCurveKeyChanges(mesh.skeleton.clips[0].curves, retimed.skeleton.clips[0].curves, 9, edit.curveKeys.selection,
										&report, &error) &&
			   report.channels.size() == 2 && report.channels[0].maximumChange == 4,
		   "report measures whole-channel continuous change");
	auto reject = [&](const ModelMesh &source, ModelEdit bad, const char *label)
	{
		auto candidate = source;
		ModelSelection selection;
		selection.joint = "upper";
		expect(!applyModelEdit(&candidate, bad, &selection, &error) && !error.isEmpty() && rigBytes(candidate) == rigBytes(source) &&
				   selection.joint == "upper",
			   label);
	};
	for (int kind = 0; kind < 11; ++kind)
	{
		auto bad = edit;
		if (kind == 0)
			bad.curveKeys.factor = 0;
		if (kind == 1)
			bad.curveKeys.factor = -1;
		if (kind == 2)
			bad.curveKeys.offset = 9;
		if (kind == 3)
			bad.curveKeys.factor = std::numeric_limits<double>::infinity();
		if (kind == 4)
			bad.curveKeys.selection[0].samples = {0, 0};
		if (kind == 5)
			bad.curveKeys.selection[0].samples = {1};
		if (kind == 6)
			bad.curveKeys.selection.append(bad.curveKeys.selection[0]);
		if (kind == 7)
			bad.curveKeys.selection[0].joint = "missing";
		if (kind == 8)
			bad.curveKeys.selection[0].channel = 9;
		if (kind == 9)
			bad.curveKeys.selection.clear();
		if (kind == 10)
			bad.curveKeys.action = ModelCurveKeyAction(7);
		reject(mesh, bad, "invalid batch settings preserve the complete source");
	}
	auto rounded = edit;
	rounded.curveKeys.factor = .75;
	reject(mesh, rounded, "fractional sample times require explicit rounding");
	rounded.curveKeys.snap = true;
	auto candidate = mesh;
	expect(applyModelEdit(&candidate, rounded, nullptr, &error) && candidate.skeleton.clips[0].curves[0].channels[1][1].frame == 2 &&
			   candidate.skeleton.clips[0].curves[0].channels[1].last().frame == 3,
		   "nearest-sample rounding uses half away from zero");
	rounded.curveKeys.factor = .2;
	reject(mesh, rounded, "rounding cannot merge keys implicitly");
	auto cross = curveKeysEdit(ModelCurveKeyAction::Retime, mesh);
	cross.curveKeys.selection = {{"upper", 1, {2}}};
	cross.curveKeys.offset = 3;
	candidate = mesh;
	expect(applyModelEdit(&candidate, cross, nullptr, &error) && candidate.skeleton.clips[0].curves[0].channels[1].last().frame == 5,
		   "retiming may reorder keys without losing unrelated keys");
	auto duplicate = cross;
	duplicate.curveKeys.action = ModelCurveKeyAction::Duplicate;
	duplicate.curveKeys.offset = 4;
	candidate = mesh;
	expect(applyModelEdit(&candidate, duplicate, nullptr, &error) && candidate.skeleton.clips[0].curves[0].channels[1].size() == 4 &&
			   candidate.skeleton.clips[0].curves[0].channels[1][1].frame == 2,
		   "duplication preserves original keys");
	duplicate.curveKeys.offset = 2;
	reject(mesh, duplicate, "duplicate into an existing key refuses");
	auto removal = curveKeysEdit(ModelCurveKeyAction::Remove, mesh);
	removal.curveKeys.selection = {{"upper", 1, {0, 4}}};
	candidate = mesh;
	expect(applyModelEdit(&candidate, removal, nullptr, &error) && candidate.skeleton.clips[0].curves[0].channels[1].size() == 1 &&
			   near(candidate.surfaces[0].frames[1].positions[2].y, 4),
		   "multiple removal leaves a held single-key curve");
	removal.curveKeys.selection[0].samples = {0, 2, 4};
	reject(mesh, removal, "removing every key of a channel refuses");
	removal.curveKeys.selection[0].samples = {2};
	removal.curveKeys.factor = 2;
	reject(mesh, removal, "timing settings cannot leak into deletion");
	auto unrelated = edit;
	unrelated.kind = ModelEditKind::Transform;
	reject(mesh, unrelated, "batch settings cannot leak into mesh edits");
	auto freeGroups = mesh.skeleton.clips[0].curves;
	for (auto &group : freeGroups)
		for (auto &key : group.channels[1])
		{
			key.interpolation = ModelCurveInterpolation::Cubic;
			key.tangent = ModelCurveTangent::Free;
			key.inSlope = .5;
			key.outSlope = -.25;
		}
	const auto freeOriginal = freeGroups;
	expect(editModelCurveKeys(&freeGroups, 9, edit.curveKeys, &report, &error), "retime free cubic tangents");
	bool faithful = true;
	for (int g = 0; g < 2; ++g)
		for (int i = 0; i <= 400; ++i)
			faithful = faithful && near(evaluateModelCurve(freeGroups[g].channels[1], i / 50.0),
										evaluateModelCurve(freeOriginal[g].channels[1], i / 100.0), 1e-10);
	expect(faithful && freeGroups[0].channels[1][0].inSlope == .25,
		   "affine retiming preserves normalised cubic shape and scales free slopes");
	const auto dense = curveKeysFixture(true);
	auto reduction = curveKeysEdit(ModelCurveKeyAction::Reduce, dense, true);
	candidate = dense;
	expect(applyModelEdit(&candidate, reduction, nullptr, &error), "reduce captured motion across all joints and channels");
	bool reduced = true, positions = true;
	for (const auto &group : candidate.skeleton.clips[0].curves)
		for (const auto &channel : group.channels)
			reduced = reduced && channel.size() == 2;
	for (int f = 0; f < dense.frames.size(); ++f)
		for (int v = 0; v < dense.surfaces[0].vertexCount; ++v)
		{
			const auto &p = candidate.surfaces[0].frames[f].positions[v], &q = dense.surfaces[0].frames[f].positions[v];
			positions = positions && p.x == q.x && p.y == q.y && p.z == q.z;
		}
	expect(reduced && positions, "linear reduction keeps endpoints and exact sampled deformation");
	auto groups = dense.skeleton.clips[0].curves;
	groups.resize(1);
	auto &channel = groups[0].channels[1];
	channel = {curveKey(0, 0, ModelCurveInterpolation::Cubic, ModelCurveTangent::Free),
			   curveKey(1, 0, ModelCurveInterpolation::Cubic, ModelCurveTangent::Free),
			   curveKey(2, 0, ModelCurveInterpolation::Linear, ModelCurveTangent::Free)};
	channel[0].outSlope = 1;
	channel[1].inSlope = -1;
	channel[1].outSlope = 1;
	channel[2].inSlope = -1;
	ModelCurveKeyOptions options;
	options.action = ModelCurveKeyAction::Reduce;
	options.selection = {{"upper", 1, {0, 1, 2}}};
	options.tolerance[0] = .1;
	expect(editModelCurveKeys(&groups, 9, options, &report, &error) && groups[0].channels[1].size() == 3,
		   "reduction refuses a shortcut whose error exists only between samples");
	options.tolerance[0] = .25;
	expect(editModelCurveKeys(&groups, 9, options, &report, &error) && groups[0].channels[1].size() == 2 &&
			   near(report.channels[0].maximumChange, .25, 1e-12),
		   "reduction accepts the declared continuous error bound");
	groups = dense.skeleton.clips[0].curves;
	groups.resize(1);
	auto &steps = groups[0].channels[1];
	steps[3].interpolation = ModelCurveInterpolation::Step;
	options.selection = {{"upper", 1, {0, 1, 2, 3, 4, 5, 6, 7, 8}}};
	options.tolerance[0] = 100;
	expect(editModelCurveKeys(&groups, 9, options, &report, &error) &&
			   std::any_of(groups[0].channels[1].cbegin(), groups[0].channels[1].cend(),
						   [](const auto &k) { return k.frame == 3 && k.interpolation == ModelCurveInterpolation::Step; }) &&
			   near(evaluateModelCurve(groups[0].channels[1], 3.99), 3),
		   "step changes retain both adjacent anchors even at a large tolerance");
	groups = dense.skeleton.clips[0].curves;
	groups.resize(1);
	auto &boundary = groups[0].channels[1];
	boundary[1].value = 0;
	boundary[8].value = 11;
	boundary[1].interpolation = boundary[7].interpolation = ModelCurveInterpolation::Cubic;
	const auto boundaryOriginal = boundary;
	options.selection = {{"upper", 1, {2, 3, 4, 5, 6, 7}}};
	options.tolerance[0] = 1e-9;
	expect(editModelCurveKeys(&groups, 9, options, &report, &error) && groups[0].channels[1].size() == 5 &&
			   report.channels[0].maximumChange < 1e-9,
		   "reduction freezes boundary tangents to preserve unselected cubic segments");
	expect(groups[0].channels[1][0] == boundaryOriginal[0] && groups[0].channels[1][1] == boundaryOriginal[1] &&
			   groups[0].channels[1].last() == boundaryOriginal.last(),
		   "unselected keys retain their exact metadata");
	const auto groupBytes = QJsonDocument(modelClipCurvesJson(groups)).toJson();
	options.selection[0].samples = {2, 2};
	expect(!editModelCurveKeys(&groups, 9, options, &report, &error) && QJsonDocument(modelClipCurvesJson(groups)).toJson() == groupBytes,
		   "pure group operations fail atomically");
	auto modified = mesh;
	modified.surfaces[0].frames[5].positions[2].z = 3;
	reject(modified, edit, "batch rebake protects independent affected geometry");
	modified = mesh;
	modified.tags[5].origin.z = 3;
	reject(modified, edit, "batch rebake protects independent linked attachments");
	ModelWorkControl control;
	int polls = 0;
	control.cancelled = [&] { return ++polls > 5; };
	candidate = mesh;
	expect(!applyModelEdit(&candidate, edit, nullptr, &error, control) && rigBytes(candidate) == bytes,
		   "cancellation preserves source and curve metadata");
	auto large = dense.skeleton.clips[0].curves;
	large.resize(1);
	auto &largeKeys = large[0].channels[1];
	largeKeys.clear();
	options = {};
	options.action = ModelCurveKeyAction::Reduce;
	options.selection = {{"upper", 1, {}}};
	options.tolerance[0] = 1e-8;
	for (int i = 0; i < 50001; ++i)
	{
		largeKeys.append(curveKey(i, i * .125));
		options.selection[0].samples.append(i);
	}
	QElapsedTimer timer;
	timer.start();
	expect(editModelCurveKeys(&large, 50001, options, &report, &error) && large[0].channels[1].size() == 2,
		   "large linear curve reduces with bounded work");
	std::cout << "50,001-key reduction: " << timer.elapsed() << " ms\n";
	const auto source = QDir(tmp.path()).filePath("source.mesh.json"), out = QDir(tmp.path()).filePath("out.mesh.json");
	expect(document.setMesh(mesh, &error) && document.save(source, false, &error), "save batch source");
	expect(document.edit(edit, &error) && document.save(out, false, &error), "save retimed source");
	ModelDocument reopened;
	expect(reopened.load(out, &error) && rigBytes(reopened.mesh()) == rigBytes(retimed), "retimed curves reopen without a schema change");
	ModelRecoverySnapshot recovered;
	const auto recovery = writeModelRecovery({retimed, {}}, tmp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &recovered).isValid() && rigBytes(recovered.mesh) == rigBytes(retimed),
		   "recovery retains batch motion");
	for (const auto &format : QStringList{"iqm", "md3"})
	{
		const auto exported = exportEditableModel(retimed, format, 0, &error);
		const auto parsed = decodeModelMesh("retimed." + format, exported);
		expect(!exported.isEmpty() && parsed.isValid() && near(parsed.surfaces[0].frames[5].positions[2].y, 6),
			   "native export uses retimed dense motion");
	}
	expect(!exportEditableModel(retimed, "md5anim", 1, &error).isEmpty(), "MD5 animation exports retimed curves");
	const QStringList base{"vibestudio", "--cli", "model", "curve-keys", source, "--clip", "0", "--channel", "ty"};
	expect(cli::runModelCurveKeys(base).exitCode == 0 && cli::runModelCurveKeys(base).payload["channels"].toArray().size() == 2,
		   "CLI inspection spans matching joints");
	const auto arguments = base + QStringList{"--operation", "retime", "--factor", "2", "--output", out};
	expect(cli::runModelCurveKeys(arguments).exitCode == 1, "CLI overwrite remains explicit");
	const auto absent = QDir(tmp.path()).filePath("dry.mesh.json");
	auto dry = arguments;
	dry[dry.indexOf("--output") + 1] = absent;
	expect(cli::runModelCurveKeys(dry + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(absent),
		   "CLI dry-run validates without writing");
	expect(cli::runModelCurveKeys(arguments + QStringList{"--overwrite"}).exitCode == 0 && reopened.load(out, &error) &&
			   rigBytes(reopened.mesh()) == rigBytes(retimed),
		   "CLI shares the complete document operation");
	for (const auto &bad : QList<QStringList>{{"--clip", "0"},
											  {"--factor", "2"},
											  {"--joint", "missing"},
											  {"--samples", "3"},
											  {"--samples", "0,0"},
											  {"--samples", "0", "--first", "0"},
											  {"--snap=true"},
											  {"--channel", "ty"}})
		expect(cli::runModelCurveKeys(base + bad).exitCode != 0, "CLI refuses malformed or unmatched selectors and inapplicable options");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), base.mid(1) + QStringList{"--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["channels"].toArray().size() == 2,
			   "application dispatches batch key CLI");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
