#include "core/level_build_workspace.h"
#include "core/level_build_artifacts.h"
#include "core/level_document.h"
#include "core/level_quake_assets.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <memory>

namespace vibestudio {
namespace {
constexpr int maximumFiles = 100000;
constexpr quint64 hardByteLimit = 1024ULL * 1024 * 1024 * 1024;
QString text(const char* source) { return QCoreApplication::translate("LevelBuildWorkspace", source); }
QString key(const QString& value) { return value.normalized(QString::NormalizationForm_C).toCaseFolded(); }
bool validName(const QString& value) {
	return QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,64}$")).match(value).hasMatch() &&
		   packageFilesystemPathIssue(value) == PackagePathIssue::None;
}
QString mapRelativePath(const LevelBuildWorkspace& workspace) {
	return workspace.assetPrefix() + QStringLiteral("maps/%1.map").arg(workspace.mapName);
}
bool outputCompanion(const QString& relative, const LevelBuildWorkspace& workspace) {
	return levelBuildArtifactKind(relative, workspace.mapName, workspace.target) != LevelBuildArtifactKind::Unknown;
}
bool safeTreePath(const QString& path, QString* error) {
	QFileInfo at(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
	for (;;) {
		if (at.isSymbolicLink() || at.isJunction()) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Build workspace paths must not traverse links: %1")).arg(at.filePath());
			return false;
		}
		const auto parent = at.dir().absolutePath();
		if (parent == at.absoluteFilePath()) {
			return true;
		}
		at.setFile(parent);
	}
}
bool stopped(const PackageReadControl& control, QString* error) {
	if (!control.isCancelled || !control.isCancelled()) {
		return false;
	}
	*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Build workspace preparation cancelled."));
	return true;
}
bool publishDirectory(const QString& source, const QString& destination, const PackageReadControl& control, QString* error) {
	// Windows can briefly deny a directory rename while another reader holds
	// a child or directory handle. Keep the same private tree and retry briefly;
	// never replace a destination that appeared or follow changed ancestor links.
	for (int attempt = 0; attempt < 6; ++attempt) {
		if (stopped(control, error) || !safeTreePath(source, error) || !safeTreePath(destination, error)) {
			return false;
		}
		if (QFileInfo::exists(destination)) {
			break;
		}
		if (QDir().rename(source, destination)) {
			return true;
		}
#ifdef Q_OS_WIN
		if (attempt < 5) {
			if (control.progress) {
				control.progress(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Waiting to publish the build workspace…")), 0, 0);
			}
			if (stopped(control, error)) {
				return false;
			}
			QThread::msleep(10UL << attempt);
		}
#else
		break;
#endif
	}
	*error = text(
		QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The workspace destination changed or could not be published. Choose a new directory."));
	return false;
}
void progress(const PackageReadControl& control, const QString& phase, quint64 done, quint64 total) {
	if (control.progress) {
		control.progress(phase, static_cast<qint64>(done), static_cast<qint64>(total));
	}
}
bool writeBytes(const QString& path, const QByteArray& bytes, QString* error) {
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Cannot create a build workspace folder."));
		return false;
	}
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		*error = file.errorString();
		return false;
	}
	return true;
}
bool inspectInputPaths(const LevelBuildWorkspace& workspace, QString* error) {
	QSet<QString> paths;
	if (workspace.assetPrefix().isEmpty()) {
		*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Unsupported build target."));
		return false;
	}
	for (const auto& input : workspace.inputs) {
		if (!input.path.startsWith(workspace.assetPrefix()) || !isSafePackageVirtualPath(input.path) ||
			packageFilesystemPathIssue(input.path) != PackagePathIssue::None || paths.contains(key(input.path)) ||
			input.bytes > hardByteLimit || input.sha256.size() != 32) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The build input inventory has an invalid or repeated path or hash."));
			return false;
		}
		paths.insert(key(input.path));
	}
	for (const auto& input : workspace.inputs) {
		QString parent = packageVirtualPathParent(input.path);
		while (!parent.isEmpty()) {
			if (paths.contains(key(parent))) {
				*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "A build input is both a file and a folder."));
				return false;
			}
			parent = packageVirtualPathParent(parent);
		}
	}
	if (!paths.contains(key(mapRelativePath(workspace)))) {
		*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The build inventory does not contain its source map."));
		return false;
	}
	if (!workspace.textureWadPath().isEmpty() && !paths.contains(key(workspace.textureWadPath()))) {
		*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The Quake build inventory has no captured texture WAD."));
		return false;
	}
	return true;
}
} // namespace

