// The Levels Shapes tab: brush shapes to draw, add or turn the selected
// brushes into, after the object tools of Hammer, J.A.C.K. and Sledge
// (block, wedge, cylinder, spike, sphere, arch, torus) and Radiant's
// arbitrary-sided brush commands, which turn the selected brush into a
// shape filling its bounds. Doom maps get sector shapes instead, as Doom
// Builder's rectangle and ellipse drawing modes make.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_icons.h"
#include "app/studio_layout.h"
#include "app/studio_sidebar.h"
#include "app/tile_grid.h"
#include "core/level_doom_shapes.h"
#include "core/level_shapes.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio {

namespace {

// Doom's sector shapes are traced through Draw Sector's service, not built
// as brushes, so they live here rather than in core/level_shapes.
constexpr QLatin1StringView kSectorRectangle("sector-rectangle");
constexpr QLatin1StringView kSectorPolygon("sector-polygon");
constexpr QLatin1StringView kSectorStairs("sector-stairs");
constexpr QLatin1StringView kSectorGrid("sector-grid");

bool isSectorShape(const QString& shape)
{
	return shape == kSectorRectangle || shape == kSectorPolygon || shape == kSectorStairs || shape == kSectorGrid;
}

QLabel* shapeHint(const QString& name)
{
	auto* label = new QLabel;
	label->setObjectName(name);
	label->setProperty("sidebarHint", true);
	label->setWordWrap(true);
	label->setTextFormat(Qt::PlainText);
	return label;
}

QSpinBox* shapeSpin(const QString& name, int minimum, int maximum, int value, const QString& accessibleName, const QString& tip,
	const QString& suffix = QString())
{
	auto* spin = new QSpinBox;
	spin->setObjectName(name);
	spin->setRange(minimum, maximum);
	spin->setValue(value);
	spin->setSuffix(suffix);
	spin->setAccessibleName(accessibleName);
	spin->setToolTip(tip);
	spin->setKeyboardTracking(false);
	return spin;
}

QLabel* fieldLabel(const QString& text, QWidget* buddy)
{
	auto* label = new QLabel(text);
	label->setObjectName(QStringLiteral("sidebarFieldLabel"));
	label->setBuddy(buddy);
	return label;
}

} // namespace

