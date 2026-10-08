#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_uv_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QJsonDocument>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTranslator>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
bool near(QPointF a, QPointF b)
{
	return QLineF(a, b).length() < 0.001;
}
QByteArray source(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
QImage capture(QWidget &widget)
{
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image;
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
		const auto name = QByteArray(context);
		if (name != "VibeStudioModelEditor" && name != "VibeStudioModelUvView")
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
	// Public geometry APIs and widget signals only. No input injection or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-uv-ui-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	bool ok = true;
	QString error;
	const auto original = tests::uvSquare();
	QImage texture(128, 64, QImage::Format_RGB32);
	texture.fill(QColor(35, 65, 90));
	ModelUvRenderRequest request;
	request.surface = original.surfaces[0];
	request.logicalSize = {640, 480};
	request.texture = texture;
	request.background = Qt::black;
	request.foreground = Qt::white;
	request.accent = Qt::yellow;
	ModelUvRenderResult rendered;
	ok &= expect(renderModelUv(request, &rendered, &error) && rendered.topology->islands.size() == 1 &&
					 std::abs(rendered.camera.m11() / rendered.camera.m22() - 2) < 1e-8 &&
					 rendered.image.pixelColor(10, 100) == texture.pixelColor(0, 0),
				 "UV renderer respects texture aspect and repeats outside the unit tile");
	request.logicalSize = {8192, 8192};
	request.pixelRatio = 4;
	ok &= expect(renderModelUv(request, &rendered, &error) && qint64(rendered.image.width()) * rendered.image.height() <= 4 * 1024 * 1024 &&
					 rendered.image.deviceIndependentSize().toSize() == QSize(8192, 8192),
				 "UV output bounds physical pixels while retaining logical size");
	request.logicalSize = {640, 480};
	request.pixelRatio = 1;
	request.fit = ModelUvFit::None;
	request.camera = QTransform(200, 0, 0, 200, 100, 100);
	request.surface.texCoords = {{-1000000, -1000000}, {1000000, -1000000}, {1000000, 1000000}, {-1000000, 1000000}};
	request.selection.faces = {0};
	ok &= expect(renderModelUv(request, &rendered, &error) && !rendered.image.isNull(), "extreme valid UVs clip before QPainter paths");
	const auto before = rendered.image.cacheKey();
	request.camera = QTransform(std::numeric_limits<double>::quiet_NaN(), 0, 0, 1, 0, 0);
	ok &= expect(!renderModelUv(request, &rendered, &error) && rendered.image.cacheKey() == before,
				 "nonfinite camera fails without publishing");
	ModelWorkControl cancelled;
	cancelled.cancelled = [] { return true; };
	request.camera = QTransform();
	ok &= expect(!renderModelUv(request, &rendered, &error, cancelled) && rendered.image.cacheKey() == before,
				 "UV render cancellation is atomic");
	{
		ModelUvView view;
		view.resize(640, 480);
		view.setSource(original.surfaces[0], {}, texture);
		view.show();
		app.processEvents();
		ok &= expect(tests::settleModelUv(view), "asynchronous UV view settles");
		view.resize(800, 600);
		app.processEvents();
		ok &= expect(tests::settleModelUv(view) && near(view.uvToScreen({0.5f, 0.5f}), QPointF(400, 300)),
					 "fitted UV view refits on resize or first tab presentation");
		const auto *accessible = QAccessible::queryAccessibleInterface(&view);
		ok &= expect(accessible && accessible->role() == QAccessible::Graphic && !view.accessibleDescription().isEmpty() &&
						 view.focusPolicy() == Qt::StrongFocus,
					 "UV view exposes a named graphic role, help, and keyboard focus");
		const auto point = view.uvToScreen({0.75f, 0.25f});
		ok &= expect(view.hitAt(point).kind == 0 && view.hitAt(point).a == 0, "face picking uses UV triangles");
		view.setPickMode(1);
		ok &= tests::settleModelUv(view);
		ok &= expect(view.hitAt(view.uvToScreen({1, 0})).a == 1 && view.hitAt(view.uvToScreen({1, 0})).kind == 1,
					 "vertex picking selects exact indexed UV corner");
		view.setPickMode(2);
		ok &= tests::settleModelUv(view);
		const auto edge = view.hitAt(view.uvToScreen({0.5f, 0.5f}));
		ok &= expect(edge.kind == 2 && edge.a == 0 && edge.b == 2, "edge picking returns canonical indexed endpoints");
		view.setPickMode(3);
		ok &= tests::settleModelUv(view);
		ok &= expect(view.hitAt(point).kind == 3 && view.hitAt(point).a == 0, "island picking returns the visible face seed");
		view.zoomAt(point, 2);
		ok &= tests::settleModelUv(view);
		ok &= expect(near(view.uvToScreen({0.75f, 0.25f}), point), "zoom keeps cursor UV anchored");
		view.panView({-20, 15});
		ok &= tests::settleModelUv(view);
		ok &= expect(near(view.uvToScreen({0.75f, 0.25f}), point + QPointF(-20, 15)), "pan translates the UV camera");
		const auto beforeResize = view.uvToScreen({0.75f, 0.25f});
		view.resize(900, 650);
		app.processEvents();
		ok &= expect(tests::settleModelUv(view) && near(view.uvToScreen({0.75f, 0.25f}), beforeResize + QPointF(50, 25)),
					 "manual UV camera keeps its centre and scale across resize");
		ModelSelection selection;
		selection.faces = {0};
		view.setSource(original.surfaces[0], selection, texture);
		view.frameSelection();
		ok &= tests::settleModelUv(view);
		ok &= expect(near(view.uvToScreen({0.5f, 0.5f}), QPointF(view.width() / 2.0, view.height() / 2.0)),
					 "frame selection centres selected UV bounds");
		int commits = 0;
		QObject::connect(&view, &ModelUvView::moveRequested, [&](double, double) { ++commits; });
		view.setMoveEnabled(true, 0.125);
		auto handle = view.moveHandle();
		const auto movement = view.uvToScreen({0.08f, 0}) - view.uvToScreen({0, 0});
		ok &= expect(view.beginMove(handle) && view.updateMove(handle + movement) && std::abs(view.moveDelta().u - 0.125f) < 1e-6f,
					 "UV move preview uses shared delta snapping");
		view.finishMove(false);
		ok &= expect(!view.moving() && commits == 0 && tests::settleModelUv(view), "cancelling retires preview and publishes no edit");
		handle = view.moveHandle();
		ok &= expect(view.beginMove(handle) && view.updateMove(handle + movement), "second UV drag starts");
		view.finishMove(true);
		ok &= expect(commits == 1 && tests::settleModelUv(view), "release publishes one UV offset");
		view.setSource(tests::uvGrid(128), {}, texture);
		QElapsedTimer elapsed;
		elapsed.start();
		int ticks = 0;
		QTimer heartbeat;
		heartbeat.setInterval(1);
		QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
		heartbeat.start();
		ok &= expect(tests::settleModelUv(view) && view.topology()->islands.size() == 1 && (ticks > 0 || elapsed.elapsed() < 2),
					 "large UV analysis and render keep GUI events responsive");
		heartbeat.stop();
		std::cout << "UV grid: 32768 triangles, " << elapsed.elapsed() << " ms, " << ticks << " GUI timer ticks\n";
		view.setPickMode(0);
		for (const auto cells : {QSize(255, 255), QSize(32767, 1)})
		{
			const auto surface = tests::uvGrid(cells.width(), cells.height());
			ModelSelection selected;
			for (int face = 0; face < surface.triangles.size(); ++face)
			{
				selected.faces.insert(face);
			}
			qint64 previous = 0, gap = 0;
			int beats = 0;
			QTimer responsive;
			responsive.setInterval(5);
			responsive.setTimerType(Qt::PreciseTimer);
			const auto beat = [&] {
				const auto now = elapsed.nsecsElapsed();
				gap = std::max(gap, now - previous);
				previous = now;
				++beats;
			};
			QObject::connect(&responsive, &QTimer::timeout, &app, beat);
			elapsed.restart();
			responsive.start();
			view.setSource(surface, selected, texture);
			view.frameAll();
			ok &= expect(tests::settleModelUv(view, 60000), "maximum selected UV layout completes through the production worker");
			beat();
			responsive.stop();
			const double renderMs = elapsed.nsecsElapsed() / 1e6;
			const double gapBudget = qEnvironmentVariable("VIBESTUDIO_MODELLER_MAX_EVENT_GAP_MS").toDouble();
			const double renderBudget = qEnvironmentVariable("VIBESTUDIO_MODELLER_MAX_UV_VIEW_MS").toDouble();
			ok &= expect(beats > 1 && gap / 1e6 <= (gapBudget > 0 ? gapBudget : 300),
						 "maximum UV selection preserves event-loop responsiveness");
			ok &= expect(renderBudget <= 0 || renderMs <= renderBudget, "maximum UV worker stays within its explicit completion budget");
			const ModelTexCoord target{.3753125f, .63125f};
			const double u = double(target.u) * cells.width(), v = double(target.v) * cells.height();
			const int expected = (int(v) * cells.width() + int(u)) * 2 + ((u - int(u)) > (v - int(v)) ? 0 : 1);
			const auto hit = view.hitAt(view.uvToScreen(target));
			ok &= expect(hit.kind == 0 && hit.a == expected && view.topology()->faceIsland.size() == surface.triangles.size(),
						 "maximum layout keeps exact indexed-face picking and complete topology");
			std::cout << QJsonDocument(QJsonObject{{"stage", "selected-uv-worker"},
												   {"columns", cells.width() + 1},
												   {"rows", cells.height() + 1},
												   {"vertices", surface.vertexCount},
												   {"triangles", surface.triangles.size()},
												   {"pixelRatio", view.devicePixelRatioF()},
												   {"elapsedMs", renderMs},
												   {"maxEventGapMs", gap / 1e6},
												   {"beats", beats}})
							 .toJson(QJsonDocument::Compact)
							 .constData()
					  << '\n';
		}
		view.setSource(tests::uvGrid(127), {}, texture);
		app.processEvents();
		auto newer = original.surfaces[0];
		newer.uvSeams = {{0, 2}};
		view.setSource(newer, {}, texture);
		ok &= expect(tests::settleModelUv(view) && view.topology()->islands.size() == 2 && !view.pixmap().isNull(),
					 "new surface retires an in-flight larger topology request");
	}
	ModelEditorDialog editor;
	editor.setAttribute(Qt::WA_DeleteOnClose, false);
	editor.findChild<QCheckBox *>("meshRecoveryEnabled")->setChecked(false);
	ok &= expect(editor.setMesh(original, &error), "open UV editor fixture");
	editor.resize(1440, 960);
	editor.show();
	app.processEvents();
	auto *view = editor.findChild<ModelUvView *>("meshUvPreview");
	auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
	auto *table = editor.findChild<QTableView *>("meshComponents");
	auto *pick = editor.findChild<QAction *>("pickMeshUvIslands");
	auto *inspector = editor.findChild<QTabWidget *>("meshInspector");
	if (!view || !mode || !table || !pick || !inspector)
	{
		std::cerr << "Missing UV controls\n";
		return EXIT_FAILURE;
	}
	for (auto *tabs : editor.findChildren<QTabWidget *>())
	{
		for (int index = 0; index < tabs->count(); ++index)
		{
			if (tabs->widget(index)->isAncestorOf(view))
			{
				tabs->setCurrentIndex(index);
			}
		}
	}
	editor.showSidebarPage(QStringLiteral("surface"));
	app.processEvents();
	ok &= tests::settleModelUv(*view);
	view->componentPicked(0, 0, -1, false);
	ok &= expect(editor.document().selection().faces == QSet<int>{0} && table->selectionModel()->selectedRows().size() == 1 &&
					 !editor.document().isModified(),
				 "UV pick synchronizes table and document selection without history");
	editor.findChild<QAction *>("selectMeshUvIslands")->trigger();
	ok &= expect(editor.document().selection().faces == QSet<int>{0, 1} && !editor.document().isModified(),
				 "Select Islands expands through the shared worker without dirtying source");
	mode->setCurrentIndex(2);
	ok &= tests::settleModelUv(*view);
	view->componentPicked(2, 0, 2, false);
	editor.findChild<QPushButton *>("markMeshUvSeams")->click();
	ok &= tests::settleModelUv(*view);
	ok &= expect(editor.document().mesh().surfaces[0].uvSeams == QSet<ModelEdge>{{0, 2}} && view->topology()->islands.size() == 2,
				 "marking selected UV edge updates island analysis and document");
	mode->setCurrentIndex(0);
	pick->setChecked(true);
	ok &= tests::settleModelUv(*view);
	view->componentPicked(3, 0, -1, false);
	ok &= tests::settleModelUv(*view);
	ok &= expect(editor.document().selection().faces == QSet<int>{0}, "island pick respects the marked seam");
	const auto beforeMove = source(editor.document().mesh());
	auto handle = view->moveHandle();
	const auto step = view->uvToScreen({0.2f, 0.1f}) - view->uvToScreen({0, 0});
	ok &= expect(view->beginMove(handle) && view->updateMove(handle + step) && source(editor.document().mesh()) == beforeMove,
				 "drag preview leaves document and history untouched");
	view->finishMove(false);
	ok &= expect(source(editor.document().mesh()) == beforeMove && tests::settleModelUv(*view),
				 "editor UV cancellation restores source view");
	handle = view->moveHandle();
	ok &= expect(view->beginMove(handle) && view->updateMove(handle + step), "editor UV drag starts after cancellation");
	view->finishMove(true);
	ok &= expect(editor.document().mesh().vertexCount == 6 && tests::settleModelUv(*view), "UV drag commits isolated face corners");
	editor.findChild<QAction *>("undoMesh")->trigger();
	ok &= expect(source(editor.document().mesh()) == beforeMove, "one undo restores the complete pre-drag source");
	mode->setCurrentIndex(0);
	ok &= tests::settleModelUv(*view);
	view->componentPicked(3, 0, -1, false);
	auto *pivot = editor.findChild<QComboBox *>("meshUvPivotMode");
	auto *custom = editor.findChild<QDoubleSpinBox *>("meshUvPivot0");
	pivot->setCurrentIndex(2);
	ok &= expect(custom->isEnabled(), "custom UV pivot enables numeric coordinates");
	pivot->setCurrentIndex(1);
	editor.findChild<QDoubleSpinBox *>("meshUvScale0")->setValue(2);
	editor.findChild<QPushButton *>("transformMeshUvs")->click();
	const auto &surface = editor.document().mesh().surfaces[0];
	ok &= expect(std::abs(surface.texCoords[surface.triangles[0].a].u + 0.5f) < 1e-6f && !custom->isEnabled(),
				 "numeric UV transform uses selected pivot mode");
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (!evidence.isEmpty())
	{
		QDir().mkpath(evidence);
		ok &= tests::settleModelUv(*view);
		capture(editor).save(QDir(evidence).filePath(QStringLiteral("uv-editor-%1x.png").arg(editor.devicePixelRatioF(), 0, 'g', 2)));
	}
	Expansion expansion;
	app.installTranslator(&expansion);
	{
		applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 200));
		ModelEditorDialog expanded;
		expanded.setAttribute(Qt::WA_DeleteOnClose, false);
		expanded.findChild<QCheckBox *>("meshRecoveryEnabled")->setChecked(false);
		expanded.setLayoutDirection(Qt::RightToLeft);
		expanded.resize(1700, 1100);
		expanded.setMesh(original, &error);
		expanded.show();
		app.processEvents();
		auto *uv = expanded.findChild<ModelUvView *>("meshUvPreview");
		for (auto *tabs : expanded.findChildren<QTabWidget *>())
		{
			for (int index = 0; index < tabs->count(); ++index)
			{
				if (tabs->widget(index)->isAncestorOf(uv))
				{
					tabs->setCurrentIndex(index);
				}
			}
		}
		expanded.showSidebarPage(QStringLiteral("surface"));
		app.processEvents();
		ok &= expect(tests::settleModelUv(*uv) && uv->width() >= 200 && uv->height() >= 180,
					 "expanded RTL high-contrast UV view remains usable");
		for (const auto *name : {"meshUvPivotMode", "meshUvGrid", "meshUvPivot0", "meshUvPivot1"})
		{
			const auto *control = expanded.findChild<QWidget *>(name);
			ok &= expect(control && !control->accessibleName().isEmpty() && control->focusPolicy() != Qt::NoFocus && control->width() > 50,
						 "numeric UV controls remain named, focusable, and laid out");
		}
		if (!evidence.isEmpty())
		{
			capture(expanded).save(
				QDir(evidence).filePath(QStringLiteral("uv-editor-expanded-rtl-%1x.png").arg(expanded.devicePixelRatioF(), 0, 'g', 2)));
		}
	}
	app.removeTranslator(&expansion);
	std::cout << "UV view device pixel ratio: " << editor.devicePixelRatioF() << '\n';
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
