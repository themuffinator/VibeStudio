// Editing tools the popular editors share and the Levels page adds to its
// menus, context menu and command search: brush entities (Radiant's entity
// menu, Hammer's Tie to Entity and Move to World), Radiant's region
// selections, Make Detail and Make Structural, VibeRadiant's Drop to Floor and
// TrenchBroom's CSG Intersect. Each is one undo step from core/level_map.h,
// and each has a `map` CLI command doing the same.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_actions.h"
#include "app/model_viewport.h"
#include "app/studio_icons.h"
#include "core/entity_builtin_catalogue.h"
#include "core/level_doom_align.h"
#include "core/level_linked_groups.h"
#include "core/level_scene.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio {

namespace {

// The brush classes most maps use, offered first whatever definitions say.
const QStringList& commonBrushClasses()
{
	static const QStringList classes {QStringLiteral("func_group"), QStringLiteral("func_detail"), QStringLiteral("func_door"), QStringLiteral("func_wall"),
		QStringLiteral("func_button"), QStringLiteral("func_plat"), QStringLiteral("func_train"), QStringLiteral("func_rotating"),
		QStringLiteral("trigger_multiple"), QStringLiteral("trigger_once"), QStringLiteral("trigger_teleport"), QStringLiteral("trigger_hurt"),
		QStringLiteral("trigger_push")};
	return classes;
}

} // namespace

EntityDefinitionCatalogue ApplicationShell::levelEntityCatalogue() const
{
	// The project's own definitions win; without them the map's game brings
	// its starter catalogue.
	if (!m_entityDefinitions.isEmpty()) {
		return m_entityDefinitions;
	}
	if (m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map) {
		return builtinEntityDefinitions(builtinEntityGameForMap(m_levelMapDocument));
	}
	return {};
}

QStringList ApplicationShell::levelBrushEntityClasses() const
{
	QStringList classes;
	const EntityDefinitionCatalogue catalogue = levelEntityCatalogue();
	for (const EntityClassDefinition& definition : catalogue.classes) {
		if (definition.kind == EntityClassKind::Brush && definition.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
			classes << definition.className;
		}
	}
	if (classes.isEmpty()) {
		classes = commonBrushClasses();
	}
	classes.sort(Qt::CaseInsensitive);
	return classes;
}

QMenu* ApplicationShell::buildLevelBrushEntityMenu(QWidget* parent)
{
	auto* menu = new QMenu(tr("Brush Entity"), parent);
	menu->setObjectName(QStringLiteral("levelBrushEntityMenu"));
	menu->setIcon(studioIcon(QStringLiteral("entity")));
	menu->setToolTipsVisible(true);
	const QStringList available = levelBrushEntityClasses();
	// The everyday classes first, then the rest of the catalogue's in a
	// submenu, the way Radiant's right-click entity menu groups them.
	QStringList quick;
	for (const QString& name : commonBrushClasses()) {
		if (available.contains(name, Qt::CaseInsensitive) || available == commonBrushClasses()) {
			quick << name;
		}
	}
	if (!m_lastTiedEntityClass.isEmpty() && !quick.contains(m_lastTiedEntityClass, Qt::CaseInsensitive)) {
		quick.prepend(m_lastTiedEntityClass);
	}
	for (const QString& name : std::as_const(quick)) {
		QAction* make = menu->addAction(tr("Make %1").arg(name));
		make->setObjectName(QStringLiteral("levelMake-") + name);
		connect(make, &QAction::triggered, this, [this, name]() { tieLevelMapSelectionFromUi(name); });
	}
	QStringList rest;
	for (const QString& name : available) {
		if (!quick.contains(name, Qt::CaseInsensitive)) {
			rest << name;
		}
	}
	if (!rest.isEmpty()) {
		QMenu* more = menu->addMenu(tr("All Brush Classes"));
		more->setObjectName(QStringLiteral("levelBrushEntityAll"));
		QHash<QString, QMenu*> groups;
		for (const QString& name : std::as_const(rest)) {
			// Filed by prefix, func_ and trigger_, as the palette files them.
			const QString prefix = name.contains(QLatin1Char('_')) ? name.section(QLatin1Char('_'), 0, 0) + QLatin1Char('_') : QString();
			QMenu* group = more;
			if (!prefix.isEmpty()) {
				QMenu*& made = groups[prefix];
				if (!made) {
					made = more->addMenu(prefix);
				}
				group = made;
			}
			QAction* make = group->addAction(name);
			connect(make, &QAction::triggered, this, [this, name]() { tieLevelMapSelectionFromUi(name); });
		}
	}
	menu->addSeparator();
	if (QAction* other = m_commands ? m_commands->action(QStringLiteral("map.tieToEntity")) : nullptr) {
		menu->addAction(other);
	}
	if (QAction* world = m_commands ? m_commands->action(QStringLiteral("map.moveToWorld")) : nullptr) {
		menu->addAction(world);
	}
	return menu;
}

