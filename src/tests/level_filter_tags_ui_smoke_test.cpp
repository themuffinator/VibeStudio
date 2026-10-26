// Tag filters from data and entity class filters in the real shell,
// offscreen, in the NetRadiant Custom and TrenchBroom profiles at 100% (dark)
// and 200% (high contrast, expanded translation, right to left):
// - the chosen tag file's tags become View tab check boxes with counts and
//   checkable Toggle <tag> Visible commands with palette entries, which the
//   view bar's Select > Filters menu lists; a tag command, its box, the saved
//   list and every plan view agree, and hiding a face tag says how many faces
//   leave the camera;
// - Skip and Fog are checkable kinds; Invert Filters flips tags; Show Every
//   Kind shows tags and classes again;
// - Entity Classes lists the built-in and map classes in prefix groups;
//   a Toggle <class> Visible command, a class row, a group row and Hide All /
//   Show All agree with each other and hide every entity of the class, those
//   added later included, never saving class filters; a user's key reaches a
//   class command through Qt Test;
// - Invert Filters says how many tags it hid;
// - opening another map shows every class again; the tag state comes back
//   in a new shell, a light created while lights are hidden is hidden too,
//   and forgetting the tag file drops the tags;
// - the map, the selection and undo never change.
// Owned offscreen widgets, settings in a temporary folder and synthetic key
// events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_filter_tags.h"
#include "core/level_view_filters.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"
#include "tests/level_view_filter_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSet>
#include <QStatusBar>
#include <QTemporaryDir>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>

#include <algorithm>
#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << phase.toUtf8().constData() << ": " << label << ": " << detail.toUtf8().constData() << std::endl;
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
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const QString text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QStringList savedFilters()
{
	StudioSettings fresh;
	return levelViewFiltersFromSaved(fresh.shellLayoutState(levelViewFiltersSettingsKey()));
}

