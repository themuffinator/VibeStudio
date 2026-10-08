#pragma once

// A grid of equal tiles (shape and layout pickers in the sidebars) that takes
// as many columns as its widest tile allows at the current width, so labels
// stay whole at any text scale, in any language and at any sidebar width.
// Hidden tiles leave no gap, and grid layouts follow the layout direction, so
// the tiles mirror right to left.
//
// Re-arranging waits for the event loop: Qt is often part way through showing
// or laying out the tiles when a resize or a show tells the grid to change.
// The grid's height is that of its rows as arranged, so a narrower sidebar
// gives it more rows once the arrangement has run.

#include <QVector>
#include <QWidget>

class QAbstractButton;
class QGridLayout;

namespace vibestudio {

class TileGrid final : public QWidget {
	Q_OBJECT

public:
	explicit TileGrid(int maximumColumns = 4, QWidget* parent = nullptr);

	void addTile(QAbstractButton* tile);
	[[nodiscard]] int columnCount() const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	[[nodiscard]] QVector<QAbstractButton*> shownTiles() const;
	[[nodiscard]] int widestTile() const;
	[[nodiscard]] int columnsFor(int width) const;
	void arrange(int columns);
	void scheduleArrange();

	QGridLayout* m_grid = nullptr;
	QVector<QAbstractButton*> m_tiles;
	int m_maximumColumns = 4;
	int m_columns = 0;
	bool m_arrangePending = false;
};

} // namespace vibestudio
