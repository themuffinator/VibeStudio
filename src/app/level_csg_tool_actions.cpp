// NetRadiant Custom's CSG Tool in the Levels page: a section of the Tools tab
// holding the tool's thickness (Use Grid copies the grid size), its face
// policy (leave the picked faces alone, keep only them, or leave out the faces
// facing the 2D view or the camera), Caulk and Keep Inner Brush, and Shrink,
// Expand and the four hollows as commands that read those saved options, so
// the palette, the menus and profile keys run them too. Each is one undo step
// from the core (core/level_hollow.h), and `map hollow`, `map shrink-brushes`
// and `map expand-brushes` do the same from the command line.
//
// Workflow reference: NetRadiant Custom CSG_Tool, CSGdlg_getSettings,
// CSG_Hollow and CSGdlg_BrushShrink/Expand (radiant/csg.cpp) at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/map_viewport_projection.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_hollow.h"
#include "core/studio_settings.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

// The commands the section's buttons run, in the order NetRadiant Custom's
// window shows them.
const QStringList& csgToolCommandIds()
{
	static const QStringList ids {QStringLiteral("map.csgShrink"), QStringLiteral("map.csgExpand"), QStringLiteral("map.csgHollowDiagonal"),
		QStringLiteral("map.csgHollowWrap"), QStringLiteral("map.csgHollowExtrude"), QStringLiteral("map.csgHollowPull")};
	return ids;
}

QString thicknessText(double value)
{
	return QString::number(value, 'g', 10);
}

} // namespace

void ApplicationShell::registerLevelCsgToolCommands()
{
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::Edit;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.menuSection = tr("CSG Tool");
		command.handler = [this, handler = std::move(handler)]() {
			setMode(StudioMode::Levels);
			handler();
		};
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.csgTool"), tr("CSG Tool"),
		tr("Show the CSG Tool in the Tools tab: a thickness, which faces to leave alone, caulk and keep inner brush, with Shrink, Expand and "
		   "four ways to hollow, as NetRadiant Custom's CSG Tool window has."),
		QStringLiteral("cube"), [this]() { showLevelCsgToolFromUi(); });
	const auto run = [this](const QString& id) { return [this, id]() { runLevelCsgToolCommandFromUi(id); }; };
	add(QStringLiteral("map.csgShrink"), tr("Shrink Brushes"),
		tr("Move the faces of the selected brushes in by the CSG Tool's thickness, except those its face policy leaves alone. One undo step."),
		QStringLiteral("shrink-fatten"), run(QStringLiteral("map.csgShrink")));
	add(QStringLiteral("map.csgExpand"), tr("Expand Brushes"),
		tr("Move the faces of the selected brushes out by the CSG Tool's thickness, except those its face policy leaves alone. One undo step."),
		QStringLiteral("shrink-fatten"), run(QStringLiteral("map.csgExpand")));
	add(QStringLiteral("map.csgHollowDiagonal"), tr("Hollow with Diagonal Joints"),
		tr("Build walls of the CSG Tool's thickness outside the selected brushes, meeting on mitred joints with no overlap or gap, so each brush "
		   "becomes the room."),
		QStringLiteral("cube"), run(QStringLiteral("map.csgHollowDiagonal")));
	add(QStringLiteral("map.csgHollowWrap"), tr("Hollow by Wrapping"),
		tr("Build walls of the CSG Tool's thickness inside the selected brushes, each stopping at the walls before it, as NetRadiant Custom's "
		   "wrap does."),
		QStringLiteral("cube"), run(QStringLiteral("map.csgHollowWrap")));
	add(QStringLiteral("map.csgHollowExtrude"), tr("Hollow by Extruding Faces"),
		tr("Build a wall of the CSG Tool's thickness straight out from each face of the selected brushes, its sides square to the face."),
		QStringLiteral("extrude"), run(QStringLiteral("map.csgHollowExtrude")));
	add(QStringLiteral("map.csgHollowPull"), tr("Hollow by Pulling Faces"),
		tr("Build walls of the CSG Tool's thickness outside the selected brushes with their corners filled, as Make Room does, using the CSG "
		   "Tool's options."),
		QStringLiteral("shape-room"), run(QStringLiteral("map.csgHollowPull")));
}

