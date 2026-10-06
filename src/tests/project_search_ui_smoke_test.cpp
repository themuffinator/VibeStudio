#include "app/application_shell.h"
#include "app/project_search_panel.h"
#include "app/studio_theme.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QStatusBar>
#include <QTextCursor>
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

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool waitForSearch(ProjectSearchPanel& panel)
{
	if (!panel.busy()) { return true; }
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (!panel.busy()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5);
	timeout.start(15000);
	loop.exec();
	return !panel.busy();
}

bool renderEvidence(QWidget& widget, const QString& name)
{
	const QString evidence = qEnvironmentVariable("VIBESTUDIO_SEARCH_TEST_EVIDENCE");
	if (evidence.isEmpty()) { return true; }
	QImage rendering(widget.size(), QImage::Format_ARGB32_Premultiplied);
	rendering.fill(Qt::transparent);
	widget.render(&rendering);
	return rendering.save(QDir(evidence).filePath(name));
}

bool applyWithConfirmation(ProjectSearchPanel& panel, const std::function<void()>& duringConfirmation = {})
{
	// Complete the offscreen message box directly. No input events are sent.
	QTimer answer;
	QObject::connect(&answer, &QTimer::timeout, &panel, [&]() {
		if (auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			if (auto* apply = question->button(QMessageBox::Apply)) {
				if (duringConfirmation) { duringConfirmation(); }
				apply->click();
			}
		}
	});
	answer.start(5);
	panel.applyPreview();
	answer.stop();
	return waitForSearch(panel);
}

class ExpandedLabels final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "vibestudio::ProjectSearchPanel") { return {}; }
		const QString text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

} // namespace