void ApplicationShell::buildLevelShapesPage(SidebarPage* page)
{
	// The shapes as tiles, one chosen at a time: the brush shapes for Quake
	// maps and, in their place on a Doom map, the sector shapes.
	auto* tiles = new QWidget;
	tiles->setObjectName(QStringLiteral("levelShapeTiles"));
	auto* column = new QVBoxLayout(tiles);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(4);
	// Only one set shows at a time, and hidden tiles leave no gaps.
	auto* grid = new TileGrid(3);
	grid->setObjectName(QStringLiteral("levelShapeGrid"));
	column->addWidget(grid);
	m_levelShapeButtons = new QButtonGroup(tiles);
	m_levelShapeButtons->setExclusive(true);
	struct Tile {
		QString id;
		QString icon;
		QString label;
		QString description;
	};
	QVector<Tile> entries;
	for (const QString& id : levelShapeIds()) {
		entries.push_back({id, levelShapeIconName(id), levelShapeLabel(id), levelShapeDescription(id)});
	}
	entries.push_back({QString(kSectorRectangle), QStringLiteral("select-box"), tr("Rectangle"), tr("A four-sided sector as wide and deep as you set.")});
	entries.push_back({QString(kSectorPolygon), QStringLiteral("polygon"), tr("Polygon"),
		tr("A sector with equal sides around an ellipse as wide and deep as you set, for round rooms and pillars.")});
	entries.push_back({QString(kSectorStairs), QStringLiteral("shape-stairs"), tr("Stairs"),
		tr("A row of step sectors, each floor rising above the last, as Doom Builder's stair builder makes.")});
	entries.push_back({QString(kSectorGrid), QStringLiteral("grid"), tr("Grid"),
		tr("The footprint cut into columns and rows of sectors, as Doom Builder's grid drawing makes.")});
	for (int index = 0; index < entries.size(); ++index) {
		const Tile& entry = entries.at(index);
		auto* tile = new QToolButton;
		tile->setObjectName(QStringLiteral("levelShape-") + entry.id);
		tile->setProperty("shapeTile", true);
		tile->setProperty("shapeId", entry.id);
		tile->setProperty("shapeIcon", entry.icon);
		tile->setCheckable(true);
		tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		tile->setIcon(studioIcon(entry.icon));
		setBaseIconSize(tile, QSize(24, 24));
		tile->setText(entry.label);
		tile->setToolTip(entry.description);
		tile->setAccessibleName(entry.label);
		tile->setAccessibleDescription(entry.description);
		tile->setMinimumWidth(48);
		m_levelShapeButtons->addButton(tile);
		grid->addTile(tile);
		connect(tile, &QToolButton::clicked, this, [this, id = entry.id]() { chooseLevelShape(id); });
	}
	m_levelShapeAbout = shapeHint(QStringLiteral("levelShapeAbout"));
	column->addWidget(m_levelShapeAbout);
	addLevelSidebarSection(page, QStringLiteral("shapes.shape"), tr("Shape"), tiles);

	// The settings the chosen shape reads; the others are hidden.
	auto* settings = new QWidget;
	settings->setObjectName(QStringLiteral("levelShapeSettings"));
	m_levelShapeForm = new QFormLayout(settings);
	m_levelShapeForm->setContentsMargins(0, 0, 0, 0);
	m_levelShapeForm->setHorizontalSpacing(8);
	m_levelShapeForm->setVerticalSpacing(6);
	m_levelShapeForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_levelShapeForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_levelShapeAxis = new QComboBox;
	m_levelShapeAxis->setObjectName(QStringLiteral("levelShapeAxis"));
	m_levelShapeAxis->addItem(tr("Across the View"), -1);
	m_levelShapeAxis->addItem(tr("X"), 0);
	m_levelShapeAxis->addItem(tr("Y"), 1);
	m_levelShapeAxis->addItem(tr("Z"), 2);
	m_levelShapeAxis->setAccessibleName(tr("Shape axis"));
	m_levelShapeAxis->setToolTip(tr("The axis a shape's length, poles or curve turn about. Across the View follows the view you draw or add it in: "
									"Z in Top, Y in Front and X in Side, so an arch drawn in Front stands up as a doorway."));
	m_levelShapeSides = shapeSpin(QStringLiteral("levelShapeSides"), 3, 64, 8, tr("Sides"),
		tr("How many sides round shapes have, or how many segments an arch or ring is built of."));
	m_levelShapeBands = shapeSpin(QStringLiteral("levelShapeBands"), 2, 16, 4, tr("Sphere bands"),
		tr("A sphere's latitude bands; sides times bands is at most 128."));
	m_levelShapeThickness = shapeSpin(QStringLiteral("levelShapeThickness"), 1, 4096, 16, tr("Wall thickness"),
		tr("How thick the walls of arches, rings and rooms are, in map units. Walls as thick as the radius close an arch into slices."));
	m_levelShapeArc = shapeSpin(QStringLiteral("levelShapeArc"), 1, 360, 180, tr("Sweep"),
		tr("How far the arch turns, in degrees: 180 is a half circle and 360 a whole ring."), QStringLiteral("°"));
	m_levelShapeStart = shapeSpin(QStringLiteral("levelShapeStart"), -360, 360, 0, tr("Start angle"),
		tr("Where the arch starts, in degrees from the view's right towards its top."), QStringLiteral("°"));
	m_levelShapeSteps = shapeSpin(QStringLiteral("levelShapeSteps"), 2, 64, 8, tr("Steps"), tr("How many steps the stairs have; each is one solid brush."));
	m_levelShapeRise = new QComboBox;
	m_levelShapeRise->setObjectName(QStringLiteral("levelShapeRise"));
	m_levelShapeRise->addItem(tr("Along the Longer Side"), QStringLiteral("auto"));
	m_levelShapeRise->addItem(tr("Up Towards +X"), QStringLiteral("+x"));
	m_levelShapeRise->addItem(tr("Up Towards −X"), QStringLiteral("-x"));
	m_levelShapeRise->addItem(tr("Up Towards +Y"), QStringLiteral("+y"));
	m_levelShapeRise->addItem(tr("Up Towards −Y"), QStringLiteral("-y"));
	m_levelShapeRise->setAccessibleName(tr("Stairs climb"));
	m_levelShapeRise->setToolTip(tr("Which way the stairs climb. They always rise along Z, from the bottom of the box to its top."));
	m_levelShapeWidth = shapeSpin(QStringLiteral("levelShapeWidth"), 8, 16384, 256, tr("Width"), tr("How wide the sector is along X, in map units."));
	m_levelShapeDepth = shapeSpin(QStringLiteral("levelShapeDepth"), 8, 16384, 256, tr("Depth"), tr("How deep the sector is along Y, in map units."));
	m_levelShapeStepHeight = shapeSpin(QStringLiteral("levelShapeStepHeight"), -1024, 1024, 8, tr("Step height"),
		tr("How far each step's floor rises above the last; a negative height makes the stairs go down."));
	m_levelShapeColumns = shapeSpin(QStringLiteral("levelShapeColumns"), 1, 32, 4, tr("Columns"), tr("How many sectors the grid has across."));
	m_levelShapeRows = shapeSpin(QStringLiteral("levelShapeRows"), 1, 32, 4, tr("Rows"), tr("How many sectors the grid has down."));
	m_levelShapeForm->addRow(fieldLabel(tr("Axis"), m_levelShapeAxis), m_levelShapeAxis);
	m_levelShapeForm->addRow(fieldLabel(tr("Sides"), m_levelShapeSides), m_levelShapeSides);
	m_levelShapeForm->addRow(fieldLabel(tr("Bands"), m_levelShapeBands), m_levelShapeBands);
	m_levelShapeForm->addRow(fieldLabel(tr("Walls"), m_levelShapeThickness), m_levelShapeThickness);
	m_levelShapeForm->addRow(fieldLabel(tr("Sweep"), m_levelShapeArc), m_levelShapeArc);
	m_levelShapeForm->addRow(fieldLabel(tr("Start"), m_levelShapeStart), m_levelShapeStart);
	m_levelShapeForm->addRow(fieldLabel(tr("Steps"), m_levelShapeSteps), m_levelShapeSteps);
	m_levelShapeForm->addRow(fieldLabel(tr("Climbs"), m_levelShapeRise), m_levelShapeRise);
	m_levelShapeForm->addRow(fieldLabel(tr("Width"), m_levelShapeWidth), m_levelShapeWidth);
	m_levelShapeForm->addRow(fieldLabel(tr("Depth"), m_levelShapeDepth), m_levelShapeDepth);
	m_levelShapeForm->addRow(fieldLabel(tr("Step height"), m_levelShapeStepHeight), m_levelShapeStepHeight);
	m_levelShapeForm->addRow(fieldLabel(tr("Columns"), m_levelShapeColumns), m_levelShapeColumns);
	m_levelShapeForm->addRow(fieldLabel(tr("Rows"), m_levelShapeRows), m_levelShapeRows);
	for (QSpinBox* spin : {m_levelShapeSides, m_levelShapeBands, m_levelShapeThickness, m_levelShapeArc, m_levelShapeStart, m_levelShapeSteps,
			 m_levelShapeWidth, m_levelShapeDepth, m_levelShapeStepHeight, m_levelShapeColumns, m_levelShapeRows}) {
		connect(spin, &QSpinBox::valueChanged, this, [this]() { saveLevelShapeSettings(); });
	}
	for (QComboBox* combo : {m_levelShapeAxis, m_levelShapeRise}) {
		connect(combo, &QComboBox::currentIndexChanged, this, [this]() { saveLevelShapeSettings(); });
	}
	m_levelShapeSettingsSection = addLevelSidebarSection(page, QStringLiteral("shapes.settings"), tr("Settings"), settings);

	// Making one: by dragging, at the view's centre, or from the selection.
	auto* create = new QWidget;
	create->setObjectName(QStringLiteral("levelShapeCreate"));
	auto* createLayout = new QVBoxLayout(create);
	createLayout->setContentsMargins(0, 0, 0, 0);
	createLayout->setSpacing(6);
	m_levelShapeDraw = createButton(tr("Draw in a View"), QStringLiteral("cube"));
	m_levelShapeDraw->setObjectName(QStringLiteral("levelShapeDraw"));
	m_levelShapeDraw->setAccessibleName(tr("Draw the chosen shape in a view"));
	m_levelShapeAdd = createButton(tr("Add at View Centre"), QStringLiteral("crosshair"));
	m_levelShapeAdd->setObjectName(QStringLiteral("levelShapeAdd"));
	m_levelShapeAdd->setAccessibleName(tr("Add the chosen shape in the middle of the active view"));
	m_levelShapeReplace = createButton(tr("Replace Selected Brushes"), QStringLiteral("refresh"));
	m_levelShapeReplace->setObjectName(QStringLiteral("levelShapeReplace"));
	m_levelShapeReplace->setAccessibleName(tr("Replace the selected brushes with the chosen shape"));
	m_levelShapeReplace->setToolTip(tr("Replace the selected world brushes with the chosen shape, filling their bounds, as one undo step."));
	m_levelShapeNote = shapeHint(QStringLiteral("levelShapeNote"));
	createLayout->addWidget(m_levelShapeDraw);
	createLayout->addWidget(m_levelShapeAdd);
	createLayout->addWidget(m_levelShapeReplace);
	createLayout->addWidget(m_levelShapeNote);
	connect(m_levelShapeDraw, &QPushButton::clicked, this, [this]() {
		const bool doom = levelMapDoomEditable();
		chooseLevelTool(doom ? QStringLiteral("shape") : QStringLiteral("brush"));
		QString name;
		for (QAbstractButton* button : m_levelShapeButtons->buttons()) {
			if (button->property("shapeId").toString() == (doom ? m_levelSectorShape : m_levelShape)) {
				name = button->text();
			}
		}
		statusBar()->showMessage(doom ? tr("Drag a box in the Top view and the %1 fills it; Escape stops drawing.").arg(name)
									  : tr("Drag a box in a 2D view, or in the camera, and the %1 fills it.").arg(levelShapeLabel(m_levelShape)),
			6000);
	});
	connect(m_levelShapeAdd, &QPushButton::clicked, this, [this]() {
		if (levelMapDoomEditable()) {
			addLevelSectorShapeAtViewCentre();
		} else {
			addLevelShapeAtViewCentre();
		}
	});
	connect(m_levelShapeReplace, &QPushButton::clicked, this, &ApplicationShell::replaceLevelSelectionWithShape);
	addLevelSidebarSection(page, QStringLiteral("shapes.create"), tr("Create"), create);

	// The last shape and settings come back.
	const QScopedValueRollback<bool> restoring(m_restoringLevelShape, true);
	const QJsonObject saved = QJsonDocument::fromJson(m_settings.shellLayoutState(QStringLiteral("levelShapes/settings"))).object();
	const auto restoreSpin = [&saved](QSpinBox* spin, const char* key) {
		if (saved.contains(QLatin1String(key))) {
			spin->setValue(saved.value(QLatin1String(key)).toInt(spin->value()));
		}
	};
	restoreSpin(m_levelShapeSides, "sides");
	restoreSpin(m_levelShapeBands, "bands");
	restoreSpin(m_levelShapeThickness, "thickness");
	restoreSpin(m_levelShapeArc, "arc");
	restoreSpin(m_levelShapeStart, "start");
	restoreSpin(m_levelShapeSteps, "steps");
	restoreSpin(m_levelShapeWidth, "width");
	restoreSpin(m_levelShapeDepth, "depth");
	restoreSpin(m_levelShapeStepHeight, "stepHeight");
	restoreSpin(m_levelShapeColumns, "columns");
	restoreSpin(m_levelShapeRows, "rows");
	if (const int axis = m_levelShapeAxis->findData(saved.value(QStringLiteral("axis")).toInt(-1)); axis >= 0) {
		m_levelShapeAxis->setCurrentIndex(axis);
	}
	if (const int rise = m_levelShapeRise->findData(saved.value(QStringLiteral("rise")).toString()); rise >= 0) {
		m_levelShapeRise->setCurrentIndex(rise);
	}
	const QString sectorShape = saved.value(QStringLiteral("sectorShape")).toString();
	m_levelSectorShape = isSectorShape(sectorShape) ? sectorShape : QString(kSectorRectangle);
	chooseLevelShape(saved.value(QStringLiteral("shape")).toString(QStringLiteral("box")));
}

