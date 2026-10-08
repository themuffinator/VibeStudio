// The Levels page's sidebars: which panels each tab holds, where the tabs sit
// (core/level_sidebar.h, by editor profile), what is remembered between
// sessions, and the commands that show each tab.

#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_icons.h"
#include "app/tile_grid.h"
#include "app/studio_layout.h"
#include "app/studio_sidebar.h"
#include "core/editor_profiles.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio {

namespace {

QString sidebarKey(const QString& name)
{
	return QStringLiteral("levelSidebar/") + name;
}

QByteArray onOff(bool on)
{
	return on ? QByteArrayLiteral("on") : QByteArrayLiteral("off");
}

// A check box that shows and drives a checkable action, so the sidebar, the
// menus and the command search never disagree about a setting.
QCheckBox* actionCheckBox(QAction* action, const QString& objectName, QWidget* owner)
{
	auto* box = new QCheckBox;
	box->setObjectName(objectName);
	box->setFocusPolicy(Qt::TabFocus);
	const auto sync = [box, action]() {
		const QSignalBlocker blocker(box);
		QString text = action->text();
		text.remove(QLatin1Char('&'));
		box->setText(text);
		box->setToolTip(action->toolTip());
		box->setAccessibleDescription(action->toolTip());
		box->setChecked(action->isChecked());
		box->setEnabled(action->isEnabled());
		box->setVisible(action->isVisible());
	};
	sync();
	QObject::connect(action, &QAction::changed, owner, sync);
	QObject::connect(action, &QAction::toggled, owner, sync);
	QObject::connect(box, &QCheckBox::toggled, owner, [action](bool checked) {
		if (action->isChecked() != checked) {
			// Commands that act on a trigger (the view links) toggle there.
			action->trigger();
			if (action->isChecked() != checked && action->isCheckable()) {
				action->setChecked(checked);
			}
		}
	});
	return box;
}

QToolButton* actionButton(QAction* action, const QString& objectName)
{
	auto* button = new QToolButton;
	button->setObjectName(objectName);
	button->setDefaultAction(action);
	button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	button->setFocusPolicy(Qt::TabFocus);
	button->setAutoRaise(true);
	setBaseIconSize(button, QSize(16, 16));
	return button;
}

} // namespace

SidebarPage* ApplicationShell::createLevelSidebarPage(const QString& tabId, bool scrollable)
{
	LevelSidebarTab tab;
	if (!levelSidebarTabForId(tabId, &tab)) {
		tab.id = tabId;
		tab.iconName = QStringLiteral("list");
		tab.title = tabId;
	}
	auto* page = new SidebarPage(tab.id, tab.iconName, tab.title, tab.description);
	page->setObjectName(QStringLiteral("levelSidebarPage-%1").arg(tab.id));
	page->setScrollable(scrollable);
	m_levelSidebarPages.insert(tab.id, page);
	connect(page, &SidebarPage::sectionExpandedChanged, this, [this](const QString& sectionId, bool expanded) {
		if (!m_restoringLevelSidebars) {
			m_settings.setShellLayoutState(sidebarKey(QStringLiteral("section/") + sectionId), onOff(expanded));
		}
	});
	return page;
}

SidebarSection* ApplicationShell::addLevelSidebarSection(SidebarPage* page, const QString& sectionId, const QString& title, QWidget* body, int stretch,
	bool expandedByDefault)
{
	SidebarSection* section = page->addSection(sectionId, title, body, stretch);
	restoreLevelSidebarSection(section, expandedByDefault);
	return section;
}

void ApplicationShell::restoreLevelSidebarSection(SidebarSection* section, bool expandedByDefault)
{
	const QByteArray saved = m_settings.shellLayoutState(sidebarKey(QStringLiteral("section/") + section->sectionId()));
	const QScopedValueRollback<bool> restoring(m_restoringLevelSidebars, true);
	section->setExpanded(saved.isEmpty() ? expandedByDefault : saved == QByteArrayLiteral("on"));
}

