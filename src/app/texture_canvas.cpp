#include "app/texture_canvas.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vibestudio {

TextureCanvas::TextureCanvas(TextureDocument* document, QWidget* parent) : QWidget(parent), m_document(document)
{
	static const bool factoryInstalled = []() {
		QAccessible::installFactory([](const QString&, QObject* object) -> QAccessibleInterface* {
			if (auto* canvas = qobject_cast<TextureCanvas*>(object)) { return new QAccessibleWidget(canvas, QAccessible::Graphic); }
			return nullptr;
		});
		return true;
	}();
	Q_UNUSED(factoryInstalled);
	setObjectName(QStringLiteral("textureCanvas"));
	setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
	setAccessibleName(QCoreApplication::translate("VibeStudioTextureEditor", "Texture editing canvas"));
	setAccessibleDescription(QCoreApplication::translate("VibeStudioTextureEditor", "Arrow keys move the pixel cursor; Space applies the tool. Shift and arrows select a rectangle. Plus and minus zoom; F fits. Middle-drag pans. Escape cancels a stroke."));
}

void TextureCanvas::setTool(Tool tool) { finishStroke(); m_tool = tool; update(); }
void TextureCanvas::setZoom(double zoom) { setZoomAt(zoom, QPointF(width() / 2.0, height() / 2.0)); }
void TextureCanvas::setZoomAt(double zoom, QPointF anchor)
{
	if (!std::isfinite(zoom) || !std::isfinite(anchor.x()) || !std::isfinite(anchor.y())) { return; }
	zoom = std::clamp(zoom, 0.0625, 64.0);
	m_offset += (anchor - QPointF(width() / 2.0, height() / 2.0) - m_offset) * (1.0 - zoom / m_zoom);
	m_zoom = zoom; update();
}
void TextureCanvas::fit()
{
	const auto size = m_document->image().size();
	if (size.isEmpty()) { return; }
	m_offset = {};
	const double tiles = m_tiled ? 3.0 : 1.0;
	setZoom(std::min((width() - 24) / (tiles * size.width()), (height() - 24) / (tiles * size.height())));
}

void TextureCanvas::refresh()
{
	setCursorPixel(m_pixel);
	m_selection = m_document->selection();
	update();
}

QRectF TextureCanvas::imageRect() const
{
	const QSizeF size = QSizeF(m_document->image().size()) * m_zoom;
	return QRectF(QPointF(width() / 2.0, height() / 2.0) + m_offset - QPointF(size.width() / 2, size.height() / 2), size);
}

QPoint TextureCanvas::pixelAt(QPointF point) const
{
	const auto position = imagePositionAt(point);
	return {int(std::floor(position.x())), int(std::floor(position.y()))};
}

QPointF TextureCanvas::imagePositionAt(QPointF point) const
{
	return (point - imageRect().topLeft()) / m_zoom;
}

bool TextureCanvas::shapeTool() const { return m_tool == Tool::Line || m_tool == Tool::Rectangle || m_tool == Tool::Ellipse; }

TextureBrush TextureCanvas::currentBrush() const
{
	return {m_brushWidth, m_tool == Tool::Pencil ? TextureBrushShape::Square : m_brushShape,
		(m_tool == Tool::Pencil || m_tool == Tool::Eraser) ? TexturePaintMode::Replace : m_paintMode, m_wrap};
}

QPoint TextureCanvas::boundedPoint(QPoint point) const
{
	const QSize size = m_document->size();
	const bool repeat = m_wrap && m_tiled && m_tool != Tool::Select;
	return {std::clamp(point.x(), repeat ? -size.width() : 0, std::max(0, (repeat ? 2 : 1) * size.width() - 1)),
		std::clamp(point.y(), repeat ? -size.height() : 0, std::max(0, (repeat ? 2 : 1) * size.height() - 1))};
}

void TextureCanvas::setCursorPixel(QPoint pixel)
{
	if (m_drawing) { pixel = boundedPoint(pixel); m_shapeEnd = pixel; }
	if (m_wrap && !m_document->size().isEmpty() && m_tool != Tool::Select) {
		pixel = {(pixel.x() % m_document->size().width() + m_document->size().width()) % m_document->size().width(),
			(pixel.y() % m_document->size().height() + m_document->size().height()) % m_document->size().height()};
	}
	m_pixel = {std::clamp(pixel.x(), 0, std::max(0, m_document->image().width() - 1)), std::clamp(pixel.y(), 0, std::max(0, m_document->image().height() - 1))};
	reportPixel(); update();
}

void TextureCanvas::reportPixel()
{
	if (m_document->image().isNull()) { return; }
	const QString text = QCoreApplication::translate("VibeStudioTextureEditor", "Pixel %1, %2 · %3").arg(m_pixel.x()).arg(m_pixel.y()).arg(m_document->image().pixelColor(m_pixel).name(QColor::HexArgb));
	const QString state = m_drawing ? QCoreApplication::translate("VibeStudioTextureEditor", "Shape %1, %2 to %3, %4 · Space commits; Escape cancels.").arg(m_shapeStart.x()).arg(m_shapeStart.y()).arg(m_shapeEnd.x()).arg(m_shapeEnd.y()) : text;
	setAccessibleDescription(state + QLatin1Char('\n') + QCoreApplication::translate("VibeStudioTextureEditor", "Arrows move, Space applies or sets shape endpoints, Shift and arrows select, plus/minus zoom, F fits, Escape cancels."));
	if (statusChanged) { statusChanged(state); }
}

