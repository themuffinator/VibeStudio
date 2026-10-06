#include "core/texture_document.h"

#include "core/package_staging.h"
#include "core/texture_output.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonObject>
#include <QPainter>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio {

TextureDocument TextureDocument::storageSnapshot() const
{
	TextureDocument snapshot;
	snapshot.m_state = m_state; snapshot.m_revision = m_revision; snapshot.m_savedRevision = m_savedRevision;
	snapshot.m_serial = m_serial; snapshot.m_layerSerial = m_layerSerial;
	return snapshot;
}
namespace {
bool fail(QString* error, const QString& message)
{
	if (error) { *error = message; }
	return false;
}
bool checkpoint(const TextureProgress& progress, qint64 completed, qint64 total, QString* error)
{
	return !progress || progress(completed, total) || fail(error, QCoreApplication::translate("VibeStudioTexture", "Texture operation cancelled."));
}
bool integer(const QJsonObject& object, const char* key, int* result)
{
	const auto value = object.value(QLatin1String(key));
	const double number = value.toDouble(1e20);
	if (!value.isDouble() || !std::isfinite(number) || number < -100000 || number > 100000 || std::floor(number) != number) { return false; }
	*result = static_cast<int>(number);
	return true;
}

bool brushFields(const QJsonObject& object, TextureBrush* brush)
{
	const bool valid = (!object.contains(QStringLiteral("width")) || integer(object, "width", &brush->width)) &&
		(!object.contains(QStringLiteral("brush")) || textureBrushShapeFromId(object.value(QStringLiteral("brush")).toString(), &brush->shape)) &&
		(!object.contains(QStringLiteral("mode")) || texturePaintModeFromId(object.value(QStringLiteral("mode")).toString(), &brush->mode)) &&
		(!object.contains(QStringLiteral("wrap")) || object.value(QStringLiteral("wrap")).isBool());
	brush->wrap = object.value(QStringLiteral("wrap")).toBool();
	return valid && validTextureBrush(*brush);
}

QPainter::CompositionMode compositionMode(TextureBlendMode mode)
{
	switch (mode) {
	case TextureBlendMode::Normal: return QPainter::CompositionMode_SourceOver;
	case TextureBlendMode::Multiply: return QPainter::CompositionMode_Multiply;
	case TextureBlendMode::Screen: return QPainter::CompositionMode_Screen;
	case TextureBlendMode::Add: return QPainter::CompositionMode_Plus;
	}
	return QPainter::CompositionMode_SourceOver;
}

bool validLayers(QSize size, const QVector<TextureLayer>& layers, int active, QString* error)
{
	if (!validTextureSize(size, error)) { return false; }
	if (layers.isEmpty() || layers.size() > TextureDocument::MaximumLayers || active < 0 || active >= layers.size() ||
		qint64(size.width()) * size.height() * layers.size() > TextureDocument::MaximumLayerPixels) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "A texture supports 1–32 layers and at most 33,554,432 layer pixels in total."));
	}
	QSet<int> ids;
	qint64 pixelBytes = 0;
	for (const auto& layer : layers) {
		const bool supportedPixels = layer.pixels.format() == QImage::Format_ARGB32 || layer.pixels.format() == QImage::Format_RGB32 || layer.pixels.format() == QImage::Format_Indexed8;
		if (layer.id < 1 || layer.id > 1000000000 || ids.contains(layer.id) || layer.name.trimmed().isEmpty() || layer.name.size() > 128 ||
			layer.name.contains(QChar::Null) || layer.pixels.isNull() || layer.pixels.size() != size || layer.opacity < 0 || layer.opacity > 100 ||
			textureBlendModeId(layer.blend).isEmpty() || !supportedPixels) {
			return fail(error, QCoreApplication::translate("VibeStudioTexture", "Invalid layer identity, name, dimensions, opacity, blend mode, or pixel format. Layers use 8-bit RGBA or indexed pixels."));
		}
		if (layer.pixels.format() == QImage::Format_Indexed8 && (layer.pixels.colorCount() < 1 || layer.pixels.colorCount() > 256)) {
			return fail(error, QCoreApplication::translate("VibeStudioTexture", "An indexed layer requires 1–256 palette entries."));
		}
		if (layer.pixels.format() == QImage::Format_Indexed8 && layer.pixels.colorCount() < 256) {
			const int colors = layer.pixels.colorCount();
			for (int y = 0; y < size.height(); ++y) {
				const uchar* row = layer.pixels.constScanLine(y);
				if (std::any_of(row, row + size.width(), [colors](uchar index) { return index >= colors; })) {
					return fail(error, QCoreApplication::translate("VibeStudioTexture", "An indexed layer contains pixels outside its palette."));
				}
			}
		}
		pixelBytes += layer.pixels.sizeInBytes();
		if (pixelBytes > TextureDocument::MaximumLayerBytes) {
			return fail(error, QCoreApplication::translate("VibeStudioTexture", "Layer pixel storage exceeds the 128 MiB document limit."));
		}
		ids.insert(layer.id);
	}
	return true;
}

QImage compositeLayers(QSize size, const QVector<TextureLayer>& layers, QRect rectangle = {})
{
	if (layers.size() == 1 && layers.first().visible && layers.first().opacity == 100 && layers.first().blend == TextureBlendMode::Normal) {
		return rectangle.isEmpty() ? layers.first().pixels : layers.first().pixels.copy(rectangle);
	}
	if (rectangle.isEmpty()) { rectangle = QRect(QPoint(), size); }
	QImage image(rectangle.size(), QImage::Format_ARGB32_Premultiplied);
	if (image.isNull()) { return {}; }
	image.fill(Qt::transparent);
	QPainter painter(&image);
	for (const auto& layer : layers) {
		if (!layer.visible || layer.opacity == 0) { continue; }
		painter.setOpacity(layer.opacity / 100.0);
		painter.setCompositionMode(compositionMode(layer.blend));
		painter.drawImage(-rectangle.topLeft(), layer.pixels);
	}
	painter.end();
	return image.convertToFormat(QImage::Format_ARGB32);
}
}

QString textureBlendModeId(TextureBlendMode mode)
{
	switch (mode) {
	case TextureBlendMode::Normal: return QStringLiteral("normal");
	case TextureBlendMode::Multiply: return QStringLiteral("multiply");
	case TextureBlendMode::Screen: return QStringLiteral("screen");
	case TextureBlendMode::Add: return QStringLiteral("add");
	}
	return {};
}

bool textureBlendModeFromId(const QString& id, TextureBlendMode* mode)
{
	for (const auto candidate : {TextureBlendMode::Normal, TextureBlendMode::Multiply, TextureBlendMode::Screen, TextureBlendMode::Add}) {
		if (textureBlendModeId(candidate) == id) { if (mode) { *mode = candidate; } return true; }
	}
	return false;
}

