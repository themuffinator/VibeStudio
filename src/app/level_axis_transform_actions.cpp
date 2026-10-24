// World-axis Flip X/Y/Z and Rotate X/Y/Z, the view-relative Roll, Yaw (Rotate
// 90° Left and Right) and Pitch turns and Flip Horizontal and Vertical, and the
// quick-turn and flip pivots they share (core/level_quick_transform.h). Every
// command sends the same Mirror and Rotate placement requests, so component
// mode, Texture Lock, Repeat and undo agree. A view-relative command takes its
// axis from the plan view or camera it is used from; while the Sweep tool is
// on, a turn rotates the sweep destination by the rotation step instead.
// Workflow reference: NRC radiant/select.cpp Selection_Flipx to
// Selection_Rotatez and radiant/selection.cpp rotateSelected/scaleSelected
// (snapOrigin) at 449778b3 (GPL-2.0-or-later); TB MapViewBase::rotate,
// rotationAxis and flip (grid and half-grid reference points) and
// MapView2D/MapView3D::flipAxis at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_sweep_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_brush.h"
#include "core/level_nudge.h"
#include "core/level_placement.h"
#include "core/level_quick_transform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QStatusBar>

namespace vibestudio {
namespace {

MapRenderProjection viewProjection(MapViewportProjection projection)
{
	switch (projection) {
	case MapViewportProjection::FrontXZ:
		return MapRenderProjection::FrontXZ;
	case MapViewportProjection::SideZY:
		return MapRenderProjection::SideZY;
	case MapViewportProjection::TopXY:
		break;
	}
	return MapRenderProjection::TopXY;
}

LevelMapVec3 worldVector(const ModelVec3& value)
{
	return {value.x, value.y, value.z, true};
}

bool planarOnly(const LevelViewTransform& transform)
{
	// Roll and Pitch always turn about a horizontal axis from a top view or
	// a level camera, so Doom geometry, which turns about Z only, never takes them.
	return transform.kind == LevelViewTransformKind::Roll || transform.kind == LevelViewTransformKind::Pitch;
}

} // namespace

void ApplicationShell::registerLevelAxisTransformCommands()
{
	struct Entry {
		QString label;
		QString tip;
		QString icon;
	};
	// Same order as levelAxisTransforms().
	const QVector<Entry> entries {
		{tr("Flip X"), tr("Mirror the selection along the world X axis, whatever view is active."), QStringLiteral("compare")},
		{tr("Flip Y"), tr("Mirror the selection along the world Y axis, whatever view is active."), QStringLiteral("compare")},
		{tr("Flip Z"), tr("Mirror the selection along the world Z axis, whatever view is active."), QStringLiteral("compare")},
		{tr("Rotate X"), tr("Turn the selection 90° clockwise about the world X axis, as the side view shows it, whatever view is active."),
			QStringLiteral("rotate")},
		{tr("Rotate Y"), tr("Turn the selection 90° clockwise about the world Y axis, as the front view shows it, whatever view is active."),
			QStringLiteral("rotate")},
		{tr("Rotate Z"), tr("Turn the selection 90° clockwise about the world Z axis, as the top view shows it, whatever view is active."),
			QStringLiteral("rotate")},
	};
	const QVector<LevelAxisTransform>& transforms = levelAxisTransforms();
	for (int index = 0; index < transforms.size() && index < entries.size(); ++index) {
		StudioCommandRegistration command;
		command.commandId = transforms.at(index).commandId;
		command.group = StudioCommandGroup::Edit;
		command.label = entries.at(index).label;
		command.statusTip = entries.at(index).tip;
		command.iconName = entries.at(index).icon;
		command.menuSection = tr("Transform");
		command.handler = [this, id = transforms.at(index).commandId]() {
			setMode(StudioMode::Levels);
			applyLevelAxisTransformFromUi(id);
		};
		m_commands->registerCommand(command);
	}

	// TrenchBroom's Roll and Pitch. Its Yaw is Rotate 90° Right and Left, and
	// its flips are Flip Horizontal and Vertical, which the shell registers
	// with its other transforms and which resolve their axes here too.
	const QVector<QPair<QString, Entry>> view {
		{QStringLiteral("map.rollClockwise"),
			{tr("Roll Clockwise"),
				tr("Turn the selection 90° clockwise about the view's forward axis: up the plan view, or the horizontal axis nearest the camera's view."),
				QStringLiteral("rotate")}},
		{QStringLiteral("map.rollCounterclockwise"),
			{tr("Roll Counter-clockwise"),
				tr("Turn the selection 90° counter-clockwise about the view's forward axis: up the plan view, or the horizontal axis nearest the camera's view."),
				QStringLiteral("rotate")}},
		{QStringLiteral("map.pitchClockwise"),
			{tr("Pitch Clockwise"),
				tr("Turn the selection 90° clockwise about the view's right axis: across the plan view, or the camera's right axis snapped to a world axis."),
				QStringLiteral("rotate")}},
		{QStringLiteral("map.pitchCounterclockwise"),
			{tr("Pitch Counter-clockwise"),
				tr("Turn the selection 90° counter-clockwise about the view's right axis: across the plan view, or the camera's right axis snapped to a world axis."),
				QStringLiteral("rotate")}},
	};
	for (const auto& entry : view) {
		StudioCommandRegistration command;
		command.commandId = entry.first;
		command.group = StudioCommandGroup::Edit;
		command.label = entry.second.label;
		command.statusTip = entry.second.tip;
		command.iconName = entry.second.icon;
		command.menuSection = tr("Transform");
		command.handler = [this, id = entry.first]() {
			setMode(StudioMode::Levels);
			applyLevelViewTransformFromUi(id);
		};
		m_commands->registerCommand(command);
	}
}

void ApplicationShell::refreshLevelAxisTransformCommands()
{
	if (!m_commands) {
		return;
	}
	const bool hasSelection = !m_levelMapDocument.selection.isEmpty();
	for (const LevelAxisTransform& transform : levelAxisTransforms()) {
		m_commands->setEnabled(transform.commandId, hasSelection && levelAxisTransformSupported(m_levelMapDocument, transform));
	}
	// The view-relative commands: turns act on the sweep destination while
	// the Sweep tool is on, and flips wait for it to close, as TrenchBroom's
	// do while a modal tool is active.
	const bool open = m_levelMapDocument.format != LevelMapFormat::Unknown && m_levelMapViewport;
	const bool doom = m_levelMapDocument.format == LevelMapFormat::DoomWad;
	const bool sweep = open && m_levelSweepGizmo;
	for (const LevelViewTransform& transform : levelViewTransforms()) {
		const bool target = levelViewTransformMirrors(transform) ? hasSelection && !sweep : sweep || hasSelection;
		m_commands->setEnabled(transform.commandId, open && target && !(doom && planarOnly(transform)));
	}
}

LevelMapVec3 ApplicationShell::levelQuickTurnPivotFromUi() const
{
	// Invalid keeps the transform's own default: the exact centre of what it
	// moves, or a custom component pivot, which always wins as in NRC.
	if (!m_levelControls.quickTurnSnapsPivot || !m_levelMapSnap || !m_levelMapSnap->isChecked() || !m_levelMapGrid) {
		return {};
	}
	const double grid = levelMapGridUnits();
	if (levelComponentsActive()) {
		if (m_levelComponentPivot.valid) {
			return {};
		}
		QString problem;
		const auto sets = levelComponentTransformSets(&problem);
		LevelMapVec3 mins;
		LevelMapVec3 maxs;
		if (!problem.isEmpty() || !levelMapComponentBounds(m_levelMapDocument, sets, &mins, &maxs)) {
			return {};
		}
		return levelQuickTurnPivot(mins, maxs, grid);
	}
	LevelMapVec3 pivot;
	return levelMapQuickTurnPivot(m_levelMapDocument, grid, &pivot) ? pivot : LevelMapVec3 {};
}

LevelMapVec3 ApplicationShell::levelFlipPivotFromUi() const
{
	// The same bounds as a quick turn's pivot, rounded to half the grid under
	// TrenchBroom's policy; invalid keeps the exact centre or a custom
	// component pivot.
	if (!m_levelControls.flipSnapsPivot || !m_levelMapSnap || !m_levelMapSnap->isChecked() || !m_levelMapGrid) {
		return {};
	}
	const double grid = levelMapGridUnits();
	if (levelComponentsActive()) {
		if (m_levelComponentPivot.valid) {
			return {};
		}
		QString problem;
		const auto sets = levelComponentTransformSets(&problem);
		LevelMapVec3 mins;
		LevelMapVec3 maxs;
		if (!problem.isEmpty() || !levelMapComponentBounds(m_levelMapDocument, sets, &mins, &maxs)) {
			return {};
		}
		return levelFlipPivot(mins, maxs, grid);
	}
	LevelMapVec3 pivot;
	return levelMapFlipPivot(m_levelMapDocument, grid, &pivot) ? pivot : LevelMapVec3 {};
}

bool ApplicationShell::turnLevelMapSelectionQuarter(int axis, int quarterTurns, QString* error)
{
	LevelMapRotationRequest rotation;
	rotation.axis = axis;
	rotation.degrees = 90.0 * quarterTurns;
	rotation.pivot = levelQuickTurnPivotFromUi();
	rotation.textureLock = m_settings.levelTextureLock();
	rotation.allowValve220 = m_settings.levelAllowValve220();
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::Rotate;
	request.rotation = rotation;
	return runLevelPlacementFromUi(request, error);
}

void ApplicationShell::applyLevelAxisTransformFromUi(const QString& commandId)
{
	LevelAxisTransform transform;
	if (!levelAxisTransformForCommand(commandId, &transform)) {
		return;
	}
	if (m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(transform.mirror ? tr("Select map objects before flipping them.") : tr("Select map objects before turning them."));
		return;
	}
	if (!levelAxisTransformSupported(m_levelMapDocument, transform)) {
		statusBar()->showMessage(tr("Doom maps flip along X or Y and turn about Z only."));
		return;
	}
	const QString axis(QLatin1Char("XYZ"[transform.axis]));
	QString error;
	if (transform.mirror) {
		LevelPlacementRequest request;
		request.operation = LevelPlacementOperation::Mirror;
		request.axis = transform.axis;
		request.mirrorPivot = levelFlipPivotFromUi();
		request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
		if (!runLevelPlacementFromUi(request, &error)) {
			statusBar()->showMessage(tr("Could not flip the selection: %1").arg(error));
			return;
		}
		recordActivity(tr("Level map selection flipped"), tr("along world %1").arg(axis), QStringLiteral("level-map"),
			OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Flipped the selection along the world %1 axis.").arg(axis));
		return;
	}
	if (!turnLevelMapSelectionQuarter(transform.axis, transform.quarterTurns, &error)) {
		statusBar()->showMessage(tr("Could not turn the selection: %1").arg(error));
		return;
	}
	const QString degrees = QString::number(90 * transform.quarterTurns);
	recordActivity(tr("Level map selection turned"), tr("%1° about world %2").arg(degrees, axis), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Turned the selection %1° about the world %2 axis.").arg(degrees, axis));
}

void ApplicationShell::applyLevelViewTransformFromUi(const QString& commandId, QWidget* source)
{
	LevelViewTransform transform;
	if (!levelViewTransformForCommand(commandId, &transform)) {
		return;
	}
	const bool mirror = levelViewTransformMirrors(transform);
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || !m_levelMapViewport) {
		statusBar()->showMessage(mirror ? tr("Select map objects before flipping them.") : tr("Select map objects before turning them."));
		return;
	}

	// The frame: the plan view the command came from or was used last, or the
	// camera where it is the view in use, as TrenchBroom's MapView3D. Radiant-
	// family profiles keep the last plan view's axes, as their nudges do.
	QWidget* from = source ? source : m_levelLastFocusedView;
	MapViewport* plan = nullptr;
	for (auto* view : std::as_const(m_levelPlanViews)) {
		if (view && view == from) {
			plan = view;
		}
	}
	if (plan) {
		activateLevelPlanViewport(plan);
	}
	const bool camera = !plan && m_levelMap3D && levelMap3DShowing() && !levelNudgeCameraFollowsPlan(m_levelControls)
		&& (from == m_levelMap3D || !levelMap2DShowing());
	if (!plan) {
		plan = m_levelMapViewport;
	}
	LevelAxisTransform resolved;
	bool framed = false;
	if (camera) {
		ModelVec3 right;
		ModelVec3 up;
		m_levelMap3D->viewAxes(&right, &up);
		framed = levelCameraViewTransform(worldVector(m_levelMap3D->viewForward()), worldVector(right), worldVector(up), transform, &resolved);
	} else {
		framed = levelPlanViewTransform(viewProjection(plan->projection()), transform, &resolved);
	}
	if (!framed) {
		statusBar()->showMessage(tr("The view has no axis to turn or flip about."));
		return;
	}
	const QString axis(QLatin1Char("XYZ"[resolved.axis]));

	// TrenchBroom's order: the Sweep tool's destination before the selection.
	if (m_levelSweepGizmo) {
		if (mirror) {
			statusBar()->showMessage(tr("Create or cancel the sweep before flipping the selection."));
			return;
		}
		const double step = m_levelSweepGizmo->angleStep() > 0 ? m_levelSweepGizmo->angleStep() : 15.0;
		QString error;
		if (m_levelSweepGizmo->rotateDestination(levelAxisTransformTurnVector(resolved), step, &error)) {
			const QString degrees = QString::number(resolved.quarterTurns < 0 ? -step : step);
			statusBar()->showMessage(tr("Turned the sweep destination %1° about the world %2 axis.").arg(degrees, axis));
		} else {
			statusBar()->showMessage(error);
		}
		return;
	}
	if (m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(mirror ? tr("Select map objects before flipping them.") : tr("Select map objects before turning them."));
		return;
	}
	if (!levelAxisTransformSupported(m_levelMapDocument, resolved)) {
		statusBar()->showMessage(tr("Doom maps flip along X or Y and turn about Z only."));
		return;
	}
	QString error;
	if (mirror) {
		LevelPlacementRequest request;
		request.operation = LevelPlacementOperation::Mirror;
		request.axis = resolved.axis;
		request.mirrorPivot = levelFlipPivotFromUi();
		request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
		if (!runLevelPlacementFromUi(request, &error)) {
			statusBar()->showMessage(tr("Could not flip the selection: %1").arg(error));
			return;
		}
		const bool horizontal = transform.kind == LevelViewTransformKind::FlipHorizontal;
		recordActivity(tr("Level map selection flipped"), horizontal ? tr("horizontal") : tr("vertical"), QStringLiteral("level-map"),
			OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(horizontal ? tr("Flipped the selection horizontally.") : tr("Flipped the selection vertically."));
		return;
	}
	// The pivot follows the profile (levelQuickTurnPivotFromUi()).
	if (!turnLevelMapSelectionQuarter(resolved.axis, resolved.quarterTurns, &error)) {
		statusBar()->showMessage(tr("Could not turn the selection: %1").arg(error));
		return;
	}
	const QString degrees = QString::number(90 * resolved.quarterTurns);
	QString detail = tr("%1° about world %2").arg(degrees, axis);
	QString message;
	switch (transform.kind) {
	case LevelViewTransformKind::Yaw:
		detail = transform.clockwise ? tr("90° right") : tr("90° left");
		message = transform.clockwise ? tr("Turned the selection 90° right.") : tr("Turned the selection 90° left.");
		break;
	case LevelViewTransformKind::Roll:
		message = transform.clockwise ? tr("Rolled the selection 90° clockwise about the world %1 axis.").arg(axis)
			: tr("Rolled the selection 90° counter-clockwise about the world %1 axis.").arg(axis);
		break;
	case LevelViewTransformKind::Pitch:
		message = transform.clockwise ? tr("Pitched the selection 90° clockwise about the world %1 axis.").arg(axis)
			: tr("Pitched the selection 90° counter-clockwise about the world %1 axis.").arg(axis);
		break;
	case LevelViewTransformKind::FlipHorizontal:
	case LevelViewTransformKind::FlipVertical:
		break;
	}
	recordActivity(tr("Level map selection turned"), detail, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(message);
}

} // namespace vibestudio