QString levelBuildTargetForDocument(const LevelMapDocument& document) {
	if (document.format == LevelMapFormat::Quake3Map) {
		return QStringLiteral("quake3");
	}
	if (document.format != LevelMapFormat::QuakeMap) {
		return {};
	}
	return document.originalText.section('\n', 0, 0).trimmed() == QString::fromLatin1(kQuake2MapTargetHeader).trimmed()
			   ? QStringLiteral("quake2")
			   : QStringLiteral("quake");
}

QString LevelBuildWorkspace::inputPath() const { return QDir(directory).filePath(mapRelativePath(*this)); }
QString LevelBuildWorkspace::assetPrefix() const {
	if (target == QStringLiteral("quake")) {
		return QStringLiteral("game/id1/");
	}
	if (target == QStringLiteral("quake2")) {
		return QStringLiteral("game/baseq2/");
	}
	return target == QStringLiteral("quake3") ? QStringLiteral("game/baseq3/") : QString();
}
QString LevelBuildWorkspace::assetsPath() const { return QDir(directory).filePath(assetPrefix().chopped(1)); }
QString LevelBuildWorkspace::defaultPipeline() const {
	return target == QStringLiteral("quake3") ? QStringLiteral("quake3-full") : QStringLiteral("quake-full");
}
QString LevelBuildWorkspace::packageSuffix() const {
	return target == QStringLiteral("quake3") ? QStringLiteral("pk3") : QStringLiteral("pak");
}
QString LevelBuildWorkspace::textureWadPath() const {
	return target == QStringLiteral("quake") ? assetPrefix() + QStringLiteral("maps/%1.wad").arg(mapName) : QString();
}
bool LevelBuildWorkspace::supportsPipeline(const QString& pipeline) const {
	return !assetPrefix().isEmpty() && (target == QStringLiteral("quake3") ? QStringList{"quake3-full", "quake3-bsp-only"}
																		   : QStringList{"quake-full", "quake-fast", "quake-bsp-only"})
										   .contains(pipeline);
}
QString LevelBuildWorkspace::manifestPath() const { return QDir(directory).filePath(QStringLiteral("build-inputs.json")); }

QJsonObject levelBuildWorkspaceJson(const LevelBuildWorkspace& workspace) {
	QJsonArray inputs;
	for (const auto& input : workspace.inputs) {
		inputs << QJsonObject{
			{"path", input.path}, {"bytes", static_cast<qint64>(input.bytes)}, {"sha256", QString::fromLatin1(input.sha256.toHex())}};
	}
	return {{"schemaVersion", workspace.target == QStringLiteral("quake3") ? 1 : 2},
			{"kind", "vibestudio-level-build"},
			{"target", workspace.target},
			{"mapName", workspace.mapName},
			{"sourceMap", workspace.sourceMapPath},
			{"sourcePackage", workspace.sourcePackagePath},
			{"sourceRevision", QString::number(workspace.sourceRevision)},
			{"sourceContentHash", QString::fromLatin1(workspace.sourceContentHash.toHex())},
			{"inputs", inputs},
			{"dependencies", levelDependencyReportJson(workspace.dependencies)},
			{"omittedPaths", QJsonArray::fromStringList(workspace.omittedPaths)},
			{"warnings", QJsonArray::fromStringList(workspace.warnings)}};
}