void ApplicationShell::buildLevelSidebars(const LevelSidebarContent& content)
{
	// Outliner: every object, and the map's layers and groups.
	SidebarPage* outliner = createLevelSidebarPage(QStringLiteral("outliner"), false);
	m_levelObjectsSection = addLevelSidebarSection(outliner, QStringLiteral("outliner.objects"), tr("Objects"), content.objects, 3);
	addLevelSidebarSection(outliner, QStringLiteral("outliner.scene"), tr("Layers and Groups"), content.scene, 2, false);

	// Entities: the classes to place, what the chosen one is, and where it goes.
	SidebarPage* entities = createLevelSidebarPage(QStringLiteral("entities"), false);
	addLevelSidebarSection(entities, QStringLiteral("entities.classes"), tr("Classes"), content.palette, 1);
	m_levelEntityClassInfo = new QLabel;
	m_levelEntityClassInfo->setObjectName(QStringLiteral("levelEntityClassInfo"));
	m_levelEntityClassInfo->setWordWrap(true);
	m_levelEntityClassInfo->setTextFormat(Qt::PlainText);
	m_levelEntityClassInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_levelEntityClassInfo->setAccessibleName(tr("About the chosen class"));
	addLevelSidebarSection(entities, QStringLiteral("entities.about"), tr("About"), m_levelEntityClassInfo);
	auto* place = new QWidget;
	auto* placeLayout = new QVBoxLayout(place);
	placeLayout->setContentsMargins(0, 0, 0, 0);
	placeLayout->setSpacing(6);
	auto* placeInView = createButton(tr("Place in View"), QStringLiteral("crosshair"));
	placeInView->setObjectName(QStringLiteral("levelPlaceInView"));
	placeInView->setAccessibleName(tr("Place the chosen class in the middle of the active view"));
	placeInView->setToolTip(tr("Place the chosen class or thing in the middle of the active 2D view. You can also drag it onto any view."));
	m_levelPlaceInView = placeInView;
	connect(placeInView, &QPushButton::clicked, this, &ApplicationShell::placeLevelPaletteInView);
	placeLayout->addWidget(placeInView);
	if (content.placement) {
		placeLayout->addWidget(content.placement);
	}
	addLevelSidebarSection(entities, QStringLiteral("entities.place"), tr("Place"), place);
	connect(m_levelMapPalette, &QTreeWidget::currentItemChanged, this, [this]() { refreshLevelEntityClassInfo(); });

	// Shapes: the brush shapes to draw or add, and Doom's sector shapes.
	buildLevelShapesPage(createLevelSidebarPage(QStringLiteral("shapes"), false));

	// Textures: the map's and the package's, to apply or paint.
	SidebarPage* textures = createLevelSidebarPage(QStringLiteral("textures"), false);
	m_levelTexturesSection = addLevelSidebarSection(textures, QStringLiteral("textures.browser"), tr("In This Map"), content.textures, 1);
	m_levelTexturesAll = m_settings.shellLayoutState(QStringLiteral("levelTextures/all")) == QByteArrayLiteral("on");
	auto* all = m_levelTexturesSection->addHeaderButton(QStringLiteral("package"), tr("Package Textures"),
		tr("List every texture in the open package as well as the ones the map uses."));
	all->setObjectName(QStringLiteral("levelTexturesAll"));
	all->setCheckable(true);
	all->setChecked(m_levelTexturesAll);
	m_levelTexturesSection->setTitle(m_levelTexturesAll ? tr("Map and Package") : tr("In This Map"));
	connect(all, &QToolButton::toggled, this, [this](bool on) {
		m_levelTexturesAll = on;
		m_settings.setShellLayoutState(QStringLiteral("levelTextures/all"), onOff(on));
		if (m_levelTexturesSection) {
			m_levelTexturesSection->setTitle(on ? tr("Map and Package") : tr("In This Map"));
		}
		refreshLevelMapTextures();
	});
	auto* tiles = m_levelTexturesSection->addHeaderButton(QStringLiteral("grid"), tr("Tile Size"), tr("Make the texture tiles smaller or larger."));
	tiles->setObjectName(QStringLiteral("levelTextureTileSize"));
	tiles->setPopupMode(QToolButton::InstantPopup);
	auto* sizes = new QMenu(tiles);
	sizes->setObjectName(QStringLiteral("levelTextureTileMenu"));
	auto* sizeGroup = new QActionGroup(sizes);
	bool savedSize = false;
	const int saved = m_settings.shellLayoutState(QStringLiteral("levelTextures/tile")).toInt(&savedSize);
	for (const auto& [pixels, label] : {std::pair {40, tr("Small")}, std::pair {56, tr("Medium")}, std::pair {80, tr("Large")}, std::pair {112, tr("Largest")}}) {
		QAction* choice = sizes->addAction(label);
		choice->setCheckable(true);
		choice->setChecked(savedSize ? saved == pixels : pixels == 56);
		sizeGroup->addAction(choice);
		connect(choice, &QAction::triggered, this, [this, pixels]() { setLevelTextureTileSize(pixels); });
	}
	tiles->setMenu(sizes);
	if (savedSize && saved >= 24 && saved <= 256) {
		setBaseIconSize(m_levelMapTextures, QSize(saved, saved));
	}

	// Models, sounds and prefabs browse the package and the project.
	buildLevelModelBrowser(createLevelSidebarPage(QStringLiteral("models"), false));
	buildLevelSoundBrowser(createLevelSidebarPage(QStringLiteral("sounds"), false));
	buildLevelPrefabBrowser(createLevelSidebarPage(QStringLiteral("prefabs"), false));

	// Inspector: the selection's keys and fields.
	SidebarPage* inspector = createLevelSidebarPage(QStringLiteral("inspector"), false);
	m_levelTransformSection = addLevelSidebarSection(inspector, QStringLiteral("inspector.transform"), tr("Transform"), buildLevelTransformPanel());
	SidebarSection* properties = addLevelSidebarSection(inspector, QStringLiteral("inspector.properties"), tr("Properties"), content.inspector, 1);
	auto* editKey = properties->addHeaderButton(QStringLiteral("edit"), tr("Edit Key…"), tr("Change a key on the selected map object."));
	editKey->setObjectName(QStringLiteral("levelMapEditProperty"));
	editKey->setAccessibleName(tr("Edit selected entity key"));
	connect(editKey, &QToolButton::clicked, this, [this]() { editSelectedLevelMapProperty(); });
	m_levelMapEditProperty = editKey;
	auto* replaceKeys = properties->addHeaderButton(QStringLiteral("search"), tr("Replace Key Values…"),
		tr("Find a value of one key across the map's entities and replace it."));
	replaceKeys->setObjectName(QStringLiteral("levelReplaceKeyValues"));
	connect(replaceKeys, &QToolButton::clicked, this, [this]() { replaceLevelMapKeyValuesFromUi(); });
	auto* move = properties->addHeaderButton(QStringLiteral("move"), tr("Move…"), tr("Translate the selected map object by a delta."));
	move->setObjectName(QStringLiteral("levelMapMoveSelection"));
	move->setAccessibleName(tr("Move selected map object"));
	connect(move, &QToolButton::clicked, this, [this]() { moveSelectedLevelMapObject(); });
	m_levelMapMoveSelection = move;

	// Tools: every editing command, grouped.
	buildLevelToolsPage(createLevelSidebarPage(QStringLiteral("tools"), false));

	// Surfaces: texture alignment for the selection or the inspected face. The
	// surface tools scroll by themselves and bring their own sections.
	SidebarPage* surfaces = createLevelSidebarPage(QStringLiteral("surfaces"), false);
	surfaces->addWidget(content.surfaces, 1);
	if (m_levelSurfaceTools) {
		for (SidebarSection* section : m_levelSurfaceTools->sections()) {
			surfaces->adoptSection(section);
			restoreLevelSidebarSection(section, true);
		}
	}

	// Map: what the map is and what it is checked against.
	SidebarPage* map = createLevelSidebarPage(QStringLiteral("map"), true);
	buildLevelMapSettings(map);
	if (auto* statistics = qobject_cast<QAbstractItemView*>(content.statistics)) {
		statistics->setMinimumHeight(140);
	}
	addLevelSidebarSection(map, QStringLiteral("map.statistics"), tr("Statistics"), content.statistics, 0, false);
	addLevelSidebarSection(map, QStringLiteral("map.definitions"), tr("Entity Definitions"), content.definitions);
	if (content.details) {
		content.details->setMinimumHeight(220);
	}
	addLevelSidebarSection(map, QStringLiteral("map.details"), tr("Raw Details"), content.details, 0, false);
	if (content.outline) {
		content.outline->setMinimumHeight(180);
	}
	addLevelSidebarSection(map, QStringLiteral("map.outline"), tr("Text Outline"), content.outline, 0, false);

	// View: what the views draw, how they are laid out and linked.
	SidebarPage* view = createLevelSidebarPage(QStringLiteral("view"), true);
	addLevelSidebarSection(view, QStringLiteral("view.display"), tr("Display"), buildLevelViewOptions());
	addLevelSidebarSection(view, QStringLiteral("view.filters"), tr("Filters"), buildLevelFilterOptions());
	addLevelSidebarSection(view, QStringLiteral("view.region"), tr("Region"), buildLevelRegionPanel());
	addLevelSidebarSection(view, QStringLiteral("view.layout"), tr("Layout"), buildLevelLayoutOptions());
	addLevelSidebarSection(view, QStringLiteral("view.navigation"), tr("Navigation"), buildLevelNavigationOptions());

	// Health and history.
	SidebarPage* health = createLevelSidebarPage(QStringLiteral("health"), false);
	addLevelSidebarSection(health, QStringLiteral("health.problems"), tr("Problems"), content.health, 1);
	SidebarPage* history = createLevelSidebarPage(QStringLiteral("history"), false);
	if (m_commands) {
		for (const auto& id : {QStringLiteral("map.undo"), QStringLiteral("map.redo")}) {
			if (QAction* action = m_commands->action(id)) {
				auto* button = new QToolButton;
				button->setObjectName(QStringLiteral("sidebarHeaderButton"));
				button->setDefaultAction(action);
				button->setToolButtonStyle(Qt::ToolButtonIconOnly);
				button->setAutoRaise(true);
				button->setFocusPolicy(Qt::TabFocus);
				setBaseIconSize(button, QSize(16, 16));
				history->addHeaderWidget(button);
			}
		}
	}
	history->addWidget(content.history, 1);

	m_levelLeadingSidebar = new StudioSidebar(tr("Level browsers"), StudioSidebar::TabEdge::Trailing);
	m_levelLeadingSidebar->setObjectName(QStringLiteral("levelLeadingSidebar"));
	m_levelTrailingSidebar = new StudioSidebar(tr("Level properties"), StudioSidebar::TabEdge::Leading);
	m_levelTrailingSidebar->setObjectName(QStringLiteral("levelTrailingSidebar"));
	for (StudioSidebar* sidebar : {m_levelLeadingSidebar, m_levelTrailingSidebar}) {
		connect(sidebar, &StudioSidebar::currentPageChanged, this, [this](const QString& pageId) {
			saveLevelSidebarState();
			if (pageId == QLatin1String("models") || pageId == QLatin1String("sounds") || pageId == QLatin1String("prefabs")) {
				refreshLevelAssetBrowsers();
			} else if (pageId == QLatin1String("view")) {
				refreshLevelViewFilters();
			} else if (pageId == QLatin1String("map")) {
				refreshLevelMapSettings();
			}
		});
		connect(sidebar, &StudioSidebar::foldedChanged, this, [this]() { saveLevelSidebarState(); });
		connect(sidebar, &StudioSidebar::tabMenuRequested, this, [this, sidebar](const QString& pageId, const QPoint& global) {
			showLevelSidebarTabMenu(sidebar, pageId, global);
		});
	}
	m_levelSidebarFamily = levelSidebarFamilyForProfile(m_settings.selectedEditorProfileId());
	arrangeLevelSidebars(savedLevelSidebarArrangement());
	const bool captions = m_settings.shellLayoutState(sidebarKey(QStringLiteral("captions"))) == QByteArrayLiteral("on");
	m_levelLeadingSidebar->setShowLabels(captions);
	m_levelTrailingSidebar->setShowLabels(captions);
	refreshLevelEntityClassInfo();
}

