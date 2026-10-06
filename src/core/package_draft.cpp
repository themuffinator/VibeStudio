#include "core/package_draft.h"
#include "core/package_protection_p.h"
#include "core/package_directory.h"
#include "core/package_draft_access.h"
#include "core/package_draft_storage.h"
#include "core/package_plan_p.h"
#include "core/package_index_p.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>
#include <QTimeZone>

#include <limits>

namespace vibestudio {
namespace {

constexpr qint64 maximumManifestBytes = 32 * 1024 * 1024;
constexpr qsizetype maximumRecords = 250000;

bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }
bool cancelled(const PackageReadControl& control, QString* error)
{
	if (!control.isCancelled || !control.isCancelled()) { return false; }
	fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Package draft operation cancelled."));
	return true;
}

bool noLinks(const QString& path, QString* error)
{
	QFileInfo info(QFileInfo(path).absoluteFilePath());
	for (;;) {
		if (info.isSymbolicLink() || info.isJunction()) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Package drafts cannot use symbolic links or junctions: %1").arg(info.absoluteFilePath())); }
		const QString parent = info.absolutePath();
		if (parent == info.absoluteFilePath()) { break; }
		info.setFile(parent);
	}
	return true;
}

bool draftDirectory(const QString& input, QString* absolute, QString* error)
{
	if (input.trimmed().isEmpty()) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Choose a .vibepackage draft directory.")); }
	*absolute = QDir::cleanPath(QFileInfo(input).absoluteFilePath());
	if (!absolute->endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "A package draft directory must end in .vibepackage."));
	}
	return noLinks(*absolute, error);
}

bool readUnsigned(const QJsonValue& value, quint64* result)
{
	if (!value.isString()) { return false; }
	bool ok = false;
	*result = value.toString().toULongLong(&ok);
	return ok && QString::number(*result) == value.toString();
}

// Resolve existing ancestors without creating a destination. This also counts
// expanded Windows short-path aliases before any future object is written.
QString prospectiveResolvedPath(const QString& path)
{
	QFileInfo current(path); QStringList suffix;
	while (!current.exists() && current.absolutePath() != current.absoluteFilePath()) {
		suffix.prepend(current.fileName()); current.setFile(current.absolutePath());
	}
	const QString prefix = current.canonicalFilePath();
	return prefix.isEmpty() ? path : (suffix.isEmpty() ? prefix : QDir(prefix).filePath(suffix.join(QLatin1Char('/'))));
}

// The only payload path accepted from a document is a lowercase SHA-256.
bool validObjectName(const QString& name)
{
	static const QRegularExpression expression(QStringLiteral("\\A[0-9a-f]{64}\\z"));
	return expression.match(name).hasMatch();
}

PackageFileIdentityPtr protectDraftIdentity(const PackageFileIdentityPtr& identity,
	const std::shared_ptr<const PackageDraftAccess>& access, const QString& directory)
{
	if (!identity) { return {}; }
	auto owned = std::make_shared<PackageFileIdentity>(*identity);
	owned->draftAccess = access; owned->storageDirectory = directory; return owned;
}

class ObjectStore final {
public:
	ObjectStore(QString root, PackageReadControl control, QString* error, bool dryRun = false, qint64* remainingBytes = nullptr,
		std::shared_ptr<const PackageDraftAccess> access = {}, int* remainingFiles = nullptr,
		qint64 maximumFingerprintBytes = PackageStagingContentLimits::fingerprintCeiling)
		: m_root(std::move(root)), m_control(std::move(control)), m_error(error), m_dryRun(dryRun), m_remainingBytes(remainingBytes), m_access(std::move(access)), m_remainingFiles(remainingFiles), m_remainingFingerprintBytes(maximumFingerprintBytes) {}

