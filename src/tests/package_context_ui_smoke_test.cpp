#include "package_entry_test_helpers.h"
#include "package_legacy_test_helpers.h"
#include "app/application_shell.h"
#include "app/ui_primitives.h"
#include "core/package_draft.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; } return value;
}
bool until(const std::function<bool()>& done)
{
	QElapsedTimer timer; timer.start();
	while (!done() && timer.elapsed() < 15000) { QApplication::processEvents(QEventLoop::ExcludeUserInputEvents); QThread::msleep(1); }
	return done();
}
QString healthText(QListWidget* list)
{
	QStringList lines;
	for (int i = 0; list && i < list->count(); ++i) { lines << list->item(i)->text(); }
	return lines.join('\n');
}
}

int main(int argc, char** argv)
{
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
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings; settings.setCurrentProjectPath(temporary.path());
	settings.setPackageRecoveryEnabled(false); settings.setLevelRecoveryEnabled(false);
	bool ok = true; QString error;
	QImage image(2, 2, QImage::Format_RGB32); image.fill(Qt::blue);
	QByteArray pixels; QBuffer buffer(&pixels); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
	PackageStagingModel initial; initial.createEmpty(PackageArchiveFormat::Pk3);
	const QString texture = QStringLiteral("textures/studio/wall.png");
	ok &= expect(initial.addBytes(pixels, texture, &error) && initial.addBytes("readme", QStringLiteral("readme.txt"), &error), "prepare synthetic package context");
	PackageWriteRequest write; write.destinationPath = temporary.filePath(QStringLiteral("source.pk3"));
	ok &= expect(initial.writeArchive(write).succeeded(), "write independent package source");
	PackageArchive archive; ok &= expect(archive.load(write.destinationPath, &error), "open context source");
	PackageStagingModel expected; expected.loadBaseArchive(archive, &error); expected.deleteEntry(texture, &error);
	const int baseCount = archive.summary().entryCount, deletedCount = packagePlannedArchive(expected).summary().entryCount;
	if (!expect(ok && baseCount != deletedCount, "fixture distinguishes original and planned entry counts")) { return 1; }
	const QString blockedDraft = temporary.filePath(QStringLiteral("blocked.vibepackage"));
	ok &= expect(tests::saveLegacyDeepPackageDraft(blockedDraft, &error), "prepare an admitted draft with a refused planned projection");
	{
		ApplicationShell shell; shell.resize(1440, 900); shell.show();
		shell.openPathFromCommandLine(write.destinationPath);
		LevelMapCreateRequest request; request.game = QStringLiteral("quake3");
		request.wallTexture = request.floorTexture = request.ceilingTexture = QStringLiteral("studio/wall");
		ok &= expect(shell.createLevelDocument(request, &error), "create a map referencing the mounted texture");
		auto* health = shell.findChild<QListWidget*>(QStringLiteral("levelMapHealth"));
		DetailDrawer* workspace = nullptr;
		for (auto* widget : shell.findChildren<QWidget*>()) {
			auto* drawer = dynamic_cast<DetailDrawer*>(widget);
			if (drawer && drawer->accessibleName() == QStringLiteral("Workspace dashboard details")) { workspace = drawer; }
		}
		auto* filter = shell.findChild<QLineEdit*>(QStringLiteral("packageFilter"));
		auto entries = tests::PackageRows(shell.findChild<PackageEntryView*>(QStringLiteral("packageEntries")));
		auto* remove = shell.findChild<QAction*>(QStringLiteral("package.stageDelete"));
		auto* undo = shell.findChild<QAbstractButton*>(QStringLiteral("packageUndo"));
		auto* redo = shell.findChild<QAbstractButton*>(QStringLiteral("packageRedo"));
		if (!expect(health && workspace && filter && entries && remove && undo && redo, "real shell context controls exist")) { return 1; }
		workspace->showSection(QStringLiteral("packages"));
		const auto countIs = [&](int count) { return workspace->currentSectionText().contains(QStringLiteral("Entries: %1\n").arg(count)); };
		ok &= expect(until([&] { return healthText(health).contains("All ") && healthText(health).contains("resolve against"); }), "initial level health resolves the texture");
		ok &= expect(countIs(baseCount), "workspace initially reports the source entry count");
		filter->setText(texture);
		bool selected = false;
		for (int i = 0; i < entries->count(); ++i) {
			if (entries->item(i)->data(Qt::UserRole).toString() == texture) { entries->setCurrentItem(entries->item(i)); selected = true; break; }
		}
		ok &= expect(selected && remove->isEnabled(), "select exact texture for staged deletion");
		QTimer::singleShot(0, [&] { if (auto* question = qobject_cast<QMessageBox*>(app.activeModalWidget())) { question->done(QMessageBox::Yes); } });
		remove->trigger();
		ok &= expect(until([&] { return healthText(health).contains("MISSING TEXTURES"); }), "staged deletion refreshes level texture health without reopening the map");
		ok &= expect(countIs(deletedCount), "workspace summary follows staged deletion without resetting the selected section");
		undo->click();
		ok &= expect(until([&] { return !healthText(health).contains("MISSING TEXTURES") && healthText(health).contains("resolve against"); }) && countIs(baseCount), "Undo restores both dependent views");
		redo->click();
		ok &= expect(until([&] { return healthText(health).contains("MISSING TEXTURES"); }) && countIs(deletedCount), "Redo refreshes both dependent views");

		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
			for (auto* tabs : shell.findChildren<QTabWidget*>()) {
				if (tabs->indexOf(health->parentWidget()) >= 0) { tabs->setCurrentWidget(health->parentWidget()); }
			}
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
			QImage capture(shell.size(), QImage::Format_ARGB32_Premultiplied); capture.fill(Qt::transparent); shell.render(&capture);
			ok &= expect(capture.save(QDir(captures).filePath(QStringLiteral("package-context-level-missing.png"))), "render the real staged texture health context");
		}
		QByteArray sourcePixels;
		ok &= expect(archive.readEntryBytes(texture, &sourcePixels, &error) && sourcePixels == pixels, "context checks and staged deletion preserve original archive bytes");
		undo->click();
		shell.openPathFromCommandLine(blockedDraft);
		workspace->showSection(QStringLiteral("packages"));
		ok &= expect(workspace->currentSectionText().contains("unavailable", Qt::CaseInsensitive)
			&& !workspace->currentSectionText().contains("Entries:"), "refused planned metadata never falls back to source counts");
		ok &= expect(until([&] { return healthText(health).contains("TEXTURE CHECK [Unavailable]"); })
			&& !healthText(health).contains("resolve against"), "refused planned texture health clears successful source results");
		// Destruction, rather than a synthetic close event, avoids dirty prompts.
	}
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
