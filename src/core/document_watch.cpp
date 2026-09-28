#include "core/document_watch.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QObject>

#include <algorithm>
#include <utility>

namespace vibestudio {

namespace {

QString watchText(const char* source)
{
	return QCoreApplication::translate("VibeStudioDocumentWatch", source);
}

} // namespace

// ---------------------------------------------------------------------------
// Fingerprints
// ---------------------------------------------------------------------------

bool DocumentFingerprint::metadataMatches(const DocumentFingerprint& other) const
{
	if (exists != other.exists) {
		return false;
	}
	if (!exists) {
		return true;
	}
	return size == other.size && modified == other.modified;
}

bool DocumentFingerprint::comparableByHash(const DocumentFingerprint& other) const
{
	return exists && other.exists && hashed && other.hashed && !contentHash.isEmpty() && !other.contentHash.isEmpty();
}

bool DocumentFingerprint::contentMatches(const DocumentFingerprint& other) const
{
	if (exists != other.exists) {
		return false;
	}
	if (!exists) {
		return true;
	}
	if (comparableByHash(other)) {
		return contentHash == other.contentHash;
	}
	// Without a hash on both sides the strongest available statement is
	// "size and modification time agree". Falling back here rather than
	// guessing keeps an oversized or unreadable file from being reported as
	// edited every poll.
	return metadataMatches(other);
}

QString DocumentFingerprint::describe() const
{
	if (!exists) {
		return watchText("missing");
	}
	QString text = watchText("%1 bytes, modified %2")
		.arg(QString::number(size), modified.toUTC().toString(Qt::ISODate));
	if (hashed && !contentHash.isEmpty()) {
		text += QStringLiteral(" [%1]").arg(QString::fromLatin1(contentHash.left(6).toHex()));
	} else if (hashSkipped) {
		text += QLatin1Char(' ') + watchText("(too large to hash)");
	}
	if (!errorId.isEmpty()) {
		text += QLatin1Char(' ') + watchText("(unreadable)");
	}
	return text;
}

QJsonObject DocumentFingerprint::toJson() const
{
	QJsonObject object;
	object.insert(QStringLiteral("exists"), exists);
	object.insert(QStringLiteral("size"), static_cast<double>(size));
	object.insert(QStringLiteral("modified"), modified.isValid() ? modified.toUTC().toString(Qt::ISODate) : QString());
	object.insert(QStringLiteral("hashed"), hashed);
	object.insert(QStringLiteral("hashSkipped"), hashSkipped);
	if (hashed && !contentHash.isEmpty()) {
		object.insert(QStringLiteral("contentHash"), QString::fromLatin1(contentHash.toHex()));
	}
	if (!errorId.isEmpty()) {
		object.insert(QStringLiteral("errorId"), errorId);
	}
	return object;
}

QString normalizeDocumentWatchPath(const QString& path)
{
	const QString trimmed = path.trimmed();
	if (trimmed.isEmpty()) {
		return QString();
	}
	const QFileInfo info(trimmed);
	const QString absolute = info.absoluteFilePath();
	return QDir::cleanPath(absolute.isEmpty() ? trimmed : absolute);
}

DocumentFingerprint fingerprintDocumentFile(const QString& path, qint64 hashSizeLimit)
{
	DocumentFingerprint fingerprint;
	const QString normalized = normalizeDocumentWatchPath(path);
	if (normalized.isEmpty()) {
		fingerprint.errorId = QStringLiteral("empty-path");
		return fingerprint;
	}

	QFileInfo info(normalized);
	info.refresh();
	if (!info.exists() || !info.isFile()) {
		return fingerprint;
	}

	fingerprint.exists = true;
	fingerprint.size = info.size();
	fingerprint.modified = info.lastModified().toUTC();

	if (hashSizeLimit < 0 || fingerprint.size > hashSizeLimit) {
		fingerprint.hashSkipped = true;
		return fingerprint;
	}

	QFile file(normalized);
	if (!file.open(QIODevice::ReadOnly)) {
		fingerprint.errorId = QStringLiteral("open-failed");
		return fingerprint;
	}

	QCryptographicHash hash(QCryptographicHash::Sha1);
	QByteArray chunk;
	qint64 consumed = 0;
	while (!file.atEnd()) {
		chunk = file.read(kDocumentWatchHashChunkBytes);
		if (chunk.isEmpty()) {
			break;
		}
		consumed += chunk.size();
		// A file that grows while it is being hashed is a file mid-save. Stop at
		// the size observed above rather than reading without a bound.
		if (consumed > hashSizeLimit) {
			fingerprint.hashSkipped = true;
			fingerprint.errorId = QStringLiteral("grew-while-reading");
			return fingerprint;
		}
		hash.addData(chunk);
	}

	fingerprint.contentHash = hash.result();
	fingerprint.hashed = !fingerprint.contentHash.isEmpty();
	if (!fingerprint.hashed) {
		fingerprint.errorId = QStringLiteral("hash-failed");
	}
	return fingerprint;
}

DocumentChangeKind classifyDocumentChange(const DocumentFingerprint& previous, const DocumentFingerprint& current, bool sawMissing)
{
	if (!previous.exists && !current.exists) {
		return DocumentChangeKind::None;
	}
	if (previous.exists && !current.exists) {
		return DocumentChangeKind::Removed;
	}
	if (!previous.exists && current.exists) {
		// Whether this reads as "created" or "replaced" is decided by the caller,
		// which knows whether the path existed when it was registered. From a
		// bare pair of fingerprints it is a creation.
		return DocumentChangeKind::Created;
	}
	if (sawMissing) {
		return DocumentChangeKind::Replaced;
	}
	if (current.contentMatches(previous)) {
		return current.metadataMatches(previous) ? DocumentChangeKind::None : DocumentChangeKind::Touched;
	}
	return DocumentChangeKind::Modified;
}

// ---------------------------------------------------------------------------
// Ids and display names
// ---------------------------------------------------------------------------

QString documentWatchRoleId(DocumentWatchRole role)
{
	switch (role) {
	case DocumentWatchRole::LevelMap:
		return QStringLiteral("level-map");
	case DocumentWatchRole::Package:
		return QStringLiteral("package");
	case DocumentWatchRole::CodeEditor:
		return QStringLiteral("code-editor");
	case DocumentWatchRole::ProjectManifest:
		return QStringLiteral("project-manifest");
	case DocumentWatchRole::Auxiliary:
		return QStringLiteral("auxiliary");
	case DocumentWatchRole::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString documentWatchRoleDisplayName(DocumentWatchRole role)
{
	switch (role) {
	case DocumentWatchRole::LevelMap:
		return watchText("Open map");
	case DocumentWatchRole::Package:
		return watchText("Open package");
	case DocumentWatchRole::CodeEditor:
		return watchText("Code editor file");
	case DocumentWatchRole::ProjectManifest:
		return watchText("Project manifest");
	case DocumentWatchRole::Auxiliary:
		return watchText("Supporting file");
	case DocumentWatchRole::Unknown:
		break;
	}
	return watchText("Watched file");
}

DocumentWatchRole documentWatchRoleFromId(const QString& id)
{
	const QString key = id.trimmed().toLower();
	if (key == QLatin1String("level-map")) {
		return DocumentWatchRole::LevelMap;
	}
	if (key == QLatin1String("package")) {
		return DocumentWatchRole::Package;
	}
	if (key == QLatin1String("code-editor")) {
		return DocumentWatchRole::CodeEditor;
	}
	if (key == QLatin1String("project-manifest")) {
		return DocumentWatchRole::ProjectManifest;
	}
	if (key == QLatin1String("auxiliary")) {
		return DocumentWatchRole::Auxiliary;
	}
	return DocumentWatchRole::Unknown;
}

QString documentChangeKindId(DocumentChangeKind kind)
{
	switch (kind) {
	case DocumentChangeKind::Modified:
		return QStringLiteral("modified");
	case DocumentChangeKind::Touched:
		return QStringLiteral("touched");
	case DocumentChangeKind::Removed:
		return QStringLiteral("removed");
	case DocumentChangeKind::Replaced:
		return QStringLiteral("replaced");
	case DocumentChangeKind::Created:
		return QStringLiteral("created");
	case DocumentChangeKind::None:
		break;
	}
	return QStringLiteral("none");
}

QString documentChangeKindDisplayName(DocumentChangeKind kind)
{
	switch (kind) {
	case DocumentChangeKind::Modified:
		return watchText("Changed on disk");
	case DocumentChangeKind::Touched:
		return watchText("Touched (content unchanged)");
	case DocumentChangeKind::Removed:
		return watchText("Removed from disk");
	case DocumentChangeKind::Replaced:
		return watchText("Replaced on disk");
	case DocumentChangeKind::Created:
		return watchText("Created on disk");
	case DocumentChangeKind::None:
		break;
	}
	return watchText("Unchanged");
}

DocumentChangeKind documentChangeKindFromId(const QString& id)
{
	const QString key = id.trimmed().toLower();
	if (key == QLatin1String("modified")) {
		return DocumentChangeKind::Modified;
	}
	if (key == QLatin1String("touched")) {
		return DocumentChangeKind::Touched;
	}
	if (key == QLatin1String("removed")) {
		return DocumentChangeKind::Removed;
	}
	if (key == QLatin1String("replaced")) {
		return DocumentChangeKind::Replaced;
	}
	if (key == QLatin1String("created")) {
		return DocumentChangeKind::Created;
	}
	return DocumentChangeKind::None;
}

bool documentChangeKindIsSubstantive(DocumentChangeKind kind)
{
	switch (kind) {
	case DocumentChangeKind::Modified:
	case DocumentChangeKind::Removed:
	case DocumentChangeKind::Replaced:
	case DocumentChangeKind::Created:
		return true;
	case DocumentChangeKind::Touched:
	case DocumentChangeKind::None:
		break;
	}
	return false;
}

QString DocumentChangeEvent::describe() const
{
	QString text = watchText("%1: %2 (%3)")
		.arg(documentWatchRoleDisplayName(role), documentChangeKindDisplayName(kind), path);
	if (coalescedNotifications > 1) {
		text += QLatin1Char(' ')
			+ watchText("[%1 notifications coalesced]").arg(QString::number(coalescedNotifications));
	}
	return text;
}

QJsonObject DocumentChangeEvent::toJson() const
{
	QJsonObject object;
	object.insert(QStringLiteral("path"), path);
	object.insert(QStringLiteral("role"), documentWatchRoleId(role));
	if (!documentId.isEmpty()) {
		object.insert(QStringLiteral("documentId"), documentId);
	}
	object.insert(QStringLiteral("kind"), documentChangeKindId(kind));
	object.insert(QStringLiteral("previous"), previous.toJson());
	object.insert(QStringLiteral("current"), current.toJson());
	object.insert(QStringLiteral("coalescedNotifications"), coalescedNotifications);
	object.insert(QStringLiteral("settleMsecs"), static_cast<double>(settleMsecs));
	object.insert(QStringLiteral("detectedAt"), detectedAt.isValid() ? detectedAt.toUTC().toString(Qt::ISODate) : QString());
	object.insert(QStringLiteral("contentChanged"), contentChanged);
	object.insert(QStringLiteral("differsFromRegistered"), differsFromRegistered);
	return object;
}

QStringList documentChangeEventLines(const QVector<DocumentChangeEvent>& events)
{
	QStringList lines;
	if (events.isEmpty()) {
		lines << watchText("No external changes detected.");
		return lines;
	}
	lines << watchText("%1 external change(s) detected:").arg(QString::number(events.size()));
	for (const DocumentChangeEvent& event : events) {
		lines << QStringLiteral("- ") + event.describe();
		lines << QStringLiteral("    ") + watchText("was %1").arg(event.previous.describe());
		lines << QStringLiteral("    ") + watchText("now %1").arg(event.current.describe());
	}
	return lines;
}

QString documentChangeEventText(const QVector<DocumentChangeEvent>& events)
{
	return documentChangeEventLines(events).join(QLatin1Char('\n'));
}

QJsonObject documentWatchSummaryJson(const QVector<DocumentWatchEntry>& entries, const QVector<DocumentChangeEvent>& events)
{
	QJsonArray entryArray;
	for (const DocumentWatchEntry& entry : entries) {
		QJsonObject object;
		object.insert(QStringLiteral("path"), entry.path);
		object.insert(QStringLiteral("role"), documentWatchRoleId(entry.role));
		if (!entry.documentId.isEmpty()) {
			object.insert(QStringLiteral("documentId"), entry.documentId);
		}
		object.insert(QStringLiteral("registeredAt"), entry.registeredAt.isValid() ? entry.registeredAt.toUTC().toString(Qt::ISODate) : QString());
		object.insert(QStringLiteral("registered"), entry.registered.toJson());
		object.insert(QStringLiteral("observed"), entry.observed.toJson());
		object.insert(QStringLiteral("pending"), entry.pending);
		object.insert(QStringLiteral("pendingNotifications"), entry.pendingNotifications);
		object.insert(QStringLiteral("filesystemWatched"), entry.filesystemWatched);
		entryArray.append(object);
	}

	QJsonArray eventArray;
	for (const DocumentChangeEvent& event : events) {
		eventArray.append(event.toJson());
	}

	QJsonObject summary;
	summary.insert(QStringLiteral("watchedCount"), entryArray.size());
	summary.insert(QStringLiteral("entries"), entryArray);
	summary.insert(QStringLiteral("changeCount"), eventArray.size());
	summary.insert(QStringLiteral("changes"), eventArray);
	return summary;
}

// ---------------------------------------------------------------------------
// DocumentWatcher
// ---------------------------------------------------------------------------

struct DocumentWatcher::WatchState {
	DocumentWatchEntry entry;
	// Fingerprint the pending change has settled towards, valid only when
	// `hasPendingFingerprint` is set. A notification with no poll behind it
	// marks a path pending without one.
	DocumentFingerprint pendingFingerprint;
	bool hasPendingFingerprint = false;
	// Monotonic milliseconds of the last notification that actually moved the
	// fingerprint. The quiet period is measured from here.
	qint64 pendingSince = 0;
	// Monotonic milliseconds of the first notification in the current burst.
	qint64 pendingFirstNotice = 0;
	// The path was observed missing during the current burst, which is what an
	// atomic rename-over save looks like.
	bool sawMissing = false;
	// Raw filesystem notifications not yet folded into the pending change.
	int rawNotifications = 0;
	// Directory handed to the QFileSystemWatcher alongside the file, so a delete
	// and recreate is still noticed after Qt drops the file path.
	QString watchedDirectory;
};

DocumentWatcher::DocumentWatcher()
	: DocumentWatcher(true)
{
}

DocumentWatcher::DocumentWatcher(bool enableFilesystemNotifications)
	: m_notificationsRequested(enableFilesystemNotifications)
{
	m_clock.start();
}

DocumentWatcher::~DocumentWatcher() = default;

void DocumentWatcher::ensureFilesystemWatcher()
{
	if (m_watcher || !m_notificationsRequested) {
		return;
	}
	// QFileSystemWatcher needs a QCoreApplication for its platform engine. A
	// headless caller (the CLI, a smoke test) simply polls instead.
	if (!QCoreApplication::instance()) {
		return;
	}

	m_watcher = std::make_unique<QFileSystemWatcher>();
	QFileSystemWatcher* watcher = m_watcher.get();
	// The watcher itself is the connection context, so both connections die with
	// it and this class needs no QObject base, no Q_OBJECT macro and no moc.
	QObject::connect(watcher, &QFileSystemWatcher::fileChanged, watcher, [this](const QString& path) {
		noteExternalNotification(path);
	});
	QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, watcher, [this](const QString& path) {
		noteDirectoryNotification(path);
	});
}

DocumentWatcher::WatchState* DocumentWatcher::stateFor(const QString& normalizedPath)
{
	const auto it = m_index.constFind(normalizedPath);
	if (it == m_index.constEnd()) {
		return nullptr;
	}
	const int index = it.value();
	if (index < 0 || index >= static_cast<int>(m_states.size())) {
		return nullptr;
	}
	return m_states[static_cast<size_t>(index)].get();
}

const DocumentWatcher::WatchState* DocumentWatcher::stateFor(const QString& normalizedPath) const
{
	const auto it = m_index.constFind(normalizedPath);
	if (it == m_index.constEnd()) {
		return nullptr;
	}
	const int index = it.value();
	if (index < 0 || index >= static_cast<int>(m_states.size())) {
		return nullptr;
	}
	return m_states[static_cast<size_t>(index)].get();
}

void DocumentWatcher::attachFilesystemWatch(WatchState& state)
{
	ensureFilesystemWatcher();
	if (!m_watcher) {
		state.entry.filesystemWatched = false;
		return;
	}

	if (state.entry.registered.exists || QFileInfo::exists(state.entry.path)) {
		state.entry.filesystemWatched = m_watcher->addPath(state.entry.path);
	} else {
		state.entry.filesystemWatched = false;
	}

	const QString directory = QFileInfo(state.entry.path).absolutePath();
	if (!directory.isEmpty() && QFileInfo::exists(directory)) {
		if (m_watcher->addPath(directory)) {
			state.watchedDirectory = directory;
		} else if (m_watcher->directories().contains(directory)) {
			// Another entry in the same directory already registered it.
			state.watchedDirectory = directory;
		}
	}
}

void DocumentWatcher::detachFilesystemWatch(WatchState& state)
{
	if (!m_watcher) {
		state.entry.filesystemWatched = false;
		state.watchedDirectory.clear();
		return;
	}

	if (state.entry.filesystemWatched) {
		m_watcher->removePath(state.entry.path);
		state.entry.filesystemWatched = false;
	}

	if (!state.watchedDirectory.isEmpty()) {
		const QString directory = state.watchedDirectory;
		state.watchedDirectory.clear();
		bool stillNeeded = false;
		for (const std::unique_ptr<WatchState>& other : m_states) {
			if (other && other.get() != &state && other->watchedDirectory == directory) {
				stillNeeded = true;
				break;
			}
		}
		if (!stillNeeded) {
			m_watcher->removePath(directory);
		}
	}
}

bool DocumentWatcher::registerPath(const QString& path, DocumentWatchRole role, const QString& documentId)
{
	const QString normalized = normalizeDocumentWatchPath(path);
	if (normalized.isEmpty()) {
		return false;
	}

	WatchState* existing = stateFor(normalized);
	if (existing) {
		detachFilesystemWatch(*existing);
		existing->entry.role = role;
		existing->entry.documentId = documentId;
		existing->entry.registeredAt = QDateTime::currentDateTimeUtc();
		existing->entry.registered = fingerprintDocumentFile(normalized, m_hashSizeLimit);
		existing->entry.observed = existing->entry.registered;
		existing->entry.pending = false;
		existing->entry.pendingNotifications = 0;
		existing->pendingFingerprint = DocumentFingerprint();
		existing->hasPendingFingerprint = false;
		existing->pendingSince = 0;
		existing->pendingFirstNotice = 0;
		existing->sawMissing = false;
		existing->rawNotifications = 0;
		attachFilesystemWatch(*existing);
		return true;
	}

	auto state = std::make_unique<WatchState>();
	state->entry.path = normalized;
	state->entry.role = role;
	state->entry.documentId = documentId;
	state->entry.registeredAt = QDateTime::currentDateTimeUtc();
	state->entry.registered = fingerprintDocumentFile(normalized, m_hashSizeLimit);
	state->entry.observed = state->entry.registered;

	m_states.push_back(std::move(state));
	m_index.insert(normalized, static_cast<int>(m_states.size()) - 1);
	attachFilesystemWatch(*m_states.back());
	return true;
}

bool DocumentWatcher::unregisterPath(const QString& path)
{
	const QString normalized = normalizeDocumentWatchPath(path);
	const auto it = m_index.constFind(normalized);
	if (it == m_index.constEnd()) {
		return false;
	}
	const int index = it.value();
	if (index < 0 || index >= static_cast<int>(m_states.size())) {
		m_index.remove(normalized);
		return false;
	}

	detachFilesystemWatch(*m_states[static_cast<size_t>(index)]);
	m_states.erase(m_states.begin() + index);
	m_index.remove(normalized);
	// Indices after the erased entry shift down by one.
	for (auto entry = m_index.begin(); entry != m_index.end(); ++entry) {
		if (entry.value() > index) {
			entry.value() -= 1;
		}
	}
	return true;
}

int DocumentWatcher::unregisterRole(DocumentWatchRole role)
{
	QStringList doomed;
	for (const std::unique_ptr<WatchState>& state : m_states) {
		if (state && state->entry.role == role) {
			doomed << state->entry.path;
		}
	}
	int removed = 0;
	for (const QString& path : std::as_const(doomed)) {
		if (unregisterPath(path)) {
			++removed;
		}
	}
	return removed;
}

void DocumentWatcher::clear()
{
	for (const std::unique_ptr<WatchState>& state : m_states) {
		if (state) {
			detachFilesystemWatch(*state);
		}
	}
	m_states.clear();
	m_index.clear();
	if (m_watcher) {
		const QStringList files = m_watcher->files();
		if (!files.isEmpty()) {
			m_watcher->removePaths(files);
		}
		const QStringList directories = m_watcher->directories();
		if (!directories.isEmpty()) {
			m_watcher->removePaths(directories);
		}
	}
}

bool DocumentWatcher::isWatching(const QString& path) const
{
	return stateFor(normalizeDocumentWatchPath(path)) != nullptr;
}

int DocumentWatcher::watchedCount() const
{
	return static_cast<int>(m_states.size());
}

QStringList DocumentWatcher::watchedPaths() const
{
	QStringList paths;
	paths.reserve(static_cast<qsizetype>(m_states.size()));
	for (const std::unique_ptr<WatchState>& state : m_states) {
		if (state) {
			paths << state->entry.path;
		}
	}
	return paths;
}

QVector<DocumentWatchEntry> DocumentWatcher::entries() const
{
	QVector<DocumentWatchEntry> result;
	result.reserve(static_cast<qsizetype>(m_states.size()));
	for (const std::unique_ptr<WatchState>& state : m_states) {
		if (state) {
			result.push_back(state->entry);
		}
	}
	return result;
}

DocumentWatchEntry DocumentWatcher::entryFor(const QString& path) const
{
	const WatchState* state = stateFor(normalizeDocumentWatchPath(path));
	return state ? state->entry : DocumentWatchEntry();
}

QStringList DocumentWatcher::pathsForRole(DocumentWatchRole role) const
{
	QStringList paths;
	for (const std::unique_ptr<WatchState>& state : m_states) {
		if (state && state->entry.role == role) {
			paths << state->entry.path;
		}
	}
	return paths;
}

DocumentChangeKind DocumentWatcher::changeSinceRegistered(const QString& path) const
{
	const WatchState* state = stateFor(normalizeDocumentWatchPath(path));
	if (!state) {
		return DocumentChangeKind::None;
	}

	const DocumentFingerprint current = fingerprintDocumentFile(state->entry.path, m_hashSizeLimit);
	const DocumentChangeKind kind = classifyDocumentChange(state->entry.registered, current, false);
	if (kind == DocumentChangeKind::Created && state->entry.registered.exists) {
		return DocumentChangeKind::Replaced;
	}
	return kind;
}

bool DocumentWatcher::hasChangedSinceRegistered(const QString& path) const
{
	return documentChangeKindIsSubstantive(changeSinceRegistered(path));
}

bool DocumentWatcher::refreshBaseline(const QString& path)
{
	const QString normalized = normalizeDocumentWatchPath(path);
	WatchState* state = stateFor(normalized);
	if (!state) {
		return false;
	}

	state->entry.registered = fingerprintDocumentFile(normalized, m_hashSizeLimit);
	state->entry.observed = state->entry.registered;
	state->entry.pending = false;
	state->entry.pendingNotifications = 0;
	state->pendingFingerprint = DocumentFingerprint();
	state->hasPendingFingerprint = false;
	state->pendingSince = 0;
	state->pendingFirstNotice = 0;
	state->sawMissing = false;
	state->rawNotifications = 0;

	// A file that was recreated after a delete is no longer known to the
	// QFileSystemWatcher, so re-arm it here.
	if (m_watcher && !state->entry.filesystemWatched && state->entry.observed.exists) {
		state->entry.filesystemWatched = m_watcher->addPath(state->entry.path);
	}
	return true;
}

int DocumentWatcher::refreshAllBaselines()
{
	int refreshed = 0;
	const QStringList paths = watchedPaths();
	for (const QString& path : paths) {
		if (refreshBaseline(path)) {
			++refreshed;
		}
	}
	return refreshed;
}

void DocumentWatcher::setCoalesceIntervalMsecs(int msecs)
{
	m_coalesceMsecs = std::max(0, msecs);
}

int DocumentWatcher::coalesceIntervalMsecs() const
{
	return m_coalesceMsecs;
}

void DocumentWatcher::setHashSizeLimit(qint64 bytes)
{
	m_hashSizeLimit = std::max<qint64>(0, bytes);
}

qint64 DocumentWatcher::hashSizeLimit() const
{
	return m_hashSizeLimit;
}

void DocumentWatcher::noteExternalNotification(const QString& path)
{
	WatchState* state = stateFor(normalizeDocumentWatchPath(path));
	if (!state) {
		return;
	}
	state->rawNotifications += 1;
	m_totalNotifications += 1;
}

void DocumentWatcher::noteDirectoryNotification(const QString& directory)
{
	const QString normalized = normalizeDocumentWatchPath(directory);
	if (normalized.isEmpty()) {
		return;
	}
	for (const std::unique_ptr<WatchState>& state : m_states) {
		if (!state) {
			continue;
		}
		if (QFileInfo(state->entry.path).absolutePath() == normalized) {
			state->rawNotifications += 1;
			m_totalNotifications += 1;
		}
	}
}

int DocumentWatcher::pendingChangeCount() const
{
	int count = 0;
	for (const std::unique_ptr<WatchState>& state : m_states) {
		if (state && state->entry.pending) {
			++count;
		}
	}
	return count;
}

bool DocumentWatcher::hasPendingChanges() const
{
	return pendingChangeCount() > 0;
}

bool DocumentWatcher::filesystemNotificationsActive() const
{
	return m_watcher && !m_watcher->files().isEmpty();
}

qint64 DocumentWatcher::totalNotifications() const
{
	return m_totalNotifications;
}

QVector<DocumentChangeEvent> DocumentWatcher::poll()
{
	return pollInternal(m_clock.isValid() ? m_clock.elapsed() : 0, false);
}

QVector<DocumentChangeEvent> DocumentWatcher::pollAt(qint64 monotonicMsecs)
{
	return pollInternal(monotonicMsecs, false);
}

QVector<DocumentChangeEvent> DocumentWatcher::flushPendingChanges()
{
	return pollInternal(m_clock.isValid() ? m_clock.elapsed() : 0, true);
}

DocumentChangeEvent DocumentWatcher::settleState(WatchState& state, qint64 monotonicMsecs)
{
	DocumentChangeEvent event;
	event.path = state.entry.path;
	event.role = state.entry.role;
	event.documentId = state.entry.documentId;
	event.previous = state.entry.observed;
	event.current = state.hasPendingFingerprint ? state.pendingFingerprint : state.entry.observed;
	event.coalescedNotifications = std::max(1, state.entry.pendingNotifications);
	event.settleMsecs = std::max<qint64>(0, monotonicMsecs - state.pendingFirstNotice);
	event.detectedAt = QDateTime::currentDateTimeUtc();

	event.kind = classifyDocumentChange(event.previous, event.current, state.sawMissing);
	if (event.kind == DocumentChangeKind::Created && state.entry.registered.exists) {
		// It existed when it was registered, so this is a replacement, not a
		// first appearance.
		event.kind = DocumentChangeKind::Replaced;
	}
	event.contentChanged = !event.current.contentMatches(event.previous);
	event.differsFromRegistered = !event.current.contentMatches(state.entry.registered);

	state.entry.observed = event.current;
	state.entry.pending = false;
	state.entry.pendingNotifications = 0;
	state.pendingFingerprint = DocumentFingerprint();
	state.hasPendingFingerprint = false;
	state.pendingSince = 0;
	state.pendingFirstNotice = 0;
	state.sawMissing = false;
	return event;
}

QVector<DocumentChangeEvent> DocumentWatcher::pollInternal(qint64 monotonicMsecs, bool forceSettle)
{
	QVector<DocumentChangeEvent> events;

	for (const std::unique_ptr<WatchState>& owned : m_states) {
		if (!owned) {
			continue;
		}
		WatchState& state = *owned;

		const int rawNotifications = state.rawNotifications;
		state.rawNotifications = 0;

		// The fingerprint is recomputed every poll rather than only when the
		// metadata moved: a rewrite can land inside the modification time's
		// resolution and keep the same size, which would otherwise be invisible.
		// Files above the hash size limit fall back to metadata anyway.
		const DocumentFingerprint current = fingerprintDocumentFile(state.entry.path, m_hashSizeLimit);
		const DocumentFingerprint& reference = state.hasPendingFingerprint ? state.pendingFingerprint : state.entry.observed;
		const bool differs = !current.metadataMatches(reference) || !current.contentMatches(reference);

		// Every filesystem notification counts, plus this poll itself when it saw
		// a difference the notifications had not already reported.
		const int notices = rawNotifications + (differs ? 1 : 0);
		if (notices > 0) {
			if (!state.entry.pending) {
				state.entry.pending = true;
				state.entry.pendingNotifications = 0;
				state.pendingFirstNotice = monotonicMsecs;
				state.pendingSince = monotonicMsecs;
				state.sawMissing = false;
			}
			state.entry.pendingNotifications += notices;
			if (differs) {
				// The file is still moving, so restart the quiet period.
				state.pendingSince = monotonicMsecs;
			}
		}

		if (state.entry.pending) {
			state.pendingFingerprint = current;
			state.hasPendingFingerprint = true;
			if (!current.exists) {
				state.sawMissing = true;
			}
		}

		if (!state.entry.pending) {
			continue;
		}

		const bool settled = forceSettle || (monotonicMsecs - state.pendingSince) >= m_coalesceMsecs;
		if (!settled) {
			continue;
		}

		const DocumentChangeEvent event = settleState(state, monotonicMsecs);
		if (event.kind != DocumentChangeKind::None) {
			events.push_back(event);
		}

		// Qt drops a watched file as soon as it is deleted or replaced, so
		// re-arm the notification for a path that came back.
		if (m_watcher && state.entry.observed.exists && !state.entry.filesystemWatched) {
			state.entry.filesystemWatched = m_watcher->addPath(state.entry.path);
		} else if (m_watcher && !state.entry.observed.exists && state.entry.filesystemWatched) {
			state.entry.filesystemWatched = false;
		}
	}

	return events;
}

} // namespace vibestudio
