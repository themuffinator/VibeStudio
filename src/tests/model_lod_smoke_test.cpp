// Linked detail levels (core/model_lod.h): the recipe an editable source stores
// (schema 13) and its undo, generated and hand-made levels, the consistency
// check the engine's loader needs (frames and surface names on every level),
// inspection of existing sets on disk and in packages, R_ComputeLOD's switch
// distances, staging a set as one package step, the level dependency review's
// detail-level companions, the detail files a write leaves beside the model
// (on disk and in packages, and Enemy Territory's MDC files), hand-made
// levels that follow a source saved elsewhere, and the CLI (model lod
// --inspect, --save-recipe, --use-recipe, --level-source, --target-triangles,
// and model build's review of the levels beside its output).
#include "core/level_dependencies.h"
#include "core/level_map.h"
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_lod.h"
#include "core/model_targets.h"
#include "core/package_archive.h"
#include "core/package_staging.h"
#include "tests/model_targets_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
ModelTargetProfile md3Target(const QString &id = {})
{
	ModelTargetProfile result;
	expect(findModelTargetProfile(QStringLiteral("md3"), id, &result), "the MD3 target exists");
	return result;
}
bool hasFinding(const ModelLodValidation &validation, ModelLodSeverity severity, const QString &code, int level = -1)
{
	return std::any_of(validation.findings.cbegin(), validation.findings.cend(), [&](const ModelLodFinding &finding)
					   { return finding.severity == severity && finding.code == code && (level < 0 || finding.level == level); });
}
bool put(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
QByteArray bytesOf(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray md3Of(const ModelMesh &mesh)
{
	QString error;
	const auto bytes = exportEditableModel(mesh, QStringLiteral("md3"), 0, &error);
	expect(!bytes.isEmpty(), "the fixture exports as MD3");
	return bytes;
}
// A prop with two surfaces and two frames.
ModelMesh prop(int columns = 12)
{
	auto mesh = targetMesh({targetGridSurface(QStringLiteral("body"), columns, columns), targetGridSurface(QStringLiteral("cap"), 4, 4, 8)});
	mesh.frames.append(mesh.frames[0]);
	mesh.frames[1].name = QStringLiteral("lift");
	for (auto &surface : mesh.surfaces)
	{
		auto pose = surface.frames[0];
		for (auto &p : pose.positions)
			p.z += 2;
		surface.frames.append(pose);
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void checkRecipe(const QString &directory)
{
	QString error;
	QVector<ModelLodRecipeLevel> recipe = modelLodGeneratedRecipe(2, 0.4);
	// A triangle target replaces the ratio, which keeps its default.
	recipe[1].ratio = 0.5;
	recipe[1].targetTriangles = 40;
	QVector<ModelLodRecipeLevel> parsed;
	expect(parseModelLodRecipe(modelLodRecipeJson(recipe), &parsed, &error) && parsed == recipe && parsed[0].preserveBoundary && !parsed[1].preserveBoundary,
		   "a recipe round-trips through its JSON");
	const auto malformed = [&](const QJsonArray &json) { return !parseModelLodRecipe(json, &parsed, &error); };
	expect(malformed({QJsonObject{{QStringLiteral("level"), 2}, {QStringLiteral("mode"), QStringLiteral("generated")}, {QStringLiteral("ratio"), 0.5}}}) &&
			   malformed({QJsonObject{{QStringLiteral("level"), 1}, {QStringLiteral("mode"), QStringLiteral("mirror")}}}) &&
			   malformed({QJsonObject{{QStringLiteral("level"), 1}, {QStringLiteral("mode"), QStringLiteral("generated")}, {QStringLiteral("ratio"), 0.99}}}) &&
			   malformed({QJsonObject{{QStringLiteral("level"), 1}, {QStringLiteral("mode"), QStringLiteral("source")}}}) &&
			   malformed({QJsonObject{{QStringLiteral("level"), 1}, {QStringLiteral("mode"), QStringLiteral("generated")}, {QStringLiteral("ratio"), 0.5},
									  {QStringLiteral("colour"), 1}}}),
		   "gaps, unknown modes and fields, ratios out of range and hand-made levels without a model are refused");
	// The editable source stores it as schema 13; undo and recovery follow.
	auto mesh = prop();
	ModelDocument document;
	expect(document.setMesh(mesh, &error), "open the prop");
	const auto base = document.revisionFingerprint();
	auto withRecipe = mesh;
	withRecipe.detailLevels = recipe;
	expect(document.adopt(withRecipe, {}, base, &error) && document.revisionFingerprint() != base && document.mesh().detailLevels == recipe,
		   "storing a recipe is one document change");
	const auto path = QDir(directory).filePath(QStringLiteral("recipe/prop.mesh.json"));
	QDir().mkpath(QFileInfo(path).absolutePath());
	expect(document.save(path, true, &error), "save the source with its recipe");
	const auto json = QJsonDocument::fromJson(bytesOf(path)).object();
	expect(json.value(QStringLiteral("version")).toInt() == 13 && json.value(QStringLiteral("detailLevels")).toArray().size() == 2,
		   "the source records its detail levels in schema 13");
	ModelDocument reopened;
	expect(reopened.load(path, &error) && reopened.mesh().detailLevels == recipe, "the recipe loads back");
	auto old = json;
	old.insert(QStringLiteral("version"), 11);
	ModelMesh refused;
	expect(!parseEditableModel(QJsonDocument(old).toJson(), &refused, &error), "an older schema cannot carry a recipe");
	// Recovery drafts are the source's JSON, so they keep the recipe too.
	ModelMesh recovered;
	expect(parseEditableModel(QJsonDocument(editableModelJson(document.mesh(), &error)).toJson(), &recovered, &error) && recovered.detailLevels == recipe,
		   "a recovery draft keeps the recipe");
	expect(document.undo() && document.mesh().detailLevels.isEmpty() && document.revisionFingerprint() == base, "undo removes the recipe");
	auto tooMany = mesh;
	tooMany.detailLevels = modelLodGeneratedRecipe(modelLodMaxLevels + 1, 0.5);
	expect(!validateEditableModel(tooMany).isEmpty(), "a source cannot own more levels than any game loads");
}

void checkLevels(const QString &directory)
{
	QString error;
	const auto mesh = prop();
	const int baseTriangles = mesh.surfaces[0].triangles.size() + mesh.surfaces[1].triangles.size();
	// Generated levels: a share, then a triangle target.
	auto recipe = modelLodGeneratedRecipe(2, 0.5);
	recipe[1].ratio = 0.5;
	recipe[1].targetTriangles = 60;
	QVector<ModelLodLevel> levels;
	expect(buildModelLodsFromRecipe(mesh, recipe, {}, &levels, &error) && levels.size() == 2 && levels[0].triangles < baseTriangles &&
			   levels[1].triangles <= 80 && levels[1].triangles < levels[0].triangles,
		   "generated levels keep a share, or reduce to a triangle target");
	ModelLodValidation validation;
	QVector<ModelMesh> meshes{levels[0].mesh, levels[1].mesh};
	expect(validateModelLodSet(mesh, meshes, md3Target(), &validation, &error) && validation.ok(), "a generated set is consistent");
	const auto rows = modelLodSurfaceTable(mesh, meshes);
	expect(rows.size() == 2 && rows[0].surface == QStringLiteral("body") && rows[0].triangles.size() == 3 &&
			   rows[0].triangles[0] == mesh.surfaces[0].triangles.size(),
		   "the table has a row per surface and a column per level");
	// A hand-made level read from its own source, relative to the base.
	auto handMade = levels[0].mesh;
	const auto handPath = QDir(directory).filePath(QStringLiteral("levels/prop_low.mesh.json"));
	QDir().mkpath(QFileInfo(handPath).absolutePath());
	ModelDocument document;
	expect(document.setMesh(handMade, &error) && document.save(handPath, true, &error), "save a hand-made level");
	QVector<ModelLodRecipeLevel> manual{modelLodGeneratedRecipe(1, 0.5).first()};
	manual[0].generated = false;
	manual[0].source = QStringLiteral("levels/prop_low.mesh.json");
	expect(buildModelLodsFromRecipe(mesh, manual, directory, &levels, &error) && levels.size() == 1 &&
			   QFileInfo(levels[0].source) == QFileInfo(handPath) && levels[0].triangles == handMade.surfaces[0].triangles.size() + handMade.surfaces[1].triangles.size(),
		   "a hand-made level is read from its file beside the base source");
	manual[0].source = QStringLiteral("levels/missing.mesh.json");
	expect(!buildModelLodsFromRecipe(mesh, manual, directory, &levels, &error) && error.contains(QStringLiteral("missing.mesh.json")),
		   "a missing hand-made level names its file");
	// What the engine needs of every level.
	auto shortLevel = meshes[0];
	shortLevel.frames.removeLast();
	for (auto &surface : shortLevel.surfaces)
		surface.frames.removeLast();
	updateEditableModelMetadata(&shortLevel);
	expect(validateModelLodSet(mesh, {shortLevel}, md3Target(), &validation, &error) && !validation.ok() &&
			   hasFinding(validation, ModelLodSeverity::Error, QStringLiteral("frames"), 1),
		   "a level with fewer frames than the base is an error");
	auto renamed = meshes[0];
	renamed.surfaces[1].name = QStringLiteral("lid");
	expect(validateModelLodSet(mesh, {renamed}, md3Target(), &validation, &error) && hasFinding(validation, ModelLodSeverity::Error, QStringLiteral("surface-names")) &&
			   hasFinding(validation, ModelLodSeverity::Warning, QStringLiteral("surfaces")),
		   "a surface the base lacks is an error, and a base surface the level drops a warning");
	auto suffixed = meshes[0];
	suffixed.surfaces[0].name = QStringLiteral("BODY_1");
	expect(validateModelLodSet(mesh, {suffixed}, md3Target(), &validation, &error) && validation.ok(),
		   "names match after Quake III's normalisation (lower case, no final _1)");
	expect(validateModelLodSet(mesh, {mesh}, md3Target(), &validation, &error) && validation.ok() &&
			   hasFinding(validation, ModelLodSeverity::Warning, QStringLiteral("triangles")),
		   "a level with no fewer triangles than the one above is a warning");
	const auto big = targetMesh({targetGridSurface(QStringLiteral("body"), 40, 30), targetGridSurface(QStringLiteral("cap"), 4, 4, 8)});
	expect(validateModelLodSet(prop(40), {big}, md3Target(), &validation, &error) && hasFinding(validation, ModelLodSeverity::Error, QStringLiteral("target")),
		   "a level over the target's limits is an error");
	// Switch distances, against hand-computed R_ComputeLOD values: cg_fov 90
	// at 4:3 gives tan(fov_y / 2) = 0.75; with r_lodscale 5 and three models,
	// level k starts at 100 * 5 / (0.75 * (1 - k / 3)).
	const auto distances = modelQuake3LodSwitchDistances(100, 3);
	expect(distances.size() == 3 && distances[0] == 0 && std::abs(distances[1] - 1000) < 1e-6 && std::abs(distances[2] - 2000) < 1e-6,
		   "the switch distances match R_ComputeLOD");
	ModelLodView biased;
	biased.lodBias = 1;
	const auto shifted = modelQuake3LodSwitchDistances(100, 3, biased);
	expect(shifted[1] == 0 && std::abs(shifted[2] - 1000) < 1e-6, "r_lodbias 1 shows each level one step sooner");
	expect(modelQuake3LodSwitchDistances(100, 1) == QVector<double>{0}, "a model without levels never switches");
	auto boxed = targetMesh({targetGridSurface(QStringLiteral("body"), 2, 2)});
	boxed.surfaces[0].frames[0].positions = {{-3, 0, 0}, {3, 4, 0}, {0, -4, 0}, {1, 1, 0}};
	expect(std::abs(modelLodRadius(boxed) - 5) < 1e-6, "the radius is RadiusFromBounds of the base's first frame");
}

void checkInspection(const QString &directory)
{
	QString error;
	const auto mesh = prop();
	const auto base = QDir(directory).filePath(QStringLiteral("inspect/lamp.md3"));
	QDir().mkpath(QFileInfo(base).absolutePath());
	ModelLodSet set;
	expect(prepareModelLodSet(mesh, 2, 0.5, base, {}, {}, false, &set, &error) && publishModelLodSet(&set, false, &error), "write a generated set");
	ModelLodInspection inspection;
	expect(inspectModelLodSet(base, modelLodDiskReader(), md3Target(), &inspection, &error) && inspection.levelsPresent() == 2 && inspection.validation.ok() &&
			   inspection.switchDistances.size() == 3 && inspection.surfaces.size() == 2,
		   "an exported set inspects as consistent, with its switch distances");
	expect(modelLodInspectionJson(inspection).value(QStringLiteral("ready")).toBool() &&
			   modelLodInspectionJson(inspection).value(QStringLiteral("files")).toArray().size() == 3,
		   "the inspection's JSON lists the base and both levels");
	// A level with fewer frames than the base would be read past its end.
	auto still = mesh;
	still.frames.removeLast();
	for (auto &surface : still.surfaces)
		surface.frames.removeLast();
	updateEditableModelMetadata(&still);
	expect(put(modelLodPath(base, 2), md3Of(still)) && inspectModelLodSet(base, modelLodDiskReader(), md3Target(), &inspection, &error) &&
			   hasFinding(inspection.validation, ModelLodSeverity::Error, QStringLiteral("frames"), 2),
		   "an existing level with fewer frames is an error");
	// One with more frames never shows them, which is an error too.
	auto longer = mesh;
	longer.frames.append(longer.frames.last());
	longer.frames.last().name = QStringLiteral("extra");
	for (auto &surface : longer.surfaces)
		surface.frames.append(surface.frames.last());
	updateEditableModelMetadata(&longer);
	ModelLodValidation extra;
	expect(validateModelLodSet(mesh, {longer}, md3Target(), &extra, &error) && hasFinding(extra, ModelLodSeverity::Error, QStringLiteral("frames"), 1) &&
			   extra.findings.first().message.contains(QStringLiteral("never show")),
		   "a level with more frames than the base is an error that says its extra frames never show");
	// A gap leaves an empty slot the renderer can pick.
	expect(QFile::remove(modelLodPath(base, 1)) && inspectModelLodSet(base, modelLodDiskReader(), md3Target(), &inspection, &error) &&
			   hasFinding(inspection.validation, ModelLodSeverity::Error, QStringLiteral("surfaces"), 2),
		   "a level after a missing one is an error");
	// After the gap, level 2's findings and table column are still level 2's.
	expect(hasFinding(inspection.validation, ModelLodSeverity::Error, QStringLiteral("frames"), 2) &&
			   !hasFinding(inspection.validation, ModelLodSeverity::Error, QStringLiteral("frames"), 1) &&
			   std::all_of(inspection.validation.findings.cbegin(), inspection.validation.findings.cend(),
						   [](const ModelLodFinding &finding) { return finding.level != 1; }) &&
			   inspection.surfaces.size() == 2 && inspection.surfaces[0].triangles.size() == 3 && inspection.surfaces[0].triangles[1] == -1 &&
			   inspection.surfaces[0].triangles[2] > 0,
		   "after a missing level the next one's findings and surface column keep its own number");
	// A level the target never loads, and Enemy Territory's MDC first.
	expect(put(modelLodPath(base, 1), md3Of(mesh)) && put(modelLodPath(base, 2), md3Of(mesh)) && put(modelLodPath(base, 3), md3Of(mesh)) &&
			   inspectModelLodSet(base, modelLodDiskReader(), md3Target(), &inspection, &error) &&
			   hasFinding(inspection.validation, ModelLodSeverity::Warning, QStringLiteral("target"), 3),
		   "a third level is never loaded by Quake III");
	auto mdc = base;
	mdc.chop(1);
	expect(put(mdc + QLatin1Char('c'), "IDPC") && inspectModelLodSet(base, modelLodDiskReader(), md3Target(QStringLiteral("et")), &inspection, &error) &&
			   hasFinding(inspection.validation, ModelLodSeverity::Warning, QStringLiteral("target"), 0) &&
			   !hasFinding(inspection.validation, ModelLodSeverity::Warning, QStringLiteral("target"), 3),
		   "Enemy Territory loads the third level, and warns that a stale MDC loads first");
	// It tries each level's MDC first too; a level that is only an MDC loads,
	// so the levels after it are no gap.
	const auto levelMdc = modelLodMdcPath(modelLodPath(base, 1));
	expect(levelMdc.endsWith(QStringLiteral("lamp_1.mdc")) && put(levelMdc, "IDPC") &&
			   inspectModelLodSet(base, modelLodDiskReader(), md3Target(QStringLiteral("et")), &inspection, &error) &&
			   std::any_of(inspection.validation.findings.cbegin(), inspection.validation.findings.cend(), [&](const ModelLodFinding &finding)
						   { return finding.level == 1 && finding.severity == ModelLodSeverity::Warning && finding.message.contains(QStringLiteral("lamp_1.mdc")); }),
		   "a detail level's own stale MDC is a warning");
	expect(QFile::remove(modelLodPath(base, 1)) && inspectModelLodSet(base, modelLodDiskReader(), md3Target(QStringLiteral("et")), &inspection, &error) &&
			   !hasFinding(inspection.validation, ModelLodSeverity::Error, QStringLiteral("surfaces")) &&
			   std::any_of(inspection.validation.findings.cbegin(), inspection.validation.findings.cend(),
						   [](const ModelLodFinding &finding) { return finding.level == 1 && finding.message.contains(QStringLiteral("is only")); }),
		   "a level present only as an MDC is no gap, and the review says it checks MD3 files");
	expect(QFile::remove(levelMdc) && QFile::remove(mdc + QLatin1Char('c')) && put(modelLodPath(base, 1), md3Of(mesh)), "restore the MD3 level");
	expect(!inspectModelLodSet(QDir(directory).filePath(QStringLiteral("inspect/none.md3")), modelLodDiskReader(), md3Target(), &inspection, &error) &&
			   error.contains(QStringLiteral("none.md3")),
		   "a missing base model is refused");
	// Package entries read through the same reader interface.
	PackageArchive archive;
	expect(archive.load(QFileInfo(base).absolutePath(), &error), "open the folder as a package");
	const auto reader = [&archive](const QString &path, QByteArray *bytes, QString *problem)
	{
		for (const auto &entry : archive.entries())
		{
			if (entry.virtualPath.compare(path, Qt::CaseInsensitive) == 0)
				return archive.readEntryBytes(entry.virtualPath, bytes, problem);
		}
		return false;
	};
	expect(inspectModelLodSet(QStringLiteral("LAMP.md3"), reader, md3Target(), &inspection, &error) && inspection.levelsPresent() == 2,
		   "a set in a package is inspected through its entries");
}

void checkStaging(const QString &directory)
{
	QString error;
	const auto assets = QDir(directory).filePath(QStringLiteral("stage-assets"));
	expect(put(QDir(assets).filePath(QStringLiteral("readme.txt")), "fixture"), "write a package fixture");
	PackageArchive archive;
	PackageStagingModel staging;
	expect(archive.load(assets, &error) && staging.loadBaseArchive(archive, &error), "load the package");
	const auto mapPath = QDir(directory).filePath(QStringLiteral("stage.map"));
	LevelMapDocument map;
	expect(put(mapPath, "{\n\"classname\" \"worldspawn\"\n}\n") && loadLevelMap({mapPath, {}, QStringLiteral("idTech3")}, &map, &error), "load a Quake III map");
	ModelLodSet set;
	auto mesh = prop();
	mesh.detailLevels = modelLodGeneratedRecipe(2, 0.5);
	expect(prepareModelLodFiles(mesh, mesh.detailLevels, {}, QStringLiteral("models/props/lamp.md3"), {}, false, &set, &error) && set.files.size() == 3 &&
			   set.files[2].path == QStringLiteral("models/props/lamp_2.md3"),
		   "a set's files are prepared for package paths");
	QVector<QPair<QString, QByteArray>> files;
	for (const auto &file : std::as_const(set.files))
		files.append({file.path, file.bytes});
	const int before = int(staging.operations().size());
	expect(stageModelExports(files, &staging, &map, {64, 0, 0, true}, false, &error) && map.entities.size() == 2, "the set is staged and the base placed");
	PackageStagingArchive snapshot(staging);
	QByteArray staged;
	expect(snapshot.readEntryBytes(QStringLiteral("models/props/lamp_1.md3"), &staged, &error) && staged == set.files[1].bytes, "each level is staged");
	// The level dependency review lists the levels as the model's companions.
	const auto dependencies = inspectLevelDependencies(map, snapshot);
	const auto lod = std::find_if(dependencies.dependencies.cbegin(), dependencies.dependencies.cend(), [](const LevelDependency &dependency)
								  { return dependency.kind == QStringLiteral("model-lod") && dependency.reference == QStringLiteral("models/props/lamp_2.md3"); });
	expect(lod != dependencies.dependencies.cend() && lod->status == LevelDependencyStatus::Resolved && dependencies.resolvedPaths.contains(QStringLiteral("models/props/lamp_1.md3")),
		   "the dependency review includes each detail level the package has");
	expect(staging.undo() && staging.operations().size() == before && !PackageStagingArchive(staging).readEntryBytes(QStringLiteral("models/props/lamp.md3"), &staged, &error),
		   "one undo removes the base model and every level");
	files[2].first = QStringLiteral("models/props/a-name-long-enough-to-pass-the-sixty-three-byte-limit_2.md3");
	expect(!stageModelExports(files, &staging, nullptr, {}, false, &error) && staging.operations().size() == before, "a file that cannot stage stages none of them");
}

// Writing a base MD3 leaves the detail files beside it; the game still loads
// them, so each write reviews them against the new model.
void checkSiblings(const QString &directory)
{
	QString error;
	const auto mesh = prop();
	auto still = mesh;
	still.frames.removeLast();
	for (auto &surface : still.surfaces)
		surface.frames.removeLast();
	updateEditableModelMetadata(&still);
	QVector<ModelLodLevel> levels;
	expect(buildModelLods(mesh, 2, 0.5, &levels, &error), "build two matching levels");
	const auto base = QDir(directory).filePath(QStringLiteral("siblings/p.md3"));
	expect(put(base, md3Of(mesh)) && put(modelLodPath(base, 1), md3Of(still)) && put(modelLodPath(base, 2), md3Of(levels[1].mesh)),
		   "write a base with an old first level and a matching second");
	ModelLodSiblingReview review;
	expect(reviewModelLodSiblings(mesh, base, 0, modelLodDiskReader(), md3Target(), &review, &error) && review.files.size() == 2 &&
			   !review.validation.ok() && hasFinding(review.validation, ModelLodSeverity::Error, QStringLiteral("frames"), 1) &&
			   hasFinding(review.validation, ModelLodSeverity::Warning, QStringLiteral("stale"), 2),
		   "a base written alone leaves both levels, and the old one no longer fits its frames");
	const auto lines = modelLodSiblingLines(review).join(QLatin1Char('\n'));
	expect(lines.contains(QStringLiteral("p_1.md3")) && lines.contains(QStringLiteral("wrongly")) && modelLodSiblingJson(review).value(QStringLiteral("consistent")) == false,
		   "the review names the files and says the game would read them wrongly");
	expect(reviewModelLodSiblings(mesh, base, 1, modelLodDiskReader(), md3Target(), &review, &error) && review.files.size() == 1 &&
			   review.files[0].level == 2 && review.validation.ok(),
		   "writing the first level as well leaves only the matching second");
	expect(reviewModelLodSiblings(mesh, base, 2, modelLodDiskReader(), md3Target(), &review, &error) && review.isEmpty() &&
			   reviewModelLodSiblings(mesh, base, 0, modelLodDiskReader(), md3Target(QStringLiteral("gzdoom")), &review, &error) && review.isEmpty(),
		   "a full set, or a target that loads no levels, leaves nothing to review");
	expect(put(modelLodMdcPath(base), "IDPC") && reviewModelLodSiblings(mesh, base, 2, modelLodDiskReader(), md3Target(QStringLiteral("et")), &review, &error) &&
			   review.files.size() == 1 && review.files[0].level == 0 && hasFinding(review.validation, ModelLodSeverity::Error, QStringLiteral("mdc"), 0),
		   "on Enemy Territory an MDC beside the model loads in its place");
	expect(QFile::remove(modelLodMdcPath(base)), "remove the MDC");
	// A detail set with fewer levels than the folder holds leaves the rest.
	ModelLodSet set;
	expect(prepareModelLodSet(mesh, 1, 0.5, base, {}, {}, false, &set, &error) && set.stale.files.size() == 1 && set.stale.files[0].level == 2,
		   "a one-level set over a two-level one names the second level it leaves");
	// Package entries are matched without case, as the game's file system does.
	const auto memory = modelLodMemoryReader({{QStringLiteral("Models/Props/P_1.MD3"), QByteArray("x")}});
	QByteArray bytes;
	expect(memory(QStringLiteral("models/props/p_1.md3"), &bytes, &error) && bytes == "x" && !memory(QStringLiteral("models/props/p_2.md3"), &bytes, &error),
		   "the memory reader matches paths without case");
	const auto siblingPaths = modelLodSiblingPaths(QStringLiteral("models/p.md3"));
	expect(siblingPaths.contains(QStringLiteral("models/p.mdc")) && siblingPaths.contains(QStringLiteral("models/p_3.md3")) &&
			   siblingPaths.contains(QStringLiteral("models/p_2.mdc")) && siblingPaths.size() == 1 + 2 * modelLodMaxLevels,
		   "the sibling paths cover each level's MD3 and every MDC");
	// In a package: the staged files the review reads, and the dependency
	// review's note on a level that does not fit its model.
	const auto assets = QDir(directory).filePath(QStringLiteral("sibling-assets"));
	expect(put(QDir(assets).filePath(QStringLiteral("models/props/crate.md3")), md3Of(mesh)) &&
			   put(QDir(assets).filePath(QStringLiteral("models/props/crate_1.md3")), md3Of(still)),
		   "write a package whose detail level is out of date");
	PackageArchive archive;
	PackageStagingModel staging;
	expect(archive.load(assets, &error) && staging.loadBaseArchive(archive, &error), "load the package");
	const auto found = readStagedModelFiles(staging, modelLodSiblingPaths(QStringLiteral("Models/Props/Crate.md3")));
	expect(found.size() == 1 && found.contains(QStringLiteral("models/props/crate_1.md3")), "staging reads the detail files the package holds");
	expect(reviewModelLodSiblings(mesh, QStringLiteral("models/props/crate.md3"), 0, modelLodMemoryReader(found), md3Target(), &review, &error) &&
			   hasFinding(review.validation, ModelLodSeverity::Error, QStringLiteral("frames"), 1),
		   "the package's old level is checked against the model being staged");
	const auto mapPath = QDir(directory).filePath(QStringLiteral("siblings.map"));
	LevelMapDocument map;
	expect(put(mapPath, "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"misc_model\"\n\"model\" \"models/props/crate.md3\"\n}\n") &&
			   loadLevelMap({mapPath, {}, QStringLiteral("idTech3")}, &map, &error),
		   "load a map that places the model");
	const auto dependencies = inspectLevelDependencies(map, archive);
	const auto lod = std::find_if(dependencies.dependencies.cbegin(), dependencies.dependencies.cend(),
								  [](const LevelDependency &dependency) { return dependency.kind == QStringLiteral("model-lod"); });
	expect(lod != dependencies.dependencies.cend() && lod->note.contains(QStringLiteral("does not match")) &&
			   std::any_of(dependencies.warnings.cbegin(), dependencies.warnings.cend(), [](const QString &warning) { return warning.contains(QStringLiteral("crate_1.md3")); }),
		   "the dependency review warns that the detail level does not match its model");
}

// Hand-made levels are stored relative to the source's folder and follow the
// source when it is saved somewhere else.
void checkRelativeSources(const QString &directory)
{
	QString error;
	const auto folder = QDir(directory).filePath(QStringLiteral("relative"));
	const auto low = QDir(folder).filePath(QStringLiteral("low.mesh.json"));
	ModelDocument document;
	auto still = prop();
	expect(document.setMesh(still, &error) && QDir().mkpath(folder) && document.save(low, true, &error), "save a hand-made level");
	auto mesh = prop();
	QVector<ModelLodRecipeLevel> recipe{modelLodGeneratedRecipe(1, 0.5).first()};
	recipe[0].generated = false;
	recipe[0].source = QStringLiteral("low.mesh.json");
	mesh.detailLevels = recipe;
	const auto source = QDir(folder).filePath(QStringLiteral("base.mesh.json"));
	expect(put(source, QJsonDocument(editableModelJson(mesh, &error)).toJson()), "write a source that names its level relatively");
	ModelDocument opened;
	expect(opened.load(source, &error) && QFileInfo(opened.mesh().detailLevels[0].source).isAbsolute() &&
			   QFileInfo(opened.mesh().detailLevels[0].source) == QFileInfo(low),
		   "an open source holds the level's path absolute");
	const auto moved = QDir(folder).filePath(QStringLiteral("sub/base.mesh.json"));
	QDir().mkpath(QFileInfo(moved).absolutePath());
	expect(opened.save(moved, true, &error) &&
			   QJsonDocument::fromJson(bytesOf(moved)).object().value(QStringLiteral("detailLevels")).toArray()[0].toObject().value(QStringLiteral("source")) ==
				   QStringLiteral("../low.mesh.json"),
		   "saved in a subfolder, the source names the level relative to its new folder");
	ModelDocument reopened;
	QVector<ModelLodLevel> levels;
	expect(reopened.load(moved, &error) && buildModelLodsFromRecipe(reopened.mesh(), reopened.mesh().detailLevels, QFileInfo(moved).absolutePath(), &levels, &error) &&
			   levels.size() == 1 && QFileInfo(levels[0].source) == QFileInfo(low),
		   "the moved source still builds its hand-made level");
	expect(reopened.save(source, true, &error) &&
			   QJsonDocument::fromJson(bytesOf(source)).object().value(QStringLiteral("detailLevels")).toArray()[0].toObject().value(QStringLiteral("source")) ==
				   QStringLiteral("low.mesh.json"),
		   "saved back beside the level, the source names it as before");
	// Version 12 is another encoding's number: a recipe needs version 13.
	auto json = QJsonDocument::fromJson(bytesOf(source)).object();
	json.insert(QStringLiteral("version"), 12);
	ModelMesh refused;
	expect(!parseEditableModel(QJsonDocument(json).toJson(), &refused, &error), "a version 12 source cannot carry a recipe");
}

int run(const QString &executable, const QString &settings, const QStringList &arguments, QJsonObject *json)
{
	QProcess process;
	process.start(executable, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), settings} + arguments + QStringList{QStringLiteral("--json")});
	if (!process.waitForFinished(180000))
	{
		process.kill();
		return -1;
	}
	if (json)
		*json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
	return process.exitCode();
}

// The same in text mode, keeping what the command printed and its errors.
int runText(const QString &executable, const QString &settings, const QStringList &arguments, QString *out, QString *err)
{
	QProcess process;
	process.start(executable, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), settings} + arguments);
	if (!process.waitForFinished(180000))
	{
		process.kill();
		return -1;
	}
	*out = QString::fromUtf8(process.readAllStandardOutput());
	*err = QString::fromUtf8(process.readAllStandardError());
	return process.exitCode();
}

