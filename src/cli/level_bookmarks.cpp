#include "cli/level_bookmarks.h"
#include "core/level_navigation.h"
#include "core/level_map.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSet>

namespace vibestudio::cli {
LevelBookmarksCliResult runLevelBookmarks(const QStringList& arguments)
{
	const auto failure = [](int code, const QString& message) { return LevelBookmarksCliResult{code, message, {}, {}}; };
	const QSet<QString> globals {"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> flags {"--cli", "--json", "--quiet", "--verbose", "--overwrite", "--replace"};
	const QSet<QString> options {"--map-name", "--input", "--output", "--id", "--name"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const auto equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key)) { return failure(2, QCoreApplication::translate("LevelBookmarksCli", "Repeated option: %1").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key) && !globals.contains(key)) { return failure(2, QCoreApplication::translate("LevelBookmarksCli", "Unexpected option: %1").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal + 1); }
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return failure(2, QCoreApplication::translate("LevelBookmarksCli", "Missing value for %1.").arg(key)); }
		values.insert(key, value);
	}
	if (positional.size() != 4) { return failure(2, QCoreApplication::translate("LevelBookmarksCli", "Expected editor bookmarks list|import|export|rename|remove <map> with operation options.")); }
	const auto action = positional[2];
	const auto map = positional[3];
	QSet<QString> allowed {"--map-name"};
	QSet<QString> required;
	if (action == QStringLiteral("import")) { allowed << "--input" << "--replace"; required << "--input"; }
	else if (action == QStringLiteral("export")) { allowed << "--output" << "--overwrite"; required << "--output"; }
	else if (action == QStringLiteral("rename")) { allowed << "--id" << "--name"; required << "--id" << "--name"; }
	else if (action == QStringLiteral("remove")) { allowed << "--id"; required << "--id"; }
	else if (action != QStringLiteral("list")) { return failure(2, QCoreApplication::translate("LevelBookmarksCli", "Expected list, import, export, rename or remove.")); }
	for (const auto& key : seen) {
		if (!globals.contains(key) && key != QStringLiteral("--cli") && key != QStringLiteral("--json") &&
			key != QStringLiteral("--quiet") && key != QStringLiteral("--verbose") && !allowed.contains(key)) {
			return failure(2, QCoreApplication::translate("LevelBookmarksCli", "Option %1 does not apply to %2.").arg(key, action));
		}
	}
	for (const auto& key : required) { if (!values.contains(key)) { return failure(2, QCoreApplication::translate("LevelBookmarksCli", "Missing required option %1.").arg(key)); } }
	if (!QFileInfo(map).isFile()) { return failure(3, QCoreApplication::translate("LevelBookmarksCli", "The map file does not exist.")); }
	const bool wad = QFileInfo(map).suffix().compare(QStringLiteral("wad"), Qt::CaseInsensitive) == 0;
	if (wad != values.contains(QStringLiteral("--map-name"))) {
		return failure(2, QCoreApplication::translate("LevelBookmarksCli", "WAD files require --map-name; text maps do not accept it."));
	}
	LevelMapLoadRequest request;
	request.path = map;
	request.mapName = values.value(QStringLiteral("--map-name"));
	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) { return failure(4, error); }
	const auto store = levelBookmarkStorePath(document.sourcePath, wad ? document.mapName : QString());
	LevelViewBookmarks views;
	QByteArray revision;
	if (!readLevelBookmarks(store, &views, &revision, &error)) { return failure(4, error); }
	const bool writing = action == QStringLiteral("import") || action == QStringLiteral("rename") || action == QStringLiteral("remove");
	if (writing && StudioSettings().isReadOnly()) { return failure(1, QCoreApplication::translate("LevelBookmarksCli", "The settings store is read-only.")); }
	if (action == QStringLiteral("import")) {
		if (!views.isEmpty() && !seen.contains(QStringLiteral("--replace"))) {
			return failure(2, QCoreApplication::translate("LevelBookmarksCli", "This map has saved views. Use --replace to replace the complete list."));
		}
		const auto input = values.value(QStringLiteral("--input"));
		QByteArray importedRevision;
		if (!QFileInfo(input).isFile()) { return failure(3, QCoreApplication::translate("LevelBookmarksCli", "The level views input file does not exist.")); }
		if (!readLevelBookmarks(input, &views, &importedRevision, &error)) { return failure(4, error); }
	} else if (action == QStringLiteral("rename") || action == QStringLiteral("remove")) {
		const auto id = values.value(QStringLiteral("--id"));
		int index = -1;
		for (int i = 0; i < views.size(); ++i) { if (views[i].id == id) { index = i; break; } }
		if (index < 0) { return failure(3, QCoreApplication::translate("LevelBookmarksCli", "The saved view ID was not found.")); }
		if (action == QStringLiteral("remove")) { views.removeAt(index); }
		else { views[index].name = values.value(QStringLiteral("--name")).trimmed(); }
	}
	if (writing && !writeLevelBookmarks(store, views, revision, &revision, &error)) { return failure(4, error); }
	if (action == QStringLiteral("export")) {
		const auto output = values.value(QStringLiteral("--output"));
		if (QFileInfo::exists(output) && !seen.contains(QStringLiteral("--overwrite"))) { return failure(2, QCoreApplication::translate("LevelBookmarksCli", "The output exists. Use --overwrite to replace a level views file.")); }
		LevelViewBookmarks existing;
		QByteArray outputRevision;
		if (!readLevelBookmarks(output, &existing, &outputRevision, &error) || !writeLevelBookmarks(output, views, outputRevision, nullptr, &error)) {
			return failure(4, error);
		}
	}
	LevelBookmarksCliResult result;
	result.payload = {{"operation", action}, {"map", document.sourcePath}, {"mapName", wad ? document.mapName : QString()},
		{"storage", store}, {"revision", QString::fromLatin1(revision.toHex())}, {"views", levelBookmarksJson(views)}};
	result.lines << QCoreApplication::translate("LevelBookmarksCli", "Saved views: %1").arg(views.size());
	for (const auto& view : views) { result.lines << QStringLiteral("%1  %2  [%3]").arg(view.id, view.name, levelViewLayoutId(view.view.layout)); }
	return result;
}
} // namespace vibestudio::cli
