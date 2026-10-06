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
#include <QImage>
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
void caret(StudioCodeEditor* editor, int position) { QTextCursor cursor(editor->document()); cursor.setPosition(position); editor->setTextCursor(cursor); }
bool choose(StudioCodeEditor* editor, int row = 0)
{
	const auto index = editor->completer()->completionModel()->index(row, 0);
	return index.isValid() && QMetaObject::invokeMethod(editor->completer(), "activated", Qt::DirectConnection, Q_ARG(QModelIndex, index));
}
QJsonObject preferences(const QString& exe, const QString& mode)
{
	return {{QStringLiteral("program"), exe}, {QStringLiteral("arguments"), QJsonArray {QStringLiteral("--fixture"), mode}}, {QStringLiteral("language"), QStringLiteral("cpp")}, {QStringLiteral("extensions"), QStringLiteral("cpp;h")}};
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
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
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp, settingsRoot; if (!temp.isValid() || !settingsRoot.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")), other = temp.filePath(QStringLiteral("other.cpp")); const QByteArray original = "// fixture\r\nobject.taSuffix\r\n";
	write(path, original); write(other, "// other\nobject.ta\n");
	StudioSettings::setOverrideFilePath(settingsRoot.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(app.applicationFilePath(), QStringLiteral("resolve-completion-slow"))); settings.sync(); }
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	QTimer modalGuard; QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() {
		if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { std::cerr << "Unexpected modal: " << qPrintable(dialog->windowTitle()) << '\n'; ok = false; dialog->reject(); }
	}); modalGuard.start(100);
	if (!app.arguments().contains(QStringLiteral("--popup-only"))) {
		ApplicationShell shell; shell.openPathFromCommandLine(path); shell.show(); app.processEvents();
		auto* editor = dynamic_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")));
		auto* language = shell.findChild<CodeLanguagePanel*>(); auto* action = shell.findChild<QAction*>(QStringLiteral("code.complete"));
		if (!editor || !language || !action) { return 1; }
		const auto connect = [&](const QString& mode) { language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }); language->setPreferences(preferences(app.applicationFilePath(), mode)); language->connectServer(); return wait([&]() { return language->client()->diagnostics(path).received; }); };
		const auto query = [&]() { caret(editor, 20); action->trigger(); return wait([&]() { return editor->completer() && editor->completer()->popup()->isVisible(); }); };
		ok &= expect(connect(QStringLiteral("resolve-completion-slow")) && query(), "explicit connection exposes deferred suggestions");
		ok &= expect(editor->completer()->completionModel()->index(0, 0).data().toString().contains(QStringLiteral("resolving")) && language->client()->pendingRequests() > 0, "highlight resolves with visible loading state");
		ok &= expect(choose(editor), "accept the pending suggestion"); editor->completer()->popup()->hide(); app.processEvents();
		ok &= expect(wait([&]() { return editor->toPlainText().contains(QStringLiteral("resolved import")); }) && editor->toPlainText() == QStringLiteral("// resolved import\n// fixture\nobject.targetMember\n")
			&& read(path) == original && editor->document()->isModified(), "early acceptance waits for related edits and applies only to the unsaved document");
		editor->undo(); ok &= expect(editor->toPlainText() == QStringLiteral("// fixture\nobject.taSuffix\n"), "one Undo restores primary insertion and deferred import");
		editor->redo(); ok &= expect(editor->toPlainText().contains(QStringLiteral("resolved import")), "Redo reapplies the full resolved set"); editor->undo();
		ok &= expect(query(), "start resolve for cancellation"); editor->dismissCompletions(); wait([] { return false; }, 450);
		ok &= expect(!editor->completer()->popup()->isVisible() && language->client()->pendingRequests() == 0 && !editor->toPlainText().contains(QStringLiteral("resolved import")), "dismissal cancels resolve and ignores late replies");
		ok &= expect(query(), "start resolve before caret change"); caret(editor, 19); wait([] { return false; }, 450);
		ok &= expect(!editor->completer()->popup()->isVisible() && editor->textCursor().position() == 19, "caret movement invalidates resolution");
		ok &= expect(query(), "start resolve before typing"); choose(editor); editor->insertPlainText(QStringLiteral("x")); wait([] { return false; }, 450);
		ok &= expect(editor->toPlainText().contains(QStringLiteral("taxSuffix")) && !editor->toPlainText().contains(QStringLiteral("resolved import")), "typing after early acceptance never receives stale related edits"); editor->undo();
		ok &= expect(connect(QStringLiteral("resolve-completion-identity")) && query(), "connect invalid resolving provider");
		wait([&]() { return language->client()->pendingRequests() == 0; }); choose(editor);
		ok &= expect(editor->toPlainText() == QStringLiteral("// fixture\nobject.taSuffix\n") && shell.statusBar()->currentMessage().contains(QStringLiteral("identity")), "identity changes make the suggestion unavailable without partial insertion"); editor->dismissCompletions();
		ok &= expect(connect(QStringLiteral("resolve-completion-slow")) && query(), "start resolve before tab switch"); choose(editor); shell.openPathFromCommandLine(other); wait([] { return false; }, 450);
		ok &= expect(editor->toPlainText() == QStringLiteral("// other\nobject.ta\n") && read(path) == original && read(other) == QByteArray("// other\nobject.ta\n"), "switching tabs retires pending acceptance without editing either file");
		language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); });
	}
	modalGuard.stop();
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		StudioCodeEditor editor; editor.resize(scale == 200 ? 1800 : 1100, 450); editor.setBaseFont(QFont(QStringLiteral("Consolas"), scale == 200 ? 20 : 10));
		editor.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); editor.setPlainText(QStringLiteral("object.ta")); caret(&editor, 9); editor.show(); app.processEvents();
		const auto list = parseLanguageCompletions(QJsonArray {QJsonObject {{QStringLiteral("label"), QStringLiteral("targetA")}}, QJsonObject {{QStringLiteral("label"), QStringLiteral("targetB")}}}, editor.toPlainText(), 0, 9, true);
		int requested = -1, cancelled = 0; bool accepted = false; editor.languageCompletionCancelled = [&]() { ++cancelled; };
		const auto token = editor.beginLanguageCompletions();
		editor.finishLanguageCompletions(token, list, [&](const auto& item) { accepted = !item.needsResolve; return accepted; }, [&](int choice, const auto&) { requested = choice; });
		ok &= expect(requested == 0 && !editor.completer()->popup()->accessibleName().isEmpty(), "first visible suggestion resolves automatically with accessible popup metadata");
		const QString evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) { QImage image(editor.completer()->popup()->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); editor.completer()->popup()->render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("completion-resolve-%1.png").arg(scale))), "render resolving popup without OS capture"); }
		const auto second = editor.completer()->completionModel()->index(1, 0); editor.completer()->popup()->setCurrentIndex(second);
		QMetaObject::invokeMethod(editor.completer(), "highlighted", Qt::DirectConnection, Q_ARG(QModelIndex, second));
		const auto first = editor.completer()->completionModel()->index(0, 0); editor.completer()->popup()->setCurrentIndex(first);
		QMetaObject::invokeMethod(editor.completer(), "highlighted", Qt::DirectConnection, Q_ARG(QModelIndex, first)); app.processEvents();
		ok &= expect(requested == 0 && cancelled == 0, "rapid highlight changes resolve only the latest selection");
		editor.completer()->popup()->setCurrentIndex(second);
		QMetaObject::invokeMethod(editor.completer(), "highlighted", Qt::DirectConnection, Q_ARG(QModelIndex, second));
		ok &= expect(wait([&]() { return requested == 1; }) && cancelled > 0, "moving through suggestions cancels the prior resolve");
		QString error; auto wire = list.items[1].wire; wire.insert(QStringLiteral("documentation"), QStringLiteral("Docs <b>remain literal</b>"));
		const auto resolved = resolveLanguageCompletion(list.items[1], wire, editor.toPlainText(), &error);
		ok &= expect(!editor.finishLanguageCompletionResolve(token, 0, resolved, {}) && editor.finishLanguageCompletionResolve(token, 1, resolved, error), "late metadata cannot overwrite a different selection");
		ok &= expect(editor.completer()->popup()->currentIndex().row() == 1, "resolved metadata preserves the highlighted suggestion");
		ok &= expect(editor.completer()->completionModel()->index(1, 0).data(Qt::ToolTipRole).toString().contains(QStringLiteral("&lt;b&gt;")), "resolved documentation cannot inject tooltip markup");
		choose(&editor, 1); ok &= expect(accepted, "resolved cached suggestion accepts without a second request");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
