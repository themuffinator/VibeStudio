#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
void u16(QByteArray& bytes, quint16 value)
{
	bytes.append(static_cast<char>(value)); bytes.append(static_cast<char>(value >> 8));
}
void u32(QByteArray& bytes, quint32 value)
{
	u16(bytes, value & 0xffff); u16(bytes, value >> 16);
}
QByteArray pak(const QList<QByteArray>& names)
{
	QByteArray bytes("PACK"); u32(bytes, 15); u32(bytes, names.size() * 64); bytes += "abc";
	for (QByteArray name : names) {
		name.resize(56, '\0'); bytes += name; u32(bytes, 12); u32(bytes, 3);
	}
	return bytes;
}
QByteArray wad(const QList<QByteArray>& names)
{
	QByteArray bytes("PWAD"); u32(bytes, names.size()); u32(bytes, 15); bytes += "abc";
	for (QByteArray name : names) {
		u32(bytes, 12); u32(bytes, 3); name.resize(8, '\0'); bytes += name;
	}
	return bytes;
}
// Small stored ZIPs with independently chosen central-record tails. These
// exercise the reader without depending on the studio's archive writer.
QByteArray zip(const QList<QByteArray>& names, quint16 commentBytes = 0, quint16 extraBytes = 0,
	quint32* centralOffset = nullptr, quint16 flags = 0)
{
	QByteArray bytes, directory, extra;
	if (extraBytes >= 4) { u16(extra, 0xcafe); u16(extra, extraBytes - 4); extra += QByteArray(extraBytes - 4, 'x'); }
	for (const auto& name : names) {
		const quint32 offset = bytes.size();
		u32(bytes, 0x04034b50); u16(bytes, 20); u16(bytes, flags); u16(bytes, 0);
		u16(bytes, 0); u16(bytes, 0); u32(bytes, 0x352441c2); u32(bytes, 3); u32(bytes, 3);
		u16(bytes, name.size()); u16(bytes, 0); bytes += name; bytes += "abc";
		u32(directory, 0x02014b50); u16(directory, 20); u16(directory, 20); u16(directory, flags);
		u16(directory, 0); u16(directory, 0); u16(directory, 0); u32(directory, 0x352441c2);
		u32(directory, 3); u32(directory, 3); u16(directory, name.size()); u16(directory, extra.size());
		u16(directory, commentBytes); u16(directory, 0); u16(directory, 0); u32(directory, 0); u32(directory, offset);
		directory += name; directory += extra; directory += QByteArray(commentBytes, 'c');
	}
	const quint32 offset = bytes.size();
	if (centralOffset) { *centralOffset = offset; }
	bytes += directory; u32(bytes, 0x06054b50); u16(bytes, 0); u16(bytes, 0);
	u16(bytes, names.size()); u16(bytes, names.size()); u32(bytes, directory.size()); u32(bytes, offset); u16(bytes, 0);
	return bytes;
}
bool emptyReader(const PackageArchive& archive)
{
	return !archive.isOpen() && archive.entries().isEmpty() && archive.warnings().isEmpty()
		&& archive.sourcePath().isEmpty() && archive.format() == PackageArchiveFormat::Unknown && archive.contentId().isEmpty();
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	QDir root(temp.path());
	PackageArchive archive;
	QString error;
	bool ok = true;
	const QString nested = root.filePath(QStringLiteral("nested.pak"));
	ok &= expect(write(nested, pak({"a/b"})), "create nested PAK");
	PackageIndexLimits limits;
	limits.maximumEntries = 2; limits.maximumMetadataBytes = 72; limits.maximumFingerprintBytes = 32;
	ok &= expect(archive.load(nested, &error, {}, limits) && archive.entries().size() == 2 && error.isEmpty(),
		"exact record, path, implied-directory and fingerprint allowances succeed");
	QByteArray payload;
	ok &= expect(archive.readEntryBytes(QStringLiteral("a/b"), &payload, &error) && payload == "abc", "bounded listing preserves payload reads");
	limits.maximumEntries = 1;
	ok &= expect(!archive.load(nested, &error, {}, limits) && !error.isEmpty() && emptyReader(archive), "implied folders consume entry allowance without publishing a partial index");
	limits.maximumEntries = 2; limits.maximumMetadataBytes = 71;
	ok &= expect(!archive.load(nested, &error, {}, limits) && !error.isEmpty() && emptyReader(archive), "implied-folder text consumes metadata allowance");
	limits.maximumMetadataBytes = 72; limits.maximumPathDepth = 1;
	ok &= expect(!archive.load(nested, &error, {}, limits) && error.contains(QStringLiteral("depth")) && emptyReader(archive), "path components enforce the selected depth");
	limits.maximumPathDepth = 2; limits.maximumFingerprintBytes = 31;
	int progress = 0;
	PackageReadControl control;
	control.progress = [&](const QString&, qint64, qint64) { ++progress; };
	ok &= expect(!archive.load(nested, &error, control, limits) && progress == 0 && emptyReader(archive), "archive fingerprint rejection precedes source hashing");

	for (const auto& extension : {QStringLiteral("pak"), QStringLiteral("wad"), QStringLiteral("zip")}) {
		const QList<QByteArray> names {"a", "../bad", "z"};
		const QString path = root.filePath(QStringLiteral("unsafe.") + extension);
		const QByteArray bytes = extension == QStringLiteral("pak") ? pak(names) : extension == QStringLiteral("wad") ? wad(names) : zip(names, 65535, 65535);
		ok &= expect(write(path, bytes), "create skipped-record fixture");
		limits = {}; limits.maximumEntries = 2;
		ok &= expect(!archive.load(path, &error, {}, limits) && !error.isEmpty() && emptyReader(archive), "skipped records cannot bypass physical entry admission");
		limits.maximumEntries = 3;
		ok &= expect(archive.load(path, &error, {}, limits) && archive.entries().size() == 2 && archive.warnings().size() == 1,
			"skipping an unsafe record advances to the following record, including tails spanning verified chunks");
		ok &= expect(archive.readEntryBytes(QStringLiteral("z"), &payload, &error) && payload == "abc", "streamed central tails preserve subsequent local offsets and CRCs");
		const qint64 records = extension == QStringLiteral("pak") ? 192 : extension == QStringLiteral("wad") ? 48 : 3 * (46 + 65535 + 65535) + 8;
		limits.maximumMetadataBytes = records + 16;
		ok &= expect(!archive.load(path, &error, {}, limits) && !error.isEmpty() && emptyReader(archive), "diagnostic text is charged even when raw names fit");
	}

	const QString encrypted = root.filePath(QStringLiteral("encrypted.zip"));
	ok &= expect(write(encrypted, zip({"a"}, 0, 0, nullptr, 1)), "create unreadable-entry fixture");
	limits = {}; limits.maximumMetadataBytes = 49;
	ok &= expect(!archive.load(encrypted, &error, {}, limits) && !error.isEmpty() && emptyReader(archive), "entry notes consume metadata allowance");
	limits = {};
	ok &= expect(archive.load(encrypted, &error, {}, limits) && !archive.entries().first().readable && !archive.entries().first().note.isEmpty(), "unreadable-entry diagnostic remains available within budget");

	quint32 central = 0;
	QByteArray truncated = zip({"a", "z"}, 0, 0, &central);
	truncated[central + 47 + 32] = char(0xff); truncated[central + 47 + 33] = char(0xff);
	const QString broken = root.filePath(QStringLiteral("truncated.zip"));
	ok &= expect(write(broken, truncated) && !archive.load(broken, &error) && !error.isEmpty() && emptyReader(archive), "truncated later central-record tail clears earlier records");

	const QString deep = root.filePath(QStringLiteral("deep.zip"));
	QByteArray deepName;
	for (int index = 0; index < 127; ++index) { deepName += "a/"; }
	deepName += 'z';
	ok &= expect(write(deep, zip({deepName})) && archive.load(deep, &error) && archive.entries().size() == 128, "default depth boundary admits all implied folders");
	ok &= expect(write(deep, zip({QByteArray("a/") + deepName})) && !archive.load(deep, &error) && error.contains(QStringLiteral("depth")) && emptyReader(archive), "default depth ceiling refuses one extra component");

	QByteArray advertised("PWAD"); u32(advertised, PackageIndexLimits::entryCeiling + 1); u32(advertised, 12);
	advertised += QByteArray((PackageIndexLimits::entryCeiling + 1) * 16, '\0');
	const QString many = root.filePath(QStringLiteral("advertised.wad"));
	ok &= expect(write(many, advertised) && !archive.load(many, &error) && error.contains(QStringLiteral("limits")) && emptyReader(archive), "default physical-record cap refuses advertised records before accumulating entries");

	const QString folder = root.filePath(QStringLiteral("folder"));
	ok &= expect(root.mkpath(QStringLiteral("folder")), "create folder package");
	limits = {}; limits.maximumEntries = 0; limits.maximumMetadataBytes = 0; limits.maximumFingerprintBytes = 0;
	ok &= expect(archive.load(folder, &error, {}, limits) && archive.entries().isEmpty(), "empty folder fits zero entry, metadata and fingerprint budgets");
	ok &= expect(write(QDir(folder).filePath(QStringLiteral("a")), QByteArray(65537, 'a'))
		&& write(QDir(folder).filePath(QStringLiteral("b")), QByteArray(65537, 'b')), "create rounded fingerprint fixtures");
	QSet<QString> hashedFiles;
	control.progress = [&](const QString& phase, qint64, qint64) { if (phase.endsWith(QStringLiteral("/a")) || phase.endsWith(QStringLiteral("/b"))) { hashedFiles.insert(phase); } };
	limits = {}; limits.maximumFingerprintBytes = 96;
	ok &= expect(!archive.load(folder, &error, control, limits) && hashedFiles.size() == 1 && error.contains(QStringLiteral("fingerprints")) && emptyReader(archive), "aggregate folder fingerprint admission stops before hashing the second source");
	limits.maximumFingerprintBytes = 128;
	ok &= expect(archive.load(folder, &error, {}, limits) && archive.entries().size() == 2, "aggregate fingerprint boundary includes per-file chunk rounding");
	progress = 0; control.progress = [&](const QString&, qint64, qint64) { ++progress; };
	ok &= expect(!capturePackageFileIdentity(QDir(folder).filePath(QStringLiteral("a")), &error, control, 63) && progress == 0 && !error.isEmpty(), "direct bounded capture refuses before reporting any hashing progress");
	const auto identity = capturePackageFileIdentity(QDir(folder).filePath(QStringLiteral("a")), &error, {}, 64);
	ok &= expect(identity && identity->chunkHashes.size() == 64, "direct capture accepts exact rounded chunk budget");
	limits = {}; limits.maximumEntries = 1;
	ok &= expect(!archive.load(folder, &error, {}, limits) && emptyReader(archive), "folder records share entry limits");

	for (int invalid = 0; invalid < 8; ++invalid) {
		limits = {};
		if (invalid == 0) { limits.maximumEntries = -1; }
		if (invalid == 1) { limits.maximumEntries = PackageIndexLimits::entryCeiling + 1; }
		if (invalid == 2) { limits.maximumMetadataBytes = -1; }
		if (invalid == 3) { limits.maximumMetadataBytes = PackageIndexLimits::metadataCeiling + 1; }
		if (invalid == 4) { limits.maximumFingerprintBytes = -1; }
		if (invalid == 5) { limits.maximumFingerprintBytes = PackageIndexLimits::fingerprintCeiling + 1; }
		if (invalid == 6) { limits.maximumPathDepth = 0; }
		if (invalid == 7) { limits.maximumPathDepth = PackageIndexLimits::depthCeiling + 1; }
		ok &= expect(!archive.load(nested, &error, {}, limits) && !error.isEmpty() && emptyReader(archive), "invalid API limits cannot raise ceilings or retain a prior open reader");
	}

	QList<QByteArray> names;
	for (int index = 0; index < 130; ++index) { names.append(QByteArray("f") + QByteArray::number(index)); }
	for (const auto& extension : {QStringLiteral("pak"), QStringLiteral("wad"), QStringLiteral("zip")}) {
		const QString path = root.filePath(QStringLiteral("cancel.") + extension);
		ok &= expect(write(path, extension == QStringLiteral("pak") ? pak(names) : extension == QStringLiteral("wad") ? wad(names) : zip(names)), "create indexing cancellation fixture");
		bool cancel = false; int updates = 0;
		control.progress = [&](const QString& phase, qint64 completed, qint64 total) {
			if (phase.startsWith(QStringLiteral("Indexing "))) { ++updates; if (completed > 0 && total > completed) { cancel = true; } }
		};
		control.isCancelled = [&] { return cancel; };
		ok &= expect(!archive.load(path, &error, control) && updates == 2 && error.contains(QStringLiteral("cancelled")) && emptyReader(archive), "directory indexing reports progress and cancels after the first batch");
		cancel = false; updates = 0;
		control.progress = [&](const QString& phase, qint64, qint64) { if (phase.startsWith(QStringLiteral("Preparing package index"))) { cancel = true; ++updates; } };
		ok &= expect(!archive.load(path, &error, control) && updates == 1 && error.contains(QStringLiteral("cancelled")) && emptyReader(archive), "finalization cancellation leaves no partial index");
		control = {};
		ok &= expect(archive.load(path, &error) && archive.entries().size() == 130, "reader remains reusable after indexing cancellation");
	}
	const QString firstLayer = root.filePath(QStringLiteral("first.pak"));
	const QString secondLayer = root.filePath(QStringLiteral("second.pak"));
	ok &= expect(write(firstLayer, pak({"a"})) && write(secondLayer, pak({"a"})), "create overriding layer fixtures");
	PackageArchiveSession unrestricted;
	ok &= expect(unrestricted.openPrimaryArchive(firstLayer, &error), "open a session base");
	const auto primaryUsage = unrestricted.indexUsage();
	ok &= expect(primaryUsage.entries == 1 && primaryUsage.fingerprintBytes == 32, "session charges captured source resources");
	limits = {}; limits.maximumEntries = 1;
	PackageArchiveSession single(limits);
	ok &= expect(single.openPrimaryArchive(firstLayer, &error), "open at the aggregate record boundary");
	ok &= expect(!single.mountArchive(secondLayer, {}, &error) && !error.isEmpty() && single.depth() == 1
		&& single.indexUsage().entries == 1 && single.readEntryBytes(QStringLiteral("a"), &payload, &error) && payload == "abc",
		"a shadowed record still consumes admission and failed mount preserves the current reader");
	limits.maximumEntries = 2;
	PackageArchiveSession two(limits);
	ok &= expect(two.openPrimaryArchive(firstLayer, &error) && two.mountArchive(secondLayer, {}, &error)
		&& two.indexUsage().entries == 2 && two.entries().size() == 1 && two.entryLayerIndex(QStringLiteral("a")) == 1,
		"later layer wins while both retained source records remain charged");
	ok &= expect(two.popMountedLayer() && two.indexUsage().entries == 1 && two.indexUsage().fingerprintBytes == 32
		&& two.entryLayerIndex(QStringLiteral("a")) == 0 && two.mountArchive(secondLayer, {}, &error), "pop releases the layer budget and restores the earlier owner");
	two.clearMountedLayers();
	ok &= expect(two.indexUsage().metadataBytes == primaryUsage.metadataBytes && two.depth() == 1, "clear mounts releases exact metadata charges");
	two.clear();
	ok &= expect(two.indexUsage().entries == 0 && two.indexUsage().metadataBytes == 0 && two.indexUsage().fingerprintBytes == 0, "clear releases every layer resource");

	limits = {}; limits.maximumFingerprintBytes = 32;
	PackageArchiveSession hashes(limits);
	ok &= expect(hashes.openPrimaryArchive(firstLayer, &error), "open at the session fingerprint boundary");
	progress = 0; control.progress = [&](const QString&, qint64, qint64) { ++progress; };
	ok &= expect(!hashes.mountArchive(secondLayer, {}, &error, control) && progress == 0 && hashes.depth() == 1,
		"remaining aggregate fingerprint admission rejects before reading the second source");
	control = {};
	limits = {}; limits.maximumMetadataBytes = primaryUsage.metadataBytes;
	PackageArchiveSession metadata(limits);
	ok &= expect(metadata.openPrimaryArchive(firstLayer, &error) && !metadata.mountArchive(secondLayer, {}, &error)
		&& metadata.indexUsage().metadataBytes == primaryUsage.metadataBytes, "layer names and retained metadata consume the shared allowance");

	limits = {}; limits.maximumEntries = 3;
	PackageArchiveSession prefix(limits);
	ok &= expect(prefix.openPrimaryArchive(firstLayer, &error) && !prefix.mountArchive(secondLayer, QStringLiteral("mounted/deep"), &error)
		&& prefix.depth() == 1 && prefix.entries().size() == 1, "mount-prefix implied folders cannot bypass aggregate entry admission");
	limits.maximumEntries = 4;
	PackageArchiveSession expanded(limits);
	ok &= expect(expanded.openPrimaryArchive(firstLayer, &error) && expanded.mountArchive(secondLayer, QStringLiteral("mounted/deep"), &error)
		&& expanded.indexUsage().entries == 4 && expanded.entries().size() == 4
		&& expanded.readEntryBytes(QStringLiteral("mounted/deep/a"), &payload, &error) && payload == "abc", "exact prefix allowance preserves relocated reads");
	limits = {}; limits.maximumPathDepth = 2;
	PackageArchiveSession depth(limits);
	ok &= expect(depth.openPrimaryArchive(firstLayer, &error) && !depth.mountArchive(secondLayer, QStringLiteral("mounted/deep"), &error)
		&& error.contains(QStringLiteral("depth")) && depth.depth() == 1, "relocated paths obey the combined depth ceiling");

	PackageArchiveSession lengths;
	const QString longestMount(4094, QLatin1Char('m'));
	ok &= expect(lengths.openPrimaryArchive(firstLayer, &error) && lengths.mountArchive(secondLayer, longestMount, &error)
		&& lengths.readEntryBytes(longestMount + QStringLiteral("/a"), &payload, &error) && payload == "abc",
		"a relocated path at the reader's length boundary remains readable");
	ok &= expect(lengths.popMountedLayer() && !lengths.mountArchive(secondLayer, longestMount + QLatin1Char('m'), &error)
		&& error.contains(QStringLiteral("length")) && lengths.depth() == 1 && lengths.entries().size() == 1
		&& lengths.indexUsage().metadataBytes == primaryUsage.metadataBytes,
		"relocation cannot publish an overlong unreadable path or charge a failed mount");

	PackageArchiveSession layers;
	PackageMountLayer layer; layer.id = QStringLiteral("empty");
	for (int index = 0; index < PackageArchiveSession::layerCeiling; ++index) {
		ok &= expect(layers.pushMountedLayer(layer, &error), "bookkeeping layers fit the documented ceiling");
	}
	const auto fullUsage = layers.indexUsage();
	ok &= expect(!layers.pushMountedLayer(layer, &error) && layers.depth() == 64
		&& layers.indexUsage().metadataBytes == fullUsage.metadataBytes, "even empty bookkeeping layers are bounded atomically");
	progress = 0; control.progress = [&](const QString&, qint64, qint64) { ++progress; };
	ok &= expect(!layers.mountArchive(firstLayer, {}, &error, control) && progress == 0 && layers.depth() == 64, "layer-count admission precedes source hashing");
	control = {};
	ok &= expect(layers.openPrimaryArchive(firstLayer, &error) && layers.depth() == 1, "a new primary atomically replaces a full layer stack");
	layer.entryCount = -1;
	ok &= expect(!layers.setPrimaryLayer(layer, &error) && layers.hasOpenArchive() && layers.depth() == 1, "invalid bookkeeping replacement retains the open primary");

	bool merging = false;
	control.progress = [&](const QString& phase, qint64, qint64) { merging = phase.startsWith(QStringLiteral("Combining package layers")); };
	control.isCancelled = [&] { return merging; };
	const auto beforeCancel = layers.indexUsage();
	ok &= expect(!layers.mountArchive(secondLayer, {}, &error, control) && merging && error.contains(QStringLiteral("cancelled"))
		&& layers.depth() == 1 && layers.indexUsage().metadataBytes == beforeCancel.metadataBytes
		&& layers.entryLayerIndex(QStringLiteral("a")) == 0, "cancellation during merged-index preparation preserves layers, budget and cached ownership");
	control = {};
	ok &= expect(layers.mountArchive(secondLayer, {}, &error), "cancelled layer admission is retryable");
	const auto beforeMissing = layers.indexUsage();
	ok &= expect(!layers.openPrimaryArchive(root.filePath(QStringLiteral("missing.pak")), &error) && layers.depth() == 2
		&& layers.indexUsage().metadataBytes == beforeMissing.metadataBytes, "failed primary replacement preserves all previous layers");

	QByteArray duplicates = pak({"repeat", "repeat"});
	duplicates[duplicates.size() - 4] = 2;
	const QString duplicatePath = root.filePath(QStringLiteral("duplicate-layer.pak"));
	PackageArchiveSession duplicateLayer;
	ok &= expect(write(duplicatePath, duplicates) && duplicateLayer.openPrimaryArchive(duplicatePath, &error)
		&& duplicateLayer.entries().size() == 1 && duplicateLayer.entries().first().sizeBytes == 3
		&& duplicateLayer.readEntryBytes(QStringLiteral("repeat"), &payload, &error) && payload == "abc"
		&& duplicateLayer.warnings().size() == 1 && !duplicateLayer.warnings().first().blocksSaving,
		"layer metadata agrees with first-occurrence reads and preserves advisory warning severity");
	limits = {}; limits.maximumEntries = PackageIndexLimits::entryCeiling + 1;
	PackageArchiveSession invalid(limits);
	ok &= expect(!invalid.openPrimaryArchive(firstLayer, &error) && !error.isEmpty() && invalid.depth() == 0,
		"session callers cannot raise hard index ceilings");
	return ok ? 0 : 1;
}
