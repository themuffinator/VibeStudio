#include "app/package_preview_worker.h"
#include "core/package_staging.h"
#include "core/deflate.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool until(const std::function<bool()>& ready, int timeout = 10000)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < timeout) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return ready();
}
class Reader final : public PackageArchiveReader {
public:
	QVector<PackageEntry> directory;
	QVector<QByteArray> payloads;
	mutable std::atomic_int reads = 0, active = 0, maximumActive = 0, stops = 0;
	mutable std::atomic_bool reading = false, release = true, onGui = false;
	mutable std::atomic<qint64> delivered = 0;
	bool failAtEnd = false;
	void add(const QString& path, const QByteArray& bytes) { PackageEntry entry; entry.virtualPath = path; entry.sizeBytes = bytes.size(); directory << entry; payloads << bytes; }
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Pak; }
	QString sourcePath() const override { return QStringLiteral("immutable-preview-fixture"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return directory; }
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { return false; }
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& cancelled) const override
	{
		++reads; reading = true; const int count = ++active; maximumActive = std::max(maximumActive.load(), count);
		struct Leave { std::atomic_int& active; ~Leave() { --active; } } leave{active};
		if (QThread::currentThread() == QCoreApplication::instance()->thread()) { onGui = true; }
		QElapsedTimer timer; timer.start();
		while (!release && timer.elapsed() < 5000) { if (cancelled && cancelled()) { ++stops; return false; } QThread::msleep(2); }
		if (index < 0 || index >= payloads.size()) { return false; }
		const auto& bytes = payloads.at(index);
		for (qsizetype offset = 0; offset < bytes.size(); offset += 4096) {
			if (cancelled && cancelled()) { ++stops; return false; }
			const auto chunk = QByteArrayView(bytes).sliced(offset, std::min<qsizetype>(4096, bytes.size() - offset));
			delivered += chunk.size();
			if (!sink(chunk)) { ++stops; return false; }
		}
		if (failAtEnd && error) { *error = QStringLiteral("final integrity check failed"); }
		return !failAtEnd;
	}
};
PackagePreviewRequest request(const std::shared_ptr<const PackageArchiveReader>& reader, const QString& revision, qsizetype index)
{
	PackagePreviewRequest value; value.archive = reader; value.revision = revision; value.entryIndex = index;
	value.virtualPath = reader->entries().at(index).virtualPath; return value;
}
void u16(QByteArray& data, quint16 value) { data.append(char(value)); data.append(char(value >> 8)); }
void u32(QByteArray& data, quint32 value) { for (int i = 0; i < 4; ++i) { data.append(char(value >> (8 * i))); } }
QByteArray zip(const QByteArray& bytes, quint32 crc)
{
	const QByteArray name("text.txt"), encoded = deflateRaw(bytes);
	QByteArray output; u32(output, 0x04034b50); u16(output, 20); u16(output, 0); u16(output, 8);
	u16(output, 0); u16(output, 0); u32(output, crc); u32(output, encoded.size()); u32(output, bytes.size());
	u16(output, name.size()); u16(output, 0); output += name; output += encoded;
	const auto offset = output.size();
	u32(output, 0x02014b50); u16(output, 20); u16(output, 20); u16(output, 0); u16(output, 8);
	u16(output, 0); u16(output, 0); u32(output, crc); u32(output, encoded.size()); u32(output, bytes.size());
	u16(output, name.size()); u16(output, 0); u16(output, 0); u16(output, 0); u16(output, 0); u32(output, 0); u32(output, 0); output += name;
	const auto size = output.size() - offset;
	u32(output, 0x06054b50); u16(output, 0); u16(output, 0); u16(output, 1); u16(output, 1); u32(output, size); u32(output, offset); u16(output, 0);
	return output;
}
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true;
	auto reader = std::make_shared<Reader>(); reader->add(QStringLiteral("same.txt"), "first occurrence"); reader->add(QStringLiteral("same.txt"), "second occurrence");
	PackagePreviewWorker worker; QVector<PackagePreviewResult> results;
	worker.completed = [&](const auto& result) { ok &= expect(QThread::currentThread() == app.thread(), "completion returns on UI thread"); results << result; };
	int ticks = 0; QTimer heartbeat; heartbeat.setInterval(5);
	QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; }); heartbeat.start();
	reader->release = false;
	worker.request(request(reader, QStringLiteral("old"), 0));
	ok &= expect(until([&] { return reader->reading.load(); }), "background preview starts");
	for (int index = 0; index < 40; ++index) { worker.request(request(reader, QString::number(index), 0)); }
	worker.request(request(reader, QStringLiteral("current"), 1));
	ok &= expect(until([&] { return ticks >= 12; }), "UI remains responsive while the reader is waiting");
	reader->release = true;
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.first().revision == QStringLiteral("current")
		&& results.first().entryIndex == 1 && results.first().preview.body == QStringLiteral("second occurrence")
		&& reader->reads == 2 && reader->maximumActive == 1 && !reader->onGui && reader->stops > 0,
		"only latest exact occurrence publishes and superseded read is cancelled");
	results.clear(); reader->reading = false; reader->release = false;
	worker.request(request(reader, QStringLiteral("cancel"), 0));
	ok &= expect(until([&] { return reader->reading.load(); }), "read starts before explicit cancellation");
	worker.cancel();
	ok &= expect(until([&] { return !worker.busy(); }) && results.isEmpty(), "explicit cancellation stops without a late result");
	reader->reading = false;
	{
		PackagePreviewWorker closing; closing.request(request(reader, QStringLiteral("closing"), 0));
		ok &= expect(until([&] { return reader->reading.load(); }), "read starts before worker destruction");
	}
	ok &= expect(reader->active == 0, "destruction joins the cancelled reader");
	reader->release = true;
	results.clear(); const int beforeInvalid = reader->reads;
	auto invalid = request(reader, QStringLiteral("invalid"), 0); invalid.byteLimit = -1;
	worker.request(invalid);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && !results.last().preview.error.isEmpty()
		&& reader->reads == beforeInvalid, "worker refuses unbounded sample requests before reading");

	auto sampled = std::make_shared<Reader>(); sampled->add(QStringLiteral("large.txt"), QByteArray(1024 * 1024, 'x'));
	PackageReadControl control; qint64 done = 0;
	control.progress = [&](const QString& path, qint64 bytes, qint64 total) { ok &= expect(path == QStringLiteral("large.txt") && bytes <= total && bytes >= done, "sample progress stays in range"); done = bytes; };
	const auto prefix = buildPackageEntryPreviewAt(*sampled, 0, 5000, 0, control);
	ok &= expect(prefix.bytesRead == 5000 && prefix.body.size() <= 5000 && prefix.truncated && prefix.error.isEmpty()
		&& done == 5000 && sampled->delivered <= 8192, "prefix stops streaming at its requested bound");
	// Declared ZIP64 sizes are metadata, not a request to allocate the payload.
	for (const quint64 total : { (quint64(1) << 53) + 1, quint64(std::numeric_limits<qint64>::max()),
		quint64(std::numeric_limits<qint64>::max()) + 1, std::numeric_limits<quint64>::max() }) {
		sampled->directory[0].sizeBytes = total; sampled->delivered = 0;
		const auto huge = buildPackageEntryPreviewAt(*sampled, 0, 4096, 0);
		ok &= expect(huge.totalBytesKnown && huge.totalBytes == total && huge.bytesRead == 4096 && huge.truncated && huge.error.isEmpty()
			&& sampled->delivered == 4096 && huge.body.size() == 4096
			&& huge.detailLines.join('\n').contains(QString::number(total))
			&& huge.assetDetailLines.join('\n').contains(QString::number(total)),
			"bounded preview and asset details retain every bit of a huge declared size");
	}
	const auto impossible = buildPackageEntryPreviewAt(*sampled, 0, -1, 0);
	ok &= expect(impossible.kind == PackagePreviewKind::Unavailable && impossible.bytesRead == 0
		&& impossible.totalBytes == std::numeric_limits<quint64>::max() && !impossible.error.isEmpty(),
		"unrepresentable full sample is refused without losing the known declared size");
	sampled->directory[0].readable = false;
	const auto unreadable = buildPackageEntryPreviewAt(*sampled, 0);
	ok &= expect(unreadable.kind == PackagePreviewKind::Unavailable && unreadable.bytesRead == 0
		&& unreadable.totalBytes == std::numeric_limits<quint64>::max(), "unreadable entries retain their exact metadata size");
	sampled->directory[0].readable = true;
	sampled->directory[0].sizeBytes = sampled->payloads[0].size();
	bool cancelled = false; control.isCancelled = [&] { return cancelled; };
	control.progress = [&](const QString&, qint64 bytes, qint64) { if (bytes >= 4096) { cancelled = true; } };
	const auto aborted = buildPackageEntryPreviewAt(*sampled, 0, 65536, 0, control);
	ok &= expect(aborted.cancelled && aborted.body.isEmpty() && aborted.bytesRead == 0 && aborted.totalBytes == sampled->directory[0].sizeBytes, "within-sample cancellation discards unverified content");
	sampled->failAtEnd = true;
	const auto failed = buildPackageEntryPreviewAt(*sampled, 0, 2 * 1024 * 1024, 0);
	ok &= expect(failed.kind == PackagePreviewKind::Unavailable && failed.body.isEmpty() && failed.error.contains("integrity") && failed.totalBytes == sampled->directory[0].sizeBytes, "failure after the final chunk invalidates full preview");
	sampled->failAtEnd = false; sampled->directory[0].sizeBytes = 10;
	ok &= expect(buildPackageEntryPreviewAt(*sampled, 0).kind == PackagePreviewKind::Unavailable, "oversized reader output is rejected");
	sampled->directory[0].sizeBytes = 2 * 1024 * 1024;
	ok &= expect(buildPackageEntryPreviewAt(*sampled, 0, 3 * 1024 * 1024, 0).kind == PackagePreviewKind::Unavailable, "short reader output is rejected");
	ok &= expect(buildPackageEntryPreview(*reader, QStringLiteral("same.txt")).kind == PackagePreviewKind::Unavailable, "path preview still refuses repeated names");

	Reader empty; empty.add(QStringLiteral("empty.txt"), {});
	const auto emptyPreview = buildPackageEntryPreviewAt(empty, 0);
	const auto missingPreview = buildPackageEntryPreviewAt(empty, 2);
	ok &= expect(emptyPreview.totalBytesKnown && emptyPreview.totalBytes == 0 && emptyPreview.error.isEmpty()
		&& !missingPreview.totalBytesKnown && !missingPreview.error.isEmpty(), "known zero-byte entries differ from missing metadata");

	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QByteArray payload(256 * 1024, 'a');
	for (const bool corrupt : {false, true}) {
		const QString path = temporary.filePath(corrupt ? QStringLiteral("bad.zip") : QStringLiteral("valid.zip"));
		PackageArchive archive; QString error;
		ok &= expect(write(path, zip(payload, crc32Bytes(payload) ^ (corrupt ? 1u : 0u))) && archive.load(path, &error), "load actual deflated archive");
		const auto full = buildPackageEntryPreview(archive, QStringLiteral("text.txt"), payload.size(), 0);
		ok &= expect(corrupt ? full.kind == PackagePreviewKind::Unavailable && full.bytesRead == 0 : full.bytesRead == payload.size() && full.error.isEmpty(), "full deflated sample checks final CRC");
		const auto head = buildPackageEntryPreview(archive, QStringLiteral("text.txt"), 1024, 0);
		ok &= expect(head.bytesRead == 1024 && head.truncated && head.error.isEmpty(), "partial deflated sample explicitly leaves tail unverified");
	}
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); plan.addBytes("original", QStringLiteral("new.txt"));
	auto snapshot = std::make_shared<PackageStagingArchive>(plan); plan.addBytes("changed", QStringLiteral("new.txt"), nullptr, PackageStageConflictResolution::ReplaceExisting);
	results.clear(); worker.request(request(snapshot, QStringLiteral("immutable"), 0));
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().preview.body == QStringLiteral("original"), "worker owns immutable planned bytes across subsequent edits");
	return ok ? 0 : 1;
}
