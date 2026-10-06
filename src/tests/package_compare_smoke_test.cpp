#include "core/deflate.h"
#include "core/package_archive.h"
#include "core/package_compare.h"
#include "core/package_import_store.h"
#include "core/package_staging.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <QStringList>
#include <QTemporaryDir>
#include <QThreadPool>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeFile(const QString& path, const QByteArray& data)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return file.write(data) == data.size();
}

void appendLe16(QByteArray* data, quint16 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* data, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8) {
		data->append(static_cast<char>((value >> shift) & 0xff));
	}
}

struct ZipInput {
	QString name;
	QByteArray data;
};

// Minimal stored-only ZIP fixture writer (PKWARE APPNOTE.TXT sections 4.3.7,
// 4.3.12 and 4.3.16). Comparison must read and verify the payload even when
// two central directories carry matching CRC-32 values.
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
bool buildStoredZip(const QString& path, const QVector<ZipInput>& inputs)
{
	QByteArray data;
	QByteArray central;
	for (const ZipInput& input : inputs) {
		const QByteArray name = input.name.toUtf8();
		const quint32 localOffset = static_cast<quint32>(data.size());
		const quint32 size = static_cast<quint32>(input.data.size());
		const quint32 crc = crc32Bytes(input.data);
		const quint16 fixedDate = static_cast<quint16>((1 << 5) | 1);

		appendLe32(&data, 0x04034b50);
		appendLe16(&data, 20);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, fixedDate);
		appendLe32(&data, crc);
		appendLe32(&data, size);
		appendLe32(&data, size);
		appendLe16(&data, static_cast<quint16>(name.size()));
		appendLe16(&data, 0);
		data.append(name);
		data.append(input.data);

		appendLe32(&central, 0x02014b50);
		appendLe16(&central, 20);
		appendLe16(&central, 20);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, fixedDate);
		appendLe32(&central, crc);
		appendLe32(&central, size);
		appendLe32(&central, size);
		appendLe16(&central, static_cast<quint16>(name.size()));
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe32(&central, 0);
		appendLe32(&central, localOffset);
		central.append(name);
	}

	const quint32 centralOffset = static_cast<quint32>(data.size());
	data.append(central);
	appendLe32(&data, 0x06054b50);
	appendLe16(&data, 0);
	appendLe16(&data, 0);
	appendLe16(&data, static_cast<quint16>(inputs.size()));
	appendLe16(&data, static_cast<quint16>(inputs.size()));
	appendLe32(&data, static_cast<quint32>(central.size()));
	appendLe32(&data, centralOffset);
	appendLe16(&data, 0);
	return writeFile(path, data);
}

const PackageCompareEntry* findByKey(const PackageCompareResult& result, const QString& key)
{
	for (const PackageCompareEntry& entry : result.entries) {
		if (entry.key == key) {
			return &entry;
		}
	}
	return nullptr;
}

bool hasStatus(const PackageCompareResult& result, const QString& key, PackageCompareStatus status)
{
	const PackageCompareEntry* entry = findByKey(result, key);
	return entry && entry->status == status;
}

QStringList entryKeys(const PackageCompareResult& result)
{
	QStringList keys;
	for (const PackageCompareEntry& entry : result.entries) {
		keys.push_back(QStringLiteral("%1#%2").arg(entry.key).arg(entry.occurrence));
	}
	return keys;
}

