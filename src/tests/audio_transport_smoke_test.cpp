#include "app/audio_editor_dialog.h"
#include "app/audio_playback.h"
#include "app/audio_waveform_view.h"
#include "app/studio_theme.h"
#include "core/audio_export.h"
#include "core/studio_settings.h"
#include "tests/fake_audio_playback.h"
#include "vibestudio_config.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
#include <QAudioDecoder>
#include <QAudioFormat>
#endif
#include <algorithm>
#include <bit>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool waitUntil(const std::function<bool()>& done, int milliseconds = 10000)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (!done() && elapsed.elapsed() < milliseconds) {
		QEventLoop loop;
		QTimer::singleShot(5, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return done();
}
class ExpandedTranslator final : public QTranslator
{
public:
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!QByteArray(context).contains("Audio")) { return {}; }
		const auto text = QString::fromUtf8(source);
		return text + QStringLiteral(" · ") + text;
	}
};

bool lifecycle()
{
	bool ok = true;
	auto backend = std::make_unique<FakeAudioPlaybackBackend>();
	auto* fake = backend.get();
	fake->autoReady = false;
	AudioPlayback playback(nullptr, std::move(backend), 30);
	const QByteArray bytes("prepared-wave"); // Opaque to this lifecycle controller.
	int failures = 0;
	QObject::connect(&playback, &AudioPlayback::failed, &playback, [&](const QString&) { ++failures; });
	ok &= expect(playback.start(bytes, 3, 88203, 44100) && playback.state() == AudioPlayback::State::Loading &&
	                 playback.positionFrame() == 3 && !playback.canSeek(),
	             "loading is explicit and initial offset is an exact frame");
	const quint64 firstSession = fake->currentSession;
	playback.pause();
	playback.resume();
	ok &= expect(fake->plays == 0 && !playback.seekToFrame(100), "loading cannot seek or resume prematurely");
	emit fake->ready(firstSession, true);
	ok &= expect(playback.state() == AudioPlayback::State::Playing && fake->plays == 1 && playback.canSeek(),
	             "ready starts playback exactly once");
	emit fake->ready(firstSession, true);
	ok &= expect(fake->plays == 1, "repeated loaded/buffered notifications do not restart the sound");
	emit fake->positionChanged(firstSession, 500);
	ok &= expect(playback.positionFrame() == 22053, "reported time includes the exact trimmed offset");
	playback.pause();
	ok &= expect(playback.state() == AudioPlayback::State::Paused, "pause preserves a resumable session");
	ok &= expect(playback.seekToFrame(22054) && fake->lastSeek == 500 && playback.positionFrame() == 22053 &&
	                 playback.state() == AudioPlayback::State::Paused,
	             "paused seek reports the millisecond-aligned frame and does not resume");
	emit fake->ready(firstSession, true);
	ok &= expect(fake->plays == 1, "late buffered notification cannot resume a paused sound");
	playback.setLoop(true);
	playback.setVolume(0.25f);
	ok &= expect(fake->loop && fake->volume == 0.25f, "loop and volume updates reach a live session");
	playback.setVolume(std::numeric_limits<float>::quiet_NaN());
	ok &= expect(fake->volume == 0.25f, "non-finite monitor volume is ignored");
	playback.resume();
	ok &= expect(playback.state() == AudioPlayback::State::Playing && fake->plays == 2, "resume retains the session");
	emit fake->seekableChanged(firstSession, false);
	const int previousSeeks = fake->seeks;
	ok &= expect(!playback.seekToFrame(40000) && fake->seeks == previousSeeks, "non-seekable media never receives a seek");
	emit fake->seekableChanged(firstSession, true);
	ok &= expect(playback.seekToFrame(std::numeric_limits<qint64>::min()) && playback.positionFrame() == 3,
	             "negative seeks clamp without overflow");
	emit fake->positionChanged(firstSession, std::numeric_limits<qint64>::max());
	ok &= expect(playback.positionFrame() == 88203, "oversized backend positions clamp before multiplication");
	ok &= expect(playback.seekToFrame(88203) && !playback.active() && playback.positionFrame() == 88203 &&
	                 fake->bytes.isEmpty(), "seeking to the exclusive end stops and releases audio");

	// Old backend notifications must not alter a newer source, even after stop.
	ok &= expect(playback.start(bytes, 7, 1007, 1000), "new session can start");
	const auto secondSession = fake->currentSession;
	emit fake->ready(firstSession, true);
	emit fake->positionChanged(firstSession, 99);
	emit fake->ended(firstSession);
	emit fake->failed(firstSession, QStringLiteral("old failure"));
	ok &= expect(playback.state() == AudioPlayback::State::Loading && playback.positionFrame() == 7 &&
	                 fake->plays == 2 && failures == 0, "late source callbacks are rejected");
	emit fake->ready(secondSession, true);
	emit fake->stateChanged(secondSession, AudioPlaybackBackend::State::Stopped);
	ok &= expect(playback.start(bytes, 11, 1011, 1000), "restart races safely with a queued backend stop");
	const auto thirdSession = fake->currentSession;
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Loading, "old queued stop cannot clear the new session");
	emit fake->ready(thirdSession, true);
	playback.setLoop(false);
	emit fake->stateChanged(thirdSession, AudioPlaybackBackend::State::Stopped);
	emit fake->ended(thirdSession);
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Stopped && playback.positionFrame() == 1011,
	             "natural end shows the exact selection end, ignoring stop's reset to zero");

	ok &= expect(playback.start(bytes, 0, 1000, 1000), "failure fixture starts");
	emit fake->failed(fake->currentSession, {});
	ok &= expect(playback.state() == AudioPlayback::State::Error && !playback.errorString().isEmpty() &&
	                 fake->bytes.isEmpty() && failures == 1, "empty backend errors still release audio and explain failure");
	fake->openError = QStringLiteral("No output device fixture");
	ok &= expect(!playback.start(bytes, 0, 1000, 1000) && playback.errorString() == fake->openError && failures == 2,
	             "synchronous device failure is not overwritten by a playing status");
	fake->openError.clear();
	ok &= expect(playback.start(bytes, 0, 1000, 1000) && playback.errorString().isEmpty(), "Play retries after failure");
	ok &= expect(waitUntil([&]() { return playback.state() == AudioPlayback::State::Error; }, 500) &&
	                 playback.errorString().contains(QStringLiteral("timed out")) && failures == 3,
	             "a backend that never becomes ready times out and releases the source");

	fake->autoReady = true;
	ok &= expect(playback.start(bytes, 3, 1003, 1000), "buffering fixture starts");
	emit fake->stalledChanged(fake->currentSession, true);
	playback.pause();
	QEventLoop pauseWait;
	QTimer::singleShot(50, &pauseWait, &QEventLoop::quit);
	pauseWait.exec();
	ok &= expect(playback.state() == AudioPlayback::State::Paused, "intentional pause does not time out");
	playback.resume();
	ok &= expect(waitUntil([&]() { return playback.state() == AudioPlayback::State::Error; }, 500) && failures == 4,
	             "a stalled resumed session has a finite timeout");
	ok &= expect(playback.start(bytes, 3, 1003, 1000), "buffer recovery fixture starts");
	emit fake->stalledChanged(fake->currentSession, true);
	emit fake->stalledChanged(fake->currentSession, false);
	QEventLoop recoveredWait;
	QTimer::singleShot(50, &recoveredWait, &QEventLoop::quit);
	recoveredWait.exec();
	ok &= expect(playback.state() == AudioPlayback::State::Playing && !playback.stalled(),
	             "recovered buffering cancels the timeout");
	playback.stop();
	const int opens = fake->opens;
	ok &= expect(!playback.start(bytes, -1, 1000, 1000) &&
	                 !playback.start(bytes, 0, std::numeric_limits<qint64>::max(), 1000) &&
	                 !playback.start({}, 0, 1000, 1000) && !playback.start(bytes, 0, 1000, 0) &&
	                 fake->opens == opens, "invalid prepared ranges never enter the backend");
	const auto cancelOnLoading = QObject::connect(&playback, &AudioPlayback::changed, &playback, [&]() {
		if (playback.state() == AudioPlayback::State::Loading) { playback.stop(); }
	});
	ok &= expect(!playback.start(bytes, 0, 1000, 1000) && fake->opens == opens,
	             "synchronous loading cancellation cannot publish a source afterward");
	QObject::disconnect(cancelOnLoading);
	return ok;
}

