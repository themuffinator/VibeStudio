#include "core/package_publication.h"
#include "core/package_storage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>

#include <array>
#include <utility>

namespace vibestudio {
namespace {
using Cancel = std::function<bool()>;
#ifdef Q_OS_WIN
constexpr auto pathCaseSensitivity = Qt::CaseInsensitive;
#else
constexpr auto pathCaseSensitivity = Qt::CaseSensitive;
#endif

struct Identity {
	bool exists = false;
	qint64 size = 0;
	QString hash;
	bool operator==(const Identity&) const = default;
};

QString absolutePath(const QString& path)
{
	const QFileInfo info(path);
	const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
	return QDir::cleanPath(QDir(parent.isEmpty() ? info.absolutePath() : parent).filePath(info.fileName()));
}

bool cancelled(const Cancel& cancel, QString* error)
{
	if (!cancel || !cancel()) { return false; }
	*error = QCoreApplication::translate("VibeStudioPackagePublication", "Package save cancelled before publication; the destination was left untouched.");
	return true;
}

// Both copying and hashing are bounded and cancellable within a large file.
bool transfer(QIODevice& input, QIODevice* output, Identity* identity, const Cancel& cancel, QString* error,
	const std::function<void(qint64, qint64)>& progress = {})
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	std::array<char, 65536> buffer;
	qint64 total = 0;
	if (progress) { progress(0, input.size()); }
	for (;;) {
		if (cancelled(cancel, error)) { return false; }
		const qint64 count = input.read(buffer.data(), buffer.size());
		if (count < 0) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to read package transaction bytes: %1").arg(input.errorString()); return false; }
		if (count == 0) { break; }
		hash.addData(QByteArrayView(buffer.data(), count));
		if (output && output->write(buffer.data(), count) != count) {
			*error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to write package transaction bytes: %1").arg(output->errorString()); return false;
		}
		total += count;
		if (progress) { progress(total, input.size()); }
	}
	*identity = {true, total, QString::fromLatin1(hash.result().toHex())};
	return true;
}

bool identify(const QString& path, Identity* identity, const Cancel& cancel, QString* error, const PackageReadControl* control = nullptr)
{
	const QFileInfo before(path);
	if (before.isSymLink() || (before.exists() && !before.isFile())) {
		*error = QCoreApplication::translate("VibeStudioPackagePublication", "Package transaction paths must be regular files, without symbolic links: %1").arg(path); return false;
	}
	if (!before.exists()) { *identity = {}; return !cancelled(cancel, error); }
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to read %1: %2").arg(path, file.errorString()); return false; }
	if (!transfer(file, nullptr, identity, cancel, error, [control, path](qint64 done, qint64 total) {
		if (control && control->progress) { control->progress(path, done, total); }
	})) { return false; }
	const QFileInfo after(path);
	if (!after.isFile() || after.isSymLink() || before.size() != after.size()
		|| before.lastModified() != after.lastModified() || identity->size != before.size()) {
		*error = QCoreApplication::translate("VibeStudioPackagePublication", "A file changed while its contents were being verified: %1").arg(path); return false;
	}
	return true;
}

bool matches(const QString& path, const Identity& expected, const Cancel& cancel, QString* error, const PackageReadControl* control = nullptr)
{
	Identity current;
	if (!identify(path, &current, cancel, error, control)) { return false; }
	if (current == expected) { return true; }
	*error = QCoreApplication::translate("VibeStudioPackagePublication", "The file changed during the save; nothing was replaced at %1.").arg(path);
	return false;
}

bool copyVerified(const QString& source, QIODevice& output, const Identity& expected, const Cancel& cancel, QString* error, const PackageReadControl* control = nullptr)
{
	QFile input(source);
	if (QFileInfo(source).isSymLink() || !input.open(QIODevice::ReadOnly)) {
		*error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to read the package transaction source: %1").arg(source); return false;
	}
	Identity copied;
	if (!transfer(input, &output, &copied, cancel, error, [control, source](qint64 done, qint64 total) {
		if (control && control->progress) { control->progress(source, done, total); }
	})) { return false; }
	if (copied != expected) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Package transaction bytes no longer match their recorded SHA-256: %1").arg(source); return false; }
	return true;
}

QString temporaryTemplate(const QString& destination)
{
	return QDir(QFileInfo(destination).absolutePath()).filePath(QStringLiteral(".vibestudio-package-XXXXXX.payload"));
}

// QTemporaryFile::rename is atomic-only and refuses an existing target. QFile's
// copy/delete fallback is deliberately not used. QSaveFile handles replacement
// with its direct-write fallback disabled. These APIs work on our Qt 6.8 floor.
bool atomicCopy(const QString& source, const QString& destination, const Identity& bytes,
	const Identity& destinationBefore, const Cancel& cancel, QString* error, const PackageReadControl* control = nullptr)
{
	if (!destinationBefore.exists) {
		QTemporaryFile output(temporaryTemplate(destination));
		if (!output.open()) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to reserve a file beside %1: %2").arg(destination, output.errorString()); return false; }
		if (!copyVerified(source, output, bytes, cancel, error, control) || !output.flush()) {
			if (error->isEmpty()) { *error = output.errorString(); } return false;
		}
		if (!matches(destination, destinationBefore, cancel, error, control)) { return false; }
		if (!output.rename(destination)) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to publish %1 without overwriting another file: %2").arg(destination, output.errorString()); return false; }
		output.setAutoRemove(false);
		return true;
	}
	QSaveFile output(destination);
	output.setDirectWriteFallback(false);
	if (!output.open(QIODevice::WriteOnly)) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to prepare atomic replacement of %1: %2").arg(destination, output.errorString()); return false; }
	if (!copyVerified(source, output, bytes, cancel, error, control) || !matches(destination, destinationBefore, cancel, error, control)) { return false; }
	if (!output.commit()) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to atomically replace %1: %2").arg(destination, output.errorString()); return false; }
	return true;
}

QJsonObject identityJson(const Identity& value)
{
	return {{QStringLiteral("exists"), value.exists}, {QStringLiteral("sizeBytes"), QString::number(value.size)}, {QStringLiteral("sha256"), value.hash}};
}

bool parseIdentity(const QJsonValue& value, Identity* result)
{
	const auto object = value.toObject();
	if (!object.value(QStringLiteral("exists")).isBool()) { return false; }
	bool sizeOk = false;
	result->exists = object.value(QStringLiteral("exists")).toBool();
	result->size = object.value(QStringLiteral("sizeBytes")).toString().toLongLong(&sizeOk);
	result->hash = object.value(QStringLiteral("sha256")).toString();
	static const QRegularExpression sha(QStringLiteral("^[a-f0-9]{64}$"));
	return sizeOk && result->size >= 0 && (result->exists ? sha.match(result->hash).hasMatch() : result->size == 0 && result->hash.isEmpty());
}

struct Journal {
	QString path;
	QString destination;
	QString replacement;
	QString original;
	QString backup;
	Identity before;
	Identity after;
	Identity backupBefore;
	Identity journalIdentity;
};

QJsonObject journalJson(const Journal& journal)
{
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("kind"), QStringLiteral("vibestudio-package-publication")},
		{QStringLiteral("destination"), journal.destination}, {QStringLiteral("replacement"), journal.replacement},
		{QStringLiteral("original"), journal.original}, {QStringLiteral("backup"), journal.backup},
		{QStringLiteral("before"), identityJson(journal.before)}, {QStringLiteral("after"), identityJson(journal.after)},
		{QStringLiteral("backupBefore"), identityJson(journal.backupBefore)}};
}

