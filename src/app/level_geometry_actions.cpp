// Geometry, patch, region and navigation tools of the Levels page, after
// the editors mappers know: vertex, edge and face editing in the 2D views
// (Radiant, TrenchBroom), Snap Vertices (TrenchBroom), hollow with touching
// walls and Make Room (NetRadiant Custom), Expand Selection to Whole
// Entities and Find Brush (Radiant), the patch matrix commands and Thicken
// (GtkRadiant, NetRadiant Custom), region drawing and Region Set Brush
// (Radiant), Go to Coordinates (Hammer) and replacing the selection with a
// prefab. Each edit is one undo step from the core, with a `map` command
// doing the same.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/map_viewport_projection.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_brush.h"
#include "core/level_hollow.h"
#include "core/level_patch_tools.h"
#include "core/level_prefab.h"
#include "core/level_selection_tools.h"
#include "core/level_scene.h"
#include "core/level_textured_selection.h"
#include "core/package_staging.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QDir>
#include <QFileInfo>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

struct PatchMatrixCommand {
	const char* id;
	LevelPatchMatrixOperation operation;
	const char* label;
	const char* tip;
};

const QList<PatchMatrixCommand>& patchMatrixCommands()
{
	static const QList<PatchMatrixCommand> commands {
		{"map.patchInsertColumns", LevelPatchMatrixOperation::InsertColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Insert Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Split the first span of each row of the selected patches in two, adding two control columns without changing the surface.")},
		{"map.patchAddColumns", LevelPatchMatrixOperation::AddColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Add Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Split the last span of each row of the selected patches in two, adding two control columns without changing the surface.")},
		{"map.patchInsertRows", LevelPatchMatrixOperation::InsertRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Insert Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Split the first span of each column of the selected patches in two, adding two control rows without changing the surface.")},
		{"map.patchAddRows", LevelPatchMatrixOperation::AddRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Add Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Split the last span of each column of the selected patches in two, adding two control rows without changing the surface.")},
		{"map.patchDeleteFirstColumns", LevelPatchMatrixOperation::DeleteFirstColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Delete First Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Join the first two spans of each row into one through the point they shared, two control columns fewer.")},
		{"map.patchDeleteLastColumns", LevelPatchMatrixOperation::DeleteLastColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Delete Last Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Join the last two spans of each row into one through the point they shared, two control columns fewer.")},
		{"map.patchDeleteFirstRows", LevelPatchMatrixOperation::DeleteFirstRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Delete First Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Join the first two spans of each column into one through the point they shared, two control rows fewer.")},
		{"map.patchDeleteLastRows", LevelPatchMatrixOperation::DeleteLastRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Delete Last Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Join the last two spans of each column into one through the point they shared, two control rows fewer.")},
		{"map.patchTranspose", LevelPatchMatrixOperation::Transpose, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Transpose Patch"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Swap the selected patches' rows and columns, as Radiant's Matrix Transpose does; they then face the other way.")},
		{"map.patchInvert", LevelPatchMatrixOperation::Invert, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Invert Patch Facing"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Turn the selected patches to face the other way.")},
		{"map.patchRedisperseColumns", LevelPatchMatrixOperation::RedisperseColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Re-disperse Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Put each span's middle control point halfway between its ends along the rows, spacing the patch evenly, as Radiant's Re-disperse does.")},
		{"map.patchRedisperseRows", LevelPatchMatrixOperation::RedisperseRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Re-disperse Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Put each span's middle control point halfway between its ends down the columns, spacing the patch evenly.")},
		{"map.patchSmoothColumns", LevelPatchMatrixOperation::SmoothColumns, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Smooth Patch Columns"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Move each joint between spans along the rows halfway between its neighbours, so the spans meet without a crease, as NetRadiant Custom's Smooth does.")},
		{"map.patchSmoothRows", LevelPatchMatrixOperation::SmoothRows, QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Smooth Patch Rows"),
			QT_TRANSLATE_NOOP("VibeStudioLevelGeometry", "Move each joint between spans down the columns halfway between its neighbours, so the spans meet without a crease.")},
	};
	return commands;
}

QByteArray regionKey(const QString& path)
{
	// One remembered region per map file, keyed by its path.
	return QCryptographicHash::hash(QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded().toUtf8(), QCryptographicHash::Sha1).toHex();
}

} // namespace

QStringList ApplicationShell::levelPatchMatrixCommandIds()
{
	QStringList ids;
	for (const PatchMatrixCommand& command : patchMatrixCommands()) {
		ids << QString::fromLatin1(command.id);
	}
	return ids;
}

void ApplicationShell::registerLevelGeometryCommands()
{
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, const QString& section,
						 std::function<void()> handler, bool checkable = false) {
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
		if (checkable) {
			if (QAction* action = m_commands->action(id)) {
				action->setCheckable(true);
			}
		}
	};
	// Vertex, edge and face editing in the 2D views. Each command turns its
	// mode on, or off when the views are in it already: the views, not the
	// action's check mark, which the page refresh resets on the way in.
	const QString components = tr("Vertex Editing");
	const QString groups = tr("Groups");
	add(QStringLiteral("map.wrapMerge"),tr("Wrap/Merge"),
		tr("Build a convex hull, filling gaps. Whole brushes are replaced; picked components form a new brush and retain the originals. Uses current material and any placed clipper points."),
		QStringLiteral("cube"),tr("Geometry"),[this] { wrapLevelMapFromUi(); });
	add(QStringLiteral("map.wrapMergePoints"),tr("Wrap/Merge with Points…"),
		tr("Preview a convex hull with up to three extra XYZ points. Picked components retain their original objects."),
		QStringLiteral("cube"),tr("Geometry"),[this] { wrapLevelMapFromUi(true); });
	add(QStringLiteral("map.openGroup"),tr("Open Group"),tr("Edit the selected group's contents. Objects outside it remain visible and protected."),
		QStringLiteral("folder-open"),groups,[this] { changeLevelGroupContext(levelSelectedClosedGroup()); });
	add(QStringLiteral("map.closeGroup"),tr("Close Group"),tr("Return to the parent group and select the group just closed."),
		QStringLiteral("folder"),groups,[this] { changeLevelGroupContext({}); });
	add(QStringLiteral("map.closeAllGroups"),tr("Close All Groups"),tr("Return to the map and restore the previous creation destination."),
		QStringLiteral("folder"),groups,[this] { changeLevelGroupContext({},true); });
	const auto mode = [this](int componentMode) {
		const int current = m_levelMapViewport ? static_cast<int>(m_levelMapViewport->componentMode()) : 0;
		setLevelComponentMode(current == componentMode ? 0 : componentMode);
	};
	add(QStringLiteral("map.vertexTool"), tr("Edit Vertices"),
		tr("Show the selected brushes' vertices and patches' control points in the plan views and camera: pick and drag them, or nudge them in a plan view with the "
		   "arrow keys, as Radiant's vertex mode and TrenchBroom's vertex tool do. Escape drops the picks, then leaves the mode."),
		QStringLiteral("vertex-tool"), components, [mode]() { mode(1); }, true);
	add(QStringLiteral("map.edgeTool"), tr("Edit Edges"),
		tr("Show the selected brushes' edges as handles in the plan views and camera and move them, as Radiant's edge mode does."), QStringLiteral("edge-tool"),
		components, [mode]() { mode(2); }, true);
	add(QStringLiteral("map.faceTool"), tr("Edit Faces"),
		tr("Show the selected brushes' faces as handles in the plan views and camera and move them, pushing or pulling a face to grow or shrink its brush, as "
		   "Radiant's face mode does. Faces picked by Select Textured move, rotate and scale with them without selecting their brushes."),
		QStringLiteral("face-tool"), components, [mode]() { mode(3); }, true);
	const QString componentSelection = tr("Component Selection");
	const auto cycleComponent = [this](int steps) {
		for (auto* view : std::as_const(m_levelPlanViews)) { view->finishComponentMove(false); }
		if (m_levelMap3D) { m_levelMap3D->finishComponentMove(false); }
		const bool camera = m_levelMap3D && levelMap3DShowing() && (m_levelLastFocusedView == m_levelMap3D || !levelMap2DShowing());
		const bool cycled = camera ? m_levelMap3D->cycleComponentSelection(steps) : m_levelMapViewport && m_levelMapViewport->cycleComponentSelection(steps);
		if (!cycled) { statusBar()->showMessage(tr("No eligible component handles at this position. Move to a handle or select fewer objects."),5000); }
	};
	add(QStringLiteral("map.nextComponent"),tr("Next Overlapping Component"),tr("Cycle towards the next depth at the last component pick in the active view. Coincident geometry stays together. Tab in a viewport also cycles."),
		QStringLiteral("select-box"),componentSelection,[cycleComponent] { cycleComponent(1); });
	add(QStringLiteral("map.previousComponent"),tr("Previous Overlapping Component"),tr("Cycle towards the previous depth at the last component pick in the active view. Shift+Tab in a viewport also cycles."),
		QStringLiteral("select-box"),componentSelection,[cycleComponent] { cycleComponent(-1); });
	add(QStringLiteral("map.componentTouchingEdges"),tr("Select Touching Edges"),tr("Convert picked components to edges incident to any pick. Patch control points are retained."),
		QStringLiteral("edge-tool"),componentSelection,[this] { selectLevelComponentTopology(0,2,true); });
	add(QStringLiteral("map.componentTouchingFaces"),tr("Select Touching Faces"),tr("Convert picked components to faces incident to any pick. Patch control points are retained."),
		QStringLiteral("face-tool"),componentSelection,[this] { selectLevelComponentTopology(0,3,true); });
	add(QStringLiteral("map.growComponents"),tr("Grow Component Selection"),tr("Add one neighbour step within each picked brush or patch. Faces connect across edges; patch points connect along rows and columns."),
		QStringLiteral("select-box"),componentSelection,[this] { selectLevelComponentTopology(1); });
	add(QStringLiteral("map.shrinkComponents"),tr("Shrink Component Selection"),tr("Remove the selection's boundary by one neighbour step. An entirely selected closed brush remains selected; patch borders erode."),
		QStringLiteral("select-box"),componentSelection,[this] { selectLevelComponentTopology(2); });
	add(QStringLiteral("map.connectedComponents"),tr("Select Connected Components"),tr("Select all components connected to the picks within each brush or patch. Other objects remain unchanged."),
		QStringLiteral("select-box"),componentSelection,[this] { selectLevelComponentTopology(3); });
	add(QStringLiteral("map.componentMoveTool"),tr("Move Components"),tr("Drag picked components or the camera's XYZ handles."),QStringLiteral("move"),components,[this] { setLevelComponentTool(0); },true);
	add(QStringLiteral("map.componentRotateTool"),tr("Rotate Components"),tr("Drag an XYZ rotation ring around the shared component pivot."),QStringLiteral("rotate"),components,[this] { setLevelComponentTool(1); },true);
	add(QStringLiteral("map.componentScaleTool"),tr("Scale Components"),tr("Drag an axis handle, or All for uniform scale, around the shared component pivot."),QStringLiteral("resize"),components,[this] { setLevelComponentTool(2); },true);
	add(QStringLiteral("map.componentShearTool"),tr("Shear Components"),tr("Drag X/Y to displace X with distance along Y; the other labelled pairs work in the same way."),QStringLiteral("resize"),components,[this] { setLevelComponentTool(3); },true);
	add(QStringLiteral("map.componentPivotTool"),tr("Move Component Pivot"),tr("Drag XYZ or Pivot to reposition the shared pivot without editing geometry."),QStringLiteral("vertex-tool"),components,[this] { setLevelComponentTool(4); },true);
	add(QStringLiteral("map.componentResetPivot"),tr("Reset Component Pivot"),tr("Use the centre of the picked corners' combined bounds."),QStringLiteral("vertex-tool"),components,[this] { m_levelComponentPivot = {}; refreshLevelComponentGizmos(); refreshLevelGeometryCommands(); });
	add(QStringLiteral("map.componentGizmoOptions"),tr("Component Manipulator Options…"),tr("Set rotation, scale and shear snapping, or enter a shared pivot."),QStringLiteral("settings"),components,[this] { editLevelComponentGizmoOptions(); });
	add(QStringLiteral("map.snapPickedVertex"),tr("Snap Picked Vertex…"),tr("Move one picked geometric vertex exactly onto another existing brush vertex, merging corners when valid."),QStringLiteral("vertex-tool"),components,[this] { snapLevelVertexFromUi(); });
	add(QStringLiteral("map.transformComponents"), tr("Transform Components…"),
		tr("Move, rotate, scale or shear picked components around a shared or custom pivot, with optional snapping and UV lock. In face mode this includes faces picked by Select Textured."),
		QStringLiteral("vertex-tool"), components, [this] { transformLevelComponentsFromUi(); });
	add(QStringLiteral("map.componentUvLock"), tr("Lock Component UVs"),
		tr("Preserve each surviving corner's texture coordinates during component moves, transforms and rounding. Separate from object Texture Lock. "
		   "Where no single projection can keep every corner, Fit UVs When Lock Fails decides between fitting and refusing."),
		QStringLiteral("image"), components, [this] {
			m_settings.setLevelComponentUvLock(!m_settings.levelComponentUvLock());
			if (m_levelComponentsRefresh) { m_levelComponentsRefresh(); }
			refreshLevelGeometryCommands();
			statusBar()->showMessage(m_settings.levelComponentUvLock() ? tr("Component UV lock on: component edits keep corner texture coordinates.")
																	  : tr("Component UV lock off: textures stay in their stored frame during component edits."),
				4000);
		}, true);
	// NRC TextureVertexLock fits three corners and lets the rest drift (brush.cpp
	// vertexModeBuildHull at 449778b3, GPL-2.0-or-later); TB's UV Lock never
	// refuses either. Workflow reference only; see docs/CREDITS.md.
	add(QStringLiteral("map.componentUvFit"), tr("Fit UVs When Lock Fails"),
		tr("With Lock Component UVs on, a face no single projection can follow, such as a side made into a trapezoid, keeps three corners' "
		   "texture coordinates, preferring corners the edit did not move, and the others drift. Off refuses such edits instead."),
		QStringLiteral("image"), components, [this] {
			m_settings.setLevelComponentUvFit(!m_settings.levelComponentUvFit());
			if (m_levelComponentsRefresh) { m_levelComponentsRefresh(); }
			refreshLevelGeometryCommands();
			statusBar()->showMessage(m_settings.levelComponentUvFit() ? tr("Locked UVs that cannot stay exact are fitted to three corners.")
																	 : tr("Edits whose locked UVs cannot stay exact are refused."),
				4000);
		}, true);
	add(QStringLiteral("map.componentXray"), tr("X-ray Component Handles"),
		tr("Show and pick camera component handles through surfaces. Turn off to pick only visible handles."),
		QStringLiteral("vertex-tool"), components, [this] {
			if (m_levelMap3D) { m_levelMap3D->setComponentXray(!m_levelMap3D->componentXray()); }
			refreshLevelGeometryCommands();
		}, true);
	add(QStringLiteral("map.splitComponents"), tr("Split Edge/Face"),
		tr("Drag selected edge midpoints or face centres to create new corners. Preview the convex result before release; existing corners are preserved."),
		QStringLiteral("vertex-tool"), components, [this] {
			setLevelComponentSplit(m_levelMapViewport && !m_levelMapViewport->componentSplit());
		}, true);
	add(QStringLiteral("map.insertVertexTool"), tr("Insert Vertex"),
		tr("Drag from a grid point on an edge or face to add a convex corner. Shared geometry is edited together; release waits for validation."),
		QStringLiteral("vertex-tool"), components, [this] { setLevelVertexInsertion(!m_levelVertexInsertion); }, true);
	add(QStringLiteral("map.extrudeFaces"), tr("Extrude Faces"),
		tr("Move picked brush planes along their normals. Drag a face handle or the camera's N axis; every original face must survive."),
		QStringLiteral("face-tool"), components, [this] { setLevelComponentExtrusion(m_levelComponentExtrusion ? 0 : 1); }, true);
	add(QStringLiteral("map.extrudeSplit"), tr("Split Extrusion"),
		tr("Create a separate brush when pulling outward, or divide the original brush when pushing inward."),
		QStringLiteral("clip"), components, [this] { setLevelComponentExtrusion(m_levelComponentExtrusion == 2 ? 1 : 2); }, true);
	add(QStringLiteral("map.extrudePrecisely"), tr("Extrude Faces Precisely…"),
		tr("Set an exact signed distance along the first picked face's outward normal. Uses the shared checked extrusion and Texture Lock setting."),
		QStringLiteral("face-tool"), components, [this] { extrudeLevelFacesPreciselyFromUi(); });
	add(QStringLiteral("map.sweepFaces"),tr("Sweep Faces…"),
		tr("Join picked face profiles through an arc, straight or S-bend path, with rotation, taper and repeated sections. Preview before creating new brushes."),
		QStringLiteral("face-tool"),components,[this]{sweepLevelFacesFromUi();});
	add(QStringLiteral("map.sweepTool"),tr("Sweep Tool"),
		tr("Move, rotate and scale a destination cap in the level views. Create Sweep applies the checked draft as one undo step."),
		QStringLiteral("face-tool"),components,[this]{toggleLevelSweepTool();},true);
	add(QStringLiteral("map.sweepApply"),tr("Create Sweep"),tr("Create the checked sweep and retain the source faces."),
		QStringLiteral("face-tool"),components,[this]{commitLevelSweepTool();});
	add(QStringLiteral("map.sweepDecreaseScale"),tr("Decrease Sweep Scale"),tr("Move the destination's scale handle inward by one grid step."),
		QStringLiteral("scale"),components,[this]{stepLevelSweepToolScale(-1);});
	add(QStringLiteral("map.sweepIncreaseScale"),tr("Increase Sweep Scale"),tr("Move the destination's scale handle outward by one grid step."),
		QStringLiteral("scale"),components,[this]{stepLevelSweepToolScale(1);});
	add(QStringLiteral("map.removeComponents"), tr("Remove Selected Components"),
		tr("Remove the vertices of the picked brush components. Every affected brush must remain three-dimensional; Undo restores the edit."),
		QStringLiteral("vertex-tool"), components, [this]() { removeLevelComponentsFromUi(); });
	add(QStringLiteral("map.snapVertices"), tr("Snap Vertices to Grid"),
		tr("Round every vertex of the selected brushes onto the grid and rebuild them, as TrenchBroom's Snap Vertices does, to clean up brushes "
		   "left off the grid."),
		QStringLiteral("grid"), components, [this]() { snapLevelMapVerticesFromUi(); });
	add(QStringLiteral("map.roundToGrid"), tr("Round Selection to Grid"),
		tr("Round every brush vertex, patch control point and point-entity origin of the selection onto the grid, or only the picked components "
		   "in a component mode, as Radiant's Snap To Grid does. One undo step; brushes the grid would collapse are left as they were."),
		QStringLiteral("grid"), tr("Brush Tools"), [this]() { roundLevelMapSelectionToGridFromUi(); });
	const QString geometry = tr("Brush Tools");
	add(QStringLiteral("map.hollowTouching"), tr("Hollow Touching…"),
		tr("Hollow the selected brushes into walls that meet without overlapping, as NetRadiant Custom's wrapped hollow does."),
		QStringLiteral("cube"), geometry, [this]() { hollowLevelMapTouchingFromUi(); });
	add(QStringLiteral("map.makeRoom"), tr("Make Room"),
		tr("Build walls one grid step thick around the outside of each selected brush and remove the brush, so it becomes the room, as "
		   "NetRadiant Custom's Make Room does. Make Room Options decides whether hidden wall faces are caulked and picked faces left open."),
		QStringLiteral("shape-room"), geometry, [this]() { makeLevelRoomFromUi(); });
	add(QStringLiteral("map.makeRoomOptions"), tr("Make Room Options…"),
		tr("Choose whether Make Room caulks the wall faces the room does not see, with which material, and whether it leaves picked faces open."),
		QStringLiteral("settings"), geometry, [this]() { editLevelRoomOptionsFromUi(); });
	add(QStringLiteral("map.selectWholeEntities"), tr("Expand Selection to Whole Entities"),
		tr("Select the whole brush entity of every selected brush or patch, as Radiant's Expand Selection to Entities does."), QStringLiteral("entity"),
		tr("Select by Region"), [this]() { selectLevelWholeEntitiesFromUi(); });
	// Radiant's Patch menu.
	const QString patches = tr("Patch Matrix");
	for (const PatchMatrixCommand& command : patchMatrixCommands()) {
		const LevelPatchMatrixOperation operation = command.operation;
		add(QString::fromLatin1(command.id), QCoreApplication::translate("VibeStudioLevelGeometry", command.label),
			QCoreApplication::translate("VibeStudioLevelGeometry", command.tip), QStringLiteral("patch-cylinder"), patches,
			[this, operation]() { applyLevelPatchMatrixFromUi(operation); });
	}
	add(QStringLiteral("map.thickenPatch"), tr("Thicken Patch…"),
		tr("Give the selected patches a thickness: a copy behind each, facing away, and walls along its open edges, as NetRadiant Custom's "
		   "Thicken does."),
		QStringLiteral("patch-thicken"), tr("Patches"), [this]() { thickenLevelPatchesFromUi(); });
	// Regions.
	const QString regions = tr("Region");
	add(QStringLiteral("map.regionDraw"), tr("Draw Region"),
		tr("Drag a box in a 2D view to make it the region, through the whole map's depth, as Radiant's Region Set XY does. Escape leaves."),
		QStringLiteral("select-box"), regions, [this]() { setLevelRegionDrawMode(!(m_levelMapViewport && m_levelMapViewport->regionDrawMode())); }, true);
	add(QStringLiteral("map.regionSetBrush"), tr("Set Region to Brush"),
		tr("Make the selected brushes' bounds the region and remove the brushes, as Radiant's Region Set Brush does; Undo brings them back."),
		QStringLiteral("cube"), regions, [this]() { setLevelRegionFromBrushes(); });
	add(QStringLiteral("map.regionBounds"), tr("Region Bounds…"), tr("Type the region's lowest and highest corners."), QStringLiteral("resize"),
		regions, [this]() { editLevelRegionBounds(); });
	add(QStringLiteral("map.regionSelect"), tr("Select Everything in Region"),
		tr("Select every object the region keeps that is not hidden."), QStringLiteral("select-box"), regions, [this]() { selectLevelRegionObjects(); });
	// Navigation.
	const QString navigation = tr("Navigate");
	add(QStringLiteral("map.zoomToFit"), tr("Zoom to Fit"), tr("Frame the whole map in every 2D view."), QStringLiteral("frame"), navigation,
		[this]() { zoomLevelViewsToFit(); });
	add(QStringLiteral("map.centerOnSelection"), tr("Centre Views on Selection"),
		tr("Move every 2D view to the selection's centre without changing its zoom, as Radiant's Center does."), QStringLiteral("crosshair"), navigation,
		[this]() { centreLevelViewsOnSelection(); });
	add(QStringLiteral("map.goToCoordinates"), tr("Go to Coordinates…"),
		tr("Centre the 2D views on a point you type, and take the camera there, as Hammer's Go to Coordinates does."), QStringLiteral("crosshair"),
		navigation, [this]() { goToLevelCoordinatesFromUi(); });
	add(QStringLiteral("map.findObject"), tr("Find Brush by Number…"),
		tr("Select and frame the object compilers name as \"Entity 0, Brush 12\", as Radiant's Find Brush does."), QStringLiteral("search"), navigation,
		[this]() { findLevelObjectByNumberFromUi(); });
	add(QStringLiteral("map.replaceWithPrefab"), tr("Replace Selection with Prefab…"),
		tr("Put a prefab where the selection stands, its anchor at the selection's bottom centre, and remove the selection, as one undo step."),
		QStringLiteral("prefab"), tr("Prefabs"), [this]() { replaceLevelSelectionWithPrefabFromUi(); });
	// NetRadiant Custom's CSG Tool (level_csg_tool_actions.cpp).
	registerLevelCsgToolCommands();
}

void ApplicationShell::refreshLevelGeometryCommands()
{
	if (!m_commands) {
		return;
	}
	const LevelMapDocument& document = m_levelMapDocument;
	const bool textMap = document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
	m_commands->setEnabled(QStringLiteral("map.duplicateUnique"),canDuplicateLevelMapSelection(LevelDuplicateMode::MakeUnique));
	for (const auto& id : {QStringLiteral("map.duplicateSelection"),QStringLiteral("map.duplicateWithOffset")}) {
		m_commands->setEnabled(id,canDuplicateLevelMapSelection());
	}
	bool brushes = false;
	bool primitives = false;
	bool owned = false;
	for (const LevelMapSelectionRef& ref : document.selection) {
		brushes = brushes || ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::Entity;
		primitives = primitives || ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch;
		owned = owned || ref.kind == LevelMapSelectionKind::QuakeBrush || ref.kind == LevelMapSelectionKind::QuakePatch;
	}
	const bool patches = document.format == LevelMapFormat::Quake3Map && !levelMapSelectedPatchIds(document).isEmpty();
	for (const auto& id : {QStringLiteral("map.vertexTool"), QStringLiteral("map.edgeTool"), QStringLiteral("map.faceTool")}) {
		m_commands->setEnabled(id, textMap);
	}
	m_commands->setEnabled(QStringLiteral("map.snapVertices"), textMap && brushes);
	const auto picked = m_levelMapViewport ? m_levelMapViewport->pickedComponents() : QVector<LevelMapComponentSet>{};
	for (const auto& id : {QStringLiteral("map.wrapMerge"),QStringLiteral("map.wrapMergePoints")}) {
		m_commands->setEnabled(id,textMap && (levelComponentsActive() ? !picked.isEmpty() && !m_levelMapViewport->componentHandlesLimited() : levelMapSelectionClippable()));
	}
	for (const auto& id : {QStringLiteral("map.nextComponent"),QStringLiteral("map.previousComponent")}) {
		m_commands->setEnabled(id,textMap && levelComponentsActive() && m_levelMapViewport->componentHandleCount() && !m_levelMapViewport->componentHandlesLimited());
	}
	for (const auto& id : {QStringLiteral("map.componentTouchingEdges"),QStringLiteral("map.componentTouchingFaces"),QStringLiteral("map.growComponents"),QStringLiteral("map.shrinkComponents"),QStringLiteral("map.connectedComponents")}) {
		m_commands->setEnabled(id,textMap && levelComponentsActive() && !picked.isEmpty() && !m_levelMapViewport->componentHandlesLimited());
	}
	const QStringList tools{QStringLiteral("map.componentMoveTool"),QStringLiteral("map.componentRotateTool"),QStringLiteral("map.componentScaleTool"),QStringLiteral("map.componentShearTool"),QStringLiteral("map.componentPivotTool")};
	for (int tool = 0; tool < tools.size(); ++tool) {
		m_commands->setEnabled(tools[tool],textMap && levelComponentsActive());
		if (auto* action = m_commands->action(tools[tool])) { const QSignalBlocker blocker(action); action->setChecked(tool == m_levelComponentTool && levelComponentsActive()); }
	}
	m_commands->setEnabled(QStringLiteral("map.componentResetPivot"),textMap && m_levelComponentPivot.valid);
	m_commands->setEnabled(QStringLiteral("map.componentGizmoOptions"),textMap);
	m_commands->setEnabled(QStringLiteral("map.snapPickedVertex"),textMap && m_levelMapViewport && m_levelMapViewport->canSnapPickedVertex());
	m_commands->setEnabled(QStringLiteral("map.transformComponents"), textMap && levelComponentsActive() && (!picked.isEmpty() || !levelComponentFacePicks().faces.isEmpty()));
	// Open Rotate and Scale windows follow component picks and mode changes.
	refreshLevelArbitraryTransformDialogs();
	m_commands->setEnabled(QStringLiteral("map.componentUvLock"), textMap);
	if (auto* action = m_commands->action(QStringLiteral("map.componentUvLock"))) {
		const QSignalBlocker blocker(action); action->setChecked(m_settings.levelComponentUvLock());
	}
	m_commands->setEnabled(QStringLiteral("map.componentUvFit"), textMap);
	if (auto* action = m_commands->action(QStringLiteral("map.componentUvFit"))) {
		const QSignalBlocker blocker(action); action->setChecked(m_settings.levelComponentUvFit());
	}
	// Doom-format maps fall back to Snap Selection to Grid.
	m_commands->setEnabled(QStringLiteral("map.roundToGrid"), document.format != LevelMapFormat::Unknown
		&& (levelComponentsActive() ? !picked.isEmpty() || !levelComponentFacePicks().faces.isEmpty() : !document.selection.isEmpty()));
	refreshLevelDeletionCommands();
	m_commands->setEnabled(QStringLiteral("map.hollowTouching"), textMap && levelMapSelectionClippable());
	m_commands->setEnabled(QStringLiteral("map.makeRoom"), textMap && levelMapSelectionClippable());
	refreshLevelCsgToolCommands();
	m_commands->setEnabled(QStringLiteral("map.selectWholeEntities"), textMap && owned);
	for (const QString& id : levelPatchMatrixCommandIds()) {
		m_commands->setEnabled(id, patches);
	}
	m_commands->setEnabled(QStringLiteral("map.thickenPatch"), patches);
	m_commands->setEnabled(QStringLiteral("map.regionDraw"), textMap);
	m_commands->setEnabled(QStringLiteral("map.regionSetBrush"), textMap && brushes);
	m_commands->setEnabled(QStringLiteral("map.regionBounds"), textMap);
	m_commands->setEnabled(QStringLiteral("map.regionSelect"), levelRegionActive());
	m_commands->setEnabled(QStringLiteral("map.zoomToFit"), document.format != LevelMapFormat::Unknown);
	m_commands->setEnabled(QStringLiteral("map.centerOnSelection"), !document.selection.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.goToCoordinates"), document.format != LevelMapFormat::Unknown);
	m_commands->setEnabled(QStringLiteral("map.findObject"), textMap);
	m_commands->setEnabled(QStringLiteral("map.replaceWithPrefab"), textMap && (primitives || brushes));
	// The mode commands show the views' mode, whichever way it was left.
	const int componentMode = m_levelMapViewport ? static_cast<int>(m_levelMapViewport->componentMode()) : 0;
	const bool sweepSource=textMap && componentMode==3 && !picked.isEmpty()
		&& std::all_of(picked.cbegin(),picked.cend(),[](const auto& set){return set.object.kind==LevelMapSelectionKind::QuakeBrush && set.kind==LevelBrushComponent::Face;});
	m_commands->setEnabled(QStringLiteral("map.sweepFaces"),sweepSource || levelSweepToolActive());
	m_commands->setEnabled(QStringLiteral("map.sweepTool"),sweepSource || levelSweepToolActive());
	m_commands->setChecked(QStringLiteral("map.sweepTool"),levelSweepToolActive());
	m_commands->setEnabled(QStringLiteral("map.sweepApply"),levelSweepToolReady());
	m_commands->setEnabled(QStringLiteral("map.sweepDecreaseScale"),levelSweepToolActive());
	m_commands->setEnabled(QStringLiteral("map.sweepIncreaseScale"),levelSweepToolActive());
	m_commands->setEnabled(QStringLiteral("map.openGroup"), !levelSelectedClosedGroup().isEmpty());
	m_commands->setEnabled(QStringLiteral("map.closeGroup"), !document.openSceneGroups.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.closeAllGroups"), !document.openSceneGroups.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.extrudeFaces"), textMap && brushes);
	m_commands->setEnabled(QStringLiteral("map.extrudeSplit"), textMap && m_levelComponentExtrusion != 0);
	m_commands->setEnabled(QStringLiteral("map.extrudePrecisely"), textMap && componentMode == 3 && !picked.isEmpty()
		&& std::all_of(picked.cbegin(),picked.cend(),[](const auto& set) { return set.object.kind == LevelMapSelectionKind::QuakeBrush && set.kind == LevelBrushComponent::Face && set.components.size() == 1; }));
	for (const auto& id : {QStringLiteral("map.extrudeFaces"),QStringLiteral("map.extrudeSplit")}) {
		if (auto* action = m_commands->action(id)) { const QSignalBlocker blocker(action); action->setChecked(id.endsWith(QStringLiteral("Split")) ? m_levelComponentExtrusion == 2 : m_levelComponentExtrusion != 0); }
	}
	m_commands->setEnabled(QStringLiteral("map.splitComponents"), textMap && (componentMode == 2 || componentMode == 3));
	m_commands->setEnabled(QStringLiteral("map.insertVertexTool"), textMap && brushes);
	if (auto* action = m_commands->action(QStringLiteral("map.insertVertexTool"))) {
		const QSignalBlocker blocker(action); action->setChecked(m_levelVertexInsertion);
	}
	if (auto* action = m_commands->action(QStringLiteral("map.splitComponents"))) {
		const QSignalBlocker blocker(action); action->setChecked(m_levelMapViewport && m_levelMapViewport->componentSplit());
	}
	m_commands->setEnabled(QStringLiteral("map.componentXray"), textMap && componentMode && levelMap3DShowing());
	if (auto* action = m_commands->action(QStringLiteral("map.componentXray"))) {
		const QSignalBlocker blocker(action); action->setChecked(m_levelMap3D && m_levelMap3D->componentXray());
	}
	const QStringList modes {QStringLiteral("map.vertexTool"), QStringLiteral("map.edgeTool"), QStringLiteral("map.faceTool")};
	for (int index = 0; index < modes.size(); ++index) {
		if (QAction* action = m_commands->action(modes.at(index))) {
			const QSignalBlocker blocker(action);
			action->setChecked(componentMode == index + 1);
		}
	}
	if (QAction* action = m_commands->action(QStringLiteral("map.regionDraw"))) {
		const QSignalBlocker blocker(action);
		action->setChecked(m_levelMapViewport && m_levelMapViewport->regionDrawMode());
	}
	// Picks, the sweep tool and the component tool decide what a nudge moves,
	// and the sweep tool what a view-relative turn or flip may act on.
	refreshLevelNudgeCommands();
	refreshLevelAxisTransformCommands();
}

// ---------------------------------------------------------------------------
// Vertex editing

void ApplicationShell::setLevelComponentMode(int mode, bool convertSelection)
{
	if (mode < 0 || mode > 3) { return; }
	QVector<LevelMapComponentSet> converted;
	const bool converting = convertSelection && mode && levelComponentsActive() && static_cast<int>(m_levelMapViewport->componentMode()) != mode;
	if (converting) {
		LevelComponentSelectionRequest request; request.target = static_cast<LevelBrushComponent>(mode-1); QString error;
		if (!m_levelMapViewport->queryComponentSelection(request,&converted,&error)) { statusBar()->showMessage(error,6000); return; }
	}
	for (auto* view : std::as_const(m_levelPlanViews)) { view->finishComponentMove(false); }
	if (m_levelMap3D) { m_levelMap3D->finishComponentMove(false); }
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (mode != 0 && !textMap) {
		statusBar()->showMessage(tr("Open a Quake-family map to edit brush vertices and patch control points."));
		mode = 0;
	}
	if (mode != 0) {
		// Construction and material tools give way to shared component editing.
		m_levelPickedFaces.clear();
		if (m_levelMapViewport && m_levelMapViewport->clipMode()) {
			setLevelMapClipMode(false);
		}
		setLevelShearMode(false);
		setLevelRegionDrawMode(false);
		if (m_levelMap3D) { m_levelMap3D->setSurfaceTool(ModelViewportSurfaceTool::None); }
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		const QSignalBlocker blocker(view);
		view->setComponentMode(static_cast<MapViewport::ComponentMode>(mode));
		if (converting) { view->setPickedComponents(converted); }
	}
	if (levelMap3DShowing()) { refreshLevelMap3DHighlight(); }
	else { refreshLevelCameraComponents(); refreshLevelCameraResize(); }
	refreshLevelGeometryCommands();
	refreshLevelToolShelf();
	refreshLevelSurfaceTools();
	if (mode != 0) {
		if (levelMap3DShowing() && !levelMap2DShowing()) { m_levelMap3D->setFocus(Qt::ShortcutFocusReason); }
		else if (m_levelMapViewport) {
			m_levelMapViewport->setFocus(Qt::ShortcutFocusReason);
		}
		const int handles = m_levelMapViewport ? m_levelMapViewport->componentHandleCount() : 0;
		statusBar()->showMessage(handles == 0
				? tr("Vertex editing on: select brushes or patches to show their handles. Escape leaves.")
				: tr("Vertex editing on: %n handle(s). Click or band them, then drag or use the arrow keys; Escape leaves.", nullptr, handles),
			6000);
		if (converting) { statusBar()->showMessage(tr("Selection converted: %n component(s).",nullptr,m_levelMapViewport->pickedComponentCount()),6000); }
		else if (const auto faces = static_cast<int>(levelComponentFacePicks().faces.size()); faces > 0) {
			statusBar()->showMessage(tr("Face editing includes %n face(s) picked by Select Textured. Use the arrow keys, the manipulator or Transform Components; Escape drops the picks.",nullptr,faces),6000);
		}
	}
}

void ApplicationShell::selectLevelComponentTopology(int operation, int target, bool touching)
{
	if (!levelComponentsActive()) { return; }
	LevelComponentSelectionRequest request;
	request.operation = static_cast<LevelComponentSelectionOperation>(operation);
	request.target = static_cast<LevelBrushComponent>(std::max(1,target)-1); request.touching = touching;
	QVector<LevelMapComponentSet> picked; QString error;
	if (!m_levelMapViewport->queryComponentSelection(request,&picked,&error)) { statusBar()->showMessage(error,6000); return; }
	for (auto* view : std::as_const(m_levelPlanViews)) { view->finishComponentMove(false); }
	if (m_levelMap3D) { m_levelMap3D->finishComponentMove(false); }
	if (operation == 0) { setLevelComponentMode(target,false); }
	m_levelMapViewport->setPickedComponents(picked); shareLevelComponentPicks(m_levelMapViewport);
	statusBar()->showMessage(tr("Selected %n component(s).",nullptr,m_levelMapViewport->pickedComponentCount()),6000);
}

void ApplicationShell::setLevelComponentSplit(bool enabled)
{
	if (enabled) { m_levelComponentExtrusion = 0; m_levelComponentTool = 0; }
	for (auto* view : std::as_const(m_levelPlanViews)) { view->setComponentSplit(enabled); }
	refreshLevelCameraComponents(); refreshLevelGeometryCommands();
	statusBar()->showMessage(enabled ? tr("Split: drag an edge midpoint or face centre outward to create a corner. Escape cancels the drag.")
		: tr("Component movement restored."),6000);
}

void ApplicationShell::setLevelVertexInsertion(bool enabled)
{
	for (auto* view : std::as_const(m_levelPlanViews)) { view->finishComponentMove(false); }
	if (m_levelMap3D) { m_levelMap3D->finishComponentMove(false); }
	if (enabled) {
		setLevelComponentMode(1); m_levelComponentTool = 0; m_levelComponentExtrusion = 0;
		for (auto* view : std::as_const(m_levelPlanViews)) { view->setComponentSplit(false); }
	}
	m_levelVertexInsertion = enabled;
	refreshLevelCameraComponents(); refreshLevelGeometryCommands(); refreshLevelToolShelf();
	statusBar()->showMessage(enabled ? tr("Insert Vertex: drag outward from an edge or face grid point. Alt constrains to Z; numeric insertion is in Edit Brush Components.") : tr("Component movement restored."),6000);
}

void ApplicationShell::setLevelComponentExtrusion(int mode)
{
	if (mode && m_levelMapViewport && m_levelMapViewport->componentMode() != MapViewport::ComponentMode::Faces) { setLevelComponentMode(3); }
	m_levelComponentExtrusion = std::clamp(mode,0,2);
	if (mode) { m_levelComponentTool = 0; }
	for (auto* view : std::as_const(m_levelPlanViews)) { view->setComponentSplit(false); }
	refreshLevelCameraComponents(); refreshLevelGeometryCommands();
	statusBar()->showMessage(mode ? tr("Extrude: pick a face, then drag its handle along the normal. Split Extrusion creates separate solids; numeric editing is available in Geometry.")
		: tr("Component movement restored."),6000);
}

void ApplicationShell::extrudeLevelFacesPreciselyFromUi()
{
	if (!m_levelMapViewport || m_levelMapViewport->pickedComponents().isEmpty()) { return; }
	QDialog dialog(this); dialog.setObjectName(QStringLiteral("extrudeFacesDialog")); dialog.setWindowTitle(tr("Extrude Faces Precisely"));
	auto* form = new QFormLayout(&dialog); form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	const auto reference = m_levelMapViewport->pickedComponents().first();
	if (reference.components.isEmpty()) { return; }
	auto* face = new QLabel(tr("Brush %1 · Face %2").arg(reference.object.objectId).arg(reference.components.first()),&dialog);
	face->setObjectName(QStringLiteral("extrudeReference")); face->setAccessibleName(tr("Reference face")); face->setWordWrap(true);
	face->setToolTip(tr("The first selected brush in document order supplies the extrusion normal. The camera's N axis shows this direction."));
	face->setAccessibleDescription(face->toolTip()); form->addRow(tr("Reference face"),face);
	auto* distance = new QDoubleSpinBox(&dialog); distance->setObjectName(QStringLiteral("extrudeDistance"));
	distance->setRange(-32768,32768); distance->setDecimals(6); distance->setSingleStep(m_levelMapViewport->gridSize());
	distance->setValue(m_levelMapViewport->gridSize()); distance->setAccessibleName(tr("Extrusion distance"));
	distance->setToolTip(tr("Positive moves outward along the reference face's normal; negative moves inward. Exact values are not snapped."));
	distance->setAccessibleDescription(distance->toolTip()); form->addRow(tr("Distance"),distance);
	auto* split = new QCheckBox(tr("Split into separate brushes"),&dialog); split->setObjectName(QStringLiteral("extrudeSeparate"));
	split->setChecked(m_levelComponentExtrusion == 2); form->addRow(split);
	auto* state = new QLabel(tr("Uses the current Texture Lock setting. Invalid or locked geometry leaves every brush unchanged."),&dialog);
	state->setWordWrap(true); form->addRow(state);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);
	auto* apply = buttons->addButton(tr("Extrude"),QDialogButtonBox::AcceptRole); apply->setObjectName(QStringLiteral("extrudeApply"));
	form->addRow(buttons); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
	connect(apply,&QPushButton::clicked,&dialog,&QDialog::accept);
	distance->setFocus(Qt::OtherFocusReason);
	if (dialog.exec() != QDialog::Accepted) { return; }
	// Retire the modal window's focus/grab lifecycle before starting a checked
	// viewport transaction; closing the dialog must not cancel its own request.
	setLevelComponentExtrusion(split->isChecked() ? 2 : 1);
	if (!m_levelMap3D->requestComponentExtrusion(distance->value())) {
		statusBar()->showMessage(tr("The camera or face selection is not ready. Wait for the map preview and try again."),6000);
	}
}

void ApplicationShell::shareLevelComponentPicks(MapViewport* from)
{
	if (!from) {
		return;
	}
	const auto picks = from->pickedComponents();
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		if (view != from) {
			view->setPickedComponents(picks);
		}
	}
	if (levelMap3DShowing()) { refreshLevelMap3DHighlight(); }
	else { refreshLevelCameraComponents(); }
	refreshLevelGeometryCommands();
	refreshLevelSurfaceTools();
	refreshLevelObjectSelectionCommands();
}

void ApplicationShell::snapLevelMapVerticesFromUi()
{
	QSet<int> entities;
	QVector<int> brushes;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes << ref.objectId;
		} else if (ref.kind == LevelMapSelectionKind::Entity) {
			entities.insert(ref.objectId);
		}
	}
	for (const LevelMapBrush& brush : std::as_const(m_levelMapDocument.brushes)) {
		if (entities.contains(brush.entityId) && !brushes.contains(brush.id)) {
			brushes << brush.id;
		}
	}
	const double grid = m_levelMapViewport ? m_levelMapViewport->gridSize() : 8.0;
	int snapped = 0;
	QStringList skipped;
	QString error;
	if (!snapLevelMapBrushVertices(&m_levelMapDocument, brushes, grid, &snapped, &skipped, &error)) {
		statusBar()->showMessage(tr("Could not snap the vertices: %1").arg(error), 6000);
		return;
	}
	if (snapped == 0) {
		statusBar()->showMessage(skipped.isEmpty() ? tr("The selected brushes' vertices are already on grid %1.").arg(grid)
												   : tr("No brush changed.") + levelMapSkippedNote(skipped),
			5000);
		return;
	}
	recordActivity(tr("Brush vertices snapped to the grid"), tr("%n brush(es)", nullptr, snapped), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Snapped the vertices of %n brush(es) to grid %1.", nullptr, snapped).arg(grid) + levelMapSkippedNote(skipped), 5000);
}

void ApplicationShell::hollowLevelMapTouchingFromUi()
{
	if (!levelMapSelectionClippable()) {
		statusBar()->showMessage(tr("Select brushes, or a brush entity, to hollow."));
		return;
	}
	// The prompt takes whole units, so a grid finer than 1 offers 1.
	const int grid = m_levelMapViewport ? std::max(1, qRound(m_levelMapViewport->gridSize())) : 8;
	bool ok = false;
	const int thickness = QInputDialog::getInt(this, tr("Hollow Touching"), tr("Wall thickness (units)"), grid, 1, 4096, 1, &ok);
	if (!ok) {
		return;
	}
	int hollowed = 0;
	QString error;
	QStringList skipped;
	if (!hollowLevelMapSelection(&m_levelMapDocument, thickness, LevelMapHollowMode::Touch, &hollowed, &error, &skipped)) {
		statusBar()->showMessage(tr("Could not hollow: %1").arg(error));
		return;
	}
	recordActivity(tr("Level map brushes hollowed"), tr("%n wall(s)", nullptr, static_cast<int>(m_levelMapDocument.selection.size())),
		QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(
		tr("Hollowed %n brush(es) into walls %1 units thick that meet without overlapping.", nullptr, hollowed).arg(thickness) + levelMapSkippedNote(skipped));
}

void ApplicationShell::makeLevelRoomFromUi()
{
	if (!levelMapSelectionClippable()) {
		statusBar()->showMessage(tr("Select brushes, or a brush entity, to make rooms of."));
		return;
	}
	const double grid = m_levelMapViewport ? m_levelMapViewport->gridSize() : 8.0;
	// NRC's Make Room caulks every wall face but the room's and leaves the
	// picked faces open; Make Room Options can turn either off.
	const LevelRoomPreferences preferences = m_settings.levelRoomPreferences();
	LevelMapHollowOptions options;
	options.caulkTexture = levelRoomCaulkTexture(m_levelMapDocument, preferences);
	if (preferences.openPickedFaces) {
		options.openFaces = levelPickedSurfaceFaces();
	}
	LevelMapHollowReport report;
	QString error;
	if (!hollowLevelMapSelection(&m_levelMapDocument, grid, LevelMapHollowMode::Room, options, &report, &error)) {
		statusBar()->showMessage(tr("Could not make the room: %1").arg(error));
		return;
	}
	recordActivity(tr("Level map rooms made"), tr("%n brush(es)", nullptr, report.hollowed), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message = tr("Made %n room(s) with walls %1 units thick outside them.", nullptr, report.hollowed).arg(grid);
	if (report.openFaces > 0) {
		message += QLatin1Char(' ') + tr("Left %n face(s) open.", nullptr, report.openFaces);
	}
	if (!options.caulkTexture.isEmpty()) {
		message += QLatin1Char(' ') + tr("Caulked %n hidden wall face(s) with %1.", nullptr, report.caulkedFaces).arg(options.caulkTexture);
	} else if (preferences.caulk) {
		message += QLatin1Char(' ') + tr("This game has no caulk material, so the walls keep their textures; name one in Make Room Options.");
	}
	statusBar()->showMessage(message + levelMapSkippedNote(report.skipped), 8000);
}

void ApplicationShell::editLevelRoomOptionsFromUi()
{
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelRoomOptionsDialog"));
	dialog.setWindowTitle(tr("Make Room Options"));
	dialog.setAccessibleName(dialog.windowTitle());
	auto* layout = new QVBoxLayout(&dialog);
	auto* form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapAllRows);
	layout->addLayout(form);
	auto* caulk = new QCheckBox(tr("&Caulk the wall faces the room does not see"), &dialog);
	caulk->setObjectName(QStringLiteral("levelRoomCaulk"));
	caulk->setToolTip(tr("Every wall face except the one facing the room takes the caulk material, so the compiler leaves it out. Faces lining an "
						 "open face keep its material."));
	auto* texture = new QLineEdit(&dialog);
	texture->setObjectName(QStringLiteral("levelRoomCaulkTexture"));
	texture->setAccessibleName(tr("Caulk material"));
	texture->setMaxLength(1024);
	texture->setToolTip(tr("Leave empty to use the game's own caulk: common/caulk in Quake III and skip in Quake. Quake II has none, so name a "
						   "material its assets provide."));
	auto* open = new QCheckBox(tr("Leave picked faces &open"), &dialog);
	open->setObjectName(QStringLiteral("levelRoomOpenFaces"));
	open->setToolTip(tr("Faces picked in face mode, with Select Textured or in the camera get no wall, so they become openings; the walls beside "
						"them stop flush with them."));
	for (auto* control : {caulk, open}) {
		control->setAccessibleName(control->text().remove(QLatin1Char('&')));
		control->setAccessibleDescription(control->toolTip());
	}
	texture->setAccessibleDescription(texture->toolTip());
	form->addRow(caulk);
	form->addRow(tr("Caulk &material"), texture);
	form->addRow(open);
	// What Make Room would use on the open map, so the game's default is
	// never a guess.
	auto* effective = new QLabel(&dialog);
	effective->setObjectName(QStringLiteral("levelRoomCaulkSummary"));
	effective->setWordWrap(true);
	effective->setAccessibleName(tr("Caulk material in use"));
	layout->addWidget(effective);
	auto* error = new QLabel(&dialog);
	error->setObjectName(QStringLiteral("levelRoomOptionsError"));
	error->setAccessibleName(tr("Make Room options validation"));
	error->setWordWrap(true);
	error->hide();
	layout->addWidget(error);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
	layout->addWidget(buttons);
	const QString gameCaulk = levelMapCaulkTexture(m_levelMapDocument);
	texture->setPlaceholderText(gameCaulk.isEmpty() ? tr("No game caulk: name a material") : gameCaulk);
	const auto current = [&] {
		return LevelRoomPreferences {caulk->isChecked(), texture->text().trimmed(), open->isChecked()};
	};
	const auto refresh = [&] {
		texture->setEnabled(caulk->isChecked());
		const QString used = levelRoomCaulkTexture(m_levelMapDocument, current());
		effective->setText(!caulk->isChecked() ? tr("Walls keep the brush's textures.")
			: used.isEmpty() ? tr("This map's game has no caulk material, so walls keep the brush's textures until you name one.")
							 : tr("Hidden wall faces on this map take %1.").arg(used));
		effective->setAccessibleDescription(effective->text());
		error->hide();
	};
	const auto populate = [&](const LevelRoomPreferences& preferences) {
		caulk->setChecked(preferences.caulk);
		texture->setText(preferences.caulkTexture);
		open->setChecked(preferences.openPickedFaces);
		refresh();
	};
	populate(m_settings.levelRoomPreferences());
	connect(caulk, &QCheckBox::toggled, &dialog, [&] { refresh(); });
	connect(texture, &QLineEdit::textChanged, &dialog, [&] { refresh(); });
	connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] { populate({}); });
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
		const LevelRoomPreferences next = current();
		if (const QString problem = levelRoomPreferencesProblem(next); !problem.isEmpty()) {
			error->setText(problem);
			error->show();
			texture->setFocus();
			return;
		}
		if (!m_settings.setLevelRoomPreferences(next)) {
			error->setText(tr("Unable to save the Make Room options."));
			error->show();
			return;
		}
		dialog.accept();
	});
	dialog.exec();
}