QWidget* ApplicationShell::buildLevelViewOptions()
{
	auto* panel = new QWidget;
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	const QList<QPair<QAction*, QString>> toggles {
		{m_levelMapShowGrid, QStringLiteral("levelViewShowGrid")},
		{m_levelMapShowThings, QStringLiteral("levelViewShowThings")},
		{m_levelMapShowSectors, QStringLiteral("levelViewShowSectors")},
		{m_levelMapShowVertices, QStringLiteral("levelViewShowVertices")},
		{m_levelMapShowLabels, QStringLiteral("levelViewShowLabels")},
		{m_levelMapShowLinks, QStringLiteral("levelViewShowLinks")},
		{m_levelPreviewTextured, QStringLiteral("levelViewTextured")},
	};
	for (const auto& [action, name] : toggles) {
		if (action) {
			layout->addWidget(actionCheckBox(action, name, this));
		}
	}
	if (QAction* wireframe = m_commands ? m_commands->action(QStringLiteral("map.previewWireframe")) : nullptr) {
		layout->addWidget(actionCheckBox(wireframe, QStringLiteral("levelViewWireframe"), this));
	}

	// The grid and snapping, mirrored from the view bar so either one works.
	auto* gridRow = new QWidget;
	auto* gridForm = new QFormLayout(gridRow);
	gridForm->setContentsMargins(0, 6, 0, 0);
	gridForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto* grid = new QComboBox;
	grid->setObjectName(QStringLiteral("levelViewGrid"));
	grid->setAccessibleName(tr("Grid size"));
	grid->setToolTip(tr("Grid spacing in world units, the same setting as the view bar's grid."));
	if (m_levelMapGrid) {
		for (int index = 0; index < m_levelMapGrid->count(); ++index) {
			grid->addItem(tr("%n unit(s)", nullptr, m_levelMapGrid->itemData(index).toInt()), m_levelMapGrid->itemData(index));
		}
		grid->setCurrentIndex(m_levelMapGrid->currentIndex());
		connect(grid, &QComboBox::currentIndexChanged, this, [this](int index) {
			if (m_levelMapGrid && m_levelMapGrid->currentIndex() != index) {
				m_levelMapGrid->setCurrentIndex(index);
			}
		});
		connect(m_levelMapGrid, &QComboBox::currentIndexChanged, grid, [grid](int index) {
			const QSignalBlocker blocker(grid);
			grid->setCurrentIndex(index);
		});
	}
	auto* gridLabel = new QLabel(tr("Grid"));
	gridLabel->setObjectName(QStringLiteral("sidebarFieldLabel"));
	gridLabel->setBuddy(grid);
	gridForm->addRow(gridLabel, grid);
	layout->addWidget(gridRow);
	if (m_levelMapSnap) {
		auto* snap = new QCheckBox(m_levelMapSnap->text());
		snap->setObjectName(QStringLiteral("levelViewSnap"));
		snap->setFocusPolicy(Qt::TabFocus);
		snap->setToolTip(m_levelMapSnap->toolTip());
		snap->setChecked(m_levelMapSnap->isChecked());
		connect(snap, &QCheckBox::toggled, m_levelMapSnap, &QCheckBox::setChecked);
		connect(m_levelMapSnap, &QCheckBox::toggled, snap, [snap](bool checked) {
			const QSignalBlocker blocker(snap);
			snap->setChecked(checked);
		});
		layout->addWidget(snap);
	}
	if (m_commands) {
		for (const auto& id : {QStringLiteral("map.textureLock"), QStringLiteral("map.textureScaleLock")}) {
			if (QAction* action = m_commands->action(id)) {
				layout->addWidget(actionCheckBox(action, QStringLiteral("levelView-") + id.section(QLatin1Char('.'), 1), this));
			}
		}
	}
	return panel;
}