void TextureCanvas::setSelection(QRect rectangle)
{
	if (!m_document->setSelection(rectangle)) { return; }
	m_selection = m_document->selection();
	if (selectionChanged) { selectionChanged(m_selection); }
	update();
}

void TextureCanvas::applyAtCursor()
{
	if (!isEnabled() || m_document->image().isNull()) { return; }
	if (m_tool == Tool::Fill) { if (fillRequested) { fillRequested(m_pixel); } }
	else if (m_tool == Tool::Pick) { if (colorPicked) { colorPicked(m_document->image().pixelColor(m_pixel)); } }
	else if (m_tool == Tool::Select) {
		m_selectionStart = m_pixel; setSelection(QRect(m_pixel, QSize(1, 1)));
	} else {
		if (shapeTool()) {
			if (m_drawing) { submitShape(); }
			else { m_shapeStart = m_shapeEnd = m_pixel; m_drawing = true; }
			reportPixel(); update(); return;
		}
		QString error;
		if (!m_document->paintStroke({m_pixel}, m_tool == Tool::Eraser ? QColor(Qt::transparent) : m_color, currentBrush(), &error)) { if (operationFailed) { operationFailed(error); } return; }
		if (changed) { changed(); }
	}
	reportPixel(); update();
}

void TextureCanvas::finishStroke()
{
	if (m_stroke) { m_document->endStroke(); m_stroke = false; if (changed) { changed(); } }
	m_selecting = false; m_panning = false; m_drawing = false; reportPixel(); update();
}

void TextureCanvas::submitShape()
{
	if (!m_drawing) { return; }
	m_drawing = false;
	const auto shape = m_tool == Tool::Line ? TextureShape::Line : (m_tool == Tool::Rectangle ? TextureShape::Rectangle : TextureShape::Ellipse);
	if (shapeRequested) { shapeRequested(shape, m_shapeStart, m_shapeEnd, currentBrush(), m_filledShapes); }
}

void TextureCanvas::beginGesture(QPoint point)
{
	if (!isEnabled() || m_document->size().isEmpty() || !validTexturePaintPoint(point, m_document->size(), m_wrap && m_tiled && m_tool != Tool::Select)) { return; }
	finishStroke(); setCursorPixel(point);
	if (m_tool == Tool::Pencil || m_tool == Tool::Brush || m_tool == Tool::Eraser) {
		QString error;
		m_stroke = m_document->beginStroke(point, m_tool == Tool::Eraser ? QColor(Qt::transparent) : m_color, currentBrush(), &error);
		if (!m_stroke && operationFailed) { operationFailed(error); }
	} else if (shapeTool()) { m_shapeStart = m_shapeEnd = point; m_drawing = true; }
	else if (m_tool == Tool::Select) { m_selecting = true; m_selectionStart = m_pixel; setSelection(QRect(m_pixel, QSize(1, 1))); }
	else { applyAtCursor(); }
	reportPixel(); update();
}

void TextureCanvas::continueGesture(QPoint point)
{
	if (!isEnabled() || m_document->size().isEmpty()) { return; }
	point = boundedPoint(point); setCursorPixel(point);
	if (m_stroke) { m_document->continueStroke(point); }
	if (m_selecting) { setSelection(texturePixelBounds(m_selectionStart, m_pixel)); }
	reportPixel(); update();
}

void TextureCanvas::endGesture()
{
	submitShape(); finishStroke(); reportPixel(); update();
}

void TextureCanvas::cancelGesture()
{
	m_document->cancelStroke(); m_stroke = false; m_selecting = false; m_panning = false; m_drawing = false;
	if (changed) { changed(); } reportPixel(); update();
}

