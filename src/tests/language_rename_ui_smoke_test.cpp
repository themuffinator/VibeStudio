#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/project_search_panel.h"
#include "app/studio_theme.h"
#include "tests/language_server_fixture.h"

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QInputDialog>
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
		if (QByteArray(context) != "vibestudio::ProjectSearchPanel") { return {}; }
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
	bool approve = false, cancelName = false; int prompts = 0;
	QTimer dialogs; QObject::connect(&dialogs, &QTimer::timeout, &app, [&]() {
		if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) { ++prompts; if (cancelName) { input->reject(); } else { input->setTextValue(QStringLiteral("renamed")); input->accept(); } }
		else if (auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { confirmation->done(approve ? QMessageBox::Apply : QMessageBox::Cancel); }
	}); dialogs.start(25);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path); shell.show(); app.processEvents();
		auto* editor = static_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")));
		auto* language = shell.findChild<CodeLanguagePanel*>(); auto* panel = shell.findChild<ProjectSearchPanel*>(); auto* action = shell.findChild<QAction*>(QStringLiteral("code.renameSymbol"));
		if (!editor || !language || !panel || !action) { return 1; }
		auto* apply = panel->findChild<QPushButton*>(QStringLiteral("projectSearchApply")); if (!apply) { return 1; }
		const auto connect = [&](const QString& mode) { language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }); language->setPreferences(preferences(app.applicationFilePath(), mode)); language->connectServer(); return wait([&]() { return language->client()->diagnostics(path).received; }); };
		const auto rename = [&]() { QTextCursor cursor(editor->document()); cursor.setPosition(5); editor->setTextCursor(cursor); action->trigger(); return wait([&]() { return !panel->busy(); }); };
		action->trigger(); ok &= expect(!panel->busy() && shell.statusBar()->currentMessage().contains(QStringLiteral("Connect")), "offline rename explains provider requirement");
		ok &= expect(action->toolTip().contains(QStringLiteral("F2")) && connect(QStringLiteral("incremental")), "F2 is scoped and remappable; provider connects");
		editor->moveCursor(QTextCursor::End); editor->insertPlainText(QStringLiteral("// unsaved\n")); const QString unsaved = editor->toPlainText();
		ok &= expect(rename() && prompts > 0 && panel->report().semanticRename && panel->report().canApply() && panel->report().buffersScanned == 1 && panel->report().changes.size() == 2 && read(path) == original, "rename prepares a review of unsaved and disk sources before applying");
		panel->applyPreview(); ok &= expect(editor->toPlainText() == unsaved && read(other) == QByteArray("value++;\r\n"), "Cancel at final review leaves every source unchanged");
		approve = true; panel->applyPreview(); ok &= expect(wait([&]() { return !panel->busy(); }) && panel->report().succeeded() && panel->report().editedBuffers.size() == 1 && panel->report().writtenFiles.size() == 1
			&& editor->toPlainText() == QStringLiteral("int renamed;\nrenamed++;\n// unsaved\n") && read(path) == original && read(other) == QByteArray("renamed++;\r\n"), "review applies undoable unsaved edits and atomic unopened-file saves");
		editor->undo(); ok &= expect(editor->toPlainText() == unsaved, "one Undo restores the whole rename while preserving prior author edits");
		write(other, "value++;\r\n"); ok &= expect(rename() && apply->isEnabled(), "new preview can be applied");
		editor->insertPlainText(QStringLiteral("x")); ok &= expect(!apply->isEnabled(), "typing invalidates a completed semantic preview"); editor->undo();
		ok &= expect(connect(QStringLiteral("rename-slow")), "connect delayed rename provider");
		action->trigger(); panel->cancel(); wait([&]() { return false; }, 800); ok &= expect(!panel->busy() && !apply->isEnabled() && editor->toPlainText() == unsaved, "Cancel retires late protocol replies");
		cancelName = true; ok &= expect(rename() && panel->report().cancelled, "name prompt cancellation ends the activity"); cancelName = false;
		ok &= expect(connect(QStringLiteral("rename-resource")) && rename() && !panel->report().succeeded() && !apply->isEnabled() && read(path) == original, "unsupported resource operations block the whole preview");
		ok &= expect(connect(QStringLiteral("incremental")) && rename(), "restore valid preview");
		shell.openPathFromCommandLine(other); editor->moveCursor(QTextCursor::End); editor->insertPlainText(QStringLiteral("// inactive author edit\n"));
		QTextDocument* inactive = editor->document(); const QString inactiveBefore = editor->toPlainText();
		shell.openPathFromCommandLine(path);
		ok &= expect(rename() && panel->report().buffersScanned == 2, "inactive unsaved targets synchronize before rename");
		panel->applyPreview();
		ok &= expect(wait([&]() { return !panel->busy(); }) && panel->report().succeeded() && panel->report().editedBuffers.size() == 2 && panel->report().writtenFiles.isEmpty()
			&& inactive->toPlainText() == QStringLiteral("renamed++;\n// inactive author edit\n") && read(path) == original && read(other) == QByteArray("value++;\r\n"), "both active and inactive targets stay unsaved with their preceding edits");
		editor->undo(); inactive->undo();
		ok &= expect(editor->toPlainText() == unsaved && inactive->toPlainText() == inactiveBefore, "each tab independently undoes the whole rename");
		ok &= expect(rename(), "prepare a final preview for disconnect");
		language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); });
		ok &= expect(!apply->isEnabled(), "disconnect invalidates outstanding preview");
	}
	// Render the actual review widget after releasing the full studio, so global
	// theme changes measure this surface rather than every unrelated module.
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		// Recreate a panel so its labels also exercise expanded translation.
		ProjectSearchPanel review; review.resize(scale == 200 ? 1800 : 1100, scale == 200 ? 1100 : 700); review.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		LanguageRenameRequest request; request.rootPath = temp.path(); request.provider = QStringLiteral("VibeStudio test server"); request.symbol = QStringLiteral("value"); request.newName = QStringLiteral("renamed");
		request.rename.workspaceEdit = QJsonObject {{QStringLiteral("changes"), QJsonObject {{languageServerUri(other), QJsonArray {QJsonObject {{QStringLiteral("range"), QJsonObject {{QStringLiteral("start"), QJsonObject {{QStringLiteral("line"), 0}, {QStringLiteral("character"), 0}}}, {QStringLiteral("end"), QJsonObject {{QStringLiteral("line"), 0}, {QStringLiteral("character"), 5}}}}}, {QStringLiteral("newText"), request.newName}}}}}}};
		const auto token = review.beginReferences(request.rootPath, request.symbol, request.provider, {}, true); review.finishRename(token, request); review.show(); wait([&]() { return !review.busy(); });
		const QString evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) { QImage image(review.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); review.render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("language-rename-%1.png").arg(scale))), "render rename review without OS screen capture"); }
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	dialogs.stop(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
