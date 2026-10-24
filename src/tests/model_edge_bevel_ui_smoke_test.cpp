#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_edge_bevel_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFrame>
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
	return image.save(QDir(root).filePath(QString("edge-bevel-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
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
	QTemporaryDir temp(QDir(root).filePath("edge-bevel-ui-XXXXXX"));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("edge bevel UI"); unavailable >= 0)
		return unavailable;
	const auto source = bevelCube();
	const auto bytes = bevelBytes(source);
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
		editor.setAttribute(Qt::WA_DeleteOnClose, false); // This fixture owns the dialog on the stack.
		editor.resize(1500, 1000);
		editor.show();
		app.processEvents();
		expect(editor.setMesh(source, &error), "editor opens bevel fixture");
		auto *tools = editor.tools();
		auto *view = editor.findChild<ModelViewport *>("meshPreview");
		if (!expect(tools && view, "editor has bevel tools and viewport"))
			return 1;
		view->frameModel();
		expect(settleModelViewport(*view), "source viewport settles");
		auto *action = tools->action("meshBevelEdges");
		expect(action && !action->text().isEmpty() && tools->searchableActions().contains(action),
			   "bevel available through menu and command search");
		for (const auto &profile : modelEditorProfiles())
		{
			tools->applyControls(profile.controls);
			expect(modelEditorControlProblems(profile.controls).isEmpty() && tools->action("meshBevelEdges"),
				   "every profile keeps a conflict-free named bevel command");
		}
		tools->applyControls(modelEditorControlsForProfile("studio"));
		expect(modelEditorCommandKeys(tools->controls(), "meshBevelEdges") == QStringList{"Ctrl+B"}, "studio shortcut selects edge bevel");
		// Converting face selection omits inferred quad diagonals; explicit edge
		// selections remain exact and the core protects their triangle sides.
		ModelSelection faces;
		for (int face = 0; face < source.surfaces[0].triangles.size(); ++face)
			faces.faces.insert(face);
		tools->applySelection(faces);
		ModelGeometricTopology topology;
		buildModelGeometricTopology(source.surfaces[0], &topology);
		QSet<ModelEdge> geometricEdges;
		for (auto edge : tools->toolEdit(ModelEditKind::BevelEdges).selection.edges)
			geometricEdges.insert(topology.geometricEdge(edge.first, edge.second));
		expect(geometricEdges.size() == 12, "face selection converts to cube sides without internal diagonals");
		tools->applySelection(bevelCubeEdit(source, 12).selection);
		app.processEvents();
		view->frameModel();
		expect(settleModelViewport(*view), "profile layout and selected view settle");
		editor.findChild<QCheckBox *>("meshSnapTranslation")->setChecked(false);
		ModelVec3 right, up;
		view->viewAxes(&right, &up);
		QPointF centre, offset;
		expect(view->projectToView({0, 0, 0}, &centre) && view->projectToView({right.x * 8, right.y * 8, right.z * 8}, &offset),
			   "project an eight-unit pointer width");
		offset -= centre;
		const QPointF start = centre + offset * 2;
		expect(tools->beginModal(ModelModalOperator::BevelEdges, start), "start pointer bevel");
		tools->moveModal(start + offset);
		app.processEvents(); // Pointer previews are coalesced on the event queue.
		if (!tools->modalPreviewValid())
			std::cerr << tools->modalText().toStdString() << '\n';
		expect(tools->modalPreviewValid() && tools->modalPreview() && closedBevel(*tools->modalPreview()), "pointer previews valid bevel");
		expect(bevelBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(),
			   "preview leaves document and history unchanged");
		expect(settleModelViewport(*view) && render(editor, QString("pointer-%1").arg(scenario)), "render pointer bevel");
		tools->finishModal(false);
		expect(bevelBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "cancel discards preview");
		expect(tools->beginModal(ModelModalOperator::BevelEdges, start), "start typed bevel");
		bool eventDelivered = false, reentrantRejected = false;
		QTimer::singleShot(0, &editor,
						   [&]
						   {
							   eventDelivered = true;
							   reentrantRejected = !tools->finishModal(true);
							   tools->typeModal("9");
							   tools->adjustModal(10);
						   });
		type(*tools, "8");
		expect(eventDelivered && reentrantRejected, "preview worker delivers UI events and rejects reentrant confirmation");
		tools->adjustModal(3);
		tools->constrainModal(2, false);
		auto expected = source;
		expect(applyModelEdit(&expected, bevelCubeEdit(source, 12, 4), nullptr, &error), "prepare shared-service expected result");
		expect(tools->modalPreviewValid() && tools->modalPreview() && bevelBytes(*tools->modalPreview()) == bevelBytes(expected),
			   "typed width and wheel segments match shared service exactly");
		auto *banner = editor.findChild<QLabel *>("meshOperatorBanner");
		expect(banner && banner->height() < view->height() / 3 && banner->accessibleDescription().contains("Page") &&
				   view->toolTip().contains("Esc"),
			   "compact status exposes detailed guidance accessibly");
		expect(settleModelViewport(*view) && render(editor, QString("typed-%1").arg(scenario)),
			   "render rounded preview and compact status");
		expect(tools->finishModal(true, &error) && bevelBytes(editor.document().mesh()) == bevelBytes(expected),
			   "commit matches accepted preview");
		tools->action("meshAdjustLast")->trigger();
		app.processEvents();
		auto *panel = editor.findChild<QFrame *>("meshLastOperation");
		if (!panel)
			panel = editor.findChild<QFrame *>("meshLastPanel");
		if (!expect(panel && panel->isVisible(), "adjust-last panel is visible"))
			return 1;
		auto *scroll = panel->findChild<QScrollArea *>("meshLastOperationScroll");
		if (!expect(scroll && panel->height() < view->height() * .7 && view->rect().contains(panel->geometry()),
					"five fields fit a contained scrolling panel"))
			return 1;
		auto fields = panel->findChildren<QDoubleSpinBox *>();
		if (!expect(fields.size() == 5, "width, segments, roundness, tile size and pairing angle are adjustable"))
			return 1;
		for (auto *field : fields)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(field);
			expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && (field->focusPolicy() & Qt::TabFocus) &&
					   field->layoutDirection() == Qt::LeftToRight,
				   "numeric fields have accessible names, keyboard focus and stable numeric direction");
			field->setFocus(Qt::OtherFocusReason);
			app.processEvents();
			// A focused spin box reports its input-method cursor. Scroll its full border.
			const QRect control(field->mapTo(scroll->widget(), QPoint()), field->size());
			scroll->ensureVisible(control.center().x(), control.center().y(), (control.width() + 1) / 2, control.height() / 2 + 2);
			app.processEvents();
			const QRect mapped(field->mapTo(scroll->viewport(), QPoint()), field->size());
			if (!expect(scroll->viewport()->rect().contains(mapped), "every field can be fully revealed"))
				std::cerr << field->accessibleName().toStdString() << ": " << mapped.x() << ',' << mapped.y() << ' ' << mapped.width()
						  << 'x' << mapped.height() << " within " << scroll->viewport()->width() << 'x' << scroll->viewport()->height()
						  << '\n';
		}
		expect(settleModelViewport(*view) && render(editor, QString("adjust-%1").arg(scenario)), "render adjustment controls");
		fields[2]->setValue(0);
		app.processEvents();
		auto chamfer = bevelCubeEdit(source, 12, 4);
		chamfer.tool.bevelRoundness = 0;
		expected = source;
		expect(applyModelEdit(&expected, chamfer, nullptr, &error) && bevelBytes(editor.document().mesh()) == bevelBytes(expected),
			   "roundness replays against source without adding history");
		const auto accepted = bevelBytes(editor.document().mesh());
		fields = panel->findChildren<QDoubleSpinBox *>();
		fields[0]->setValue(64);
		app.processEvents();
		fields = panel->findChildren<QDoubleSpinBox *>();
		expect(bevelBytes(editor.document().mesh()) == accepted && fields[0]->value() == 8,
			   "invalid width preserves accepted geometry and restores control value");
		editor.findChild<QAction *>("undoMesh")->trigger();
		app.processEvents();
		expect(bevelBytes(editor.document().mesh()) == bytes, "adjusted bevel remains one undo step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		app.processEvents();
		expect(bevelBytes(editor.document().mesh()) == accepted, "redo restores adjusted result");
		expect(editor.setMesh(source, &error), "reload for invalid preview");
		tools->applySelection(bevelCubeEdit(source, 12).selection);
		expect(tools->beginModal(ModelModalOperator::BevelEdges, start), "start refusal preview");
		expect(!panel->isVisible(), "fresh modal preview hides the previous adjustment overlay");
		type(*tools, "64");
		expect(!tools->modalPreviewValid() && !tools->finishModal(true, &error) && !error.isEmpty() &&
				   bevelBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(),
			   "invalid preview cannot commit or create history");
		expect(settleModelViewport(*view) && render(editor, QString("refusal-%1").arg(scenario)), "render refusal with original geometry");
		// Avoid unsaved-close prompts; all changes above are synthetic fixtures.
		expect(banner->height() < view->height() / 3 && banner->accessibleDescription() == error && view->toolTip() == error,
			   "refusal stays compact while full diagnostic remains accessible");
		expect(editor.setMesh(source, &error), "restore clean fixture before closing");
		editor.close();
		app.processEvents();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << checks << " edge bevel UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
