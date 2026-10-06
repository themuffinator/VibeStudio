#include "app/application_shell.h"
#include "app/level_primitive_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
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
	QElapsedTimer time;
	time.start();
	while (!condition() && time.elapsed() < 45000) {
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
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Direct Qt methods and widget rendering; no native input or OS capture.
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
	QString error;
	LevelMapDocument source;
	bool ok = expect(tests::createMaterialFixture(temp.path(), &source, &error), "original material fixture", error);
	PackageArchive archive;
	ok &= expect(archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error), "asset reader", error);
	const auto before = serializeLevelMap(source).bytes;
	const auto count = source.brushes.size();
	LevelBrushPrimitiveRequest request;
	request.shape = QStringLiteral("sphere");
	request.texture = QStringLiteral("studio/shader");
	request.mins = {-64, -64, -64, true};
	request.maxs = {64, 64, 64, true};
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
		LevelPrimitiveDialog dialog(source, request, std::make_shared<const PackageArchive>(archive));
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1750, 1150);
		}
		dialog.show();
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "primitive preview",
					 dialog.findChild<QLabel *>(QStringLiteral("primitiveStatus"))->text());
		auto *preview = dialog.findChild<ModelViewport *>();
		auto *apply = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
		auto *center = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("primitiveCenter0"));
		auto *shapes = dialog.findChild<QComboBox *>(QStringLiteral("primitiveShape"));
		ok &= expect(preview && preview->hasMesh() && preview->hasSkin() && preview->reducedMotion(),
					 "shared renderer and shader editor image");
		ok &= expect(dialog.previewDocument().brushes.size() == count + 1 && dialog.previewDocument().brushes.last().faceCount == 32 &&
						 serializeLevelMap(source).bytes == before,
					 "one sphere preview leaves source unchanged");
		ok &= expect(center && center->layoutDirection() == Qt::LeftToRight && !center->accessibleDescription().isEmpty() &&
						 QAccessible::queryAccessibleInterface(center)->role() == QAccessible::SpinBox &&
						 center->focusPolicy() != Qt::NoFocus,
					 "numeric accessibility and RTL");
		ok &= expect(shapes && shapes->count() == 5 && shapes->focusPolicy() != Qt::NoFocus && !shapes->accessibleName().isEmpty(),
					 "shape selector accessibility");
		ok &= expect(tests::settleModelViewport(*preview), "renderer settled");
		auto *scroll = dialog.findChild<QScrollArea *>();
		ok &= expect(scroll && scroll->horizontalScrollBar()->maximum() == 0, "translated controls fit without horizontal scrolling");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			QImage capture(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			capture.fill(Qt::transparent);
			dialog.render(&capture);
			ok &= expect(capture.save(QDir(captures).filePath(QStringLiteral("primitive-%1.png").arg(scale))), "widget render saved");
		}
		auto latest = request;
		latest.shape = QStringLiteral("cylinder");
		latest.sides = 16;
		dialog.setRequest(latest);
		latest.shape = QStringLiteral("cone");
		latest.sides = 5;
		dialog.setRequest(latest);
		ok &= expect(!apply->isEnabled() && until([&] { return dialog.isReady(); }) && dialog.previewValid() &&
						 dialog.previewDocument().brushes.last().faceCount == 6,
					 "latest queued draft wins");
		latest.shape = QStringLiteral("sphere");
		latest.sides = 64;
		latest.bands = 16;
		dialog.setRequest(latest);
		ok &=
			expect(until([&] { return dialog.isReady(); }) && !dialog.previewValid() && !apply->isEnabled(), "excess detail cannot apply");
		dialog.setRequest(request);
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "draft recovers after invalid controls");
		dialog.setApplyHandler([](const auto &, QString *failure) {
			*failure = QStringLiteral("changed draft");
			return false;
		});
		dialog.accept();
		ok &= expect(dialog.isVisible() && dialog.result() != QDialog::Accepted, "failed commit retains draft");
		const auto previewBytes = serializeLevelMap(dialog.previewDocument()).bytes;
		dialog.setApplyHandler([&](const LevelMapDocument &candidate, QString *) {
			return serializeLevelMap(candidate).bytes == previewBytes && candidate.undoStack.size() == source.undoStack.size() + 1;
		});
		dialog.accept();
		ok &= expect(dialog.result() == QDialog::Accepted, "valid draft accepted");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	StudioSettings settings;
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	LevelPrimitiveDialog offline(source, request);
	ok &= expect(until([&] { return offline.isReady(); }) && offline.previewValid(), "geometry authoring works without assets");
	offline.reject();
	LevelPrimitiveDialog cancelled(source, request);
	cancelled.reject();
	cancelled.setRequest(request);
	QEventLoop cancelledWait;
	QTimer::singleShot(250, &cancelledWait, &QEventLoop::quit);
	cancelledWait.exec(QEventLoop::ExcludeUserInputEvents);
	ok &= expect(!cancelled.isReady() && !cancelled.previewValid(), "cancel stops queued preview and prevents restart");
	ApplicationShell shell;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	ok &= expect(shell.createLevelDocument(create, &error), "new shell map", error);
	const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	auto *add = shell.findChild<QAction *>(QStringLiteral("map.addBrush"));
	if (!expect(add && add->isEnabled(), "Add Brush registered and enabled")) {
		return EXIT_FAILURE;
	}
	QTimer drive;
	drive.setInterval(30);
	int phase = 0;
	bool accepted = false, stale = false;
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto *modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("addBrushDialog")) {
			return;
		}
		auto *dialog = static_cast<LevelPrimitiveDialog *>(modal);
		if (phase == 0 || phase == 2) {
			dialog->setRequest(request);
			++phase;
			return;
		}
		if (!dialog->isReady()) {
			return;
		}
		drive.stop();
		if (phase == 1) {
			dialog->accept();
			accepted = dialog->result() == QDialog::Accepted;
		} else {
			if (shell.createLevelDocument(create, &error)) {
				dialog->accept();
				stale = dialog->isVisible() &&
						dialog->findChild<QLabel *>(QStringLiteral("primitiveStatus"))->text().contains(QStringLiteral("changed"));
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
	add->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(accepted && shell.levelDocument().brushes.size() == 1 && shell.levelDocument().brushes.first().faceCount == 32 &&
					 shell.levelDocument().undoStack.size() == 1,
				 "actual shell creates one sphere and undo step");
	const auto changed = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original, "actual shell exact undo");
	shell.findChild<QAction *>(QStringLiteral("map.redo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == changed, "actual shell exact redo");
	ok &= expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("sphere.map")), false, &error), "actual shell save",
				 error);
	phase = 2;
	drive.start();
	watchdog.start(60000);
	add->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(stale && shell.levelDocument().brushes.isEmpty(), "shell rejects draft from replaced map");
	ok &= expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("replacement.map")), false, &error), "save replacement",
				 error);
	shell.close();
	auto *pending = new LevelPrimitiveDialog(source, request, std::make_shared<const PackageArchive>(archive));
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
