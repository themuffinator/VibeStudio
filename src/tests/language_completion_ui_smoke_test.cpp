#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/studio_theme.h"
#include "tests/language_server_fixture.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCompleter>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QTranslator>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* text) { if (!condition) { std::cerr << text << '\n'; } return condition; }
bool wait(const std::function<bool()>& ready, int ms = 10000)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); }
	return ready();
}
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
void caret(StudioCodeEditor* editor, int position) { QTextCursor cursor(editor->document()); cursor.setPosition(position); editor->setTextCursor(cursor); }
bool choose(StudioCodeEditor* editor)
{
	const auto index = editor->completer()->completionModel()->index(0, 0);
	return index.isValid() && QMetaObject::invokeMethod(editor->completer(), "activated", Qt::DirectConnection, Q_ARG(QModelIndex, index));
}
QJsonObject preferences(const QString& executable, const QString& mode = QStringLiteral("incremental"))
{
	return {{QStringLiteral("program"), executable}, {QStringLiteral("arguments"), QJsonArray {QStringLiteral("--fixture"), mode}},
		{QStringLiteral("language"), QStringLiteral("cpp")}, {QStringLiteral("extensions"), QStringLiteral("cpp;h")}};
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "VibeStudioCodeEditor") { return {}; }
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
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		StudioCodeEditor editor; editor.resize(scale == 200 ? 1800 : 1000, 450);
		editor.setBaseFont(QFont(QStringLiteral("Consolas"), scale == 200 ? 20 : 10));
		editor.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.setPlainText(QStringLiteral("object.ta")); caret(&editor, 9); editor.show(); app.processEvents();
		auto suggestions = parseLanguageCompletions(QJsonArray {
			QJsonObject {{QStringLiteral("label"), QStringLiteral("targetMember")}, {QStringLiteral("detail"), QStringLiteral("int Object::targetMember")}, {QStringLiteral("documentation"), QStringLiteral("Documentation <b>must stay text</b>.")}},
			QJsonObject {{QStringLiteral("label"), QStringLiteral("targetOld")}, {QStringLiteral("deprecated"), true}}}, editor.toPlainText(), 0, 9);
		bool accepted = false;
		const auto token = editor.beginLanguageCompletions();
		ok &= expect(editor.finishLanguageCompletions(token, suggestions, [&](const auto&) { accepted = true; return true; }), "display semantic suggestions at the unchanged caret");
		app.processEvents();
		const auto index = editor.completer()->completionModel()->index(0, 0);
		ok &= expect(editor.languageCompletionCurrent(token) && index.data().toString().contains(QStringLiteral("int Object::targetMember"))
			&& index.data(Qt::ToolTipRole).toString().contains(QStringLiteral("&lt;b&gt;")) && !index.data(Qt::AccessibleDescriptionRole).toString().isEmpty()
			&& !editor.completer()->popup()->accessibleName().isEmpty(), "suggestions retain signature, safe text documentation and accessible descriptions");
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) {
			auto* popup = editor.completer()->popup(); QImage image(popup->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); popup->render(&image);
			ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("language-completion-%1.png").arg(scale))), "render completion popup at studio scale");
		}
		ok &= expect(choose(&editor) && accepted, "direct list activation accepts the selected semantic item");
		accepted = false; const auto stale = editor.beginLanguageCompletions(); caret(&editor, 8);
		ok &= expect(!editor.finishLanguageCompletions(stale, suggestions, [&](const auto&) { accepted = true; return true; }) && !accepted, "caret changes retire pending suggestions");
		editor.setPlainText(QStringLiteral("object.\U00010400e\u0301")); caret(&editor, int(editor.toPlainText().size()));
		editor.completionsFor = [](const QString& prefix) { return QStringList {prefix + QStringLiteral("Name")}; };
		ok &= expect(editor.showLocalCompletions(), "local fallback offers matching Unicode identifiers");
		editor.dismissLanguageCompletions();
		ok &= expect(editor.completer()->popup()->isVisible() && choose(&editor) && editor.toPlainText() == QStringLiteral("object.\U00010400e\u0301Name"), "retiring language requests preserves local completion and Unicode punctuation");
		editor.undo(); ok &= expect(editor.toPlainText() == QStringLiteral("object.\U00010400e\u0301"), "local completion remains one undo step");
		editor.setReadOnly(true); ok &= expect(!editor.showCompletions(), "read-only documents cannot request editable suggestions");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(app.applicationFilePath())); settings.sync(); }
	const QString path = temp.filePath(QStringLiteral("source.cpp")), second = temp.filePath(QStringLiteral("second.cpp"));
	const QByteArray source = "// fixture\nobject.taSuffix\n";
	ok &= expect(write(path, source) && write(second, "// second\nobject.ta\n"), "write completion workflow fixtures");
	QTimer modalGuard; QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { ok &= expect(false, "unexpected modal dialog during completion test"); dialog->reject(); } }); modalGuard.start(100);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path);
		auto* panel = shell.findChild<CodeLanguagePanel*>();
		auto* editor = dynamic_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")));
		auto* complete = shell.findChild<QAction*>(QStringLiteral("code.complete"));
		if (!panel || !editor || !complete) { return 1; }
		panel->connectServer(); ok &= expect(wait([&]() { return panel->client()->diagnostics(path).received; }), "explicit connection synchronizes completion source");
		caret(editor, 20); complete->trigger();
		ok &= expect(wait([&]() { return editor->completer() && editor->completer()->popup()->isVisible(); }), "Ctrl+Space action exposes server member completions");
		ok &= expect(choose(editor) && editor->toPlainText() == QStringLiteral("// imported\n// fixture\nobject.targetMember\n")
			&& editor->textCursor().position() == editor->toPlainText().indexOf(QStringLiteral("targetMember")) + 12
			&& editor->document()->isModified() && read(path) == source, "accepting a completion applies its replacement and related import to the unsaved document only");
		editor->undo(); ok &= expect(editor->toPlainText().toUtf8() == source, "one undo restores both replacement and import");
		editor->redo(); ok &= expect(editor->toPlainText().contains(QStringLiteral("// imported")), "redo reapplies the complete edit set"); editor->undo();
		panel->client()->stop(); ok &= expect(wait([&]() { return panel->client()->state() == QStringLiteral("stopped"); }), "stop completion provider before delayed checks");
		panel->setPreferences(preferences(app.applicationFilePath(), QStringLiteral("slow"))); panel->connectServer();
		ok &= expect(wait([&]() { return panel->client()->diagnostics(path).received; }), "connect delayed completion provider");
		caret(editor, 20); complete->trigger(); editor->dismissCompletions();
		wait([&]() { return false; }, 500);
		ok &= expect(!editor->completer()->popup()->isVisible() && panel->client()->pendingRequests() == 0 && read(path) == source, "dismissal cancels the request and late replies cannot reopen the list");
		complete->trigger(); caret(editor, 19); wait([&]() { return false; }, 500);
		ok &= expect(!editor->completer()->popup()->isVisible() && editor->textCursor().position() == 19, "moving the caret retires a delayed completion");
		caret(editor, 20); complete->trigger(); editor->insertPlainText(QStringLiteral("x")); wait([&]() { return false; }, 500);
		ok &= expect(!editor->completer()->popup()->isVisible() && !editor->toPlainText().contains(QStringLiteral("imported")), "editing while waiting cannot apply stale related edits"); editor->undo();
		caret(editor, 20); complete->trigger(); shell.openPathFromCommandLine(second); wait([&]() { return false; }, 500);
		ok &= expect(!editor->completer()->popup()->isVisible() && read(second) == QByteArray("// second\nobject.ta\n"), "switching tabs retires suggestions without touching either saved file");
		caret(editor, 19); complete->trigger();
		ok &= expect(panel->client()->pendingRequests() > 0, "shell destruction must retire an outstanding completion safely");
	}
	modalGuard.stop(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
