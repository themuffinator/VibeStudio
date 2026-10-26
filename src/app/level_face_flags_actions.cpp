// The Surfaces tab's Flags section and the Show Surfaces toggle. The section
// shows and changes the content and surface flags and value of the picked
// faces, or of the Surfaces target's brush faces, as one undo step through
// core/level_face_flags.h, the service `map face-flags` uses. Show Surfaces
// shows the tab, or hides it again when it is showing.
//
// Workflow reference: NRC radiant/surfacedialog.cpp SurfaceInspector::ApplyFlags,
// OnBtnUnsetFlags and SurfaceInspector_toggleShown at 449778b3
// (GPL-2.0-or-later), and TB FaceAttribsEditor at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_sidebar.h"
#include "core/level_face_flags.h"

#include <QApplication>
#include <QStatusBar>

namespace vibestudio {

void ApplicationShell::registerLevelFaceFlagCommands()
{
	// NetRadiant Custom's Unset button: every flag but detail cleared, value 0.
	StudioCommandRegistration command;
	command.commandId = QStringLiteral("map.surfaceClearFlags");
	command.label = tr("Clear Face Flags");
	command.group = StudioCommandGroup::Edit;
	command.menuSection = tr("Surface Adjustments");
	command.iconName = QStringLiteral("image");
	command.statusTip = tr("Clear every content and surface flag and the value of the picked faces, or of the Surfaces target's brush faces, keeping the detail flag, as NetRadiant Custom's Unset does.");
	command.handler = [this] {
		if (!m_levelSurfaceTools) {
			return;
		}
		setMode(StudioMode::Levels);
		showLevelSidebarPage(QStringLiteral("surfaces"), QStringLiteral("surfaces.flags"));
		QString error;
		if (!applyLevelFaceFlags(LevelFaceFlagsEdit::clearAll(), &error)) {
			m_levelSurfaceTools->setStatus(error, levelSurfaceEditsPending());
			statusBar()->showMessage(error, 6000);
		}
	};
	m_commands->registerCommand(command);
	m_commands->setEnabled(command.commandId, false);
}

void ApplicationShell::refreshLevelFaceFlags()
{
	if (!m_levelSurfaceTools || !m_commands) {
		return;
	}
	const LevelFaceFlagGame game = levelFaceFlagGame(m_levelMapDocument);
	LevelFaceFlagsSummary summary;
	if (game != LevelFaceFlagGame::None) {
		QVector<LevelSurfaceFace> faces;
		QVector<int> patches;
		levelSurfaceQueueTargets(true, &faces, &patches);
		if (faces.size() <= 262144) {
			summary = levelFaceFlagsSummary(m_levelMapDocument, faces);
		}
	}
	const bool editable = game != LevelFaceFlagGame::None && summary.faces > 0 && !levelSurfaceEditsPending() && !levelSurfaceGeometryBusy();
	m_levelSurfaceTools->setFaceFlags(game, summary, editable);
	const bool anything = (summary.contentAny & ~kLevelFaceDetailContent) != 0 || summary.surfaceAny != 0 || summary.valueMixed || summary.value != 0;
	m_commands->setEnabled(QStringLiteral("map.surfaceClearFlags"), editable && anything);
}

bool ApplicationShell::applyLevelFaceFlags(const LevelFaceFlagsEdit& edit, QString* error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [this, error](const QString& message) {
		if (error) {
			*error = message;
		}
		// Puts back any box the click changed.
		refreshLevelFaceFlags();
		return false;
	};
	if (!m_levelSurfaceTools) {
		return fail(tr("Surface tools are unavailable."));
	}
	if (levelSurfaceEditsPending() || levelSurfaceGeometryBusy()
		|| (m_levelMap3D && (m_levelMap3D->surfaceStrokeActive() || m_levelMap3D->isDrawingBrush()))) {
		return fail(tr("Finish or cancel the pending level edit before changing face flags."));
	}
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	levelSurfaceQueueTargets(true, &faces, &patches);
	LevelFaceFlagsReport report;
	QString problem;
	if (!setLevelMapFaceFlags(&m_levelMapDocument, faces, edit, &report, &problem)) {
		return fail(problem);
	}
	recordActivity(tr("Face flags changed"), tr("%n brush face(s)", nullptr, report.changedFaces), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	refreshLevelFaceFlags();
	const QString message = tr("Changed the flags of %n brush face(s). The change is one undo step.", nullptr, report.changedFaces);
	m_levelSurfaceTools->setStatus(message, false);
	statusBar()->showMessage(message, 6000);
	return true;
}

void ApplicationShell::refreshLevelSurfacesToggle()
{
	if (!m_commands) {
		return;
	}
	// Checked only while the tab can be seen: in another mode the next press
	// shows it rather than hiding it, so the check follows that.
	const StudioSidebar* sidebar = levelSidebarHolding(QStringLiteral("surfaces"));
	m_commands->setChecked(QStringLiteral("map.sidebar.surfaces"),
		currentMode() == StudioMode::Levels && sidebar && !sidebar->isFolded() && sidebar->currentPageId() == QLatin1String("surfaces"));
}

void ApplicationShell::toggleLevelSurfacesTab()
{
	StudioSidebar* sidebar = levelSidebarHolding(QStringLiteral("surfaces"));
	if (!sidebar) {
		refreshLevelSurfacesToggle();
		return;
	}
	const bool showing = !sidebar->isFolded() && sidebar->currentPageId() == QLatin1String("surfaces");
	if (showing && currentMode() == StudioMode::Levels) {
		// Hide it again: back to the tab it replaced, or fold the sidebar it
		// opened, as NetRadiant Custom's S closes its Surface Inspector.
		const QString back = m_levelSurfacesReturnTab;
		m_levelSurfacesReturnTab.clear();
		if (!back.isEmpty() && back != QLatin1String("surfaces") && sidebar->indexOfPage(back) >= 0) {
			sidebar->showPage(back);
		} else {
			sidebar->setFolded(true);
		}
		refreshLevelSurfacesToggle();
		return;
	}
	// Remember the tab to go back to, unless the sidebar already shows Surfaces
	// (pressed from another mode): then the tab it replaced stays remembered.
	if (!showing) {
		m_levelSurfacesReturnTab = sidebar->isFolded() ? QString() : sidebar->currentPageId();
	}
	// From a view's key the view keeps the keyboard, so the same key hides
	// the tab again; from a menu or command search focus moves into the tab.
	QWidget* focused = QApplication::focusWidget();
	const bool fromView = qobject_cast<MapViewport*>(focused) || qobject_cast<ModelViewport*>(focused);
	setMode(StudioMode::Levels);
	showLevelSidebarPage(QStringLiteral("surfaces"), QString(), !fromView);
	refreshLevelSurfacesToggle();
}

} // namespace vibestudio
