#pragma once

#include "core/game_installation.h"
#include "core/idtech_image.h"

#include <QHash>
#include <QObject>
#include <functional>
#include <memory>
#include <optional>

class QThread;
class QTimer;

namespace vibestudio {

struct TexturePreviewSource {
	std::shared_ptr<const PackageArchiveReader> archive;
	QString revision;
	QString paletteId;
	bool paletteFromFormat = false;
	GameInstallationProfile installation;
};

// Value-only resolver shared by browser jobs and explicit editor refreshes.
// Run off the UI thread: an installation archive may need fingerprinting.
IdTechPaletteResolution resolveTexturePreviewPalette(const TexturePreviewSource& source, const QString& paletteId,
	const std::function<bool()>& isCancelled = {});

struct TexturePreviewRequest {
	TexturePreviewSource source;
	QString virtualPath;
	QString decodePath;
	// Exact package occurrence, when the caller selected a directory row.
	qsizetype entryIndex = -1;
	// Positive requests a bounded thumbnail instead of retaining decoded frames.
	int thumbnailSide = 0;
	// A dimension/format search needs metadata for offscreen rows, not pixels.
	bool metadataOnly = false;
	// Level materials may already be resolved from a project/installation.
	// Share that bounded image with the worker instead of rereading a package.
	QImage suppliedImage;
	QSize suppliedSourceSize;
};

struct TexturePreviewResult {
	QString revision;
	QString virtualPath;
	qsizetype entryIndex = -1;
	IdTechImageDecodeResult decoded;
	IdTechPaletteResolution palette;
	QImage thumbnail;
};

// One active value-only job and one replaceable pending request. Superseded
// work never publishes. Read/codec phases are bounded; cancellation is checked
// between them and while fingerprinting an installation package.
class TexturePreviewWorker final : public QObject {
public:
	explicit TexturePreviewWorker(QObject* parent = nullptr);
	~TexturePreviewWorker() override;
	void request(TexturePreviewRequest request);
	void cancel();
	[[nodiscard]] bool busy() const;
	std::function<void(int phase)> progress;
	std::function<void(const TexturePreviewResult&)> completed;

private:
	struct Work;
	quint64 m_serial = 0;
	std::optional<TexturePreviewRequest> m_pending;
	std::shared_ptr<Work> m_work;
	QThread* m_thread = nullptr;
	QTimer* m_timer = nullptr;
	QString m_paletteRevision;
	QHash<QString, IdTechPaletteResolution> m_palettes;
	void startNext();
};

} // namespace vibestudio
