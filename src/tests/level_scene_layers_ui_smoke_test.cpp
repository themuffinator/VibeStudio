// Offscreen checks of the layer commands in the Levels page: Expand Selection
// to Layers and Move to Layer, as a popup at the pointer and as a submenu of
// the view context menu. Each runs under the NetRadiant Custom, TrenchBroom
// and VibeStudio profiles at 100% and at 200% text with the high-visibility
// theme, an expanding translator and right-to-left layout. Expansion selects
// the whole layer with its closed groups and records a selection step only
// where the profile keeps selection history; a move is one undo step back to
// the exact bytes; NetRadiant Custom's keys reach the commands without taking
// Quit; tool rows are keyboard-reachable and accessible; and a locked layer's
// entry is greyed out with its reason.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "core/level_scene_layers.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"
#include "tests/level_scene_layers_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <functional>
#include <iostream>
#include <memory>

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

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
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

// A popup menu runs its own event loop; this answers the next one once it
// shows. `seen` reports whether it did.
std::shared_ptr<bool> whenMenuShows(std::function<void(QMenu*)> answer)
{
	auto seen = std::make_shared<bool>(false);
	auto poll = std::make_shared<std::function<void(int)>>();
	*poll = [seen, poll, answer](int attempts) {
		if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()); menu && menu->isVisible()) {
			*seen = true;
			answer(menu);
			return;
		}
		if (attempts > 0) {
			QTimer::singleShot(10, [poll, attempts]() { (*poll)(attempts - 1); });
		}
	};
	QTimer::singleShot(10, [poll]() { (*poll)(600); });
	return seen;
}

QAction* childAction(QMenu* menu, const QString& name)
{
	for (QAction* action : menu->actions()) {
		if (action->objectName() == name) {
			return action;
		}
	}
	return nullptr;
}

