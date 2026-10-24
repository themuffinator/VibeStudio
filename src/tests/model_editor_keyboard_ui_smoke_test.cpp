// Keyboard-only use of the Mesh Editor in every modeller profile, with real
// key presses through Qt's shortcut map: Tab keys belong to the view only
// while it has focus, Ctrl+Tab and Ctrl+Shift+Tab (Control+Tab on macOS)
// move between panels and always leave the view, the document actions and
// the tool shelf are one Tab stop each with arrow keys between buttons, F9
// moves focus into Adjust Last Operation and Esc back, its settings keep
// the keys they use (Space in a check box is not Blender's Play), header,
// shelf and panel hints name the profile's own keys (no F9 or F3 where 3ds
// Max has none), and the key field in Customise Controls never holds focus.
// The pointer-only routes have
// keyboard ones: edge loops and rings of a selection made in the component
// table, Loop Cut on the selected edge, UV view pan and zoom, and Next View
// and Previous View between four views. Dark at 100% left to right, then
// high-contrast light at 200% text right to left.
#include "app/model_controls_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_uv_view.h"
#include "app/model_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/model_design.h"
#include "core/model_editor_controls.h"
#include "core/studio_settings.h"
#include "tests/model_uv_view_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

using namespace vibestudio;
namespace
{
int failures = 0;
bool expect(bool condition, const std::string &message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

bool until(const std::function<bool()> &condition)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < 3000)
		QTest::qWait(10);
	return condition();
}

// A key press where a keyboard user's goes: to the focused widget, through
// the shortcut map first.
void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : QApplication::activeWindow();
	if (target)
		QTest::keyClick(target, key, modifiers);
	QApplication::processEvents();
}

bool inside(const QWidget *area, const QWidget *widget)
{
	return area && widget && (widget == area || area->isAncestorOf(widget));
}

