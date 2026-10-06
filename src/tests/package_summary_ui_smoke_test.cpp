#include "app/application_shell.h"
#include "app/package_staging_view.h"
#include "app/package_entry_view.h"
#include "app/package_folder_view.h"
#include "app/studio_charts.h"
#include "app/ui_primitives.h"
#include "core/package_validation.h"
#include "app/package_operation_dialog.h"
#include "app/studio_theme.h"
#include "package_summary_test_fixture.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QFileDialog>
#include <QLocale>
#include <QMenu>
#include <QToolButton>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QScrollBar>
#include <QSettings>
#include <QListWidget>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; } return value;
}
class ExpandedSummaryTranslator final : public QTranslator {
public:
	bool expanded = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		const QByteArray scope(context), text(source);
		return expanded && (scope.endsWith("ApplicationShell") || scope == "VibeStudioPackageStaging")
			&& (text.contains("supported range") || text == "Include Staging &Manifest")
			? QStringLiteral("[%1 — expanded explanation]").arg(QString::fromUtf8(source)) : QString();
	}
};
bool render(QWidget* widget, const QString& name)
{
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (root.isEmpty()) { return true; }
	widget->ensurePolished(); if (widget->layout()) { widget->layout()->activate(); }
	QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); widget->render(&image);
	return image.save(QDir(root).filePath(name + QStringLiteral(".png")));
}
bool waitUntil(QApplication& app, const std::function<bool()>& ready)
{
	QElapsedTimer time; time.start();
	do { app.processEvents(); if (ready()) { return true; } QThread::msleep(1); } while (time.elapsed() < 15000);
	return false;
}
QByteArray fileBytes(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool repairOverflow(QApplication& app, ApplicationShell& shell, const QString& path, int scale)
{
	std::cerr << "Starting browser repair " << scale << std::endl;
	auto* entries = shell.findChild<PackageEntryView*>(QStringLiteral("packageEntries"));
	auto* tree = shell.findChild<PackageFolderView*>(QStringLiteral("packageTree"));
	auto* composition = shell.findChild<QListWidget*>(QStringLiteral("packageComposition"));
	auto* chart = composition ? composition->parentWidget()->findChild<CompositionChart*>() : nullptr;
	const auto action = [&](const char* name) { return shell.findChild<QAction*>(QString::fromLatin1(name)); };
	auto* save = action("package.saveAs"); auto* remove = action("package.stageDelete");
	auto* undo = action("package.unstageLast"); auto* redo = action("package.redo");
	auto* manifest = action("package.writeManifest");
	auto* saveButton = shell.findChild<QToolButton*>(QStringLiteral("packageSaveAsButton"));
	DetailDrawer* drawer = nullptr;
	for (auto* frame : shell.findChildren<QFrame*>()) {
		auto* candidate = dynamic_cast<DetailDrawer*>(frame);
		if (candidate && candidate->title() == QStringLiteral("Package Entry Details")) { drawer = candidate; break; }
	}
	if (!expect(entries && tree && composition && chart && save && remove && undo && redo && drawer && manifest && saveButton && saveButton->menu(),
		"Browser recovery controls and entry inspector are registered.")) { return false; }
	const auto ready = [&](int count) { return waitUntil(app, [&] { return !entries->busy() && entries->entryCount() == count; }); };
	if (!expect(ready(2), "Oversized metadata remains browsable in the shell.")) { return false; }
	const auto original = fileBytes(path);
	bool ok = expect(!original.isEmpty(), "Retain the independent source fixture for preservation checks.");
	const auto oversized = [&] {
		QStringList text;
		for (int row = 0; row < composition->count(); ++row) { text << composition->item(row)->text(); }
		const auto root = tree->model()->index(0, 0);
		return !save->isEnabled() && chart->isHidden() && chart->slices().isEmpty() && chart->accessibleSummary().contains("proportions are unavailable")
			&& composition->wordWrap() && composition->horizontalScrollBar()->maximum() == 0
			&& text.join('\n').contains("Size exceeds the supported range") && !text.join('\n').contains("[##################]")
			&& root.data(Qt::AccessibleDescriptionRole).toString().contains("Size exceeds the supported range")
			&& root.data(Qt::UserRole + 1).toString() == QStringLiteral("warning");
	};
	ok &= expect(oversized(), "Tree and composition expose overflow in text, omit false proportions, and keep export blocked.");
	entries->clearSelection(); entries->selectEntry(QStringLiteral("a.bin"));
	const auto selected = entries->currentIndex();
	const auto exactSize = QLocale().toString(std::numeric_limits<quint64>::max());
	ok &= expect(selected.data(Qt::UserRole).toString() == QStringLiteral("a.bin") && entries->selectedEntryIndexes() == QVector<qsizetype>{0}
		&& selected.data().toString().contains(exactSize) && selected.data(Qt::AccessibleTextRole).toString().contains(exactSize)
		&& selected.data(Qt::ToolTipRole).toString().contains(exactSize) && remove->isEnabled(),
		"A huge entry is selectable by its exact identity and has a positive, exact localized size in all row presentations.");
	bool drawerWarning = false;
	for (const auto& section : drawer->sections()) {
		if (section.id == QStringLiteral("composition")) {
			drawerWarning = section.state == OperationState::Warning && section.content.contains("proportions are unavailable")
				&& section.content.contains("Size exceeds the supported range") && !section.content.contains("[##################]");
		}
	}
	ok &= expect(drawerWarning, "The entry drawer shares the checked category sizes and non-proportional overflow diagnosis.");
	for (auto* tabs : shell.findChildren<QTabWidget*>()) {
		if (tabs->indexOf(composition->parentWidget()) >= 0) { tabs->setCurrentWidget(composition->parentWidget()); }
	}
	auto* splitter = shell.findChild<QSplitter*>(QStringLiteral("packagesWorkbench"));
	if (splitter) { splitter->setSizes(scale == 200 ? QList<int>{240, 980, 1180} : QList<int>{180, 620, 1000}); }
	app.processEvents();
	ok &= expect(splitter && render(splitter, QStringLiteral("package-overflow-browser-%1").arg(scale)), "Render browseable overflow warnings at both scales.");
	remove->trigger();
	if (!expect(ready(1), "Deleting the offending row leaves the surviving entry browsable.")) { return false; }
	const auto repaired = [&] {
		const auto slices = chart->slices();
		return save->isEnabled() && !chart->isHidden() && slices.size() == 1 && slices.first().value == 1.0
			&& !chart->accessibleSummary().contains("supported range")
			&& !tree->model()->index(0, 0).data(Qt::AccessibleDescriptionRole).toString().contains("supported range")
			&& entries->model()->index(0, 0).data(Qt::UserRole).toString() == QStringLiteral("b.bin");
	};
	ok &= expect(repaired() && undo->isEnabled(), "A representable plan restores exact composition and Save As.");
	undo->trigger();
	ok &= expect(ready(2) && oversized() && redo->isEnabled(), "Undo restores browsable overflow and the export blocker.");
	redo->trigger();
	if (!expect(ready(1) && repaired(), "Redo restores the repaired plan and clears stale warnings.")) { return false; }
	app.processEvents();
	ok &= expect(render(splitter, QStringLiteral("package-overflow-repaired-%1").arg(scale)), "Render the repaired package with accurate size proportions.");
	ok &= expect(manifest->isCheckable() && manifest->isChecked() && manifest->isEnabled() && !manifest->toolTip().isEmpty()
		&& saveButton->popupMode() == QToolButton::MenuButtonPopup && saveButton->focusPolicy() != Qt::NoFocus,
		"Save As exposes the default-enabled manifest option through a named, focusable native menu button.");
	// A new window constructed in this locale would translate its static menu once.
	manifest->setText(ApplicationShell::tr("Include Staging &Manifest"));
	ok &= expect(scale != 200 || manifest->text().contains("expanded explanation"), "The 200% menu fixture exercises translation expansion.");
	auto* menu = saveButton->menu(); menu->ensurePolished();
	// QWidget::adjustSize caps hidden top-level widgets to two thirds of the
	// offscreen virtual monitor. QMenu normally sizes itself when opened.
	menu->resize(menu->sizeHint());
	QString menuLabel = manifest->text(); menuLabel.remove(QLatin1Char('&'));
	ok &= expect(menu->actionGeometry(manifest).width() > menu->fontMetrics().horizontalAdvance(menuLabel),
		"The complete translated option fits in its native menu action rectangle.");
	ok &= expect(render(saveButton, QStringLiteral("package-overflow-save-button-%1").arg(scale))
		&& render(menu, QStringLiteral("package-overflow-save-options-%1").arg(scale)), "Render the export option without opening a popup or grabbing input.");
	const auto saveTo = [&](const QString& destination) {
		bool visited = false;
		QTimer::singleShot(0, &shell, [&] {
			if (auto* picker = qobject_cast<QFileDialog*>(app.activeModalWidget())) {
				visited = true; picker->selectFile(destination); QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection);
			}
		});
		save->trigger();
		QPointer<QDialog> dialog;
		for (auto* candidate : shell.findChildren<QDialog*>(QStringLiteral("packageSaveDialog"))) { if (candidate->isVisible()) { dialog = candidate; } }
		if (!expect(visited && dialog && waitUntil(app, [&] { return dialog && !dialog->property("operationRunning").toBool(); }),
			"Save As completes through the normal GUI worker.")) { return false; }
		const bool exists = QFileInfo::exists(destination);
		if (!exists) { if (auto* report = dialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails"))) { std::cerr << report->toPlainText().toStdString() << std::endl; } }
		QMetaObject::invokeMethod(dialog, "reject", Qt::DirectConnection);
		return exists;
	};
	// The strict staging manifest hashes deleted original payloads as well.
	// Choose archive-only export to repair deliberately invalid source metadata.
	manifest->setChecked(false);
	const QString output = QFileInfo(path).dir().filePath(QStringLiteral("repaired-%1.zip").arg(scale));
	if (!expect(saveTo(output), "Archive-only Save As repairs the damaged source without requiring historical payload hashes.")) { return false; }
	PackageArchive reopened; QString error; QByteArray payload;
	const bool opened = reopened.load(output, &error);
	const auto validation = validatePackage(reopened);
	ok &= expect(opened && reopened.summary().entryCount == 1 && reopened.summary().totalSizeBytes == 1 && !reopened.summary().totalSizeOverflow
		&& validation.valid() && validation.verifiedCount == 1 && validation.bytesRead == 1
		&& reopened.readEntryBytes(QStringLiteral("b.bin"), &payload, &error) && payload == QByteArray("x")
		&& !QFileInfo::exists(QFileInfo(output).dir().filePath(QStringLiteral("repaired-%1.manifest.json").arg(scale)))
		&& fileBytes(path) == original && ready(1) && repaired() && !undo->isEnabled(),
		"The independently reopened output verifies its surviving byte; adoption clears history and preserves the original source.");
	const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	if (!captures.isEmpty()) {
		ok &= expect(QFile::copy(output, QDir(captures).filePath(QStringLiteral("package-overflow-repaired-%1.zip").arg(scale))),
			"Retain the repaired output for independent format validation.");
	}
	manifest->setChecked(true);
	const QString withManifest = QFileInfo(path).dir().filePath(QStringLiteral("repaired-manifest-%1.zip").arg(scale));
	if (!expect(saveTo(withManifest), "The same GUI option creates a strict manifest once the repaired package is the source.")) { return false; }
	const QString manifestPath = QFileInfo(path).dir().filePath(QStringLiteral("repaired-manifest-%1.manifest.json").arg(scale));
	const auto document = QJsonDocument::fromJson(fileBytes(manifestPath)).object();
	const auto before = document.value("beforeEntries").toArray(), after = document.value("afterEntries").toArray();
	ok &= expect(before.size() == 1 && after.size() == 1 && before.first().toObject().value("sha256").toString()
		== QStringLiteral("2d711642b726b04401627ca9fbac32f5c8530fb1903cc4db02258717921a4881")
		&& before.first().toObject().value("sha256") == after.first().toObject().value("sha256") && fileBytes(path) == original,
		"Manifest-enabled export records verified before/after payloads and still leaves the original damaged source untouched.");
	if (!captures.isEmpty()) { ok &= QFile::copy(manifestPath, QDir(captures).filePath(QStringLiteral("package-overflow-repaired-%1.manifest.json").arg(scale))); }
	std::cerr << "Browser repair completed " << scale << std::endl;
	return ok;
}
bool cached(const PackageStagingModel& plan)
{
	int progress = 0; PackageReadControl control;
	control.progress = [&](const QString&, qint64, qint64) { ++progress; };
	return plan.preparePlan(nullptr, control) && progress == 0 && plan.summary().totalsAvailable;
}
bool worker(QApplication& app, const QString& path, int scale, bool cancel)
{
	std::mutex mutex; std::condition_variable gate; bool release = false;
	std::atomic_bool reached{false}, stopped{false}, onWorker{false};
	bool tick = false, visible = false, captured = false;
	QElapsedTimer elapsed; elapsed.start();
	PackageReadControl control;
	control.isCancelled = [&] { return stopped.load(); };
	control.progress = [&](const QString& phase, qint64, qint64) {
		if (phase != QStringLiteral("Preparing package summaries") || reached.exchange(true)) { return; }
		std::cerr << "Summary preparation gate reached" << std::endl;
		onWorker = QThread::currentThread() != app.thread();
		std::unique_lock lock(mutex); gate.wait_for(lock, std::chrono::seconds(5), [&] { return release; });
	};
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&] {
		if (!reached || tick) { return; }
		auto* dialog = qobject_cast<QDialog*>(app.activeModalWidget());
		auto* status = dialog ? dialog->findChild<QLabel*>(QStringLiteral("packageOperationSummary")) : nullptr;
		auto* details = dialog ? dialog->findChild<QPlainTextEdit*>(QStringLiteral("packageOperationDetails")) : nullptr;
		visible = status && status->text().contains(QStringLiteral("Records checked:"))
			&& details && details->toPlainText().contains(QStringLiteral("Preparing package summaries"))
			&& status->contentsRect().height() >= status->fontMetrics().lineSpacing() * 2;
		if (!visible && elapsed.elapsed() < 3000) { return; }
		auto* button = dialog ? dialog->findChild<QPushButton*>(QStringLiteral("cancelPackageOperation")) : nullptr;
		visible = visible && button && button->isEnabled() && !button->accessibleName().isEmpty();
		std::cerr << "UI timer releasing summary gate, visible=" << visible << std::endl;
		captured = dialog && render(dialog, QStringLiteral("package-summary-worker-%1-%2").arg(scale).arg(cancel ? "cancel" : "complete"));
		tick = true; stopped = cancel;
		{ std::lock_guard lock(mutex); release = true; } gate.notify_all();
	});
	timer.start(10);
	std::cerr << "Starting worker check " << scale << " cancel=" << cancel << std::endl;
	const auto result = runPackageLoadDialog(nullptr, path, control);
	std::cerr << "Worker check returned " << scale << " cancel=" << cancel << std::endl;
	bool ok = expect(reached && onWorker && tick && visible && captured, "Opening prepares visible, accessible summaries on a worker while the UI dispatches timers.");
	ok &= expect(cancel ? result.cancelled && !result.ready()
		: result.ready() && cached(result.staging) && result.staging.summary().afterSizeOverflow && !result.staging.summary().canSave,
		"Cancellation rejects adoption; successful opening returns a complete cached blocked plan.");
	return ok;
}
}

