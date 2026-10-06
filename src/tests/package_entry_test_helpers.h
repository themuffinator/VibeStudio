#pragma once

#include "app/package_entry_view.h"
#include "app/package_staging_view.h"
#include <type_traits>

#include <QApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QThread>
#include <memory>
#include <stdexcept>
#include <vector>

namespace vibestudio::tests {

// Existing workflow fixtures assert row semantics, not the concrete Qt item
// container. Await the asynchronous listing, then address real model indexes.
// Responsiveness/cancellation tests use PackageEntryView directly without this
// waiter, so they can inspect pending and stale-result states.
inline void waitForPackageEntries(PackageEntryView* view)
{
	QElapsedTimer elapsed; elapsed.start();
	while (view && view->busy() && elapsed.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	if (view && view->busy()) { throw std::runtime_error("Timed out waiting for package entries"); }
}

struct PackageRow {
	QPointer<QListView> view;
	QPersistentModelIndex index;
	QVariant data(int role) const { return index.data(role); }
	QString text() const { return data(Qt::DisplayRole).toString(); }
	QString toolTip() const { return data(Qt::ToolTipRole).toString(); }
	Qt::ItemFlags flags() const { return index.flags(); }
	bool isHidden() const { return false; }
	bool isSelected() const { return view && view->selectionModel()->isSelected(index); }
	void setSelected(bool selected) { if (view) { view->selectionModel()->select(index, selected ? QItemSelectionModel::Select : QItemSelectionModel::Deselect); } }
	QFont font() const { return data(Qt::FontRole).isValid() ? data(Qt::FontRole).value<QFont>() : view ? view->font() : QFont(); }
};

template<class View> class PackageModelRows {
	struct State { QPointer<View> view; std::vector<std::unique_ptr<PackageRow>> rows; };
	std::shared_ptr<State> m_state = std::make_shared<State>();
	View* ready() const { if constexpr (std::is_same_v<View, PackageEntryView>) { waitForPackageEntries(m_state->view); } return m_state->view; }
public:
	explicit PackageModelRows(View* view = nullptr) { m_state->view = view; }
	PackageModelRows* operator->() { return this; }
	const PackageModelRows* operator->() const { return this; }
	explicit operator bool() const { return m_state->view; }
	operator View*() const { return m_state->view; }
	int count() const { auto* view = ready(); return view ? view->model()->rowCount() : 0; }
	PackageRow* item(int row) const
	{
		auto* view = ready(); if (!view || row < 0 || row >= view->model()->rowCount()) { return nullptr; }
		const auto index = view->model()->index(row, 0);
		for (const auto& entry : m_state->rows) { if (entry->index == index) { return entry.get(); } }
		m_state->rows.push_back(std::make_unique<PackageRow>(PackageRow{view, index})); return m_state->rows.back().get();
	}
	PackageRow* currentItem() const { auto* view = ready(); return view && view->currentIndex().isValid() ? item(view->currentIndex().row()) : nullptr; }
	int currentRow() const { auto* view = ready(); return view ? view->currentIndex().row() : -1; }
	QList<PackageRow*> selectedItems() const { QList<PackageRow*> result; auto* view = ready(); if (view) { for (const auto& index : view->selectionModel()->selectedRows()) { result << item(index.row()); } } return result; }
	void setCurrentItem(PackageRow* row, QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::ClearAndSelect) { auto* view = ready(); if (view) { view->selectionModel()->setCurrentIndex(row ? QModelIndex(row->index) : QModelIndex(), flags); } }
	void setCurrentRow(int row) { setCurrentItem(item(row)); }
	void clearSelection() { if (auto* view = ready()) { view->clearSelection(); } }
	void selectAll() { if (auto* view = ready()) { view->selectAll(); } }
	void itemActivated(PackageRow* row) { if (auto* view = ready(); view && row) { emit view->activated(row->index); } }
	void scrollToItem(PackageRow* row) { if (auto* view = ready(); view && row) { view->scrollTo(row->index); } }
	QAbstractItemModel* model() const { auto* view = ready(); return view ? view->model() : nullptr; }
	QItemSelectionModel* selectionModel() const { auto* view = ready(); return view ? view->selectionModel() : nullptr; }
	QWidget* parentWidget() const { return m_state->view->parentWidget(); }
	QWidget* viewport() const { return m_state->view->viewport(); }
	void setFocus(Qt::FocusReason reason = Qt::OtherFocusReason) { m_state->view->setFocus(reason); }
	Qt::FocusPolicy focusPolicy() const { return m_state->view->focusPolicy(); }
	QString accessibleName() const { return m_state->view->accessibleName(); }
	bool hasFocus() const { return m_state->view->hasFocus(); }
	bool isVisible() const { return m_state->view->isVisible(); }
	QRect visualItemRect(PackageRow* row) const { return row ? m_state->view->visualRect(row->index) : QRect(); }
	QRect rect() const { return m_state->view->rect(); }
	QPoint mapToGlobal(const QPoint& point) const { return m_state->view->mapToGlobal(point); }
	QPoint mapTo(const QWidget* target, const QPoint& point) const { return m_state->view->mapTo(target, point); }
};

using PackageRows = PackageModelRows<PackageEntryView>;
using StagingRows = PackageModelRows<PackageStagingView>;

template<class View> int rowWithData(const PackageModelRows<View>& list, int role, const QVariant& value)
{
	for (int row = 0; row < list->count(); ++row) { if (list->item(row)->data(role) == value) { return row; } }
	return -1;
}
inline void activateRow(PackageRows list, int row) { list->setCurrentRow(row); list->itemActivated(list->item(row)); }

} // namespace vibestudio::tests