void ApplicationShell::chooseLevelShape(const QString& shape)
{
	if (isSectorShape(shape)) {
		m_levelSectorShape = shape;
	} else {
		m_levelShape = isLevelShape(shape) ? shape : QStringLiteral("box");
	}
	if (!m_restoringLevelShape) {
		saveLevelShapeSettings();
	}
	refreshLevelShapesPanel();
	refreshLevelToolShelf();
}

void ApplicationShell::refreshLevelShapesPanel()
{
	if (!m_levelShapeButtons || !m_levelShapeForm) {
		return;
	}
	const bool hasMap = m_levelMapDocument.format != LevelMapFormat::Unknown;
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool doomMap = m_levelMapDocument.format == LevelMapFormat::DoomWad;
	const bool doom = doomMap && levelMapDoomEditable();
	// A Doom map shows its sector shapes; anything else the brush shapes.
	const QString current = doomMap ? m_levelSectorShape : m_levelShape;
	for (QAbstractButton* button : m_levelShapeButtons->buttons()) {
		const QString id = button->property("shapeId").toString();
		const QSignalBlocker blocker(button);
		button->setVisible(isSectorShape(id) == doomMap);
		button->setEnabled(quake || doom || !hasMap);
		button->setChecked(id == current);
	}
	const bool sector = isSectorShape(current);
	QStringList parameters = levelShapeParameters(current);
	if (current == kSectorPolygon) {
		parameters = {QStringLiteral("width"), QStringLiteral("depth"), QStringLiteral("sides")};
	} else if (current == kSectorStairs) {
		parameters = {QStringLiteral("width"), QStringLiteral("depth"), QStringLiteral("steps"), QStringLiteral("stepHeight"), QStringLiteral("rise")};
	} else if (current == kSectorGrid) {
		parameters = {QStringLiteral("width"), QStringLiteral("depth"), QStringLiteral("columns"), QStringLiteral("rows")};
	} else if (sector) {
		parameters = {QStringLiteral("width"), QStringLiteral("depth")};
	}
	const auto show = [this, &parameters](QWidget* field, const char* key) {
		m_levelShapeForm->setRowVisible(field, parameters.contains(QLatin1String(key)));
	};
	show(m_levelShapeAxis, "axis");
	show(m_levelShapeSides, "sides");
	show(m_levelShapeBands, "bands");
	show(m_levelShapeThickness, "thickness");
	show(m_levelShapeArc, "arc");
	show(m_levelShapeStart, "start");
	show(m_levelShapeSteps, "steps");
	show(m_levelShapeRise, "rise");
	show(m_levelShapeWidth, "width");
	show(m_levelShapeDepth, "depth");
	show(m_levelShapeStepHeight, "stepHeight");
	show(m_levelShapeColumns, "columns");
	show(m_levelShapeRows, "rows");
	// Round shapes and polygons have sides; arches and rings are built of
	// segments.
	const bool segments = current == QLatin1String("arch") || current == QLatin1String("ring");
	if (auto* label = qobject_cast<QLabel*>(m_levelShapeForm->labelForField(m_levelShapeSides))) {
		label->setText(segments ? tr("Segments") : tr("Sides"));
	}
	m_levelShapeSides->setAccessibleName(segments ? tr("Segments") : tr("Sides"));
	{
		const QSignalBlocker blocker(m_levelShapeSides);
		m_levelShapeSides->setMinimum(current == QLatin1String("arch") ? 1 : 3);
	}
	if (m_levelShapeSettingsSection) {
		m_levelShapeSettingsSection->setVisible(!parameters.isEmpty());
	}
	QString about;
	for (QAbstractButton* button : m_levelShapeButtons->buttons()) {
		if (button->property("shapeId").toString() == current) {
			about = button->toolTip();
		}
	}
	m_levelShapeAbout->setText(about);
	QString icon = levelShapeIconName(current);
	for (QAbstractButton* button : m_levelShapeButtons->buttons()) {
		if (sector && button->property("shapeId").toString() == current) {
			icon = button->property("shapeIcon").toString();
		}
	}
	m_levelShapeDraw->setIcon(studioIcon(icon));
	m_levelShapeDraw->setText(tr("Draw in a View"));

	bool brushes = false;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		brushes = brushes || ref.kind == LevelMapSelectionKind::QuakeBrush;
	}
	m_levelShapeDraw->setEnabled(quake || doom);
	m_levelShapeAdd->setEnabled(quake || doom);
	m_levelShapeReplace->setVisible(!doomMap);
	m_levelShapeReplace->setEnabled(quake && brushes);
	if (!hasMap) {
		m_levelShapeNote->setText(tr("Open a map to add shapes to it."));
	} else if (quake) {
		m_levelShapeNote->setText(tr("New brushes get %1, the material chosen in Textures.").arg(levelBrushMaterial()));
	} else if (doom) {
		m_levelShapeNote->setText(tr("Sectors get the map's default floor, ceiling and wall textures. Rebuild the nodes before playing."));
	} else if (doomMap) {
		m_levelShapeNote->setText(tr("This map's format is edited through UDMF Properties; sector shapes need a Doom or Hexen format map."));
	} else {
		m_levelShapeNote->setText(tr("Shapes are for Quake-family maps and Doom sectors."));
	}
}

