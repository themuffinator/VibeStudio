// Camera-relative texture moves and flips (core/level_camera_texture.h):
// Move Textures Up, Down, Left and Right by the grid size, their Coarse
// (twice the grid) and Fine (one texel) forms, and Flip Textures Horizontally
// and Vertically. Each takes the 3D camera's current screen-right and
// screen-up directions and goes through the shared surface queue
// (queueLevelSurfaceEdit), so a burst is one undo step on the surface worker,
// with its size lookups, cancellation, scene locks and linked groups. The
// targets are the picked faces, as TrenchBroom acts on its face selection,
// or the Surfaces tab's target when no face is picked. Rotate Textures
// Clockwise and Counter-clockwise (15°, Coarse 90°, Fine 1°) share the table,
// targets and keys' layer, but need no camera: each face turns as seen from
// its own front (ClockwiseRotate in core/level_surface.h).
//
// The keys belong to the editor profile (TrenchBroom: the arrows, Shift and
// Ctrl with them, Ctrl+F and Ctrl+Alt+F, and Page Up and Page Down with
// Shift and Ctrl for the turns) and act only in the camera. There
// they share the nudge, Duplicate and Move and flip keys in a claim layer of
// their own (StudioCommandRegistry::setShortcutScopeClaims), and
// handleLevelNudgeKey gives them the key while faces are picked outside
// component mode, as TrenchBroom's face-selection context does.
//
// Workflow reference: TrenchBroom lib/TbUiLib/src/ActionManager.cpp "Move
// textures ...", "Rotate textures ..." and "Flip textures ..." actions and
// MapViewBase::moveUV, rotateUV and flipUV at 4c756be1 (GPL-3.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_camera_texture.h"

#include <QAction>
#include <QStatusBar>

namespace vibestudio {
namespace {

LevelMapVec3 mapVector(const ModelVec3& value)
{
	return {value.x, value.y, value.z, true};
}

// Arrows and paging keys can carry the keypad flag (macOS sets it on every
// arrow); they match their plain binding, as the nudge keys do.
QKeyCombination plainKey(QKeyCombination combination)
{
	const int key = combination.key();
	if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
		return QKeyCombination(combination.keyboardModifiers() & ~Qt::KeypadModifier, combination.key());
	}
	return combination;
}

} // namespace

void ApplicationShell::registerLevelCameraTextureCommands()
{
	struct Entry {
		QString label;
		QString tip;
		QString icon;
	};
	// Same order as levelCameraTextureCommands().
	const QVector<Entry> entries {
		{tr("Move Textures Up"), tr("Move the textures of the picked faces, or of the Surfaces target, one grid size up the 3D camera's view."),
			QStringLiteral("chevron-up")},
		{tr("Move Textures Down"), tr("Move the textures of the picked faces, or of the Surfaces target, one grid size down the 3D camera's view."),
			QStringLiteral("chevron-down")},
		{tr("Move Textures Left"), tr("Move the textures of the picked faces, or of the Surfaces target, one grid size left in the 3D camera's view."),
			QStringLiteral("chevron-left")},
		{tr("Move Textures Right"), tr("Move the textures of the picked faces, or of the Surfaces target, one grid size right in the 3D camera's view."),
			QStringLiteral("chevron-right")},
		{tr("Move Textures Up (Coarse)"), tr("Move the textures twice the grid size up the 3D camera's view."), QStringLiteral("chevron-up")},
		{tr("Move Textures Down (Coarse)"), tr("Move the textures twice the grid size down the 3D camera's view."), QStringLiteral("chevron-down")},
		{tr("Move Textures Left (Coarse)"), tr("Move the textures twice the grid size left in the 3D camera's view."), QStringLiteral("chevron-left")},
		{tr("Move Textures Right (Coarse)"), tr("Move the textures twice the grid size right in the 3D camera's view."), QStringLiteral("chevron-right")},
		{tr("Move Textures Up (Fine)"), tr("Move the textures one texel up the 3D camera's view, whatever the grid."), QStringLiteral("chevron-up")},
		{tr("Move Textures Down (Fine)"), tr("Move the textures one texel down the 3D camera's view, whatever the grid."), QStringLiteral("chevron-down")},
		{tr("Move Textures Left (Fine)"), tr("Move the textures one texel left in the 3D camera's view, whatever the grid."), QStringLiteral("chevron-left")},
		{tr("Move Textures Right (Fine)"), tr("Move the textures one texel right in the 3D camera's view, whatever the grid."), QStringLiteral("chevron-right")},
		{tr("Flip Textures Horizontally"),
			tr("Mirror the texture axis that runs across the 3D camera's view on the picked faces, or on the Surfaces target."), QStringLiteral("compare")},
		{tr("Flip Textures Vertically"),
			tr("Mirror the texture axis that runs up the 3D camera's view on the picked faces, or on the Surfaces target."), QStringLiteral("compare")},
		{tr("Rotate Textures Clockwise"),
			tr("Turn the textures of the picked faces, or of the Surfaces target, 15° clockwise as seen from each face's front, about the texture origin."),
			QStringLiteral("rotate")},
		{tr("Rotate Textures Clockwise (Coarse)"), tr("Turn the textures 90° clockwise as seen from each face's front, about the texture origin."),
			QStringLiteral("rotate")},
		{tr("Rotate Textures Clockwise (Fine)"), tr("Turn the textures 1° clockwise as seen from each face's front, about the texture origin."),
			QStringLiteral("rotate")},
		{tr("Rotate Textures Counter-clockwise"),
			tr("Turn the textures of the picked faces, or of the Surfaces target, 15° counter-clockwise as seen from each face's front, about the texture origin."),
			QStringLiteral("rotate")},
		{tr("Rotate Textures Counter-clockwise (Coarse)"), tr("Turn the textures 90° counter-clockwise as seen from each face's front, about the texture origin."),
			QStringLiteral("rotate")},
		{tr("Rotate Textures Counter-clockwise (Fine)"), tr("Turn the textures 1° counter-clockwise as seen from each face's front, about the texture origin."),
			QStringLiteral("rotate")},
	};
	const QVector<LevelCameraTextureCommand>& commands = levelCameraTextureCommands();
	for (int index = 0; index < commands.size() && index < entries.size(); ++index) {
		StudioCommandRegistration command;
		command.commandId = commands.at(index).commandId;
		command.group = StudioCommandGroup::Edit;
		command.label = entries.at(index).label;
		command.statusTip = entries.at(index).tip;
		command.iconName = entries.at(index).icon;
		command.menuSection = levelCameraTextureRotates(commands.at(index)) ? tr("Rotate Textures") : tr("Move Textures");
		command.handler = [this, id = commands.at(index).commandId]() {
			QString error;
			if (!runLevelCameraTextureCommand(id, &error) && !error.isEmpty()) {
				statusBar()->showMessage(error, 6000);
				if (m_levelSurfaceTools) {
					m_levelSurfaceTools->setStatus(error, levelSurfaceEditsPending());
				}
			}
		};
		m_commands->registerCommand(command);
		m_commands->setEnabled(command.commandId, false);
	}
}

