#include "app/asset_views.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

void installImageAccessibility()
{
	static const bool installed = []() {
		QAccessible::installFactory([](const QString&, QObject* object) -> QAccessibleInterface* {
			if (auto* image = qobject_cast<ImagePreviewView*>(object)) { return new QAccessibleWidget(image, QAccessible::Graphic); }
			if (auto* palette = qobject_cast<PaletteSwatchView*>(object)) { return new QAccessibleWidget(palette, QAccessible::ColorChooser); }
			return nullptr;
		});
		return true;
	}();
	Q_UNUSED(installed);
}

// Zoom limits: below 5% even a 4096 texture is unreadable, above 64x a single
// texel already covers a large block of the viewport.
constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 64.0;
constexpr double kZoomStep = 1.25;
// A 1px grid line only helps once a texel is comfortably bigger than the line.
constexpr double kPixelGridZoom = 8.0;
// Tiny sprites would otherwise open filling the whole pane.
constexpr double kInitialFitCap = 8.0;
constexpr int kPaletteColumns = 16;
constexpr int kPaletteRows = 16;
constexpr int kPaletteEntryCount = kPaletteColumns * kPaletteRows;

struct PreviewColors {
	QColor background;
	QColor text;
	QColor mutedText;
	QColor outline;
	QColor accent;
	QColor grid;
	QColor contrastInk;
};

PreviewColors previewColors(const QPalette& base, bool highContrast)
{
	PreviewColors colors;
	if (highContrast) {
		colors.background = QColor(0, 0, 0);
		colors.text = QColor(255, 255, 255);
		colors.mutedText = QColor(255, 255, 255);
		colors.outline = QColor(255, 255, 255);
		colors.accent = QColor(255, 214, 0);
		colors.grid = QColor(255, 255, 255, 170);
		colors.contrastInk = QColor(0, 0, 0);
		return colors;
	}
	colors.background = base.color(QPalette::Base);
	colors.text = base.color(QPalette::Text);
	colors.mutedText = base.color(QPalette::Disabled, QPalette::Text);
	colors.outline = base.color(QPalette::Mid);
	colors.accent = base.color(QPalette::Highlight);
	colors.grid = QColor(base.color(QPalette::Mid).red(), base.color(QPalette::Mid).green(), base.color(QPalette::Mid).blue(), 130);
	colors.contrastInk = base.color(QPalette::Base);
	return colors;
}

QImage makeCheckerTile(const QColor& light, const QColor& dark)
{
	QImage tile(16, 16, QImage::Format_RGB32);
	tile.fill(dark);
	QPainter painter(&tile);
	painter.fillRect(0, 0, 8, 8, light);
	painter.fillRect(8, 8, 8, 8, light);
	painter.end();
	return tile;
}

// Built once per process; QImage holds no windowing-system resource, so a
// function-local static is safe here and keeps paintEvent allocation-free.
const QImage& checkerTile(bool highContrast)
{
	static const QImage standard = makeCheckerTile(QColor(64, 64, 68), QColor(46, 46, 50));
	static const QImage contrast = makeCheckerTile(QColor(255, 255, 255), QColor(24, 24, 24));
	return highContrast ? contrast : standard;
}

double clampZoom(double zoom)
{
	if (!std::isfinite(zoom)) {
		return 1.0;
	}
	return std::clamp(zoom, kMinZoom, kMaxZoom);
}

QString rgbHex(QRgb color)
{
	return QStringLiteral("#%1").arg(static_cast<uint>(color) & 0xFFFFFFu, 6, 16, QLatin1Char('0')).toUpper();
}

QString formatSeconds(double seconds)
{
	if (seconds < 0.0) {
		seconds = 0.0;
	}
	if (seconds >= 60.0) {
		const int totalSeconds = static_cast<int>(seconds);
		return QStringLiteral("%1:%2")
			.arg(totalSeconds / 60)
			.arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
	}
	if (seconds >= 10.0) {
		return QCoreApplication::translate("VibeStudioAssetViews", "%1 s").arg(seconds, 0, 'f', 1);
	}
	return QCoreApplication::translate("VibeStudioAssetViews", "%1 s").arg(seconds, 0, 'f', 2);
}

// 1 / 2 / 5 x 10^n tick step so the time axis lands on readable numbers.
double niceTickStep(double rawStep)
{
	if (!(rawStep > 0.0) || !std::isfinite(rawStep)) {
		return 1.0;
	}
	const double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
	const double normalized = rawStep / magnitude;
	if (normalized <= 1.0) {
		return magnitude;
	}
	if (normalized <= 2.0) {
		return 2.0 * magnitude;
	}
	if (normalized <= 5.0) {
		return 5.0 * magnitude;
	}
	return 10.0 * magnitude;
}

void drawFocusRing(QPainter& painter, const QRect& area, const PreviewColors& colors)
{
	// Two-tone ring so the focus indicator survives on light and dark content.
	QPen outer(colors.accent, 2.0);
	outer.setJoinStyle(Qt::MiterJoin);
	painter.setPen(outer);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(QRectF(area).adjusted(1.0, 1.0, -1.0, -1.0));

	QPen inner(colors.contrastInk, 1.0, Qt::DashLine);
	inner.setJoinStyle(Qt::MiterJoin);
	painter.setPen(inner);
	painter.drawRect(QRectF(area).adjusted(2.5, 2.5, -2.5, -2.5));
}

void drawEmptyState(QPainter& painter, const QRect& area, const PreviewColors& colors, const QString& text)
{
	painter.setPen(colors.mutedText);
	painter.drawText(QRectF(area).adjusted(16.0, 16.0, -16.0, -16.0), Qt::AlignCenter | Qt::TextWordWrap, text);
}

} // namespace

ImagePreviewView::ImagePreviewView(QWidget* parent)
	: QWidget(parent)
{
	installImageAccessibility();
	setObjectName("imagePreviewView");
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent, false);
	setAccessibleName(QCoreApplication::translate("VibeStudioAssetViews", "Image preview"));
	setAccessibleDescription(accessibleSummary());
}

void ImagePreviewView::setImage(const QImage& image, const QString& title)
{
	m_result = IdTechImageDecodeResult();
	m_image = image;
	m_title = title.trimmed();
	m_mipLevel = 0;
	m_frameIndex = 0;
	m_offset = QPointF();
	m_hoverPixel = QPoint(-1, -1);
	m_hoverSummary.clear();

	if (m_image.isNull()) {
		m_zoom = 1.0;
	} else {
		m_result.width = m_image.width();
		m_result.height = m_image.height();
		m_result.decoded = true;
		m_result.paletted = m_image.format() == QImage::Format_Indexed8;
		m_result.hasTransparency = m_image.hasAlphaChannel();
		zoomToFit();
		m_zoom = std::min(m_zoom, kInitialFitCap);
	}

	setAccessibleDescription(accessibleSummary());
	clampOffset();
	update();
	Q_EMIT hoverChanged(m_hoverSummary);
	Q_EMIT viewChanged();
}