void ApplicationShell::selectLevelWholeEntitiesFromUi()
{
	const QVector<LevelMapSelectionRef> whole = levelMapSelectionWholeEntities(m_levelMapDocument);
	if (whole == m_levelMapDocument.selection) {
		statusBar()->showMessage(tr("The selection holds no part of a brush entity."), 4000);
		return;
	}
	QString error;
	if (!setLevelMapSelection(&m_levelMapDocument, whole, &error)) {
		statusBar()->showMessage(error, 5000);
		return;
	}
	refreshLevelMapWorkbench();
	showLevelSelectionMessage(tr("Selected %n object(s), brush entities whole.", nullptr, static_cast<int>(whole.size())));
}

// ---------------------------------------------------------------------------
// Patches

void ApplicationShell::applyLevelPatchMatrixFromUi(LevelPatchMatrixOperation operation)
{
	const QVector<int> ids = levelMapSelectedPatchIds(m_levelMapDocument);
	if (ids.isEmpty()) {
		statusBar()->showMessage(tr("Select Quake III patches first."));
		return;
	}
	int changed = 0;
	QString error;
	if (!applyLevelMapPatchMatrixOperation(&m_levelMapDocument, ids, operation, &changed, &error)) {
		statusBar()->showMessage(tr("Could not change the patches: %1").arg(error), 6000);
		return;
	}
	if (changed == 0) {
		statusBar()->showMessage(tr("The patches are already that way; nothing changed."), 4000);
		return;
	}
	recordActivity(tr("Level patches changed"), levelPatchMatrixOperationId(operation), QStringLiteral("level-map"), OperationState::Warning,
		tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QStringList grids;
	for (const LevelMapPatch& patch : std::as_const(m_levelMapDocument.patches)) {
		if (ids.contains(patch.id) && grids.size() < 4) {
			grids << tr("%1 × %2").arg(patch.width).arg(patch.height);
		}
	}
	statusBar()->showMessage(tr("Changed %n patch(es); now %1.", nullptr, changed).arg(grids.join(QStringLiteral(", "))), 5000);
}

void ApplicationShell::thickenLevelPatchesFromUi()
{
	const QVector<int> ids = levelMapSelectedPatchIds(m_levelMapDocument);
	if (ids.isEmpty()) {
		statusBar()->showMessage(tr("Select Quake III patches to thicken."));
		return;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelThickenDialog"));
	dialog.setWindowTitle(tr("Thicken Patch"));
	auto* form = new QFormLayout(&dialog);
	auto* thickness = new QDoubleSpinBox;
	thickness->setObjectName(QStringLiteral("levelThickenAmount"));
	thickness->setRange(-4096, 4096);
	thickness->setDecimals(3);
	thickness->setValue(m_levelMapViewport ? m_levelMapViewport->gridSize() : 8.0);
	thickness->setAccessibleName(tr("Thickness in map units"));
	thickness->setToolTip(tr("How far behind the surface the copy goes; negative thickens in front of it."));
	auto* direction = new QComboBox;
	direction->setObjectName(QStringLiteral("levelThickenDirection"));
	direction->setAccessibleName(tr("Direction to thicken in"));
	direction->addItem(tr("Along the surface's normals"), -1);
	direction->addItem(tr("Along X"), 0);
	direction->addItem(tr("Along Y"), 1);
	direction->addItem(tr("Along Z"), 2);
	auto* seams = new QCheckBox(tr("Close the open edges with walls"));
	seams->setObjectName(QStringLiteral("levelThickenSeams"));
	seams->setChecked(true);
	form->addRow(tr("Thickness"), thickness);
	form->addRow(tr("Direction"), direction);
	form->addRow(seams);
	auto* note = new QLabel(tr("Each patch keeps its shape; the thickened copy faces away from it and joins its entity."));
	note->setWordWrap(true);
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Thicken"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	LevelPatchThickenRequest request;
	request.thickness = thickness->value();
	request.axis = direction->currentData().toInt();
	request.seams = seams->isChecked();
	QVector<int> created;
	QString error;
	if (!thickenLevelMapPatches(&m_levelMapDocument, ids, request, &created, &error)) {
		statusBar()->showMessage(tr("Could not thicken: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Level patches thickened"), tr("%n patch(es) made", nullptr, static_cast<int>(created.size())), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Thickened %n patch(es); the new pieces are selected.", nullptr, static_cast<int>(ids.size())), 5000);
}

// ---------------------------------------------------------------------------
// Regions

void ApplicationShell::setLevelRegionDrawMode(bool enabled)
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (enabled && !textMap) {
		statusBar()->showMessage(tr("Regions are for Quake-family maps."));
		enabled = false;
	}
	if (enabled) {
		setLevelComponentMode(0);
		if (!levelMap2DShowing()) {
			setLevelMap3D(false);
		}
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		const QSignalBlocker blocker(view);
		view->setRegionDrawMode(enabled);
	}
	refreshLevelGeometryCommands();
	if (enabled) {
		if (m_levelMapViewport) {
			m_levelMapViewport->setFocus(Qt::ShortcutFocusReason);
		}
		statusBar()->showMessage(tr("Draw Region on: drag a box in a 2D view to make it the region. Escape leaves."), 6000);
	}
}

void ApplicationShell::setLevelRegionFromDrawnBox(MapViewport* view, const QRectF& box)
{
	setLevelRegionDrawMode(false);
	if (!view || box.width() < 1 || box.height() < 1) {
		return;
	}
	const LevelMapStatistics statistics = levelMapStatistics(m_levelMapDocument);
	// Across the view as drawn, and through the whole map's depth.
	const double depthLow = std::min({statistics.mins.valid ? statistics.mins.x : -4096.0, statistics.mins.valid ? statistics.mins.y : -4096.0,
		statistics.mins.valid ? statistics.mins.z : -4096.0}) - 64;
	const double depthHigh = std::max({statistics.maxs.valid ? statistics.maxs.x : 4096.0, statistics.maxs.valid ? statistics.maxs.y : 4096.0,
		statistics.maxs.valid ? statistics.maxs.z : 4096.0}) + 64;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	switch (view->projection()) {
	case MapViewportProjection::TopXY:
		mins = {box.left(), box.top(), statistics.mins.valid ? statistics.mins.z : depthLow, true};
		maxs = {box.right(), box.bottom(), statistics.maxs.valid ? statistics.maxs.z : depthHigh, true};
		break;
	case MapViewportProjection::FrontXZ:
		mins = {box.left(), statistics.mins.valid ? statistics.mins.y : depthLow, box.top(), true};
		maxs = {box.right(), statistics.maxs.valid ? statistics.maxs.y : depthHigh, box.bottom(), true};
		break;
	case MapViewportProjection::SideZY:
		mins = {statistics.mins.valid ? statistics.mins.x : depthLow, box.left(), box.top(), true};
		maxs = {statistics.maxs.valid ? statistics.maxs.x : depthHigh, box.right(), box.bottom(), true};
		break;
	}
	if (maxs.x - mins.x < 1 || maxs.y - mins.y < 1 || maxs.z - mins.z < 1) {
		maxs.x = std::max(maxs.x, mins.x + 1);
		maxs.y = std::max(maxs.y, mins.y + 1);
		maxs.z = std::max(maxs.z, mins.z + 1);
	}
	setLevelRegion(mins, maxs);
	showLevelSidebarPage(QStringLiteral("view"), QStringLiteral("view.region"));
	statusBar()->showMessage(tr("The region is set to the box you drew."), 4000);
}

void ApplicationShell::setLevelRegionFromBrushes()
{
	QVector<LevelMapSelectionRef> brushes;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes << ref;
		}
	}
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (brushes.isEmpty() || !levelMapObjectsBounds(m_levelMapDocument, brushes, &mins, &maxs) || maxs.x - mins.x < 1 || maxs.y - mins.y < 1
		|| maxs.z - mins.z < 1) {
		statusBar()->showMessage(tr("Select a brush with some size on every axis to make the region of."));
		return;
	}
	QString error;
	if (!deleteLevelMapObjects(&m_levelMapDocument, brushes, &error)) {
		statusBar()->showMessage(tr("Could not take the brush away: %1").arg(error), 6000);
		return;
	}
	refreshLevelMapWorkbench();
	setLevelRegion(mins, maxs);
	showLevelSidebarPage(QStringLiteral("view"), QStringLiteral("view.region"));
	statusBar()->showMessage(tr("The region is set to the brush, which is gone; Undo brings it back and keeps the region."), 6000);
}

void ApplicationShell::editLevelRegionBounds()
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap) {
		statusBar()->showMessage(tr("Regions are for Quake-family maps."));
		return;
	}
	LevelMapVec3 mins = m_levelRegionMins;
	LevelMapVec3 maxs = m_levelRegionMaxs;
	if (!levelRegionActive() && !levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs)) {
		const LevelMapStatistics statistics = levelMapStatistics(m_levelMapDocument);
		mins = statistics.mins.valid ? statistics.mins : LevelMapVec3 {-512, -512, -512, true};
		maxs = statistics.maxs.valid ? statistics.maxs : LevelMapVec3 {512, 512, 512, true};
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelRegionBoundsDialog"));
	dialog.setWindowTitle(tr("Region Bounds"));
	auto* form = new QFormLayout(&dialog);
	QDoubleSpinBox* fields[6] {};
	const QStringList axes {tr("X"), tr("Y"), tr("Z")};
	for (int corner = 0; corner < 2; ++corner) {
		for (int axis = 0; axis < 3; ++axis) {
			auto* field = new QDoubleSpinBox;
			field->setObjectName(QStringLiteral("levelRegionBound%1%2").arg(corner == 0 ? QStringLiteral("Min") : QStringLiteral("Max")).arg(axis));
			field->setRange(-131072, 131072);
			field->setDecimals(2);
			const LevelMapVec3& from = corner == 0 ? mins : maxs;
			field->setValue(axis == 0 ? from.x : (axis == 1 ? from.y : from.z));
			const QString label = corner == 0 ? tr("Lowest %1").arg(axes.at(axis)) : tr("Highest %1").arg(axes.at(axis));
			field->setAccessibleName(label);
			form->addRow(label, field);
			fields[corner * 3 + axis] = field;
		}
	}
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Set Region"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	const LevelMapVec3 low {std::min(fields[0]->value(), fields[3]->value()), std::min(fields[1]->value(), fields[4]->value()),
		std::min(fields[2]->value(), fields[5]->value()), true};
	const LevelMapVec3 high {std::max(fields[0]->value(), fields[3]->value()), std::max(fields[1]->value(), fields[4]->value()),
		std::max(fields[2]->value(), fields[5]->value()), true};
	if (high.x - low.x < 1 || high.y - low.y < 1 || high.z - low.z < 1) {
		statusBar()->showMessage(tr("A region needs at least one unit of size on every axis."));
		return;
	}
	setLevelRegion(low, high);
	statusBar()->showMessage(tr("The region is set to the bounds you typed."), 4000);
}

