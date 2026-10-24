// Every Levels command in the shared command registry, which feeds the
// command palette, the CLI's `ui semantics`, the editor profiles, Keyboard
// settings and the parity reviews. Drives the real shell offscreen in the
// NetRadiant Custom, TrenchBroom and studio profiles at 100% and 200% (high
// contrast, expanded translation, right to left):
// - each registered map.* action, and Load Entity Definitions, resolves to a
//   registry entry; palette entries carry a translated label and summary in
//   the Levels category and the hyphenated id reviews cite; none is listed
//   twice;
// - the familiar keys the TrenchBroom and NetRadiant Custom profiles give
//   vertex, edge and face editing, the patch matrix and the other newly
//   registered commands are installed, take no key another command keeps,
//   and reach the plan view, with exact undo for the patch edits;
// - Show Map Statistics and Show Worldspawn Keys bring their Map tab sections
//   up expanded, with keyboard focus in their lists.
// With the vibestudio binary as the first argument, `ui semantics --json`
// is checked for the same ids. Owned offscreen widgets, settings in a
// temporary folder and synthetic key events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_editor_controls.h"
#include "core/level_map.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"
#include "tests/level_prefab_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>

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

void settle()
{
	QEventLoop loop;
	QTimer::singleShot(40, &loop, &QEventLoop::quit);
	loop.exec();
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

// The registry's spelling of a shell id: a lower-to-upper step is a hyphen.
QString hyphenated(const QString& id)
{
	QString result;
	for (int index = 0; index < id.size(); ++index) {
		const QChar ch = id.at(index);
		if (ch.isUpper() && index > 0 && (id.at(index - 1).isLower() || id.at(index - 1).isDigit())) {
			result += QLatin1Char('-');
		}
		result += ch.toLower();
	}
	return result;
}

QList<QKeySequence> sequences(const QStringList& texts)
{
	QList<QKeySequence> result;
	for (const QString& text : texts) {
		result << QKeySequence::fromString(text, QKeySequence::PortableText);
	}
	return result;
}

QString sequenceText(const QList<QKeySequence>& keys)
{
	return QKeySequence::listToString(keys, QKeySequence::PortableText);
}

// The keys each familiar profile gives commands that only gained registry
// entries with this coverage. Upstream sources are named in
// core/level_editor_controls.cpp.
QHash<QString, QStringList> familiarKeys(const QString& profile)
{
	if (profile == QLatin1String("netradiant-custom")) {
		return {
			{QStringLiteral("map.vertexTool"), {QStringLiteral("V")}},
			{QStringLiteral("map.edgeTool"), {QStringLiteral("E")}},
			{QStringLiteral("map.faceTool"), {QStringLiteral("F")}},
			{QStringLiteral("map.patchInsertRows"), {QStringLiteral("Ctrl+Num++")}},
			{QStringLiteral("map.patchInsertColumns"), {QStringLiteral("Ctrl+Shift+Num++")}},
			{QStringLiteral("map.patchDeleteLastRows"), {QStringLiteral("Ctrl+Num+-")}},
			{QStringLiteral("map.patchDeleteLastColumns"), {QStringLiteral("Ctrl+Shift+Num+-")}},
			{QStringLiteral("map.patchInvert"), {QStringLiteral("Ctrl+I")}},
			{QStringLiteral("map.patchTranspose"), {QStringLiteral("Ctrl+Shift+M")}},
			{QStringLiteral("map.thickenPatch"), {QStringLiteral("Ctrl+T")}},
			{QStringLiteral("map.patchSmoothColumns"), {QStringLiteral("Ctrl+Shift+W")}},
			{QStringLiteral("map.surfaceFlipV"), {QStringLiteral("Shift+I")}},
			{QStringLiteral("map.textureLock"), {QStringLiteral("Shift+T")}},
			{QStringLiteral("map.makeDetail"), {QStringLiteral("Alt+D")}},
			{QStringLiteral("map.makeStructural"), {QStringLiteral("Alt+S")}},
			{QStringLiteral("map.centerOnSelection"), {QStringLiteral("Ctrl+Shift+Tab")}},
		};
	}
	if (profile == QLatin1String("trenchbroom")) {
		return {
			{QStringLiteral("map.vertexTool"), {QStringLiteral("V")}},
			{QStringLiteral("map.edgeTool"), {QStringLiteral("E")}},
			{QStringLiteral("map.faceTool"), {QStringLiteral("F")}},
			{QStringLiteral("map.snapVertices"), {QStringLiteral("Ctrl+Alt+Shift+V")}},
			{QStringLiteral("map.createLinkedDuplicate"), {QStringLiteral("Ctrl+Shift+D")}},
			{QStringLiteral("map.componentUvLock"), {QStringLiteral("U")}},
		};
	}
	return {};
}

// Core-only checks of the profile tables: every familiar key is in the
// profile, no other Levels or window-wide command keeps it, and the derived
// NetRadiant baseline does not inherit keys audited only for NRC.
void checkProfileTables()
{
	phase = QStringLiteral("profile tables");
	for (const QString& profileId : {QStringLiteral("netradiant-custom"), QStringLiteral("trenchbroom")}) {
		EditorProfileDescriptor profile;
		if (!expect(editorProfileForId(profileId, &profile), "profile exists", profileId)) {
			continue;
		}
		QHash<QString, QStringList> keysByCommand;
		for (const LevelEditorKeyBinding& binding : profile.controls.keys) {
			keysByCommand.insert(hyphenated(binding.commandId), binding.keys);
		}
		const auto wanted = familiarKeys(profileId);
		for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
			expect(keysByCommand.value(hyphenated(it.key())) == it.value(), "profile table key", profileId + QLatin1Char(' ') + it.key());
			expect(QKeySequence::fromString(it.value().first(), QKeySequence::PortableText).count() == 1, "portable key parses", it.value().first());
			// Bindings of these commands now count as implemented, so the shell
			// installs them.
			bool implemented = false;
			for (const EditorProfileBinding& binding : profile.bindings) {
				implemented |= hyphenated(binding.commandId) == hyphenated(it.key()) && binding.implemented;
			}
			expect(implemented, "profile binding resolves to a registered command", it.key());
			const QKeySequence key = QKeySequence::fromString(it.value().first(), QKeySequence::PortableText);
			for (const ShortcutDescriptor& descriptor : shortcutDescriptors()) {
				if (descriptor.surfaceScoped && descriptor.context != QLatin1String("map")) {
					continue;
				}
				QStringList defaults = descriptor.alternateSequences;
				defaults.prepend(descriptor.defaultSequence);
				const QString owner = hyphenated(descriptor.commandId);
				const QStringList effective = keysByCommand.contains(owner) ? keysByCommand.value(owner) : defaults;
				if (owner == hyphenated(it.key())) {
					continue;
				}
				for (const QString& text : effective) {
					expect(QKeySequence::fromString(text, QKeySequence::PortableText) != key, "familiar key is not kept by another command",
						profileId + QLatin1Char(' ') + it.value().first() + QLatin1Char(' ') + descriptor.commandId);
				}
			}
			for (auto other = keysByCommand.cbegin(); other != keysByCommand.cend(); ++other) {
				if (other.key() == hyphenated(it.key())) {
					continue;
				}
				for (const QString& text : other.value()) {
					expect(QKeySequence::fromString(text, QKeySequence::PortableText) != key, "familiar key is unique in the profile",
						profileId + QLatin1Char(' ') + other.key());
				}
			}
		}
	}
	EditorProfileDescriptor baseline;
	if (expect(editorProfileForId(QStringLiteral("netradiant"), &baseline), "NetRadiant baseline exists")) {
		const auto nrc = familiarKeys(QStringLiteral("netradiant-custom"));
		for (const LevelEditorKeyBinding& binding : baseline.controls.keys) {
			expect(!nrc.contains(binding.commandId), "NetRadiant baseline does not claim NRC-only keys", binding.commandId);
		}
	}
	EditorProfileDescriptor studio;
	if (expect(editorProfileForId(QStringLiteral("vibestudio-default"), &studio), "studio profile exists")) {
		for (const LevelEditorKeyBinding& binding : studio.controls.keys) {
			expect(binding.commandId != QLatin1String("map.vertexTool") && binding.commandId != QLatin1String("map.faceTool"),
				"studio profile keeps component modes unbound", binding.commandId);
		}
	}
}

