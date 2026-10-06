#include "core/advanced_studio.h"
#include "core/level_dependencies.h"
#include "core/model_design.h"
#include "core/package_selection.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
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
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	return file.readAll();
}

const LevelDependency* find(const LevelDependencyReport& report, const QString& reference, const QString& kind = {})
{
	for (const auto& dependency : report.dependencies) {
		if (dependency.reference == reference && (kind.isEmpty() || dependency.kind == kind)) { return &dependency; }
	}
	return nullptr;
}

class FixtureReader : public PackageArchiveReader {
public:
	QVector<PackageEntry> files;
	QByteArray script;
	PackageArchiveFormat archiveFormat = PackageArchiveFormat::Pk3;
	PackageArchiveFormat format() const override { return archiveFormat; }
	QString sourcePath() const override { return QStringLiteral("fixture.pk3"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return files; }
	bool readEntryBytes(const QString&, QByteArray* out, QString*, qint64) const override { *out = script; return true; }
};

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return EXIT_FAILURE; }
	const QDir root(temp.path());
	const QString source = root.filePath(QStringLiteral("assets"));
	const auto asset = [&](const QString& name, const QByteArray& bytes) { return put(QDir(source).filePath(name), bytes); };
	bool ok = true;
	const QByteArray shader = "/* braces { and } in comments */\ntextures/studio/wall {\n"
		"qer_editorimage textures/studio/editor\nq3map_lightimage textures/studio/light\n"
		"{ animMap 2 textures/studio/frame1 textures/studio/frame2 }\n{ map $lightmap }\n}\n";
	ok &= expect(asset(QStringLiteral("scripts/studio.shader"), shader), "write shader fixture");
	for (const QString& path : {QStringLiteral("textures/studio/editor.tga"), QStringLiteral("textures/studio/light.jpg"),
		QStringLiteral("textures/studio/frame1.tga"), QStringLiteral("textures/studio/frame2.png"),
		QStringLiteral("models/props/lamp.md3"), QStringLiteral("sound/world/hum.wav"), QStringLiteral("textures/studio/wall.tga"),
		QStringLiteral("textures/studio2/unrelated.tga"), QStringLiteral("docs/readme.txt")}) {
		ok &= expect(asset(path, path.toUtf8()), "write asset fixture");
	}
	ModelDesign model; ModelDesignPart modelPart; modelPart.material = QStringLiteral("textures/studio/wall"); model.parts << modelPart;
	ok &= expect(asset(QStringLiteral("models/props/lamp.md3"), exportModelDesign(model, QStringLiteral("md3"))), "write generated model fixture with material references");
	LevelMapDocument document;
	document.format = LevelMapFormat::Quake3Map;
	document.engineFamily = QStringLiteral("idTech3");
	document.mapName = QStringLiteral("studio");
	LevelMapBrush brush;
	brush.id = 7;
	LevelMapBrushFace face;
	face.textureName = QStringLiteral("textures/studio/wall");
	brush.faces = {face, face};
	document.brushes << brush;
	LevelMapEntity entity;
	entity.id = 3;
	entity.properties = {{QStringLiteral("model"), QStringLiteral("models/props/lamp.md3"), 0},
		{QStringLiteral("noise"), QStringLiteral("world/hum.wav"), 0}, {QStringLiteral("model2"), QStringLiteral("*1"), 0}};
	document.entities << entity;
	PackageArchive archive;
	QString error;
	ok &= expect(archive.load(source, &error), "load folder assets");
	const LevelDependencyReport report = inspectLevelDependencies(document, archive);
	ok &= expect(report.complete && report.canExport() && report.problemCount == 0, "all explicit dependencies should resolve");
	ok &= expect(report.resolvedPaths.size() == 7 && report.builtinCount == 1, "script, four images, model, and sound should be included once");
	const auto* wall = find(report, QStringLiteral("textures/studio/wall"), QStringLiteral("texture"));
	ok &= expect(wall && wall->resolvedPath == QStringLiteral("scripts/studio.shader") && wall->selectors == QStringList {QStringLiteral("brush:7")}, "shader must take precedence over same-named image with object attribution");
	ok &= expect(!report.resolvedPaths.contains(QStringLiteral("textures/studio/wall.tga")), "unused same-named image must not enter the subset");
	ok &= expect(QJsonDocument(levelDependencyReportJson(report)).toJson() == QJsonDocument(levelDependencyReportJson(inspectLevelDependencies(document, archive))).toJson(), "reports must be deterministic");
	const LevelDependencyReport cancelled = inspectLevelDependencies(document, archive, [](int, int) { return false; });
	ok &= expect(cancelled.cancelled && !cancelled.complete && !cancelled.canExport(), "cancelled scans must not authorize export");
	int callbacks = 0;
	const auto duringRead = inspectLevelDependencies(document, archive, [&](int, int) { return ++callbacks < 3; });
	ok &= expect(duringRead.cancelled, "cancellation must be observed after a shader read");

	LevelMapDocument missing = document;
	missing.entities[0].properties.append({QStringLiteral("sound"), QStringLiteral("gone.wav"), 0});
	missing.entities[0].properties.append({QStringLiteral("model"), QStringLiteral("../escape.md3"), 0});
	const auto missingReport = inspectLevelDependencies(missing, archive);
	ok &= expect(missingReport.missingCount == 1 && missingReport.problemCount == 2 && !missingReport.canExport(), "missing and unsafe references should block export");
	ok &= expect(find(missingReport, QStringLiteral("gone.wav"))->selectors.contains(QStringLiteral("entity:3")), "missing sound must name its entity");

	PackageSelectionRequest selection;
	selection.prefixes = {QStringLiteral("textures/studio/")};
	selection.query = QStringLiteral("ext=tga");
	const auto selected = selectPackageFiles(archive, selection);
	ok &= expect(selected.succeeded() && selected.paths.size() == 3 && !selected.paths.contains(QStringLiteral("textures/studio2/unrelated.tga")), "prefix boundary and query must both hold");
	selection.entries << QStringLiteral("docs/readme.txt");
	selection.query.clear();
	ok &= expect(selectPackageFiles(archive, selection).paths.size() == 6, "entries and prefixes should form a union");
	selection.prefixes << QStringLiteral("does-not-exist");
	ok &= expect(!selectPackageFiles(archive, selection).succeeded(), "misspelled prefixes must not silently yield a partial selection");
	ok &= expect(!selectPackageFiles(archive, {}).succeeded(), "empty selection must not export everything");
	ok &= expect(!selectPackageFiles(archive, {{QStringLiteral("../escape")}, {}, {}, {}}).succeeded(), "unsafe entry selector must fail");
	ok &= expect(!selectPackageFiles(archive, {{}, {}, QStringLiteral("colour=red"), {}}).succeeded(), "unknown query fields must fail");

	PackageStagingModel subset;
	ok &= expect(subset.loadBaseArchiveSubset(archive, report.resolvedPaths, &error), "dependency subset should stage");
	const QByteArray before = subset.manifestJson();
	ok &= expect(!subset.loadBaseArchiveSubset(archive, {QStringLiteral("absent")}, &error) && subset.manifestJson() == before, "failed subset selection must preserve the previous plan");
	PackageWriteRequest write;
	write.destinationPath = root.filePath(QStringLiteral("level-assets.pk3"));
	write.verifyDeterminism = true;
	const auto written = subset.writeArchive(write);
	ok &= expect(written.succeeded() && written.entryCount == 7 && written.determinismVerified && written.deterministic, "subset archive should write deterministically");
	PackageArchive rebuilt;
	ok &= expect(rebuilt.load(write.destinationPath, &error), "exported subset must reopen");
	for (const QString& path : report.resolvedPaths) {
		QByteArray bytes;
		ok &= expect(rebuilt.readEntryBytes(path, &bytes, &error) && bytes == get(QDir(source).filePath(path)), "subset bytes must match original source files");
	}
	const auto exportedReport = inspectLevelDependencies(document, rebuilt);
	ok &= expect(exportedReport.canExport() && exportedReport.problemCount == 0, "exported asset package must resolve the same map dependencies");
	write.destinationPath = root.filePath(QStringLiteral("cancelled.pk3"));
	write.isCancelled = []() { return true; };
	ok &= expect(subset.writeArchive(write).cancelled && !QFileInfo::exists(write.destinationPath), "pre-cancelled writer must leave no output");
	write.allowOverwrite = true;
	ok &= expect(put(write.destinationPath, "keep-me"), "write existing output fixture");
	int cancelChecks = 0;
	write.isCancelled = [&]() { return ++cancelChecks >= 3; };
	const auto interrupted = subset.writeArchive(write);
	ok &= expect(interrupted.cancelled && !interrupted.succeeded() && get(write.destinationPath) == "keep-me", "mid-export cancellation must preserve an existing destination");
	write.isCancelled = {};
	write.destinationPath = QDir(source).filePath(QStringLiteral("sound/world/hum.wav"));
	ok &= expect(!subset.writeArchive(write).succeeded() && get(write.destinationPath) == QByteArray("sound/world/hum.wav"), "export must not overwrite source-folder content");
	write.destinationPath = root.filePath(QStringLiteral("manifest-collision.pk3"));
	write.writeManifest = true;
	write.manifestPath = write.destinationPath;
	ok &= expect(!subset.writeArchive(write).succeeded() && !QFileInfo::exists(write.destinationPath), "manifest collision must be rejected before creating an archive");
	ok &= expect(!subset.exportManifest(QDir(source).filePath(QStringLiteral("docs/readme.txt")), &error), "standalone manifests must protect source files");

	FixtureReader duplicate;
	PackageEntry first;
	first.virtualPath = QStringLiteral("models/props/lamp.md3");
	duplicate.files = {first, first};
	ok &= expect(!selectPackageFiles(duplicate, {{first.virtualPath}, {}, {}, {}}).succeeded(), "duplicate paths must be ambiguous");
	duplicate.archiveFormat = PackageArchiveFormat::Wad;
	ok &= expect(!subset.loadBaseArchiveSubset(duplicate, {first.virtualPath}, &error), "WAD subset must be refused before corrupting namespace groups");
	duplicate.archiveFormat = PackageArchiveFormat::Pk3;
	const auto ambiguous = inspectLevelDependencies(document, duplicate);
	ok &= expect(find(ambiguous, first.virtualPath)->status == LevelDependencyStatus::Ambiguous, "dependency resolver must reject duplicate paths");
	first.virtualPath = QStringLiteral("scripts/large.shader");
	first.sizeBytes = 5 * 1024 * 1024;
	duplicate.files = {first};
	const auto budget = inspectLevelDependencies(document, duplicate);
	ok &= expect(!budget.complete && !budget.canExport() && !budget.warnings.isEmpty(), "oversized shader must mark scan incomplete");
	ok &= expect(asset(QStringLiteral("scripts/duplicate.shader"), shader) && archive.load(source, &error), "duplicate shader fixture");
	const auto duplicateShader = inspectLevelDependencies(document, archive);
	ok &= expect(find(duplicateShader, QStringLiteral("textures/studio/wall"))->status == LevelDependencyStatus::Ambiguous, "duplicate shader declarations must be reported");
	QFile::remove(QDir(source).filePath(QStringLiteral("scripts/duplicate.shader")));
	ok &= expect(asset(QStringLiteral("scripts/broken.shader"), "textures/a { { map textures/a\n") && archive.load(source, &error), "malformed script fixture");
	ok &= expect(!inspectLevelDependencies(document, archive).complete, "malformed scripts must block claims of a complete audit");
	QFile::remove(QDir(source).filePath(QStringLiteral("scripts/broken.shader")));

	const auto sky = parseShaderScriptText(QStringLiteral("/* header\ncomment */\ntextures/sky {\nskyParms env/day 512 -\nq3map_lightimage textures/light\n{ map textures/cloud }\n}\n"));
	ok &= expect(sky.issues.isEmpty() && sky.shaders.size() == 1 && sky.shaders.first().textureReferences.size() == 8 && sky.shaders.first().startLine == 3, "inline braces, multiline comments, light image, and six sky faces must parse with source locations");
	ok &= expect(!parseShaderScriptText(QStringLiteral("textures/a { /*")).issues.isEmpty(), "unterminated comment must produce a diagnostic");
	ok &= expect(!parseShaderScriptText(QStringLiteral("textures/a { { map } }")).issues.isEmpty(), "missing asset directive argument must produce a diagnostic");
	const auto quoted = parseShaderScriptText(QStringLiteral("\"textures/quoted\" { { map \"textures/with space.tga\" } }"));
	ok &= expect(quoted.issues.isEmpty() && quoted.shaders.first().textureReferences == QStringList {QStringLiteral("textures/with space.tga")}, "quoted references and inline braces must parse");

	if (argc > 1) {
		const QString mapPath = root.filePath(QStringLiteral("cli.map"));
		ok &= expect(put(mapPath, "{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"misc_model\"\n\"model\" \"models/props/lamp.md3\"\n}\n"), "CLI map fixture");
		const auto cli = [&](const QStringList& args, int expected, const char* message) {
			QProcess process;
			process.start(QString::fromLocal8Bit(argv[1]), QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), root.filePath(QStringLiteral("settings.ini")), QStringLiteral("--json")} + args);
			const bool finished = process.waitForFinished(30000);
			const QByteArray output = process.readAllStandardOutput();
			const bool valid = finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && QJsonDocument::fromJson(output).isObject();
			if (!valid) { std::cerr << output.constData() << process.readAllStandardError().constData(); }
			return expect(valid, message);
		};
		ok &= cli({QStringLiteral("map"), QStringLiteral("dependencies"), mapPath, QStringLiteral("--package"), source, QStringLiteral("--engine"), QStringLiteral("idTech3")}, 0, "CLI dependencies should return valid successful JSON");
		const QString cliOutput = root.filePath(QStringLiteral("cli-subset.pk3"));
		ok &= cli({QStringLiteral("package"), QStringLiteral("subset"), source, cliOutput, QStringLiteral("--map-input"), mapPath, QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--dry-run")}, 0, "map-driven subset dry run should succeed");
		ok &= expect(!QFileInfo::exists(cliOutput), "dry run must not create a package");
		ok &= cli({QStringLiteral("package"), QStringLiteral("subset"), source, cliOutput, QStringLiteral("--prefix"), QStringLiteral("models")}, 0, "CLI prefix subset should write");
		ok &= expect(rebuilt.load(cliOutput, &error) && rebuilt.summary().fileCount == 1, "CLI subset should contain exactly the selected file");
		ok &= cli({QStringLiteral("package"), QStringLiteral("subset"), source, cliOutput}, 2, "empty CLI subset should be a usage error");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
