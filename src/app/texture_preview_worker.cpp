#include "app/texture_preview_worker.h"

#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <new>

namespace vibestudio {
namespace {
bool matchingInstallation(const GameInstallationProfile& installation, const QString& paletteId)
{
	if (installation.id.isEmpty()) { return false; }
	const bool doom = paletteId == QStringLiteral("doom") || paletteId == QStringLiteral("heretic") || paletteId == QStringLiteral("hexen");
	return doom ? installation.engineFamily == GameEngineFamily::IdTech1
		: paletteId.startsWith(QStringLiteral("quake2")) ? installation.engineFamily == GameEngineFamily::IdTech2
		: installation.engineFamily == GameEngineFamily::IdTech2 || installation.engineFamily == GameEngineFamily::IdTech3;
}
} // namespace

IdTechPaletteResolution resolveTexturePreviewPalette(const TexturePreviewSource& source, const QString& paletteId,
	const std::function<bool()>& isCancelled)
{
	if (isCancelled && isCancelled()) { return {}; }
	IdTechPaletteResolution palette;
	if (source.archive) { palette = resolveIdTechPalette(*source.archive, paletteId); }
	else { palette.requestedPaletteId = paletteId; palette.palette = generatedIdTechPalette(paletteId); }
	if (isCancelled && isCancelled()) { return {}; }
	if (!palette.fromPackage && matchingInstallation(source.installation, paletteId)) {
		PackageReadControl control; control.isCancelled = isCancelled;
		for (const auto& path : source.installation.basePackagePaths) {
			if (isCancelled && isCancelled()) { return {}; }
			PackageArchive installed; QString error;
			const QString fullPath = QDir(source.installation.rootPath).absoluteFilePath(path);
			if (!installed.load(fullPath, &error, control)) { continue; }
			auto found = resolveIdTechPalette(installed, paletteId);
			if (found.fromPackage) { found.sourcePackagePath = fullPath; palette = std::move(found); break; }
		}
	}
	return isCancelled && isCancelled() ? IdTechPaletteResolution{} : palette;
}

struct TexturePreviewWorker::Work {
	quint64 serial = 0;
	std::atomic_bool cancelled = false;
	std::atomic_int phase = 0;
	TexturePreviewResult result;
	QHash<QString, IdTechPaletteResolution> palettes;
};

TexturePreviewWorker::TexturePreviewWorker(QObject* parent) : QObject(parent)
{
	m_timer = new QTimer(this);
	m_timer->setInterval(75);
	connect(m_timer, &QTimer::timeout, this, [this] {
		if (m_work && m_work->serial == m_serial && progress) { progress(m_work->phase.load()); }
	});
}

TexturePreviewWorker::~TexturePreviewWorker()
{
	cancel();
	if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
}

bool TexturePreviewWorker::busy() const { return m_thread || m_pending.has_value(); }

void TexturePreviewWorker::cancel()
{
	++m_serial;
	m_pending.reset();
	if (m_work) { m_work->cancelled = true; }
	m_timer->stop();
}

void TexturePreviewWorker::request(TexturePreviewRequest request)
{
	cancel();
	m_pending = std::move(request);
	// Give rapid list/selection changes one event-loop turn to coalesce.
	QTimer::singleShot(0, this, [this] { startNext(); });
}

void TexturePreviewWorker::startNext()
{
	if (m_thread || !m_pending) { return; }
	auto request = std::move(*m_pending); m_pending.reset();
	auto work = std::make_shared<Work>(); m_work = work;
	work->serial = m_serial;
	work->result.revision = request.source.revision;
	work->result.virtualPath = request.virtualPath;
	work->result.entryIndex = request.entryIndex;
	if (m_paletteRevision == request.source.revision) { work->palettes = m_palettes; }
	m_thread = QThread::create([work, request = std::move(request)] {
		try {
			if (work->cancelled) { return; }
			if (!request.suppliedImage.isNull()) {
				work->phase = 3;
				const auto size = request.suppliedSourceSize.isValid() ? request.suppliedSourceSize : request.suppliedImage.size();
				work->result.decoded.decoded = true; work->result.decoded.width = size.width(); work->result.decoded.height = size.height();
				const int side = std::clamp(request.thumbnailSide, 1, 1024);
				const bool upscale = request.suppliedImage.width() <= side && request.suppliedImage.height() <= side;
				work->result.thumbnail = request.suppliedImage.scaled(side, side, Qt::KeepAspectRatio, upscale ? Qt::FastTransformation : Qt::SmoothTransformation);
				return;
			}
			if (!request.source.archive) { return; }
			const auto& archive = *request.source.archive;
			QByteArray bytes;
			const bool read = request.virtualPath.isEmpty() || (request.entryIndex >= 0
				? readIdTechImageEntryAt(archive, request.entryIndex, &bytes, &work->result.decoded.error)
				: readIdTechImageEntry(archive, request.virtualPath, &bytes, &work->result.decoded.error));
			if (!read || work->cancelled) { return; }
			work->phase = 1;
			const auto format = detectIdTechImageFormat(request.decodePath, bytes);
			const QString paletteId = request.source.paletteFromFormat && !request.virtualPath.isEmpty()
				? defaultIdTechPaletteIdForImage(format, bytes.size()) : request.source.paletteId;
			if (!work->palettes.contains(paletteId)) {
				auto palette = resolveTexturePreviewPalette(request.source, paletteId, [work] { return work->cancelled.load(); });
				work->palettes.insert(paletteId, std::move(palette));
			}
			if (work->cancelled) { return; }
			work->result.palette = work->palettes.value(paletteId);
			if (request.virtualPath.isEmpty()) { return; }
			work->phase = 2;
			IdTechImageDecodeContext context; context.archive = &archive;
			context.isCancelled = [work] { return work->cancelled.load(); };
			work->result.decoded = decodeIdTechImage(request.decodePath, bytes, work->result.palette.palette, context);
			if (work->cancelled) { return; }
			if ((request.thumbnailSide > 0 || request.metadataOnly) && work->result.decoded.decoded) {
				work->phase = 3;
				const auto& image = work->result.decoded.image;
				const int side = std::clamp(request.thumbnailSide, 1, 1024);
				const bool upscale = image.width() <= side && image.height() <= side;
				if (!request.metadataOnly) { work->result.thumbnail = image.scaled(side, side, Qt::KeepAspectRatio, upscale ? Qt::FastTransformation : Qt::SmoothTransformation); }
				work->result.decoded.image = {};
				work->result.decoded.mipLevels.clear();
				work->result.decoded.frames.clear();
			}
		} catch (const std::bad_alloc&) {
			work->result.decoded = {};
			work->result.decoded.error = QCoreApplication::translate("VibeStudioTexturePreview", "Not enough memory to decode this texture.");
		}
	});
	m_thread->setParent(this);
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread->deleteLater(); m_thread = nullptr; m_timer->stop();
		if (work->serial == m_serial && !work->cancelled) {
			m_paletteRevision = work->result.revision;
			m_palettes = work->palettes;
			if (completed) { completed(work->result); }
		}
		if (m_work == work) { m_work.reset(); }
		startNext();
	});
	m_timer->start(); m_thread->start();
}

} // namespace vibestudio
