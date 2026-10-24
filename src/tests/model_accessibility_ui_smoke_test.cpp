// What a screen reader hears from the modeller, and how its previews follow
// the high-contrast and reduced-motion preferences.
//
// Status lines, sidebar summaries and dialog statuses keep their fixed
// accessible names and carry what they say in their accessible description.
// Results and refusals are announced once (through the studio's announcement
// listener, which stands in for a screen reader here; offscreen none is
// listening), while counts after an edit, readouts while typing and a modal
// tool's refused live preview are not (its refusal is heard on confirming).
// A collision map export is heard as a result and a refused placement as a
// problem. The delivery targets describe each format and announce a change,
// the curve graph names the selected key and the component table names a
// face's corners. This runs in the dark theme at 100% text, and in high-contrast
// light at 200% text, right to left, with every tools string longer. A
// recovery copy that keeps failing is announced once. The curve graph keeps
// a screen reader's value within the channel's range, which the curves
// dialog's value field shares.
//
// Then, with the HighContrastDark theme and reduced motion recorded as the
// shell records them, every modeller dialog's preview starts in high contrast
// and still, Space does not start playback, the Mesh Editor's Play / Pause
// button is off and says why, its Play or Pause command and Space answer with
// the reason, and the busy bars of a model operation and the recovery scan
// stand still.
#include "app/live_status_label.h"
#include "app/model_animation_curve_view.h"
#include "app/model_animation_curves_dialog.h"
#include "app/model_curve_keys_dialog.h"
#include "app/model_curve_transfer_dialog.h"
#include "app/model_design_dialog.h"
#include "app/model_document_work.h"
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_ik_dialog.h"
#include "app/model_recovery_dialog.h"
#include "app/model_skinning_dialog.h"
#include "app/model_viewport.h"
#include "app/model_weight_paint_dialog.h"
#include "app/model_weight_transfer_dialog.h"
#include "app/studio_accessibility.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_collision_animation_test_helpers.h"
#include "tests/model_curve_keys_test_helpers.h"
#include "tests/model_ik_test_helpers.h"
#include "tests/model_rip_test_helpers.h"
#include "tests/model_spin_test_helpers.h"
#include "tests/model_targets_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include "tests/model_weight_transfer_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAbstractItemModel>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>

using namespace vibestudio;
namespace
{
int failures = 0;
bool expect(bool ok, const std::string &message, const QString &detail = {})
{
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty())
			std::cerr << ": " << detail.toUtf8().constData();
		std::cerr << '\n';
	}
	return ok;
}
// Every tools and editor string a third longer, in brackets.
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		const QByteArray name(context);
		if (name != "VibeStudioModelTools" && name != "VibeStudioModelEditor")
			return {};
		const auto s = QString::fromUtf8(text);
		return QStringLiteral("[%1 %2]").arg(s, QStringLiteral("~~ ").repeated(s.size() / 6));
	}
};
struct Heard
{
	QString message;
	LiveStatus kind = LiveStatus::Update;
};
QVector<Heard> heard;
// Announcements settle when the event loop next runs.
void settle()
{
	for (int pass = 0; pass < 3; ++pass)
		QCoreApplication::processEvents();
}
QString heardText()
{
	QStringList lines;
	for (const auto &entry : std::as_const(heard))
		lines << QStringLiteral("%1 %2").arg(int(entry.kind)).arg(entry.message);
	return lines.join(QStringLiteral(" | "));
}
// A result, or the same result of an operation slow enough (here, under
// load) to count as a long task that finished.
bool polite(LiveStatus kind)
{
	return kind == LiveStatus::Result || kind == LiveStatus::Finished;
}
// The label's fixed name, and what it says as its description.
bool readsItsText(QLabel *label)
{
	auto *access = label ? QAccessible::queryAccessibleInterface(label) : nullptr;
	return access && !label->accessibleName().isEmpty() && access->text(QAccessible::Name) == label->accessibleName() &&
		   (label->text().isEmpty() || access->text(QAccessible::Name) != label->text()) &&
		   access->text(QAccessible::Description) == (label->text().isEmpty() ? label->toolTip() : label->text());
}
void keyPress(QWidget *widget, int key)
{
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(widget, &release);
}

