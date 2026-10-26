// The texture filter kinds in the real shell, offscreen, in the NetRadiant
// Custom and TrenchBroom profiles at 100% (dark) and 200% (high contrast,
// expanded translation, right to left):
// - Area Portals, Cluster Portals, Structural, Light Grid, Bot Clip and Decals
//   are checkable View > Filters commands with palette entries and View tab
//   check boxes that name their counts; NetRadiant Custom's keys (Alt+3,
//   Alt+9, Ctrl+Shift+D, Alt+M, Shift+D) are installed there only, and with
//   Qt Test each reaches its command from a plan view;
// - switching Caulk off hides the brush caulked all over in every plan view
//   and leaves the caulk faces of mixed brushes out of the camera mesh, with
//   the face count in the status bar; switching it on brings them back;
// - a Quake map disables the Quake III kinds and keeps Area Portals and
//   Structural;
// - none of it changes the map, the selection or undo.
// Owned offscreen widgets, settings in a temporary folder and synthetic key
// events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_view_filters.h"
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
#include <QFont>
#include <QStatusBar>
#include <QTemporaryDir>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif
#include <QTimer>
#include <QTranslator>

#include <functional>
#include <iostream>
#include <tuple>

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

const QStringList newKinds {QStringLiteral("areaportals"), QStringLiteral("clusterportals"), QStringLiteral("structural"), QStringLiteral("lightgrid"),
	QStringLiteral("botclip"), QStringLiteral("decals")};

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
	const QString path = temp.filePath(QStringLiteral("kinds.map"));
	if (!expect(writeFile(path, tests::viewFilterTextureKindsFixture()), "fixture")) {
		return 1;
	}
	const QHash<QString, QString> nrcKeys {{QStringLiteral("areaportals"), QStringLiteral("Alt+3")},
		{QStringLiteral("clusterportals"), QStringLiteral("Alt+9")}, {QStringLiteral("structural"), QStringLiteral("Ctrl+Shift+D")},
		{QStringLiteral("botclip"), QStringLiteral("Alt+M")}, {QStringLiteral("decals"), QStringLiteral("Shift+D")}};
	// Caulk on mixed brushes: brush 14 and the door's brush 18 keep five
	// caulk faces each; brush 15 is caulk all over. Two triangles a face.
	const int mixedCaulkTriangles = 2 * 10;
	const int wholeCaulkTriangles = 2 * 6;

	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			const QString profileId = QString::fromLatin1(profile);
			phase = QStringLiteral("%1 %2%").arg(profileId).arg(scale);
			std::cout << "Filter texture kinds UI " << profile << ' ' << scale << '%' << std::endl;
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
				shell.openPathFromCommandLine(path);
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				const auto command = [&](const QString& kind) { return action(levelViewFilterCommandId(kind)); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				action(QStringLiteral("map.layout.four-views"))->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(view && camera && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 19; }),
						"map ready")) {
					return 1;
				}
				if (QAction* sidebar = action(QStringLiteral("map.sidebar.view"))) {
					sidebar->trigger();
				}
				const bool nrc = profileId == QLatin1String("netradiant-custom");

				// Each new kind is a described, checkable command with a palette
				// entry, a check box that names its count, and its profile key.
				for (const QString& kind : newKinds) {
					QAction* toggle = command(kind);
					if (!expect(toggle, "kind command exists", kind)) {
						continue;
					}
					CommandPaletteEntry entry;
					expect(toggle->isCheckable() && toggle->isChecked() && toggle->isEnabled() && !toggle->text().isEmpty() && !toggle->statusTip().isEmpty()
							&& commandPaletteEntryForCommandId(toggle->objectName(), &entry) && !entry.label.isEmpty(),
						"kind command is checked, enabled, described and in the palette", kind);
					const QList<QKeySequence> wanted = nrc && nrcKeys.contains(kind) ? QList<QKeySequence> {QKeySequence(nrcKeys.value(kind))} : QList<QKeySequence> {};
					expect(toggle->shortcuts() == wanted, "profile key", kind + QLatin1String(": ") + toggle->shortcut().toString());
					auto* box = shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-") + kind);
					const int count = static_cast<int>(levelViewFilterMatches(shell.levelDocument(), kind).size());
					expect(box && box->isChecked() && !box->toolTip().isEmpty() && !box->accessibleDescription().isEmpty() && box->focusPolicy() == Qt::TabFocus
							&& until([&] { return box->accessibleName().contains(QString::number(count)); }),
						"check box names the kind and its count", kind + QLatin1String(": ") + (box ? box->accessibleName() : QString()));
				}

				const QByteArray baseline = serializeLevelMap(shell.levelDocument()).bytes;
				const auto revision = shell.levelDocument().revision;
				const auto undo = shell.levelDocument().undoStack.size();
				const auto selection = shell.levelDocument().selection;
				const auto unchanged = [&] {
					return serializeLevelMap(shell.levelDocument()).bytes == baseline && shell.levelDocument().revision == revision
						&& shell.levelDocument().undoStack.size() == undo && shell.levelDocument().selection == selection;
				};

				// Caulk off: the brush caulked all over leaves every view and the
				// camera, and the camera drops the caulk faces of mixed brushes.
				const QString caulkTexture = QStringLiteral("common/caulk");
				expect(until([&] { return camera->isEnabled() && camera->hasMesh()
										  && cameraTriangles(camera, caulkTexture) == mixedCaulkTriangles + wholeCaulkTriangles; }),
					"the camera draws every caulk face", QString::number(cameraTriangles(camera, caulkTexture)));
				command(QStringLiteral("caulk"))->trigger();
				expect(view->filteredCount() == 1 && view->isHidden(LevelMapSelectionKind::QuakeBrush, 15) && !view->isHidden(LevelMapSelectionKind::QuakeBrush, 14),
					"Caulk hides the brush caulked all over in the plan views", QString::number(view->filteredCount()));
				expect(shell.statusBar()->currentMessage().contains(QString::number(10)), "the status bar counts the hidden faces",
					shell.statusBar()->currentMessage());
				expect(until([&] { return camera->isEnabled() && camera->hasMesh() && cameraTriangles(camera, caulkTexture) == 0
										  && cameraTriangles(camera, QStringLiteral("base/wall")) > 0; }),
					"the camera leaves out every caulk face and keeps the rest", QString::number(cameraTriangles(camera, caulkTexture)));
				command(QStringLiteral("caulk"))->trigger();
				expect(view->filteredCount() == 0
						&& until([&] { return camera->isEnabled() && cameraTriangles(camera, caulkTexture) == mixedCaulkTriangles + wholeCaulkTriangles; }),
					"Caulk on brings the faces back", QString::number(cameraTriangles(camera, caulkTexture)));

				// Structural and Detail split the brushes between them.
				command(QStringLiteral("structural"))->trigger();
				expect(view->filteredCount() == 17 && view->isHidden(LevelMapSelectionKind::QuakeBrush, 0) && !view->isHidden(LevelMapSelectionKind::QuakeBrush, 13)
						&& !view->isHidden(LevelMapSelectionKind::QuakeBrush, 17),
					"Structural hides every brush but the detail ones", QString::number(view->filteredCount()));
				action(QStringLiteral("map.resetFilters"))->trigger();
				expect(view->filteredCount() == 0, "Show Every Kind brings them back");