void ApplicationShell::refreshLevelCameraTextureCommands(bool surfaceTargetEditable)
{
	if (!m_commands) {
		return;
	}
	bool enabled = surfaceTargetEditable;
	if (!enabled && (m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map)
		&& !levelSurfaceGeometryBusy()) {
		// Picked faces are targets whatever the Surfaces tab's target says.
		const qsizetype picked = levelPickedSurfaceFaces().size();
		enabled = picked > 0 && picked <= 16384;
	}
	for (const LevelCameraTextureCommand& command : levelCameraTextureCommands()) {
		// Moves and flips read the camera's directions; turns need no camera.
		m_commands->setEnabled(command.commandId, enabled && (m_levelMap3D || levelCameraTextureRotates(command)));
	}
}

bool ApplicationShell::levelCameraTextureFacesPicked() const
{
	// Face mode's picked components belong to the component nudges, as
	// TrenchBroom's face tool keeps its arrows for moving faces.
	return !levelComponentsActive() && !levelPickedSurfaceFaces().isEmpty();
}

QString ApplicationShell::levelCameraTextureCommandForKey(QKeyCombination combination, bool facesPickedOnly) const
{
	if (!m_commands || (facesPickedOnly && !levelCameraTextureFacesPicked())) {
		return {};
	}
	combination = plainKey(combination);
	for (const LevelCameraTextureCommand& command : levelCameraTextureCommands()) {
		const QAction* action = m_commands->action(command.commandId);
		if (!action || !action->isEnabled()) {
			continue;
		}
		for (const QKeySequence& sequence : m_commands->shortcutSequences(command.commandId)) {
			if (sequence.count() == 1 && plainKey(sequence[0]) == combination) {
				return command.commandId;
			}
		}
	}
	return {};
}

bool ApplicationShell::runLevelCameraTextureCommand(const QString& commandId, QString* error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	LevelCameraTextureCommand command;
	if (!levelCameraTextureCommandForId(commandId, &command)) {
		return fail(tr("%1 is not a camera texture command.").arg(commandId));
	}
	setMode(StudioMode::Levels);
	if (levelCameraTextureRotates(command)) {
		// Each face turns as seen from its own front, so no camera is needed.
		if (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
			return fail(tr("Open a Quake-family map to rotate textures."));
		}
		return queueLevelSurfaceEdit(levelCameraTextureRequest(command, {}, {}, levelMapGridUnits()), error, true);
	}
	if (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		return fail(tr("Open a Quake-family map to move textures from the camera."));
	}
	if (!m_levelMap3D) {
		return fail(tr("Show the 3D view to move textures as the camera sees them."));
	}
	ModelVec3 right;
	ModelVec3 up;
	m_levelMap3D->viewAxes(&right, &up);
	const LevelSurfaceRequest request = levelCameraTextureRequest(command, mapVector(right), mapVector(up), levelMapGridUnits());
	return queueLevelSurfaceEdit(request, error, true);
}

} // namespace vibestudio
