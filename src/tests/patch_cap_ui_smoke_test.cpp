#include "app/application_shell.h"
#include "app/model_viewport.h"
#include "app/patch_cap_dialog.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/patch_cap_test_helpers.h"
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
#include <QStyleOptionComboBox>
#include <QTemporaryDir>
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
	// Test-owned Qt controls and render targets only. No user input or OS capture.
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
	ok &= expect(tests::createCapMap(&map, &error) && tests::createMaterialFixture(temp.path(), &ignored, &error), "original fixtures",
				 error);
	const auto before = serializeLevelMap(map).bytes;
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error) && staging.loadBaseArchive(archive, &error);
	const auto imagePath = QDir(temp.path()).filePath(QStringLiteral("replacement.png"));
	ok &= tests::putMaterialFile(imagePath, tests::materialImage(128, 64, true)) &&
		  staging.replaceFile(QStringLiteral("textures/studio/grid.png"), imagePath, &error);
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
		PatchCapDialog dialog(map, std::make_shared<PackageStagingArchive>(staging));
		if (scale == 200) {
			dialog.resize(1750, 1150);
			dialog.setLayoutDirection(Qt::RightToLeft);
		}
		dialog.show();
		auto *status = dialog.findChild<QPlainTextEdit *>(QStringLiteral("capStatus"));
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "ready two-cap preview", status->toPlainText());
		auto *preview = dialog.findChild<ModelViewport *>();
		auto *apply = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
		ok &= expect(dialog.capResult().caps.size() == 2 && preview->hasMesh() && preview->hasSkin() &&
						 preview->highlightedTriangleCount() > 0 && preview->reducedMotion() && apply->isEnabled(),
					 "staged materials and cap highlighting");
		for (auto *c : dialog.findChildren<QComboBox *>()) {
			ok &= expect(!c->accessibleName().isEmpty() && c->focusPolicy() != Qt::NoFocus &&
							 QAccessible::queryAccessibleInterface(c)->role() == QAccessible::ComboBox,
						 "named focusable selectors");
			QStyleOptionComboBox option;
			option.initFrom(c);
			option.editable = c->isEditable();
			option.frame = true;
			const auto field = c->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, c);
			ok &= expect(c->fontMetrics().horizontalAdvance(c->currentText()) <= field.width(),
						 "expanded selected text fits the combo field", c->objectName());
		}
		auto *view = dialog.findChild<QComboBox *>(QStringLiteral("capView"));
		const int combined = preview->mesh().triangleCount;
		view->setCurrentIndex(2);
		ok &= expect(until([&] { return dialog.isReady(); }) && preview->mesh().triangleCount < combined &&
						 preview->highlightedTriangleCount() == 0,
					 "original view has no cap geometry");
		view->setCurrentIndex(1);
		ok &= expect(until([&] { return dialog.isReady(); }) && preview->highlightedTriangleCount() == preview->mesh().triangleCount,
					 "caps-only view");
		auto *custom = dialog.findChild<QCheckBox *>(QStringLiteral("capCustomCenter"));
		custom->setChecked(true);
		ok &= expect(!apply->isEnabled() && until([&] { return dialog.isReady(); }) && !dialog.previewValid(),
					 "off-plane custom center disables apply");
		dialog.findChild<QDoubleSpinBox *>(QStringLiteral("capCenterZ"))->setValue(-64);
		dialog.findChild<QComboBox *>(QStringLiteral("capUv"))->setCurrentIndex(1);
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid() && dialog.capResult().caps.size() == 1,
					 "custom single center and boundary UVs", status->toPlainText());
		custom->setChecked(false);
		dialog.findChild<QCheckBox *>(QStringLiteral("capOpposite"))->setChecked(true);
		dialog.findChild<QComboBox *>(QStringLiteral("capUv"))->setCurrentIndex(0);
		view->setCurrentIndex(0);
		ok &= until([&] { return dialog.isReady(); }) && tests::settleModelViewport(*preview);
		for (auto *scroll : dialog.findChildren<QScrollArea *>()) {
			ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, "expanded controls fit horizontally");
		}
		ok &= expect(status->isReadOnly() && status->focusPolicy() != Qt::NoFocus && status->horizontalScrollBar()->maximum() == 0,
					 "status is selectable and wraps");
		const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!capture.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= image.save(QDir(capture).filePath(QStringLiteral("caps-%1.png").arg(scale)));
			if (scale == 200) {
				dialog.findChild<QScrollArea *>()->ensureWidgetVisible(status);
				QApplication::processEvents();
				dialog.render(&image);
				ok &= image.save(QDir(capture).filePath(QStringLiteral("caps-200-status.png")));
			}
		}
		ok &= expect(serializeLevelMap(map).bytes == before, "preview leaves live map untouched");
		dialog.setApplyHandler([](int, const auto &, QString *error) {
			*error = QStringLiteral("changed source");
			return false;
		});
		dialog.accept();
		ok &= expect(dialog.isVisible(), "failed apply keeps review open");
		auto edited = map;
		dialog.setApplyHandler(
			[&](int id, const auto &request, QString *error) { return capLevelMapPatch(&edited, id, request, nullptr, error); });
		dialog.accept();
		ok &= expect(dialog.result() == QDialog::Accepted && edited.patches.size() == 3, "reviewed caps apply");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	StudioSettings settings;
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ApplicationShell shell;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	ok &= shell.createLevelDocument(create, &error) && shell.applyLevelPatch(tests::capSource(), -1, &error);
	auto *action = shell.findChild<QAction *>(QStringLiteral("map.capPatch"));
	if (!expect(action && action->isEnabled(), "cap action enabled")) {
		return EXIT_FAILURE;
	}
	const auto shellBefore = serializeLevelMap(shell.levelDocument()).bytes;
	const auto depth = shell.levelDocument().undoStack.size();
	int phase = 0;
	bool accepted = false, stale = false;
	QTimer drive, watchdog;
	drive.setInterval(30);
	watchdog.setSingleShot(true);
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto *modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("patchCapDialog")) {
			return;
		}
		auto *dialog = static_cast<PatchCapDialog *>(modal);
		if (!dialog->isReady()) {
			return;
		}
		drive.stop();
		if (phase == 0) {
			dialog->accept();
			accepted = dialog->result() == QDialog::Accepted;
		} else {
			shell.createLevelDocument(create, &error);
			dialog->accept();
			stale = dialog->isVisible() &&
					dialog->findChild<QPlainTextEdit *>(QStringLiteral("capStatus"))->toPlainText().contains(QStringLiteral("changed"));
			dialog->reject();
		}
	});
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
		if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			d->reject();
		}
	});
	drive.start();
	watchdog.start(60000);
	action->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(accepted && shell.levelDocument().patches.size() == 3 && shell.levelDocument().undoStack.size() == depth + 1,
				 "shell adds caps in one undo");
	const auto shellAfter = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == shellBefore, "shell source restored by undo");
	shell.findChild<QAction *>(QStringLiteral("map.redo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == shellAfter, "shell redo");
	ok &= shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("caps.map")), false, &error);
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	phase = 1;
	ok &= expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("restored.map")), false, &error),
				 "clean source before stale-draft test", error);
	drive.start();
	watchdog.start(60000);
	action->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(stale && shell.levelDocument().patches.isEmpty(), "stale document rejected");
	ok &= shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("empty.map")), false, &error);
	shell.close();
	auto *pending = new PatchCapDialog(map, std::make_shared<PackageStagingArchive>(staging));
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
