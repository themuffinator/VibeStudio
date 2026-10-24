#include "app/model_editor_dialog.h"
#include "app/model_skinning_dialog.h"
#include "app/model_viewport.h"
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
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
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
bool waitReady(ModelSkinningDialog &dialog)
{
	QElapsedTimer time;
	time.start();
	while (!dialog.ready() && time.elapsed() < 10000)
	{
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return dialog.ready();
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
	return image.save(QDir(path).filePath(QString("skinning-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
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
		if (QByteArray(context) != "ModelSkinningDialog" && QByteArray(context) != "VibeStudioModelEditor")
			return {};
		const auto source = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(source, QString("~~ ").repeated(source.size() / 6));
	}
};
ModelMesh fixture()
{
	auto mesh = rigFixture();
	auto part = mesh.surfaces[0];
	part.name = "armour";
	part.index = 1;
	part.skinning = {};
	mesh.surfaces.append(part);
	updateEditableModelMetadata(&mesh);
	return mesh;
}
} // namespace
int main(int argc, char **argv)
{
	// Invoke only owned Qt objects and widget render targets, never OS input.
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
	QTemporaryDir temporary(QDir(root).filePath("skin-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("skinning UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = fixture();
	const auto original = rigBytes(mesh);
	ModelSelection selection;
	selection.surface = 1;
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
		ModelSkinningDialog dialog(mesh, selection, "tip", 2);
		dialog.resize(1100, 900);
		dialog.show();
		if (!expect(waitReady(dialog), "initial inspection worker completes"))
			return 1;
		auto *operation = dialog.findChild<QComboBox *>("skinOperation");
		auto *method = dialog.findChild<QComboBox *>("skinMethod");
		auto *review = dialog.findChild<QPushButton *>("reviewSkinning");
		auto *apply = dialog.findChild<QPushButton *>("applySkinning");
		auto *select = dialog.findChild<QPushButton *>("selectSkinVertices");
		auto *table = dialog.findChild<QTableView *>("skinWeightsTable");
		auto *preview = dialog.findChild<ModelViewport *>("skinPreview");
		auto *tabs = dialog.findChild<QTabWidget *>("skinReviewTabs");
		auto *scroll = dialog.findChild<QScrollArea *>();
		if (!expect(operation && method && review && apply && select && table && preview && tabs && scroll, "binding controls present"))
			return 1;
		expect(table->model()->rowCount() == 3 && table->model()->data(table->model()->index(0, 3)).toString() == "0",
			   "inspector reports unbound vertices numerically");
		for (const char *id : {"skinOperation", "skinScope", "skinSurface", "skinJoint", "skinInfluenceLimit", "skinFilter",
							   "reviewSkinning", "selectSkinVertices", "skinWeightsTable"})
		{
			auto *control = dialog.findChild<QWidget *>(id);
			expect(control && QAccessible::queryAccessibleInterface(control) && (control->focusPolicy() & Qt::TabFocus) &&
					   !control->accessibleDescription().isEmpty(),
				   "weight controls have native accessibility and keyboard focus");
		}
		expect(scroll->horizontalScrollBar()->maximum() == 0, "inspect controls fit expanded scaled labels");
		expect(settleModelViewport(*preview), "initial inspection render completes");
		expect(render(dialog, QString("inspect-%1").arg(scenario)), "render inspection controls");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::BindSkin)));
		method->setCurrentIndex(method->findData(int(ModelSkinBindingMethod::NearestJoints)));
		dialog.findChild<QSpinBox *>("skinInfluenceLimit")->setValue(2);
		expect(!dialog.ready() && !apply->isEnabled(), "changed operation invalidates review and apply");
		review->click();
		expect(dialog.ready() && apply->isEnabled(), "binding preview passes shared validation");
		if (!dialog.ready())
			std::cerr << dialog.findChild<QLabel *>("skinReviewStatus")->text().toStdString() << '\n';
		preview->setEditSelection(1, {});
		preview->setVertexPicking(true);
		preview->setBackfaceCulling(false);
		expect(settleModelViewport(*preview), "reviewed binding renders");
		QPointF position;
		preview->projectToView({8, 4, 0}, &position);
		const auto vertices = preview->editVertexProjection();
		expect(vertices && vertices->positions.size() == 3 && QLineF(vertices->positions[1], position).length() < .05,
			   "rendered pose matches analytic binding");
		expect(rigBytes(mesh) == original, "review leaves source unchanged");
		scroll->verticalScrollBar()->setValue(0);
		QCoreApplication::processEvents();
		expect(scroll->horizontalScrollBar()->maximum() == 0 && render(dialog, QString("bind-%1").arg(scenario)),
			   "render expanded bind controls without horizontal overflow");
		tabs->setCurrentIndex(1);
		table->setCurrentIndex(table->model()->index(2, 2));
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
		QCoreApplication::processEvents();
		expect(table->model()->data(table->model()->index(2, 2)).toString().contains("tip") &&
				   table->model()->data(table->model()->index(2, 2)).toString().contains("0.333"),
			   "weight table exposes exact preview influences");
		expect(render(dialog, QString("weights-%1").arg(scenario)), "render reviewed weights and outcome");
		auto *details = dialog.findChild<QPlainTextEdit *>("skinWeightDetails");
		expect(details && details->isReadOnly() && details->toPlainText().contains("0.333") && details->toPlainText().contains("root") &&
				   details->toPlainText().contains("tip"),
			   "keyboard-selected row exposes full weights independently of table width");
		dialog.findChild<QDoubleSpinBox *>("skinFalloff")->setValue(4);
		expect(!dialog.ready() && !apply->isEnabled() && table->model()->rowCount() == 0, "parameter changes retire previous result");
		method->setCurrentIndex(method->findData(int(ModelSkinBindingMethod::Rigid)));
		review->click();
		expect(dialog.ready() && dialog.edit().skin.joints == QStringList{"tip"} && dialog.edit().skin.falloff == 2,
			   "rigid review ignores hidden automatic parameters");
		operation->setCurrentIndex(operation->findData(int(ModelEditKind::RebindSkin)));
		review->click();
		expect(!dialog.ready() && !apply->isEnabled(), "invalid repair cannot be committed");
		operation->setCurrentIndex(0);
		review->click();
		dialog.findChild<QComboBox *>("skinFilter")->setCurrentIndex(1);
		select->click();
		expect(dialog.result() == QDialog::Accepted && dialog.choosingSelection() &&
				   dialog.chosenSelection().vertices == QSet<int>({0, 1, 2}),
			   "inspector returns exact selection without geometry changes");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelEditorDialog editor;
	expect(editor.setMesh(mesh, &error), "editor loads mixed binding model");
	editor.resize(1400, 1000);
	editor.show();
	editor.showSidebarPage("skeleton");
	auto *button = editor.findChild<QPushButton *>("editMeshSkinning");
	if (!expect(button, "editor exposes binding workflow"))
		return 1;
	bool opened = false;
	QTimer::singleShot(0,
					   [&]
					   {
						   auto *dialog = dynamic_cast<ModelSkinningDialog *>(QApplication::activeModalWidget());
						   if (!dialog)
						   {
							   if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
								   modal->reject();
							   return;
						   }
						   opened = true;
						   waitReady(*dialog);
						   dialog->findChild<QComboBox *>("skinSurface")->setCurrentIndex(1);
						   auto *operation = dialog->findChild<QComboBox *>("skinOperation");
						   operation->setCurrentIndex(operation->findData(int(ModelEditKind::BindSkin)));
						   auto *method = dialog->findChild<QComboBox *>("skinMethod");
						   method->setCurrentIndex(method->findData(int(ModelSkinBindingMethod::NearestJoints)));
						   dialog->findChild<QPushButton *>("reviewSkinning")->click();
						   if (dialog->ready())
							   dialog->findChild<QPushButton *>("applySkinning")->click();
						   else
							   dialog->reject();
					   });
	button->click();
	expect(opened && !editor.document().mesh().surfaces[1].skinning.isEmpty() && editor.document().canUndo(),
		   "editor adopts reviewed skinning through worker history");
	expect(std::abs(editor.document().mesh().surfaces[1].frames[2].positions[1].y - 4) < .0001,
		   "editor commit matches preview deformation");
	editor.findChild<QAction *>("undoMesh")->trigger();
	expect(rigBytes(editor.document().mesh()) == original, "editor undo restores entire unbound source");
	editor.findChild<QAction *>("redoMesh")->trigger();
	QTimer::singleShot(0,
					   [&]
					   {
						   auto *dialog = dynamic_cast<ModelSkinningDialog *>(QApplication::activeModalWidget());
						   if (!dialog)
							   return;
						   waitReady(*dialog);
						   dialog->findChild<QComboBox *>("skinSurface")->setCurrentIndex(1);
						   dialog->findChild<QPushButton *>("reviewSkinning")->click();
						   dialog->findChild<QComboBox *>("skinJoint")->setCurrentIndex(1);
						   dialog->findChild<QDoubleSpinBox *>("skinMinimum")->setValue(.3);
						   dialog->findChild<QDoubleSpinBox *>("skinMaximum")->setValue(.4);
						   dialog->findChild<QPushButton *>("selectSkinVertices")->click();
					   });
	button->click();
	expect(editor.document().selection().surface == 1 && editor.document().selection().vertices == QSet<int>{2},
		   "inspector selection returns to editor components");
	auto *editorPreview = editor.findChild<ModelViewport *>("meshPreview");
	expect(editorPreview && settleModelViewport(*editorPreview), "editor selection handoff finishes rendering");
	expect(render(editor, "editor-selection"), "render editor selection handoff");
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