QWidget* ApplicationShell::buildLevelLayoutOptions()
{
	auto* panel = new QWidget;
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	if (!m_commands) {
		return panel;
	}
	// Tiles picturing the panes, filled where the camera is, with short names;
	// the full name is the tooltip and the accessible name.
	auto* group = new QButtonGroup(panel);
	group->setExclusive(true);
	auto* tiles = new TileGrid(4);
	tiles->setObjectName(QStringLiteral("levelLayoutTiles"));
	const QList<std::pair<QString, QString>> entries = {{QStringLiteral("profile"), tr("Profile")}, {QStringLiteral("single-2d"), tr("2D")},
		{QStringLiteral("single-3d"), tr("3D")}, {QStringLiteral("camera-and-plan"), tr("3D and 2D")}, {QStringLiteral("four-views"), tr("Four Views")},
		{QStringLiteral("camera-above-plans"), tr("3D Above")}, {QStringLiteral("camera-beside-plans"), tr("3D Beside")}};
	for (const auto& [id, label] : entries) {
		QAction* action = m_commands->action(QStringLiteral("map.layout.") + id);
		if (!action) {
			continue;
		}
		QString name = action->text();
		name.remove(QLatin1Char('&'));
		if (const int colon = name.indexOf(QStringLiteral(": ")); colon >= 0) {
			name = name.mid(colon + 2);
		}
		auto* tile = new QToolButton;
		tile->setObjectName(QStringLiteral("levelLayout-") + id);
		tile->setProperty("shapeTile", true);
		tile->setCheckable(true);
		tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		tile->setIcon(studioIcon(QStringLiteral("layout-") + id));
		setBaseIconSize(tile, QSize(24, 24));
		tile->setText(label);
		tile->setToolTip(name);
		tile->setAccessibleName(name);
		tile->setAccessibleDescription(action->toolTip());
		tile->setFocusPolicy(Qt::TabFocus);
		tile->setMinimumWidth(48);
		tile->setChecked(action->isChecked());
		group->addButton(tile);
		connect(tile, &QToolButton::clicked, action, &QAction::trigger);
		connect(action, &QAction::toggled, tile, [tile](bool checked) {
			if (checked) {
				const QSignalBlocker blocker(tile);
				tile->setChecked(true);
			}
		});
		tiles->addTile(tile);
	}
	layout->addWidget(tiles);
	// One per line, as in Navigation: side by side they outgrow a narrow sidebar.
	layout->addSpacing(4);
	for (const auto& id : {QStringLiteral("map.maximizeView"), QStringLiteral("map.equalizeViews")}) {
		if (QAction* action = m_commands->action(id)) {
			layout->addWidget(actionButton(action, QStringLiteral("levelLayoutAction-") + id.section(QLatin1Char('.'), 1)));
		}
	}
	return panel;
}

