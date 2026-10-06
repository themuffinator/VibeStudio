#include "app/application_shell.h"
#include "app/quick_open_catalog.h"
#include "app/studio_actions.h"
#include "app/studio_theme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
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
bool write(const QString& path, const QByteArray& contents = "int main() {}\n")
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
bool waitFor(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 20000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return ready();
}
class ExpandedLabels final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "vibestudio::QuickOpenDialog") { return {}; }
		const QString text = QString::fromUtf8(source);
		return text + QStringLiteral(" ~").repeated(text.size() / 6);
	}
};
} // namespace

int main(int argc, char** argv)
{
	// Exercise Qt APIs directly; no keyboard/mouse events or OS captures.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	bool ok = true;
	const QString root = temp.filePath(QStringLiteral("project"));
	const QString other = temp.filePath(QStringLiteral("other"));
	const QString main = QDir(root).filePath(QStringLiteral("src/main.cpp"));
	ok &= expect(write(main) && write(QDir(root).filePath(QStringLiteral("textures/wall.tga")))
		&& write(QDir(root).filePath(QStringLiteral(".agents/ignored.cfg"))) && write(QDir(other).filePath(QStringLiteral("other.qc"))), "create finder fixtures");
	ok &= expect(saveProjectManifest(defaultProjectManifest(root, QStringLiteral("Finder project")))
		&& saveProjectManifest(defaultProjectManifest(other, QStringLiteral("Other project"))), "create project context fixtures");
	QImage texture(4, 4, QImage::Format_RGB32);
	texture.fill(QColor(80, 140, 200));
	const QString imagePath = QDir(root).filePath(QStringLiteral("textures/preview.png"));
	ok &= expect(texture.save(imagePath), "write a real texture for cross-surface routing");
	QuickOpenRequest request;
	request.rootPath = root;
	request.recentPaths = {main, main, temp.filePath(QStringLiteral("missing.cfg"))};
	request.packageSource = temp.filePath(QStringLiteral("pak0.pak"));
	PackageEntry packageEntry;
	packageEntry.virtualPath = QStringLiteral("scripts/autoexec.cfg");
	request.packageEntries << packageEntry;
	packageEntry.virtualPath = QStringLiteral("textures/package.tga");
	request.packageEntries << packageEntry;
	QuickOpenCatalog worker;
	QuickOpenResult result;
	int completions = 0;
	worker.completed = [&](const auto& value) { result = value; ++completions; };
	worker.start(request);
	ok &= expect(worker.busy() && completions == 0, "discovery returns before inspecting filesystem or package metadata");
	ok &= expect(waitFor([&]() { return !worker.busy(); }) && result.state == OperationState::Completed, "discovery completes");
	int mainCount = 0;
	bool media = false, package = false, missing = false, ignored = false;
	for (const auto& entry : result.entries) {
		mainCount += entry.key == main;
		media |= entry.name == QStringLiteral("wall.tga");
		package |= entry.key == QStringLiteral("package:scripts/autoexec.cfg");
		missing |= entry.name == QStringLiteral("missing.cfg");
		ignored |= entry.name == QStringLiteral("ignored.cfg");
	}
	ok &= expect(mainCount == 1 && result.entries.first().key == main && media && package && !missing && !ignored, "recents lead and deduplicate with project files; assets and package entries share the picker");
	QuickOpenRequest replacement;
	replacement.rootPath = other;
	worker.start(request);
	worker.start(replacement);
	ok &= expect(waitFor([&]() { return !worker.busy(); }) && completions == 2 && result.entries.first().name == QStringLiteral("other.qc"), "replacement discards old workspace results");
	worker.start(request);
	worker.cancel();
	ok &= expect(waitFor([&]() { return !worker.busy(); }) && result.state == OperationState::Cancelled, "scan cancellation wins completion races");
	worker.start(request);
	worker.start(replacement);
	worker.cancel();
	ok &= expect(waitFor([&]() { return !worker.busy(); }) && result.state == OperationState::Cancelled, "queued replacement can be cancelled");
	request.maxProjectFiles = 1;
	request.maxPackageEntries = 1;
	worker.start(request);
	ok &= expect(waitFor([&]() { return !worker.busy(); }) && result.state == OperationState::Warning && result.warnings.size() >= 2, "project and archive bounds produce explicit partial results");
	request.maxProjectFiles = 20000;
	request.maxPackageEntries = 100000;
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		QuickOpenDialog picker;
		picker.resize(700, 530);
		picker.show();
		auto* filter = picker.findChild<QLineEdit*>(QStringLiteral("quickOpenFilter"));
		auto* list = picker.findChild<QListWidget*>(QStringLiteral("quickOpenList"));
		picker.startCatalog(request);
		ok &= expect(picker.catalogBusy() && list->count() == 2, "recent paths appear immediately while availability is checked");
		filter->setText(QStringLiteral("main"));
		ok &= expect(waitFor([&]() { return !picker.catalogBusy() && !picker.filtering(); }) && list->count() == 1, "discovery preserves a query typed while scanning");
		QVector<QuickOpenEntry> many;
		for (int i = 0; i < 30000; ++i) {
			const QString name = QStringLiteral("source-%1.cpp").arg(i, 5, 10, QLatin1Char('0'));
			many.push_back({name, name, QStringLiteral("src"), QStringLiteral("Project")});
		}
		filter->clear();
		picker.setEntries(many);
		ok &= expect(picker.filtering() && list->count() == 0, "large ranking yields with no stale activatable rows");
		int pulses = 0;
		QTimer pulse;
		QObject::connect(&pulse, &QTimer::timeout, &picker, [&]() { ++pulses; });
		pulse.start(0);
		ok &= expect(waitFor([&]() { return !picker.filtering(); }) && pulses > 2 && list->count() == 200, "ranking yields to other events and bounds published rows");
		pulse.stop();
		filter->setText(QStringLiteral("source-00001"));
		filter->setText(QStringLiteral("source-29999"));
		ok &= expect(waitFor([&]() { return !picker.filtering(); }) && list->count() == 1 && list->item(0)->text() == QStringLiteral("source-29999.cpp"), "new queries retire unfinished ranking work");
		filter->clear();
		picker.setEntries({{QStringLiteral("a"), QStringLiteral("alpha"), {}, {}}, {QStringLiteral("b"), QStringLiteral("beta"), {}, {}}});
		list->setCurrentRow(1);
		picker.setEntries({{QStringLiteral("a"), QStringLiteral("alpha"), {}, {}}, {QStringLiteral("b"), QStringLiteral("beta"), {}, {}}, {QStringLiteral("c"), QStringLiteral("gamma"), {}, {}}});
		ok &= expect(list->currentItem()->data(Qt::UserRole + 4).toString() == QStringLiteral("b"), "incoming results preserve the user's selected entry");
		picker.startCatalog(request);
		picker.cancelCatalog();
		ok &= expect(waitFor([&]() { return !picker.catalogBusy(); }) && picker.findChild<QLabel*>(QStringLiteral("quickOpenStatus"))->text().contains(QStringLiteral("cancelled")), "inline cancellation has a visible terminal state");
		picker.startCatalog(request);
		picker.reject();
		auto* catalog = static_cast<QuickOpenCatalog*>(picker.findChild<QObject*>(QStringLiteral("quickOpenCatalog")));
		ok &= expect(!picker.catalogBusy() && catalog && waitFor([&]() { return !catalog->busy(); }) && !picker.isVisible(), "closing retires scan work and never reopens the picker");
	}
	for (int scale : {100, 200}) {
		ExpandedLabels translator;
		if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		QuickOpenDialog picker;
		picker.setLayoutDirection(scale == 100 ? Qt::LeftToRight : Qt::RightToLeft);
		picker.resize(scale == 100 ? 700 : 1100, scale == 100 ? 520 : 850);
		picker.show();
		picker.startCatalog(request);
		ok &= expect(waitFor([&]() { return !picker.catalogBusy() && !picker.filtering(); }), "render fixture discovery completes");
		app.processEvents();
		auto* refresh = picker.findChild<QPushButton*>(QStringLiteral("quickOpenRefresh"));
		auto* list = picker.findChild<QListWidget*>(QStringLiteral("quickOpenList"));
		ok &= expect(refresh && !refresh->accessibleName().isEmpty() && refresh->focusPolicy() != Qt::NoFocus && list->height() > 200, "scaled picker retains accessible standard controls and useful result space");
		QImage image(picker.size(), QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		picker.render(&image);
		const QString output = qEnvironmentVariable("VIBESTUDIO_TEXT_TEST_EVIDENCE");
		if (!output.isEmpty()) { ok &= expect(image.save(QDir(output).filePath(QStringLiteral("quick-open-%1.png").arg(scale))), "save picker render evidence"); }
		picker.reject();
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	{
		StudioSettings settings;
		settings.setCurrentProjectPath(root);
		settings.setCodeRecoveryEnabled(false);
		settings.sync();
	}
	QTimer modalGuard;
	QObject::connect(&modalGuard, &QTimer::timeout, &app, [&]() {
		if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); dialog && dialog->objectName() != QStringLiteral("quickOpen")
			&& dialog->objectName() != QStringLiteral("packageLoadDialog") && dialog->objectName() != QStringLiteral("packagePaletteDialog")) {
			std::cerr << "Unexpected modal: " << dialog->windowTitle().toStdString() << '\n';
			ok = false;
			dialog->reject();
		}
	});
	modalGuard.start(100);
	{
		ApplicationShell shell;
		QuickOpenDialog* picker = shell.findChild<QuickOpenDialog*>(QStringLiteral("quickOpen"));
		QAction* action = shell.findChild<QAction*>(QStringLiteral("shell.goToFile"));
		if (!action) {
			for (auto* candidate : shell.findChildren<QAction*>()) {
				if (candidate->property("commandId").toString() == QStringLiteral("shell.goToFile")) { action = candidate; break; }
			}
		}
		ok &= expect(picker && action, "shell wires its real Go to File command");
		if (picker && action) {
			action->trigger();
			auto* filter = picker->findChild<QLineEdit*>(QStringLiteral("quickOpenFilter"));
			auto* list = picker->findChild<QListWidget*>(QStringLiteral("quickOpenList"));
			filter->setText(QStringLiteral("main.cpp"));
			ok &= expect(waitFor([&]() { return !picker->catalogBusy() && !picker->filtering(); }) && list->count() == 1, "real shell discovers C++ sources");
			if (list->count() == 1) { list->itemActivated(list->item(0)); }
			auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
			ok &= expect(editor && editor->toPlainText().contains(QStringLiteral("int main")) && !editor->isReadOnly() && !picker->isVisible(), "selection opens a discovered source through the editable Code document service");
			action->trigger();
			filter->setText(QStringLiteral("preview.png"));
			ok &= expect(waitFor([&]() { return !picker->catalogBusy() && !picker->filtering(); }) && list->count() == 1, "loose media is discoverable alongside sources");
			if (list->count() == 1) { list->itemActivated(list->item(0)); }
			auto* textures = shell.findChild<QListWidget*>(QStringLiteral("textureEntries"));
			ok &= expect(textures && textures->currentItem() && textures->currentItem()->data(Qt::UserRole).toString() == QStringLiteral("preview.png"), "loose texture selection uses the existing Textures surface and folder package");
			action->trigger();
			filter->setText(QStringLiteral("preview.png"));
			ok &= expect(waitFor([&]() { return !picker->catalogBusy() && !picker->filtering(); }), "package metadata joins the refreshed picker");
			QListWidgetItem* packaged = nullptr;
			for (int row = 0; row < list->count(); ++row) {
				if (list->item(row)->data(Qt::UserRole + 4).toString() == QStringLiteral("package:preview.png")) { packaged = list->item(row); break; }
			}
			ok &= expect(packaged, "open package entries retain their distinct navigation keys");
			if (packaged) { list->setCurrentItem(packaged); list->itemActivated(packaged); }
			ok &= expect(!picker->isVisible() && textures && textures->currentItem() && textures->currentItem()->data(Qt::UserRole).toString() == QStringLiteral("preview.png"), "package texture selection uses the same surface routing");
			action->trigger();
			shell.openPathFromCommandLine(other);
			ok &= expect(!picker->isVisible() && !picker->catalogBusy(), "project changes close and retire the old picker");
			action->trigger();
			filter->setText(QStringLiteral("other.qc"));
			ok &= expect(waitFor([&]() { return !picker->catalogBusy() && !picker->filtering(); }) && list->count() == 1, "reopening uses the new project snapshot");
			picker->reject();
		}
	}
	modalGuard.stop();
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
