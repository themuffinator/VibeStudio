#include "core/language_rename.h"
#include "core/language_server.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
bool integer(const QJsonValue& value, int* result)
{
	const double n = value.toDouble(-1);
	if (!value.isDouble() || !std::isfinite(n) || n < 0 || n >= std::numeric_limits<int>::max() || std::floor(n) != n) { return false; }
	*result = int(n); return true;
}
}

bool validLanguageRenameName(const QString& name)
{
	return !name.trimmed().isEmpty() && name.size() <= 1024 && name.isValidUtf16()
		&& std::none_of(name.cbegin(), name.cend(), [](QChar ch) { return ch.isNull() || ch.category() == QChar::Other_Control || ch == QChar(0x2028) || ch == QChar(0x2029); });
}

// LSP 3.17 prepareRename, rename and WorkspaceEdit message facts, original
// implementation. Microsoft specification (CC-BY-4.0), reviewed 2026-10-04:
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_rename
LanguageRenamePreparation parseLanguageRenamePreparation(const QJsonValue& response, const QString& source, int at)
{
	LanguageRenamePreparation result;
	const auto fail = [&](const QString& why) { result.error = why; return result; };
	if (response.isNull()) { return fail(QCoreApplication::translate("LanguageRename", "The language server cannot rename the symbol at this position.")); }
	const auto object = response.toObject();
	const auto range = object.contains(QStringLiteral("range")) ? object.value(QStringLiteral("range")).toObject() : object;
	const auto offset = [&](const QJsonValue& point) {
		int line = 0, character = 0; const auto p = point.toObject();
		return integer(p.value(QStringLiteral("line")), &line) && integer(p.value(QStringLiteral("character")), &character)
			? languageSourceOffset(source, line, character) : -1;
	};
	const int first = offset(range.value(QStringLiteral("start"))), last = offset(range.value(QStringLiteral("end")));
	if (first < 0 || last <= first || at < first || at > last || last - first > 1024) { return fail(QCoreApplication::translate("LanguageRename", "The language server returned an invalid rename range.")); }
	result.placeholder = object.contains(QStringLiteral("placeholder")) ? object.value(QStringLiteral("placeholder")).toString() : source.mid(first, last - first);
	if (!validLanguageRenameName(result.placeholder)) { return fail(QCoreApplication::translate("LanguageRename", "The language server returned an invalid rename placeholder.")); }
	result.offset = first; result.length = last - first; return result;
}

LanguageWorkspaceEditRequest languageRenameWorkspaceRequest(const LanguageRenameRequest& request)
{
	LanguageWorkspaceEditRequest plan;
	plan.rootPath = request.rootPath; plan.provider = request.provider; plan.rename = true;
	plan.findText = request.symbol; plan.replaceText = request.newName; plan.error = request.rename.error;
	plan.workspaceEdit = request.rename.workspaceEdit; plan.buffers = request.buffers; plan.versions = request.versions;
	plan.isCancelled = request.isCancelled; plan.progress = request.progress;
	if (plan.error.isEmpty() && !validLanguageRenameName(request.newName)) { plan.error = QCoreApplication::translate("LanguageRename", "Rename requires a valid new name."); }
	return plan;
}

AssetTextSearchReport prepareLanguageRename(const LanguageRenameRequest& request)
{
	return prepareLanguageWorkspaceEdit(languageRenameWorkspaceRequest(request));
}

QByteArray languageRenamePlanHash(const AssetTextSearchReport& report)
{
	return report.semanticRename ? languageWorkspaceEditPlanHash(report) : QByteArray();
}

} // namespace vibestudio
