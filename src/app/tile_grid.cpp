#include "app/tile_grid.h"

#include <QAbstractButton>
#include <QEvent>
#include <QGridLayout>
#include <QMetaObject>
#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>

namespace vibestudio {

namespace {

constexpr int kSpacing = 4;

} // namespace

TileGrid::TileGrid(int maximumColumns, QWidget* parent)
	: QWidget(parent)
	, m_maximumColumns(std::max(1, maximumColumns))
{
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	// One grid for the widget's life: Qt may hold on to a layout while it shows
	// the tiles, so the grid is refilled, never replaced.
	m_grid = new QGridLayout(this);
	m_grid->setContentsMargins(0, 0, 0, 0);
	m_grid->setSpacing(kSpacing);
}

void TileGrid::addTile(QAbstractButton* tile)
{
	if (!tile) {
		return;
	}
	tile->setParent(this);
	// Tiles take the column's width, however long their labels.
	tile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	tile->installEventFilter(this);
	m_tiles << tile;
	arrange(columnsFor(width()));
}

int TileGrid::columnCount() const
{
	return m_columns;
}

QVector<QAbstractButton*> TileGrid::shownTiles() const
{
	QVector<QAbstractButton*> shown;
	for (QAbstractButton* tile : m_tiles) {
		// A tile not shown yet still counts; one hidden on purpose does not.
		if (!tile->isHidden() || !tile->testAttribute(Qt::WA_WState_ExplicitShowHide)) {
			shown << tile;
		}
	}
	return shown;
}

int TileGrid::widestTile() const
{
	int widest = 0;
	for (const QAbstractButton* tile : shownTiles()) {
		widest = std::max(widest, std::max(tile->sizeHint().width(), tile->minimumWidth()));
	}
	return widest;
}

int TileGrid::columnsFor(int width) const
{
	const int count = int(shownTiles().size());
	const int widest = widestTile();
	if (count == 0 || widest <= 0) {
		return m_maximumColumns;
	}
	const int fit = (width + kSpacing) / (widest + kSpacing);
	return std::clamp(fit, 1, std::min(m_maximumColumns, count));
}

void TileGrid::arrange(int columns)
{
	m_columns = std::max(1, columns);
	while (QLayoutItem* item = m_grid->takeAt(0)) {
		delete item;
	}
	const QVector<QAbstractButton*> shown = shownTiles();
	for (int index = 0; index < shown.size(); ++index) {
		m_grid->addWidget(shown.at(index), index / m_columns, index % m_columns);
	}
	for (int column = 0; column < m_maximumColumns; ++column) {
		m_grid->setColumnStretch(column, column < m_columns ? 1 : 0);
	}
	updateGeometry();
}

void TileGrid::scheduleArrange()
{
	if (m_arrangePending) {
		return;
	}
	m_arrangePending = true;
	QMetaObject::invokeMethod(
		this,
		[this]() {
			m_arrangePending = false;
			arrange(columnsFor(width()));
		},
		Qt::QueuedConnection);
}

bool TileGrid::eventFilter(QObject* watched, QEvent* event)
{
	// A tile hidden or shown on purpose changes which cells are filled.
	if (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent) {
		scheduleArrange();
	}
	return QWidget::eventFilter(watched, event);
}

void TileGrid::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (columnsFor(event->size().width()) != m_columns) {
		scheduleArrange();
	}
}

void TileGrid::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	// New text sizes or another style change how wide the widest label is.
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange) {
		scheduleArrange();
	}
}

void TileGrid::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	if (columnsFor(width()) != m_columns) {
		scheduleArrange();
	}
}

} // namespace vibestudio