bool writeJournal(Journal* journal, QString* error)
{
	const QByteArray bytes = QJsonDocument(journalJson(*journal)).toJson();
	QFile file(journal->path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to reserve package recovery journal: %1").arg(file.errorString()); return false; }
	if (file.write(bytes) != bytes.size() || !file.flush()) {
		*error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to write package recovery journal: %1").arg(file.errorString()); file.remove(); return false;
	}
	journal->journalIdentity = {true, bytes.size(), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())};
	return true;
}

bool readJournal(const QString& path, Journal* journal, QString* error)
{
	QFile input(path);
	if (!safePackageStoragePath(path) || !QFileInfo(path).isFile() || !input.open(QIODevice::ReadOnly) || input.size() > 65536) {
		*error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to read a regular package recovery journal (maximum 64 KiB)."); return false;
	}
	const QByteArray bytes = input.read(65537);
	if (bytes.size() > 65536 || input.error() != QFileDevice::NoError) {
		*error = QCoreApplication::translate("VibeStudioPackagePublication", "The recovery journal exceeded its read limit or could not be read completely."); return false;
	}
	QJsonParseError parseError;
	const auto object = QJsonDocument::fromJson(bytes, &parseError).object();
	journal->path = absolutePath(path);
	journal->destination = object.value(QStringLiteral("destination")).toString();
	journal->replacement = object.value(QStringLiteral("replacement")).toString();
	journal->original = object.value(QStringLiteral("original")).toString();
	journal->backup = object.value(QStringLiteral("backup")).toString();
	journal->journalIdentity = {true, bytes.size(), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())};
	bool valid = parseError.error == QJsonParseError::NoError && object.value(QStringLiteral("schemaVersion")).toInt() == 1
		&& object.value(QStringLiteral("kind")).toString() == QStringLiteral("vibestudio-package-publication")
		&& parseIdentity(object.value(QStringLiteral("before")), &journal->before)
		&& parseIdentity(object.value(QStringLiteral("after")), &journal->after)
		&& parseIdentity(object.value(QStringLiteral("backupBefore")), &journal->backupBefore) && journal->after.exists;
	for (const QString& file : {journal->destination, journal->replacement}) {
		valid = valid && !file.isEmpty() && QDir::isAbsolutePath(file) && file == absolutePath(file);
	}
	const QString replacementName = QFileInfo(journal->replacement).fileName();
	static const QRegularExpression name(QStringLiteral("^\\.vibestudio-package-[A-Za-z0-9]{6,}\\.payload$"));
	valid = valid && name.match(replacementName).hasMatch() && journal->path == journal->replacement + QStringLiteral(".json")
		&& QFileInfo(journal->replacement).absolutePath() == QFileInfo(journal->destination).absolutePath()
		&& journal->destination != journal->replacement && journal->destination != journal->path;
	if (journal->before.exists) {
		valid = valid && journal->original == journal->replacement + QStringLiteral(".original")
			&& !journal->backup.isEmpty() && QDir::isAbsolutePath(journal->backup) && journal->backup == absolutePath(journal->backup)
			&& journal->backup != journal->destination && journal->backup != journal->replacement && journal->backup != journal->original && journal->backup != journal->path;
	} else { valid = valid && journal->original.isEmpty() && journal->backup.isEmpty() && !journal->backupBefore.exists; }
	// No transaction path may alias the output or its cooperative lock. This
	// check is also required for hand-edited/untrusted recovery journals.
	QStringList paths {journal->destination, journal->replacement, journal->path, journal->destination + QStringLiteral(".vibestudio-save.lock")};
	if (!journal->original.isEmpty()) { paths << journal->original; }
	if (!journal->backup.isEmpty()) { paths << journal->backup; }
	for (const auto& file : paths) { valid = valid && safePackageStoragePath(file); }
	for (qsizetype left = 0; left < paths.size(); ++left) {
		for (qsizetype right = left + 1; right < paths.size(); ++right) {
			valid = valid && paths[left].compare(paths[right], Qt::CaseInsensitive) != 0;
		}
	}
	if (!valid) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Invalid package recovery journal or unsafe transaction paths."); }
	return valid;
}