QSet<QString> keys(const QVector<LevelMapSelectionRef>& selection)
{
	QSet<QString> result;
	for (const auto& ref : selection) {
		result.insert(levelMapSelectionRefId(ref));
	}
	return result;
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
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary directory")) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QByteArray source = tests::sceneLayersFixtureBytes();
	const QString path = temp.filePath(QStringLiteral("layers.map"));
	if (!expect(!source.isEmpty() && writeFile(path, source), "fixture")) {
		return 1;
	}
	// The same map with its Lights layer locked.
	LevelMapDocument lockedMap;
	QString error;
	const bool lockedBuilt = loadLevelMap({path, {}, {}}, &lockedMap, &error)
		&& setLevelSceneLocked(&lockedMap, tests::sceneLayersNode(lockedMap, QStringLiteral("Lights")), true, &error);
	const QString lockedPath = temp.filePath(QStringLiteral("locked.map"));
	if (!expect(lockedBuilt && writeFile(lockedPath, serializeLevelMap(lockedMap).bytes), "locked fixture", error)) {
		return 1;
	}
	for (const char* id : {"map.expand-selection-to-layers", "map.move-to-layer"}) {
		phase = QString::fromLatin1(id);
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id)), "palette entry");
	}
	const QString expandId = QStringLiteral("map.expandSelectionToLayers");
	const QString moveId = QStringLiteral("map.moveToLayer");
	for (const char* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (const int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Layer commands UI " << phase.toStdString() << std::endl;
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
				if (!expect(view && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->hasMesh(); }), "map ready")) {
					return 1;
				}
				const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
				const bool tb = QLatin1String(profile) == QLatin1String("trenchbroom");
				// Discoverable, keyboard-reachable and accessible commands.
				for (const QString& id : {expandId, moveId}) {
					QAction* command = action(id);
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
					if (!expect(command && row, "command and Tools row", id)) {
						return 1;
					}
					expect(!command->toolTip().isEmpty() && !row->accessibleName().isEmpty() && row->focusPolicy() != Qt::NoFocus
							&& QAccessible::queryAccessibleInterface(row),
						"accessible keyboard command", id);
				}
				auto* select = shell.findChild<QToolButton*>(QStringLiteral("levelSelectTools"));
				expect(select && select->defaultAction() && select->defaultAction()->menu()
						&& select->defaultAction()->menu()->actions().contains(action(expandId))
						&& select->defaultAction()->menu()->actions().contains(action(moveId)),
					"the view bar's Select menu lists both commands");
				// NetRadiant Custom's keys, except that Ctrl+Q stays Quit.
				expect(action(moveId)->shortcuts() == (nrc ? QList<QKeySequence> {QKeySequence(QStringLiteral("Shift+Q"))} : QList<QKeySequence> {}),
					"Shift+Q opens the layers menu only in NetRadiant Custom", action(moveId)->shortcut().toString());
				expect(action(expandId)->shortcuts() == (nrc ? QList<QKeySequence> {QKeySequence(QStringLiteral("Ctrl+Shift+Q"))} : QList<QKeySequence> {}),
					"Ctrl+Shift+Q expands only in NetRadiant Custom", action(expandId)->shortcut().toString());
				expect(!action(expandId)->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Q"))), "Ctrl+Q is left to Quit");

				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				qsizetype depth = 0;
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) {
					view->setSelectionSet(refs);
					view->selectionSetChanged(refs);
					view->setFocus(Qt::OtherFocusReason);
					depth = shell.levelDocument().undoStack.size();
				};
				const LevelMapDocument& document = shell.levelDocument();
				const auto brush = [&](const char* name) {
					return LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, tests::sceneLayersBrush(document, QLatin1String(name))};
				};
				const auto key = [](const LevelMapSelectionRef& ref) { return levelMapSelectionRefId(ref); };
				const QString lights = tests::sceneLayersNode(document, QStringLiteral("Lights"));
				const QString pillars = tests::sceneLayersNode(document, QStringLiteral("Pillars"));
				const QByteArray baseline = bytes();
				const auto undoBack = [&](const char* label) {
					action(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == baseline && shell.levelDocument().undoStack.size() == depth, label, shell.statusBar()->currentMessage());
				};

				pick({});
				expect(!action(expandId)->isEnabled() && !action(moveId)->isEnabled(), "disabled with nothing selected");

				// Expand Selection to Layers: the whole Architecture layer.
				const QSet<QString> architecture {key(brush("w1")), key(brush("w2")), key(brush("g1")), key(brush("g2")), key(brush("d1")), key(brush("d2"))};
				pick({brush("w1")});
				expect(action(expandId)->isEnabled() && action(moveId)->isEnabled(), "a selection enables both");
				action(expandId)->trigger();
				expect(keys(shell.levelDocument().selection) == architecture && !shell.statusBar()->currentMessage().isEmpty(),
					"the layer, its closed group and its door's brushes", shell.statusBar()->currentMessage());
				expect(shell.levelDocument().undoStack.size() == depth + (tb ? 1 : 0) && bytes() == baseline,
					"selection history follows the profile and the map is unchanged");
				if (tb) {
					action(QStringLiteral("map.undo"))->trigger();
					expect(keys(shell.levelDocument().selection) == QSet<QString> {key(brush("w1"))}, "undo restores the pick");
				}

				// Move to Layer at the pointer: Default is greyed out, Lights takes the floor.
				pick({brush("floor")});
				bool defaultGreyed = false;
				QString popupName;
				auto shown = whenMenuShows([&](QMenu* menu) {
					popupName = menu->objectName();
					QAction* toDefault = childAction(menu, QStringLiteral("levelMoveToLayer-default"));
					defaultGreyed = toDefault && !toDefault->isEnabled() && !toDefault->toolTip().isEmpty();
					if (QAction* toLights = childAction(menu, QStringLiteral("levelMoveToLayer-") + lights); toLights && toLights->isEnabled()) {
						toLights->trigger();
					}
					menu->close();
				});
				action(moveId)->trigger();
				expect(*shown && popupName == QStringLiteral("levelMoveToLayerPopup") && defaultGreyed, "the layers popup, Default greyed out", popupName);
				expect(levelSceneMembership(shell.levelDocument().scene, key(brush("floor"))) == lights
						&& shell.levelDocument().undoStack.size() == depth + 1 && !shell.statusBar()->currentMessage().isEmpty(),
					"the floor moved to Lights in one step", shell.statusBar()->currentMessage());
				undoBack("undo restores the exact bytes");

				// The view context menu: Expand and a Move to Layer submenu; a closed group moves whole.
				pick({brush("g1"), brush("g2")});
				bool listed = false;
				shown = whenMenuShows([&](QMenu* menu) {
					auto* submenu = menu->findChild<QMenu*>(QStringLiteral("levelMoveToLayerMenu"));
					listed = submenu && menu->actions().contains(action(expandId)) && menu->actions().contains(submenu->menuAction())
						&& submenu->menuAction()->isEnabled() && !submenu->accessibleName().isEmpty();
					if (QAction* toLights = submenu ? childAction(submenu, QStringLiteral("levelMoveToLayer-") + lights) : nullptr) {
						toLights->trigger();
					}
					menu->close();
				});
				Q_EMIT view->customContextMenuRequested(QPoint(24, 24));
				expect(*shown && listed, "the context menu offers both");
				expect(levelSceneNode(shell.levelDocument().scene, pillars)->parentId == lights && shell.levelDocument().undoStack.size() == depth + 1,
					"Pillars moved whole into Lights");
				undoBack("undo the group move");
				pick({});