	PackageFileIdentityPtr read(const QString& name)
	{
		if (cancelled(m_control, m_error)) { return {}; }
		if (!validObjectName(name)) { fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "The package draft contains an invalid object identity.")); return {}; }
		if (m_verified.contains(name)) { return m_verified.value(name); }
		const QString path = QDir(m_root).filePath(QStringLiteral("objects/") + name);
		if (!noLinks(path, m_error)) { return {}; }
		auto identity = capturePackageFileIdentity(path, m_error, m_control, m_remainingFingerprintBytes);
		if (!identity) { return {}; }
		if (QString::fromLatin1(identity->sha256.toHex()) != name) {
			fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "Package draft object failed its content check: %1").arg(name)); return {};
		}
		identity = protectDraftIdentity(identity, m_access, m_root);
		m_remainingFingerprintBytes -= identity->chunkHashes.size();
		m_verified.insert(name, identity);
		return identity;
	}

	using Producer = std::function<bool(const std::function<bool(QByteArrayView)>&)>;
	PackageFileIdentityPtr write(qint64 size, const QString& label, const Producer& producer, bool* sourceUnavailable = nullptr)
	{
		if (sourceUnavailable) { *sourceUnavailable = false; }
		if (size < 0 || cancelled(m_control, m_error)) { return {}; }
		const QString objects = QDir(m_root).filePath(QStringLiteral("objects"));
		if (!noLinks(objects, m_error) || (!m_dryRun && !QDir().mkpath(objects)) || !noLinks(objects, m_error)
			|| (QFileInfo::exists(objects) && !QFileInfo(objects).isDir())) {
			fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "Unable to create the package draft object directory.")); return {};
		}
		// When a new object cannot fit, hash without creating a temporary file:
		// an identical stored object can still be reused within the limit.
		const qint64 fingerprints = (size / PackageFileIdentity::chunkBytes + (size % PackageFileIdentity::chunkBytes != 0)) * 32;
		const bool hashOnly = m_dryRun || (m_remainingBytes && size > *m_remainingBytes) || (m_remainingFiles && *m_remainingFiles < 1)
			|| fingerprints > m_remainingFingerprintBytes;
		QTemporaryFile temporary(QDir(objects).filePath(QStringLiteral(".writing-XXXXXX")));
		if (!hashOnly && !temporary.open()) { fail(m_error, temporary.errorString()); return {}; }
		QCryptographicHash hash(QCryptographicHash::Sha256);
		qint64 written = 0; bool outputFailed = false;
		if (m_control.progress) { m_control.progress(label, 0, size); }
		const bool ok = producer([&](QByteArrayView bytes) {
			if (cancelled(m_control, m_error)) { return false; }
			if (bytes.size() > size - written) {
				return fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "Incomplete package draft object: %1").arg(label));
			}
			if (!hashOnly && temporary.write(bytes.data(), bytes.size()) != bytes.size()) {
				outputFailed = true;
				return fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "Unable to write complete package draft content: %1").arg(label));
			}
			hash.addData(bytes); written += bytes.size();
			if (m_control.progress) { m_control.progress(label, written, size); }
			return true;
		});
		if (cancelled(m_control, m_error)) { return {}; }
		if (!ok) { if (sourceUnavailable) { *sourceUnavailable = !outputFailed; } return {}; }
		if (written != size) {
			if (sourceUnavailable) { *sourceUnavailable = true; }
			fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "Incomplete package draft object: %1").arg(label)); return {};
		}
		if (!hashOnly && !temporary.flush()) { fail(m_error, temporary.errorString()); return {}; }
		const QString name = QString::fromLatin1(hash.result().toHex());
		const QString destination = QDir(objects).filePath(name);
		if (!noLinks(destination, m_error)) { return {}; }
		if (QFileInfo::exists(destination)) { return read(name); }
		if (m_projected.contains(name)) { return m_verified.value(name); }
		if (fingerprints > m_remainingFingerprintBytes) {
			fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "Draft payload fingerprints exceed the document content limit. Save a smaller package and reopen it; the committed draft is unchanged.")); return {};
		}
		if ((m_remainingBytes && size > *m_remainingBytes) || (m_remainingFiles && *m_remainingFiles < 1)) {
			fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "The package storage byte or file limit would be exceeded. Review storage or increase the limit; the committed document is unchanged.")); return {};
		}
		if (m_dryRun) {
			auto identity = std::make_shared<PackageFileIdentity>();
			identity->path = destination; identity->resolvedPath = prospectiveResolvedPath(destination); identity->storageDirectory = m_root; identity->draftAccess = m_access;
			identity->size = size; identity->sha256 = hash.result();
			m_projected.insert(name); m_verified.insert(name, identity);
			m_remainingFingerprintBytes -= fingerprints;
			if (m_remainingBytes) { *m_remainingBytes -= size; } if (m_remainingFiles) { --*m_remainingFiles; }
			return identity;
		}
		// QTemporaryFile's rename is atomic-only, with no copy/delete fallback.
		if (!temporary.rename(destination)) {
			if (QFileInfo::exists(destination)) { return read(name); }
			fail(m_error, QCoreApplication::translate("VibeStudioPackageDraft", "Unable to publish a package draft object: %1").arg(temporary.errorString())); return {};
		}
		temporary.setAutoRemove(false);
		if (m_remainingBytes) { *m_remainingBytes -= size; } if (m_remainingFiles) { --*m_remainingFiles; }
		return read(name);
	}

	PackageFileIdentityPtr copy(const PackageFileIdentityPtr& identity, const QString& label)
	{
		if (!identity) { return {}; }
		const QString name = QString::fromLatin1(identity->sha256.toHex());
		// Verify the live input even when a matching object already exists.
		if (!verifyPackageFileIdentity(identity, m_error, m_control)) { return {}; }
		if (QFileInfo::exists(QDir(m_root).filePath(QStringLiteral("objects/") + name))) { return read(name); }
		PackageContentDevice device(identity);
		if (!device.open()) { fail(m_error, device.errorString()); return {}; }
		return write(identity->size, label, [&](const auto& sink) {
			while (!device.atEnd()) {
				if (cancelled(m_control, m_error)) { return false; }
				const QByteArray chunk = device.read(PackageFileIdentity::chunkBytes);
				if (device.failed() || chunk.isEmpty()) { return fail(m_error, device.errorString()); }
				if (!sink(chunk)) { return false; }
			}
			return true;
		});
	}

	bool verifyAll()
	{
		for (auto it = m_verified.cbegin(); it != m_verified.cend(); ++it) {
			if (m_projected.contains(it.key())) { continue; }
			const auto& identity = it.value();
			if (!noLinks(identity->path, m_error) || !verifyPackageFileIdentity(identity, m_error, m_control)) { return false; }
		}
		return true;
	}

private:
	QString m_root;
	PackageReadControl m_control;
	QString* m_error;
	bool m_dryRun = false;
	qint64* m_remainingBytes = nullptr;
	QHash<QString, PackageFileIdentityPtr> m_verified;
	std::shared_ptr<const PackageDraftAccess> m_access;
	int* m_remainingFiles = nullptr;
	QSet<QString> m_projected;
	qint64 m_remainingFingerprintBytes;
};

QJsonObject operationJson(const PackageStageOperation& operation)
{
	return {{QStringLiteral("id"), operation.id}, {QStringLiteral("type"), packageStageOperationTypeId(operation.type)},
		{QStringLiteral("path"), operation.virtualPath}, {QStringLiteral("target"), operation.targetVirtualPath},
		{QStringLiteral("sourceOrdinal"), operation.sourceOrdinal},
		{QStringLiteral("sourceTreeIdentity"), operation.sourceTreeIdentity},
		{QStringLiteral("source"), operation.sourceFilePath}, {QStringLiteral("error"), operation.sourceError},
		{QStringLiteral("resolution"), packageStageConflictResolutionId(operation.conflictResolution)},
		{QStringLiteral("modified"), operation.sourceModifiedUtc.toString(Qt::ISODateWithMs)},
		{QStringLiteral("wadType"), operation.wadLumpType}, {QStringLiteral("wadInsertBefore"), operation.wadInsertBefore},
		{QStringLiteral("wadNamespace"), operation.wadNamespace},
		{QStringLiteral("object"), operation.sourceIdentity ? QString::fromLatin1(operation.sourceIdentity->sha256.toHex()) : QString()}};
}

} // namespace

PackageArchive PackageDraft::baseArchive(const PackageStagingModel& staging, QString* error, const PackageReadControl& control, const PackageIndexLimits& limits)
{
	PackageStagingModel base = staging;
	if (!base.freezeInputProtections(error, control, base.m_draftPath)) { return {}; }
	base.m_operations.clear(); base.m_baseConflicts.clear(); base.resetHistory(); base.invalidatePlan();
	PackageArchive archive;
	QVector<PackageLoadWarning> warnings;
	for (const auto& conflict : staging.m_baseConflicts) { warnings.append({conflict.virtualPath, conflict.message, conflict.blocking}); }
	archive.loadSnapshot(std::make_shared<PackageStagingArchive>(base, PackageStagingReadMode::InspectPlan, control, limits), error, warnings, control, limits);
	return archive;
}