void ImagePreviewView::setDecodeResult(const IdTechImageDecodeResult& result)
{
	m_result = result;
	m_image = result.image;
	m_title = result.textureName.trimmed();
	m_mipLevel = 0;
	m_frameIndex = 0;
	m_offset = QPointF();
	m_hoverPixel = QPoint(-1, -1);
	m_hoverSummary.clear();

	if (activeImage().isNull()) {
		m_zoom = 1.0;
	} else {
		zoomToFit();
		m_zoom = std::min(m_zoom, kInitialFitCap);
	}

	setAccessibleDescription(accessibleSummary());
	clampOffset();
	update();
	Q_EMIT hoverChanged(m_hoverSummary);
	Q_EMIT viewChanged();
}

void ImagePreviewView::clearImage()
{
	m_result = IdTechImageDecodeResult();
	m_image = QImage();
	m_title.clear();
	m_mipLevel = 0;
	m_frameIndex = 0;
	m_zoom = 1.0;
	m_offset = QPointF();
	m_hoverPixel = QPoint(-1, -1);
	m_hoverSummary.clear();
	m_panning = false;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(m_hoverSummary);
	Q_EMIT viewChanged();
}

bool ImagePreviewView::hasImage() const
{
	return !activeImage().isNull();
}

QImage ImagePreviewView::image() const
{
	return activeImage();
}

void ImagePreviewView::setMipLevel(int level)
{
	const int count = mipLevelCount();
	const int clamped = count > 0 ? std::clamp(level, 0, count - 1) : 0;
	if (clamped == m_mipLevel) {
		return;
	}
	m_mipLevel = clamped;
	m_hoverPixel = QPoint(-1, -1);
	setAccessibleDescription(accessibleSummary());
	clampOffset();
	update();
	Q_EMIT viewChanged();
}

int ImagePreviewView::mipLevel() const
{
	return m_mipLevel;
}

int ImagePreviewView::mipLevelCount() const
{
	return static_cast<int>(m_result.mipLevels.size());
}

void ImagePreviewView::setFrameIndex(int index)
{
	const int count = frameCount();
	const int clamped = count > 0 ? std::clamp(index, 0, count - 1) : 0;
	if (clamped == m_frameIndex) {
		return;
	}
	m_frameIndex = clamped;
	m_hoverPixel = QPoint(-1, -1);
	setAccessibleDescription(accessibleSummary());
	clampOffset();
	update();
	Q_EMIT viewChanged();
}

int ImagePreviewView::frameIndex() const
{
	return m_frameIndex;
}

int ImagePreviewView::frameCount() const
{
	return static_cast<int>(m_result.frames.size());
}

void ImagePreviewView::setShowCheckerboard(bool show)
{
	if (m_showCheckerboard == show) {
		return;
	}
	m_showCheckerboard = show;
	update();
}

bool ImagePreviewView::showCheckerboard() const
{
	return m_showCheckerboard;
}

void ImagePreviewView::setShowPixelGrid(bool show)
{
	if (m_showPixelGrid == show) {
		return;
	}
	m_showPixelGrid = show;
	update();
}

bool ImagePreviewView::showPixelGrid() const
{
	return m_showPixelGrid;
}

void ImagePreviewView::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled) {
		return;
	}
	m_highContrast = enabled;
	update();
}

void ImagePreviewView::zoomToFit()
{
	const QImage active = activeImage();
	if (active.isNull() || active.width() <= 0 || active.height() <= 0) {
		m_zoom = 1.0;
		m_offset = QPointF();
		update();
		Q_EMIT viewChanged();
		return;
	}

	const QRect area = contentsRect().adjusted(8, 8, -8, -8);
	const double availableWidth = std::max(1, area.width());
	const double availableHeight = std::max(1, area.height());
	const double fit = std::min(availableWidth / active.width(), availableHeight / active.height());
	m_zoom = clampZoom(fit);
	m_offset = QPointF();
	update();
	Q_EMIT viewChanged();
}

void ImagePreviewView::zoomToActualSize()
{
	m_zoom = 1.0;
	m_offset = QPointF();
	update();
	Q_EMIT viewChanged();
}

void ImagePreviewView::zoomIn()
{
	const double next = clampZoom(m_zoom * kZoomStep);
	if (qFuzzyCompare(next, m_zoom)) {
		return;
	}
	// Zooming about the viewport centre keeps the centred layout stable when the
	// offset simply scales with the zoom ratio.
	m_offset *= next / m_zoom;
	m_zoom = next;
	clampOffset();
	update();
	Q_EMIT viewChanged();
}

void ImagePreviewView::zoomOut()
{
	const double next = clampZoom(m_zoom / kZoomStep);
	if (qFuzzyCompare(next, m_zoom)) {
		return;
	}
	m_offset *= next / m_zoom;
	m_zoom = next;
	clampOffset();
	update();
	Q_EMIT viewChanged();
}

double ImagePreviewView::zoom() const
{
	return m_zoom;
}

QString ImagePreviewView::hoverSummary() const
{
	return m_hoverSummary;
}

QStringList ImagePreviewView::statusLines() const
{
	QStringList lines;
	const QImage active = activeImage();
	if (active.isNull()) {
		lines << QCoreApplication::translate("VibeStudioAssetViews", "No image loaded.");
		return lines;
	}

	if (!m_title.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAssetViews", "Name: %1").arg(m_title);
	}
	if (!m_result.formatName.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAssetViews", "Format: %1").arg(m_result.formatName);
	}
	lines << QCoreApplication::translate("VibeStudioAssetViews", "Dimensions: %1 x %2 px").arg(active.width()).arg(active.height());
	lines << QCoreApplication::translate("VibeStudioAssetViews", "Zoom: %1%").arg(QString::number(m_zoom * 100.0, 'f', m_zoom < 1.0 ? 1 : 0));
	if (mipLevelCount() > 1) {
		lines << QCoreApplication::translate("VibeStudioAssetViews", "Mip level: %1 of %2").arg(m_mipLevel + 1).arg(mipLevelCount());
	}
	if (frameCount() > 1) {
		const QString label = m_result.frames.at(std::clamp(m_frameIndex, 0, frameCount() - 1)).label;
		if (label.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioAssetViews", "Frame: %1 of %2").arg(m_frameIndex + 1).arg(frameCount());
		} else {
			lines << QCoreApplication::translate("VibeStudioAssetViews", "Frame: %1 of %2 (%3)").arg(m_frameIndex + 1).arg(frameCount()).arg(label);
		}
	}
	if (m_result.leftOffset != 0 || m_result.topOffset != 0) {
		lines << QCoreApplication::translate("VibeStudioAssetViews", "Offset: %1, %2").arg(m_result.leftOffset).arg(m_result.topOffset);
	}
	if (!m_result.paletteId.isEmpty()) {
		lines << (m_result.paletteGenerated
			? QCoreApplication::translate("VibeStudioAssetViews", "Palette: %1 (generated stand-in)").arg(m_result.paletteId)
			: QCoreApplication::translate("VibeStudioAssetViews", "Palette: %1").arg(m_result.paletteId));
	}
	if (!m_result.paletteSourceVirtualPath.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAssetViews", "Palette source: %1").arg(m_result.paletteSourceVirtualPath);
	}
	lines << (m_result.hasTransparency || active.hasAlphaChannel()
		? QCoreApplication::translate("VibeStudioAssetViews", "Transparency: yes")
		: QCoreApplication::translate("VibeStudioAssetViews", "Transparency: no"));
	if (!m_hoverSummary.isEmpty()) {
		lines << m_hoverSummary;
	}
	return lines;
}