void checkEditorAnnouncements(const std::string &variant, const QDir &directory)
{
	const auto file = [&](const char *name) { return directory.filePath(QString::fromUtf8(variant.c_str()) + QLatin1Char('-') + QString::fromLatin1(name)); };
	// As the installed translator gives it.
	const QString ripRefused = QCoreApplication::translate("VibeStudioModelTools", "Rip refused");
	QString error;
	ModelEditorDialog editor;
	editor.setAttribute(Qt::WA_DeleteOnClose, false);
	editor.resize(1600, 1000);
	editor.show();
	settle();
	auto *status = editor.findChild<QLabel *>(QStringLiteral("meshStatus"));
	auto *context = editor.findChild<QLabel *>(QStringLiteral("meshContext"));
	auto *view = editor.findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *tool = editor.tools();
	if (!expect(status && context && view && tool && editor.setMesh(tests::ripGrid(), &error), variant + ": the editor opens the grid", error))
		return;
	settle();
	heard.clear();
	expect(readsItsText(status) && !status->text().isEmpty(), variant + ": the status line keeps its name and reads its text",
		   QAccessible::queryAccessibleInterface(status)->text(QAccessible::Description));
	expect(readsItsText(context), variant + ": the project context line reads its text");
	for (const char *name : {"meshCollisionSummary", "meshSkeletonSummary", "meshTagSummary", "meshMaterialStatus", "meshRecoveryStatus"})
	{
		auto *label = editor.findChild<QLabel *>(QString::fromLatin1(name));
		expect(dynamic_cast<LiveStatusLabel *>(label) && readsItsText(label), variant + ": " + name + " keeps its name and reads its text");
	}

	// A quick edit changes the line, and is not announced.
	ModelEdit edit;
	edit.selection.faces = {0};
	edit.translation = {1, 0, 0};
	expect(editor.applyEdit(edit, &error), variant + ": a quick edit applies", error);
	settle();
	expect(readsItsText(status) && heard.isEmpty(), variant + ": a quick edit is read on the line, not announced", heardText());

	// A refused transform interrupts once, however often it is repeated.
	tool->applySelection(ModelSelection{});
	const QPointF centre = QRectF(view->rect()).center();
	expect(!tool->beginModal(ModelModalOperator::Move, centre), variant + ": moving nothing is refused");
	settle();
	expect(!tool->beginModal(ModelModalOperator::Move, centre), variant + ": and refused again");
	settle();
	expect(heard.size() == 1 && heard[0].kind == LiveStatus::Problem && heard[0].message == status->text().simplified() && readsItsText(status),
		   variant + ": the refusal is announced once, assertively, and read on the line", heardText());

	// A refused rip says why.
	heard.clear();
	expect(!tool->beginModal(ModelModalOperator::Rip, centre), variant + ": ripping nothing is refused");
	settle();
	expect(heard.size() == 1 && heard[0].kind == LiveStatus::Problem && heard[0].message.contains(ripRefused) &&
			   heard[0].message.size() > ripRefused.size() + 4,
		   variant + ": the rip refusal is announced with its reason", heardText());

	// A modal operator's prompt is announced as it starts; typing is not.
	heard.clear();
	ModelSelection one;
	one.vertices = {0};
	tool->applySelection(one);
	settle();
	heard.clear();
	expect(tool->beginModal(ModelModalOperator::Move, centre), variant + ": a move starts");
	settle();
	const auto prompt = heard;
	for (const auto *typed : {"1", "2", ".", "5"})
	{
		tool->typeModal(QString::fromLatin1(typed));
		settle();
	}
	expect(prompt.size() == 1 && prompt[0].kind == LiveStatus::Result && heard.size() == 1,
		   variant + ": the move's prompt is announced once and typed values are not", heardText());
	tool->finishModal(false);
	settle();

	// An edit the document refuses is announced as a problem.
	heard.clear();
	ModelEdit bad;
	bad.selection.faces = {9999};
	bad.translation = {1, 0, 0};
	expect(!editor.applyEdit(bad, &error) && !error.isEmpty(), variant + ": an edit of a face that does not exist is refused");
	settle();
	expect(heard.size() == 1 && heard[0].kind == LiveStatus::Problem && heard[0].message == error.simplified(),
		   variant + ": the refused edit is announced", heardText());

	// An export and a save are announced as results.
	heard.clear();
	expect(editor.exportToPath(file("grid.obj"), QStringLiteral("obj"), ModelExportOptions{}, &error),
		   variant + ": the grid exports", error);
	settle();
	expect(heard.size() == 1 && polite(heard[0].kind) && heard[0].message == status->text().simplified(),
		   variant + ": the export is announced as it reads on the line", heardText());
	const auto source = file("grid.mesh.json");
	QFile output(source);
	expect(output.open(QIODevice::WriteOnly) && output.write(QJsonDocument(editableModelJson(tests::ripGrid())).toJson()) > 0,
		   variant + ": write an editable source");
	output.close();
	{
		// A fresh editor, since opening over unsaved edits asks first.
		ModelEditorDialog saving;
		saving.setAttribute(Qt::WA_DeleteOnClose, false);
		expect(saving.openSource(source, &error) && saving.applyEdit(edit, &error), variant + ": open and edit the source", error);
		settle();
		heard.clear();
		auto *save = saving.findChild<QAction *>(QStringLiteral("saveMesh"));
		if (expect(save != nullptr, variant + ": the editor has Save"))
			save->trigger();
		settle();
		expect(!saving.document().isModified() && heard.size() == 1 && polite(heard[0].kind) &&
				   heard[0].message.contains(QStringLiteral("grid.mesh.json")),
			   variant + ": the save is announced", heardText());
	}

	// A collision map export is announced as a result, as the model's own
	// exports are, and a placement the level refuses as a problem, with its
	// reason.
	if (expect(editor.setMesh(tests::collisionAnimationFixture(), &error), variant + ": open the collision fixture", error))
	{
		settle();
		heard.clear();
		expect(editor.exportCollision(file("collision.map"), {QStringLiteral("quake2"), {}, {}, 0}, false, &error),
			   variant + ": the collision map exports", error);
		settle();
		expect(heard.size() == 1 && polite(heard[0].kind) && heard[0].message == status->text().simplified(),
			   variant + ": the collision export is announced as it reads on the line", heardText());
		auto *place = editor.findChild<QPushButton *>(QStringLiteral("placeMeshCollision"));
		// A level is open, but nothing can be written to it.
		editor.context = [&file] { return ModelDesignContext{{}, file("level.map"), false, false, true}; };
		editor.collisionDestination = [] { return ModelCollisionDestination{LevelMapDocument{}, {}}; };
		editor.refreshContext();
		settle();
		heard.clear();
		if (expect(place && place->isEnabled(), variant + ": Place Collision in Level is offered with a level open"))
			place->click();
		settle();
		expect(heard.size() == 1 && heard[0].kind == LiveStatus::Problem && !status->text().isEmpty() &&
				   heard[0].message == status->text().simplified(),
			   variant + ": a placement the level refuses is announced, assertively, as it reads on the line", heardText());
		editor.collisionDestination = {};
		editor.context = {};
		editor.refreshContext();
	}

	// The delivery targets describe each format and announce a change.
	auto *budget = editor.findChild<QToolButton *>(QStringLiteral("meshExportBudget"));
	expect(budget && editor.setMesh(tests::targetMesh({tests::targetGridSurface(QStringLiteral("body"), 37, 27)}), &error),
		   variant + ": open 999 vertices", error);
	settle();
	heard.clear();
	const auto ready = budget ? budget->accessibleDescription() : QString();
	expect(ready.contains(QStringLiteral("MD3")) && ready.contains(QStringLiteral("MD2")) && ready != budget->text(),
		   variant + ": the delivery targets describe each format", ready);
	expect(editor.setMesh(tests::targetMesh({tests::targetGridSurface(QStringLiteral("body"), 40, 25)}), &error),
		   variant + ": open 1,000 vertices", error);
	settle();
	bool over = false;
	for (const auto &entry : std::as_const(heard))
		over |= entry.kind == LiveStatus::Result && entry.message.contains(budget->text());
	expect(over && budget->accessibleDescription() != ready, variant + ": the move over MD3's limit is announced", heardText());

	// A face's corners are named.
	auto *table = editor.findChild<QTableView *>(QStringLiteral("meshComponents"));
	auto *mode = editor.findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
	if (expect(table && mode, variant + ": the component table exists"))
	{
		mode->setCurrentIndex(0);
		settle();
		const auto *model = table->model();
		const auto corner = [model](int section, int role) { return model->headerData(section, Qt::Horizontal, role).toString(); };
		expect(corner(1, Qt::DisplayRole) == QStringLiteral("A") && !corner(1, Qt::AccessibleTextRole).isEmpty() &&
				   corner(1, Qt::AccessibleTextRole) == corner(1, Qt::ToolTipRole) &&
				   corner(1, Qt::AccessibleTextRole) != corner(3, Qt::AccessibleTextRole),
			   variant + ": the face columns say which vertex each holds", corner(1, Qt::AccessibleTextRole));
	}

	// A modal tool's live preview runs at every typed digit or pointer move
	// and is not announced, even when refused; confirming the refused tool
	// announces its refusal once.
	if (expect(editor.setMesh(tests::spinProfile(), &error), variant + ": open the spin profile", error))
	{
		tool->applySelection(tests::spinEdit().selection);
		tool->setCursor3d({0, 0, 0});
		settle();
		heard.clear();
		expect(tool->beginModal(ModelModalOperator::Spin, centre + QPointF(60, 0)), variant + ": a spin starts");
		tool->constrainModal(2, false);
		settle();
		const auto started = heard.size();
		// 361 degrees is more than a turn, which the spin refuses.
		for (const auto *typed : {"3", "6", "1"})
		{
			tool->typeModal(QString::fromLatin1(typed));
			settle();
		}
		expect(!tool->modalPreviewValid() && heard.size() == started, variant + ": a refused spin preview is not announced while typing",
			   heardText());
		const QString spinRefused = QCoreApplication::translate("VibeStudioModelTools", "Spin refused");
		expect(!tool->finishModal(true, &error) && !error.isEmpty(), variant + ": the refused spin does not apply");
		settle();
		expect(heard.size() == started + 1 && heard.last().kind == LiveStatus::Problem && heard.last().message.contains(spinRefused),
			   variant + ": confirming it announces the refusal once", heardText());
	}
}

