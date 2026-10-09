#include "cli/materials.h"

#include "cli/render.h"

#include "core/material_classic.h"
#include "core/material_eval.h"
#include "core/material_graph.h"
#include "core/material_images.h"
#include "core/material_library.h"
#include "core/material_model.h"
#include "core/material_render.h"
#include "core/material_script.h"
#include "core/package_archive.h"
#include "core/studio_query.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QSaveFile>

#include <algorithm>
#include <climits>
#include <cmath>
#include <initializer_list>
#include <memory>

namespace vibestudio::cli {

namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(MaterialsCli)
};

// CliExitCode values (cli/cli.cpp).
constexpr int kFailure = 1;
constexpr int kUsage = 2;
constexpr int kNotFound = 3;
constexpr int kValidationFailed = 4;
constexpr int kUnavailable = 5;

constexpr qint64 kMaximumEditBytes = 4LL * 1024 * 1024;
constexpr qint64 kMaximumWalBytes = 16LL * 1024 * 1024;
constexpr int kMaximumSheetSide = 16384;

MaterialsCliResult fail(int code, const QString& message)
{
	MaterialsCliResult result;
	result.exitCode = code;
	result.error = message;
	return result;
}

QString native(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

struct Arguments {
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;

	[[nodiscard]] bool has(const QString& key) const { return seen.contains(key); }
	[[nodiscard]] bool hasValue(const QString& key) const { return values.contains(key); }
	[[nodiscard]] QString value(const QString& key) const { return values.value(key); }
};

// Every option appears at most once; values use separate or --option=value
// forms, as in the other CLI modules.
bool parseArguments(const QStringList& arguments, QSet<QString> flags, QSet<QString> options, Arguments* parsed, QString* error)
{
	flags.unite(QSet<QString> {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--quiet"), QStringLiteral("--verbose"),
		QStringLiteral("--no-color")});
	options.unite(QSet<QString> {QStringLiteral("--settings-file"), QStringLiteral("--locale"), QStringLiteral("--catalog-root")});
	for (int i = 1; i < arguments.size(); ++i) {
		const QString argument = arguments.at(i);
		if (!argument.startsWith(QLatin1Char('-')) || argument == QStringLiteral("-")) {
			parsed->positional.append(argument);
			continue;
		}
		const qsizetype equal = argument.indexOf(QLatin1Char('='));
		const QString key = equal < 0 ? argument : argument.left(equal);
		if (parsed->seen.contains(key)) {
			*error = Text::tr("Repeated option: %1.").arg(key);
			return false;
		}
		parsed->seen.insert(key);
		if (flags.contains(key) && equal < 0) {
			continue;
		}
		if (!options.contains(key)) {
			*error = Text::tr("Unknown or invalid option: %1.").arg(key);
			return false;
		}
		const QString value = equal < 0 ? arguments.value(++i) : argument.mid(equal + 1);
		// Negative numbers are values, not options.
		bool numeric = false;
		value.left(value.indexOf(QLatin1Char(','))).toDouble(&numeric);
		if (value.isEmpty() || (value.startsWith(QStringLiteral("--")) && !numeric)) {
			*error = Text::tr("Option %1 requires a value.").arg(key);
			return false;
		}
		parsed->values.insert(key, value);
	}
	return true;
}

QSet<QString> sourceOptions()
{
	return {QStringLiteral("--input"), QStringLiteral("--package"), QStringLiteral("--base"), QStringLiteral("--engine"),
		QStringLiteral("--palette"), QStringLiteral("--material")};
}

bool readNumber(const Arguments& args, const QString& key, double minimum, double maximum, double* out, QString* error)
{
	if (!args.hasValue(key)) {
		return true;
	}
	bool ok = false;
	const double value = args.value(key).toDouble(&ok);
	if (!ok || !std::isfinite(value) || value < minimum || value > maximum) {
		*error = Text::tr("%1 needs a number from %2 to %3.").arg(key, QString::number(minimum), QString::number(maximum));
		return false;
	}
	*out = value;
	return true;
}

bool readInteger(const Arguments& args, const QString& key, int minimum, int maximum, int* out, QString* error)
{
	if (!args.hasValue(key)) {
		return true;
	}
	bool ok = false;
	const int value = args.value(key).toInt(&ok);
	if (!ok || value < minimum || value > maximum) {
		*error = Text::tr("%1 needs a whole number from %2 to %3.").arg(key, QString::number(minimum), QString::number(maximum));
		return false;
	}
	*out = value;
	return true;
}

// "r,g,b", "r,g,b,a" (0-255) or a colour name or #hex.
bool readColor(const QString& text, QColor* color)
{
	const QStringList parts = text.split(QLatin1Char(','));
	if (parts.size() == 3 || parts.size() == 4) {
		int channels[4] = {0, 0, 0, 255};
		for (int i = 0; i < parts.size(); ++i) {
			bool ok = false;
			channels[i] = parts.at(i).trimmed().toInt(&ok);
			if (!ok || channels[i] < 0 || channels[i] > 255) {
				return false;
			}
		}
		*color = QColor(channels[0], channels[1], channels[2], channels[3]);
		return true;
	}
	const QColor named = QColor::fromString(text.trimmed());
	if (!named.isValid()) {
		return false;
	}
	*color = named;
	return true;
}

bool samePath(const QString& a, const QString& b)
{
	const QString left = QFileInfo(a).absoluteFilePath();
	const QString right = QFileInfo(b).absoluteFilePath();
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
	// The usual file systems there ignore case.
	return left.compare(right, Qt::CaseInsensitive) == 0;
#else
	return left == right;
#endif
}

bool readFileBytes(const QString& path, qint64 limit, QByteArray* bytes, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		*error = Text::tr("Could not read %1: %2").arg(native(path), file.errorString());
		return false;
	}
	if (file.size() > limit) {
		const qint64 mebibytes = limit / (1024 * 1024);
		*error = Text::tr("%1 is larger than %2 MiB.").arg(native(path)).arg(mebibytes);
		return false;
	}
	*bytes = file.readAll();
	return true;
}

bool writeBytes(const QString& path, const QByteArray& bytes, QString* error)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		*error = Text::tr("Could not write %1: %2").arg(native(path), file.errorString());
		return false;
	}
	return true;
}

bool writeImage(const QString& path, const QImage& image, QString* error)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		*error = Text::tr("Could not write %1: %2").arg(native(path), file.errorString());
		return false;
	}
	QImageWriter writer(&file, QFileInfo(path).suffix().toLower().toLatin1());
	if (!writer.write(image)) {
		*error = Text::tr("Could not write %1: %2").arg(native(path), writer.errorString());
		file.cancelWriting();
		return false;
	}
	if (!file.commit()) {
		*error = Text::tr("Could not write %1: %2").arg(native(path), file.errorString());
		return false;
	}
	return true;
}

// --- Sources -------------------------------------------------------------

// What a command reads: a package, folder or WAD scanned into a library, or
// a loose material script with the folder around it (the parent of scripts/
// or materials/) for images and Doom 3 tables.
struct MaterialSource {
	QString path;
	std::shared_ptr<PackageArchive> archive;
	std::shared_ptr<PackageArchive> base;
	MaterialLibrary library;
	// The loose script's index in library.scripts, or -1.
	int looseScript = -1;
	QString looseScriptFile;
	// Why images could not be searched, when they could not.
	QString imageWarning;
	std::shared_ptr<MaterialImageCache> cache = std::make_shared<MaterialImageCache>();
};

int sourceArgument(const Arguments& args, QString* path, QString* error)
{
	const QStringList rest = args.positional.mid(2);
	const bool input = args.hasValue(QStringLiteral("--input"));
	if (rest.size() > (input ? 0 : 1) || (!input && rest.isEmpty())) {
		*error = Text::tr("Give one source: a package, folder, WAD or material script, positionally or with --input.");
		return kUsage;
	}
	*path = input ? args.value(QStringLiteral("--input")) : rest.first();
	return 0;
}

int noPositional(const Arguments& args, QString* error)
{
	if (args.positional.size() > 2) {
		*error = Text::tr("Unexpected argument: %1.").arg(args.positional.at(2));
		return kUsage;
	}
	return 0;
}

// The game folder a loose script belongs to: the parent of its scripts/ or
// materials/ folder. Anywhere else the caller names it with --package, so a
// script in a busy folder never indexes that whole folder.
QString looseScriptRoot(const QFileInfo& script)
{
	QDir folder = script.absoluteDir();
	const QString name = folder.dirName().toLower();
	if ((name == QStringLiteral("scripts") || name == QStringLiteral("materials")) && folder.cdUp()) {
		return folder.absolutePath();
	}
	return {};
}

int openSource(const Arguments& args, const QString& path, MaterialSource* source, QString* error)
{
	const QFileInfo info(path);
	if (path.isEmpty() || !info.exists()) {
		*error = Text::tr("%1 does not exist.").arg(native(path));
		return kNotFound;
	}
	source->path = info.absoluteFilePath();
	const bool script = info.isFile() && isMaterialScriptPath(source->path);
	const bool explicitRoot = args.hasValue(QStringLiteral("--package"));
	QString root = args.value(QStringLiteral("--package"));
	if (root.isEmpty()) {
		root = script ? looseScriptRoot(info) : source->path;
	}
	QString loadError;
	QString rootWarning;
	auto archive = std::make_shared<PackageArchive>();
	if (root.isEmpty()) {
		rootWarning = Text::tr("Images were not searched: pass --package with the folder or package that holds them.");
	} else if (archive->load(root, &loadError)) {
		source->archive = archive;
	} else if (!script || explicitRoot) {
		*error = Text::tr("Could not open %1: %2").arg(native(root), loadError);
		return kFailure;
	} else {
		rootWarning = Text::tr("Images were not searched: %1 could not be opened (%2).").arg(native(root), loadError);
	}
	if (args.hasValue(QStringLiteral("--base"))) {
		const QString basePath = args.value(QStringLiteral("--base"));
		auto base = std::make_shared<PackageArchive>();
		if (!base->load(basePath, &loadError)) {
			*error = Text::tr("Could not open the base package %1: %2").arg(native(basePath), loadError);
			return kFailure;
		}
		source->base = base;
	}
	MaterialLibraryOptions options;
	options.includeImplicit = !args.has(QStringLiteral("--no-implicit"));
	if (source->archive) {
		source->library = scanMaterialLibrary(*source->archive, options);
	}
	if (source->library.sourcePath.isEmpty()) {
		source->library.sourcePath = source->path;
	}
	if (!source->archive && !source->base) {
		source->imageWarning = rootWarning;
	}
	if (!script) {
		return 0;
	}
	MaterialScript parsed;
	if (!loadMaterialScript(source->path, &parsed, &loadError)) {
		*error = loadError;
		return kFailure;
	}
	source->looseScriptFile = source->path;
	// The script joins the package where the game would read it, replacing a
	// package script at that path (core/material_library.h).
	const QString virtualPath = source->archive ? materialScriptVirtualPath(source->path, root, parsed.engine) : QString();
	source->looseScript = source->library.adoptScript(parsed, virtualPath);
	return 0;
}

