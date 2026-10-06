#include "package_entry_test_helpers.h"
#include <QStandardItemModel>
#include "package_copy_test_helpers.h"
#include "core/package_copy_store.h"
#include "app/application_shell.h"
#include "app/package_operation_dialog.h"
#include "app/studio_theme.h"
#include "app/studio_actions.h"
#include "core/package_draft.h"
#include "core/package_import_store.h"

#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFileDialog>
#include <QProcess>
#include <filesystem>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QStatusBar>
#include <QSpinBox>
#include <QThreadPool>
#include <QTextDocument>
#include <QTimer>
#include <QTranslator>

using namespace vibestudio;
using namespace package_copy_test;
namespace {
class ExpandedTranslator final : public QTranslator {
public:
	bool expanded = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		return expanded && QByteArray(context) == "VibeStudioPackageDialog"
			? QStringLiteral("[%1 — expanded label]").arg(QString::fromUtf8(source)) : QString();
	}
};
}

int main(int argc, char** argv)
{
	// Direct Qt properties, model MIME requests and QWidget::render only.
	// No native input, actual OS drag, screen capture or game launch.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const auto cleanup = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup(); waitForPackageCopyCleanup(); });
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	ExpandedTranslator translator; app.installTranslator(&translator); bool ok = true;
	auto reader = std::make_shared<Reader>(); reader->add(QStringLiteral("a.txt"), "already copied");
	reader->add(QStringLiteral("z.bin"), QByteArray(1024 * 1024, 'z'));
	PackageCopyRequest request; request.parentDirectory = QDir(temporary.path()).canonicalPath(); request.entryIndexes = {0, 1};
	for (const int scale : {100, 200}) {
		StudioSettings settings; auto preferences = settings.accessibilityPreferences();
		preferences.textScalePercent = scale; preferences.reducedMotion = scale == 200;
		preferences.theme = scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark;
		settings.setAccessibilityPreferences(preferences); settings.sync();
		applyStudioTheme(app, studioThemeTokens(preferences.theme, preferences.density, scale));
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); translator.expanded = scale == 200;
		std::atomic_bool reached = false, release = false;
		request.control.progress = [&](const QString& path, qint64 bytes, qint64) {
			if (path != QStringLiteral("z.bin") || bytes != 65536) { return; }
			reached = true; QElapsedTimer timeout; timeout.start();
			while (!release && timeout.elapsed() < 10000) { QThread::msleep(2); }
		};
		int ticks = 0; bool inspected = false; QTimer poll;
		QObject::connect(&poll, &QTimer::timeout, [&] {
			if (!reached || ++ticks < 10 || inspected) { return; }
			auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
			if (!dialog) { return; }
			inspected = true;
			auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation"));
			auto* progress = dialog->findChild<QProgressBar*>();
			auto* summary = dialog->findChild<QLabel*>(QStringLiteral("packageOperationSummary"));
			ok &= expect(dialog->objectName() == QStringLiteral("packageCopyDialog") && cancel && cancel->isEnabled()
				&& !cancel->accessibleName().isEmpty() && cancel->focusPolicy() != Qt::NoFocus
				&& progress && !progress->accessibleName().isEmpty() && progress->maximum() > 0
				&& progress->format().contains(QChar(0x2066)) && progress->format().contains(QChar(0x2069))
				&& summary && !summary->accessibleName().isEmpty(), "copy worker exposes accessible determinate progress and cancellation");
			dialog->ensurePolished(); dialog->layout()->activate();
			ok &= expect(cancel && dialog->rect().contains(QRect(cancel->mapTo(dialog, QPoint()), cancel->size()))
				&& cancel->width() >= cancel->sizeHint().width(), "expanded cancel action fits at each scale");
			if (scale == 200) {
				ok &= expect(dialog->fontMetrics().height() >= 24 && dialog->palette().color(QPalette::Window).lightness() > 128
					&& dialog->layoutDirection() == Qt::RightToLeft, "copy fixture actually uses large high-contrast RTL text");
			}
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
				ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-copy-%1.png").arg(scale))), "record package copy progress");
			}
			if (scale == 200) { dialog->close(); } else if (cancel) { cancel->click(); }
			release = true;
		});
		poll.start(10);
		const auto cancelled = runPackageCopyDialog(nullptr, reader, request); poll.stop();
		ok &= expect(inspected && ticks >= 10 && cancelled.cancelled && !cancelled.storage && cancelled.paths.isEmpty()
			&& cancelled.extraction.writtenCount == 1 && !reader->readOnUi && reader->buffered == 0
			&& QDir(temporary.path()).entryList({QStringLiteral("package-copy-*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
			"Cancel and window close keep the UI running, stop streamed reads and discard all temporary output");
	}
	request.control = {}; translator.expanded = false;
	const auto completed = runPackageCopyDialog(nullptr, reader, request);
	ok &= expect(completed.succeeded() && read(completed.paths.first()) == "already copied" && !reader->readOnUi, "successful copy ownership outlives its worker dialog");
	reader->failAtEnd = 1;
	bool failureShown = false; QTimer failurePoll;
	QObject::connect(&failurePoll, &QTimer::timeout, [&] {
		auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
		if (!dialog || dialog->property("operationRunning").toBool()) { return; }
		auto* details = dialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"));
		failureShown = details && details->toPlainText().contains(QStringLiteral("integrity"));
		dialog->close();
	});
	failurePoll.start(10);
	const auto failed = runPackageCopyDialog(nullptr, reader, request); failurePoll.stop();
	ok &= expect(failureShown && !failed.succeeded() && !failed.storage && failed.paths.isEmpty(), "copy failure retains inspectable diagnostics until Close");
	reader->failAtEnd = -1;
	StudioSettings settings; auto preferences = settings.accessibilityPreferences(); preferences.theme = StudioTheme::Dark;
	preferences.textScalePercent = 100; preferences.reducedMotion = false; settings.setAccessibilityPreferences(preferences); settings.sync();
	app.setLayoutDirection(Qt::LeftToRight);
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); plan.addBytes("planned script", QStringLiteral("scripts/test.cfg"));
	plan.createDirectory(QStringLiteral("empty"));
	LevelMapCreateRequest mapRequest; mapRequest.starterRoom = false;
	LevelMapDocument mapDocument; QString mapError;
	if (!expect(createLevelMap(mapRequest, &mapDocument, &mapError), "create synthetic package map")) { return 1; }
	const auto mapBytes = serializeLevelMap(mapDocument).bytes;
	plan.addBytes(mapBytes, QStringLiteral("maps/fixture.map"));
	const QString project = temporary.filePath(QStringLiteral("project"));
	if (!expect(QDir().mkpath(project), "create durable project destination")) { return 1; }
	settings.setCurrentProjectPath(project); settings.sync();
	const QString fixture = temporary.filePath(QStringLiteral("fixture.vibepackage")); QString error;
	if (!expect(PackageDraft::save(fixture, &plan, false, &error), "save generated copy fixture")) { return 1; }
	{
		ApplicationShell shell; shell.show(); shell.openPathFromCommandLine(fixture); app.processEvents();
		auto entries = tests::PackageRows(shell.findChild<PackageEntryView*>(QStringLiteral("packageEntries")));
		auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("packageFilter"));
		if (!expect(entries && filter, "shell package browser exists")) { return 1; }
		const auto mimeFor = [&](const QString& path) {
			filter->clear();
			QModelIndexList indexes;
			for (int row = 0; row < entries->count(); ++row) {
				if (entries->item(row)->data(Qt::UserRole).toString() == path) { indexes << entries->model()->index(row, 0); }
			}
			return std::unique_ptr<QMimeData>(indexes.isEmpty() ? nullptr : entries->model()->mimeData(indexes));
		};
		const auto first = mimeFor(QStringLiteral("scripts")); const auto second = mimeFor(QStringLiteral("scripts"));
		ok &= expect(first && second && first->urls().size() == 1 && second->urls().size() == 1
			&& first->urls() != second->urls() && read(QDir(first->urls().first().toLocalFile()).filePath(QStringLiteral("test.cfg"))) == "planned script",
			"real drag MIME preparation returns distinct owned copies of staged folder content");
		const auto empty = mimeFor(QStringLiteral("empty"));
		ok &= expect(empty && empty->urls().size() == 1 && QFileInfo(empty->urls().first().toLocalFile()).isDir(), "empty planned folder is draggable");
		auto* storageAction = shell.findChild<QAction*>(QStringLiteral("package.copy-storage"));
		if (storageAction) { storageAction->trigger(); app.processEvents(); }
		auto* storageDialog = shell.findChild<QDialog*>(QStringLiteral("packageCopyBudgetDialog"));
		if (storageDialog) {
			auto* batches = storageDialog->findChild<QSpinBox*>(QStringLiteral("packageCopyMaximumBatches"));
			auto* apply = storageDialog->findChild<QPushButton*>(QStringLiteral("applyPackageCopyLimits"));
			if (batches && apply) { batches->setValue(3); apply->click(); }
			storageDialog->close();
		}
		ok &= expect(storageAction && storageDialog && settings.packageCopyLimits().maximumBatches == 3, "shell exposes and saves the shared session-copy limit");
		bool quotaShown = false; QTimer quotaPoll;
		QObject::connect(&quotaPoll, &QTimer::timeout, [&] {
			auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
			if (!dialog || dialog->property("operationRunning").toBool()) { return; }
			auto* details = dialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"));
			quotaShown = details && details->toPlainText().contains(QStringLiteral("session")); dialog->close();
		});
		quotaPoll.start(10); const auto refused = mimeFor(QStringLiteral("scripts")); quotaPoll.stop();
		ok &= expect(quotaShown && (!refused || refused->urls().isEmpty())
			&& read(QDir(first->urls().first().toLocalFile()).filePath(QStringLiteral("test.cfg"))) == "planned script",
			"a full window budget refuses further MIME handoffs and preserves earlier copies");
		settings.setPackageCopyLimits({}); settings.sync();
		filter->setText(QStringLiteral("test.cfg"));
		if (entries->count() == 1) { entries->setCurrentRow(0); entries->itemActivated(entries->item(0)); }
		auto* editor = shell.findChild<QPlainTextEdit*>(QStringLiteral("codeEditor"));
		ok &= expect(editor && editor->toPlainText() == QStringLiteral("planned script") && editor->isReadOnly(), "script handoff opens the verified planned copy read-only");
		const QByteArray packageBeforeCodeSave = read(fixture);
		const QString copiedScript = QDir(first->urls().first().toLocalFile()).filePath(QStringLiteral("test.cfg"));
		const QString rejectedScript = QFileInfo(copiedScript).dir().filePath(QStringLiteral("rejected.cfg"));
		ok &= expect(!shell.saveCodeDocumentAs(inspectTextWriteTarget(copiedScript), &error) && error.contains(QStringLiteral("permanent folder"))
			&& !shell.saveCodeDocumentAs(inspectTextWriteTarget(rejectedScript), &error) && error.contains(QStringLiteral("permanent folder"))
			&& !QFileInfo::exists(rejectedScript) && editor->isReadOnly() && read(copiedScript) == "planned script",
			"Code Save As refuses existing and new disposable destinations without adopting or changing copies");
		const QString savedScript = QDir(project).filePath(QStringLiteral("script-edited.cfg"));
		ok &= expect(shell.saveCodeDocumentAs(inspectTextWriteTarget(savedScript), &error)
			&& read(savedScript) == "planned script" && !editor->isReadOnly() && read(fixture) == packageBeforeCodeSave,
			"Code Save As adopts an editable independent file and leaves the package intact");
		const QByteArray packageBeforeMapSave = read(fixture);
		filter->setText(QStringLiteral("fixture.map"));
		if (entries->count() == 1) { entries->setCurrentRow(0); entries->itemActivated(entries->item(0)); }
		const QString copiedMap = shell.levelDocument().sourcePath;
		if (!expect(!copiedMap.isEmpty() && read(copiedMap) == mapBytes, "map handoff opens the verified planned map")) { return 1; }
		ok &= expect(shell.applyLevelBrushPrimitive({}, &error), "edit the package-derived map");
		const auto editedMap = serializeLevelMap(shell.levelDocument()).bytes;
		const auto editedRevision = shell.levelDocument().revision;
		const auto editedUndoDepth = shell.levelDocument().undoStack.size();
		const QString rejectedMap = QDir(QFileInfo(copiedMap).absolutePath()).filePath(QStringLiteral("rejected.map"));
		ok &= expect(!shell.saveLevelDocument(copiedMap, true, &error) && error.contains(QStringLiteral("permanent folder"))
			&& !shell.saveLevelDocument(rejectedMap, false, &error) && !QFileInfo::exists(rejectedMap)
			&& read(copiedMap) == mapBytes && shell.levelDocument().revision == editedRevision
			&& shell.levelDocument().undoStack.size() == editedUndoDepth,
			"existing and new copy destinations are refused without writing or losing edits");
		const QString nestedMap = QDir(QFileInfo(copiedMap).absolutePath()).filePath(QStringLiteral("new/nested.map"));
		ok &= expect(!shell.saveLevelDocument(nestedMap, false, &error) && error.contains(QStringLiteral("permanent folder"))
			&& !QFileInfo::exists(QFileInfo(nestedMap).absolutePath()), "missing descendants of copy storage are refused before directory creation");
#ifdef Q_OS_WIN
		ok &= expect(!shell.saveLevelDocument(rejectedMap.toUpper(), false, &error) && error.contains(QStringLiteral("permanent folder")),
			"Windows copy protection ignores path case");
#endif
		// Both endpoints belong to the isolated fixture; remove only the verified
		// alias itself before any directory destructor can recurse through it.
		const QString alias = temporary.filePath(QStringLiteral("copy-alias"));
		const QString aliasTarget = QFileInfo(copiedMap).absolutePath();
		std::error_code linkError;
		std::filesystem::create_directory_symlink(std::filesystem::path(aliasTarget.toStdU16String()),
			std::filesystem::path(alias.toStdU16String()), linkError);
		bool linked = !linkError;
#ifdef Q_OS_WIN
		if (!linked) {
			const auto quote = [](QString path) { return path.replace(QLatin1Char('\''), QStringLiteral("''")); };
			QProcess junction; junction.setWorkingDirectory(temporary.path());
			junction.start(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
				QStringLiteral("New-Item -ItemType Junction -Path '%1' -Target '%2' -ErrorAction Stop | Out-Null").arg(quote(alias), quote(aliasTarget))});
			linked = junction.waitForFinished(10000) && junction.exitStatus() == QProcess::NormalExit && junction.exitCode() == 0
				&& QFileInfo(alias).isJunction();
		}
