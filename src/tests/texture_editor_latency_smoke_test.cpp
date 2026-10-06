#include "app/studio_theme.h"
#include "app/texture_canvas.h"
#include "app/texture_editor_dialog.h"
#include "app/texture_export_panel.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}

// Includes dispatch, completion callbacks, layer thumbnails and queued paints,
// rather than measuring only the worker's core operation. Timings are evidence,
// not machine-dependent pass/fail thresholds.
struct UiMeasurement {
	QJsonObject result;
	bool finished = false;
};

UiMeasurement measure(TextureEditorDialog& editor, const QString& name, const std::function<void()>& start, int cancelAfterMs = -1)
{
	QElapsedTimer clock; clock.start();
	QEventLoop loop;
	QTimer heartbeat, poll, timeout, cancel;
	heartbeat.setTimerType(Qt::PreciseTimer); heartbeat.setInterval(5);
	poll.setInterval(2); timeout.setSingleShot(true); cancel.setSingleShot(true);
	qint64 previous = 0, maximumGap = 0, cancelledAt = -1, publicationAt = -1;
	int beats = 0;
	auto* cancelButton = editor.findChild<QPushButton*>(QStringLiteral("cancelTextureOperation"));
	const auto observePublication = [&]() {
		if (publicationAt < 0 && editor.isBusy() && cancelButton && cancelButton->isHidden()) { publicationAt = clock.nsecsElapsed(); }
	};
	QObject::connect(&heartbeat, &QTimer::timeout, &loop, [&]() {
		const auto now = clock.nsecsElapsed(); maximumGap = std::max(maximumGap, now - previous); previous = now; ++beats;
		observePublication();
	});
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { observePublication(); if (!editor.isBusy()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(&cancel, &QTimer::timeout, &loop, [&]() { cancelledAt = clock.nsecsElapsed(); editor.cancelPending(); });
	heartbeat.start(); start();
	const double dispatch = clock.nsecsElapsed() / 1e6;
	if (cancelAfterMs >= 0) { cancel.start(cancelAfterMs); }
	poll.start(); timeout.start(120000);
	if (editor.isBusy()) { loop.exec(); }
	cancel.stop(); poll.stop(); timeout.stop();
	// Include the completion's paint/layout cost without injecting user input.
	QApplication::processEvents();
	heartbeat.stop();
	const auto elapsed = clock.nsecsElapsed(); maximumGap = std::max(maximumGap, elapsed - previous);
	UiMeasurement sample;
	sample.finished = !editor.isBusy();
	sample.result = {{QStringLiteral("workload"), name}, {QStringLiteral("finished"), sample.finished},
		{QStringLiteral("milliseconds"), elapsed / 1e6}, {QStringLiteral("dispatchMs"), dispatch},
		{QStringLiteral("maximumUiGapMs"), maximumGap / 1e6}, {QStringLiteral("heartbeats"), beats}};
	if (cancelAfterMs >= 0) {
		sample.result.insert(QStringLiteral("cancelRequested"), cancelledAt >= 0);
		sample.result.insert(QStringLiteral("cancellationMs"), cancelledAt >= 0 ? (elapsed - cancelledAt) / 1e6 : -1.0);
	}
	if (publicationAt >= 0) {
		sample.result.insert(QStringLiteral("preparationMs"), publicationAt / 1e6);
		sample.result.insert(QStringLiteral("publicationAndRefreshMs"), (elapsed - publicationAt) / 1e6);
	}
	return sample;
}
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return EXIT_FAILURE; }
	const QDir root(temporary.path());
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("profile.ini")));
	StudioSettings settings; settings.setTextureRecoveryEnabled(false); settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	TextureEditorDialog editor;
	QImage noise(2048, 2048, QImage::Format_ARGB32); quint32 random = 0x195327u;
	for (int y = 0; y < noise.height(); ++y) {
		auto* row = reinterpret_cast<QRgb*>(noise.scanLine(y));
		for (int x = 0; x < noise.width(); ++x) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; row[x] = random | 0xff000000u; }
	}
	QString error; bool ok = expect(editor.setImage(noise, QStringLiteral("synthetic-noise.png"), &error), "open maximum authoring surface");
	editor.show(); app.processEvents();
	QJsonArray measurements;
	const auto run = [&](const QString& name, const std::function<void()>& operation, int cancelAfterMs = -1) {
		auto sample = measure(editor, name, operation, cancelAfterMs); measurements.append(sample.result);
		ok &= expect(sample.finished, qPrintable(name)); return sample.result;
	};
	QJsonArray layers;
	for (int i = 1; i < 8; ++i) {
		layers.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-duplicate")}});
		layers.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("layer-properties")}, {QStringLiteral("opacity"), 50}});
	}
	run(QStringLiteral("32M layer pixel authoring and preview"), [&]() { editor.applyOperations(layers); });
	ok &= expect(editor.document().layers().size() == 8, "maximum aggregate layered document is editable");
	const auto layeredRevision = editor.document().revision();
	auto* canvas = editor.findChild<TextureCanvas*>(QStringLiteral("textureCanvas"));
	run(QStringLiteral("32M layer pixel interactive stamp and UI refresh"), [&]() { canvas->beginGesture({100, 100}); canvas->continueGesture({105, 105}); canvas->endGesture(); });
	ok &= expect(editor.document().revision() != layeredRevision, "direct canvas gesture changes the maximum document");
	run(QStringLiteral("32M layer pixel undo and UI refresh"), [&]() { editor.undo(); });
	ok &= expect(editor.document().revision() == layeredRevision, "large asynchronous undo restores the authored layers");
	const auto cancelledProject = root.filePath(QStringLiteral("cancelled.vtexture")); bool cancelledSaveCompleted = false;
	bool cancelVisible = false;
	const auto cancelledSave = run(QStringLiteral("32M noisy layer pixel project save cancellation"), [&]() {
		editor.saveProjectToPath(cancelledProject, false, [&]() { cancelledSaveCompleted = true; });
		cancelVisible = editor.findChild<QPushButton*>(QStringLiteral("cancelTextureOperation"))->isVisible();
	}, 20);
	ok &= expect(cancelVisible && cancelledSave.value(QStringLiteral("cancelRequested")).toBool() && !cancelledSaveCompleted &&
		!QFileInfo::exists(cancelledProject) && editor.hasUnsavedChanges() && editor.document().revision() == layeredRevision,
		"project encoding offers cancellation and preserves dirty layers without publishing or running its continuation");
	const QString project = root.filePath(QStringLiteral("large.vtexture")); bool saved = false;
	const auto save = run(QStringLiteral("32M noisy layer pixel project save"), [&]() { editor.saveProjectToPath(project, false, [&]() { saved = true; }); });
	ok &= expect(saved && !editor.hasUnsavedChanges() && QFileInfo(project).size() > 80ll * 1024 * 1024 && save.value(QStringLiteral("heartbeats")).toInt() > 0,
		"large incompressible project publishes asynchronously and acknowledges its saved revision");
	const auto savedRevision = editor.document().revision();
	const auto cancelledOpen = run(QStringLiteral("32M noisy layer pixel project open cancellation"), [&]() { editor.openFromPath(project); }, 20);
	ok &= expect(cancelledOpen.value(QStringLiteral("cancelRequested")).toBool() && editor.document().revision() == savedRevision && !editor.hasUnsavedChanges(),
		"cancelling project reads and checksums retains the current document and saved revision");
	run(QStringLiteral("32M noisy layer pixel project open"), [&]() { editor.openFromPath(project); });
	ok &= expect(!editor.hasUnsavedChanges() && editor.document().layers().size() == 8 && editor.document().layers().front().pixels == noise,
		"large project opens with exact layer pixels and clean state");
	TextureExportOptions options; options.format = TextureExportFormat::Pcx; options.dither = true;
	options.extendedLimits = true; options.allowGeneratedPalette = true; options.alpha = TextureExportAlpha::Matte;
	editor.setExportOptions(options);
	const auto revision = editor.document().revision(); const auto before = editor.document().image();
	const auto cancelled = run(QStringLiteral("4M noisy pixel export cancellation"), [&]() { editor.previewExport(); }, 20);
	auto* status = editor.findChild<QLabel*>(QStringLiteral("textureEditorStatus"));
	ok &= expect(cancelled.value(QStringLiteral("cancelRequested")).toBool() && status->text().contains(QStringLiteral("Cancelled")) &&
		editor.document().revision() == revision && editor.document().image() == before, "timed cancellation stops real conversion work without changing the layered document");
	const auto converted = run(QStringLiteral("4M noisy pixel dithered PCX preview"), [&]() { editor.previewExport(); });
	auto* panel = static_cast<TextureExportPanel*>(editor.findChild<QWidget*>(QStringLiteral("textureExportPanel")));
	ok &= expect(!panel->previewImage().isNull() && converted.value(QStringLiteral("heartbeats")).toInt() > 0 && editor.document().revision() == revision,
		"long native conversion and preview keep the event loop running");
	options.format = TextureExportFormat::Png; editor.setExportOptions(options);
	const QString exported = root.filePath(QStringLiteral("large.png")); bool published = false;
	run(QStringLiteral("4M noisy pixel PNG encoding and publication"), [&]() { editor.saveExportToPath(exported, false, [&]() { published = true; }); });
	ok &= expect(published && QImage(exported) == before && editor.hasUnsavedChanges(), "export preserves exact composite pixels without marking project metadata saved");
	std::cout << QJsonDocument(measurements).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