class StreamOnlyReader final : public PackageArchiveReader {
public:
	qint64 declared = 200003;
	qint64 actual = declared;
	bool supported = true;
	mutable int bufferedReads = 0;
	mutable int streamedReads = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Pak; }
	QString sourcePath() const override { return QStringLiteral("synthetic-stream"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override {
		PackageEntry directory; directory.virtualPath = QStringLiteral("synthetic");
		directory.kind = PackageEntryKind::Directory; directory.storageMethod = QStringLiteral("synthetic");
		PackageEntry file; file.virtualPath = QStringLiteral("PAYLOAD"); file.kind = PackageEntryKind::File;
		file.readable = true; file.sizeBytes = static_cast<quint64>(declared);
		return {directory, file};
	}
	bool readEntryBytes(const QString&, QByteArray*, QString* error, qint64 = -1) const override {
		++bufferedReads; if (error) *error = QStringLiteral("Unexpected whole-entry read"); return false;
	}
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
		const std::function<bool()>& isCancelled = {}) const override {
		++streamedReads;
		if (!supported) return PackageArchiveReader::streamEntryAt(index, sink, error, isCancelled);
		if (index != 1) { if (error) *error = QStringLiteral("Wrong positional index"); return false; }
		const QByteArray block(65536, 'a');
		for (qint64 pos = 0; pos < actual; pos += block.size()) {
			if ((isCancelled && isCancelled()) || !sink(QByteArrayView(block).first(qMin<qint64>(block.size(), actual - pos)))) return false;
		}
		return true;
	}
};

bool streamingComparisonSmoke(const QDir& root)
{
	bool ok = true;
	StreamOnlyReader first, second;
	ok &= expect(comparePackages(first, second).identical() && first.bufferedReads == 0 && second.bufferedReads == 0
		&& first.streamedReads == 1 && second.streamedReads == 1, "all reader types must use the positional streaming contract");
	for (const int difference : {-1, 1}) {
		second.actual = second.declared + difference;
		const auto result = comparePackages(first, second);
		ok &= expect(!result.identical() && result.summary.uncomparedCount == 1 && !result.warnings.isEmpty()
			&& result.entries.first().rightHash.isEmpty(), "short or excessive streamed data must never acquire a valid digest");
	}
	second.supported = false;
	const auto unsupported = comparePackages(first, second);
	ok &= expect(!unsupported.identical() && unsupported.summary.uncomparedCount == 1 && second.bufferedReads == 0,
		"unsupported streaming must remain explicitly unchecked without buffering fallback");
	QString error;
	const QByteArray payload(2 * 1024 * 1024 + 7, 'a');
	const QString folder = root.filePath(QStringLiteral("stream-comparison"));
	ok &= expect(QDir().mkpath(folder) && writeFile(QDir(folder).filePath(QStringLiteral("AAA")), QByteArray("x"))
		&& writeFile(QDir(folder).filePath(QStringLiteral("PAYLOAD")), payload), "prepare comparison source");
	PackageArchive source;
	PackageStagingModel plan;
	ok &= expect(source.load(folder, &error) && plan.createEmpty(PackageArchiveFormat::Pak, {}, &error)
		&& plan.addBytes(QByteArray("x"), QStringLiteral("AAA"), &error)
		&& plan.addBytes(payload, QStringLiteral("PAYLOAD"), &error), "prepare generated planned comparison");
	PackageStagingArchive planned(plan);
	for (const bool throughReader : {false, true}) {
		PackageCompareRequest request;
		quint64 previous = 0;
		int partialNotifications = 0;
		request.byteProgress = [&](PackageCompareSource, const QString& path, quint64 done, quint64 total) {
			if (done == 0) previous = 0;
			ok &= expect(done >= previous && done - previous <= 65536 && done <= total, "progress must reflect bounded verified input chunks");
			previous = done;
			if (path == QStringLiteral("PAYLOAD") && done > 0 && done < total) ++partialNotifications;
		};
		const auto compare = [&]() { return throughReader ? comparePackages(source, planned, request) : comparePackageToPlan(source, plan, request); };
		ok &= expect(compare().identical() && partialNotifications > 2, "generated plans and staged readers must compare in bounded chunks");
		request.maxEntryBytes = 1024;
		partialNotifications = 0;
		ok &= expect(compare().summary.uncomparedCount == 1 && partialNotifications == 0, "the I/O budget must skip an oversized entry before reading it");
		request.maxEntryBytes = kPackageCompareDefaultMaxEntryBytes;
		for (const auto side : {PackageCompareSource::Left, PackageCompareSource::Right}) {
			bool cancel = false;
			quint64 last = 0;
			request.isCancelled = [&]() { return cancel; };
			request.byteProgress = [&](PackageCompareSource reading, const QString& path, quint64 done, quint64 total) {
				if (reading == side && path == QStringLiteral("PAYLOAD") && done > 0 && done < total) { last = done; cancel = true; }
			};
			const auto stopped = compare();
			ok &= expect(stopped.cancelled && !stopped.completed && !stopped.identical() && last == 65536
				&& stopped.entries.size() == 1 && stopped.summary.identicalCount == 1
				&& stopped.summary.leftBytes == 1 && stopped.summary.rightBytes == 1 && stopped.warnings.isEmpty(),
				"cancellation on either side must retain only finished rows and accurate partial counters");
		}
	}
	return ok;
}

} // namespace