bool validTextureSize(QSize size, QString* error)
{
	if (size.width() < 1 || size.height() < 1 || size.width() > TextureDocument::MaximumDimension ||
	    size.height() > TextureDocument::MaximumDimension || qint64(size.width()) * size.height() > TextureDocument::MaximumPixels) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Texture dimensions must be 1–4096 with at most 4,194,304 pixels."));
	}
	return true;
}

bool TextureDocument::reset(const QImage& image, QString* error)
{
	if (error) { error->clear(); }
	if (image.isNull() || !validTextureSize(image.size(), error)) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Cannot edit this image. Use a decoded image of at most 4,194,304 pixels and 4096 pixels per side."));
	}
	TextureLayer layer;
	layer.id = 1;
	layer.name = QCoreApplication::translate("VibeStudioTexture", "Background");
	// Preserve indexed palette entries and alpha for lossless project storage.
	layer.pixels = image.format() == QImage::Format_Indexed8 ? image : image.convertToFormat(QImage::Format_ARGB32);
	return restoreLayers(image.size(), {layer}, 0, error);
}

bool TextureDocument::restoreLayers(QSize size, const QVector<TextureLayer>& layers, int active, QString* error)
{
	if (error) { error->clear(); }
	if (!validLayers(size, layers, active, error)) { return false; }
	m_state = {size, layers, active, {}};
	m_layerSerial = 0;
	for (const auto& layer : layers) { m_layerSerial = std::max(m_layerSerial, layer.id); }
	m_undo.clear(); m_redo.clear(); m_strokeBefore = {}; m_stroking = false;
	m_revision = ++m_serial; m_savedRevision = m_revision; invalidateComposite();
	return true;
}

const QImage& TextureDocument::image() const
{
	if (m_composite.isNull() && !m_state.layers.isEmpty()) { m_composite = compositeLayers(m_state.size, m_state.layers); }
	else if (!m_compositeDirty.isEmpty()) {
		QPainter painter(&m_composite);
		painter.setCompositionMode(QPainter::CompositionMode_Source);
		for (const QRect& rectangle : m_compositeDirty) {
			const QImage part = compositeLayers(m_state.size, m_state.layers, rectangle);
			if (part.isNull()) { painter.end(); m_composite = {}; m_compositeDirty = {}; return m_composite; }
			painter.drawImage(rectangle.topLeft(), part);
		}
	}
	m_compositeDirty = {};
	return m_composite;
}

void TextureDocument::invalidateStrokeComposite(QPoint from, QPoint to)
{
	if (m_composite.isNull()) { return; }
	if (m_state.layers.size() == 1 || m_composite.format() != QImage::Format_ARGB32) { invalidateComposite(); return; }
	const int bias = (m_brush.width - 1) / 2;
	const QRect touched(std::min(from.x(), to.x()) - bias, std::min(from.y(), to.y()) - bias,
		std::abs(from.x() - to.x()) + m_brush.width, std::abs(from.y() - to.y()) + m_brush.width);
	if (!m_brush.wrap) { m_compositeDirty += touched.intersected(editBounds()); }
	else {
		// Split each axis at the repeat seam. Even a brush wider than a small
		// canvas produces at most four dirty rectangles, without scanning tiles.
		const auto ranges = [](int start, int length, int side) {
			QVector<QPair<int, int>> result;
			if (length >= side) { result.append({0, side}); return result; }
			start = (start % side + side) % side;
			result.append({start, std::min(length, side - start)});
			if (start + length > side) { result.append({0, start + length - side}); }
			return result;
		};
		for (const auto& x : ranges(touched.left(), touched.width(), m_state.size.width())) {
			for (const auto& y : ranges(touched.top(), touched.height(), m_state.size.height())) {
				m_compositeDirty += QRect(x.first, y.first, x.second, y.second).intersected(editBounds());
			}
		}
	}
	if (m_compositeDirty.rectCount() > 64) { m_compositeDirty = m_compositeDirty.boundingRect(); }
}

const TextureLayer* TextureDocument::activeLayer() const
{
	return m_state.active >= 0 && m_state.active < m_state.layers.size() ? &m_state.layers[m_state.active] : nullptr;
}

bool TextureDocument::selectLayer(int index)
{
	if (m_stroking || index < 0 || index >= m_state.layers.size()) { return false; }
	m_state.active = index;
	return true;
}

bool TextureDocument::create(QSize size, QColor color, QString* error)
{
	if (!validTextureSize(size, error) || !color.isValid()) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Choose valid dimensions and a valid color."));
	}
	QImage next(size, QImage::Format_ARGB32);
	if (next.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Unable to allocate the texture.")); }
	next.fill(color);
	if (!reset(next, error)) { return false; }
	m_savedRevision = 0;
	return true;
}

qint64 TextureDocument::historyBytes() const
{
	QSet<const uchar*> seen;
	for (const auto& layer : m_state.layers) { seen.insert(layer.pixels.constBits()); }
	qint64 bytes = 0;
	for (const auto* history : {&m_undo, &m_redo}) {
		for (const auto& entry : *history) {
			for (const auto& layer : entry.state.layers) {
				if (!seen.contains(layer.pixels.constBits())) { seen.insert(layer.pixels.constBits()); bytes += layer.pixels.sizeInBytes(); }
			}
		}
	}
	return bytes;
}

QString TextureDocument::undoLabel() const { return canUndo() ? m_undo.last().label : QString(); }
QString TextureDocument::redoLabel() const { return canRedo() ? m_redo.last().label : QString(); }

void TextureDocument::remember(History entry)
{
	m_undo.push_back(std::move(entry));
	trimHistory(false);
}

void TextureDocument::trimHistory(bool preferRedo)
{
	// The budget excludes current pixels, so returning to a smaller canvas can make
	// previously excluded buffers count again. Recheck after every transition.
	// Keep the immediate inverse action and evict distant states first.
	auto& preferred = preferRedo ? m_redo : m_undo;
	auto& other = preferRedo ? m_undo : m_redo;
	while (m_undo.size() + m_redo.size() > 64 || historyBytes() > 128 * 1024 * 1024) {
		if (!other.isEmpty()) { other.removeFirst(); }
		else if (preferred.size() > 1) { preferred.removeFirst(); }
		else { break; } // One valid document's pixels cannot exceed 128 MiB.
	}
}