void ApplicationShell::registerLevelEditingCommands()
{
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, const QString& section,
						 std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::Edit;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.menuSection = section;
		command.handler = [this, handler = std::move(handler)]() {
			setMode(StudioMode::Levels);
			handler();
		};
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.replaceKeyValues"), tr("Replace Key Values…"),
		tr("Find a value of one key across the map's entities, or the selected ones, and replace it, as one undo step."), QStringLiteral("search"),
		tr("Keys"), [this]() { replaceLevelMapKeyValuesFromUi(); });
	const QString linked = tr("Linked Groups");
	add(QStringLiteral("map.createLinkedCopy"), tr("Create Linked Copy"),
		tr("Copy the selection's scene group beside itself and link the copies, as TrenchBroom's linked duplicates: an edit inside one "
		   "is made to every copy, while moving a whole copy moves only that one."),
		QStringLiteral("copy"), linked, [this]() { createLinkedLevelCopyFromUi(); });
	add(QStringLiteral("map.selectLinkedCopies"), tr("Select Linked Copies"),
		tr("Select every copy of the selection's linked groups."), QStringLiteral("select-box"), linked,
		[this]() { selectLinkedLevelCopiesFromUi(); });
	add(QStringLiteral("map.updateLinkedCopies"), tr("Update Linked Copies"),
		tr("Make the other copies of the selection's linked group match it now, for copies changed in different ways at once."),
		QStringLiteral("refresh"), linked, [this]() { updateLinkedLevelCopiesFromUi(); });
	add(QStringLiteral("map.unlinkCopy"), tr("Separate Linked Copy"),
		tr("Unlink the selection's group from its copies; it keeps its content."), QStringLiteral("close"), linked,
		[this]() { unlinkLevelCopyFromUi(); });
	const QString entities = tr("Brush Entities");
	add(QStringLiteral("map.tieToEntity"), tr("Make Brush Entity…"),
		tr("Make the selected brushes into a brush entity of a class you choose, as Radiant's entity menu and Hammer's Tie to Entity do."),
		QStringLiteral("entity"), entities, [this]() { tieLevelMapSelectionFromUi(QString()); });
	add(QStringLiteral("map.moveToWorld"), tr("Move to World"),
		tr("Give the selected brush entities' brushes back to worldspawn; the entities and their keys go."), QStringLiteral("map"), entities,
		[this]() { moveLevelMapSelectionToWorldFromUi(); });
	const QString region = tr("Select by Region");
	add(QStringLiteral("map.selectInside"), tr("Select Inside"), tr("Select what lies wholly inside the selection's bounds."), QStringLiteral("select-box"),
		region, [this]() { selectLevelMapRegionFromUi(LevelMapRegionSelection::Inside); });
	add(QStringLiteral("map.selectTouching"), tr("Select Touching"), tr("Select what touches or overlaps the selection's bounds."),
		QStringLiteral("select-box"), region, [this]() { selectLevelMapRegionFromUi(LevelMapRegionSelection::Touching); });
	add(QStringLiteral("map.selectCompleteTall"), tr("Select Complete Tall"),
		tr("Select what stands wholly within the selection's footprint in the active 2D view, at any height."), QStringLiteral("select-box"), region,
		[this]() { selectLevelMapRegionFromUi(LevelMapRegionSelection::CompleteTall); });
	add(QStringLiteral("map.selectPartialTall"), tr("Select Partial Tall"),
		tr("Select what overlaps the selection's footprint in the active 2D view, at any height."), QStringLiteral("select-box"), region,
		[this]() { selectLevelMapRegionFromUi(LevelMapRegionSelection::PartialTall); });
	const QString geometry = tr("Brush Tools");
	add(QStringLiteral("map.makeDetail"), tr("Make Detail"),
		tr("Make the selected brushes detail: the detail content flag in Quake II and III, func_detail in Quake."), QStringLiteral("cube"), geometry,
		[this]() { setLevelMapDetailFromUi(true); });
	add(QStringLiteral("map.makeStructural"), tr("Make Structural"), tr("Make the selected detail brushes structural again."), QStringLiteral("cube"),
		geometry, [this]() { setLevelMapDetailFromUi(false); });
	add(QStringLiteral("map.intersect"), tr("CSG Intersect"),
		tr("Replace the selected brushes with the one brush where they all overlap, as TrenchBroom's CSG Intersect does."), QStringLiteral("merge"),
		geometry, [this]() { intersectLevelMapSelectionFromUi(); });
	add(QStringLiteral("map.dropToFloor"), tr("Drop to Floor"),
		tr("Move the selected point entities straight down onto the surface beneath them, keeping each class's height above it."),
		QStringLiteral("move"), geometry, [this]() { dropLevelMapSelectionToFloorFromUi(); });
	add(QStringLiteral("map.shearTool"), tr("Shear Tool"),
		tr("Drag the handle in the middle of a side of the selection in a 2D view to slant the selection about the opposite side, "
		   "as TrenchBroom's shear tool does. Escape or the command again turns it off."),
		QStringLiteral("resize"), geometry, [this]() {
			QAction* tool = m_commands ? m_commands->action(QStringLiteral("map.shearTool")) : nullptr;
			setLevelShearMode(tool && tool->isChecked());
		});
	if (QAction* tool = m_commands->action(QStringLiteral("map.shearTool"))) {
		tool->setCheckable(true);
	}
	add(QStringLiteral("map.shearSelection"), tr("Shear…"),
		tr("Slant the selection: points slide along one axis by how far they lie along another, as TrenchBroom's shear tool does."),
		QStringLiteral("resize"), geometry, [this]() { shearLevelMapSelectionFromUi(); });
	const QString visual = tr("Camera Surface");
	add(QStringLiteral("map.raiseCameraSurface"), tr("Raise Surface at Crosshair"),
		tr("Raise the Doom floor or ceiling under the camera's crosshair by 8 units, as Doom Builder's visual mode does."), QStringLiteral("chevron-up"),
		visual, [this]() { shiftLevelCameraSurfaceFromUi(8, false); });
	add(QStringLiteral("map.lowerCameraSurface"), tr("Lower Surface at Crosshair"),
		tr("Lower the Doom floor or ceiling under the camera's crosshair by 8 units."), QStringLiteral("chevron-down"), visual,
		[this]() { shiftLevelCameraSurfaceFromUi(-8, false); });
	add(QStringLiteral("map.brightenCameraSurface"), tr("Brighten Sector at Crosshair"),
		tr("Raise the light of the Doom sector under the camera's crosshair by 16."), QStringLiteral("sparkle"), visual,
		[this]() { shiftLevelCameraSurfaceFromUi(16, true); });
	add(QStringLiteral("map.darkenCameraSurface"), tr("Darken Sector at Crosshair"),
		tr("Lower the light of the Doom sector under the camera's crosshair by 16."), QStringLiteral("eye"), visual,
		[this]() { shiftLevelCameraSurfaceFromUi(-16, true); });
	// A larger X offset slides a Doom texture left, a larger Y offset up.
	add(QStringLiteral("map.nudgeCameraTextureLeft"), tr("Nudge Texture Left at Crosshair"),
		tr("Slide the texture of the Doom wall under the camera's crosshair one unit left."), QStringLiteral("chevron-left"), visual,
		[this]() { nudgeLevelCameraTextureFromUi(1, 0); });
	add(QStringLiteral("map.nudgeCameraTextureRight"), tr("Nudge Texture Right at Crosshair"),
		tr("Slide the texture of the Doom wall under the camera's crosshair one unit right."), QStringLiteral("chevron-right"), visual,
		[this]() { nudgeLevelCameraTextureFromUi(-1, 0); });
	add(QStringLiteral("map.nudgeCameraTextureUp"), tr("Nudge Texture Up at Crosshair"),
		tr("Slide the texture of the Doom wall under the camera's crosshair one unit up."), QStringLiteral("chevron-up"), visual,
		[this]() { nudgeLevelCameraTextureFromUi(0, 1); });
	add(QStringLiteral("map.nudgeCameraTextureDown"), tr("Nudge Texture Down at Crosshair"),
		tr("Slide the texture of the Doom wall under the camera's crosshair one unit down."), QStringLiteral("chevron-down"), visual,
		[this]() { nudgeLevelCameraTextureFromUi(0, -1); });
	add(QStringLiteral("map.dragCameraTextures"), tr("Drag Textures in Camera"),
		tr("Drag a Doom wall's texture with the left button in the camera to slide its offsets, as in Doom Builder's visual mode; the camera "
		   "follows as you drag, letting go makes one undo step and Escape puts the texture back."),
		QStringLiteral("move"), visual, [this]() {
			QAction* tool = m_commands ? m_commands->action(QStringLiteral("map.dragCameraTextures")) : nullptr;
			setLevelTextureDragMode(tool && tool->isChecked());
		});
	if (QAction* tool = m_commands->action(QStringLiteral("map.dragCameraTextures"))) {
		tool->setCheckable(true);
	}
	add(QStringLiteral("map.alignWallTexturesCrosshair"), tr("Align Textures at Crosshair"),
		tr("Line up the textures of the walls joined to the one under the camera's crosshair, as Doom Builder's auto-align does: "
		   "they run on across each join and keep their rows level."),
		QStringLiteral("uv"), visual, [this]() { alignLevelWallTexturesFromUi(true); });
	add(QStringLiteral("map.alignWallTextures"), tr("Align Wall Textures"),
		tr("Line up the textures of the walls joined to the first selected linedef, along the selected linedefs when there are several."),
		QStringLiteral("uv"), visual, [this]() { alignLevelWallTexturesFromUi(false); });
	add(QStringLiteral("map.makeSectorMode"), tr("Make Sector Mode"),
		tr("Click inside a closed shape of lines in the Top view to make a sector of it, islands of lines inside included, as Doom "
		   "Builder's Make Sectors mode does. Escape or the command again turns it off."),
		QStringLiteral("polygon"), visual, [this]() {
			QAction* tool = m_commands ? m_commands->action(QStringLiteral("map.makeSectorMode")) : nullptr;
			setLevelMakeSectorMode(tool && tool->isChecked());
		});
	if (QAction* tool = m_commands->action(QStringLiteral("map.makeSectorMode"))) {
		tool->setCheckable(true);
	}
	add(QStringLiteral("map.curveLinedefs"), tr("Curve Linedefs…"),
		tr("Bend the selected Doom linedefs into arcs of several linedefs, as Doom Builder's curve mode does."), QStringLiteral("polygon"), geometry,
		[this]() { curveLevelMapLinedefsFromUi(); });
	// Hammer's Align Objects, along the active 2D view's right and up.
	const QString align = tr("Align");
	add(QStringLiteral("map.alignLeft"), tr("Align Left"), tr("Line the selected objects up on the selection's left edge in the active 2D view."),
		QStringLiteral("move"), align, [this]() { alignLevelMapSelectionFromUi(true, static_cast<int>(LevelMapAlignEdge::Minimum)); });
	add(QStringLiteral("map.alignRight"), tr("Align Right"), tr("Line the selected objects up on the selection's right edge in the active 2D view."),
		QStringLiteral("move"), align, [this]() { alignLevelMapSelectionFromUi(true, static_cast<int>(LevelMapAlignEdge::Maximum)); });
	add(QStringLiteral("map.alignTop"), tr("Align Top"), tr("Line the selected objects up on the selection's top edge in the active 2D view."),
		QStringLiteral("move"), align, [this]() { alignLevelMapSelectionFromUi(false, static_cast<int>(LevelMapAlignEdge::Maximum)); });
	add(QStringLiteral("map.alignBottom"), tr("Align Bottom"), tr("Line the selected objects up on the selection's bottom edge in the active 2D view."),
		QStringLiteral("move"), align, [this]() { alignLevelMapSelectionFromUi(false, static_cast<int>(LevelMapAlignEdge::Minimum)); });
	add(QStringLiteral("map.alignCentreHorizontal"), tr("Centre Horizontally"),
		tr("Line the selected objects' centres up across the active 2D view."), QStringLiteral("move"), align,
		[this]() { alignLevelMapSelectionFromUi(true, static_cast<int>(LevelMapAlignEdge::Centre)); });
	add(QStringLiteral("map.alignCentreVertical"), tr("Centre Vertically"),
		tr("Line the selected objects' centres up down the active 2D view."), QStringLiteral("move"), align,
		[this]() { alignLevelMapSelectionFromUi(false, static_cast<int>(LevelMapAlignEdge::Centre)); });
	// Radiant's regions and Hammer's cordon.
	const QString regions = tr("Region");
	add(QStringLiteral("map.regionSetSelection"), tr("Set Region to Selection"), tr("Keep the views to the selection's bounds."),
		QStringLiteral("select-box"), regions, [this]() { setLevelRegionFromSelection(); });
	add(QStringLiteral("map.regionSetView"), tr("Set Region to View"), tr("Keep the views to what the active 2D view shows."), QStringLiteral("frame"),
		regions, [this]() { setLevelRegionFromView(); });
	add(QStringLiteral("map.regionClear"), tr("Clear Region"), tr("Show the whole map again."), QStringLiteral("close"), regions,
		[this]() { clearLevelRegion(); });
	add(QStringLiteral("map.regionSave"), tr("Save Region As…"), tr("Write the region as a sealed map of its own, to compile and test on its own."),
		QStringLiteral("save"), regions, [this]() { saveLevelRegionAs(); });
	add(QStringLiteral("map.regionCompile"), tr("Compile Region"),
		tr("Write the region beside the map as a sealed map and compile it with the chosen profile."), QStringLiteral("play"), regions,
		[this]() { compileLevelRegion(); });
}

