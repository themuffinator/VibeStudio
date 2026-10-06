#include "core/document_watch.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QVector>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const qint64 written = file.write(bytes);
	file.close();
	return written == bytes.size();
}

// Moves the modification time without touching a byte, which is what a build
// tool or an rsync-style copy does to an otherwise identical file.
bool moveModificationTime(const QString& path, int secondsForward)
{
	const QDateTime target = QFileInfo(path).lastModified().addSecs(secondsForward);
	QFile file(path);
	if (!file.open(QIODevice::ReadWrite)) {
		return false;
	}
	const bool ok = file.setFileTime(target, QFileDevice::FileModificationTime);
	file.close();
	return ok;
}

bool runFingerprintSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Fingerprint smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QString path = QDir(temporary.path()).filePath(QStringLiteral("fingerprint.txt"));
	ok &= expect(writeFile(path, QByteArrayLiteral("alpha")), "Fixture file should be written.");

	const DocumentFingerprint first = fingerprintDocumentFile(path);
	ok &= expect(first.exists, "A written file should fingerprint as existing.");
	ok &= expect(first.size == 5, "Fingerprint should record the file size.");
	ok &= expect(first.hashed && !first.contentHash.isEmpty(), "A small file should be hashed.");
	ok &= expect(!first.hashSkipped, "A small file should not report a skipped hash.");
	ok &= expect(first.modified.isValid(), "Fingerprint should record a modification time.");

	const DocumentFingerprint again = fingerprintDocumentFile(path);
	ok &= expect(first.metadataMatches(again), "Re-fingerprinting an untouched file should match on metadata.");
	ok &= expect(first.contentMatches(again), "Re-fingerprinting an untouched file should match on content.");
	ok &= expect(first.comparableByHash(again), "Two hashed fingerprints should be hash-comparable.");

	ok &= expect(writeFile(path, QByteArrayLiteral("bravo")), "Fixture file should be rewritten.");
	const DocumentFingerprint edited = fingerprintDocumentFile(path);
	ok &= expect(edited.size == first.size, "The rewrite keeps the same size, which is the interesting case.");
	ok &= expect(!edited.contentMatches(first), "A same-size rewrite should still differ by hash.");

	const DocumentFingerprint missing = fingerprintDocumentFile(QDir(temporary.path()).filePath(QStringLiteral("absent.txt")));
	ok &= expect(!missing.exists, "A missing path should fingerprint as not existing.");
	ok &= expect(missing.contentMatches(missing), "A missing fingerprint should match itself.");
	ok &= expect(!missing.contentMatches(first), "A missing fingerprint should not match an existing one.");

	const DocumentFingerprint empty = fingerprintDocumentFile(QString());
	ok &= expect(!empty.exists && empty.errorId == QStringLiteral("empty-path"), "An empty path should report an error id.");

	// The hash size limit degrades to size plus modification time.
	const DocumentFingerprint unhashed = fingerprintDocumentFile(path, 2);
	ok &= expect(unhashed.exists && !unhashed.hashed && unhashed.hashSkipped, "A file over the hash limit should skip hashing.");
	ok &= expect(!unhashed.comparableByHash(edited), "An unhashed fingerprint is not hash-comparable.");
	ok &= expect(unhashed.contentMatches(edited), "An unhashed fingerprint falls back to metadata comparison.");

	ok &= expect(!first.describe().isEmpty(), "A fingerprint should describe itself.");
	ok &= expect(!missing.describe().isEmpty(), "A missing fingerprint should describe itself.");
	ok &= expect(first.toJson().value(QStringLiteral("exists")).toBool(), "Fingerprint JSON should carry existence.");
	return ok;
}

