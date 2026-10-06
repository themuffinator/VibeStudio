#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_language_panel.h"
#include "app/code_signature_panel.h"
#include "app/studio_theme.h"
#include "tests/language_server_fixture.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QShortcut>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
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
QJsonObject preferences(const QString& executable, const QString& mode)
{
	return {{QStringLiteral("program"), executable}, {QStringLiteral("arguments"), QJsonArray {QStringLiteral("--fixture"), mode}}, {QStringLiteral("language"), QStringLiteral("cpp")}, {QStringLiteral("extensions"), QStringLiteral("cpp;h")}};
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		if (QByteArray(context) != "vibestudio::CodeSignaturePanel") { return {}; }
		const auto text = QString::fromUtf8(source); return text + QStringLiteral(" ~").repeated(text.size() / 5);
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
	int resources = 0; const auto previousProvider = QTextDocument::defaultResourceProvider();
	QTextDocument::setDefaultResourceProvider([&](const QUrl&) { ++resources; return QVariant(); });
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		CodeSignaturePanel panel; const int width = scale == 200 ? 900 : 840; panel.resize(width, 300); panel.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		const QString label = QStringLiteral("sum(int first, int second)");
		const QJsonObject signature {{QStringLiteral("label"), label}, {QStringLiteral("parameters"), QJsonArray {
			QJsonObject {{QStringLiteral("label"), QStringLiteral("int first")}}, QJsonObject {{QStringLiteral("label"), QStringLiteral("int second")},
				{QStringLiteral("documentation"), QJsonObject {{QStringLiteral("kind"), QStringLiteral("markdown")}, {QStringLiteral("value"), QStringLiteral("The **second** value. ![blocked](file:///never-read) [link](https://example.invalid) <b>literal HTML</b>")}}}}}},
			{QStringLiteral("documentation"), QStringLiteral("Sum two values. <b>Literal text</b>.")}, {QStringLiteral("activeParameter"), 1}};
		auto report = parseLanguageSignatureHelp(QJsonObject {{QStringLiteral("signatures"), QJsonArray {signature, signature}}}, QStringLiteral("sum(1, "), 0, 7);
		panel.finish(report, QStringLiteral("Fixture provider")); panel.show(); app.processEvents();
		auto* text = panel.findChild<QTextBrowser*>(QStringLiteral("codeSignatureText")); auto* docs = panel.findChild<QTextBrowser*>(QStringLiteral("codeSignatureDocumentation"));
		auto* labelView = panel.findChild<QLabel*>(QStringLiteral("codeSignatureParameter")); auto* overloads = panel.findChild<QComboBox*>(QStringLiteral("codeSignatureOverloads"));
		QTextCursor active(text->document()); active.setPosition(label.indexOf(QStringLiteral("int second")) + 1);
		ok &= expect(active.charFormat().fontWeight() == QFont::Bold && active.charFormat().fontUnderline() && labelView->text().contains(QStringLiteral("2"))
			&& text->layoutDirection() == Qt::LeftToRight && !text->accessibleDescription().isEmpty(), "active arguments use text, emphasis and accessible metadata in RTL layouts");
		overloads->setCurrentIndex(1); ok &= expect(panel.report().activeSignature == 1, "native overload selection updates retrigger context");
		const auto checkEmphasis = [&]() {
			QTextCursor inactive(text->document()); inactive.setPosition(1);
			QTextCursor selected(text->document()); selected.setPosition(label.indexOf(QStringLiteral("int second")) + 1);
			return inactive.charFormat().fontWeight() != QFont::Bold && !inactive.charFormat().fontUnderline()
				&& selected.charFormat().fontWeight() == QFont::Bold && selected.charFormat().fontUnderline();
		};
		ok &= expect(checkEmphasis(), "changing overloads emphasizes only the active argument");
		panel.begin(QStringLiteral("Fixture provider")); panel.finish(report, QStringLiteral("Fixture provider"));
		ok &= expect(checkEmphasis(), "repeated replies reset inactive signature formatting");
		panel.findChild<QToolButton*>(QStringLiteral("codeSignatureDetails"))->click(); app.processEvents();
		ok &= expect(docs->isVisible() && docs->toPlainText().contains(QStringLiteral("<b>Literal text</b>")) && resources == 0
			&& !docs->openLinks() && !docs->openExternalLinks() && docs->focusPolicy() == Qt::StrongFocus, "expanded documentation is selectable and cannot load resources or activate links");
		panel.layout()->activate(); panel.resize(width, panel.sizeHint().height()); app.processEvents();
		for (auto* control : {static_cast<QWidget*>(overloads), static_cast<QWidget*>(panel.findChild<QToolButton*>(QStringLiteral("codeSignatureDetails"))), static_cast<QWidget*>(panel.findChild<QPushButton*>(QStringLiteral("codeSignatureClose")))}) {
			ok &= expect(panel.rect().contains(QRect(control->mapTo(&panel, QPoint()), control->size())) && control->width() >= control->sizeHint().width(), "scaled, expanded controls stay fully inside a narrow parameter panel");
		}
		const QString evidence = qEnvironmentVariable("VIBESTUDIO_LANGUAGE_TEST_EVIDENCE");
		if (!evidence.isEmpty()) { QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel.render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("signature-help-%1.png").arg(scale))), "render parameter hints without OS capture"); }
		if (scale == 200) {
			panel.resize(600, panel.height()); app.processEvents(); panel.layout()->activate();
			panel.resize(600, panel.layout()->totalHeightForWidth(600)); app.processEvents();
			auto* details = panel.findChild<QToolButton*>(QStringLiteral("codeSignatureDetails"));
			ok &= expect(panel.width() == 600 && details->mapTo(&panel, QPoint()).y() >= overloads->mapTo(&panel, QPoint()).y() + overloads->height()
				&& details->width() >= details->sizeHint().width() && overloads->width() >= overloads->sizeHint().width(), "controls wrap vertically in a narrow high-scale pane");
			if (!evidence.isEmpty()) { QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); panel.render(&image); ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("signature-help-200-narrow.png"))), "render narrow parameter hints without OS capture"); }
		}
		bool dismissed = false; panel.dismissed = [&]() { dismissed = true; };
		panel.begin(QStringLiteral("Fixture provider")); ok &= expect(panel.busy() && !overloads->isEnabled(), "loading hints retain visible, noneditable context");
		auto* escape = panel.findChild<QShortcut*>(); ok &= expect(escape && escape->context() == Qt::WidgetWithChildrenShortcut
			&& QMetaObject::invokeMethod(escape, "activated", Qt::DirectConnection) && dismissed, "panel children expose a scoped Escape dismissal without OS input");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	QTextDocument::setDefaultResourceProvider(previousProvider);
	if (app.arguments().contains(QStringLiteral("--panel-only"))) { return ok ? 0 : 1; }
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	QTemporaryDir temp, settingsRoot; if (!temp.isValid() || !settingsRoot.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cpp")), other = temp.filePath(QStringLiteral("other.cpp")); const QByteArray original = "int value = sum(1, 2);\r\n";
	ok &= expect(write(path, original) && write(other, "sum(3, 4)\n"), "write isolated parameter-hint fixtures");
	StudioSettings::setOverrideFilePath(settingsRoot.filePath(QStringLiteral("settings.ini")));
	{ StudioSettings settings; settings.setCurrentProjectPath(temp.path()); settings.setLanguageServerPreferences(preferences(app.applicationFilePath(), QStringLiteral("signature-slow"))); settings.sync(); }
	QTimer modalGuard; QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() { if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { ok &= expect(false, "unexpected modal during parameter hints"); dialog->reject(); } }); modalGuard.start(100);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(path); shell.show(); app.processEvents();
		auto* editor = dynamic_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")));
		auto* panel = shell.findChild<CodeSignaturePanel*>(); auto* language = shell.findChild<CodeLanguagePanel*>(); auto* action = shell.findChild<QAction*>(QStringLiteral("code.parameterHints"));
		if (!editor || !panel || !language || !action) { return 1; }
		ok &= expect(action->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+Space")), "parameter hints receive the remappable Code shortcut");
		const auto connect = [&](const QString& mode) { language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); }); language->setPreferences(preferences(app.applicationFilePath(), mode)); language->connectServer(); return wait([&]() { return language->client()->diagnostics(path).received; }); };
		const auto query = [&]() { caret(editor, editor->toPlainText().indexOf(QStringLiteral("2)"))); action->trigger(); return wait([&]() { return !panel->busy() && panel->report().signatures.size() == 2; }); };
		if (!expect(connect(QStringLiteral("signature-slow")) && query(), "explicit action shows synchronized call signatures")) { return 1; }
		ok &= expect(panel->isVisible() && panel->report().signatures[0].activeParameter == 1 && read(path) == original && !editor->document()->isModified(), "hints identify the second argument without changing document or file");
		panel->findChild<QComboBox*>(QStringLiteral("codeSignatureOverloads"))->setCurrentIndex(1); caret(editor, editor->textCursor().position() + 1);
		ok &= expect(wait([&]() { return !panel->busy(); }) && panel->report().activeSignature == 1, "caret retriggers retain the chosen overload");
		editor->insertPlainText(QStringLiteral("3")); ok &= expect(wait([&]() { return !panel->busy(); }) && panel->report().sourceSha256 == QCryptographicHash::hash(editor->toPlainText().toUtf8(), QCryptographicHash::Sha256), "typing refreshes hints against the new unsaved snapshot"); editor->undo();
		ok &= expect(query(), "refresh after Undo"); action->trigger(); ok &= expect(editor->signatureHelpDismissed(), "dismiss pending hints"); wait([] { return false; }, 450);
		ok &= expect(panel->isHidden() && language->client()->pendingRequests() == 0, "late replies cannot reopen dismissed hints");
		caret(editor, editor->toPlainText().size()); editor->insertPlainText(QStringLiteral("sum(")); editor->signatureHelpTyped(QStringLiteral("("));
		ok &= expect(wait([&]() { return !panel->busy() && panel->report().signatures.size() == 2; }) && panel->report().signatures[0].activeParameter == 0, "advertised typing triggers open first-argument hints");
		caret(editor, 0); ok &= expect(wait([&]() { return panel->isHidden(); }), "leaving the call closes hints when the provider returns no signature"); editor->undo();
		ok &= expect(connect(QStringLiteral("signature-malformed")), "connect malformed provider"); caret(editor, editor->toPlainText().indexOf(QStringLiteral("2)"))); action->trigger();
		ok &= expect(wait([&]() { return !panel->busy(); }) && panel->findChild<QLabel*>(QStringLiteral("codeSignatureStatus"))->text().contains(QStringLiteral("invalid")), "provider failures replace stale signatures with a visible reason");
		ok &= expect(connect(QStringLiteral("signature-slow")) && query(), "restore delayed provider"); action->trigger(); shell.openPathFromCommandLine(other); wait([] { return false; }, 450);
		ok &= expect(panel->isHidden() && language->client()->pendingRequests() == 0 && read(path) == original && read(other) == QByteArray("sum(3, 4)\n"), "tab switches retire pending hints and preserve saved sources");
		caret(editor, 7); action->trigger(); language->client()->stop(); wait([&]() { return language->client()->state() == QStringLiteral("stopped"); });
		ok &= expect(panel->isHidden(), "disconnect dismisses pending hints");
	}
	modalGuard.stop(); StudioSettings::setOverrideFilePath({}); return ok ? 0 : 1;
}
