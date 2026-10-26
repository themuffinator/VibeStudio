#include "cli/level_view_filters.h"
#include "core/entity_builtin_catalogue.h"
#include "core/entity_definitions.h"
#include "core/level_filter_tags.h"
#include "core/level_material_paint.h"
#include "core/level_view_filters.h"
#include "core/package_archive.h"
#include "core/project_manifest.h"
#include "core/studio_settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli {
namespace { struct Text { Q_DECLARE_TR_FUNCTIONS(LevelViewFiltersCli) }; }

LevelViewFiltersCliResult runLevelViewFilters(const QStringList& arguments)
{
	LevelViewFiltersCliResult result; result.payload.insert("mapWritten",false); result.payload.insert("queryOnly",true);
	const auto fail = [&](int code, const QString& message) { result.exitCode = code; result.error = message; return result; };
	const QSet<QString> flags{"--cli","--json","--quiet","--verbose","--saved","--invert","--objects","--classes"};
	const QSet<QString> options{"--off","--class","--tags","--project","--definitions","--engine","--map","--package","--settings-file","--locale","--catalog-root"};
	QSet<QString> seen; QHash<QString,QStringList> values; QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i]; if (!arg.startsWith('-')) { positional.append(arg); continue; }
		const auto equal = arg.indexOf('='); const auto name = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(name) && name != "--off" && name != "--class") { return fail(2,Text::tr("Repeated option: %1.").arg(name)); }
		seen.insert(name);
		if (flags.contains(name) && equal < 0) { continue; }
		if (!options.contains(name)) { return fail(2,Text::tr("Unexpected option: %1.").arg(arg)); }
		QString value;
		if (equal >= 0) { value = arg.mid(equal+1); }
		else if (i+1 < arguments.size() && !arguments[i+1].startsWith('-')) { value = arguments[++i]; }
		if (value.trimmed().isEmpty()) { return fail(2,Text::tr("Missing value for %1.").arg(name)); }
		values[name].append(value);
		if (values[name].size() > 256) { return fail(2,Text::tr("At most 256 values are allowed for %1.").arg(name)); }
	}
	if (positional.size() != 3 || !QStringList{"map","maps","level"}.contains(positional[0]) || positional[1] != "filters") {
		return fail(2,Text::tr("Expected map filters <map>, with optional --off <kinds>, --class <classes>, --classes, --tags <file>, --project <folder>, --definitions <file>, --saved, --invert, --objects and --package <path>."));
	}
	LevelMapDocument document; QString error;
	if (!loadLevelMap({QFileInfo(positional[2]).absoluteFilePath(),values.value("--map").value(0),values.value("--engine").value(0)},&document,&error)) {
		return fail(4,error);
	}
	// The material kinds read a Quake III map's shaders from --package, the
	// way the View tab reads the open package's.
	LevelViewFilterMaterials materials;
	const QString packagePath = values.value("--package").value(0);
	if (!packagePath.isEmpty()) {
		PackageArchive package;
		if (!package.load(QFileInfo(packagePath).absoluteFilePath(),&error)) { return fail(4,error); }
		if (document.format == LevelMapFormat::Quake3Map) { materials = scanLevelViewFilterMaterials(package); }
		else { materials.sourcePath = package.sourcePath(); }
	}
	// Tags a project's manifest or a tag file define, as the View tab reads
	// them, and the classes of the definitions and the map.
	LevelFilterTagSet tags;
	if (values.contains("--project")) {
		ProjectManifest manifest;
		const QString root = QFileInfo(values.value("--project").first()).absoluteFilePath();
		if (!loadProjectManifest(root,&manifest,&error)) { return fail(4,error); }
		mergeLevelFilterTags(&tags,levelFilterTagsForProject(manifest));
	}
	if (values.contains("--tags")) {
		LevelFilterTagSet fromFile;
		if (!loadLevelFilterTagFile(QFileInfo(values.value("--tags").first()).absoluteFilePath(),&fromFile,&error)) { return fail(4,error); }
		mergeLevelFilterTags(&tags,fromFile);
	}
	const bool quakeFamily = levelFilterTagsApply(document);
	// Without --definitions the map's game brings its built-in classes, as
	// the View tab does without project definitions.
	EntityDefinitionCatalogue catalogue = quakeFamily ? builtinEntityDefinitions(builtinEntityGameForMap(document)) : EntityDefinitionCatalogue();
	if (values.contains("--definitions")) {
		catalogue = loadEntityDefinitions({QFileInfo(values.value("--definitions").first()).absoluteFilePath()});
		if (catalogue.isEmpty()) { return fail(4,Text::tr("No entity classes were read from %1.").arg(QDir::toNativeSeparators(values.value("--definitions").first()))); }
	}
	const QVector<LevelFilterClass> classes = levelFilterClasses(document,catalogue);
	const QVector<LevelViewFilter> filters = levelViewFilters(document);
	QSet<QString> offered;
	for (const auto& filter : filters) { offered.insert(filter.id); }
	if (quakeFamily) { for (const auto& tag : std::as_const(tags.tags)) { offered.insert(tag.id); } }
	// The View tab's saved list keeps kinds for other formats; only the ones
	// this map offers count here, as they do in the views.
	QStringList off;
	if (seen.contains("--saved")) {
		for (const auto& id : levelViewFiltersFromSaved(StudioSettings().shellLayoutState(levelViewFiltersSettingsKey()))) {
			if (offered.contains(id) && !off.contains(id)) { off.append(id); }
		}
	}
	for (const auto& list : values.value("--off")) {
		for (const auto& part : list.split(',',Qt::SkipEmptyParts)) {
			const auto id = part.trimmed();
			if (!offered.contains(id) && !(quakeFamily && levelFilterClassForId(id))) {
				QStringList known; for (const auto& filter : filters) { known.append(filter.id); }
				if (quakeFamily) { known += tags.ids(); }
				return fail(2,known.isEmpty() ? Text::tr("This map's format has no view filters.")
					: Text::tr("This map has no %1 filter. Choose from: %2.").arg(id,known.join(QStringLiteral(", "))));
			}
			if (!off.contains(id)) { off.append(id); }
		}
	}
	for (const auto& list : values.value("--class")) {
		for (const auto& part : list.split(',',Qt::SkipEmptyParts)) {
			const auto id = levelFilterClassId(part.trimmed());
			if (!quakeFamily || !levelFilterClassForId(id)) {
				return fail(2,quakeFamily ? Text::tr("%1 is not a class name a map can hold.").arg(part.trimmed())
					: Text::tr("Class filters are for Quake-family maps."));
			}
			if (!off.contains(id)) { off.append(id); }
		}
	}
	const bool invert = seen.contains("--invert");
	// Invert flips the built-in kinds and the tags; class filters stay.
	if (invert) { off = levelViewFiltersInverted(document,off); if (quakeFamily) { off = levelFilterTagsInverted(tags,off); } }
	const bool objects = seen.contains("--objects");
	// Faces as map select-textured prints them: face:brushId:faceNumber, one-based.
	const auto faceSelector = [](const LevelSurfaceFace& face) {
		return levelMaterialTargetId({LevelMaterialKind::BrushFace,face.brushId,face.faceIndex});
	};
	QJsonArray entries;
	for (const auto& filter : filters) {
		const auto matches = levelViewFilterMatches(document,filter.id,materials);
		// Caulk also hides caulk faces one at a time on other brushes.
		const auto faces = levelViewFilterFaceMatches(document,filter.id,materials);
		QJsonObject entry{{"id",filter.id},{"title",filter.title},{"description",filter.description},
			{"count",static_cast<int>(matches.size())},{"faceCount",static_cast<int>(faces.size())},{"shown",!off.contains(filter.id)},
			{"command",levelViewFilterCommandId(filter.id)},{"readsShaders",levelViewFilterReadsMaterials(filter.id) && document.format == LevelMapFormat::Quake3Map}};
		if (objects) {
			QJsonArray selectors; for (const auto& ref : matches) { selectors.append(levelMapSelectionRefId(ref)); }
			entry.insert("objects",selectors);
			QJsonArray faceSelectors; for (const auto& face : faces) { faceSelectors.append(faceSelector(face)); }
			entry.insert("faces",faceSelectors);
		}
		entries.append(entry);
	}
	QJsonArray tagEntries;
	for (const auto& tag : std::as_const(tags.tags)) {
		const auto matches = quakeFamily ? levelFilterTagMatches(document,tag,materials) : QVector<LevelMapSelectionRef>();
		const auto faces = quakeFamily ? levelFilterTagFaceMatches(document,tag,materials) : QVector<LevelSurfaceFace>();
		static const char* const matchNames[] {"texture","surfaceparm","contentflag","surfaceflag","classname"};
		QStringList unresolved; const auto bits = levelFilterTagFlagBits(tag,document,&unresolved);
		QJsonObject entry{{"id",tag.id},{"name",tag.name},{"match",QString::fromLatin1(matchNames[static_cast<int>(tag.match)])},
			{"patterns",QJsonArray::fromStringList(tag.patterns)},{"flags",QJsonArray::fromStringList(tag.flagNames)},{"flagBits",static_cast<qint64>(bits)},
			{"unresolvedFlags",QJsonArray::fromStringList(unresolved)},{"source",tag.source},{"description",levelFilterTagDescription(tag)},
			{"count",static_cast<int>(matches.size())},{"faceCount",static_cast<int>(faces.size())},{"shown",!off.contains(tag.id)},
			{"command",levelFilterTagCommandId(tag.id)},{"applies",quakeFamily}};
		if (objects) {
			QJsonArray selectors; for (const auto& ref : matches) { selectors.append(levelMapSelectionRefId(ref)); }
			entry.insert("objects",selectors);
			QJsonArray faceSelectors; for (const auto& face : faces) { faceSelectors.append(faceSelector(face)); }
			entry.insert("faces",faceSelectors);
		}
		tagEntries.append(entry);
	}
	QJsonArray classEntries;
	QStringList hiddenClasses;
	for (const auto& entry : classes) {
		const bool shown = !off.contains(entry.id);
		if (!shown) { hiddenClasses.append(entry.className); }
		if (!seen.contains("--classes") && shown) { continue; }
		QJsonObject object{{"class",entry.className},{"id",entry.id},{"group",entry.group},
			{"kind",entry.kind == EntityClassKind::Brush ? QStringLiteral("brush") : QStringLiteral("point")},{"count",entry.count},
			{"defined",entry.defined},{"shown",shown},{"command",levelFilterClassCommandId(entry.className)}};
		if (objects) {
			QJsonArray selectors; for (const auto& ref : levelFilterClassMatches(document,entry.className)) { selectors.append(levelMapSelectionRefId(ref)); }
			object.insert("objects",selectors);
		}
		classEntries.append(object);
	}
	const auto hidden = levelViewFilterHidden(document,off,materials,tags);
	QStringList filtered;
	for (const auto& ref : hidden.objects) { filtered.append(levelMapSelectionRefId(ref)); }
	QStringList filteredFaces;
	for (const auto& face : hidden.faces) { filteredFaces.append(faceSelector(face)); }
	result.payload.insert("tags",tagEntries);
	result.payload.insert("tagSources",QJsonArray::fromStringList(tags.sources));
	result.payload.insert("tagWarnings",QJsonArray::fromStringList(tags.warnings));
	result.payload.insert("classes",classEntries);
	result.payload.insert("classCount",static_cast<int>(classes.size()));
	result.payload.insert("filters",entries);
	result.payload.insert("off",QJsonArray::fromStringList(off));
	result.payload.insert("inverted",invert);
	result.payload.insert("saved",seen.contains("--saved"));
	result.payload.insert("settingsKey",levelViewFiltersSettingsKey());
	result.payload.insert("filteredObjects",QJsonArray::fromStringList(filtered));
	result.payload.insert("filteredCount",static_cast<int>(filtered.size()));
	result.payload.insert("filteredFaces",QJsonArray::fromStringList(filteredFaces));
	result.payload.insert("filteredFaceCount",static_cast<int>(filteredFaces.size()));
	result.payload.insert("source",document.sourcePath); result.payload.insert("sourceSha256",QString::fromLatin1(document.sourceContentHash.toHex()));
	QJsonObject shaderReport{{"package",materials.sourcePath},{"read",!packagePath.isEmpty() && document.format == LevelMapFormat::Quake3Map},
		{"scripts",materials.scriptCount},{"shaders",materials.scriptedShaderCount},{"names",static_cast<int>(materials.shaders.size())},
		{"complete",materials.complete},{"warnings",QJsonArray::fromStringList(materials.warnings)}};
	result.payload.insert("shaders",shaderReport);
	result.lines.append(Text::tr("View filters query: the map and the View tab's saved filters are unchanged."));
	if (document.format == LevelMapFormat::Quake3Map) {
		const int shaders = materials.scriptedShaderCount;
		result.lines.append(packagePath.isEmpty()
			? Text::tr("No --package: the material kinds go by texture names, and Translucent finds nothing on this map.")
			: Text::tr("%n shader(s) from %1 decide the material kinds; textures the package does not hold go by name.",nullptr,shaders)
				.arg(QDir::toNativeSeparators(materials.sourcePath)));
		for (const auto& warning : std::as_const(materials.warnings)) { result.lines.append(QStringLiteral("  ")+warning); }
	} else if (!packagePath.isEmpty()) {
		result.lines.append(Text::tr("Only Quake III maps read shader scripts; this map's kinds go by its face flags and texture names."));
	}
	if (filters.isEmpty()) { result.lines.append(Text::tr("This map's format has no view filters.")); }
	if (!tags.isEmpty()) {
		const int tagCount = static_cast<int>(tags.tags.size());
		result.lines.append(Text::tr("%n tag(s) from game or project data:",nullptr,tagCount));
		for (const auto& value : std::as_const(tagEntries)) {
			const auto object = value.toObject();
			const int count = object.value("count").toInt();
			const auto state = object.value("shown").toBool() ? Text::tr("shown") : Text::tr("hidden");
			result.lines.append(QStringLiteral("  ")+Text::tr("%1 (%2): %3, %n object(s)",nullptr,count).arg(object.value("name").toString(),object.value("id").toString(),state));
			const int faceCount = object.value("faceCount").toInt();
			if (faceCount > 0) { result.lines.append(QStringLiteral("    ")+Text::tr("It also names %n face(s) on other brushes.",nullptr,faceCount)); }
			if (!object.value("unresolvedFlags").toArray().isEmpty()) {
				QStringList names; for (const auto& name : object.value("unresolvedFlags").toArray()) { names.append(name.toString()); }
				result.lines.append(QStringLiteral("    ")+Text::tr("This map's game has no flags named %1.").arg(names.join(QStringLiteral(", "))));
			}
		}
	}
	for (const auto& warning : std::as_const(tags.warnings)) { result.lines.append(QStringLiteral("  ")+warning); }
	if (!hiddenClasses.isEmpty()) {
		const int hiddenClassCount = static_cast<int>(hiddenClasses.size());
		result.lines.append(Text::tr("%n class filter(s) switched off: %1.",nullptr,hiddenClassCount).arg(hiddenClasses.join(QStringLiteral(", "))));
	}
	if (seen.contains("--classes")) {
		const int classCount = static_cast<int>(classes.size());
		result.lines.append(Text::tr("%n entity class(es) to filter:",nullptr,classCount));
		for (const auto& entry : classes) {
			result.lines.append(QStringLiteral("  ")+Text::tr("%1: %n in this map",nullptr,entry.count).arg(entry.className));
		}
	}
	for (const auto& entry : std::as_const(entries)) {
		const auto object = entry.toObject();
		const int count = object.value("count").toInt();
		const auto state = object.value("shown").toBool() ? Text::tr("shown") : Text::tr("hidden");
		result.lines.append(Text::tr("%1 (%2): %3, %n object(s)",nullptr,count).arg(object.value("title").toString(),object.value("id").toString(),state));
		const int faceCount = object.value("faceCount").toInt();
		if (faceCount > 0) {
			result.lines.append(QStringLiteral("  ")+Text::tr("It also hides %n face(s) on other brushes in the camera.",nullptr,faceCount));
		}
		if (objects) {
			for (const auto& selector : object.value("objects").toArray()) { result.lines.append(QStringLiteral("  ")+selector.toString()); }
			for (const auto& selector : object.value("faces").toArray()) { result.lines.append(QStringLiteral("  ")+selector.toString()); }
		}
	}
	const int hiddenCount = static_cast<int>(filtered.size());
	result.lines.append(Text::tr("The switched-off kinds hide %n object(s) from the views.",nullptr,hiddenCount));
	const int hiddenFaceCount = static_cast<int>(filteredFaces.size());
	if (hiddenFaceCount > 0) {
		result.lines.append(Text::tr("They also leave %n face(s) on other brushes out of the camera.",nullptr,hiddenFaceCount));
	}
	return result;
}
} // namespace vibestudio::cli
