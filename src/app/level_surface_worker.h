#pragma once

#include "core/level_surface_clipboard.h"
#include "core/package_archive.h"
#include <QObject>
#include <memory>

class QThread;

namespace vibestudio {
class PackageStagingModel;
struct LevelSurfaceWork {
	LevelMapDocument document;
	QVector<LevelSurfaceFace> faces;
	QVector<LevelMaterialTarget> pasteTargets;
	QVector<LevelSurfaceRequest> adjustments;
	LevelSurfaceClipboard clipboard;
	LevelSurfacePasteOptions paste;
	std::shared_ptr<const PackageArchiveReader> archive;
	std::shared_ptr<const PackageStagingModel> staging;
	std::shared_ptr<const PackageArchiveReader> clipboardArchive;
	std::shared_ptr<const PackageStagingModel> clipboardStaging;
	QString clipboardPalette;
	QString clipboardEngineFamily;
	LevelMapFormat clipboardFormat = LevelMapFormat::Unknown;
	bool clipboardContextCaptured = false;
	QString palette;
	QHash<QString, QSize> textureSizes;
	quint64 token = 0;
	std::shared_ptr<const LevelSurfaceStroke> stroke;
	bool startStroke = false;
	bool finishStroke = false;
	bool stagingIndexed = false;
};
struct LevelSurfaceResult {
	LevelMapDocument document;
	LevelSurfaceClipboard nextClipboard; // Adopt only with the verified result.
	std::shared_ptr<const PackageArchiveReader> nextClipboardArchive;
	std::shared_ptr<const PackageStagingModel> nextClipboardStaging;
	QString nextClipboardPalette;
	QString nextClipboardEngineFamily;
	LevelMapFormat nextClipboardFormat = LevelMapFormat::Unknown;
	QHash<QString, QSize> textureSizes;
	QString error;
	quint64 token = 0;
	int changedFaces = 0;
	int changedPatches = 0;
	int convertedFaces = 0;
	int edgeOnFaces = 0;
	bool cancelled = false;
	bool pasted = false;
	std::shared_ptr<const LevelSurfaceStroke> stroke;
	std::shared_ptr<const PackageArchiveReader> resolvedArchive;
	QSize sourceTextureSize;
	bool strokeResult = false;
	bool finishedStroke = false;
	bool stagingIndexed = false;
};

// One immutable batch at a time. Preparation, asset lookup, scene-lock checks
// and undo construction all run off the GUI thread. The shell verifies its
// live context before adopting the complete candidate.
class LevelSurfaceWorker final : public QObject {
public:
	explicit LevelSurfaceWorker(QObject* parent = nullptr);
	~LevelSurfaceWorker() override;
	bool start(LevelSurfaceWork request);
	void cancel();
	bool busy() const { return m_thread != nullptr; }
	std::function<void(LevelSurfaceResult)> completed;
private:
	struct Work;
	QThread* m_thread = nullptr;
	std::shared_ptr<Work> m_work;
};
} // namespace vibestudio
