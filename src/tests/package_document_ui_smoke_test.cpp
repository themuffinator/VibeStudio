#include "package_entry_test_helpers.h"
#include "app/application_shell.h"
#include "app/package_folder_view.h"
#include "app/studio_theme.h"
#include "app/studio_charts.h"
#include "core/package_draft.h"
#include "package_legacy_test_helpers.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QPersistentModelIndex>
#include <QFont>
#include <QImage>
#include <QInputDialog>
#include <QLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>

#include <iostream>
#include <memory>
#include <utility>

using namespace vibestudio;
namespace {
struct DocumentStep {
	QString label;
	QElapsedTimer elapsed;
	bool trace = qEnvironmentVariableIsSet("VIBESTUDIO_TEST_PACKAGE_DOCUMENT_TRACE");
	explicit DocumentStep(QString name) : label(std::move(name)) {
		elapsed.start();
		if (trace) { std::cerr << "Begin " << label.toStdString() << '\n'; }
	}
	~DocumentStep() { if (trace) { std::cerr << "End " << label.toStdString() << ": " << elapsed.elapsed() << " ms\n"; } }
};
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool waitForPreview(QPlainTextEdit* preview, const QString& text)
{
	QElapsedTimer timer; timer.start();
	while (!preview->toPlainText().contains(text) && timer.elapsed() < 10000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return preview->toPlainText().contains(text);
}
class ExpandedTranslator final : public QTranslator {
public:
	bool expanded = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		const QByteArray text(source);
		if (expanded && QByteArray(context) == "VibeStudioPackageArchive" && text.startsWith("Package entry paths exceed")) {
			return QStringLiteral("[%1 — expanded admission explanation]").arg(QString::fromUtf8(source));
		}
		return expanded && QByteArray(context).endsWith("ApplicationShell")
			&& (text == "Package format:" || text == "Virtual folder path:" || text == "New virtual package path:" || text == "Package view unavailable")
			? QStringLiteral("[%1 — expanded field description]").arg(QString::fromUtf8(source)) : QString();
	}
};
}

