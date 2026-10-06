#include "app/property_grid.h"

#include <QEvent>
#include <QHeaderView>
#include <QScopedValueRollback>
#include <QTimer>

namespace vibestudio {

PropertyGrid::PropertyGrid(QWidget* parent) : QTreeWidget(parent)
{
	setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	header()->installEventFilter(this);
	connect(header(), &QHeaderView::sectionResized, this, [this](int column, int, int size) {
		if (column == 0 && m_fontHeight && !m_updatingMetrics) { m_preferredSectionEm = double(size) / m_fontHeight; }
	});
	connect(model(), &QAbstractItemModel::headerDataChanged, this, [this] { scheduleHeaderMetrics(); });
	scheduleHeaderMetrics();
}

bool PropertyGrid::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == header() && (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange
		|| event->type() == QEvent::LanguageChange || event->type() == QEvent::Show)) { scheduleHeaderMetrics(); }
	return QTreeWidget::eventFilter(watched, event);
}

void PropertyGrid::scheduleHeaderMetrics()
{
	if (m_metricsQueued) { return; }
	m_metricsQueued = true;
	QTimer::singleShot(0, this, [this] { m_metricsQueued = false; updateHeaderMetrics(); });
}

void PropertyGrid::updateHeaderMetrics()
{
	if (columnCount() < 2) { return; }
	const int fontHeight = qMax(1, header()->fontMetrics().height());
	if (!m_fontHeight) { m_preferredSectionEm = double(fontMetrics().averageCharWidth() * 18) / fontHeight; }
	const int preferred = qRound(m_preferredSectionEm * fontHeight);
	QScopedValueRollback<bool> updating(m_updatingMetrics, true);
	// sectionSizeHint includes the native/style-sheet padding. Reset the old
	// floor before measuring so a 200% -> 100% transition can shrink again.
	header()->setMinimumSectionSize(-1);
	int minimum = 0;
	for (int column = 0; column < columnCount(); ++column) { minimum = qMax(minimum, header()->sectionSizeHint(column)); }
	header()->setMinimumSectionSize(minimum);
	// Stretch-last retains an earlier explicit size. Refresh that floor as well
	// so a narrow viewport cannot keep an obsolete, undersized last heading.
	if (header()->stretchLastSection()) { header()->resizeSection(columnCount() - 1, minimum); }
	setColumnWidth(0, qMax(minimum, preferred));
	m_fontHeight = fontHeight;
}

} // namespace vibestudio