QString ImagePreviewView::accessibleSummary() const
{
	const QImage active = activeImage();
	if (active.isNull()) {
		return QCoreApplication::translate("VibeStudioAssetViews", "Image preview, empty. No image is loaded.");
	}

	QStringList parts;
	const QString formatName = m_result.formatName.isEmpty()
		? QCoreApplication::translate("VibeStudioAssetViews", "image")
		: m_result.formatName;
	if (m_title.isEmpty()) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "%1, %2 by %3 pixels").arg(formatName).arg(active.width()).arg(active.height());
	} else {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "%1, %2, %3 by %4 pixels").arg(m_title, formatName).arg(active.width()).arg(active.height());
	}

	if (m_result.paletteId.isEmpty()) {
		parts << (active.format() == QImage::Format_Indexed8
			? QCoreApplication::translate("VibeStudioAssetViews", "indexed colour")
			: QCoreApplication::translate("VibeStudioAssetViews", "direct colour"));
	} else {
		parts << (m_result.paletteGenerated
			? QCoreApplication::translate("VibeStudioAssetViews", "palette %1, generated stand-in").arg(m_result.paletteId)
			: QCoreApplication::translate("VibeStudioAssetViews", "palette %1").arg(m_result.paletteId));
	}
	if (!m_result.paletteSourceVirtualPath.isEmpty()) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "palette read from %1").arg(m_result.paletteSourceVirtualPath);
	}

	parts << (m_result.hasTransparency || active.hasAlphaChannel()
		? QCoreApplication::translate("VibeStudioAssetViews", "has transparency")
		: QCoreApplication::translate("VibeStudioAssetViews", "fully opaque"));

	if (mipLevelCount() > 1) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "mip level %1 of %2").arg(m_mipLevel + 1).arg(mipLevelCount());
	}
	if (frameCount() > 1) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "frame %1 of %2").arg(m_frameIndex + 1).arg(frameCount());
	}
	parts << QCoreApplication::translate("VibeStudioAssetViews", "zoom %1 percent").arg(QString::number(m_zoom * 100.0, 'f', 0));
	return parts.join(QCoreApplication::translate("VibeStudioAssetViews", ", "));
}

QSize ImagePreviewView::sizeHint() const
{
	return QSize(480, 360);
}

QSize ImagePreviewView::minimumSizeHint() const
{
	return QSize(160, 120);
}

void ImagePreviewView::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	const PreviewColors colors = previewColors(palette(), m_highContrast);
	const QRect area = rect();
	painter.fillRect(event->rect(), colors.background);

	const QImage active = activeImage();
	if (active.isNull()) {
		drawEmptyState(painter, area, colors,
			QCoreApplication::translate("VibeStudioAssetViews", "No image to preview.\nSelect a texture, sprite, flat, or picture asset."));
		if (hasFocus()) {
			drawFocusRing(painter, area, colors);
		}
		return;
	}

	const QRectF target = imageRect();
	const QRectF clipped = target.intersected(QRectF(event->rect()));
	if (!clipped.isEmpty()) {
		if (m_showCheckerboard) {
			QBrush checker(checkerTile(m_highContrast));
			painter.setBrushOrigin(target.topLeft().toPoint());
			painter.fillRect(clipped, checker);
			painter.setBrushOrigin(QPoint(0, 0));
		}

		// Nearest-neighbour when magnifying keeps indexed idTech art crisp;
		// minification may smooth so downscaled textures do not alias.
		painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.drawImage(target, active);
		painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
	}

	if (m_showPixelGrid && m_zoom >= kPixelGridZoom && !clipped.isEmpty()) {
		QPen gridPen(colors.grid, 0.0);
		painter.setPen(gridPen);
		const int firstColumn = std::max(0, static_cast<int>(std::floor((clipped.left() - target.left()) / m_zoom)));
		const int lastColumn = std::min(active.width(), static_cast<int>(std::ceil((clipped.right() - target.left()) / m_zoom)));
		for (int column = firstColumn; column <= lastColumn; ++column) {
			const double x = target.left() + column * m_zoom;
			painter.drawLine(QPointF(x, clipped.top()), QPointF(x, clipped.bottom()));
		}
		const int firstRow = std::max(0, static_cast<int>(std::floor((clipped.top() - target.top()) / m_zoom)));
		const int lastRow = std::min(active.height(), static_cast<int>(std::ceil((clipped.bottom() - target.top()) / m_zoom)));
		for (int row = firstRow; row <= lastRow; ++row) {
			const double y = target.top() + row * m_zoom;
			painter.drawLine(QPointF(clipped.left(), y), QPointF(clipped.right(), y));
		}
	}

	// Border so the image bounds stay findable when its edges match the backdrop.
	QPen borderPen(colors.outline, 1.0);
	borderPen.setJoinStyle(Qt::MiterJoin);
	painter.setPen(borderPen);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(target.adjusted(-0.5, -0.5, 0.5, 0.5));

	if (m_hoverPixel.x() >= 0 && m_hoverPixel.y() >= 0 && m_zoom >= 4.0) {
		const QRectF texel(target.left() + m_hoverPixel.x() * m_zoom,
			target.top() + m_hoverPixel.y() * m_zoom,
			m_zoom,
			m_zoom);
		QPen outerPen(colors.contrastInk, 3.0);
		outerPen.setJoinStyle(Qt::MiterJoin);
		painter.setPen(outerPen);
		painter.drawRect(texel);
		QPen innerPen(colors.accent, 1.0);
		innerPen.setJoinStyle(Qt::MiterJoin);
		painter.setPen(innerPen);
		painter.drawRect(texel);
	}

	if (hasFocus()) {
		drawFocusRing(painter, area, colors);
	}
}

