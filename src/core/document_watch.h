#pragma once

// External change detection for the files the studio holds open.
//
// Nothing in VibeStudio noticed when a file it had open changed underneath it.
// That is not only a missing convenience: the Doom WAD save path re-reads the
// source file at save time, so a WAD edited by another tool between load and
// save silently produced an archive built from content the user never saw.
//
// `DocumentWatcher` tracks a small set of paths by role (the open map, the open
// package, the file in the code editor, the active project manifest), records a
// content fingerprint for each when it is registered, and reports what changed
// and how. Two independent signals feed it:
//
//  - `QFileSystemWatcher` notifications, when a `QCoreApplication` exists. These
//    are treated purely as hints: the watcher stops reporting a path once the
//    file is deleted and platforms differ in how many notifications a single
//    save produces, so a notification only marks a path as worth re-checking.
//    https://doc.qt.io/qt-6/qfilesystemwatcher.html
//  - The fingerprint comparison performed by `poll()`, which is authoritative.
//
// The class deliberately has **no Q_OBJECT, no signals and no slots**. The
// notification hookup uses a lambda bound to the owned `QFileSystemWatcher` as
// its context object, so no moc step is needed for src/core. The app layer
// drives `poll()` from a timer it already owns, which also means a smoke test
// can exercise the whole state machine without an event loop and, through
// `pollAt()`, without real time passing.
//
// A single save is rarely one filesystem event: editors truncate and rewrite,
// or write a temporary file and rename it over the target. `poll()` therefore
// holds a changed path back until it has looked the same for
// `coalesceIntervalMsecs()`, and reports the burst as one event carrying the
// number of notifications folded into it.
//
// This module is plain QtCore: no QtWidgets, no threads. A `DocumentWatcher`
// belongs to the thread that created it and must be used only from there.

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <vector>

class QFileSystemWatcher;

namespace vibestudio {

// What the studio is holding a path open as. A path may be registered under one
// role at a time; registering it again replaces the role and the baseline.
enum class DocumentWatchRole {
	Unknown,
	LevelMap,
	Package,
	CodeEditor,
	ProjectManifest,
	Auxiliary,
};

// How a watched path changed between two observations.
enum class DocumentChangeKind {
	// Indistinguishable from the previous observation.
	None,
	// Content differs.
	Modified,
	// Metadata moved (modification time, and on some platforms the size after
	// a rewrite with identical bytes) but the content is provably identical.
	// Only reported when the file was small enough to hash.
	Touched,
	// The path no longer exists.
	Removed,
	// The path vanished and came back, which is what an atomic
	// write-temporary-then-rename save looks like from outside.
	Replaced,
	// A path that did not exist when it was registered now does.
	Created,
};

// Files at or below this size are hashed, so a rewrite that keeps the same size
// and modification time is still detected and a rewrite with identical bytes is
// still recognised as a touch. Larger files fall back to size plus modification
// time, which is all that can be checked without reading them on every poll.
inline constexpr qint64 kDocumentWatchHashSizeLimit = 4LL * 1024LL * 1024LL;

// Default quiet period a changed path must hold still for before `poll()`
// reports it.
inline constexpr int kDocumentWatchCoalesceMsecs = 250;

// Bytes read per chunk while hashing, so hashing a large file never allocates
// proportionally to its size.
inline constexpr int kDocumentWatchHashChunkBytes = 64 * 1024;

// What a path looked like at one moment. `contentHash` is a SHA-1 digest of the
// whole file, present only when the file existed and was within the hash size
// limit.
struct DocumentFingerprint {
	bool exists = false;
	qint64 size = -1;
	QDateTime modified;
	QByteArray contentHash;
	bool hashed = false;
	// True when the file existed but was too large to hash.
	bool hashSkipped = false;
	// Set when the file existed but could not be read.
	QString errorId;

