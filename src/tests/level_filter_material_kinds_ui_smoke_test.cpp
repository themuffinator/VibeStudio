// The material filter kinds in the real shell, offscreen, in the NetRadiant
// Custom and TrenchBroom profiles at 100% (dark) and 200% (high contrast,
// expanded translation, right to left):
// - with a package folder open, the View tab reads its Quake III shader
//   scripts on a worker and says so in an accessible status line; the
//   Translucent, Liquids, Sky, Clip (with Player, Monster and Weapon Clip),
//   Bot Clip and Nodraw check boxes then count by shader, as the core does;
// - Translucent, Nodraw and the clip parts are checkable View > Filters
//   commands with palette entries; NetRadiant Custom binds Alt+4 to
//   Translucent, and with Qt Test the key reaches it from a plan view;
// - switching Translucent off hides the glass, grate, fence, fog and weapclip
//   brushes and the glass patch in every plan view and the camera; Liquids
//   leaves the lavarock wall;
// - Reload Shaders reads the scripts again, showing that it is reading;
// - without a package the kinds go by names, and a Quake map hides the line;
// - none of it changes the map, the selection or undo.
// Owned offscreen widgets, settings in a temporary folder and synthetic key
// events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_view_filters.h"
#include "core/package_archive.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"
#include "tests/level_view_filter_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QStatusBar>
#include <QTemporaryDir>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif
#include <QTimer>
#include <QToolButton>
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
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
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
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// Triangles the camera draws with one texture.
int cameraTriangles(const ModelViewport* camera, const QString& texture)
{
	int count = 0;
	for (const ModelSurface& surface : camera->mesh().surfaces) {
		if (surface.name.compare(texture, Qt::CaseInsensitive) == 0) {
			count += static_cast<int>(surface.triangles.size());
		}
	}
	return count;
}

const QStringList materialKinds {QStringLiteral("translucent"), QStringLiteral("liquids"), QStringLiteral("sky"), QStringLiteral("clip"),
	QStringLiteral("player-clip"), QStringLiteral("monster-clip"), QStringLiteral("weapon-clip"), QStringLiteral("botclip"), QStringLiteral("nodraw")};
const QStringList newKinds {QStringLiteral("translucent"), QStringLiteral("nodraw"), QStringLiteral("player-clip"), QStringLiteral("monster-clip"),
	QStringLiteral("weapon-clip")};