void ImagePreviewView::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton && hasImage()) {
		setFocus(Qt::MouseFocusReason);
		m_panning = true;
		m_panAnchor = event->position();
		m_panAnchorOffset = m_offset;
		setCursor(Qt::ClosedHandCursor);
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void ImagePreviewView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_panning) {
		m_offset = m_panAnchorOffset + (event->position() - m_panAnchor);
		clampOffset();
		update();
		Q_EMIT viewChanged();
		event->accept();
		return;
	}

	const QImage active = activeImage();
	QString summary;
	QPoint texel(-1, -1);
	if (!active.isNull()) {
		const QRectF target = imageRect();
		if (target.contains(event->position()) && m_zoom > 0.0) {
			const int x = static_cast<int>(std::floor((event->position().x() - target.left()) / m_zoom));
			const int y = static_cast<int>(std::floor((event->position().y() - target.top()) / m_zoom));
			if (x >= 0 && y >= 0 && x < active.width() && y < active.height()) {
				texel = QPoint(x, y);
				const QRgb color = active.pixel(x, y);
				if (active.format() == QImage::Format_Indexed8) {
					summary = QCoreApplication::translate("VibeStudioAssetViews", "Texel %1, %2 — palette index %3, %4")
						.arg(x)
						.arg(y)
						.arg(active.pixelIndex(x, y))
						.arg(rgbHex(color));
				} else if (active.hasAlphaChannel()) {
					summary = QCoreApplication::translate("VibeStudioAssetViews", "Texel %1, %2 — %3, alpha %4")
						.arg(x)
						.arg(y)
						.arg(rgbHex(color))
						.arg(qAlpha(color));
				} else {
					summary = QCoreApplication::translate("VibeStudioAssetViews", "Texel %1, %2 — %3").arg(x).arg(y).arg(rgbHex(color));
				}
			}
		}
	}

	const bool changed = summary != m_hoverSummary || texel != m_hoverPixel;
	m_hoverSummary = summary;
	m_hoverPixel = texel;
	if (changed) {
		update();
		Q_EMIT hoverChanged(m_hoverSummary);
	}
	QWidget::mouseMoveEvent(event);
}

void ImagePreviewView::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_panning && event->button() == Qt::LeftButton) {
		m_panning = false;
		unsetCursor();
		event->accept();
		return;
	}
	QWidget::mouseReleaseEvent(event);
}

void ImagePreviewView::wheelEvent(QWheelEvent* event)
{
	const QImage active = activeImage();
	if (active.isNull()) {
		QWidget::wheelEvent(event);
		return;
	}

	const int delta = event->angleDelta().y();
	if (delta == 0) {
		QWidget::wheelEvent(event);
		return;
	}

	const double factor = std::pow(kZoomStep, delta / 120.0);
	const double next = clampZoom(m_zoom * factor);
	if (qFuzzyCompare(next, m_zoom)) {
		event->accept();
		return;
	}

	// Keep the texel under the cursor pinned to the cursor.
	const QRectF target = imageRect();
	const QPointF anchor = event->position();
	const double texelX = (anchor.x() - target.left()) / m_zoom;
	const double texelY = (anchor.y() - target.top()) / m_zoom;
	const double nextWidth = active.width() * next;
	const double nextHeight = active.height() * next;
	m_offset = QPointF(anchor.x() - texelX * next - (width() - nextWidth) / 2.0,
		anchor.y() - texelY * next - (height() - nextHeight) / 2.0);
	m_zoom = next;
	clampOffset();
	update();
	Q_EMIT viewChanged();
	event->accept();
}

void ImagePreviewView::keyPressEvent(QKeyEvent* event)
{
	const double panStep = (event->modifiers() & Qt::ShiftModifier) ? 64.0 : 16.0;
	switch (event->key()) {
	case Qt::Key_Left:
		m_offset += QPointF(panStep, 0.0);
		break;
	case Qt::Key_Right:
		m_offset += QPointF(-panStep, 0.0);
		break;
	case Qt::Key_Up:
		m_offset += QPointF(0.0, panStep);
		break;
	case Qt::Key_Down:
		m_offset += QPointF(0.0, -panStep);
		break;
	case Qt::Key_Plus:
	case Qt::Key_Equal:
		zoomIn();
		event->accept();
		return;
	case Qt::Key_Minus:
	case Qt::Key_Underscore:
		zoomOut();
		event->accept();
		return;
	case Qt::Key_0:
		zoomToActualSize();
		event->accept();
		return;
	case Qt::Key_F:
		zoomToFit();
		event->accept();
		return;
	default:
		QWidget::keyPressEvent(event);
		return;
	}

	clampOffset();
	update();
	Q_EMIT viewChanged();
	event->accept();
}

void ImagePreviewView::leaveEvent(QEvent* event)
{
	m_panning = false;
	if (m_hoverPixel.x() >= 0 || !m_hoverSummary.isEmpty()) {
		m_hoverPixel = QPoint(-1, -1);
		m_hoverSummary.clear();
		update();
		Q_EMIT hoverChanged(m_hoverSummary);
	}
	QWidget::leaveEvent(event);
}

QImage ImagePreviewView::activeImage() const
{
	// An explicitly selected mip level wins, so mip browsing keeps working even
	// for formats that also expose frames. Otherwise frames, then the base mip,
	// then the decoded top-level image.
	if (m_mipLevel > 0 && m_mipLevel < static_cast<int>(m_result.mipLevels.size())) {
		const QImage& mip = m_result.mipLevels.at(m_mipLevel);
		if (!mip.isNull()) {
			return mip;
		}
	}
	if (!m_result.frames.isEmpty()) {
		const int index = std::clamp(m_frameIndex, 0, static_cast<int>(m_result.frames.size()) - 1);
		const QImage& frame = m_result.frames.at(index).image;
		if (!frame.isNull()) {
			return frame;
		}
	}
	if (!m_result.mipLevels.isEmpty()) {
		const int level = std::clamp(m_mipLevel, 0, static_cast<int>(m_result.mipLevels.size()) - 1);
		const QImage& mip = m_result.mipLevels.at(level);
		if (!mip.isNull()) {
			return mip;
		}
	}
	return m_image;
}

QRectF ImagePreviewView::imageRect() const
{
	const QImage active = activeImage();
	if (active.isNull()) {
		return QRectF();
	}
	const double scaledWidth = active.width() * m_zoom;
	const double scaledHeight = active.height() * m_zoom;
	double left = (width() - scaledWidth) / 2.0 + m_offset.x();
	double top = (height() - scaledHeight) / 2.0 + m_offset.y();
	if (m_zoom >= 1.0) {
		// Whole-pixel placement avoids half-texel seams under magnification.
		left = std::round(left);
		top = std::round(top);
	}
	return QRectF(left, top, scaledWidth, scaledHeight);
}

void ImagePreviewView::clampOffset()
{
	const QImage active = activeImage();
	if (active.isNull()) {
		m_offset = QPointF();
		return;
	}

	const double scaledWidth = active.width() * m_zoom;
	const double scaledHeight = active.height() * m_zoom;
	const double centeredLeft = (width() - scaledWidth) / 2.0;
	const double centeredTop = (height() - scaledHeight) / 2.0;
	// Always leave a graspable sliver of the image inside the viewport.
	const double marginX = std::min(scaledWidth, 48.0);
	const double marginY = std::min(scaledHeight, 48.0);

	const double minX = marginX - scaledWidth - centeredLeft;
	const double maxX = width() - marginX - centeredLeft;
	const double minY = marginY - scaledHeight - centeredTop;
	const double maxY = height() - marginY - centeredTop;

	m_offset.setX(minX <= maxX ? std::clamp(m_offset.x(), minX, maxX) : 0.0);
	m_offset.setY(minY <= maxY ? std::clamp(m_offset.y(), minY, maxY) : 0.0);
}

