// View > Camera: Level Camera, Focus Camera on Selection and Reset Camera
// Zoom, and the held free-focus key. They move only the level camera: the
// plan views, the map and its undo history stay as they are, so scene locks
// do not apply, while hidden objects never count towards the focused box and
// the selection already respects the open group.
//
// Workflow reference: NRC camwindow.cpp GlobalCamera_ResetAngles,
// GlobalCamera_FocusOnSelected and CameraFreeFocus at 449778b3
// (GPL-2.0-or-later) / TB MapViewBase::resetCameraZoom at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_camera_focus.h"
#include "core/level_camera_keys.h"
#include "core/level_scene.h"

#include <QAction>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace vibestudio {

namespace {

ModelVec3 modelPoint(const LevelMapVec3& point)
{
	ModelVec3 result;
	result.x = static_cast<float>(point.x);
	result.y = static_cast<float>(point.y);
	result.z = static_cast<float>(point.z);
	return result;
}

bool samePoint(const ModelVec3& a, const ModelVec3& b)
{
	const auto matches = [](float x, float y) { return std::abs(x - y) <= 1.0e-3f + 1.0e-6f * std::max(std::abs(x), std::abs(y)); };
	return matches(a.x, b.x) && matches(a.y, b.y) && matches(a.z, b.z);
}

// The focus request for the camera as it is now: its heading, pitch, field
// of view and the shape of its view.
LevelCameraFocusRequest focusRequestFor(const ModelViewport& camera, const LevelMapVec3& mins, const LevelMapVec3& maxs, int offset)
{
	LevelCameraFocusRequest request;
	request.mins = mins;
	request.maxs = maxs;
	request.yawDegrees = camera.cameraYaw();
	request.pitchDegrees = camera.cameraPitch();
	request.fieldOfViewDegrees = camera.fieldOfView();
	request.aspect = camera.width() > 0 && camera.height() > 0 ? static_cast<double>(camera.width()) / camera.height() : 4.0 / 3.0;
	request.offsetSteps = offset;
	return request;
}

} // namespace

void ApplicationShell::registerLevelCameraFocusCommands()
{
	if (!m_commands) {
		return;
	}
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::View;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.menuSection = tr("Camera");
		command.handler = std::move(handler);
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.levelCamera"), tr("Level Camera"),
		tr("Level the 3D camera where it stands: no pitch, and the heading turned to the nearest 22.5 degrees. The 2D views and the map are unchanged."),
		QStringLiteral("frame"), [this]() { levelLevelMapCamera(); });
	add(QStringLiteral("map.focusCameraOnSelection"), tr("Focus Camera on Selection"),
		tr("Move the 3D camera along its view until the selection fits, at least 64 units from its centre. The 2D views stay put; with nothing selected the last selection is used."),
		QStringLiteral("crosshair"), [this]() { focusLevelCameraOnSelection(); });
	add(QStringLiteral("map.resetCameraZoom"), tr("Reset Camera Zoom"),
		tr("Return the 3D camera's field of view to the editor profile's, undoing field-of-view zoom. The camera keeps its place."),
		QStringLiteral("fit"), [this]() { resetLevelCameraZoom(); });
}

bool ApplicationShell::levelCameraFocusReady() const
{
	return m_levelMapDocument.format != LevelMapFormat::Unknown && m_levelMap3D && m_levelMap3D->isEnabled() && m_levelMap3D->hasMesh()
		&& m_levelMap3DSource == QString::number(m_levelMapLoadSerial);
}