int readEngine(const Arguments& args, MaterialEngine* engine, QString* error)
{
	*engine = MaterialEngine::Unknown;
	if (!args.hasValue(QStringLiteral("--engine"))) {
		return 0;
	}
	*engine = materialEngineFromId(args.value(QStringLiteral("--engine")));
	if (*engine == MaterialEngine::Unknown) {
		*error = Text::tr("Unknown engine %1; use doom, quake, quake2, quake3 or doom3.").arg(args.value(QStringLiteral("--engine")));
		return kUsage;
	}
	return 0;
}

// The entry for a name: the loose script's own definition first, then the
// one the game would use.
int findEntry(const MaterialSource& source, const QString& name, MaterialEngine engine)
{
	const QString key = materialLookupKey(name);
	if (source.looseScript >= 0) {
		for (int index = 0; index < source.library.entries.size(); ++index) {
			const MaterialLibraryEntry& entry = source.library.entries.at(index);
			if (entry.scriptIndex == source.looseScript && materialLookupKey(entry.name) == key) {
				return index;
			}
		}
	}
	return source.library.indexOf(name, engine);
}

// --material, or the only material the source (or its loose script) holds.
int chooseEntry(const Arguments& args, const MaterialSource& source, int* entry, QString* error)
{
	MaterialEngine engine = MaterialEngine::Unknown;
	if (const int code = readEngine(args, &engine, error)) {
		return code;
	}
	if (args.hasValue(QStringLiteral("--material"))) {
		*entry = findEntry(source, args.value(QStringLiteral("--material")), engine);
		if (*entry < 0) {
			*error = Text::tr("No material named %1 in %2.").arg(args.value(QStringLiteral("--material")), native(source.path));
			return kNotFound;
		}
		return 0;
	}
	QVector<int> candidates;
	for (int index = 0; index < source.library.entries.size(); ++index) {
		const MaterialLibraryEntry& item = source.library.entries.at(index);
		if ((source.looseScript < 0 || item.scriptIndex == source.looseScript) && !item.shadowed) {
			candidates.push_back(index);
		}
	}
	if (candidates.size() == 1) {
		*entry = candidates.first();
		return 0;
	}
	if (candidates.isEmpty()) {
		*error = Text::tr("%1 defines no materials.").arg(native(source.path));
		return kNotFound;
	}
	const int count = static_cast<int>(candidates.size());
	*error = Text::tr("Name the material with --material; %1 defines %n material(s).", nullptr, count).arg(native(source.path));
	return kUsage;
}

// Entries a listing or check covers: the loose script's when one was given,
// then --engine and --where.
int selectEntries(const Arguments& args, const MaterialSource& source, QVector<int>* selected, QString* error)
{
	MaterialEngine engine = MaterialEngine::Unknown;
	if (const int code = readEngine(args, &engine, error)) {
		return code;
	}
	const StudioQuery query = parseStudioQuery(args.value(QStringLiteral("--where")));
	QSet<QString> knownKeys;
	for (int index = 0; index < source.library.entries.size(); ++index) {
		const MaterialLibraryEntry& entry = source.library.entries.at(index);
		if (source.looseScript >= 0 && entry.scriptIndex != source.looseScript) {
			continue;
		}
		if (engine != MaterialEngine::Unknown && entry.engine != engine) {
			continue;
		}
		if (!query.isEmpty()) {
			const StudioQueryProperties properties = materialLibraryEntryQueryProperties(entry, source.library.definition(index));
			for (const QString& key : properties.uniqueKeys()) {
				knownKeys.insert(key);
			}
			if (!studioQueryMatches(query, properties, entry.name)) {
				continue;
			}
		}
		selected->push_back(index);
	}
	if (!query.isEmpty() && selected->isEmpty()) {
		const QStringList unknown = studioQueryUnknownKeys(query, knownKeys);
		if (!unknown.isEmpty() && !knownKeys.isEmpty()) {
			*error = Text::tr("No material has %1; check the --where keys.").arg(unknown.join(QStringLiteral(", ")));
			return kUsage;
		}
	}
	return 0;
}

MaterialImageSource imageSource(const Arguments& args, const MaterialSource& source)
{
	MaterialImageSource images;
	images.archive = source.archive;
	if (source.base) {
		images.fallbacks.push_back(source.base);
	}
	images.revision = source.path;
	images.doomCatalog = source.library.doomCatalog;
	images.paletteId = args.value(QStringLiteral("--palette"));
	images.quake2Sky = args.value(QStringLiteral("--sky"));
	images.doomSky = args.value(QStringLiteral("--sky"));
	images.developerDefault = args.has(QStringLiteral("--developer-default"));
	images.cache = source.cache.get();
	return images;
}

// Where an entry's text lives on disk or in the package, for messages.
QString entryLocation(const MaterialSource& source, const MaterialLibraryEntry& entry)
{
	return entry.scriptIndex >= 0 && entry.scriptIndex == source.looseScript ? native(source.looseScriptFile) : entry.sourcePath;
}

// Checks an output path before anything is written: its folder exists, it
// is not a package being read, and it is new unless --overwrite.
int checkOutput(const Arguments& args, const MaterialSource* source, const QString& path, QString* error)
{
	const QFileInfo info(path);
	if (info.exists() && info.isDir()) {
		*error = Text::tr("%1 is a folder; give a file name.").arg(native(path));
		return kUsage;
	}
	if (!info.absoluteDir().exists()) {
		*error = Text::tr("The folder for %1 does not exist.").arg(native(path));
		return kNotFound;
	}
	if (source) {
		for (const std::shared_ptr<PackageArchive>& archive : {source->archive, source->base}) {
			if (archive && QFileInfo(archive->sourcePath()).isFile() && samePath(archive->sourcePath(), path)) {
				*error = Text::tr("%1 is a package being read; write somewhere else.").arg(native(path));
				return kUsage;
			}
		}
	}
	if (info.exists() && !args.has(QStringLiteral("--overwrite"))) {
		*error = Text::tr("%1 already exists; use --overwrite to replace it.").arg(native(path));
		return kFailure;
	}
	return 0;
}

QString diagnosticLine(const QString& path, const MaterialDiagnostic& diagnostic)
{
	QString location = path.isEmpty() ? QStringLiteral("-") : path;
	if (diagnostic.line > 0) {
		location += QStringLiteral(":%1").arg(diagnostic.line);
		if (diagnostic.column > 0) {
			location += QStringLiteral(":%1").arg(diagnostic.column);
		}
	}
	return QStringLiteral("%1: %2: %3 [%4]").arg(location, materialDiagnosticSeverityId(diagnostic.severity), diagnostic.message,
		diagnostic.code);
}

QString entryLine(const MaterialSource& source, const MaterialLibraryEntry& entry)
{
	// The trait words are the --where keys, so they stay untranslated.
	QStringList traits;
	if (entry.animated) {
		traits << QStringLiteral("animated");
	}
	if (entry.sky) {
		traits << QStringLiteral("sky");
	}
	if (entry.light) {
		traits << QStringLiteral("light");
	}
	if (entry.fog) {
		traits << QStringLiteral("fog");
	}
	if (entry.translucent) {
		traits << QStringLiteral("translucent");
	}
	if (entry.rejected) {
		traits << QStringLiteral("rejected");
	}
	if (entry.shadowed) {
		traits << QStringLiteral("shadowed");
	}
	if (entry.errors > 0) {
		traits << QStringLiteral("errors=%1").arg(entry.errors);
	}
	if (entry.warnings > 0) {
		traits << QStringLiteral("warnings=%1").arg(entry.warnings);
	}
	QString location = entryLocation(source, entry);
	if (entry.line > 0) {
		location += QStringLiteral(":%1").arg(entry.line);
	}
	const int stages = entry.stageCount;
	const QString kind = QStringLiteral("%1 %2").arg(materialEngineId(entry.engine), entry.kind);
	const QString traitText = traits.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(traits.join(QLatin1Char(' ')));
	return Text::tr("%1  %2, %n stage(s)%3  %4", nullptr, stages).arg(entry.name, kind, traitText, location);
}

QJsonArray imageReport(const MaterialDefinition& definition, const MaterialImageSet& images, QStringList* lines)
{
	QJsonArray array;
	const QStringList references = definition.imageReferences();
	if (!references.isEmpty()) {
		*lines << Text::tr("Images:");
	}
	for (const QString& reference : references) {
		QJsonObject item;
		item.insert(QStringLiteral("reference"), reference);
		if (const MaterialTexturePtr texture = images.find(reference)) {
			item.insert(QStringLiteral("status"), texture->status);
			item.insert(QStringLiteral("path"), texture->path);
			item.insert(QStringLiteral("width"), texture->width);
			item.insert(QStringLiteral("height"), texture->height);
			item.insert(QStringLiteral("paletted"), texture->hasIndices());
			if (!texture->note.isEmpty()) {
				item.insert(QStringLiteral("note"), texture->note);
			}
			QString line = QStringLiteral("  %1: %2").arg(reference, texture->status);
			if (texture->isValid()) {
				line += QStringLiteral(" %1x%2").arg(texture->width).arg(texture->height);
			}
			if (!texture->path.isEmpty() && texture->path != reference) {
				line += QStringLiteral(" <- %1").arg(texture->path);
			}
			if (!texture->note.isEmpty()) {
				line += QStringLiteral(" (%1)").arg(texture->note);
			}
			*lines << line;
		} else if (const MaterialCubeTexturePtr cube = images.findCube(reference)) {
			item.insert(QStringLiteral("status"), cube->status);
			item.insert(QStringLiteral("cube"), true);
			item.insert(QStringLiteral("complete"), cube->isValid());
			if (!cube->note.isEmpty()) {
				item.insert(QStringLiteral("note"), cube->note);
			}
			*lines << QStringLiteral("  %1: %2 %3").arg(reference, cube->status, cube->isValid() ? Text::tr("cube") : Text::tr("incomplete cube"));
		} else {
			item.insert(QStringLiteral("status"), QStringLiteral("missing"));
			*lines << QStringLiteral("  %1: missing").arg(reference);
		}
		array.append(item);
	}
	return array;
}

QString definitionText(const MaterialSource& source, int index, const MaterialDefinition& definition)
{
	if (const MaterialScript* script = source.library.script(index)) {
		if (definition.span.isValid() && definition.span.end <= script->text.size()) {
			return script->text.mid(definition.span.start, definition.span.length());
		}
	}
	if (definition.kind == QStringLiteral("implicit") && definition.engine == MaterialEngine::Doom3) {
		return implicitDoom3MaterialText(definition.name);
	}
	return {};
}

bool readArchiveEntry(const PackageArchive& archive, const QString& virtualPath, qint64 limit, QByteArray* bytes, QString* error)
{
	const QVector<PackageEntry> entries = archive.entries();
	for (const PackageEntry& entry : entries) {
		if (entry.kind == PackageEntryKind::File && entry.virtualPath.compare(virtualPath, Qt::CaseInsensitive) == 0) {
			return archive.readEntryBytes(entry.virtualPath, bytes, error, limit);
		}
	}
	*error = Text::tr("%1 is not in the package.").arg(virtualPath);
	return false;
}

