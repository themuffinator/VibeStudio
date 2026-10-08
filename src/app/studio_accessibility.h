#pragma once

// Accessibility plumbing the shell shares with its dialogs: announcements to
// the screen reader, how long status bar messages stay, and the words used to
// name the accessibility preferences.

#include "core/studio_settings.h"

#include <QString>

class QObject;
class QStatusBar;

namespace vibestudio {

// Asks the screen reader to speak `message` without moving focus, through
// QAccessibleAnnouncementEvent (Qt 6.8 and later). Assertive announcements
// interrupt what the screen reader is saying; use them for failures only.
// Returns false when no assistive technology is listening or the Qt in use
// cannot announce, so the caller knows the message was not delivered.
bool announceToScreenReader(QObject* source, const QString& message, bool assertive = false);

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