void checkCli(const QString& binary, const QString& folder)
{
	phase = QStringLiteral("ui semantics CLI");
	QProcess process;
	process.setWorkingDirectory(folder);
	process.start(binary, {QStringLiteral("--cli"), QStringLiteral("--settings-file"), QDir(folder).filePath(QStringLiteral("cli.ini")),
		QStringLiteral("ui"), QStringLiteral("semantics"), QStringLiteral("--json")});
	const bool finished = process.waitForFinished(60000);
	const QByteArray output = process.readAllStandardOutput();
	if (!expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "ui semantics runs",
			QString::fromUtf8(output.left(400) + process.readAllStandardError()))) {
		return;
	}
	const QJsonObject root = QJsonDocument::fromJson(output).object();
	const QJsonObject semantics = root.value(QStringLiteral("uiSemantics")).toObject();
	QHash<QString, QJsonObject> palette;
	for (const QJsonValue& value : semantics.value(QStringLiteral("commandPalette")).toArray()) {
		palette.insert(value.toObject().value(QStringLiteral("commandId")).toString(), value.toObject());
	}
	for (const char* id : {"map.vertex-tool", "map.patch-invert", "map.surface-justify-left", "map.region-set-view", "map.sidebar.toggle-leading",
			 "map.allow-valve220", "map.layout.four-views", "map.statistics", "map.worldspawn", "entity.definitions"}) {
		const QJsonObject entry = palette.value(QString::fromLatin1(id));
		expect(!entry.isEmpty() && entry.value(QStringLiteral("category")).toString() == QLatin1String("Levels")
				&& !entry.value(QStringLiteral("label")).toString().isEmpty() && !entry.value(QStringLiteral("summary")).toString().isEmpty(),
			"CLI lists the palette route", QString::fromLatin1(id));
	}
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
	const QByteArray source = tests::prefabFixture();
	const QString path = temp.filePath(QStringLiteral("palette.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) {
		return 1;
	}
	file.close();

	checkProfileTables();
	if (argc > 1) {
		checkCli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), temp.path());
	}

	for (const auto* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (int scale : {100, 200}) {
			const QString profileId = QString::fromLatin1(profile);
			phase = QStringLiteral("%1 %2%").arg(profileId).arg(scale);
			std::cout << "Palette coverage UI " << profile << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profileId);
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
				action(QStringLiteral("map.layout.four-views"))->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				if (!expect(view && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 2
							&& shell.levelDocument().patches.size() == 1; }), "map ready")) {
					return 1;
				}

				// Every registered Levels command is in the shared registry.
				QStringList commands, missing, undescribed;
				for (QAction* candidate : shell.findChildren<QAction*>()) {
					const QString id = candidate->property("commandId").toString();
					if (!id.startsWith(QLatin1String("map.")) && id != QLatin1String("entity.definitions")) {
						continue;
					}
					commands << id;
					CommandPaletteEntry entry;
					const bool listed = commandPaletteEntryForCommandId(id, &entry);
					if (!shellCommandIdExists(id) || (!listed && !shortcutForCommandId(id))) {
						missing << id;
						continue;
					}
					// Open Map and Save Map As are filed under Maps beside the
					// other document commands; everything else is Levels.
					const bool document = entry.category == QCoreApplication::translate("VibeStudioSemantics", "Maps")
						&& (id == QLatin1String("map.open") || id == QLatin1String("map.saveAs"));
					if (listed && (entry.id.isEmpty() || entry.label.isEmpty() || entry.summary.isEmpty()
							|| (entry.category != QCoreApplication::translate("VibeStudioSemantics", "Levels") && !document))) {
						undescribed << id;
					}
				}
				expect(commands.size() > 200, "Levels commands enumerated", QString::number(commands.size()));
				expect(missing.isEmpty(), "every Levels command is in the shared registry", missing.join(QStringLiteral(", ")));
				expect(undescribed.isEmpty(), "palette entries are labelled, described and filed under Levels", undescribed.join(QStringLiteral(", ")));
				QSet<QString> seen;
				QStringList duplicates, exact;
				for (const CommandPaletteEntry& entry : commandPaletteEntries()) {
					const QString key = hyphenated(entry.commandId);
					if (seen.contains(key)) {
						duplicates << entry.commandId;
					}
					seen.insert(key);
					exact << entry.commandId;
				}
				expect(duplicates.isEmpty(), "no command is listed twice in the palette", duplicates.join(QStringLiteral(", ")));
				for (const char* id : {"map.vertex-tool", "map.component-uv-lock", "map.component-uv-fit", "map.round-to-grid", "map.sidebar.toggle-leading", "map.allow-valve220",
						 "map.layout.four-views", "map.statistics", "map.worldspawn", "entity.definitions"}) {
					expect(exact.contains(QString::fromLatin1(id)), "palette lists the hyphenated id reviews cite", QString::fromLatin1(id));
				}
				CommandPaletteEntry vertex;
				expect(commandPaletteEntryForCommandId(QStringLiteral("map.vertexTool"), &vertex)
						&& (scale == 200) == vertex.label.startsWith(QLatin1Char('['))
						&& (scale == 200) == vertex.summary.startsWith(QLatin1Char('[')),
					"palette label and summary follow the installed translation", vertex.label + QLatin1String(" / ") + vertex.summary);

				// The familiar profiles' keys are installed on the shell's actions.
				const auto wanted = familiarKeys(profileId);
				for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
					QAction* target = action(it.key());
					expect(target && target->shortcuts() == sequences(it.value()), "profile key installed",
						it.key() + QLatin1String(": ") + (target ? sequenceText(target->shortcuts()) : QStringLiteral("missing")));
					expect(target && target->toolTip().contains(it.value().first()), "tool tip names the profile key",
						target ? target->toolTip() : QString());
				}
				if (wanted.isEmpty()) {
					for (const char* id : {"map.vertexTool", "map.edgeTool", "map.faceTool"}) {
						expect(action(QString::fromLatin1(id))->shortcuts().isEmpty(), "studio profile leaves component modes without keys",
							QString::fromLatin1(id));
					}
				}

				// The keys reach the plan view: V, E and F step through the
				// component modes and F again leaves them; without a profile key
				// the view keeps V and F (F frames).
				const QVector<LevelMapSelectionRef> brush{{LevelMapSelectionKind::QuakeBrush, 0}};
				view->setSelectionSet(brush);
				view->selectionSetChanged(brush);
				shell.activateWindow();
				expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
				view->setFocus(Qt::OtherFocusReason);
				expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
				const auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
					if (QApplication::focusWidget() != view) {
						shell.activateWindow();
						until([&] { return QApplication::activeWindow() == &shell; });
						view->setFocus(Qt::OtherFocusReason);
						until([&] { return view->hasFocus(); });
					}
					QTest::keyClick(view, key, modifiers);
					settle();
				};
				const bool familiar = !wanted.isEmpty();
				press(Qt::Key_V);
				expect(view->componentMode() == (familiar ? MapViewport::ComponentMode::Vertices : MapViewport::ComponentMode::Off), "V key",
					QString::number(static_cast<int>(view->componentMode())));
				if (familiar) {
					press(Qt::Key_E);
					expect(view->componentMode() == MapViewport::ComponentMode::Edges, "E key edits edges");
					press(Qt::Key_F);
					expect(view->componentMode() == MapViewport::ComponentMode::Faces, "F key edits faces");
					press(Qt::Key_F);
					expect(view->componentMode() == MapViewport::ComponentMode::Off, "F again leaves face editing");
				}

				// NetRadiant Custom's patch keys edit the patch as one undo step.
				if (profileId == QLatin1String("netradiant-custom")) {
					const QVector<LevelMapSelectionRef> patch{{LevelMapSelectionKind::QuakePatch, 0}};
					view->setSelectionSet(patch);
					view->selectionSetChanged(patch);
					const QByteArray baseline = serializeLevelMap(shell.levelDocument()).bytes;
					const int height = shell.levelDocument().patches.first().height;
					press(Qt::Key_Plus, Qt::ControlModifier | Qt::KeypadModifier);
					expect(shell.levelDocument().patches.first().height == height + 2, "Ctrl+keypad plus inserts patch rows",
						QString::number(shell.levelDocument().patches.first().height));
					action(QStringLiteral("map.undo"))->trigger();
					expect(serializeLevelMap(shell.levelDocument()).bytes == baseline, "row insertion undoes exactly");
					const auto revision = shell.levelDocument().revision;
					press(Qt::Key_I, Qt::ControlModifier);
					expect(shell.levelDocument().revision != revision && serializeLevelMap(shell.levelDocument()).bytes != baseline,
						"Ctrl+I inverts the patch");
					action(QStringLiteral("map.undo"))->trigger();
					expect(serializeLevelMap(shell.levelDocument()).bytes == baseline, "inversion undoes exactly");
				}

				// Direct routes to two Map tab sections.
				SidebarPage* mapPage = shell.findChild<SidebarPage*>(QStringLiteral("levelSidebarPage-map"));
				for (const char* id : {"map.statistics", "map.worldspawn"}) {
					phase = QStringLiteral("%1 %2% %3").arg(profileId).arg(scale).arg(QLatin1String(id));
					QAction* route = action(QString::fromLatin1(id));
					if (!expect(route && mapPage, "Map section route exists")) {
						continue;
					}
					expect(!route->text().isEmpty() && !route->toolTip().isEmpty() && !route->statusTip().isEmpty(), "route is labelled and described");
					SidebarSection* section = mapPage->section(QString::fromLatin1(id));
					if (!expect(section, "Map tab section exists")) {
						continue;
					}
					section->setExpanded(false);
					shell.activateWindow();
					until([&] { return QApplication::activeWindow() == &shell; });
					route->trigger();
					settle();
					StudioSidebar* holder = shell.levelSidebar(true)->indexOfPage(QStringLiteral("map")) >= 0 ? shell.levelSidebar(true) : shell.levelSidebar(false);
					expect(holder && holder->currentPageId() == QLatin1String("map") && !holder->isFolded(), "Map tab is current and open",
						holder ? holder->currentPageId() : QString());
					expect(section->isExpanded() && section->isVisible(), "section is expanded and shown");
					QWidget* focused = QApplication::focusWidget();
					expect(focused && section->isAncestorOf(focused) && QAccessible::queryAccessibleInterface(focused)
							&& !focused->accessibleName().isEmpty(),
						"keyboard focus lands on the section's accessible list", focused ? focused->objectName() : QStringLiteral("none"));
				}
				phase = QStringLiteral("%1 %2%").arg(profileId).arg(scale);
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	if (!ok) {
		std::cerr << "level palette coverage UI smoke test failed" << std::endl;
		return 1;
	}
	std::cout << "level palette coverage UI smoke test passed" << std::endl;
	return 0;
}
