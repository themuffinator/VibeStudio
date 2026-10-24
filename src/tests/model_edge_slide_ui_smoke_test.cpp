#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_edge_slide_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *message)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << message << '\n';
	}
	return ok;
}
bool near(double a, double b)
{
	return std::abs(a - b) < 1e-4;
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
		if (QByteArray(context) != "VibeStudioModelTools")
			return {};
		const auto s = QString::fromUtf8(text);
		return QString("[%1 %2]").arg(s, QString("~~ ").repeated(s.size() / 6));
	}
};
bool render(QWidget &widget, const QString &name)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (root.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(root).filePath(QString("edge-slide-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
void type(ModelEditorTools &tools, const QString &number)
{
	for (QChar c : number)
		tools.typeModal(QString(c));
}
} // namespace
int main(int argc, char **argv)
{
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
	QTemporaryDir temp(QDir(root).filePath("edge-slide-ui-XXXXXX"));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("edge slide UI"); unavailable >= 0)
		return unavailable;
	const auto source = edgeSlideGrid();
	const auto bytes = slideBytes(source);
	QString error;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		app.setLayoutDirection(scenario == 2 ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		ModelEditorDialog editor;
		editor.resize(1500, 1000);
		editor.show();
		app.processEvents();
		expect(editor.setMesh(source, &error), "editor opens slide fixture");
		auto *tools = editor.tools();
		auto *view = editor.findChild<ModelViewport *>("meshPreview");
		if (!expect(tools && view, "editor has tools and viewport"))
			return 1;
		tools->alignView(2, true);
		view->frameModel();
		expect(settleModelViewport(*view), "source viewport settles");
		auto *action = tools->action("meshEdgeSlide");
		expect(action && !action->text().isEmpty() && tools->searchableActions().contains(action),
			   "slide is available through menu/search and profile command");
		tools->applySelection(edgeSlideEdit().selection);
		QPointF start, target;
		expect(view->projectToView(source.surfaces[0].frames[0].positions[2], &start) &&
				   view->projectToView(source.surfaces[0].frames[0].positions[1], &target),
			   "project a selected vertex and its positive rail");
		expect(tools->beginModal(ModelModalOperator::EdgeSlide, start), "edge slide starts from selection");
		tools->moveModal(start + (target - start) * .25);
		app.processEvents();
		expect(tools->modalPreviewValid() && tools->modalPreview() && near(tools->modalPreview()->surfaces[0].frames[0].positions[2].x, 14),
			   "pointer follows the projected rail at quarter fraction");
		expect(slideBytes(editor.document().mesh()) == bytes, "live preview does not mutate document");
		auto *banner = editor.findChild<QLabel *>("meshOperatorBanner");
		expect(banner && banner->height() < view->height() / 3 && banner->text().contains("0.2500") &&
				   banner->accessibleDescription().contains("Ctrl") && view->toolTip().contains("Esc"),
			   "compact preview status keeps geometry visible and exposes detailed guidance accessibly");
		expect(settleModelViewport(*view) && render(editor, QString("pointer-%1").arg(scenario)), "render rail-guided pointer preview");
		tools->finishModal(false);
		expect(tools->modalOperator() == ModelModalOperator::None && slideBytes(editor.document().mesh()) == bytes &&
				   !editor.document().canUndo(),
			   "cancel leaves no history");
		expect(tools->beginModal(ModelModalOperator::EdgeSlide, start), "start numeric slide");
		type(*tools, "-.25");
		tools->constrainModal(2, false);
		expect(tools->modalPreviewValid() && near(tools->modalPreview()->surfaces[0].frames[0].positions[2].x, 18),
			   "typed signed fraction chooses other rail and ignores axis constraints");
		expect(settleModelViewport(*view) && render(editor, QString("typed-%1").arg(scenario)), "render typed negative preview and status");
		expect(tools->finishModal(true, &error) && near(editor.document().mesh().surfaces[0].frames[0].positions[2].x, 18),
			   "confirm commits preview through document");
		const auto committed = slideBytes(editor.document().mesh());
		tools->action("meshAdjustLast")->trigger();
		app.processEvents();
		auto *panel = editor.findChild<QFrame *>("meshLastOperation");
		if (!panel)
			panel = editor.findChild<QFrame *>("meshLastPanel");
		if (!expect(panel && panel->isVisible(), "adjust-last panel is visible"))
			return 1;
		expect(view->rect().contains(panel->geometry()), "adjust-last panel stays inside the viewport");
		expect(panel->height() < view->height() * .7, "adjustment panel leaves part of the viewport visible");
		auto *scroll = panel->findChild<QScrollArea *>("meshLastOperationScroll");
		if (!expect(scroll, "adjustment fields have a scrolling container"))
			return 1;
		for (auto *widget : panel->findChildren<QWidget *>())
			if (qobject_cast<QComboBox *>(widget) || qobject_cast<QDoubleSpinBox *>(widget))
			{
				auto *accessible = QAccessible::queryAccessibleInterface(widget);
				expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && (widget->focusPolicy() & Qt::TabFocus),
					   "numeric slide fields have accessible names and keyboard focus");
				scroll->ensureWidgetVisible(widget);
				app.processEvents();
				const QRect mapped(widget->mapTo(scroll->viewport(), QPoint()), widget->size());
				expect(widget->isVisible() && scroll->viewport()->rect().intersects(mapped) && scroll->height() > widget->height(),
					   "each adjustment control is reachable within the visible scroll area");
			}
		expect(settleModelViewport(*view) && render(editor, QString("adjust-%1").arg(scenario)),
			   "render fraction mode in adjust-last panel");
		auto *mode = panel->findChild<QComboBox *>();
		expect(mode && mode->count() == 2, "fraction and reference-distance modes are exposed");
		mode->setCurrentIndex(1);
		app.processEvents();
		auto fields = panel->findChildren<QDoubleSpinBox *>();
		if (!expect(fields.size() == 2, "distance and pairing-angle fields exist"))
			return 1;
		fields[0]->setValue(-2);
		app.processEvents();
		expect(slideBytes(editor.document().mesh()) == committed, "distance mode replays exactly against original geometry");
		fields[0]->setFocus(Qt::OtherFocusReason);
		scroll->ensureWidgetVisible(fields[0]);
		app.processEvents();
		expect(scroll->viewport()->rect().contains(QRect(fields[0]->mapTo(scroll->viewport(), QPoint()), fields[0]->size())),
			   "distance control can be fully revealed after changing modes");
		expect(settleModelViewport(*view) && render(editor, QString("distance-%1").arg(scenario)), "render fixed reference-distance mode");
		fields = panel->findChildren<QDoubleSpinBox *>();
		fields[0]->setValue(-8);
		app.processEvents();
		fields = panel->findChildren<QDoubleSpinBox *>();
		expect(slideBytes(editor.document().mesh()) == committed && fields.size() == 2 && near(fields[0]->value(), -2),
			   "invalid adjustment preserves the accepted mesh and restores its displayed value");
		editor.findChild<QAction *>("undoMesh")->trigger();
		app.processEvents();
		expect(slideBytes(editor.document().mesh()) == bytes, "adjustments remain one undo step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		app.processEvents();
		expect(slideBytes(editor.document().mesh()) == committed, "redo restores accepted slide");
		// A no-op tool must not replace the real last operation's history anchor.
		auto noop = edgeSlideEdit();
		noop.tool.slide = 0;
		expect(tools->runTool(noop, &error), "a no-op tool succeeds without taking over adjust-last");
		app.processEvents();
		fields = panel->findChildren<QDoubleSpinBox *>();
		fields[0]->setValue(-1);
		app.processEvents();
		expect(near(editor.document().mesh().surfaces[0].frames[0].positions[2].x, 17),
			   "adjust-last still replays the preceding real operation");
		// A separate operation first creates history that a zero replay must retain.
		auto next = edgeSlideEdit();
		next.tool.slide = .25;
		const auto previous = slideBytes(editor.document().mesh());
		expect(tools->runTool(next, &error), "create another undoable operation");
		app.processEvents();
		fields = panel->findChildren<QDoubleSpinBox *>();
		fields[0]->setValue(0);
		app.processEvents();
		expect(slideBytes(editor.document().mesh()) == previous && panel->findChildren<QDoubleSpinBox *>().isEmpty(),
			   "zero replay removes its own entry and clears adjust-last controls");
		editor.findChild<QAction *>("undoMesh")->trigger();
		app.processEvents();
		expect(slideBytes(editor.document().mesh()) == bytes, "zero replay preserves earlier undo history");
		expect(editor.setMesh(source, &error), "reset fixture for invalid slide");
		tools->applySelection(edgeSlideEdit().selection);
		expect(tools->beginModal(ModelModalOperator::EdgeSlide, start), "start refusal preview");
		type(*tools, "1");
		expect(!tools->modalPreviewValid() && !tools->finishModal(true, &error) && slideBytes(editor.document().mesh()) == bytes,
			   "collapsed endpoint cannot commit");
		expect(settleModelViewport(*view) && render(editor, QString("refusal-%1").arg(scenario)), "render invalid-slide feedback");
		tools->finishModal(false);
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
