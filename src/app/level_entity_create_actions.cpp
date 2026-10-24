// TrenchBroom's Create <class> commands on the Levels page: one command for
// each point and brush class of the loaded entity definitions, registered
// again whenever those classes change, so the command palette finds a class
// by name and Keyboard Shortcuts can give any class its own keys. A point
// class goes in at the pointer, or the middle of the view, the way a cursor
// paste places a pasted entity; a brush class takes the selected brushes.
// Each runs as the CreateEntity placement operation through
// core/level_entity_create.h, the service `map create-entity` uses, so it
// is one undo step that honours scene locks, hidden objects and the open
// group. Create Entity… types a class first.
//
// Workflow reference: TB ActionManager::createEntityDefinitionActions and
// MapViewBase::createEntity at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_entity_create.h"
#include "core/level_placement.h"

#include <QAction>
#include <QFileInfo>
#include <QStackedWidget>
#include <QStatusBar>

namespace vibestudio {

bool ApplicationShell::levelCursorPlacement(LevelPastePlacement* placement, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!placement) {
		return fail(tr("No placement to fill."));
	}
	placement->grid = m_levelMapViewport && m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0;
	// The camera when it was the view used last, or the only one showing.
	const bool camera = levelMap3DShowing() && (m_levelLastFocusedView == m_levelMap3D || !levelMap2DShowing());
	if (camera) {
		if (!m_levelMap3D || !m_levelMap3D->isEnabled()) {
			return fail(tr("Wait for the map camera before placing at its pointer."));
		}
		if (m_levelMap3D->isRendering()) {
			return fail(tr("Wait for the current camera view before placing at its pointer."));
		}
		placement->mode = LevelPasteMode::ViewRay;
		const auto ray = m_levelMap3D->viewRay(m_levelMap3D->commandPosition());
		const auto forward = m_levelMap3D->viewForward();
		placement->rayOrigin = {ray.origin.x, ray.origin.y, ray.origin.z, true};
		placement->rayForwardOnly = ray.forwardOnly;
		placement->direction = {ray.direction.x, ray.direction.y, ray.direction.z, true};
		placement->viewDirection = {forward.x, forward.y, forward.z, true};
		// Only brushes the camera draws can take the placement.
		placement->filterBrushes = true;
		placement->visibleBrushes.clear();
		for (const auto& owner : std::as_const(m_levelMap3DOwners)) {
			if (owner.kind == LevelMapSelectionKind::QuakeBrush) {
				placement->visibleBrushes.insert(owner.objectId);
			}
		}
		return true;
	}
	if (!m_levelMapViewport || !m_levelMapViewport->isEnabled()) {
		return fail(tr("Show a map view before placing at its pointer."));
	}
	// The plan view's plane at the far depth of what it shows, facing the
	// viewer; the selection's depth replaces it when there is one.
	const auto projection = m_levelMapViewport->projection();
	const int axis = projection == MapViewportProjection::TopXY ? 2 : projection == MapViewportProjection::FrontXZ ? 1 : 0;
	const auto depth = m_levelMapViewport->brushDepthRange(axis);
	const double hidden = axis == 1 ? depth.second : depth.first;
	placement->mode = LevelPasteMode::Plane;
	placement->referenceAxis = axis;
	placement->point = m_levelMapViewport->worldPositionAt(m_levelMapViewport->commandPosition(), hidden, false);
	placement->normal = {axis == 0 ? 1.0 : 0, axis == 1 ? -1.0 : 0, axis == 2 ? 1.0 : 0, true};
	placement->direction = {-placement->normal.x, -placement->normal.y, -placement->normal.z, true};
	return true;
}

