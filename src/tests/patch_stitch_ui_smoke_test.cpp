#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/patch_stitch_dialog.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/patch_stitch_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *text, const QString &error = {})
{
	if (!value) {
		std::cerr << text << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()> &ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
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
	// Direct Qt calls and widget renders only; no injected input or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	bool ok = true;
	QString error;
	LevelMapDocument map, ignored;
	ok &= expect(tests::createStitchMap(&map, &error) && tests::createMaterialFixture(temp.path(), &ignored, &error),
				 "create original fixtures", error);
	const auto before = serializeLevelMap(map).bytes;
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error) && staging.loadBaseArchive(archive, &error);
	const auto replacement = QDir(temp.path()).filePath(QStringLiteral("replacement.png"));
	ok &= tests::putMaterialFile(replacement, tests::materialImage(128, 64, true)) &&
		  staging.replaceFile(QStringLiteral("textures/studio/grid.png"), replacement, &error);
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
		PatchStitchDialog dialog(map, std::make_shared<PackageStagingArchive>(staging));
		if (scale == 200) {
			dialog.resize(1750, 1150);
			dialog.setLayoutDirection(Qt::RightToLeft);
		}
		dialog.show();
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "ready seam preview",
					 dialog.findChild<QPlainTextEdit *>(QStringLiteral("stitchStatus"))->toPlainText());
		auto *preview = dialog.findChild<ModelViewport *>();
		auto *apply = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
		ok &= expect(preview->hasMesh() && preview->hasSkin() && preview->highlightedTriangleCount() > 0 && preview->reducedMotion(),
					 "staged material preview and boundary highlight");
		ok &= expect(dialog.stitchResult().boundaryPoints == 13 && apply->isEnabled(), "exact grid refinement enabled");
		for (auto *combo : dialog.findChildren<QComboBox *>()) {
			ok &= expect(!combo->accessibleName().isEmpty() && combo->focusPolicy() != Qt::NoFocus &&
							 QAccessible::queryAccessibleInterface(combo)->role() == QAccessible::ComboBox,
						 "accessible keyboard focus for each seam control");
		}
		auto *gap = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("stitchGap"));
		gap->setValue(1);
		ok &= expect(!dialog.isReady() && !apply->isEnabled() && until([&] { return dialog.isReady(); }) && !dialog.previewValid(),
					 "invalid draft cannot apply");
		gap->setValue(8);
		dialog.findChild<QCheckBox *>()->setChecked(true);
		dialog.findChild<QComboBox *>(QStringLiteral("stitchUv"))->setCurrentIndex(1);
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "coalesced tangent and UV preview",
					 dialog.findChild<QPlainTextEdit *>(QStringLiteral("stitchStatus"))->toPlainText());
		const int triangles = preview->mesh().triangleCount;
		auto *view = dialog.findChild<QComboBox *>(QStringLiteral("stitchView"));
		view->setCurrentIndex(1);
		ok &= expect(until([&] { return dialog.isReady(); }) && preview->mesh().triangleCount < triangles,
					 "original view exposes unrefined inputs");
		view->setCurrentIndex(0);
		ok &= until([&] { return dialog.isReady(); });
		ok &= expect(tests::settleModelViewport(*preview), "renderer settles");
		for (auto *scroll : dialog.findChildren<QScrollArea *>()) {
			ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, "expanded controls fit without horizontal clipping");
		}
		auto *status = dialog.findChild<QPlainTextEdit *>(QStringLiteral("stitchStatus"));
		ok &= expect(status->isReadOnly() && status->focusPolicy() != Qt::NoFocus &&
						 QAccessible::queryAccessibleInterface(status)->state().readOnly,
					 "status remains accessible and selectable without editing");
		const auto seamStatus = status->toPlainText();
		status->setPlainText(seamStatus + '\n' + QString(400, QLatin1Char('x')));
		status->moveCursor(QTextCursor::End);
		status->ensureCursorVisible();
		QApplication::processEvents();
		ok &= expect(status->horizontalScrollBar()->maximum() == 0, "long status tokens wrap without horizontal scrolling");
		ok &= expect(status->document()->lastBlock().layout()->lineCount() > 1, "unbroken status token spans several visible-width lines");
		for (auto block = status->document()->begin(); block.isValid(); block = block.next()) {
			const auto *textLayout = block.layout();
			for (int i = 0; i < textLayout->lineCount(); ++i) {
				ok &= expect(textLayout->lineAt(i).naturalTextWidth() <= status->viewport()->width() + 1,
							 "every expanded status line fits its viewport");
			}
		}
		status->setPlainText(seamStatus);
		QApplication::processEvents();
		const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!capture.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= image.save(QDir(capture).filePath(QStringLiteral("stitch-%1.png").arg(scale)));
		}
		ok &= expect(serializeLevelMap(map).bytes == before, "preview does not edit document");
		dialog.setApplyHandler([](int, int, const auto &, QString *error) {
			*error = QStringLiteral("changed source");
			return false;
		});
		dialog.accept();
		ok &= expect(dialog.isVisible(), "failed apply retains review");
		auto edited = map;
		dialog.setApplyHandler([&](int a, int b, const auto &request, QString *error) {
			return stitchLevelMapPatches(&edited, a, b, request, nullptr, error);
		});
		dialog.accept();
		ok &= expect(dialog.result() == QDialog::Accepted && serializeLevelMap(edited).bytes != before, "reviewed seam applies");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	StudioSettings settings;
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		QByteArray shared = before;
		shared.replace("}\n}\n", "}}\n");
		LevelMapDocument source;
		ok &=
			expect(loadLevelMapBytes({QStringLiteral("shared.map"), {}, {}}, shared, &source, &error), "load shared source fixture", error);
		PatchStitchDialog dialog(source);
		dialog.show();
		ok &= expect(until([&] { return dialog.isReady(); }) && !dialog.previewValid() &&
						 !dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),
					 "source preflight disables unsupported edit before Apply");
		dialog.reject();
	}
	ApplicationShell shell;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	ok &= expect(shell.createLevelDocument(create, &error), "shell map", error);
	for (const auto &p : map.patches) {
		ok &= shell.applyLevelPatch(p, -1, &error);
	}
	shell.findChild<QAction *>(QStringLiteral("map.selectAll"))->trigger();
	auto *action = shell.findChild<QAction *>(QStringLiteral("map.stitchPatches"));
	if (!expect(action && action->isEnabled(), "stitch action enabled")) {
		return EXIT_FAILURE;
	}
	const auto shellBefore = serializeLevelMap(shell.levelDocument()).bytes;
	const auto depth = shell.levelDocument().undoStack.size();
	bool accepted = false, stale = false;
	int phase = 0;
	QTimer drive, watchdog;
	drive.setInterval(30);
	watchdog.setSingleShot(true);
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto *modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("patchStitchDialog")) {
			return;
		}
		auto *dialog = static_cast<PatchStitchDialog *>(modal);
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
				stale =
					dialog->isVisible() &&
					dialog->findChild<QPlainTextEdit *>(QStringLiteral("stitchStatus"))->toPlainText().contains(QStringLiteral("changed"));
			}
			dialog->reject();
		}
	});
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
		if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			modal->reject();
		}
	});
	drive.start();
	watchdog.start(60000);
	action->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(accepted && shell.levelDocument().undoStack.size() == depth + 1, "actual shell atomic apply");
	const auto shellAfter = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == shellBefore, "shell exact undo");
	shell.findChild<QAction *>(QStringLiteral("map.redo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == shellAfter, "shell exact redo");
	ok &=
		expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("seam.map")), false, &error), "shell persistence", error);
	phase = 1;
	drive.start();
	watchdog.start(60000);
	action->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(stale && shell.levelDocument().patches.isEmpty(), "map replacement invalidates modal seam draft");
	ok &= shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("replacement.map")), false, &error);
	shell.close();
	auto *pending = new PatchStitchDialog(map, std::make_shared<PackageStagingArchive>(staging));
	pending->show();
	QEventLoop tick;
	QTimer::singleShot(110, &tick, &QEventLoop::quit);
	tick.exec();
	delete pending;
	QEventLoop finish;
	QTimer::singleShot(200, &finish, &QEventLoop::quit);
	finish.exec();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
