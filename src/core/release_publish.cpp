#include "core/release_publish.h"

#include "core/game_installation.h"
#include "core/package_publication.h"
#include "core/package_staging.h"
#include "core/project_content.h"
#include "core/studio_manifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QtEndian>

#include <cstring>
#include <limits>

namespace vibestudio {

namespace {

constexpr qint64 kMaximumWadBytes = 1024ll * 1024 * 1024;
constexpr qint64 kMaximumLooseBytes = 512ll * 1024 * 1024;

QString sha256Hex(const QByteArray& bytes)
{
	return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool insideOrSame(const QString& folder, const QString& candidate)
{
	if (folder.isEmpty() || candidate.isEmpty()) {
		return false;
	}
	const QString a = QDir::cleanPath(folder);
	const QString b = QDir::cleanPath(candidate);
	return b.compare(a, Qt::CaseInsensitive) == 0 || b.startsWith(a + QLatin1Char('/'), Qt::CaseInsensitive);
}

QString baseName(const ReleasePlan& plan)
{
	return plan.release.packageName.isEmpty() ? QStringLiteral("release") : plan.release.packageName;
}

bool modFolder(const ReleasePlan& plan)
{
	const QString base = gameDefinitionForKey(plan.gameKey).baseGameDirectory;
	return !plan.packageFolder.isEmpty() && plan.packageFolder.compare(base, Qt::CaseInsensitive) != 0;
}

bool publishBytes(const QString& path, const QByteArray& bytes, bool overwrite, const std::function<bool()>& isCancelled, ReleasePublishResult* result, QString* error)
{
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		*error = QCoreApplication::translate("VibeStudioReleasePublish", "Unable to create %1.").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
		return false;
	}
	PackagePublicationOptions options;
	options.destinationPath = path;
	options.allowOverwrite = overwrite;
	options.isCancelled = isCancelled;
	PackagePublication publication(options);
	if (!publication.begin(error)) {
		return false;
	}
	if (publication.device()->write(bytes) != bytes.size()) {
		*error = QCoreApplication::translate("VibeStudioReleasePublish", "Unable to write %1.").arg(QDir::toNativeSeparators(path));
		return false;
	}
	const PackagePublicationResult published = publication.commit(quint64(bytes.size()), sha256Hex(bytes));
	result->warnings += published.warnings;
	if (!published.backupPath.isEmpty()) {
		result->backupPaths << published.backupPath;
	}
	if (!published.committed) {
		*error = published.error.isEmpty() ? QCoreApplication::translate("VibeStudioReleasePublish", "Unable to write %1.").arg(QDir::toNativeSeparators(path)) : published.error;
		return false;
	}
	return true;
}

// Reads a planned entry that is not a plain disk file.
bool readPlannedEntry(const ReleasePlan& plan, const ReleaseEntry& entry, QByteArray* bytes, QString* error)
{
	if (entry.lumpSource >= 0 && entry.lumpSource < plan.lumpSources.size()) {
		return plan.lumpSources[entry.lumpSource]->readEntryAt(entry.layerIndex, bytes, error);
	}
	if (plan.reader && entry.layer >= 0) {
		const auto* layer = plan.reader->layer(entry.layer);
		if (layer && layer->reader) {
			return layer->reader->readEntryAt(entry.layerIndex, bytes, error);
		}
	}
	*error = QCoreApplication::translate("VibeStudioReleasePublish", "%1 has no readable source.").arg(entry.virtualPath);
	return false;
}

// A PWAD of the planned lumps in plan order. The layout is the classic one:
// a 12-byte header, the lump data, then 16-byte directory records
// (https://doomwiki.org/wiki/WAD).
bool mergedWadBytes(const ReleasePlan& plan, QByteArray* out, QString* error, const std::function<bool()>& isCancelled)
{
	QByteArray data("PWAD", 4);
	data.append(8, '\0');
	struct Record { qint32 offset; qint32 size; QByteArray name; };
	QVector<Record> directory;
	for (const ReleaseEntry& entry : plan.entries) {
		if (entry.lumpSource < 0 || entry.loose) {
			continue;
		}
		if (isCancelled && isCancelled()) {
			*error = QCoreApplication::translate("VibeStudioReleasePublish", "Publishing was cancelled.");
			return false;
		}
		const QByteArray name = entry.virtualPath.toUpper().toLatin1();
		if (name.isEmpty() || name.size() > 8) {
			*error = QCoreApplication::translate("VibeStudioReleasePublish", "%1 is not a valid lump name (one to eight characters).").arg(entry.virtualPath);
			return false;
		}
		QByteArray bytes;
		if (!readPlannedEntry(plan, entry, &bytes, error)) {
			return false;
		}
		if (data.size() + bytes.size() > kMaximumWadBytes) {
			*error = QCoreApplication::translate("VibeStudioReleasePublish", "The merged WAD would exceed 1 GiB.");
			return false;
		}
		directory.push_back({qint32(data.size()), qint32(bytes.size()), name});
		data += bytes;
	}
	const qint32 directoryOffset = qint32(data.size());
	for (const Record& record : std::as_const(directory)) {
		char buffer[16] = {};
		qToLittleEndian<qint32>(record.offset, buffer);
		qToLittleEndian<qint32>(record.size, buffer + 4);
		std::memcpy(buffer + 8, record.name.constData(), size_t(record.name.size()));
		data.append(buffer, 16);
	}
	qToLittleEndian<qint32>(qint32(directory.size()), data.data() + 4);
	qToLittleEndian<qint32>(directoryOffset, data.data() + 8);
	*out = std::move(data);
	return true;
}

// Stages planned entries into an archive document. `prefix` relocates them
// (a mod folder in the distribution archive); `loose` picks the entries that
// ship beside a package rather than in it.
bool stageEntries(PackageStagingModel* model, const ReleasePlan& plan, const QString& prefix, bool loose, const PackageReadControl& control, QString* error)
{
	for (const ReleaseEntry& entry : plan.entries) {
		if (entry.loose != loose || entry.lumpSource >= 0) {
			continue;
		}
		const QString path = prefix + entry.virtualPath;
		if (!entry.sourcePath.isEmpty()) {
			if (!model->addFile(entry.sourcePath, path, error, PackageStageConflictResolution::Block, control, PackageFileImportMode::VerifyOnly)) {
				return false;
			}
			continue;
		}
		QByteArray bytes;
		if (!readPlannedEntry(plan, entry, &bytes, error) || !model->addBytes(bytes, path, error)) {
			return false;
		}
	}
	return true;
}

PackageWriteReport writeDocument(PackageStagingModel& model, PackageArchiveFormat format, const QString& path, const ReleasePublishRequest& request, const QString& phase)
{
	PackageWriteRequest write;
	write.format = format;
	write.destinationPath = path;
	write.allowOverwrite = request.overwrite;
	write.compression = request.compression;
	write.timestampMode = PackageTimestampMode::Reproducible;
	write.dryRun = request.dryRun;
	write.isCancelled = request.isCancelled;
	write.byteProgress = [&request, phase](PackageWritePhase, const QString&, quint64 completed, quint64 total) {
		if (request.progress) {
			request.progress(phase, qint64(std::min<quint64>(completed, quint64(std::numeric_limits<qint64>::max()))),
				qint64(std::min<quint64>(total, quint64(std::numeric_limits<qint64>::max()))));
		}
	};
	return model.writeArchive(write);
}

QString writeFailure(const PackageWriteReport& report)
{
	QStringList details = report.blockedMessages;
	details += report.warnings;
	return details.isEmpty() ? QCoreApplication::translate("VibeStudioReleasePublish", "The package could not be written.") : details.join(QLatin1Char(' '));
}

} // namespace

QString defaultReleaseOutputDirectory(const ProjectManifest& manifest, const ProjectReleaseSettings& release)
{
	const QString root = manifest.rootPath.trimmed();
	if (root.isEmpty()) {
		return {};
	}
	const QString folder = release.outputFolder.trimmed().isEmpty() ? QStringLiteral("build/releases") : release.outputFolder.trimmed();
	const QString base = QDir::isAbsolutePath(folder) ? folder : QDir(root).absoluteFilePath(folder);
	const QString name = (release.packageName.isEmpty() ? QStringLiteral("release") : release.packageName)
		+ QLatin1Char('-') + (release.version.isEmpty() ? QStringLiteral("1.0.0") : release.version);
	return QDir::cleanPath(QDir(base).absoluteFilePath(name));
}

QString releaseArchiveFileName(const ReleasePlan& plan)
{
	return QStringLiteral("%1-%2.zip").arg(baseName(plan), plan.release.version.isEmpty() ? QStringLiteral("1.0.0") : plan.release.version);
}

QString releaseOutputProblem(const ReleasePlan& plan, const QString& outputDirectory, bool overwrite)
{
	const QString directory = outputDirectory.trimmed();
	if (directory.isEmpty() || !QDir::isAbsolutePath(directory)) {
		return QCoreApplication::translate("VibeStudioReleasePublish", "Choose an absolute output folder.");
	}
	const QString clean = QDir::cleanPath(directory);
	if (plan.reader) {
		if (const auto* layer = plan.reader->layer(0)) {
			if (const auto* content = dynamic_cast<const ProjectContentReader*>(layer->reader.get())) {
				for (const ProjectContentRoot& root : content->roots()) {
					if (!insideOrSame(root.path, clean)) {
						continue;
					}
					bool excluded = false;
					for (const QString& folder : root.excludedFolders) {
						excluded = excluded || insideOrSame(folder, clean);
					}
					if (!excluded) {
						return QCoreApplication::translate("VibeStudioReleasePublish",
							"%1 is inside the project's content, so the next release would include this one. Choose a folder under the project's output folder, such as build/releases.")
							.arg(QDir::toNativeSeparators(clean));
					}
				}
			}
		}
	}
	if (!overwrite) {
		QStringList targets {plan.packageFileName, baseName(plan) + QStringLiteral(".txt"), QStringLiteral("RELEASE_NOTES.md")};
		if (plan.formatId != QStringLiteral("zip")) {
			targets << releaseArchiveFileName(plan);
		}
		for (const QString& name : std::as_const(targets)) {
			if (QFileInfo::exists(QDir(clean).filePath(name))) {
				return QCoreApplication::translate("VibeStudioReleasePublish", "%1 already holds a release (%2). Choose a new version, or allow replacing it.")
					.arg(QDir::toNativeSeparators(clean), name);
			}
		}
	}
	return {};
}

ReleasePublishResult publishRelease(const ReleasePublishRequest& request)
{
	ReleasePublishResult result;
	result.dryRun = request.dryRun;
	const ReleasePlan& plan = request.plan;
	const auto cancelled = [&request]() { return request.isCancelled && request.isCancelled(); };
	const auto fail = [&result, &cancelled](const QString& message) {
		result.succeeded = false;
		result.cancelled = cancelled();
		result.error = result.cancelled ? QCoreApplication::translate("VibeStudioReleasePublish", "Publishing was cancelled.") : message;
		return result;
	};
	const auto phase = [&request](const QString& text, qint64 completed = 0, qint64 total = 0) {
		if (request.progress) {
			request.progress(text, completed, total);
		}
	};
	if (!plan.canPublish()) {
		return fail(QCoreApplication::translate("VibeStudioReleasePublish", "The release plan has blocking problems. Resolve them, then plan again."));
	}
	if (!isValidReleaseVersion(plan.release.version)) {
		return fail(QCoreApplication::translate("VibeStudioReleasePublish", "%1 is not a usable version. Use letters, digits, '.', '-', '+' or '_', such as 1.2.0.").arg(plan.release.version));
	}
	const QString problem = releaseOutputProblem(plan, request.outputDirectory, request.overwrite);
	if (!problem.isEmpty()) {
		return fail(problem);
	}
	result.outputDirectory = QDir::cleanPath(request.outputDirectory);
	const QDir output(result.outputDirectory);
	if (!request.dryRun && !QDir().mkpath(result.outputDirectory)) {
		return fail(QCoreApplication::translate("VibeStudioReleasePublish", "Unable to create %1.").arg(QDir::toNativeSeparators(result.outputDirectory)));
	}

	phase(QCoreApplication::translate("VibeStudioReleasePublish", "Checking the release's files"));
	QString error;
	if (!releaseInventory(plan, &result.inventory, &error, request.isCancelled)) {
		return fail(error);
	}

	PackageReadControl control;
	control.isCancelled = request.isCancelled;
	const QString readmeName = baseName(plan) + QStringLiteral(".txt");
	const QString prefix = modFolder(plan) ? plan.packageFolder + QLatin1Char('/') : QString();
	result.packagePath = output.filePath(plan.packageFileName);
	const QString packagePhase = QCoreApplication::translate("VibeStudioReleasePublish", "Writing %1").arg(plan.packageFileName);
	phase(packagePhase);
	if (plan.formatId == QStringLiteral("wad")) {
		QByteArray wad;
		if (!mergedWadBytes(plan, &wad, &error, request.isCancelled)) {
			return fail(error);
		}
		result.packageBytes = quint64(wad.size());
		result.packageSha256 = sha256Hex(wad);
		if (!request.dryRun) {
			if (!publishBytes(result.packagePath, wad, request.overwrite, request.isCancelled, &result, &error)) {
				return fail(error);
			}
			// Reopen to prove the merged directory reads back as written.
			PackageArchive check;
			if (!check.load(result.packagePath, &error) || check.wadMagic() != QStringLiteral("PWAD")) {
				return fail(QCoreApplication::translate("VibeStudioReleasePublish", "The merged WAD does not read back: %1").arg(error));
			}
			result.packageCommitted = true;
		}
	} else {
		PackageStagingModel model;
		if (!model.createEmpty(plan.format, QString(), &error) || !model.beginOperationGroup(QStringLiteral("release"), &error)) {
			return fail(error);
		}
		// A ZIP of loose game files is itself what players download: it carries
		// the readme and loose files too, in the same layout as the archive.
		const bool zipRelease = plan.formatId == QStringLiteral("zip");
		const QString zipPrefix = zipRelease ? prefix : QString();
		bool staged = stageEntries(&model, plan, zipPrefix, false, control, &error);
		if (staged && zipRelease) {
			staged = stageEntries(&model, plan, zipPrefix, true, control, &error);
			if (staged && !request.readmeText.isEmpty()) {
				// The archive cannot name its own hash.
				staged = model.addBytes(fillReleasePackageTokens(request.readmeText, QString(), 0).toUtf8(), readmeName, &error);
			}
		}
		if (!staged || !model.endOperationGroup(true, &error)) {
			model.endOperationGroup(false);
			return fail(error);
		}
		const PackageWriteReport report = writeDocument(model, plan.format, result.packagePath, request, packagePhase);
		result.warnings += report.warnings;
		if (!report.backupPath.isEmpty()) {
			result.backupPaths << report.backupPath;
		}
		if (!report.succeeded()) {
			return fail(writeFailure(report));
		}
		result.packageBytes = report.bytesWritten;
		result.packageSha256 = report.sha256;
		result.packageCommitted = report.outputCommitted;
	}

	// Files that must sit beside the package (native game code, a WAD
	// release's text files), mirrored into the release folder.
	if (plan.formatId != QStringLiteral("zip")) {
		for (const ReleaseEntry& entry : plan.entries) {
			if (!entry.loose) {
				continue;
			}
			const QString target = output.filePath(prefix + entry.virtualPath);
			result.loosePaths << target;
			if (request.dryRun) {
				continue;
			}
			QByteArray bytes;
			if (!entry.sourcePath.isEmpty()) {
				QFile file(entry.sourcePath);
				if (file.size() > kMaximumLooseBytes || !file.open(QIODevice::ReadOnly)) {
					return fail(QCoreApplication::translate("VibeStudioReleasePublish", "Unable to read %1.").arg(QDir::toNativeSeparators(entry.sourcePath)));
				}
				bytes = file.readAll();
			} else if (!readPlannedEntry(plan, entry, &bytes, &error)) {
				return fail(error);
			}
			if (!publishBytes(target, bytes, request.overwrite, request.isCancelled, &result, &error)) {
				return fail(error);
			}
		}
	}

	const QString readmeText = fillReleasePackageTokens(request.readmeText, result.packageSha256, result.packageBytes);
	const QString notesText = fillReleasePackageTokens(request.notesMarkdown, result.packageSha256, result.packageBytes);
	if (!readmeText.isEmpty()) {
		result.readmePath = output.filePath(readmeName);
		if (!request.dryRun && !publishBytes(result.readmePath, readmeText.toUtf8(), request.overwrite, request.isCancelled, &result, &error)) {
			return fail(error);
		}
	}
	if (!notesText.isEmpty()) {
		result.notesPath = output.filePath(QStringLiteral("RELEASE_NOTES.md"));
		if (!request.dryRun && !publishBytes(result.notesPath, notesText.toUtf8(), request.overwrite, request.isCancelled, &result, &error)) {
			return fail(error);
		}
	}

	if (request.writeArchive && plan.formatId != QStringLiteral("zip")) {
		result.archivePath = output.filePath(releaseArchiveFileName(plan));
		const QString archivePhase = QCoreApplication::translate("VibeStudioReleasePublish", "Writing %1").arg(QFileInfo(result.archivePath).fileName());
		phase(archivePhase);
		if (!request.dryRun) {
			PackageStagingModel archive;
			bool staged = archive.createEmpty(PackageArchiveFormat::Zip, QString(), &error) && archive.beginOperationGroup(QStringLiteral("distribution"), &error)
				&& archive.addFile(result.packagePath, prefix + plan.packageFileName, &error, PackageStageConflictResolution::Block, control, PackageFileImportMode::VerifyOnly)
				&& stageEntries(&archive, plan, prefix, true, control, &error);
			if (staged && !readmeText.isEmpty()) {
				staged = archive.addBytes(readmeText.toUtf8(), readmeName, &error);
			}
			if (!staged || !archive.endOperationGroup(true, &error)) {
				archive.endOperationGroup(false);
				return fail(error);
			}
			const PackageWriteReport report = writeDocument(archive, PackageArchiveFormat::Zip, result.archivePath, request, archivePhase);
			result.warnings += report.warnings;
			if (!report.backupPath.isEmpty()) {
				result.backupPaths << report.backupPath;
			}
			if (!report.succeeded()) {
				return fail(writeFailure(report));
			}
			result.archiveBytes = report.bytesWritten;
			result.archiveSha256 = report.sha256;
		}
	}

	if (request.writeRecord && !request.projectRoot.trimmed().isEmpty()) {
		ReleaseRecord record;
		record.version = plan.release.version;
		record.title = plan.release.title;
		record.gameKey = plan.gameKey;
		record.scope = releaseScopeId(plan.scope);
		record.formatId = plan.formatId;
		record.packageFileName = plan.packageFileName;
		record.packageSha256 = result.packageSha256;
		record.packageBytes = result.packageBytes;
		record.publishedUtc = QDateTime::currentDateTimeUtc();
		for (const ReleaseMapInfo& map : plan.maps) {
			record.maps += map.doomMaps.isEmpty() ? QStringList {map.name} : map.doomMaps;
		}
		record.files = result.inventory;
		record.notes = notesText;
		record.studioVersion = versionString();
		const QString relativeOutput = QDir(request.projectRoot).relativeFilePath(result.outputDirectory);
		record.outputDirectory = relativeOutput.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(relativeOutput) ? result.outputDirectory : relativeOutput;
		result.recordPath = releaseRecordPath(request.projectRoot, record.version);
		if (!request.dryRun && !saveReleaseRecord(request.projectRoot, record, request.overwrite, &result.recordPath, &error)) {
			return fail(error);
		}
	}

	if (request.updateChangelog && !request.changelog.path.isEmpty()) {
		ProjectChangelog changelog = request.changelog;
		for (const ChangelogEntry& entry : request.changelogAdditions) {
			if (!addChangelogEntry(&changelog, entry.category, entry.text, &error)) {
				result.warnings << error;
			}
		}
		if (!releaseProjectChangelog(&changelog, plan.release.version, request.releaseDate, &error)) {
			result.warnings << error;
		} else {
			result.changelogPath = changelog.path;
			if (!request.dryRun && !saveProjectChangelog(changelog, &error)) {
				result.warnings << error;
				result.changelogPath.clear();
			}
		}
	}
	phase(QCoreApplication::translate("VibeStudioReleasePublish", "Release ready"), 1, 1);
	result.succeeded = true;
	return result;
}

QJsonObject releasePublishResultJson(const ReleasePublishResult& result)
{
	QJsonArray inventory;
	for (const ReleaseRecordFile& file : result.inventory) {
		inventory.append(QJsonObject {
			{QStringLiteral("path"), file.path},
			{QStringLiteral("role"), file.role},
			{QStringLiteral("bytes"), double(file.sizeBytes)},
			{QStringLiteral("crc32"), QStringLiteral("%1").arg(file.crc32, 8, 16, QLatin1Char('0'))},
		});
	}
	const auto native = [](const QString& path) { return path.isEmpty() ? QString() : QDir::toNativeSeparators(path); };
	QJsonArray loose;
	for (const QString& path : result.loosePaths) {
		loose.append(native(path));
	}
	QJsonArray backups;
	for (const QString& path : result.backupPaths) {
		backups.append(native(path));
	}
	QJsonArray warnings;
	for (const QString& warning : result.warnings) {
		warnings.append(warning);
	}
	return {
		{QStringLiteral("succeeded"), result.succeeded},
		{QStringLiteral("cancelled"), result.cancelled},
		{QStringLiteral("dryRun"), result.dryRun},
		{QStringLiteral("packageCommitted"), result.packageCommitted},
		{QStringLiteral("outputDirectory"), native(result.outputDirectory)},
		{QStringLiteral("package"), native(result.packagePath)},
		{QStringLiteral("packageSha256"), result.packageSha256},
		{QStringLiteral("packageBytes"), double(result.packageBytes)},
		{QStringLiteral("readme"), native(result.readmePath)},
		{QStringLiteral("notes"), native(result.notesPath)},
		{QStringLiteral("archive"), native(result.archivePath)},
		{QStringLiteral("archiveSha256"), result.archiveSha256},
		{QStringLiteral("archiveBytes"), double(result.archiveBytes)},
		{QStringLiteral("record"), native(result.recordPath)},
		{QStringLiteral("changelog"), native(result.changelogPath)},
		{QStringLiteral("loose"), loose},
		{QStringLiteral("backups"), backups},
		{QStringLiteral("inventory"), inventory},
		{QStringLiteral("warnings"), warnings},
		{QStringLiteral("error"), result.error},
	};
}

QString releasePublishResultText(const ReleasePublishResult& result)
{
	QStringList lines;
	if (!result.succeeded) {
		lines << (result.cancelled ? QCoreApplication::translate("VibeStudioReleasePublish", "Publishing was cancelled.")
								   : QCoreApplication::translate("VibeStudioReleasePublish", "Publishing failed: %1").arg(result.error));
		if (result.packageCommitted) {
			lines << QCoreApplication::translate("VibeStudioReleasePublish", "The package was already written: %1").arg(QDir::toNativeSeparators(result.packagePath));
		}
		return lines.join(QLatin1Char('\n'));
	}
	lines << (result.dryRun ? QCoreApplication::translate("VibeStudioReleasePublish", "Dry run: nothing was written. The release would go to %1").arg(QDir::toNativeSeparators(result.outputDirectory))
							: QCoreApplication::translate("VibeStudioReleasePublish", "Release written to %1").arg(QDir::toNativeSeparators(result.outputDirectory)));
	lines << QCoreApplication::translate("VibeStudioReleasePublish", "Package: %1 (%2 bytes, SHA-256 %3)").arg(QDir::toNativeSeparators(result.packagePath)).arg(result.packageBytes).arg(result.packageSha256);
	for (const QString& path : {result.readmePath, result.notesPath, result.archivePath, result.recordPath, result.changelogPath}) {
		if (!path.isEmpty()) {
			lines << QStringLiteral("  ") + QDir::toNativeSeparators(path);
		}
	}
	for (const QString& path : result.loosePaths) {
		lines << QStringLiteral("  ") + QDir::toNativeSeparators(path);
	}
	for (const QString& warning : result.warnings) {
		lines << QStringLiteral("~ ") + warning;
	}
	return lines.join(QLatin1Char('\n'));
}

} // namespace vibestudio
