#include "core/package_directory.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool recordsAndRecovery(const QDir& root)
{
	bool ok = true; QString error; PackageStagingPlanLimits limits; limits.maximumRecords = 2;
	PackageStagingModel model({}, {}, limits);
	ok &= expect(model.createEmpty(PackageArchiveFormat::Zip, {}, &error)
		&& model.addBytes("a", "a", &error) && model.addBytes("b", "b", &error)
		&& model.preparePlan(&error) && model.plannedEntries().size() == 2, "Exact plan record allowance is usable.");
	ok &= expect(!model.addBytes("c", "c", &error) && error.contains("2-record") && model.operations().size() == 2,
		"New edits refuse plan overflow before entering history.");
	PackageStagingModel legacy; legacy.createEmpty(PackageArchiveFormat::Zip);
	legacy.addBytes("a", "a"); legacy.addBytes("b", "b"); legacy.addBytes("c", "c");
	const auto legacyDraft = root.filePath("legacy.vibepackage");
	ok &= expect(PackageDraft::save(legacyDraft, &legacy, false, &error) && PackageDraft::load(legacyDraft, &model, &error),
		"A draft from a larger policy retains history for rejected-plan recovery.");
	const auto revision = model.revision();
	ok &= expect(!model.preparePlan(&error) && error.contains("2-record") && model.plannedEntries().isEmpty()
		&& !model.summary().canSave && !model.summary().totalsAvailable && model.summary().blockingCount == 1 && model.conflicts().size() == 1
		&& model.beforeComposition().isEmpty() && model.afterComposition().isEmpty()
		&& model.revision() == revision, "Over-limit preparation publishes one blocking diagnostic and no partial rows.");
	int callbacks = 0; PackageReadControl count;
	count.progress = [&](const QString&, qint64, qint64) { ++callbacks; };
	ok &= expect(!model.preparePlan(&error, count) && callbacks == 0, "Repeated queries reuse a failed plan diagnostic.");
	PackageReadControl cancelled; cancelled.isCancelled = [] { return true; };
	ok &= expect(!model.preparePlan(&error, cancelled) && error.contains("cancelled")
		&& !model.preparePlan(&error) && error.contains("2-record"), "Cancellation does not replace a cached resource refusal.");
	const auto draft = root.filePath("limited.vibepackage");
	ok &= expect(PackageDraft::save(draft, &model, false, &error), "A rejected derived plan can still save its history as a draft.");
	PackageStagingModel loaded({}, {}, limits);
	ok &= expect(PackageDraft::load(draft, &loaded, &error) && loaded.planLimits().maximumRecords == 2
		&& !loaded.preparePlan(&error) && loaded.undo() && loaded.preparePlan(&error)
		&& loaded.plannedEntries().size() == 2 && loaded.summary().canSave && loaded.summary().totalsAvailable
		&& loaded.summary().afterBytes == 2 && !loaded.afterComposition().isEmpty(), "Draft reload preserves caller policy and recovers complete statistics through Undo.");
	ok &= expect(model.undo() && model.preparePlan(&error) && model.plannedEntries().size() == 2
		&& model.redo() && !model.preparePlan(&error) && model.undo() && model.preparePlan(&error),
		"Undo and Redo invalidate cached failures without losing edits.");
	const auto before = model.revision();
	ok &= expect(!model.createDirectory("empty", &error) && error.contains("2-record")
		&& model.revision() == before && model.canRedo() && model.operations().size() == 2,
		"Folder creation refuses excess records before changing revision or redo history.");
	return ok;
}
bool textAndKeys()
{
	bool ok = true; QString error;
	for (const qint64 allowance : {35, 36}) {
		PackageStagingPlanLimits limits; limits.maximumMetadataBytes = allowance;
		PackageStagingModel model({}, {}, limits);
		ok &= expect(model.createEmpty(PackageArchiveFormat::Zip), "Prepare text-boundary fixture.");
		const bool ready = model.addBytes({}, "a", &error);
		ok &= expect(ready == (allowance == 36) && (ready || error.contains("text limit")),
			"Logical row text includes the path, source label and generated operation identity.");
	}
	for (const qint64 allowance : {127, 128}) {
		PackageStagingPlanLimits limits; limits.maximumMetadataBytes = allowance;
		PackageStagingModel model({}, {}, limits);
		ok &= expect(model.createEmpty(PackageArchiveFormat::Zip), "Prepare prefix-text fixture.");
		const bool ready = model.addBytes({}, "a/b/c/d/e/f/g/h", &error);
		ok &= expect(ready == (allowance == 128) && (ready || error.contains("text limit")),
			"Prefix index charges each retained UTF-16 prefix before insertion.");
	}
	for (const qsizetype allowance : {2, 3}) {
		PackageStagingPlanLimits limits; limits.maximumIndexKeys = allowance;
		PackageStagingModel model({}, {}, limits);
		ok &= expect(model.createEmpty(PackageArchiveFormat::Zip), "Prepare index-key fixture.");
		const bool ready = model.addBytes({}, "a/b/c", &error);
		ok &= expect(ready == (allowance == 3) && (ready || error.contains("index-key limit")),
			"Entry and parent keys share an exact key allowance.");
	}
	PackageStagingPlanLimits limits; limits.maximumIndexKeys = 2;
	PackageStagingModel folders({}, {}, limits);
	ok &= expect(folders.createEmpty(PackageArchiveFormat::Zip) && folders.addBytes({}, "old/a") && folders.preparePlan(&error), "Prepare folder index-growth fixture.");
	const auto revision = folders.revision();
	ok &= expect(!folders.renameDirectory("old", "a/b/c", &error) && error.contains("index-key limit")
		&& folders.revision() == revision && folders.operations().size() == 1 && folders.summary().canSave,
		"Folder rename admits all derived parent keys before changing history.");
	limits = {}; limits.maximumMetadataBytes = 400;
	PackageStagingModel expansion({}, {}, limits);
	ok &= expect(expansion.createEmpty(PackageArchiveFormat::Zip) && expansion.addBytes({}, "old/a")
		&& expansion.addBytes({}, "old/b") && expansion.preparePlan(&error), "Prepare folder text-expansion fixture.");
	const auto before = expansion.revision();
	ok &= expect(!expansion.renameDirectory("old", QString(100, 'x'), &error) && error.contains("text limit")
		&& expansion.revision() == before && expansion.operations().size() == 2
		&& expansion.plannedEntries().first().virtualPath == "old/a", "Folder rewrite stops before accumulating excess derived text.");
	return ok;
}
bool reuseAndConflicts()
{
	bool ok = true; QString error; PackageStagingPlanLimits limits;
	limits.maximumRecords = 2; limits.maximumIndexKeys = 2;
	PackageStagingModel model({}, {}, limits);
	ok &= expect(model.createEmpty(PackageArchiveFormat::Wad, "WAD3", &error)
		&& model.addBytes({}, "A") && model.addBytes({}, "B") && model.deleteEntry("A")
		&& model.addBytes({}, "C") && model.deleteEntry("B") && model.addBytes({}, "D")
		&& model.preparePlan(&error), "Deleted slots and keys are reclaimed during replay.");
	const auto entries = model.plannedEntries();
	ok &= expect(entries.size() == 2 && entries.first().virtualPath == "C" && entries.last().virtualPath == "D",
		"Slot compaction preserves WAD insertion order.");
	PackageStagingModel conflicts({}, {}, limits);
	ok &= expect(conflicts.createEmpty(PackageArchiveFormat::Zip) && conflicts.addBytes({}, "a")
		&& conflicts.addBytes({}, "a", &error, PackageStageConflictResolution::Skip)
		&& conflicts.preparePlan(&error) && conflicts.summary().canSave && conflicts.conflicts().size() == 1,
		"A warning and its live entry share the record budget.");
	const auto revision = conflicts.revision();
	ok &= expect(!conflicts.addBytes({}, "a", &error, PackageStageConflictResolution::Skip)
		&& error.contains("2-record") && conflicts.revision() == revision && conflicts.summary().canSave
		&& conflicts.conflicts().size() == 1, "Repeated conflicts cannot bypass record admission or consume history.");
	return ok;
}
bool identity()
{
	bool ok = true; QString error; QVector<PackageStagedEntry> entries;
	for (const QString& path : {QStringLiteral("folder/a"), QStringLiteral("folder/b")}) {
		PackageStagedEntry entry; entry.virtualPath = path; entries << entry;
	}
	qint64 bytes = 0;
	for (const auto& entry : entries) {
		bytes += QJsonDocument(QJsonArray{entry.virtualPath, packageEntryKindId(entry.kind), entry.sourceOrdinal,
			entry.operationId, entry.baseVirtualPath, QString::number(entry.sourceReaderIndex)}).toJson(QJsonDocument::Compact).size() * 2;
	}
	const auto expected = packageDirectoryIdentity(entries, "folder", &error);
	PackageStagingPlanLimits limits; limits.maximumMetadataBytes = bytes;
	ok &= expect(!expected.isEmpty() && packageDirectoryIdentity(entries, "folder", &error, {}, limits) == expected,
		"Folder identity accepts the exact conservative serialized-text budget.");
	--limits.maximumMetadataBytes;
	ok &= expect(packageDirectoryIdentity(entries, "folder", &error, {}, limits).isEmpty() && error.contains("text limit"),
		"Folder identity refuses before retaining an over-limit serialized record.");
	for (const int invalid : {0, 1, 2, 3, 4, 5}) {
		limits = {};
		if (invalid == 0) { limits.maximumRecords = 0; }
		if (invalid == 1) { limits.maximumRecords = PackageStagingPlanLimits::recordCeiling + 1; }
		if (invalid == 2) { limits.maximumIndexKeys = 0; }
		if (invalid == 3) { limits.maximumIndexKeys = PackageStagingPlanLimits::indexKeyCeiling + 1; }
		if (invalid == 4) { limits.maximumMetadataBytes = 0; }
		if (invalid == 5) { limits.maximumMetadataBytes = PackageStagingPlanLimits::metadataCeiling + 1; }
		PackageStagingModel model({}, {}, limits);
		ok &= expect(!model.createEmpty(PackageArchiveFormat::Zip, {}, &error) && error.contains("Invalid package plan limits")
			&& !model.isLoaded(), "Invalid caller limits cannot create a document.");
		ok &= expect(!PackageDraft::load("missing-policy-fixture.vibepackage", &model, &error)
			&& error.contains("Invalid package plan limits") && !model.isLoaded(),
			"Draft loading validates caller plan limits before attempting filesystem access.");
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
		for (int at = 0; at < 3; ++at) { PackageEntry entry; entry.virtualPath = QString::number(at); result << entry; }
		return result;
	}
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { ++reads; return false; }
};
bool adoptionAndPublication(const QDir& root)
{
	bool ok = true; QString error; Source source; PackageStagingPlanLimits limits; limits.maximumRecords = 2;
	PackageStagingModel model({}, {}, limits);
	ok &= expect(model.createEmpty(PackageArchiveFormat::Zip) && model.addBytes("a", "a")
		&& model.addBytes("b", "b") && model.undo(), "Prepare preserved document and redo history.");
	const auto revision = model.revision();
	ok &= expect(!model.loadBaseArchive(source, &error) && error.contains("2-record") && source.reads == 0
		&& model.revision() == revision && model.canRedo() && model.plannedEntries().size() == 1,
		"Over-limit base admission preserves the live document before reading any payload.");
	const auto manifest = QJsonDocument::fromJson(model.manifestJson(&error)).object();
	const auto policy = manifest.value("summary").toObject().value("planLimits").toObject();
	ok &= expect(policy.value("maximumRecords").toString() == "2"
		&& policy.value("maximumIndexKeys").toString() == "500000"
		&& policy.value("maximumMetadataBytes").toString() == "134217728", "Manifest exposes the actual plan policy as decimal strings.");
	PackageStagingModel legacy; legacy.createEmpty(PackageArchiveFormat::Zip);
	legacy.addBytes("a", "a"); legacy.addBytes("b", "b"); legacy.addBytes("c", "c");
	const auto legacyDraft = root.filePath("publication-legacy.vibepackage");
	ok &= expect(PackageDraft::save(legacyDraft, &legacy, false, &error) && PackageDraft::load(legacyDraft, &model, &error),
		"Prepare publication refusal from a draft saved under a larger policy.");
	const auto write = [](const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); };
	const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
	PackageWriteRequest request; request.destinationPath = root.filePath("sentinel.zip"); request.backupPath = root.filePath("sentinel.bak");
	request.manifestPath = root.filePath("sentinel.json"); request.writeManifest = true; request.allowOverwrite = true;
	ok &= expect(write(request.destinationPath, "output") && write(request.backupPath, "backup") && write(request.manifestPath, "manifest"), "Prepare existing publication files.");
	const auto inventory = root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
	for (const bool dryRun : {false, true}) {
		request.dryRun = dryRun; const auto report = model.writeArchive(request);
		ok &= expect(!report.succeeded() && !report.cancelled && !report.outputCommitted
			&& report.blockedMessages.join(' ').contains("2-record") && read(request.destinationPath) == "output"
			&& read(request.backupPath) == "backup" && read(request.manifestPath) == "manifest"
			&& root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) == inventory,
			"Actual and dry-run writes reject before touching output, backup, manifest or publication storage.");
	}
	const PackageStagingArchive view(model, PackageStagingReadMode::InspectPlan);
	ok &= expect(!view.isOpen() && view.entries().isEmpty() && view.errorString().contains("2-record")
		&& model.undo() && model.summary().canSave, "Snapshot refusal exposes no partial rows and leaves recovery available.");
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	bool ok = recordsAndRecovery(QDir(temporary.path())); ok &= textAndKeys(); ok &= reuseAndConflicts(); ok &= identity();
	ok &= adoptionAndPublication(QDir(temporary.path()));
	return ok ? 0 : 1;
}