bool runRegistrationSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Registration smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QString path = QDir(temporary.path()).filePath(QStringLiteral("e1m1.map"));
	ok &= expect(writeFile(path, QByteArrayLiteral("{ \"classname\" \"worldspawn\" }\n")), "Map fixture should be written.");

	DocumentWatcher watcher(false);
	ok &= expect(!watcher.filesystemNotificationsActive(), "A polling-only watcher should report no filesystem notifications.");
	ok &= expect(watcher.watchedCount() == 0, "A fresh watcher should watch nothing.");
	ok &= expect(!watcher.registerPath(QString(), DocumentWatchRole::LevelMap), "An empty path should not register.");

	ok &= expect(watcher.registerPath(path, DocumentWatchRole::LevelMap, QStringLiteral("map-1")), "The map should register.");
	ok &= expect(watcher.watchedCount() == 1, "The watcher should hold one path.");
	ok &= expect(watcher.isWatching(path), "The registered path should be watched.");

	const DocumentWatchEntry entry = watcher.entryFor(path);
	ok &= expect(entry.isValid(), "The entry should be found by path.");
	ok &= expect(entry.role == DocumentWatchRole::LevelMap, "The entry should carry its role.");
	ok &= expect(entry.documentId == QStringLiteral("map-1"), "The entry should carry the document id.");
	ok &= expect(entry.registered.exists && entry.registered.hashed, "The baseline should be a real fingerprint.");
	ok &= expect(entry.registeredAt.isValid(), "The entry should record when it was registered.");
	ok &= expect(!entry.pending, "A freshly registered path should have nothing pending.");

	// Nothing has happened yet, so nothing may be reported.
	ok &= expect(watcher.pollAt(0).isEmpty(), "An untouched file should report no change.");
	ok &= expect(watcher.pollAt(10'000).isEmpty(), "An untouched file should still report no change later.");
	ok &= expect(!watcher.hasPendingChanges(), "An untouched file should leave nothing pending.");
	ok &= expect(!watcher.hasChangedSinceRegistered(path), "An untouched file has not changed since registration.");
	ok &= expect(watcher.changeSinceRegistered(path) == DocumentChangeKind::None, "An untouched file classifies as unchanged.");

	// Unknown paths are inert rather than fatal.
	const QString unknown = QDir(temporary.path()).filePath(QStringLiteral("nothing-here.map"));
	ok &= expect(!watcher.hasChangedSinceRegistered(unknown), "An unwatched path reports no change.");
	ok &= expect(!watcher.entryFor(unknown).isValid(), "An unwatched path has no entry.");
	ok &= expect(!watcher.unregisterPath(unknown), "Unregistering an unwatched path should fail quietly.");
	watcher.noteExternalNotification(unknown);
	ok &= expect(watcher.totalNotifications() == 0, "A notification for an unwatched path should be ignored.");
	return ok;
}

bool runModificationSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Modification smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QString path = QDir(temporary.path()).filePath(QStringLiteral("pak0.wad"));
	ok &= expect(writeFile(path, QByteArrayLiteral("IWAD-original-content")), "WAD fixture should be written.");

	DocumentWatcher watcher(false);
	watcher.setCoalesceIntervalMsecs(200);
	ok &= expect(watcher.coalesceIntervalMsecs() == 200, "The coalesce interval should round-trip.");
	ok &= expect(watcher.registerPath(path, DocumentWatchRole::Package, QStringLiteral("wad-1")), "The WAD should register.");

	ok &= expect(writeFile(path, QByteArrayLiteral("IWAD-rewritten-by-another-tool")), "The WAD should be rewritten.");

	// This is the check the Doom WAD save path performs before it re-reads its
	// source file: it must not need a poll to have run first.
	ok &= expect(watcher.hasChangedSinceRegistered(path), "An edited file should be flagged against its registration baseline.");
	ok &= expect(watcher.changeSinceRegistered(path) == DocumentChangeKind::Modified, "An edited file should classify as modified.");

	const QVector<DocumentChangeEvent> immediate = watcher.pollAt(0);
	ok &= expect(immediate.isEmpty(), "A change should be held back for the quiet period.");
	ok &= expect(watcher.pendingChangeCount() == 1, "The change should be pending.");
	ok &= expect(watcher.entryFor(path).pending, "The entry should report the pending change.");

	const QVector<DocumentChangeEvent> settled = watcher.pollAt(1'000);
	ok &= expect(settled.size() == 1, "The settled change should be reported exactly once.");
	if (settled.size() == 1) {
		const DocumentChangeEvent& event = settled.first();
		ok &= expect(event.kind == DocumentChangeKind::Modified, "The change should be reported as modified.");
		ok &= expect(event.path == watcher.entryFor(path).path, "The event should carry the normalized path.");
		ok &= expect(event.role == DocumentWatchRole::Package, "The event should carry the role.");
		ok &= expect(event.documentId == QStringLiteral("wad-1"), "The event should carry the document id.");
		ok &= expect(event.contentChanged, "A modification should report changed content.");
		ok &= expect(event.differsFromRegistered, "A modification should differ from the registration baseline.");
		ok &= expect(event.previous.exists && event.current.exists, "Both fingerprints should exist.");
		ok &= expect(event.current.size != event.previous.size, "The fixture rewrite changed the size.");
		ok &= expect(event.settleMsecs >= 0, "The settle time should be non-negative.");
		ok &= expect(event.detectedAt.isValid(), "The event should be timestamped.");
		ok &= expect(!event.describe().isEmpty(), "The event should describe itself.");
		ok &= expect(event.toJson().value(QStringLiteral("kind")).toString() == QStringLiteral("modified"), "Event JSON should carry the kind id.");
	}

	ok &= expect(watcher.pollAt(2'000).isEmpty(), "A settled change should not be reported twice.");
	ok &= expect(!watcher.hasPendingChanges(), "Nothing should be pending after settling.");
	// The baseline is untouched by a poll, so the save-time guard still fires.
	ok &= expect(watcher.hasChangedSinceRegistered(path), "Reporting a change must not silently adopt it.");

	ok &= expect(watcher.refreshBaseline(path), "The baseline should be refreshable.");
	ok &= expect(!watcher.hasChangedSinceRegistered(path), "Refreshing the baseline should clear the change.");
	ok &= expect(watcher.pollAt(3'000).isEmpty(), "Refreshing the baseline should leave nothing to report.");
	ok &= expect(!watcher.refreshBaseline(QDir(temporary.path()).filePath(QStringLiteral("absent"))), "Refreshing an unwatched path should fail.");
	return ok;
}

bool runTouchSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Touch smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QString path = QDir(temporary.path()).filePath(QStringLiteral("project.vsproj"));
	const QByteArray content = QByteArrayLiteral("{\"name\":\"demo\"}\n");
	ok &= expect(writeFile(path, content), "Manifest fixture should be written.");

	DocumentWatcher watcher(false);
	watcher.setCoalesceIntervalMsecs(0);
	ok &= expect(watcher.registerPath(path, DocumentWatchRole::ProjectManifest), "The manifest should register.");

	const QDateTime before = QFileInfo(path).lastModified();
	// Rewriting byte-for-byte identical content is the everyday case: a build
	// step or a save with no edits.
	ok &= expect(writeFile(path, content), "The manifest should be rewritten with identical content.");
	moveModificationTime(path, 120);
	const bool timeMoved = QFileInfo(path).lastModified() != before;

	const DocumentChangeKind kind = watcher.changeSinceRegistered(path);
	ok &= expect(kind == DocumentChangeKind::Touched || kind == DocumentChangeKind::None,
		"An identical rewrite must never classify as modified.");
	if (timeMoved) {
		ok &= expect(kind == DocumentChangeKind::Touched, "An identical rewrite with a newer timestamp is a touch.");
	}
	ok &= expect(!watcher.hasChangedSinceRegistered(path), "A touch must not be reported as a real change.");
	ok &= expect(!documentChangeKindIsSubstantive(DocumentChangeKind::Touched), "A touch is not a substantive change.");

	const QVector<DocumentChangeEvent> events = watcher.pollAt(0);
	if (timeMoved) {
		ok &= expect(events.size() == 1, "A touch should still surface as an event.");
		if (events.size() == 1) {
			ok &= expect(events.first().kind == DocumentChangeKind::Touched, "The event should be a touch.");
			ok &= expect(!events.first().contentChanged, "A touch reports unchanged content.");
			ok &= expect(!events.first().differsFromRegistered, "A touch does not differ from the baseline.");
		}
	} else {
		ok &= expect(events.isEmpty(), "Without a timestamp move there is nothing to report.");
	}

	// A real edit after the touch is still caught.
	ok &= expect(writeFile(path, QByteArrayLiteral("{\"name\":\"demo2\"}\n")), "The manifest should be edited.");
	ok &= expect(watcher.hasChangedSinceRegistered(path), "A real edit after a touch should be flagged.");
	const QVector<DocumentChangeEvent> edited = watcher.pollAt(1'000);
	ok &= expect(edited.size() == 1 && edited.first().kind == DocumentChangeKind::Modified, "A real edit should report as modified.");
	return ok;
}

bool runRemovalSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Removal smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QString path = QDir(temporary.path()).filePath(QStringLiteral("shader.glsl"));
	ok &= expect(writeFile(path, QByteArrayLiteral("void main() {}\n")), "Shader fixture should be written.");

	DocumentWatcher watcher(false);
	watcher.setCoalesceIntervalMsecs(0);
	ok &= expect(watcher.registerPath(path, DocumentWatchRole::CodeEditor, QStringLiteral("editor-1")), "The shader should register.");

	ok &= expect(QFile::remove(path), "The shader should be removable.");
	ok &= expect(watcher.changeSinceRegistered(path) == DocumentChangeKind::Removed, "A deleted file should classify as removed.");
	ok &= expect(watcher.hasChangedSinceRegistered(path), "A deleted file is a substantive change.");

	const QVector<DocumentChangeEvent> removal = watcher.pollAt(0);
	ok &= expect(removal.size() == 1, "The removal should be reported.");
	if (removal.size() == 1) {
		ok &= expect(removal.first().kind == DocumentChangeKind::Removed, "The event should be a removal.");
		ok &= expect(removal.first().previous.exists && !removal.first().current.exists, "The removal fingerprints should bracket the deletion.");
		ok &= expect(removal.first().differsFromRegistered, "A removal differs from the baseline.");
	}
	ok &= expect(watcher.pollAt(100).isEmpty(), "A removal should not repeat while the file stays gone.");

	// A rename-over save: the path vanishes and comes back with new content.
	ok &= expect(writeFile(path, QByteArrayLiteral("void main() { gl_FragColor = vec4(1.0); }\n")), "The shader should reappear.");
	const QVector<DocumentChangeEvent> replacement = watcher.pollAt(200);
	ok &= expect(replacement.size() == 1, "The reappearance should be reported.");
	if (replacement.size() == 1) {
		ok &= expect(replacement.first().kind == DocumentChangeKind::Replaced, "A file that came back should report as replaced.");
		ok &= expect(replacement.first().differsFromRegistered, "The replacement differs from the baseline.");
	}

	// A path registered while missing reports a creation, not a replacement.
	const QString pending = QDir(temporary.path()).filePath(QStringLiteral("not-yet.cfg"));
	ok &= expect(watcher.registerPath(pending, DocumentWatchRole::Auxiliary), "A missing path should still register.");
	ok &= expect(!watcher.entryFor(pending).registered.exists, "The baseline for a missing path should record absence.");
	ok &= expect(watcher.changeSinceRegistered(pending) == DocumentChangeKind::None, "A still-missing path is unchanged.");
	ok &= expect(writeFile(pending, QByteArrayLiteral("set cl_run 1\n")), "The missing path should be created.");
	ok &= expect(watcher.changeSinceRegistered(pending) == DocumentChangeKind::Created, "An appearing file should classify as created.");
	const QVector<DocumentChangeEvent> created = watcher.pollAt(300);
	ok &= expect(created.size() == 1 && created.first().kind == DocumentChangeKind::Created, "The creation should be reported.");
	return ok;
}

bool runReRegisterSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Re-registration smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QString path = QDir(temporary.path()).filePath(QStringLiteral("q3dm1.map"));
	ok &= expect(writeFile(path, QByteArrayLiteral("original")), "Map fixture should be written.");

	DocumentWatcher watcher(false);
	watcher.setCoalesceIntervalMsecs(500);
	ok &= expect(watcher.registerPath(path, DocumentWatchRole::LevelMap, QStringLiteral("map-a")), "The map should register.");

	ok &= expect(writeFile(path, QByteArrayLiteral("changed on disk")), "The map should change on disk.");
	ok &= expect(watcher.pollAt(0).isEmpty(), "The change should be pending.");
	ok &= expect(watcher.pendingChangeCount() == 1, "There should be one pending change.");
	ok &= expect(watcher.hasChangedSinceRegistered(path), "The change should be flagged against the baseline.");

	// Re-registering is what happens when the document is reloaded.
	ok &= expect(watcher.registerPath(path, DocumentWatchRole::LevelMap, QStringLiteral("map-b")), "The map should re-register.");
	ok &= expect(watcher.watchedCount() == 1, "Re-registering must not duplicate the entry.");
	ok &= expect(watcher.entryFor(path).documentId == QStringLiteral("map-b"), "Re-registering should replace the document id.");
	ok &= expect(!watcher.hasChangedSinceRegistered(path), "Re-registering should reset the baseline.");
	ok &= expect(watcher.pendingChangeCount() == 0, "Re-registering should drop the pending change.");
	ok &= expect(watcher.pollAt(5'000).isEmpty(), "Re-registering should leave nothing to report.");

	ok &= expect(writeFile(path, QByteArrayLiteral("changed again")), "The map should change again.");
	ok &= expect(watcher.hasChangedSinceRegistered(path), "A change after re-registration should be flagged.");
	return ok;
}

bool runCoalesceSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Coalesce smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QString path = QDir(temporary.path()).filePath(QStringLiteral("burst.map"));
	ok &= expect(writeFile(path, QByteArrayLiteral("rev0")), "Burst fixture should be written.");

	DocumentWatcher watcher(false);
	watcher.setCoalesceIntervalMsecs(500);
	ok &= expect(watcher.registerPath(path, DocumentWatchRole::LevelMap), "The burst fixture should register.");

	// A single save from a real editor arrives as several writes in a few
	// milliseconds. Each poll that sees the file still moving restarts the
	// quiet period, so only one event comes out.
	ok &= expect(writeFile(path, QByteArrayLiteral("rev1")), "First write of the burst.");
	ok &= expect(watcher.pollAt(0).isEmpty(), "The first write should not settle.");
	ok &= expect(writeFile(path, QByteArrayLiteral("rev2-longer")), "Second write of the burst.");
	ok &= expect(watcher.pollAt(50).isEmpty(), "The second write should not settle.");
	ok &= expect(writeFile(path, QByteArrayLiteral("rev3")), "Third write of the burst.");
	ok &= expect(watcher.pollAt(100).isEmpty(), "The third write should not settle.");
	ok &= expect(watcher.pollAt(400).isEmpty(), "The quiet period should still be running.");
	ok &= expect(watcher.pendingChangeCount() == 1, "The whole burst should be one pending change.");

	const QVector<DocumentChangeEvent> events = watcher.pollAt(700);
	ok &= expect(events.size() == 1, "A burst of writes should coalesce into one event.");
	if (events.size() == 1) {
		const DocumentChangeEvent& event = events.first();
		ok &= expect(event.kind == DocumentChangeKind::Modified, "The coalesced event should be a modification.");
		ok &= expect(event.coalescedNotifications == 3, "The event should count the three differing polls.");
		ok &= expect(event.settleMsecs == 700, "The settle time should span the whole burst.");
		ok &= expect(event.current.size == 4, "The reported content should be the final revision.");
	}
	ok &= expect(watcher.pollAt(800).isEmpty(), "Nothing should follow a coalesced event.");

	// Filesystem notifications feed the same counter without needing a poll of
	// their own, and a notification with no real change reports nothing.
	watcher.noteExternalNotification(path);
	watcher.noteExternalNotification(path);
	ok &= expect(watcher.totalNotifications() == 2, "Notifications should be counted.");
	ok &= expect(watcher.pollAt(900).isEmpty(), "A notification alone should not settle immediately.");
	ok &= expect(watcher.pendingChangeCount() == 1, "A notification should mark the path pending.");
	ok &= expect(watcher.pollAt(2'000).isEmpty(), "A notification with no real change should report nothing.");
	ok &= expect(watcher.pendingChangeCount() == 0, "A spurious notification should clear itself.");

	// flushPendingChanges ignores the quiet period, which is what a save or a
	// shutdown needs.
	ok &= expect(writeFile(path, QByteArrayLiteral("rev4")), "A final write before the flush.");
	const QVector<DocumentChangeEvent> flushed = watcher.flushPendingChanges();
	ok &= expect(flushed.size() == 1 && flushed.first().kind == DocumentChangeKind::Modified, "Flushing should settle immediately.");
	ok &= expect(watcher.flushPendingChanges().isEmpty(), "A second flush should have nothing to report.");
	return ok;
}

bool runRoleSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Role smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QDir root(temporary.path());
	const QString mapPath = root.filePath(QStringLiteral("a.map"));
	const QString packagePath = root.filePath(QStringLiteral("b.pk3"));
	const QString codePath = root.filePath(QStringLiteral("c.qc"));
	ok &= expect(writeFile(mapPath, QByteArrayLiteral("map")), "Map fixture should be written.");
	ok &= expect(writeFile(packagePath, QByteArrayLiteral("pk3")), "Package fixture should be written.");
	ok &= expect(writeFile(codePath, QByteArrayLiteral("code")), "Code fixture should be written.");

	DocumentWatcher watcher(false);
	ok &= expect(watcher.registerPath(mapPath, DocumentWatchRole::LevelMap), "Map should register.");
	ok &= expect(watcher.registerPath(packagePath, DocumentWatchRole::Package), "Package should register.");
	ok &= expect(watcher.registerPath(codePath, DocumentWatchRole::CodeEditor), "Code file should register.");
	ok &= expect(watcher.watchedCount() == 3, "Three paths should be watched.");
	ok &= expect(watcher.watchedPaths().size() == 3, "Three paths should be listed.");
	ok &= expect(watcher.pathsForRole(DocumentWatchRole::Package).size() == 1, "One package should be watched.");
	ok &= expect(watcher.pathsForRole(DocumentWatchRole::Auxiliary).isEmpty(), "No auxiliary files should be watched.");

	// Removing the middle entry must keep the remaining lookups correct.
	ok &= expect(watcher.unregisterPath(packagePath), "The package should unregister.");
	ok &= expect(watcher.watchedCount() == 2, "Two paths should remain.");
	ok &= expect(!watcher.isWatching(packagePath), "The package should no longer be watched.");
	ok &= expect(watcher.entryFor(codePath).role == DocumentWatchRole::CodeEditor, "Lookups after a removal should stay correct.");
	ok &= expect(watcher.entryFor(mapPath).role == DocumentWatchRole::LevelMap, "The first entry should still resolve.");

	ok &= expect(writeFile(codePath, QByteArrayLiteral("code changed")), "The code file should change.");
	const QVector<DocumentChangeEvent> events = watcher.flushPendingChanges();
	ok &= expect(events.size() == 1 && events.first().role == DocumentWatchRole::CodeEditor, "The surviving entry should still report.");

	ok &= expect(watcher.unregisterRole(DocumentWatchRole::CodeEditor) == 1, "Unregistering by role should remove one path.");
	ok &= expect(watcher.watchedCount() == 1, "One path should remain after the role removal.");

	watcher.clear();
	ok &= expect(watcher.watchedCount() == 0, "Clearing should drop every path.");
	ok &= expect(watcher.watchedPaths().isEmpty(), "Clearing should empty the path list.");
	ok &= expect(watcher.pollAt(0).isEmpty(), "Polling an empty watcher should report nothing.");
	return ok;
}

bool runVocabularySmoke()
{
	bool ok = true;

	const DocumentChangeKind kinds[] = {
		DocumentChangeKind::None,
		DocumentChangeKind::Modified,
		DocumentChangeKind::Touched,
		DocumentChangeKind::Removed,
		DocumentChangeKind::Replaced,
		DocumentChangeKind::Created,
	};
	for (const DocumentChangeKind kind : kinds) {
		const QString id = documentChangeKindId(kind);
		ok &= expect(!id.isEmpty(), "Every change kind should have an id.");
		ok &= expect(documentChangeKindFromId(id) == kind, "Change kind ids should round-trip.");
		ok &= expect(!documentChangeKindDisplayName(kind).isEmpty(), "Every change kind should have a display name.");
	}
	ok &= expect(documentChangeKindFromId(QStringLiteral("not-a-kind")) == DocumentChangeKind::None, "An unknown kind id falls back to none.");

	const DocumentWatchRole roles[] = {
		DocumentWatchRole::Unknown,
		DocumentWatchRole::LevelMap,
		DocumentWatchRole::Package,
		DocumentWatchRole::CodeEditor,
		DocumentWatchRole::ProjectManifest,
		DocumentWatchRole::Auxiliary,
	};
	for (const DocumentWatchRole role : roles) {
		const QString id = documentWatchRoleId(role);
		ok &= expect(!id.isEmpty(), "Every role should have an id.");
		ok &= expect(documentWatchRoleFromId(id) == role, "Role ids should round-trip.");
		ok &= expect(!documentWatchRoleDisplayName(role).isEmpty(), "Every role should have a display name.");
	}
	ok &= expect(documentWatchRoleFromId(QStringLiteral("nope")) == DocumentWatchRole::Unknown, "An unknown role id falls back to unknown.");

	ok &= expect(documentChangeKindIsSubstantive(DocumentChangeKind::Modified), "A modification is substantive.");
	ok &= expect(documentChangeKindIsSubstantive(DocumentChangeKind::Removed), "A removal is substantive.");
	ok &= expect(documentChangeKindIsSubstantive(DocumentChangeKind::Replaced), "A replacement is substantive.");
	ok &= expect(documentChangeKindIsSubstantive(DocumentChangeKind::Created), "A creation is substantive.");
	ok &= expect(!documentChangeKindIsSubstantive(DocumentChangeKind::None), "No change is not substantive.");

	DocumentFingerprint missing;
	DocumentFingerprint present;
	present.exists = true;
	present.size = 10;
	present.modified = QDateTime::fromSecsSinceEpoch(1'000'000, QTimeZone::utc());
	present.contentHash = QByteArrayLiteral("aaaa");
	present.hashed = true;

	DocumentFingerprint edited = present;
	edited.contentHash = QByteArrayLiteral("bbbb");

	ok &= expect(classifyDocumentChange(missing, missing) == DocumentChangeKind::None, "Missing to missing is no change.");
	ok &= expect(classifyDocumentChange(present, missing) == DocumentChangeKind::Removed, "Present to missing is a removal.");
	ok &= expect(classifyDocumentChange(missing, present) == DocumentChangeKind::Created, "Missing to present is a creation.");
	ok &= expect(classifyDocumentChange(present, present) == DocumentChangeKind::None, "Identical fingerprints are no change.");
	ok &= expect(classifyDocumentChange(present, edited) == DocumentChangeKind::Modified, "A different hash is a modification.");
	ok &= expect(classifyDocumentChange(present, edited, true) == DocumentChangeKind::Replaced, "A change across a disappearance is a replacement.");

	DocumentFingerprint touched = present;
	touched.modified = present.modified.addSecs(60);
	ok &= expect(classifyDocumentChange(present, touched) == DocumentChangeKind::Touched, "A newer timestamp with the same hash is a touch.");

	ok &= expect(normalizeDocumentWatchPath(QString()).isEmpty(), "An empty path normalizes to empty.");
	ok &= expect(!normalizeDocumentWatchPath(QStringLiteral("relative/file.map")).isEmpty(), "A relative path normalizes to something absolute.");
	ok &= expect(!normalizeDocumentWatchPath(QStringLiteral("relative/file.map")).contains(QStringLiteral("\\")), "Normalized paths use forward slashes.");
	return ok;
}

bool runReportingSmoke()
{
	bool ok = true;
	QTemporaryDir temporary;
	ok &= expect(temporary.isValid(), "Reporting smoke needs a temporary directory.");
	if (!temporary.isValid()) {
		return false;
	}

	const QString path = QDir(temporary.path()).filePath(QStringLiteral("report.map"));
	ok &= expect(writeFile(path, QByteArrayLiteral("before")), "Reporting fixture should be written.");

	DocumentWatcher watcher(false);
	watcher.setCoalesceIntervalMsecs(0);
	ok &= expect(watcher.registerPath(path, DocumentWatchRole::LevelMap, QStringLiteral("map-report")), "The fixture should register.");

	const QStringList quiet = documentChangeEventLines({});
	ok &= expect(quiet.size() == 1, "An empty change list should still produce a line.");

	ok &= expect(writeFile(path, QByteArrayLiteral("after the edit")), "The fixture should change.");
	const QVector<DocumentChangeEvent> events = watcher.pollAt(0);
	ok &= expect(events.size() == 1, "The change should be reported.");

	const QStringList lines = documentChangeEventLines(events);
	ok &= expect(lines.size() >= 4, "A reported change should produce a header and detail lines.");
	ok &= expect(documentChangeEventText(events).contains(QLatin1Char('\n')), "The change text should be multi-line.");

	const QJsonObject summary = documentWatchSummaryJson(watcher.entries(), events);
	ok &= expect(summary.value(QStringLiteral("watchedCount")).toInt() == 1, "The summary should count the watched path.");
	ok &= expect(summary.value(QStringLiteral("changeCount")).toInt() == 1, "The summary should count the change.");
	ok &= expect(summary.value(QStringLiteral("entries")).isArray(), "The summary should carry the entries array.");
	ok &= expect(summary.value(QStringLiteral("changes")).isArray(), "The summary should carry the changes array.");
	return ok;
}

} // namespace

int main()
{
	bool ok = true;
	ok &= runFingerprintSmoke();
	ok &= runRegistrationSmoke();
	ok &= runModificationSmoke();
	ok &= runTouchSmoke();
	ok &= runRemovalSmoke();
	ok &= runReRegisterSmoke();
	ok &= runCoalesceSmoke();
	ok &= runRoleSmoke();
	ok &= runVocabularySmoke();
	ok &= runReportingSmoke();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
