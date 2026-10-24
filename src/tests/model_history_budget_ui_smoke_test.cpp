// Source size and undo history in the Mesh Editor (app/model_editor_budget.cpp):
// the summary line in the status strip and the Undo history limit in Model >
// Undo History Limit… with accessible names, the
// setting applied and persisted, the Undo and Redo tooltips, the one-time
// notice when history discards steps, a model opened over the source limit
// (refused growth, allowed shrinking, paused recovery copies), translation
// expansion, right-to-left layout, 200% scale and high-visibility themes, and
// the Animation Curves dialog's bounded draft history.
#include "app/model_animation_curves_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/status_line_label.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_scale_test_helpers.h"
#include "tests/model_skeletal_budget_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty())
			std::cerr << ": " << qPrintable(detail);
		std::cerr << '\n';
	}
	return value;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int n) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor")
			return {};
		auto original = QString::fromUtf8(text);
		original.replace(QStringLiteral("%n"), QString::number(n));
		return QStringLiteral("[%1 %2]").arg(original, QString("~~ ").repeated(original.size() / 3));
	}
};
ModelEdit frameMove(int index, int frame)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Transform;
	edit.frame = frame;
	edit.translation = {0.5f, 0, 0};
	edit.selection.vertices = {index * 7, index * 7 + 1, index * 7 + 2, index * 7 + 3};
	return edit;
}