void ApplicationShell::shiftLevelCameraSurfaceFromUi(int delta, bool light)
{
	if (!levelMapDoomEditable() || !m_levelMap3D || !levelMap3DShowing()) {
		statusBar()->showMessage(tr("Show the camera on a Doom or Hexen format map, and aim at a floor or ceiling."));
		return;
	}
	CameraSurfacePoint surface;
	int triangle = -1;
	const QPointF centre(m_levelMap3D->width() * 0.5, m_levelMap3D->height() * 0.5);
	if (m_levelMap3D->isRendering() || !m_levelMap3D->surfacePointAt(centre, &surface, &triangle) || triangle < 0
		|| triangle >= m_levelPreviewMaterialTargets.size()) {
		statusBar()->showMessage(tr("Aim the crosshair at a floor or ceiling once the camera is ready."));
		return;
	}
	const LevelMaterialTarget target = m_levelPreviewMaterialTargets.at(triangle);
	if (target.kind != LevelMaterialKind::SectorFloor && target.kind != LevelMaterialKind::SectorCeiling) {
		statusBar()->showMessage(tr("Aim the crosshair at a floor or ceiling."));
		return;
	}
	const LevelMapSectorField field = light ? LevelMapSectorField::Light
		: (target.kind == LevelMaterialKind::SectorFloor ? LevelMapSectorField::Floor : LevelMapSectorField::Ceiling);
	// The shift acts on the sector aimed at; the selection comes back after.
	const QVector<LevelMapSelectionRef> selection = m_levelMapDocument.selection;
	QString error;
	setLevelMapSelection(&m_levelMapDocument, {{LevelMapSelectionKind::DoomSector, target.objectId}}, &error);
	int changed = 0;
	const bool shifted = shiftLevelMapSectors(&m_levelMapDocument, field, delta, &changed, &error);
	setLevelMapSelection(&m_levelMapDocument, selection);
	if (!shifted) {
		statusBar()->showMessage(tr("Could not change the sector: %1").arg(error), 5000);
		return;
	}
	recordActivity(tr("Sector changed from the camera"), QStringLiteral("sector:%1").arg(target.objectId), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(light ? tr("Sector %1 light changed by %2.").arg(target.objectId).arg(delta)
									 : (field == LevelMapSectorField::Floor ? tr("Sector %1 floor moved by %2.").arg(target.objectId).arg(delta)
																	  : tr("Sector %1 ceiling moved by %2.").arg(target.objectId).arg(delta)),
		3000);
}

bool ApplicationShell::levelCameraWallAtCrosshair(LevelMaterialTarget* target)
{
	if (!levelMapDoomEditable() || !m_levelMap3D || !levelMap3DShowing()) {
		statusBar()->showMessage(tr("Show the camera on a Doom or Hexen format map, and aim at a wall."));
		return false;
	}
	CameraSurfacePoint surface;
	int triangle = -1;
	const QPointF centre(m_levelMap3D->width() * 0.5, m_levelMap3D->height() * 0.5);
	if (m_levelMap3D->isRendering() || !m_levelMap3D->surfacePointAt(centre, &surface, &triangle) || triangle < 0
		|| triangle >= m_levelPreviewMaterialTargets.size()) {
		statusBar()->showMessage(tr("Aim the crosshair at a wall once the camera is ready."));
		return false;
	}
	*target = m_levelPreviewMaterialTargets.at(triangle);
	if (target->kind != LevelMaterialKind::WallUpper && target->kind != LevelMaterialKind::WallLower
		&& target->kind != LevelMaterialKind::WallMiddle) {
		statusBar()->showMessage(tr("Aim the crosshair at a wall."));
		return false;
	}
	return true;
}

void ApplicationShell::alignLevelWallTexturesFromUi(bool crosshair)
{
	LevelDoomAlignRequest request;
	if (crosshair) {
		LevelMaterialTarget target;
		if (!levelCameraWallAtCrosshair(&target)) {
			return;
		}
		request.sidedef = target.objectId;
		request.part = target.kind == LevelMaterialKind::WallUpper
			? LevelDoomWallPart::Upper
			: (target.kind == LevelMaterialKind::WallLower ? LevelDoomWallPart::Lower : LevelDoomWallPart::Middle);
	} else {
		if (!levelMapDoomEditable()) {
			statusBar()->showMessage(tr("Align wall textures on a Doom or Hexen format map."));
			return;
		}
		// From the first selected linedef's front side, along the selected ones
		// when there are several.
		QSet<int> lines;
		int first = -1;
		for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
			if (ref.kind == LevelMapSelectionKind::DoomLinedef) {
				lines.insert(ref.objectId);
				first = first < 0 ? ref.objectId : first;
			}
		}
		for (const LevelMapDoomLinedef& line : std::as_const(m_levelMapDocument.doomLinedefs)) {
			if (line.id == first) {
				request.sidedef = line.frontSidedef >= 0 ? line.frontSidedef : line.backSidedef;
			}
		}
		if (request.sidedef < 0 || !levelDoomSideTexturedPart(m_levelMapDocument, request.sidedef, &request.part)) {
			statusBar()->showMessage(tr("Select a linedef whose wall shows a texture, then the walls to align with it."));
			return;
		}
		if (lines.size() > 1) {
			request.within = lines;
		}
	}
	for (const auto& material : std::as_const(m_levelPreviewAssets.materials)) {
		if (material.ready() && material.key.startsWith(QStringLiteral("textures/"))) {
			request.widths.insert(material.key.mid(9).toUpper(), material.sourceSize.width());
		}
	}
	int aligned = 0;
	QString error;
	if (!alignLevelMapDoomWallTextures(&m_levelMapDocument, request, &aligned, &error)) {
		statusBar()->showMessage(tr("Could not align the textures: %1").arg(error), 5000);
		return;
	}
	recordActivity(tr("Wall textures aligned"), QStringLiteral("side:%1").arg(request.sidedef), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(aligned > 0 ? tr("Aligned %n walls with sidedef %1.", nullptr, aligned).arg(request.sidedef)
										 : tr("The joined walls already line up."),
		3000);
}

void ApplicationShell::setLevelShearMode(bool enabled)
{
	const bool editable = m_levelMapDocument.format != LevelMapFormat::Unknown;
	if (enabled && !editable) {
		statusBar()->showMessage(tr("Open a map to shear its objects."));
		enabled = false;
	}
	if (enabled && !levelMap2DShowing()) {
		setLevelMap3D(false);
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		const QSignalBlocker blocker(view);
		view->setShearMode(enabled);
	}
	if (QAction* tool = m_commands ? m_commands->action(QStringLiteral("map.shearTool")) : nullptr) {
		const QSignalBlocker blocker(tool);
		tool->setChecked(enabled);
	}
	if (enabled) {
		statusBar()->showMessage(tr("Shear Tool on: drag the handle in the middle of a side of the selection in a 2D view. Escape turns it off."),
			6000);
	}
}

void ApplicationShell::setLevelMakeSectorMode(bool enabled)
{
	if (enabled && !levelMapDoomEditable()) {
		statusBar()->showMessage(tr("Open a Doom or Hexen map to make sectors."));
		enabled = false;
	}
	if (enabled && !levelMap2DShowing()) {
		setLevelMap3D(false);
	}
	if (enabled && m_levelMapProjection && m_levelMapProjection->currentIndex() != 0) {
		m_levelMapProjection->setCurrentIndex(0);
	}
	if (m_levelMapViewport) {
		const QSignalBlocker blocker(m_levelMapViewport);
		m_levelMapViewport->setMakeSectorMode(enabled);
		if (enabled) {
			m_levelMapViewport->setFocus(Qt::ShortcutFocusReason);
		}
	}
	if (QAction* tool = m_commands ? m_commands->action(QStringLiteral("map.makeSectorMode")) : nullptr) {
		const QSignalBlocker blocker(tool);
		tool->setChecked(enabled);
	}
	if (enabled) {
		statusBar()->showMessage(tr("Make Sector Mode on: click inside a closed shape of lines in the Top view. Escape turns it off."), 6000);
	}
}

void ApplicationShell::makeLevelSectorFromViewport(const QPointF& point)
{
	int sector = -1;
	QString error;
	if (!makeLevelMapDoomSectorAt(&m_levelMapDocument, point.x(), point.y(), &sector, &error)) {
		statusBar()->showMessage(tr("Could not make a sector: %1").arg(error), 5000);
		return;
	}
	recordActivity(tr("Level map sector made"), QStringLiteral("sector:%1").arg(sector), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit; run a node builder before playing"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Made sector:%1 from the lines around the point. Rebuild the nodes before playing.").arg(sector), 5000);
}

