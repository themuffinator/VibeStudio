// Offscreen checks of TrenchBroom's generated Create <class> commands on the
// Levels page, under the NetRadiant Custom, TrenchBroom and VibeStudio
// profiles at 100% and at 200% text with the high-visibility theme, an
// expanding translator and right-to-left layout:
// - each point and brush class of the map's definitions has a Create
//   command in the shared registry and the command palette, worldspawn
//   none; point classes are enabled with a Quake map, brush classes only
//   with brushes selected;
// - Create light places the light at the plan view's pointer on the grid,
//   and in the camera at its pointer, Create func_wall makes the selected
//   brush a wall, each one undo step back to the exact bytes, redo again;
// - Create Entity… types a class in its picker and places it at the pointer
//   the view had when the picker opened;
// - a key the user gave Create light is installed, works in the plan view
//   (with Qt Test), and is kept while a reload of the definitions takes the
//   class away and brings it back;
// - a locked wall refuses a brush class with a reason;
// - the registry keeps the keys of unregistered Create commands.
// Owned offscreen widgets, settings in a temporary folder, semantic events.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_theme.h"
#include "core/level_entity_create.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"
#include "tests/level_entity_ownership_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFocusEvent>
#include <QFont>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}

bool until(const std::function<bool()>& ready, int milliseconds = 30000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < milliseconds) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
}

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

const LevelMapEntity* newest(const LevelMapDocument& document, const QString& className)
{
	const LevelMapEntity* found = nullptr;
	for (const LevelMapEntity& value : document.entities) {
		if (value.className == className && (!found || value.id > found->id)) {
			found = &value;
		}
	}
	return found;
}

int countOf(const LevelMapDocument& document, const QString& className)
{
	int count = 0;
	for (const LevelMapEntity& value : document.entities) {
		count += value.className == className ? 1 : 0;
	}
	return count;
}

QString origin(const LevelMapEntity* entity)
{
	return entity ? QStringLiteral("%1 %2 %3").arg(entity->origin.x).arg(entity->origin.y).arg(entity->origin.z) : QStringLiteral("none");
}

