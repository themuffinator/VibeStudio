#include "core/level_dependencies.h"
#include "core/level_materials.h"
#include "core/model_fingerprint.h"
#include "core/package_draft.h"
#include "tests/level_model_appearance_test_helpers.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace {
int checks = 0;
bool expect(bool pass, const char* text, const QString& detail = {}) {
	++checks; if (!pass) { std::cerr << "FAIL: " << text << ": " << detail.toStdString() << '\n'; } return pass;
}
class AlteredReader final : public PackageArchiveReader {
public:
	const PackageArchiveReader& source;
	QString path;
	bool duplicate = false, truncate = false;
	explicit AlteredReader(const PackageArchiveReader& reader) : source(reader) {}
	PackageArchiveFormat format() const override { return source.format(); }
	QString sourcePath() const override { return source.sourcePath(); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override {
		auto rows = source.entries(); if (duplicate) { for (const auto& row : source.entries()) { if (row.virtualPath == path) { rows << row; break; } } } return rows;
	}
	bool readEntryBytes(const QString& name, QByteArray* out, QString* error, qint64 cap) const override { return source.readEntryBytes(name, out, error, cap); }
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 cap) const override {
		const auto ok = source.readEntryAt(index, out, error, cap);
		if (ok && truncate && source.entries()[index].virtualPath == path) { out->chop(1); }
		return ok;
	}
};
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || argc != 2 || !QDir().mkpath(root)) { return 1; }
	QTemporaryDir temporary(QDir(root).filePath("level-model-appearance-XXXXXX"));
	LevelModelAppearanceFixture fixture; QString error;
	bool ok = expect(temporary.isValid() && fixture.create(temporary.path(), &error), "create original two-surface animated fixture", error);
	if (!ok) { return 1; }
	PackageArchive archive;
	ok &= expect(archive.load(fixture.path("assets"), &error), "load package folder", error);
	auto map = fixture.map(&error);
	const auto source = decodeModelMesh("models/test.md3", fixture.files.value("assets/models/test.md3"));
	const auto fingerprint = modelStateFingerprint(source);
	auto invalidRequest = levelModelAppearance(map, map.entities[1]); invalidRequest.frame = -1;
	ok &= expect(!prepareLevelModelAppearance(source, invalidRequest, archive).succeeded(), "direct service caller cannot index a negative frame");
	const auto prepare = [&](const LevelMapDocument& candidate, int entity = 1) { return prepareLevelModelAppearance(source, levelModelAppearance(candidate, candidate.entities[entity]), archive); };
	auto result = prepare(map);
	ok &= expect(result.succeeded() && result.mesh.surfaces.size() == 2 && result.mesh.frameCount == 1 && result.mesh.surfaces[0].frames.size() == 1
		&& result.mesh.skinPaths == QStringList{"models/old_body", "models/old_head"}, "default compiler uses slot zero on every surface", result.error);
	ok &= expect(setLevelMapEntityProperty(&map, 1, "_skin", "blue", &error) && setLevelMapEntityProperty(&map, 1, "_frame", "0", &error), "appearance uses ordinary map authoring history", error);
	result = prepare(map);
	ok &= expect(result.succeeded() && result.mesh.skinPaths == QStringList{"models/new_body", "models/new_head"}, "compiler skin maps source materials", result.error);
	ok &= expect(result.mesh.surfaces[0].frames[0].positions[0].z == source.surfaces[0].frames[0].positions[0].z, "compiler frame zero geometry preserved");
	const auto input = result.receipt.value("inputs").toArray().first().toObject();
	ok &= expect(input.value("path") == "models/test_blue.skin" && input.value("entryIndex").toInt(-1) >= 0 && input.value("sha256").toString().size() == 64, "receipt records exact skin identity and hash");
	ok &= expect(setLevelMapEntityProperty(&map, 2, "skin", "body", &error) && setLevelMapEntityProperty(&map, 2, "frame", "0", &error), "legacy aliases authored", error);
	result = prepare(map, 2);
	ok &= expect(result.succeeded() && result.mesh.surfaces.size() == 1 && result.mesh.skinPaths == QStringList{"models/alternate"}
		&& result.receipt.value("omittedSurfaces").toInt() == 1, "nonempty compiler mappings omit unmapped surfaces", result.error);
	auto alias = map;
	ok &= expect(setLevelMapEntityProperty(&alias, 2, "_skin", "14", &error) && setLevelMapEntityProperty(&alias, 2, "_frame", "0", &error)
		&& setLevelMapEntityProperty(&alias, 2, "frame", "1", &error), "underscored parameters authored");
	result = prepare(alias, 2);
	ok &= expect(result.succeeded() && result.receipt.value("skinPath") == "models/test.md3_14.skin" && result.mesh.skinPaths == QStringList{"models/new_head"}
		&& result.receipt.value("frame").toInt() == 0, "underscored keys win and numeric suffix retains model extension", result.error);
	ok &= expect(setLevelMapEntityProperty(&alias, 2, "_skin", "", &error) && setLevelMapEntityProperty(&alias, 2, "_frame", "", &error)
		&& setLevelMapEntityProperty(&alias, 2, "frame", "0", &error), "empty preferred values authored");
	result = prepare(alias, 2);
	ok &= expect(result.succeeded() && result.mesh.skinPaths == QStringList{"models/alternate"} && result.receipt.value("frame").toInt() == 0, "empty preferred keys fall back to aliases");
	auto hidden = map;
	setLevelMapEntityProperty(&hidden, 1, "_skin", "native", &error);
	result = prepare(hidden);
	ok &= expect(result.succeeded() && result.mesh.surfaces.isEmpty() && result.receipt.value("omittedSurfaces").toInt() == 2,
		"native surface names are not silently interpreted as compiler material names");
	auto history = map;
	setLevelMapEntityProperty(&history, 1, "_skin", "body", &error);
	ok &= expect(prepare(history).mesh.surfaces.size() == 1 && undoLevelMapEdit(&history, &error)
		&& prepare(history).mesh.surfaces.size() == 2 && redoLevelMapEdit(&history, &error) && prepare(history).mesh.surfaces.size() == 1,
		"shared undo and redo preserve appearance parameters");
	auto remapped = map;
	for (const auto& [key, value] : QVector<QPair<QString, QString>>{{"_remap0", "*;models/old_body"}, {"_remap1", "body;models/old_head"},
		{"_remap2", "new_body;models/alternate"}, {"_remap3", "new_body;models/old_body"}, {"_remap4", "*;models/new_body"}}) {
		ok &= expect(setLevelMapEntityProperty(&remapped, 1, key, value, &error), "author ordered remap", error);
	}
	result = prepare(remapped);
	ok &= expect(result.succeeded() && result.mesh.skinPaths == QStringList{"models/alternate", "models/new_body"}, "longest suffix then first equal-length match; last wildcard fallback", result.error);
	auto normalized = map;
	setLevelMapEntityProperty(&normalized, 1, "model", "MODELS\\test.md3", &error);
	ok &= expect(levelModelAppearance(normalized, normalized.entities[1]).cacheKey == levelModelAppearance(map, map.entities[1]).cacheKey, "equivalent package paths share appearance key");
	const auto assets = resolveLevelPreviewAssets(map, archive);
	ok &= expect(assets.complete && assets.models.size() == 3 && assets.readyCount() == 5 && assets.problemCount() == 0, "three appearances of one model coexist", levelPreviewAssetsText(assets));
	LevelPreviewAssetOptions limited; limited.modelLimit = 1;
	const auto limitedAssets = resolveLevelPreviewAssets(map, archive, limited);
	ok &= expect(!limitedAssets.complete && limitedAssets.models.size() == 1 && limitedAssets.unavailableModels == 2, "appearance count budget covers variations of one source");
	limited = {}; limited.totalReadByteLimit = fixture.files.value("assets/models/test.md3").size();
	ok &= expect(!resolveLevelPreviewAssets(map, archive, limited).complete, "skin reads participate in total preview byte budget");
	LevelMapPreviewMeshOptions geometry; geometry.modelMeshes = assets.models;
	const auto preview = buildLevelMapPreviewMesh(map, geometry);
	ok &= expect(preview.modelInstances == 3 && preview.triangles == 60 && preview.owners.size() == 60, "camera mesh retains each appearance and entity ownership");
	int triangles[4]{};
	for (const auto& owner : preview.owners) { if (owner.objectId >= 0 && owner.objectId < 4) { ++triangles[owner.objectId]; } }
	ok &= expect(triangles[1] == 24 && triangles[2] == 12 && triangles[3] == 24, "omitted surface belongs only to its configured instance");
	const auto dependencies = inspectLevelDependencies(map, archive);
	ok &= expect(dependencies.canExport() && dependencies.modelAppearances.size() == 3 && dependencies.resolvedPaths.contains("models/test_blue.skin")
		&& dependencies.resolvedPaths.contains("models/test_body.skin"), "dependency closure carries compiler skin files", levelDependencyReportText(dependencies));
	bool ownership = false;
	for (const auto& row : dependencies.dependencies) { if (row.reference == "models/new_body") { ownership = row.selectors == QStringList{"entity:1"}; } }
	ok &= expect(ownership, "effective materials retain their exact map users");
	const auto bundleDependencies = inspectModelMaterialDependencies(source, archive);
	bool alternate = false;
	for (const auto& row : bundleDependencies.dependencies) { alternate |= row.reference == "models/alternate"; }
	ok &= expect(alternate, "standalone model bundles still review all retained slots");
	for (const auto& [key, value] : QVector<QPair<QString, QString>>{{"_skin", "absent"}, {"_skin", "../blue"}, {"_frame", "-1"},
		{"_frame", "1"}, {"_frame", "2"}, {"_frame", "1junk"}, {"_remap", "missing separator"}, {"_remap", "x;../escape"}, {"_remap", "x;" + QString(64, 'a')}}) {
		auto invalid = map; setLevelMapEntityProperty(&invalid, 1, key, value, &error);
		const auto failed = prepare(invalid);
		const auto badAssets = resolveLevelPreviewAssets(invalid, archive);
		const auto badDependencies = inspectLevelDependencies(invalid, archive);
		ok &= expect(!failed.succeeded() && !failed.error.isEmpty() && !badAssets.complete && badAssets.unavailableModels == 1
			&& !badDependencies.canExport(), "invalid appearance fails preview and packaging together", failed.error);
	}
	AlteredReader altered(archive); altered.path = "models/test_blue.skin"; altered.duplicate = true;
	ok &= expect(!prepareLevelModelAppearance(source, levelModelAppearance(map, map.entities[1]), altered).succeeded(), "duplicate skin identity refused");
	altered.duplicate = false; altered.truncate = true;
	ok &= expect(!prepareLevelModelAppearance(source, levelModelAppearance(map, map.entities[1]), altered).succeeded(), "partial skin read refused");
	ModelWorkControl control; int checkpoints = 0; control.cancelled = [&] { return ++checkpoints > 5; };
	result = prepareLevelModelAppearance(source, levelModelAppearance(map, map.entities[1]), archive, control);
	ok &= expect(result.cancelled && result.mesh.surfaces.isEmpty(), "cancelled preparation publishes no geometry");
	PackageStagingModel staging;
	ok &= expect(staging.loadBaseArchive(archive, &error) && staging.addBytes("models/old_body,models/new_head\n", "models/test_blue.skin", &error,
		PackageStageConflictResolution::ReplaceExisting), "stage replacement compiler skin", error);
	PackageStagingArchive staged(staging);
	const auto stagedAssets = resolveLevelPreviewAssets(map, staged);
	const auto stagedDependencies = inspectLevelDependencies(map, staged);
	ok &= expect(stagedAssets.complete && stagedDependencies.canExport() && stagedAssets.models.value(levelModelAppearance(map, map.entities[1]).cacheKey).surfaces.size() == 1,
		"preview and dependency checks see staged skin bytes");
	ok &= expect(staging.addBytes("BODY_1,models/new_body\nhead,models/new_head\ntag_mount,\n", "models/test_default.skin", &error), "stage implicit importer skin", error);
	PackageStagingArchive withDefault(staging);
	auto baseline = fixture.map(&error);
	result = prepareLevelModelAppearance(source, levelModelAppearance(baseline, baseline.entities[1]), withDefault);
	ok &= expect(result.succeeded() && result.mesh.skinPaths == QStringList{"models/new_body", "models/new_head"}, "implicit importer skin uses exact surface names before compiler remaps", result.error);
	ok &= expect(inspectLevelDependencies(baseline, withDefault).resolvedPaths.contains("models/test_default.skin"), "implicit importer skin included in export closure");
	const auto beforeDefault = prepareLevelModelAppearance(source, levelModelAppearance(baseline, baseline.entities[1]), staged);
	ok &= expect(beforeDefault.mesh.skinPaths == QStringList{"models/old_body", "models/old_head"}, "previous immutable staging reader does not see later skin addition");
	for (const auto& malformed : QVector<QByteArray>{"models/old_body," + QByteArray(64, 'a') + "\n", QByteArrayLiteral("models/old_body,models/new_body\0x"),
		"models/old_body,models/new_body\r\nmodels/old_head,models/new_head\n", "replace models/old_body models/new_body junk\n", "head,\n"}) {
		PackageStagingModel invalid; invalid.loadBaseArchive(archive, &error);
		invalid.addBytes(malformed, "models/test_blue.skin", &error, PackageStageConflictResolution::ReplaceExisting);
		PackageStagingArchive inputArchive(invalid);
		ok &= expect(!prepareLevelModelAppearance(source, levelModelAppearance(map, map.entities[1]), inputArchive).succeeded(), "malformed and oversized compiler mappings refused");
	}
	LevelDocumentSaveRequest save; save.path = fixture.path("instance.map");
	ok &= expect(writeLevelDocument(map, save).succeeded(), "save entity appearances through map writer");
	LevelMapLoadRequest load; load.path = save.path; load.engineHint = "idTech3";
	LevelMapDocument reopened;
	ok &= expect(loadLevelMap(load, &reopened, &error) && resolveLevelPreviewAssets(reopened, archive).models.size() == 3, "reopened map resolves all appearances", error);
	const auto run = [&](const QStringList& words, int expected) {
		QProcess process; process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--json", "--settings-file", fixture.path("settings.ini")} + words);
		const bool finished = process.waitForFinished(60000);
		const auto output = process.readAllStandardOutput();
		ok &= expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI process outcome", QString::fromUtf8(output + process.readAllStandardError()));
		return QJsonDocument::fromJson(output).object();
	};
	auto cli = run({"map", "materials", save.path, "--package", fixture.path("assets"), "--engine", "idTech3", "--geometry"}, 0);
	ok &= expect(cli.value("materials").toObject().value("modelAppearances").toArray().size() == 3 && cli.value("geometry").toObject().value("triangles").toInt() == 60, "CLI reviews placed appearances and geometry");
	run({"map", "edit", save.path, "--engine", "idTech3", "--entity", "3", "--set", "_skin=body", "--set", "_frame=0", "--output", fixture.path("edited.map")}, 0);
	cli = run({"map", "materials", fixture.path("edited.map"), "--package", fixture.path("assets"), "--engine", "idTech3", "--geometry"}, 0);
	ok &= expect(cli.value("geometry").toObject().value("triangles").toInt() == 48, "CLI authoring changes only the selected model instance");
	PackageStagingModel draft; draft.loadBaseArchive(archive, &error);
	draft.addBytes("models/old_body,models/new_head\n", "models/test_blue.skin", &error, PackageStageConflictResolution::ReplaceExisting);
	ok &= expect(PackageDraft::save(fixture.path("assets.vibepackage"), &draft, false, &error), "save portable draft with replaced compiler skin", error);
	cli = run({"map", "materials", save.path, "--package", fixture.path("assets.vibepackage"), "--engine", "idTech3", "--geometry"}, 0);
	ok &= expect(cli.value("geometry").toObject().value("triangles").toInt() == 48, "CLI material review resolves staged draft content");
	ok &= expect(modelStateFingerprint(source) == fingerprint && fixture.unchanged(), "source models, animation, native skins and asset files unchanged");
	std::cout << checks << " checks; " << (ok ? "passed" : "FAILED") << '\n';
	return ok ? 0 : 1;
}