bool removeVerified(const QString& path, const Identity& expected, QString* error, const PackageReadControl* control = nullptr)
{
	Identity current;
	if (!identify(path, &current, control ? control->isCancelled : Cancel{}, error, control)) { return false; }
	if (!current.exists) { return true; }
	if (current != expected) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Recovery file changed; retained for inspection: %1").arg(path); return false; }
	if (control && cancelled(control->isCancelled, error)) { return false; }
	if (!safePackageStoragePath(path, error) || !QFile::remove(path)) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to remove a completed recovery file: %1").arg(path); return false; }
	return true;
}

bool finishBackup(const Journal& journal, QString* error, const PackageReadControl* control = nullptr)
{
	if (!journal.before.exists) { return true; }
	Identity current;
	if (!identify(journal.backup, &current, control ? control->isCancelled : Cancel{}, error, control)) { return false; }
	// A crash after backup publication but before cleanup is safely repeatable.
	if (current == journal.before) { return true; }
	if (current != journal.backupBefore) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "The backup changed since this save. The verified original is retained at %1.").arg(journal.original); return false; }
	return atomicCopy(journal.original, journal.backup, journal.before, journal.backupBefore, control ? control->isCancelled : Cancel{}, error, control);
}

bool cleanup(const Journal& journal, QString* error, const PackageReadControl* control = nullptr)
{
	if (!removeVerified(journal.replacement, journal.after, error, control)) { return false; }
	if (!journal.original.isEmpty() && !removeVerified(journal.original, journal.before, error, control)) { return false; }
	return removeVerified(journal.path, journal.journalIdentity, error, control);
}

