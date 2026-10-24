// The Mesh Editor's Prepare for Target: the review dialog (fix kinds as
// checkable choices, the changes and what still blocks), applying as one undo
// step, cancelling without change, the Delivery targets menu's Prepare entry,
// choosing a blocker to select its exact faces, and the delivery targets and
// strict setting a project saves. Also under right-to-left layout and 200%
// text.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/studio_theme.h"
#include "core/model_target_prepare.h"
#include "core/studio_settings.h"
#include "tests/model_targets_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
QAction *findAction(QMenu *menu, const QString &name)
{
	for (auto *action : menu->actions())
	{
		if (action->objectName() == name)
			return action;
		if (action->menu())
		{
			if (auto *inner = findAction(action->menu(), name))
				return inner;
		}
	}
	return nullptr;
}
// Runs `handle` on the next modal dialog once it opens (work runs on a worker
// with its own event loop first, so this polls).
void onNextDialog(QObject *context, const QString &name, std::function<void(QDialog *)> handle)
{
	auto *timer = new QTimer(context);
	timer->setInterval(20);
	QObject::connect(timer, &QTimer::timeout, context,
					 [timer, handle, name]
					 {
						 auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
						 if (dialog && dialog->objectName() == name)
						 {
							 timer->stop();
							 timer->deleteLater();
							 handle(dialog);
						 }
					 });
	timer->start();
}

void checkPrepare(ModelEditorDialog *editor)
{
	QString error;
	const auto big = targetMesh({targetGridSurface(QStringLiteral("body"), 60, 40)});
	expect(editor->setMesh(big, &error), "open a surface over Quake III's budget");
	auto *budget = editor->findChild<QToolButton *>(QStringLiteral("meshExportBudget"));
	auto *prepare = budget ? findAction(budget->menu(), QStringLiteral("meshPrepareTarget-md3")) : nullptr;
	expect(prepare && prepare->isEnabled() && !prepare->toolTip().isEmpty(), "the Delivery targets menu offers Prepare for the MD3 target");
	// Cancelling the review changes nothing.
	QString shown;
	bool kindsNamed = false;
	onNextDialog(editor, QStringLiteral("meshPrepareTargetDialog"), [&](QDialog *dialog)
				 {
					 if (auto *text = dialog->findChild<QPlainTextEdit *>(QStringLiteral("meshPrepareTargetReview")))
						 shown = text->toPlainText();
					 auto *split = dialog->findChild<QCheckBox *>(QStringLiteral("meshPrepareFix-split"));
					 kindsNamed = split && split->isChecked() && !split->accessibleName().isEmpty() && !split->accessibleDescription().isEmpty() &&
								  dialog->findChild<QCheckBox *>(QStringLiteral("meshPrepareFix-fit"));
					 dialog->reject();
				 });
	expect(!editor->prepareForTarget(QStringLiteral("md3")) && !editor->document().canUndo() && editor->document().mesh().surfaces.size() == 1,
		   "cancelling the review leaves the model unchanged");
	expect(shown.contains(QStringLiteral("Split")) && kindsNamed, "the review lists the split and offers each fix kind, named and described");
	// Turning off splits reviews a decimation instead; applying is one undo step.
	QString reduced;
	onNextDialog(editor, QStringLiteral("meshPrepareTargetDialog"), [&](QDialog *dialog)
				 {
					 auto *split = dialog->findChild<QCheckBox *>(QStringLiteral("meshPrepareFix-split"));
					 auto *text = dialog->findChild<QPlainTextEdit *>(QStringLiteral("meshPrepareTargetReview"));
					 auto *apply = dialog->findChild<QPushButton *>(QStringLiteral("meshPrepareTargetApply"));
					 if (split)
						 split->setChecked(false);
					 if (text)
						 reduced = text->toPlainText();
					 if (apply && apply->isEnabled())
						 apply->click();
					 else
						 dialog->reject();
				 });
	expect(editor->prepareForTarget(QStringLiteral("md3")) && editor->document().canUndo() && editor->document().mesh().surfaces.size() == 1 &&
			   reduced.contains(QStringLiteral("Reduced")),
		   "with splits off the review reduces the surface, and Apply applies it");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(!editor->document().canUndo() && editor->document().mesh().surfaces[0].triangles.size() == big.surfaces[0].triangles.size(),
		   "one undo restores the whole surface");
	// Without a review: split, one undo step.
	expect(editor->prepareForTarget(QStringLiteral("md3"), false, &error) && editor->document().mesh().surfaces.size() >= 3 &&
			   editor->document().canUndo(),
		   "Prepare for Target splits the surface");
	ModelTargetReport report;
	ModelTargetProfile quake3;
	findModelTargetProfile(QStringLiteral("md3"), {}, &quake3);
	expect(assessModelTarget(editor->document().mesh(), quake3, &report, &error) && report.ready(), "the prepared model is ready for Quake III");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(editor->document().mesh().surfaces.size() == 1 && !editor->document().canUndo(), "and one undo joins it again");
}

