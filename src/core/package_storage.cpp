#include "core/package_storage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace vibestudio {
namespace {
constexpr qint64 metadataHashLimit = 32 * 1024 * 1024;
bool validObject(const QString& name) { return name.size() == 64 && QByteArray::fromHex(name.toLatin1()).toHex() == name.toLatin1(); }
qint64 createdMs(const QFileInfo& info) { return info.birthTime().isValid() ? info.birthTime().toMSecsSinceEpoch() : 0; }
}

bool safePackageStoragePath(const QString& path, QString* error)
{
	if (path.trimmed().isEmpty()) { if (error) { *error = QCoreApplication::translate("PackageStorage", "Choose a package storage directory."); } return false; }
	QFileInfo info(QFileInfo(path).absoluteFilePath());
	for (;;) {
		if (info.isSymbolicLink() || info.isJunction()) {
			if (error) { *error = QCoreApplication::translate("PackageStorage", "Package storage cannot contain symbolic links or junctions: %1").arg(info.absoluteFilePath()); }
			return false;
		}
		const QString parent = info.absolutePath();
		if (parent == info.absoluteFilePath()) { return true; }
		info.setFile(parent);
	}
}

bool packageStorageTemporaryName(const QString& name, const QString& prefix)
{
	if (!name.startsWith(prefix) || name.size() != prefix.size() + 6) { return false; }
	for (const QChar c : name.sliced(prefix.size())) {
		if (!((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
			|| (c >= QLatin1Char('0') && c <= QLatin1Char('9')))) { return false; }
	}
	return true;
}

bool packageStorageFileUnchanged(const QString& root, const PackageStorageFile& file)
{
	const QString path = QDir(root).filePath(file.relativePath);
	const QFileInfo info(path);
	return safePackageStoragePath(path) && info.isFile() && info.size() == file.bytes
		&& info.lastModified().toMSecsSinceEpoch() == file.modifiedMs && createdMs(info) == file.createdMs;
}

bool packageStorageLayoutUnchanged(const PackageStorageSnapshot& snapshot)
{
	const QFileInfo root(snapshot.path), objects(QDir(snapshot.path).filePath(QStringLiteral("objects")));
	return safePackageStoragePath(snapshot.path) && root.isDir() && root.canonicalFilePath() == snapshot.canonicalPath
		&& createdMs(root) == snapshot.rootCreated && root.lastModified() == snapshot.rootModified
		&& objects.exists() == snapshot.objectsPresent
		&& (!snapshot.objectsPresent || (safePackageStoragePath(objects.absoluteFilePath()) && objects.isDir()
			&& objects.canonicalFilePath() == snapshot.canonicalObjects && createdMs(objects) == snapshot.objectsCreated
			&& objects.lastModified() == snapshot.objectsModified));
}

PackageStorageSnapshot inspectPackageStorage(const QString& path, const PackageReadControl& control, int maximumEntries)
{
	PackageStorageSnapshot result; result.path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	const auto fail = [&](const QString& error) { result.error = error; result.fingerprint.clear(); return result; };
	const auto stopped = [&]() { return control.isCancelled && control.isCancelled(); };
	if (!safePackageStoragePath(path, &result.error)) { return result; }
	const QFileInfo original(path);
	if (!original.isDir()) { return fail(QCoreApplication::translate("PackageStorage", "Package storage is not a regular directory.")); }
	const QString canonical = original.canonicalFilePath(); const qint64 rootCreated = createdMs(original);
	const auto rootModified = original.lastModified();
	const QFileInfo originalObjects(QDir(path).filePath(QStringLiteral("objects")));
	const bool objectsExisted = originalObjects.exists();
	maximumEntries = std::clamp(maximumEntries, 0, PackageStorageEntryLimit);
	const auto append = [&](const QFileInfo& info, bool object, bool temporary) {
		if (result.files.size() >= maximumEntries || info.size() < 0 || result.bytes > std::numeric_limits<qint64>::max() - info.size()) {
			result.error = QCoreApplication::translate("PackageStorage", "The storage scan reached its file or byte limit."); return false;
		}
		if (!safePackageStoragePath(info.absoluteFilePath(), &result.error) || !info.isFile()) {
			result.error = QCoreApplication::translate("PackageStorage", "Package storage contains an unsafe file or directory: %1").arg(info.absoluteFilePath()); return false;
		}
		PackageStorageFile file; file.relativePath = QDir(path).relativeFilePath(info.absoluteFilePath()); file.bytes = info.size();
		file.modifiedMs = info.lastModified().toMSecsSinceEpoch(); file.createdMs = createdMs(info); file.temporary = temporary;
		if (file.relativePath == QStringLiteral("document.json")) {
			result.manifestPresent = true;
			if (file.bytes <= metadataHashLimit) {
				QFile input(info.absoluteFilePath());
				if (!input.open(QIODevice::ReadOnly)) { result.error = input.errorString(); return false; }
				QCryptographicHash hash(QCryptographicHash::Sha256); qint64 read = 0;
				while (!input.atEnd()) {
					if (stopped()) { result.error = QCoreApplication::translate("PackageStorage", "Package storage scan cancelled."); return false; }
					const QByteArray chunk = input.read(PackageFileIdentity::chunkBytes);
					if (chunk.isEmpty() || chunk.size() > file.bytes - read) { result.error = QCoreApplication::translate("PackageStorage", "The package manifest changed or could not be read."); return false; }
					read += chunk.size(); hash.addData(chunk);
				}
				if (read != file.bytes || input.error() != QFileDevice::NoError) { result.error = QCoreApplication::translate("PackageStorage", "The package manifest changed or could not be read."); return false; }
				file.metadataSha256 = hash.result(); result.manifestSha256 = file.metadataSha256;
			}
		}
		result.bytes += file.bytes; if (object) { result.objectBytes += file.bytes; } if (temporary) { result.temporaryBytes += file.bytes; }
		result.files.append(std::move(file)); return true;
	};
	bool hasObjects = false; int roots = 0;
	QDirIterator top(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
	while (top.hasNext()) {
		if (stopped()) { return fail(QCoreApplication::translate("PackageStorage", "Package storage scan cancelled.")); }
		top.next(); const auto info = top.fileInfo(); const QString name = info.fileName();
		if (++roots > 256 || !safePackageStoragePath(info.absoluteFilePath(), &result.error)) { return fail(QCoreApplication::translate("PackageStorage", "Package storage contains too many entries or an unsafe path.")); }
		if (name == QStringLiteral("objects") && info.isDir()) { hasObjects = true; continue; }
		if (name == QStringLiteral(".write.lock") && info.isFile()) { continue; }
		if (name == QStringLiteral("document.json") || packageStorageTemporaryName(name, QStringLiteral(".document-"))
			|| packageStorageTemporaryName(name, QStringLiteral("document.json.")) || packageStorageTemporaryName(name, QStringLiteral(".document.json."))) {
			if (!append(info, false, name != QStringLiteral("document.json"))) { return result; }
		} else { return fail(QCoreApplication::translate("PackageStorage", "Package storage contains an unrelated entry: %1").arg(info.absoluteFilePath())); }
	}
	if (hasObjects) {
		QDirIterator objects(QDir(path).filePath(QStringLiteral("objects")), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
		while (objects.hasNext()) {
			if (stopped()) { return fail(QCoreApplication::translate("PackageStorage", "Package storage scan cancelled.")); }
			objects.next(); const auto info = objects.fileInfo(); const bool temporary = packageStorageTemporaryName(info.fileName(), QStringLiteral(".writing-"));
			if (!validObject(info.fileName()) && !temporary) { return fail(QCoreApplication::translate("PackageStorage", "Package storage contains an unrelated entry: %1").arg(info.absoluteFilePath())); }
			if (!append(info, !temporary, temporary)) { return result; }
		}
	}
	std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
	QCryptographicHash hash(QCryptographicHash::Sha256);
	const auto addField = [&](const QByteArray& field) { hash.addData(QByteArray::number(field.size())); hash.addData(QByteArrayLiteral(":")); hash.addData(field); };
	addField(QByteArrayLiteral("vibestudio-package-storage-v1")); addField(result.path.toUtf8()); addField(QByteArray::number(rootCreated));
	addField(hasObjects ? QByteArrayLiteral("objects-present") : QByteArrayLiteral("objects-absent"));
	for (const auto& file : result.files) {
		if (!packageStorageFileUnchanged(path, file)) { return fail(QCoreApplication::translate("PackageStorage", "Package storage changed during the scan. Refresh and try again.")); }
		for (const auto& field : {file.relativePath.toUtf8(), QByteArray::number(file.bytes), QByteArray::number(file.modifiedMs), QByteArray::number(file.createdMs), file.metadataSha256}) {
			addField(field);
		}
	}
	const QFileInfo currentRoot(path), currentObjects(originalObjects.absoluteFilePath());
	if (stopped() || !safePackageStoragePath(path) || currentRoot.canonicalFilePath() != canonical || createdMs(currentRoot) != rootCreated
		|| currentRoot.lastModified() != rootModified || currentObjects.exists() != objectsExisted
		|| (hasObjects && (!safePackageStoragePath(currentObjects.absoluteFilePath()) || !currentObjects.isDir()
			|| currentObjects.canonicalFilePath() != originalObjects.canonicalFilePath() || createdMs(currentObjects) != createdMs(originalObjects)
			|| currentObjects.lastModified() != originalObjects.lastModified()))) {
		return fail(QCoreApplication::translate("PackageStorage", "The storage scan was cancelled or its directory changed."));
	}
	result.canonicalPath = canonical; result.rootCreated = rootCreated; result.rootModified = rootModified;
	result.objectsPresent = objectsExisted; result.canonicalObjects = originalObjects.canonicalFilePath();
	result.objectsCreated = createdMs(originalObjects); result.objectsModified = originalObjects.lastModified();
	result.fingerprint = hash.result(); return result;
}

} // namespace vibestudio