bool shadersRead(const QLabel* status, int shaders)
{
	return status && !status->isHidden() && !status->property("reading").toBool() && status->property("shaderCount").toInt() == shaders;
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
	const QString root = temp.filePath(QStringLiteral("baseq3"));
	const QString path = temp.filePath(QStringLiteral("materials.map"));
	if (!expect(writeFile(root + QStringLiteral("/scripts/filterkinds.shader"), tests::viewFilterMaterialKindsShaders())
				&& writeFile(root + QStringLiteral("/textures/skies/stars.tga"), QByteArrayLiteral("not decoded"))
				&& writeFile(path, tests::viewFilterMaterialKindsFixture()),
			"fixtures")) {
		return 1;
	}
	// What the core says each kind names with and without the package.
	LevelMapDocument document;
	QString error;
	PackageArchive package;
	if (!expect(loadLevelMapBytes({path, {}, QStringLiteral("idTech3")}, tests::viewFilterMaterialKindsFixture(), &document, &error)
				&& package.load(root, &error),
			"core fixtures", error)) {
		return 1;
	}
	const LevelViewFilterMaterials materials = scanLevelViewFilterMaterials(package);
	const QHash<QString, int> shaderCounts = levelViewFilterCounts(document, materials);
	const QHash<QString, int> nameCounts = levelViewFilterCounts(document);
	const int shaders = materials.scriptedShaderCount;
	expect(shaders == 13 && shaderCounts.value(QStringLiteral("translucent")) == 6 && shaderCounts.value(QStringLiteral("liquids")) == 2,
		"the core reads the package", QString::number(shaders));

	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			const QString profileId = QString::fromLatin1(profile);
			phase = QStringLiteral("%1 %2%").arg(profileId).arg(scale);
			std::cout << "Filter material kinds UI " << profile << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profileId);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setShellLayoutState(levelViewFiltersSettingsKey(), QByteArray());
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
				shell.openPathFromCommandLine(root);
				shell.openPathFromCommandLine(path);
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				const auto command = [&](const QString& kind) { return action(levelViewFilterCommandId(kind)); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				action(QStringLiteral("map.layout.four-views"))->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(view && camera && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 16; }),
						"map ready")) {
					return 1;
				}
				if (QAction* sidebar = action(QStringLiteral("map.sidebar.view"))) {
					sidebar->trigger();
				}
				const bool nrc = profileId == QLatin1String("netradiant-custom");
				auto* status = shell.findChild<QLabel*>(QStringLiteral("levelFilterMaterialStatus"));
				auto* reload = shell.findChild<QToolButton*>(QStringLiteral("levelFiltersReloadShaders"));
				if (!expect(status && reload && until([&] { return shadersRead(status, shaders); }), "the package's shaders are read",
						status ? status->text() : QString())) {
					return 1;
				}
				expect(!status->text().isEmpty() && !status->accessibleName().isEmpty() && status->accessibleDescription() == status->text()
						&& status->textFormat() == Qt::PlainText && status->wordWrap(),
					"the status line is accessible plain text", status->text());
				expect(!reload->isHidden() && reload->isEnabled() && !reload->accessibleName().isEmpty() && !reload->toolTip().isEmpty()
						&& reload->focusPolicy() == Qt::TabFocus,
					"Reload Shaders is an accessible button");

				// The new kinds are described, checkable commands in the palette;
				// NetRadiant Custom binds Alt+4 to Translucent only.
				for (const QString& kind : newKinds) {
					QAction* toggle = command(kind);
					if (!expect(toggle, "kind command exists", kind)) {
						continue;
					}
					CommandPaletteEntry entry;
					expect(toggle->isCheckable() && toggle->isChecked() && toggle->isEnabled() && !toggle->text().isEmpty() && !toggle->statusTip().isEmpty()
							&& commandPaletteEntryForCommandId(toggle->objectName(), &entry) && !entry.label.isEmpty(),
						"kind command is checked, enabled, described and in the palette", kind);
					const QList<QKeySequence> wanted = nrc && kind == QLatin1String("translucent") ? QList<QKeySequence> {QKeySequence(QStringLiteral("Alt+4"))}
																								  : QList<QKeySequence> {};
					expect(toggle->shortcuts() == wanted, "profile key", kind + QLatin1String(": ") + toggle->shortcut().toString());
				}
				// Every material kind's check box counts by shader.
				for (const QString& kind : materialKinds) {
					auto* box = shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-") + kind);
					const int count = shaderCounts.value(kind);
					expect(box && box->isChecked() && !box->toolTip().isEmpty() && box->focusPolicy() == Qt::TabFocus
							&& until([&] { return box->accessibleName().contains(QString::number(count)); }),
						"check box counts by shader", kind + QLatin1String(": ") + (box ? box->accessibleName() : QString()));
				}

				const QByteArray baseline = serializeLevelMap(shell.levelDocument()).bytes;
				const auto revision = shell.levelDocument().revision;
				const auto undo = shell.levelDocument().undoStack.size();
				const auto selection = shell.levelDocument().selection;
				const auto unchanged = [&] {
					return serializeLevelMap(shell.levelDocument()).bytes == baseline && shell.levelDocument().revision == revision
						&& shell.levelDocument().undoStack.size() == undo && shell.levelDocument().selection == selection;
				};

				// Translucent off: the see-through shaders leave every view.
				const QString glass = QStringLiteral("base_wall/glass");
				expect(until([&] { return camera->isEnabled() && camera->hasMesh() && cameraTriangles(camera, glass) > 0; }), "the camera draws the glass");
				command(QStringLiteral("translucent"))->trigger();
				expect(view->filteredCount() == 6 && view->isHidden(LevelMapSelectionKind::QuakeBrush, 0) && view->isHidden(LevelMapSelectionKind::QuakeBrush, 9)
						&& view->isHidden(LevelMapSelectionKind::QuakePatch, 0) && !view->isHidden(LevelMapSelectionKind::QuakeBrush, 15)
						&& !view->isHidden(LevelMapSelectionKind::QuakePatch, 1),
					"Translucent hides the see-through brushes and patch", QString::number(view->filteredCount()));
				expect(!command(QStringLiteral("translucent"))->isChecked()
						&& !shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-translucent"))->isChecked(),
					"the command and the check box agree");
				expect(until([&] { return camera->isEnabled() && camera->hasMesh() && cameraTriangles(camera, glass) == 0
										  && cameraTriangles(camera, QStringLiteral("base_wall/wall")) > 0; }),
					"the camera leaves the glass out", QString::number(cameraTriangles(camera, glass)));
				command(QStringLiteral("translucent"))->trigger();
				expect(view->filteredCount() == 0, "Translucent on shows them again");

				// Liquids goes by surfaceparm: the slime face's brush and the
				// unknown liquids/ texture, not the lavarock wall.
				command(QStringLiteral("liquids"))->trigger();
				expect(view->filteredCount() == 2 && view->isHidden(LevelMapSelectionKind::QuakeBrush, 5) && view->isHidden(LevelMapSelectionKind::QuakeBrush, 13)
						&& !view->isHidden(LevelMapSelectionKind::QuakeBrush, 4),
					"Liquids reads surfaceparms", QString::number(view->filteredCount()));
				// Player Clip alone leaves weapon and monster clip shown.
				command(QStringLiteral("player-clip"))->trigger();
				expect(view->isHidden(LevelMapSelectionKind::QuakeBrush, 8) && view->isHidden(LevelMapSelectionKind::QuakePatch, 1)
						&& !view->isHidden(LevelMapSelectionKind::QuakeBrush, 9) && !view->isHidden(LevelMapSelectionKind::QuakeBrush, 10),
					"Player Clip hides player clip only");
				action(QStringLiteral("map.resetFilters"))->trigger();
				expect(view->filteredCount() == 0, "Show Every Kind brings them back");

				// Reload Shaders reads the scripts again and says so meanwhile.
				reload->click();
				expect(status->property("reading").toBool() && !reload->isEnabled(), "Reload Shaders shows the read in progress");
				expect(until([&] { return shadersRead(status, shaders) && reload->isEnabled(); }), "the read finishes", status->text());

