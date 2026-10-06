#include "core/texture_handoff.h"
#include "core/texture_project.h"
#include "core/level_dependencies.h"
#include "core/level_materials.h"
#include "core/model_design.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	std::cout << (value ? "PASS " : "FAIL ") << message << std::endl;
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}

bool makeMap(LevelMapDocument* map, const QString& engine, QString* error)
{
	return loadLevelMapBytes({QStringLiteral("handoff.map"), {}, engine}, "{\n\"classname\" \"worldspawn\"\n}\n", map, error) &&
		addLevelMapBoxBrush(map, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("old"), nullptr, error);
}

bool allPixels(const LevelPreviewAssets& assets, const QImage& pixels)
{
	if (assets.materials.isEmpty() || assets.problemCount() != 0) { return false; }
	for (const auto& material : assets.materials) {
		if (!material.ready() || material.image.convertToFormat(QImage::Format_ARGB32) != pixels.convertToFormat(QImage::Format_ARGB32)) { return false; }
	}
	return true;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	const QDir root(temporary.path());
	bool ok = true; QString error, reference;
	TextureDocument document;
	QImage detail(32, 32, QImage::Format_ARGB32); detail.fill(Qt::transparent); detail.setPixelColor(3, 7, Qt::green);
	ok &= expect(document.create({32, 32}, Qt::red, &error) && document.addLayer(QStringLiteral("Detail"), detail, &error), "author layered fixture", error);
	TextureProjectSaveRequest project; project.path = root.filePath(QStringLiteral("authored.vtexture"));
	const auto projectSave = writeTextureProject(document, project);
	TextureDocument reopened;
	ok &= expect(projectSave.succeeded && readTextureProject(project.path, &reopened, nullptr, nullptr, &error) && reopened.image() == document.image(), "lossless project feeds the same composite into handoff", error);
	TextureExportOptions options;
	const auto first = encodeTextureExport(reopened.image(), options);
	PackageStagingModel plan;
	ok &= expect(plan.createEmpty(PackageArchiveFormat::Pk3, {}, &error), "create target package", error);
	LevelMapDocument map;
	ok &= expect(makeMap(&map, QStringLiteral("idTech3"), &error), "create generated map and selection", error);
	const auto initialRevision = map.revision;
	const auto initialPackage = plan.revision();
	for (const QString& path : {QStringLiteral("models/authored.png"), QStringLiteral("authored.png"), QStringLiteral("textures/.png"),
		QStringLiteral("Textures/authored.png"), QStringLiteral("textures/authored.PNG"), QStringLiteral("textures/../authored.png")}) {
		ok &= expect(!stageAndApplyTextureExport(first, options, path, &plan, false, &map, &reference, &error) &&
			map.revision == initialRevision && plan.revision() == initialPackage && reference.isEmpty() && !error.isEmpty(),
			"invalid compiler path leaves both documents unchanged", error);
	}
	const QString path = QStringLiteral("textures/handoff/authored.png");
	const auto selection = map.selection; map.selection.clear();
	ok &= expect(!stageAndApplyTextureExport(first, options, path, &plan, false, &map, nullptr, &error) && plan.operations().isEmpty(),
		"missing selection rolls back prepared staging", error);
	map.selection = selection;
	ok &= expect(stageAndApplyTextureExport(first, options, path, &plan, false, &map, &reference, &error) && reference == QStringLiteral("handoff/authored"),
		"map uses the compiler token while package retains its complete path", error);
	const auto appliedRevision = map.revision;
	const auto appliedUndo = map.undoStack.size();
	const auto appliedPackage = plan.revision();
	ok &= expect(!stageAndApplyTextureExport(first, options, path, &plan, false, &map, nullptr, &error) && map.revision == appliedRevision && plan.revision() == appliedPackage,
		"unapproved replacement leaves map and package unchanged", error);
	ok &= expect(!applyLevelMapTexture(&map, reference, nullptr, &error), "standalone map apply still reports an unchanged selection");
	QImage changed = document.image(); changed.setPixelColor(9, 11, Qt::blue);
	const auto second = encodeTextureExport(changed, options);
	ok &= expect(stageAndApplyTextureExport(second, options, path, &plan, true, &map, nullptr, &error) && map.revision == appliedRevision && map.undoStack.size() == appliedUndo,
		"restaging changed pixels keeps the existing map reference and undo history", error);
	ok &= expect(allPixels(resolveLevelPreviewAssets(map, PackageStagingArchive(plan)), changed), "level preview sees restaged bytes");
	ok &= expect(plan.undo() && allPixels(resolveLevelPreviewAssets(map, PackageStagingArchive(plan)), document.image()) && plan.redo(),
		"package undo and redo invalidate the dependent material without map edits");
	// One authored asset is shared by a direct brush, a shader brush, and an MD3 prop.
	const QByteArray shader = "textures/handoff/material\n{\n qer_editorimage textures/handoff/authored.png\n { map textures/handoff/authored.png }\n}\n";
	ok &= expect(plan.addBytes(shader, QStringLiteral("scripts/handoff.shader"), &error) &&
		addLevelMapBoxBrush(&map, {96, 0, 0, true}, {160, 64, 64, true}, QStringLiteral("handoff/material"), nullptr, &error), "connect authored pixels through a shader", error);
	ModelDesign model; ModelDesignPart part; part.material = QStringLiteral("textures/handoff/material"); model.parts << part;
	ok &= expect(stageModelDesign(model, QStringLiteral("models/handoff/prop.md3"), &plan, &map, {0, 128, 0, true}, false, &error), "connect authored pixels to a placed model", error);
	const auto mesh = buildModelDesignMesh(model);
	const auto materials = resolveModelPreviewAssets(mesh, PackageStagingArchive(plan));
	ok &= expect(allPixels(materials, changed) && materials.materials.first().shaderPath == QStringLiteral("scripts/handoff.shader"),
		"modeller uses the same staged shader image as the level");
	ok &= expect(allPixels(resolveLevelPreviewAssets(map, PackageStagingArchive(plan)), changed), "direct brush, shader brush, and placed model share updated pixels");
	const auto report = inspectLevelDependencies(map, PackageStagingArchive(plan));
	ok &= expect(report.canExport() && report.problemCount == 0 && report.resolvedPaths == QStringList{QStringLiteral("models/handoff/prop.md3"), QStringLiteral("scripts/handoff.shader"), path},
		"dependency review traces the authored image through all consumers", levelDependencyReportText(report));
	const auto draft = root.filePath(QStringLiteral("handoff.vibepackage")); PackageStagingModel reopenedPlan;
	ok &= expect(PackageDraft::save(draft, &plan, false, &error), "save cross-editor handoff in a package draft", error);
	ok &= expect(PackageDraft::load(draft, &reopenedPlan, &error), "reopen cross-editor handoff from a package draft", error);
	PackageWriteRequest output; output.format = PackageArchiveFormat::Pk3; output.destinationPath = root.filePath(QStringLiteral("handoff.pk3"));
	output.verifyDeterminism = true;
	const auto written = reopenedPlan.writeArchive(output); PackageArchive saved;
	ok &= expect(written.succeeded() && written.determinismVerified && saved.load(output.destinationPath, &error), "publish deterministic handoff package", error);
	const auto mapPath = root.filePath(QStringLiteral("handoff.map")); LevelMapDocument savedMap;
	ok &= expect(saveLevelMapAs(map, mapPath).succeeded() && loadLevelMap({mapPath, {}, QStringLiteral("idTech3")}, &savedMap, &error), "persist map references separately", error);
	ok &= expect(inspectLevelDependencies(savedMap, saved).canExport() && allPixels(resolveLevelPreviewAssets(savedMap, saved), changed) &&
		allPixels(resolveModelPreviewAssets(mesh, saved), changed), "saved map, model, shader and package retain exact authored pixels");
	// Native placement retains palette indices; no WAD container is staged as a lump.
	IdTechPaletteResolution palette; palette.palette.id = QStringLiteral("test-grayscale");
	for (int i = 0; i < 256; ++i) { palette.palette.colors << qRgb(i, i, i); }
	QImage indexed(32, 32, QImage::Format_Indexed8); indexed.setColorTable(palette.palette.colors); indexed.fill(96);
	for (const bool wal : {false, true}) {
		PackageStagingModel native;
		ok &= expect(native.createEmpty(wal ? PackageArchiveFormat::Pak : PackageArchiveFormat::Wad, wal ? QString() : QStringLiteral("WAD2"), &error), "create native package", error);
		LevelMapDocument nativeMap;
		ok &= expect(makeMap(&nativeMap, QStringLiteral("idTech2"), &error), "create native map", error);
		TextureExportOptions nativeOptions; nativeOptions.format = wal ? TextureExportFormat::Quake2Wal : TextureExportFormat::QuakeMiptex;
		nativeOptions.name = wal ? QStringLiteral("handoff/native") : QStringLiteral("native");
		const auto encoded = encodeTextureExport(indexed, nativeOptions, palette);
		const QString target = wal ? QStringLiteral("textures/handoff/native.wal") : QStringLiteral("native");
		ok &= expect(stageAndApplyTextureExport(encoded, nativeOptions, target, &native, false, &nativeMap, &reference, &error) && reference == nativeOptions.name,
			"native package and map agree on the texture name", error);
		QByteArray nativeBytes;
		ok &= expect(PackageStagingArchive(native).readEntryBytes(target, &nativeBytes, &error), "read staged native bytes", error);
		const auto asset = decodeIdTechImage(wal ? target : QStringLiteral("native.mip"), nativeBytes, palette.palette);
		ok &= expect(asset.decoded && asset.image.pixelColor(1, 1) == QColor(96, 96, 96), "native handoff retains indexed pixels");
		const auto preview = resolveLevelPreviewAssets(nativeMap, PackageStagingArchive(native));
		ok &= expect(preview.readyCount() == 1 && preview.materials.first().sourceSize == QSize(32, 32),
			"native staged texture resolves in the level material preview", levelPreviewAssetsText(preview));
		ok &= expect(inspectLevelDependencies(nativeMap, PackageStagingArchive(native)).missingCount == 0,
			"native map reference resolves during dependency review");
		const auto revision = nativeMap.revision;
		ok &= expect(stageAndApplyTextureExport(encoded, nativeOptions, target, &native, true, &nativeMap, nullptr, &error) && nativeMap.revision == revision,
			"native restaging also avoids redundant map history", error);
		nativeOptions.format = TextureExportFormat::QuakeWad2;
		ok &= expect(!stageAndApplyTextureExport(encoded, nativeOptions, target, &native, true, &nativeMap, nullptr, &error), "WAD containers cannot masquerade as map texture lumps");
	}
	LevelMapDocument doom; doom.format = LevelMapFormat::DoomWad;
	ok &= expect(!stageAndApplyTextureExport(first, options, path, &plan, true, &doom, nullptr, &error), "Doom wall composition requires its explicit workflow");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
