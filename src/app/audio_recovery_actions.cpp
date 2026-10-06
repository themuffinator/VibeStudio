#include "app/application_shell.h"
#include "app/audio_editor_dialog.h"
#include "app/audio_recovery.h"
#include "app/audio_recovery_dialog.h"
#include "app/audio_session_dialog.h"
#include "app/studio_layout.h"

#include <QCheckBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <utility>

namespace vibestudio
{
QWidget *ApplicationShell::buildAudioRecoveryPreferences()
{
	auto *panel = new QGroupBox(tr("Audio Recovery"));
	panel->setObjectName(QStringLiteral("audioRecoveryPreferences"));
	panel->setAccessibleName(tr("Audio recovery preferences"));
	auto *layout = new QFormLayout(panel);
	layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_audioRecoveryEnabled = new QCheckBox(tr("Keep recovery copies"));
	m_audioRecoveryEnabled->setObjectName(QStringLiteral("setupAudioRecoveryEnabled"));
	m_audioRecoveryEnabled->setAccessibleName(tr("Keep local audio recovery copies"));
	m_audioRecoveryEnabled->setToolTip(
	    tr("Checkpoint unsaved waveform edits and multitrack sessions on this device. Copies may contain "
	       "private audio. Turning this off retains existing copies."));
	m_audioRecoveryEnabled->setChecked(m_settings.audioRecoveryEnabled());
	layout->addRow(m_audioRecoveryEnabled);
	m_audioRecoveryNotify = new QCheckBox(tr("Offer copies at startup"));
	m_audioRecoveryNotify->setObjectName(QStringLiteral("setupAudioRecoveryNotify"));
	m_audioRecoveryNotify->setAccessibleName(tr("Offer audio recovery copies at startup"));
	m_audioRecoveryNotify->setToolTip(tr("Check local recovery filenames in the background at startup. "
	                                     "Review copies before restoring; nothing opens automatically."));
	m_audioRecoveryNotify->setChecked(m_settings.audioRecoveryNotifyAtStartup());
	layout->addRow(m_audioRecoveryNotify);
	auto *folder = new QLineEdit(QDir::toNativeSeparators(audioRecoveryDirectory()));
	folder->setReadOnly(true);
	folder->setAccessibleName(tr("Local audio recovery folder"));
	folder->setToolTip(folder->text());
	folder->setCursorPosition(0);
	layout->addRow(tr("Folder"), folder);
	auto *review = new QPushButton(tr("Review Copies"));
	review->setObjectName(QStringLiteral("setupReviewAudioRecovery"));
	review->setAccessibleName(tr("Review audio recovery copies"));
	review->setToolTip(tr("Verify local copies, restore an unsaved draft, or discard a reviewed copy. "
	                      "Available even when automatic recovery and startup offers are off."));
	layout->addRow(review);
	connect(review, &QPushButton::clicked, this, &ApplicationShell::recoverAudioFromUi);
	connect(m_audioRecoveryEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
		m_settings.setAudioRecoveryEnabled(enabled);
		m_settings.sync();
		if (m_audioEditorDialog) {
			m_audioEditorDialog->setRecoveryEnabled(enabled);
		}
		if (m_audioSessionDialog) {
			m_audioSessionDialog->setRecoveryEnabled(enabled);
		}
	});
	connect(m_audioRecoveryNotify, &QCheckBox::toggled, this, [this](bool enabled) {
		m_settings.setAudioRecoveryNotifyAtStartup(enabled);
		m_settings.sync();
		if (!enabled) {
			m_audioRecoveryNotice.clear();
			if (m_audioRecoveryDiscovery) {
				m_audioRecoveryDiscovery->cancel();
			}
			if (m_audioRecoveryNoticeVisible) {
				m_noticeBar->dismiss();
			}
		}
	});
	return panel;
}

