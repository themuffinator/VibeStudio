#include "app/material_preview_view.h"

#include "app/material_tasks.h"
#include "app/studio_theme.h"

#include <QAccessible>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

constexpr int kFrameIntervalMs = 33;
constexpr int kMaximumSide = 2048;

const MaterialImageSet& emptyImages()
{
	static const MaterialImageSet images;
	return images;
}

const MaterialTableSet& emptyTables()
{
	static const MaterialTableSet tables;
	return tables;
}

} // namespace

MaterialPreviewView::MaterialPreviewView(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("materialPreview"));
	setFocusPolicy(Qt::StrongFocus);
	setMinimumSize(160, 120);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	setAccessibleName(tr("Material preview"));
	setToolTip(tr("Drag to orbit, Shift+drag to move the light, wheel to zoom, double-click to reset. "
				  "Space plays or pauses; comma and full stop step time."));
	m_options.shape = MaterialPreviewShape::Wall;
	m_timer = new QTimer(this);
	m_timer->setInterval(kFrameIntervalMs);
	m_timer->setTimerType(Qt::PreciseTimer);
	connect(m_timer, &QTimer::timeout, this, &MaterialPreviewView::tick);
	m_lane = new MaterialTaskLane(this);
	m_clock.start();
	m_message = tr("Choose a material to see it the way its engine draws it.");
	refreshAccessibleDescription();
}

MaterialPreviewView::~MaterialPreviewView()
{
	// Wait for a frame in flight before the members it publishes into go.
	delete m_lane;
	m_lane = nullptr;
}

void MaterialPreviewView::setMaterial(const MaterialDefinition& definition, std::shared_ptr<const MaterialImageSet> images,
	std::shared_ptr<const MaterialTableSet> tables)
{
	m_definition = definition;
	m_images = std::move(images);
	m_tables = std::move(tables);
	m_hasMaterial = true;
	m_message.clear();
	refreshAccessibleDescription();
	requestRender(true);
}

void MaterialPreviewView::clearMaterial(const QString& message)
{
	m_hasMaterial = false;
	m_definition = MaterialDefinition();
	m_images.reset();
	m_tables.reset();
	m_message = message;
	m_image = QImage();
	m_result = MaterialRenderResult();
	if (m_lane) {
		m_lane->cancel();
	}
	refreshAccessibleDescription();
	update();
}

void MaterialPreviewView::setRenderOptions(const MaterialRenderOptions& options)
{
	// Time, size and camera stay the view's own.
	const MaterialRenderOptions current = m_options;
	m_options = options;
	m_options.time = current.time;
	m_options.size = current.size;
	m_options.yaw = current.yaw;
	m_options.pitch = current.pitch;
	m_options.zoom = current.zoom;
	if (options.shape != current.shape) {
		m_defaultPitch = defaultMaterialPreviewPitch(options.shape);
		m_options.pitch = m_defaultPitch;
	}
	refreshAccessibleDescription();
	requestRender(true);
}

void MaterialPreviewView::setShape(MaterialPreviewShape shape)
{
	if (shape == m_options.shape) {
		return;
	}
	m_options.shape = shape;
	m_defaultPitch = defaultMaterialPreviewPitch(shape);
	m_options.pitch = m_defaultPitch;
	refreshAccessibleDescription();
	requestRender(true);
}

double MaterialPreviewView::time() const
{
	if (!m_playing) {
		return m_time;
	}
	return m_clockBase + static_cast<double>(m_clock.elapsed()) / 1000.0 * m_speed;
}

void MaterialPreviewView::setPlaying(bool playing)
{
	if (playing == m_playing) {
		return;
	}
	if (playing) {
		m_playing = true;
		restartClock();
		if (isVisible()) {
			m_timer->start();
		}
	} else {
		m_time = time();
		m_playing = false;
		m_timer->stop();
		// The paused frame is drawn at full resolution.
		requestRender(true);
	}
	refreshAccessibleDescription();
	Q_EMIT playingChanged(m_playing);
}

void MaterialPreviewView::setTime(double seconds)
{
	m_time = std::clamp(seconds, 0.0, 86400.0);
	restartClock();
	Q_EMIT timeChanged(m_time);
	requestRender(!m_playing);
}

void MaterialPreviewView::setSpeed(double speed)
{
	const double now = time();
	m_speed = std::clamp(speed, 0.05, 16.0);
	m_time = now;
	restartClock();
}

void MaterialPreviewView::stepTime(double seconds)
{
	setPlaying(false);
	setTime(time() + seconds);
}

void MaterialPreviewView::resetCamera()
{
	m_options.yaw = m_defaultYaw;
	m_options.pitch = m_defaultPitch;
	m_options.zoom = 1.0;
	Q_EMIT cameraChanged();
	requestRender(true);
}

