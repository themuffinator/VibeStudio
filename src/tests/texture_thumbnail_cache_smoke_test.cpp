#include "app/texture_thumbnail_cache.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QListWidget>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen"); QApplication app(argc, argv); bool ok = true;
	QPixmap pixels(16, 16); pixels.fill(Qt::red); const QIcon icon(pixels);
	TextureThumbnailCache cache(3072, 3);
	cache.insert(QStringLiteral("a"), icon, 1024); cache.insert(QStringLiteral("b"), icon, 1024); cache.insert(QStringLiteral("c"), icon, 1024);
	ok &= expect(!cache.value(QStringLiteral("a")).isNull(), "recent thumbnail remains available");
	ok &= expect(cache.insert(QStringLiteral("d"), icon, 1024) == QStringList{QStringLiteral("b")} && !cache.contains(QStringLiteral("b")) &&
		cache.retainedBytes() == 3072 && cache.size() == 3, "eviction reports the least recently used view icon as well as releasing its cache entry");
	cache.insert(QStringLiteral("a"), icon, 256);
	ok &= expect(cache.retainedBytes() == 2304 && cache.size() == 3, "replacing an entry updates its accounted storage");
	ok &= expect(cache.insert(QStringLiteral("a"), icon, 4096) == QStringList{QStringLiteral("a")} && !cache.contains(QStringLiteral("a")) &&
		cache.retainedBytes() == 2048, "oversized replacement is rejected and instructs the view to clear its previous pixels");
	cache.clear(); ok &= expect(cache.size() == 0 && cache.retainedBytes() == 0, "source invalidation releases all retained source pixels");
	TextureThumbnailCache large;
	for (int i = 0; i < 10000; ++i) {
		large.insert(QString::number(i), icon, 256 * 256 * 4);
		ok &= expect(large.size() <= TextureThumbnailCache::MaximumEntries && large.retainedBytes() <= TextureThumbnailCache::MaximumBytes,
			"visiting ten thousand textures cannot grow the source pixel cache beyond its budget");
	}
	ok &= expect(large.size() == 256 && large.retainedBytes() == TextureThumbnailCache::MaximumBytes && !large.contains(QStringLiteral("0")),
		"maximum-resolution thumbnail retention obeys the exact byte ceiling");
	TextureThumbnailCache tiny(1024, 3);
	for (int i = 0; i < 100; ++i) { tiny.insert(QString::number(i), icon, 1); }
	ok &= expect(tiny.size() == 3, "tiny/error icons are also bounded by entry count");

	QListWidget list; list.resize(360, 480); list.setUniformItemSizes(true);
	for (int i = 0; i < 10000; ++i) {
		auto* item = new QListWidgetItem(QStringLiteral("Texture %1").arg(i), &list); item->setData(Qt::UserRole, QString::number(i));
	}
	list.show(); app.processEvents();
	QElapsedTimer clock; clock.start();
	const auto first = visibleTextureThumbnailPaths(&list, Qt::UserRole);
	ok &= expect(!first.isEmpty() && first.size() <= 128 && first.first() == QStringLiteral("0") && !first.contains(QStringLiteral("9999")),
		"a large browser requests only visible rows");
	const auto topMs = clock.elapsed();
	list.scrollToItem(list.item(9999)); app.processEvents(); clock.restart();
	const auto last = visibleTextureThumbnailPaths(&list, Qt::UserRole);
	ok &= expect(!last.isEmpty() && last.size() <= 128 && last.contains(QStringLiteral("9999")) && !last.contains(QStringLiteral("0")),
		"scrolling replaces demand with the newly visible rows");
	const auto bottomMs = clock.elapsed();
	for (int i = 0; i < 9998; ++i) { list.item(i)->setHidden(true); }
	list.setViewMode(QListView::IconMode); list.setGridSize(QSize(100, 70)); list.setLayoutDirection(Qt::RightToLeft); app.processEvents();
	const auto filtered = visibleTextureThumbnailPaths(&list, Qt::UserRole);
	ok &= expect(filtered.size() == 2 && filtered.contains(QStringLiteral("9998")) && filtered.contains(QStringLiteral("9999")),
		"filtered RTL tiles request the visible paths, independent of row numbers");
	list.hide(); ok &= expect(visibleTextureThumbnailPaths(&list, Qt::UserRole).isEmpty(), "hidden tabs do not decode package thumbnails");
	std::cout << "10,000-row demand scan: top " << topMs << " ms, bottom " << bottomMs << " ms; retained source bytes " << large.retainedBytes() << '\n';
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
