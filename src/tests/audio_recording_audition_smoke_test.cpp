#include "app/audio_recording_dialog.h"
#include "app/audio_session_dialog.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_recording.h"
#include "tests/fake_audio_stream.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTreeWidget>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::test;
namespace
{
using State = AudioSessionPlaybackSnapshot::State;
bool expect(bool value, const char *text)
{
	if (!value)
		std::cerr << text << '\n';
	return value;
}
bool wait(const std::function<bool()> &ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 10000) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	QCoreApplication::processEvents();
	return ready();
}
QByteArray bytes(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool exactSamples(const QByteArray &bytes, const QVector<float> &expected)
{
	if (bytes.size() != expected.size() * 4)
		return false;
	for (qsizetype i = 0; i < expected.size(); ++i) {
		float value;
		std::memcpy(&value, bytes.constData() + i * 4, 4);
		if (value != expected[i])
			return false;
	}
	return true;
}
AudioSession session()
{
	AudioSession result;
	result.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "backing";
	source.audio.clip = {2, 48000, QVector<float>(64, .03125f)};
	source.audio.endFrame = 32;
	result.sources = {source};
	for (int i = 0; i < 2; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		track.regions = {{"backing-" + track.id, "Backing", source.id, 0, 0, 32}};
		result.tracks << track;
	}
	return result;
}
void section(AudioRecordingDialog &view, int pass, int first, int end)
{
	view.findChild<QSpinBox *>("recordingTakePass")->setValue(pass);
	view.findChild<QDoubleSpinBox *>("recordingTakeFirst")->setValue(first);
	view.findChild<QDoubleSpinBox *>("recordingTakeEnd")->setValue(end);
	view.findChild<QPushButton *>("recordingCompAdd")->click();
}
void comp(AudioRecordingDialog &view)
{
	view.findChild<QCheckBox *>("recordingReplaceExisting")->setChecked(true);
	view.findChild<QCheckBox *>("recordingComp")->setChecked(true);
	section(view, 1, 0, 4);
	section(view, 2, 4, 8);
	section(view, 1, 8, 12);
	view.findChild<QDoubleSpinBox *>("recordingCompCrossfade")->setValue(3);
	view.findChild<QSlider *>("recordingAuditionVolume")->setValue(100);
}
} // namespace
int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath("recording-audition-XXXXXX"));
	if (!temp.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return QDir(temp.path()).filePath(QLatin1String(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	const auto base = session();
	const auto original = encodeAudioSession(base);
	AudioRecordingPlan plan;
	plan.name = "Audition fixture";
	plan.pass.punchFirst = 3;
	plan.pass.punchEnd = 15;
	plan.pass.loopPasses = 2;
	plan.pass.inputChannels = 2;
	plan.pass.arms = {{"0", 2, {0, 1}}};
	QString error;
	QByteArray digest;
	const auto folder = path("takes.vsrecord");
	bool ok = expect(createAudioRecordingFolder(folder, plan, &digest, &error), "create audition fixture");
	std::vector<float> samples;
	for (int i = 0; i < 48; ++i)
		samples.push_back(float(i + 1) / 128);
	AudioTakeWriter writer;
	ok &= expect(writer.open(audioRecordingArmPath(folder, 0), audioRecordingArmMetadata(plan, 0), &error) &&
	                 writer.append(samples, &error) && writer.finish(&error),
	             "write audition take journal");
	AudioRecordingReceipt receipt;
	receipt.outcome = AudioRecordingReceipt::Outcome::Complete;
	receipt.progress.state = AudioDuplexProgress::State::Complete;
	receipt.progress.capturedFrames = 24;
	ok &= expect(finishAudioRecordingFolder(folder, digest, receipt, &error), "seal audition fixture");
	if (!ok)
		return EXIT_FAILURE;
	const auto journalBytes = bytes(audioRecordingArmPath(folder, 0));
	auto output = std::make_shared<FakeAudioStreamState>();
	output->maxWrite = 5; // Device writes split individual samples and frames.
	auto input = std::make_shared<RecordingFixture>();
	std::function<void(QString)> gate;
	int permissions = 0;
	const AudioRecordingGate permission = [&](QObject *, auto complete) {
		++permissions;
		complete({});
	};
	AudioRecordingDialog view(
	    base, {}, 3, 15, nullptr, recordingDeviceFactory(input), {}, permission,
	    [&](QObject *, auto complete) { gate = complete; }, fakeAudioStreamFactory(output));
	view.show();
	auto *playback = view.findChild<AudioSessionPlayback *>("recordingAuditionPlayback");
	if (!expect(view.inspectRecording(folder) && wait([&] { return !view.busy() && playback->available(); }),
	            "inspect without opening either device"))
		return EXIT_FAILURE;
	comp(view);
	view.findChild<QComboBox *>("recordingAuditionOutput")->setCurrentIndex(1);
	view.findChild<QComboBox *>("recordingAuditionBuffer")->setCurrentIndex(1);
	auto *play = view.findChild<QPushButton *>("recordingAuditionPlayReview");
	auto *stop = view.findChild<QPushButton *>("recordingStop");
	play->click();
	ok &= expect(wait([&] { return bool(gate); }) && view.busy() && input->opens == 0 &&
	                 output->locked([](auto &v) { return v.opens == 0; }),
	             "audition waits for shared playback shutdown");
	const auto stale = gate;
	stop->click();
	stale({});
	ok &= expect(wait([&] { return !view.busy(); }) && output->locked([](auto &v) { return v.opens == 0; }),
	             "Stop invalidates a delayed playback handoff");
	gate = {};
	play->click();
	ok &= wait([&] { return bool(gate); });
	gate({});
	gate({}); // Duplicate external acknowledgement must not launch a second worker.
	ok &= expect(wait([&] {
		             return playback->snapshot().state == State::Playing &&
		                    output->locked([](auto &v) { return v.bytes.size() == 96; });
	             }) &&
	                 output->locked([](auto &v) {
		                 return v.opens == 1 && v.bytes.size() == 96 && v.configuration.deviceId == "fixture" &&
		                        v.configuration.bufferFrames == 512;
	                 }) &&
	                 view.findChild<QTabWidget *>()->currentIndex() == 3,
	             "one verified comp starts on the audition tab after one acknowledged handoff");
	const auto expected = prepareAudioRecordingAudition(base, view.importRequest());
	ok &= expect(
	    expected.succeeded() &&
	        exactSamples(output->locked([](auto &v) { return v.bytes; }),
	                     renderAudioSession(expected.imported.session, expected.first, expected.end).clip.samples) &&
	        view.imported().regionIds.isEmpty() && encodeAudioSession(base) == original && input->opens == 0 &&
	        permissions == 0,
	    "live review audition matches shared offline rendering without adopting clips or opening input");
	auto *pause = view.findChild<QPushButton *>("recordingAuditionPause");
	pause->click();
	ok &= expect(wait([&] { return playback->snapshot().state == State::Paused; }), "pause suspends review output");
	view.findChild<QDoubleSpinBox *>("recordingAuditionPosition")->setValue(7);
	ok &= expect(wait([&] { return output->locked([](auto &v) { return v.opens == 2; }); }) &&
	                 playback->snapshot().state == State::Paused,
	             "seek repositions while retaining pause");
	pause->click();
	ok &= expect(wait([&] { return playback->snapshot().state == State::Playing; }), "resume preserves review intent");
	output->locked([](auto &v) { v.processed = 8; });
	ok &= expect(wait([&] { return playback->snapshot().state == State::Ended; }),
	             "finite audition ends at the reviewed range");
	view.findChild<QCheckBox *>("recordingAuditionBacking")->setChecked(false);
	gate = {};
	play->click();
	ok &= wait([&] { return bool(gate); });
	gate({});
	ok &= wait([&] {
		return playback->snapshot().state == State::Playing &&
		       output->locked([](auto &v) { return v.bytes.size() == 96; });
	});
	const auto isolated = prepareAudioRecordingAudition(base, view.importRequest(), false);
	ok &= expect(
	    isolated.succeeded() &&
	        exactSamples(output->locked([](auto &v) { return v.bytes; }),
	                     renderAudioSession(isolated.imported.session, isolated.first, isolated.end).clip.samples),
	    "backing toggle auditions the isolated comp through the same mixer");
	view.findChild<QDoubleSpinBox *>("recordingTakeFirst")->setValue(9);
	ok &= expect(playback->snapshot().state == State::Stopped, "changing reviewed source fields stops stale audition");
	gate = {};
	view.findChild<QPushButton *>("recordingAuditionPlaySection")->click();
	ok &= wait([&] { return bool(gate); });
	gate({});
	ok &= wait([&] { return playback->snapshot().state == State::Playing; });
	QVector<float> sectionExpected;
	for (int i = 18; i < 24; ++i)
		sectionExpected << samples[size_t(i)];
	ok &= expect(exactSamples(output->locked([](auto &v) { return v.bytes; }), sectionExpected) &&
	                 playback->snapshot().position == 12,
	             "focused-section audition uses draft trims rather than queued comp cuts");
	stop->click();
	view.findChild<QCheckBox *>("recordingAuditionLoop")->setChecked(true);
	output->locked([](auto &v) { v.capacity = 24 * 8; });
	gate = {};
	play->click();
	ok &= wait([&] { return bool(gate); });
	gate({});
	ok &= wait([&] {
		return playback->snapshot().state == State::Playing &&
		       output->locked([](auto &v) { return v.bytes.size() == 192; });
	});
	auto repeated = renderAudioSession(isolated.imported.session, isolated.first, isolated.end).clip.samples;
	repeated += repeated;
	ok &= expect(exactSamples(output->locked([](auto &v) { return v.bytes; }), repeated),
	             "repeat auditions the comp continuously through the shared loop engine");
	stop->click();
	view.findChild<QCheckBox *>("recordingAuditionLoop")->setChecked(false);
	gate = {};
	play->click();
	ok &= wait([&] { return bool(gate); });
	gate({});
	stop->click();
	const auto beforeCancelledOpen = output->locked([](auto &v) { return v.opens; });
	ok &= expect(wait([&] { return !view.busy(); }) &&
	                 output->locked([&](auto &v) { return v.opens == beforeCancelledOpen; }),
	             "cancellation during verification prevents a late output open");
	const auto receiptBytes = bytes(QDir(folder).filePath("result.json"));
	ok &= expect(QFile::remove(QDir(folder).filePath("result.json")), "simulate changed reviewed receipt");
	gate = {};
	play->click();
	ok &= wait([&] { return bool(gate); });
	gate({});
	ok &= expect(wait([&] { return !view.busy(); }) && output->locked([&](auto &v) {
		return v.opens == beforeCancelledOpen;
	}) && view.findChild<QLabel *>("recordingStatus")->text().contains("changed"),
	             "stale receipt prevents preview playback");
	QFile restored(QDir(folder).filePath("result.json"));
	ok &= expect(restored.open(QIODevice::WriteOnly) && restored.write(receiptBytes) == receiptBytes.size(),
	             "restore owned receipt fixture");
	restored.close();
	output->locked([](auto &v) { v.rejectOpen = true; });
	gate = {};
	play->click();
	ok &= wait([&] { return bool(gate); });
	gate({});
	ok &= expect(wait([&] { return playback->snapshot().state == State::Error; }) &&
	                 view.findChild<QLabel *>("recordingAuditionStatus")->text().contains("Fixture open failure"),
	             "device failures stay visible without modifying the review");
	output->locked([](auto &v) { v.rejectOpen = false; });
	gate = {};
	play->click();
	ok &= wait([&] { return bool(gate); });
	gate({});
	ok &= wait([&] { return playback->snapshot().state == State::Playing; });
	view.findChild<QLineEdit *>("recordingPath")->setText(path("new-recording.vsrecord"));
	view.findChild<QComboBox *>("recordingInput")->setCurrentIndex(1);
	view.findChild<QComboBox *>("recordingOutput")->setCurrentIndex(1);
	auto *arms = view.findChild<QTreeWidget *>("recordingArms");
	arms->setCurrentItem(arms->topLevelItem(0));
	arms->currentItem()->setCheckState(0, Qt::Checked);
	gate = {};
	view.findChild<QPushButton *>("recordingRecord")->click();
	auto *unsaved = view.findChild<QMessageBox *>("recordingUnsavedReview");
	ok &= expect(unsaved && input->opens == 0, "Record preserves changed review choices until Save or Discard");
	if (!unsaved)
		return EXIT_FAILURE;
	unsaved->button(QMessageBox::Discard)->click();
	ok &= expect(wait([&] { return bool(gate); }) && input->opens == 0 &&
	                 output->locked([](auto &v) { return v.state == AudioStreamDeviceStatus::State::Closed; }),
	             "Record acknowledges actual audition shutdown before input can start");
	gate({});
	ok &= expect(wait([&] { return !view.busy(); }) && input->opens == 1,
	             "recording starts through normal permission and playback gates");
	view.close();
	ok &= expect(wait([&] { return !view.isVisible(); }), "review close joins audition shutdown");
	{
		auto closingOutput = std::make_shared<FakeAudioStreamState>();
		std::function<void(QString)> pending;
		AudioRecordingDialog closing(
		    base, {}, 3, 15, nullptr, recordingDeviceFactory(std::make_shared<RecordingFixture>()), {}, permission,
		    [&](QObject *, auto done) { pending = done; }, fakeAudioStreamFactory(closingOutput));
		closing.show();
		closing.inspectRecording(folder);
		auto *player = closing.findChild<AudioSessionPlayback *>("recordingAuditionPlayback");
		ok &= wait([&] { return !closing.busy() && player->available(); });
		closing.findChild<QPushButton *>("recordingAuditionPlayReview")->click();
		ok &= wait([&] { return bool(pending); });
		closing.close();
		ok &= expect(wait([&] { return !closing.isVisible(); }), "Close cancels an outstanding handoff");
		pending({});
		QCoreApplication::processEvents();
		ok &= expect(closingOutput->locked([](auto &v) { return v.opens == 0; }),
		             "late handoff cannot resurrect a closed review");
	}
	{
		AudioRecordingDialog disabled(base, {}, 3, 15, nullptr,
		                              recordingDeviceFactory(std::make_shared<RecordingFixture>()), {}, permission, {},
		                              [] { return std::unique_ptr<AudioStreamDevice>{}; });
		disabled.inspectRecording(folder);
		ok &= expect(wait([&] { return !disabled.busy(); }) &&
		                 !disabled.findChild<QPushButton *>("recordingAuditionPlayReview")->isEnabled() &&
		                 disabled.findChild<QPushButton *>("recordingImport")->isEnabled(),
		             "unavailable output disables audition without blocking reviewed import");
	}
	{
		AudioProjectSaveRequest save;
		save.path = path("base.vssession");
		ok &= expect(writeAudioSession(base, save).succeeded, "write parent session fixture");
		auto childOutput = std::make_shared<FakeAudioStreamState>();
		AudioSessionDialog parent(nullptr, fakeAudioStreamFactory());
		parent.setAttribute(Qt::WA_DeleteOnClose, false);
		parent.setRecoveryEnabled(false);
		ok &= expect(parent.openSession(save.path) && wait([&] { return !parent.isBusy(); }) &&
		                 parent.openRecording(folder, recordingDeviceFactory(std::make_shared<RecordingFixture>()), {},
		                                      permission, fakeAudioStreamFactory(childOutput)),
		             "open parent-coordinated audition review");
		auto *review = parent.findChild<AudioRecordingDialog *>();
		if (!review)
			return EXIT_FAILURE;
		auto *player = review->findChild<AudioSessionPlayback *>("recordingAuditionPlayback");
		ok &= wait([&] { return !review->busy() && player->available(); });
		comp(*review);
		review->findChild<QPushButton *>("recordingAuditionPlayReview")->click();
		ok &= expect(wait([&] { return player->snapshot().state == State::Playing; }) &&
		                 encodeAudioSession(parent.session()) == original && !parent.hasChanges(),
		             "audition never creates parent undo or dirty state");
		review->findChild<QPushButton *>("recordingImport")->click();
		ok &= expect(wait([&] { return !parent.isBusy(); }) && parent.session().sources.size() == 4 &&
		                 childOutput->locked([](auto &v) { return v.state == AudioStreamDeviceStatus::State::Closed; }),
		             "import stops and acknowledges output before parent adoption");
		parent.undo();
		ok &= expect(encodeAudioSession(parent.session()) == original, "audition adds no undo step before comp import");
	}
	ok &= expect(bytes(audioRecordingArmPath(folder, 0)) == journalBytes && encodeAudioSession(base) == original &&
	                 permissions == 1,
	             "all audition operations retain the source journal and session");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
