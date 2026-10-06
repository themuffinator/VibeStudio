#include "core/model_import_repair.h"
#include "core/model_fingerprint.h"
#include "tests/model_import_repair_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"
#include "tests/model_scale_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
int word(const QByteArray &bytes, int at)
{
	return qFromLittleEndian<qint32>(bytes.constData() + at);
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root) || argc < 2)
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("import-repair-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const QDir dir(temporary.path());
	const auto input = dir.filePath("damaged.mesh.json"), output = dir.filePath("repaired.mesh.json");
	const auto damaged = tests::damagedImportFixture(), expected = tests::expectedImportRepair();
	const auto bytes = tests::importRepairSource(damaged), repaired = tests::importRepairSource(expected);
	QString error;
	bool ok = expect(tests::writeImportRepairFixture(input, bytes), "write original fault fixture");
	ModelMesh strict = expected;
	ok &= expect(!parseEditableModel(bytes, &strict, &error) && tests::importRepairSource(strict) == repaired,
				 "ordinary import stays strict and leaves its output untouched");
	ModelImportRepairPlan plan;
	ok &= expect(prepareModelImportRepair(input, &plan, &error), "prepare explicit repair before source admission");
	if (!ok)
	{
		std::cerr << error.toStdString();
		return 1;
	}
	ok &= expect(plan.removedFaces == 4 && plan.rebuiltNormals == 3 && plan.fallbackNormals == 1 && plan.removedSeams == 2 &&
					 plan.changes.size() == 9,
				 "report includes every required removal and only unusable normals");
	ok &= expect(tests::importRepairSource(plan.mesh) == repaired,
				 "all surviving positions, UVs, normals, unused vertices, tags, materials, clips and collision remain exact");
	ok &= expect(plan.changes[0].kind == ModelImportRepairKind::RepeatedVertex && plan.changes[0].element == 2 &&
					 plan.changes[1].kind == ModelImportRepairKind::InvalidIndex && plan.changes[1].element == 3 &&
					 plan.changes[2].element == 4 && plan.changes[3].kind == ModelImportRepairKind::CollapsedFace &&
					 plan.changes[3].frame == 2,
				 "face report retains original indices and first collapsing pose");
	const auto report = modelImportRepairJson(plan);
	const auto first = report["surfaces"].toArray()[0].toObject();
	ok &= expect(first["removedFaces"].toArray().size() == 4 && first["rebuiltNormals"].toArray().size() == 3 &&
					 first["removedSeams"].toArray().size() == 2,
				 "JSON groups exact change indices by source surface and pose");
	ok &= expect(tests::readImportRepairFixture(input) == bytes && !QFileInfo::exists(output), "preparation never writes files");
	ModelImportRepairPlan repeated;
	ok &= expect(prepareModelImportRepair(input, &repeated, &error) && modelImportRepairJson(repeated) == report &&
					 tests::importRepairSource(repeated.mesh) == repaired,
				 "repeated preparation is deterministic");
	ModelDocument document;
	ok &= expect(document.setMesh(expected, &error), "prepare destination sentinel document");
	const auto sentinel = document.revisionFingerprint();
	for (int stage = 0; stage < 3; ++stage)
	{
		bool cancel = stage == 0;
		ModelImportRepairPlan untouched;
		untouched.removedFaces = 777;
		ModelWorkControl control{[&] { return cancel; },
								 [&](ModelWorkPhase phase, qint64 completed, qint64 total) {
									 if ((stage == 1 && phase == ModelWorkPhase::Editing && completed == 1 && total == 2) ||
										 (stage == 2 && phase == ModelWorkPhase::Validating && completed == 1 && total == 1))
										 cancel = true;
								 }};
		ok &= expect(!prepareModelImportRepair(input, &untouched, &error, control) && cancel && untouched.removedFaces == 777 &&
						 untouched.mesh.surfaces.isEmpty(),
					 "cancel before decode, after a repaired surface or before publication preserves the previous plan");
	}
	bool cancel = false;
	ModelWorkControl stopCommit{[&] { return cancel; },
								[&](ModelWorkPhase phase, qint64, qint64) {
									if (phase == ModelWorkPhase::Committing)
										cancel = true;
								}};
	ok &= expect(!saveModelImportRepair(plan, output, &document, &error, stopCommit) && cancel && !QFileInfo::exists(output) &&
					 document.revisionFingerprint() == sentinel,
				 "cancel before publication leaves files and editor document untouched");
	ok &= expect(!saveModelImportRepair(plan, input, &document, &error), "repair cannot overwrite its input");
	ok &= expect(tests::writeImportRepairFixture(output, "sentinel") && !saveModelImportRepair(plan, output, &document, &error) &&
					 tests::readImportRepairFixture(output) == "sentinel",
				 "repair cannot overwrite an unrelated destination");
	const auto changedOutput = dir.filePath("changed.mesh.json");
	ok &= expect(tests::writeImportRepairFixture(input, bytes + "\n") && !saveModelImportRepair(plan, changedOutput, &document, &error) &&
					 !QFileInfo::exists(changedOutput),
				 "input changed since review blocks publication");
	ok &= expect(tests::writeImportRepairFixture(input, bytes), "restore owned source fixture");
	for (const auto phase : {ModelWorkPhase::Writing, ModelWorkPhase::Committing})
	{
		bool changed = false;
		ModelWorkControl race{{}, [&](ModelWorkPhase current, qint64 completed, qint64) {
								  if (!changed && current == phase && completed == 0)
									  changed = tests::writeImportRepairFixture(input, bytes + "\n");
							  }};
		ok &= expect(!saveModelImportRepair(plan, changedOutput, &document, &error, race) && changed && !QFileInfo::exists(changedOutput) &&
						 document.revisionFingerprint() == sentinel && error.contains("source changed"),
					 "input edits during writing or at the final commit checkpoint block publication and adoption");
		ok &= expect(tests::writeImportRepairFixture(input, bytes), "restore fixture after racing source change");
	}
	const auto saved = dir.filePath("accepted.mesh.json");
	bool committed = false;
	ModelWorkControl lateCancel{[&] { return committed; },
								[&](ModelWorkPhase phase, qint64 completed, qint64 total) {
									if (phase == ModelWorkPhase::Committing && completed == 1 && total == 1)
										committed = true;
								}};
	ok &= expect(saveModelImportRepair(plan, saved, &document, &error, lateCancel) && committed && document.path() == saved &&
					 !document.isModified() && !document.canUndo(),
				 "late cancellation after publication still adopts the ordinary saved editable source");
	ModelDocument reopened;
	ok &= expect(reopened.load(saved, &error) && tests::importRepairSource(reopened.mesh()) == repaired &&
					 tests::readImportRepairFixture(input) == bytes,
				 "repaired source reopens exactly without changing damaged input");
	ModelEdit edit;
	edit.selection.surface = 0;
	edit.selection.faces = {0};
	edit.translation = {0, 0, 2};
	ok &=
		expect(document.edit(edit, &error) && document.undo() && tests::importRepairSource(document.mesh()) == repaired && document.redo(),
			   "normal authoring and history operate after repaired import");
	for (int fault = 0; fault < 7; ++fault)
	{
		auto invalid = damaged;
		if (fault == 0)
			invalid.surfaces[0].frames[0].positions.removeLast();
		if (fault == 1)
			invalid.surfaces[0].frames[0].normals.removeLast();
		if (fault == 2)
			invalid.surfaces[0].skinPaths = {"../outside.pcx"};
		if (fault == 3)
			invalid.tags[0].axis[0] = 0;
		if (fault == 4)
			invalid.surfaces[0].triangles = {{0, 0, 1}};
		if (fault == 5)
			invalid.surfaces[0].texCoords[0].u = 2000000;
		if (fault == 6)
			invalid.surfaces[1].name = invalid.surfaces[0].name;
		const auto path = dir.filePath(QStringLiteral("fatal%1.mesh.json").arg(fault));
		ok &= expect(tests::writeImportRepairFixture(path, tests::importRepairSource(invalid)), "write blocking fault");
		ModelImportRepairPlan untouched;
		untouched.removedFaces = 777;
		ok &= expect(!prepareModelImportRepair(path, &untouched, &error) && !error.isEmpty() && untouched.removedFaces == 777,
					 "missing arrays, invalid metadata, all-face loss and unknown coordinates are not silently repaired");
	}
	for (const QString &format : QStringList{"mdl", "md2", "md3"})
	{
		QByteArray native;
		if (format == "mdl")
		{
			native = tests::groupedMdlFixture().bytes;
			for (int axis = 0; axis < 3; ++axis)
				native[native.size() - 12 + axis] = native[native.size() - 16 + axis];
		}
		else
		{
			auto mesh = tests::intersectionFixture(true);
			if (format == "md2")
			{
				mesh.surfaces.resize(1);
				mesh.tags.clear();
			}
			mesh.collisionBoxes.clear();
			mesh.animations.clear();
			updateEditableModelMetadata(&mesh);
			native = exportEditableModel(mesh, format, 0, &error);
			ok &= expect(!native.isEmpty(), "export valid synthetic native model before damage");
			if (native.isEmpty())
			{
				std::cerr << error.toStdString();
				return 1;
			}
			if (format == "md2")
			{
				const int triangle = word(native, 52), firstVertex = qFromLittleEndian<quint16>(native.constData() + triangle),
						  secondVertex = qFromLittleEndian<quint16>(native.constData() + triangle + 2),
						  pose = word(native, 56) + 2 * word(native, 16) + 40;
				for (int axis = 0; axis < 3; ++axis)
					native[pose + secondVertex * 4 + axis] = native[pose + firstVertex * 4 + axis];
			}
			else
			{
				const int surface = word(native, 100), triangle = surface + word(native, surface + 88),
						  firstVertex = word(native, triangle), secondVertex = word(native, triangle + 4),
						  pose = surface + word(native, surface + 100) + 2 * word(native, surface + 80) * 8;
				for (int axis = 0; axis < 6; ++axis)
					native[pose + secondVertex * 8 + axis] = native[pose + firstVertex * 8 + axis];
			}
		}
		const auto path = dir.filePath("damaged." + format);
		ok &= expect(tests::writeImportRepairFixture(path, native), "write native collapsed-pose fixture");
		const auto raw = decodeModelMesh(path, native);
		auto wanted = raw;
		wanted.surfaces[0].triangles.removeFirst();
		updateEditableModelMetadata(&wanted);
		ok &= expect(!importEditableModel(path, native, &strict, &error), "native normal import rejects collapse");
		ModelImportRepairPlan nativePlan;
		ok &=
			expect(prepareModelImportRepair(path, &nativePlan, &error) && nativePlan.removedFaces == 1 &&
					   nativePlan.changes[0].frame == 2 && tests::importRepairSource(nativePlan.mesh) == tests::importRepairSource(wanted),
				   "MDL/MD2/MD3 repair retains every native pose, skin, group, tag and authored attribute except the identified face");
		ok &= expect(tests::readImportRepairFixture(path) == native, "native source bytes remain exact");
	}
	auto warningMdl = tests::groupedMdlFixture();
	warningMdl.bytes[warningMdl.normalIndex] = char(255);
	const auto warningPath = dir.filePath("incomplete.mdl");
	ok &=
		expect(tests::writeImportRepairFixture(warningPath, warningMdl.bytes) && !prepareModelImportRepair(warningPath, &repeated, &error),
			   "preview fallback warnings cannot be laundered into an editable repair");
	auto excessive = tests::intersectionCrowd(modelDocumentMaxTriangles);
	excessive.surfaces[0].frames.fill(excessive.surfaces[0].frames[0], 129);
	excessive.frames.fill(excessive.frames[0], 129);
	updateEditableModelMetadata(&excessive);
	const auto excessivePath = dir.filePath("excessive.mesh.json");
	ok &= expect(tests::writeImportRepairFixture(excessivePath, tests::importRepairSource(excessive)) &&
					 !prepareModelImportRepair(excessivePath, &repeated, &error) && error.contains("face poses"),
				 "face-pose budget refuses excess before expensive repair");
	auto maximum = tests::maximumEditableGrid();
	for (auto &pose : maximum.surfaces[0].frames)
		pose.normals.fill({}, pose.normals.size());
	const auto maximumPath = dir.filePath("maximum.mesh.json");
	ok &= expect(tests::writeImportRepairFixture(maximumPath, tests::importRepairSource(maximum)), "write full frame-vertex-budget damage");
	QElapsedTimer timer;
	timer.start();
	ok &= expect(prepareModelImportRepair(maximumPath, &repeated, &error) && repeated.rebuiltNormals == modelDocumentMaxFrameVertices &&
					 repeated.fallbackNormals == 0 && repeated.removedFaces == 0 &&
					 modelStateFingerprint(repeated.mesh) == modelStateFingerprint(tests::maximumEditableGrid()),
				 "repair every normal at the document frame-vertex limit without changing any geometry");
	std::cout << "maximum animated grid repair: " << timer.elapsed() << " ms, " << repeated.rebuiltNormals << " rebuilt normals\n";
	const auto cli = [&](QStringList args, int expectedCode, QJsonObject *json = nullptr) {
		QProcess process;
		args.prepend("--cli");
		args.append("--json");
		process.start(QString::fromLocal8Bit(argv[1]), args);
		if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != expectedCode)
			return false;
		const auto payload = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		if (json)
			*json = payload;
		return !payload.isEmpty();
	};
	const QStringList base{"model", "repair-import", input};
	QJsonObject json;
	ok &= expect(cli(base, 0, &json) && json["removedFaces"] == 4 && json["surfaces"] == report["surfaces"] && !json["written"].toBool(),
				 "CLI read-only report matches reviewed core changes");
	QProcess textProcess;
	textProcess.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli"} + base);
	ok &= expect(textProcess.waitForFinished(60000) && textProcess.exitStatus() == QProcess::NormalExit && textProcess.exitCode() == 0 &&
					 QString::fromUtf8(textProcess.readAllStandardOutput()).contains(QString::fromUtf8("pose 2 · element 6 · +Z fallback")),
				 "CLI text report identifies the exact vertex receiving a fallback normal");
	const auto cliOutput = dir.filePath("cli.mesh.json");
	ok &= expect(cli(base + QStringList{"--output", cliOutput, "--dry-run"}, 0) && !QFileInfo::exists(cliOutput),
				 "CLI dry-run validates without writing");
	ok &= expect(cli({"model", "repair-import", "--input=" + input, "--output=" + cliOutput}, 0) &&
					 tests::readImportRepairFixture(cliOutput) == repaired,
				 "CLI inline selectors write the exact repaired copy");
	ok &= expect(cli(base + QStringList{"--output", cliOutput}, 1) && cli(base + QStringList{"--output", input}, 1),
				 "CLI protects both original and existing output");
	for (const auto &args : QVector<QStringList>{{"--dry-run"},
												 {"--overwrite"},
												 {"--output", "bad.md3"},
												 {"--frame", "all"},
												 {"--input", input},
												 {"--output", cliOutput, "--output", output}})
		ok &= expect(cli(base + args, 2), "unknown, repeated and incompatible CLI options are rejected");
	ok &= expect(cli({"model", "repair-import", dir.filePath("missing.mesh.json")}, 1), "CLI missing input is an IO error");
	ok &= expect(tests::readImportRepairFixture(input) == bytes, "all CLI paths preserve original source");
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " import repair core/CLI checks\n";
	return ok ? 0 : 1;
}