QStringList recoveryPaths(const Journal& journal)
{
	QStringList result;
	for (const auto& path : {journal.path, journal.original, journal.replacement}) {
		if (!path.isEmpty() && QFileInfo::exists(path)) { result << path; }
	}
	return result;
}
} // namespace

class PackagePublication::State {
public:
	PackagePublicationOptions options;
	Checkpoint checkpoint;
	std::unique_ptr<QTemporaryFile> output;
	std::unique_ptr<QLockFile> lock;
	Journal journal;
	bool ready = false;
	bool completed = false;
	bool step(PackagePublicationStep phase, QString* error) { return !checkpoint || checkpoint(phase, error); }
};

PackagePublication::PackagePublication(PackagePublicationOptions options, Checkpoint checkpoint) : m_state(std::make_unique<State>())
{
	m_state->options = std::move(options);
	m_state->checkpoint = std::move(checkpoint);
}
PackagePublication::~PackagePublication() = default;
QIODevice* PackagePublication::device() { return m_state->ready && !m_state->completed ? m_state->output.get() : nullptr; }

bool PackagePublication::begin(QString* error)
{
	QString localError;
	if (!error) { error = &localError; }
	error->clear();
	auto& state = *m_state;
	if (state.output || state.options.destinationPath.trimmed().isEmpty()) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "A package publication needs one non-empty destination."); return false; }
	state.journal.destination = absolutePath(state.options.destinationPath);
	state.lock = std::make_unique<QLockFile>(state.journal.destination + QStringLiteral(".vibestudio-save.lock"));
	state.lock->setStaleLockTime(0);
	if (!state.lock->tryLock(0)) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Another package save or recovery owns this destination: %1").arg(state.journal.destination); return false; }
	if (!identify(state.journal.destination, &state.journal.before, state.options.isCancelled, error)) { return false; }
	if (state.options.expectedDestinationSha256) {
		const QString expected = *state.options.expectedDestinationSha256;
		const bool valid = expected.isEmpty() || QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(expected).hasMatch();
		if (!valid || (expected.isEmpty() ? state.journal.before.exists : !state.journal.before.exists || state.journal.before.hash != expected)) {
			*error = QCoreApplication::translate("VibeStudioPackagePublication", "The destination changed since review. Review it again before publishing.");
			return false;
		}
	}
	if (state.journal.before.exists) {
		if (!state.options.allowOverwrite) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "The destination already exists; overwrite must be explicitly enabled."); return false; }
		state.journal.backup = state.options.backupPath.isEmpty() ? state.journal.destination + QStringLiteral(".bak") : absolutePath(state.options.backupPath);
		if (state.journal.backup.compare(state.journal.destination, Qt::CaseInsensitive) == 0
			|| state.journal.backup.compare(state.journal.destination + QStringLiteral(".vibestudio-save.lock"), Qt::CaseInsensitive) == 0) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "The backup must be separate from the destination and save lock."); return false; }
		if (!QFileInfo(QFileInfo(state.journal.backup).absolutePath()).isDir()) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "The backup directory does not exist: %1").arg(state.journal.backup); return false; }
		if (!identify(state.journal.backup, &state.journal.backupBefore, state.options.isCancelled, error)) { return false; }
	}
	state.output = std::make_unique<QTemporaryFile>(temporaryTemplate(state.journal.destination));
	if (!state.output->open()) { *error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to reserve package output: %1").arg(state.output->errorString()); return false; }
	state.journal.replacement = state.output->fileName();
	state.journal.path = state.journal.replacement + QStringLiteral(".json");
	state.ready = true;
	return true;
}

