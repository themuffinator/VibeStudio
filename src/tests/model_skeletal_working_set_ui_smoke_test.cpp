// The skeletal working set in the Mesh Editor: the Skeleton page summary and
// budget meter, the Working Set dialog (checklist, meter, details, keyboard
// toggling, translation expansion, high-visibility themes and right-to-left
// layout), applying it through the worker and history, tool dialogs listing
// clips outside the working set, and opening an oversized skeletal model.
#include "app/model_animation_curves_dialog.h"
#include "app/model_curve_transfer_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_ik_dialog.h"
#include "app/model_skeletal_working_set_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_skeletal_budget_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <cmath>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return value;
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
		if (QByteArray(context) != "ModelSkeletalWorkingSetDialog")
			return {};
		const auto original = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(original, QString("~~ ").repeated(original.size() / 6));
	}
};
bool render(QWidget &widget, const QString &name)
{
	const auto folder = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (folder.isEmpty())
		return true;
	QImage target(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	target.setDevicePixelRatio(widget.devicePixelRatioF());
	target.fill(Qt::transparent);
	widget.render(&target);
	return target.save(QDir(folder).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
QVector<int> bakedClips(const ModelMesh &mesh)
{
	QVector<int> clips;
	for (int clip : mesh.skeleton.bakedClipForFrame)
		if (clip >= 0 && !clips.contains(clip))
			clips.append(clip);
	return clips;
}
QTreeWidgetItem *item(QTreeWidget *tree, int row)
{
	return tree ? tree->topLevelItem(row) : nullptr;
}
void press(QWidget *widget, Qt::Key key)
{
	QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, key == Qt::Key_Space ? QStringLiteral(" ") : QString());
	QCoreApplication::sendEvent(widget, &down);
	QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, key == Qt::Key_Space ? QStringLiteral(" ") : QString());
	QCoreApplication::sendEvent(widget, &up);
}
} // namespace

