#pragma once

#include "app/level_object_list.h"
#include <QItemSelectionModel>

namespace vibestudio::tests {
inline QModelIndex objectIndex(const LevelObjectList* list, int row)
{
	return list ? list->model()->index(row, 0) : QModelIndex();
}
inline void setObjectCurrentRow(LevelObjectList* list, int row,
	QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::ClearAndSelect)
{
	list->selectionModel()->setCurrentIndex(objectIndex(list, row), flags);
}
inline void setObjectSelected(LevelObjectList* list, int row, bool selected)
{
	list->selectionModel()->select(objectIndex(list, row), selected ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
}
} // namespace vibestudio::tests
