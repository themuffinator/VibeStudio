#include "core/level_build_artifacts.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
constexpr quint64 outputByteLimit = 16ULL * 1024 * 1024 * 1024;
constexpr qsizetype outputFileLimit = 100000;
QString text(const char* source) { return QCoreApplication::translate("LevelBuildArtifacts", source); }
QString key(const QString& path) { return path.normalized(QString::NormalizationForm_C).toCaseFolded(); }
QString recordPath(const LevelBuildWorkspace& workspace) {
	return QDir(workspace.directory).filePath(QStringLiteral("build-outputs.json"));
}
QString lockPath(const LevelBuildWorkspace& workspace) {
	return QDir(workspace.directory).filePath(QStringLiteral(".build-workspace.lock"));
}
bool stopped(const PackageReadControl& control, QString* error) {
	if (!control.isCancelled || !control.isCancelled()) {
		return false;
	}
	*error = text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Build artifact operation cancelled."));
	return true;
}
bool safePath(const QString& path, QString* error) {
	QFileInfo item(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
	for (;;) {
		if (item.isSymbolicLink() || item.isJunction()) {
			*error =
				text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Build artifact paths must not traverse links: %1")).arg(item.filePath());
			return false;
		}
		const auto parent = item.dir().absolutePath();
		if (parent == item.absoluteFilePath()) {
			return true;
		}
		item.setFile(parent);
	}
}
QJsonArray fileArray(const QVector<LevelBuildInput>& files) {
	QJsonArray result;
	for (const auto& file : files) {
		result << QJsonObject{{"path", file.path}, {"bytes", qint64(file.bytes)}, {"sha256", QString::fromLatin1(file.sha256.toHex())}};
	}
	return result;
}
QByteArray inputFingerprint(const LevelBuildWorkspace& workspace) {
	auto inputs = workspace.inputs;
	std::sort(inputs.begin(), inputs.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
	return QCryptographicHash::hash(
		QJsonDocument(QJsonObject{{"mapName", workspace.mapName}, {"inputs", fileArray(inputs)}}).toJson(QJsonDocument::Compact),
		QCryptographicHash::Sha256);
}
bool scanOutputs(const LevelBuildWorkspace& workspace, QVector<LevelBuildInput>* outputs, QString* error, const PackageReadControl& control,
				 bool hash) {
	outputs->clear();
	quint64 total = 0;
	QSet<QString> seen;
	QDirIterator it(workspace.assetsPath(), QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const auto path = it.next();
		if (stopped(control, error) || !safePath(path, error)) {
			return false;
		}
		const auto relative = QDir(workspace.directory).relativeFilePath(path);
		if (levelBuildArtifactKind(relative, workspace.mapName, workspace.target) == LevelBuildArtifactKind::Unknown) {
			continue;
		}
		const auto size = it.fileInfo().size();
		if (!it.fileInfo().isFile() || size < 0 || quint64(size) > outputByteLimit || total > outputByteLimit - quint64(size) ||
			outputs->size() >= outputFileLimit || seen.contains(key(relative))) {
			*error =
				text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Compiler outputs exceed the supported size or contain colliding paths."));
			return false;
		}
		total += quint64(size);
		seen.insert(key(relative));
		LevelBuildInput output{relative, quint64(size), {}};
		if (hash) {
			const auto identity = capturePackageFileIdentity(path, error, control);
			if (!identity || quint64(identity->size) != output.bytes) {
				return false;
			}
			output.sha256 = identity->sha256;
		}
		outputs->append(output);
		if (control.progress) {
			control.progress(relative, outputs->size(), 0);
		}
	}
	std::sort(outputs->begin(), outputs->end(), [](const auto& a, const auto& b) { return a.path < b.path; });
	return !stopped(control, error);
}
bool storeRecord(const LevelBuildWorkspace& workspace, const LevelBuildArtifacts& artifacts, QString* error) {
	const auto path = recordPath(workspace);
	if (!safePath(path, error)) {
		return false;
	}
	auto json = levelBuildArtifactsJson(artifacts);
	json.remove("verified");
	json.remove("recordSha256");
	json.remove("workspaceDirectory");
	const auto bytes = QJsonDocument(json).toJson();
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		*error = file.errorString();
		return false;
	}
	return true;
}
bool acquire(const LevelBuildWorkspace& workspace, QLockFile& lock, QString* error) {
	if (!safePath(lockPath(workspace), error)) {
		return false;
	}
	lock.setStaleLockTime(0);
	if (lock.tryLock(0)) {
		return true;
	}
	*error = text(
		QT_TRANSLATE_NOOP("LevelBuildArtifacts", "This workspace is already being built or published. Wait for that operation to finish."));
	return false;
}
// A full BSP rebuild starts with no previous generated shaders or lightmaps in
// q3map2's search path. Retain outputs as independent history files, never delete
// them or touch the captured source inputs. On a failed move, restore prior moves.
bool retainPreviousOutputs(const LevelBuildWorkspace& workspace, const QString& run, QStringList* warnings, QString* error,
						   const PackageReadControl& control) {
	QVector<LevelBuildInput> previous;
	if (!scanOutputs(workspace, &previous, error, control, false)) {
		return false;
	}
	if (previous.isEmpty()) {
		return true;
	}
	const auto root = QDir(workspace.directory).filePath(QStringLiteral("history/%1").arg(run));
	if (!safePath(root, error)) {
		return false;
	}
	if (QFileInfo::exists(root)) {
		*error = text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The output history directory already exists: %1")).arg(root);
		return false;
	}
	QVector<QPair<QString, QString>> moved;
	for (const auto& file : previous) {
		const auto source = QDir(workspace.directory).filePath(file.path), target = QDir(root).filePath(file.path);
		if (stopped(control, error) || !safePath(source, error) || !safePath(target, error) ||
			!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::rename(source, target)) {
			for (auto it = moved.crbegin(); it != moved.crend(); ++it) {
				if (!QFile::rename(it->second, it->first)) {
					warnings->append(
						text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "A previous output remains in history: %1")).arg(it->second));
				}
			}
			if (error->isEmpty()) {
				*error = text(QT_TRANSLATE_NOOP("LevelBuildArtifacts",
												"Could not retain the previous compiler outputs. Inspect the workspace history."));
			}
			return false;
		}
		moved.append({source, target});
	}
	if (QFileInfo::exists(recordPath(workspace))) {
		QString copyError;
		if (!safePath(recordPath(workspace), &copyError) ||
			!QFile::copy(recordPath(workspace), QDir(root).filePath(QStringLiteral("build-outputs.json")))) {
			warnings->append(text(QT_TRANSLATE_NOOP(
				"LevelBuildArtifacts", "Previous outputs were retained, but their previous output record could not be copied.")));
		}
	}
	warnings->append(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Previous compiler outputs retained in %1.")).arg(root));
	return true;
}
} // namespace

