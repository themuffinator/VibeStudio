#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "core/model_transform_axes.h"
#include "tests/model_transform_axes_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFont>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value) std::cerr << "FAIL: " << message << '\n';
	return value;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor") return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool render(QWidget &widget, int scenario, const QString &phase)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty()) return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF()); image.fill(Qt::transparent); widget.render(&image);
	return image.save(QDir(directory).filePath(QString("transform-axes-%1-%2-%3x.png").arg(phase).arg(scenario).arg(widget.devicePixelRatioF())));
}
}
int main(int argc, char **argv)
{
	// Test-owned semantic gestures, widget signals and render targets only.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root)) return 1;
	QTemporaryDir temporary(QDir(root).filePath("transform-axes-ui-XXXXXX"));
	if (!temporary.isValid()) return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	const auto original = transformAxesFixture();
	ModelEdit reference; reference.selection.vertices = {0, 1, 2, 3}; reference.transformSpace = ModelTransformSpace::Selection;
	ModelTransformBasis basis;
	ok &= expect(resolveModelTransformAxes(original, reference, &basis, &error), "resolve original fixture axes");
	for (bool perspective : {false, true})
	{
		ModelViewport viewport; viewport.resize(680, 580); viewport.setMesh(original); viewport.setOrbit(32, 22);
		auto camera = viewport.cameraControls(); camera.perspective = perspective; viewport.setCameraControls(camera);
		viewport.setEditSelection(0, {0, 1, 2, 3}); viewport.setTransformAxes(basis, "Selection"); viewport.show();
		ModelVec3 legacyDelta;
		QObject::connect(&viewport, &ModelViewport::editMoveRequested, [&](double x, double y, double z) { legacyDelta = {float(x), float(y), float(z)}; });
		for (auto tool : {ModelTransformTool::Move, ModelTransformTool::Rotate, ModelTransformTool::Scale})
		{
			viewport.setTransformGizmo(true, tool, 1, 15, .25);
			for (int axis = 0; axis < 3; ++axis)
			{
				ok &= expect(settleModelViewport(viewport), "oriented gesture source settles");
				const auto handles = viewport.transformGizmoPoints();
				QPointF start = handles[axis], end = start + (start - handles[3]) * .75;
				if (tool == ModelTransformTool::Rotate)
				{
					const auto ring = viewport.rotationGizmoRings()[axis];
					int step = -1;
					for (int i = 0; i < 96 && ring.size() == 97; ++i)
						if (viewport.transformGizmoAt(ring[i]) == axis && (ring[(i + 16) % 96] - ring[i]).manhattanLength() > 20) { step = i; break; }
					if (!expect(step >= 0, "rotated ring has a distinct pickable segment")) { ok = false; continue; }
					start = ring[step]; end = ring[(step + 16) % 96];
				}
				if (!expect(viewport.transformGizmoAt(start) == axis && viewport.beginEditTransform(start) && viewport.updateEditTransform(end), "rotated handle starts and updates through semantic API")) { ok = false; continue; }
				const auto transform = viewport.editTransform();
				ok &= expect(!transform.basis.world && nearAxesVectors(transform.basis.axes, basis.axes), "gesture captures exact fixed axes");
				if (tool == ModelTransformTool::Rotate)
				{
					const float angles[]{transform.rotation.x, transform.rotation.y, transform.rotation.z};
					ok &= expect(std::abs(angles[axis] - 60) < .01, "oriented ring measures local geometric angle");
				}
				if (tool == ModelTransformTool::Scale)
				{
					const float scales[]{transform.scale.x, transform.scale.y, transform.scale.z};
					ok &= expect(perspective ? scales[axis] > 1 : std::abs(scales[axis] - 1.75) < .01, "oriented scale handle measures axis distance");
				}
				QPolygonF preview;
				for (int vertex = 0; vertex < 4; ++vertex) preview << viewport.vertexScreenPosition(0, vertex);
				auto mesh = original; auto edit = reference;
				edit.translation = transform.translation; edit.rotation = transform.rotation; edit.scale = transform.scale; edit.pivot = transform.pivot;
				ok &= expect(applyModelEdit(&mesh, edit, nullptr, &error), "core validates oriented gesture");
				viewport.finishEditTransform(true);
				if (tool == ModelTransformTool::Move)
					ok &= expect(nearAxesVector(legacyDelta, {transform.translation.z, transform.translation.x, transform.translation.y}), "legacy move signal retains world-coordinate delta");
				viewport.setMesh(mesh, true);
				for (int vertex = 0; vertex < 4; ++vertex)
					ok &= expect((viewport.vertexScreenPosition(0, vertex) - preview[vertex]).manhattanLength() < .01, "committed geometry matches oriented preview");
				viewport.setMesh(original, true); viewport.setEditSelection(0, {0, 1, 2, 3});
			}
		}
		viewport.setTransformGizmo(true, ModelTransformTool::Move);
		const auto handles = viewport.transformGizmoPoints();
		ok &= expect(viewport.beginEditTransform(handles[0]) && viewport.updateEditTransform(handles[0] + (handles[0] - handles[3])), "start before axes change");
		viewport.setTransformAxes({}, "World");
		ok &= expect(!viewport.editingMove(), "changing axes cancels active preview");
		viewport.setTransformAxes({}, "Selection", false);
		ok &= expect(!std::isfinite(viewport.transformGizmoPoints()[3].x()) && viewport.accessibleDescription().contains("unavailable"), "unavailable basis hides handles and explains recovery");
	}
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion; if (scenario) app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0 ? StudioTheme::Dark : scenario == 1 ? StudioTheme::HighContrastLight : StudioTheme::HighContrastDark,
			UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelEditorDialog editor; editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open animated axes fixture");
		editor.resize(scenario ? 2250 : 1400, scenario ? 1600 : 1100); editor.show(); app.processEvents();
		auto *space = editor.findChild<QComboBox *>("meshTransformSpace");
		auto *axesRow = editor.findChild<QWidget *>("meshCustomAxesFields");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *viewport = editor.findChild<ModelViewport *>("meshPreview");
		auto *apply = editor.findChild<QPushButton *>("transformMesh");
		auto *frame = editor.findChild<QComboBox *>("meshFrame");
		auto *scope = editor.findChild<QComboBox *>("meshFrameScope");
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *tool = editor.findChild<QComboBox *>("meshTransformTool");
		auto *snap = editor.findChild<QCheckBox *>("meshSnapTranslation");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		if (!expect(space && axesRow && table && viewport && apply && frame && scope && mode && tool && snap && undo, "axes controls exist")) return 1;
		snap->setChecked(false);
		for (auto *tabs : editor.findChildren<QTabWidget *>())
			for (int i = 0; i < tabs->count(); ++i) if (tabs->widget(i)->isAncestorOf(space)) tabs->setCurrentIndex(i);
		table->selectAll();
		ok &= expect(space->currentIndex() == 0 && !axesRow->isVisible(), "world default hides custom orientation");
		space->setCurrentIndex(1);
		auto *offset = editor.findChild<QDoubleSpinBox *>("meshOffset0");
		offset->setValue(2); apply->click();
		ok &= expect(nearAxesVector(editor.document().mesh().surfaces[0].frames[0].positions[0], {3, 6, 5}) &&
			nearAxesVector(editor.document().mesh().surfaces[0].frames[1].positions[0], {23, 6, 5}), "numeric selection offset uses displayed axes in both poses");
		undo->trigger();
		ok &= expect(axesMeshBytes(editor.document().mesh()) == axesMeshBytes(original) && !editor.document().canUndo(), "numeric axes edit undoes in one step");
		ok &= expect(settleModelViewport(*viewport) && render(editor, scenario, "selection"), "render selection axes and geometry controls");
		auto handles = viewport->transformGizmoPoints();
		ok &= expect(viewport->beginEditTransform(handles[0]) && viewport->updateEditTransform(handles[0] + (handles[0] - handles[3]) * .65), "editor starts oriented move gesture");
		const auto move = viewport->editTransform(); viewport->finishEditTransform(true);
		ok &= expect(nearAxesVector(editor.document().mesh().surfaces[0].frames[1].positions[0], {23, 4 + move.translation.x, 5}), "editor gesture commit retains fixed axes across poses");
		undo->trigger();
		frame->setCurrentIndex(1); scope->setCurrentIndex(1); apply->click();
		ok &= expect(nearAxesVector(editor.document().mesh().surfaces[0].frames[1].positions[0], {25, 4, 5}) &&
			nearAxesVectors(editor.document().mesh().surfaces[0].frames[0].positions, original.surfaces[0].frames[0].positions), "displayed reference refreshes and current-pose scope is respected");
		undo->trigger(); frame->setCurrentIndex(0); scope->setCurrentIndex(0);
		{
			// Pause the test-owned timer signal so exact seeks can exercise live
			// playback frame changes without wall-clock or pointer input.
			auto *timer = viewport->findChild<QTimer *>("modelPlaybackTimer");
			if (!expect(timer != nullptr, "playback timer exists")) return 1;
			QSignalBlocker pausedClock(timer);
			viewport->setReducedMotion(false); viewport->setAnimationInterpolation(false); viewport->setFramesPerSecond(1);
			viewport->play();
			ok &= expect(viewport->isPlaying() && viewport->seekAnimation(1), "seek exact second pose during discrete playback");
			const auto playingHandles = viewport->transformGizmoPoints();
			viewport->pause();
			ok &= expect(viewport->transformGizmoPoints() == playingHandles, "visible selection axes follow the displayed pose before playback pauses");
			frame->setCurrentIndex(0); viewport->setReducedMotion(true);
		}
		space->setCurrentIndex(2);
		QList<QDoubleSpinBox *> angles;
		for (int axis = 0; axis < 3; ++axis) angles << editor.findChild<QDoubleSpinBox *>(QString("meshAxisRotation%1").arg(axis));
		if (!expect(std::all_of(angles.begin(), angles.end(), [](auto *field) { return field; }), "custom XYZ controls exist")) return 1;
		angles[0]->setValue(90); angles[2]->setValue(90);
		ok &= expect(axesRow->isVisible() && angles[0]->isEnabled(), "custom controls appear progressively");
		for (QWidget *control : QList<QWidget *>{space, angles[0], angles[1], angles[2]})
		{
			auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !accessible->text(QAccessible::Description).isEmpty() && control->focusPolicy() != Qt::NoFocus, "axes controls expose help, names and keyboard focus");
		}
		angles[0]->setFocus(Qt::OtherFocusReason);
		ok &= expect(angles[0]->hasFocus() && angles[0]->layoutDirection() == Qt::LeftToRight, "custom angle field retains numeric direction and focus in RTL");
		for (auto *scroll : editor.findChildren<QScrollArea *>())
		{
			if (!scroll->isAncestorOf(space)) continue;
			scroll->ensureWidgetVisible(space, 0, 0); app.processEvents();
			ok &= expect(scroll->horizontalScrollBar()->maximum() == 0 && space->width() >= space->minimumSizeHint().width() &&
				axesRow->width() >= axesRow->minimumSizeHint().width(), "expanded axes controls fit without horizontal scrolling");
		}
		editor.findChild<QDoubleSpinBox *>("meshRotation2")->setValue(60);
		editor.findChild<QDoubleSpinBox *>("meshScale0")->setValue(1.5);
		ModelEdit expectedEdit; expectedEdit.selection = editor.document().selection(); expectedEdit.transformSpace = ModelTransformSpace::Custom;
		expectedEdit.axisRotation = {90, 0, 90}; expectedEdit.rotation = {0, 0, 60}; expectedEdit.scale = {1.5, 1, 1};
		expectedEdit.translation = {2, 0, 0}; expectedEdit.pivotMode = ModelTransformPivot::SelectionCentre;
		auto expected = original;
		ok &= expect(applyModelEdit(&expected, expectedEdit, nullptr, &error), "core resolves custom numeric controls");
		apply->click();
		ok &= expect(axesMeshBytes(editor.document().mesh()) == axesMeshBytes(expected), "GUI custom transform matches core exactly");
		tool->setCurrentIndex(1);
		ok &= expect(settleModelViewport(*viewport) && render(editor, scenario, "custom"), "render custom axes after compound transform");
		undo->trigger();
		tool->setCurrentIndex(0); handles = viewport->transformGizmoPoints();
		ok &= expect(viewport->beginEditTransform(handles[0]), "begin before custom axes edit");
		angles[1]->setValue(30);
		ok &= expect(!viewport->editingMove() && !editor.document().canUndo(), "custom field change cancels preview without document history");
		mode->setCurrentIndex(3); space->setCurrentIndex(1); viewport->tagPicked("tag_mount", int(ModelViewportPick::Replace));
		ok &= expect(viewport->beginEditTransform(viewport->transformGizmoPoints()[0]) && nearAxesVectors(viewport->editTransform().basis.axes, basis.axes), "tag selection supplies native axes to the viewport");
		viewport->finishEditTransform(false);
		mode->setCurrentIndex(4); viewport->collisionPicked("body", int(ModelViewportPick::Replace)); tool->setCurrentIndex(2);
		const auto localHandles = viewport->transformGizmoPoints(); space->setCurrentIndex(2);
		ok &= expect(viewport->transformGizmoPoints() == localHandles, "collision scale handles remain box-local under custom axes");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2") ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "actual 2x display scaling");
		editor.setMesh(original, &error); editor.hide(); if (scenario) app.removeTranslator(&expansion);
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok) std::cerr << error.toStdString() << '\n';
	std::cout << checks << " transform axes GUI checks\n";
	return ok ? 0 : 1;
}
