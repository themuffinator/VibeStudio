#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "core/level_scene.h"
#include "core/level_visibility.h"
#include <QStatusBar>

namespace vibestudio {
bool ApplicationShell::setLevelMapUserSelection(const QVector<LevelMapSelectionRef>& selection, QString* error, bool reveal)
{
	const auto before = m_levelMapDocument.historyRevision;
	// Viewport/group picks include hidden descendants as members of a unit.
	// Only explicit object-list activation asks to reveal those descendants.
	const bool changed = reveal ? selectAndRevealLevelMapObjects(&m_levelMapDocument, selection, m_levelControls.undoObjectSelection, error)
		: m_levelControls.undoObjectSelection ? changeLevelMapSelection(&m_levelMapDocument,selection,error)
		: setLevelMapSelection(&m_levelMapDocument,selection,error);
	if (before != m_levelMapDocument.historyRevision) {
		refreshLevelMapHistory();
		refreshCommandEnablement();
	}
	return changed;
}

QString ApplicationShell::levelSelectedClosedGroup() const
{
	if (!m_levelMapViewport || m_levelMapDocument.selection.isEmpty()) { return {}; }
	const auto& context = m_levelMapViewport->sceneSelectionContext();
	QString group;
	for (const auto& ref : m_levelMapDocument.selection) {
		const auto found = context.closedGroups.value(levelMapSelectionRefId(ref));
		if (found.isEmpty() || (!group.isEmpty() && group != found)) { return {}; }
		group = found;
	}
	return group;
}

void ApplicationShell::changeLevelGroupContext(const QString& id, bool closeAll)
{
	QString error;
	const bool changed = m_levelControls.undoObjectSelection ? changeLevelSceneGroupContext(&m_levelMapDocument,id,closeAll,&error)
		: (id.isEmpty() ? closeLevelSceneGroup(&m_levelMapDocument,closeAll,&error) : openLevelSceneGroup(&m_levelMapDocument,id,&error));
	if (!changed) { statusBar()->showMessage(error,6000); return; }
	setLevelComponentMode(0); setLevelMapClipMode(false);
	refreshLevelMapWorkbench();
	const auto* group = m_levelMapDocument.openSceneGroups.isEmpty() ? nullptr : levelSceneNode(m_levelMapDocument.scene,m_levelMapDocument.openSceneGroups.last());
	statusBar()->showMessage(group ? tr("Editing group %1. New objects join it; close the group to edit outside it.").arg(group->name)
		: tr("Group editing closed. Closed groups select and transform as units."),6000);
}
} // namespace vibestudio
