#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_paste.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n';
		ok = false;
	}
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
bool settle(ModelViewport* view)
{
	return until([&] {
		QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		view->render(&image);
		return view->isEnabled() && !view->isRendering();
	});
}
bool near(LevelMapVec3 a, LevelMapVec3 b)
{
	return a.valid && b.valid && std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) < 0.001;
}
class Expanded final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char*, const char* text, const char*, int) const override
	{
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(source, QString(source.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char** argv)
{
	// Owned offscreen windows, semantic action calls and QWidget render targets.
	// This test never queries, injects or captures operating-system input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!captures.isEmpty()) {
		QDir().mkpath(captures);
	}
	LevelMapDocument map;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	if (!createLevelMap(create, &map, &error) ||
	    !addLevelMapBoxBrush(&map, {0, 0, 0, true}, {32, 48, 64, true}, "studio/checker", nullptr, &error)) {
		return 1;
	}
	selectLevelMapObject(&map, "brush:0");
	const auto clipboard = levelMapSelectionText(map);
	const auto path = temp.filePath("paste.map");
	if (!expect(saveLevelMapAs(map, path, false, false).succeeded(), "save fixture")) {
		return 1;
	}
	for (const QString& profile :
	     {QStringLiteral("trenchbroom"), QStringLiteral("netradiant-custom"), QStringLiteral("vibestudio-default")}) {
		for (int scale : {100, 200}) {
			if (qEnvironmentVariableIsSet("VIBESTUDIO_PASTE_UI_QUICK") && (profile != "trenchbroom" || scale != 100)) {
				continue;
			}
			std::cout << "Paste UI: " << profile.toStdString() << ' ' << scale << " start" << std::endl;
			settings.setSelectedEditorProfileId(profile);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight);
			settings.setTextScalePercent(scale);
			settings.sync();
			Expanded translator;
			if (scale == 200) {
				app.installTranslator(&translator);
			}
			app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			{
				ApplicationShell shell;
				shell.resize(1800, 1200);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				const auto focus = [&](QWidget* pane) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					pane->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return pane->hasFocus(); }), "owned pane receives semantic focus");
				};
				action("shell.mode.levels")->trigger();
				auto* plan = shell.findChild<MapViewport*>("mapViewport");
				auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				if (!plan || !camera || !until([&] { return shell.levelDocument().brushes.size() == 1 && camera->isEnabled(); })) {
					return 1;
				}
				action("map.layout.four-views")->trigger();
				action("map.grid16")->trigger();
				if (!expect(settle(camera), "camera finishes rendering")) {
					return 1;
				}
				for (const auto* id : {"map.paste", "map.pasteAtOriginal", "map.pasteAtCursor", "map.pasteToCamera"}) {
					expect(action(id) && action(id)->isEnabled(), "paste action enabled on text map");
				}
				if (profile != "vibestudio-default") {
					expect(action(profile == "trenchbroom" ? "map.pasteAtOriginal" : "map.pasteToCamera")
					           ->toolTip()
					           .contains(profile == "trenchbroom" ? "Ctrl+Alt+V" : "Shift+V"),
					       "profile paste shortcut hint");
				}
				expect(camera->isPerspective() == (profile != "vibestudio-default"), "profile camera projection");
				QApplication::clipboard()->setText(clipboard);
				const auto base = serializeLevelMap(shell.levelDocument()).bytes;
				qsizetype undoDepth = shell.levelDocument().undoStack.size();
				const auto paste = [&](const char* id) { undoDepth = shell.levelDocument().undoStack.size(); action(id)->trigger(); };
				const auto checkAndUndo = [&](LevelMapVec3 minimum, const char* label) {
					const auto& document = shell.levelDocument();
					LevelMapVec3 low, high;
					const bool bounds = levelMapSelectionBounds(document, &low, &high);
					std::cout << label << " actual " << low.x << ',' << low.y << ',' << low.z << " objects " << document.brushes.size()
					          << " selection " << (document.selection.isEmpty() ? -1 : document.selection.first().objectId) << std::endl;
					expect(document.brushes.size() == 2 && document.undoStack.size() == undoDepth + 1 && bounds && near(low, minimum),
					       label,
					       shell.statusBar()->currentMessage() + QStringLiteral(" actual (%1,%2,%3), expected (%4,%5,%6)")
					                                                 .arg(low.x)
					                                                 .arg(low.y)
					                                                 .arg(low.z)
					                                                 .arg(minimum.x)
					                                                 .arg(minimum.y)
					                                                 .arg(minimum.z));
					action("map.undo")->trigger();
					expect(serializeLevelMap(shell.levelDocument()).bytes == base, "GUI single undo restores map");
					expect(settle(camera), "camera settles after paste undo");
				};
				paste("map.pasteAtOriginal");
				std::cout << "Original paste returned" << std::endl;
				checkAndUndo({0, 0, 0, true}, "explicit original position");
				// Semantic pointer coordinates exercise all plan projections without OS events.
				for (auto projection : {MapViewportProjection::TopXY, MapViewportProjection::FrontXZ, MapViewportProjection::SideZY}) {
					std::cout << "Plan projection " << int(projection) << std::endl;
					plan->setProjection(projection);
					plan->setGridSize(16);
					plan->setSnapToGrid(true);
					focus(plan);
					plan->selectSceneObjects({{LevelMapSelectionKind::QuakeBrush, 0}}, LevelScenePick::Replace);
					for (int zoom = 0; zoom < 8; ++zoom) {
						plan->zoomOut();
					}
					const auto pixel = plan->viewPointFor(83, -41);
					expect(plan->rect().contains(pixel.toPoint()), "semantic pointer lies inside the pane");
					plan->rememberCommandPointer(pixel);
					paste("map.pasteAtCursor");
					const LevelMapVec3 expected = projection == MapViewportProjection::TopXY     ? LevelMapVec3{80, -48, 0, true}
					                              : projection == MapViewportProjection::FrontXZ ? LevelMapVec3{80, 0, -48, true}
					                                                                             : LevelMapVec3{0, 80, -48, true};
					checkAndUndo(expected, "cursor paste follows projection and reference depth");
				}
				plan->setProjection(MapViewportProjection::TopXY);
				for (int zoom = 0; zoom < 8; ++zoom) {
					plan->zoomOut();
				}
				focus(plan);
				plan->rememberCommandPointer(plan->viewPointFor(83, -41));
				std::cout << "Profile before paste: " << StudioSettings().selectedEditorProfileId().toStdString() << " point "
				          << plan->commandPosition().x() << ',' << plan->commandPosition().y() << std::endl;
				paste("map.paste");
				checkAndUndo(profile == "trenchbroom" ? LevelMapVec3{80, -48, 0, true} : LevelMapVec3{0, 0, 0, true},
				             "default paste follows profile");
				if (camera->isPerspective()) {
					camera->setCameraView({200, 300, 400}, 0, 0);
				} else {
					camera->frameBounds({184, 284, 384}, {216, 316, 416});
				}
				std::cout << "Camera commands" << std::endl;
				expect(settle(camera), "set camera position");
				paste("map.pasteToCamera");
				checkAndUndo({192, 272, 368, true}, "camera command snaps delta from assembly centre");
				if (camera->isPerspective()) {
					camera->setCameraView({16, 24, 160}, 0, -90);
				} else {
					camera->frameBounds({0, 0, 0}, {32, 48, 64});
					camera->setOrbit(0, 90);
				}
				focus(camera);
				camera->rememberCommandPointer({camera->width() * 0.5, camera->height() * 0.5});
				expect(settle(camera), "aim camera at original brush");
				paste("map.pasteAtCursor");
				checkAndUndo({16, 32, 64, true}, "camera cursor lands on existing brush top");
				if (!camera->isPerspective()) {
					// Pan the orbit focus into empty space and keep its depth.
					camera->frameBounds({184, 284, 144}, {216, 316, 176});
					focus(camera);
					expect(settle(camera), "orbit camera focuses empty space");
					paste("map.pasteAtCursor");
					checkAndUndo({208, 304, 160, true}, "orbit miss uses its focus plane");
				}
				// Cancellation and the shell's stale-result guard cover the real modal worker.
				for (bool stale : {false, true}) {
					std::cout << "Cancel/stale " << stale << std::endl;
					bool visited = false;
					QTimer timer;
					timer.setSingleShot(true);
					QObject::connect(&timer, &QTimer::timeout, &shell, [&] {
						auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog");
						if (!dialog) {
							return;
						}
						visited = true;
						if (stale) {
							plan->selectSceneObjects({}, LevelScenePick::Replace);
						} else {
							dialog->reject();
						}
					});
					timer.start(0);
					paste("map.pasteAtOriginal");
					timer.stop();
					expect(visited && serializeLevelMap(shell.levelDocument()).bytes == base &&
					           shell.levelDocument().undoStack.size() == undoDepth + (stale && profile == "trenchbroom" ? 1 : 0),
					       "cancel/stale worker publishes nothing", shell.statusBar()->currentMessage());
				}
				shell.showLevelSidebarTab("tools");
				QWidget* clipboardBody = nullptr;
				for (auto* section : shell.findChildren<SidebarSection*>()) {
					if (section->sectionId() == QLatin1String("tools.clipboard")) {
						section->setExpanded(true);
						clipboardBody = section->body();
					}
				}
				expect(clipboardBody && until([&] { return clipboardBody->isVisible() && clipboardBody->height() > 0; }),
				       "clipboard section opens in the real Tools layout");
				if (clipboardBody && !captures.isEmpty()) {
					QEventLoop layout;
					QTimer::singleShot(50, &layout, &QEventLoop::quit);
					layout.exec(QEventLoop::ExcludeUserInputEvents);
					QImage image(clipboardBody->size(), QImage::Format_ARGB32_Premultiplied);
					clipboardBody->render(&image);
					expect(image.save(QDir(captures).filePath(QStringLiteral("%1-clipboard-panel-%2.png").arg(profile).arg(scale))),
					       "save actual clipboard section evidence");
				}
				for (const auto* id : {"map.pasteAtOriginal", "map.pasteAtCursor", "map.pasteToCamera"}) {
					auto* row = shell.findChild<QPushButton*>(QStringLiteral("levelToolsRow-") + QLatin1String(id));
					expect(row && row->focusPolicy() == Qt::TabFocus && !row->accessibleName().isEmpty() && !row->toolTip().isEmpty(),
					       "clipboard tool rows are keyboard accessible");
					if (row && !captures.isEmpty()) {
						QPushButton preview(row->icon(), row->text());
						preview.setFont(row->font());
						preview.setPalette(row->palette());
						preview.setLayoutDirection(row->layoutDirection());
						preview.ensurePolished();
						preview.resize(preview.sizeHint());
						QImage image(preview.size(), QImage::Format_ARGB32_Premultiplied);
						preview.render(&image);
						expect(
						    image.save(QDir(captures).filePath(QStringLiteral("%1-%2-%3.png").arg(profile, QLatin1String(id)).arg(scale))),
						    "save owned widget evidence");
					}
				}
				shell.hide();
			}
			if (scale == 200) {
				app.removeTranslator(&translator);
			}
		}
	}
	std::cout << (ok ? "Paste profile, plan/camera, history, cancellation, stale-state and accessibility checks passed.\n"
	                 : "Paste UI checks failed.\n");
	return ok ? 0 : 1;
}
