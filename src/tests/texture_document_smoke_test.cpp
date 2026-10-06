#include "core/texture_document.h"
#include "core/texture_project.h"
#include "core/texture_output.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	const QDir root(temporary.path());
	bool ok = true; QString error;
	TextureDocument document;
	ok &= expect(document.create({8, 8}, Qt::transparent, &error) && document.isDirty(), "new images need saving");
	document.markSaved();
	ok &= expect(!validTextureSize({4096, 4096}) && !validTextureSize({0, 4}) && validTextureSize({2048, 2048}), "allocation bounds must reject oversized textures");
	ok &= expect(document.paintStroke({{0, 0}, {7, 7}}, Qt::red, 1, &error), "paint one continuous diagonal stroke");
	for (int i = 0; i < 8; ++i) { ok &= expect(document.image().pixelColor(i, i) == QColor(Qt::red), "a fast stroke must leave no missing pixels"); }
	ok &= expect(document.isDirty() && document.undo() && !document.isDirty() && document.redo() && document.isDirty(), "stroke is one undo unit, and save point tracks undo and redo");
	document.markSaved();
	ok &= expect(document.beginStroke({0, 7}, Qt::blue, 3) && document.continueStroke({7, 0}), "interactive stroke can span several updates");
	document.cancelStroke();
	ok &= expect(!document.isDirty() && document.image().pixelColor(0, 7).alpha() == 0, "cancel restores all pixels and save state");
	const QImage before = document.image();
	ok &= expect(!document.paintStroke({{0, 0}, {8, 0}}, Qt::blue, 1, &error) && document.image() == before, "invalid stroke is atomic");
	ok &= expect(!document.crop({-1, 0, 4, 4}, &error) && document.image() == before, "out-of-bounds crop does not pad or mutate");
	ok &= expect(document.floodFill({7, 0}, QColor(10, 20, 30, 40), &error), "fill must support partial alpha");
	ok &= expect(document.image().pixelColor(7, 0) == QColor(10, 20, 30, 40) && document.image().pixelColor(0, 7).alpha() == 0 && document.image().pixelColor(1, 1) == QColor(Qt::red), "fill must stay in its four-connected region");
	ok &= expect(document.paintStroke({{1, 1}}, Qt::transparent, 1) && document.image().pixelColor(1, 1).alpha() == 0, "eraser replaces alpha instead of blending a transparent color");
	ok &= expect(document.crop({0, 0, 4, 2}) && document.image().size() == QSize(4, 2), "crop uses exact texel bounds");
	const QImage crop = document.image();
	ok &= expect(document.resize({8, 4}, false) && document.image().pixelColor(6, 0) == crop.pixelColor(3, 0), "nearest resize preserves source texels");
	ok &= expect(document.rotateClockwise() && document.image().size() == QSize(4, 8), "quarter-turn swaps dimensions");
	ok &= expect(document.flip(true) && document.flip(false), "both mirror directions work");
	const auto palette = generatedIdTechPalette(QStringLiteral("quake"));
	ok &= expect(document.remapPalette(palette, false) && document.image().format() == QImage::Format_Indexed8, "remap produces an indexed PNG surface");
	const QString png = root.filePath(QStringLiteral("edited.png"));
	ok &= expect(document.savePng(png, false, true, &error) && !QFileInfo::exists(png), "dry run must not create output");
	ok &= expect(document.savePng(png, false, false, &error) && QImage(png).size() == document.image().size(), "saved PNG can be decoded");
	const auto saved = read(png);
	ok &= expect(!document.savePng(png, false, false, &error) && read(png) == saved, "overwrite protection preserves existing bytes");
	ok &= expect(!document.savePng(root.filePath(QStringLiteral("wrong.wal")), false, false, &error), "PNG cannot masquerade as a native texture");
	TextureOutputTarget exportTarget;
	const QString appeared = root.filePath(QStringLiteral("appeared.png"));
	ok &= expect(inspectTextureOutputTarget(appeared, false, &exportTarget, &error), "capture a new export destination before encoding");
	put(appeared, "another writer");
	ok &= expect(!writeTextureOutput(exportTarget, saved, false, &error) && read(appeared) == "another writer", "a destination created during encoding is never replaced");
	ok &= expect(!writeTextureOutput(exportTarget, saved, true, &error) && read(appeared) == "another writer", "dry-run also detects destination appearance without writes");
	const QString changed = root.filePath(QStringLiteral("changed.png")); put(changed, "first version");
	ok &= expect(inspectTextureOutputTarget(changed, true, &exportTarget, &error), "observe an explicitly replaceable export");
	put(changed, "outside changes");
	ok &= expect(!writeTextureOutput(exportTarget, saved, false, &error) && read(changed) == "outside changes", "fingerprint mismatch preserves edits made during encoding");
	ok &= expect(inspectTextureOutputTarget(changed, true, &exportTarget, &error) && QFile::remove(changed) && !writeTextureOutput(exportTarget, saved, false, &error) && !QFileInfo::exists(changed), "a deleted destination is a conflict rather than permission to recreate it");
	const QString exported = root.filePath(QStringLiteral("guarded.png"));
	ok &= expect(inspectTextureOutputTarget(exported, false, &exportTarget, &error), "prepare an uncontested export");
	const auto filesBefore = root.entryList(QDir::Files | QDir::Hidden);
	ok &= expect(writeTextureOutput(exportTarget, saved, true, &error) && root.entryList(QDir::Files | QDir::Hidden) == filesBefore, "export dry-run creates no destination or temporary file");
	ok &= expect(writeTextureOutput(exportTarget, saved, false, &error) && read(exported) == saved, "new-file publication preserves encoded bytes");
	ok &= expect(!writeTextureOutput(exportTarget, saved, false, &error) && read(exported) == saved, "a consumed new-file identity cannot overwrite the published export");
	ok &= expect(document.savePng(exported, true, false, &error) && QImage(exported) == document.image(), "explicit replacement uses a fresh destination fingerprint and atomic writer");
	ok &= expect(!inspectTextureOutputTarget(root.filePath(QStringLiteral("missing/dry.png")), false, &exportTarget, &error) && !QFileInfo::exists(root.filePath(QStringLiteral("missing"))), "missing output directories fail without creating folders");
	const QString largeOutput = root.filePath(QStringLiteral("oversized.png"));
	QFile largeFile(largeOutput); ok &= expect(largeFile.open(QIODevice::WriteOnly) && largeFile.resize(textureOutputByteLimit + 1), "create a bounded-inspection fixture"); largeFile.close();
	ok &= expect(!document.savePng(largeOutput, true, true, &error) && QFileInfo(largeOutput).size() == textureOutputByteLimit + 1, "existing exports over the inspection limit are rejected without replacement");
	TextureDocument loaded;
	ok &= expect(loadTextureFile(png, palette, &loaded, &error) && !loaded.isDirty(), "PNG reopens clean");
	const QString lump = root.filePath(QStringLiteral("image.lmp"));
	const QByteArray lumpBytes = QByteArray::fromHex("020000000200000001020304");
	put(lump, lumpBytes); TextureDocument imported;
	ok &= expect(loadTextureFile(lump, palette, &imported, &error) && imported.image().size() == QSize(2, 2) && imported.image().pixel(0, 0) == palette.colorAt(1), "native paletted pixels import through the shared decoder");
	imported.paintStroke({{0, 0}}, Qt::red, 1);
	ok &= expect(imported.savePng(root.filePath(QStringLiteral("imported.png")), false, false, &error) && read(lump) == lumpBytes, "PNG export never rewrites the native source");
	const auto original = loaded.image();
	const QJsonArray badRecipe{QJsonObject{{QStringLiteral("op"), QStringLiteral("flip")}, {QStringLiteral("axis"), QStringLiteral("horizontal")}},
		QJsonObject{{QStringLiteral("op"), QStringLiteral("resize")}, {QStringLiteral("width"), 2.5}, {QStringLiteral("height"), 2}}};
	ok &= expect(!applyTextureOperations(&loaded, badRecipe, palette, &error) && loaded.image() == original && !loaded.canUndo(), "recipe rollback includes history when a later operation fails");
	ok &= expect(!applyTextureOperations(&loaded, QJsonArray{QJsonObject{{QStringLiteral("op"), QStringLiteral("stroke")}, {QStringLiteral("points"), QJsonArray{QJsonArray{1}}}, {QStringLiteral("color"), QStringLiteral("red")}}}, palette, &error), "malformed point arrays fail safely");
	document.create({1, 1}, Qt::black);
	for (int i = 0; i < 100; ++i) { document.paintStroke({{0, 0}}, QColor(i + 1, 0, 0), 1); }
	int count = 0; while (document.undo()) { ++count; }
	ok &= expect(count == 64, "history count is bounded");
	document.paintStroke({{0, 0}}, Qt::green, 1); ok &= expect(!document.canRedo(), "new edits invalidate redo branches");
	PackageArchive archive; PackageStagingModel staging;
	root.mkpath(QStringLiteral("assets"));
	ok &= expect(archive.load(root.filePath(QStringLiteral("assets")), &error) && staging.loadBaseArchive(archive, &error), "open fixture package folder");
	ok &= expect(stageTexturePng(saved, QStringLiteral("textures/custom/edit.png"), &staging, false, &error), "generated PNG bytes stage without scratch files");
	const auto operationCount = staging.operations().size();
	ok &= expect(!stageTexturePng(saved, QStringLiteral("textures/custom/edit.png"), &staging, false, &error) && staging.operations().size() == operationCount, "collision leaves the live plan unchanged");
	ok &= expect(!stageTexturePng(saved, QStringLiteral("../escape.png"), &staging, true, &error) && !stageTexturePng("bad", QStringLiteral("textures/bad.png"), &staging, false, &error), "unsafe paths and malformed payloads cannot stage");
	ok &= expect(stageTexturePng(saved, QStringLiteral("textures/custom/edit.png"), &staging, true, &error), "explicit replacement can update a staged texture");
	PackageStagingArchive planned(staging); QByteArray payload;
	ok &= expect(planned.readEntryBytes(QStringLiteral("textures/custom/edit.png"), &payload, &error) && payload == saved, "shared dependency reader sees authored pixels before save");
	PackageWriteRequest write; write.destinationPath = root.filePath(QStringLiteral("authored.pk3"));
	ok &= expect(staging.writeArchive(write).succeeded(), "authored texture can be packaged");
	PackageArchive packaged;
	ok &= expect(packaged.load(write.destinationPath, &error) && packaged.readEntryBytes(QStringLiteral("textures/custom/edit.png"), &payload, &error) && payload == saved, "package round trip preserves PNG content");
	const QString wad = root.filePath(QStringLiteral("empty.wad")); put(wad, QByteArray::fromHex("50574144000000000c000000"));
	PackageArchive wadArchive; PackageStagingModel wadPlan;
	ok &= expect(wadArchive.load(wad, &error) && wadPlan.loadBaseArchive(wadArchive, &error) && !stageTexturePng(saved, QStringLiteral("TEST"), &wadPlan, false, &error) && wadPlan.operations().isEmpty(), "PNG staging cannot corrupt native WAD texture namespaces");
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const QString output = root.filePath(QStringLiteral("cli.png"));
		const QString recipe = root.filePath(QStringLiteral("recipe.json"));
		put(recipe, "[{\"op\":\"stroke\",\"points\":[[0,0],[7,7]],\"color\":\"#ff0000\"},{\"op\":\"resize\",\"width\":16,\"height\":16}]");
		const auto cli = [&](const QStringList& arguments, int expected) {
			QProcess process; process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")), QStringLiteral("--json")} + arguments);
			if (!process.waitForFinished(30000)) { process.kill(); process.waitForFinished(); return false; }
			const auto result = QJsonDocument::fromJson(process.readAllStandardOutput());
			if (process.exitCode() != expected) { std::cerr << result.toJson().constData(); }
			return process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && result.isObject();
		};
		const QStringList create{QStringLiteral("texture"), QStringLiteral("create"), QStringLiteral("--size=8x8"), QStringLiteral("--color=transparent"), QStringLiteral("--output"), output};
		ok &= expect(cli(create + QStringList{QStringLiteral("--dry-run")}, 0) && !QFileInfo::exists(output), "CLI dry-run is write-free");
		ok &= expect(cli(create, 0) && QImage(output).size() == QSize(8, 8), "CLI create routes and writes pixels");
		ok &= expect(cli(create, 1), "CLI cannot overwrite implicitly");
		const QString edited = root.filePath(QStringLiteral("cli-edited.png"));
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), output, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), edited}, 0) && QImage(edited).pixelColor(14, 14) == QColor(Qt::red), "CLI recipes share stroke and resize semantics");
		put(recipe, "[{\"op\":\"crop\",\"x\":-1,\"y\":0,\"width\":2,\"height\":2}]");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), output, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), edited, QStringLiteral("--overwrite")}, 4) && QImage(edited).size() == QSize(16, 16), "invalid CLI recipe preserves a previous output");
		put(recipe, "[]");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), QStringLiteral("--package"), write.destinationPath, QStringLiteral("--entry=textures/custom/edit.png"), QStringLiteral("--operations"), recipe, QStringLiteral("--output"), root.filePath(QStringLiteral("package-edited.png"))}, 0), "CLI edits a texture from a package");
		put(recipe, R"json([
			{"op":"stroke","points":[[-1,0],[1,0]],"color":"#80ff0000","width":1,"brush":"round","mode":"source-over","wrap":true},
			{"op":"rectangle","x":2,"y":2,"x2":5,"y2":5,"color":"blue","filled":false},
			{"op":"fill","x":3,"y":3,"color":"green","tolerance":5},
			{"op":"ellipse","x":6,"y":6,"x2":7,"y2":7,"color":"yellow","filled":true},
			{"op":"line","x":0,"y":6,"x2":1,"y2":7,"color":"white"},
			{"op":"offset","x":1,"y":0}
		])json");
		const QString toolsOutput = root.filePath(QStringLiteral("cli-tools.png"));
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), output, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), toolsOutput}, 0), "CLI exposes brush shape, alpha mode, wrapped stroke, shapes, tolerant fill, and offset");
		const QImage toolsImage(toolsOutput);
		ok &= expect(toolsImage.pixelColor(0, 0) == QColor(255, 0, 0, 128) && toolsImage.pixelColor(4, 3) == QColor(QStringLiteral("green")) && toolsImage.pixelColor(3, 2) == QColor(Qt::blue) && toolsImage.pixelColor(0, 7) == QColor(Qt::yellow) && toolsImage.pixelColor(2, 7) == QColor(Qt::white), "CLI output reflects exact common-core tool semantics");
		const QString project = root.filePath(QStringLiteral("cli.vtexture"));
		const QString transformed = root.filePath(QStringLiteral("cli-transformed.vtexture"));
		put(recipe, R"json([
			{"op":"canvas-size","width":12,"height":10,"x":2,"y":1},
			{"op":"stroke","points":[[4,3]],"color":"#40d22850"},
			{"op":"select","x":4,"y":3,"width":2,"height":3},
			{"op":"resize-selection","width":4,"height":2,"anchor":"top-left"},
			{"op":"rotate-selection","anchor":"top-left"}
		])json");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), output, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), transformed, QStringLiteral("--dry-run")}, 0) && !QFileInfo::exists(transformed), "CLI transform dry-run validates without creating a project");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), output, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), transformed}, 0) &&
			readTextureProject(transformed, &loaded) && loaded.size() == QSize(12, 10) && loaded.activeLayer()->pixels.pixelColor(5, 3) == QColor(210, 40, 80, 64) && loaded.activeLayer()->pixels.pixelColor(5, 4) == QColor(210, 40, 80, 64), "executable CLI preserves selected transform pixels and alpha in native projects");
		const auto transformedBytes = read(transformed);
		put(recipe, "[{\"op\":\"select\",\"x\":0,\"y\":0,\"width\":2,\"height\":4},{\"op\":\"rotate-selection\"}]");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), transformed, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), transformed, QStringLiteral("--overwrite")}, 4) && read(transformed) == transformedBytes, "invalid selected rotation cannot replace the CLI source project");
		put(recipe, "[{\"op\":\"layer-add\",\"name\":\"Ink\"},{\"op\":\"fill\",\"x\":0,\"y\":0,\"color\":\"blue\"},{\"op\":\"layer-properties\",\"opacity\":50}]");
		const QStringList layered{QStringLiteral("texture"), QStringLiteral("create"), QStringLiteral("--size=8x8"), QStringLiteral("--color=red"), QStringLiteral("--operations"), recipe, QStringLiteral("--output"), project};
		ok &= expect(cli(layered + QStringList{QStringLiteral("--dry-run")}, 0) && !QFileInfo::exists(project), "CLI layered project dry-run remains write-free");
		ok &= expect(cli(layered, 0) && readTextureProject(project, &loaded) && loaded.layers().size() == 2 && loaded.activeLayer()->opacity == 50, "CLI creates a native editable layered project");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("inspect"), project}, 0), "CLI native inspection is registered and validates the saved document");
		put(recipe, "[{\"op\":\"layer-properties\",\"name\":\"Renamed\"}]");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), project, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), project, QStringLiteral("--overwrite")}, 0) && readTextureProject(project, &loaded) && loaded.activeLayer()->name == QStringLiteral("Renamed"), "CLI can update a native source with source fingerprint protection");
		const QString composite = root.filePath(QStringLiteral("composite.png"));
		const auto projectBytes = read(project); put(recipe, "[]");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("edit"), project, QStringLiteral("--operations"), recipe, QStringLiteral("--output"), composite}, 0) && QImage(composite) == loaded.image() && read(project) == projectBytes, "CLI PNG export composites layers without modifying the project");
		put(project, "invalid project");
		ok &= expect(cli({QStringLiteral("texture"), QStringLiteral("inspect"), project}, 4), "CLI inspection reports invalid projects with a validation exit code");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
