// Offscreen checks of Set Entity Colour and Normalise Entity Colour on the
// Levels page, under the NetRadiant Custom, TrenchBroom and VibeStudio
// profiles at 100% and at 200% text with the high-visibility theme, an
// expanding translator and right-to-left layout. The picker opens on the
// primary entity's colour, or the colour chosen last; each command is one
// undo step back to the exact bytes; K sets the colour only in NetRadiant
// Custom; the Tools rows are keyboard-reachable and accessible; and a locked
// door refuses the edit with a reason before the picker opens.

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
#include <QColorDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

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

QString keyOf(const LevelMapDocument& document, int entityId)
{
	for (const LevelMapEntity& value : document.entities) {
		if (value.id == entityId) {
			for (const LevelMapProperty& property : value.properties) {
				if (property.key.compare(QStringLiteral("_color"), Qt::CaseInsensitive) == 0) {
					return property.value;
				}
			}
		}
	}
	return QString();
}

// What the colour picker showed when it opened, and whether it was there.
struct PickerSeen {
	bool shown = false;
	QColor initial;
	QString accessibleName;
	// Polls left; zero stops watching for the picker.
	int attempts = 600;
};

// Answers the entity colour picker once it shows: picks `colour`, or cancels
// when it is invalid.
std::shared_ptr<PickerSeen> answerPicker(const QColor& colour)
{
	auto seen = std::make_shared<PickerSeen>();
	auto poll = std::make_shared<std::function<void()>>();
	*poll = [seen, poll, colour]() {
		auto* picker = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
		if (picker && picker->isVisible() && picker->objectName() == QStringLiteral("levelEntityColourDialog")) {
			seen->shown = true;
			seen->initial = picker->currentColor();
			seen->accessibleName = picker->accessibleName();
			if (colour.isValid()) {
				picker->setCurrentColor(colour);
				picker->accept();
			} else {
				picker->reject();
			}
			return;
		}
		if (--seen->attempts > 0) {
			QTimer::singleShot(10, *poll);
		}
	};
	QTimer::singleShot(10, *poll);
	return seen;
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
	// The ownership fixture with a red light.
	LevelMapDocument seeded;
	QString error;
	LevelMapLoadRequest request;
	request.path = temp.filePath(QStringLiteral("colour.map"));
	request.engineHint = QStringLiteral("idtech2");
	const bool seededOk = loadLevelMapBytes(request, tests::entityOwnershipFixtureBytes(), &seeded, &error)
		&& setLevelMapEntityProperty(&seeded, tests::entityOwnershipEntity(seeded, QStringLiteral("light")), QStringLiteral("_color"), QStringLiteral("255 0 0"), &error);
	const QByteArray source = serializeLevelMap(seeded).bytes;
	const QString path = request.path;
	if (!expect(seededOk && writeFile(path, source), "fixture", error)) {
		return 1;
	}
	// The same map with its door in a locked layer.
	LevelMapDocument lockedMap;
	QString layer;
	const bool lockedBuilt = loadLevelMapBytes(request, source, &lockedMap, &error)
		&& createLevelSceneNode(&lockedMap, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
		&& assignLevelSceneObjects(&lockedMap, layer, {QStringLiteral("entity:%1").arg(tests::entityOwnershipEntity(lockedMap, QStringLiteral("func_door")))}, &error)
		&& setLevelSceneLocked(&lockedMap, layer, true, &error);
	const QString lockedPath = temp.filePath(QStringLiteral("locked.map"));
	if (!expect(lockedBuilt && writeFile(lockedPath, serializeLevelMap(lockedMap).bytes), "locked fixture", error)) {
		return 1;
	}
	for (const char* id : {"map.set-entity-colour", "map.normalise-entity-colour"}) {
		phase = QString::fromLatin1(id);
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id)), "palette entry");
	}
	const QStringList commands {QStringLiteral("map.setEntityColour"), QStringLiteral("map.normaliseEntityColour")};
	for (const char* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (const int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Entity colour UI " << phase.toStdString() << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			// Each pass starts from white, the colour nothing has chosen yet.
			settings.setLevelEntityColour({1.0, 1.0, 1.0});
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
				for (const QString& id : commands) {
					QAction* command = action(id);
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + id);
					if (!expect(command && row, "command and Tools row", id)) {
						return 1;
					}
					expect(!command->toolTip().isEmpty() && !row->accessibleName().isEmpty() && row->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(row),
						"accessible keyboard command", id);
				}
				// K in NetRadiant Custom only; Normalise has no key there either.
				expect(action(QStringLiteral("map.setEntityColour"))->shortcuts() == (nrc ? QList<QKeySequence> {QKeySequence(QStringLiteral("K"))} : QList<QKeySequence> {}),
					"K only in NetRadiant Custom", action(QStringLiteral("map.setEntityColour"))->shortcut().toString());
				expect(action(QStringLiteral("map.normaliseEntityColour"))->shortcuts().isEmpty(), "Normalise Entity Colour is unbound, as upstream");

				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				qsizetype depth = 0;
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) {
					view->setSelectionSet(refs);
					view->selectionSetChanged(refs);
					view->setFocus(Qt::OtherFocusReason);
					depth = shell.levelDocument().undoStack.size();
				};
				const LevelMapDocument& document = shell.levelDocument();
				const int door = tests::entityOwnershipEntity(document, QStringLiteral("func_door"));
				const int light = tests::entityOwnershipEntity(document, QStringLiteral("light"));
				const int d1 = tests::entityOwnershipBrush(document, QStringLiteral("d1"));
				const int w1 = tests::entityOwnershipBrush(document, QStringLiteral("w1"));
				const LevelMapSelectionRef lightRef {LevelMapSelectionKind::Entity, light};
				const LevelMapSelectionRef d1Ref {LevelMapSelectionKind::QuakeBrush, d1};
				const LevelMapSelectionRef w1Ref {LevelMapSelectionKind::QuakeBrush, w1};
				const QByteArray baseline = bytes();
				const auto undoBack = [&](const char* label) {
					action(QStringLiteral("map.undo"))->trigger();
					expect(bytes() == baseline && shell.levelDocument().undoStack.size() == depth, label, shell.statusBar()->currentMessage());
				};

				pick({});
				expect(!action(QStringLiteral("map.setEntityColour"))->isEnabled() && !action(QStringLiteral("map.normaliseEntityColour"))->isEnabled(),
					"disabled with nothing selected");
				pick({w1Ref});
				expect(!action(QStringLiteral("map.setEntityColour"))->isEnabled(), "a world brush has no entity colour");
				// The light picked last seeds the picker with its red.
				pick({d1Ref, lightRef});
				expect(action(QStringLiteral("map.setEntityColour"))->isEnabled() && action(QStringLiteral("map.normaliseEntityColour"))->isEnabled(),
					"the light and the door enable both commands");
				auto seen = answerPicker(QColor(0, 0, 255));
				action(QStringLiteral("map.setEntityColour"))->trigger();
				expect(seen->shown && seen->initial == QColor(255, 0, 0) && !seen->accessibleName.isEmpty(), "the picker opens on the light's colour",
					seen->initial.name());
				expect(keyOf(shell.levelDocument(), light) == QStringLiteral("0 0 255") && keyOf(shell.levelDocument(), door) == QStringLiteral("0 0 1")
						&& shell.levelDocument().undoStack.size() == depth + 1 && !shell.statusBar()->currentMessage().isEmpty(),
					"Set Entity Colour colours the light and the door's entity in one step",
					keyOf(shell.levelDocument(), light) + QStringLiteral(" / ") + keyOf(shell.levelDocument(), door) + QStringLiteral(": ") + shell.statusBar()->currentMessage());
				const QByteArray coloured = bytes();
				undoBack("undo restores the original bytes");
				action(QStringLiteral("map.redo"))->trigger();
				expect(bytes() == coloured, "redo colours them again");
				undoBack("undo once more");
				// A door without a colour opens on the colour chosen last; Cancel changes nothing.
				pick({lightRef, d1Ref});
				expect(!action(QStringLiteral("map.normaliseEntityColour"))->isEnabled(), "a primary door without _color cannot be normalised");
				seen = answerPicker(QColor());
				action(QStringLiteral("map.setEntityColour"))->trigger();
				expect(seen->shown && seen->initial == QColor(0, 0, 255) && bytes() == baseline && shell.levelDocument().undoStack.size() == depth,
					"the picker remembers blue and Cancel leaves the map alone", seen->initial.name());
				// Normalise: the light's red, brightest channel at 1, goes to the door too.
				pick({d1Ref, lightRef});
				action(QStringLiteral("map.normaliseEntityColour"))->trigger();
				expect(keyOf(shell.levelDocument(), light) == QStringLiteral("1 0 0") && keyOf(shell.levelDocument(), door) == QStringLiteral("1 0 0")
						&& shell.levelDocument().undoStack.size() == depth + 1,
					"Normalise Entity Colour is one step", shell.statusBar()->currentMessage());
				undoBack("undo Normalise Entity Colour");

				if (nrc && scale == 100) {
#ifdef VIBESTUDIO_HAVE_QTEST
					// K in a view sets the colour, as in NetRadiant Custom. The window
					// must be active for its shortcuts to reach the plan view.
					pick({lightRef});
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
					seen = answerPicker(QColor(0, 255, 0));
					QTest::keyClick(view, Qt::Key_K);
					until([&] { return seen->shown; });
					expect(seen->shown && keyOf(shell.levelDocument(), light) == QStringLiteral("0 255 0"), "K opens the picker and sets the colour",
						keyOf(shell.levelDocument(), light));
					if (shell.levelDocument().undoStack.size() > depth) {
						undoBack("undo the K colour");
					}
#endif

					// A locked door refuses the colour for every target, with a
					// reason, before the picker opens.
					shell.openPathFromCommandLine(lockedPath);
					action(QStringLiteral("shell.mode.levels"))->trigger();
					if (expect(until([&] { return shell.levelDocument().sourcePath == lockedPath && !shell.levelDocument().scene.nodes.isEmpty(); }), "locked map ready")) {
						const QByteArray lockedBaseline = bytes();
						const int lockedLight = tests::entityOwnershipEntity(shell.levelDocument(), QStringLiteral("light"));
						const int lockedD1 = tests::entityOwnershipBrush(shell.levelDocument(), QStringLiteral("d1"));
						pick({{LevelMapSelectionKind::Entity, lockedLight}, {LevelMapSelectionKind::QuakeBrush, lockedD1}});
						seen = answerPicker(QColor(0, 255, 0));
						action(QStringLiteral("map.setEntityColour"))->trigger();
						QEventLoop settle;
						QTimer::singleShot(200, &settle, &QEventLoop::quit);
						settle.exec();
						seen->attempts = 0;
						expect(!seen->shown && bytes() == lockedBaseline && shell.levelDocument().undoStack.size() == depth
								&& shell.statusBar()->currentMessage().contains(QString::number(tests::entityOwnershipEntity(shell.levelDocument(), QStringLiteral("func_door")))),
							"a locked door refuses the colour without opening the picker", shell.statusBar()->currentMessage());
						pick({});
					}
				}
				pick({});
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
	std::cout << (ok ? "Entity colour UI checks passed" : "Entity colour UI checks failed") << std::endl;
	return ok ? 0 : 1;
}