bool TextureDocument::commit(State next, const QString& label, QString* error)
{
	if (m_stroking) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "The texture operation could not be applied."));
	}
	if (!validLayers(next.size, next.layers, next.active, error)) { return false; }
	bool same = next.size == m_state.size && next.layers.size() == m_state.layers.size();
	for (int i = 0; same && i < next.layers.size(); ++i) {
		auto& a = next.layers[i]; const auto& b = m_state.layers[i];
		// A clipped or transparent paint operation must not convert an untouched
		// indexed layer or dirty its palette merely because the working copy is RGBA.
		if (a.id == b.id && a.pixels.format() == QImage::Format_ARGB32 && b.pixels.format() != QImage::Format_ARGB32 &&
			a.pixels == b.pixels.convertToFormat(QImage::Format_ARGB32)) { a.pixels = b.pixels; }
		same = a.id == b.id && a.name == b.name && a.visible == b.visible && a.locked == b.locked && a.opacity == b.opacity && a.blend == b.blend &&
			a.pixels.format() == b.pixels.format() && a.pixels == b.pixels;
	}
	if (same) {
		// A transform can change only the selection (for example, rotating an
		// empty rectangle). Undo its geometry without dirtying saved pixels.
		if (m_state.selection != next.selection) { m_redo.clear(); remember({m_state, m_revision, label}); }
		m_state.active = next.active; m_state.selection = next.selection; return true;
	}
	History previous{m_state, m_revision, label};
	m_state = std::move(next); m_redo.clear(); remember(std::move(previous));
	m_revision = ++m_serial; invalidateComposite();
	return true;
}

bool TextureDocument::editable(QString* error) const
{
	const auto* layer = activeLayer();
	if (!layer || layer->locked || !layer->visible) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Select a visible, unlocked layer to edit pixels."));
	}
	return true;
}

QRect TextureDocument::editBounds() const
{
	return m_state.selection.isEmpty() ? QRect(QPoint(), m_state.size) : m_state.selection;
}

bool TextureDocument::commitPixels(QImage pixels, const QString& label, QString* error)
{
	if (!editable(error) || pixels.isNull() || pixels.size() != m_state.size) { return false; }
	auto next = m_state; next.layers[next.active].pixels = std::move(pixels);
	return commit(std::move(next), label, error);
}

bool TextureDocument::addLayer(const QString& name, const QImage& pixels, QString* error)
{
	if (m_stroking || m_state.layers.isEmpty() || m_state.layers.size() >= MaximumLayers || m_layerSerial >= 1000000000 ||
		qint64(m_state.size.width()) * m_state.size.height() * (m_state.layers.size() + 1) > MaximumLayerPixels) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "The document cannot hold another layer within its resource limits."));
	}
	TextureLayer layer;
	layer.id = m_layerSerial + 1; layer.name = name.trimmed();
	if (pixels.isNull()) { layer.pixels = QImage(m_state.size, QImage::Format_ARGB32); layer.pixels.fill(Qt::transparent); }
	else {
		if (pixels.size() != m_state.size) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Imported layers must match the canvas dimensions.")); }
		layer.pixels = pixels.format() == QImage::Format_Indexed8 ? pixels : pixels.convertToFormat(QImage::Format_ARGB32);
	}
	auto next = m_state;
	next.active++; next.layers.insert(next.active, layer);
	if (!commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Add layer"), error)) { return false; }
	m_layerSerial = layer.id;
	return true;
}

bool TextureDocument::duplicateLayer(QString* error)
{
	const auto* layer = activeLayer();
	if (!layer) { return false; }
	auto next = m_state;
	TextureLayer copy = *layer; copy.id = m_layerSerial + 1;
	copy.name = QCoreApplication::translate("VibeStudioTexture", "%1 copy").arg(layer->name).left(128);
	next.layers.insert(++next.active, copy);
	if (!commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Duplicate layer"), error)) { return false; }
	m_layerSerial = copy.id;
	return true;
}

bool TextureDocument::removeLayer(QString* error)
{
	if (!activeLayer() || activeLayer()->locked || m_state.layers.size() <= 1) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Keep at least one layer, and unlock a layer before removing it."));
	}
	auto next = m_state; next.layers.removeAt(next.active); next.active = std::min(next.active, int(next.layers.size()) - 1);
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Remove layer"), error);
}

bool TextureDocument::moveLayer(int index, QString* error)
{
	if (index < 0 || index >= m_state.layers.size()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Layer destination is out of range.")); }
	auto next = m_state; next.layers.move(next.active, index); next.active = index;
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Reorder layer"), error);
}

bool TextureDocument::setLayerProperties(const QString& name, bool visible, bool locked, int opacity, TextureBlendMode blend, QString* error)
{
	if (!activeLayer()) { return false; }
	auto next = m_state; auto& layer = next.layers[next.active];
	layer.name = name.trimmed(); layer.visible = visible; layer.locked = locked; layer.opacity = opacity; layer.blend = blend;
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Layer properties"), error);
}

bool TextureDocument::mergeDown(QString* error)
{
	if (!editable(error) || m_state.active == 0) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Choose a layer above an unlocked visible layer to merge down.")); }
	const auto& lower = m_state.layers[m_state.active - 1];
	if (lower.locked || !lower.visible || lower.blend != TextureBlendMode::Normal || activeLayer()->blend != TextureBlendMode::Normal) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Merge Down requires two visible, unlocked layers with Normal blending. Flatten preserves the complete blended appearance."));
	}
	auto next = m_state;
	const QImage merged = compositeLayers(next.size, {lower, *activeLayer()});
	next.layers.removeAt(next.active--);
	next.layers[next.active].pixels = merged; next.layers[next.active].opacity = 100;
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Merge down"), error);
}

bool TextureDocument::flatten(QString* error)
{
	if (m_state.layers.isEmpty()) { return false; }
	for (const auto& layer : m_state.layers) {
		if (layer.locked) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Unlock all layers before flattening the document.")); }
	}
	auto next = m_state;
	TextureLayer layer; layer.id = m_layerSerial + 1; layer.name = QCoreApplication::translate("VibeStudioTexture", "Flattened"); layer.pixels = image();
	next.layers = {layer}; next.active = 0;
	if (!commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Flatten"), error)) { return false; }
	m_layerSerial = layer.id; return true;
}

bool TextureDocument::setSelection(QRect rectangle, QString* error)
{
	if (m_stroking || (!rectangle.isEmpty() && (rectangle.x() < 0 || rectangle.y() < 0 ||
		qint64(rectangle.x()) + rectangle.width() > m_state.size.width() || qint64(rectangle.y()) + rectangle.height() > m_state.size.height()))) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "The selection must lie inside the canvas."));
	}
	m_state.selection = rectangle.isEmpty() ? QRect() : rectangle;
	return true;
}

QImage TextureDocument::copySelection() const
{
	return activeLayer() ? activeLayer()->pixels.copy(editBounds()) : QImage();
}

