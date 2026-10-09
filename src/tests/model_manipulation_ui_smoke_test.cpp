#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QLabel>
#include <QLineF>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTranslator>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
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
bool equal(ModelVec3 a, ModelVec3 b)
{
	return std::abs(a.x - b.x) < 0.0001f && std::abs(a.y - b.y) < 0.0001f && std::abs(a.z - b.z) < 0.0001f;
}
QImage render(ModelViewport &viewport)
{
	QImage image(viewport.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	viewport.render(&image);
	return image;
}
ModelMesh fixture()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	auto mesh = buildModelDesignMesh(design);
	mesh.frames << mesh.frames[0];
	mesh.frames[1].name = QStringLiteral("pose2");
	mesh.surfaces[0].frames << mesh.surfaces[0].frames[0];
	for (auto &point : mesh.surfaces[0].frames[1].positions)
	{
		point.z += 8;
	}
	updateEditableModelMetadata(&mesh);
	return mesh;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
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
	// Public value APIs and widget signals only: no mouse/key event injection or
	// operating-system captures. Render the widget's own target for visual QA.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-manipulation-ui-smoke"); skip >= 0) {
		return skip;
	}
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-manipulation-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	bool ok = true;
	QString error;
	const auto original = fixture();
	{
		auto overlapping = original;
		overlapping.surfaces << overlapping.surfaces[0];
		overlapping.surfaces[1].name = QStringLiteral("front");
		for (int surface = 0; surface < 2; ++surface)
		{
			for (auto &pose : overlapping.surfaces[surface].frames)
			{
				pose.positions = {{float(surface * 4), -20, -20},
								  {float(surface * 4), 20, -20},
								  {float(surface * 4), 20, 20},
								  {float(surface * 4), -20, 20}};
			}
		}
		updateEditableModelMetadata(&overlapping);
		ModelViewport viewport;
		viewport.resize(500, 500);
		viewport.setMesh(overlapping);
		viewport.setBackfaceCulling(false);
		viewport.setOrbit(0, 0);
		viewport.setVertexPicking(true);
		viewport.setEditSelection(0, {});
		viewport.show();
		app.processEvents();
		ok &= expect(tests::settleModelViewport(viewport), "vertex occlusion fixture renders");
		const auto point = viewport.vertexScreenPosition(0, 0);
		ok &= expect(!viewport.vertexAt(point).valid, "hidden rear vertex cannot use foreground face coverage");
		viewport.setXrayVertices(true);
		ok &= expect(tests::settleModelViewport(viewport), "X-ray vertex overlay settles");
		ok &= expect(viewport.vertexAt(point).valid && viewport.vertexAt(point).surface == 0 && viewport.vertexAt(point).vertex == 0,
					 "X-ray explicitly enables hidden point selection on the active surface");
		viewport.setXrayVertices(false);
		viewport.setEditSelection(1, {});
		ok &= expect(tests::settleModelViewport(viewport), "new surface vertex projection settles");
		ok &= expect(viewport.vertexAt(point).valid && viewport.vertexAt(point).vertex == 0,
					 "visible silhouette vertex is pickable at its exact projected position");
		viewport.setRenderMode(ModelViewportRenderMode::Wireframe);
		viewport.setEditSelection(0, {});
		ok &= expect(tests::settleModelViewport(viewport) && viewport.vertexAt(point).valid,
					 "completed wireframe vertex selection follows its through-mesh rendering");
		CameraViewControls controls;
		controls.perspective = true;
		viewport.setCameraControls(controls);
		viewport.setCameraView({-100, 0, 0}, 0, 0);
		viewport.setXrayVertices(true);
		ok &= expect(tests::settleModelViewport(viewport), "perspective vertex snapshot settles");
		const auto front = viewport.vertexScreenPosition(0, 0);
		ok &= expect(viewport.vertexAt(front, .01).vertex == 0, "perspective vertex index matches exact projection");
		viewport.setCameraView({100, 0, 0}, 0, 0);
		ok &= expect(!viewport.vertexAt(front).valid && tests::settleModelViewport(viewport) && !viewport.vertexAt(front).valid &&
						 !std::isfinite(viewport.vertexScreenPosition(0, 0).x()),
					 "camera changes reject stale and behind-camera X-ray vertices");
		auto shifted = overlapping;
		for (auto &surface : shifted.surfaces)
		{
			for (auto &position : surface.frames[1].positions)
			{
				position.z += 8;
			}
		}
		updateEditableModelMetadata(&shifted);
		viewport.setMesh(shifted, true);
		viewport.setEditSelection(0, {});
		viewport.setCameraView({-100, 0, 0}, 0, 0);
		ok &= expect(tests::settleModelViewport(viewport), "replacement mesh vertex snapshot settles");
		viewport.setFrame(1);
		ok &= expect(!viewport.vertexAt(front).valid && tests::settleModelViewport(viewport), "pose changes retire old vertex picks");
		const auto shiftedPoint = viewport.vertexScreenPosition(0, 0);
		ok &= expect((shiftedPoint - front).manhattanLength() > 1 && viewport.vertexAt(shiftedPoint, .01).vertex == 0 &&
						 !viewport.vertexAt(front, .01).valid,
					 "vertex index follows the replacement mesh and current pose");
	}
	{
		ModelViewport viewport;
		viewport.resize(660, 500);
		viewport.setMesh(original);
		viewport.setVertexPicking(true);
		viewport.setEditSelection(0, {0, 1, 2, 3});
		viewport.show();
		for (const auto orbit : {QPointF(30, 20), QPointF(-90, 35), QPointF(90, 10), QPointF(120, 15)})
		{
			viewport.setOrbit(orbit.x(), orbit.y());
			ok &= expect(tests::settleModelViewport(viewport), "oblique silhouette vertex fixture settles");
			for (int vertex = 0; vertex < 4; ++vertex)
			{
				const auto hit = viewport.vertexAt(viewport.vertexScreenPosition(0, vertex), .01);
				ok &= expect(hit.valid && hit.vertex == vertex,
							 "all unoccluded silhouette corners remain pickable in an oblique filled view");
			}
		}
	}
	{
		ModelViewport viewport;
		viewport.resize(660, 500);
		viewport.setMesh(original);
		viewport.setOrbit(30, 30);
		viewport.setMoveGizmo(true);
		viewport.setEditSelection(0, {0});
		const auto verifyPivot = [&](int vertex, const char *message)
		{
			const auto expected = viewport.vertexScreenPosition(0, vertex);
			const auto first = viewport.transformGizmoPoints()[3];
			const auto second = viewport.transformGizmoPoints()[3];
			ok &= expect(QLineF(first, expected).length() < .001 && first == second, message);
		};
		verifyPivot(0, "repeated gizmo queries retain the selected vertex pivot");
		viewport.setEditSelection(0, {2});
		verifyPivot(2, "selection changes refresh the cached gizmo pivot");
		viewport.setFrame(1);
		verifyPivot(2, "pose changes refresh the cached gizmo pivot");
		auto replacement = original;
		replacement.surfaces[0].frames[1].positions[2].z += 12;
		updateEditableModelMetadata(&replacement);
		viewport.setMesh(replacement, true);
		viewport.setEditSelection(0, {2});
		viewport.setFrame(1);
		verifyPivot(2, "replacement mesh data refreshes the cached gizmo pivot");
	}
	{
		ModelViewport viewport;
		viewport.resize(500, 500);
		viewport.setMesh(original);
		viewport.setShowGrid(false);
		viewport.setShowAxes(false);
		viewport.setOrbit(-90, 90);
		viewport.setEditSelection(0, {0, 1, 2, 3});
		viewport.setMoveGizmo(true);
		viewport.show();
		app.processEvents();
		ok &= expect(tests::settleModelViewport(viewport), "rollback fixture renders its source");
		const auto sourceImage = render(viewport);
		auto handles = viewport.moveGizmoPoints();
		ok &= expect(viewport.beginEditMove(handles[0]) && viewport.updateEditMove(handles[0] + QPointF(60, 0)) &&
						 tests::settleModelViewport(viewport),
					 "move preview reaches the presented image");
		const auto previewImage = render(viewport);
		const auto sample =
			((viewport.vertexScreenPosition(0, 0) + viewport.vertexScreenPosition(0, 1) + viewport.vertexScreenPosition(0, 2)) / 3)
				.toPoint();
		const auto background = sourceImage.pixelColor(10, 250);
		ok &= expect(previewImage.pixelColor(sample) != background, "rollback probe lies inside a rendered preview face");
		viewport.updateEditMove(handles[0] + QPointF(80, 0));
		render(viewport); // Start another render without dispatching its completion.
		ok &= expect(viewport.isRendering(), "a preview render is pending at cancellation");
		viewport.finishEditMove(false);
		ok &= expect(render(viewport).pixelColor(sample) == background,
					 "cancellation immediately removes the presented preview while retiring its worker");
		ok &= expect(tests::settleModelViewport(viewport) && render(viewport) == sourceImage,
					 "a late preview result cannot overwrite the restored source image");
		handles = viewport.moveGizmoPoints();
		ok &= expect(viewport.beginEditMove(handles[0]) && viewport.updateEditMove(handles[0] + QPointF(60, 0)),
					 "begin a gesture before changing the camera");
		viewport.setOrbit(30, 20);
		ok &= expect(!viewport.editingMove(), "a camera change cancels its obsolete drag plane");
	}
	{
		ModelViewport viewport;
		viewport.resize(500, 500);
		viewport.setMesh(original);
		viewport.setShowGrid(false);
		viewport.setShowAxes(false);
		viewport.setOrbit(-90, 90);
		viewport.setEditSelection(0, {0, 1, 2, 3});
		viewport.setTransformGizmo(true, ModelTransformTool::Rotate, 0, 180);
		viewport.show();
		app.processEvents();
		ok &= expect(tests::settleModelViewport(viewport), "rotation-facing source renders");
		const auto sourceImage = render(viewport);
		const auto ring = viewport.rotationGizmoRings()[0];
		const auto tangent = (ring[13] - ring[11]) / (4 * std::numbers::pi / 96);
		ok &= expect(viewport.transformGizmoAt(ring[12]) == 0 && viewport.beginEditTransform(ring[12]) &&
						 viewport.updateEditTransform(ring[12] + tangent * std::numbers::pi) &&
						 std::abs(viewport.editTransform().rotation.x - 180) < 0.01 && tests::settleModelViewport(viewport),
					 "edge-on ring fallback produces a snapped half-turn");
		const auto flipped = render(viewport);
		// Probe the face away from the free-rotation handle, labels and ring.
		ok &= expect(sourceImage.pixelColor(350, 350) != sourceImage.pixelColor(10, 250) &&
						 flipped.pixelColor(350, 350) == flipped.pixelColor(10, 250),
					 "rotation preview transforms normals before backface culling");
		viewport.finishEditTransform(false);
		ok &= expect(tests::settleModelViewport(viewport) && render(viewport) == sourceImage,
					 "rotation cancellation retires its raster preview");
	}
	{
		ModelViewport viewport;
		viewport.resize(500, 500);
		viewport.setMesh(original);
		viewport.setOrbit(32, 22);
		auto controls = viewport.cameraControls();
		controls.perspective = true;
		viewport.setCameraControls(controls);
		viewport.setEditSelection(0, {0, 1, 2, 3});
		viewport.setTransformGizmo(true, ModelTransformTool::Rotate, 0, 15);
		viewport.show();
		app.processEvents();
		for (int axis = 0; axis < 3; ++axis)
		{
			ok &= expect(tests::settleModelViewport(viewport), "perspective rotation source settles");
			const auto ring = viewport.rotationGizmoRings()[axis];
			int start = -1;
			for (int i = 0; i < 96 && ring.size() == 97; ++i)
			{
				if (viewport.transformGizmoAt(ring[i]) == axis && (ring[(i + 16) % 96] - ring[i]).manhattanLength() > 20)
				{
					start = i;
					break;
				}
			}
			ok &= expect(start >= 0, "each perspective ring has a distinct pickable segment");
			if (start < 0)
			{
				continue;
			}
			ok &= expect(viewport.beginEditTransform(ring[start]) && viewport.updateEditTransform(ring[(start + 16) % 96]),
						 "perspective ring rays produce a valid world-axis rotation");
			const auto transform = viewport.editTransform();
			const float angles[]{transform.rotation.x, transform.rotation.y, transform.rotation.z};
			ok &= expect(std::abs(angles[axis] - 60) < 0.01,
						 "perspective rotation measures the world angle, not the projected ellipse angle");
			QPolygonF preview;
			for (int vertex = 0; vertex < 4; ++vertex)
			{
				preview << viewport.vertexScreenPosition(0, vertex);
			}
			auto committed = original;
			ModelEdit edit;
			edit.selection.vertices = {0, 1, 2, 3};
			edit.rotation = transform.rotation;
			edit.pivot = transform.pivot;
			ok &= expect(applyModelEdit(&committed, edit, nullptr, &error),
						 "perspective preview validates through the shared document service");
			viewport.finishEditTransform(false);
			viewport.setMesh(committed, true);
			for (int vertex = 0; vertex < 4; ++vertex)
			{
				ok &= expect((viewport.vertexScreenPosition(0, vertex) - preview[vertex]).manhattanLength() < 0.01,
							 "perspective committed vertices match the preview without a camera jump");
			}
			viewport.setMesh(original, true);
			viewport.setEditSelection(0, {0, 1, 2, 3});
		}
		viewport.setTransformGizmo(true, ModelTransformTool::Scale, 0, 0, 0.25);
		ok &= expect(tests::settleModelViewport(viewport), "perspective scale source settles");
		const auto centre = viewport.transformGizmoPoints()[3];
		int committed = 0;
		QObject::connect(&viewport, &ModelViewport::editTransformRequested, [&] { ++committed; });
		ok &= expect(viewport.beginEditTransform(centre) && viewport.updateEditTransform(centre + QPointF(96, 0)) &&
						 !viewport.updateEditTransform({std::numeric_limits<double>::quiet_NaN(), 0}) && !viewport.editTransformValid(),
					 "nonfinite positions invalidate a previous valid scale preview");
		ok &= expect(viewport.updateEditTransform(centre + QPointF(96, 0)) && viewport.editTransformValid() &&
						 equal(viewport.editTransform().scale, {2, 2, 2}),
					 "returning to a valid scale position restores the gesture");
		viewport.setTransformPivot(ModelTransformPivot::Origin);
		viewport.finishEditTransform(true);
		ok &= expect(!viewport.editingMove() && committed == 0, "pivot changes cancel an active scale without dispatching an edit");
		ok &= expect(tests::settleModelViewport(viewport), "cancelled perspective scale settles");
		ok &= expect(viewport.beginEditTransform(viewport.transformGizmoPoints()[3]), "begin scale before changing the snap step");
		viewport.setTransformGizmo(true, ModelTransformTool::Scale, 0, 0, 0.5);
		ok &= expect(!viewport.editingMove() && committed == 0, "snap changes cancel obsolete gestures");
	}
	for (int scale : {100, 200})
	{
		Expansion expansion;
		if (scale == 200)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app,
						 studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		auto *editor = new ModelEditorDialog;
		ok &= expect(editor->setMesh(original, &error), "animated mesh opens for manipulation");
		if (scale == 200)
		{
			editor->setLayoutDirection(Qt::RightToLeft);
			editor->resize(1900, 1150);
		}
		editor->setAccessibility(scale == 200, true);
		editor->show();
		app.processEvents();
		auto *preview = editor->findChild<ModelViewport *>("meshPreview");
		auto *table = editor->findChild<QTableView *>("meshComponents");
		auto *mode = editor->findChild<QComboBox *>("meshSelectionMode");
		auto *grid = editor->findChild<QDoubleSpinBox *>("meshTranslationGrid");
		auto *snap = editor->findChild<QCheckBox *>("meshSnapTranslation");
		auto *view = editor->findChild<QComboBox *>("meshViewPreset");
		ok &= expect(preview && table && mode && grid && snap && view && !grid->accessibleName().isEmpty() &&
						 !snap->accessibleName().isEmpty(),
					 "viewport editing controls have accessible metadata");
		mode->setCurrentIndex(1);
		preview->vertexPicked(0, 0, int(ModelViewportPick::Replace));
		ok &= expect(editor->document().selection().vertices == QSet<int>{0} && table->selectionModel()->selectedRows().size() == 1,
					 "precise vertex signal selects only one indexed point and synchronizes the table");
		preview->vertexPicked(0, 0, int(ModelViewportPick::Toggle));
		ok &= expect(editor->document().selection().vertices.isEmpty(), "vertex toggle clears the exact point");
		table->selectAll();
		snap->setChecked(true);
		grid->setValue(2);
		ok &= expect(tests::settleModelViewport(*preview), "selected vertices render before gizmo hit testing");
		auto handles = preview->moveGizmoPoints();
		ok &= expect(std::isfinite(handles[0].x()) && preview->moveGizmoAt(handles[0]) == 0 && preview->moveGizmoAt(handles[3]) == 3,
					 "labelled axis and view-plane handles have independent hit regions");
		const auto before = editableModelJson(editor->document().mesh());
		ok &= expect(preview->beginEditMove(handles[0]) && preview->updateEditMove(handles[0] + (handles[0] - handles[3]) * 0.65),
					 "axis manipulation begins and updates without input injection");
		const auto delta = preview->editMoveDelta();
		ok &= expect(delta.x != 0 && std::fmod(delta.x, 2) == 0 && delta.y == 0 && delta.z == 0 &&
						 editableModelJson(editor->document().mesh()) == before && !editor->document().isModified() &&
						 !editor->document().canUndo(),
					 "snapped preview leaves source and history untouched");
		preview->finishEditMove(false);
		ok &= expect(!preview->editingMove() && editableModelJson(editor->document().mesh()) == before && !editor->document().canUndo(),
					 "cancelling a move rolls back preview without an undo item");
		ok &= expect(tests::settleModelViewport(*preview), "cancelled preview retires before a second gesture");
		handles = preview->moveGizmoPoints();
		ok &= expect(preview->beginEditMove(handles[0]) && preview->updateEditMove(handles[0] + (handles[0] - handles[3]) * 0.65),
					 "repeat an axis gesture for commit");
		const auto committed = preview->editMoveDelta();
		preview->finishEditMove(true);
		ok &= expect(editor->document().canUndo() && editor->document().isModified(), "release commits one document edit");
		for (int frame = 0; frame < 2; ++frame)
		{
			const auto a = original.surfaces[0].frames[frame].positions[0],
					   b = editor->document().mesh().surfaces[0].frames[frame].positions[0];
			ok &= expect(equal(b, {a.x + committed.x, a.y, a.z}), "gizmo commit applies the same translation in every frame");
		}
		editor->findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(!editor->document().isModified() && !editor->document().canUndo() &&
						 editableModelJson(editor->document().mesh()) == before && editor->document().selection().vertices.size() == 4,
					 "one undo restores all poses and the component selection");
		auto *scope = editor->findChild<QComboBox *>("meshFrameScope");
		auto *frame = editor->findChild<QComboBox *>("meshFrame");
		scope->setCurrentIndex(1);
		frame->setCurrentIndex(1);
		ok &= expect(tests::settleModelViewport(*preview), "current-frame selection settles");
		handles = preview->moveGizmoPoints();
		ok &= expect(preview->beginEditMove(handles[0]) && preview->updateEditMove(handles[0] + (handles[0] - handles[3]) * 0.65),
					 "current-frame gesture begins");
		const auto frameDelta = preview->editMoveDelta();
		preview->finishEditMove(true);
		const auto originalPose = original.surfaces[0].frames[1].positions[0];
		ok &= expect(equal(editor->document().mesh().surfaces[0].frames[0].positions[0], original.surfaces[0].frames[0].positions[0]) &&
						 equal(editor->document().mesh().surfaces[0].frames[1].positions[0],
							   {originalPose.x + frameDelta.x, originalPose.y, originalPose.z}),
					 "current-frame commit preserves the unselected animation pose");
		editor->findChild<QAction *>("undoMesh")->trigger();
		scope->setCurrentIndex(0);
		frame->setCurrentIndex(0);
		ok &= expect(tests::settleModelViewport(*preview), "playback start fixture settles");
		handles = preview->moveGizmoPoints();
		editor->setAccessibility(scale == 200, false);
		preview->play();
		ok &= expect(preview->isPlaying() && preview->beginEditMove(handles[0]) && preview->editingMove() && !preview->isPlaying() &&
						 preview->updateEditMove(handles[0] + (handles[0] - handles[3]) * 0.65),
					 "a gesture pauses playback and survives the editor's synchronous pause refresh");
		preview->setFrame(1);
		ok &= expect(!preview->editingMove() && !editor->document().canUndo() && editableModelJson(editor->document().mesh()) == before,
					 "changing the frame cancels its gesture without editing another pose");
		frame->setCurrentIndex(0);
		editor->setAccessibility(scale == 200, true);
		view->setCurrentIndex(1);
		ok &= expect(!preview->isPerspective() && preview->pitch() == 90 && tests::settleModelViewport(*preview),
					 "top preset uses an exact orthographic plane");
		handles = preview->moveGizmoPoints();
		ok &= expect(std::isfinite(handles[0].x()) && std::isfinite(handles[1].x()) && !std::isfinite(handles[2].x()),
					 "view-aligned axis is hidden instead of producing an unstable drag");
		view->setCurrentIndex(4);
		ok &= expect(preview->isPerspective() && tests::settleModelViewport(*preview), "perspective preset completes its render");
		handles = preview->moveGizmoPoints();
		ok &= expect(preview->beginEditMove(handles[3]) && preview->updateEditMove(handles[3] + QPointF(30, 25)),
					 "perspective view-plane movement intersects stable camera rays");
		preview->finishEditMove(false);
		view->setCurrentIndex(0);
		ok &= expect(tests::settleModelViewport(*preview), "orbit preset is restored for evidence");
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty())
		{
			QImage image(editor->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			editor->render(&image);
			ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("mesh-manipulation-%1.png").arg(scale))),
						 "save manipulation UI evidence");
		}
		auto *tool = editor->findChild<QComboBox *>("meshTransformTool");
		auto *pivot = editor->findChild<QComboBox *>("meshTransformPivotMode");
		auto *angleGrid = editor->findChild<QDoubleSpinBox *>("meshRotationGrid");
		auto *scaleGrid = editor->findChild<QDoubleSpinBox *>("meshScaleGrid");
		auto *customPivotFields = editor->findChild<QWidget *>("meshCustomPivotFields");
		ok &= expect(customPivotFields && customPivotFields->isHidden(), "custom coordinates are hidden for the selection pivot");
		ok &= expect(tool && pivot && angleGrid && scaleGrid && !tool->accessibleName().isEmpty() && !pivot->accessibleName().isEmpty() &&
						 !angleGrid->accessibleName().isEmpty() && !scaleGrid->accessibleName().isEmpty(),
					 "rotation, scale and pivot controls expose accessible names");
		const auto capture = [&](const QString &kind)
		{
			if (evidence.isEmpty())
			{
				return true;
			}
			QImage image(editor->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			editor->render(&image);
			return image.save(QDir(evidence).filePath(QStringLiteral("mesh-%1-%2.png").arg(kind).arg(scale)));
		};
		view->setCurrentIndex(1);
		tool->setCurrentIndex(1);
		angleGrid->setValue(15);
		ok &= expect(tests::settleModelViewport(*preview), "rotation selection settles in the top view");
		auto rings = preview->rotationGizmoRings();
		ok &= expect(preview->transformGizmoAt(rings[2][12]) == 2 && preview->beginEditTransform(rings[2][12]) &&
						 preview->updateEditTransform(rings[2][36]) && std::abs(preview->editTransform().rotation.z - 90) < 0.001 &&
						 editableModelJson(editor->document().mesh()) == before && !editor->document().canUndo(),
					 "quarter-turn preview is snapped without changing history");
		ok &= expect(tests::settleModelViewport(*preview) && capture(QStringLiteral("rotate-preview")),
					 "capture labelled rotation rings and their lossless preview");
		const auto rotatedPoint = preview->vertexScreenPosition(0, 0);
		preview->finishEditTransform(true);
		ok &= expect(tests::settleModelViewport(*preview) && (preview->vertexScreenPosition(0, 0) - rotatedPoint).manhattanLength() < 0.01,
					 "committed rotation matches the displayed preview");
		for (int pose = 0; pose < 2; ++pose)
		{
			const auto p = original.surfaces[0].frames[pose].positions[0];
			ok &= expect(equal(editor->document().mesh().surfaces[0].frames[pose].positions[0], {-p.y, p.x, p.z}),
						 "rotation reaches every pose about one fixed pivot");
		}
		editor->findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(!editor->document().canUndo() && editableModelJson(editor->document().mesh()) == before,
					 "rotation undo restores the complete source");
		scope->setCurrentIndex(1);
		frame->setCurrentIndex(1);
		ok &= expect(tests::settleModelViewport(*preview), "local rotation pose settles");
		rings = preview->rotationGizmoRings();
		const auto tangent = (rings[0][13] - rings[0][11]) / (4 * std::numbers::pi / 96);
		ok &= expect(preview->beginEditTransform(rings[0][12]) && preview->updateEditTransform(rings[0][12] + tangent),
					 "edge-on current-pose rotation begins");
		const auto localAngle = preview->editTransform().rotation.x;
		preview->finishEditTransform(true);
		const double radians = localAngle * std::numbers::pi / 180;
		const auto p = original.surfaces[0].frames[1].positions[0];
		ok &= expect(equal(editor->document().mesh().surfaces[0].frames[0].positions[0], original.surfaces[0].frames[0].positions[0]) &&
						 equal(editor->document().mesh().surfaces[0].frames[1].positions[0],
							   {p.x, float(p.y * std::cos(radians)), float(8 + p.y * std::sin(radians))}),
					 "current-frame rotation resolves the displayed pose's pivot and preserves the other pose");
		editor->findChild<QAction *>("undoMesh")->trigger();
		scope->setCurrentIndex(0);
		frame->setCurrentIndex(0);
		tool->setCurrentIndex(2);
		scaleGrid->setValue(0.25);
		ok &= expect(tests::settleModelViewport(*preview), "scale handles settle");
		handles = preview->transformGizmoPoints();
		ok &=
			expect(preview->beginEditTransform(handles[0]) && preview->updateEditTransform(handles[0] + (handles[0] - handles[3]) * 0.65) &&
					   equal(preview->editTransform().scale, {1.75f, 1, 1}) && editableModelJson(editor->document().mesh()) == before,
				   "axis scaling snaps the factor relative to identity without altering other axes");
		ok &= expect(tests::settleModelViewport(*preview) && capture(QStringLiteral("scale-preview")),
					 "capture scale boxes and factor diagnostics");
		preview->finishEditTransform(true);
		for (int pose = 0; pose < 2; ++pose)
		{
			const auto v = original.surfaces[0].frames[pose].positions[0];
			ok &= expect(equal(editor->document().mesh().surfaces[0].frames[pose].positions[0], {v.x * 1.75f, v.y, v.z}),
						 "axis scaling commits to every pose");
		}
		editor->findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(tests::settleModelViewport(*preview), "uniform scale source settles");
		handles = preview->transformGizmoPoints();
		ok &= expect(preview->beginEditTransform(handles[3]) && preview->updateEditTransform(handles[3] + QPointF(96, 0)) &&
						 equal(preview->editTransform().scale, {2, 2, 2}),
					 "centre handle scales uniformly");
		preview->finishEditTransform(true);
		ok &= expect(editor->document().mesh().surfaces[0].frames[1].positions[0].z == 16,
					 "all poses use the displayed fixed pivot for uniform scaling");
		editor->findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(tests::settleModelViewport(*preview), "invalid scale source settles");
		handles = preview->transformGizmoPoints();
		ok &= expect(preview->beginEditTransform(handles[0]) && !preview->updateEditTransform(handles[3]) && !preview->editTransformValid(),
					 "zero-scale pointer position is rejected without collapsing the preview");
		preview->finishEditTransform(true);
		ok &= expect(!editor->document().canUndo() && editableModelJson(editor->document().mesh()) == before,
					 "releasing an invalid scale cancels instead of committing a prior valid value");
		frame->setCurrentIndex(1);
		auto *rotationX = editor->findChild<QDoubleSpinBox *>("meshRotation0");
		auto *scaleZ = editor->findChild<QDoubleSpinBox *>("meshScale2");
		rotationX->setValue(179);
		scaleZ->setValue(1.26);
		scaleGrid->setValue(0.1);
		editor->findChild<QPushButton *>("transformMesh")->click();
		ok &= expect(std::abs(editor->document().mesh().surfaces[0].frames[0].positions[0].z - 18.4f) < 0.0001 &&
						 editor->document().mesh().surfaces[0].frames[1].positions[0].z == 8,
					 "numeric Apply Transform uses all snap steps and the displayed selection pivot across poses");
		editor->findChild<QAction *>("undoMesh")->trigger();
		rotationX->setValue(0);
		scaleZ->setValue(1);
		scaleGrid->setValue(0.25);
		frame->setCurrentIndex(0);
		pivot->setCurrentIndex(2);
		auto *pivotX = editor->findChild<QDoubleSpinBox *>("meshPivot0");
		ok &= expect(pivotX && pivotX->isEnabled() && !customPivotFields->isHidden() && pivotX->layoutDirection() == Qt::LeftToRight,
					 "custom pivot reveals coordinate editing with stable numeric direction");
		pivotX->setValue(10);
		ok &= expect(tests::settleModelViewport(*preview), "custom pivot source settles");
		handles = preview->transformGizmoPoints();
		ok &= expect(preview->beginEditTransform(handles[0]) && preview->updateEditTransform(handles[0] + (handles[0] - handles[3]) * 0.65),
					 "scale around a custom pivot");
		preview->finishEditTransform(true);
		const auto first = original.surfaces[0].frames[0].positions[0];
		ok &= expect(equal(editor->document().mesh().surfaces[0].frames[0].positions[0], {(first.x - 10) * 1.75f + 10, first.y, first.z}),
					 "custom pivot is identical between preview and document validation");
		editor->findChild<QAction *>("undoMesh")->trigger();
		pivot->setCurrentIndex(1);
		tool->setCurrentIndex(0);
		view->setCurrentIndex(0);
		// Moving one corner exactly onto another is previewable but cannot commit.
		preview->vertexPicked(0, 0, int(ModelViewportPick::Replace));
		grid->setValue(64);
		ok &= expect(tests::settleModelViewport(*preview), "single vertex selection settles");
		handles = preview->moveGizmoPoints();
		const auto travel = preview->vertexScreenPosition(0, 1) - preview->vertexScreenPosition(0, 0);
		ok &= expect(preview->beginEditMove(handles[0]) && preview->updateEditMove(handles[0] + travel),
					 "prepare an invalid collapsed-face movement");
		preview->finishEditMove(true);
		ok &= expect(editableModelJson(editor->document().mesh()) == before && !editor->document().canUndo() &&
						 !editor->document().isModified() && !editor->findChild<QLabel *>("meshStatus")->text().isEmpty(),
					 "invalid gesture restores the source and reports validation without history pollution");
		editor->close();
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		if (scale == 200)
		{
			app.removeTranslator(&expansion);
		}
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
