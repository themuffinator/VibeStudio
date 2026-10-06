#include "core/package_publication.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QString hash(const QByteArray& bytes)
{
	return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString journalIn(const QString& root)
{
	const auto files = QDir(root).entryList({QStringLiteral(".vibestudio-package-*.payload.json")}, QDir::Files | QDir::Hidden);
	return files.size() == 1 ? QDir(root).filePath(files.first()) : QString();
}
QStringList transactionFiles(const QString& root)
{
	return QDir(root).entryList({QStringLiteral(".vibestudio-package-*")}, QDir::Files | QDir::Hidden);
}
PackagePublicationResult publish(const QString& path, bool overwrite, const QByteArray& bytes, PackagePublication::Checkpoint hook = {})
{
	PackagePublicationOptions options; options.destinationPath = path; options.allowOverwrite = overwrite;
	PackagePublication publication(options, hook);
	QString error;
	if (!publication.begin(&error)) { PackagePublicationResult result; result.error = error; return result; }
	if (publication.device()->write(bytes) != bytes.size()) { PackagePublicationResult result; result.error = QStringLiteral("fixture write failed"); return result; }
	return publication.commit(bytes.size(), hash(bytes));
}
int crashChild(const QString& root, const QString& phase)
{
	const auto crashAt = phase == QStringLiteral("before") ? PackagePublicationStep::CommitOutput : PackagePublicationStep::OutputCommitted;
	const auto result = publish(QDir(root).filePath(QStringLiteral("output.pak")), true, "new!", [crashAt](auto step, QString*) {
		if (step == crashAt) { std::_Exit(99); }
		return true;
	});
	std::cerr << result.error.toStdString();
	return 2;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const auto args = app.arguments();
	if (args.size() == 4 && args[1] == QStringLiteral("--crash-child")) { return crashChild(args[2], args[3]); }
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	bool ok = true;
	const auto folder = [&](const QString& name) { const auto path = QDir(temp.path()).filePath(name); QDir().mkpath(path); return path; };
	const QString root = folder(QStringLiteral("normal"));
	const QString destination = QDir(root).filePath(QStringLiteral("package.pak"));
	auto result = publish(destination, false, "initial");
	ok &= expect(result.committed && result.error.isEmpty() && result.backupPath.isEmpty() && read(destination) == "initial", "new output is fully published");
	ok &= expect(transactionFiles(root).isEmpty(), "successful new output removes journal");
	ok &= expect(write(destination + ".bak", "older"), "create previous backup");
	result = publish(destination, true, "replacement");
	ok &= expect(result.committed && result.error.isEmpty() && result.warnings.isEmpty() && read(destination) == "replacement" && read(result.backupPath) == "initial", "atomic replacement preserves exact original");
	ok &= expect(transactionFiles(root).isEmpty(), "successful overwrite cleans transaction files");
	result = publish(destination, false, "forbidden");
	ok &= expect(!result.committed && read(destination) == "replacement", "overwrite requires explicit opt-in");
	{
		const auto path = QDir(folder(QStringLiteral("reviewed-destination"))).filePath(QStringLiteral("output.pk3"));
		PackagePublicationOptions options; options.destinationPath = path; options.allowOverwrite = true;
		options.expectedDestinationSha256 = QString();
		QString error;
		{ PackagePublication absent(options); ok &= expect(absent.begin(&error), "reviewed absent destination can be reserved"); }
		ok &= expect(write(path, "reviewed"), "create reviewed package");
		{ PackagePublication appeared(options); ok &= expect(!appeared.begin(&error), "expected absence blocks newly created destination"); }
		options.expectedDestinationSha256 = hash("reviewed");
		{ PackagePublication unchanged(options); ok &= expect(unchanged.begin(&error), "reviewed digest accepts unchanged destination"); }
		ok &= expect(write(path, "modified"), "same-size package modification after review");
		{ PackagePublication changed(options); ok &= expect(!changed.begin(&error) && read(path) == "modified", "reviewed digest blocks changed destination before writing"); }
		options.expectedDestinationSha256 = QStringLiteral("invalid");
		{ PackagePublication malformed(options); ok &= expect(!malformed.begin(&error), "invalid expected digest fails closed"); }
	}

	for (auto phase : {PackagePublicationStep::VerifyOutput, PackagePublicationStep::CopyOriginal, PackagePublicationStep::WriteJournal,
		PackagePublicationStep::CommitOutput, PackagePublicationStep::OutputCommitted, PackagePublicationStep::CommitBackup, PackagePublicationStep::Cleanup}) {
		const QString caseRoot = folder(QStringLiteral("failure-%1").arg(static_cast<int>(phase)));
		const QString path = QDir(caseRoot).filePath(QStringLiteral("output.pak"));
		ok &= expect(write(path, "old!") && write(path + ".bak", "older"), "create failure fixtures");
		const bool afterCommit = phase == PackagePublicationStep::OutputCommitted || phase == PackagePublicationStep::CommitBackup || phase == PackagePublicationStep::Cleanup;
		result = publish(path, true, "new!", [=](auto step, QString* error) {
			if (step == phase) { *error = QStringLiteral("injected failure"); return false; }
			return true;
		});
		ok &= expect(result.committed == afterCommit && read(path) == (afterCommit ? "new!" : "old!"), "failure boundary never removes or partially changes output");
		if (!afterCommit) { ok &= expect(read(path + ".bak") == "older", "precommit failure preserves previous backup"); }
		if (afterCommit) {
			const QString journal = journalIn(caseRoot);
			auto recovery = recoverPackagePublication(journal);
			ok &= expect(!journal.isEmpty() && recovery.error.isEmpty() && recovery.canFinish && recovery.state == "replacement", "postcommit failure exposes recoverable journal");
			recovery = recoverPackagePublication(journal, true);
			if (!recovery.error.isEmpty()) { std::cerr << recovery.error.toStdString() << '\n'; }
			ok &= expect(recovery.finished && read(path + ".bak") == "old!" && transactionFiles(caseRoot).isEmpty(), "finish is safe before and after backup publication");
		} else if (phase == PackagePublicationStep::CommitOutput) {
			const auto recovery = recoverPackagePublication(journalIn(caseRoot), true);
			ok &= expect(!recovery.finished && recovery.state == "original" && read(path) == "old!", "recovery never silently installs an uncommitted replacement");
		} else { ok &= expect(transactionFiles(caseRoot).isEmpty(), "early failure removes disposable transaction files"); }
	}

	{
		const QString caseRoot = folder(QStringLiteral("races"));
		const QString path = QDir(caseRoot).filePath(QStringLiteral("output.pak"));
		PackagePublicationOptions options; options.destinationPath = path;
		PackagePublication first(options);
		QString error;
		ok &= expect(first.begin(&error), "reserve output for race test");
		PackagePublication second(options);
		ok &= expect(!second.begin(&error), "cooperating saves cannot publish concurrently");
		ok &= expect(first.device()->write("new!") == 4 && write(path, "external"), "create destination during save");
		result = first.commit(4, hash("new!"));
		ok &= expect(!result.committed && read(path) == "external", "late destination creation is never overwritten");
	}
	{
		const QString caseRoot = folder(QStringLiteral("same-metadata"));
		const QString path = QDir(caseRoot).filePath(QStringLiteral("output.pak"));
		ok &= expect(write(path, "old!") && write(path + ".bak", "older"), "create same-metadata fixture");
		const auto time = QFileInfo(path).lastModified();
		result = publish(path, true, "new!", [&](auto step, QString*) {
			if (step == PackagePublicationStep::CopyOriginal) {
				ok &= expect(write(path, "evil"), "mutate output without changing size");
				QFile file(path); ok &= expect(file.open(QIODevice::ReadWrite) && file.setFileTime(time, QFileDevice::FileModificationTime), "restore output modification time");
			}
			return true;
		});
		ok &= expect(!result.committed && read(path) == "evil" && read(path + ".bak") == "older", "content identity detects equal-size equal-time destination mutation");
	}
	{
		const QString caseRoot = folder(QStringLiteral("late-backup"));
		const QString path = QDir(caseRoot).filePath(QStringLiteral("output.pak"));
		ok &= expect(write(path, "old!") && write(path + ".bak", "older"), "create backup-race fixture");
		result = publish(path, true, "new!", [&](auto step, QString*) {
			if (step == PackagePublicationStep::CommitBackup) { ok &= expect(write(path + ".bak", "external backup"), "change backup concurrently"); }
			return true;
		});
		ok &= expect(result.committed && !result.warnings.isEmpty() && read(result.backupPath) == "old!" && read(path + ".bak") == "external backup", "backup race retains verified original and reports committed output");
		const auto recovered = recoverPackagePublication(journalIn(caseRoot), true);
		ok &= expect(!recovered.finished && read(path + ".bak") == "external backup", "recovery refuses a changed backup");
	}
	{
		const QString path = QDir(folder(QStringLiteral("verify"))).filePath(QStringLiteral("output.pak"));
		PackagePublicationOptions options; options.destinationPath = path;
		PackagePublication publication(options);
		QString error;
		ok &= expect(publication.begin(&error) && publication.device()->write("actual") == 6, "create integrity failure fixture");
		result = publication.commit(6, hash("wrong!"));
		ok &= expect(!result.committed && !QFileInfo::exists(path) && result.recoveryPaths.isEmpty(), "incorrect writer digest cannot publish");
	}
	{
		const QString caseRoot = folder(QStringLiteral("later-edit"));
		const QString path = QDir(caseRoot).filePath(QStringLiteral("output.pak"));
		ok &= expect(write(path, "old!"), "create later-edit fixture");
		result = publish(path, true, "new!", [&](auto step, QString*) {
			if (step == PackagePublicationStep::OutputCommitted) { ok &= expect(write(path, "later edit"), "edit after publication"); return false; }
			return true;
		});
		const auto recovery = recoverPackagePublication(journalIn(caseRoot), true);
		ok &= expect(result.committed && recovery.state == "changed" && !recovery.finished && read(path) == "later edit", "recovery never overwrites a postcommit user edit");
	}
	{
		const QString path = QDir(folder(QStringLiteral("cancel"))).filePath(QStringLiteral("output.pak"));
		int polls = 0; bool enabled = false;
		PackagePublicationOptions options; options.destinationPath = path; options.isCancelled = [&] { return enabled && ++polls >= 5; };
		PackagePublication publication(options);
		QString error;
		const QByteArray payload(1024 * 1024, 'x');
		ok &= expect(publication.begin(&error) && publication.device()->write(payload) == payload.size(), "create cancellable verification fixture");
		enabled = true;
		result = publication.commit(payload.size(), hash(payload));
		ok &= expect(result.cancelled && !result.committed && !QFileInfo::exists(path), "large-file verification cancels before publication");
	}
	{
		const QString caseRoot = folder(QStringLiteral("external-backup"));
		const QString path = QDir(caseRoot).filePath(QStringLiteral("output.pak"));
		const QString backup = QDir(folder(QStringLiteral("separate-backups"))).filePath(QStringLiteral("previous.pak"));
		ok &= expect(write(path, "old!") && write(backup, "older"), "create external backup fixture");
		{
			PackagePublicationOptions options; options.destinationPath = path; options.allowOverwrite = true; options.backupPath = backup;
			PackagePublication publication(options, [](auto step, QString*) { return step != PackagePublicationStep::OutputCommitted; });
			QString error;
			ok &= expect(publication.begin(&error) && publication.device()->write("new!") == 4 && publication.commit(4, hash("new!")).committed, "interrupt before external backup publication");
		}
		const QString journal = journalIn(caseRoot);
		const auto inspected = recoverPackagePublication(journal);
		ok &= expect(inspected.requiresBackupConfirmation && !inspected.canFinish, "journal alone cannot authorize an external write");
		ok &= expect(!recoverPackagePublication(journal, true).finished && read(backup) == "older", "external backup recovery requires explicit destination");
		ok &= expect(!recoverPackagePublication(journal, true, destination).finished, "incorrect explicit backup path is refused");
		ok &= expect(recoverPackagePublication(journal, true, backup).finished && read(backup) == "old!", "explicit recorded backup path authorizes external completion");
	}
#ifdef Q_OS_WIN
	// A real Windows sharing violation forces QSaveFile's native commit to
	// fail after all copies have succeeded. POSIX permits renaming open files;
	// the portable checkpoint tests above cover the equivalent failure boundary.
	{
		const QString caseRoot = folder(QStringLiteral("native-commit-failure"));
		const QString path = QDir(caseRoot).filePath(QStringLiteral("output.pak"));
		ok &= expect(write(path, "old!") && write(path + ".bak", "older"), "create native commit failure fixture");
		const HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(path.utf16()), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		ok &= expect(held != INVALID_HANDLE_VALUE, "hold output without delete sharing");
		if (held != INVALID_HANDLE_VALUE) {
			result = publish(path, true, "new!");
			CloseHandle(held);
			ok &= expect(!result.committed && !result.error.isEmpty() && read(path) == "old!" && read(path + ".bak") == "older", "native atomic-commit failure preserves output and previous backup");
			ok &= expect(recoverPackagePublication(journalIn(caseRoot)).state == "original", "native commit failure retains inspectable recovery versions");
		}
	}
#endif
	for (const QString& phase : {QStringLiteral("before"), QStringLiteral("after")}) {
		const QString caseRoot = folder(QStringLiteral("crash-%1").arg(phase));
		const QString path = QDir(caseRoot).filePath(QStringLiteral("output.pak"));
		ok &= expect(write(path, "old!") && write(path + ".bak", "older"), "create process interruption fixture");
		QProcess child;
		child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--crash-child"), caseRoot, phase});
		ok &= expect(child.waitForStarted() && child.waitForFinished(15000) && child.exitCode() == 99, "terminate child at commit boundary without destructors");
		const QString journal = journalIn(caseRoot);
		auto recovery = recoverPackagePublication(journal);
		ok &= expect(!journal.isEmpty() && recovery.error.isEmpty() && read(recovery.originalPath) == "old!", "process interruption preserves a verified original and readable journal");
		if (phase == "after") {
			recovery = recoverPackagePublication(journal, true);
			ok &= expect(recovery.finished && read(path) == "new!" && read(path + ".bak") == "old!", "recovery handles stale process lock and completes interrupted publication");
		} else {
			ok &= expect(recovery.state == "original" && read(path) == "old!" && read(recovery.replacementPath) == "new!", "precommit process interruption leaves both versions available");
			QJsonObject malformed = QJsonDocument::fromJson(read(journal)).object();
			QJsonObject alias = malformed;
			alias.insert(QStringLiteral("destination"), recovery.originalPath);
			alias.insert(QStringLiteral("after"), alias.value(QStringLiteral("before")));
			ok &= expect(write(journal, QJsonDocument(alias).toJson()), "create aliased output journal fixture");
			ok &= expect(recoverPackagePublication(journal, true).state == "invalid" && read(recovery.originalPath) == "old!", "recovery cannot treat its destination as disposable original-copy storage");
			malformed.insert(QStringLiteral("original"), destination);
			ok &= expect(write(journal, QJsonDocument(malformed).toJson()), "create unsafe journal fixture");
			const auto invalid = recoverPackagePublication(journal, true);
			ok &= expect(invalid.state == "invalid" && !invalid.error.isEmpty() && read(destination) == "replacement", "journal cannot redirect recovery cleanup onto unrelated files");
		}
	}
	return ok ? 0 : 1;
}
