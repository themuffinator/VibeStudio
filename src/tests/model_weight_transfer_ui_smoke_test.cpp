#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/model_weight_transfer_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_weight_transfer_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTableWidget>
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
		if (QByteArray(context) != "ModelWeightTransferDialog")
			return {};
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(source, QString("~~ ").repeated(source.size() / 6));
	}
};
bool render(QWidget &widget, const QString &name)
{
	const auto path = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (path.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(path).filePath(QString("weight-transfer-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
void setMirror(ModelWeightTransferDialog &dialog)
{
	auto *axis = dialog.findChild<QComboBox *>("transferMirror"), *side = dialog.findChild<QComboBox *>("transferSide");
	axis->setCurrentIndex(axis->findData(0));
	side->setCurrentIndex(side->findData(int(ModelWeightTransferSide::PositiveToNegative)));
	dialog.findChild<QPushButton *>("transferPairJoints")->click();
}
} // namespace
int main(int argc, char **argv)
{
	// Semantic Qt calls and owned render targets only: no OS input or capture.
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
	QTemporaryDir tmp(QDir(root).filePath("transfer-ui-XXXXXX"));
	if (!tmp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(tmp.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("weight transfer UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = transferFixture();
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
		ModelWeightTransferDialog dialog(mesh, {}, 2);
		dialog.resize(1100, 900);
		dialog.show();
		QCoreApplication::processEvents();
		auto *apply = dialog.findChild<QPushButton *>("transferApply"), *review = dialog.findChild<QPushButton *>("transferReview");
		auto *tabs = dialog.findChild<QTabWidget *>("transferTabs");
		auto *preview = dialog.findChild<ModelViewport *>("transferPreview");
		auto *map = dialog.findChild<QTableWidget *>("transferJointMap");
		auto *table = dialog.findChild<QTableView *>("transferWeights");
		auto *scroll = dialog.findChild<QScrollArea *>();
		if (!expect(apply && review && tabs && preview && map && table && scroll, "transfer controls exist"))
			return 1;
		expect(!dialog.ready() && !apply->isEnabled(), "applying requires a reviewed candidate");
		setMirror(dialog);
		expect(map->item(1, 1)->text() == "arm.R" && map->item(2, 1)->text() == "arm.L", "suffix action exposes both joint directions");
		for (const char *id :
			 {"transferSource", "transferTarget", "transferScope", "transferMethod", "transferMirror", "transferSide", "transferDistance",
			  "transferAngle", "transferStrength", "transferUnmatched", "transferJointMap", "transferReview", "transferWeights"})
		{
			auto *widget = dialog.findChild<QWidget *>(id);
			expect(widget && QAccessible::queryAccessibleInterface(widget) && (widget->focusPolicy() & Qt::TabFocus) &&
					   !widget->accessibleDescription().isEmpty(),
				   "native controls provide names, descriptions and keyboard focus");
		}
		expect(scroll->horizontalScrollBar()->maximum() == 0, "settings fit scaled translated text");
		for (auto *label : scroll->findChildren<QLabel *>())
			if (label->wordWrap() && label->isVisible())
				expect(label->height() >= label->heightForWidth(label->width()), "wrapped setting labels have enough height");
		expect(render(dialog, QString("settings-%1").arg(scenario)), "render settings");
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(render(dialog, QString("settings-bottom-%1").arg(scenario)), "render lower settings at expanded text scale");
		review->click();
		expect(dialog.ready() && apply->isEnabled(), "review runs the shared transfer worker");
		if (!dialog.ready())
			std::cerr << dialog.findChild<QLabel *>("transferStatus")->text().toStdString() << '\n';
		expect(std::abs(modelVertexJointWeight(dialog.previewMesh().surfaces[0], 3, 2) - .75) < .00001 && rigBytes(mesh) == bytes,
			   "review deforms candidate without touching source");
		tabs->setCurrentIndex(1);
		QCoreApplication::processEvents();
		expect(render(dialog, QString("mapping-%1").arg(scenario)), "render joint map and locks");
		tabs->setCurrentIndex(2);
		preview->setBackfaceCulling(false);
		preview->setEditSelection(0, {});
		preview->setVertexPicking(true);
		expect(settleModelViewport(*preview), "reviewed preview render completes");
		QPointF projected;
		preview->projectToView({-2, -2.25, 0}, &projected);
		const auto vertices = preview->editVertexProjection();
		expect(vertices && vertices->positions.size() == 7 && QLineF(vertices->positions[3], projected).length() < .05,
			   "review viewport shows the analytic mirrored pose");
		expect(render(dialog, QString("preview-%1").arg(scenario)), "render posed review");
		dialog.findChild<QComboBox *>("transferView")->setCurrentIndex(1);
		preview->setEditSelection(0, {});
		expect(settleModelViewport(*preview), "original comparison renders");
		preview->projectToView({-2, 0, 0}, &projected);
		const auto original = preview->editVertexProjection();
		expect(original && QLineF(original->positions[3], projected).length() < .05 && dialog.ready(),
			   "comparison keeps original pose without invalidating review");
		tabs->setCurrentIndex(3);
		table->setCurrentIndex(table->model()->index(3, 2));
		QCoreApplication::processEvents();
		expect(table->model()->rowCount() == 7 && table->model()->data(table->model()->index(3, 2)).toString().contains("0.75"),
			   "table shows numeric destination weights");
		expect(dialog.findChild<QPlainTextEdit *>("transferWeightDetail")->toPlainText().contains("arm.R"),
			   "selected row exposes full influence details");
		expect(render(dialog, QString("weights-%1").arg(scenario)), "render exact weight review");
		auto *strength = dialog.findChild<QDoubleSpinBox *>("transferStrength");
		strength->setValue(.5);
		expect(!dialog.ready() && !apply->isEnabled() && table->model()->rowCount() == 0 &&
				   dialog.findChild<QPlainTextEdit *>("transferWeightDetail")->toPlainText().isEmpty(),
			   "changed settings retire preview, apply and weight details");
		review->click();
		expect(dialog.ready() && std::abs(modelVertexJointWeight(dialog.previewMesh().surfaces[0], 3, 2) - .375) < .00001,
			   "new review uses changed strength");
		dialog.findChild<QLineEdit *>("transferLeftSuffix")->setText("L");
		dialog.findChild<QLineEdit *>("transferRightSuffix")->setText(".L");
		dialog.findChild<QPushButton *>("transferPairJoints")->click();
		expect(dialog.findChild<QLabel *>("transferStatus")->text().contains("overlap") && map->item(1, 1)->text() == "arm.R",
			   "overlapping suffixes are refused without changing the reviewed mapping");
		map->item(1, 1)->setText("missing joint");
		review->click();
		expect(!dialog.ready() && !apply->isEnabled(), "invalid joint mapping cannot be applied");
		map->item(1, 1)->setText("arm.R");
		review->click();
		expect(dialog.ready(), "correcting map restores a valid review");
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(dialog.edit(), &error) &&
				   rigBytes(document.mesh()) == rigBytes(dialog.previewMesh()),
			   "reviewed edit reproduces candidate through document service");
		expect(document.undo() && rigBytes(document.mesh()) == bytes, "document undo restores the entire pre-transfer state");
		apply->click();
		expect(dialog.result() == QDialog::Accepted, "Apply accepts the reviewed operation");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelSelection selection;
	selection.vertices = {3};
	ModelWeightTransferDialog scoped(mesh, selection, 2);
	setMirror(scoped);
	scoped.review();
	expect(scoped.ready() && scoped.edit().selection.vertices == selection.vertices &&
			   modelVertexJointWeight(scoped.previewMesh().surfaces[0], 5, 2) == 0,
		   "captured component scope restricts transfer");
	scoped.reject();
	ModelWeightTransferDialog closing(mesh, {}, 2);
	setMirror(closing);
	bool cancellation = false;
	QTimer::singleShot(0, &closing,
					   [&]
					   {
						   cancellation = closing.busy();
						   closing.reject();
					   });
	closing.review();
	expect(cancellation && !closing.ready() && !closing.busy() && closing.result() == QDialog::Rejected && rigBytes(mesh) == bytes,
		   "close cancels and joins worker without source mutation");
	{
		ModelEditorDialog editor;
		expect(editor.setMesh(mesh, &error), "open editor integration fixture");
		ModelEdit identity;
		identity.selection.vertices = {3};
		expect(editor.applyEdit(identity, &error), "select destination component in editor");
		editor.showSidebarPage("skeleton");
		editor.show();
		auto *button = editor.findChild<QPushButton *>("transferMeshSkinWeights");
		expect(button != nullptr, "Skeleton offers weight transfer");
		QTimer::singleShot(0,
						   [&]
						   {
							   ModelWeightTransferDialog *dialog = nullptr;
							   for (auto *widget : app.topLevelWidgets())
								   if (auto *current = dynamic_cast<ModelWeightTransferDialog *>(widget); current && current->isVisible())
									   dialog = current;
							   if (!expect(dialog != nullptr, "editor opens weight transfer review"))
								   return;
							   setMirror(*dialog);
							   dialog->review();
							   expect(dialog->ready(), "editor review validates");
							   if (dialog->ready())
								   dialog->findChild<QPushButton *>("transferApply")->click();
							   else
								   dialog->reject();
						   });
		if (button)
			button->click();
		expect(std::abs(modelVertexJointWeight(editor.document().mesh().surfaces[0], 3, 2) - .75) < .00001 && editor.document().canUndo(),
			   "Apply adopts through editor worker and history");
		expect(editor.document().selection().vertices == QSet<int>{3}, "editor preserves destination selection");
		editor.findChild<QAction *>("undoMesh")->trigger();
		expect(rigBytes(editor.document().mesh()) == bytes, "editor undo restores the source in one step");
		editor.findChild<QAction *>("redoMesh")->trigger();
		expect(std::abs(modelVertexJointWeight(editor.document().mesh().surfaces[0], 3, 2) - .75) < .00001,
			   "editor redo restores transferred weights");
	}
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