// --- list ----------------------------------------------------------------

MaterialsCliResult listCommand(const QStringList& arguments)
{
	QSet<QString> options = sourceOptions();
	options.unite(QSet<QString> {QStringLiteral("--where"), QStringLiteral("--limit")});
	Arguments args;
	QString error;
	if (!parseArguments(arguments, {QStringLiteral("--no-implicit")}, options, &args, &error)) {
		return fail(kUsage, error);
	}
	QString path;
	if (const int code = sourceArgument(args, &path, &error)) {
		return fail(code, error);
	}
	int limit = INT_MAX;
	if (!readInteger(args, QStringLiteral("--limit"), 1, INT_MAX, &limit, &error)) {
		return fail(kUsage, error);
	}
	MaterialSource source;
	if (const int code = openSource(args, path, &source, &error)) {
		return fail(code, error);
	}
	QVector<int> selected;
	if (const int code = selectEntries(args, source, &selected, &error)) {
		return fail(code, error);
	}
	const int matched = static_cast<int>(selected.size());
	const int shown = std::min(matched, limit);
	MaterialsCliResult result;
	QJsonObject payload = materialLibraryJson(source.library, false);
	QJsonArray entries;
	result.lines = materialLibrarySummaryLines(source.library);
	for (int i = 0; i < shown; ++i) {
		const MaterialLibraryEntry& entry = source.library.entries.at(selected.at(i));
		QJsonObject item = materialLibraryEntryJson(entry);
		item.insert(QStringLiteral("location"), entryLocation(source, entry));
		entries.append(item);
		result.lines << entryLine(source, entry);
	}
	if (matched == 0) {
		result.lines << Text::tr("No materials match.");
	} else if (shown < matched) {
		const int hidden = matched - shown;
		result.lines << Text::tr("%n more not shown; raise --limit to see them.", nullptr, hidden);
	}
	payload.insert(QStringLiteral("source"), source.path);
	payload.insert(QStringLiteral("matched"), matched);
	payload.insert(QStringLiteral("shown"), shown);
	payload.insert(QStringLiteral("entries"), entries);
	result.payload = payload;
	return result;
}

// --- inspect -------------------------------------------------------------

MaterialsCliResult inspectCommand(const QStringList& arguments)
{
	QSet<QString> options = sourceOptions();
	options.unite(QSet<QString> {QStringLiteral("--sky")});
	Arguments args;
	QString error;
	if (!parseArguments(arguments,
			{QStringLiteral("--no-images"), QStringLiteral("--show-source"), QStringLiteral("--no-implicit"), QStringLiteral("--developer-default")},
			options, &args, &error)) {
		return fail(kUsage, error);
	}
	QString path;
	if (const int code = sourceArgument(args, &path, &error)) {
		return fail(code, error);
	}
	MaterialSource source;
	if (const int code = openSource(args, path, &source, &error)) {
		return fail(code, error);
	}
	int index = -1;
	if (const int code = chooseEntry(args, source, &index, &error)) {
		return fail(code, error);
	}
	const MaterialLibraryEntry& entry = source.library.entries.at(index);
	const MaterialDefinition definition = source.library.definition(index);
	MaterialsCliResult result;
	QJsonObject payload;
	payload.insert(QStringLiteral("source"), source.path);
	payload.insert(QStringLiteral("location"), entryLocation(source, entry));
	payload.insert(QStringLiteral("entry"), materialLibraryEntryJson(entry));
	payload.insert(QStringLiteral("material"), materialDefinitionJson(definition));
	payload.insert(QStringLiteral("graph"), materialGraphJson(buildMaterialGraph(definition, defaultMaterialTextKind(definition))));
	result.lines = materialDefinitionSummaryLines(definition);
	if (entry.shadowed) {
		result.lines << Text::tr("Shadowed: the game uses the definition in %1.").arg(entry.shadowedBy);
	}
	if (!definition.engineRejection.isEmpty()) {
		result.lines << Text::tr("The engine drops this material: %1").arg(definition.engineRejection);
	}
	if (!args.has(QStringLiteral("--no-images"))) {
		const MaterialImageSet images = resolveMaterialImages(definition, imageSource(args, source));
		payload.insert(QStringLiteral("images"), imageReport(definition, images, &result.lines));
		payload.insert(QStringLiteral("missingImages"), QJsonArray::fromStringList(images.missing()));
		if (!images.palette.isEmpty()) {
			payload.insert(QStringLiteral("palette"), images.paletteSource);
			payload.insert(QStringLiteral("paletteGenerated"), images.paletteGenerated);
		}
		for (const QString& warning : images.warnings) {
			result.lines << Text::tr("Warning: %1").arg(warning);
		}
	}
	if (!source.imageWarning.isEmpty()) {
		payload.insert(QStringLiteral("imageWarning"), source.imageWarning);
		result.lines << source.imageWarning;
	}
	const QString location = entryLocation(source, entry);
	QJsonArray diagnostics;
	for (const MaterialDiagnostic& diagnostic : definition.diagnostics) {
		diagnostics.append(materialDiagnosticJson(diagnostic));
		result.lines << diagnosticLine(location, diagnostic);
	}
	payload.insert(QStringLiteral("diagnostics"), diagnostics);
	const QString text = definitionText(source, index, definition);
	if (!text.isEmpty()) {
		payload.insert(QStringLiteral("text"), text);
		if (args.has(QStringLiteral("--show-source"))) {
			result.lines << QString() << text;
		}
	}
	result.payload = payload;
	return result;
}

// --- validate ------------------------------------------------------------

MaterialsCliResult validateCommand(const QStringList& arguments)
{
	QSet<QString> options = sourceOptions();
	options.unite(QSet<QString> {QStringLiteral("--where"), QStringLiteral("--sky")});
	Arguments args;
	QString error;
	if (!parseArguments(arguments,
			{QStringLiteral("--no-images"), QStringLiteral("--strict"), QStringLiteral("--no-implicit"), QStringLiteral("--developer-default")},
			options, &args, &error)) {
		return fail(kUsage, error);
	}
	QString path;
	if (const int code = sourceArgument(args, &path, &error)) {
		return fail(code, error);
	}
	MaterialSource source;
	if (const int code = openSource(args, path, &source, &error)) {
		return fail(code, error);
	}
	QVector<int> selected;
	const bool oneMaterial = args.hasValue(QStringLiteral("--material"));
	if (oneMaterial) {
		int index = -1;
		if (const int code = chooseEntry(args, source, &index, &error)) {
			return fail(code, error);
		}
		selected.push_back(index);
	} else if (const int code = selectEntries(args, source, &selected, &error)) {
		return fail(code, error);
	}
	const bool filtered = oneMaterial || args.hasValue(QStringLiteral("--where")) || args.hasValue(QStringLiteral("--engine"));

	int errors = 0;
	int warnings = 0;
	int infos = 0;
	int missingImages = 0;
	QJsonArray problems;
	QJsonArray rejected;
	MaterialsCliResult result;
	const auto add = [&](const MaterialDiagnostic& diagnostic, const QString& location, MaterialEngine engine) {
		switch (diagnostic.severity) {
		case MaterialDiagnosticSeverity::Error:
			++errors;
			break;
		case MaterialDiagnosticSeverity::Warning:
			++warnings;
			break;
		case MaterialDiagnosticSeverity::Info:
			++infos;
			break;
		}
		QJsonObject item = materialDiagnosticJson(diagnostic);
		item.insert(QStringLiteral("location"), location);
		item.insert(QStringLiteral("engine"), materialEngineId(engine));
		problems.append(item);
		result.lines << diagnosticLine(location, diagnostic);
	};
	const auto note = [&](MaterialDiagnosticSeverity severity, const QString& code, const QString& message, const QString& material,
						  const QString& location, MaterialEngine engine, int line = 0) {
		MaterialDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.material = material;
		diagnostic.line = line;
		add(diagnostic, location, engine);
	};

	// Problems outside any one material: script syntax, the Doom texture
	// lumps, and what the scan could not read.
	QSet<int> scripts;
	for (int index : std::as_const(selected)) {
		if (source.library.entries.at(index).scriptIndex >= 0) {
			scripts.insert(source.library.entries.at(index).scriptIndex);
		}
	}
	if (!filtered) {
		for (int index = 0; index < source.library.scripts.size(); ++index) {
			if (source.looseScript < 0 || index == source.looseScript) {
				scripts.insert(index);
			}
		}
	}
	QList<int> scriptOrder = scripts.values();
	std::sort(scriptOrder.begin(), scriptOrder.end());
	for (int index : std::as_const(scriptOrder)) {
		const MaterialScript& script = source.library.scripts.at(index);
		const QString location = index == source.looseScript ? native(source.looseScriptFile) : script.path;
		for (const MaterialDiagnostic& diagnostic : script.diagnostics) {
			add(diagnostic, location, script.engine);
		}
	}
	if (!filtered && source.looseScript < 0) {
		if (source.library.doomCatalog) {
			for (const MaterialDiagnostic& diagnostic : source.library.doomCatalog->diagnostics) {
				add(diagnostic, source.library.doomCatalog->animdefsPath, MaterialEngine::Doom);
			}
			for (const QString& warning : source.library.doomCatalog->warnings) {
				note(MaterialDiagnosticSeverity::Warning, QStringLiteral("doom-catalog"), warning, QString(), source.path, MaterialEngine::Doom);
			}
		}
		for (const QString& warning : source.library.warnings) {
			note(MaterialDiagnosticSeverity::Warning, QStringLiteral("scan"), warning, QString(), source.path, MaterialEngine::Unknown);
		}
	}

	const bool checkImages = !args.has(QStringLiteral("--no-images")) && source.imageWarning.isEmpty();
	if (!args.has(QStringLiteral("--no-images")) && !source.imageWarning.isEmpty()) {
		note(MaterialDiagnosticSeverity::Warning, QStringLiteral("images-not-searched"), source.imageWarning, QString(), source.path,
			MaterialEngine::Unknown);
	}
	const MaterialImageSource images = imageSource(args, source);
	for (int index : std::as_const(selected)) {
		const MaterialLibraryEntry& entry = source.library.entries.at(index);
		const MaterialDefinition definition = source.library.definition(index);
		const QString location = entryLocation(source, entry);
		for (const MaterialDiagnostic& diagnostic : definition.diagnostics) {
			add(diagnostic, location, definition.engine);
		}
		if (!definition.engineRejection.isEmpty()) {
			rejected.append(definition.name);
			if (definition.errorCount() == 0) {
				note(MaterialDiagnosticSeverity::Error, QStringLiteral("engine-rejects"), definition.engineRejection, definition.name, location,
					definition.engine, definition.span.line);
			}
		}
		if (entry.shadowed) {
			note(MaterialDiagnosticSeverity::Warning, QStringLiteral("shadowed"),
				Text::tr("%1 is defined again in %2; the game uses that definition.").arg(definition.name, entry.shadowedBy), definition.name,
				location, definition.engine, definition.span.line);
		}
		if (!checkImages) {
			continue;
		}
		const MaterialImageSet resolved = resolveMaterialImages(definition, images);
		for (const QString& reference : resolved.missing()) {
			++missingImages;
			QString message;
			switch (definition.engine) {
			case MaterialEngine::Quake3:
				message = Text::tr("%1: image %2 was not found; Quake III drops the shader and draws its default shader.");
				break;
			case MaterialEngine::Doom3:
				message = Text::tr("%1: image %2 was not found; Doom 3 draws its default image instead.");
				break;
			default:
				message = Text::tr("%1: image %2 was not found.");
				break;
			}
			note(MaterialDiagnosticSeverity::Error, QStringLiteral("missing-image"), message.arg(definition.name, reference), definition.name,
				location, definition.engine, definition.span.line);
		}
	}

	const int checked = static_cast<int>(selected.size());
	result.lines << Text::tr("Checked %n material(s):", nullptr, checked) + QLatin1Char(' ') + Text::tr("%n error(s)", nullptr, errors)
			+ QStringLiteral(", ") + Text::tr("%n warning(s)", nullptr, warnings) + QLatin1Char('.');
	if (missingImages > 0 && !source.base) {
		result.lines << Text::tr("Pass --base with the game's own package or folder to find images this one borrows.");
	}
	const bool strict = args.has(QStringLiteral("--strict"));
	const bool passed = errors == 0 && (!strict || warnings == 0);
	QJsonObject payload;
	payload.insert(QStringLiteral("source"), source.path);
	payload.insert(QStringLiteral("checked"), checked);
	payload.insert(QStringLiteral("errors"), errors);
	payload.insert(QStringLiteral("warnings"), warnings);
	payload.insert(QStringLiteral("infos"), infos);
	payload.insert(QStringLiteral("missingImages"), missingImages);
	payload.insert(QStringLiteral("rejected"), rejected);
	payload.insert(QStringLiteral("strict"), strict);
	payload.insert(QStringLiteral("passed"), passed);
	payload.insert(QStringLiteral("problems"), problems);
	result.payload = payload;
	if (!passed) {
		result.exitCode = kValidationFailed;
		result.error = strict && errors == 0 ? Text::tr("Material validation found warnings (--strict).") : Text::tr("Material validation found errors.");
	}
	return result;
}