void checkCli(const QString &executable, const QString &directory)
{
	QString error;
	const auto settings = QDir(directory).filePath(QStringLiteral("cli.ini"));
	const auto root = QDir(directory).filePath(QStringLiteral("cli-lod"));
	QDir().mkpath(root);
	const auto source = QDir(root).filePath(QStringLiteral("prop.mesh.json"));
	ModelDocument document;
	expect(document.setMesh(prop(), &error) && document.save(source, true, &error), "save the CLI source");
	QJsonObject json;
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), source, QStringLiteral("--use-recipe"), QStringLiteral("--output"),
									  QDir(root).filePath(QStringLiteral("out/prop.md3"))},
			   &json) == 4,
		   "--use-recipe on a source without one exits 4");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("lod"), source, QStringLiteral("--target"), QStringLiteral("rtcw-mp"), QStringLiteral("--levels"),
				QStringLiteral("1"), QStringLiteral("--output"), QDir(root).filePath(QStringLiteral("out/prop.md3"))},
			   &json) == 2 &&
			   json.value(QStringLiteral("message")).toString().contains(QStringLiteral("loads no detail levels")),
		   "a target that loads no detail levels says so instead of offering a range");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("lod"), source, QStringLiteral("--target-triangles"), QStringLiteral("200,80"), QStringLiteral("--save-recipe")},
			   &json) == 0 &&
			   json.value(QStringLiteral("recipeSaved")).toBool() && json.value(QStringLiteral("recipe")).toArray().size() == 2,
		   "--save-recipe stores triangle targets in the source");
	ModelDocument reopened;
	expect(reopened.load(source, &error) && reopened.mesh().detailLevels.size() == 2 && reopened.mesh().detailLevels[1].targetTriangles == 80,
		   "the source now owns its detail levels");
	const auto out = QDir(root).filePath(QStringLiteral("out/prop.md3"));
	QDir().mkpath(QFileInfo(out).absolutePath());
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), source, QStringLiteral("--output"), out}, &json) == 0 &&
			   json.value(QStringLiteral("recipeFromSource")).toBool() && json.value(QStringLiteral("committed")).toInt() == 3 && QFile::exists(modelLodPath(out, 2)),
		   "model lod builds the stored levels without options");
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), QStringLiteral("--inspect"), out}, &json) == 0 &&
			   json.value(QStringLiteral("ready")).toBool() && json.value(QStringLiteral("levelsPresent")).toInt() == 2 &&
			   json.value(QStringLiteral("switchDistances")).toArray().size() == 3,
		   "model lod --inspect reads the set back as consistent");
	// A hand-made level with fewer frames is refused before anything is written.
	auto still = prop();
	still.frames.removeLast();
	for (auto &surface : still.surfaces)
		surface.frames.removeLast();
	updateEditableModelMetadata(&still);
	const auto stillPath = QDir(root).filePath(QStringLiteral("still.mesh.json"));
	expect(document.setMesh(still, &error) && document.save(stillPath, true, &error), "save a level with fewer frames");
	const auto other = QDir(root).filePath(QStringLiteral("out/other.md3"));
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("lod"), source, QStringLiteral("--levels"), QStringLiteral("1"), QStringLiteral("--level-source"),
				QStringLiteral("1=") + stillPath, QStringLiteral("--output"), other},
			   &json) == 4 &&
			   !QFile::exists(other),
		   "a hand-made level without the base's frames is refused");
	expect(put(modelLodPath(out, 1), md3Of(still)) && run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), QStringLiteral("--inspect"), out}, &json) == 4 &&
			   !json.value(QStringLiteral("ready")).toBool(),
		   "--inspect exits 4 when the game would read a level past its frames");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("lod"), QStringLiteral("--inspect"), QStringLiteral("--package"), QFileInfo(out).absolutePath(),
				QStringLiteral("--entry"), QStringLiteral("prop.md3")},
			   &json) == 4 &&
			   json.value(QStringLiteral("files")).toArray().size() == 3,
		   "--inspect reads a set from package entries");
	// In text mode a failed inspection still prints its findings.
	QString printed, errors;
	expect(runText(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), QStringLiteral("--inspect"), out}, &printed, &errors) == 4 &&
			   printed.contains(QStringLiteral("Level 1 has 1 frames")) && errors.contains(QStringLiteral("problem")),
		   "a failed text-mode inspection lists its findings before the refusal");
	// model build writes the base alone: the source's stored levels are not
	// written, and the old level beside it is named.
	expect(run(executable, settings, {QStringLiteral("model"), QStringLiteral("build"), source, QStringLiteral("--output"), out, QStringLiteral("--overwrite")}, &json) == 0 &&
			   QJsonDocument(json.value(QStringLiteral("exportNotes")).toArray()).toJson().contains("writes the base model only") &&
			   !json.value(QStringLiteral("leftInPlace")).toObject().value(QStringLiteral("consistent")).toBool() &&
			   json.value(QStringLiteral("leftInPlace")).toObject().value(QStringLiteral("files")).toArray().size() == 2,
		   "model build notes the stored levels and the levels it leaves, one of which no longer fits");
	expect(runText(executable, settings, {QStringLiteral("model"), QStringLiteral("build"), source, QStringLiteral("--output"), out, QStringLiteral("--overwrite")},
				   &printed, &errors) == 0 &&
			   printed.contains(QStringLiteral("prop_1.md3")) && printed.contains(QStringLiteral("wrongly")),
		   "model build's text names the level that no longer fits");
	// A set of fewer levels leaves the next one in place, and says so.
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("lod"), source, QStringLiteral("--levels"), QStringLiteral("1"), QStringLiteral("--output"), out,
				QStringLiteral("--overwrite")},
			   &json) == 0 &&
			   json.value(QStringLiteral("leftInPlace")).toObject().value(QStringLiteral("files")).toArray().size() == 1 &&
			   json.value(QStringLiteral("leftInPlace")).toObject().value(QStringLiteral("files")).toArray()[0].toObject().value(QStringLiteral("level")).toInt() == 2,
		   "a one-level set names the second level it leaves in place");
	// A hand-made level keeps working when another command saves the source
	// into another folder.
	const auto relative = QDir(root).filePath(QStringLiteral("rel/rel.mesh.json"));
	const auto low = QDir(root).filePath(QStringLiteral("rel/low.mesh.json"));
	QDir().mkpath(QFileInfo(relative).absolutePath());
	expect(document.setMesh(prop(), &error) && document.save(relative, true, &error) && document.save(low, true, &error), "save a source and its level");
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("lod"), relative, QStringLiteral("--levels"), QStringLiteral("1"), QStringLiteral("--level-source"),
				QStringLiteral("1=") + low, QStringLiteral("--save-recipe")},
			   &json) == 0 &&
			   QJsonDocument::fromJson(bytesOf(relative)).object().value(QStringLiteral("detailLevels")).toArray()[0].toObject().value(QStringLiteral("source")) ==
				   QStringLiteral("low.mesh.json"),
		   "--save-recipe stores the level relative to the source");
	const auto moved = QDir(root).filePath(QStringLiteral("rel/sub/rel.mesh.json"));
	QDir().mkpath(QFileInfo(moved).absolutePath());
	expect(run(executable, settings,
			   {QStringLiteral("model"), QStringLiteral("skin-variants"), relative, QStringLiteral("--operation"), QStringLiteral("add"), QStringLiteral("--name"),
				QStringLiteral("red"), QStringLiteral("--surface-material"), QStringLiteral("body=textures/red.tga"), QStringLiteral("--output"), moved},
			   &json) == 0 &&
			   run(executable, settings, {QStringLiteral("model"), QStringLiteral("lod"), moved, QStringLiteral("--output"), QDir(root).filePath(QStringLiteral("rel/sub/rel.md3"))},
				   &json) == 0 &&
			   json.value(QStringLiteral("committed")).toInt() == 2,
		   "a source saved elsewhere by another command still builds its hand-made level");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir directory;
	if (!expect(directory.isValid(), "a temporary folder"))
		return EXIT_FAILURE;
	checkRecipe(directory.path());
	checkLevels(directory.path());
	checkInspection(directory.path());
	checkStaging(directory.path());
	checkSiblings(directory.path());
	checkRelativeSources(directory.path());
	if (argc > 1)
		checkCli(QString::fromLocal8Bit(argv[1]), directory.path());
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "model detail level checks passed\n";
	return EXIT_SUCCESS;
}
