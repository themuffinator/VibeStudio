#include "app/application_shell.h"
#include "app/code_language_panel.h"
#include "app/studio_theme.h"
#include "tests/diagnostic_server_fixture.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* why) { if (!value) { std::cerr << why << '\n'; } return value; }
bool wait(const std::function<bool()>& ready, int ms = 12000) { QElapsedTimer elapsed; elapsed.start(); while (!ready() && elapsed.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(2); } return ready(); }
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QJsonObject preferences(const QString& mode) { return {{QStringLiteral("program"), QCoreApplication::applicationFilePath()}, {QStringLiteral("arguments"), QJsonArray {QStringLiteral("--fixture"), mode}}, {QStringLiteral("language"), QStringLiteral("cpp")}, {QStringLiteral("extensions"), QStringLiteral("cpp")}}; }
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		if (QByteArray(context) != "vibestudio::CodeLanguagePanel" && QByteArray(context) != "LanguageServer") { return {}; }
		const auto value = QString::fromUtf8(source); return value + QStringLiteral(" ~").repeated(value.size() / 6);
	}
};
}
int main(int argc, char** argv)
{
	if (argc > 1 && QByteArray(argv[1]) == "--fixture") { QCoreApplication app(argc, argv); return runDiagnosticServerFixture(app.arguments().value(2)); }
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); bool ok = true; QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")); const QString source = QStringLiteral("bad value\r\n"); QByteArray original("\xff\xfe", 2);
	for (const auto ch : source) { original.append(char(ch.unicode() & 255)); original.append(char(ch.unicode() >> 8)); }
	if (!write(path, original)) { return 1; }
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		CodeLanguagePanel panel; panel.setRootPath(temp.path()); panel.setPreferences(preferences(QStringLiteral("pull-slow")));
		panel.snapshots = [&](const QString&, const QStringList&, QString*) { return QVector<LanguageDocument> {{path, QStringLiteral("cpp"), QStringLiteral("bad value\n")}}; };
		panel.resize(scale == 200 ? 1300 : 900, scale == 200 ? 850 : 440); panel.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); panel.show(); panel.connectServer();
		if (!expect(wait([&]() { return panel.client()->diagnostics(path).received; }), "the panel automatically pulls shared document diagnostics")) { return 1; }
		auto* refresh = panel.findChild<QPushButton*>(QStringLiteral("languageServerRefreshDiagnostics")); auto* status = panel.findChild<QLabel*>(QStringLiteral("languageServerStatus"));
		ok &= expect(refresh && refresh->isVisible() && refresh->focusPolicy() != Qt::NoFocus && !refresh->accessibleName().isEmpty()
			&& status->text().contains(QStringLiteral("requested by the editor")), "pull diagnostics expose accessible refresh and provider-mode status");
		refresh->click(); ok &= expect(panel.client()->diagnostics(path).pending && wait([&]() { return panel.client()->diagnostics(path).received; }), "native refresh requests a new report without writing");
		app.processEvents(); const auto evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) { QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel.render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("diagnostic-lifecycle-%1.png").arg(scale))), "render diagnostic controls through QWidget at studio scale"); }
		panel.client()->stop(); wait([&]() { return panel.client()->state() == QStringLiteral("stopped"); }); if (scale == 200) { app.removeTranslator(&translator); }
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(QStringLiteral("pull"))); settings.sync(); }
	QTimer modalGuard; QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { ok &= expect(false, "unexpected modal in diagnostic/save workflow"); dialog->reject(); } }); modalGuard.start(100);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path); shell.show(); app.processEvents();
		auto* panel = shell.findChild<CodeLanguagePanel*>(); auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")); auto* problems = shell.findChild<QListWidget*>(QStringLiteral("codeDiagnostics")); auto* save = shell.findChild<QAction*>(QStringLiteral("code.save"));
		if (!panel || !editor || !problems || !save) { return 1; } panel->connectServer();
		if (!expect(wait([&]() { return panel->client()->diagnostics(path).received; }), "pull provider connects through the studio shell")) { return 1; }
		bool found = false; for (int i = 0; i < problems->count(); ++i) { found |= problems->item(i)->text().contains(QStringLiteral("Fixture diagnostic")) && problems->item(i)->data(Qt::UserRole + 8).toInt() > 0; }
		ok &= expect(found, "pulled diagnostics merge into navigable Problems rows");
		editor->setTextCursor(editor->document()->find(QStringLiteral("bad"))); editor->insertPlainText(QStringLiteral("good"));
		ok &= expect(wait([&]() { const auto report = panel->client()->diagnostics(path); return panel->client()->matchesDocument(path, editor->toPlainText()) && report.received && report.items.isEmpty(); }) && read(path) == original && editor->document()->isModified(), "unsaved changes refresh diagnostics without changing disk bytes");
		save->trigger(); ok &= expect(wait([&]() { return panel->client()->logLines().join(QLatin1Char('\n')).contains(QStringLiteral("saved:")); })
			&& !editor->document()->isModified() && read(path).startsWith(QByteArray("\xff\xfe", 2)) && readTextFile(path).text == QStringLiteral("good value\n"), "normal Save notifies after successful encoded-byte persistence and synchronization");
		const QString target = temp.filePath(QStringLiteral("renamed.cpp")); const auto stale = inspectTextWriteTarget(target); write(target, "external\n"); QString error;
		ok &= expect(!shell.saveCodeDocumentAs(stale, &error) && !error.isEmpty() && !panel->client()->logLines().join(QLatin1Char('\n')).contains(QStringLiteral("saved:") + languageServerUri(target)), "a rejected Save As emits no save notification");
		ok &= expect(shell.saveCodeDocumentAs(inspectTextWriteTarget(target), &error) && wait([&]() { return panel->client()->logLines().join(QLatin1Char('\n')).contains(QStringLiteral("saved:") + languageServerUri(target)); }), "Save As synchronizes and notifies the new document identity");
		const QString log = panel->client()->logLines().join(QLatin1Char('\n'));
		ok &= expect(panel->client()->documentVersion(path) == 0 && log.indexOf(QStringLiteral("closed:") + languageServerUri(path)) < log.indexOf(QStringLiteral("saved:") + languageServerUri(target))
			&& read(target).startsWith(QByteArray("\xff\xfe", 2)), "Save As retires the old URI and preserves encoding");
		panel->client()->stop(); wait([&]() { return panel->client()->state() == QStringLiteral("stopped"); }); panel->setPreferences(preferences(QStringLiteral("pull-error"))); panel->connectServer();
		ok &= expect(wait([&]() { return !panel->client()->diagnostics(target).error.isEmpty(); }), "failed pulls produce an explicit terminal diagnostic state");
		found = false; for (int i = 0; i < problems->count(); ++i) { found |= problems->item(i)->text().contains(QStringLiteral("Language diagnostics failed:")) && !problems->item(i)->flags().testFlag(Qt::ItemIsSelectable); }
		ok &= expect(found, "Problems shows diagnostic failures without offering invalid source navigation");
		panel->findChild<QPushButton*>(QStringLiteral("languageServerRefreshDiagnostics"))->click();
		ok &= expect(wait([&]() { return panel->client()->logLines().join(QLatin1Char('\n')).contains(QStringLiteral("count=2")) && !panel->client()->diagnostics(target).error.isEmpty(); }), "the user can retry diagnostic failures through the native control");
	}
	modalGuard.stop(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
