#include "app/application_shell.h"
#include "app/brush_editor_dialog.h"
#include "app/map_viewport.h"
#include <QStatusBar>

namespace vibestudio
{
bool ApplicationShell::applyLevelBrushGeometry(const LevelMapBrush &brush, int brushId, QString *error)
{
	const auto revision = m_levelMapDocument.revision;
	if (!replaceLevelMapBrushGeometry(&m_levelMapDocument, brushId, brush, error)) {
		return false;
	}
	if (revision == m_levelMapDocument.revision) {
		return true;
	}
	recordActivity(tr("Brush components edited"), QStringLiteral("brush:%1").arg(brushId), QStringLiteral("level-map"),
				   OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Brush updated. Save the map to include it in the next build."));
	return true;
}
void ApplicationShell::editLevelMapBrushComponentsFromUi()
{
	if (m_levelMapDocument.selectionKind != LevelMapSelectionKind::QuakeBrush) {
		statusBar()->showMessage(tr("Select a brush to edit its vertices, edges or faces."));
		return;
	}
	const int id = m_levelMapDocument.selectedObjectId;
	for (const auto &brush : m_levelMapDocument.brushes) {
		if (brush.id != id) {
			continue;
		}
		auto probe = m_levelMapDocument;
		QString error;
		if (!replaceLevelMapBrushGeometry(&probe, id, brush, &error)) {
			statusBar()->showMessage(error);
			return;
		}
		BrushEditorDialog dialog(brush, m_levelMapViewport ? m_levelMapViewport->gridSize() : 8, this);
		const auto load = m_levelMapLoadSerial;
		const auto revision = m_levelMapDocument.revision;
		dialog.setApplyHandler([this, id, load, revision](const LevelMapBrush &draft, QString *commitError) {
			if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision) {
				*commitError = tr("The map changed while this draft was open. Close this "
								  "draft and reopen the brush from the current map.");
				return false;
			}
			return applyLevelBrushGeometry(draft, id, commitError);
		});
		dialog.exec();
		return;
	}
}
} // namespace vibestudio
