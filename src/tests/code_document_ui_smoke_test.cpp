#include "app/application_shell.h"
#include "app/code_editor.h"
#include "app/code_recovery.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QImage>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QThread>
#include <QTranslator>
#include <QUuid>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	else if (qEnvironmentVariableIsSet("VIBESTUDIO_TEST_TRACE")) { std::cerr << "PASS: " << message << std::endl; }
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
bool waitFor(const std::function<bool()>& done)
{
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(2); }
	return done();
}
class ExpandedLabels final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "CodeRecovery" && QByteArray(context) != "vibestudio::ApplicationShell") { return {}; }
		const QString text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QStringLiteral(" ~").repeated(text.size() / 6));
	}
};
bool render(QWidget& widget, const QString& name)
{
	const QString evidence = qEnvironmentVariable("VIBESTUDIO_TEXT_TEST_EVIDENCE");
	QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	widget.render(&image);
	return !image.isNull() && (evidence.isEmpty() || image.save(QDir(evidence).filePath(name)));
}
} // namespace

int main(int argc, char** argv)
{
	// Only direct widget/service calls: no input injection or OS screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool ok = true;
	{
		ApplicationShell shell;
		shell.findChild<QTimer*>(QStringLiteral("codeRecoveryTimer"))->stop();
		auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
		auto* tabs = shell.findChild<QTabBar*>(QStringLiteral("codeTabs"));
		auto* save = shell.findChild<QAction*>(QStringLiteral("code.save"));
		auto* saveAs = shell.findChild<QAction*>(QStringLiteral("code.saveAs"));
		auto* create = shell.findChild<QAction*>(QStringLiteral("code.new"));
		auto* close = shell.findChild<QAction*>(QStringLiteral("code.closeFile"));
		if (!editor || !tabs || !save || !saveAs || !create || !close) { return 1; }
		create->trigger();
		QTextDocument* first = editor->document();
		ok &= expect(tabs->count() == 1 && !editor->isReadOnly() && save->isEnabled() && saveAs->isEnabled(), "an empty draft is writable and can be saved without a project");
		editor->insertPlainText(QStringLiteral("first\n雪"));
		const QString previousDirectory = QDir::currentPath();
		writeFile(temp.filePath(QStringLiteral("unrelated.qc")), "void() unrelated_symbol = {};\n");
		QDir::setCurrent(temp.path());
		const auto names = dynamic_cast<StudioCodeEditor*>(editor)->completionsFor(QStringLiteral("fir"));
		QDir::setCurrent(previousDirectory);
		ok &= expect(names.contains(QStringLiteral("first")) && !names.contains(QStringLiteral("unrelated_symbol")), "draft completions must not index the working directory without a project");
		create->trigger();
		QTextDocument* second = editor->document();
		editor->insertPlainText(QStringLiteral("second"));
		ok &= expect(first != second && tabs->count() == 2 && tabs->tabText(0) != tabs->tabText(1), "draft tabs have distinct identities and names");
		shell.checkpointCodeDocuments();
		ok &= expect(waitFor([&]() { return listTextRecoveries(textRecoveryDirectory()).records.size() == 2; }), "both unsaved drafts receive independent recovery copies");
		tabs->setCurrentIndex(0);
		ok &= expect(editor->document() == first && editor->toPlainText() == QStringLiteral("first\n雪"), "switching draft tabs retains separate buffers");
		QTimer answer;
		bool pickerSeen = false;
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) { pickerSeen = true; picker->reject(); }
		});
		answer.start(5);
		save->trigger();
		answer.stop();
		ok &= expect(pickerSeen && first->isModified() && tabs->count() == 2, "cancelling the first Save retains the unsaved draft");
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		const QString targetPath = temp.filePath(QStringLiteral("draft.qc"));
		QString error;
		const int caret = editor->textCursor().position();
		ok &= expect(shell.saveCodeDocumentAs(inspectTextWriteTarget(targetPath), &error)
			&& readFile(targetPath) == QStringLiteral("first\n雪").toUtf8() && editor->document() == first
			&& !first->isModified() && editor->textCursor().position() == caret, "Save As adopts the path without replacing the document or cursor");
		ok &= expect(tabs->tabText(0) == QStringLiteral("draft.qc") && tabs->tabButton(0, QTabBar::RightSide)->accessibleName().contains(QStringLiteral("draft.qc")), "Save As refreshes tab and close-button accessible names");
		ok &= expect(waitFor([&]() { return listTextRecoveries(textRecoveryDirectory()).records.size() == 1; }), "Save As retires only the saved document's recovery copy");
		editor->undo();
		ok &= expect(first->isModified() && editor->toPlainText().isEmpty(), "Save As preserves undo history");
		editor->redo();
		ok &= expect(!first->isModified(), "redo to the saved contents restores the clean state");
		tabs->setCurrentIndex(1);
		const auto stale = inspectTextWriteTarget(targetPath);
		ok &= expect(writeFile(targetPath, "external") && !shell.saveCodeDocumentAs(stale, &error)
			&& second->isModified() && readFile(targetPath) == "external", "a destination changed after review must keep both buffers and the disk file");
		bool sawOverwriteReview = false;
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* picker = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
				picker->selectFile(targetPath);
				QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
			} else if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				if (auto* replace = box->findChild<QPushButton*>(QStringLiteral("replaceCodeDestination"))) {
					sawOverwriteReview = box->defaultButton() == box->button(QMessageBox::Cancel);
					writeFile(targetPath, "changed during Save As review");
					replace->click();
				} else { box->accept(); }
			}
		});
		answer.start(5);
		saveAs->trigger();
		answer.stop();
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		ok &= expect(sawOverwriteReview && second->isModified() && tabs->count() == 2
			&& readFile(targetPath) == "changed during Save As review", "Save As defaults to Cancel and rejects a destination changed during its actual overwrite dialog");
		ok &= expect(shell.saveCodeDocumentAs(inspectTextWriteTarget(targetPath), &error)
			&& tabs->count() == 1 && editor->document() == second && readFile(targetPath) == "second", "replacing a clean destination tab retains the saving tab's undo history");
		editor->insertPlainText(QStringLiteral(" modified"));
		create->trigger();
		QTextDocument* third = editor->document();
		editor->insertPlainText(QStringLiteral("third"));
		ok &= expect(!shell.saveCodeDocumentAs(inspectTextWriteTarget(targetPath), &error)
			&& readFile(targetPath) == "second" && tabs->count() == 2, "Save As refuses a destination with another tab's unsaved edits");
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { box->button(QMessageBox::Cancel)->click(); }
		});
		answer.start(5);
		close->trigger();
		answer.stop();
		ok &= expect(tabs->count() == 2 && editor->document() == third && third->isModified(), "cancelling close retains the selected draft");
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { box->button(QMessageBox::Discard)->click(); }
		});
		answer.start(5);
		close->trigger();
		answer.stop();
		ok &= expect(tabs->count() == 1 && editor->document() == second, "discard closes only the intended draft");
		create->trigger();
		const QString emptyPath = temp.filePath(QStringLiteral("empty.txt"));
		ok &= expect(shell.saveCodeDocumentAs(inspectTextWriteTarget(emptyPath), &error)
			&& QFile::exists(emptyPath) && QFileInfo(emptyPath).size() == 0, "saving a clean empty draft creates a real file");
		shell.resize(1440, 1000);
		shell.show();
		create->trigger();
		editor->insertPlainText(QStringLiteral("// New script\nvoid() main =\n{\n};\n"));
		app.processEvents();
		ok &= expect(render(shell, QStringLiteral("code-draft-100.png")), "render the unsaved document workbench");
		const QString originalPath = temp.filePath(QStringLiteral("recovery.cfg"));
		const QByteArray originalBytes = QByteArray::fromHex("feff0061000d000a0062");
		ok &= expect(writeFile(originalPath, originalBytes), "write restoration fixture");
		TextRecoverySnapshot snapshot {readTextFile(originalPath), QStringLiteral("restored\n雪"), QStringLiteral("recovery.cfg")};
		snapshot.position = 5;
		snapshot.anchor = 1;
		const QString checkpoint = writeTextRecovery(snapshot, textRecoveryDirectory(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		ok &= expect(shell.recoverCodeDocument(checkpoint, &error) && editor->toPlainText() == snapshot.text
			&& editor->document()->isModified() && editor->textCursor().position() == 5 && editor->textCursor().anchor() == 1
			&& readFile(originalPath) == originalBytes, "restoring opens a draft with its caret and selection, leaving the source untouched");
		QByteArray recoveredBytes;
		encodeTextFile(snapshot.source, snapshot.text, &recoveredBytes);
		const QString recoveredPath = temp.filePath(QStringLiteral("recovered.cfg"));
		ok &= expect(shell.saveCodeDocumentAs(inspectTextWriteTarget(recoveredPath), &error)
			&& readFile(recoveredPath) == recoveredBytes && QFile::exists(checkpoint) && readFile(originalPath) == originalBytes,
			"recovered Save As retains encoding and the imported copy without overwriting the original");
		ExpandedLabels translator;
		app.installTranslator(&translator);
		applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 200));
		dynamic_cast<StudioCodeEditor*>(editor)->setBaseFont(studioMonospaceFont());
		shell.setLayoutDirection(Qt::RightToLeft);
		shell.resize(1920, 1440);
		app.processEvents();
		ok &= expect(render(shell, QStringLiteral("code-draft-200.png")), "render expanded RTL text lifecycle at 200 percent");
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		bool recoveryBrowserSeen = false;
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			auto* dialog = QApplication::activeModalWidget();
			if (!dialog || dialog->objectName() != QStringLiteral("codeRecoveryDialog")) { return; }
			auto* list = dialog->findChild<QListWidget*>(QStringLiteral("codeRecoveryList"));
			if (!list || list->count() == 0) { return; }
			list->setCurrentRow(0);
			auto* details = dialog->findChild<QPlainTextEdit*>(QStringLiteral("codeRecoveryDetails"));
			ok &= expect(dialog->layoutDirection() == Qt::RightToLeft && details && details->isReadOnly()
				&& details->focusPolicy() != Qt::NoFocus && details->toPlainText().contains(QStringLiteral("original")), "recovery details must support keyboard scrolling in RTL layouts");
			auto* restore = dialog->findChild<QPushButton*>(QStringLiteral("restoreCodeRecovery"));
			auto* discard = dialog->findChild<QPushButton*>(QStringLiteral("discardCodeRecovery"));
			recoveryBrowserSeen = restore && restore->isEnabled() && discard && !discard->isEnabled();
			ok &= expect(render(*dialog, QStringLiteral("code-recovery-200.png")), "render the recovery browser with RTL expanded labels");
			restore->click();
		});
		answer.start(5);
		const QString chosen = chooseTextRecovery(&shell, textRecoveryDirectory());
		answer.stop();
		ok &= expect(recoveryBrowserSeen && !chosen.isEmpty(), "the recovery browser restores verified copies and protects live owners from discard");
		app.removeTranslator(&translator);
		shell.checkpointCodeDocuments();
		ok &= expect(waitFor([&]() { return listTextRecoveries(textRecoveryDirectory()).records.size() >= 3; }), "checkpoint unsaved documents before testing close cancellation");
		const int countBeforeClose = static_cast<int>(listTextRecoveries(textRecoveryDirectory()).records.size());
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		int closeQuestions = 0;
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				box->button(++closeQuestions == 1 ? QMessageBox::Discard : QMessageBox::Cancel)->click();
			}
		});
		answer.start(5);
		ok &= expect(!shell.close(), "a later Cancel must abort window closure");
		answer.stop();
		ok &= expect(listTextRecoveries(textRecoveryDirectory()).records.size() == countBeforeClose, "window close cancellation retains recovery for previously considered documents");
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { box->button(QMessageBox::Discard)->click(); }
		});
		answer.start(5);
		ok &= expect(shell.close(), "approved discards allow the window to close");
		answer.stop();
		ok &= expect(waitFor([&]() { return listTextRecoveries(textRecoveryDirectory()).records.size() == 1; }) && QFile::exists(checkpoint), "approved closure retires owned copies and preserves the imported checkpoint");
	}
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