#ifdef VIBESTUDIO_HAVE_QTEST
				if (nrc && scale == 100) {
					// The keys in a view, through the shortcut map.
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					pick({brush("w1")});
					expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
					QTest::keyClick(view, Qt::Key_Q, Qt::ControlModifier | Qt::ShiftModifier);
					expect(until([&] { return keys(shell.levelDocument().selection) == architecture; }), "Ctrl+Shift+Q expands");
					// The expansion's task dialog may have taken activation; the
					// shortcut map only serves the active window.
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "the window is active again");
					pick({brush("floor")});
					expect(until([&] { return view->hasFocus() && action(moveId)->isEnabled(); }), "plan view focused, command enabled");
					popupName.clear();
					shown = whenMenuShows([&](QMenu* menu) {
						popupName = menu->objectName();
						menu->close();
					});
					QTest::keyClick(view, Qt::Key_Q, Qt::ShiftModifier);
					expect(until([&] { return *shown; }) && popupName == QStringLiteral("levelMoveToLayerPopup"), "Shift+Q opens the layers popup", popupName);
					expect(bytes() == baseline, "closing the popup changes nothing", QString::fromUtf8(bytes()).left(4000));
					pick({});
				}
#endif

				if (scale == 100 && nrc) {
					// A locked layer's entry is greyed out with its reason.
					shell.openPathFromCommandLine(lockedPath);
					action(QStringLiteral("shell.mode.levels"))->trigger();
					if (expect(until([&] { return shell.levelDocument().sourcePath == lockedPath && !shell.levelDocument().scene.nodes.isEmpty(); }), "locked map ready")) {
						const QByteArray lockedBaseline = bytes();
						pick({brush("floor")});
						QString reason;
						bool greyed = false;
						shown = whenMenuShows([&](QMenu* menu) {
							if (QAction* toLights = childAction(menu, QStringLiteral("levelMoveToLayer-") + lights)) {
								greyed = !toLights->isEnabled();
								reason = toLights->toolTip();
								toLights->trigger();
							}
							menu->close();
						});
						action(moveId)->trigger();
						expect(*shown && greyed && reason.contains(QStringLiteral("Lights")) && bytes() == lockedBaseline
								&& shell.levelDocument().undoStack.size() == depth,
							"a locked layer cannot take the floor", reason);
						pick({});
					}
				}
				expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("saved-%1-%2.map").arg(QLatin1String(profile)).arg(scale)), false, &error),
					"save the test document", error);
				shell.close();
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	QEventLoop drain;
	QTimer::singleShot(200, &drain, &QEventLoop::quit);
	drain.exec();
	std::cout << (ok ? "Layer commands UI checks passed" : "Layer commands UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
