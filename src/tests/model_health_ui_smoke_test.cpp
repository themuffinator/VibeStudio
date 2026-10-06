#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_health_test_helpers.h"
#include "tests/model_nonmanifold_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <cmath>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool condition, const char *message)
{
	++checks;
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor")
		{
			return {};
		}
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
} // namespace
int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	// Public Qt values/signals and QWidget::render; no injected input or OS capture.
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
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-health-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	const auto original = tests::healthFixture();
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		ModelEditorDialog editor;
		editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open health UI fixture");
		editor.resize(scenario == 0 ? 1320 : 2080, scenario == 0 ? 920 : 1360);
		editor.show();
		app.processEvents();
		auto *inspect = editor.findChild<QPushButton *>("inspectMeshTopology");
		auto *select = editor.findChild<QPushButton *>("selectMeshHealthFinding");
		auto *repair = editor.findChild<QPushButton *>("repairMeshHealthFinding");
		auto *finding = editor.findChild<QComboBox *>("meshHealthFinding");
		auto *status = editor.findChild<QLabel *>("meshHealthStatus");
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *tabs = editor.findChild<QTabWidget *>("meshInspector");
		auto *preview = editor.findChild<ModelViewport *>("meshPreview");
		ok &= expect(inspect && select && repair && finding && status && mode && tabs && preview, "health controls exist");
		if (!inspect || !select || !repair || !finding || !status || !mode || !tabs || !preview)
		{
			return EXIT_FAILURE;
		}
		for (int i = 0; i < tabs->count(); ++i)
		{
			if (tabs->widget(i)->isAncestorOf(inspect))
			{
				tabs->setCurrentIndex(i);
			}
		}
		ok &= expect(!select->isEnabled() && !repair->isEnabled(), "unscanned report cannot select or repair");
		inspect->click();
		ok &= expect(select->isEnabled() && repair->isEnabled() && !editor.document().isModified() && !editor.document().canUndo(),
					 "inspection is read-only");
		const QList<QWidget *> controls{inspect, select, repair, finding};
		for (auto *control : controls)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !control->accessibleDescription().isEmpty() &&
							 control->focusPolicy() != Qt::NoFocus,
						 "health controls expose accessible names, descriptions and focus");
		}
		select->click();
		ok &= expect(editor.document().selection().faces == QSet<int>{0, 2} && mode->currentIndex() == 0,
					 "select duplicate findings including retained face");
		finding->setCurrentIndex(4);
		select->click();
		ok &= expect(editor.document().selection().edges == QSet<ModelEdge>{{0, 2}} && mode->currentIndex() == 2 && repair->isEnabled(),
					 "select branching edge with explicit topology splitting available");
		finding->setCurrentIndex(5);
		select->click();
		ok &= expect(editor.document().selection().edges.size() == 5 && !repair->isEnabled(), "boundaries remain informational");
		finding->setCurrentIndex(0);
		select->click();
		ok &= expect(tests::settleModelViewport(*preview), "health selection renders in viewport");
		app.processEvents();
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
		{
			ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "health UI uses actual 2x device pixel ratio");
		}
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		for (auto *area : editor.findChildren<QScrollArea *>())
		{
			if (area->isAncestorOf(inspect))
			{
				ok &= expect(area->horizontalScrollBar()->maximum() == 0, "health descriptions wrap without horizontal overflow");
				for (auto *control : controls)
				{
					const auto bounds = QRect(control->mapTo(area->viewport(), QPoint()), control->size());
					ok &= expect(bounds.left() >= 0 && bounds.right() < area->viewport()->width(),
								 "health controls fit the visible inspector width");
				}
			}
		}
		if (!evidence.isEmpty())
		{
			QImage image(editor.size() * editor.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(editor.devicePixelRatioF());
			image.fill(Qt::transparent);
			editor.render(&image);
			ok &=
				expect(image.save(QDir(evidence).filePath(QString("mesh-health-%1-%2x.png").arg(scenario).arg(editor.devicePixelRatioF()))),
					   "save health widget render");
		}
		repair->click();
		ok &= expect(editor.document().mesh().triangleCount == 3 && editor.document().selection().faces == QSet<int>{0} &&
						 !repair->isEnabled(),
					 "repair commits and refreshes findings");
		finding->setCurrentIndex(1);
		select->click();
		ok &= expect(editor.document().selection().vertices == QSet<int>{6}, "select unused vertex");
		repair->click();
		ok &= expect(editor.document().mesh().vertexCount == 6 && editor.document().selection().vertices.isEmpty(),
					 "remove unused vertex through UI");
		finding->setCurrentIndex(2);
		select->click();
		repair->click();
		ok &= expect(editor.document().selection().vertices == QSet<int>{0, 6}, "fan repair keeps both vertex copies selected");
		finding->setCurrentIndex(3);
		repair->click();
		ModelTopologyHealth health;
		ok &= expect(inspectModelTopology(editor.document().mesh().surfaces[0], &health, &error) && health.windingEdges.isEmpty(),
					 "orient faces through shared service");
		const auto repaired = bytes(editor.document().mesh());
		for (int i = 0; i < 4; ++i)
		{
			editor.findChild<QAction *>("undoMesh")->trigger();
		}
		ok &= expect(bytes(editor.document().mesh()) == bytes(original) && !repair->isEnabled() && !select->isEnabled(),
					 "undo invalidates stale health report");
		for (int i = 0; i < 4; ++i)
		{
			editor.findChild<QAction *>("redoMesh")->trigger();
		}
		ok &= expect(bytes(editor.document().mesh()) == repaired, "redo restores complete repair");
		mode->setCurrentIndex(3);
		ok &= expect(!inspect->isEnabled() && !repair->isEnabled() && !select->isEnabled(), "tag mode disables geometry health actions");
		mode->setCurrentIndex(4);
		ok &= expect(!inspect->isEnabled() && !repair->isEnabled() && !select->isEnabled(),
					 "collision mode disables geometry health actions");
		mode->setCurrentIndex(0);
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.inspectTopology(&error) && bytes(editor.document().mesh()) == repaired,
					 "cancel health worker without modifying source");
		const auto book = tests::nonmanifoldBook(3, 2, false);
		ok &= expect(editor.setMesh(book, &error), "open nonmanifold repair fixture");
		preview->frameModel();
		inspect->click();
		finding->setCurrentIndex(4);
		select->click();
		ok &= expect(repair->isEnabled() &&
						 repair->text() == QCoreApplication::translate("VibeStudioModelEditor", "Split Nonmanifold Edges") &&
						 editor.document().selection().edges == QSet<ModelEdge>{{0, 1}},
					 "branch category offers explicit surface repair and exact edge selection");
		const auto branchSelection = editor.document().selection();
		const auto captureBranch = [&](const QString &stage) {
			ok &= expect(tests::settleModelViewport(*preview), "nonmanifold selection preview settles");
			app.processEvents();
			ok &= expect(repair->fontMetrics().horizontalAdvance(repair->text()) <= repair->contentsRect().width(),
						 "expanded nonmanifold action label fits without clipping");
			for (auto *area : editor.findChildren<QScrollArea *>())
				if (area->isAncestorOf(repair))
					ok &= expect(area->horizontalScrollBar()->maximum() == 0, "nonmanifold description fits RTL and scaled inspector");
			auto *accessible = QAccessible::queryAccessibleInterface(repair);
			ok &= expect(accessible && accessible->role() == QAccessible::Button && accessible->text(QAccessible::Name) == repair->text() &&
							 !repair->accessibleDescription().isEmpty() && repair->focusPolicy() != Qt::NoFocus,
						 "nonmanifold action exposes current accessible name, role, scope and focus");
			if (!evidence.isEmpty())
			{
				QImage image(editor.size() * editor.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
				image.setDevicePixelRatio(editor.devicePixelRatioF());
				image.fill(Qt::transparent);
				editor.render(&image);
				ok &= expect(image.save(QDir(evidence).filePath(
								 QString("mesh-nonmanifold-%1-%2-%3x.png").arg(scenario).arg(stage).arg(editor.devicePixelRatioF()))),
							 "save nonmanifold widget render");
			}
		};
		captureBranch(QStringLiteral("before"));
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		repair->click();
		ok &= expect(bytes(editor.document().mesh()) == bytes(book) && editor.document().selection() == branchSelection &&
						 !editor.document().canUndo(),
					 "cancelled branch repair preserves source, selection and history");
		repair->click();
		const auto cutBook = bytes(editor.document().mesh());
		ok &=
			expect(editor.document().mesh().vertexCount == 9 && editor.document().selection().edges.size() == 3 && !repair->isEnabled() &&
					   inspectModelTopology(editor.document().mesh().surfaces[0], &health, &error) && health.nonmanifoldEdges.isEmpty(),
				   "branch repair commits every pose, expands selection and refreshes health");
		captureBranch(QStringLiteral("after"));
		editor.findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(bytes(editor.document().mesh()) == bytes(book) && editor.document().selection() == branchSelection &&
						 !editor.document().canUndo(),
					 "one GUI undo restores all branching geometry and selected spine");
		editor.findChild<QAction *>("redoMesh")->trigger();
		ok &= expect(bytes(editor.document().mesh()) == cutBook, "GUI redo restores exact split");
		ok &= expect(editor.setMesh(original, &error), "retire health test recovery");
		if (scenario > 0)
		{
			app.removeTranslator(&expansion);
		}
	}
	// Measure the complete UI operation near the animation storage ceiling.
	// A Qt timer observes event-loop gaps; no input injection or desktop capture.
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelEditorDialog large;
	large.resize(1600, 1000);
	large.setAccessibility(false, true);
	large.show();
	app.processEvents();
	auto *largePreview = large.findChild<ModelViewport *>("meshPreview");
	if (!largePreview)
		return EXIT_FAILURE;
	const auto measure = [&](const char *stage, auto operation) {
		QElapsedTimer clock;
		clock.start();
		qint64 previous = 0, gap = 0;
		int beats = 0;
		const auto beat = [&] {
			const auto now = clock.nsecsElapsed();
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
		ok &= expect(tests::settleModelViewport(*largePreview, 60000), "large nonmanifold preview settles");
		app.processEvents();
		beat();
		timer.stop();
		const double gapMs = gap / 1e6;
		const double requested = qEnvironmentVariable("VIBESTUDIO_MODELLER_MAX_AUTHORING_GAP_MS").toDouble();
		ok &= expect(gapMs <= (requested > 0 ? requested : 300), "large nonmanifold operation keeps the event loop responsive");
		std::cout << QJsonDocument(QJsonObject{{"stage", QString::fromLatin1(stage)},
											   {"elapsedMs", clock.nsecsElapsed() / 1e6},
											   {"maxEventGapMs", gapMs},
											   {"beats", beats},
											   {"frames", 16},
											   {"outputVertices", 65535}})
						 .toJson(QJsonDocument::Compact)
						 .constData()
				  << '\n';
	};
	const auto maximum = tests::nonmanifoldBook(21845, 16, false);
	measure("nonmanifold-load", [&] { ok &= expect(large.setMesh(maximum, &error), "open maximum animated branch fixture"); });
	measure("nonmanifold-inspect", [&] { ok &= expect(large.inspectTopology(&error), "inspect maximum animated branch fixture"); });
	auto *finding = large.findChild<QComboBox *>("meshHealthFinding");
	auto *repair = large.findChild<QPushButton *>("repairMeshHealthFinding");
	auto *select = large.findChild<QPushButton *>("selectMeshHealthFinding");
	if (!finding || !repair || !select)
		return EXIT_FAILURE;
	finding->setCurrentIndex(4);
	select->click();
	measure("nonmanifold-repair", [&] { repair->click(); });
	ok &= expect(large.document().mesh().vertexCount == 65535 && large.document().selection().edges.size() == 21845 && !repair->isEnabled(),
				 "maximum GUI repair retains every page and updates findings");
	measure("nonmanifold-undo", [&] { large.findChild<QAction *>("undoMesh")->trigger(); });
	ok &= expect(large.document().mesh().vertexCount == 21847 && !large.document().canUndo(), "maximum GUI repair is one undo step");
	ok &= expect(large.setMesh(original, &error), "retire maximum health recovery state");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	std::cout << checks << " health GUI checks\n";
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
