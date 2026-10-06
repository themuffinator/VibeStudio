#include "app/application_shell.h"
#include "app/level_preview_worker.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_model_appearance_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/level_object_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <atomic>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace {
int checks = 0;
bool expect(bool pass, const char* text, const QString& detail = {}) {
	++checks; if (!pass) { std::cerr << "FAIL: " << text << ": " << detail.toStdString() << '\n'; } return pass;
}
bool waitFor(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 20000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
	return ready();
}
bool capture(QWidget& widget, const QString& name) {
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty()) { return true; }
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF()); image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(directory).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
class CountingReader final : public PackageArchiveReader {
public:
	PackageArchive archive;
	mutable std::atomic_int reads{0};
	PackageArchiveFormat format() const override { return archive.format(); }
	QString sourcePath() const override { return archive.sourcePath(); }
	bool isOpen() const override { return archive.isOpen(); }
	QVector<PackageEntry> entries() const override { return archive.entries(); }
	bool readEntryBytes(const QString& path, QByteArray* out, QString* error, qint64 cap) const override { ++reads; return archive.readEntryBytes(path, out, error, cap); }
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 cap) const override { ++reads; QThread::msleep(2); return archive.readEntryAt(index, out, error, cap); }
};
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		if (QByteArray(context) != "ApplicationShell" && QByteArray(context) != "vibestudio::ApplicationShell" && QByteArray(context) != "VibeStudioLevelMaterials") { return {}; }
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 2, '~'));
	}
};
}
int main(int argc, char** argv)
{
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root)) { return 1; }
	QTemporaryDir temporary(QDir(root).filePath("level-model-ui-XXXXXX"));
	LevelModelAppearanceFixture fixture; QString error;
	bool ok = expect(temporary.isValid() && fixture.create(temporary.path(), &error), "create generated appearance fixture", error);
	if (!ok) { return 1; }
	auto map = fixture.map(&error);
	setLevelMapEntityProperty(&map, 1, "_skin", "blue", &error); setLevelMapEntityProperty(&map, 1, "_frame", "0", &error);
	setLevelMapEntityProperty(&map, 2, "_skin", "body", &error);
	auto reader = std::make_shared<CountingReader>();
	ok &= expect(reader->archive.load(fixture.path("assets"), &error), "open immutable package reader", error);
	LevelPreviewWorker worker;
	LevelPreviewRequest request; request.document = map; request.archive = reader; request.assetKey = "original"; request.sourceKey = "map";
	LevelPreviewResult latest; int publications = 0;
	worker.completed = [&](const auto& result) { latest = result; ++publications; };
	worker.request(request);
	ok &= expect(waitFor([&] { return !worker.busy(); }) && latest.preview.triangles == 60 && latest.assets.models.size() == 3, "worker prepares independent appearances");
	const int firstReads = reader->reads;
	++request.document.revision; worker.request(request);
	ok &= expect(waitFor([&] { return !worker.busy(); }) && reader->reads == firstReads, "unrelated revision reuses appearance assets");
	setLevelMapEntityProperty(&request.document, 1, "_skin", "body", &error); worker.request(request);
	ok &= expect(waitFor([&] { return !worker.busy(); }) && reader->reads > firstReads && latest.preview.triangles == 48, "changing skin invalidates material cache");
	const int changedReads = reader->reads;
	setLevelMapEntityProperty(&request.document, 1, "_frame", "1", &error); worker.request(request);
	ok &= expect(waitFor([&] { return !worker.busy(); }) && reader->reads > changedReads && latest.assets.unavailableModels == 1,
		"changing frame invalidates cache and exposes compiler frame limitation");
	const int before = publications;
	for (int i = 0; i < 6; ++i) { request.assetKey = QString::number(i); request.document.revision += 1; worker.request(request); }
	ok &= expect(waitFor([&] { return !worker.busy(); }) && publications == before + 1 && latest.revision == request.document.revision, "retired appearance requests cannot publish");
	worker.request(request); worker.cancel();
	ok &= expect(latest.assets.cancelled && latest.assets.models.isEmpty(), "cancelled request exposes no stale ready appearance");
	LevelDocumentSaveRequest save; save.path = fixture.path("fixture.map");
	ok &= expect(writeLevelDocument(map, save).succeeded(), "save shell fixture");
	StudioSettings::setOverrideFilePath(fixture.path("settings.ini"));
	StudioSettings settings; settings.setReducedMotion(true);
	for (int scenario = 0; scenario < 3; ++scenario) {
		const auto theme = scenario == 0 ? StudioTheme::Dark : scenario == 1 ? StudioTheme::HighContrastDark : StudioTheme::HighContrastLight;
		const int textScale = 100 + scenario * 50;
		settings.setTheme(theme); settings.setTextScalePercent(textScale); settings.sync();
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, textScale));
		Expansion expansion; if (scenario) { app.installTranslator(&expansion); }
		ApplicationShell shell; shell.resize(1600, 1000);
		if (scenario) { shell.setLayoutDirection(Qt::RightToLeft); }
		shell.show(); shell.openPathFromCommandLine(fixture.path("assets")); shell.openPathFromCommandLine(save.path);
		auto* view = shell.findChild<ModelViewport*>("mapPreview3D");
		auto* toggle = shell.findChild<QToolButton*>("levelMap3DButton");
		auto* label = shell.findChild<QLabel*>("levelPreviewLabel");
		if (!expect(view && toggle && label, "find actual shell preview")) { return 1; }
		toggle->setChecked(true);
		ok &= expect(waitFor([&] { return view->isEnabled() && view->hasMesh() && view->hasSkin(); }), "real shell resolves instance materials", label->text());
		ok &= expect(view->mesh().triangleCount == 60 && view->mesh().surfaces.size() == 5, "real camera receives all five retained model surfaces");
		view->frameModel();
		ok &= expect(settleModelViewport(*view) && capture(*view, QStringLiteral("camera-%1").arg(scenario)), "owned camera render");
		auto* details = shell.findChild<QToolButton*>("levelPreviewDetails");
		ok &= expect(details && details->focusPolicy() != Qt::NoFocus && !QAccessible::queryAccessibleInterface(details)->text(QAccessible::Name).isEmpty(), "details control accessible and keyboard focusable");
		QTimer::singleShot(0, [&] {
			auto* dialog = shell.findChild<QDialog*>("levelMaterialDetails");
			auto* text = dialog ? dialog->findChild<QPlainTextEdit*>() : nullptr;
			ok &= expect(text && text->isReadOnly() && text->toPlainText().contains("models/test_blue.skin") && text->toPlainText().contains("SHA-256")
				&& text->toPlainText().contains("entity:2") && text->toPlainText().contains("omitted"), "details exposes instance ownership, omitted surfaces and exact inputs");
			if (dialog) {
				dialog->resize(900, 700);
				if (text) { auto cursor = text->textCursor(); cursor.setPosition(text->toPlainText().indexOf("Model models")); text->setTextCursor(cursor); }
				ok &= expect(capture(*dialog, QStringLiteral("details-%1").arg(scenario)), "owned appearance details render"); dialog->reject();
			}
		});
		details->click();
		// Drive the editor's owned Qt item model, never keyboard/mouse input.
		auto* objects = shell.findChild<LevelObjectList*>("levelMapObjects");
		bool selected = false;
		for (int row = 0; objects && row < objects->model()->rowCount(); ++row) {
			if (objectIndex(objects, row).data(Qt::UserRole).toString() == "entity:1") { setObjectCurrentRow(objects, row); selected = true; break; }
		}
		ok &= expect(selected, "select instance for entity inspector");
		auto* inspector = shell.findChild<QTreeWidget*>("entityInspector");
		const auto edit = [&](const QString& key, const QString& value) {
			QTreeWidgetItem* row = nullptr;
			for (QTreeWidgetItemIterator item(inspector); *item; ++item) { if ((*item)->data(0, Qt::UserRole).toString() == key) { row = *item; break; } }
			if (!row) { return false; }
			row->setText(1, value); return true;
		};
		const auto camera = view->cameraPosition();
		ok &= expect(edit("_skin", "body"), "edit skin through existing inspector");
		ok &= expect(waitFor([&] { return view->isEnabled() && view->mesh().triangleCount == 48; }), "inspector edit refreshes material and geometry cache");
		ok &= expect(view->cameraPosition().x == camera.x && view->cameraPosition().y == camera.y && view->cameraPosition().z == camera.z, "appearance edit preserves camera");
		shell.findChild<QAction*>("map.undo")->trigger();
		ok &= expect(waitFor([&] { return view->isEnabled() && view->mesh().triangleCount == 60; }), "undo restores surface and material appearance");
		shell.findChild<QAction*>("map.redo")->trigger();
		ok &= expect(waitFor([&] { return view->isEnabled() && view->mesh().triangleCount == 48; }), "redo restores selected instance appearance");
		ok &= expect(edit("_skin", "missing"), "author unresolved skin for diagnostics");
		ok &= expect(waitFor([&] { return view->isEnabled() && view->mesh().triangleCount == 36; }), "missing skin omits only unavailable instance");
		ok &= expect(label->text().contains("2/3") && label->text().contains("Details"), "failure remains visible in preview status");
		shell.findChild<QAction*>("map.undo")->trigger();
		ok &= expect(waitFor([&] { return view->isEnabled() && view->mesh().triangleCount == 48; }), "undo recovers failed appearance");
		ok &= expect(shell.saveLevelDocument(fixture.path(QStringLiteral("ui-%1.map").arg(scenario)), false, &error), "save authored appearance from shell", error);
		ok &= expect(waitFor([&] { return view->isEnabled(); }) && shell.close(), "clean shell close");
		if (scenario) { app.removeTranslator(&expansion); }
	}
	ok &= expect(fixture.unchanged(), "UI authoring preserves all source model and package bytes");
	std::cout << checks << " level model appearance UI checks; " << (ok ? "passed" : "FAILED") << '\n';
	return ok ? 0 : 1;
}