QString native(const char *portable)
{
	return QKeySequence::fromString(QString::fromLatin1(portable), QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

// The panel keys as this platform has them: Ctrl+Tab and Ctrl+Shift+Tab, or
// Control+Tab and Control+Shift+Tab on macOS, where Qt's Ctrl is Command.
QKeyCombination focusKey(bool forward)
{
	return QKeySequence::fromString(modelEditorFocusKeys(forward).value(0), QKeySequence::PortableText)[0];
}

QString nativeFocus(bool forward)
{
	return QKeySequence(focusKey(forward)).toString(QKeySequence::NativeText);
}

void pressFocus(bool forward)
{
	const auto key = focusKey(forward);
	press(key.key(), key.keyboardModifiers());
}

QString keys(const QAction *action)
{
	return action && !action->shortcuts().isEmpty() ? action->shortcuts().first().toString(QKeySequence::NativeText) : QString();
}

// A command as a keyboard user reaches it: the profile's own key where it has
// one, else its menu entry (a profile without the key still lists it there).
void runCommand(QAction *action)
{
	if (!action)
		return;
	if (!action->shortcuts().isEmpty() && QApplication::focusWidget())
		QTest::keySequence(QApplication::focusWidget(), action->shortcuts().first());
	else
		action->trigger();
	QApplication::processEvents();
}

bool near(QPointF a, QPointF b)
{
	return QLineF(a, b).length() < 0.01;
}

int panelOf(ModelEditorTools *tools, const QWidget *widget)
{
	const auto panels = tools->focusPanels();
	for (int index = 0; index < panels.size(); ++index)
	{
		if (inside(panels[index], widget))
			return index;
	}
	return int(panels.size());
}

// A flat 2x2 grid of quads at z = 0, as the profiles test builds it.
ModelMesh gridMesh()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	auto &surface = mesh.surfaces[0];
	constexpr int cells = 2;
	constexpr float size = 16;
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= cells; ++j)
	{
		for (int i = 0; i <= cells; ++i)
			positions.append({i * size, j * size, 0});
	}
	const auto at = [](int i, int j) { return j * (cells + 1) + i; };
	for (int j = 0; j < cells; ++j)
	{
		for (int i = 0; i < cells; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	}
	surface.triangles = triangles;
	surface.vertexCount = positions.size();
	surface.texCoords.clear();
	for (const auto &p : positions)
		surface.texCoords.append({p.x / 32.0f, p.y / 32.0f});
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame};
	surface.uvSeams.clear();
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void checkKeyHints(ModelEditorDialog *editor, const QString &profile, const std::string &name)
{
	auto *tools = editor->tools();
	auto *face = editor->findChild<QToolButton *>(QStringLiteral("meshSelectMode2"));
	auto *edge = editor->findChild<QToolButton *>(QStringLiteral("meshSelectMode1"));
	auto *xray = editor->findChild<QToolButton *>(QStringLiteral("meshXray"));
	if (!expect(face && edge && xray, name + ": the header has its mode and X-ray buttons"))
		return;
	const QString faceKey = keys(tools->action(QStringLiteral("meshModeFace")));
	const QString expectedFace = profile == QStringLiteral("3ds-max") ? QStringLiteral("4") : profile == QStringLiteral("milkshape-3d") ? native("F6") : QStringLiteral("3");
	expect(faceKey == expectedFace && face->toolTip().contains(faceKey) && face->accessibleDescription().contains(faceKey) &&
			   !face->accessibleName().contains(faceKey),
		   name + ": the face mode button names the profile's face key in its tooltip and description, not its name");
	if (profile == QStringLiteral("3ds-max"))
	{
		expect(!face->toolTip().contains(QStringLiteral("(3)")) && xray->toolTip().contains(native("Alt+X")) && !xray->toolTip().contains(native("Alt+Z")),
			   name + ": Face is 4 and X-ray is Alt+X, as 3ds Max binds them");
		expect(!tools->action(QStringLiteral("meshMove"))->toolTip().contains(QStringLiteral("(G)")),
			   name + ": the shelf's Move names no key where the profile gives it none");
	}
	if (profile == QStringLiteral("milkshape-3d"))
		expect(edge->toolTip() == edge->accessibleName() && !edge->accessibleDescription().isEmpty() && !edge->accessibleDescription().contains(QStringLiteral("2")),
			   name + ": a command without a key says so instead of naming Blender's");
	if (profile == QStringLiteral("studio"))
		expect(tools->action(QStringLiteral("meshMove"))->toolTip().contains(QStringLiteral("G")) && xray->toolTip().contains(native("Alt+Z")),
			   name + ": the shelf and header name the studio keys");
}

// Picks one edge in the component table with an arrow key, as a keyboard
// user moves through its rows.
bool selectEdgeInTable(ModelEditorDialog *editor, ModelEdge edge)
{
	auto *table = editor->findChild<QTableView *>(QStringLiteral("meshComponents"));
	editor->showSidebarPage(QStringLiteral("outliner"));
	if (!table || !table->model() || table->model()->rowCount() < 2)
		return false;
	auto *model = table->model();
	int row = -1;
	for (int candidate = 0; candidate < model->rowCount() && row < 0; ++candidate)
	{
		if (model->index(candidate, 1).data().toInt() == edge.first && model->index(candidate, 2).data().toInt() == edge.second)
			row = candidate;
	}
	if (row < 0)
		return false;
	table->setFocus(Qt::TabFocusReason);
	if (!until([&] { return table->hasFocus(); }))
		return false;
	table->selectionModel()->setCurrentIndex(model->index(row == 0 ? 1 : row - 1, 0), QItemSelectionModel::NoUpdate);
	press(row == 0 ? Qt::Key_Up : Qt::Key_Down);
	return table->currentIndex().row() == row;
}

// Keyboard routes for what was pointer-only: loops and rings of the selected
// edges, Loop Cut on a selected edge, UV view pan and zoom, and stepping the
// active one of four views.
void checkKeyboardRoutes(QApplication &app, ModelEditorDialog *editor, const QString &profile, const std::string &name)
{
	auto *tools = editor->tools();
	auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *mode = editor->findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
	auto *status = editor->findChild<QLabel *>(QStringLiteral("meshStatus"));
	const auto &document = editor->document();
	const int savedMode = mode->currentIndex();
	const auto focusView = [&]
	{
		preview->setFocus(Qt::OtherFocusReason);
		return until([&] { return QApplication::focusWidget() == preview; });
	};
	const auto edges = [&] { return document.selection().edges; };
	// The 2x2 grid's middle column: edge 1:4 runs up from (16, 0) to (16, 16).
	const auto seed = modelEdge(1, 4);
	expect(focusView(), name + ": the view takes focus for the edge mode key");
	runCommand(tools->action(QStringLiteral("meshModeEdge")));
	expect(mode->currentIndex() == 2, name + ": the edge mode command switches to edges");
	expect(selectEdgeInTable(editor, seed) && edges() == QSet<ModelEdge>{seed}, name + ": an arrow key in the component table selects edge 1:4");
	expect(focusView(), name + ": the view takes focus for the loop keys");
	runCommand(tools->action(QStringLiteral("meshSelectLoop")));
	expect(edges() == QSet<ModelEdge>{modelEdge(1, 4), modelEdge(4, 7)}, name + ": Select Edge Loops grows the selected edge to its loop");
	runCommand(tools->action(QStringLiteral("meshSelectRing")));
	expect(edges() == QSet<ModelEdge>{modelEdge(0, 3), modelEdge(1, 4), modelEdge(2, 5), modelEdge(3, 6), modelEdge(4, 7), modelEdge(5, 8)},
		   name + ": Select Edge Rings grows each selected edge to its ring");
	// Every profile but MilkShape 3D (which has no loops) keys them: Alt+L and
	// Alt+R, as 3ds Max binds them.
	if (profile != QStringLiteral("milkshape-3d"))
		expect(tools->action(QStringLiteral("meshSelectLoop"))->shortcut() == QKeySequence(Qt::ALT | Qt::Key_L) &&
				   tools->action(QStringLiteral("meshSelectRing"))->shortcut() == QKeySequence(Qt::ALT | Qt::Key_R),
			   name + ": Alt+L and Alt+R select loops and rings");
	// A quad diagonal has no loop: it stays selected and the banner says so.
	tools->applySelection({0, {}, {}, {seed, modelEdge(0, 4)}});
	runCommand(tools->action(QStringLiteral("meshSelectLoop")));
	auto *banner = preview->findChild<QLabel *>(QStringLiteral("meshOperatorBanner"));
	expect(edges() == QSet<ModelEdge>{modelEdge(1, 4), modelEdge(4, 7), modelEdge(0, 4)} && status->text().contains(QStringLiteral("without a loop")) &&
			   banner && banner->accessibleDescription().contains(QStringLiteral("Edge 0:4:")),
		   name + ": a diagonal is kept and named while the other edge grows");

	// Loop Cut on the selected edge, confirmed with Enter: one undo step.
	expect(selectEdgeInTable(editor, seed) && edges() == QSet<ModelEdge>{seed}, name + ": select edge 1:4 again");
	expect(focusView(), name + ": the view takes focus for Loop Cut");
	const auto before = document.revisionFingerprint();
	const bool couldUndo = document.canUndo();
	runCommand(tools->action(QStringLiteral("meshLoopCut")));
	expect(tools->modalOperator() == ModelModalOperator::LoopCut && tools->modalPreviewValid() &&
			   tools->modalText().contains(QStringLiteral("1:4")),
		   name + ": Loop Cut starts on the selected edge without a pointer");
	press(Qt::Key_Return);
	expect(until([&] { return tools->modalOperator() == ModelModalOperator::None && document.mesh().surfaces[0].triangles.size() == 12; }),
		   name + ": Enter cuts across the selected edge's ring");
	int across = 0;
	for (const auto &p : document.mesh().surfaces[0].frames[0].positions)
		across += std::abs(p.y - 8) < 1e-4 && std::abs(p.z) < 1e-4;
	expect(across == 3 && edges().size() == 2, name + ": the cut runs at y = 8 through both quads and is selected");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(until([&] { return document.revisionFingerprint() == before; }) && document.canUndo() == couldUndo,
		   name + ": one undo removes the loop cut");

	// The UV view pans with the arrow keys and zooms with + and -, in either
	// direction; the arrows are not mirrored.
	auto *tabs = editor->findChild<QTabWidget *>(QStringLiteral("meshViewTabs"));
	auto *uv = editor->findChild<ModelUvView *>(QStringLiteral("meshUvPreview"));
	tabs->setCurrentIndex(1);
	app.processEvents();
	if (expect(tests::settleModelUv(*uv), name + ": the UV view settles"))
	{
		uv->setFocus(Qt::TabFocusReason);
		expect(until([&] { return uv->hasFocus(); }), name + ": the UV view takes focus");
		const ModelTexCoord probe{0.25f, 0.25f};
		const auto start = uv->uvToScreen(probe);
		press(Qt::Key_Right);
		expect(near(uv->uvToScreen(probe), start + QPointF(-24, 0)), name + ": Right pans the UV view 24 px");
		press(Qt::Key_Down, Qt::ShiftModifier);
		expect(near(uv->uvToScreen(probe), start + QPointF(-24, -4)), name + ": Shift+Down pans it 4 px");
		const auto panned = uv->uvToScreen(probe);
		const QPointF centre(uv->width() / 2.0, uv->height() / 2.0);
		press(Qt::Key_Plus);
		expect(near(uv->uvToScreen(probe), centre + (panned - centre) * 1.2), name + ": + zooms the UV view about its centre");
		press(Qt::Key_Minus);
		expect(near(uv->uvToScreen(probe), panned), name + ": - zooms back out");
		tests::settleModelUv(*uv);
	}
	tabs->setCurrentIndex(0);
	app.processEvents();

	// Next View and Previous View step the active pane of four views.
	const bool single = editor->viewLayout() != ModelViewLayout::FourViews;
	auto *next = tools->action(QStringLiteral("meshNextView"));
	auto *previous = tools->action(QStringLiteral("meshPreviousView"));
	expect(next && previous && next->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_PageDown) &&
			   previous->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_PageUp),
		   name + ": every profile gives Next View and Previous View Ctrl+Alt+Page Down and Page Up");
	if (single)
	{
		expect(focusView(), name + ": the single view takes focus");
		const int pane = editor->activePane();
		runCommand(next);
		expect(editor->activePane() == pane && status->text().contains(QStringLiteral("four views")),
			   name + ": Next View in one view says how to open four");
		editor->setViewLayout(ModelViewLayout::FourViews);
		app.processEvents();
	}
	expect(focusView(), name + ": the active pane takes focus");
	const int start = editor->activePane();
	const auto stepTo = [&](QAction *action, int expected, const char *what)
	{
		runCommand(action);
		const QString view = modelPaneViewDisplayName(editor->paneView(expected));
		expect(editor->activePane() == expected && editor->panes().value(expected) == preview && QApplication::focusWidget() == preview &&
				   preview->accessibleName().contains(view) && status->text().contains(view),
			   name + ": " + what);
	};
	stepTo(next, (start + 1) % 4, "Next View makes the next pane active, with focus and its name");
	stepTo(previous, start, "Previous View returns to the pane before");
	stepTo(previous, (start + 3) % 4, "Previous View wraps round to the last pane");
	stepTo(next, start, "Next View wraps back");
	if (single)
	{
		editor->setViewLayout(ModelViewLayout::Single);
		app.processEvents();
	}
	mode->setCurrentIndex(savedMode);
	static_cast<void>(profile);
}