#ifdef VIBESTUDIO_HAVE_QTEST
				if (nrc && scale == 100) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
					const QVector<std::tuple<QString, Qt::Key, Qt::KeyboardModifiers>> presses {
						{QStringLiteral("areaportals"), Qt::Key_3, Qt::AltModifier}, {QStringLiteral("clusterportals"), Qt::Key_9, Qt::AltModifier},
						{QStringLiteral("structural"), Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier},
						{QStringLiteral("botclip"), Qt::Key_M, Qt::AltModifier}, {QStringLiteral("decals"), Qt::Key_D, Qt::ShiftModifier}};
					for (const auto& press : presses) {
						const QString kind = std::get<0>(press);
						QTest::keyClick(view, std::get<1>(press), std::get<2>(press));
						expect(until([&] { return !command(kind)->isChecked(); }, 3000), "the key hides its kind", kind);
						QTest::keyClick(view, std::get<1>(press), std::get<2>(press));
						expect(until([&] { return command(kind)->isChecked(); }, 3000), "the key shows it again", kind);
					}
					expect(view->filteredCount() == 0, "every key left its kind shown");
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

	// A Quake map offers Area Portals and Structural but not the Quake III kinds.
	{
		phase = QStringLiteral("quake");
		settings.setSelectedEditorProfileId(QStringLiteral("netradiant-custom"));
		settings.setTextScalePercent(100);
		settings.setTheme(StudioTheme::Dark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, 100));
		ApplicationShell shell;
		shell.resize(1600, 1000);
		shell.show();
		const QString quakePath = temp.filePath(QStringLiteral("quake.map"));
		const QByteArray quake = "{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n{\n"
								 "( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) skip 0 0 0 1 1\n"
								 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) ground1_6 0 0 0 1 1\n"
								 "( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) skip 0 0 0 1 1\n"
								 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) skip 0 0 0 1 1\n"
								 "( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) skip 0 0 0 1 1\n"
								 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) skip 0 0 0 1 1\n"
								 "}\n}\n";
		if (expect(writeFile(quakePath, quake), "Quake fixture")) {
			shell.openPathFromCommandLine(quakePath);
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			action(QStringLiteral("shell.mode.levels"))->trigger();
			if (expect(until([&] { return shell.levelDocument().sourcePath == quakePath && shell.levelDocument().brushes.size() == 1; }), "Quake map ready")) {
				for (const char* id : {"map.filterClusterportals", "map.filterLightgrid", "map.filterBotclip", "map.filterDecals"}) {
					expect(action(QString::fromLatin1(id)) && !action(QString::fromLatin1(id))->isEnabled(), "Quake III kinds are disabled", QString::fromLatin1(id));
				}
				expect(action(QStringLiteral("map.filterAreaportals"))->isEnabled() && action(QStringLiteral("map.filterStructural"))->isEnabled(),
					"Area Portals and Structural stay");
				expect(!shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-decals")) || !shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-decals"))->isVisible(),
					"no Decals check box for a Quake map");
				// Quake's skip is caulk: five faces of the one brush.
				const auto faces = levelViewFilterFaceMatches(shell.levelDocument(), QStringLiteral("caulk"));
				expect(faces.size() == 5 && levelViewFilterMatches(shell.levelDocument(), QStringLiteral("hint")).isEmpty(), "Quake's skip counts as caulk");
			}
		}
	}
	if (ok) {
		std::cout << "Filter texture kinds UI: commands, keys, caulk faces in the camera and Quake enablement passed." << std::endl;
	}
	return ok ? 0 : 1;
}