void ApplicationShell::selectLevelRegionObjects()
{
	if (!levelRegionActive()) {
		statusBar()->showMessage(tr("Set a region first."));
		return;
	}
	int leftHidden = 0;
	const QVector<LevelMapSelectionRef> objects =
		visibleLevelMapObjects(levelMapRegionObjects(m_levelMapDocument, m_levelRegionMins, m_levelRegionMaxs), &leftHidden);
	// Worldspawn itself is never picked, as Select All leaves it.
	QVector<LevelMapSelectionRef> picked;
	for (const LevelMapSelectionRef& ref : objects) {
		bool world = false;
		if (ref.kind == LevelMapSelectionKind::Entity) {
			for (const LevelMapEntity& entity : std::as_const(m_levelMapDocument.entities)) {
				if (entity.id == ref.objectId) {
					world = entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
					break;
				}
			}
		}
		if (!world) {
			picked << ref;
		}
	}
	if (picked.isEmpty()) {
		statusBar()->showMessage(tr("Nothing in the region can be selected."), 4000);
		return;
	}
	QString error;
	if (!setLevelMapUserSelection(picked, &error)) {
		statusBar()->showMessage(error, 5000);
		return;
	}
	refreshLevelMapWorkbench();
	showLevelSelectionMessage(leftHidden > 0 ? tr("Selected %n object(s) in the region; hidden ones were left out.", nullptr, static_cast<int>(picked.size()))
											 : tr("Selected %n object(s) in the region.", nullptr, static_cast<int>(picked.size())));
}