QToolButton *focusedButton(QToolBar *bar)
{
	auto *button = qobject_cast<QToolButton *>(QApplication::focusWidget());
	return button && button->parentWidget() == bar ? button : nullptr;
}

// The tool shelf is one Tab stop: arrows, Home and End move between its
// buttons and Space presses one, even where the profile gives Space to a
// command (Blender's Play). The document commands are the Model menu's.
void checkToolBars(ModelEditorDialog *editor, const std::string &name)
{
	auto *shelf = editor->findChild<QToolBar *>(QStringLiteral("meshToolShelf"));
	auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *proportional = editor->findChild<QToolButton *>(QStringLiteral("meshProportional"));
	auto *model = editor->findChild<QMenu *>(QStringLiteral("meshMenuModel"));
	auto *menus = editor->findChild<QMenuBar *>(QStringLiteral("meshMenuBar"));
	if (!expect(shelf && preview && proportional && model && menus, name + ": the editor has its tool shelf and Model menu"))
		return;
	// The Model menu leads the menu bar, with a mnemonic, and holds the
	// document commands; Undo and Redo lead the editing header.
	QStringList commands;
	for (auto *entry : model->actions())
		commands << entry->objectName();
	expect(menus->actions().value(0) == model->menuAction() && model->title().contains(QLatin1Char('&')) &&
			   commands.contains(QStringLiteral("openMesh")) && commands.contains(QStringLiteral("saveMesh")) &&
			   commands.contains(QStringLiteral("exportMeshMd3")) && commands.contains(QStringLiteral("stageMesh")) &&
			   commands.contains(QStringLiteral("meshRecoveryEnabled")),
		   name + ": the Model menu leads the menu bar with the document commands");
	// Command search finds every Model menu command, the recovery preference
	// included, as the manual says.
	QStringList unsearchable;
	const auto searchable = editor->tools()->searchableActions();
	for (auto *entry : model->actions())
	{
		const auto entries = entry->menu() ? entry->menu()->actions() : QList<QAction *>{entry};
		for (auto *item : entries)
		{
			if (!item->isSeparator() && !item->text().isEmpty() && !searchable.contains(item))
				unsearchable << item->objectName();
		}
	}
	expect(unsearchable.isEmpty(), name + ": command search finds every Model menu command" +
									   (unsearchable.isEmpty() ? std::string() : ": " + std::string(unsearchable.join(QLatin1Char(' ')).toUtf8().constData())));
	auto *header = editor->findChild<QToolBar *>(QStringLiteral("meshToolHeader"));
	expect(header && header->actions().value(0) == editor->findChild<QAction *>(QStringLiteral("undoMesh")) &&
			   header->actions().value(1) == editor->findChild<QAction *>(QStringLiteral("redoMesh")),
		   name + ": Undo and Redo lead the editing header");
	const auto reach = [&](QToolBar *bar)
	{
		for (int step = 0; step < 14 && !focusedButton(bar); ++step)
			pressFocus(true);
		return focusedButton(bar);
	};
	preview->setFocus(Qt::OtherFocusReason);
	auto *first = reach(shelf);
	if (!expect(first, name + ": Ctrl+Tab reaches the tool shelf"))
		return;
	int stops = 0;
	for (auto *button : shelf->findChildren<QToolButton *>())
		stops += (button->focusPolicy() & Qt::TabFocus) == Qt::TabFocus;
	expect(stops == 1, name + ": the shelf is one Tab stop");
	press(Qt::Key_Down);
	auto *second = focusedButton(shelf);
	expect(second && second != first, name + ": Down moves to the next tool");
	press(Qt::Key_End);
	auto *last = focusedButton(shelf);
	expect(last && last->defaultAction() == editor->tools()->action(QStringLiteral("meshToggleProportional")), name + ": End moves to the last tool");
	const bool on = proportional->isChecked();
	press(Qt::Key_Space);
	expect(proportional->isChecked() != on && focusedButton(shelf) == last, name + ": Space presses the focused tool, not a profile command");
	press(Qt::Key_Return);
	expect(proportional->isChecked() == on, name + ": Enter presses it too");
	press(Qt::Key_Home);
	expect(focusedButton(shelf) == first, name + ": Home moves to the first tool");
	press(Qt::Key_Up);
	expect(focusedButton(shelf) == last, name + ": Up from the first tool wraps to the last");
	press(Qt::Key_Tab);
	expect(!inside(shelf, QApplication::focusWidget()), name + ": Tab leaves the shelf in one step");
	preview->setFocus(Qt::OtherFocusReason);
}

