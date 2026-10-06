#include "app/application_shell.h"
#include "app/audio_editor_dialog.h"
#include "app/audio_recovery.h"
#include "app/audio_recovery_dialog.h"
#include "app/audio_session_dialog.h"
#include "app/studio_layout.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_playback.h"
#include "tests/fake_audio_stream.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLockFile>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *text)
{
	if (!value) {
		std::cerr << text << '\n';
	}
	return value;
}
bool waitFor(const std::function<bool()> &done)
{
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < 30000) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	QCoreApplication::processEvents();
	return done();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray hash(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
AudioSession fixture()
{
	AudioProject project{{1, 48000, QVector<float>(48000, 0.125f)}, 0, 48000, QStringLiteral("Test tone"), {}, {}};
	auto session = importAudioSessionSource({}, project).session;
	session.sources[0].id = QStringLiteral("11111111-1111-4111-8111-111111111111");
	session.tracks[0].id = QStringLiteral("22222222-2222-4222-8222-222222222222");
	session.tracks[0].regions[0].id = QStringLiteral("33333333-3333-4333-8333-333333333333");
	session.tracks[0].regions[0].sourceId = session.sources[0].id;
	session.name = QStringLiteral("Interrupted arrangement");
	session.masterEffects = {makeAudioEffect("lookahead-limiter", session.sampleRate)};
	session.masterEffects[0].id = QStringLiteral("44444444-4444-4444-8444-444444444444");
	return session;
}
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication application(argc, argv);
	application.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
	application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--crash-fixture") {
		const QString directory = QString::fromLocal8Bit(argv[2]), id = QString::fromLocal8Bit(argv[3]);
		AudioRecoveryWriter writer(directory, nullptr, AudioRecoveryKind::Session);
		writer.finished = [](const QString &path, const QString &error) {
			if (path.isEmpty() || !error.isEmpty()) {
				QCoreApplication::exit(1);
			} else {
				std::cout << "checkpoint-ready\n" << std::flush;
			}
		};
		writer.checkpoint(id, 1, AudioSessionRecovery{fixture(), {}, {}});
		return application.exec(); // Parent terminates only this disposable fixture process.
	}
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root)) {
		return 1;
	}
	QTemporaryDir temporary(QDir(root).filePath("session-recovery-ui-XXXXXX"));
	if (!temporary.isValid()) {
		return 1;
	}
	const auto directory = temporary.filePath("copies");
	qputenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT", directory.toUtf8());
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	StudioSettings settings;
	settings.setAudioRecoveryEnabled(true);
	settings.setAudioRecoveryNotifyAtStartup(true);
	settings.sync();
	const auto session = fixture();
	const auto original = encodeAudioSession(session);
	bool ok = true;
	QString path;
	{
		AudioSessionDialog dialog(nullptr, fakeAudioStreamFactory());
		dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		ok &= expect(dialog.importSource(session.sources[0].audio) && waitFor([&] { return !dialog.isBusy(); }),
		             "import prepares session");
		path = dialog.recoveryPath();
		ok &= expect(dialog.recoveryBusy() && !QFileInfo::exists(path), "edit first queues a coalesced checkpoint");
		// Destruction before the timer fires must still preserve the accepted edit.
	}
	ok &= expect(QFileInfo::exists(path) && !QFileInfo::exists(path + ".active"),
	             "destruction flushes latest queued edit and releases lease");
	const auto digest = hash(read(path));
	const auto nativePath = temporary.filePath("original.vssession");
	ok &= expect(writeAudioSession(session, {nativePath, false, false, {}, {}}).succeeded, "save original session");
	const auto reviewId = uuid();
	const auto reviewPath = writeAudioSessionRecovery(session, nativePath, directory, reviewId);
	const auto reviewed = read(reviewPath), reviewDigest = hash(reviewed);
	{
		AudioSessionDialog dialog(nullptr, fakeAudioStreamFactory());
		dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		ok &= expect(dialog.restoreRecovery(reviewPath, QByteArray(32, 'x')) &&
		                 waitFor([&] { return !dialog.isBusy(); }) && dialog.session().tracks.isEmpty() &&
		                 !dialog.hasChanges(),
		             "stale restore preserves current clean session");
		ok &= expect(dialog.restoreRecovery(reviewPath, reviewDigest) && waitFor([&] { return !dialog.isBusy(); }) &&
		                 dialog.hasChanges() && dialog.sessionPath().isEmpty() &&
		                 encodeAudioSession(dialog.session()) == original,
		             "restoration creates unsaved editable session");
		ok &= expect(waitFor([&] { return !dialog.recoveryBusy(); }), "timer and background checkpoint finish");
		const auto own = dialog.recoveryPath();
		ok &= expect(QFileInfo::exists(own) && own != reviewPath, "restored draft owns separate recovery UUID");
		ok &= expect(dialog.saveSession(nativePath, true) && waitFor([&] { return !dialog.isBusy(); }) &&
		                 dialog.hasChanges() && read(nativePath) == original && read(reviewPath) == reviewed,
		             "restored save cannot overwrite original session");
		dialog.setRecoveryEnabled(false);
		AudioSessionEdit edit;
		edit.operation = "master";
		edit.gainDb = -6;
		const auto beforeOff = read(own);
		ok &= expect(dialog.applyEdit(edit, "Gain") && !dialog.recoveryBusy() && read(own) == beforeOff,
		             "disabled recovery retains existing checkpoint");
		dialog.setRecoveryEnabled(true);
		ok &= expect(waitFor([&] { return !dialog.recoveryBusy(); }), "re-enabling checkpoints current dirty revision");
		AudioSessionRecovery recovered;
		ok &= expect(readAudioSessionRecovery(own, hash(read(own)), &recovered) && recovered.session.masterGainDb == -6,
		             "current mix state reaches recovery");
		ok &= expect(dialog.saveSession(temporary.filePath("restored.vssession")) &&
		                 waitFor([&] { return !dialog.isBusy() && !dialog.recoveryBusy(); }) && !dialog.hasChanges() &&
		                 !QFileInfo::exists(own) && read(reviewPath) == reviewed,
		             "save retires own draft and preserves reviewed origin");
		const auto savedPath = dialog.sessionPath();
		dialog.undo();
		ok &= expect(waitFor([&] { return !dialog.recoveryBusy(); }) && dialog.hasChanges(),
		             "undo away from saved revision checkpoints");
		const auto undoCopy = dialog.recoveryPath();
		dialog.redo();
		ok &= expect(waitFor([&] { return !dialog.recoveryBusy(); }) && !dialog.hasChanges() &&
		                 !QFileInfo::exists(undoCopy) && dialog.sessionPath() == savedPath,
		             "redo to saved revision retires redundant recovery");
		dialog.close();
	}
	{
		AudioRecoveryWriter writer(directory, nullptr, AudioRecoveryKind::Session);
		const auto key = uuid();
		writer.checkpoint(key, 1, AudioSessionRecovery{session, {}, {}});
		auto newer = session;
		newer.masterGainDb = -9;
		writer.checkpoint(key, 2, AudioSessionRecovery{newer, {}, {}});
		writer.retire(key);
		ok &= expect(waitFor([&] { return !writer.busy(); }) &&
		                 !QFileInfo::exists(audioSessionRecoveryPath(directory, key)),
		             "retiring in-flight and pending writes cannot resurrect a draft");
		writer.checkpoint(key, 3, AudioSessionRecovery{newer, {}, {}});
		ok &= expect(!writer.busy() && !QFileInfo::exists(audioSessionRecoveryPath(directory, key)),
		             "retired IDs cannot be requeued");
		const auto latestKey = uuid();
		QLockFile inventoryLock(QDir(directory).filePath(".inventory.lock"));
		ok &= expect(inventoryLock.tryLock(), "reserve inventory for contention fixture");
		bool heartbeat = false;
		QTimer::singleShot(100, [&] {
			heartbeat = true;
			inventoryLock.unlock();
		});
		writer.checkpoint(latestKey, 4, AudioSessionRecovery{session, {}, {}});
		writer.checkpoint(latestKey, 5, AudioSessionRecovery{newer, {}, {}});
		newer.masterGainDb = -12;
		writer.checkpoint(latestKey, 6, AudioSessionRecovery{newer, {}, {}});
		ok &= expect(waitFor([&] { return !writer.busy(); }) && heartbeat,
		             "inventory contention waits on worker while UI remains responsive");
		const auto latestPath = audioSessionRecoveryPath(directory, latestKey);
		AudioSessionRecovery latest;
		ok &= expect(readAudioSessionRecovery(latestPath, hash(read(latestPath)), &latest) &&
		                 latest.session.masterGainDb == -12,
		             "replaceable pending snapshots retain the latest accepted revision");
		writer.retire(latestKey);
	}
	{
		AudioSessionDialog dialog(nullptr, fakeAudioStreamFactory());
		dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		ok &= expect(dialog.importSource(session.sources[0].audio) &&
		                 waitFor([&] { return !dialog.isBusy() && !dialog.recoveryBusy(); }),
		             "prepare own idle checkpoint");
		const auto own = dialog.recoveryPath();
		const auto ownBytes = read(own);
		QTimer::singleShot(0, [] {
			if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
				box->button(QMessageBox::Discard)->click();
			}
		});
		ok &= expect(dialog.restoreRecovery(own, hash(ownBytes)) &&
		                 waitFor([&] { return !dialog.isBusy() && !dialog.recoveryBusy(); }) &&
		                 dialog.recoveryPath() != own && read(own) == ownBytes,
		             "restoring own reviewed copy retains it under its original UUID");
		ok &= expect(dialog.saveSession(temporary.filePath("self-restored.vssession")) &&
		                 waitFor([&] { return !dialog.isBusy(); }) && read(own) == ownBytes,
		             "saving self-restored draft cannot retire reviewed origin");
	}
	{
		AudioSessionDialog dialog(nullptr, fakeAudioStreamFactory());
		dialog.setAttribute(Qt::WA_DeleteOnClose, false);
		ok &= expect(dialog.openSession(nativePath) && waitFor([&] { return !dialog.isBusy(); }),
		             "open saved session for save-before-restore guard");
		AudioSessionEdit edit;
		edit.operation = "master";
		edit.gainDb = -8;
		ok &= expect(dialog.applyEdit(edit, "Gain") && waitFor([&] { return !dialog.recoveryBusy(); }),
		             "checkpoint edited saved session");
		const auto own = dialog.recoveryPath();
		const auto ownBytes = read(own);
		QTimer::singleShot(0, [] {
			if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
				box->button(QMessageBox::Save)->click();
			}
		});
		ok &= expect(!dialog.restoreRecovery(own, hash(ownBytes)) &&
		                 waitFor([&] { return !dialog.isBusy() && dialog.hasChanges(); }) &&
		                 dialog.sessionPath().isEmpty() && read(own) == ownBytes,
		             "save-before-restore preserves reviewed session input and continues into draft");
		ok &= expect(dialog.saveSession(temporary.filePath("saved-before-restore.vssession")) &&
		                 waitFor([&] { return !dialog.isBusy(); }),
		             "save guard fixture draft");
	}
	{
		AudioEditorDialog editor(nullptr, std::make_unique<FakeAudioPlaybackBackend>());
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		ok &= expect(editor.createNew(48000, 1, 4) && waitFor([&] { return !editor.isBusy(); }) &&
		                 editor.saveProjectTo(temporary.filePath("waveform.vsaudio")) &&
		                 waitFor([&] { return !editor.isBusy(); }),
		             "prepare saved waveform guard fixture");
		editor.insertSilence(1);
		ok &= expect(waitFor([&] { return !editor.isBusy() && !editor.recoveryBusy(); }) && editor.hasChanges(),
		             "waveform edit checkpoints");
		const auto own = editor.recoveryPath();
		const auto ownBytes = read(own);
		QTimer::singleShot(0, [] {
			if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
				box->button(QMessageBox::Save)->click();
			}
		});
		ok &= expect(!editor.openProject(own, true, hash(ownBytes)) &&
		                 waitFor([&] { return !editor.isBusy() && editor.hasChanges(); }) &&
		                 editor.projectPath().isEmpty() && read(own) == ownBytes,
		             "waveform save-before-restore also retains reviewed origin");
		ok &= expect(editor.saveProjectTo(temporary.filePath("waveform-restored.vsaudio")) &&
		                 waitFor([&] { return !editor.isBusy(); }),
		             "save waveform guard fixture draft");
	}
	{
		const auto key = uuid();
		const auto crashedPath = audioSessionRecoveryPath(directory, key);
		QProcess child;
		child.setWorkingDirectory(temporary.path());
		child.start(QCoreApplication::applicationFilePath(), {"--crash-fixture", directory, key});
		QByteArray output;
		QElapsedTimer timer;
		timer.start();
		while (!output.contains("checkpoint-ready") && timer.elapsed() < 30000 &&
		       child.state() != QProcess::NotRunning) {
			child.waitForReadyRead(100);
			output += child.readAllStandardOutput();
		}
		ok &= expect(output.contains("checkpoint-ready") && QFileInfo::exists(crashedPath + ".active"),
		             "child writes checkpoint while holding editor lease");
		child.kill();
		child.waitForFinished();
		AudioSessionRecovery recovered;
		const auto crashedDigest = hash(read(crashedPath));
		ok &= expect(readAudioSessionRecovery(crashedPath, crashedDigest, &recovered) &&
		                 encodeAudioSession(recovered.session) == original,
		             "abrupt process termination retains complete session and media");
		ok &= expect(discardAudioRecovery(directory, key, crashedDigest, false, nullptr, AudioRecoveryKind::Session),
		             "portable stale lease recovery allows reviewed discard after interruption");
	}
	{
		AudioRecoveryDialog manager(directory);
		manager.setAttribute(Qt::WA_DeleteOnClose, false);
		manager.show();
		QCoreApplication::processEvents();
		ok &= expect(waitFor([&] { return !manager.busy(); }), "shared recovery manager verifies session copies");
		auto *table = manager.findChild<QTableWidget *>("audioRecoveryRecords");
		bool routed = false;
		manager.restore = [&](const QString &, const QByteArray &sha, AudioRecoveryKind kind) {
			routed = sha.size() == 32 && kind == AudioRecoveryKind::Session;
		};
		int sessionRow = -1;
		for (int row = 0; table && row < table->rowCount(); ++row) {
			if (table->item(row, 2)->text().contains("Session")) {
				sessionRow = row;
				break;
			}
		}
		ok &= expect(sessionRow >= 0, "manager distinguishes arrangements with track and clip counts");
		if (table && sessionRow >= 0) {
			table->setCurrentCell(sessionRow, 0);
			table->selectRow(sessionRow);
			const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!captures.isEmpty()) {
				QDir().mkpath(captures);
				QImage image(manager.size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				manager.render(&image);
				ok &= expect(image.save(QDir(captures).filePath("session-recovery-manager.png")),
				             "render mixed recovery manager directly");
			}
			manager.findChild<QPushButton *>("audioRecoveryRestore")->click();
		}
		ok &= expect(routed, "manager routes verified record by editor kind");
	}
	{
		ApplicationShell shell;
		shell.show();
		shell.beginSession(false);
		ok &= expect(waitFor([&] {
			             auto *notice = shell.findChild<QPushButton *>("noticeReviewAudio");
			             return notice && !notice->isHidden();
		             }),
		             "startup offers session recovery through shared notification");
		auto *review = shell.findChild<QPushButton *>("noticeReviewAudio");
		if (!review) {
			return 1;
		}
		review->click();
		auto *manager = shell.findChild<AudioRecoveryDialog *>();
		ok &= expect(manager && waitFor([&] {
			             return !manager->busy() &&
			                    manager->findChild<QTableWidget *>("audioRecoveryRecords")->rowCount() > 0;
		             }),
		             "startup opens shared review");
		if (manager) {
			manager->restore(reviewPath, reviewDigest, AudioRecoveryKind::Session);
			manager->close();
		}
		auto *editor = shell.findChild<AudioSessionDialog *>();
		ok &= expect(editor && waitFor([&] { return !editor->isBusy(); }) && editor->hasChanges(),
		             "shell restores session into matching editor");
		if (editor) {
			editor->setRecoveryEnabled(false);
			ok &= expect(!shell.findChild<QCheckBox *>("setupAudioRecoveryEnabled")->isChecked(),
			             "session recovery preference synchronizes setup");
			shell.findChild<QCheckBox *>("setupAudioRecoveryEnabled")->setChecked(true);
			ok &= expect(editor->findChild<QCheckBox *>("sessionRecoveryEnabled")->isChecked(),
			             "setup preference synchronizes session editor");
			ok &= expect(editor->saveSession(temporary.filePath("shell-restored.vssession")) &&
			                 waitFor([&] { return !editor->isBusy(); }),
			             "save shell draft before close");
		}
		shell.close();
	}
	ok &= expect(hash(read(path)) == digest, "unrelated retained session copy survives every lifecycle test");
	return ok ? 0 : 1;
}