QWidget* ApplicationShell::buildLevelNavigationOptions()
{
	auto* panel = new QWidget;
	auto* layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(4);
	if (!m_commands) {
		return panel;
	}
	for (const auto& id : {QStringLiteral("map.linkPlanCenters"), QStringLiteral("map.linkPlanZoom"), QStringLiteral("map.followCamera")}) {
		if (QAction* action = m_commands->action(id)) {
			layout->addWidget(actionCheckBox(action, QStringLiteral("levelNavigation-") + id.section(QLatin1Char('.'), 1), this));
		}
	}
	for (const auto& id : {QStringLiteral("map.centerPlansOnCamera"), QStringLiteral("map.manageViews")}) {
		if (QAction* action = m_commands->action(id)) {
			layout->addWidget(actionButton(action, QStringLiteral("levelNavigation-") + id.section(QLatin1Char('.'), 1)));
		}
	}
	return panel;
}

SidebarPage* ApplicationShell::levelSidebarPage(const QString& tabId) const
{
	return m_levelSidebarPages.value(tabId);
}

StudioSidebar* ApplicationShell::levelSidebarHolding(const QString& tabId) const
{
	for (StudioSidebar* sidebar : {m_levelLeadingSidebar, m_levelTrailingSidebar}) {
		if (sidebar && sidebar->indexOfPage(tabId) >= 0) {
			return sidebar;
		}
	}
	return nullptr;
}

bool ApplicationShell::showLevelSidebarPage(const QString& tabId, const QString& sectionId, bool focus)
{
	StudioSidebar* sidebar = levelSidebarHolding(tabId);
	SidebarPage* page = levelSidebarPage(tabId);
	if (!sidebar || !page) {
		return false;
	}
	sidebar->showPage(tabId);
	QWidget* target = page;
	if (!sectionId.isEmpty()) {
		if (SidebarSection* section = page->section(sectionId)) {
			section->setExpanded(true);
			target = section;
		}
	}
	if (focus) {
		// A filter field first, then the list it filters, then anything that
		// takes the keyboard.
		QWidget* chosen = nullptr;
		for (QLineEdit* field : target->findChildren<QLineEdit*>()) {
			if (field->isVisibleTo(page) && field->isEnabled() && field->focusPolicy() != Qt::NoFocus) {
				chosen = field;
				break;
			}
		}
		if (!chosen) {
			for (QAbstractItemView* view : target->findChildren<QAbstractItemView*>()) {
				if (view->isVisibleTo(page) && view->isEnabled()) {
					chosen = view;
					break;
				}
			}
		}
		if (chosen) {
			chosen->setFocus(Qt::ShortcutFocusReason);
		} else if (SidebarTabBar* bar = sidebar->sidebarTabBar()) {
			bar->setFocus(Qt::ShortcutFocusReason);
		}
	}
	return true;
}

void ApplicationShell::arrangeLevelSidebars(const LevelSidebarArrangement& requested)
{
	if (!m_levelLeadingSidebar || !m_levelTrailingSidebar) {
		return;
	}
	const LevelSidebarArrangement arrangement = normalizedLevelSidebarArrangement(requested, m_settings.selectedEditorProfileId());
	const QScopedValueRollback<bool> arranging(m_arrangingLevelSidebars, true);
	const QSignalBlocker leadingBlocker(m_levelLeadingSidebar);
	const QSignalBlocker trailingBlocker(m_levelTrailingSidebar);
	for (StudioSidebar* sidebar : {m_levelLeadingSidebar, m_levelTrailingSidebar}) {
		for (const QString& id : sidebar->pageIds()) {
			sidebar->takePage(id);
		}
	}
	const auto place = [this, &arrangement](StudioSidebar* sidebar, const QStringList& ids, const QString& current) {
		for (const QString& id : ids) {
			if (SidebarPage* page = levelSidebarPage(id)) {
				sidebar->addPage(page, sidebar->count() > 0 && arrangement.groupStarts.contains(id));
			}
		}
		const int index = sidebar->indexOfPage(current);
		sidebar->setCurrentIndex(index >= 0 ? index : 0);
		sidebar->setVisible(sidebar->count() > 0);
	};
	place(m_levelLeadingSidebar, arrangement.leading, arrangement.leadingCurrent);
	place(m_levelTrailingSidebar, arrangement.trailing, arrangement.trailingCurrent);
	refreshLevelSidebarTitles();
}

LevelSidebarArrangement ApplicationShell::currentLevelSidebarArrangement() const
{
	LevelSidebarArrangement arrangement;
	if (!m_levelLeadingSidebar || !m_levelTrailingSidebar) {
		return levelSidebarArrangementForProfile(m_settings.selectedEditorProfileId());
	}
	const auto read = [&arrangement](const StudioSidebar* sidebar, QStringList* ids) {
		for (int index = 0; index < sidebar->count(); ++index) {
			if (const SidebarPage* page = sidebar->pageAt(index)) {
				*ids << page->pageId();
				if (sidebar->sidebarTabBar()->startsGroup(index)) {
					arrangement.groupStarts << page->pageId();
				}
			}
		}
	};
	read(m_levelLeadingSidebar, &arrangement.leading);
	read(m_levelTrailingSidebar, &arrangement.trailing);
	arrangement.leadingCurrent = m_levelLeadingSidebar->currentPageId();
	arrangement.trailingCurrent = m_levelTrailingSidebar->currentPageId();
	return arrangement;
}