// Ctrl+Tab moves between panels, so the 3D and UV tabs switch from their tab
// bar: Tab reaches it, and the arrow keys change the tab.
void checkViewTabs(ModelEditorDialog *editor, const std::string &name)
{
	auto *tabs = editor->findChild<QTabWidget *>(QStringLiteral("meshViewTabs"));
	auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	if (!expect(tabs && tabs->tabBar(), name + ": the views have a tab bar"))
		return;
	preview->setFocus(Qt::OtherFocusReason);
	until([&] { return QApplication::focusWidget() == preview; });
	// Tab goes through the component table in one step: it moves between rows
	// with the arrow keys, and its Tab would otherwise cycle every cell.
	auto *table = editor->findChild<QTableView *>(QStringLiteral("meshComponents"));
	editor->showSidebarPage(QStringLiteral("outliner"));
	if (expect(table && !table->tabKeyNavigation(), name + ": the component table leaves Tab to focus"))
	{
		table->setFocus(Qt::TabFocusReason);
		until([&] { return table->hasFocus(); });
		press(Qt::Key_Tab);
		expect(QApplication::focusWidget() != table, name + ": Tab leaves the component table");
		preview->setFocus(Qt::OtherFocusReason);
		until([&] { return QApplication::focusWidget() == preview; });
	}
	for (int step = 0; step < 300 && QApplication::focusWidget() != tabs->tabBar(); ++step)
		press(Qt::Key_Tab);
	if (!expect(QApplication::focusWidget() == tabs->tabBar(), name + ": Tab reaches the 3D and UV tab bar"))
		return;
	const int page = tabs->currentIndex();
	const bool rtl = tabs->tabBar()->layoutDirection() == Qt::RightToLeft;
	press(rtl ? Qt::Key_Left : Qt::Key_Right);
	expect(tabs->currentIndex() != page, name + ": an arrow key on the tab bar switches the view tab");
	tabs->setCurrentIndex(page);
	QApplication::processEvents();
	preview->setFocus(Qt::OtherFocusReason);
}

