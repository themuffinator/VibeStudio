#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/studio_theme.h"
#include "tests/language_server_fixture.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* why) { if (!value) { std::cerr << why << '\n'; } return value; }
bool wait(const std::function<bool()>& ready, int ms = 10000) { QElapsedTimer clock; clock.start(); while (!ready() && clock.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready(); }
void select(StudioCodeEditor* editor, int start, int length = 0) { QTextCursor cursor(editor->document()); cursor.setPosition(start); cursor.setPosition(start + length, QTextCursor::KeepAnchor); editor->setTextCursor(cursor); }
bool insert(StudioCodeEditor* editor, const QString& body)
{
	editor->setPlainText(QStringLiteral("t")); select(editor, 1);
	const auto list = parseLanguageCompletions(QJsonArray {QJsonObject {{QStringLiteral("label"), QStringLiteral("target")}, {QStringLiteral("insertTextFormat"), 2}, {QStringLiteral("insertText"), body}}}, editor->toPlainText(), 0, 1);
	return list.items.size() == 1 && editor->applyLanguageCompletion(list.items[0]);
}
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		if (QByteArray(context) != "VibeStudioCodeEditor" && QByteArray(context) != "vibestudio::ApplicationShell") { return {}; }
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
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		StudioCodeEditor editor; editor.resize(scale == 200 ? 1200 : 840, 320); editor.setBaseFont(QFont(QStringLiteral("Consolas"), scale == 200 ? 20 : 10));
		editor.setAccessibleDescription(QStringLiteral("Original editor description")); editor.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); editor.show();
		const QString body = QStringLiteral("call(${1:value}, $1, ${2|left,right|})$0;");
		if (!expect(insert(&editor, body), "insert a structured completion through the editor service")) { return 1; }
		ok &= expect(editor.snippetActive() && editor.textCursor().selectedText() == QStringLiteral("value") && editor.accessibleDescription().contains(QStringLiteral("Shift+Tab")), "first placeholder is selected and exposes keyboard instructions");
		editor.replaceSnippetSelection(QStringLiteral("🌍"));
		ok &= expect(editor.toPlainText() == QStringLiteral("call(🌍, 🌍, left);") && editor.snippetActive(), "typing replaces every linked occurrence without splitting Unicode");
		editor.undo(); ok &= expect(editor.toPlainText() == QStringLiteral("call(value, value, left);") && !editor.snippetActive(), "Undo restores the typed value and all mirrors together, ending the session");
		editor.undo(); ok &= expect(editor.toPlainText() == QStringLiteral("t"), "a second Undo restores the original completion source");
		editor.redo(); editor.redo(); ok &= expect(editor.toPlainText() == QStringLiteral("call(🌍, 🌍, left);") && !editor.snippetActive(), "Redo restores text without reviving stale fields");
		ok &= expect(insert(&editor, body), "restore fields for ordinary completion integration"); editor.replaceSnippetSelection(QStringLiteral("le"));
		const auto plain = parseLanguageCompletions(QJsonArray {QJsonObject {{QStringLiteral("label"), QStringLiteral("leftHand")}}}, editor.toPlainText(), 0, editor.textCursor().position());
		ok &= expect(plain.items.size() == 1 && editor.applyLanguageCompletion(plain.items[0]) && editor.snippetActive()
			&& editor.toPlainText() == QStringLiteral("call(leftHand, leftHand, left);"), "ordinary completion inside a linked field updates every occurrence");
		ok &= expect(insert(&editor, body), "restore fields for local completion integration"); editor.replaceSnippetSelection(QStringLiteral("le"));
		editor.completionsFor = [](const QString&) { return QStringList {QStringLiteral("leftLocal")}; };
		ok &= expect(editor.showLocalCompletions(), "local completion remains available inside linked fields");
		const auto localChoice = editor.completer()->completionModel()->index(0, 0);
		QMetaObject::invokeMethod(editor.completer(), "activated", Qt::DirectConnection, Q_ARG(QModelIndex, localChoice));
		ok &= expect(editor.snippetActive() && editor.toPlainText() == QStringLiteral("call(leftLocal, leftLocal, left);"), "local completion inside a linked field updates every occurrence");
		editor.completionsFor = {};
		ok &= expect(insert(&editor, body) && editor.navigateSnippet(1) && editor.snippetChoices().size() == 2 && editor.chooseSnippetValue(1)
			&& editor.toPlainText() == QStringLiteral("call(value, value, right);") && editor.textCursor().selectedText() == QStringLiteral("right"), "native choices replace the complete active field");
		ok &= expect(editor.navigateSnippet(-1) && editor.textCursor().selectedText() == QStringLiteral("value"), "backward navigation selects the preceding field");
		select(&editor, editor.toPlainText().lastIndexOf(QStringLiteral("value")), 5); editor.replaceSnippetSelection(QStringLiteral("x"));
		ok &= expect(editor.toPlainText() == QStringLiteral("call(x, x, right);") && editor.textCursor().position() == 9, "editing a later mirror preserves its caret as earlier occurrences shrink");
		editor.navigateSnippet(1); editor.navigateSnippet(1);
		ok &= expect(!editor.snippetActive() && editor.textCursor().position() == editor.toPlainText().indexOf(QLatin1Char(';'))
			&& editor.accessibleDescription() == QStringLiteral("Original editor description"), "last Tab reaches the explicit final stop and restores accessible metadata");
		ok &= expect(insert(&editor, QStringLiteral("${1:pair(${2:name})} + $1$0")) && editor.navigateSnippet(1), "navigate into nested linked fields");
		editor.replaceSnippetSelection(QStringLiteral("actor"));
		ok &= expect(editor.toPlainText() == QStringLiteral("pair(actor) + pair(actor)"), "nested fields update inside every parent mirror");
		editor.navigateSnippet(-1); editor.replaceSnippetSelection(QStringLiteral("replacement")); editor.navigateSnippet(1);
		ok &= expect(editor.toPlainText() == QStringLiteral("replacement + replacement") && !editor.snippetActive(), "replacing a parent retires its nested fields");
		ok &= expect(insert(&editor, QStringLiteral("$1$2$0")), "adjacent empty fields are usable"); editor.replaceSnippetSelection(QStringLiteral("a")); editor.navigateSnippet(1); editor.replaceSnippetSelection(QStringLiteral("b")); editor.navigateSnippet(1);
		ok &= expect(editor.toPlainText() == QStringLiteral("ab") && editor.textCursor().position() == 2 && !editor.snippetActive(), "empty tab stops retain insertion order and final position");
		ok &= expect(insert(&editor, QStringLiteral("${0:body}")) && !editor.snippetActive() && editor.textCursor().selectedText() == QStringLiteral("body"), "a final placeholder keeps its default selected after the session ends");
		ok &= expect(insert(&editor, body), "restore fields for external-edit guard"); QTextCursor external(editor.document()); external.insertText(QStringLiteral("// external\n"));
		ok &= expect(!editor.snippetActive() && editor.toPlainText().contains(QStringLiteral("value, value")), "external edits retire fields without introducing hidden mirrored writes");
		ok &= expect(insert(&editor, body), "restore fields for mirrored-paste bounds"); editor.replaceSnippetSelection(QString(65537, QLatin1Char('x')));
		ok &= expect(!editor.snippetActive() && editor.toPlainText().contains(QStringLiteral(", value, left")) && editor.snippetStatus().contains(QStringLiteral("size limit")), "excessive mirrored edits keep user input and report why linking ended");
		ok &= expect(insert(&editor, body), "restore fields for read-only guard"); editor.setReadOnly(true); const auto readonlyText = editor.toPlainText(); editor.replaceSnippetSelection(QStringLiteral("forbidden"));
		ok &= expect(!editor.snippetActive() && editor.toPlainText() == readonlyText, "read-only changes retire the session and reject field replacement"); editor.setReadOnly(false);
		ok &= expect(insert(&editor, body), "restore fields for rendering"); editor.navigateSnippet(1); app.processEvents();
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) { QImage image(editor.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); editor.render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("snippet-fields-%1.png").arg(scale))), "render snippet field emphasis without OS capture"); }
		auto* replacement = new QTextDocument(&editor); replacement->setDocumentLayout(new QPlainTextDocumentLayout(replacement)); editor.setDocument(replacement);
		ok &= expect(!editor.snippetActive() && !editor.navigateSnippet(1), "document changes cannot reuse old tab stop offsets");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	if (app.arguments().contains(QStringLiteral("--editor-only"))) { return ok ? 0 : 1; }
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	QTemporaryDir temp; if (!temp.isValid()) { return 1; } const QString path = temp.filePath(QStringLiteral("source.cpp")), second = temp.filePath(QStringLiteral("second.cpp"));
	const QByteArray source = "// snippet\r\nobject.ta\r\n"; ok &= expect(write(path, source) && write(second, "// second\n"), "write isolated snippet fixtures");
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences({{QStringLiteral("program"), app.applicationFilePath()},
		{QStringLiteral("arguments"), QJsonArray {QStringLiteral("--fixture"), QStringLiteral("resolve-completion-snippet")}}, {QStringLiteral("language"), QStringLiteral("cpp")}, {QStringLiteral("extensions"), QStringLiteral("cpp")}}); settings.sync(); }
	QTimer modalGuard; QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { ok &= expect(false, "unexpected modal during snippet workflow"); dialog->reject(); } }); modalGuard.start(100);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path); shell.show(); app.processEvents();
		auto* editor = dynamic_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"))); auto* language = shell.findChild<CodeLanguagePanel*>();
		auto* complete = shell.findChild<QAction*>(QStringLiteral("code.complete")); auto* bar = shell.findChild<QWidget*>(QStringLiteral("codeSnippetBar"));
		if (!editor || !language || !complete || !bar) { return 1; }
		language->connectServer(); if (!expect(wait([&]() { return language->client()->diagnostics(path).received; }), "connect a provider that requires snippet capability")) { return 1; }
		select(editor, editor->toPlainText().indexOf(QStringLiteral("ta")) + 2); complete->trigger();
		if (!expect(wait([&]() { return editor->completer() && editor->completer()->popup()->isVisible(); }), "show deferred snippet completion")) { return 1; }
		const auto choice = editor->completer()->completionModel()->index(0, 0); QMetaObject::invokeMethod(editor->completer(), "activated", Qt::DirectConnection, Q_ARG(QModelIndex, choice));
		ok &= expect(wait([&]() { return editor->snippetActive(); }) && bar->isVisible() && editor->toPlainText().startsWith(QStringLiteral("// resolved import\n"))
			&& editor->textCursor().selectedText() == QStringLiteral("value") && read(path) == source, "resolved imports and snippet fields share one unsaved insertion");
		bar->findChild<QToolButton*>(QStringLiteral("codeSnippetNext"))->click(); auto* choices = bar->findChild<QComboBox*>(QStringLiteral("codeSnippetChoices"));
		ok &= expect(choices->isVisible() && choices->count() == 2 && !choices->accessibleName().isEmpty(), "field choices use an accessible native selector");
		QMetaObject::invokeMethod(choices, "activated", Qt::DirectConnection, Q_ARG(int, 1));
		ok &= expect(editor->toPlainText().contains(QStringLiteral("value, value, right")), "the native selector replaces the active snippet field");
		for (const int scale : {100, 200}) {
			Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
			applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
			bar->findChild<QToolButton*>(QStringLiteral("codeSnippetPrevious"))->setText(QCoreApplication::translate("vibestudio::ApplicationShell", "Previous"));
			bar->findChild<QToolButton*>(QStringLiteral("codeSnippetNext"))->setText(QCoreApplication::translate("vibestudio::ApplicationShell", "Next"));
			bar->findChild<QPushButton*>(QStringLiteral("codeSnippetFinish"))->setText(QCoreApplication::translate("vibestudio::ApplicationShell", "Finish"));
			bar->setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); editor->navigateSnippet(-1); editor->navigateSnippet(1); app.processEvents();
			const auto evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
			if (!evidence.isEmpty()) { QImage image(bar->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); bar->render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("snippet-controls-%1.png").arg(scale))), "render native snippet controls at studio scale"); }
			if (scale == 200) {
				bar->setParent(nullptr); bar->resize(600, bar->sizeHint().height()); bar->show(); app.processEvents();
				ok &= expect(bar->findChild<QToolButton*>(QStringLiteral("codeSnippetPrevious"))->text().startsWith(QStringLiteral("Previous"))
					&& bar->findChild<QToolButton*>(QStringLiteral("codeSnippetNext"))->text().startsWith(QStringLiteral("Next")), "field navigation uses visible theme-scaled text controls");
				if (!evidence.isEmpty()) { QImage image(bar->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); bar->render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("snippet-controls-200-narrow.png"))), "render expanded RTL snippet controls in a narrow pane"); }
				bar->hide(); bar->setParent(&shell);
			}
			if (scale == 200) { app.removeTranslator(&translator); }
		}
		shell.openPathFromCommandLine(second); ok &= expect(!editor->snippetActive() && bar->isHidden() && read(path) == source, "switching tabs retires fields and preserves saved source");
	}
	modalGuard.stop(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