bool PackageDraft::admitMetadata(const QString& root, const PackageStagingModel& staging, QString* error, const PackageReadControl& control)
{
	// Every content address has the same encoded length. Predict destination
	// paths and identity slots before hashing payloads or creating directories.
	// Shared placeholders suffice: metadata charges are logical per record.
	auto predicted = staging;
	auto object = std::make_shared<PackageFileIdentity>();
	object->path = QDir(root).filePath(QStringLiteral("objects/") + QString(64, QLatin1Char('0')));
	object->resolvedPath = prospectiveResolvedPath(object->path); object->storageDirectory = root;
	for (auto& entry : predicted.m_baseEntries) {
		if (cancelled(control, error)) { return false; }
		if (entry.unavailableReason.isEmpty()) { entry.sourceIdentity = object; entry.sourceFilePath = object->path; entry.hasInlineBytes = false; entry.inlineBytes.clear(); }
	}
	const auto freeze = [&](PackageStageOperation& operation) {
		if (operation.hasInlineBytes || operation.sourceIdentity) { operation.sourceIdentity = object; operation.hasInlineBytes = false; operation.inlineBytes.clear(); }
	};
	for (auto& operation : predicted.m_operations) { if (cancelled(control, error)) { return false; } freeze(operation); }
	for (auto& step : predicted.m_history) {
		for (auto& change : step.changes) { if (cancelled(control, error)) { return false; } freeze(change.operation); }
	}
	auto manifest = std::make_shared<PackageFileIdentity>();
	manifest->path = QDir(root).filePath(QStringLiteral("document.json")); manifest->resolvedPath = prospectiveResolvedPath(manifest->path); manifest->storageDirectory = root;
	predicted.m_draftIdentity = std::move(manifest); predicted.m_draftPath = root; predicted.m_baseReader.reset(); predicted.invalidatePlan();
	PackageStagingMetadataUsage usage;
	return predicted.metadataUsage(&usage, error, control) && baseArchive(predicted, error, control).isOpen();
}

bool PackageDraft::save(const QString& directory, PackageStagingModel* staging, bool overwrite, QString* error, const PackageReadControl& control, bool dryRun)
{
	// Preflight exact content-addressed additions and manifest headroom without
	// creating the destination. The real pass rechecks under the writer lock.
	if (!dryRun && !saveWithRecovery(directory, staging, overwrite, error, control, true, {}, {}, -1, true)) { return false; }
	return saveWithRecovery(directory, staging, overwrite, error, control, dryRun, {});
}