LevelBuildWorkspace prepareLevelBuildWorkspace(const LevelMapDocument& document, const PackageArchiveReader& archive,
											   const LevelBuildWorkspaceRequest& request, const PackageReadControl& control) {
	LevelBuildWorkspace result;
	result.directory = QDir::cleanPath(QFileInfo(request.directory).absoluteFilePath());
	result.mapName = request.mapName;
	result.target = request.target.isEmpty() ? levelBuildTargetForDocument(document) : request.target;
	result.dryRun = request.dryRun;
	result.sourceMapPath = document.sourcePath;
	result.sourcePackagePath = archive.sourcePath();
	result.sourceRevision = document.revision;
	result.sourceContentHash = document.sourceContentHash;
	const auto fail = [&](const QString& message) {
		result.error = message;
		result.cancelled = control.isCancelled && control.isCancelled();
		return result;
	};
	if (result.assetPrefix().isEmpty() || (document.format == LevelMapFormat::Quake3Map ? result.target != QStringLiteral("quake3")
																						: document.format != LevelMapFormat::QuakeMap ||
																							  result.target == QStringLiteral("quake3"))) {
		return fail(text(
			QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Choose Quake or Quake II for a classic MAP, or Quake III for a Quake III MAP.")));
	}
	if (request.directory.trimmed().isEmpty() || !validName(request.mapName) || request.maximumBytes == 0 ||
		request.maximumBytes > hardByteLimit) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace",
										   "Choose a new workspace directory, a portable map name and a positive asset byte limit.")));
	}
	if (!safeTreePath(result.directory, &result.error) || stopped(control, &result.error)) {
		return fail(result.error);
	}
	if (QFileInfo::exists(result.directory) || archive.protectsInputPath(result.directory)) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace",
										   "The workspace must be a new directory outside the source asset folder or package draft.")));
	}
	if (!archive.isOpen() || (archive.format() == PackageArchiveFormat::Wad && result.target != QStringLiteral("quake"))) {
		return fail(text(
			QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Open a folder, PAK, ZIP, PK3 or compatible package draft for the build assets.")));
	}
	progress(control, text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Inspecting map dependencies…")), 0, 0);
	result.dependencies = inspectLevelDependencies(
		document, archive,
		[&](int done, int total) {
			progress(control, text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Inspecting map dependencies…")), done, total);
			return !stopped(control, &result.error);
		},
		result.target);
	if (result.dependencies.cancelled || stopped(control, &result.error)) {
		return fail(result.error);
	}
	if (!result.dependencies.complete || result.dependencies.problemCount != 0) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace",
										   "Resolve missing, ambiguous or unreadable level dependencies before preparing the build.")));
	}
	result.warnings = result.dependencies.warnings;
	for (const auto& dependency : result.dependencies.resolvedPaths) {
		if (outputCompanion(result.assetPrefix() + dependency, result)) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace",
											   "The map references a reserved compiler output. Use the original source asset: %1"))
							.arg(dependency));
		}
	}
	progress(control, text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Serializing the current map…")), 0, 0);
	auto captured = document;
	QByteArray textureWad;
	if (result.target != QStringLiteral("quake3")) {
		for (const auto& entity : document.entities) {
			for (const auto& property : entity.properties) {
				if (property.key == QStringLiteral("_external_map") && !property.value.isEmpty()) {
					return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace",
													   "Merge external map prefabs into the document before preparing a build.")));
				}
			}
		}
	}
	if (result.target == QStringLiteral("quake")) {
		const auto textures = inspectLevelQuakeTextures(archive, control, true);
		textureWad = encodeLevelQuakeTextureWad(textures, &result.error, control);
		if (textureWad.isEmpty()) {
			return fail(result.error);
		}
		for (const auto& dependency : result.dependencies.dependencies) {
			if (dependency.kind == QStringLiteral("texture") && dependency.status == LevelDependencyStatus::Resolved &&
				!textures.textures.contains(dependency.reference.toCaseFolded())) {
				return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Quake requires a complete WAD2 miptexture for %1."))
								.arg(dependency.reference));
			}
		}
		const auto world = std::find_if(captured.entities.cbegin(), captured.entities.cend(),
										[](const auto& entity) { return entity.className == QStringLiteral("worldspawn"); });
		if (world == captured.entities.cend()) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The map has no worldspawn entity.")));
		}
		const auto id = world->id;
		for (const auto& property : {QStringLiteral("wad"), QStringLiteral("_wad")}) {
			if (std::count_if(world->properties.cbegin(), world->properties.cend(),
							  [&](const auto& item) { return item.key.compare(property, Qt::CaseInsensitive) == 0; }) > 1) {
				return fail(
					text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Resolve duplicate worldspawn WAD keys before preparing a build.")));
			}
		}
		// Scene locks protect live editing. This private compiler adaptation must
		// work for locked documents too, while retaining their source metadata.
		const auto scene = captured.scene;
		captured.scene = {};
		for (const auto& property : {QStringLiteral("wad"), QStringLiteral("_wad")}) {
			if (!setLevelMapEntityProperty(&captured, id, property, result.mapName + QStringLiteral(".wad"), &result.error)) {
				return fail(result.error);
			}
		}
		captured.scene = scene;
	}
	auto map = serializeLevelMap(captured);
	if (!map.succeeded()) {
		return fail(map.errors.join(QLatin1Char('\n')));
	}
	if (result.target == QStringLiteral("quake2") && !map.bytes.startsWith(kQuake2MapTargetHeader)) {
		map.bytes.prepend(kQuake2MapTargetHeader);
	}
	if (result.target == QStringLiteral("quake") && map.bytes.startsWith(kQuake2MapTargetHeader)) {
		map.bytes.remove(0, qsizetype(qstrlen(kQuake2MapTargetHeader)));
	}
	result.warnings += map.warnings;
	const auto entries = archive.entries();
	if (std::any_of(entries.cbegin(), entries.cend(), [](const auto& entry) {
			return entry.kind == PackageEntryKind::File &&
				   entry.virtualPath.compare(QStringLiteral("scripts/shaderlist.txt"), Qt::CaseInsensitive) == 0;
		})) {
		result.warnings << text(QT_TRANSLATE_NOOP(
			"LevelBuildWorkspace",
			"The compiler honors scripts/shaderlist.txt. Dependency preview scans all shaders; ensure required scripts are listed."));
	}
	QVector<qsizetype> selected;
	QSet<QString> destinations;
	quint64 total = static_cast<quint64>(map.bytes.size() + textureWad.size());
	for (qsizetype at = 0; at < entries.size(); ++at) {
		if (stopped(control, &result.error)) {
			return fail(result.error);
		}
		const auto& entry = entries[at];
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		// Texture WAD lumps can contain '*' (liquids), which cannot be loose
		// Windows filenames. Their bytes are already captured in the generated WAD.
		if (result.target == QStringLiteral("quake") && archive.format() == PackageArchiveFormat::Wad &&
			entry.typeHint == QStringLiteral("wad-texture")) {
			continue;
		}
		const auto relative = result.assetPrefix() + entry.virtualPath;
		if (key(relative) == key(mapRelativePath(result)) || key(relative) == key(result.textureWadPath()) ||
			outputCompanion(relative, result)) {
			result.omittedPaths << entry.virtualPath;
			continue;
		}
		const auto first = entry.virtualPath.section(QLatin1Char('/'), 0, 0).toLower();
		if (first.endsWith(QStringLiteral(".pak")) || first.endsWith(QStringLiteral(".pk3")) || first.endsWith(QStringLiteral(".dpk")) ||
			first.endsWith(QStringLiteral(".pk3dir")) || first.endsWith(QStringLiteral(".dpkdir"))) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Mount nested compiler asset packages before preparing a build: %1"))
							.arg(entry.virtualPath));
		}
		if (!entry.readable || !isSafePackageVirtualPath(entry.virtualPath) ||
			packageFilesystemPathIssue(entry.virtualPath) != PackagePathIssue::None || destinations.contains(key(relative))) {
			return fail(
				text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "A build asset is unreadable, unsafe or has a duplicate output name: %1"))
					.arg(entry.virtualPath));
		}
		if (selected.size() >= maximumFiles || entry.sizeBytes > request.maximumBytes || total > request.maximumBytes - entry.sizeBytes) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The complete asset snapshot exceeds the file or byte limit.")));
		}
		total += entry.sizeBytes;
		selected << at;
		destinations.insert(key(relative));
	}
	if (total > request.maximumBytes) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The current map exceeds the workspace byte limit.")));
	}
	std::sort(selected.begin(), selected.end(),
			  [&](auto left, auto right) { return entries[left].virtualPath < entries[right].virtualPath; });
	// Use the shared extraction planner to reject portable file/folder collisions
	// and protected source paths before any output directory is created.
	if (!selected.isEmpty()) {
		PackageExtractionRequest extraction;
		extraction.targetDirectory = result.assetsPath();
		extraction.dryRun = true;
		extraction.control = control;
		for (auto index : selected) {
			extraction.entrySelections << PackageExtractionSelection{index, {}};
		}
		const auto review = extractPackageEntries(archive, extraction);
		if (!review.succeeded()) {
			return fail(packageExtractionReportText(review));
		}
	}
	std::unique_ptr<QTemporaryDir> temporary;
	QString writeRoot;
	if (!request.dryRun) {
		const auto parent = QFileInfo(result.directory).absolutePath();
		if (!QDir().mkpath(parent) || !safeTreePath(parent, &result.error)) {
			return fail(result.error.isEmpty()
							? text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Cannot create the workspace parent folder."))
							: result.error);
		}
		temporary = std::make_unique<QTemporaryDir>(QDir(parent).filePath(QStringLiteral(".vibestudio-build-XXXXXX")));
		if (!temporary->isValid()) {
			return fail(temporary->errorString());
		}
		writeRoot = temporary->path();
		if (!QDir().mkpath(QDir(writeRoot).filePath(QStringLiteral("home")))) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Cannot create the isolated compiler home folder.")));
		}
	}
	quint64 completed = 0;
	for (auto index : selected) {
		if (stopped(control, &result.error)) {
			return fail(result.error);
		}
		const auto& entry = entries[index];
		const auto relative = result.assetPrefix() + entry.virtualPath;
		std::unique_ptr<QSaveFile> output;
		if (!request.dryRun) {
			const auto path = safePackageOutputPath(writeRoot, relative, &result.error);
			if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
				return fail(result.error);
			}
			output = std::make_unique<QSaveFile>(path);
			output->setDirectWriteFallback(false);
			if (!output->open(QIODevice::WriteOnly)) {
				return fail(output->errorString());
			}
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		quint64 read = 0;
		const bool streamed = archive.streamEntryAt(
			index,
			[&](QByteArrayView bytes) {
				if (stopped(control, &result.error) || static_cast<quint64>(bytes.size()) > entry.sizeBytes - read) {
					return false;
				}
				if (output && output->write(bytes.data(), bytes.size()) != bytes.size()) {
					result.error = output->errorString();
					return false;
				}
				hash.addData(bytes);
				read += bytes.size();
				progress(control, entry.virtualPath, completed + read, total);
				return true;
			},
			&result.error, control.isCancelled);
		if (!streamed || read != entry.sizeBytes || stopped(control, &result.error)) {
			return fail(result.error.isEmpty()
							? text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "An asset changed or could not be copied completely: %1"))
								  .arg(entry.virtualPath)
							: result.error);
		}
		if (output && !output->commit()) {
			return fail(output->errorString());
		}
		result.inputs << LevelBuildInput{relative, read, hash.result()};
		completed += read;
	}
	if (stopped(control, &result.error)) {
		return fail(result.error);
	}
	if (!textureWad.isEmpty()) {
		result.inputs << LevelBuildInput{result.textureWadPath(), quint64(textureWad.size()),
										 QCryptographicHash::hash(textureWad, QCryptographicHash::Sha256)};
		if (!request.dryRun && !writeBytes(QDir(writeRoot).filePath(result.textureWadPath()), textureWad, &result.error)) {
			return fail(result.error);
		}
	}
	result.inputs << LevelBuildInput{mapRelativePath(result), static_cast<quint64>(map.bytes.size()),
									 QCryptographicHash::hash(map.bytes, QCryptographicHash::Sha256)};
	if (!inspectInputPaths(result, &result.error)) {
		return fail(result.error);
	}
	if (!request.dryRun) {
		if (!writeBytes(QDir(writeRoot).filePath(mapRelativePath(result)), map.bytes, &result.error) ||
			!writeBytes(QDir(writeRoot).filePath(QStringLiteral("build-inputs.json")),
						QJsonDocument(levelBuildWorkspaceJson(result)).toJson(), &result.error)) {
			return fail(result.error);
		}
		if (stopped(control, &result.error) || !safeTreePath(result.directory, &result.error)) {
			return fail(result.error);
		}
		if (!publishDirectory(writeRoot, result.directory, control, &result.error)) {
			return fail(result.error);
		}
		temporary->setAutoRemove(false);
	}
	result.ready = true;
	progress(control, text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Build workspace ready.")), total, total);
	return result;
}

