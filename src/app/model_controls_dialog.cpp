#include "app/model_controls_dialog.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelControls)
};

const Qt::KeyboardModifiers kModifierChoices[] = {
	Qt::NoModifier,
	Qt::ShiftModifier,
	Qt::ControlModifier,
	Qt::AltModifier,
	Qt::ControlModifier | Qt::ShiftModifier,
	Qt::ControlModifier | Qt::AltModifier,
	Qt::AltModifier | Qt::ShiftModifier,
	Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier,
};

QString nativeKeys(const QStringList &keys)
{
	QStringList native;
	for (const auto &key : keys)
		native << QKeySequence::fromString(key, QKeySequence::PortableText).toString(QKeySequence::NativeText);
	return native.join(QStringLiteral(", "));
}

void selectData(QComboBox *combo, const QVariant &value)
{
	combo->setCurrentIndex(std::max(0, combo->findData(value)));
}
} // namespace

ModelControlsDialog::ModelControlsDialog(const ModelEditorControls &controls, QWidget *parent) : QDialog(parent), m_controls(controls)
{
	setObjectName(QStringLiteral("modelControlsDialog"));
	ModelEditorProfile profile;
	static_cast<void>(modelEditorProfileForId(controls.profileId, &profile));
	setWindowTitle(Text::tr("Customise %1 Controls").arg(profile.displayName));
	setAccessibleName(windowTitle());
	auto *layout = new QVBoxLayout(this);
	auto *intro = new QLabel(Text::tr("Changes are saved for this profile only; anything you leave alone keeps following it."));
	intro->setWordWrap(true);
	layout->addWidget(intro);
	auto *tabs = new QTabWidget;
	tabs->setObjectName(QStringLiteral("modelControlsTabs"));
	tabs->addTab(buildKeys(), Text::tr("Keys"));
	tabs->addTab(buildMouse(), Text::tr("Mouse"));
	tabs->addTab(buildTransforms(), Text::tr("Transforms and Layout"));
	layout->addWidget(tabs, 1);
	m_problems = new QLabel;
	m_problems->setObjectName(QStringLiteral("modelControlsProblems"));
	m_problems->setWordWrap(true);
	m_problems->setTextFormat(Qt::PlainText);
	m_problems->setAccessibleName(Text::tr("Control conflicts"));
	layout->addWidget(m_problems);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto *reset = buttons->addButton(Text::tr("Reset to Profile Defaults"), QDialogButtonBox::ResetRole);
	reset->setObjectName(QStringLiteral("modelControlsReset"));
	connect(reset, &QPushButton::clicked, this, [this] { resetToProfile(); });
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	resize(760, 640);
	load();
}

void ModelControlsDialog::resetToProfile()
{
	m_controls = modelEditorControlsForProfile(m_controls.profileId);
	load();
}

void ModelControlsDialog::setCommandKeys(const QString &commandId, const QStringList &keys)
{
	if (!modelEditorCommandForId(commandId))
		return;
	bool replaced = false;
	for (auto &binding : m_controls.keys)
	{
		if (binding.commandId == commandId)
		{
			binding.keys = keys;
			replaced = true;
		}
	}
	if (!replaced)
		m_controls.keys.append({commandId, keys});
	refreshKeys();
	refreshProblems();
}

