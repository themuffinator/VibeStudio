#include "core/package_directory.h"
#include "core/package_plan_p.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace vibestudio {
namespace {
QString key(const QString& path)
{
	return path.normalized(QString::NormalizationForm_C).toCaseFolded();
}
bool inTree(const QString& path, const QString& root)
{
	const auto value = key(path);
	return value == root || value.startsWith(root + QLatin1Char('/'));
}
}

bool isPackageDirectoryOperation(PackageStageOperationType type)
{
	return type == PackageStageOperationType::CreateDirectory || type == PackageStageOperationType::RenameDirectory
		|| type == PackageStageOperationType::DeleteDirectory;
}

QString packageDirectoryIdentity(const QVector<PackageStagedEntry>& entries, const QString& directory, QString* error, const PackageReadControl& control, const PackageStagingPlanLimits& limits)
{
	if (error) { error->clear(); }
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageDirectory", "Checking package folder contents"));
	if (!work.checkpoint()) { return {}; }
	PackagePlanBudget budget(limits, work);
	if (!budget.validate() || !budget.checkRecords(entries.size())) { return {}; }
	const auto root = key(directory);
	// A conservative UTF-8 JSON size, computed before allocating the record.
	// Surrogate pairs are charged as two escaped code units (an upper bound).
	const auto jsonStringBytes = [&](const QString& value) -> qint64 {
		qint64 bytes = 2;
		qsizetype checked = 0;
		for (const auto character : value) {
			if (checked++ % 256 == 0 && !work.checkpoint()) { return -1; }
			const auto u = character.unicode();
			bytes += (u == '"' || u == '\\') ? 2 : u < 0x20 || character.isSurrogate() ? 6 : u < 0x80 ? 1 : u < 0x800 ? 2 : 3;
			if (bytes > limits.maximumMetadataBytes) { break; }
		}
		return bytes;
	};
	QStringList records;
	for (const auto& entry : entries) {
		if (!work.checkpoint()) { return {}; }
		if (!inTree(entry.virtualPath, root)) { continue; }
		const auto kind = packageEntryKindId(entry.kind), readerIndex = QString::number(entry.sourceReaderIndex);
		const qint64 encodedBytes = 7 + jsonStringBytes(entry.virtualPath) + jsonStringBytes(kind)
			+ QString::number(entry.sourceOrdinal).size() + jsonStringBytes(entry.operationId)
			+ jsonStringBytes(entry.baseVirtualPath) + jsonStringBytes(readerIndex);
		if (work.cancelled() || !budget.addRecord(encodedBytes * 2)) { return {}; }
		const QJsonArray record{entry.virtualPath, kind, entry.sourceOrdinal,
			entry.operationId, entry.baseVirtualPath, readerIndex};
		records << QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact));
	}
	if (!sortPackagePlan(&records, [](const auto& left, const auto& right) { return left < right; }, work)) { return {}; }
	// Preserve the persisted identity: a compact JSON array of sorted record
	// strings. Stream its escaped elements instead of retaining another full
	// JSON tree and byte array alongside the sort buffer.
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(QByteArrayView("[", 1));
	bool first = true;
	for (const auto& record : records) {
		if (!work.checkpoint()) { return {}; }
		if (!first) { hash.addData(QByteArrayView(",", 1)); }
		first = false;
		const auto encodedBytes = jsonStringBytes(record);
		if (work.cancelled() || !budget.checkText(0, encodedBytes + 2)) { return {}; }
		const auto encoded = QJsonDocument(QJsonArray{record}).toJson(QJsonDocument::Compact);
		hash.addData(QByteArrayView(encoded).sliced(1, encoded.size() - 2));
	}
	hash.addData(QByteArrayView("]", 1));
	if (!work.finish() || records.isEmpty()) { return {}; }
	return QString::fromLatin1(hash.result().toHex());
}