// --- render --------------------------------------------------------------

int readRenderOptions(const Arguments& args, MaterialRenderOptions* options, QString* error)
{
	if (args.hasValue(QStringLiteral("--size"))) {
		const QStringList parts = args.value(QStringLiteral("--size")).toLower().split(QLatin1Char('x'));
		bool widthOk = false;
		bool heightOk = false;
		const int width = parts.value(0).toInt(&widthOk);
		const int height = parts.value(1).toInt(&heightOk);
		if (parts.size() != 2 || !widthOk || !heightOk || width < 16 || height < 16 || width > 4096 || height > 4096) {
			*error = Text::tr("--size needs WIDTHxHEIGHT from 16x16 to 4096x4096, such as 512x384.");
			return kUsage;
		}
		options->size = QSize(width, height);
	}
	if (args.hasValue(QStringLiteral("--shape"))) {
		MaterialPreviewShape shape = MaterialPreviewShape::Wall;
		if (!materialPreviewShapeFromId(args.value(QStringLiteral("--shape")), &shape)) {
			QStringList ids;
			for (MaterialPreviewShape each : materialPreviewShapes()) {
				ids << materialPreviewShapeId(each);
			}
			*error = Text::tr("Unknown shape %1; use %2.").arg(args.value(QStringLiteral("--shape")), ids.join(QStringLiteral(", ")));
			return kUsage;
		}
		options->shape = shape;
	}
	options->pitch = defaultMaterialPreviewPitch(options->shape);
	MaterialPreviewLighting& lighting = options->lighting;
	int seed = static_cast<int>(options->seed);
	if (!readNumber(args, QStringLiteral("--time"), 0.0, 86400.0, &options->time, error)
		|| !readNumber(args, QStringLiteral("--yaw"), -360.0, 360.0, &options->yaw, error)
		|| !readNumber(args, QStringLiteral("--pitch"), -89.0, 89.0, &options->pitch, error)
		|| !readNumber(args, QStringLiteral("--zoom"), 0.1, 10.0, &options->zoom, error)
		|| !readNumber(args, QStringLiteral("--fov"), 10.0, 150.0, &options->fieldOfView, error)
		|| !readNumber(args, QStringLiteral("--tiling"), 0.0, 64.0, &options->tiling, error)
		|| !readNumber(args, QStringLiteral("--lightmap"), 0.0, 4.0, &lighting.lightmap, error)
		|| !readInteger(args, QStringLiteral("--doom-light"), 0, 255, &lighting.doomLight, error)
		|| !readInteger(args, QStringLiteral("--doom-extralight"), 0, 2, &lighting.doomExtraLight, error)
		|| !readInteger(args, QStringLiteral("--quake-lightmap"), 0, 255, &lighting.quakeLightmap, error)
		|| !readNumber(args, QStringLiteral("--quake2-intensity"), 1.0, 4.0, &lighting.quake2Intensity, error)
		|| !readNumber(args, QStringLiteral("--ambient"), 0.0, 1.0, &lighting.doom3Ambient, error)
		|| !readNumber(args, QStringLiteral("--sound"), 0.0, 1.0, &options->eval.sound, error)
		|| !readInteger(args, QStringLiteral("--seed"), 0, INT_MAX, &seed, error)) {
		return kUsage;
	}
	options->seed = static_cast<quint32>(seed);
	if (args.hasValue(QStringLiteral("--light-angle"))) {
		if (!readNumber(args, QStringLiteral("--light-angle"), -360.0, 360.0, &lighting.lightAngle, error)) {
			return kUsage;
		}
		lighting.lightOrbit = false;
	}
	if (args.hasValue(QStringLiteral("--context"))) {
		const QString context = args.value(QStringLiteral("--context")).toLower();
		if (context == QStringLiteral("world")) {
			options->context = MaterialSurfaceContext::World;
		} else if (context == QStringLiteral("model")) {
			options->context = MaterialSurfaceContext::Model;
		} else if (context == QStringLiteral("2d")) {
			options->context = MaterialSurfaceContext::TwoD;
		} else {
			*error = Text::tr("Unknown context %1; use world, model or 2d.").arg(args.value(QStringLiteral("--context")));
			return kUsage;
		}
	}
	if (args.hasValue(QStringLiteral("--quake-renderer"))
		&& !materialQuakeRendererFromId(args.value(QStringLiteral("--quake-renderer")), &lighting.quakeRenderer)) {
		*error = Text::tr("Unknown Quake renderer %1; use glquake, modern or software.").arg(args.value(QStringLiteral("--quake-renderer")));
		return kUsage;
	}
	if (args.hasValue(QStringLiteral("--quake-style"))) {
		const QString style = args.value(QStringLiteral("--quake-style"));
		bool numbered = false;
		const int number = style.toInt(&numbered);
		QString pattern;
		if (numbered) {
			for (const QuakeLightStyle& each : quakeLightStyles()) {
				if (each.number == number) {
					pattern = each.pattern;
				}
			}
		} else {
			pattern = style.toLower();
			const bool letters = std::all_of(pattern.cbegin(), pattern.cend(), [](QChar c) { return c >= QLatin1Char('a') && c <= QLatin1Char('z'); });
			if (!letters || pattern.size() > 64) {
				pattern.clear();
			}
		}
		if (pattern.isEmpty()) {
			*error = Text::tr("--quake-style needs a style number from 0 to 11 or a pattern of up to 64 letters a-z, such as mmnmmommommnonmmonqnmmo.");
			return kUsage;
		}
		lighting.quakeStyle = pattern;
	}
	if (args.hasValue(QStringLiteral("--light-color")) && !readColor(args.value(QStringLiteral("--light-color")), &lighting.lightColor)) {
		*error = Text::tr("--light-color needs r,g,b from 0 to 255, a colour name or #rrggbb.");
		return kUsage;
	}
	if (args.hasValue(QStringLiteral("--background"))) {
		const QString background = args.value(QStringLiteral("--background"));
		if (background.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
			options->background = QColor(0, 0, 0, 0);
			options->checker = false;
		} else if (!readColor(background, &options->background)) {
			*error = Text::tr("--background needs r,g,b from 0 to 255, a colour name, #rrggbb or none.");
			return kUsage;
		}
	}
	if (args.hasValue(QStringLiteral("--doom3-shading"))) {
		const QString shading = args.value(QStringLiteral("--doom3-shading")).toLower();
		if (shading == QStringLiteral("vanilla")) {
			lighting.doom3Shading = MaterialDoom3Shading::Vanilla;
		} else if (shading == QStringLiteral("bfg")) {
			lighting.doom3Shading = MaterialDoom3Shading::Bfg;
		} else {
			*error = Text::tr("Unknown Doom 3 shading %1; use vanilla or bfg.").arg(args.value(QStringLiteral("--doom3-shading")));
			return kUsage;
		}
	}
	if (args.hasValue(QStringLiteral("--filter")) && !materialFilteringFromId(args.value(QStringLiteral("--filter")), &options->filtering)) {
		*error = Text::tr("Unknown filter %1; use engine, nearest or bilinear.").arg(args.value(QStringLiteral("--filter")));
		return kUsage;
	}
	if (args.hasValue(QStringLiteral("--parms"))) {
		for (const QString& pair : args.value(QStringLiteral("--parms")).split(QLatin1Char(','))) {
			const QStringList parts = pair.split(QLatin1Char('='));
			bool indexOk = false;
			bool valueOk = false;
			const int parm = parts.value(0).trimmed().toInt(&indexOk);
			const double value = parts.value(1).trimmed().toDouble(&valueOk);
			if (parts.size() != 2 || !indexOk || !valueOk || parm < 0 || parm >= static_cast<int>(options->eval.parms.size())
				|| !std::isfinite(value) || std::abs(value) > 1000000.0) {
				*error = Text::tr("--parms needs index=value pairs with indexes 0 to 11, such as 0=1,1=0.5,4=2.");
				return kUsage;
			}
			options->eval.parms[static_cast<size_t>(parm)] = value;
		}
	}
	options->orthographic = args.has(QStringLiteral("--orthographic"));
	lighting.lightmapSpot = !args.has(QStringLiteral("--flat-lightmap"));
	lighting.overbright = !args.has(QStringLiteral("--no-overbright"));
	lighting.doomDistance = !args.has(QStringLiteral("--no-doom-distance"));
	lighting.doomFakeContrast = !args.has(QStringLiteral("--no-fake-contrast"));
	lighting.doomBoomWrapping = args.has(QStringLiteral("--boom-wrapping"));
	lighting.doom3Specular = !args.has(QStringLiteral("--no-specular"));
	options->alternate = args.has(QStringLiteral("--alternate"));
	options->editorImage = args.has(QStringLiteral("--editor-image"));
	options->honourRejection = !args.has(QStringLiteral("--ignore-rejection"));
	options->developerDefault = args.has(QStringLiteral("--developer-default"));
	if (args.has(QStringLiteral("--no-checker"))) {
		options->checker = false;
	}
	return 0;
}

