#include "app/application_shell.h"
#include "app/level_document_dialog.h"
#include "app/studio_theme.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool waitFor(const std::function<bool()>& condition)
{
	QElapsedTimer time;
	time.start();
	while (!condition() && time.elapsed() < 15000) {
		QApplication::processEvents();
		QThread::msleep(5);
	}
	return condition();
}
class ExpandedLabels final : public QTranslator
{
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "NewLevelMapDialog") {
			return {};
		}
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 3, QLatin1Char('~')));
	}
};
} // namespace

int main(int argc, char** argv)
{
	// Direct widget/model commands and widget rendering only. No input injection
	// or OS capture, and all output belongs to the test's temporary settings root.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	bool ok = true;
	QString error;
	for (int scale : {100, 200}) {
		ExpandedLabels translator;
		if (scale == 200) {
			app.installTranslator(&translator);
		}
		applyStudioTheme(app,
						 studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		NewLevelMapDialog dialog;
		if (scale == 200) {
			dialog.setLayoutDirection(Qt::RightToLeft);
			dialog.resize(950, 850);
		}
		dialog.show();
		app.processEvents();
		auto* game = dialog.findChild<QComboBox*>(QStringLiteral("newMapGame"));
		auto* marker = dialog.findChild<QLineEdit*>(QStringLiteral("newMapMarker"));
		auto* preset = dialog.findChild<QComboBox*>(QStringLiteral("newMapPreset"));
		auto* wall = dialog.findChild<QLineEdit*>(QStringLiteral("newMapWall"));
		auto* create = dialog.findChild<QPushButton*>(QStringLiteral("createMapButton"));
		ok &= expect(game && marker && preset && wall && create, "new map controls are discoverable");
		if (!game || !marker || !preset || !wall || !create) {
			return EXIT_FAILURE;
		}
		ok &= expect(!marker->isVisible() && game->focusPolicy() != Qt::NoFocus && !game->accessibleName().isEmpty(),
					 "format control exposes focus and an accessible name");
		game->setCurrentIndex(game->findData(QStringLiteral("hexen")));
		ok &= expect(marker->isVisible() && dialog.request().game == QStringLiteral("hexen"), "Doom marker appears for binary WAD formats");
		preset->setCurrentIndex(1);
		ok &= expect(!wall->isEnabled() && !dialog.request().starterRoom, "empty preset disables unused texture controls");
		preset->setCurrentIndex(0);
		marker->setText(QStringLiteral("bad/name"));
		create->click();
		ok &= expect(dialog.isVisible() && !dialog.findChild<QLabel*>(QStringLiteral("newMapError"))->text().isEmpty(),
					 "invalid marker is shown inline without creating a map");
		marker->setText(QStringLiteral("map03"));
		LevelMapDocument created;
		ok &= expect(createLevelMap(dialog.request(), &created, &error) && created.mapName == QStringLiteral("MAP03"),
					 "UI request uses the shared creation service", error);
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			for (int bottom : {0, 1}) {
				auto* scroll = dialog.findChild<QScrollArea*>();
				scroll->verticalScrollBar()->setValue(bottom ? scroll->verticalScrollBar()->maximum() : 0);
				app.processEvents();
				QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				dialog.render(&image);
				ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("new-map-%1-%2.png").arg(scale).arg(bottom))),
							 "capture new map controls through widget rendering");
			}
		}
		create->click();
		ok &= expect(dialog.result() == QDialog::Accepted, "valid new map is accepted");
		if (scale == 200) {
			app.removeTranslator(&translator);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	LevelMapDocument document;
	createLevelMap({}, &document, &error);
	const QString directory = levelMapRecoveryDirectory();
	{
		LevelRecoveryWriter writer(directory);
		ok &= expect(writer.checkpoint(document), "background recovery starts");
		writer.retire();
		ok &= expect(waitFor([&]() { return !writer.busy(); }) && listLevelMapRecoveries(directory).isEmpty(),
					 "retiring an in-flight write prevents stale recovery resurrection");
		ok &= expect(writer.checkpoint(document) && waitFor([&]() { return !writer.busy(); }),
					 "next document can checkpoint after retired work");
		ok &= expect(listLevelMapRecoveries(directory).size() == 1 && !writer.checkpoint(document),
					 "unchanged revision does not rewrite its checkpoint");
		writer.retire();
	}
	{
		LevelRecoveryWriter writer(directory);
		writer.checkpoint(document);
		writer.retire();
		// Destructor waits/cancels and removes a checkpoint retired before finish.
	}
	ok &= expect(listLevelMapRecoveries(directory).isEmpty(), "writer destruction removes retired work");
	{
		ApplicationShell shell;
		ok &= expect(shell.createLevelDocument({}, &error) && shell.levelDocument().brushes.size() == 6,
					 "shell creates an editable untitled map", error);
		ok &= expect(shell.findChild<QPushButton*>(QStringLiteral("saveLevelMapButton"))->isEnabled(), "untitled maps enable Save");
		shell.checkpointLevelDocument();
		ok &= expect(waitFor([&]() { return !listLevelMapRecoveries(directory).isEmpty(); }), "shell checkpoints its unsaved map");
		const QString output = QDir(temp.path()).filePath(QStringLiteral("saved.map"));
		ok &= expect(shell.saveLevelDocument(output, false, &error) && shell.levelDocument().sourcePath == output,
					 "shell save adopts the new document path", error);
		ok &= expect(waitFor([&]() { return listLevelMapRecoveries(directory).isEmpty(); }), "successful save retires recovery");
		const auto bytes = serializeLevelMap(shell.levelDocument()).bytes;
		const QString broken = QDir(temp.path()).filePath(QStringLiteral("bad.wad"));
		QFile file(broken);
		ok &= expect(file.open(QIODevice::WriteOnly) && file.write("PWAD") == 4, "write malformed WAD fixture");
		file.close();
		shell.openPathFromCommandLine(broken);
		ok &= expect(shell.levelDocument().sourcePath == output && serializeLevelMap(shell.levelDocument()).bytes == bytes,
					 "failed shell open preserves the working map");
		auto* path = shell.findChild<QLineEdit*>(QStringLiteral("levelMapPath"));
		ok &= expect(path && path->text() == output, "failed open restores the current path control");
		const QString recovery = writeLevelMapRecovery(document, directory, QStringLiteral("ca735c42-3593-4db0-8b26-5b8472f5a6ca"), &error);
		ok &= expect(shell.recoverLevelDocument(recovery, &error) && shell.levelDocument().sourcePath.isEmpty(),
					 "shell restores an untitled map without touching a source", error);
		ok &= expect(shell.saveLevelDocument(output, true, &error) && QFileInfo::exists(recovery),
					 "save after recovery preserves the imported checkpoint until explicit deletion", error);
		const QString codePath = QDir(temp.path()).filePath(QStringLiteral("shared.txt"));
		QFile codeFile(codePath);
		ok &= expect(codeFile.open(QIODevice::WriteOnly) && codeFile.write("original") == 8, "write shared Code fixture");
		codeFile.close();
		shell.openPathFromCommandLine(codePath);
		auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
		if (!expect(editor != nullptr, "Code editor is present")) {
			return EXIT_FAILURE;
		}
		editor->setPlainText(QStringLiteral("unsaved Code changes"));
		editor->document()->setModified(true);
		ok &= expect(!shell.saveLevelDocument(codePath, true, &error) && editor->document()->isModified(),
					 "map save protects unsaved Code edits");
		ok &= expect(codeFile.open(QIODevice::ReadOnly) && codeFile.readAll() == QByteArray("original"),
					 "blocked map save leaves shared disk content intact");
		codeFile.close();
		editor->document()->setModified(false);
		ok &= expect(shell.saveLevelDocument(codePath, true, &error) && editor->toPlainText().contains(QStringLiteral("worldspawn")) &&
						 !editor->document()->isModified(),
					 "map save refreshes an already-open clean Code view", error);
		ok &= expect(shell.close(), "saved shell closes without a prompt");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
