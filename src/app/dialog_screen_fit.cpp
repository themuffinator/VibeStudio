#include "app/dialog_screen_fit.h"

#include <QCursor>
#include <QGuiApplication>
#include <QLayout>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace vibestudio {

namespace {

QRect& screenAreaOverride()
{
	static QRect area;
	return area;
}

} // namespace

QSize boundedDialogSize(const QSize& preferred, const QSize& minimum, const QSize& available)
{
	const QSize room = (available - QSize(2 * dialogScreenMargin, 2 * dialogScreenMargin)).expandedTo(QSize(0, 0));
	return preferred.boundedTo(room).expandedTo(minimum);
}

QRect dialogScreenArea(const QWidget* dialog)
{
	if (!screenAreaOverride().isEmpty()) {
		return screenAreaOverride();
	}
	// Offscreen rendering (tests and snapshots) has no real screen: its
	// placeholder 800x600 screen says nothing about where a window is seen.
	if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
		return {};
	}
	QScreen* screen = nullptr;
	// A dialog opens over its parent's window, so it shares that window's
	// screen; an embedded editor's parent window is the studio shell.
	if (dialog && dialog->parentWidget()) {
		screen = dialog->parentWidget()->window()->screen();
	}
	if (!screen) {
		screen = QGuiApplication::screenAt(QCursor::pos());
	}
	if (!screen) {
		screen = QGuiApplication::primaryScreen();
	}
	return screen ? screen->availableGeometry() : QRect();
}

void setDialogScreenAreaOverride(const QRect& area)
{
	screenAreaOverride() = area;
}

void fitDialogToScreen(QWidget* dialog, const QSize& preferred)
{
	if (!dialog) {
		return;
	}
	// Polishing applies the theme's fonts and paddings, which the minimum
	// depends on.
	dialog->ensurePolished();
	const QSize minimum = dialog->minimumSizeHint().expandedTo(dialog->minimumSize());
	QSize wanted = preferred;
	// A scrolling body scrolls down, not sideways: the dialog asks for the
	// width its content cannot do without (large text, a long translation),
	// which the screen then bounds.
	auto *body = dialog->findChild<QScrollArea *>(QStringLiteral("dialogBodyScroll"));
	if (body && body->widget() && body->parentWidget() == dialog)
	{
		body->widget()->ensurePolished();
		const QMargins margins = dialog->layout() ? dialog->layout()->contentsMargins() : QMargins();
		wanted.setWidth(std::max(wanted.width(), body->widget()->minimumSizeHint().width() + body->verticalScrollBar()->sizeHint().width() +
													 2 * body->frameWidth() + margins.left() + margins.right()));
	}
	const QRect area = dialogScreenArea(dialog);
	dialog->resize(area.isEmpty() ? wanted.expandedTo(minimum) : boundedDialogSize(wanted, minimum, area.size()));
}

QVBoxLayout* addScrollingDialogBody(QVBoxLayout* dialogLayout)
{
	auto* scroll = new QScrollArea;
	scroll->setObjectName(QStringLiteral("dialogBodyScroll"));
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	auto* body = new QWidget;
	body->setObjectName(QStringLiteral("dialogBody"));
	auto* layout = new QVBoxLayout(body);
	layout->setContentsMargins(0, 0, 0, 0);
	scroll->setWidget(body);
	dialogLayout->addWidget(scroll, 1);
	return layout;
}

} // namespace vibestudio
