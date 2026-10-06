#include "core/package_copy.h"
#include "core/package_copy_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QSet>

namespace vibestudio {
namespace {
QString text(const char* message) { return QCoreApplication::translate("VibeStudioPackageCopy", message); }
QString key(const QString& path) { return path.normalized(QString::NormalizationForm_C).toCaseFolded(); }
bool hasParent(const QString& path, const QSet<QString>& directories)
{
	for (QString parent = packageVirtualPathParent(path); !parent.isEmpty(); parent = packageVirtualPathParent(parent)) {
		if (directories.contains(parent)) { return true; }
	}
	return false;
}
bool safeParent(const QString& path)
{
	if (path.isEmpty() || !QDir::isAbsolutePath(path) || !QFileInfo(path).isDir()) { return false; }
	for (QString probe = QDir::cleanPath(path); !probe.isEmpty();) {
		const QFileInfo info(probe);
		if (info.isSymLink() || info.isJunction() || !info.isDir()) { return false; }
		const QString parent = info.absolutePath();
		if (parent == probe) { break; }
		probe = parent;
	}
	return true;
}
} // namespace

// Keep the existing reservation tokens pending until the consumer accepts the
// batch. Cleanup failure retains both charges instead of understating storage.
class PackageCopyPreparation final {
public:
	std::shared_ptr<QTemporaryDir> storage;
	std::unique_ptr<PackageCopyReservation> reservation;
	std::unique_ptr<PackageCopyStoreReservation> sharedReservation;
	bool pending = true, published = false;
	~PackageCopyPreparation()
	{
		if (pending) { const auto error = discard(); if (!error.isEmpty()) { qWarning().noquote() << error; } }
	}
	void publish()
	{
		if (!pending) { return; }
		if (reservation) { reservation->retain(); }
		if (sharedReservation) { sharedReservation->retain(); }
		reservation.reset(); sharedReservation.reset(); storage.reset();
		pending = false; published = true;
	}
	QString discard()
	{
		if (!pending) { return {}; }
		QString error;
		if (!storage->remove()) {
			storage->setAutoRemove(false);
			if (reservation) { reservation->retain(true); }
			if (sharedReservation) { sharedReservation->retain(true); }
			error = text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "Temporary copy cleanup failed; files may remain in %1.")).arg(storage->path());
			if (reservation) { error += QLatin1Char('\n') + text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "The full reservation remains charged for this session.")); }
		}
		reservation.reset(); sharedReservation.reset(); storage.reset(); pending = false;
		return error;
	}
};

bool PackageCopyResult::prepared() const
{
	return m_preparation && m_preparation->pending && storage && !paths.isEmpty() && error.isEmpty() && !cancelled;
}
bool PackageCopyResult::succeeded() const
{
	return (!m_preparation || m_preparation->published) && storage && !paths.isEmpty() && error.isEmpty() && !cancelled;
}
bool publishPreparedPackageCopies(PackageCopyResult* result)
{
	if (!result || !result->prepared()) { return result && result->succeeded(); }
	result->m_preparation->publish(); return result->succeeded();
}
bool discardPreparedPackageCopies(PackageCopyResult* result)
{
	if (!result || !result->m_preparation || !result->m_preparation->pending) { return false; }
	const auto error = result->m_preparation->discard();
	result->cancelled = true; result->paths.clear(); result->storage.reset(); result->session.reset();
	if (!error.isEmpty()) { if (!result->error.isEmpty()) { result->error += QLatin1Char('\n'); } result->error += error; }
	return error.isEmpty();
}

bool packageCopyStorageContainsPath(const QString& directory, const QString& path)
{
	if (directory.isEmpty() || path.isEmpty()) { return false; }
	const QString root = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
	const QString candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
	constexpr auto sensitivity = Qt::CaseInsensitive;
#else
	constexpr auto sensitivity = Qt::CaseSensitive;
#endif
	// A lexical child is disposable even if a link points out of the tree.
	// Resolved containment also catches aliases into it, including junctions.
	return candidate.compare(root, sensitivity) == 0
		|| candidate.startsWith(root + QLatin1Char('/'), sensitivity)
		|| packagePathIsInsideDirectory(directory, path);
}