// A recovery copy that cannot be written is announced once, however often
// the timer tries again; the line keeps saying so.
void checkRecoveryFailure(const QDir &directory)
{
	const auto settings = StudioSettings::overrideFilePath();
	const QDir blocked(directory.filePath(QStringLiteral("blocked-recovery")));
	// A file where the recovery folder belongs.
	QFile placeholder(blocked.filePath(QStringLiteral("model-recovery")));
	if (!expect(QDir().mkpath(blocked.path()) && placeholder.open(QIODevice::WriteOnly) && placeholder.write("x") == 1,
				"recovery: put a file where the recovery folder belongs"))
		return;
	placeholder.close();
	StudioSettings::setOverrideFilePath(blocked.filePath(QStringLiteral("settings.ini")));
	{
		QString error;
		ModelEditorDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		auto *line = dynamic_cast<LiveStatusLabel *>(editor.findChild<QLabel *>(QStringLiteral("meshRecoveryStatus")));
		const QString failed = QCoreApplication::translate("VibeStudioModelEditor", "Recovery failed: %1").arg(QString());
		if (!expect(line && editor.setMesh(tests::ripGrid(), &error), "recovery: the editor opens the grid", error))
			return;
		settle();
		heard.clear();
		ModelEdit edit;
		edit.selection.faces = {0};
		edit.translation = {1, 0, 0};
		bool reported = true;
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			line->clear();
			// The second edit's operation checkpoints the first, which fails.
			expect(editor.applyEdit(edit, &error) && editor.applyEdit(edit, &error), "recovery: two edits apply", error);
			QElapsedTimer waiting;
			waiting.start();
			while (!line->text().startsWith(failed) && waiting.elapsed() < 20000)
			{
				QThread::msleep(10);
				QCoreApplication::processEvents();
			}
			reported &= line->text().startsWith(failed);
			// Past the two seconds in which the same words are never repeated,
			// so only the editor can hold the second failure back.
			while (waiting.elapsed() < 2200)
				QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
			settle();
		}
		int problems = 0;
		for (const auto &entry : std::as_const(heard))
			problems += entry.kind == LiveStatus::Problem && entry.message.startsWith(failed.simplified());
		expect(reported && problems == 1 && readsItsText(line), "recovery: a failure that repeats is announced once and stays on its line",
			   heardText());
	}
	StudioSettings::setOverrideFilePath(settings);
}

