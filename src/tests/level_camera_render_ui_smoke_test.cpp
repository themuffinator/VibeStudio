// View > Camera Display in the Levels page: Textures in Camera and Edges in
// Camera as saved commands with View tab and status-strip boxes kept in step,
// 3D Wireframe surviving preview rebuilds and restoring the Textures and Edges
// choices, TrenchBroom's Show Textures, Hide Textures and Hide Faces, and
// NetRadiant Custom's Next and Previous Camera Mode (Shift+] and Shift+[ in
// that profile) with their status announcements. Runs under the NetRadiant
// Custom and TrenchBroom profiles at 100% and at 200% with high contrast,
// right-to-left and expanded text; each shell starts with the display the
// previous one left, so the choices are shown to be saved. The commands never
// change the map or its undo history, and wait for the camera to be showing.
// With a renderer, the camera's edge overlay and wireframe are checked to
// draw a brush face's outline without its triangle diagonal. Owned offscreen
// widgets and semantic Qt events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_camera_render.h"
#include "core/render_device.h"
#include "core/studio_semantics.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QMenu>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif

#include <array>
#include <functional>
#include <iostream>
#include <utility>

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

bool until(const std::function<bool()>& ready, int timeoutMs = 45000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < timeoutMs) {
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

QString box(std::array<double, 3> lo, std::array<double, 3> hi)
{
	QStringList lines {QStringLiteral("{")};
	const auto point = [](const std::array<double, 3>& v) {
		return QStringLiteral("( %1 %2 %3 )").arg(v[0], 0, 'g', 10).arg(v[1], 0, 'g', 10).arg(v[2], 0, 'g', 10);
	};
	for (int axis = 0; axis < 3; ++axis) {
		for (const bool positive : {false, true}) {
			std::array<double, 3> p = lo;
			p[axis] = positive ? hi[axis] : lo[axis];
			auto a = p;
			auto b = p;
			int first = (axis + 1) % 3;
			int second = (axis + 2) % 3;
			if (positive) {
				std::swap(first, second);
			}
			a[first] += 1;
			b[second] += 1;
			lines << QStringLiteral("%1 %2 %3 base/wall 0 0 0 0.5 0.5 0 0 0").arg(point(p), point(a), point(b));
		}
	}
	lines << QStringLiteral("}");
	return lines.join(QLatin1Char('\n'));
}

QString describe(const LevelCameraRenderState& state)
{
	return QStringLiteral("textures=%1 hidden=%2 edges=%3").arg(int(state.textures)).arg(int(state.facesHidden)).arg(int(state.edges));
}

// Pixels that differ between two images of one size, inside `area`.
int differences(const QImage& a, const QImage& b, const QRect& area)
{
	if (a.size() != b.size()) {
		return -1;
	}
	int count = 0;
	const QRect clipped = area.intersected(a.rect());
	for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
		for (int x = clipped.left(); x <= clipped.right(); ++x) {
			count += a.pixel(x, y) != b.pixel(x, y) ? 1 : 0;
		}
	}
	return count;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	prepareRenderBackends();
	RenderDeviceInfo failure;
	const bool renderer = activeRenderDevice(&failure) != nullptr;
	if (!renderer) {
		std::cout << "No 3D renderer starts here; the camera's pixel checks are skipped. " << failure.error.toUtf8().constData() << std::endl;
	}
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QByteArray source = QStringLiteral("{\n\"classname\" \"worldspawn\"\n%1\n%2\n}\n")
								  .arg(box({0, 0, 0}, {64, 64, 64}), box({256, -32, 0}, {320, 32, 128}))
								  .toUtf8();
	const QString path = temp.filePath(QStringLiteral("camera.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "fixture")) {
		return 1;
	}
	file.close();

	// What each shell leaves behind for the next one to start with.
	const std::array<LevelCameraRenderState, 4> leaves {
		LevelCameraRenderState {false, false, true},
		LevelCameraRenderState {false, true, true},
		LevelCameraRenderState {true, false, true},
		LevelCameraRenderState {true, false, false},
	};
	LevelCameraRenderState expectedStart;
	int run = 0;
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			const bool nrc = QLatin1String(profile) == QLatin1String("netradiant-custom");
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Level camera render UI " << phase.toUtf8().constData() << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setTextScalePercent(scale);
			const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
			settings.setTheme(theme);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(scale == 100 ? 1700 : 2600, scale == 100 ? 1050 : 1800);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				action("shell.mode.levels")->trigger();
				action("map.layout.four-views")->trigger();
				auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				if (!expect(top && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && camera->hasMesh(); }),
						"map and camera ready")) {
					return 1;
				}
				const char* ids[] = {"map.previewTextures", "map.previewEdges", "map.previewWireframe", "map.showCameraTextures", "map.hideCameraTextures",
					"map.hideCameraFaces", "map.nextCameraMode", "map.previousCameraMode"};
				bool present = true;
				for (const char* id : ids) {
					present = expect(action(id) != nullptr, "the camera display command exists", QString::fromLatin1(id)) && present;
				}
				auto* viewTextured = shell.findChild<QCheckBox*>(QStringLiteral("levelViewTextured"));
				auto* viewEdges = shell.findChild<QCheckBox*>(QStringLiteral("levelViewEdges"));
				auto* viewWireframe = shell.findChild<QCheckBox*>(QStringLiteral("levelViewWireframe"));
				auto* stripTextured = shell.findChild<QCheckBox*>(QStringLiteral("levelPreviewTextured"));
				if (!expect(present && viewTextured && viewEdges && viewWireframe && stripTextured, "View tab and status-strip boxes exist")) {
					return 1;
				}
				const auto showing = [&](const LevelCameraRenderState& state) {
					const ModelViewportRenderMode mode = state.facesHidden ? ModelViewportRenderMode::Wireframe
						: state.textures                                   ? ModelViewportRenderMode::Textured
																		   : ModelViewportRenderMode::FlatShaded;
					return camera->renderMode() == mode && camera->showEdges() == (state.edges && !state.facesHidden) && camera->polygonEdges()
						&& action("map.previewTextures")->isChecked() == state.textures && action("map.previewEdges")->isChecked() == state.edges
						&& action("map.previewWireframe")->isChecked() == state.facesHidden && viewTextured->isChecked() == state.textures
						&& viewEdges->isChecked() == state.edges && viewWireframe->isChecked() == state.facesHidden
						&& stripTextured->isChecked() == state.textures;
				};
				const auto actual = [&] {
					return QStringLiteral("mode=%1 edges=%2 polygon=%3 checks=%4%5%6 boxes=%7%8%9 strip=%10")
						.arg(static_cast<int>(camera->renderMode()))
						.arg(int(camera->showEdges()))
						.arg(int(camera->polygonEdges()))
						.arg(int(action("map.previewTextures")->isChecked()))
						.arg(int(action("map.previewEdges")->isChecked()))
						.arg(int(action("map.previewWireframe")->isChecked()))
						.arg(int(viewTextured->isChecked()))
						.arg(int(viewEdges->isChecked()))
						.arg(int(viewWireframe->isChecked()))
						.arg(int(stripTextured->isChecked()));
				};
				expect(showing(expectedStart), "the saved camera display comes back", describe(expectedStart) + QStringLiteral(" / ") + actual());

				// Every command is a labelled View > Camera Display (or View) item
				// with a palette entry; profile keys follow NetRadiant Custom.
				for (const char* id : ids) {
					QAction* command = action(id);
					expect(!command->text().isEmpty() && !command->statusTip().isEmpty(), "command text and status tip", QString::fromLatin1(id));
					bool inMenu = false;
					for (QObject* owner : command->associatedObjects()) {
						inMenu = inMenu || qobject_cast<QMenu*>(owner) != nullptr;
					}
					expect(inMenu, "the command is in a menu", QString::fromLatin1(id));
					QString kebab;
					for (const QChar character : QString::fromLatin1(id)) {
						kebab += character.isUpper() ? QStringLiteral("-") + character.toLower() : QString(character);
					}
					// 3D Wireframe is listed among the shortcuts, with its W.
					CommandPaletteEntry entry;
					expect(commandPaletteEntryForCommandId(kebab, &entry) || shortcutForCommandId(kebab), "the command is in command search", kebab);
				}
				QMenu* displayMenu = nullptr;
				for (QObject* owner : action("map.nextCameraMode")->associatedObjects()) {
					if (auto* menu = qobject_cast<QMenu*>(owner)) {
						displayMenu = menu;
					}
				}
				expect(displayMenu && displayMenu->title().contains(QStringLiteral("Camera Display")), "View > Camera Display",
					displayMenu ? displayMenu->title() : QString());
				expect(nrc ? action("map.nextCameraMode")->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("Shift+]"))}
						   : action("map.nextCameraMode")->shortcuts().isEmpty(),
					"Shift+] steps the camera mode only in the NetRadiant Custom profile", action("map.nextCameraMode")->shortcut().toString());
				expect(nrc ? action("map.previousCameraMode")->shortcuts() == QList<QKeySequence> {QKeySequence(QStringLiteral("Shift+["))}
						   : action("map.previousCameraMode")->shortcuts().isEmpty(),
					"Shift+[ steps back only in the NetRadiant Custom profile", action("map.previousCameraMode")->shortcut().toString());
				for (const char* id : {"map.previewTextures", "map.previewEdges", "map.previewWireframe", "map.showCameraTextures", "map.hideCameraTextures",
						 "map.hideCameraFaces"}) {
					expect(action(id)->shortcuts().isEmpty(), "TrenchBroom and NetRadiant Custom give the face modes no key", QString::fromLatin1(id));
				}
				for (QCheckBox* box : {viewTextured, viewEdges, viewWireframe, stripTextured}) {
					QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(box);
					expect(accessible && accessible->role() == QAccessible::CheckBox && !accessible->text(QAccessible::Name).isEmpty()
							&& box->focusPolicy() != Qt::NoFocus && !box->toolTip().isEmpty(),
						"the box has a name, a role, a tooltip and keyboard focus", box->objectName());
				}
				expect(viewEdges->text() == QString(action("map.previewEdges")->text()).remove(QLatin1Char('&')), "the View tab box names the command",
					viewEdges->text());

				const QByteArray bytes = serializeLevelMap(shell.levelDocument()).bytes;
				// Each display command leaves the revision and undo history as they
				// were; TrenchBroom's selection history may add entries between them.
				const auto untouched = [&](const std::function<void()>& command, const QString& name) {
					const auto revision = shell.levelDocument().revision;
					const auto history = shell.levelDocument().undoStack.size();
					command();
					expect(shell.levelDocument().revision == revision && shell.levelDocument().undoStack.size() == history,
						"a display command adds no edit or undo step", name);
				};
				const auto apply = [&](const char* id, const LevelCameraRenderState& wanted, const char* label) {
					untouched([&] { action(id)->trigger(); }, QString::fromLatin1(id));
					expect(showing(wanted), label, describe(wanted) + QStringLiteral(" / ") + actual());
					expect(!shell.statusBar()->currentMessage().isEmpty(), "the command says what the camera now draws", QString::fromLatin1(id));
				};

				// Back to the defaults through the commands themselves.
				action("map.showCameraTextures")->trigger();
				if (action("map.previewEdges")->isChecked()) {
					action("map.previewEdges")->trigger();
				}
				expect(showing({true, false, false}), "textured faces without edges", actual());

				// Edges in Camera, then 3D Wireframe, which keeps both choices.
				apply("map.previewEdges", {true, false, true}, "Edges in Camera outlines the textured faces");
				expect(shell.statusBar()->currentMessage() == levelCameraRenderSummary({true, false, true}), "the edges are announced",
					shell.statusBar()->currentMessage());
				apply("map.previewWireframe", {true, true, true}, "3D Wireframe hides the faces");

				// An edit rebuilds the preview; wireframe stays.
				const QVector<LevelMapSelectionRef> far {{LevelMapSelectionKind::QuakeBrush, 1}};
				top->setSelectionSet(far);
				top->selectionSetChanged(far);
				expect(until([&] { return shell.levelDocument().selection.size() == 1; }), "the far brush is selected");
				QString error;
				LevelMapRotationRequest turn;
				turn.axis = 2;
				turn.degrees = 90;
				expect(shell.applyLevelRotation(turn, &error), "an edit to rebuild the preview", error);
				expect(!camera->isEnabled(), "the preview rebuilds after the edit");
				expect(until([&] { return camera->isEnabled() && camera->hasMesh(); }) && showing({true, true, true}),
					"wireframe outlasts a preview rebuild after an edit", actual());
				action("map.undo")->trigger();
				expect(until([&] { return camera->isEnabled() && serializeLevelMap(shell.levelDocument()).bytes == bytes; }) && showing({true, true, true}),
					"wireframe outlasts the undo's rebuild", actual());
				top->setSelectionSet({});
				top->selectionSetChanged({});
				expect(until([&] { return shell.levelDocument().selection.isEmpty(); }), "nothing is selected");

				apply("map.previewWireframe", {true, false, true}, "turning 3D Wireframe off restores textured faces with edges");

				// The status-strip box and the View tab box are one command.
				untouched([&] { stripTextured->setChecked(false); }, QStringLiteral("status-strip Textures"));
				expect(showing({false, false, true}), "the status-strip box turns textures off everywhere", actual());
				untouched([&] { viewTextured->setChecked(true); }, QStringLiteral("View tab Textures in Camera"));
				expect(showing({true, false, true}), "the View tab box turns them on, and the status-strip box follows", actual());
				untouched([&] { viewEdges->setChecked(false); }, QStringLiteral("View tab Edges in Camera"));
				expect(showing({true, false, false}), "the View tab box turns the edges off", actual());

				// TrenchBroom's face modes.
				apply("map.hideCameraFaces", {true, true, false}, "Hide Faces in Camera");
				apply("map.hideCameraTextures", {false, false, false}, "Hide Textures in Camera leaves wireframe for flat faces");
				apply("map.showCameraTextures", {true, false, false}, "Show Textures in Camera");
				apply("map.hideCameraFaces", {true, true, false}, "Hide Faces in Camera again");
				apply("map.showCameraTextures", {true, false, false}, "Show Textures in Camera leaves wireframe");

				// NetRadiant Custom's cycle, forward and back, wrapping.
				const auto step = [&](const char* id, LevelCameraDrawMode mode, const LevelCameraRenderState& wanted) {
					untouched([&] { action(id)->trigger(); }, QString::fromLatin1(id));
					expect(showing(wanted) && shell.statusBar()->currentMessage() == levelCameraDrawModeAnnouncement(mode), "a camera mode step",
						levelCameraDrawModeId(mode) + QStringLiteral(" / ") + actual() + QStringLiteral(" / ") + shell.statusBar()->currentMessage());
				};
				step("map.nextCameraMode", LevelCameraDrawMode::TexturedEdges, {true, false, true});
				step("map.nextCameraMode", LevelCameraDrawMode::Wireframe, {true, true, true});
				step("map.nextCameraMode", LevelCameraDrawMode::Flat, {false, false, false});
				step("map.nextCameraMode", LevelCameraDrawMode::Textured, {true, false, false});
				step("map.previousCameraMode", LevelCameraDrawMode::Flat, {false, false, false});
				step("map.previousCameraMode", LevelCameraDrawMode::Wireframe, {false, true, false});
				step("map.previousCameraMode", LevelCameraDrawMode::TexturedEdges, {true, false, true});
