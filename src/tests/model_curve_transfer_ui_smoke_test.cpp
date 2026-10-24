#include "app/model_animation_curve_view.h"
#include "app/model_animation_curves_dialog.h"
#include "app/model_curve_transfer_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_curve_transfer_test_helpers.h"
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
#include <QLineEdit>
#include <QListWidget>
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
bool expect(bool ok, const char *label)
{
	++checks;
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << label << '\n';
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
		if (QByteArray(context) != "ModelCurveTransferDialog" && QByteArray(context) != "ModelCurveKeysDialog")
			return {};
		const auto s = QString::fromUtf8(text);
		return QString("[%1 %2]").arg(s, QString("~~ ").repeated(s.size() / 6));
	}
};
bool render(QWidget &w, const QString &name)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (root.isEmpty())
		return true;
	QImage image(w.size() * w.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(w.devicePixelRatioF());
	image.fill(Qt::transparent);
	w.render(&image);
	return image.save(QDir(root).filePath(QString("transfer-%1-%2x.png").arg(name).arg(w.devicePixelRatioF())));
}
void configure(ModelCurveTransferDialog &d)
{
	d.findChild<QSpinBox *>("curveTransferFirst")->setValue(2);
	d.findChild<QSpinBox *>("curveTransferLast")->setValue(6);
	d.findChild<QSpinBox *>("curveTransferTargetFirst")->setValue(4);
	d.findChild<QDoubleSpinBox *>("curveTransferFactor")->setValue(2);
	d.findChild<QCheckBox *>("curveTransferReverse")->setChecked(true);
	auto *joints = d.findChild<QListWidget *>("curveTransferJoints");
	for (int i = 0; i < joints->count(); ++i)
		joints->item(i)->setSelected(joints->item(i)->text() == "upper" || joints->item(i)->text() == "lower");
	auto *channels = d.findChild<QListWidget *>("curveTransferChannels");
	for (int i = 0; i < channels->count(); ++i)
		channels->item(i)->setSelected(i == 1);
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
	QTemporaryDir tmp(QDir(root).filePath("curve-transfer-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("curve transfer UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = curveTransferFixture();
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
		ModelCurveTransferDialog dialog(mesh, "upper", 1);
		dialog.resize(1180, 1000);
		dialog.show();
		QCoreApplication::processEvents();
		configure(dialog);
		auto *tabs = dialog.findChild<QTabWidget *>("curveTransferTabs");
		auto *review = dialog.findChild<QPushButton *>("curveTransferReview");
		auto *apply = dialog.findChild<QPushButton *>("curveTransferApply");
		auto *results = dialog.findChild<QTableView *>("curveTransferResults");
		auto *preview = dialog.findChild<ModelViewport *>("curveTransferPreview");
		expect(tabs && review && apply && results && preview && review->isEnabled() && !apply->isEnabled(),
			   "transfer review controls are available");
		for (const char *id :
			 {"curveTransferSource", "curveTransferTarget", "curveTransferFirst", "curveTransferLast", "curveTransferTargetFirst",
			  "curveTransferFactor", "curveTransferMatchRate", "curveTransferReverse", "curveTransferJoints", "curveTransferChannels",
			  "curveTransferReview", "curveTransferResults", "curveTransferDetail", "curveTransferSample", "curveTransferView"})
		{
			auto *w = dialog.findChild<QWidget *>(id);
			expect(w && QAccessible::queryAccessibleInterface(w) && !w->accessibleName().isEmpty() &&
					   !w->accessibleDescription().isEmpty() && (w->focusPolicy() & Qt::TabFocus),
				   "native transfer control exposes accessible metadata and tab focus");
		}
		expect(render(dialog, QString("range-top-%1").arg(scenario)), "render transfer range top");
		auto *scroll = qobject_cast<QScrollArea *>(tabs->widget(0));
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(render(dialog, QString("range-bottom-%1").arg(scenario)), "render transfer range bottom");
		tabs->setCurrentIndex(1);
		QCoreApplication::processEvents();
		expect(render(dialog, QString("tracks-%1").arg(scenario)), "render multi-track selection");
		dialog.findChild<QListWidget *>("curveTransferChannels")->clearSelection();
		expect(!review->isEnabled(), "empty channel selection prevents review");
		configure(dialog);
		dialog.findChild<QCheckBox *>("curveTransferMatchRate")->setChecked(true);
		expect(!dialog.findChild<QDoubleSpinBox *>("curveTransferFactor")->isEnabled(), "matching rates disables manual scaling");
		review->click();
		expect(dialog.ready() && apply->isEnabled() && dialog.edit().curveTransfer.factor == 2,
			   "review captures effective rate factor and complete operation");
		expect(rigBytes(mesh) == bytes && results->model()->rowCount() == 2,
			   "review leaves source unchanged and reports both selected channels");
		expect(dialog.previewMesh().skeleton.clips[1].frames[4][1].m[7] == 6, "reviewed destination starts with reversed source endpoint");
		tabs->setCurrentIndex(2);
		results->setCurrentIndex(results->model()->index(0, 0));
		QCoreApplication::processEvents();
		expect(!dialog.findChild<QLineEdit *>("curveTransferDetail")->text().isEmpty() &&
				   render(dialog, QString("results-%1").arg(scenario)),
			   "render reports with full selected detail");
		tabs->setCurrentIndex(3);
		dialog.findChild<QSpinBox *>("curveTransferSample")->setValue(4);
		QCoreApplication::processEvents();
		expect(settleModelViewport(*preview) && render(dialog, QString("result-preview-%1").arg(scenario)),
			   "render destination result pose");
		const int f = mesh.skeleton.bakedClipForFrame.indexOf(1) + 4;
		expect(preview->frame() == f && preview->mesh().surfaces[0].frames[f].positions[2].y == 6, "preview uses destination clip sample");
		dialog.findChild<QComboBox *>("curveTransferView")->setCurrentIndex(1);
		expect(settleModelViewport(*preview) && preview->mesh().surfaces[0].frames[f].positions[2].y == 0 &&
				   render(dialog, QString("original-preview-%1").arg(scenario)),
			   "original preview uses unchanged destination");
		dialog.findChild<QCheckBox *>("curveTransferReverse")->setChecked(false);
		expect(!dialog.ready() && !apply->isEnabled(), "setting changes invalidate review");
		configure(dialog);
		dialog.review();
		expect(dialog.ready(), "changed settings can be reviewed again");
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(dialog.edit(), &error) && document.undo() &&
				   rigBytes(document.mesh()) == bytes,
			   "reviewed transfer replays and undoes through document service");
		dialog.findChild<QSpinBox *>("curveTransferTargetFirst")->setValue(15);
		dialog.review();
		expect(!dialog.ready() && !apply->isEnabled() && !dialog.findChild<QLabel *>("curveTransferStatus")->text().isEmpty(),
			   "invalid destination range refuses without stale Apply");
		configure(dialog);
		dialog.review();
		expect(dialog.ready(), "compact layout retains a successful reviewed operation");
		dialog.resize(900, 600);
		QCoreApplication::processEvents();
		expect(dialog.height() <= 600 && dialog.width() <= 900 && apply->mapTo(&dialog, QPoint(0, apply->height())).y() <= dialog.height(),
			   "transfer dialog fits a compact logical display at the chosen text scale");
		expect(settleModelViewport(*preview), "compact preview rendering settles");
		expect(render(dialog, QString("compact-%1").arg(scenario)), "render compact transfer dialog");
		auto *previewScroll = qobject_cast<QScrollArea *>(tabs->widget(3));
		expect(previewScroll != nullptr, "preview can scroll on a small logical display");
		previewScroll->verticalScrollBar()->setValue(previewScroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(render(dialog, QString("compact-bottom-%1").arg(scenario)), "render compact scrolled preview with reachable Apply");
		dialog.reject();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelCurveTransferDialog noClips(ModelMesh{}, QString(), 0);
	expect(!noClips.findChild<QPushButton *>("curveTransferReview")->isEnabled(), "model without clips cannot review");
	ModelCurveTransferDialog closing(mesh, "upper", 1);
	configure(closing);
	bool cancelled = false;
	QTimer::singleShot(0,
					   [&]
					   {
						   cancelled = closing.busy();
						   closing.reject();
					   });
	closing.review();
	expect(cancelled && !closing.busy() && !closing.ready() && closing.result() == QDialog::Rejected,
		   "close cancels and joins active review");
	ModelAnimationCurveView graph;
	graph.resize(900, 500);
	graph.setCurve({curveKey(0, 0, ModelCurveInterpolation::Step), curveKey(4, 10, ModelCurveInterpolation::StepNext), curveKey(8, 20)}, 9,
				   1, 1);
	graph.show();
	expect(render(graph, "step-directions"), "render exact one-sided step graph");
	ModelEditorDialog editor;
	expect(editor.setMesh(mesh, &error), "editor accepts transfer fixture");
	editor.showSidebarPage("skeleton");
	editor.show();
	auto *button = editor.findChild<QPushButton *>("editMeshCurveTransfer");
	expect(button != nullptr, "Skeleton provides animation range transfer");
	QTimer::singleShot(0,
					   [&]
					   {
						   ModelCurveTransferDialog *dialog = nullptr;
						   for (auto *w : app.topLevelWidgets())
							   if (auto *d = dynamic_cast<ModelCurveTransferDialog *>(w); d && d->isVisible())
								   dialog = d;
						   if (!expect(dialog != nullptr, "editor opens transfer dialog"))
							   return;
						   configure(*dialog);
						   dialog->review();
						   expect(dialog->ready(), "editor transfer review succeeds");
						   if (dialog->ready())
							   dialog->findChild<QPushButton *>("curveTransferApply")->click();
						   else
							   dialog->reject();
					   });
	if (button)
		button->click();
	expect(editor.document().canUndo() && editor.document().mesh().skeleton.clips[1].frames[4][1].m[7] == 6,
		   "editor applies one transfer transaction");
	editor.findChild<QAction *>("undoMesh")->trigger();
	expect(rigBytes(editor.document().mesh()) == bytes, "editor undo restores all clips");
	editor.findChild<QAction *>("redoMesh")->trigger();
	expect(editor.document().mesh().skeleton.clips[1].frames[4][1].m[7] == 6, "editor redo restores transfer");
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