PackageCopyResult preparePackageCopyEntries(const PackageArchiveReader& source, const PackageCopyRequest& request)
{
	PackageCopyResult result;
	const auto cancelled = [&] { return request.control.isCancelled && request.control.isCancelled(); };
	const auto fail = [&](const QString& error) { result.error = error; return result; };
	if (cancelled()) { result.cancelled = true; return result; }
	if (!source.isOpen() && !source.errorString().isEmpty()) { return fail(source.errorString()); }
	if (!source.isOpen() || request.entryIndexes.isEmpty()) {
		return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "Select package entries to copy.")));
	}
	if (request.maximumFiles <= 0 || request.maximumEntries <= 0 || request.entryIndexes.size() > request.maximumEntries) {
		return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "The copy selection exceeds its entry limit. Use Extract Selected for larger selections.")));
	}
	const auto entries = source.entries();
	QSet<qsizetype> roots;
	QSet<QString> directories;
	for (const qsizetype index : request.entryIndexes) {
		if (cancelled()) { result.cancelled = true; return result; }
		if (index < 0 || index >= entries.size() || !isSafePackageVirtualPath(entries.at(index).virtualPath)) {
			return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "The selected package entry is missing or has an unsafe path.")));
		}
		roots.insert(index);
		if (entries.at(index).kind == PackageEntryKind::Directory) { directories.insert(key(entries.at(index).virtualPath)); }
	}
	PackageExtractionRequest extraction;
	extraction.control = request.control;
	qsizetype selectedCount = 0; QSet<QString> payloadEntries;
	for (qsizetype index = 0; index < entries.size(); ++index) {
		if (cancelled()) { result.cancelled = true; return result; }
		const auto& entry = entries.at(index);
		const QString name = key(entry.virtualPath);
		if (!roots.contains(index) && !directories.contains(name) && !hasParent(name, directories)) { continue; }
		if (++selectedCount > request.maximumEntries) {
			return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "The copy selection exceeds its entry limit. Use Extract Selected for larger selections.")));
		}
		int parents = entry.kind == PackageEntryKind::Directory ? 1 : 0;
		for (QString path = entry.virtualPath; !path.isEmpty(); path = packageVirtualPathParent(path)) {
			payloadEntries.insert(path);
			if (payloadEntries.size() > request.maximumEntries) {
				return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "The copy selection exceeds its entry limit. Use Extract Selected for larger selections.")));
			}
			if (!packageVirtualPathParent(path).isEmpty()) { ++parents; }
		}
		if ((request.session || !request.storeDirectory.isEmpty()) && parents >= PackageCopyStoreDepthLimit) {
			return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "Temporary copy paths exceed the managed storage depth limit. Use Extract Selected for this layout.")));
		}
		if (entry.kind != PackageEntryKind::File) { continue; }
		if (++result.fileCount > request.maximumFiles || entry.sizeBytes > request.maximumBytes - result.totalBytes) {
			return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "The copy selection exceeds its file or byte limit. Use Extract Selected for larger selections.")));
		}
		result.totalBytes += entry.sizeBytes;
		extraction.entrySelections << PackageExtractionSelection{index, {}};
	}
	QStringList relativeRoots;
	QSet<qsizetype> emitted;
	for (const qsizetype index : request.entryIndexes) {
		if (emitted.contains(index)) { continue; }
		emitted.insert(index);
		const auto& entry = entries.at(index);
		if (hasParent(key(entry.virtualPath), directories)) { continue; }
		relativeRoots << entry.virtualPath;
		if (entry.kind == PackageEntryKind::Directory) { extraction.virtualPaths << entry.virtualPath; }
	}
	if (cancelled()) { result.cancelled = true; return result; }
	const qsizetype reservedEntries = qMax(selectedCount, payloadEntries.size());
	std::unique_ptr<PackageCopyReservation> reservation;
	if (request.budget) {
		reservation = request.budget->reserve(result.totalBytes, result.fileCount, reservedEntries, request.sessionLimits, &result.error);
		if (!reservation) { return result; }
	}
	auto session = request.session;
	if (!session && !request.storeDirectory.isEmpty()) {
		session = PackageCopySession::create(request.storeDirectory, &result.error, request.control);
		if (!session) { result.cancelled = cancelled(); return result; }
	}
	result.session = session;
	if (session && (!session->isValid() || (!request.parentDirectory.isEmpty()
		&& QDir::cleanPath(request.parentDirectory) != QDir::cleanPath(session->path())))) {
		return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "The temporary copy session changed or does not own the requested directory.")));
	}
	const QString parentDirectory = session ? session->path() : request.parentDirectory;
	if (!safeParent(parentDirectory)) {
		return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "Package copies require an existing session directory without symbolic links or junctions.")));
	}
	std::unique_ptr<PackageCopyStoreReservation> sharedReservation;
	if (session) {
		sharedReservation = session->reserve(result.totalBytes, result.fileCount, reservedEntries, &result.error, request.control);
		if (!sharedReservation) { result.cancelled = cancelled(); return result; }
	}
	// A storage lease also retains its managed session. Keeping only the
	// returned directory lease therefore still protects paths and native ownership.
	auto storage = std::shared_ptr<QTemporaryDir>(new QTemporaryDir(QDir(parentDirectory).filePath(QStringLiteral("package-copy-XXXXXX"))),
		[session](QTemporaryDir* directory) { delete directory; });
	if (session) { storage->setAutoRemove(false); }
	if (!storage->isValid()) {
		return fail(text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "Unable to create a temporary package copy directory.")));
	}
	const auto discard = [&] {
		if (!storage->remove()) {
			storage->setAutoRemove(false);
			if (reservation) { reservation->retain(true); }
			if (sharedReservation) { sharedReservation->retain(true); }
			if (!result.error.isEmpty()) { result.error += QLatin1Char('\n'); }
			result.error += text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "Temporary copy cleanup failed; files may remain in %1.")).arg(storage->path());
			if (reservation) { result.error += QLatin1Char('\n') + text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "The full reservation remains charged for this session.")); }
		}
		return result;
	};
	extraction.targetDirectory = storage->path();
	result.extraction = extractPackageEntries(source, extraction);
	result.cancelled = result.extraction.cancelled || cancelled();
	if (result.cancelled) { return discard(); }
	if (!result.extraction.succeeded() || result.extraction.skippedCount || result.extraction.processedCount != selectedCount) {
		result.error = text(QT_TRANSLATE_NOOP("VibeStudioPackageCopy", "Package copies were not completed; no files were handed off. %1"))
			.arg(result.extraction.warnings.join(QLatin1Char('\n')));
		return discard();
	}
	for (const auto& path : relativeRoots) { result.paths << QDir(storage->path()).filePath(path); }
	result.m_preparation = std::make_shared<PackageCopyPreparation>();
	result.m_preparation->storage = storage;
	result.m_preparation->reservation = std::move(reservation);
	result.m_preparation->sharedReservation = std::move(sharedReservation);
	result.storage = std::move(storage);
	return result;
}

PackageCopyResult copyPackageEntries(const PackageArchiveReader& source, const PackageCopyRequest& request)
{
	auto result = preparePackageCopyEntries(source, request);
	if (result.prepared()) {
		if (request.control.isCancelled && request.control.isCancelled()) { discardPreparedPackageCopies(&result); }
		else { publishPreparedPackageCopies(&result); }
	}
	return result;
}

} // namespace vibestudio
