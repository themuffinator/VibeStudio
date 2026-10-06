#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/studio_theme.h"
#include "tests/language_server_fixture.h"

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QKeySequence>
#include <QProgressDialog>
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
		if (QByteArray(context) != "vibestudio::ApplicationShell") { return {}; }
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
	const QByteArray saved = "int   value;\r\nint   other;\r\n"; const QString path = temp.filePath(QStringLiteral("source.cpp"));
	ok &= expect(write(path, saved), "write formatting UI fixture");
	StudioSettings::setOverrideFilePath(settingsRoot.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(app.applicationFilePath(), QStringLiteral("incremental"))); settings.sync(); }
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	QTimer modalGuard; QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { ok &= expect(false, "formatting must not block the editor with a modal dialog"); dialog->reject(); } }); modalGuard.start(100);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path); shell.show(); app.processEvents();
		auto* editor = static_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")));
		auto* language = shell.findChild<CodeLanguagePanel*>(); auto* document = shell.findChild<QAction*>(QStringLiteral("code.formatDocument")); auto* selection = shell.findChild<QAction*>(QStringLiteral("code.formatSelection"));
		if (!editor || !language || !document || !selection) { return 1; }
		const auto progress = [&]() -> QProgressDialog* { for (auto* dialog : shell.findChildren<QProgressDialog*>()) { if (dialog->objectName() == QStringLiteral("codeFormattingProgress") && dialog->isVisible()) { return dialog; } } return nullptr; };
		const auto connect = [&](const QString& mode) { language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }); language->setPreferences(preferences(app.applicationFilePath(), mode)); language->connectServer(); return wait([&]() { return language->client()->diagnostics(path).received; }); };
		const auto select = [&](int first, int end) { QTextCursor cursor(editor->document()); cursor.setPosition(first); cursor.setPosition(end, QTextCursor::KeepAnchor); editor->setTextCursor(cursor); };
		ok &= expect(document->toolTip().contains(QKeySequence(QStringLiteral("Alt+Shift+F")).toString(QKeySequence::NativeText)), "document formatting exposes its remappable shortcut");
		document->trigger(); ok &= expect(!progress() && shell.statusBar()->currentMessage().contains(QStringLiteral("Connect")), "offline formatting explains the provider requirement");
		ok &= expect(connect(QStringLiteral("incremental")), "connect formatting provider");
		selection->trigger(); ok &= expect(!progress() && shell.statusBar()->currentMessage().contains(QStringLiteral("Select")), "empty selection does not send a request");
		const QString original = editor->toPlainText(); select(0, 12); selection->trigger();
		ok &= expect(progress() && !progress()->accessibleName().isEmpty() && progress()->windowModality() == Qt::NonModal, "formatting exposes accessible cancellable progress while editing stays available");
		ok &= expect(wait([&]() { return !progress(); }) && editor->toPlainText() == QStringLiteral("int value;\nint   other;\n") && read(path) == saved, "selection formatting applies only returned edits and never saves");
		editor->undo(); ok &= expect(editor->toPlainText() == original && !editor->document()->isModified(), "one Undo restores the entire formatting request and clean state");
		editor->redo(); ok &= expect(editor->toPlainText() == QStringLiteral("int value;\nint   other;\n"), "redo reapplies the formatting transaction"); editor->undo();
		editor->selectAll(); editor->insertPlainText(QStringLiteral("int   unsaved;\nint   other;\n")); const QString unsaved = editor->toPlainText();
		document->trigger(); ok &= expect(wait([&]() { return !progress(); }) && editor->toPlainText() == QStringLiteral("int unsaved;\nint other;\n") && read(path) == saved, "formatting uses current unsaved text");
		editor->undo(); ok &= expect(editor->toPlainText() == unsaved, "formatting Undo preserves preceding author edits");
		ok &= expect(connect(QStringLiteral("slow")), "connect delayed formatter");
		document->trigger(); if (auto* dialog = progress()) { dialog->findChild<QPushButton*>()->click(); }
		wait([&]() { return false; }, 500); ok &= expect(!progress() && editor->toPlainText() == unsaved, "Cancel suppresses a late formatting reply");
		document->trigger(); editor->insertPlainText(QStringLiteral("x")); const QString changed = editor->toPlainText();
		wait([&]() { return false; }, 500); ok &= expect(!progress() && editor->toPlainText() == changed, "typing retires stale formatting without touching the new edit"); editor->undo();
		document->trigger(); language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); });
		ok &= expect(!progress() && editor->toPlainText() == unsaved, "disconnect retires pending formatting");
		ok &= expect(connect(QStringLiteral("format-overlap")), "connect malformed formatter"); document->trigger();
		ok &= expect(wait([&]() { return !progress(); }) && editor->toPlainText() == unsaved && shell.statusBar()->currentMessage().contains(QStringLiteral("overlapping")), "invalid reply applies no partial edits and exposes its failure");
		ok &= expect(connect(QStringLiteral("incremental")), "reconnect valid formatter"); document->trigger(); wait([&]() { return !progress(); });
		auto* save = shell.findChild<QAction*>(QStringLiteral("code.save")); if (!save) { return 1; } save->trigger();
		ok &= expect(read(path) == QByteArray("int unsaved;\r\nint other;\r\n") && !editor->document()->isModified(), "normal save preserves source encoding/newline policy after formatting");
		const int undoSteps = editor->document()->availableUndoSteps(); document->trigger();
		ok &= expect(wait([&]() { return !progress(); }) && !editor->document()->isModified() && editor->document()->availableUndoSteps() == undoSteps,
			"already formatted documents remain clean without an extra Undo step");
		ok &= expect(connect(QStringLiteral("format-hang")), "connect cancellable render fixture");
		for (const int scale : {100, 200}) {
			Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
			applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
			document->trigger(); auto* dialog = progress(); if (!dialog) { ok = false; continue; }
			dialog->setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); app.processEvents();
			const QString evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
			if (!evidence.isEmpty()) { QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("language-formatting-%1.png").arg(scale))), "render formatting progress at both scales"); }
			dialog->findChild<QPushButton*>()->click(); if (scale == 200) { app.removeTranslator(&translator); }
		}
		document->trigger(); ok &= expect(progress() != nullptr, "shutdown retires a live formatting request");
	}
	modalGuard.stop(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
