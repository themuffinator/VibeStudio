#include "core/level_build_artifacts.h"
#include "core/level_build_deployment.h"
#include "core/level_quake_assets.h"
#include "tests/level_build_engines_test_helpers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#ifdef Q_OS_WIN
#include <atomic>
#include <qt_windows.h>
#include <thread>
#endif
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
	const auto stem = QDir(input.absolutePath()).filePath(input.completeBaseName());
	if (input.suffix() == "map") {
		QByteArray bsp(args.contains("-q2bsp") ? 160 : 124, '\0');
		if (args.contains("-q2bsp")) {
			bsp.replace(0, 4, "IBSP");
			qToLittleEndian<qint32>(38, bsp.data() + 4);
		} else {
			qToLittleEndian<qint32>(29, bsp.data());
		}
		if (!subset_test::put(stem + ".bsp", bsp) || !subset_test::put(stem + ".prt", "PRT1\n0\n0\n")) {
			return 8;
		}
	}
	if (args.contains("-lit")) {
		subset_test::put(stem + ".lit", QByteArray("QLIT\1\0\0\0", 8) + QByteArray(3, 'a'));
	}
	const auto log = args.indexOf("-logfile");
	if (log >= 0) {
		subset_test::put(args[log + 1], "Synthetic ericw compiler fixture\n");
	}
	return 0;
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	if (app.arguments().contains("-nodefaultpaths")) {
		return compiler(app.arguments());
	}
	QTemporaryDir temp;
	if (!temp.isValid() || argc != 2) {
		return 1;
	}
	QString error;
	for (const auto& target : QStringList{"quake", "quake2"}) {
		const auto root = temp.filePath(target);
		QDir().mkpath(root);
		LevelMapDocument map;
		PackageArchive archive;
		if (!expect(tests::buildEngineFixture(root, target, &map, &archive, &error), "fixture", error)) {
			return 1;
		}
		if (target == "quake") {
			LevelSceneNode locked;
			locked.id = QStringLiteral("11111111-1111-4111-8111-111111111111");
			locked.name = QStringLiteral("Locked world");
			locked.kind = LevelSceneNodeKind::Layer;
			locked.locked = true;
			locked.objects = {QStringLiteral("brush:0")};
			map.scene.nodes << locked;
		}
		const auto original = serializeLevelMap(map).bytes;
		LevelBuildWorkspaceRequest prepare;
		prepare.directory = QDir(root).filePath("build with spaces");
		prepare.target = target;
		prepare.dryRun = true;
		const auto dry = prepareLevelBuildWorkspace(map, archive, prepare);
		expect(dry.ready && !QFileInfo::exists(prepare.directory), "dry capture has no filesystem output", dry.error);
		prepare.dryRun = false;
		const auto workspace = prepareLevelBuildWorkspace(map, archive, prepare);
		if (!expect(workspace.ready, "capture succeeds", workspace.error)) {
			return 1;
		}
		expect(serializeLevelMap(map).bytes == original, "live map is unchanged");
		const auto reopened = readLevelBuildWorkspace(workspace.directory);
		expect(reopened.ready && reopened.target == target && verifyLevelBuildWorkspace(reopened, &error),
			   "target-aware inventory round trip", error);
		expect(workspace.inputPath().contains(target == "quake" ? "/game/id1/" : "/game/baseq2/"), "engine layout");
#ifdef Q_OS_WIN
		if (target == "quake2") {
			for (const auto& mode : QStringList{"retry", "collision", "cancel"}) {
				const auto retryRoot = QDir(root).filePath("rename " + mode);
				QDir().mkpath(retryRoot);
				prepare.directory = QDir(retryRoot).filePath("prepared");
				const auto destination = prepare.directory;
				std::thread release;
				std::atomic_bool cancelled{false};
				std::atomic_bool collisionCreated{false};
				HANDLE cancelHandle = INVALID_HANDLE_VALUE;
				bool held = false;
				PackageReadControl control;
				control.isCancelled = [&] { return cancelled.load(); };
				control.progress = [&](const QString& phase, qint64, qint64) {
					if (held) {
						if (mode == "cancel" && phase == QStringLiteral("Waiting to publish the build workspace…")) {
							cancelled = true;
							CloseHandle(cancelHandle);
							cancelHandle = INVALID_HANDLE_VALUE;
						}
						return;
					}
					const auto candidates =
						QDir(retryRoot).entryList({".vibestudio-build-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
					if (candidates.size() != 1) {
						return;
					}
					const auto path = QDir::toNativeSeparators(QDir(retryRoot).filePath(candidates.first()));
					const auto handle =
						CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
									OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
					if (handle == INVALID_HANDLE_VALUE) {
						return;
					}
					held = true;
					if (mode == "cancel") {
						cancelHandle = handle;
						return;
					}
					release = std::thread([&, handle, destination] {
						Sleep(100);
						if (mode == "collision") {
							collisionCreated =
								QDir().mkpath(destination) && subset_test::put(QDir(destination).filePath("keep.txt"), "keep destination");
						}
						CloseHandle(handle);
					});
				};
				const auto retried = prepareLevelBuildWorkspace(map, archive, prepare, control);
				if (cancelHandle != INVALID_HANDLE_VALUE) {
					CloseHandle(cancelHandle);
				}
				if (release.joinable()) {
					release.join();
				}
				if (mode == "retry") {
					expect(held && retried.ready && verifyLevelBuildWorkspace(retried, &error),
						   "transient directory handle does not lose capture", retried.error + error);
				} else if (mode == "collision") {
					expect(held && collisionCreated && !retried.ready &&
							   subset_test::get(QDir(destination).filePath("keep.txt")) == "keep destination" &&
							   !QFileInfo::exists(QDir(destination).filePath("build-inputs.json")),
						   "retry never replaces a new destination", retried.error);
				} else {
					expect(held && retried.cancelled && !retried.ready && !QFileInfo::exists(destination),
						   "publication retry remains cancellable", retried.error);
				}
			}
		}
#endif
		if (target == "quake") {
			PackageArchive wad;
			expect(wad.load(QDir(workspace.directory).filePath(workspace.textureWadPath()), &error), "captured WAD opens", error);
			expect(wad.entries().size() == 4, "animation frames and liquid survive WAD capture");
			expect(!subset_test::get(workspace.inputPath()).contains("Z:/unavailable") &&
					   subset_test::get(workspace.inputPath()).contains("studio_build.wad"),
				   "captured map uses portable WAD");
			LevelBuildWorkspaceRequest direct;
			direct.directory = QDir(root).filePath("direct wad");
			const auto directWorkspace = prepareLevelBuildWorkspace(map, wad, direct);
			expect(directWorkspace.ready, "direct WAD with liquid names supports Windows", directWorkspace.error);
		}
		BuildPipelineRequest build;
		build.pipelineId = "quake-full";
		build.executableOverrides = {{"vibemap2-bsp", app.applicationFilePath()},
									 {"vibemap2-vis", app.applicationFilePath()},
									 {"vibemap2-light", app.applicationFilePath()}};
		if (target == "quake") {
			build.stageExtraArguments["light"] = {"-lit"};
		}
		expect(levelBuildArtifactKind(workspace.assetPrefix() + "maps/studio_build.vis", workspace.mapName, target) ==
					   LevelBuildArtifactKind::Diagnostic &&
				   levelBuildArtifactKind(workspace.assetPrefix() + "maps/studio_build.vi0", workspace.mapName, target) ==
					   LevelBuildArtifactKind::Diagnostic,
			   "ericw visibility state stays diagnostic");
		auto configured = build;
		expect(configureLevelBuildPipeline(workspace, &configured, &error), "configure", error);
		const auto args = configured.stageExtraArguments;
		expect(configureLevelBuildPipeline(workspace, &configured, &error) && args == configured.stageExtraArguments,
			   "configuration is idempotent");
		expect(configured.stageExtraArguments["qbsp"].contains("-q2bsp") == (target == "quake2"), "explicit BSP dialect");
		for (const auto& flag : QStringList{"-path", "-wadpath", "-aliasdef", "-texturedefs", "-logfile", "-defaultpaths", "-convert",
											"-onlyents", "--path"}) {
			auto conflicting = build;
			conflicting.stageExtraArguments["qbsp"] = {flag, "outside"};
			expect(!configureLevelBuildPipeline(workspace, &conflicting, &error), "reject escaping/incompatible options");
		}
		for (const auto& words : QList<QStringList>{
				 {"2"}, {"-threads"}, {"-threads", "-path"}, {"-lit", "2"}, {"-threads", "2", "3"}, {"-phong", "2"}, {"-extra4", "1"}}) {
			auto conflicting = build;
			conflicting.stageExtraArguments["light"] = words;
			const auto before = conflicting.stageExtraArguments;
			expect(!configureLevelBuildPipeline(workspace, &conflicting, &error) && conflicting.stageExtraArguments == before,
				   "reject positional numbers and missing values without mutating request");
		}
		for (const auto& words : QList<QStringList>{{"-threads", "2"},
													{"-soft"},
													{"-soft", "2"},
													{"-bounce"},
													{"-bounce", "2", "-lit", "-lux"},
													{"-phong", "0"},
													{"-extra4"}}) {
			auto scalar = build;
			scalar.stageExtraArguments["light"] = words;
			expect(configureLevelBuildPipeline(workspace, &scalar, &error), "accept bounded scalar compiler options", error);
		}
		if (target == "quake2") {
			auto wrongTarget = build;
			wrongTarget.stageExtraArguments["qbsp"] = {"-BSP2"};
			expect(!configureLevelBuildPipeline(workspace, &wrongTarget, &error), "target override is case insensitive");
		}
		auto run = runLevelBuildWorkspace(workspace, build);
		if (!expect(run.succeeded(), "three-stage captured build", run.errors.join('\n'))) {
			return 1;
		}
		expect(!run.warnings.join('\n').contains("does not match the selected compiler profile"),
			   "VIS and LIGHT accept the captured Quake II BSP dialect");
		const auto receipt = inspectLevelBuildArtifacts(workspace);
		expect(receipt.verified, "receipt verifies", receipt.error);
		LevelBuildPackageRequest publish;
		publish.outputPath = QDir(root).filePath("pak0.pak");
		const auto published = publishLevelBuildPackage(workspace, publish);
		expect(published.succeeded() && published.paths.contains("maps/studio_build.bsp") &&
				   !published.paths.contains("maps/studio_build.map") && !published.paths.contains("maps/studio_build.wad"),
			   "runtime PAK excludes captured sources", published.error);
		expect(published.paths.contains("maps/studio_build.lit") == (target == "quake"), "Quake colored lighting is a runtime asset");
		expect(!published.paths.contains("maps/studio_build.qbsp.log") && !published.paths.contains("maps/studio_build.prt"),
			   "diagnostics stay in workspace");
		PackageArchive pak;
		expect(pak.load(publish.outputPath, &error) && pak.format() == PackageArchiveFormat::Pak, "published PAK opens", error);
		QByteArray bsp;
		expect(pak.readEntryBytes("maps/studio_build.bsp", &bsp, &error) &&
				   bsp == subset_test::get(workspace.inputPath().chopped(4) + ".bsp"),
			   "published BSP bytes match verified output");
		publish.outputPath = QDir(root).filePath("sources.pak");
		publish.includeSourceMap = true;
		const auto sources = publishLevelBuildPackage(workspace, publish);
		expect(sources.succeeded() && sources.paths.contains("maps/studio_build.map") &&
				   (target != "quake" || sources.paths.contains("maps/studio_build.wad")),
			   "source-inclusive publication", sources.error);
		GameInstallationProfile installation;
		installation.gameKey = "quake3";
		installation.engineFamily = GameEngineFamily::IdTech3;
		expect(!planLevelBuildDeployment(workspace, installation, {}).error.isEmpty() &&
				   !levelBuildDeploymentLaunchPlan(workspace, installation, {}).runnable,
			   "never route Quake PAK to Quake III deployment");
		build.disabledStageIds = {"qbsp"};
		expect(runLevelBuildWorkspace(workspace, build).succeeded(), "verified incremental vis/light");
		const auto source = QDir(root).filePath("source.map");
		subset_test::put(source, original);
		const auto cli = [&](QStringList words, int code) {
			QProcess process;
			process.setWorkingDirectory(root);
			process.start(QString::fromLocal8Bit(argv[1]),
						  QStringList{"--cli", "--json", "--settings-file", QDir(root).filePath("settings.ini"), "build"} + words);
			const bool finished = process.waitForFinished(30000);
			const auto out = process.readAllStandardOutput();
			expect(finished && process.exitCode() == code, "CLI status", QString::fromUtf8(out + process.readAllStandardError()));
			return QJsonDocument::fromJson(out).object();
		};
		const auto cliRoot = QDir(root).filePath("cli workspace");
		cli({"prepare", source, "--target", target, "--package", archive.sourcePath(), "--output", cliRoot}, 0);
		cli({"run-prepared", cliRoot, "--tool", "vibemap2-bsp=" + app.applicationFilePath(), "--tool",
			 "vibemap2-vis=" + app.applicationFilePath(), "--tool", "vibemap2-light=" + app.applicationFilePath()},
			0);
		cli({"publish-prepared", cliRoot, "--output", QDir(root).filePath("cli.pak")}, 0);
		cli({"run-prepared", cliRoot, "--tool", "vibemap2-bsp=one", "--tool", "vibemap2-bsp=two"}, 2);
		cli({"run-prepared", cliRoot, "--pipeline", "quake3-full"}, 4);
		if (target == "quake") {
			const auto generated = QDir(workspace.directory).filePath(workspace.textureWadPath());
			auto damaged = subset_test::get(generated);
			damaged.back() ^= 1;
			subset_test::put(generated, damaged);
			expect(!verifyLevelBuildWorkspace(workspace, &error) && !inspectLevelBuildArtifacts(workspace).verified,
				   "WAD mutation invalidates inputs and outputs");
			PackageStagingModel duplicates;
			expect(duplicates.loadBaseArchive(archive, &error), "duplicate fixture");
			duplicates.addBytes(tests::buildMiptex("authored"), "textures/authored.mip", &error);
			prepare.directory = QDir(root).filePath("ambiguous");
			const auto rejected = prepareLevelBuildWorkspace(map, PackageStagingArchive(duplicates), prepare);
			expect(!rejected.ready && !QFileInfo::exists(prepare.directory), "duplicate native texture refuses ambiguous WAD");
			auto wadBytes = subset_test::get(QDir(archive.sourcePath()).filePath("textures.wad"));
			qToLittleEndian<quint32>(0xffffffff, wadBytes.data() + 8);
			PackageStagingModel malformed;
			expect(malformed.loadBaseArchive(archive, &error), "malformed fixture");
			malformed.addBytes(wadBytes, "textures.wad", &error, PackageStageConflictResolution::ReplaceExisting);
			prepare.directory = QDir(root).filePath("malformed");
			expect(!prepareLevelBuildWorkspace(map, PackageStagingArchive(malformed), prepare).ready &&
					   !QFileInfo::exists(prepare.directory),
				   "malformed WAD directory has no published workspace");
			PackageReadControl cancel;
			cancel.isCancelled = [] { return true; };
			expect(prepareLevelBuildWorkspace(map, archive, prepare, cancel).cancelled, "cancelled texture capture");
		} else {
			PackageStagingModel animated;
			expect(animated.loadBaseArchive(archive, &error), "WAL fixture");
			auto wal = subset_test::get(QDir(archive.sourcePath()).filePath("textures/authored.wal"));
			wal.replace(56, 7, "missing");
			animated.addBytes(wal, "textures/authored.wal", &error, PackageStageConflictResolution::ReplaceExisting);
			prepare.directory = QDir(root).filePath("missing animation");
			expect(!prepareLevelBuildWorkspace(map, PackageStagingArchive(animated), prepare).ready,
				   "missing WAL animation blocks capture");
			wal[56] = '\0';
			qToLittleEndian<quint32>(0xffffffff, wal.data() + 40);
			animated.addBytes(wal, "textures/authored.wal", &error, PackageStageConflictResolution::ReplaceExisting);
			expect(!prepareLevelBuildWorkspace(map, PackageStagingArchive(animated), prepare).ready,
				   "malformed WAL mip offsets block capture");
		}
	}
	return ok ? 0 : 1;
}