#ifdef VIBESTUDIO_HAVE_QTEST
				if (nrc) {
					// The profile's keys, pressed in the camera.
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					camera->setFocus(Qt::OtherFocusReason);
					if (expect(until([&] { return camera->hasFocus(); }), "camera focus")) {
						QTest::keyClick(camera, Qt::Key_BracketRight, Qt::ShiftModifier);
						expect(showing({true, true, true}), "Shift+] steps to wireframe", actual());
						QTest::keyClick(camera, Qt::Key_BracketLeft, Qt::ShiftModifier);
						expect(showing({true, false, true}), "Shift+[ steps back to textured with edges", actual());
					}
				}
#endif

				// With the camera hidden only the saved choices can change.
				action("map.layout.single-2d")->trigger();
				expect(until([&] { return !camera->isVisible() && !action("map.nextCameraMode")->isEnabled(); }), "the camera is hidden");
				for (const char* id : {"map.previewWireframe", "map.showCameraTextures", "map.hideCameraTextures", "map.hideCameraFaces", "map.nextCameraMode",
						 "map.previousCameraMode"}) {
					expect(!action(id)->isEnabled(), "waits for the camera to be showing", QString::fromLatin1(id));
				}
				expect(action("map.previewTextures")->isEnabled() && action("map.previewEdges")->isEnabled(), "the saved choices stay available");
				action("map.previewEdges")->trigger();
				expect(!action("map.previewEdges")->isChecked() && !camera->showEdges(), "Edges in Camera changes while the camera is hidden");
				action("map.previewEdges")->trigger();
				action("map.layout.four-views")->trigger();
				expect(until([&] { return camera->isVisible() && action("map.nextCameraMode")->isEnabled() && camera->isEnabled(); })
						&& showing({true, false, true}),
					"the camera comes back as it was", actual());

				// A brush face is outlined, not its two triangles: square on to the
				// first box's -Y face, whose fan diagonal crosses the centre.
				if (renderer && run == 0) {
					camera->setCameraView({32, -150, 32}, 90, 0);
					action("map.hideCameraTextures")->trigger();
					const auto picture = [&] {
						expect(tests::settleModelViewport(*camera), "the camera finishes drawing");
						QImage image(camera->size(), QImage::Format_ARGB32_Premultiplied);
						image.fill(Qt::transparent);
						camera->render(&image);
						return image;
					};
					const QImage outlined = picture();
					camera->setPolygonEdges(false);
					const QImage triangles = picture();
					camera->setPolygonEdges(true);
					action("map.previewEdges")->trigger();
					const QImage plain = picture();
					const QPoint centre = camera->rect().center();
					const QRect middle(centre - QPoint(6, 6), QSize(13, 13));
					expect(differences(outlined, plain, camera->rect()) > 0, "Edges in Camera draws the face outlines",
						QString::number(differences(outlined, plain, camera->rect())));
					expect(differences(outlined, plain, middle) == 0, "the outline leaves the face's diagonal out",
						QString::number(differences(outlined, plain, middle)));
					expect(differences(triangles, plain, middle) > 0, "triangle edges would draw the diagonal",
						QString::number(differences(triangles, plain, middle)));
					// The wireframe leaves the diagonal out too.
					action("map.previewWireframe")->trigger();
					const QImage wire = picture();
					camera->setPolygonEdges(false);
					const QImage wireTriangles = picture();
					camera->setPolygonEdges(true);
					expect(differences(wire, wireTriangles, middle) > 0 && differences(wire, plain, camera->rect()) > 0,
						"wireframe strokes polygon edges without the diagonal", QString::number(differences(wire, wireTriangles, middle)));
					std::cout << "Camera pixels: outline " << differences(outlined, plain, camera->rect()) << ", diagonal with triangle edges "
							  << differences(triangles, plain, middle) << ", wireframe diagonal " << differences(wireTriangles, wire, middle) << std::endl;
					action("map.previewWireframe")->trigger();
					action("map.previewEdges")->trigger();
					action("map.showCameraTextures")->trigger();
					expect(showing({true, false, true}), "the pixel checks leave textured faces with edges", actual());
				}

				expect(serializeLevelMap(shell.levelDocument()).bytes == bytes, "camera display never changes the map");

				// Leave a display for the next shell to bring back.
				const LevelCameraRenderState leave = leaves[size_t(run)];
				action(leave.textures ? "map.showCameraTextures" : "map.hideCameraTextures")->trigger();
				if (action("map.previewEdges")->isChecked() != leave.edges) {
					action("map.previewEdges")->trigger();
				}
				if (leave.facesHidden) {
					action("map.previewWireframe")->trigger();
				}
				expect(showing(leave), "the display to leave", describe(leave) + QStringLiteral(" / ") + actual());
				expectedStart = leave;
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
			++run;
		}
	}
	if (ok) {
		std::cout << "Level camera render UI: saved face modes, edges, wireframe across rebuilds, cycle and keys passed." << std::endl;
	}
	return ok ? 0 : 1;
}
