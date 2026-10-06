#include "core/package_draft_storage.h"
#include "core/package_draft.h"
#include "core/package_draft_access.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSet>

namespace vibestudio {

PackageDraftStorageReview reviewPackageDraftStorage(const QString& path, const PackageReadControl& control)
{
	PackageDraftStorageReview result; result.storage.path = QFileInfo(path).absoluteFilePath();
	const auto access = PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Read, &result.error);
	if (!access) { return result; }
	result.storage = inspectPackageStorage(path, control);
	if (!result.storage.safe()) { result.error = result.storage.error; return result; }
	PackageStagingModel verified;
	if (!PackageDraft::load(path, &verified, &result.error, control)) { return result; }
	QFile manifest(QDir(path).filePath(QStringLiteral("document.json")));
	constexpr qint64 maximumMetadata = 32 * 1024 * 1024;
	if (!manifest.open(QIODevice::ReadOnly)) { result.error = manifest.errorString(); return result; }
	const auto bytes = manifest.read(maximumMetadata + 1); const auto document = QJsonDocument::fromJson(bytes).object();
	if (bytes.size() > maximumMetadata || manifest.error() != QFileDevice::NoError
		|| QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != result.storage.manifestSha256) {
		result.error = QCoreApplication::translate("PackageDraftStorage", "The draft changed during storage review. Refresh before continuing."); return result;
	}
	QSet<QString> referenced;
	for (const char* field : {"base", "operations"}) {
		for (const auto value : document.value(QLatin1String(field)).toArray()) {
			const auto name = value.toObject().value(QStringLiteral("object")).toString();
			if (!name.isEmpty()) { referenced.insert(name); }
		}
	}
	result.referencedObjects = referenced.size();
	for (const auto& file : result.storage.files) {
		if (file.relativePath == QStringLiteral("document.json")
			|| (!file.temporary && file.relativePath.startsWith(QStringLiteral("objects/")) && referenced.contains(QFileInfo(file.relativePath).fileName()))) {
			result.retainedBytes += file.bytes;
		} else { result.reclaimable.append(file); result.reclaimableBytes += file.bytes; }
	}
	const auto current = inspectPackageStorage(path, control);
	if (!access->matchesDirectory() || !current.safe() || current.fingerprint != result.storage.fingerprint) {
		result.error = QCoreApplication::translate("PackageDraftStorage", "The draft changed during storage review. Refresh before continuing.");
	}
	return result;
}

QJsonObject packageDraftStorageReviewJson(const PackageDraftStorageReview& review)
{
	QJsonArray files;
	for (const auto& file : review.reclaimable) {
		files.append(QJsonObject{{QStringLiteral("path"), file.relativePath}, {QStringLiteral("bytes"), file.bytes}, {QStringLiteral("temporary"), file.temporary}});
	}
	return {{QStringLiteral("path"), review.storage.path}, {QStringLiteral("complete"), review.complete()},
		{QStringLiteral("error"), review.error}, {QStringLiteral("storageBytes"), review.storage.bytes},
		{QStringLiteral("storageFiles"), static_cast<int>(review.storage.files.size())}, {QStringLiteral("retainedBytes"), review.retainedBytes},
		{QStringLiteral("referencedObjects"), review.referencedObjects}, {QStringLiteral("reclaimableBytes"), review.reclaimableBytes},
		{QStringLiteral("reclaimableFiles"), static_cast<int>(review.reclaimable.size())}, {QStringLiteral("files"), files},
		{QStringLiteral("manifestSha256"), QString::fromLatin1(review.storage.manifestSha256.toHex())},
		{QStringLiteral("storageSha256"), QString::fromLatin1(review.storage.fingerprint.toHex())}};
}

PackageDraftCompaction compactPackageDraftStorage(const QString& path, const QByteArray& expectedStorageSha256,
	bool dryRun, const PackageReadControl& control)
{
	PackageDraftCompaction result; result.dryRun = dryRun;
	if (expectedStorageSha256.size() != 32) {
		result.error = QCoreApplication::translate("PackageDraftStorage", "Review the draft's current storage checksum before compacting it."); return result;
	}
	const auto review = reviewPackageDraftStorage(path, control);
	if (!review.complete()) { result.error = review.error; return result; }
	if (review.storage.fingerprint != expectedStorageSha256) {
		result.error = QCoreApplication::translate("PackageDraftStorage", "The draft storage changed. Refresh before compacting it."); return result;
	}
	if (dryRun) { result.succeeded = true; result.reclaimedBytes = review.reclaimableBytes; result.reclaimedFiles = review.reclaimable.size(); return result; }
	const QString lockPath = QDir(path).filePath(QStringLiteral(".write.lock"));
	if (!safePackageStoragePath(lockPath, &result.error)) { return result; }
	QLockFile lock(lockPath); lock.setStaleLockTime(0);
	if (!lock.tryLock()) { result.error = QCoreApplication::translate("PackageDraftStorage", "Another process is saving or maintaining this draft."); return result; }
	const auto checked = inspectPackageStorage(path, control);
	if (!checked.safe() || checked.fingerprint != expectedStorageSha256) {
		result.error = QCoreApplication::translate("PackageDraftStorage", "The draft storage changed. Refresh before compacting it."); return result;
	}
	const auto access = PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, &result.error);
	if (!access) { return result; }
	result.readerExclusion = true;
	if (!packageStorageLayoutUnchanged(checked)) {
		result.error = QCoreApplication::translate("PackageDraftStorage", "The draft storage changed. Refresh before compacting it."); return result;
	}
	PackageStorageFile manifest;
	for (const auto& file : checked.files) { if (file.relativePath == QStringLiteral("document.json")) { manifest = file; break; } }
	for (const auto& file : review.reclaimable) {
		if (control.isCancelled && control.isCancelled()) {
			result.error = QCoreApplication::translate("PackageDraftStorage", "Draft compaction cancelled. Refresh to inspect the remaining unused files."); return result;
		}
		if (!access->matchesDirectory() || !packageStorageFileUnchanged(path, manifest) || !packageStorageFileUnchanged(path, file)
			|| !QFile::remove(QDir(path).filePath(file.relativePath))) {
			result.error = QCoreApplication::translate("PackageDraftStorage", "An unused file changed or could not be removed. Refresh before continuing."); return result;
		}
		result.reclaimedBytes += file.bytes; ++result.reclaimedFiles;
		if (control.progress) { control.progress(file.relativePath, result.reclaimedFiles, review.reclaimable.size()); }
	}
	result.succeeded = true; return result;
}

} // namespace vibestudio
