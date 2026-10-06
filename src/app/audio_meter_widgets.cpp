#include "app/audio_meter_widgets.h"
#include <QPainter>
#include <QStyle>
#include <QStyleOptionProgressBar>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
QString audioMeterNumeric(const QString &text) { return QChar(0x2066) + text + QChar(0x2069); }
QString audioMeterDb(double amplitude)
{
	return audioMeterNumeric(amplitude > 0 ? QString::number(20 * std::log10(amplitude), 'f', 1)
	                                       : QStringLiteral("−∞"));
}
void AudioMeterDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	QStyledItemDelegate::paint(painter, option, index);
	if (!index.data(AudioMeterLevelRole).isValid() || !option.widget)
		return;
	QStyleOptionProgressBar bar;
	bar.rect = option.rect.adjusted(3, option.rect.height() - 6, -3, -2);
	bar.palette = option.palette;
	bar.state = option.state | QStyle::State_Horizontal;
	bar.direction = option.direction;
	bar.minimum = -720;
	bar.maximum = 120;
	bar.progress = int(std::clamp(index.data(AudioMeterLevelRole).toDouble(), -72.0, 12.0) * 10);
	bar.textVisible = false;
	option.widget->style()->drawControl(QStyle::CE_ProgressBar, &bar, painter, option.widget);
}
} // namespace vibestudio
