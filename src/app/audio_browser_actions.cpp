#include "app/application_shell.h"

#include "app/asset_views.h"
#include "app/audio_browser_worker.h"
#include "app/audio_editor_dialog.h"
#include "app/audio_session_dialog.h"
#include "app/studio_actions.h"
#include "app/studio_icons.h"
#include "app/ui_primitives.h"

#include <QAction>
#include <QLabel>
#include <QSlider>
#include <QSignalBlocker>
#include <QStatusBar>

#include <algorithm>
#include <limits>
#include <utility>

namespace vibestudio
{
namespace
{
QString audioClockText(qint64 positionMs)
{
	const qint64 value = std::max<qint64>(0, positionMs);
	return QStringLiteral("%1:%2.%3").arg(value / 60000)
		.arg((value / 1000) % 60, 2, 10, QLatin1Char('0')).arg((value % 1000) / 10, 2, 10, QLatin1Char('0'));
}
} // namespace

void ApplicationShell::initializeAudioBrowser(std::unique_ptr<AudioPlaybackBackend> backend)
{
	m_audioPlayback = new AudioPlayback(this, std::move(backend));
	m_audioPlayback->setObjectName(QStringLiteral("audioBrowserPlayback"));
	m_audioPlayback->setVolume(0.8f);
	m_audioPreviewWorker = new AudioBrowserWorker(this);
	m_audioAuditionWorker = new AudioBrowserWorker(this);
	m_audioPreviewWorker->completed = [this](const AudioBrowserResult& result) { acceptAudioPreview(result); };
	m_audioPreviewWorker->progress = [this](int progress) {
		if (m_audioPreviewPending && m_audioState) {
			m_audioState->setDetail(tr("Reading and preparing %1: %2%").arg(m_audioShownPath).arg(progress));
		}
	};
	m_audioAuditionWorker->progress = [this](int progress) {
		if (m_audioAuditionPending) { statusBar()->showMessage(tr("Preparing %1 for playback: %2%").arg(m_audioShownPath).arg(progress)); }
	};
	m_audioAuditionWorker->completed = [this](const AudioBrowserResult& result) {
		if (m_audioSessionDialog && m_audioSessionDialog->recordingOpen()) { unloadAudioPlayback(); refreshAudioTransport(); return; }
		if (!m_audioAuditionPending || result.revision != m_audioRevision || result.entryIndex != m_audioShownIndex ||
		    result.virtualPath != m_audioShownPath) { return; }
		m_audioAuditionPending = false;
		if (!result.error.isEmpty() || !result.source.playable()) {
			m_audioPlaybackPath.clear();
			statusBar()->showMessage(tr("Unable to play %1: %2").arg(result.virtualPath,
				result.error.isEmpty() ? tr("No playable audio was prepared.") : result.error));
			refreshAudioTransport(); return;
		}
		m_audioPlaybackPath = result.virtualPath;
		statusBar()->showMessage(tr("Loading %1 for playback…").arg(result.virtualPath));
		m_audioPlayback->startMedia(result.source.bytes, result.source.fileName, m_audioDurationMs, m_audioCursorMs);
		refreshAudioTransport();
	};
	connect(m_audioPlayback, &AudioPlayback::changed, this, [this] {
		if (!m_audioPlaybackPath.isEmpty() && m_audioPlaybackPath == m_audioShownPath &&
		    m_audioPlayback->durationMilliseconds() > 0) {
			m_audioDurationMs = m_audioPlayback->durationMilliseconds();
		}
		if (!m_audioPlaybackPath.isEmpty() && m_audioPlayback->state() == AudioPlayback::State::Playing) {
			statusBar()->showMessage(m_audioPlayback->stalled() ? tr("Buffering %1…").arg(m_audioPlaybackPath)
				: tr("Playing %1").arg(m_audioPlaybackPath), 3000);
		} else if (!m_audioPlaybackPath.isEmpty() && m_audioPlayback->state() == AudioPlayback::State::Paused) {
			statusBar()->showMessage(tr("Paused %1").arg(m_audioPlaybackPath), 3000);
		}
		refreshAudioTransport();
	});
	connect(m_audioPlayback, &AudioPlayback::positionChanged, this, [this](qint64) { refreshAudioTransport(); });
	connect(m_audioPlayback, &AudioPlayback::failed, this, [this](const QString& message) {
		statusBar()->showMessage(tr("Unable to play %1: %2").arg(m_audioPlaybackPath, message));
	});
}

bool ApplicationShell::audioPlaybackAvailable() const { return m_audioPlayback && m_audioPlayback->available(); }

void ApplicationShell::toggleAudioPlayback()
{
	if (m_audioSessionDialog && m_audioSessionDialog->recordingOpen()) {
		statusBar()->showMessage(tr("Finish recording and take review before starting another audition."));
		return;
	}
	if (!audioPlaybackAvailable() || m_audioAuditionPending || m_audioPlayback->state() == AudioPlayback::State::Loading) { return; }
	if (m_audioEditorDialog) { m_audioEditorDialog->stopPlayback(); }
	if (m_audioSessionDialog) { m_audioSessionDialog->stopPlayback(); }
	if (m_audioPlayback->state() == AudioPlayback::State::Playing) { m_audioPlayback->pause(); return; }
	if (m_audioPlayback->state() == AudioPlayback::State::Paused) { m_audioPlayback->resume(); return; }
	if (!m_audioSelectedPlayable || m_audioShownPath.isEmpty() || m_audioShownIndex < 0 || !m_audioArchive.isOpen()) {
		statusBar()->showMessage(tr("Select a playable sound first.")); return;
	}
	unloadAudioPlayback();
	m_audioAuditionPending = true;
	m_audioPlaybackPath = m_audioShownPath;
	m_audioAuditionWorker->request({AudioBrowserRequest::Kind::Audition, std::make_shared<PackageArchive>(m_audioArchive),
		m_audioRevision, m_audioShownIndex, m_audioShownPath});
	statusBar()->showMessage(tr("Preparing %1 for playback…").arg(m_audioShownPath));
	refreshAudioTransport();
}

void ApplicationShell::stopAudioPlayback()
{
	const bool preparing = m_audioAuditionPending;
	unloadAudioPlayback();
	if (preparing) { statusBar()->showMessage(tr("Audio playback preparation cancelled.")); }
	refreshAudioTransport();
}
void ApplicationShell::seekAudioPlayback(qint64 positionMs)
{
	const qint64 requested = m_audioDurationMs > 0 ? std::clamp<qint64>(positionMs, 0, m_audioDurationMs) : std::max<qint64>(0, positionMs);
	if (m_audioPlayback && m_audioPlayback->active()) {
		if (!m_audioPlayback->seekToMilliseconds(requested)) {
			statusBar()->showMessage(tr("This sound cannot be seeked while loading or with this playback backend.")); return;
		}
	}
	m_audioCursorMs = requested;
	refreshAudioTransport();
}
void ApplicationShell::setAudioLoop(bool enabled) { if (m_audioPlayback) { m_audioPlayback->setLoop(enabled); } }
void ApplicationShell::unloadAudioPlayback()
{
	m_audioAuditionPending = false;
	if (m_audioAuditionWorker) { m_audioAuditionWorker->cancel(); }
	m_audioPlaybackPath.clear();
	if (m_audioPlayback) { m_audioPlayback->stop(); }
}
void ApplicationShell::refreshAudioTransport()
{
	if (!m_commands || !m_audioPlayback) { return; }
	refreshAssetWorkbenchCommands();
	const auto state = m_audioPlayback->state();
	const bool playing = state == AudioPlayback::State::Playing;
	const bool loading = m_audioAuditionPending || state == AudioPlayback::State::Loading;
	const bool active = m_audioAuditionPending || m_audioPlayback->active();
	const qint64 position = m_audioPlayback->active() ? m_audioPlayback->positionMilliseconds() : m_audioCursorMs;
	const bool available = audioPlaybackAvailable();
	if (auto* play = m_commands->action(QStringLiteral("audio.playPause"))) {
		play->setText(loading ? tr("Preparing Sound…") : playing ? tr("&Pause Sound") : tr("&Play Sound"));
		play->setIcon(studioIcon(playing ? QStringLiteral("pause") : QStringLiteral("play")));
		play->setToolTip(!available ? tr("This build has no audio playback: it was built without Qt Multimedia.")
			: loading ? tr("Preparing audio. Use Stop to cancel.") : playing ? tr("Pause the sound (Space).")
			: tr("Play the selected sound from the playhead (Space)."));
	}
	m_commands->setEnabled(QStringLiteral("audio.playPause"), available && !loading && (active || m_audioSelectedPlayable) &&
		(!m_audioSessionDialog || !m_audioSessionDialog->recordingOpen()));
	m_commands->setEnabled(QStringLiteral("audio.stop"), available && active);
	m_commands->setEnabled(QStringLiteral("audio.loop"), available);
	if (m_audioTimeLabel) {
		const QString status = loading ? tr("Loading") : m_audioPlayback->stalled() ? tr("Buffering")
			: playing ? tr("Playing") : state == AudioPlayback::State::Paused ? tr("Paused")
			: state == AudioPlayback::State::Error ? tr("Playback failed") : tr("Stopped");
		m_audioTimeLabel->setText(tr("%1 · %2 / %3").arg(status, audioClockText(position), audioClockText(m_audioDurationMs)));
		m_audioTimeLabel->setAccessibleDescription(status);
	}
	if (m_audioWaveform) { m_audioWaveform->setPlayhead(position); }
	if (m_audioSeek) {
		const QSignalBlocker blocker(m_audioSeek);
		m_audioSeek->setEnabled(m_audioDurationMs > 0 && !loading && (!active || m_audioPlayback->canSeek()));
		if (!m_audioSeek->isSliderDown()) {
			const int maximum = static_cast<int>(std::min<qint64>(m_audioDurationMs, std::numeric_limits<int>::max()));
			m_audioSeek->setRange(0, maximum);
			const qint64 bounded = std::clamp<qint64>(position, 0, m_audioDurationMs);
			m_audioSeek->setValue(m_audioDurationMs == maximum ? static_cast<int>(bounded)
				: static_cast<int>(static_cast<long double>(bounded) / m_audioDurationMs * maximum));
		}
		m_audioSeek->setAccessibleDescription(tr("%1 milliseconds of %2").arg(position).arg(m_audioDurationMs));
	}
}

} // namespace vibestudio
