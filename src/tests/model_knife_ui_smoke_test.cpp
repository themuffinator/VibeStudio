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
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>
#include <numbers>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &error = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (!QByteArray(context).startsWith("VibeStudioModel"))
			return {};
		const auto original = QString::fromUtf8(text);
		return QString("[%1 %2]").arg(original, QString("~~ ").repeated(original.size() / 6));
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
	return image.save(QDir(root).filePath(QString("knife-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
bool displayedHidden(const ModelMesh &display, const QVector<int> &sources, const QSet<int> &hidden)
{
	const auto &triangles = display.surfaces[0].triangles;
	if (triangles.size() != sources.size())
	{
		std::cerr << "display triangles " << triangles.size() << ", reference " << sources.size() << '\n';
		return false;
	}
	for (int f = 0; f < triangles.size(); ++f)
	{
		const auto t = triangles[f];
		if ((t.a == t.b && t.b == t.c) != hidden.contains(sources[f]))
		{
			std::cerr << "display hidden mismatch face " << f << ", source " << sources[f] << '\n';
			return false;
		}
	}
	return true;
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
	QTemporaryDir temp(QDir(root).filePath("knife-ui-XXXXXX"));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("knife UI"); unavailable >= 0)
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
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.resize(1500, 1000);
		editor.show();
		app.processEvents();
		expect(editor.setMesh(source, &error), "load knife cube", error);
		auto *tools = editor.tools();
		auto *view = editor.findChild<ModelViewport *>("meshPreview");
		if (!expect(tools && view, "knife editor tools exist"))
			return 1;
		for (const auto &profile : modelEditorProfiles())
		{
			tools->applyControls(profile.controls);
			expect(modelEditorControlProblems(profile.controls).isEmpty() && tools->action("meshKnife"),
				   "knife command in every conflict-free profile");
		}
		tools->applyControls(modelEditorControlsForProfile("studio"));
		expect(modelEditorCommandKeys(tools->controls(), "meshKnife") == QStringList{"K"}, "studio knife shortcut");
		expect(tools->searchableActions().contains(tools->action("meshKnife")), "knife appears in command search");
		tools->alignView(2, true);
		view->frameModel();
		expect(settleModelViewport(*view), "settle top view");
		QPointF start, finish;
		expect(view->projectToView({-40, 3, 0}, &start) && view->projectToView({40, 3, 0}, &finish), "project knife stroke");
		expect(tools->beginModal(ModelModalOperator::Knife, start), "begin knife without selected components");
		expect(tools->addKnifePoint(start, Qt::ControlModifier), "place first point");
		bool delivered = false, rejected = false;
		QTimer::singleShot(0, &editor,
						   [&]
						   {
							   delivered = true;
							   rejected = !tools->finishModal(true);
							   tools->newKnifePath();
						   });
		tools->addKnifePoint(finish, Qt::ControlModifier);
		expect(delivered && rejected, "worker delivers events and rejects reentrant edits");
		expect(tools->modalPreviewValid() && tools->modalPreview() && closedBevel(*tools->modalPreview()),
			   "knife preview preserves closed geometry", tools->modalText());
		expect(bevelBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "draft leaves source and history unchanged");
		expect(settleModelViewport(*view) && render(editor, QString("visible-%1").arg(scenario)), "render visible-face knife");
		tools->setKnifeThrough(true);
		expect(tools->modalPreviewValid(), "through preview valid", tools->modalText());
		expect(settleModelViewport(*view) && render(editor, QString("through-%1").arg(scenario)), "render cut-through knife");
		const auto accepted = tools->modalPreview() ? bevelBytes(*tools->modalPreview()) : QByteArray{};
		expect(tools->finishModal(true, &error) && bevelBytes(editor.document().mesh()) == accepted, "confirmation matches full preview",
			   error);
		tools->action("meshAdjustLast")->trigger();
		app.processEvents();
		auto *panel = editor.findChild<QFrame *>("meshLastOperation");
		if (!panel)
			panel = editor.findChild<QFrame *>("meshLastPanel");
		if (!expect(panel && panel->isVisible(), "F9 knife settings available"))
			return 1;
		const auto fields = panel->findChildren<QCheckBox *>();
		expect(fields.size() == 2, "F9 exposes visibility and selection scope");
		auto *scroll = panel->findChild<QScrollArea *>("meshLastOperationScroll");
		for (auto *field : fields)
		{
			const auto *accessible = QAccessible::queryAccessibleInterface(field);
			expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && (field->focusPolicy() & Qt::TabFocus),
				   "knife settings labelled and focusable");
			field->setFocus(Qt::OtherFocusReason);
			app.processEvents();
			bool reachable = scroll;
			QString geometry;
			if (scroll)
			{
				QRect content(field->mapTo(scroll->widget(), QPoint()), field->size());
				if (auto *form = scroll->widget()->findChild<QFormLayout *>())
					if (auto *label = form->labelForField(field))
						content |= QRect(label->mapTo(scroll->widget(), QPoint()), label->size());
				scroll->ensureVisible(content.center().x(), content.center().y(), content.width() / 2 + 2, content.height() / 2 + 2);
				app.processEvents();
				const QRect mapped(scroll->widget()->mapTo(scroll->viewport(), content.topLeft()), content.size());
				reachable = scroll->viewport()->rect().contains(mapped);
				geometry = QString("%1: %2,%3 %4x%5 in %6x%7")
							   .arg(field->accessibleName())
							   .arg(mapped.x())
							   .arg(mapped.y())
							   .arg(mapped.width())
							   .arg(mapped.height())
							   .arg(scroll->viewport()->width())
							   .arg(scroll->viewport()->height());
			}
			expect(reachable, "each knife checkbox and label can be fully revealed", geometry);
		}
		expect(settleModelViewport(*view) && render(editor, QString("adjust-%1").arg(scenario)), "render knife adjustment");
		if (fields.size() == 2)
		{
			fields[0]->setChecked(false);
			app.processEvents();
		}
		expect(bevelBytes(editor.document().mesh()) != accepted && editor.document().canUndo(), "F9 reruns visible-face cut");
		editor.findChild<QAction *>("undoMesh")->trigger();
		app.processEvents();
		expect(bevelBytes(editor.document().mesh()) == bytes, "knife and F9 remain one undo step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		app.processEvents();
		expect(closedBevel(editor.document().mesh()), "redo restores valid cut");
		expect(editor.setMesh(source, &error), "reload refusal fixture", error);
		expect(tools->beginModal(ModelModalOperator::Knife, start), "begin selection refusal");
		tools->addKnifePoint(start, Qt::ControlModifier);
		tools->addKnifePoint(finish, Qt::ControlModifier);
		tools->setKnifeSelectedOnly(true);
		expect(!tools->modalPreviewValid() && !tools->finishModal(true, &error) && tools->modalOperator() == ModelModalOperator::Knife,
			   "invalid confirmation retains editable draft");
		auto *banner = editor.findChild<QLabel *>("meshOperatorBanner");
		expect(banner && banner->height() < view->height() / 3 && !banner->accessibleDescription().isEmpty(),
			   "refusal is compact with accessible details");
		expect(settleModelViewport(*view) && render(editor, QString("refusal-%1").arg(scenario)), "render knife refusal");
		tools->setKnifeSelectedOnly(false);
		expect(tools->modalPreviewValid(), "repair draft without redrawing");
		tools->finishModal(false);
		expect(bevelBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(), "cancel retains original source");
		// Face provenance must be used for both a display preview and a commit.
		int hiddenFace = -1;
		const auto &surface = source.surfaces[0];
		for (int f = 0; f < surface.triangles.size(); ++f)
		{
			const auto t = surface.triangles[f];
			const auto &p = surface.frames[0].positions;
			if (p[t.a].z == 32 && p[t.b].z == 32 && p[t.c].z == 32)
			{
				hiddenFace = f;
				break;
			}
		}
		expect(hiddenFace >= 0, "top face fixture exists");
		tools->applySelection({0, {}, {hiddenFace}});
		tools->hideSelection(false);
		const auto hidden = tools->hiddenFaces();
		expect(hidden.value(0) == QSet<int>{hiddenFace}, "hide a face before knife");
		view->projectToView({-40, 3, 0}, &start);
		view->projectToView({40, 3, 0}, &finish);
		expect(tools->beginModal(ModelModalOperator::Knife, start), "begin knife with hidden face");
		tools->addKnifePoint(start, Qt::ControlModifier);
		tools->addKnifePoint(finish, Qt::ControlModifier);
		ModelEdit expectedEdit;
		expectedEdit.kind = ModelEditKind::Knife;
		ModelVec3 right, up;
		view->viewAxes(&right, &up);
		const auto cameraOrigin = view->viewRay(QPointF(view->width() / 2., view->height() / 2.)).origin;
		const auto forward = view->viewForward();
		expectedEdit.tool.referenceFrame = tools->toolEdit(ModelEditKind::Knife).tool.referenceFrame;
		expectedEdit.tool.knife.origin = {cameraOrigin.x, cameraOrigin.y, cameraOrigin.z};
		expectedEdit.tool.knife.right = {right.x, right.y, right.z};
		expectedEdit.tool.knife.up = {up.x, up.y, up.z};
		expectedEdit.tool.knife.forward = {forward.x, forward.y, forward.z};
		const auto projected = [&](QPointF p)
		{
			using namespace model_geometry;
			const auto relative = point(view->viewRay(p).origin) - point(cameraOrigin);
			return std::array<double, 2>{dot(relative, point(right)), dot(relative, point(up))};
		};
		expectedEdit.tool.knife.segments = {{projected(start), projected(finish)}};
		expectedEdit.tool.knife.hiddenFaces = hidden;
		auto expected = source;
		ModelEditReport mapping;
		expect(applyModelEdit(&expected, expectedEdit, nullptr, &error, {}, &mapping), "prepare hidden-face reference", error);
		expect(tools->modalPreviewValid() && displayedHidden(view->mesh(), mapping.sourceFaces.value(0), hidden.value(0)),
			   "preview uses child-face provenance when hiding geometry", tools->modalText());
		expect(settleModelViewport(*view) && render(editor, QString("hidden-%1").arg(scenario)), "render hidden-face knife");
		expect(tools->finishModal(true, &error) && displayedHidden(view->mesh(), mapping.sourceFaces.value(0), hidden.value(0)),
			   "committed child faces retain hidden state", error);
		tools->action("meshAdjustLast")->trigger();
		app.processEvents();
		const auto hiddenFields = tools->lastOperationPanel()->findChildren<QCheckBox *>();
		if (hiddenFields.size() == 2)
			hiddenFields[0]->setChecked(true);
		app.processEvents();
		expected = source;
		expectedEdit.tool.knife.through = true;
		expect(applyModelEdit(&expected, expectedEdit, nullptr, &error, {}, &mapping) &&
				   displayedHidden(view->mesh(), mapping.sourceFaces.value(0), hidden.value(0)),
			   "F9 replays the original visibility mapping", error);
		expect(editor.setMesh(source, &error), "reload path fixture");
		tools->revealHidden();
		tools->applySelection({});
		tools->alignView(2, true);
		view->frameModel();
		const auto project = [&](float x, float y, float z = 32.f)
		{
			QPointF p;
			view->projectToView({x, y, z}, &p);
			return p;
		};
		const auto a = project(-16, -12), b = project(14, -12), c = project(0, 15);
		expect(tools->beginModal(ModelModalOperator::Knife, a), "begin closed path");
		for (const auto &p : {a, b, c, a})
			tools->addKnifePoint(p, Qt::ControlModifier);
		expect(tools->modalPreviewValid(), "closed path preview", tools->modalText());
		const auto closed = tools->modalPreview() ? bevelBytes(*tools->modalPreview()) : QByteArray{};
		tools->newKnifePath();
		tools->addKnifePoint(project(-20, 20), Qt::ControlModifier);
		tools->addKnifePoint(project(20, 20), Qt::ControlModifier);
		expect(tools->modalPreviewValid() && bevelBytes(*tools->modalPreview()) != closed, "disconnected path shares one draft");
		tools->undoKnifePoint();
		expect(tools->modalPreviewValid() && bevelBytes(*tools->modalPreview()) == closed,
			   "path undo removes the last segment without retaining a stale hover");
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		tools->addKnifePoint(project(20, 20), Qt::ControlModifier);
		expect(!tools->modalPreviewValid() && !editor.operationBusy() && bevelBytes(editor.document().mesh()) == bytes,
			   "cancelled preview leaves the draft and source intact");
		tools->setKnifeThrough(true);
		expect(tools->modalPreviewValid(), "draft recovers after cancelled preparation", tools->modalText());
		expect(settleModelViewport(*view) && render(editor, QString("paths-%1").arg(scenario)), "render disconnected closed paths");
		tools->finishModal(false);
		// A pending hover is flushed by confirmation, even before its queued preview.
		const auto anchor = project(-10, -9), target = project(14, 7);
		expect(tools->beginModal(ModelModalOperator::Knife, anchor), "begin angle draft");
		tools->addKnifePoint(anchor, Qt::ControlModifier);
		tools->setKnifeAngleSnap(true);
		tools->typeModal("30");
		tools->moveModal(target, Qt::ControlModifier);
		app.processEvents();
		bool onAngle = tools->modalPreviewValid();
		if (const auto *preview = tools->modalPreview())
			for (int v = surface.vertexCount; v < preview->surfaces[0].vertexCount; ++v)
			{
				QPointF p;
				view->projectToView(preview->surfaces[0].frames[0].positions[v], &p);
				const auto delta = p - anchor;
				if (std::abs(delta.x()) > 1e-3)
					onAngle &= std::abs(std::abs(delta.y() / delta.x()) - std::tan(std::numbers::pi / 6)) < 1e-4;
			}
		expect(onAngle, "typed angle constrains every cut point in the screen plane", tools->modalText());
		tools->moveModal(target + QPointF(12, -8), Qt::ControlModifier);
		expect(tools->finishModal(true, &error) && closedBevel(editor.document().mesh()), "confirm flushes pending hover preview", error);
		expect(editor.setMesh(source, &error), "reload snap fixture");
		tools->alignView(2, true);
		view->frameModel();
		expect(settleModelViewport(*view), "settle snap geometry");
		const auto midpoint = project(0, -32);
		expect(tools->beginModal(ModelModalOperator::Knife, midpoint), "begin midpoint snap");
		tools->setKnifeThrough(true);
		tools->addKnifePoint(midpoint + QPointF(2, 2), Qt::ShiftModifier);
		tools->addKnifePoint(project(3, 5), Qt::ControlModifier);
		bool midpointFound = false;
		if (const auto *preview = tools->modalPreview())
			for (int v = surface.vertexCount; v < preview->surfaces[0].vertexCount; ++v)
			{
				const auto p = preview->surfaces[0].frames[0].positions[v];
				midpointFound |= std::abs(p.x) < 1e-5 && std::abs(p.y + 32) < 1e-5 && p.z == 32;
			}
		expect(tools->modalPreviewValid() && midpointFound, "Shift uses an exact edge midpoint", tools->modalText());
		tools->finishModal(false);
		// Visible snapping must also work at silhouettes, not just interior pixels.
		for (const auto &corner : QVector<ModelVec3>{{-32, -32, 32}, {32, -32, 32}, {32, 32, 32}, {-32, 32, 32}})
		{
			const auto from = project(0, 3), to = project(corner.x, corner.y);
			expect(settleModelViewport(*view) && tools->beginModal(ModelModalOperator::Knife, from), "begin visible vertex snapping");
			tools->addKnifePoint(from, Qt::ControlModifier);
			const auto inward = (from - to) / QLineF(from, to).length();
			tools->addKnifePoint(to + inward * 2);
			bool snapped = tools->modalPreviewValid();
			if (const auto *preview = tools->modalPreview())
				for (int v = surface.vertexCount; v < preview->surfaces[0].vertexCount; ++v)
				{
					const auto p = preview->surfaces[0].frames[0].positions[v];
					snapped &= std::abs(p.x * (corner.y - 3) - (p.y - 3) * corner.x) < 1e-3;
				}
			expect(snapped, "visible vertex snap reaches a silhouette corner", tools->modalText());
			tools->finishModal(false);
		}
		expect(tools->beginModal(ModelModalOperator::Knife, project(-12, 12)), "begin resize snap test");
		tools->setKnifeThrough(true);
		tools->addKnifePoint(project(-12, 12), Qt::ControlModifier);
		editor.resize(1600, 1050);
		app.processEvents();
		tools->addKnifePoint(project(0, -32) + QPointF(2, 2), Qt::ShiftModifier);
		bool resizedSnap = false;
		if (const auto *preview = tools->modalPreview())
			for (int v = surface.vertexCount; v < preview->surfaces[0].vertexCount; ++v)
			{
				const auto p = preview->surfaces[0].frames[0].positions[v];
				resizedSnap |= std::abs(p.x) < 1e-5 && std::abs(p.y + 32) < 1e-5 && p.z == 32;
			}
		expect(tools->modalPreviewValid() && resizedSnap, "resizing refreshes snap projections while retaining fixed points");
		tools->finishModal(false);
		editor.resize(1500, 1000);
		app.processEvents();
		// Rotation after a valid preview must be noticed even without pointer motion.
		expect(tools->beginModal(ModelModalOperator::Knife, project(-40, 3)), "begin camera-change case");
		tools->addKnifePoint(project(-40, 3), Qt::ControlModifier);
		tools->addKnifePoint(project(40, 3), Qt::ControlModifier);
		tools->alignView(0, true);
		expect(!tools->finishModal(true, &error) && tools->modalOperator() == ModelModalOperator::Knife && !editor.document().canUndo(),
			   "camera change refuses old preview and retains source");
		tools->finishModal(false);
		// Every draft depends on its starting pose, mesh and visibility, even if
		// no pointer event follows a change elsewhere in the editor.
		auto *frame = editor.findChild<QComboBox *>("meshFrame");
		if (!expect(frame && frame->count() == 2, "knife context fixture has two poses"))
			return 1;
		for (int changed = 0; changed < 4; ++changed)
		{
			expect(editor.setMesh(source, &error), "reload knife context fixture", error);
			tools->revealHidden();
			tools->applySelection({});
			frame->setCurrentIndex(0);
			tools->alignView(2, true);
			view->frameModel();
			expect(tools->beginModal(ModelModalOperator::Knife, project(-40, 3)), "begin context-change draft");
			tools->addKnifePoint(project(-40, 3), Qt::ControlModifier);
			tools->addKnifePoint(project(40, 3), Qt::ControlModifier);
			expect(tools->modalPreviewValid(), "context-change case starts with a valid preview", tools->modalText());
			if (changed == 0)
				frame->setCurrentIndex(1);
			else if (changed == 1)
			{
				ModelEdit move;
				move.selection.vertices.insert(0);
				move.translation.x = 1;
				expect(editor.applyEdit(move, &error), "edit source while knife draft exists", error);
			}
			else if (changed == 2)
			{
				tools->applySelection({0, {}, {0}});
				tools->hideSelection(false);
			}
			else
			{
				auto replacement = source;
				replacement.surfaces[0].frames[0].positions[0].x -= 2;
				updateEditableModelMetadata(&replacement);
				expect(editor.setMesh(replacement, &error), "replace source while knife draft exists", error);
			}
			app.processEvents();
			const auto live = bevelBytes(editor.document().mesh());
			const auto liveHidden = tools->hiddenFaces();
			const int liveFrame = frame->currentIndex();
			const bool liveUndo = editor.document().canUndo();
			expect(!tools->finishModal(true, &error) && !tools->modalPreviewValid() &&
					   tools->modalOperator() == ModelModalOperator::Knife && error.contains("Cancel this knife path"),
				   "changed context refuses the stale preview without pointer motion", error);
			expect(bevelBytes(editor.document().mesh()) == live && tools->hiddenFaces() == liveHidden &&
					   frame->currentIndex() == liveFrame && editor.document().canUndo() == liveUndo,
				   "refusal preserves live mesh, visibility, pose and history");
			tools->finishModal(false);
			expect(bevelBytes(editor.document().mesh()) == live && tools->hiddenFaces() == liveHidden &&
					   frame->currentIndex() == liveFrame && editor.document().canUndo() == liveUndo,
				   "cancelling a stale draft preserves the newer context");
			if (changed == 1)
			{
				editor.findChild<QAction *>("undoMesh")->trigger();
				app.processEvents();
				expect(bevelBytes(editor.document().mesh()) == bytes && !editor.document().canUndo(),
					   "external edit remains one undo step after cancelling knife");
			}
		}
		expect(editor.setMesh(source, &error), "reload perspective fixture", error);
		frame->setCurrentIndex(0);
		tools->revealHidden();
		tools->action("meshViewPerspective")->trigger();
		view->setCameraView({0, -160, 80}, 90, -25);
		expect(settleModelViewport(*view) && view->isPerspective(), "prepare perspective knife view");
		QPointF pa, pb;
		view->projectToView({-40, 0, 3}, &pa);
		view->projectToView({40, 0, 3}, &pb);
		expect(tools->beginModal(ModelModalOperator::Knife, pa), "begin perspective path");
		tools->addKnifePoint(pa, Qt::ControlModifier);
		tools->addKnifePoint(pb, Qt::ControlModifier);
		expect(tools->modalPreviewValid(), "perspective preview uses camera projection", tools->modalText());
		expect(settleModelViewport(*view) && render(editor, QString("perspective-%1").arg(scenario)), "render perspective knife");
		tools->finishModal(false);
		editor.close();
		app.processEvents();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << checks << " knife UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
