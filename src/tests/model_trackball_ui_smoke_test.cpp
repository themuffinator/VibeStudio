#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/model_tags.h"
#include "core/model_transform_axes.h"
#include "core/model_recovery.h"
#include "core/studio_settings.h"
#include "tests/model_collision_animation_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_scale_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QJsonDocument>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
int checks = 0;
bool expect(bool value, const char *message, const QString &error = {})
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << ' ' << error.toStdString() << '\n';
	return value;
}
bool near(ModelVec3 a, ModelVec3 b)
{
	return std::hypot(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z) < .0003;
}
QByteArray bytes(const ModelMesh &mesh)
{
	return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact);
}
bool capture(QWidget &widget, const QString &name)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (root.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(root).filePath(QString("trackball-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).startsWith("VibeStudioModel") && !QByteArray(context).endsWith("ModelViewport"))
			return {};
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool gestures()
{
	bool ok = true;
	QString error;
	const auto original = tests::collisionAnimationFixture();
	for (bool perspective : {false, true})
		for (int kind = 0; kind < 5; ++kind)
			for (auto space : {ModelTransformSpace::World, ModelTransformSpace::Selection, ModelTransformSpace::Custom})
			{
				ModelViewport viewport;
				viewport.resize(800, 650);
				viewport.setMesh(original);
				viewport.setFrame(1);
				auto camera = viewport.cameraControls();
				camera.perspective = perspective;
				viewport.setCameraControls(camera);
				viewport.setOrbit(35, 25);
				ModelEdit edit;
				edit.frame = kind == 3 ? -1 : 1;
				edit.pivotFrame = 1;
				edit.transformSpace = space;
				edit.axesFrame = space == ModelTransformSpace::Selection ? 1 : -1;
				if (space == ModelTransformSpace::Custom)
					edit.axisRotation = {35, -28, 73};
				if (kind == 0)
				{
					edit.selection.vertices = {0, 1, 2};
					viewport.setEditSelection(0, edit.selection.vertices);
				}
				if (kind == 1)
				{
					edit.selection.surfaces = {0, 1};
					viewport.setEditSurfaces(0, edit.selection.surfaces);
				}
				if (kind == 2)
				{
					edit.kind = ModelEditKind::TransformTag;
					edit.selection.tag = "tag_reflected";
					viewport.setEditTag(edit.selection.tag);
					viewport.setShowTags(true);
				}
				if (kind >= 3)
				{
					edit.kind = ModelEditKind::TransformCollisionBox;
					edit.selection.collision = kind == 3 ? "static" : "body";
					viewport.setEditCollision(edit.selection.collision);
					viewport.setShowCollision(true);
				}
				ModelTransformBasis axes;
				if (!expect(resolveModelTransformAxes(original, edit, &axes, &error), "resolve gesture axes", error))
				{
					ok = false;
					continue;
				}
				viewport.setTransformAxes(axes, "Axes");
				viewport.setTransformPivot(space == ModelTransformSpace::World		 ? ModelTransformPivot::Origin
										   : space == ModelTransformSpace::Selection ? ModelTransformPivot::SelectionCentre
																					 : ModelTransformPivot::Custom,
										   {2, 3, 4});
				viewport.setTransformGizmo(true, ModelTransformTool::Rotate, 0, 15);
				viewport.show();
				ok &= expect(tests::settleModelViewport(viewport), "initial viewport settles");
				const auto bounds = viewport.trackballGizmoRect();
				const auto centre = bounds.center();
				if (!expect(!bounds.isEmpty() && bounds.width() == 172 && viewport.transformGizmoAt(centre) == 3,
							"screen-space sphere has a visible centre target in either camera"))
				{
					ok = false;
					continue;
				}
				for (int axis = 0; axis < 3; ++axis)
				{
					const auto ring = viewport.rotationGizmoRings()[axis];
					ok &= expect(std::any_of(ring.begin(), ring.end(), [&](QPointF p) { return viewport.transformGizmoAt(p) == axis; }),
								 "all three constrained rings remain pickable");
				}
				int freePoints = 0;
				for (int y = -50; y <= 50; y += 10)
					for (int x = -50; x <= 50; x += 10)
						if (std::hypot(x, y) > 10 && viewport.transformGizmoAt(centre + QPointF(x, y)) == 3)
							++freePoints;
				ok &= expect(freePoints > 10 && viewport.transformGizmoAt(centre + QPointF(300, 300)) == -1,
							 "free sphere interiors and outside misses are distinct");
				const auto end = centre + QPointF(54, -31);
				ok &= expect(viewport.beginEditTransform(centre) && viewport.updateEditTransform(end) && viewport.editTransformValid(),
							 "free rotation starts and updates through owned semantic calls");
				const auto transform = viewport.editTransform();
				ok &= expect(bytes(viewport.mesh()) == bytes(original) && viewport.accessibleSummary().contains("Free rotation preview"),
							 "preview changes no source and announces free rotation");
				QPolygonF screen;
				if (kind < 2)
					for (int v = 0; v < 4; ++v)
						screen << viewport.vertexScreenPosition(0, v);
				ModelTag tagPreview;
				if (kind == 2)
					ok &= expect(viewport.tagPose(edit.selection.tag, &tagPreview), "tag free preview resolves");
				ModelCollisionBox boxPreview;
				if (kind >= 3)
					ok &= expect(viewport.collisionPose(edit.selection.collision, &boxPreview), "collision free preview resolves");
				edit.rotation = transform.rotation;
				edit.pivot = transform.pivot;
				auto changed = original;
				ok &= expect(applyModelEdit(&changed, edit, nullptr, &error), "document validates free preview", error);
				int commits = 0;
				QObject::connect(&viewport, &ModelViewport::editTransformRequested, [&](const ModelTransform &value) {
					++commits;
					ok &= near(value.rotation, transform.rotation);
				});
				viewport.finishEditTransform(true);
				ok &= expect(commits == 1, "free gesture emits one validated commit");
				viewport.setMesh(changed, true);
				viewport.setFrame(1);
				if (kind < 2)
					for (int v = 0; v < 4; ++v)
						ok &= expect((viewport.vertexScreenPosition(0, v) - screen[v]).manhattanLength() < .02,
									 "committed geometry matches screen preview");
				if (kind == 2)
				{
					const auto tag = findModelTag(changed, edit.selection.tag, 1);
					ok &= expect(tag && near(tag->origin, tagPreview.origin) &&
									 std::equal(std::begin(tag->axis), std::end(tag->axis), std::begin(tagPreview.axis),
												[](float a, float b) { return std::abs(a - b) < .0003; }),
								 "reflected tag preview and committed axes agree");
				}
				if (kind >= 3)
				{
					ModelCollisionBox box;
					sampleModelCollisionBox(*findModelCollisionBox(changed, edit.selection.collision), 1, 1, 0, &box);
					const auto a = modelCollisionCorners(box), b = modelCollisionCorners(boxPreview);
					ok &= expect(std::equal(a.begin(), a.end(), b.begin(), [](auto x, auto y) { return near(x, y); }),
								 "static and animated collision previews match commits");
				}
			}
	ModelViewport viewport;
	viewport.resize(700, 600);
	viewport.setMesh(original);
	viewport.setEditSelection(0, {0, 1, 2});
	viewport.setTransformGizmo(true, ModelTransformTool::Rotate);
	viewport.show();
	const auto centre = viewport.trackballGizmoRect().center();
	int commits = 0;
	QObject::connect(&viewport, &ModelViewport::editTransformRequested, [&] { ++commits; });
	viewport.beginEditTransform(centre);
	viewport.updateEditTransform(centre + QPointF(2, 1));
	viewport.finishEditTransform(true);
	ok &= expect(commits == 0, "under-threshold gesture creates no edit");
	viewport.beginEditTransform(centre);
	viewport.updateEditTransform(centre + QPointF(40, 20));
	viewport.updateEditTransform(centre);
	viewport.finishEditTransform(true);
	ok &= expect(commits == 0, "returning to press position creates no edit");
	viewport.beginEditTransform(centre);
	viewport.updateEditTransform(centre + QPointF(40, 20));
	ok &= expect(!viewport.updateEditTransform({std::numeric_limits<double>::quiet_NaN(), 0}) && !viewport.editTransformValid(),
				 "nonfinite drag invalidates current gesture");
	viewport.finishEditTransform(true);
	ok &= expect(commits == 0, "invalid release cancels without stale commit");
	viewport.beginEditTransform(centre);
	viewport.updateEditTransform({std::numeric_limits<double>::infinity(), 0});
	ok &= expect(viewport.updateEditTransform(centre + QPointF(40, 20)) && viewport.editTransformValid(),
				 "valid position recovers after rejected sample");
	viewport.finishEditTransform(false);
	ok &= expect(commits == 0, "explicit cancel retains source");
	viewport.beginEditTransform(centre);
	viewport.updateEditTransform(centre + QPointF(40, 20));
	viewport.setFrame(1);
	ok &= expect(!viewport.editingMove() && commits == 0, "frame change cancels free preview");
	viewport.beginEditTransform(viewport.trackballGizmoRect().center());
	viewport.setTransformAxes({}, "changed");
	ok &= expect(!viewport.editingMove() && commits == 0, "axes change cancels free preview");
	viewport.beginEditTransform(viewport.trackballGizmoRect().center());
	viewport.setOrbit(18, 32);
	ok &= expect(!viewport.editingMove() && commits == 0, "camera change cancels free preview");
	viewport.beginEditTransform(viewport.trackballGizmoRect().center());
	viewport.resize(720, 620);
	ok &= expect(!viewport.editingMove() && commits == 0, "resize cancels free preview");
	viewport.setTransformGizmo(false, ModelTransformTool::Rotate);
	ok &= expect(viewport.trackballGizmoRect().isEmpty(), "ordinary model and level browsing exposes no free edit handle");
	return ok;
}
bool editorScenario(QApplication &app, int scenario)
{
	Expansion expansion;
	if (scenario == 2)
		app.installTranslator(&expansion);
	applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
											: scenario == 1 ? StudioTheme::HighContrastDark
															: StudioTheme::HighContrastLight,
											UiDensity::Standard, scenario ? 200 : 100));
	app.setLayoutDirection(scenario == 2 ? Qt::RightToLeft : Qt::LeftToRight);
	const auto original = tests::collisionAnimationFixture();
	ModelEditorDialog editor;
	editor.setAccessibility(scenario > 0, true);
	editor.resize(scenario ? 2200 : 1440, scenario ? 1500 : 1050);
	QString error;
	bool ok = expect(editor.setMesh(original, &error), "open editor fixture", error);
	editor.show();
	app.processEvents();
	auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
	auto *tool = editor.findChild<QComboBox *>("meshTransformTool");
	auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
	auto *frame = editor.findChild<QComboBox *>("meshFrame");
	auto *scope = editor.findChild<QComboBox *>("meshFrameScope");
	auto *axes = editor.findChild<QComboBox *>("meshTransformSpace");
	auto *table = editor.findChild<QTableView *>("meshComponents");
	auto *snap = editor.findChild<QCheckBox *>("meshSnapTranslation");
	auto *undo = editor.findChild<QAction *>("undoMesh"), *redo = editor.findChild<QAction *>("redoMesh");
	if (!expect(viewport && tool && mode && frame && scope && axes && table && snap && undo && redo, "editor transform controls exist"))
		return false;
	tool->setCurrentIndex(1);
	snap->setChecked(true);
	for (auto *tabs : editor.findChildren<QTabWidget *>())
		for (int i = 0; i < tabs->count(); ++i)
			if (tabs->widget(i)->isAncestorOf(axes))
				tabs->setCurrentIndex(i);
	axes->setCurrentIndex(scenario);
	frame->setCurrentIndex(1);
	if (scenario == 2)
		editor.findChild<QDoubleSpinBox *>("meshAxisRotation1")->setValue(35);
	for (int kind = 0; kind < 5; ++kind)
		for (int poseScope = 0; poseScope < (kind == 3 ? 1 : 2); ++poseScope)
		{
			mode->setCurrentIndex(kind == 0 ? 0 : kind == 1 ? 5 : kind == 2 ? 3 : 4);
			if (kind < 2)
				table->selectAll();
			if (kind == 2)
				viewport->tagPicked("tag_mount", int(ModelViewportPick::Replace));
			if (kind >= 3)
				viewport->collisionPicked(kind == 3 ? "static" : "body", int(ModelViewportPick::Replace));
			scope->setCurrentIndex(poseScope);
			viewport->frameModel();
			if (!expect(tests::settleModelViewport(*viewport), "selected editor pose settles"))
			{
				ok = false;
				continue;
			}
			const auto selection = editor.document().selection();
			const auto centre = viewport->trackballGizmoRect().center();
			ok &= expect(viewport->beginEditTransform(centre) && viewport->updateEditTransform(centre + QPointF(54, -31)),
						 "editor free gesture starts and previews");
			const auto transform = viewport->editTransform();
			ModelEdit edit;
			edit.selection = selection;
			edit.pivot = transform.pivot;
			edit.rotation = transform.rotation;
			edit.frame = kind == 3 || poseScope == 0 ? -1 : 1;
			edit.pivotFrame = 1;
			edit.kind = kind < 2	? ModelEditKind::Transform
						: kind == 2 ? ModelEditKind::TransformTag
									: ModelEditKind::TransformCollisionBox;
			edit.transformSpace = ModelTransformSpace(scenario);
			edit.axesFrame = scenario == 1 ? 1 : -1;
			if (scenario == 2)
				edit.axisRotation = {0, 35, 0};
			auto expected = original;
			ok &= expect(applyModelEdit(&expected, edit, nullptr, &error), "calculate shared expected commit", error);
			if (poseScope == 0 && (kind == 0 || kind == 2 || kind == 4))
				ok &= expect(tests::settleModelViewport(*viewport) && capture(*viewport, QString("%1-%2-preview").arg(scenario).arg(kind)),
							 "render free geometry, tag and animated collision previews");
			viewport->finishEditTransform(true);
			ok &=
				expect(bytes(editor.document().mesh()) == bytes(expected), "GUI commits exactly the preview without second Euler snapping");
			undo->trigger();
			ok &= expect(bytes(editor.document().mesh()) == bytes(original) && !editor.document().canUndo(),
						 "GUI gesture undoes in one step");
			redo->trigger();
			ok &= expect(bytes(editor.document().mesh()) == bytes(expected), "GUI redo restores complete result");
			undo->trigger();
		}
	mode->setCurrentIndex(0);
	table->selectAll();
	ok &= expect(tests::settleModelViewport(*viewport) && capture(editor, QString("%1-controls").arg(scenario)),
				 "render rotate controls and accessible gizmo");
	const auto accessible = QAccessible::queryAccessibleInterface(tool);
	ok &= expect(accessible && accessible->role() == QAccessible::ComboBox && !accessible->text(QAccessible::Name).isEmpty() &&
					 tool->toolTip().contains("trackball") && viewport->accessibleDescription().contains("trackball") &&
					 tool->focusPolicy() != Qt::NoFocus,
				 "free rotation has accessible help and existing keyboard numeric alternative");
	tool->setFocus(Qt::OtherFocusReason);
	ok &= expect(tool->hasFocus(), "transform tool remains focusable at expanded text sizes");
	for (auto *scroll : editor.findChildren<QScrollArea *>())
		if (scroll->isAncestorOf(axes))
			ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, "expanded geometry controls avoid horizontal clipping");
	if (scenario == 2)
		app.removeTranslator(&expansion);
	if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
		ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "actual 2x device scaling");
	return ok;
}
bool maximum(QApplication &app)
{
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	app.setLayoutDirection(Qt::LeftToRight);
	const auto mesh = tests::maximumEditableGrid();
	ModelEditorDialog editor;
	editor.setAccessibility(false, true);
	editor.resize(1600, 1000);
	editor.show();
	app.processEvents();
	QElapsedTimer elapsed;
	elapsed.start();
	qint64 last = elapsed.nsecsElapsed(), maximumGap = 0;
	int events = 0;
	QTimer tick;
	QObject::connect(&tick, &QTimer::timeout, &editor, [&] {
		const auto now = elapsed.nsecsElapsed();
		maximumGap = std::max(maximumGap, now - last);
		last = now;
		++events;
	});
	tick.start(1);
	QString error;
	bool ok = expect(editor.setMesh(mesh, &error), "open maximum frame-vertex grid", error);
	const auto before = editor.document().revisionFingerprint();
	editor.findChild<QComboBox *>("meshSelectionMode")->setCurrentIndex(5);
	editor.findChild<QTableView *>("meshComponents")->selectAll();
	editor.findChild<QComboBox *>("meshTransformTool")->setCurrentIndex(1);
	auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
	ok &= expect(tests::settleModelViewport(*viewport), "maximum source finishes rendering");
	const auto centre = viewport->trackballGizmoRect().center();
	ok &= expect(viewport->beginEditTransform(centre), "maximum geometry starts free rotation");
	for (int i = 1; i <= 32; ++i)
	{
		ok &= expect(viewport->updateEditTransform(centre + QPointF(i * 1.6, -i * .9)), "maximum geometry accepts coalesced free updates");
		app.processEvents();
	}
	const auto transform = viewport->editTransform();
	ok &= expect(tests::settleModelViewport(*viewport) && editor.document().revisionFingerprint() == before,
				 "maximum preview leaves source unchanged");
	viewport->finishEditTransform(true);
	ok &= expect(editor.document().revisionFingerprint() != before, "maximum all-frame rotation commits");
	for (int f : {0, 15})
		ok &= expect(near(editor.document().mesh().surfaces[0].frames[f].positions[0],
						  transformModelPoint(mesh.surfaces[0].frames[f].positions[0], transform)),
					 "maximum commit retains exact preview transform across all frames");
	editor.findChild<QAction *>("undoMesh")->trigger();
	ok &= expect(editor.document().revisionFingerprint() == before && !editor.document().canUndo() && tests::settleModelViewport(*viewport),
				 "maximum free edit undoes completely");
	app.processEvents();
	maximumGap = std::max(maximumGap, elapsed.nsecsElapsed() - last);
	tick.stop();
	const double gapMs = maximumGap / 1e6, budget = qEnvironmentVariable("VIBESTUDIO_MODELLER_MAX_AUTHORING_GAP_MS").toDouble();
	ok &= expect(events >= 2 && (budget <= 0 || gapMs <= budget), "maximum free authoring remains within GUI event-gap budget");
	std::cout << "Maximum trackball: 65536 vertices, 130050 faces, 16 poses; elapsed ms=" << elapsed.elapsed() << "; events=" << events
			  << "; max gap ms=" << gapMs << '\n';
	ok &= expect(capture(*viewport, "maximum-restored"), "render restored maximum geometry");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	// Owned widget methods/signals and render targets only. No OS input/capture.
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
	QTemporaryDir temporary(QDir(root).filePath("trackball-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	StudioSettings().setModelRecoveryEnabled(false);
	bool ok = gestures();
	for (int scenario = 0; scenario < 3; ++scenario)
		ok &= editorScenario(app, scenario);
	ok &= maximum(app);
	std::cout << checks << " trackball GUI checks\n";
	return ok ? 0 : 1;
}