void history(QApplication &app)
{
	QString error;
	ModelEditorDialog editor;
	editor.resize(1500, 1000);
	editor.show();
	QCoreApplication::processEvents();
	auto *budget = editor.findChild<QLabel *>("meshDocumentBudget");
	auto *limit = editor.findChild<QSpinBox *>("meshHistoryLimit");
	auto *status = editor.findChild<QLabel *>("meshStatus");
	auto *undo = editor.findChild<QAction *>("undoMesh");
	auto *redo = editor.findChild<QAction *>("redoMesh");
	if (!expect(budget && limit && status && undo && redo, "the editor shows the source and history summary and the history limit"))
		return;
	QLabel *buddy = nullptr;
	for (auto *label : editor.findChildren<QLabel *>())
		if (label->buddy() == limit)
			buddy = label;
	expect(!budget->accessibleName().isEmpty() && !limit->accessibleName().isEmpty() && !limit->accessibleDescription().isEmpty() &&
			   (limit->focusPolicy() & Qt::TabFocus) && buddy && !buddy->text().isEmpty(),
		   "the summary and the history limit have accessible names, a description, a label and keyboard focus");
	// The limit is a setting, in the Model menu beside Keep Local Recovery
	// Copies, not in the status strip.
	auto *limitAction = editor.findChild<QAction *>("meshHistoryLimitAction");
	auto *limitWindow = editor.findChild<QDialog *>("meshHistoryLimitDialog");
	auto *modelMenu = editor.findChild<QMenu *>("meshMenuModel");
	expect(limitAction && limitWindow && modelMenu && modelMenu->actions().contains(limitAction) && !limitAction->toolTip().isEmpty() &&
			   limitWindow->isAncestorOf(limit) && dynamic_cast<StatusLineLabel *>(budget),
		   "Model > Undo History Limit… opens the limit, and the summary is a status line");
	expect(limit->value() == 16 && modelDocumentHistoryLimit() == 16LL * 1024 * 1024,
		   "the editor applies the saved history limit to every document");
	if (!expect(editor.setMesh(maximumEditableGrid(), &error), "open the maximum grid", error))
		return;
	expect(budget->text().contains(QStringLiteral("of 64 MiB")) && budget->text().contains(QStringLiteral("Undo:")) &&
			   budget->accessibleDescription().contains(QStringLiteral("of the 64 MiB limit")) &&
			   budget->accessibleDescription().contains(QStringLiteral("Undo history:")) &&
			   budget->toolTip().startsWith(budget->accessibleDescription()),
		   "the summary states the source size against its limit and the undo history, in full for screen readers", budget->text());
	// Each frame-local move holds about 1.7 MiB, so a 16 MiB limit fills.
	int notices = 0;
	for (int i = 0; i < 12; ++i)
	{
		const int discarded = editor.document().discardedHistorySteps();
		if (!expect(editor.applyEdit(frameMove(i, i % 16), &error), "a frame-local move applies through the editor", error))
			return;
		expect(editor.document().historyBytes() <= modelDocumentHistoryLimit(), "the editor's history stays within its limit");
		if (status->text().contains(QStringLiteral("Undo history reached its 16.0 MiB limit")))
		{
			++notices;
			expect(editor.document().discardedHistorySteps() > discarded, "the notice appears when history discards steps");
		}
	}
	expect(notices == 1 && editor.document().discardedHistorySteps() > 0, "the editor says once that history discarded old steps",
		   QString::number(notices));
	expect(budget->accessibleDescription().contains(QStringLiteral("older steps were discarded")), "the summary says older steps were discarded",
		   budget->accessibleDescription());
	expect(undo->toolTip().contains(QStringLiteral("Undo:")) && undo->toolTip().contains(modelMebibytesText(editor.document().historyBytes())),
		   "the Undo tooltip gives the steps available and the history kept", undo->toolTip());
	undo->trigger();
	expect(redo->toolTip().contains(QStringLiteral("Redo:")) && editor.document().canRedo(), "the Redo tooltip gives the steps available",
		   redo->toolTip());
	limit->setValue(32);
	expect(StudioSettings(StudioSettings::AccessMode::ReadOnly).modelHistoryBudgetMiB() == 32 &&
			   modelDocumentHistoryLimit() == 32LL * 1024 * 1024 && budget->text().contains(QStringLiteral("32.0 MiB")),
		   "changing the history limit saves the setting and applies it", budget->text());
	for (int i = 12; i < 24; ++i)
		editor.applyEdit(frameMove(i, i % 16), &error);
	const int kept = editor.document().undoSteps();
	limit->setValue(16);
	expect(editor.document().undoSteps() < kept && editor.document().historyBytes() <= 16LL * 1024 * 1024,
		   "lowering the limit trims the open document at once");
	// Typing a larger limit passes through smaller numbers (2, 25, 256); only
	// the committed value applies, so history is not trimmed on the way.
	limit->setValue(64);
	for (int i = 24; i < 44; ++i)
		editor.applyEdit(frameMove(i, i % 16), &error);
	const int steps = editor.document().undoSteps(), discarded = editor.document().discardedHistorySteps();
	expect(editor.document().historyBytes() > 26LL * 1024 * 1024, "history holds more than an intermediate typed limit",
		   modelMebibytesText(editor.document().historyBytes()));
	const auto type = [&](Qt::Key key, const QString &text)
	{
		QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier, text);
		QCoreApplication::sendEvent(limit, &down);
		QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier, text);
		QCoreApplication::sendEvent(limit, &up);
	};
	if (limitAction)
		limitAction->trigger();
	QCoreApplication::processEvents();
	expect(limitWindow && limitWindow->isVisible() && limit->isVisible(), "Model > Undo History Limit… opens the limit's window");
	limit->setFocus();
	limit->selectAll();
	type(Qt::Key_2, QStringLiteral("2"));
	type(Qt::Key_5, QStringLiteral("5"));
	expect(editor.document().undoSteps() == steps && editor.document().discardedHistorySteps() == discarded &&
			   modelDocumentHistoryLimit() == 64LL * 1024 * 1024 &&
			   StudioSettings(StudioSettings::AccessMode::ReadOnly).modelHistoryBudgetMiB() == 64,
		   "a partly typed limit neither applies nor trims history");
	type(Qt::Key_6, QStringLiteral("6"));
	type(Qt::Key_Return, QStringLiteral("\r"));
	expect(limit->value() == 256 && modelDocumentHistoryLimit() == 256LL * 1024 * 1024 && editor.document().undoSteps() == steps &&
			   editor.document().discardedHistorySteps() == discarded &&
			   StudioSettings(StudioSettings::AccessMode::ReadOnly).modelHistoryBudgetMiB() == 256,
		   "the committed larger limit applies and keeps every step");
	// Committing a lower limit discards at once and says so.
	limit->selectAll();
	type(Qt::Key_2, QStringLiteral("2"));
	type(Qt::Key_0, QStringLiteral("0"));
	type(Qt::Key_Return, QStringLiteral("\r"));
	expect(limit->value() == 20 && editor.document().discardedHistorySteps() > discarded &&
			   editor.document().historyBytes() <= 20LL * 1024 * 1024 && status->text().contains(QStringLiteral("Undo history reached its 20.0 MiB limit")),
		   "a committed lower limit trims history and says so", status->text());
	expect(limitWindow && limitWindow->isVisible(), "Enter commits a typed limit and keeps its window open");
	if (limitWindow)
		limitWindow->close();
	Q_UNUSED(app);
}

void overLimit()
{
	QString error;
	// 400 clips of 45 frames for 64 joints: about 70 MiB of joint matrices alone.
	const auto mesh = realisticSkeletalDocument(400, 45, 0);
	ModelEditorDialog editor;
	editor.resize(1500, 1000);
	editor.show();
	QCoreApplication::processEvents();
	auto *budget = editor.findChild<QLabel *>("meshDocumentBudget");
	auto *recovery = editor.findChild<QLabel *>("meshRecoveryStatus");
	if (!expect(!mesh.surfaces.isEmpty() && budget && recovery && editor.setMesh(mesh, &error), "open a skeletal model over the source limit", error))
		return;
	expect(editor.document().sourceBytes() > modelDocumentMaxSourceBytes && budget->text().contains(QStringLiteral("over the 64 MiB limit")) &&
			   budget->accessibleDescription().contains(QStringLiteral("Saving and recovery copies are unavailable")),
		   "the summary says the source is over the limit and what is unavailable", budget->accessibleDescription());
	// Even a longer clip name grows the source, so it waits until it fits.
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameSkeletalClip;
	rename.skeletal.clip = 5;
	rename.skeletal.name = QStringLiteral("a_much_longer_name_for_the_sixth_clip");
	expect(!editor.applyEdit(rename, &error) && error.contains(QStringLiteral("over the 64 MiB limit")) && !editor.document().canUndo(),
		   "an edit that grows a source over the limit is refused with its size", error);
	ModelEdit remove;
	remove.kind = ModelEditKind::DeleteSkeletalClip;
	remove.skeletal.clip = 399;
	expect(editor.applyEdit(remove, &error) && editor.document().canUndo() && editor.document().isModified(),
		   "an edit that shrinks a source over the limit applies", error);
	editor.checkpointRecovery();
	expect(recovery->text().contains(QStringLiteral("Recovery copies are paused")), "recovery copies pause with the reason", recovery->text());
}