bool loopFallback()
{
	bool ok = true;
	auto backend = std::make_unique<FakeAudioPlaybackBackend>();
	auto* fake = backend.get();
	AudioPlayback playback(nullptr, std::move(backend), 30);
	const QByteArray bytes("prepared-loop");
	playback.setLoop(true);
	playback.setVolume(0.25f);
	const auto start = [&]() { return playback.start(bytes, 3, 1003, 1000); };
	const auto ended = [&](bool stoppedFirst) {
		fake->state = AudioPlaybackBackend::State::Stopped;
		if (stoppedFirst) { emit fake->stateChanged(fake->currentSession, fake->state); }
		emit fake->ended(fake->currentSession);
		if (!stoppedFirst) { emit fake->stateChanged(fake->currentSession, fake->state); }
	};
	ok &= expect(start(), "loop fallback fixture starts");
	for (const bool stoppedFirst : {true, false}) {
		const auto session = fake->currentSession;
		const auto plays = fake->plays;
		ended(stoppedFirst);
		emit fake->ended(session);
		ok &= expect(playback.state() == AudioPlayback::State::Loading && fake->plays == plays,
		             "loop restart is queued outside end callbacks and duplicate EOF is coalesced");
		QApplication::processEvents();
		ok &= expect(playback.state() == AudioPlayback::State::Playing && fake->plays == plays + 1 &&
		                 fake->lastSeek == 0 && playback.positionFrame() == 3 && fake->currentSession == session &&
		                 fake->bytes == bytes && fake->loop && fake->volume == 0.25f,
		             "either EOF/Stopped ordering restarts at the exact selection start with the same source and controls");
	}
	ended(true);
	const auto stoppedPlays = fake->plays;
	playback.stop();
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Stopped && fake->plays == stoppedPlays && fake->bytes.isEmpty(),
	             "Stop cancels a pending loop restart");
	ok &= expect(start(), "loop cancellation fixture starts");
	ended(true);
	const auto disabledPlays = fake->plays;
	playback.setLoop(false);
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Stopped && playback.positionFrame() == 1003 &&
	                 fake->plays == disabledPlays, "turning Loop off before a queued repeat retains the exact end");
	playback.setLoop(true);
	ok &= expect(start(), "source replacement fixture starts");
	ended(true);
	ok &= expect(playback.start(bytes, 21, 1021, 1000), "new source supersedes the pending repeat");
	const auto replacementPlays = fake->plays;
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Playing && playback.positionFrame() == 21 &&
	                 fake->plays == replacementPlays, "old loop callbacks cannot seek or restart a new session");
	ended(true);
	const auto failedPlays = fake->plays;
	emit fake->failed(fake->currentSession, QStringLiteral("loop failure"));
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Error && fake->plays == failedPlays && fake->bytes.isEmpty(),
	             "a late failure wins over a pending loop restart");
	fake->seekable = false;
	ok &= expect(start(), "non-seekable loop fixture starts");
	const auto nonSeekableSeeks = fake->seeks;
	ended(true);
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Error && fake->seeks == nonSeekableSeeks &&
	                 playback.errorString().contains(QStringLiteral("Loop off")),
	             "a backend without repeat/seek support gives an actionable error instead of silently stopping");
	fake->seekable = true;
	ok &= expect(start(), "seek callback fixture starts");
	const auto pausedDuringSeek = QObject::connect(fake, &AudioPlaybackBackend::positionChanged, &playback,
	    [&](quint64 session, qint64) { emit fake->stateChanged(session, AudioPlaybackBackend::State::Paused); });
	ended(true);
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Playing, "a backend pause during loop rewind does not cancel the repeat");
	QObject::disconnect(pausedDuringSeek);
	const auto disableDuringSeek = QObject::connect(fake, &AudioPlaybackBackend::positionChanged, &playback,
	    [&](quint64, qint64) { playback.setLoop(false); });
	ended(true);
	const auto seekCancelledPlays = fake->plays;
	QApplication::processEvents();
	ok &= expect(playback.state() == AudioPlayback::State::Stopped && playback.positionFrame() == 1003 &&
	                 fake->plays == seekCancelledPlays, "turning Loop off inside a rewind callback prevents a later play");
	QObject::disconnect(disableDuringSeek);
	playback.setLoop(true);
	ok &= expect(start(), "loop timeout fixture starts");
	fake->autoState = false;
	ended(true);
	QApplication::processEvents();
	ok &= expect(waitUntil([&]() { return playback.state() == AudioPlayback::State::Error; }, 500) &&
	                 playback.errorString().contains(QStringLiteral("timed out")) && fake->bytes.isEmpty(),
	             "a loop restart that never plays has a finite timeout and releases the source");
	return ok;
}