PaletteSwatchView::PaletteSwatchView(QWidget* parent)
	: QWidget(parent)
{
	installImageAccessibility();
	setObjectName("paletteSwatchView");
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAccessibleName(QCoreApplication::translate("VibeStudioAssetViews", "Palette swatches"));
	setAccessibleDescription(accessibleSummary());
}

void PaletteSwatchView::setPalette(const IdTechPalette& palette)
{
	m_palette = palette;
	m_hoverIndex = -1;
	m_hoverSummary.clear();
	if (m_selectedIndex >= m_palette.colors.size()) {
		m_selectedIndex = m_palette.colors.isEmpty() ? -1 : 0;
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(m_hoverSummary);
}

void PaletteSwatchView::clearPalette()
{
	m_palette = IdTechPalette();
	m_selectedIndex = -1;
	m_hoverIndex = -1;
	m_hoverSummary.clear();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(m_hoverSummary);
}

bool PaletteSwatchView::hasPalette() const
{
	return !m_palette.colors.isEmpty();
}

int PaletteSwatchView::selectedIndex() const
{
	return m_selectedIndex;
}

void PaletteSwatchView::setSelectedIndex(int index)
{
	if (!hasPalette()) {
		m_selectedIndex = -1;
		return;
	}
	const int limit = static_cast<int>(m_palette.colors.size());
	const int clamped = std::clamp(index, 0, limit - 1);
	if (clamped == m_selectedIndex) {
		return;
	}
	m_selectedIndex = clamped;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT indexSelected(m_selectedIndex);
}

QString PaletteSwatchView::hoverSummary() const
{
	return m_hoverSummary;
}

QString PaletteSwatchView::accessibleSummary() const
{
	if (!hasPalette()) {
		return QCoreApplication::translate("VibeStudioAssetViews", "Palette swatches, empty. No palette is loaded.");
	}

	QStringList parts;
	const QString name = m_palette.displayName.isEmpty() ? m_palette.id : m_palette.displayName;
	parts << QCoreApplication::translate("VibeStudioAssetViews", "Palette %1, %2 entries in a 16 by 16 grid").arg(name).arg(m_palette.colors.size());
	if (!m_palette.sourceDescription.isEmpty()) {
		parts << m_palette.sourceDescription;
	}
	if (m_palette.generated) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "generated stand-in, not a shipped game palette");
	}
	if (m_palette.transparentIndex >= 0) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "transparent index %1, drawn with a hatch pattern").arg(m_palette.transparentIndex);
	}
	if (m_palette.fullbrightStartIndex >= 0) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "fullbright range starts at index %1, marked with a dashed edge").arg(m_palette.fullbrightStartIndex);
	}
	if (m_selectedIndex >= 0 && m_selectedIndex < m_palette.colors.size()) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "selected index %1, %2").arg(m_selectedIndex).arg(rgbHex(m_palette.colors.at(m_selectedIndex)));
	} else {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "no index selected");
	}
	return parts.join(QCoreApplication::translate("VibeStudioAssetViews", ", "));
}

QSize PaletteSwatchView::sizeHint() const
{
	return QSize(272, 272);
}

QSize PaletteSwatchView::minimumSizeHint() const
{
	return QSize(128, 128);
}

void PaletteSwatchView::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	const PreviewColors colors = previewColors(palette(), false);
	painter.fillRect(event->rect(), colors.background);

	if (!hasPalette()) {
		drawEmptyState(painter, rect(), colors,
			QCoreApplication::translate("VibeStudioAssetViews", "No palette to preview.\nSelect a PLAYPAL, palette.lmp, or colormap asset."));
		if (hasFocus()) {
			drawFocusRing(painter, rect(), colors);
		}
		return;
	}

	const int cell = std::max(1, std::min(width(), height()) / kPaletteColumns);
	const int gridSize = cell * kPaletteColumns;
	const int originX = (width() - gridSize) / 2;
	const int originY = (height() - gridSize) / 2;
	const int entryCount = std::min(static_cast<int>(m_palette.colors.size()), kPaletteEntryCount);
	const int fullbrightStart = m_palette.fullbrightStartIndex;

	painter.setRenderHint(QPainter::Antialiasing, false);
	for (int index = 0; index < entryCount; ++index) {
		const int column = index % kPaletteColumns;
		const int row = index / kPaletteColumns;
		const QRect cellRect(originX + column * cell, originY + row * cell, cell, cell);
		const QColor color = QColor::fromRgb(m_palette.colors.at(index));
		painter.fillRect(cellRect, color);

		if (index == m_palette.transparentIndex) {
			// Pattern, not colour, so the transparent entry reads without hue.
			painter.fillRect(cellRect, QBrush(color.lightnessF() > 0.5f ? QColor(0, 0, 0) : QColor(255, 255, 255), Qt::BDiagPattern));
		}
	}

	// Cell separators keep adjacent similar ramps distinguishable.
	if (cell >= 6) {
		QPen separator(colors.grid, 0.0);
		painter.setPen(separator);
		for (int line = 0; line <= kPaletteColumns; ++line) {
			const int x = originX + line * cell;
			painter.drawLine(x, originY, x, originY + gridSize);
			const int y = originY + line * cell;
			painter.drawLine(originX, y, originX + gridSize, y);
		}
	}

	if (fullbrightStart >= 0 && fullbrightStart < entryCount) {
		QPen fullbrightPen(colors.accent, 2.0, Qt::DashLine);
		fullbrightPen.setJoinStyle(Qt::MiterJoin);
		painter.setPen(fullbrightPen);
		for (int index = fullbrightStart; index < entryCount; ++index) {
			const int column = index % kPaletteColumns;
			const int row = index / kPaletteColumns;
			const QRect cellRect(originX + column * cell, originY + row * cell, cell, cell);
			if (index - kPaletteColumns < fullbrightStart) {
				painter.drawLine(cellRect.topLeft(), cellRect.topRight());
			}
			if (index + kPaletteColumns >= entryCount) {
				painter.drawLine(cellRect.bottomLeft(), cellRect.bottomRight());
			}
			if (column == 0 || index - 1 < fullbrightStart) {
				painter.drawLine(cellRect.topLeft(), cellRect.bottomLeft());
			}
			if (column == kPaletteColumns - 1 || index + 1 >= entryCount) {
				painter.drawLine(cellRect.topRight(), cellRect.bottomRight());
			}
		}
	}

	if (m_hoverIndex >= 0 && m_hoverIndex < entryCount && m_hoverIndex != m_selectedIndex) {
		const QRect cellRect(originX + (m_hoverIndex % kPaletteColumns) * cell,
			originY + (m_hoverIndex / kPaletteColumns) * cell,
			cell,
			cell);
		QPen hoverPen(colors.outline, 1.0, Qt::DotLine);
		hoverPen.setJoinStyle(Qt::MiterJoin);
		painter.setPen(hoverPen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(cellRect).adjusted(0.5, 0.5, -0.5, -0.5));
	}

	if (m_selectedIndex >= 0 && m_selectedIndex < entryCount) {
		const QRect cellRect(originX + (m_selectedIndex % kPaletteColumns) * cell,
			originY + (m_selectedIndex / kPaletteColumns) * cell,
			cell,
			cell);
		// Black-on-white double outline stays visible over any swatch colour.
		QPen outerPen(QColor(0, 0, 0), 3.0);
		outerPen.setJoinStyle(Qt::MiterJoin);
		painter.setPen(outerPen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(cellRect).adjusted(-1.5, -1.5, 1.5, 1.5));
		QPen innerPen(QColor(255, 255, 255), 1.0);
		innerPen.setJoinStyle(Qt::MiterJoin);
		painter.setPen(innerPen);
		painter.drawRect(QRectF(cellRect).adjusted(-0.5, -0.5, 0.5, 0.5));
	}

	if (hasFocus()) {
		drawFocusRing(painter, rect(), colors);
	}
}

