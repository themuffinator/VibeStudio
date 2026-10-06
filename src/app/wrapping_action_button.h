#pragma once

#include <QPushButton>
#include <QPainter>
#include <QStyleOptionButton>

namespace vibestudio {

// Retain Qt's push-button accessibility, focus and activation while allowing
// translated captions to wrap when text scaling leaves little horizontal room.
class WrappingActionButton final : public QPushButton {
public:
	explicit WrappingActionButton(const QString& label, int contentPadding = 8) : QPushButton(label), m_contentPadding(qMax(0, contentPadding))
	{
		QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Minimum); policy.setHeightForWidth(true); setSizePolicy(policy);
	}
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override
	{
		QStyleOptionButton option; initStyleOption(&option); option.rect = QRect(0, 0, qMax(1, width), 100000);
		const QRect content = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this).adjusted(m_contentPadding, 0, -m_contentPadding, 0);
		const int height = fontMetrics().boundingRect(QRect(0, 0, qMax(1, content.width()), 100000), Qt::TextWordWrap | Qt::TextShowMnemonic, text()).height();
		return height + option.rect.height() - content.height() + 2 * m_contentPadding;
	}
	QSize sizeHint() const override
	{
		ensurePolished();
		QStyleOptionButton option; initStyleOption(&option);
		const QSize contents(fontMetrics().horizontalAdvance(text()) + 2 * m_contentPadding,
			fontMetrics().height() + 2 * m_contentPadding);
		const auto styled = style()->sizeFromContents(QStyle::CT_PushButton, &option, contents, this);
		const int width = qMin(styled.width(), fontMetrics().averageCharWidth() * 40 + qMax(0, styled.width() - contents.width()));
		return {width, heightForWidth(width)};
	}
	QSize minimumSizeHint() const override { return {qMin(sizeHint().width(), fontMetrics().averageCharWidth() * 12), sizeHint().height()}; }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QStyleOptionButton option; initStyleOption(&option); option.text.clear();
		QPainter painter(this); style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
		QRect content = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this).adjusted(m_contentPadding, 0, -m_contentPadding, 0);
		style()->drawItemText(&painter, content, Qt::AlignCenter | Qt::TextWordWrap | Qt::TextShowMnemonic, option.palette, isEnabled(), text(), QPalette::ButtonText);
	}
private:
	int m_contentPadding;
};


} // namespace vibestudio