LevelBuildWorkspace readLevelBuildWorkspace(const QString& directory, const PackageReadControl& control) {
	LevelBuildWorkspace result;
	result.directory = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
	if (!safeTreePath(result.directory, &result.error) || stopped(control, &result.error)) {
		result.cancelled = !result.error.isEmpty() && control.isCancelled && control.isCancelled();
		return result;
	}
	QFile file(result.manifestPath());
	if (!safeTreePath(file.fileName(), &result.error) || !file.open(QIODevice::ReadOnly) || file.size() > 48 * 1024 * 1024) {
		if (result.error.isEmpty()) {
			result.error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The build input manifest is missing, unreadable or too large."));
		}
		return result;
	}
	QJsonParseError parseError;
	const auto bytes = file.read(48 * 1024 * 1024 + 1);
	if (bytes.size() > 48 * 1024 * 1024 || file.error() != QFileDevice::NoError) {
		result.error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The build input manifest is missing, unreadable or too large."));
		return result;
	}
	const auto json = QJsonDocument::fromJson(bytes, &parseError);
	const auto object = json.object();
	if (parseError.error != QJsonParseError::NoError ||
		(object.value("schemaVersion").toInt() != 1 && object.value("schemaVersion").toInt() != 2) ||
		object.value("kind").toString() != QStringLiteral("vibestudio-level-build") ||
		!QStringList{"quake", "quake2", "quake3"}.contains(object.value("target").toString()) ||
		(object.value("schemaVersion").toInt() == 1 && object.value("target").toString() != QStringLiteral("quake3")) ||
		!validName(object.value("mapName").toString()) || !object.value("inputs").isArray()) {
		result.error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The build input manifest has an unsupported schema or target."));
		return result;
	}
	result.target = object.value("target").toString();
	result.mapName = object.value("mapName").toString();
	result.sourceMapPath = object.value("sourceMap").toString();
	result.sourcePackagePath = object.value("sourcePackage").toString();
	result.sourceRevision = object.value("sourceRevision").toString().toULongLong();
	result.sourceContentHash = QByteArray::fromHex(object.value("sourceContentHash").toString().toLatin1());
	const auto inputs = object.value("inputs").toArray();
	if (inputs.isEmpty() || inputs.size() > maximumFiles + 2) {
		result.error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The build input inventory exceeds its supported size."));
		return result;
	}
	quint64 total = 0;
	for (const auto& value : inputs) {
		const auto item = value.toObject();
		const double bytes = item.value("bytes").toDouble(-1);
		const auto digest = item.value("sha256").toString();
		if (!std::isfinite(bytes) || bytes < 0 || bytes > hardByteLimit || bytes != std::floor(bytes) ||
			!QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(digest).hasMatch() ||
			total > hardByteLimit - static_cast<quint64>(bytes)) {
			result.error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "The build input inventory contains invalid sizes or hashes."));
			return result;
		}
		total += static_cast<quint64>(bytes);
		result.inputs << LevelBuildInput{item.value("path").toString(), static_cast<quint64>(bytes),
										 QByteArray::fromHex(digest.toLatin1())};
	}
	if (!inspectInputPaths(result, &result.error)) {
		return result;
	}
	for (const auto& warning : object.value("warnings").toArray()) {
		result.warnings << warning.toString();
	}
	result.ready = true;
	return result;
}

