#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "core/model_joint_tools.h"
#include "core/model_tags.h"
#include "core/studio_settings.h"
#include "tests/model_joint_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTranslator>
#include <QTreeWidget>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
bool render(QWidget &widget, const QString &name)
{
	const auto folder = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (folder.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(folder).filePath(QString("joints-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
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
			return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(value, QString("~~ ").repeated(value.size() / 6));
	}
};
bool move(ModelViewport &viewport)
{
	viewport.setTransformGizmo(true, ModelTransformTool::Move, 1);
	const auto handles = viewport.transformGizmoPoints();
	return viewport.beginEditTransform(handles[0]) && viewport.updateEditTransform(handles[0] + (handles[0] - handles[3]) * .6);
}
} // namespace
int main(int argc, char **argv)
{
	// Owned Qt semantic calls and render targets only; no user input/capture.
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
	QTemporaryDir temporary(QDir(root).filePath("joint-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("joint UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = jointFixture();
	QString error;
	{
		ModelViewport view;
		view.resize(800, 680);
		view.setMesh(mesh);
		view.setOrbit(0, 90);
		view.setBackfaceCulling(false);
		view.setShowJoints(true);
		view.setJointPicking(true);
		view.setShowTags(true);
		view.setEditSelection(0, {});
		view.setEditJoint("tip");
		view.setFrame(2);
		view.show();
		expect(settleModelViewport(view), "joint viewport renders");
		const auto hidden = view.jointScreenPosition("hidden");
		expect(view.jointVisible("hidden") && view.jointAt(hidden) == "hidden", "X-ray joint can be selected through opaque skin");
		view.setXrayJoints(false);
		expect(view.jointAt(hidden).isEmpty(), "stale raster cannot select joints during visibility change");
		expect(settleModelViewport(view) && !view.jointVisible("hidden") && view.jointAt(hidden).isEmpty(),
			   "opaque skin occludes hidden joint and its bone");
		expect(view.jointVisible("tip") && view.jointAt(view.jointScreenPosition("tip")) == "end",
			   "coincident visible joint origins prefer nearest depth");
		expect(render(view, "occlusion"), "render depth-aware joint overlay");
		view.setRenderMode(ModelViewportRenderMode::Wireframe);
		expect(settleModelViewport(view) && view.jointVisible("hidden"), "wireframe reveals joints");
		view.setRenderMode(ModelViewportRenderMode::FlatShaded);
		view.setXrayJoints(true);
		view.setOrbit(32, 22);
		expect(settleModelViewport(view) && move(view), "joint move begins");
		const auto transform = view.editTransform();
		ModelJointMatrix delta;
		ModelJointPreview preview;
		expect(modelJointTransformDelta(transform, &delta) && previewModelJointTransform(mesh, 2, "tip", delta, true, &preview),
			   "reference skin preview");
		expect(settleModelViewport(view), "asynchronous skin preview renders");
		const auto vertices = view.editVertexProjection();
		for (int vertex = 0; vertex < 3; ++vertex)
		{
			QPointF screen;
			view.projectToView(preview.surfaces[0].positions[vertex], &screen);
			expect(vertices && QLineF(vertices->positions[vertex], screen).length() < .05,
				   "renderer snapshot has exact deformed vertex positions");
		}
		ModelTag attachment;
		expect(view.tagPose("tag_tip", &attachment) && jointNear(attachment.origin, preview.tags[0].origin),
			   "render snapshot retains transformed attachment pose");
		QPointF expected;
		view.projectToView(modelJointTranslation(preview.pose[1]), &expected);
		expect(QLineF(expected, view.jointScreenPosition("tip")).length() < .05, "rendered joint matches transformed pose");
		ModelVec3 centroid;
		for (const auto point : preview.surfaces[0].positions)
		{
			centroid.x += point.x / 3;
			centroid.y += point.y / 3;
			centroid.z += point.z / 3;
		}
		view.projectToView(centroid, &expected);
		expect(view.hitAt(expected).valid, "renderer and ID buffer contain deformed skin");
		expect(view.jointAt(view.jointScreenPosition("tip")).isEmpty(), "active drag cannot change joint selection");
		expect(render(view, "pose-preview"), "render skin and joint preview");
		ModelViewport mirror;
		mirror.resize(600, 500);
		mirror.mirrorDisplayFrom(view);
		mirror.show();
		expect(settleModelViewport(mirror), "mirror renders same joint preview");
		ModelJointMatrix mirrored;
		expect(mirror.jointPose("tip", &mirrored) && modelJointMatrixDistance(mirrored, preview.pose[1]) < .0001,
			   "mirror uses shared pose delta");
		view.finishEditTransform(false);
		expect(settleModelViewport(view) && view.jointPose("tip", &mirrored) && jointNear(modelJointTranslation(mirrored), {8, 4, 0}),
			   "cancel restores pose");
		int commits = 0;
		QObject::connect(&view, &ModelViewport::editTransformRequested, [&](const auto &) { ++commits; });
		expect(move(view), "begin before frame change");
		view.setFrame(1);
		expect(!view.editingMove() && commits == 0, "frame change cancels gesture");
		view.setFrame(2);
		expect(settleModelViewport(view) && move(view), "begin before scope change");
		view.setJointDescendants(false);
		expect(!view.editingMove() && commits == 0, "descendant change cancels gesture");
		expect(move(view), "begin before joint change");
		view.setEditJoint("root");
		expect(!view.editingMove() && commits == 0, "joint change cancels gesture");
		view.setEditJoint("tip");
		view.setFrame(0);
		expect(settleModelViewport(view) && move(view) && settleModelViewport(view), "bind preview renders");
		expect(view.accessibleDescription().contains("preserves the mesh"), "bind semantics announced");
		expect(render(view, "bind-preview"), "render bind handles");
		view.finishEditTransform(false);
		auto independent = mesh;
		independent.surfaces[0].frames[2].positions[1].x += 1;
		view.setMesh(independent, true);
		view.setEditSelection(0, {});
		view.setEditJoint("tip");
		view.setFrame(2);
		expect(settleModelViewport(view) && move(view) && settleModelViewport(view), "protected preview completes render worker");
		expect(!view.editTransformValid() && view.accessibleDescription().contains("independent"),
			   "independent edit explains refused preview");
		view.finishEditTransform(true);
		expect(commits == 0, "refused preview cannot emit commit");
	}
	for (bool perspective : {false, true})
	{
		ModelViewport view;
		view.resize(800, 680);
		view.setMesh(mesh);
		view.setFrame(2);
		view.setOrbit(32, 22);
		auto camera = view.cameraControls();
		camera.perspective = perspective;
		view.setCameraControls(camera);
		view.setEditSelection(0, {});
		view.setEditJoint("root");
		view.setShowJoints(true);
		view.setShowTags(true);
		ModelTransformBasis basis;
		basis.world = false;
		for (auto &axis : basis.axes)
			axis = rotateModelVector(axis, {20, 10, 30});
		view.setTransformAxes(basis, "Custom");
		view.setTransformPivot(ModelTransformPivot::Custom, {2, 2, 0});
		view.show();
		for (auto tool : {ModelTransformTool::Rotate, ModelTransformTool::Scale})
		{
			view.setTransformGizmo(true, tool, 1, 15, .25);
			expect(settleModelViewport(view), "oriented joint gesture source renders");
			auto start = view.transformGizmoPoints()[3], end = start + QPointF(48, -48);
			if (tool == ModelTransformTool::Rotate)
			{
				const auto ring = view.rotationGizmoRings()[2];
				int step = -1;
				for (int i = 0; i < 96 && ring.size() == 97; ++i)
					if (view.transformGizmoAt(ring[i]) == 2 && QLineF(ring[i], ring[(i + 16) % 96]).length() > 20)
					{
						step = i;
						break;
					}
				if (!expect(step >= 0, "joint rotation ring exposes a distinct handle"))
					continue;
				start = ring[step];
				end = ring[(step + 16) % 96];
			}
			expect(view.beginEditTransform(start) && view.updateEditTransform(end) && settleModelViewport(view) &&
					   view.editTransformValid(),
				   "joint rotation or scale renders through shared gizmo");
			ModelJointMatrix delta;
			ModelJointPreview preview;
			expect(modelJointTransformDelta(view.editTransform(), &delta) &&
					   previewModelJointTransform(mesh, 2, "root", delta, true, &preview),
				   "oriented joint transform validates");
			const auto vertices = view.editVertexProjection();
			for (int vertex = 0; vertex < 3 && !preview.surfaces.isEmpty(); ++vertex)
			{
				QPointF expected;
				view.projectToView(preview.surfaces[0].positions[vertex], &expected);
				expect(vertices && QLineF(vertices->positions[vertex], expected).length() < .05,
					   "rotated/scaled skin agrees in both projections");
			}
			expect(render(view, QString("oriented-%1-%2").arg(perspective).arg(int(tool))), "render joint rotation and scale");
			view.finishEditTransform(false);
		}
	}
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 2 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelEditorDialog editor;
		editor.setAccessibility(scenario != 0, true);
		expect(editor.setMesh(mesh, &error), "editor opens rig");
		editor.resize(scenario ? 2300 : 1400, scenario ? 1500 : 1000);
		editor.show();
		editor.showSidebarPage("skeleton");
		app.processEvents();
		auto *tree = editor.findChild<QTreeWidget *>("meshSkeletonJoints");
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *frame = editor.findChild<QComboBox *>("meshFrame");
		auto *scope = editor.findChild<QComboBox *>("meshFrameScope");
		auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *apply = editor.findChild<QPushButton *>("transformMesh");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		auto *children = editor.findChild<QCheckBox *>("meshJointDescendants");
		if (!expect(tree && mode && frame && scope && viewport && table && apply && undo && redo && children,
					"joint editor controls exist"))
			return 1;
		for (const char *id : {"meshShowJoints", "meshXrayJoints", "meshJointDescendants", "meshSkeletonJoints"})
		{
			auto *widget = editor.findChild<QWidget *>(id);
			expect(widget && QAccessible::queryAccessibleInterface(widget) && (widget->focusPolicy() & Qt::TabFocus) &&
					   !widget->accessibleDescription().isEmpty(),
				   "joint control exposes focus and accessible description");
		}
		tree->setCurrentItem(tree->topLevelItem(0)->child(0));
		expect(editor.document().selection().joint == "tip" && mode->currentIndex() == 6 && !scope->isEnabled() &&
				   table->model()->rowCount() == 4,
			   "hierarchy selects joint, table and explicit scope");
		frame->setCurrentIndex(2);
		expect(editor.tools()->beginModal(ModelModalOperator::Rotate, QPointF(100, 100)) &&
				   editor.findChild<QComboBox *>("meshTransformTool")->currentIndex() == 1 &&
				   editor.tools()->modalOperator() == ModelModalOperator::None,
			   "joint transform shortcut selects its shared handle tool");
		editor.findChild<QCheckBox *>("meshSnapTranslation")->setChecked(false);
		expect(settleModelViewport(*viewport) && render(editor, QString("hierarchy-%1").arg(scenario)), "render accessible skeleton panel");
		children->setChecked(false);
		editor.findChild<QDoubleSpinBox *>("meshOffset1")->setValue(3);
		apply->click();
		expect(jointNear(editor.document().mesh().surfaces[0].frames[2].positions[1], {8, 7, 0}) &&
				   jointNear(modelJointTranslation(editor.document().mesh().skeleton.clips[0].frames[1][2]), {8, 4, 2}),
			   "numeric joint edit affects current pose and respects children option");
		expect(undo->isEnabled(), "joint numeric edit creates undo");
		undo->trigger();
		expect(rigBytes(editor.document().mesh()) == rigBytes(mesh) && editor.document().selection().joint == "tip",
			   "undo restores mesh and joint selection");
		redo->trigger();
		expect(jointNear(editor.document().mesh().surfaces[0].frames[2].positions[1], {8, 7, 0}), "redo restores edited pose");
		undo->trigger();
		viewport->setOrbit(32, 22);
		expect(settleModelViewport(*viewport) && move(*viewport), "editor joint gizmo begins");
		const auto transform = viewport->editTransform();
		viewport->finishEditTransform(true);
		expect(jointNear(editor.document().mesh().surfaces[0].frames[2].positions[1], transformModelPoint({8, 4, 0}, transform)),
			   "editor gizmo commits same pose delta");
		undo->trigger();
		frame->setCurrentIndex(0);
		apply->click();
		expect(jointNear(modelJointTranslation(editor.document().mesh().skeleton.joints[1].bind), {8, 3, 0}) &&
				   jointNear(editor.document().mesh().surfaces[0].frames[2].positions[1], {8, 4, 0}),
			   "numeric bind edit changes pivot while preserving animation");
		undo->trigger();
		editor.showSidebarPage("outliner");
		app.processEvents();
		table->selectionModel()->select(table->model()->index(2, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		expect(editor.document().selection().joint == "end" && tree->currentItem() && tree->currentItem()->text(0) == "end",
			   "component table synchronises hierarchy");
		mode->setCurrentIndex(1);
		table->selectionModel()->select(table->model()->index(0, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		expect(editor.document().selection().joint.isEmpty() && editor.document().selection().vertices.contains(0) && scope->isEnabled(),
			   "mesh selection clears joint mode and restores geometry scope");
		tree->setCurrentItem(tree->topLevelItem(0)->child(0));
		editor.setViewLayout(ModelViewLayout::FourViews);
		for (auto *pane : editor.panes())
			expect(settleModelViewport(*pane), "four-view joint pane settles");
		expect(render(editor, QString("four-views-%1").arg(scenario)), "render four-view joint workflow");
		if (auto *scroll = pageScroll(editor.sidebarPage("skeleton")))
			expect(scroll->horizontalScrollBar()->maximum() == 0, "skeleton panel fits translated scaled content");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