void PaletteSwatchView::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) {
		setFocus(Qt::MouseFocusReason);
		const int index = indexAt(event->position().toPoint());
		if (index >= 0) {
			setSelectedIndex(index);
			event->accept();
			return;
		}
	}
	QWidget::mousePressEvent(event);
}

void PaletteSwatchView::mouseMoveEvent(QMouseEvent* event)
{
	const int index = indexAt(event->position().toPoint());
	QString summary;
	if (index >= 0 && index < m_palette.colors.size()) {
		const QRgb color = m_palette.colors.at(index);
		if (index == m_palette.transparentIndex) {
			summary = QCoreApplication::translate("VibeStudioAssetViews", "Index %1 — %2 (transparent index)").arg(index).arg(rgbHex(color));
		} else if (m_palette.fullbrightStartIndex >= 0 && index >= m_palette.fullbrightStartIndex) {
			summary = QCoreApplication::translate("VibeStudioAssetViews", "Index %1 — %2 (fullbright)").arg(index).arg(rgbHex(color));
		} else {
			summary = QCoreApplication::translate("VibeStudioAssetViews", "Index %1 — %2").arg(index).arg(rgbHex(color));
		}
	}

	if (index != m_hoverIndex || summary != m_hoverSummary) {
		m_hoverIndex = index;
		m_hoverSummary = summary;
		update();
		Q_EMIT hoverChanged(m_hoverSummary);
	}
	QWidget::mouseMoveEvent(event);
}

void PaletteSwatchView::keyPressEvent(QKeyEvent* event)
{
	if (!hasPalette()) {
		QWidget::keyPressEvent(event);
		return;
	}

	const int limit = std::min(static_cast<int>(m_palette.colors.size()), kPaletteEntryCount);
	const int current = m_selectedIndex < 0 ? 0 : m_selectedIndex;
	int next = current;
	switch (event->key()) {
	case Qt::Key_Left:
		next = current - 1;
		break;
	case Qt::Key_Right:
		next = current + 1;
		break;
	case Qt::Key_Up:
		next = current - kPaletteColumns;
		break;
	case Qt::Key_Down:
		next = current + kPaletteColumns;
		break;
	case Qt::Key_PageUp:
		next = current - kPaletteColumns * 4;
		break;
	case Qt::Key_PageDown:
		next = current + kPaletteColumns * 4;
		break;
	case Qt::Key_Home:
		next = 0;
		break;
	case Qt::Key_End:
		next = limit - 1;
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}

	if (m_selectedIndex < 0) {
		next = 0;
	}
	setSelectedIndex(std::clamp(next, 0, limit - 1));
	event->accept();
}

void PaletteSwatchView::leaveEvent(QEvent* event)
{
	if (m_hoverIndex >= 0 || !m_hoverSummary.isEmpty()) {
		m_hoverIndex = -1;
		m_hoverSummary.clear();
		update();
		Q_EMIT hoverChanged(m_hoverSummary);
	}
	QWidget::leaveEvent(event);
}

int PaletteSwatchView::indexAt(const QPoint& point) const
{
	if (!hasPalette()) {
		return -1;
	}
	const int cell = std::max(1, std::min(width(), height()) / kPaletteColumns);
	const int gridSize = cell * kPaletteColumns;
	const int originX = (width() - gridSize) / 2;
	const int originY = (height() - gridSize) / 2;
	if (point.x() < originX || point.y() < originY || point.x() >= originX + gridSize || point.y() >= originY + gridSize) {
		return -1;
	}
	const int column = (point.x() - originX) / cell;
	const int row = (point.y() - originY) / cell;
	if (column < 0 || column >= kPaletteColumns || row < 0 || row >= kPaletteRows) {
		return -1;
	}
	const int index = row * kPaletteColumns + column;
	return index < m_palette.colors.size() ? index : -1;
}

WaveformView::WaveformView(QWidget* parent)
	: QWidget(parent)
{
	setObjectName("waveformView");
	// Focusable, so the playhead can be moved from the keyboard.
	setFocusPolicy(Qt::StrongFocus);
	setAccessibleName(QCoreApplication::translate("VibeStudioAssetViews", "Audio waveform"));
	setAccessibleDescription(accessibleSummary());
}

void WaveformView::setPeaks(const QVector<float>& peaks, int channels, int sampleRate, qint64 durationMs)
{
	m_peaks = peaks;
	m_channels = std::max(0, channels);
	m_sampleRate = std::max(0, sampleRate);
	m_durationMs = std::max<qint64>(0, durationMs);
	m_playheadMs = 0;
	m_selectionStartMs = m_selectionEndMs = m_selectionAnchorMs = 0;
	if (m_channels <= 0 || m_peaks.size() < 2) {
		m_peaks.clear();
		m_channels = 0;
	}
	setAccessibleDescription(accessibleSummary());
	update();
}

void WaveformView::clearPeaks()
{
	m_peaks.clear();
	m_channels = 0;
	m_sampleRate = 0;
	m_durationMs = 0;
	m_playheadMs = 0;
	m_selectionStartMs = m_selectionEndMs = m_selectionAnchorMs = 0;
	setAccessibleDescription(accessibleSummary());
	update();
}

void WaveformView::setEmptyText(const QString& text)
{
	if (m_emptyText != text) {
		m_emptyText = text;
		update();
	}
}

bool WaveformView::hasPeaks() const
{
	return m_channels > 0 && m_peaks.size() >= 2 * m_channels;
}

qint64 WaveformView::durationMs() const
{
	return m_durationMs;
}

