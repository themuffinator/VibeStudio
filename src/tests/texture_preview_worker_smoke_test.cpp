#include "app/texture_preview_worker.h"
#include "core/package_archive.h"
#include "core/texture_export.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool until(const std::function<bool()>& ready, int timeout = 15000)
{
	QElapsedTimer clock; clock.start();
	while (!ready() && clock.elapsed() < timeout) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return ready();
}
QByteArray png(const QImage& image)
{
	QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); image.save(&output, "PNG"); return bytes;
}
class Reader final : public PackageArchiveReader {
public:
	QVector<PackageEntry> directory;
	QVector<QByteArray> payloads;
	mutable std::atomic_int reads = 0;
	mutable std::atomic_bool onGui = false, reading = false, release = true;
	void add(const QString& path, const QByteArray& bytes) { PackageEntry entry; entry.virtualPath = path; entry.sizeBytes = bytes.size(); directory << entry; payloads << bytes; }
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Pak; }
	QString sourcePath() const override { return QStringLiteral("immutable-preview-fixture"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { return directory; }
	bool readEntryBytes(const QString& path, QByteArray* out, QString* error, qint64 maximum) const override {
		for (qsizetype i = 0; i < directory.size(); ++i) { if (directory[i].virtualPath == path) { return readEntryAt(i, out, error, maximum); } } return false;
	}
	bool readEntryAt(qsizetype index, QByteArray* out, QString*, qint64 maximum) const override {
		++reads; reading = true;
		if (QThread::currentThread() == QCoreApplication::instance()->thread()) { onGui = true; }
		QElapsedTimer timeout; timeout.start();
		while (!release && timeout.elapsed() < 5000) { QThread::msleep(2); }
		*out = maximum < 0 ? payloads[index] : payloads[index].left(maximum); return true;
	}
};
TexturePreviewRequest request(const std::shared_ptr<Reader>& reader, const QString& revision, const QString& path)
{
	TexturePreviewRequest value; value.source.archive = reader; value.source.revision = revision;
	value.source.paletteId = QStringLiteral("quake"); value.virtualPath = path; value.decodePath = path; return value;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); bool ok = true;
	QImage red(16, 16, QImage::Format_ARGB32); red.fill(Qt::red);
	QImage blue(16, 16, QImage::Format_ARGB32); blue.fill(Qt::blue);
	auto reader = std::make_shared<Reader>(); reader->add(QStringLiteral("red.png"), png(red)); reader->add(QStringLiteral("blue.png"), png(blue));
	TexturePreviewWorker worker; QVector<TexturePreviewResult> results;
	worker.completed = [&](const auto& result) { results << result; };
	int ticks = 0; qint64 maxGap = 0; QElapsedTimer heartbeatClock; heartbeatClock.start();
	QTimer heartbeat; heartbeat.setInterval(5);
	QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; maxGap = std::max(maxGap, heartbeatClock.restart()); }); heartbeat.start();
	reader->release = false;
	worker.request(request(reader, QStringLiteral("old"), QStringLiteral("red.png")));
	ok &= expect(until([&] { return reader->reading.load(); }), "preview reading begins on a worker");
	for (int i = 0; i < 40; ++i) { worker.request(request(reader, QString::number(i), QStringLiteral("red.png"))); }
	worker.request(request(reader, QStringLiteral("latest"), QStringLiteral("blue.png")));
	ok &= expect(until([&] { return ticks >= 12; }), "the event loop remains responsive during a slow source read");
	reader->release = true;
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.first().revision == QStringLiteral("latest")
		&& results.first().decoded.image == blue && reader->reads == 2 && !reader->onGui,
		"rapid selections coalesce to one pending job; only the newest snapshot publishes");
	results.clear(); reader->reading = false; reader->release = false;
	worker.request(request(reader, QStringLiteral("cancelled"), QStringLiteral("red.png")));
	ok &= expect(until([&] { return reader->reading.load(); }), "start a preview before cancellation");
	worker.cancel(); reader->release = true;
	ok &= expect(until([&] { return !worker.busy(); }) && results.isEmpty(), "cancellation suppresses even a decode already reading");

	IdTechPaletteResolution palette; palette.palette = generatedIdTechPalette(QStringLiteral("quake"));
	TextureExportOptions options; options.format = TextureExportFormat::QuakeMiptex; options.name = QStringLiteral("brick");
	options.allowGeneratedPalette = true;
	const auto mip = encodeTextureExport(red, options, palette);
	ok &= expect(mip.succeeded, "encode the native preview fixture");
	reader->add(QStringLiteral("brick.mip"), mip.bytes);
	worker.request(request(reader, QStringLiteral("native"), QStringLiteral("brick.mip")));
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().decoded.mipLevels.size() == 4
		&& results.last().decoded.textureName == QStringLiteral("brick"), "preview retains native metadata and mip images for editing");
	results.clear(); auto thumbnail = request(reader, QStringLiteral("native"), QStringLiteral("brick.mip")); thumbnail.thumbnailSide = 7;
	worker.request(thumbnail);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().thumbnail.size() == QSize(7, 7)
		&& results.last().decoded.image.isNull() && results.last().decoded.mipLevels.isEmpty(), "thumbnails retain only small pixels after background scaling");
	results.clear(); thumbnail.metadataOnly = true; worker.request(thumbnail);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().thumbnail.isNull() &&
		results.last().decoded.image.isNull() && results.last().decoded.mipLevels.isEmpty() && results.last().decoded.width == 16 &&
		results.last().decoded.formatId == QStringLiteral("quake-miptex"), "offscreen dimension queries retain metadata without thumbnail pixels");
	results.clear(); TexturePreviewRequest material; material.source.revision = QStringLiteral("resolved-material"); material.virtualPath = QStringLiteral("project/brick");
	material.suppliedImage = blue; material.suppliedSourceSize = QSize(64, 64); material.thumbnailSide = 8;
	worker.request(material);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().decoded.width == 64 &&
		results.last().thumbnail.size() == QSize(8, 8) && results.last().thumbnail.pixelColor(0, 0) == QColor(Qt::blue),
		"level materials from project sources scale on the same worker without an open package");

	results.clear(); auto indexed = std::make_shared<Reader>(); QByteArray rgb(768, '\0'); rgb[0] = char(240);
	indexed->add(QStringLiteral("PLAYPAL"), rgb); indexed->add(QStringLiteral("floor.flat"), QByteArray(4096, '\0'));
	auto flat = request(indexed, QStringLiteral("palette-a"), QStringLiteral("floor.flat")); flat.source.paletteFromFormat = true;
	worker.request(flat);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().palette.fromPackage
		&& results.last().decoded.image.pixelColor(0, 0) == QColor(240, 0, 0), "automatic palettes resolve inside the immutable package snapshot");
	results.clear(); auto replacement = std::make_shared<Reader>(); rgb[0] = 0; rgb[2] = char(220);
	replacement->add(QStringLiteral("PLAYPAL"), rgb); replacement->add(QStringLiteral("floor.flat"), QByteArray(4096, '\0'));
	flat.source.archive = replacement; flat.source.revision = QStringLiteral("palette-b"); worker.request(flat);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().decoded.image.pixelColor(0, 0) == QColor(0, 0, 220),
		"staged palette revisions invalidate cached resolutions");
	results.clear(); auto paletteOnly = flat; paletteOnly.virtualPath.clear(); paletteOnly.decodePath.clear(); paletteOnly.source.paletteId = QStringLiteral("doom");
	worker.request(paletteOnly);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().palette.fromPackage && results.last().decoded.image.isNull(),
		"packages without a selected image can resolve palettes asynchronously");
	results.clear(); QTemporaryDir installation;
	ok &= expect(installation.isValid() && QDir().mkpath(installation.filePath(QStringLiteral("gfx"))), "create an independent installation palette fixture");
	QFile paletteFile(installation.filePath(QStringLiteral("gfx/palette.lmp")));
	ok &= expect(paletteFile.open(QIODevice::WriteOnly) && paletteFile.write(rgb) == rgb.size(), "write generated installation palette bytes"); paletteFile.close();
	auto installed = request(reader, QStringLiteral("installation"), QStringLiteral("brick.mip"));
	installed.source.installation.id = QStringLiteral("fixture"); installed.source.installation.engineFamily = GameEngineFamily::IdTech2;
	installed.source.installation.rootPath = installation.path(); installed.source.installation.basePackagePaths = {QStringLiteral(".")};
	worker.request(installed);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().palette.fromPackage
		&& !results.last().palette.sourcePackagePath.isEmpty() && results.last().palette.palette.colors.first() == qRgb(0, 0, 220),
		"background palette fallback uses only the matching selected installation");

	results.clear(); auto duplicates = std::make_shared<Reader>(); duplicates->add(QStringLiteral("same.png"), png(red)); duplicates->add(QStringLiteral("same.png"), png(blue));
	auto occurrence = request(duplicates, QStringLiteral("duplicates"), QStringLiteral("same.png")); occurrence.entryIndex = 1;
	worker.request(occurrence);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().decoded.image == blue, "package preview honors the selected duplicate occurrence");
	results.clear(); occurrence.entryIndex = -1; worker.request(occurrence);
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && !results.last().decoded.decoded && !results.last().decoded.error.isEmpty(), "ambiguous paths fail explicitly");

	results.clear(); QImage large(4096, 4096, QImage::Format_ARGB32); large.fill(QColor(30, 40, 50, 160));
	auto maximum = std::make_shared<Reader>(); maximum->add(QStringLiteral("maximum.png"), png(large));
	const int before = ticks; heartbeatClock.restart(); maxGap = 0;
	worker.request(request(maximum, QStringLiteral("maximum"), QStringLiteral("maximum.png")));
	ok &= expect(until([&] { return !worker.busy(); }) && results.size() == 1 && results.last().decoded.image == large && ticks > before,
		"a maximum-size browser image decodes while the GUI event loop continues");
	std::cout << "Preview heartbeat ticks: " << ticks - before << ", maximum gap ms: " << maxGap << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
