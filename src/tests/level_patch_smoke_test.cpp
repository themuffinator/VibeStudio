#include "core/level_dependencies.h"
#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/map_geometry.h"
#include "core/map_preview_mesh.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool near(double a, double b)
{
	return std::abs(a - b) < 1e-9;
}
bool pointNear(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}
bool put(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray get(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	const QDir root(temp.path());
	bool ok = true;
	QString error;
	LevelPatchCreateRequest request;
	request.columns = 5;
	request.rows = 3;
	request.center = {32, -16, 64, true};
	LevelMapPatch patch;
	for (const QString& shape : {QStringLiteral("plane"), QStringLiteral("cylinder"), QStringLiteral("cone")}) {
		request.shape = shape;
		ok &= expect(createLevelPatch(request, &patch, &error), "preset creates valid geometry", error);
		LevelMapDocument map;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake3");
		create.starterRoom = false;
		ok &= expect(createLevelMap(create, &map, &error), "create target map", error);
		int id = -1;
		ok &= expect(addLevelMapPatch(&map, patch, &id, &error), "add patch to worldspawn", error);
		ok &= expect(id == 0 && map.selectionKind == LevelMapSelectionKind::QuakePatch && map.textureReferences.contains(patch.textureName),
		             "creation updates selection and texture references");
		const auto serialized = serializeLevelMap(map);
		LevelMapDocument loaded;
		ok &= expect(serialized.succeeded() && loadLevelMapBytes({QStringLiteral("patch.map"), {}, {}}, serialized.bytes, &loaded, &error),
		             "new patch reloads", error);
		ok &= expect(loaded.patches.size() == 1 && loaded.patches.first().width == patch.width &&
		                 loaded.patches.first().height == patch.height,
		             "dimensions survive reload");
		if (loaded.patches.size() == 1) {
			for (int i = 0; i < patch.controlPoints.size(); ++i) {
				ok &= expect(pointNear(loaded.patches.first().controlPoints.at(i), patch.controlPoints.at(i)) &&
				                 near(loaded.patches.first().controlU.at(i), patch.controlU.at(i)),
				             "every point and UV retains row-major identity");
			}
		}
		ok &= expect(undoLevelMapEdit(&map, &error) && map.patches.isEmpty() && map.textureReferences.isEmpty(),
		             "creation undo removes geometry and stale dependency references", error);
		ok &= expect(redoLevelMapEdit(&map, &error) && map.patches.size() == 1, "creation redo restores patch", error);
		if (shape != QStringLiteral("plane")) {
			ok &= expect(patch.width == 9 && pointNear(patch.controlPoints.first(), patch.controlPoints.at(8)),
			             "curved preset seam is closed geometrically");
			if (shape == QStringLiteral("cone")) {
				ok &= expect(pointNear(patch.controlPoints.at(18), patch.controlPoints.last()), "cone tip closes");
			}
		}
	}
	request.shape = QStringLiteral("plane");
	createLevelPatch(request, &patch, &error);
	patch.controlPoints[7].z += 80;
	patch.controlU[7] = 2.75;
	const auto oldGrid = tessellatePatchMesh(patch, 8);
	const auto oldUv = tessellatePatchTexCoords(patch, 8);
	const auto original = patch;
	ok &= expect(subdivideLevelPatch(&patch, true, &error), "split columns", error);
	const auto newGrid = tessellatePatchMesh(patch, 4);
	const auto newUv = tessellatePatchTexCoords(patch, 4);
	ok &= expect(patch.width == 9 && patch.height == 3, "non-square split has correct topology");
	for (int row = 0; row < newGrid.size(); ++row) {
		for (int col = 0; col < newGrid.at(row).size(); ++col) {
			ok &= expect(pointNear(newGrid.at(row).at(col), oldGrid.at(row * 2).at(col)) &&
			                 near(newUv.at(row).at(col).x(), oldUv.at(row * 2).at(col).x()),
			             "Bézier split preserves the independently sampled surface and UV function");
		}
	}
	ok &= expect(invertLevelPatch(&patch, &error) && invertLevelPatch(&patch, &error), "invert twice", error);
	ok &= expect(pointNear(patch.controlPoints.first(), original.controlPoints.first()), "double inversion retains position");
	const auto beforeInvalid = levelPatchDefinition(patch);
	ok &= expect(!moveLevelPatchPoints(&patch, {0, 9999}, {1, 2, 3, true}, 0, &error) && levelPatchDefinition(patch) == beforeInvalid,
	             "bad point rejects entire multi-point edit");
	ok &= expect(!moveLevelPatchPoints(&patch, {0}, {std::numeric_limits<double>::infinity(), 0, 0, true}, 0, &error),
	             "infinite movement rejected");
	ok &= expect(moveLevelPatchPoints(&patch, {0, 0}, {5, 0, 0, true}, 0, &error) &&
	                 near(patch.controlPoints.first().x, original.controlPoints.first().x + 5),
	             "duplicate point IDs move once");
	ok &= expect(moveLevelPatchPoints(&patch, {0}, {0, 0, 0, true}, 8, &error) &&
	                 near(patch.controlPoints.first().x, snapLevelMapCoordinate(patch.controlPoints.first().x, 8)),
	             "point snap uses shared grid service");
	request.columns = 4;
	ok &= expect(!createLevelPatch(request, &patch, &error), "even control grids rejected");
	request.columns = 31;
	request.rows = 31;
	ok &= expect(createLevelPatch(request, &patch, &error) && !subdivideLevelPatch(&patch, true, &error),
	             "maximum grid cannot overflow format limit");
	request.columns = 5;
	request.rows = 3;
	createLevelPatch(request, &patch, &error);
	patch.fixedSubdivisions = true;
	patch.subdivisionsX = 5;
	patch.subdivisionsY = 2;
	ok &= expect(tessellatePatchMesh(patch, 1).size() == 3 && tessellatePatchMesh(patch, 1).first().size() == 11,
	             "fixed patchDef3 counts drive preview axes independently");
	LevelMapDocument fixed;
	LevelMapCreateRequest fixedRequest;
	fixedRequest.game = QStringLiteral("quake3");
	fixedRequest.starterRoom = false;
	createLevelMap(fixedRequest, &fixed, &error);
	ok &= expect(addLevelMapPatch(&fixed, patch, nullptr, &error), "create fixed-subdivision patch", error);
	LevelMapDocument fixedReload;
	ok &= expect(loadLevelMapBytes({QStringLiteral("fixed.map"), {}, {}}, serializeLevelMap(fixed).bytes, &fixedReload, &error) &&
	                 fixedReload.patches.first().fixedSubdivisions && fixedReload.patches.first().subdivisionsX == 5 &&
	                 fixedReload.patches.first().subdivisionsY == 2,
	             "patchDef3 header survives save and reload", error);
	auto fixedEdit = fixedReload.patches.first();
	fixedEdit.controlPoints[7].z += 12;
	ok &= expect(replaceLevelMapPatch(&fixedReload, 0, fixedEdit, &error) && serializeLevelMap(fixedReload).bytes.contains("patchDef3"),
	             "component edit retains patch dialect", error);
	patch.fixedSubdivisions = false;
	auto definition = levelPatchDefinition(patch).join(QLatin1Char('\n'));
	definition.replace(QStringLiteral("( 5 3 0 0 0 )"), QStringLiteral("( 5 3 7 8 9 )"));
	definition.replace(QStringLiteral("patchDef2\n{"), QStringLiteral("patchDef2\n{\n// keep author note\n/* keep block\n   comment */"));
	definition.replace(QStringLiteral("( ("), QStringLiteral("(\n ("));
	definition.replace(QStringLiteral(") )"), QStringLiteral(")\n )"));
	const auto source = (QStringLiteral("{\n\"classname\" \"worldspawn\"\n") + definition +
	                     QStringLiteral("\n}\n{\n\"classname\" \"light\"\n\"note\" \"unchanged\"\n}\n"))
	                        .replace(QLatin1Char('\n'), QStringLiteral("\r\n"))
	                        .toUtf8();
	LevelMapDocument map;
	ok &= expect(loadLevelMapBytes({QStringLiteral("comments.map"), {}, {}}, source, &map, &error) && map.patches.size() == 1,
	             "multiline patch fixture loads", error);
	if (map.patches.isEmpty()) {
		return EXIT_FAILURE;
	}
	const auto baseline = serializeLevelMap(map).bytes;
	auto changed = map.patches.first();
	changed.controlPoints[7].z += 48;
	changed.controlU[7] = 2.5;
	changed.textureName = QStringLiteral("studio/curve");
	ok &= expect(replaceLevelMapPatch(&map, 0, changed, &error), "patch component edit applies", error);
	ok &= expect(map.undoStack.size() == 1 && map.revision == 1 && map.textureReferences.contains(changed.textureName),
	             "component edit is one undo revision and updates material dependency");
	ok &= expect(levelPatchMaterialToken(QStringLiteral("textures/studio/curve"), false) == QStringLiteral("studio/curve") &&
	                 levelPatchMaterialToken(QStringLiteral("textures/studio/curve"), true) == QStringLiteral("textures/studio/curve"),
	             "Quake III shader tokens avoid a doubled textures prefix while patchDef3 keeps material paths");
	const auto edited = serializeLevelMap(map).bytes;
	LevelMapDocument reopened;
	ok &= expect(loadLevelMapBytes({QStringLiteral("edited.map"), {}, {}}, edited, &reopened, &error) && reopened.patches.size() == 1,
	             "authored multiline patch reloads", error);
	ok &= expect(edited.contains("keep author note") && edited.contains("keep block") && edited.contains("\"note\" \"unchanged\"") &&
	                 reopened.patches.first().headerTail == QVector<double>({7, 8, 9}),
	             "comments, header metadata and unrelated entity survive");
	ok &= expect(near(reopened.patches.first().controlPoints.at(7).z, changed.controlPoints.at(7).z) &&
	                 near(reopened.patches.first().controlU.at(7), 2.5),
	             "edited position and UV persisted");
	ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == baseline, "undo restores exact original source bytes",
	             error);
	ok &= expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == edited, "redo restores edited source", error);
	const auto depth = map.undoStack.size();
	ok &= expect(replaceLevelMapPatch(&map, 0, map.patches.first(), &error) && map.undoStack.size() == depth,
	             "unchanged apply does not dirty history");
	ok &= expect(subdivideLevelPatch(&changed, false, &error) && replaceLevelMapPatch(&map, 0, changed, &error),
	             "topology change uses same document service", error);
	ok &= expect(duplicateLevelMapObjects(&map, {{LevelMapSelectionKind::QuakePatch, 0}}, 16, 0, 0, &error) && map.patches.size() == 2,
	             "authored patch can be duplicated", error);
	ok &= expect(loadLevelMapBytes({QStringLiteral("duplicate.map"), {}, {}}, serializeLevelMap(map).bytes, &reopened, &error) &&
	                 reopened.patches.size() == 2 && reopened.patches.at(1).height == 5,
	             "duplicate persists edited dimensions", error);
	ok &= expect(deleteLevelMapObjects(&map, {{LevelMapSelectionKind::QuakePatch, 0}}, &error) && undoLevelMapEdit(&map, &error) &&
	                 map.patches.size() == 2,
	             "delete and undo preserve authored patch", error);
	const auto recovery =
	    writeLevelMapRecovery(map, root.filePath(QStringLiteral("recovery")), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ok &= expect(!recovery.isEmpty() && restoreLevelMapRecovery(recovery, &reopened, &error) && reopened.patches.size() == 2 &&
	                 reopened.patches.first().height == 5,
	             "recovery retains changed topology", error);
	const auto preview = buildLevelMapPreviewMesh(map);
	LevelMapDocument copied = map;
	setLevelMapSelection(&copied, {{LevelMapSelectionKind::QuakePatch, 0}});
	const auto clipboard = levelMapSelectionText(copied, &error);
	ok &= expect(!clipboard.isEmpty() && pasteLevelMapText(&copied, clipboard, &error) && copied.patches.size() == 3 &&
	                 copied.patches.last().height == 5,
	             "copy/paste preserves authored patch topology and comments", error);
	auto invalid = copied;
	invalid.patches[0].controlPoints[0].x = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!serializeLevelMap(invalid).succeeded(), "invalid authored grid cannot silently serialize or disappear");
	bool hasUv = false;
	for (const auto& surface : preview.mesh.surfaces) {
		for (const auto& uv : surface.texCoords) {
			hasUv |= uv.u != 0 || uv.v != 0;
		}
	}
	ok &= expect(preview.patches == 2 && hasUv, "shared level preview carries tessellated UV coordinates");
	const QString assets = root.filePath(QStringLiteral("assets"));
	put(QDir(assets).filePath(QStringLiteral("textures/studio/curve.tga")), QByteArray("fixture"));
	PackageArchive archive;
	archive.load(assets, &error);
	const auto dependencies = inspectLevelDependencies(map, archive);
	bool resolved = false;
	for (const auto& dependency : dependencies.dependencies) {
		resolved |= dependency.reference == changed.textureName && dependency.status == LevelDependencyStatus::Resolved &&
		            dependency.selectors.contains(QStringLiteral("patch:0"));
	}
	ok &= expect(resolved, "package dependency report resolves authored patch shader and selector");
	put(QDir(assets).filePath(QStringLiteral("scripts/curves.shader")),
	    "textures/studio/curve\n{\n{\nmap textures/studio/curve.tga\n}\n}\n");
	archive.load(assets, &error);
	const auto shaderDependencies = inspectLevelDependencies(map, archive);
	ok &= expect(shaderDependencies.canExport() && shaderDependencies.resolvedPaths.contains(QStringLiteral("scripts/curves.shader")) &&
	                 shaderDependencies.resolvedPaths.contains(QStringLiteral("textures/studio/curve.tga")),
	             "Compiler map tokens resolve shader declarations and their transitive image dependencies");
	LevelMapDocument quake;
	LevelMapCreateRequest quakeRequest;
	quakeRequest.game = QStringLiteral("quake");
	createLevelMap(quakeRequest, &quake, &error);
	ok &= expect(!addLevelMapPatch(&quake, patch, nullptr, &error), "Quake maps are not silently converted to another engine");
	if (argc > 1) {
		const QString input = root.filePath(QStringLiteral("source.map")), output = root.filePath(QStringLiteral("cli.map"));
		put(input, baseline);
		const auto cli = [&](QStringList args, int expected) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]),
			              QStringList{QStringLiteral("--cli"), QStringLiteral("map")} + args + QStringList{QStringLiteral("--json")});
			if (!process.waitForFinished(30000) || process.exitCode() != expected) {
				ok &= expect(false, "CLI patch operation", QString::fromUtf8(process.readAllStandardError()));
				return QJsonObject();
			}
			return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		};
		cli({QStringLiteral("edit-patch"), input, QStringLiteral("--patch"), QStringLiteral("0"), QStringLiteral("--point"),
		     QStringLiteral("1,2"), QStringLiteral("--delta"), QStringLiteral("0,0,48"), QStringLiteral("--uv"), QStringLiteral("2.5,0.5"),
		     QStringLiteral("--texture"), changed.textureName, QStringLiteral("--output"), output, QStringLiteral("--dry-run")},
		    0);
		ok &= expect(!QFileInfo::exists(output), "CLI dry run creates no map");
		cli({QStringLiteral("edit-patch"), input, QStringLiteral("--patch"), QStringLiteral("0"), QStringLiteral("--point"),
		     QStringLiteral("1,2"), QStringLiteral("--delta"), QStringLiteral("0,0,48"), QStringLiteral("--uv"), QStringLiteral("2.5,0.5"),
		     QStringLiteral("--texture"), changed.textureName, QStringLiteral("--output"), output},
		    0);
		ok &= expect(get(output) == edited, "CLI and GUI core patch edits produce identical bytes");
		cli({QStringLiteral("edit-patch"), input, QStringLiteral("--patch"), QStringLiteral("0"), QStringLiteral("--point"),
		     QStringLiteral("99,2"), QStringLiteral("--delta"), QStringLiteral("0,0,48"), QStringLiteral("--output"), output,
		     QStringLiteral("--overwrite")},
		    2);
		ok &= expect(get(output) == edited, "invalid CLI edit preserves destination");
		cli({QStringLiteral("add-patch"), input, QStringLiteral("--shape"), QStringLiteral("cylinder"), QStringLiteral("--texture"),
		     QStringLiteral("textures/studio/curve"), QStringLiteral("--output"), output, QStringLiteral("--overwrite")},
		    0);
		ok &= expect(loadLevelMap({output, {}, {}}, &reopened, &error) && reopened.patches.size() == 2 &&
		                 reopened.patches.at(1).width == 9 && reopened.patches.at(1).textureName == QStringLiteral("studio/curve"),
		             "CLI creates and persists curved primitive", error);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
