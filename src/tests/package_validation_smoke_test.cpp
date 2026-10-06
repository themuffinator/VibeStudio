#include "core/deflate.h"
#include "core/package_compare.h"
#include "core/package_staging.h"
#include "core/package_validation.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
void u16(QByteArray& bytes, quint16 value)
{
	bytes.append(static_cast<char>(value)); bytes.append(static_cast<char>(value >> 8));
}
void u32(QByteArray& bytes, quint32 value)
{
	u16(bytes, static_cast<quint16>(value)); u16(bytes, static_cast<quint16>(value >> 16));
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray zip(const QByteArray& payload, quint16 method = 0, quint16 flags = 0, bool trailingDeflateBytes = false)
{
	const QByteArray name("data.bin");
	QByteArray stored = method == 8 ? deflateRaw(payload) : payload;
	if (trailingDeflateBytes) { stored.append("unexpected"); }
	const auto size = static_cast<quint32>(payload.size());
	const auto packed = static_cast<quint32>(stored.size());
	const quint32 crc = crc32Bytes(payload);
	QByteArray result;
	u32(result, 0x04034b50); u16(result, 20); u16(result, flags); u16(result, method);
	u16(result, 0); u16(result, 33); u32(result, crc); u32(result, packed); u32(result, size);
	u16(result, name.size()); u16(result, 0); result.append(name); result.append(stored);
	const auto offset = static_cast<quint32>(result.size());
	u32(result, 0x02014b50); u16(result, 20); u16(result, 20); u16(result, flags); u16(result, method);
	u16(result, 0); u16(result, 33); u32(result, crc); u32(result, packed); u32(result, size);
	u16(result, name.size()); u16(result, 0); u16(result, 0); u16(result, 0); u16(result, 0); u32(result, 0); u32(result, 0);
	result.append(name);
	const auto directorySize = static_cast<quint32>(result.size()) - offset;
	u32(result, 0x06054b50); u16(result, 0); u16(result, 0); u16(result, 1); u16(result, 1);
	u32(result, directorySize); u32(result, offset); u16(result, 0);
	return result;
}
QByteArray wad(const QByteArray& second = QByteArray("BBBB"))
{
	QByteArray result("PWAD");
	u32(result, 4); u32(result, 20); result.append("AAAA"); result.append(second);
	for (int index = 0; index < 4; ++index) {
		u32(result, index < 2 ? 12 : 16); u32(result, index % 2 ? 4 : 0);
		QByteArray name = index % 2 ? QByteArray("THINGS") : index == 0 ? QByteArray("MAP01") : QByteArray("MAP02");
		name.resize(8, '\0'); result.append(name);
	}
	return result;
}
}

