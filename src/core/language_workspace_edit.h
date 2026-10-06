#pragma once

#include "core/project_text_search.h"
#include <QHash>
#include <QJsonValue>

namespace vibestudio {

struct LanguageWorkspaceEditRequest {
	QString rootPath, provider, title, findText, replaceText, error;
	bool rename = false;
	QJsonValue workspaceEdit;
	QVector<AssetTextBuffer> buffers;
	QHash<QString, int> versions;
	std::function<bool()> isCancelled;
	std::function<void(int, int)> progress;
};

// Shared all-or-nothing validation and preview for client-invoked language edits.
// Existing project replacement services own actual writes and live-buffer undo.
AssetTextSearchReport prepareLanguageWorkspaceEdit(const LanguageWorkspaceEditRequest& request);
QByteArray languageWorkspaceEditPlanHash(const AssetTextSearchReport& report);

} // namespace vibestudio