bool TextureDocument::clearSelectedPixels(QString* error)
{
	if (!editable(error)) { return false; }
	QImage next = replaceTexturePixels(activeLayer()->pixels, editBounds(), {}, {}, error);
	return commitPixels(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Clear pixels"), error);
}

bool TextureDocument::pastePixels(const QImage& pixels, QPoint position, QString* error)
{
	if (pixels.format() == QImage::Format_Indexed8) {
		TextureDocument validated;
		if (!validated.reset(pixels, error)) { return false; }
	}
	if (!editable(error) || !validTextureSize(pixels.size(), error) || qint64(position.x()) + pixels.width() <= editBounds().left() ||
		qint64(position.y()) + pixels.height() <= editBounds().top() || position.x() > editBounds().right() || position.y() > editBounds().bottom()) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "The pasted image must intersect the editable canvas area."));
	}
	QImage next = activeLayer()->pixels.convertToFormat(QImage::Format_ARGB32);
	QPainter painter(&next); painter.setClipRect(editBounds()); painter.drawImage(position, pixels); painter.end();
	return commitPixels(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Paste pixels"), error);
}

bool TextureDocument::moveSelectedPixels(QPoint delta, QString* error)
{
	if (!editable(error)) { return false; }
	const QRect source = editBounds();
	// Refuse destructive clipping; canvas resize is an explicit separate action.
	const qint64 left = qint64(source.x()) + delta.x(), top = qint64(source.y()) + delta.y();
	if (left < 0 || top < 0 || left + source.width() > m_state.size.width() || top + source.height() > m_state.size.height()) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Moving the selection would place pixels outside the canvas."));
	}
	auto next = m_state;
	QImage& pixels = next.layers[next.active].pixels; const QImage cut = pixels.copy(source);
	if (cut.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Unable to allocate transformed pixels.")); }
	pixels = replaceTexturePixels(pixels, source, cut, source.topLeft() + delta, error);
	if (pixels.isNull()) { return false; }
	next.selection = source.translated(delta);
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Move pixels"), error);
}

bool TextureDocument::offsetPixels(QPoint delta, QString* error, const TextureProgress& progress)
{
	if (!editable(error)) { return false; }
	const QRect area = editBounds();
	const int dx = (delta.x() % area.width() + area.width()) % area.width();
	const int dy = (delta.y() % area.height() + area.height()) % area.height();
	if (!checkpoint(progress, 0, area.height(), error)) { return false; }
	if (dx == 0 && dy == 0) { return true; }
	const QImage source = activeLayer()->pixels.copy(area);
	if (source.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Unable to allocate offset pixels.")); }
	QImage next = activeLayer()->pixels;
	const int pixelBytes = source.format() == QImage::Format_Indexed8 ? 1 : 4;
	for (int y = 0; y < area.height(); ++y) {
		if (y % 32 == 0 && !checkpoint(progress, y, area.height(), error)) { return false; }
		const uchar* from = source.constScanLine(y);
		uchar* to = next.scanLine(area.top() + (y + dy) % area.height()) + area.left() * pixelBytes;
		std::copy_n(from, (area.width() - dx) * pixelBytes, to + dx * pixelBytes);
		std::copy_n(from + (area.width() - dx) * pixelBytes, dx * pixelBytes, to);
	}
	if (!checkpoint(progress, area.height(), area.height(), error)) { return false; }
	return commitPixels(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Offset pixels"), error);
}

bool TextureDocument::undo()
{
	if (!canUndo()) { return false; }
	const History previous = m_undo.takeLast();
	m_redo.push_back({m_state, m_revision, previous.label}); m_state = previous.state; m_revision = previous.revision; invalidateComposite();
	trimHistory(true);
	return true;
}

bool TextureDocument::redo()
{
	if (!canRedo()) { return false; }
	const History next = m_redo.takeLast();
	History previous{m_state, m_revision, next.label}; m_state = next.state; m_revision = next.revision;
	remember(std::move(previous)); invalidateComposite();
	return true;
}

bool TextureDocument::beginStroke(QPoint point, QColor color, int width, QString* error)
{
	return beginStroke(point, color, TextureBrush{width}, error);
}

bool TextureDocument::beginStroke(QPoint point, QColor color, const TextureBrush& brush, QString* error)
{
	if (!editable(error)) { return false; }
	if (m_stroking || !validTexturePaintPoint(point, m_state.size, brush.wrap) || !color.isValid() || !validTextureBrush(brush)) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "A stroke needs a valid pixel, color, and brush of width 1–128. Wrapped points must lie inside the 3 × 3 repeat."));
	}
	QImage pixels = activeLayer()->pixels.convertToFormat(QImage::Format_ARGB32);
	if (pixels.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Unable to allocate stroke pixels.")); }
	m_strokeBefore = m_state; m_stroking = true; m_lastPoint = point;
	m_color = color.rgba(); m_brush = brush;
	m_state.layers[m_state.active].pixels = std::move(pixels);
	return continueStroke(point);
}

bool TextureDocument::continueStroke(QPoint point, const TextureProgress& progress)
{
	if (!m_stroking || !validTexturePaintPoint(point, m_state.size, m_brush.wrap)) { return false; }
	QImage& pixels = m_state.layers[m_state.active].pixels;
	if (!paintTextureSegment(&pixels, m_strokeBefore.layers[m_state.active].pixels, m_lastPoint, point, m_color, m_brush, editBounds(), progress)) {
		cancelStroke(); return false;
	}
	invalidateStrokeComposite(m_lastPoint, point);
	m_lastPoint = point;
	return true;
}

