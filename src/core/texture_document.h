#pragma once

#include "core/idtech_image.h"
#include "core/texture_paint.h"
#include "core/texture_transform.h"

#include <QColor>
#include <QJsonArray>
#include <QPoint>
#include <QRect>
#include <QRegion>

namespace vibestudio {

class PackageStagingModel;

enum class TextureBlendMode { Normal, Multiply, Screen, Add };

struct TextureLayer {
	int id = 0;
	QString name;
	QImage pixels;
	bool visible = true;
	bool locked = false;
	int opacity = 100;
	TextureBlendMode blend = TextureBlendMode::Normal;
};

QString textureBlendModeId(TextureBlendMode mode);
bool textureBlendModeFromId(const QString& id, TextureBlendMode* mode);

// Layers are ordered bottom to top. Every layer has the canvas dimensions.
// Raster export is a composite snapshot; it never mutates the authoring layers.
class TextureDocument final {
public:
	static constexpr int MaximumDimension = 4096;
	static constexpr qint64 MaximumPixels = 4 * 1024 * 1024;
	static constexpr int MaximumLayers = 32;
	static constexpr qint64 MaximumLayerPixels = 32 * 1024 * 1024;
	static constexpr qint64 MaximumLayerBytes = 128 * 1024 * 1024;
	bool reset(const QImage& image, QString* error = nullptr);
	bool create(QSize size, QColor color, QString* error = nullptr);
	[[nodiscard]] const QImage& image() const;
	[[nodiscard]] QSize size() const { return m_state.size; }
	[[nodiscard]] const QVector<TextureLayer>& layers() const { return m_state.layers; }
	[[nodiscard]] int activeLayerIndex() const { return m_state.active; }
	[[nodiscard]] const TextureLayer* activeLayer() const;
	bool selectLayer(int index);
	bool addLayer(const QString& name, const QImage& pixels = {}, QString* error = nullptr);
	bool duplicateLayer(QString* error = nullptr);
	bool removeLayer(QString* error = nullptr);
	bool moveLayer(int index, QString* error = nullptr);
	bool setLayerProperties(const QString& name, bool visible, bool locked, int opacity, TextureBlendMode blend, QString* error = nullptr);
	bool mergeDown(QString* error = nullptr);
	bool flatten(QString* error = nullptr);
	[[nodiscard]] QRect selection() const { return m_state.selection; }
	bool setSelection(QRect rectangle, QString* error = nullptr);
	void clearSelection() { m_state.selection = {}; }
	[[nodiscard]] QImage copySelection() const;
	bool clearSelectedPixels(QString* error = nullptr);
	bool pastePixels(const QImage& pixels, QPoint position, QString* error = nullptr);
	bool moveSelectedPixels(QPoint delta, QString* error = nullptr);
	bool offsetPixels(QPoint delta, QString* error = nullptr, const TextureProgress& progress = {});
	// Used by the bounded project reader. Invalid data leaves this document intact.
	bool restoreLayers(QSize size, const QVector<TextureLayer>& layers, int active, QString* error = nullptr);
	[[nodiscard]] bool isDirty() const { return m_revision != m_savedRevision; }
	[[nodiscard]] quint64 revision() const { return m_revision; }
	[[nodiscard]] bool strokeActive() const { return m_stroking; }
	// Storage workers retain only current pixels, never the undo/redo buffers.
	[[nodiscard]] TextureDocument storageSnapshot() const;
	[[nodiscard]] bool canUndo() const { return !m_undo.isEmpty() && !m_stroking; }
	[[nodiscard]] bool canRedo() const { return !m_redo.isEmpty() && !m_stroking; }
	[[nodiscard]] QString undoLabel() const;
	[[nodiscard]] QString redoLabel() const;
	[[nodiscard]] qint64 historyBytes() const;
	void markSaved() { m_savedRevision = m_revision; }
	void markUnsaved() { m_savedRevision = 0; }
	bool undo();
	bool redo();
	bool beginStroke(QPoint point, QColor color, int width, QString* error = nullptr);
	bool beginStroke(QPoint point, QColor color, const TextureBrush& brush, QString* error = nullptr);
	bool continueStroke(QPoint point, const TextureProgress& progress = {});
	bool endStroke();
	void cancelStroke();
	bool paintStroke(const QVector<QPoint>& points, QColor color, int width, QString* error = nullptr, const TextureProgress& progress = {});
	bool paintStroke(const QVector<QPoint>& points, QColor color, const TextureBrush& brush, QString* error = nullptr, const TextureProgress& progress = {});
	bool drawShape(TextureShape shape, QPoint from, QPoint to, QColor color, const TextureBrush& brush, bool filled, QString* error = nullptr, const TextureProgress& progress = {});
	bool floodFill(QPoint point, QColor color, QString* error = nullptr, const TextureProgress& progress = {});
	bool floodFill(QPoint point, QColor color, int tolerance, bool wrap, QString* error = nullptr, const TextureProgress& progress = {});
	bool crop(QRect rectangle, QString* error = nullptr, const TextureProgress& progress = {});
	bool resize(QSize size, bool smooth, QString* error = nullptr, const TextureProgress& progress = {});
	bool resizeCanvas(QSize size, QPoint offset, QString* error = nullptr, const TextureProgress& progress = {});
	bool resizeSelectedPixels(QSize size, bool smooth, TextureAnchor anchor, QString* error = nullptr, const TextureProgress& progress = {});
	bool rotateSelectedPixelsClockwise(TextureAnchor anchor, QString* error = nullptr, const TextureProgress& progress = {});
	bool flip(bool horizontal, QString* error = nullptr);
	bool rotateClockwise(QString* error = nullptr, const TextureProgress& progress = {});
	bool remapPalette(const IdTechPalette& palette, bool dither, QString* error = nullptr, const TextureProgress& progress = {});
	[[nodiscard]] QByteArray pngBytes(QString* error = nullptr) const;
	bool savePng(const QString& path, bool overwrite, bool dryRun, QString* error = nullptr) const;

private:
	struct State {
		QSize size;
		QVector<TextureLayer> layers;
		int active = 0;
		QRect selection;
	};
	struct History { State state; quint64 revision = 0; QString label; };
	bool commit(State next, const QString& label, QString* error = nullptr);
	bool commitPixels(QImage next, const QString& label, QString* error);
	bool editable(QString* error) const;
	void remember(History state);
	void trimHistory(bool preferRedo);
	void invalidateComposite() { m_composite = {}; m_compositeDirty = {}; }
	void invalidateStrokeComposite(QPoint from, QPoint to);
	[[nodiscard]] QRect editBounds() const;
	State m_state, m_strokeBefore;
	mutable QImage m_composite;
	mutable QRegion m_compositeDirty;
	QVector<History> m_undo, m_redo;
	quint64 m_revision = 0, m_savedRevision = 0, m_serial = 0;
	int m_layerSerial = 0;
	bool m_stroking = false;
	QPoint m_lastPoint;
	QRgb m_color = 0;
	TextureBrush m_brush;
};

bool validTextureSize(QSize size, QString* error = nullptr);
bool loadTextureFile(const QString& path, const IdTechPalette& palette, TextureDocument* document, QString* error = nullptr, IdTechImageDecodeResult* decodedOut = nullptr, const TextureProgress& progress = {});
// All operations are validated and applied on a copy; a failed recipe is atomic.
bool applyTextureOperations(TextureDocument* document, const QJsonArray& operations, const IdTechPalette& palette, QString* error = nullptr, const TextureProgress& progress = {});
bool stageTexturePng(const QByteArray& png, const QString& virtualPath, PackageStagingModel* staging, bool replaceExisting, QString* error = nullptr);

} // namespace vibestudio