void ApplicationShell::saveLevelShapeSettings()
{
	if (m_restoringLevelShape || !m_levelShapeSides) {
		return;
	}
	const QJsonObject settings {
		{QStringLiteral("shape"), m_levelShape},
		{QStringLiteral("sectorShape"), m_levelSectorShape},
		{QStringLiteral("axis"), m_levelShapeAxis->currentData().toInt()},
		{QStringLiteral("sides"), m_levelShapeSides->value()},
		{QStringLiteral("bands"), m_levelShapeBands->value()},
		{QStringLiteral("thickness"), m_levelShapeThickness->value()},
		{QStringLiteral("arc"), m_levelShapeArc->value()},
		{QStringLiteral("start"), m_levelShapeStart->value()},
		{QStringLiteral("steps"), m_levelShapeSteps->value()},
		{QStringLiteral("rise"), m_levelShapeRise->currentData().toString()},
		{QStringLiteral("width"), m_levelShapeWidth->value()},
		{QStringLiteral("depth"), m_levelShapeDepth->value()},
		{QStringLiteral("stepHeight"), m_levelShapeStepHeight->value()},
		{QStringLiteral("columns"), m_levelShapeColumns->value()},
		{QStringLiteral("rows"), m_levelShapeRows->value()},
	};
	m_settings.setShellLayoutState(QStringLiteral("levelShapes/settings"), QJsonDocument(settings).toJson(QJsonDocument::Compact));
}

