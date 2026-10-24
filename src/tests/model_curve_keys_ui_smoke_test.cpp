#include "app/model_curve_keys_dialog.h"
#include "app/model_curve_keys_table.h"
#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_curve_keys_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
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
		if (QByteArray(context) != "ModelCurveKeysDialog")
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
	return image.save(QDir(root).filePath(QString("keys-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
void allKeys(ModelCurveKeysDialog &dialog)
{
	dialog.findChild<QComboBox *>("curveKeysJoint")->setCurrentIndex(0);
	dialog.findChild<QComboBox *>("curveKeysChannel")->setCurrentIndex(0);
	dialog.findChild<QPushButton *>("curveKeysAll")->click();
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
	QTemporaryDir tmp(QDir(root).filePath("key-tools-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("curve key tools UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = curveKeysFixture(true);
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
		ModelCurveKeysDialog dialog(mesh, "upper", 1);
		dialog.resize(1180, 1000);
		dialog.show();
		QCoreApplication::processEvents();
		auto *keys = dialog.findChild<QTableView *>("curveKeysTable"), *results = dialog.findChild<QTableView *>("curveKeysResults");
		auto *tabs = dialog.findChild<QTabWidget *>("curveKeysTabs");
		auto *review = dialog.findChild<QPushButton *>("curveKeysReview"), *apply = dialog.findChild<QPushButton *>("curveKeysApply");
		if (!expect(keys && results && tabs && review && apply, "batch key controls exist"))
			return 1;
		expect(keys->model()->rowCount() == 81 && !review->isEnabled() && !apply->isEnabled(),
			   "joint filter lists existing keys and requires selection");
		for (const char *id :
			 {"curveKeysClip",			"curveKeysJoint",	   "curveKeysChannel", "curveKeysTable",   "curveKeysFirst",
			  "curveKeysLast",			"curveKeysRange",	   "curveKeysAll",	   "curveKeysClear",   "curveKeysOperation",
			  "curveKeysPivot",			"curveKeysFactor",	   "curveKeysOffset",  "curveKeysSnap",	   "curveKeysTranslationError",
			  "curveKeysRotationError", "curveKeysScaleError", "curveKeysReview",  "curveKeysResults", "curveKeysDetail"})
		{
			auto *w = dialog.findChild<QWidget *>(id);
			expect(w && QAccessible::queryAccessibleInterface(w) && !w->accessibleName().isEmpty() &&
					   !w->accessibleDescription().isEmpty() && (w->focusPolicy() & Qt::TabFocus),
				   "native batch control exposes role, name, description and focus");
		}
		dialog.findChild<QComboBox *>("curveKeysChannel")->setCurrentIndex(2);
		dialog.findChild<QSpinBox *>("curveKeysFirst")->setValue(2);
		dialog.findChild<QSpinBox *>("curveKeysLast")->setValue(6);
		dialog.findChild<QPushButton *>("curveKeysRange")->click();
		expect(keys->selectionModel()->selectedRows().size() == 5, "range selection includes exact endpoints");
		allKeys(dialog);
		expect(keys->model()->rowCount() == 162 && keys->selectionModel()->selectedRows().size() == 162,
			   "select all spans both joints and nine channels");
		expect(render(dialog, QString("selection-%1").arg(scenario)), "render key selection");
		dialog.findChild<QComboBox *>("curveKeysOperation")->setCurrentIndex(3);
		tabs->setCurrentIndex(1);
		QCoreApplication::processEvents();
		expect(!dialog.findChild<QDoubleSpinBox *>("curveKeysFactor")->isEnabled() &&
				   dialog.findChild<QDoubleSpinBox *>("curveKeysTranslationError")->isEnabled(),
			   "reduction exposes error limits and disables timing fields");
		expect(render(dialog, QString("reduction-top-%1").arg(scenario)), "render reduction settings");
		// The page's own scroll area, not the dialog body's.
		QScrollArea *scroll = nullptr;
		for (auto *candidate : dialog.findChildren<QScrollArea *>())
			if (!scroll && candidate->objectName() != QStringLiteral("dialogBodyScroll"))
				scroll = candidate;
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(render(dialog, QString("reduction-bottom-%1").arg(scenario)), "render all error controls");
		review->click();
		if (!expect(dialog.ready(), "reduction review validates"))
		{
			std::cerr << dialog.findChild<QLabel *>("curveKeysStatus")->text().toStdString();
			return 1;
		}
		expect(dialog.report().channels.size() == 18 && dialog.report().channels[0].before == 9 && dialog.report().channels[0].after == 2 &&
				   rigBytes(mesh) == bytes,
			   "review reports removed keys without mutating source");
		tabs->setCurrentIndex(2);
		results->setCurrentIndex(results->model()->index(1, 0));
		QCoreApplication::processEvents();
		expect(!dialog.findChild<QLineEdit *>("curveKeysDetail")->text().isEmpty(), "result row exposes full numeric detail");
		expect(render(dialog, QString("results-%1").arg(scenario)), "render continuous-change report");
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(dialog.edit(), &error) &&
				   rigBytes(document.mesh()) == rigBytes(dialog.previewMesh()),
			   "review reproduces through document edit");
		expect(document.undo() && rigBytes(document.mesh()) == bytes && document.redo(), "one-step undo and redo retain batch curves");
		const auto reduced = document.mesh();
		apply->click();
		expect(dialog.result() == QDialog::Accepted, "Apply accepts reviewed key reduction");
		ModelCurveKeysDialog retime(reduced, "upper", 1);
		retime.resize(1180, 1000);
		retime.show();
		allKeys(retime);
		retime.findChild<QDoubleSpinBox *>("curveKeysFactor")->setValue(.5);
		retime.review();
		if (!expect(retime.ready(), "retime reduced channels"))
		{
			std::cerr << retime.findChild<QLabel *>("curveKeysStatus")->text().toStdString();
			return 1;
		}
		expect(std::abs(retime.previewMesh().surfaces[0].frames[5].positions[2].y - 8) < .0001, "retiming accelerates the sampled pose");
		retime.findChild<QTabWidget *>("curveKeysTabs")->setCurrentIndex(3);
		retime.findChild<QSpinBox *>("curveKeysPreviewSample")->setValue(4);
		auto *preview = retime.findChild<ModelViewport *>("curveKeysPreview");
		preview->setBackfaceCulling(false);
		preview->setEditJoint({});
		preview->setEditSelection(0, {});
		preview->setVertexPicking(true);
		expect(settleModelViewport(*preview), "reviewed batch pose renders");
		QPointF p;
		preview->projectToView({8, 8, 0}, &p);
		auto projection = preview->editVertexProjection();
		expect(projection && QLineF(projection->positions[2], p).length() < .05, "viewport agrees with retimed skin position");
		expect(render(retime, QString("retimed-preview-%1").arg(scenario)), "render retimed preview");
		retime.findChild<QComboBox *>("curveKeysPreviewSource")->setCurrentIndex(1);
		preview->setEditJoint({});
		preview->setEditSelection(0, {});
		expect(settleModelViewport(*preview), "original batch pose renders");
		preview->projectToView({8, 4, 0}, &p);
		projection = preview->editVertexProjection();
		expect(projection && QLineF(projection->positions[2], p).length() < .05 && retime.ready(),
			   "original comparison keeps review and camera");
		retime.findChild<QDoubleSpinBox *>("curveKeysOffset")->setValue(1);
		expect(!retime.ready() && !retime.findChild<QPushButton *>("curveKeysApply")->isEnabled(), "timing change invalidates review");
		retime.review();
		expect(retime.ready(), "changed timing revalidates");
		retime.findChild<QPushButton *>("curveKeysClear")->click();
		expect(!retime.ready() && !retime.findChild<QPushButton *>("curveKeysReview")->isEnabled(), "selection change invalidates review");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelCurveKeysTable table;
	table.setGroups(mesh.skeleton.clips[0].curves);
	QVector<ModelCurveKeySelection> selection;
	expect(modelCurveKeysFromRows(table.snapshot(), {{10, 13}, {12, 15}, {0, 0}}, &selection, &error) && selection.size() == 2 &&
			   selection[1].samples == QVector<int>({1, 2, 3, 4, 5, 6}),
		   "overlapping selection ranges merge without duplicate keys");
	expect(table.sampleRange(8, 8).size() == 18, "range selection uses compressed runs per channel");
	ModelCurveKeysDialog absent(curveFixture(), {}, 0);
	expect(!absent.findChild<QPushButton *>("curveKeysReview")->isEnabled(), "unkeyed source requires curve capture");
	ModelCurveKeysDialog closing(mesh, {}, 1);
	allKeys(closing);
	closing.findChild<QComboBox *>("curveKeysOperation")->setCurrentIndex(3);
	bool cancelled = false;
	QTimer::singleShot(0, &closing,
					   [&]
					   {
						   cancelled = closing.busy();
						   closing.reject();
					   });
	closing.review();
	expect(cancelled && !closing.ready() && !closing.busy() && closing.result() == QDialog::Rejected,
		   "close cancels review and joins the worker");
	ModelEditorDialog editor;
	expect(editor.setMesh(mesh, &error), "editor opens keyed fixture");
	editor.showSidebarPage("skeleton");
	editor.show();
	auto *button = editor.findChild<QPushButton *>("editMeshAnimationKeys");
	expect(button != nullptr, "Skeleton offers Edit Animation Keys");
	QTimer::singleShot(0,
					   [&]
					   {
						   ModelCurveKeysDialog *dialog = nullptr;
						   for (auto *w : app.topLevelWidgets())
							   if (auto *d = dynamic_cast<ModelCurveKeysDialog *>(w); d && d->isVisible())
								   dialog = d;
						   if (!expect(dialog != nullptr, "editor opens batch key review"))
							   return;
						   allKeys(*dialog);
						   dialog->findChild<QComboBox *>("curveKeysOperation")->setCurrentIndex(3);
						   dialog->review();
						   expect(dialog->ready(), "editor batch review validates");
						   if (dialog->ready())
							   dialog->findChild<QPushButton *>("curveKeysApply")->click();
						   else
							   dialog->reject();
					   });
	if (button)
		button->click();
	expect(editor.document().canUndo() && editor.document().mesh().skeleton.clips[0].curves[0].channels[1].size() == 2,
		   "editor applies one batch transaction");
	editor.findChild<QAction *>("undoMesh")->trigger();
	expect(rigBytes(editor.document().mesh()) == bytes, "editor undo restores dense curves");
	editor.findChild<QAction *>("redoMesh")->trigger();
	expect(editor.document().mesh().skeleton.clips[0].curves[0].channels[1].size() == 2, "editor redo restores reduced curves");
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
