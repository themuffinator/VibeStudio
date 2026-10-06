#include "app/audio_playback.h"

#include "core/audio_clip.h"
#include "vibestudio_config.h"

#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
#include <QAudioDevice>
#include <QAudioOutput>
#include <QBuffer>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QUrl>
#endif

#include <algorithm>
#include <cmath>
#include <QFileInfo>
#include <QStringList>
#include <utility>
#include <QPointer>
#include <QVector>

namespace vibestudio
{
namespace
{
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
// Original adapter using Qt 6.10.1's public media/device contract. No upstream
// source is copied. See Qt Multimedia attribution in docs/CREDITS.md.
class QtAudioPlaybackBackend final : public AudioPlaybackBackend
{
public:
	void open(const QByteArray& bytes, const QString& fileName, quint64 session, float volume, bool loop) override
	{
		clear();
		const auto device = QMediaDevices::defaultAudioOutput();
		if (device.isNull()) {
			emit failed(session, AudioPlayback::tr("No audio output device is available. Select an output "
			                                       "in system sound settings, then try Play again."));
			return;
		}
		if (!m_devices) {
			m_devices = new QMediaDevices(this);
			connect(m_devices, &QMediaDevices::audioOutputsChanged, this, [this]() {
				if (!m_player || !m_output) { return; }
				const auto devices = QMediaDevices::audioOutputs();
				const bool present = std::any_of(devices.cbegin(), devices.cend(),
				                                 [this](const QAudioDevice& output) { return output.id() == m_deviceId; });
				if (!present) {
					emit failed(m_session, AudioPlayback::tr("The audio output device was disconnected. "
					                                         "Select an output in system sound settings, then try Play again."));
				}
			});
		}
		m_session = session;
		m_deviceId = device.id();
		auto* player = new QMediaPlayer(this);
		m_player = player;
		if (!player->isAvailable()) {
			emit failed(session, AudioPlayback::tr("The Qt audio playback backend is unavailable."));
			return;
		}
		m_output = new QAudioOutput(device, player);
		m_output->setVolume(volume);
		player->setAudioOutput(m_output);
		player->setLoops(loop ? QMediaPlayer::Infinite : QMediaPlayer::Once);
		connect(player, &QMediaPlayer::positionChanged, this,
		        [this, session](qint64 position) { emit positionChanged(session, position); });
		connect(player, &QMediaPlayer::durationChanged, this,
		        [this, session](qint64 duration) { emit durationChanged(session, duration); });
		connect(player, &QMediaPlayer::seekableChanged, this,
		        [this, session](bool seekable) { emit seekableChanged(session, seekable); });
		connect(player, &QMediaPlayer::playbackStateChanged, this, [this, session](QMediaPlayer::PlaybackState state) {
			emit stateChanged(session, state == QMediaPlayer::PlayingState ? State::Playing
			                           : state == QMediaPlayer::PausedState ? State::Paused : State::Stopped);
		});
		connect(player, &QMediaPlayer::mediaStatusChanged, this, [this, player, session](QMediaPlayer::MediaStatus status) {
			if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
				emit stalledChanged(session, false);
				emit ready(session, player->isSeekable());
			} else if (status == QMediaPlayer::StalledMedia) {
				emit stalledChanged(session, true);
			} else if (status == QMediaPlayer::EndOfMedia) {
				emit ended(session);
			} else if (status == QMediaPlayer::InvalidMedia) {
				emit failed(session, player->errorString().isEmpty()
				                         ? AudioPlayback::tr("The audio backend could not open the prepared sound.")
				                         : player->errorString());
			}
		});
		connect(player, &QMediaPlayer::errorOccurred, this,
		        [this, session](QMediaPlayer::Error error, const QString& message) {
			        if (error != QMediaPlayer::NoError) { emit failed(session, message); }
		        });
		auto* buffer = new QBuffer(player);
		buffer->setObjectName(QStringLiteral("audioPlaybackBuffer"));
		buffer->setData(bytes);
		buffer->open(QIODevice::ReadOnly);
		// The buffer outlives the player's source. Retired players are muted and
		// detached on the next event turn, outside any backend signal callback.
		player->setSourceDevice(buffer, QUrl(fileName));
	}
	void clear() override
	{
		if (auto* player = std::exchange(m_player, nullptr)) {
			m_retired.removeIf([](const auto& value) { return value.isNull(); });
			m_retired.append(player);
			player->disconnect(this);
			if (m_output) { m_output->setMuted(true); }
			m_output = nullptr;
			QTimer::singleShot(0, player, [player]() {
				player->stop();
				player->setSourceDevice(nullptr);
				player->deleteLater();
			});
		}
	}
	void clearAndWait(QObject* context, std::function<void()> complete) override
	{
		clear();
		m_retired.removeIf([](const auto& value) { return value.isNull(); });
		if (m_retired.isEmpty()) { complete(); return; }
		auto remaining = std::make_shared<qsizetype>(m_retired.size());
		for (const auto& player : m_retired) {
			connect(player.data(), &QObject::destroyed, context, [remaining, complete] {
				if (--*remaining == 0) { complete(); }
			});
		}
	}
	void play() override { if (m_player) { m_player->play(); } }
	void pause() override { if (m_player) { m_player->pause(); } }
	void seek(qint64 milliseconds) override { if (m_player) { m_player->setPosition(milliseconds); } }
	void setVolume(float volume) override { if (m_output) { m_output->setVolume(volume); } }
	void setLoop(bool enabled) override
	{
		if (m_player) { m_player->setLoops(enabled ? QMediaPlayer::Infinite : QMediaPlayer::Once); }
	}
private:
	QMediaPlayer* m_player = nullptr;
	QVector<QPointer<QMediaPlayer>> m_retired;
	QAudioOutput* m_output = nullptr;
	QMediaDevices* m_devices = nullptr;
	QByteArray m_deviceId;
	quint64 m_session = 0;
};
#endif
} // namespace