LevelSidebarArrangement ApplicationShell::savedLevelSidebarArrangement() const
{
	const QString profile = m_settings.selectedEditorProfileId();
	const QByteArray saved = m_settings.shellLayoutState(sidebarKey(QStringLiteral("arrangement/") + levelSidebarFamilyForProfile(profile)));
	LevelSidebarArrangement arrangement;
	if (!saved.isEmpty()) {
		const QJsonDocument document = QJsonDocument::fromJson(saved);
		// A saved arrangement from an older build may lack newer tabs;
		// normalization adds them where the profile puts them.
		if (document.isObject()) {
			LevelSidebarArrangement read;
			const QJsonObject object = document.object();
			const auto ids = [](const QJsonValue& value) {
				QStringList list;
				for (const QJsonValue& entry : value.toArray()) {
					list << (entry.isObject() ? entry.toObject().value(QStringLiteral("id")).toString() : entry.toString());
				}
				return list;
			};
			read.leading = ids(object.value(QStringLiteral("leading")));
			read.trailing = ids(object.value(QStringLiteral("trailing")));
			for (const QString& side : {QStringLiteral("leading"), QStringLiteral("trailing")}) {
				for (const QJsonValue& entry : object.value(side).toArray()) {
					if (entry.isObject() && entry.toObject().value(QStringLiteral("startsGroup")).toBool()) {
						read.groupStarts << entry.toObject().value(QStringLiteral("id")).toString();
					}
				}
			}
			read.leadingCurrent = object.value(QStringLiteral("leadingCurrent")).toString();
			read.trailingCurrent = object.value(QStringLiteral("trailingCurrent")).toString();
			if (!read.leading.isEmpty() || !read.trailing.isEmpty()) {
				return normalizedLevelSidebarArrangement(read, profile);
			}
		}
	}
	arrangement = levelSidebarArrangementForProfile(profile);
	return arrangement;
}

void ApplicationShell::applyLevelSidebarProfile()
{
	if (!m_levelLeadingSidebar) {
		return;
	}
	const QString family = levelSidebarFamilyForProfile(m_settings.selectedEditorProfileId());
	if (family != m_levelSidebarFamily) {
		m_levelSidebarFamily = family;
		arrangeLevelSidebars(savedLevelSidebarArrangement());
	} else {
		refreshLevelSidebarTitles();
	}
}

void ApplicationShell::refreshLevelSidebarTitles()
{
	const QString profile = m_settings.selectedEditorProfileId();
	const bool doom = m_levelMapDocument.format == LevelMapFormat::DoomWad;
	for (auto it = m_levelSidebarPages.cbegin(); it != m_levelSidebarPages.cend(); ++it) {
		it.value()->setTitle(levelSidebarTabTitle(it.key(), profile, doom));
		if (QAction* action = m_commands ? m_commands->action(QStringLiteral("map.sidebar.") + it.key()) : nullptr) {
			it.value()->setShortcutText(action->shortcut().toString(QKeySequence::NativeText));
		}
	}
}

void ApplicationShell::saveLevelSidebarState()
{
	if (m_arrangingLevelSidebars || m_restoringLevelSidebars || !m_levelLeadingSidebar || !m_levelTrailingSidebar) {
		return;
	}
	const QString profile = m_settings.selectedEditorProfileId();
	m_settings.setShellLayoutState(sidebarKey(QStringLiteral("arrangement/") + levelSidebarFamilyForProfile(profile)),
		QJsonDocument(levelSidebarArrangementJson(currentLevelSidebarArrangement(), profile)).toJson(QJsonDocument::Compact));
	const auto side = [this](const StudioSidebar* sidebar, const QString& name) {
		m_settings.setShellLayoutState(sidebarKey(name + QStringLiteral("/folded")), onOff(sidebar->isFolded()));
		if (sidebar->expandedWidth() > 0) {
			m_settings.setShellLayoutState(sidebarKey(name + QStringLiteral("/width")), QByteArray::number(sidebar->expandedWidth()));
		}
	};
	side(m_levelLeadingSidebar, QStringLiteral("leading"));
	side(m_levelTrailingSidebar, QStringLiteral("trailing"));
}

void ApplicationShell::restoreLevelSidebarState()
{
	if (!m_levelLeadingSidebar || !m_levelTrailingSidebar) {
		return;
	}
	const QScopedValueRollback<bool> restoring(m_restoringLevelSidebars, true);
	auto* splitter = qobject_cast<QSplitter*>(m_levelLeadingSidebar->parentWidget());
	for (auto [sidebar, name] : {std::pair {m_levelLeadingSidebar, QStringLiteral("leading")}, std::pair {m_levelTrailingSidebar, QStringLiteral("trailing")}}) {
		bool ok = false;
		const int width = m_settings.shellLayoutState(sidebarKey(name + QStringLiteral("/width"))).toInt(&ok);
		if (ok && width > sidebar->foldedWidth()) {
			sidebar->setExpandedWidth(width);
		}
		const bool folded = m_settings.shellLayoutState(sidebarKey(name + QStringLiteral("/folded"))) == QByteArrayLiteral("on");
		if (folded) {
			sidebar->setFolded(true);
			continue;
		}
		// A splitter state saved while the sidebar was folded leaves it as
		// narrow as its tab column; give it its width back.
		if (splitter && sidebar->expandedWidth() > 0) {
			QList<int> sizes = splitter->sizes();
			const int self = splitter->indexOf(sidebar);
			if (self >= 0 && self < sizes.size() && sizes.at(self) < sidebar->foldedWidth() + 40) {
				const int centre = splitter->indexOf(m_levelLeadingSidebar) == 0 ? 1 : 0;
				sizes[centre] = std::max(0, sizes.at(centre) - (sidebar->expandedWidth() - sizes.at(self)));
				sizes[self] = sidebar->expandedWidth();
				splitter->setSizes(sizes);
			}
		}
	}
}