int main()
{
	bool ok = true;
	QTemporaryDir tempDir;
	const auto drainCleanup = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup(); });
	ok &= expect(tempDir.isValid(), "temporary directory should be valid");
	QDir root(tempDir.path());
	QString error;

	// Two folder packages that differ in every way the report has a category
	// for, plus a same-size/different-bytes pair that only a content
	// comparison can tell apart.
	ok &= expect(root.mkpath(QStringLiteral("left/Textures")), "left package directory should be created");
	ok &= expect(root.mkpath(QStringLiteral("right/textures")), "right package directory should be created");
	ok &= expect(writeFile(root.filePath(QStringLiteral("left/same.txt")), QByteArray("same")), "left same.txt should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("right/same.txt")), QByteArray("same")), "right same.txt should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("left/changed.txt")), QByteArray("one")), "left changed.txt should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("right/changed.txt")), QByteArray("two and longer")), "right changed.txt should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("left/removed.txt")), QByteArray("gone")), "left removed.txt should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("right/added.txt")), QByteArray("new")), "right added.txt should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("left/Textures/Wall.tga")), QByteArray("wall")), "left cased texture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("right/textures/wall.tga")), QByteArray("wall")), "right lower-case texture should be written");
	// Same size, different bytes: identical to metadata, different in fact.
	ok &= expect(writeFile(root.filePath(QStringLiteral("left/trap.bin")), QByteArray("AAAA")), "left trap.bin should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("right/trap.bin")), QByteArray("BBBB")), "right trap.bin should be written");

	PackageArchive leftArchive;
	PackageArchive rightArchive;
	ok &= expect(leftArchive.load(root.filePath(QStringLiteral("left")), &error), "left package should load");
	ok &= expect(rightArchive.load(root.filePath(QStringLiteral("right")), &error), "right package should load");

	PackageCompareRequest request;
	request.leftLabel = QStringLiteral("base");
	request.rightLabel = QStringLiteral("candidate");
	const PackageCompareResult result = comparePackages(leftArchive, rightArchive, request);

	ok &= expect(!result.identical(), "two differing packages must not report as identical");
	ok &= expect(result.summary.leftCount == 5 && result.summary.rightCount == 5, "both sides should contribute five files");
	ok &= expect(result.summary.addedCount == 1, "added.txt should be the only addition");
	ok &= expect(result.summary.removedCount == 1, "removed.txt should be the only removal");
	ok &= expect(result.summary.changedCount == 2, "changed.txt and trap.bin should both be changed");
	ok &= expect(result.summary.caseOnlyCount == 1, "the re-cased texture should be its own category");
	ok &= expect(result.summary.identicalCount == 1, "same.txt should be the only identical entry");

	ok &= expect(hasStatus(result, QStringLiteral("added.txt"), PackageCompareStatus::Added), "added.txt should be reported as added");
	ok &= expect(hasStatus(result, QStringLiteral("removed.txt"), PackageCompareStatus::Removed), "removed.txt should be reported as removed");
	ok &= expect(hasStatus(result, QStringLiteral("changed.txt"), PackageCompareStatus::Changed), "changed.txt should be reported as changed");
	ok &= expect(hasStatus(result, QStringLiteral("same.txt"), PackageCompareStatus::Identical), "same.txt should be reported as identical");
	ok &= expect(hasStatus(result, QStringLiteral("trap.bin"), PackageCompareStatus::Changed), "a same-size content change must be caught by the digest");

	const PackageCompareEntry* trap = findByKey(result, QStringLiteral("trap.bin"));
	ok &= expect(trap && trap->content == PackageCompareContent::Sha256, "a folder package has no stored CRC, so the bytes should be hashed");
	ok &= expect(trap && trap->leftHash.size() == 64 && trap->leftHash != trap->rightHash, "differing SHA-256 digests should be reported");
	ok &= expect(trap && trap->sizeDelta == 0, "a same-size change should report no size delta");

	const PackageCompareEntry* changed = findByKey(result, QStringLiteral("changed.txt"));
	ok &= expect(changed && changed->sizeDelta == 11, "size delta should be right minus left");
	ok &= expect(changed && changed->content == PackageCompareContent::SizeOnly, "a size difference should not need a digest");

	const PackageCompareEntry* cased = findByKey(result, QStringLiteral("textures/wall.tga"));
	ok &= expect(cased && cased->status == PackageCompareStatus::CaseOnly, "a path that differs only in case should be its own category");
	ok &= expect(cased && cased->leftPath == QStringLiteral("Textures/Wall.tga") && cased->rightPath == QStringLiteral("textures/wall.tga"), "both spellings should be reported");
	ok &= expect(cased && !cased->contentChanged, "the re-cased entry's bytes are the same");

	const PackageCompareEntry* removed = findByKey(result, QStringLiteral("removed.txt"));
	ok &= expect(removed && removed->hasLeft && !removed->hasRight, "a removal should only have a left side");
	ok &= expect(removed && removed->rightPath.isEmpty(), "a removal should not invent a right path");

	// Metadata-only mode must never read entry contents, which is exactly what
	// the same-size trap file proves: it comes back identical.
	{
		PackageCompareRequest metadataRequest = request;
		metadataRequest.metadataOnly = true;
		const PackageCompareResult metadata = comparePackages(leftArchive, rightArchive, metadataRequest);
		ok &= expect(metadata.metadataOnly, "metadata-only mode should be reported");
		ok &= expect(hasStatus(metadata, QStringLiteral("trap.bin"), PackageCompareStatus::Identical), "metadata-only mode must not read bytes");
		const PackageCompareEntry* metadataTrap = findByKey(metadata, QStringLiteral("trap.bin"));
		ok &= expect(metadataTrap && metadataTrap->content == PackageCompareContent::NotCompared, "metadata-only mode should say the content was not compared");
		ok &= expect(metadataTrap && metadataTrap->noteId == QStringLiteral("metadata-only"), "metadata-only mode should carry its reason token");
		ok &= expect(metadataTrap && metadataTrap->leftHash.isEmpty() && metadataTrap->rightHash.isEmpty(), "metadata-only mode should produce no hashes");
		ok &= expect(metadata.summary.uncomparedCount >= 1, "metadata-only mode should count uncompared entries");
		// The differences that metadata alone can see are still reported.
		ok &= expect(metadata.summary.addedCount == 1 && metadata.summary.removedCount == 1, "metadata-only mode should still find additions and removals");
		ok &= expect(hasStatus(metadata, QStringLiteral("changed.txt"), PackageCompareStatus::Changed), "metadata-only mode should still catch a size change");
		ok &= expect(metadata.summary.caseOnlyCount == 1, "metadata-only mode should still catch a case-only difference");
	}

	// Determinism: the same inputs must produce the same entry order, the same
	// summary and byte-identical JSON on a second run.
	{
		const PackageCompareResult first = comparePackages(leftArchive, rightArchive, request);
		const PackageCompareResult second = comparePackages(leftArchive, rightArchive, request);
		ok &= expect(entryKeys(first) == entryKeys(second), "compare entry order should be deterministic");
		ok &= expect(packageCompareJsonBytes(first) == packageCompareJsonBytes(second), "compare JSON should be byte-identical across runs");
		ok &= expect(!packageCompareJsonBytes(first).isEmpty(), "compare JSON should not be empty");

		QStringList sortedKeys = entryKeys(first);
		QStringList asReported = sortedKeys;
		sortedKeys.sort();
		ok &= expect(asReported == sortedKeys, "compare entries should come out in key order");

		const QJsonObject json = packageCompareJson(first);
		ok &= expect(json.value(QStringLiteral("identical")).toBool() == false, "JSON should report the packages as different");
		ok &= expect(json.value(QStringLiteral("entries")).toArray().size() == first.entries.size(), "JSON should hold every entry");
		ok &= expect(json.value(QStringLiteral("summary")).toObject().value(QStringLiteral("addedCount")).toInt() == 1, "JSON summary should carry the counts");
		ok &= expect(!packageCompareText(first).isEmpty(), "compare text should render");
		ok &= expect(packageCompareStatusFromId(packageCompareStatusId(PackageCompareStatus::CaseOnly)) == PackageCompareStatus::CaseOnly, "status ids should round-trip");
	}

	// A package compared against itself is identical after payload verification.
	{
		const QString zipA = root.filePath(QStringLiteral("a.pk3"));
		const QString zipB = root.filePath(QStringLiteral("b.pk3"));
		const QVector<ZipInput> members = {
			{QStringLiteral("scripts/common.shader"), QByteArray("textures/common/caulk { }")},
			{QStringLiteral("maps/q3dm1.bsp"), QByteArray("IBSP-fake-payload")},
		};
		ok &= expect(buildStoredZip(zipA, members), "left PK3 fixture should be written");
		ok &= expect(buildStoredZip(zipB, members), "right PK3 fixture should be written");

		PackageArchive zipLeft;
		PackageArchive zipRight;
		ok &= expect(zipLeft.load(zipA, &error), "left PK3 should load");
		ok &= expect(zipRight.load(zipB, &error), "right PK3 should load");
		const PackageCompareResult same = comparePackages(zipLeft, zipRight);
		ok &= expect(same.identical(), "two identical PK3s should compare as identical");
		ok &= expect(same.summary.identicalCount == 2, "both members should be identical");
		const PackageCompareEntry* shader = findByKey(same, QStringLiteral("scripts/common.shader"));
		ok &= expect(shader && shader->content == PackageCompareContent::Sha256, "ZIP payloads must be read and verified before declaring equality");
		ok &= expect(shader && shader->leftHash.size() == 64 && shader->leftHash == shader->rightHash, "matching SHA-256 values should be reported");

		// One byte different, same length: the CRC has to notice.
		QVector<ZipInput> mutated = members;
		mutated[1].data = QByteArray("IBSP-fake-paylOad");
		const QString zipC = root.filePath(QStringLiteral("c.pk3"));
		ok &= expect(buildStoredZip(zipC, mutated), "mutated PK3 fixture should be written");
		PackageArchive zipMutated;
		ok &= expect(zipMutated.load(zipC, &error), "mutated PK3 should load");
		const PackageCompareResult mutatedResult = comparePackages(zipLeft, zipMutated);
		ok &= expect(hasStatus(mutatedResult, QStringLiteral("maps/q3dm1.bsp"), PackageCompareStatus::Changed), "a same-size CRC difference should be reported as changed");
		ok &= expect(!mutatedResult.identical(), "a mutated PK3 must not compare as identical");
	}

	// Comparing a package against what a staging plan would write.
	{
		PackageStagingModel plan;
		PackageReadControl importControl;
		auto importOptions = std::make_shared<PackageImportOptions>();
		importOptions->directory = root.filePath(QStringLiteral("comparison-imports"));
		importControl.importOptions = importOptions;
		ok &= expect(plan.loadBaseArchive(leftArchive, &error), "the plan should load the left package");
		ok &= expect(plan.addFile(root.filePath(QStringLiteral("right/added.txt")), QStringLiteral("added.txt"), &error,
			PackageStageConflictResolution::Block, importControl), "the plan should stage an addition");
		ok &= expect(plan.deleteEntry(QStringLiteral("removed.txt"), &error), "the plan should stage a deletion");
		ok &= expect(plan.replaceFile(QStringLiteral("trap.bin"), root.filePath(QStringLiteral("right/trap.bin")), &error, importControl), "the plan should stage a replacement");

		PackageCompareRequest planRequest;
		planRequest.leftLabel = QStringLiteral("package");
		planRequest.rightLabel = QStringLiteral("plan");
		const PackageCompareResult planResult = comparePackageToPlan(leftArchive, plan, planRequest);
		ok &= expect(hasStatus(planResult, QStringLiteral("added.txt"), PackageCompareStatus::Added), "the plan's addition should be reported as added");
		ok &= expect(hasStatus(planResult, QStringLiteral("removed.txt"), PackageCompareStatus::Removed), "the plan's deletion should be reported as removed");
		ok &= expect(hasStatus(planResult, QStringLiteral("trap.bin"), PackageCompareStatus::Changed), "the plan's same-size replacement should be reported as changed");
		ok &= expect(hasStatus(planResult, QStringLiteral("same.txt"), PackageCompareStatus::Identical), "an untouched entry should be identical");
		ok &= expect(planResult.rightLabel == QStringLiteral("plan"), "the plan side should keep its label");

		PackageStagingModel untouched;
		ok &= expect(untouched.loadBaseArchive(leftArchive, &error), "an untouched plan should load");
		const PackageCompareResult noop = comparePackageToPlan(leftArchive, untouched, planRequest);
		ok &= expect(noop.identical(), "a plan with no operations should compare as identical to its source");
	}

	// An unopened package must report instead of crashing.
	{
		PackageArchive closed;
		const PackageCompareResult closedResult = comparePackages(leftArchive, closed);
		ok &= expect(closedResult.entries.isEmpty() && !closedResult.warnings.isEmpty(), "comparing against a closed package should warn");
		ok &= expect(!closedResult.completed && !closedResult.identical(), "a failed comparison must never report a match");
	}

	{
		PackageCompareRequest limited;
		limited.maxEntryBytes = 1;
		const auto result = comparePackages(leftArchive, leftArchive, limited);
		ok &= expect(result.completed && !result.identical() && result.summary.uncomparedCount > 0,
			"over-budget content must prevent an identical verdict");
		ok &= expect(hasStatus(result, QStringLiteral("same.txt"), PackageCompareStatus::Uncompared),
			"unchecked content must have its own status rather than identical");
		limited.metadataOnly = true;
		ok &= expect(comparePackages(leftArchive, leftArchive, limited).identical(), "explicit metadata comparisons can match without reading contents");
	}
	{
		PackageCompareRequest cancellable;
		int progressed = 0;
		cancellable.progress = [&](int current, int) { progressed = current; };
		cancellable.isCancelled = [&]() { return progressed > 0; };
		const auto result = comparePackages(leftArchive, leftArchive, cancellable);
		ok &= expect(result.cancelled && !result.completed && !result.identical(), "cancellation must never report a complete match");
		ok &= expect(result.entries.size() < leftArchive.entries().size(), "cancellation must stop between entries");
		ok &= expect(packageCompareJson(result).value(QStringLiteral("cancelled")).toBool(), "JSON must mark partial comparisons");
	}

	ok &= streamingComparisonSmoke(root);
	return ok ? 0 : 1;
}
