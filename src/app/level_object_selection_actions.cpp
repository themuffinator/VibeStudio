#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_scene.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <algorithm>

namespace vibestudio {
bool ApplicationShell::canSelectLevelMapObjects(bool sourceLines, bool includeComponents, bool anyMapFormat) const
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (m_levelObjectSelectionBusy || m_levelRegionSelectionBusy
		|| (anyMapFormat ? m_levelMapDocument.format == LevelMapFormat::Unknown : !textMap)
		|| (!sourceLines && m_levelMapDocument.selection.isEmpty()) || (!includeComponents && (levelComponentsActive() || !levelPickedSurfaceFaces().isEmpty()))
		|| m_levelQuickClip || levelSurfaceEditsPending()) { return false; }
	if (std::any_of(m_levelPlanViews.cbegin(),m_levelPlanViews.cend(),[](const auto* view) {
		return view && (view->clipMode() || view->isDrawingBrush() || view->isDragging() || view->isMovingComponents());
	})) { return false; }
	return !m_levelMap3D || (!m_levelMap3D->surfaceStrokeActive() && !m_levelMap3D->materialStrokeActive() && !m_levelMap3D->isDrawingBrush()
		&& !m_levelMap3D->isMovingSelection() && !m_levelMap3D->isResizingSelection() && !m_levelMap3D->isMovingComponents());
}
bool ApplicationShell::canSelectLevelMapType() const
{
	return canSelectLevelMapObjects(true,true) && (!levelComponentsActive()
		|| (m_levelMapViewport->componentMode() == MapViewport::ComponentMode::Faces && !m_levelMapViewport->componentHandlesLimited()));
}
void ApplicationShell::refreshLevelObjectSelectionCommands()
{
	m_commands->setEnabled(QStringLiteral("map.selectSiblings"),canSelectLevelMapObjects(false));
	m_commands->setEnabled(QStringLiteral("map.selectSourceLines"),canSelectLevelMapObjects(true));
	m_commands->setEnabled(QStringLiteral("map.selectConnectedEntities"),canSelectLevelMapObjects(false,true));
	m_commands->setEnabled(QStringLiteral("map.selectAllOfType"),canSelectLevelMapType());
	m_commands->setEnabled(QStringLiteral("map.selectTextured"),canSelectLevelMapTextured());
	// Doom maps have layers too (core/level_scene_layers.h).
	m_commands->setEnabled(QStringLiteral("map.expandSelectionToLayers"),canSelectLevelMapObjects(false,false,true));
}
void ApplicationShell::selectLevelMapObjectsFromUi(LevelObjectSelectionMode mode)
{
	const bool sourceLines = mode == LevelObjectSelectionMode::SourceLines;
	const bool connected = mode == LevelObjectSelectionMode::ConnectedEntities;
	const bool byType = mode == LevelObjectSelectionMode::RadiantType;
	const bool layers = mode == LevelObjectSelectionMode::Layers;
	const auto eligible = [&] { return byType ? canSelectLevelMapType() : canSelectLevelMapObjects(sourceLines,connected,layers); };
	if (!eligible()) { statusBar()->showMessage(tr("Finish the active tool before selecting map objects.")); return; }
	const auto exclusions = [this] {
		QSet<QString> result;
		for (const auto& key : levelSceneObjects(m_levelMapDocument)) {
			const auto colon = key.indexOf(':');
			if (m_levelMapViewport && m_levelMapViewport->isHidden(levelMapSelectionKindFromId(key.left(colon)),key.mid(colon+1).toInt())) { result.insert(key); }
		}
		return result;
	};
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectObjects;
	request.objectSelection.mode = mode;
	request.objectSelection.undoSelection = m_levelControls.undoObjectSelection;
	request.objectSelection.excludedObjects = exclusions();
	if (byType) {
		request.objectSelection.material = m_levelPaintMaterial;
		request.objectSelection.typeScope = levelComponentsActive() ? LevelTypeSelectionScope::Faces : LevelTypeSelectionScope::Objects;
	}
	// Capture before the input dialog too: a reload must not reinterpret entered
	// source positions against a different map while this dialog is open.
	const auto validate = levelPlacementCommitter(true);
	const auto publish = levelPlacementCommitter();
	if (sourceLines) {
		QDialog dialog(this); dialog.setObjectName(QStringLiteral("levelSourceLineDialog"));
		dialog.setWindowTitle(tr("Select by Source Line Number")); dialog.setAccessibleName(dialog.windowTitle());
		dialog.setWindowModality(Qt::WindowModal); dialog.setMinimumWidth(440);
		auto* layout = new QFormLayout(&dialog); layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
		auto* field = new QLineEdit; field->setObjectName(QStringLiteral("levelSourceLines")); field->setMaxLength(256 * 1024);
		field->setAccessibleName(tr("Source line numbers"));
		field->setAccessibleDescription(tr("Positive line numbers separated by commas or spaces. Uses the source text at last open; save and reopen to refresh line numbers."));
		field->setToolTip(field->accessibleDescription());
		auto* label = new QLabel(tr("&Source lines:")); label->setBuddy(field); layout->addRow(label,field);
		auto* problem = new QLabel; problem->setObjectName(QStringLiteral("levelSourceLineProblem")); problem->setWordWrap(true);
		problem->setAccessibleName(tr("Line number validation")); layout->addRow(problem);
		auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		auto* accept = buttons->button(QDialogButtonBox::Ok); accept->setText(tr("&Select")); accept->setEnabled(false);
		accept->setAccessibleName(tr("Select objects at source lines"));
		connect(field,&QLineEdit::textChanged,&dialog,[&] {
			QString error; QVector<int> lines; const bool valid = parseLevelMapSourceLines(field->text(),&lines,&error);
			accept->setEnabled(valid); problem->setText(field->text().isEmpty() ? QString{} : error);
		});
		connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
			QString error;
			if (!parseLevelMapSourceLines(field->text(),&request.objectSelection.lines,&error)) { problem->setText(error); return; }
			dialog.accept();
		});
		connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject); layout->addRow(buttons); field->setFocus();
		if (dialog.exec() != QDialog::Accepted) { return; }
	}
	QString error;
	const auto contextUnchanged = [&] {
		return eligible() && request.objectSelection.undoSelection == m_levelControls.undoObjectSelection
			&& request.objectSelection.excludedObjects == exclusions() && (!byType || request.objectSelection.material == m_levelPaintMaterial);
	};
	if (!contextUnchanged() || !validate(m_levelMapDocument,&error)) {
		statusBar()->showMessage(error.isEmpty() ? tr("The view, profile or filters changed. Select objects again from the current state.") : error,6000); return;
	}
	LevelPlacementResult result;
	{
		QScopedValueRollback<bool> busy(m_levelObjectSelectionBusy,true); refreshLevelObjectSelectionCommands();
		result = LevelPlacementTaskDialog::prepare(this,m_levelMapDocument,request);
	}
	refreshLevelObjectSelectionCommands();
	if (!result.succeeded) { statusBar()->showMessage(result.error,6000); return; }
	if (!contextUnchanged()) { statusBar()->showMessage(tr("The view, profile or filters changed. Select objects again from the current state."),6000); return; }
	if (result.objectSelectionReport.componentSelectionChanged) {
		if (!validate(result.document,&error)) { statusBar()->showMessage(error,6000); return; }
		if (!m_levelMapViewport || !m_levelMapViewport->setPickedComponents(result.objectSelectionReport.components)) {
			statusBar()->showMessage(tr("The complete face selection does not fit the current handles. Select fewer objects and try again."),6000); return;
		}
		shareLevelComponentPicks(m_levelMapViewport);
		statusBar()->showMessage(tr("Selected %n matching face(s).",nullptr,m_levelMapViewport->pickedComponentCount())); return;
	}
	if (!publish(result.document,&error)) { statusBar()->showMessage(error,6000); return; }
	refreshLevelMapWorkbench();
	if (layers) {
		QStringList names;
		for (const auto& id : std::as_const(result.objectSelectionReport.layers)) {
			const auto* node = levelSceneNode(m_levelMapDocument.scene,id);
			names << (node ? node->name : tr("Default"));
		}
		const int count = static_cast<int>(m_levelMapDocument.selection.size());
		QString message = tr("Selection now contains %n object(s) from %1.",nullptr,count).arg(names.join(QStringLiteral(", ")));
		if (const int skipped = result.objectSelectionReport.skippedGroups; skipped > 0) {
			message += QLatin1Char(' ') + tr("%n closed group(s) with hidden, locked or filtered members were left out.",nullptr,skipped);
		}
		statusBar()->showMessage(message,7000);
		return;
	}
	statusBar()->showMessage(byType ? tr("Selected %n object(s) by type.",nullptr,static_cast<int>(m_levelMapDocument.selection.size()))
		: connected ? tr("Selection now contains %n object(s), including direct entity connections.",nullptr,static_cast<int>(m_levelMapDocument.selection.size())) : sourceLines
		? tr("Selected %n object(s) from the source text at last open.",nullptr,static_cast<int>(m_levelMapDocument.selection.size()))
		: tr("Selected %n sibling object(s).",nullptr,static_cast<int>(m_levelMapDocument.selection.size())));
}
} // namespace vibestudio