int ApplicationShell::levelShapeViewAxis() const
{
	if (!m_levelMapViewport) {
		return 2;
	}
	switch (m_levelMapViewport->projection()) {
	case MapViewportProjection::FrontXZ:
		return 1;
	case MapViewportProjection::SideZY:
		return 0;
	case MapViewportProjection::TopXY:
		break;
	}
	return 2;
}

LevelShapeRequest ApplicationShell::levelShapeRequest(const LevelMapVec3& mins, const LevelMapVec3& maxs, int viewAxis) const
{
	LevelShapeRequest request;
	request.shape = m_levelShape;
	request.mins = mins;
	request.maxs = maxs;
	const int chosen = m_levelShapeAxis ? m_levelShapeAxis->currentData().toInt() : -1;
	request.axis = chosen >= 0 ? chosen : std::clamp(viewAxis, 0, 2);
	if (m_levelShapeSides) {
		request.sides = m_levelShapeSides->value();
		request.bands = m_levelShapeBands->value();
		request.thickness = m_levelShapeThickness->value();
		request.arc = m_levelShapeArc->value();
		request.startAngle = m_levelShapeStart->value();
		request.steps = m_levelShapeSteps->value();
		request.rise = m_levelShapeRise->currentData().toString();
	}
	request.texture = levelBrushMaterial();
	return request;
}

