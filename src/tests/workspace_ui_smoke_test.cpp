#include "app/application_shell.h"
#include "app/studio_theme.h"
#include "core/workspace_document.h"
#include "core/extra_image.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStatusBar>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool ok, const char* message, const QString& detail = {}) { if (!ok) { std::cerr << message << ": " << detail.toStdString() << '\n'; } return ok; }
bool put(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
class Expansion final : public QTranslator {
public:
	QString translate(const char*, const char* text, const char*, int) const override { return QStringLiteral("[%1 %1]").arg(QString::fromUtf8(text)); }
};
}
int main(int argc, char** argv)
{
	// No input injection, OS capture, game launch or external execution.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings; settings.setRestoreSession(false); settings.setReducedMotion(true); settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 150));
	Expansion expansion; app.installTranslator(&expansion);
	QDir().mkpath(temp.filePath(QStringLiteral("project/textures")));
	QImage image(2, 2, QImage::Format_ARGB32); image.fill(qRgb(24, 48, 96)); QString error;
	bool ok = expect(put(temp.filePath(QStringLiteral("project/textures/wall.dds")), encodeExtraImage(image, true, &error)), "texture fixture");
	ok &= expect(put(temp.filePath(QStringLiteral("project/code.qc")), "void() main = {};\n"), "code fixture");
	WorkspaceDocument document;
	document.projectPath = temp.filePath(QStringLiteral("project")); document.packagePath = document.projectPath;
	document.codeFiles << temp.filePath(QStringLiteral("project/code.qc")); document.currentCodeFile = document.codeFiles.first();
	document.activeModule = QStringLiteral("textures"); document.assetSelections = {{"textures", "textures/wall.dds"}};
	document.extensions = {{"user", "preserve"}};
	const auto workspace = temp.filePath(QStringLiteral("fixture.vibeworkspace"));
	ok &= expect(writeWorkspaceDocument(workspace, document, {}, nullptr, &error), "workspace fixture", error);
	{
		ApplicationShell shell;
		const auto mode = [&] { return StudioMode(shell.findChild<QStackedWidget*>(QStringLiteral("modeStack"))->currentIndex()); };
		const auto* open = shell.findChild<QAction*>(QStringLiteral("workspace.open"));
		const auto* save = shell.findChild<QAction*>(QStringLiteral("workspace.save"));
		ok &= expect(open && save && open->isEnabled() && save->isEnabled() && !open->text().isEmpty() && !save->statusTip().isEmpty(), "translated menu and command actions");
		ok &= expect(shell.openWorkspaceFrom(workspace, &error) && mode() == StudioMode::Textures, "workspace opens on the saved module", error);
		ok &= expect(StudioSettings().currentProjectPath() == document.projectPath, "shared project context restored");
		const auto saved = temp.filePath(QStringLiteral("saved.vibeworkspace"));
		ok &= expect(shell.saveWorkspaceTo(saved, false, &error), "shell capture", error);
		WorkspaceDocument captured; QByteArray revision;
		ok &= expect(readWorkspaceDocument(saved, &captured, &revision, &error) && captured.packagePath == document.packagePath && captured.codeFiles == document.codeFiles &&
			captured.extensions == document.extensions && captured.assetSelections == document.assetSelections, "package, code and asset selection share workspace context", error);
		captured.activeModule = QStringLiteral("build");
		ok &= expect(writeWorkspaceDocument(saved, captured, revision, nullptr, &error) && !shell.saveWorkspaceTo(saved, true, &error), "GUI rejects external workspace changes");
		const auto broken = temp.filePath(QStringLiteral("bad.vibeworkspace")); put(broken, "{broken");
		ok &= expect(!shell.openWorkspaceFrom(broken, &error) && mode() == StudioMode::Textures, "invalid open preserves current module");
		// Command-line and drop routing use the same public opening path.
		shell.openPathFromCommandLine(workspace);
		ok &= expect(mode() == StudioMode::Textures, "workspace path routing");
	}
	app.removeTranslator(&expansion);
	return ok ? 0 : 1;
}
