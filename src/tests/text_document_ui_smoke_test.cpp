#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/project_search_panel.h"
#include "app/studio_theme.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
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

void replaceWord(QPlainTextEdit* editor, const QString& before, const QString& after)
{
	editor->setTextCursor(editor->document()->find(before));
	editor->insertPlainText(after);
}

class ExpandedLabels final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "TextDocument" && QByteArray(context) != "vibestudio::ApplicationShell") { return {}; }
		const QString text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QStringLiteral(" ~").repeated(text.size() / 6));
	}
};

bool renderEvidence(QWidget& widget, const QString& name)
{
	QImage rendering(widget.size(), QImage::Format_ARGB32_Premultiplied);
	rendering.fill(Qt::transparent);
	widget.render(&rendering);
	const QString evidence = qEnvironmentVariable("VIBESTUDIO_TEXT_TEST_EVIDENCE");
	return !rendering.isNull() && (evidence.isEmpty() || rendering.save(QDir(evidence).filePath(name)));
}

} // namespace

int main(int argc, char** argv)
{
	// Direct widget/service calls only: no keyboard/mouse events or OS capture.
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
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{
		StudioSettings settings;
		settings.setCurrentProjectPath(temp.path());
		settings.sync();
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const QString first = temp.filePath(QStringLiteral("mixed.qc"));
	const QString second = temp.filePath(QStringLiteral("utf16.cfg"));
	const QString invalid = temp.filePath(QStringLiteral("legacy.cfg"));
	const QByteArray bom = QByteArray::fromHex("efbbbf");
	const QByteArray firstBytes = bom + QStringLiteral("alpha\r\nbeta\ngamma\rlast\u00a0value\u2028soft\u2029paragraph").toUtf8();
	bool ok = writeFile(first, firstBytes) && writeFile(second, QByteArray::fromHex("feff0061000d000a0062"))
		&& writeFile(invalid, QByteArray::fromHex("61ff62"));
	{
		ApplicationShell shell;
		shell.openPathFromCommandLine(first);
		auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
		auto* save = shell.findChild<QAction*>(QStringLiteral("code.save"));
		if (!editor || !save) { return 1; }
		replaceWord(editor, QStringLiteral("alpha"), QStringLiteral("edited"));
		ok &= expect(save->isEnabled(), "editing enables Save");
		save->trigger();
		const QByteArray savedFirst = bom + QStringLiteral("edited\r\nbeta\ngamma\rlast\u00a0value\u2028soft\u2029paragraph").toUtf8();
		ok &= expect(readFile(first) == savedFirst && !editor->document()->isModified(), "Code Save must preserve BOM, mixed endings, NBSP, and Unicode breaks");
		bool metadata = false;
		for (auto* widget : shell.findChildren<QWidget*>()) {
			if (widget->accessibleName() == QStringLiteral("Editor encoding, line endings, and save state")) {
				metadata = widget->accessibleDescription().contains(QStringLiteral("UTF-8 with BOM"))
					&& widget->toolTip().contains(QStringLiteral("Mixed:"));
			}
		}
		ok &= expect(metadata, "encoding and mixed endings must be accessible without relying on color");
		shell.resize(1440, 1000);
		shell.show();
		app.processEvents();
		ok &= expect(renderEvidence(shell, QStringLiteral("code-save-100.png")), "render the Code readout at standard scale");
		editor->undo();
		save->trigger();
		ok &= expect(readFile(first) == firstBytes, "undo followed by Save restores the source text and formatting");
		shell.openPathFromCommandLine(second);
		ok &= expect(editor->toPlainText() == QStringLiteral("a\nb"), "BOM-marked UTF-16 loads in the Code editor");
		replaceWord(editor, QStringLiteral("a"), QStringLiteral("A"));
		save->trigger();
		ok &= expect(readFile(second) == QByteArray::fromHex("feff0041000d000a0062"), "tabs retain separate encoding metadata");
		shell.openPathFromCommandLine(first);
		replaceWord(editor, QStringLiteral("alpha"), QStringLiteral("local"));
		ok &= expect(writeFile(first, "external\n"), "write unobserved external edit");
		bool sawConflict = false;
		QTimer answer;
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				if (box->objectName() == QStringLiteral("codeSaveConflict")) {
					sawConflict = box->defaultButton() == box->button(QMessageBox::Cancel);
					box->button(QMessageBox::Cancel)->click();
				} else { box->accept(); }
			}
		});
		answer.start(5);
		save->trigger();
		answer.stop();
		ok &= expect(sawConflict && readFile(first) == QByteArray("external\n") && editor->document()->isModified(), "a cancelled conflict must retain disk bytes and unsaved edits");
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		bool sawSecondChange = false;
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				if (auto* overwrite = box->findChild<QPushButton*>(QStringLiteral("overwriteCodeFile"))) {
					ok &= expect(writeFile(first, "changed during review\n"), "change the file while the overwrite prompt is open");
					overwrite->click();
				} else { sawSecondChange = true; box->accept(); }
			}
		});
		answer.start(5);
		save->trigger();
		answer.stop();
		ok &= expect(sawSecondChange && readFile(first) == QByteArray("changed during review\n") && editor->document()->isModified(), "a change during overwrite review must still be blocked");
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		ExpandedLabels translator;
		app.installTranslator(&translator);
		applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 200));
		shell.setLayoutDirection(Qt::RightToLeft);
		shell.resize(1920, 1440);
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				if (auto* overwrite = box->findChild<QPushButton*>(QStringLiteral("overwriteCodeFile"))) {
					ok &= expect(renderEvidence(*box, QStringLiteral("code-save-conflict-200.png")), "render the expanded RTL conflict dialog at 200 percent");
					overwrite->click();
				}
				else { box->accept(); }
			}
		});
		answer.start(5);
		save->trigger();
		answer.stop();
		ok &= expect(readFile(first) == bom + QStringLiteral("local\r\nbeta\ngamma\rlast\u00a0value\u2028soft\u2029paragraph").toUtf8()
			&& !editor->document()->isModified(), "reviewed overwrite retains the editing session's format");
		app.processEvents();
		ok &= expect(renderEvidence(shell, QStringLiteral("code-save-200.png")), "render the expanded RTL Code readout at 200 percent");
		app.removeTranslator(&translator);
		const QString sharedMap = temp.filePath(QStringLiteral("shared.map"));
		ok &= expect(writeFile(sharedMap, "{\n\"classname\" \"worldspawn\"\n\"message\" \"before\"\n}\n{\n\"classname\" \"light\"\n\"origin\" \"0 0 64\"\n}\n"), "write shared map fixture");
		shell.openPathFromCommandLine(sharedMap);
		auto* search = shell.findChild<ProjectSearchPanel*>();
		auto* viewport = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		auto* mapUndo = shell.findChild<QAction*>(QStringLiteral("map.undo"));
		if (!search || !viewport || !mapUndo || shell.levelDocument().entities.size() != 2) { return 1; }
		AssetTextMatch mapLocation;
		mapLocation.filePath = sharedMap;
		mapLocation.line = 1;
		mapLocation.textSha256 = assetTextSnapshotHash(readTextFile(sharedMap).text);
		search->openMatch(mapLocation);
		replaceWord(editor, QStringLiteral("before"), QStringLiteral("after"));
		save->trigger();
		bool mapRefreshed = false;
		for (const auto& property : shell.levelDocument().entities.first().properties) {
			if (property.key == QStringLiteral("message")) { mapRefreshed = property.value == QStringLiteral("after"); }
		}
		ok &= expect(mapRefreshed, "Code Save reloads a shared clean Levels document");
		viewport->selectionSetChanged({{LevelMapSelectionKind::Entity, 1}});
		viewport->moveRequested(viewport->gridSize(), 0, 0);
		ok &= expect(mapUndo->isEnabled(), "the map fixture has an unsaved undoable edit");
		AssetTextSearchRequest mapSearch;
		mapSearch.rootPath = temp.path();
		mapSearch.findText = QStringLiteral("after");
		mapSearch.replaceText = QStringLiteral("replacement");
		mapSearch.replace = true;
		mapSearch.includeGlobs = {QFileInfo(sharedMap).fileName()};
		mapSearch.buffers = search->captureBuffers();
		const auto mapPreview = findReplaceProjectText(mapSearch);
		ok &= expect(mapPreview.canApply() && !search->beforeApply(mapPreview).isEmpty(), "unsaved Levels geometry blocks replacements in its shared Code buffer");
		replaceWord(editor, QStringLiteral("after"), QStringLiteral("blocked"));
		const auto savedMap = readFile(sharedMap);
		save->trigger();
		ok &= expect(readFile(sharedMap) == savedMap && editor->document()->isModified(), "unsaved Levels edits block Code Save of the same map");
		mapUndo->trigger();
		save->trigger();
		ok &= expect(readFile(sharedMap).contains("blocked") && !editor->document()->isModified(), "Code Save becomes available after the map edit is undone");
		shell.openPathFromCommandLine(second);
		replaceWord(editor, QStringLiteral("A"), QStringLiteral("restored"));
		ok &= expect(QFile::remove(second), "remove the open source fixture");
		QObject::disconnect(&answer, nullptr, &shell, nullptr);
		bool sawRecreate = false;
		QObject::connect(&answer, &QTimer::timeout, &shell, [&]() {
			if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
				if (auto* recreate = box->findChild<QPushButton*>(QStringLiteral("recreateCodeFile"))) { sawRecreate = true; recreate->click(); }
				else { box->accept(); }
			}
		});
		answer.start(5);
		save->trigger();
		answer.stop();
		ok &= expect(sawRecreate && readTextFile(second).text == QStringLiteral("restored\nb")
			&& readFile(second).startsWith(QByteArray::fromHex("feff")) && !editor->document()->isModified(), "reviewed recreation restores a deleted file with its tab's encoding");
		shell.openPathFromCommandLine(invalid);
		ok &= expect(editor->isReadOnly() && !save->isEnabled() && readFile(invalid) == QByteArray::fromHex("61ff62"), "invalid encoding previews must be read-only");
	}
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
