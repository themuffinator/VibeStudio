// Speech, screen reader announcements, and status message timing, without a
// sound: the "log" speech engine records what would be said.

#include "app/studio_accessibility.h"
#include "app/studio_speech.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QStatusBar>
#include <QTimer>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
	}
	return condition;
}

void wait(int milliseconds)
{
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < milliseconds) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
	}
}

bool checkLoggingEngine()
{
	bool ok = true;
	qputenv("VIBESTUDIO_SPEECH_ENGINE", "log");
	clearLoggedSpeechForTesting();
	StudioSpeech speech;
	ok &= expect(speech.available() && !speech.engineName().isEmpty() && speech.unavailableReason().isEmpty(), "The log engine should be available.");
	ok &= expect(speech.voices().size() == 2, "The log engine should list its two voices.");
	const SpeechCapabilities capabilities = speech.capabilities();
	ok &= expect(capabilities.voices && capabilities.rate && capabilities.pitch && capabilities.volume, "The log engine should take every setting.");
	SpeechSettings settings;
	settings.voiceId = QStringLiteral(" log-voice-2 ");
	settings.rate = 50;
	settings.pitch = -50;
	settings.volume = 150;
	speech.setSettings(settings);
	ok &= expect(speech.settings().voiceId == QStringLiteral("log-voice-2") && speech.settings().rate == 10 && speech.settings().pitch == -10
			&& speech.settings().volume == 100,
		"Speech settings should be trimmed and clamped.");
	ok &= expect(speech.say(QStringLiteral("  Build\n finished  ")) && speech.sayAndWait(speechTestPhrase(), 1000), "The log engine should accept text.");
	ok &= expect(!speech.say(QStringLiteral("   ")), "Blank text should not be spoken.");
	const QStringList spoken = loggedSpeechForTesting();
	ok &= expect(spoken == QStringList({QStringLiteral("Build finished"), speechTestPhrase()}), "The log engine should record what was said, simplified.");
	qunsetenv("VIBESTUDIO_SPEECH_ENGINE");
	return ok;
}

bool checkSilencedEngine()
{
	bool ok = true;
	qputenv("VIBESTUDIO_SPEECH_ENGINE", "none");
	StudioSpeech speech;
	ok &= expect(!speech.available() && speech.engineName().isEmpty() && !speech.unavailableReason().isEmpty(), "A silenced engine should say why.");
	ok &= expect(!speech.say(QStringLiteral("Hello")) && speech.voices().isEmpty() && !speech.isSpeaking(), "A silenced engine should speak nothing.");
	qunsetenv("VIBESTUDIO_SPEECH_ENGINE");
	ok &= expect(normalizedSpeechRate(-20) == -10 && normalizedSpeechPitch(20) == 10 && normalizedSpeechVolume(-5) == 0, "Speech ranges should clamp.");
	return ok;
}

bool checkMessageDuration()
{
	bool ok = true;
	QMainWindow window;
	QStatusBar* bar = window.statusBar();
	MessageDuration duration = MessageDuration::UntilReplaced;
	QObject::connect(bar, &QStatusBar::messageChanged, bar, [&](const QString& text) {
		if (!text.isEmpty()) {
			applyStatusMessageDuration(bar, duration);
		}
	});
	window.show();
	bar->showMessage(QStringLiteral("kept"), 200);
	wait(450);
	ok &= expect(bar->currentMessage() == QStringLiteral("kept"), "Until replaced should keep a timed message.");
	duration = MessageDuration::Longer;
	bar->showMessage(QStringLiteral("longer"), 200);
	wait(350);
	ok &= expect(bar->currentMessage() == QStringLiteral("longer"), "Longer should keep a timed message past its time.");
	wait(500);
	ok &= expect(bar->currentMessage().isEmpty(), "Longer should still clear the message, three times later.");
	duration = MessageDuration::Standard;
	bar->showMessage(QStringLiteral("standard"), 200);
	wait(450);
	ok &= expect(bar->currentMessage().isEmpty(), "Standard should leave a timed message's time alone.");
	bar->showMessage(QStringLiteral("untimed"));
	applyStatusMessageDuration(bar, MessageDuration::Longer);
	wait(100);
	ok &= expect(bar->currentMessage() == QStringLiteral("untimed"), "An untimed message should stay.");
	return ok;
}

bool checkNames()
{
	bool ok = true;
	for (const ColorVision vision : {ColorVision::Typical, ColorVision::RedGreen, ColorVision::BlueYellow, ColorVision::Monochrome}) {
		ok &= expect(!localizedColorVisionName(vision).isEmpty(), "Every colour vision choice should have a name.");
	}
	for (const MessageDuration duration : {MessageDuration::Standard, MessageDuration::Longer, MessageDuration::UntilReplaced}) {
		ok &= expect(!localizedMessageDurationName(duration).isEmpty(), "Every message duration should have a name.");
	}
	for (const QString& eventId : speechEventIds()) {
		ok &= expect(localizedSpeechEventName(eventId) != eventId, "Every speech event should have a name.");
	}
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
	QApplication app(argc, argv);
	bool ok = true;
	ok &= checkLoggingEngine();
	ok &= checkSilencedEngine();
	ok &= checkMessageDuration();
	ok &= checkNames();
	// No screen reader listens to an offscreen run, so nothing is delivered,
	// and the call says so rather than pretending.
	QWidget source;
	ok &= expect(!announceToScreenReader(&source, QStringLiteral("Build finished")), "An announcement with no screen reader listening should report it was not delivered.");
	ok &= expect(!announceToScreenReader(nullptr, QStringLiteral("x")) && !announceToScreenReader(&source, QStringLiteral("  ")), "Nothing to say, or no source, delivers nothing.");
	if (!ok) {
		std::cerr << "studio speech smoke test failed\n";
		return 1;
	}
	return 0;
}