bool verifyLevelBuildWorkspace(const LevelBuildWorkspace& workspace, QString* error, const PackageReadControl& control) {
	QString local;
	if (!error) {
		error = &local;
	}
	error->clear();
	if (!workspace.ready || workspace.assetPrefix().isEmpty() || workspace.dryRun || !validName(workspace.mapName) ||
		!safeTreePath(workspace.directory, error) || !inspectInputPaths(workspace, error)) {
		if (error->isEmpty()) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Prepare a complete build workspace first."));
		}
		return false;
	}
	QSet<QString> expected;
	for (const auto& input : workspace.inputs) {
		if (outputCompanion(input.path, workspace)) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "A compiler output cannot also be a captured source input."));
			return false;
		}
		expected.insert(input.path);
	}
	for (const auto& folder : {workspace.assetsPath(), QDir(workspace.directory).filePath(QStringLiteral("home"))}) {
		if (!safeTreePath(folder, error) || !QFileInfo(folder).isDir()) {
			if (error->isEmpty()) {
				*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "A prepared compiler asset folder is missing."));
			}
			return false;
		}
		QDirIterator iterator(folder, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while (iterator.hasNext()) {
			const auto path = iterator.next();
			if (stopped(control, error) || !safeTreePath(path, error)) {
				return false;
			}
			const auto relative = QDir(workspace.directory).relativeFilePath(path);
			if (iterator.fileInfo().isFile() && !expected.contains(relative) && !outputCompanion(relative, workspace)) {
				*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Unrecorded compiler input found. Prepare a fresh workspace: %1"))
							 .arg(relative);
				return false;
			}
		}
	}
	qint64 completed = 0;
	for (const auto& input : workspace.inputs) {
		if (stopped(control, error)) {
			return false;
		}
		const auto path = QDir(workspace.directory).filePath(input.path);
		if (!safeTreePath(path, error)) {
			return false;
		}
		QFile file(path);
		QCryptographicHash hash(QCryptographicHash::Sha256);
		if (!file.open(QIODevice::ReadOnly) || static_cast<quint64>(file.size()) != input.bytes) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "A prepared build input is missing or changed: %1")).arg(input.path);
			return false;
		}
		while (!file.atEnd()) {
			if (stopped(control, error)) {
				return false;
			}
			const auto bytes = file.read(256 * 1024);
			if (bytes.isEmpty() && file.error() != QFileDevice::NoError) {
				*error = file.errorString();
				return false;
			}
			hash.addData(bytes);
		}
		if (hash.result() != input.sha256) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "A prepared build input failed its content hash: %1")).arg(input.path);
			return false;
		}
		progress(control, input.path, ++completed, workspace.inputs.size());
	}
	return !stopped(control, error);
}

