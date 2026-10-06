#include "package_entry_test_helpers.h"
#include "app/application_shell.h"
#include "app/package_subset_dialog.h"
#include "app/studio_theme.h"
#include "core/package_draft.h"
#include "core/studio_settings.h"
#include "tests/package_subset_test_helpers.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::subset_test;
namespace {
bool expect(bool value, const char* label) { if (!value) { std::cerr << label << '\n'; } return value; }
bool wait(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	do { QApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); if (ready()) { return true; } QThread::msleep(5); } while (timer.elapsed() < 15000);
	return false;
}
class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		return QByteArray(context) == "PackageSubsetDialog" ? QStringLiteral("[%1 — expanded label]").arg(QString::fromUtf8(source)) : QString();
	}
};
QPushButton* button(QWidget& parent, const char* name) { return parent.findChild<QPushButton*>(QString::fromLatin1(name)); }
bool save(PackageSubsetDialog& dialog, const QString& path, bool approve = true, bool* prompted = nullptr)
{
	bool chosen = false; QTimer timer; timer.setInterval(5);
	QObject::connect(&timer, &QTimer::timeout, [&]() {
		if (auto* file = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); file && !chosen) {
			chosen = true; file->selectFile(path); QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
		} else if (auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			if (prompted) { *prompted = true; } question->button(approve ? QMessageBox::Yes : QMessageBox::No)->click();
		}
	});
	timer.start(); button(dialog, "packageSubsetExport")->click(); timer.stop();
	return chosen && wait([&]() { return !dialog.busy(); });
}
}
int main(int argc, char** argv)
{
	// Tests use direct Qt state and widget render targets, never native input.
	qputenv("QT_QPA_PLATFORM", "offscreen"); QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const QDir root(temporary.path()); bool ok = true; QString error; ExpandedTranslator translator;
	StudioSettings::setOverrideFilePath(root.filePath("settings.ini"));
	QTranslator english;
	if (english.load(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../i18n/vibestudio_en.qm")))) { app.installTranslator(&english); }
	const auto source = root.filePath("maps.wad"); auto archive = std::make_shared<PackageArchive>();
	ok &= wad(source, fixture()) && archive->load(source, &error); if (!expect(ok, "prepare subset UI fixture")) { return 1; }
	const auto original = get(source); PackageSelectionRequest selection; selection.entryIndexes = {index(*archive, "THINGS", 1)};
	for (int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		if (scale == 200) { app.installTranslator(&translator); }
		PackageSubsetDialog dialog(archive, selection); dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); } dialog.show();
		auto* table = dialog.findChild<QTableWidget*>("packageSubsetFiles"); auto* apply = button(dialog, "packageSubsetExport");
		auto* details = dialog.findChild<QPlainTextEdit*>("packageSubsetDetails");
		ok &= expect(!apply->isEnabled() && wait([&]() { return !dialog.busy() && table->rowCount() == 13; }), "review starts disabled and prepares asynchronously");
		if (!ok) { return 1; }
		ok &= expect(apply->isEnabled() && table->item(0, 0)->text().contains("MAP02") && table->item(1, 2)->text().contains("Selected")
			&& table->item(0, 2)->text().contains("Map MAP02"), "review distinguishes explicit occurrence and required group members");
		ok &= expect(!table->item(0, 0)->data(Qt::AccessibleTextRole).toString().isEmpty() && table->focusPolicy() != Qt::NoFocus
			&& apply->focusPolicy() != Qt::NoFocus && !apply->accessibleName().isEmpty() && details->isReadOnly(), "subset controls expose readable names and keyboard focus");
		ok &= expect(table->horizontalScrollBar()->maximum() == 0 && dialog.findChild<QLabel*>("packageSubsetStatus")->wordWrap(), "translated metadata stays within the viewport");
		auto* close = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close); auto* scroll = dialog.findChild<QScrollArea*>();
		ok &= expect(dialog.rect().contains(QRect(close->mapTo(&dialog, QPoint()), close->size())) && close->focusPolicy() != Qt::NoFocus, "Close remains visible and focusable at each scale");
		const auto capture = [&](const QString& name) {
			const QString folder = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT"); if (folder.isEmpty()) { return true; }
			QDir().mkpath(folder); QApplication::processEvents(); dialog.repaint();
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			return image.save(QDir(folder).filePath(QStringLiteral("subset-%1-%2.png").arg(scale).arg(name)));
		};
		ok &= capture("top"); scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); QApplication::processEvents();
		ok &= expect(scroll->viewport()->rect().contains(QRect(apply->mapTo(scroll->viewport(), QPoint()), apply->size())), "export action is reachable through the scaled scroll area");
		ok &= capture("actions");
		int completed = 0; PackageWriteReport last; dialog.exported = [&](const auto& report) { ++completed; last = report; };
		const auto output = root.filePath(QStringLiteral("subset-%1.wad").arg(scale));
		ok &= expect(save(dialog, output) && completed == 1 && last.succeeded() && details->toPlainText().contains(output), "UI exports the reviewed snapshot and reports its destination");
		PackageArchive reopened; QByteArray bytes;
		ok &= expect(reopened.load(output, &error) && reopened.readEntryBytes("THINGS", &bytes, &error) && bytes == "THINGS-second", "GUI exact occurrence survives writing");
		bool prompted = false; const auto first = get(output);
		ok &= expect(save(dialog, output, false, &prompted) && prompted && completed == 1 && get(output) == first, "declining overwrite preserves the existing export");
		ok &= expect(save(dialog, output, true) && completed == 2 && last.succeeded(), "confirmed overwrite uses the verified publication path");
		ok &= expect(save(dialog, source) && completed == 2 && get(source) == original, "GUI refuses to overwrite the source");
		app.removeTranslator(&translator);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastLight, UiDensity::Standard, 200)); app.installTranslator(&translator);
		QVector<Lump> files; for (int at = 0; at < 503; ++at) { files.append({QByteArray::number(at).rightJustified(4, '0') + ".txt", "payload"}); }
		const auto path = root.filePath("paged.pak"); auto paged = std::make_shared<PackageArchive>();
		ok &= pak(path, files) && paged->load(path, &error); PackageSelectionRequest all; all.query = "ext=txt";
		PackageSubsetDialog dialog(paged, all); dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.setLayoutDirection(Qt::RightToLeft); dialog.show();
		auto* table = dialog.findChild<QTableWidget*>("packageSubsetFiles"); auto* page = dialog.findChild<QSpinBox*>("packageSubsetPage");
		ok &= expect(wait([&]() { return !dialog.busy() && table->rowCount() == 500; }) && page->isVisible() && page->maximum() == 2, "large reviews bound visible widgets and expose every page");
		page->setValue(2); QApplication::processEvents();
		auto* scroll = dialog.findChild<QScrollArea*>();
		ok &= expect(scroll->horizontalScrollBar()->maximum() == 0 && scroll->viewport()->rect().contains(QRect(page->mapTo(scroll->viewport(), QPoint()), page->size())), "expanded RTL page control stays visible at 200 percent");
		ok &= expect(table->rowCount() == 3 && table->item(2, 0)->text().contains("0502.txt"), "last review page includes the final occurrence");
		table->setCurrentCell(2, 0);
		ok &= expect(dialog.findChild<QPlainTextEdit*>("packageSubsetDetails")->toPlainText().contains("0502.txt"), "paged details resolve the correct entry");
		if (const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT"); !captures.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			ok &= image.save(QDir(captures).filePath("subset-paged-200.png"));
		}
		const auto output = root.filePath("paged-out.pak"); PackageArchive reopened;
		ok &= expect(save(dialog, output) && reopened.load(output, &error) && reopened.summary().fileCount == 503, "export includes all reviewed pages");
		app.removeTranslator(&translator); applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	}
	{
		PackageSelectionRequest invalid; invalid.entryIndexes = {9999}; PackageSubsetDialog dialog(archive, invalid); dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.show();
		ok &= expect(wait([&]() { return !dialog.busy() && dialog.findChild<QLabel*>("packageSubsetStatus")->text().contains("index"); })
			&& !button(dialog, "packageSubsetExport")->isEnabled(), "invalid review cannot enable export");
	}
	{
		PackageSubsetDialog dialog(archive, selection); dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.show();
		ok &= wait([&]() { return !dialog.busy() && button(dialog, "packageSubsetExport")->isEnabled(); });
		auto changed = original; changed[12] = static_cast<char>(changed.at(12) ^ 1); ok &= put(source, changed);
		PackageWriteReport last; dialog.exported = [&](const auto& report) { last = report; };
		const auto output = root.filePath("changed.wad");
		ok &= expect(save(dialog, output) && !last.succeeded() && !last.blockedMessages.isEmpty() && !QFileInfo::exists(output)
			&& dialog.findChild<QPlainTextEdit*>("packageSubsetDetails")->toPlainText().contains(last.blockedMessages.first()), "changed source fails visibly without publication");
	}
	// The browser must pass its selected row index, including a portable draft's
	// staged bytes, without reducing it to an ambiguous name.
	ok &= put(source, original); PackageArchive refreshed; PackageStagingModel staged;
	ok &= refreshed.load(source, &error) && staged.loadBaseArchive(refreshed, &error);
	const auto replacement = root.filePath("replacement.bin"), draft = root.filePath("browser.vibepackage");
	ok &= put(replacement, "staged-things") && staged.replaceOccurrence(12, replacement, &error) && PackageDraft::save(draft, &staged, false, &error);
	{
		ApplicationShell shell; shell.openPathFromCommandLine(draft); shell.show(); QApplication::processEvents();
		auto* filter = shell.findChild<QLineEdit*>("packageFilter"); auto list = tests::PackageRows(shell.findChild<PackageEntryView*>("packageEntries"));
		filter->setText("THINGS"); ok &= expect(list->count() == 2, "browser preserves both map occurrences");
		if (list->count() != 2) { return 1; } list->setCurrentRow(1); button(shell, "packageExportSelected")->click();
		auto* dialog = dynamic_cast<PackageSubsetDialog*>(shell.findChild<QDialog*>("packageSubsetDialog"));
		ok &= expect(dialog && wait([&]() { return !dialog->busy() && button(*dialog, "packageSubsetExport")->isEnabled(); }), "browser selection opens exact subset review");
		if (dialog) {
			const auto output = root.filePath("browser-out.wad"); ok &= save(*dialog, output); PackageArchive reopened; QByteArray bytes;
			ok &= expect(reopened.load(output, &error) && reopened.readEntryBytes("THINGS", &bytes, &error) && bytes == "staged-things", "browser exports selected planned bytes");
			dialog->reject(); QApplication::processEvents();
		}
	}
	// Closing before or during the scheduled worker cannot leave a live thread.
	delete new PackageSubsetDialog(std::make_shared<PackageArchive>(refreshed), selection);
	QPointer<PackageSubsetDialog> closing = new PackageSubsetDialog(std::make_shared<PackageArchive>(refreshed), selection);
	closing->show(); QTimer::singleShot(0, closing, &PackageSubsetDialog::reject);
	ok &= expect(wait([&]() { return closing.isNull(); }), "closing review cancels and joins preparation before destruction");
	return ok ? 0 : 1;
}
