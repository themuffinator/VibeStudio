#include "cli/model_tools.h"
#include "core/model_joint_tools.h"
#include "core/model_recovery.h"
#include "core/model_skinning_tools.h"
#include "tests/model_rig_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *label)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
	return value;
}
bool close(double a, double b)
{
	return std::abs(a - b) < .0001;
}
double weight(const ModelSurface &surface, int vertex, int joint)
{
	double value = 0;
	for (int i = surface.skinning.first[vertex]; i < surface.skinning.first[vertex] + surface.skinning.count[vertex]; ++i)
		if (surface.skinning.influences[i].joint == joint)
			value += surface.skinning.influences[i].weight;
	return value;
}
ModelMesh fixture()
{
	auto mesh = rigFixture();
	auto part = mesh.surfaces[0];
	part.skinning = {};
	part.name = "armour";
	part.index = 1;
	mesh.surfaces.append(part);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto temporaryRoot = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (temporaryRoot.isEmpty() || !QDir().mkpath(temporaryRoot))
		return 1;
	QTemporaryDir temporary(QDir(temporaryRoot).filePath("skinning-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto mesh = fixture();
	QString error;
	expect(validateEditableModel(mesh).isEmpty(), "mixed binding fixture is valid");
	ModelSelection selection;
	selection.surface = 1;
	ModelSkinReport report;
	expect(inspectModelSkinning(mesh, selection, 4, &report, &error) && report.vertices.size() == 3 && report.unbound == 3,
		   "inspect an unskinned surface without inventing weights");
	ModelSelection found;
	expect(selectModelSkinVertices(mesh, report, 1, ModelSkinFilter::Unbound, {}, 0, 1, &found, &error) && found.vertices.size() == 3,
		   "inspector produces exact unbound selection");
	ModelEdit edit;
	edit.kind = ModelEditKind::BindSkin;
	edit.selection = selection;
	edit.skin.method = ModelSkinBindingMethod::NearestJoints;
	ModelDocument document;
	expect(document.setMesh(mesh, &error) && document.edit(edit, &error), "bind additional surface with nearest-joint weights");
	if (!error.isEmpty())
		std::cerr << error.toStdString() << '\n';
	const auto bound = document.mesh();
	const auto &part = bound.surfaces[1];
	expect(close(weight(part, 0, 0), 1) && close(weight(part, 1, 1), 1), "joint hits bind rigidly to exact joint");
	expect(close(weight(part, 2, 0), 2. / 3) && close(weight(part, 2, 1), 1. / 3), "inverse-square distances give analytic weights");
	expect(close(part.frames[2].positions[1].y, 4) && close(part.frames[2].positions[2].y, 8 + 4. / 3),
		   "existing clip deforms newly bound surface");
	auto expectedSkeleton = mesh;
	for (auto &clip : expectedSkeleton.skeleton.clips)
	{
		clip.frameMins.clear();
		clip.frameMaxs.clear();
	}
	auto boundSkeletonJson = modelSkeletonJson(bound);
	auto expectedSkeletonJson = modelSkeletonJson(expectedSkeleton);
	boundSkeletonJson.remove("skinning");
	expectedSkeletonJson.remove("skinning");
	expect(boundSkeletonJson == expectedSkeletonJson, "binding retains joints, clip matrices and tags");
	for (int f = 0; f < mesh.frames.size(); ++f)
		for (int v = 0; v < 3; ++v)
			expect(close(bound.surfaces[0].frames[f].positions[v].x, mesh.surfaces[0].frames[f].positions[v].x) &&
					   close(bound.surfaces[0].frames[f].positions[v].y, mesh.surfaces[0].frames[f].positions[v].y),
				   "unselected surface remains intact");
	expect(document.undo() && rigBytes(document.mesh()) == rigBytes(mesh), "undo restores binding and geometry exactly");
	expect(document.redo() && rigBytes(document.mesh()) == rigBytes(bound), "redo restores bound source exactly");
	auto transformedBind = mesh;
	const float halfTurn = float(std::sqrt(.5));
	transformedBind.skeleton.joints[1].bind = modelJointMatrix({0, 0, halfTurn, halfTurn}, {8, 0, 0}, {2, 3, 4});
	transformedBind.skeleton.clips[0].frames[0][1] = transformedBind.skeleton.joints[1].bind;
	transformedBind.skeleton.clips[0].frames[1][1] =
		modelJointMultiply(modelJointMatrix({halfTurn, 0, 0, halfTurn}, {0, 4, 0}), transformedBind.skeleton.joints[1].bind);
	expect(applyModelEdit(&transformedBind, edit, nullptr, &error), "binding supports rotated nonuniform bind matrices");
	const auto &transformedSurface = transformedBind.surfaces[1];
	expect(close(transformedSurface.frames[0].positions[1].x, 8) && close(transformedSurface.frames[0].normals[1].z, 1) &&
			   close(transformedSurface.frames[2].positions[1].x, 8) && close(transformedSurface.frames[2].positions[1].y, 4) &&
			   close(transformedSurface.frames[2].normals[1].y, -1) && close(transformedSurface.frames[2].normals[1].z, 0),
		   "rotated scaled inverse bind preserves rest shape and gives analytic posed normals");
	expect(inspectModelSkinning(bound, selection, 1, &report, &error) && report.unbound == 0 && report.overLimit == 1 &&
			   report.noncanonical == 0,
		   "inspector identifies multi-influence vertex and canonical offsets");
	expect(selectModelSkinVertices(bound, report, 1, ModelSkinFilter::Weighted, "tip", .3, .4, &found, &error) &&
			   found.vertices == QSet<int>{2},
		   "weight range selects analytic vertex");
	expect(selectModelSkinVertices(bound, report, 1, ModelSkinFilter::OverLimit, {}, 0, 1, &found, &error) &&
			   found.vertices == QSet<int>{2},
		   "influence limit selection agrees with report");
	const auto unchanged = rigBytes(document.mesh());
	expect(!document.edit(edit, &error) && rigBytes(document.mesh()) == unchanged, "existing binding requires explicit replacement");
	edit.skin.replaceExisting = true;
	edit.skin.method = ModelSkinBindingMethod::NearestBones;
	expect(document.edit(edit, &error) && close(weight(document.mesh().surfaces[1], 2, 1), .5),
		   "nearest-bone binding measures parent-child segments");
	edit.skin.method = ModelSkinBindingMethod::Rigid;
	edit.skin.joints = {"tip"};
	expect(document.edit(edit, &error) && close(document.mesh().surfaces[1].frames[2].positions[0].y, 4),
		   "rigid binding follows chosen joint");
	edit.selection.vertices = {1};
	edit.skin.joints = {"root"};
	expect(document.edit(edit, &error) && close(weight(document.mesh().surfaces[1], 1, 0), 1) &&
			   close(weight(document.mesh().surfaces[1], 0, 1), 1),
		   "partial replacement leaves other vertex assignments intact");
	ModelEdit remove;
	remove.kind = ModelEditKind::UnbindSkin;
	remove.selection = selection;
	const auto posed = document.mesh();
	expect(document.edit(remove, &error) && document.mesh().surfaces[1].skinning.isEmpty(), "unbind removes only skinning");
	for (int f = 0; f < mesh.frames.size(); ++f)
		for (int v = 0; v < 3; ++v)
			expect(close(document.mesh().surfaces[1].frames[f].positions[v].y, posed.surfaces[1].frames[f].positions[v].y),
				   "unbind retains each baked pose");
	edit.selection.vertices.clear();
	edit.skin.replaceExisting = false;
	expect(!document.edit(edit, &error), "animated unskinned geometry is protected from replacement binding");
	ModelEdit reduce;
	reduce.kind = ModelEditKind::PruneSkinWeights;
	reduce.selection = selection;
	reduce.skin.maxInfluences = 1;
	expect(document.setMesh(bound, &error) && document.edit(reduce, &error) && close(weight(document.mesh().surfaces[1], 2, 0), 1),
		   "strongest influence retained and normalised");
	expect(document.mesh().surfaces[1].frames[0].positions[2].y == 8 && document.mesh().surfaces[1].frames[2].positions[2].y == 8,
		   "reduction preserves bind and updates deformation");
	reduce.skin.maxInfluences = 4;
	reduce.skin.minimumWeight = 1;
	expect(document.setMesh(bound, &error) && document.edit(reduce, &error) && document.mesh().surfaces[1].skinning.count[2] == 1,
		   "pruning always keeps a strongest influence when all weights miss threshold");
	auto offsets = bound;
	auto &skin = offsets.surfaces[1].skinning;
	const int at = skin.first[2];
	skin.influences[at].offset.x += 1;
	skin.influences[at + 1].offset.x -= 2;
	for (int f = 0; f < offsets.frames.size(); ++f)
	{
		ModelJointFrame pose;
		modelJointFrame(offsets, f, &pose);
		expect(skinModelSurface(offsets.surfaces[1], pose.pose, &offsets.surfaces[1].frames[f]), "rebuild imported offset fixture");
	}
	updateEditableModelMetadata(&offsets);
	expect(inspectModelSkinning(offsets, selection, 4, &report, &error) && report.noncanonical == 1, "inspect distinct imported offsets");
	expect(document.setMesh(offsets, &error) && !document.edit(reduce, &error), "weight reduction protects distinct imported offsets");
	ModelEdit repair;
	repair.kind = ModelEditKind::RebindSkin;
	repair.selection = selection;
	expect(document.edit(repair, &error), "explicit rebinding repairs distinct offsets while retaining bind shape");
	expect(inspectModelSkinning(document.mesh(), selection, 4, &report, &error) && report.noncanonical == 0 && report.unnormalised == 0,
		   "repaired binding is canonical and normalised");
	auto independent = mesh;
	independent.surfaces[1].frames[2].positions[0].y += 1;
	updateEditableModelMetadata(&independent);
	expect(document.setMesh(independent, &error), "independent frame fixture valid");
	const auto original = rigBytes(document.mesh());
	edit.skin = {};
	edit.skin.method = ModelSkinBindingMethod::NearestJoints;
	expect(!document.edit(edit, &error) && rigBytes(document.mesh()) == original, "refusal preserves independent frame and document");
	expect(document.setMesh(mesh, &error), "reset fixture");
	for (int limit : {0, 65})
	{
		edit.skin.maxInfluences = limit;
		expect(!document.edit(edit, &error), "invalid influence budget refused");
	}
	edit.skin = {};
	edit.skin.joints = {"tip", "tip"};
	expect(!document.edit(edit, &error), "duplicate joint pool refused");
	edit.skin.joints = {"missing"};
	expect(!document.edit(edit, &error), "unknown joint pool refused");
	edit.skin = {};
	edit.selection.vertices = {0};
	expect(!document.edit(edit, &error), "first binding must cover every vertex");
	edit.selection = {};
	edit.selection.joint = "root";
	expect(!document.edit(edit, &error), "joint-only selection cannot imply a binding target");
	edit.selection = {};
	edit.selection.surfaces = {0, 1};
	edit.skin.replaceExisting = true;
	expect(document.edit(edit, &error) && !document.mesh().surfaces[1].skinning.isEmpty(), "multi-surface binding commits atomically");
	const auto beforeCancel = rigBytes(document.mesh());
	ModelWorkControl cancel;
	cancel.cancelled = [] { return true; };
	auto candidate = document.mesh();
	expect(!applyModelSkinEdit(&candidate, edit, &error, cancel) && rigBytes(candidate) == beforeCancel,
		   "cancelled binding does not publish changes");
	const auto previousReport = report.vertices.size();
	bool cancelledDuringEdit = false;
	ModelWorkControl lateCancel;
	lateCancel.cancelled = [&] { return cancelledDuringEdit; };
	lateCancel.progress = [&](ModelWorkPhase phase, qint64 completed, qint64)
	{
		if (phase == ModelWorkPhase::Editing && completed > 0)
			cancelledDuringEdit = true;
	};
	candidate = document.mesh();
	expect(!applyModelEdit(&candidate, edit, nullptr, &error, lateCancel) && cancelledDuringEdit && rigBytes(candidate) == beforeCancel,
		   "cancellation after assigning weights preserves the published model");
	auto lateFailure = mesh;
	lateFailure.surfaces[1].frames[2].positions[0].y += 1;
	updateEditableModelMetadata(&lateFailure);
	const auto lateOriginal = rigBytes(lateFailure);
	expect(!applyModelEdit(&lateFailure, edit, nullptr, &error) && rigBytes(lateFailure) == lateOriginal,
		   "late surface refusal rolls back earlier surface binding");
	expect(!inspectModelSkinning(mesh, selection, 4, &report, &error, cancel) && report.vertices.size() == previousReport,
		   "cancelled inspection leaves output untouched");
	expect(inspectModelSkinning(bound, selection, 4, &report, &error), "fresh report for stale selection test");
	auto stale = bound;
	stale.surfaces[1].skinPaths = {"changed"};
	expect(!selectModelSkinVertices(stale, report, 1, ModelSkinFilter::Unbound, {}, 0, 1, &found, &error),
		   "stale report cannot select a changed source");
	auto duplicates = bound;
	auto &duplicateSkin = duplicates.surfaces[1].skinning;
	const int duplicateAt = duplicateSkin.first[2];
	duplicateSkin.influences[duplicateAt].weight *= .5f;
	duplicateSkin.influences.insert(duplicateAt, duplicateSkin.influences[duplicateAt]);
	++duplicateSkin.count[2];
	expect(inspectModelSkinning(duplicates, selection, 4, &report, &error) && report.duplicate == 1, "duplicate influence diagnostic");
	reduce.skin.maxInfluences = 64;
	reduce.skin.minimumWeight = 0;
	expect(document.setMesh(duplicates, &error) && document.edit(reduce, &error) && document.mesh().surfaces[1].skinning.count[2] == 2,
		   "normalisation merges duplicate entries without losing joint assignments");
	auto geometric = bound;
	for (auto &influence : geometric.surfaces[1].skinning.influences)
		influence.normalOffset = {};
	expect(document.setMesh(geometric, &error) && document.edit(reduce, &error), "normalisation supports geometric-normal bindings");
	for (const auto &influence : document.mesh().surfaces[1].skinning.influences)
		expect(influence.normalOffset.x == 0 && influence.normalOffset.y == 0 && influence.normalOffset.z == 0,
			   "weight reduction retains geometric normal policy");
	ModelDocument saved;
	expect(saved.setMesh(bound, &error), "prepare bound source");
	const auto input = QDir(temporary.path()).filePath("input.mesh.json");
	const auto output = QDir(temporary.path()).filePath("output.mesh.json");
	expect(saved.save(input, false, &error), "save authored binding");
	ModelDocument reopened;
	expect(reopened.load(input, &error) && rigBytes(reopened.mesh()) == rigBytes(bound), "saved source retains exact binding");
	const auto recovery =
		writeModelRecovery({bound, selection}, temporary.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	expect(!recovery.isEmpty() && inspectModelRecovery(recovery, &restored).isValid() && rigBytes(restored.mesh) == rigBytes(bound),
		   "binding survives recovery envelope");
	for (const auto &format : {QStringLiteral("md5mesh"), QStringLiteral("iqm"), QStringLiteral("md3")})
	{
		const auto bytes = exportEditableModel(bound, format, 0, &error);
		const auto decoded = decodeModelMesh("bound." + format, bytes);
		expect(!bytes.isEmpty() && decoded.isValid() && decoded.surfaces.size() == 2, "bound surfaces export and decode natively");
		if (format == "iqm")
		{
			ModelFrameGeometry posedGeometry;
			expect(decoded.skeleton.clips.size() == 1 &&
					   skinModelSurface(decoded.surfaces[1], decoded.skeleton.clips[0].frames[1], &posedGeometry, &error) &&
					   close(posedGeometry.positions[1].y, 4) && std::abs(posedGeometry.positions[2].y - (8 + 4. / 3)) < .02,
				   "IQM retained weights animate within native quantisation tolerance");
		}
	}
	const QStringList command{"vibestudio", "--cli", "model", "skinning", input};
	expect(cli::runModelSkinning(command + QStringList{"--surface", "1"}).exitCode == 0, "CLI reports bound influences");
	const auto selected = cli::runModelSkinning(command + QStringList{"--operation", "select", "--surface", "1", "--filter", "weighted",
																	  "--joint", "tip", "--minimum", "0.3", "--maximum", "0.4"});
	expect(selected.exitCode == 0 && selected.payload["selectedVertices"].toArray() == QJsonArray{2},
		   "CLI weight selection matches GUI service");
	const QStringList bindCommand = command + QStringList{"--operation", "bind", "--surface",		  "1",		  "--method", "rigid",
														  "--joint",	 "tip",	 "--replace-weights", "--output", output};
	expect(cli::runModelSkinning(bindCommand + QStringList{"--dry-run"}).exitCode == 0 && !QFile::exists(output),
		   "CLI dry run leaves output absent");
	expect(cli::runModelSkinning(bindCommand).exitCode == 0 && reopened.load(output, &error) &&
			   close(reopened.mesh().surfaces[1].frames[2].positions[0].y, 4),
		   "CLI writes reviewed rigid binding");
	expect(cli::runModelSkinning(bindCommand).exitCode != 0, "CLI refuses unapproved output replacement");
	for (const QStringList &invalid : {QStringList{"--operation", "bind", "--surface", "1", "--output", output, "--method", "rigid"},
									   {"--operation", "select", "--filter", "unbound", "--joint", "tip"},
									   {"--operation", "inspect", "--output", output},
									   {"--surface", "1", "--surface", "0"},
									   {"--operation", "unbind", "--surface", "1", "--falloff", "2", "--output", output},
									   {"--max-influences", "65"},
									   {"--operation", "select", "--filter", "weighted", "--joint", "tip", "--minimum", "nan"}})
		expect(cli::runModelSkinning(command + invalid).exitCode == 2, "CLI rejects missing, repeated or inapplicable options");
	if (argc > 1)
	{
		QProcess process;
		process.start(QString::fromLocal8Bit(argv[1]), bindCommand.mid(1) + QStringList{"--overwrite", "--dry-run", "--json"});
		expect(process.waitForFinished(30000) && process.exitCode() == 0 && process.readAllStandardOutput().contains("dryRun"),
			   "actual executable routes skinning CLI");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