void ApplicationShell::rememberLevelRegion()
{
	if (m_levelMapDocument.sourcePath.isEmpty()) {
		return;
	}
	const QString key = QStringLiteral("levelRegion/") + QString::fromLatin1(regionKey(m_levelMapDocument.sourcePath));
	if (!levelRegionActive()) {
		m_settings.setShellLayoutState(key, {});
		return;
	}
	const QStringList values {QString::number(m_levelRegionMins.x, 'g', 17), QString::number(m_levelRegionMins.y, 'g', 17),
		QString::number(m_levelRegionMins.z, 'g', 17), QString::number(m_levelRegionMaxs.x, 'g', 17), QString::number(m_levelRegionMaxs.y, 'g', 17),
		QString::number(m_levelRegionMaxs.z, 'g', 17)};
	m_settings.setShellLayoutState(key, values.join(QLatin1Char(' ')).toUtf8());
}

bool ApplicationShell::rememberedLevelRegion(LevelMapVec3* mins, LevelMapVec3* maxs) const
{
	if (m_levelMapDocument.sourcePath.isEmpty()
		|| (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map)) {
		return false;
	}
	const QByteArray stored =
		m_settings.shellLayoutState(QStringLiteral("levelRegion/") + QString::fromLatin1(regionKey(m_levelMapDocument.sourcePath)));
	const QStringList values = QString::fromUtf8(stored).split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (values.size() != 6) {
		return false;
	}
	double numbers[6] {};
	for (int i = 0; i < 6; ++i) {
		bool ok = false;
		numbers[i] = values.at(i).toDouble(&ok);
		if (!ok || !std::isfinite(numbers[i])) {
			return false;
		}
	}
	if (numbers[3] - numbers[0] < 1 || numbers[4] - numbers[1] < 1 || numbers[5] - numbers[2] < 1) {
		return false;
	}
	*mins = {numbers[0], numbers[1], numbers[2], true};
	*maxs = {numbers[3], numbers[4], numbers[5], true};
	return true;
}