	// Size, modification time and existence agree.
	[[nodiscard]] bool metadataMatches(const DocumentFingerprint& other) const;
	// Content agrees. Uses the hashes when both sides carry one, and falls back
	// to metadata otherwise, so an unreadable or oversized file degrades to the
	// weaker check rather than reporting a spurious edit.
	[[nodiscard]] bool contentMatches(const DocumentFingerprint& other) const;
	// True when the comparison in `contentMatches` was hash-backed on both
	// sides, i.e. a `Touched` verdict can be trusted.
	[[nodiscard]] bool comparableByHash(const DocumentFingerprint& other) const;
	[[nodiscard]] QString describe() const;
	[[nodiscard]] QJsonObject toJson() const;
};

// One registered path and its baselines.
struct DocumentWatchEntry {
	// Cleaned absolute path, as `QDir::cleanPath(QFileInfo::absoluteFilePath())`
	// produced it at registration time. This is the key everywhere in the API.
	QString path;
	DocumentWatchRole role = DocumentWatchRole::Unknown;
	// Caller-supplied id, e.g. the open document's id. Never translated.
	QString documentId;
	QDateTime registeredAt;
	// What the file looked like when it was registered, or when the baseline was
	// last refreshed. `hasChangedSinceRegistered()` compares against this.
	DocumentFingerprint registered;
	// Last settled observation. `poll()` compares against this.
	DocumentFingerprint observed;
	// A change has been seen but has not held still long enough to report.
	bool pending = false;
	// Notifications folded into the pending change so far.
	int pendingNotifications = 0;
	// The path is currently handed to the `QFileSystemWatcher`.
	bool filesystemWatched = false;

	[[nodiscard]] bool isValid() const { return !path.isEmpty(); }
};

// One settled change, as reported by `poll()`.
struct DocumentChangeEvent {
	QString path;
	DocumentWatchRole role = DocumentWatchRole::Unknown;
	QString documentId;
	DocumentChangeKind kind = DocumentChangeKind::None;
	// The last settled observation this change is measured against.
	DocumentFingerprint previous;
	DocumentFingerprint current;
	// Filesystem notifications and polls that saw a difference, folded into this
	// one event.
	int coalescedNotifications = 0;
	// Milliseconds between the first notification and this report.
	qint64 settleMsecs = 0;
	QDateTime detectedAt;
	// The content differs from the previous observation (false for `Touched`).
	bool contentChanged = false;
	// The content differs from what was registered. This is the flag a save path
	// should act on: it stays true across polls until the baseline is refreshed.
	bool differsFromRegistered = false;

	[[nodiscard]] QString describe() const;
	[[nodiscard]] QJsonObject toJson() const;
};

QString documentWatchRoleId(DocumentWatchRole role);
QString documentWatchRoleDisplayName(DocumentWatchRole role);
DocumentWatchRole documentWatchRoleFromId(const QString& id);

QString documentChangeKindId(DocumentChangeKind kind);
QString documentChangeKindDisplayName(DocumentChangeKind kind);
DocumentChangeKind documentChangeKindFromId(const QString& id);

// True for the kinds that mean the bytes on disk are not the bytes that were
// registered: everything except `None` and `Touched`.
bool documentChangeKindIsSubstantive(DocumentChangeKind kind);

// The canonical key a path is stored under. Exposed so callers can compare
// their own paths against `DocumentWatchEntry::path` without guessing.
QString normalizeDocumentWatchPath(const QString& path);

// Fingerprints one path. Never throws and never allocates proportionally to the
// file: hashing reads in `kDocumentWatchHashChunkBytes` chunks, and files above
// `hashSizeLimit` are not hashed at all. A missing path yields
// `exists == false` rather than an error.
DocumentFingerprint fingerprintDocumentFile(const QString& path, qint64 hashSizeLimit = kDocumentWatchHashSizeLimit);

// Compares two fingerprints as `poll()` does. `sawMissing` records that the path
// disappeared between them, which turns an otherwise ordinary modification into
// `Replaced`.
DocumentChangeKind classifyDocumentChange(const DocumentFingerprint& previous, const DocumentFingerprint& current, bool sawMissing = false);

QStringList documentChangeEventLines(const QVector<DocumentChangeEvent>& events);
QString documentChangeEventText(const QVector<DocumentChangeEvent>& events);
QJsonObject documentWatchSummaryJson(const QVector<DocumentWatchEntry>& entries, const QVector<DocumentChangeEvent>& events);

class DocumentWatcher {
public:
	DocumentWatcher();
	// `enableFilesystemNotifications == false` skips the `QFileSystemWatcher`
	// entirely, leaving a pure polling watcher. Notifications are also skipped
	// when no `QCoreApplication` exists, because `QFileSystemWatcher` needs one.
	explicit DocumentWatcher(bool enableFilesystemNotifications);
	~DocumentWatcher();