void WaveformView::setPlayhead(qint64 positionMs)
{
	const qint64 clamped = std::clamp<qint64>(positionMs, 0, m_durationMs);
	if (clamped == m_playheadMs) {
		return;
	}
	m_playheadMs = clamped;
	setAccessibleDescription(accessibleSummary());
	update();
}

qint64 WaveformView::playhead() const
{
	return m_playheadMs;
}

// The lanes' rectangle, as paintEvent lays it out.
QRect WaveformView::laneArea() const
{
	const int axisHeight = fontMetrics().height() + 8;
	return rect().adjusted(8, 8, -8, -(axisHeight + 4));
}

double WaveformView::xForTime(qint64 positionMs) const
{
	const QRect lanes = laneArea();
	if (m_durationMs <= 0) {
		return lanes.left();
	}
	return lanes.left() + static_cast<double>(std::clamp<qint64>(positionMs, 0, m_durationMs)) * lanes.width() / static_cast<double>(m_durationMs);
}

qint64 WaveformView::timeForX(double x) const
{
	const QRect lanes = laneArea();
	if (m_durationMs <= 0 || lanes.width() <= 0) {
		return 0;
	}
	const double ratio = std::clamp((x - lanes.left()) / static_cast<double>(lanes.width()), 0.0, 1.0);
	return static_cast<qint64>(std::llround(ratio * static_cast<double>(m_durationMs)));
}

void WaveformView::seekTo(qint64 positionMs)
{
	setPlayhead(positionMs);
	emit seekRequested(m_playheadMs);
}

void WaveformView::setSelectionEnabled(bool enabled)
{
	m_selectionEnabled = enabled;
	if (!enabled) { setSelection(0, 0); }
	setAccessibleDescription(accessibleSummary());
}

void WaveformView::setSelection(qint64 firstMs, qint64 endMs)
{
	m_selectionStartMs = std::clamp<qint64>(std::min(firstMs, endMs), 0, m_durationMs);
	m_selectionEndMs = std::clamp<qint64>(std::max(firstMs, endMs), 0, m_durationMs);
	setAccessibleDescription(accessibleSummary());
	update();
}

void WaveformView::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton || !hasPeaks() || m_durationMs <= 0) {
		QWidget::mousePressEvent(event);
		return;
	}
	setFocus(Qt::MouseFocusReason);
	if (m_selectionEnabled) {
		if (!(event->modifiers() & Qt::ShiftModifier)) { m_selectionAnchorMs = timeForX(event->position().x()); }
		setSelection(m_selectionAnchorMs, timeForX(event->position().x()));
		emit selectionChanged(m_selectionStartMs, m_selectionEndMs);
	}
	seekTo(timeForX(event->position().x()));
	event->accept();
}

void WaveformView::mouseMoveEvent(QMouseEvent* event)
{
	if (!(event->buttons() & Qt::LeftButton) || !hasPeaks() || m_durationMs <= 0) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	if (m_selectionEnabled) {
		setSelection(m_selectionAnchorMs, timeForX(event->position().x()));
		emit selectionChanged(m_selectionStartMs, m_selectionEndMs);
	}
	seekTo(timeForX(event->position().x()));
	event->accept();
}

void WaveformView::keyPressEvent(QKeyEvent* event)
{
	if (!hasPeaks() || m_durationMs <= 0) {
		QWidget::keyPressEvent(event);
		return;
	}
	const qint64 fine = std::max<qint64>(10, m_durationMs / 50);
	const qint64 coarse = std::max<qint64>(10, m_durationMs / 10);
	if (m_selectionEnabled && !(event->modifiers() & Qt::ShiftModifier)) { m_selectionAnchorMs = m_playheadMs; }
	switch (event->key()) {
	case Qt::Key_Left:
		seekTo(m_playheadMs - fine);
		break;
	case Qt::Key_Right:
		seekTo(m_playheadMs + fine);
		break;
	case Qt::Key_PageUp:
		seekTo(m_playheadMs - coarse);
		break;
	case Qt::Key_PageDown:
		seekTo(m_playheadMs + coarse);
		break;
	case Qt::Key_Home:
		seekTo(0);
		break;
	case Qt::Key_End:
		seekTo(m_durationMs);
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	if (m_selectionEnabled) {
		if (event->modifiers() & Qt::ShiftModifier) { setSelection(m_selectionAnchorMs, m_playheadMs); }
		else { m_selectionAnchorMs = m_playheadMs; setSelection(m_playheadMs, m_playheadMs); }
		emit selectionChanged(m_selectionStartMs, m_selectionEndMs);
	}
	event->accept();
}

void WaveformView::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	update();
}

void WaveformView::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
}

void WaveformView::setHighContrast(bool enabled)
{
	if (m_highContrast == enabled) {
		return;
	}
	m_highContrast = enabled;
	update();
}

QString WaveformView::accessibleSummary() const
{
	if (!hasPeaks()) {
		return QCoreApplication::translate("VibeStudioAssetViews", "Audio waveform, empty. No decoded audio is loaded.");
	}

	float peak = 0.0f;
	for (float value : m_peaks) {
		peak = std::max(peak, std::fabs(value));
	}

	QStringList parts;
	parts << (m_channels == 1
		? QCoreApplication::translate("VibeStudioAssetViews", "Waveform, mono")
		: (m_channels == 2 ? QCoreApplication::translate("VibeStudioAssetViews", "Waveform, stereo") : QCoreApplication::translate("VibeStudioAssetViews", "Waveform, %1 channels").arg(m_channels)));
	if (m_sampleRate > 0) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "%1 Hz").arg(m_sampleRate);
	}
	parts << QCoreApplication::translate("VibeStudioAssetViews", "duration %1").arg(formatSeconds(static_cast<double>(m_durationMs) / 1000.0));
	parts << QCoreApplication::translate("VibeStudioAssetViews", "playhead at %1").arg(formatSeconds(static_cast<double>(m_playheadMs) / 1000.0));
	if (m_selectionEnabled) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "selection from %1 to %2 milliseconds; Shift with navigation keys extends the selection")
			.arg(m_selectionStartMs).arg(m_selectionEndMs);
	}
	if (peak <= 0.0f) {
		parts << QCoreApplication::translate("VibeStudioAssetViews", "silent");
	} else {
		const double peakLevel = static_cast<double>(peak);
		parts << QCoreApplication::translate("VibeStudioAssetViews", "peak level %1 percent, %2 dBFS")
			.arg(QString::number(peakLevel * 100.0, 'f', 1), QString::number(20.0 * std::log10(peakLevel), 'f', 1));
	}
	return parts.join(QCoreApplication::translate("VibeStudioAssetViews", ", "));
}

QSize WaveformView::sizeHint() const
{
	return QSize(480, 160);
}

QSize WaveformView::minimumSizeHint() const
{
	return QSize(160, 72);
}