#endif
		ok &= expect(linked, "create a real alias into temporary map storage");
		if (linked) {
			const QString aliasOutput = QDir(alias).filePath(QStringLiteral("new/aliased.map"));
			ok &= expect(!shell.saveLevelDocument(aliasOutput, false, &error) && error.contains(QStringLiteral("permanent folder"))
				&& !QFileInfo::exists(QDir(aliasTarget).filePath(QStringLiteral("new"))), "new outputs through a directory alias are refused");
			editor->appendPlainText(QStringLiteral("unsaved independent edit"));
			const QString modifiedText = editor->toPlainText();
			const QString aliasScript = QDir(alias).filePath(QStringLiteral("aliased.cfg"));
			ok &= expect(!shell.saveCodeDocumentAs(inspectTextWriteTarget(aliasScript), &error) && error.contains(QStringLiteral("permanent folder"))
				&& !QFileInfo::exists(aliasScript) && editor->document()->isModified() && editor->toPlainText() == modifiedText
				&& read(savedScript) == "planned script", "Code alias refusal preserves unsaved text and its existing durable file");
			ok &= expect(shell.saveCodeDocumentAs(inspectTextWriteTarget(savedScript), &error) && !editor->document()->isModified()
				&& read(savedScript) == modifiedText.toUtf8() && read(fixture) == packageBeforeCodeSave,
				"Code can save the preserved edits to its durable destination after a refusal");
			const QFileInfo link(alias);
			ok &= expect(link.isJunction() ? QDir().rmdir(alias) : link.isSymLink() && QFile::remove(alias), "remove only the verified fixture alias");
			ok &= expect(read(copiedMap) == mapBytes, "alias cleanup preserves the verified map copy");
		}
		QTimer savePoll, saveWatchdog; saveWatchdog.setSingleShot(true);
		bool saveDialogSeen = false, suggestedProject = false, saveTimedOut = false;
		QString saveDestination;
		QObject::connect(&savePoll, &QTimer::timeout, [&] {
			auto* dialog = qobject_cast<QFileDialog*>(app.activeModalWidget());
			if (!dialog || saveDialogSeen) { return; }
			saveDialogSeen = true;
			suggestedProject = dialog->directory().canonicalPath() == QDir(project).canonicalPath();
			if (saveDestination.isEmpty()) { dialog->reject(); return; }
			dialog->setDirectory(QFileInfo(saveDestination).absolutePath());
			dialog->selectFile(QFileInfo(saveDestination).fileName());
			static_cast<QDialog*>(dialog)->accept();
		});
		QObject::connect(&saveWatchdog, &QTimer::timeout, [&] {
			saveTimedOut = true;
			if (auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget())) { dialog->reject(); }
		});
		auto* mapSave = shell.findChild<QAction*>(QStringLiteral("map.save"));
		const auto chooseMapOutput = [&](const QString& destination) {
			saveDestination = destination; saveDialogSeen = false; suggestedProject = false;
			savePoll.start(10); saveWatchdog.start(15000);
			if (mapSave) { mapSave->trigger(); }
			savePoll.stop(); saveWatchdog.stop();
		};
		chooseMapOutput({});
		ok &= expect(mapSave && saveDialogSeen && suggestedProject && !saveTimedOut
			&& serializeLevelMap(shell.levelDocument()).bytes == editedMap && shell.levelDocument().sourcePath == copiedMap,
			"Save offers the project directory and cancellation preserves map edits");
		chooseMapOutput(rejectedMap);
		ok &= expect(saveDialogSeen && !saveTimedOut && !QFileInfo::exists(rejectedMap)
			&& shell.statusBar()->currentMessage().contains(QStringLiteral("permanent folder"))
			&& shell.levelDocument().revision == editedRevision, "Save As refuses an explicitly chosen disposable destination");
		const QString savedMap = QDir(project).filePath(QStringLiteral("fixture-edited.map"));
		chooseMapOutput(savedMap);
		ok &= expect(saveDialogSeen && !saveTimedOut && read(savedMap) == editedMap && shell.levelDocument().sourcePath == savedMap
			&& read(copiedMap) == mapBytes && read(fixture) == packageBeforeMapSave,
			"Save As preserves package/copy bytes and adopts the independently saved map");
		QByteArray wad("PWAD");
		const auto u32 = [&](quint32 value) { for (int shift = 0; shift < 32; shift += 8) { wad.append(static_cast<char>(value >> shift)); } };
		u32(2); u32(20); wad.append("AAAABBBB");
		for (const quint32 offset : {12, 16}) { u32(offset); u32(4); wad.append("TEST.CFG", 8); }
		const QString duplicates = temporary.filePath(QStringLiteral("duplicates.wad"));
		QFile duplicateFile(duplicates);
		if (!expect(duplicateFile.open(QIODevice::WriteOnly) && duplicateFile.write(wad) == wad.size(), "write duplicate script fixture")) { return 1; }
		duplicateFile.close(); shell.openPathFromCommandLine(duplicates); filter->setText(QStringLiteral("TEST.CFG"));
		tests::PackageRow* secondOccurrence = nullptr;
		for (int row = 0; row < entries->count(); ++row) {
			if (entries->item(row)->data(Qt::UserRole + 7).toLongLong() == 1) { secondOccurrence = entries->item(row); }
		}
		if (secondOccurrence) { entries->setCurrentItem(secondOccurrence); entries->itemActivated(secondOccurrence); }
		ok &= expect(secondOccurrence && editor && editor->toPlainText() == QStringLiteral("BBBB") && editor->isReadOnly(),
			"explicit script activation preserves the exact second occurrence through the copy worker");
		auto* quickOpen = shell.findChild<QuickOpenDialog*>(QStringLiteral("quickOpen"));
		if (quickOpen) { quickOpen->entryChosen(QStringLiteral("package:TEST.CFG")); }
		ok &= expect(quickOpen && shell.statusBar()->currentMessage().contains(QStringLiteral("repeated entries"))
			&& editor && editor->toPlainText() == QStringLiteral("BBBB"), "path-only requests reject duplicates even when one occurrence is selected");
		if (secondOccurrence) {
			ok &= expect(!entries->model()->setData(secondOccurrence->index, 5000, Qt::UserRole + 6), "browser source identities are immutable");
			// Inject an invalid explicit request without making the production
			// snapshot writable just for this regression fixture.
			QStandardItemModel stale(1, 1); const auto invalid = stale.index(0, 0);
			stale.setData(invalid, QStringLiteral("TEST.CFG"), Qt::UserRole);
			stale.setData(invalid, 5000, Qt::UserRole + 6);
			PackageEntryView* view = entries; view->activated(invalid);
		}
		ok &= expect(shell.statusBar()->currentMessage().contains(QStringLiteral("entry changed"))
			&& editor && editor->toPlainText() == QStringLiteral("BBBB"), "stale explicit indexes fail without falling back to a same-name entry");
	}
	app.removeTranslator(&translator);
	return ok ? 0 : 1;
}