QWidget *ModelControlsDialog::buildKeys()
{
	auto *page = new QWidget;
	auto *column = new QVBoxLayout(page);
	m_filter = new QLineEdit;
	m_filter->setObjectName(QStringLiteral("modelControlsFilter"));
	m_filter->setPlaceholderText(Text::tr("Filter commands or keys"));
	m_filter->setAccessibleName(Text::tr("Filter commands"));
	m_filter->setClearButtonEnabled(true);
	column->addWidget(m_filter);
	m_keys = new QTableWidget;
	m_keys->setObjectName(QStringLiteral("modelControlsKeys"));
	m_keys->setAccessibleName(Text::tr("Commands and their keys"));
	m_keys->setColumnCount(3);
	m_keys->setHorizontalHeaderLabels({Text::tr("Group"), Text::tr("Command"), Text::tr("Keys")});
	m_keys->verticalHeader()->hide();
	m_keys->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_keys->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_keys->setSelectionMode(QAbstractItemView::SingleSelection);
	m_keys->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_keys->horizontalHeader()->setStretchLastSection(true);
	column->addWidget(m_keys, 1);
	auto *row = new QHBoxLayout;
	auto *label = new QLabel(Text::tr("Key for the selected command:"));
	m_keyEdit = new QKeySequenceEdit;
	m_keyEdit->setObjectName(QStringLiteral("modelControlsKeyEdit"));
	m_keyEdit->setAccessibleName(Text::tr("New key"));
	m_keyEdit->setMaximumSequenceLength(1);
	label->setBuddy(m_keyEdit);
	auto *assign = new QPushButton(Text::tr("Assign"));
	assign->setObjectName(QStringLiteral("modelControlsAssign"));
	auto *add = new QPushButton(Text::tr("Add"));
	add->setObjectName(QStringLiteral("modelControlsAddKey"));
	auto *clear = new QPushButton(Text::tr("No Keys"));
	clear->setObjectName(QStringLiteral("modelControlsClearKeys"));
	auto *restore = new QPushButton(Text::tr("Profile Keys"));
	restore->setObjectName(QStringLiteral("modelControlsRestoreKeys"));
	row->addWidget(label);
	row->addWidget(m_keyEdit, 1);
	row->addWidget(assign);
	row->addWidget(add);
	row->addWidget(clear);
	row->addWidget(restore);
	column->addLayout(row);
	connect(m_filter, &QLineEdit::textChanged, this, [this] { refreshKeys(); });
	connect(assign, &QPushButton::clicked, this,
			[this]
			{
				const QString id = selectedCommand();
				if (!id.isEmpty() && !m_keyEdit->keySequence().isEmpty())
					setCommandKeys(id, {m_keyEdit->keySequence().toString(QKeySequence::PortableText)});
			});
	connect(add, &QPushButton::clicked, this,
			[this]
			{
				const QString id = selectedCommand();
				if (id.isEmpty() || m_keyEdit->keySequence().isEmpty())
					return;
				auto keys = modelEditorCommandKeys(m_controls, id);
				const auto key = m_keyEdit->keySequence().toString(QKeySequence::PortableText);
				if (!keys.contains(key))
					keys << key;
				setCommandKeys(id, keys);
			});
	connect(clear, &QPushButton::clicked, this,
			[this]
			{
				const QString id = selectedCommand();
				if (!id.isEmpty())
					setCommandKeys(id, {});
			});
	connect(restore, &QPushButton::clicked, this,
			[this]
			{
				const QString id = selectedCommand();
				if (id.isEmpty())
					return;
				setCommandKeys(id, modelEditorCommandKeys(modelEditorControlsForProfile(m_controls.profileId), id));
			});
	return page;
}

ModelControlsDialog::Gesture ModelControlsDialog::gestureField(const QString &name)
{
	Gesture gesture;
	gesture.button = new QComboBox;
	gesture.button->setObjectName(name + QStringLiteral("Button"));
	gesture.button->addItem(Text::tr("None"), int(Qt::NoButton));
	gesture.button->addItem(Text::tr("Left button"), int(Qt::LeftButton));
	gesture.button->addItem(Text::tr("Middle button"), int(Qt::MiddleButton));
	gesture.button->addItem(Text::tr("Right button"), int(Qt::RightButton));
	gesture.modifiers = modifierField(name + QStringLiteral("Keys"));
	for (auto *combo : {gesture.button, gesture.modifiers})
		connect(combo, &QComboBox::currentIndexChanged, this, [this] { store(); });
	return gesture;
}

QComboBox *ModelControlsDialog::modifierField(const QString &name)
{
	auto *combo = new QComboBox;
	combo->setObjectName(name);
	for (const auto modifiers : kModifierChoices)
		combo->addItem(modifiers == Qt::NoModifier ? Text::tr("No keys") : modelModifierText(modifiers), int(modifiers));
	connect(combo, &QComboBox::currentIndexChanged, this, [this] { store(); });
	return combo;
}