void TextureCanvas::paintEvent(QPaintEvent*)
{
	QPainter painter(this); painter.fillRect(rect(), palette().brush(QPalette::Base));
	if (m_document->image().isNull()) { return; }
	const auto base = imageRect();
	const auto whole = m_tiled ? base.adjusted(-base.width(), -base.height(), base.width(), base.height()) : base;
	painter.save(); painter.setClipRect(whole);
	const QColor light = m_highContrast ? QColor(210, 210, 210) : QColor(92, 92, 92);
	const QColor dark = m_highContrast ? QColor(70, 70, 70) : QColor(56, 56, 56);
	const QRect visible = whole.toAlignedRect().intersected(rect());
	for (int y = visible.top() / 12 * 12; y <= visible.bottom(); y += 12) {
		for (int x = visible.left() / 12 * 12; x <= visible.right(); x += 12) { painter.fillRect(QRect(x, y, 12, 12), ((x / 12 + y / 12) & 1) ? light : dark); }
	}
	painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
	for (int y = m_tiled ? -1 : 0; y <= (m_tiled ? 1 : 0); ++y) {
		for (int x = m_tiled ? -1 : 0; x <= (m_tiled ? 1 : 0); ++x) { painter.drawImage(base.translated(x * base.width(), y * base.height()), m_document->image()); }
	}
	painter.restore();
	if (m_grid && m_zoom >= 8) {
		painter.setPen(QColor(0, 0, 0, 100));
		for (int x = 0; x <= m_document->image().width(); ++x) { const double px = base.x() + x * m_zoom; if (px >= 0 && px <= width()) { painter.drawLine(QPointF(px, base.top()), QPointF(px, base.bottom())); } }
		for (int y = 0; y <= m_document->image().height(); ++y) { const double py = base.y() + y * m_zoom; if (py >= 0 && py <= height()) { painter.drawLine(QPointF(base.left(), py), QPointF(base.right(), py)); } }
	}
	painter.setPen(QPen(palette().color(QPalette::Text), 1)); painter.drawRect(base);
	const auto outline = [&](QRect pixels) {
		const QRectF bounds(base.topLeft() + QPointF(pixels.x() * m_zoom, pixels.y() * m_zoom), QSizeF(pixels.size()) * m_zoom);
		painter.setPen(QPen(Qt::black, 3)); painter.drawRect(bounds);
		painter.setPen(QPen(Qt::white, 1, Qt::DashLine)); painter.drawRect(bounds);
	};
	if (!m_selection.isEmpty()) { outline(m_selection); }
	if (m_drawing) {
		const auto point = [&](QPoint pixel) { return base.topLeft() + QPointF((pixel.x() + 0.5) * m_zoom, (pixel.y() + 0.5) * m_zoom); };
		const QRect bounds = texturePixelBounds(m_shapeStart, m_shapeEnd);
		const QRectF area(base.topLeft() + QPointF(bounds.x() * m_zoom, bounds.y() * m_zoom), QSizeF(bounds.size()) * m_zoom);
		for (bool lightOutline : {false, true}) {
			painter.setPen(QPen(lightOutline ? Qt::white : Qt::black, lightOutline ? 1 : 3, lightOutline ? Qt::DashLine : Qt::SolidLine));
			if (m_tool == Tool::Line) { painter.drawLine(point(m_shapeStart), point(m_shapeEnd)); }
			else if (m_tool == Tool::Rectangle) { painter.drawRect(area); }
			else { painter.drawEllipse(area); }
		}
	}
	if (hasFocus()) { outline(QRect(m_pixel, QSize(1, 1))); }
}

void TextureCanvas::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	if (event->button() == Qt::MiddleButton) { m_panning = true; m_panStart = event->position(); return; }
	if (event->button() == Qt::LeftButton) { beginGesture(pixelAt(event->position())); }
}

void TextureCanvas::mouseMoveEvent(QMouseEvent* event)
{
	if (m_panning) { m_offset += event->position() - m_panStart; m_panStart = event->position(); update(); return; }
	const QPoint pixel = pixelAt(event->position());
	if (!validTexturePaintPoint(pixel, m_document->size(), m_wrap && m_tiled) && !m_stroke && !m_selecting && !m_drawing) { return; }
	continueGesture(pixel);
}

void TextureCanvas::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) {
		if (m_stroke || m_selecting || m_drawing) { continueGesture(pixelAt(event->position())); }
		endGesture();
	} else if (event->button() == Qt::MiddleButton) { m_panning = false; }
}
void TextureCanvas::focusOutEvent(QFocusEvent* event) { finishStroke(); QWidget::focusOutEvent(event); update(); }
void TextureCanvas::wheelEvent(QWheelEvent* event) { if (event->angleDelta().y()) { setZoomAt(m_zoom * (event->angleDelta().y() > 0 ? 1.25 : 0.8), event->position()); } event->accept(); }

void TextureCanvas::keyPressEvent(QKeyEvent* event)
{
	QPoint delta;
	switch (event->key()) {
	case Qt::Key_Left: delta = {-1, 0}; break;
	case Qt::Key_Right: delta = {1, 0}; break;
	case Qt::Key_Up: delta = {0, -1}; break;
	case Qt::Key_Down: delta = {0, 1}; break;
	case Qt::Key_Space: applyAtCursor(); return;
	case Qt::Key_Plus: case Qt::Key_Equal: setZoom(m_zoom * 1.25); return;
	case Qt::Key_Minus: setZoom(m_zoom / 1.25); return;
	case Qt::Key_F: fit(); return;
	case Qt::Key_0: setZoom(1); return;
	case Qt::Key_Escape:
		if (m_stroke || m_selecting || m_drawing) { cancelGesture(); } else { setSelection({}); } return;
	default: QWidget::keyPressEvent(event); return;
	}
	if (event->modifiers().testFlag(Qt::ShiftModifier)) {
		if (m_selection.isEmpty()) { m_selectionStart = m_pixel; }
		setCursorPixel(m_pixel + delta); setSelection(texturePixelBounds(m_selectionStart, m_pixel));
	} else { setCursorPixel((m_drawing ? m_shapeEnd : m_pixel) + delta); }
	QAccessibleEvent accessible(this, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&accessible);
}

} // namespace vibestudio
