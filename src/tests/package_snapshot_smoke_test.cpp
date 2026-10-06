#include "core/package_archive.h"
#include "core/package_compare.h"
#include "core/package_copy.h"
#include "core/package_draft.h"
#include "core/package_validation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <array>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
class Listing final : public PackageArchiveReader {
public:
	QString source = QStringLiteral("memory");
	QVector<PackageEntry> rows;
	mutable int listings = 0, reads = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Zip; }
	QString sourcePath() const override { return source; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { ++listings; return rows; }
	bool readEntryBytes(const QString&, QByteArray*, QString* error, qint64) const override { if (error) { *error = "Use the exact entry."; } return false; }
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 limit) const override
	{
		++reads; if (error) { error->clear(); }
		if (index < 0 || index >= rows.size()) { return false; }
		if (out) { const auto bytes = QByteArray::number(index); *out = limit < 0 ? bytes : bytes.left(limit); }
		return true;
	}
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& cancelled) const override
	{
		QByteArray bytes; return (!cancelled || !cancelled()) && readEntryAt(index, &bytes, error, -1) && sink(bytes);
	}
};
std::shared_ptr<Listing> listing(const QStringList& paths)
{
	auto result = std::make_shared<Listing>();
	for (const auto& path : paths) { PackageEntry entry; entry.virtualPath = path; entry.sizeBytes = 1; result->rows << entry; }
	return result;
}
bool boundaries()
{
	bool ok = true; QString error; QByteArray bytes;
	auto input = listing({"a/b", "a/c"});
	PackageIndexLimits limits; limits.maximumEntries = 3; limits.maximumMetadataBytes = 82;
	PackageArchive archive;
	ok &= expect(archive.loadSnapshot(input, &error, {}, {}, limits) && archive.entries().size() == 2
		&& archive.indexUsage().entries == 3 && archive.indexUsage().metadataBytes == 82 && input->reads == 0,
		"Snapshot records/text include the implicit folder at the exact boundary without payload reads or reordered indexes.");
	ok &= expect(archive.readEntryAt(1, &bytes, &error) && bytes == "1", "Exact source indexes remain readable after admission.");
	const auto preserved = [&] { return archive.isOpen() && archive.entries().size() == 2 && archive.entries().at(1).virtualPath == "a/c"
		&& archive.indexUsage().entries == 3 && archive.readEntryAt(1, &bytes, &error) && bytes == "1"; };
	limits.maximumEntries = 2;
	ok &= expect(!archive.loadSnapshot(input, &error, {}, {}, limits) && !error.isEmpty() && preserved(), "Implicit folder refusal preserves the previous usable snapshot.");
	limits.maximumEntries = 3; limits.maximumMetadataBytes = 81;
	ok &= expect(!archive.loadSnapshot(input, &error, {}, {}, limits) && !error.isEmpty() && preserved(), "One metadata byte below the boundary preserves the prior snapshot.");
	limits.maximumMetadataBytes = 82; limits.maximumPathDepth = 1;
	ok &= expect(!archive.loadSnapshot(input, &error, {}, {}, limits) && error.contains("depth") && preserved(), "Snapshot depth is admitted before publication.");
	limits.maximumPathDepth = 2;
	ok &= expect(!archive.loadSnapshot(input, &error, {{{}, {}, false}}, {}, limits) && !error.isEmpty() && preserved(), "Even an empty diagnostic occupies a bounded snapshot record.");
	const int listings = input->listings; limits.maximumEntries = -1;
	ok &= expect(!archive.loadSnapshot(input, &error, {}, {}, limits) && input->listings == listings && preserved(), "Invalid snapshot policies fail before requesting the reader listing.");
	for (const auto& path : {QStringLiteral("../escape"), QStringLiteral("a\\b"), QStringLiteral("/absolute")}) {
		ok &= expect(!archive.loadSnapshot(listing({path}), &error) && !error.isEmpty() && preserved(), "Unsafe or noncanonical snapshot paths cannot replace the current index.");
	}
	auto repeated = listing({"same", "same"});
	ok &= expect(archive.loadSnapshot(repeated, &error) && archive.readEntryAt(0, &bytes, &error) && bytes == "0"
		&& archive.readEntryAt(1, &bytes, &error) && bytes == "1", "Repeated paths preserve their independent physical positions.");
	return ok;
}
bool cancellationAndOwnership(const QDir& root)
{
	bool ok = true; QString error; QByteArray bytes;
	PackageArchive archive; auto input = listing({"keep"});
	ok &= expect(archive.loadSnapshot(input, &error), "Prepare a reusable snapshot before cancellation.");
	PackageReadControl control; control.isCancelled = [] { return true; };
	const int before = input->listings;
	ok &= expect(!archive.loadSnapshot(input, &error, {}, control) && input->listings == before && archive.isOpen(), "Early cancellation does not request a listing or replace the archive.");
	auto many = listing({});
	for (int index = 0; index < 600; ++index) { PackageEntry entry; entry.virtualPath = QString::number(index); many->rows << entry; }
	bool stop = false, reached = false;
	control.isCancelled = [&] { return stop; };
	control.progress = [&](const QString& phase, qint64 done, qint64) { if (phase == "Preparing package snapshot" && done == 256) { reached = true; stop = true; } };
	ok &= expect(!archive.loadSnapshot(many, &error, {}, control) && reached && many->reads == 0
		&& archive.entries().first().virtualPath == "keep", "Snapshot preparation reports progress and cancels partway without publishing its partial list.");
	for (int index = 0; index < many->rows.size(); ++index) { many->rows[index].virtualPath = QStringLiteral("%1/a/b/file").arg(index); }
	stop = false; reached = false;
	control.progress = [&](const QString& phase, qint64 done, qint64) { if (phase == "Preparing package snapshot folders" && done == 256) { reached = true; stop = true; } };
	ok &= expect(!archive.loadSnapshot(many, &error, {}, control) && reached && many->reads == 0
		&& archive.entries().first().virtualPath == "keep", "Implied-folder expansion has its own cancellable progress phase and cannot publish a partial view.");
	PackageIndexLimits folders; folders.maximumEntries = 1000;
	ok &= expect(!archive.loadSnapshot(many, &error, {}, {}, folders) && error.contains("1000")
		&& archive.entries().first().virtualPath == "keep", "Many independent directory prefixes share the snapshot's record allowance.");
	std::array<QChar, 3> path{QChar('r'), QChar('a'), QChar('w')};
	std::array<QChar, 4> note{QChar('n'), QChar('o'), QChar('t'), QChar('e')};
	auto raw = listing({}); PackageEntry entry; entry.virtualPath = QString::fromRawData(path.data(), path.size());
	entry.note = QString::fromRawData(note.data(), note.size()); raw->rows << entry;
	ok &= expect(archive.loadSnapshot(raw, &error), "Admit caller-backed snapshot metadata.");
	path[0] = QChar('x'); note[0] = QChar('x');
	ok &= expect(archive.entries().first().virtualPath == "raw" && archive.entries().first().note == "note", "Accepted metadata owns raw caller text.");

	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pak); plan.addBytes("payload", "file");
	PackageWriteRequest request; request.destinationPath = root.filePath("backing.pak");
	ok &= expect(plan.writeArchive(request).succeeded(), "Write a synthetic disk-backed snapshot source.");
	auto disk = std::make_shared<PackageArchive>();
	ok &= expect(disk->load(request.destinationPath, &error) && archive.loadSnapshot(disk, &error), "Admit a known archive reader.");
	const auto hashes = disk->indexUsage().fingerprintBytes;
	PackageIndexLimits strict; strict.maximumFingerprintBytes = hashes - 1;
	ok &= expect(!archive.loadSnapshot(disk, &error, {}, {}, strict) && archive.isOpen(), "Adaptation cannot bypass a stricter backing fingerprint budget.");
	disk->clear();
	ok &= expect(archive.readEntryAt(0, &bytes, &error) && bytes == "payload" && archive.protectsInputPath(request.destinationPath)
		&& archive.indexUsage().fingerprintBytes == hashes, "Known archive backing is frozen despite later mutation through its original owner.");
	auto wrapped = std::make_shared<PackageArchive>(archive);
	for (int index = 0; index < 100; ++index) {
		auto next = std::make_shared<PackageArchive>();
		ok &= next->loadSnapshot(wrapped, &error); wrapped = std::move(next);
	}
	const auto backing = std::dynamic_pointer_cast<const PackageArchive>(wrapped->snapshotReader());
	ok &= expect(wrapped->readEntryAt(0, &bytes, &error) && bytes == "payload"
		&& backing && !backing->snapshotReader(), "Repeated adapters flatten instead of accumulating recursive reader chains.");
	ok &= expect(!wrapped->loadSnapshot(wrapped, &error) && wrapped->readEntryAt(0, &bytes, &error) && bytes == "payload", "Self adaptation refuses a cycle and preserves readable backing.");
	return ok;
}
bool stagedViews(const QDir& root)
{
	bool ok = true; QString error;
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Zip); plan.addBytes("a", "a/x"); plan.addBytes("b", "b/y");
	const auto revision = plan.revision();
	PackageIndexLimits limits; limits.maximumEntries = 3;
	const auto rejected = packagePlannedArchive(plan, &error, {}, limits);
	ok &= expect(!rejected.isOpen() && rejected.entries().isEmpty() && error == rejected.errorString()
		&& !error.isEmpty() && plan.revision() == revision && plan.operations().size() == 2, "Staged implied-folder admission refuses a view without erasing the document.");
	PackageStagingArchive complete(plan, PackageStagingReadMode::CompletePlan, {}, limits);
	ok &= expect(!complete.isOpen() && !complete.errorString().isEmpty() && complete.entries().isEmpty(), "Complete-plan readers share implied-folder admission even without exposing synthetic rows.");
	ok &= expect(plan.undo() && packagePlannedArchive(plan, &error, {}, limits).isOpen()
		&& plan.redo() && !packagePlannedArchive(plan, &error, {}, limits).isOpen(), "Undo restores a usable view, and Redo retains the limit diagnostic.");
	const auto validation = validatePackage(rejected);
	ok &= expect(!validation.valid() && validation.warnings.last().message == rejected.errorString(), "Validation preserves the snapshot admission reason.");
	PackageExtractionRequest extract; extract.extractAll = true; extract.targetDirectory = root.filePath("refused-extraction");
	const auto extraction = extractPackageEntries(rejected, extract);
	ok &= expect(!extraction.succeeded() && !QFileInfo::exists(extract.targetDirectory), "A refused view cannot create extraction output.");
	PackageCopyRequest copy; copy.entryIndexes = {0};
	ok &= expect(copyPackageEntries(rejected, copy).error == rejected.errorString(), "Temporary copies preserve the snapshot admission reason.");
	const auto comparison = comparePackages(packagePlannedArchive(PackageStagingModel()), rejected);
	ok &= expect(!comparison.completed && comparison.warnings.contains(rejected.errorString()), "Comparison does not describe a refused snapshot as an empty package.");
	return ok;
}
}

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	bool ok = boundaries(); ok &= cancellationAndOwnership(QDir(temporary.path())); ok &= stagedViews(QDir(temporary.path()));
	return ok ? 0 : 1;
}