int main()
{
	bool ok = true;
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	QDir root(temp.path());
	QString error;
	const QByteArray payload = QByteArray("abcdefgh01234567").repeated(20000);
	for (quint16 method : {quint16(0), quint16(8)}) {
		const QString path = root.filePath(QStringLiteral("valid-%1.pk3").arg(method));
		ok &= expect(write(path, zip(payload, method)), "create ZIP fixture");
		PackageArchive archive;
		ok &= expect(archive.load(path, &error), "open ZIP fixture");
		const auto report = validatePackage(archive);
		ok &= expect(report.valid() && report.verifiedCount == 1 && report.bytesRead == static_cast<quint64>(payload.size()), "verify complete ZIP payload");
		ok &= expect(report.entries.size() == 1 && report.entries.first().sha256 == QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()), "hash payload rather than directory metadata");
		quint64 seen = 0;
		PackageValidationRequest cancel;
		cancel.progress = [&](int, int, quint64 count, const QString&) { seen = count; };
		cancel.isCancelled = [&]() { return seen >= 65536; };
		const auto stopped = validatePackage(archive, cancel);
		ok &= expect(stopped.cancelled && !stopped.completed && !stopped.valid() && stopped.bytesRead < static_cast<quint64>(payload.size()), "cancel within a stored or compressed member");
		PackageValidationRequest budget;
		budget.maxEntryBytes = payload.size() - 1;
		const auto limited = validatePackage(archive, budget);
		ok &= expect(limited.completed && !limited.valid() && limited.uncheckedCount == 1 && limited.bytesRead == 0, "budget-limited validation is never a pass");
		ok &= expect(!packageValidationJson(limited).value(QStringLiteral("usable")).toBool(), "legacy usable field reflects integrity validation");
		// A loaded reader must not use old offsets after external replacement.
		PackageStagingModel snapshot;
		ok &= expect(snapshot.loadBaseArchive(archive, &error), "retain a staging reader snapshot");
		ok &= expect(write(path, zip(QByteArray("different layout"), method)), "replace source behind reader");
		ok &= expect(!validatePackage(archive).valid(), "detect a changed source before reading stale offsets");
		PackageWriteRequest save;
		save.destinationPath = root.filePath(QStringLiteral("stale-%1.pak").arg(method));
		ok &= expect(!snapshot.writeArchive(save).succeeded() && !QFile::exists(save.destinationPath), "staging must not reopen a changed source and bind old metadata to new bytes");
	}
	for (int variant = 0; variant < 5; ++variant) {
		QByteArray bytes = zip(payload, variant == 3 ? 8 : variant == 4 ? 99 : 0, variant == 2 ? 1 : 0, variant == 3);
		if (variant == 0) { bytes[38] ^= 1; } // payload corruption; metadata still agrees
		if (variant == 1) { bytes[18] ^= 1; } // local header contradicts directory
		const QString path = root.filePath(QStringLiteral("broken-%1.pk3").arg(variant));
		ok &= expect(write(path, bytes), "create damaged or unsupported fixture");
		PackageArchive archive;
		ok &= expect(archive.load(path, &error), "damaged payload remains inspectable");
		ok &= expect(!validatePackage(archive).valid(), "corruption, header mismatch, trailing stream bytes and unsupported methods cannot pass");
		if (variant == 0) {
			ok &= expect(!comparePackages(archive, archive).identical(), "equal CRC metadata cannot hide damaged bytes during comparison");
		}
	}
	const QString wadPath = root.filePath(QStringLiteral("maps.wad"));
	const QString changedPath = root.filePath(QStringLiteral("maps-changed.wad"));
	ok &= expect(write(wadPath, wad()) && write(changedPath, wad("CCCC")), "create repeated WAD lumps");
	PackageArchive archive, changed;
	ok &= expect(archive.load(wadPath, &error) && changed.load(changedPath, &error), "open repeated WAD lumps");
	const auto checked = validatePackage(archive);
	ok &= expect(checked.valid() && checked.verifiedCount == 4, "valid repeated WAD names are verified individually");
	const auto difference = comparePackages(archive, changed);
	ok &= expect(difference.completed && difference.summary.changedCount == 1 && difference.summary.uncomparedCount == 0, "compare the second map's own repeated lump");
	PackageStagingModel plan;
	ok &= expect(plan.loadBaseArchive(archive, &error), "load repeated WAD plan");
	ok &= expect(comparePackageToPlan(archive, plan).identical(), "compare a complete WAD staging snapshot positionally");
	PackageArchive closed;
	ok &= expect(!validatePackage(closed).valid(), "closed readers cannot validate");
	{
		root.mkpath(QStringLiteral("empty"));
		PackageArchive empty;
		ok &= expect(empty.load(root.filePath(QStringLiteral("empty")), &error), "open empty package source");
		PackageStagingModel staged;
		ok &= expect(staged.loadBaseArchive(empty, &error) && staged.addBytes("new bytes", QStringLiteral("new.txt"), &error), "stage new output");
		PackageWriteRequest save;
		save.destinationPath = root.filePath(QStringLiteral("appeared.pak"));
		save.progress = [&](int, int, const QString& path) {
			if (path.isEmpty()) { ok &= expect(write(save.destinationPath, "someone else's file"), "create destination during save"); }
		};
		const auto result = staged.writeArchive(save);
		QFile output(save.destinationPath);
		ok &= expect(output.open(QIODevice::ReadOnly), "read the independently created destination");
		ok &= expect(!result.succeeded() && output.readAll() == "someone else's file", "a destination created during save must remain untouched");
		PackageWriteRequest collision;
		collision.destinationPath = root.filePath(QStringLiteral("manifest-lock.pak"));
		collision.writeManifest = true;
		collision.manifestPath = collision.destinationPath + QStringLiteral(".vibestudio-save.lock");
		const auto rejected = staged.writeArchive(collision);
		ok &= expect(!rejected.succeeded() && !rejected.outputCommitted && !QFile::exists(collision.destinationPath), "manifest cannot replace the save lock and then disappear when it unlocks");
		PackageWriteRequest manifestFailure;
		manifestFailure.destinationPath = root.filePath(QStringLiteral("manifest-warning.pak"));
		manifestFailure.writeManifest = true;
		manifestFailure.manifestPath = root.filePath(QStringLiteral("missing-parent/manifest.json"));
		const auto committed = staged.writeArchive(manifestFailure);
		ok &= expect(committed.succeeded() && committed.outputCommitted && !committed.wroteManifest && !committed.warnings.isEmpty()
			&& QFile::exists(manifestFailure.destinationPath), "manifest failure after commit must report saved output with a warning");
	}
	return ok ? 0 : 1;
}