QWidget *ModelControlsDialog::buildMouse()
{
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *page = new QWidget;
	auto *form = new QFormLayout(page);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	const auto gestureRow = [&](const QString &label, Gesture gesture)
	{
		auto *row = new QWidget;
		auto *line = new QHBoxLayout(row);
		line->setContentsMargins(0, 0, 0, 0);
		line->addWidget(gesture.modifiers);
		line->addWidget(gesture.button, 1);
		gesture.button->setAccessibleName(label);
		gesture.modifiers->setAccessibleName(Text::tr("%1 keys").arg(label));
		form->addRow(label, row);
	};
	const auto check = [&](QCheckBox **box, const QString &name, const QString &text)
	{
		*box = new QCheckBox(text);
		(*box)->setObjectName(name);
		connect(*box, &QCheckBox::toggled, this, [this] { store(); });
		form->addRow(*box);
	};
	const auto modifierRow = [&](QComboBox **combo, const QString &name, const QString &label)
	{
		*combo = modifierField(name);
		(*combo)->setAccessibleName(label);
		form->addRow(label, *combo);
	};
	gestureRow(Text::tr("Orbit (3D view)"), m_orbit = gestureField(QStringLiteral("modelControlsOrbit")));
	gestureRow(Text::tr("Orbit (orthographic views)"), m_planOrbit = gestureField(QStringLiteral("modelControlsPlanOrbit")));
	gestureRow(Text::tr("Pan"), m_pan = gestureField(QStringLiteral("modelControlsPan")));
	modifierRow(&m_leftPan, QStringLiteral("modelControlsLeftPan"), Text::tr("Left drag pans with"));
	gestureRow(Text::tr("Drag zoom"), m_zoom = gestureField(QStringLiteral("modelControlsZoom")));
	check(&m_emulateMiddle, QStringLiteral("modelControlsEmulateMiddle"), Text::tr("Alt+left drag stands in for the middle button"));
	check(&m_invertWheel, QStringLiteral("modelControlsInvertWheel"), Text::tr("Wheel up zooms the 3D view out"));
	check(&m_orbitLeavesOrtho, QStringLiteral("modelControlsOrbitLeavesOrtho"), Text::tr("Orbiting an orthographic view makes it a user view"));
	modifierRow(&m_extend, QStringLiteral("modelControlsExtend"), Text::tr("Add to the selection with"));
	check(&m_extendToggles, QStringLiteral("modelControlsExtendToggles"), Text::tr("Adding to a selected element removes it"));
	modifierRow(&m_subtract, QStringLiteral("modelControlsSubtract"), Text::tr("Remove from the selection with"));
	gestureRow(Text::tr("Drag to remove"), m_subtractDrag = gestureField(QStringLiteral("modelControlsSubtractDrag")));
	check(&m_boxSelect, QStringLiteral("modelControlsBoxSelect"), Text::tr("A left drag over empty space draws a selection box"));
	modifierRow(&m_loop, QStringLiteral("modelControlsLoop"), Text::tr("Click selects an edge loop with"));
	modifierRow(&m_ring, QStringLiteral("modelControlsRing"), Text::tr("Click selects an edge ring with"));
	modifierRow(&m_path, QStringLiteral("modelControlsPath"), Text::tr("Click selects a shortest path with"));
	check(&m_doubleClickLoop, QStringLiteral("modelControlsDoubleClickLoop"), Text::tr("Double-click an edge to select its loop"));
	gestureRow(Text::tr("Lasso"), m_lasso = gestureField(QStringLiteral("modelControlsLasso")));
	gestureRow(Text::tr("Place the 3D cursor"), m_cursor = gestureField(QStringLiteral("modelControlsCursor")));
	check(&m_rightClickMenu, QStringLiteral("modelControlsRightClickMenu"), Text::tr("Right-click opens the context menu"));
	scroll->setWidget(page);
	return scroll;
}

