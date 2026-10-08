#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/model_fingerprint.h"
#include "core/studio_settings.h"
#include "tests/model_intersections_test_helpers.h"
#include "tests/model_scale_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontMetrics>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool condition, const char *message)
{
	++checks;
	if (!condition)
		std::cerr << "FAIL: " << message << '\n';
	return condition;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor")
			return {};
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Test-owned offscreen values, signals and QWidget::render; no input injection or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("intersections-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	auto mesh = tests::intersectionFixture();
	mesh.surfaces.removeLast();
	mesh.animations.append({QStringLiteral("clear-only"), 0, 1, 20});
	updateEditableModelMetadata(&mesh);
	const auto fingerprint = modelStateFingerprint(mesh);
	bool ok = true;
	QString error;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		ModelEditorDialog editor;
		editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(mesh, &error), "open animated intersection fixture");
		editor.resize(scenario == 0 ? 1360 : 2160, scenario == 0 ? 960 : 1460);
		editor.show();
		app.processEvents();
		auto *inspect = editor.findChild<QPushButton *>("inspectMeshIntersections");
		auto *scope = editor.findChild<QComboBox *>("meshIntersectionScope");
		auto *finding = editor.findChild<QComboBox *>("meshIntersectionFinding");
		auto *first = editor.findChild<QPushButton *>("showMeshIntersectionFirst");
		auto *second = editor.findChild<QPushButton *>("showMeshIntersectionSecond");
		auto *status = editor.findChild<QLabel *>("meshIntersectionStatus");
		auto *detail = editor.findChild<QLabel *>("meshIntersectionDetail");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		ok &= expect(inspect && scope && finding && first && second && status && detail && preview && tabs, "intersection controls exist");
		if (!inspect || !scope || !finding || !first || !second || !status || !detail || !preview || !tabs)
			return 1;
		QScrollArea *page = nullptr;
		for (int i = 0; i < tabs->count(); ++i)
			if (tabs->widget(i)->isAncestorOf(inspect))
			{
				tabs->setCurrentIndex(i);
				page = tests::pageScroll(tabs->widget(i));
			}
		ok &= expect(scope->currentIndex() == 0 && !first->isEnabled() && !second->isEnabled(), "default all-pose scan requires a report");
		const auto beforeSelection = editor.document().selection();
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.inspectIntersections(&error) && finding->count() == 0 && !editor.document().canUndo() &&
						 editor.document().selection() == beforeSelection && modelStateFingerprint(editor.document().mesh()) == fingerprint,
					 "cancelled inspection preserves geometry, selection, report and history");
		inspect->click();
		ok &= expect(finding->count() == 2 && finding->isEnabled() && first->isEnabled() && second->isEnabled() &&
						 status->text().contains("Face pairs: 2") && status->text().contains("Poses scanned: 3"),
					 "worker publishes complete all-pose findings");
		ok &= expect(modelStateFingerprint(editor.document().mesh()) == fingerprint && !editor.document().canUndo(),
					 "scan does not modify the source");
		for (auto *button : {inspect, first, second})
		{
			const auto *accessible = QAccessible::queryAccessibleInterface(button);
			ok &= expect(accessible && accessible->role() == QAccessible::Button && !accessible->text(QAccessible::Name).isEmpty() &&
							 !accessible->text(QAccessible::Description).isEmpty() && button->focusPolicy() != Qt::NoFocus,
						 "actions expose names, roles, descriptions and keyboard focus");
			ok &= expect(button->fontMetrics().horizontalAdvance(button->text()) + 24 <= button->width(), "expanded action text fits");
		}
		ok &= expect(!scope->accessibleName().isEmpty() && !scope->accessibleDescription().isEmpty() &&
						 !finding->accessibleName().isEmpty() && !finding->accessibleDescription().isEmpty() &&
						 !status->accessibleDescription().isEmpty(),
					 "scan controls expose accessible metadata");
		const auto capture = [&](const char *side) {
			if (page)
				page->ensureWidgetVisible(second);
			app.processEvents();
			ok &= expect(tests::settleModelViewport(*preview), "intersection preview settles");
			ok &= expect(!page || page->horizontalScrollBar()->maximum() == 0, "inspector has no horizontal overflow");
			const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
			if (!evidence.isEmpty())
			{
				QImage image(editor.size() * editor.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
				image.setDevicePixelRatio(editor.devicePixelRatioF());
				image.fill(Qt::transparent);
				editor.render(&image);
				ok &= expect(image.save(QDir(evidence).filePath(
								 QString("mesh-intersections-%1-%2-%3x.png").arg(scenario).arg(side).arg(editor.devicePixelRatioF()))),
							 "retain widget-owned intersection capture");
			}
		};
		auto *clip = editor.findChild<QComboBox *>("meshAnimationClip");
		if (!clip)
			return 1;
		clip->setCurrentIndex(clip->findData(1));
		ok &= expect(preview->animationIndex() == 1, "choose a preview clip outside the reported pose");
		first->click();
		preview->frameModel();
		// Offscreen windows lose activation when another top-level opens.
		editor.activateWindow();
		app.processEvents();
		first->setFocus(Qt::OtherFocusReason);
		ok &= expect(editor.document().selection().surface == 0 && editor.document().selection().faces == QSet<int>{0} &&
						 preview->frame() == 1 && !preview->isPlaying() && detail->text().contains("floor") &&
						 detail->text().contains("moving") && detail->text().contains("Crossing"),
					 "first action selects the exact face and affected pose");
		ok &= expect(QApplication::focusWidget() == first, "first-face control accepts focus");
		capture("first");
		second->click();
		second->setFocus(Qt::OtherFocusReason);
		ok &= expect(editor.document().selection().surface == 1 && editor.document().selection().faces == QSet<int>{0} &&
						 preview->frame() == 1 && modelStateFingerprint(editor.document().mesh()) == fingerprint &&
						 !editor.document().canUndo(),
					 "second action changes only view and selection");
		capture("second");
		finding->setCurrentIndex(1);
		first->click();
		ok &= expect(preview->frame() == 2 && detail->text().contains("Coplanar overlap"), "coplanar finding shows its stored pose");
		ModelEdit edit;
		edit.kind = ModelEditKind::Transform;
		edit.selection = editor.document().selection();
		edit.translation = {.25f, 0, 0};
		ok &= expect(editor.applyEdit(edit, &error) && !finding->isEnabled() && !first->isEnabled() && !second->isEnabled(),
					 "edits invalidate diagnostics");
		editor.findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(modelStateFingerprint(editor.document().mesh()) == fingerprint && !editor.document().canUndo(),
					 "undo restores exact source");
		scope->setCurrentIndex(1);
		preview->setFrame(0);
		inspect->click();
		ok &= expect(finding->count() == 0 && !first->isEnabled() && status->text().contains("Face pairs: 0") &&
						 status->text().contains("Poses scanned: 1"),
					 "current clear pose is explicitly inspected");
		preview->setFrame(1);
		ok &= expect(!finding->isEnabled() && status->text().contains("Inspect the mesh"),
					 "changing a single scanned pose invalidates its report");
		inspect->click();
		ok &= expect(finding->count() == 1 && first->isEnabled(), "current positive pose has one pair");
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.inspectIntersections(&error) && finding->count() == 1 && first->isEnabled() &&
						 modelStateFingerprint(editor.document().mesh()) == fingerprint,
					 "cancelled repeat preserves the last complete report");
		preview->setReducedMotion(false);
		preview->setFramesPerSecond(30);
		QEventLoop advancing;
		QTimer deadline;
		deadline.setSingleShot(true);
		QObject::connect(preview, &ModelViewport::frameChanged, &advancing, &QEventLoop::quit);
		QObject::connect(&deadline, &QTimer::timeout, &advancing, &QEventLoop::quit);
		preview->play();
		deadline.start(1000);
		advancing.exec();
		deadline.stop();
		ok &= expect(preview->isPlaying() && preview->frame() != 1 && !first->isEnabled() && !second->isEnabled() &&
						 status->text().contains("Inspect the mesh"),
					 "actual playback invalidates a current-pose report before navigation");
		preview->pause();
		preview->setReducedMotion(true);
		ok &= expect(editor.setMesh(mesh, &error), "retire fixture recovery state");
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelEditorDialog large;
	large.resize(1600, 1000);
	large.setAccessibility(false, true);
	large.show();
	app.processEvents();
	auto *preview = large.findChild<ModelViewport *>("meshPreview");
	if (!preview)
		return 1;
	const auto measure = [&](const char *stage, auto operation) {
		QElapsedTimer elapsed;
		elapsed.start();
		qint64 previous = 0, gap = 0;
		int beats = 0;
		const auto beat = [&] {
			const auto now = elapsed.nsecsElapsed();
			gap = std::max(gap, now - previous);
			previous = now;
			++beats;
		};
		QTimer timer;
		timer.setTimerType(Qt::PreciseTimer);
		timer.setInterval(5);
		QObject::connect(&timer, &QTimer::timeout, &app, beat);
		timer.start();
		operation();
		ok &= expect(tests::settleModelViewport(*preview, 60000), "maximum-grid preview settles");
		app.processEvents();
		beat();
		timer.stop();
		const double budget = qEnvironmentVariable("VIBESTUDIO_MODELLER_MAX_AUTHORING_GAP_MS").toDouble();
		ok &= expect(gap / 1e6 <= (budget > 0 ? budget : 300), "maximum-grid scan keeps the editor responsive");
		std::cout << QJsonDocument(QJsonObject{{"stage", QString::fromLatin1(stage)},
											   {"elapsedMs", elapsed.nsecsElapsed() / 1e6},
											   {"maxEventGapMs", gap / 1e6},
											   {"beats", beats},
											   {"vertices", large.document().mesh().vertexCount},
											   {"triangles", large.document().mesh().triangleCount},
											   {"frames", large.document().mesh().frameCount}})
						 .toJson(QJsonDocument::Compact)
						 .constData()
				  << '\n';
	};
	const auto maximum = tests::maximumEditableGrid();
	measure("intersections-load", [&] { ok &= expect(large.setMesh(maximum, &error), "load maximum animated grid"); });
	measure("intersections-scan", [&] { ok &= expect(large.inspectIntersections(&error), "scan all sixteen maximum-grid poses"); });
	ok &= expect(large.findChild<QLabel *>("meshIntersectionStatus")->text().contains("Face pairs: 0") && !large.document().canUndo(),
				 "maximum scan is complete and read-only");
	const auto crowd = tests::intersectionCrowd();
	measure("intersections-crowd-load", [&] { ok &= expect(large.setMesh(crowd, &error), "load dense positive diagnostic fixture"); });
	measure("intersections-crowd-scan", [&] { ok &= expect(large.inspectIntersections(&error), "scan near-limit positive findings"); });
	auto *findings = large.findChild<QComboBox *>("meshIntersectionFinding");
	ok &= expect(findings->count() == 65341 &&
					 findings->model()->data(findings->model()->index(65340, 0)).toString().contains("0:360 / 0:361"),
				 "large finder formats its last row on demand without losing face identity");
	findings->setCurrentIndex(65340);
	large.findChild<QPushButton *>("showMeshIntersectionSecond")->click();
	ok &= expect(large.document().selection().faces == QSet<int>{361} && !large.document().canUndo(),
				 "last positive finding selects its exact face");
	ok &= expect(large.setMesh(mesh, &error), "retire maximum diagnostic state");
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " intersection GUI checks\n";
	return ok ? 0 : 1;
}
