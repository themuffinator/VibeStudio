#pragma once

#include <QCheckBox>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionButton>

#include <algorithm>

namespace vibestudio {

// Retains Qt's check box accessibility, focus and toggling while letting a
// translated caption wrap under itself when text scaling leaves a sidebar
// little width, as WrappingActionButton does for push buttons. A plain
// QCheckBox is never narrower than its whole caption.
class WrappingCheckBox final : public QCheckBox {
public:
	explicit WrappingCheckBox(const QString& label) : QCheckBox(label)
	{
		QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		policy.setHeightForWidth(true);
		setSizePolicy(policy);
	}
	[[nodiscard]] bool hasHeightForWidth() const override { return true; }
	[[nodiscard]] int heightForWidth(int width) const override
	{
		const QRect area = textRect(QRect(0, 0, std::max(1, width), 100000));
		const int lines = fontMetrics()
							  .boundingRect(QRect(0, 0, std::max(1, area.width()), 100000), Qt::TextWordWrap | Qt::TextShowMnemonic, text())
							  .height();
		return std::max(QCheckBox::sizeHint().height(), lines + 2 * style()->pixelMetric(QStyle::PM_FocusFrameVMargin, nullptr, this));
	}
	[[nodiscard]] QSize sizeHint() const override
	{
		ensurePolished();
		const QSize single = QCheckBox::sizeHint();
		// About forty characters on one line before the caption wraps.
		const int width = std::min(single.width(), indicatorWidth() + fontMetrics().averageCharWidth() * 40);
		return {width, heightForWidth(width)};
	}
	[[nodiscard]] QSize minimumSizeHint() const override
	{
		const int width = std::min(sizeHint().width(), indicatorWidth() + fontMetrics().averageCharWidth() * 10);
		return {width, heightForWidth(width)};
	}

protected:
	// The indicator and the caption as drawn toggle the box, the second line
	// of a wrapped caption included, as Qt's own check box takes its indicator
	// and caption. The blank rest of a row the layout stretched the box across
	// does not, so a click beside a short caption changes nothing.
	[[nodiscard]] bool hitButton(const QPoint& position) const override
	{
		QStyleOptionButton option;
		initStyleOption(&option);
		const QRect indicator = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
		const QRect caption = style()->itemTextRect(fontMetrics(), textRect(rect()), captionFlags(), isEnabled(), text());
		return (indicator | caption).adjusted(-2, -2, 2, 2).contains(position);
	}
	void paintEvent(QPaintEvent*) override
	{
		QStyleOptionButton option;
		initStyleOption(&option);
		const QString caption = option.text;
		const bool focused = option.state.testFlag(QStyle::State_HasFocus);
		// The style draws the indicator; the caption and its focus frame are
		// drawn here, wrapped.
		option.text.clear();
		option.state.setFlag(QStyle::State_HasFocus, false);
		QPainter painter(this);
		style()->drawControl(QStyle::CE_CheckBox, &option, &painter, this);
		const int flags = captionFlags();
		const QRect area = textRect(rect());
		style()->drawItemText(&painter, area, flags, option.palette, isEnabled(), caption, foregroundRole());
		if (focused) {
			QStyleOptionFocusRect frame;
			frame.QStyleOption::operator=(option);
			frame.state |= QStyle::State_HasFocus;
			frame.rect = style()->itemTextRect(fontMetrics(), area, flags, isEnabled(), caption).adjusted(-2, -1, 2, 1) & rect();
			style()->drawPrimitive(QStyle::PE_FrameFocusRect, &frame, &painter, this);
		}
	}

private:
	// How the caption is laid out beside the indicator, mirrored right to left.
	[[nodiscard]] int captionFlags() const
	{
		return QStyle::visualAlignment(layoutDirection(), Qt::AlignLeft | Qt::AlignVCenter) | Qt::TextWordWrap | Qt::TextShowMnemonic;
	}
	[[nodiscard]] int indicatorWidth() const
	{
		return style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, this) +
			style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, this) + 4;
	}
	// The caption's room beside the indicator, mirrored right to left.
	[[nodiscard]] QRect textRect(const QRect& area) const
	{
		QRect text = area.adjusted(indicatorWidth(), 0, 0, 0);
		return QStyle::visualRect(layoutDirection(), area, text);
	}
};

} // namespace vibestudio