bool ApplicationShell::addLevelShapeFromUi(const LevelMapVec3& mins, const LevelMapVec3& maxs, int viewAxis, bool drawn)
{
	const LevelShapeRequest request = levelShapeRequest(mins, maxs, viewAxis);
	const QString label = levelShapeLabel(request.shape);
	QVector<int> added;
	QString error;
	if (!addLevelMapShape(&m_levelMapDocument, request, &added, &error)) {
		statusBar()->showMessage(tr("Could not make the %1: %2").arg(label, error), 6000);
		return false;
	}
	m_lastDrawnBrushTexture = request.texture;
	rememberLevelMaterial(request.texture);
	recordActivity(drawn ? tr("Level map shape drawn") : tr("Level map shape added"), QStringLiteral("%1 brush:%2").arg(request.shape).arg(added.value(0)),
		QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("%1 added: %n brush(es) in %2, selected.", nullptr, static_cast<int>(added.size())).arg(label, request.texture), 5000);
	return true;
}

void ApplicationShell::addLevelShapeAtViewCentre()
{
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!quake || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a Quake-family map to add brush shapes."));
		return;
	}
	const LevelMapVec3 centre = m_levelMapViewport->worldPositionAt(QPointF(m_levelMapViewport->rect().center()), levelMapHiddenAxisValue());
	const int grid = std::max(1, m_levelMapViewport->gridSize());
	// Shapes of several brushes need room for their walls and steps.
	const double size = levelShapeIsCompound(m_levelShape) ? std::max(128, grid * 4) : std::max(64, grid * 2);
	const LevelMapVec3 mins {snapLevelMapCoordinate(centre.x - size / 2, grid), snapLevelMapCoordinate(centre.y - size / 2, grid),
		snapLevelMapCoordinate(centre.z - size / 2, grid), true};
	const LevelMapVec3 maxs {mins.x + size, mins.y + size, mins.z + size, true};
	addLevelShapeFromUi(mins, maxs, levelShapeViewAxis(), false);
}