bool TextureDocument::endStroke()
{
	if (!m_stroking) { return false; }
	State next = m_state; m_state = m_strokeBefore; m_strokeBefore = {}; m_stroking = false;
	const QImage composite = m_composite; const QRegion dirty = m_compositeDirty;
	if (!commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Paint stroke"))) { return false; }
	// The stroke preview already describes the committed pixels. Retain it
	// across release so finishing a gesture does not render the whole canvas.
	m_composite = composite; m_compositeDirty = dirty;
	return true;
}

void TextureDocument::cancelStroke()
{
	if (m_stroking) { m_state = m_strokeBefore; m_strokeBefore = {}; m_stroking = false; invalidateComposite(); }
}

bool TextureDocument::paintStroke(const QVector<QPoint>& points, QColor color, int width, QString* error, const TextureProgress& progress)
{
	return paintStroke(points, color, TextureBrush{width}, error, progress);
}

bool TextureDocument::paintStroke(const QVector<QPoint>& points, QColor color, const TextureBrush& brush, QString* error, const TextureProgress& progress)
{
	if (points.isEmpty() || points.size() > 65536) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "A stroke needs 1–65,536 points.")); }
	for (const auto& point : points) {
		if (!validTexturePaintPoint(point, m_state.size, brush.wrap)) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Stroke points must lie inside the texture, or its 3 × 3 repeat when wrapping.")); }
	}
	if (!validTextureBrush(brush)) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Choose a valid brush shape, paint mode, and width of 1–128.")); }
	qint64 stamps = 1; QPoint previous = points.first();
	for (const auto& point : points) { stamps += std::max(std::abs(point.x() - previous.x()), std::abs(point.y() - previous.y())) + 1; previous = point; }
	if (stamps * brush.width * brush.width > 256ll * 1024 * 1024) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "The stroke exceeds the 268,435,456 pixel-write work limit. Split it into shorter strokes.")); }
	if (!checkpoint(progress, 0, stamps, error)) { return false; }
	if (!beginStroke(points.first(), color, brush, error)) { return false; }
	qint64 completed = 1;
	for (const auto& point : points) {
		const int steps = std::max(std::abs(point.x() - m_lastPoint.x()), std::abs(point.y() - m_lastPoint.y())) + 1;
		if (!continueStroke(point, [&](qint64 done, qint64 total) { return checkpoint(progress, completed + done * steps / total, stamps, error); })) { cancelStroke(); return false; }
		completed += steps;
	}
	if (!checkpoint(progress, stamps, stamps, error)) { cancelStroke(); return false; }
	return endStroke();
}

bool TextureDocument::drawShape(TextureShape shape, QPoint from, QPoint to, QColor color, const TextureBrush& brush, bool filled, QString* error, const TextureProgress& progress)
{
	if (!editable(error)) { return false; }
	if (!color.isValid() || !validTextureBrush(brush) || !validTexturePaintPoint(from, m_state.size, brush.wrap) ||
		!validTexturePaintPoint(to, m_state.size, brush.wrap) || (shape != TextureShape::Line && shape != TextureShape::Rectangle && shape != TextureShape::Ellipse)) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Choose a valid shape, brush, color, and endpoints inside the canvas or wrapped repeat."));
	}
	QImage next = activeLayer()->pixels.convertToFormat(QImage::Format_ARGB32);
	const auto report = [&](qint64 done, qint64 total) { return checkpoint(progress, done, total, error); };
	if (!paintTextureShape(&next, activeLayer()->pixels, shape, from, to, color.rgba(), brush, filled, editBounds(), report)) { return false; }
	const QString label = shape == TextureShape::Line ? QCoreApplication::translate("VibeStudioTexture", "Draw line") :
		(shape == TextureShape::Rectangle ? QCoreApplication::translate("VibeStudioTexture", "Draw rectangle") : QCoreApplication::translate("VibeStudioTexture", "Draw ellipse"));
	return commitPixels(std::move(next), label, error);
}

bool TextureDocument::floodFill(QPoint point, QColor color, QString* error, const TextureProgress& progress)
{
	return floodFill(point, color, 0, false, error, progress);
}

bool TextureDocument::floodFill(QPoint point, QColor color, int tolerance, bool wrap, QString* error, const TextureProgress& progress)
{
	if (!editable(error)) { return false; }
	if (wrap && validTexturePaintPoint(point, m_state.size, true)) {
		point = {(point.x() % m_state.size.width() + m_state.size.width()) % m_state.size.width(), (point.y() % m_state.size.height() + m_state.size.height()) % m_state.size.height()};
	}
	if (!editBounds().contains(point) || !color.isValid() || tolerance < 0 || tolerance > 255) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Fill needs a selected image pixel, valid color, and tolerance from 0 to 255.")); }
	const QRgb target = activeLayer()->pixels.pixel(point), replacement = color.rgba();
	const QRect bounds = editBounds();
	const qint64 total = qint64(bounds.width()) * bounds.height();
	if (!checkpoint(progress, 0, total, error)) { return false; }
	if (target == replacement && tolerance == 0) { return true; }
	QImage next = activeLayer()->pixels.convertToFormat(QImage::Format_ARGB32);
	if (next.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Unable to allocate fill pixels.")); }
	const int width = next.width(), height = next.height(), stride = next.bytesPerLine() / int(sizeof(QRgb));
	auto* pixels = reinterpret_cast<QRgb*>(next.bits());
	const auto matches = [&](QRgb value) {
		return std::abs(qRed(value) - qRed(target)) <= tolerance && std::abs(qGreen(value) - qGreen(target)) <= tolerance &&
			std::abs(qBlue(value) - qBlue(target)) <= tolerance && std::abs(qAlpha(value) - qAlpha(target)) <= tolerance;
	};
	QByteArray visited((width * height + 7) / 8, '\0');
	auto* bits = reinterpret_cast<uchar*>(visited.data());
	QVector<int> queue; queue.reserve(int(total)); queue << point.y() * width + point.x();
	bits[queue.first() / 8] |= uchar(1u << (queue.first() % 8));
	pixels[point.y() * stride + point.x()] = replacement;
	const int left = bounds.left(), right = bounds.right(), top = bounds.top(), bottom = bounds.bottom();
	const auto visit = [&](int x, int y) {
		if (wrap) {
			if (x < 0) { x += width; } else if (x == width) { x = 0; }
			if (y < 0) { y += height; } else if (y == height) { y = 0; }
		}
		if (x < left || x > right || y < top || y > bottom) { return; }
		const int offset = y * width + x; const uchar mask = uchar(1u << (offset % 8));
		if (bits[offset / 8] & mask) { return; }
		bits[offset / 8] |= mask;
		// Only unvisited pixels are compared. Their channels still contain the
		// original color, even when the replacement falls inside the tolerance.
		QRgb& pixel = pixels[y * stride + x];
		if (matches(pixel)) { pixel = replacement; queue << offset; }
	};
	for (qsizetype index = 0; index < queue.size(); ++index) {
		if (index % 4096 == 0 && !checkpoint(progress, index, total, error)) { return false; }
		const int x = queue[index] % width, y = queue[index] / width;
		visit(x - 1, y); visit(x + 1, y); visit(x, y - 1); visit(x, y + 1);
	}
	if (!checkpoint(progress, total, total, error)) { return false; }
	return commitPixels(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Fill"), error);
}

bool TextureDocument::crop(QRect rectangle, QString* error, const TextureProgress& progress)
{
	if (rectangle.isEmpty() || !QRect(QPoint(), m_state.size).contains(rectangle)) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "The crop rectangle must lie completely inside the texture.")); }
	auto next = m_state; next.size = rectangle.size(); next.selection = {};
	for (int i = 0; i < next.layers.size(); ++i) {
		if (!checkpoint(progress, i, next.layers.size(), error)) { return false; }
		next.layers[i].pixels = next.layers[i].pixels.copy(rectangle);
	}
	if (!checkpoint(progress, next.layers.size(), next.layers.size(), error)) { return false; }
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Crop canvas"), error);
}