void ApplicationShell::refreshLevelCameraFocusCommands()
{
	if (!m_commands) {
		return;
	}
	// The last selection of this map load is what an empty selection focuses
	// on, as Radiant's work zone outlives the selection it came from.
	if (!m_levelMapDocument.selection.isEmpty()) {
		m_levelCameraFocus.lastSelection = m_levelMapDocument.selection;
		m_levelCameraFocus.lastSelectionSerial = m_levelMapLoadSerial;
	} else if (m_levelCameraFocus.lastSelectionSerial != m_levelMapLoadSerial) {
		m_levelCameraFocus.lastSelection.clear();
	}
	const bool ready = levelCameraFocusReady();
	const bool something = !m_levelMapDocument.selection.isEmpty() || !m_levelCameraFocus.lastSelection.isEmpty();
	m_commands->setEnabled(QStringLiteral("map.levelCamera"), ready);
	m_commands->setEnabled(QStringLiteral("map.focusCameraOnSelection"), ready && something);
	m_commands->setEnabled(QStringLiteral("map.resetCameraZoom"), ready && m_levelMap3D->isPerspective());
	if (!ready) {
		endLevelCameraFreeFocus();
	}
}

bool ApplicationShell::levelCameraFocusBounds(LevelMapVec3* mins, LevelMapVec3* maxs, bool* remembered) const
{
	if (remembered) {
		*remembered = false;
	}
	if (levelCameraFocusObjectsBounds(m_levelMapDocument, m_levelMapDocument.selection, mins, maxs)) {
		return true;
	}
	if (m_levelCameraFocus.lastSelectionSerial != m_levelMapLoadSerial) {
		return false;
	}
	// Only what the open group still lets the user pick counts.
	const LevelSceneSelectionContext context = levelSceneSelectionContext(m_levelMapDocument);
	QVector<LevelMapSelectionRef> last;
	for (const LevelMapSelectionRef& ref : m_levelCameraFocus.lastSelection) {
		if (context.allowedObjects.contains(levelMapSelectionRefId(ref))) {
			last.push_back(ref);
		}
	}
	if (!levelCameraFocusObjectsBounds(m_levelMapDocument, last, mins, maxs)) {
		return false;
	}
	if (remembered) {
		*remembered = true;
	}
	return true;
}

void ApplicationShell::levelLevelMapCamera()
{
	if (!levelCameraFocusReady()) {
		statusBar()->showMessage(tr("Show the map in the 3D view to level its camera."), 4000);
		return;
	}
	if (m_levelMap3D->isPerspective()) {
		const double yaw = levelCameraLevelledYaw(m_levelMap3D->cameraYaw());
		m_levelMap3D->setCameraView(m_levelMap3D->cameraPosition(), yaw, 0.0);
		statusBar()->showMessage(tr("Camera levelled where it stands: heading %1 degrees, no pitch.").arg(yaw, 0, 'g', 6), 4000);
		return;
	}
	// The orbiting view levels about its own centre.
	const double yaw = levelCameraLevelledYaw(m_levelMap3D->yaw());
	m_levelMap3D->setOrbit(yaw, 0.0);
	statusBar()->showMessage(tr("Orbit view levelled: heading %1 degrees, no pitch.").arg(yaw, 0, 'g', 6), 4000);
}

void ApplicationShell::focusLevelCameraOnSelection()
{
	if (!levelCameraFocusReady()) {
		statusBar()->showMessage(tr("Show the map in the 3D view to focus its camera."), 4000);
		return;
	}
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	bool remembered = false;
	if (!levelCameraFocusBounds(&mins, &maxs, &remembered)) {
		statusBar()->showMessage(tr("Select something visible to focus the camera on."), 4000);
		return;
	}
	if (!m_levelMap3D->isPerspective()) {
		// The orbiting view turns about the box and fits it; the plans stay.
		m_levelMap3D->frameBounds(modelPoint(mins), modelPoint(maxs));
		statusBar()->showMessage(remembered ? tr("Orbit view framed on the last selection; the 2D views are unchanged.")
										  : tr("Orbit view framed on the selection; the 2D views are unchanged."), 4000);
		return;
	}
	const LevelCameraFocus focus = levelCameraFocusPosition(focusRequestFor(*m_levelMap3D, mins, maxs, 0));
	if (!focus.valid) {
		statusBar()->showMessage(focus.error, 5000);
		return;
	}
	m_levelMap3D->setCameraView(modelPoint(focus.position), m_levelMap3D->cameraYaw(), m_levelMap3D->cameraPitch());
	const QString distance = QString::number(focus.distance, 'f', 0);
	statusBar()->showMessage(remembered ? tr("Camera focused on the last selection, %1 units from its centre; the 2D views are unchanged.").arg(distance)
									  : tr("Camera focused on the selection, %1 units from its centre; the 2D views are unchanged.").arg(distance), 4000);
}

