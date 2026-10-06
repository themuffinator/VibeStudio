#include "core/package_directory.h"
#include "core/package_staging.h"
#include "core/package_wad_groups.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
QByteArray signature(const QVector<PackageStagedEntry>& entries)
{
	QJsonArray result;
	for (const auto& entry : entries) {
		result.append(QJsonArray{entry.virtualPath, packageEntryKindId(entry.kind), entry.sourceOrdinal,
			QString::number(entry.sourceReaderIndex), entry.operationId, entry.baseVirtualPath, entry.source,
			entry.wadInsertBefore, entry.wadNamespace, entry.wadLumpType, entry.hasInlineBytes,
			QString::fromLatin1(entry.inlineBytes.toHex())});
	}
	return QJsonDocument(result).toJson(QJsonDocument::Compact);
}
QByteArray diagnostics(const PackageStagingModel& model)
{
	QJsonArray result;
	for (const auto& conflict : model.conflicts()) { result.append(QJsonArray{conflict.operationId, conflict.virtualPath, conflict.message, conflict.blocking}); }
	return QJsonDocument(result).toJson(QJsonDocument::Compact);
}
QString legacyIdentity(const QVector<PackageStagedEntry>& entries, const QString& directory)
{
	// Independent reference for the exact persisted representation used before
	// streaming. Include escaping, Unicode, duplicate records and empty folders.
	const auto root = directory.normalized(QString::NormalizationForm_C).toCaseFolded();
	QStringList records;
	for (const auto& entry : entries) {
		const auto path = entry.virtualPath.normalized(QString::NormalizationForm_C).toCaseFolded();
		if (path != root && !path.startsWith(root + '/')) { continue; }
		records << QString::fromUtf8(QJsonDocument(QJsonArray{entry.virtualPath, packageEntryKindId(entry.kind),
			entry.sourceOrdinal, entry.operationId, entry.baseVirtualPath, QString::number(entry.sourceReaderIndex)}).toJson(QJsonDocument::Compact));
	}
	if (records.isEmpty()) { return {}; }
	records.sort(Qt::CaseSensitive);
	QJsonArray values; for (const auto& record : records) { values.append(record); }
	return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(values).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
bool folders()
{
	bool ok = true; QString error;
	QVector<PackageStagedEntry> entries;
	for (int at = 1030; at >= 0; --at) {
		PackageStagedEntry entry;
		entry.virtualPath = QString::fromUtf8("Fo\xcc\x88lder/") + QString::number(at) + QString::fromUtf8("/\"\xe6\xbc\xa2\xf0\x9f\x8c\xb3");
		entry.operationId = QStringLiteral("operation\n\"%1").arg(at % 9);
		entry.baseVirtualPath = "original/" + QString::number(at); entry.sourceOrdinal = at % 12; entry.sourceReaderIndex = at;
		entry.kind = at % 8 == 0 ? PackageEntryKind::Directory : PackageEntryKind::File;
		entries << entry;
	}
	entries << entries.first();
	PackageStagedEntry empty; empty.virtualPath = QString::fromUtf8("F\xc3\xb6lder/empty"); empty.kind = PackageEntryKind::Directory; entries << empty;
	const QString root = QString::fromUtf8("F\xc3\xb6lder"), expected = legacyIdentity(entries, root);
	ok &= expect(!expected.isEmpty() && packageDirectoryIdentity(entries, root, &error) == expected && error.isEmpty(),
		"Streamed folder identity is byte-compatible with persisted escaped JSON records.");
	std::reverse(entries.begin(), entries.end());
	ok &= expect(packageDirectoryIdentity(entries, root) == expected && packageDirectoryIdentity(entries, "missing").isEmpty(),
		"Identity preserves order independence and the missing-tree sentinel.");
	PackageStageOperation operation; operation.type = PackageStageOperationType::RenameDirectory;
	operation.id = "rename"; operation.virtualPath = root; operation.targetVirtualPath = "moved"; operation.sourceTreeIdentity = expected;
	const auto original = signature(entries);
	qsizetype checks = 0; PackageReadControl count;
	count.isCancelled = [&] { ++checks; return false; };
	auto completed = entries;
	ok &= expect(applyPackageDirectoryOperation(&completed, operation, &error, count), "Prepare a complete folder rename for cancellation boundaries.");
	const auto renamed = signature(completed);
	const QVector<qsizetype> boundaries{1, 30, checks / 4, checks / 2, checks - 2, checks};
	for (const auto boundary : boundaries) {
		auto candidate = entries; qsizetype at = 0; PackageReadControl control;
		// A one-shot signal proves nested helpers latch the cancellation.
		control.isCancelled = [&] { return ++at == boundary; };
		ok &= expect(!applyPackageDirectoryOperation(&candidate, operation, &error, control)
			&& error.contains("cancelled") && signature(candidate) == original,
			"Cancelled folder scan/hash/sort/rewrite leaves the entire candidate unchanged.");
		ok &= expect(applyPackageDirectoryOperation(&candidate, operation, &error) && signature(candidate) == renamed,
			"A cancelled folder edit can be retried with its original captured identity.");
	}
	for (const bool atFinish : {false, true}) {
		bool fired = false, stop = false; PackageReadControl control;
		control.isCancelled = [&] { return stop; };
		control.progress = [&](const QString&, qint64 done, qint64 total) {
			if ((atFinish && total > 0) || (!atFinish && done >= 256)) { fired = stop = true; }
		};
		ok &= expect(packageDirectoryIdentity(entries, root, &error, control).isEmpty() && fired && error.contains("cancelled"),
			"Folder identity checks cancellation after both intermediate and final progress callbacks.");
	}
	return ok;
}

PackageStagingModel editedPlan()
{
	PackageStagingModel model; model.createEmpty(PackageArchiveFormat::Zip);
	model.beginOperationGroup("Fixture files");
	for (int at = 599; at >= 0; --at) {
		model.addBytes(QByteArray::number(at), QStringLiteral("tree/part-%1/a/b/file-%2.txt").arg(at % 7).arg(at, 4, 10, QLatin1Char('0')));
	}
	model.endOperationGroup();
	model.createDirectory("tree/empty");
	model.renameDirectory("tree", "moved");
	model.deleteDirectory("moved/part-2");
	model.addBytes("conflict", "moved/part-0/a/b/file-0000.txt");
	model.undo(); model.redo(); // Exercise cold replay after admitted edits.
	return model;
}
bool replay()
{
	bool ok = true; QString error;
	const auto original = editedPlan(); auto reference = original;
	qsizetype checks = 0, callbacks = 0; PackageReadControl count;
	count.isCancelled = [&] { ++checks; return false; };
	count.progress = [&](const QString&, qint64, qint64) { ++callbacks; };
	ok &= expect(reference.preparePlan(&error, count) && checks > 1000 && callbacks > 5 && reference.summary().blockingCount == 1,
		"Full replay reports progress through files, folder edits, sorting and a real conflict.");
	const auto expected = signature(reference.plannedEntries()), conflicts = diagnostics(reference);
	const QVector<qsizetype> boundaries{1, 5, 64, 256, checks / 4, checks / 2, checks - 2, checks};
	for (const auto boundary : boundaries) {
		auto model = original; qsizetype at = 0; PackageReadControl control;
		control.isCancelled = [&] { return ++at == boundary; };
		ok &= expect(!model.preparePlan(&error, control) && error.contains("cancelled")
			&& model.revision() == original.revision() && model.operations().size() == original.operations().size()
			&& model.undoLabel() == original.undoLabel() && model.canRedo() == original.canRedo(),
			"Cancelled replay never mutates document revision, staged edits or Undo/Redo.");
		qsizetype retryProgress = 0; PackageReadControl retry;
		retry.progress = [&](const QString&, qint64, qint64) { ++retryProgress; };
		ok &= expect(model.preparePlan(&error, retry) && retryProgress > 0 && signature(model.plannedEntries()) == expected
			&& diagnostics(model) == conflicts, "Retry rebuilds the complete plan and conflicts; no partial cache was adopted.");
		ok &= expect(model.undo() && model.summary().canSave && model.redo() && signature(model.plannedEntries()) == expected
			&& diagnostics(model) == conflicts, "Undo and Redo still restore the exact plan after cancellation and retry.");
	}
	bool stop = true; PackageReadControl cancelled; cancelled.isCancelled = [&] { return stop; };
	ok &= expect(!reference.preparePlan(&error, cancelled) && error.contains("cancelled"), "An already cached plan still honors pre-cancellation.");
	stop = false;
	ok &= expect(reference.preparePlan(&error, cancelled) && error.isEmpty() && signature(reference.plannedEntries()) == expected,
		"Pre-cancellation preserves an existing complete cache.");
	for (const QString& phase : {QStringLiteral("Preparing package plan"), QStringLiteral("Checking package folder contents"), QStringLiteral("Preparing package folder edit")}) {
		bool reached = false; PackageReadControl control; auto model = original;
		control.progress = [&](const QString& current, qint64 done, qint64) { if (current == phase && done > 0) { reached = true; } };
		control.isCancelled = [&] { return reached; };
		const PackageStagingArchive view(model, PackageStagingReadMode::InspectPlan, control);
		ok &= expect(reached && !view.isOpen() && view.entries().isEmpty() && view.errorString().contains("cancelled"),
			"Inspection exposes no partial rows when plan or folder preparation is cancelled.");
	}
	// Folder admission has its own atomic boundary, including a warm plan cache.
	auto model = original; model.undo(); model.preparePlan();
	const auto before = signature(model.plannedEntries()); const auto revision = model.revision();
	bool stopped = false; PackageReadControl control;
	control.progress = [&](const QString& phase, qint64 done, qint64) { if (phase == "Checking package folder contents" && done > 0) { stopped = true; } };
	control.isCancelled = [&] { return stopped; };
	ok &= expect(!model.renameDirectory("moved", "final", &error, control) && stopped && error.contains("cancelled")
		&& model.revision() == revision && model.canRedo() && signature(model.plannedEntries()) == before,
		"A cancelled public folder edit preserves its redo branch and cached plan.");
	ok &= expect(model.renameDirectory("moved", "final", &error) && model.undo() && signature(model.plannedEntries()) == before,
		"Folder admission can retry and Undo after cancellation.");
	return ok;
}

bool wad()
{
	bool ok = true; QString error; PackageStagingModel original;
	original.createEmpty(PackageArchiveFormat::Wad, "PWAD"); original.beginOperationGroup("WAD preparation fixture");
	for (int at = 599; at >= 0; --at) { original.addBytes({}, QStringLiteral("G%1").arg(at, 6, 10, QLatin1Char('0'))); }
	for (const QString& name : {"MAP01", "SECTORS", "VERTEXES", "SIDEDEFS", "LINEDEFS", "THINGS"}) { original.addBytes({}, name); }
	original.endOperationGroup();
	original.undo(); original.redo(); // Drop the admission cache for replay cancellation coverage.
	auto reference = original;
	ok &= expect(reference.preparePlan(&error) && reference.summary().canSave, "Source-free WAD assembly remains usable.");
	const auto expected = signature(reference.plannedEntries());
	const auto entries = reference.plannedEntries();
	ok &= expect(entries.at(600).virtualPath == "MAP01" && entries.at(601).virtualPath == "THINGS"
		&& entries.last().virtualPath == "SECTORS", "WAD assembly preserves globals and canonical map membership.");
	qsizetype checks = 0; PackageReadControl count; auto counted = original;
	count.isCancelled = [&] { ++checks; return false; };
	ok &= expect(counted.preparePlan(&error, count), "Count checkpoints across WAD assembly.");
	for (const qsizetype boundary : {checks / 3, checks * 2 / 3, checks - 100, checks - 2}) {
		qsizetype at = 0; auto cancelled = original; PackageReadControl interrupt;
		interrupt.isCancelled = [&] { return ++at == boundary; };
		ok &= expect(!cancelled.preparePlan(&error, interrupt) && error.contains("cancelled")
			&& cancelled.preparePlan(&error) && signature(cancelled.plannedEntries()) == expected,
			"Cancellation within WAD scan, grouping and final ordering cannot publish a partial plan.");
	}
	bool stop = false; PackageReadControl control;
	control.progress = [&](const QString& phase, qint64 done, qint64) { if (phase == "Ordering package WAD lumps" && done > 0) { stop = true; } };
	control.isCancelled = [&] { return stop; };
	auto model = original;
	ok &= expect(!model.preparePlan(&error, control) && stop && error.contains("cancelled")
		&& model.preparePlan(&error) && signature(model.plannedEntries()) == expected, "WAD ordering cancellation discards its partial layout and retries exactly.");
	stop = false;
	const auto inventory = inspectPackageWadGroups(original, control);
	ok &= expect(stop && !inventory.error.isEmpty() && inventory.groups.isEmpty(), "WAD group inspection controls initial plan preparation too.");
	const auto reviewed = inspectPackageWadGroups(model);
	ok &= expect(reviewed.succeeded() && reviewed.groups.size() == 1, "Review the complete WAD group before editing.");
	if (reviewed.groups.size() == 1) {
		PackageWadGroupEditRequest request; request.groupId = reviewed.groups.first().id;
		request.expectedFingerprint = reviewed.fingerprint; request.targetName = "INTRO";
		const auto revision = model.revision(); PackageWadGroupEditReview edit;
		stop = false;
		control.progress = [&](const QString& phase, qint64 done, qint64) { if (phase == "Preparing package plan" && done > 0) { stop = true; } };
		ok &= expect(!stagePackageWadGroupEdit(&model, request, &edit, &error, control) && stop && error.contains("cancelled")
			&& model.revision() == revision && signature(model.plannedEntries()) == expected && edit.changes.isEmpty(),
			"Cancelling final WAD edit preparation preserves the reviewed original and exposes no applied changes.");
		ok &= expect(stagePackageWadGroupEdit(&model, request, &edit, &error) && model.undo() && signature(model.plannedEntries()) == expected,
			"The same WAD edit review retries successfully and remains one undo step.");
	}
	return ok;
}

class Source final : public PackageArchiveReader {
public:
	mutable int reads = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Zip; }
	QString sourcePath() const override { return "memory"; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override
	{
		QVector<PackageEntry> result;
		for (int at = 1025; at >= 0; --at) { PackageEntry entry; entry.virtualPath = QStringLiteral("base/%1.txt").arg(at); entry.sizeBytes = 1; result << entry; }
		return result;
	}
	bool readEntryBytes(const QString&, QByteArray* bytes, QString*, qint64) const override { ++reads; if (bytes) { *bytes = "x"; } return true; }
};
bool publication(const QDir& root)
{
	bool ok = true; QString error; auto owner = std::make_shared<Source>(); auto& source = *owner; PackageStagingModel original;
	PackageArchive archive;
	ok &= expect(archive.loadSnapshot(owner, &error) && original.loadBaseArchive(archive, &error), "Prepare base-backed plan without payload reads.");
	const auto revision = original.revision();
	const auto write = [](const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); };
	const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
	PackageWriteRequest request; request.destinationPath = root.filePath("output.zip"); request.backupPath = root.filePath("backup.zip");
	request.manifestPath = root.filePath("manifest.json"); request.writeManifest = true; request.allowOverwrite = true;
	ok &= expect(write(request.destinationPath, "output") && write(request.backupPath, "backup") && write(request.manifestPath, "manifest"), "Prepare publication sentinels.");
	const auto inventory = root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
	for (const bool dryRun : {false, true}) {
		bool stop = false; auto model = original; request.dryRun = dryRun;
		request.byteProgress = [&](PackageWritePhase phase, const QString& text, quint64 done, quint64) {
			if (phase == PackageWritePhase::CheckIndex && text == "Preparing package plan" && done > 0) { stop = true; }
		};
		request.isCancelled = [&] { return stop; };
		const auto result = model.writeArchive(request);
		ok &= expect(stop && result.cancelled && !result.outputCommitted && !result.succeeded() && !result.blockedMessages.isEmpty()
			&& source.reads == 0 && model.revision() == revision, "Both writer modes cancel during base plan construction before reading sources.");
		ok &= expect(read(request.destinationPath) == "output" && read(request.backupPath) == "backup" && read(request.manifestPath) == "manifest"
			&& root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) == inventory,
			"Plan cancellation leaves output, backup, manifest and publication inventory untouched.");
	}
	bool stop = false; PackageReadControl control;
	control.progress = [&](const QString& phase, qint64 done, qint64) { if (phase == "Preparing package plan" && done > 0) { stop = true; } };
	control.isCancelled = [&] { return stop; };
	auto model = original;
	ok &= expect(!model.verifySources(&error, control) && stop && source.reads == 0, "Source verification honors plan cancellation before payload work.");
	stop = false;
	ok &= expect(model.manifestJson(&error, control).isEmpty() && stop && source.reads == 0, "Manifest preparation honors initial plan cancellation.");
	ok &= expect(model.preparePlan(&error) && model.plannedEntries().size() == 1026 && model.summary().canSave,
		"A base-backed plan is fully recoverable after cancelled writer, verification and manifest preparation.");
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	bool ok = folders(); ok &= replay(); ok &= wad(); ok &= publication(QDir(temporary.path()));
	return ok ? 0 : 1;
}
