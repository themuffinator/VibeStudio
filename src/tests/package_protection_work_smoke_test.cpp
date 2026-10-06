#include "core/package_archive.h"
#include "core/package_staging.h"
#include "core/package_protection_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <filesystem>
#include <memory>
#include <utility>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& details = {})
{
	if (!value) { std::cerr << message << ": " << details.toStdString() << '\n'; }
	return value;
}
class Reader final : public PackageArchiveReader {
public:
	QStringList inputs;
	mutable int enumerations = 0, visited = 0, reads = 0;
	mutable bool pulse = false;
	bool cancelDuringEnumeration = false;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Zip; }
	QString sourcePath() const override { return {}; }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override
	{
		QVector<PackageEntry> result;
		for (int index = 0; index < 8; ++index) {
			PackageEntry entry; entry.virtualPath = QStringLiteral("file-%1.bin").arg(index); entry.sizeBytes = 4; result.append(entry);
		}
		return result;
	}
	bool readEntryBytes(const QString&, QByteArray* bytes, QString*, qint64 maximum) const override
	{
		++reads; if (bytes) { *bytes = maximum < 0 ? QByteArray("data") : QByteArray("data").left(maximum); } return true;
	}
	bool streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString*, const std::function<bool()>& cancelled) const override
	{
		++reads; return index >= 0 && index < 8 && !(cancelled && cancelled()) && sink(QByteArrayView("data", 4));
	}
	bool visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor, QString* error, const PackageReadControl& control) const override
	{
		++enumerations;
		if (!PackageArchiveReader::visitProtectedInputPaths(visitor, error, control)) { return false; }
		for (const auto& input : inputs) {
			++visited;
			if (!visitor(input)) { return false; }
			if (cancelDuringEnumeration && visited == 1) { pulse = true; }
			if (control.isCancelled && control.isCancelled()) { if (error) { *error = "cancelled in synthetic protection provider"; } return false; }
		}
		return true;
	}
};
std::shared_ptr<Reader> reader(const QDir& root)
{
	auto result = std::make_shared<Reader>();
	for (int index = 0; index < 20; ++index) { result->inputs.append(root.filePath(QStringLiteral("input-%1").arg(index))); }
	return result;
}
bool extraction(const QDir& root)
{
	bool ok = true; auto source = reader(root);
	PackageExtractionRequest request; request.extractAll = true; request.dryRun = true; request.targetDirectory = root.filePath("extracted");
	const auto report = extractPackageEntries(*source, request);
	ok &= expect(report.succeeded() && report.processedCount == 8 && source->enumerations == 1 && source->visited == source->inputs.size()
		&& source->reads == 0 && !QFileInfo::exists(request.targetDirectory), "Extraction prepares source protections once for the complete operation.",
		QStringLiteral("enumerations=%1, roots visited=%2").arg(source->enumerations).arg(source->visited));
	source = reader(root); source->cancelDuringEnumeration = true;
	request.control.isCancelled = [&] { return std::exchange(source->pulse, false); };
	const auto cancelled = extractPackageEntries(*source, request);
	ok &= expect(cancelled.cancelled && !cancelled.succeeded() && source->visited <= 2 && source->reads == 0 && !QFileInfo::exists(request.targetDirectory),
		"Extraction latches a one-shot cancellation inside source protection enumeration.", QStringLiteral("roots visited=%1").arg(source->visited));
	return ok;
}
bool exporting(const QDir& root)
{
	bool ok = true; QString error; auto source = reader(root); PackageArchive archive; PackageStagingModel model;
	ok &= expect(archive.loadSnapshot(source, &error) && model.loadBaseArchive(archive, &error), "Prepare an owned export provider.", error);
	source->enumerations = source->visited = 0;
	PackageWriteRequest request; request.destinationPath = root.filePath("output.zip"); request.format = PackageArchiveFormat::Zip;
	request.dryRun = true; request.allowOverwrite = true; request.writeManifest = true;
	const auto report = model.writeArchive(request);
	ok &= expect(report.succeeded() && source->enumerations == 1 && source->visited == source->inputs.size() && !QFileInfo::exists(request.destinationPath),
		"Export shares one protection preparation across output, lock, backup and manifest checks.",
		QStringLiteral("enumerations=%1, roots visited=%2; %3").arg(source->enumerations).arg(source->visited).arg(report.blockedMessages.join(';')));
	source->enumerations = source->visited = source->reads = 0; source->cancelDuringEnumeration = true;
	request.isCancelled = [&] { return std::exchange(source->pulse, false); };
	const auto cancelled = model.writeArchive(request);
	ok &= expect(cancelled.cancelled && !cancelled.succeeded() && source->visited <= 2 && source->reads == 0 && !QFileInfo::exists(request.destinationPath),
		"Export latches a one-shot cancellation during source protection preparation before payload reads.",
		QStringLiteral("roots visited=%1, reads=%2").arg(source->visited).arg(source->reads));
	return ok;
}
bool indexedMatching(const QDir& root)
{
	bool ok = true; QString error; qint64 matchingSteps = 0;
	PackageReadControl control;
	control.progress = [&](const QString& phase, qint64 done, qint64) { if (phase == "Checking package source protections") { matchingSteps = done; } };
	PackageInputProtectionSet inputs(PackageInputProtectionSet::pathCeiling, PackageInputProtectionSet::byteCeiling, &error, control);
	for (int index = 0; index < 20000; ++index) { ok &= inputs.add(root.filePath(QStringLiteral("protected-%1").arg(index)), false); }
	ok &= inputs.add(root.filePath("CaseProtected"), false) && inputs.finish();
#ifdef Q_OS_WIN
	ok &= inputs.add("/", false); // Foreign POSIX provenance must not protect this drive.
#else
	ok &= inputs.add("E:/", false); // Foreign drive roots must not become relative paths.
#endif
	bool protectedPath = false;
	ok &= expect(inputs.check(root.filePath("protected-123/future.bin"), &protectedPath) && protectedPath,
		"A disappeared root still protects its future descendants through the index.", error);
	ok &= expect(inputs.check(root.filePath("protected-123-sibling/file"), &protectedPath) && !protectedPath,
		"Protection requires a complete path component, not a shared name prefix.", error);
	ok &= inputs.check(root.filePath("caseprotected/file"), &protectedPath);
#ifdef Q_OS_WIN
	ok &= expect(protectedPath, "Native Windows protection keys ignore case.", error);
#else
	ok &= expect(!protectedPath, "Native POSIX protection keys retain case.", error);
#endif
	QElapsedTimer elapsed; elapsed.start();
	for (int index = 0; index < 512; ++index) {
		ok &= inputs.check(root.filePath(QStringLiteral("unprotected/file-%1.bin").arg(index)), &protectedPath) && !protectedPath;
	}
	ok &= expect(matchingSteps > 0 && matchingSteps < 50000, "Matching work follows output depth rather than rescanning 20,000 roots per output.", error);
	std::cout << "20,000 roots / 512 output checks: " << elapsed.elapsed() << " ms; metadata checkpoints: " << matchingSteps << '\n';
	return ok;
}
bool matchingCancellation(const QDir& root)
{
	bool ok = true; QString error; bool pulse = false, reached = false; PackageReadControl control;
	control.progress = [&](const QString& phase, qint64, qint64) { if (phase == "Checking package source protections") { pulse = reached = true; } };
	control.isCancelled = [&] { return std::exchange(pulse, false); };
	PackageInputProtectionSet inputs(20, 65536, &error, control);
	ok &= inputs.add(root.filePath("input"), false) && inputs.finish(); bool protectedPath = true;
	ok &= expect(!inputs.check(root.filePath("output"), &protectedPath) && reached && error.contains("cancelled") && protectedPath
		&& !inputs.check(root.filePath("another-output"), &protectedPath), "A cancelled match preserves its output argument and remains failed closed.", error);
	PackageInputProtectionSet invalid(20, 65536, &error);
	ok &= expect(!invalid.check(root.filePath(QString(32769, 'x')), &protectedPath) && error.contains("invalid or too long") && protectedPath,
		"Overlong output paths refuse before matching or filesystem traversal.", error);
	auto source = reader(root); PackageExtractionRequest request; request.extractAll = true; request.dryRun = true;
	request.targetDirectory = root.filePath("cancel-matching"); pulse = reached = false; request.control = control;
	const auto report = extractPackageEntries(*source, request);
	ok &= expect(report.cancelled && reached && report.errorCount == 0 && source->reads == 0 && !QFileInfo::exists(request.targetDirectory),
		"Matching cancellation is reported as cancellation rather than a damaged output, without creating the extraction root.", report.warnings.join(';'));
	return ok;
}
bool aliases(const QDir& root)
{
	bool ok = true; QString error; const auto target = root.filePath("alias-target"), alias = root.filePath("source-alias");
	ok &= QDir().mkpath(target);
	const auto sentinel = QDir(target).filePath("sentinel");
	{ QFile file(sentinel); ok &= file.open(QIODevice::WriteOnly) && file.write("original") == 8; }
	std::error_code status;
	std::filesystem::create_directory_symlink(std::filesystem::path(target.toStdU16String()), std::filesystem::path(alias.toStdU16String()), status);
	bool linked = !status;
#ifdef Q_OS_WIN
	if (!linked) {
		const auto quote = [](QString value) { return value.replace('\'', "''"); };
		QProcess junction; junction.setWorkingDirectory(root.absolutePath());
		junction.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command",
			QStringLiteral("New-Item -ItemType Junction -Path '%1' -Target '%2' -ErrorAction Stop | Out-Null").arg(quote(alias), quote(target))});
		linked = junction.waitForFinished(10000) && junction.exitStatus() == QProcess::NormalExit && junction.exitCode() == 0 && QFileInfo(alias).isJunction();
	}