bool PackageDraft::saveWithRecovery(const QString& directory, PackageStagingModel* staging, bool overwrite, QString* error,
	const PackageReadControl& control, bool dryRun, const QJsonObject& recovery, const QByteArray& expectedManifestSha256,
	qint64 maximumAdditionalBytes, bool preflightForWrite)
{
	if (error) { error->clear(); }
	if (!staging || !staging->m_loaded || staging->m_groupDepth != 0) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Finish the current package edit before saving a draft.")); }
	PackageStagingContentUsage retained;
	PackageStagingMetadataUsage metadata;
	if (!staging->contentUsage(&retained, error, control) || !staging->metadataUsage(&metadata, error, control)) { return false; }
	for (const auto& conflict : staging->m_baseConflicts) {
		if (conflict.blocking) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "The source contains unreadable or skipped records and cannot be preserved completely in a draft: %1").arg(conflict.message)); }
	}
	QString root;
	if (!draftDirectory(directory, &root, error) || cancelled(control, error)) { return false; }
	const QString manifestPath = QDir(root).filePath(QStringLiteral("document.json"));
	const QString lockPath = QDir(root).filePath(QStringLiteral(".write.lock"));
	const bool sameDraft = !staging->m_draftPath.isEmpty() && QDir::cleanPath(staging->m_draftPath).compare(root, Qt::CaseInsensitive) == 0;
	PackageStagingModel protectedSource = *staging;
	if (!protectedSource.freezeInputProtections(error, control, sameDraft ? root : QString())) { return false; }
	// A new draft owns its entire directory, including locks and objects. Do
	// not place that managed store around an existing protected source file.
	if (!sameDraft) {
		for (const auto& input : protectedSource.m_protectedInputPaths) {
			if (cancelled(control, error)) { return false; }
			if (packagePathIsInsideDirectory(root, input)) {
				return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Save the package draft outside its source package, folder and imported files."));
			}
		}
	}
	if (!sameDraft && (staging->protectsInputPath(root) || staging->protectsInputPath(manifestPath))) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Save the package draft outside its source package, folder and imported files."));
	}
	if (QFileInfo::exists(root) && !QFileInfo(root).isDir()) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "The package draft destination is not a directory.")); }
	QFileInfo parent(QFileInfo(root).absolutePath());
	while (!parent.exists() && parent.absolutePath() != parent.absoluteFilePath()) { parent.setFile(parent.absolutePath()); }
	if (!parent.isDir()) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "A file occupies a required draft directory.")); }
	if (!admitMetadata(root, protectedSource, error, control)) { return false; }
	if (!noLinks(manifestPath, error) || !noLinks(lockPath, error) || (!dryRun && !QDir().mkpath(root)) || !noLinks(root, error)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Unable to prepare the package draft directory."));
	}
	std::shared_ptr<const PackageDraftAccess> access;
	if (QFileInfo::exists(root)) {
		access = PackageDraftAccess::acquire(root, PackageDraftAccess::Mode::Read, error);
		if (!access) { return false; }
	}
	QLockFile lock(lockPath);
	lock.setStaleLockTime(0);
	// A real save's preflight leaves even stale lock files untouched. The write
	// pass then lets QLockFile recover a crashed owner, or refuses a live writer.
	// Explicit previews remain conservative and never acquire/clear that lock.
	if (dryRun ? (!preflightForWrite && QFileInfo::exists(lockPath)) : !lock.tryLock()) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Another process is saving this package draft.")); }
	PackageStorageSnapshot usage;
	if (QFileInfo::exists(root)) {
		usage = inspectPackageStorage(root, control);
		if (!usage.safe()) { return fail(error, usage.error); }
	}
	int remainingFiles = -1;
	if (recovery.isEmpty()) {
		const auto limits = control.draftLimits ? *control.draftLimits : PackageDraftSaveLimits();
		if (limits.maximumBytes < 0 || limits.maximumFiles < 1 || limits.maximumFiles > PackageStorageEntryLimit) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Package draft storage limits are invalid."));
		}
		maximumAdditionalBytes = qMax<qint64>(0, limits.maximumBytes - usage.bytes);
		remainingFiles = qMax(0, limits.maximumFiles - static_cast<int>(usage.files.size()));
	}
	const bool exists = QFileInfo::exists(manifestPath);
	if (!expectedManifestSha256.isEmpty() && !exists) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "The draft changed on disk. Reopen it or save to another directory.")); }
	if (exists && !overwrite) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "The package draft already exists. Enable overwrite explicitly.")); }
	PackageFileIdentityPtr original;
	if (exists) {
		original = capturePackageFileIdentity(manifestPath, error, control, (maximumManifestBytes / PackageFileIdentity::chunkBytes) * 32);
		if (!original) { return false; }
		if ((!expectedManifestSha256.isEmpty() && original->sha256 != expectedManifestSha256)
			|| (sameDraft && staging->m_draftIdentity && original->sha256 != staging->m_draftIdentity->sha256)) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "The draft changed on disk. Reopen it or save to another directory."));
		}
	}

	PackageStagingModel saved = std::move(protectedSource);
	// Only an original occurrence absent from the current plan may first become
	// unavailable. A failed projection cannot establish absence. Imported and
	// previously verified draft objects remain strict even in deleted history.
	QString projectionError;
	const bool knowCurrent = staging->preparePlan(&projectionError, control);
	QSet<qsizetype> currentOriginals;
	if (knowCurrent) {
		for (const auto& entry : staging->plannedEntries()) {
			if (!entry.hasInlineBytes && !entry.sourceIdentity && entry.sourceFilePath.isEmpty() && entry.unavailableReason.isEmpty()) {
				currentOriginals.insert(entry.sourceReaderIndex);
			}
		}
	}
	qint64 additionalUnavailableText = 0;
	ObjectStore objects(root, control, error, dryRun, maximumAdditionalBytes >= 0 ? &maximumAdditionalBytes : nullptr, access,
		remainingFiles >= 0 ? &remainingFiles : nullptr, staging->m_contentLimits.maximumFingerprintBytes);
	const PackageArchive base = baseArchive(*staging, error, control);
	if (!base.isOpen()) { return false; }
	// The base view preserves entry order. Locate each physical base record by
	// its stable reader slot rather than a name shared by multiple WAD lumps.
	PackageStagingModel basePlan = *staging;
	basePlan.m_operations.clear(); basePlan.m_baseConflicts.clear(); basePlan.invalidatePlan();
	if (!basePlan.preparePlan(error, control)) { return false; }
	const auto baseEntries = basePlan.plannedEntries();
	QHash<qsizetype, qsizetype> readerSlots;
	for (qsizetype i = 0; i < baseEntries.size(); ++i) {
		if (baseEntries.at(i).kind == PackageEntryKind::File) { readerSlots.insert(baseEntries.at(i).sourceReaderIndex, i); }
	}
	QJsonArray entries;
	for (auto& entry : saved.m_baseEntries) {
		if (cancelled(control, error)) { return false; }
		PackageFileIdentityPtr identity;
		if (!entry.unavailableReason.isEmpty()) { /* Keep the existing diagnostic without rereading its source. */ }
		else if (entry.sourceIdentity) { identity = objects.copy(entry.sourceIdentity, entry.virtualPath); }
		else if (entry.hasInlineBytes) {
			identity = objects.write(entry.inlineBytes.size(), entry.virtualPath, [&](const auto& sink) {
				for (qsizetype offset = 0; offset < entry.inlineBytes.size(); offset += PackageFileIdentity::chunkBytes) {
					if (!sink(QByteArrayView(entry.inlineBytes).sliced(offset, qMin<qsizetype>(PackageFileIdentity::chunkBytes, entry.inlineBytes.size() - offset)))) { return false; }
				}
				return true;
			});
		} else {
			const qsizetype index = readerSlots.value(entry.sourceReaderIndex, -1);
			if (index < 0) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Invalid base entry in the package draft.")); }
			bool unavailable = entry.sizeBytes > static_cast<quint64>(std::numeric_limits<qint64>::max());
			QString sourceError;
			if (!unavailable) {
				identity = objects.write(static_cast<qint64>(entry.sizeBytes), entry.virtualPath, [&](const auto& sink) {
					const bool read = base.streamEntryAt(index, sink, &sourceError, control.isCancelled);
					if (!read && error && error->isEmpty()) { *error = sourceError; } return read;
				}, &unavailable);
			}
			if (!identity && unavailable && knowCurrent && !currentOriginals.contains(entry.sourceReaderIndex) && !cancelled(control, error)) {
				entry.unavailableReason = sourceError.isEmpty()
					? QCoreApplication::translate("VibeStudioPackageDraft", "The original entry could not be read completely within the supported limits.") : sourceError.left(4096);
				if (!entry.unavailableReason.isEmpty() && entry.unavailableReason.back().isHighSurrogate()) { entry.unavailableReason.chop(1); }
				additionalUnavailableText += entry.unavailableReason.size() * qint64(2);
				if (additionalUnavailableText > saved.m_metadataLimits.maximumMetadataBytes - metadata.metadataBytes) {
					return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Unavailable original-content diagnostics exceed the document metadata limit."));
				}
				if (error) { error->clear(); }
			}
			if (!identity && entry.unavailableReason.isEmpty() && error && error->isEmpty()) {
				*error = sourceError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageDraft", "Invalid base entry in the package draft.") : sourceError;
			}
		}
		if (!identity && entry.unavailableReason.isEmpty()) { return false; }
		entry.sourceFilePath = identity ? identity->path : QString(); entry.sourceIdentity = identity;
		entry.inlineBytes.clear(); entry.hasInlineBytes = false;
		QJsonObject record{{QStringLiteral("path"), entry.virtualPath}, {QStringLiteral("basePath"), entry.baseVirtualPath},
			{QStringLiteral("size"), QString::number(entry.sizeBytes)}, {QStringLiteral("modified"), entry.modifiedUtc.toString(Qt::ISODateWithMs)},
			{QStringLiteral("ordinal"), entry.sourceOrdinal}, {QStringLiteral("readerIndex"), QString::number(entry.sourceReaderIndex)},
			{QStringLiteral("originOperation"), entry.operationId},
			{QStringLiteral("wadType"), entry.wadLumpType}};
		if (identity) { record.insert(QStringLiteral("object"), QString::fromLatin1(identity->sha256.toHex())); }
		else { record.insert(QStringLiteral("unavailable"), entry.unavailableReason); }
		entries.append(record);
	}
	QJsonArray directories;
	for (const auto& entry : saved.m_baseDirectories) {
		directories.append(QJsonObject{{QStringLiteral("path"), entry.virtualPath}, {QStringLiteral("basePath"), entry.baseVirtualPath}, {QStringLiteral("originOperation"), entry.operationId}, {QStringLiteral("modified"), entry.modifiedUtc.toString(Qt::ISODateWithMs)}});
	}
	QHash<QString, PackageStageOperation> registry;
	const auto freeze = [&](PackageStageOperation* operation) {
		if (registry.contains(operation->id)) { *operation = registry.value(operation->id); return true; }
		if (operation->hasInlineBytes) {
			const QByteArray bytes = operation->inlineBytes;
			operation->sourceIdentity = objects.write(bytes.size(), operation->virtualPath, [&](const auto& sink) {
				for (qsizetype offset = 0; offset < bytes.size(); offset += PackageFileIdentity::chunkBytes) {
					if (!sink(QByteArrayView(bytes).sliced(offset, qMin<qsizetype>(PackageFileIdentity::chunkBytes, bytes.size() - offset)))) { return false; }
				}
				return true;
			});
			if (!operation->sourceIdentity) { return false; }
			operation->sourceModifiedUtc = QDateTime::fromSecsSinceEpoch(315532800, QTimeZone(0));
			operation->hasInlineBytes = false; operation->inlineBytes.clear();
		} else if (operation->sourceIdentity) {
			operation->sourceIdentity = objects.copy(operation->sourceIdentity, operation->virtualPath);
			if (!operation->sourceIdentity) { return false; }
		}
		registry.insert(operation->id, *operation);
		return true;
	};
	QJsonArray currentIds;
	for (auto& operation : saved.m_operations) { if (!freeze(&operation)) { return false; } currentIds.append(operation.id); }
	QJsonArray history;
	for (auto& step : saved.m_history) {
		QJsonArray changes;
		for (auto& change : step.changes) {
			if (!freeze(&change.operation)) { return false; }
			changes.append(QJsonObject{{QStringLiteral("id"), change.operation.id}, {QStringLiteral("index"), QString::number(change.index)}, {QStringLiteral("inserted"), change.inserted}});
		}
		history.append(QJsonObject{{QStringLiteral("label"), step.label}, {QStringLiteral("before"), QString::number(step.beforeRevision)},
			{QStringLiteral("after"), QString::number(step.afterRevision)}, {QStringLiteral("changes"), changes}});
	}
	QJsonArray operations;
	QStringList ids = registry.keys(); ids.sort();
	for (const auto& id : ids) { operations.append(operationJson(registry.value(id))); }
	QJsonArray conflicts;
	for (const auto& conflict : saved.m_baseConflicts) {
		conflicts.append(QJsonObject{{QStringLiteral("id"), conflict.operationId}, {QStringLiteral("path"), conflict.virtualPath},
			{QStringLiteral("message"), conflict.message}, {QStringLiteral("blocking"), conflict.blocking}});
	}
	QJsonArray lumps;
	for (const auto& lump : saved.m_sourceWadLumps) {
		lumps.append(QJsonObject{{QStringLiteral("name"), lump.name}, {QStringLiteral("offset"), QString::number(lump.dataOffset)},
			{QStringLiteral("diskSize"), QString::number(lump.diskSizeBytes)}, {QStringLiteral("size"), QString::number(lump.sizeBytes)},
			{QStringLiteral("type"), lump.type}, {QStringLiteral("compression"), lump.compression}});
	}
	QJsonArray protectedInputs;
	for (const auto& input : saved.m_protectedInputPaths) { protectedInputs.append(input); }
	QJsonObject document{{QStringLiteral("type"), QStringLiteral("vibestudio-package-draft")}, {QStringLiteral("version"), 4},
		{QStringLiteral("protectedInputs"), protectedInputs},
		{QStringLiteral("source"), saved.m_sourcePath}, {QStringLiteral("format"), packageArchiveFormatId(saved.m_sourceFormat)},
		{QStringLiteral("wadMagic"), saved.m_sourceWadMagic}, {QStringLiteral("base"), entries}, {QStringLiteral("directories"), directories},
		{QStringLiteral("conflicts"), conflicts}, {QStringLiteral("wadLumps"), lumps}, {QStringLiteral("operations"), operations},
		{QStringLiteral("current"), currentIds}, {QStringLiteral("history"), history}, {QStringLiteral("cursor"), static_cast<int>(saved.m_historyCursor)},
		{QStringLiteral("operationSerial"), QString::number(saved.m_operationSerial)}, {QStringLiteral("revisionSerial"), QString::number(saved.m_revisionSerial)},
		{QStringLiteral("revision"), QString::number(saved.m_revision)}};
	if (!recovery.isEmpty()) { document.insert(QStringLiteral("recovery"), recovery); }
	const QByteArray json = QJsonDocument(document).toJson(QJsonDocument::Indented);
	qsizetype recordCount = protectedInputs.size() + entries.size() + directories.size() + conflicts.size() + lumps.size() + operations.size() + currentIds.size() + history.size();
	for (const auto& step : saved.m_history) { recordCount += step.changes.size(); }
	if (json.size() > maximumManifestBytes || recordCount > maximumRecords) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "The package draft exceeds its document metadata limit."));
	}
	const auto committedSha256 = QCryptographicHash::hash(json, QCryptographicHash::Sha256);
	const bool unchanged = original && original->sha256 == committedSha256;
	if (!unchanged && ((maximumAdditionalBytes >= 0 && json.size() > maximumAdditionalBytes) || remainingFiles == 0)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "The package storage limit leaves insufficient space to commit the manifest. The committed document is unchanged."));
	}
	// Recheck source membership as well as all captured bytes before commit.
	if (staging->m_baseReader && !staging->m_baseReader->verifySourceIdentity(error, control)) { return false; }
	if (!objects.verifyAll()) { return false; }
	if (cancelled(control, error) || !noLinks(manifestPath, error) || (access && !access->matchesDirectory())) { return false; }
	if (original && !verifyPackageFileIdentity(original, error, control)) { return false; }
	// Account for the destination document identity as well as its new objects
	// before publishing the manifest. Keep the hash we intend to commit even if
	// an external writer immediately changes the file after publication.
	auto committedIdentity = std::make_shared<PackageFileIdentity>();
	committedIdentity->path = manifestPath; committedIdentity->resolvedPath = prospectiveResolvedPath(manifestPath);
	committedIdentity->draftAccess = access; committedIdentity->storageDirectory = root;
	committedIdentity->sha256 = committedSha256;
	saved.m_draftIdentity = std::move(committedIdentity); saved.m_draftPath = root;
	saved.m_baseReader.reset(); saved.invalidatePlan();
	if (!saved.contentUsage(&retained, error, control) || !saved.metadataUsage(&metadata, error, control)
		|| !baseArchive(saved, error, control).isOpen()) { return false; }
	if (dryRun) { return true; }
	if (unchanged) {
		// Verification still runs, but an identical manifest needs no transient
		// file or additional bytes, including after a limit has been lowered.
	} else if (exists) {
		QSaveFile file(manifestPath); file.setDirectWriteFallback(false);
		if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Unable to commit the package draft: %1").arg(file.errorString())); }
	} else {
		QTemporaryFile file(QDir(root).filePath(QStringLiteral(".document-XXXXXX")));
		if (!file.open() || file.write(json) != json.size() || !file.flush() || !file.rename(manifestPath)) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Unable to create the package draft: %1").arg(file.errorString())); }
		file.setAutoRemove(false);
	}
	// The committed state reads only independent blobs. Source labels remain
	// provenance and overwrite protection, never a path used to reopen content.
	saved.markSaved(); saved.invalidatePlan();
	*staging = std::move(saved);
	return true;
}

