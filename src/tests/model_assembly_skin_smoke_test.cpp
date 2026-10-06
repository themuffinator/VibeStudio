#include "core/model_assembly_animation.h"
#include "core/model_assembly_recovery.h"
#include "core/package_draft.h"
#include "tests/model_assembly_test_helpers.h"
#include "tests/model_skin_binding_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

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
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray hash(const QByteArray &bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
QByteArray serialized(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("assembly-skin-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const char *name) { return temporary.filePath(QString::fromLatin1(name)); };
	const auto modelBytes = serialized(tests::skinBindingMesh());
	const auto skinBytes = tests::skinBindings();
	const QByteArray blue("body,models/blue_body\nhead,models/blue_head\n");
	QString error;
	bool ok = expect(write(path("part.mesh.json"), modelBytes) && write(path("default.skin"), skinBytes), "write original model and skin");
	auto base = tests::assemblyRecipe("part.mesh.json");
	base.parts[1].tag = QStringLiteral("tag_mount");
	auto recipe = base;
	recipe.parts[0].skin = ModelAssemblySkin{"default.skin"};
	const ModelAssemblyContext context{temporary.path(), {}, {}};
	ModelAssemblyResolved resolved;
	ok &= expect(resolveModelAssembly(recipe, context, &resolved, &error), "resolve linked skin alongside untouched part");
	if (!ok)
	{
		std::cerr << error.toStdString();
		return 1;
	}
	const auto &input = resolved.inputs[0];
	ok &= expect(input.skin && input.skin->sha256 == hash(skinBytes) && input.sha256 == hash(modelBytes) &&
					 input.skin->bytes == skinBytes.size() && input.skin->entryIndex == -1,
				 "separate original input identities retained");
	ok &= expect(input.mesh.surfaces[0].skinPaths == QStringList{"models/new_body", "models/alternate"} &&
					 input.mesh.surfaces[1].skinPaths.first() == "models/new_head" && !resolved.inputs[1].skin &&
					 resolved.inputs[1].mesh.surfaces[0].skinPaths.first() == "models/old_body",
				 "per-part override retains alternate materials and sibling");
	auto restoredMesh = input.mesh;
	for (int i = 0; i < restoredMesh.surfaces.size(); ++i)
		restoredMesh.surfaces[i].skinPaths = resolved.inputs[1].mesh.surfaces[i].skinPaths;
	updateEditableModelMetadata(&restoredMesh);
	ok &= expect(serialized(restoredMesh) == serialized(resolved.inputs[1].mesh),
				 "every pose, normal, UV, tag and collision attribute remains exact");
	const auto receipt = modelAssemblySkinInputJson(*input.skin);
	ok &= expect(receipt.value("bindings").toObject().value("assignments").toArray().size() == 2 &&
					 input.skin->bindings.ignoredTags == QStringList{"tag_mount"} &&
					 input.skin->bindings.unusedSurfaces == QStringList{"other"},
				 "receipt exposes assignments and unused native records");
	ModelAssemblyPose pose;
	ok &= expect(sampleModelAssembly(recipe, resolved, .5, &pose, &error) && pose.mesh.surfaces[0].skinPaths.first() == "models/new_body" &&
					 pose.mesh.surfaces[2].skinPaths.first() == "models/old_body",
				 "animated composition uses independent effective materials");
	ModelAssemblyAnimation baked;
	ModelAssemblyAnimationOptions options;
	options.frameCount = 4;
	options.framesPerSecond = 4;
	ok &= expect(bakeModelAssemblyAnimation(recipe, resolved, options, &baked, &error) && baked.mesh.frames.size() == 4 &&
					 baked.mesh.surfaces[0].skinPaths == input.mesh.surfaces[0].skinPaths,
				 "animation bake carries skin materials and alternates");
	ok &= expect(exportModelAssemblyAnimation(recipe, resolved, options, path("baked.md3"), {}, false, false, &error),
				 "native bake writes skin material assignment");
	ModelMesh native;
	ok &= expect(importEditableModel("baked.md3", read(path("baked.md3")), &native, &error) &&
					 native.surfaces[0].skinPaths.first() == "models/new_body",
				 "native MD3 reopens with baked primary shader");
	const auto node = modelAssemblyJson(recipe);
	ModelAssembly parsed;
	ok &= expect(node.value("version") == 3 && parseModelAssembly(QJsonDocument(node).toJson(), &parsed, &error) &&
					 modelAssemblyFingerprint(parsed) == modelAssemblyFingerprint(recipe),
				 "schema 3 exactly round trips skin references");
	for (int version : {1, 2})
	{
		auto bad = node;
		bad.insert("version", version);
		ok &= expect(!parseModelAssembly(QJsonDocument(bad).toJson(), &parsed, &error) &&
						 modelAssemblyFingerprint(parsed) == modelAssemblyFingerprint(recipe),
					 "older schema rejects skin without replacing output");
	}
	for (const QJsonValue &value : QJsonArray{QJsonValue(), -2, 1.25, 2147483648.0, "0"})
	{
		auto bad = node;
		auto parts = bad.value("parts").toArray();
		auto part = parts[0].toObject();
		auto skin = part.value("skin").toObject();
		skin.insert("entryIndex", value);
		part.insert("skin", skin);
		parts[0] = part;
		bad.insert("parts", parts);
		ok &= expect(!parseModelAssembly(QJsonDocument(bad).toJson(), &parsed, &error), "invalid or unrepresentable occurrence rejected");
	}
	for (const auto &source : QStringList{"", "../bad.skin", "/bad.skin", "a\\b.skin", "C:/bad.skin", "a/./b.skin"})
	{
		auto bad = recipe;
		bad.parts[0].skin = ModelAssemblySkin{source, ModelAssemblySource::Package};
		ok &= expect(!validateModelAssembly(bad, &error), "unsafe package skin reference rejected structurally");
	}
	auto malformed = recipe;
	malformed.parts[0].skin->entryIndex = 0;
	ok &= expect(!validateModelAssembly(malformed, &error), "loose file cannot retain package occurrence");
	malformed.parts[0].skin = ModelAssemblySkin{"default.skin", static_cast<ModelAssemblySource>(42)};
	ok &= expect(!validateModelAssembly(malformed, &error), "unknown source kind rejected");
	ok &= expect(modelAssemblyJson(base).value("version") == 1, "ordinary recipes keep their schema");
	auto nativeRecipe = recipe;
	nativeRecipe.q3Animation = ModelAssemblyQ3Animation{};
	nativeRecipe.q3Animation->lowerPart = "root";
	nativeRecipe.q3Animation->upperPart = "child";
	ModelAssemblyResolved nativeResolved;
	ok &= expect(resolveModelAssembly(nativeRecipe, context, &nativeResolved, &error) &&
					 parseModelAssembly(QJsonDocument(modelAssemblyJson(nativeRecipe)).toJson(), &parsed, &error) && parsed.q3Animation &&
					 parsed.parts[0].skin,
				 "native animation and linked skins coexist in the saved assembly");
	const auto preserved = resolved;
	const auto preservedRecipe = resolved.recipeSha256;
	ok &= expect(write(path("default.skin"), blue) && sampleModelAssembly(recipe, resolved, 0, &pose, &error) &&
					 pose.mesh.surfaces[0].skinPaths.first() == "models/new_body",
				 "external edits cannot mutate an accepted snapshot");
	ok &= expect(resolveModelAssembly(recipe, context, &resolved, &error) && resolved.inputs[0].skin->sha256 == hash(blue) &&
					 resolved.inputs[0].mesh.surfaces[0].skinPaths.first() == "models/blue_body",
				 "reload adopts new verified skin bytes");
	const auto reloadedHash = resolved.inputs[0].skin->sha256;
	ok &= expect(write(path("default.skin"), "body,models/incomplete\n") && !resolveModelAssembly(recipe, context, &resolved, &error) &&
					 resolved.inputs[0].skin->sha256 == reloadedHash && resolved.recipeSha256 == preservedRecipe,
				 "invalid reload leaves output snapshot intact");
	ok &= expect(write(path("default.skin"), skinBytes), "restore valid fixture");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	ok &= expect(!resolveModelAssembly(recipe, context, &resolved, &error, cancelled) && resolved.inputs[0].skin->sha256 == reloadedHash,
				 "cancelled resolution preserves output and original source");
	malformed = recipe;
	malformed.parts[0].skin->source = "missing.skin";
	ok &= expect(validateModelAssembly(malformed) && !resolveModelAssembly(malformed, context, &resolved, &error),
				 "missing skin remains a repairable recipe but cannot preview or bake");
	for (bool dry : {false, true})
	{
		ok &= expect(!exportModelAssemblyPose(recipe, preserved, 0, path("default.skin"), {}, true, dry, &error) &&
						 !exportModelAssemblyAnimation(recipe, preserved, options, path("default.skin"), {}, true, dry, &error) &&
						 !exportModelAssemblyQ3Animation(nativeRecipe, nativeResolved, path("default.skin"), {}, true, dry, &error),
					 "all exporters protect linked skin inputs even with overwrite and dry run");
	}
	ModelAssemblyDocument document;
	ok &= expect(document.setAssembly(base, temporary.path(), &error), "begin ordinary assembly document");
	auto part = document.assembly().parts[0];
	part.skin = recipe.parts[0].skin;
	ok &= expect(document.setPart("root", part, &error) && document.canUndo() && document.undo() && !document.assembly().parts[0].skin &&
					 !document.canUndo() && document.redo(),
				 "one history step links and restores skin");
	ok &= expect(QDir().mkpath(path("moved")) && document.save(path("moved/saved.assembly.json"), false, &error),
				 "save rebases linked file references");
	ModelAssemblyDocument reopened;
	ok &= expect(reopened.load(path("moved/saved.assembly.json"), &error) &&
					 modelPathsReferToSameFile(reopened.assembly().parts[0].skin->source, path("default.skin")) &&
					 resolveModelAssembly(reopened.assembly(), {reopened.directory(), {}, {}}, &resolved, &error),
				 "reopen resolves rebased skin without changing original");
	ModelAssemblyRecoverySnapshot recovery;
	recovery.assembly = document.assembly();
	recovery.directory = document.directory();
	recovery.selectedPart = "root";
	recovery.sourcePath = document.path();
	recovery.sourceSha256 = document.sourceFingerprint();
	auto session = ModelAssemblyRecoverySession::acquire(path("recoveries"), "7c3f5e37-98f6-40a7-8527-fbab419f2113", &error);
	ok &= expect(session && session->write(recovery, &error), "checkpoint stores linked skin reference");
	if (!session)
		return 1;
	const auto record = inspectModelAssemblyRecovery(session->path());
	ModelAssemblyDocument recovered;
	ok &= expect(record.isValid() && restoreModelAssemblyRecovery(session->path(), record.sha256, &recovered, nullptr, &error) &&
					 recovered.assembly().parts[0].skin &&
					 resolveModelAssembly(recovered.assembly(), {recovered.directory(), {}, {}}, &resolved, &error),
				 "verified recovery retains material link through ordinary resolution");
	ok &= expect(session->retire(&error), "owned recovery copy retired");
	auto package = std::make_shared<tests::SkinReader>();
	package->add("models/default.skin", skinBytes);
	package->add("models/default.skin", blue);
	auto packaged = base;
	packaged.parts[0].skin = ModelAssemblySkin{"models/default.skin", ModelAssemblySource::Package};
	const ModelAssemblyContext packageContext{temporary.path(), package, {}};
	ok &= expect(!resolveModelAssembly(packaged, packageContext, &resolved, &error),
				 "ambiguous skin path never silently chooses an occurrence");
	packaged.parts[0].skin->entryIndex = 1;
	ok &= expect(resolveModelAssembly(packaged, packageContext, &resolved, &error) && resolved.inputs[0].skin->entryIndex == 1 &&
					 resolved.inputs[0].mesh.surfaces[0].skinPaths.first() == "models/blue_body",
				 "exact skin occurrence overrides materials");
	package->lateFailure = true;
	ok &= expect(!resolveModelAssembly(packaged, packageContext, &resolved, &error) && resolved.inputs[0].skin->sha256 == hash(blue),
				 "late payload verification failure preserves resolved output");
	package->lateFailure = false;
	packaged.parts[0].skin->source = "models/stale.skin";
	ok &= expect(!resolveModelAssembly(packaged, packageContext, &resolved, &error), "path guards stale package occurrence");
	QJsonObject output;
	const auto run = [&](QStringList args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  QStringList{"--cli", "--settings-file", path("settings.ini"), "model", "assembly"} + args + QStringList{"--json"});
		if (!process.waitForFinished(30000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		output = QJsonDocument::fromJson(bytes).object();
		if (process.exitCode() != code || output.isEmpty())
			std::cerr << args.join(' ').toStdString() << '\n' << bytes.toStdString() << process.readAllStandardError().toStdString();
		return process.exitStatus() == QProcess::NormalExit && process.exitCode() == code && !output.isEmpty();
	};
	const QStringList create{"--new",  "--part",	   "root",	   "--model",		   "part.mesh.json",
							 "--skin", "default.skin", "--output", "cli.assembly.json"};
	ok &= expect(run(create + QStringList{"--dry-run"}) && !QFileInfo::exists(path("cli.assembly.json")),
				 "CLI dry run validates complete skin without publishing");
	ok &= expect(run(create) && output.value("inputs").toArray()[0].toObject().value("skin").toObject().value("sha256") ==
									QString::fromLatin1(hash(skinBytes).toHex()),
				 "CLI links skin and reports original content identity");
	ok &= expect(
		run({"cli.assembly.json", "--operation", "update", "--part", "root", "--translation", "1,2,3", "--output", "cli.assembly.json"}) &&
			output.value("assembly").toObject().value("parts").toArray()[0].toObject().contains("skin"),
		"ordinary CLI part update retains skin reference");
	ok &= expect(run({"cli.assembly.json", "--operation", "bake-animation", "--frames", "3", "--sample-fps", "10", "--output", "cli.md3"}),
				 "CLI animation bake uses effective linked materials");
	ok &= expect(run({"cli.assembly.json", "--operation", "update", "--part", "root", "--clear-skin", "--output", "clear.assembly.json"}) &&
					 output.value("assembly").toObject().value("version") == 1 &&
					 !output.value("inputs").toArray()[0].toObject().contains("skin"),
				 "CLI clear returns to original materials and ordinary schema");
	for (const QStringList &extra : QVector<QStringList>{{"--clear-skin", "--skin", "default.skin"},
														 {"--skin-entry-index", "0"},
														 {"--skin", "default.skin", "--skin-entry-index", "0"},
														 {"--skin", "default.skin", "--skin-kind", "other"}})
		ok &= expect(
			run(QStringList{"cli.assembly.json", "--operation", "update", "--part", "root", "--output", "invalid.assembly.json"} + extra,
				2) &&
				!QFileInfo::exists(path("invalid.assembly.json")),
			"invalid CLI skin combinations cannot write output");
	ok &= expect(QDir().mkpath(path("assets")) && write(path("assets/default.skin"), skinBytes), "prepare staged package source");
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(archive.load(path("assets"), &error) && staging.loadBaseArchive(archive, &error) &&
					 staging.addBytes(blue, "default.skin", &error, PackageStageConflictResolution::ReplaceExisting) &&
					 PackageDraft::save(path("skins.vibepackage"), &staging, false, &error),
				 "save independent package draft with material replacement");
	ok &= expect(run({"cli.assembly.json", "--operation", "update", "--part", "root", "--skin", "default.skin", "--skin-kind", "package",
					  "--skin-entry-index", "0", "--package", "skins.vibepackage", "--output", "package.assembly.json"}) &&
					 output.value("inputs").toArray()[0].toObject().value("skin").toObject().value("sha256") ==
						 QString::fromLatin1(hash(blue).toHex()),
				 "CLI consumes verified staged skin occurrence instead of base bytes");
	ok &= expect(
		run({"package.assembly.json", "--package", "skins.vibepackage", "--operation", "bake", "--output", "assets/protected.md3"}, 2) &&
			!QFileInfo::exists(path("assets/protected.md3")),
		"package source remains protected through linked skin handoff");
	ok &= expect(read(path("part.mesh.json")) == modelBytes && read(path("default.skin")) == skinBytes &&
					 read(path("assets/default.skin")) == skinBytes,
				 "all authoring, native export, recovery and CLI operations retain original model and skin bytes");
	std::cout << checks << " assembly skin checks\n";
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	return ok ? 0 : 1;
}
