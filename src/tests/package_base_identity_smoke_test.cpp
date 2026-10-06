#include "core/package_draft.h"
#include "core/package_selection.h"
#include "package_subset_test_helpers.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>
#include <memory>
#include <limits>
#include <utility>

using namespace vibestudio;
using namespace vibestudio::subset_test;

namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}
class Reader final : public PackageArchiveReader {
public:
	QString path, protectedPath, magic;
	QVector<PackageEntry> rows;
	QVector<QByteArray> payloads;
	PackageArchiveFormat type = PackageArchiveFormat::Zip;
	mutable int reads = 0;
	bool ignoreSink = false;
	std::function<void()> afterStream;
	PackageArchiveFormat format() const override { return type; }
	QString sourcePath() const override { return path; }
	QString wadMagic() const override { return magic; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return rows; }
	bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor, QString* error, const PackageReadControl& control) const override
	{
		return PackageArchiveReader::visitProtectedInputPaths(visitor, error, control) && (protectedPath.isEmpty() || visitor(protectedPath));
	}
	bool readEntryBytes(const QString& name, QByteArray* out, QString* error, qint64 maximum) const override
	{
		for (qsizetype at = 0; at < rows.size(); ++at) { if (rows[at].virtualPath == name) { return readEntryAt(at, out, error, maximum); } }
		return false;
	}
	bool readEntryAt(qsizetype at, QByteArray* out, QString*, qint64 maximum) const override
	{
		++reads;
		if (at < 0 || at >= payloads.size()) { return false; }
		if (out) { *out = maximum < 0 ? payloads[at] : payloads[at].left(maximum); }
		return true;
	}
	bool streamEntryAt(qsizetype at, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& cancelled) const override
	{
		++reads;
		if (at < 0 || at >= payloads.size() || (cancelled && cancelled())) { return false; }
		const bool accepted = sink(payloads[at]);
		if (afterStream) { afterStream(); }
		if (!accepted && !ignoreSink) { if (error) { *error = "stopped"; } return false; }
		return true;
	}
	void add(const QString& name, const QByteArray& payload)
	{
		PackageEntry row; row.virtualPath = name; row.sizeBytes = payload.size(); row.sourceOrdinal = rows.size();
		rows.append(row); payloads.append(payload);
	}
};
bool ownedReader(const QDir& root)
{
	bool ok = true; QString error; QByteArray bytes;
	const auto folder = root.filePath("display-folder"); ok &= root.mkpath("display-folder");
	ok &= put(QDir(folder).filePath("file.txt"), "changed!");
	auto reader = std::make_shared<Reader>(); reader->path = folder; reader->add("file.txt", "original");
	reader->protectedPath = root.filePath("protected-input.zip"); ok &= put(reader->protectedPath, "sentinel");
	PackageArchive wrapper; PackageStagingModel model;
	ok &= expect(wrapper.loadSnapshot(reader, &error) && model.loadBaseArchive(wrapper, &error), "Adopt an owned virtual reader.", error);
	std::weak_ptr<Reader> lifetime = reader;
	reader.reset(); wrapper.clear();
	ok &= expect(!lifetime.expired(), "Base staging retains the byte provider after callers release its adapter.");
	const auto entries = model.plannedEntries();
	if (entries.isEmpty()) { return false; }
	ok &= expect(model.entryBytes(entries.first(), &bytes, &error, 3) && bytes == "ori", "Preview reads the retained provider, not its display path.", error);
	PackageWriteRequest write; write.destinationPath = root.filePath("owned.zip");
	const auto result = model.writeArchive(write); PackageArchive output;
	ok &= expect(result.succeeded() && output.load(write.destinationPath, &error) && output.readEntryBytes("file.txt", &bytes, &error)
		&& bytes == "original", "Export uses the same owned source as preview.", result.blockedMessages.join(';'));
	write.destinationPath = root.filePath("protected-input.zip"); write.allowOverwrite = true;
	ok &= expect(!model.writeArchive(write).succeeded() && get(write.destinationPath) == "sentinel", "Adoption retains the provider's additional protected inputs.");
	const auto draft = root.filePath("owned.vibepackage"); PackageStagingModel restored;
	ok &= expect(PackageDraft::save(draft, &model, false, &error) && PackageDraft::load(draft, &restored, &error)
		&& restored.entryBytes(restored.plannedEntries().first(), &bytes, &error) && bytes == "original",
		"Draft checkpointing materializes the owned provider's bytes.", error);
	return ok;
}
bool borrowedFilesystem(const QDir& root)
{
	bool ok = true; QString error; QByteArray bytes;
	ok &= root.mkpath("borrowed"); const auto folder = root.filePath("borrowed"), file = QDir(folder).filePath("file.txt");
	ok &= put(file, "original"); const auto modified = QFileInfo(file).lastModified();
	Reader reader; reader.path = folder; reader.type = PackageArchiveFormat::Folder; reader.add("file.txt", "unused!!");
	PackageStagingModel model; ok &= expect(model.loadBaseArchive(reader, &error), "Capture a borrowed filesystem adapter at adoption.", error);
	ok &= put(file, "changed!"); { QFile changed(file); ok &= changed.open(QIODevice::ReadWrite) && changed.setFileTime(modified, QFileDevice::FileModificationTime); }
	ok &= expect(!model.entryBytes(model.plannedEntries().first(), &bytes, &error) && bytes.isEmpty(),
		"Equal-size, equal-timestamp source changes fail before any deferred first preview.", error);
	PackageWriteRequest request; request.destinationPath = root.filePath("changed.zip");
	ok &= expect(!model.writeArchive(request).succeeded() && !QFileInfo::exists(request.destinationPath), "A changed deferred source cannot become exported content.");
	return ok;
}
bool missingBacking(const QDir& root)
{
	bool ok = true; QString error; QByteArray bytes;
	Reader reader; reader.path = root.filePath("not-yet-present"); reader.type = PackageArchiveFormat::Folder; reader.add("file.txt", "unknown!");
	PackageStagingModel model;
	ok &= expect(model.loadBaseArchive(reader, &error) && !model.summary().canSave
		&& !model.plannedEntries().first().unavailableReason.isEmpty(), "Unowned, unavailable backing is explicit before editing.", error);
	ok &= root.mkpath("not-yet-present") && put(QDir(reader.path).filePath("file.txt"), "changed!");
	ok &= expect(!model.entryBytes(model.plannedEntries().first(), &bytes, &error), "A later filesystem arrival cannot substitute for an unavailable original.", error);
	ok &= expect(model.addBytes("replacement", "file.txt", &error, PackageStageConflictResolution::ReplaceExisting)
		&& model.summary().canSave && model.entryBytes(model.plannedEntries().first(), &bytes, &error) && bytes == "replacement"
		&& model.undo() && !model.summary().canSave, "Unavailable metadata remains repairable and Undo restores its blocker.", error);
	return ok;
}
bool ownedWad(const QDir& root)
{
	bool ok = true; QString error; QByteArray bytes;
	for (const QString& magic : {QStringLiteral("PWAD"), QStringLiteral("IWAD"), QStringLiteral("WAD2"), QStringLiteral("WAD3")}) {
		auto source = std::make_shared<Reader>(); source->type = PackageArchiveFormat::Wad; source->magic = magic;
		source->path = root.filePath(magic + "-virtual.wad"); source->add("DUP", "first"); source->add("DUP", "second");
		source->rows[0].wadLumpType = 0; source->rows[1].wadLumpType = 0x44;
		PackageArchive wrapper; PackageStagingModel model;
		if (!expect(wrapper.loadSnapshot(source, &error) && model.loadBaseArchive(wrapper, &error), "Adopt an owned WAD without a filesystem surrogate.", error)) { ok = false; continue; }
		ok &= expect(model.sourceWadMagic() == magic && model.entryBytes(model.plannedEntries().last(), &bytes, &error, 2)
			&& bytes == "se", "Virtual WAD subtype and repeated occurrences remain exact.", error);
		PackageWriteRequest write; write.destinationPath = root.filePath(magic + "-out.wad"); write.format = PackageArchiveFormat::Wad;
		const auto result = model.writeArchive(write); PackageArchive output;
		ok &= expect(result.succeeded() && get(write.destinationPath).first(4) == magic.toLatin1() && output.load(write.destinationPath, &error)
			&& output.readEntryAt(1, &bytes, &error) && bytes == "second"
			&& (magic == "PWAD" || magic == "IWAD" || (output.entries()[0].wadLumpType == 0 && output.entries()[1].wadLumpType == 0x44)),
			"Owned WAD export preserves its subtype, exact directory types including zero, and second lump.", result.blockedMessages.join(';'));
		const auto draft = root.filePath(magic + ".vibepackage"); PackageStagingModel restored;
		ok &= expect(PackageDraft::save(draft, &model, false, &error) && PackageDraft::load(draft, &restored, &error)
			&& restored.sourceWadMagic() == magic && restored.plannedEntries()[0].wadLumpType == 0
			&& restored.entryBytes(restored.plannedEntries()[1], &bytes, &error) && bytes == "second",
			"Portable drafts retain virtual WAD metadata and bytes.", error);
	}
	return ok;
}
bool verifyProvider(const QDir& root)
{
	bool ok = true; QString error;
	for (const int actual : {7, 9}) {
		auto provider = std::make_shared<Reader>(); provider->add("file", QByteArray(actual, 'x')); provider->rows[0].sizeBytes = 8;
		PackageArchive archive; PackageStagingModel model;
		ok &= archive.loadSnapshot(provider, &error) && model.loadBaseArchive(archive, &error);
		ok &= expect(!archive.verifySourceIdentity(&error) && error.contains("changed size"), "Provider verification rejects short and oversized streams.", error);
		PackageWriteRequest request; request.destinationPath = root.filePath(QStringLiteral("size-%1.zip").arg(actual));
		ok &= expect(!model.writeArchive(request).succeeded() && !QFileInfo::exists(request.destinationPath), "A provider size mismatch cannot publish an archive.");
		ok &= expect(!PackageDraft::save(root.filePath(QStringLiteral("size-%1.vibepackage").arg(actual)), &model, false, &error), "A provider size mismatch cannot checkpoint a draft.", error);
	}
	for (const bool ignoreSink : {false, true}) {
		auto provider = std::make_shared<Reader>(); provider->add("file", "data"); provider->ignoreSink = ignoreSink;
		bool cancel = false; PackageReadControl control; control.isCancelled = [&] { return std::exchange(cancel, false); };
		if (ignoreSink) { control.progress = [&](const QString&, qint64, qint64) { cancel = true; }; }
		else { provider->afterStream = [&] { cancel = true; }; }
		PackageArchive archive; ok &= archive.loadSnapshot(provider, &error);
		ok &= expect(!archive.verifySourceIdentity(&error, control) && error.contains("cancelled"), "Verification latches cancellation at the sink and provider-return boundaries.", error);
	}
	auto provider = std::make_shared<Reader>(); provider->add("unavailable", "data"); provider->rows[0].readable = false;
	PackageArchive archive; ok &= archive.loadSnapshot(provider, &error);
	ok &= expect(archive.verifySourceIdentity(&error) && provider->reads == 0, "Known unavailable metadata is not streamed during snapshot verification.", error);
	return ok;
}
bool capturedOccurrences(const QDir& root)
{
	bool ok = true; QString error; QByteArray bytes;
	const auto path = root.filePath("occurrences.pak"); ok &= pak(path, {{"same", "abc"}, {"same", "xyz"}});
	Reader adapter; adapter.path = path; adapter.type = PackageArchiveFormat::Pak; adapter.add("same", "abc"); adapter.add("same", "xyz");
	PackageStagingModel exact; ok &= exact.loadBaseArchive(adapter, &error);
	ok &= expect(exact.entryBytes(exact.plannedEntries().last(), &bytes, &error) && bytes == "xyz", "Explicit physical ordinals bind duplicate metadata to its exact captured occurrence.", error);
	for (auto& row : adapter.rows) { row.sourceOrdinal = -1; }
	PackageStagingModel ambiguous; ok &= ambiguous.loadBaseArchive(adapter, &error);
	ok &= expect(!ambiguous.summary().canSave && !ambiguous.entryBytes(ambiguous.plannedEntries().first(), &bytes, &error), "Ambiguous borrowed occurrences remain unavailable instead of guessing.", error);
	adapter.rows[0].sourceOrdinal = adapter.rows[1].sourceOrdinal = 0;
	PackageStagingModel repeated; ok &= repeated.loadBaseArchive(adapter, &error);
	int unavailable = 0; for (const auto& row : repeated.plannedEntries()) { unavailable += !row.unavailableReason.isEmpty(); }
	ok &= expect(unavailable == 1, "A captured physical occurrence can be consumed only once.");
	PackageStagingContentUsage usage; PackageArchive native; ok &= native.load(path, &error) && exact.contentUsage(&usage, &error);
	ok &= expect(usage.fingerprintBytes == native.indexUsage().fingerprintBytes && usage.fingerprintBytes > 0, "Borrowed capture charges actual retained fingerprints.", error);
	PackageStagingContentLimits limits; limits.maximumFingerprintBytes = 0; PackageStagingModel limited(limits);
	ok &= limited.createEmpty(PackageArchiveFormat::Zip, {}, &error); const auto before = limited.manifestJson();
	ok &= expect(!limited.loadBaseArchive(adapter, &error) && !error.isEmpty() && limited.manifestJson() == before, "Capture respects the destination fingerprint limit without replacing its current document.", error);
	return ok;
}
bool invalidVirtualWads()
{
	bool ok = true; QString error;
	for (int variant = 0; variant < 6; ++variant) {
		auto provider = std::make_shared<Reader>(); provider->type = PackageArchiveFormat::Wad; provider->magic = "WAD2";
		provider->add("A", "a"); provider->add("B", "b"); provider->rows[0].wadLumpType = provider->rows[1].wadLumpType = 0x44;
		if (variant == 0) { provider->magic.clear(); }
		if (variant == 1) { provider->rows[1].sourceOrdinal = 0; }
		if (variant == 2) { provider->rows[1].sourceOrdinal = 2; }
		if (variant == 3) { provider->rows[1].wadLumpType = -1; }
		if (variant == 4) { provider->rows[1].wadLumpType = 256; }
		if (variant == 5) { provider->magic = "WAD4"; }
		PackageArchive archive; PackageStagingModel model;
		ok &= model.createEmpty(PackageArchiveFormat::Zip, {}, &error); const auto before = model.manifestJson();
		const bool accepted = archive.loadSnapshot(provider, &error) && model.loadBaseArchive(archive, &error);
		ok &= expect(!accepted && !error.isEmpty() && model.manifestJson() == before && provider->reads == 0, "Invalid virtual WAD directory metadata refuses atomically before payload reads.", error);
	}
	return ok;
}
bool wadTypes(const QDir& root)
{
	bool ok = true; QString error; QByteArray bytes;
	for (const QString& magic : {QStringLiteral("WAD2"), QStringLiteral("WAD3")}) {
		const int defaultType = magic == "WAD2" ? 0x44 : 0x43;
		PackageStagingModel generated;
		ok &= generated.createEmpty(PackageArchiveFormat::Wad, magic, &error) && generated.addBytes("new", "AUTO", &error)
			&& generated.addWadBytes("zero", "ZERO", {}, 0, false, &error);
		PackageWriteRequest request; request.destinationPath = root.filePath(magic + "-types.wad"); request.format = PackageArchiveFormat::Wad;
		PackageArchive output; const auto result = generated.writeArchive(request);
		ok &= expect(result.succeeded() && output.load(request.destinationPath, &error)
			&& output.entries().at(index(output, "AUTO")).wadLumpType == defaultType
			&& output.entries().at(index(output, "ZERO")).wadLumpType == 0, "Generated WAD entries receive defaults while explicit type zero stays zero.", error);
		PackageStagingModel reopened; ok &= reopened.loadBaseArchive(output, &error)
			&& reopened.addBytes("replace", "ZERO", &error, PackageStageConflictResolution::ReplaceExisting);
		request.destinationPath = root.filePath(magic + "-replaced.wad"); const auto replaced = reopened.writeArchive(request);
		ok &= expect(replaced.succeeded() && output.load(request.destinationPath, &error)
			&& output.entries().at(index(output, "ZERO")).wadLumpType == 0
			&& output.readEntryBytes("ZERO", &bytes, &error) && bytes == "replace", "Native WAD type zero survives an ordinary replacement and export.", error);
		PackageStagingModel converted; ok &= converted.createEmpty(PackageArchiveFormat::Zip, {}, &error) && converted.addBytes("data", "CONVERT", &error);
		request.destinationPath = root.filePath(magic + "-converted.wad"); request.wadMagic = magic;
		ok &= expect(converted.writeArchive(request).succeeded() && output.load(request.destinationPath, &error)
			&& output.entries().first().wadLumpType == defaultType, "Non-WAD conversion keeps the selected texture WAD default.", error);
	}
	return ok;
}