bool PackageDraft::load(const QString& directory, PackageStagingModel* staging, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	if (!staging) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "A package staging model is required.")); }
	PackageStagingModel loaded(staging->m_contentLimits, staging->m_metadataLimits, staging->m_planLimits, staging->m_viewLimits);
	PackageStagingContentUsage retained;
	PackageStagingMetadataUsage metadata;
	// Policy validation is constant work, not a plan-preparation progress phase.
	// Preserve storage-review callbacks that describe actual verified files.
	PackagePlanWork policy({}, error, {});
	PackagePlanBudget planBudget(staging->m_planLimits, policy);
	if (!loaded.contentUsage(&retained, error, control) || !loaded.metadataUsage(&metadata, error, control)
		|| !planBudget.validate() || !validPackageIndexLimits(staging->m_viewLimits, error)) { return false; }
	QString root;
	if (!draftDirectory(directory, &root, error) || cancelled(control, error)) { return false; }
	const QString manifestPath = QDir(root).filePath(QStringLiteral("document.json"));
	if (!noLinks(manifestPath, error) || QFileInfo(manifestPath).size() > maximumManifestBytes) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "The package draft metadata is unsafe or too large.")); }
	const auto access = PackageDraftAccess::acquire(root, PackageDraftAccess::Mode::Read, error);
	if (!access) { return false; }
	const auto identity = protectDraftIdentity(capturePackageFileIdentity(manifestPath, error, control, (maximumManifestBytes / PackageFileIdentity::chunkBytes) * 32), access, root);
	if (!identity) { return false; }
	PackageContentDevice file(identity);
	if (!file.open()) { return fail(error, file.errorString()); }
	const QByteArray bytes = file.readAll();
	if (file.failed() || bytes.size() != identity->size) { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Unable to read the complete package draft.")); }
	QJsonParseError parseError;
	const auto json = QJsonDocument::fromJson(bytes, &parseError);
	const QJsonObject document = json.object();
	const auto invalid = [&]() { return fail(error, QCoreApplication::translate("VibeStudioPackageDraft", "Invalid package draft metadata or undo history.")); };
	const int version = document.value(QStringLiteral("version")).toInt(-1);
	if (parseError.error != QJsonParseError::NoError || !json.isObject() || document.value(QStringLiteral("type")) != QStringLiteral("vibestudio-package-draft")
		|| (version != 1 && version != 2 && version != 3 && version != 4)) { return invalid(); }
	qsizetype recordCount = 0;
	for (const char* field : {"base", "directories", "conflicts", "wadLumps", "operations", "current", "history"}) {
		const auto value = document.value(QLatin1String(field));
		if (!value.isArray()) { return invalid(); }
		recordCount += value.toArray().size();
		if (recordCount > maximumRecords) { return invalid(); }
	}
	if (version >= 4 || document.contains(QStringLiteral("protectedInputs"))) {
		const auto value = document.value(QStringLiteral("protectedInputs"));
		if (!value.isArray()) { return invalid(); }
		const auto paths = value.toArray(); recordCount += paths.size();
		if (recordCount > maximumRecords) { return invalid(); }
		PackageInputProtectionSet inputs(loaded.m_metadataLimits.maximumRecords, loaded.m_metadataLimits.maximumMetadataBytes, error, control);
		for (const auto& path : paths) {
			if (!path.isString() || !PackageInputProtectionSet::validStoredPath(path.toString())) { return invalid(); }
			if (!inputs.add(path.toString(), false)) { return false; }
		}
		if (!inputs.finish()) { return false; }
		loaded.m_protectedInputPaths = inputs.paths();
	}
	loaded.m_sourcePath = document.value(QStringLiteral("source")).toString();
	const QString format = document.value(QStringLiteral("format")).toString();
	loaded.m_sourceFormat = packageArchiveFormatFromId(format);
	if (loaded.m_sourceFormat == PackageArchiveFormat::Unknown || packageArchiveFormatId(loaded.m_sourceFormat) != format) { return invalid(); }
	loaded.m_sourceWadMagic = document.value(QStringLiteral("wadMagic")).toString();
	if (loaded.m_sourceFormat == PackageArchiveFormat::Wad && !QStringList{QStringLiteral("PWAD"), QStringLiteral("IWAD"), QStringLiteral("WAD2"), QStringLiteral("WAD3")}.contains(loaded.m_sourceWadMagic)) { return invalid(); }
	if (!readUnsigned(document.value(QStringLiteral("operationSerial")), &loaded.m_operationSerial)
		|| !readUnsigned(document.value(QStringLiteral("revisionSerial")), &loaded.m_revisionSerial)
		|| !readUnsigned(document.value(QStringLiteral("revision")), &loaded.m_revision)
		|| loaded.m_revision > loaded.m_revisionSerial || loaded.m_revisionSerial > PackageStagingModel::maximumSerial
		|| loaded.m_operationSerial > PackageStagingModel::maximumSerial) { return invalid(); }
	ObjectStore objects(root, control, error, false, nullptr, access, nullptr, loaded.m_contentLimits.maximumFingerprintBytes);
	// Parse and admit metadata/history before capturing any payload. References
	// carry prospective identity paths; only the final hydration reads content.
	QHash<QString, PackageFileIdentityPtr> references;
	const auto reference = [&](const QString& name) -> PackageFileIdentityPtr {
		if (cancelled(control, error)) { return {}; }
		if (!validObjectName(name)) { invalid(); return {}; }
		if (references.contains(name)) { return references.value(name); }
		const QString path = QDir(root).filePath(QStringLiteral("objects/") + name);
		if (!noLinks(path, error)) { return {}; }
		auto value = std::make_shared<PackageFileIdentity>();
		value->path = path; value->resolvedPath = QFileInfo(path).canonicalFilePath(); value->storageDirectory = root; value->draftAccess = access;
		references.insert(name, value); return value;
	};
	for (const auto value : document.value(QStringLiteral("base")).toArray()) {
		if (!value.isObject()) { return invalid(); }
		const auto object = value.toObject();
		PackageStagedEntry entry;
		entry.virtualPath = object.value(QStringLiteral("path")).toString();
		entry.baseVirtualPath = object.value(QStringLiteral("basePath")).toString();
		entry.modifiedUtc = QDateTime::fromString(object.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
		entry.sourceOrdinal = object.value(QStringLiteral("ordinal")).toInt(-1);
		entry.operationId = object.value(QStringLiteral("originOperation")).toString();
		bool indexOk = false;
		entry.sourceReaderIndex = object.value(QStringLiteral("readerIndex")).toString().toLongLong(&indexOk);
		const int wadType = object.value(QStringLiteral("wadType")).toInt(-1);
		if (entry.virtualPath.isEmpty() || !indexOk || entry.sourceReaderIndex < -1 || wadType < 0 || wadType > 255
			|| !readUnsigned(object.value(QStringLiteral("size")), &entry.sizeBytes)) { return invalid(); }
		entry.wadLumpType = static_cast<quint8>(wadType); entry.source = QStringLiteral("base");
		if (object.contains(QStringLiteral("unavailable"))) {
			entry.unavailableReason = object.value(QStringLiteral("unavailable")).toString();
			if (version < 3 || object.contains(QStringLiteral("object")) || entry.unavailableReason.isEmpty()
				|| entry.unavailableReason.size() > 4096 || !entry.unavailableReason.isValidUtf16() || entry.unavailableReason.contains(QChar(0))) { return invalid(); }
		} else {
			entry.sourceIdentity = reference(object.value(QStringLiteral("object")).toString());
			if (!entry.sourceIdentity) { return false; }
			entry.sourceFilePath = entry.sourceIdentity->path;
		}
		loaded.m_baseEntries.append(std::move(entry));
	}
	for (const auto value : document.value(QStringLiteral("directories")).toArray()) {
		const auto object = value.toObject();
		PackageStagedEntry entry;
		entry.kind = PackageEntryKind::Directory; entry.virtualPath = object.value(QStringLiteral("path")).toString();
		entry.baseVirtualPath = object.value(QStringLiteral("basePath")).toString();
		entry.modifiedUtc = QDateTime::fromString(object.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
		entry.source = QStringLiteral("base-directory");
		entry.operationId = object.value(QStringLiteral("originOperation")).toString();
		if (entry.virtualPath.isEmpty()) { return invalid(); }
		loaded.m_baseDirectories.append(entry);
	}
	for (const auto value : document.value(QStringLiteral("conflicts")).toArray()) {
		const auto object = value.toObject();
		if (!object.value(QStringLiteral("blocking")).isBool()) { return invalid(); }
		loaded.m_baseConflicts.append({object.value(QStringLiteral("id")).toString(), object.value(QStringLiteral("path")).toString(),
			object.value(QStringLiteral("message")).toString(), object.value(QStringLiteral("blocking")).toBool()});
	}
	for (const auto value : document.value(QStringLiteral("wadLumps")).toArray()) {
		const auto object = value.toObject();
		PackageWadLumpLocation lump;
		lump.name = object.value(QStringLiteral("name")).toString();
		bool offsetOk = false, diskOk = false, sizeOk = false;
		lump.dataOffset = object.value(QStringLiteral("offset")).toString().toLongLong(&offsetOk);
		lump.diskSizeBytes = object.value(QStringLiteral("diskSize")).toString().toLongLong(&diskOk);
		lump.sizeBytes = object.value(QStringLiteral("size")).toString().toLongLong(&sizeOk);
		const int type = object.value(QStringLiteral("type")).toInt(-1), compression = object.value(QStringLiteral("compression")).toInt(-1);
		if (!offsetOk || !diskOk || !sizeOk || lump.dataOffset < 0 || lump.diskSizeBytes < 0 || lump.sizeBytes < 0 || type < 0 || type > 255 || compression < 0 || compression > 255) { return invalid(); }
		lump.type = static_cast<quint8>(type); lump.compression = static_cast<quint8>(compression);
		loaded.m_sourceWadLumps.append(lump);
	}
	QHash<QString, PackageStageOperation> registry;
	for (const auto value : document.value(QStringLiteral("operations")).toArray()) {
		const auto object = value.toObject();
		PackageStageOperation operation;
		operation.id = object.value(QStringLiteral("id")).toString();
		quint64 serial = 0;
		if (!operation.id.startsWith(QStringLiteral("stage-")) || !readUnsigned(operation.id.mid(6), &serial) || serial == 0
			|| serial > loaded.m_operationSerial || registry.contains(operation.id)) { return invalid(); }
		const QString type = object.value(QStringLiteral("type")).toString();
		operation.type = packageStageOperationTypeFromId(type);
		if (version >= 2) {
			const auto ordinal = object.value(QStringLiteral("sourceOrdinal"));
			operation.sourceOrdinal = ordinal.toInt(-2);
			if (!ordinal.isDouble() || operation.sourceOrdinal < -1
				|| ordinal.toDouble() != operation.sourceOrdinal
				|| (operation.sourceOrdinal >= 0 && operation.type == PackageStageOperationType::Add)) { return invalid(); }
		} else if (object.contains(QStringLiteral("sourceOrdinal"))) { return invalid(); }
		operation.sourceTreeIdentity = object.value(QStringLiteral("sourceTreeIdentity")).toString();
		if (isPackageDirectoryOperation(operation.type)) {
			if (version < 2 || operation.sourceOrdinal != -1 || loaded.m_sourceFormat == PackageArchiveFormat::Wad
				|| (operation.type != PackageStageOperationType::CreateDirectory && (operation.sourceTreeIdentity.size() != 64
					|| QByteArray::fromHex(operation.sourceTreeIdentity.toLatin1()).toHex() != operation.sourceTreeIdentity.toLatin1()))) { return invalid(); }
		}
		const QString resolution = object.value(QStringLiteral("resolution")).toString();
		operation.conflictResolution = packageStageConflictResolutionFromId(resolution);
		if (packageStageOperationTypeId(operation.type) != type || packageStageConflictResolutionId(operation.conflictResolution) != resolution) { return invalid(); }
		operation.virtualPath = object.value(QStringLiteral("path")).toString();
		operation.targetVirtualPath = object.value(QStringLiteral("target")).toString();
		operation.sourceFilePath = object.value(QStringLiteral("source")).toString();
		operation.sourceError = object.value(QStringLiteral("error")).toString();
		operation.wadLumpType = object.value(QStringLiteral("wadType")).toInt(-1);
		operation.wadInsertBefore = object.value(QStringLiteral("wadInsertBefore")).toString();
		operation.wadNamespace = object.value(QStringLiteral("wadNamespace")).toString();
		if (object.contains(QStringLiteral("wadNamespace")) && (!object.value(QStringLiteral("wadNamespace")).isString()
			|| (!operation.wadNamespace.isEmpty() && (!QStringList{QStringLiteral("flat"), QStringLiteral("patch"), QStringLiteral("sprite"), QStringLiteral("global")}.contains(operation.wadNamespace)
				|| loaded.m_sourceFormat != PackageArchiveFormat::Wad)))) { return invalid(); }
		if ((object.contains(QStringLiteral("wadType")) && (!object.value(QStringLiteral("wadType")).isDouble()
			|| object.value(QStringLiteral("wadType")).toDouble() != operation.wadLumpType))
			|| operation.wadLumpType < -1 || operation.wadLumpType > 255
			|| (object.contains(QStringLiteral("wadInsertBefore")) && !object.value(QStringLiteral("wadInsertBefore")).isString())) { return invalid(); }
		if (!operation.wadInsertBefore.isEmpty()) {
			bool opens = false;
			if (doomNamespaceMarker(operation.wadInsertBefore, &opens).isEmpty() || opens
				|| loaded.m_sourceFormat != PackageArchiveFormat::Wad) { return invalid(); }
		}
		operation.sourceModifiedUtc = QDateTime::fromString(object.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
		const QString name = object.value(QStringLiteral("object")).toString();
		if (!name.isEmpty()) { operation.sourceIdentity = reference(name); if (!operation.sourceIdentity) { return false; } }
		if ((operation.type == PackageStageOperationType::Add || operation.type == PackageStageOperationType::Replace)
			&& !operation.sourceIdentity && operation.sourceError.isEmpty()) { return invalid(); }
		registry.insert(operation.id, operation);
	}
	QSet<QString> current;
	for (const auto id : document.value(QStringLiteral("current")).toArray()) {
		if (!id.isString() || !registry.contains(id.toString()) || current.contains(id.toString())) { return invalid(); }
		current.insert(id.toString()); loaded.m_operations.append(registry.value(id.toString()));
	}
	for (const auto value : document.value(QStringLiteral("history")).toArray()) {
		const auto object = value.toObject();
		PackageStageHistoryStep step;
		step.label = object.value(QStringLiteral("label")).toString();
		if (!readUnsigned(object.value(QStringLiteral("before")), &step.beforeRevision) || !readUnsigned(object.value(QStringLiteral("after")), &step.afterRevision)
			|| step.beforeRevision >= step.afterRevision || step.afterRevision > loaded.m_revisionSerial || !object.value(QStringLiteral("changes")).isArray()) { return invalid(); }
		const auto changes = object.value(QStringLiteral("changes")).toArray();
		recordCount += changes.size();
		if (changes.isEmpty() || recordCount > maximumRecords || (!loaded.m_history.isEmpty() && loaded.m_history.last().afterRevision != step.beforeRevision)) { return invalid(); }
		for (const auto changeValue : changes) {
			const auto change = changeValue.toObject();
			quint64 index = 0;
			const QString id = change.value(QStringLiteral("id")).toString();
			if (!registry.contains(id) || !readUnsigned(change.value(QStringLiteral("index")), &index) || index > static_cast<quint64>(maximumRecords) || !change.value(QStringLiteral("inserted")).isBool()) { return invalid(); }
			step.changes.append({registry.value(id), static_cast<qsizetype>(index), change.value(QStringLiteral("inserted")).toBool()});
		}
		loaded.m_history.append(std::move(step));
	}
	loaded.m_historyCursor = document.value(QStringLiteral("cursor")).toInt(-1);
	if (loaded.m_history.size() > 256 || loaded.m_historyCursor < 0 || loaded.m_historyCursor > loaded.m_history.size()) { return invalid(); }
	if (!loaded.m_history.isEmpty()) {
		const quint64 expected = loaded.m_historyCursor == 0 ? loaded.m_history.first().beforeRevision : loaded.m_history.at(loaded.m_historyCursor - 1).afterRevision;
		if (expected != loaded.m_revision) { return invalid(); }
	}
	// Validate every reachable undo AND redo delta before exposing the model.
	// Malformed indexes must never reach QVector::removeAt/insert in the UI.
	QStringList state;
	QSet<QString> present;
	for (const auto& operation : loaded.m_operations) { state.append(operation.id); present.insert(operation.id); }
	const auto replay = [&](const PackageStageHistoryStep& step, bool forward) {
		const auto apply = [&](const PackageStageHistoryChange& change) {
			if (cancelled(control, error)) { return false; }
			const bool insert = change.inserted == forward;
			if (change.index < 0 || change.index > state.size() || (!insert && (change.index == state.size() || state.at(change.index) != change.operation.id))) { return false; }
			if (insert) { if (present.contains(change.operation.id)) { return false; } state.insert(change.index, change.operation.id); present.insert(change.operation.id); }
			else { state.removeAt(change.index); present.remove(change.operation.id); }
			return true;
		};
		if (forward) { for (const auto& change : step.changes) { if (!apply(change)) { return false; } } }
		else { for (auto it = step.changes.crbegin(); it != step.changes.crend(); ++it) { if (!apply(*it)) { return false; } } }
		return true;
	};
	for (qsizetype i = loaded.m_historyCursor; i > 0; --i) { if (!replay(loaded.m_history.at(i - 1), false)) { return invalid(); } }
	for (const auto& step : loaded.m_history) { if (!replay(step, true)) { return invalid(); } }
	if (cancelled(control, error) || !access->matchesDirectory() || !verifyPackageFileIdentity(identity, error, control)) { return false; }
	loaded.m_loaded = true; loaded.m_draftPath = root; loaded.m_draftIdentity = identity; loaded.markSaved(); loaded.invalidatePlan();
	if (!loaded.metadataUsage(&metadata, error, control)) { return false; }
	QSet<QString> reachable;
	for (const auto& operation : loaded.m_operations) { reachable.insert(operation.id); }
	for (const auto& step : loaded.m_history) { for (const auto& change : step.changes) { reachable.insert(change.operation.id); } }
	if (reachable.size() != registry.size()) { return invalid(); }
	const auto hydrate = [&](PackageFileIdentityPtr* value) {
		if (!*value) { return true; }
		*value = objects.read(QFileInfo((*value)->path).fileName()); return static_cast<bool>(*value);
	};
	for (auto& entry : loaded.m_baseEntries) {
		if (!hydrate(&entry.sourceIdentity)) { return false; }
		if (entry.sourceIdentity && entry.sizeBytes != static_cast<quint64>(entry.sourceIdentity->size)) { return invalid(); }
	}
	for (auto it = registry.begin(); it != registry.end(); ++it) { if (!hydrate(&it->sourceIdentity)) { return false; } }
	for (auto& operation : loaded.m_operations) { operation = registry.value(operation.id); }
	for (auto& step : loaded.m_history) { for (auto& change : step.changes) { change.operation = registry.value(change.operation.id); } }
	if (cancelled(control, error) || !access->matchesDirectory() || !verifyPackageFileIdentity(identity, error, control)) { return false; }
	loaded.invalidatePlan();
	if (!loaded.contentUsage(&retained, error, control) || !loaded.metadataUsage(&metadata, error, control)) { return false; }
	*staging = std::move(loaded);
	return true;
}

} // namespace vibestudio
