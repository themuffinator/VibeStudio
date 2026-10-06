#include "package_copy_test_helpers.h"
#include "core/package_staging.h"
#include "core/package_copy_store.h"

#include <QDir>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <limits>

using namespace vibestudio;
using namespace package_copy_test;

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const auto cleanup = qScopeGuard([] { waitForPackageCopyCleanup(); });
	const auto children = [&] { return QDir(temporary.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); };
	bool ok = true; Reader reader;
	reader.folder(QStringLiteral("folder")); reader.folder(QStringLiteral("folder/empty"));
	reader.add(QStringLiteral("folder/a.txt"), "first"); reader.add(QStringLiteral("folder/z.bin"), QByteArray(1024 * 1024, 'z'));
	reader.add(QStringLiteral("same.cfg"), "first occurrence"); reader.add(QStringLiteral("same.cfg"), "second occurrence");
	PackageCopyRequest request; request.parentDirectory = QDir(temporary.path()).canonicalPath(); request.entryIndexes = {0, 2, 0};
	auto copied = copyPackageEntries(reader, request);
	ok &= expect(copied.succeeded() && copied.paths.size() == 1 && copied.fileCount == 2 && copied.totalBytes == 1024 * 1024 + 5
		&& copied.extraction.requestedCount == 4 && QFileInfo(QDir(copied.paths.first()).filePath(QStringLiteral("empty"))).isDir()
		&& read(QDir(copied.paths.first()).filePath(QStringLiteral("a.txt"))) == "first" && reader.buffered == 0,
		"folder copies preserve empty folders, deduplicate overlapping roots and use streaming");
	const auto again = copyPackageEntries(reader, request);
	ok &= expect(again.succeeded() && again.paths != copied.paths && QFileInfo::exists(copied.paths.first()), "each batch has an independent namespace and lifetime");
	const auto retained = copied.storage;
	const QString retainedPath = copied.paths.first(); copied = {};
	ok &= expect(QFileInfo::exists(retainedPath), "a retained lease keeps a completed copy alive");
	request.entryIndexes = {5};
	const auto occurrence = copyPackageEntries(reader, request);
	ok &= expect(occurrence.succeeded() && read(occurrence.paths.first()) == "second occurrence", "single repeated name copies its exact selected occurrence");
	const auto before = children();
	request.entryIndexes = {4, 5};
	const auto collision = copyPackageEntries(reader, request);
	ok &= expect(!collision.succeeded() && collision.paths.isEmpty() && !collision.storage && !collision.error.isEmpty() && children() == before,
		"colliding selected names never publish a batch or retain partial files");
	request.entryIndexes = {1};
	{
		const auto empty = copyPackageEntries(reader, request);
		ok &= expect(empty.succeeded() && empty.fileCount == 0 && QFileInfo(empty.paths.first()).isDir(), "empty folder drag produces a real empty folder");
	}
	ok &= expect(children() == before, "unretained batch removes only its own temporary directory");
	request.entryIndexes = {0}; request.maximumBytes = 10;
	const int reads = reader.streamed;
	ok &= expect(!copyPackageEntries(reader, request).succeeded() && reader.streamed == reads && children() == before, "byte admission fails before any payload read or directory creation");
	request.maximumBytes = 2 * 1024 * 1024; request.maximumFiles = 1;
	ok &= expect(!copyPackageEntries(reader, request).succeeded() && reader.streamed == reads, "file count admission is enforced");
	request.maximumFiles = 2000; request.maximumEntries = 3;
	ok &= expect(!copyPackageEntries(reader, request).succeeded() && reader.streamed == reads, "empty and nonempty directories also count toward metadata admission");
	request.maximumEntries = 10000; request.entryIndexes = {3};
	const auto declared = reader.directory[3].sizeBytes; reader.directory[3].sizeBytes = std::numeric_limits<quint64>::max();
	ok &= expect(!copyPackageEntries(reader, request).succeeded() && reader.streamed == reads, "oversized declared sizes cannot wrap quota arithmetic");
	reader.directory[3].sizeBytes = declared;
	request.entryIndexes = {0}; bool cancel = false, sawProtectionMetadata = false;
	request.control.isCancelled = [&] { return cancel; };
	request.control.progress = [&](const QString& path, qint64 done, qint64 total) {
		if (path == QStringLiteral("Retaining package source protections") || path == QStringLiteral("Checking package source protections")) {
			sawProtectionMetadata = true;
			ok &= expect(done >= 0 && total == 0, "protection metadata progress has an explicitly unknown total");
			return;
		}
		ok &= expect(done >= 0 && done <= total, "copy payload progress stays within the file size");
		if (path == QStringLiteral("folder/z.bin") && done >= 65536) { cancel = true; }
	};
	const auto interrupted = copyPackageEntries(reader, request);
	ok &= expect(sawProtectionMetadata && interrupted.cancelled && interrupted.extraction.writtenCount > 0 && interrupted.paths.isEmpty()
		&& !interrupted.storage && children() == before, "cancelling within a file removes the whole batch including earlier committed copies");
	request.control = {}; reader.failAtEnd = 3;
	const auto corrupt = copyPackageEntries(reader, request);
	ok &= expect(!corrupt.succeeded() && corrupt.error.contains(QStringLiteral("integrity")) && children() == before,
		"final integrity failure prevents any handoff and removes earlier completed files");
	reader.failAtEnd = -1; request.entryIndexes = {-1};
	ok &= expect(!copyPackageEntries(reader, request).succeeded() && children() == before, "invalid exact index is refused");
	Reader unsafe; unsafe.add(QStringLiteral("../escape.txt"), "never written"); request.entryIndexes = {0};
	ok &= expect(!copyPackageEntries(unsafe, request).succeeded() && unsafe.streamed == 0 && children() == before, "unsafe entry paths fail before output");
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); plan.addBytes("owned first", QStringLiteral("new.txt"));
	PackageStagingArchive snapshot(plan); plan.addBytes("owned second", QStringLiteral("new.txt"), nullptr, PackageStageConflictResolution::ReplaceExisting);
	const auto staged = copyPackageEntries(snapshot, request);
	ok &= expect(staged.succeeded() && read(staged.paths.first()) == "owned first", "copy reads the captured planned snapshot after later edits");
	{
		QString error; const QString store = temporary.filePath(QStringLiteral("prepared-store"));
		PackageCopyRequest preparedRequest; preparedRequest.session = PackageCopySession::create(store, &error);
		preparedRequest.budget = std::make_shared<PackageCopyBudget>(); preparedRequest.entryIndexes = {2};
		if (!expect(preparedRequest.session && preparedRequest.session->isValid(), "create prepared-copy session")) { return 1; }
		const auto clear = [&] {
			const auto quota = inspectPackageCopyQuota(store);
			return quota.complete() && quota.reserved.batches == 0 && preparedRequest.budget->usage().batches == 0
				&& QDir(preparedRequest.session->path()).entryList({QStringLiteral("package-copy-*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
		};
		auto pending = preparePackageCopyEntries(reader, preparedRequest);
		const auto pendingQuota = inspectPackageCopyQuota(store);
		ok &= expect(pending.prepared() && !pending.succeeded() && read(pending.paths.first()) == "first"
			&& pendingQuota.complete() && pendingQuota.reserved.pendingBatches == 1 && preparedRequest.budget->usage().pendingBatches == 1,
			"private preparation verifies payload but keeps shared and window reservations pending");
		auto alias = pending;
		ok &= expect(discardPreparedPackageCopies(&pending) && pending.cancelled && !pending.prepared() && !pending.succeeded()
			&& !alias.prepared() && !alias.succeeded() && pending.paths.isEmpty() && !pending.storage && clear(),
			"discard invalidates every preparation alias and removes files before releasing reservations");
		ok &= expect(!publishPreparedPackageCopies(&alias) && !discardPreparedPackageCopies(&alias) && clear(),
			"discarded preparation cannot publish later or release reservations twice");
		alias = {};
		{ const auto abandoned = preparePackageCopyEntries(reader, preparedRequest); ok &= expect(abandoned.prepared(), "prepare abandoned batch"); }
		ok &= expect(clear(), "abandoning the final unpublished result safely disposes its batch");
		auto accepted = preparePackageCopyEntries(reader, preparedRequest);
		ok &= expect(publishPreparedPackageCopies(&accepted) && accepted.succeeded() && !accepted.prepared()
			&& preparedRequest.budget->usage().batches == 1 && preparedRequest.budget->usage().pendingBatches == 0,
			"acceptance publishes the batch and commits its window reservation");
		const auto publishedQuota = inspectPackageCopyQuota(store);
		ok &= expect(publishedQuota.complete() && publishedQuota.reserved.batches == 1 && publishedQuota.reserved.pendingBatches == 0
			&& !discardPreparedPackageCopies(&accepted) && accepted.succeeded() && read(accepted.paths.first()) == "first",
			"published copies retain shared accounting and cannot be discarded through preparation rollback");
	}
	return ok ? 0 : 1;
}
