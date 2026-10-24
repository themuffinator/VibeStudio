#include "app/studio_accessibility.h"

#include <QAccessible>
#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QStatusBar>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <utility>

namespace vibestudio {

namespace {

AccessibilityPreferences& recordedPreferences()
{
	static AccessibilityPreferences preferences;
	return preferences;
}

StatusAnnouncementListener& announcementListener()
{
	static StatusAnnouncementListener listener;
	return listener;
}

struct PendingAnnouncement {
	QPointer<QObject> source;
	QString message;
	LiveStatus kind = LiveStatus::Update;
	bool scheduled = false;
};

PendingAnnouncement& pendingAnnouncement()
{
	static PendingAnnouncement pending;
	return pending;
}

struct LastAnnouncement {
	QString message;
	QElapsedTimer clock;
};

LastAnnouncement& lastAnnouncement()
{
	static LastAnnouncement last;
	return last;
}

void settleAnnouncement()
{
	const PendingAnnouncement settled = std::exchange(pendingAnnouncement(), PendingAnnouncement{});
	if (settled.message.isEmpty()) {
		return;
	}
	LastAnnouncement& last = lastAnnouncement();
	if (last.message == settled.message && last.clock.isValid() && last.clock.elapsed() < 2000) {
		return;
	}
	last.message = settled.message;
	last.clock.start();
	if (studioAccessibilityPreferences().screenReaderAnnouncements) {
		// A dialog that reported and then closed in the same pass leaves its
		// message to the window that is active now.
		QObject* source = settled.source ? settled.source.data() : static_cast<QObject*>(QApplication::activeWindow());
		announceToScreenReader(source, settled.message, settled.kind == LiveStatus::Problem);
	}
	if (const StatusAnnouncementListener listener = announcementListener()) {
		listener(settled.message, settled.kind);
	}
}

} // namespace

bool announceToScreenReader(QObject* source, const QString& message, bool assertive)
{
	const QString text = message.simplified();
	if (!source || text.isEmpty() || !QAccessible::isActive()) {
		return false;
	}
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	QAccessibleAnnouncementEvent event(source, text);
	event.setPoliteness(assertive ? QAccessible::AnnouncementPoliteness::Assertive : QAccessible::AnnouncementPoliteness::Polite);
	QAccessible::updateAccessibility(&event);
	return true;
#else
	Q_UNUSED(assertive);
	return false;
#endif
}

void recordStudioAccessibilityPreferences(const AccessibilityPreferences& preferences)
{
	recordedPreferences() = preferences;
}

const AccessibilityPreferences& studioAccessibilityPreferences()
{
	return recordedPreferences();
}

bool studioReducedMotion()
{
	return recordedPreferences().reducedMotion;
}

void setLiveStatus(QLabel* label, const QString& text, LiveStatus kind)
{
	if (!label) {
		return;
	}
	label->setText(text);
	// The description is what a screen reader reads, so markup is dropped.
	const bool rich = label->textFormat() == Qt::RichText || (label->textFormat() == Qt::AutoText && Qt::mightBeRichText(text));
	const QString spoken = rich ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
	label->setAccessibleDescription(spoken);
	announceStatus(label, spoken, kind);
}

bool announceStatus(QObject* source, const QString& message, LiveStatus kind)
{
	const QString text = message.simplified();
	if (kind == LiveStatus::Update || text.isEmpty()) {
		return false;
	}
	// A result and a long task's end rank alike; a problem outranks both.
	const auto rank = [](LiveStatus status) { return status == LiveStatus::Problem ? 2 : 1; };
	PendingAnnouncement& pending = pendingAnnouncement();
	if (pending.message.isEmpty() || rank(kind) >= rank(pending.kind)) {
		const bool longTask = !pending.message.isEmpty() && pending.kind == LiveStatus::Finished && kind == LiveStatus::Result;
		pending.source = source;
		pending.message = text;
		pending.kind = longTask ? LiveStatus::Finished : kind;
	}
	if (!pending.scheduled) {
		if (!QCoreApplication::instance()) {
			settleAnnouncement();
			return true;
		}
		pending.scheduled = true;
		QTimer::singleShot(0, QCoreApplication::instance(), settleAnnouncement);
	}
	return true;
}

void setStatusAnnouncementListener(StatusAnnouncementListener listener)
{
	announcementListener() = std::move(listener);
}

void setBusyProgress(QProgressBar* bar, bool reducedMotion, const QString& text)
{
	if (!bar) {
		return;
	}
	// A range of zero to zero is Qt's marquee, which never stops moving; one
	// step that stays at zero is still and keeps the text in view.
	if (reducedMotion) {
		bar->setRange(0, 1);
		bar->setValue(0);
		bar->setTextVisible(true);
	} else {
		bar->setRange(0, 0);
	}
	bar->setFormat(text);
}

void applyStatusMessageDuration(QStatusBar* statusBar, MessageDuration duration)
{
	if (!statusBar || duration == MessageDuration::Standard || statusBar->currentMessage().isEmpty()) {
		return;
	}
	// The status bar's own timer is its only direct QTimer child; the shell
	// parents its timers elsewhere. Anything not running is not a timed
	// message.
	for (QTimer* timer : statusBar->findChildren<QTimer*>(QString(), Qt::FindDirectChildrenOnly)) {
		if (!timer->isActive()) {
			continue;
		}
		if (duration == MessageDuration::UntilReplaced) {
			timer->stop();
		} else {
			timer->start(std::min(timer->interval(), 60000) * 3);
		}
	}
}

QString localizedColorVisionName(ColorVision vision)
{
	switch (vision) {
	case ColorVision::Typical:
		return QCoreApplication::translate("VibeStudioAccessibility", "Standard colours");
	case ColorVision::RedGreen:
		return QCoreApplication::translate("VibeStudioAccessibility", "Red-green safe (protanopia, deuteranopia)");
	case ColorVision::BlueYellow:
		return QCoreApplication::translate("VibeStudioAccessibility", "Blue-yellow safe (tritanopia)");
	case ColorVision::Monochrome:
		return QCoreApplication::translate("VibeStudioAccessibility", "Monochrome (no colour)");
	}
	return QCoreApplication::translate("VibeStudioAccessibility", "Standard colours");
}

QString localizedMessageDurationName(MessageDuration duration)
{
	switch (duration) {
	case MessageDuration::Standard:
		return QCoreApplication::translate("VibeStudioAccessibility", "Standard");
	case MessageDuration::Longer:
		return QCoreApplication::translate("VibeStudioAccessibility", "Three times longer");
	case MessageDuration::UntilReplaced:
		return QCoreApplication::translate("VibeStudioAccessibility", "Until the next message");
	}
	return QCoreApplication::translate("VibeStudioAccessibility", "Standard");
}

QString localizedSpeechEventName(const QString& eventId)
{
	if (eventId == QStringLiteral("task-results")) {
		return QCoreApplication::translate("VibeStudioAccessibility", "Long tasks that finish");
	}
	if (eventId == QStringLiteral("task-problems")) {
		return QCoreApplication::translate("VibeStudioAccessibility", "Failures, warnings, and cancellations");
	}
	if (eventId == QStringLiteral("status-messages")) {
		return QCoreApplication::translate("VibeStudioAccessibility", "Status bar messages");
	}
	return eventId;
}

} // namespace vibestudio
