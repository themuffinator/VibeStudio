#include "core/package_draft.h"
#include "tests/model_player_bundle_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
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
		std::cerr << error.toStdString();
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
		std::cerr << packageWriteReportText(written).toStdString();
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
			std::cerr << bytes.toStdString() << p.readAllStandardError().toStdString();
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