QWidget *ModelControlsDialog::buildTransforms()
{
	auto *page = new QWidget;
	auto *form = new QFormLayout(page);
	m_style = new QComboBox;
	m_style->setObjectName(QStringLiteral("modelControlsStyle"));
	m_style->setAccessibleName(Text::tr("Transform style"));
	m_style->addItem(Text::tr("Keys start a transform that follows the pointer (Blender)"), modelTransformStyleId(ModelTransformStyle::Modal));
	m_style->addItem(Text::tr("Keys pick a lasting tool (3ds Max, MilkShape 3D)"), modelTransformStyleId(ModelTransformStyle::ToolMode));
	connect(m_style, &QComboBox::currentIndexChanged, this, [this] { store(); });
	form->addRow(Text::tr("Transforms"), m_style);
	m_dragSelection = new QCheckBox(Text::tr("With a tool, dragging the selection transforms it"));
	m_dragSelection->setObjectName(QStringLiteral("modelControlsDragSelection"));
	connect(m_dragSelection, &QCheckBox::toggled, this, [this] { store(); });
	form->addRow(m_dragSelection);
	m_duplicateDrag = modifierField(QStringLiteral("modelControlsDuplicateDrag"));
	m_duplicateDrag->setAccessibleName(Text::tr("Duplicate while dragging with"));
	form->addRow(Text::tr("Duplicate while dragging with"), m_duplicateDrag);
	m_precision = modifierField(QStringLiteral("modelControlsPrecision"));
	m_precision->setAccessibleName(Text::tr("Fine control with"));
	form->addRow(Text::tr("Fine control with"), m_precision);
	m_snap = modifierField(QStringLiteral("modelControlsSnap"));
	m_snap->setAccessibleName(Text::tr("Snap while holding"));
	form->addRow(Text::tr("Snap while holding"), m_snap);
	m_layout = new QComboBox;
	m_layout->setObjectName(QStringLiteral("modelControlsLayout"));
	m_layout->setAccessibleName(Text::tr("View layout"));
	m_layout->addItem(Text::tr("One view"), modelViewLayoutId(ModelViewLayout::Single));
	m_layout->addItem(Text::tr("Four views"), modelViewLayoutId(ModelViewLayout::FourViews));
	connect(m_layout, &QComboBox::currentIndexChanged, this, [this] { store(); });
	form->addRow(Text::tr("Views"), m_layout);
	const QString corners[4]{Text::tr("Top left"), Text::tr("Top right"), Text::tr("Bottom left"), Text::tr("Bottom right")};
	for (int quadrant = 0; quadrant < 4; ++quadrant)
	{
		m_panes[quadrant] = new QComboBox;
		m_panes[quadrant]->setObjectName(QStringLiteral("modelControlsPane%1").arg(quadrant));
		m_panes[quadrant]->setAccessibleName(corners[quadrant]);
		for (ModelPaneView pane : {ModelPaneView::Perspective, ModelPaneView::Top, ModelPaneView::Bottom, ModelPaneView::Front, ModelPaneView::Back,
								   ModelPaneView::Left, ModelPaneView::Right})
			m_panes[quadrant]->addItem(modelPaneViewDisplayName(pane), modelPaneViewId(pane));
		connect(m_panes[quadrant], &QComboBox::currentIndexChanged, this, [this] { store(); });
		form->addRow(corners[quadrant], m_panes[quadrant]);
	}
	const auto check = [&](QCheckBox **box, const QString &name, const QString &text)
	{
		*box = new QCheckBox(text);
		(*box)->setObjectName(name);
		connect(*box, &QCheckBox::toggled, this, [this] { store(); });
		form->addRow(*box);
	};
	check(&m_startPerspective, QStringLiteral("modelControlsStartPerspective"), Text::tr("One view starts in perspective"));
	check(&m_timeline, QStringLiteral("modelControlsTimeline"), Text::tr("Show the animation timeline"));
	check(&m_toolShelf, QStringLiteral("modelControlsToolShelf"), Text::tr("Show the tool shelf"));
	return page;
}