LevelBuildArtifactKind levelBuildArtifactKind(const QString& path, const QString& mapName, const QString& target) {
	if (!isSafePackageVirtualPath(path) || packageFilesystemPathIssue(path) != PackagePathIssue::None) {
		return LevelBuildArtifactKind::Unknown;
	}
	const auto normalized = path.toLower(), name = mapName.toLower();
	const auto stem = QStringLiteral("game/%1/maps/%2")
						  .arg(target == QStringLiteral("quake")	? QStringLiteral("id1")
							   : target == QStringLiteral("quake2") ? QStringLiteral("baseq2")
																	: QStringLiteral("baseq3"),
							   name);
	if (normalized == stem + QStringLiteral(".bsp")) {
		return LevelBuildArtifactKind::CompiledMap;
	}
	// q3map2 shaders.cpp and lightmaps_ydnar.cpp define these output names;
	// q3map2.h defines EXTERNAL_LIGHTMAP as lm_%04d.tga. See compiler credits.
	if (target == QStringLiteral("quake3") && normalized == QStringLiteral("game/baseq3/scripts/q3map2_%1.shader").arg(name)) {
		return LevelBuildArtifactKind::GeneratedShader;
	}
	if (target == QStringLiteral("quake3") && normalized.startsWith(stem + QLatin1Char('/')) &&
		QRegularExpression(QStringLiteral("^lm_[0-9]{4,8}\\.tga$")).match(normalized.mid(stem.size() + 1)).hasMatch()) {
		return LevelBuildArtifactKind::ExternalLightmap;
	}
	// ericw-tools light/write.cc, qbsp/writebsp.cc and vis/vis.cc define
	// these runtime and diagnostic companions; see the pinned compiler credits.
	if (target == QStringLiteral("quake") && (normalized == stem + QStringLiteral(".lit") || normalized == stem + QStringLiteral(".lux"))) {
		return LevelBuildArtifactKind::ExternalLighting;
	}
	QStringList diagnostics{"prt", "lin", "pts", "srf", "log", "lit", "lux"};
	if (target != QStringLiteral("quake3")) {
		diagnostics += QStringList{"texinfo", "texinfo.json", "content.json", "leak.prt", "qbsp.log", "vis.log", "light.log", "vis", "vi0"};
	}
	if (normalized.startsWith(stem + QLatin1Char('.')) && diagnostics.contains(normalized.mid(stem.size() + 1))) {
		return LevelBuildArtifactKind::Diagnostic;
	}
	return LevelBuildArtifactKind::Unknown;
}
QString levelBuildArtifactKindId(LevelBuildArtifactKind kind) {
	switch (kind) {
	case LevelBuildArtifactKind::CompiledMap:
		return QStringLiteral("compiled-map");
	case LevelBuildArtifactKind::GeneratedShader:
		return QStringLiteral("generated-shader");
	case LevelBuildArtifactKind::ExternalLightmap:
		return QStringLiteral("external-lightmap");
	case LevelBuildArtifactKind::ExternalLighting:
		return QStringLiteral("external-lighting");
	case LevelBuildArtifactKind::Diagnostic:
		return QStringLiteral("diagnostic");
	default:
		return QStringLiteral("unknown");
	}
}
QJsonObject levelBuildArtifactsJson(const LevelBuildArtifacts& artifacts) {
	auto outputs = fileArray(artifacts.outputs);
	for (qsizetype at = 0; at < outputs.size(); ++at) {
		auto output = outputs[at].toObject();
		output.insert("role",
					  levelBuildArtifactKindId(levelBuildArtifactKind(artifacts.outputs[at].path, artifacts.mapName, artifacts.target)));
		outputs[at] = output;
	}
	return {{"schemaVersion", 1},
			{"kind", "vibestudio-level-outputs"},
			{"workspaceDirectory", artifacts.workspaceDirectory},
			{"target", artifacts.target},
			{"runId", artifacts.runId},
			{"pipelineId", artifacts.pipelineId},
			{"state", artifacts.state},
			{"inputSha256", QString::fromLatin1(artifacts.inputSha256.toHex())},
			{"recordSha256", QString::fromLatin1(artifacts.recordSha256.toHex())},
			{"outputs", outputs},
			{"warnings", QJsonArray::fromStringList(artifacts.warnings)},
			{"error", artifacts.error},
			{"verified", artifacts.verified}};
}
LevelBuildArtifacts inspectLevelBuildArtifacts(const LevelBuildWorkspace& workspace, const PackageReadControl& control) {
	LevelBuildArtifacts result;
	result.workspaceDirectory = workspace.directory;
	result.mapName = workspace.mapName;
	result.target = workspace.target;
	const auto fail = [&](const QString& error) {
		result.error = error;
		result.cancelled = control.isCancelled && control.isCancelled();
		return result;
	};
	const auto path = recordPath(workspace);
	QFile file(path);
	if (!safePath(path, &result.error) || !file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) {
		return fail(
			text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "A successful output record is unavailable. Run a full prepared build first.")));
	}
	const auto bytes = file.read(32 * 1024 * 1024 + 1);
	if (bytes.size() > 32 * 1024 * 1024 || file.error() != QFile::NoError) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The compiler output record could not be read within its size limit.")));
	}
	QJsonParseError parse;
	const auto json = QJsonDocument::fromJson(bytes, &parse).object();
	if (parse.error != QJsonParseError::NoError || json.value("schemaVersion").toInt() != 1 ||
		json.value("kind").toString() != "vibestudio-level-outputs" || json.value("state").toString() != "completed" ||
		!json.value("outputs").isArray()) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts",
										   "The last build is incomplete or its output record is invalid. Run a full prepared build.")));
	}
	result.runId = json.value("runId").toString();
	result.pipelineId = json.value("pipelineId").toString();
	result.state = json.value("state").toString();
	if (QUuid(result.runId).isNull() || !workspace.supportsPipeline(result.pipelineId) ||
		json.value("target").toString(QStringLiteral("quake3")) != workspace.target) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The output record has an invalid build or pipeline identifier.")));
	}
	const auto fingerprint = json.value("inputSha256").toString();
	if (!QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(fingerprint).hasMatch() ||
		QByteArray::fromHex(fingerprint.toLatin1()) != inputFingerprint(workspace)) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The compiler outputs belong to different captured inputs.")));
	}
	result.inputSha256 = QByteArray::fromHex(fingerprint.toLatin1());
	result.recordSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	const auto records = json.value("outputs").toArray();
	QSet<QString> seen;
	quint64 total = 0;
	bool hasMap = false;
	if (records.isEmpty() || records.size() > outputFileLimit) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The output inventory is empty or exceeds its file limit.")));
	}
	for (const auto& record : records) {
		if (stopped(control, &result.error)) {
			return fail(result.error);
		}
		const auto entry = record.toObject();
		const auto relative = entry.value("path").toString(), digest = entry.value("sha256").toString();
		const auto kind = levelBuildArtifactKind(relative, workspace.mapName, workspace.target);
		const double size = entry.value("bytes").toDouble(-1);
		if (kind == LevelBuildArtifactKind::Unknown || seen.contains(key(relative)) || !std::isfinite(size) || size < 0 ||
			size != std::floor(size) || size > outputByteLimit || total > outputByteLimit - quint64(size) ||
			!QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(digest).hasMatch()) {
			return fail(
				text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The output inventory contains an unsafe, repeated or invalid record.")));
		}
		hasMap |= kind == LevelBuildArtifactKind::CompiledMap;
		total += quint64(size);
		seen.insert(key(relative));
		result.outputs.append({relative, quint64(size), QByteArray::fromHex(digest.toLatin1())});
	}
	if (!hasMap) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The output inventory has no compiled map.")));
	}
	if (!verifyLevelBuildWorkspace(workspace, &result.error, control)) {
		return fail(result.error);
	}
	QVector<LevelBuildInput> actual;
	if (!scanOutputs(workspace, &actual, &result.error, control, true)) {
		return fail(result.error);
	}
	std::sort(result.outputs.begin(), result.outputs.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
	if (actual.size() != result.outputs.size()) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Compiler outputs were added or removed after the build.")));
	}
	for (qsizetype at = 0; at < actual.size(); ++at) {
		if (actual[at].path != result.outputs[at].path || actual[at].bytes != result.outputs[at].bytes ||
			actual[at].sha256 != result.outputs[at].sha256) {
			return fail(
				text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "A compiler output changed after the build: %1")).arg(actual[at].path));
		}
	}
	for (const auto& warning : json.value("warnings").toArray()) {
		result.warnings << warning.toString();
	}
	result.verified = true;
	return result;
}
QString levelBuildArtifactsText(const LevelBuildArtifacts& artifacts) {
	if (!artifacts.error.isEmpty()) {
		return artifacts.error;
	}
	return QCoreApplication::translate("LevelBuildArtifacts", "%n verified compiler output(s)", nullptr, int(artifacts.outputs.size())) +
		   QLatin1Char('\n') + artifacts.runId + QLatin1Char('\n') + artifacts.warnings.join(QLatin1Char('\n'));
}

