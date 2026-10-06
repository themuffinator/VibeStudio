#include "app/application_shell.h"
#include "app/code_files_panel.h"
#include "app/code_files_worker.h"
#include "app/studio_theme.h"
#include "app/syntax_highlight.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
bool write(const QString& path, const QByteArray& content)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}
bool waitFor(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return ready();
}
int visibleFiles(QTreeWidget* tree)
{
	int count = 0;
	for (QTreeWidgetItemIterator it(tree); *it; ++it) {
		if (!(*it)->isHidden() && !(*it)->data(0, Qt::UserRole).toString().isEmpty()) { ++count; }
	}
	return count;
}
class ExpandedLabels final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "vibestudio::CodeFilesPanel") { return {}; }
		const QString text = QString::fromUtf8(source);
		return text + QStringLiteral(" ~").repeated(text.size() / 6);
	}
};
} // namespace

int main(int argc, char** argv)
{
	// Direct widget/service calls and QWidget rendering, never input injection.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	bool ok = true;
	const QString project = temp.filePath(QStringLiteral("project"));
	const QString other = temp.filePath(QStringLiteral("other"));
	for (int i = 0; i < 350; ++i) {
		ok &= expect(write(QDir(project).filePath(QStringLiteral("scripts/source-%1.qc").arg(i)), "void() game = {\n};\n"), "write source list fixture");
	}
	const QString main = QDir(project).filePath(QStringLiteral("src/main.cpp"));
	ok &= expect(write(main, "int main() {}\n"), "write C++ fixture");
	ok &= expect(write(QDir(other).filePath(QStringLiteral("other.qc")), "void() second = {};\n"), "write replacement project");
	ok &= expect(saveProjectManifest(defaultProjectManifest(other, QStringLiteral("Other project"))), "create recognized project");
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		auto* tree = new QTreeWidget;
		tree->setHeaderHidden(true);
		tree->setAccessibleName(QStringLiteral("Project source tree"));
		auto* filter = new QLineEdit;
		filter->setAccessibleName(QStringLiteral("Project file filter"));
		filter->setPlaceholderText(QStringLiteral("Filter project files"));
		CodeFilesPanel panel(tree, filter);
		int starts = 0;
		panel.operationStarted = [&]() { ++starts; };
		bool observedBatch = false;
		QTimer pulse;
		QObject::connect(&pulse, &QTimer::timeout, &panel, [&]() {
			const int shown = visibleFiles(tree);
			observedBatch |= panel.busy() && shown > 0 && shown < 351;
		});
		pulse.start(0);
		panel.setRootPath(project);
		ok &= expect(panel.busy() && tree->topLevelItemCount() == 0, "initial file scan returns before traversal or row creation");
		panel.setCurrentPath(main);
		ok &= expect(waitFor([&]() { return !panel.busy(); }) && panel.result().files.size() == 351 && observedBatch, "file rows are published in event-loop batches");
		pulse.stop();
		ok &= expect(tree->currentItem() && tree->currentItem()->data(0, Qt::UserRole).toString() == main, "active file selection survives asynchronous population");
		const int before = starts;
		panel.setRootPath(project);
		panel.setRootPath(project);
		ok &= expect(!panel.busy() && starts == before, "ordinary mode refresh reuses a complete catalog");
		const auto scripts = tree->findItems(QStringLiteral("scripts"), Qt::MatchExactly);
		if (!scripts.isEmpty()) { scripts.first()->setExpanded(true); }
		filter->setText(QStringLiteral("language=cpp size=14"));
		ok &= expect(visibleFiles(tree) == 1, "metadata filtering finds C++ files");
		ok &= expect(write(main, QByteArray(96, 'x')), "change the disk size behind the cached catalog");
		filter->setText(QStringLiteral("size=96"));
		ok &= expect(visibleFiles(tree) == 0 && starts == before, "filter changes use cached sizes and do not read or rescan disk");
		panel.refresh();
		ok &= expect(waitFor([&]() { return !panel.busy(); }) && visibleFiles(tree) == 1, "explicit refresh updates cached metadata under the existing filter");
		const auto restoredScripts = tree->findItems(QStringLiteral("scripts"), Qt::MatchExactly);
		ok &= expect(!restoredScripts.isEmpty() && restoredScripts.first()->isExpanded(), "refresh preserves folder expansion");
		filter->setText(QStringLiteral("sizze>0"));
		ok &= expect(panel.findChild<QLabel*>(QStringLiteral("codeFilesFilterStatus"))->text().contains(QStringLiteral("sizze")), "unknown property filters are explained inline");
		filter->clear();
		bool cancelledDuringRows = false;
		QTimer cancelRows;
		QObject::connect(&cancelRows, &QTimer::timeout, &panel, [&]() {
			const int shown = visibleFiles(tree);
			if (panel.busy() && shown > 0 && shown < 351) { cancelledDuringRows = true; panel.cancel(); cancelRows.stop(); }
		});
		cancelRows.start(0);
		panel.refresh();
		ok &= expect(waitFor([&]() { return !panel.busy(); }) && cancelledDuringRows && panel.result().cancelled
			&& panel.result().files.size() < 351, "cancel also stops batched row publication and labels the visible subset");
		cancelRows.stop();
		panel.refresh();
		panel.cancel();
		ok &= expect(waitFor([&]() { return !panel.busy(); }) && panel.result().cancelled, "file scan cancellation has an explicit terminal state");
		const int afterCancel = starts;
		panel.setRootPath(project);
		ok &= expect(!panel.busy() && starts == afterCancel, "entering Code must not restart a cancelled catalog");
		panel.refresh();
		panel.setRootPath(other);
		ok &= expect(waitFor([&]() { return !panel.busy(); }) && panel.result().rootPath == other && panel.result().files.size() == 1, "project switch discards in-flight old results");
		panel.refresh();
		panel.setRootPath({});
		auto* worker = static_cast<CodeFilesWorker*>(panel.findChild<QObject*>(QStringLiteral("codeFilesWorker")));
		ok &= expect(worker && waitFor([&]() { return !worker->busy(); }) && panel.result().files.isEmpty() && visibleFiles(tree) == 0, "closing a project cannot publish retired rows");
		panel.setRootPath(project);
		ok &= expect(waitFor([&]() { return !panel.busy(); }), "restore catalog for rendering");
		for (int scale : {100, 200}) {
			ExpandedLabels translator;
			if (scale == 200) { app.installTranslator(&translator); }
			applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
			panel.setLayoutDirection(scale == 100 ? Qt::LeftToRight : Qt::RightToLeft);
			panel.resize(scale == 100 ? 360 : 720, scale == 100 ? 560 : 850);
			panel.show();
			panel.refresh();
			ok &= expect(waitFor([&]() { return !panel.busy(); }), "rendered catalog refresh finishes");
			app.processEvents();
			auto* cancel = panel.findChild<QPushButton*>(QStringLiteral("codeFilesCancel"));
			auto* status = panel.findChild<QLabel*>(QStringLiteral("codeFilesStatus"));
			ok &= expect(cancel && status && !cancel->accessibleName().isEmpty() && cancel->focusPolicy() != Qt::NoFocus
				&& tree->height() > 250 && status->width() > 200, "scaled file panel keeps status and keyboard-focusable controls usable");
			QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			panel.render(&image);
			const QString output = qEnvironmentVariable("VIBESTUDIO_TEXT_TEST_EVIDENCE");
			if (!output.isEmpty()) { ok &= expect(image.save(QDir(output).filePath(QStringLiteral("code-files-%1.png").arg(scale))), "save file panel visual evidence"); }
			if (scale == 200) { app.removeTranslator(&translator); }
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{
		StudioSettings settings;
		settings.setCurrentProjectPath(project);
		settings.setCodeRecoveryEnabled(false);
		settings.sync();
	}
	QTimer modalGuard;
	QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() {
		if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
			std::cerr << "Unexpected modal: " << dialog->windowTitle().toStdString() << '\n';
			ok = false;
			dialog->reject();
		}
	});
	modalGuard.start(100);
	{
		ApplicationShell shell;
		auto* panel = shell.findChild<CodeFilesPanel*>(QStringLiteral("codeFilesPanel"));
		auto* tree = shell.findChild<QTreeWidget*>(QStringLiteral("codeTree"));
		auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("codeTreeFilter"));
		auto* refresh = shell.findChild<QToolButton*>(QStringLiteral("codeFilesRefresh"));
		auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
		auto* save = shell.findChild<QAction*>(QStringLiteral("code.save"));
		if (!panel || !tree || !filter || !refresh || !editor || !save) { return 1; }
		ok &= expect(waitFor([&]() { return !panel->busy(); }), "shell initializes its asynchronous catalog");
		const QString created = QDir(project).filePath(QStringLiteral("new.qh"));
		ok &= expect(write(created, "void() header = {};\n"), "create a new file after listing");
		shell.openPathFromCommandLine(created);
		ok &= expect(waitFor([&]() { return !panel->busy(); }) && tree->currentItem() && tree->currentItem()->data(0, Qt::UserRole).toString() == created, "opening a new Code file refreshes and selects its tree row");
		ok &= expect(studioLanguageForPath(created) == StudioLanguage::QuakeC, "catalog language matches the editor's QuakeC header language");
		editor->selectAll();
		editor->insertPlainText(QString(200, QLatin1Char('a')));
		save->trigger();
		filter->setText(QStringLiteral("name=new.qh size=200"));
		ok &= expect(waitFor([&]() { return !panel->busy(); }) && visibleFiles(tree) == 1, "Code save invalidates the shared file metadata snapshot");
		filter->clear();
		refresh->click();
		shell.openPathFromCommandLine(other);
		ok &= expect(waitFor([&]() { return !panel->busy(); }) && panel->result().rootPath == other && panel->result().files.size() == 1, "shell project changes cannot retain previous file rows");
	}
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
