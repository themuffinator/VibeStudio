#include "app/application_shell.h"
#include "app/level_rotation_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QImage>
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
bool expect(bool ok, const char *message, const QString &error = {})
{
	if (!ok) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return ok;
}
bool settle(LevelRotationDialog &dialog)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (!dialog.isReady() && elapsed.elapsed() < 45000) {
		QEventLoop events;
		QTimer::singleShot(30, &events, &QEventLoop::quit);
		events.exec();
	}
	return dialog.isReady();
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) != "LevelRotationDialog") {
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Direct Qt APIs and widget rendering; no simulated input or OS capture.
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
	LevelMapDocument map;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	QString error;
	bool ok = createLevelMap(create, &map, &error) &&
			  addLevelMapBoxBrush(&map, {0, 0, 0, true}, {128, 64, 96, true}, QStringLiteral("studio/stone"));
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
	for (int face = 0; face < 6; ++face) {
		ok &= setLevelMapBrushFaceProperty(&map, 0, face, QStringLiteral("rotation"), QStringLiteral("15"), &error);
	}
	const auto unchanged = serializeLevelMap(map).bytes;
	for (int scale : {100, 200}) {
		std::cerr << "Rotation UI scale " << scale << std::endl;
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		StudioSettings settings;
		settings.setTheme(theme);
		settings.setReducedMotion(true);
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		LevelRotationDialog dialog(map);
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1550, 1000);
		}
		dialog.show();
		LevelMapRotationRequest request{1, 31.75, {20, 30, 40, true}, true, true};
		dialog.setRequest(request);
		ok &= expect(settle(dialog) && dialog.previewValid(), "async rotation preview",
					 dialog.findChild<QLabel *>(QStringLiteral("rotationStatus"))->text());
		auto *preview = dialog.findChild<ModelViewport *>();
		auto *degrees = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("rotationDegrees"));
		ok &= expect(preview && preview->hasMesh() && preview->reducedMotion() && degrees && degrees->layoutDirection() == Qt::LeftToRight,
					 "shared viewport and LTR numeric controls");
		ok &= expect(QAccessible::queryAccessibleInterface(degrees)->role() == QAccessible::SpinBox &&
						 !degrees->accessibleDescription().isEmpty() && degrees->focusPolicy() != Qt::NoFocus,
					 "numeric accessibility metadata and focus");
		ok &= expect(serializeLevelMap(map).bytes == unchanged, "preview does not mutate source");
		ok &= expect(tests::settleModelViewport(*preview), "render completes");
		ok &= expect(dialog.findChild<QScrollArea*>()->horizontalScrollBar()->maximum() == 0, "translated controls fit without horizontal clipping");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("rotation-%1.png").arg(scale))), "capture dialog");
		}
		// Queue replacements while a worker runs. Only the final request may enable Apply.
		for (int i = 0; i < 8; ++i) {
			request.degrees = 10 + i;
			dialog.setRequest(request);
		}
		ok &= expect(!dialog.isReady() && settle(dialog) && dialog.previewValid() && dialog.request().degrees == 17, "latest preview wins");
		request.degrees = 360;
		dialog.setRequest(request);
		ok &= expect(settle(dialog) && !dialog.previewValid() &&
						 !dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),
					 "invalid rotation cannot apply");
		request.degrees = 31.75;
		dialog.setRequest(request);
		settle(dialog);
		dialog.setApplyHandler([](const auto &, QString *problem) {
			*problem = QStringLiteral("Map changed during preview.");
			return false;
		});
		dialog.accept();
		ok &= expect(dialog.isVisible() &&
						 dialog.findChild<QLabel *>(QStringLiteral("rotationStatus"))->text().contains(QStringLiteral("Map changed")),
					 "failed commit retains draft");
		dialog.setApplyHandler({});
		dialog.accept();
		ok &= expect(dialog.result() == QDialog::Accepted, "valid preview accepted");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ApplicationShell shell;
	create.starterRoom = true;
	ok &= expect(shell.createLevelDocument(create, &error), "shell room", error);
	auto *select = shell.findChild<QAction *>(QStringLiteral("map.selectAll"));
	auto *rotate = shell.findChild<QAction *>(QStringLiteral("map.rotatePrecisely"));
	if (!expect(select && rotate, "registered rotation action")) {
		return EXIT_FAILURE;
	}
	select->trigger();
	LevelMapRotationRequest request{2, 31.75, {0, 0, 0, true}, true, true};
	ok &= expect(shell.applyLevelRotation(request, &error) && shell.levelDocument().undoStack.size() == 1,
				 "shell shared rotation and single undo", error);
	const auto rotated = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	shell.findChild<QAction *>(QStringLiteral("map.redo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == rotated, "shell undo redo");
	const auto output = QDir(temp.path()).filePath(QStringLiteral("rotated.map"));
	ok &= expect(shell.saveLevelDocument(output, false, &error), "shell save", error);
	select->trigger();
	// Open the actual command and change the document after preview. This
	// exercises the shell's load/revision guard without input injection.
	QTimer drive;
	drive.setInterval(30);
	int phase = 0;
	bool staleRejected = false;
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto *modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("levelRotationDialog")) {
			return;
		}
		auto *dialog = static_cast<LevelRotationDialog *>(modal);
		if (phase == 0) {
			dialog->setRequest(request);
			phase = 1;
			return;
		}
		if (!dialog->isReady()) {
			return;
		}
		drive.stop();
		create.starterRoom = false;
		if (shell.createLevelDocument(create, &error)) {
			dialog->accept();
			staleRejected = dialog->isVisible() &&
							dialog->findChild<QLabel *>(QStringLiteral("rotationStatus"))->text().contains(QStringLiteral("changed"));
		}
		dialog->reject();
	});
	QTimer watchdog;
	watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
		if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			modal->reject();
		}
	});
	drive.start();
	watchdog.start(60000);
	rotate->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(staleRejected, "actual shell rejects stale rotation draft", error);
	ok &= expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("replacement.map")), false, &error), "save replacement",
				 error);
	ok &= expect(shell.close(), "clean shell closure");
	// Destruction while an immutable preview worker is pending must be safe.
	{
		LevelRotationDialog abandoned(map);
		abandoned.setRequest(request);
		app.processEvents();
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