void checkExactSelection(ModelEditorDialog *editor)
{
	QString error;
	const auto over = targetMesh({targetGridSurface(QStringLiteral("body"), 40, 25)});
	expect(editor->setMesh(over, &error), "open a surface one vertex over");
	ModelTargetProfile quake3;
	findModelTargetProfile(QStringLiteral("md3"), {}, &quake3);
	ModelTargetReport report;
	expect(assessModelTarget(over, quake3, &report, &error) && !report.issues.isEmpty() && !report.issues.first().faces.isEmpty(), "the blocker names its faces");
	const auto issue = report.issues.first();
	auto *mode = editor->findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
	expect(editor->selectTargetIssue(issue) && mode && mode->currentIndex() == 0 &&
			   editor->document().selection().faces == QSet<int>(issue.faces.cbegin(), issue.faces.cend()) && editor->document().selection().surfaces.isEmpty(),
		   "choosing the blocker selects exactly its faces, in face mode");
	auto *budget = editor->findChild<QToolButton *>(QStringLiteral("meshExportBudget"));
	bool described = false;
	for (auto *action : budget->menu()->actions())
		described |= action->toolTip().contains(QStringLiteral("face(s)")) || action->toolTip().contains(QStringLiteral("faces concerned"));
	expect(described, "the menu entry says it selects the faces concerned");
}