MaterialsCliResult renderCommand(const QStringList& arguments)
{
	QSet<QString> options = sourceOptions();
	options.unite(QSet<QString> {QStringLiteral("--output"), QStringLiteral("--frames"), QStringLiteral("--fps"), QStringLiteral("--columns"),
		QStringLiteral("--time"), QStringLiteral("--shape"), QStringLiteral("--size"), QStringLiteral("--yaw"), QStringLiteral("--pitch"),
		QStringLiteral("--zoom"), QStringLiteral("--fov"), QStringLiteral("--tiling"), QStringLiteral("--context"), QStringLiteral("--lightmap"),
		QStringLiteral("--doom-light"), QStringLiteral("--doom-extralight"), QStringLiteral("--quake-renderer"), QStringLiteral("--quake-style"),
		QStringLiteral("--quake-lightmap"), QStringLiteral("--quake2-intensity"), QStringLiteral("--light-color"), QStringLiteral("--light-angle"),
		QStringLiteral("--doom3-shading"), QStringLiteral("--ambient"), QStringLiteral("--parms"), QStringLiteral("--sound"), QStringLiteral("--filter"),
		QStringLiteral("--background"), QStringLiteral("--seed"), QStringLiteral("--sky"), QStringLiteral("--renderer")});
	const QSet<QString> flags {QStringLiteral("--dry-run"), QStringLiteral("--overwrite"), QStringLiteral("--swatch"), QStringLiteral("--orthographic"),
		QStringLiteral("--flat-lightmap"), QStringLiteral("--no-overbright"), QStringLiteral("--no-doom-distance"), QStringLiteral("--no-fake-contrast"),
		QStringLiteral("--boom-wrapping"), QStringLiteral("--no-specular"), QStringLiteral("--alternate"), QStringLiteral("--editor-image"),
		QStringLiteral("--ignore-rejection"), QStringLiteral("--developer-default"), QStringLiteral("--no-checker"), QStringLiteral("--no-implicit")};
	Arguments args;
	QString error;
	if (!parseArguments(arguments, flags, options, &args, &error)) {
		return fail(kUsage, error);
	}
	QString path;
	if (const int code = sourceArgument(args, &path, &error)) {
		return fail(code, error);
	}
	const bool dry = args.has(QStringLiteral("--dry-run"));
	const QString output = args.value(QStringLiteral("--output"));
	if (output.isEmpty() && !dry) {
		return fail(kUsage, Text::tr("Give --output for the image, or --dry-run to render without writing."));
	}
	if (!output.isEmpty() && !QImageWriter::supportedImageFormats().contains(QFileInfo(output).suffix().toLower().toLatin1())) {
		return fail(kUsage, Text::tr("%1 has no image type VibeStudio can write; use .png.").arg(native(output)));
	}
	int frames = 1;
	double fps = 10.0;
	int columns = 0;
	if (!readInteger(args, QStringLiteral("--frames"), 1, 240, &frames, &error) || !readNumber(args, QStringLiteral("--fps"), 0.1, 120.0, &fps, &error)
		|| !readInteger(args, QStringLiteral("--columns"), 1, 240, &columns, &error)) {
		return fail(kUsage, error);
	}
	MaterialSource source;
	if (const int code = openSource(args, path, &source, &error)) {
		return fail(code, error);
	}
	int index = -1;
	if (const int code = chooseEntry(args, source, &index, &error)) {
		return fail(code, error);
	}
	const MaterialDefinition definition = source.library.definition(index);
	MaterialRenderOptions render;
	render.shape = defaultMaterialPreviewShape(definition);
	if (const int code = readRenderOptions(args, &render, &error)) {
		return fail(code, error);
	}
	if (!applyRendererChoice(arguments, &error)) {
		return fail(kUsage, error);
	}
	const bool swatch = args.has(QStringLiteral("--swatch"));
	const int side = std::min(render.size.width(), render.size.height());
	const QSize frameSize = swatch ? QSize(side, side) : render.size;
	columns = columns > 0 ? std::min(columns, frames) : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(frames))));
	const int rows = (frames + columns - 1) / columns;
	if (static_cast<qint64>(columns) * frameSize.width() > kMaximumSheetSide || static_cast<qint64>(rows) * frameSize.height() > kMaximumSheetSide) {
		return fail(kUsage, Text::tr("The frame sheet would be wider or taller than %1 pixels; lower --size or --frames.").arg(kMaximumSheetSide));
	}
	if (!output.isEmpty()) {
		if (const int code = checkOutput(args, &source, output, &error)) {
			return fail(code, error);
		}
	}

	const MaterialImageSet images = resolveMaterialImages(definition, imageSource(args, source));
	QImage sheet;
	QJsonArray frameList;
	QStringList notes;
	bool fallback = false;
	QString renderer;
	for (int frame = 0; frame < frames; ++frame) {
		const double time = render.time + frame / fps;
		MaterialRenderResult rendered;
		if (swatch) {
			rendered = renderMaterialSwatch(definition, images, source.library.tables, side, time, render.lighting);
		} else {
			MaterialRenderOptions each = render;
			each.time = time;
			rendered = renderMaterial(definition, images, source.library.tables, each);
		}
		if (rendered.image.isNull()) {
			const QString detail = rendered.errorDetail.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(rendered.errorDetail);
			return fail(kUnavailable, Text::tr("%1 could not be drawn: %2").arg(definition.name, rendered.error + detail));
		}
		renderer = rendered.renderer;
		if (frames == 1) {
			sheet = rendered.image;
		} else {
			if (sheet.isNull()) {
				sheet = QImage(columns * frameSize.width(), rows * frameSize.height(), QImage::Format_ARGB32);
				sheet.fill(Qt::transparent);
			}
			QPainter painter(&sheet);
			painter.drawImage(QPoint((frame % columns) * frameSize.width(), (frame / columns) * frameSize.height()), rendered.image);
		}
		for (const QString& each : std::as_const(rendered.notes)) {
			if (!notes.contains(each)) {
				notes << each;
			}
		}
		fallback = fallback || rendered.fallback;
		QJsonObject item;
		item.insert(QStringLiteral("time"), time);
		item.insert(QStringLiteral("frameIndex"), rendered.frameIndex);
		item.insert(QStringLiteral("frameName"), rendered.frameName);
		item.insert(QStringLiteral("stagesDrawn"), rendered.stagesDrawn);
		item.insert(QStringLiteral("fallback"), rendered.fallback);
		item.insert(QStringLiteral("milliseconds"), std::round(rendered.milliseconds * 100.0) / 100.0);
		item.insert(QStringLiteral("notes"), QJsonArray::fromStringList(rendered.notes));
		frameList.append(item);
	}
	if (!dry && !writeImage(output, sheet, &error)) {
		return fail(kFailure, error);
	}

	MaterialsCliResult result;
	QJsonObject payload;
	payload.insert(QStringLiteral("source"), source.path);
	payload.insert(QStringLiteral("material"), definition.name);
	payload.insert(QStringLiteral("engine"), materialEngineId(definition.engine));
	payload.insert(QStringLiteral("shape"), swatch ? QStringLiteral("swatch") : materialPreviewShapeId(render.shape));
	payload.insert(QStringLiteral("width"), sheet.width());
	payload.insert(QStringLiteral("height"), sheet.height());
	payload.insert(QStringLiteral("frameWidth"), frameSize.width());
	payload.insert(QStringLiteral("frameHeight"), frameSize.height());
	payload.insert(QStringLiteral("frameCount"), frames);
	payload.insert(QStringLiteral("fps"), fps);
	payload.insert(QStringLiteral("columns"), columns);
	payload.insert(QStringLiteral("frames"), frameList);
	payload.insert(QStringLiteral("fallback"), fallback);
	payload.insert(QStringLiteral("renderer"), renderer);
	payload.insert(QStringLiteral("notes"), QJsonArray::fromStringList(notes));
	payload.insert(QStringLiteral("missingImages"), QJsonArray::fromStringList(images.missing()));
	payload.insert(QStringLiteral("outputPath"), output.isEmpty() ? QString() : QFileInfo(output).absoluteFilePath());
	payload.insert(QStringLiteral("written"), !dry);
	payload.insert(QStringLiteral("dryRun"), dry);
	result.payload = payload;
	const QString shapeName = swatch ? Text::tr("Swatch") : materialPreviewShapeDisplayName(render.shape);
	result.lines << Text::tr("%1 (%2): %3, %4x%5, %n frame(s).", nullptr, frames)
						.arg(definition.name, materialEngineDisplayName(definition.engine), shapeName)
						.arg(frameSize.width())
						.arg(frameSize.height());
	if (fallback) {
		result.lines << Text::tr("The engine would not draw this material; the preview shows what it draws instead.");
	}
	for (const QString& each : std::as_const(notes)) {
		result.lines << Text::tr("Note: %1").arg(each);
	}
	if (!source.imageWarning.isEmpty()) {
		result.lines << source.imageWarning;
	} else {
		for (const QString& missing : images.missing()) {
			result.lines << Text::tr("Missing image: %1").arg(missing);
		}
	}
	result.lines << (dry ? Text::tr("Dry run: nothing written.") : Text::tr("Wrote %1.").arg(native(output)));
	return result;
}

// --- graph ---------------------------------------------------------------

QStringList graphLines(const MaterialGraph& graph)
{
	QStringList lines;
	const int nodes = static_cast<int>(graph.nodes.size());
	const int links = static_cast<int>(graph.links.size());
	lines << Text::tr("Graph of %1 (%2): %3, %4.")
				 .arg(graph.material, materialTextKindId(graph.textKind), Text::tr("%n node(s)", nullptr, nodes), Text::tr("%n link(s)", nullptr, links));
	for (const MaterialGraphNode& node : graph.nodes) {
		QString line = QStringLiteral("  %1  [%2] %3").arg(node.id, node.kind, node.title);
		if (!node.subtitle.isEmpty()) {
			line += QStringLiteral(": ") + node.subtitle;
		}
		if (node.inactive) {
			line += QLatin1Char(' ') + Text::tr("(not drawn)");
		}
		lines << line;
		for (const MaterialGraphProperty& property : node.properties) {
			lines << QStringLiteral("      %1 = %2").arg(property.id, property.value);
		}
	}
	if (!graph.links.isEmpty()) {
		lines << Text::tr("Links:");
		for (const MaterialGraphLink& link : graph.links) {
			lines << QStringLiteral("  %1.%2 -> %3.%4").arg(link.fromNode, link.fromPort, link.toNode, link.toPort);
		}
	}
	return lines;
}

// The text a graph edit changes, read from where the material lives.
struct EditableText {
	QString text;
	QString path;
	// Quake II: the WAL whose header the text describes.
	QByteArray walBytes;
};

