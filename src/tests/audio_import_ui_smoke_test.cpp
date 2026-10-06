#include "app/application_shell.h"
#include "app/audio_editor_dialog.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
QByteArray readFile(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool waitUntil(const std::function<bool()>& ready)
{
	QEventLoop loop;
	QTimer poll, timeout;
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
		if (ready()) {
			loop.quit();
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(15000);
	if (!ready()) {
		loop.exec();
	}
	return ready();
}
bool finish(AudioEditorDialog* editor)
{
	return expect(waitUntil([&] { return !editor->isBusy(); }),
	              "Audio import worker finishes while GUI events run");
}
bool finishRecovery(AudioEditorDialog* editor)
{
	return expect(waitUntil([&] { return !editor->recoveryBusy(); }),
	              "Recovery worker finishes while GUI events run");
}
QListWidgetItem* audioEntry(ApplicationShell* shell, const QString& path, bool)
{
	auto* list = shell->findChild<QListWidget*>(QStringLiteral("audioEntries"));
	QListWidgetItem* found = nullptr;
	waitUntil([&] {
		for (int i = 0; list && i < list->count(); ++i) {
			if (list->item(i)->data(Qt::UserRole).toString() == path) {
				found = list->item(i);
				return true;
			}
		}
		return false;
	});
	return found;
}
} // namespace
int main(int argc, char** argv)
{
	// Direct Qt calls only: no keyboard/mouse events or OS screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-import-ui-XXXXXX")));
	if (!temporary.isValid()) {
		return 2;
	}
	// QFileDialog has its own Qt settings in addition to StudioSettings.
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	qputenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT",
	        QDir(temporary.path()).filePath(QStringLiteral("recovery")).toUtf8());
	QString error;
	bool ok = true;
	const QDir compressedFixtures(qEnvironmentVariable("VIBESTUDIO_AUDIO_FIXTURES"));
	{
		auto* browser = new ApplicationShell;
		browser->show();
		browser->openPathFromCommandLine(compressedFixtures.absolutePath());
		browser->findChild<QAction*>(QStringLiteral("shell.mode.audio"))->trigger();
		auto* entry = audioEntry(browser, QStringLiteral("flac-stereo24.flac"), true);
		ok &= expect(entry != nullptr, "Audio browser lists compressed sound fixtures");
		if (entry) {
			browser->findChild<QListWidget*>(QStringLiteral("audioEntries"))->setCurrentItem(entry);
			// Accept the browser's suggested filename in the isolated test folder.
			const QString output = QDir(temporary.path()).filePath(QStringLiteral("flac-stereo24.wav"));
			QTimer picker;
			int attempts = 0;
			bool selected = false;
			QObject::connect(&picker, &QTimer::timeout, browser, [browser, output, &selected, &attempts] {
				if (auto* dialog = browser->findChild<QFileDialog*>()) {
					if (!selected) {
						dialog->selectFile(output);
						selected = true;
					} else if (++attempts < 100) {
						QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
					} else {
						dialog->reject();
					}
				}
			});
			picker.start(100);
			browser->findChild<QPushButton*>(QStringLiteral("audioBrowserExportWav"))->click();
			picker.stop();
			QPointer<QProgressDialog> progress =
			    browser->findChild<QProgressDialog*>(QStringLiteral("audioBrowserExportProgress"));
			ok &= expect(progress && !progress->accessibleName().isEmpty() &&
			                 !progress->accessibleDescription().isEmpty(),
			             "compressed browser export exposes accessible asynchronous progress");
			QEventLoop loop;
			QTimer poll, timeout;
			timeout.setSingleShot(true);
			QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
				if (!progress || !progress->isVisible()) {
					loop.quit();
				}
			});
			QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
			poll.start(10);
			timeout.start(15000);
			if (progress) {
				loop.exec();
			}
			const auto expected =
			    decodeAudioClip(QStringLiteral("flac-stereo24.flac"),
			                    readFile(compressedFixtures.filePath(QStringLiteral("flac-stereo24.flac"))));
			if ((progress && progress->isVisible()) || readFile(output) != encodeAudioWav(expected.clip)) {
				std::cerr << "Browser export: visible=" << bool(progress && progress->isVisible())
				          << ", output=" << readFile(output).size()
				          << ", expected=" << encodeAudioWav(expected.clip).size()
				          << ", status=" << browser->statusBar()->currentMessage().toStdString() << '\n';
			}
			ok &= expect((!progress || !progress->isVisible()) &&
			                 readFile(output) == encodeAudioWav(expected.clip),
			             "browser compressed export completes with shared PCM16 output");
		}
		delete browser;
	}
	for (const auto& name : {QStringLiteral("flac-stereo24.flac"), QStringLiteral("mp3-vbr.mp3"),
	                         QStringLiteral("vorbis-6ch.ogg")}) {
		const auto sourcePath = compressedFixtures.filePath(name);
		const auto original = readFile(sourcePath);
		const auto decodedImport = decodeAudioClip(name, original);
		auto* imported = new AudioEditorDialog;
		ok &= expect(imported->openFile(sourcePath), "start compressed editor import");
		ok &= finish(imported);
		ok &= expect(decodedImport.succeeded() && imported->clip().samples == decodedImport.clip.samples &&
		                 imported->findChild<QLabel*>(QStringLiteral("audioEditorStatus"))
		                     ->text()
		                     .contains(QStringLiteral("tags")),
		             "compressed editor imports all samples and exposes omitted metadata");
		const auto nativePath = QDir(temporary.path()).filePath(name + QStringLiteral(".vsaudio"));
		ok &= expect(imported->saveProjectTo(nativePath), "save compressed import as native audio project");
		ok &= finish(imported);
		AudioProject savedImport;
		ok &= expect(readAudioProject(nativePath, &savedImport, nullptr, &error) &&
		                 savedImport.clip.samples == decodedImport.clip.samples &&
		                 !savedImport.metadata.value(QStringLiteral("importWarnings")).toArray().isEmpty(),
		             "native save preserves compressed sample precision and import warning");
		ok &= expect(imported->openFile(sourcePath), "begin cancellable compressed reload");
		imported->cancelWork();
		ok &= finish(imported);
		ok &= expect(imported->clip().samples == savedImport.clip.samples &&
		                 imported->projectPath() == nativePath && !imported->hasChanges(),
		             "cancelled compressed import retains saved document and revision");
		ok &=
		    expect(imported->loadSource(QStringLiteral("broken.ogg"), {},
		                                [original](QString*) { return original.first(original.size() / 2); }),
		           "begin malformed compressed import");
		ok &= finish(imported);
		ok &= expect(imported->clip().samples == savedImport.clip.samples &&
		                 imported->projectPath() == nativePath && readFile(sourcePath) == original,
		             "failed compressed import retains current work and source file");
		ok &= finishRecovery(imported);
		delete imported;
	}
	StudioSettings::setOverrideFilePath({});
	return ok ? 0 : 1;
}