int main(int argc, char** argv)
{
	// Widget state, signals, and application rendering only. No OS input,
	// screen capture, external applications, or installed game content.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	const QString path = temp.filePath(QStringLiteral("source.cfg"));
	bool ok = true;
	for (const int scale : {100, 200}) {
		ExpandedLabels translator;
		if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		ProjectSearchPanel panel;
		panel.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		panel.resize(scale == 100 ? 1000 : 1600, scale == 100 ? 500 : 1000);
		panel.show();
		ok &= expect(writeFile(path, "player player_run\r\n  player\n"), "write panel fixture");
		AssetTextSearchRequest request;
		request.rootPath = temp.path();
		request.findText = QStringLiteral("player");
		request.replace = true;
		request.replaceText = QStringLiteral("enemy");
		request.wholeWords = true;
		panel.startSearch(request);
		ok &= expect(panel.busy() && !panel.findChild<QPushButton*>(QStringLiteral("projectSearchApply"))->isEnabled(), "search runs asynchronously and cannot apply before it finishes");
		ok &= expect(waitForSearch(panel) && panel.report().matchCount == 2 && panel.report().canApply(), "async search must produce the shared preview");
		auto* results = panel.resultsList();
		auto* details = panel.findChild<QPlainTextEdit*>(QStringLiteral("projectSearchDetails"));
		ok &= expect(results->count() == 2 && details->toPlainText().contains(QStringLiteral("enemy player_run")), "match selection must show before and after text");
		int openedLine = 0;
		panel.openMatch = [&](const AssetTextMatch& match) { openedLine = match.line; };
		results->itemActivated(results->item(1));
		ok &= expect(openedLine == 2, "row activation must pass the exact location to the editor");
		for (QWidget* field : QVector<QWidget*> {panel.findField(), panel.replaceField(), results, details,
			panel.findChild<QCheckBox*>(QStringLiteral("projectSearchCase")), panel.findChild<QCheckBox*>(QStringLiteral("projectSearchWords")),
			panel.findChild<QPushButton*>(QStringLiteral("projectSearchApply"))}) {
			ok &= expect(field && !field->accessibleName().isEmpty() && field->focusPolicy() != Qt::NoFocus, "search controls must expose keyboard focus and accessible names");
		}
		app.processEvents();
		ok &= expect(panel.findField()->width() > 200 && results->height() > 80 && details->width() > 180, "expanded and RTL layouts must retain usable search and preview areas");
		ok &= expect(renderEvidence(panel, QStringLiteral("project-search-%1.png").arg(scale)), "save application-rendered layout evidence");
		panel.replaceField()->setText(QStringLiteral("changed"));
		ok &= expect(!panel.findChild<QPushButton*>(QStringLiteral("projectSearchApply"))->isEnabled(), "changing query options must invalidate the reviewed preview");
		panel.startSearch(request);
		ok &= expect(waitForSearch(panel), "refresh preview");
		panel.beforeApply = [](const auto&) { return QStringLiteral("Unsaved source.cfg"); };
		panel.applyPreview();
		ok &= expect(!panel.busy() && readFile(path).startsWith("player") && details->toPlainText().contains(QStringLiteral("Unsaved")), "the host must be able to block replacement for unsaved documents");
		panel.beforeApply = {};
		ok &= expect(applyWithConfirmation(panel, [&]() {
			if (auto* question = QApplication::activeModalWidget()) {
				ok &= expect(renderEvidence(*question, QStringLiteral("project-replace-confirm-%1.png").arg(scale)), "render scaled replacement confirmation");
			}
		}) && panel.report().writtenFiles.size() == 1
			&& readFile(path) == QByteArray("enemy player_run\r\n  enemy\n"), "confirmed preview must apply exactly and preserve newlines");
		ok &= expect(!panel.findChild<QPushButton*>(QStringLiteral("projectSearchApply"))->isEnabled(), "a committed preview must require a new search");
		panel.startSearch(request);
		panel.cancel();
		ok &= expect(waitForSearch(panel) && panel.report().cancelled && !panel.report().canApply(), "UI cancellation must leave an explicit non-applicable report");
		panel.startSearch(request);
		panel.setRootPath(temp.filePath(QStringLiteral("other-project")));
		ok &= expect(waitForSearch(panel) && panel.report().matches.isEmpty() && results->count() == 0, "changing project must suppress stale worker results");
		request.buffers = {{path, QStringLiteral("unhosted"), 1, QStringLiteral("player\n"), QStringLiteral("UTF-8")}};
		panel.startSearch(request);
		ok &= expect(waitForSearch(panel) && panel.report().canApply(), "prepare a standalone buffer preview");
		ok &= expect(renderEvidence(panel, QStringLiteral("project-search-live-%1.png").arg(scale)), "render live source indicators and preview at both scales");
		panel.applyPreview();
		ok &= expect(!panel.busy() && readFile(path) == QByteArray("enemy player_run\r\n  enemy\n")
			&& panel.findChild<QLabel*>(QStringLiteral("projectSearchStatus"))->text().contains(QStringLiteral("editor host")),
			"the panel cannot apply buffer previews without a GUI document handler");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	// Real shell integration: open snapshots, undo, save and mixed disk/buffer
	// batches. These calls exercise the application without input injection.
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("test-settings.ini")));
	{
		StudioSettings settings;
		settings.setCurrentProjectPath(temp.path());
		settings.sync();
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ok &= expect(writeFile(path, "player\n"), "write shell fixture");
	{
		ApplicationShell shell;
		shell.openPathFromCommandLine(path);
		auto* panel = shell.findChild<ProjectSearchPanel*>();
		auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
		if (!panel || !editor) { return 1; }
		editor->insertPlainText(QStringLiteral("unsaved player "));
		AssetTextSearchRequest request;
		request.rootPath = temp.path();
		request.findText = QStringLiteral("player");
		request.replaceText = QStringLiteral("enemy");
		request.replace = true;
		request.includeGlobs = {QStringLiteral("source.cfg")};
		panel->startSearch(request);
		ok &= expect(waitForSearch(*panel) && panel->report().matchCount == 2 && panel->report().buffersScanned == 1
			&& !panel->report().matches.first().bufferId.isEmpty(), "shell search must use live editor text rather than stale disk");
		panel->resultsList()->itemActivated(panel->resultsList()->item(1));
		ok &= expect(editor->textCursor().position() == 15, "live search navigation uses the reviewed UTF-16 column");
		ok &= expect(applyWithConfirmation(*panel) && panel->report().succeeded() && panel->report().editedBuffers == QStringList {path}
			&& panel->report().writtenFiles.isEmpty() && readFile(path) == QByteArray("player\n")
			&& editor->toPlainText() == QStringLiteral("unsaved enemy enemy\n") && editor->document()->isModified(),
			"reviewed live replacements must edit without saving or losing previous unsaved work");
		editor->undo();
		ok &= expect(editor->toPlainText() == QStringLiteral("unsaved player player\n") && editor->document()->isModified(),
			"one Undo restores all replacements in a document while retaining earlier edits");
		editor->redo();
		auto* save = shell.findChild<QAction*>(QStringLiteral("code.save"));
		if (!save) { return 1; }
		save->trigger();
		ok &= expect(!editor->document()->isModified() && readFile(path) == QByteArray("unsaved enemy enemy\n"),
			"explicit Save persists reviewed buffer edits through the normal document service");
		request.findText = QStringLiteral("enemy");
		request.replaceText = QStringLiteral("player");
		panel->startSearch(request);
		ok &= expect(waitForSearch(*panel), "preview before a concurrent editor change");
		const auto staleMatch = panel->report().matches.first();
		const auto savedBytes = readFile(path);
		ok &= expect(applyWithConfirmation(*panel, [&]() { editor->insertPlainText(QStringLiteral("changed ")); })
			&& panel->report().dryRun && readFile(path) == savedBytes
			&& panel->findChild<QLabel*>(QStringLiteral("projectSearchStatus"))->text().contains(QStringLiteral("changed or closed")),
			"edits while confirmation is open must invalidate the entire reviewed batch");
		const int stalePosition = editor->textCursor().position();
		panel->openMatch(staleMatch);
		ok &= expect(editor->textCursor().position() == stalePosition && shell.statusBar()->currentMessage().contains(QStringLiteral("changed or closed")),
			"stale buffer results must not jump to an unrelated location");
		editor->undo();
		const QString inactive = temp.filePath(QStringLiteral("inactive.cfg"));
		const QString unopened = temp.filePath(QStringLiteral("unopened.cfg"));
		ok &= expect(writeFile(inactive, "player\r\n") && writeFile(unopened, "player\r\n"), "write mixed-batch fixtures");
		shell.openPathFromCommandLine(inactive);
		editor->insertPlainText(QStringLiteral("live "));
		shell.openPathFromCommandLine(path);
		QTextDocument* activeDocument = editor->document();
		request.findText = QStringLiteral("player");
		request.replaceText = QStringLiteral("enemy");
		request.includeGlobs = {QStringLiteral("source.cfg"), QStringLiteral("inactive.cfg"), QStringLiteral("unopened.cfg")};
		panel->startSearch(request);
		ok &= expect(waitForSearch(*panel) && panel->report().matchCount == 2 && panel->report().buffersScanned == 2, "inactive tabs participate in project search");
		const auto operationStarted = panel->operationStarted;
		panel->operationStarted = [&](bool applying) {
			operationStarted(applying);
			if (applying) { panel->cancel(); }
		};
		ok &= expect(applyWithConfirmation(*panel) && panel->report().cancelled && panel->report().editedBuffers.isEmpty()
			&& panel->report().writtenFiles.isEmpty() && readFile(unopened) == QByteArray("player\r\n"),
			"cancelling a mixed batch before its worker starts leaves disk and live documents untouched");
		panel->operationStarted = operationStarted;
		panel->startSearch(request);
		ok &= expect(waitForSearch(*panel), "refresh the cancelled mixed batch");
		ok &= expect(applyWithConfirmation(*panel) && panel->report().succeeded() && panel->report().editedBuffers == QStringList {inactive}
			&& panel->report().writtenFiles == QStringList {unopened} && panel->report().replacementsApplied == 2
			&& editor->document() == activeDocument && readFile(unopened) == QByteArray("enemy\r\n") && readFile(inactive) == QByteArray("player\r\n"),
			"mixed batches distinguish disk saves from inactive buffer edits without switching the active tab");
		shell.openPathFromCommandLine(inactive);
		ok &= expect(editor->toPlainText() == QStringLiteral("live enemy\n") && editor->document()->isModified(), "inactive replacements remain unsaved");
		editor->undo();
		ok &= expect(editor->toPlainText() == QStringLiteral("live player\n"), "inactive replacements retain their own undo step");
		ok &= expect(QFile::remove(inactive), "delete an open source file");
		request.includeGlobs = {QStringLiteral("inactive.cfg")};
		panel->startSearch(request);
		ok &= expect(waitForSearch(*panel) && panel->report().canApply() && panel->report().matchCount == 1, "deleted open files remain searchable");
		ok &= expect(applyWithConfirmation(*panel) && !QFile::exists(inactive) && editor->toPlainText() == QStringLiteral("live enemy\n"),
			"replacing a deleted open document never silently recreates its file");
		// A disk target becoming an open tab changes the destination semantics.
		request.findText = QStringLiteral("enemy");
		request.replaceText = QStringLiteral("player");
		request.includeGlobs = {QStringLiteral("unopened.cfg")};
		panel->startSearch(request);
		ok &= expect(waitForSearch(*panel), "preview an unopened file");
		shell.openPathFromCommandLine(unopened);
		panel->applyPreview();
		ok &= expect(readFile(unopened) == QByteArray("enemy\r\n")
			&& panel->findChild<QLabel*>(QStringLiteral("projectSearchStatus"))->text().contains(QStringLiteral("opened after preview")),
			"opening a disk target after preview requires a fresh buffer-aware preview");
		panel->startSearch(request);
		ok &= expect(waitForSearch(*panel) && applyWithConfirmation(*panel) && editor->document()->isModified()
			&& editor->toPlainText() == QStringLiteral("player\n") && readFile(unopened) == QByteArray("enemy\r\n"),
			"even clean open documents receive undoable unsaved edits");
		const QString unicodePath = temp.filePath(QStringLiteral("unicode.cfg"));
		const QByteArray unicodeBytes = QByteArray::fromHex("feffd83cdf0d00200070006c0061007900650072000a");
		ok &= expect(writeFile(unicodePath, unicodeBytes), "write a UTF-16 source with a supplementary character");
		shell.openPathFromCommandLine(unicodePath);
		request.findText = QStringLiteral("player");
		request.replaceText = QStringLiteral("enemy");
		request.includeGlobs = {QStringLiteral("unicode.cfg")};
		panel->startSearch(request);
		ok &= expect(waitForSearch(*panel) && panel->report().matchCount == 1 && panel->report().matches.first().column == 4, "UTF-16 live search exposes code-unit columns");
		panel->resultsList()->itemActivated(panel->resultsList()->item(0));
		ok &= expect(editor->textCursor().position() == 3, "navigation past supplementary Unicode characters must land on the exact match");
		ok &= expect(applyWithConfirmation(*panel) && readFile(unicodePath) == unicodeBytes, "UTF-16 buffer replacement remains unsaved");
		save->trigger();
		ok &= expect(readFile(unicodePath) == QByteArray::fromHex("feffd83cdf0d00200065006e0065006d0079000a") && !editor->document()->isModified(),
			"explicit Save after live replacement preserves UTF-16 byte order and BOM");
	}
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
