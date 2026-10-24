#include "cli/model_tools.h"
#include "core/model_recovery.h"
#include "tests/model_ik_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *name)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << name << '\n';
	}
	return ok;
}
bool close(double a, double b, double tol = .0001)
{
	return std::abs(a - b) <= tol;
}
bool point(ModelVec3 p, double x, double y, double z)
{
	return close(p.x, x) && close(p.y, y) && close(p.z, z);
}
ModelVec3 position(const ModelMesh &m, int j, int f = 0)
{
	return modelJointTranslation(m.skeleton.clips[0].frames[f][j]);
}
double length(ModelVec3 a, ModelVec3 b)
{
	return std::sqrt(std::pow(double(a.x) - b.x, 2) + std::pow(double(a.y) - b.y, 2) + std::pow(double(a.z) - b.z, 2));
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir tmp(QDir(root).filePath("ik-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	const auto mesh = ikFixture();
	const auto bytes = rigBytes(mesh);
	const auto edit = ikEdit();
	QString error;
	ModelDocument doc;
	if (!expect(doc.setMesh(mesh, &error) && doc.edit(edit, &error), "analytic IK commits"))
	{
		std::cerr << error.toStdString();
		return 1;
	}
	const auto solved = doc.mesh();
	expect(point(position(solved, 1), 0, 0, 0) && point(position(solved, 2), 0, 4, 0) && point(position(solved, 3), 4, 4, 0),
		   "law-of-cosines elbow and target positions");
	expect(point(position(solved, 4), -2, 0, 0) && point(position(solved, 5), 0, 6, 0) && point(position(solved, 6), 4, 6, 0),
		   "side branches follow nearest chain ancestor");
	expect(modelJointMatrixDistance(solved.skeleton.clips[0].frames[0][0], mesh.skeleton.clips[0].frames[0][0]) == 0,
		   "root ancestor remains byte exact");
	expect(point(solved.surfaces[0].frames[1].positions[2], 4, 4, 0), "skinned geometry follows analytic target");
	expect(point(solved.tags[1].origin, 4, 4, 0), "skeletal attachment follows target");
	expect(point(solved.surfaces[0].frames[0].positions[2], 8, 0, 0) && point(position(solved, 3, 1), 8, 0, 0),
		   "bind and unselected samples preserved");
	const auto result = modelIkSamples(solved, edit.ik);
	expect(result.size() == 1 && result[0].reached && result[0].error < .0001, "read-only diagnostic reports target reach");
	expect(doc.undo() && rigBytes(doc.mesh()) == bytes, "undo restores full source");
	expect(doc.redo() && rigBytes(doc.mesh()) == rigBytes(solved), "redo restores solved pose");
	const auto revision = doc.revisionFingerprint();
	expect(doc.edit(edit, &error) && doc.revisionFingerprint() == revision, "repeated IK is a true no-op");
	expect(doc.undo() && rigBytes(doc.mesh()) == bytes, "no-op consumes no undo step");
	auto reject = [&](const ModelMesh &source, ModelEdit e, const char *label)
	{
		auto candidate = source;
		const auto before = rigBytes(source);
		ModelSelection selection;
		selection.joint = "hand";
		expect(!applyModelEdit(&candidate, e, &selection, &error) && !error.isEmpty() && rigBytes(candidate) == before &&
				   selection.joint == "hand",
			   label);
	};
	auto request = edit;
	request.ik.target = {0, 4, 0};
	request.ik.pole = {4, 0, 0};
	auto candidate = mesh;
	expect(applyModelEdit(&candidate, request, nullptr, &error), "bent limb solves");
	const auto kept = candidate;
	request.ik.keepEndOrientation = false;
	candidate = mesh;
	expect(applyModelEdit(&candidate, request, nullptr, &error), "end orientation can follow lower bone");
	expect(close(kept.skeleton.clips[0].frames[0][3].m[0], 1) && !close(candidate.skeleton.clips[0].frames[0][3].m[0], 1),
		   "orientation policy changes hand basis only");
	expect(!close(position(kept, 6).x, position(candidate, 6).x) && close(length(position(candidate, 6), position(candidate, 3)), 2),
		   "end descendants inherit chosen orientation");
	request = edit;
	request.ik.pole = {8, 0, 0};
	candidate = mesh;
	expect(applyModelEdit(&candidate, request, nullptr, &error) && point(position(candidate, 2), 4, 0, 0),
		   "opposite pole chooses opposite elbow");
	request.ik.target = {-8, 0, 0};
	request.ik.pole = {0, 8, 0};
	candidate = mesh;
	expect(applyModelEdit(&candidate, request, nullptr, &error) && point(position(candidate, 2), -4, 0, 0) &&
			   point(position(candidate, 3), -8, 0, 0),
		   "antiparallel straight chain has deterministic half turn");
	request.ik.target = {0, 0, 0};
	candidate = mesh;
	expect(applyModelEdit(&candidate, request, nullptr, &error) && point(position(candidate, 2), 0, 4, 0) &&
			   point(position(candidate, 3), 0, 0, 0),
		   "equal bones fold completely towards pole");
	request.ik.pole = {0, 0, 0};
	reject(mesh, request, "folded chain requires distinct pole");
	request = edit;
	request.ik.pole = {8, 8, 0};
	reject(mesh, request, "bent solution refuses collinear pole");
	request = edit;
	request.ik.target = {20, 0, 0};
	reject(mesh, request, "unreachable target refused by default");
	request.ik.nearestReachable = true;
	candidate = mesh;
	expect(applyModelEdit(&candidate, request, nullptr, &error) && point(position(candidate, 3), 8, 0, 0),
		   "outer reach clamps without stretching");
	expect(modelIkSamples(candidate, request.ik).size() == 1 && !modelIkSamples(candidate, request.ik)[0].reached &&
			   close(modelIkSamples(candidate, request.ik)[0].error, 12),
		   "clamped no-op still reports nonzero residual");
	auto unequal = mesh;
	for (auto &pose : unequal.skeleton.clips[0].frames)
	{
		pose[3].m[3] = 6;
		pose[6].m[3] = 6;
	}
	expect(bakeModelSkeleton(&unequal), "prepare unequal-length fixture");
	updateEditableModelMetadata(&unequal);
	request = edit;
	request.ik.target = {0, 0, 0};
	reject(unequal, request, "inner reach cannot collapse unequal bones");
	request.ik.nearestReachable = true;
	candidate = unequal;
	expect(applyModelEdit(&candidate, request, nullptr, &error) && point(position(candidate, 3), 2, 0, 0) &&
			   close(length(position(candidate, 2), position(candidate, 3)), 2),
		   "inner reach uses stable original chain direction");
	request = edit;
	request.ik.last = 2;
	candidate = mesh;
	expect(applyModelEdit(&candidate, request, nullptr, &error), "range solves all selected samples atomically");
	for (int f = 0; f < 3; ++f)
		expect(point(position(candidate, 3, f), 4, 4, 0) && close(length(position(candidate, 1, f), position(candidate, 2, f)), 4) &&
				   close(length(position(candidate, 2, f), position(candidate, 3, f)), 4),
			   "fixed model target over moving roots preserves lengths");
	auto late = mesh;
	for (int j = 1; j < late.skeleton.joints.size(); ++j)
		late.skeleton.clips[0].frames[2][j].m[7] += 30;
	expect(bakeModelSkeleton(&late), "prepare later unreachable sample");
	updateEditableModelMetadata(&late);
	reject(late, request, "later failure cannot partially publish earlier solved samples");
	for (int field = 0; field < 7; ++field)
	{
		request = edit;
		if (field == 0)
			request.ik.clip = -1;
		if (field == 1)
			request.ik.last = 999;
		if (field == 2)
			request.ik.first = 1;
		if (field == 3)
			request.ik.endJoint = "Hand";
		if (field == 4)
			request.ik.endJoint = "upper";
		if (field == 5)
			request.ik.target.x = std::numeric_limits<float>::quiet_NaN();
		if (field == 6)
			request.ik.pole.z = 1000001;
		reject(mesh, request, "invalid IK input is refused atomically");
	}
	request = edit;
	request.kind = ModelEditKind::Transform;
	reject(mesh, request, "IK options rejected by unrelated operations");
	request = edit;
	request.skeletal.clip = 0;
	reject(mesh, request, "mixed pose options refused");
	auto malformed = mesh;
	malformed.skeleton.joints[2].parent = 3;
	reject(malformed, edit, "invalid skeleton refused before indexing");
	malformed = mesh;
	malformed.skeleton.clips[0].frames[0].clear();
	reject(malformed, edit, "missing pose matrices refused");
	malformed = mesh;
	malformed.skeleton.clips[0].frames[0][2] = malformed.skeleton.clips[0].frames[0][1];
	reject(malformed, edit, "zero-length bone refused");
	malformed = mesh;
	malformed.surfaces[0].frames[1].positions[1].z = 2;
	reject(malformed, edit, "independent affected geometry is protected");
	malformed = mesh;
	malformed.surfaces[0].frames[1].normals[1] = {0, 1, 0};
	reject(malformed, edit, "independent affected normals are protected");
	malformed = mesh;
	malformed.tags[1].origin.z = 2;
	reject(malformed, edit, "independent affected attachment is protected");
	candidate = mesh;
	candidate.surfaces[0].frames[0].positions[0].z = 2;
	expect(applyModelEdit(&candidate, edit, nullptr, &error) && candidate.surfaces[0].frames[0].positions[0].z == 2,
		   "independent unchanged bind geometry preserved");
	auto auxiliary = mesh;
	for (int frame = 0; frame < auxiliary.frames.size(); ++frame)
		auxiliary.tags.append({"manual", frame, {3, 2, 1}});
	auxiliary.collisionBoxes.append({"box", {1, 2, 3}, {4, 5, 6}, {0, 0, 0}});
	auxiliary.animations.append({"custom", 1, 2, 17});
	auxiliary.frames[1].name = "custom_pose";
	auxiliary.frames[1].origin = {1, 2, 3};
	candidate = auxiliary;
	const bool auxiliaryOk = applyModelEdit(&candidate, edit, nullptr, &error);
	if (!auxiliaryOk)
		std::cerr << "Auxiliary fixture: " << error.toStdString() << '\n';
	const auto manual =
		std::find_if(candidate.tags.begin(), candidate.tags.end(), [](const ModelTag &tag) { return tag.name == "manual"; });
	expect(auxiliaryOk && manual != candidate.tags.end() && point(manual->origin, 3, 2, 1) &&
			   point(candidate.collisionBoxes[0].centre, 1, 2, 3) && candidate.animations.last().name == "custom" &&
			   candidate.frames[1].name == "custom_pose" && point(candidate.frames[1].origin, 1, 2, 3),
		   "manual tracks and metadata preserved");
	auto affine = mesh;
	// Sheared joint bases are valid; rigid attachment axes cannot carry shear.
	affine.skeleton.tags.clear();
	for (auto &pose : affine.skeleton.clips[0].frames)
		for (auto &matrix : pose)
		{
			matrix.m[0] = 2;
			matrix.m[1] = .25f;
			matrix.m[5] = .75f;
		}
	expect(bakeModelSkeleton(&affine), "prepare scaled and sheared affine fixture");
	updateEditableModelMetadata(&affine);
	candidate = affine;
	const bool affineOk = applyModelEdit(&candidate, edit, nullptr, &error);
	if (!affineOk)
		std::cerr << "Affine fixture: " << error.toStdString() << '\n';
	expect(affineOk, "IK retains affine poses without TRS loss");
	bool gram = true;
	for (int j = 1; j < 7; ++j)
		for (int a = 0; a < 3; ++a)
			for (int b = 0; b < 3; ++b)
			{
				double before = 0, after = 0;
				for (int r = 0; r < 3; ++r)
				{
					before +=
						double(affine.skeleton.clips[0].frames[0][j].m[r * 4 + a]) * affine.skeleton.clips[0].frames[0][j].m[r * 4 + b];
					after += double(candidate.skeleton.clips[0].frames[0][j].m[r * 4 + a]) *
							 candidate.skeleton.clips[0].frames[0][j].m[r * 4 + b];
				}
				gram = gram && close(before, after);
			}
	expect(affineOk && gram, "affine Gram matrix preserved");
	expect(close(length(position(candidate, 2), position(candidate, 1)), 4), "scaled chain uses posed world lengths");
	bool sphere = true;
	for (int n = 1; n < 80; ++n)
	{
		request = edit;
		request.ik.target = {float(6 * std::cos(n * .23)), float(5 * std::sin(n * .23)), float(2 * std::sin(n * .79))};
		request.ik.pole = {0, 0, 9};
		candidate = mesh;
		sphere = sphere && applyModelEdit(&candidate, request, nullptr, &error) && modelIkSamples(candidate, request.ik)[0].reached &&
				 close(length(position(candidate, 1), position(candidate, 2)), 4) &&
				 close(length(position(candidate, 2), position(candidate, 3)), 4);
	}
	expect(sphere, "three-dimensional target sweep preserves both lengths and reaches targets");
	auto hidden = mesh;
	hidden.skeleton.clips.append(hidden.skeleton.clips.first());
	hidden.skeleton.clips[1].name = "outside_preview";
	for (auto &clip : hidden.skeleton.clips)
	{
		clip.frameMins.fill({-1, -1, -1}, 3);
		clip.frameMaxs.fill({9, 9, 9}, 3);
	}
	request = edit;
	request.ik.clip = 1;
	candidate = hidden;
	expect(applyModelEdit(&candidate, request, nullptr, &error) && modelIkSamples(candidate, request.ik)[0].reached &&
			   point(candidate.surfaces[0].frames[1].positions[2], 8, 0, 0),
		   "retained unpreviewed clip solves without changing displayed frames");
	expect(candidate.skeleton.clips[1].frameMins.isEmpty() && candidate.skeleton.clips[0].frameMins.size() == 3,
		   "only changed clip bounds are invalidated");
	auto longClip = mesh;
	longClip.skeleton.clips[0].frames.fill(modelSkeletonBindPose(mesh.skeleton), 1023);
	expect(bakeModelSkeleton(&longClip), "prepare 1023-sample animation");
	updateEditableModelMetadata(&longClip);
	request = edit;
	request.ik.last = 1022;
	QElapsedTimer timer;
	timer.start();
	candidate = longClip;
	const bool longOk = applyModelEdit(&candidate, request, nullptr, &error);
	const auto elapsed = timer.elapsed();
	expect(longOk && modelIkSamples(candidate, request.ik).size() == 1023 && modelIkSamples(candidate, request.ik).last().reached,
		   "full preview frame budget solves completely");
	std::cout << "1023-sample solve: " << elapsed << " ms\n";
	int polls = 0;
	ModelWorkControl control;
	control.cancelled = [&] { return ++polls > 5; };
	candidate = mesh;
	expect(!applyModelEdit(&candidate, edit, nullptr, &error, control) && rigBytes(candidate) == bytes,
		   "cancellation leaves source unchanged");
	const auto path = QDir(tmp.path()).filePath("in.mesh.json"), output = QDir(tmp.path()).filePath("out.mesh.json");
	expect(doc.setMesh(solved, &error) && doc.save(output, false, &error), "save IK authored source");
	ModelDocument reopened;
	expect(reopened.load(output, &error) && rigBytes(reopened.mesh()) == rigBytes(solved), "saved source reopens exactly");
	ModelRecoverySnapshot recovered;
	const auto recovery = writeModelRecovery({solved, {}}, tmp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &recovered).isValid() && rigBytes(recovered.mesh) == rigBytes(solved),
		   "recovery preserves solved animation");
	for (const auto &format : QStringList{"iqm", "md3"})
	{
		const auto encoded = exportEditableModel(solved, format, 0, &error);
		const auto decoded = decodeModelMesh("ik." + format, encoded);
		expect(!encoded.isEmpty() && decoded.isValid() && point(decoded.surfaces[0].frames[1].positions[2], 4, 4, 0),
			   "native round trip preserves analytic hand position");
	}
	expect(!exportEditableModel(solved, "md5anim", 1, &error).isEmpty(), "MD5 clip export accepts solved pose");
	expect(doc.setMesh(mesh, &error) && doc.save(path, false, &error), "prepare CLI source");
	const QStringList base{"vibestudio", "--cli",		"model", "ik",		 path,	  "--clip", "0",	 "--first",	 "0",	"--last",
						   "0",			 "--end-joint", "hand",	 "--target", "4,4,0", "--pole", "0,8,0", "--output", output};
	QFile::remove(output);
	auto cli = cli::runModelIk(base + QStringList{"--dry-run"});
	expect(cli.exitCode == 0 && !QFile::exists(output) && cli.payload["samples"].toArray()[0].toObject()["reached"].toBool(),
		   "CLI dry run exposes numeric result without writes");
	expect(cli::runModelIk(base).exitCode == 0 && reopened.load(output, &error) && rigBytes(reopened.mesh()) == rigBytes(solved),
		   "CLI shares exact document semantics");
	expect(cli::runModelIk(base).exitCode == 1 && cli::runModelIk(base + QStringList{"--overwrite"}).exitCode == 0,
		   "CLI overwrite policy enforced");
	for (const auto &bad :
		 QList<QStringList>{{"--clip", "0"}, {"--unknown"}, {"--json=true"}, {"--first", "-1"}, {"--nearest-reachable=true"}, {"extra"}})
		expect(cli::runModelIk(base + bad).exitCode == 2, "CLI rejects malformed and duplicate arguments");
	auto badVector = base;
	badVector[badVector.indexOf("--target") + 1] = "nan,0,0";
	expect(cli::runModelIk(badVector).exitCode == 2, "CLI rejects nonfinite coordinates");
	badVector = base;
	badVector[badVector.indexOf("--target") + 1] = "50,0,0";
	expect(cli::runModelIk(badVector + QStringList{"--overwrite"}).exitCode == 4, "CLI reports unreachable target as validation failure");
	cli = cli::runModelIk(badVector + QStringList{"--nearest-reachable", "--overwrite", "--dry-run"});
	expect(cli.exitCode == 0 && !cli.payload["samples"].toArray()[0].toObject()["reached"].toBool(),
		   "CLI explicit clamping reports unmet target");
	if (argc > 1)
	{
		QProcess process;
		auto args = base.mid(1) + QStringList{"--overwrite", "--dry-run", "--json"};
		process.start(QString::fromLocal8Bit(argv[1]), args);
		expect(process.waitForFinished(30000) && process.exitCode() == 0 &&
				   QJsonDocument::fromJson(process.readAllStandardOutput()).object()["samples"].toArray().size() == 1,
			   "application dispatches IK command");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