int editableText(const MaterialSource& source, int index, const MaterialDefinition& definition, MaterialTextKind kind, EditableText* out,
	QString* error)
{
	const MaterialLibraryEntry& entry = source.library.entries.at(index);
	switch (kind) {
	case MaterialTextKind::Quake3Shader:
	case MaterialTextKind::Doom3Material: {
		const MaterialScript* script = source.library.script(index);
		if (!script) {
			*error = Text::tr("%1 has no script text yet; start one with material new.").arg(definition.name);
			return kValidationFailed;
		}
		out->text = script->text;
		out->path = entry.scriptIndex == source.looseScript ? source.looseScriptFile : script->path;
		return 0;
	}
	case MaterialTextKind::DoomSwantbls:
	case MaterialTextKind::DoomAnimdefs: {
		const std::shared_ptr<const DoomMaterialCatalog> catalog = source.library.doomCatalog;
		if (!catalog) {
			*error = Text::tr("%1 has no Doom textures or flats.").arg(native(source.path));
			return kNotFound;
		}
		if (kind == MaterialTextKind::DoomSwantbls) {
			out->text = swantblsText(catalog->ranges, catalog->switches);
			out->path = QStringLiteral("SWANTBLS");
		} else {
			out->text = catalog->animdefsText;
			out->path = catalog->animdefsPath.isEmpty() ? QStringLiteral("ANIMDEFS") : catalog->animdefsPath;
		}
		return 0;
	}
	case MaterialTextKind::Quake2WalJson: {
		if (!source.archive || !readArchiveEntry(*source.archive, entry.sourcePath, kMaximumWalBytes, &out->walBytes, error)) {
			if (error->isEmpty()) {
				*error = Text::tr("%1 could not be read.").arg(entry.sourcePath);
			}
			return kFailure;
		}
		Quake2WalInfo info;
		if (!readQuake2WalInfo(out->walBytes, &info, error)) {
			return kValidationFailed;
		}
		out->text = quake2WalInfoText(info);
		out->path = entry.sourcePath;
		return 0;
	}
	case MaterialTextKind::None:
		break;
	}
	*error = Text::tr("%1 has no text to edit.").arg(definition.name);
	return kValidationFailed;
}

MaterialsCliResult graphCommand(const QStringList& arguments)
{
	QSet<QString> options = sourceOptions();
	options.unite(QSet<QString> {QStringLiteral("--text-kind"), QStringLiteral("--edits"), QStringLiteral("--output")});
	Arguments args;
	QString error;
	if (!parseArguments(arguments, {QStringLiteral("--dry-run"), QStringLiteral("--overwrite"), QStringLiteral("--no-implicit")}, options, &args,
			&error)) {
		return fail(kUsage, error);
	}
	QString path;
	if (const int code = sourceArgument(args, &path, &error)) {
		return fail(code, error);
	}
	const bool editing = args.hasValue(QStringLiteral("--edits"));
	const bool dry = args.has(QStringLiteral("--dry-run"));
	const QString output = args.value(QStringLiteral("--output"));
	if (!editing && (args.hasValue(QStringLiteral("--output")) || dry)) {
		return fail(kUsage, Text::tr("--output and --dry-run go with --edits."));
	}
	if (editing && output.isEmpty() && !dry) {
		return fail(kUsage, Text::tr("Give --output for the edited text, or --dry-run to check the edits without writing."));
	}
	QVector<MaterialGraphEdit> edits;
	if (editing) {
		QByteArray bytes;
		if (!readFileBytes(args.value(QStringLiteral("--edits")), kMaximumEditBytes, &bytes, &error)) {
			return fail(kNotFound, error);
		}
		if (!parseMaterialGraphEditsJson(bytes, &edits, &error)) {
			return fail(kUsage, error);
		}
	}
	MaterialSource source;
	if (const int code = openSource(args, path, &source, &error)) {
		return fail(code, error);
	}
	int index = -1;
	if (const int code = chooseEntry(args, source, &index, &error)) {
		return fail(code, error);
	}
	const MaterialDefinition definition = source.library.definition(index);
	MaterialTextKind kind = defaultMaterialTextKind(definition);
	if (args.hasValue(QStringLiteral("--text-kind")) && !materialTextKindFromId(args.value(QStringLiteral("--text-kind")), &kind)) {
		return fail(kUsage, Text::tr("Unknown text kind %1; use quake3-shader, doom3-material, swantbls, animdefs or wal-json.")
								.arg(args.value(QStringLiteral("--text-kind"))));
	}
	const MaterialGraph graph = buildMaterialGraph(definition, kind);
	MaterialsCliResult result;
	QJsonObject payload;
	payload.insert(QStringLiteral("source"), source.path);
	QJsonArray templates;
	for (const MaterialGraphNodeTemplate& each : materialGraphNodeTemplates(definition.engine, kind)) {
		QJsonObject item;
		item.insert(QStringLiteral("id"), each.id);
		item.insert(QStringLiteral("category"), each.category);
		item.insert(QStringLiteral("title"), each.title);
		item.insert(QStringLiteral("description"), each.description);
		item.insert(QStringLiteral("needsStage"), each.needsStage);
		templates.append(item);
	}
	payload.insert(QStringLiteral("nodeTemplates"), templates);
	if (!editing) {
		payload.insert(QStringLiteral("graph"), materialGraphJson(graph));
		result.lines = graphLines(graph);
		result.payload = payload;
		return result;
	}

	EditableText text;
	if (const int code = editableText(source, index, definition, kind, &text, &error)) {
		return fail(code, error);
	}
	QStringList related {definition.name};
	for (const MaterialFrame& frame : definition.classic.frames) {
		related << frame.name;
	}
	QString focus;
	for (int number = 0; number < edits.size(); ++number) {
		MaterialGraphEdit edit = edits.at(number);
		if (edit.related.isEmpty()) {
			edit.related = related;
		}
		if (edit.kind == MaterialGraphEditKind::AddNode && edit.nodeTemplate == QStringLiteral("doom.range") && edit.value.isEmpty()) {
			edit.value = definition.kind == QStringLiteral("doom-flat") ? QStringLiteral("flat") : QStringLiteral("texture");
		}
		const MaterialGraphEditResult applied = applyMaterialGraphEdit(text.text, kind, definition.name, edit, text.path);
		if (!applied.ok) {
			return fail(kValidationFailed, Text::tr("Edit %1 (%2 %3): %4")
											   .arg(number + 1)
											   .arg(materialGraphEditKindId(edit.kind), edit.node, applied.error));
		}
		text.text = applied.text;
		focus = applied.focusNode;
	}
	QByteArray bytes = text.text.toUtf8();
	const bool walOutput = kind == MaterialTextKind::Quake2WalJson && QFileInfo(output).suffix().compare(QStringLiteral("wal"), Qt::CaseInsensitive) == 0;
	if (walOutput) {
		Quake2WalInfo info;
		if (!readQuake2WalInfo(text.walBytes, &info, &error) || !parseQuake2WalInfoText(text.text, &info, &error)) {
			return fail(kValidationFailed, error);
		}
		bytes = text.walBytes;
		if (!rewriteQuake2WalHeader(&bytes, info, &error)) {
			return fail(kValidationFailed, error);
		}
	}
	// What the edited text says about this material now.
	QJsonArray diagnostics;
	if (kind == MaterialTextKind::Quake3Shader || kind == MaterialTextKind::Doom3Material) {
		const MaterialScript edited = parseMaterialScript(text.text, kind == MaterialTextKind::Doom3Material ? MaterialEngine::Doom3 : MaterialEngine::Quake3,
			text.path);
		if (const MaterialDefinition* changed = edited.find(definition.name)) {
			for (const MaterialDiagnostic& diagnostic : changed->diagnostics) {
				diagnostics.append(materialDiagnosticJson(diagnostic));
				result.lines << diagnosticLine(native(text.path), diagnostic);
			}
			payload.insert(QStringLiteral("graph"), materialGraphJson(buildMaterialGraph(*changed, kind)));
		}
	}
	if (!output.isEmpty()) {
		if (const int code = checkOutput(args, &source, output, &error)) {
			return fail(code, error);
		}
	}
	if (!dry && !writeBytes(output, bytes, &error)) {
		return fail(kFailure, error);
	}
	const int applied = static_cast<int>(edits.size());
	payload.insert(QStringLiteral("material"), definition.name);
	payload.insert(QStringLiteral("textKind"), materialTextKindId(kind));
	payload.insert(QStringLiteral("edits"), applied);
	payload.insert(QStringLiteral("focusNode"), focus);
	payload.insert(QStringLiteral("diagnostics"), diagnostics);
	payload.insert(QStringLiteral("outputPath"), output.isEmpty() ? QString() : QFileInfo(output).absoluteFilePath());
	payload.insert(QStringLiteral("written"), !dry);
	payload.insert(QStringLiteral("dryRun"), dry);
	if (dry && !walOutput) {
		payload.insert(QStringLiteral("text"), text.text);
	}
	result.payload = payload;
	result.lines.prepend(Text::tr("Applied %n graph edit(s) to %1.", nullptr, applied).arg(definition.name));
	result.lines << (dry ? Text::tr("Dry run: nothing written.") : Text::tr("Wrote %1.").arg(native(output)));
	return result;
}

// --- edit ----------------------------------------------------------------