bool qtFloatDecode()
{
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
	bool ok = true;
	for (int channels : {1, 2, 6}) {
		AudioClip clip{channels, 44100, {}};
		clip.samples.resize(1000 * channels);
		for (qsizetype index = 0; index < clip.samples.size(); ++index) {
			clip.samples[index] = index % 3 == 0 ? 1.234567f : index % 3 == 1 ? -0.1234567f : 1.0e-8f;
		}
		// This uses Qt's actual codec path without an output device or player.
		QBuffer source;
		source.setData(encodeAudioWav(clip, {AudioWavFormat::Float32}));
		source.open(QIODevice::ReadOnly);
		QAudioDecoder decoder;
		if (!expect(decoder.isSupported(), "Qt build must provide the configured playback codec backend")) { return false; }
		QAudioFormat format;
		format.setSampleRate(clip.sampleRate);
		format.setChannelCount(channels);
		format.setSampleFormat(QAudioFormat::Float);
		decoder.setAudioFormat(format);
		decoder.setSourceDevice(&source);
		QVector<float> decoded;
		bool finished = false;
		QString error;
		QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &decoder,
		                 [&](QAudioDecoder::Error) { error = decoder.errorString(); });
		QObject::connect(&decoder, &QAudioDecoder::finished, &decoder, [&]() { finished = true; });
		QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]() {
			const auto buffer = decoder.read();
			if (buffer.format().sampleFormat() != QAudioFormat::Float || buffer.format().channelCount() != channels ||
			    buffer.format().sampleRate() != clip.sampleRate) {
				error = QStringLiteral("Qt decoder returned a different requested sample layout");
				return;
			}
			const auto offset = decoded.size();
			decoded.resize(offset + buffer.sampleCount());
			std::copy_n(buffer.constData<float>(), buffer.sampleCount(), decoded.begin() + offset);
		});
		decoder.start();
		ok &= expect(waitUntil([&]() { return finished || !error.isEmpty(); }), "Qt float decoder finishes within its test deadline");
		decoder.stop();
		if (!error.isEmpty()) { std::cerr << error.toStdString() << '\n'; }
		ok &= expect(finished && error.isEmpty() && decoded == clip.samples,
		             "actual Qt codec preserves mono/stereo/multichannel float audition samples and frame count");
	}
	return ok;