bool configureLevelBuildPipeline(const LevelBuildWorkspace& workspace, BuildPipelineRequest* request, QString* error) {
	QString local;
	if (!error) {
		error = &local;
	}
	error->clear();
	if (!request || !workspace.ready || !validName(workspace.mapName) || !workspace.supportsPipeline(request->pipelineId)) {
		*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Select a pipeline compatible with the prepared target."));
		return false;
	}
	auto configured = *request;
	BuildPipelineDescriptor pipeline;
	buildPipelineForId(request->pipelineId, &pipeline);
	// Public ericw-tools common settings and qbsp's WAD/target flags, pinned
	// f80b1e216a415581aea7475cb52b16b8c4859084; see docs/CREDITS.md.
	// Restrict custom switches to scalar compiler settings. Input/output paths,
	// response files, conversion modes and alternate targets are not accepted.
	const QSet<QString> scalarFlags{"threads",
									"subdivide",
									"maxedges",
									"scale",
									"bounce",
									"bouncescale",
									"light",
									"gate",
									"gamma",
									"anglescale",
									"dirtdepth",
									"dirtangle",
									"dirtscale",
									"minlight",
									"range",
									"dist",
									"soft",
									"extra",
									"extra4",
									"phong",
									"lux",
									"lit",
									"fast",
									"verbose",
									"quiet",
									"nocolor",
									"nopercent",
									"noprogress",
									"nostat",
									"leaktest",
									"litwater",
									"transwater",
									"notranswater",
									"nosubdivide",
									"noclip",
									"notex",
									"omitdetail",
									"omitdetailwall",
									"omitdetailillusionary",
									"omitdetailfence",
									"dirt",
									"bsp2",
									"2psb",
									"bspxlit",
									"bspxlux",
									"noambient",
									"lowpriority"};
	const QSet<QString> requiredValues{"threads",	 "subdivide", "maxedges",  "scale",		"bouncescale", "light", "gate", "gamma",
									   "anglescale", "dirtdepth", "dirtangle", "dirtscale", "minlight",	   "range", "dist"};
	const QSet<QString> optionalValues{"soft", "bounce"};
	const QSet<QString> optionalBooleans{"phong", "litwater", "transwater", "notranswater", "dirt", "lowpriority"};
	const QRegularExpression numericValue(QStringLiteral("^-?[0-9]+(?:\\.[0-9]+)?$"));
	QSet<QString> stageIds;
	for (const auto& stage : pipeline.stages) {
		stageIds.insert(stage.id);
	}
	for (auto it = configured.stageExtraArguments.cbegin(); it != configured.stageExtraArguments.cend(); ++it) {
		if (!stageIds.contains(it.key())) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Unknown prepared build stage: %1")).arg(it.key());
			return false;
		}
	}
	for (const auto& stage : pipeline.stages) {
		QStringList paths;
		if (workspace.target == QStringLiteral("quake3")) {
			paths = {"-game",		 "quake3",
					 "-fs_basepath", QDir(workspace.directory).filePath("game"),
					 "-fs_homepath", QDir(workspace.directory).filePath("home"),
					 "-fs_game",	 "baseq3",
					 "-fs_basegame", "baseq3"};
		} else {
			paths = {"-nodefaultpaths", "-path", workspace.assetsPath(), "-logfile",
					 workspace.inputPath().chopped(4) + QLatin1Char('.') + stage.id + QStringLiteral(".log")};
			if (stage.id == QStringLiteral("qbsp")) {
				paths += QStringList{"-wadpath", QFileInfo(workspace.inputPath()).absolutePath()};
				if (workspace.target == QStringLiteral("quake2")) {
					paths << QStringLiteral("-q2bsp");
				}
			}
		}
		auto& arguments = configured.stageExtraArguments[stage.id];
		if (arguments.size() >= paths.size() && arguments.mid(arguments.size() - paths.size()) == paths) {
			arguments = arguments.mid(0, arguments.size() - paths.size());
		}
		for (qsizetype index = 0; index < arguments.size(); ++index) {
			const auto& arg = arguments[index];
			bool conflict = false;
			if (workspace.target == QStringLiteral("quake3")) {
				conflict = arg.startsWith(QStringLiteral("-fs_"), Qt::CaseInsensitive) ||
						   QStringList{"-game", "-lightmapdir", "-tempname", "-rename"}.contains(arg.toLower());
			} else {
				const auto flag = arg.mid(1).toLower();
				conflict = !arg.startsWith('-') || !scalarFlags.contains(flag);
				if (workspace.target == QStringLiteral("quake2") && (flag == "bsp2" || flag == "2psb")) {
					conflict = true;
				}
				// A bare number is still a positional filename to the compiler. Consume
				// values only with a flag that actually accepts them; never let a missing
				// value swallow the paths supplied below or the captured input map.
				const auto next = arguments.value(index + 1);
				if (!conflict && requiredValues.contains(flag)) {
					conflict = !numericValue.match(next).hasMatch();
					if (!conflict) {
						++index;
					}
				} else if (!conflict && optionalValues.contains(flag) && numericValue.match(next).hasMatch()) {
					++index;
				} else if (!conflict && optionalBooleans.contains(flag) && (next == "0" || next == "1" || next == "-1")) {
					++index;
				}
			}
			if (conflict) {
				*error = text(QT_TRANSLATE_NOOP("LevelBuildWorkspace", "Prepared builds supply their own game, asset and output paths; "
																	   "remove conflicting or unsupported stage argument: %1"))
							 .arg(arg);
				return false;
			}
		}
		arguments += paths;
	}
	configured.inputPath = workspace.inputPath();
	configured.outputPath.clear();
	configured.workingDirectory = workspace.assetsPath();
	configured.manifestDirectory = QDir(workspace.directory).filePath(QStringLiteral("manifests"));
	*request = std::move(configured);
	return true;
}

QString levelBuildWorkspaceText(const LevelBuildWorkspace& workspace) {
	QStringList lines{workspace.error};
	if (workspace.ready) {
		lines << QCoreApplication::translate("LevelBuildWorkspace", "%n recorded compiler input(s)", nullptr,
											 static_cast<int>(workspace.inputs.size()));
		lines << workspace.inputPath() << workspace.manifestPath();
	}
	lines += workspace.warnings;
	lines.removeAll(QString());
	return lines.join(QLatin1Char('\n'));
}
} // namespace vibestudio
