#include "core/model_skin_bindings.h"
#include "core/model_document.h"
#include "core/package_draft.h"
#include "tests/model_skin_binding_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray serialized(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return 1;
	}
	QTemporaryDir temporary(QDir(root).filePath("skin-bindings-XXXXXX"));
	if (!temporary.isValid() || argc != 2)
	{
		return 1;
	}
	bool ok = true;
	QString error;
	const auto mesh = tests::skinBindingMesh();
	const auto original = serialized(mesh);
	ModelSkinBindingPlan plan;
	ok &= expect(validateEditableModel(mesh).isEmpty() && planModelSkinBindings(mesh, tests::skinBindings(), &plan, &error),
				 "skin matches every valid authored surface");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
		return 1;
	}
	ok &= expect(plan.assignments.size() == 2 && plan.assignments[0].engineName == "body" &&
					 plan.assignments[0].material == "models/new_body" && plan.assignments[1].material == "models/new_head" &&
					 plan.ignoredTags == QStringList{"tag_mount"} && plan.unusedSurfaces == QStringList{"other"},
				 "native suffix/case matching, quotes/comments and unconsumed records are explicit");
	const auto retainedPlan = modelSkinBindingPlanJson(plan);
	ModelSkinBindingPlan nativeNames;
	auto sharedNames = mesh;
	sharedNames.surfaces[1].name = QStringLiteral("body_A");
	ok &= expect(planModelSkinBindings(sharedNames, QByteArray::fromHex("efbbbf") + "body,models/shared\n", &nativeNames, &error) &&
					 nativeNames.assignments.size() == 2 && nativeNames.assignments[1].material == "models/shared",
				 "BOM and native nonnumeric suffix permit a binding shared by two engine surface names");
	auto embedded = mesh;
	embedded.mdl.enabled = true;
	ok &= expect(!planModelSkinBindings(embedded, tests::skinBindings(), &plan, &error) && modelSkinBindingPlanJson(plan) == retainedPlan,
				 "native MDL metadata cannot be silently replaced by shader bindings");
	const QVector<QByteArray> bad{{},
								  "body,models/body\n",
								  "body,models/body\nBODY,models/duplicate\nhead,models/head\n",
								  "body models/body\nhead,models/head\n",
								  "body,../outside\nhead,models/head\n",
								  "body,C:/outside\nhead,models/head\n",
								  "body,models\\body\nhead,models/head\n",
								  "body,\"unterminated",
								  "/* unfinished",
								  "body,\"\"\n",
								  QByteArray("body,models/body\0head,models/head", 32),
								  QByteArray(65537, ' '),
								  "body," + QByteArray(64, 'a') + "\nhead,models/head\n",
								  QByteArray("body,models/") + char(0xff) + "\n",
								  QByteArray("tag_one,\n").repeated(257) + "body,models/body\nhead,models/head\n",
								  "body,,models/body\nhead,models/head\n"};
	for (const auto &bytes : bad)
	{
		error.clear();
		ok &=
			expect(!planModelSkinBindings(mesh, bytes, &plan, &error) && !error.isEmpty() && modelSkinBindingPlanJson(plan) == retainedPlan,
				   "malformed, incomplete and oversized bindings retain the prior plan");
	}
	bool cancelled = false;
	ModelWorkControl control{[&] { return cancelled; },
							 [&](ModelWorkPhase phase, qint64 count, qint64) {
								 if (phase == ModelWorkPhase::Reading && count >= 256)
								 {
									 cancelled = true;
								 }
							 }};
	ok &= expect(!planModelSkinBindings(mesh, "/*" + QByteArray(4096, ' ') + "*/\n" + tests::skinBindings(), &plan, &error, control) &&
					 cancelled && modelSkinBindingPlanJson(plan) == retainedPlan,
				 "skin parsing polls cancellation inside long comments");
	ModelDocument document;
	ok &= expect(document.setMesh(mesh, &error), "open source before binding edit");
	ModelSelection selection;
	selection.tag = "tag_mount";
	document.setSelection(selection);
	ModelEdit edit;
	edit.kind = ModelEditKind::ApplySkinBindings;
	edit.selection = selection;
	edit.skinBindings = tests::skinBindings();
	ok &= expect(document.edit(edit, &error) && document.selection() == selection, "one whole-model skin edit retains tag selection");
	auto expected = mesh;
	expected.surfaces[0].skinPaths[0] = "models/new_body";
	expected.surfaces[1].skinPaths[0] = "models/new_head";
	updateEditableModelMetadata(&expected);
	ok &= expect(serialized(document.mesh()) == serialized(expected),
				 "only primary materials change; poses, normals, seams, tags, collision and alternates survive");
	ok &= expect(document.edit(edit, &error) && serialized(document.mesh()) == serialized(expected),
				 "reapplying identical skin assignments is a no-op");
	ok &= expect(document.undo() && serialized(document.mesh()) == original && !document.canUndo() && document.redo(),
				 "entire assignment is one undo/redo step");
	const auto changed = serialized(document.mesh());
	edit.skinBindings = "body,models/no_head\n";
	ok &= expect(!document.edit(edit, &error) && serialized(document.mesh()) == changed, "failed assignment is atomic");
	ModelMesh restored;
	ok &= expect(parseEditableModel(changed, &restored, &error) && serialized(restored) == changed,
				 "editable source round trip retains bindings");
	const auto native = exportEditableModel(document.mesh(), "md3", 0, &error);
	if (!expect(!native.isEmpty(), "native MD3 exports applied materials"))
	{
		std::cerr << error.toStdString() << '\n';
		return 1;
	}
	const auto integer = [&](qsizetype at) { return qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(native.constData() + at)); };
	qsizetype surfaceOffset = integer(100);
	for (const auto &path : {QByteArray("models/new_body"), QByteArray("models/new_head")})
	{
		const auto shaderOffset = surfaceOffset + integer(surfaceOffset + 92);
		ok &= expect(native.mid(shaderOffset, 64).split('\0').first() == path, "independent MD3 shader record contains the assigned path");
		surfaceOffset += integer(surfaceOffset + 104);
	}
	tests::SkinReader reader;
	reader.add("models/default.skin", tests::skinBindings());
	reader.add("models/default.skin", "body,models/second\nhead,models/new_head\n");
	ModelSkinBindingInput input{"unchanged", 42, "original"};
	ok &= expect(!readModelSkinBindings(reader, {"models/default.skin", -1}, &input, &error) && input.bytes == "original",
				 "ambiguous package name cannot select silently");
	ok &= expect(readModelSkinBindings(reader, {"models/default.skin", 1}, &input, &error) && input.entryIndex == 1 &&
					 input.bytes.contains("second"),
				 "exact occurrence is read and verified");
	reader.lateFailure = true;
	ok &= expect(!readModelSkinBindings(reader, {"models/default.skin", 0}, &input, &error) && input.entryIndex == 1,
				 "late package checksum failure cannot publish bytes");
	reader.lateFailure = false;
	ok &= expect(!readModelSkinBindings(reader, {"models/stale.skin", 0}, &input, &error), "entry/path pair rejects a stale selection");
	reader.add("models/oversized.skin", QByteArray(65537, ' '));
	ok &= expect(!readModelSkinBindings(reader, {"", 2}, &input, &error) && input.entryIndex == 1,
				 "oversized package skin cannot publish partial bytes");
	const auto source = temporary.filePath("source.mesh.json"), skin = temporary.filePath("default.skin"),
			   output = temporary.filePath("output.mesh.json");
	ok &= expect(write(source, original) && write(skin, tests::skinBindings()), "write original CLI fixture inputs");
	QJsonObject cliJson;
	const auto cli = [&](QStringList options, int expectedExit) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  QStringList{"--cli", "--settings-file", temporary.filePath("settings.ini"), "model", "skin", source} + options +
						  QStringList{"--json"});
		process.closeWriteChannel();
		if (!process.waitForFinished(30000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		cliJson = QJsonDocument::fromJson(bytes).object();
		if (process.exitCode() != expectedExit)
		{
			std::cerr << bytes.constData() << process.readAllStandardError().constData();
		}
		return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expectedExit && !cliJson.isEmpty();
	};
	const QStringList options{"--file", skin, "--output", output};
	ok &= expect(cli(options + QStringList{"--dry-run"}, 0) && !QFileInfo::exists(output) && cliJson.value("dryRun").toBool() &&
					 cliJson.value("bindings").toObject().value("assignments").toArray().size() == 2,
				 "real CLI dry run reports complete plan without writing");
	ok &=
		expect(cli(options, 0) && read(output) == changed && read(source) == original, "real CLI writes the same source as document edit");
	ok &= expect(cli(options, 1) && read(output) == changed, "existing destination needs explicit overwrite");
	ok &= expect(cli(options + QStringList{"--overwrite"}, 0), "explicit overwrite uses the guarded model writer");
	ok &= expect(cli(options + QStringList{"--unknown", "x"}, 2) && cli(options + QStringList{"--file", skin}, 2),
				 "CLI rejects unknown and repeated options");
	const auto assets = temporary.filePath("assets");
	ok &= expect(QDir().mkpath(assets) && write(QDir(assets).filePath("default.skin"), tests::skinBindings()), "prepare a folder package");
	ok &= expect(cli({"--package", assets, "--entry", "default.skin", "--output", output, "--overwrite"}, 0) && read(output) == changed,
				 "CLI folder-package handoff matches loose-file assignments");
	PackageStagingModel staging;
	PackageArchive package;
	ok &= expect(package.load(assets, &error) && staging.loadBaseArchive(package, &error), "open staged package");
	const QByteArray stagedBindings("body,models/staged_body\nhead,models/new_head\n");
	ok &= expect(staging.addBytes(stagedBindings, "default.skin", &error, PackageStageConflictResolution::ReplaceExisting),
				 "stage new bindings over the package's original skin");
	const auto draft = temporary.filePath("skin.vibepackage");
	ok &= expect(PackageDraft::save(draft, &staging, false, &error), "save a portable package draft");
	ok &= expect(cli({"--package", draft, "--entry-index", "0", "--output", output, "--overwrite", "--locale", "en"}, 0),
				 "CLI imports exact staged occurrence with global locale options");
	auto stagedExpected = expected;
	stagedExpected.surfaces[0].skinPaths[0] = "models/staged_body";
	updateEditableModelMetadata(&stagedExpected);
	ok &= expect(read(output) == serialized(stagedExpected) && read(QDir(assets).filePath("default.skin")) == tests::skinBindings(),
				 "saved draft consumes replacement bytes while retaining the base package");
	ok &= expect(
		cli({"--package", assets, "--entry", "default.skin", "--output", QDir(assets).filePath("protected.mesh.json"), "--dry-run"}, 2),
		"CLI cannot overwrite package input paths even on dry runs");
	std::cout << "Skin binding parser, transaction, native export, package reads and CLI checks completed.\n";
	return ok ? 0 : 1;
}
