#pragma once
#include <QStyledItemDelegate>

namespace vibestudio
{
inline constexpr int AudioMeterLevelRole = Qt::UserRole + 4;
QString audioMeterNumeric(const QString &text);
QString audioMeterDb(double amplitude);
// Shared native bar painting for session and recording meter tables. Numeric
// item text remains the accessible value and never overlaps the thin bar.
class AudioMeterDelegate final : public QStyledItemDelegate {
  public:
	using QStyledItemDelegate::QStyledItemDelegate;
	void paint(QPainter *, const QStyleOptionViewItem &, const QModelIndex &) const override;
};
} // namespace vibestudio
