#include "cli/level_smart_tags.h"
#include "core/entity_builtin_catalogue.h"
#include "core/level_scene.h"
#include "core/level_smart_tags.h"
#include "core/package_archive.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelSmartTagsCli) }; }

LevelSmartTagsCliResult runLevelSmartTags(const QStringList& arguments)
{
	LevelSmartTagsCliResult result;
	const auto fail = [&](int code, const QString& message) { result.exitCode = code; result.error = message; return result; };
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose", "--disable", "--choices", "--dry-run", "--overwrite"};
	const QSet<QString> options{"--tags", "--project", "--tag", "--choice", "--object", "--face", "--package", "--definitions", "--output", "--context", "--engine", "--map-name", "--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QStringList> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString arg = arguments[i];
		if (!arg.startsWith(QStringLiteral("--"))) { positional << arg; continue; }
		const auto equal = arg.indexOf('=');
		const QString name = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(name) && name != QLatin1String("--object") && name != QLatin1String("--face")) { return fail(2, Text::tr("Repeated option: %1.").arg(name)); }
		seen.insert(name);
		if (flags.contains(name) && equal < 0) { continue; }
		if (!options.contains(name)) { return fail(2, Text::tr("Unexpected option: %1.").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal + 1); }
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith(QStringLiteral("--"))) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return fail(2, Text::tr("Missing value for %1.").arg(name)); }
		values[name] << value;
	}
	if (positional.size() != 3 || values.value("--tag").isEmpty() || (!values.contains("--tags") && !values.contains("--project"))) {
		return fail(2, Text::tr("Expected map smart-tag <map> --tags <file> (or --project <folder>) --tag <id>, with --choices to list choices, or --object/--face targets and --output to edit. Use --disable to remove flag or classname tags."));
	}
	const bool query = seen.contains("--choices");
	if (!query && values.value("--output").isEmpty()) { return fail(2, Text::tr("Smart-tag changes require --output; --dry-run checks without writing.")); }
	if (query && (seen.contains("--output") || seen.contains("--dry-run") || seen.contains("--overwrite") || seen.contains("--disable"))) {
		return fail(2, Text::tr("--choices only lists names; omit change and output options."));
	}
	QString error;
	LevelMapDocument document;
	if (!loadLevelMap({QFileInfo(positional[2]).absoluteFilePath(), values.value("--map-name").value(0), values.value("--engine").value(0)}, &document, &error)) { return fail(4, error); }
	if (seen.contains("--context") && !openLevelSceneGroup(&document, values.value("--context").first(), &error)) { return fail(4, error); }
	LevelFilterTagSet tags;
	if (values.contains("--project")) {
		ProjectManifest project;
		if (!loadProjectManifest(QFileInfo(values.value("--project").first()).absoluteFilePath(), &project, &error)) { return fail(4, error); }
		mergeLevelFilterTags(&tags, levelFilterTagsForProject(project));
	}
	if (values.contains("--tags")) {
		LevelFilterTagSet loaded;
		if (!loadLevelFilterTagFile(values.value("--tags").first(), &loaded, &error)) { return fail(4, error); }
		mergeLevelFilterTags(&tags, loaded);
	}
	QString id = values.value("--tag").first();
	if (!id.startsWith(QLatin1String("tag:"))) { id = levelFilterTagId(id); }
	const LevelFilterTag* tag = tags.find(id);
	if (!tag) { return fail(2, Text::tr("No loaded tag named %1. Available ids: %2.").arg(id, tags.ids().join(QStringLiteral(", ")))); }
	LevelSmartTagRequest request;
	request.tag = *tag;
	request.enabled = !seen.contains("--disable");
	request.choice = values.value("--choice").value(0);
	QStringList materials = levelMapTextureNames(document);
	if (values.contains("--package")) {
		PackageArchive package;
		if (!package.load(QFileInfo(values.value("--package").first()).absoluteFilePath(), &error)) { return fail(4, error); }
		request.shaders = scanLevelViewFilterMaterials(package);
		materials += levelSmartTagPackageMaterials(package);
		materials += request.shaders.shaders.keys();
	}
	EntityDefinitionCatalogue definitions = builtinEntityDefinitions(builtinEntityGameForMap(document));
	if (values.contains("--definitions")) { definitions = loadEntityDefinitions({values.value("--definitions").first()}); }
	const QStringList choices = levelSmartTagChoices(*tag, materials, definitions, request.shaders);
	result.payload.insert(QStringLiteral("tag"), id);
	result.payload.insert(QStringLiteral("canDisable"), levelSmartTagCanDisable(*tag));
	result.payload.insert(QStringLiteral("choices"), QJsonArray::fromStringList(choices));
	result.payload.insert(QStringLiteral("mapWritten"), false);
	result.payload.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(tags.warnings + request.shaders.warnings));
	if (query) { result.lines = choices; return result; }
	const bool needsChoice = request.enabled && (tag->match == LevelFilterTagMatch::Texture || tag->match == LevelFilterTagMatch::SurfaceParm || tag->match == LevelFilterTagMatch::ClassName);
	if (needsChoice && request.choice.isEmpty()) {
		if (choices.size() != 1) { return fail(2, Text::tr("Give --choice for this tag; --choices lists matching names.")); }
		request.choice = choices.first();
	}
	if (!needsChoice && !request.choice.isEmpty()) { return fail(2, Text::tr("This tag operation takes no --choice.")); }
	QVector<LevelMapSelectionRef> selection;
	for (const QString& list : values.value("--object")) {
		for (const QString& name : list.split(',', Qt::SkipEmptyParts)) {
			const QStringList parts = name.trimmed().split(':');
			bool valid = false;
			const int object = parts.value(1).toInt(&valid);
			const auto kind = levelMapSelectionKindFromId(parts.value(0));
			if (!valid || object < 0 || parts.size() != 2 || (kind != LevelMapSelectionKind::QuakeBrush && kind != LevelMapSelectionKind::QuakePatch && kind != LevelMapSelectionKind::Entity)) {
				return fail(2, Text::tr("--object takes brush:ID, patch:ID or entity:ID."));
			}
			selection << LevelMapSelectionRef{kind, object};
		}
	}
	if (!selection.isEmpty() && !setLevelMapSelection(&document, selection, &error)) { return fail(2, error); }
	if (values.contains("--face")) {
		if (!selection.isEmpty()) { return fail(2, Text::tr("Use --face or --object, not both.")); }
		request.surfaceTargets = true;
		for (const QString& name : values.value("--face")) {
			const auto parts = name.split(':');
			bool a = false, b = false;
			const int brush = parts.value(0).toInt(&a), face = parts.value(1).toInt(&b);
			if (parts.size() != 2 || !a || !b || brush < 0 || face < 1) { return fail(2, Text::tr("--face takes brushId:faceNumber; face numbers start at one.")); }
			request.faces << LevelSurfaceFace{brush, face - 1};
		}
	}
	if (!applyLevelSmartTag(&document, request, &error)) { return fail(4, error); }
	const auto saved = saveLevelMapAs(document, values.value("--output").first(), seen.contains("--dry-run"), seen.contains("--overwrite"));
	if (!saved.succeeded()) { return fail(4, saved.errors.join(QLatin1Char('\n'))); }
	result.payload.insert(QStringLiteral("choice"), request.choice);
	result.payload.insert(QStringLiteral("enabled"), request.enabled);
	result.payload.insert(QStringLiteral("mapWritten"), saved.written);
	result.payload.insert(QStringLiteral("output"), saved.outputPath);
	result.payload.insert(QStringLiteral("dryRun"), saved.dryRun);
	result.lines << Text::tr("Applied the smart-tag change in one undo step.") << levelMapSaveReportText(saved);
	return result;
}
} // namespace vibestudio::cli