void AudioPlaybackBackend::clearAndWait(QObject* context, std::function<void()> complete)
{
	clear();
	QTimer::singleShot(0, context, std::move(complete));
}

AudioPlayback::AudioPlayback(QObject* parent, std::unique_ptr<AudioPlaybackBackend> backend, int timeoutMs)
    : QObject(parent), m_backend(std::move(backend))
{
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
	if (!m_backend) { m_backend = std::make_unique<QtAudioPlaybackBackend>(); }
#endif
	m_timeout.setSingleShot(true);
	m_timeout.setInterval(std::max(1, timeoutMs));
	connect(&m_timeout, &QTimer::timeout, this, [this]() {
		if (active()) {
			fail(tr("Audio playback timed out while loading or buffering. Try Play again or choose another "
			        "output in system sound settings."));
		}
	});
	if (!m_backend) { return; }
	m_state = State::Stopped;
	connect(m_backend.get(), &AudioPlaybackBackend::ready, this, [this](quint64 session, bool seekable) {
		if (!current(session)) { return; }
		m_seekable = seekable;
		if (m_pendingStart) {
			m_pendingStart = false;
			if (m_initialMs > 0) {
				if (!seekable) { fail(tr("This sound cannot be started at the selected position. Seek to the beginning and try Play again.")); return; }
				m_backend->seek(m_initialMs);
				if (!current(session)) { return; }
			}
			m_backend->play();
		}
		emit changed();
	});
	connect(m_backend.get(), &AudioPlaybackBackend::stateChanged, this,
	        [this](quint64 session, AudioPlaybackBackend::State state) {
		        if (!current(session)) { return; }
		        m_backendState = state;
		        // A seek used to restart a loop may temporarily pause the backend.
		        if (m_pendingLoopRestart && state == AudioPlaybackBackend::State::Paused) { return; }
		        if (state == AudioPlaybackBackend::State::Stopped) {
			        // End/error notifications can follow Stopped in the same turn.
			        // A queued stop must not clear a later session or its diagnostics.
			        if (m_state != State::Loading) {
				        QTimer::singleShot(0, this, [this, session]() {
					        if (current(session) && !m_pendingLoopRestart &&
					            m_backendState == AudioPlaybackBackend::State::Stopped) { stop(); }
				        });
			        }
			        return;
		        }
		        m_state = state == AudioPlaybackBackend::State::Playing ? State::Playing : State::Paused;
		        m_pendingStart = false;
		        m_pendingLoopRestart = false;
		        if (!m_stalled || m_state == State::Paused) { m_timeout.stop(); }
		        else { m_timeout.start(); }
		        emit changed();
	        });
	connect(m_backend.get(), &AudioPlaybackBackend::positionChanged, this, [this](quint64 session, qint64 milliseconds) {
		if (!current(session) || m_state == State::Loading || m_backendState == AudioPlaybackBackend::State::Stopped) {
			return;
		}
		if (m_media) {
			setPosition(m_end > 0 ? std::clamp<qint64>(milliseconds, 0, m_end) : std::max<qint64>(0, milliseconds));
			return;
		}
		const qint64 duration = durationMilliseconds();
		const qint64 bounded = std::clamp<qint64>(milliseconds, 0, duration);
		setPosition(std::min(m_end, m_first + bounded * m_sampleRate / 1000));
	});
	connect(m_backend.get(), &AudioPlaybackBackend::durationChanged, this, [this](quint64 session, qint64 duration) {
		if (!current(session) || !m_media || duration <= 0) { return; }
		m_end = duration;
		m_initialMs = std::min(m_initialMs, duration);
		if (m_position > duration) { setPosition(duration); }
		if (current(session)) { emit changed(); }
	});
	connect(m_backend.get(), &AudioPlaybackBackend::seekableChanged, this, [this](quint64 session, bool seekable) {
		if (current(session)) { m_seekable = seekable; emit changed(); }
	});
	connect(m_backend.get(), &AudioPlaybackBackend::stalledChanged, this, [this](quint64 session, bool stalled) {
		if (!current(session)) { return; }
		m_stalled = stalled;
		if (stalled && m_state != State::Paused) {
			if (!m_timeout.isActive()) { m_timeout.start(); }
		} else if (!m_pendingStart && m_state != State::Loading) {
			m_timeout.stop();
		}
		emit changed();
	});
	connect(m_backend.get(), &AudioPlaybackBackend::ended, this, [this](quint64 session) {
		if (!current(session) || m_pendingLoopRestart) { return; }
		if (m_loop) {
			// Some backends (observed with Qt 6.4.2 FFmpeg) announce EOF even
			// with Infinite requested. Native looping remains preferred; this
			// bounded fallback runs outside the backend's end callback.
			m_pendingLoopRestart = true;
			m_pendingStart = false;
			m_stalled = false;
			m_state = State::Loading;
			m_timeout.start();
			setPosition(m_first);
			if (!current(session)) { return; }
			emit changed();
			QTimer::singleShot(0, this, [this, session]() { restartLoop(session); });
			return;
		}
		finishAtEnd(session);
	});
	connect(m_backend.get(), &AudioPlaybackBackend::failed, this, [this](quint64 session, const QString& message) {
		if (current(session)) { fail(message); }
	});
}

