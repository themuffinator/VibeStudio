#include "app/application_shell.h"
#include "app/level_bookmarks_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QUuid>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* label, const QString& detail = {})
{
	if (!condition) { std::cerr << label << ": " << detail.toStdString() << '\n'; }
	return condition;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
}
bool near(double a, double b) { return std::abs(a - b) < 0.001; }
QByteArray encode(const LevelViewBookmarks& views) { return QJsonDocument(levelBookmarksJson(views)).toJson(); }
class Expansion final : public QTranslator {
public:
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace

int main(int argc, char** argv)
{
	// Service calls, Qt signals and QWidget::render only; no input injection or
	// OS capture. Files and settings are independent disposable fixtures.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.setRestoreSession(false);
	settings.setSelectedEditorProfileId(QStringLiteral("netradiant-custom"));
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	QString error;
	LevelMapDocument fixture;
	bool ok = expect(tests::createMaterialFixture(temp.path(), &fixture, &error), "material/model fixture", error);
	const auto mapPath = temp.filePath(QStringLiteral("arena.map"));
	ok &= expect(tests::putMaterialFile(mapPath, serializeLevelMap(fixture).bytes), "map fixture written");
	MapViewport plan;
	plan.resize(400, 300);
	plan.setDocument(fixture);
	const PlanViewState planState {2, QPointF(120, -64), 2.5};
	ok &= expect(plan.restoreNavigationState(planState) && plan.navigationState().center == planState.center && plan.zoom() == 2.5,
		"plan exact restore");
	auto invalidPlan = planState;
	invalidPlan.zoom = -1;
	ok &= expect(!plan.restoreNavigationState(invalidPlan) && plan.navigationState().center == planState.center && plan.zoom() == 2.5,
		"invalid plan leaves view intact");
	LevelViewBookmarks saved;
	{
		ApplicationShell shell;
		shell.resize(1700, 1100);
		shell.show();
		shell.openPathFromCommandLine(mapPath);
		shell.openPathFromCommandLine(temp.filePath(QStringLiteral("assets")));
		shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
		auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
		auto* side = shell.findChild<MapViewport*>(QStringLiteral("mapViewport2"));
		if (!camera || !top || !front || !side) { return EXIT_FAILURE; }
		ok &= expect(until([&] { return camera->isEnabled() && camera->hasMesh() && camera->hasSkin(); }), "current asset-backed camera ready");
		LevelViewState initial;
		ok &= expect(shell.captureLevelViewState(&initial, &error), "capture initial state", error);
		initial.layout = LevelViewLayout::FourViews;
		initial.activePlan = 1;
		initial.cameraVisible = true;
		initial.camera.position = {144, -320, 180};
		initial.camera.yaw = 67;
		initial.camera.pitch = -18;
		initial.camera.fieldOfView = 72;
		initial.plans[0].center = QPointF(32, 48);
		initial.plans[0].zoom = 2;
		initial.plans[1].center = QPointF(96, 80);
		initial.plans[1].zoom = 1.5;
		initial.plans[2].center = QPointF(72, 16);
		initial.plans[2].zoom = 0.75;
		const auto originalMap = serializeLevelMap(shell.levelDocument()).bytes;
		const auto undoSize = shell.levelDocument().undoStack.size();
		ok &= expect(shell.restoreLevelViewState(initial, &error) && shell.captureLevelBookmark(QStringLiteral("North gate & courtyard"), &error), "capture named state", error);
		saved = shell.levelBookmarks();
		ok &= expect(saved.size() == 1 && front->isActivePane(), "bookmark and active plane");
		const auto id = saved[0].id;
		auto displaced = initial;
		displaced.layout = LevelViewLayout::Single2D;
		displaced.cameraVisible = false;
		displaced.activePlan = 2;
		displaced.plans[2].zoom = 4;
		displaced.camera.position = {-1000, -1000, 1000};
		ok &= expect(shell.restoreLevelViewState(displaced, &error) && shell.restoreLevelBookmark(id, &error), "restore after navigation", error);
		ok &= expect(near(camera->cameraPosition().x, 144) && near(camera->fieldOfView(), 72) && top->navigationState().center == initial.plans[0].center &&
			front->isActivePane() && near(side->zoom(), 0.75) && camera->isVisible(), "camera and all plan states restored");
		ok &= expect(settings.levelViewLayoutPreference() == QStringLiteral("four-views"), "bookmark restore does not rewrite layout preference");
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == originalMap && shell.levelDocument().undoStack.size() == undoSize, "navigation leaves map and undo unchanged");
		ok &= expect(!shell.captureLevelBookmark(QStringLiteral("north gate & COURTYARD"), &error) && shell.levelBookmarks().size() == 1, "duplicate name refused");
		auto* menu = shell.findChild<QToolButton*>(QStringLiteral("levelBookmarksButton"))->menu();
		menu->aboutToShow();
		ok &= expect(menu->actions().last()->text().contains(QStringLiteral("&&")) && menu->actions().last()->data() == id, "menu protects name ampersands and targets stable IDs");
		menu->actions().last()->trigger();
		// Real command opens the manager. Drive its controls through Qt APIs.
		bool dialogRan = false;
		QTimer::singleShot(0, &shell, [&] {
			auto* dialog = shell.findChild<QDialog*>(QStringLiteral("levelBookmarksDialog"));
			if (!dialog) { ok &= expect(false, "manager command opens dialog"); return; }
			dialogRan = true;
			auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("levelBookmarkName"));
			name->setText(QStringLiteral("Upper walkway"));
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkCapture"))->click();
			name->setText(QStringLiteral("Walkway approach"));
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkRename"))->click();
			camera->setCameraView({224, -192, 176}, 10, -5);
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkUpdate"))->click();
			camera->setCameraView({-512, -512, 512}, 45, -30);
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkGo"))->click();
			ok &= expect(near(camera->cameraPosition().x, 224) && near(camera->cameraYaw(), 10), "manager update replaces pose");
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkSave"))->click();
			if (dialog->isVisible()) { ok &= expect(false, "manager save succeeds", dialog->findChild<QLabel*>(QStringLiteral("levelBookmarkStatus"))->text()); dialog->reject(); }
		});
		shell.findChild<QAction*>(QStringLiteral("map.manageViews"))->trigger();
		ok &= expect(dialogRan && shell.levelBookmarks().size() == 2 && shell.levelBookmarks()[1].name == QStringLiteral("Walkway approach"), "manager changes durable collection");
		shell.findChild<QAction*>(QStringLiteral("map.previousSavedView"))->trigger();
		shell.findChild<QAction*>(QStringLiteral("map.nextSavedView"))->trigger();
		// An outside CLI-like writer must not be lost to the open shell.
		const auto store = levelBookmarkStorePath(mapPath);
		LevelViewBookmarks external;
		QByteArray revision;
		ok &= expect(readLevelBookmarks(store, &external, &revision, &error), "external reads");
		external[0].name = QStringLiteral("Outside edit");
		ok &= expect(writeLevelBookmarks(store, external, revision, nullptr, &error), "external writes");
		ok &= expect(!shell.captureLevelBookmark(QStringLiteral("Stale"), &error) && shell.levelBookmarks().size() == 2, "stale GUI write refused");
		ok &= expect(shell.reloadLevelBookmarks(&error) && shell.levelBookmarks()[0].name == QStringLiteral("Outside edit"), "reload reconciles CLI changes");
		QTimer::singleShot(0, &shell, [&] {
			auto* dialog = static_cast<LevelBookmarksDialog*>(shell.findChild<QDialog*>(QStringLiteral("levelBookmarksDialog")));
			if (!dialog) { ok &= expect(false, "stale manager opens"); return; }
			dialog->findChild<QLineEdit*>(QStringLiteral("levelBookmarkName"))->setText(QStringLiteral("Local draft"));
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkRename"))->click();
			ok &= expect(readLevelBookmarks(store, &external, &revision, &error), "outside revision read");
			external[0].name = QStringLiteral("Latest outside edit");
			ok &= expect(writeLevelBookmarks(store, external, revision, nullptr, &error), "outside revision changed");
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkSave"))->click();
			ok &= expect(dialog->isVisible() && dialog->bookmarks()[0].name == QStringLiteral("Local draft") &&
				!dialog->findChild<QLabel*>(QStringLiteral("levelBookmarkStatus"))->text().isEmpty(), "failed manager save retains draft");
			ok &= expect(dialog->exportFile(temp.filePath(QStringLiteral("retained-draft.vviews")), false), "failed draft remains exportable");
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkReload"))->click();
			ok &= expect(dialog->bookmarks()[0].name == QStringLiteral("Latest outside edit"), "reload reconciles manager draft");
			dialog->findChild<QPushButton*>(QStringLiteral("levelBookmarkSave"))->click();
			if (dialog->isVisible()) { ok &= expect(false, "reconciled draft saves"); dialog->reject(); }
		});
		shell.findChild<QAction*>(QStringLiteral("map.manageViews"))->trigger();
		saved = shell.levelBookmarks();
		// Selection and normal authoring retain exactly one undo step.
		const QVector<LevelMapSelectionRef> selection {{LevelMapSelectionKind::QuakeBrush, 0}};
		front->setSelectionSet(selection);
		front->selectionSetChanged(selection);
		front->moveRequested(0, 0, 16);
		ok &= expect(shell.restoreLevelBookmark(id, &error) && shell.levelDocument().selection == selection && shell.levelDocument().undoStack.size() == undoSize + 1, "restore retains edited selection and undo");
		shell.findChild<QAction*>(QStringLiteral("map.undo"))->trigger();
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == originalMap, "normal undo remains exact");
		const auto copyPath = temp.filePath(QStringLiteral("copy.map"));
		ok &= expect(shell.saveLevelDocument(copyPath, false, &error), "save as copies views", error);
		ok &= expect(readLevelBookmarks(levelBookmarkStorePath(copyPath), &external, &revision, &error) && encode(external) == encode(saved), "save-as storage copied");
		const auto conflictPath = temp.filePath(QStringLiteral("conflict.map"));
		external[0].name = QStringLiteral("Destination views");
		ok &= expect(writeLevelBookmarks(levelBookmarkStorePath(conflictPath), external, {}, nullptr, &error), "existing destination metadata");
		const auto destinationViews = encode(external);
		ok &= expect(shell.saveLevelDocument(conflictPath, false, &error) && encode(shell.levelBookmarks()) == encode(saved), "map save succeeds and keeps uncopied source views", error);
		ok &= expect(readLevelBookmarks(levelBookmarkStorePath(conflictPath), &external, &revision, &error) && encode(external) == destinationViews &&
			shell.statusBar()->currentMessage().contains(QStringLiteral("need attention")), "save-as protects destination views and reports warning");
		ok &= expect(!shell.captureLevelBookmark(QStringLiteral("Unresolved destination"), &error) && shell.reloadLevelBookmarks(&error) &&
			encode(shell.levelBookmarks()) == destinationViews, "reload resolves destination conflict explicitly");
	}
	{
		ApplicationShell shell;
		shell.resize(1500, 1000);
		shell.show();
		shell.openPathFromCommandLine(mapPath);
		ok &= expect(encode(shell.levelBookmarks()) == encode(saved), "reopening restores view list");
		// Restore immediately, before async mesh completion, and require it to stick.
		ok &= expect(shell.restoreLevelBookmark(saved[0].id, &error), "early restore queued", error);
		auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		ok &= expect(near(camera->cameraPosition().x, 144) && near(camera->fieldOfView(), 72), "queued restore also updates camera immediately");
		camera->moveCamera(16, 0, 0);
		const auto laterPose = camera->cameraPosition();
		ok &= expect(until([&] { return camera->isEnabled() && camera->hasMesh(); }) && near(camera->cameraPosition().x, laterPose.x) &&
			near(camera->cameraPosition().y, laterPose.y) && near(camera->fieldOfView(), 72), "first preview retains newer navigation after queued restore");
		ok &= expect(shell.restoreLevelBookmark(saved[0].id, &error) && near(camera->cameraPosition().x, 144), "stored pose remains unchanged by navigation");
		LevelViewState orbit;
		ok &= expect(shell.captureLevelViewState(&orbit, &error), "capture for orbit", error);
		orbit.camera.perspective = false;
		orbit.camera.orbitYaw = 63;
		orbit.camera.orbitPitch = 25;
		orbit.camera.orbitScale = 1.25;
		ok &= expect(shell.restoreLevelViewState(orbit, &error), "restore orbit camera", error);
		const auto orbitBefore = camera->navigationState();
		camera->resize(700, 520);
		camera->setOrbit(0, 90);
		ok &= expect(camera->restoreNavigationState(orbitBefore), "orbit restore after resize");
		const auto orbitAfter = camera->navigationState();
		for (int i = 0; i < 3; ++i) { ok &= expect(near(orbitBefore.orbitTarget[i], orbitAfter.orbitTarget[i]), "world-space orbit target retained"); }
		auto invalid = orbitAfter;
		invalid.fieldOfView = 0;
		ok &= expect(!camera->restoreNavigationState(invalid) && camera->navigationState().fieldOfView == orbitAfter.fieldOfView, "invalid camera leaves view intact");
		LevelMapCreateRequest create;
		create.game = QStringLiteral("doom");
		ok &= expect(shell.createLevelDocument(create, &error) && shell.levelBookmarks().isEmpty(), "new Doom map has fresh view list", error);
		// Trigger preparation even if the current single view is hidden.
		LevelViewState state;
		shell.captureLevelViewState(&state, &error);
		ok &= expect(until([&] { return shell.captureLevelViewState(&state, &error); }), "Doom camera prepared");
		ok &= expect(shell.captureLevelBookmark(QStringLiteral("Sector overview"), &error), "unsaved map session view", error);
		const auto wadPath = temp.filePath(QStringLiteral("new.wad"));
		ok &= expect(shell.saveLevelDocument(wadPath, false, &error), "first save persists unsaved views", error);
		LevelViewBookmarks wadViews;
		QByteArray revision;
		ok &= expect(readLevelBookmarks(levelBookmarkStorePath(wadPath, QStringLiteral("MAP01")), &wadViews, &revision, &error) && wadViews.size() == 1, "WAD map-specific persistence");
	}
	// Manager layout, translation expansion and accessible Qt controls at two scales.
	for (int scale : {100, 200}) {
		Expansion expansion;
		if (scale == 200) { app.installTranslator(&expansion); app.setLayoutDirection(Qt::RightToLeft); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		LevelBookmarksDialog dialog(saved,
			[&](LevelViewState* state, QString*) { *state = saved[0].view; return true; },
			[](const LevelViewState&, QString*) { return true; },
			[](const LevelViewBookmarks&, QString*) { return true; },
			[&](LevelViewBookmarks* views, QString*) { *views = saved; return true; }, false);
		dialog.resize(scale == 200 ? 1500 : 760, scale == 200 ? 1100 : 600);
		dialog.show();
		app.processEvents();
		ok &= expect(QAccessible::queryAccessibleInterface(&dialog) && !dialog.accessibleDescription().isEmpty(), "manager accessible metadata");
		for (auto* button : dialog.findChildren<QPushButton*>()) { ok &= expect(button->focusPolicy() != Qt::NoFocus && button->width() >= button->sizeHint().width(), "buttons fit and accept keyboard focus"); }
		const auto output = temp.filePath(QStringLiteral("manager-%1.vviews").arg(scale));
		ok &= expect(dialog.exportFile(output, false) && dialog.importFile(output) && encode(dialog.bookmarks()) == encode(saved), "manager portable export/import");
		dialog.findChild<QPushButton*>(QStringLiteral("levelBookmarkRemove"))->click();
		ok &= expect(dialog.bookmarks().size() == saved.size() - 1 && dialog.bookmarks()[0].id == saved[1].id, "manager removal preserves other views");
		ok &= expect(dialog.importFile(output), "restore captured list for visual review");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("saved-views-%1.png").arg(scale))), "widget render saved");
		}
		if (scale == 200) { app.removeTranslator(&expansion); app.setLayoutDirection(Qt::LeftToRight); }
	}
	{
		int writes = 0;
		LevelBookmarksDialog dialog(saved,
			[&](LevelViewState* state, QString*) { *state = saved[0].view; return true; },
			[](const LevelViewState&, QString*) { return true; },
			[&](const LevelViewBookmarks&, QString*) { ++writes; return true; },
			[&](LevelViewBookmarks* views, QString*) { *views = saved; return true; }, true);
		const auto before = encode(dialog.bookmarks());
		ok &= expect(!dialog.findChild<QPushButton*>(QStringLiteral("levelBookmarkCapture"))->isEnabled() &&
			!dialog.findChild<QPushButton*>(QStringLiteral("levelBookmarkSave"))->isEnabled(), "read-only manager disables edits");
		dialog.findChild<QPushButton*>(QStringLiteral("levelBookmarkSave"))->click();
		ok &= expect(!dialog.importFile(temp.filePath(QStringLiteral("manager-100.vviews"))) && encode(dialog.bookmarks()) == before && writes == 0, "read-only manager protects list");
		ok &= expect(dialog.exportFile(temp.filePath(QStringLiteral("readonly-export.vviews")), false), "read-only manager still exports");
	}
	std::cout << (ok ? "Level bookmark UI passed.\n" : "Level bookmark UI failed.\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
