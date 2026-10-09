#include "core/compiler_profiles.h"

#include "core/compiler_known_issues.h"
#include "core/quake_map_preflight.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace vibestudio {

namespace {

QString normalizedId(const QString& value)
{
	return value.trimmed().toLower().replace('_', '-');
}

CompilerProfileDescriptor compilerProfile(
	const QString& id,
	const QString& toolId,
	const QString& displayName,
	const QString& engineFamily,
	const QString& stageId,
	const QString& inputDescription,
	const QStringList& inputExtensions,
	const QString& defaultOutputExtension,
	const QString& description,
	const QStringList& defaultArguments,
	bool inputRequired)
{
	CompilerProfileDescriptor descriptor;
	descriptor.id = id;
	descriptor.toolId = toolId;
	descriptor.displayName = displayName;
	descriptor.engineFamily = engineFamily;
	descriptor.stageId = stageId;
	descriptor.inputDescription = inputDescription;
	descriptor.inputExtensions = inputExtensions;
	descriptor.defaultOutputExtension = defaultOutputExtension;
	descriptor.description = description;
	descriptor.defaultArguments = defaultArguments;
	descriptor.inputRequired = inputRequired;
	return descriptor;
}

CompilerArgumentPreset argumentPreset(
	const QString& id,
	const QString& displayName,
	const QString& description,
	const QStringList& arguments,
	bool requiresValue = false,
	const QString& valuePlaceholder = QString())
{
	CompilerArgumentPreset preset;
	preset.id = id;
	preset.displayName = displayName;
	preset.description = description;
	preset.arguments = arguments;
	preset.requiresValue = requiresValue;
	preset.valuePlaceholder = valuePlaceholder;
	return preset;
}

// VibeMap2 bsp targets and map-development switches, from
// external/compilers/vibemap2/src/qbsp/qbsp.cc (game_target_group / common_format_group / map_development_group).
QVector<CompilerArgumentPreset> vibemap2BspPresets()
{
	return {
		argumentPreset(QStringLiteral("bsp2"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Target BSP2"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Writes Quake's extended BSP2 format for large maps."), {QStringLiteral("-bsp2")}),
		argumentPreset(QStringLiteral("hlbsp"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Target Half-Life BSP30"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Writes Half-Life's BSP version 30 format."), {QStringLiteral("-hlbsp")}),
		argumentPreset(QStringLiteral("q2bsp"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Target Quake II BSP"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Writes Quake II's IBSP format."), {QStringLiteral("-q2bsp")}),
		argumentPreset(QStringLiteral("qbism"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Target Qbism BSP"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Writes Qbism's extended Quake II BSP format."), {QStringLiteral("-qbism")}),
		argumentPreset(QStringLiteral("hexen2"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Target Hexen II"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Writes Hexen II's BSP format."), {QStringLiteral("-hexen2")}),
		argumentPreset(QStringLiteral("notex"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Omit textures"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Writes stub texture data for a faster development compile."), {QStringLiteral("-notex")}),
		argumentPreset(QStringLiteral("leaktest"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Fail on leak"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Makes the compile fail instead of continuing when the map leaks."), {QStringLiteral("-leaktest")}),
		argumentPreset(QStringLiteral("wadpath"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Add WAD search path"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Adds a directory that is searched for the map's WAD files."), {QStringLiteral("-wadpath")}, true, QCoreApplication::translate("VibeStudioCompilerProfiles", "directory")),
	};
}

// VibeMap2 vis switches, from external/compilers/vibemap2/src/include/vis/vis.hh.
QVector<CompilerArgumentPreset> vibemap2VisPresets()
{
	return {
		argumentPreset(QStringLiteral("level4"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Full detail (level 4)"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Runs the highest visibility test iteration count."), {QStringLiteral("-level"), QStringLiteral("4")}),
		argumentPreset(QStringLiteral("fast"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Fast vis"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Runs the simple, fast visibility pass for development builds."), {QStringLiteral("-fast")}),
	};
}

// VibeMap2 light switches, from external/compilers/vibemap2/src/light/light.cc.
QVector<CompilerArgumentPreset> vibemap2LightPresets()
{
	return {
		argumentPreset(QStringLiteral("extra4"), QCoreApplication::translate("VibeStudioCompilerProfiles", "4x4 supersampling"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Supersamples lighting at 4x4 for release-quality output."), {QStringLiteral("-extra4")}),
		argumentPreset(QStringLiteral("bounce"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Bounce lighting"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Enables bounced (radiosity-style) lighting."), {QStringLiteral("-bounce")}),
		argumentPreset(QStringLiteral("lit"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Write .lit colour file"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Writes a sibling .lit coloured lighting file next to the BSP."), {QStringLiteral("-lit")}),
		argumentPreset(QStringLiteral("soft"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Soften lighting"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Applies the post-process softening filter."), {QStringLiteral("-soft")}),
	};
}

// VibeMap3 general options, from external/compilers/vibemap3/tools/quake3/q3map2/main.cpp and path_init.cpp.
QVector<CompilerArgumentPreset> vibemap3Presets()
{
	return {
		argumentPreset(QStringLiteral("meta"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Meta surfaces"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Enables surface metadata optimisation for the BSP stage."), {QStringLiteral("-meta")}),
		argumentPreset(QStringLiteral("fast"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Fast pass"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Runs the faster, lower quality variant of the stage."), {QStringLiteral("-fast")}),
		argumentPreset(QStringLiteral("fs-basepath"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Set base path"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Points VibeMap3 at the game's base installation directory."), {QStringLiteral("-fs_basepath")}, true, QCoreApplication::translate("VibeStudioCompilerProfiles", "directory")),
		argumentPreset(QStringLiteral("fs-game"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Set mod"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Selects the mod directory used for shaders and assets."), {QStringLiteral("-fs_game")}, true, QCoreApplication::translate("VibeStudioCompilerProfiles", "mod name")),
		argumentPreset(QStringLiteral("threads"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Thread count"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Limits the number of worker threads."), {QStringLiteral("-threads")}, true, QCoreApplication::translate("VibeStudioCompilerProfiles", "count")),
		argumentPreset(QStringLiteral("verbose"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Verbose output"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Prints verbose progress output."), {QStringLiteral("-v")}),
	};
}

const CompilerToolDiscovery* findTool(const CompilerRegistrySummary& summary, const QString& toolId)
{
	for (const CompilerToolDiscovery& discovery : summary.tools) {
		if (normalizedId(discovery.descriptor.id) == normalizedId(toolId)) {
			return &discovery;
		}
	}
	return nullptr;
}

QString absoluteCleanPath(const QString& path)
{
	if (path.trimmed().isEmpty()) {
		return {};
	}
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString defaultWorkingDirectory(const QString& inputPath)
{
	const QFileInfo info(inputPath);
	if (info.exists()) {
		return QDir::cleanPath(info.absolutePath());
	}
	return QDir::currentPath();
}

QString defaultOutputPath(const QString& inputPath, const QString& extension)
{
	const QFileInfo info(inputPath);
	if (inputPath.trimmed().isEmpty()) {
		return {};
	}
	if (extension.trimmed().isEmpty()) {
		return absoluteCleanPath(inputPath);
	}
	const QString suffix = extension.startsWith('.') ? extension : QStringLiteral(".%1").arg(extension);
	const QString baseName = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
	return QDir::cleanPath(QDir(info.absolutePath()).filePath(baseName + suffix));
}

QString siblingPath(const QString& basePath, const QString& extension)
{
	if (basePath.trimmed().isEmpty() || extension.trimmed().isEmpty()) {
		return {};
	}
	const QFileInfo info(basePath);
	const QString suffix = extension.startsWith('.') ? extension : QStringLiteral(".%1").arg(extension);
	const QString baseName = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
	return QDir::cleanPath(QDir(info.absolutePath()).filePath(baseName + suffix));
}

bool argumentsContain(const QStringList& arguments, const QString& flag)
{
	for (const QString& argument : arguments) {
		if (argument.compare(flag, Qt::CaseInsensitive) == 0) {
			return true;
		}
	}
	return false;
}

bool extensionMatches(const QString& path, const QStringList& extensions)
{
	if (extensions.isEmpty()) {
		return true;
	}
	const QString suffix = QFileInfo(path).suffix().toLower();
	for (const QString& extension : extensions) {
		QString normalized = extension.toLower();
		if (normalized.startsWith('.')) {
			normalized.remove(0, 1);
		}
		if (suffix == normalized) {
			return true;
		}
	}
	return false;
}

QString displayProgramForMissingExecutable(const CompilerProfileDescriptor& profile, const CompilerToolDiscovery& tool)
{
	if (!tool.descriptor.executableNames.isEmpty()) {
		return tool.descriptor.executableNames.first();
	}
	return profile.toolId;
}

QString quoteCommandPart(const QString& part)
{
	if (part.isEmpty()) {
		return QStringLiteral("\"\"");
	}
	bool needsQuotes = false;
	for (const QChar ch : part) {
		if (ch.isSpace() || ch == '"' || ch == '\'') {
			needsQuotes = true;
			break;
		}
	}
	if (!needsQuotes) {
		return part;
	}
	QString escaped = part;
	escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
	escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
	return QStringLiteral("\"%1\"").arg(escaped);
}

QJsonArray stringArrayJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QStringList stringArrayFromJson(const QJsonValue& value)
{
	QStringList values;
	const QJsonArray array = value.toArray();
	for (const QJsonValue& item : array) {
		const QString text = item.toString();
		if (!text.isEmpty()) {
			values.push_back(text);
		}
	}
	return values;
}

QMap<QString, QString> compilerEnvironmentSubset()
{
	QMap<QString, QString> environment;
	QStringList seenKeys;
	for (const QByteArray& key : {QByteArray("PATH"), QByteArray("Path"), QByteArray("HOME"), QByteArray("USERPROFILE"), QByteArray("APPDATA"), QByteArray("XDG_DATA_HOME"), QByteArray("QTDIR")}) {
		const QString normalizedKey = QString::fromLatin1(key).toLower();
		if (seenKeys.contains(normalizedKey)) {
			continue;
		}
		const QByteArray value = qgetenv(key.constData());
		if (!value.isEmpty()) {
			environment.insert(QString::fromLatin1(key), QString::fromLocal8Bit(value).left(4096));
			seenKeys.push_back(normalizedKey);
		}
	}
	return environment;
}

CompilerFileHash compilerFileHash(const QString& path)
{
	CompilerFileHash hash;
	hash.path = QDir::cleanPath(path);
	const QFileInfo info(hash.path);
	hash.exists = info.isFile();
	hash.sizeBytes = hash.exists ? info.size() : 0;
	if (!hash.exists) {
		return hash;
	}

	QFile file(hash.path);
	if (!file.open(QIODevice::ReadOnly)) {
		return hash;
	}
	QCryptographicHash digest(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		digest.addData(file.read(64 * 1024));
	}
	hash.sha256 = QString::fromLatin1(digest.result().toHex());
	return hash;
}

QJsonObject environmentSubsetJson(const QMap<QString, QString>& environment)
{
	QJsonObject object;
	for (auto it = environment.cbegin(); it != environment.cend(); ++it) {
		object.insert(it.key(), it.value());
	}
	return object;
}

QMap<QString, QString> environmentSubsetFromJson(const QJsonValue& value)
{
	QMap<QString, QString> environment;
	const QJsonObject object = value.toObject();
	for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
		environment.insert(it.key(), it.value().toString());
	}
	return environment;
}

QJsonObject fileHashJson(const CompilerFileHash& hash)
{
	QJsonObject object;
	object.insert(QStringLiteral("path"), hash.path);
	object.insert(QStringLiteral("exists"), hash.exists);
	object.insert(QStringLiteral("sizeBytes"), QString::number(hash.sizeBytes));
	object.insert(QStringLiteral("sha256"), hash.sha256);
	return object;
}

CompilerFileHash fileHashFromJson(const QJsonValue& value)
{
	const QJsonObject object = value.toObject();
	CompilerFileHash hash;
	hash.path = object.value(QStringLiteral("path")).toString();
	hash.exists = object.value(QStringLiteral("exists")).toBool();
	hash.sizeBytes = object.value(QStringLiteral("sizeBytes")).toString().toLongLong();
	hash.sha256 = object.value(QStringLiteral("sha256")).toString();
	return hash;
}

QJsonArray fileHashesJson(const QVector<CompilerFileHash>& hashes)
{
	QJsonArray array;
	for (const CompilerFileHash& hash : hashes) {
		array.append(fileHashJson(hash));
	}
	return array;
}

QVector<CompilerFileHash> fileHashesFromJson(const QJsonValue& value)
{
	QVector<CompilerFileHash> hashes;
	for (const QJsonValue& item : value.toArray()) {
		hashes.push_back(fileHashFromJson(item));
	}
	return hashes;
}

QJsonObject diagnosticJson(const CompilerDiagnostic& diagnostic)
{
	QJsonObject object;
	object.insert(QStringLiteral("level"), diagnostic.level);
	object.insert(QStringLiteral("message"), diagnostic.message);
	object.insert(QStringLiteral("filePath"), diagnostic.filePath);
	object.insert(QStringLiteral("line"), diagnostic.line);
	object.insert(QStringLiteral("column"), diagnostic.column);
	object.insert(QStringLiteral("rawLine"), diagnostic.rawLine);
	object.insert(QStringLiteral("channel"), diagnostic.channel);
	return object;
}

CompilerDiagnostic diagnosticFromJson(const QJsonValue& value)
{
	const QJsonObject object = value.toObject();
	CompilerDiagnostic diagnostic;
	diagnostic.level = object.value(QStringLiteral("level")).toString();
	diagnostic.message = object.value(QStringLiteral("message")).toString();
	diagnostic.filePath = object.value(QStringLiteral("filePath")).toString();
	diagnostic.line = object.value(QStringLiteral("line")).toInt();
	diagnostic.column = object.value(QStringLiteral("column")).toInt();
	diagnostic.rawLine = object.value(QStringLiteral("rawLine")).toString();
	diagnostic.channel = object.value(QStringLiteral("channel")).toString();
	return diagnostic;
}

QJsonArray diagnosticsJson(const QVector<CompilerDiagnostic>& diagnostics)
{
	QJsonArray array;
	for (const CompilerDiagnostic& diagnostic : diagnostics) {
		array.append(diagnosticJson(diagnostic));
	}
	return array;
}

QVector<CompilerDiagnostic> diagnosticsFromJson(const QJsonValue& value)
{
	QVector<CompilerDiagnostic> diagnostics;
	for (const QJsonValue& item : value.toArray()) {
		diagnostics.push_back(diagnosticFromJson(item));
	}
	return diagnostics;
}

CompilerTaskLogEntry taskLogEntry(const QString& level, const QString& message)
{
	return {QDateTime::currentDateTimeUtc(), level, message};
}

QJsonObject taskLogEntryJson(const CompilerTaskLogEntry& entry)
{
	QJsonObject object;
	object.insert(QStringLiteral("timestampUtc"), entry.timestampUtc.toUTC().toString(Qt::ISODate));
	object.insert(QStringLiteral("level"), entry.level);
	object.insert(QStringLiteral("message"), entry.message);
	return object;
}

QJsonArray taskLogJson(const QVector<CompilerTaskLogEntry>& entries)
{
	QJsonArray array;
	for (const CompilerTaskLogEntry& entry : entries) {
		array.append(taskLogEntryJson(entry));
	}
	return array;
}

QVector<CompilerTaskLogEntry> taskLogFromJson(const QJsonValue& value)
{
	QVector<CompilerTaskLogEntry> entries;
	for (const QJsonValue& item : value.toArray()) {
		const QJsonObject object = item.toObject();
		CompilerTaskLogEntry entry;
		entry.timestampUtc = QDateTime::fromString(object.value(QStringLiteral("timestampUtc")).toString(), Qt::ISODate).toUTC();
		entry.level = object.value(QStringLiteral("level")).toString();
		entry.message = object.value(QStringLiteral("message")).toString();
		if (!entry.timestampUtc.isValid()) {
			entry.timestampUtc = QDateTime::currentDateTimeUtc();
		}
		entries.push_back(entry);
	}
	return entries;
}

} // namespace

bool CompilerCommandPlan::isRunnable() const
{
	return profileFound && toolFound && executableAvailable && errors.isEmpty();
}

OperationState CompilerCommandPlan::state() const
{
	if (!errors.isEmpty()) {
		return OperationState::Failed;
	}
	if (isRunnable()) {
		return OperationState::Completed;
	}
	if (profileFound || toolFound || !warnings.isEmpty()) {
		return OperationState::Warning;
	}
	return OperationState::Idle;
}

QVector<CompilerProfileDescriptor> compilerProfileDescriptors()
{
	QVector<CompilerProfileDescriptor> profiles;

	{
		// qbsp accepts "sourcefile.map [destfile.bsp]" (external/compilers/vibemap2/src/qbsp/qbsp.cc).
		CompilerProfileDescriptor qbsp = compilerProfile(
			QStringLiteral("vibemap2-bsp"),
			QStringLiteral("vibemap2-bsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap2 bsp"),
			QStringLiteral("idTech2"),
			QStringLiteral("qbsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake .map source"),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiles a Quake-family .map file into an idTech2 BSP."),
			{},
			true);
		qbsp.outputArgumentStyle = CompilerOutputArgumentStyle::Positional;
		qbsp.defaultOutputMode = CompilerDefaultOutputMode::DerivedFromInput;
		// qbsp writes "<bsp>.prt" for vis and "<bsp>.pts" plus "<bsp>.leak.prt" when the map leaks
		// (external/compilers/vibemap2/src/qbsp/outside.cc).
		qbsp.relatedOutputExtensions = {QStringLiteral("prt"), QStringLiteral("pts"), QStringLiteral("leak.prt")};
		qbsp.argumentPresets = vibemap2BspPresets();
		profiles.push_back(qbsp);
	}

	{
		CompilerProfileDescriptor vis = compilerProfile(
			QStringLiteral("vibemap2-vis"),
			QStringLiteral("vibemap2-vis"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap2 vis"),
			QStringLiteral("idTech2"),
			QStringLiteral("vis"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("bsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Runs visibility processing for a Quake-family BSP."),
			{},
			true);
		vis.defaultOutputMode = CompilerDefaultOutputMode::InPlace;
		// vis loads "<bsp base>.prt" (external/compilers/vibemap2/src/vis/vis.cc).
		vis.requiredCompanionInputExtensions = {QStringLiteral("prt")};
		vis.argumentPresets = vibemap2VisPresets();
		profiles.push_back(vis);
	}

	{
		CompilerProfileDescriptor light = compilerProfile(
			QStringLiteral("vibemap2-light"),
			QStringLiteral("vibemap2-light"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap2 light"),
			QStringLiteral("idTech2"),
			QStringLiteral("light"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("bsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Runs light compilation for a Quake-family BSP."),
			{},
			true);
		light.defaultOutputMode = CompilerDefaultOutputMode::InPlace;
		// "-lit" writes a sibling coloured lighting file (external/compilers/vibemap2/src/light/light.cc).
		light.argumentTriggeredOutputExtensions.insert(QStringLiteral("-lit"), QStringLiteral("lit"));
		light.argumentPresets = vibemap2LightPresets();
		profiles.push_back(light);
	}

	{
		// bspinfo takes bsp files only and serialises "<base>.bsp.json"
		// (external/compilers/vibemap2/src/bspinfo/main.cc).
		CompilerProfileDescriptor bspinfo = compilerProfile(
			QStringLiteral("vibemap2-bspinfo"),
			QStringLiteral("vibemap2-bspinfo"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap2 bspinfo"),
			QStringLiteral("idTech2"),
			QStringLiteral("inspect"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("bsp.json"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Prints BSP lump sizes and texture usage, and writes a JSON dump beside the BSP."),
			{},
			true);
		bspinfo.defaultOutputMode = CompilerDefaultOutputMode::DerivedFromInput;
		profiles.push_back(bspinfo);
	}

	{
		// bsputil parses options before the single positional bsp path
		// (external/compilers/vibemap2/src/bsputil/bsputil.cc and common/settings.cc).
		CompilerProfileDescriptor check = compilerProfile(
			QStringLiteral("vibemap2-bsputil-check"),
			QStringLiteral("vibemap2-bsputil"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap2 bsputil --check"),
			QStringLiteral("idTech2"),
			QStringLiteral("inspect"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake BSP"),
			{QStringLiteral("bsp")},
			QString(),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Verifies BSP data consistency and reports problems on the console."),
			{},
			true);
		check.leadingStageArgument = QStringLiteral("--check");
		check.defaultOutputMode = CompilerDefaultOutputMode::NoArtifact;
		profiles.push_back(check);

		CompilerProfileDescriptor entities = compilerProfile(
			QStringLiteral("vibemap2-bsputil-extract-entities"),
			QStringLiteral("vibemap2-bsputil"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap2 bsputil --extract-entities"),
			QStringLiteral("idTech2"),
			QStringLiteral("extract"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("ent"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Extracts the BSP entity lump to a sibling .ent file."),
			{},
			true);
		entities.leadingStageArgument = QStringLiteral("--extract-entities");
		entities.defaultOutputMode = CompilerDefaultOutputMode::DerivedFromInput;
		profiles.push_back(entities);

		CompilerProfileDescriptor textures = compilerProfile(
			QStringLiteral("vibemap2-bsputil-extract-textures"),
			QStringLiteral("vibemap2-bsputil"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap2 bsputil --extract-textures"),
			QStringLiteral("idTech2"),
			QStringLiteral("extract"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("wad"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Extracts embedded BSP textures to a sibling WAD file."),
			{},
			true);
		textures.leadingStageArgument = QStringLiteral("--extract-textures");
		textures.defaultOutputMode = CompilerDefaultOutputMode::DerivedFromInput;
		profiles.push_back(textures);
	}

	{
		// ZDBSP selects its destination with "-o/--output=FILE" and otherwise writes "tmp.wad"
		// into the working directory (external/compilers/zdbsp/main.cpp).
		CompilerProfileDescriptor zdbsp = compilerProfile(
			QStringLiteral("zdbsp-nodes"),
			QStringLiteral("zdbsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "ZDBSP nodes"),
			QStringLiteral("idTech1"),
			QStringLiteral("nodes"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Doom WAD"),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Builds Doom-family map nodes with ZDBSP."),
			{},
			true);
		zdbsp.outputArgumentStyle = CompilerOutputArgumentStyle::Flag;
		zdbsp.outputArgumentFlag = QStringLiteral("-o");
		zdbsp.defaultOutputMode = CompilerDefaultOutputMode::WorkingDirectoryFile;
		zdbsp.defaultOutputFileName = QStringLiteral("tmp.wad");
		profiles.push_back(zdbsp);
	}

	{
		// ZokumBSP reads "{-o|x output[.wad]}" after the input file and level list
		// (external/compilers/zokumbsp/src/zokumbsp/zenmain.cpp, getOutputFile).
		CompilerProfileDescriptor zokum = compilerProfile(
			QStringLiteral("zokumbsp-nodes"),
			QStringLiteral("zokumbsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "ZokumBSP nodes"),
			QStringLiteral("idTech1"),
			QStringLiteral("nodes"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Doom WAD"),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Builds Doom-family nodes, blockmap, and reject data with ZokumBSP."),
			{},
			true);
		zokum.outputArgumentStyle = CompilerOutputArgumentStyle::Flag;
		zokum.outputArgumentFlag = QStringLiteral("-o");
		zokum.outputArgumentAfterInput = true;
		zokum.defaultOutputMode = CompilerDefaultOutputMode::InPlace;
		profiles.push_back(zokum);
	}

	{
		CompilerProfileDescriptor probe = compilerProfile(
			QStringLiteral("vibemap3-probe"),
			QStringLiteral("vibemap3"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap3 help/probe"),
			QStringLiteral("idTech3"),
			QStringLiteral("probe"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "No input"),
			{},
			QString(),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Runs VibeMap3 help/probe output to verify the executable and inspect supported options."),
			{QStringLiteral("-help")},
			false);
		probe.defaultOutputMode = CompilerDefaultOutputMode::NoArtifact;
		profiles.push_back(probe);
	}

	{
		// VibeMap3 dispatches on the first remaining token like q3map2; BSPMain is the fall-through case, so the
		// BSP profile has no leading stage token (tools/quake3/q3map2/main.cpp).
		CompilerProfileDescriptor bsp = compilerProfile(
			QStringLiteral("vibemap3-bsp"),
			QStringLiteral("vibemap3"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap3 BSP compile"),
			QStringLiteral("idTech3"),
			QStringLiteral("bsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake III .map source"),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Builds a Quake III-family BSP from a .map source through VibeMap3."),
			{QStringLiteral("-meta")},
			true);
		bsp.defaultOutputMode = CompilerDefaultOutputMode::DerivedFromInput;
		bsp.relatedOutputExtensions = {QStringLiteral("prt"), QStringLiteral("srf"), QStringLiteral("lin")};
		bsp.argumentPresets = vibemap3Presets();
		profiles.push_back(bsp);

		CompilerProfileDescriptor vis = compilerProfile(
			QStringLiteral("vibemap3-vis"),
			QStringLiteral("vibemap3"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap3 vis"),
			QStringLiteral("idTech3"),
			QStringLiteral("vis"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake III BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("bsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Runs the VibeMap3 visibility stage over an existing Quake III-family BSP."),
			{},
			true);
		vis.leadingStageArgument = QStringLiteral("-vis");
		vis.defaultOutputMode = CompilerDefaultOutputMode::InPlace;
		// VisMain loads "<base>.prt" written by the BSP stage (tools/quake3/q3map2/vis.cpp).
		vis.requiredCompanionInputExtensions = {QStringLiteral("prt")};
		vis.argumentPresets = vibemap3Presets();
		profiles.push_back(vis);

		CompilerProfileDescriptor light = compilerProfile(
			QStringLiteral("vibemap3-light"),
			QStringLiteral("vibemap3"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap3 light"),
			QStringLiteral("idTech3"),
			QStringLiteral("light"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake III BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("bsp"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Runs the VibeMap3 lighting stage over an existing Quake III-family BSP."),
			{},
			true);
		light.leadingStageArgument = QStringLiteral("-light");
		light.defaultOutputMode = CompilerDefaultOutputMode::InPlace;
		light.argumentPresets = vibemap3Presets();
		profiles.push_back(light);

		CompilerProfileDescriptor convert = compilerProfile(
			QStringLiteral("vibemap3-convert"),
			QStringLiteral("vibemap3"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap3 convert"),
			QStringLiteral("idTech3"),
			QStringLiteral("convert"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake III BSP or .map source"),
			{QStringLiteral("bsp"), QStringLiteral("map")},
			QString(),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Converts a BSP or .map through VibeMap3; the destination depends on the requested -format."),
			{},
			true);
		convert.leadingStageArgument = QStringLiteral("-convert");
		// ConvertBSPMain names its output from the chosen -format (tools/quake3/q3map2/convert_bsp.cpp).
		convert.defaultOutputMode = CompilerDefaultOutputMode::Unknown;
		convert.argumentPresets = vibemap3Presets();
		profiles.push_back(convert);

		CompilerProfileDescriptor pk3 = compilerProfile(
			QStringLiteral("vibemap3-pk3"),
			QStringLiteral("vibemap3"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap3 auto-package"),
			QStringLiteral("idTech3"),
			QStringLiteral("package"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Quake III BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("pk3"),
			QCoreApplication::translate("VibeStudioCompilerProfiles", "Collects the assets a BSP references into an automatic pk3 package."),
			{},
			true);
		pk3.leadingStageArgument = QStringLiteral("-pk3");
		// pk3BSPMain writes "<engine path>/<name>_autopacked.pk3" (tools/quake3/q3map2/autopk3.cpp).
		pk3.defaultOutputMode = CompilerDefaultOutputMode::Unknown;
		pk3.argumentPresets = vibemap3Presets();
		profiles.push_back(pk3);
	}

	for (CompilerProfileDescriptor& descriptor : profiles) {
		descriptor.outputPathArgumentSupported = descriptor.outputArgumentStyle != CompilerOutputArgumentStyle::None;
	}
	return profiles;
}

QVector<CompilerArgumentPreset> compilerArgumentPresetsForProfile(const QString& profileId)
{
	CompilerProfileDescriptor descriptor;
	if (!compilerProfileForId(profileId, &descriptor)) {
		return {};
	}
	return descriptor.argumentPresets;
}

bool compilerArgumentPresetForId(const QString& profileId, const QString& presetId, CompilerArgumentPreset* out)
{
	const QString normalized = normalizedId(presetId);
	for (const CompilerArgumentPreset& preset : compilerArgumentPresetsForProfile(profileId)) {
		if (normalizedId(preset.id) == normalized) {
			if (out) {
				*out = preset;
			}
			return true;
		}
	}
	return false;
}

QStringList compilerProfileIds()
{
	QStringList ids;
	for (const CompilerProfileDescriptor& descriptor : compilerProfileDescriptors()) {
		ids.push_back(descriptor.id);
	}
	return ids;
}

bool compilerProfileForId(const QString& id, CompilerProfileDescriptor* out)
{
	const QString normalized = normalizedId(id);
	for (const CompilerProfileDescriptor& descriptor : compilerProfileDescriptors()) {
		if (normalizedId(descriptor.id) == normalized) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

CompilerCommandPlan buildCompilerCommandPlan(const CompilerCommandRequest& request)
{
	CompilerCommandPlan plan;
	const QString requestedProfileId = normalizedId(request.profileId);
	plan.profileFound = compilerProfileForId(requestedProfileId, &plan.profile);
	if (!plan.profileFound) {
		const QString renamed = renamedCompilerId(requestedProfileId);
		plan.errors << (renamed.isEmpty()
			? QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiler profile is not known.")
			: QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiler profile %1 was renamed %2 when VibeStudio moved to VibeMap2 and VibeMap3.").arg(requestedProfileId, renamed));
		plan.commandLine = compilerCommandLineText(plan.program, plan.arguments);
		return plan;
	}

	CompilerRegistryOptions registryOptions;
	registryOptions.workspaceRootPath = request.workspaceRootPath;
	registryOptions.extraSearchPaths = request.extraSearchPaths;
	registryOptions.executableOverrides = request.executableOverrides;
	registryOptions.probeVersions = false;
	const CompilerRegistrySummary registry = discoverCompilerTools(registryOptions);
	if (const CompilerToolDiscovery* tool = findTool(registry, plan.profile.toolId)) {
		plan.tool = *tool;
		plan.toolFound = true;
		plan.executableAvailable = tool->executableAvailable;
		plan.program = tool->executableAvailable ? tool->executablePath : displayProgramForMissingExecutable(plan.profile, *tool);
		if (!tool->executableAvailable) {
			plan.warnings << QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiler executable was not found; command can be reviewed but not run yet.");
		}
		for (const QString& warning : tool->warnings) {
			plan.warnings << warning;
		}
	} else {
		plan.errors << QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiler registry entry for this profile is missing.");
		plan.program = plan.profile.toolId;
	}

	plan.inputPath = absoluteCleanPath(request.inputPath);
	if (plan.inputPath.isEmpty()) {
		if (plan.profile.inputRequired) {
			plan.errors << QCoreApplication::translate("VibeStudioCompilerProfiles", "Input path is required.");
		}
	} else {
		const QFileInfo inputInfo(plan.inputPath);
		if (!inputInfo.isFile()) {
			plan.errors << QCoreApplication::translate("VibeStudioCompilerProfiles", "Input file does not exist.");
		}
		if (!extensionMatches(plan.inputPath, plan.profile.inputExtensions)) {
			plan.warnings << QCoreApplication::translate("VibeStudioCompilerProfiles", "Input extension does not match the profile's expected file type.");
		}
	}

	plan.workingDirectory = request.workingDirectory.trimmed().isEmpty() ? defaultWorkingDirectory(plan.inputPath) : absoluteCleanPath(request.workingDirectory);

	const bool outputArgumentSupported = plan.profile.outputArgumentStyle != CompilerOutputArgumentStyle::None;
	const QString requestedOutputPath = request.outputPath.trimmed().isEmpty() ? QString() : absoluteCleanPath(request.outputPath);
	const QString passedOutputPath = outputArgumentSupported ? requestedOutputPath : QString();

	if (!requestedOutputPath.isEmpty() && !outputArgumentSupported) {
		plan.warnings << QCoreApplication::translate("VibeStudioCompilerProfiles", "This compiler profile updates its input in place; the requested output path cannot be passed to the tool and will not be registered as the expected artifact.");
	}

	if (!passedOutputPath.isEmpty()) {
		plan.expectedOutputPath = passedOutputPath;
	} else {
		switch (plan.profile.defaultOutputMode) {
		case CompilerDefaultOutputMode::DerivedFromInput:
			plan.expectedOutputPath = defaultOutputPath(plan.inputPath, plan.profile.defaultOutputExtension);
			break;
		case CompilerDefaultOutputMode::InPlace:
			plan.expectedOutputPath = plan.inputPath;
			break;
		case CompilerDefaultOutputMode::WorkingDirectoryFile:
			plan.expectedOutputPath = QDir::cleanPath(QDir(plan.workingDirectory).filePath(plan.profile.defaultOutputFileName));
			plan.warnings << QCoreApplication::translate("VibeStudioCompilerProfiles", "No output path was requested, so this tool writes its default file (%1) into the working directory.").arg(plan.profile.defaultOutputFileName);
			break;
		case CompilerDefaultOutputMode::Unknown:
			plan.expectedOutputPath.clear();
			plan.expectedOutputKnown = false;
			plan.warnings << QCoreApplication::translate("VibeStudioCompilerProfiles", "This stage decides its own destination, so VibeStudio cannot predict or validate the output artifact.");
			break;
		case CompilerDefaultOutputMode::NoArtifact:
			plan.expectedOutputPath.clear();
			plan.expectedOutputKnown = false;
			break;
		}
	}

	// Never let a derived output collapse onto the input: that would "validate" an untouched input file.
	if (plan.profile.defaultOutputMode != CompilerDefaultOutputMode::InPlace
		&& !plan.expectedOutputPath.isEmpty()
		&& !plan.inputPath.isEmpty()
		&& plan.expectedOutputPath == plan.inputPath) {
		plan.expectedOutputPath.clear();
		plan.expectedOutputKnown = false;
		plan.warnings << QCoreApplication::translate("VibeStudioCompilerProfiles", "The expected output path collapsed onto the input file, so the artifact destination is treated as unknown instead of validating an untouched input.");
	}

	plan.arguments.clear();
	if (!plan.profile.leadingStageArgument.isEmpty()) {
		plan.arguments << plan.profile.leadingStageArgument;
	}
	plan.arguments += plan.profile.defaultArguments;
	plan.arguments += request.extraArguments;
	if (plan.profile.outputArgumentStyle == CompilerOutputArgumentStyle::Flag && !plan.profile.outputArgumentAfterInput && !passedOutputPath.isEmpty()) {
		plan.arguments << plan.profile.outputArgumentFlag << passedOutputPath;
	}
	if (!plan.inputPath.isEmpty()) {
		plan.arguments << plan.inputPath;
	}
	if (plan.profile.outputArgumentStyle == CompilerOutputArgumentStyle::Positional && !passedOutputPath.isEmpty()) {
		plan.arguments << passedOutputPath;
	}
	if (plan.profile.outputArgumentStyle == CompilerOutputArgumentStyle::Flag && plan.profile.outputArgumentAfterInput && !passedOutputPath.isEmpty()) {
		plan.arguments << plan.profile.outputArgumentFlag << passedOutputPath;
	}

	const QString artifactBasePath = plan.expectedOutputPath.isEmpty() ? plan.inputPath : plan.expectedOutputPath;
	for (auto it = plan.profile.argumentTriggeredOutputExtensions.cbegin(); it != plan.profile.argumentTriggeredOutputExtensions.cend(); ++it) {
		if (!argumentsContain(plan.arguments, it.key())) {
			continue;
		}
		const QString path = siblingPath(artifactBasePath, it.value());
		if (!path.isEmpty() && path != plan.expectedOutputPath && !plan.additionalExpectedOutputPaths.contains(path)) {
			plan.additionalExpectedOutputPaths << path;
		}
	}
	for (const QString& extension : plan.profile.relatedOutputExtensions) {
		const QString path = siblingPath(artifactBasePath, extension);
		if (!path.isEmpty() && path != plan.expectedOutputPath && !plan.relatedOutputPaths.contains(path)) {
			plan.relatedOutputPaths << path;
		}
	}

	for (const QString& extension : plan.profile.requiredCompanionInputExtensions) {
		const QString companion = siblingPath(plan.inputPath, extension);
		if (companion.isEmpty() || QFileInfo(companion).isFile()) {
			continue;
		}
		// Both BSP compilers skip or delete the portal file when the map leaks
		// (external/compilers/vibemap2/src/qbsp/outside.cc and
		// external/compilers/vibemap3/tools/quake3/q3map2/bsp.cpp), so this warning must not name
		// one of them: it fires for the VibeMap3 chain exactly as it does for the VibeMap2 chain.
		plan.warnings << QCoreApplication::translate("VibeStudioCompilerProfiles", "The %1 stage needs %2 beside its input, but that file is missing. This is usually the classic chain where the BSP stage found a leak, so no portal file was kept and the visibility stage cannot run.")
			.arg(plan.profile.stageId, QDir::toNativeSeparators(companion));
	}

	if (plan.profile.toolId.startsWith(QStringLiteral("vibemap2-"), Qt::CaseInsensitive)) {
		const QVector<CompilerKnownIssueDescriptor> profileIssues = compilerKnownIssuesForProfile(plan.profile.id);
		int highValueCount = 0;
		for (const CompilerKnownIssueDescriptor& issue : profileIssues) {
			if (issue.highValue) {
				++highValueCount;
			}
		}
		if (!profileIssues.isEmpty()) {
			// Informational only: tracking upstream issues must not make every VibeMap2 run a warning.
			plan.knownIssueNotes << QCoreApplication::translate("VibeStudioCompilerProfiles", "VibeMap2 known-issue checks active: %1 high-value issues inherited from ericw-tools are tracked for this profile.").arg(highValueCount);
		}
		plan.knownIssueWarnings += vibemap2KnownIssuePlanWarnings(plan.profile.id, plan.inputPath, plan.arguments);
		plan.warnings += plan.knownIssueWarnings;
		if (QFileInfo(plan.inputPath).suffix().compare(QStringLiteral("map"), Qt::CaseInsensitive) == 0) {
			QString preflightError;
			const QuakeMapPreflightReport preflight = inspectQuakeMapPreflightFile(plan.inputPath, &preflightError);
			if (!preflightError.isEmpty()) {
				plan.preflightWarnings << preflightError;
			}
			plan.preflightWarnings += preflight.warningMessages();
			plan.warnings += plan.preflightWarnings;
		}
	}

	plan.commandLine = compilerCommandLineText(plan.program, plan.arguments);
	return plan;
}

QString compilerCommandLineText(const QString& program, const QStringList& arguments)
{
	QStringList parts;
	if (!program.isEmpty()) {
		parts << quoteCommandPart(program);
	}
	for (const QString& argument : arguments) {
		parts << quoteCommandPart(argument);
	}
	return parts.join(' ');
}

QString compilerCommandPlanText(const CompilerCommandPlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiler command plan");
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Profile: %1").arg(plan.profileFound ? plan.profile.id : QCoreApplication::translate("VibeStudioCompilerProfiles", "(unknown)"));
	if (plan.profileFound) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Tool: %1").arg(plan.profile.toolId);
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Stage: %1").arg(plan.profile.stageId);
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Engine: %1").arg(plan.profile.engineFamily);
	}
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "State: %1").arg(operationStateId(plan.state()));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Runnable: %1").arg(plan.isRunnable() ? QCoreApplication::translate("VibeStudioCompilerProfiles", "yes") : QCoreApplication::translate("VibeStudioCompilerProfiles", "no"));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Program: %1").arg(plan.program.isEmpty() ? QCoreApplication::translate("VibeStudioCompilerProfiles", "(not resolved)") : QDir::toNativeSeparators(plan.program));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Working directory: %1").arg(QDir::toNativeSeparators(plan.workingDirectory));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Input: %1").arg(QDir::toNativeSeparators(plan.inputPath));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Expected output: %1").arg(plan.expectedOutputKnown ? QDir::toNativeSeparators(plan.expectedOutputPath) : QCoreApplication::translate("VibeStudioCompilerProfiles", "(unknown)"));
	for (const QString& output : plan.additionalExpectedOutputPaths) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Expected output: %1").arg(QDir::toNativeSeparators(output));
	}
	for (const QString& output : plan.relatedOutputPaths) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Related output (optional): %1").arg(QDir::toNativeSeparators(output));
	}
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Command line: %1").arg(plan.commandLine);
	if (!plan.profile.argumentPresets.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Argument presets");
		for (const CompilerArgumentPreset& preset : plan.profile.argumentPresets) {
			lines << QStringLiteral("- %1 [%2]: %3%4")
				.arg(preset.displayName, preset.id, preset.arguments.join(' '), preset.requiresValue ? QStringLiteral(" <%1>").arg(preset.valuePlaceholder) : QString());
		}
	}
	if (!plan.knownIssueNotes.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Known issue notes");
		for (const QString& note : plan.knownIssueNotes) {
			lines << QStringLiteral("- %1").arg(note);
		}
	}
	if (!plan.knownIssueWarnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Known issue checks");
		for (const QString& warning : plan.knownIssueWarnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.preflightWarnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Preflight warnings");
		for (const QString& warning : plan.preflightWarnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Warnings");
		for (const QString& warning : plan.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.errors.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Errors");
		for (const QString& error : plan.errors) {
			lines << QStringLiteral("- %1").arg(error);
		}
	}
	return lines.join('\n');
}

CompilerCommandManifest compilerCommandManifestFromPlan(const CompilerCommandPlan& plan)
{
	CompilerCommandManifest manifest;
	manifest.manifestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	manifest.createdUtc = QDateTime::currentDateTimeUtc();
	manifest.profileId = plan.profileFound ? plan.profile.id : QString();
	manifest.toolId = plan.profileFound ? plan.profile.toolId : QString();
	manifest.stageId = plan.profileFound ? plan.profile.stageId : QString();
	manifest.engineFamily = plan.profileFound ? plan.profile.engineFamily : QString();
	manifest.runnable = plan.isRunnable();
	manifest.state = plan.state();
	manifest.program = plan.program;
	manifest.arguments = plan.arguments;
	manifest.commandLine = plan.commandLine;
	manifest.workingDirectory = plan.workingDirectory;
	manifest.environmentSubset = compilerEnvironmentSubset();
	manifest.knownIssueNotes = plan.knownIssueNotes;
	manifest.knownIssueWarnings = plan.knownIssueWarnings;
	manifest.preflightWarnings = plan.preflightWarnings;
	manifest.expectedOutputKnown = plan.expectedOutputKnown;
	if (!plan.inputPath.isEmpty()) {
		manifest.inputPaths << plan.inputPath;
		manifest.inputHashes.push_back(compilerFileHash(plan.inputPath));
	}
	if (!plan.expectedOutputPath.isEmpty()) {
		manifest.expectedOutputPaths << plan.expectedOutputPath;
	}
	for (const QString& output : plan.additionalExpectedOutputPaths) {
		if (!manifest.expectedOutputPaths.contains(output)) {
			manifest.expectedOutputPaths << output;
		}
	}
	for (const QString& output : manifest.expectedOutputPaths) {
		manifest.outputHashes.push_back(compilerFileHash(output));
	}
	for (const QString& output : plan.relatedOutputPaths) {
		if (!manifest.optionalOutputPaths.contains(output) && !manifest.expectedOutputPaths.contains(output)) {
			manifest.optionalOutputPaths << output;
		}
	}
	manifest.warnings = plan.warnings;
	manifest.errors = plan.errors;

	manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiler command plan created.")));
	for (const QString& note : manifest.knownIssueNotes) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), note));
	}
	if (!manifest.profileId.isEmpty()) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Profile: %1").arg(manifest.profileId)));
	}
	if (!manifest.program.isEmpty()) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Program: %1").arg(manifest.program)));
	}
	for (const QString& input : manifest.inputPaths) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Input: %1").arg(input)));
	}
	for (const QString& output : manifest.expectedOutputPaths) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), QCoreApplication::translate("VibeStudioCompilerProfiles", "Expected output: %1").arg(output)));
	}
	for (const QString& warning : manifest.warnings) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("warning"), warning));
	}
	for (const QString& error : manifest.errors) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("error"), error));
	}
	manifest.taskLog.push_back(taskLogEntry(manifest.runnable ? QStringLiteral("info") : QStringLiteral("warning"), manifest.runnable ? QCoreApplication::translate("VibeStudioCompilerProfiles", "Plan is runnable.") : QCoreApplication::translate("VibeStudioCompilerProfiles", "Plan is not runnable yet.")));
	return manifest;
}

QJsonObject compilerCommandManifestJson(const CompilerCommandManifest& manifest)
{
	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), manifest.schemaVersion);
	object.insert(QStringLiteral("manifestId"), manifest.manifestId);
	object.insert(QStringLiteral("createdUtc"), manifest.createdUtc.toUTC().toString(Qt::ISODate));
	if (manifest.startedUtc.isValid()) {
		object.insert(QStringLiteral("startedUtc"), manifest.startedUtc.toUTC().toString(Qt::ISODate));
	}
	if (manifest.finishedUtc.isValid()) {
		object.insert(QStringLiteral("finishedUtc"), manifest.finishedUtc.toUTC().toString(Qt::ISODate));
	}
	object.insert(QStringLiteral("profileId"), manifest.profileId);
	object.insert(QStringLiteral("toolId"), manifest.toolId);
	object.insert(QStringLiteral("stageId"), manifest.stageId);
	object.insert(QStringLiteral("engineFamily"), manifest.engineFamily);
	object.insert(QStringLiteral("runnable"), manifest.runnable);
	object.insert(QStringLiteral("state"), operationStateId(manifest.state));
	object.insert(QStringLiteral("exitCode"), manifest.exitCode);
	object.insert(QStringLiteral("durationMs"), QString::number(manifest.durationMs));
	object.insert(QStringLiteral("program"), manifest.program);
	object.insert(QStringLiteral("arguments"), stringArrayJson(manifest.arguments));
	object.insert(QStringLiteral("commandLine"), manifest.commandLine);
	object.insert(QStringLiteral("workingDirectory"), manifest.workingDirectory);
	object.insert(QStringLiteral("environmentSubset"), environmentSubsetJson(manifest.environmentSubset));
	object.insert(QStringLiteral("inputPaths"), stringArrayJson(manifest.inputPaths));
	object.insert(QStringLiteral("expectedOutputPaths"), stringArrayJson(manifest.expectedOutputPaths));
	object.insert(QStringLiteral("expectedOutputKnown"), manifest.expectedOutputKnown);
	object.insert(QStringLiteral("optionalOutputPaths"), stringArrayJson(manifest.optionalOutputPaths));
	object.insert(QStringLiteral("registeredOutputPaths"), stringArrayJson(manifest.registeredOutputPaths));
	object.insert(QStringLiteral("inputHashes"), fileHashesJson(manifest.inputHashes));
	object.insert(QStringLiteral("outputHashes"), fileHashesJson(manifest.outputHashes));
	object.insert(QStringLiteral("diagnostics"), diagnosticsJson(manifest.diagnostics));
	object.insert(QStringLiteral("knownIssueNotes"), stringArrayJson(manifest.knownIssueNotes));
	object.insert(QStringLiteral("knownIssueWarnings"), stringArrayJson(manifest.knownIssueWarnings));
	object.insert(QStringLiteral("preflightWarnings"), stringArrayJson(manifest.preflightWarnings));
	object.insert(QStringLiteral("stdout"), manifest.stdoutText);
	object.insert(QStringLiteral("stderr"), manifest.stderrText);
	object.insert(QStringLiteral("warnings"), stringArrayJson(manifest.warnings));
	object.insert(QStringLiteral("errors"), stringArrayJson(manifest.errors));
	object.insert(QStringLiteral("taskLog"), taskLogJson(manifest.taskLog));
	return object;
}

QString compilerCommandManifestText(const CompilerCommandManifest& manifest)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiler command manifest");
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Schema: %1").arg(manifest.schemaVersion);
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Manifest ID: %1").arg(manifest.manifestId);
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Created UTC: %1").arg(manifest.createdUtc.toUTC().toString(Qt::ISODate));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Profile: %1").arg(manifest.profileId.isEmpty() ? QCoreApplication::translate("VibeStudioCompilerProfiles", "(unknown)") : manifest.profileId);
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Tool: %1").arg(manifest.toolId.isEmpty() ? QCoreApplication::translate("VibeStudioCompilerProfiles", "(unknown)") : manifest.toolId);
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "State: %1").arg(operationStateId(manifest.state));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Runnable: %1").arg(manifest.runnable ? QCoreApplication::translate("VibeStudioCompilerProfiles", "yes") : QCoreApplication::translate("VibeStudioCompilerProfiles", "no"));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Exit code: %1").arg(manifest.exitCode >= 0 ? QString::number(manifest.exitCode) : QCoreApplication::translate("VibeStudioCompilerProfiles", "not run"));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Duration: %1 ms").arg(manifest.durationMs >= 0 ? QString::number(manifest.durationMs) : QCoreApplication::translate("VibeStudioCompilerProfiles", "not run"));
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Command line: %1").arg(manifest.commandLine);
	lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Working directory: %1").arg(QDir::toNativeSeparators(manifest.workingDirectory));
	if (!manifest.environmentSubset.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Environment subset");
		for (auto it = manifest.environmentSubset.cbegin(); it != manifest.environmentSubset.cend(); ++it) {
			QString value = it.value();
			if (value.size() > 240) {
				value = QStringLiteral("%1...").arg(value.left(237));
			}
			lines << QStringLiteral("- %1=%2").arg(it.key(), value);
		}
	}
	if (!manifest.inputPaths.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Inputs");
		for (const QString& input : manifest.inputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(input));
		}
	}
	if (!manifest.expectedOutputPaths.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Expected outputs");
		for (const QString& output : manifest.expectedOutputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(output));
		}
	} else if (!manifest.expectedOutputKnown) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Expected outputs: (unknown for this stage)");
	}
	if (!manifest.optionalOutputPaths.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Related outputs (optional)");
		for (const QString& output : manifest.optionalOutputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(output));
		}
	}
	if (!manifest.registeredOutputPaths.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Registered outputs");
		for (const QString& output : manifest.registeredOutputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(output));
		}
	}
	if (!manifest.inputHashes.isEmpty() || !manifest.outputHashes.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "File hashes");
		for (const CompilerFileHash& hash : manifest.inputHashes) {
			lines << QStringLiteral("- %1 %2 %3")
				.arg(hash.exists ? QCoreApplication::translate("VibeStudioCompilerProfiles", "present") : QCoreApplication::translate("VibeStudioCompilerProfiles", "missing"), hash.sha256.isEmpty() ? QCoreApplication::translate("VibeStudioCompilerProfiles", "(no hash)") : hash.sha256, QDir::toNativeSeparators(hash.path));
		}
		for (const CompilerFileHash& hash : manifest.outputHashes) {
			lines << QStringLiteral("- %1 %2 %3")
				.arg(hash.exists ? QCoreApplication::translate("VibeStudioCompilerProfiles", "present") : QCoreApplication::translate("VibeStudioCompilerProfiles", "missing"), hash.sha256.isEmpty() ? QCoreApplication::translate("VibeStudioCompilerProfiles", "(no hash)") : hash.sha256, QDir::toNativeSeparators(hash.path));
		}
	}
	if (!manifest.diagnostics.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Diagnostics");
		for (const CompilerDiagnostic& diagnostic : manifest.diagnostics) {
			QString location;
			if (!diagnostic.filePath.isEmpty()) {
				location = QStringLiteral(" (%1:%2)").arg(QDir::toNativeSeparators(diagnostic.filePath)).arg(diagnostic.line);
			} else if (diagnostic.line > 0) {
				location = QStringLiteral(" (line %1)").arg(diagnostic.line);
			}
			lines << QStringLiteral("- [%1] %2: %3%4")
				.arg(diagnostic.channel.isEmpty() ? QStringLiteral("stdout") : diagnostic.channel, diagnostic.level, diagnostic.message, location);
		}
	}
	if (!manifest.knownIssueNotes.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Known issue notes");
		for (const QString& note : manifest.knownIssueNotes) {
			lines << QStringLiteral("- %1").arg(note);
		}
	}
	if (!manifest.knownIssueWarnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Known issue checks");
		for (const QString& warning : manifest.knownIssueWarnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!manifest.preflightWarnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Preflight warnings");
		for (const QString& warning : manifest.preflightWarnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!manifest.stdoutText.trimmed().isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Stdout");
		lines << manifest.stdoutText.trimmed();
	}
	if (!manifest.stderrText.trimmed().isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Stderr");
		lines << manifest.stderrText.trimmed();
	}
	if (!manifest.taskLog.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioCompilerProfiles", "Task log");
		for (const CompilerTaskLogEntry& entry : manifest.taskLog) {
			lines << QStringLiteral("- [%1] %2: %3").arg(entry.timestampUtc.toUTC().toString(Qt::ISODate), entry.level, entry.message);
		}
	}
	return lines.join('\n');
}

bool saveCompilerCommandManifest(const CompilerCommandManifest& manifest, const QString& path, QString* error)
{
	if (path.trimmed().isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioCompilerProfiles", "Manifest path is required.");
		}
		return false;
	}
	const QFileInfo info(path);
	const QString parentPath = info.absolutePath();
	if (!QDir().mkpath(parentPath)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioCompilerProfiles", "Failed to create manifest directory: %1").arg(parentPath);
		}
		return false;
	}

	QSaveFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (error) {
			*error = file.errorString();
		}
		return false;
	}
	const QByteArray bytes = QJsonDocument(compilerCommandManifestJson(manifest)).toJson(QJsonDocument::Indented);
	if (file.write(bytes) != bytes.size()) {
		if (error) {
			*error = file.errorString();
		}
		return false;
	}
	if (!file.commit()) {
		if (error) {
			*error = file.errorString();
		}
		return false;
	}
	return true;
}