PackagePublicationResult PackagePublication::commit(quint64 expectedBytes, const QString& expectedSha256)
{
	PackagePublicationResult result;
	auto& state = *m_state;
	auto& journal = state.journal;
	if (!state.ready || state.completed) { result.error = QCoreApplication::translate("VibeStudioPackagePublication", "Package publication is not ready or has already finished."); return result; }
	state.completed = true;
	QString error;
	const auto fail = [&]() {
		result.error = error.isEmpty() ? QCoreApplication::translate("VibeStudioPackagePublication", "Package publication failed before commit.") : error;
		result.cancelled = state.options.isCancelled && state.options.isCancelled();
		if (state.output && !state.output->autoRemove()) { result.recoveryPaths = recoveryPaths(journal); }
		return result;
	};
	if (cancelled(state.options.isCancelled, &error) || !state.step(PackagePublicationStep::VerifyOutput, &error)) { return fail(); }
	if (!state.output->flush()) { error = state.output->errorString(); return fail(); }
	if (!identify(journal.replacement, &journal.after, state.options.isCancelled, &error)) { return fail(); }
	if (journal.after.size < 0 || static_cast<quint64>(journal.after.size) != expectedBytes || journal.after.hash != expectedSha256) {
		error = QCoreApplication::translate("VibeStudioPackagePublication", "The completed package failed size or SHA-256 verification; the destination was left untouched."); return fail();
	}
	std::unique_ptr<QFile> original;
	if (journal.before.exists) {
		if (!state.step(PackagePublicationStep::CopyOriginal, &error)) { return fail(); }
		journal.original = journal.replacement + QStringLiteral(".original");
		original = std::make_unique<QFile>(journal.original);
		if (!original->open(QIODevice::WriteOnly | QIODevice::NewOnly)) { error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to reserve the original package recovery copy: %1").arg(original->errorString()); return fail(); }
		if (!copyVerified(journal.destination, *original, journal.before, state.options.isCancelled, &error) || !original->flush()) {
			if (error.isEmpty()) { error = original->errorString(); }
			original->remove(); journal.original.clear(); return fail();
		}
		original->close();
		if (!matches(journal.original, journal.before, state.options.isCancelled, &error)) { original->remove(); journal.original.clear(); return fail(); }
	}
	if (!state.step(PackagePublicationStep::WriteJournal, &error) || !writeJournal(&journal, &error)) {
		if (original) { original->remove(); journal.original.clear(); }
		return fail();
	}
	// From here onward both completed versions and the journal survive process
	// termination. Recovery determines commit state from content, never a flag.
	state.output->setAutoRemove(false);
	if (!state.step(PackagePublicationStep::CommitOutput, &error)) { return fail(); }
	if (!matches(journal.destination, journal.before, state.options.isCancelled, &error)) { return fail(); }
	if (journal.before.exists) {
		if (!atomicCopy(journal.replacement, journal.destination, journal.after, journal.before, state.options.isCancelled, &error)) { return fail(); }
	} else {
		if (!state.output->rename(journal.destination)) { error = QCoreApplication::translate("VibeStudioPackagePublication", "Unable to publish the new package without overwriting another file: %1").arg(state.output->errorString()); return fail(); }
	}
	result.committed = true;
	state.output.reset();
	// Cancellation after the commit cannot undo publication. Complete backup
	// bookkeeping and report the committed result, rather than false cancellation.
	if (!state.step(PackagePublicationStep::OutputCommitted, &error)
		|| !state.step(PackagePublicationStep::CommitBackup, &error) || !finishBackup(journal, &error)) {
		result.backupPath = journal.original;
		result.warnings << (error.isEmpty() ? QCoreApplication::translate("VibeStudioPackagePublication", "The package was saved; recovery bookkeeping remains unfinished.") : error);
		result.recoveryPaths = recoveryPaths(journal);
		return result;
	}
	result.backupPath = journal.backup;
	if (!state.step(PackagePublicationStep::Cleanup, &error) || !cleanup(journal, &error)) {
		result.warnings << (error.isEmpty() ? QCoreApplication::translate("VibeStudioPackagePublication", "The package was saved; transaction cleanup remains unfinished.") : error);
		result.recoveryPaths = recoveryPaths(journal);
	}
	return result;
}

PackagePublicationJournalInfo inspectPackagePublicationJournal(const QString& path)
{
	PackagePublicationJournalInfo info; info.journalPath = QFileInfo(path).absoluteFilePath(); Journal journal;
	if (!readJournal(path, &journal, &info.error)) { return info; }
	info.journalPath = journal.path; info.destinationPath = journal.destination; info.replacementPath = journal.replacement;
	info.originalPath = journal.original; info.backupPath = journal.backup;
	info.journalSha256 = QByteArray::fromHex(journal.journalIdentity.hash.toLatin1()); return info;
}

PackageRecoveryReport recoverPackagePublication(const QString& journalPath, bool finish, const QString& confirmedBackupPath,
	const PackageReadControl& control, const QByteArray& expectedJournalSha256)
{
	PackageRecoveryReport report; report.journalPath = absolutePath(journalPath);
	const auto fail = [&]() {
		report.canFinish = false;
		report.cancelled = control.isCancelled && control.isCancelled();
		if (report.cancelled) {
			report.error = QCoreApplication::translate("VibeStudioPackagePublication", "Recovery cancelled. The saved output was not changed; transaction files may remain for review.");
		}
		return report;
	};
	if (cancelled(control.isCancelled, &report.error)) { return fail(); }
	Journal journal;
	if (!readJournal(journalPath, &journal, &report.error)) { return fail(); }
	report.journalSha256 = QByteArray::fromHex(journal.journalIdentity.hash.toLatin1());
	report.destinationPath = journal.destination; report.replacementPath = journal.replacement;
	report.originalPath = journal.original; report.backupPath = journal.backup;
	if (!expectedJournalSha256.isEmpty() && (expectedJournalSha256.size() != 32 || report.journalSha256 != expectedJournalSha256)) {
		report.error = QCoreApplication::translate("VibeStudioPackagePublication", "The recovery journal changed. Refresh and review it before continuing."); return fail();
	}
	if (!confirmedBackupPath.isEmpty() && absolutePath(confirmedBackupPath).compare(journal.backup, pathCaseSensitivity) != 0) {
		report.error = QCoreApplication::translate("VibeStudioPackagePublication", "The selected backup path does not match the recovery journal."); return fail();
	}
	std::unique_ptr<QLockFile> lock;
	if (finish) {
		lock = std::make_unique<QLockFile>(journal.destination + QStringLiteral(".vibestudio-save.lock"));
		lock->setStaleLockTime(0);
		if (!lock->tryLock(0)) { report.error = QCoreApplication::translate("VibeStudioPackagePublication", "Another package save or recovery owns this destination."); return fail(); }
	}
	Identity current;
	if (!identify(journal.destination, &current, control.isCancelled, &report.error, &control)) { return fail(); }
	report.state = current == journal.after ? QStringLiteral("replacement") : current == journal.before ? QStringLiteral("original")
		: current.exists ? QStringLiteral("changed") : QStringLiteral("missing");
	Identity backup;
	if (journal.before.exists) {
		if (!identify(journal.backup, &backup, control.isCancelled, &report.error, &control)) { return fail(); }
		if (backup != journal.before && !matches(journal.original, journal.before, control.isCancelled, &report.error, &control)) { return fail(); }
	}
	report.backupVerified = journal.before.exists && backup == journal.before;
	for (const auto& item : {std::pair{journal.replacement, journal.after}, std::pair{journal.original, journal.before}}) {
		if (item.first.isEmpty()) { continue; }
		Identity retained;
		if (!identify(item.first, &retained, control.isCancelled, &report.error, &control)) { return fail(); }
		if (retained.exists && retained != item.second) {
			report.error = QCoreApplication::translate("VibeStudioPackagePublication", "Recovery file changed; retained for inspection: %1").arg(item.first); return fail();
		}
	}
	const QString outputDirectory = QFileInfo(journal.destination).absolutePath() + QLatin1Char('/');
	report.requiresBackupConfirmation = journal.before.exists && backup != journal.before
		&& !journal.backup.startsWith(outputDirectory, pathCaseSensitivity)
		&& (confirmedBackupPath.isEmpty() || absolutePath(confirmedBackupPath).compare(journal.backup, pathCaseSensitivity) != 0);
	report.canFinish = current == journal.after && !report.requiresBackupConfirmation
		&& (!journal.before.exists || backup == journal.before || backup == journal.backupBefore);
	if (!matches(journal.path, journal.journalIdentity, control.isCancelled, &report.error, &control)) { return fail(); }
	if (!finish) { return report; }
	if (report.requiresBackupConfirmation) {
		report.error = QCoreApplication::translate("VibeStudioPackagePublication", "The backup is outside the output directory. Review the journal and explicitly select its backup path before finishing recovery."); return fail();
	}
	if (!report.canFinish) {
		report.error = QCoreApplication::translate("VibeStudioPackagePublication", "Recovery cannot finish automatically. Inspect the retained files; the destination or backup does not match the recorded save."); return fail();
	}
	if (!matches(journal.destination, journal.after, control.isCancelled, &report.error, &control)
		|| !finishBackup(journal, &report.error, &control)) { return fail(); }
	report.backupVerified = journal.before.exists;
	if (!matches(journal.destination, journal.after, control.isCancelled, &report.error, &control)
		|| !matches(journal.path, journal.journalIdentity, control.isCancelled, &report.error, &control)
		|| !cleanup(journal, &report.error, &control)) { return fail(); }
	report.finished = true; report.canFinish = false; return report;
}

QJsonObject packageRecoveryJson(const PackageRecoveryReport& report)
{
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("journalPath"), report.journalPath},
		{QStringLiteral("destinationPath"), report.destinationPath}, {QStringLiteral("replacementPath"), report.replacementPath},
		{QStringLiteral("originalPath"), report.originalPath}, {QStringLiteral("backupPath"), report.backupPath},
		{QStringLiteral("state"), report.state}, {QStringLiteral("canFinish"), report.canFinish},
		{QStringLiteral("journalSha256"), QString::fromLatin1(report.journalSha256.toHex())},
		{QStringLiteral("cancelled"), report.cancelled}, {QStringLiteral("backupVerified"), report.backupVerified},
		{QStringLiteral("requiresBackupConfirmation"), report.requiresBackupConfirmation},
		{QStringLiteral("finished"), report.finished}, {QStringLiteral("error"), report.error},
		{QStringLiteral("warnings"), QJsonArray::fromStringList(report.warnings)}};
}

