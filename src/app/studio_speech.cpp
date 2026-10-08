#include "app/studio_speech.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <memory>

// The Windows engine is SAPI 5 through COM. Only sapi.h is used, not
// sphelper.h, which some SDKs tie to ATL; class and interface ids come from
// __uuidof, so no GUID library has to be linked.
#if defined(Q_OS_WIN) && defined(_MSC_VER) && __has_include(<sapi.h>)
#define VIBESTUDIO_HAVE_SAPI 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sapi.h>
#pragma comment(lib, "ole32.lib")
#endif

namespace vibestudio {

class SpeechBackend {
public:
	virtual ~SpeechBackend() = default;
	[[nodiscard]] virtual bool usable() const = 0;
	[[nodiscard]] virtual QString engineName() const = 0;
	[[nodiscard]] virtual QString unavailableReason() const = 0;
	[[nodiscard]] virtual SpeechCapabilities capabilities() const = 0;
	[[nodiscard]] virtual QVector<SpeechVoice> voices() const = 0;
	virtual bool say(const QString& text, const SpeechSettings& settings) = 0;
	virtual bool waitUntilDone(int timeoutMs) = 0;
	virtual void stop() = 0;
	[[nodiscard]] virtual bool isSpeaking() const = 0;
};

namespace {

// Text the studio asked to be spoken, kept by the "log" engine that tests
// select with VIBESTUDIO_SPEECH_ENGINE=log so nothing is heard while they run.
QStringList& loggedSpeech()
{
	static QStringList spoken;
	return spoken;
}

QMutex& loggedSpeechMutex()
{
	static QMutex mutex;
	return mutex;
}

class UnavailableSpeechBackend final : public SpeechBackend {
public:
	explicit UnavailableSpeechBackend(QString reason)
		: m_reason(std::move(reason))
	{
	}
	bool usable() const override { return false; }
	QString engineName() const override { return {}; }
	QString unavailableReason() const override { return m_reason; }
	SpeechCapabilities capabilities() const override { return {}; }
	QVector<SpeechVoice> voices() const override { return {}; }
	bool say(const QString&, const SpeechSettings&) override { return false; }
	bool waitUntilDone(int) override { return true; }
	void stop() override {}
	bool isSpeaking() const override { return false; }

private:
	QString m_reason;
};

// Speaks nothing and remembers everything: what tests listen to.
class LoggingSpeechBackend final : public SpeechBackend {
public:
	bool usable() const override { return true; }
	QString engineName() const override
	{
		return QCoreApplication::translate("VibeStudioSpeech", "Test log (silent)");
	}
	QString unavailableReason() const override { return {}; }
	SpeechCapabilities capabilities() const override { return {true, true, true, true}; }
	QVector<SpeechVoice> voices() const override
	{
		return {
			{QStringLiteral("log-voice-1"), QStringLiteral("Log Voice One"), QStringLiteral("en-US")},
			{QStringLiteral("log-voice-2"), QStringLiteral("Log Voice Two"), QStringLiteral("de-DE")},
		};
	}
	bool say(const QString& text, const SpeechSettings&) override
	{
		if (text.trimmed().isEmpty()) {
			return false;
		}
		const QMutexLocker locker(&loggedSpeechMutex());
		loggedSpeech().push_back(text);
		return true;
	}
	bool waitUntilDone(int) override { return true; }
	void stop() override {}
	bool isSpeaking() const override { return false; }
};

#if defined(VIBESTUDIO_HAVE_SAPI)

template <typename T>
void releaseCom(T*& pointer)
{
	if (pointer) {
		pointer->Release();
		pointer = nullptr;
	}
}

QString fromWide(const wchar_t* text)
{
	return text ? QString::fromWCharArray(text) : QString();
}

// SAPI registers desktop voices under Speech\Voices; the newer OneCore voices
// that Narrator uses sit under Speech_OneCore and SAPI can drive them too.
const wchar_t* const kSapiVoiceCategories[] = {
	L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices",
	L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices",
};

// A voice token names its languages as hexadecimal LCIDs ("409;9"); the first
// one, as a BCP 47 tag.
QString sapiTokenLanguage(ISpObjectToken* token)
{
	ISpDataKey* attributes = nullptr;
	if (FAILED(token->OpenKey(L"Attributes", &attributes)) || !attributes) {
		return {};
	}
	QString language;
	wchar_t* value = nullptr;
	if (SUCCEEDED(attributes->GetStringValue(L"Language", &value)) && value) {
		const QString lcids = fromWide(value);
		CoTaskMemFree(value);
		bool ok = false;
		const LCID lcid = static_cast<LCID>(lcids.section(QLatin1Char(';'), 0, 0).trimmed().toUInt(&ok, 16));
		wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
		if (ok && LCIDToLocaleName(lcid, name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
			language = fromWide(name);
		}
	}
	releaseCom(attributes);
	return language;
}

class SapiSpeechBackend final : public SpeechBackend {
public:
	SapiSpeechBackend()
	{
		// The GUI thread already runs COM in a single-threaded apartment, so
		// this only adds a reference there; the CLI thread starts it here.
		// RPC_E_CHANGED_MODE means a multithreaded apartment, which SAPI also
		// works in, and must not be balanced by CoUninitialize.
		const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		m_comInitialized = SUCCEEDED(initialized);
		m_error = CoCreateInstance(__uuidof(SpVoice), nullptr, CLSCTX_ALL, __uuidof(ISpVoice), reinterpret_cast<void**>(&m_voice));
		if (FAILED(m_error)) {
			m_voice = nullptr;
		}
	}

	~SapiSpeechBackend() override
	{
		if (m_voice) {
			m_voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
		}
		releaseCom(m_voice);
		if (m_comInitialized) {
			CoUninitialize();
		}
	}

	bool usable() const override { return m_voice != nullptr; }

	QString engineName() const override
	{
		return QCoreApplication::translate("VibeStudioSpeech", "Windows Speech API");
	}

	QString unavailableReason() const override
	{
		if (m_voice) {
			return {};
		}
		return QCoreApplication::translate("VibeStudioSpeech", "The Windows Speech API could not be started (error 0x%1).")
			.arg(static_cast<quint32>(m_error), 8, 16, QLatin1Char('0'));
	}

	SpeechCapabilities capabilities() const override { return {true, true, true, true}; }

	QVector<SpeechVoice> voices() const override
	{
		QVector<SpeechVoice> found;
		QStringList names;
		for (const wchar_t* categoryId : kSapiVoiceCategories) {
			ISpObjectTokenCategory* category = nullptr;
			if (FAILED(CoCreateInstance(__uuidof(SpObjectTokenCategory), nullptr, CLSCTX_ALL, __uuidof(ISpObjectTokenCategory), reinterpret_cast<void**>(&category))) || !category) {
				continue;
			}
			IEnumSpObjectTokens* tokens = nullptr;
			if (SUCCEEDED(category->SetId(categoryId, FALSE)) && SUCCEEDED(category->EnumTokens(nullptr, nullptr, &tokens)) && tokens) {
				ISpObjectToken* token = nullptr;
				while (tokens->Next(1, &token, nullptr) == S_OK && token) {
					SpeechVoice voice;
					wchar_t* id = nullptr;
					if (SUCCEEDED(token->GetId(&id)) && id) {
						voice.id = fromWide(id);
						CoTaskMemFree(id);
					}
					wchar_t* name = nullptr;
					if (SUCCEEDED(token->GetStringValue(nullptr, &name)) && name) {
						voice.name = fromWide(name);
						CoTaskMemFree(name);
					}
					voice.language = sapiTokenLanguage(token);
					releaseCom(token);
					// A voice installed both ways is listed once, by its
					// desktop registration, which every SAPI version can use.
					if (!voice.id.isEmpty() && !voice.name.isEmpty() && !names.contains(voice.name, Qt::CaseInsensitive)) {
						names << voice.name;
						found.push_back(voice);
					}
				}
			}
			releaseCom(tokens);
			releaseCom(category);
		}
		return found;
	}

	bool say(const QString& text, const SpeechSettings& settings) override
	{
		if (!m_voice || text.trimmed().isEmpty()) {
			return false;
		}
		applyVoice(settings.voiceId);
		m_voice->SetRate(std::clamp(settings.rate, -10, 10));
		m_voice->SetVolume(static_cast<USHORT>(std::clamp(settings.volume, 0, 100)));
		DWORD flags = SPF_ASYNC | SPF_PURGEBEFORESPEAK;
		QString spoken = text;
		if (settings.pitch != 0) {
			// SAPI changes pitch only through its XML markup.
			spoken = QStringLiteral("<pitch absmiddle=\"%1\">%2</pitch>").arg(std::clamp(settings.pitch, -10, 10)).arg(text.toHtmlEscaped());
			flags |= SPF_IS_XML;
		} else {
			flags |= SPF_IS_NOT_XML;
		}
		const std::wstring wide = spoken.toStdWString();
		return SUCCEEDED(m_voice->Speak(wide.c_str(), flags, nullptr));
	}

	bool waitUntilDone(int timeoutMs) override
	{
		return !m_voice || m_voice->WaitUntilDone(static_cast<ULONG>(std::max(0, timeoutMs))) == S_OK;
	}

	void stop() override
	{
		if (m_voice) {
			m_voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
		}
	}

	bool isSpeaking() const override
	{
		if (!m_voice) {
			return false;
		}
		SPVOICESTATUS status = {};
		return SUCCEEDED(m_voice->GetStatus(&status, nullptr)) && status.dwRunningState == SPRS_IS_SPEAKING;
	}

private:
	void applyVoice(const QString& voiceId)
	{
		if (m_voiceApplied && voiceId == m_appliedVoiceId) {
			return;
		}
		ISpObjectToken* token = nullptr;
		if (!voiceId.isEmpty()) {
			const std::wstring id = voiceId.toStdWString();
			if (FAILED(CoCreateInstance(__uuidof(SpObjectToken), nullptr, CLSCTX_ALL, __uuidof(ISpObjectToken), reinterpret_cast<void**>(&token)))
				|| !token || FAILED(token->SetId(nullptr, id.c_str(), FALSE))) {
				releaseCom(token);
			}
		}
		// No token selects the voice chosen in the Windows speech settings,
		// which is also where a voice that has been uninstalled falls back.
		m_voice->SetVoice(token);
		releaseCom(token);
		m_appliedVoiceId = voiceId;
		m_voiceApplied = true;
	}

	ISpVoice* m_voice = nullptr;
	HRESULT m_error = S_OK;
	bool m_comInitialized = false;
	bool m_voiceApplied = false;
	QString m_appliedVoiceId;
};

#endif

// The command-line engines: macOS `say`, Speech Dispatcher's `spd-say`, and
// eSpeak NG (or classic eSpeak). Each utterance is its own process, which a
// newer one stops.
class ProcessSpeechBackend final : public SpeechBackend {
public:
	enum class Kind {
		Say,
		SpeechDispatcher,
		Espeak,
	};

	ProcessSpeechBackend(Kind kind, QString program)
		: m_kind(kind)
		, m_program(std::move(program))
	{
	}

	~ProcessSpeechBackend() override { stop(); }

	bool usable() const override { return !m_program.isEmpty(); }

	QString engineName() const override
	{
		switch (m_kind) {
		case Kind::Say:
			return QCoreApplication::translate("VibeStudioSpeech", "macOS speech (say)");
		case Kind::SpeechDispatcher:
			return QCoreApplication::translate("VibeStudioSpeech", "Speech Dispatcher");
		case Kind::Espeak:
			return QCoreApplication::translate("VibeStudioSpeech", "eSpeak NG");
		}
		return {};
	}

	QString unavailableReason() const override { return {}; }

	SpeechCapabilities capabilities() const override
	{
		// `say` takes a voice and a rate; pitch and volume belong to the
		// system voice settings there.
		if (m_kind == Kind::Say) {
			return {true, true, false, false};
		}
		return {true, true, true, true};
	}

	QVector<SpeechVoice> voices() const override
	{
		QStringList arguments;
		switch (m_kind) {
		case Kind::Say:
			arguments << QStringLiteral("-v") << QStringLiteral("?");
			break;
		case Kind::SpeechDispatcher:
			arguments << QStringLiteral("-L");
			break;
		case Kind::Espeak:
			arguments << QStringLiteral("--voices");
			break;
		}
		QProcess process;
		process.start(m_program, arguments);
		if (!process.waitForFinished(4000)) {
			process.kill();
			process.waitForFinished(500);
			return {};
		}
		const QStringList lines = QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
		QVector<SpeechVoice> found;
		QStringList ids;
		for (const QString& rawLine : lines) {
			SpeechVoice voice;
			if (m_kind == Kind::Say) {
				// "Alex                en_US    # Most people recognize me by my voice."
				static const QRegularExpression pattern(QStringLiteral("^(.+?)\\s{2,}([a-z]{2,3}[_-][A-Za-z0-9]+)\\s+#"));
				const QRegularExpressionMatch match = pattern.match(rawLine);
				if (!match.hasMatch()) {
					continue;
				}
				voice.id = match.captured(1).trimmed();
				voice.name = voice.id;
				voice.language = match.captured(2).replace(QLatin1Char('_'), QLatin1Char('-'));
			} else if (m_kind == Kind::SpeechDispatcher) {
				// A header row, then "NAME  LANGUAGE  VARIANT" columns.
				const QStringList columns = rawLine.trimmed().split(QRegularExpression(QStringLiteral("\\s{2,}")), Qt::SkipEmptyParts);
				if (columns.size() < 2 || columns.first() == QStringLiteral("NAME")) {
					continue;
				}
				voice.id = columns.at(0);
				voice.name = columns.at(0);
				voice.language = columns.at(1);
			} else {
				// "Pty Language Age/Gender VoiceName File Other Languages"
				const QStringList columns = rawLine.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
				if (columns.size() < 4 || columns.at(1) == QStringLiteral("Language")) {
					continue;
				}
				voice.id = columns.at(1);
				voice.name = QStringLiteral("%1 (%2)").arg(columns.at(3), columns.at(1));
				voice.language = columns.at(1);
			}
			if (!voice.id.isEmpty() && !ids.contains(voice.id)) {
				ids << voice.id;
				found.push_back(voice);
			}
		}
		return found;
	}

	bool say(const QString& text, const SpeechSettings& settings) override
	{
		if (text.trimmed().isEmpty()) {
			return false;
		}
		stop();
		m_process = std::make_unique<QProcess>();
		QStringList arguments;
		bool viaStandardInput = false;
		const double rateFactor = std::pow(2.0, std::clamp(settings.rate, -10, 10) / 10.0);
		switch (m_kind) {
		case Kind::Say:
			if (!settings.voiceId.isEmpty()) {
				arguments << QStringLiteral("-v") << settings.voiceId;
			}
			arguments << QStringLiteral("-r") << QString::number(qRound(180.0 * rateFactor));
			// Read from standard input, so text that starts with a dash is
			// never taken for an option.
			viaStandardInput = true;
			break;
		case Kind::SpeechDispatcher:
			// -w keeps the process alive while the words are spoken, so a
			// newer message can stop it and isSpeaking() is true meanwhile.
			arguments << QStringLiteral("-w")
					  << QStringLiteral("-r") << QString::number(std::clamp(settings.rate, -10, 10) * 10)
					  << QStringLiteral("-p") << QString::number(std::clamp(settings.pitch, -10, 10) * 10)
					  << QStringLiteral("-i") << QString::number(std::clamp(settings.volume, 0, 100) * 2 - 100);
			if (!settings.voiceId.isEmpty()) {
				arguments << QStringLiteral("-y") << settings.voiceId;
			}
			arguments << QStringLiteral("--") << text;
			break;
		case Kind::Espeak:
			arguments << QStringLiteral("-s") << QString::number(qRound(175.0 * rateFactor))
					  << QStringLiteral("-p") << QString::number(std::clamp(50 + settings.pitch * 5, 0, 99))
					  << QStringLiteral("-a") << QString::number(std::clamp(settings.volume, 0, 100) * 2);
			if (!settings.voiceId.isEmpty()) {
				arguments << QStringLiteral("-v") << settings.voiceId;
			}
			arguments << QStringLiteral("--stdin");
			viaStandardInput = true;
			break;
		}
		m_process->start(m_program, arguments);
		if (!m_process->waitForStarted(3000)) {
			m_process.reset();
			return false;
		}
		if (viaStandardInput) {
			m_process->write(text.toUtf8());
			m_process->closeWriteChannel();
		}
		return true;
	}

	bool waitUntilDone(int timeoutMs) override
	{
		return !m_process || m_process->state() == QProcess::NotRunning || m_process->waitForFinished(std::max(0, timeoutMs));
	}

	// Ends this studio's utterance only. Speech Dispatcher is shared with the
	// desktop's screen reader, so nothing is ever cancelled at the daemon: a
	// message it already accepted may finish before the next one is heard.
	void stop() override
	{
		if (m_process && m_process->state() != QProcess::NotRunning) {
			m_process->kill();
			m_process->waitForFinished(500);
		}
		m_process.reset();
	}

	bool isSpeaking() const override { return m_process && m_process->state() != QProcess::NotRunning; }

private:
	Kind m_kind;
	QString m_program;
	std::unique_ptr<QProcess> m_process;
};

std::unique_ptr<SpeechBackend> createSpeechBackend()
{
	// VIBESTUDIO_SPEECH_ENGINE picks an engine by name: "none" silences the
	// studio, and "log" records what would be said without a sound, which is
	// what the tests use.
	const QString requested = qEnvironmentVariable("VIBESTUDIO_SPEECH_ENGINE").trimmed().toLower();
	if (requested == QStringLiteral("none") || requested == QStringLiteral("off")) {
		return std::make_unique<UnavailableSpeechBackend>(QCoreApplication::translate("VibeStudioSpeech", "Speech is turned off for this run by VIBESTUDIO_SPEECH_ENGINE."));
	}
	if (requested == QStringLiteral("log")) {
		return std::make_unique<LoggingSpeechBackend>();
	}
#if defined(VIBESTUDIO_HAVE_SAPI)
	if (requested.isEmpty() || requested == QStringLiteral("sapi")) {
		auto sapi = std::make_unique<SapiSpeechBackend>();
		if (sapi->usable() || requested == QStringLiteral("sapi")) {
			return sapi;
		}
		return std::make_unique<UnavailableSpeechBackend>(sapi->unavailableReason());
	}
#endif
	struct Candidate {
		QString name;
		ProcessSpeechBackend::Kind kind;
	};
	QVector<Candidate> candidates;
#if defined(Q_OS_MACOS)
	candidates.push_back({QStringLiteral("say"), ProcessSpeechBackend::Kind::Say});
#endif
	candidates.push_back({QStringLiteral("spd-say"), ProcessSpeechBackend::Kind::SpeechDispatcher});
	candidates.push_back({QStringLiteral("espeak-ng"), ProcessSpeechBackend::Kind::Espeak});
	candidates.push_back({QStringLiteral("espeak"), ProcessSpeechBackend::Kind::Espeak});
	for (const Candidate& candidate : std::as_const(candidates)) {
		if (!requested.isEmpty() && requested != candidate.name) {
			continue;
		}
		const QString program = QStandardPaths::findExecutable(candidate.name);
		if (!program.isEmpty()) {
			return std::make_unique<ProcessSpeechBackend>(candidate.kind, program);
		}
	}
#if defined(Q_OS_WIN)
	return std::make_unique<UnavailableSpeechBackend>(QCoreApplication::translate("VibeStudioSpeech", "This build has no Windows Speech API support."));
#elif defined(Q_OS_MACOS)
	return std::make_unique<UnavailableSpeechBackend>(QCoreApplication::translate("VibeStudioSpeech", "The macOS say command was not found."));
#else
	return std::make_unique<UnavailableSpeechBackend>(QCoreApplication::translate("VibeStudioSpeech", "No speech engine was found. Install Speech Dispatcher (spd-say) or eSpeak NG to hear status changes read aloud."));
#endif
}

} // namespace

StudioSpeech::StudioSpeech()
	: m_backend(createSpeechBackend())
{
}

StudioSpeech::~StudioSpeech() = default;

bool StudioSpeech::available() const
{
	return m_backend && m_backend->usable();
}

QString StudioSpeech::engineName() const
{
	return available() ? m_backend->engineName() : QString();
}

QString StudioSpeech::unavailableReason() const
{
	return m_backend ? m_backend->unavailableReason() : QString();
}

SpeechCapabilities StudioSpeech::capabilities() const
{
	return available() ? m_backend->capabilities() : SpeechCapabilities {};
}

QVector<SpeechVoice> StudioSpeech::voices() const
{
	return available() ? m_backend->voices() : QVector<SpeechVoice> {};
}

void StudioSpeech::setSettings(const SpeechSettings& settings)
{
	m_settings.voiceId = settings.voiceId.trimmed();
	m_settings.rate = normalizedSpeechRate(settings.rate);
	m_settings.pitch = normalizedSpeechPitch(settings.pitch);
	m_settings.volume = normalizedSpeechVolume(settings.volume);
}

SpeechSettings StudioSpeech::settings() const
{
	return m_settings;
}

bool StudioSpeech::say(const QString& text)
{
	return available() && m_backend->say(text.simplified(), m_settings);
}

bool StudioSpeech::sayAndWait(const QString& text, int timeoutMs)
{
	if (!say(text)) {
		return false;
	}
	return m_backend->waitUntilDone(timeoutMs);
}

void StudioSpeech::stop()
{
	if (available()) {
		m_backend->stop();
	}
}

bool StudioSpeech::isSpeaking() const
{
	return available() && m_backend->isSpeaking();
}

int normalizedSpeechRate(int rate)
{
	return std::clamp(rate, -10, 10);
}

int normalizedSpeechPitch(int pitch)
{
	return std::clamp(pitch, -10, 10);
}

int normalizedSpeechVolume(int volume)
{
	return std::clamp(volume, 0, 100);
}

QString speechTestPhrase()
{
	return QCoreApplication::translate("VibeStudioSpeech", "This is how VibeStudio reads status changes aloud.");
}

QStringList loggedSpeechForTesting()
{
	const QMutexLocker locker(&loggedSpeechMutex());
	return loggedSpeech();
}

void clearLoggedSpeechForTesting()
{
	const QMutexLocker locker(&loggedSpeechMutex());
	loggedSpeech().clear();
}

} // namespace vibestudio