void layouts(QApplication &app)
{
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		Expansion expansion;
		app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0 ? StudioTheme::HighContrastLight : StudioTheme::HighContrastDark,
												UiDensity::Standard, 200));
		{
			ModelEditorDialog editor;
			editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
			editor.resize(2600, 1700);
			editor.show();
			QString error;
			editor.setMesh(maximumEditableGrid(), &error);
			QCoreApplication::processEvents();
			auto *budget = editor.findChild<QLabel *>("meshDocumentBudget");
			auto *limit = editor.findChild<QSpinBox *>("meshHistoryLimit");
			auto *limitAction = editor.findChild<QAction *>("meshHistoryLimitAction");
			auto *limitWindow = editor.findChild<QDialog *>("meshHistoryLimitDialog");
			if (expect(budget && limit && limitAction && limitWindow, "the summary and limit exist under expansion"))
			{
				// One line that elides when painted, so the strip never takes
				// height from the viewport and never widens the window; the
				// whole text stays its text, tooltip and description.
				const auto *line = dynamic_cast<StatusLineLabel *>(budget);
				expect(line && budget->text().contains(QStringLiteral("~~")) && budget->accessibleDescription().contains(QStringLiteral("~~")) &&
						   budget->toolTip().contains(QStringLiteral("~~")) && budget->isVisible() &&
						   budget->height() < 2 * budget->fontMetrics().lineSpacing() &&
						   budget->minimumSizeHint().width() <= 8 * budget->fontMetrics().averageCharWidth() + budget->contentsMargins().left() +
																	  budget->contentsMargins().right() + 2 * budget->frameWidth() + 2,
					   scenario == 0 ? "expanded text stays one line in the status strip at 200% in high contrast"
									 : "expanded text stays one line in the status strip right to left",
					   budget->text());
				limitAction->trigger();
				QCoreApplication::processEvents();
				const QRect spin(limit->mapTo(limitWindow, QPoint()), limit->size());
				expect(limitWindow->isVisible() && limit->isVisible() && spin.left() >= 0 && spin.right() <= limitWindow->width() &&
						   spin.bottom() <= limitWindow->height(),
					   scenario == 0 ? "the limit's window holds the expanded limit at 200% in high contrast"
									 : "the limit's window holds the expanded limit right to left");
				limitWindow->close();
			}
		}
		app.removeTranslator(&expansion);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
}

void curveDrafts()
{
	const auto mesh = realisticSkeletalDocument(1, 30, 1);
	if (!expect(!mesh.surfaces.isEmpty(), "prepare a clip with curves"))
		return;
	ModelAnimationCurvesDialog dialog(mesh, mesh.skeleton.joints[3].name, 1);
	dialog.show();
	QCoreApplication::processEvents();
	auto *value = dialog.findChild<QDoubleSpinBox *>("curvesKeyValue");
	auto *set = dialog.findChild<QPushButton *>("curvesSetKey");
	auto *undo = dialog.findChild<QPushButton *>("curvesUndo");
	auto *discard = dialog.findChild<QPushButton *>("curvesDiscard");
	if (!expect(value && set && undo && discard && set->isEnabled(), "the curves dialog can set keys"))
		return;
	for (int i = 0; i < 300; ++i)
	{
		value->setValue(0.5 + 0.01 * i);
		set->click();
	}
	int undone = 0;
	while (undo->isEnabled() && undone < 400)
	{
		undo->click();
		++undone;
	}
	expect(undone == 255 && discard->isEnabled(), "the curves dialog keeps its 256 newest drafts and still knows the stored curve",
		   QString::number(undone));
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
	QTemporaryDir temporary(QDir(root).filePath("history-budget-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const auto settings = QDir(temporary.path()).filePath("settings.ini");
	{
		StudioSettings saved(settings);
		saved.setModelHistoryBudgetMiB(16);
		saved.setModelRecoveryEnabled(true);
		saved.sync();
	}
	StudioSettings::setOverrideFilePath(settings);
	// Sizes in the editor follow the locale; keep the expected text fixed.
	QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
	// The settings override also keeps recovery copies in the temporary folder.
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	history(app);
	overLimit();
	layouts(app);
	curveDrafts();
	if (failures == 0)
		std::cout << "model history budget ui smoke passed\n";
	return failures == 0 ? 0 : 1;
}