void ModelControlsDialog::load()
{
	m_loading = true;
	const auto setGesture = [](const Gesture &gesture, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
	{
		selectData(gesture.button, int(button));
		selectData(gesture.modifiers, int(modifiers));
	};
	const auto &n = m_controls.navigation;
	setGesture(m_orbit, n.view3D.orbitButton, n.view3D.orbitModifiers);
	setGesture(m_planOrbit, n.orthographic.orbitButton, n.orthographic.orbitModifiers);
	setGesture(m_pan, n.view3D.panButtons.isEmpty() ? Qt::NoButton : n.view3D.panButtons.first(), n.view3D.panModifiers);
	selectData(m_leftPan, int(n.view3D.leftPanModifiers));
	setGesture(m_zoom, n.zoomButton, n.zoomModifiers);
	m_emulateMiddle->setChecked(n.emulateMiddleButton);
	m_invertWheel->setChecked(n.invertWheel3D);
	m_orbitLeavesOrtho->setChecked(n.orbitLeavesOrthographic);
	const auto &s = m_controls.selection;
	selectData(m_extend, int(s.extendModifiers));
	m_extendToggles->setChecked(s.extendToggles);
	selectData(m_subtract, int(s.subtractModifiers));
	setGesture(m_subtractDrag, s.subtractDragButton, s.subtractDragModifiers);
	m_boxSelect->setChecked(s.emptyDragBoxSelects);
	selectData(m_loop, int(s.loopModifiers));
	selectData(m_ring, int(s.ringModifiers));
	selectData(m_path, int(s.pathModifiers));
	m_doubleClickLoop->setChecked(s.doubleClickSelectsLoop);
	setGesture(m_lasso, s.lassoButton, s.lassoModifiers);
	setGesture(m_cursor, s.cursorButton, s.cursorModifiers);
	m_rightClickMenu->setChecked(s.rightClickMenu);
	const auto &t = m_controls.transform;
	selectData(m_style, modelTransformStyleId(t.style));
	m_dragSelection->setChecked(t.dragSelectionTransforms);
	selectData(m_duplicateDrag, int(t.duplicateDragModifiers));
	selectData(m_precision, int(t.precisionModifiers));
	selectData(m_snap, int(t.snapModifiers));
	const auto &l = m_controls.layout;
	selectData(m_layout, modelViewLayoutId(l.layout));
	for (int quadrant = 0; quadrant < 4; ++quadrant)
		selectData(m_panes[quadrant], modelPaneViewId(l.panes.value(quadrant)));
	m_startPerspective->setChecked(l.startInPerspective);
	m_timeline->setChecked(l.timelineVisible);
	m_toolShelf->setChecked(l.toolShelfVisible);
	m_loading = false;
	refreshKeys();
	refreshProblems();
}

void ModelControlsDialog::store()
{
	if (m_loading)
		return;
	const auto button = [](const Gesture &gesture) { return Qt::MouseButton(gesture.button->currentData().toInt()); };
	const auto modifiers = [](QComboBox *combo) { return Qt::KeyboardModifiers(combo->currentData().toInt()); };
	auto &n = m_controls.navigation;
	n.view3D.orbitButton = button(m_orbit);
	n.view3D.orbitModifiers = modifiers(m_orbit.modifiers);
	n.orthographic.orbitButton = button(m_planOrbit);
	n.orthographic.orbitModifiers = modifiers(m_planOrbit.modifiers);
	for (auto *camera : {&n.view3D, &n.orthographic})
	{
		camera->panButtons = button(m_pan) == Qt::NoButton ? QVector<Qt::MouseButton>{} : QVector<Qt::MouseButton>{button(m_pan)};
		camera->panModifiers = modifiers(m_pan.modifiers);
		camera->leftPanModifiers = modifiers(m_leftPan);
	}
	n.zoomButton = button(m_zoom);
	n.zoomModifiers = modifiers(m_zoom.modifiers);
	n.emulateMiddleButton = m_emulateMiddle->isChecked();
	n.invertWheel3D = m_invertWheel->isChecked();
	n.orbitLeavesOrthographic = m_orbitLeavesOrtho->isChecked();
	auto &s = m_controls.selection;
	s.extendModifiers = modifiers(m_extend);
	s.extendToggles = m_extendToggles->isChecked();
	s.subtractModifiers = modifiers(m_subtract);
	s.subtractDragButton = button(m_subtractDrag);
	s.subtractDragModifiers = modifiers(m_subtractDrag.modifiers);
	s.emptyDragBoxSelects = m_boxSelect->isChecked();
	s.loopModifiers = modifiers(m_loop);
	s.ringModifiers = modifiers(m_ring);
	s.pathModifiers = modifiers(m_path);
	s.doubleClickSelectsLoop = m_doubleClickLoop->isChecked();
	s.lassoButton = button(m_lasso);
	s.lassoModifiers = modifiers(m_lasso.modifiers);
	s.cursorButton = button(m_cursor);
	s.cursorModifiers = modifiers(m_cursor.modifiers);
	s.rightClickMenu = m_rightClickMenu->isChecked();
	auto &t = m_controls.transform;
	modelTransformStyleForId(m_style->currentData().toString(), &t.style);
	t.dragSelectionTransforms = m_dragSelection->isChecked();
	t.duplicateDragModifiers = modifiers(m_duplicateDrag);
	t.precisionModifiers = modifiers(m_precision);
	t.snapModifiers = modifiers(m_snap);
	auto &l = m_controls.layout;
	modelViewLayoutForId(m_layout->currentData().toString(), &l.layout);
	for (int quadrant = 0; quadrant < 4 && quadrant < l.panes.size(); ++quadrant)
		modelPaneViewForId(m_panes[quadrant]->currentData().toString(), &l.panes[quadrant]);
	l.startInPerspective = m_startPerspective->isChecked();
	l.timelineVisible = m_timeline->isChecked();
	l.toolShelfVisible = m_toolShelf->isChecked();
	refreshProblems();
}

QString ModelControlsDialog::selectedCommand() const
{
	const int row = m_keys->currentRow();
	return row >= 0 && m_keys->item(row, 1) ? m_keys->item(row, 1)->data(Qt::UserRole).toString() : QString();
}

void ModelControlsDialog::refreshKeys()
{
	const QString selected = selectedCommand();
	const QString filter = m_filter->text().trimmed();
	const QHash<QString, QString> groups{
		{QStringLiteral("file"), Text::tr("File")},		  {QStringLiteral("edit"), Text::tr("Edit")},
		{QStringLiteral("select"), Text::tr("Select")},	  {QStringLiteral("transform"), Text::tr("Transform")},
		{QStringLiteral("mesh"), Text::tr("Mesh")},		  {QStringLiteral("uv"), Text::tr("UV")},
		{QStringLiteral("view"), Text::tr("View")},		  {QStringLiteral("animation"), Text::tr("Animation")},
		{QStringLiteral("tools"), Text::tr("Tools")},
	};
	m_keys->setRowCount(0);
	for (const auto &command : modelEditorCommands())
	{
		const QString keys = nativeKeys(modelEditorCommandKeys(m_controls, command.id));
		const QString group = groups.value(command.category, command.category);
		if (!filter.isEmpty() && !command.title.contains(filter, Qt::CaseInsensitive) && !keys.contains(filter, Qt::CaseInsensitive) &&
			!group.contains(filter, Qt::CaseInsensitive))
			continue;
		const int row = m_keys->rowCount();
		m_keys->insertRow(row);
		m_keys->setItem(row, 0, new QTableWidgetItem(group));
		auto *title = new QTableWidgetItem(command.title);
		title->setData(Qt::UserRole, command.id);
		m_keys->setItem(row, 1, title);
		m_keys->setItem(row, 2, new QTableWidgetItem(keys.isEmpty() ? Text::tr("(none)") : keys));
		if (command.id == selected)
			m_keys->setCurrentCell(row, 1);
	}
}

void ModelControlsDialog::refreshProblems()
{
	const auto problems = modelEditorControlProblems(m_controls);
	m_problems->setText(problems.isEmpty() ? Text::tr("No conflicts.") : problems.join(QLatin1Char('\n')));
	m_problems->setProperty("state", problems.isEmpty() ? QStringLiteral("ok") : QStringLiteral("warning"));
}
} // namespace vibestudio