#ifdef VIBESTUDIO_HAVE_QTEST
				if (nrc && scale == 100) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
					QTest::keyClick(view, Qt::Key_4, Qt::AltModifier);
					expect(until([&] { return !command(QStringLiteral("translucent"))->isChecked() && view->filteredCount() == 6; }, 3000),
						"Alt+4 hides Translucent");
					QTest::keyClick(view, Qt::Key_4, Qt::AltModifier);
					expect(until([&] { return command(QStringLiteral("translucent"))->isChecked() && view->filteredCount() == 0; }, 3000),
						"Alt+4 shows it again");
				}
#endif
				expect(unchanged(), "filters never change the map, the selection or undo");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}

	// Without a package the material kinds go by texture names and the line
	// says how to read shaders; a Quake map hides it.
	{
		phase = QStringLiteral("no package");
		settings.setSelectedEditorProfileId(QStringLiteral("netradiant-custom"));
		settings.setTextScalePercent(100);
		settings.setTheme(StudioTheme::Dark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, 100));
		ApplicationShell shell;
		shell.resize(1600, 1000);
		shell.show();
		shell.openPathFromCommandLine(path);
		const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
		action(QStringLiteral("shell.mode.levels"))->trigger();
		if (expect(until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 16; }), "map ready")) {
			if (QAction* sidebar = action(QStringLiteral("map.sidebar.view"))) {
				sidebar->trigger();
			}
			auto* status = shell.findChild<QLabel*>(QStringLiteral("levelFilterMaterialStatus"));
			expect(status && until([&] { return shadersRead(status, 0); }) && !status->text().isEmpty(), "the line says no shaders were read");
			for (const QString& kind : materialKinds) {
				auto* box = shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-") + kind);
				const int count = nameCounts.value(kind);
				expect(box && until([&] { return box->accessibleName().contains(QString::number(count)); }), "check box counts by name",
					kind + QLatin1String(": ") + (box ? box->accessibleName() : QString()));
			}
			const QString quakePath = temp.filePath(QStringLiteral("quake.map"));
			const QByteArray quake = "{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n{\n"
									 "( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) clip 0 0 0 1 1\n"
									 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) clip 0 0 0 1 1\n"
									 "( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) clip 0 0 0 1 1\n"
									 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) clip 0 0 0 1 1\n"
									 "( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) clip 0 0 0 1 1\n"
									 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) clip 0 0 0 1 1\n"
									 "}\n}\n";
			if (expect(writeFile(quakePath, quake), "Quake fixture")) {
				shell.openPathFromCommandLine(quakePath);
				action(QStringLiteral("shell.mode.levels"))->trigger();
				if (expect(until([&] { return shell.levelDocument().sourcePath == quakePath && shell.levelDocument().brushes.size() == 1; }), "Quake map ready")) {
					auto* quakeStatus = shell.findChild<QLabel*>(QStringLiteral("levelFilterMaterialStatus"));
					expect(!quakeStatus || quakeStatus->isHidden(), "a Quake map reads no shaders");
					// Quake's clip stops players and monsters alike.
					auto* monster = shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-monster-clip"));
					expect(monster && action(QStringLiteral("map.filterMonsterClip"))->isEnabled()
							&& until([&] { return monster->accessibleName().contains(QString::number(1)); }),
						"Quake's clip counts as monster clip", monster ? monster->accessibleName() : QString());
				}
			}
		}
	}
	if (ok) {
		std::cout << "Filter material kinds UI: shader reading, commands, keys, counts, reload and fallbacks passed." << std::endl;
	}
	return ok ? 0 : 1;
}
