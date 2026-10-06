#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/level_surface_worker.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_camera_keys.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include "tests/level_object_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFont>
#include <QLabel>
#include <QScrollBar>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>
#include <atomic>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop events; QTimer::singleShot(10, &events, &QEventLoop::quit); events.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "LevelSurfaceTools") { return {}; }
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 2, '~'));
	}
};
class GatedReader final : public PackageArchiveReader {
public:
	PackageArchive archive;
	mutable std::atomic_bool entered{false}, armed{true};
	mutable QSemaphore release;
	PackageArchiveFormat format() const override { return archive.format(); }
	QString sourcePath() const override { return archive.sourcePath(); }
	bool isOpen() const override { return archive.isOpen(); }
	QVector<PackageEntry> entries() const override { return archive.entries(); }
	bool readEntryBytes(const QString& path, QByteArray* bytes, QString* error, qint64 limit) const override
	{
		return archive.readEntryBytes(path, bytes, error, limit);
	}
	bool readEntryAt(qsizetype index, QByteArray* bytes, QString* error, qint64 limit) const override
	{
		if (armed.exchange(false)) {
			entered = true;
			if (!release.tryAcquire(1, 10000)) { if (error) { *error = QStringLiteral("Gate timed out"); } return false; }
		}
		return archive.readEntryAt(index, bytes, error, limit);
	}
};
}

