#include "app/audio_editor_dialog.h"
#include "core/audio_export.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_playback.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool waitUntil(const std::function<bool()>& done, int timeoutMs = 30000)
{
	QElapsedTimer timer; timer.start();
	while (!done() && timer.elapsed() < timeoutMs) {
		QEventLoop loop; QTimer::singleShot(5, &loop, &QEventLoop::quit); loop.exec();
	}
	return done();
}
bool finish(AudioEditorDialog& editor)
{
	const bool done = waitUntil([&] { return !editor.isBusy(); });
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	return expect(done, "audio operation completes without blocking the GUI event loop");
}
QByteArray readFile(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool sameBits(const AudioClip& first, const AudioClip& second)
{
	return first.channels == second.channels && first.sampleRate == second.sampleRate &&
	       first.markers == second.markers && first.samples.size() == second.samples.size() &&
	       (first.samples.isEmpty() || std::memcmp(first.samples.constData(), second.samples.constData(),
	                                             static_cast<size_t>(first.samples.size()) * sizeof(float)) == 0);
}
bool workerFailures(QApplication& app, const QString& root)
{
	AudioEditorDialog editor(nullptr, std::make_unique<FakeAudioPlaybackBackend>());
	AudioClip source{1, 44100, {0.0f, -0.0f, 0.125f, -0.25f, 0.5f, 0.25f, -0.5f, 0.0f}};
	AudioWavOptions options; options.format = AudioWavFormat::Float32;
	const auto bytes = encodeAudioWav(source, options);
	bool ok = expect(editor.loadSource(QStringLiteral("fixture.wav"), {}, [bytes](QString*) { return bytes; }), "load exact float fixture");
	ok &= finish(editor);
	editor.applyEdit(QStringLiteral("gain"), -3); ok &= finish(editor);
	const QString path = QDir(root).filePath(QStringLiteral("worker-failure.vsaudio"));
	ok &= expect(editor.saveProjectTo(path), "save a document with retained undo history"); ok &= finish(editor);
	editor.setSelection(2, 6);
	const auto saved = editor.clip();
	const auto disk = readFile(path);
	auto* undo = editor.findChild<QAction*>(QStringLiteral("audioEditorUndo"));
	auto* redo = editor.findChild<QAction*>(QStringLiteral("audioEditorRedo"));
	auto* status = editor.findChild<QLabel*>(QStringLiteral("audioEditorStatus"));
	auto* first = editor.findChild<QSpinBox*>(QStringLiteral("audioSelectionStart"));
	auto* end = editor.findChild<QSpinBox*>(QStringLiteral("audioSelectionEnd"));
	const QString undoText = undo->text();
	const auto intact = [&](bool canRedo = false) {
		return sameBits(editor.clip(), saved) && !editor.hasChanges() && editor.projectPath() == path &&
		       first->value() == 2 && end->value() == 6 && undo->isEnabled() && redo->isEnabled() == canRedo &&
		       undo->text() == undoText && readFile(path) == disk;
	};
	for (int kind = 0; kind < 3; ++kind) {
		std::atomic_bool onWorker = false;
		ok &= expect(editor.loadSource(QStringLiteral("failure.wav"), {}, [&](QString*) -> QByteArray {
			onWorker = QThread::currentThread() != app.thread();
			if (kind == 0) { throw std::bad_alloc{}; }
			if (kind == 1) { throw std::runtime_error("fixture failure"); }
			throw 7;
		}), "start failing reader on the normal editor worker");
		ok &= finish(editor);
		const QString message = kind == 0 ? QStringLiteral("Not enough memory")
			: kind == 1 ? QStringLiteral("fixture failure") : QStringLiteral("failed unexpectedly");
		ok &= expect(onWorker && status->text().contains(message) && intact(),
		             "worker exceptions preserve sample bits, selection, project, saved revision and history");
		editor.undo();
		ok &= expect(editor.hasChanges() && sameBits(editor.clip(), source), "failure leaves Undo operational");
		editor.redo();
		ok &= expect(intact(), "failure leaves Redo and the saved revision operational");
	}
	std::atomic_bool entered = false, release = false;
	ok &= expect(editor.loadSource(QStringLiteral("late.wav"), {}, [&](QString*) -> QByteArray {
		entered = true;
		while (!release) { QThread::msleep(1); }
		throw std::runtime_error("late cancelled reader");
	}), "start controlled reader");
	ok &= expect(waitUntil([&] { return entered.load(); }), "controlled reader entered");
	editor.cancelWork(); release = true;
	ok &= finish(editor);
	ok &= expect(intact() && status->text().startsWith(QStringLiteral("Cancelled")),
	             "cancellation wins over a late worker failure without changing the document");
	editor.applyEdit(QStringLiteral("gain"), 0); ok &= finish(editor);
	ok &= expect(intact() && status->text().contains(QStringLiteral("did not change")),
	             "an exact no-op retains the existing history and saved revision");
	editor.applyEdit(QStringLiteral("gain"), -1); ok &= finish(editor);
	ok &= expect(editor.hasChanges(), "editing can continue after worker failures");
	editor.undo();
	ok &= expect(intact(true), "undo after retry returns to the exact saved state and retains Redo");
	return ok;
}
bool signedZero(const QString& root)
{
	AudioEditorDialog editor(nullptr, std::make_unique<FakeAudioPlaybackBackend>());
	bool ok = editor.createNew(44100, 1, 16) && finish(editor);
	ok &= editor.saveProjectTo(QDir(root).filePath(QStringLiteral("zero.vsaudio"))) && finish(editor);
	editor.applyEdit(QStringLiteral("invert")); ok &= finish(editor);
	ok &= expect(editor.hasChanges() && std::all_of(editor.clip().samples.cbegin(), editor.clip().samples.cend(),
	             [](float value) { return value == 0 && std::signbit(value); }),
	             "polarity edits retain changed float zero bits instead of dropping them as a no-op");
	editor.undo();
	ok &= expect(!editor.hasChanges() && !std::signbit(editor.clip().samples.first()), "undo restores saved positive zero bits");
	editor.redo();
	ok &= expect(editor.hasChanges() && std::signbit(editor.clip().samples.first()), "redo restores negative zero bits");
	return ok;
}
bool historyCount(const QString& root)
{
	AudioEditorDialog editor(nullptr, std::make_unique<FakeAudioPlaybackBackend>());
	bool ok = editor.createNew(44100, 1, 64) && finish(editor);
	ok &= editor.saveProjectTo(QDir(root).filePath(QStringLiteral("history.vsaudio"))) && finish(editor);
	const auto* sharedSamples = editor.clip().samples.constData();
	for (int index = 0; index < 36; ++index) {
		AudioMarkers markers; markers.cues.push_back({1, index, QString::number(index)});
		ok &= editor.setMarkers(markers);
	}
	ok &= expect(editor.clip().samples.constData() == sharedSamples, "marker history retains shared sample storage");
	auto* undo = editor.findChild<QAction*>(QStringLiteral("audioEditorUndo"));
	auto* redo = editor.findChild<QAction*>(QStringLiteral("audioEditorRedo"));
	int undos = 0;
	while (undo->isEnabled() && undos < 40) { editor.undo(); ++undos; }
	ok &= expect(undos == 32 && editor.clip().markers.cues.first().name == QStringLiteral("3") && editor.hasChanges(),
	             "history retains the nearest 32 edits and cannot fabricate an evicted saved revision");
	int redos = 0;
	while (redo->isEnabled() && redos < 40) { editor.redo(); ++redos; }
	ok &= expect(redos == 32 && editor.clip().markers.cues.first().name == QStringLiteral("35"),
	             "redo traverses every retained marker state in order");
	editor.undo(); editor.undo();
	AudioMarkers branch; branch.cues.push_back({7, 9, QStringLiteral("branch")});
	ok &= editor.setMarkers(branch);
	ok &= expect(!redo->isEnabled() && undo->isEnabled(), "a new edit discards the old redo branch");
	return ok;
}
bool historyStorage()
{
	AudioEditorDialog editor(nullptr, std::make_unique<FakeAudioPlaybackBackend>());
	bool ok = editor.createNew(44100, 1, AudioSampleLimit) && finish(editor);
	if (!ok) { return false; }
	const auto measured = [&](const QString& operation, double gain = 0) {
		QElapsedTimer clock; clock.start();
		qint64 last = 0, maximumGap = 0; int pulses = 0;
		QTimer heartbeat;
		QObject::connect(&heartbeat, &QTimer::timeout, &editor, [&] {
			const qint64 now = clock.elapsed(); maximumGap = std::max(maximumGap, now - last); last = now; ++pulses;
		});
		heartbeat.start(5); editor.applyEdit(operation, gain);
		const bool finished = finish(editor); heartbeat.stop();
		maximumGap = std::max(maximumGap, clock.elapsed() - last);
		std::cout << "History workload: operation=" << operation.toStdString() << " samples=" << AudioSampleLimit
		          << " elapsedMs=" << clock.elapsed() << " guiTimerTicks=" << pulses << " maxGuiGapMs=" << maximumGap << '\n';
		return expect(finished && pulses > 0, "near-limit edit keeps the GUI event loop running");
	};
	for (int index = 0; index < 5; ++index) { ok &= measured(QStringLiteral("invert")); }
	const auto* samples = editor.clip().samples.constData();
	ok &= measured(QStringLiteral("gain"), 0);
	ok &= expect(editor.clip().samples.constData() == samples, "near-limit no-op keeps the original samples and cache");
	auto* undo = editor.findChild<QAction*>(QStringLiteral("audioEditorUndo"));
	auto* redo = editor.findChild<QAction*>(QStringLiteral("audioEditorRedo"));
	int undos = 0;
	while (undo->isEnabled() && undos < 8) { editor.undo(); ++undos; }
	// Each state has 64 MiB of unique PCM plus its extrema cache. The documented
	// 256 MiB history budget permits three of these complete prior states.
	ok &= expect(undos == 3 && !std::signbit(editor.clip().samples.first()),
	             "the byte budget evicts distant large states and retains the nearest undo operations");
	int redos = 0;
	while (redo->isEnabled() && redos < 8) { editor.redo(); ++redos; }
	ok &= expect(redos == 3 && std::signbit(editor.clip().samples.first()), "redo remains bounded and restores exact large-state bits");
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	// Direct Qt calls and a fake backend; no input injection or output device.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath()); QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-history-XXXXXX")));
	if (!temporary.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	StudioSettings().setAudioRecoveryEnabled(false);
	const bool failures = workerFailures(app, temporary.path());
	const bool zero = signedZero(temporary.path()), count = historyCount(temporary.path()), storage = historyStorage();
	std::cout << "Audio history checks " << (failures && zero && count && storage ? "passed" : "failed") << '\n';
	return failures && zero && count && storage ? EXIT_SUCCESS : EXIT_FAILURE;
}