void checkCurveGraph()
{
	ModelAnimationCurveView graph;
	graph.resize(420, 300);
	graph.show();
	ModelPoseCurveKey first, second;
	second.frame = 12;
	second.value = 0.5;
	second.interpolation = ModelCurveInterpolation::Cubic;
	second.tangent = ModelCurveTangent::Free;
	graph.setCurve({first, second}, 24, 0, 1);
	settle();
	auto *access = QAccessible::queryAccessibleInterface(&graph);
	if (!expect(access && access->valueInterface(), "the curve graph is accessible"))
		return;
	const auto name = access->text(QAccessible::Name);
	const auto details = access->text(QAccessible::Description);
	expect(name.startsWith(graph.accessibleName()) && name.contains(QStringLiteral("key 2 of 2, frame 12")), "the graph names the selected key", name);
	expect(details.contains(QStringLiteral("value 0.5, cubic curve, free tangents")) && details.endsWith(graph.accessibleDescription()),
		   "the graph describes the key before its help", details);
	expect(access->text(QAccessible::Value) == QStringLiteral("0.5") &&
			   access->valueInterface()->maximumValue().toDouble() == graph.maximumValue() &&
			   access->valueInterface()->minimumValue().toDouble() == graph.minimumValue(),
		   "the graph's value and range come from the widget");
	graph.setFocus();
	keyPress(&graph, Qt::Key_Left);
	expect(graph.selectedKey() == 0 && access->text(QAccessible::Name).contains(QStringLiteral("key 1 of 2, frame 0")),
		   "Left moves to the first key, and the name follows", access->text(QAccessible::Name));

	// What a screen reader sets stays within the channel's range: a
	// translation takes a negative value, and a scale stays above zero.
	ModelPoseCurveKey changed;
	graph.edited = [&changed](int, const ModelPoseCurveKey &key) { changed = key; };
	access->valueInterface()->setCurrentValue(-5.0);
	expect(changed.value == -5.0 && access->valueInterface()->minimumValue().toDouble() < -5.0,
		   "a translation key takes a negative value from a screen reader");
	access->valueInterface()->setCurrentValue(1.0e12);
	expect(changed.value == access->valueInterface()->maximumValue().toDouble() && changed.value < 1.0e12,
		   "a value past the graph's range is held at its maximum");
	graph.setCurve({first, second}, 24, 6, 1);
	settle();
	access->valueInterface()->setCurrentValue(-5.0);
	expect(access->valueInterface()->minimumValue().toDouble() > 0 && changed.value > 0 &&
			   changed.value == access->valueInterface()->minimumValue().toDouble(),
		   "a scale key stays above zero, as its range says", QString::number(changed.value));
}

