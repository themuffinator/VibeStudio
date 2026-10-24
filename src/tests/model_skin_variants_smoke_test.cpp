// Named skin variants (core/model_skin_variants.h): editing, the editable
// source schema, undo, the Quake III .skin and Doom 3 skin writers, and the
// model skin-variants and model skin-export commands.
#include "core/material_script.h"
#include "core/model_document.h"
#include "core/model_skin_bindings.h"
#include "core/model_skin_variants.h"
#include "tests/model_q3_animation_test_helpers.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
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
// Three triangle surfaces; body_1 and visor share a material, as a Doom 3
// model's surfaces often do.
ModelMesh model()
{
	auto mesh = q3Model();
	mesh.tags.clear();
	auto body = mesh.surfaces[0];
	body.name = "body_1";
	body.skinPaths = {"models/prop/body.tga"};
	auto head = body;
	head.name = "head";
	head.skinPaths = {"models/prop/head.tga", "models/prop/head_alt.tga"};
	auto visor = body;
	visor.name = "visor";
	mesh.surfaces = {body, head, visor};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
bool edit(const ModelMesh &mesh, QVector<ModelSkinVariant> *variants, ModelSkinVariantAction action, const QString &name,
		  QVector<ModelSkinVariantBinding> bindings = {}, const QString &newName = {})
{
	ModelSkinVariantEdit change;
	change.action = action;
	change.name = name;
	change.newName = newName;
	change.bindings = std::move(bindings);
	return editModelSkinVariants(mesh, *variants, change, variants);
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("skin-variants-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const QString &name) { return temporary.filePath(name); };
	bool ok = true;
	QString error;
	const auto mesh = model();

	// Editing: names, duplicates, surfaces and the working list.
	QVector<ModelSkinVariant> variants;
	ok &= expect(edit(mesh, &variants, ModelSkinVariantAction::Add, "red", {{"body_1", "models/prop/body_red.tga"}}) && variants.size() == 1 &&
					 variants[0].bindings.size() == 1,
				 "add a variant with its first binding");
	const auto before = variants;
	ok &= expect(!edit(mesh, &variants, ModelSkinVariantAction::Add, "RED") && variants == before, "names differ in more than case");
	for (const QString &name : {QStringLiteral("default"), QStringLiteral("Default"), QStringLiteral("1red"), QStringLiteral("red team"),
								QString(25, 'a'), QString()})
		ok &= expect(!edit(mesh, &variants, ModelSkinVariantAction::Add, name) && variants == before,
					 "invalid or reserved variant names are refused without changing the list");
	ok &= expect(!edit(mesh, &variants, ModelSkinVariantAction::Add, "blue", {{"arm", "models/prop/arm.tga"}}) && variants == before,
				 "a binding to a surface the model lacks is refused");
	ok &= expect(!edit(mesh, &variants, ModelSkinVariantAction::Add, "blue", {{"head", "../escape.tga"}}) && variants == before,
				 "an unsafe material path is refused");
	ok &= expect(!edit(mesh, &variants, ModelSkinVariantAction::Set, "green", {{"head", "models/prop/head.tga"}}) && variants == before,
				 "changing an unknown variant is refused");
	ok &= expect(edit(mesh, &variants, ModelSkinVariantAction::Set, "red", {{"head", "models/prop/head_red.tga"}, {"body_1", "models/prop/body_red2.tga"}}) &&
					 variants[0].bindings.size() == 2 && variants[0].bindings[0].material == "models/prop/body_red2.tga",
				 "set replaces and adds bindings in order");
	ok &= expect(edit(mesh, &variants, ModelSkinVariantAction::Set, "red", {{"head", QString()}}) && variants[0].bindings.size() == 1,
				 "an empty material returns the surface to its primary material");
	ok &= expect(edit(mesh, &variants, ModelSkinVariantAction::Add, "blue", {{"body_1", "models/prop/body_blue.tga"}, {"visor", "models/prop/body_blue.tga"}}) &&
					 edit(mesh, &variants, ModelSkinVariantAction::Rename, "blue", {}, "Blue") && variants[1].name == "Blue" &&
					 !edit(mesh, &variants, ModelSkinVariantAction::Rename, "Blue", {}, "red"),
				 "rename changes case but never onto another variant");
	auto many = variants;
	for (int i = int(many.size()); i < modelSkinVariantMaxCount; ++i)
		ok &= expect(edit(mesh, &many, ModelSkinVariantAction::Add, QStringLiteral("v%1").arg(i)), "fill the variant limit");
	ok &= expect(!edit(mesh, &many, ModelSkinVariantAction::Add, "extra") && many.size() == modelSkinVariantMaxCount, "variant count is bounded");
	ok &= expect(edit(mesh, &many, ModelSkinVariantAction::Remove, "v5") && many.size() == modelSkinVariantMaxCount - 1, "remove a variant");

	// The editable source keeps variants (schema 14); older sources load with none.
	auto owned = mesh;
	owned.skinVariants = variants;
	ok &= expect(validateModelSkinVariants(owned).isEmpty() && validateEditableModel(owned).isEmpty(), "edited variants validate");
	const auto json = editableModelJson(owned, &error);
	ModelMesh reloaded;
	ok &= expect(json.value("version").toInt() == 14 && json.value("skinVariants").toArray().size() == 2 &&
					 parseEditableModel(QJsonDocument(json).toJson(), &reloaded, &error) && reloaded.skinVariants == variants,
				 "variants round-trip through the editable source");
	ModelMesh legacy;
	const auto plain = editableModelJson(mesh, &error);
	ok &= expect(plain.value("version").toInt() < 13 && !plain.contains("skinVariants") &&
					 parseEditableModel(QJsonDocument(plain).toJson(), &legacy, &error) && legacy.skinVariants.isEmpty(),
				 "a source without variants keeps its older schema and loads with none");
	auto early = json;
	early.insert("version", 13);
	ok &= expect(!parseEditableModel(QJsonDocument(early).toJson(), &legacy, &error), "a version 13 source cannot carry variants");
	auto malformed = json;
	auto list = malformed.value("skinVariants").toArray();
	auto first = list[0].toObject();
	first.insert("name", "default");
	list[0] = first;
	malformed.insert("skinVariants", list);
	ok &= expect(!parseEditableModel(QJsonDocument(malformed).toJson(), &legacy, &error), "a reserved name in a source is refused");

	// Undo: adopting the list is one step.
	ModelDocument document;
	ok &= expect(document.setMesh(mesh, &error) && document.adopt(owned, document.selection(), document.revisionFingerprint(), &error) &&
					 document.mesh().skinVariants == variants && document.canUndo(),
				 "the document adopts the list as one undo step");
	ok &= expect(document.undo() && document.mesh().skinVariants.isEmpty() && document.redo() && document.mesh().skinVariants == variants,
				 "undo and redo restore the variant list");

	// Quake III .skin: every surface by its engine name, the variant over the primary materials.
	ModelMesh applied;
	QStringList inherited;
	QByteArray skin;
	ModelSkinBindingPlan plan;
	ok &= expect(applyModelSkinVariant(owned, variants[0], &applied, &error, &inherited) && inherited == QStringList({"head", "visor"}) &&
					 applied.surfaces[0].skinPaths == QStringList{"models/prop/body_red2.tga"} &&
					 applied.surfaces[1].skinPaths == mesh.surfaces[1].skinPaths,
				 "a variant replaces only the primary material of the surfaces it binds");
	ok &= expect(writeModelQ3Skin(applied, &skin, &error) && skin.startsWith("\"body\",\"models/prop/body_red2.tga\"\n") &&
					 planModelSkinBindings(applied, skin, &plan, &error) && plan.assignments.size() == 3,
				 "the .skin names engine surfaces and re-plans");
	for (const auto &assignment : plan.assignments)
		ok &= expect(assignment.material == applied.surfaces[assignment.surface].skinPaths.value(0), "the .skin re-plans to the variant materials");
	auto clash = mesh;
	clash.surfaces[2].name = "body_2";
	clash.surfaces[2].skinPaths = {"models/prop/other.tga"};
	ok &= expect(!writeModelQ3Skin(clash, &skin, &error) && error.contains("engine name"),
				 "surfaces sharing an engine name cannot get different materials");
	clash.surfaces[2].skinPaths = mesh.surfaces[0].skinPaths;
	ok &= expect(writeModelQ3Skin(clash, &skin, &error) && skin.count('\n') == 2, "surfaces sharing a name and material are written once");
	auto bare = mesh;
	bare.surfaces[1].skinPaths.clear();
	ok &= expect(!writeModelQ3Skin(bare, &skin, &error) && error.contains("head"), "a surface without a material is refused");

	// Doom 3 skins map materials, so a variant must not change a surface it keeps.
	QByteArray decl;
	ok &= expect(!writeModelDoom3Skin(owned, variants[0], "skins/prop_red", "models/prop.md5mesh", &decl, &error) && error.contains("visor"),
				 "a material shared with a kept surface is refused");
	ok &= expect(writeModelDoom3Skin(owned, variants[1], "skins/prop_blue", "models/prop.md5mesh", &decl, &error), "write a Doom 3 skin");
	QVector<ModelDoom3Skin> declarations;
	ok &= expect(parseModelDoom3Skins(decl, &declarations, &error) && declarations.size() == 1 && declarations[0].name == "skins/prop_blue" &&
					 declarations[0].models == QStringList{"models/prop.md5mesh"} && declarations[0].mappings.size() == 1 &&
					 declarations[0].mappings[0].from == "models/prop/body.tga" && declarations[0].mappings[0].to == "models/prop/body_blue.tga",
				 "the declaration maps the shared material once and names its model");
	const auto script = parseMaterialScript(QString::fromLatin1(decl), MaterialEngine::Doom3, "skins/prop.skin");
	bool clean = true;
	for (const auto &diagnostic : script.allDiagnostics())
		clean &= diagnostic.severity != MaterialDiagnosticSeverity::Error;
	ok &= expect(clean && script.materials.isEmpty(), "the material decl parser reads the skin declaration without errors");
	auto split = variants[1];
	split.bindings[1].material = "models/prop/visor_blue.tga";
	ok &= expect(!writeModelDoom3Skin(owned, split, "skins/prop_blue", {}, &decl, &error) && error.contains("body_1") && error.contains("visor"),
				 "two surfaces sharing a material cannot get different materials");
	ModelSkinVariant unchanged{"same", {{"head", "models/prop/head.tga"}}};
	ok &= expect(!writeModelDoom3Skin(owned, unchanged, "skins/prop_same", {}, &decl, &error), "a variant that changes nothing is refused");
	ok &= expect(!writeModelDoom3Skin(owned, variants[1], "skins/bad name", {}, &decl, &error), "declaration names stay bare tokens");
	ok &= expect(modelDoom3SkinDeclName("models/md5/imp.md5mesh", "dark-red") == "skins/models/md5/imp_dark_red",
				 "default declaration names follow the model path");
	ok &= expect(!parseModelDoom3Skins("skin a { \"x\" }", &declarations, &error) && !parseModelDoom3Skins("skin a { x y", &declarations, &error),
				 "malformed declarations are refused");
	// Doom 3 finds materials by canonical name (MakeNameCanonical): case and the
	// extension do not count, so differently spelled names are one material.
	auto spelled = mesh;
	spelled.surfaces[0].skinPaths = {"models/x/body"};
	spelled.surfaces[1].skinPaths = {"Models/X/Body.tga"};
	spelled.surfaces[2].skinPaths = {"models/x/visor.tga"};
	const ModelSkinVariant torso{"red", {{"body_1", "models/x/body_red"}}};
	ok &= expect(!writeModelDoom3Skin(spelled, torso, "skins/x_red", {}, &decl, &error) && error.contains("head"),
				 "a kept surface whose material differs only in case and extension would change too, so it is refused");
	const ModelSkinVariant both{"red", {{"body_1", "models/x/body_red"}, {"head", "Models/X/Body_Red.tga"}}};
	ok &= expect(writeModelDoom3Skin(spelled, both, "skins/x_red", {}, &decl, &error) && parseModelDoom3Skins(decl, &declarations, &error) &&
					 declarations.size() == 1 && declarations[0].mappings.size() == 1,
				 "one material under two spellings is mapped once");
	const ModelSkinVariant respelled{"same", {{"body_1", "MODELS/X/BODY.tga"}, {"head", "models/x/body"}}};
	ok &= expect(!writeModelDoom3Skin(spelled, respelled, "skins/x_same", {}, &decl, &error) && error.contains("changes no material"),
				 "a variant material that is the same Doom 3 material changes nothing");
	const ModelSkinVariant clashing{"red", {{"body_1", "models/x/body_red"}, {"head", "models/x/head_red"}}};
	ok &= expect(!writeModelDoom3Skin(spelled, clashing, "skins/x_red", {}, &decl, &error) && error.contains("both use"),
				 "two spellings of one material cannot get different variant materials");

	// Renaming a surface keeps the variant, which then refuses to apply until updated.
	auto renamed = owned;
	renamed.surfaces[0].name = "torso";
	ok &= expect(validateEditableModel(renamed).isEmpty() && modelSkinVariantMissingSurfaces(renamed, variants[0]) == QStringList{"body_1"} &&
					 !applyModelSkinVariant(renamed, variants[0], &applied, &error) && error.contains("body_1"),
				 "a stale binding is kept, reported and refused when applied");
	auto cleaned = renamed.skinVariants;
	ok &= expect(edit(renamed, &cleaned, ModelSkinVariantAction::Set, "red", {{"body_1", QString()}}) && cleaned[0].bindings.isEmpty(),
				 "returning a stale binding to the primary material removes it");
	auto embedded = owned;
	embedded.mdl.enabled = true;
	ok &= expect(!validateModelSkinVariants(embedded).isEmpty(), "embedded MDL skins cannot own variants");

	// The commands share the core service.
	const auto source = path("prop.mesh.json");
	ok &= expect(q3Write(source, QJsonDocument(editableModelJson(mesh)).toJson()), "write the editable fixture");
	const auto run = [&](const QStringList &arguments, int exitCode, QJsonObject *payload = nullptr) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  QStringList{"--cli", "model"} + arguments + QStringList{"--settings-file", path("settings.ini"), "--json"});
		if (!process.waitForStarted(5000) || !process.waitForFinished(30000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		if (payload)
			*payload = QJsonDocument::fromJson(bytes).object();
		if (process.exitCode() != exitCode)
			std::cerr << bytes.constData() << process.readAllStandardError().constData() << '\n';
		return process.exitStatus() == QProcess::NormalExit && process.exitCode() == exitCode;
	};
	QJsonObject payload;
	ok &= expect(run({"skin-variants", source}, 0, &payload) && payload.value("variants").toArray().isEmpty(), "CLI lists no variants");
	ok &= expect(run({"skin-variants", source, "--operation", "add", "--name", "blue", "--surface-material", "body_1=models/prop/body_blue.tga",
					  "--surface-material", "visor=models/prop/body_blue.tga", "--output", path("blue.mesh.json")},
					 0, &payload) &&
					 payload.value("written").toBool() && payload.value("variants").toArray().size() == 1,
				 "CLI adds a variant with repeated bindings");
	ModelDocument saved;
	ok &= expect(saved.load(path("blue.mesh.json"), &error) && saved.mesh().skinVariants.size() == 1 &&
					 saved.mesh().skinVariants[0].bindings.size() == 2,
				 "the written source holds the variant");
	ok &= expect(run({"skin-variants", path("blue.mesh.json"), "--operation", "set", "--name", "blue", "--surface-material", "head=models/prop/head_blue.tga",
					  "--output", path("blue.mesh.json")},
					 1),
				 "an existing output needs --overwrite");
	ok &= expect(run({"skin-variants", path("blue.mesh.json"), "--operation", "set", "--name", "blue", "--surface-material", "head=models/prop/head_blue.tga",
					  "--output", path("dry.mesh.json"), "--dry-run"},
					 0) &&
					 !QFileInfo::exists(path("dry.mesh.json")),
				 "a dry run writes nothing");
	ok &= expect(run({"skin-variants", source, "--operation", "add", "--name", "default", "--output", path("bad.mesh.json")}, 4) &&
					 !QFileInfo::exists(path("bad.mesh.json")),
				 "the CLI refuses a reserved name");
	ok &= expect(run({"skin-variants", source, "--operation", "add", "--name", "red"}, 2) &&
					 run({"skin-variants", source, "--operation", "set", "--name", "red", "--output", path("x.mesh.json")}, 2) &&
					 run({"skin-variants", source, "--surface-material", "head=x.tga"}, 2),
				 "the CLI refuses incomplete or irrelevant options");
	ok &= expect(run({"skin-export", path("blue.mesh.json"), "--variant", "blue", "--output", path("prop_blue.skin")}, 0, &payload) &&
					 payload.value("written").toBool() && payload.value("inheritedSurfaces").toArray().size() == 1,
				 "CLI writes a variant's .skin");
	ModelMesh blue;
	saved.load(path("blue.mesh.json"), &error);
	applyModelSkinVariant(saved.mesh(), saved.mesh().skinVariants[0], &blue, &error);
	QByteArray expected;
	ok &= expect(writeModelQ3Skin(blue, &expected, &error) && q3Read(path("prop_blue.skin")) == expected, "CLI and core write the same bytes");
	ok &= expect(run({"skin-export", path("blue.mesh.json"), "--variant", "blue", "--output", path("prop_blue.skin")}, 1) &&
					 run({"skin-export", path("blue.mesh.json"), "--variant", "blue", "--output", path("prop_blue.skin"), "--overwrite"}, 0),
				 "an existing skin needs --overwrite");
	ok &= expect(run({"skin-export", path("blue.mesh.json"), "--variant", "blue", "--format", "doom3", "--model-path", "models/prop.md5mesh",
					  "--output", path("prop_blue_d3.skin")},
					 0, &payload) &&
					 payload.value("declName").toString() == "skins/models/prop_blue" && payload.value("mappings").toArray().size() == 1 &&
					 parseModelDoom3Skins(q3Read(path("prop_blue_d3.skin")), &declarations, &error) && declarations.size() == 1,
				 "CLI writes a Doom 3 skin declaration");
	ok &= expect(run({"skin-export", path("blue.mesh.json"), "--format", "doom3", "--output", path("none.skin")}, 2) &&
					 run({"skin-export", path("blue.mesh.json"), "--variant", "green", "--output", path("none.skin")}, 4) &&
					 run({"skin-export", path("blue.mesh.json"), "--decl-name", "x", "--output", path("none.skin")}, 2) &&
					 !QFileInfo::exists(path("none.skin")),
				 "the CLI refuses a Doom 3 skin without a variant, unknown variants and stray options");
	ok &= expect(run({"skin-export", path("blue.mesh.json"), "--variant", "blue", "--output", path("dry.skin"), "--dry-run"}, 0) &&
					 !QFileInfo::exists(path("dry.skin")),
				 "a dry run checks the skin without writing it");
	// A native model: the primary materials of an arbitrary MD3.
	const auto md3 = exportEditableModel(mesh, "md3", 0, &error);
	ok &= expect(!md3.isEmpty() && q3Write(path("prop.md3"), md3) &&
					 run({"skin-export", path("prop.md3"), "--output", path("prop_default.skin")}, 0, &payload) &&
					 q3Read(path("prop_default.skin")).contains("\"head\",\"models/prop/head.tga\""),
				 "CLI writes the .skin of a native MD3's own materials");
	std::cout << checks << " skin variant checks\n";
	if (!ok)
		std::cerr << qPrintable(error) << '\n';
	return ok ? 0 : 1;
}
