#include "core/level_dependencies.h"
#include "core/level_model_appearance.h"
#include "core/map_preview_mesh.h"
#include "core/model_design.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
bool put(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	QDir root(temp.path());
	bool ok = true;
	QString error;
	ModelDesign design;
	design.name = QStringLiteral("test_prop");
	ModelDesignPart box;
	box.name = QStringLiteral("body");
	box.material = QStringLiteral("textures/props/paint");
	box.origin = {12, -20, 32};
	box.yaw = 37;
	design.parts << box;
	ModelDesignPart cylinder = box;
	cylinder.name = QStringLiteral("post");
	cylinder.primitive = QStringLiteral("cylinder");
	cylinder.size = {32, 48, 96};
	cylinder.origin = {0, 0, 100};
	cylinder.segments = 16;
	design.parts << cylinder;
	ModelDesignPart plane = box;
	plane.name = QStringLiteral("panel");
	plane.primitive = QStringLiteral("plane");
	plane.origin = {0, 0, 150};
	design.parts << plane;
	// Existing yaw-only files retain their geometry; all new saves use v2 so
	// older readers cannot silently discard full rotations or UV edits.
	auto legacyJson = modelDesignJson(design);
	legacyJson.insert(QStringLiteral("schemaVersion"), 1);
	QJsonArray legacyParts;
	for (const auto& value : legacyJson.value(QStringLiteral("parts")).toArray()) {
		auto part = value.toObject();
		for (const auto* key : {"roll", "pitch", "uvScale", "uvOffset", "uvRotation"}) {
			part.remove(QLatin1String(key));
		}
		legacyParts << part;
	}
	legacyJson.insert(QStringLiteral("parts"), legacyParts);
	ModelDesign legacy;
	ok &= expect(parseModelDesign(QJsonDocument(legacyJson).toJson(), &legacy, &error) &&
	                 exportModelDesign(legacy, QStringLiteral("md3")) == exportModelDesign(design, QStringLiteral("md3")),
	             "schema 1 designs load with identity UV transforms and zero pitch/roll");
	design.parts[0].roll = 23;
	design.parts[0].pitch = -31;
	design.parts[0].uvScale = {2, -3};
	design.parts[0].uvOffset = {0.25f, -0.5f};
	design.parts[0].uvRotation = 47;
	design.parts[1].pitch = 90;
	design.parts[1].uvScale = {3, 0.5f};
	design.parts[2].roll = 90;
	ModelDesign parsed;
	ok &= expect(parseModelDesign(QJsonDocument(modelDesignJson(design)).toJson(), &parsed, &error) &&
	                 modelDesignJson(parsed) == modelDesignJson(design),
	             "design JSON must round trip");
	const auto before = modelDesignJson(parsed);
	ok &= expect(before.value(QStringLiteral("schemaVersion")) == QJsonValue(2), "full transform sources require schema 2");
	ok &= expect(!parseModelDesign("{\"schemaVersion\":2}", &parsed, &error) && modelDesignJson(parsed) == before,
	             "invalid JSON must not change the current design");
	for (const auto* key : {"roll", "pitch", "uvScale", "uvOffset", "uvRotation"}) {
		auto malformed = before;
		auto parts = malformed.value(QStringLiteral("parts")).toArray();
		auto part = parts[0].toObject();
		part.insert(QLatin1String(key), QStringLiteral("invalid"));
		parts[0] = part;
		malformed.insert(QStringLiteral("parts"), parts);
		ok &= expect(!parseModelDesign(QJsonDocument(malformed).toJson(), &parsed, &error) && modelDesignJson(parsed) == before,
		             "malformed version 2 transforms must preserve the loaded document");
	}
	ModelDesign axes;
	ModelDesignPart axisPart;
	axisPart.size = {2, 4, 6};
	axisPart.origin = {10, 20, 30};
	axisPart.roll = axisPart.pitch = axisPart.yaw = 90;
	axisPart.uvScale = {2, -3};
	axisPart.uvOffset = {0.25f, -0.5f};
	axisPart.uvRotation = 90;
	axes.parts << axisPart;
	const auto axisMesh = buildModelDesignMesh(axes);
	const auto axisPosition = axisMesh.surfaces[0].frames[0].positions[0];
	const auto axisNormal = axisMesh.surfaces[0].frames[0].normals[0];
	const auto axisUv = axisMesh.surfaces[0].texCoords[0];
	ok &=
	    expect(std::abs(axisPosition.x - 7) < 0.0001 && std::abs(axisPosition.y - 18) < 0.0001 && std::abs(axisPosition.z - 29) < 0.0001 &&
	               std::abs(axisNormal.x) < 0.0001 && std::abs(axisNormal.y) < 0.0001 && std::abs(axisNormal.z + 1) < 0.0001,
	           "rotation order is X then Y then Z, around the scaled part centre, for positions and normals");
	ok &= expect(std::abs(axisUv.u - 3.25) < 0.0001 && std::abs(axisUv.v + 0.5) < 0.0001, "UVs scale, rotate around zero, then translate");
	ok &= expect(exportModelDesign(axes, QStringLiteral("obj")).contains("vt 3.250000 1.500000\n"),
	             "OBJ carries transformed UVs with the standard V-axis conversion");
	auto invalid = design;
	invalid.parts[0].size.x = std::numeric_limits<float>::infinity();
	ok &= expect(!validateModelDesign(invalid).isEmpty(), "reject infinite dimensions");
	for (double angle : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 36001.0}) {
		for (int field = 0; field < 3; ++field) {
			invalid = design;
			if (field == 0) {
				invalid.parts[0].roll = angle;
			}
			if (field == 1) {
				invalid.parts[0].pitch = angle;
			}
			if (field == 2) {
				invalid.parts[0].uvRotation = angle;
			}
			ok &= expect(!validateModelDesign(invalid).isEmpty(), "reject non-finite or out-of-range rotations");
		}
	}
	for (float scale : {0.0f, 0.001f, -65.0f, std::numeric_limits<float>::infinity()}) {
		invalid = design;
		invalid.parts[0].uvScale.u = scale;
		ok &= expect(!validateModelDesign(invalid).isEmpty(), "reject degenerate or out-of-range UV scale");
	}
	invalid = design;
	invalid.parts[0].uvOffset.v = 1025;
	ok &= expect(!validateModelDesign(invalid).isEmpty(), "reject out-of-range UV offsets");
	invalid = design;
	invalid.parts[1].name = QStringLiteral("BODY");
	ok &= expect(!validateModelDesign(invalid).isEmpty(), "names must be unique ignoring case");
	invalid = design;
	invalid.parts[0].material = QStringLiteral("../escape");
	ok &= expect(!validateModelDesign(invalid).isEmpty(), "material path safety");
	invalid = design;
	invalid.parts[0].origin.x = 1000;
	ok &= expect(exportModelDesign(invalid, QStringLiteral("md3"), &error).isEmpty(), "MD3 coordinates must not wrap");
	invalid = design;
	invalid.parts[1].size = {0.016f, 0.016f, 0.016f};
	ok &= expect(exportModelDesign(invalid, QStringLiteral("md3"), &error).isEmpty(), "reject collapsed MD3 triangles");
	const ModelMesh mesh = buildModelDesignMesh(design);
	const QByteArray md3 = exportModelDesign(design, QStringLiteral("md3"), &error);
	ok &= expect(!md3.isEmpty() && error.isEmpty() && md3 == exportModelDesign(design, QStringLiteral("md3")),
	             "MD3 output must be valid and deterministic");
	ok &= expect(md3.startsWith("IDP3") && qFromLittleEndian<qint32>(md3.constData() + 104) == md3.size(),
	             "MD3 header must describe complete file");
	const auto decoded = decodeModelMesh(QStringLiteral("models/props/test.md3"), md3);
	ok &= expect(decoded.geometryAvailable && decoded.error.isEmpty() && decoded.warnings.isEmpty() && decoded.surfaceCount == 3,
	             "new MD3 must decode without warnings");
	ok &= expect(decoded.triangleCount == 12 + 16 * 4 + 2 && decoded.skinPaths == QStringList{box.material},
	             "primitive topology and materials must survive export");
	for (int s = 0; s < decoded.surfaces.size(); ++s) {
		const auto& original = mesh.surfaces[s];
		const auto& surface = decoded.surfaces[s];
		for (int i = 0; i < surface.vertexCount; ++i) {
			const auto p = original.frames[0].positions[i], q = surface.frames[0].positions[i];
			const auto n = original.frames[0].normals[i], m = surface.frames[0].normals[i];
			ok &= expect(std::abs(original.texCoords[i].u - surface.texCoords[i].u) < 0.0001 &&
			                 std::abs(original.texCoords[i].v - surface.texCoords[i].v) < 0.0001,
			             "MD3 preserves transformed UVs including negative and tiled coordinates");
			ok &= expect(std::abs(p.x - q.x) <= 1.0 / 128 + 0.0001 && std::abs(p.y - q.y) <= 1.0 / 128 + 0.0001 &&
			                 std::abs(p.z - q.z) <= 1.0 / 128 + 0.0001,
			             "MD3 quantization error is bounded");
			ok &= expect(n.x * m.x + n.y * m.y + n.z * m.z > 0.999, "packed MD3 normals keep their direction");
		}
		for (const auto& t : original.triangles) {
			const auto a = original.frames[0].positions[t.a], b = original.frames[0].positions[t.b], c = original.frames[0].positions[t.c],
			           n = original.frames[0].normals[t.a];
			const double x = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
			             y = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
			             z = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
			ok &= expect(x * n.x + y * n.y + z * n.z > 0, "primitive winding must agree with outward normals");
		}
	}
	const auto obj = exportModelDesign(design, QStringLiteral("obj"));
	ok &= expect(obj.count("usemtl textures/props/paint") == 3 && obj.contains("g panel\n"), "OBJ keeps part/material assignments");
	const QString assets = root.filePath(QStringLiteral("assets"));
	ok &= expect(
	    put(QDir(assets).filePath(QStringLiteral("scripts/props.shader")), "textures/props/paint { { map textures/props/paint.tga } }\n") &&
	        put(QDir(assets).filePath(QStringLiteral("textures/props/paint.tga")), "generated-fixture"),
	    "write synthetic asset fixtures");
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(archive.load(assets, &error) && staging.loadBaseArchive(archive, &error), "load authoring package");
	const QString mapPath = root.filePath(QStringLiteral("source.map"));
	ok &= expect(put(mapPath, "{\n\"classname\" \"worldspawn\"\n}\n"), "write empty level fixture");
	LevelMapDocument map;
	ok &= expect(loadLevelMap({mapPath, {}, QStringLiteral("idTech3")}, &map, &error), "load Quake III map");
	const QString modelPath = QStringLiteral("models/props/test.md3");
	ok &= expect(stageModelDesign(design, modelPath, &staging, &map, {128, -64, 16, true}, false, &error),
	             "stage-and-place must work as one handoff");
	ok &= expect(map.entities.size() == 2 && map.undoStack.size() == 1 && staging.operations().size() == 1,
	             "handoff adds one map undo and one package operation");
	const auto stagedManifest = staging.manifestJson();
	ok &= expect(stagedManifest.contains("generated") && stagedManifest.contains("sha256") && !stagedManifest.contains(md3.toBase64()),
	             "manifest describes generated content without embedding the payload");
	const int entityId = map.entities.last().id;
	ok &= expect(undoLevelMapEdit(&map, &error) && map.entities.size() == 1 && redoLevelMapEdit(&map, &error),
	             "placement is undoable and redoable");
	const auto operations = staging.operations().size();
	const auto revision = map.revision;
	ok &= expect(!stageModelDesign(design, modelPath, &staging, &map, {}, false, &error) && staging.operations().size() == operations &&
	                 map.revision == revision,
	             "path conflicts leave both modules unchanged");
	LevelMapDocument wrongMap;
	wrongMap.format = LevelMapFormat::DoomWad;
	ok &= expect(!stageModelDesign(design, QStringLiteral("models/wrong.md3"), &staging, &wrongMap, {}, false, &error) &&
	                 staging.operations().size() == operations,
	             "unsupported map leaves package untouched");
	PackageStagingArchive snapshot(staging);
	QByteArray savedBytes;
	ok &= expect(snapshot.readEntryBytes(modelPath, &savedBytes, &error) && savedBytes == md3, "snapshot must read generated model bytes");
	QByteArray prefix;
	ok &= expect(snapshot.readEntryBytes(modelPath, &prefix, &error, 4) && prefix == "IDP3", "snapshot respects bounded reads");
	const auto dependencies = inspectLevelDependencies(map, snapshot);
	ok &= expect(dependencies.canExport() && dependencies.resolvedPaths.size() == 3,
	             "level audit must include model, shader, and shader image");
	for (const auto& dependency : dependencies.dependencies) {
		ok &= expect(dependency.selectors.contains(QStringLiteral("entity:%1").arg(entityId)),
		             "transitive dependencies retain their map owner");
	}
	auto changed = design;
	changed.parts[0].size.x = 80;
	ok &= expect(stageModelDesign(changed, modelPath, &staging, nullptr, {}, true, &error), "explicit replacement should stage");
	ok &= expect(snapshot.readEntryBytes(modelPath, &savedBytes, &error) && savedBytes == md3,
	             "later edits cannot change a snapshot's generated bytes");
	PackageStagingModel subset;
	ok &= expect(subset.loadBaseArchiveSubset(snapshot, dependencies.resolvedPaths, &error), "subset must consume the plan snapshot");
	PackageWriteRequest write;
	write.destinationPath = root.filePath(QStringLiteral("export.pk3"));
	write.verifyDeterminism = true;
	const auto report = subset.writeArchive(write);
	ok &= expect(report.succeeded() && report.determinismVerified, "write deterministic dependency subset");
	PackageArchive exported;
	ok &=
	    expect(exported.load(write.destinationPath, &error) && exported.readEntryBytes(modelPath, &savedBytes, &error) && savedBytes == md3,
	           "exported model must match snapshot");
	ok &= expect(inspectLevelDependencies(map, exported).canExport(), "reopened output must contain the complete dependency closure");
	// A plan reader preserves generated payloads through rename/delete and
	// protects original import paths even after a subset flattens the plan.
	PackageStagingModel renamed;
	ok &= expect(renamed.loadBaseArchive(snapshot, &error) && renamed.renameEntry(modelPath, QStringLiteral("models/renamed.md3"), &error),
	             "rename generated snapshot entry");
	PackageStagingArchive renamedSnapshot(renamed);
	ok &= expect(!renamedSnapshot.readEntryBytes(modelPath, &savedBytes, &error) &&
	                 renamedSnapshot.readEntryBytes(QStringLiteral("models/renamed.md3"), &savedBytes, &error) && savedBytes == md3,
	             "renaming must retain payload and remove the previous path");
	ok &= expect(renamed.deleteEntry(QStringLiteral("models/renamed.md3"), &error), "delete generated entry");
	PackageStagingArchive deletedSnapshot(renamed);
	ok &= expect(!deletedSnapshot.readEntryBytes(QStringLiteral("models/renamed.md3"), &savedBytes, &error) &&
	                 renamedSnapshot.readEntryBytes(QStringLiteral("models/renamed.md3"), &savedBytes, &error),
	             "deleting cannot change an earlier snapshot");
	const QString importedPath = root.filePath(QStringLiteral("imported.bin"));
	ok &= expect(put(importedPath, "import-source") && renamed.addFile(importedPath, QStringLiteral("data/imported.bin"), &error),
	             "stage an external file");
	PackageStagingArchive importedSnapshot(renamed);
	PackageStagingModel importedSubset;
	ok &= expect(importedSubset.loadBaseArchiveSubset(importedSnapshot, {QStringLiteral("data/imported.bin")}, &error),
	             "flatten imported subset");
	PackageWriteRequest overwriteSource;
	overwriteSource.destinationPath = importedPath;
	overwriteSource.format = PackageArchiveFormat::Zip;
	overwriteSource.allowOverwrite = true;
	ok &= expect(!importedSubset.writeArchive(overwriteSource).succeeded() && read(importedPath) == "import-source",
	             "subset export must protect imported source files");
	// Relative materials belong to each model, even when their names match.
	ModelDesign relativeDesign = design;
	for (auto& part : relativeDesign.parts) {
		part.material = QStringLiteral("skin.tga");
	}
	PackageStagingModel relativePlan;
	LevelMapDocument relativeMap;
	ok &= expect(relativePlan.loadBaseArchive(archive, &error) &&
	                 loadLevelMap({mapPath, {}, QStringLiteral("idTech3")}, &relativeMap, &error),
	             "prepare relative material fixtures");
	for (const QString& directory : {QStringLiteral("models/a"), QStringLiteral("models/b")}) {
		ok &= expect(
		    relativePlan.addBytes("generated-skin", directory + QStringLiteral("/skin.tga"), &error) &&
		        stageModelDesign(relativeDesign, directory + QStringLiteral("/prop.md3"), &relativePlan, &relativeMap, {}, false, &error),
		    "stage models with independent relative skins");
	}
	const auto relativeDependencies = inspectLevelDependencies(relativeMap, PackageStagingArchive(relativePlan));
	ok &= expect(relativeDependencies.canExport() && relativeDependencies.resolvedPaths.size() == 4 &&
	                 relativeDependencies.resolvedPaths.contains(QStringLiteral("models/a/skin.tga")) &&
	                 relativeDependencies.resolvedPaths.contains(QStringLiteral("models/b/skin.tga")),
	             "same-named relative skins must both enter the dependency closure");
	for (const auto& dependency : relativeDependencies.dependencies) {
		if (dependency.kind == QStringLiteral("model-material")) {
			ok &= expect(dependency.selectors.size() == 1 && dependency.requiredBy.size() == 1,
			             "relative materials keep their own model's attribution");
		}
	}
	QByteArray damagedMd3 = md3;
	qToLittleEndian<qint32>(100000, damagedMd3.data() + 164 + 176);
	PackageStagingModel damagedPlan;
	ok &= expect(damagedPlan.loadBaseArchive(snapshot, &error) &&
	                 damagedPlan.addBytes(damagedMd3, modelPath, &error, PackageStageConflictResolution::ReplaceExisting),
	             "stage partial model fixture");
	ok &= expect(!inspectLevelDependencies(map, PackageStagingArchive(damagedPlan)).canExport(),
	             "a partial surface must block complete dependency claims");
	const auto cancelled = inspectLevelDependencies(map, snapshot, [](int, int) { return false; });
	ok &= expect(cancelled.cancelled && !cancelled.canExport(), "dependency cancellation prevents export");
	LevelMapPreviewMeshOptions previewOptions;
	previewOptions.modelMeshes.insert(levelModelAppearance(map, map.entities.last()).cacheKey, decoded);
	const auto preview = buildLevelMapPreviewMesh(map, previewOptions);
	ok &= expect(preview.modelInstances == 1 && preview.triangles == decoded.triangleCount &&
	                 preview.owners.first().kind == LevelMapSelectionKind::Entity && preview.owners.first().objectId == entityId,
	             "placed prop must be visible and selectable in map preview");
	ok &= expect(std::abs(preview.mesh.mins.x - decoded.mins.x - 128) < 0.01 && std::abs(preview.mesh.mins.y - decoded.mins.y + 64) < 0.01,
	             "level preview applies entity placement");
	LevelMapDocument transformed = map;
	transformed.entities.last().properties << LevelMapProperty{QStringLiteral("angle"), QStringLiteral("90"), 0}
	                                       << LevelMapProperty{QStringLiteral("modelscale_vec"), QStringLiteral("2 3 1"), 0};
	const auto transformedPreview = buildLevelMapPreviewMesh(transformed, previewOptions);
	ok &= expect(std::abs(transformedPreview.mesh.mins.x - (128 - 3 * decoded.maxs.y)) < 0.01 &&
	                 std::abs(transformedPreview.mesh.mins.y - (-64 + 2 * decoded.mins.x)) < 0.01,
	             "model preview rotates nonuniform scale around the entity origin");
	previewOptions.triangleLimit = 4;
	ok &= expect(buildLevelMapPreviewMesh(map, previewOptions).truncated, "model instances share the map triangle budget");
	const QString designPath = root.filePath(QStringLiteral("prop.model.json"));
	ok &= expect(saveModelDesignBytes(designPath, QJsonDocument(modelDesignJson(design)).toJson(), false, &error), "atomic design save");
	ok &= expect(!saveModelDesignBytes(designPath, md3, true, &error, designPath) && read(designPath).startsWith("{"),
	             "model export must protect design source");
	ok &= expect(!saveModelDesignBytes(designPath, md3, false, &error), "overwrite requires explicit permission");
	ok &= expect(!saveModelDesignBytes(assets, md3, true, &error), "model output cannot be a directory");
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto cli = [&](QStringList arguments, int expected) {
			QProcess process;
			process.setProgram(binary);
			process.setArguments(QStringList{QStringLiteral("--cli"), QStringLiteral("--json")} + arguments);
			process.start();
			const bool finished = process.waitForFinished(30000);
			const auto output = process.readAllStandardOutput();
			if (!finished || process.exitCode() != expected) {
				std::cerr << output.constData() << process.readAllStandardError().constData();
			}
			return expect(finished && process.exitCode() == expected && QJsonDocument::fromJson(output).isObject(),
			              "CLI contract must return expected exit status and JSON");
		};
		const QString built = root.filePath(QStringLiteral("dryrun/new.md3"));
		ok &= cli(
		    {QStringLiteral("model"), QStringLiteral("build"), designPath, QStringLiteral("--output"), built, QStringLiteral("--dry-run")},
		    0);
		ok &= expect(!QFileInfo::exists(QFileInfo(built).absolutePath()), "CLI dry run must create no output directories");
		ok &= cli({QStringLiteral("model"), QStringLiteral("build"), designPath, QStringLiteral("--output"), built}, 0);
		ok &= expect(read(built) == md3, "CLI and GUI core produce identical MD3 bytes");
		const QString builtObj = root.filePath(QStringLiteral("transformed.obj"));
		ok &= cli({QStringLiteral("model"), QStringLiteral("build"), designPath, QStringLiteral("--output"), builtObj}, 0);
		ok &= expect(read(builtObj) == obj, "CLI and GUI core produce identical transformed OBJ bytes");
		const QString legacyPath = root.filePath(QStringLiteral("legacy.model.json"));
		const QString legacyOutput = root.filePath(QStringLiteral("legacy.md3"));
		ok &= expect(put(legacyPath, QJsonDocument(legacyJson).toJson()), "write schema 1 CLI fixture");
		ok &= cli({QStringLiteral("model"), QStringLiteral("build"), legacyPath, QStringLiteral("--output"), legacyOutput}, 0);
		ok &= expect(read(legacyOutput) == exportModelDesign(legacy, QStringLiteral("md3")), "CLI preserves legacy source semantics");
		ok &= cli({QStringLiteral("model"), QStringLiteral("build"), designPath, QStringLiteral("--output"), designPath,
		           QStringLiteral("--format"), QStringLiteral("md3"), QStringLiteral("--overwrite")},
		          1);
		ok &= cli({QStringLiteral("model"), QStringLiteral("build"), designPath, QStringLiteral("--output"), assets,
		           QStringLiteral("--format"), QStringLiteral("md3"), QStringLiteral("--overwrite"), QStringLiteral("--dry-run")},
		          1);
		const QString placed = root.filePath(QStringLiteral("placed.map"));
		ok &= cli({QStringLiteral("map"), QStringLiteral("place-model"), mapPath, QStringLiteral("--engine"), QStringLiteral("idTech3"),
		           QStringLiteral("--package"), write.destinationPath, QStringLiteral("--entry"), modelPath, QStringLiteral("--origin"),
		           QStringLiteral("16,32,48"), QStringLiteral("--output"), placed},
		          0);
		LevelMapDocument placedMap;
		ok &= expect(loadLevelMap({placed, {}, QStringLiteral("idTech3")}, &placedMap, &error) && placedMap.entities.size() == 2 &&
		                 placedMap.entities.last().origin.z == 48,
		             "CLI placement must round trip through map save/load");
		const QString damagedRoot = root.filePath(QStringLiteral("damaged"));
		ok &= expect(put(QDir(damagedRoot).filePath(modelPath), damagedMd3), "write damaged model fixture");
		ok &= cli({QStringLiteral("map"), QStringLiteral("place-model"), mapPath, QStringLiteral("--engine"), QStringLiteral("idTech3"),
		           QStringLiteral("--package"), damagedRoot, QStringLiteral("--entry"), modelPath, QStringLiteral("--origin"),
		           QStringLiteral("0,0,0"), QStringLiteral("--output"), placed, QStringLiteral("--overwrite")},
		          4);
		ok &= expect(loadLevelMap({placed, {}, QStringLiteral("idTech3")}, &placedMap, &error) && placedMap.entities.last().origin.z == 48,
		             "rejected placement must preserve the previous output");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