void ApplicationShell::refreshLevelEntityCreateCommands()
{
	if (!m_commands) {
		return;
	}
	const QVector<LevelEntityCreateClass> classes = levelEntityCreateClasses(levelEntityCatalogue());
	QStringList parts;
	parts.reserve(classes.size());
	for (const LevelEntityCreateClass& entry : classes) {
		parts << entry.className + QLatin1Char('\t') + QString::number(static_cast<int>(entry.kind)) + QLatin1Char('\t') + entry.description;
	}
	const QString key = parts.join(QLatin1Char('\n'));
	if (key != m_levelEntityCreateKey) {
		m_levelEntityCreateKey = key;
		m_levelEntityCreateState = -1;
		// One install of every key for the whole change, however many classes.
		m_commands->beginBatch();
		for (const QString& id : std::as_const(m_levelEntityCreateCommandIds)) {
			m_commands->unregisterCommand(id);
		}
		m_levelEntityCreateCommandIds.clear();
		// A map command's keys act on the Levels page, as installShortcutScopes()
		// scopes every map command without a documented key.
		QWidget* levels = m_modeStack ? m_modeStack->widget(static_cast<int>(StudioMode::Levels)) : nullptr;
		const QString section = tr("Create Entity");
		for (const LevelEntityCreateClass& entry : classes) {
			StudioCommandRegistration command;
			command.commandId = levelEntityCreateCommandId(entry.className);
			command.group = StudioCommandGroup::Edit;
			command.label = tr("Create %1").arg(entry.className);
			QString tip;
			if (entry.kind == EntityClassKind::Point) {
				tip = tr("Place a %1 point entity at the pointer, or in the middle of the view, as one undo step.").arg(entry.className);
			} else {
				tip = tr("Make the selected brushes and patches into a %1 brush entity as one undo step.").arg(entry.className);
			}
			if (!entry.description.isEmpty()) {
				tip += QLatin1Char(' ') + entry.description.left(200);
			}
			command.statusTip = tip;
			command.iconName = QStringLiteral("entity");
			command.menuSection = section;
			const QString name = entry.className;
			command.handler = [this, name]() {
				setMode(StudioMode::Levels);
				createLevelEntityFromUi(name);
			};
			m_commands->registerCommand(command);
			if (levels) {
				m_commands->setShortcutScopes(command.commandId, {levels});
			}
			m_levelEntityCreateCommandIds << command.commandId;
		}
		m_commands->endBatch();
		refreshLevelViewportShortcuts();
	}
	// Point classes need a Quake-family map; brush classes something to take.
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool brushes = textMap && levelMapSelectionCanCreateBrushEntity(m_levelMapDocument);
	const int state = (textMap ? 1 : 0) | (brushes ? 2 : 0);
	if (state != m_levelEntityCreateState) {
		m_levelEntityCreateState = state;
		for (const LevelEntityCreateClass& entry : classes) {
			m_commands->setEnabled(levelEntityCreateCommandId(entry.className), textMap && (entry.kind == EntityClassKind::Point || brushes));
		}
	}
	m_commands->setEnabled(QStringLiteral("map.createEntity"), textMap && !classes.isEmpty());
}

