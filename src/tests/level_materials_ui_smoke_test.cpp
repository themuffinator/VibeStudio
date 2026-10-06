#include "app/application_shell.h"
#include "app/level_preview_worker.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <atomic>
#include <cmath>
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
class CountingReader final : public PackageArchiveReader
{
  public:
	PackageArchive archive;
	mutable std::atomic_int reads{0};
	PackageArchiveFormat format() const override { return archive.format(); }
	QString sourcePath() const override { return archive.sourcePath(); }
	bool isOpen() const override { return archive.isOpen(); }
	QVector<PackageEntry> entries() const override { return archive.entries(); }
	bool readEntryBytes(const QString &name, QByteArray *out, QString *error, qint64 cap) const override
	{
		++reads;
		QThread::msleep(10);
		return archive.readEntryBytes(name, out, error, cap);
	}
	bool readEntryAt(qsizetype index, QByteArray *out, QString *error, qint64 cap) const override
	{
		++reads;
		QThread::msleep(10);
		return archive.readEntryAt(index, out, error, cap);
	}
};
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) != "ApplicationShell") {
			return {};
		}
		const auto value = QString::fromUtf8(source);
		if (value == QStringLiteral("Textures") || value == QStringLiteral("Reload") || value == QStringLiteral("Details") ||
			value.startsWith(QStringLiteral("Materials %"))) {
			return QStringLiteral("[%1 %2]").arg(value, QString(value.size() / 3, QLatin1Char('~')));
		}
		return {};
	}
};
} // namespace

