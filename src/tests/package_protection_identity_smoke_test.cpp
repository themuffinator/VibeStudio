#include "core/package_archive.h"
#include "core/package_draft.h"
#include "core/package_path_identity_p.h"
#include "core/package_protection_p.h"
#include "core/package_staging.h"
#include "tests/model_output_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <iostream>
#include <utility>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *message, const QString &error = {})
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
bool put(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("package-identity-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	QDir dir(temporary.path());
	const auto input = dir.filePath(QString::fromUtf8("input-\xc3\xa9.bin")), alias = dir.filePath("input-alias.zip");
	const auto copy = dir.filePath("copy.zip"), second = dir.filePath("second.bin"), secondAlias = dir.filePath("second-alias.zip");
	const QByteArray original("protected source"), different("another file");
	expect(put(input, original) && put(copy, original) && modelTestHardLink(input, alias) && put(second, different) &&
			   modelTestHardLink(second, secondAlias),
		   "prepare independent linked and copied input fixtures");
	QString error;
	for (bool portable : {false, true})
	{
		const auto a = inspectPackagePathIdentity(input, portable), b = inspectPackagePathIdentity(alias, portable);
		expect(a.error.isEmpty() && b.error.isEmpty() && a.regular && a.links == 2 && b.links == 2 &&
				   (portable ? a.key.isEmpty() && b.key.isEmpty() : !a.key.isEmpty() && a.key == b.key),
			   "native and portable probes recognise Unicode regular-file hard links");
		PackageInputProtectionSet set(100, 65536, &error, {}, {}, portable);
		expect(set.add(input) && set.finish(), "retain a protected file", error);
		bool protectedPath = false;
		expect(set.check(alias, &protectedPath) && protectedPath, "protect an existing hard-link alias", error);
		expect(set.check(copy, &protectedPath) && !protectedPath, "byte-identical independent copies remain valid outputs", error);
		expect(set.check(secondAlias, &protectedPath) && !protectedPath, "unrelated hard-linked files remain valid outputs", error);
		expect(set.add(second) && set.check(secondAlias, &protectedPath) && protectedPath,
			   "adding a source invalidates the cached identity index", error);
		expect(set.check(dir.filePath("new/nested/result.zip"), &protectedPath) && !protectedPath &&
				   !QFileInfo::exists(dir.filePath("new")),
			   "new destinations are inspected without directories or payload writes", error);
		const auto later = dir.filePath(QStringLiteral("later-%1.bin").arg(portable));
		const auto laterAlias = later + ".alias";
		expect(set.add(later) && set.check(alias, &protectedPath) && protectedPath && put(later, original) &&
				   modelTestHardLink(later, laterAlias) && set.add(later) && set.check(laterAlias, &protectedPath) && protectedPath,
			   "re-adding a previously missing source refreshes captured identities", error);
	}
	const auto folder = dir.filePath("folder");
	expect(QDir().mkpath(folder + "/nested"), "prepare protected source folder");
	const auto child = folder + "/nested/child.bin", childAlias = dir.filePath("folder-child-alias.zip");
	expect(put(child, original) && modelTestHardLink(child, childAlias), "prepare outside alias of a protected descendant");
	for (bool portable : {false, true})
	{
		PackageInputProtectionSet set(100, 65536, &error, {}, {}, portable);
		bool found = false;
		expect(set.add(folder) && set.finish() && set.check(childAlias, &found) && found,
			   "folder protection includes hard links outside the input root", error);
		expect(set.check(folder + "/future/new.bin", &found) && found && set.check(dir.filePath("folder-sibling/new.bin"), &found) &&
				   !found,
			   "future descendants remain protected without confusing similar path prefixes", error);
	}
	const auto crowded = dir.filePath("crowded");
	expect(QDir().mkpath(crowded), "prepare bounded identity indexing fixture");
	bool prepared = true;
	for (int i = 0; i < 512; ++i)
		prepared &= put(QDir(crowded).filePath(QStringLiteral("item-%1").arg(i)), original);
	expect(prepared, "write independent indexed fixtures");
	const auto crowdedAlias = dir.filePath("crowded-alias.zip");
	expect(modelTestHardLink(QDir(crowded).filePath("item-0"), crowdedAlias), "prepare indexed matching target");
	for (bool portable : {false, true})
	{
		PackageInputProtectionSet limited(2, 65536, &error, {}, {}, portable);
		bool found = true;
		expect(limited.add(crowded) && !limited.check(crowdedAlias, &found) && found && error.contains("limit") && limited.protects(copy),
			   "incomplete path-bounded identity scans fail closed and preserve the query output", error);
		PackageInputProtectionSet bytes(1000, 1024, &error, {}, {}, portable);
		expect(bytes.add(crowded) && !bytes.check(crowdedAlias, &found) && error.contains("metadata bytes"),
			   "identity scans obey their metadata byte budget", error);
		bool pulse = false, reached = false;
		PackageReadControl control;
		control.isCancelled = [&] { return std::exchange(pulse, false); };
		control.progress = [&](const QString &phase, qint64 done, qint64)
		{
			if (phase == "Checking package source protections" && done >= 256)
				pulse = reached = true;
		};
		PackageInputProtectionSet cancelled(1000, 1048576, &error, control, {}, portable);
		expect(cancelled.add(crowded) && !cancelled.check(crowdedAlias, &found) && reached && found && error.contains("cancelled") &&
				   !cancelled.check(copy, &found),
			   "one-shot cancellation during directory identity capture remains latched", error);
	}
	{
		int polls = 0;
		PackageReadControl control;
		control.isCancelled = [&]
		{
			++polls;
			return false;
		};
		PackageInputProtectionSet indexed(1000, 1048576, &error, control);
		bool found = false;
		expect(indexed.add(crowded) && indexed.check(crowdedAlias, &found) && found, "capture native identity index once", error);
		const int before = polls;
		QElapsedTimer time;
		time.start();
		bool matched = true;
		for (int i = 0; i < 256; ++i)
			matched &= indexed.check(crowdedAlias, &found) && found;
		expect(matched, "repeated native indexed lookup protects the alias", error);
		expect(polls - before < 256 * 100, "native matches do not rescan all protected directory entries");
		std::cout << "512 indexed files / 256 alias queries: " << time.elapsed() << " ms; checkpoints: " << polls - before << '\n';
	}
	PackageStagingModel model;
	expect(model.createEmpty(PackageArchiveFormat::Zip, {}, &error) && model.addFile(input, "payload.bin", &error),
		   "stage protected source through shared package services", error);
	const auto draft = dir.filePath("snapshot.vibepackage");
	expect(PackageDraft::save(draft, &model, false, &error), "persist source provenance in a portable draft", error);
	PackageStagingModel restored;
	expect(PackageDraft::load(draft, &restored, &error), "restore portable draft", error);
	for (bool dry : {true, false})
	{
		PackageWriteRequest request;
		request.destinationPath = alias;
		request.format = PackageArchiveFormat::Zip;
		request.allowOverwrite = true;
		request.dryRun = dry;
		const auto result = restored.writeArchive(request);
		expect(!result.succeeded() && modelTestReadFile(input) == original && modelTestReadFile(alias) == original,
			   "draft-backed archive publication protects original-source aliases in dry and real writes",
			   result.blockedMessages.join(';'));
		const auto extraction = dir.filePath(QStringLiteral("extraction-%1").arg(dry));
		expect(QDir().mkpath(extraction) && modelTestHardLink(input, QDir(extraction).filePath("payload.bin")),
			   "prepare linked extraction destination");
		PackageExtractionRequest extract;
		extract.extractAll = true;
		extract.targetDirectory = extraction;
		extract.overwriteExisting = true;
		extract.dryRun = dry;
		const auto report = extractPackageEntries(PackageStagingArchive(restored), extract);
		expect(!report.succeeded() && modelTestReadFile(QDir(extraction).filePath("payload.bin")) == original,
			   "draft extraction refuses protected aliases even with overwrite");
	}
	expect(modelTestReadFile(input) == original && modelTestReadFile(child) == original, "all source payloads remain unchanged");
	std::cout << checks << " identity checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
