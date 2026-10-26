// The Levels Tools tab, after Blender's Tool tab in the sidebar: every
// editing command in one place, grouped, each button the command itself, so
// its enabled state, check state and key follow the menus and command search.
// Groups a map's format cannot use are hidden.

#include "app/application_shell.h"
#include "app/studio_actions.h"
#include "app/studio_sidebar.h"

#include <QAction>
#include <QCoreApplication>
#include <QPushButton>
#include <QVBoxLayout>

namespace vibestudio {

namespace {

struct ToolGroup {
	const char* id;
	const char* title;
	QList<const char*> commands;
};

const QList<ToolGroup>& toolGroups()
{
	static const QList<ToolGroup> groups {
		{"clipboard", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Clipboard"),
			{"map.copySelection", "map.cutSelection", "map.copySurface", "map.paste", "map.pasteAtOriginal", "map.pasteAtCursor", "map.pasteToCamera", "map.pasteWithOffset", "map.pasteSurface"}},
		{"brush", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Brush"),
			{"map.clipTool", "map.quickClip", "map.clipSelection", "map.clipperOptions", "map.hollowSelection", "map.hollowTouching", "map.makeRoom", "map.makeRoomOptions", "map.carve", "map.mergeBrushes",
				"map.wrapMerge", "map.wrapMergePoints", "map.intersect", "map.editBrushComponents", "map.makeDetail", "map.makeStructural"}},
		{"vertices", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Vertices"),
			{"map.vertexTool", "map.edgeTool", "map.faceTool", "map.componentMoveTool", "map.componentRotateTool", "map.componentScaleTool", "map.componentShearTool", "map.componentPivotTool", "map.componentResetPivot", "map.componentGizmoOptions", "map.transformComponents", "map.snapPickedVertex", "map.componentUvLock", "map.componentUvFit", "map.componentXray", "map.insertVertexTool", "map.splitComponents", "map.extrudeFaces", "map.extrudeSplit", "map.extrudePrecisely", "map.sweepTool", "map.sweepApply", "map.sweepDecreaseScale", "map.sweepIncreaseScale", "map.sweepFaces", "map.removeComponents", "map.snapVertices"}},
		{"component-selection", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Component Selection"),
			{"map.nextComponent", "map.previousComponent", "map.componentTouchingEdges", "map.componentTouchingFaces", "map.growComponents", "map.shrinkComponents", "map.connectedComponents"}},
		{"transform", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Transform"),
			{"map.moveSelection", "map.resizeSelection", "map.scaleSelection", "map.rotatePrecisely", "map.rotateLeft", "map.rotateRight", "map.rollClockwise",
				"map.rollCounterclockwise", "map.pitchClockwise", "map.pitchCounterclockwise", "map.flipHorizontal",
				"map.flipVertical", "map.flipX", "map.flipY", "map.flipZ", "map.rotateX", "map.rotateY", "map.rotateZ", "map.shearTool", "map.shearSelection", "map.snapToGrid", "map.roundToGrid", "map.dropToFloor", "map.duplicateWithOffset", "map.duplicateUnique",
				"map.repeat", "map.clearRepeat", "map.copyRepeatRecipe", "map.repeatTransforms", "map.resetRepeatTransforms"}},
		{"nudge", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Nudge"),
			{"map.nudgeLeft", "map.nudgeRight", "map.nudgeForward", "map.nudgeBack", "map.nudgeDepthUp", "map.nudgeDepthDown", "map.nudgeWorldUp",
				"map.nudgeWorldDown"}},
		{"duplicate-move", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Duplicate and Move"),
			{"map.duplicateMoveLeft", "map.duplicateMoveRight", "map.duplicateMoveForward", "map.duplicateMoveBack", "map.duplicateMoveDepthUp",
				"map.duplicateMoveDepthDown", "map.moveToCamera"}},
		{"align", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Align"),
			{"map.alignLeft", "map.alignRight", "map.alignTop", "map.alignBottom", "map.alignCentreHorizontal", "map.alignCentreVertical"}},
		{"select", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Select"),
			{"map.selectAll", "map.selectNone", "map.invertSelection", "map.selectSiblings", "map.expandSelectionToLayers", "map.selectSourceLines", "map.selectConnectedEntities", "map.selectAllOfType", "map.selectTextured", "map.selectSimilar", "map.selectByTexture", "map.selectTargets", "map.selectSources",
				"map.selectWholeEntities", "map.selectInside", "map.selectTouching", "map.selectCompleteTall", "map.selectPartialTall", "map.hideSelection",
				"map.isolateSelection", "map.showAll"}},
		{"navigate", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Navigate"),
			{"map.frameSelection", "map.focusCameraOnSelection", "map.levelCamera", "map.resetCameraZoom", "map.centerOnSelection", "map.zoomToFit", "map.goToCoordinates", "map.findObject"}},
		{"entities", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Entities"),
			{"map.addEntity", "map.createEntity", "map.tieToEntity", "map.moveToWorld", "map.movePrimitivesToEntity", "map.movePrimitivesToFirstEntity", "map.moveSelectedToWorld",
				"map.explodeEntity", "map.explodeEntityUnique", "map.connectEntities", "map.killConnectEntities", "map.replaceKeyValues", "map.setEntityColour",
					"map.normaliseEntityColour", "map.replaceWithPrefab"}},
		{"groups", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Groups"),
			{"map.openGroup", "map.closeGroup", "map.closeAllGroups", "map.moveToLayer", "map.createLinkedDuplicate", "map.selectLinkedGroups", "map.updateLinkedGroups", "map.separateLinkedGroups", "map.addProtectedProperty", "map.clearProtectedProperties", "map.extractLinkedGroups"}},
		{"surfaces", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Surfaces"),
			{"map.applyTexture", "map.replaceTexture", "map.revealMaterial", "map.alignSurfaces", "map.paintMaterial", "map.sampleMaterial",
				"map.surfaceReset", "map.surfaceRadiantReset", "map.resetTextures"}},
		{"camera-textures", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Move Textures from Camera"),
			{"map.moveTexturesUp", "map.moveTexturesDown", "map.moveTexturesLeft", "map.moveTexturesRight", "map.moveTexturesUpCoarse",
				"map.moveTexturesDownCoarse", "map.moveTexturesLeftCoarse", "map.moveTexturesRightCoarse", "map.moveTexturesUpFine",
				"map.moveTexturesDownFine", "map.moveTexturesLeftFine", "map.moveTexturesRightFine", "map.flipTexturesHorizontally",
				"map.flipTexturesVertically"}},
		{"rotate-textures", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Rotate Textures"),
			{"map.rotateTexturesClockwise", "map.rotateTexturesClockwiseCoarse", "map.rotateTexturesClockwiseFine",
				"map.rotateTexturesCounterclockwise", "map.rotateTexturesCounterclockwiseCoarse", "map.rotateTexturesCounterclockwiseFine"}},
		{"patches", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Patches"),
			{"map.addPatch", "map.editPatch", "map.stitchPatches", "map.capPatch", "map.thickenPatch", "map.patchInvert", "map.patchTranspose"}},
		{"patchMatrix", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Patch Rows and Columns"),
			{"map.patchInsertColumns", "map.patchAddColumns", "map.patchInsertRows", "map.patchAddRows", "map.patchDeleteFirstColumns",
				"map.patchDeleteLastColumns", "map.patchDeleteFirstRows", "map.patchDeleteLastRows", "map.patchRedisperseColumns",
				"map.patchRedisperseRows", "map.patchSmoothColumns", "map.patchSmoothRows"}},
		{"doom", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Sectors and Lines"),
			{"map.drawSector", "map.makeSectorMode", "map.addSector", "map.addThing", "map.splitLinedefs", "map.curveLinedefs", "map.flipLinedefs", "map.mergeVertices",
				"map.joinSectors", "map.mergeSectors", "map.makeDoor", "map.raiseCameraSurface", "map.lowerCameraSurface", "map.brightenCameraSurface",
				"map.darkenCameraSurface", "map.nudgeCameraTextureLeft", "map.nudgeCameraTextureRight", "map.nudgeCameraTextureUp",
				"map.nudgeCameraTextureDown", "map.dragCameraTextures", "map.alignWallTexturesCrosshair", "map.alignWallTextures"}},
		{"region", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Region and Build"),
			{"map.regionSetSelection", "map.regionSetView", "map.regionDraw", "map.regionSetBrush", "map.regionBounds", "map.regionSelect",
				"map.regionClear", "map.regionSave", "map.regionCompile", "map.loadLeakTrail", "map.loadPortals"}},
	};
	return groups;
}

} // namespace

void ApplicationShell::buildLevelToolsPage(SidebarPage* page)
{
	for (const ToolGroup& group : toolGroups()) {
		auto* body = new QWidget;
		body->setObjectName(QStringLiteral("levelTools-%1").arg(QLatin1String(group.id)));
		auto* layout = new QVBoxLayout(body);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(1);
		for (const char* id : group.commands) {
			QAction* action = m_commands ? m_commands->action(QString::fromLatin1(id)) : nullptr;
			if (!action) {
				continue;
			}
			auto* button = new QPushButton(action->icon(), QString(action->text()).remove(QLatin1Char('&')));
			button->setObjectName(QStringLiteral("levelToolsRow-") + QString::fromLatin1(id));
			button->setProperty("sidebarToolRow", true);
			button->setAccessibleName(button->text());
			button->setFocusPolicy(Qt::TabFocus);
			bindCommandButton(button, action);
			if (action->isCheckable()) {
				button->setCheckable(true);
				button->setChecked(action->isChecked());
				connect(action, &QAction::toggled, button, &QPushButton::setChecked);
			}
			layout->addWidget(button);
		}
		// The first two groups start open; the rest wait to be asked for.
		const QString groupId = QLatin1String(group.id);
		SidebarSection* section = addLevelSidebarSection(page, QStringLiteral("tools.") + groupId, QCoreApplication::translate("VibeStudioLevelTools", group.title), body, 0,
			groupId == QLatin1String("brush") || groupId == QLatin1String("transform"));
		m_levelToolSections.insert(groupId, section);
		// NetRadiant Custom's CSG Tool window, as the section after Brush
		// (level_csg_tool_actions.cpp); CSG Tool opens it.
		if (groupId == QLatin1String("brush")) {
			m_levelToolSections.insert(QStringLiteral("csg"), addLevelSidebarSection(page, QStringLiteral("tools.csg"),
				QCoreApplication::translate("VibeStudioLevelTools", "CSG Tool"), buildLevelCsgToolPanel(), 0, false));
		}
	}
	refreshLevelToolsPage();
}

void ApplicationShell::refreshLevelToolsPage()
{
	if (m_levelToolSections.isEmpty()) {
		return;
	}
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool doom = m_levelMapDocument.format == LevelMapFormat::DoomWad;
	const bool unknown = m_levelMapDocument.format == LevelMapFormat::Unknown;
	// What a format cannot use is hidden, not just disabled.
	const QHash<QString, bool> shown {
		{QStringLiteral("clipboard"), quake || unknown},
		{QStringLiteral("brush"), quake || unknown},
		{QStringLiteral("csg"), quake || unknown},
		{QStringLiteral("vertices"), quake || unknown},
		{QStringLiteral("component-selection"), quake || unknown},
		{QStringLiteral("patchMatrix"), m_levelMapDocument.format == LevelMapFormat::Quake3Map},
		{QStringLiteral("entities"), quake || unknown},
		{QStringLiteral("groups"), true},
		{QStringLiteral("patches"), m_levelMapDocument.format == LevelMapFormat::Quake3Map},
		{QStringLiteral("camera-textures"), quake || unknown},
		{QStringLiteral("rotate-textures"), quake || unknown},
		{QStringLiteral("doom"), doom},
		{QStringLiteral("region"), quake || unknown},
	};
	for (auto it = m_levelToolSections.cbegin(); it != m_levelToolSections.cend(); ++it) {
		it.value()->setVisible(shown.value(it.key(), true));
	}
}

} // namespace vibestudio
