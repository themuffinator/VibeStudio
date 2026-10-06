#include "app/texture_thumbnail_cache.h"

#include <QListWidget>
#include <QSet>
#include <algorithm>

namespace vibestudio {

TextureThumbnailCache::TextureThumbnailCache(qint64 maximumBytes, int maximumEntries)
	: m_limit(std::clamp<qint64>(maximumBytes, 1, MaximumBytes)), m_entryLimit(std::clamp(maximumEntries, 1, MaximumEntries)) {}

bool TextureThumbnailCache::contains(const QString& path) const { return m_entries.contains(path); }
QIcon TextureThumbnailCache::value(const QString& path)
{
	const auto found = m_entries.find(path);
	if (found == m_entries.end()) { return {}; }
	found->used = ++m_clock; return found->icon;
}

QStringList TextureThumbnailCache::insert(const QString& path, const QIcon& icon, qint64 bytes)
{
	QStringList evicted;
	const auto existing = m_entries.find(path);
	if (existing != m_entries.end()) { m_bytes -= existing->bytes; m_entries.erase(existing); }
	if (bytes < 1 || bytes > m_limit || icon.isNull()) { return {path}; }
	while (!m_entries.isEmpty() && (m_bytes > m_limit - bytes || m_entries.size() >= m_entryLimit)) {
		auto oldest = m_entries.begin();
		for (auto it = m_entries.begin(); it != m_entries.end(); ++it) { if (it->used < oldest->used) { oldest = it; } }
		evicted << oldest.key(); m_bytes -= oldest->bytes; m_entries.erase(oldest);
	}
	m_entries.insert(path, {icon, bytes, ++m_clock}); m_bytes += bytes; return evicted;
}

void TextureThumbnailCache::clear() { m_entries.clear(); m_bytes = 0; m_clock = 0; }

QStringList visibleTextureThumbnailPaths(QListWidget* list, int pathRole)
{
	QStringList paths;
	if (!list || !list->isVisible()) { return paths; }
	QSet<QString> seen;
	const QRect viewport = list->viewport()->rect();
	QVector<int> rows; rows.reserve(list->count());
	for (int row = 0; row < list->count(); ++row) { if (!list->item(row)->isHidden()) { rows << row; } }
	// These are the studio's tile/list layouts. Looking up every visual rect
	// costs much more than reading hidden flags in a large filtered package.
	const bool ordered = list->uniformItemSizes() && ((list->flow() == QListView::LeftToRight && list->isWrapping()) ||
		(list->flow() == QListView::TopToBottom && !list->isWrapping()));
	qsizetype first = 0;
	if (ordered) {
		qsizetype end = rows.size();
		while (first < end) {
			const auto middle = first + (end - first) / 2;
			if (list->visualItemRect(list->item(rows[middle])).bottom() < viewport.top()) { first = middle + 1; }
			else { end = middle; }
		}
	}
	for (qsizetype index = first; index < rows.size() && paths.size() < 128; ++index) {
		const auto* item = list->item(rows[index]);
		const auto rect = list->visualItemRect(item);
		if (ordered && rect.top() > viewport.bottom()) { break; }
		if (!rect.intersects(viewport)) { continue; }
		const auto path = item->data(pathRole).toString();
		if (!path.isEmpty() && !seen.contains(path)) { seen.insert(path); paths << path; }
	}
	return paths;
}

} // namespace vibestudio
