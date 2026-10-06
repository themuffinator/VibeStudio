#include "app/package_publication_dialog.h"
#include "app/package_recovery_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/package_publication_test_helpers.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::publication_test;
namespace {
bool expect(bool value, const char* label) { if (!value) { std::cerr << label << '\n'; } return value; }
bool wait(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	do { QApplication::processEvents(); if (ready()) { return true; } QThread::msleep(5); } while (timer.elapsed() < 15000);
	return false;
}
class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		return QByteArray(context) == "PackagePublicationDialog" ? QStringLiteral("[%1 — expanded label]").arg(QString::fromUtf8(source)) : QString();
	}
};
QPushButton* button(QWidget& parent, const char* name) { return parent.findChild<QPushButton*>(QString::fromLatin1(name)); }
bool finish(PackagePublicationDialog& dialog)
{
	bool confirmed = false;
	QTimer::singleShot(0, [&]() {
		if (auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			if (auto* yes = question->button(QMessageBox::Yes)) { confirmed = true; yes->click(); }
		}
	});
	button(dialog, "packagePublicationFinish")->click(); return confirmed && wait([&]() { return !dialog.busy(); });
}
}
int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen"); QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const QDir root(temporary.path()); bool ok = true; ExpandedTranslator translator;
	StudioSettings::setOverrideFilePath(root.filePath("settings.ini"));
	QTranslator english;
	if (english.load(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../i18n/vibestudio_en.qm")))) { app.installTranslator(&english); }
	for (int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastLight, UiDensity::Standard, scale));
		if (scale == 200) { app.installTranslator(&translator); }
		const auto folder = root.filePath(QStringLiteral("output-%1").arg(scale)); const auto info = interrupt(folder);
		if (!expect(info.metadataValid(), "interrupted UI fixture")) { return 1; }
		PackagePublicationDialog dialog({folder}); dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		QString opened; int completed = 0; PackageRecoveryReport last;
		dialog.openOutput = [&](const QString& path) { opened = path; };
		dialog.recoveryFinished = [&](const auto& report) { ++completed; last = report; };
		if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); } dialog.show();
		auto* verify = button(dialog, "packagePublicationVerify"), *apply = button(dialog, "packagePublicationFinish");
		auto* table = dialog.findChild<QTableWidget*>("packagePublicationJournals");
		auto* details = dialog.findChild<QPlainTextEdit*>("packagePublicationDetails");
		auto* status = dialog.findChild<QLabel*>("packagePublicationStatus");
		ok &= expect(verify && apply && table && details && status && wait([&]() { return !dialog.busy() && table->rowCount() == 1; }), "bounded metadata inventory loads asynchronously");
		if (!ok) { return 1; }
		ok &= expect(!apply->isEnabled() && verify->isEnabled() && !button(dialog, "packagePublicationOpen")->isEnabled(), "metadata alone cannot finish or open a save");
		verify->click(); ok &= expect(wait([&]() { return !dialog.busy(); }) && apply->isEnabled(), "content verification enables reviewed finish");
		table->setCurrentCell(0, 1);
		ok &= expect(apply->isEnabled(), "changing columns preserves the verified save selection");
		ok &= expect(table->horizontalScrollBar()->maximum() == 0, "long folders do not push output and status columns out of view");
		ok &= expect(table->item(0, 2)->data(Qt::AccessibleTextRole).toString().contains(table->item(0, 2)->text())
			&& !details->accessibleName().isEmpty() && details->isReadOnly() && status->wordWrap(), "verification updates accessible status and readable details");
		auto* close = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close); auto* scroll = dialog.findChild<QScrollArea*>();
		ok &= expect(close->isVisible() && dialog.rect().contains(QRect(close->mapTo(&dialog, QPoint()), close->size())) && close->focusPolicy() != Qt::NoFocus, "Close stays visible and focusable at each scale");
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures); scroll->verticalScrollBar()->setValue(0); QApplication::processEvents(); dialog.repaint();
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			ok &= image.save(QDir(captures).filePath(QStringLiteral("publication-%1-top.png").arg(scale)));
		}
		auto* refresh = button(dialog, "packagePublicationRefresh");
		ok &= expect(wait([&]() {
			scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
			return scroll->viewport()->rect().contains(QRect(refresh->mapTo(scroll->viewport(), QPoint()), refresh->size()));
		}), "Refresh remains reachable at the end of the scroll area");
		QApplication::processEvents();
		ok &= expect(scroll->viewport()->rect().contains(QRect(apply->mapTo(scroll->viewport(), QPoint()), apply->size()))
			&& apply->focusPolicy() != Qt::NoFocus && !apply->accessibleName().isEmpty(), "scaled finish action is reachable and focusable");
		if (!captures.isEmpty()) {
			dialog.repaint(); QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			ok &= image.save(QDir(captures).filePath(QStringLiteral("publication-%1-actions.png").arg(scale)));
		}
		ok &= write(info.journalPath, read(info.journalPath) + '\n');
		ok &= expect(finish(dialog) && completed == 1 && !last.finished && !last.error.isEmpty()
			&& !apply->isEnabled() && QFileInfo::exists(info.journalPath) && !QFileInfo::exists(info.backupPath), "changed journal is refused after the user confirms reviewed finish");
		dialog.reload(); ok &= wait([&]() { return !dialog.busy(); }); verify->click(); ok &= wait([&]() { return !dialog.busy(); });
		ok &= expect(finish(dialog) && completed == 2 && last.finished && !apply->isEnabled() && !verify->isEnabled()
			&& read(info.backupPath) == "original" && !QFileInfo::exists(info.journalPath), "refreshed review completes backup and cleanup");
		button(dialog, "packagePublicationOpen")->click();
		ok &= expect(opened == info.destinationPath && !dialog.isVisible(), "opening current output closes chooser and delegates to the document workflow");
		app.removeTranslator(&translator);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	{
		const QString folder = root.filePath("external"), backup = root.filePath("external-backup.pak"); const auto info = interrupt(folder, "old", "new", backup);
		PackagePublicationDialog dialog({folder}); dialog.setAttribute(Qt::WA_DeleteOnClose, false); dialog.show();
		auto* verify = button(dialog, "packagePublicationVerify"), *apply = button(dialog, "packagePublicationFinish"), *choose = button(dialog, "packagePublicationBackup");
		ok &= wait([&]() { return !dialog.busy() && verify->isEnabled(); }); verify->click(); ok &= wait([&]() { return !dialog.busy(); });
		ok &= expect(choose->isVisible() && !apply->isEnabled(), "external backup needs explicit path selection");
		for (const QString& path : {root.filePath("wrong.pak"), backup}) {
			bool selected = false;
			QTimer::singleShot(0, [&]() {
				if (auto* picker = qobject_cast<QFileDialog*>(app.activeModalWidget())) { selected = true; picker->selectFile(path); QMetaObject::invokeMethod(picker, "accept", Qt::QueuedConnection); }
			}); choose->click(); ok &= selected && wait([&]() { return !dialog.busy(); });
			ok &= expect(apply->isEnabled() == (path == backup), "only the exact recorded external backup authorizes finish");
		}
		ok &= expect(finish(dialog) && read(backup) == "old", "explicit external backup finish preserves original bytes");
		dialog.reload(); dialog.reject(); ok &= expect(wait([&]() { return !dialog.busy() && !dialog.isVisible(); }), "close cancels and joins an active folder scan");
	}
	{
		const QString folder = root.filePath("from-recovery"); const auto info = interrupt(folder); StudioSettings().rememberPackagePublicationDirectory(folder);
		PackageRecoveryDialog recovery(root.filePath("recoveries")); recovery.setAttribute(Qt::WA_DeleteOnClose, false); recovery.show();
		ok &= wait([&]() { return !recovery.busy(); });
		auto* interrupted = button(recovery, "packageInterruptedSaves");
		ok &= expect(interrupted && !interrupted->accessibleName().isEmpty(), "package recovery links interrupted saves");
		QString opened; recovery.openPublicationOutput = [&](const auto& path) { opened = path; };
		if (interrupted) {
			interrupted->click(); auto* child = dynamic_cast<PackagePublicationDialog*>(recovery.findChild<QDialog*>("packagePublicationDialog"));
			ok &= expect(child && wait([&]() { return !child->busy() && button(*child, "packagePublicationVerify")->isEnabled(); }), "remembered save folders survive reopening recovery manager");
			if (child) {
				button(*child, "packagePublicationVerify")->click(); ok &= wait([&]() { return !child->busy(); }); button(*child, "packagePublicationOpen")->click();
				ok &= expect(opened == info.destinationPath && !recovery.isVisible(), "normal output opening leaves both modal recovery views");
			}
		}
	}
	std::cout << (ok ? "Interrupted save UI smoke passed\n" : "Interrupted save UI smoke failed\n"); return ok ? 0 : 1;
}
