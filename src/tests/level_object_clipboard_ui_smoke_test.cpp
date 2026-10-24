#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_object_clipboard_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
bool write(const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
class Expansion final : public QTranslator {
	QString translate(const char*, const char* text, const char*, int) const override {
		const auto source = QString::fromUtf8(text); return QStringLiteral("[%1%2]").arg(source, QString(source.size() / 3, '~'));
	}
};
}
int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and process-local clipboard only. No OS input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	const auto path = temp.filePath("objects.map"); const auto original = tests::objectClipboardFixture();
	if (!write(path, original)) { return 1; }
	for (const auto& profile : {QStringLiteral("trenchbroom"), QStringLiteral("netradiant-custom"), QStringLiteral("vibestudio-default")}) {
		std::cout << "Object clipboard UI " << profile.toStdString() << std::endl;
		settings.setSelectedEditorProfileId(profile); settings.sync(); ApplicationShell shell; shell.resize(1600, 1000); shell.show(); shell.openPathFromCommandLine(path);
		const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
		action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
		auto* top = shell.findChild<MapViewport*>("mapViewport");
		if (!expect(top && until([&] { return shell.levelDocument().sourcePath == path && top->isEnabled(); }), "map ready")) { return 1; }
		const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
		const auto select = [&](const QVector<LevelMapSelectionRef>& refs) {
			top->setSelectionSet(refs); top->selectionSetChanged(refs); top->setFocus(Qt::OtherFocusReason);
		};
		const QVector<LevelMapSelectionRef> selected{{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakePatch, 1}};
		select(selected); action("map.copySelection")->trigger(); const auto clipboard = QApplication::clipboard()->text();
		expect(clipboard.contains("\"classname\" \"func_door\"") && clipboard.contains("\"speed\" \"160\"") && bytes() == original,
			"object copy retains owner and source", shell.statusBar()->currentMessage());
		for (const auto* tool : {"map.vertexTool", "map.edgeTool", "map.faceTool", "map.clipTool"}) {
			action(tool)->trigger();
			expect(!action("map.cutSelection")->isEnabled(), "cut disabled during component or clip tool", QLatin1String(tool));
			action("map.cutSelection")->trigger(); expect(bytes() == original && QApplication::clipboard()->text() == clipboard, "modal cut leaves map and clipboard intact");
			action(tool)->trigger();
			expect(action("map.cutSelection")->isEnabled(), "cut restored when tool closes", QLatin1String(tool));
		}
		const auto beforeCut = shell.levelDocument().undoStack.size();
		action("map.cutSelection")->trigger(); const auto after = bytes();
		expect(shell.levelDocument().brushes.size() == 1 && shell.levelDocument().patches.size() == 1 && shell.levelDocument().undoStack.size() == beforeCut + 1
			&& QApplication::clipboard()->text() == clipboard, "cut whole children as one undo", shell.statusBar()->currentMessage());
		action("map.undo")->trigger(); expect(bytes() == original, "GUI exact undo");
		action("map.redo")->trigger(); expect(bytes() == after, "GUI exact redo"); action("map.undo")->trigger();
		select(selected); QApplication::clipboard()->setText("before worker");
		bool changed = false;
		QTimer::singleShot(0, &shell, [&] { changed = true; QApplication::clipboard()->setText("new clipboard owner"); });
		action("map.cutSelection")->trigger();
		expect(changed && bytes() == original && QApplication::clipboard()->text() == "new clipboard owner", "clipboard changes during preparation refuse cut", shell.statusBar()->currentMessage());
		select(selected); QApplication::clipboard()->setText("before selection change");
		QTimer::singleShot(0, &shell, [&] { select({{LevelMapSelectionKind::Entity, 2}}); });
		action("map.cutSelection")->trigger();
		expect(bytes() == original && QApplication::clipboard()->text() == "before selection change", "stale selected objects cannot publish", shell.statusBar()->currentMessage());
		if (profile == QStringLiteral("trenchbroom")) {
			select(selected); QApplication::clipboard()->setText("before selection history change");
			const auto history = shell.levelDocument().historyRevision;
			QTimer::singleShot(0, &shell, [&] { select({{LevelMapSelectionKind::Entity, 2}}); select(selected); });
			action("map.cutSelection")->trigger();
			expect(shell.levelDocument().selection == selected && shell.levelDocument().historyRevision > history && bytes() == original
				&& QApplication::clipboard()->text() == "before selection history change", "selection round trip invalidates prepared history", shell.statusBar()->currentMessage());
		}
		select(selected); QApplication::clipboard()->setText("before cancellation"); bool cancelled = false;
		QTimer::singleShot(0, &shell, [&] {
			if (auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog")) { cancelled = true; dialog->reject(); }
		});
		action("map.cutSelection")->trigger();
		expect(cancelled && bytes() == original && QApplication::clipboard()->text() == "before cancellation", "GUI cancellation preserves map and clipboard", shell.statusBar()->currentMessage());
		// Clipboard acceptance can synchronously notify observers. A tool change
		// at that point leaves a useful copy but must not apply the prepared deletion.
		select(selected); bool once = false;
		const auto connection = QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, &shell, [&] {
			if (!once) { once = true; action("map.vertexTool")->trigger(); }
		});
		action("map.cutSelection")->trigger(); QObject::disconnect(connection);
		expect(once && bytes() == original && QApplication::clipboard()->text() == clipboard, "tool change after copy retains objects", shell.statusBar()->currentMessage());
		action("map.vertexTool")->trigger();
	}
	QString error; LevelMapDocument large;
	if (!tests::createGeometryFixture(7000, &large, &error)) { return 1; }
	QVector<LevelMapSelectionRef> refs; for (const auto& brush : large.brushes) { refs << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, brush.id}; }
	setLevelMapSelection(&large, refs); LevelPlacementRequest request; request.operation = LevelPlacementOperation::Copy;
	for (int scale : {100, 200}) {
		settings.setTextScalePercent(scale); settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
		bool inspected = false; int beats = 0; QElapsedTimer elapsed; elapsed.start(); qint64 previous = 0, gap = 0;
		QTimer pulse; pulse.setInterval(5); QObject::connect(&pulse, &QTimer::timeout, &app, [&] {
			const auto now = elapsed.elapsed(); gap = std::max(gap, now - previous); previous = now; ++beats;
		}); pulse.start();
		QTimer inspect; inspect.setInterval(10); QObject::connect(&inspect, &QTimer::timeout, &app, [&] {
			for (auto* widget : QApplication::topLevelWidgets()) {
				auto* dialog = dynamic_cast<LevelPlacementTaskDialog*>(widget);
				if (!dialog || inspected) { continue; }
				inspected = true;
				// The same delayed dialog is inspected before it appears for a fast
				// machine, without depending on timing or synthesising input events.
				dialog->show(); dialog->adjustSize();
				auto* bar = dialog->findChild<QProgressBar*>("levelPlacementTaskProgress");
				auto* cancel = dialog->findChild<QPushButton*>("levelPlacementTaskCancel");
				expect(dialog->accessibleName() == dialog->windowTitle() && bar && QAccessible::queryAccessibleInterface(bar)->role() == QAccessible::ProgressBar
					&& cancel && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleDescription().isEmpty(), "copy progress and cancellation accessibility");
				const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!capture.isEmpty()) {
					QDir().mkpath(capture); QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
					expect(image.save(QDir(capture).filePath(QStringLiteral("object-copy-%1.png").arg(scale))), "render owned progress widget");
				}
				cancel->click(); break;
			}
		}); inspect.start();
		const auto result = LevelPlacementTaskDialog::prepare(nullptr, large, request);
		inspect.stop(); pulse.stop();
		expect(inspected && beats > 0 && gap < 1000 && result.cancelled && result.clipboardText.isEmpty(), "responsive cancellable copy worker", result.error);
		std::cout << scale << "% copy: " << beats << " heartbeats, max gap " << gap << " ms\n";
		if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
	}
	std::cout << (ok ? "Object clipboard GUI checks passed.\n" : "Object clipboard GUI checks failed.\n");
	return ok ? 0 : 1;
}
