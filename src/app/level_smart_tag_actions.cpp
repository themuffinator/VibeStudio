#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_smart_tags.h"

#include <QAction>
#include <QInputDialog>
#include <QStatusBar>

namespace vibestudio {

void ApplicationShell::refreshLevelSmartTagCommands()
{
	if (!m_commands) { return; }
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	levelMaterialPicks(&faces, &patches);
	const bool ready = levelFilterTagsApply(m_levelMapDocument) && !levelSurfaceEditsPending() && !levelSurfaceGeometryBusy()
		&& (!m_levelMapDocument.selection.isEmpty() || !faces.isEmpty() || !patches.isEmpty());
	for (const QString& id : std::as_const(m_levelSmartTagCommandIds)) { m_commands->setEnabled(id, ready); }
}

void ApplicationShell::applyLevelSmartTagFromUi(const QString& tagId, bool enabled)
{
	const LevelFilterTag* found = currentLevelFilterTags().find(tagId);
	if (!found || levelSurfaceEditsPending() || levelSurfaceGeometryBusy()) {
		statusBar()->showMessage(tr("Finish or cancel the current edit before applying a smart tag."), 6000);
		return;
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::SmartTag;
	auto& edit = request.smartTag;
	edit.tag = *found;
	edit.enabled = enabled;
	edit.shaders = currentLevelFilterMaterials();
	levelMaterialPicks(&edit.faces, &edit.patches);
	edit.surfaceTargets = !edit.faces.isEmpty() || !edit.patches.isEmpty();
	const auto hidden = currentLevelViewFilterHidden();
	for (const auto& ref : hidden.objects) { edit.excludedObjects.insert(levelMapSelectionRefId(ref)); }
	for (const auto& face : hidden.faces) {
		edit.excludedObjects.insert(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, face.brushId}));
	}
	const auto publish = levelPlacementCommitter();
	const LevelMapDocument source = m_levelMapDocument;
	if (enabled && (edit.tag.match == LevelFilterTagMatch::Texture || edit.tag.match == LevelFilterTagMatch::SurfaceParm || edit.tag.match == LevelFilterTagMatch::ClassName)) {
		const QStringList names = levelMapTextureNames(source) + levelPackageTextureNames();
		const QStringList choices = levelSmartTagChoices(edit.tag, names, levelEntityCatalogue(), edit.shaders);
		if (choices.size() == 1) { edit.choice = choices.first(); }
		else {
			QInputDialog dialog(this);
			dialog.setObjectName(QStringLiteral("levelSmartTagChoice"));
			dialog.setWindowTitle(tr("Make %1").arg(edit.tag.name));
			dialog.setLabelText(edit.tag.match == LevelFilterTagMatch::ClassName ? tr("Brush classname") : tr("Matching material"));
			dialog.setComboBoxItems(choices);
			dialog.setComboBoxEditable(edit.tag.match != LevelFilterTagMatch::SurfaceParm);
			dialog.setAccessibleName(dialog.windowTitle());
			dialog.setAccessibleDescription(tr("Choose a name matching the tag. Cancel leaves the map unchanged."));
			if (choices.isEmpty() && edit.tag.match == LevelFilterTagMatch::SurfaceParm) {
				statusBar()->showMessage(tr("No loaded shader matches %1. Open the game package and let its materials finish loading.").arg(edit.tag.name), 8000);
				return;
			}
			if (dialog.exec() != QDialog::Accepted) { return; }
			edit.choice = dialog.textValue().trimmed();
		}
	}
	const LevelPlacementResult result = LevelPlacementTaskDialog::prepare(this, source, request);
	QString error = result.error;
	if (!result.succeeded || !publish(result.document, &error)) {
		statusBar()->showMessage(error, 8000);
		return;
	}
	const QString label = enabled ? tr("Make %1").arg(edit.tag.name) : tr("Make non-%1").arg(edit.tag.name);
	recordActivity(label, edit.choice, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("%1 completed as one undo step.").arg(label), 6000);
}
} // namespace vibestudio