void ApplicationShell::resetLevelCameraZoom()
{
	if (!levelCameraFocusReady() || !m_levelMap3D->isPerspective()) {
		statusBar()->showMessage(tr("Reset Camera Zoom needs the first-person 3D camera."), 4000);
		return;
	}
	const double profile = std::clamp(m_levelControls.camera.fieldOfViewDegrees, 15.0, 150.0);
	const QString degrees = QString::number(profile, 'g', 4);
	if (std::abs(m_levelMap3D->fieldOfView() - profile) < 1.0e-6) {
		statusBar()->showMessage(tr("The camera already uses the profile's %1 degree field of view.").arg(degrees), 4000);
		return;
	}
	CameraViewState state = m_levelMap3D->navigationState();
	state.fieldOfView = profile;
	// Restoring the view ends mouse look; it resumes as it was, so a key press
	// mid-flight only changes the field of view.
	const bool looking = m_levelMap3D->isLooking();
	const bool heldLook = m_levelMap3D->isTemporarilyLooking();
	if (!m_levelMap3D->restoreNavigationState(state)) {
		statusBar()->showMessage(tr("The camera's field of view could not be reset."), 4000);
		return;
	}
	if (heldLook) {
		m_levelMap3D->setTemporaryLooking(true);
	} else if (looking) {
		m_levelMap3D->setLooking(true);
	}
	statusBar()->showMessage(tr("Camera zoom reset to the profile's %1 degree field of view.").arg(degrees), 4000);
}

bool ApplicationShell::handleLevelCameraFocusEvent(QObject* watched, QEvent* event)
{
	if (!m_levelMap3D || watched != m_levelMap3D) {
		return false;
	}
	switch (event->type()) {
	case QEvent::ShortcutOverride:
	case QEvent::KeyPress: {
		auto* key = static_cast<QKeyEvent*>(event);
		if (!cameraFocusKeyMatches(m_levelMap3D->cameraControls(), key->key(), key->modifiers())) {
			return false;
		}
		// The key acts only during mouse look: a look button held down, or
		// free look toggled on. Otherwise it keeps its usual meaning, so Tab
		// still moves keyboard focus out of the camera.
		const CameraViewControls& controls = m_levelMap3D->cameraControls();
		const bool heldLook = controls.lookButton != Qt::NoButton && !controls.lookClickToggles
			&& QGuiApplication::mouseButtons().testFlag(controls.lookButton);
		if (!m_levelCameraFocus.held && !m_levelMap3D->isLooking() && !heldLook) {
			return false;
		}
		event->accept();
		if (event->type() == QEvent::KeyPress && !key->isAutoRepeat() && !m_levelCameraFocus.held) {
			beginLevelCameraFreeFocus(key->key());
		}
		return true;
	}
	case QEvent::KeyRelease: {
		auto* key = static_cast<QKeyEvent*>(event);
		if (!m_levelCameraFocus.held || key->key() != m_levelCameraFocus.key) {
			return false;
		}
		if (!key->isAutoRepeat()) {
			endLevelCameraFreeFocus();
		}
		return true;
	}
	case QEvent::Wheel: {
		if (!m_levelCameraFocus.held) {
			return false;
		}
		// While the key is held, each notch rolled away from the user moves the
		// camera nearer and each one rolled back moves it farther.
		auto* wheel = static_cast<QWheelEvent*>(event);
		const int delta = wheel->angleDelta().y() != 0 ? wheel->angleDelta().y() : wheel->angleDelta().x();
		m_levelCameraFocus.wheel += static_cast<double>(delta) / 120.0;
		const int steps = static_cast<int>(m_levelCameraFocus.wheel);
		if (steps != 0) {
			m_levelCameraFocus.wheel -= steps;
			m_levelCameraFocus.offset = std::clamp(m_levelCameraFocus.offset + steps, -kLevelCameraFocusOffsetLimit, kLevelCameraFocusOffsetLimit);
			applyLevelCameraFreeFocus();
		}
		event->accept();
		return true;
	}
	case QEvent::FocusOut:
	case QEvent::Hide:
		endLevelCameraFreeFocus();
		return false;
	default:
		return false;
	}
}

