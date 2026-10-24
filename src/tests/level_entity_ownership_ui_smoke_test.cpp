// Offscreen checks of the entity ownership commands in the Levels page: Move
// Primitives to Entity (last and first), Move Selected to World, Explode
// Entity and Make Structural's profile policy. Each runs under the NetRadiant
// Custom, TrenchBroom and VibeStudio profiles at 100% and at 200% text with
// the high-visibility theme, an expanding translator and right-to-left
// layout; each is one undo step back to the exact bytes, keys follow each
// profile, tool rows are keyboard-reachable and accessible, and a locked
// entity refuses the edit with a reason.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
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
#include <QFont>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

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

int ownerOf(const LevelMapDocument& document, int brushId)
{
	for (const LevelMapBrush& value : document.brushes) {
		if (value.id == brushId) {
			return value.entityId;
		}
	}
	return -2;
}

int countOfClass(const LevelMapDocument& document, const QString& className)
{
	int count = 0;
	for (const LevelMapEntity& value : document.entities) {
		count += value.className == className ? 1 : 0;
	}
	return count;
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
	const QByteArray source = tests::entityOwnershipFixtureBytes();
	const QString path = temp.filePath(QStringLiteral("ownership.map"));
	if (!expect(!source.isEmpty() && writeFile(path, source), "fixture")) {
		return 1;
	}
	// The same map with its door in a locked layer.
	LevelMapDocument lockedMap;
	QString error;
	LevelMapLoadRequest request;
	request.path = path;
	request.engineHint = QStringLiteral("idtech2");
	QString layer;
	const bool lockedBuilt = loadLevelMapBytes(request, source, &lockedMap, &error)
		&& createLevelSceneNode(&lockedMap, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
		&& assignLevelSceneObjects(&lockedMap, layer, {QStringLiteral("entity:%1").arg(tests::entityOwnershipEntity(lockedMap, QStringLiteral("func_door")))}, &error)
		&& setLevelSceneLocked(&lockedMap, layer, true, &error);
	const QString lockedPath = temp.filePath(QStringLiteral("locked.map"));
	if (!expect(lockedBuilt && writeFile(lockedPath, serializeLevelMap(lockedMap).bytes), "locked fixture", error)) {
		return 1;
	}
	for (const char* id : {"map.move-primitives-to-entity", "map.move-primitives-to-first-entity", "map.move-selected-to-world", "map.explode-entity",
			 "map.explode-entity-unique", "map.make-structural", "map.move-to-world", "map.tie-to-entity"}) {
		phase = QString::fromLatin1(id);
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id)), "palette entry");
	}
	const QStringList commands {QStringLiteral("map.movePrimitivesToEntity"), QStringLiteral("map.movePrimitivesToFirstEntity"),
		QStringLiteral("map.moveSelectedToWorld"), QStringLiteral("map.explodeEntity"), QStringLiteral("map.explodeEntityUnique")};
	for (const char* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (const int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Entity ownership UI " << phase.toStdString() << std::endl;
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
				for (const QString& id : commands) {
					QAction* command = action(id);
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
					if (!expect(command && row, "command and Tools row", id)) {
						return 1;
					}
					expect(!command->toolTip().isEmpty() && !row->accessibleName().isEmpty() && row->focusPolicy() != Qt::NoFocus
							&& QAccessible::queryAccessibleInterface(row),
						"accessible keyboard command", id);
				}
				auto* create = shell.findChild<QToolButton*>(QStringLiteral("levelCreateTools"));
				expect(create && create->defaultAction() && create->defaultAction()->menu()
						&& create->defaultAction()->menu()->actions().contains(action(QStringLiteral("map.movePrimitivesToEntity")))
						&& create->defaultAction()->menu()->actions().contains(action(QStringLiteral("map.explodeEntity"))),
					"the view bar's Create menu lists the commands");
				// Keys follow each profile's own editor.
				expect(action(QStringLiteral("map.movePrimitivesToEntity"))->shortcuts() == (nrc ? QList<QKeySequence> {QKeySequence(QStringLiteral("Ctrl+M"))} : QList<QKeySequence> {}),
					"Ctrl+M only in NetRadiant Custom", action(QStringLiteral("map.movePrimitivesToEntity"))->shortcut().toString());
				expect(action(QStringLiteral("map.movePrimitivesToFirstEntity"))->shortcuts().isEmpty() && action(QStringLiteral("map.moveSelectedToWorld"))->shortcuts().isEmpty()
						&& action(QStringLiteral("map.explodeEntity"))->shortcuts().isEmpty(),
					"the other ownership commands are unbound, as upstream");
				// Both editors put Make Structural on Alt+S; only TrenchBroom's moves brushes out of entities.
				expect(action(QStringLiteral("map.makeStructural"))->shortcuts()
						== (tb || nrc ? QList<QKeySequence> {QKeySequence(QStringLiteral("Alt+S"))} : QList<QKeySequence> {}),
					"Alt+S in TrenchBroom and NetRadiant Custom", action(QStringLiteral("map.makeStructural"))->shortcut().toString());

				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				// The TrenchBroom profile records picks in the history too, so each
				// edit is measured from the depth right after its own pick.
				qsizetype depth = 0;
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) {
					view->setSelectionSet(refs);
					view->selectionSetChanged(refs);
					view->setFocus(Qt::OtherFocusReason);
					depth = shell.levelDocument().undoStack.size();
				};
				const LevelMapDocument& document = shell.levelDocument();
				const int door = tests::entityOwnershipEntity(document, QStringLiteral("func_door"));
				const int wall = tests::entityOwnershipEntity(document, QStringLiteral("func_wall"));
				const int world = tests::entityOwnershipEntity(document, QStringLiteral("worldspawn"));
				const int w1 = tests::entityOwnershipBrush(document, QStringLiteral("w1"));
				const int c1 = tests::entityOwnershipBrush(document, QStringLiteral("c1"));
				const int d1 = tests::entityOwnershipBrush(document, QStringLiteral("d1"));
				const int d2 = tests::entityOwnershipBrush(document, QStringLiteral("d2"));
				const auto brush = [](int id) { return LevelMapSelectionRef {LevelMapSelectionKind::QuakeBrush, id}; };
				const QByteArray baseline = bytes();
				const auto undoBack = [&](const char* label) {
					action(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == baseline && shell.levelDocument().undoStack.size() == depth, label, shell.statusBar()->currentMessage());
				};

				pick({});
				for (const QString& id : commands) {
					expect(!action(id)->isEnabled(), "disabled with nothing selected", id);
				}
				// Move Primitives to Entity: the door of the last-selected brush takes the others.
				pick({brush(w1), brush(c1), brush(d1)});
				expect(action(QStringLiteral("map.movePrimitivesToEntity"))->isEnabled() && action(QStringLiteral("map.explodeEntity"))->isEnabled()
						&& action(QStringLiteral("map.moveSelectedToWorld"))->isEnabled(),
					"the selection enables the commands");
				action(QStringLiteral("map.movePrimitivesToEntity"))->trigger();
				expect(ownerOf(shell.levelDocument(), w1) == door && ownerOf(shell.levelDocument(), c1) == door && countOfClass(shell.levelDocument(), QStringLiteral("func_wall")) == 0
						&& shell.levelDocument().undoStack.size() == depth + 1 && !shell.statusBar()->currentMessage().isEmpty(),
					"Move Primitives to Entity is one step into the door", shell.statusBar()->currentMessage());
				const QByteArray moved = bytes();
				undoBack("undo restores the original bytes");
				action(QStringLiteral("map.redo"))->trigger();
				expect(bytes() == moved, "redo moves them again");
				undoBack("undo once more");
				// The first-selected brush's entity takes the rest instead.
				pick({brush(c1), brush(d1)});
				action(QStringLiteral("map.movePrimitivesToFirstEntity"))->trigger();
				expect(ownerOf(shell.levelDocument(), d1) == wall, "the first-selected func_wall takes the door brush", shell.statusBar()->currentMessage());
				undoBack("undo the first-entity move");
				// Move Selected to World keeps the door and its other brushes.
				pick({brush(d1)});
				action(QStringLiteral("map.moveSelectedToWorld"))->trigger();
				expect(ownerOf(shell.levelDocument(), d1) == world && ownerOf(shell.levelDocument(), d2) == door, "one brush leaves the door", shell.statusBar()->currentMessage());
				undoBack("undo Move Selected to World");
				// Explode Entity: one door per brush, all selected.
				pick({brush(d2)});
				action(QStringLiteral("map.explodeEntity"))->trigger();
				expect(countOfClass(shell.levelDocument(), QStringLiteral("func_door")) == 3 && shell.levelDocument().selection.size() == 3
						&& shell.levelDocument().undoStack.size() == depth + 1,
					"Explode Entity makes three selected doors in one step", shell.statusBar()->currentMessage());
				undoBack("undo Explode Entity");
				// Make Structural follows the profile's policy.
				pick({brush(d1)});
				action(QStringLiteral("map.makeStructural"))->trigger();
				if (tb) {
					expect(ownerOf(shell.levelDocument(), d1) == world && shell.levelDocument().undoStack.size() == depth + 1,
						"TrenchBroom's Make Structural takes the brush out of the door", shell.statusBar()->currentMessage());
					undoBack("undo Make Structural");
				} else {
					expect(bytes() == baseline && shell.levelDocument().undoStack.size() == depth && !shell.statusBar()->currentMessage().isEmpty(),
						"other profiles keep the brush in its door and say why", shell.statusBar()->currentMessage());
				}
				pick({});

				if (scale == 100 && nrc) {
					// A locked entity refuses the edit with a reason and leaves the map alone.
					shell.openPathFromCommandLine(lockedPath);
					action(QStringLiteral("shell.mode.levels"))->trigger();
					if (expect(until([&] { return shell.levelDocument().sourcePath == lockedPath && !shell.levelDocument().scene.nodes.isEmpty(); }), "locked map ready")) {
						const QByteArray lockedBaseline = bytes();
						const int lockedDoor = tests::entityOwnershipEntity(shell.levelDocument(), QStringLiteral("func_door"));
						const int lockedW1 = tests::entityOwnershipBrush(shell.levelDocument(), QStringLiteral("w1"));
						const int lockedD1 = tests::entityOwnershipBrush(shell.levelDocument(), QStringLiteral("d1"));
						pick({brush(lockedW1), brush(lockedD1)});
						action(QStringLiteral("map.movePrimitivesToEntity"))->trigger();
						expect(bytes() == lockedBaseline && shell.levelDocument().undoStack.size() == depth
								&& ownerOf(shell.levelDocument(), lockedW1) != lockedDoor && !shell.statusBar()->currentMessage().isEmpty(),
							"a locked door refuses new brushes", shell.statusBar()->currentMessage());
						pick({brush(lockedD1)});
						action(QStringLiteral("map.explodeEntity"))->trigger();
						expect(bytes() == lockedBaseline && shell.levelDocument().undoStack.size() == depth, "a locked door cannot be exploded",
							shell.statusBar()->currentMessage());
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
	std::cout << (ok ? "Entity ownership UI checks passed" : "Entity ownership UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
