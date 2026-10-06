#include "cli/workspace.h"
#include "core/asset_formats.h"
#include "core/workspace_document.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace vibestudio::cli {
WorkspaceCliResult runWorkspaceCommand(const QStringList& arguments)
{
	const auto fail = [](int code, const char* text) {
		return WorkspaceCliResult{code, QCoreApplication::translate("VibeStudioWorkspaceCli", text), {}, {}};
	};
	const QSet<QString> globals {"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--dry-run", "--from-session"};
	const QSet<QString> options {"--project", "--package", "--map", "--map-name", "--code", "--current-code", "--active-module", "--module"};
	QStringList positional, codeFiles; QHash<QString, QString> values; QSet<QString> seen;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('='); const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key) && key != QStringLiteral("--code")) { return fail(2, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "Repeated workspace or format option.")); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key) && !globals.contains(key)) { return fail(2, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "Unknown workspace or format option.")); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal + 1); }
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith('-')) { value = arguments[++i]; }
		if (value.isEmpty()) { return fail(2, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "Missing workspace or format option value.")); }
		if (key == QStringLiteral("--code")) { codeFiles << value; } else { values.insert(key, value); }
	}
	const auto family = positional.value(0), action = positional.value(1);
	const bool catalog = family == QStringLiteral("asset") && action == QStringLiteral("formats");
	const bool route = family == QStringLiteral("asset") && action == QStringLiteral("route");
	const bool create = family == QStringLiteral("workspace") && action == QStringLiteral("create");
	const bool inspect = family == QStringLiteral("workspace") && action == QStringLiteral("inspect");
	if ((!catalog && !route && !create && !inspect) || positional.size() != (catalog ? 2 : 3)) {
		return fail(2, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "Expected workspace create|inspect <file>, asset formats, or asset route <path>."));
	}
	for (const auto& key : seen) {
		if (globals.contains(key) || QStringList{"--cli", "--json", "--quiet", "--verbose"}.contains(key)) { continue; }
		if ((catalog && key == QStringLiteral("--module")) || (create && key != QStringLiteral("--module"))) { continue; }
		return fail(2, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "This option does not apply to the selected operation."));
	}
	WorkspaceCliResult result;
	if (catalog) {
		const auto module = values.value(QStringLiteral("--module"));
		if (!module.isEmpty() && !workspaceModuleIds().contains(module)) { return fail(2, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "Unknown module identifier.")); }
		QJsonArray formats;
		for (const auto& format : assetFormats()) {
			if (!module.isEmpty() && format.module != module) { continue; }
			formats.append(assetFormatJson(format));
			result.lines << QStringLiteral("%1  [%2]  read=%3  export=%4").arg(format.id, format.module,
				format.runtimeRead ? format.readCapability : QStringLiteral("unavailable"), format.exportProfiles.join(','));
		}
		result.payload = {{"formats", formats}}; return result;
	}
	if (route) {
		const auto* descriptor = assetFormatForPath(positional[2]);
		const auto kind = assetPreviewKindForPath(positional[2]);
		const auto module = descriptor ? descriptor->module : kind == AssetPreviewKind::Text ? QStringLiteral("code") : QStringLiteral("packages");
		result.payload = {{"path", positional[2]}, {"module", module}, {"recognized", descriptor != nullptr || kind == AssetPreviewKind::Text},
			{"format", descriptor ? QJsonValue(assetFormatJson(*descriptor)) : QJsonValue(QJsonValue::Null)}};
		result.lines << module; return result;
	}
	const auto path = QFileInfo(positional[2]).absoluteFilePath();
	WorkspaceDocument document; QByteArray revision; QString error;
	if (inspect) {
		if (!QFileInfo::exists(path)) { return fail(3, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "Workspace file not found.")); }
		if (!readWorkspaceDocument(path, &document, &revision, &error)) { return {4, error, {}, {}}; }
	} else {
		if (seen.contains(QStringLiteral("--from-session"))) {
			StudioSettings settings; const auto session = settings.lastSession();
			document.projectPath = settings.currentProjectPath(); document.packagePath = session.packagePath;
			document.mapPath = session.mapPath; document.mapName = session.mapName; document.codeFiles = session.codeFiles; document.currentCodeFile = session.currentCodeFile;
		}
		const auto setPath = [&](const char* key, QString* target) { if (values.contains(QLatin1String(key))) { *target = QFileInfo(values.value(QLatin1String(key))).absoluteFilePath(); } };
		setPath("--project", &document.projectPath); setPath("--package", &document.packagePath); setPath("--map", &document.mapPath); setPath("--current-code", &document.currentCodeFile);
		if (values.contains(QStringLiteral("--map-name"))) { document.mapName = values.value(QStringLiteral("--map-name")); }
		if (values.contains(QStringLiteral("--active-module"))) { document.activeModule = values.value(QStringLiteral("--active-module")); }
		for (const auto& code : codeFiles) { const auto file = QFileInfo(code).absoluteFilePath(); if (!document.codeFiles.contains(file)) { document.codeFiles << file; } }
		if (QFileInfo::exists(path)) {
			if (!seen.contains(QStringLiteral("--overwrite"))) { return fail(2, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "The workspace exists. Use --overwrite after reviewing it.")); }
			WorkspaceDocument existing;
			if (!readWorkspaceDocument(path, &existing, &revision, &error)) { return {4, error, {}, {}}; }
			document.extensions = existing.extensions;
		}
		WorkspaceDocument checked;
		if (!parseWorkspaceDocument(QJsonDocument(workspaceDocumentJson(document, path)).toJson(), path, &checked, &error)) { return {2, error, {}, {}}; }
		if (QFileInfo(path).suffix().compare(QStringLiteral("vibeworkspace"), Qt::CaseInsensitive) != 0 || !QDir(QFileInfo(path).absolutePath()).exists() || QFileInfo(path).isSymLink()) {
			return fail(2, QT_TRANSLATE_NOOP("VibeStudioWorkspaceCli", "Choose a .vibeworkspace file in an existing folder, with no symbolic-link destination."));
		}
		if (!seen.contains(QStringLiteral("--dry-run")) && !writeWorkspaceDocument(path, document, revision, &revision, &error)) { return {4, error, {}, {}}; }
	}
	result.payload = {{"path", path}, {"workspace", workspaceDocumentJson(document, path)}, {"missingReferences", QJsonArray::fromStringList(workspaceMissingReferences(document))},
		{"revision", QString::fromLatin1(revision.toHex())}, {"dryRun", seen.contains(QStringLiteral("--dry-run"))}};
	result.lines << path << QCoreApplication::translate("VibeStudioWorkspaceCli", "Active module: %1").arg(document.activeModule);
	for (const auto& missing : workspaceMissingReferences(document)) { result.lines << QCoreApplication::translate("VibeStudioWorkspaceCli", "Missing: %1").arg(missing); }
	return result;
}
} // namespace vibestudio::cli
