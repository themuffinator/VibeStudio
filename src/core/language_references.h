#pragma once

#include "core/language_server.h"
#include "core/project_text_search.h"

namespace vibestudio {

struct LanguageReferenceRequest {
	QString rootPath, symbol, provider;
	LanguageReferences references;
	QVector<AssetTextBuffer> buffers;
	std::function<bool()> isCancelled;
	std::function<void(int files, int matches)> progress;
};

// Reads only referenced project files through the shared text codec. Live
// snapshots override disk, including deleted open files. Returns navigation
// matches with hashes and exact ranges; never proposes or writes replacements.
// Run off the UI thread. Sources total at most 64 MiB; previews at most 4 MiB.
AssetTextSearchReport prepareLanguageReferences(const LanguageReferenceRequest& request);

} // namespace vibestudio