#else
	return true;
#endif
}

bool editorTransport(QApplication& app)
{
	bool ok = true;
	AudioClip input{2, 44100, {}};
	input.samples.resize(88200 * 2);
	const float values[] = {1.0e-8f, -1.234567f, 0.1234567f, std::bit_cast<float>(0x80000000u)};
	for (qsizetype index = 0; index < input.samples.size(); ++index) { input.samples[index] = values[index % 4]; }
	const auto source = encodeAudioWav(input, {AudioWavFormat::Float32});
	for (int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::HighContrastDark : StudioTheme::HighContrastLight,
		                                       UiDensity::Standard, scale));
		ExpandedTranslator expanded;
		if (scale == 200) { app.installTranslator(&expanded); }
		auto backend = std::make_unique<FakeAudioPlaybackBackend>();
		[[maybe_unused]] auto* fake = backend.get();
		AudioEditorDialog editor(nullptr, std::move(backend));
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		if (scale == 200) { editor.setLayoutDirection(Qt::RightToLeft); }
		editor.setRecoveryEnabled(false);
		editor.resize(980, 850);
		editor.show();
		ok &= expect(editor.loadSource(QStringLiteral("float-fixture.wav"), {}, [source](QString*) { return source; }) &&
		                 waitUntil([&]() { return !editor.isBusy(); }), "load exact float audio for transport UI");
		editor.setSelection(3, 44103);
		auto* play = editor.findChild<QAction*>(QStringLiteral("audioEditorPlay"));
		auto* stop = editor.findChild<QAction*>(QStringLiteral("audioEditorStop"));
		auto* seek = editor.findChild<QSlider*>(QStringLiteral("audioPlaybackSeek"));
		auto* frame = editor.findChild<QSpinBox*>(QStringLiteral("audioPlaybackFrame"));
		auto* volume = editor.findChild<QSlider*>(QStringLiteral("audioPlaybackVolume"));
		auto* loop = editor.findChild<QCheckBox*>(QStringLiteral("audioPlaybackLoop"));
		auto* clock = editor.findChild<QLabel*>(QStringLiteral("audioPlaybackPosition"));
		auto* waveform = editor.findChild<AudioWaveformView*>();
		auto* transport = editor.findChild<AudioPlayback*>();
		ok &= expect(play && stop && seek && frame && volume && loop && clock && waveform && transport,
		             "transport provides named standard controls");
		if (!(play && stop && seek && frame && volume && loop && clock && waveform && transport)) { return false; }
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
		int starts = 0;
		editor.beforePlayback = [&]() { ++starts; };
		volume->setValue(0);
		loop->setChecked(true);
		play->trigger();
		ok &= expect(editor.isBusy() && stop->isEnabled(), "Stop can cancel playback preparation");
		ok &= expect(waitUntil([&]() { return !editor.isBusy(); }), "playback prepares asynchronously");
		const auto decoded = decodeAudioClip(QStringLiteral("prepared.wav"), fake->bytes);
		const auto selected = input.samples.mid(6, 44100 * 2);
		bool exact = decoded.succeeded() && decoded.clip.samples.size() == selected.size();
		for (qsizetype index = 0; exact && index < selected.size(); ++index) {
			exact &= std::bit_cast<quint32>(decoded.clip.samples[index]) == std::bit_cast<quint32>(selected[index]);
		}
		ok &= expect(exact && starts == 1 && fake->loop && fake->volume == 0 && waveform->playheadFrame() == 3,
		             "audition preserves float bits, headroom, sub-16-bit detail, negative zero and exact boundaries");
		ok &= expect(seek->isEnabled() && frame->isEnabled(), "live seek controls are accessible");
		seek->setValue(2003);
		ok &= expect(fake->lastSeek == 45 && waveform->playheadFrame() == 1987 &&
		                 waveform->selectionStart() == 3 && waveform->selectionEnd() == 44103,
		             "live seeking preserves selection and reflects backend precision");
		play->trigger();
		frame->setValue(3100);
		ok &= expect(transport->state() == AudioPlayback::State::Paused && fake->lastSeek == 70 &&
		                 frame->value() == 3090 && starts == 1, "paused frame entry seeks without resuming or changing selection");
		play->trigger();
		ok &= expect(starts == 2 && transport->state() == AudioPlayback::State::Playing,
		             "resume coordinates with other studio audio");
		loop->setChecked(false);
		ok &= expect(!fake->loop, "loop changes affect the active audition");
		emit fake->failed(fake->currentSession, QStringLiteral("Disconnected output fixture"));
		ok &= expect(play->isEnabled() && !stop->isEnabled() && !seek->isEnabled() &&
		                 transport->state() == AudioPlayback::State::Error, "device failure leaves Play available for retry");
		play->trigger();
		ok &= expect(waitUntil([&]() { return !editor.isBusy(); }) && transport->state() == AudioPlayback::State::Playing,
		             "retry after a device error works");
		loop->setChecked(true);
		emit fake->seekableChanged(fake->currentSession, false);
		emit fake->stateChanged(fake->currentSession, AudioPlaybackBackend::State::Stopped);
		emit fake->ended(fake->currentSession);
		QApplication::processEvents();
		auto* errorStatus = editor.findChild<QLabel*>(QStringLiteral("audioEditorStatus"));
		const bool errorFits = errorStatus && waitUntil([&]() {
			return errorStatus->height() >= errorStatus->heightForWidth(errorStatus->width());
		}, 1000);
		ok &= expect(transport->state() == AudioPlayback::State::Error && play->isEnabled() && !stop->isEnabled() &&
		                 errorStatus && errorStatus->text().contains(QStringLiteral("Loop off")) &&
		                 errorStatus->accessibleDescription() == errorStatus->text() &&
		                 errorFits,
		             "unsupported loop errors remain readable, accessible and retryable in scaled/expanded layouts");
		if (!errorFits && errorStatus) {
			std::cerr << "Loop error geometry at " << scale << "%: " << errorStatus->width() << 'x'
			          << errorStatus->height() << " needs " << errorStatus->heightForWidth(errorStatus->width()) << '\n';
		}
		loop->setChecked(false);
		stop->trigger();
		const int opens = fake->opens;
		play->trigger();
		stop->trigger();
		ok &= expect(waitUntil([&]() { return !editor.isBusy(); }) && fake->opens == opens && !transport->active(),
		             "Stop during preparation prevents delayed autoplay");
		play->trigger();
		editor.stopPlayback();
		ok &= expect(waitUntil([&]() { return !editor.isBusy(); }) && fake->opens == opens && !transport->active(),
		             "browser handoff also cancels pending preparation");
		ok &= expect(!editor.hasChanges() && editor.clip().samples == input.samples,
		             "transport operations never dirty or alter the document");
		play->trigger();
		ok &= expect(waitUntil([&]() { return !editor.isBusy(); }), "prepare final layout state");
