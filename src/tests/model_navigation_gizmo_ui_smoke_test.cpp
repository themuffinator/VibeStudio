// The Mesh Editor's navigation gizmo: in the view's trailing top corner (the
// left one right to left), clear of the heads-up tags, the tool banner and
// Adjust Last Operation, with click targets at least 24 px across that grow
// with the text, and each positive bubble looking along its axis. Runs at
// 100% and 200% text, left to right and right to left, in an 800x600 and a
// 500x400 view and the active pane of four views. Adjust Last Operation sits
// in the bottom leading corner. VIBESTUDIO_MODELLER_EVIDENCE receives a render
// of each view.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/model_design.h"
#include "core/studio_settings.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

using namespace vibestudio;
namespace
{
int failures = 0;
bool expect(bool condition, const std::string &message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}

bool until(const std::function<bool()> &condition)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < 5000)
		QTest::qWait(10);
	return condition();
}

// A flat 4x4 grid of quads, named so both heads-up tags fit an 800 px view
// at 200% text and only the leading one fits 500 px.
ModelMesh gridMesh()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	auto &surface = mesh.surfaces[0];
	constexpr int cells = 4;
	QVector<ModelVec3> positions;
	QVector<ModelTriangle> triangles;
	for (int j = 0; j <= cells; ++j)
	{
		for (int i = 0; i <= cells; ++i)
			positions.append({float(i * 8), float(j * 8), 0});
	}
	const auto at = [](int i, int j) { return j * (cells + 1) + i; };
	for (int j = 0; j < cells; ++j)
	{
		for (int i = 0; i < cells; ++i)
		{
			triangles.append({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
			triangles.append({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
		}
	}
	surface.triangles = triangles;
	surface.vertexCount = positions.size();
	surface.texCoords.clear();
	for (const auto &p : positions)
		surface.texCoords.append({p.x / 32.0f, p.y / 32.0f});
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame};
	surface.uvSeams.clear();
	mesh.sourcePath = QStringLiteral("models/players/sarge/upper.md3");
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void capture(ModelViewport *widget, const QString &name)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (root.isEmpty() || !QDir().mkpath(root))
		return;
	tests::settleModelViewport(*widget);
	QImage image(widget->size() * widget->devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget->devicePixelRatioF());
	image.fill(Qt::transparent);
	widget->render(&image);
	expect(image.save(QDir(root).filePath(QStringLiteral("navigation-gizmo-%1.png").arg(name))), "save the evidence render " + name.toStdString());
}

bool clear(const QRectF &gizmo, const QRectF &other)
{
	return other.isEmpty() || !gizmo.intersects(other);
}

// Every placement rule, then a click on each positive bubble.
void checkView(QApplication &app, ModelEditorDialog *editor, bool rtl, const std::string &name, double *bubbleRadius)
{
	auto *tools = editor->tools();
	auto *viewport = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *banner = viewport->findChild<QLabel *>(QStringLiteral("meshOperatorBanner"));
	auto *panel = tools->lastOperationPanel();
	viewport->setOrbit(35, 25);
	app.processEvents();
	const QRectF view(viewport->rect());
	auto gizmo = tools->navigationGizmo();
	const auto hud = viewport->hudLayout();
	if (!expect(!gizmo.bounds.isEmpty(), name + ": the gizmo has room"))
		return;
	*bubbleRadius = gizmo.bubbleRadius;
	expect(gizmo.bubbleRadius >= 12 && gizmo.bubbles.size() == 6, name + ": six bubbles, each a click target at least 24 px across");
	expect(view.adjusted(8, 8, -8, -8).contains(gizmo.bounds), name + ": the gizmo stays inside the view's margins");
	expect(rtl ? std::abs(gizmo.bounds.left() - 8) < 0.5 : std::abs(gizmo.bounds.right() - (view.width() - 8)) < 0.5,
		   name + ": the gizmo sits in the trailing corner");
	expect(!hud.leading.bounds.isEmpty(), name + ": the heads-up line is laid out");
	expect(clear(gizmo.bounds, hud.leading.bounds) && clear(gizmo.bounds, hud.trailing.bounds), name + ": the gizmo keeps clear of both heads-up tags");
	for (const auto &bubble : gizmo.bubbles)
		expect(gizmo.bounds.adjusted(-0.5, -0.5, 0.5, 0.5).contains(QRectF(bubble.at - QPointF(gizmo.bubbleRadius, gizmo.bubbleRadius),
																		   QSizeF(gizmo.bubbleRadius * 2, gizmo.bubbleRadius * 2))),
			   name + ": every bubble lies on the backdrop");
	capture(viewport, QString::fromStdString(name).replace(QLatin1Char(' '), QLatin1Char('-')).remove(QLatin1Char('%')));

	// The tool banner and Adjust Last Operation push it clear.
	tools->action(QStringLiteral("meshSelectBox"))->trigger();
	app.processEvents();
	if (expect(banner && banner->isVisible(), name + ": Box Select shows its banner"))
	{
		const auto withBanner = tools->navigationGizmo();
		expect(withBanner.bounds.isEmpty() || clear(withBanner.bounds, QRectF(banner->geometry())), name + ": the gizmo keeps clear of the banner");
	}
	viewport->setFocus(Qt::OtherFocusReason);
	QTest::keyClick(viewport, Qt::Key_Escape);
	app.processEvents();
	tools->applySelection({0, {0, 1}, {}});
	auto edit = tools->toolEdit(ModelEditKind::Transform);
	edit.translation = {1, 0, 0};
	QString error;
	expect(tools->runTool(edit, &error), name + ": a move runs");
	if (!panel->isVisible())
		tools->action(QStringLiteral("meshAdjustLast"))->trigger();
	app.processEvents();
	if (expect(panel->isVisible(), name + ": Adjust Last Operation is open"))
	{
		const QRectF placed(panel->geometry());
		expect(rtl ? std::abs(placed.right() + 1 - (view.width() - 8)) <= 1 : std::abs(placed.left() - 8) < 0.5,
			   name + ": Adjust Last Operation sits in the bottom leading corner");
		const auto withPanel = tools->navigationGizmo();
		expect(withPanel.bounds.isEmpty() || clear(withPanel.bounds, placed), name + ": the gizmo keeps clear of Adjust Last Operation");
		capture(viewport, QString::fromStdString(name + " last").replace(QLatin1Char(' '), QLatin1Char('-')).remove(QLatin1Char('%')));
		tools->action(QStringLiteral("meshAdjustLast"))->trigger();
		app.processEvents();
	}
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	app.processEvents();

	// A click on each positive bubble looks along that axis, back at the model.
	for (int axis = 0; axis < 3; ++axis)
	{
		viewport->setOrbit(35, 25);
		app.processEvents();
		gizmo = tools->navigationGizmo();
		QPointF at;
		bool found = false;
		for (const auto &bubble : gizmo.bubbles)
		{
			if (bubble.axis == axis && bubble.positive)
			{
				at = bubble.at;
				found = true;
			}
		}
		if (!expect(found, name + ": each axis has a positive bubble"))
			continue;
		QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at.toPoint());
		app.processEvents();
		const auto forward = viewport->viewForward();
		const double along = axis == 0 ? -forward.x : axis == 1 ? -forward.y : -forward.z;
		expect(along > 0.99, name + ": clicking +" + std::string(1, "XYZ"[axis]) + " looks along that axis");
	}
}

void run(QApplication &app, int variant, const QString &settingsPath)
{
	const bool large = variant >= 2, rtl = variant % 2 == 1;
	StudioSettings::setOverrideFilePath(settingsPath);
	applyStudioTheme(app, studioThemeTokens(large ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, large ? 200 : 100));
	app.setLayoutDirection(rtl ? Qt::RightToLeft : Qt::LeftToRight);
	const std::string pass = std::string(large ? "200%" : "100%") + (rtl ? " RTL" : " LTR");
	auto *editor = new ModelEditorDialog;
	editor->resize(large ? 2400 : 1600, large ? 1500 : 1000);
	editor->show();
	app.processEvents();
	QString error;
	if (!expect(editor->setMesh(gridMesh(), &error), pass + ": load a grid"))
	{
		delete editor;
		return;
	}
	auto *viewport = editor->findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	expect(tests::settleModelViewport(*viewport), pass + ": the view settles");
	static double small = 0;
	for (const QSize size : {QSize(800, 600), QSize(500, 400)})
	{
		viewport->setFixedSize(size);
		const std::string name = pass + " " + std::to_string(size.width()) + "x" + std::to_string(size.height());
		if (!expect(until([&] { return viewport->size() == size; }), name + ": the view takes its size"))
			continue;
		double radius = 0;
		checkView(app, editor, rtl, name, &radius);
		// The 200% gizmo is bigger than the 100% one in the same view.
		if (size.width() == 800 && !large)
			small = radius;
		if (size.width() == 800 && large)
			expect(radius > small, name + ": the gizmo grows with the text size");
	}
	viewport->setMinimumSize(0, 0);
	viewport->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
	// The active pane of four views is smaller still.
	editor->setViewLayout(ModelViewLayout::FourViews);
	app.processEvents();
	expect(until([&] { return viewport->width() < 900; }), pass + ": four views give the active pane a quarter");
	tests::settleModelViewport(*viewport);
	const auto gizmo = editor->tools()->navigationGizmo();
	const auto hud = viewport->hudLayout();
	expect(gizmo.bounds.isEmpty() || (QRectF(viewport->rect()).contains(gizmo.bounds) && clear(gizmo.bounds, hud.leading.bounds) &&
									  clear(gizmo.bounds, hud.trailing.bounds) && gizmo.bubbleRadius >= 12),
		   pass + " four views: the active pane's gizmo fits clear of its heads-up tags or is hidden");
	capture(viewport, QString::fromStdString(pass + " four").replace(QLatin1Char(' '), QLatin1Char('-')).remove(QLatin1Char('%')));
	// Closing could ask whether to save, which waits forever offscreen.
	delete editor;
	app.setLayoutDirection(Qt::LeftToRight);
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = test_support::exitCodeWithoutRenderer("model-navigation-gizmo-ui-smoke"); skip >= 0)
		return skip;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("mesh-gizmo-ui-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	for (int variant = 0; variant < 4; ++variant)
		run(app, variant, QDir(temporary.path()).filePath(QStringLiteral("settings-%1.ini").arg(variant)));
	if (failures)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "Model navigation gizmo UI smoke passed.\n";
	return EXIT_SUCCESS;
}