void ApplicationShell::replaceLevelSelectionWithShape()
{
	QVector<int> brushes;
	for (const LevelMapSelectionRef& ref : std::as_const(m_levelMapDocument.selection)) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes.push_back(ref.objectId);
		}
	}
	if (brushes.isEmpty()) {
		statusBar()->showMessage(tr("Select the brushes the shape should replace."));
		return;
	}
	const LevelShapeRequest request = levelShapeRequest({}, {}, levelShapeViewAxis());
	QVector<int> added;
	QString error;
	if (!replaceLevelMapBrushesWithShape(&m_levelMapDocument, brushes, request, &added, &error)) {
		statusBar()->showMessage(tr("Could not replace them: %1").arg(error), 6000);
		return;
	}
	rememberLevelMaterial(request.texture);
	recordActivity(tr("Brushes replaced with a shape"), QStringLiteral("%1 brush:%2").arg(request.shape).arg(added.value(0)), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Replaced %n brush(es) with the %1.", nullptr, static_cast<int>(brushes.size())).arg(levelShapeLabel(request.shape)), 5000);
}

void ApplicationShell::addLevelSectorShapeAtViewCentre()
{
	if (!levelMapDoomEditable() || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a Doom or Hexen format map to add sectors."));
		return;
	}
	const LevelMapVec3 middle = m_levelMapViewport->worldPositionAt(QPointF(m_levelMapViewport->rect().center()), 0.0);
	const int grid = std::max(1, m_levelMapViewport->gridSize());
	const double centreX = snapLevelMapCoordinate(middle.x, grid);
	const double centreY = snapLevelMapCoordinate(middle.y, grid);
	const double halfWidth = m_levelShapeWidth->value() / 2.0;
	const double halfDepth = m_levelShapeDepth->value() / 2.0;
	addLevelSectorShape(centreX - halfWidth, centreY - halfDepth, centreX + halfWidth, centreY + halfDepth);
}

