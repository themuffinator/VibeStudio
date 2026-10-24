#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_rip_test_helpers.h"
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
bool expect(bool ok, const char *message, const QString &error = {})
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << error.toStdString() << '\n';
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
	return image.save(QDir(root).filePath(QString("rip-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
QPointF sidePoint(ModelViewport &view, const ModelMesh &mesh, int face)
{
	const auto &s = mesh.surfaces[0];
	const auto t = s.triangles[face];
	const auto &p = s.frames[0].positions;
	QPointF point;
	view.projectToView({(p[t.a].x + p[t.b].x + p[t.c].x) / 3, (p[t.a].y + p[t.b].y + p[t.c].y) / 3, (p[t.a].z + p[t.b].z + p[t.c].z) / 3},
					   &point);
	return point;
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
	QTemporaryDir temp(QDir(root).filePath("rip-ui-XXXXXX"));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("rip UI"); unavailable >= 0)
		return unavailable;
	const auto source = ripGrid();
	const auto bytes = ripBytes(source);
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
		expect(editor.setMesh(source, &error), "open two-pose rip grid", error);
		auto *tools = editor.tools();
		auto *view = editor.findChild<ModelViewport *>("meshPreview");
		if (!expect(tools && view, "editor exposes semantic controls"))
			return 1;
		for (const auto &profile : modelEditorProfiles())
		{
			tools->applyControls(profile.controls);
			for (const auto &id : {"meshRip", "meshRipFill"})
				expect(tools->action(id) && tools->searchableActions().contains(tools->action(id)) &&
						   modelEditorControlProblems(profile.controls).isEmpty(),
					   "rip commands remain searchable in every conflict-free profile");
		}
		tools->applyControls(modelEditorControlsForProfile("studio"));
		auto edit = ripEdit(source);
		tools->applySelection(edit.selection);
		tools->alignView(2, true);
		view->frameModel();
		expect(settleModelViewport(*view), "top view ready");
		auto start = sidePoint(*view, source, edit.tool.ripFace);
		expect(tools->beginModal(ModelModalOperator::Rip, start), "start vertex rip");
		tools->constrainModal(0, false);
		bool delivered = false, reentrant = false;
		QTimer::singleShot(0, &editor,
						   [&]
						   {
							   delivered = true;
							   reentrant = !tools->finishModal(true);
							   tools->setRipOptions(true, true);
							   tools->typeModal("9");
						   });
		tools->typeModal("4");
		expect(delivered && reentrant, "worker delivers events while guarding reentrant changes");
		auto expected = source;
		expect(applyModelEdit(&expected, edit, nullptr, &error), "prepare shared service result", error);
		expect(tools->modalPreviewValid() && tools->modalPreview() && ripBytes(*tools->modalPreview()) == ripBytes(expected),
			   "preview matches shared service", tools->modalText());
		expect(ripBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "preview leaves document and undo untouched");
		auto *banner = editor.findChild<QLabel *>("meshOperatorBanner");
		expect(banner && banner->height() < view->height() / 3 && banner->accessibleDescription().contains("B") &&
				   view->toolTip().contains("Esc"),
			   "compact status exposes full keyboard guidance");
		view->frameModel();
		expect(settleModelViewport(*view) && render(editor, QString("open-%1").arg(scenario)), "render open rip preview");
		tools->setRipOptions(false, true);
		edit.tool.ripFill = true;
		expected = source;
		expect(applyModelEdit(&expected, edit, nullptr, &error) && tools->modalPreviewValid() &&
				   ripBytes(*tools->modalPreview()) == ripBytes(expected),
			   "fill toggle matches shared service", tools->modalText());
		tools->setRipOptions(true, true);
		edit.tool.ripOpposite = true;
		expected = source;
		expect(applyModelEdit(&expected, edit, nullptr, &error) && tools->modalPreviewValid() &&
				   ripBytes(*tools->modalPreview()) == ripBytes(expected),
			   "opposite side toggle matches shared service", tools->modalText());
		tools->finishModal(false);
		expect(ripBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "cancel discards all rip topology");
		view->frameModel();
		settleModelViewport(*view);
		start = sidePoint(*view, source, edit.tool.ripFace);
		expect(tools->beginModal(ModelModalOperator::RipFill, start), "start filled rip");
		tools->constrainModal(0, false);
		tools->typeModal("4");
		edit.tool.ripOpposite = false;
		expected = source;
		applyModelEdit(&expected, edit, nullptr, &error);
		expect(tools->finishModal(true, &error) && ripBytes(editor.document().mesh()) == ripBytes(expected),
			   "commit filled rip matches preview", error);
		expect(editor.document().selection().vertices.size() == 1 &&
				   editor.findChild<QComboBox *>("meshSelectionMode")->currentIndex() == 1,
			   "ripped vertex remains selected in vertex mode");
		tools->action("meshAdjustLast")->trigger();
		app.processEvents();
		auto *panel = tools->lastOperationPanel();
		auto *scroll = panel ? panel->findChild<QScrollArea *>("meshLastOperationScroll") : nullptr;
		if (!expect(panel && panel->isVisible() && scroll && view->rect().contains(panel->geometry()) &&
						panel->height() < view->height() * .7,
					"adjustment stays inside viewport"))
			return 1;
		auto fields = panel->findChildren<QDoubleSpinBox *>();
		auto toggles = panel->findChildren<QCheckBox *>();
		if (!expect(fields.size() == 6 && toggles.size() == 2, "offset, side face, fill, side, UV scale and pairing are adjustable"))
			return 1;
		for (auto *field : fields)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(field);
			expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && (field->focusPolicy() & Qt::TabFocus) &&
					   field->layoutDirection() == Qt::LeftToRight,
				   "numeric controls have accessible names, focus and stable numeric direction");
			field->setFocus(Qt::OtherFocusReason);
			app.processEvents();
			const QRect control(field->mapTo(scroll->widget(), QPoint()), field->size());
			scroll->ensureVisible(control.center().x(), control.center().y(), (control.width() + 1) / 2, control.height() / 2 + 2);
			app.processEvents();
			expect(scroll->viewport()->rect().contains(QRect(field->mapTo(scroll->viewport(), QPoint()), field->size())),
				   "each field can be fully revealed");
		}
		expect(settleModelViewport(*view) && render(editor, QString("adjust-%1").arg(scenario)), "render scaled translated controls");
		fields[0]->setValue(6);
		app.processEvents();
		edit.translation.x = 6;
		expected = source;
		expect(applyModelEdit(&expected, edit, nullptr, &error) && ripBytes(editor.document().mesh()) == ripBytes(expected),
			   "F9 replays movement against original mesh");
		const auto accepted = ripBytes(editor.document().mesh());
		fields = panel->findChildren<QDoubleSpinBox *>();
		fields[0]->setValue(40);
		app.processEvents();
		fields = panel->findChildren<QDoubleSpinBox *>();
		expect(ripBytes(editor.document().mesh()) == accepted && fields[0]->value() == 6, "invalid F9 restores last accepted value");
		editor.findChild<QAction *>("undoMesh")->trigger();
		app.processEvents();
		expect(ripBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "adjusted rip is one undo step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		app.processEvents();
		expect(ripBytes(editor.document().mesh()) == accepted, "redo restores adjusted rip");
		expect(editor.setMesh(source, &error), "reload edge fixture");
		edit = ripEdit(source, true, true);
		tools->applySelection(edit.selection);
		view->frameModel();
		settleModelViewport(*view);
		start = sidePoint(*view, source, edit.tool.ripFace);
		expect(tools->beginModal(ModelModalOperator::RipFill, start), "start edge rip");
		tools->constrainModal(0, false);
		tools->typeModal("4");
		view->frameModel();
		expect(settleModelViewport(*view) && render(editor, QString("edge-%1").arg(scenario)), "render filled edge rip");
		expect(tools->finishModal(true, &error) && editor.document().selection().edges.size() == 1 &&
				   editor.findChild<QComboBox *>("meshSelectionMode")->currentIndex() == 2,
			   "edge rip keeps moved edge selected", error);
		expect(editor.setMesh(source, &error), "reload invalid preview fixture");
		tools->applySelection(ripEdit(source).selection);
		view->frameModel();
		settleModelViewport(*view);
		start = sidePoint(*view, source, edit.tool.ripFace);
		expect(tools->beginModal(ModelModalOperator::Rip, start), "start invalid rip");
		tools->constrainModal(0, false);
		tools->typeModal("4");
		tools->typeModal("0");
		expect(!tools->modalPreviewValid() && !tools->modalPreview() && !tools->finishModal(true, &error) && !error.isEmpty() &&
				   ripBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(),
			   "invalid preview clears stale geometry and cannot commit", error);
		expect(banner->height() < view->height() / 3 && banner->accessibleDescription() == error, "refusal exposes accessible diagnostic");
		expect(editor.setMesh(source, &error), "reload zero-distance split fixture", error);
		tools->applySelection(ripEdit(source).selection);
		view->frameModel(); settleModelViewport(*view);
		start = sidePoint(*view, source, ripEdit(source).tool.ripFace);
		auto zeroEdit = ripEdit(source); zeroEdit.translation = {};
		auto zeroMesh = source;
		expect(applyModelEdit(&zeroMesh, zeroEdit, nullptr, &error), "shared service zero-distance split", error);
		expect(tools->beginModal(ModelModalOperator::Rip, start) && tools->modalPreviewValid() && tools->modalPreview() &&
			ripBytes(*tools->modalPreview()) == ripBytes(zeroMesh), "zero-distance rip previews immediately", tools->modalText());
		tools->setRipOptions(false, true);
		expect(!tools->modalPreviewValid() && !tools->modalPreview(), "zero-width fill clears open-split preview");
		tools->setRipOptions(false, false);
		expect(tools->modalPreviewValid(), "return to an open zero-distance split");
		tools->finishModal(false);
		expect(ripBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "cancelling zero-distance split restores connectivity");
		expect(tools->beginModal(ModelModalOperator::Rip, start) && tools->finishModal(true, &error) &&
			ripBytes(editor.document().mesh()) == ripBytes(zeroMesh), "commit zero-distance topology without movement", error);
		tools->action("meshAdjustLast")->trigger(); app.processEvents();
		panel = tools->lastOperationPanel(); fields = panel->findChildren<QDoubleSpinBox *>();
		fields[0]->setValue(2); app.processEvents();
		fields = panel->findChildren<QDoubleSpinBox *>(); fields[0]->setValue(0); app.processEvents();
		expect(ripBytes(editor.document().mesh()) == ripBytes(zeroMesh), "F9 can return a split to coincident positions");
		expect(settleModelViewport(*view) && render(editor, QString("zero-%1").arg(scenario)), "render zero-distance source state");
		editor.findChild<QAction *>("undoMesh")->trigger(); app.processEvents();
		expect(ripBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "zero-distance F9 remains one undo step");
		editor.setMesh(source, &error);
		editor.close();
		app.processEvents();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << checks << " rip UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
