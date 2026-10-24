#pragma once

#include <QLayout>
#include <QVector>
#include <array>

class QLabel;
class QDoubleSpinBox;

namespace vibestudio {
// Keeps the same native editors while changing between a compact matrix and
// stacked axis rows. Widths come from the current font, locale and style.
class LevelTransformLayout final : public QLayout {
public:
	using Labels = std::array<std::array<QLabel*,3>,2>;
	using Fields = std::array<std::array<QDoubleSpinBox*,3>,2>;
	LevelTransformLayout(QWidget* parent, const std::array<QLabel*,2>& rows,
		const Labels& axes, const Fields& fields, QLabel* note);
	~LevelTransformLayout() override;
	void addItem(QLayoutItem* item) override;
	int count() const override;
	QLayoutItem* itemAt(int index) const override;
	QLayoutItem* takeAt(int index) override;
	QSize sizeHint() const override;
	QSize minimumSize() const override;
	bool hasHeightForWidth() const override;
	int heightForWidth(int width) const override;
	void setGeometry(const QRect& rect) override;
private:
	int arrange(const QRect& rect, bool apply) const;
	int matrixWidth() const;
	int fieldWidth() const;
	int axisWidth() const;
	QVector<QLayoutItem*> m_items;
	std::array<QLabel*,2> m_rows;
	Labels m_axes;
	Fields m_fields;
	QLabel* m_note;
};
} // namespace vibestudio
