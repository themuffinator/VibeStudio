// The Mesh Editor's delivery targets: the header summary and its menu (target
// choice per format, live counts, Check Delivery Targets), exports refused by
// the selected target with every blocker in the Export Report, and the Quake
// III detail levels' replace confirmation and report, the strict setting, and
// choosing a blocker to select its surface. Also under right-to-left layout
// and 200% text.
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/studio_theme.h"
#include "core/model_targets.h"
#include "core/studio_settings.h"
#include "tests/model_targets_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

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
QByteArray bytesOf(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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
// Runs `handle` on the next modal dialog of type T once it opens. Work runs
// on a worker with its own event loop first, so a single-shot timer could
// fire before the dialog exists; this polls until it does.
template <typename T> void onNextModal(QObject *context, std::function<void(T *)> handle)
{
	auto *timer = new QTimer(context);
	timer->setInterval(20);
	QObject::connect(timer, &QTimer::timeout, context,
					 [timer, handle]
					 {
						 if (auto *dialog = qobject_cast<T *>(QApplication::activeModalWidget()))
						 {
							 timer->stop();
							 timer->deleteLater();
							 handle(dialog);
						 }
					 });
	timer->start();
}
// Answers the next modal message box with `button`, noting its text.
void answerNextBox(QWidget *context, QMessageBox::StandardButton button, QString *text, QString *name)
{
	onNextModal<QMessageBox>(context, [=](QMessageBox *box)
							 {
								 *text = box->text();
								 *name = box->objectName();
								 box->button(button)->click();
							 });
}
// Reads the named text of the next modal dialog, then closes it.
void readNextDialog(QWidget *context, const QString &textName, QString *shown)
{
	onNextModal<QDialog>(context, [=](QDialog *dialog)
						 {
							 if (auto *text = dialog->findChild<QPlainTextEdit *>(textName))
								 *shown = text->toPlainText();
							 dialog->reject();
						 });
}

void checkSummary(ModelEditorDialog *editor, bool scaled)
{
	QString error;
	auto *budget = editor->findChild<QToolButton *>(QStringLiteral("meshExportBudget"));
	if (!expect(budget && budget->menu(), "the header has the delivery targets button"))
		return;
	expect(budget->accessibleName() == QStringLiteral("Delivery targets") && budget->focusPolicy() != Qt::NoFocus &&
			   !budget->accessibleDescription().isEmpty(),
		   "the button is named, described and keyboard reachable");
	expect(editor->setMesh(targetMesh({targetGridSurface(QStringLiteral("body"), 37, 27)}), &error) && budget->text().contains(QStringLiteral("MD3 ready")),
		   "999 vertices read ready for Quake III, in words as well as the icon");
	expect(editor->setMesh(targetMesh({targetGridSurface(QStringLiteral("body"), 40, 25)}), &error) && budget->text().contains(QStringLiteral("MD3 over")),
		   "1,000 vertices read over");
	QAction *issue = nullptr;
	for (auto *action : budget->menu()->actions())
	{
		if (action->text().contains(QStringLiteral("body")) && action->text().contains(QStringLiteral("1000")))
			issue = action;
	}
	expect(issue, "the menu names the surface and its count");
	if (expect(issue && issue->isEnabled() && !issue->toolTip().isEmpty(), "a surface's blocker can be chosen to select what it is about"))
	{
		issue->trigger();
		QApplication::processEvents();
		// The vertex budget names the faces past it, which a split would move.
		expect(!editor->document().selection().faces.isEmpty() && editor->document().selection().surfaces.isEmpty() &&
				   editor->document().selection().surface == 0,
			   "choosing the blocker selects the faces of the surface it is about");
	}
	auto *choice = findAction(budget->menu(), QStringLiteral("meshTarget-md3-ioquake3"));
	if (expect(choice && choice->isCheckable() && !choice->isChecked() && !choice->toolTip().isEmpty(), "the menu offers ioquake3 for MD3"))
	{
		choice->trigger();
		QApplication::processEvents();
		auto *chosen = findAction(budget->menu(), QStringLiteral("meshTarget-md3-ioquake3"));
		expect(editor->deliveryTarget(QStringLiteral("md3")) == QStringLiteral("ioquake3") && chosen && chosen->isChecked(),
			   "choosing a target selects it for MD3 exports and the menu shows it");
	}
	expect(budget->text().contains(QStringLiteral("MD3 over")), "ioquake3 refuses 1,000 vertices too");
	if (scaled)
		expect(budget->sizeHint().width() < editor->width(), "the summary fits the window at 200% text");
	// The full check runs on the worker.
	QVector<ModelTargetReport> reports;
	expect(editor->checkDeliveryTargets(&reports, false, &error) && reports.size() == 3, "Check Delivery Targets assesses MDL, MD2 and MD3");
	bool md3Blocked = false;
	for (const auto &report : reports)
		md3Blocked |= report.format == QStringLiteral("md3") && report.target == QStringLiteral("ioquake3") && !report.ready();
	auto *status = editor->findChild<QLabel *>(QStringLiteral("meshStatus"));
	expect(md3Blocked && status && status->text().contains(QStringLiteral("blockers")), "the check reports the MD3 blocker in the status");
	QString shown;
	readNextDialog(editor, QStringLiteral("meshDeliveryTargetsText"), &shown);
	editor->tools()->action(QStringLiteral("meshCheckTargets"))->trigger();
	expect(shown.contains(QStringLiteral("ioquake3")) && shown.contains(QStringLiteral("body")), "the check dialog lists every target and blocker");
	expect(shown.contains(QStringLiteral("tags per frame")) && !shown.contains(QStringLiteral("tags-per-frame")),
		   "the check dialog names each measure as the header menu does");
	editor->setDeliveryTarget(QStringLiteral("md3"), QStringLiteral("quake3"));
	// The header scans MD3's frames, so a vertex out of MD3's range reads over
	// rather than ready.
	auto far = targetMesh({targetGridSurface(QStringLiteral("body"), 9, 9)});
	far.surfaces[0].frames[0].positions[5].x = 600;
	updateEditableModelMetadata(&far);
	expect(editor->setMesh(far, &error) && budget->text().contains(QStringLiteral("MD3 over")), "a vertex outside MD3's range reads over in the header");
}

void checkBlockedExport(ModelEditorDialog *editor, const QString &directory)
{
	QString error;
	expect(editor->setMesh(targetMesh({targetGridSurface(QStringLiteral("body"), 40, 25), targetGridSurface(QStringLiteral("cape"), 41, 26, 4)}),
						   &error),
		   "open a model over two limits");
	const auto path = QDir(directory).filePath(QStringLiteral("blocked.md3"));
	ModelExportOptions options;
	options.target = editor->deliveryTarget(QStringLiteral("md3"));
	expect(!editor->exportToPath(path, QStringLiteral("md3"), options, &error) && !QFileInfo::exists(path), "the export is refused and writes nothing");
	const auto &report = editor->lastExportReport();
	auto *button = editor->findChild<QPushButton *>(QStringLiteral("meshExportReport"));
	auto *status = editor->findChild<QLabel *>(QStringLiteral("meshStatus"));
	expect(report.target.issues.size() == 3 && button && button->isVisible() && button->text().contains(QStringLiteral("3 blocker")) &&
			   status->text().contains(QStringLiteral("Nothing was written")),
		   "the status and the Export Report button say how many limits block the export");
	QString shown;
	readNextDialog(editor, QStringLiteral("meshExportReportText"), &shown);
	button->click();
	expect(shown.contains(QStringLiteral("Limits exceeded")) && shown.contains(QStringLiteral("body")) && shown.contains(QStringLiteral("cape")) &&
			   shown.contains(QStringLiteral("Separate selected faces")),
		   "the report lists every blocker with its fix");
	// Opening another model forgets the report.
	expect(editor->setMesh(targetMesh({targetGridSurface(QStringLiteral("prop"), 9, 9)}), &error) && !button->isVisible() &&
			   editor->lastExportReport().target.issues.isEmpty(),
		   "another model hides the previous model's export report");
	// Refuse exports that leave data out: a clip rate MD3 cannot hold.
	auto *strict = editor->findChild<QCheckBox *>(QStringLiteral("meshStrictExport"));
	if (!expect(strict && !strict->isChecked() && !strict->accessibleName().isEmpty() && !strict->accessibleDescription().isEmpty() &&
					strict->focusPolicy() != Qt::NoFocus,
				"the Export page has a named, keyboard-reachable strict setting"))
		return;
	auto timed = targetMesh({targetGridSurface(QStringLiteral("prop"), 9, 9)});
	timed.animations = {{QStringLiteral("spin"), 0, 1, 12}};
	expect(editor->setMesh(timed, &error), "open a model whose clip rate MD3 cannot hold");
	options.strict = true;
	const auto strictPath = QDir(directory).filePath(QStringLiteral("strict.md3"));
	expect(!editor->exportToPath(strictPath, QStringLiteral("md3"), options, &error) && !QFileInfo::exists(strictPath) && button->isVisible() &&
			   button->text().contains(QStringLiteral("left out")) && status->text().contains(QStringLiteral("Nothing was written")) &&
			   editor->lastExportReport().omissions.size() == 1,
		   "a strict refusal writes nothing and keeps what it would leave out in the report");
}

void checkDetailLevels(ModelEditorDialog *editor, const QString &directory)
{
	QString error, text, name;
	expect(editor->setMesh(targetMesh({targetGridSurface(QStringLiteral("prop"), 9, 9)}), &error), "open a small prop");
	auto *tools = editor->tools();
	const auto base = QDir(directory).filePath(QStringLiteral("prop.md3"));
	const auto level1 = QDir(directory).filePath(QStringLiteral("prop_1.md3"));
	const auto level2 = QDir(directory).filePath(QStringLiteral("prop_2.md3"));
	QFile handMade(level1);
	expect(handMade.open(QIODevice::WriteOnly) && handMade.write("hand-made") > 0, "a hand-made level 1 sits beside the base name");
	handMade.close();
	answerNextBox(editor, QMessageBox::Cancel, &text, &name);
	expect(!tools->exportDetailLevels(base, 2, 0.5, &error), "cancelling the confirmation exports nothing");
	expect(name == QStringLiteral("meshLodReplaceConfirm") && text.contains(QStringLiteral("prop_1.md3")), "the confirmation names the file it would replace");
	expect(bytesOf(level1) == "hand-made" && !QFileInfo::exists(base) && !QFileInfo::exists(level2), "Cancel leaves the hand-made file and writes nothing");
	answerNextBox(editor, QMessageBox::Yes, &text, &name);
	expect(tools->exportDetailLevels(base, 2, 0.5, &error) && bytesOf(level1).startsWith("IDP3") && QFileInfo::exists(base) &&
			   QFileInfo::exists(level2),
		   "Replace writes the whole set");
	auto *button = editor->findChild<QPushButton *>(QStringLiteral("meshExportReport"));
	expect(button && button->isVisible() && editor->lastExportReport().target.target == QStringLiteral("quake3"),
		   "the detail set's notes go to the Export Report, with the target it was checked against");
	// With Refuse exports that leave data out on, a clip rate MD3 cannot hold
	// stops the whole set.
	auto timed = targetMesh({targetGridSurface(QStringLiteral("prop"), 9, 9)});
	timed.animations = {{QStringLiteral("spin"), 0, 1, 12}};
	auto *strict = editor->findChild<QCheckBox *>(QStringLiteral("meshStrictExport"));
	const auto strictBase = QDir(directory).filePath(QStringLiteral("timed.md3"));
	if (expect(strict && editor->setMesh(timed, &error), "open a model whose clip rate MD3 cannot hold"))
	{
		strict->setChecked(true);
		error.clear();
		expect(!tools->exportDetailLevels(strictBase, 2, 0.5, &error) && error.contains(QStringLiteral("spin")) && !QFileInfo::exists(strictBase),
			   "the strict setting refuses the detail set and writes nothing");
		strict->setChecked(false);
		expect(tools->exportDetailLevels(strictBase, 2, 0.5, &error) && QFileInfo::exists(strictBase) && editor->lastExportReport().omissions.size() == 3,
			   "without it the set is written and each file's omission is reported");
	}
	text.clear();
	expect(!tools->exportDetailLevels(QDir(directory).filePath(QStringLiteral("other.md3")), 3, 0.5, &error) && text.isEmpty() &&
			   error.contains(QStringLiteral("name_2.md3")),
		   "a third level is refused before anything is asked or written");
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// The editor draws in 3D: skip its checks where no renderer starts.
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-targets-ui-smoke"); skip >= 0)
		return skip;
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("targets-ui-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		// The second pass mirrors the layout and doubles the text.
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		const auto directory = QDir(temporary.path()).filePath(QStringLiteral("pass%1").arg(scenario));
		QDir().mkpath(directory);
		auto *editor = new ModelEditorDialog;
		editor->resize(1400, 900);
		editor->show();
		QApplication::processEvents();
		checkSummary(editor, scenario == 1);
		checkBlockedExport(editor, directory);
		if (scenario == 0)
			checkDetailLevels(editor, directory);
		delete editor;
	}
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "model delivery target UI checks passed\n";
	return EXIT_SUCCESS;
}