#else
		ok &= expect(!play->isEnabled() && !seek->isEnabled() && !frame->isEnabled() && !volume->isEnabled() &&
		                 !loop->isEnabled(), "no-playback build exposes disabled transport without blocking editing");
		editor.applyEdit(QStringLiteral("invert"));
		ok &= expect(waitUntil([&]() { return !editor.isBusy(); }) && editor.hasChanges(),
		             "core editing remains available without playback");
		editor.undo();
#endif
		auto* scroll = editor.findChild<QScrollArea*>();
		scroll->ensureWidgetVisible(frame);
		QApplication::processEvents();
		ok &= expect(frame->focusPolicy() != Qt::NoFocus && seek->focusPolicy() != Qt::NoFocus &&
		                 QAccessible::queryAccessibleInterface(frame) && !frame->accessibleName().isEmpty() &&
		                 clock->height() >= clock->heightForWidth(clock->width()),
		             "transport controls expose focus, names and uncut expanded status text");
		const auto directory = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!directory.isEmpty()) {
			QDir().mkpath(directory);
			QImage capture(editor.size(), QImage::Format_ARGB32_Premultiplied);
			capture.fill(Qt::transparent);
			editor.render(&capture);
			ok &= expect(capture.save(QDir(directory).filePath(QStringLiteral("audio-transport-%1.png").arg(scale))),
			             "save direct widget render");
		}
		editor.stopPlayback();
		editor.close();
		if (scale == 200) { app.removeTranslator(&expanded); }
	}
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	// All interactions are direct widget calls and fake media events. No physical
	// input, screen capture, audio output or system clipboard access is used.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-transport-XXXXXX")));
	if (!temporary.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	qputenv("VIBESTUDIO_AUDIO_RECOVERY_ROOT", QDir(temporary.path()).filePath(QStringLiteral("recovery")).toUtf8());
	const bool core = lifecycle();
	const bool loops = loopFallback();
	const bool codec = qtFloatDecode();
	const bool ui = editorTransport(app);
	std::cout << "Audio transport lifecycle, loop fallback, codec and editor checks "
	          << (core && loops && codec && ui ? "passed" : "failed") << '\n';
	return core && loops && codec && ui ? EXIT_SUCCESS : EXIT_FAILURE;
}