#endif
	if (!expect(linked, "Exercise a real source alias confined to the synthetic test directory.")) { return false; }
	PackageInputProtectionSet inputs(20, 65536, &error);
	ok &= inputs.add(alias) && inputs.finish(); bool protectedPath = false;
	ok &= expect(inputs.check(QDir(target).filePath("new.bin"), &protectedPath) && protectedPath
		&& inputs.check(QDir(alias).filePath("new.bin"), &protectedPath) && protectedPath,
		"A captured source alias protects both lexical and physical destinations, including nonexistent suffixes.", error);
	PackageInputProtectionSet physical(20, 65536, &error); ok &= physical.add(target) && physical.finish();
	ok &= expect(physical.check(QDir(alias).filePath("new.bin"), &protectedPath) && protectedPath,
		"Output alias resolution uses the same junction-aware path semantics as existing containment checks.", error);
	int checks = 0; PackageReadControl cancelled; cancelled.isCancelled = [&] { return ++checks >= 3; };
	ok &= expect(packageResolvedAbsolutePath(QDir(alias).filePath("missing/child"), cancelled).isEmpty() && checks == 3,
		"Missing-prefix traversal responds to cancellation.");
#ifdef Q_OS_WIN
	checks = 0; cancelled.isCancelled = [&] { return ++checks >= 4; };
	ok &= expect(packageResolvedAbsolutePath(alias, cancelled).isEmpty() && checks == 4,
		"Existing Windows path segment traversal responds to cancellation before it can finish resolving a junction.");
#endif
	const QFileInfo linkInfo(alias);
	if (linkInfo.isJunction()) { ok &= QDir().rmdir(alias); }
	else if (linkInfo.isSymLink()) { ok &= QFile::remove(alias); }
	else { ok = false; }
	QFile file(sentinel);
	ok &= expect(!QFileInfo::exists(alias) && file.open(QIODevice::ReadOnly) && file.readAll() == "original",
		"Removing the verified fixture alias preserves its synthetic target.");
	return ok;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	QDir root(temporary.path()); bool ok = extraction(root); ok &= exporting(root);
	ok &= indexedMatching(root); ok &= matchingCancellation(root); ok &= aliases(root); return ok ? 0 : 1;
}