MaterialsCliResult editCommand(const QStringList& arguments)
{
	QSet<QString> options = sourceOptions();
	options.unite(QSet<QString> {QStringLiteral("--edits"), QStringLiteral("--script"), QStringLiteral("--output")});
	Arguments args;
	QString error;
	if (!parseArguments(arguments, {QStringLiteral("--dry-run"), QStringLiteral("--overwrite"), QStringLiteral("--strict")}, options, &args, &error)) {
		return fail(kUsage, error);
	}
	QString path;
	if (const int code = sourceArgument(args, &path, &error)) {
		return fail(code, error);
	}
	if (!args.hasValue(QStringLiteral("--edits"))) {
		return fail(kUsage, Text::tr("Give the edits as a JSON file with --edits."));
	}
	const bool dry = args.has(QStringLiteral("--dry-run"));
	const QString output = args.value(QStringLiteral("--output"));
	if (output.isEmpty() && !dry) {
		return fail(kUsage, Text::tr("Give --output for the edited script, or --dry-run to check the edits without writing."));
	}
	QByteArray editBytes;
	if (!readFileBytes(args.value(QStringLiteral("--edits")), kMaximumEditBytes, &editBytes, &error)) {
		return fail(kNotFound, error);
	}
	QVector<MaterialEdit> edits;
	if (!parseMaterialEditsJson(editBytes, args.value(QStringLiteral("--material")), &edits, &error)) {
		return fail(kUsage, error);
	}
	MaterialSource source;
	if (const int code = openSource(args, path, &source, &error)) {
		return fail(code, error);
	}
	int scriptIndex = source.looseScript;
	if (scriptIndex < 0 && args.hasValue(QStringLiteral("--script"))) {
		const QString wanted = args.value(QStringLiteral("--script"));
		for (int index = 0; index < source.library.scripts.size(); ++index) {
			if (source.library.scripts.at(index).path.compare(wanted, Qt::CaseInsensitive) == 0) {
				scriptIndex = index;
			}
		}
		if (scriptIndex < 0) {
			return fail(kNotFound, Text::tr("%1 has no script %2.").arg(native(source.path), wanted));
		}
	} else if (scriptIndex < 0) {
		if (!args.hasValue(QStringLiteral("--material"))) {
			return fail(kUsage, Text::tr("Name the script with --script, or a material it defines with --material."));
		}
		int entry = -1;
		if (const int code = chooseEntry(args, source, &entry, &error)) {
			return fail(code, error);
		}
		scriptIndex = source.library.entries.at(entry).scriptIndex;
		if (scriptIndex < 0) {
			return fail(kValidationFailed, Text::tr("%1 is not defined in a script.").arg(args.value(QStringLiteral("--material"))));
		}
	}
	const MaterialScript& script = source.library.scripts.at(scriptIndex);
	const QString scriptLocation = scriptIndex == source.looseScript ? native(source.looseScriptFile) : script.path;
	const MaterialEditResult edited = applyMaterialEdits(script, edits);
	if (!edited.ok) {
		return fail(kValidationFailed, edited.error);
	}
	MaterialsCliResult result;
	QJsonArray diagnostics;
	int errors = 0;
	for (const MaterialDiagnostic& diagnostic : edited.script.allDiagnostics()) {
		if (diagnostic.severity == MaterialDiagnosticSeverity::Error) {
			++errors;
		}
		diagnostics.append(materialDiagnosticJson(diagnostic));
		result.lines << diagnosticLine(scriptLocation, diagnostic);
	}
	if (errors > 0 && args.has(QStringLiteral("--strict"))) {
		return fail(kValidationFailed, Text::tr("The edited script has %n error(s); nothing written (--strict).", nullptr, errors));
	}
	if (!output.isEmpty()) {
		if (const int code = checkOutput(args, &source, output, &error)) {
			return fail(code, error);
		}
	}
	if (!dry && !writeBytes(output, edited.text.toUtf8(), &error)) {
		return fail(kFailure, error);
	}
	QJsonArray materials;
	for (const MaterialDefinition& each : edited.script.materials) {
		materials.append(each.name);
	}
	const int applied = static_cast<int>(edits.size());
	QJsonObject payload;
	payload.insert(QStringLiteral("source"), source.path);
	payload.insert(QStringLiteral("script"), scriptLocation);
	payload.insert(QStringLiteral("edits"), applied);
	payload.insert(QStringLiteral("changeStart"), edited.changeStart);
	payload.insert(QStringLiteral("changeEnd"), edited.changeEnd);
	payload.insert(QStringLiteral("materials"), materials);
	payload.insert(QStringLiteral("errors"), errors);
	payload.insert(QStringLiteral("diagnostics"), diagnostics);
	payload.insert(QStringLiteral("outputPath"), output.isEmpty() ? QString() : QFileInfo(output).absoluteFilePath());
	payload.insert(QStringLiteral("written"), !dry);
	payload.insert(QStringLiteral("dryRun"), dry);
	if (dry) {
		payload.insert(QStringLiteral("text"), edited.text);
	}
	result.payload = payload;
	result.lines.prepend(Text::tr("Applied %n edit(s) to %1.", nullptr, applied).arg(scriptLocation));
	result.lines << (dry ? Text::tr("Dry run: nothing written.") : Text::tr("Wrote %1.").arg(native(output)));
	return result;
}

// --- templates and new -----------------------------------------------------

MaterialsCliResult templatesCommand(const QStringList& arguments)
{
	Arguments args;
	QString error;
	if (!parseArguments(arguments, {}, {QStringLiteral("--engine")}, &args, &error)) {
		return fail(kUsage, error);
	}
	if (const int code = noPositional(args, &error)) {
		return fail(code, error);
	}
	MaterialEngine engine = MaterialEngine::Unknown;
	if (const int code = readEngine(args, &engine, &error)) {
		return fail(code, error);
	}
	MaterialsCliResult result;
	QJsonArray list;
	for (const MaterialTemplate& each : materialTemplates(engine)) {
		QJsonObject item;
		item.insert(QStringLiteral("id"), each.id);
		item.insert(QStringLiteral("engine"), materialEngineId(each.engine));
		item.insert(QStringLiteral("name"), each.displayName);
		item.insert(QStringLiteral("description"), each.description);
		item.insert(QStringLiteral("body"), each.body);
		list.append(item);
		result.lines << QStringLiteral("%1  %2  %3: %4").arg(each.id, materialEngineId(each.engine), each.displayName, each.description);
	}
	result.payload.insert(QStringLiteral("templates"), list);
	return result;
}

MaterialsCliResult newCommand(const QStringList& arguments)
{
	Arguments args;
	QString error;
	if (!parseArguments(arguments, {QStringLiteral("--append"), QStringLiteral("--overwrite"), QStringLiteral("--dry-run")},
			{QStringLiteral("--template"), QStringLiteral("--name"), QStringLiteral("--image"), QStringLiteral("--output")}, &args, &error)) {
		return fail(kUsage, error);
	}
	if (const int code = noPositional(args, &error)) {
		return fail(code, error);
	}
	if (!args.hasValue(QStringLiteral("--template")) || !args.hasValue(QStringLiteral("--name"))) {
		return fail(kUsage, Text::tr("Give --template (see material templates) and --name for the new material."));
	}
	MaterialTemplate materialTemplate;
	if (!materialTemplateById(args.value(QStringLiteral("--template")), &materialTemplate)) {
		return fail(kNotFound, Text::tr("Unknown template %1; list them with material templates.").arg(args.value(QStringLiteral("--template"))));
	}
	const QString name = args.value(QStringLiteral("--name")).trimmed();
	const bool plain = std::none_of(name.cbegin(), name.cend(), [](QChar c) {
		return c.isSpace() || c == QLatin1Char('{') || c == QLatin1Char('}') || c == QLatin1Char('"');
	});
	if (name.isEmpty() || !plain) {
		return fail(kUsage, Text::tr("A material name cannot hold spaces, braces or quotes."));
	}
	const QString text = instantiateMaterialTemplate(materialTemplate, name, args.value(QStringLiteral("--image")));
	const MaterialScript check = parseMaterialScript(text, materialTemplate.engine);
	const MaterialDefinition* created = check.find(name);
	if (!created || created->errorCount() > 0) {
		return fail(kValidationFailed, Text::tr("The template does not make a valid material named %1.").arg(name));
	}
	const QString output = args.value(QStringLiteral("--output"));
	const bool append = args.has(QStringLiteral("--append"));
	const bool dry = args.has(QStringLiteral("--dry-run"));
	if (append && output.isEmpty()) {
		return fail(kUsage, Text::tr("--append needs --output naming the script to add to."));
	}
	QString written = text;
	if (!written.endsWith(QLatin1Char('\n'))) {
		written += QLatin1Char('\n');
	}
	if (append) {
		if (!QFileInfo::exists(output)) {
			return fail(kNotFound, Text::tr("%1 does not exist; leave out --append to create it.").arg(native(output)));
		}
		MaterialScript script;
		if (!loadMaterialScript(output, &script, &error, materialTemplate.engine)) {
			return fail(kFailure, error);
		}
		if (script.find(name)) {
			return fail(kValidationFailed, Text::tr("%1 already defines %2.").arg(native(output), name));
		}
		MaterialEdit edit;
		edit.kind = MaterialEditKind::AddDefinition;
		edit.text = text;
		const MaterialEditResult edited = applyMaterialEdit(script, edit);
		if (!edited.ok) {
			return fail(kValidationFailed, edited.error);
		}
		written = edited.text;
	} else if (!output.isEmpty()) {
		if (const int code = checkOutput(args, nullptr, output, &error)) {
			return fail(code, error);
		}
	}
	if (!output.isEmpty() && !dry && !writeBytes(output, written.toUtf8(), &error)) {
		return fail(kFailure, error);
	}
	MaterialsCliResult result;
	result.payload.insert(QStringLiteral("template"), materialTemplate.id);
	result.payload.insert(QStringLiteral("engine"), materialEngineId(materialTemplate.engine));
	result.payload.insert(QStringLiteral("name"), name);
	result.payload.insert(QStringLiteral("text"), text);
	result.payload.insert(QStringLiteral("outputPath"), output.isEmpty() ? QString() : QFileInfo(output).absoluteFilePath());
	result.payload.insert(QStringLiteral("appended"), append);
	result.payload.insert(QStringLiteral("written"), !output.isEmpty() && !dry);
	result.payload.insert(QStringLiteral("dryRun"), dry);
	if (output.isEmpty()) {
		result.lines = text.split(QLatin1Char('\n'));
		while (!result.lines.isEmpty() && result.lines.constLast().isEmpty()) {
			result.lines.removeLast();
		}
	} else if (dry) {
		result.lines << Text::tr("Dry run: nothing written.");
	} else {
		result.lines << (append ? Text::tr("Added %1 to %2.") : Text::tr("Wrote %1 to %2.")).arg(name, native(output));
	}
	return result;
}

// --- doom-tables -----------------------------------------------------------

