#include "core/package_browser.h"
#include "package_browser_test_fixture.h"

#include <QCoreApplication>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message) { if (!condition) { std::cerr << message << '\n'; } return condition; }
}

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	QString error; bool ok = true;
	const auto source = tests::browserFixture(4000); PackageArchive archive;
	if (!expect(archive.loadSnapshot(source, &error), "admit browser fixture")) { return 1; }
	const auto prepared = preparePackageBrowserIndex(archive, &error);
	if (!expect(prepared && source->payloadReads == 0, "browser metadata preparation reads no payloads")) { return 1; }
	const auto bulk = prepared->folderLookup.value(QStringLiteral("bulk"), -1);
	const auto empty = prepared->folderLookup.value(QStringLiteral("empty"), -1);
	ok &= expect(prepared->folders.size() == 3 && bulk > 0 && empty > 0 && prepared->folders.at(bulk).row == 0
		&& prepared->folders.at(empty).row == 1 && prepared->folders.at(bulk).parent == 0 && prepared->folders.at(bulk).entry == 4004,
		"folder index preserves explicit empty directories, stable sibling order and source entry identity");
	auto mixed = std::make_shared<tests::BrowserFixture>();
	const auto add = [&](const QString& path, quint64 bytes, const QString& hint = {}) {
		PackageEntry entry; entry.virtualPath = path; entry.sizeBytes = bytes; entry.typeHint = hint; mixed->rows.append(entry);
	};
	add(QStringLiteral("z/deep/texture.png"), 10); add(QStringLiteral("Alpha/readme.txt"), 20);
	add(QStringLiteral("music.wav"), 30); add(QStringLiteral("mesh.md3"), 40); add(QStringLiteral("world.bsp"), 50);
	add(QStringLiteral("nested.pk3"), 60); add(QStringLiteral("unknown.bin"), 70);
	PackageEntry directory; directory.virtualPath = QStringLiteral("z/deep"); directory.kind = PackageEntryKind::Directory;
	directory.sizeBytes = 999; directory.note = QStringLiteral("Folder diagnostic"); mixed->rows.append(directory);
	directory.virtualPath = QStringLiteral("empty"); mixed->rows.append(directory);
	directory.virtualPath = QStringLiteral("z/deep"); directory.note.clear(); mixed->rows.append(directory);
	PackageArchive mixedArchive;
	if (!expect(mixedArchive.loadSnapshot(mixed, &error), "admit mixed composition fixture")) { return 1; }
	const auto index = preparePackageBrowserIndex(mixedArchive, &error);
	if (!expect(index && index->composition.size() == 8 && index->summary.totalSizeBytes == 280
		&& index->summary.fileCount == 7 && index->summary.directoryCount == 3 && index->summary.entryCount == 10
		&& index->summary.sourcePath == mixed->sourcePath() && index->summary.format == mixed->format() && mixed->payloadReads == 0,
		"cached composition classifies metadata across all categories and excludes directory bytes")) { return 1; }
	const QStringList categories = {QStringLiteral("binary"), QStringLiteral("archive"), QStringLiteral("map"), QStringLiteral("model"),
		QStringLiteral("audio"), QStringLiteral("text"), QStringLiteral("image"), QStringLiteral("directory")};
	bool categoriesMatch = true;
	for (qsizetype at = 0; at < categories.size(); ++at) {
		const auto& bucket = index->composition.at(at);
		categoriesMatch &= bucket.id == categories.at(at) && bucket.bytes == static_cast<quint64>((7 - at) * 10)
			&& bucket.count == (at == 7 ? 3 : 1) && !bucket.sizeOverflow;
	}
	ok &= expect(categoriesMatch, "composition sorting, sizes and entry counts are exact");
	const auto deep = index->folderLookup.value(QStringLiteral("z/deep"), -1);
	ok &= expect(index->folders.size() == 5 && deep > 0 && index->folders.at(deep).entry == 7
		&& index->folders.at(index->folders.at(deep).parent).path == QStringLiteral("z")
		&& index->folders.at(index->folders.at(0).children.first()).path == QStringLiteral("Alpha"),
		"implied parents share a sorted hierarchy and repeated directories preserve the first diagnostic");
	mixed->rows.clear(); add(QStringLiteral("huge.bin"), std::numeric_limits<quint64>::max()); add(QStringLiteral("overflow.txt"), 1);
	PackageArchive overflowing;
	ok &= expect(overflowing.loadSnapshot(mixed, &error), "admit oversized aggregate metadata for inspection");
	const auto overflowingIndex = preparePackageBrowserIndex(overflowing, &error);
	ok &= expect(overflowingIndex && error.isEmpty() && overflowingIndex->summary.totalSizeOverflow
		&& overflowingIndex->summary.totalSizeBytes == std::numeric_limits<quint64>::max()
		&& overflowingIndex->entries.size() == 2 && mixed->payloadReads == 0,
		"aggregate composition overflow remains browsable with a checked total and no payload reads");
	QVector<qsizetype> rows;
	if (overflowingIndex) {
		ok &= expect(overflowingIndex->composition.size() == 2 && !overflowingIndex->composition.first().sizeOverflow
			&& !overflowingIndex->composition.last().sizeOverflow && overflowingIndex->composition.first().bytes == std::numeric_limits<quint64>::max(),
			"aggregate overflow does not hide individually exact category totals");
		ok &= expect(filterPackageBrowser(*overflowingIndex, QString(), QStringLiteral("size>1"), &rows, &error)
			&& rows == QVector<qsizetype>{0} && overflowingIndex->entries.at(rows.first()).virtualPath == QStringLiteral("huge.bin"),
			"oversized entries retain exact row identity and positive query sizes for repair");
	}
	mixed->rows.last().sizeBytes = 0; directory.sizeBytes = std::numeric_limits<quint64>::max(); mixed->rows.append(directory);
	PackageArchive exact;
	ok &= expect(exact.loadSnapshot(mixed, &error), "admit exact maximum aggregate metadata");
	const auto exactIndex = preparePackageBrowserIndex(exact, &error);
	ok &= expect(exactIndex && !exactIndex->summary.totalSizeOverflow && exactIndex->summary.totalSizeBytes == std::numeric_limits<quint64>::max()
		&& exactIndex->summary.directoryCount == 1 && exactIndex->summary.fileCount == 2,
		"exact maximum plus zero remains exact and directory metadata is excluded");
	mixed->rows.clear(); add(QStringLiteral("huge.bin"), std::numeric_limits<quint64>::max()); add(QStringLiteral("overflow.bin"), 1);
	PackageArchive sameCategory;
	ok &= expect(sameCategory.loadSnapshot(mixed, &error), "admit overflowing category metadata");
	const auto sameCategoryIndex = preparePackageBrowserIndex(sameCategory, &error);
	ok &= expect(sameCategoryIndex && sameCategoryIndex->summary.totalSizeOverflow && sameCategoryIndex->composition.size() == 1
		&& sameCategoryIndex->composition.first().sizeOverflow && sameCategoryIndex->composition.first().bytes == std::numeric_limits<quint64>::max()
		&& overflowingIndex && overflowingIndex->composition.size() == 2 && exactIndex && !exactIndex->summary.totalSizeOverflow && mixed->payloadReads == 0,
		"category overflow is checked independently and prepared snapshots retain their metadata without payload reads");
	ok &= expect(filterPackageBrowser(*prepared, QString(), QString(), &rows, &error) && rows.size() == 5,
		"root listing contains direct folders, repeated names and unreadable files");
	const auto first = prepared->entries.at(rows.first());
	ok &= expect(first.kind == PackageEntryKind::Directory && first.virtualPath == QStringLiteral("bulk") && prepared->childCounts.at(rows.first()) == 4000,
		"directories sort first and carry exact direct-child counts");
	ok &= expect(filterPackageBrowser(*prepared, QStringLiteral("bulk"), QString(), &rows, &error) && rows.size() == 4000
		&& prepared->entries.at(rows.first()).virtualPath == QStringLiteral("bulk/000000.txt")
		&& prepared->entries.at(rows.last()).virtualPath == QStringLiteral("bulk/003999.txt"), "a complete sorted folder is exposed without truncation");
	ok &= expect(filterPackageBrowser(*prepared, QStringLiteral("empty"), QStringLiteral("ext=txt size>2 folder:bulk"), &rows, &error) && rows.size() == 3903,
		"property queries search the whole snapshot with shared numeric and folder semantics");
	ok &= expect(filterPackageBrowser(*prepared, QString(), QStringLiteral("things"), &rows, &error) && rows.size() == 2
		&& prepared->occurrences.at(rows.first()) == 2 && prepared->entries.at(rows.first()).sourceOrdinal == 1 && prepared->entries.at(rows.last()).sourceOrdinal == 2,
		"duplicate names retain stable exact source identities");
	const auto retained = rows;
	bool cancelled = false; PackageReadControl control;
	control.isCancelled = [&] { return cancelled; };
	control.progress = [&](const QString&, qint64 count, qint64) { if (count >= 256) { cancelled = true; } };
	ok &= expect(!filterPackageBrowser(*prepared, QStringLiteral("bulk"), QString(), &rows, &error, control) && cancelled && rows == retained,
		"mid-filter cancellation publishes no partial rows");
	cancelled = false;
	ok &= expect(!preparePackageBrowserIndex(archive, &error, control) && cancelled && source->payloadReads == 0,
		"index cancellation discards its candidate without reading payloads");
	ok &= expect(filterPackageBrowser(*prepared, QString(), QStringLiteral("unsupported"), &rows, &error) && rows.size() == 1
		&& !prepared->entries.at(rows.first()).readable, "unreadable members remain visible for diagnosis");
	return ok ? 0 : 1;
}