void ApplicationShell::refreshLevelCsgToolCommands()
{
	if (!m_commands) {
		return;
	}
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool brushes = textMap && levelMapSelectionClippable();
	for (const QString& id : csgToolCommandIds()) {
		m_commands->setEnabled(id, brushes);
	}
	m_commands->setEnabled(QStringLiteral("map.csgTool"), m_levelMapDocument.format != LevelMapFormat::DoomWad);
	syncLevelCsgToolPanel();
}

QWidget* ApplicationShell::buildLevelCsgToolPanel()
{
	auto* panel = new QWidget;
	panel->setObjectName(QStringLiteral("levelCsgToolPanel"));
	panel->setAccessibleName(tr("CSG Tool"));
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	auto* form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	layout->addLayout(form);

	// Thickness, with NetRadiant Custom's "Grid->" as a button beside it.
	auto* thicknessRow = new QWidget(panel);
	auto* thicknessLayout = new QHBoxLayout(thicknessRow);
	thicknessLayout->setContentsMargins(0, 0, 0, 0);
	auto* thickness = new QDoubleSpinBox(thicknessRow);
	thickness->setObjectName(QStringLiteral("levelCsgThickness"));
	thickness->setRange(kLevelCsgMinimumThickness, kLevelCsgMaximumThickness);
	thickness->setDecimals(3);
	thickness->setSingleStep(16.0);
	thickness->setAccelerated(true);
	thickness->setAccessibleName(tr("CSG Tool thickness in map units"));
	thickness->setToolTip(tr("How thick the walls are, and how far Shrink and Expand move each face."));
	thickness->setAccessibleDescription(thickness->toolTip());
	auto* useGrid = new QToolButton(thicknessRow);
	useGrid->setObjectName(QStringLiteral("levelCsgUseGrid"));
	useGrid->setText(tr("Use Grid"));
	useGrid->setToolButtonStyle(Qt::ToolButtonTextOnly);
	useGrid->setFocusPolicy(Qt::TabFocus);
	useGrid->setAccessibleName(tr("Use the grid size as the thickness"));
	useGrid->setToolTip(tr("Copy the grid size into the thickness and step it by the grid, as NetRadiant Custom's Grid button does."));
	useGrid->setAccessibleDescription(useGrid->toolTip());
	thicknessLayout->addWidget(thickness, 1);
	thicknessLayout->addWidget(useGrid);
	form->addRow(tr("Thickness"), thicknessRow);
	if (auto* label = qobject_cast<QLabel*>(form->labelForField(thicknessRow))) {
		label->setBuddy(thickness);
	}

	auto* faces = new QComboBox(panel);
	faces->setObjectName(QStringLiteral("levelCsgFacePolicy"));
	faces->addItem(tr("All but the picked faces"), levelCsgFacePolicyId(LevelCsgFacePolicy::ExcludePicked));
	faces->setItemData(0, tr("Leave the picked faces alone: they get no wall, or stay where they are. NetRadiant Custom's -faces."), Qt::ToolTipRole);
	faces->addItem(tr("Only the picked faces"), levelCsgFacePolicyId(LevelCsgFacePolicy::OnlyPicked));
	faces->setItemData(1, tr("Work on the picked faces only; a brush with none picked is left as it is. NetRadiant Custom's +faces."), Qt::ToolTipRole);
	faces->addItem(tr("All but those facing the 2D view"), levelCsgFacePolicyId(LevelCsgFacePolicy::ExcludeFacingView));
	faces->setItemData(2, tr("Leave alone the faces of each brush that face along the active 2D view's axis, either way: in the top view, the "
							 "floor and ceiling. NetRadiant Custom's -proj."),
		Qt::ToolTipRole);
	faces->addItem(tr("All but those facing the camera"), levelCsgFacePolicyId(LevelCsgFacePolicy::ExcludeFacingCamera));
	faces->setItemData(3, tr("Leave alone the faces of each brush that face along the 3D camera's view, either way, so hollowing makes a "
							 "passage the way the camera looks. NetRadiant Custom's -cam."),
		Qt::ToolTipRole);
	faces->setAccessibleName(tr("Faces the CSG Tool works on"));
	faces->setToolTip(tr("Which faces of each brush the CSG Tool leaves alone."));
	faces->setAccessibleDescription(faces->toolTip());
	form->addRow(tr("Faces"), faces);

	auto* caulk = new QCheckBox(tr("Caulk hidden faces"), panel);
	caulk->setObjectName(QStringLiteral("levelCsgCaulk"));
	caulk->setToolTip(tr("Hidden wall faces take the caulk material Make Room Options names, or the game's own. Without the inner brush that is "
						 "every wall face the room does not see; with it, the faces between the walls and the inner brush."));
	auto* keepInner = new QCheckBox(tr("Keep inner brush"), panel);
	keepInner->setObjectName(QStringLiteral("levelCsgKeepInner"));
	keepInner->setToolTip(tr("Keep the brush the walls grow from, or what the walls leave of it inside them, instead of removing it. "
							 "NetRadiant Custom's Remove inner brush, turned round."));
	for (QCheckBox* box : {caulk, keepInner}) {
		box->setAccessibleName(box->text());
		box->setAccessibleDescription(box->toolTip());
		form->addRow(box);
	}
	// What caulk would use on this map, so the game's material is never a
	// guess.
	auto* summary = new QLabel(panel);
	summary->setObjectName(QStringLiteral("levelCsgSummary"));
	summary->setWordWrap(true);
	summary->setAccessibleName(tr("CSG Tool caulk material"));
	layout->addWidget(summary);

	// The commands, each button the command itself.
	auto* actions = new QWidget(panel);
	actions->setObjectName(QStringLiteral("levelCsgActions"));
	auto* actionsLayout = new QVBoxLayout(actions);
	actionsLayout->setContentsMargins(0, 0, 0, 0);
	actionsLayout->setSpacing(1);
	for (const QString& id : csgToolCommandIds()) {
		QAction* action = m_commands ? m_commands->action(id) : nullptr;
		if (!action) {
			continue;
		}
		auto* button = new QPushButton(action->icon(), QString(action->text()).remove(QLatin1Char('&')), actions);
		button->setObjectName(QStringLiteral("levelCsgRow-") + id);
		button->setProperty("sidebarToolRow", true);
		button->setAccessibleName(button->text());
		button->setFocusPolicy(Qt::TabFocus);
		bindCommandButton(button, action);
		actionsLayout->addWidget(button);
	}
	layout->addWidget(actions);
	m_levelCsgToolPanel = panel;

	// Every change is saved at once, so the commands, whichever way they are
	// run, use what the section shows.
	const auto save = [this, thickness, faces, caulk, keepInner]() {
		LevelCsgToolPreferences preferences;
		preferences.thickness = thickness->value();
		parseLevelCsgFacePolicy(faces->currentData().toString(), &preferences.facePolicy);
		preferences.caulk = caulk->isChecked();
		preferences.keepInner = keepInner->isChecked();
		if (!m_settings.setLevelCsgToolPreferences(preferences)) {
			statusBar()->showMessage(tr("Unable to save the CSG Tool's options."), 5000);
		}
		syncLevelCsgToolPanel();
	};
	connect(thickness, &QDoubleSpinBox::valueChanged, panel, save);
	connect(faces, &QComboBox::currentIndexChanged, panel, save);
	connect(caulk, &QCheckBox::toggled, panel, save);
	connect(keepInner, &QCheckBox::toggled, panel, save);
	connect(useGrid, &QToolButton::clicked, panel, [this, thickness]() {
		const double grid = m_levelMapViewport ? m_levelMapViewport->gridSize() : 8.0;
		const double value = std::clamp(grid, kLevelCsgMinimumThickness, kLevelCsgMaximumThickness);
		thickness->setSingleStep(value);
		thickness->setValue(value);
		statusBar()->showMessage(tr("CSG Tool thickness set to the grid, %1 units.").arg(thicknessText(value)), 4000);
	});
	syncLevelCsgToolPanel();
	return panel;
}

