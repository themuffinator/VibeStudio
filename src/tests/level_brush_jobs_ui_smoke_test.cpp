#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/level_primitive_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; ok = false; }
	return value;
}
void drain(int ms = 50)
{
	QEventLoop events; QTimer::singleShot(ms, &events, &QEventLoop::quit); events.exec(QEventLoop::ExcludeUserInputEvents);
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 60000) { drain(20); }
	return ready();
}
bool write(const QString& path, const LevelMapDocument& document)
{
	QFile file(path); const auto bytes = serializeLevelMap(document).bytes;
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* text, const char*, int) const override
	{
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(source, QString(source.size()/3, QLatin1Char('~')));
	}
};
struct Heartbeat {
	QTimer timer;
	QElapsedTimer elapsed;
	qint64 previous = 0, maximum = 0;
	int beats = 0;
	Heartbeat()
	{
		timer.setInterval(5);
		QObject::connect(&timer, &QTimer::timeout, &timer, [this] {
			const auto now = elapsed.elapsed(); maximum = std::max(maximum, now - previous); previous = now; ++beats;
		});
		elapsed.start(); timer.start();
	}
};
}
int main(int argc, char** argv)
{
	// Semantic Qt calls only. Offscreen widgets render to owned images; no
	// native input injection, OS capture, clipboard or game launch is involved.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp; QString error;
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings; settings.setReducedMotion(true); settings.setRestoreSession(false);
	settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom"));
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views")); settings.sync();
	QJsonArray measurements;
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::AddBrush;
	request.primitive.texture = QStringLiteral("studio/new");
	if (!app.arguments().contains(QStringLiteral("--latency"))) {
		LevelMapDocument large;
		if (!expect(tests::createGeometryFixture(50000, &large, &error), "large job fixture", error)) { return 1; }
		LevelSceneNode layer; layer.id = QStringLiteral("e681b0b0-9eca-4a57-9062-e08a1d32df12"); layer.kind = LevelSceneNodeKind::Layer;
		layer.name = QStringLiteral("Generated geometry");
		for (const auto& brush : large.brushes) { layer.objects << QStringLiteral("brush:%1").arg(brush.id); }
		large.scene.nodes << layer; large.activeSceneNode = layer.id;
		expect(validateLevelScene(large, large.scene, &error), "large scene fixture", error);
		for (const int scale : {100, 200}) {
			const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
			settings.setTextScalePercent(scale); settings.setTheme(theme);
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			Expansion expanded;
			if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			bool observed = false;
			QElapsedTimer cancelling;
			QTimer drive; drive.setInterval(10);
			QObject::connect(&drive, &QTimer::timeout, &app, [&] {
				auto* modal = QApplication::activeModalWidget();
				if (!modal || modal->objectName() != QStringLiteral("levelPlacementTaskDialog") || observed) { return; }
				observed = true;
				auto* phase = modal->findChild<QLabel*>("levelPlacementTaskPhase");
				auto* bar = modal->findChild<QProgressBar*>("levelPlacementTaskProgress");
				auto* cancel = modal->findChild<QPushButton*>("levelPlacementTaskCancel");
				expect(modal->windowTitle().contains("Add Brush") && phase && !phase->text().isEmpty(), "visible brush progress");
				expect(bar && QAccessible::queryAccessibleInterface(bar)->role() == QAccessible::ProgressBar &&
					cancel && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleDescription().isEmpty(), "accessible progress/cancel");
				const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!capture.isEmpty()) {
					QDir().mkpath(capture); QImage image(modal->size(), QImage::Format_ARGB32_Premultiplied);
					image.fill(Qt::transparent); modal->render(&image);
					expect(image.save(QDir(capture).filePath(QStringLiteral("brush-job-%1.png").arg(scale))), "job render");
				}
				cancelling.start(); if (cancel) { cancel->click(); }
			});
			Heartbeat heartbeat; drive.start();
			const auto result = LevelPlacementTaskDialog::prepare(nullptr, large, request);
			drive.stop(); heartbeat.timer.stop();
			const auto cancelMs = cancelling.isValid() ? cancelling.elapsed() : -1;
			expect(observed && result.cancelled && !result.succeeded && result.document.format == LevelMapFormat::Unknown,
				"cancel discards private brush preparation", result.error);
			expect(heartbeat.beats > 1 && heartbeat.maximum < 1000 && cancelMs >= 0 && cancelMs < 1000, "responsive preparation/cancel");
			expect(large.brushes.size() == 50000 && large.undoStack.isEmpty(), "source/history preserved on cancel");
			measurements << QJsonObject{{"text_scale", scale}, {"brushes", 50000}, {"heartbeat_gap_ms", heartbeat.maximum},
				{"cancel_return_ms", cancelMs}};
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	settings.setTextScalePercent(100); settings.setTheme(StudioTheme::Dark);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ApplicationShell shell;
	shell.resize(1700, 1100); shell.show();
	auto* plan = shell.findChild<MapViewport*>("mapViewport");
	auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	LevelMapCreateRequest create; create.starterRoom = false;
	expect(shell.createLevelDocument(create, &error), "shell map", error);
	shell.findChild<QAction*>("shell.mode.levels")->trigger();
	expect(until([&] { return camera->isEnabled() && camera->hasMesh(); }), "empty shell camera");
	expect(shell.saveLevelDocument(temp.filePath("empty.map"), false, &error), "save before source replacement", error);
	if (app.arguments().contains(QStringLiteral("--latency"))) {
		for (const int count : {1000, 10000}) {
			LevelMapDocument source;
			expect(tests::createGeometryFixture(count, &source, &error), "shell scale fixture", error);
			const auto path = temp.filePath(QStringLiteral("scale-%1.map").arg(count));
			expect(write(path, source), "write scale fixture"); shell.openPathFromCommandLine(path);
			expect(until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && !camera->isRendering(); }),
				"shell scale load"); drain(500);
			Heartbeat heartbeat; QElapsedTimer elapsed; elapsed.start();
			expect(shell.applyLevelBrushPrimitive(request.primitive, &error), "shell scale add", error);
			const auto applyMs = elapsed.elapsed(); drain(100); heartbeat.timer.stop();
			expect(shell.levelDocument().brushes.size() == count + 1 && shell.levelDocument().undoStack.size() == 1,
				"shell scale single publication");
			measurements << QJsonObject{{"shell_brushes", count}, {"apply_return_ms", applyMs},
				{"heartbeat_gap_ms", heartbeat.maximum}, {"heartbeat_count", heartbeat.beats}};
			expect(shell.saveLevelDocument(path, true, &error), "save scale result", error);
		}
	} else {
		const auto original = serializeLevelMap(shell.levelDocument()).bytes;
		QTimer::singleShot(0, &shell, [&] { shell.createLevelDocument(create, &error); });
		expect(!shell.applyLevelBrushPrimitive(request.primitive, &error) && error.contains("changed") &&
			serializeLevelMap(shell.levelDocument()).bytes == original, "replaced source rejects in-flight brush", error);
		expect(shell.applyLevelBrushPrimitive(request.primitive, &error) && shell.levelDocument().undoStack.size() == 1,
			"public shell brush uses worker", error);
		const auto first = serializeLevelMap(shell.levelDocument()).bytes;
		QTimer::singleShot(0, &shell, [&] { plan->selectionSetChanged({}); });
		plan->brushDrawRequested({128, 128, 0, true}, {192, 192, 64, true});
		expect(shell.levelDocument().brushes.size() == 1 && shell.statusBar()->currentMessage().contains("changed") &&
			serializeLevelMap(shell.levelDocument()).bytes == first, "selection change rejects in-flight plan creation");
		plan->brushDrawRequested({128, 128, 0, true}, {192, 192, 64, true});
		expect(shell.levelDocument().brushes.size() == 2 && shell.levelDocument().undoStack.size() == 2, "plan creation publishes once");
		shell.findChild<QAction*>("map.undo")->trigger();
		expect(serializeLevelMap(shell.levelDocument()).bytes == first, "plan creation exact undo");
		expect(shell.saveLevelDocument(temp.filePath("first.map"), false, &error), "save shell fixture", error);
		// Include a destination whose selection changes without a revision change.
		auto source = shell.levelDocument(); QString layer;
		expect(createLevelSceneNode(&source, LevelSceneNodeKind::Layer, "Destination", {}, &layer, &error), "draft destination", error);
		const auto path = temp.filePath("destination.map"); expect(write(path, source), "write destination map");
		shell.openPathFromCommandLine(path);
		expect(until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled(); }), "load destination map");
		for (const int change : {0, 1, 2}) {
			const auto before = serializeLevelMap(shell.levelDocument()).bytes;
			QByteArray preview; bool handled = false, accepted = false;
			QTimer drive; drive.setInterval(20);
			QObject::connect(&drive, &QTimer::timeout, &shell, [&] {
				auto* modal = QApplication::activeModalWidget();
				if (!modal || modal->objectName() != QStringLiteral("addBrushDialog")) { return; }
				auto* dialog = static_cast<LevelPrimitiveDialog*>(modal);
				if (!dialog->isReady()) { return; }
				drive.stop(); handled = true; preview = serializeLevelMap(dialog->previewDocument()).bytes;
				if (change == 0) {
					plan->selectionSetChanged(shell.levelDocument().selection.isEmpty()
						? QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::QuakeBrush, 0}} : QVector<LevelMapSelectionRef>{});
				}
				if (change == 1) {
					auto* destination = shell.findChild<QComboBox*>("levelSceneCreation");
					destination->setCurrentIndex(destination->findData(layer));
				}
				dialog->accept(); accepted = dialog->result() == QDialog::Accepted;
				if (change != 2) {
					expect(dialog->isVisible() && dialog->findChild<QLabel*>("primitiveStatus")->text().contains("changed"),
						"numeric draft reports stale selection/destination"); dialog->reject();
				}
			});
			QTimer watchdog; watchdog.setSingleShot(true);
			QObject::connect(&watchdog, &QTimer::timeout, &shell, [] {
				if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { dialog->reject(); }
			});
			drive.start(); watchdog.start(60000); shell.findChild<QAction*>("map.addBrush")->trigger(); drive.stop(); watchdog.stop();
			expect(handled && accepted == (change == 2), "numeric preview acceptance state");
			expect(serializeLevelMap(shell.levelDocument()).bytes == (change == 2 ? preview : before), "publish exact preview or preserve source");
		}
		expect(shell.levelDocument().brushes.size() == 2 && shell.levelDocument().undoStack.size() == 1 &&
			levelSceneMembership(shell.levelDocument().scene, "brush:1") == layer, "numeric reuse preserves destination and one undo");
		expect(shell.saveLevelDocument(path, true, &error), "save numeric candidate", error);
	}
	shell.close(); drain(250);
	std::cout << QJsonDocument(QJsonObject{{"measurements", measurements}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
