#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_index_worker.h"
#include "app/studio_theme.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool waitFor(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(2); }
	return ready();
}
class ExpandedLabels final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "vibestudio::ApplicationShell") { return {}; }
		const QString text = QString::fromUtf8(source);
		return text + QStringLiteral(" ~").repeated(text.size() / 6);
	}
};
} // namespace

int main(int argc, char** argv)
{
	// Direct widget calls and QWidget rendering only; no input injection.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	bool ok = true;
	QTimer modalGuard;
	modalGuard.setInterval(100);
	QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() {
		if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
			std::cerr << "Unexpected modal: " << dialog->windowTitle().toStdString();
			if (auto* message = qobject_cast<QMessageBox*>(dialog)) { std::cerr << " " << message->text().toStdString(); }
			std::cerr << std::endl;
			ok = false;
			dialog->reject();
		}
	});
	modalGuard.start();
	const QString first = temp.filePath(QStringLiteral("first.qc"));
	const QString second = temp.filePath(QStringLiteral("second.qc"));
	ok &= expect(write(first, "void() saved_definition = {\n};\n"), "write first source");
	ok &= expect(write(second, "live_definition();\n"), "write second source");
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{
		StudioSettings settings;
		settings.setCurrentProjectPath(temp.path());
		settings.setCodeRecoveryEnabled(false);
		settings.sync();
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		ApplicationShell shell;
		shell.openPathFromCommandLine(first);
		auto* editor = static_cast<StudioCodeEditor*>(shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor")));
		auto* worker = static_cast<CodeIndexWorker*>(shell.findChild<QObject*>(QStringLiteral("codeIndexWorker")));
		auto* indexAction = shell.findChild<QAction*>(QStringLiteral("code.index"));
		auto* definition = shell.findChild<QAction*>(QStringLiteral("code.goToDefinition"));
		auto* status = shell.findChild<QLabel*>(QStringLiteral("codeIndexStatus"));
		auto* cancel = shell.findChild<QPushButton*>(QStringLiteral("codeIndexCancel"));
		auto* refresh = shell.findChild<QPushButton*>(QStringLiteral("codeIndexRefresh"));
		auto* list = shell.findChild<QListWidget*>(QStringLiteral("codeIndex"));
		auto* panel = shell.findChild<QWidget*>(QStringLiteral("codeIndexPanel"));
		if (!editor || !worker || !indexAction || !definition || !status || !cancel || !refresh || !list || !panel) { return 1; }
		int starts = 0, finishes = 0, scannedFiles = -1;
		const auto onStart = worker->started;
		const auto onFinish = worker->completed;
		const auto onProgress = worker->progress;
		worker->started = [&]() { ++starts; onStart(); };
		worker->completed = [&](const auto& result) { ++finishes; onFinish(result); };
		worker->progress = [&](int files, int symbols) { scannedFiles = files; onProgress(files, symbols); };
		editor->selectAll();
		editor->insertPlainText(QStringLiteral("\nvoid() live_definition = {\n};\n"));
		QTextDocument* unsaved = editor->document();
		shell.openPathFromCommandLine(second);
		QTextDocument* caller = editor->document();
		QTextCursor cursor(caller);
		cursor.setPosition(3);
		editor->setTextCursor(cursor);
		const int requestedRevision = caller->revision();
		definition->trigger();
		ok &= expect(worker->busy() && editor->document() == caller && cancel->isEnabled(), "definition lookup starts background work without immediate navigation");
		const bool navigated = waitFor([&]() { return !worker->busy() && editor->document() == unsaved; });
		if (!expect(navigated && editor->textCursor().blockNumber() == 1 && unsaved->isModified(), "deferred lookup navigates to another tab's unsaved definition")) {
			std::cerr << "busy=" << worker->busy() << " atTarget=" << (editor->document() == unsaved)
				<< " targetDirty=" << unsaved->isModified() << " line=" << editor->textCursor().blockNumber()
				<< " cursor=" << editor->textCursor().position() << " revision=" << editor->document()->revision()
				<< " requestedRevision=" << requestedRevision << " callerRevision=" << caller->revision()
				<< " starts=" << starts << " finishes=" << finishes << " files=" << scannedFiles
				<< " indexStatus=" << status->text().toStdString()
				<< " status=" << shell.statusBar()->currentMessage().toStdString() << '\n';
			for (int row = 0; row < std::min(6, list->count()); ++row) { std::cerr << list->item(row)->text().toStdString() << '\n'; }
			return 1;
		}
		const QStringList completions = editor->completionsFor(QStringLiteral("live"));
		ok &= expect(starts == 1 && finishes == 1, "resuming navigation must reuse the completed index instead of starting another scan");
		ok &= expect(completions.contains(QStringLiteral("live_definition")) && !completions.contains(QStringLiteral("saved_definition")), "project completion uses live symbols instead of stale disk definitions");
		// A scan finishing after the caret moves must never steal navigation.
		shell.openPathFromCommandLine(second);
		cursor = QTextCursor(editor->document());
		cursor.setPosition(3);
		editor->setTextCursor(cursor);
		indexAction->trigger();
		definition->trigger();
		cursor.setPosition(0);
		editor->setTextCursor(cursor);
		ok &= expect(waitFor([&]() { return !worker->busy(); }) && editor->document() == caller && editor->textCursor().position() == 0, "caret movement invalidates automatic definition navigation");
		indexAction->trigger();
		cancel->click();
		ok &= expect(waitFor([&]() { return !worker->busy(); }) && status->text().contains(QStringLiteral("cancelled")) && !cancel->isEnabled(), "cancel publishes an explicit terminal UI state");
		editor->completionsFor(QStringLiteral("live"));
		ok &= expect(!worker->busy(), "completion must not restart a cancelled scan on each keystroke");
		refresh->click();
		ok &= expect(waitFor([&]() { return !worker->busy(); }) && status->text().contains(QStringLiteral("ready")), "explicit refresh restarts indexing");
		// Changes in an inactive document retire its old snapshot and debounce a new scan.
		QTextCursor edit(unsaved);
		edit.select(QTextCursor::Document);
		edit.insertText(QStringLiteral("void() renamed_live = {\n};\n"));
		editor->selectAll();
		editor->insertPlainText(QStringLiteral("// completion probe\n"));
		ok &= expect(waitFor([&]() { return editor->completionsFor(QStringLiteral("renamed")).contains(QStringLiteral("renamed_live")); }), "inactive document edits refresh the shared symbol snapshot");
		ok &= expect(!editor->completionsFor(QStringLiteral("live")).contains(QStringLiteral("live_definition")), "removed live symbols do not linger in completion");
		auto* filter = panel->findChild<QLineEdit*>(QStringLiteral("codeIndexFilter"));
		if (!filter) { return 1; }
		filter->setText(QStringLiteral("renamed_live"));
		ok &= expect(list->findItems(QStringLiteral("*renamed_live*"), Qt::MatchWildcard).size() == 1, "index filter finds current symbols without a new scan");
		filter->clear();
		for (QWidget* control : QVector<QWidget*> {cancel, refresh, list, filter}) {
			ok &= expect(!control->accessibleName().isEmpty() && control->focusPolicy() != Qt::NoFocus, "index controls expose accessible names and keyboard focus");
		}
		// Render the actual panel at both scales, without the unrelated shell's
		// large collection of asset previews affecting layout verification.
		auto* panelTabs = qobject_cast<QTabWidget*>(panel->parentWidget()->parentWidget());
		if (!panelTabs) { return 1; }
		const int panelIndex = panelTabs->indexOf(panel);
		const QString panelTitle = panelTabs->tabText(panelIndex);
		panelTabs->removeTab(panelIndex);
		panel->setParent(nullptr);
		for (int scale : {100, 200}) {
			ExpandedLabels translator;
			if (scale == 200) { app.installTranslator(&translator); }
			applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
			refresh->setText(QCoreApplication::translate("vibestudio::ApplicationShell", "Refresh Index"));
			cancel->setText(QCoreApplication::translate("vibestudio::ApplicationShell", "Cancel"));
			filter->setPlaceholderText(QCoreApplication::translate("vibestudio::ApplicationShell", "Filter symbols and source paths"));
			panel->setLayoutDirection(scale == 100 ? Qt::LeftToRight : Qt::RightToLeft);
			panel->resize(scale == 100 ? 900 : 1400, scale == 100 ? 430 : 740);
			panel->show();
			refresh->click();
			ok &= expect(waitFor([&]() { return !worker->busy(); }), "rendered index refresh finishes");
			app.processEvents();
			ok &= expect(list->height() > 100 && status->width() > 300 && refresh->width() >= refresh->sizeHint().width(), "scaled RTL index keeps status, buttons, and results usable");
			QImage image(panel->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			panel->render(&image);
			const QString evidence = qEnvironmentVariable("VIBESTUDIO_TEXT_TEST_EVIDENCE");
			if (!evidence.isEmpty()) { ok &= expect(image.save(QDir(evidence).filePath(QStringLiteral("code-index-%1.png").arg(scale))), "save index render evidence"); }
			if (scale == 200) { app.removeTranslator(&translator); }
		}
		panel->hide();
		panelTabs->insertTab(panelIndex, panel, panelTitle);
		// Switching projects while a scan is active retires its old results.
		std::cerr << "Starting project-switch check" << std::endl;
		indexAction->trigger();
		std::cerr << "Project-switch scan started" << std::endl;
		const QString elsewhere = temp.filePath(QStringLiteral("other-project"));
		QDir().mkpath(elsewhere);
		ok &= expect(saveProjectManifest(defaultProjectManifest(elsewhere, QStringLiteral("Second project"))), "create a recognized project-switch fixture");
		ok &= expect(write(QDir(elsewhere).filePath(QStringLiteral("other.qc")), "void() unrelated_project = {\n};\n"), "write replacement project");
		std::cerr << "Opening project-switch fixture" << std::endl;
		shell.openPathFromCommandLine(elsewhere);
		std::cerr << "Project-switch fixture opened" << std::endl;
		refresh->click();
		ok &= expect(waitFor([&]() { return !worker->busy(); }) && !editor->completionsFor(QStringLiteral("renamed")).contains(QStringLiteral("renamed_live")), "project changes cannot retain symbols from the retired project");
		std::cerr << "Project-switch check finished" << std::endl;
	}
	std::cerr << "Code index UI shell destroyed" << std::endl;
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