AudioPlayback::~AudioPlayback() { detach(); }

void AudioPlayback::finishAtEnd(quint64 session)
{
	if (!current(session)) { return; }
	if (m_end > 0) { setPosition(m_end); }
	if (current(session)) { stop(); }
}

void AudioPlayback::restartLoop(quint64 session)
{
	if (!current(session) || !m_pendingLoopRestart) { return; }
	if (!m_loop) { finishAtEnd(session); return; }
	if (!m_seekable) {
		fail(tr("This sound cannot be looped by the audio backend. Turn Loop off, then try Play again."));
		return;
	}
	m_backend->seek(0);
	if (!current(session) || !m_pendingLoopRestart) { return; }
	if (!m_loop) { finishAtEnd(session); return; }
	m_backend->play();
}

bool AudioPlayback::active() const
{
	return m_state == State::Loading || m_state == State::Playing || m_state == State::Paused;
}
bool AudioPlayback::current(quint64 session) const { return session == m_session && active(); }
bool AudioPlayback::canSeek() const
{
	return m_seekable && (m_state == State::Playing || m_state == State::Paused);
}

bool AudioPlayback::start(const QByteArray& wav, qint64 first, qint64 end, int sampleRate)
{
	if (!available()) { return false; }
	detach();
	m_error.clear();
	if (wav.isEmpty() || wav.size() > AudioInputByteLimit || first < 0 || end <= first || end > AudioSampleLimit ||
	    sampleRate < 1 || sampleRate > 384000) {
		fail(tr("The prepared audio playback range is invalid."));
		return false;
	}
	m_first = first;
	m_end = end;
	m_sampleRate = sampleRate;
	m_media = false;
	m_initialMs = 0;
	return open(wav, QStringLiteral("edited.wav"), first);
}