int main(int argc, char** argv)
{
	// Exercise direct Qt properties and actions, never native input or capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	QDir root(temporary.path()); QString error; bool ok = true;
	QElapsedTimer lifecycleTime; lifecycleTime.start();
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("settings.ini")));
	// This owned file is only listed in Levels; no map or engine is opened.
	const auto recentMapPath = root.filePath(QStringLiteral("recent.map"));
	{ QFile file(recentMapPath); if (!file.open(QIODevice::WriteOnly) || file.write("// recent-map fixture\n") < 0) { return 1; } }
	{ StudioSettings settings; settings.recordRecentFile(QStringLiteral("map"), recentMapPath); settings.sync(); }
	PackageStagingModel fixture;
	const QString fixturePath = root.filePath(QStringLiteral("fixture.vibepackage"));
	{
		DocumentStep step("prepare two-entry fixture");
		fixture.createEmpty(PackageArchiveFormat::Pk3);
		fixture.addBytes("payload", QStringLiteral("source/deep/file.txt")); fixture.addBytes("sibling", QStringLiteral("other/keep.txt"));
		fixture.createDirectory(QStringLiteral("source/empty"));
		if (!PackageDraft::save(fixturePath, &fixture, false, &error)) { std::cerr << error.toStdString(); return 1; }
	}
	const auto brokenPackagePath = root.filePath(QStringLiteral("damaged.pk3"));
	{ QFile file(brokenPackagePath); if (!file.open(QIODevice::WriteOnly) || file.write("not an archive") < 0) { return 1; } }
	const QString deepFixture = root.filePath(QStringLiteral("snapshot-depth.vibepackage"));
	{
		DocumentStep step("prepare legacy-depth fixture");
		if (!tests::saveLegacyDeepPackageDraft(deepFixture, &error)) { std::cerr << error.toStdString(); return 1; }
	}
	ExpandedTranslator translator;
	{ DocumentStep step("install translator"); app.installTranslator(&translator); }
	{
		auto owner = [] { DocumentStep step("construct shell"); return std::make_unique<ApplicationShell>(); }();
		auto& shell = *owner;
		{ DocumentStep step("show shell"); shell.resize(1600, 1000); shell.show(); app.processEvents(); }
		const auto action = [&](const char* name) { return shell.findChild<QAction*>(QString::fromLatin1(name)); };
		const auto trigger = [&](QAction* target) { DocumentStep step("action " + target->objectName()); target->trigger(); };
		auto* create = action("package.new"); auto* folder = action("package.createDirectory"); auto* save = action("package.saveDraft");
		auto* rename = action("package.stageRename"); auto* remove = action("package.stageDelete"); auto* undo = action("package.unstageLast");
		auto* close = action("package.close");
		auto entries = tests::PackageRows(shell.findChild<PackageEntryView*>(QStringLiteral("packageEntries")));
		auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("packageFilter"));
		auto* tree = shell.findChild<PackageFolderView*>(QStringLiteral("packageTree"));
		auto* preview = shell.findChild<QPlainTextEdit*>(QStringLiteral("packageTextPreview"));
		auto* composition = shell.findChild<QListWidget*>(QStringLiteral("packageComposition"));
		QLabel* packageSubtitle = nullptr;
		for (auto* header : shell.findChildren<QWidget*>(QStringLiteral("pageHeader"))) {
			const auto* title = header->findChild<QLabel*>(QStringLiteral("pageTitle"));
			if (title && title->text() == QStringLiteral("Packages")) { packageSubtitle = header->findChild<QLabel*>(QStringLiteral("pageSubtitle")); }
		}
		auto* compositionChart = composition ? composition->parentWidget()->findChild<CompositionChart*>() : nullptr;
		if (!expect(composition && packageSubtitle && compositionChart, "package status and composition controls are registered")) { return 1; }
		if (!expect(create && folder && save && rename && remove && undo && close && entries && filter && tree && preview, "document controls are registered")) { return 1; }
		const auto checkOpenCommands = [&](bool enabled) {
			for (const char* id : {"package.new", "package.open", "package.openFolder", "package.openDraft", "package.recover"}) {
				const auto* command = action(id);
				if (!command || command->isEnabled() != enabled) { std::cerr << "Package open-command state: " << id << " expected " << enabled << '\n'; ok = false; }
			}
		};
		checkOpenCommands(true);
		const auto saveTo = [&](const QString& path) {
			DocumentStep step("save draft " + QFileInfo(path).fileName());
			bool visited = false;
			QTimer::singleShot(0, [&]() {
				if (auto* picker = qobject_cast<QFileDialog*>(app.activeModalWidget())) { visited = true; picker->selectFile(path); QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection); }
			});
			trigger(action("package.saveDraftAs"));
			ok &= expect(visited && QFileInfo::exists(QDir(path).filePath(QStringLiteral("document.json"))), "GUI saves a complete package draft");
			checkOpenCommands(true);
		};
		const auto select = [&](const QString& path) {
			DocumentStep step("select " + path);
			filter->setText(path); entries->clearSelection();
			for (int row = 0; row < entries->count(); ++row) {
				if (entries->item(row)->data(Qt::UserRole).toString() == path) { entries->setCurrentItem(entries->item(row)); return true; }
			}
			return false;
		};
		for (const int scale : {100, 200}) {
			std::cerr << "Document scale " << scale << " at " << lifecycleTime.elapsed() << " ms\n";
			translator.expanded = scale == 200;
			{
				DocumentStep step("theme " + QString::number(scale));
				applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
				app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
				shell.setLayoutDirection(app.layoutDirection());
			}
			const auto render = [&](QWidget* widget, const QString& name) {
				DocumentStep step("render " + name);
				widget->ensurePolished(); widget->layout()->activate(); app.processEvents();
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
				if (captures.isEmpty()) { return true; }
				QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); widget->render(&image);
				return image.save(QDir(captures).filePath(QStringLiteral("package-%1-%2.png").arg(name).arg(scale)));
			};
			bool visited = false;
			QTimer::singleShot(0, [&]() {
				if (auto* dialog = qobject_cast<QInputDialog*>(app.activeModalWidget())) {
					visited = true; auto* combo = dialog->findChild<QComboBox*>();
					ok &= expect(dialog->layoutDirection() == app.layoutDirection(), "format dialog follows the active layout direction");
					ok &= expect(combo && combo->count() == 7 && combo->focusPolicy() != Qt::NoFocus && !dialog->labelText().isEmpty(), "new document format selector is labeled and focusable");
					ok &= expect(render(dialog, "new-document"), "render new document format dialog");
					dialog->accept();
				}
			});
			trigger(create);
			ok &= expect(visited && folder->isEnabled() && save->isEnabled() && action("package.saveAs")->isEnabled() && !undo->isEnabled(), "empty new document can save and create folders without a source");
			bool dirtyPrompt = false;
			QTimer::singleShot(0, [&]() {
				if (auto* box = qobject_cast<QMessageBox*>(app.activeModalWidget())) { dirtyPrompt = true; box->done(QMessageBox::Cancel); }
			});
			trigger(close);
			ok &= expect(dirtyPrompt && save->isEnabled(), "closing even an empty unsaved package prompts and Cancel keeps it");
			const QString emptyDraft = root.filePath(QStringLiteral("empty-%1.vibepackage").arg(scale)); saveTo(emptyDraft);
			PackageStagingModel restored;
			ok &= expect(PackageDraft::load(emptyDraft, &restored, &error) && restored.sourcePath().isEmpty() && restored.plannedEntries().isEmpty(), "GUI-created empty draft has no synthetic filesystem source");
			std::cerr << "Empty draft saved at " << lifecycleTime.elapsed() << " ms\n";
			{ DocumentStep step("failed package open"); shell.openPathFromCommandLine(brokenPackagePath); }
			checkOpenCommands(true);
			ok &= expect(save->isEnabled() && !undo->isEnabled(), "failed replacement keeps the saved empty document and restores its commands");
			shell.openPathFromCommandLine(fixturePath);
			ok &= expect(entries->count() > 0, "wait for the shared browser metadata before checking composition");
			ok &= expect(packageSubtitle->accessibleDescription().contains(QStringLiteral("6 entries")) && !compositionChart->slices().isEmpty(),
				"package header and composition report the planned files and folders of a draft with an empty base");
			auto* renameFolder = action("package.renameFolder"); auto* deleteFolder = action("package.deleteFolder");
			ok &= expect(renameFolder && deleteFolder && !renameFolder->isEnabled() && !deleteFolder->isEnabled(), "package root is not a rename/delete target");
			tree->selectFolder(QStringLiteral("source"));
			ok &= expect(static_cast<PackageEntryView*>(entries)->busy() && renameFolder && renameFolder->isEnabled(), "folder rename is available before its asynchronous entry list finishes");
			QTimer::singleShot(0, [&]() { if (auto* dialog = qobject_cast<QInputDialog*>(app.activeModalWidget())) { dialog->setTextValue("tree-moved"); dialog->accept(); } });
			if (renameFolder) { trigger(renameFolder); }
			ok &= expect(select("tree-moved/deep/file.txt") && waitForPreview(preview, "payload"), "tree rename operates on the folder identity while the entry list is pending");
			trigger(undo);
			ok &= expect(select("source/deep/file.txt"), "undo restores a folder renamed from the tree");
			checkOpenCommands(true);
			auto* recentMaps = shell.findChild<QListWidget*>(QStringLiteral("levelsRecentMaps"));
			if (!expect(recentMaps && recentMaps->count() == 1, "owned recent-map context is visible in Levels")) { return 1; }
			recentMaps->setCurrentRow(0);
			const QPersistentModelIndex recentIndex = recentMaps->currentIndex();
			entries->clearSelection(); entries->selectAll();
			ok &= expect(recentIndex.isValid() && recentMaps->currentIndex() == recentIndex,
				"package multi-selection preserves the recent-map selection in the Levels surface");
			tree->selectFolder(QStringLiteral("other")); tree->selectFolder(QStringLiteral("source"));
			ok &= expect(static_cast<PackageEntryView*>(entries)->busy() && deleteFolder && deleteFolder->isEnabled(), "folder delete is available before its asynchronous entry list finishes");
			if (deleteFolder) { trigger(deleteFolder); }
			ok &= expect(!select("source/deep/file.txt") && select("other/keep.txt"), "tree delete targets its subtree and preserves the sibling during pending navigation");
			trigger(undo);
			ok &= expect(select("source/deep/file.txt") && select("source/empty"), "one undo restores files and empty folders removed from the tree");
			ok &= expect(select("source"), "select a folder row");
			ok &= expect(!action("package.stageReplace")->isEnabled() && rename->isEnabled() && remove->isEnabled(), "folder rows expose folder-compatible actions");
			QTimer::singleShot(0, [&]() { if (auto* dialog = qobject_cast<QInputDialog*>(app.activeModalWidget())) { dialog->setTextValue("moved"); dialog->accept(); } });
			trigger(rename);
			ok &= expect(select("moved/deep/file.txt") && waitForPreview(preview, "payload"), "renamed folder previews the same planned bytes asynchronously");
			trigger(undo);
			ok &= expect(select("source/deep/file.txt") && waitForPreview(preview, "payload"), "one undo restores the complete folder path");
			ok &= expect(select("source"), "reselect source folder");
			QTimer::singleShot(0, [&]() { if (auto* dialog = qobject_cast<QInputDialog*>(app.activeModalWidget())) { dialog->setTextValue("other"); dialog->accept(); } });
			trigger(rename);
			ok &= expect(select("source/deep/file.txt") && waitForPreview(preview, "payload"), "occupied folder rename leaves all bytes and paths unchanged");
			bool folderDialog = false;
			QTimer::singleShot(0, [&]() {
				if (auto* dialog = qobject_cast<QInputDialog*>(app.activeModalWidget())) {
					folderDialog = true; dialog->setTextValue("source/new-empty");
					ok &= expect(dialog->layoutDirection() == app.layoutDirection(), "folder dialog follows the active layout direction");
					auto* text = dialog->findChild<QLineEdit*>(); ok &= expect(text && text->focusPolicy() != Qt::NoFocus && !dialog->labelText().isEmpty(), "folder editor is labeled and focusable");
					ok &= expect(render(dialog, "new-folder"), "render folder dialog"); dialog->accept();
				}
			});
			trigger(folder);
			ok &= expect(folderDialog && select("source/new-empty"), "new empty folder appears immediately");
			bool editCancelled = false;
			QTimer::singleShot(0, [&]() {
				if (auto* picker = qobject_cast<QInputDialog*>(app.activeModalWidget())) {
					picker->setTextValue("cancelled-folder"); picker->accept();
					QTimer::singleShot(0, [&]() {
						auto* worker = app.activeModalWidget();
						if (!worker || worker->objectName() != QStringLiteral("packageEditDialog")) { return; }
						auto* cancel = worker->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
						ok &= expect(!undo->isEnabled() && !remove->isEnabled() && !renameFolder->isEnabled() && !deleteFolder->isEnabled(), "Package entry and folder edit actions are disabled during worker preparation.");
						checkOpenCommands(false);
						for (const char* name : {"packageNew", "packageValidate", "packageSaveAsButton"}) {
							const auto* button = shell.findChild<QAbstractButton*>(QString::fromLatin1(name));
							ok &= expect(button && !button->isEnabled(), "visible package toolbar controls follow the same busy state as their commands");
						}
						int enabledWhileBusy = 0;
						QVector<QMetaObject::Connection> observers;
						for (const char* id : {"package.close", "package.saveAs", "package.saveDraft", "package.createDirectory", "package.extractAll", "package.stageAdd"}) {
							auto* command = action(id);
							if (!expect(command && !command->isEnabled(), "package work starts with conflicting commands disabled")) { ok = false; continue; }
							observers << QObject::connect(command, &QAction::changed, &shell, [&, command] { if (command->isEnabled()) { ++enabledWhileBusy; } });
						}
						// A synchronous model reset used to re-enable these commands and
						// disable them again inside a whole-studio refresh.
						filter->setText(QStringLiteral("keep"));
						for (const auto& observer : observers) { QObject::disconnect(observer); }
						ok &= expect(enabledWhileBusy == 0, "selection refresh never transiently enables commands during package work");
						if (cancel) { editCancelled = true; cancel->click(); }
					});
				}
			});
			trigger(folder);
			ok &= expect(editCancelled && !select("cancelled-folder") && select("source/new-empty") && undo->isEnabled(),
				"Cancelling a shell edit keeps its current rows and restores command availability.");
			checkOpenCommands(true);
			filter->setText("source"); entries->clearSelection();
			for (int row = 0; row < entries->count(); ++row) {
				const auto path = entries->item(row)->data(Qt::UserRole).toString();
				if (path == "source" || path == "source/deep/file.txt") { entries->item(row)->setSelected(true); }
			}
			trigger(remove);
			ok &= expect(!select("source/deep/file.txt") && select("other/keep.txt"), "folder plus child selection deletes the subtree once and preserves siblings");
			trigger(undo);
			ok &= expect(select("source/new-empty") && select("source/deep/file.txt"), "one undo restores both empty and populated folders");
			const QString edited = root.filePath(QStringLiteral("edited-%1.vibepackage").arg(scale)); saveTo(edited);
			ok &= expect(PackageDraft::load(edited, &restored, &error) && restored.canRedo() && restored.redo() && restored.summary().stagedFileCount == 1, "GUI folder deletion redo persists in the saved draft");
			std::cerr << "Folder lifecycle saved at " << lifecycleTime.elapsed() << " ms\n";
			trigger(close);
			ok &= expect(!save->isEnabled(), "saved document closes cleanly");
			checkOpenCommands(true);
			shell.openPathFromCommandLine(deepFixture); app.processEvents();
			ok &= expect(entries->count() == 1 && entries->item(0)->toolTip().contains("depth")
				&& entries->item(0)->data(Qt::AccessibleDescriptionRole).toString().contains("depth") && undo->isEnabled(),
				"GUI view refusal exposes an accessible limit reason while retaining document history");
			ok &= expect(packageSubtitle->accessibleDescription().contains(QStringLiteral("Package view unavailable"))
				&& composition->count() == 1 && composition->item(0)->text().contains(QStringLiteral("depth"))
				&& composition->item(0)->data(Qt::AccessibleDescriptionRole).toString().contains(QStringLiteral("depth"))
				&& compositionChart->slices().isEmpty() && compositionChart->accessibleSummary().contains(QStringLiteral("depth")),
				"refused snapshots never report zero-entry success or retain a previous composition chart");
			int unavailableAssetViews = 0;
			for (auto* empty : shell.findChildren<QWidget*>(QStringLiteral("emptyState"))) {
				if (empty->accessibleName().contains(QStringLiteral("Package view unavailable"))
					&& empty->accessibleDescription().contains(QStringLiteral("depth"))) { ++unavailableAssetViews; }
			}
			ok &= expect(unavailableAssetViews == 3, "Textures, Models and Audio share the package admission reason");
			ok &= expect(render(&shell, "snapshot-refusal"), "render the snapshot admission diagnostic");
			trigger(undo); app.processEvents();
			auto* redo = action("package.redo");
			ok &= expect(redo && redo->isEnabled() && (entries->count() == 0 || !entries->item(0)->toolTip().contains("depth")),
				"GUI Undo restores the package view without erasing the failed edit's redo");
			if (redo) { trigger(redo); }
			ok &= expect(entries->count() == 1 && entries->item(0)->toolTip().contains("depth") && undo->isEnabled(),
				"GUI Redo returns the truthful diagnostic for the same preserved edit");
			trigger(close);
		}
		std::cerr << "Legacy views checked at " << lifecycleTime.elapsed() << " ms\n";
		QTimer::singleShot(0, [&]() { if (auto* dialog = qobject_cast<QInputDialog*>(app.activeModalWidget())) { dialog->findChild<QComboBox*>()->setCurrentIndex(6); dialog->accept(); } });
		trigger(create);
		ok &= expect(!folder->isEnabled(), "flat WAD documents disable folder creation");
		const QString wad = root.filePath(QStringLiteral("texture.vibepackage")); saveTo(wad);
		PackageStagingModel restored; ok &= expect(PackageDraft::load(wad, &restored, &error) && restored.sourceWadMagic() == QStringLiteral("WAD3"), "GUI-selected WAD variant survives draft save");
		trigger(close);
	}
	app.removeTranslator(&translator);
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
