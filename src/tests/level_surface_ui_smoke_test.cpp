#include "tests/level_object_test_helpers.h"
using vibestudio::tests::objectIndex;
using vibestudio::tests::setObjectCurrentRow;
using vibestudio::tests::setObjectSelected;
#include "app/application_shell.h"
#include "app/level_surface_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()> &ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop events;
		QTimer::singleShot(20, &events, &QEventLoop::quit);
		events.exec();
	}
	return ready();
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
	// Native Qt calls and QWidget render only: no input injection or OS capture.
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
	LevelMapDocument map;
	bool ok = expect(tests::createMaterialFixture(temp.path(), &map, &error), "fixture", error);
	PackageArchive archive;
	ok &= expect(archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error), "asset folder", error);
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}});
	const auto source = serializeLevelMap(map).bytes;
	const auto faces = levelMapSelectedSurfaces(map);
	for (int scale : {100, 200}) {
		std::cerr << "Surface UI at " << scale << "%\n";
		StudioSettings settings;
		settings.setTextScalePercent(scale);
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.setReducedMotion(true);
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		LevelSurfaceDialog dialog(map, faces, std::make_shared<const PackageArchive>(archive));
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1750, 1150);
		}
		dialog.show();
		dialog.setRequest({LevelSurfaceOperation::Fit, 2, 1});
		const bool prepared = until([&] { return dialog.isReady(); });
		ok &= expect(prepared && dialog.previewValid(), "package-backed fit preview",
					 dialog.findChild<QLabel *>(QStringLiteral("surfaceStatus"))->text());
		auto *preview = dialog.findChild<ModelViewport *>();
		auto *x = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("surfaceX"));
		auto *list = dialog.findChild<QListWidget *>();
		ok &= expect(preview && preview->hasMesh() && preview->hasSkin() && preview->reducedMotion(), "shared textured preview");
		ok &= expect(x && x->layoutDirection() == Qt::LeftToRight && !x->accessibleDescription().isEmpty() &&
						 QAccessible::queryAccessibleInterface(x)->role() == QAccessible::SpinBox && x->focusPolicy() != Qt::NoFocus,
					 "numeric accessible role and focus");
		ok &= expect(list && !list->accessibleName().isEmpty() && list->focusPolicy() != Qt::NoFocus &&
						 QAccessible::queryAccessibleInterface(list)->role() == QAccessible::List,
					 "face list keyboard and screen-reader metadata");
		ok &= expect(serializeLevelMap(map).bytes == source, "preview leaves map unchanged");
		ok &= expect(tests::settleModelViewport(*preview), "preview rendered");
		ok &= expect(dialog.findChild<QScrollArea *>()->horizontalScrollBar()->maximum() == 0, "expanded labels and RTL controls fit");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("surfaces-%1.png").arg(scale))), "widget capture");
		}
		for (int i = 0; i < 8; ++i) {
			dialog.setRequest({LevelSurfaceOperation::Shift, double(i), -double(i)});
		}
		ok &= expect(!dialog.isReady() && until([&] { return dialog.isReady(); }) && dialog.previewValid() && dialog.request().x == 7,
					 "latest request wins");
		dialog.setSelectedFaces({{0, 0}});
		dialog.setRequest({LevelSurfaceOperation::Scale, 0, 1});
		ok &= expect(until([&] { return dialog.isReady(); }) && !dialog.previewValid() &&
						 !dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled(),
					 "zero scale cannot apply");
		dialog.setRequest({LevelSurfaceOperation::Fit, 1, 1});
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid() && dialog.selectedFaces().size() == 1,
					 "single-face preview");
		dialog.setApplyHandler([](const auto &, QString *problem) {
			*problem = QStringLiteral("Map changed.");
			return false;
		});
		dialog.accept();
		ok &= expect(dialog.isVisible() &&
						 dialog.findChild<QLabel *>(QStringLiteral("surfaceStatus"))->text().contains(QStringLiteral("changed")),
					 "failed commit retains draft");
		dialog.setApplyHandler({});
		dialog.accept();
		ok &= expect(dialog.result() == QDialog::Accepted, "valid draft accepted");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	StudioSettings settings;
	settings.setTextScalePercent(100);
	settings.setTheme(StudioTheme::Dark);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		LevelSurfaceDialog dialog(map, faces);
		dialog.show();
		ok &= expect(until([&] { return dialog.isReady(); }) && !dialog.previewValid(), "missing image size blocks fit");
		dialog.setTextureSizeOverride({128, 64});
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "explicit size enables offline fit");
		dialog.reject();
	}
	ApplicationShell shell;
	LevelMapCreateRequest create;
	ok &= expect(shell.createLevelDocument(create, &error), "shell room", error);
	auto *select = shell.findChild<QAction *>(QStringLiteral("map.selectAll"));
	auto *align = shell.findChild<QAction *>(QStringLiteral("map.alignSurfaces"));
	if (!expect(select && align, "surface command registered")) {
		return EXIT_FAILURE;
	}
	select->trigger();
	ok &= expect(align->isEnabled(), "surface command enabled for brushes");
	const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	QTimer drive;
	drive.setInterval(30);
	int phase = 0;
	bool accepted = false, staleRejected = false;
	const auto driveDialog = [&] {
		auto *modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("levelSurfaceDialog")) {
			return;
		}
		auto *dialog = static_cast<LevelSurfaceDialog *>(modal);
		if (phase == 0 || phase == 2) {
			dialog->setTextureSizeOverride({128, 64});
			dialog->setRequest({LevelSurfaceOperation::Fit, 2, 1});
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
			create.starterRoom = false;
			if (shell.createLevelDocument(create, &error)) {
				dialog->accept();
				staleRejected = dialog->isVisible() &&
								dialog->findChild<QLabel *>(QStringLiteral("surfaceStatus"))->text().contains(QStringLiteral("changed"));
			}
			dialog->reject();
		}
	};
	QObject::connect(&drive, &QTimer::timeout, &shell, driveDialog);
	QTimer watchdog;
	watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
		if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			modal->reject();
		}
	});
	drive.start();
	watchdog.start(60000);
	align->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(accepted && shell.levelDocument().undoStack.size() == 1 && serializeLevelMap(shell.levelDocument()).bytes != original,
				 "actual shell applies batch as one undo");
	const auto changed = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original, "shell exact undo");
	shell.findChild<QAction *>(QStringLiteral("map.redo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == changed, "shell exact redo");
	ok &= expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("shell.map")), false, &error), "shell save", error);
	select->trigger();
	phase = 2;
	drive.start();
	watchdog.start(60000);
	align->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(staleRejected, "shell rejects a preview from a replaced document");
	ok &= expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("replacement.map")), false, &error), "save replacement",
				 error);
	const auto primitivePath = QDir(temp.path()).filePath(QStringLiteral("primitive.map"));
	ok &= expect(tests::putMaterialFile(primitivePath, tests::surfaceFixture(QStringLiteral("brushDef3"))), "primitive source");
	shell.openPathFromCommandLine(primitivePath);
	ok &= expect(until([&] { return shell.levelDocument().sourcePath == primitivePath; }), "shell opens primitive map");
	auto *objects = shell.findChild<LevelObjectList*>(QStringLiteral("levelMapObjects"));
	auto *inspector = shell.findChild<QTreeWidget *>(QStringLiteral("entityInspector"));
	QTreeWidgetItem *matrix = nullptr;
	int fields = 0;
	if (objects && inspector) {
		for (int i = 0; i < objects->model()->rowCount(); ++i) {
			if (objectIndex(objects, i).data(Qt::UserRole).toString() == QStringLiteral("brush:0")) {
				setObjectCurrentRow(objects, i, QItemSelectionModel::ClearAndSelect);
				break;
			}
		}
		for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
			const auto path = (*it)->data(0, Qt::UserRole + 11).toString();
			if (path == QStringLiteral("face:0:0:matrix")) {
				matrix = *it;
			}
			if (path.startsWith(QStringLiteral("face:0:0:matrix")) && (*it)->flags().testFlag(Qt::ItemIsEditable)) {
				++fields;
			}
		}
	}
	ok &= expect(matrix && fields == 7, "Inspector exposes six matrix elements and an atomic full matrix");
	if (matrix) {
		inspector->setCurrentItem(matrix);
		matrix->setText(1, QStringLiteral("0,0.015625,0.5,-0.03125,0,0.25"));
		ok &= expect(shell.levelDocument().brushes.first().faces.first().textureMatrix[2] == 0.5 &&
						 shell.levelDocument().undoStack.size() == 1,
					 "Inspector matrix commit uses map undo");
		bool focused = false;
		QTimer inspect;
		inspect.setInterval(20);
		QObject::connect(&inspect, &QTimer::timeout, &shell, [&] {
			auto *modal = QApplication::activeModalWidget();
			if (!modal || modal->objectName() != QStringLiteral("levelSurfaceDialog")) {
				return;
			}
			auto *dialog = static_cast<LevelSurfaceDialog *>(modal);
			focused = dialog->selectedFaces() == QVector<LevelSurfaceFace>{{0, 0}};
			inspect.stop();
			dialog->reject();
		});
		inspect.start();
		watchdog.start(60000);
		align->trigger();
		inspect.stop();
		watchdog.stop();
		ok &= expect(focused, "surface dialog starts from the Inspector's focused face");
		ok &= expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("primitive-edited.map")), false, &error),
					 "save matrix edit", error);
	}
	ok &= expect(shell.close(), "close clean shell");
	// Closing while a worker is pending is safe; the worker owns its snapshots.
	{
		LevelSurfaceDialog abandoned(map, faces, std::make_shared<const PackageArchive>(archive));
		QEventLoop events;
		QTimer::singleShot(140, &events, &QEventLoop::quit);
		events.exec();
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