void ApplicationShell::shearLevelMapSelectionFromViewport(int axis, int along, double factor, double anchor)
{
	QString error;
	const LevelMapTextureLockOptions textures {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	bool unlocked = false;
	if (!shearLevelMapSelectionAbout(&m_levelMapDocument, axis, along, factor, anchor, textures, &error)) {
		// Classic face formats cannot slant a texture with its face; the tool
		// shears the geometry anyway rather than refuse the drag.
		QString retry;
		if (!textures.enabled
			|| !shearLevelMapSelectionAbout(&m_levelMapDocument, axis, along, factor, anchor, {false, false}, &retry)) {
			statusBar()->showMessage(tr("Could not shear the selection: %1").arg(error), 5000);
			return;
		}
		unlocked = true;
	}
	recordActivity(tr("Level map selection sheared"), QString::number(m_levelMapDocument.selection.size()), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	const QStringList axes {tr("X"), tr("Y"), tr("Z")};
	statusBar()->showMessage(unlocked ? tr("Sheared along %1 by %2 for each unit of %3, without texture lock: this map's face format cannot slant "
										   "textures with their faces.")
											.arg(axes.value(axis))
											.arg(factor, 0, 'g', 4)
											.arg(axes.value(along))
									  : tr("Sheared along %1 by %2 for each unit of %3.").arg(axes.value(axis)).arg(factor, 0, 'g', 4).arg(axes.value(along)),
		5000);
}

void ApplicationShell::setLevelTextureDragMode(bool enabled)
{
	if (enabled && !levelMapDoomEditable()) {
		statusBar()->showMessage(tr("Open a Doom or Hexen format map to drag wall textures."));
		enabled = false;
	}
	if (enabled && !levelMap3DShowing()) {
		setLevelMap3D(true);
	}
	m_levelTextureDragMode = enabled;
	m_levelTextureDrag.reset();
	if (QAction* tool = m_commands ? m_commands->action(QStringLiteral("map.dragCameraTextures")) : nullptr) {
		const QSignalBlocker blocker(tool);
		tool->setChecked(enabled);
	}
	if (enabled) {
		statusBar()->showMessage(tr("Drag Textures on: drag a wall's texture with the left button in the camera; Escape puts it back."), 6000);
	}
}

bool ApplicationShell::handleLevelTextureDragEvent(QEvent* event)
{
	if (!m_levelMap3D) {
		return false;
	}
	const auto set = [this](const char* key, int value, int* steps) {
		QString error;
		if (setLevelMapSidedefProperty(&m_levelMapDocument, m_levelTextureDrag->sidedef, QString::fromLatin1(key), QString::number(value), &error)) {
			++*steps;
			return true;
		}
		statusBar()->showMessage(tr("Could not move the texture: %1").arg(error), 5000);
		return false;
	};
	// Takes back the steps a drag made, leaving no redo behind.
	const auto restore = [this]() {
		for (int step = 0; step < m_levelTextureDrag->steps; ++step) {
			undoLevelMapEdit(&m_levelMapDocument);
		}
		m_levelMapDocument.redoStack.clear();
		m_levelTextureDrag.reset();
		refreshLevelMap3D();
	};
	switch (event->type()) {
	case QEvent::MouseButtonPress: {
		const auto* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() != Qt::LeftButton || mouse->modifiers() != Qt::NoModifier || m_levelMap3D->isRendering()) {
			return false;
		}
		CameraSurfacePoint surface;
		int triangle = -1;
		if (!m_levelMap3D->surfacePointAt(mouse->position(), &surface, &triangle) || triangle < 0 || triangle >= m_levelPreviewMaterialTargets.size()) {
			return false;
		}
		const LevelMaterialTarget target = m_levelPreviewMaterialTargets.at(triangle);
		if (target.kind != LevelMaterialKind::WallUpper && target.kind != LevelMaterialKind::WallLower && target.kind != LevelMaterialKind::WallMiddle) {
			return false;
		}
		// Along the wall the way its texture runs: start to end on a front side,
		// end to start on a back one.
		QHash<int, QPointF> vertices;
		for (const LevelMapDoomVertex& vertex : std::as_const(m_levelMapDocument.doomVertices)) {
			vertices.insert(vertex.id, {vertex.x, vertex.y});
		}
		QPointF along;
		for (const LevelMapDoomLinedef& line : std::as_const(m_levelMapDocument.doomLinedefs)) {
			if (line.frontSidedef == target.objectId || line.backSidedef == target.objectId) {
				const QPointF a = vertices.value(line.startVertex);
				const QPointF b = vertices.value(line.endVertex);
				along = line.frontSidedef == target.objectId ? b - a : a - b;
				break;
			}
		}
		const double length = std::hypot(along.x(), along.y());
		if (length <= 0.0) {
			return false;
		}
		LevelTextureDrag drag;
		drag.sidedef = target.objectId;
		for (const LevelMapDoomSidedef& side : std::as_const(m_levelMapDocument.doomSidedefs)) {
			if (side.id == target.objectId) {
				drag.offsetX = qRound(side.offsetX);
				drag.offsetY = qRound(side.offsetY);
			}
		}
		drag.origin = surface.position;
		drag.normal = surface.normal;
		drag.along = {along.x() / length, along.y() / length, 0.0};
		m_levelTextureDrag = drag;
		statusBar()->showMessage(tr("Dragging the texture of sidedef %1: offsets %2, %3.").arg(drag.sidedef).arg(drag.offsetX).arg(drag.offsetY));
		return true;
	}
	case QEvent::MouseMove: {
		if (!m_levelTextureDrag) {
			return false;
		}
		const auto* mouse = static_cast<QMouseEvent*>(event);
		// Where the pointer's ray meets the wall's plane.
		const ModelPickRay ray = m_levelMap3D->viewRay(mouse->position());
		const auto& o = m_levelTextureDrag->origin;
		const auto& n = m_levelTextureDrag->normal;
		const double facing = ray.direction.x * n[0] + ray.direction.y * n[1] + ray.direction.z * n[2];
		if (std::abs(facing) < 1e-6) {
			return true;
		}
		const double t = ((o[0] - ray.origin.x) * n[0] + (o[1] - ray.origin.y) * n[1] + (o[2] - ray.origin.z) * n[2]) / facing;
		const double dx = ray.origin.x + t * ray.direction.x - o[0];
		const double dy = ray.origin.y + t * ray.direction.y - o[1];
		const double dz = ray.origin.z + t * ray.direction.z - o[2];
		const auto& a = m_levelTextureDrag->along;
		// The texture follows the pointer: a larger X offset slides it back
		// along the wall, a larger Y offset up.
		const int movedX = -qRound(dx * a[0] + dy * a[1]);
		const int movedY = qRound(dz);
		if (movedX == m_levelTextureDrag->movedX && movedY == m_levelTextureDrag->movedY) {
			return true;
		}
		int steps = m_levelTextureDrag->steps;
		const bool moved = (movedX == m_levelTextureDrag->movedX || set("offsetx", m_levelTextureDrag->offsetX + movedX, &steps))
			&& (movedY == m_levelTextureDrag->movedY || set("offsety", m_levelTextureDrag->offsetY + movedY, &steps));
		m_levelTextureDrag->steps = steps;
		if (moved) {
			m_levelTextureDrag->movedX = movedX;
			m_levelTextureDrag->movedY = movedY;
			refreshLevelMap3D();
			statusBar()->showMessage(tr("Dragging the texture of sidedef %1: offsets %2, %3.")
					.arg(m_levelTextureDrag->sidedef)
					.arg(m_levelTextureDrag->offsetX + movedX)
					.arg(m_levelTextureDrag->offsetY + movedY));
		}
		return true;
	}
	case QEvent::MouseButtonRelease: {
		const auto* mouse = static_cast<QMouseEvent*>(event);
		if (!m_levelTextureDrag || mouse->button() != Qt::LeftButton) {
			return false;
		}
		const LevelTextureDrag drag = *m_levelTextureDrag;
		m_levelTextureDrag.reset();
		if (drag.steps > 0) {
			collapseLevelMapUndoSteps(&m_levelMapDocument, drag.steps, tr("Drag the texture of sidedef %1").arg(drag.sidedef),
				tr("Put back the texture of sidedef %1").arg(drag.sidedef));
			recordActivity(tr("Wall texture dragged in the camera"), QStringLiteral("side:%1").arg(drag.sidedef), QStringLiteral("level-map"),
				OperationState::Warning, tr("Unsaved map edit"));
			refreshLevelMapWorkbench();
			statusBar()->showMessage(tr("Sidedef %1 offsets are now %2, %3.").arg(drag.sidedef).arg(drag.offsetX + drag.movedX).arg(drag.offsetY + drag.movedY),
				3000);
		}
		return true;
	}
	case QEvent::KeyPress: {
		const auto* key = static_cast<QKeyEvent*>(event);
		if (key->key() != Qt::Key_Escape) {
			return false;
		}
		if (m_levelTextureDrag) {
			restore();
			statusBar()->showMessage(tr("The texture is back where it was."), 3000);
		} else {
			setLevelTextureDragMode(false);
		}
		return true;
	}
	default:
		return false;
	}
}

void ApplicationShell::nudgeLevelCameraTextureFromUi(int dx, int dy)
{
	LevelMaterialTarget target;
	if (!levelCameraWallAtCrosshair(&target)) {
		return;
	}
	int offsetX = 0;
	int offsetY = 0;
	bool found = false;
	for (const LevelMapDoomSidedef& side : std::as_const(m_levelMapDocument.doomSidedefs)) {
		if (side.id == target.objectId) {
			// Sidedef offsets are whole units; a fractional UDMF offset rounds first.
			offsetX = qRound(side.offsetX);
			offsetY = qRound(side.offsetY);
			found = true;
			break;
		}
	}
	QString error;
	const bool horizontal = dx != 0;
	if (!found
		|| !setLevelMapSidedefProperty(&m_levelMapDocument, target.objectId, horizontal ? QStringLiteral("offsetx") : QStringLiteral("offsety"),
			QString::number(horizontal ? offsetX + dx : offsetY + dy), &error)) {
		statusBar()->showMessage(tr("Could not move the texture: %1").arg(error), 5000);
		return;
	}
	recordActivity(tr("Wall texture moved from the camera"), QStringLiteral("side:%1").arg(target.objectId), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Sidedef %1 offsets are now %2, %3.").arg(target.objectId).arg(horizontal ? offsetX + dx : offsetX)
								 .arg(horizontal ? offsetY : offsetY + dy),
		3000);
}

QString ApplicationShell::levelSelectionSceneGroup() const
{
	// The one group every selected object belongs to, directly or through the
	// entity that owns it.
	const LevelMapDocument& document = m_levelMapDocument;
	if (document.selection.isEmpty() || document.format == LevelMapFormat::DoomWad) {
		return {};
	}
	QString group;
	for (const LevelMapSelectionRef& ref : document.selection) {
		QString holder = levelSceneMembership(document.scene, levelSceneCanonicalObject(document, levelMapSelectionRefId(ref)));
		if (holder.isEmpty() && (ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch)) {
			int owner = -1;
			if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
				for (const LevelMapBrush& brush : document.brushes) {
					if (brush.id == ref.objectId) {
						owner = brush.entityId;
						break;
					}
				}
			} else {
				for (const LevelMapPatch& patch : document.patches) {
					if (patch.id == ref.objectId) {
						owner = patch.entityId;
						break;
					}
				}
			}
			if (owner >= 0) {
				holder = levelSceneMembership(document.scene, QStringLiteral("entity:%1").arg(owner));
			}
		}
		const LevelSceneNode* node = levelSceneNode(document.scene, holder);
		if (!node || node->kind != LevelSceneNodeKind::Group || (!group.isEmpty() && holder != group)) {
			return {};
		}
		group = holder;
	}
	return group;
}