int main(int argc, char** argv)
{
	// Direct Qt state/actions and Qt-owned rendering only; no native input/capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	std::cerr << "Constructing QApplication" << std::endl;
	QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QApplication app(argc, argv);
	std::cerr << "QApplication ready" << std::endl;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path());
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.filePath(QStringLiteral("qt-settings")));
	QCoreApplication::setOrganizationName(QStringLiteral("VibeStudioTests"));
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("settings.ini")));
	bool ok = true; const QString path = root.filePath(QStringLiteral("oversized.zip"));
	if (!tests::summaryZip(path, {{"a.bin", std::numeric_limits<quint64>::max()}, {"b.bin", 1}})) { return 1; }
	ExpandedSummaryTranslator translator; app.installTranslator(&translator);
	{
		std::cerr << "Constructing ApplicationShell" << std::endl;
		ApplicationShell shell;
		std::cerr << "ApplicationShell ready" << std::endl;
		for (const int scale : {100, 200}) {
			std::cerr << "Applying summary theme " << scale << std::endl;
			translator.expanded = scale == 200;
			applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
			app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); shell.setLayoutDirection(app.layoutDirection());
			ok &= worker(app, path, scale, false); ok &= worker(app, path, scale, true);
			shell.resize(scale == 200 ? 2400 : 1800, scale == 200 ? 1700 : 1100); shell.show();
			std::cerr << "Opening shell package " << scale << std::endl;
			shell.openPathFromCommandLine(path); app.processEvents();
			std::cerr << "Shell package opened " << scale << std::endl;
			auto* summary = shell.findChild<PackageStagingView*>(QStringLiteral("packageStagingSummary"));
			auto* save = shell.findChild<QAction*>(QStringLiteral("package.saveAs"));
			auto* details = shell.findChild<QPlainTextEdit*>(QStringLiteral("packageStagingDetails"));
			if (!expect(summary && save && details && details->isReadOnly() && details->toPlainText().isEmpty(), "Package summary, export controls and cleared selectable details are registered.")) { return 1; }
			for (auto* tabs : shell.findChildren<QTabWidget*>()) { if (tabs->indexOf(summary->parentWidget()) >= 0) { tabs->setCurrentWidget(summary->parentWidget()); } }
			if (auto* splitter = shell.findChild<QSplitter*>(QStringLiteral("packagesWorkbench"))) { splitter->setSizes({200, 360, scale == 200 ? 1350 : 900}); }
			app.processEvents();
			QStringList lines; QModelIndex proportions, conflict;
			for (int row = 0; row < summary->model()->rowCount(); ++row) {
				const auto index = summary->model()->index(row, 0);
				lines << index.data().toString();
				if (index.data(Qt::UserRole + 1).toString() == QStringLiteral("idle")) { ok &= expect(!index.data(Qt::ForegroundRole).isValid(), "Idle rows use the normal palette text instead of an invalid black color."); }
				if (index.data(Qt::UserRole + 10).toBool() && !index.data(Qt::UserRole + 5).isValid()) { conflict = index; }
				if (!proportions.isValid() && index.data().toString().contains("proportions are unavailable")) { proportions = index; }
			}
			const QString text = lines.join('\n');
			ok &= expect(!save->isEnabled() && text.contains("Staging blocked") && text.contains("Size exceeds the supported range")
				&& text.contains("proportions are unavailable") && !text.contains("0 B") && !text.contains("[##################]")
				&& !summary->accessibleName().isEmpty() && summary->focusPolicy() != Qt::NoFocus && summary->wordWrap()
				&& summary->layoutDirection() == app.layoutDirection() && summary->horizontalScrollBar()->maximum() == 0, "Oversized totals have textual blockers and no false zero totals or proportions at both scales and directions.");
			summary->setCurrentIndex(conflict);
			ok &= expect(conflict.isValid() && details->toPlainText() == summary->currentDetails() && details->toPlainText().contains("supported range")
				&& !details->accessibleName().isEmpty(), "Selecting a compact blocker exposes its complete text in the named detail pane.");
			summary->scrollToTop(); app.processEvents();
			ok &= expect(render(summary->parentWidget(), QStringLiteral("package-summary-blocked-%1").arg(scale)), "Render the blocked summary using Qt-owned pixels.");
			if (proportions.isValid()) { summary->scrollTo(proportions, QAbstractItemView::PositionAtTop); app.processEvents(); }
			ok &= expect(proportions.isValid() && render(summary->parentWidget(), QStringLiteral("package-summary-composition-%1").arg(scale)), "Render overflow composition warnings at both scales.");
			if (!repairOverflow(app, shell, path, scale)) { return 1; }
		}
	}
	translator.expanded = false;
	PackageStagingModel generated; generated.createEmpty(PackageArchiveFormat::Zip); generated.addBytes("abc", "a.txt");
	PackageWriteRequest request; request.destinationPath = root.filePath(QStringLiteral("saved.zip"));
	const auto reopened = reopenSavedPackage(generated.writeArchive(request));
	ok &= expect(reopened.ready() && cached(reopened.staging) && reopened.staging.summary().afterBytes == 3,
		"A committed output is reopened with summaries already prepared for UI adoption.");
	return ok ? 0 : 1;
}