void ApplicationShell::syncLevelCsgToolPanel()
{
	if (!m_levelCsgToolPanel) {
		return;
	}
	auto* thickness = m_levelCsgToolPanel->findChild<QDoubleSpinBox*>(QStringLiteral("levelCsgThickness"));
	auto* faces = m_levelCsgToolPanel->findChild<QComboBox*>(QStringLiteral("levelCsgFacePolicy"));
	auto* caulk = m_levelCsgToolPanel->findChild<QCheckBox*>(QStringLiteral("levelCsgCaulk"));
	auto* keepInner = m_levelCsgToolPanel->findChild<QCheckBox*>(QStringLiteral("levelCsgKeepInner"));
	auto* summary = m_levelCsgToolPanel->findChild<QLabel*>(QStringLiteral("levelCsgSummary"));
	if (!thickness || !faces || !caulk || !keepInner || !summary) {
		return;
	}
	const LevelCsgToolPreferences preferences = m_settings.levelCsgToolPreferences();
	{
		const QSignalBlocker thicknessBlocker(thickness);
		const QSignalBlocker facesBlocker(faces);
		const QSignalBlocker caulkBlocker(caulk);
		const QSignalBlocker innerBlocker(keepInner);
		if (std::abs(thickness->value() - preferences.thickness) > 1e-9) {
			thickness->setValue(preferences.thickness);
		}
		const int policy = faces->findData(levelCsgFacePolicyId(preferences.facePolicy));
		faces->setCurrentIndex(policy < 0 ? 0 : policy);
		caulk->setChecked(preferences.caulk);
		keepInner->setChecked(preferences.keepInner);
	}
	LevelRoomPreferences material = m_settings.levelRoomPreferences();
	material.caulk = true;
	const QString used = levelRoomCaulkTexture(m_levelMapDocument, material);
	summary->setText(!preferences.caulk ? tr("Walls keep the brushes' materials.")
		: used.isEmpty()			   ? tr("This map's game has no caulk material, so walls keep their materials until Make Room Options names one.")
									   : tr("Hidden faces take %1.").arg(used));
	summary->setAccessibleDescription(summary->text());
}