bool TextureDocument::resize(QSize size, bool smooth, QString* error, const TextureProgress& progress)
{
	if (!validTextureSize(size, error)) { return false; }
	if (qint64(size.width()) * size.height() * m_state.layers.size() > MaximumLayerPixels) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Resizing all layers would exceed the document pixel limit.")); }
	auto next = m_state; next.size = size; next.selection = {};
	for (int i = 0; i < next.layers.size(); ++i) {
		const auto report = [&](qint64 done, qint64 total) { return !progress || progress(i * 1000 + done * 1000 / std::max(qint64(1), total), next.layers.size() * 1000); };
		next.layers[i].pixels = resizeTexturePixels(next.layers[i].pixels, size, smooth, error, report);
		if (next.layers[i].pixels.isNull()) { return false; }
	}
	if (!checkpoint(progress, next.layers.size(), next.layers.size(), error)) { return false; }
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Resample canvas"), error);
}

bool TextureDocument::resizeCanvas(QSize size, QPoint offset, QString* error, const TextureProgress& progress)
{
	if (!validTextureSize(size, error)) { return false; }
	if (qint64(size.width()) * size.height() * m_state.layers.size() > MaximumLayerPixels) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Resizing all layers would exceed the document pixel limit.")); }
	if (qint64(offset.x()) + m_state.size.width() <= 0 || qint64(offset.y()) + m_state.size.height() <= 0 || offset.x() >= size.width() || offset.y() >= size.height()) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "The existing canvas must intersect the new canvas."));
	}
	auto next = m_state; next.size = size; next.selection = {};
	for (int i = 0; i < next.layers.size(); ++i) {
		const auto report = [&](qint64 done, qint64 total) { return !progress || progress(i * 1000 + done * 1000 / std::max(qint64(1), total), next.layers.size() * 1000); };
		next.layers[i].pixels = resizeTextureCanvas(next.layers[i].pixels, size, offset, error, report);
		if (next.layers[i].pixels.isNull()) { return false; }
	}
	if (!checkpoint(progress, 1, 1, error)) { return false; }
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Set canvas size"), error);
}

bool TextureDocument::resizeSelectedPixels(QSize size, bool smooth, TextureAnchor anchor, QString* error, const TextureProgress& progress)
{
	if (!editable(error) || !validTextureSize(size, error)) { return false; }
	const QRect source = m_state.selection;
	if (source.isEmpty() || textureAnchorId(anchor).isEmpty()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Choose a rectangular selection and a valid anchor.")); }
	const QRect destination(source.topLeft() + textureAnchorOffset(size, source.size(), anchor), size);
	if (!QRect(QPoint(), m_state.size).contains(destination)) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "The transformed selection must fit completely inside the canvas.")); }
	const auto resampleProgress = [&](qint64 done, qint64 total) { return !progress || progress(done * 1000 / std::max(qint64(1), total), 2000); };
	const auto replaceProgress = [&](qint64 done, qint64 total) { return !progress || progress(1000 + done * 1000 / std::max(qint64(1), total), 2000); };
	const QImage pixels = resizeTexturePixels(activeLayer()->pixels.copy(source), size, smooth, error, resampleProgress);
	if (pixels.isNull()) { return false; }
	auto next = m_state;
	next.layers[next.active].pixels = replaceTexturePixels(activeLayer()->pixels, source, pixels, destination.topLeft(), error, replaceProgress);
	if (next.layers[next.active].pixels.isNull()) { return false; }
	next.selection = destination;
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Resize selected pixels"), error);
}

bool TextureDocument::rotateSelectedPixelsClockwise(TextureAnchor anchor, QString* error, const TextureProgress& progress)
{
	if (!editable(error)) { return false; }
	const QRect source = m_state.selection;
	if (source.isEmpty() || textureAnchorId(anchor).isEmpty()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Choose a rectangular selection and a valid anchor.")); }
	const QSize size = source.size().transposed();
	const QRect destination(source.topLeft() + textureAnchorOffset(size, source.size(), anchor), size);
	if (!QRect(QPoint(), m_state.size).contains(destination)) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "The transformed selection must fit completely inside the canvas.")); }
	const auto rotateProgress = [&](qint64 done, qint64 total) { return !progress || progress(done * 1000 / std::max(qint64(1), total), 2000); };
	const auto replaceProgress = [&](qint64 done, qint64 total) { return !progress || progress(1000 + done * 1000 / std::max(qint64(1), total), 2000); };
	const QImage pixels = rotateTexturePixelsClockwise(activeLayer()->pixels.copy(source), error, rotateProgress);
	if (pixels.isNull()) { return false; }
	auto next = m_state;
	next.layers[next.active].pixels = replaceTexturePixels(activeLayer()->pixels, source, pixels, destination.topLeft(), error, replaceProgress);
	if (next.layers[next.active].pixels.isNull()) { return false; }
	next.selection = destination;
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Rotate selected pixels"), error);
}

bool TextureDocument::flip(bool horizontal, QString* error)
{
	if (!editable(error)) { return false; }
	QImage part = activeLayer()->pixels.copy(editBounds());
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
	part = part.flipped(horizontal ? Qt::Horizontal : Qt::Vertical);
#else
	part = part.mirrored(horizontal, !horizontal);
#endif
	if (part.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Unable to allocate transformed pixels.")); }
	if (m_state.selection.isEmpty()) { return commitPixels(part, QCoreApplication::translate("VibeStudioTexture", "Flip pixels"), error); }
	QImage next = replaceTexturePixels(activeLayer()->pixels, editBounds(), part, editBounds().topLeft(), error);
	return commitPixels(next, QCoreApplication::translate("VibeStudioTexture", "Flip pixels"), error);
}

bool TextureDocument::rotateClockwise(QString* error, const TextureProgress& progress)
{
	auto next = m_state; next.size.transpose(); next.selection = {};
	for (int i = 0; i < next.layers.size(); ++i) {
		const auto report = [&](qint64 done, qint64 total) { return !progress || progress(i * 1000 + done * 1000 / std::max(qint64(1), total), next.layers.size() * 1000); };
		next.layers[i].pixels = rotateTexturePixelsClockwise(next.layers[i].pixels, error, report);
		if (next.layers[i].pixels.isNull()) { return false; }
	}
	if (!checkpoint(progress, next.layers.size(), next.layers.size(), error)) { return false; }
	return commit(std::move(next), QCoreApplication::translate("VibeStudioTexture", "Rotate canvas"), error);
}

bool TextureDocument::remapPalette(const IdTechPalette& palette, bool dither, QString* error, const TextureProgress& progress)
{
	if (!palette.isValid()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "Resolve a valid game palette before remapping colors.")); }
	if (!editable(error)) { return false; }
	const auto report = [&](int done, int total) { return checkpoint(progress, done, total, error); };
	if (m_state.selection.isEmpty()) { return commitPixels(quantizeToIdTechPalette(activeLayer()->pixels, palette, dither, report), QCoreApplication::translate("VibeStudioTexture", "Remap palette"), error); }
	QImage next = activeLayer()->pixels.convertToFormat(QImage::Format_ARGB32);
	const auto converted = quantizeToIdTechPalette(next.copy(editBounds()), palette, dither, report);
	if (converted.isNull()) { return false; }
	QPainter painter(&next); painter.setCompositionMode(QPainter::CompositionMode_Source); painter.drawImage(editBounds().topLeft(), converted); painter.end();
	return commitPixels(next, QCoreApplication::translate("VibeStudioTexture", "Remap palette"), error);
}