// The registry keeps a key given to a Create command while the command is
// not registered, and gives it back when it is.
void registryKeepsKeys()
{
	phase = QStringLiteral("registry");
	QWidget host;
	StudioCommandRegistry registry(&host);
	registry.setUserShortcuts({{QStringLiteral("map.createEntity.light"), {QStringLiteral("Ctrl+Alt+L")}},
		{QStringLiteral("map.notACommand"), {QStringLiteral("Ctrl+Alt+U")}}});
	QHash<QString, QStringList> keys = registry.userShortcuts();
	expect(keys.size() == 1 && keys.value(QStringLiteral("map.create-entity.light")) == QStringList {QStringLiteral("Ctrl+Alt+L")},
		"an unregistered Create command keeps its key", QStringList(keys.keys()).join(QLatin1Char(',')));
	StudioCommandRegistration command;
	command.commandId = QStringLiteral("map.createEntity.light");
	command.label = QStringLiteral("Create light");
	QAction* action = registry.registerCommand(command);
	expect(action && action->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("Ctrl+Alt+L"))}, "the key is installed on registration");
	keys = registry.userShortcuts();
	expect(keys.value(QStringLiteral("map.createEntity.light")) == QStringList {QStringLiteral("Ctrl+Alt+L")}, "keyed by the registered id while registered");
	QPointer<QAction> guarded(action);
	registry.unregisterCommand(command.commandId);
	expect(!registry.action(command.commandId) && !registry.commandIds().contains(command.commandId), "unregistered");
	expect(until([&] { return guarded.isNull(); }, 2000), "its action goes once the event loop runs");
	expect(registry.userShortcuts().value(QStringLiteral("map.create-entity.light")) == QStringList {QStringLiteral("Ctrl+Alt+L")}, "the key outlives it");
	registry.setUserShortcuts({});
	expect(registry.userShortcuts().isEmpty(), "resetting every key forgets it too");
}

} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic events only; no system input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	registryKeepsKeys();
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	// A key of the user's own for Create light, as Keyboard Shortcuts saves it.
	settings.setUserShortcuts({{QStringLiteral("map.createEntity.light"), {QStringLiteral("Ctrl+Alt+L")}}});
	const QString path = temp.filePath(QStringLiteral("create.map"));
	const QByteArray source = tests::entityOwnershipFixtureBytes();
	if (!expect(!source.isEmpty() && writeFile(path, source), "fixture")) {
		return 1;
	}
	// The same map with its func_wall in a locked layer.
	LevelMapDocument lockedMap;
	QString error;
	QString layer;
	LevelMapLoadRequest request;
	request.path = temp.filePath(QStringLiteral("locked.map"));
	request.engineHint = QStringLiteral("idtech2");
	const bool lockedBuilt = loadLevelMapBytes(request, source, &lockedMap, &error)
		&& createLevelSceneNode(&lockedMap, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
		&& assignLevelSceneObjects(&lockedMap, layer, {QStringLiteral("entity:%1").arg(tests::entityOwnershipEntity(lockedMap, QStringLiteral("func_wall")))}, &error)
		&& setLevelSceneLocked(&lockedMap, layer, true, &error);
	const QString lockedPath = request.path;
	if (!expect(lockedBuilt && writeFile(lockedPath, serializeLevelMap(lockedMap).bytes), "locked fixture", error)) {
		return 1;
	}
	// Definitions with a lamp and no light, to reload in one pass.
	const QString fgd = temp.filePath(QStringLiteral("lamps.fgd"));
	writeFile(fgd, QByteArrayLiteral("@SolidClass = worldspawn : \"World\" []\n@PointClass size(-4 -4 -4, 4 4 4) = misc_lamp : \"Lamp\" []\n"
									 "@SolidClass = func_wall : \"Wall\" []\n"));
	const QStringList ids {QStringLiteral("map.createEntity.light"), QStringLiteral("map.createEntity.info_player_start"), QStringLiteral("map.createEntity.func_wall"),
		QStringLiteral("map.createEntity.func_door")};
	for (const char* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (const int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Entity create UI " << phase.toStdString() << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(1600, 1000);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(view && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->hasMesh() && action(ids.first()); }), "map ready")) {
					return 1;
				}
				const bool first = QLatin1String(profile) == QLatin1String("netradiant-custom") && scale == 100;
				// Every class of the definitions has a command; worldspawn none.
				for (const QString& id : ids) {
					QAction* command = action(id);
					CommandPaletteEntry entry;
					if (expect(command, "generated command", id)) {
						QString className;
						levelEntityCreateClassForCommand(id, &className);
						expect(command->text().contains(className) && !command->statusTip().isEmpty() && !command->toolTip().isEmpty()
								&& commandPaletteEntryForCommandId(id, &entry) && entry.category == (scale == 200 ? QStringLiteral("[Levels~~]") : QStringLiteral("Levels")),
							"labelled, described and in the registry", command->text() + QStringLiteral(" / ") + entry.category);
					}
				}
				expect(!action(QStringLiteral("map.createEntity.worldspawn")) && action(QStringLiteral("map.createEntity")), "no Create worldspawn; Create Entity is there");
				auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-map.createEntity"));
				expect(row && !row->accessibleName().isEmpty() && row->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(row),
					"Create Entity has an accessible Tools row");
				// The command palette finds a class by name.
				action(QStringLiteral("shell.commandPalette"))->trigger();
				auto* palette = shell.findChild<QDialog*>(QStringLiteral("commandPalette"));
				if (expect(palette && until([&] { return palette->isVisible(); }), "palette opens")) {
					auto* filter = palette->findChild<QLineEdit*>();
					auto* list = palette->findChild<QListWidget*>();
					if (expect(filter && list, "palette filter and list")) {
						filter->setText(QStringLiteral("create info_player_start"));
						bool found = false;
						for (int index = 0; index < list->count(); ++index) {
							found = found || list->item(index)->text().contains(QStringLiteral("info_player_start"));
						}
						expect(found, "the palette lists Create info_player_start");
					}
					palette->reject();
				}
				// The user's key is installed.
				expect(action(ids.first())->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("Ctrl+Alt+L"))}, "the user's key for Create light",
					action(ids.first())->shortcut().toString());

				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const LevelMapDocument& document = shell.levelDocument();
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) {
					view->setSelectionSet(refs);
					view->selectionSetChanged(refs);
				};
				const auto focusPlan = [&] {
					QFocusEvent focus(QEvent::FocusIn, Qt::OtherFocusReason);
					QApplication::sendEvent(view, &focus);
				};
				const int w1 = tests::entityOwnershipBrush(document, QStringLiteral("w1"));
				pick({});
				focusPlan();
				expect(action(ids.first())->isEnabled() && !action(QStringLiteral("map.createEntity.func_wall"))->isEnabled(),
					"point classes enabled, brush classes waiting for brushes");
				pick({{LevelMapSelectionKind::QuakeBrush, w1}});
				expect(action(QStringLiteral("map.createEntity.func_wall"))->isEnabled(), "a selected brush enables brush classes");
				pick({});
				// Selecting can be an undo step of its own (TrenchBroom), so each
				// command is measured from just before it runs.
				QByteArray baseline;
				qsizetype depth = 0;
				const auto mark = [&] {
					baseline = bytes();
					depth = shell.levelDocument().undoStack.size();
				};
				const auto undoBack = [&](const char* label) {
					action(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == baseline && shell.levelDocument().undoStack.size() == depth, label, shell.statusBar()->currentMessage());
				};
				mark();

				// Create light at the plan view's pointer: the box corner on the
				// grid, standing on the lowest brush depth.
				const bool top = view->projection() == MapViewportProjection::TopXY;
				// A point near the middle of the view, a quarter unit off whole
				// units so no power-of-two grid rounds it from exactly halfway.
				const LevelMapVec3 middle = view->worldPositionAt(QPointF(view->width() * 0.5, view->height() * 0.5), 0.0, false);
				const double pointerX = std::floor(middle.x) + 0.25;
				const double pointerY = std::floor(middle.y) + 0.25;
				const QPointF pointer = view->viewPointFor(pointerX, pointerY);
				expect(QRectF(view->rect()).contains(pointer), "the pointer is inside the plan view");
				view->rememberCommandPointer(pointer);
				const double grid = view->snapToGrid() ? view->gridSize() : 0.0;
				const auto snapped = [grid](double value) { return grid > 0 ? std::round(value / grid) * grid : value; };
				action(ids.first())->trigger();
				const LevelMapEntity* light = newest(shell.levelDocument(), QStringLiteral("light"));
				expect(countOf(shell.levelDocument(), QStringLiteral("light")) == 2 && shell.levelDocument().undoStack.size() == depth + 1
						&& light && shell.levelDocument().selection == QVector<LevelMapSelectionRef> {{LevelMapSelectionKind::Entity, light->id}},
					"Create light adds a selected light in one step", shell.statusBar()->currentMessage());
				if (top && light) {
					expect(std::abs(light->origin.x - (snapped(pointerX) + 8)) < 1e-6 && std::abs(light->origin.y - (snapped(pointerY) + 8)) < 1e-6
							&& std::abs(light->origin.z - 8) < 1e-6,
						"the light stands at the pointer on the grid", origin(light) + QStringLiteral(" grid %1").arg(grid));
				}
				const QByteArray created = bytes();
				undoBack("undo removes the light");
				action(QStringLiteral("map.redo"))->trigger();
				expect(bytes() == created, "redo puts it back");
				undoBack("undo once more");

				// Create func_wall makes the selected world brush a wall.
				pick({{LevelMapSelectionKind::QuakeBrush, w1}});
				mark();
				action(QStringLiteral("map.createEntity.func_wall"))->trigger();
				const LevelMapEntity* wall = newest(shell.levelDocument(), QStringLiteral("func_wall"));
				bool holds = false;
				for (const LevelMapBrush& value : shell.levelDocument().brushes) {
					holds = holds || (wall && value.entityId == wall->id && !value.faces.isEmpty() && value.faces.first().textureName == QStringLiteral("studio/w1"));
				}
				expect(countOf(shell.levelDocument(), QStringLiteral("func_wall")) == 2 && holds && shell.levelDocument().undoStack.size() == depth + 1,
					"Create func_wall makes the brush a wall in one step", shell.statusBar()->currentMessage());
				undoBack("undo gives the brush back to the world");
				pick({});
				mark();

				// Create Entity…: the picker places at the pointer the view had.
				// The Inspector may have opened beside the views since; aim at the
				// same map point again.
				view->rememberCommandPointer(view->viewPointFor(pointerX, pointerY));
				action(QStringLiteral("map.createEntity"))->trigger();
				auto* picker = shell.findChild<QDialog*>(QStringLiteral("levelEntityCreatePicker"));
				if (expect(picker && until([&] { return picker->isVisible(); }), "the picker opens")) {
					auto* filter = picker->findChild<QLineEdit*>(QStringLiteral("quickOpenFilter"));
					auto* list = picker->findChild<QListWidget*>(QStringLiteral("quickOpenList"));
					if (expect(filter && list && !list->accessibleName().isEmpty(), "picker filter and list")) {
						filter->setText(QStringLiteral("info_player_start"));
						const bool ready = until([&] {
							return list->count() > 0 && list->item(0)->data(Qt::UserRole + 4).toString() == QStringLiteral("info_player_start");
						});
						if (expect(ready, "the picker finds the class", list->count() > 0 ? list->item(0)->text() : QString())) {
							list->setCurrentRow(0);
							Q_EMIT list->itemActivated(list->item(0));
						}
					}
					const LevelMapEntity* start = newest(shell.levelDocument(), QStringLiteral("info_player_start"));
					expect(!picker->isVisible() && start && shell.levelDocument().undoStack.size() == depth + 1, "the chosen class is created",
						shell.statusBar()->currentMessage());
					if (top && start) {
						expect(std::abs(start->origin.x - (snapped(pointerX) + 16)) < 1e-6 && std::abs(start->origin.y - (snapped(pointerY) + 16)) < 1e-6
								&& std::abs(start->origin.z - 24) < 1e-6,
							"at the pointer the view had", origin(start));
					}
					undoBack("undo the picked class");
				}

				if (first) {
					// In the camera, at its pointer: the ray from the middle of the view.
					QFocusEvent focus(QEvent::FocusIn, Qt::OtherFocusReason);
					QApplication::sendEvent(camera, &focus);
					// Each edit rebuilds the camera's picture; it takes no placement until then.
					until([&] { return camera->isEnabled() && !camera->isRendering(); });
					mark();
					action(ids.first())->trigger();
					expect(countOf(shell.levelDocument(), QStringLiteral("light")) == 2 && shell.levelDocument().undoStack.size() == depth + 1,
						"Create light at the camera's pointer", shell.statusBar()->currentMessage());
					if (shell.levelDocument().undoStack.size() > depth) {
						undoBack("undo the camera light");
					}
					focusPlan();
					mark();
#ifdef VIBESTUDIO_HAVE_QTEST
					// The user's key in the plan view creates a light.
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
					QTest::keyClick(view, Qt::Key_L, Qt::ControlModifier | Qt::AltModifier);
					until([&] { return shell.levelDocument().undoStack.size() > depth; }, 5000);
					expect(countOf(shell.levelDocument(), QStringLiteral("light")) == 2, "Ctrl+Alt+L creates a light", shell.statusBar()->currentMessage());
					if (shell.levelDocument().undoStack.size() > depth) {
						undoBack("undo the keyed light");
					}
#endif
					// A locked wall refuses a brush class, with the brush named.
					shell.openPathFromCommandLine(lockedPath);
					action(QStringLiteral("shell.mode.levels"))->trigger();
					if (expect(until([&] { return shell.levelDocument().sourcePath == lockedPath && !shell.levelDocument().scene.nodes.isEmpty(); }), "locked map ready")) {
						const int c1 = tests::entityOwnershipBrush(shell.levelDocument(), QStringLiteral("c1"));
						const int w1Locked = tests::entityOwnershipBrush(shell.levelDocument(), QStringLiteral("w1"));
						pick({{LevelMapSelectionKind::QuakeBrush, w1Locked}, {LevelMapSelectionKind::QuakeBrush, c1}});
						const QByteArray lockedBaseline = bytes();
						const auto lockedDepth = shell.levelDocument().undoStack.size();
						action(QStringLiteral("map.createEntity.func_door"))->trigger();
						expect(bytes() == lockedBaseline && shell.levelDocument().undoStack.size() == lockedDepth
								&& shell.statusBar()->currentMessage().contains(QStringLiteral("brush:%1").arg(c1)),
							"a locked wall brush refuses Create func_door with its name", shell.statusBar()->currentMessage());
						pick({});
					}
				}

				if (QLatin1String(profile) == QLatin1String("vibestudio-default") && scale == 100) {
					// Reloading the definitions replaces the commands: a lamp comes,
					// the light goes, and its key waits for it.
					QLineEdit* definitions = nullptr;
					for (auto* edit : shell.findChildren<QLineEdit*>()) {
						definitions = edit->accessibleName() == QStringLiteral("Entity definition path") ? edit : definitions;
					}
					if (expect(definitions, "entity definition path field")) {
						definitions->setText(fgd);
						Q_EMIT definitions->returnPressed();
						expect(until([&] { return action(QStringLiteral("map.createEntity.misc_lamp")) && !action(ids.first()); }), "the lamp's command replaces the light's");
						expect(action(QStringLiteral("map.createEntity.func_wall")) && !action(QStringLiteral("map.createEntity.info_player_start")), "the commands follow the definitions");
						definitions->clear();
						Q_EMIT definitions->returnPressed();
						expect(until([&] { return action(ids.first()) != nullptr; }), "the built-in light comes back");
						expect(action(ids.first()) && action(ids.first())->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("Ctrl+Alt+L"))},
							"with the user's key");
					}
				}
				pick({});
				shell.close();
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	expect(StudioSettings().userShortcuts().value(QStringLiteral("map.createEntity.light")) == QStringList {QStringLiteral("Ctrl+Alt+L")}
			|| StudioSettings().userShortcuts().value(QStringLiteral("map.create-entity.light")) == QStringList {QStringLiteral("Ctrl+Alt+L")},
		"the saved key survives the sessions");
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Entity create UI checks passed" : "Entity create UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