void ApplicationShell::createLevelEntityFromUi(const QString& className, const LevelPastePlacement* placement)
{
	LevelEntityCreateRequest create;
	QString error;
	if (!levelEntityCreateRequestForClass(levelEntityCatalogue(), className, &create, &error)) {
		statusBar()->showMessage(tr("Could not create %1: %2").arg(className, error), 8000);
		return;
	}
	if (create.kind == EntityClassKind::Point) {
		const auto bounds = create.placement.entityBounds;
		if (placement) {
			create.placement = *placement;
		} else if (!levelCursorPlacement(&create.placement, &error)) {
			statusBar()->showMessage(tr("Could not create %1: %2").arg(create.className, error), 8000);
			return;
		}
		create.placement.entityBounds = bounds;
	}
	// Refused before the task starts, with the reason, as the worker would.
	if (const QString blocked = levelEntityCreateBlockedReason(m_levelMapDocument, create); !blocked.isEmpty()) {
		statusBar()->showMessage(tr("Could not create %1: %2").arg(create.className, blocked), 8000);
		return;
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::CreateEntity;
	request.entityCreate = create;
	const auto publish = levelPlacementCommitter();
	const LevelPlacementResult result = LevelPlacementTaskDialog::prepare(this, m_levelMapDocument, request);
	if (!result.succeeded) {
		statusBar()->showMessage(tr("Could not create %1: %2").arg(create.className, result.error), 8000);
		return;
	}
	if (!publish(result.document, &error)) {
		statusBar()->showMessage(tr("Could not create %1: %2").arg(create.className, error), 8000);
		return;
	}
	const LevelEntityCreateReport& report = result.entityCreateReport;
	// The new entity is the selection now; a component or clip tool left on
	// would act on nothing.
	setLevelComponentMode(0);
	setLevelMapClipMode(false);
	if (report.kind == EntityClassKind::Point) {
		m_lastAddedEntityClass = report.className;
	} else {
		m_lastTiedEntityClass = report.className;
	}
	recordActivity(tr("Entity created"), QStringLiteral("entity:%1 %2").arg(report.entityId).arg(report.className), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	if (report.kind == EntityClassKind::Point) {
		statusBar()->showMessage(tr("Created %1 as entity:%2 at %3 %4 %5. Undo removes it; save the map before building.")
									 .arg(report.className)
									 .arg(report.entityId)
									 .arg(report.origin.x, 0, 'g', 8)
									 .arg(report.origin.y, 0, 'g', 8)
									 .arg(report.origin.z, 0, 'g', 8),
			8000);
		return;
	}
	// Its keys come next, as Make Brush Entity shows them.
	showLevelSidebarPage(QStringLiteral("inspector"));
	const int held = report.brushes + report.patches;
	statusBar()->showMessage(tr("Made %n brush(es) and patch(es) into %1 (entity:%2). Its keys are in the Inspector; Undo puts them back.", nullptr, held)
								 .arg(report.className)
								 .arg(report.entityId),
		8000);
}

void ApplicationShell::showLevelEntityCreatePicker()
{
	const QVector<LevelEntityCreateClass> classes = levelEntityCreateClasses(levelEntityCatalogue());
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap || classes.isEmpty()) {
		statusBar()->showMessage(tr("Open a Quake-family map with entity definitions to create entities by class."), 6000);
		return;
	}
	setMode(StudioMode::Levels);
	// The picker takes the pointer off the view, so where it was is kept now.
	auto placement = std::make_shared<LevelPastePlacement>();
	QString ignored;
	m_levelEntityCreatePickerPlacement = levelCursorPlacement(placement.get(), &ignored) ? placement : nullptr;
	if (!m_levelEntityCreatePicker) {
		m_levelEntityCreatePicker = new QuickOpenDialog(this);
		m_levelEntityCreatePicker->setObjectName(QStringLiteral("levelEntityCreatePicker"));
		QuickOpenPurpose purpose;
		purpose.title = tr("Create Entity");
		purpose.placeholder = tr("Type part of a class name, such as light or func_door");
		purpose.listName = tr("Entity classes of the loaded definitions");
		purpose.emptyHint = tr("The loaded entity definitions have no point or brush classes.");
		purpose.noMatchHint = tr("No class matches \"%1\".");
		purpose.keyHelp = tr("Enter creates the highlighted class: a point class at the pointer, a brush class from the selected brushes. Escape closes.");
		m_levelEntityCreatePicker->setPurpose(purpose);
		connect(m_levelEntityCreatePicker, &QuickOpenDialog::entryChosen, this, [this](const QString& className) {
			const auto kept = std::move(m_levelEntityCreatePickerPlacement);
			m_levelEntityCreatePickerPlacement = nullptr;
			createLevelEntityFromUi(className, kept.get());
		});
	}
	QVector<QuickOpenEntry> entries;
	entries.reserve(classes.size());
	const bool brushes = levelMapSelectionCanCreateBrushEntity(m_levelMapDocument);
	for (const LevelEntityCreateClass& entry : classes) {
		// Brush classes are listed without brushes selected too; choosing one
		// then says what to select.
		QString kind;
		if (entry.kind == EntityClassKind::Point) {
			kind = tr("Point entity");
		} else if (brushes) {
			kind = tr("Brush entity");
		} else {
			kind = tr("Brush entity, select brushes first");
		}
		const QString source = entry.sourcePath.isEmpty() ? tr("built-in definitions") : QFileInfo(entry.sourcePath).fileName();
		entries.push_back({entry.className, entry.className, kind, source});
	}
	m_levelEntityCreatePicker->setEntries(entries);
	m_levelEntityCreatePicker->focusFilter();
	m_levelEntityCreatePicker->show();
	m_levelEntityCreatePicker->raise();
	m_levelEntityCreatePicker->activateWindow();
}

} // namespace vibestudio
