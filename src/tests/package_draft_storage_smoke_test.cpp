#include "core/package_draft_storage.h"
#include "core/package_draft.h"
#include "core/package_draft_access.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; } return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QString objectPath(const QString& draft, const QByteArray& bytes)
{
	return QDir(draft).filePath(QStringLiteral("objects/") + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); const QString path = root.filePath(QStringLiteral("draft.vibepackage"));
	const QString manifest = QDir(path).filePath(QStringLiteral("document.json")); QString error; bool ok = true;
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
	ok &= plan.addBytes("retained", QStringLiteral("first.bin"), &error);
	ok &= plan.addBytes("redo retained", QStringLiteral("redo.bin"), &error); ok &= plan.undo();
	if (!expect(PackageDraft::save(path, &plan, false, &error), "prepare draft and redo branch", error)) { return 1; }
	const auto metadata = read(manifest);
	const QString orphan = objectPath(path, "unused"), interrupted = QDir(path).filePath(QStringLiteral("objects/.writing-Ab1234"));
	ok &= write(orphan, "unused") && write(interrupted, "partial");
	auto review = reviewPackageDraftStorage(path);
	ok &= expect(review.complete() && review.referencedObjects == 2 && review.reclaimable.size() == 2
		&& review.reclaimableBytes == 13 && review.storage.bytes == review.retainedBytes + 13,
		"review retains undo/redo objects and counts only unused data", review.error);
	const auto before = inspectPackageStorage(path).fingerprint;
	auto result = compactPackageDraftStorage(path, review.storage.fingerprint);
	ok &= expect(result.succeeded && result.dryRun && !result.readerExclusion && result.reclaimedBytes == 13
		&& inspectPackageStorage(path).fingerprint == before && !QFileInfo::exists(QDir(path).filePath(QStringLiteral(".write.lock"))),
		"compaction dry run writes no files and leaves reader exclusion unproven", result.error);
	result = compactPackageDraftStorage(path, review.storage.fingerprint, false);
	ok &= expect(!result.succeeded && QFileInfo::exists(orphan) && QFileInfo::exists(interrupted), "live document protection prevents all compaction deletions", result.error);
	auto reader = std::make_unique<PackageStagingArchive>(plan); plan.clear();
	result = compactPackageDraftStorage(path, review.storage.fingerprint, false);
	ok &= expect(!result.succeeded && QFileInfo::exists(orphan), "background snapshot keeps draft storage protected", result.error);
	reader.reset();
	result = compactPackageDraftStorage(path, review.storage.fingerprint, false);
	ok &= expect(result.succeeded && result.readerExclusion && result.reclaimedFiles == 2 && result.reclaimedBytes == 13
		&& !QFileInfo::exists(orphan) && !QFileInfo::exists(interrupted) && read(manifest) == metadata, "closed draft compaction removes only reviewed unreachable files", result.error);
	ok &= expect(PackageDraft::load(path, &plan, &error) && plan.redo(), "compacted draft keeps redo history", error);
	{
		QByteArray bytes; PackageStagingArchive restored(plan);
		ok &= expect(restored.readEntryBytes(QStringLiteral("redo.bin"), &bytes, &error) && bytes == "redo retained", "redo bytes survive compaction", error);
	}
	plan.clear();
	ok &= write(orphan, "unused"); review = reviewPackageDraftStorage(path); ok &= write(orphan, "changed unused");
	result = compactPackageDraftStorage(path, review.storage.fingerprint, false);
	ok &= expect(!result.succeeded && read(orphan) == "changed unused", "stale storage review protects changed objects");
	const QString unknown = QDir(path).filePath(QStringLiteral("notes.txt")); ok &= write(unknown, "preserve");
	review = reviewPackageDraftStorage(path);
	ok &= expect(!review.complete() && QFileInfo::exists(orphan), "unknown paths block storage maintenance"); ok &= QFile::remove(unknown);
	const QString damaged = objectPath(path, "redo retained"); const auto original = read(damaged); ok &= write(damaged, "bad");
	review = reviewPackageDraftStorage(path);
	ok &= expect(!review.complete() && QFileInfo::exists(orphan), "corrupt referenced content blocks compaction before any deletion"); ok &= write(damaged, original);
	const QString interruptedManifest = QDir(path).filePath(QStringLiteral(".document-Ab1234")); ok &= write(interruptedManifest, "partial manifest");
	review = reviewPackageDraftStorage(path); bool stop = false; PackageReadControl cancel;
	cancel.isCancelled = [&]() { return stop; };
	cancel.progress = [&](const QString& value, qint64, qint64) {
		for (const auto& file : review.reclaimable) { if (value == file.relativePath) { stop = true; } }
	};
	result = compactPackageDraftStorage(path, review.storage.fingerprint, false, cancel);
	ok &= expect(!result.succeeded && stop && result.reclaimedFiles == 1 && read(manifest) == metadata,
		"cancellation reports completed reclamation without changing document metadata", result.error);
	review = reviewPackageDraftStorage(path); result = compactPackageDraftStorage(path, review.storage.fingerprint, false);
	ok &= expect(result.succeeded && reviewPackageDraftStorage(path).reclaimable.isEmpty(), "refreshed review can finish cancelled compaction", result.error);
	std::cout << (ok ? "Package draft storage smoke passed\n" : "Package draft storage smoke failed\n"); return ok ? 0 : 1;
}
