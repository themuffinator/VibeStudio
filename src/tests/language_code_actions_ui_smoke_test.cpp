#include "app/application_shell.h"
#include "app/code_actions_dialog.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/project_search_panel.h"
#include "app/studio_theme.h"
#include "tests/language_server_fixture.h"
#include <QAction>
#include <QApplication>
#include <QImage>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QTranslator>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* why) { if (!condition) { std::cerr << why << '\n'; } return condition; }
bool wait(const std::function<bool()>& ready, int ms = 8000)
{
	QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready();
}
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QJsonObject preferences(const QString& exe, const QString& mode)
{
	return {{QStringLiteral("program"), exe}, {QStringLiteral("arguments"), QJsonArray {QStringLiteral("--fixture"), mode}}, {QStringLiteral("language"), QStringLiteral("cpp")}, {QStringLiteral("extensions"), QStringLiteral("cpp;h")}};
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		if (QByteArray(context) != "CodeActionsDialog" && QByteArray(context) != "vibestudio::ProjectSearchPanel") { return {}; }
		const auto text = QString::fromUtf8(source); return text + QStringLiteral(" ~").repeated(text.size() / 6);
	}
};
}
int main(int argc, char** argv)
{
	if (argc > 1 && QByteArray(argv[1]) == "--fixture") { QCoreApplication app(argc, argv); return runLanguageServerFixture(app.arguments().value(2)); }
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); bool ok = true;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp, settingsRoot; if (!temp.isValid() || !settingsRoot.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")), other = temp.filePath(QStringLiteral("other.cpp")); const QByteArray original = "int value;\r\nvalue++;\r\n";
	write(path, original); write(other, "value++;\r\n");
	StudioSettings::setOverrideFilePath(settingsRoot.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(app.applicationFilePath(), QStringLiteral("incremental"))); settings.sync(); }
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool approve = false, cancelChoice = false, staleChoice = false; int selected = 0, prompts = 0;
	StudioCodeEditor* editor = nullptr; LanguageCodeActions renderActions; LanguageWorkspaceEditRequest renderRequest;
	QTimer dialogs; QObject::connect(&dialogs, &QTimer::timeout, &app, [&]() {
		if (auto* dialog = dynamic_cast<CodeActionsDialog*>(QApplication::activeModalWidget())) {
			++prompts;
			if (staleChoice) { staleChoice = false; if (editor) { editor->insertPlainText(QStringLiteral("x")); } return; }
			if (cancelChoice) { dialog->reject(); } else { dialog->findChild<QListWidget*>(QStringLiteral("codeActionsList"))->setCurrentRow(selected); dialog->accept(); }
		} else if (auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { confirmation->done(approve ? QMessageBox::Apply : QMessageBox::Cancel); }
	}); dialogs.start(25);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path); shell.show(); app.processEvents();
		editor = static_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")));
		auto* language = shell.findChild<CodeLanguagePanel*>(); auto* panel = shell.findChild<ProjectSearchPanel*>(); auto* action = shell.findChild<QAction*>(QStringLiteral("code.codeActions"));
		if (!editor || !language || !panel || !action) { return 1; }
		auto* apply = panel->findChild<QPushButton*>(QStringLiteral("projectSearchApply")); if (!apply) { return 1; }
		const auto connect = [&](const QString& mode) { language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }); language->setPreferences(preferences(app.applicationFilePath(), mode)); language->connectServer(); return wait([&]() { return language->client()->diagnostics(path).received; }); };
		const auto query = [&]() { QTextCursor cursor(editor->document()); cursor.setPosition(4); cursor.setPosition(9, QTextCursor::KeepAnchor); editor->setTextCursor(cursor); action->trigger(); return wait([&]() { return !panel->busy(); }); };
		action->trigger(); ok &= expect(!panel->busy() && shell.statusBar()->currentMessage().contains(QStringLiteral("Connect")), "offline code actions explain the provider requirement");
		ok &= expect(action->toolTip().contains(QStringLiteral("Ctrl+.")) && connect(QStringLiteral("incremental")), "Code Actions have a scoped remappable shortcut");
		editor->moveCursor(QTextCursor::End); editor->insertPlainText(QStringLiteral("// author edit\n")); const QString unsaved = editor->toPlainText();
		ok &= expect(query() && prompts > 0 && panel->report().codeActionTitle == QStringLiteral("Fix value usage") && panel->report().canApply() && panel->report().changes.size() == 2 && read(path) == original, "selection action prepares review from unsaved and disk sources without writing");
		panel->applyPreview(); ok &= expect(editor->toPlainText() == unsaved && read(other) == QByteArray("value++;\r\n"), "final review Cancel leaves every source untouched");
		approve = true; panel->applyPreview();
		ok &= expect(wait([&]() { return !panel->busy(); }) && panel->report().succeeded() && panel->report().editedBuffers.size() == 1 && panel->report().writtenFiles.size() == 1
			&& editor->toPlainText() == QStringLiteral("int fixedValue;\nfixedValue++;\n// author edit\n") && read(path) == original && read(other) == QByteArray("fixedValue++;\r\n"), "code action applies undoable unsaved edits and atomic disk saves");
		editor->undo(); ok &= expect(editor->toPlainText() == unsaved, "one Undo restores the action while keeping prior author edits"); write(other, "value++;\r\n");
		selected = 1; ok &= expect(query() && panel->report().codeActionTitle == QStringLiteral("Resolve value refactoring") && apply->isEnabled(), "lazy resolution feeds the same review flow");
		editor->insertPlainText(QStringLiteral("x")); ok &= expect(!apply->isEnabled(), "typing invalidates completed action previews"); editor->undo();
		cancelChoice = true; ok &= expect(query() && panel->report().cancelled, "action picker cancellation completes the activity"); cancelChoice = false;
		staleChoice = true; ok &= expect(query() && !apply->isEnabled(), "source changes while choosing an action invalidate its preview"); editor->undo();
		ok &= expect(connect(QStringLiteral("actions-resolve-slow")), "connect delayed resolution provider");
		QTimer cancelResolve; cancelResolve.setSingleShot(true); QObject::connect(&cancelResolve, &QTimer::timeout, panel, &ProjectSearchPanel::cancel); cancelResolve.start(150); action->trigger();
		wait([&]() { return !panel->busy(); }); wait([] { return false; }, 450); ok &= expect(!apply->isEnabled() && editor->toPlainText() == unsaved, "cancellation retires lazy resolution without editing");
		selected = 0; ok &= expect(connect(QStringLiteral("actions-resource")) && query() && !panel->report().succeeded() && !apply->isEnabled(), "unsupported resource operations block the complete edit plan");
		ok &= expect(connect(QStringLiteral("actions-skipped")) && query() && !panel->report().succeeded() && !apply->isEnabled(), "entirely malformed lists report failure rather than a successful empty preview");
		ok &= expect(connect(QStringLiteral("incremental")) && query(), "restore valid action preview");
		bool captured = false; language->client()->codeActions(path, 4, 5, [&](const auto& result) { renderActions = result; captured = true; }); wait([&]() { return captured; });
		ok &= expect(captured && !renderActions.items.isEmpty(), "capture actual provider result for widget rendering");
		if (renderActions.items.isEmpty()) { return 1; }
		renderRequest.rootPath = temp.path(); renderRequest.provider = language->client()->serverName(); renderRequest.title = renderActions.items[0].title; renderRequest.findText = renderRequest.title;
		renderRequest.workspaceEdit = renderActions.items[0].wire.value(QStringLiteral("edit")); renderRequest.versions.insert(path, renderActions.version);
		language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }); ok &= expect(!apply->isEnabled(), "disconnect invalidates a completed action preview");
	}
	editor = nullptr; dialogs.stop();
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		CodeActionsDialog picker(renderActions, renderRequest.provider); picker.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); picker.resize(scale == 200 ? 1280 : 700, scale == 200 ? 900 : 480); picker.show(); app.processEvents();
		auto* list = picker.findChild<QListWidget*>(QStringLiteral("codeActionsList")); auto* preview = picker.findChild<QPushButton*>(QStringLiteral("codeActionPreview"));
		ok &= expect(list && preview && !list->accessibleName().isEmpty() && list->focusPolicy() != Qt::NoFocus && !preview->accessibleName().isEmpty(), "picker exposes keyboard focus and accessible names");
		list->setCurrentRow(3); picker.accept(); ok &= expect(!preview->isEnabled() && picker.result() != QDialog::Accepted && !list->currentItem()->data(Qt::AccessibleDescriptionRole).toString().isEmpty(), "unavailable entries explain their reason and cannot be accepted"); list->setCurrentRow(0);
		const QString evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		const auto render = [&](QWidget& widget, const QString& name) { if (!evidence.isEmpty()) { QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); widget.render(&image); ok &= expect(image.save(QDir(evidence).filePath(name)), "render actual action widgets without OS capture"); } };
		render(picker, QStringLiteral("language-code-actions-picker-%1.png").arg(scale));
		ProjectSearchPanel review; review.resize(scale == 200 ? 1800 : 1100, scale == 200 ? 1100 : 700); review.setLayoutDirection(picker.layoutDirection());
		const auto token = review.beginCodeActions(renderRequest.rootPath, renderRequest.provider, {}); review.finishLanguageEdits(token, renderRequest); review.show(); wait([&]() { return !review.busy(); });
		ok &= expect(review.report().canApply(), "isolated action review retains validated edit ranges"); render(review, QStringLiteral("language-code-actions-review-%1.png").arg(scale));
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