void WaveformView::paintEvent(QPaintEvent* event)
{
	QPainter painter(this);
	const PreviewColors colors = previewColors(palette(), m_highContrast);
	painter.fillRect(event->rect(), colors.background);

	if (!hasPeaks()) {
		drawEmptyState(painter, rect(), colors, !m_emptyText.isEmpty() ? m_emptyText
			: QCoreApplication::translate("VibeStudioAssetViews", "No waveform to preview.\nSelect a WAV asset, or the audio could not be decoded."));
		return;
	}

	const int pairsPerChannel = static_cast<int>(m_peaks.size()) / (2 * m_channels);
	if (pairsPerChannel <= 0) {
		drawEmptyState(painter, rect(), colors, QCoreApplication::translate("VibeStudioAssetViews", "Decoded audio contains no peak data."));
		return;
	}

	const QFontMetrics metrics(painter.font());
	const int axisHeight = metrics.height() + 8;
	const QRect content = laneArea();
	if (content.width() <= 2 || content.height() <= 2) {
		return;
	}

	const int laneHeight = std::max(8, content.height() / m_channels);
	painter.setRenderHint(QPainter::Antialiasing, false);

	QPen envelopePen(colors.accent, 1.0);
	QPen centerPen(colors.outline, 1.0, Qt::DashLine);
	QPen labelPen(colors.mutedText, 1.0);

	for (int channel = 0; channel < m_channels; ++channel) {
		const int laneTop = content.top() + channel * laneHeight;
		const QRect lane(content.left(), laneTop, content.width(), laneHeight - 2);
		const double center = lane.top() + lane.height() / 2.0;
		const double halfHeight = std::max(1.0, lane.height() / 2.0 - 1.0);

		painter.setPen(QPen(colors.outline, 1.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(lane).adjusted(0.5, 0.5, -0.5, -0.5));

		painter.setPen(centerPen);
		painter.drawLine(QPointF(lane.left(), center), QPointF(lane.right(), center));

		painter.setPen(envelopePen);
		const int base = channel * pairsPerChannel * 2;
		for (int x = 0; x < lane.width(); ++x) {
			const int firstPair = static_cast<int>(static_cast<qint64>(x) * pairsPerChannel / lane.width());
			int lastPair = static_cast<int>(static_cast<qint64>(x + 1) * pairsPerChannel / lane.width());
			lastPair = std::clamp(lastPair, firstPair + 1, pairsPerChannel);
			float minValue = 1.0f;
			float maxValue = -1.0f;
			for (int pair = firstPair; pair < lastPair; ++pair) {
				minValue = std::min(minValue, m_peaks.at(base + pair * 2));
				maxValue = std::max(maxValue, m_peaks.at(base + pair * 2 + 1));
			}
			if (minValue > maxValue) {
				minValue = 0.0f;
				maxValue = 0.0f;
			}
			const double top = center - std::clamp(static_cast<double>(maxValue), -1.0, 1.0) * halfHeight;
			const double bottom = center - std::clamp(static_cast<double>(minValue), -1.0, 1.0) * halfHeight;
			const double px = lane.left() + x + 0.5;
			painter.drawLine(QPointF(px, std::min(top, bottom)), QPointF(px, std::max(top, bottom) + 1.0));
		}

		const QString laneLabel = m_channels == 1
			? QCoreApplication::translate("VibeStudioAssetViews", "Mono")
			: (m_channels == 2 ? (channel == 0 ? QCoreApplication::translate("VibeStudioAssetViews", "Left") : QCoreApplication::translate("VibeStudioAssetViews", "Right")) : QCoreApplication::translate("VibeStudioAssetViews", "Ch %1").arg(channel + 1));
		// On a chip of the background, so the label reads over a loud envelope.
		const QRect chip(lane.left() + 2, lane.top() + 2, metrics.horizontalAdvance(laneLabel) + 8, metrics.height() + 2);
		painter.fillRect(chip, colors.background);
		painter.setPen(labelPen);
		painter.drawText(chip, Qt::AlignCenter, laneLabel);
	}

	if (m_selectionEnabled && m_selectionEndMs > m_selectionStartMs) {
		const QRectF selection(xForTime(m_selectionStartMs), content.top(),
			xForTime(m_selectionEndMs) - xForTime(m_selectionStartMs), content.height());
		QColor fill = colors.accent;
		fill.setAlpha(m_highContrast ? 85 : 45);
		painter.fillRect(selection, fill);
		painter.setPen(QPen(colors.text, 1.0, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(selection);
	}

	// Time ticks along the bottom, derived from the reported duration.
	const double totalSeconds = static_cast<double>(m_durationMs) / 1000.0;
	const QRect axis(content.left(), content.bottom() + 2, content.width(), axisHeight);
	painter.setPen(QPen(colors.outline, 1.0));
	painter.drawLine(axis.left(), axis.top(), axis.right(), axis.top());

	if (totalSeconds > 0.0) {
		const int tickSpacing = std::max(90, 2 * metrics.horizontalAdvance(formatSeconds(totalSeconds)) + 16);
		const int targetTicks = std::clamp(axis.width() / tickSpacing, 2, 10);
		const double step = niceTickStep(totalSeconds / targetTicks);
		painter.setPen(labelPen);
		for (double t = 0.0; t <= totalSeconds + step * 0.001; t += step) {
			const double ratio = std::clamp(t / totalSeconds, 0.0, 1.0);
			const double x = axis.left() + ratio * axis.width();
			painter.setPen(QPen(colors.outline, 1.0));
			painter.drawLine(QPointF(x, axis.top()), QPointF(x, axis.top() + 4.0));
			painter.setPen(labelPen);
			const QString label = formatSeconds(t);
			const int labelWidth = metrics.horizontalAdvance(label);
			double labelLeft = x - labelWidth / 2.0;
			labelLeft = std::clamp(labelLeft, static_cast<double>(axis.left()), static_cast<double>(axis.right() - labelWidth));
			painter.drawText(QRectF(labelLeft, axis.top() + 4.0, labelWidth, axis.height() - 4.0),
				Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextForceLeftToRight, label);
		}
	} else {
		painter.setPen(labelPen);
		painter.drawText(axis, Qt::AlignLeft | Qt::AlignVCenter, QCoreApplication::translate("VibeStudioAssetViews", "Duration unavailable"));
	}

	// The playhead: a text-coloured line over the envelope, with a notch on the
	// axis so it reads without colour.
	if (m_durationMs > 0) {
		const double x = std::round(xForTime(m_playheadMs)) + 0.5;
		painter.setPen(QPen(colors.text, 2.0));
		painter.drawLine(QPointF(x, content.top()), QPointF(x, content.bottom() + 2.0));
		QPolygonF notch;
		notch << QPointF(x - 5.0, content.bottom() + 2.0) << QPointF(x + 5.0, content.bottom() + 2.0) << QPointF(x, content.bottom() + 8.0);
		painter.setBrush(colors.text);
		painter.setPen(Qt::NoPen);
		painter.drawPolygon(notch);
	}
	if (hasFocus()) {
		painter.setPen(QPen(colors.accent, 2.0));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0));
	}
}

} // namespace vibestudio
