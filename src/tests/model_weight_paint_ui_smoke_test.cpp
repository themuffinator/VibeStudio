#include "app/model_editor_dialog.h"
#include "app/model_vertex_overlay.h"
#include "app/model_weight_paint_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_rig_test_helpers.h"
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
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *label)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << label << '\n';
	}
	return value;
}
bool close(double a, double b)
{
	return std::abs(a - b) < .0001;
}
bool idle(ModelWeightPaintDialog &dialog)
{
	QElapsedTimer time;
	time.start();
	while (dialog.busy() && time.elapsed() < 20000)
	{
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return !dialog.busy();
}
bool render(QWidget &widget, const QString &name)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (root.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(root).filePath(QString("weight-paint-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
void select(QTableView *table, const QSet<int> &rows)
{
	table->selectionModel()->clearSelection();
	for (int row : rows)
		table->selectionModel()->select(table->model()->index(row, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
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
		if (QByteArray(context) != "ModelWeightPaintDialog")
			return {};
		const auto text = QString::fromUtf8(source);
		return QString("[%1 %2]").arg(text, QString("~~ ").repeated(text.size() / 6));
	}
};
} // namespace
int main(int argc, char **argv)
{
	// Only owned semantic Qt APIs and widget render targets; no OS input.
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
	QTemporaryDir temporary(QDir(root).filePath("weight-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("weight painting UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = rigFixture();
	const auto original = rigBytes(mesh);
	QString error;
	ModelSelection selection;
	selection.vertices = {0};
	{
		ModelWeightPaintDialog cancelled(mesh, selection, "tip", 0);
		cancelled.show(); cancelled.reject();
		expect(idle(cancelled) && !cancelled.isVisible() && cancelled.stagedMesh().surfaces.isEmpty(),
			   "cancelling queued preparation closes without starting document work");
	}
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
		ModelWeightPaintDialog dialog(mesh, selection, "tip", 2);
		dialog.resize(1140, 960);
		dialog.show();
		if (!expect(idle(dialog), "initial document preparation completes"))
			return 1;
		auto *preview = dialog.findChild<ModelViewport *>("weightPaintPreview");
		auto *table = dialog.findChild<QTableView *>("weightPaintTable");
		auto *value = dialog.findChild<QDoubleSpinBox *>("weightPaintValue");
		auto *strength = dialog.findChild<QDoubleSpinBox *>("weightPaintStrength");
		auto *mask = dialog.findChild<QCheckBox *>("weightPaintSelectionOnly");
		auto *apply = dialog.findChild<QPushButton *>("weightPaintApply");
		if (!expect(preview && table && value && strength && mask && apply, "paint controls present"))
			return 1;
		expect(mask->isChecked() && table->selectionModel()->selectedRows().size() == 1, "editor selection becomes explicit brush mask");
		for (const char *name : {"weightPaintSurface", "weightPaintJoint", "weightPaintMode", "weightPaintValue", "weightPaintStrength",
								 "weightPaintRadius", "weightPaintLocks", "weightPaintTable", "weightPaintSelected", "weightPaintApply"})
		{
			auto *control = dialog.findChild<QWidget *>(name);
			expect(control && !control->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(control) &&
					   (control->focusPolicy() & Qt::TabFocus),
				   "brush control exposes native accessibility and keyboard focus");
		}
		preview->setOrbit(0, 90);
		strength->setValue(1);
		value->setValue(.6);
		dialog.findChild<QSpinBox *>("weightPaintRadius")->setValue(16);
		expect(settleModelViewport(*preview), "initial paint projection completes");
		const auto p = preview->vertexScreenPosition(0, 0);
		expect(dialog.beginStroke(p), "brush begins from exact render projection");
		for (int i = 0; i < 100; ++i)
			dialog.extendStroke(p);
		expect(rigBytes(dialog.stagedMesh()) == original && !apply->isEnabled(),
			   "pending coverage leaves staged model unchanged and cannot apply");
		expect(render(dialog, QString("pending-%1").arg(scenario)), "render brush coverage and radius");
		expect(dialog.finishStroke(true), "released stroke reviews through worker");
		if (dialog.edit().weightStrokes.isEmpty())
			std::cerr << dialog.findChild<QLabel *>("weightPaintStatus")->text().toStdString() << '\n';
		expect(close(modelVertexJointWeight(dialog.stagedMesh().surfaces[0], 0, 1), .6) &&
				   close(modelVertexJointWeight(dialog.stagedMesh().surfaces[0], 1, 1), 0),
			   "mask and repeated pointer points preserve deterministic coverage");
		expect(dialog.edit().weightStrokes.size() == 1 && apply->isEnabled() && rigBytes(mesh) == original,
			   "one reviewed stroke leaves original source unchanged");
		expect(close(table->model()->data(table->model()->index(0, 1)).toString().toDouble(), .6),
			   "exact weight table updates after review");
		const auto painted = rigBytes(dialog.stagedMesh());
		auto replay = mesh;
		expect(applyModelEdit(&replay, dialog.edit(), nullptr, &error) && rigBytes(replay) == painted,
			   "GUI batch replays through shared core exactly");
		expect(settleModelViewport(*preview) && render(dialog, QString("reviewed-%1").arg(scenario)), "render posed result");
		dialog.undoStroke();
		expect(rigBytes(dialog.stagedMesh()) == original && !apply->isEnabled(), "local undo restores source and retires apply");
		dialog.redoStroke();
		expect(rigBytes(dialog.stagedMesh()) == painted && dialog.edit().weightStrokes.size() == 1, "local redo restores exact batch");
		expect(settleModelViewport(*preview), "redo projection ready");
		expect(dialog.beginStroke(preview->vertexScreenPosition(0, 0)) && !dialog.finishStroke(false) &&
				   rigBytes(dialog.stagedMesh()) == painted,
			   "explicit cancellation is atomic");
		expect(dialog.beginStroke(preview->vertexScreenPosition(0, 0)), "begin stroke before frame change");
		dialog.findChild<QComboBox *>("weightPaintFrame")->setCurrentIndex(0);
		expect(!dialog.strokeActive() && rigBytes(dialog.stagedMesh()) == painted, "frame change cancels pending coverage");
		expect(settleModelViewport(*preview), "bind preview ready");
		const bool noopBegan = dialog.beginStroke(preview->vertexScreenPosition(0, 0));
		const bool noopDone = noopBegan && dialog.finishStroke(true);
		if (!expect(noopDone && dialog.edit().weightStrokes.size() == 1, "no-op stroke does not desynchronise undo recipe"))
			std::cerr << "Noop: began=" << noopBegan << " done=" << noopDone << " strokes=" << dialog.edit().weightStrokes.size()
					  << " status=" << dialog.findChild<QLabel *>("weightPaintStatus")->text().toStdString() << '\n';
		mask->setChecked(false);
		value->setValue(.3);
		dialog.findChild<QSpinBox *>("weightPaintRadius")->setValue(3);
		expect(settleModelViewport(*preview), "continuous stroke projection ready");
		const auto first = preview->vertexScreenPosition(0, 0), last = preview->vertexScreenPosition(0, 2);
		expect(dialog.beginStroke(first) && dialog.extendStroke(last) && dialog.finishStroke(true),
			   "long pointer segment paints continuous coverage");
		expect(close(modelVertexJointWeight(dialog.stagedMesh().surfaces[0], 2, 1), .3), "segment endpoint receives full coverage");
		select(table, {1});
		value->setValue(.75);
		expect(dialog.paintSelected() && close(modelVertexJointWeight(dialog.stagedMesh().surfaces[0], 1, 1), .75),
			   "numeric row painting works without pointer actions");
		const auto numeric = rigBytes(dialog.stagedMesh());
		mask->setChecked(true);
		select(table, {});
		expect(settleModelViewport(*preview), "empty mask projection ready");
		expect(dialog.beginStroke(preview->vertexScreenPosition(0, 0)) && !dialog.finishStroke(true) &&
				   rigBytes(dialog.stagedMesh()) == numeric,
			   "empty mask paints nothing");
		select(table, {0});
		auto *locks = dialog.findChild<QListWidget *>("weightPaintLocks");
		locks->item(1)->setCheckState(Qt::Checked);
		expect(!dialog.paintSelected() && rigBytes(dialog.stagedMesh()) == numeric,
			   "locked painted joint fails without losing reviewed strokes");
		locks->item(1)->setCheckState(Qt::Unchecked);
		expect(settleModelViewport(*preview) && render(dialog, QString("numeric-%1").arg(scenario)), "render table and staged weights");
		auto *scroll = dialog.findChild<QScrollArea *>();
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(scroll->horizontalScrollBar()->maximum() == 0, "settings fit scaled expanded labels without horizontal scrolling");
		bool labelsFit = true;
		for (auto *label : scroll->widget()->findChildren<QLabel *>())
			if (label->wordWrap() && label->heightForWidth(label->width()) > label->height() + 1)
			{
				labelsFit = false;
				std::cerr << "Wrapped label needs " << label->heightForWidth(label->width()) << " pixels, has " << label->height()
						  << ": " << label->text().toStdString() << '\n';
			}
		expect(labelsFit, "wrapped settings retain their complete label height");
		expect(render(dialog, QString("settings-%1").arg(scenario)), "render locks and keyboard alternative");
		expect(dialog.beginStroke(preview->vertexScreenPosition(0, 0)), "begin stroke before dialog cancellation");
		dialog.reject();
		expect(!dialog.strokeActive() && dialog.isVisible() && rigBytes(dialog.stagedMesh()) == numeric,
			   "first Escape-style rejection cancels only active stroke");
		dialog.reject();
		expect(!dialog.isVisible() && rigBytes(mesh) == original, "cancel discards review without source writes");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		ModelWeightPaintDialog dialog(mesh, selection, "tip", 0);
		dialog.show();
		expect(idle(dialog), "falloff fixture prepares");
		auto *preview = dialog.findChild<ModelViewport *>("weightPaintPreview");
		preview->setOrbit(0, 90);
		dialog.findChild<QDoubleSpinBox *>("weightPaintStrength")->setValue(1);
		dialog.findChild<QDoubleSpinBox *>("weightPaintValue")->setValue(.8);
		dialog.findChild<QSpinBox *>("weightPaintRadius")->setValue(40);
		for (int falloff = 0; falloff < 3; ++falloff)
		{
			dialog.findChild<QComboBox *>("weightPaintFalloff")->setCurrentIndex(falloff);
			expect(settleModelViewport(*preview), "falloff projection ready");
			const auto p = preview->vertexScreenPosition(0, 0) + QPointF(10, 0);
			expect(dialog.beginStroke(p) && dialog.finishStroke(true), "off-centre brush stroke commits");
			const double opacity = falloff == 0 ? .84375 : falloff == 1 ? .75 : 1;
			expect(close(modelVertexJointWeight(dialog.stagedMesh().surfaces[0], 0, 1), .8 * opacity), "falloff gives analytic coverage");
			dialog.undoStroke();
		}
		auto *table = dialog.findChild<QTableView *>("weightPaintTable");
		select(table, {0});
		QTimer::singleShot(0, &dialog, [&] { dialog.reject(); });
		expect(!dialog.paintSelected() && !dialog.isVisible() && rigBytes(dialog.stagedMesh()) == original,
			   "closing during worker review cancels and joins before rejecting");
	}
	{
		auto occluded = mesh;
		auto &surface = occluded.surfaces[0];
		surface.vertexCount = 4;
		surface.texCoords.append({.25f, .25f});
		for (auto &geometry : surface.frames)
		{
			geometry.positions.append({2, 2, -2});
			geometry.normals.append({0, 0, 1});
		}
		expect(modelSkinningFromBindPose(occluded.skeleton, surface.frames[0].positions, surface.frames[0].normals, {0, 0, 0, 0},
										 {1, 1, 1, 1}, 1, &surface.skinning) &&
				   bakeModelSkeleton(&occluded),
			   "occluded vertex fixture binds");
		updateEditableModelMetadata(&occluded);
		ModelWeightPaintDialog dialog(occluded, {}, "tip", 0);
		dialog.show();
		expect(idle(dialog), "occlusion fixture prepares");
		auto *preview = dialog.findChild<ModelViewport *>("weightPaintPreview");
		preview->setOrbit(0, 90);
		dialog.findChild<QSpinBox *>("weightPaintRadius")->setValue(4);
		expect(settleModelViewport(*preview), "occlusion projection completes");
		const auto projection = preview->editVertexProjection();
		expect(projection && !projection->visible.testBit(3), "opaque triangle occludes the interior vertex");
		const auto point = preview->vertexScreenPosition(0, 3);
		expect(dialog.beginStroke(point) && !dialog.finishStroke(true) && dialog.edit().weightStrokes.isEmpty(),
			   "visible-only brush excludes hidden vertices");
		dialog.findChild<QCheckBox *>("weightPaintThrough")->setChecked(true);
		expect(dialog.beginStroke(point) && dialog.finishStroke(true) &&
				   close(modelVertexJointWeight(dialog.stagedMesh().surfaces[0], 3, 1), .5),
			   "through brush reaches hidden vertex explicitly");
		expect(settleModelViewport(*preview) && render(dialog, "through"), "render explicit through-mesh weights");
	}
	{
		ModelEditorDialog editor;
		expect(editor.setMesh(mesh, &error), "open editor integration fixture");
		ModelEdit identity;
		identity.selection.vertices = {1};
		expect(editor.applyEdit(identity, &error), "select editor components before painting");
		editor.showSidebarPage("skeleton");
		editor.show();
		auto *button = editor.findChild<QPushButton *>("paintMeshSkinWeights");
		expect(button != nullptr, "Skeleton offers weight painting");
		QTimer::singleShot(0,
						   [&]
						   {
							   ModelWeightPaintDialog *dialog = nullptr;
							   for (auto *widget : app.topLevelWidgets())
								   if (auto *paintDialog = dynamic_cast<ModelWeightPaintDialog *>(widget))
									   dialog = paintDialog;
							   if (!expect(dialog != nullptr, "editor opens weight painting review"))
								   return;
							   expect(idle(*dialog), "editor review prepares");
							   auto *table = dialog->findChild<QTableView *>("weightPaintTable");
							   select(table, {0});
							   dialog->findChild<QComboBox *>("weightPaintJoint")->setCurrentIndex(1);
							   select(table, {0});
							   dialog->findChild<QDoubleSpinBox *>("weightPaintStrength")->setValue(1);
							   dialog->findChild<QDoubleSpinBox *>("weightPaintValue")->setValue(.7);
							   expect(dialog->paintSelected(), "editor review stages numeric stroke");
							   dialog->findChild<QPushButton *>("weightPaintApply")->click();
						   });
		if (button)
			button->click();
		expect(close(modelVertexJointWeight(editor.document().mesh().surfaces[0], 0, 1), .7) && editor.document().canUndo(),
			   "reviewed batch commits through editor undo service");
		expect(editor.document().selection().vertices == QSet<int>{1}, "editor retains original component selection after painting");
		editor.findChild<QAction *>("undoMesh")->trigger();
		expect(rigBytes(editor.document().mesh()) == original, "editor undo restores the full source in one step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		expect(close(modelVertexJointWeight(editor.document().mesh().surfaces[0], 0, 1), .7), "editor redo restores reviewed weights");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