// A setting in Adjust Last Operation keeps the keys it uses: Space toggles a
// check box instead of Blender's Play, and a letter runs no mesh command.
void checkSettingKeys(QApplication &app, ModelEditorDialog *editor, const std::string &name)
{
	auto *tools = editor->tools();
	auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *mode = editor->findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
	const int savedMode = mode->currentIndex();
	mode->setCurrentIndex(0);
	tools->applySelection({0, {}, {0, 1}});
	auto inset = tools->toolEdit(ModelEditKind::InsetFaces);
	inset.tool.insetThickness = 2;
	QString error;
	if (!expect(tools->runTool(inset, &error), name + ": an inset runs"))
	{
		mode->setCurrentIndex(savedMode);
		return;
	}
	const auto revision = editor->document().revisionFingerprint();
	preview->setFocus(Qt::OtherFocusReason);
	until([&] { return QApplication::focusWidget() == preview; });
	press(Qt::Key_F9);
	auto *panel = tools->lastOperationPanel();
	const auto box = [&]() -> QCheckBox *
	{
		for (auto *candidate : panel->findChildren<QCheckBox *>())
		{
			if (candidate->accessibleName() == QStringLiteral("Individual"))
				return candidate;
		}
		return nullptr;
	};
	if (expect(panel->isVisible() && box(), name + ": the inset's settings show Individual"))
	{
		// Tab through the settings to the check box, as a keyboard user does.
		for (int step = 0; step < 6 && QApplication::focusWidget() != box(); ++step)
			press(Qt::Key_Tab);
		expect(QApplication::focusWidget() == box(), name + ": Tab reaches the Individual check box");
		const bool checked = box()->isChecked();
		press(Qt::Key_Space);
		app.processEvents();
		expect(until([&] { return box() && box()->isChecked() != checked; }) && editor->document().revisionFingerprint() != revision,
			   name + ": Space toggles the check box and replays the inset");
		expect(QApplication::focusWidget() == box(), name + ": focus stays on the check box");
		press(Qt::Key_G);
		expect(tools->modalOperator() == ModelModalOperator::None && QApplication::focusWidget() == box(),
			   name + ": G in a setting starts no move and leaves focus there");
		// Arrows move between check boxes, as Qt's buttons do, and stay in the panel.
		press(Qt::Key_Down);
		expect(inside(panel, QApplication::focusWidget()), name + ": an arrow in a setting stays in the panel");
		box()->setFocus(Qt::OtherFocusReason);
		press(Qt::Key_Escape);
		expect(QApplication::focusWidget() == preview, name + ": Esc returns to the view");
	}
	press(Qt::Key_F9);
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	mode->setCurrentIndex(savedMode);
}

// Where 3ds Max gives Adjust Last Operation and Search Commands no key, or
// another one, no visible or spoken text names F9 or F3 for them.
void checkUnkeyedHints(QApplication &app, ModelEditorDialog *editor, const std::string &pass)
{
	auto *tools = editor->tools();
	auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *mode = editor->findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
	editor->applyProfile(QStringLiteral("3ds-max"), false);
	app.processEvents();
	const std::string name = pass + " 3ds-max";
	auto *adjust = tools->action(QStringLiteral("meshAdjustLast"));
	if (!expect(adjust && adjust->shortcuts().isEmpty() && keys(tools->action(QStringLiteral("meshOperatorSearch"))) == QStringLiteral("X"),
				name + ": 3ds Max has no Adjust Last Operation key and searches with X"))
		return;
	adjust->trigger();
	app.processEvents();
	auto *hint = tools->lastOperationPanel()->findChild<QLabel *>(QStringLiteral("meshLastOperationHint"));
	expect(hint && hint->isVisible() && !hint->text().contains(native("F9")) && hint->text().contains(QStringLiteral("View > Adjust Last Operation")),
		   name + ": the empty Adjust Last Operation panel points to the View menu, not F9");
	adjust->trigger();
	app.processEvents();
	const int savedMode = mode->currentIndex();
	mode->setCurrentIndex(0);
	tools->applySelection({0, {}, {0, 1}});
	const QPointF centre(preview->width() / 2.0, preview->height() / 2.0);
	if (expect(tools->beginModal(ModelModalOperator::Spin, centre, false), name + ": Spin starts"))
	{
		const auto text = tools->modalText();
		expect(!text.contains(native("F9")) && text.contains(QStringLiteral("View > Adjust Last Operation")),
			   name + ": the Spin banner names the View menu, not F9");
		tools->finishModal(false);
	}
	mode->setCurrentIndex(2);
	tools->applySelection({0, {}, {}, {modelEdge(1, 4)}});
	if (expect(tools->beginModal(ModelModalOperator::BevelEdges, centre, false), name + ": Bevel Edges starts"))
	{
		const auto text = tools->modalText();
		expect(!text.contains(native("F9")) && text.contains(QStringLiteral("View > Adjust Last Operation")),
			   name + ": the Bevel Edges banner names the View menu, not F9");
		tools->finishModal(false);
	}
	mode->setCurrentIndex(savedMode);
	// The Blender navigation toggle describes this profile's keys.
	auto *navigation = tools->action(QStringLiteral("meshBlenderNavigation"));
	navigation->trigger();
	app.processEvents();
	const auto description = preview->accessibleDescription();
	expect(navigation->isChecked() && description.contains(QStringLiteral("Blender navigation:")) && !description.contains(QStringLiteral("F3")) &&
			   description.contains(QStringLiteral("X searches every command")) && !description.contains(QStringLiteral("G, R and S")),
		   name + ": Blender navigation names 3ds Max's search key and no Blender transform keys");
	editor->applyProfile(QStringLiteral("3ds-max"), false);
	app.processEvents();
}