void ApplicationShell::showLevelCsgToolFromUi()
{
	if (!showLevelSidebarPage(QStringLiteral("tools"), QStringLiteral("tools.csg"), true)) {
		statusBar()->showMessage(tr("Open the Levels page with a Quake-family map to use the CSG Tool."), 5000);
		return;
	}
	// The thickness takes the keyboard first.
	if (m_levelCsgToolPanel) {
		if (auto* thickness = m_levelCsgToolPanel->findChild<QDoubleSpinBox*>(QStringLiteral("levelCsgThickness"))) {
			thickness->setFocus(Qt::ShortcutFocusReason);
			thickness->selectAll();
		}
	}
}

void ApplicationShell::runLevelCsgToolCommandFromUi(const QString& commandId)
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap || !levelMapSelectionClippable()) {
		statusBar()->showMessage(tr("Select brushes, or a brush entity, in a Quake-family map to use the CSG Tool."));
		return;
	}
	const LevelCsgToolPreferences tool = m_settings.levelCsgToolPreferences();
	// The face policy's direction: the active 2D view's axis, or the camera's.
	LevelMapVec3 axis;
	if (tool.facePolicy == LevelCsgFacePolicy::ExcludeFacingView) {
		if (!m_levelMapViewport) {
			statusBar()->showMessage(tr("Show a 2D view to leave out the faces facing it."), 5000);
			return;
		}
		const MapViewportProjection projection = m_levelMapViewport->projection();
		axis = projection == MapViewportProjection::TopXY ? LevelMapVec3 {0.0, 0.0, 1.0, true}
			: projection == MapViewportProjection::FrontXZ ? LevelMapVec3 {0.0, 1.0, 0.0, true}
															: LevelMapVec3 {1.0, 0.0, 0.0, true};
	} else if (tool.facePolicy == LevelCsgFacePolicy::ExcludeFacingCamera) {
		if (!m_levelMap3D || !levelMap3DShowing()) {
			statusBar()->showMessage(tr("Show the 3D camera to leave out the faces facing it."), 5000);
			return;
		}
		const ModelVec3 forward = m_levelMap3D->viewForward();
		axis = {forward.x, forward.y, forward.z, true};
	}
	const QVector<LevelSurfaceFace> excluded = levelCsgExcludedFaces(m_levelMapDocument, tool.facePolicy, levelPickedSurfaceFaces(), axis);
	const QString units = thicknessText(tool.thickness);
	QString error;
	if (commandId == QLatin1String("map.csgShrink") || commandId == QLatin1String("map.csgExpand")) {
		const bool shrink = commandId == QLatin1String("map.csgShrink");
		LevelMapBrushOffsetReport report;
		if (!offsetLevelMapSelectionFaces(&m_levelMapDocument, shrink ? -tool.thickness : tool.thickness, excluded, &report, &error)) {
			statusBar()->showMessage((shrink ? tr("Could not shrink the brushes: %1") : tr("Could not expand the brushes: %1")).arg(error), 6000);
			return;
		}
		recordActivity(shrink ? tr("Level map brushes shrunk") : tr("Level map brushes expanded"), tr("%n brush(es)", nullptr, report.changed),
			QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		const QString message = shrink ? tr("Shrank %n brush(es) by %1 units.", nullptr, report.changed).arg(units)
									   : tr("Expanded %n brush(es) by %1 units.", nullptr, report.changed).arg(units);
		statusBar()->showMessage(message + levelMapSkippedNote(report.skipped), 6000);
		return;
	}
	LevelMapHollowMode mode = LevelMapHollowMode::Diagonal;
	if (commandId == QLatin1String("map.csgHollowWrap")) {
		mode = LevelMapHollowMode::Touch;
	} else if (commandId == QLatin1String("map.csgHollowExtrude")) {
		mode = LevelMapHollowMode::Extrude;
	} else if (commandId == QLatin1String("map.csgHollowPull")) {
		mode = LevelMapHollowMode::Room;
	}
	LevelMapHollowOptions options;
	options.keepInner = tool.keepInner;
	options.openFaces = excluded;
	if (tool.caulk) {
		LevelRoomPreferences material = m_settings.levelRoomPreferences();
		material.caulk = true;
		options.caulkTexture = levelRoomCaulkTexture(m_levelMapDocument, material);
	}
	LevelMapHollowReport report;
	if (!hollowLevelMapSelection(&m_levelMapDocument, tool.thickness, mode, options, &report, &error)) {
		statusBar()->showMessage(tr("Could not hollow: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Level map brushes hollowed"), tr("%n wall(s)", nullptr, report.walls), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	const int walls = report.walls;
	QString message = tr("Hollowed %n brush(es) into walls %1 units thick.", nullptr, report.hollowed).arg(units);
	message += QLatin1Char(' ') + tr("Built %n wall(s).", nullptr, walls);
	if (report.innerKept > 0) {
		message += QLatin1Char(' ') + tr("Kept %n inner brush(es).", nullptr, report.innerKept);
	}
	if (report.openFaces > 0) {
		message += QLatin1Char(' ') + tr("Left %n face(s) open.", nullptr, report.openFaces);
	}
	if (!options.caulkTexture.isEmpty()) {
		message += QLatin1Char(' ') + tr("Caulked %n hidden face(s) with %1.", nullptr, report.caulkedFaces).arg(options.caulkTexture);
	} else if (tool.caulk) {
		message += QLatin1Char(' ') + tr("This game has no caulk material, so the walls keep their materials; name one in Make Room Options.");
	}
	statusBar()->showMessage(message + levelMapSkippedNote(report.skipped), 8000);
}

} // namespace vibestudio
