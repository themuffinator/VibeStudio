#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_spin_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTimer>
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
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
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
	return image.save(QDir(root).filePath(QString("spin-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
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
	QTemporaryDir temp(QDir(root).filePath("spin-ui-XXXXXX"));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("spin UI"); unavailable >= 0)
		return unavailable;
	const auto source = spinProfile();
	const auto bytes = spinBytes(source);
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
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.resize(1500, 1000);
		editor.show();
		app.processEvents();
		expect(editor.setMesh(source, &error), "open two-pose spin profile");
		auto *tools = editor.tools();
		auto *view = editor.findChild<ModelViewport *>("meshPreview");
		if (!expect(tools && view, "editor exposes tools and viewport"))
			return 1;
		for (const auto &profile : modelEditorProfiles())
		{
			tools->applyControls(profile.controls);
			for (const auto &key : {"meshSpin", "meshSpinDuplicate"})
				expect(tools->action(key) && tools->searchableActions().contains(tools->action(key)) &&
						   modelEditorControlProblems(profile.controls).isEmpty(),
					   "spin commands remain searchable in every conflict-free profile");
		}
		tools->applyControls(modelEditorControlsForProfile("studio"));
		tools->applySelection(spinEdit().selection);
		tools->setCursor3d({0, 0, 0});
		view->frameModel();
		expect(settleModelViewport(*view), "selected profile view settles");
		const QPointF start = view->rect().center() + QPointF(60, 0);
		expect(tools->beginModal(ModelModalOperator::Spin, start), "start spin modal");
		tools->constrainModal(2, false);
		bool eventDelivered = false, reentrantRejected = false;
		QTimer::singleShot(0, &editor,
						   [&]
						   {
							   eventDelivered = true;
							   reentrantRejected = !tools->finishModal(true);
							   tools->typeModal("9");
							   tools->adjustModal(10);
						   });
		type(*tools, "90");
		expect(eventDelivered && reentrantRejected, "worker keeps UI responsive and rejects reentrant edits");
		auto expected = source;
		expect(applyModelEdit(&expected, spinEdit(), nullptr, &error), "prepare shared-service result");
		if (!tools->modalPreviewValid())
			std::cerr << tools->modalText().toStdString() << '\n';
		expect(tools->modalPreviewValid() && tools->modalPreview() && spinBytes(*tools->modalPreview()) == spinBytes(expected),
			   "typed angle and world axis preview the shared-service result");
		expect(spinBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "preview leaves source and history untouched");
		auto *banner = editor.findChild<QLabel *>("meshOperatorBanner");
		expect(banner && banner->height() < view->height() / 3 && banner->accessibleDescription().contains("Page") &&
				   view->toolTip().contains("Esc"),
			   "compact spin status exposes full keyboard guidance");
		view->frameModel();
		expect(settleModelViewport(*view) && render(editor, QString("preview-%1").arg(scenario)), "render curved extrusion preview");
		tools->finishModal(false);
		expect(spinBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "cancel discards spin preview");
		expect(tools->beginModal(ModelModalOperator::Spin, start), "restart spin");
		tools->constrainModal(2, false);
		type(*tools, "90");
		expect(tools->finishModal(true, &error) && spinBytes(editor.document().mesh()) == spinBytes(expected),
			   "commit exactly matches preview");
		expect(editor.document().selection().faces.size() == 2 && editor.document().selection().edges.isEmpty() &&
				   editor.findChild<QComboBox *>("meshSelectionMode")->currentIndex() == 0,
			   "face spin keeps its cap selected in face mode after confirmation");
		tools->action("meshAdjustLast")->trigger();
		app.processEvents();
		auto *panel = tools->lastOperationPanel();
		auto *scroll = panel ? panel->findChild<QScrollArea *>("meshLastOperationScroll") : nullptr;
		if (!expect(panel && panel->isVisible() && scroll && view->rect().contains(panel->geometry()) &&
						panel->height() < view->height() * .7,
					"eleven-field adjustment remains inside the viewport"))
			return 1;
		auto fields = panel->findChildren<QDoubleSpinBox *>();
		auto toggles = panel->findChildren<QCheckBox *>();
		if (!expect(fields.size() == 9 && toggles.size() == 2, "angle, steps, UV scale, centre, axis and two switches are adjustable"))
			return 1;
		for (auto *field : fields)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(field);
			expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && (field->focusPolicy() & Qt::TabFocus) &&
					   field->layoutDirection() == Qt::LeftToRight,
				   "numeric controls have accessible names, keyboard focus and stable numeric direction");
			field->setFocus(Qt::OtherFocusReason);
			app.processEvents();
			const QRect control(field->mapTo(scroll->widget(), QPoint()), field->size());
			scroll->ensureVisible(control.center().x(), control.center().y(), (control.width() + 1) / 2, control.height() / 2 + 2);
			app.processEvents();
			expect(scroll->viewport()->rect().contains(QRect(field->mapTo(scroll->viewport(), QPoint()), field->size())),
				   "each field can be fully revealed");
		}
		expect(settleModelViewport(*view) && render(editor, QString("adjust-%1").arg(scenario)),
			   "render scaled translated adjustment panel");
		fields[0]->setValue(120);
		app.processEvents();
		expected = source;
		expect(applyModelEdit(&expected, spinEdit(120), nullptr, &error) && spinBytes(editor.document().mesh()) == spinBytes(expected),
			   "angle adjustment replays once against the original profile");
		const auto accepted = spinBytes(editor.document().mesh());
		fields = panel->findChildren<QDoubleSpinBox *>();
		fields[8]->setValue(0);
		app.processEvents();
		fields = panel->findChildren<QDoubleSpinBox *>();
		expect(spinBytes(editor.document().mesh()) == accepted && fields[8]->value() == 1,
			   "zero axis refuses and restores accepted adjustment");
		editor.findChild<QAction *>("undoMesh")->trigger();
		app.processEvents();
		expect(spinBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "adjusted spin remains one undo step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		app.processEvents();
		expect(spinBytes(editor.document().mesh()) == accepted, "redo restores adjusted spin");
		expect(editor.setMesh(source, &error), "reload for radial copies");
		tools->applySelection(spinEdit().selection);
		expect(tools->beginModal(ModelModalOperator::SpinDuplicate, start), "start radial copies");
		tools->constrainModal(2, false);
		tools->adjustModal(-4);
		type(*tools, "360");
		expect(tools->modalPreviewValid() && tools->modalPreview()->surfaces[0].triangles.size() == 8,
			   "four steps make four non-overlapping radial copies");
		view->frameModel();
		expect(settleModelViewport(*view) && render(editor, QString("copies-%1").arg(scenario)), "render radial copies");
		tools->finishModal(false);
		ModelSelection border;
		const auto &surface = source.surfaces[0];
		for (const auto &t : surface.triangles)
			for (const auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
				if (surface.frames[0].positions[edge.first].x == 24 && surface.frames[0].positions[edge.second].x == 24)
					border.edges.insert(edge);
		tools->applySelection(border);
		expect(tools->beginModal(ModelModalOperator::Spin, start), "start open-border spin");
		tools->constrainModal(2, false);
		type(*tools, "90");
		expect(tools->finishModal(true, &error) && editor.document().selection().edges.size() == 1 &&
				   editor.document().selection().faces.isEmpty() && editor.findChild<QComboBox *>("meshSelectionMode")->currentIndex() == 2,
			   "border spin keeps its leading edge selected in edge mode");
		expect(editor.setMesh(source, &error), "reload for refusal preview");
		tools->applySelection(spinEdit().selection);
		expect(tools->beginModal(ModelModalOperator::Spin, start), "start invalid spin");
		tools->constrainModal(2, false);
		type(*tools, "361");
		expect(!tools->modalPreviewValid() && !tools->finishModal(true, &error) && !error.isEmpty() &&
				   spinBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(),
			   "invalid preview cannot commit or add history");
		expect(banner->height() < view->height() / 3 && banner->accessibleDescription() == error,
			   "refusal remains compact with an accessible diagnostic");
		expect(editor.setMesh(source, &error), "restore clean fixture before closing");
		editor.close();
		app.processEvents();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << checks << " spin UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
