#include "app/level_transform_layout.h"
#include <QDoubleSpinBox>
#include <QLabel>
#include <QStyle>
#include <algorithm>

namespace vibestudio {
namespace {
int labelHeight(const QLabel* label, int width)
{
	return std::max(label->minimumSizeHint().height(),label->heightForWidth(std::max(1,width)));
}
}
LevelTransformLayout::LevelTransformLayout(QWidget* parent, const std::array<QLabel*,2>& rows,
	const Labels& axes, const Fields& fields, QLabel* note)
	: QLayout(parent), m_rows(rows), m_axes(axes), m_fields(fields), m_note(note)
{
	setContentsMargins(0,0,0,0); setSpacing(4);
	for (int row = 0; row < 2; ++row) {
		addWidget(m_rows[row]);
		for (int axis = 0; axis < 3; ++axis) { addWidget(m_axes[row][axis]); addWidget(m_fields[row][axis]); }
	}
	addWidget(m_note);
}
LevelTransformLayout::~LevelTransformLayout() { while (auto* item = takeAt(0)) { delete item; } }
void LevelTransformLayout::addItem(QLayoutItem* item) { m_items.append(item); invalidate(); }
int LevelTransformLayout::count() const { return static_cast<int>(m_items.size()); }
QLayoutItem* LevelTransformLayout::itemAt(int index) const { return m_items.value(index); }
QLayoutItem* LevelTransformLayout::takeAt(int index) { return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr; }
int LevelTransformLayout::fieldWidth() const
{
	int width = 0;
	for (const auto& row : m_fields) { for (const auto* field : row) { width = std::max(width,field->sizeHint().width()); } }
	return width;
}
int LevelTransformLayout::axisWidth() const
{
	int width = 0;
	for (const auto& row : m_axes) { for (const auto* label : row) { width = std::max(width,label->fontMetrics().horizontalAdvance(label->text())); } }
	return width;
}
int LevelTransformLayout::matrixWidth() const
{
	const int caption = std::max(m_rows[0]->fontMetrics().horizontalAdvance(m_rows[0]->text()),m_rows[1]->fontMetrics().horizontalAdvance(m_rows[1]->text()));
	return caption + 3 * std::max(fieldWidth(),axisWidth()) + 3 * spacing();
}
QSize LevelTransformLayout::sizeHint() const { const int width = matrixWidth(); return {width,heightForWidth(width)}; }
QSize LevelTransformLayout::minimumSize() const
{
	// A narrow column can put each axis above its editor. Do not let the
	// currently displayed matrix impose its wider minimum on a scroll area.
	int width = std::max(fieldWidth(),m_note->minimumSizeHint().width());
	for (const auto* label : m_rows) { width = std::max(width,label->minimumSizeHint().width()); }
	return {width,sizeHint().height()};
}
bool LevelTransformLayout::hasHeightForWidth() const { return true; }
int LevelTransformLayout::heightForWidth(int width) const { return arrange({0,0,width,0},false); }
void LevelTransformLayout::setGeometry(const QRect& rect) { QLayout::setGeometry(rect); arrange(rect,true); }
int LevelTransformLayout::arrange(const QRect& rect, bool apply) const
{
	const auto bounds = rect.marginsRemoved(contentsMargins());
	const int width = std::max(1,bounds.width()), gap = spacing();
	const bool matrix = width >= matrixWidth();
	const auto place = [&](QWidget* widget, int x, int y, int w, int h) {
		if (apply) { widget->setGeometry(QStyle::visualRect(parentWidget()->layoutDirection(),bounds,{bounds.x()+x,bounds.y()+y,w,h})); }
	};
	if (apply) { for (auto* label : m_axes[1]) { label->setVisible(!matrix); } }
	int y = 0;
	if (matrix) {
		const int caption = std::max(m_rows[0]->fontMetrics().horizontalAdvance(m_rows[0]->text()),m_rows[1]->fontMetrics().horizontalAdvance(m_rows[1]->text()));
		const int field = (width-caption-3*gap)/3;
		int heading = 0;
		for (const auto* label : m_axes[0]) { heading = std::max(heading,labelHeight(label,field)); }
		for (int axis = 0; axis < 3; ++axis) { place(m_axes[0][axis],caption+gap+axis*(field+gap),y,field,heading); }
		y += heading+gap;
		for (int row = 0; row < 2; ++row) {
			int height = labelHeight(m_rows[row],caption);
			for (const auto* widget : m_fields[row]) { height = std::max(height,widget->sizeHint().height()); }
			place(m_rows[row],0,y,caption,height);
			for (int axis = 0; axis < 3; ++axis) { place(m_fields[row][axis],caption+gap+axis*(field+gap),y,field,height); }
			y += height+gap;
		}
	} else {
		const int axisWidth = this->axisWidth();
		const bool inlineAxis = width >= axisWidth+gap+fieldWidth();
		for (int row = 0; row < 2; ++row) {
			const int captionHeight = labelHeight(m_rows[row],width);
			place(m_rows[row],0,y,width,captionHeight); y += captionHeight+gap;
			for (int axis = 0; axis < 3; ++axis) {
				const int label = labelHeight(m_axes[row][axis],inlineAxis ? axisWidth : width);
				const int height = m_fields[row][axis]->sizeHint().height();
				if (inlineAxis) {
					const int rowHeight = std::max(label,height);
					place(m_axes[row][axis],0,y,axisWidth,rowHeight);
					place(m_fields[row][axis],axisWidth+gap,y,width-axisWidth-gap,rowHeight); y += rowHeight+gap;
				} else {
					place(m_axes[row][axis],0,y,width,label); y += label+gap;
					place(m_fields[row][axis],0,y,width,height); y += height+gap;
				}
			}
		}
	}
	const int note = labelHeight(m_note,width); place(m_note,0,y,width,note);
	return y+note+contentsMargins().top()+contentsMargins().bottom();
}
} // namespace vibestudio