QString packageRecoveryText(const PackageRecoveryReport& report)
{
	QStringList lines {QCoreApplication::translate("VibeStudioPackagePublication", "Package recovery: %1").arg(report.state), QCoreApplication::translate("VibeStudioPackagePublication", "Destination: %1").arg(report.destinationPath), QCoreApplication::translate("VibeStudioPackagePublication", "Journal: %1").arg(report.journalPath)};
	if (!report.originalPath.isEmpty()) { lines << QCoreApplication::translate("VibeStudioPackagePublication", "Original copy: %1").arg(report.originalPath); }
	if (!report.replacementPath.isEmpty()) { lines << QCoreApplication::translate("VibeStudioPackagePublication", "Replacement copy: %1").arg(report.replacementPath); }
	if (!report.backupPath.isEmpty()) { lines << QCoreApplication::translate("VibeStudioPackagePublication", "Backup: %1").arg(report.backupPath); }
	if (!report.journalSha256.isEmpty()) { lines << QCoreApplication::translate("VibeStudioPackagePublication", "Journal SHA-256: %1").arg(QString::fromLatin1(report.journalSha256.toHex())); }
	if (report.finished) { lines << QCoreApplication::translate("VibeStudioPackagePublication", "Recovery completed; verified transaction files removed."); }
	else if (report.canFinish) { lines << QCoreApplication::translate("VibeStudioPackagePublication", "The saved replacement is verified. Use --finish to complete backup publication and cleanup."); }
	else if (report.requiresBackupConfirmation) { lines << QCoreApplication::translate("VibeStudioPackagePublication", "Review the external backup path, then repeat --finish with --backup <path> to confirm that destination."); }
	else if (report.error.isEmpty()) { lines << QCoreApplication::translate("VibeStudioPackagePublication", "Retained files require review; recovery has not changed them."); }
	if (!report.error.isEmpty()) { lines << report.error; }
	return lines.join(QLatin1Char('\n'));
}

} // namespace vibestudio
