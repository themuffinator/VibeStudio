#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/code_quick_info.h"
#include "app/studio_theme.h"
#include "tests/language_server_fixture.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTimer>
#include <QToolTip>
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
void caret(QPlainTextEdit* editor, int position) { QTextCursor cursor(editor->document()); cursor.setPosition(position); editor->setTextCursor(cursor); }
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
		if (QByteArray(context) != "vibestudio::CodeQuickInfoPanel" && QByteArray(context) != "CodeQuickInfo") { return {}; }
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
	const QString path = temp.filePath(QStringLiteral("source.cpp")); const QByteArray saved = "int target;\n";
	ok &= expect(write(path, saved), "write the Quick Info source fixture");
	int resourceReads = 0;
	QTextDocument::setDefaultResourceProvider([&](const QUrl&) -> QVariant { ++resourceReads; return QByteArray("unexpected"); });
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		CodeQuickInfoPanel panel; panel.resize(scale == 200 ? 1650 : 950, scale == 200 ? 700 : 480); panel.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); panel.show(); app.processEvents();
		LanguageHover report; report.contents = {{QStringLiteral("code"), QStringLiteral("int Object::target;"), QStringLiteral("cpp")},
			{QStringLiteral("markdown"), QStringLiteral("### Member documentation\n\nA **documented** field with `inline code`.\n\n- First detail\n- Second detail\n\n[Link text](command:run) ![image](file:///never-read.png)"), {}},
			{QStringLiteral("plaintext"), QStringLiteral("<b>Plain text stays literal.</b>"), {}}};
		panel.begin(QStringLiteral("Fixture server"), QStringLiteral("source.cpp:1:5"));
		ok &= expect(panel.busy() && panel.findChild<QPushButton*>(QStringLiteral("codeQuickInfoCancel"))->isEnabled(), "Quick Info loading has a cancellable state");
		panel.finish(report, QStringLiteral("Fixture server"), QStringLiteral("source.cpp:1:5"));
		const auto text = panel.view()->toPlainText();
		ok &= expect(!panel.busy() && text.contains(QStringLiteral("int Object::target;")) && text.contains(QStringLiteral("documented"))
			&& !text.contains(QStringLiteral("**documented**")) && text.contains(QStringLiteral("<b>Plain text stays literal.</b>")) && text.contains(QStringLiteral("Image omitted")), "Markdown, code and literal text render distinctly without remote images");
		for (auto block = panel.view()->document()->begin(); block.isValid(); block = block.next()) {
			for (auto item = block.begin(); !item.atEnd(); ++item) { const auto format = item.fragment().charFormat(); ok &= expect(!format.isImageFormat() && !format.isAnchor(), "documentation contains no active links or image resources"); }
		}
		ok &= expect(resourceReads == 0 && !panel.view()->openLinks() && !panel.view()->openExternalLinks(), "documentation cannot invoke resource providers or open links");
		ok &= expect(!panel.view()->accessibleName().isEmpty() && panel.view()->focusPolicy() == Qt::StrongFocus && panel.view()->isReadOnly(), "persistent documentation remains selectable and keyboard accessible");
		const QString evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) { QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel.render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("language-hover-%1.png").arg(scale))), "render Quick Info at both scales"); }
		LanguageHover longInfo; longInfo.contents = {{QStringLiteral("plaintext"), QString(5000, QLatin1Char('x')), {}}};
		ok &= expect(CodeQuickInfoPanel::hintText(longInfo).size() < 2000 && CodeQuickInfoPanel::hintText(longInfo).contains(QStringLiteral("Quick Info")), "pointer hints stay bounded and expose the longer-documentation route");
		panel.invalidate(QStringLiteral("Source changed")); ok &= expect(panel.report().contents.isEmpty() && panel.view()->toPlainText().isEmpty(), "stale documentation is retired visibly");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	QTextDocument::setDefaultResourceProvider({});
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	StudioSettings::setOverrideFilePath(settingsRoot.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(app.applicationFilePath())); settings.sync(); }
	QTimer modalGuard; QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { ok &= expect(false, "unexpected modal dialog during Quick Info test"); dialog->reject(); } }); modalGuard.start(100);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path); shell.show(); app.processEvents();
		auto* editor = static_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")));
		auto* language = shell.findChild<CodeLanguagePanel*>(); auto* panel = shell.findChild<CodeQuickInfoPanel*>(); auto* action = shell.findChild<QAction*>(QStringLiteral("code.quickInfo"));
		if (!editor || !language || !panel || !action) { return 1; }
		ok &= expect(action->toolTip().contains(QKeySequence(QStringLiteral("Ctrl+I")).toString(QKeySequence::NativeText)), "Quick Info exposes its registered and remappable keyboard shortcut");
		caret(editor, 6); const auto initialPosition = editor->textCursor().position(); action->trigger();
		ok &= expect(!panel->busy() && panel->view()->toPlainText().isEmpty() && panel->findChild<QLabel*>(QStringLiteral("codeQuickInfoStatus"))->text().contains(QStringLiteral("Connect")), "offline Quick Info explains the optional provider requirement");
		language->connectServer(); ok &= expect(wait([&]() { return language->client()->diagnostics(path).received; }), "connect Quick Info provider");
		action->trigger(); ok &= expect(panel->busy(), "keyboard command opens a loading documentation pane");
		ok &= expect(wait([&]() { return !panel->busy(); }) && panel->view()->toPlainText().contains(QStringLiteral("int target;")) && panel->report().hasRange
			&& editor->textCursor().position() == initialPosition && read(path) == saved && !editor->document()->isModified(), "Quick Info preserves caret, undo state and saved bytes");
		editor->selectAll(); editor->insertPlainText(QStringLiteral("int renamed;\n")); caret(editor, 6); action->trigger();
		ok &= expect(wait([&]() { return !panel->busy(); }) && panel->view()->toPlainText().contains(QStringLiteral("int renamed;")) && read(path) == saved, "Quick Info synchronizes unsaved source before asking the server");
		language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }); language->setPreferences(preferences(app.applicationFilePath(), QStringLiteral("slow"))); language->connectServer();
		ok &= expect(wait([&]() { return language->client()->diagnostics(path).received; }), "connect delayed Quick Info provider");
		action->trigger(); panel->findChild<QPushButton*>(QStringLiteral("codeQuickInfoCancel"))->click(); wait([&]() { return false; }, 500);
		ok &= expect(!panel->busy() && panel->view()->toPlainText().isEmpty(), "cancelled information cannot republish later");
		action->trigger(); caret(editor, 7); wait([&]() { return false; }, 500);
		ok &= expect(!panel->busy() && panel->report().contents.isEmpty(), "moving the caret retires pending explicit information");
		action->trigger(); editor->insertPlainText(QStringLiteral("x")); wait([&]() { return false; }, 500);
		ok &= expect(!panel->busy() && panel->report().contents.isEmpty(), "editing retires pending information"); editor->undo(); caret(editor, 6);
		editor->setFocus(); app.processEvents(); const auto pointerPosition = editor->cursorRect().center(); const auto originalCaret = editor->textCursor().position();
		ok &= expect(editor->requestQuickInfoAt(pointerPosition, editor->viewport()->mapToGlobal(pointerPosition)), "pointer dwell uses the same asynchronous hover service");
		ok &= expect(wait([&]() { return QToolTip::text().contains(QStringLiteral("int renamed;")); }) && editor->textCursor().position() == originalCaret && !panel->busy(), "pointer hints neither move the caret nor replace the persistent pane");
		editor->dismissQuickInfoHint();
		ok &= expect(editor->requestQuickInfoAt(pointerPosition, editor->viewport()->mapToGlobal(pointerPosition)), "start a second pointer hint"); editor->dismissQuickInfoHint(); wait([&]() { return false; }, 500);
		ok &= expect(language->client()->pendingRequests() == 0 && !QToolTip::isVisible(), "leaving a pointer hint retires its pending reply");
		action->trigger(); language->client()->stop();
		ok &= expect(wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }) && !panel->busy() && panel->report().contents.isEmpty(), "disconnect clears pending and displayed information");
		language->connectServer(); wait([&]() { return language->client()->diagnostics(path).received; }); action->trigger();
		ok &= expect(panel->busy(), "shutdown begins with pending Quick Info");
	}
	modalGuard.stop(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