// ---------------------------------------------------------------------------
// Navigation

void ApplicationShell::zoomLevelViewsToFit()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return;
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		if (view->hasDocument()) {
			view->zoomToFit();
		}
	}
	statusBar()->showMessage(tr("The 2D views frame the whole map."), 3000);
}

void ApplicationShell::centreLevelViewsOnSelection()
{
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs)) {
		statusBar()->showMessage(tr("Select something to centre the views on."));
		return;
	}
	const LevelMapVec3 centre {(mins.x + maxs.x) / 2, (mins.y + maxs.y) / 2, (mins.z + maxs.z) / 2, true};
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		if (!view->hasDocument()) {
			continue;
		}
		PlanViewState state = view->navigationState();
		state.center = mapViewportProjectPoint(view->projection(), centre);
		view->restoreNavigationState(state);
	}
	statusBar()->showMessage(tr("The 2D views are centred on the selection."), 3000);
}

void ApplicationShell::goToLevelCoordinatesFromUi()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return;
	}
	LevelMapVec3 start {0, 0, 0, true};
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (levelMapSelectionBounds(m_levelMapDocument, &mins, &maxs)) {
		start = {(mins.x + maxs.x) / 2, (mins.y + maxs.y) / 2, (mins.z + maxs.z) / 2, true};
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelGoToDialog"));
	dialog.setWindowTitle(tr("Go to Coordinates"));
	auto* form = new QFormLayout(&dialog);
	auto* point = new QLineEdit(QStringLiteral("%1 %2 %3").arg(start.x, 0, 'g', 8).arg(start.y, 0, 'g', 8).arg(start.z, 0, 'g', 8));
	point->setObjectName(QStringLiteral("levelGoToPoint"));
	point->setAccessibleName(tr("Coordinates, x y z"));
	point->setToolTip(tr("Three numbers, such as 128 -64 32, separated by spaces or commas."));
	auto* camera = new QCheckBox(tr("Take the camera there too"));
	camera->setObjectName(QStringLiteral("levelGoToCamera"));
	camera->setChecked(levelMap3DShowing());
	form->addRow(tr("Point"), point);
	form->addRow(camera);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Go"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	// Three numbers, between spaces or commas.
	const QStringList parts = point->text().split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
	LevelMapVec3 target {0, 0, 0, true};
	bool parsed = parts.size() == 3;
	for (int axis = 0; parsed && axis < 3; ++axis) {
		bool ok = false;
		const double value = parts.at(axis).toDouble(&ok);
		parsed = ok && std::isfinite(value);
		(axis == 0 ? target.x : (axis == 1 ? target.y : target.z)) = value;
	}
	if (!parsed) {
		statusBar()->showMessage(tr("Type three numbers, x y z."), 5000);
		return;
	}
	for (MapViewport* view : std::as_const(m_levelPlanViews)) {
		if (!view->hasDocument()) {
			continue;
		}
		PlanViewState state = view->navigationState();
		state.center = mapViewportProjectPoint(view->projection(), target);
		view->restoreNavigationState(state);
	}
	if (camera->isChecked() && m_levelMap3D) {
		if (!levelMap3DShowing()) {
			setLevelMap3D(true);
		}
		ModelVec3 position;
		position.x = static_cast<float>(target.x);
		position.y = static_cast<float>(target.y);
		position.z = static_cast<float>(target.z);
		m_levelMap3D->setCameraView(position, m_levelMap3D->cameraYaw(), m_levelMap3D->cameraPitch());
	}
	statusBar()->showMessage(tr("The views are at %1, %2, %3.").arg(target.x, 0, 'g', 8).arg(target.y, 0, 'g', 8).arg(target.z, 0, 'g', 8), 4000);
}

