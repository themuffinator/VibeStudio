#include "app/application_shell.h"
#include "app/audio_editor_dialog.h"
#include "app/audio_session_dialog.h"
#include <QCheckBox>
#include <QSignalBlocker>
#include <QStatusBar>

namespace vibestudio
{
AudioSessionDialog *ApplicationShell::ensureAudioSession()
{
	if (!m_audioSessionDialog) {
		auto *dialog = new AudioSessionDialog(this);
		m_audioSessionDialog = dialog;
		connect(dialog, &AudioSessionDialog::recoveryPreferenceChanged, this, [this](bool enabled) {
			if (m_audioRecoveryEnabled) {
				const QSignalBlocker blocker(m_audioRecoveryEnabled);
				m_audioRecoveryEnabled->setChecked(enabled);
			}
			if (m_audioEditorDialog) {
				m_audioEditorDialog->setRecoveryEnabled(enabled);
			}
		});
		connect(dialog, &AudioSessionDialog::waveformRecoveryRequested, this,
		        [this](const QString &path, const QByteArray &digest) {
			        auto *editor = ensureAudioEditor();
			        editor->show();
			        editor->raise();
			        if (editor->isBusy()) {
				        statusBar()->showMessage(
				            tr("Wait for the audio operation to finish, then reopen Audio Recoveries."));
			        } else {
				        editor->openProject(path, true, digest);
			        }
		        });
		dialog->beforePlayback = [this] {
			unloadAudioPlayback();
			refreshAudioTransport();
			if (m_audioEditorDialog) {
				m_audioEditorDialog->stopPlayback();
			}
		};
		dialog->beforeRecording = [this](QObject *context, auto complete) {
			unloadAudioPlayback();
			refreshAudioTransport();
			struct Pending { int remaining = 0; bool stopped = true; };
			auto pending = std::make_shared<Pending>();
			pending->remaining = m_audioEditorDialog ? 2 : 1;
			const auto acknowledged = [pending, complete](bool stopped) {
				pending->stopped = pending->stopped && stopped;
				if (--pending->remaining == 0)
					complete(pending->stopped ? QString{} : tr("Audition changed while preparing recording. Stop playback and try again."));
			};
			m_audioPlayback->stopAndWait(context, acknowledged);
			if (m_audioEditorDialog) m_audioEditorDialog->stopPlaybackAndWait(context, acknowledged);
		};
		connect(dialog, &AudioSessionDialog::mixReady, this, [this](const QByteArray &wav, const QString &source) {
			auto *editor = ensureAudioEditor();
			editor->show();
			editor->raise();
			editor->refreshContext();
			if (!editor->loadSource(tr("Session mixdown.wav"), source, [wav](QString *) { return wav; })) {
				statusBar()->showMessage(tr(
				    "The Audio Editor could not accept the mixdown yet. Finish its current operation and try again."));
			}
		});
	}
	return m_audioSessionDialog;
}
void ApplicationShell::showAudioSession(const QString &path)
{
	auto *dialog = ensureAudioSession();
	dialog->show();
	dialog->raise();
	if (!path.isEmpty()) {
		dialog->openSession(path);
	}
}
} // namespace vibestudio
