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
		{"brush", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Brush"),
			{"map.clipTool", "map.clipSelection", "map.hollowSelection", "map.carve", "map.mergeBrushes", "map.intersect", "map.editBrushComponents",
				"map.makeDetail", "map.makeStructural"}},
		{"transform", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Transform"),
			{"map.moveSelection", "map.resizeSelection", "map.rotatePrecisely", "map.rotateLeft", "map.rotateRight", "map.flipHorizontal",
				"map.flipVertical", "map.shearTool", "map.shearSelection", "map.snapToGrid", "map.dropToFloor", "map.duplicateWithOffset"}},
		{"align", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Align"),
			{"map.alignLeft", "map.alignRight", "map.alignTop", "map.alignBottom", "map.alignCentreHorizontal", "map.alignCentreVertical"}},
		{"select", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Select"),
			{"map.selectAll", "map.selectNone", "map.invertSelection", "map.selectSimilar", "map.selectByTexture", "map.selectTargets", "map.selectSources",
				"map.selectInside", "map.selectTouching", "map.selectCompleteTall", "map.selectPartialTall", "map.hideSelection", "map.isolateSelection",
				"map.showAll"}},
		{"entities", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Entities"),
			{"map.addEntity", "map.tieToEntity", "map.moveToWorld", "map.connectEntities", "map.replaceKeyValues"}},
		{"groups", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Linked Groups"),
			{"map.createLinkedCopy", "map.selectLinkedCopies", "map.updateLinkedCopies", "map.unlinkCopy"}},
		{"surfaces", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Surfaces"),
			{"map.applyTexture", "map.replaceTexture", "map.alignSurfaces", "map.paintMaterial", "map.sampleMaterial"}},
		{"patches", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Patches"), {"map.addPatch", "map.editPatch", "map.stitchPatches", "map.capPatch"}},
		{"doom", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Sectors and Lines"),
			{"map.drawSector", "map.makeSectorMode", "map.addSector", "map.addThing", "map.splitLinedefs", "map.curveLinedefs", "map.flipLinedefs", "map.mergeVertices",
				"map.joinSectors", "map.mergeSectors", "map.makeDoor", "map.raiseCameraSurface", "map.lowerCameraSurface", "map.brightenCameraSurface",
				"map.darkenCameraSurface", "map.nudgeCameraTextureLeft", "map.nudgeCameraTextureRight", "map.nudgeCameraTextureUp",
				"map.nudgeCameraTextureDown", "map.dragCameraTextures", "map.alignWallTexturesCrosshair", "map.alignWallTextures"}},
		{"region", QT_TRANSLATE_NOOP("VibeStudioLevelTools", "Region and Build"),
			{"map.regionSetSelection", "map.regionSetView", "map.regionClear", "map.regionSave", "map.regionCompile", "map.loadLeakTrail", "map.loadPortals"}},
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
		{QStringLiteral("brush"), quake || unknown},
		{QStringLiteral("entities"), quake || unknown},
		{QStringLiteral("groups"), quake || unknown},
		{QStringLiteral("patches"), m_levelMapDocument.format == LevelMapFormat::Quake3Map},
		{QStringLiteral("doom"), doom},
		{QStringLiteral("region"), quake || unknown},
	};
	for (auto it = m_levelToolSections.cbegin(); it != m_levelToolSections.cend(); ++it) {
		it.value()->setVisible(shown.value(it.key(), true));
	}
}

} // namespace vibestudio
