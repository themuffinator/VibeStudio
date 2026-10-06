#include "package_entry_test_helpers.h"
#include "app/application_shell.h"
#include "app/package_wad_groups_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "core/package_draft.h"
#include "tests/package_subset_test_helpers.h"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
#include <QToolButton>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::subset_test;
namespace {
bool expect(bool value, const char* label) { if (!value) { std::cerr << label << '\n'; } return value; }
bool wait(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start(); do { QApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		if (ready()) { return true; } QThread::msleep(5); } while (timer.elapsed() < 15000); return false;
}
class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{ return QByteArray(context) == "PackageWadGroupsDialog" ? QStringLiteral("[%1 — expanded label]").arg(QString::fromUtf8(source)) : QString(); }
};
QPushButton* button(QWidget& parent, const char* name) { return parent.findChild<QPushButton*>(QString::fromLatin1(name)); }
bool choose(PackageWadGroupsDialog& dialog, const QString& name)
{
	auto* groups = dialog.findChild<QComboBox*>("packageGroupsSelection");
	if (!wait([&]() { return !dialog.busy() && groups->count() > 0; })) { return false; }
	for (int at = 0; at < groups->count(); ++at) { if (groups->itemText(at).contains(name + " ·")) { groups->setCurrentIndex(at); return true; } } return false;
}
bool prepare(PackageWadGroupsDialog& dialog, bool remove, const QString& target = {})
{
	dialog.findChild<QComboBox*>("packageGroupsOperation")->setCurrentIndex(remove ? 1 : 0);
	if (!remove) { dialog.findChild<QLineEdit*>("packageGroupsTarget")->setText(target); }
	button(dialog, "packageGroupsReview")->click(); return wait([&]() { return !dialog.busy(); });
}
bool capture(QWidget& widget, const QString& name)
{
	const auto folder = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT"); if (folder.isEmpty()) { return true; }
	QDir().mkpath(folder); QApplication::processEvents(); widget.repaint(); QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent); widget.render(&image); return image.save(QDir(folder).filePath(name));
}
}
int main(int argc, char** argv)
{
	// Direct Qt state and render targets only; no native input or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen"); QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const QDir root(temporary.path()); StudioSettings::setOverrideFilePath(root.filePath("settings.ini")); bool ok = true; QString error;
	const auto source = root.filePath("maps.wad"); PackageArchive archive; PackageStagingModel original;
	ok &= wad(source, fixture()) && archive.load(source, &error) && original.loadBaseArchive(archive, &error);
	if (!expect(ok, "prepare group UI source")) { return 1; } const auto sourceBytes = get(source); ExpandedTranslator translator;
	for (int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		if (scale == 200) { app.installTranslator(&translator); }
		PackageWadGroupsDialog dialog(original); dialog.setAttribute(Qt::WA_DeleteOnClose, false); if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); }
		PackageStagingModel applied; dialog.apply = [&](auto&& candidate, const auto&, QString*) { applied = std::move(candidate); return true; }; dialog.show();
		ok &= expect(!button(dialog, "packageGroupsApply")->isEnabled() && choose(dialog, "MAP02") && prepare(dialog, false, "MAP03"), "group inspection and review run asynchronously");
		auto* changes = dialog.findChild<QTableWidget*>("packageGroupsChanges"); auto* apply = button(dialog, "packageGroupsApply");
		ok &= expect(apply->isEnabled() && changes->rowCount() == 1 && changes->item(0, 1)->text().contains("MAP02")
			&& changes->item(0, 2)->text().contains("MAP03") && original.operations().isEmpty(), "review shows exact labels before adoption");
		ok &= expect(!changes->item(0, 0)->data(Qt::AccessibleTextRole).toString().isEmpty() && apply->focusPolicy() != Qt::NoFocus
			&& !apply->accessibleName().isEmpty() && dialog.findChild<QPlainTextEdit*>("packageGroupsDetails")->isReadOnly(), "group edit exposes readable state and keyboard focus");
		auto* selection = dialog.findChild<QComboBox*>("packageGroupsSelection");
		ok &= expect(selection->fontMetrics().horizontalAdvance(selection->currentText()) < selection->width() - 70
			&& selection->toolTip().contains("13"), "compact group name fits expanded RTL without hiding its identifier or losing member details");
		auto* close = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close); auto* scroll = dialog.findChild<QScrollArea*>();
		ok &= expect(dialog.rect().contains(QRect(close->mapTo(&dialog, QPoint()), close->size())) && changes->horizontalScrollBar()->maximum() == 0, "Close and review columns fit scaled RTL layout");
		scroll->verticalScrollBar()->setValue(0); ok &= capture(dialog, QString("groups-%1-top.png").arg(scale));
		scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); ok &= capture(dialog, QString("groups-%1-actions.png").arg(scale));
		dialog.findChild<QLineEdit*>("packageGroupsTarget")->setText("MAP04"); ok &= expect(!apply->isEnabled() && changes->rowCount() == 0, "changing a reviewed target invalidates Apply");
		ok &= prepare(dialog, true); ok &= expect(apply->isEnabled() && changes->rowCount() == 13, "delete review includes all optional map members");
		apply->click(); ok &= expect(applied.isLoaded() && applied.operations().size() == 13 && applied.undo() && applied.plannedEntries().size() == original.plannedEntries().size(), "Apply adopts one complete undoable edit");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	PackageStagingModel fresh; ok &= fresh.createEmpty(PackageArchiveFormat::Wad);
	for (const auto& [name, bytes] : newWadFixture()) { ok &= fresh.addBytes(bytes, QString::fromLatin1(name)); }
	for (int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		if (scale == 200) { app.installTranslator(&translator); }
		PackageWadGroupsDialog dialog(fresh); dialog.setAttribute(Qt::WA_DeleteOnClose, false); if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); }
		PackageStagingModel applied; dialog.apply = [&](auto&& candidate, const auto&, QString*) { applied = std::move(candidate); return true; }; dialog.show();
		ok &= expect(choose(dialog, "INTRO") && prepare(dialog, false, "BEGIN"), "source-free WAD groups are immediately available in review");
		auto* changes = dialog.findChild<QTableWidget*>("packageGroupsChanges"); auto* apply = button(dialog, "packageGroupsApply");
		ok &= expect(apply->isEnabled() && changes->rowCount() == 2 && changes->item(0, 1)->text().contains("INTRO")
			&& changes->item(1, 2)->text().contains("GL_BEGIN"), "source-free review includes the named map and its GL companion");
		dialog.findChild<QScrollArea*>()->verticalScrollBar()->setValue(0); ok &= capture(dialog, QString("groups-new-%1.png").arg(scale));
		apply->click(); ok &= expect(applied.isLoaded() && applied.sourcePath().isEmpty() && applied.undo()
			&& applied.plannedEntries().at(1).virtualPath == "INTRO", "source-free GUI edit uses ordinary atomic undo");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastLight, UiDensity::Standard, 200)); app.installTranslator(&translator);
	{
		QVector<Lump> entries{{"F_START", {}}}; for (int i = 0; i < 503; ++i) { entries.append({QByteArray("F") + QByteArray::number(i), "flat"}); } entries.append(Lump{"F_END", {}});
		const auto path = root.filePath("large.wad"); PackageArchive large; PackageStagingModel plan;
		ok &= wad(path, entries) && large.load(path, &error) && plan.loadBaseArchive(large, &error);
		PackageWadGroupsDialog dialog(plan); dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.setLayoutDirection(Qt::RightToLeft);
		dialog.apply = [&](auto&& candidate, const auto&, QString*) { plan = std::move(candidate); return true; }; dialog.show();
		ok &= choose(dialog, "F_START") && prepare(dialog, true); auto* page = dialog.findChild<QSpinBox*>("packageGroupsPage"); auto* table = dialog.findChild<QTableWidget*>("packageGroupsChanges");
		ok &= expect(table->rowCount() == 500 && page->maximum() == 2, "large group review is bounded to 500 rows per page"); page->setValue(2);
		ok &= expect(table->rowCount() == 5 && table->item(4, 1)->text().contains("F_END"), "paged review includes the final namespace boundary");
		ok &= capture(dialog, "groups-paged-200.png"); button(dialog, "packageGroupsApply")->click();
		ok &= expect(plan.plannedEntries().isEmpty() && plan.undo() && plan.plannedEntries().size() == 505, "paged application includes all reviewed members");
	}
	app.removeTranslator(&translator); applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		PackageWadGroupsDialog dialog(original); dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.apply = [](auto&&, const auto&, QString* reason) { *reason = "Owner revision changed"; return false; }; dialog.show();
		ok &= choose(dialog, "MAP02") && prepare(dialog, false, "MAP03"); button(dialog, "packageGroupsApply")->click();
		ok &= expect(!button(dialog, "packageGroupsApply")->isEnabled() && dialog.findChild<QLabel*>("packageGroupsStatus")->text().contains("Owner revision changed"), "owner refusal is visible and disables stale Apply");
	}
	{
		PackageWadGroupsDialog dialog(original); dialog.setAttribute(Qt::WA_DeleteOnClose, false); bool applied = false;
		dialog.apply = [&](auto&&, const auto&, QString*) { applied = true; return true; }; dialog.show();
		ok &= choose(dialog, "MAP02"); dialog.findChild<QLineEdit*>("packageGroupsTarget")->setText("MAP03");
		button(dialog, "packageGroupsReview")->click(); button(dialog, "packageGroupsCancel")->click();
		ok &= expect(wait([&]() { return !dialog.busy(); }) && !button(dialog, "packageGroupsApply")->isEnabled() && !applied
			&& dialog.findChild<QLabel*>("packageGroupsStatus")->text().contains("cancelled"), "cancellation never makes a completed worker result applicable");
		ok &= choose(dialog, "F_START"); dialog.findChild<QComboBox*>("packageGroupsOperation")->setCurrentIndex(0);
		ok &= expect(!button(dialog, "packageGroupsReview")->isEnabled() && !dialog.findChild<QLineEdit*>("packageGroupsTarget")->isEnabled()
			&& dialog.findChild<QLabel*>("packageGroupsStatus")->text().contains("deletion only"), "unsupported namespace rename explains the disabled controls");
	}
	{
		ApplicationShell shell; shell.show();
		// Generic .wad opening intentionally routes maps to Levels. Exercise the
		// explicit Packages > Open command through its non-native Qt dialog.
		bool chosen = false; QTimer picker; picker.setInterval(5);
		QObject::connect(&picker, &QTimer::timeout, [&]() {
			if (auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); dialog && !chosen) {
				chosen = true; dialog->selectFile(source); QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
			}
		});
		picker.start(); shell.findChild<QAction*>("package.open")->trigger(); picker.stop();
		auto* groups = shell.findChild<QToolButton*>("packageWadGroups"); auto list = tests::PackageRows(shell.findChild<PackageEntryView*>("packageEntries"));
		auto* filter = shell.findChild<QLineEdit*>("packageFilter");
		filter->setText("MAP02");
		const auto shows = [&](const QString& name) { return list->count() == 1 && list->item(0)->data(Qt::UserRole).toString() == name; };
		ok &= expect(chosen && groups && wait([&]() { return groups->isEnabled() && shows("MAP02"); }), "shell exposes WAD group action after package loading");
		if (!groups || !shows("MAP02")) { return 1; } groups->click();
		auto* dialog = dynamic_cast<PackageWadGroupsDialog*>(shell.findChild<QDialog*>("packageWadGroupsDialog"));
		ok &= expect(dialog && choose(*dialog, "MAP02") && prepare(*dialog, false, "MAP03"), "shell opens current package group snapshot");
		if (dialog) { button(*dialog, "packageGroupsApply")->click(); }
		filter->setText("MAP03"); ok &= expect(wait([&]() { return shows("MAP03"); }), "applied group edit refreshes browser");
		shell.findChild<QToolButton*>("packageUndo")->click(); filter->setText("MAP02");
		ok &= expect(shows("MAP02"), "shell undo restores map marker");
	}
	{
		const auto freshPath = root.filePath("new.vibepackage"); ok &= PackageDraft::save(freshPath, &fresh, false, &error);
		ApplicationShell shell; shell.show(); shell.openPathFromCommandLine(freshPath);
		auto* action = shell.findChild<QAction*>("package.groups"); auto list = tests::PackageRows(shell.findChild<PackageEntryView*>("packageEntries"));
		auto* filter = shell.findChild<QLineEdit*>("packageFilter"); filter->setText("TITLEMAP");
		const auto shows = [&](const QString& name) { return list->count() == 1 && list->item(0)->data(Qt::UserRole).toString() == name; };
		ok &= expect(action && wait([&]() { return action->isEnabled() && shows("TITLEMAP"); }), "shell enables group edits for a source-free draft");
		if (action) { action->trigger(); }
		auto* dialog = dynamic_cast<PackageWadGroupsDialog*>(shell.findChild<QDialog*>("packageWadGroupsDialog"));
		ok &= expect(dialog && choose(*dialog, "TITLEMAP") && prepare(*dialog, false, "MENU"), "source-free draft shell uses its current planned order");
		if (dialog) { button(*dialog, "packageGroupsApply")->click(); }
		filter->setText("MENU"); ok &= expect(wait([&]() { return shows("MENU"); }), "new WAD group edit refreshes the shell browser");
		shell.findChild<QToolButton*>("packageUndo")->click(); filter->setText("TITLEMAP");
		ok &= expect(shows("TITLEMAP"), "new WAD shell undo restores the complete group label");
	}
	QPointer<PackageWadGroupsDialog> closing = new PackageWadGroupsDialog(original); closing->show();
	QTimer::singleShot(0, closing, &PackageWadGroupsDialog::reject); ok &= expect(wait([&]() { return closing.isNull(); }), "close cancels and joins inspection before destruction");
	ok &= expect(get(source) == sourceBytes, "UI group edits preserve source bytes");
	return ok ? 0 : 1;
}