void ApplicationShell::createLinkedLevelCopyFromUi()
{
	const QString group = levelSelectionSceneGroup();
	if (group.isEmpty()) {
		statusBar()->showMessage(tr("Select objects of one scene group first; the Outliner's Scene section makes and fills groups."));
		return;
	}
	const LevelMapVec3 offset = levelLinkedCopyOffset(m_levelMapDocument, group);
	QString created;
	QString error;
	if (!createLinkedLevelGroup(&m_levelMapDocument, group, offset, &created, &error)) {
		statusBar()->showMessage(tr("Could not make a linked copy: %1").arg(error), 5000);
		return;
	}
	const LevelSceneNode* copy = levelSceneNode(m_levelMapDocument.scene, created);
	recordActivity(tr("Linked copy made"), copy ? copy->name : created, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("%1 is a linked copy %2 units along x; edits inside either reach both.").arg(copy ? copy->name : created).arg(offset.x),
		5000);
}

void ApplicationShell::selectLinkedLevelCopiesFromUi()
{
	QVector<LevelMapSelectionRef> members;
	int copies = 0;
	QSet<QString> seen;
	for (const QString& group : levelLinkedGroupsOfSelection(m_levelMapDocument)) {
		for (const QString& id : levelLinkedGroupNodes(m_levelMapDocument.scene, group)) {
			if (!seen.contains(id)) {
				seen.insert(id);
				members += levelSceneSelection(m_levelMapDocument, id);
				++copies;
			}
		}
	}
	if (members.isEmpty()) {
		statusBar()->showMessage(tr("Select part of a linked group first."));
		return;
	}
	setLevelMapSelection(&m_levelMapDocument, members);
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Selected %n linked copies.", nullptr, copies), 3000);
}

void ApplicationShell::updateLinkedLevelCopiesFromUi()
{
	const QStringList groups = levelLinkedGroupsOfSelection(m_levelMapDocument);
	if (groups.size() != 1) {
		statusBar()->showMessage(groups.isEmpty() ? tr("Select part of a linked group first.")
												  : tr("Select part of one linked copy: the one the others should match."));
		return;
	}
	int updated = 0;
	QString error;
	if (!updateLinkedLevelGroups(&m_levelMapDocument, groups.first(), &updated, &error)) {
		statusBar()->showMessage(tr("Could not update the linked copies: %1").arg(error), 5000);
		return;
	}
	recordActivity(tr("Linked copies updated"), groups.first(), QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Updated %n linked copies to match.", nullptr, updated), 3000);
}

void ApplicationShell::unlinkLevelCopyFromUi()
{
	const QStringList groups = levelLinkedGroupsOfSelection(m_levelMapDocument);
	if (groups.size() != 1) {
		statusBar()->showMessage(groups.isEmpty() ? tr("Select part of a linked group first.") : tr("Select part of one linked copy to separate."));
		return;
	}
	QString error;
	if (!unlinkLevelGroup(&m_levelMapDocument, groups.first(), &error)) {
		statusBar()->showMessage(tr("Could not separate the copy: %1").arg(error), 5000);
		return;
	}
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("The copy is separate now; edits to it stay with it."), 3000);
}

