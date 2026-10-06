#include "core/package_draft.h"
#include "core/package_selection.h"
#include "package_subset_test_helpers.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <iostream>
#include <utility>

using namespace vibestudio;
using namespace vibestudio::subset_test;

namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}
class MetadataReader final : public PackageArchiveReader {
public:
	QVector<PackageEntry> rows;
	QString path = QStringLiteral("memory");
	PackageArchiveFormat type = PackageArchiveFormat::Zip;
	mutable int lists = 0, reads = 0;
	PackageArchiveFormat format() const override { return type; }
	QString sourcePath() const override { return path; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { ++lists; return rows; }
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { ++reads; return false; }
};
MetadataReader directories(int count)
{
	MetadataReader reader;
	for (int at = count - 1; at >= 0; --at) {
		PackageEntry entry; entry.kind = PackageEntryKind::Directory;
		entry.virtualPath = QStringLiteral("entry%1/").arg(at, 6, 10, QLatin1Char('0'));
		reader.rows.append(entry);
	}
	return reader;
}
bool seed(PackageStagingModel* model)
{
	return model->createEmpty(PackageArchiveFormat::Zip) && model->addBytes("keep", "keep")
		&& model->addBytes("redo", "redo") && model->undo();
}
bool cancellationAndReuse()
{
	bool ok = true; QString error; auto reader = directories(2048);
	PackageStagingModel model; if (!seed(&model)) { return false; }
	const auto before = model.manifestJson();
	PackageReadControl control; control.isCancelled = [] { return true; };
	ok &= expect(!model.loadBaseArchive(reader, &error, control) && reader.lists == 0 && reader.reads == 0
		&& error.contains("cancelled") && model.manifestJson() == before, "Pre-cancellation reads no source metadata and preserves redo.", error);
	for (const QString& phase : {QStringLiteral("Reading package base metadata"), QStringLiteral("Ordering package base entries")}) {
		bool requested = false, reached = false;
		control.isCancelled = [&] { return std::exchange(requested, false); };
		control.progress = [&](const QString& current, qint64 completed, qint64) {
			if (current == phase && completed >= 256 && !reached) { reached = requested = true; }
		};
		ok &= expect(!model.loadBaseArchive(reader, &error, control) && reached && error.contains("cancelled")
			&& model.manifestJson() == before && reader.reads == 0, "A one-shot cancellation is latched through metadata and merge sorting.", error);
	}
	ok &= expect(model.loadBaseArchive(reader, &error) && model.summary().baseDirectoryCount == 2048 && reader.reads == 0
		&& model.plannedEntries().first().virtualPath == "entry000000", "A cancelled destination can adopt the complete sorted metadata without payload reads.", error);
	PackageStagingArchive snapshot(model);
	PackageStagingModel rebased; if (!seed(&rebased)) { return false; }
	const auto beforeRebase = rebased.manifestJson(); bool cancel = false, reached = false;
	control.isCancelled = [&] { return cancel; };
	control.progress = [&](const QString& phase, qint64 completed, qint64) {
		if (phase == "Reading package base metadata" && completed >= 256) { reached = cancel = true; }
	};
	ok &= expect(!rebased.loadBaseArchive(snapshot, &error, control) && reached && rebased.manifestJson() == beforeRebase,
		"Rebasing an immutable planned snapshot remains cancellable with its plan cache warm.", error);
	PackageArchive wrapper; ok &= wrapper.loadSnapshot(std::make_shared<PackageStagingArchive>(snapshot), &error);
	cancel = reached = false;
	ok &= expect(!rebased.loadBaseArchive(wrapper, &error, control) && reached && rebased.manifestJson() == beforeRebase,
		"Archive adapters preserve control through nested snapshot adoption.", error);
	ok &= expect(rebased.loadBaseArchive(snapshot, &error) && rebased.plannedEntries().size() == 2048
		&& !rebased.canUndo() && !rebased.canRedo(), "Successful rebase retains all entries and resets edit history.", error);
	return ok;
}
bool quotas()
{
	bool ok = true; QString error; auto reader = directories(30000);
	PackageStagingModel model({}, {7, PackageStagingMetadataLimits::metadataCeiling});
	if (!seed(&model)) { return false; }
	const auto before = model.manifestJson(); int probes = 0;
	PackageReadControl control; control.isCancelled = [&] { ++probes; return false; };
	ok &= expect(!model.loadBaseArchive(reader, &error, control) && error.contains("7-record") && reader.lists == 1
		&& reader.reads == 0 && probes < 1000 && model.manifestJson() == before,
		"A tiny retained-record allowance refuses a large adapter before walking and duplicating its complete inventory.", error);
	PackageStagingModel reference; auto small = directories(8);
	ok &= reference.loadBaseArchive(small, &error); PackageStagingMetadataUsage usage;
	ok &= reference.metadataUsage(&usage, &error);
	for (int delta : {-1, 0}) {
		PackageStagingModel exact({}, {usage.records, usage.metadataBytes + delta});
		const bool loaded = exact.loadBaseArchive(small, &error);
		ok &= expect(loaded == (delta == 0) && (loaded || error.contains("byte document limit")),
			"Base metadata admission uses the same exact UTF-16 accounting as retained usage.", error);
	}
	PackageStagingPlanLimits planLimits; planLimits.maximumRecords = 7;
	PackageStagingModel planned({}, {}, planLimits); probes = 0;
	ok &= expect(!planned.loadBaseArchive(reader, &error, control) && error.contains("7-record") && probes < 1000,
		"Plan row limits also stop base materialization before sorting a large adapter.", error);
	return ok;
}
bool wadAdmission(const QDir& root)
{
	bool ok = true; QString error; PackageStagingModel destination;
	if (!seed(&destination)) { return false; }
	const auto before = destination.manifestJson();
	QVector<Lump> many;
	for (int at = 0; at < 1024; ++at) { many.append({QByteArray::number(at), "payload"}); }
	const auto large = root.filePath("many.wad"); PackageArchive archive;
	ok &= wad(large, many, "WAD3") && archive.load(large, &error);
	bool reached = false, cancel = false;
	PackageReadControl control; control.isCancelled = [&] { return std::exchange(cancel, false); };
	control.progress = [&](const QString& phase, qint64 completed, qint64) {
		if (phase == "Reading package WAD directory" && completed >= 256 && !reached) { reached = cancel = true; }
	};
	ok &= expect(!destination.loadBaseArchive(archive, &error, control) && reached && error.contains("cancelled")
		&& destination.manifestJson() == before, "WAD directory reconstruction obeys and latches cancellation.", error);
	reached = cancel = false;
	ok &= expect(!destination.loadBaseArchiveSubset(archive, {"0"}, &error, control) && reached
		&& destination.manifestJson() == before, "Path-selected subsets share cancellable base preparation.", error);
	PackageSubsetReview review; reached = cancel = false;
	ok &= expect(!destination.loadBaseArchiveSubsetAt(archive, {0}, &error, &review, control) && reached
		&& review.members.isEmpty() && destination.manifestJson() == before, "Subset loading passes control through its base admission.", error);

	MetadataReader generic; generic.type = PackageArchiveFormat::Wad; generic.path = large;
	PackageStagingModel limited({}, {32, PackageStagingMetadataLimits::metadataCeiling});
	int directorySteps = 0; control.isCancelled = {};
	control.progress = [&](const QString& phase, qint64 completed, qint64) {
		if (phase == "Reading package WAD directory") { directorySteps = int(completed); }
	};
	// One retained source root consumes a record before native directory admission.
	ok &= expect(!limited.loadBaseArchive(generic, &error, control) && error.contains("31 entries") && directorySteps == 0
		&& !limited.isLoaded(), "A borrowed WAD is admitted by the native reader before staging reconstructs its directory.", error);
	QByteArray header("PWAD"); u32(header, PackageIndexLimits::entryCeiling + 1); u32(header, 12);
	generic.path = root.filePath("excessive.wad");
	{
		QFile file(generic.path); ok &= file.open(QIODevice::WriteOnly) && file.write(header) == header.size()
			&& file.resize(12 + (PackageIndexLimits::entryCeiling + 1) * 16);
	}
	ok &= expect(!destination.loadBaseArchive(generic, &error) && error.contains("250000") && destination.manifestJson() == before,
		"A generic WAD cannot bypass the disk reader's directory ceiling.", error);
	const auto altered = get(large); auto changed = altered; changed[12] = 'X'; ok &= put(large, changed);
	ok &= expect(!destination.loadBaseArchive(archive, &error) && !error.isEmpty() && destination.manifestJson() == before,
		"A changed original WAD fails adoption without replacing the current document with partial metadata.", error);
	return ok;
}
bool wadOccurrences(const QDir& root)
{
	bool ok = true; QString error;
	for (const QByteArray& magic : {QByteArray("PWAD"), QByteArray("IWAD"), QByteArray("WAD2"), QByteArray("WAD3")}) {
		const auto path = root.filePath(QString::fromLatin1(magic) + ".wad"); PackageArchive archive;
		ok &= wad(path, {{"DUP", "first"}, {"DUP", "second"}}, magic) && archive.load(path, &error);
		MetadataReader generic; generic.path = path; generic.type = PackageArchiveFormat::Wad; generic.rows = archive.entries();
		// One exact pairing followed by a legacy name-only pairing must not
		// consume the first occurrence a second time.
		generic.rows[1].dataOffset = -1; generic.rows[1].compressedSizeBytes = 0; generic.rows[1].sourceOrdinal = -1;
		for (const PackageArchiveReader* reader : {static_cast<const PackageArchiveReader*>(&archive), static_cast<const PackageArchiveReader*>(&generic)}) {
			PackageStagingModel model;
			if (!expect(model.loadBaseArchive(*reader, &error), "Load repeated WAD records.", error)) { return false; }
			const auto rows = model.plannedEntries(); QByteArray bytes;
			ok &= expect(rows.size() == 2 && rows[0].sourceOrdinal == 0 && rows[1].sourceOrdinal == 1
				&& model.entryBytes(rows[1], &bytes, &error, 2) && bytes == "se"
				&& model.entryBytes(rows[1], &bytes, &error, 0) && bytes.isEmpty()
				&& model.entryBytes(rows[1], &bytes, &error) && bytes == "second",
				"Concrete and legacy WAD occurrences stay distinct and honour zero, capped and complete reads.", error);
		}
	}
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	QElapsedTimer timer; timer.start();
	bool ok = cancellationAndReuse(); ok &= quotas(); ok &= wadAdmission(QDir(temporary.path())); ok &= wadOccurrences(QDir(temporary.path()));
	std::cout << "Base admission checks completed in " << timer.elapsed() << " ms\n";
	return ok ? 0 : 1;
}