bool run(QApplication &app, int variant, const QString &settingsPath)
{
	StudioSettings::setOverrideFilePath(settingsPath);
	applyStudioTheme(app, studioThemeTokens(variant == 0 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard,
										   variant == 0 ? 100 : 200));
	app.setLayoutDirection(variant == 0 ? Qt::LeftToRight : Qt::RightToLeft);
	const std::string pass = variant == 0 ? "100% LTR" : "200% RTL";
	auto *editor = new ModelEditorDialog;
	editor->resize(variant == 0 ? 1500 : 2200, variant == 0 ? 950 : 1400);
	editor->show();
	editor->activateWindow();
	if (!expect(until([&] { return QApplication::activeWindow() == editor; }), pass + ": the editor is the active window"))
		return false;
	QString error;
	if (!expect(editor->setMesh(gridMesh(), &error), pass + ": load a grid"))
		std::cerr << "  " << error.toStdString() << '\n';
	auto *tools = editor->tools();
	auto *preview = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *area = editor->findChild<QWidget *>(QStringLiteral("meshModellingArea"));
	auto *mode = editor->findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
	auto *snap = editor->findChild<QCheckBox *>(QStringLiteral("meshSnapTranslation"));
	if (!expect(tools && preview && area && mode && snap, pass + ": the editor has its view, modelling area and mode controls"))
		return false;
	const auto focusView = [&]
	{
		preview->setFocus(Qt::OtherFocusReason);
		return until([&] { return QApplication::focusWidget() == preview; });
	};
	// Before any tool runs, while Adjust Last Operation is still empty.
	checkUnkeyedHints(app, editor, pass);
	for (const QString &profile : {QStringLiteral("studio"), QStringLiteral("blender"), QStringLiteral("3ds-max"), QStringLiteral("milkshape-3d")})
	{
		const std::string name = pass + " " + profile.toStdString();
		editor->applyProfile(profile, false);
		app.processEvents();
		const bool tabCommand = profile == QStringLiteral("studio") || profile == QStringLiteral("blender");
		checkKeyHints(editor, profile, name);
		expect(preview->accessibleDescription().contains(nativeFocus(true)) && preview->accessibleDescription().contains(nativeFocus(false)),
			   name + ": the view's description names the way out");
		expect(preview->accessibleDescription().contains(native("F9")) == tabCommand,
			   name + ": the description names F9 only where the profile gives it to Adjust Last Operation");
		if (!expect(focusView(), name + ": the view takes keyboard focus"))
			continue;

		// Ctrl+Tab always leaves the view, and Ctrl+Shift+Tab comes back.
		const int startMode = mode->currentIndex();
		pressFocus(true);
		QWidget *next = QApplication::focusWidget();
		expect(next && editor->isAncestorOf(next) && !inside(area, next) && mode->currentIndex() == startMode,
			   name + ": Ctrl+Tab moves focus out of the modelling area without editing");
		pressFocus(false);
		expect(QApplication::focusWidget() == preview, name + ": Ctrl+Shift+Tab returns to the view");
		// Ctrl+Tab visits each panel in turn and comes round to the view again.
		QList<int> visited{panelOf(tools, preview)};
		bool cycled = false;
		for (int step = 0; step < 12 && !cycled; ++step)
		{
			pressFocus(true);
			const int panel = panelOf(tools, QApplication::focusWidget());
			expect(panel != visited.last(), name + ": each Ctrl+Tab reaches another panel");
			cycled = QApplication::focusWidget() == preview;
			visited << panel;
		}
		expect(cycled && visited.size() >= 4, name + ": Ctrl+Tab goes through the panels and back to the view");
		// Each shown panel has a control to land on: the document actions and
		// the tool shelf are tool bars, whose buttons Qt gives no focus.
		const auto panels = tools->focusPanels();
		for (int index = 0; index < panels.size(); ++index)
		{
			if (panels[index]->isVisible())
				expect(visited.contains(index), name + ": Ctrl+Tab lands in panel " + panels[index]->objectName().toStdString());
		}
		// MilkShape 3D keeps every page on one side: its empty leading sidebar
		// is hidden and the cycle starts at the editing header.
		expect((visited.contains(0) || !panels.value(0)->isVisible()) && panels.value(0) == static_cast<QWidget *>(editor->leadingSidebar()) &&
				   panels.contains(editor->findChild<QToolBar *>(QStringLiteral("meshToolShelf"))),
			   name + ": the cycle starts at the leading sidebar and includes the tool shelf");

		// Tab in the view: a command where the profile binds it, else focus.
		expect(focusView(), name + ": back in the view");
		press(Qt::Key_Tab);
		if (tabCommand)
		{
			expect(QApplication::focusWidget() == preview && mode->currentIndex() != startMode, name + ": Tab in the view toggles object mode");
			press(Qt::Key_Tab);
			expect(QApplication::focusWidget() == preview && mode->currentIndex() == startMode, name + ": Tab again returns to the edit mode");
			const bool snapped = snap->isChecked();
			press(Qt::Key_Backtab, Qt::ShiftModifier);
			expect(QApplication::focusWidget() == preview && snap->isChecked() != snapped, name + ": Shift+Tab in the view toggles snapping");
			press(Qt::Key_Backtab, Qt::ShiftModifier);
		}
		else
		{
			expect(QApplication::focusWidget() != preview && mode->currentIndex() == startMode, name + ": with no Tab command, Tab moves focus on");
		}

		// Adjust Last Operation: F9 in, Tab between fields, Esc out.
		tools->applySelection({0, {0, 1}, {}});
		auto edit = tools->toolEdit(ModelEditKind::Transform);
		edit.translation = {1, 0, 0};
		expect(tools->runTool(edit, &error), name + ": a move runs");
		expect(focusView(), name + ": the view has focus before F9");
		if (tabCommand)
			press(Qt::Key_F9);
		else
			tools->action(QStringLiteral("meshAdjustLast"))->trigger();
		app.processEvents();
		auto *panel = tools->lastOperationPanel();
		QWidget *field = QApplication::focusWidget();
		expect(panel->isVisible() && field != panel && inside(panel, field), name + ": F9 moves focus into Adjust Last Operation");
		const int modeBefore = mode->currentIndex();
		const bool snapBefore = snap->isChecked();
		press(Qt::Key_Tab);
		expect(QApplication::focusWidget() != field && inside(panel, QApplication::focusWidget()) && mode->currentIndex() == modeBefore,
			   name + ": Tab moves to the next setting and runs no command");
		press(Qt::Key_Backtab, Qt::ShiftModifier);
		expect(QApplication::focusWidget() == field && snap->isChecked() == snapBefore, name + ": Shift+Tab moves back without toggling snapping");
		pressFocus(true);
		expect(!inside(area, QApplication::focusWidget()), name + ": Ctrl+Tab also leaves from the panel");
		pressFocus(false);
		expect(QApplication::focusWidget() == preview, name + ": Ctrl+Shift+Tab comes back to the view");
		if (tabCommand)
		{
			press(Qt::Key_F9);
			press(Qt::Key_F9);
		}
		else
		{
			tools->action(QStringLiteral("meshAdjustLast"))->trigger();
			tools->action(QStringLiteral("meshAdjustLast"))->trigger();
		}
		app.processEvents();
		expect(panel->isVisible() && inside(panel, QApplication::focusWidget()), name + ": F9 shows the panel again with focus in it");
		press(Qt::Key_Escape);
		expect(QApplication::focusWidget() == preview && panel->isVisible() && editor->isVisible(), name + ": Esc in the panel returns focus to the view");
		if (tabCommand)
			press(Qt::Key_F9);
		else
			tools->action(QStringLiteral("meshAdjustLast"))->trigger();
		expect(!panel->isVisible() && QApplication::focusWidget() == preview, name + ": F9 from the view hides the panel");
		editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
		if (profile == QStringLiteral("blender"))
		{
			checkToolBars(editor, name);
			checkSettingKeys(app, editor, name);
		}
		if (profile == QStringLiteral("3ds-max"))
			checkViewTabs(editor, name);
		checkKeyboardRoutes(app, editor, profile, name);
	}

	// An imported controls file: the hints follow its keys, and a focus key
	// it gives a command is skipped with a word in the status line.
	const QString controlsFile = QDir(QFileInfo(settingsPath).path()).filePath(QStringLiteral("blender-controls-%1.json").arg(variant));
	{
		QFile file(controlsFile);
		expect(file.open(QIODevice::WriteOnly) &&
				   file.write(modelEditorControlsFile(
					   QStringLiteral("blender"),
					   QJsonObject{{QStringLiteral("keys"), QJsonObject{{QStringLiteral("meshModeFace"), QJsonArray{QStringLiteral("Ctrl+J")}},
																	   {QStringLiteral("meshHide"), QJsonArray{QKeySequence(focusKey(true)).toString(QKeySequence::PortableText)}}}}})) > 0,
			   pass + ": write a Blender controls file");
	}
	expect(editor->importControlsFile(controlsFile, &error) && editor->profileId() == QStringLiteral("blender"), pass + ": import the controls file");
	app.processEvents();
	auto *face = editor->findChild<QToolButton *>(QStringLiteral("meshSelectMode2"));
	expect(face->toolTip().contains(native("Ctrl+J")) && !face->toolTip().contains(QStringLiteral("(3)")), pass + ": the face hint follows an override");
	const QString imported = editor->findChild<QLabel *>(QStringLiteral("meshStatus"))->text();
	expect(!tools->action(QStringLiteral("meshHide"))->shortcuts().contains(QKeySequence(focusKey(true))) &&
			   tools->action(QStringLiteral("meshHide"))->shortcut() == QKeySequence(Qt::Key_H) && imported.contains(nativeFocus(true)) &&
			   imported.contains(QStringLiteral("Hide Selected")) && !imported.contains(QStringLiteral("meshHide")) &&
			   imported.contains(QStringLiteral("Blender")) && !imported.contains(QStringLiteral("blender ")),
		   pass + ": an override that takes Ctrl+Tab is skipped and says so, naming the command and profile as the user sees them");
	expect(focusView(), pass + ": the view takes focus under the override");
	pressFocus(true);
	expect(!inside(area, QApplication::focusWidget()), pass + ": Ctrl+Tab still leaves the view");
	expect(StudioSettings().setModelEditorControlOverrides(QStringLiteral("blender"), {}, &error), pass + ": clear the overrides");

	// Customise Controls: the key field never holds focus.
	editor->applyProfile(QStringLiteral("milkshape-3d"), false);
	{
		ModelControlsDialog dialog(editor->controls(), editor);
		dialog.show();
		dialog.activateWindow();
		expect(until([&] { return QApplication::activeWindow() == &dialog; }), pass + ": Customise Controls is active");
		auto *keyEdit = dialog.findChild<QKeySequenceEdit *>(QStringLiteral("modelControlsKeyEdit"));
		auto *special = dialog.findChild<QComboBox *>(QStringLiteral("modelControlsSpecialKey"));
		auto *assign = dialog.findChild<QPushButton *>(QStringLiteral("modelControlsAssign"));
		auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("modelControlsKeys"));
		auto *problems = dialog.findChild<QLabel *>(QStringLiteral("modelControlsProblems"));
		if (expect(keyEdit && special && assign && table && problems, pass + ": the key controls exist"))
		{
			for (int row = 0; row < table->rowCount(); ++row)
			{
				if (table->item(row, 1)->data(Qt::UserRole).toString() == QStringLiteral("meshToggleEditMode"))
					table->setCurrentCell(row, 1);
			}
			table->setFocus(Qt::TabFocusReason);
			until([&] { return table->hasFocus(); });
			press(Qt::Key_Tab);
			expect(QApplication::focusWidget() != table && table->currentRow() >= 0 &&
					   table->item(table->currentRow(), 1)->data(Qt::UserRole).toString() == QStringLiteral("meshToggleEditMode"),
				   pass + ": Tab leaves the command table and keeps the chosen command");
			keyEdit->setFocus(Qt::OtherFocusReason);
			expect(until([&] { return keyEdit->hasFocus(); }), pass + ": the key field takes focus");
			press(Qt::Key_Tab);
			expect(QApplication::focusWidget() == special && keyEdit->keySequence().isEmpty(), pass + ": Tab leaves the key field without recording it");
			press(Qt::Key_Backtab, Qt::ShiftModifier);
			expect(QApplication::focusWidget() == keyEdit && keyEdit->keySequence().isEmpty(), pass + ": Shift+Tab comes back without recording it");
			press(Qt::Key_Escape);
			expect(keyEdit->keySequence().isEmpty() && dialog.isVisible(), pass + ": Esc stops recording without a key");
			special->setCurrentIndex(special->findData(QStringLiteral("Tab")));
			expect(keyEdit->keySequence() == QKeySequence(Qt::Key_Tab), pass + ": Special key puts Tab in the key field");
			assign->click();
			expect(modelEditorCommandKeys(dialog.controls(), QStringLiteral("meshToggleEditMode")) == QStringList{QStringLiteral("Tab")},
				   pass + ": Assign binds Tab deliberately");
			keyEdit->setFocus(Qt::OtherFocusReason);
			until([&] { return keyEdit->hasFocus(); });
			pressFocus(true);
			expect(keyEdit->keySequence() == QKeySequence(focusKey(true)), pass + ": the key field records Ctrl+Tab");
			assign->click();
			expect(modelEditorCommandKeys(dialog.controls(), QStringLiteral("meshToggleEditMode")) == QStringList{QStringLiteral("Tab")} &&
					   problems->text().contains(nativeFocus(true)),
				   pass + ": Assign refuses Ctrl+Tab and says why");
		}
		dialog.reject();
	}

	// Closing could ask whether to save, which waits forever offscreen.
	delete editor;
	app.setLayoutDirection(Qt::LeftToRight);
	return true;
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("mesh-keyboard-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	for (int variant = 0; variant < 2; ++variant)
	{
		if (!run(app, variant, QDir(temporary.path()).filePath(QStringLiteral("settings-%1.ini").arg(variant))))
			++failures;
	}
	if (failures)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "Model editor keyboard UI smoke passed.\n";
	return EXIT_SUCCESS;
}