void ApplicationShell::beginLevelCameraFreeFocus(int key)
{
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	bool remembered = false;
	if (!levelCameraFocusReady() || !m_levelMap3D->isPerspective() || !levelCameraFocusBounds(&mins, &maxs, &remembered)) {
		statusBar()->showMessage(tr("Select something visible for the camera to keep in view."), 4000);
		return;
	}
	m_levelCameraFocus.held = true;
	m_levelCameraFocus.key = key;
	m_levelCameraFocus.offset = 0;
	m_levelCameraFocus.wheel = 0.0;
	m_levelCameraFocus.mins = mins;
	m_levelCameraFocus.maxs = maxs;
	// Every turn of mouse look, fly step or other camera move lands back on
	// the line through the box's centre along the view.
	m_levelCameraFocus.connections.push_back(connect(m_levelMap3D, &ModelViewport::viewChanged, this, [this]() { applyLevelCameraFreeFocus(); }));
	m_levelCameraFocus.connections.push_back(connect(m_levelMap3D, &ModelViewport::lookingChanged, this, [this](bool looking) {
		if (!looking) {
			endLevelCameraFreeFocus();
		}
	}));
	applyLevelCameraFreeFocus();
	const QString name = QKeySequence(QKeyCombination(static_cast<Qt::Key>(key))).toString(QKeySequence::NativeText);
	statusBar()->showMessage(remembered ? tr("Keeping the last selection in view while %1 is held; the wheel moves nearer or farther.").arg(name)
									  : tr("Keeping the selection in view while %1 is held; the wheel moves nearer or farther.").arg(name), 4000);
}

void ApplicationShell::applyLevelCameraFreeFocus()
{
	if (!m_levelCameraFocus.held || m_levelCameraFocus.applying || !m_levelMap3D || !m_levelMap3D->isPerspective()) {
		return;
	}
	const LevelCameraFocus focus = levelCameraFocusPosition(focusRequestFor(*m_levelMap3D, m_levelCameraFocus.mins, m_levelCameraFocus.maxs,
		m_levelCameraFocus.offset));
	const ModelVec3 position = modelPoint(focus.position);
	if (!focus.valid || samePoint(position, m_levelMap3D->cameraPosition())) {
		return;
	}
	const QScopedValueRollback<bool> applying(m_levelCameraFocus.applying, true);
	m_levelMap3D->setCameraView(position, m_levelMap3D->cameraYaw(), m_levelMap3D->cameraPitch());
}

void ApplicationShell::endLevelCameraFreeFocus()
{
	if (!m_levelCameraFocus.held) {
		return;
	}
	m_levelCameraFocus.held = false;
	for (const QMetaObject::Connection& connection : std::as_const(m_levelCameraFocus.connections)) {
		disconnect(connection);
	}
	m_levelCameraFocus.connections.clear();
	m_levelCameraFocus.key = 0;
	m_levelCameraFocus.offset = 0;
	m_levelCameraFocus.wheel = 0.0;
}

} // namespace vibestudio
