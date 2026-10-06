#include "app/application_shell.h"
#include "app/code_language_panel.h"
#include "app/project_search_panel.h"
#include "app/studio_theme.h"
#include "tests/language_server_fixture.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
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
	QTemporaryDir temp, preferencesRoot; if (!temp.isValid() || !preferencesRoot.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")), second = temp.filePath(QStringLiteral("references-other.cpp"));
	const QByteArray source = "int target;\nint use = target;\n// target comment\nint shadow = target;\n";
	const QByteArray other = "int second = target;\n";
	ok &= expect(write(path, source) && write(second, other), "write reference workflow fixtures");
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		ProjectSearchPanel panel; panel.resize(scale == 200 ? 1800 : 1050, scale == 200 ? 750 : 500);
		panel.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); panel.show(); app.processEvents();
		int starts = 0, finishes = 0; bool cancelled = false;
		panel.operationStarted = [&](bool applying) { ++starts; ok &= expect(!applying, "semantic references never begin a replacement operation"); };
		panel.operationFinished = [&](const auto&) { ++finishes; };
		const auto token = panel.beginReferences(temp.path(), QStringLiteral("target"), QStringLiteral("Fixture server"), [&]() { cancelled = true; });
		ok &= expect(token > 0 && panel.busy() && panel.referencesActive() && panel.findChild<QPushButton*>(QStringLiteral("projectSearchCancel"))->isEnabled(), "pending protocol work exposes progress and cancellation");
		LanguageReferenceRequest request; request.references.items = {{path, 0, 4, 0, 10}, {path, 1, 10, 1, 16}};
		panel.finishReferences(token, request);
		ok &= expect(wait([&]() { return !panel.busy(); }) && panel.report().matchCount == 2 && panel.report().referenceProvider == QStringLiteral("Fixture server")
			&& !panel.findChild<QPushButton*>(QStringLiteral("projectSearchApply"))->isEnabled() && starts == 1 && finishes == 1, "references share the existing results panel and Activity lifecycle without enabling replacement");
		ok &= expect(panel.resultsList()->layoutDirection() == Qt::LeftToRight, "reference source paths and code retain left-to-right reading order in RTL layouts");
		for (auto* control : QVector<QWidget*> {panel.resultsList(), panel.findField(), panel.findChild<QPushButton*>(QStringLiteral("projectSearchCancel"))}) {
			ok &= expect(control && !control->accessibleName().isEmpty() && control->focusPolicy() != Qt::NoFocus, "reference controls expose names and keyboard focus");
		}
		const QString evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) { QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel.render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("language-references-%1.png").arg(scale))), "render semantic reference results"); }
		const auto retired = panel.beginReferences(temp.path(), QStringLiteral("target"), QStringLiteral("Delayed"), [&]() { cancelled = true; });
		panel.cancel(); panel.finishReferences(retired, request);
		ok &= expect(cancelled && !panel.busy() && panel.report().cancelled && panel.report().matches.isEmpty() && starts == 2 && finishes == 2, "cancelled protocol work cannot republish a late result");
		AssetTextSearchRequest textQuery; textQuery.rootPath = temp.path(); textQuery.findText = QStringLiteral("target"); textQuery.wholeWords = true;
		panel.startSearch(textQuery);
		ok &= expect(wait([&]() { return !panel.busy(); }) && panel.report().referenceProvider.isEmpty() && panel.report().matchCount == 5, "a new text search remains distinct and can include comments and unrelated bindings");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	StudioSettings::setOverrideFilePath(preferencesRoot.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(app.applicationFilePath())); settings.sync(); }
	QTimer modalGuard; QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { ok &= expect(false, "unexpected modal dialog during semantic reference test"); dialog->reject(); } }); modalGuard.start(100);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(second);
		auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
		auto* language = shell.findChild<CodeLanguagePanel*>();
		auto* results = shell.findChild<ProjectSearchPanel*>();
		auto* references = shell.findChild<QAction*>(QStringLiteral("code.findReferences"));
		if (!editor || !language || !results || !references) { return 1; }
		editor->selectAll(); editor->insertPlainText(QStringLiteral("// unsaved header\nint extra = target;\n"));
		shell.openPathFromCommandLine(path); language->connectServer();
		ok &= expect(wait([&]() { return language->client()->diagnostics(path).received && language->client()->diagnostics(second).received; }), "connect and synchronize active and inactive reference sources");
		caret(editor, 6); references->trigger();
		ok &= expect(wait([&]() { return !results->busy(); }) && results->report().succeeded() && results->report().matchCount == 3
			&& !results->report().referenceProvider.isEmpty(), "Shift+F12 shows semantic references and excludes same-spelling nonreferences");
		const auto report = results->report(); AssetTextMatch live;
		for (const auto& match : report.matches) { if (match.filePath == second) { live = match; } }
		ok &= expect(!live.bufferId.isEmpty() && live.line == 2, "inactive unsaved buffers supply reference preview positions");
		results->openMatch(live);
		ok &= expect(editor->toPlainText().startsWith(QStringLiteral("// unsaved header")) && editor->textCursor().selectedText() == QStringLiteral("target")
			&& results->report().matchCount == 3 && read(second) == other && read(path) == source, "activating a reference selects its exact range without discarding other results or saving buffers");
		caret(editor, 0); editor->insertPlainText(QStringLiteral("// changed\n")); const int unchangedCaret = editor->textCursor().position();
		results->openMatch(live);
		ok &= expect(editor->textCursor().position() == unchangedCaret && shell.statusBar()->currentMessage().contains(QStringLiteral("changed")), "stale result activation cannot navigate into a changed live snapshot");
		language->client()->stop(); ok &= expect(wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }), "disconnect the initial provider");
		shell.openPathFromCommandLine(path); language->setPreferences(preferences(app.applicationFilePath(), QStringLiteral("slow"))); language->connectServer();
		ok &= expect(wait([&]() { return language->client()->diagnostics(path).received; }), "connect a delayed reference provider");
		caret(editor, 6); references->trigger(); results->findChild<QPushButton*>(QStringLiteral("projectSearchCancel"))->click();
		wait([&]() { return false; }, 500);
		ok &= expect(!results->busy() && results->report().cancelled && results->report().matches.isEmpty(), "Cancel retires a delayed reference response");
		references->trigger(); editor->insertPlainText(QStringLiteral("x")); wait([&]() { return false; }, 500);
		ok &= expect(!results->busy() && results->report().cancelled && results->report().matches.isEmpty(), "source changes cancel pending semantic lookup"); editor->undo();
		caret(editor, 6); references->trigger(); language->client()->stop();
		ok &= expect(wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }) && !results->busy() && results->report().cancelled, "disconnect ends pending lookup visibly");
		references->trigger();
		const bool localFinished = wait([&]() { return !results->busy(); });
		ok &= expect(localFinished && results->report().referenceProvider.isEmpty() && results->report().matchCount == 5 && results->report().filesWithMatches == 2, "disconnected reference lookup keeps the existing whole-word project search");
		language->setPreferences(preferences(app.applicationFilePath(), QStringLiteral("slow"))); language->connectServer();
		ok &= expect(wait([&]() { return language->client()->diagnostics(path).received; }), "reconnect for shutdown validation");
		references->trigger(); ok &= expect(results->referencesActive(), "shutdown begins with a pending semantic request");
	}
	modalGuard.stop(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
