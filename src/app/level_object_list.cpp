#include "app/level_object_list.h"
#include "app/studio_layout.h"
#include <QEvent>
#include <QItemSelectionModel>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>

namespace vibestudio {
struct LevelObjectList::Work {
	std::atomic_bool cancelled{false};
	LevelObjectRows source;
	QString filter;
	quint64 generation = 0, sourceGeneration = 0;
	QBitArray hidden;
	QHash<QString, LevelMapObjectProperties> properties;
	QSet<QString> knownKeys;
	bool propertiesReady = false;
};

LevelObjectList::LevelObjectList(QWidget* parent) : QListView(parent), m_objects(new LevelObjectModel(this))
{
	setModel(m_objects);
	setSelectionMode(QAbstractItemView::ExtendedSelection);
	setUniformItemSizes(true);
	setWordWrap(false);
	useElidingRows(this);
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { Q_EMIT selectionEdited(); });
	m_debounce = new QTimer(this);
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(80);
	connect(m_debounce, &QTimer::timeout, this, &LevelObjectList::startFilter);
}

LevelObjectList::~LevelObjectList()
{
	if (m_work) { m_work->cancelled = true; }
}

void LevelObjectList::setDocument(const LevelMapDocument& document)
{
	const QSignalBlocker blocker(this);
	++m_sourceGeneration;
	m_properties.clear();
	m_propertiesReady = false;
	m_knownKeys.clear();
	m_objects->setDocument(document);
	requestFilter();
}

void LevelObjectList::refreshHidden(const std::function<bool(LevelMapSelectionRef)>& predicate)
{
	if (m_objects->refreshHidden(predicate) && !m_filter.trimmed().isEmpty()) { requestFilter(); }
}

void LevelObjectList::setFilterText(const QString& text)
{
	if (text == m_filter) { return; }
	m_filter = text;
	requestFilter();
}

void LevelObjectList::setFiltering(bool filtering)
{
	m_filtering = filtering;
	setEnabled(!filtering);
	Q_EMIT filterStateChanged();
}

void LevelObjectList::requestFilter()
{
	++m_generation;
	if (m_work) { m_work->cancelled = true; }
	m_debounce->stop();
	if (m_filter.trimmed().isEmpty() || !m_objects->objectCount()) {
		applyFilter(QBitArray(m_objects->objectCount()));
		setFiltering(false);
		Q_EMIT filterFinished();
		return;
	}
	setFiltering(true);
	m_debounce->start();
}

void LevelObjectList::startFilter()
{
	if (m_thread || !m_filtering) { return; }
	auto work = std::make_shared<Work>();
	work->source = m_objects->snapshot();
	work->filter = m_filter;
	work->generation = m_generation;
	work->sourceGeneration = m_sourceGeneration;
	work->properties = m_properties;
	work->propertiesReady = m_propertiesReady;
	m_work = work;
	m_thread = QThread::create([work] {
		const auto cancelled = [work] { return work->cancelled.load(); };
		const auto query = parseLevelMapQuery(work->filter);
		if (query.testsProperties() && !work->propertiesReady) {
			work->properties = levelMapQueryProperties(work->source.document, cancelled);
			work->propertiesReady = !cancelled();
		}
		if (cancelled()) { return; }
		const auto needle = work->filter.trimmed().toCaseFolded();
		work->hidden.resize(work->source.rows.size());
		for (int i = 0; i < work->source.rows.size(); ++i) {
			if (cancelled()) { return; }
			const auto selector = work->source.selector(i);
			const auto description = work->source.description(i);
			const bool kept = query.testsProperties()
				? levelMapQueryMatches(query, work->properties.value(selector), description + QLatin1Char(' ') + selector)
				: description.toCaseFolded().contains(needle) || selector.toCaseFolded().contains(needle);
			work->hidden.setBit(i, !kept);
		}
		if (query.testsProperties()) {
			for (const auto& properties : std::as_const(work->properties)) {
				if (cancelled()) { return; }
				for (auto it = properties.cbegin(); it != properties.cend(); ++it) { work->knownKeys.insert(it.key()); }
			}
		}
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread = nullptr;
		if (m_work == work) { m_work.reset(); }
		if (work->generation != m_generation || work->cancelled) {
			if (!m_debounce->isActive()) { startFilter(); }
			return;
		}
		if (work->sourceGeneration == m_sourceGeneration && work->propertiesReady) {
			m_properties = std::move(work->properties);
			m_propertiesReady = true;
		}
		m_knownKeys = std::move(work->knownKeys);
		applyFilter(work->hidden);
		setFiltering(false);
		Q_EMIT filterFinished();
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}

void LevelObjectList::applyFilter(const QBitArray& hidden)
{
	const QSignalBlocker blocker(this);
	m_matchingCount = m_objects->objectCount();
	for (int row = 0; row < m_objects->rowCount(); ++row) {
		const bool hide = row < hidden.size() && hidden.testBit(row);
		if (isRowHidden(row) != hide) { setRowHidden(row, hide); }
		if (hide) { --m_matchingCount; }
	}
}

int LevelObjectList::rowForSelector(const QString& selector) const
{
	const int colon = static_cast<int>(selector.indexOf(QLatin1Char(':')));
	bool valid = false;
	const int id = selector.mid(colon + 1).toInt(&valid);
	return colon > 0 && valid ? m_objects->rowForReference({levelMapSelectionKindFromId(selector.left(colon)), id}) : -1;
}

QVector<LevelMapSelectionRef> LevelObjectList::selectedReferences() const
{
	QVector<LevelMapSelectionRef> result;
	const auto current = currentIndex();
	for (const auto& index : selectionModel()->selectedRows()) {
		if (index == current) { continue; }
		const auto ref = m_objects->reference(index.row());
		if (ref.kind != LevelMapSelectionKind::None) { result.append(ref); }
	}
	if (current.isValid() && selectionModel()->isSelected(current)) {
		const auto ref = m_objects->reference(current.row());
		if (ref.kind != LevelMapSelectionKind::None) { result.append(ref); }
	}
	return result;
}

void LevelObjectList::selectRows(const QVector<int>& rows, int primary)
{
	const QSignalBlocker blocker(this);
	QItemSelection selection;
	int start = -1, end = -1;
	for (const int row : rows) {
		if (row < 0 || row >= m_objects->objectCount()) { continue; }
		if (start >= 0 && row == end + 1) { end = row; continue; }
		if (start >= 0) { selection.select(model()->index(start, 0), model()->index(end, 0)); }
		start = end = row;
	}
	if (start >= 0) { selection.select(model()->index(start, 0), model()->index(end, 0)); }
	selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
	selectionModel()->setCurrentIndex(model()->index(primary, 0), QItemSelectionModel::NoUpdate);
}

void LevelObjectList::setSelectedReferences(const QVector<LevelMapSelectionRef>& selected, LevelMapSelectionRef primary)
{
	QVector<int> rows;
	rows.reserve(selected.size() + 1);
	for (const auto ref : selected) {
		const int row = m_objects->rowForReference(ref);
		if (row >= 0) { rows.append(row); }
	}
	const int primaryRow = m_objects->rowForReference(primary);
	if (primaryRow >= 0) { rows.append(primaryRow); }
	std::sort(rows.begin(), rows.end());
	rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
	selectRows(rows, primaryRow);
}

void LevelObjectList::changeEvent(QEvent* event)
{
	QListView::changeEvent(event);
	if (event->type() == QEvent::LanguageChange) { requestFilter(); }
	if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LanguageChange) {
		doItemsLayout();
		viewport()->update();
	}
}
} // namespace vibestudio
