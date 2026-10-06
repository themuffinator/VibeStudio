#include "app/application_shell.h"
#include "app/audio_editor_dialog.h"
#include "app/audio_meter_widgets.h"
#include "app/audio_recording_dialog.h"
#include "app/audio_recording_meters.h"
#include "app/audio_session_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_playback.h"
#include "tests/fake_audio_recording.h"
#include "tests/fake_audio_stream.h"
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QTreeWidget>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::test;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool wait(const std::function<bool()> &condition, int milliseconds = 15000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < milliseconds) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	QCoreApplication::processEvents();
	return condition();
}
AudioSession baseSession()
{
	AudioSession session;
	session.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "backing";
	source.audio.clip = {1, 48000, QVector<float>(500, .125f)};
	source.audio.endFrame = 500;
	session.sources = {source};
	for (int i = 0; i < 2; ++i) {
		AudioSessionTrack track;
		track.id = QString::number(i);
		track.name = i ? "Room microphone" : "Lead microphone";
		track.regions = {{"backing" + track.id, "Backing", source.id, 0, 0, 500}};
		session.tracks << track;
	}
	AudioSessionTrack bus;
	bus.id = bus.name = "Main bus";
	bus.routing.bus = true;
	session.tracks << bus;
	return session;
}
void arm(AudioRecordingDialog &dialog, const QString &path)
{
	dialog.findChild<QLineEdit *>("recordingPath")->setText(path);
	dialog.findChild<QComboBox *>("recordingInput")->setCurrentIndex(1);
	dialog.findChild<QComboBox *>("recordingOutput")->setCurrentIndex(1);
	dialog.findChild<QSpinBox *>("recordingInputChannels")->setValue(4);
	dialog.findChild<QDoubleSpinBox *>("recordingPlaybackFirst")->setValue(5);
	dialog.findChild<QDoubleSpinBox *>("recordingPunchFirst")->setValue(37);
	dialog.findChild<QDoubleSpinBox *>("recordingPunchEnd")->setValue(94);
	dialog.findChild<QSpinBox *>("recordingCalibration")->setValue(3);
	auto *tracks = dialog.findChild<QTreeWidget *>("recordingArms");
	tracks->setCurrentItem(tracks->topLevelItem(0));
	tracks->currentItem()->setCheckState(0, Qt::Checked);
	dialog.findChild<QLineEdit *>("recordingChannels")->setText("4,2");
	dialog.findChild<QCheckBox *>("recordingMonitor")->setChecked(true);
	dialog.findChild<QCheckBox *>("recordingReplacePlayback")->setChecked(true);
	tracks->setCurrentItem(tracks->topLevelItem(1));
	tracks->currentItem()->setCheckState(0, Qt::Checked);
	dialog.findChild<QLineEdit *>("recordingChannels")->setText("3");
}
class Expanded final : public QTranslator {
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).contains("AudioRecording"))
			return {};
		return QString::fromUtf8(source) + QStringLiteral(" · ") + QString::fromUtf8(source);
	}
};
void addCompSection(AudioRecordingDialog &dialog, int pass, int first, int end)
{
	dialog.findChild<QSpinBox *>("recordingTakePass")->setValue(pass);
	dialog.findChild<QDoubleSpinBox *>("recordingTakeFirst")->setValue(first);
	dialog.findChild<QDoubleSpinBox *>("recordingTakeEnd")->setValue(end);
	dialog.findChild<QPushButton *>("recordingCompAdd")->click();
}
bool playbackAcknowledgement()
{
	auto device = std::make_unique<FakeAudioPlaybackBackend>();
	auto *fake = device.get();
	fake->delayClear = true;
	AudioPlayback playback(nullptr, std::move(device));
	QObject context;
	int completed = 0;
	bool acknowledged = false;
	const auto complete = [&](bool stopped) {
		++completed;
		acknowledged = stopped;
	};
	bool ok = playback.start("fixture", 0, 100, 48000);
	playback.stopAndWait(&context, complete);
	QCoreApplication::processEvents();
	ok &= expect(completed == 0 && fake->pendingClear.size() == 1, "stop waits for actual backend release");
	fake->finishClear();
	ok &= expect(completed == 0 && wait([&] { return completed == 1; }) && acknowledged,
	             "backend completion is queued through playback owner lifetime");
	playback.stopAndWait(&context, complete);
	playback.start("fixture", 0, 100, 48000);
	fake->finishClear();
	ok &= expect(wait([&] { return completed == 2; }) && !acknowledged,
	             "new audition invalidates an earlier shutdown acknowledgement");
	bool restart = true;
	const auto listener = QObject::connect(&playback, &AudioPlayback::changed, &context, [&] {
		if (restart && playback.state() == AudioPlayback::State::Stopped) {
			restart = false;
			playback.start("new audition", 0, 100, 48000);
		}
	});
	playback.stopAndWait(&context, complete);
	QObject::disconnect(listener);
	ok &= expect(wait([&] { return completed == 3; }) && !acknowledged && fake->bytes == "new audition",
	             "reentrant audition is rejected by the gate without clearing the newer backend");
	auto *discarded = new QObject;
	playback.stopAndWait(discarded, complete);
	delete discarded;
	fake->finishClear();
	QCoreApplication::processEvents();
	ok &= expect(completed == 3, "destroyed recording context receives no stale completion");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath("recording-ui-XXXXXX"));
	if (!temp.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return QDir(temp.path()).filePath(QLatin1String(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	const auto base = baseSession();
	bool ok = playbackAcknowledgement();
	const auto folder = path("pass.vsrecord");
	auto fixture = std::make_shared<RecordingFixture>();
	auto disk = std::make_shared<RecordingDiskFixture>();
	std::function<void(QString)> permitted, stopped;
	AudioRecordingDialog dialog(
	    base, {}, 37, 94, nullptr, recordingDeviceFactory(fixture), recordingStorageFactory(fixture, disk),
	    [&](QObject *, auto done) { permitted = done; }, [&](QObject *, auto done) { stopped = done; });
	dialog.show();
	auto *recording = dialog.findChild<AudioRecording *>();
	ok &= expect(wait([&] { return recording->available(); }) && fixture->opens == 0,
	             "opening recording only enumerates simulated endpoints");
	arm(dialog, folder);
	ok &= expect(dialog.findChild<QTreeWidget *>("recordingArms")->topLevelItemCount() == 2 && fixture->opens == 0,
	             "buses cannot be armed and arming does not open input");
	auto *record = dialog.findChild<QPushButton *>("recordingRecord");
	dialog.findChild<QLineEdit *>("recordingChannels")->setText("5");
	ok &= expect(!record->isEnabled() && !permitted, "out-of-range input maps are refused before permission");
	dialog.findChild<QLineEdit *>("recordingChannels")->setText("3");
	dialog.findChild<QDoubleSpinBox *>("recordingPunchEnd")->setValue(37);
	ok &= expect(!record->isEnabled(), "empty punch is refused before device work");
	dialog.findChild<QDoubleSpinBox *>("recordingPunchEnd")->setValue(94);
	record->click(); // Direct widget API; no mouse/keyboard injection.
	ok &= expect(bool(permitted) && fixture->opens == 0 && !QFileInfo::exists(folder),
	             "permission precedes file/device work");
	dialog.findChild<QPushButton *>("recordingStop")->click();
	permitted({});
	ok &= expect(wait([&] { return !dialog.busy(); }) && fixture->opens == 0 && !QFileInfo::exists(folder),
	             "cancelled permission cannot start a late recording");
	record->click();
	permitted({});
	ok &= expect(wait([&] { return bool(stopped); }) && fixture->opens == 0, "input waits for all playback owners");
	stopped({});
	ok &=
	    expect(wait([&] { return !dialog.busy(); }) && recording->snapshot().result.receiptMatches &&
	               recording->snapshot().result.takes.size() == 2 && fixture->validThreads && fixture->allJournalsReady,
	           "native controls produce verified grouped takes via worker-owned devices and disk");
	if (!recording->snapshot().result.receiptMatches) {
		std::cerr << recording->snapshot().error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	ok &= expect(recording->snapshot().result.takes[0].frames == 57 && dialog.importRequest().selections.size() == 2,
	             "automatic review selects exact punched frames");
	const auto recordedLevels = recording->snapshot();
	auto *meters = dialog.findChild<QTreeWidget *>("recordingMeterTable");
	ok &= expect(meters && meters->topLevelItemCount() == 5 && meters->topLevelItem(0)->text(0).contains("Input 4") &&
	                 meters->topLevelItem(1)->text(0).contains("Input 2") &&
	                 meters->topLevelItem(0)->data(1, AudioMeterLevelRole).toDouble() > 0 &&
	                 meters->topLevelItem(3)->text(6) == "Output clipped",
	             "meters preserve input channel order, measured levels and explicit output clipping state");
	dialog.findChild<QTabWidget *>()->setCurrentIndex(2);
	dialog.findChild<QPushButton *>("recordingMeterReset")->click();
	ok &= expect(recording->snapshot().levels.count == 0 && recording->snapshot().result.receiptMatches &&
	                 meters->topLevelItem(0)->text(6) == "No measured frames",
	             "meter reset clears transient readings without altering verified takes");
	dialog.findChild<QDoubleSpinBox *>("recordingTakeFirst")->setValue(2);
	dialog.findChild<QLineEdit *>("recordingStoredChannels")->setText("2,1");
	ok &= expect(dialog.importRequest().selections[0].position == 39, "trim keeps recorded alignment");
	auto *takes = dialog.findChild<QTreeWidget *>("recordingTakes");
	takes->setCurrentItem(takes->topLevelItem(1));
	dialog.findChild<QCheckBox *>("recordingUseRecordedPlacement")->setChecked(false);
	dialog.findChild<QDoubleSpinBox *>("recordingTakePosition")->setValue(120);
	takes->setCurrentItem(takes->topLevelItem(0));
	ok &=
	    expect(dialog.importRequest().selections[1].position == 120 && dialog.importRequest().selections[0].first == 2,
	           "switching take selection retains independent manual placement and trim choices");
	dialog.findChild<QPushButton *>("recordingImport")->click();
	ok &= expect(wait([&] { return !dialog.busy(); }) && dialog.result() == QDialog::Accepted &&
	                 dialog.imported().session.groups.size() == 1 && dialog.imported().session.sources.size() == 3,
	             "review imports both arms into a grouped immutable session snapshot");
	if (dialog.imported().succeeded() && dialog.imported().session.sources.size() == 3) {
		const auto &samples = dialog.imported().session.sources[1].audio.clip.samples;
		ok &= expect(samples.size() == 110 && samples[0] == 661 && samples[1] == 663,
		             "review trims and reorders exact dry input ordinals without printing monitor effects");
	}
	AudioProjectSaveRequest save;
	save.path = path("base.vssession");
	ok &= expect(writeAudioSession(base, save).succeeded, "write session handoff fixture");
	AudioSessionDialog session(nullptr, fakeAudioStreamFactory());
	session.setAttribute(Qt::WA_DeleteOnClose, false);
	session.setRecoveryEnabled(false);
	ok &= expect(session.openSession(save.path) && wait([&] { return !session.isBusy(); }), "open handoff session");
	ok &= expect(session.openRecording(folder, recordingDeviceFactory(std::make_shared<RecordingFixture>())),
	             "session opens grouped review with fake device enumeration");
	auto *review = session.findChild<AudioRecordingDialog *>();
	ok &= expect(review && wait([&] { return !review->busy(); }) && session.isBusy() && session.recordingOpen(),
	             "review freezes parent session revision");
	if (!review)
		return EXIT_FAILURE;
	AudioSessionEdit edit;
	edit.operation = "add-track";
	edit.name = "Must remain blocked";
	ok &= expect(!session.applyEdit(edit, "Blocked edit"), "session edits cannot invalidate active review");
	review->findChild<QPushButton *>("recordingImport")->click();
	ok &= expect(wait([&] { return !session.isBusy(); }) && session.session().sources.size() == 3 &&
	                 session.session().groups.size() == 1,
	             "session adopts prepared waveforms and all takes atomically");
	session.undo();
	ok &= expect(encodeAudioSession(session.session()) == encodeAudioSession(base),
	             "one undo restores all replaced clips");
	session.redo();
	ok &= expect(session.session().sources.size() == 3 && session.session().groups.size() == 1,
	             "one redo restores all takes");
	ok &= expect(session.saveSession(path("recorded.vssession")) && wait([&] { return !session.isBusy(); }),
	             "recorded session saves through native guarded writer");
	const auto receiptPath = QDir(folder).filePath("result.json");
	QFile receipt(receiptPath);
	ok &= expect(receipt.open(QIODevice::ReadOnly), "read receipt for controlled interruption fixture");
	const auto receiptBytes = receipt.readAll();
	receipt.close();
	ok &= expect(receipt.remove(), "remove fixture receipt to simulate interrupted finalization");
	AudioRecordingDialog recovery(base, {}, 0, 1, nullptr,
	                              recordingDeviceFactory(std::make_shared<RecordingFixture>()));
	recovery.inspectRecording(folder);
	ok &= expect(wait([&] { return !recovery.busy(); }) &&
	                 !recovery.findChild<QPushButton *>("recordingImport")->isEnabled(),
	             "missing receipt requires explicit acceptance despite complete individual journals");
	recovery.findChild<QCheckBox *>("recordingAcceptInterrupted")->setChecked(true);
	recovery.findChild<QPushButton *>("recordingImport")->click();
	recovery.findChild<QPushButton *>("recordingStop")->click();
	ok &= expect(wait([&] { return !recovery.busy(); }) && recovery.result() != QDialog::Accepted &&
	                 recovery.imported().regionIds.isEmpty(),
	             "cancelled import never publishes a partial session");
	recovery.findChild<QPushButton *>("recordingImport")->click();
	ok &= expect(wait([&] { return !recovery.busy(); }) && recovery.result() == QDialog::Accepted,
	             "explicit prefix review recovers retained takes");
	ok &= expect(receipt.open(QIODevice::WriteOnly) && receipt.write(receiptBytes) == receiptBytes.size(),
	             "restore fixture receipt");
	receipt.close();
	{
		auto browserDevice = std::make_unique<FakeAudioPlaybackBackend>();
		auto *browser = browserDevice.get();
		browser->delayClear = true;
		ApplicationShell shell(nullptr, std::move(browserDevice));
		shell.openPathFromCommandLine(save.path);
		auto *liveSession = shell.findChild<AudioSessionDialog *>();
		ok &= expect(liveSession && wait([&] { return !liveSession->isBusy(); }), "shell opens recording destination");
		if (!liveSession)
			return EXIT_FAILURE;
		liveSession->setRecoveryEnabled(false);
		auto *browserPlayback = shell.findChild<AudioPlayback *>(QString{}, Qt::FindDirectChildrenOnly);
		ok &=
		    expect(browserPlayback && browserPlayback->start("fixture", 0, 100, 48000), "start fake browser audition");
		auto input = std::make_shared<RecordingFixture>();
		auto files = std::make_shared<RecordingDiskFixture>();
		liveSession->setSelection(37, 94);
		ok &=
		    expect(liveSession->openRecording({}, recordingDeviceFactory(input), recordingStorageFactory(input, files)),
		           "shell recording uses coordinated output handoff");
		auto *recordView = liveSession->findChild<AudioRecordingDialog *>();
		if (!recordView)
			return EXIT_FAILURE;
		ok &= wait([&] { return recordView->findChild<AudioRecording *>()->available(); });
		arm(*recordView, path("shell.vsrecord"));
		recordView->findChild<QPushButton *>("recordingRecord")->click();
		ok &= expect(wait([&] { return !browser->pendingClear.isEmpty(); }) && input->opens == 0 &&
		                 !QFileInfo::exists(path("shell.vsrecord")),
		             "recording waits for the browser's real shutdown acknowledgement");
		browser->finishClear();
		ok &= expect(wait([&] { return !recordView->busy(); }) &&
		                 recordView->findChild<AudioRecording *>()->snapshot().result.receiptMatches,
		             "capture starts after browser and session outputs release");
		recordView->close();
		ok &= expect(wait([&] { return !liveSession->recordingOpen(); }), "closing review releases session editing");
	}
	const auto loopFolder = path("looped.vsrecord");
	{
		auto input = std::make_shared<RecordingFixture>();
		auto files = std::make_shared<RecordingDiskFixture>();
		AudioRecordingDialog looped(base, {}, 37, 94, nullptr, recordingDeviceFactory(input),
		                            recordingStorageFactory(input, files));
		looped.show();
		auto *worker = looped.findChild<AudioRecording *>();
		ok &= wait([&] { return worker->available(); });
		arm(looped, loopFolder);
		looped.findChild<QSpinBox *>("recordingLoopPasses")->setValue(3);
		looped.findChild<QPushButton *>("recordingRecord")->click();
		ok &= expect(wait([&] { return !looped.busy(); }) && worker->snapshot().result.receiptMatches &&
		                 worker->snapshot().progress.capturedFrames == 171 && input->opens == 1,
		             "native loop control records three exact passes through one stream");
		auto *pass = looped.findChild<QSpinBox *>("recordingTakePass");
		ok &= expect(pass->maximum() == 3, "review offers every captured pass");
		pass->setValue(2);
		looped.findChild<QDoubleSpinBox *>("recordingTakeFirst")->setValue(1);
		auto *takeList = looped.findChild<QTreeWidget *>("recordingTakes");
		takeList->setCurrentItem(takeList->topLevelItem(1));
		pass->setValue(3);
		ok &= expect(looped.importRequest().selections.size() == 2 &&
		                 looped.importRequest().selections[0].loopPass == 1 &&
		                 looped.importRequest().selections[0].first == 1 &&
		                 looped.importRequest().selections[0].position == 38 &&
		                 looped.importRequest().selections[1].loopPass == 2,
		             "each arm retains its reviewed pass with local trims and original timeline alignment");
		looped.findChild<QPushButton *>("recordingImport")->click();
		ok &= expect(wait([&] { return !looped.busy(); }) && looped.result() == QDialog::Accepted &&
		                 looped.imported().session.sources.size() == 3,
		             "selected loop passes import as one grouped session change");
		if (looped.imported().session.sources.size() == 3)
			ok &= expect(looped.imported().session.sources[1].audio.clip.samples.first() == 1223 &&
			                 looped.imported().session.sources[2].audio.clip.samples.first() == 1782,
			             "GUI pass choices select exact dry sample ordinals without cumulative latency compensation");
	}
	{
		AudioSessionDialog parent(nullptr, fakeAudioStreamFactory());
		parent.setAttribute(Qt::WA_DeleteOnClose, false);
		parent.setRecoveryEnabled(false);
		ok &= expect(parent.openSession(save.path) && wait([&] { return !parent.isBusy(); }) &&
		                 parent.openRecording(loopFolder, recordingDeviceFactory(std::make_shared<RecordingFixture>())),
		             "open comp review against an unchanged session snapshot");
		auto *comp = parent.findChild<AudioRecordingDialog *>();
		if (!comp || !wait([&] { return !comp->busy(); }))
			return EXIT_FAILURE;
		comp->findChild<QCheckBox *>("recordingComp")->setChecked(true);
		auto *queued = comp->findChild<QTreeWidget *>("recordingCompSections");
		auto *import = comp->findChild<QPushButton *>("recordingImport");
		ok &= expect(!import->isEnabled(), "an empty comp cannot import implicitly checked whole takes");
		addCompSection(*comp, 1, 0, 20);
		addCompSection(*comp, 2, 20, 40);
		addCompSection(*comp, 1, 40, 57);
		comp->findChild<QDoubleSpinBox *>("recordingCompCrossfade")->setValue(3);
		ok &= expect(queued->topLevelItemCount() == 3 && import->isEnabled() &&
		                 comp->importRequest().selections[2].loopPass == 0 &&
		                 audioRecordingImportRequestJson(comp->importRequest())["version"].toInt() == 3,
		             "native queue retains repeated-pass sections and reviewed crossfade intent");
		queued->setCurrentItem(queued->topLevelItem(1));
		ok &= expect(comp->findChild<QSpinBox *>("recordingTakePass")->value() == 2 &&
		                 comp->findChild<QDoubleSpinBox *>("recordingTakeFirst")->value() == 20,
		             "selecting a queued section restores its exact editable source choice");
		comp->findChild<QDoubleSpinBox *>("recordingTakeFirst")->setValue(19);
		comp->findChild<QPushButton *>("recordingCompUpdate")->click();
		ok &= expect(comp->importRequest().selections[1].first == 20 &&
		                 comp->findChild<QLabel *>("recordingStatus")->text().contains("overlap"),
		             "an overlapping edit leaves the prior valid comp queue unchanged");
		comp->findChild<QDoubleSpinBox *>("recordingTakeFirst")->setValue(20);
		comp->findChild<QSpinBox *>("recordingTakePass")->setValue(3);
		comp->findChild<QDoubleSpinBox *>("recordingTakeFirst")->setValue(20);
		comp->findChild<QDoubleSpinBox *>("recordingTakeEnd")->setValue(40);
		comp->findChild<QPushButton *>("recordingCompUpdate")->click();
		ok &=
		    expect(comp->importRequest().selections[1].loopPass == 2, "valid queue edits replace the selected section");
		queued->setCurrentItem(queued->topLevelItem(2));
		comp->findChild<QPushButton *>("recordingCompRemove")->click();
		ok &= expect(queued->topLevelItemCount() == 2, "remove affects only the selected comp section");
		addCompSection(*comp, 1, 40, 57);
		comp->findChild<QDoubleSpinBox *>("recordingCompCrossfade")->setValue(20);
		ok &= expect(!import->isEnabled(), "insufficient handle audio disables comp import");
		comp->findChild<QDoubleSpinBox *>("recordingCompCrossfade")->setValue(3);
		import->click();
		ok &= expect(wait([&] { return !parent.isBusy(); }) && parent.session().sources.size() == 4 &&
		                 parent.session().groups.size() == 1,
		             "comp sections adopt as one session edit");
		const auto committed = encodeAudioSession(parent.session());
		parent.undo();
		ok &= expect(encodeAudioSession(parent.session()) == encodeAudioSession(base),
		             "one comp undo restores replaced backing clips and removes every imported section");
		parent.redo();
		ok &= expect(encodeAudioSession(parent.session()) == committed, "one comp redo restores the entire assembly");
	}
	{
		AudioRecordingDialog savedView(base, {}, 37, 94, nullptr,
		                               recordingDeviceFactory(std::make_shared<RecordingFixture>()));
		ok &= expect(savedView.inspectRecording(loopFolder) && wait([&] { return !savedView.busy(); }),
		             "inspect retained loop takes for saved review");
		savedView.findChild<QCheckBox *>("recordingComp")->setChecked(true);
		addCompSection(savedView, 1, 0, 20);
		addCompSection(savedView, 2, 20, 40);
		addCompSection(savedView, 1, 40, 57);
		savedView.findChild<QDoubleSpinBox *>("recordingCompCrossfade")->setValue(3);
		const auto queued = audioRecordingImportRequestJson(savedView.importRequest());
		ok &= expect(savedView.reviewModified() && savedView.saveReview(path("saved-review.json")) &&
		                 wait([&] { return !savedView.busy(); }) && !savedView.reviewModified() &&
		                 !savedView.reviewIdentity().path.isEmpty(),
		             "saving a comp preserves its complete queue and marks the review saved");
		AudioRecordingDialog reopened(base, {}, 37, 94, nullptr,
		                              recordingDeviceFactory(std::make_shared<RecordingFixture>()));
		ok &= expect(reopened.openReview(path("saved-review.json")) && wait([&] { return !reopened.busy(); }) &&
		                 !reopened.reviewModified() &&
		                 audioRecordingImportRequestJson(reopened.importRequest()) == queued &&
		                 reopened.findChild<QTreeWidget *>("recordingCompSections")->topLevelItemCount() == 3,
		             "opening a saved comp restores repeated-pass sections and crossfades exactly");
		reopened.findChild<QDoubleSpinBox *>("recordingCompCrossfade")->setValue(2);
		ok &= expect(reopened.reviewModified() && reopened.findChild<QPushButton *>("recordingSaveReview")->isEnabled(),
		             "an edited saved review exposes unsaved state and can be saved again");
		QFile external(path("saved-review.json"));
		ok &=
		    expect(external.open(QIODevice::Append) && external.write("\n") == 1, "simulate a concurrent review edit");
		external.close();
		reopened.show();
		reopened.close();
		auto *conflictGuard = reopened.findChild<QMessageBox *>("recordingUnsavedReview");
		if (!conflictGuard)
			return EXIT_FAILURE;
		conflictGuard->button(QMessageBox::Save)->click();
		ok &= expect(wait([&] { return !reopened.busy(); }) && reopened.isVisible() && reopened.reviewModified() &&
		                 reopened.findChild<QLabel *>("recordingStatus")->text().contains("changed"),
		             "stale review identity refuses an overwrite and retains unsaved choices");
		ok &= expect(reopened.saveReview(path("review-variation.json")) && wait([&] { return !reopened.busy(); }) &&
		                 !reopened.reviewModified(),
		             "Save As retains a conflicting review as a separate variation");
		const auto retainedReview = audioRecordingImportRequestJson(reopened.importRequest());
		const auto retainedIdentity = reopened.reviewIdentity();
		ok &= expect(reopened.openReview(path("missing-review.json")) && wait([&] { return !reopened.busy(); }) &&
		                 audioRecordingImportRequestJson(reopened.importRequest()) == retainedReview &&
		                 reopened.reviewIdentity().sha256 == retainedIdentity.sha256,
		             "failed review loading leaves the working queue and saved identity intact");
		ok &= expect(reopened.openReview(path("saved-review.json")), "begin cancellable review open");
		reopened.findChild<QPushButton *>("recordingStop")->click();
		ok &= expect(wait([&] { return !reopened.busy(); }) &&
		                 audioRecordingImportRequestJson(reopened.importRequest()) == retainedReview &&
		                 reopened.reviewIdentity().sha256 == retainedIdentity.sha256,
		             "cancelled review verification cannot replace the working queue");
		auto multiple = savedView.importRequest();
		multiple.comp = false;
		multiple.crossfadeFrames = 0;
		multiple.selections.removeLast();
		AudioProjectSaveRequest target;
		target.path = path("multiple-passes.json");
		ok &= expect(writeAudioRecordingReview(base, multiple, target).succeeded && reopened.openReview(target.path) &&
		                 wait([&] { return !reopened.busy(); }) && !reopened.importRequest().comp &&
		                 reopened.findChild<QCheckBox *>("recordingUseQueue")->isChecked() &&
		                 audioRecordingImportRequestJson(reopened.importRequest()) ==
		                     audioRecordingImportRequestJson(multiple),
		             "non-comp CLI reviews retain multiple selections from one arm without collapsing passes");
		ok &= expect(reopened.imported().regionIds.isEmpty() && savedView.imported().regionIds.isEmpty(),
		             "review persistence does not adopt session edits");
		reopened.show();
		reopened.findChild<QCheckBox *>("recordingGroup")->setChecked(!multiple.groupRegions);
		reopened.close();
		auto *unsaved = reopened.findChild<QMessageBox *>("recordingUnsavedReview");
		ok &= expect(unsaved && reopened.isVisible() && reopened.reviewModified(),
		             "closing a changed review asks before losing its choices");
		if (!unsaved)
			return EXIT_FAILURE;
		unsaved->button(QMessageBox::Cancel)->click();
		ok &= expect(wait([&] { return !reopened.findChild<QMessageBox *>("recordingUnsavedReview"); }) &&
		                 reopened.isVisible() && reopened.reviewModified(),
		             "Cancel retains the open editable review");
		reopened.close();
		unsaved = reopened.findChild<QMessageBox *>("recordingUnsavedReview");
		if (!unsaved)
			return EXIT_FAILURE;
		unsaved->button(QMessageBox::Save)->click();
		ok &= expect(wait([&] { return !reopened.busy() && !reopened.isVisible(); }) && !reopened.reviewModified() &&
		                 openAudioRecordingReview(base, target.path).review.groupRegions == !multiple.groupRegions,
		             "Save completes the guarded write before the requested close");
	}
	const auto renders = qEnvironmentVariable("VIBESTUDIO_AUDIO_RECORDING_RENDER_DIR");
	if (!renders.isEmpty())
		ok &= expect(QDir().mkpath(renders), "create recording layout evidence directory");
	struct Layout {
		StudioTheme theme;
		int scale;
		bool rtl;
		const char *name;
	};
	for (const auto &layout :
	     {Layout{StudioTheme::Dark, 100, false, "dark"}, Layout{StudioTheme::HighContrastLight, 125, false, "contrast"},
	      Layout{StudioTheme::HighContrastDark, 200, true, "expanded-rtl"}}) {
		Expanded expanded;
		if (layout.rtl)
			app.installTranslator(&expanded);
		applyStudioTheme(app, studioThemeTokens(layout.theme, UiDensity::Comfortable, layout.scale));
		AudioRecordingDialog view(base, {}, 37, 94, nullptr,
		                          recordingDeviceFactory(std::make_shared<RecordingFixture>()));
		view.setLayoutDirection(layout.rtl ? Qt::RightToLeft : Qt::LeftToRight);
		view.resize(920, 780);
		view.show();
		ok &= wait([&] { return view.findChild<AudioRecording *>()->available(); });
		arm(view, path("layout.vsrecord"));
		view.findChild<QSpinBox *>("recordingLoopPasses")->setValue(3);
		for (int page = 0; page < 4; ++page) {
			if (page == 1) {
				view.inspectRecording(loopFolder);
				ok &= wait([&] { return !view.busy(); });
				view.findChild<QCheckBox *>("recordingComp")->setChecked(true);
				addCompSection(view, 1, 0, 20);
				addCompSection(view, 2, 20, 40);
				addCompSection(view, 1, 40, 57);
				view.findChild<QDoubleSpinBox *>("recordingCompCrossfade")->setValue(3);
			}
			view.findChild<QTabWidget *>()->setCurrentIndex(page);
			if (page == 2) {
				auto *panel = view.findChild<AudioRecordingMeters *>();
				panel->setPass(base, recordedLevels.result.plan);
				panel->setSnapshot(recordedLevels, false);
			}
			QCoreApplication::processEvents();
			for (auto *scroll : view.findChildren<QScrollArea *>())
				if (scroll->isVisible())
					ok &= expect(scroll->horizontalScrollBar()->maximum() == 0,
					             "scaled translated dialog has no horizontal clipping");
			for (auto *label : view.findChildren<QLabel *>())
				if (label->isVisible() && label->wordWrap())
					ok &= expect(label->height() >= label->heightForWidth(label->width()),
					             "recording labels reserve wrapped text height");
			for (auto *control : view.findChildren<QLineEdit *>())
				if (control->objectName().startsWith("recording"))
					ok &= expect(!control->accessibleName().isEmpty() && control->focusPolicy() != Qt::NoFocus,
					             "recording fields expose accessible names and keyboard focus");
			for (auto *control :
			     {view.findChild<QSpinBox *>("recordingLoopPasses"), view.findChild<QSpinBox *>("recordingTakePass")}) {
				auto *accessible = QAccessible::queryAccessibleInterface(control);
				ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
				                 control->focusPolicy() != Qt::NoFocus,
				             "loop count and reviewed pass expose native accessible keyboard controls");
			}
			for (auto *tree : view.findChildren<QTreeWidget *>()) {
				auto *accessible = QAccessible::queryAccessibleInterface(tree);
				ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
				                 tree->focusPolicy() != Qt::NoFocus,
				             "arm and take trees expose native accessible keyboard semantics");
			}
			for (const auto *name :
			     {"recordingOpenReview", "recordingSaveReview", "recordingSaveReviewAs", "recordingUseQueue",
			      "recordingComp", "recordingCompAdd", "recordingCompUpdate", "recordingCompRemove",
			      "recordingCompCrossfade", "recordingAuditionPlaySection", "recordingAuditionPlayReview",
			      "recordingAuditionBacking", "recordingAuditionOutput", "recordingAuditionBuffer",
			      "recordingAuditionVolume", "recordingAuditionLoop", "recordingAuditionPosition",
			      "recordingAuditionReplay", "recordingAuditionPause"}) {
				auto *control = view.findChild<QWidget *>(QLatin1String(name));
				auto *accessible = QAccessible::queryAccessibleInterface(control);
				ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
				                 control->focusPolicy() != Qt::NoFocus,
				             "comp actions expose accessible names and native keyboard focus");
			}
			if (!renders.isEmpty()) {
				QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				view.render(&image);
				const auto stem = QString::fromLatin1(layout.name) + (page == 3   ? "-audition"
				                                                      : page == 2 ? "-meters"
				                                                      : page      ? "-review"
				                                                                  : "-record");
				ok &= expect(image.save(QDir(renders).filePath(stem + ".png")), "render widget without OS capture");
				if (page < 2) {
					for (auto *scroll : view.findChildren<QScrollArea *>())
						if (scroll->isVisible())
							scroll->ensureWidgetVisible(
							    view.findChild<QSpinBox *>(page ? "recordingTakePass" : "recordingLoopPasses"));
					QCoreApplication::processEvents();
					view.render(&image);
					ok &= expect(image.save(QDir(renders).filePath(stem + "-pass.png")),
					             "render reachable pass selection at expanded scale");
				}
				if (page == 1) {
					for (auto *scroll : view.findChildren<QScrollArea *>())
						if (scroll->isVisible())
							scroll->ensureWidgetVisible(view.findChild<QTreeWidget *>("recordingCompSections"));
					QCoreApplication::processEvents();
					view.render(&image);
					ok &= expect(image.save(QDir(renders).filePath(stem + "-comp.png")),
					             "render the reachable comp queue");
				}
				for (auto *scroll : view.findChildren<QScrollArea *>())
					if (scroll->isVisible())
						scroll->ensureWidgetVisible(view.findChild<QPushButton *>(page == 3   ? "recordingAuditionPause"
						                                                          : page == 2 ? "recordingMeterReset"
						                                                          : page      ? "recordingImport"
						                                                                      : "recordingRecord"));
				QCoreApplication::processEvents();
				view.render(&image);
				ok &= expect(image.save(QDir(renders).filePath(stem + "-controls.png")),
				             "render reachable lower controls");
			}
		}
		view.resize(600, 400);
		QCoreApplication::processEvents();
		bool scrolls = false;
		for (auto *scroll : view.findChildren<QScrollArea *>())
			scrolls |= scroll->verticalScrollBar()->maximum() > 0;
		ok &= expect(scrolls, "small scaled review remains reachable by scrolling");
		view.close();
		auto *guard = view.findChild<QMessageBox *>("recordingUnsavedReview");
		ok &= expect(guard && guard->defaultButton() == guard->button(QMessageBox::Cancel) &&
		                 guard->layoutDirection() == view.layoutDirection(),
		             "changed review uses a native confirmation with Cancel as default and the editor's direction");
		if (!guard)
			return EXIT_FAILURE;
		QCoreApplication::processEvents();
		if (!renders.isEmpty()) {
			QImage image(guard->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			guard->render(&image);
			ok &= expect(image.save(QDir(renders).filePath(QString::fromLatin1(layout.name) + "-review-unsaved.png")),
			             "render native saved-review confirmation without OS capture");
		}
		guard->button(QMessageBox::Discard)->click();
		ok &= expect(wait([&] { return !view.isVisible(); }), "explicit Discard completes review close");
		if (layout.rtl)
			app.removeTranslator(&expanded);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
