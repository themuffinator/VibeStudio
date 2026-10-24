#include "app/model_animation_curve_view.h"
#include "app/model_animation_curves_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_animation_curves_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>
using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool ok, const char *name)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << name << '\n';
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
		if (QByteArray(context) != "ModelAnimationCurvesDialog")
			return {};
		const auto s = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(s, QString("~~ ").repeated(s.size() / 6));
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
	return image.save(QDir(root).filePath(QString("curves-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
void sparseMotion(ModelAnimationCurvesDialog &dialog)
{
	dialog.capture();
	dialog.findChild<QComboBox *>("curvesChannel")->setCurrentIndex(1);
	auto *table = dialog.findChild<QTableView *>("curvesKeys");
	for (int i = 0; i < 3; ++i)
	{
		table->setCurrentIndex(table->model()->index(1, 0));
		dialog.findChild<QPushButton *>("curvesRemoveKey")->click();
	}
	table->setCurrentIndex(table->model()->index(0, 0));
	dialog.findChild<QComboBox *>("curvesInterpolation")->setCurrentIndex(2);
	dialog.findChild<QComboBox *>("curvesTangent")->setCurrentIndex(3);
	dialog.findChild<QPushButton *>("curvesSetKey")->click();
	table->setCurrentIndex(table->model()->index(1, 0));
	dialog.findChild<QDoubleSpinBox *>("curvesKeyValue")->setValue(8);
	dialog.findChild<QComboBox *>("curvesTangent")->setCurrentIndex(3);
	dialog.findChild<QPushButton *>("curvesSetKey")->click();
}
} // namespace
int main(int argc, char **argv)
{
	// Owned widget semantics and render targets only, without OS input/capture.
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
	QTemporaryDir tmp(QDir(root).filePath("curves-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("animation curves UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = curveFixture();
	const auto bytes = rigBytes(mesh);
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
		ModelAnimationCurvesDialog dialog(mesh, "upper", 1);
		dialog.resize(1180, 1000);
		dialog.show();
		QCoreApplication::processEvents();
		auto *apply = dialog.findChild<QPushButton *>("curvesApply"), *review = dialog.findChild<QPushButton *>("curvesReview");
		auto *table = dialog.findChild<QTableView *>("curvesKeys");
		auto *tabs = dialog.findChild<QTabWidget *>("curvesTabs");
		auto *graph = dynamic_cast<ModelAnimationCurveView *>(dialog.findChild<QWidget *>("animationCurveView"));
		auto *preview = dialog.findChild<ModelViewport *>("curvesPreview");
		if (!expect(apply && review && table && tabs && graph && preview, "curve authoring controls exist"))
			return 1;
		expect(!apply->isEnabled() && !review->isEnabled(), "capture precedes review and apply");
		for (const char *id : {"curvesClip", "curvesJoint", "curvesChannel", "curvesCapture", "curvesKeys", "curvesKeyFrame",
							   "curvesKeyValue", "curvesInterpolation", "curvesTangent", "curvesInSlope", "curvesOutSlope", "curvesSetKey",
							   "curvesMoveKey", "curvesRemoveKey", "curvesReview"})
		{
			auto *w = dialog.findChild<QWidget *>(id);
			expect(w && QAccessible::queryAccessibleInterface(w) && !w->accessibleName().isEmpty() &&
					   !w->accessibleDescription().isEmpty() && (w->focusPolicy() & Qt::TabFocus),
				   "native control exposes role, name, description and keyboard focus");
		}
		sparseMotion(dialog);
		expect(table->model()->rowCount() == 2 && !dialog.ready(), "native key controls stage sparse Hermite curve");
		expect(!dialog.findChild<QComboBox *>("curvesJoint")->isEnabled() && dialog.findChild<QPushButton *>("curvesDiscard")->isEnabled(),
			   "draft edits cannot be lost by changing context");
		review->click();
		if (!expect(dialog.ready(), "curve review validates and samples"))
		{
			std::cerr << dialog.findChild<QLabel *>("curvesStatus")->text().toStdString();
			return 1;
		}
		expect(std::abs(dialog.previewMesh().surfaces[0].frames[2].positions[2].y - 1.25) < .0001 && rigBytes(mesh) == bytes,
			   "review matches analytic cubic motion and preserves source");
		QCoreApplication::processEvents();
		expect(render(dialog, QString("keys-top-%1").arg(scenario)), "render keys and numeric controls");
		// The page's own scroll area, not the dialog body's.
		QScrollArea *scroll = nullptr;
		for (auto *candidate : dialog.findChildren<QScrollArea *>())
			if (!scroll && candidate->objectName() != QStringLiteral("dialogBodyScroll"))
				scroll = candidate;
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(render(dialog, QString("keys-bottom-%1").arg(scenario)), "render tangent fields and key actions");
		tabs->setCurrentIndex(1);
		graph->fit();
		graph->selectKey(0);
		QCoreApplication::processEvents();
		auto *accessible = QAccessible::queryAccessibleInterface(graph);
		expect(accessible && accessible->role() == QAccessible::Graphic && accessible->valueInterface(),
			   "curve graph exposes an accessible editable value");
		expect(graph->keyPoint(0).x() < graph->keyPoint(1).x() && graph->layoutDirection() == Qt::LeftToRight,
			   "time direction remains left to right in RTL layouts");
		expect(render(dialog, QString("graph-%1").arg(scenario)), "render Hermite graph and tangent handles");
		if (accessible && accessible->valueInterface())
			accessible->valueInterface()->setCurrentValue(1);
		expect(!dialog.ready() && !apply->isEnabled() && std::abs(table->model()->data(table->model()->index(0, 1)).toDouble() - 1) < .0001,
			   "accessible value editing stages a real key change and invalidates review");
		dialog.findChild<QPushButton *>("curvesUndo")->click();
		expect(std::abs(table->model()->data(table->model()->index(0, 1)).toDouble()) < .0001, "draft undo restores previous key");
		dialog.findChild<QPushButton *>("curvesRedo")->click();
		expect(std::abs(table->model()->data(table->model()->index(0, 1)).toDouble() - 1) < .0001, "draft redo restores edited key");
		dialog.findChild<QPushButton *>("curvesUndo")->click();
		review->click();
		tabs->setCurrentIndex(2);
		dialog.findChild<QSpinBox *>("curvesPreviewSample")->setValue(1);
		preview->setBackfaceCulling(false);
		preview->setEditJoint({});
		preview->setEditSelection(0, {});
		preview->setVertexPicking(true);
		expect(settleModelViewport(*preview), "curve pose preview renders");
		QPointF p;
		preview->projectToView({8, 1.25f, 0}, &p);
		const auto projection = preview->editVertexProjection();
		expect(projection && QLineF(projection->positions[2], p).length() < .05, "viewport renders the sampled cubic position");
		expect(render(dialog, QString("preview-%1").arg(scenario)), "render deformed pose preview");
		dialog.findChild<QComboBox *>("curvesPreviewSource")->setCurrentIndex(1);
		preview->setEditJoint({});
		preview->setEditSelection(0, {});
		expect(settleModelViewport(*preview), "original pose comparison renders");
		preview->projectToView({8, 0, 0}, &p);
		const auto original = preview->editVertexProjection();
		expect(original && QLineF(original->positions[2], p).length() < .05 && dialog.ready(),
			   "comparison preserves original motion and reviewed operation");
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(dialog.edit(), &error) &&
				   rigBytes(document.mesh()) == rigBytes(dialog.previewMesh()),
			   "review reproduces through shared document service");
		expect(document.undo() && rigBytes(document.mesh()) == bytes, "document undo restores unkeyed motion");
		apply->click();
		expect(dialog.result() == QDialog::Accepted, "Apply accepts reviewed curve group");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelAnimationCurvesDialog discard(mesh, "upper", 1);
	sparseMotion(discard);
	discard.findChild<QPushButton *>("curvesDiscard")->click();
	expect(discard.findChild<QTableView *>("curvesKeys")->model()->rowCount() == 0 &&
			   discard.findChild<QComboBox *>("curvesJoint")->isEnabled(),
		   "discard restores source and unlocks context");
	auto keyed = mesh;
	expect(applyModelEdit(&keyed, captureCurves(), nullptr, &error), "prepare stored-curve fixture");
	ModelAnimationCurvesDialog baking(keyed, "upper", 1);
	baking.capture();
	expect(!baking.busy() && baking.findChild<QComboBox *>("curvesJoint")->isEnabled(),
		   "existing curves cannot be recaptured and leave controls enabled");
	baking.findChild<QComboBox *>("curvesChannel")->setCurrentIndex(6);
	auto *scaleGraph = dynamic_cast<ModelAnimationCurveView *>(baking.findChild<QWidget *>("animationCurveView"));
	auto *scaleAccess = QAccessible::queryAccessibleInterface(scaleGraph);
	expect(scaleAccess && scaleAccess->valueInterface() && scaleAccess->valueInterface()->minimumValue().toDouble() > .000001,
		   "accessible scale editing advertises the positive range");
	baking.findChild<QPushButton *>("curvesBake")->click();
	baking.review();
	expect(baking.ready() && !hasModelAnimationCurves(baking.previewMesh()) && baking.edit().curves.action == ModelCurveAction::Bake,
		   "explicit bake stages metadata removal while retaining motion");
	ModelAnimationCurvesDialog closing(mesh, "upper", 1);
	bool cancelled = false;
	QTimer::singleShot(0, &closing,
					   [&]
					   {
						   cancelled = closing.busy();
						   closing.reject();
					   });
	closing.capture();
	expect(cancelled && !closing.ready() && !closing.busy() && closing.result() == QDialog::Rejected,
		   "close cancels capture and joins worker");
	{
		ModelEditorDialog editor;
		expect(editor.setMesh(mesh, &error), "editor opens curve fixture");
		editor.showSidebarPage("skeleton");
		editor.show();
		auto *button = editor.findChild<QPushButton *>("editMeshAnimationCurves");
		expect(button != nullptr, "Skeleton offers Animation Curves");
		QTimer::singleShot(0,
						   [&]
						   {
							   ModelAnimationCurvesDialog *dialog = nullptr;
							   for (auto *w : app.topLevelWidgets())
								   if (auto *d = dynamic_cast<ModelAnimationCurvesDialog *>(w); d && d->isVisible())
									   dialog = d;
							   if (!expect(dialog != nullptr, "editor opens curve authoring"))
								   return;
							   dialog->findChild<QComboBox *>("curvesJoint")->setCurrentText("upper");
							   sparseMotion(*dialog);
							   dialog->review();
							   expect(dialog->ready(), "editor curve review validates");
							   if (dialog->ready())
								   dialog->findChild<QPushButton *>("curvesApply")->click();
							   else
								   dialog->reject();
						   });
		if (button)
			button->click();
		expect(editor.document().canUndo() && hasModelAnimationCurves(editor.document().mesh()),
			   "editor applies curves through shared worker and history");
		editor.findChild<QAction *>("undoMesh")->trigger();
		expect(rigBytes(editor.document().mesh()) == bytes, "editor undo restores source in one step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		expect(hasModelAnimationCurves(editor.document().mesh()), "editor redo restores curve authoring");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