QByteArray TextureDocument::pngBytes(QString* error) const
{
	if (image().isNull()) { fail(error, QCoreApplication::translate("VibeStudioTexture", "There is no texture to save.")); return {}; }
	return encodeTextureOutputPng(image(), error);
}

bool TextureDocument::savePng(const QString& path, bool overwrite, bool dryRun, QString* error) const
{
	if (error) { error->clear(); }
	const QFileInfo info(path);
	if (info.suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Choose a PNG export filename."));
	}
	TextureOutputTarget target;
	if (!inspectTextureOutputTarget(path, overwrite, &target, error)) { return false; }
	const auto bytes = pngBytes(error);
	if (bytes.isEmpty()) { return false; }
	return writeTextureOutput(target, bytes, dryRun, error);
}

bool loadTextureFile(const QString& path, const IdTechPalette& palette, TextureDocument* document, QString* error, IdTechImageDecodeResult* decodedOut, const TextureProgress& progress)
{
	if (!checkpoint(progress, 0, 0, error)) { return false; }
	QFile file(path);
	if (!document || !QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Unable to open texture, or the input exceeds 64 MiB."));
	}
	const auto bytes = file.read(64 * 1024 * 1024 + 1);
	if (file.error() != QFile::NoError || bytes.size() > 64 * 1024 * 1024) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "The texture could not be read within the 64 MiB input limit.")); }
	QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
	QImageReader reader(&buffer);
	if (reader.canRead() && !validTextureSize(reader.size(), error)) { return false; }
	IdTechImageDecodeContext context;
	context.maximumDimension = TextureDocument::MaximumDimension;
	context.maximumImagePixels = TextureDocument::MaximumPixels;
	context.isCancelled = [&progress] { return progress && !progress(0, 0); };
	const auto decoded = decodeIdTechImage(path, bytes, palette, context);
	if (!decoded.decoded) { return fail(error, decoded.error); }
	if (!checkpoint(progress, 1, 1, error)) { return false; }
	if (!document->reset(decoded.image, error)) { return false; }
	if (decodedOut) { *decodedOut = decoded; }
	return true;
}