int main(int argc, char** argv)
{
	// Semantic Qt calls and QWidget render targets; no native input or capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	StudioSettings settings; settings.setReducedMotion(true); settings.setSelectedEditorProfileId(QStringLiteral("q3radiant"));
	QString error; bool ok = true;
	LevelMapDocument materials;
	ok &= expect(tests::createMaterialFixture(temp.path(), &materials, &error), "material fixture", error);
	const auto mapPath = QDir(temp.path()).filePath(QStringLiteral("quick.map"));
	ok &= tests::putMaterialFile(mapPath, tests::surfaceFixture(QStringLiteral("classic")));
	{
		// Hold a real asset read on the worker to exercise cancellation while
		// work is in flight, without relying on a fixture being slow enough.
		auto reader = std::make_shared<GatedReader>();
		ok &= reader->archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error);
		LevelSurfaceWork request; request.archive = reader; request.token = 77;
		ok &= loadLevelMap({mapPath, {}, QStringLiteral("idtech3")}, &request.document, &error);
		request.faces = {{0, 0}}; request.adjustments = {{LevelSurfaceOperation::Fit, 1, 1}};
		LevelSurfaceWorker worker; LevelSurfaceResult result;
		worker.completed = [&](LevelSurfaceResult value) { result = std::move(value); };
		int beats = 0; QTimer heartbeat; heartbeat.setInterval(1);
		QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++beats; }); heartbeat.start();
		ok &= expect(worker.start(request) && !worker.start(request), "one bounded worker accepts only one batch");
		ok &= expect(until([&] { return reader->entered.load() && beats >= 3; }) && worker.busy(), "GUI event loop remains live during asset read");
		worker.cancel(); reader->release.release();
		ok &= expect(until([&] { return !worker.busy(); }) && result.cancelled && result.token == 77, "in-flight cancellation retires the result");
		request.token = 78;
		ok &= expect(worker.start(request) && until([&] { return !worker.busy(); }) && !result.cancelled && result.error.isEmpty()
			&& result.token == 78 && result.changedFaces == 1 && result.document.undoStack.size() == 1
			&& result.textureSizes.value(QStringLiteral("studio/grid")) == QSize(128, 64), "worker restarts and resolves original dimensions", result.error);
	}
	for (int scale : {100, 200}) {
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme); settings.setTextScalePercent(scale); settings.sync();
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion expansion; if (scale == 200) { app.installTranslator(&expansion); }
		{
			ApplicationShell shell; shell.resize(scale == 100 ? 1650 : 2400, scale == 100 ? 1000 : 1500);
			if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
			for (auto* combo : shell.findChildren<QComboBox*>()) {
				if (combo->accessibleName() == QStringLiteral("Level map engine hint")) { combo->setCurrentIndex(combo->findData(QStringLiteral("idtech3"))); }
			}
			shell.show(); shell.openPathFromCommandLine(mapPath);
			ok &= expect(until([&] { return shell.levelDocument().sourcePath == mapPath; }), "map loaded");
			const auto command = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
			command("shell.mode.levels")->trigger(); command("map.selectAll")->trigger();
			auto* panel = static_cast<LevelSurfaceTools*>(shell.findChild<QScrollArea*>(QStringLiteral("levelSurfaceTools")));
			auto* shift = command("map.surfaceShiftRight");
			if (!expect(panel && shift && shift->isEnabled(), "quick surface commands and panel")) { return 1; }
			ok &= expect(shift->shortcuts().contains(QKeySequence(QStringLiteral("Shift+Right")))
				&& command("map.surfaceFit")->shortcuts().contains(QKeySequence(QStringLiteral("Shift+5"))), "audited Q3 bindings installed");
			auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
			ok &= expect(shift->shortcutContext() == Qt::WidgetWithChildrenShortcut && camera->actions().contains(shift)
				&& !panel->actions().contains(shift) && !panel->findChild<QDoubleSpinBox*>()->actions().contains(shift), "shortcut is attached to viewports, not numeric fields");
			ok &= expect(camera && !cameraKeyMotions(camera->cameraControls(), false, Qt::NoButton, Qt::NoModifier, Qt::ShiftModifier).contains(Qt::Key_Right),
				"texture chord is not claimed by camera motion");
			const auto source = serializeLevelMap(shell.levelDocument()).bytes;
			const double initial = shell.levelDocument().brushes.first().faces.first().shiftX;
			for (int i = 0; i < 16; ++i) { shift->trigger(); }
			ok &= expect(shell.levelSurfaceEditsPending() && serializeLevelMap(shell.levelDocument()).bytes == source, "burst queued without synchronously editing map");
			ok &= expect(until([&] { return !shell.levelSurfaceEditsPending(); }), "burst completed");
			ok &= expect(shell.levelDocument().undoStack.size() == 1
				&& shell.levelDocument().brushes.first().faces.first().shiftX == initial + 128, "all sixteen steps applied once in one undo");
			const auto changed = serializeLevelMap(shell.levelDocument()).bytes;
			command("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == source, "shell undo exact");
			command("map.redo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == changed, "shell redo exact");
			command("map.surfaceShiftLeft")->trigger(); shift->trigger();
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); });
			ok &= expect(shell.levelDocument().undoStack.size() == 1 && serializeLevelMap(shell.levelDocument()).bytes == changed, "inverse burst creates no undo entry");
			shift->trigger(); shell.cancelLevelSurfaceEdits(); shift->trigger();
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); });
			ok &= expect(shell.levelDocument().brushes.first().faces.first().shiftX == initial + 136, "cancel and immediate requeue do not replay retired work");
			const auto stable = serializeLevelMap(shell.levelDocument()).bytes;
			shift->trigger(); command("map.selectNone")->trigger();
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); });
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == stable && !shift->isEnabled(), "selection change discards pending edit");
			command("map.selectAll")->trigger();
			ok &= shell.queueLevelSurfaceEdit({LevelSurfaceOperation::Scale, 0, 1}, &error);
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); });
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == stable, "worker error leaves live map unchanged");
			for (int i = 0; i < 64; ++i) { ok &= shell.queueLevelSurfaceEdit({LevelSurfaceOperation::Shift, 1, 0}, &error); }
			ok &= expect(!shell.queueLevelSurfaceEdit({LevelSurfaceOperation::Shift, 1, 0}, &error), "bounded pending queue"); shell.cancelLevelSurfaceEdits();
			// Fit requires actual dimensions, so an offline failure is visible.
			command("map.surfaceFit")->trigger(); ok &= until([&] { return !shell.levelSurfaceEditsPending(); });
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == stable
				&& panel->findChild<QLabel*>(QStringLiteral("surfaceQuickStatus"))->text().contains(QStringLiteral("image size")), "no guessed material dimensions");
			shell.openPathFromCommandLine(QDir(temp.path()).filePath(QStringLiteral("assets")));
			command("map.surfaceFit")->trigger();
			ok &= expect(camera->isVisible() && panel->isVisible(), "command from Packages reveals map and surface controls");
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); });
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes != stable, "package-backed fit commits",
				panel->findChild<QLabel*>(QStringLiteral("surfaceQuickStatus"))->text());
			// Explicit face scope retains all other face mappings.
			auto* objects = shell.findChild<LevelObjectList*>(QStringLiteral("levelMapObjects"));
			for (int row = 0; row < objects->model()->rowCount(); ++row) {
				if (tests::objectIndex(objects, row).data(Qt::UserRole).toString() == QStringLiteral("brush:0")) {
					tests::setObjectCurrentRow(objects, row, QItemSelectionModel::ClearAndSelect); break;
				}
			}
			auto* inspector = shell.findChild<QTreeWidget*>(QStringLiteral("entityInspector")); bool found = false;
			for (QTreeWidgetItemIterator it(inspector); *it; ++it) {
				if ((*it)->data(0, Qt::UserRole + 11).toString() == QStringLiteral("face:0:0:texture")) {
					inspector->setCurrentItem(*it); found = true; break;
				}
			}
			ok &= expect(found, "face inspector row available");
			panel->findChild<QComboBox*>(QStringLiteral("surfaceQuickTarget"))->setCurrentIndex(1);
			const auto beforeFace = shell.levelDocument().brushes.first(); shift->trigger();
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); });
			const auto afterFace = shell.levelDocument().brushes.first();
			ok &= expect(afterFace.faces[0].shiftX == beforeFace.faces[0].shiftX + 8 && afterFace.faces[1].shiftX == beforeFace.faces[1].shiftX, "inspected face scope is exact");
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
			ok &= expect(panel->horizontalScrollBar()->maximum() == 0, "panel fits at scaled expanded RTL layout");
			for (auto* step : panel->findChildren<QDoubleSpinBox*>()) {
				ok &= expect(!step->accessibleName().isEmpty() && !step->accessibleDescription().isEmpty()
					&& step->focusPolicy() != Qt::NoFocus && step->layoutDirection() == Qt::LeftToRight
					&& QAccessible::queryAccessibleInterface(step)->role() == QAccessible::SpinBox, "accessible numeric controls");
			}
			for (auto* button : panel->findChildren<QToolButton*>()) { ok &= expect(!button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus, "accessible buttons"); }
			const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!captures.isEmpty()) {
				QDir().mkpath(captures); QImage image(panel->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel->render(&image);
				ok &= image.save(QDir(captures).filePath(QStringLiteral("quick-surfaces-%1.png").arg(scale)));
			}
			auto* cancel = panel->findChild<QToolButton*>(QStringLiteral("surfaceQuickCancel"));
			panel->ensureWidgetVisible(cancel, 0, 8); app.processEvents(QEventLoop::ExcludeUserInputEvents);
			ok &= expect(panel->viewport()->rect().intersects(QRect(cancel->mapTo(panel->viewport(), QPoint()), cancel->size())),
				"cancellation control remains reachable by scrolling at every scale");
			panel->findChild<QComboBox*>(QStringLiteral("surfaceQuickTarget"))->setCurrentIndex(0);
			shift->trigger();
			const auto savePath = QDir(temp.path()).filePath(QStringLiteral("saved-%1.map").arg(scale));
			ok &= expect(shell.saveLevelDocument(savePath, false, &error), "save during pending work", error);
			const auto saved = serializeLevelMap(shell.levelDocument()).bytes;
			ok &= until([&] { return !shell.levelSurfaceEditsPending(); });
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == saved && shell.levelDocument().sourcePath == savePath, "save invalidates pending source and retains saved identity");
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	return ok ? 0 : 1;
}