int main(int argc, char **argv)
{
	// Direct Qt APIs and QWidget::render; no injected user input or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	QString error;
	bool ok = temp.isValid();
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setReducedMotion(true);
	LevelMapDocument map;
	ok &= expect(tests::createMaterialFixture(temp.path(), &map, &error), "material fixture", error);
	auto reader = std::make_shared<CountingReader>();
	ok &= reader->archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error);
	LevelPreviewWorker worker;
	LevelPreviewRequest request;
	request.document = map;
	request.archive = reader;
	request.assetKey = QStringLiteral("first");
	request.sourceKey = QStringLiteral("fixture");
	LevelPreviewResult latest;
	int publications = 0;
	worker.completed = [&](const auto &result) {
		latest = result;
		++publications;
	};
	worker.request(request);
	ok &= expect(until([&] { return !worker.busy(); }) && latest.assets.readyCount() == 4 && latest.preview.modelInstances == 1,
				 "background material and geometry result");
	const int firstReads = reader->reads;
	request.document.revision += 1;
	worker.request(request);
	ok &= expect(until([&] { return !worker.busy(); }) && reader->reads == firstReads, "unchanged assets reused across map revisions");
	request.assetKey = QStringLiteral("reload");
	worker.request(request);
	ok &= expect(until([&] { return reader->reads > firstReads; }), "slow read in flight");
	const int beforeReplace = publications;
	for (int i = 0; i < 8; ++i) {
		request.document.revision = 100 + i;
		worker.request(request);
	}
	ok &= expect(until([&] { return !worker.busy(); }) && publications == beforeReplace + 1 && latest.revision == 107,
				 "only latest generation is published");
	worker.request(request);
	worker.cancel();
	ok &= expect(latest.assets.cancelled, "queued cancellation publishes explicit state");
	{
		LevelPreviewWorker closing;
		closing.request(request);
		app.processEvents();
	}
	LevelDocumentSaveRequest save;
	save.path = QDir(temp.path()).filePath(QStringLiteral("fixture.map"));
	ok &= expect(writeLevelDocument(map, save).succeeded(), "save real-shell fixture");
	PackageStagingModel staging;
	staging.loadBaseArchive(reader->archive, &error);
	PackageWriteRequest write;
	write.destinationPath = QDir(temp.path()).filePath(QStringLiteral("fixture.pk3"));
	ok &= expect(staging.writeArchive(write).succeeded(), "write package fixture");
	for (int scale : {100, 200}) {
		std::cerr << "Material UI scale " << scale << std::endl;
		const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
		settings.setTheme(theme);
		settings.setTextScalePercent(scale);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
		Expansion translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		ApplicationShell shell;
		shell.resize(scale == 100 ? 1440 : 2000, scale == 100 ? 950 : 1400);
		if (scale == 200) {
			shell.setLayoutDirection(Qt::RightToLeft);
		}
		shell.show();
		shell.openPathFromCommandLine(write.destinationPath);
		shell.openPathFromCommandLine(save.path);
		auto *view = shell.findChild<ModelViewport *>(QStringLiteral("mapPreview3D"));
		auto *toggle = shell.findChild<QToolButton *>(QStringLiteral("levelMap3DButton"));
		auto *label = shell.findChild<QLabel *>(QStringLiteral("levelPreviewLabel"));
		if (scale == 200) {
			for (const auto &name :
				 {QStringLiteral("levelPreviewTextured"), QStringLiteral("levelPreviewReload"), QStringLiteral("levelPreviewDetails")}) {
				auto *button = shell.findChild<QAbstractButton *>(name);
				button->setText(QStringLiteral("[%1 expanded]").arg(button->text()));
			}
		}
		toggle->setChecked(true);
		ok &= expect(until([&] { return view->isEnabled() && view->hasMesh() && view->hasSkin(); }), "actual shell loads material images",
					 label->text());
		ok &= expect(view->renderMode() == ModelViewportRenderMode::Textured && view->mesh().surfaces.size() == 4,
					 "actual shell textures brush patch and model");
		auto controls = view->cameraControls();
		controls.perspective = true;
		view->setCameraControls(controls);
		view->setCameraView({-180, -230, 180}, 52, -28);
		ok &= expect(tests::settleModelViewport(*view), "texture raster completes");
		auto *textures = shell.findChild<QCheckBox *>(QStringLiteral("levelPreviewTextured"));
		ok &= expect(QAccessible::queryAccessibleInterface(textures)->role() == QAccessible::CheckBox &&
						 textures->focusPolicy() != Qt::NoFocus && !textures->accessibleName().isEmpty(),
					 "texture toggle keyboard and screen-reader metadata");
		textures->setChecked(false);
		ok &= expect(view->renderMode() == ModelViewportRenderMode::FlatShaded, "flat mode control");
		textures->setChecked(true);
		ok &= expect(shell.width() <= (scale == 100 ? 1440 : 2000), "scaled controls do not force window wider");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			tests::settleModelViewport(*view);
			QImage image(shell.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			shell.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("materials-%1.png").arg(scale))), "capture actual shell");
			QImage camera(view->size(), QImage::Format_ARGB32_Premultiplied);
			camera.fill(Qt::transparent);
			view->render(&camera);
			ok &= expect(camera.save(QDir(captures).filePath(QStringLiteral("camera-%1.png").arg(scale))), "capture render target widget");
		}
		const auto position = view->cameraPosition();
		shell.findChild<QAction *>(QStringLiteral("map.selectAll"))->trigger();
		LevelMapRotationRequest rotation{2, 15, {0, 0, 0, true}, true, true};
		ok &= expect(shell.applyLevelRotation(rotation, &error), "rotation with material preview", error);
		ok &= expect(!view->isEnabled(), "stale picking suspended while geometry rebuilds");
		ok &= expect(until([&] { return view->isEnabled() && view->hasSkin(); }) && view->cameraPosition().x == position.x,
					 "edit preserves camera and materials");
		shell.findChild<QAction *>(QStringLiteral("map.undo"))->trigger();
		ok &= expect(until([&] { return view->isEnabled(); }), "undo refresh finishes");
		// Invalidates the asset cache while retaining the user's camera.
		shell.findChild<QToolButton *>(QStringLiteral("levelPreviewReload"))->click();
		ok &= expect(until([&] { return view->isEnabled() && view->hasSkin(); }), "explicit reload");
		ok &= expect(shell.saveLevelDocument(QDir(temp.path()).filePath(QStringLiteral("ui-%1.map").arg(scale)), false, &error),
					 "save clean shell fixture", error);
		ok &= expect(until([&] { return view->isEnabled() && view->hasSkin(); }) && view->cameraPosition().x == position.x &&
						 view->cameraPosition().y == position.y && view->cameraPosition().z == position.z && view->cameraYaw() == 52 &&
						 view->cameraPitch() == -28,
					 "Save As preserves camera position and direction");
		if (scale == 200) {
			const auto gridU = [&] {
				for (const auto &surface : view->mesh().surfaces) {
					if (surface.name == QStringLiteral("studio/grid")) {
						for (const auto &uv : surface.texCoords) {
							if (std::abs(uv.u) > 0.001) {
								return uv.u;
							}
						}
					}
				}
				return 0.0f;
			};
			const float beforeU = gridU();
			ok &= staging.addBytes(tests::materialImage(64, 128, true), QStringLiteral("textures/studio/grid.png"), &error,
								   PackageStageConflictResolution::ReplaceExisting);
			write.allowOverwrite = true;
			ok &= expect(staging.writeArchive(write).succeeded(), "replace external package fixture");
			shell.openPathFromCommandLine(write.destinationPath);
			ok &= expect(until([&] { return view->isEnabled() && view->hasSkin() && std::abs(gridU() - beforeU * 2) < 0.001; }),
						 "reopening the same package path refreshes image dimensions and brush UVs");
		}
		ok &= expect(shell.close(), "clean shell close");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
