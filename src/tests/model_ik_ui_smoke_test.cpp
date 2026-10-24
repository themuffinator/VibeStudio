#include "app/model_editor_dialog.h"
#include "app/model_ik_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_ik_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
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
		if (QByteArray(context) != "ModelIkDialog")
			return {};
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(source, QString("~~ ").repeated(source.size() / 6));
	}
};
void configure(ModelIkDialog &dialog)
{
	dialog.findChild<QDoubleSpinBox *>("ikTarget0")->setValue(4);
	dialog.findChild<QDoubleSpinBox *>("ikTarget1")->setValue(4);
	dialog.findChild<QDoubleSpinBox *>("ikTarget2")->setValue(0);
	dialog.findChild<QDoubleSpinBox *>("ikPole0")->setValue(0);
	dialog.findChild<QDoubleSpinBox *>("ikPole1")->setValue(8);
	dialog.findChild<QDoubleSpinBox *>("ikPole2")->setValue(0);
}
bool render(QWidget &widget, const QString &name)
{
	const auto path = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (path.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(path).filePath(QString("ik-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
} // namespace
int main(int argc, char **argv)
{
	// Semantic Qt calls and owned render targets only; no OS input or capture.
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
	QTemporaryDir tmp(QDir(root).filePath("ik-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("inverse kinematics UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = ikFixture();
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
		ModelIkDialog dialog(mesh, "hand", 1);
		dialog.resize(1100, 900);
		dialog.show();
		QCoreApplication::processEvents();
		auto *apply = dialog.findChild<QPushButton *>("ikApply"), *review = dialog.findChild<QPushButton *>("ikReview");
		auto *tabs = dialog.findChild<QTabWidget *>("ikTabs");
		auto *preview = dialog.findChild<ModelViewport *>("ikPreview");
		auto *table = dialog.findChild<QTableView *>("ikSamples");
		auto *scroll = dialog.findChild<QScrollArea *>();
		if (!expect(apply && review && tabs && preview && table && scroll, "IK controls exist"))
			return 1;
		expect(!dialog.ready() && !apply->isEnabled(), "apply requires successful review");
		expect(dialog.findChild<QComboBox *>("ikEndJoint")->currentText() == "hand" &&
				   dialog.findChild<QLabel *>("ikChain")->text().contains("lower"),
			   "selected joint resolves full chain");
		for (const char *id : {"ikClip", "ikEndJoint", "ikFirst", "ikLast", "ikTarget0", "ikTarget1", "ikTarget2", "ikPole0", "ikPole1",
							   "ikPole2", "ikUsePose", "ikReach", "ikOrientation", "ikReview", "ikSamples"})
		{
			auto *w = dialog.findChild<QWidget *>(id);
			expect(w && QAccessible::queryAccessibleInterface(w) && (w->focusPolicy() & Qt::TabFocus) && !w->accessibleName().isEmpty() &&
					   !w->accessibleDescription().isEmpty(),
				   "control has native accessible role, name, description and keyboard focus");
		}
		configure(dialog);
		review->click();
		if (!expect(dialog.ready(), "review solves pose"))
		{
			std::cerr << dialog.findChild<QLabel *>("ikStatus")->text().toStdString();
			return 1;
		}
		expect(rigBytes(mesh) == bytes && std::abs(dialog.previewMesh().surfaces[0].frames[1].positions[2].y - 4) < .0001,
			   "review stages analytic pose without changing source");
		QCoreApplication::processEvents();
		expect(dialog.findChild<QLabel *>("ikStatus")->height() >=
				   dialog.findChild<QLabel *>("ikStatus")->heightForWidth(dialog.findChild<QLabel *>("ikStatus")->width()),
			   "expanded status fits available height");
		expect(render(dialog, QString("settings-top-%1").arg(scenario)), "render chain settings");
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(render(dialog, QString("settings-bottom-%1").arg(scenario)), "render lower settings and action status");
		tabs->setCurrentIndex(1);
		preview->setBackfaceCulling(false);
		preview->setEditJoint({});
		preview->setEditSelection(0, {});
		preview->setVertexPicking(true);
		expect(settleModelViewport(*preview), "solved pose renders");
		QPointF expected;
		preview->projectToView({4, 4, 0}, &expected);
		const auto projected = preview->editVertexProjection();
		expect(projected && QLineF(projected->positions[2], expected).length() < .05, "viewport shows solved end position");
		expect(render(dialog, QString("preview-%1").arg(scenario)), "render solved pose review");
		dialog.findChild<QComboBox *>("ikPreviewSource")->setCurrentIndex(1);
		preview->setEditJoint({});
		preview->setEditSelection(0, {});
		expect(settleModelViewport(*preview), "original comparison renders");
		preview->projectToView({8, 0, 0}, &expected);
		const auto original = preview->editVertexProjection();
		expect(original && QLineF(original->positions[2], expected).length() < .05 && dialog.ready(),
			   "comparison retains original pose and review validity");
		tabs->setCurrentIndex(2);
		table->setCurrentIndex(table->model()->index(0, 2));
		QCoreApplication::processEvents();
		expect(table->model()->rowCount() == 1 && table->model()->data(table->model()->index(0, 1)).toString().contains("Reached"),
			   "results expose non-colour target status");
		expect(dialog.findChild<QPlainTextEdit *>("ikSampleDetail")->toPlainText().contains("4, 4, 0"),
			   "selected sample has complete numeric detail");
		expect(render(dialog, QString("results-%1").arg(scenario)), "render sample result table");
		table->scrollTo(table->model()->index(0, 4), QAbstractItemView::EnsureVisible);
		QCoreApplication::processEvents();
		expect(table->visualRect(table->model()->index(0, 4)).intersects(table->viewport()->rect()),
			   "full tolerance column is reachable at expanded text size");
		expect(render(dialog, QString("results-end-%1").arg(scenario)), "render rightmost result fields");
		dialog.findChild<QSpinBox *>("ikLast")->setValue(2);
		expect(!dialog.ready() && !apply->isEnabled() && table->model()->rowCount() == 0 &&
				   dialog.findChild<QPlainTextEdit *>("ikSampleDetail")->toPlainText().isEmpty(),
			   "changed settings retire candidate and result details");
		review->click();
		expect(dialog.ready() && table->model()->rowCount() == 3, "range reviews all sample results");
		dialog.findChild<QDoubleSpinBox *>("ikTarget0")->setValue(30);
		review->click();
		expect(!dialog.ready() && !apply->isEnabled() && rigBytes(dialog.previewMesh()) == bytes,
			   "failed target cannot apply stale preview");
		dialog.findChild<QComboBox *>("ikReach")->setCurrentIndex(1);
		review->click();
		expect(dialog.ready() && table->model()->data(table->model()->index(0, 1)).toString().contains("Nearest"),
			   "explicit clamping reports remaining distance");
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(dialog.edit(), &error) &&
				   rigBytes(document.mesh()) == rigBytes(dialog.previewMesh()),
			   "review reproduces through document service");
		expect(document.undo() && rigBytes(document.mesh()) == bytes, "document undo restores all samples");
		apply->click();
		expect(dialog.result() == QDialog::Accepted, "Apply accepts reviewed request");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelIkDialog closing(mesh, "hand", 1);
	configure(closing);
	bool cancelled = false;
	QTimer::singleShot(0, &closing,
					   [&]
					   {
						   cancelled = closing.busy();
						   closing.reject();
					   });
	closing.review();
	expect(cancelled && !closing.ready() && !closing.busy() && closing.result() == QDialog::Rejected && rigBytes(mesh) == bytes,
		   "close cancels and joins worker safely");
	ModelIkDialog empty(rigFixture(), "tip", 1);
	expect(!empty.findChild<QPushButton *>("ikReview")->isEnabled(), "incomplete chains explain unavailable IK");
	{
		ModelEditorDialog editor;
		expect(editor.setMesh(mesh, &error), "editor opens IK fixture");
		editor.showSidebarPage("skeleton");
		editor.show();
		auto *button = editor.findChild<QPushButton *>("editMeshIk");
		expect(button != nullptr, "Skeleton offers inverse kinematics");
		QTimer::singleShot(0,
						   [&]
						   {
							   ModelIkDialog *dialog = nullptr;
							   for (auto *w : app.topLevelWidgets())
								   if (auto *d = dynamic_cast<ModelIkDialog *>(w); d && d->isVisible())
									   dialog = d;
							   if (!expect(dialog != nullptr, "editor opens IK review"))
								   return;
							   dialog->findChild<QComboBox *>("ikEndJoint")->setCurrentText("hand");
							   configure(*dialog);
							   dialog->review();
							   expect(dialog->ready(), "editor review validates");
							   if (dialog->ready())
								   dialog->findChild<QPushButton *>("ikApply")->click();
							   else
								   dialog->reject();
						   });
		if (button)
			button->click();
		expect(editor.document().canUndo() && std::abs(editor.document().mesh().surfaces[0].frames[1].positions[2].y - 4) < .0001,
			   "editor applies through shared worker and history");
		editor.findChild<QAction *>("undoMesh")->trigger();
		expect(rigBytes(editor.document().mesh()) == bytes, "editor undo restores source in one step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		expect(std::abs(editor.document().mesh().surfaces[0].frames[1].positions[2].y - 4) < .0001, "editor redo restores IK pose");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