bool applyTextureOperations(TextureDocument* document, const QJsonArray& operations, const IdTechPalette& palette, QString* error, const TextureProgress& progress)
{
	if (!document || operations.size() > 256) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "A texture recipe supports at most 256 operations.")); }
	auto next = *document;
	for (int index = 0; index < operations.size(); ++index) {
		const auto report = [&](qint64 done, qint64 total) { return checkpoint(progress, index * 1000 + (total > 0 ? done * 1000 / total : 0), operations.size() * 1000, error); };
		if (!report(0, 1)) { return false; }
		const auto op = operations[index].toObject();
		const QString type = op.value(QStringLiteral("op")).toString();
		QString problem; bool applied = false;
		int x = 0, y = 0, w = 0, h = 0;
		if (type == QStringLiteral("stroke")) {
			QVector<QPoint> points;
			TextureBrush brush;
			bool valid = brushFields(op, &brush) && op.value(QStringLiteral("points")).isArray() && op.value(QStringLiteral("points")).toArray().size() <= 65536;
			for (const auto& value : op.value(QStringLiteral("points")).toArray()) {
				if (!valid) { break; }
				const auto pair = value.toArray();
				QJsonObject coordinates{{QStringLiteral("x"), pair.size() > 0 ? pair.at(0) : QJsonValue{}}, {QStringLiteral("y"), pair.size() > 1 ? pair.at(1) : QJsonValue{}}};
				if (pair.size() != 2 || !integer(coordinates, "x", &x) || !integer(coordinates, "y", &y)) { valid = false; break; }
				points << QPoint(x, y);
			}
			applied = valid && next.paintStroke(points, QColor(op.value(QStringLiteral("color")).toString()), brush, &problem, report);
		} else if (type == QStringLiteral("line") || type == QStringLiteral("rectangle") || type == QStringLiteral("ellipse")) {
			TextureBrush brush;
			const auto shape = type == QStringLiteral("line") ? TextureShape::Line : (type == QStringLiteral("rectangle") ? TextureShape::Rectangle : TextureShape::Ellipse);
			applied = brushFields(op, &brush) && integer(op, "x", &x) && integer(op, "y", &y) && integer(op, "x2", &w) && integer(op, "y2", &h) &&
				(!op.contains(QStringLiteral("filled")) || op.value(QStringLiteral("filled")).isBool()) &&
				next.drawShape(shape, {x, y}, {w, h}, QColor(op.value(QStringLiteral("color")).toString()), brush, op.value(QStringLiteral("filled")).toBool(), &problem, report);
		} else if (type == QStringLiteral("fill")) {
			applied = integer(op, "x", &x) && integer(op, "y", &y) && (!op.contains(QStringLiteral("tolerance")) || integer(op, "tolerance", &w)) &&
				(!op.contains(QStringLiteral("wrap")) || op.value(QStringLiteral("wrap")).isBool()) &&
				next.floodFill({x, y}, QColor(op.value(QStringLiteral("color")).toString()), w, op.value(QStringLiteral("wrap")).toBool(), &problem, report);
		} else if (type == QStringLiteral("crop")) {
			applied = integer(op, "x", &x) && integer(op, "y", &y) && integer(op, "width", &w) && integer(op, "height", &h) && next.crop({x, y, w, h}, &problem, report);
		} else if (type == QStringLiteral("resize")) {
			applied = integer(op, "width", &w) && integer(op, "height", &h) &&
			          (!op.contains(QStringLiteral("smooth")) || op.value(QStringLiteral("smooth")).isBool()) &&
			          next.resize({w, h}, op.value(QStringLiteral("smooth")).toBool(), &problem, report);
		} else if (type == QStringLiteral("canvas-size")) {
			TextureAnchor anchor = TextureAnchor::Center;
			const bool explicitOffset = op.contains(QStringLiteral("x")) || op.contains(QStringLiteral("y"));
			const bool fields = integer(op, "width", &w) && integer(op, "height", &h) &&
				(explicitOffset ? (!op.contains(QStringLiteral("anchor")) && integer(op, "x", &x) && integer(op, "y", &y)) :
				 (!op.contains(QStringLiteral("anchor")) || textureAnchorFromId(op.value(QStringLiteral("anchor")).toString(), &anchor)));
			applied = fields && next.resizeCanvas({w, h}, explicitOffset ? QPoint(x, y) : textureAnchorOffset(next.size(), {w, h}, anchor), &problem, report);
		} else if (type == QStringLiteral("resize-selection") || type == QStringLiteral("rotate-selection")) {
			TextureAnchor anchor = TextureAnchor::Center;
			const bool fields = !op.contains(QStringLiteral("anchor")) || textureAnchorFromId(op.value(QStringLiteral("anchor")).toString(), &anchor);
			applied = fields && (type == QStringLiteral("rotate-selection") ? next.rotateSelectedPixelsClockwise(anchor, &problem, report) :
				(integer(op, "width", &w) && integer(op, "height", &h) && (!op.contains(QStringLiteral("smooth")) || op.value(QStringLiteral("smooth")).isBool()) &&
				 next.resizeSelectedPixels({w, h}, op.value(QStringLiteral("smooth")).toBool(), anchor, &problem, report)));
		} else if (type == QStringLiteral("flip")) {
			const auto axis = op.value(QStringLiteral("axis")).toString();
			applied = (axis == QStringLiteral("horizontal") || axis == QStringLiteral("vertical")) && next.flip(axis == QStringLiteral("horizontal"), &problem);
		} else if (type == QStringLiteral("rotate")) {
			applied = next.rotateClockwise(&problem, report);
		} else if (type == QStringLiteral("palette")) {
			applied = (!op.contains(QStringLiteral("dither")) || op.value(QStringLiteral("dither")).isBool()) && next.remapPalette(palette, op.value(QStringLiteral("dither")).toBool(), &problem, report);
		} else if (type == QStringLiteral("layer-add")) {
			applied = next.addLayer(op.value(QStringLiteral("name")).toString(), {}, &problem);
		} else if (type == QStringLiteral("layer-select")) {
			applied = integer(op, "index", &x) && next.selectLayer(x);
		} else if (type == QStringLiteral("layer-duplicate")) {
			applied = next.duplicateLayer(&problem);
		} else if (type == QStringLiteral("layer-remove")) {
			applied = next.removeLayer(&problem);
		} else if (type == QStringLiteral("layer-move")) {
			applied = integer(op, "index", &x) && next.moveLayer(x, &problem);
		} else if (type == QStringLiteral("layer-properties")) {
			const auto* layer = next.activeLayer();
			if (layer) {
				TextureBlendMode blend = layer->blend; w = layer->opacity;
				applied = (!op.contains(QStringLiteral("opacity")) || integer(op, "opacity", &w)) &&
					(!op.contains(QStringLiteral("name")) || op.value(QStringLiteral("name")).isString()) &&
					(!op.contains(QStringLiteral("visible")) || op.value(QStringLiteral("visible")).isBool()) &&
					(!op.contains(QStringLiteral("locked")) || op.value(QStringLiteral("locked")).isBool()) &&
					(!op.contains(QStringLiteral("blend")) || textureBlendModeFromId(op.value(QStringLiteral("blend")).toString(), &blend)) &&
					next.setLayerProperties(op.value(QStringLiteral("name")).toString(layer->name),
						op.value(QStringLiteral("visible")).toBool(layer->visible), op.value(QStringLiteral("locked")).toBool(layer->locked), w, blend, &problem);
			}
		} else if (type == QStringLiteral("merge-down")) {
			applied = next.mergeDown(&problem);
		} else if (type == QStringLiteral("flatten")) {
			applied = next.flatten(&problem);
		} else if (type == QStringLiteral("select")) {
			applied = integer(op, "x", &x) && integer(op, "y", &y) && integer(op, "width", &w) && integer(op, "height", &h) && w > 0 && h > 0 && next.setSelection({x, y, w, h}, &problem);
		} else if (type == QStringLiteral("select-none")) {
			next.clearSelection(); applied = true;
		} else if (type == QStringLiteral("clear")) {
			applied = next.clearSelectedPixels(&problem);
		} else if (type == QStringLiteral("move-pixels")) {
			applied = integer(op, "x", &x) && integer(op, "y", &y) && next.moveSelectedPixels({x, y}, &problem);
		} else if (type == QStringLiteral("offset")) {
			applied = integer(op, "x", &x) && integer(op, "y", &y) && next.offsetPixels({x, y}, &problem, report);
		}
		if (!applied) {
			return fail(error, QCoreApplication::translate("VibeStudioTexture", "Operation %1 (%2) failed: %3").arg(index + 1).arg(type,
				problem.isEmpty() ? QCoreApplication::translate("VibeStudioTexture", "Invalid operation or fields.") : problem));
		}
	}
	if (!checkpoint(progress, operations.size() * 1000, operations.size() * 1000, error)) { return false; }
	*document = std::move(next);
	return true;
}

bool stageTexturePng(const QByteArray& png, const QString& virtualPath, PackageStagingModel* staging, bool replaceExisting, QString* error)
{
	if (!staging || !staging->isLoaded() || staging->sourceFormat() == PackageArchiveFormat::Wad) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Open a folder, PAK, ZIP, or PK3 before staging a PNG. Use a native texture export profile for WAD staging."));
	}
	const auto normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe() || !normalized.normalizedPath.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Choose a safe package-relative PNG path."));
	}
	QBuffer buffer; buffer.setData(png); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer, "PNG");
	if (!validTextureSize(reader.size(), error) || reader.read().isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTexture", "The staged texture is not a valid bounded PNG.")); }
	auto next = *staging;
	if (!next.addBytes(png, normalized.normalizedPath, error, replaceExisting ? PackageStageConflictResolution::ReplaceExisting : PackageStageConflictResolution::Block) || !next.summary().canSave) {
		return fail(error, QCoreApplication::translate("VibeStudioTexture", "Resolve package conflicts or enable replacement before staging this texture."));
	}
	*staging = std::move(next);
	return true;
}

} // namespace vibestudio
