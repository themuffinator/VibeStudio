#include "app/application_shell.h"
#include "app/level_merge_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *label, const QString &error = {})
{
	if (!value) {
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()> &condition)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return condition();
}
class Expansion final : public QTranslator
{
  public:
	QString translate(const char *, const char *source, const char *, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Direct Qt methods and widget renders. No native mouse, keyboard or capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	QString error;
	LevelMapDocument ignored, document;
	bool ok = expect(tests::createMaterialFixture(temporary.path(), &ignored, &error), "original package materials", error);
	LevelMapCreateRequest create;
	create.starterRoom = false;
	ok &= createLevelMap(create, &document, &error);
	ok &= addLevelMapBoxBrush(&document, {-64, -32, -32, true}, {0, 32, 32, true}, QStringLiteral("studio/grid"));
	ok &= addLevelMapBoxBrush(&document, {0, -32, -32, true}, {64, 32, 32, true}, QStringLiteral("studio/shader"));
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}});
	const auto original = serializeLevelMap(document).bytes;
	PackageArchive archive;
	ok &= expect(archive.load(QDir(temporary.path()).filePath(QStringLiteral("assets")), &error), "open asset folder", error);
	PackageStagingModel staging;
	ok &= expect(staging.loadBaseArchive(archive, &error), "stage archive", error);
	const auto image = QDir(temporary.path()).filePath(QStringLiteral("replacement.png"));
	ok &= tests::putMaterialFile(image, tests::materialImage(128, 64, true));
	ok &= expect(staging.replaceFile(QStringLiteral("textures/studio/grid.png"), image, &error), "staged image replacement", error);
	for (int scale : {100, 200}) {
		StudioSettings settings;
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.setTextScalePercent(scale);
		settings.setReducedMotion(true);
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		LevelMergeDialog dialog(document, std::make_shared<PackageStagingArchive>(staging));
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1750, 1150);
		}
		dialog.show();
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.plan().prepared() && !dialog.plan().ready(),
					 "reviewable conflict preview", dialog.findChild<QLabel *>(QStringLiteral("mergeStatus"))->text());
		auto *preview = dialog.findChild<ModelViewport *>();
		auto *faces = dialog.findChild<QListWidget *>(QStringLiteral("mergeFaces"));
		auto *sources = dialog.findChild<QComboBox *>(QStringLiteral("mergeSource"));
		auto *apply = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
		ok &= expect(preview && preview->hasMesh() && preview->hasSkin() && preview->reducedMotion(),
					 "shared Models renderer and staged materials");
		ok &= expect(faces->count() == 6 && !apply->isEnabled() && dialog.plan().geometry().unresolvedCount() == 4,
					 "all coplanar material conflicts require a choice");
		ok &= expect(sources->focusPolicy() != Qt::NoFocus && !sources->accessibleName().isEmpty() &&
						 QAccessible::queryAccessibleInterface(sources)->role() == QAccessible::ComboBox,
					 "keyboard focus and accessible source selector");
		// Exercise real source controls, accepting a single conflict at a time.
		for (int f = 0; f < 6; ++f) {
			if (dialog.plan().geometry().faces[f].resolved) {
				continue;
			}
			faces->setCurrentRow(f);
			sources->setCurrentIndex(sources->count() - 1);
			ok &= expect(!apply->isEnabled() && until([&] { return dialog.isReady(); }), "choice schedules a new immutable preview");
		}
		ok &= expect(dialog.plan().ready() && apply->isEnabled() && serializeLevelMap(document).bytes == original,
					 "all choices ready without source mutation");
		faces->setCurrentRow(0); // The top surface is visible in the default orbit.
		ok &= expect(tests::settleModelViewport(*preview), "renderer settled");
		auto *scroll = dialog.findChild<QScrollArea *>();
		ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, "translated controls fit");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			QImage capture(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			capture.fill(Qt::transparent);
			dialog.render(&capture);
			ok &= expect(capture.save(QDir(captures).filePath(QStringLiteral("merge-%1.png").arg(scale))), "widget capture saved");
		}
		auto *view = dialog.findChild<QComboBox *>(QStringLiteral("mergeView"));
		ok &= expect(preview->highlightedTriangleCount() == 2, "selected result face is highlighted");
		view->setCurrentIndex(1);
		ok &= expect(until([&] { return dialog.isReady(); }) && preview->highlightedTriangleCount() == 4,
					 "source view highlights both contributing polygons");
		view->setCurrentIndex(0);
		view->setCurrentIndex(1);
		view->setCurrentIndex(0);
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.plan().ready(), "latest queued view retains source choices");
		dialog.setApplyHandler([](const auto &, QString *failure) {
			*failure = QStringLiteral("stale test");
			return false;
		});
		dialog.accept();
		ok &= expect(dialog.isVisible() && dialog.result() != QDialog::Accepted, "failed commit retains dialog");
		dialog.setApplyHandler([&](const auto &plan, QString *failure) {
			auto copy = document;
			return commitLevelBrushMerge(&copy, plan, failure);
		});
		dialog.accept();
		ok &= expect(dialog.result() == QDialog::Accepted, "reviewed merge accepted");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	StudioSettings settings;
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	// Actual shell path, including one undo step, selection restore and save.
	ApplicationShell shell;
	ok &= expect(shell.createLevelDocument(create, &error), "shell new map", error);
	LevelBrushPrimitiveRequest primitive;
	primitive.mins = {-64, -32, -32, true};
	primitive.maxs = {0, 32, 32, true};
	primitive.texture = QStringLiteral("studio/grid");
	ok &= shell.applyLevelBrushPrimitive(primitive, &error);
	primitive.mins.x = 0;
	primitive.maxs.x = 64;
	ok &= shell.applyLevelBrushPrimitive(primitive, &error);
	shell.findChild<QAction *>(QStringLiteral("map.selectAll"))->trigger();
	auto *merge = shell.findChild<QAction *>(QStringLiteral("map.mergeBrushes"));
	if (!expect(merge && merge->isEnabled(), "merge action registered and enabled")) {
		return EXIT_FAILURE;
	}
	const auto before = serializeLevelMap(shell.levelDocument()).bytes;
	const auto undoDepth = shell.levelDocument().undoStack.size();
	const auto selection = shell.levelDocument().selection;
	bool accepted = false, stale = false;
	int phase = 0;
	QTimer drive;
	drive.setInterval(30);
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto *modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("mergeBrushesDialog")) {
			return;
		}
		auto *dialog = static_cast<LevelMergeDialog *>(modal);
		if (!dialog->isReady()) {
			return;
		}
		drive.stop();
		if (phase == 0) {
			dialog->accept();
			accepted = dialog->result() == QDialog::Accepted;
		} else {
			if (shell.createLevelDocument(create, &error)) {
				dialog->accept();
				stale = dialog->isVisible() &&
						dialog->findChild<QLabel *>(QStringLiteral("mergeStatus"))->text().contains(QStringLiteral("changed"));
			}
			dialog->reject();
		}
	});
	QTimer watchdog;
	watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
		if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			dialog->reject();
		}
	});
	drive.start();
	watchdog.start(60000);
	merge->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(accepted && shell.levelDocument().brushes.size() == 1 && shell.levelDocument().undoStack.size() == undoDepth + 1,
				 "shell merge one undo step");
	const auto after = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before && shell.levelDocument().selection == selection,
				 "shell exact undo and selection");
	shell.findChild<QAction *>(QStringLiteral("map.redo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == after, "shell exact redo");
	ok &=
		expect(shell.saveLevelDocument(QDir(temporary.path()).filePath(QStringLiteral("merged.map")), false, &error), "shell save", error);
	// Create a second adjacent solid, then replace the document under its preview.
	primitive.mins.x = 64;
	primitive.maxs.x = 128;
	ok &= shell.applyLevelBrushPrimitive(primitive, &error);
	shell.findChild<QAction *>(QStringLiteral("map.selectAll"))->trigger();
	ok &= expect(shell.saveLevelDocument(QDir(temporary.path()).filePath(QStringLiteral("before-replacement.map")), false, &error),
				 "save before replacing map under preview", error);
	phase = 1;
	drive.start();
	watchdog.start(60000);
	merge->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(stale && shell.levelDocument().brushes.isEmpty(), "shell rejects a preview from a replaced map");
	ok &= shell.saveLevelDocument(QDir(temporary.path()).filePath(QStringLiteral("replacement.map")), false, &error);
	shell.close();
	auto *pending = new LevelMergeDialog(document, std::make_shared<PackageStagingArchive>(staging));
	pending->show();
	QEventLoop start;
	QTimer::singleShot(130, &start, &QEventLoop::quit);
	start.exec();
	delete pending;
	QEventLoop finish;
	QTimer::singleShot(200, &finish, &QEventLoop::quit);
	finish.exec();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