void MaterialPreviewView::setReducedMotion(bool reduced)
{
	m_reducedMotion = reduced;
	if (reduced) {
		setPlaying(false);
	}
}

bool MaterialPreviewView::renderPending() const
{
	return m_lane && m_lane->pending();
}

void MaterialPreviewView::restartClock()
{
	m_clockBase = m_time;
	m_clock.restart();
}

void MaterialPreviewView::tick()
{
	if (!m_playing) {
		m_timer->stop();
		return;
	}
	Q_EMIT timeChanged(time());
	requestRender(false);
}

void MaterialPreviewView::requestRender(bool supersede)
{
	if (!m_hasMaterial || !m_lane) {
		update();
		return;
	}
	if (!isVisible() || width() < 8 || height() < 8) {
		m_dirty = true;
		return;
	}
	m_dirty = false;
	MaterialRenderOptions options = m_options;
	const double dpr = devicePixelRatioF();
	const double scale = m_playing || m_dragging ? m_playScale : 1.0;
	options.size = QSize(std::clamp(static_cast<int>(std::lround(width() * dpr * scale)), 16, kMaximumSide),
		std::clamp(static_cast<int>(std::lround(height() * dpr * scale)), 16, kMaximumSide));
	options.time = time();
	const MaterialDefinition definition = m_definition;
	const std::shared_ptr<const MaterialImageSet> images = m_images;
	const std::shared_ptr<const MaterialTableSet> tables = m_tables;
	const bool animating = m_playing || m_dragging;
	m_lane->submit(
		[this, definition, images, tables, options, animating](const MaterialTaskLane::Cancelled& cancelled) -> std::function<void()> {
			MaterialRenderResult result =
				renderMaterial(definition, images ? *images : emptyImages(), tables ? *tables : emptyTables(), options, cancelled);
			if (result.cancelled) {
				return {};
			}
			return [this, result = std::move(result), animating]() mutable {
				m_image = std::move(result.image);
				result.image = QImage();
				m_result = std::move(result);
				if (animating) {
					// Keep about 30 frames a second by drawing fewer pixels.
					if (m_result.milliseconds > 40.0) {
						m_playScale = std::max(0.3, m_playScale * 0.85);
					} else if (m_result.milliseconds < 16.0) {
						m_playScale = std::min(1.0, m_playScale * 1.1);
					}
				}
				update();
				Q_EMIT frameRendered();
				if (m_dirty) {
					requestRender(true);
				}
			};
		},
		supersede);
}

void MaterialPreviewView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	const StudioThemeColors& colors = currentStudioTheme().colors;
	painter.fillRect(rect(), colors.surface);
	if (m_hasMaterial && !m_image.isNull()) {
		painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
		painter.drawImage(rect(), m_image);
	} else {
		painter.setPen(colors.textMuted);
		const QRect area = rect().adjusted(24, 24, -24, -24);
		painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, m_hasMaterial ? tr("Drawing...") : m_message);
	}
	if (m_hasMaterial && m_result.fallback) {
		// The engine would not draw this material; say so on the image, not
		// by colour alone.
		const QString text = tr("Engine fallback shown");
		QFont font = painter.font();
		font.setBold(true);
		painter.setFont(font);
		const QFontMetrics metrics(font);
		const int pad = 6;
		QRect chip(0, 0, metrics.horizontalAdvance(text) + pad * 2 + 14, metrics.height() + pad);
		chip.moveTopLeft(layoutDirection() == Qt::RightToLeft ? QPoint(width() - chip.width() - 8, 8) : QPoint(8, 8));
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);
		painter.setBrush(colors.panelRaised);
		painter.drawRoundedRect(chip, 4, 4);
		painter.setBrush(colors.warning);
		const QRect mark(layoutDirection() == Qt::RightToLeft ? chip.right() - pad - 8 : chip.left() + pad, chip.center().y() - 4, 8, 8);
		painter.drawEllipse(mark);
		painter.setPen(colors.text);
		painter.drawText(chip.adjusted(layoutDirection() == Qt::RightToLeft ? pad : pad + 14, 0,
							 layoutDirection() == Qt::RightToLeft ? -(pad + 14) : -pad, 0),
			Qt::AlignVCenter | Qt::AlignLeading, text);
	}
	if (hasFocus()) {
		const StudioThemeTokens& tokens = currentStudioTheme();
		QPen pen(colors.focus);
		pen.setWidth(std::max(2, tokens.metrics.focusWidth * 2));
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}

void MaterialPreviewView::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	requestRender(true);
}

void MaterialPreviewView::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	if (m_playing) {
		restartClock();
		m_timer->start();
	}
	if (m_dirty || (m_hasMaterial && m_image.isNull())) {
		requestRender(true);
	}
}

