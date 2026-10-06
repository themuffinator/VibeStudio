#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
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
class Source final : public PackageArchiveReader {
public:
	mutable int reads = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Zip; }
	QString sourcePath() const override { return "memory"; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override
	{
		QVector<PackageEntry> result;
		for (int at = 0; at < 4; ++at) { PackageEntry entry; entry.virtualPath = QString::number(at); result << entry; }
		return result;
	}
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { ++reads; return false; }
};
bool individualEdits()
{
	bool ok = true; QString error; PackageIndexLimits limits; limits.maximumEntries = 3;
	PackageStagingModel model({}, {}, {}, limits);
	ok &= expect(model.createEmpty(PackageArchiveFormat::Zip) && model.addBytes("x", "a/x")
		&& model.addBytes("y", "a/y") && model.undo(), "Prepare an editable view with a redo branch.");
	const auto revision = model.revision(); const auto undo = model.undoLabel(), redo = model.redoLabel();
	ok &= expect(!model.addBytes("z", "b/z", &error) && error.contains("snapshot")
		&& model.revision() == revision && model.operations().size() == 1 && model.undoLabel() == undo
		&& model.redoLabel() == redo && model.canRedo() && PackageStagingArchive(model, PackageStagingReadMode::InspectPlan).entries().size() == 2,
		"Implied-folder overflow refuses an individual edit without consuming history or redo.");
	ok &= expect(model.addBytes("z", "a/z", &error) && model.operations().last().id == "stage-3"
		&& !model.canRedo() && PackageStagingArchive(model, PackageStagingReadMode::InspectPlan).entries().size() == 3,
		"Exact browser allowance is usable and a refused edit consumes no operation identity.");
	const auto beforeRename = model.revision();
	ok &= expect(!model.renameEntry("a/z", "b/c/z", &error) && model.revision() == beforeRename
		&& model.operations().size() == 2 && model.summary().canSave,
		"File rename cannot admit an unavailable browser projection.");
	ok &= expect(!model.renameDirectory("a", "b/c", &error) && model.revision() == beforeRename,
		"Folder rename applies the same implied-folder policy.");
	const auto manifest = QJsonDocument::fromJson(model.manifestJson()).object().value("summary").toObject().value("viewLimits").toObject();
	ok &= expect(manifest.value("maximumEntries").toString() == "3" && manifest.value("maximumPathDepth").toInt() == 128,
		"Diagnostics expose the actual browser policy.");
	Source source;
	ok &= expect(!model.loadBaseArchive(source, &error) && error.contains("snapshot") && source.reads == 0
		&& model.revision() == beforeRename && model.operations().size() == 2,
		"Base adoption enforces the browser policy without reading payloads or replacing the live document.");
	PackageIndexLimits stricter; stricter.maximumEntries = 2;
	ok &= expect(!PackageStagingArchive(model, PackageStagingReadMode::InspectPlan, {}, stricter).isOpen()
		&& PackageStagingArchive(model, PackageStagingReadMode::InspectPlan).isOpen(),
		"A consumer can lower its view policy without altering the document policy.");
	return ok;
}
bool textDepthAndWarnings()
{
	bool ok = true; QString error;
	for (const qint64 allowance : {81, 82}) {
		PackageIndexLimits limits; limits.maximumMetadataBytes = allowance;
		PackageStagingModel model({}, {}, {}, limits); model.createEmpty(PackageArchiveFormat::Zip);
		const auto revision = model.revision(); const bool accepted = model.addBytes({}, "a/b", &error);
		ok &= expect(accepted == (allowance == 82) && (accepted || (error.contains("metadata") && model.revision() == revision && !model.canUndo())),
			"Browser text admission includes generated metadata and every implied folder at the exact UTF-16 boundary.");
	}
	PackageIndexLimits limits; limits.maximumPathDepth = 2;
	PackageStagingModel depth({}, {}, {}, limits); depth.createEmpty(PackageArchiveFormat::Zip);
	ok &= expect(depth.addBytes({}, "a/b") && !depth.renameEntry("a/b", "a/c/d", &error)
		&& error.contains("depth") && depth.operations().size() == 1,
		"Path-depth refusal preserves an admitted file and its history.");
	limits = {}; limits.maximumEntries = 2;
	PackageStagingModel warnings({}, {}, {}, limits); warnings.createEmpty(PackageArchiveFormat::Zip);
	ok &= expect(warnings.addBytes({}, "a") && warnings.addBytes({}, "a", &error, PackageStageConflictResolution::Skip)
		&& warnings.conflicts().size() == 1, "A visible warning uses the remaining browser record.");
	const auto revision = warnings.revision();
	ok &= expect(!warnings.addBytes({}, "a", &error, PackageStageConflictResolution::Skip)
		&& warnings.revision() == revision && warnings.conflicts().size() == 1,
		"Repeated warnings cannot bypass browser admission.");
	PackageStagingModel conflict; conflict.createEmpty(PackageArchiveFormat::Zip);
	ok &= expect(conflict.addBytes({}, "a") && conflict.addBytes({}, "a") && !conflict.summary().canSave
		&& PackageStagingArchive(conflict, PackageStagingReadMode::InspectPlan).isOpen(),
		"Ordinary semantic conflicts remain stageable and inspectable.");
	return ok;
}
bool groups()
{
	bool ok = true; QString error; PackageIndexLimits limits; limits.maximumEntries = 3;
	PackageStagingModel model({}, {}, {}, limits); model.createEmpty(PackageArchiveFormat::Zip);
	model.addBytes({}, "keep"); model.addBytes({}, "redo"); model.undo();
	const auto revision = model.revision(); const auto redo = model.redoLabel();
	ok &= expect(model.beginOperationGroup("Atomic import") && model.addBytes({}, "x/a")
		&& model.beginOperationGroup("Nested") && model.addBytes({}, "x/b") && model.endOperationGroup(true, &error),
		"Nested groups defer view admission until their outer commit.");
	ok &= expect(!model.endOperationGroup(true, &error) && error.contains("snapshot")
		&& model.revision() == revision && model.operations().size() == 1 && model.canRedo() && model.redoLabel() == redo,
		"Refused outer commit rolls back all nested edits while preserving redo and revision.");
	bool stop = false, reached = false; PackageReadControl control;
	control.progress = [&](const QString& phase, qint64, qint64) { if (phase == "Preparing package snapshot") { stop = reached = true; } };
	control.isCancelled = [&] { return stop; };
	ok &= expect(model.beginOperationGroup("Cancelled import") && model.addBytes({}, "next")
		&& !model.endOperationGroup(true, &error, control) && reached && error.contains("cancelled")
		&& model.revision() == revision && model.canRedo() && model.operations().size() == 1,
		"Cancellation during browser preparation rolls back a whole import.");
	ok &= expect(model.beginOperationGroup("Accepted import") && model.addBytes({}, "next")
		&& model.addBytes({}, "last") && model.endOperationGroup(true, &error)
		&& model.operations().size() == 3 && !model.canRedo() && model.undoLabel() == "Accepted import"
		&& model.undo() && model.revision() == revision && model.operations().size() == 1
		&& model.redo() && model.operations().size() == 3, "An accepted import remains a single undoable step.");
	// An outer batch may temporarily exceed its final browser allowance.
	PackageStagingModel transient({}, {}, {}, limits); transient.createEmpty(PackageArchiveFormat::Zip);
	ok &= expect(transient.beginOperationGroup("Replace batch") && transient.addBytes({}, "a/x")
		&& transient.addBytes({}, "b/x") && transient.deleteEntry("a/x") && transient.endOperationGroup(true, &error)
		&& PackageStagingArchive(transient, PackageStagingReadMode::InspectPlan).entries().size() == 2,
		"Batch admission checks its final view, allowing temporary browser growth within the private transaction.");
	return ok;
}
bool unstageAndRecovery(const QDir& root)
{
	bool ok = true; QString error;
	PackageStagingModel source; source.createEmpty(PackageArchiveFormat::Zip);
	source.addBytes({}, "a/x"); source.addBytes({}, "b/x"); source.deleteEntry("b/x");
	const auto draft = root.filePath("history.vibepackage");
	ok &= expect(PackageDraft::save(draft, &source, false, &error), "Persist history under the normal view policy.");
	PackageIndexLimits limits; limits.maximumEntries = 2;
	PackageStagingModel model({}, {}, {}, limits);
	ok &= expect(PackageDraft::load(draft, &model, &error) && model.viewLimits().maximumEntries == 2,
		"Loading a draft keeps the caller's browser policy and preserves legacy history.");
	const auto revision = model.revision();
	ok &= expect(!model.clearOperation(model.operations().last().id, &error) && error.contains("snapshot")
		&& model.revision() == revision && model.operations().size() == 3,
		"Unstaging a deletion cannot silently admit too many browser rows.");
	ok &= expect(model.undo() && !PackageStagingArchive(model, PackageStagingReadMode::InspectPlan).isOpen()
		&& model.undo() && PackageStagingArchive(model, PackageStagingReadMode::InspectPlan).isOpen()
		&& model.redo() && !PackageStagingArchive(model, PackageStagingReadMode::InspectPlan).isOpen()
		&& model.redo() && PackageStagingArchive(model, PackageStagingReadMode::InspectPlan).isOpen(),
		"Undo/Redo can traverse legacy unavailable views and recover without losing history.");
	const auto operations = model.operations();
	ok &= expect(model.beginOperationGroup("Unstage related changes") && model.clearOperation(operations.at(2).id, &error)
		&& model.clearOperation(operations.at(1).id, &error) && model.endOperationGroup(true, &error)
		&& model.operations().size() == 1 && model.undo() && model.operations().size() == 3,
		"An atomic unstage group can remove related changes whose intermediate view would exceed its policy.");
	limits.maximumPathDepth = 0; PackageStagingModel invalid({}, {}, {}, limits);
	ok &= expect(!invalid.createEmpty(PackageArchiveFormat::Zip, {}, &error) && !invalid.isLoaded()
		&& !PackageDraft::load(root.filePath("missing"), &invalid, &error) && error.contains("limits"),
		"Invalid view policies fail before document adoption or draft filesystem work.");
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	bool ok = individualEdits(); ok &= textDepthAndWarnings(); ok &= groups(); ok &= unstageAndRecovery(QDir(temporary.path()));
	// Controls reach metadata/history admission as well as expensive projection.
	{
		QString error;
		PackageStagingModel edits; edits.createEmpty(PackageArchiveFormat::Zip);
		edits.addBytes("original", QStringLiteral("original.txt"));
		edits.createDirectory(QStringLiteral("redo")); edits.undo();
		const auto revision = edits.revision(); const auto operations = edits.operations().size();
		const auto id = edits.operations().first().id;
		PackageReadControl cancelled; cancelled.isCancelled = [] { return true; };
		ok &= expect(!edits.renameEntry(QStringLiteral("original.txt"), QStringLiteral("renamed.txt"), &error, PackageStageConflictResolution::Block, cancelled)
			&& !edits.deleteEntry(QStringLiteral("original.txt"), &error, PackageStageConflictResolution::Block, cancelled)
			&& !edits.clearOperation(id, &error, cancelled) && !edits.beginOperationGroup(QStringLiteral("cancelled"), &error, cancelled)
			&& edits.revision() == revision && edits.operations().size() == operations && edits.canRedo(),
			"Pre-cancelled entry/history controls leave edits and redo intact.");
		int progress = 0; bool cancel = false; PackageReadControl during;
		during.isCancelled = [&] { return cancel; };
		during.progress = [&](const QString&, qint64, qint64) { if (++progress == 2) { cancel = true; } };
		ok &= expect(!edits.clearOperation(id, &error, during) && cancel && edits.revision() == revision && edits.canRedo()
			&& edits.operations().size() == operations, "Cancellation during unstage validation keeps the original operation and history.");
	}
	return ok ? 0 : 1;
}