void ApplicationShell::findLevelObjectByNumberFromUi()
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap) {
		statusBar()->showMessage(tr("Entity and brush numbers belong to Quake-family maps."));
		return;
	}
	int entity = 0;
	int primitive = 0;
	if (!m_levelMapDocument.selection.isEmpty()) {
		levelMapObjectNumber(m_levelMapDocument, m_levelMapDocument.selection.last(), &entity, &primitive);
		primitive = std::max(0, primitive);
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("levelFindObjectDialog"));
	dialog.setWindowTitle(tr("Find Brush by Number"));
	auto* form = new QFormLayout(&dialog);
	auto* entityField = new QSpinBox;
	entityField->setObjectName(QStringLiteral("levelFindEntity"));
	entityField->setRange(0, 1 << 20);
	entityField->setValue(entity);
	entityField->setAccessibleName(tr("Entity number, worldspawn is 0"));
	auto* primitiveField = new QSpinBox;
	primitiveField->setObjectName(QStringLiteral("levelFindBrush"));
	primitiveField->setRange(-1, 1 << 20);
	primitiveField->setValue(primitive);
	primitiveField->setSpecialValueText(tr("The entity itself"));
	primitiveField->setAccessibleName(tr("Brush or patch number within the entity"));
	auto* message = new QLineEdit;
	message->setObjectName(QStringLiteral("levelFindMessage"));
	message->setPlaceholderText(tr("Or paste a compiler line, such as Entity 0, Brush 12"));
	message->setAccessibleName(tr("Compiler message to read the numbers from"));
	connect(message, &QLineEdit::textChanged, &dialog, [entityField, primitiveField](const QString& text) {
		int e = -1;
		int p = -1;
		if (parseLevelMapObjectNumber(text, &e, &p)) {
			entityField->setValue(e);
			primitiveField->setValue(p);
		}
	});
	form->addRow(tr("Entity"), entityField);
	form->addRow(tr("Brush"), primitiveField);
	form->addRow(message);
	auto* note = new QLabel(tr("Counted from 0 in the order the map file lists them, brushes and patches together, as compilers count them."));
	note->setWordWrap(true);
	form->addRow(note);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Find"));
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	LevelMapSelectionRef ref;
	QString error;
	if (!levelMapObjectAtNumber(m_levelMapDocument, entityField->value(), primitiveField->value(), &ref, &error)) {
		statusBar()->showMessage(error, 6000);
		return;
	}
	if (selectLevelMapObjectBySelector(levelMapSelectionRefId(ref))) {
		frameLevelMapSelection();
		statusBar()->showMessage(tr("Entity %1, brush %2 is %3.").arg(entityField->value()).arg(primitiveField->value()).arg(levelMapSelectionRefId(ref)),
			5000);
	}
}

