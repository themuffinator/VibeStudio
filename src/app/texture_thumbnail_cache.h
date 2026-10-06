#pragma once

#include <QHash>
#include <QIcon>
#include <QStringList>

class QListWidget;

namespace vibestudio {

// Retained source pixmaps are bounded independently of package entry count.
// Callers must remove evicted icons from views too: QIcon copies share pixels.
class TextureThumbnailCache {
public:
	static constexpr qint64 MaximumBytes = 64 * 1024 * 1024;
	static constexpr int MaximumEntries = 512;
	explicit TextureThumbnailCache(qint64 maximumBytes = MaximumBytes, int maximumEntries = MaximumEntries);
	[[nodiscard]] bool contains(const QString& path) const;
	[[nodiscard]] QIcon value(const QString& path);
	[[nodiscard]] qsizetype size() const { return m_entries.size(); }
	[[nodiscard]] qint64 retainedBytes() const { return m_bytes; }
	QStringList insert(const QString& path, const QIcon& icon, qint64 bytes);
	void clear();
private:
	struct Entry { QIcon icon; qint64 bytes = 0; quint64 used = 0; };
	QHash<QString, Entry> m_entries;
	qint64 m_limit = MaximumBytes, m_bytes = 0;
	int m_entryLimit = MaximumEntries;
	quint64 m_clock = 0;
};

// Demand comes from the visible viewport, in row order. A hidden tab does not
// decode a whole package. Each of the two browser views requests at most 128
// thumbnails, so 256px RGBA tiles fit together in the shared 64 MiB budget.
QStringList visibleTextureThumbnailPaths(QListWidget* list, int pathRole);

} // namespace vibestudio
