#include "app/application_shell.h"
#include "app/level_document_dialog.h"
#include "app/level_load_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <atomic>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer clock; clock.start();
	while (!ready() && clock.elapsed() < 10000) { QApplication::processEvents(); QThread::msleep(5); }
	return ready();
}
class ExpandedLabels final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		if (QByteArray(context) != "vibestudio::LevelMapLoadDialog") { return {}; }
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1 %2]").arg(value, QString(value.size() / 2, QLatin1Char('~')));
	}
};
LevelMapLoadDialog* loadDialog()
{
	return qobject_cast<LevelMapLoadDialog*>(QApplication::activeModalWidget());
}
void discardFixtureEdits()
{
	if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
		for (auto* button : box->buttons()) { if (box->buttonRole(button) == QMessageBox::DestructiveRole) { button->click(); break; } }
	}
}
} // namespace

int main(int argc, char** argv)
{
	// Semantic widget APIs and QWidget render targets only; no injected input,
	// native captures, real installation assets or network services.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QString error; LevelMapDocument fixture;
	if (!temp.isValid() || !tests::createGeometryFixture(1000, &fixture, &error)) { return 1; }
	const auto largePath = temp.filePath(QStringLiteral("a-map-with-a-descriptive-production-name-and-a-long-source-path.map"));
	bool ok = expect(write(largePath, serializeLevelMap(fixture).bytes), "write large fixture");
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	for (int variant = 0; variant < 3; ++variant) {
		const int scale = variant == 1 ? 200 : 100;
		ExpandedLabels expanded;
		if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		std::atomic_bool reached{false}, release{false}, wrongThread{false}, gateTimeout{false};
		LevelMapLoadRequest request {largePath, {}, {}};
		request.progress = [&](LevelMapLoadPhase phase, qint64, qint64) {
			if (QThread::currentThread() == app.thread()) { wrongThread = true; }
			if (phase != LevelMapLoadPhase::Solving || reached.exchange(true)) { return; }
			QElapsedTimer clock; clock.start();
			while (!release && !QThread::currentThread()->isInterruptionRequested() && clock.elapsed() < 10000) { QThread::msleep(1); }
			if (!release && !QThread::currentThread()->isInterruptionRequested()) { gateTimeout = true; }
		};
		int heartbeats = 0, gatedHeartbeats = 0; bool inspected = false;
		QTimer pulse; pulse.setInterval(10);
		QObject::connect(&pulse, &QTimer::timeout, &app, [&] {
			++heartbeats;
			if (!reached || inspected || ++gatedHeartbeats < 8) { return; }
			auto* dialog = loadDialog();
			if (!dialog) { return; }
			inspected = true;
			auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("levelMapLoadCancel"));
			auto* bar = dialog->findChild<QProgressBar*>(QStringLiteral("levelMapLoadProgress"));
			auto* phase = dialog->findChild<QLabel*>(QStringLiteral("levelMapLoadPhase"));
			ok &= expect(cancel && bar && phase && !dialog->accessibleName().isEmpty(), "loading controls expose stable identities and accessible names");
			if (cancel && bar && phase) {
				auto* source = dialog->findChild<QLabel*>(QStringLiteral("levelMapLoadSource"));
				ok &= expect(source && source->accessibleDescription() == QDir::toNativeSeparators(largePath)
					&& source->toolTip() == QDir::toNativeSeparators(largePath)
					&& source->height() >= source->fontMetrics().height(), "full source remains accessible while the visible path elides");
				ok &= expect(bar->height() >= bar->fontMetrics().height() + 4, "progress text fits at enlarged scale");
				if (scale == 200) { ok &= expect(phase->text().startsWith(QLatin1Char('[')), "expanded translation actually reaches the dialog"); }
				auto* accessible = QAccessible::queryAccessibleInterface(bar);
				ok &= expect(cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleName().isEmpty()
					&& accessible && accessible->role() == QAccessible::ProgressBar && !phase->text().isEmpty(), "keyboard focus and progress role exposed");
				for (auto* widget : {static_cast<QWidget*>(cancel), static_cast<QWidget*>(bar), static_cast<QWidget*>(phase)}) {
					const QRect bounds(widget->mapTo(dialog, QPoint()), widget->size());
					ok &= expect(dialog->rect().contains(bounds), "scaled translated controls fit the dialog");
				}
				if (argc > 1) {
					QImage capture(dialog->size(), QImage::Format_ARGB32_Premultiplied); capture.fill(Qt::transparent); dialog->render(&capture);
					ok &= expect(capture.save(QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("map-load-%1.png").arg(variant))), "save dialog render target");
				}
				if (variant == 1) { cancel->click(); }
				if (variant == 2) { dialog->close(); }
			}
			release = true;
		});
		pulse.start();
		auto opened = LevelMapLoadDialog::openMap(nullptr, request);
		pulse.stop();
		ok &= expect(inspected && heartbeats >= 8 && !wrongThread && !gateTimeout, "GUI stays live and callbacks execute only on the loading thread");
		if (variant == 0) {
			ok &= expect(opened.succeeded && !opened.cancelled && opened.document.brushes.size() == 1000 && opened.geometry,
				"successful dialog publishes a complete document and cache", opened.error);
			if (opened.geometry) {
				MapViewport view; view.setDocument(opened.document, opened.geometry.get());
				ok &= expect(view.geometryCacheStatistics().solved == 0 && view.geometryCacheStatistics().reused == 1000,
					"worker geometry is reused for initial viewport adoption");
			}
		} else {
			ok &= expect(!opened.succeeded && opened.cancelled && !opened.geometry && opened.document.format == LevelMapFormat::Unknown,
				"Cancel and window close discard worker results after acknowledgement");
		}
		if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));

	// Real shell adoption: a same-path reload has the same source/revision key,
	// yet it must replace geometry and invalid old visibility/tool anchors.
	LevelMapDocument small; ok &= tests::createGeometryFixture(16, &small, &error);
	const auto smallPath = temp.filePath(QStringLiteral("editable.map"));
	ok &= write(smallPath, serializeLevelMap(small).bytes);
	ApplicationShell shell; shell.openPathFromCommandLine(smallPath);
	auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
	auto* path = shell.findChild<QLineEdit*>(QStringLiteral("levelMapPath"));
	if (!expect(view && path && shell.levelDocument().brushes.size() == 16, "shell adopts loaded map")) { return 1; }
	ok &= expect(view->geometryCacheStatistics().solved == 0 && view->geometryCacheStatistics().reused == 16, "shell uses prepared geometry");
	auto navigation = view->navigationState(); navigation.center += QPointF(64, 32); navigation.zoom *= 1.25;
	ok &= view->restoreNavigationState(navigation);
	view->setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 1}}); view->hideSelection(); view->setClipMode(true);
	auto* sibling = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
	if (sibling) { sibling->setClipMode(true); }
	ok &= setLevelMapSelection(&small, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error) && moveLevelMapSelection(&small, 48, 0, 0, &error);
	ok &= write(smallPath, serializeLevelMap(small).bytes);
	shell.openPathFromCommandLine(smallPath);
	ok &= expect(view->displayDocument().brushes[0].mins.x == small.brushes[0].mins.x
		&& view->navigationState().center == navigation.center && view->navigationState().zoom == navigation.zoom
		&& view->hiddenCount() == 0 && !view->clipMode(), "reload refreshes geometry, preserves navigation and resets stale hidden/tool state");
	ok &= expect(sibling && !sibling->clipMode() && sibling->hiddenCount() == 0, "reload clears stale sibling tools and visibility");

	ok &= shell.applyLevelMaterialPaint({{LevelMaterialKind::BrushFace, 0, 0}}, QStringLiteral("studio/unsaved"), &error);
	shell.checkpointLevelDocument();
	const auto recoveryRoot = levelMapRecoveryDirectory();
	ok &= expect(until([&] { return !listLevelMapRecoveries(recoveryRoot).isEmpty(); }), "current map has a recovery checkpoint");
	const auto kept = serializeLevelMap(shell.levelDocument()).bytes;
	const auto revision = shell.levelDocument().revision;
	const auto undo = shell.levelDocument().undoStack.size();
	QTimer cancel; cancel.setInterval(1); bool cancelled = false;
	QObject::connect(&cancel, &QTimer::timeout, &app, [&] {
		discardFixtureEdits();
		if (auto* dialog = loadDialog()) { cancelled = true; dialog->reject(); }
	});
	cancel.start(); shell.openPathFromCommandLine(largePath); cancel.stop();
	ok &= expect(cancelled && serializeLevelMap(shell.levelDocument()).bytes == kept && shell.levelDocument().revision == revision
		&& shell.levelDocument().undoStack.size() == undo && !listLevelMapRecoveries(recoveryRoot).isEmpty() && path->text() == smallPath,
		"cancelled shell open preserves unsaved content, undo, recovery and path");

	QToolButton* reload = nullptr;
	for (auto* button : shell.findChildren<QToolButton*>()) { if (button->accessibleName() == QStringLiteral("Inspect level map")) { reload = button; break; } }
	ok &= expect(reload, "reload command available");
	if (reload) {
		QTimer answer; answer.setInterval(1); QObject::connect(&answer, &QTimer::timeout, &app, &discardFixtureEdits);
		path->setText(temp.filePath(QStringLiteral("missing.map")));
		answer.start(); reload->click(); answer.stop();
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == kept && shell.levelDocument().undoStack.size() == undo
			&& !listLevelMapRecoveries(recoveryRoot).isEmpty() && path->text() == smallPath, "failed open preserves the complete editing session");
	}

	ok &= shell.saveLevelDocument(smallPath, true, &error);
	bool editedDuringLoad = false;
	QTimer::singleShot(0, &app, [&] {
		// The nested request must not open a second worker or replace this map.
		shell.openPathFromCommandLine(smallPath);
		editedDuringLoad = shell.applyLevelMaterialPaint({{LevelMaterialKind::BrushFace, 0, 1}}, QStringLiteral("studio/during-load"), &error);
	});
	shell.openPathFromCommandLine(largePath);
	ok &= expect(editedDuringLoad && shell.levelDocument().sourcePath == smallPath && shell.levelDocument().brushes.size() == 16
		&& shell.levelDocument().brushes[0].faces[1].textureName == QStringLiteral("studio/during-load")
		&& shell.levelDocument().undoStack.size() == undo + 1, "stale worker cannot overwrite an edit made during modal event processing");
	return ok ? 0 : 1;
}