BuildPipelineResult runLevelBuildWorkspace(const LevelBuildWorkspace& workspace, BuildPipelineRequest request,
										   const BuildPipelineCallbacks& callbacks) {
	BuildPipelineResult result;
	result.inputPath = workspace.inputPath();
	result.state = OperationState::Failed;
	result.dryRun = request.dryRun;
	buildPipelineForId(request.pipelineId, &result.pipeline);
	PackageReadControl control;
	control.isCancelled = callbacks.cancellationRequested;
	const auto log = [&](const QString& message) {
		if (callbacks.logEntry) {
			callbacks.logEntry({QDateTime::currentDateTimeUtc(), "running", message});
		}
	};
	QElapsedTimer pulse;
	pulse.start();
	control.progress = [&](const QString& path, qint64, qint64) {
		if (pulse.elapsed() >= 1000) {
			log(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Verifying build content: %1")).arg(path));
			pulse.restart();
		}
	};
	const auto fail = [&](const QString& error) {
		result.cancelled = control.isCancelled && control.isCancelled();
		result.state = result.cancelled ? OperationState::Cancelled : OperationState::Failed;
		result.errors << error;
		result.registeredOutputPaths.clear();
		return result;
	};
	QString error;
	QLockFile lock(lockPath(workspace));
	if (!configureLevelBuildPipeline(workspace, &request, &error) || (!request.dryRun && !acquire(workspace, lock, &error))) {
		return fail(error);
	}
	log(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Verifying the prepared map and asset inputs…")));
	if (!verifyLevelBuildWorkspace(workspace, &error, control)) {
		return fail(error);
	}
	const bool full =
		!request.disabledStageIds.contains(workspace.target == QStringLiteral("quake3") ? QStringLiteral("bsp") : QStringLiteral("qbsp"));
	if (!full) {
		const auto previous = inspectLevelBuildArtifacts(workspace, control);
		if (!previous.verified) {
			return fail(previous.error);
		}
	}
	if (request.dryRun) {
		result = runBuildPipeline(request, callbacks);
		log(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Checking that compiler inputs remained unchanged…")));
		if (!verifyLevelBuildWorkspace(workspace, &error, control)) {
			return fail(error);
		}
		return result;
	}
	LevelBuildArtifacts record;
	record.workspaceDirectory = workspace.directory;
	record.mapName = workspace.mapName;
	record.target = workspace.target;
	record.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	record.pipelineId = request.pipelineId;
	record.inputSha256 = inputFingerprint(workspace);
	record.state = QStringLiteral("running");
	if (full && !retainPreviousOutputs(workspace, record.runId, &record.warnings, &error, control)) {
		return fail(error);
	}
	if (!storeRecord(workspace, record, &error)) {
		return fail(error);
	}
	result = runBuildPipeline(request, callbacks);
	result.warnings += record.warnings;
	if (!result.cancelled) {
		log(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Checking that compiler inputs remained unchanged…")));
		if (!verifyLevelBuildWorkspace(workspace, &error, control)) {
			result = fail(error);
		}
	}
	if (result.succeeded()) {
		log(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Recording compiler outputs…")));
		if (!scanOutputs(workspace, &record.outputs, &error, control, true)) {
			result = fail(error);
		} else if (std::none_of(record.outputs.cbegin(), record.outputs.cend(), [&](const auto& output) {
					   return levelBuildArtifactKind(output.path, workspace.mapName, workspace.target) ==
							  LevelBuildArtifactKind::CompiledMap;
				   })) {
			result = fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The build produced no compiled map.")));
		}
	}
	record.state = result.succeeded() ? QStringLiteral("completed")
				   : result.cancelled ? QStringLiteral("cancelled")
									  : QStringLiteral("failed");
	record.error = result.errors.join(QLatin1Char('\n'));
	record.warnings = result.warnings;
	if (!storeRecord(workspace, record, &error)) {
		return fail(error);
	}
	if (!result.succeeded()) {
		result.registeredOutputPaths.clear();
	} else if (request.registerOutputs) {
		for (const auto& output : record.outputs) {
			result.registeredOutputPaths << QDir(workspace.directory).filePath(output.path);
		}
		result.registeredOutputPaths.removeDuplicates();
	}
	return result;
}

LevelBuildPackageResult publishLevelBuildPackage(const LevelBuildWorkspace& workspace, const LevelBuildPackageRequest& request,
												 const PackageReadControl& control) {
	LevelBuildPackageResult result;
	const auto fail = [&](const QString& error) {
		result.error = error;
		result.cancelled = control.isCancelled && control.isCancelled();
		return result;
	};
	QString error;
	const auto output = QFileInfo(request.outputPath).absoluteFilePath();
	if (request.outputPath.trimmed().isEmpty() || QFileInfo(output).suffix().compare(workspace.packageSuffix(), Qt::CaseInsensitive) != 0) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Choose an output path matching the prepared package format: %1."))
						.arg(workspace.packageSuffix()));
	}
	for (const auto& candidate : QStringList{output, output + QStringLiteral(".bak")}) {
		if (!safePath(candidate, &error) || packagePathIsInsideDirectory(workspace.directory, candidate) ||
			(!workspace.sourcePackagePath.isEmpty() && packagePathIsInsideDirectory(workspace.sourcePackagePath, candidate)) ||
			(!workspace.sourceMapPath.isEmpty() &&
			 QFileInfo(candidate).canonicalFilePath() == QFileInfo(workspace.sourceMapPath).canonicalFilePath() &&
			 QFileInfo::exists(candidate))) {
			return fail(error.isEmpty() ? text(QT_TRANSLATE_NOOP("LevelBuildArtifacts",
																 "Publish outside the build workspace and original source assets."))
										: error);
		}
	}
	QLockFile lock(lockPath(workspace));
	if (!request.dryRun && !acquire(workspace, lock, &error)) {
		return fail(error);
	}
	result.artifacts = inspectLevelBuildArtifacts(workspace, control);
	if (!result.artifacts.verified) {
		return fail(result.artifacts.error);
	}
	if (!request.expectedRecordSha256.isEmpty() && request.expectedRecordSha256 != result.artifacts.recordSha256) {
		return fail(text(
			QT_TRANSLATE_NOOP("LevelBuildArtifacts", "The build changed since review. Review the current outputs before publishing.")));
	}
	QVector<LevelBuildInput> payload;
	for (const auto& input : workspace.inputs) {
		if (request.includeSourceMap ||
			(QDir(workspace.directory).filePath(input.path) != workspace.inputPath() && input.path != workspace.textureWadPath())) {
			payload << input;
		}
	}
	for (const auto& artifact : result.artifacts.outputs) {
		if (levelBuildArtifactKind(artifact.path, workspace.mapName, workspace.target) != LevelBuildArtifactKind::Diagnostic) {
			payload << artifact;
		}
	}
	PackageArchive archive;
	if (!archive.load(workspace.assetsPath(), &error, control)) {
		return fail(error);
	}
	QVector<qsizetype> indexes;
	const auto entries = archive.entries();
	QHash<QString, qsizetype> byPath;
	for (qsizetype at = 0; at < entries.size(); ++at) {
		if (entries[at].kind == PackageEntryKind::File) {
			byPath.insert(entries[at].virtualPath, at);
		}
	}
	for (const auto& file : payload) {
		if (stopped(control, &error)) {
			return fail(error);
		}
		const auto virtualPath = file.path.mid(workspace.assetPrefix().size());
		const auto identity = archive.fileIdentity(virtualPath);
		if (!byPath.contains(virtualPath) || !identity || quint64(identity->size) != file.bytes || identity->sha256 != file.sha256) {
			return fail(
				text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "A package input changed while preparing publication: %1")).arg(virtualPath));
		}
		indexes << byPath.value(virtualPath);
		result.paths << virtualPath;
	}
	PackageStagingModel staging;
	if (!staging.loadBaseArchiveSubsetAt(archive, indexes, &error, nullptr, control)) {
		return fail(error);
	}
	PackageWriteRequest write;
	write.format = workspace.target == QStringLiteral("quake3") ? PackageArchiveFormat::Pk3 : PackageArchiveFormat::Pak;
	write.destinationPath = output;
	write.allowOverwrite = request.allowOverwrite;
	write.dryRun = request.dryRun;
	write.compression = request.compression;
	write.expectedDestinationSha256 = request.expectedDestinationSha256;
	write.isCancelled = control.isCancelled;
	write.byteProgress = [&](PackageWritePhase, const QString& path, quint64 done, quint64 total) {
		if (control.progress) {
			control.progress(text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Writing package: %1")).arg(path), qint64(done), qint64(total));
		}
	};
	result.write = staging.writeArchive(write);
	result.cancelled = result.write.cancelled;
	if (!result.write.succeeded()) {
		result.error = result.write.blockedMessages.join(QLatin1Char('\n'));
		if (result.error.isEmpty()) {
			result.error = result.cancelled
							   ? text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Package publication cancelled."))
							   : text(QT_TRANSLATE_NOOP("LevelBuildArtifacts", "Package publication failed. Inspect the write report."));
		}
	}
	return result;
}
QJsonObject levelBuildPackageJson(const LevelBuildPackageResult& result) {
	return {{"artifacts", levelBuildArtifactsJson(result.artifacts)},
			{"paths", QJsonArray::fromStringList(result.paths)},
			{"outputPath", result.write.outputPath},
			{"backupPath", result.write.backupPath},
			{"sha256", result.write.sha256},
			{"bytesWritten", qint64(result.write.bytesWritten)},
			{"entryCount", result.write.entryCount},
			{"dryRun", result.write.dryRun},
			{"committed", result.write.outputCommitted},
			{"deterministic", result.write.deterministic},
			{"cancelled", result.cancelled},
			{"succeeded", result.succeeded()},
			{"error", result.error},
			{"warnings", QJsonArray::fromStringList(result.write.warnings)}};
}
} // namespace vibestudio