	DocumentWatcher(const DocumentWatcher&) = delete;
	DocumentWatcher& operator=(const DocumentWatcher&) = delete;

	// Registers or re-registers `path`, taking a fresh baseline. Re-registering
	// an already watched path resets the baseline, clears any pending change and
	// replaces the role and document id. Returns false only for an empty path.
	bool registerPath(const QString& path, DocumentWatchRole role, const QString& documentId = QString());
	bool unregisterPath(const QString& path);
	int unregisterRole(DocumentWatchRole role);
	void clear();

	[[nodiscard]] bool isWatching(const QString& path) const;
	[[nodiscard]] int watchedCount() const;
	// Registration order.
	[[nodiscard]] QStringList watchedPaths() const;
	[[nodiscard]] QVector<DocumentWatchEntry> entries() const;
	[[nodiscard]] DocumentWatchEntry entryFor(const QString& path) const;
	[[nodiscard]] QStringList pathsForRole(DocumentWatchRole role) const;

	// Re-reads the file now and compares it against the baseline taken at
	// registration. This is the check a save path performs before trusting a
	// re-read of its source file. Unknown paths report `None` / false.
	[[nodiscard]] DocumentChangeKind changeSinceRegistered(const QString& path) const;
	[[nodiscard]] bool hasChangedSinceRegistered(const QString& path) const;

	// Adopts the file as it is now: the studio wrote it, or the user accepted the
	// external change. Clears any pending change for the path.
	bool refreshBaseline(const QString& path);
	int refreshAllBaselines();

	// Re-reads every watched path and returns the changes that have settled.
	// A path reports at most one event per call.
	QVector<DocumentChangeEvent> poll();
	// `poll()` against a caller-supplied monotonic millisecond clock. All
	// coalescing timing is derived from this value, so a test can step time
	// deterministically instead of sleeping.
	QVector<DocumentChangeEvent> pollAt(qint64 monotonicMsecs);
	// Settles every pending change immediately, ignoring the quiet period. Use
	// before a save or on shutdown, when waiting is not an option.
	QVector<DocumentChangeEvent> flushPendingChanges();

	void setCoalesceIntervalMsecs(int msecs);
	[[nodiscard]] int coalesceIntervalMsecs() const;
	void setHashSizeLimit(qint64 bytes);
	[[nodiscard]] qint64 hashSizeLimit() const;

	// Marks a path as worth re-checking on the next poll. Called by the
	// `QFileSystemWatcher` hookup, and usable directly by callers that learn of a
	// change some other way. Unknown paths are ignored.
	void noteExternalNotification(const QString& path);

	[[nodiscard]] int pendingChangeCount() const;
	[[nodiscard]] bool hasPendingChanges() const;
	// The `QFileSystemWatcher` exists and has at least one path.
	[[nodiscard]] bool filesystemNotificationsActive() const;
	// Raw notifications received since construction, across all paths. Useful
	// for diagnostics; the coalesced count per event is on the event.
	[[nodiscard]] qint64 totalNotifications() const;

private:
	struct WatchState;

	WatchState* stateFor(const QString& normalizedPath);
	const WatchState* stateFor(const QString& normalizedPath) const;
	void attachFilesystemWatch(WatchState& state);
	void detachFilesystemWatch(WatchState& state);
	void ensureFilesystemWatcher();
	void noteDirectoryNotification(const QString& directory);
	QVector<DocumentChangeEvent> pollInternal(qint64 monotonicMsecs, bool forceSettle);
	DocumentChangeEvent settleState(WatchState& state, qint64 monotonicMsecs);

	std::unique_ptr<QFileSystemWatcher> m_watcher;
	QHash<QString, int> m_index;
	// std::vector rather than QList: the states are move-only, and registration
	// order is the reporting order.
	std::vector<std::unique_ptr<WatchState>> m_states;
	QElapsedTimer m_clock;
	int m_coalesceMsecs = kDocumentWatchCoalesceMsecs;
	qint64 m_hashSizeLimit = kDocumentWatchHashSizeLimit;
	qint64 m_totalNotifications = 0;
	bool m_notificationsRequested = true;
};

} // namespace vibestudio
