#pragma once

#include "core/text_document.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

struct AssetTextMatch {
	QString filePath;
	int line = 0;
	// One-based UTF-16 column, as used by the Code editor.
	int column = 0;
	QString lineText;
	QString rawLine;
	// The complete line after all its matches have been replaced.
	QString replacementLine;
	QString bufferId = {};
	int bufferRevision = -1;
	QByteArray textSha256 = {};
	QString encoding = {};
	// Optional semantic source range; ordinary text search leaves these zero.
	int endLine = 0;
	int endColumn = 0;
};

struct AssetTextBuffer {
	QString filePath;
	QString id;
	int revision = -1;
	QString text;
	QString encoding;
	QString error;
};

struct AssetTextSearchRequest {
	QString rootPath;
	QString findText;
	QString replaceText;
	QStringList extensions;
	// Globs without a slash match a basename; otherwise they match a path
	// relative to the project. Wildcards can span directories. Exclusions win.
	QStringList includeGlobs;
	QStringList excludeGlobs;
	bool replace = false;
	bool dryRun = true;
	bool caseSensitive = false;
	bool wholeWords = false;
	int maxMatches = 10000;
	int maxFiles = 20000;
	qint64 maxFileBytes = 4ll * 1024 * 1024;
	qint64 maxTotalBytes = 64ll * 1024 * 1024;
	// Immutable open project documents override disk, including deleted files.
	QVector<AssetTextBuffer> buffers;
	std::function<bool()> isCancelled;
	// Called on the caller's thread, never directly on a GUI widget.
	std::function<void(int filesScanned, int matches)> progress;
};

// An in-memory replacement proposal. Applying it uses these exact bytes,
// after verifying every source hash, rather than searching again.
struct AssetTextFileChange {
	QString filePath;
	QByteArray originalSha256;
	qint64 originalSize = 0;
	QByteArray replacementBytes;
	int replacementCount = 0;
	QString bufferId;
	int bufferRevision = -1;
	QByteArray textSha256;
	QVector<TextFileEdit> edits;
};

struct AssetTextSearchReport {
	QString rootPath;
	QString findText;
	QString replaceText;
	bool replace = false;
	bool dryRun = true;
	bool complete = true;
	bool cancelled = false;
	int filesScanned = 0;
	int buffersScanned = 0;
	int filesSkipped = 0;
	int filesWithMatches = 0;
	int matchCount = 0;
	int replacementCount = 0;
	int replacementsApplied = 0;
	QStringList writtenFiles;
	QStringList editedBuffers;
	bool bufferEditsPending = false;
	QString saveState = QStringLiteral("clean");
	QVector<AssetTextMatch> matches;
	QVector<AssetTextFileChange> changes;
	QStringList warnings;
	QString referenceProvider;
	bool semanticRename = false;
	QString codeActionTitle;
	bool hasLanguageEdits() const { return semanticRename || !codeActionTitle.isEmpty(); }
	int referenceLocationsSkipped = 0;

	[[nodiscard]] bool succeeded() const;
	[[nodiscard]] bool canApply() const;
};

// Uses the editor's strict UTF-8 / BOM-marked UTF-16 codec. Open snapshots
// override saved paths; unavailable snapshots never fall back to stale disk.
AssetTextSearchReport findReplaceProjectText(const AssetTextSearchRequest& request);
// Preflights all files before the first write, then checks again per commit.
// Each file is atomic. A later failure/cancel can leave earlier files saved;
// writtenFiles and replacementsApplied always identify those writes.
AssetTextSearchReport applyProjectTextReplacements(const AssetTextSearchReport& preview,
	const std::function<bool()>& isCancelled = {}, const std::function<void(int, int)>& progress = {}, bool deferBufferEdits = false);
QByteArray assetTextSnapshotHash(const QString& text);
QString assetTextSearchReportText(const AssetTextSearchReport& report);

} // namespace vibestudio