QTreeWidgetItem* classItem(QTreeWidget* tree, const QString& classId)
{
	for (int g = 0; tree && g < tree->topLevelItemCount(); ++g) {
		QTreeWidgetItem* group = tree->topLevelItem(g);
		for (int c = 0; c < group->childCount(); ++c) {
			if (group->child(c)->data(0, Qt::UserRole + 1).toString() == classId) {
				return group->child(c);
			}
		}
	}
	return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QString path = temp.filePath(QStringLiteral("tags.map"));
	const QString tagPath = temp.filePath(QStringLiteral("tags.json"));
	const QString quakePath = temp.filePath(QStringLiteral("quake.map"));
	const QByteArray quake = "{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n{\n"
							 "( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) ground1_6 0 0 0 1 1\n"
							 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) ground1_6 0 0 0 1 1\n"
							 "( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) ground1_6 0 0 0 1 1\n"
							 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) ground1_6 0 0 0 1 1\n"
							 "( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) ground1_6 0 0 0 1 1\n"
							 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) ground1_6 0 0 0 1 1\n"
							 "}\n}\n{\n\"classname\" \"light\"\n\"origin\" \"32 32 32\"\n}\n";
	if (!expect(writeFile(path, tests::viewFilterTagsFixture()) && writeFile(tagPath, tests::viewFilterTagLayout()) && writeFile(quakePath, quake), "fixtures")) {
		return 1;
	}
	const QString classKey = QStringLiteral("Ctrl+Alt+Shift+K");

	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			const QString profileId = QString::fromLatin1(profile);
			phase = QStringLiteral("%1 %2%").arg(profileId).arg(scale);
			std::cout << "Filter tags UI " << profile << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profileId);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setShellLayoutState(levelViewFiltersSettingsKey(), QByteArray());
			settings.setShellLayoutState(QStringLiteral("levelView/filterTagFile"), tagPath.toUtf8());
			settings.setUserShortcuts({{QStringLiteral("map.filterClass.light"), {classKey}}});
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
				action(QStringLiteral("map.layout.four-views"))->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				if (!expect(view && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 10; }), "map ready")) {
					return 1;
				}
				if (QAction* sidebar = action(QStringLiteral("map.sidebar.view"))) {
					sidebar->trigger();
				}
				const QByteArray baseline = serializeLevelMap(shell.levelDocument()).bytes;
				const auto revision = shell.levelDocument().revision;
				const auto undo = shell.levelDocument().undoStack.size();
				const auto unchanged = [&] {
					return serializeLevelMap(shell.levelDocument()).bytes == baseline && shell.levelDocument().revision == revision
						&& shell.levelDocument().undoStack.size() == undo;
				};
				const auto everyPane = [&](int count) {
					for (MapViewport* pane : shell.findChildren<MapViewport*>()) {
						if (pane->hasDocument() && pane->filteredCount() != count) {
							return false;
						}
					}
					return true;
				};

				// The new kinds are commands like the others.
				for (const char* id : {"map.filterSkip", "map.filterFog"}) {
					QAction* kind = action(QString::fromLatin1(id));
					expect(kind && kind->isCheckable() && kind->isChecked() && kind->isEnabled(), "Skip and Fog are checkable kinds", QString::fromLatin1(id));
				}
				expect(shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-skip")) && shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-fog")),
					"Skip and Fog have View tab boxes");

				// The tag file's tags: commands, palette entries and boxes.
				const QStringList tagIds {QStringLiteral("water"), QStringLiteral("triggers"), QStringLiteral("fog-volume"), QStringLiteral("detail"),
					QStringLiteral("mist"), QStringLiteral("skip-faces")};
				for (const QString& id : tagIds) {
					QAction* toggle = action(QStringLiteral("map.filterTag.") + id);
					CommandPaletteEntry entry;
					if (!expect(toggle, "tag command exists", id)) {
						continue;
					}
					expect(toggle->isCheckable() && toggle->isChecked() && toggle->isEnabled() && !toggle->text().isEmpty() && !toggle->statusTip().isEmpty()
							&& toggle->shortcuts().isEmpty(),
						"tag command is checked, enabled, described and unbound", id);
					expect(commandPaletteEntryForCommandId(toggle->objectName(), &entry) && !entry.summary.isEmpty() && shellCommandIdExists(toggle->objectName()),
						"tag command is in the shared registry", id);
					auto* box = shell.findChild<QCheckBox*>(QStringLiteral("levelFilterTag-") + id);
					expect(box && box->isChecked() && box->focusPolicy() == Qt::TabFocus && !box->accessibleDescription().isEmpty(), "tag box", id);
				}
				auto* tagStatus = shell.findChild<QLabel*>(QStringLiteral("levelFilterTagStatus"));
				expect(tagStatus && tagStatus->property("tagCount").toInt() == tagIds.size() && !tagStatus->text().isEmpty() && !tagStatus->accessibleName().isEmpty(),
					"the status names the tags' source", tagStatus ? tagStatus->text() : QString());
				auto* waterBox = shell.findChild<QCheckBox*>(QStringLiteral("levelFilterTag-water"));
				expect(waterBox && until([&] { return waterBox->accessibleName().contains(QLatin1String("2")); }), "a tag box names its count",
					waterBox ? waterBox->accessibleName() : QString());
				auto* filterMenu = shell.findChild<QMenu*>(QStringLiteral("levelFilterToolsMenu"));
				if (expect(filterMenu, "Select > Filters menu")) {
					QMetaObject::invokeMethod(filterMenu, "aboutToShow");
					expect(filterMenu->actions().contains(action(QStringLiteral("map.filterTag.water")))
							&& filterMenu->actions().contains(action(QStringLiteral("map.filterSkip"))),
						"Select > Filters lists the tags after the kinds");
				}

				// A tag command, its box, the saved list and every plan view agree.
				action(QStringLiteral("map.filterTag.water"))->trigger();
				expect(!waterBox->isChecked() && savedFilters() == QStringList {QStringLiteral("tag:water")} && view->filteredCount() == 2 && everyPane(2),
					"Toggle Water Visible hides the water brush and patch everywhere", QString::number(view->filteredCount()));
				expect(view->isHidden(LevelMapSelectionKind::QuakeBrush, 4) && view->isHidden(LevelMapSelectionKind::QuakePatch, 0)
						&& !view->isHidden(LevelMapSelectionKind::QuakeBrush, 5),
					"a brush with one water face keeps its outline");
				expect(shell.statusBar()->currentMessage().contains(QLatin1String("1")), "the status bar counts the face the camera drops",
					shell.statusBar()->currentMessage());
				waterBox->click();
				expect(action(QStringLiteral("map.filterTag.water"))->isChecked() && savedFilters().isEmpty() && view->filteredCount() == 0, "the box drives the command");
				action(QStringLiteral("map.filterTag.triggers"))->trigger();
				expect(view->isHidden(LevelMapSelectionKind::QuakeBrush, 7) && view->isHidden(LevelMapSelectionKind::QuakeBrush, 8) && view->filteredCount() == 2,
					"a class tag hides trigger entities with their brushes", QString::number(view->filteredCount()));
				action(QStringLiteral("map.invertFilters"))->trigger();
				const QStringList inverted = savedFilters();
				expect(!inverted.contains(QStringLiteral("tag:triggers")) && inverted.contains(QStringLiteral("tag:water"))
						&& inverted.contains(QStringLiteral("world")),
					"Invert Filters flips tags with the kinds", inverted.join(QLatin1Char(',')));
				const int invertedTags = static_cast<int>(tagIds.size()) - 1;
				expect(shell.statusBar()->currentMessage().contains(ApplicationShell::tr("%n tag(s)", nullptr, invertedTags)),
					"Invert Filters says how many tags it hid", shell.statusBar()->currentMessage());
				action(QStringLiteral("map.resetFilters"))->trigger();
				expect(savedFilters().isEmpty() && view->filteredCount() == 0 && action(QStringLiteral("map.filterTag.triggers"))->isChecked(), "Show Every Kind");

				// Entity Classes: commands, rows, groups and the buttons agree.
				auto* tree = shell.findChild<QTreeWidget*>(QStringLiteral("levelClassFilterTree"));
				auto* summary = shell.findChild<QLabel*>(QStringLiteral("levelClassFilterSummary"));
				if (!expect(tree && summary && tree->isEnabled() && !tree->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(tree),
						"the class tree is there, named and accessible")) {
					return 1;
				}
				QAction* lightCommand = action(QStringLiteral("map.filterClass.light"));
				CommandPaletteEntry lightEntry;
				expect(lightCommand && lightCommand->isCheckable() && lightCommand->isChecked() && lightCommand->isEnabled()
						&& commandPaletteEntryForCommandId(lightCommand->objectName(), &lightEntry)
						&& lightCommand->shortcuts() == QList<QKeySequence> {QKeySequence(classKey)},
					"Toggle light Visible is a checkable command with the user's key", lightCommand ? lightCommand->shortcut().toString() : QString());
				expect(action(QStringLiteral("map.filterClass.my_custom_thing")) && action(QStringLiteral("map.filterClass.info_player_deathmatch")),
					"commands for a map-only class and a built-in class");
				QTreeWidgetItem* lightRow = classItem(tree, QStringLiteral("class:light"));
				expect(lightRow && lightRow->checkState(0) == Qt::Checked && lightRow->text(0).contains(QLatin1String("2")) && lightRow->parent(),
					"the light row counts both lights in a group", lightRow ? lightRow->text(0) : QString());
				lightCommand->trigger();
				expect(view->filteredCount() == 2 && view->isHidden(LevelMapSelectionKind::Entity, 3) && view->isHidden(LevelMapSelectionKind::Entity, 4)
						&& lightRow->checkState(0) == Qt::Unchecked && savedFilters().isEmpty() && summary->property("hiddenClasses").toInt() == 1,
					"the class command hides every light, the row follows and nothing is saved", QString::number(view->filteredCount()));
				lightRow->setCheckState(0, Qt::Checked);
				expect(lightCommand->isChecked() && view->filteredCount() == 0, "the row drives the command");
				QTreeWidgetItem* triggerRow = classItem(tree, QStringLiteral("class:trigger_hurt"));
				QTreeWidgetItem* triggerGroup = triggerRow ? triggerRow->parent() : nullptr;
				if (expect(triggerGroup && triggerGroup->childCount() > 2, "a trigger group")) {
					triggerGroup->setCheckState(0, Qt::Unchecked);
					expect(view->isHidden(LevelMapSelectionKind::Entity, 1) && view->isHidden(LevelMapSelectionKind::Entity, 2)
							&& !action(QStringLiteral("map.filterClass.trigger_multiple"))->isChecked() && triggerGroup->checkState(0) == Qt::Unchecked,
						"a group row hides all its classes");
					triggerRow->setCheckState(0, Qt::Checked);
					expect(triggerGroup->checkState(0) == Qt::PartiallyChecked && !view->isHidden(LevelMapSelectionKind::Entity, 2)
							&& view->isHidden(LevelMapSelectionKind::Entity, 1),
						"a partly shown group is partly checked");
				}
				auto* hideAll = shell.findChild<QToolButton*>(QStringLiteral("levelClassFiltersHideAll"));
				auto* showAll = shell.findChild<QToolButton*>(QStringLiteral("levelClassFiltersShowAll"));
				if (expect(hideAll && showAll && !hideAll->accessibleName().isEmpty() && hideAll->focusPolicy() == Qt::TabFocus, "class buttons")) {
					hideAll->click();
					const int entities = static_cast<int>(shell.levelDocument().entities.size()) - 1;
					expect(view->filteredCount() == entities && !lightCommand->isChecked(), "Hide All Classes hides every entity but worldspawn",
						QString::number(view->filteredCount()));
					showAll->click();
					expect(view->filteredCount() == 0 && lightCommand->isChecked() && summary->property("hiddenClasses").toInt() == 0, "Show All Classes");
				}
				auto* find = shell.findChild<QLineEdit*>(QStringLiteral("levelClassFilterFind"));
				if (expect(find && !find->accessibleName().isEmpty(), "find box")) {
					find->setText(QStringLiteral("custom"));
					QTreeWidgetItem* custom = classItem(tree, QStringLiteral("class:my_custom_thing"));
					expect(custom && !custom->isHidden() && lightRow->isHidden(), "the find box narrows the rows");
					find->clear();
					expect(!lightRow->isHidden(), "clearing it shows every row");
				}
