#include "core/package_draft.h"
#include "core/package_recovery.h"
#include "package_subset_test_helpers.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <utility>

using namespace vibestudio;
using namespace vibestudio::subset_test;

namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}
bool protectedBy(const PackageStagingModel& model, const QString& path)
{
	return PackageStagingArchive(model, PackageStagingReadMode::InspectPlan).protectsInputPath(path);
}
class ProtectedReader final : public PackageArchiveReader {
public:
	QString extra;
	QStringList extras;
	bool refuseProtection = false;
	mutable int reads = 0, visited = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Zip; }
	QString sourcePath() const override { return {}; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { PackageEntry entry; entry.virtualPath = "file"; entry.sizeBytes = 4; return {entry}; }
	bool readEntryBytes(const QString&, QByteArray* out, QString*, qint64 maximum) const override { ++reads; if (out) { *out = maximum < 0 ? QByteArray("data") : QByteArray("data").left(maximum); } return true; }
	bool streamEntryAt(qsizetype at, const std::function<bool(QByteArrayView)>& sink, QString*, const std::function<bool()>& cancelled) const override
	{
		++reads;
		return at == 0 && !(cancelled && cancelled()) && sink(QByteArrayView("data", 4));
	}
	bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor, QString* error, const PackageReadControl& control) const override
	{
		if (refuseProtection || !PackageArchiveReader::visitProtectedInputPaths(visitor, error, control) || !visitor(extra)) { return false; }
		for (const auto& path : extras) { ++visited; if (!visitor(path)) { return false; } }
		return true;
	}
};
bool borrowedProvenance(const QDir& root)
{
	bool ok = true; QString error; const auto input = root.filePath("import.bin"); ok &= put(input, "original");
	PackageStagingModel original; ok &= original.createEmpty(PackageArchiveFormat::Zip, {}, &error) && original.addFile(input, "file", &error);
	ok &= expect(protectedBy(original, input), "An ordinary imported source starts protected.", error);
	PackageStagingModel rebased; const PackageStagingArchive projection(original);
	ok &= expect(rebased.loadBaseArchive(projection, &error) && protectedBy(rebased, input), "Rebasing retains original imported sources.", error);
	const auto draft = root.filePath("rebased.vibepackage"); PackageStagingModel restored;
	ok &= expect(PackageDraft::save(draft, &rebased, false, &error) && protectedBy(rebased, input), "Saving a rebased draft retains live source protection.", error);
	ok &= expect(PackageDraft::load(draft, &restored, &error) && protectedBy(restored, input), "Reopening a rebased draft retains original imported sources.", error);
	PackageWriteRequest request; request.destinationPath = input; request.format = PackageArchiveFormat::Zip; request.allowOverwrite = true;
	const auto report = restored.writeArchive(request);
	ok &= expect(!report.succeeded() && get(input) == "original", "Draft export cannot replace the original imported file after rebase/reopen.", report.blockedMessages.join(';'));
	return ok;
}
bool providerProvenance(const QDir& root)
{
	bool ok = true; QString error; auto provider = std::make_shared<ProtectedReader>(); provider->extra = root.filePath("provider-input.bin");
	ok &= put(provider->extra, "sentinel"); PackageArchive archive; PackageStagingModel model, restored;
	ok &= expect(archive.loadSnapshot(provider, &error) && model.loadBaseArchive(archive, &error) && protectedBy(model, provider->extra), "An owned provider's extra source starts protected.", error);
	const auto draft = root.filePath("provider.vibepackage");
	ok &= expect(PackageDraft::save(draft, &model, false, &error) && protectedBy(model, provider->extra), "Checkpointing a virtual provider preserves its extra protected source.", error);
	ok &= expect(PackageDraft::load(draft, &restored, &error) && protectedBy(restored, provider->extra), "Reopening a virtual-provider draft preserves its extra protected source.", error);
	PackageWriteRequest request; request.destinationPath = provider->extra; request.format = PackageArchiveFormat::Zip; request.allowOverwrite = true;
	const auto report = restored.writeArchive(request);
	ok &= expect(!report.succeeded() && get(provider->extra) == "sentinel", "A portable provider draft cannot overwrite its extra source.", report.blockedMessages.join(';'));
	return ok;
}
bool draftProvenance(const QDir& root)
{
	bool ok = true; QString error; PackageStagingModel original, rebased, restored;
	const auto first = root.filePath("first.vibepackage"), second = root.filePath("second.vibepackage");
	ok &= original.createEmpty(PackageArchiveFormat::Zip, {}, &error) && original.addBytes("data", "file", &error)
		&& PackageDraft::save(first, &original, false, &error);
	ok &= expect(rebased.loadBaseArchive(PackageStagingArchive(original), &error) && protectedBy(rebased, QDir(first).filePath("new.zip")), "A rebased draft protects its original backing directory.", error);
	ok &= expect(PackageDraft::save(second, &rebased, false, &error) && PackageDraft::load(second, &restored, &error)
		&& protectedBy(restored, QDir(first).filePath("new.zip")), "Saving/reopening a rebased draft retains the original backing-directory guard.", error);
	return ok;
}
bool refusalAndCancellation(const QDir& root)
{
	bool ok = true; QString error;
	for (const auto limits : {PackageStagingMetadataLimits{12, PackageStagingMetadataLimits::metadataCeiling},
		PackageStagingMetadataLimits{PackageStagingMetadataLimits::recordCeiling, 512}}) {
		auto provider = std::make_shared<ProtectedReader>();
		for (int index = 0; index < 40; ++index) { provider->extras.append(root.filePath(QStringLiteral("protected-%1.bin").arg(index))); }
		PackageArchive archive; PackageStagingModel retained({}, limits);
		ok &= archive.loadSnapshot(provider, &error) && retained.createEmpty(PackageArchiveFormat::Zip) && retained.addBytes("keep", "keep");
		const auto before = retained.manifestJson();
		ok &= expect(!retained.loadBaseArchive(archive, &error) && error.contains("source protections") && provider->reads == 0
			&& provider->visited < 40 && retained.manifestJson() == before, "Root-count and text limits refuse before payload reads and preserve the open document.", error);
	}
	auto provider = std::make_shared<ProtectedReader>(); provider->extra = root.filePath("cancelled-input.bin");
	PackageArchive archive; PackageStagingModel retained;
	ok &= archive.loadSnapshot(provider, &error) && retained.createEmpty(PackageArchiveFormat::Zip) && retained.addBytes("keep", "keep");
	const auto before = retained.manifestJson(); bool reached = false, pulse = false; PackageReadControl control;
	control.progress = [&](const QString& phase, qint64, qint64) { if (phase == "Retaining package source protections") { reached = pulse = true; } };
	control.isCancelled = [&] { return std::exchange(pulse, false); };
	ok &= expect(!retained.loadBaseArchive(archive, &error, control) && reached && error.contains("cancelled") && provider->reads == 0
		&& retained.manifestJson() == before, "A one-shot cancellation during root collection is latched without partial adoption.", error);
	// Each new snapshot has immutable provider metadata from this point onward.
	auto refusing = std::make_shared<ProtectedReader>(); refusing->refuseProtection = true;
	PackageArchive refused; ok &= refused.loadSnapshot(refusing, &error);
	ok &= expect(!retained.loadBaseArchive(refused, &error) && error.contains("Unable to retain all package source protections")
		&& refusing->reads == 0 && retained.manifestJson() == before && refused.protectsInputPath(root.filePath("anything")),
		"An incomplete provider reports a diagnostic and fails closed without replacing the document.", error);
	auto overlong = std::make_shared<ProtectedReader>(); overlong->extra = root.filePath(QString(32769, 'x'));
	PackageArchive invalid; ok &= invalid.loadSnapshot(overlong, &error);
	ok &= expect(!retained.loadBaseArchive(invalid, &error) && error.contains("invalid or too long") && overlong->reads == 0
		&& retained.manifestJson() == before, "An overlong provider root is refused before filesystem resolution or payload access.", error);
	return ok;
}
bool outputCollisions(const QDir& root)
{
	bool ok = true; QString error;
	const auto output = root.filePath("guarded-export.zip"), lock = output + ".vibestudio-save.lock";
	auto provider = std::make_shared<ProtectedReader>(); provider->extra = lock;
	PackageArchive archive; PackageStagingModel model;
	ok &= archive.loadSnapshot(provider, &error) && model.loadBaseArchive(archive, &error);
	PackageWriteRequest request; request.destinationPath = output; request.format = PackageArchiveFormat::Zip;
	for (const bool dry : {true, false}) {
		request.dryRun = dry; const auto report = model.writeArchive(request);
		ok &= expect(!report.succeeded() && report.blockedMessages.join(';').contains("save lock") && provider->reads == 0
			&& !QFileInfo::exists(output) && !QFileInfo::exists(lock), "Save previews and writes refuse a protected lock path before reading or creating anything.", report.blockedMessages.join(';'));
	}
	const auto draft = root.filePath("enclosing.vibepackage");
	auto nested = std::make_shared<ProtectedReader>(); nested->extra = QDir(draft).filePath(".write.lock");
	PackageArchive nestedArchive; PackageStagingModel nestedModel;
	ok &= nestedArchive.loadSnapshot(nested, &error) && nestedModel.loadBaseArchive(nestedArchive, &error);
	const auto before = nestedModel.manifestJson(); nested->reads = 0;
	ok &= expect(!PackageDraft::save(draft, &nestedModel, false, &error, {}, true) && !PackageDraft::save(draft, &nestedModel, false, &error)
		&& error.contains("outside its source") && nested->reads == 0 && !QFileInfo::exists(draft) && nestedModel.manifestJson() == before,
		"A new draft cannot enclose a protected input, including a future managed lock.", error);
	return ok;
}
bool manifestValidation(const QDir& root)
{
	bool ok = true; QString error; PackageStagingModel source, retained;
	const auto draft = root.filePath("validation.vibepackage"), manifest = QDir(draft).filePath("document.json");
	ok &= source.createEmpty(PackageArchiveFormat::Zip) && source.addBytes("data", "file") && PackageDraft::save(draft, &source, false, &error);
	const auto document = QJsonDocument::fromJson(get(manifest)).object(); source.clear();
	ok &= expect(document.value("version") == 4 && document.value("protectedInputs").isArray(), "Every new draft declares the protection-aware version.");
	ok &= retained.createEmpty(PackageArchiveFormat::Zip) && retained.addBytes("keep", "keep"); const auto before = retained.manifestJson();
	int objects = 0; PackageReadControl control;
	control.progress = [&](const QString& path, qint64, qint64) { if (path.contains("/objects/")) { ++objects; } };
	QVector<QJsonValue> invalid {QJsonValue(QJsonValue::Undefined), QJsonObject(), QJsonArray{7}, QJsonArray{"relative/source"},
		QJsonArray{root.filePath("a/../source")}, QJsonArray{root.filePath(QString(32769, 'x'))}, QJsonArray{root.filePath(QString("bad") + QChar(0))}};
	for (const auto& value : invalid) {
		auto damaged = document; if (value.isUndefined()) { damaged.remove("protectedInputs"); } else { damaged.insert("protectedInputs", value); }
		ok &= put(manifest, QJsonDocument(damaged).toJson());
		ok &= expect(!PackageDraft::load(draft, &retained, &error, control) && !error.isEmpty() && objects == 0 && retained.manifestJson() == before,
			"Malformed protection metadata is refused before object hydration and preserves the previous document.", error);
	}
	// A removed/nonexistent source directory is still a protected root for future writes.
	auto missing = document; const auto missingRoot = root.filePath("missing-original"); missing.insert("protectedInputs", QJsonArray{missingRoot});
	ok &= put(manifest, QJsonDocument(missing).toJson());
	PackageStagingModel restored;
	ok &= expect(PackageDraft::load(draft, &restored, &error) && protectedBy(restored, QDir(missingRoot).filePath("new.zip")),
		"An unavailable source root still protects its future descendants after reopening.", error);
	return ok;
}
bool legacyAndRecovery(const QDir& root)
{
	bool ok = true; QString error;
	for (const int version : {1, 2, 3}) {
		const auto input = root.filePath(QStringLiteral("legacy-%1.bin").arg(version)); ok &= put(input, "original");
		const auto draft = root.filePath(QStringLiteral("legacy-%1.vibepackage").arg(version)), manifest = QDir(draft).filePath("document.json");
		PackageStagingModel model; ok &= model.createEmpty(PackageArchiveFormat::Zip) && model.addFile(input, "file", &error) && PackageDraft::save(draft, &model, false, &error);
		model.clear(); auto document = QJsonDocument::fromJson(get(manifest)).object(); document.insert("version", version); document.remove("protectedInputs");
		if (version == 1) {
			auto operations = document.value("operations").toArray();
			for (qsizetype index = 0; index < operations.size(); ++index) {
				auto operation = operations.at(index).toObject(); operation.remove("sourceOrdinal"); operations[index] = operation;
			}
			document.insert("operations", operations);
		}
		ok &= put(manifest, QJsonDocument(document).toJson());
		ok &= expect(PackageDraft::load(draft, &model, &error) && protectedBy(model, input) && PackageDraft::save(draft, &model, true, &error),
			"Legacy drafts migrate known imported-source protections on their next save.", error);
		const auto upgraded = QJsonDocument::fromJson(get(manifest)).object();
		ok &= expect(upgraded.value("version") == 4 && upgraded.value("protectedInputs").toArray().contains(input), "Legacy migration serializes known roots in version 4.");
	}
	auto provider = std::make_shared<ProtectedReader>(); provider->extra = root.filePath("recovery-input.bin"); ok &= put(provider->extra, "sentinel");
	PackageArchive archive; PackageStagingModel model;
	ok &= archive.loadSnapshot(provider, &error) && model.loadBaseArchive(archive, &error);
	const auto store = root.filePath("recovery"); auto session = PackageRecoverySession::acquire(store, "621b5c42-1ad2-45cb-b3ea-3bd8e71a8650", &error);
	if (!expect(bool(session), "Acquire a synthetic recovery session.", error)) { return false; }
	const auto checkpoint = session->checkpoint(model, "Protected source");
	ok &= expect(checkpoint.succeeded() && inspectPackageRecovery(checkpoint.path).readable(), "Recovery inventory accepts protection-aware checkpoints.", checkpoint.error);
	PackageStagingModel restored;
	ok &= expect(restorePackageRecovery(store, "621b5c42-1ad2-45cb-b3ea-3bd8e71a8650", checkpoint.manifestSha256, root.filePath("restored-protection.vibepackage"), &restored, &error)
		&& protectedBy(restored, provider->extra), "Recovery restoration preserves extra provider sources.", error);
	const auto manifest = QDir(checkpoint.path).filePath("document.json"); auto document = QJsonDocument::fromJson(get(manifest)).object();
	document.remove("protectedInputs"); ok &= put(manifest, QJsonDocument(document).toJson());
	ok &= expect(!inspectPackageRecovery(checkpoint.path).readable(), "Recovery inventory refuses version 4 checkpoints with missing protection metadata.");
	return ok;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	QDir root(temporary.path()); bool ok = borrowedProvenance(root); ok &= providerProvenance(root); ok &= draftProvenance(root);
	ok &= refusalAndCancellation(root); ok &= outputCollisions(root); ok &= manifestValidation(root); ok &= legacyAndRecovery(root);
	return ok ? 0 : 1;
}
