#include "core/audio_export.h"
#include "core/package_draft.h"
#include "tests/model_player_bundle_test_helpers.h"
#include "tests/model_output_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <functional>
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
QByteArray hash(const QByteArray &bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
const ModelPlayerBundleFile *file(const ModelPlayerBundle &bundle, const QString &role)
{
	for (const auto &item : bundle.files)
		if (item.role == role)
			return &item;
	return nullptr;
}
double distance(ModelVec3 a, ModelVec3 b)
{
	return std::sqrt(std::pow(double(a.x) - b.x, 2) + std::pow(double(a.y) - b.y, 2) + std::pow(double(a.z) - b.z, 2));
}
class ChangingShaderReader final : public PackageArchiveReader
{
  public:
	mutable SkinReader storage;
	mutable int scriptReads = 0;
	PackageArchiveFormat format() const override
	{
		return storage.format();
	}
	QString sourcePath() const override
	{
		return storage.sourcePath();
	}
	bool isOpen() const override
	{
		return true;
	}
	QVector<PackageEntry> entries() const override
	{
		return storage.entries();
	}
	bool readEntryBytes(const QString &, QByteArray *, QString *, qint64) const override
	{
		return false;
	}
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)> &sink, QString *error,
					   const std::function<bool()> &cancel = {}) const override
	{
		if (storage.metadata[index].virtualPath.endsWith(".shader") && ++scriptReads == 2)
			storage.payloads[index].replace("textures/player/a.tga", "textures/player/c.tga");
		return storage.streamEntryAt(index, sink, error, cancel);
	}
};
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("player-bundle-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto path = [&](const QString &name) { return temporary.filePath(name); };
	PlayerBundleFixture fixture;
	QString error;
	bool ok = expect(fixture.create(temporary.path(), &error), "create original player, animated shader and icon fixtures");
	ModelPlayerBundle bundle;
	ok &= expect(
		prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, fixture.options, fixture.sourcePath, &bundle, &error),
		"prepare linked native player bundle");
	if (!ok)
	{
		std::cerr << qPrintable(error);
		return 1;
	}
	ok &= expect(bundle.files.size() == 12 && bundle.dependencies.canExport() && bundle.dependencies.resolvedPaths.size() == 3,
				 "complete generated files plus animated shader dependency closure");
	ok &= expect(!QJsonDocument(bundle.receipt).toJson().contains(temporary.path().toUtf8()) &&
					 bundle.receipt.value("parts").toArray().size() == 3,
				 "embedded content receipt excludes machine paths");
	ModelAssembly native = fixture.assembly;
	ModelAssemblyResolved nativeInputs = fixture.resolved;
	for (int i = 0; i < native.parts.size(); ++i)
	{
		auto &part = native.parts[i];
		const auto *content = file(bundle, part.id + "-model");
		const auto *skin = file(bundle, part.id + "-skin");
		ModelMesh decoded;
		ok &= expect(content && skin && importEditableModel(content->path, content->bytes, &decoded, &error),
					 "native model reopens without geometry loss");
		if (!content || !skin || decoded.frames.isEmpty())
			return 1;
		ok &= expect(decoded.frames.size() == fixture.resolved.inputs[i].mesh.frames.size() &&
						 decoded.tags.size() == fixture.resolved.inputs[i].mesh.tags.size(),
					 "all native poses and complete tag tracks retained");
		ModelSkinBindingPlan bindings;
		ok &= expect(planModelSkinBindings(decoded, skin->bytes, &bindings, &error) && bindings.assignments.size() == 1 &&
						 bindings.assignments[0].material == decoded.surfaces[0].skinPaths[0],
					 "generated skin names match normalized native surfaces");
		nativeInputs.inputs[i].mesh = decoded;
		part.translation = {};
		part.rotation = {};
		part.scale = 1;
		part.skin.reset();
	}
	nativeInputs.recipeSha256 = modelAssemblyFingerprint(native);
	for (double seconds : {0., .15, .43, 1.2, 2.7})
	{
		ModelAssemblyPose original, exported;
		bool match = sampleModelAssembly(fixture.assembly, fixture.resolved, seconds, &original, &error) &&
					 sampleModelAssembly(native, nativeInputs, seconds, &exported, &error);
		for (int s = 0; match && s < original.mesh.surfaces.size(); ++s)
		{
			const auto &before = original.mesh.surfaces[s].frames[0];
			const auto &after = exported.mesh.surfaces[s].frames[0];
			for (int v = 0; v < before.positions.size(); ++v)
				match &= distance(before.positions[v], after.positions[v]) < .12 && distance(before.normals[v], after.normals[v]) < .08;
		}
		ok &= expect(match, "distributed transforms and inherited scale preserve composed native animation");
	}
	ModelQ3AnimationConfig config;
	ok &= expect(parseModelQ3Animation(file(bundle, "animation")->bytes, &config, &error) &&
					 modelQ3AnimationJson(config) == modelQ3AnimationJson(fixture.assembly.q3Animation->config),
				 "native animation frame mapping and header round trip unchanged");
	const auto icon = decodeIdTechImage("icon.tga", file(bundle, "icon")->bytes, {});
	ok &= expect(icon.decoded && icon.image.size() == QSize(32, 32), "icon converted through texture service to native TGA");
	const auto written = writeModelPlayerBundle(bundle, path("player.pk3"), false, false);
	ok &= expect(written.succeeded() && written.outputCommitted && written.determinismVerified && written.entryCount == 12,
				 "atomic reproducibility-verified PK3 publication");
	if (!written.succeeded())
		std::cerr << qPrintable(packageWriteReportText(written));
	PackageArchive archive;
	ok &= expect(archive.load(path("player.pk3"), &error), "published player opens in shared package reader");
	for (const auto &item : bundle.files)
	{
		QByteArray bytes;
		ok &= expect(archive.readEntryBytes(item.path, &bytes, &error) && hash(bytes) == item.sha256,
					 "every packaged file matches reviewed content");
	}
	const auto second = writeModelPlayerBundle(bundle, path("second.pk3"), false, false);
	ok &= expect(second.succeeded() && second.sha256 == written.sha256 && q3Read(path("second.pk3")) == q3Read(path("player.pk3")),
				 "repeated publication is byte identical");
	const auto dry = writeModelPlayerBundle(bundle, path("dry.pk3"), false, true);
	ok &= expect(dry.succeeded() && !dry.outputCommitted && !QFileInfo::exists(path("dry.pk3")) && dry.sha256 == written.sha256,
				 "dry run measures exact archive without output");
	ok &= expect(!writeModelPlayerBundle(bundle, path("player.pk3"), false, false).succeeded(),
				 "existing output requires explicit overwrite");
	const auto before = q3Read(path("player.pk3"));
	const auto replacement = writeModelPlayerBundle(bundle, path("player.pk3"), true, false);
	ok &= expect(replacement.succeeded() && q3Read(replacement.backupPath) == before, "overwrite retains verified original backup");
	const auto receipt = QJsonDocument(modelPlayerBundleJson(bundle)).toJson();
	ModelPlayerBundle unchanged = bundle;
	ok &= expect(!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, fixture.options, fixture.sourcePath,
										   &unchanged, &error, {[] { return true; }, {}}) &&
					 QJsonDocument(modelPlayerBundleJson(unchanged)).toJson() == receipt,
				 "cancelled preparation preserves previous review");
	bool cancel = false;
	ModelWorkControl cancellation{[&] { return cancel; },
								  [&](ModelWorkPhase phase, qint64, qint64) {
									  if (phase == ModelWorkPhase::Writing)
										  cancel = true;
								  }};
	const auto cancelled = writeModelPlayerBundle(bundle, path("player.pk3"), true, false, cancellation);
	ok &= expect(!cancelled.outputCommitted && !cancelled.succeeded() && q3Read(path("player.pk3")) == before,
				 "cancellation before commit preserves existing output");
	auto corrupt = bundle;
	corrupt.files[0].bytes[0] ^= 1;
	ok &= expect(!writeModelPlayerBundle(corrupt, path("player.pk3"), true, false).succeeded() && q3Read(path("player.pk3")) == before,
				 "reviewed-content mutation cannot publish");
	auto guarded = bundle;
	guarded.protectedPaths << path("protected.pk3.bak");
	ok &= expect(!writeModelPlayerBundle(guarded, path("protected.pk3"), true, false).succeeded(),
				 "backup paths cannot overwrite protected source");
	ok &= expect(!writeModelPlayerBundle(bundle, path("assets/forbidden.pk3"), false, true).succeeded(),
				 "source asset directory protected in dry run too");
	const auto protectedTexture = path("assets/textures/player/a.tga");
	const auto textureBytes = q3Read(protectedTexture);
	for (const QString &suffix : {QString(), QStringLiteral(".bak"), QStringLiteral(".vibestudio-save.lock")})
		for (bool dryRun : {true, false})
		{
			const auto output = path(QStringLiteral("alias-%1-%2.pk3").arg(suffix.size()).arg(dryRun));
			const auto alias = output + suffix;
			ok &= expect(!textureBytes.isEmpty() && modelTestHardLink(protectedTexture, alias),
						 "prepare player output/backup/lock hard-link fixtures");
			const auto refused = writeModelPlayerBundle(bundle, output, true, dryRun);
			ok &= expect(!refused.succeeded() && !refused.outputCommitted && q3Read(protectedTexture) == textureBytes &&
						 q3Read(alias) == textureBytes && (suffix.isEmpty() || !QFileInfo::exists(output)),
						 "player package dry and real writes protect folder input aliases at every publication path");
		}
	for (const QString &name : {QString(), QStringLiteral("../bad"), QStringLiteral("con"), QString(25, 'a')})
	{
		auto options = fixture.options;
		options.modelName = name;
		ok &= expect(
			!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, options, fixture.sourcePath, &unchanged, &error),
			"unsafe or oversized player ID rejected");
	}
	auto options = fixture.options;
	options.headPart = "lower";
	ok &= expect(
		!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, options, fixture.sourcePath, &unchanged, &error),
		"duplicate native roles rejected");
	auto stale = fixture.resolved;
	stale.recipeSha256 = {};
	ok &=
		expect(!prepareModelPlayerBundle(fixture.assembly, stale, fixture.context, fixture.options, fixture.sourcePath, &unchanged, &error),
			   "stale model resolution rejected");
	auto wrong = fixture.assembly;
	wrong.parts[2].tag = "tag_torso";
	stale = fixture.resolved;
	stale.recipeSha256 = modelAssemblyFingerprint(wrong);
	ok &= expect(!prepareModelPlayerBundle(wrong, stale, fixture.context, fixture.options, fixture.sourcePath, &unchanged, &error),
				 "incorrect native head attachment rejected");
	stale = fixture.resolved;
	stale.inputs[2].mesh = stale.inputs[0].mesh;
	ok &=
		expect(!prepareModelPlayerBundle(fixture.assembly, stale, fixture.context, fixture.options, fixture.sourcePath, &unchanged, &error),
			   "animated head cannot be silently discarded");
	options = fixture.options;
	options.iconSource = "missing.png";
	ok &= expect(
		!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, options, fixture.sourcePath, &unchanged, &error),
		"missing icon blocks complete player publication");
	options = fixture.options;
	options.iconKind = ModelAssemblySource::Package;
	options.iconSource = "textures/player/a.tga";
	ok &= expect(
		prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, options, fixture.sourcePath, &unchanged, &error),
		"package icon uses shared immutable entry reader");
	options.iconEntryIndex = 999;
	ok &= expect(
		!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, options, fixture.sourcePath, &unchanged, &error),
		"stale exact icon occurrence rejected");
	ModelMesh material;
	material.surfaces.append(ModelSurface{});
	for (const QString &token : {QStringLiteral("textures/player/a.png"), QStringLiteral("textures/player/a.jpg")})
	{
		material.surfaces[0].skinPaths = {token};
		ok &= expect(!inspectModelMaterialDependencies(material, *fixture.context.archive).canExport(),
					 "native material lookup cannot replace missing PNG or JPG with existing TGA");
	}
	material.surfaces[0].skinPaths = {"models/player/body.tga"};
	ok &= expect(inspectModelMaterialDependencies(material, *fixture.context.archive).canExport(),
				 "native shader lookup strips the material extension before finding its declaration");
	auto changing = std::make_shared<ChangingShaderReader>();
	for (const auto &entry : fixture.context.archive->entries())
	{
		QByteArray bytes;
		if (entry.kind == PackageEntryKind::File && fixture.context.archive->readEntryBytes(entry.virtualPath, &bytes, &error))
			changing->storage.add(entry.virtualPath, bytes);
	}
	auto changingContext = fixture.context;
	changingContext.archive = changing;
	ok &= expect(!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, changingContext, fixture.options, fixture.sourcePath,
										   &unchanged, &error) &&
					 error.contains("captured shader dependencies changed"),
				 "same-size shader mutation cannot introduce an uncaptured material after dependency review");
	changing->storage.lateFailure = true;
	ok &= expect(!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, changingContext, fixture.options, fixture.sourcePath,
										   &unchanged, &error),
				 "reader failure after emitting dependency bytes cannot publish partial content");
	const auto process = [&](QStringList args, int exitCode, QJsonObject *payload = nullptr) {
		QProcess p;
		p.setWorkingDirectory(temporary.path());
		p.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "model", "assembly", fixture.sourcePath} + args +
													 QStringList{"--settings-file", path("settings.ini"), "--json"});
		if (!p.waitForStarted(5000) || !p.waitForFinished(30000))
		{
			p.kill();
			p.waitForFinished();
			return false;
		}
		const auto bytes = p.readAllStandardOutput();
		if (payload)
			*payload = QJsonDocument::fromJson(bytes).object();
		if (p.exitCode() != exitCode)
			std::cerr << bytes.constData() << p.readAllStandardError().constData();
		return p.exitStatus() == QProcess::NormalExit && p.exitCode() == exitCode;
	};
	const QStringList cli{"--player-name", "synthetic", "--head-part", "head", "--icon", fixture.iconPath, "--package", path("assets")};
	QJsonObject payload;
	ok &= expect(process(cli + QStringList{"--operation", "player-review"}, 0, &payload) && payload.value("files").toArray().size() == 12 &&
					 !payload.value("written").toBool(),
				 "actual CLI reviews complete bundle");
	ok &= expect(process(cli + QStringList{"--operation", "player-export", "--output", path("cli.pk3")}, 0, &payload) &&
					 payload.value("write").toObject().value("determinismVerified").toBool() && q3Read(path("cli.pk3")) == before,
				 "actual CLI shares deterministic publication service");
	ok &= expect(process(cli + QStringList{"--operation", "player-export", "--output", path("cli-dry.pk3"), "--dry-run"}, 0) &&
					 !QFileInfo::exists(path("cli-dry.pk3")),
				 "CLI dry run writes nothing");
	ok &= expect(process(cli + QStringList{"--operation", "player-export", "--output", path("cli.pk3")}, 4, &payload) &&
					 payload.contains("write") && !payload.value("write").toObject().value("outputCommitted").toBool() &&
					 q3Read(path("cli.pk3")) == before,
				 "failed CLI publication retains structured write evidence and existing output");
	ok &= expect(process(cli + QStringList{"--operation", "player-review", "--output", path("unused.pk3")}, 2),
				 "irrelevant CLI write flag rejected");
	ok &= expect(process(cli + QStringList{"--operation", "player-review", "--icon-entry-index", "0"}, 2),
				 "CLI file icon rejects package index");
	// A portable draft can stage material changes without rewriting source files.
	PackageStagingModel staged;
	ok &= expect(staged.loadBaseArchive(*fixture.context.archive, &error) &&
					 staged.addBytes(fixture.texture, "textures/player/a.tga", &error, PackageStageConflictResolution::ReplaceExisting) &&
					 PackageDraft::save(path("assets.vibepackage"), &staged, {}, &error),
				 "retain staged project material draft");
	auto draftCli = cli;
	draftCli[draftCli.indexOf("--package") + 1] = path("assets.vibepackage");
	ok &= expect(process(draftCli + QStringList{"--operation", "player-review"}, 0), "CLI reviews staged portable package assets");
	// The studio accepts more image variants than the original game. Refuse
	// altered/unsupported material bytes without modifying the reviewed result.
	for (int origin : {0x10, 0x20})
	{
		auto texture = fixture.texture;
		texture[17] = char(texture[17] | origin);
		q3Write(path("assets/textures/player/a.tga"), texture);
		ok &= expect(!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, fixture.options, fixture.sourcePath,
											   &unchanged, &error),
					 "TGA origin flag ignored by original game cannot silently invert player texture");
	}
	q3Write(path("assets/textures/player/a.tga"), "broken-image");
	ok &= expect(!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, fixture.options, fixture.sourcePath,
										   &unchanged, &error),
				 "corrupt dependency cannot publish");
	q3Write(path("assets/textures/player/a.tga"), fixture.texture);
	const auto originalSkin = q3Read(path("lower.skin"));
	q3Write(path("lower.skin"), "body,models/missing\n");
	ok &= expect(writeModelPlayerBundle(bundle, path("snapshot.pk3"), false, false).succeeded() && q3Read(path("snapshot.pk3")) == before &&
					 q3Read(path("lower.skin")) != originalSkin,
				 "reviewed snapshot remains stable and never rewrites a changed external skin");
	q3Write(path("lower.skin"), originalSkin);
	// The earlier checks rewrote package files, so read the folder again.
	auto current = fixture.context;
	auto reloaded = std::make_shared<PackageArchive>();
	ok &= expect(reloaded->load(path("assets"), &error), "reload the asset folder");
	current.archive = reloaded;
	// Completeness: cgame holds weapons on upper's tag_weapon.
	auto weaponless = fixture.resolved;
	for (auto &input : weaponless.inputs)
		if (input.part == "upper")
			input.mesh.tags.erase(std::remove_if(input.mesh.tags.begin(), input.mesh.tags.end(), [](const auto &tag) { return tag.name == "tag_weapon"; }),
								  input.mesh.tags.end());
	ok &= expect(!prepareModelPlayerBundle(fixture.assembly, weaponless, current, fixture.options, fixture.sourcePath, &unchanged, &error) &&
					 error.contains("tag_weapon"),
				 "upper without tag_weapon is refused");
	ok &= expect(bundle.notes.join(' ').contains("tag_flag") && bundle.notes.join(' ').contains("non-team") &&
					 bundle.notes.join(' ').contains("default model's sounds") && bundle.skins == QStringList{"default"},
				 "the review notes the missing tag_flag, the missing team skins and the default sounds");
	const auto entry = [](const ModelPlayerBundle &item, const QString &path) -> const ModelPlayerBundleFile * {
		for (const auto &candidate : item.files)
			if (candidate.path == path)
				return &candidate;
		return nullptr;
	};
	const QString players = "models/players/synthetic/";

	// Team skins: red from upper's stored skin variant, blue from a head .skin
	// file and a package icon. Parts without either keep their own materials.
	auto teamOptions = fixture.options;
	teamOptions.teamVariants = true;
	ok &= expect(!prepareModelPlayerBundle(fixture.assembly, fixture.resolved, current, teamOptions, fixture.sourcePath, &unchanged, &error) &&
					 error.contains("red"),
				 "team skins identical to the model's own materials are refused");
	ModelMesh upper;
	ok &= expect(parseEditableModel(q3Read(path("upper.mesh.json")), &upper, &error), "read the upper source");
	upper.skinVariants = {ModelSkinVariant{"red", {{"body_1", "textures/player/b.tga"}}}};
	ok &= expect(q3Write(path("upper.mesh.json"), QJsonDocument(editableModelJson(upper)).toJson()) &&
					 q3Write(path("blue_head.skin"), "body,textures/player/b.tga\n") && q3Write(path("wrong.skin"), "arm,textures/player/b.tga\n"),
				 "store a red variant on upper and write a blue head skin");
	ModelAssemblyResolved teamInputs;
	ok &= expect(resolveModelAssembly(fixture.assembly, current, &teamInputs, &error), "resolve the parts again");
	teamOptions.variants = {ModelPlayerSkinVariant{"blue", {std::nullopt, std::nullopt, ModelPlayerReference{"blue_head.skin"}},
												   ModelPlayerReference{"textures/player/a.tga", ModelAssemblySource::Package, -1}}};
	ModelPlayerBundle team, again;
	ok &= expect(prepareModelPlayerBundle(fixture.assembly, teamInputs, current, teamOptions, fixture.sourcePath, &team, &error) &&
					 prepareModelPlayerBundle(fixture.assembly, teamInputs, current, teamOptions, fixture.sourcePath, &again, &error),
				 "prepare the team package twice");
	if (!ok)
		std::cerr << qPrintable(error) << '\n';
	ok &= expect(team.files.size() == 20 && team.skins == QStringList({"default", "blue", "red"}), "three skin sets and three icons are published");
	bool deterministic = team.files.size() == again.files.size();
	for (int i = 0; deterministic && i < team.files.size(); ++i)
		deterministic = team.files[i].path == again.files[i].path && team.files[i].sha256 == again.files[i].sha256;
	ok &= expect(deterministic, "the team package is deterministic");
	for (const QString &role : {QStringLiteral("lower"), QStringLiteral("upper"), QStringLiteral("head")})
		for (const QString &skin : {QStringLiteral("default"), QStringLiteral("red"), QStringLiteral("blue")})
			ok &= expect(entry(team, players + role + '_' + skin + ".skin") && entry(team, players + "icon_" + skin + ".tga"),
						 "every role has every skin and every skin its icon");
	const auto skinMaterial = [&](const QString &role, const QString &skin) {
		const auto *model = entry(team, players + role + ".md3");
		const auto *file = entry(team, players + role + '_' + skin + ".skin");
		ModelMesh decoded;
		ModelSkinBindingPlan plan;
		return model && file && importEditableModel(model->path, model->bytes, &decoded, &error) &&
					   planModelSkinBindings(decoded, file->bytes, &plan, &error) && plan.assignments.size() == 1
				   ? plan.assignments[0].material
				   : QString();
	};
	ok &= expect(skinMaterial("upper", "red") == "textures/player/b.tga" && skinMaterial("upper", "default") == "textures/player/a.tga" &&
					 skinMaterial("head", "blue") == "textures/player/b.tga" && skinMaterial("lower", "red") == "models/player/body" &&
					 skinMaterial("head", "red") == "textures/player/a.tga",
				 "each skin covers every surface with its variant, file, linked or own material");
	const auto *redIcon = entry(team, players + "icon_red.tga"), *defaultIcon = entry(team, players + "icon_default.tga"),
			   *blueIcon = entry(team, players + "icon_blue.tga");
	ok &= expect(redIcon && defaultIcon && blueIcon && redIcon->bytes == defaultIcon->bytes &&
					 decodeIdTechImage("icon.tga", blueIcon->bytes, {}).image.size() == QSize(32, 32),
				 "the red icon reuses the package icon and the blue icon converts its own image");
	ok &= expect(team.receipt.value("schemaVersion").toInt() == 2 && team.receipt.value("skins").toArray().size() == 3 &&
					 team.receipt.value("teamSkins").toBool() && bundle.receipt.value("schemaVersion").toInt() == 1 && !bundle.receipt.contains("skins"),
				 "schema 2 receipts record skins while a single-skin package keeps schema 1");
	ok &= expect(team.notes.join(' ').contains("own materials of lower, head") && team.notes.join(' ').contains("Team games use the red and blue skins"),
				 "the review says which parts keep their own materials in a team skin");
	const auto teamWritten = writeModelPlayerBundle(team, path("team.pk3"), false, false);
	ok &= expect(teamWritten.succeeded() && teamWritten.entryCount == 20, "the team package publishes");
	for (const auto &[name, change] : QVector<QPair<QString, std::function<void(ModelPlayerBundleOptions &)>>>{
			 {"duplicate", [](auto &o) { o.variants[0].name = "Default"; }},
			 {"invalid", [](auto &o) { o.variants[0].name = "blue team"; }},
			 {"long", [](auto &o) { o.variants[0].name = QString(25, 'b'); }},
			 {"uncovered", [&](auto &o) { o.variants[0].skins[2] = ModelPlayerReference{"wrong.skin"}; }},
			 {"missing", [](auto &o) { o.variants[0].skins[1] = ModelPlayerReference{"missing.skin"}; }}})
	{
		auto options = teamOptions;
		change(options);
		ok &= expect(!prepareModelPlayerBundle(fixture.assembly, teamInputs, current, options, fixture.sourcePath, &unchanged, &error),
					 qPrintable(QStringLiteral("refuse a %1 skin").arg(name)));
	}

	// Detail levels: made in each part's own space, then placed like the part.
	auto detailOptions = fixture.options;
	detailOptions.detailLevels = ModelPlayerDetailLevels::Generated;
	ModelPlayerBundle detailed;
	ok &= expect(prepareModelPlayerBundle(fixture.assembly, teamInputs, current, detailOptions, fixture.sourcePath, &detailed, &error) &&
					 detailed.files.size() == 18 && detailed.receipt.value("detailLevels").toArray().size() == 6,
				 "two generated levels per part are published");
	for (const QString &role : {QStringLiteral("lower"), QStringLiteral("upper"), QStringLiteral("head")})
	{
		ModelMesh base;
		const auto *baseFile = entry(detailed, players + role + ".md3");
		ok &= expect(baseFile && importEditableModel(baseFile->path, baseFile->bytes, &base, &error), "decode the base part");
		for (int level = 1; level <= 2; ++level)
		{
			const auto *file = entry(detailed, players + role + QStringLiteral("_%1.md3").arg(level));
			ModelMesh decoded;
			bool same = file && importEditableModel(file->path, file->bytes, &decoded, &error) && decoded.frames.size() == base.frames.size() &&
						decoded.surfaces.size() == base.surfaces.size() && decoded.surfaces[0].name == base.surfaces[0].name;
			for (int frame = 0; same && frame < base.frames.size(); ++frame)
				for (int v = 0; same && v < base.surfaces[0].frames[frame].positions.size(); ++v)
					same = distance(base.surfaces[0].frames[frame].positions[v], decoded.surfaces[0].frames[frame].positions[v]) < .05;
			ok &= expect(same, "each level keeps the base's frames, surface names and baked placement");
		}
	}
	detailOptions.lodLevels = 3;
	ok &= expect(!prepareModelPlayerBundle(fixture.assembly, teamInputs, current, detailOptions, fixture.sourcePath, &unchanged, &error),
				 "Quake III loads two detail levels, so a third is refused");
	ModelMesh lower;
	ok &= expect(parseEditableModel(q3Read(path("lower.mesh.json")), &lower, &error) &&
					 q3Write(path("lower_detail.mesh.json"), QJsonDocument(editableModelJson(lower)).toJson()),
				 "write a hand-made lower level");
	ModelLodRecipeLevel handMade;
	handMade.generated = false;
	handMade.source = "lower_detail.mesh.json";
	lower.detailLevels = {handMade};
	ok &= expect(q3Write(path("lower.mesh.json"), QJsonDocument(editableModelJson(lower)).toJson()), "store the recipe on lower");
	ModelAssemblyResolved recipeInputs;
	detailOptions.detailLevels = ModelPlayerDetailLevels::Stored;
	ModelPlayerBundle stored;
	ok &= expect(resolveModelAssembly(fixture.assembly, current, &recipeInputs, &error) &&
					 prepareModelPlayerBundle(fixture.assembly, recipeInputs, current, detailOptions, fixture.sourcePath, &stored, &error) &&
					 stored.files.size() == 13 && entry(stored, players + "lower_1.md3") && !entry(stored, players + "upper_1.md3") &&
					 stored.notes.join(' ').contains("No detail levels are stored for upper"),
				 "stored recipes publish their levels and parts without one are noted");
	ok &= expect(std::any_of(stored.protectedPaths.cbegin(), stored.protectedPaths.cend(),
							 [&](const auto &item) { return modelPathsReferToSameFile(item, path("lower_detail.mesh.json")); }),
				 "hand-made levels are protected inputs");
	handMade.source = "head.mesh.json";
	lower.detailLevels = {handMade};
	ok &= expect(q3Write(path("lower.mesh.json"), QJsonDocument(editableModelJson(lower)).toJson()) &&
					 resolveModelAssembly(fixture.assembly, current, &recipeInputs, &error) &&
					 !prepareModelPlayerBundle(fixture.assembly, recipeInputs, current, detailOptions, fixture.sourcePath, &unchanged, &error) &&
					 error.contains("would not load correctly"),
				 "a level without the base's frames is refused");
	lower.detailLevels.clear();
	ok &= expect(q3Write(path("lower.mesh.json"), QJsonDocument(editableModelJson(lower)).toJson()) &&
					 resolveModelAssembly(fixture.assembly, current, &recipeInputs, &error),
				 "restore lower");

	// Custom sounds: mono PCM WAV is kept, other audio converted for Quake III.
	AudioClip stereo;
	stereo.channels = 2;
	stereo.sampleRate = 44100;
	for (int i = 0; i < 4410; ++i)
		stereo.samples << float(std::sin(i * .05) * .5) << float(std::sin(i * .07) * .25);
	AudioClip mono = stereo;
	mono.channels = 1;
	mono.sampleRate = 11025;
	mono.samples.resize(2205);
	AudioWavOptions eightBit;
	eightBit.format = AudioWavFormat::Pcm8;
	const auto monoBytes = encodeAudioWav(mono, eightBit, &error);
	ok &= expect(q3Write(path("death.wav"), encodeAudioWav(stereo, &error)) && q3Write(path("jump.wav"), monoBytes) &&
					 q3Write(path("noise.wav"), "not a sound") && q3Write(path("big.wav"), QByteArray(17 * 1024 * 1024, '\0')),
				 "write sound fixtures");
	auto soundOptions = fixture.options;
	soundOptions.sounds = {{"death1", ModelPlayerReference{"death.wav"}}, {"jump1", ModelPlayerReference{"jump.wav"}}};
	ModelPlayerBundle voiced, voicedAgain;
	ok &= expect(prepareModelPlayerBundle(fixture.assembly, recipeInputs, current, soundOptions, fixture.sourcePath, &voiced, &error) &&
					 prepareModelPlayerBundle(fixture.assembly, recipeInputs, current, soundOptions, fixture.sourcePath, &voicedAgain, &error) &&
					 voiced.files.size() == 14,
				 "custom sounds are published beside the model");
	const auto *death = entry(voiced, "sound/player/synthetic/death1.wav");
	const auto *jump = entry(voiced, "sound/player/synthetic/jump1.wav");
	const auto converted = death ? decodeAudioClip("death1.wav", death->bytes) : AudioClipResult{};
	ok &= expect(death && converted.succeeded() && converted.clip.channels == 1 && converted.clip.sampleRate == 22050 && death->bytes.size() > 36 &&
					 qFromLittleEndian<quint16>(death->bytes.constData() + 20) == 1 && qFromLittleEndian<quint16>(death->bytes.constData() + 34) == 16,
				 "stereo audio is converted to 22,050 Hz mono 16-bit PCM");
	ok &= expect(jump && jump->bytes == monoBytes, "a mono PCM WAV is kept as it is");
	const auto *deathAgain = entry(voicedAgain, "sound/player/synthetic/death1.wav");
	ok &= expect(death && deathAgain && deathAgain->sha256 == death->sha256, "sound conversion is deterministic");
	ok &= expect(voiced.notes.join(' ').contains("converted") && voiced.notes.join(' ').contains("Custom sounds packaged: 2 of 13") &&
					 voiced.receipt.value("sounds").toArray().size() == 2,
				 "the review and receipt record each sound");
	for (const auto &sound : QVector<ModelPlayerSound>{{"scream", ModelPlayerReference{"death.wav"}},
													   {"death1", ModelPlayerReference{"noise.wav"}},
													   {"death1", ModelPlayerReference{"big.wav"}},
													   {"death1", ModelPlayerReference{"missing.wav"}}})
	{
		auto options = fixture.options;
		options.sounds = {sound};
		ok &= expect(!prepareModelPlayerBundle(fixture.assembly, recipeInputs, current, options, fixture.sourcePath, &unchanged, &error),
					 "unknown, unreadable, oversized and missing sounds are refused");
	}
	auto doubled = soundOptions;
	doubled.sounds[1].name = "death1";
	ok &= expect(!prepareModelPlayerBundle(fixture.assembly, recipeInputs, current, doubled, fixture.sourcePath, &unchanged, &error),
				 "a sound listed twice is refused");

	// The CLI shares the options.
	ok &= expect(process(cli + QStringList{"--operation", "player-review", "--team-variants", "--variant-skin", "blue:head=" + path("blue_head.skin"),
										  "--variant-icon", "blue=package:textures/player/a.tga", "--sound", "death1=" + path("death.wav"), "--sound",
										  "jump1=" + path("jump.wav"), "--lod-levels", "1"},
						 0, &payload) &&
					 payload.value("files").toArray().size() == 25 && payload.value("skins").toArray().size() == 3 &&
					 payload.value("teamSkins").toBool() && payload.value("sounds").toArray().size() == 2,
				 "CLI reviews team skins, a level per part and custom sounds");
	ok &= expect(process(cli + QStringList{"--operation", "player-review", "--variant-skin", "blue:arm=" + path("blue_head.skin")}, 2) &&
					 process(cli + QStringList{"--operation", "player-review", "--variant-icon", "blue=a.png", "--variant-icon", "blue=b.png"}, 2) &&
					 process(cli + QStringList{"--operation", "player-review", "--lod-ratio", "0.5"}, 2) &&
					 process(cli + QStringList{"--operation", "player-review", "--lod-levels", "1", "--lod-recipes"}, 2) &&
					 process(cli + QStringList{"--operation", "player-review", "--sound", "scream=" + path("death.wav")}, 4),
				 "CLI refuses malformed, repeated, conflicting and unknown player options");
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (!evidence.isEmpty())
	{
		PlayerBundleFixture retained;
		ok &= expect(retained.create(QDir(evidence).filePath("native-fixture"), &error),
					 "retain original fixture for independent native loader oracle");
	}
	std::cout << checks << " player bundle core/CLI checks\n";
	return ok ? 0 : 1;
}