MaterialsCliResult doomTablesCommand(const QStringList& arguments)
{
	Arguments args;
	QString error;
	if (!parseArguments(arguments, {QStringLiteral("--dry-run"), QStringLiteral("--overwrite")},
			{QStringLiteral("--input"), QStringLiteral("--output-dir"), QStringLiteral("--base")}, &args, &error)) {
		return fail(kUsage, error);
	}
	QString path;
	if (const int code = sourceArgument(args, &path, &error)) {
		return fail(code, error);
	}
	const QFileInfo info(path);
	if (!info.exists()) {
		return fail(kNotFound, Text::tr("%1 does not exist.").arg(native(path)));
	}
	QVector<DoomAnimationRange> ranges;
	QVector<DoomSwitchPair> switches;
	QString animationSource;
	QString switchSource;
	int animdefs = 0;
	MaterialSource source;
	const QString suffix = info.suffix().toLower();
	if (info.isFile() && (suffix == QStringLiteral("txt") || suffix == QStringLiteral("dat"))) {
		QByteArray bytes;
		if (!readFileBytes(path, kMaximumEditBytes, &bytes, &error)) {
			return fail(kFailure, error);
		}
		QVector<MaterialDiagnostic> diagnostics;
		if (!parseSwantblsText(QString::fromUtf8(bytes), &ranges, &switches, &diagnostics)) {
			QStringList problems;
			for (const MaterialDiagnostic& diagnostic : std::as_const(diagnostics)) {
				problems << diagnosticLine(native(path), diagnostic);
			}
			return fail(kValidationFailed, problems.isEmpty() ? Text::tr("%1 is not SWANTBLS text.").arg(native(path)) : problems.join(QLatin1Char('\n')));
		}
		animationSource = QStringLiteral("swantbls");
		switchSource = QStringLiteral("swantbls");
	} else {
		if (const int code = openSource(args, path, &source, &error)) {
			return fail(code, error);
		}
		if (!source.library.doomCatalog) {
			return fail(kNotFound, Text::tr("%1 has no Doom textures or flats.").arg(native(path)));
		}
		ranges = source.library.doomCatalog->ranges;
		switches = source.library.doomCatalog->switches;
		animationSource = source.library.doomCatalog->animationSource;
		switchSource = source.library.doomCatalog->switchSource;
		animdefs = static_cast<int>(source.library.doomCatalog->animdefs.size());
	}
	const QString text = swantblsText(ranges, switches);
	const QByteArray animated = boomAnimatedLump(ranges);
	const QByteArray switchLump = boomSwitchesLump(switches);
	const bool dry = args.has(QStringLiteral("--dry-run"));
	QJsonArray outputs;
	if (args.hasValue(QStringLiteral("--output-dir"))) {
		const QDir folder(args.value(QStringLiteral("--output-dir")));
		const QVector<std::pair<QString, QByteArray>> files {{folder.filePath(QStringLiteral("ANIMATED.lmp")), animated},
			{folder.filePath(QStringLiteral("SWITCHES.lmp")), switchLump}, {folder.filePath(QStringLiteral("SWANTBLS.txt")), text.toUtf8()}};
		if (!folder.exists() && !dry && !QDir().mkpath(folder.absolutePath())) {
			return fail(kFailure, Text::tr("Could not create %1.").arg(native(folder.absolutePath())));
		}
		for (const auto& file : files) {
			if (folder.exists()) {
				if (const int code = checkOutput(args, source.archive ? &source : nullptr, file.first, &error)) {
					return fail(code, error);
				}
			}
			outputs.append(QFileInfo(file.first).absoluteFilePath());
		}
		if (!dry) {
			for (const auto& file : files) {
				if (!writeBytes(file.first, file.second, &error)) {
					return fail(kFailure, error);
				}
			}
		}
	}
	MaterialsCliResult result;
	QJsonArray rangeList;
	for (const DoomAnimationRange& range : std::as_const(ranges)) {
		QJsonObject item;
		item.insert(QStringLiteral("type"), range.texture ? QStringLiteral("texture") : QStringLiteral("flat"));
		item.insert(QStringLiteral("first"), range.first);
		item.insert(QStringLiteral("last"), range.last);
		item.insert(QStringLiteral("tics"), range.tics);
		item.insert(QStringLiteral("decals"), range.decals);
		item.insert(QStringLiteral("source"), range.source);
		rangeList.append(item);
	}
	QJsonArray switchList;
	for (const DoomSwitchPair& pair : std::as_const(switches)) {
		QJsonObject item;
		item.insert(QStringLiteral("off"), pair.off);
		item.insert(QStringLiteral("on"), pair.on);
		item.insert(QStringLiteral("episode"), pair.episode);
		item.insert(QStringLiteral("source"), pair.source);
		switchList.append(item);
	}
	result.payload.insert(QStringLiteral("source"), info.absoluteFilePath());
	result.payload.insert(QStringLiteral("animationSource"), animationSource);
	result.payload.insert(QStringLiteral("switchSource"), switchSource);
	result.payload.insert(QStringLiteral("ranges"), rangeList);
	result.payload.insert(QStringLiteral("switches"), switchList);
	result.payload.insert(QStringLiteral("animdefsEntries"), animdefs);
	result.payload.insert(QStringLiteral("swantbls"), text);
	result.payload.insert(QStringLiteral("outputs"), outputs);
	result.payload.insert(QStringLiteral("written"), !outputs.isEmpty() && !dry);
	result.payload.insert(QStringLiteral("dryRun"), dry);
	const int rangeCount = static_cast<int>(ranges.size());
	const int switchCount = static_cast<int>(switches.size());
	result.lines << Text::tr("%1 (%2), %3 (%4).")
						.arg(Text::tr("%n animated range(s)", nullptr, rangeCount), animationSource, Text::tr("%n switch(es)", nullptr, switchCount),
							switchSource);
	if (animdefs > 0) {
		result.lines << Text::tr("ANIMDEFS also defines %n entry(s); ports that read it use those first.", nullptr, animdefs);
	}
	if (outputs.isEmpty()) {
		result.lines << QString() << text.trimmed();
	} else if (dry) {
		result.lines << Text::tr("Dry run: nothing written.");
	} else {
		for (const QJsonValue& written : std::as_const(outputs)) {
			result.lines << Text::tr("Wrote %1.").arg(native(written.toString()));
		}
	}
	return result;
}

// --- wal -------------------------------------------------------------------

MaterialsCliResult walCommand(const QStringList& arguments)
{
	Arguments args;
	QString error;
	if (!parseArguments(arguments, {QStringLiteral("--dry-run"), QStringLiteral("--overwrite")},
			{QStringLiteral("--input"), QStringLiteral("--material"), QStringLiteral("--metadata"), QStringLiteral("--output")}, &args, &error)) {
		return fail(kUsage, error);
	}
	QString path;
	if (const int code = sourceArgument(args, &path, &error)) {
		return fail(code, error);
	}
	const QFileInfo info(path);
	if (!info.exists()) {
		return fail(kNotFound, Text::tr("%1 does not exist.").arg(native(path)));
	}
	QByteArray bytes;
	QString walPath = info.absoluteFilePath();
	MaterialSource source;
	if (info.isFile() && info.suffix().compare(QStringLiteral("wal"), Qt::CaseInsensitive) == 0) {
		if (!readFileBytes(path, kMaximumWalBytes, &bytes, &error)) {
			return fail(kFailure, error);
		}
	} else {
		if (!args.hasValue(QStringLiteral("--material"))) {
			return fail(kUsage, Text::tr("Name the texture with --material, such as e1u1/floor1_3."));
		}
		source.path = info.absoluteFilePath();
		source.archive = std::make_shared<PackageArchive>();
		if (!source.archive->load(path, &error)) {
			return fail(kFailure, Text::tr("Could not open %1: %2").arg(native(path), error));
		}
		walPath = quake2WalPath(args.value(QStringLiteral("--material")));
		if (!readArchiveEntry(*source.archive, walPath, kMaximumWalBytes, &bytes, &error)) {
			return fail(kNotFound, error);
		}
	}
	Quake2WalInfo wal;
	if (!readQuake2WalInfo(bytes, &wal, &error)) {
		return fail(kValidationFailed, error);
	}
	const bool dry = args.has(QStringLiteral("--dry-run"));
	const QString output = args.value(QStringLiteral("--output"));
	const bool rewriting = args.hasValue(QStringLiteral("--metadata"));
	if (rewriting) {
		if (output.isEmpty() && !dry) {
			return fail(kUsage, Text::tr("Give --output for the rewritten WAL, or --dry-run to check the metadata."));
		}
		QByteArray metadata;
		if (!readFileBytes(args.value(QStringLiteral("--metadata")), kMaximumEditBytes, &metadata, &error)) {
			return fail(kNotFound, error);
		}
		Quake2WalInfo edited = wal;
		if (!parseQuake2WalInfoText(QString::fromUtf8(metadata), &edited, &error)) {
			return fail(kValidationFailed, error);
		}
		if (edited.size != wal.size) {
			return fail(kValidationFailed, Text::tr("The metadata gives a different size; a WAL header cannot resize its pixels."));
		}
		if (!rewriteQuake2WalHeader(&bytes, edited, &error)) {
			return fail(kValidationFailed, error);
		}
		wal = edited;
	}
	if (!output.isEmpty()) {
		if (const int code = checkOutput(args, source.archive ? &source : nullptr, output, &error)) {
			return fail(code, error);
		}
		if (!dry && !writeBytes(output, rewriting ? bytes : quake2WalInfoText(wal).toUtf8(), &error)) {
			return fail(kFailure, error);
		}
	}
	const QStringList surfaceFlags = quake2FlagIds(wal.surfaceFlags, quake2SurfaceFlagDescriptors());
	const QStringList contentFlags = quake2FlagIds(wal.contentFlags, quake2ContentFlagDescriptors());
	MaterialsCliResult result;
	result.payload.insert(QStringLiteral("path"), walPath);
	result.payload.insert(QStringLiteral("name"), wal.name);
	result.payload.insert(QStringLiteral("nextFrame"), wal.nextFrame);
	result.payload.insert(QStringLiteral("width"), wal.size.width());
	result.payload.insert(QStringLiteral("height"), wal.size.height());
	result.payload.insert(QStringLiteral("surfaceFlags"), static_cast<qint64>(wal.surfaceFlags));
	result.payload.insert(QStringLiteral("surfaceFlagIds"), QJsonArray::fromStringList(surfaceFlags));
	result.payload.insert(QStringLiteral("contentFlags"), static_cast<qint64>(wal.contentFlags));
	result.payload.insert(QStringLiteral("contentFlagIds"), QJsonArray::fromStringList(contentFlags));
	result.payload.insert(QStringLiteral("value"), wal.value);
	result.payload.insert(QStringLiteral("sidecar"), quake2WalInfoText(wal));
	result.payload.insert(QStringLiteral("rewritten"), rewriting);
	result.payload.insert(QStringLiteral("outputPath"), output.isEmpty() ? QString() : QFileInfo(output).absoluteFilePath());
	result.payload.insert(QStringLiteral("written"), !output.isEmpty() && !dry);
	result.payload.insert(QStringLiteral("dryRun"), dry);
	result.lines << Text::tr("WAL %1: %2x%3").arg(wal.name).arg(wal.size.width()).arg(wal.size.height());
	result.lines << Text::tr("Next frame: %1").arg(wal.nextFrame.isEmpty() ? QStringLiteral("-") : wal.nextFrame);
	result.lines << Text::tr("Surface flags: %1").arg(surfaceFlags.isEmpty() ? QStringLiteral("-") : surfaceFlags.join(QStringLiteral(", ")));
	result.lines << Text::tr("Content flags: %1").arg(contentFlags.isEmpty() ? QStringLiteral("-") : contentFlags.join(QStringLiteral(", ")));
	result.lines << Text::tr("Value: %1").arg(wal.value);
	if (!output.isEmpty()) {
		result.lines << (dry ? Text::tr("Dry run: nothing written.") : Text::tr("Wrote %1.").arg(native(output)));
	}
	return result;
}

} // namespace

QStringList materialCommandActions()
{
	return {QStringLiteral("list"), QStringLiteral("inspect"), QStringLiteral("validate"), QStringLiteral("render"), QStringLiteral("graph"),
		QStringLiteral("edit"), QStringLiteral("templates"), QStringLiteral("new"), QStringLiteral("doom-tables"), QStringLiteral("wal")};
}

MaterialsCliResult runMaterialCommand(const QString& action, const QStringList& arguments)
{
	if (action == QStringLiteral("list")) {
		return listCommand(arguments);
	}
	if (action == QStringLiteral("inspect")) {
		return inspectCommand(arguments);
	}
	if (action == QStringLiteral("validate")) {
		return validateCommand(arguments);
	}
	if (action == QStringLiteral("render")) {
		return renderCommand(arguments);
	}
	if (action == QStringLiteral("graph")) {
		return graphCommand(arguments);
	}
	if (action == QStringLiteral("edit")) {
		return editCommand(arguments);
	}
	if (action == QStringLiteral("templates")) {
		return templatesCommand(arguments);
	}
	if (action == QStringLiteral("new")) {
		return newCommand(arguments);
	}
	if (action == QStringLiteral("doom-tables")) {
		return doomTablesCommand(arguments);
	}
	if (action == QStringLiteral("wal")) {
		return walCommand(arguments);
	}
	return fail(kUsage, Text::tr("Unknown material command %1; use %2.").arg(action, materialCommandActions().join(QStringLiteral(", "))));
}

} // namespace vibestudio::cli
