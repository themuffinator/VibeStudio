#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/studio_settings.h"
#include "tests/level_view_filter_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QInputDialog>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
void check(bool passed, const char* label) { if (!passed) { ok = false; std::cerr << label << '\n'; } }
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(20, &loop, &QEventLoop::quit); loop.exec(); }
	return ready();
}
bool write(const QString& path, const QByteArray& data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { const QString text = QString::fromUtf8(source); return text + QString(text.size() / 2, QLatin1Char('~')); }
};
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	const QString map = temp.filePath(QStringLiteral("test.map")), tags = temp.filePath(QStringLiteral("tags.json"));
	if (!write(map, tests::viewFilterTagsFixture()) || !write(tags, tests::viewFilterTagLayout())) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true);
	for (const char* profile : {"trenchbroom", "netradiant-custom"}) {
		for (const int scale : {100, 200}) {
			std::cout << profile << " " << scale << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile)); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setShellLayoutState(QStringLiteral("levelView/filterTagFile"), tags.toUtf8()); settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded translator;
			if (scale == 200) { app.installTranslator(&translator); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600, 1000); shell.show(); shell.openPathFromCommandLine(map);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				if (!until([&] { return shell.levelDocument().brushes.size() == 10; })) { return 1; }
				action("shell.mode.levels")->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				if (!view) { return 1; }
				const QVector<LevelMapSelectionRef> selection{{LevelMapSelectionKind::QuakeBrush, 5}};
				view->setSelectionSet(selection); view->selectionSetChanged(selection);
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto original = bytes(); const auto history = shell.levelDocument().undoStack.size();
				auto* make = action("map.makeTag.mist"); auto* clear = action("map.makeNonTag.mist");
				check(make && clear && !make->text().isEmpty() && !make->statusTip().isEmpty(), "generated edit actions");
				if (!make || !clear) { return 1; }
				check(until([&] { return make->isEnabled(); }), "selection enables smart tag");
				make->trigger();
				check(shell.levelDocument().brushes[5].faces[0].contentFlags == 64 && shell.levelDocument().undoStack.size() == history + 1, "GUI flag edit one step");
				action("map.undo")->trigger(); check(bytes() == original, "GUI undo restores map");
				action("map.redo")->trigger(); clear->trigger(); check(bytes() == original, "GUI clear tag");
				check(!action("map.makeNonTag.water"), "texture tag has no remove action");
				// A class choice requires a dialog. Cancel leaves the map untouched.
				bool dialogSeen = false;
				QTimer answer;
				QObject::connect(&answer, &QTimer::timeout, &shell, [&] {
					if (auto* dialog = shell.findChild<QInputDialog*>(QStringLiteral("levelSmartTagChoice"))) {
						if (dialog->isVisible()) { dialogSeen = true; check(!dialog->accessibleName().isEmpty(), "choice accessible"); dialog->reject(); }
					}
				});
				answer.start(20);
				action("map.makeTag.triggers")->trigger(); answer.stop();
				check(dialogSeen && bytes() == original, "choice cancel preserves map");
				// Choosing a class applies its configured material and owner in one step.
				QObject::disconnect(&answer, nullptr, &shell, nullptr);
				QObject::connect(&answer, &QTimer::timeout, &shell, [&] {
					if (auto* dialog = shell.findChild<QInputDialog*>(QStringLiteral("levelSmartTagChoice"))) {
						if (dialog->isVisible()) { dialog->setTextValue(QStringLiteral("trigger_multiple")); dialog->accept(); }
					}
				});
				const auto depth = shell.levelDocument().undoStack.size(); answer.start(20);
				action("map.makeTag.triggers")->trigger(); answer.stop();
				const auto& tiedDocument = shell.levelDocument();
				const auto tiedFaces = levelMapSelectedSurfaces(tiedDocument);
				const auto tiedBrush = std::find_if(tiedDocument.brushes.cbegin(), tiedDocument.brushes.cend(), [&](const auto& brush) {
					return !tiedFaces.isEmpty() && brush.id == tiedFaces.first().brushId;
				});
				check(tiedDocument.undoStack.size() == depth + 1 && tiedBrush != tiedDocument.brushes.cend() && tiedBrush->entityId != 0
					&& tiedBrush->faces[0].textureName == QStringLiteral("common/trigger"), "GUI classname and material");
				action("map.undo")->trigger(); check(bytes() == original, "GUI classname undo");
			}
			if (scale == 200) { app.removeTranslator(&translator); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	return ok ? 0 : 1;
}