void ApplicationShell::replaceLevelMapKeyValuesFromUi()
{
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!quake) {
		statusBar()->showMessage(tr("Open a Quake-family map to replace key values."));
		return;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelReplaceKeysDialog"));
	dialog.setWindowTitle(tr("Replace Key Values"));
	auto* form = new QFormLayout(&dialog);
	auto* key = new QComboBox;
	key->setObjectName(QStringLiteral("levelReplaceKey"));
	key->setEditable(true);
	key->setAccessibleName(tr("Key"));
	QStringList keys;
	for (const LevelMapEntity& entity : std::as_const(m_levelMapDocument.entities)) {
		for (const LevelMapProperty& property : entity.properties) {
			if (!keys.contains(property.key, Qt::CaseInsensitive)) {
				keys << property.key;
			}
		}
	}
	keys.sort(Qt::CaseInsensitive);
	key->addItems(keys);
	key->setCurrentText(keys.contains(QStringLiteral("targetname")) ? QStringLiteral("targetname") : keys.value(0));
	auto* find = new QLineEdit;
	find->setObjectName(QStringLiteral("levelReplaceFind"));
	find->setAccessibleName(tr("Value to find"));
	auto* replacement = new QLineEdit;
	replacement->setObjectName(QStringLiteral("levelReplaceWith"));
	replacement->setAccessibleName(tr("Replacement value"));
	auto* whole = new QCheckBox(tr("Match the whole value"));
	whole->setObjectName(QStringLiteral("levelReplaceWhole"));
	whole->setChecked(true);
	whole->setToolTip(tr("Off: replace the text wherever it appears in the value, such as a name's prefix."));
	auto* selected = new QCheckBox(tr("Only the selected entities"));
	selected->setObjectName(QStringLiteral("levelReplaceSelected"));
	bool anySelected = false;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		anySelected = anySelected || ref.kind == LevelMapSelectionKind::Entity;
	}
	selected->setEnabled(anySelected);
	auto* count = new QLabel;
	count->setObjectName(QStringLiteral("levelReplaceCount"));
	count->setWordWrap(true);
	form->addRow(tr("Key"), key);
	form->addRow(tr("Find"), find);
	form->addRow(tr("Replace with"), replacement);
	form->addRow(whole);
	form->addRow(selected);
	form->addRow(count);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Replace"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	// The count of matching entities follows every change.
	const auto recount = [this, key, find, whole, selected, count, buttons]() {
		const int matches = static_cast<int>(
			levelMapEntitiesWithValue(m_levelMapDocument, key->currentText(), find->text(), whole->isChecked(), selected->isChecked()).size());
		count->setText(tr("%n entit(y)(ies) match.", nullptr, matches));
		buttons->button(QDialogButtonBox::Ok)->setEnabled(matches > 0);
	};
	connect(key, &QComboBox::currentTextChanged, &dialog, recount);
	connect(find, &QLineEdit::textChanged, &dialog, recount);
	connect(whole, &QCheckBox::toggled, &dialog, recount);
	connect(selected, &QCheckBox::toggled, &dialog, recount);
	recount();
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	int replaced = 0;
	QString error;
	if (!replaceLevelMapEntityValues(&m_levelMapDocument, key->currentText(), find->text(), replacement->text(), whole->isChecked(),
			selected->isChecked(), &replaced, &error)) {
		statusBar()->showMessage(tr("Could not replace the values: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Key values replaced"), key->currentText(), QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Replaced %1 on %n entit(y)(ies).", nullptr, replaced).arg(key->currentText()), 5000);
}

void ApplicationShell::shearLevelMapSelectionFromUi()
{
	if (m_levelMapDocument.selection.isEmpty() || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Select objects to shear."));
		return;
	}
	const bool doom = m_levelMapDocument.format == LevelMapFormat::DoomWad;
	// By default the view's right slides with its up: X with Y in Top.
	int right = 0;
	int up = 1;
	if (!doom && m_levelMapViewport->projection() == MapViewportProjection::FrontXZ) {
		up = 2;
	} else if (!doom && m_levelMapViewport->projection() == MapViewportProjection::SideZY) {
		right = 1;
		up = 2;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelShearDialog"));
	dialog.setWindowTitle(tr("Shear Selection"));
	auto* form = new QFormLayout(&dialog);
	const QStringList axes {tr("X"), tr("Y"), tr("Z")};
	auto* slide = new QComboBox;
	slide->setObjectName(QStringLiteral("levelShearAxis"));
	auto* along = new QComboBox;
	along->setObjectName(QStringLiteral("levelShearAlong"));
	for (int axis = 0; axis < (doom ? 2 : 3); ++axis) {
		slide->addItem(axes.at(axis), axis);
		along->addItem(axes.at(axis), axis);
	}
	slide->setCurrentIndex(right);
	along->setCurrentIndex(up);
	slide->setAccessibleName(tr("Axis the points slide along"));
	along->setAccessibleName(tr("Axis whose distance sets how far"));
	auto* angle = new QDoubleSpinBox;
	angle->setObjectName(QStringLiteral("levelShearAngle"));
	angle->setRange(-75.0, 75.0);
	angle->setDecimals(1);
	angle->setValue(15.0);
	angle->setSuffix(QStringLiteral("°"));
	angle->setAccessibleName(tr("Shear angle"));
	form->addRow(tr("Slide along"), slide);
	form->addRow(tr("By distance along"), along);
	form->addRow(tr("Angle"), angle);
	auto* note = new QLabel(tr("Points slide by the angle's tangent times their distance from the selection's centre. Texture lock follows the "
								 "Texture Lock setting."));
	note->setWordWrap(true);
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Shear"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	const int axis = slide->currentData().toInt();
	const int by = along->currentData().toInt();
	QString error;
	if (!shearLevelMapSelection(&m_levelMapDocument, axis, by, std::tan(angle->value() * std::numbers::pi / 180.0),
			{m_settings.levelTextureLock(), m_settings.levelAllowValve220()}, &error)) {
		statusBar()->showMessage(tr("Could not shear: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Level map selection sheared"), QStringLiteral("%1 by %2").arg(axes.at(axis), axes.at(by)), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Sheared the selection along %1 by %2.").arg(axes.at(axis), axes.at(by)), 4000);
}

void ApplicationShell::curveLevelMapLinedefsFromUi()
{
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelCurveDialog"));
	dialog.setWindowTitle(tr("Curve Linedefs"));
	auto* form = new QFormLayout(&dialog);
	auto* segments = new QSpinBox;
	segments->setObjectName(QStringLiteral("levelCurveSegments"));
	segments->setRange(2, 64);
	segments->setValue(8);
	segments->setAccessibleName(tr("Segments in each curve"));
	auto* bulge = new QSpinBox;
	bulge->setObjectName(QStringLiteral("levelCurveBulge"));
	bulge->setRange(-16384, 16384);
	bulge->setValue(64);
	bulge->setAccessibleName(tr("How far the middle of each curve bulges"));
	form->addRow(tr("Segments"), segments);
	form->addRow(tr("Bulge"), bulge);
	auto* note = new QLabel(tr("A positive bulge curves towards each linedef's front side, a negative one towards its back. Rebuild the nodes "
								 "before playing."));
	note->setWordWrap(true);
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Curve"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	int curved = 0;
	QString error;
	if (!curveLevelMapLinedefs(&m_levelMapDocument, segments->value(), bulge->value(), &curved, &error)) {
		statusBar()->showMessage(tr("Could not curve the linedefs: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Level map linedefs curved"), QString::number(curved), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit; run a node builder before playing"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Curved %n linedef(s). Rebuild the nodes before playing the map.", nullptr, curved), 5000);
}

void ApplicationShell::alignLevelMapSelectionFromUi(bool horizontal, int edge)
{
	if (!m_levelMapViewport) {
		return;
	}
	// The active 2D view's right and up: X and Y in Top, X and Z in Front,
	// Y and Z in Side.
	int axis = horizontal ? 0 : 1;
	switch (m_levelMapViewport->projection()) {
	case MapViewportProjection::TopXY:
		break;
	case MapViewportProjection::FrontXZ:
		axis = horizontal ? 0 : 2;
		break;
	case MapViewportProjection::SideZY:
		axis = horizontal ? 1 : 2;
		break;
	}
	int moved = 0;
	QString error;
	if (!alignLevelMapSelection(&m_levelMapDocument, axis, static_cast<LevelMapAlignEdge>(edge), &moved, &error)) {
		statusBar()->showMessage(tr("Could not align: %1").arg(error), 5000);
		return;
	}
	if (moved == 0) {
		statusBar()->showMessage(tr("The selection is already aligned."), 4000);
		return;
	}
	recordActivity(tr("Level map objects aligned"), QStringList {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}.at(axis),
		QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Aligned %n object(s) along %1.", nullptr, moved).arg(QStringList {tr("X"), tr("Y"), tr("Z")}.at(axis)), 4000);
}

void ApplicationShell::refreshLevelEditingCommands()
{
	if (!m_commands) {
		return;
	}
	// Read through a const view: iterating the document's own arrays would
	// detach them from the Objects model's shared copy.
	const LevelMapDocument& document = m_levelMapDocument;
	const bool textMap = document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
	bool brushes = false;
	int brushCount = 0;
	bool pointEntity = false;
	bool ownedGeometry = false;
	QSet<int> owners;
	for (const LevelMapBrush& brush : document.brushes) {
		owners.insert(brush.entityId);
	}
	for (const LevelMapPatch& patch : document.patches) {
		owners.insert(patch.entityId);
	}
	int world = -1;
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			world = entity.id;
			break;
		}
	}
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes = true;
			++brushCount;
			for (const LevelMapBrush& brush : document.brushes) {
				if (brush.id == ref.objectId) {
					ownedGeometry = ownedGeometry || brush.entityId != world;
					break;
				}
			}
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			brushes = true;
		} else if (ref.kind == LevelMapSelectionKind::Entity && ref.objectId != world) {
			if (owners.contains(ref.objectId)) {
				brushes = true;
				ownedGeometry = true;
				brushCount += 2;
			} else {
				pointEntity = true;
			}
		}
	}
	const bool hasSelection = !document.selection.isEmpty();
	m_commands->setEnabled(QStringLiteral("map.tieToEntity"), textMap && brushes);
	m_commands->setEnabled(QStringLiteral("map.moveToWorld"), textMap && ownedGeometry);
	for (const auto& id : {QStringLiteral("map.selectInside"), QStringLiteral("map.selectTouching"), QStringLiteral("map.selectCompleteTall"),
			 QStringLiteral("map.selectPartialTall")}) {
		m_commands->setEnabled(id, m_levelMapDocument.format != LevelMapFormat::Unknown && hasSelection);
	}
	m_commands->setEnabled(QStringLiteral("map.makeDetail"), textMap && brushes);
	const bool alignable = document.format != LevelMapFormat::Unknown && document.selection.size() >= 2;
	m_commands->setEnabled(QStringLiteral("map.shearSelection"), document.format != LevelMapFormat::Unknown && hasSelection);
	m_commands->setEnabled(QStringLiteral("map.shearTool"), document.format != LevelMapFormat::Unknown);
	m_commands->setEnabled(QStringLiteral("map.replaceKeyValues"), textMap);
	const bool inLink = textMap && !levelLinkedGroupsOfSelection(document).isEmpty();
	m_commands->setEnabled(QStringLiteral("map.createLinkedCopy"), textMap && !levelSelectionSceneGroup().isEmpty());
	m_commands->setEnabled(QStringLiteral("map.selectLinkedCopies"), inLink);
	m_commands->setEnabled(QStringLiteral("map.updateLinkedCopies"), inLink);
	m_commands->setEnabled(QStringLiteral("map.unlinkCopy"), inLink);
	m_commands->setEnabled(QStringLiteral("map.curveLinedefs"), levelMapSelectionHasLinedefs());
	m_commands->setEnabled(QStringLiteral("map.alignWallTextures"), levelMapDoomEditable() && levelMapSelectionHasLinedefs());
	m_commands->setEnabled(QStringLiteral("map.makeSectorMode"), levelMapDoomEditable());
	if (m_levelTextureDragMode && !levelMapDoomEditable()) {
		setLevelTextureDragMode(false);
	}
	for (const auto& id : {QStringLiteral("map.raiseCameraSurface"), QStringLiteral("map.lowerCameraSurface"), QStringLiteral("map.brightenCameraSurface"),
			 QStringLiteral("map.darkenCameraSurface"), QStringLiteral("map.nudgeCameraTextureLeft"), QStringLiteral("map.nudgeCameraTextureRight"),
			 QStringLiteral("map.nudgeCameraTextureUp"), QStringLiteral("map.nudgeCameraTextureDown"), QStringLiteral("map.alignWallTexturesCrosshair"),
			 QStringLiteral("map.dragCameraTextures")}) {
		m_commands->setEnabled(id, levelMapDoomEditable());
	}
	for (const auto& id : {QStringLiteral("map.alignLeft"), QStringLiteral("map.alignRight"), QStringLiteral("map.alignTop"),
			 QStringLiteral("map.alignBottom"), QStringLiteral("map.alignCentreHorizontal"), QStringLiteral("map.alignCentreVertical")}) {
		m_commands->setEnabled(id, alignable);
	}
	m_commands->setEnabled(QStringLiteral("map.regionSetSelection"), textMap && hasSelection);
	m_commands->setEnabled(QStringLiteral("map.regionSetView"), textMap);
	m_commands->setEnabled(QStringLiteral("map.regionClear"), levelRegionActive());
	m_commands->setEnabled(QStringLiteral("map.regionSave"), levelRegionActive());
	m_commands->setEnabled(QStringLiteral("map.regionCompile"), levelRegionActive() && !document.sourcePath.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.makeStructural"), textMap && brushes);
	m_commands->setEnabled(QStringLiteral("map.intersect"), textMap && brushCount >= 2);
	m_commands->setEnabled(QStringLiteral("map.dropToFloor"), textMap && pointEntity);
}

void ApplicationShell::tieLevelMapSelectionFromUi(const QString& className)
{
	QString name = className.trimmed();
	if (name.isEmpty()) {
		QStringList choices = levelBrushEntityClasses();
		const QString preferred = m_lastTiedEntityClass.isEmpty() ? QStringLiteral("func_group") : m_lastTiedEntityClass;
		if (!choices.contains(preferred, Qt::CaseInsensitive)) {
			choices.prepend(preferred);
		}
		bool ok = false;
		name = QInputDialog::getItem(this, tr("Make Brush Entity"), tr("Class of the new brush entity"), choices,
			static_cast<int>(std::max<qsizetype>(0, choices.indexOf(preferred))), true, &ok)
				   .trimmed();
		if (!ok || name.isEmpty()) {
			return;
		}
	}
	int entityId = -1;
	QString error;
	if (!tieLevelMapSelectionToEntity(&m_levelMapDocument, name, {}, &entityId, &error)) {
		statusBar()->showMessage(tr("Could not make the brush entity: %1").arg(error), 6000);
		return;
	}
	m_lastTiedEntityClass = name;
	recordActivity(tr("Brush entity made"), QStringLiteral("entity:%1 %2").arg(entityId).arg(name), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	// Its keys come next, so the inspector shows it.
	showLevelSidebarPage(QStringLiteral("inspector"));
	statusBar()->showMessage(tr("Made %1 (entity:%2). Its keys are in the Inspector; Undo puts the brushes back.").arg(name).arg(entityId), 6000);
}

void ApplicationShell::moveLevelMapSelectionToWorldFromUi()
{
	int moved = 0;
	QString error;
	if (!moveLevelMapSelectionToWorld(&m_levelMapDocument, &moved, &error)) {
		statusBar()->showMessage(tr("Could not move to the world: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Brushes moved to the world"), tr("%n brush(es)", nullptr, moved), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Moved %n brush(es) to the world; their entities went with their keys.", nullptr, moved), 6000);
}

void ApplicationShell::selectLevelMapRegionFromUi(LevelMapRegionSelection mode)
{
	// The tall selections look down the active 2D view.
	const int projection = m_levelMapViewport ? static_cast<int>(m_levelMapViewport->projection()) : 0;
	const int axis = projection == 0 ? 2 : (projection == 1 ? 1 : 0);
	QString error;
	const QVector<LevelMapSelectionRef> found = levelMapRegionSelection(m_levelMapDocument, mode, axis, &error);
	if (!error.isEmpty()) {
		statusBar()->showMessage(error, 5000);
		return;
	}
	int leftHidden = 0;
	const QVector<LevelMapSelectionRef> objects = visibleLevelMapObjects(found, &leftHidden);
	if (objects.isEmpty()) {
		statusBar()->showMessage(leftHidden > 0 ? tr("Only hidden objects are there; Show All brings them back.") : tr("Nothing else is there."), 5000);
		return;
	}
	if (!setLevelMapSelection(&m_levelMapDocument, objects, &error)) {
		statusBar()->showMessage(error, 5000);
		return;
	}
	refreshLevelMapWorkbench();
	showLevelSelectionMessage(leftHidden > 0 ? tr("Selected %n object(s); hidden ones were left out.", nullptr, static_cast<int>(objects.size()))
											 : tr("Selected %n object(s).", nullptr, static_cast<int>(objects.size())));
}

void ApplicationShell::setLevelMapDetailFromUi(bool detail)
{
	int changed = 0;
	QString error;
	if (!setLevelMapSelectionDetail(&m_levelMapDocument, detail, &changed, &error)) {
		statusBar()->showMessage(error, 6000);
		return;
	}
	recordActivity(detail ? tr("Brushes made detail") : tr("Brushes made structural"), tr("%n brush(es)", nullptr, changed), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	const bool flags = levelMapUsesFaceFlags(m_levelMapDocument);
	statusBar()->showMessage(detail ? (flags ? tr("Made %n brush(es) detail with the detail content flag.", nullptr, changed)
											 : tr("Made %n brush(es) detail as a func_detail entity.", nullptr, changed))
									: tr("Made %n brush(es) structural.", nullptr, changed),
		6000);
}

void ApplicationShell::dropLevelMapSelectionToFloorFromUi()
{
	const EntityDefinitionCatalogue catalogue = levelEntityCatalogue();
	const auto bounds = [&catalogue](const QString& className, LevelMapVec3* mins, LevelMapVec3* maxs) {
		EntityClassDefinition definition;
		if (!catalogue.classForName(className, &definition) || !definition.hasSize) {
			return false;
		}
		*mins = {definition.mins[0], definition.mins[1], definition.mins[2], true};
		*maxs = {definition.maxs[0], definition.maxs[1], definition.maxs[2], true};
		return true;
	};
	int dropped = 0;
	QString error;
	if (!dropLevelMapSelectionToFloor(&m_levelMapDocument, bounds, &dropped, &error)) {
		statusBar()->showMessage(error, 6000);
		return;
	}
	recordActivity(tr("Entities dropped to the floor"), tr("%n entit(y)(ies)", nullptr, dropped), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Dropped %n entit(y)(ies) to the floor as one undo step.", nullptr, dropped), 5000);
}

void ApplicationShell::intersectLevelMapSelectionFromUi()
{
	QString error;
	if (!intersectLevelMapSelection(&m_levelMapDocument, &error)) {
		statusBar()->showMessage(error, 6000);
		return;
	}
	recordActivity(tr("Brushes intersected"), QString(), QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("The brushes gave way to their overlap; Undo brings them back."), 5000);
}

void ApplicationShell::refreshBuiltinEntityDefinitions()
{
	// A project's own definitions always win.
	if (!m_entityDefinitions.isEmpty() && !m_entityDefinitionsBuiltin) {
		return;
	}
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!quake) {
		if (m_entityDefinitionsBuiltin && m_levelMapDocument.format != LevelMapFormat::Unknown) {
			m_entityDefinitions = {};
			m_entityDefinitionsBuiltin = false;
			m_entityDefinitionsBuiltinGame.clear();
		}
		return;
	}
	const BuiltinEntityGame game = builtinEntityGameForMap(m_levelMapDocument);
	const QString id = builtinEntityGameId(game);
	if (m_entityDefinitionsBuiltin && m_entityDefinitionsBuiltinGame == id) {
		return;
	}
	m_entityDefinitions = builtinEntityDefinitions(game);
	m_entityDefinitionsBuiltin = !m_entityDefinitions.isEmpty();
	m_entityDefinitionsBuiltinGame = m_entityDefinitionsBuiltin ? id : QString();
	if (m_entityDefinitionSummary && m_entityDefinitionsBuiltin) {
		const QString requested = m_entityDefinitionPath ? m_entityDefinitionPath->text().trimmed() : QString();
		const int count = static_cast<int>(m_entityDefinitions.classes.size());
		QString summary;
		if (requested.isEmpty()) {
			summary = tr("No project definitions found, so the built-in %1 classes stand in: %n class(es), %2 point and %3 brush. "
						   "Point at a .def, .fgd or .ent file for a mod's own.",
				nullptr, count)
						  .arg(builtinEntityGameDisplayName(game))
						  .arg(m_entityDefinitions.pointClassCount)
						  .arg(m_entityDefinitions.brushClassCount);
		} else {
			summary = tr("Nothing was loaded from %1, so the built-in %2 classes stand in: %n class(es), %3 point and %4 brush.", nullptr, count)
						  .arg(QDir::toNativeSeparators(requested), builtinEntityGameDisplayName(game))
						  .arg(m_entityDefinitions.pointClassCount)
						  .arg(m_entityDefinitions.brushClassCount);
		}
		m_entityDefinitionSummary->setText(summary);
	}
}

void ApplicationShell::placeLevelBrushEntity(const QString& className, const QPointF& viewPoint)
{
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!quake || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a Quake-family map to make brush entities."));
		return;
	}
	bool brushes = false;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		brushes = brushes || ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch;
	}
	// Enter or Place with brushes selected makes them into the class; a drop,
	// or no brushes, brings a new brush of it where it lands.
	if (brushes && viewPoint.x() < 0.0) {
		tieLevelMapSelectionFromUi(className);
		return;
	}
	const QPointF point = viewPoint.x() < 0.0 ? QPointF(m_levelMapViewport->rect().center()) : viewPoint;
	const LevelMapVec3 centre = m_levelMapViewport->worldPositionAt(point, levelMapHiddenAxisValue());
	const double grid = m_levelMapGrid ? std::max(1, m_levelMapGrid->currentData().toInt()) : 16.0;
	const double half = std::max(16.0, grid * 2.0);
	const auto snap = [grid](double value) { return std::round(value / grid) * grid; };
	const LevelMapVec3 mins {snap(centre.x) - half, snap(centre.y) - half, snap(centre.z) - half, true};
	const LevelMapVec3 maxs {snap(centre.x) + half, snap(centre.y) + half, snap(centre.z) + half, true};
	const quint64 revision = m_levelMapDocument.revision;
	int brushId = -1;
	QString error;
	if (!addLevelMapBoxBrush(&m_levelMapDocument, mins, maxs, levelBrushMaterial(), &brushId, &error)) {
		statusBar()->showMessage(tr("Could not place it: %1").arg(error));
		return;
	}
	setLevelMapSelection(&m_levelMapDocument, {{LevelMapSelectionKind::QuakeBrush, brushId}});
	int entityId = -1;
	if (!tieLevelMapSelectionToEntity(&m_levelMapDocument, className, {}, &entityId, &error)) {
		QString ignored;
		if (undoLevelMapEdit(&m_levelMapDocument, &ignored) && !m_levelMapDocument.redoStack.isEmpty()) {
			m_levelMapDocument.redoStack.removeLast();
		}
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Could not place it: %1").arg(error));
		return;
	}
	// The brush and the entity it went into are one step to undo.
	collapseLevelMapUndoSteps(&m_levelMapDocument, static_cast<int>(m_levelMapDocument.revision - revision),
		tr("Place %1").arg(className), tr("Remove the placed %1").arg(className));
	m_lastTiedEntityClass = className;
	recordActivity(tr("Brush entity placed"), QStringLiteral("entity:%1 %2").arg(entityId).arg(className), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Placed a %1 brush; drag its handles to size it, and its keys are in the Inspector.").arg(className), 6000);
}

} // namespace vibestudio