#ifdef VIBESTUDIO_HAVE_QTEST
				// The user's key reaches the class command from a plan view.
				shell.activateWindow();
				expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
				view->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
				QTest::keySequence(view, QKeySequence(classKey));
				expect(until([&] { return !lightCommand->isChecked(); }, 3000) && view->isHidden(LevelMapSelectionKind::Entity, 3), "the user's key hides lights");
				QTest::keySequence(view, QKeySequence(classKey));
				expect(until([&] { return lightCommand->isChecked(); }, 3000) && !view->isHidden(LevelMapSelectionKind::Entity, 3), "and shows them again");
#endif
				// Class filters belong to the map session.
				lightCommand->trigger();
				action(QStringLiteral("map.filterTag.mist"))->trigger();
				expect(unchanged(), "filters never change the map or undo");
				shell.openPathFromCommandLine(quakePath);
				if (expect(until([&] { return shell.levelDocument().sourcePath == quakePath; }), "Quake map ready")) {
					// The Quake classes replace the Quake III ones; the old
					// commands go once control returns to the event loop.
					QAction* quakeLight = nullptr;
					until([&] {
						quakeLight = action(QStringLiteral("map.filterClass.light"));
						return quakeLight && quakeLight->isEnabled() && quakeLight->isChecked();
					}, 5000);
					expect(quakeLight && quakeLight->isChecked() && view->filteredCount() == 0, "another map shows every class again",
						QStringLiteral("command %1, checked %2, filtered %3")
							.arg(quakeLight != nullptr)
							.arg(quakeLight && quakeLight->isChecked())
							.arg(view->filteredCount()));
					expect(action(QStringLiteral("map.filterTag.mist")) && !action(QStringLiteral("map.filterTag.mist"))->isChecked()
							&& !action(QStringLiteral("map.filterFog"))->isEnabled() && action(QStringLiteral("map.filterSkip"))->isEnabled(),
						"tags stay switched off; Fog is for Quake III only");
				}
				action(QStringLiteral("map.resetFilters"))->trigger();
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}

	// The tag state comes back in a new shell; forgetting the file drops the
	// tags and their commands.
	{
		phase = QStringLiteral("persistence");
		settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom"));
		settings.setTextScalePercent(100);
		settings.setTheme(StudioTheme::Dark);
		settings.setShellLayoutState(levelViewFiltersSettingsKey(), QByteArrayLiteral("tag:water,class:light"));
		settings.setShellLayoutState(QStringLiteral("levelView/filterTagFile"), tagPath.toUtf8());
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, 100));
		ApplicationShell shell;
		shell.resize(1600, 1000);
		shell.show();
		shell.openPathFromCommandLine(path);
		const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
		action(QStringLiteral("shell.mode.levels"))->trigger();
		action(QStringLiteral("map.layout.four-views"))->trigger();
		auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		if (!expect(view && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 10; }), "map ready")) {
			return 1;
		}
		expect(until([&] { return action(QStringLiteral("map.filterTag.water")) && !action(QStringLiteral("map.filterTag.water"))->isChecked(); })
				&& until([&] { return view->filteredCount() == 2; }) && action(QStringLiteral("map.filterClass.light"))->isChecked(),
			"the saved tag state comes back; a class id in the saved list does not", QString::number(view->filteredCount()));
		// A class filter hides entities of the class added after it was
		// switched off, and the one undo removes.
		QAction* lightCommand = action(QStringLiteral("map.filterClass.light"));
		QAction* createLight = action(QStringLiteral("map.createEntity.light"));
		if (expect(lightCommand && createLight, "the light commands")) {
			lightCommand->trigger();
			expect(view->filteredCount() == 4 && view->isHidden(LevelMapSelectionKind::Entity, 3), "Toggle light Visible hides both lights",
				QString::number(view->filteredCount()));
			const int entitiesBefore = static_cast<int>(shell.levelDocument().entities.size());
			const QByteArray before = serializeLevelMap(shell.levelDocument()).bytes;
			view->rememberCommandPointer(QPointF(view->width() * 0.5, view->height() * 0.5));
			createLight->trigger();
			int added = -1;
			if (until([&] { return static_cast<int>(shell.levelDocument().entities.size()) == entitiesBefore + 1; }, 10000)) {
				for (const LevelMapEntity& entity : shell.levelDocument().entities) {
					if (entity.className == QLatin1String("light")) {
						added = std::max(added, entity.id);
					}
				}
			}
			expect(added > 4, "a light is added while lights are hidden", shell.statusBar()->currentMessage());
			expect(added >= 0 && until([&] { return view->isHidden(LevelMapSelectionKind::Entity, added) && view->filteredCount() == 5; }, 5000),
				"the class filter hides the new light too", QString::number(view->filteredCount()));
			action(QStringLiteral("map.undo"))->trigger();
			expect(until([&] { return static_cast<int>(shell.levelDocument().entities.size()) == entitiesBefore; }, 10000)
					&& serializeLevelMap(shell.levelDocument()).bytes == before && until([&] { return view->filteredCount() == 4; }, 5000),
				"undo takes it away again", QString::number(view->filteredCount()));
			lightCommand->trigger();
			expect(lightCommand->isChecked() && view->filteredCount() == 2, "and the lights come back", QString::number(view->filteredCount()));
		}
		auto* forget = shell.findChild<QToolButton*>(QStringLiteral("levelFilterTagFileClear"));
		if (expect(forget && forget->isEnabled(), "Forget Tag File")) {
			forget->click();
			expect(until([&] { return !action(QStringLiteral("map.filterTag.water")); }) && !shell.findChild<QCheckBox*>(QStringLiteral("levelFilterTag-water"))
					&& view->filteredCount() == 0,
				"forgetting the file drops its tags", QString::number(view->filteredCount()));
		}
	}
	if (ok) {
		std::cout << "Filter tags UI: tags, classes, keys, session and persistence passed." << std::endl;
	}
	return ok ? 0 : 1;
}
