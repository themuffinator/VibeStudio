#include "app/model_viewport.h"
#include <QAccessible>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace vibestudio {

void ModelViewport::stepCameraDrive(const CameraKeyMotion& motion)
{
	if (!m_perspective || !std::isfinite(motion.forward) || !std::isfinite(motion.right)
		|| !std::isfinite(motion.up) || !std::isfinite(motion.turn) || !std::isfinite(motion.pitch)) { return; }
	// Q3Radiant's fixed movement/turn defaults; see the audited profile credits.
	moveCamera(std::clamp(motion.forward, -1.0, 1.0) * 32.0, std::clamp(motion.right, -1.0, 1.0) * 32.0,
		std::clamp(motion.up, -1.0, 1.0) * 32.0, true);
	const double pitch = std::clamp(m_lookPitch + std::clamp(motion.pitch, -1.0, 1.0) * 22.5, -85.0, 85.0);
	turnCamera(std::clamp(motion.turn, -1.0, 1.0) * 22.5, pitch - m_lookPitch);
}

bool ModelViewport::beginPointerDrive(const QPointF& point)
{
	if (!m_perspective || !m_hasMesh || !isEnabled() || !isVisible() || m_controls.driveButton == Qt::NoButton
		|| m_looking || isDrawingBrush() || isResizingSelection() || isMovingSelection() || m_editMoveActive
		|| m_surfaceStrokeActive || m_surfaceToolPressed || !std::isfinite(point.x()) || !std::isfinite(point.y())) { return false; }
	if (!m_driveTimer) {
		m_driveTimer = new QTimer(this);
		m_driveTimer->setTimerType(Qt::PreciseTimer); m_driveTimer->setInterval(16);
		connect(m_driveTimer, &QTimer::timeout, this, [this] { advancePointerDrive(m_driveClock.restart() / 1000.0); });
	}
	m_pointerDrivePoint = point;
	if (m_pointerDriving) { return true; }
	m_pointerDriving = true;
	rebuildKeyMotions();
	m_driveClock.start(); m_driveTimer->start();
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent change(this, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&change);
	return true;
}

void ModelViewport::updatePointerDrive(const QPointF& point)
{
	if (!std::isfinite(point.x()) || !std::isfinite(point.y())) { endPointerDrive(); return; }
	if (m_pointerDriving) { m_pointerDrivePoint = point; }
}

void ModelViewport::advancePointerDrive(double seconds)
{
	if (!m_pointerDriving || !std::isfinite(seconds) || seconds <= 0.0 || width() <= 0 || height() <= 0) { return; }
	if (!isEnabled() || !isVisible() || !m_perspective || !m_hasMesh) { endPointerDrive(); return; }
	const auto motion = cameraPointerDriveMotion({m_pointerDrivePoint.x() * 2.0 / width() - 1.0, m_pointerDrivePoint.y() * 2.0 / height() - 1.0});
	const double elapsed = std::min(seconds, 0.1);
	// A delayed frame must never cause a large jump. Repaint is coalesced by
	// the viewport's existing renderer; the timer does no geometry work.
	if (motion.forward != 0.0) { moveCamera(motion.forward * elapsed, 0.0, 0.0, true); }
	if (motion.turn != 0.0) { turnCamera(motion.turn * elapsed, 0.0); }
}

void ModelViewport::endPointerDrive()
{
	if (!m_pointerDriving) { return; }
	m_pointerDriving = false;
	if (m_driveTimer) { m_driveTimer->stop(); }
	if (m_dragAction == DragAction::Drive) {
		m_pressButton = Qt::NoButton; m_dragAction = DragAction::None; m_dragStarted = false;
	}
	rebuildKeyMotions();
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent change(this, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&change);
}

bool ModelViewport::isPointerDriving() const { return m_pointerDriving; }

} // namespace vibestudio
