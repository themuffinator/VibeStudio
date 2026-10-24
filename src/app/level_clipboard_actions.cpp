#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <algorithm>

namespace vibestudio {
bool ApplicationShell::canCutLevelMapSelection() const
{
	// TrenchBroom v2026.2 MapWindow::canCutSelection also excludes modal tools.
	// Cut always deletes whole objects, never the component tool's selection.
	const bool planToolActive = std::any_of(m_levelPlanViews.cbegin(), m_levelPlanViews.cend(), [](const auto* view) {
		return view && (view->clipMode() || view->isDrawingBrush() || view->isDragging());
	});
	return levelMapSelectionIsDeletable(m_levelMapDocument) && !levelComponentsActive() && !planToolActive
		&& !m_levelQuickClip && (!m_levelControls.copyFaceAttributes || levelPickedSurfaceFaces().isEmpty())
		&& !levelSurfaceEditsPending() && (!m_levelMap3D || (!m_levelMap3D->surfaceStrokeActive() && !m_levelMap3D->materialStrokeActive()
			&& !m_levelMap3D->isDrawingBrush() && !m_levelMap3D->isMovingSelection() && !m_levelMap3D->isResizingSelection()));
}

bool ApplicationShell::copyLevelObjectsToClipboard(bool cut, QString* error)
{
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (m_levelClipboardBusy) { return fail(tr("A map clipboard operation is already running.")); }
	if (cut && !canCutLevelMapSelection()) {
		return fail(tr("Finish the active tool and select whole objects other than worldspawn to cut them."));
	}
	QScopedValueRollback<bool> busy(m_levelClipboardBusy, true);
	const auto validate = levelPlacementCommitter(true);
	const auto publish = levelPlacementCommitter();
	auto* clipboard = QGuiApplication::clipboard();
	bool clipboardChanged = false;
	QObject observer;
	QObject::connect(clipboard, &QClipboard::dataChanged, &observer, [&] { clipboardChanged = true; });
	LevelPlacementRequest request;
	request.operation = cut ? LevelPlacementOperation::Cut : LevelPlacementOperation::Copy;
	const auto result = LevelPlacementTaskDialog::prepare(this, m_levelMapDocument, request);
	if (!result.succeeded) { return fail(result.error); }
	if (!validate(result.document, error)) { return false; }
	if (clipboardChanged) { return fail(tr("The clipboard changed while the selection was being prepared. Copy or cut again from the current state.")); }
	if (cut && !canCutLevelMapSelection()) { return fail(tr("The active tool changed while Cut was being prepared. Finish the tool and try again.")); }
	const int count = static_cast<int>(m_levelMapDocument.selection.size());
	clipboard->setText(result.clipboardText);
	if (clipboard->text() != result.clipboardText) { return fail(tr("The clipboard could not retain the copied objects. The map was not changed.")); }
	if (cut) {
		// Publish after the clipboard accepts the text. Recheck context because
		// synchronous clipboard listeners may have changed the map or its selection.
		if (!canCutLevelMapSelection() || !publish(result.document, error)) {
			if (error && error->isEmpty()) { *error = tr("The active tool changed after copying. The map was not cut."); }
			return false;
		}
		recordActivity(tr("Level map objects cut"), tr("%n object(s)", nullptr, count), QStringLiteral("level-map"),
			OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Cut %n map object(s) as one undo step. Save the map before building.", nullptr, count));
	} else {
		statusBar()->showMessage(tr("Copied %n map object(s) as .map text.", nullptr, count));
	}
	return true;
}
} // namespace vibestudio