int main(int argc, char **argv)
{
	// Only owned QObject/widget APIs and render targets. No system input/capture.
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
	QTemporaryDir temporary(QDir(root).filePath("working-set-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	// Numbers in the editor follow the locale; keep the expected text fixed.
	QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
	// 1,200 vertices: walk and run cannot both be baked; walk is.
	const auto mesh = denseSkeletalFixture(1200, {{"walk", 500}, {"run", 500}, {"idle", 30}, {"empty", 0}}, {0});
	if (!expect(validateEditableModel(mesh).isEmpty() && mesh.frames.size() == 501, "fixture fits the budget with walk baked"))
		return 1;

	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario > 0)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario == 0 ? 100 : 200));
		ModelSkeletalWorkingSetDialog dialog(mesh);
		dialog.setLayoutDirection(scenario == 2 ? Qt::RightToLeft : Qt::LeftToRight);
		dialog.resize(760, 640);
		dialog.show();
		QCoreApplication::processEvents();
		auto *tree = dialog.findChild<QTreeWidget *>("workingSetClips");
		auto *meter = dialog.findChild<QProgressBar *>("workingSetBudget");
		auto *summary = dialog.findChild<QLabel *>("workingSetSummary");
		auto *details = dialog.findChild<QLabel *>("workingSetDetails");
		auto *apply = dialog.findChild<QPushButton *>("applyWorkingSet");
		auto *fit = dialog.findChild<QPushButton *>("workingSetFit");
		auto *clear = dialog.findChild<QPushButton *>("workingSetClear");
		auto *showDetails = dialog.findChild<QPushButton *>("workingSetShowDetails");
		if (!expect(tree && meter && summary && details && apply && fit && clear && showDetails, "dialog exposes its controls"))
			return 1;
		for (QWidget *widget : QList<QWidget *>{tree, apply, fit, clear, showDetails})
		{
			auto *interface = QAccessible::queryAccessibleInterface(widget);
			expect(interface && !widget->accessibleName().isEmpty() && !widget->accessibleDescription().isEmpty() &&
					   (widget->focusPolicy() & Qt::TabFocus),
				   "working set control has keyboard focus and accessibility metadata");
		}
		expect(!meter->accessibleName().isEmpty() && meter->accessibleDescription().contains("Frame vertices"),
			   "the meter states its numbers in words");
		expect(tree->topLevelItemCount() == 4 && item(tree, 0)->checkState(0) == Qt::Checked && item(tree, 1)->checkState(0) == Qt::Unchecked &&
				   !(item(tree, 3)->flags() & Qt::ItemIsUserCheckable),
			   "the checklist shows the working set and cannot check a clip without poses");
		expect(item(tree, 0)->text(3).contains("In working set") && item(tree, 1)->text(3).contains("Outside") &&
				   item(tree, 3)->text(3).contains("No poses"),
			   "every clip states its place in words");
		expect(!apply->isEnabled() && summary->text().contains("already"), "an unchanged working set has nothing to apply");
		expect(meter->value() == int(qint64(1200) * 501 * 1000 / 1048576), "the meter shows the budget the working set uses");

		item(tree, 1)->setCheckState(0, Qt::Checked);
		expect(!apply->isEnabled() && meter->format().contains("Over budget") && summary->text().contains("Uncheck") &&
				   item(tree, 1)->text(3).contains("Joins"),
			   "checking both long clips is over the budget, said in words");
		item(tree, 0)->setCheckState(0, Qt::Unchecked);
		expect(apply->isEnabled() && dialog.edit().skeletal.workingSet == QVector<int>{1} && item(tree, 0)->text(3).contains("Leaves") &&
				   dialog.edit().kind == ModelEditKind::SetSkeletalWorkingSet,
			   "swapping clips fits and maps to one working-set edit");
		fit->click();
		expect(dialog.checkedClips() == QVector<int>{0, 2}, "Fit Automatically fills the automatic budget in clip order");
		clear->click();
		expect(dialog.checkedClips().isEmpty() && apply->isEnabled(), "Bind Pose Only clears the checklist");
		tree->setFocus();
		tree->setCurrentItem(item(tree, 2));
		press(tree, Qt::Key_Space);
		expect(dialog.checkedClips() == QVector<int>{2}, "Space toggles the current clip");
		expect(!details->isVisible(), "details start hidden");
		showDetails->click();
		QCoreApplication::processEvents();
		expect(details->isVisible() && details->text().contains("Skeletal clips baked into editable frames") &&
				   details->text().contains("Outside the working set"),
			   "details show the full report on demand");
		expect(tree->geometry().bottom() < fit->geometry().top() && fit->geometry().bottom() < showDetails->geometry().top() &&
				   showDetails->geometry().bottom() < details->geometry().top(),
			   "the list, its actions and the details never overlap");
		auto *scroll = dialog.findChild<QScrollArea *>("workingSetScroll");
		expect(scroll && scroll->horizontalScrollBar()->maximum() == 0, "the dialog fits its width, scaled and translated");
		expect(!meter->format().contains("%%") && meter->format().contains("%"), "the meter shows a plain percentage");
		expect(render(dialog, QStringLiteral("working-set-%1").arg(scenario)), "render working set dialog");
		dialog.hide();
		if (scenario > 0)
			app.removeTranslator(&expansion);
	}

	// Enter applies the checked clips: Apply is the dialog's default button,
	// while a focused action button still takes Enter for itself.
	{
		ModelSkeletalWorkingSetDialog dialog(mesh);
		dialog.show();
		QCoreApplication::processEvents();
		auto *tree = dialog.findChild<QTreeWidget *>("workingSetClips");
		auto *fit = dialog.findChild<QPushButton *>("workingSetFit");
		auto *apply = dialog.findChild<QPushButton *>("applyWorkingSet");
		if (expect(tree && fit && apply, "the dialog opens for keyboard checks"))
		{
			expect(apply->isDefault() && !fit->isDefault(), "Apply is the default button");
			dialog.setCheckedClips({});
			press(fit, Qt::Key_Return);
			expect(dialog.isVisible() && dialog.checkedClips() == QVector<int>{0, 2}, "Enter on Fit Automatically runs it and keeps the dialog open");
			dialog.setCheckedClips({2});
			tree->setCurrentItem(item(tree, 2));
			press(tree, Qt::Key_Return);
			expect(!dialog.isVisible() && dialog.result() == QDialog::Accepted && dialog.edit().skeletal.workingSet == QVector<int>{2},
				   "Enter in the checklist applies the checked clips");
		}
	}
	// A clip too long for the whole budget cannot be checked, and says why.
	{
		const auto character = denseSkeletalFixture(2100, {{"attack", 600}, {"idle", 40}}, {1});
		ModelSkeletalWorkingSetDialog dialog(character);
		dialog.show();
		QCoreApplication::processEvents();
		auto *tree = dialog.findChild<QTreeWidget *>("workingSetClips");
		if (expect(tree && tree->topLevelItemCount() == 2, "the dialog lists the long clip"))
		{
			auto *attack = item(tree, 0);
			expect(!(attack->flags() & Qt::ItemIsUserCheckable) && (attack->flags() & Qt::ItemIsEnabled) &&
					   attack->text(3).contains("Too long to bake") && attack->toolTip(0).contains("cannot be checked") &&
					   attack->data(3, Qt::AccessibleDescriptionRole).toString().contains("cannot be checked"),
				   "a clip too long to bake is reachable but not checkable, and says why");
			tree->setFocus();
			tree->setCurrentItem(attack);
			press(tree, Qt::Key_Space);
			dialog.setCheckedClips({0, 1});
			expect(dialog.checkedClips() == QVector<int>{1} && attack->text(3).contains("Too long to bake"),
				   "neither Space nor a programmatic choice checks the long clip");
		}
	}

	// Tool dialogs list clips outside the working set and preview them.
	ModelIkDialog ik(mesh, "hand", 0);
	auto *ikClip = ik.findChild<QComboBox *>("ikClip");
	expect(ikClip && ikClip->count() == 3 && ikClip->itemText(1).contains("outside the working set") && ikClip->itemText(0) == "walk",
		   "IK lists every clip with poses and marks those outside the working set");
	if (ikClip)
	{
		ikClip->setCurrentIndex(1);
		QCoreApplication::processEvents();
		auto *preview = ik.findChild<ModelViewport *>("ikPreview");
		expect(preview && preview->mesh().frames.size() == 1, "IK previews a clip outside the working set one sample at a time");
		auto *sample = ik.findChild<QSpinBox *>("ikPreviewSample");
		if (preview && sample)
		{
			sample->setValue(123);
			QCoreApplication::processEvents();
			ModelFrameGeometry expected;
			const auto &shown = preview->mesh();
			bool same = skinModelSurface(mesh.surfaces[0], mesh.skeleton.clips[1].frames[123], &expected) && shown.frames.size() == 1 &&
						shown.surfaces[0].frames[0].positions.size() == expected.positions.size();
			for (int i = 0; same && i < expected.positions.size(); ++i)
				same = std::abs(shown.surfaces[0].frames[0].positions[i].y - expected.positions[i].y) < 1e-4 &&
					   std::abs(shown.surfaces[0].frames[0].positions[i].x - expected.positions[i].x) < 1e-4;
			expect(same && std::abs(expected.positions.last().y - mesh.surfaces[0].frames[0].positions.last().y) > 0.5,
				   "the IK preview shows the chosen sample's skinned pose");
		}
	}
	ModelAnimationCurvesDialog curves(mesh, "tip", 0);
	auto *curvesClip = curves.findChild<QComboBox *>("curvesClip");
	expect(curvesClip && curvesClip->count() == 3, "Animation Curves lists clips outside the working set");
	ModelCurveTransferDialog transfer(mesh, "tip", 0);
	auto *target = transfer.findChild<QComboBox *>("curveTransferTarget");
	expect(target && target->count() == 3, "range transfer can write a clip outside the working set");

	if (const int unavailable = test_support::exitCodeWithoutRenderer("model-skeletal-working-set-ui-smoke"); unavailable >= 0)
		return unavailable;
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelEditorDialog editor;
	QString error;
	if (!expect(editor.setMesh(mesh, &error), "editor admits fixture"))
		return 1;
	auto *pageSummary = editor.findChild<QLabel *>("meshSkeletalWorkingSet");
	auto *pageMeter = editor.findChild<QProgressBar *>("meshSkeletalBudget");
	auto *open = editor.findChild<QPushButton *>("editMeshSkeletalWorkingSet");
	if (!expect(pageSummary && pageMeter && open, "Skeleton page shows the working set"))
		return 1;
	expect(pageSummary->text().contains("in the working set") && pageMeter->value() == int(qint64(1200) * 501 * 1000 / 1048576) &&
			   !pageMeter->accessibleDescription().isEmpty() && !open->accessibleDescription().isEmpty(),
		   "the page summary and meter state the budget");
	bool opened = false;
	QTimer::singleShot(0,
					   [&]
					   {
						   auto *dialog = dynamic_cast<ModelSkeletalWorkingSetDialog *>(QApplication::activeModalWidget());
						   if (!dialog)
						   {
							   if (auto *modal = dynamic_cast<QDialog *>(QApplication::activeModalWidget()))
								   modal->reject();
							   return;
						   }
						   opened = true;
						   dialog->setCheckedClips({1, 2});
						   dialog->findChild<QPushButton *>("applyWorkingSet")->click();
					   });
	open->click();
	expect(opened && bakedClips(editor.document().mesh()) == QVector<int>{1, 2} && editor.document().mesh().frames.size() == 531 &&
			   editor.document().canUndo() && editor.document().mesh().skeleton.clips.size() == 4,
		   "the editor applies the working set through the worker and history");
	auto *status = editor.findChild<QLabel *>("meshStatus");
	expect(status && status->text().contains("Re-baked the frames") && status->text().contains("2 of 4"),
		   "the status line names the new working set");
	expect(pageSummary->text().contains("2 of 4"), "the page summary follows the edit");
	// The confirmation replaces the status line, so it carries the one-time
	// notice when the working-set edit made history discard steps.
	setModelDocumentHistoryLimit(1024 * 1024);
	QTimer::singleShot(0,
					   [&]
					   {
						   auto *dialog = dynamic_cast<ModelSkeletalWorkingSetDialog *>(QApplication::activeModalWidget());
						   if (!dialog)
						   {
							   if (auto *modal = dynamic_cast<QDialog *>(QApplication::activeModalWidget()))
								   modal->reject();
							   return;
						   }
						   dialog->setCheckedClips({0, 2});
						   dialog->findChild<QPushButton *>("applyWorkingSet")->click();
					   });
	open->click();
	expect(bakedClips(editor.document().mesh()) == QVector<int>{0, 2} && editor.document().discardedHistorySteps() > 0 && status &&
			   status->text().contains("Re-baked the frames") && status->text().contains("Undo history reached its"),
		   "a working-set edit that trims history keeps the trim notice beside its confirmation");
	setModelDocumentHistoryLimit(modelDocumentDefaultHistoryBytes);

	// A decoded model handed to the editor, as the Models page hands a package
	// model to the Mesh Editor, is fitted to the budget like an import.
	{
		const auto dense = denseSkeletalFixture(1200, {{"walk", 500}, {"run", 500}}, {});
		const auto decoded = decodeModelMesh("models/dense.iqm", exportEditableModel(dense, "iqm", 0, &error));
		ModelEditorDialog handed;
		expect(decoded.frames.size() == 1001 && !validateEditableModel(decoded).isEmpty() && handed.setMesh(decoded, &error) &&
				   handed.document().mesh().frames.size() == 501 && handed.document().mesh().skeleton.clips.size() == 2,
			   "the editor fits a decoded oversized skeletal model instead of refusing it");
		auto *handedStatus = handed.findChild<QLabel *>("meshStatus");
		expect(handedStatus && handedStatus->text().contains("re-baked") && handedStatus->text().contains("Left out to fit the budget: run."),
			   "the status line explains the fit for a handed-over model");
	}

	// Opening an oversized skeletal model re-bakes it and says so.
	const auto bindOnly = denseSkeletalFixture(1200, {{"walk", 500}, {"run", 500}}, {});
	const auto iqmPath = QDir(temporary.path()).filePath("dense.iqm");
	QFile file(iqmPath);
	expect(file.open(QIODevice::WriteOnly) && file.write(exportEditableModel(bindOnly, "iqm", 0, &error)) > 0, "write dense IQM");
	file.close();
	ModelEditorDialog opener;
	expect(opener.openSource(iqmPath, &error) && opener.document().mesh().frames.size() == 501 &&
			   opener.document().mesh().skeleton.clips.size() == 2,
		   "the editor opens an oversized skeletal model");
	auto *openerStatus = opener.findChild<QLabel *>("meshStatus");
	expect(openerStatus && openerStatus->text().contains("re-baked") && openerStatus->text().contains("Left out to fit the budget: run."),
		   "the status line explains the re-bake and names what was left out");
	std::cout << checks << " checks, " << failures << " failures\n";
	return failures ? 1 : 0;
}
