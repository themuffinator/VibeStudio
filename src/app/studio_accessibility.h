#pragma once

// Accessibility plumbing the shell shares with its dialogs: announcements to
// the screen reader, how long status bar messages stay, and the words used to
// name the accessibility preferences.

#include "core/studio_settings.h"

#include <QString>

#include <functional>

class QLabel;
class QObject;
class QProgressBar;
class QStatusBar;

namespace vibestudio {

// Asks the screen reader to speak `message` without moving focus, through
// QAccessibleAnnouncementEvent (Qt 6.8 and later). Assertive announcements
// interrupt what the screen reader is saying; use them for failures only.
// Returns false when no assistive technology is listening or the Qt in use
// cannot announce, so the caller knows the message was not delivered.
bool announceToScreenReader(QObject* source, const QString& message, bool assertive = false);

// The accessibility preferences, recorded by the shell wherever it applies
// them, for widgets built afterwards that start motion or speech of their
// own: previews in dialogs, busy bars and status announcements. Until the
// shell records any they are the defaults (announcements on, motion on).
void recordStudioAccessibilityPreferences(const AccessibilityPreferences& preferences);
[[nodiscard]] const AccessibilityPreferences& studioAccessibilityPreferences();
[[nodiscard]] bool studioReducedMotion();

// What a status message is, which decides whether it is spoken. An update
// (counts after an edit, a readout while dragging or typing) is read only
// when the user reaches its line. A result (a save, an export, a tool's
// response to a key) is announced politely, and so is a long task that
// finished, which speech reads under the same event as the studio's own long
// tasks ("Long tasks that finish") rather than with status messages. A
// problem (a refusal, a failure) is announced assertively.
enum class LiveStatus { Update, Result, Finished, Problem };

// Shows `text` on a status label that keeps a fixed accessible name ("Mesh
// editor status"). Qt reads a label's accessible name instead of its text
// once a name is set, so the text also becomes the accessible description;
// changing it tells assistive technology that the line changed. A result or
// a problem is announced as well (announceStatus).
void setLiveStatus(QLabel* label, const QString& text, LiveStatus kind = LiveStatus::Update);

// Announces a result or a problem when the user has screen reader
// announcements on; an update is never announced. Messages from one pass of
// the event loop settle into one, so a refused edit that reports itself
// twice is heard once: a problem outranks a result, and the later message
// wins at the same rank. A result that follows a long task's end in the same
// pass (the operation's own "Exported …" line after "… finished.") keeps
// the Finished kind. The same words are not repeated within two seconds, so
// typing into a field that keeps refusing is not read out at every key.
// Returns false when there is nothing to announce.
bool announceStatus(QObject* source, const QString& message, LiveStatus kind);

// Hears every settled announcement, whether or not a screen reader is
// listening: the shell reads them aloud when speech is on, and tests count
// them. There is one listener; an empty function removes it.
using StatusAnnouncementListener = std::function<void(const QString& message, LiveStatus kind)>;
void setStatusAnnouncementListener(StatusAnnouncementListener listener);

// Shows a progress bar as busy with no known total: Qt's moving marquee, or,
// under reduced motion, a still empty bar that reads `text` ("Loading…"), as
// LoadingPane does. A caller that later learns the total sets the range and
// puts back the "%p%" format.
void setBusyProgress(QProgressBar* bar, bool reducedMotion, const QString& text);

// Retimes the message the status bar is showing to the preferred duration.
// Call it from QStatusBar::messageChanged: QStatusBar starts the private
// timer of a timed message before it emits that signal (Qt 6.10.1
// qstatusbar.cpp, reviewed, nothing copied), so Longer triples the time left
// and UntilReplaced stops the timer. Untimed messages are left alone.
void applyStatusMessageDuration(QStatusBar* statusBar, MessageDuration duration);

// Translated names for the accessibility choices, for Settings and reports.
QString localizedColorVisionName(ColorVision vision);
QString localizedMessageDurationName(MessageDuration duration);
QString localizedSpeechEventName(const QString& eventId);

} // namespace vibestudio
