#include "core/package_archive.h"
#include "core/package_staging.h"
#include "core/package_validation.h"
#include "core/package_draft.h"
#include "core/deflate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QFileInfo>

#include <algorithm>

#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

// Independent synthetic wire fixtures, assembled directly from APPNOTE 6.3.10
// (2022-11-01), not through the production ZIP writer or metadata helpers.
void put(QByteArray& bytes, qsizetype at, quint64 value, int width)
{
	for (int i = 0; i < width; ++i) { bytes[at + i] = char(value >> (i * 8)); }
}
void add(QByteArray& bytes, quint64 value, int width)
{
	const auto at = bytes.size(); bytes.resize(at + width); put(bytes, at, value, width);
}
QByteArray field(quint16 id, const QByteArray& value)
{
	QByteArray bytes; add(bytes, id, 2); add(bytes, value.size(), 2); return bytes + value;
}
QByteArray unicode(const QByteArray& wire, const QByteArray& name, quint32 checksumDelta = 0, quint8 version = 1)
{
	QByteArray bytes; add(bytes, version, 1); add(bytes, crc32Bytes(wire) + checksumDelta, 4);
	return field(0x7075, bytes + name);
}
QByteArray signature()
{
	QByteArray bytes; add(bytes, 0x05054b50, 4); add(bytes, 3, 2); return bytes + "sig";
}

struct Options {
	QByteArray name = "item.txt";
	QByteArray payload = "synthetic package payload\n";
	QByteArray localExtra, centralExtra, extension, centralSuffix, followingCentral;
	quint16 flags = 0;
	bool deflate = false, descriptor = false, signedDescriptor = true;
	bool localZip64 = false, endZip64 = false, wideDescriptor = false;
	int centralZip64 = 0; // 1=size, 2=compressed size, 4=offset, 8=disk
};
struct Fixture {
	QByteArray bytes, payload, wireName;
	qsizetype localExtra = 0, payloadAt = 0, descriptor = 0, central = 0, centralExtra = 0, centralSize = 0;
	qsizetype zip64 = 0, locator = 0, end = 0;
};
Fixture make(const Options& options = {})
{
	Fixture f; f.payload = options.payload; f.wireName = options.name;
	const auto stored = options.deflate ? deflateRaw(options.payload) : options.payload;
	const auto checksum = crc32Bytes(options.payload);
	const quint16 flags = options.flags | (options.descriptor ? 8 : 0);
	const quint16 method = options.deflate ? 8 : 0;
	QByteArray localExtra = options.localExtra, centralExtra = options.centralExtra;
	if (options.localZip64) {
		QByteArray sizes; add(sizes, options.descriptor ? 0 : options.payload.size(), 8); add(sizes, options.descriptor ? 0 : stored.size(), 8);
		localExtra = field(1, sizes) + localExtra;
	}
	if (options.centralZip64) {
		QByteArray values;
		if (options.centralZip64 & 1) { add(values, options.payload.size(), 8); }
		if (options.centralZip64 & 2) { add(values, stored.size(), 8); }
		if (options.centralZip64 & 4) { add(values, 0, 8); }
		if (options.centralZip64 & 8) { add(values, 0, 4); }
		centralExtra = field(1, values) + centralExtra;
	}
	QByteArray& b = f.bytes;
	add(b, 0x04034b50, 4); add(b, options.localZip64 ? 45 : 20, 2); add(b, flags, 2); add(b, method, 2);
	add(b, 0, 4); add(b, options.descriptor ? 0 : checksum, 4);
	add(b, options.localZip64 ? 0xffffffffu : options.descriptor ? 0 : stored.size(), 4);
	add(b, options.localZip64 ? 0xffffffffu : options.descriptor ? 0 : options.payload.size(), 4);
	add(b, options.name.size(), 2); add(b, localExtra.size(), 2); b += options.name;
	f.localExtra = b.size(); b += localExtra; f.payloadAt = b.size(); b += stored;
	f.descriptor = b.size();
	if (options.descriptor) {
		if (options.signedDescriptor) { add(b, 0x08074b50, 4); }
		add(b, checksum, 4);
		const int width = options.wideDescriptor || options.localZip64 || (options.centralZip64 & 3) ? 8 : 4;
		add(b, stored.size(), width); add(b, options.payload.size(), width);
	}
	f.central = b.size();
	add(b, 0x02014b50, 4); add(b, 45, 2); add(b, 45, 2); add(b, flags, 2); add(b, method, 2);
	add(b, 0, 4); add(b, checksum, 4);
	add(b, options.centralZip64 & 2 ? 0xffffffffu : stored.size(), 4);
	add(b, options.centralZip64 & 1 ? 0xffffffffu : options.payload.size(), 4);
	add(b, options.name.size(), 2); add(b, centralExtra.size(), 2); add(b, 0, 2);
	add(b, options.centralZip64 & 8 ? 0xffffu : 0, 2); add(b, 0, 2); add(b, 0, 4);
	add(b, options.centralZip64 & 4 ? 0xffffffffu : 0, 4); b += options.name;
	f.centralExtra = b.size(); b += centralExtra; b += options.centralSuffix;
	f.centralSize = b.size() - f.central; b += options.followingCentral;
	f.zip64 = b.size();
	if (options.endZip64) {
		add(b, 0x06064b50, 4); add(b, 44 + options.extension.size(), 8); add(b, 45, 2); add(b, 45, 2);
		add(b, 0, 8); add(b, 1, 8); add(b, 1, 8); add(b, f.centralSize, 8); add(b, f.central, 8); b += options.extension;
		f.locator = b.size(); add(b, 0x07064b50, 4); add(b, 0, 4); add(b, f.zip64, 8); add(b, 1, 4);
	}
	f.end = b.size(); add(b, 0x06054b50, 4); add(b, 0, 4);
	add(b, options.endZip64 ? 0xffff : 1, 2); add(b, options.endZip64 ? 0xffff : 1, 2);
	add(b, options.endZip64 ? 0xffffffffu : f.centralSize, 4); add(b, options.endZip64 ? 0xffffffffu : f.central, 4); add(b, 0, 2);
	return f;
}

bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

enum class Outcome { Valid, Unreadable, Skipped, Rejected };

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	QDir root(temporary.path());
	bool ok = true;
	int cases = 0;
	auto expect = [&](bool condition, const QString& label, const char* message) {
		if (!condition) { std::cerr << label.toStdString() << ": " << message << '\n'; ok = false; }
		return condition;
	};
	auto check = [&](const QString& label, const Fixture& fixture, Outcome outcome, const QString& name = QStringLiteral("item.txt"), bool integration = false) {
		++cases;
		const QString path = root.filePath(label + QStringLiteral(".pk3"));
		if (!expect(write(path, fixture.bytes), label, "write fixture")) { return; }
		PackageArchive archive;
		QString error;
		const bool opened = archive.load(path, &error);
		if (!expect(opened == (outcome != Outcome::Rejected), label, qPrintable(error))) { return; }
		if (!opened) {
			expect(!archive.isOpen() && archive.entries().isEmpty() && !error.isEmpty(), label, "rejection must leave no partial index");
		} else {
			const auto validation = validatePackage(archive);
			expect(validation.valid() == (outcome == Outcome::Valid), label, "validation result");
			if (outcome == Outcome::Skipped) {
				expect(archive.entries().isEmpty() && !archive.warnings().isEmpty(), label, "skip must be visible, not a replacement filename");
			} else {
				const auto entries = archive.entries();
				const auto found = std::find_if(entries.cbegin(), entries.cend(), [&](const auto& e) { return e.sourceOrdinal == 0; });
				if (!expect(found != entries.cend(), label, "preserve source occurrence")) { return; }
				expect(found->virtualPath == name, label, "decoded filename");
				if (found->kind == PackageEntryKind::File) {
					QByteArray payload;
					expect(archive.readEntryBytes(name, &payload, &error) == (outcome == Outcome::Valid), label, "payload readability");
					if (outcome == Outcome::Valid) { expect(payload == fixture.payload, label, "payload bytes"); }
					else {
						expect(!found->readable && found->dataOffset < 0 && !found->note.isEmpty(), label, "damaged local metadata must remain listed with a reason");
						int chunks = 0;
						expect(!archive.streamEntryAt(found - entries.cbegin(), [&](QByteArrayView) { ++chunks; return true; }, &error) && chunks == 0,
							label, "malformed metadata must fail before streaming any bytes");
					}
				}
			}
			if (integration) {
				PackageArchiveSession session; QByteArray resolved;
				expect(session.openPrimaryArchive(path, &error), label, "open shared asset lookup layer");
				expect(session.readEntryBytes(name, &resolved, &error) == (outcome == Outcome::Valid), label, "shared asset lookup readability");
				if (outcome == Outcome::Valid) { expect(resolved == fixture.payload, label, "asset lookup keeps decoded identity"); }
				PackageExtractionRequest extraction; extraction.extractAll = true;
				extraction.targetDirectory = root.filePath(label + QStringLiteral("-extracted"));
				const auto extracted = extractPackageEntries(archive, extraction);
				expect(extracted.succeeded() == (outcome == Outcome::Valid), label, "extraction result");
				const QString output = QDir(extraction.targetDirectory).filePath(name);
				expect(outcome == Outcome::Valid ? read(output) == fixture.payload : !QFileInfo::exists(output), label, "extraction destination");
				PackageStagingModel staging;
				if (expect(staging.loadBaseArchive(archive, &error), label, "capture immutable base")) {
					PackageWriteRequest request; request.format = PackageArchiveFormat::Pk3;
					request.destinationPath = root.filePath(label + QStringLiteral("-saved.pk3"));
					const auto saved = staging.writeArchive(request);
					expect(saved.succeeded() == (outcome == Outcome::Valid), label, "save result");
					if (saved.succeeded()) {
						PackageArchive reopened; QByteArray payload;
						expect(reopened.load(request.destinationPath, &error) && reopened.readEntryBytes(name, &payload, &error)
							&& payload == fixture.payload && validatePackage(reopened).valid(), label, "canonical UTF-8 writer round trip");
					} else {
						expect(!QFileInfo::exists(request.destinationPath), label, "blocked save creates no destination");
						const auto before = packagePlannedArchive(staging, &error);
						expect(before.isOpen() && before.entries().size() == 1 && !before.entries().first().readable,
							label, "planned browser keeps one unreadable occurrence");
						expect(staging.deleteOccurrence(0, &error) && staging.writeArchive(request).succeeded(), label, "explicit removal can repair an unreadable local entry");
						const auto after = packagePlannedArchive(staging, &error);
						expect(after.isOpen() && after.entries().isEmpty(), label, "deleted unavailable rows do not reappear in the browser");
						const auto draft = root.filePath(label + QStringLiteral("-repaired.vibepackage"));
						PackageStagingModel restored;
						expect(PackageDraft::save(draft, &staging, false, &error) && PackageDraft::load(draft, &restored, &error),
							label, "repaired draft retains unavailable original history");
						expect(restored.undo() && !restored.summary().canSave, label, "Undo restores metadata and blocks export");
						const auto undone = packagePlannedArchive(restored, &error);
						expect(undone.isOpen() && undone.entries().size() == 1 && !undone.entries().first().readable,
							label, "draft Undo keeps one unavailable occurrence");
						expect(restored.redo() && restored.summary().canSave, label, "Redo restores repaired plan");
						expect(staging.undo() && staging.addBytes("replacement", name, &error, PackageStageConflictResolution::ReplaceExisting)
							&& staging.summary().canSave, label, "explicit replacement repairs unavailable content");
						PackageWriteRequest replacement = request; replacement.destinationPath = root.filePath(label + QStringLiteral("-replacement.pk3"));
						PackageArchive replaced; QByteArray replacementBytes;
						expect(staging.writeArchive(replacement).succeeded() && replaced.load(replacement.destinationPath, &error)
							&& replaced.readEntryBytes(name, &replacementBytes, &error) && replacementBytes == "replacement",
							label, "replacement exports only the supplied content");
						PackageArchive repaired;
						expect(repaired.load(request.destinationPath, &error) && repaired.entries().isEmpty() && validatePackage(repaired).valid(), label, "repaired empty ZIP is valid");
					}
				}
			}
		}
		expect(read(path) == fixture.bytes, label, "source stays unchanged");
	};

	check(QStringLiteral("stored"), make(), Outcome::Valid);
	for (bool wide : {false, true}) {
		for (bool signedDescriptor : {false, true}) {
			for (bool compressed : {false, true}) {
				Options o; o.descriptor = true; o.signedDescriptor = signedDescriptor; o.deflate = compressed; o.localZip64 = wide;
				check(QStringLiteral("descriptor-%1-%2-%3").arg(wide).arg(signedDescriptor).arg(compressed), make(o), Outcome::Valid);
				o.payload = QByteArray::fromHex("ac0a7ad5"); // Python zlib independently confirms CRC 0x08074b50.
				check(QStringLiteral("signature-crc-%1-%2-%3").arg(wide).arg(signedDescriptor).arg(compressed), make(o), Outcome::Valid);
			}
		}
	}
	for (int mask = 1; mask < 16; ++mask) {
		Options o; o.centralZip64 = mask; o.descriptor = true;
		check(QStringLiteral("central-zip64-%1").arg(mask), make(o), Outcome::Valid);
	}
	for (const int mask : {4, 8, 12}) {
		for (bool signedDescriptor : {false, true}) {
			Options o; o.centralZip64 = mask; o.descriptor = true; o.wideDescriptor = true; o.signedDescriptor = signedDescriptor;
			check(QStringLiteral("offset-only-wide-%1-%2").arg(mask).arg(signedDescriptor), make(o), Outcome::Valid);
		}
	}
	Options end; end.endZip64 = true;
	check(QStringLiteral("zip64-end"), make(end), Outcome::Valid);
	add(end.extension, 0xbeef, 2); add(end.extension, 3, 4); end.extension += "ext";
	check(QStringLiteral("zip64-extension"), make(end), Outcome::Valid);
	Options sig; sig.centralSuffix = signature(); check(QStringLiteral("signature-in-directory"), make(sig), Outcome::Valid);
	sig.centralSuffix.clear(); sig.followingCentral = signature(); check(QStringLiteral("signature-after-directory"), make(sig), Outcome::Valid);
	Options directory; directory.name = "folder/"; directory.payload.clear();
	check(QStringLiteral("directory"), make(directory), Outcome::Valid, QStringLiteral("folder"));

	using Mutator = std::function<void(Fixture&)>;
	const QList<QPair<QString, Mutator>> endMutations = {
		{QStringLiteral("short-zip64-end"), [](auto& f) { put(f.bytes, f.zip64 + 4, 43, 8); }},
		{QStringLiteral("long-zip64-end"), [](auto& f) { put(f.bytes, f.zip64 + 4, 0xffffffffffffffffull, 8); }},
		{QStringLiteral("outside-zip64-offset"), [](auto& f) { put(f.bytes, f.locator + 8, 0xffffffffffffffffull, 8); }},
		{QStringLiteral("overlap-zip64-offset"), [](auto& f) { put(f.bytes, f.locator + 8, f.locator - 4, 8); }},
		{QStringLiteral("zip64-multiple-disks"), [](auto& f) { put(f.bytes, f.locator + 16, 2, 4); }},
		{QStringLiteral("zip64-locator-disk"), [](auto& f) { put(f.bytes, f.locator + 4, 1, 4); }},
		{QStringLiteral("zip64-record-disk"), [](auto& f) { put(f.bytes, f.zip64 + 16, 1, 4); }},
		{QStringLiteral("zip64-disk-count"), [](auto& f) { put(f.bytes, f.zip64 + 24, 0, 8); }},
		{QStringLiteral("classic-zip64-disagree"), [](auto& f) { put(f.bytes, f.end + 10, 0, 2); }},
		{QStringLiteral("central-overlaps-zip64"), [](auto& f) { put(f.bytes, f.zip64 + 40, f.centralSize + 1, 8); }},
		{QStringLiteral("truncated-extension"), [](auto& f) { put(f.bytes, f.zip64 + 58, 4, 4); }},
	};
	for (const auto& [label, mutate] : endMutations) { auto f = make(end); mutate(f); check(label, f, Outcome::Rejected); }
	auto mismatch = make(); put(mismatch.bytes, mismatch.end + 8, 0, 2); check(QStringLiteral("classic-counts"), mismatch, Outcome::Rejected);
	mismatch = make(); put(mismatch.bytes, mismatch.end + 4, 1, 2); check(QStringLiteral("classic-multidisk"), mismatch, Outcome::Rejected);
	Options garbage; garbage.centralSuffix = "extra"; check(QStringLiteral("central-trailer"), make(garbage), Outcome::Rejected);
	garbage.centralSuffix.clear(); garbage.followingCentral = "garbage"; check(QStringLiteral("central-gap"), make(garbage), Outcome::Rejected);
	garbage.centralSuffix = make().bytes.sliced(make().central, make().centralSize); garbage.followingCentral.clear();
	check(QStringLiteral("unconsumed-record"), make(garbage), Outcome::Rejected);

	Options descriptor; descriptor.descriptor = true;
	auto missing = make(descriptor); put(missing.bytes, missing.descriptor, 0, 4); check(QStringLiteral("missing-descriptor"), missing, Outcome::Unreadable, QStringLiteral("item.txt"), true);
	auto badSize = make(descriptor); put(badSize.bytes, badSize.descriptor + 12, badSize.payload.size() + 1, 4);
	check(QStringLiteral("descriptor-size"), badSize, Outcome::Unreadable);
	auto local = make(); put(local.bytes, 18, 0xffffffffu, 4); check(QStringLiteral("missing-local-zip64"), local, Outcome::Unreadable);
	Options local64; local64.localZip64 = true; local = make(local64);
	put(local.bytes, local.localExtra + 4, local.payload.size() + 1, 8); check(QStringLiteral("local-zip64-size"), local, Outcome::Unreadable);
	local = make(local64); put(local.bytes, local.localExtra + 2, 8, 2); check(QStringLiteral("short-local-zip64"), local, Outcome::Unreadable);
	local = make(); put(local.bytes, 14, 1, 4); check(QStringLiteral("local-crc"), local, Outcome::Unreadable);
	local = make(); put(local.bytes, 6, 8, 2); check(QStringLiteral("local-flags"), local, Outcome::Unreadable);
	local = make(); put(local.bytes, 8, 8, 2); check(QStringLiteral("local-method"), local, Outcome::Unreadable);
	local = make(); local.bytes[30] = 'x'; check(QStringLiteral("local-name"), local, Outcome::Unreadable);
	local = make(directory); put(local.bytes, 0, 0, 4); check(QStringLiteral("bad-directory-header"), local, Outcome::Unreadable, QStringLiteral("folder"));

	for (bool central : {false, true}) {
		Options o; (central ? o.centralExtra : o.localExtra) = QByteArray::fromHex("feca0300ff");
		check(QStringLiteral("truncated-extra-%1").arg(central), make(o), central ? Outcome::Skipped : Outcome::Unreadable);
		(central ? o.centralExtra : o.localExtra) = QByteArray::fromHex("feca0000");
		check(QStringLiteral("unknown-extra-%1").arg(central), make(o), Outcome::Valid);
		o = {}; o.localZip64 = true; o.centralZip64 = 3;
		QByteArray sizes; add(sizes, o.payload.size(), 8); add(sizes, o.payload.size(), 8);
		(central ? o.centralExtra : o.localExtra) = field(1, sizes);
		check(QStringLiteral("duplicate-zip64-%1").arg(central), make(o), central ? Outcome::Skipped : Outcome::Unreadable);
	}
	Options legacy; legacy.name = QByteArray::fromHex("636166822e747874");
	check(QStringLiteral("cp437"), make(legacy), Outcome::Valid, QString::fromUtf8("caf\xc3\xa9.txt"), true);
	Options utf8; utf8.flags = 0x0800; utf8.name = QByteArray::fromHex("636166c3a92e747874");
	check(QStringLiteral("utf8"), make(utf8), Outcome::Valid, QString::fromUtf8(utf8.name), true);
	for (const auto& hex : {"ff", "c080", "eda080", "f4908080", "e282", "efbbbf61"}) {
		utf8.name = QByteArray::fromHex(hex) + ".txt";
		check(QStringLiteral("invalid-utf8-") + QLatin1String(hex), make(utf8), Outcome::Skipped);
	}
	utf8.name = QByteArray::fromHex("efbfbd") + ".txt";
	check(QStringLiteral("literal-replacement-character"), make(utf8), Outcome::Valid, QString::fromUtf8(utf8.name));
	const auto japanese = QString::fromUtf8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.txt");
	legacy.centralExtra = unicode(legacy.name, japanese.toUtf8());
	check(QStringLiteral("unicode-extra"), make(legacy), Outcome::Valid, japanese, true);
	legacy.localExtra = legacy.centralExtra;
	check(QStringLiteral("unicode-both"), make(legacy), Outcome::Valid, japanese);
	legacy.localExtra = unicode(legacy.name, "different.txt");
	check(QStringLiteral("unicode-mismatch"), make(legacy), Outcome::Unreadable, japanese);
	legacy.localExtra.clear(); legacy.centralExtra = unicode(legacy.name, "../escaped.txt");
	check(QStringLiteral("unsafe-unicode"), make(legacy), Outcome::Skipped);
	legacy.centralExtra = unicode(legacy.name, QByteArray::fromHex("ff"));
	check(QStringLiteral("invalid-unicode-extra"), make(legacy), Outcome::Skipped);
	legacy.centralExtra = unicode(legacy.name, japanese.toUtf8(), 1);
	check(QStringLiteral("stale-unicode"), make(legacy), Outcome::Valid, QString::fromUtf8("caf\xc3\xa9.txt"));
	legacy.centralExtra = unicode(legacy.name, japanese.toUtf8(), 0, 2);
	check(QStringLiteral("unknown-unicode-version"), make(legacy), Outcome::Valid, QString::fromUtf8("caf\xc3\xa9.txt"));
	legacy.centralExtra = unicode(legacy.name, japanese.toUtf8()); legacy.centralExtra += legacy.centralExtra;
	check(QStringLiteral("duplicate-unicode"), make(legacy), Outcome::Skipped);

	const auto bounded = make(end);
	const auto path = root.filePath(QStringLiteral("limits.pk3")); write(path, bounded.bytes);
	PackageArchive archive; QString error;
	PackageIndexLimits limits; limits.maximumMetadataBytes = end.extension.size() - 1;
	expect(!archive.load(path, &error, {}, limits) && !archive.isOpen() && archive.entries().isEmpty(), QStringLiteral("limits"), "ZIP64 extension respects metadata budget");
	PackageReadControl control; bool cancel = false;
	control.progress = [&](const QString& phase, qint64, qint64) { if (phase.startsWith(QStringLiteral("Indexing "))) { cancel = true; } };
	control.isCancelled = [&] { return cancel; };
	expect(!archive.load(path, &error, control) && !archive.isOpen() && archive.entries().isEmpty(), QStringLiteral("cancel"), "cancelled admission retains no partial index");
	expect(archive.load(path, &error) && validatePackage(archive).valid(), QStringLiteral("reuse"), "reader remains reusable");
	std::cout << cases << " ZIP metadata and encoding fixtures checked\n";
	return ok ? 0 : 1;
}