void checkProjectTargets(ModelEditorDialog *editor)
{
	QString error;
	ModelEditorDeliveryContext context;
	context.projectRoot = QStringLiteral("C:/projects/wolf");
	context.project.targets.insert(QStringLiteral("md3"), QStringLiteral("et"));
	context.project.strictSet = true;
	context.project.strict = true;
	context.installation.targets.insert(QStringLiteral("mdl"), QStringLiteral("quakespasm"));
	editor->deliveryContext = [&context] { return context; };
	ModelDeliveryPreferences savedPreferences;
	int saves = 0;
	editor->saveDeliveryPreferences = [&](const ModelDeliveryPreferences &preferences, QString *)
	{
		savedPreferences = preferences;
		++saves;
		return true;
	};
	editor->refreshContext();
	auto *strict = editor->findChild<QCheckBox *>(QStringLiteral("meshStrictExport"));
	expect(editor->deliveryTarget(QStringLiteral("md3")) == QStringLiteral("et") && editor->deliveryTargetOrigin(QStringLiteral("md3")) == ModelDeliveryOrigin::Project &&
			   editor->deliveryTarget(QStringLiteral("mdl")) == QStringLiteral("quakespasm") &&
			   editor->deliveryTargetOrigin(QStringLiteral("mdl")) == ModelDeliveryOrigin::Installation && strict && strict->isChecked(),
		   "an open project's targets and strict setting apply, then its installation's suggestion");
	expect(editor->setMesh(targetMesh({targetGridSurface(QStringLiteral("body"), 32, 32)}), &error) &&
			   editor->findChild<QToolButton *>(QStringLiteral("meshExportBudget"))->text().contains(QStringLiteral("MD3 ready")),
		   "1,024 vertices read ready for the project's Enemy Territory target");
	auto *budget = editor->findChild<QToolButton *>(QStringLiteral("meshExportBudget"));
	auto *menu = findAction(budget->menu(), QStringLiteral("meshTargetMenu-md3"));
	expect(menu && menu->text().contains(QStringLiteral("project")), "the target submenu says the choice is the project's");
	expect(editor->setDeliveryTarget(QStringLiteral("md2"), QStringLiteral("q2pro")) && editor->saveDeliveryTargetsToProject(&error) && saves == 1 &&
			   savedPreferences.targets.value(QStringLiteral("md2")) == QStringLiteral("q2pro") &&
			   savedPreferences.targets.value(QStringLiteral("md3")) == QStringLiteral("et") && savedPreferences.strictSet && savedPreferences.strict,
		   "Save Targets with the Project saves the session's choices with the project's");
	auto *save = findAction(budget->menu(), QStringLiteral("meshSaveDeliveryTargets"));
	expect(save && save->isEnabled(), "the menu offers saving with the open project");
	// A skeletal model offers the MD5 targets too.
	expect(editor->setMesh(exportArmFixture(), &error), "open a skeletal model");
	auto *md5 = findAction(budget->menu(), QStringLiteral("meshTarget-md5mesh-q2pro"));
	expect(md5 && findAction(budget->menu(), QStringLiteral("meshTargetMenu-md5anim")), "a skeletal model offers MD5 mesh and animation targets");
	if (md5)
	{
		md5->trigger();
		QApplication::processEvents();
	}
	expect(editor->deliveryTarget(QStringLiteral("md5mesh")) == QStringLiteral("q2pro") &&
			   editor->deliveryTargetOrigin(QStringLiteral("md5mesh")) == ModelDeliveryOrigin::Explicit,
		   "choosing Q2PRO in the menu sets the MD5 mesh target");
	// The MD5 targets are checked like the others: the header, its menu and
	// Check Delivery Targets.
	bool md5Status = false;
	for (auto *action : budget->menu()->actions())
		md5Status |= action->text().contains(QStringLiteral("MD5 Mesh · Q2PRO"));
	QVector<ModelTargetReport> reports;
	expect(budget->text().contains(QStringLiteral("MD5 ")) && md5Status && editor->checkDeliveryTargets(&reports, false, &error) &&
			   std::any_of(reports.cbegin(), reports.cend(), [](const ModelTargetReport &report) { return report.format == QStringLiteral("md5mesh") && report.target == QStringLiteral("q2pro"); }) &&
			   std::any_of(reports.cbegin(), reports.cend(), [](const ModelTargetReport &report) { return report.format == QStringLiteral("md5anim"); }),
		   "a skeletal model's MD5 mesh and animation targets are summarised and checked");
	context = {};
	editor->refreshContext();
	expect(editor->deliveryTarget(QStringLiteral("md3")) == QStringLiteral("quake3") && !editor->saveDeliveryTargetsToProject(&error) &&
			   error.contains(QStringLiteral("Open a project")),
		   "without a project the defaults apply and nothing can be saved");
	editor->deliveryContext = {};
	editor->saveDeliveryPreferences = {};
	strict->setChecked(false);
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-target-prepare-ui-smoke"); skip >= 0)
		return skip;
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("target-prepare-ui-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		// The second pass mirrors the layout and doubles the text.
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		auto *editor = new ModelEditorDialog;
		editor->resize(1400, 900);
		editor->show();
		QApplication::processEvents();
		checkPrepare(editor);
		checkExactSelection(editor);
		if (scenario == 0)
			checkProjectTargets(editor);
		delete editor;
	}
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "model target preparation UI checks passed\n";
	return EXIT_SUCCESS;
}