// The curves dialog's value field takes the range the graph reports.
void checkCurveRange()
{
	ModelAnimationCurvesDialog dialog(tests::curveKeysFixture(), QStringLiteral("upper"), 0);
	dialog.show();
	settle();
	auto *field = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("curvesKeyValue"));
	auto *channel = dialog.findChild<QComboBox *>(QStringLiteral("curvesChannel"));
	ModelAnimationCurveView *graph = nullptr;
	for (auto *widget : dialog.findChildren<QWidget *>())
		graph = graph ? graph : dynamic_cast<ModelAnimationCurveView *>(widget);
	if (!expect(field && channel && graph && channel->count() > 6, "curves: the dialog has its value field, channels and graph"))
		return;
	for (const int index : {0, 6})
	{
		channel->setCurrentIndex(index);
		settle();
		auto *access = QAccessible::queryAccessibleInterface(graph);
		const double low = access && access->valueInterface() ? access->valueInterface()->minimumValue().toDouble() : 0;
		const double high = access && access->valueInterface() ? access->valueInterface()->maximumValue().toDouble() : 0;
		expect(std::abs(field->minimum() - low) < 1e-12 && std::abs(field->maximum() - high) < 1e-12 && (index < 6 || low > 0),
			   "curves: the value field and the graph's accessible range agree",
			   QStringLiteral("%1..%2 against %3..%4").arg(field->minimum()).arg(field->maximum()).arg(low).arg(high));
	}
}