bool nativePreflight(const QDir& root)
{
	bool ok = true; QString error;
	for (const auto format : {PackageArchiveFormat::Pak, PackageArchiveFormat::Wad}) {
		for (int variant = 0; variant < 3; ++variant) {
			auto provider = std::make_shared<Reader>(); provider->add(variant == 2 ? QString(57, 'x') : QStringLiteral("A"), "x");
			if (variant == 0) { provider->rows[0].sizeBytes = quint64(std::numeric_limits<qint32>::max()) + 1; }
			if (variant == 1) { provider->add("B", "x"); for (auto& row : provider->rows) { row.sizeBytes = 1100000000; } }
			PackageArchive archive; PackageStagingModel model;
			ok &= archive.loadSnapshot(provider, &error) && model.loadBaseArchive(archive, &error);
			PackageWriteRequest request; request.format = format; request.destinationPath = root.filePath("refused-native-output");
			for (const bool dryRun : {true, false}) {
				request.dryRun = dryRun; const auto result = model.writeArchive(request);
				ok &= expect(!result.succeeded() && !QFileInfo::exists(request.destinationPath) && provider->reads == 0
					&& result.blockedMessages.join(';').contains(variant == 2 ? (format == PackageArchiveFormat::Pak ? "PAK entry path" : "WAD lump names") : "signed 32-bit"),
					"Impossible native sizes, offsets and names refuse before provider reads in real and dry-run exports.", result.blockedMessages.join(';'));
			}
		}
	}
	return ok;
}
bool subsetSources(const QDir& root)
{
	bool ok = true; QString error; QByteArray bytes;
	for (const auto format : {PackageArchiveFormat::Zip, PackageArchiveFormat::Wad}) {
		auto provider = std::make_shared<Reader>(); provider->type = format; provider->magic = format == PackageArchiveFormat::Wad ? "WAD2" : "";
		provider->path = root.filePath("virtual-subset-source"); provider->add("A", "alpha"); provider->add("B", "beta");
		for (auto& row : provider->rows) { row.wadLumpType = 0; }
		provider->protectedPath = root.filePath("subset-protected.zip"); ok &= put(provider->protectedPath, "sentinel");
		PackageArchive archive; PackageStagingModel subset; PackageSubsetReview review;
		ok &= expect(archive.loadSnapshot(provider, &error) && subset.loadBaseArchiveSubsetAt(archive, {1}, &error, &review)
			&& subset.plannedEntries().size() == 1 && subset.entryBytes(subset.plannedEntries().first(), &bytes, &error) && bytes == "beta"
			&& PackageStagingArchive(subset).protectsInputPath(provider->protectedPath) && review.members.first().entryIndex == 1,
			"Selected subsets retain the virtual byte provider, protected inputs and supplied reader indexes.", error);
	}
	const auto path = root.filePath("reordered-subset.pak"); ok &= pak(path, {{"A", "alpha"}, {"B", "beta"}});
	Reader adapter; adapter.path = path; adapter.type = PackageArchiveFormat::Pak; adapter.add("B", "beta"); adapter.add("A", "alpha");
	adapter.rows[0].sourceOrdinal = 1; adapter.rows[1].sourceOrdinal = 0;
	PackageStagingModel subset; PackageSubsetReview review;
	ok &= expect(subset.loadBaseArchiveSubsetAt(adapter, {0}, &error, &review) && subset.plannedEntries().size() == 1
		&& subset.plannedEntries().first().virtualPath == "B" && subset.entryBytes(subset.plannedEntries().first(), &bytes, &error) && bytes == "beta"
		&& review.members.first().entryIndex == 0, "Borrowed subsets correlate supplied metadata indexes independently of captured native order.", error);
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path());
	bool ok = ownedReader(root); ok &= borrowedFilesystem(root); ok &= missingBacking(root); ok &= ownedWad(root);
	ok &= verifyProvider(root); ok &= capturedOccurrences(root); ok &= invalidVirtualWads(); ok &= wadTypes(root); ok &= nativePreflight(root); ok &= subsetSources(root);
	return ok ? 0 : 1;
}