bool loadCompilerCommandManifest(const QString& path, CompilerCommandManifest* manifest, QString* error)
{
	if (error) {
		error->clear();
	}
	if (manifest) {
		*manifest = {};
	}
	if (path.trimmed().isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioCompilerProfiles", "Manifest path is required.");
		}
		return false;
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = file.errorString();
		}
		return false;
	}

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioCompilerProfiles", "Compiler manifest JSON is invalid: %1").arg(parseError.errorString());
		}
		return false;
	}

	const QJsonObject object = document.object();
	CompilerCommandManifest loaded;
	loaded.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(CompilerCommandManifest::kSchemaVersion);
	loaded.manifestId = object.value(QStringLiteral("manifestId")).toString();
	loaded.createdUtc = QDateTime::fromString(object.value(QStringLiteral("createdUtc")).toString(), Qt::ISODate).toUTC();
	loaded.startedUtc = QDateTime::fromString(object.value(QStringLiteral("startedUtc")).toString(), Qt::ISODate).toUTC();
	loaded.finishedUtc = QDateTime::fromString(object.value(QStringLiteral("finishedUtc")).toString(), Qt::ISODate).toUTC();
	loaded.profileId = object.value(QStringLiteral("profileId")).toString();
	loaded.toolId = object.value(QStringLiteral("toolId")).toString();
	loaded.stageId = object.value(QStringLiteral("stageId")).toString();
	loaded.engineFamily = object.value(QStringLiteral("engineFamily")).toString();
	loaded.runnable = object.value(QStringLiteral("runnable")).toBool();
	loaded.state = operationStateFromId(object.value(QStringLiteral("state")).toString());
	loaded.exitCode = object.value(QStringLiteral("exitCode")).toInt(-1);
	loaded.durationMs = object.value(QStringLiteral("durationMs")).toString(QStringLiteral("-1")).toLongLong();
	loaded.program = object.value(QStringLiteral("program")).toString();
	loaded.arguments = stringArrayFromJson(object.value(QStringLiteral("arguments")));
	loaded.commandLine = object.value(QStringLiteral("commandLine")).toString();
	loaded.workingDirectory = object.value(QStringLiteral("workingDirectory")).toString();
	loaded.environmentSubset = environmentSubsetFromJson(object.value(QStringLiteral("environmentSubset")));
	loaded.inputPaths = stringArrayFromJson(object.value(QStringLiteral("inputPaths")));
	loaded.expectedOutputPaths = stringArrayFromJson(object.value(QStringLiteral("expectedOutputPaths")));
	loaded.expectedOutputKnown = object.value(QStringLiteral("expectedOutputKnown")).toBool(true);
	loaded.optionalOutputPaths = stringArrayFromJson(object.value(QStringLiteral("optionalOutputPaths")));
	loaded.registeredOutputPaths = stringArrayFromJson(object.value(QStringLiteral("registeredOutputPaths")));
	loaded.inputHashes = fileHashesFromJson(object.value(QStringLiteral("inputHashes")));
	loaded.outputHashes = fileHashesFromJson(object.value(QStringLiteral("outputHashes")));
	loaded.diagnostics = diagnosticsFromJson(object.value(QStringLiteral("diagnostics")));
	loaded.knownIssueNotes = stringArrayFromJson(object.value(QStringLiteral("knownIssueNotes")));
	loaded.knownIssueWarnings = stringArrayFromJson(object.value(QStringLiteral("knownIssueWarnings")));
	loaded.preflightWarnings = stringArrayFromJson(object.value(QStringLiteral("preflightWarnings")));
	loaded.stdoutText = object.value(QStringLiteral("stdout")).toString();
	loaded.stderrText = object.value(QStringLiteral("stderr")).toString();
	loaded.warnings = stringArrayFromJson(object.value(QStringLiteral("warnings")));
	loaded.errors = stringArrayFromJson(object.value(QStringLiteral("errors")));
	loaded.taskLog = taskLogFromJson(object.value(QStringLiteral("taskLog")));
	if (loaded.manifestId.isEmpty()) {
		loaded.manifestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	}
	if (!loaded.createdUtc.isValid()) {
		loaded.createdUtc = QDateTime::currentDateTimeUtc();
	}
	if (manifest) {
		*manifest = loaded;
	}
	return true;
}

} // namespace vibestudio