bool applyPackageDirectoryOperation(QVector<PackageStagedEntry>* entries, const PackageStageOperation& operation, QString* error, const PackageReadControl& control, const PackageStagingPlanLimits& limits)
{
	if (error) { error->clear(); }
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageDirectory", "Preparing package folder edit"));
	if (!work.checkpoint()) { return false; }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	const auto source = normalizePackageVirtualPath(operation.virtualPath, false);
	if (!entries || !isPackageDirectoryOperation(operation.type) || !source.isSafe() || source.trailingSlash
		|| operation.sourceOrdinal >= 0 || operation.hasInlineBytes || !operation.sourceFilePath.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "Choose a safe package folder path."));
	}
	PackagePlanBudget inputBudget(limits, work);
	if (!inputBudget.validate() || !inputBudget.checkRecords(entries->size())) { return false; }
	const QString sourceKey = key(source.normalizedPath);
	QSet<QString> files;
	bool exists = false;
	for (const auto& entry : *entries) {
		if (!work.checkpoint()) { return false; }
		if (!inputBudget.addRecord(PackagePlanBudget::entryBytes(entry))) { return false; }
		if (entry.kind == PackageEntryKind::File) {
			const auto pathKey = key(entry.virtualPath);
			if (!files.contains(pathKey) && !inputBudget.addKey(pathKey)) { return false; }
			files.insert(pathKey);
		}
		exists = exists || inTree(entry.virtualPath, sourceKey);
	}
	const bool creating = operation.type == PackageStageOperationType::CreateDirectory;
	const bool deleting = operation.type == PackageStageOperationType::DeleteDirectory;
	if (creating && exists) { return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "A file or folder already uses this path.")); }
	if (!creating && (!exists || files.contains(sourceKey))) {
		return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "The selected folder is missing or a file occupies its path."));
	}
	if (!creating) {
		QString identityError;
		const auto identity = packageDirectoryIdentity(*entries, source.normalizedPath, &identityError, work.nestedControl(), limits);
		if (work.cancelled()) { return false; }
		if (!identityError.isEmpty()) { return fail(identityError); }
		if (operation.sourceTreeIdentity.isEmpty() || operation.sourceTreeIdentity != identity) {
			return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "The folder contents changed after this edit was staged. Unstage it and select the folder again."));
		}
	}
	const auto target = normalizePackageVirtualPath(creating ? source.normalizedPath : operation.targetVirtualPath, false);
	if (!deleting && (!target.isSafe() || target.trailingSlash)) {
		return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "Choose a safe destination folder path."));
	}
	const QString targetKey = key(target.normalizedPath);
	if (!creating && !deleting && sourceKey != targetKey
		&& (sourceKey.startsWith(targetKey + QLatin1Char('/')) || targetKey.startsWith(sourceKey + QLatin1Char('/')))) {
		return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "A folder cannot replace its parent or move inside itself."));
	}
	if (!deleting) {
		for (QString parent = packagePlanParent(targetKey); !parent.isEmpty(); parent = packagePlanParent(parent)) {
			if (!work.checkpoint()) { return false; }
			if (files.contains(parent)) { return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "A file occupies a required destination folder.")); }
		}
		if (!creating) {
			for (const auto& entry : *entries) {
				if (!work.checkpoint()) { return false; }
				if (!inTree(entry.virtualPath, sourceKey) && inTree(entry.virtualPath, targetKey)) {
					return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "The destination folder already exists. Choose an unused folder path."));
				}
			}
		}
	}
	PackagePlanBudget outputBudget(limits, work);
	if (!outputBudget.checkRecords(entries->size() + (creating ? 1 : 0))) { return false; }
	QSet<QString> paths, parents;
	const auto admit = [&](const PackageStagedEntry& entry) {
		if (!outputBudget.addRecord(PackagePlanBudget::entryBytes(entry))) { return false; }
		const auto pathKey = key(entry.virtualPath);
		if (!paths.contains(pathKey)) {
			if (!outputBudget.addKey(pathKey)) { return false; }
			paths.insert(pathKey);
		}
		for (auto parent = packagePlanParent(pathKey); !parent.isEmpty(); parent = packagePlanParent(parent)) {
			if (!work.checkpoint()) { return false; }
			if (!parents.contains(parent)) {
				if (!outputBudget.addKey(parent)) { return false; }
				parents.insert(parent);
			}
		}
		return true;
	};
	QVector<PackageStagedEntry> next;
	next.reserve(entries->size() + (creating ? 1 : 0));
	const qsizetype prefixParts = source.normalizedPath.count(QLatin1Char('/')) + 1;
	for (auto entry : *entries) {
		if (!work.checkpoint()) { return false; }
		if (!creating && inTree(entry.virtualPath, sourceKey)) {
			if (deleting) { continue; }
			// Slice by components, not the character count of a case-folded name.
			const QString suffix = entry.virtualPath.section(QLatin1Char('/'), static_cast<int>(prefixParts));
			entry.virtualPath = target.normalizedPath + (suffix.isEmpty() ? QString() : QLatin1Char('/') + suffix);
			if (!normalizePackageVirtualPath(entry.virtualPath, false).isSafe()) {
				return fail(QCoreApplication::translate("VibeStudioPackageDirectory", "A renamed child would exceed the package path limit. Choose a shorter folder path."));
			}
			entry.source = QStringLiteral("staged-rename"); entry.operationId = operation.id;
		}
		if (!admit(entry)) { return false; }
		next.append(std::move(entry));
	}
	if (creating) {
		PackageStagedEntry directory; directory.virtualPath = source.normalizedPath;
		directory.kind = PackageEntryKind::Directory; directory.source = QStringLiteral("staged-directory");
		directory.operationId = operation.id;
		if (!admit(directory)) { return false; }
		next.append(std::move(directory));
	}
	if (!work.finish()) { return false; }
	*entries = std::move(next);
	return true;
}

} // namespace vibestudio
