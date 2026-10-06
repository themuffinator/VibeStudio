#pragma once

#include "core/project_text_search.h"
#include "core/language_workspace_edit.h"
#include <QHash>
#include <QJsonValue>

namespace vibestudio {

struct LanguageRenamePreparation {
	QString error, placeholder;
	int offset = -1, length = 0, version = 0;
};

struct LanguageRename {
	QString error;
	QJsonValue workspaceEdit;
	int version = 0;
};

struct LanguageRenameRequest {
	QString rootPath, symbol, newName, provider;
	LanguageRename rename;
	QVector<AssetTextBuffer> buffers;
	// Wire versions captured with the immutable buffers before requesting edits.
	QHash<QString, int> versions;
	std::function<bool()> isCancelled;
	std::function<void(int, int)> progress;
};

bool validLanguageRenameName(const QString& name);
LanguageRenamePreparation parseLanguageRenamePreparation(const QJsonValue& response, const QString& source, int offset);
// All edits must validate before any are offered for review. Never writes.
AssetTextSearchReport prepareLanguageRename(const LanguageRenameRequest& request);
LanguageWorkspaceEditRequest languageRenameWorkspaceRequest(const LanguageRenameRequest& request);
// Stable across CLI invocations: binds every source, destination byte sequence,
// edit range and replacement to the project/name/provider shown in the preview.
QByteArray languageRenamePlanHash(const AssetTextSearchReport& report);

} // namespace vibestudio
