#include "core/level_build_artifacts.h"
#include "core/package_validation.h"
#include "tests/level_material_test_helpers.h"
#include "tests/package_subset_test_helpers.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <iostream>
using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool pass, const char* label, const QString& error = {}) {
	if (!pass) {
		ok = false;
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return pass;
}
int compiler(const QStringList& args) {
	const QFileInfo input(args.last());
	const auto name = input.completeBaseName();
	const auto maps = input.absolutePath();
	if (args.contains("-light") && args.contains("--fixture-fail")) {
		std::cerr << "ERROR: intentional failed lighting fixture\n";
		return 7;
	}
	QByteArray bsp(8 + 17 * 8, '\0');
	bsp.replace(0, 4, "IBSP");
	qToLittleEndian<qint32>(46, reinterpret_cast<uchar*>(bsp.data() + 4));
	if (!tests::putMaterialFile(QDir(maps).filePath(name + ".bsp"), bsp)) {
		return 8;
	}
	if (!args.contains("-vis") && !args.contains("-light")) {
		tests::putMaterialFile(QDir(maps).filePath(name + ".prt"), "PRT1\n0\n0\n");
	}
	if (args.contains("-light")) {
		tests::putMaterialFile(QDir(maps).filePath(name + "/lm_0000.tga"),
							   QByteArray::fromHex("000002000000000000000000010001001800") + QByteArray(3, 'x'));
		tests::putMaterialFile(QDir(maps).filePath("../scripts/q3map2_" + name + ".shader"),
							   "textures/generated/test\n{\n { map maps/" + name.toUtf8() + "/lm_0000.tga }\n}\n");
	}
	return 0;
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	if (app.arguments().contains("-fs_basepath")) {
		return compiler(app.arguments());
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	QString error;
	LevelMapDocument map;
	if (!expect(tests::createMaterialFixture(temp.path(), &map, &error), "asset fixture", error)) {
		return 1;
	}
	PackageArchive archive;
	tests::putMaterialFile(temp.filePath("assets/scripts/q3map2_studio_build.shader"), "// previous compiler output\n");
	tests::putMaterialFile(temp.filePath("assets/maps/studio_build/lm_0000.tga"), "previous lightmap");
	expect(archive.load(temp.filePath("assets"), &error), "load assets", error);
	LevelBuildWorkspaceRequest prepare;
	prepare.directory = temp.filePath("build workspace");
	const auto workspace = prepareLevelBuildWorkspace(map, archive, prepare);
	if (!expect(workspace.ready, "prepared source", workspace.error)) {
		return 1;
	}
	expect(!QFileInfo::exists(QDir(workspace.assetsPath()).filePath("scripts/q3map2_studio_build.shader")) &&
			   !QFileInfo::exists(QDir(workspace.assetsPath()).filePath("maps/studio_build/lm_0000.tga")),
		   "preparation omits stale generated runtime files");
	expect(!inspectLevelBuildArtifacts(workspace).verified, "unbuilt outputs cannot publish");
	BuildPipelineRequest request;
	request.pipelineId = "quake3-full";
	request.executableOverrides = {{"q3map2", app.applicationFilePath()}};
	request.registerOutputs = true;
	auto run = runLevelBuildWorkspace(workspace, request);
	if (!expect(run.succeeded(), "compiler fixture succeeds", run.errors.join('\n'))) {
		return 1;
	}
	const auto artifacts = inspectLevelBuildArtifacts(workspace);
	expect(artifacts.verified && artifacts.outputs.size() == 4, "output inventory includes BSP, shader, lightmap and diagnostic",
		   artifacts.error);
	const auto record = QDir(workspace.directory).filePath("build-outputs.json");
	const auto receipt = subset_test::get(record);
	expect(artifacts.recordSha256 == QCryptographicHash::hash(receipt, QCryptographicHash::Sha256), "review token hashes actual receipt");
	const auto originalRecord = QJsonDocument::fromJson(receipt).object();
	const auto invalidRecord = [&](QJsonObject json, const char* label) {
		tests::putMaterialFile(record, QJsonDocument(json).toJson());
		expect(!inspectLevelBuildArtifacts(workspace).verified, label);
		tests::putMaterialFile(record, receipt);
	};
	auto bad = originalRecord;
	bad["runId"] = "not-a-build-id";
	invalidRecord(bad, "malformed run identifier rejected");
	bad = originalRecord;
	bad["pipelineId"] = "unknown-pipeline";
	invalidRecord(bad, "unknown pipeline rejected");
	bad = originalRecord;
	auto records = bad["outputs"].toArray();
	auto entry = records[0].toObject();
	entry["path"] = "game/baseq3/maps/../../escape.bsp";
	records[0] = entry;
	bad["outputs"] = records;
	invalidRecord(bad, "traversing output record rejected");
	bad = originalRecord;
	records = bad["outputs"].toArray();
	records.append(records[0]);
	bad["outputs"] = records;
	invalidRecord(bad, "duplicate output record rejected");
	bad = originalRecord;
	bad["inputSha256"] = QString(64, '0');
	invalidRecord(bad, "outputs from different captured inputs rejected");
	expect(run.registeredOutputPaths.contains(QDir(workspace.assetsPath()).filePath("maps/studio_build/lm_0000.tga")),
		   "runtime outputs registered");
	LevelBuildPackageRequest package;
	package.outputPath = temp.filePath("published.pk3");
	package.expectedRecordSha256 = artifacts.recordSha256;
	package.dryRun = true;
	auto publication = publishLevelBuildPackage(workspace, package);
	expect(publication.succeeded() && !QFileInfo::exists(package.outputPath), "dry publication writes no archive", publication.error);
	package.dryRun = false;
	publication = publishLevelBuildPackage(workspace, package);
	expect(publication.succeeded() && publication.write.outputCommitted && publication.write.deterministic,
		   "atomic deterministic publication", publication.error);
	const auto publishedBytes = subset_test::get(package.outputPath);
	PackageArchive published;
	expect(published.load(package.outputPath, &error) && validatePackage(published).valid(), "valid PK3", error);
	QByteArray payload;
	expect(published.readEntryBytes("maps/studio_build/lm_0000.tga", &payload, &error) && !payload.isEmpty(), "external lightmap packaged",
		   error);
	expect(published.readEntryBytes("scripts/q3map2_studio_build.shader", &payload, &error), "generated shader packaged", error);
	expect(!publication.paths.contains("maps/studio_build.map") && !publication.paths.contains("maps/studio_build.prt"),
		   "source optional and diagnostics excluded");
	expect(!publishLevelBuildPackage(workspace, package).succeeded() && subset_test::get(package.outputPath) == publishedBytes,
		   "existing output preserved");
	package.allowOverwrite = true;
	package.includeSourceMap = true;
	publication = publishLevelBuildPackage(workspace, package);
	expect(publication.succeeded() && subset_test::get(package.outputPath + ".bak") == publishedBytes &&
			   publication.paths.contains("maps/studio_build.map"),
		   "source option and verified overwrite backup", publication.error);
	const auto lightmap = QDir(workspace.assetsPath()).filePath("maps/studio_build/lm_0000.tga");
	const auto lightBytes = subset_test::get(lightmap);
	auto damage = lightBytes;
	damage[damage.size() - 1] ^= 1;
	tests::putMaterialFile(lightmap, damage);
	expect(!inspectLevelBuildArtifacts(workspace).verified && !publishLevelBuildPackage(workspace, package).succeeded(),
		   "changed runtime output cannot publish");
	auto incremental = request;
	incremental.disabledStageIds << "bsp";
	expect(!runLevelBuildWorkspace(workspace, incremental).succeeded(), "incremental build refuses changed previous outputs");
	run = runLevelBuildWorkspace(workspace, request);
	auto current = inspectLevelBuildArtifacts(workspace);
	expect(run.succeeded() && current.verified && subset_test::get(lightmap) == lightBytes,
		   "full rebuild replaces changed generated outputs", current.error);
	expect(subset_test::get(
			   QDir(workspace.directory).filePath("history/" + current.runId + "/game/baseq3/maps/studio_build/lm_0000.tga")) == damage,
		   "previous generated bytes retained outside compiler search paths");
	package.outputPath = temp.filePath("stale-review.pk3");
	expect(!publishLevelBuildPackage(workspace, package).succeeded() && !QFileInfo::exists(package.outputPath),
		   "stale review token blocks publication");
	package.expectedRecordSha256 = current.recordSha256;
	QLockFile lock(QDir(workspace.directory).filePath(".build-workspace.lock"));
	lock.setStaleLockTime(0);
	expect(lock.tryLock(0), "test owns workspace lease");
	expect(!runLevelBuildWorkspace(workspace, request).succeeded() && !publishLevelBuildPackage(workspace, package).succeeded(),
		   "build/publication lease excludes concurrent writes");
	lock.unlock();
	request.stageExtraArguments["light"] = {"--fixture-fail"};
	run = runLevelBuildWorkspace(workspace, request);
	expect(!run.succeeded() && !inspectLevelBuildArtifacts(workspace).verified && !publishLevelBuildPackage(workspace, package).succeeded(),
		   "failed build cannot promote partial outputs");
	request.stageExtraArguments.clear();
	bool cancelBuild = false;
	BuildPipelineCallbacks callbacks;
	callbacks.cancellationRequested = [&] { return cancelBuild; };
	callbacks.logEntry = [&](const CompilerTaskLogEntry& entry) {
		if (entry.message == "Recording compiler outputs…") {
			cancelBuild = true;
		}
	};
	run = runLevelBuildWorkspace(workspace, request, callbacks);
	expect(run.cancelled && !inspectLevelBuildArtifacts(workspace).verified, "cancelled output capture invalidates publication");
	run = runLevelBuildWorkspace(workspace, request);
	current = inspectLevelBuildArtifacts(workspace);
	expect(run.succeeded() && current.verified, "retry full build after failure", run.errors.join('\n'));
	package.expectedRecordSha256 = current.recordSha256;
	package.outputPath = temp.filePath("cancelled.pk3");
	bool cancel = false;
	PackageReadControl control;
	control.isCancelled = [&] { return cancel; };
	control.progress = [&](const QString& path, qint64, qint64) {
		if (path.startsWith("Writing package:")) {
			cancel = true;
		}
	};
	publication = publishLevelBuildPackage(workspace, package, control);
	expect(publication.cancelled && !QFileInfo::exists(package.outputPath), "cancel writing before atomic publication", publication.error);
	package.outputPath = temp.filePath("changed-during-write.pk3");
	bool changed = false;
	control.isCancelled = {};
	control.progress = [&](const QString& path, qint64, qint64) {
		if (!changed && path.startsWith("Writing package:")) {
			changed = true;
			tests::putMaterialFile(lightmap, damage);
		}
	};
	publication = publishLevelBuildPackage(workspace, package, control);
	expect(changed && !publication.succeeded() && !QFileInfo::exists(package.outputPath),
		   "chunk identities prevent changed bytes from reaching publication");
	tests::putMaterialFile(lightmap, lightBytes);
	const auto extra = QDir(workspace.assetsPath()).filePath("maps/studio_build/lm_0001.tga");
	tests::putMaterialFile(extra, lightBytes);
	expect(!inspectLevelBuildArtifacts(workspace).verified, "unrecorded known output refused");
	QFile::remove(extra);
	package.outputPath = QDir(workspace.directory).filePath("output.pk3");
	expect(!publishLevelBuildPackage(workspace, package).succeeded(), "workspace protected from package output");
	if (argc > 1) {
		const auto cli = [&](const QStringList& arguments, int code = 0) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{"--cli", "--json", "--settings-file", temp.filePath("settings.ini"), "build"} + arguments);
			const bool done = process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			if (!done || process.exitCode() != code) {
				std::cerr << bytes.toStdString() << process.readAllStandardError().toStdString();
			}
			expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "artifact CLI status");
			const auto json = QJsonDocument::fromJson(bytes);
			expect(json.isObject(), "artifact CLI JSON");
			return json.object();
		};
		const auto reviewed = cli({"artifacts", workspace.directory})["artifacts"].toObject();
		expect(reviewed["verified"].toBool(), "CLI artifact review");
		const auto output = temp.filePath("cli.pk3");
		cli({"publish-prepared", workspace.directory, "--output", output, "--dry-run"});
		expect(!QFileInfo::exists(output), "CLI dry publication");
		cli({"publish-prepared", workspace.directory, "--output", output, "--expected-output-sha256", reviewed["recordSha256"].toString()});
		cli({"publish-prepared", workspace.directory, "--output", output}, 4);
		cli({"artifacts", workspace.directory, "--overwrite"}, 2);
		cli({"publish-prepared", workspace.directory, "--output", output, "--compression", "typo"}, 2);
		cli({"publish-prepared", workspace.directory, "--output", output, "--expected-output-sha256", "bad"}, 2);
	}
	expect(verifyLevelBuildWorkspace(workspace, &error), "captured inputs survive builds and publication", error);
	return ok ? 0 : 1;
}
