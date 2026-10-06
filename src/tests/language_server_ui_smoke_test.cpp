#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/studio_theme.h"
#include "core/project_manifest.h"
#include "tests/language_server_fixture.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
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
		if (QByteArray(context) != "vibestudio::CodeLanguagePanel" && QByteArray(context) != "LanguageServer") { return {}; }
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
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	bool ok = true;
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		CodeLanguagePanel panel; panel.setRootPath(temp.path()); panel.setPreferences(preferences(app.applicationFilePath()));
		panel.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); panel.resize(scale == 200 ? 1700 : 1000, scale == 200 ? 850 : 500); panel.show(); app.processEvents();
		panel.findChild<QToolButton*>(QStringLiteral("languageServerDetails"))->setChecked(true); app.processEvents();
		ok &= expect(panel.client()->state() == QStringLiteral("stopped"), "configuration must never start a server implicitly");
		for (const auto& name : {QStringLiteral("languageServerProgram"), QStringLiteral("languageServerLanguage"), QStringLiteral("languageServerExtensions"), QStringLiteral("languageServerConnect")}) {
			auto* field = panel.findChild<QWidget*>(name); ok &= expect(field && !field->accessibleName().isEmpty() && field->focusPolicy() != Qt::NoFocus, "server controls must be named and keyboard focusable");
		}
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) { QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel.render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("language-server-%1.png").arg(scale))), "render scaled language configuration"); }
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(app.applicationFilePath())); settings.sync(); }
	const QString path = temp.filePath(QStringLiteral("source.cpp")), second = temp.filePath(QStringLiteral("second.cpp"));
	const QByteArray source = QStringLiteral("bad 🌍\n  definition\n").toUtf8();
	ok &= expect(write(path, source) && write(second, "bad second\n  definition\n"), "write language service fixtures");
	QTimer modalGuard;
	QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { ok &= expect(false, "unexpected modal dialog during direct language service test"); dialog->reject(); } }); modalGuard.start(100);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path);
		auto* panel = shell.findChild<CodeLanguagePanel*>();
		auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
		auto* problems = shell.findChild<QListWidget*>(QStringLiteral("codeDiagnostics"));
		auto* definition = shell.findChild<QAction*>(QStringLiteral("code.goToDefinition"));
		if (!panel || !editor || !problems || !definition) { return 1; }
		ok &= expect(panel->client()->state() == QStringLiteral("stopped"), "restored preferences do not grant startup consent");
		panel->connectServer();
		ok &= expect(wait([&]() { return panel->client()->diagnostics(path).received; }) && panel->client()->ready(), "explicit connection synchronizes the active document");
		bool marked = false;
		for (int row = 0; row < problems->count(); ++row) { marked |= problems->item(row)->data(Qt::UserRole + 8).toInt() > 0; }
		ok &= expect(marked, "Problems merges versioned language server diagnostics");
		definition->trigger();
		ok &= expect(wait([&]() { return editor->textCursor().position() == editor->toPlainText().indexOf(QStringLiteral("definition")); }), "F12 resolves the live server LocationLink into the editor");
		editor->setTextCursor(editor->document()->find(QStringLiteral("bad"))); editor->insertPlainText(QStringLiteral("good"));
		ok &= expect(wait([&]() { const auto report = panel->client()->diagnostics(path); return report.received && report.items.isEmpty(); })
			&& editor->document()->isModified() && read(path) == source, "live edits update diagnostics while retaining unsaved text and original disk bytes");
		shell.openPathFromCommandLine(second); editor->insertPlainText(QStringLiteral("unsaved ")); shell.openPathFromCommandLine(path);
		ok &= expect(wait([&]() { return panel->client()->matchesDocument(second, QStringLiteral("unsaved bad second\n  definition\n")); }) && panel->client()->synchronizedDocuments() == 2,
			"inactive open documents share their current unsaved text");
		shell.createCodeDocument(); editor->insertPlainText(QStringLiteral("private untitled bad"));
		panel->synchronizeNow(); ok &= expect(panel->client()->synchronizedDocuments() == 2, "untitled documents are excluded from project language sharing");
		shell.openPathFromCommandLine(path);
		panel->client()->stop(); ok &= expect(wait([&]() { return panel->client()->state() == QStringLiteral("stopped"); }) && panel->client()->documentVersion(path) == 0, "disconnect clears document versions and stops the process");
		shell.openPathFromCommandLine(second);
		panel->setPreferences(preferences(app.applicationFilePath(), QStringLiteral("unversioned"))); panel->connectServer();
		ok &= expect(wait([&]() { return panel->client()->diagnostics(second).received; }), "connect an unversioned diagnostic provider");
		bool unversioned = false;
		for (int row = 0; row < problems->count(); ++row) {
			auto* item = problems->item(row);
			if (item->data(Qt::UserRole + 8).isValid()) {
				unversioned |= item->text().contains(QStringLiteral("unversioned")) && !item->flags().testFlag(Qt::ItemIsSelectable) && !item->data(Qt::UserRole).isValid();
			}
		}
		ok &= expect(unversioned, "unversioned diagnostic rows explain why source navigation is unavailable");
		panel->client()->stop(); ok &= expect(wait([&]() { return panel->client()->state() == QStringLiteral("stopped"); }), "stop the unversioned diagnostic provider");
		shell.openPathFromCommandLine(path);
		panel->setPreferences(preferences(app.applicationFilePath(), QStringLiteral("slow"))); panel->connectServer();
		ok &= expect(wait([&]() { return panel->client()->diagnostics(path).received; }), "reconnect to a delayed provider");
		QTextCursor caret(editor->document()); caret.setPosition(0); editor->setTextCursor(caret); definition->trigger();
		caret.setPosition(1); editor->setTextCursor(caret);
		ok &= expect(wait([&]() { return panel->client()->pendingRequests() == 0; }) && editor->textCursor().position() == 1, "moving the caret retires late definition navigation");
		const QString other = temp.filePath(QStringLiteral("other-project")); QDir().mkpath(other);
		ok &= expect(saveProjectManifest(defaultProjectManifest(other, QStringLiteral("Other project"))), "create a recognized project fixture");
		shell.openPathFromCommandLine(other);
		ok &= expect(wait([&]() { return panel->client()->state() == QStringLiteral("stopped"); }) && panel->client()->synchronizedDocuments() == 0, "switching projects stops the old connection without auto-restarting");
	}
	modalGuard.stop(); StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
