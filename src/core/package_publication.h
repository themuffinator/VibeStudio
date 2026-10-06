#pragma once

#include "core/package_content.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

class QIODevice;

namespace vibestudio {

// Checkpoints are an internal fault-injection seam. Production callers leave
// the hook empty; tests can fail or terminate a child process at each boundary.
enum class PackagePublicationStep {
	VerifyOutput,
	CopyOriginal,
	WriteJournal,
	CommitOutput,
	OutputCommitted,
	CommitBackup,
	Cleanup,
};

struct PackagePublicationOptions {
	QString destinationPath;
	bool allowOverwrite = false;
	// Every replacement preserves its previous bytes. Empty uses <output>.bak.
	QString backupPath;
	std::function<bool()> isCancelled;
	// No value skips the review guard; an empty hash requires an absent output.
	// Otherwise the destination must still have this reviewed SHA-256 at begin.
	std::optional<QString> expectedDestinationSha256;
};

struct PackagePublicationResult {
	bool committed = false;
	bool cancelled = false;
	QString backupPath;
	QString error;
	QStringList warnings;
	QStringList recoveryPaths;
};

// Exclusively reserves a sibling file for the writer. Existing output remains
// readable until atomic publication; a verified original and recovery journal
// survive an interrupted replacement. No direct-write fallback is permitted.
class PackagePublication final {
public:
	using Checkpoint = std::function<bool(PackagePublicationStep, QString*)>;
	explicit PackagePublication(PackagePublicationOptions options, Checkpoint checkpoint = {});
	~PackagePublication();
	PackagePublication(const PackagePublication&) = delete;
	PackagePublication& operator=(const PackagePublication&) = delete;
	bool begin(QString* error);
	[[nodiscard]] QIODevice* device();
	PackagePublicationResult commit(quint64 expectedBytes, const QString& expectedSha256);

private:
	class State;
	std::unique_ptr<State> m_state;
};

struct PackageRecoveryReport {
	QString journalPath;
	QString destinationPath;
	QString replacementPath;
	QString originalPath;
	QString backupPath;
	QByteArray journalSha256;
	// Stable IDs: original, replacement, changed, missing, invalid.
	QString state = QStringLiteral("invalid");
	bool canFinish = false;
	bool requiresBackupConfirmation = false;
	bool finished = false;
	bool cancelled = false;
	bool backupVerified = false;
	QString error;
	QStringList warnings;
};

// Read-only unless finish is explicitly requested. Finish only completes backup
// publication and removes verified transaction files when the replacement is
// already installed. It never installs an uncommitted replacement or rolls back
// a package changed since the interruption.
// A journal alone cannot authorize a write outside its output directory. The
// caller must explicitly supply the recorded backup path for that operation.
PackageRecoveryReport recoverPackagePublication(const QString& journalPath, bool finish = false, const QString& confirmedBackupPath = {},
	const PackageReadControl& control = {}, const QByteArray& expectedJournalSha256 = {});
QJsonObject packageRecoveryJson(const PackageRecoveryReport& report);
QString packageRecoveryText(const PackageRecoveryReport& report);

struct PackagePublicationJournalInfo {
	QString journalPath, destinationPath, replacementPath, originalPath, backupPath;
	QByteArray journalSha256;
	QString error;
	[[nodiscard]] bool metadataValid() const { return error.isEmpty() && journalSha256.size() == 32; }
};

// Metadata-only discovery never hashes output/backup payloads or writes locks.
// Inspection verifies the selected journal's payloads through the recovery API.
PackagePublicationJournalInfo inspectPackagePublicationJournal(const QString& path);
inline constexpr int PackagePublicationDirectoryLimit = 32;
inline constexpr int PackagePublicationJournalLimit = 256;
inline constexpr int PackagePublicationDirectoryEntryLimit = 100000;
struct PackagePublicationInventory {
	QStringList directories;
	QVector<PackagePublicationJournalInfo> journals;
	QStringList errors;
	int visitedEntries = 0;
	bool truncated = false, cancelled = false;
	[[nodiscard]] bool complete() const { return !truncated && !cancelled && errors.isEmpty(); }
};
PackagePublicationInventory listPackagePublicationJournals(const QStringList& directories, const PackageReadControl& control = {});
QJsonObject packagePublicationInventoryJson(const PackagePublicationInventory& inventory);

} // namespace vibestudio
