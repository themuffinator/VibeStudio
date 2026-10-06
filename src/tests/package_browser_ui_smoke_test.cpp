#include "package_entry_test_helpers.h"
#include "app/application_shell.h"
#include "app/package_folder_view.h"
#include "app/studio_theme.h"
#include "core/package_draft.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QFileDialog>
#include <QInputDialog>
#include <QTimer>
#include <QThread>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLineEdit>
#include <QLayout>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTranslator>
#include <QTreeWidget>
#include <QToolBar>

#include <iostream>

using namespace vibestudio;

namespace {
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

void u32(QByteArray& bytes, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8) { bytes.append(static_cast<char>(value >> shift)); }
}
class ExpandedTranslator final : public QTranslator {
public:
	bool expanded = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!expanded || (QByteArray(context) != "VibeStudioPackageBrowser" && !QByteArray(context).endsWith("ApplicationShell"))
			|| (!QByteArray(source).contains("source entry") && QByteArray(source) != "Cancel" && QByteArray(source) != "Retry")) { return {}; }
		return QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source));
	}
};
}

int main(int argc, char** argv)
{
	// Direct Qt properties and QWidget::render only: no native input or capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QApplication app(argc, argv);
	QElapsedTimer workflowElapsed; workflowElapsed.start();
	qint64 previous = 0;
	const auto mark = [&](const char* phase) {
		const auto now = workflowElapsed.elapsed();
		std::cerr << "Browser timing " << phase << " total_ms=" << now << " phase_ms=" << now - previous << '\n';
		previous = now;
	};
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	QDir root(temporary.path()); root.mkdir(QStringLiteral("source"));
	QString error;
	PackageArchive empty;
	PackageStagingModel plan;
	bool ok = empty.load(root.filePath(QStringLiteral("source")), &error) && plan.loadBaseArchive(empty, &error);
	plan.addBytes("generated text", QStringLiteral("docs/deep/generated.txt"));
	plan.addBytes("other text", QStringLiteral("other.txt"));
	plan.renameEntry(QStringLiteral("other.txt"), QStringLiteral("renamed.txt"));
	QByteArray wave("RIFF"); u32(wave, 40); wave.append("WAVEfmt "); u32(wave, 16);
	wave.append(QByteArray::fromHex("01000100401f0000803e000002001000"));
	wave.append("data"); u32(wave, 4); wave.append(QByteArray(4, '\0'));
	plan.addBytes(wave, QStringLiteral("sound/generated.wav"));
	const QString draftPath = root.filePath(QStringLiteral("browser.vibepackage"));
	ok &= expect(PackageDraft::save(draftPath, &plan, false, &error), "prepare planned browser draft");
	QByteArray wad("PWAD"); u32(wad, 4); u32(wad, 20); wad.append("AAAABBBB");
	for (int index = 0; index < 4; ++index) {
		u32(wad, index < 2 ? 12 : 16); u32(wad, index % 2 ? 4 : 0);
		// Non-map markers keep the normal open-path router on Packages.
		QByteArray name = index % 2 ? QByteArray("THINGS") : index ? QByteArray("GROUPB") : QByteArray("GROUPA");
		name.resize(8, '\0'); wad.append(name);
	}
	const QString wadPath = root.filePath(QStringLiteral("maps.wad"));
	QFile wadFile(wadPath);
	ok &= wadFile.open(QIODevice::WriteOnly) && wadFile.write(wad) == wad.size(); wadFile.close();
	if (!ok) { return 1; }
	ExpandedTranslator translator;
	app.installTranslator(&translator);
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.filePath(QStringLiteral("qt-settings")));
	QCoreApplication::setOrganizationName(QStringLiteral("VibeStudioTests"));
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("settings.ini")));
	mark("fixtures");
	{
		ApplicationShell shell;
		mark("shell");
		for (const int scale : {100, 200}) {
			std::cerr << "Browser scale " << scale << '\n';
			translator.expanded = scale == 200;
			applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
			mark("theme");
			shell.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			shell.resize(1600, 1000);
			mark("direction-size");
			shell.openPathFromCommandLine(draftPath);
			mark("open-draft");
			shell.ensurePolished(); shell.layout()->activate(); shell.show(); app.processEvents();
			mark("layout-show");
			auto entries = tests::PackageRows(shell.findChild<PackageEntryView*>(QStringLiteral("packageEntries")));
			auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("packageFilter"));
			auto* tree = shell.findChild<PackageFolderView*>(QStringLiteral("packageTree"));
			auto* preview = shell.findChild<QPlainTextEdit*>(QStringLiteral("packageTextPreview"));
			auto* workbench = shell.findChild<QSplitter*>(QStringLiteral("packagesWorkbench"));
			ok &= expect(entries && filter && tree && preview && workbench, "package browser controls are present");
			if (!entries || !filter || !tree || !preview || !workbench) { continue; }
			ok &= expect(entries->focusPolicy() != Qt::NoFocus && filter->focusPolicy() != Qt::NoFocus && tree->focusPolicy() != Qt::NoFocus
				&& !entries->accessibleName().isEmpty() && !tree->accessibleName().isEmpty(), "browser remains focusable and named at each scale");
			ok &= expect(qAbs(preview->font().pointSizeF() - QApplication::font().pointSizeF()) < 0.1,
				"fixed-pitch package preview follows the live text scale");
			auto* detailText = workbench->findChild<QTextEdit*>(QStringLiteral("detailContent"));
			ok &= expect(detailText && qAbs(detailText->font().pointSizeF() - QApplication::font().pointSizeF()) < 0.1,
				"package detail content follows the live text scale");
			filter->setText(QStringLiteral("generated.txt"));
			auto* listCancel = shell.findChild<QAbstractButton*>(QStringLiteral("packageListCancel"));
			auto* listRetry = shell.findChild<QAbstractButton*>(QStringLiteral("packageListRetry"));
			// The same shell exercises live text scaling. Retranslate these
			// construction-time labels when the fixture changes its translator.
			if (listCancel && listRetry) {
				listCancel->setText(ApplicationShell::tr("Cancel")); listRetry->setText(ApplicationShell::tr("Retry"));
				ok &= expect(scale != 200 || (listCancel->text().contains(QStringLiteral("expanded")) && listRetry->text().contains(QStringLiteral("expanded"))),
					"listing controls exercise expanded translations at 200 percent");
			}
			auto* pendingRename = shell.findChild<QAction*>(QStringLiteral("package.stageRename"));
			PackageEntryView* entryView = entries;
			const auto renderListingTools = [&](QAbstractButton* action, const QString& state) {
				auto* toolbar = action ? qobject_cast<QToolBar*>(action->parentWidget()) : nullptr;
				if (!toolbar) { return false; }
				action->ensurePolished(); toolbar->ensurePolished();
				// Showing a previously hidden QWidgetAction after live scaling can
				// post a parent LayoutRequest. Flush layouts only, so measuring the
				// button does not also adopt a finished listing before Cancel is tested.
				QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
				toolbar->layout()->activate();
				const auto actionRect = QRect(action->mapTo(toolbar, QPoint()), action->size());
				// QToolBar may allocate less than a push button's preferred hint
				// while still fitting its complete label. Check actual text/glyph
				// width with six pixels of clear space on each side, then review
				// the render; a preferred-width comparison rejected an intact label.
				const int glyphWidth = action->icon().isNull() ? 0 : action->icon().actualSize(action->iconSize()).width() + 4;
				const int contentWidth = action->fontMetrics().size(Qt::TextShowMnemonic, action->text()).width() + glyphWidth + 12;
				const bool fits = toolbar->rect().contains(actionRect) && action->width() >= contentWidth;
				std::cerr << "Listing toolbar " << state.toStdString() << " at " << scale << "%: x=" << actionRect.x()
					<< " width=" << action->width() << " content=" << contentWidth << " hint=" << action->sizeHint().width() << " toolbar=" << toolbar->width() << '\n';
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
				if (captures.isEmpty()) { return fits; }
				QImage image(toolbar->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); toolbar->render(&image);
				return image.save(QDir(captures).filePath(QStringLiteral("package-list-%1-%2.png").arg(state).arg(scale))) && fits;
			};
			ok &= expect(renderListingTools(listCancel, QStringLiteral("pending")), "listing Cancel fits the scaled toolbar");
			ok &= expect(entryView->busy() && preview->toPlainText().isEmpty() && pendingRename && !pendingRename->isEnabled()
				&& listCancel && listCancel->isVisible() && listRetry && !listRetry->isVisible(),
				"pending listing clears previews and actions and exposes Cancel");
			if (listCancel && listRetry) {
				listCancel->click();
				ok &= expect(renderListingTools(listRetry, QStringLiteral("cancelled")), "listing Retry fits the scaled toolbar");
				const bool cancelledList = entryView->canRetry() && !listCancel->isVisible() && listRetry->isVisible()
					&& !entryView->currentIndex().data(Qt::UserRole + 6).isValid();
				if (!cancelledList) { std::cerr << "Listing cancel state: retry=" << entryView->canRetry() << " cancel-visible=" << listCancel->isVisible()
					<< " retry-visible=" << listRetry->isVisible() << " current=" << entryView->currentIndex().data(Qt::UserRole + 6).toString().toStdString() << '\n'; }
				ok &= expect(cancelledList, "shell Cancel exposes Retry without stale rows");
				listRetry->click();
			}
			mark("listing-cancel-retry");
			ok &= expect(entries->count() == 1 && waitForPreview(preview, QStringLiteral("generated text")) && preview->toPlainText() == QStringLiteral("generated text"), "planned bytes preview at each scale");
			ok &= expect(listCancel && listRetry && !listCancel->isVisible() && !listRetry->isVisible(), "successful listing hides Cancel and Retry");
			mark("planned-preview");
			filter->clear();
			ok &= expect(tree->folderIndex(QStringLiteral("docs/deep")).isValid(), "planned folder navigation is present");
			auto* search = shell.findChild<QLineEdit*>(QStringLiteral("workspaceSearch"));
			auto* results = shell.findChild<QListWidget*>(QStringLiteral("workspaceSearchResults"));
			auto* audioFilter = shell.findChild<QLineEdit*>(QStringLiteral("audioFilter"));
			auto* audioEntries = shell.findChild<QListWidget*>(QStringLiteral("audioEntries"));
			ok &= expect(search && results && audioFilter && audioEntries, "shared package search and audio filter controls are present");
			if (search && results && audioFilter && audioEntries) {
				search->setText(QStringLiteral("generated.txt"));
				QElapsedTimer timer; timer.start();
				const auto searchFound = [&]() {
					return results->count() == 1 && results->item(0)->data(Qt::UserRole + 1).toString() == QStringLiteral("docs/deep/generated.txt");
				};
				while (!searchFound() && timer.elapsed() < 2000) { app.processEvents(QEventLoop::AllEvents, 25); }
				ok &= expect(searchFound(), "workspace search includes staged paths");
				audioFilter->setText(QStringLiteral("ext=wav size>0"));
				ok &= expect(audioEntries->count() == 1 && !audioEntries->item(0)->isHidden()
					&& audioEntries->item(0)->data(Qt::UserRole).toString() == QStringLiteral("sound/generated.wav"),
					"asset property filters use staged file metadata");
				audioFilter->clear(); search->clear();
			}
			mark("search-audio");
			const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			const auto render = [&](const QString& name) {
				app.processEvents();
				if (!expect(workbench->width() >= 1200 && workbench->height() >= 500, "render the laid-out browser at its full size")) { return false; }
				if (captures.isEmpty()) { return true; }
				QImage image(workbench->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
				workbench->render(&image);
				return image.save(QDir(captures).filePath(QStringLiteral("package-browser-%1-%2.png").arg(name).arg(scale)));
			};
			ok &= expect(render(QStringLiteral("planned")), "render planned browser");
			mark("render-planned");
			shell.openPathFromCommandLine(wadPath);
			mark("open-wad");
			filter->setText(QStringLiteral("THINGS"));
			ok &= expect(entries->count() == 2, "repeated WAD names remain separate rows");
			if (entries->count() == 2) {
				const auto* first = entries->item(0); const auto* second = entries->item(1);
				ok &= expect(first->data(Qt::UserRole + 6) != second->data(Qt::UserRole + 6)
					&& first->data(Qt::UserRole + 7) != second->data(Qt::UserRole + 7)
					&& first->data(Qt::AccessibleTextRole).toString() != second->data(Qt::AccessibleTextRole).toString(),
					"repeated rows have distinct reader positions, source identities and spoken labels");
				ok &= expect(scale != 200 || (second->text().contains(QStringLiteral("expanded"))
					&& second->data(Qt::AccessibleTextRole).toString().contains(QStringLiteral("expanded"))),
					"enlarged occurrence labels exercise translation expansion");
				entries->setCurrentRow(1);
				const qint64 ordinal = entries->currentItem()->data(Qt::UserRole + 7).toLongLong();
				filter->setText(QStringLiteral("THING"));
				ok &= expect(entries->currentItem() && entries->currentItem()->data(Qt::UserRole + 7).toLongLong() == ordinal,
					"filter refresh preserves the selected repeated occurrence");
				ok &= expect(waitForPreview(preview, QStringLiteral("BBBB"))
					&& !preview->toPlainText().contains(QStringLiteral("AAAA")), "selected repeated occurrence previews its own bytes");
			}
			mark("occurrence-preview");
			ok &= expect(render(QStringLiteral("occurrences")), "render labelled WAD occurrences");
			mark("render-occurrences");
			if (scale == 100 && entries->count() == 2) {
				auto* replace = shell.findChild<QAction*>(QStringLiteral("package.stageReplace"));
				auto* rename = shell.findChild<QAction*>(QStringLiteral("package.stageRename"));
				auto* remove = shell.findChild<QAction*>(QStringLiteral("package.stageDelete"));
				auto* undo = shell.findChild<QAbstractButton*>(QStringLiteral("packageUndo"));
				auto staged = tests::StagingRows(shell.findChild<PackageStagingView*>(QStringLiteral("packageStagingSummary")));
				ok &= expect(replace && rename && remove && undo && staged, "occurrence editing actions are available");
				if (replace && rename && remove && undo && staged) {
					const QString replacementPath = root.filePath(QStringLiteral("replacement.bin"));
					QFile replacement(replacementPath); ok &= replacement.open(QIODevice::WriteOnly) && replacement.write("CCCC") == 4; replacement.close();
					QTimer dialogs; QElapsedTimer elapsed;
					int fileDialogs = 0, renameDialogs = 0;
					QObject::connect(&dialogs, &QTimer::timeout, &app, [&]() {
						auto* modal = qobject_cast<QDialog*>(app.activeModalWidget());
						if (elapsed.elapsed() > 10000) { ok &= expect(false, "edit dialog completed within its deadline"); if (modal) { modal->reject(); } return; }
						if (auto* file = qobject_cast<QFileDialog*>(modal)) {
							++fileDialogs; file->selectFile(replacementPath); QMetaObject::invokeMethod(file, "accept");
						} else if (auto* input = qobject_cast<QInputDialog*>(modal)) {
							++renameDialogs;
							if (input->windowTitle() == QStringLiteral("Stage Rename")) { input->setTextValue(QStringLiteral("ACTORS")); }
							input->accept();
						}
					});
					elapsed.start(); dialogs.start(10); replace->trigger(); dialogs.stop();
					ok &= expect(fileDialogs == 1 && entries->count() == 2 && waitForPreview(preview, QStringLiteral("CCCC")), "shell replacement targets the selected second occurrence");
					mark("replace");
					entries->setCurrentRow(0);
					ok &= expect(waitForPreview(preview, QStringLiteral("AAAA")), "shell replacement leaves the first repeated payload unchanged");
					tests::PackageRow* replacementRow = nullptr;
					for (int row = 0; row < staged->count(); ++row) {
						if (staged->item(row)->data(Qt::UserRole + 7).isValid() && staged->item(row)->data(Qt::UserRole + 7).toInt() == 3) { replacementRow = staged->item(row); break; }
					}
					ok &= expect(replacementRow && replacementRow->text().contains(QStringLiteral("source entry 4")), "staging row names the repeated occurrence");
					if (replacementRow) {
						staged->itemActivated(replacementRow);
						ok &= expect(entries->currentItem()->data(Qt::UserRole + 7).toInt() == 3 && waitForPreview(preview, QStringLiteral("CCCC")), "staging activation reveals its exact occurrence");
					}
					filter->setText(QStringLiteral("THINGS"));
					tests::waitForPackageEntries(entryView);
					elapsed.restart(); dialogs.start(10); rename->trigger(); dialogs.stop();
					filter->setText(QStringLiteral("ACTORS"));
					ok &= expect(renameDialogs == 2 && entries->count() == 1 && entries->currentItem()->data(Qt::UserRole + 7).toInt() == 3
						&& waitForPreview(preview, QStringLiteral("CCCC")), "shell rename preserves the selected occurrence payload");
					mark("rename");
					undo->click(); filter->setText(QStringLiteral("THINGS"));
					ok &= expect(entries->count() == 2, "undo restores the repeated name");
					entries->setCurrentRow(1); remove->trigger();
					ok &= expect(entries->count() == 1 && waitForPreview(preview, QStringLiteral("AAAA")), "shell delete removes only the selected occurrence");
					undo->click();
					ok &= expect(entries->count() == 2, "one undo restores the selected occurrence");
					entries->selectAll(); remove->trigger();
					ok &= expect(entries->count() == 1 && !entries->item(0)->data(Qt::UserRole + 6).isValid(), "batch delete leaves only the no-matches row");
					undo->click(); undo->click();
					filter->setText(QStringLiteral("THINGS")); entries->setCurrentRow(1);
					ok &= expect(entries->count() == 2 && waitForPreview(preview, QStringLiteral("BBBB")), "group undo and replacement undo restore original bytes");
					mark("delete-undo");
				}
			}
		}
	}
	mark("shell-destroyed");
	app.removeTranslator(&translator); app.processEvents();
	mark("final-events");
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
