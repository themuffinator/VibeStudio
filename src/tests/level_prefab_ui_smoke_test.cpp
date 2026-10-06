#include "package_entry_test_helpers.h"
#include "app/application_shell.h"
#include "app/level_prefab_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
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
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Direct Qt methods and QWidget::render; no native input or screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setReducedMotion(true);
	QString error;
	LevelMapDocument source;
	bool ok = expect(tests::createMaterialFixture(temp.path(), &source, &error), "material and model fixture", error);
	QVector<LevelMapSelectionRef> selection;
	for (const auto &b : source.brushes) {
		selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, b.id};
	}
	for (const auto &p : source.patches) {
		selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakePatch, p.id};
	}
	selection << LevelMapSelectionRef{LevelMapSelectionKind::Entity, source.entities.last().id};
	setLevelMapSelection(&source, selection);
	const auto original = serializeLevelMap(source).bytes;
	PackageArchive archive;
	ok &= expect(archive.load(temp.filePath(QStringLiteral("assets")), &error), "package assets", error);
	PackageStagingModel staging;
	ok &= expect(staging.loadBaseArchive(archive, &error), "package draft", error);
	const auto prefabPath = temp.filePath(QStringLiteral("assembly.vprefab"));
	{
		LevelPrefabDialog capture(source, LevelPrefabDialogMode::Export, {}, false, std::make_shared<const PackageArchive>(archive));
		capture.findChild<QLineEdit *>(QStringLiteral("prefabName"))->setText(QStringLiteral("Material assembly"));
		capture.findChild<QLineEdit *>(QStringLiteral("prefabOutput"))->setText(prefabPath);
		capture.show();
		ok &= expect(until([&] { return capture.isReady(); }) && capture.previewValid(), "async capture",
					 capture.findChild<QLabel *>(QStringLiteral("prefabStatus"))->text());
		capture.accept();
		ok &= expect(until([&] {
						 return capture.result() == QDialog::Accepted ||
								capture.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled();
					 }) &&
						 capture.result() == QDialog::Accepted && capture.writeReport().committed && QFile::exists(prefabPath),
					 "async file publication", capture.findChild<QLabel *>(QStringLiteral("prefabStatus"))->text());
	}
	{
		LevelPrefabDialog stage(source, LevelPrefabDialogMode::Stage, {}, false, std::make_shared<const PackageArchive>(archive));
		bool applied = false;
		stage.setStageHandler([&](const QByteArray &bytes, const QString &path, bool replace, QString *failure) {
			applied = staging.addBytes(bytes, path, failure,
									   replace ? PackageStageConflictResolution::ReplaceExisting : PackageStageConflictResolution::Block);
			return applied;
		});
		ok &= expect(until([&] { return stage.isReady(); }) && stage.previewValid(), "stage preview");
		stage.accept();
		ok &= expect(applied && stage.result() == QDialog::Accepted && staging.operations().size() == 1 &&
						 serializeLevelMap(source).bytes == original,
					 "stage owns bytes without editing source map");
	}
	LevelMapDocument target;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &target);
	const auto targetBytes = serializeLevelMap(target).bytes;
	for (int scale : {100, 200}) {
		settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.setTextScalePercent(scale);
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
		}
		LevelPrefabDialog dialog(target, LevelPrefabDialogMode::Insert, QStringLiteral("prefabs/assembly.vprefab"), true,
								 std::make_shared<PackageStagingArchive>(staging));
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(1800, 1200);
		}
		dialog.show();
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "staged prefab preview",
					 dialog.findChild<QLabel *>(QStringLiteral("prefabStatus"))->text());
		auto *preview = dialog.findChild<ModelViewport *>();
		auto *position = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("prefabPosition0"));
		auto *apply = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
		ok &= expect(preview && preview->hasMesh() && preview->hasSkin() && preview->reducedMotion(), "shared textured model renderer");
		ok &= expect(dialog.previewDocument().brushes.size() == 2 && dialog.previewDocument().patches.size() == 1 &&
						 dialog.previewDocument().entities.size() == 2 && serializeLevelMap(target).bytes == targetBytes,
					 "immutable candidate contains complete assembly");
		ok &= expect(position->layoutDirection() == Qt::LeftToRight && !position->accessibleDescription().isEmpty() &&
						 position->focusPolicy() != Qt::NoFocus &&
						 QAccessible::queryAccessibleInterface(position)->role() == QAccessible::SpinBox,
					 "numeric accessibility and RTL");
		auto *scroll = dialog.findChild<QScrollArea *>();
		ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, "expanded controls need no horizontal scrolling");
		ok &= expect(tests::settleModelViewport(*preview), "preview render settled");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("prefab-%1.png").arg(scale))), "widget render saved");
		}
		LevelPrefabPlacement place;
		place.position = {64, 128, 32, true};
		place.rotation.z = 90;
		dialog.setPlacement(place);
		place.position.x = 192;
		dialog.setPlacement(place);
		ok &= expect(!apply->isEnabled() && until([&] { return dialog.isReady(); }) && dialog.previewValid() &&
						 dialog.placement().position.x == 192,
					 "latest preview wins");
		place.position.x = 32768;
		dialog.setPlacement(place);
		ok &= expect(until([&] { return dialog.isReady(); }) && !dialog.previewValid() && !apply->isEnabled(),
					 "invalid placement cannot apply");
		place.position.x = 192;
		dialog.setPlacement(place);
		ok &= expect(until([&] { return dialog.isReady(); }) && dialog.previewValid(), "preview recovers");
		dialog.setGuard([](QString *failure) {
			*failure = QStringLiteral("changed state");
			return false;
		});
		dialog.accept();
		ok &= expect(dialog.isVisible() && dialog.result() != QDialog::Accepted, "guard keeps stale draft open");
		dialog.setGuard({});
		bool inserted = false;
		dialog.setInsertHandler([&](const auto &candidate, const auto &report, QString *) {
			inserted = candidate.undoStack.size() == 1 && !report.inserted.isEmpty();
			return inserted;
		});
		dialog.accept();
		ok &= expect(inserted && dialog.result() == QDialog::Accepted, "prepared snapshot applied once");
		if (scale == 200) {
			app.removeTranslator(&expansion);
		}
	}
	settings.setTheme(StudioTheme::Dark);
	settings.setTextScalePercent(100);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ApplicationShell shell;
	ok &= expect(shell.createLevelDocument(create, &error), "new shell map", error);
	auto *insert = shell.findChild<QAction *>(QStringLiteral("map.insertPrefab"));
	ok &= expect(insert && insert->isEnabled() && !shell.findChild<QAction *>(QStringLiteral("map.exportPrefab"))->isEnabled(),
				 "commands reflect map and selection");
	QTimer drive, watchdog;
	drive.setInterval(30);
	watchdog.setSingleShot(true);
	int phase = 0;
	bool accepted = false, stale = false, staged = false;
	QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
		auto *modal = QApplication::activeModalWidget();
		if (!modal || modal->objectName() != QStringLiteral("levelPrefabDialog")) {
			return;
		}
		auto *dialog = static_cast<LevelPrefabDialog *>(modal);
		if (!dialog->isReady()) {
			return;
		}
		drive.stop();
		if (phase == 0) {
			dialog->accept();
			accepted = dialog->result() == QDialog::Accepted;
		} else if (phase == 2) {
			dialog->findChild<QLineEdit *>(QStringLiteral("prefabOutput"))->setText(QStringLiteral("prefabs/shell.vprefab"));
			dialog->accept();
			staged = dialog->result() == QDialog::Accepted;
		} else {
			shell.createLevelDocument(create, &error);
			dialog->accept();
			stale = dialog->result() != QDialog::Accepted &&
					dialog->findChild<QLabel *>(QStringLiteral("prefabStatus"))->text().contains(QStringLiteral("changed"));
			dialog->reject();
		}
	});
	QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
		if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			dialog->reject();
		}
	});
	drive.start();
	watchdog.start(60000);
	shell.openPathFromCommandLine(prefabPath);
	drive.stop();
	watchdog.stop();
	ok &= expect(accepted && shell.levelDocument().undoStack.size() == 1 && shell.levelDocument().brushes.size() == 2,
				 "file route inserts into actual shell");
	const auto after = serializeLevelMap(shell.levelDocument()).bytes;
	shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == targetBytes, "shell exact undo");
	shell.findChild<QAction *>(QStringLiteral("map.redo"))->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == after, "shell exact redo");
	ok &= expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("placed.map")), false, &error), "shell save", error);
	shell.openPathFromCommandLine(temp.filePath(QStringLiteral("assets")));
	auto *stageAction = shell.findChild<QAction *>(QStringLiteral("map.stagePrefab"));
	ok &= expect(until([&] { return stageAction->isEnabled(); }), "stage enabled with map selection and package");
	phase = 2;
	drive.start();
	watchdog.start(60000);
	stageAction->trigger();
	drive.stop();
	watchdog.stop();
	ok &= expect(staged && serializeLevelMap(shell.levelDocument()).bytes == after, "actual shell stages prefab without a map edit");
	auto entries = tests::PackageRows(shell.findChild<PackageEntryView*>(QStringLiteral("packageEntries")));
	shell.findChild<QLineEdit *>(QStringLiteral("packageFilter"))->setText(QStringLiteral("shell.vprefab"));
	tests::PackageRow* prefabRow = nullptr;
	ok &= expect(until([&] {
					 for (int row = 0; row < entries->count(); ++row) {
						 if (entries->item(row)->data(Qt::UserRole).toString() == QStringLiteral("prefabs/shell.vprefab")) {
							 prefabRow = entries->item(row);
							 return true;
						 }
					 }
					 return false;
				 }),
				 "staged prefab appears in browser");
	if (prefabRow) {
		phase = 0;
		accepted = false;
		drive.start();
		watchdog.start(60000);
		entries->itemActivated(prefabRow);
		drive.stop();
		watchdog.stop();
		ok &= expect(accepted && shell.levelDocument().brushes.size() == 4, "actual package browser inserts staged prefab");
		shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == after, "package placement undo retains original map");
	}
	shell.findChild<QAction *>(QStringLiteral("package.unstageLast"))->trigger();
	phase = 1;
	drive.start();
	watchdog.start(60000);
	shell.openPathFromCommandLine(prefabPath);
	drive.stop();
	watchdog.stop();
	ok &= expect(stale && shell.levelDocument().brushes.isEmpty(), "replaced map invalidates shell preview");
	ok &= expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("empty.map")), false, &error), "save replacement", error);
	shell.close();
	auto *pending = new LevelPrefabDialog(source, LevelPrefabDialogMode::Export);
	pending->show();
	QEventLoop start;
	QTimer::singleShot(130, &start, &QEventLoop::quit);
	start.exec();
	delete pending;
	QEventLoop finish;
	QTimer::singleShot(250, &finish, &QEventLoop::quit);
	finish.exec();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