void ApplicationShell::showLevelSidebarTabMenu(StudioSidebar* sidebar, const QString& tabId, const QPoint& globalPosition)
{
	if (!sidebar) {
		return;
	}
	QMenu menu(this);
	menu.setObjectName(QStringLiteral("levelSidebarTabMenu"));
	const bool leading = sidebar == m_levelLeadingSidebar;
	const bool mirrored = layoutDirection() == Qt::RightToLeft;
	// Physical sides read better than reading-order ones here: the leading
	// sidebar is on the left left to right.
	const bool toRight = leading != mirrored;
	QAction* move = nullptr;
	if (!tabId.isEmpty()) {
		SidebarPage* page = levelSidebarPage(tabId);
		const QString title = page ? page->title() : tabId;
		move = menu.addAction(studioIcon(toRight ? QStringLiteral("sidebar-right") : QStringLiteral("sidebar-left")),
			toRight ? tr("Move %1 to the Right Sidebar").arg(title) : tr("Move %1 to the Left Sidebar").arg(title));
		move->setObjectName(QStringLiteral("levelSidebarMoveTab"));
	}
	QAction* fold = menu.addAction(sidebar->isFolded() ? tr("Open Sidebar") : tr("Fold Sidebar"));
	fold->setObjectName(QStringLiteral("levelSidebarFold"));
	QAction* captions = menu.addAction(tr("Show Tab Captions"));
	captions->setObjectName(QStringLiteral("levelSidebarCaptions"));
	captions->setCheckable(true);
	captions->setChecked(sidebar->showsLabels());
	menu.addSeparator();
	QAction* reset = menu.addAction(studioIcon(QStringLiteral("refresh")), tr("Reset Sidebars"));
	reset->setObjectName(QStringLiteral("levelSidebarReset"));
	QAction* chosen = menu.exec(globalPosition);
	if (!chosen) {
		return;
	}
	if (chosen == move) {
		moveLevelSidebarPage(tabId);
	} else if (chosen == fold) {
		sidebar->setFolded(!sidebar->isFolded());
	} else if (chosen == captions) {
		setLevelSidebarCaptions(captions->isChecked());
	} else if (chosen == reset) {
		resetLevelSidebars();
	}
}

void ApplicationShell::moveLevelSidebarPage(const QString& tabId)
{
	LevelSidebarArrangement arrangement = currentLevelSidebarArrangement();
	StudioSidebar* target = nullptr;
	if (arrangement.leading.removeAll(tabId) > 0) {
		arrangement.trailing << tabId;
		arrangement.trailingCurrent = tabId;
		target = m_levelTrailingSidebar;
	} else if (arrangement.trailing.removeAll(tabId) > 0) {
		arrangement.leading << tabId;
		arrangement.leadingCurrent = tabId;
		target = m_levelLeadingSidebar;
	}
	if (!target) {
		return;
	}
	arrangement.groupStarts.removeAll(tabId);
	arrangeLevelSidebars(arrangement);
	target->showPage(tabId);
	saveLevelSidebarState();
	if (SidebarPage* page = levelSidebarPage(tabId)) {
		statusBar()->showMessage(tr("%1 moved to the other sidebar. Reset Sidebars on its tab menu puts every tab back.").arg(page->title()), 5000);
	}
}

void ApplicationShell::resetLevelSidebars()
{
	if (!m_levelLeadingSidebar || !m_levelTrailingSidebar) {
		return;
	}
	const QString profile = m_settings.selectedEditorProfileId();
	m_settings.setShellLayoutState(sidebarKey(QStringLiteral("arrangement/") + levelSidebarFamilyForProfile(profile)), QByteArray());
	arrangeLevelSidebars(levelSidebarArrangementForProfile(profile));
	m_levelLeadingSidebar->setFolded(false);
	m_levelTrailingSidebar->setFolded(false);
	setLevelSidebarCaptions(false);
	saveLevelSidebarState();
	statusBar()->showMessage(tr("Sidebars reset to the %1 arrangement.").arg(editorProfileDisplayNameForId(profile)), 4000);
}

void ApplicationShell::setLevelSidebarCaptions(bool show)
{
	for (StudioSidebar* sidebar : {m_levelLeadingSidebar, m_levelTrailingSidebar}) {
		if (sidebar) {
			sidebar->setShowLabels(show);
		}
	}
	m_settings.setShellLayoutState(sidebarKey(QStringLiteral("captions")), onOff(show));
	if (m_commands) {
		m_commands->setChecked(QStringLiteral("map.sidebar.captions"), show);
	}
}

void ApplicationShell::toggleLevelSidebar(bool leading)
{
	StudioSidebar* sidebar = leading ? m_levelLeadingSidebar : m_levelTrailingSidebar;
	if (sidebar && sidebar->count() > 0) {
		sidebar->setFolded(!sidebar->isFolded());
	}
}

void ApplicationShell::registerLevelSidebarCommands()
{
	const QString section = tr("Level Sidebars");
	for (const LevelSidebarTab& tab : levelSidebarTabs()) {
		StudioCommandRegistration command;
		command.commandId = tab.commandId;
		command.group = StudioCommandGroup::View;
		command.menuSection = section;
		command.label = tr("Show %1").arg(tab.title);
		command.statusTip = tab.description;
		command.iconName = tab.iconName;
		command.handler = [this, id = tab.id]() {
			setMode(StudioMode::Levels);
			showLevelSidebarPage(id, QString(), true);
		};
		m_commands->registerCommand(command);
	}
	const auto add = [this, &section](const QString& id, const QString& label, const QString& tip, const QString& icon, std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::View;
		command.menuSection = section;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.handler = std::move(handler);
		return m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.sidebar.toggleLeading"), tr("Fold or Open the Browser Sidebar"),
		tr("Fold the sidebar of browsers down to its tabs, or open it again."), QStringLiteral("sidebar-left"), [this]() {
			setMode(StudioMode::Levels);
			toggleLevelSidebar(true);
		});
	add(QStringLiteral("map.sidebar.toggleTrailing"), tr("Fold or Open the Properties Sidebar"),
		tr("Fold the sidebar of properties down to its tabs, or open it again."), QStringLiteral("sidebar-right"), [this]() {
			setMode(StudioMode::Levels);
			toggleLevelSidebar(false);
		});
	if (QAction* captions = add(QStringLiteral("map.sidebar.captions"), tr("Sidebar Tab Captions"),
			tr("Name each sidebar tab under its glyph."), QString(), [this]() {
				const bool show = !(m_levelLeadingSidebar && m_levelLeadingSidebar->showsLabels());
				setLevelSidebarCaptions(show);
			})) {
		captions->setCheckable(true);
		captions->setChecked(m_settings.shellLayoutState(sidebarKey(QStringLiteral("captions"))) == QByteArrayLiteral("on"));
	}
	add(QStringLiteral("map.sidebar.reset"), tr("Reset Sidebars"), tr("Put every Levels sidebar tab back where the editor profile places it."),
		QStringLiteral("refresh"), [this]() { resetLevelSidebars(); });
}