void MaterialPreviewView::hideEvent(QHideEvent* event)
{
	// A hidden preview costs nothing: the clock stops with it.
	if (m_playing) {
		m_time = time();
		m_timer->stop();
	}
	QWidget::hideEvent(event);
}

void MaterialPreviewView::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
		m_dragging = true;
		m_draggingLight = event->button() == Qt::RightButton || (event->modifiers() & Qt::ShiftModifier);
		m_dragOrigin = event->position().toPoint();
		m_dragYaw = m_options.yaw;
		m_dragPitch = m_options.pitch;
		m_dragLight = m_options.lighting.lightAngle;
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void MaterialPreviewView::mouseMoveEvent(QMouseEvent* event)
{
	if (!m_dragging) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	const QPoint delta = event->position().toPoint() - m_dragOrigin;
	if (m_draggingLight) {
		m_options.lighting.lightOrbit = false;
		m_options.lighting.lightAngle = std::fmod(m_dragLight + delta.x() * 0.6, 360.0);
	} else {
		m_options.yaw = std::fmod(m_dragYaw - delta.x() * 0.5, 360.0);
		m_options.pitch = std::clamp(m_dragPitch + delta.y() * 0.4, -85.0, 85.0);
	}
	Q_EMIT cameraChanged();
	requestRender(false);
	event->accept();
}

void MaterialPreviewView::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_dragging) {
		m_dragging = false;
		m_draggingLight = false;
		requestRender(true);
		event->accept();
		return;
	}
	QWidget::mouseReleaseEvent(event);
}

void MaterialPreviewView::mouseDoubleClickEvent(QMouseEvent* event)
{
	resetCamera();
	event->accept();
}

void MaterialPreviewView::wheelEvent(QWheelEvent* event)
{
	const double steps = event->angleDelta().y() / 120.0;
	if (steps == 0.0) {
		QWidget::wheelEvent(event);
		return;
	}
	m_options.zoom = std::clamp(m_options.zoom * std::pow(1.12, steps), 0.25, 6.0);
	Q_EMIT cameraChanged();
	requestRender(true);
	event->accept();
}

void MaterialPreviewView::keyPressEvent(QKeyEvent* event)
{
	switch (event->key()) {
	case Qt::Key_Space:
		setPlaying(!m_playing);
		break;
	case Qt::Key_Left:
		m_options.yaw = std::fmod(m_options.yaw + 10.0, 360.0);
		Q_EMIT cameraChanged();
		requestRender(true);
		break;
	case Qt::Key_Right:
		m_options.yaw = std::fmod(m_options.yaw - 10.0, 360.0);
		Q_EMIT cameraChanged();
		requestRender(true);
		break;
	case Qt::Key_Up:
		m_options.pitch = std::clamp(m_options.pitch + 8.0, -85.0, 85.0);
		Q_EMIT cameraChanged();
		requestRender(true);
		break;
	case Qt::Key_Down:
		m_options.pitch = std::clamp(m_options.pitch - 8.0, -85.0, 85.0);
		Q_EMIT cameraChanged();
		requestRender(true);
		break;
	case Qt::Key_Plus:
	case Qt::Key_Equal:
		m_options.zoom = std::clamp(m_options.zoom * 1.12, 0.25, 6.0);
		Q_EMIT cameraChanged();
		requestRender(true);
		break;
	case Qt::Key_Minus:
		m_options.zoom = std::clamp(m_options.zoom / 1.12, 0.25, 6.0);
		Q_EMIT cameraChanged();
		requestRender(true);
		break;
	case Qt::Key_Home:
	case Qt::Key_0:
		resetCamera();
		break;
	case Qt::Key_Comma:
		stepTime(-0.1);
		break;
	case Qt::Key_Period:
		stepTime(0.1);
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	event->accept();
}

void MaterialPreviewView::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LanguageChange) {
		if (event->type() == QEvent::LanguageChange) {
			setAccessibleName(tr("Material preview"));
			refreshAccessibleDescription();
		}
		update();
	}
}

void MaterialPreviewView::refreshAccessibleDescription()
{
	QString description;
	if (!m_hasMaterial) {
		description = m_message;
	} else {
		const QString shape = materialPreviewShapeDisplayName(m_options.shape);
		description = m_playing ? tr("%1 (%2) on a %3 shape, playing.").arg(m_definition.name, materialEngineDisplayName(m_definition.engine), shape)
								: tr("%1 (%2) on a %3 shape, paused at %4 seconds.")
									  .arg(m_definition.name, materialEngineDisplayName(m_definition.engine), shape)
									  .arg(m_time, 0, 'f', 1);
	}
	if (accessibleDescription() != description) {
		setAccessibleDescription(description);
		QAccessibleEvent event(this, QAccessible::DescriptionChanged);
		QAccessible::updateAccessibility(&event);
	}
}

} // namespace vibestudio