void checkDialogStatuses()
{
	const auto rig = tests::ikFixture();
	const auto curves = tests::curveKeysFixture();
	const ModelSelection whole{0, {}, {0}};
	std::vector<std::pair<const char *, std::unique_ptr<QDialog>>> dialogs;
	dialogs.emplace_back("prop-designer", std::make_unique<ModelDesignDialog>());
	dialogs.emplace_back("skinning", std::make_unique<ModelSkinningDialog>(rig, whole, QStringLiteral("upper"), 0));
	dialogs.emplace_back("ik", std::make_unique<ModelIkDialog>(rig, QStringLiteral("hand"), 0));
	dialogs.emplace_back("curves", std::make_unique<ModelAnimationCurvesDialog>(curves, QStringLiteral("upper"), 0));
	for (auto &[name, dialog] : dialogs)
	{
		dialog->show();
		settle();
		int live = 0;
		for (auto *label : dialog->findChildren<QLabel *>())
		{
			if (!dynamic_cast<LiveStatusLabel *>(label))
				continue;
			++live;
			expect(readsItsText(label), std::string(name) + ": its status keeps its name and reads its text",
				   label->accessibleName() + QStringLiteral(" / ") + label->text());
		}
		expect(live > 0, std::string(name) + ": the dialog has a live status line");
		dialog->hide();
	}
}