bool AudioPlayback::startMedia(const QByteArray& bytes, const QString& fileName, qint64 durationMs, qint64 startMs)
{
	if (!available()) { return false; }
	detach();
	m_error.clear();
	if (bytes.isEmpty() || bytes.size() > AudioInputByteLimit || durationMs < 0 || startMs < 0) {
		fail(tr("The prepared audio media is empty, too large, or has an invalid position."));
		return false;
	}
	m_media = true;
	m_first = 0;
	m_end = durationMs;
	m_sampleRate = 1000;
	m_initialMs = durationMs > 0 && startMs >= durationMs ? 0 : startMs;
	// A virtual package name must never become a URL scheme or remote address.
	QString suffix = QFileInfo(fileName).suffix().toLower();
	if (!QStringList{QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("opus"),
	                 QStringLiteral("mp3"), QStringLiteral("flac")}.contains(suffix)) { suffix = QStringLiteral("bin"); }
	return open(bytes, QStringLiteral("audition.") + suffix, m_initialMs);
}

bool AudioPlayback::open(const QByteArray& bytes, const QString& fileName, qint64 initialPosition)
{
	m_state = State::Loading;
	m_pendingStart = true;
	const quint64 session = m_session;
	m_timeout.start();
	setPosition(initialPosition);
	if (!current(session)) { return false; }
	emit changed();
	if (!current(session)) { return false; }
	m_backend->open(bytes, fileName, session, m_volume, m_loop);
	return current(session);
}

void AudioPlayback::detach()
{
	++m_session;
	m_timeout.stop();
	m_pendingStart = m_pendingLoopRestart = m_stalled = m_seekable = false;
	m_backendState = AudioPlaybackBackend::State::Stopped;
	if (m_backend) { m_backend->clear(); }
}

void AudioPlayback::stop()
{
	detach();
	m_state = available() ? State::Stopped : State::Unavailable;
	emit changed();
}

void AudioPlayback::stopAndWait(QObject* context, std::function<void(bool)> complete)
{
	const auto token = m_session + 1;
	stop();
	const auto acknowledged = [this, token, guard = QPointer<QObject>(context), complete = std::move(complete)] {
		// Queue through our lifetime even when a backend completes synchronously
		// or releases retired players while its owner is being destroyed.
		QMetaObject::invokeMethod(this, [this, token, guard, complete] {
			if (guard && complete) { complete(m_session == token); }
		}, Qt::QueuedConnection);
	};
	if (m_backend && m_session == token) { m_backend->clearAndWait(this, acknowledged); }
	else { acknowledged(); }
}

void AudioPlayback::fail(const QString& message)
{
	detach();
	m_state = State::Error;
	m_error = message.trimmed().isEmpty() ? tr("The audio backend reported an unspecified error.") : message;
	emit changed();
	emit failed(m_error);
}

void AudioPlayback::pause()
{
	if (m_state == State::Playing) { m_backend->pause(); }
}
void AudioPlayback::resume()
{
	if (m_state == State::Paused) { m_backend->play(); }
}
void AudioPlayback::setPosition(qint64 frame)
{
	if (m_position != frame) { m_position = frame; emit positionChanged(frame); }
}

bool AudioPlayback::seekToFrame(qint64 frame)
{
	if (!canSeek()) { return false; }
	const quint64 session = m_session;
	frame = m_media && m_end == 0 ? std::max<qint64>(0, frame) : std::clamp(frame, m_first, m_end);
	if (m_end > 0 && frame == m_end) {
		setPosition(m_end);
		if (current(session)) { stop(); }
		return true;
	}
	const qint64 milliseconds = m_media ? frame : (frame - m_first) * 1000 / m_sampleRate;
	// Report the actual requested backend position, including while paused.
	setPosition(m_media ? milliseconds : m_first + milliseconds * m_sampleRate / 1000);
	if (current(session)) { m_backend->seek(milliseconds); }
	return true;
}

bool AudioPlayback::seekToMilliseconds(qint64 milliseconds) { return m_media && seekToFrame(milliseconds); }
qint64 AudioPlayback::positionMilliseconds() const
{
	return m_media ? m_position : m_sampleRate > 0 ? (m_position - m_first) * 1000 / m_sampleRate : 0;
}
qint64 AudioPlayback::durationMilliseconds() const
{
	return m_media ? m_end : m_sampleRate > 0 ? ((m_end - m_first) * 1000 + m_sampleRate - 1) / m_sampleRate : 0;
}

void AudioPlayback::setVolume(float volume)
{
	if (!std::isfinite(volume)) { return; }
	m_volume = std::clamp(volume, 0.0f, 1.0f);
	if (m_backend) { m_backend->setVolume(m_volume); }
}
void AudioPlayback::setLoop(bool enabled)
{
	m_loop = enabled;
	if (m_backend) { m_backend->setLoop(enabled); }
}

} // namespace vibestudio
