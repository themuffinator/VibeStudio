#pragma once

#include "core/operation_state.h"
#include "core/studio_query.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace vibestudio {

struct CodeFileEntry {
	QString filePath;
	QString relativePath;
	QString languageId;
	qint64 sizeBytes = 0;
	QString kind;
};

struct CodeFilesRequest {
	QString rootPath;
	int maxFiles = 4000;
	int maxEntries = 100000;
	// Also discover assets and package files opened by the other studio surfaces.
	bool includeAssets = false;
	std::function<bool()> isCancelled;
	std::function<void(int files, int entries)> progress;
};

struct CodeFilesResult {
	QString rootPath;
	QVector<CodeFileEntry> files;
	bool complete = true;
	bool cancelled = false;
	int entriesVisited = 0;
	int directoriesExcluded = 0;
	int linksExcluded = 0;
	QString error;
	QStringList warnings;
	OperationState state = OperationState::Idle;
};

// Filename classification only. Opening a file still validates its encoding,
// size and editability through the text-document service.
QString codeFileLanguageId(const QString& path);
bool isCodeFileCandidate(const QString& path);
QString projectFileKindId(const QString& path);
bool isExcludedCodeDirectory(const QString& path);
StudioQueryProperties codeFileQueryProperties(const CodeFileEntry& file);
CodeFilesResult listCodeFiles(const CodeFilesRequest& request);

} // namespace vibestudio
