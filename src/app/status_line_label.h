#pragma once

#include "app/live_status_label.h"

#include <QEvent>
#include <QFocusEvent>
#include <QHelpEvent>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QToolTip>

#include <algorithm>

namespace vibestudio {

// One line of status text that elides instead of wrapping or widening its
// window. A wrapping status line grows a row whenever a long message arrives,
// which shrinks the view beside it (and a resize cancels a drag in that view);
// a plain QLabel's minimum width is its whole text, which forces the window
// wider. Unlike ElidedLabel, text() stays the whole message whichever pointer
// set it: only the painting is cut short. A line cut short shows everything in
// its tooltip and takes keyboard focus, showing everything when focused, so it
// can be read without a pointer. Its text is also its accessible description
// (LiveStatusLabel), so a screen reader hears the message, not only the name.
class StatusLineLabel final : public LiveStatusLabel {
public:
	explicit StatusLineLabel(QWidget* parent = nullptr) : LiveStatusLabel(parent)
	{
		setTextFormat(Qt::PlainText);
		setWordWrap(false);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		setFocusPolicy(Qt::NoFocus);
	}
	// Paths read best cut in the middle, so both ends stay in view.
	void setElideMode(Qt::TextElideMode mode)
	{
		m_mode = mode;
		update();
	}
	[[nodiscard]] bool isElided() const { return fontMetrics().horizontalAdvance(line()) > textArea().width(); }
	[[nodiscard]] QSize sizeHint() const override
	{
		const int pad = 2 * (frameWidth() + margin());
		const QMargins margins = contentsMargins();
		return {fontMetrics().horizontalAdvance(line()) + margins.left() + margins.right() + pad + 2,
			fontMetrics().height() + margins.top() + margins.bottom() + pad};
	}
	[[nodiscard]] QSize minimumSizeHint() const override
	{
		const QSize hint = sizeHint();
		return {std::min(hint.width(), fontMetrics().averageCharWidth() * 8), hint.height()};
	}

protected:
	bool event(QEvent* event) override
	{
		if (event->type() == QEvent::ToolTip && isElided()) {
			QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), wholeText(), this);
			return true;
		}
		return QLabel::event(event);
	}
	void focusInEvent(QFocusEvent* event) override
	{
		QLabel::focusInEvent(event);
		update();
		if (isElided()) {
			QToolTip::showText(mapToGlobal(QPoint(0, height())), wholeText(), this);
		}
	}
	void focusOutEvent(QFocusEvent* event) override
	{
		QLabel::focusOutEvent(event);
		QToolTip::hideText();
		update();
		refreshFocusPolicy();
	}
	void resizeEvent(QResizeEvent* event) override
	{
		QLabel::resizeEvent(event);
		refreshFocusPolicy();
	}
	void paintEvent(QPaintEvent*) override
	{
		// A new message repaints the line: whether it is cut short may have changed.
		refreshFocusPolicy();
		QPainter painter(this);
		drawFrame(&painter);
		const QRect area = textArea();
		const QString shown = fontMetrics().elidedText(line(), m_mode, area.width());
		style()->drawItemText(&painter, area, QStyle::visualAlignment(layoutDirection(), alignment()), palette(), isEnabled(), shown,
			foregroundRole());
		if (hasFocus()) {
			QStyleOptionFocusRect frame;
			frame.initFrom(this);
			frame.rect = rect();
			style()->drawPrimitive(QStyle::PE_FrameFocusRect, &frame, &painter, this);
		}
	}

private:
	// Several lines run together on the one line.
	[[nodiscard]] QString line() const { return QString(text()).replace(QLatin1Char('\n'), QLatin1Char(' ')).simplified(); }
	[[nodiscard]] QRect textArea() const { return contentsRect().adjusted(margin(), margin(), -margin(), -margin()); }
	[[nodiscard]] QString wholeText() const { return toolTip().isEmpty() ? text() : text() + QStringLiteral("\n\n") + toolTip(); }
	// Only a line cut short takes keyboard focus, since only then is there more
	// to read; a focused line keeps it until focus moves on.
	void refreshFocusPolicy()
	{
		const Qt::FocusPolicy wanted = isElided() ? Qt::TabFocus : Qt::NoFocus;
		if (focusPolicy() != wanted && !hasFocus()) {
			setFocusPolicy(wanted);
		}
	}

	Qt::TextElideMode m_mode = Qt::ElideRight;
};

} // namespace vibestudio