void ApplicationShell::recoverAudioFromUi()
{
	if (m_audioRecoveryDialog) {
		m_audioRecoveryDialog->show();
		m_audioRecoveryDialog->raise();
		return;
	}
	auto *dialog = new AudioRecoveryDialog(audioRecoveryDirectory(), this);
	m_audioRecoveryDialog = dialog;
	dialog->restore = [this](const QString &path, const QByteArray &digest, AudioRecoveryKind kind) {
		if (kind == AudioRecoveryKind::Session) {
			auto *session = ensureAudioSession();
			session->show();
			session->raise();
			if (session->isBusy()) {
				statusBar()->showMessage(tr("Wait for the session operation to finish, then reopen Audio Recoveries."));
			} else {
				session->restoreRecovery(path, digest);
			}
			return;
		}
		auto *editor = ensureAudioEditor();
		if (editor->isBusy()) {
			statusBar()->showMessage(tr("Wait for the audio operation to finish, then reopen Recover Audio."));
			return;
		}
		unloadAudioPlayback();
		refreshAudioTransport();
		editor->show();
		editor->raise();
		editor->refreshContext();
		editor->openProject(path, true, digest);
	};
	dialog->show();
}

void ApplicationShell::discoverAudioRecoveryAtStartup()
{
	if (m_audioRecoveryDiscoveryStarted) {
		return;
	}
	m_audioRecoveryDiscoveryStarted = true;
	if (!m_settings.audioRecoveryNotifyAtStartup()) {
		return;
	}
	m_audioRecoveryDiscovery = new AudioRecoveryDiscovery(this);
	m_audioRecoveryDiscovery->setObjectName(QStringLiteral("audioRecoveryDiscovery"));
	connect(m_noticeBar, &NoticeBar::dismissed, this, [this]() {
		m_audioRecoveryNoticeVisible = false;
		// A replacement notice can be installed immediately after dismissed().
		// Check again after that handler returns, without taking its place.
		QTimer::singleShot(0, this, &ApplicationShell::offerAudioRecoveryNotice);
	});
	m_audioRecoveryDiscovery->finished = [this](const AudioRecoveryInventory &inventory) {
		if (inventory.cancelled || !m_settings.audioRecoveryNotifyAtStartup()) {
			return;
		}
		if (!inventory.error.isEmpty()) {
			m_audioRecoveryNotice =
			    tr("Audio recovery discovery needs attention. Open Recover Audio to inspect the folder.");
		} else if (!inventory.records.isEmpty() || inventory.truncated) {
			m_audioRecoveryNotice = tr("%n audio recovery file(s) found. Review copies before restoring.", nullptr,
			                           inventory.records.size());
			if (inventory.truncated) {
				m_audioRecoveryNotice += QLatin1Char(' ') + tr("The directory scan reached its limit.");
			}
		} else {
			return;
		}
		recordActivity(tr("Audio recovery available"), audioRecoveryDirectory(), QStringLiteral("audio"),
		               OperationState::Warning, inventory.error.isEmpty() ? m_audioRecoveryNotice : inventory.error);
		offerAudioRecoveryNotice();
	};
	statusBar()->showMessage(tr("Checking for local audio recovery copies…"), 3000);
	m_audioRecoveryDiscovery->start(audioRecoveryDirectory());
}

void ApplicationShell::offerAudioRecoveryNotice()
{
	if (m_audioRecoveryNotice.isEmpty() || !m_settings.audioRecoveryNotifyAtStartup() || !m_noticeBar->isHidden()) {
		return;
	}
	const QString text = std::exchange(m_audioRecoveryNotice, {});
	m_noticeBar->showNotice(QStringLiteral("warning"), tr("Local audio recovery"), text);
	m_audioRecoveryNoticeVisible = true;
	auto *review = m_noticeBar->addAction(tr("Review Audio"), QStringLiteral("history"), true);
	review->setObjectName(QStringLiteral("noticeReviewAudio"));
	review->setAccessibleName(tr("Review audio recovery copies"));
	connect(review, &QPushButton::clicked, this, [this]() {
		m_noticeBar->dismiss();
		recoverAudioFromUi();
	});
}
} // namespace vibestudio