void ApplicationShell::addLevelSectorShape(double minX, double minY, double maxX, double maxY)
{
	if (!levelMapDoomEditable()) {
		statusBar()->showMessage(tr("Open a Doom or Hexen format map to add sectors."));
		return;
	}
	const double centreX = (minX + maxX) / 2.0;
	const double centreY = (minY + maxY) / 2.0;
	const double halfWidth = (maxX - minX) / 2.0;
	const double halfDepth = (maxY - minY) / 2.0;
	if (m_levelSectorShape == kSectorStairs || m_levelSectorShape == kSectorGrid) {
		QVector<int> ids;
		QString error;
		bool drawn = false;
		if (m_levelSectorShape == kSectorStairs) {
			LevelDoomStairsRequest request;
			request.minX = centreX - halfWidth;
			request.minY = centreY - halfDepth;
			request.maxX = centreX + halfWidth;
			request.maxY = centreY + halfDepth;
			request.steps = m_levelShapeSteps->value();
			request.stepHeight = m_levelShapeStepHeight->value();
			request.rise = m_levelShapeRise->currentData().toString();
			drawn = drawLevelMapDoomStairs(&m_levelMapDocument, request, &ids, &error);
		} else {
			drawn = drawLevelMapDoomGrid(&m_levelMapDocument, centreX - halfWidth, centreY - halfDepth, centreX + halfWidth, centreY + halfDepth,
				m_levelShapeColumns->value(), m_levelShapeRows->value(), &ids, &error);
		}
		if (!drawn) {
			statusBar()->showMessage(tr("Could not draw the sectors: %1").arg(error), 6000);
			return;
		}
		recordActivity(tr("Level map sectors drawn"), QString::number(ids.size()), QStringLiteral("level-map"), OperationState::Warning,
			tr("Unsaved map edit; run a node builder before playing"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Drew %n sector(s), selected. Rebuild the nodes before playing the map.", nullptr, static_cast<int>(ids.size())),
			5000);
		return;
	}
	QVector<LevelMapVec3> corners;
	if (m_levelSectorShape == kSectorPolygon) {
		// Clockwise from the top, as Draw Sector traces.
		const int sides = std::max(3, m_levelShapeSides->value());
		for (int corner = 0; corner < sides; ++corner) {
			const double angle = std::numbers::pi / 2.0 - 2.0 * std::numbers::pi * corner / sides;
			corners.push_back({std::round(centreX + halfWidth * std::cos(angle)), std::round(centreY + halfDepth * std::sin(angle)), 0.0, true});
		}
	} else {
		corners = {{centreX - halfWidth, centreY - halfDepth, 0.0, true}, {centreX - halfWidth, centreY + halfDepth, 0.0, true},
			{centreX + halfWidth, centreY + halfDepth, 0.0, true}, {centreX + halfWidth, centreY - halfDepth, 0.0, true}};
		for (LevelMapVec3& corner : corners) {
			corner = {std::round(corner.x), std::round(corner.y), 0.0, true};
		}
	}
	applyLevelMapSectorDrawing(corners);
}

} // namespace vibestudio