// Every dialog's preview follows the recorded preferences, with no plumbing.
void checkPreviews(QApplication &app)
{
	AccessibilityPreferences preferences;
	preferences.theme = StudioTheme::HighContrastDark;
	preferences.reducedMotion = true;
	recordStudioAccessibilityPreferences(preferences);
	applyStudioTheme(app, studioThemeTokens(preferences));
	const auto rig = tests::ikFixture();
	const auto curves = tests::curveKeysFixture();
	const ModelSelection whole{0, {}, {0}};
	std::vector<std::pair<const char *, std::unique_ptr<QDialog>>> dialogs;
	dialogs.emplace_back("prop-designer", std::make_unique<ModelDesignDialog>());
	dialogs.emplace_back("curves", std::make_unique<ModelAnimationCurvesDialog>(curves, QStringLiteral("upper"), 0));
	dialogs.emplace_back("curve-keys", std::make_unique<ModelCurveKeysDialog>(curves, QStringLiteral("upper"), 0));
	dialogs.emplace_back("curve-transfer", std::make_unique<ModelCurveTransferDialog>(curves, QStringLiteral("upper"), 0));
	dialogs.emplace_back("ik", std::make_unique<ModelIkDialog>(rig, QStringLiteral("hand"), 0));
	dialogs.emplace_back("skinning", std::make_unique<ModelSkinningDialog>(rig, whole, QStringLiteral("upper"), 0));
	dialogs.emplace_back("weight-paint", std::make_unique<ModelWeightPaintDialog>(rig, whole, QStringLiteral("upper"), 0));
	dialogs.emplace_back("weight-transfer", std::make_unique<ModelWeightTransferDialog>(tests::transferFixture(), whole, 0));
	for (auto &[name, dialog] : dialogs)
	{
		dialog->show();
		settle();
		const auto previews = dialog->findChildren<ModelViewport *>();
		expect(!previews.isEmpty(), std::string(name) + ": the dialog has a preview");
		for (auto *preview : previews)
		{
			expect(preview->highContrast() && preview->reducedMotion(), std::string(name) + ": its preview is in high contrast and still");
			preview->setFocus();
			keyPress(preview, Qt::Key_Space);
			expect(!preview->isPlaying(), std::string(name) + ": Space does not start playback");
		}
		dialog->hide();
	}

	// The Mesh Editor's own views and transport.
	{
		ModelEditorDialog editor;
		editor.resize(1600, 1000);
		editor.show();
		settle();
		auto *preview = editor.findChild<ModelViewport *>(QStringLiteral("meshPreview"));
		auto *play = editor.findChild<QAction *>(QStringLiteral("meshTimelinePlay"));
		auto *shortcut = editor.tools() ? editor.tools()->action(QStringLiteral("meshPlay")) : nullptr;
		auto *status = editor.findChild<QLabel *>(QStringLiteral("meshStatus"));
		expect(preview && preview->highContrast() && preview->reducedMotion(), "editor: the view starts in high contrast and still");
		expect(play && shortcut && !play->isEnabled() && play->toolTip().contains(QStringLiteral("reduced motion")) &&
				   shortcut->toolTip().contains(QStringLiteral("reduced motion")),
			   "editor: the Play / Pause button is off and says why", play ? play->toolTip() : QString());
		// The Play or Pause command (Blender's Space, 3ds Max's /, command
		// search) and Space in the view answer with the reason instead of
		// doing nothing.
		QString error;
		auto clip = tests::ripGrid();
		clip.frames << clip.frames.first();
		clip.frames.last().name = QStringLiteral("frame001");
		for (auto &surface : clip.surfaces)
			surface.frames << surface.frames.first();
		updateEditableModelMetadata(&clip);
		if (preview && shortcut && status && expect(editor.setMesh(clip, &error), "editor: open a two-frame clip", error))
		{
			int refusals = 0;
			QObject::connect(preview, &ModelViewport::playbackRefused, &editor, [&refusals] { ++refusals; });
			settle();
			heard.clear();
			shortcut->trigger();
			settle();
			expect(!preview->isPlaying() && refusals == 1 && status->text().contains(QStringLiteral("reduced motion")) && heard.size() == 1 &&
					   heard[0].kind == LiveStatus::Problem && heard[0].message == status->text().simplified(),
				   "editor: Play or Pause says why nothing plays, on the line and to the screen reader", heardText());
			preview->setFocus();
			keyPress(preview, Qt::Key_Space);
			settle();
			expect(!preview->isPlaying() && refusals == 2, "editor: Space in the view says why too");
		}
		editor.setAccessibility(false, false);
		bool moving = play && play->isEnabled() && shortcut && shortcut->isEnabled();
		for (auto *pane : editor.panes())
			moving &= !pane || !pane->reducedMotion();
		expect(moving, "editor: turning reduced motion off restores playback in every view");
	}

	// A model operation whose length is unknown, and the recovery scan.
	const auto busyBar = [](const QString &name, int *maximum, bool *seen, bool shown = true)
	{
		for (auto *widget : QApplication::allWidgets())
		{
			if (auto *bar = qobject_cast<QProgressBar *>(widget); bar && bar->objectName() == name && (bar->isVisible() || !shown))
			{
				*maximum = bar->maximum();
				*seen = true;
			}
		}
	};
	const auto operation = [&](bool reduced)
	{
		AccessibilityPreferences recorded;
		recorded.reducedMotion = reduced;
		recordStudioAccessibilityPreferences(recorded);
		int maximum = -1;
		bool seen = false;
		QTimer probe;
		QObject::connect(&probe, &QTimer::timeout, [&] { busyBar(QStringLiteral("meshOperationProgress"), &maximum, &seen); });
		probe.start(50);
		QString error;
		QWidget parent;
		runModelTask(
			&parent, QStringLiteral("Wait"),
			[](QString *, const ModelWorkControl &control)
			{
				for (int i = 0; i < 40 && !(control.cancelled && control.cancelled()); ++i)
					QThread::msleep(20);
				return true;
			},
			&error);
		probe.stop();
		return std::pair{seen, maximum};
	};
	const auto [stillSeen, stillMaximum] = operation(true);
	const auto [movingSeen, movingMaximum] = operation(false);
	expect(stillSeen && stillMaximum > 0 && movingSeen && movingMaximum == 0,
		   "an operation of unknown length shows a still bar under reduced motion and a moving one otherwise",
		   QStringLiteral("%1 %2").arg(stillMaximum).arg(movingMaximum));
	recordStudioAccessibilityPreferences(preferences);
	QTemporaryDir empty;
	int recoveryMaximum = -1;
	bool recoverySeen = false;
	QTimer closer;
	QObject::connect(&closer, &QTimer::timeout,
					 [&]
					 {
						 busyBar(QStringLiteral("modelRecoveryProgress"), &recoveryMaximum, &recoverySeen, false);
						 for (auto *widget : QApplication::topLevelWidgets())
						 {
							 auto *box = widget->isVisible() ? widget->findChild<QDialogButtonBox *>() : nullptr;
							 if (box && box->button(QDialogButtonBox::Cancel) && widget->findChild<QProgressBar *>(QStringLiteral("modelRecoveryProgress")))
								 box->button(QDialogButtonBox::Cancel)->click();
						 }
					 });
	closer.start(50);
	const auto chosen = chooseModelRecovery(nullptr, empty.path());
	closer.stop();
	expect(!chosen && recoverySeen && recoveryMaximum > 0, "the recovery scan's bar stands still under reduced motion",
		   QString::number(recoveryMaximum));
	recordStudioAccessibilityPreferences(AccessibilityPreferences{});
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = test_support::exitCodeWithoutRenderer("model-accessibility-ui-smoke"); skip >= 0)
		return skip;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-accessibility-ui-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const QDir directory(temporary.path());
	StudioSettings::setOverrideFilePath(directory.filePath(QStringLiteral("settings.ini")));
	setStatusAnnouncementListener([](const QString &message, LiveStatus kind) { heard.append({message, kind}); });

	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	std::cout << "Mesh Editor, dark, 100%\n";
	checkEditorAnnouncements("dark-100", directory);
	std::cout << "A recovery copy that keeps failing\n";
	checkRecoveryFailure(directory);
	{
		Expansion expansion;
		app.installTranslator(&expansion);
		app.setLayoutDirection(Qt::RightToLeft);
		applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastLight, UiDensity::Standard, 200));
		std::cout << "Mesh Editor, high-contrast light, 200%, right to left, longer strings\n";
		checkEditorAnnouncements("contrast-light-200-rtl", directory);
		app.setLayoutDirection(Qt::LeftToRight);
		app.removeTranslator(&expansion);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	std::cout << "Curve graph and dialog statuses\n";
	checkCurveGraph();
	checkCurveRange();
	checkDialogStatuses();
	std::cout << "Previews and busy bars under high contrast and reduced motion\n";
	checkPreviews(app);
	setStatusAnnouncementListener({});
	if (failures)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "Model accessibility UI smoke passed.\n";
	return EXIT_SUCCESS;
}