// ---------------------------------------------------------------------------
// Prefabs

void ApplicationShell::replaceLevelSelectionWithPrefabFromUi()
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap || m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(tr("Select the objects a prefab should take the place of."));
		return;
	}
	const QString start = m_lastLevelPrefabPath.isEmpty() ? m_settings.currentProjectPath() : m_lastLevelPrefabPath;
	const QString path = QFileDialog::getOpenFileName(this, tr("Replace Selection with Prefab"), start, tr("VibeStudio prefabs (*.vprefab)"));
	if (path.isEmpty()) {
		return;
	}
	m_lastLevelPrefabPath = path;
	replaceLevelSelectionWithPrefab(path);
}

void ApplicationShell::placeLevelPrefabFromPalette(const QString& payload, const QPointF& viewPoint)
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a Quake-family map to place prefabs."));
		return;
	}
	const bool entry = payload.startsWith(QStringLiteral("prefab-entry:"));
	const QString path = payload.mid(entry ? 13 : 12);
	LevelPrefab prefab;
	QString error;
	if (entry) {
		auto archive = m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
		if (!archive && m_packageArchive.isOpen()) {
			archive = std::make_shared<const PackageArchive>(m_packageArchive);
		}
		QByteArray bytes;
		if (!archive || !archive->readEntryBytes(path, &bytes, &error, levelPrefabByteLimit) || !parseLevelPrefab(bytes, &prefab, &error)) {
			statusBar()->showMessage(tr("Could not read the prefab: %1").arg(error.isEmpty() ? tr("open the package that holds it") : error), 6000);
			return;
		}
	} else if (!readLevelPrefab(path, &prefab, &error)) {
		statusBar()->showMessage(tr("Could not read the prefab: %1").arg(error), 6000);
		return;
	}
	// Its anchor goes where it was dropped, at the depth new brushes take there.
	LevelPrefabPlacement placement;
	placement.position = m_levelMapViewport->worldPositionAt(viewPoint, levelMapHiddenAxisValue());
	placement.textureLock = m_settings.levelTextureLock();
	LevelPrefabReport report;
	if (!insertLevelPrefab(&m_levelMapDocument, prefab, placement, &report, &error)) {
		statusBar()->showMessage(tr("Could not place the prefab: %1").arg(error), 6000);
		return;
	}
	recordActivity(tr("Prefab placed"), prefab.name, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	QString message = tr("Placed %1 at %2, %3, %4; Insert Prefab… turns one before placing it.")
						  .arg(prefab.name)
						  .arg(placement.position.x, 0, 'g', 8)
						  .arg(placement.position.y, 0, 'g', 8)
						  .arg(placement.position.z, 0, 'g', 8);
	if (!report.externalTargets.isEmpty()) {
		message += QLatin1Char(' ') + tr("%n link(s) name objects outside the prefab.", nullptr, static_cast<int>(report.externalTargets.size()));
	}
	statusBar()->showMessage(message, 6000);
}

bool ApplicationShell::replaceLevelSelectionWithPrefab(const QString& path)
{
	LevelPrefab prefab;
	QString error;
	if (!readLevelPrefab(path, &prefab, &error)) {
		statusBar()->showMessage(tr("Could not read the prefab: %1").arg(error), 6000);
		return false;
	}
	LevelPrefabPlacement placement;
	placement.textureLock = m_settings.levelTextureLock();
	LevelPrefabReport report;
	if (!replaceLevelMapSelectionWithPrefab(&m_levelMapDocument, prefab, placement, &report, &error)) {
		statusBar()->showMessage(tr("Could not replace the selection: %1").arg(error), 6000);
		return false;
	}
	recordActivity(tr("Selection replaced with a prefab"), prefab.name, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Replaced the selection with %1; Undo puts it back.").arg(prefab.name), 6000);
	return true;
}

} // namespace vibestudio
