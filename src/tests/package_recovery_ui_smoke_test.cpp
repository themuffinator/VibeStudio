#include "app/application_shell.h"
#include "app/package_recovery_dialog.h"
#include "app/package_recovery_writer.h"
#include "app/studio_theme.h"
#include "core/package_draft.h"
#include "package_summary_test_fixture.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFont>
#include <QFile>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QUuid>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; }
	return value;
}
bool waitFor(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start(); QCoreApplication::processEvents();
	while (!ready() && timer.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	QCoreApplication::processEvents(); return ready();
}
class ExpandedTranslator final : public QTranslator {
public:
	bool expanded = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		const QByteArray text(source);
		return expanded && QByteArray(context) == "PackageRecoveryDialog"
			&& (text == "Restore to New Draft…" || text == "Discard Selected Copy…" || text == "Checkpoint interval (seconds)"
				|| text == "Recovery storage limit (MiB)" || text == "Maximum recovery copies")
			? QStringLiteral("[%1 — expanded translated label]").arg(QString::fromUtf8(source)) : QString();
	}
};
}

int main(int argc, char** argv)
{
	// Invoke Qt document actions and widget properties. No native input or capture.
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
	const QDir root(temporary.path()); bool ok = true; QString error;
	StudioSettings::setOverrideFilePath(root.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings; settings.setPackageRecoveryIntervalSeconds(600);
	ok &= expect(settings.packageRecoveryMaximumMiB() == 8192 && settings.packageRecoveryMaximumCopies() == 32, "storage preference defaults");
	settings.setPackageRecoveryMaximumMiB(-1); settings.setPackageRecoveryMaximumCopies(1000);
	ok &= expect(settings.packageRecoveryMaximumMiB() == 128 && settings.packageRecoveryMaximumCopies() == 128, "storage preference bounds");
	settings.setPackageRecoveryMaximumMiB(8192); settings.setPackageRecoveryMaximumCopies(32);
	ExpandedTranslator translator; app.installTranslator(&translator);
	{
		ApplicationShell shell; shell.resize(1600, 1000); shell.show(); app.processEvents();
		const auto action = [&](const char* name) { return shell.findChild<QAction*>(QString::fromLatin1(name)); };
		auto* create = action("package.new"); auto* recover = action("package.recover"); auto* close = action("package.close");
		auto* save = action("package.saveDraft"); auto* folder = action("package.createDirectory");
		auto* status = shell.findChild<QLabel*>(QStringLiteral("packageCheckpointStatus"));
		PackageRecoveryWriter* writer = nullptr;
		for (auto* child : shell.children()) { if (auto* found = dynamic_cast<PackageRecoveryWriter*>(child)) { writer = found; } }
		if (!expect(create && recover && close && save && folder && status && writer, "package recovery controls exist")) { return 1; }
		const auto newPackage = [&]() {
			bool chosen = false;
			QTimer::singleShot(0, [&]() { if (auto* picker = qobject_cast<QInputDialog*>(app.activeModalWidget())) { chosen = true; picker->accept(); } });
			create->trigger(); ok &= expect(chosen, "choose a new package format");
		};
		newPackage(); shell.checkpointPackageDocument();
		ok &= expect(waitFor([&] { return !writer->busy(); }), "background checkpoint completes");
		auto copies = listPackageRecoveries(packageRecoveryDirectory());
		if (!expect(copies.records.size() == 1 && copies.records.first().readable(), "new empty dirty package is checkpointed")) { return 1; }
		const QByteArray firstDigest = copies.records.first().manifestSha256;
		ok &= expect(status->text().contains("saved"), "successful checkpoint is visible");
		for (const int scale : {100, 200}) {
			translator.expanded = scale == 200;
			applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
			app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			recover->trigger();
			auto* dialog = dynamic_cast<PackageRecoveryDialog*>(shell.findChild<QDialog*>(QStringLiteral("packageRecoveryDialog")));
			if (!expect(dialog && waitFor([&] { return !dialog->busy(); }), "recovery chooser loads")) { return 1; }
			auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("packageRecoveryRecords"));
			auto* enabled = dialog->findChild<QCheckBox*>(QStringLiteral("packageRecoveryEnabled"));
			auto* interval = dialog->findChild<QSpinBox*>(QStringLiteral("packageRecoveryInterval"));
			auto* limit = dialog->findChild<QSpinBox*>(QStringLiteral("packageRecoveryMaximumMiB"));
			auto* count = dialog->findChild<QSpinBox*>(QStringLiteral("packageRecoveryMaximumCopies"));
			auto* usage = dialog->findChild<QLabel*>(QStringLiteral("packageRecoveryUsage"));
			ok &= expect(limit && count && usage && !usage->text().isEmpty() && !limit->accessibleName().isEmpty()
				&& !count->accessibleName().isEmpty(), "storage controls expose usage and accessible preference names");
			auto* restore = dialog->findChild<QPushButton*>(QStringLiteral("packageRecoveryRestore"));
			ok &= expect(table && table->rowCount() == 1 && enabled && interval && restore && restore->isEnabled(), "chooser exposes metadata, restore and preferences");
			ok &= expect(dialog->layoutDirection() == app.layoutDirection() && table->focusPolicy() != Qt::NoFocus
				&& !table->accessibleName().isEmpty() && !interval->accessibleName().isEmpty() && !restore->accessibleName().isEmpty(), "RTL and accessible keyboard targets");
			const QString captures = qEnvironmentVariable("VIBESTUDIO_PACKAGE_UI_CAPTURES");
			const auto render = [&](const QString& suffix) {
				auto* dismiss = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close);
				ok &= expect(dismiss && dismiss->isVisible() && dismiss->focusPolicy() != Qt::NoFocus
					&& dialog->rect().contains(QRect(dismiss->mapTo(dialog, QPoint()), dismiss->size())), "recovery Close remains visible and reachable before and after scrolling");
				if (captures.isEmpty()) { return true; }
				dialog->repaint(); app.processEvents();
				QDir().mkpath(captures); QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
				return image.save(QDir(captures).filePath(QStringLiteral("package-recovery-%1-%2.png").arg(scale).arg(suffix)));
			};
			app.processEvents(); ok &= expect(render(QStringLiteral("top")), "render recovery metadata and preferences");
			if (auto* scroll = dialog->findChild<QScrollArea*>()) { scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); app.processEvents(); }
			ok &= expect(render(QStringLiteral("actions")), "render expanded recovery actions");
			if (scale == 200) { enabled->setChecked(false); interval->setValue(10); limit->setValue(4096); count->setValue(16); }
			dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); app.processEvents();
		}
		ok &= expect(!settings.packageRecoveryEnabled() && settings.packageRecoveryIntervalSeconds() == 10, "recovery preferences persist");
		ok &= expect(settings.packageRecoveryMaximumMiB() == 4096 && settings.packageRecoveryMaximumCopies() == 16, "storage preferences persist");
		QTimer::singleShot(0, [&]() { if (auto* picker = qobject_cast<QInputDialog*>(app.activeModalWidget())) { picker->setTextValue("textures"); picker->accept(); } });
		folder->trigger(); shell.checkpointPackageDocument();
		ok &= expect(!writer->busy() && listPackageRecoveries(packageRecoveryDirectory()).records.first().manifestSha256 == firstDigest, "disabled recovery preserves old copy without checkpointing new edits");
		settings.setPackageRecoveryEnabled(true); settings.setPackageRecoveryIntervalSeconds(600); shell.checkpointPackageDocument();
		ok &= expect(waitFor([&] { return !writer->busy(); }), "re-enabled checkpoint completes");
		bool cancelPrompt = false;
		QTimer::singleShot(0, [&]() { if (auto* box = qobject_cast<QMessageBox*>(app.activeModalWidget())) { cancelPrompt = true; box->done(QMessageBox::Cancel); } });
		close->trigger();
		ok &= expect(cancelPrompt && save->isEnabled() && !listPackageRecoveries(packageRecoveryDirectory()).records.isEmpty(), "cancelled close keeps live package and checkpoint");
		const QString saved = root.filePath(QStringLiteral("saved.vibepackage"));
		QTimer::singleShot(0, [&]() { if (auto* picker = qobject_cast<QFileDialog*>(app.activeModalWidget())) { picker->selectFile(saved); QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection); } });
		save->trigger();
		ok &= expect(waitFor([&] { return !writer->busy(); }) && listPackageRecoveries(packageRecoveryDirectory()).records.isEmpty(), "successful draft save retires its checkpoint");
		const auto damaged = root.filePath(QStringLiteral("damaged.zip"));
		PackageArchive damagedArchive; PackageStagingModel crash;
		ok &= tests::summaryZip(damaged, {{"bad.bin", 2}, {"keep.bin", 1}});
		ok &= expect(damagedArchive.load(damaged, &error) && crash.loadBaseArchive(damagedArchive, &error)
			&& crash.deleteEntry(QStringLiteral("bad.bin"), &error) && crash.addBytes("recovered", QStringLiteral("kept.txt")),
			"prepare a repaired damaged package for recovery", error);
		crash.addBytes("redo payload", QStringLiteral("redo.txt")); crash.undo();
		const QString crashId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		auto session = PackageRecoverySession::acquire(packageRecoveryDirectory(), crashId, &error);
		if (!expect(bool(session), "crash fixture session", error)) { return 1; }
		const auto checkpoint = session->checkpoint(crash, QStringLiteral("Recovered fixture")); session.reset();
		if (!expect(checkpoint.succeeded(), "crash fixture checkpoint", checkpoint.error)) { return 1; }
		recover->trigger();
		auto* dialog = dynamic_cast<PackageRecoveryDialog*>(shell.findChild<QDialog*>(QStringLiteral("packageRecoveryDialog")));
		if (!expect(dialog && waitFor([&] { return !dialog->busy(); }), "show crashed copy")) { return 1; }
		auto* damagedTable = dialog->findChild<QTableWidget*>(QStringLiteral("packageRecoveryRecords"));
		auto* damagedStatus = damagedTable && damagedTable->rowCount() == 1 ? damagedTable->item(0, 3) : nullptr;
		ok &= expect(damagedStatus && damagedStatus->text() == QStringLiteral("Unavailable original content")
			&& damagedStatus->data(Qt::AccessibleTextRole).toString() == damagedStatus->text()
			&& damagedStatus->toolTip() == damagedStatus->text(), "recovery list exposes missing history in visible and accessible status");
		auto* damagedDetails = dialog->findChild<QPlainTextEdit*>(QStringLiteral("packageRecoveryDetails"));
		ok &= expect(damagedDetails && damagedDetails->toPlainText().contains(QStringLiteral("Unavailable original entries: 1")),
			"recovery review discloses historical data loss before restore");
		for (const int scale : {100, 200}) {
			applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scale));
			dialog->setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			dialog->resize(1300, 1000); app.processEvents();
			if (damagedStatus) {
				damagedTable->scrollToItem(damagedStatus); app.processEvents();
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
				if (!captures.isEmpty()) {
					QImage image(damagedTable->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); damagedTable->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-recovery-damaged-list-%1.png").arg(scale))), "render visible historical-content status");
				}
			}
			if (damagedDetails) {
				damagedDetails->verticalScrollBar()->setValue(damagedDetails->verticalScrollBar()->maximum()); app.processEvents();
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
				if (!captures.isEmpty()) {
					QImage image(damagedDetails->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); damagedDetails->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-recovery-damaged-%1.png").arg(scale))), "render readable historical-content warning");
				}
			}
		}
		const QString restored = root.filePath(QStringLiteral("recovered.vibepackage")); bool destinationChosen = false;
		QTimer::singleShot(0, [&]() {
			if (auto* picker = qobject_cast<QFileDialog*>(app.activeModalWidget())) { destinationChosen = true; picker->selectFile(restored); QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection); }
		});
		dialog->findChild<QPushButton*>(QStringLiteral("packageRecoveryRestore"))->click();
		PackageStagingModel output;
		ok &= expect(destinationChosen && PackageDraft::load(restored, &output, &error) && output.canRedo() && action("package.redo")->isEnabled(), "chooser restores a new draft with usable history", error);
		ok &= expect(QFileInfo::exists(checkpoint.path), "restoring retains the original recovery copy");
		close->trigger();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); app.processEvents();
		recover->trigger(); dialog = dynamic_cast<PackageRecoveryDialog*>(shell.findChild<QDialog*>(QStringLiteral("packageRecoveryDialog")));
		if (!expect(dialog && waitFor([&] { return !dialog->busy(); }), "review restored copy before discard")) { return 1; }
		QTimer::singleShot(0, [&]() { if (auto* box = qobject_cast<QMessageBox*>(app.activeModalWidget())) { box->button(QMessageBox::Discard)->click(); } });
		dialog->findChild<QPushButton*>(QStringLiteral("packageRecoveryDiscard"))->click();
		ok &= expect(waitFor([&] { return !dialog->busy(); }) && listPackageRecoveries(packageRecoveryDirectory()).records.isEmpty(), "reviewed chooser discard refreshes the inventory");
		dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); app.processEvents();
		ok &= expect(output.redo(), "restored history remains usable after chooser discard");
		const QString incompleteId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		const QString incomplete = packageRecoveryPath(packageRecoveryDirectory(), incompleteId);
		QDir().mkpath(QDir(incomplete).filePath(QStringLiteral("objects")));
		QFile partial(QDir(incomplete).filePath(QStringLiteral("objects/.writing-AbC123")));
		ok &= partial.open(QIODevice::WriteOnly) && partial.write("partial") == 7; partial.close();
		recover->trigger(); dialog = dynamic_cast<PackageRecoveryDialog*>(shell.findChild<QDialog*>(QStringLiteral("packageRecoveryDialog")));
		if (!expect(dialog && waitFor([&] { return !dialog->busy(); }), "review an incomplete checkpoint")) { return 1; }
		ok &= expect(!dialog->findChild<QPushButton*>(QStringLiteral("packageRecoveryRestore"))->isEnabled()
			&& dialog->findChild<QPushButton*>(QStringLiteral("packageRecoveryDiscard"))->isEnabled(), "incomplete copy permits reviewed cleanup, not restoration");
		QTimer::singleShot(0, [&]() { if (auto* box = qobject_cast<QMessageBox*>(app.activeModalWidget())) { box->button(QMessageBox::Discard)->click(); } });
		dialog->findChild<QPushButton*>(QStringLiteral("packageRecoveryDiscard"))->click();
		ok &= expect(waitFor([&] { return !dialog->busy(); }) && !QFileInfo::exists(incomplete), "chooser can discard incomplete storage");
		dialog->reject(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); app.processEvents();
		newPackage(); shell.checkpointPackageDocument();
		QTimer::singleShot(0, [&]() { if (auto* box = qobject_cast<QMessageBox*>(app.activeModalWidget())) { box->button(QMessageBox::Discard)->click(); } });
		close->trigger();
		ok &= expect(waitFor([&] { return !writer->busy(); }) && listPackageRecoveries(packageRecoveryDirectory()).records.isEmpty(), "discarding the live document retires in-flight recovery");
	}
	app.removeTranslator(&translator); StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