void ApplicationShell::refreshLevelEntityClassInfo()
{
	if (!m_levelEntityClassInfo || !m_levelMapPalette) {
		return;
	}
	const QTreeWidgetItem* item = m_levelMapPalette->currentItem();
	const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
	const bool placeable = !payload.isEmpty();
	if (m_levelPlaceInView) {
		m_levelPlaceInView->setEnabled(placeable && m_levelMapViewport && m_levelMapDocument.format != LevelMapFormat::Unknown);
	}
	if (!placeable) {
		m_levelEntityClassInfo->setText(m_levelMapDocument.format == LevelMapFormat::Unknown
			? tr("Open a map to see what it can place.")
			: tr("Choose a class above to read about it. Drag it onto a view to place it there."));
		return;
	}
	if (payload.startsWith(QStringLiteral("thing:"))) {
		const int type = payload.mid(6).toInt();
		for (const LevelMapDoomThingType& thing : levelMapDoomThingTypes(m_levelMapDocument.doomFormat)) {
			if (thing.type == type) {
				m_levelEntityClassInfo->setText(tr("%1\nDoomEd number %2, filed under %3.").arg(thing.name).arg(thing.type).arg(thing.category));
				return;
			}
		}
		m_levelEntityClassInfo->setText(tr("DoomEd number %1.").arg(type));
		return;
	}
	const QString className = payload.section(QLatin1Char(':'), 1);
	EntityClassDefinition definition;
	if (!m_entityDefinitions.classForName(className, &definition)) {
		m_levelEntityClassInfo->setText(tr("%1\nNo definition is loaded for this class, so its keys are not known. Load entity definitions on the Map tab.")
				.arg(className));
		return;
	}
	QStringList lines;
	lines << definition.className;
	if (!definition.description.trimmed().isEmpty()) {
		lines << definition.description.simplified();
	}
	if (definition.hasSize) {
		lines << tr("Size %1 × %2 × %3")
					 .arg(definition.maxs[0] - definition.mins[0])
					 .arg(definition.maxs[1] - definition.mins[1])
					 .arg(definition.maxs[2] - definition.mins[2]);
	}
	if (!definition.keys.isEmpty()) {
		QStringList keys;
		for (const EntityKeyDefinition& key : definition.keys) {
			keys << key.key;
		}
		lines << tr("Keys: %1").arg(keys.join(QStringLiteral(", ")));
	}
	if (!definition.spawnflags.isEmpty()) {
		QStringList flags;
		for (const EntitySpawnflagDefinition& flag : definition.spawnflags) {
			flags << flag.name;
		}
		lines << tr("Flags: %1").arg(flags.join(QStringLiteral(", ")));
	}
	if (!definition.modelHint.isEmpty()) {
		lines << tr("Model: %1").arg(definition.modelHint);
	}
	m_levelEntityClassInfo->setText(lines.join(QLatin1Char('\n')));
}

QStringList ApplicationShell::levelPackageTextureNames()
{
	const PackageArchive& archive = packageViewArchive();
	const QString key = packageViewKey() + (archive.isOpen() ? QStringLiteral("|open") : QStringLiteral("|closed"));
	if (key == m_levelPackageTextureKey) {
		return m_levelPackageTextureNames;
	}
	m_levelPackageTextureKey = key;
	m_levelPackageTextureNames.clear();
	if (!archive.isOpen()) {
		return m_levelPackageTextureNames;
	}
	QSet<QString> seen;
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		QString name;
		const QString path = entry.virtualPath;
		if (entry.typeHint == QLatin1String("wad-texture")) {
			// A WAD2 or WAD3 lump is named by itself in a map.
			name = QFileInfo(path).completeBaseName();
		} else if (path.startsWith(QStringLiteral("textures/"), Qt::CaseInsensitive)
			&& assetPreviewKindForEntry(path, entry.typeHint) == AssetPreviewKind::Image) {
			// Quake II and Quake III name textures below textures/, without
			// their extension.
			name = path.mid(9);
			const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
			if (dot > name.lastIndexOf(QLatin1Char('/'))) {
				name.truncate(dot);
			}
		}
		if (!name.isEmpty() && !seen.contains(name.toCaseFolded())) {
			seen.insert(name.toCaseFolded());
			m_levelPackageTextureNames << name;
		}
	}
	std::sort(m_levelPackageTextureNames.begin(), m_levelPackageTextureNames.end(),
		[](const QString& left, const QString& right) { return left.compare(right, Qt::CaseInsensitive) < 0; });
	return m_levelPackageTextureNames;
}

void ApplicationShell::setLevelTextureTileSize(int pixels)
{
	if (!m_levelMapTextures) {
		return;
	}
	setBaseIconSize(m_levelMapTextures, QSize(pixels, pixels));
	m_settings.setShellLayoutState(QStringLiteral("levelTextures/tile"), QByteArray::number(pixels));
	refreshLevelMapTextures();
}

void ApplicationShell::placeLevelPaletteInView()
{
	const QTreeWidgetItem* item = m_levelMapPalette ? m_levelMapPalette->currentItem() : nullptr;
	const QString payload = item ? item->data(0, Qt::UserRole).toString() : QString();
	if (payload.isEmpty()) {
		statusBar()->showMessage(tr("Choose a class in Entities before placing it."));
		return;
	}
	placeFromLevelMapPalette(payload, QPointF(-1.0, -1.0));
}

} // namespace vibestudio
