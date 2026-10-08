#include "app/studio_accessibility.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QStatusBar>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>

namespace vibestudio {

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
