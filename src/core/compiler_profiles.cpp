#include "core/compiler_profiles.h"

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

QString profileText(const char* source)
{
	return QCoreApplication::translate("VibeStudioCompilerProfiles", source);
}

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
	bool inputRequired,
	bool outputPathArgumentSupported)
{
	return {
		id,
		toolId,
		displayName,
		engineFamily,
		stageId,
		inputDescription,
		inputExtensions,
		defaultOutputExtension,
		description,
		defaultArguments,
		inputRequired,
		outputPathArgumentSupported,
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
	return {
		compilerProfile(
			QStringLiteral("ericw-qbsp"),
			QStringLiteral("ericw-qbsp"),
			profileText("ericw-tools qbsp"),
			QStringLiteral("idTech2"),
			QStringLiteral("qbsp"),
			profileText("Quake .map source"),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			profileText("Compiles a Quake-family .map file into an idTech2 BSP."),
			{},
			true,
			true),
		compilerProfile(
			QStringLiteral("ericw-vis"),
			QStringLiteral("ericw-vis"),
			profileText("ericw-tools vis"),
			QStringLiteral("idTech2"),
			QStringLiteral("vis"),
			profileText("Quake BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("bsp"),
			profileText("Runs visibility processing for a Quake-family BSP."),
			{},
			true,
			false),
		compilerProfile(
			QStringLiteral("ericw-light"),
			QStringLiteral("ericw-light"),
			profileText("ericw-tools light"),
			QStringLiteral("idTech2"),
			QStringLiteral("light"),
			profileText("Quake BSP"),
			{QStringLiteral("bsp")},
			QStringLiteral("bsp"),
			profileText("Runs light compilation for a Quake-family BSP."),
			{},
			true,
			false),
		compilerProfile(
			QStringLiteral("zdbsp-nodes"),
			QStringLiteral("zdbsp"),
			profileText("ZDBSP nodes"),
			QStringLiteral("idTech1"),
			QStringLiteral("nodes"),
			profileText("Doom WAD"),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			profileText("Builds Doom-family map nodes with ZDBSP."),
			{},
			true,
			true),
		compilerProfile(
			QStringLiteral("zokumbsp-nodes"),
			QStringLiteral("zokumbsp"),
			profileText("ZokumBSP nodes"),
			QStringLiteral("idTech1"),
			QStringLiteral("nodes"),
			profileText("Doom WAD"),
			{QStringLiteral("wad")},
			QStringLiteral("wad"),
			profileText("Builds Doom-family nodes, blockmap, and reject data with ZokumBSP."),
			{},
			true,
			false),
		compilerProfile(
			QStringLiteral("q3map2-probe"),
			QStringLiteral("q3map2"),
			profileText("q3map2 help/probe"),
			QStringLiteral("idTech3"),
			QStringLiteral("probe"),
			profileText("No input"),
			{},
			QString(),
			profileText("Runs q3map2 help/probe output to verify the executable and inspect supported options."),
			{QStringLiteral("-help")},
			false,
			false),
		compilerProfile(
			QStringLiteral("q3map2-bsp"),
			QStringLiteral("q3map2"),
			profileText("q3map2 BSP compile"),
			QStringLiteral("idTech3"),
			QStringLiteral("bsp"),
			profileText("Quake III .map source"),
			{QStringLiteral("map")},
			QStringLiteral("bsp"),
			profileText("Builds a Quake III-family BSP from a .map source through q3map2."),
			{QStringLiteral("-meta")},
			true,
			false),
	};
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
		plan.errors << profileText("Compiler profile is not known.");
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
			plan.warnings << profileText("Compiler executable was not found; command can be reviewed but not run yet.");
		}
		for (const QString& warning : tool->warnings) {
			plan.warnings << warning;
		}
	} else {
		plan.errors << profileText("Compiler registry entry for this profile is missing.");
		plan.program = plan.profile.toolId;
	}

	plan.inputPath = absoluteCleanPath(request.inputPath);
	if (plan.inputPath.isEmpty()) {
		if (plan.profile.inputRequired) {
			plan.errors << profileText("Input path is required.");
		}
	} else {
		const QFileInfo inputInfo(plan.inputPath);
		if (!inputInfo.isFile()) {
			plan.errors << profileText("Input file does not exist.");
		}
		if (!extensionMatches(plan.inputPath, plan.profile.inputExtensions)) {
			plan.warnings << profileText("Input extension does not match the profile's expected file type.");
		}
	}

	plan.workingDirectory = request.workingDirectory.trimmed().isEmpty() ? defaultWorkingDirectory(plan.inputPath) : absoluteCleanPath(request.workingDirectory);
	plan.expectedOutputPath = request.outputPath.trimmed().isEmpty() ? defaultOutputPath(plan.inputPath, plan.profile.defaultOutputExtension) : absoluteCleanPath(request.outputPath);

	plan.arguments = plan.profile.defaultArguments;
	plan.arguments += request.extraArguments;
	if (!plan.inputPath.isEmpty()) {
		plan.arguments << plan.inputPath;
	}
	if (plan.profile.outputPathArgumentSupported && !request.outputPath.trimmed().isEmpty()) {
		plan.arguments << plan.expectedOutputPath;
	} else if (!plan.profile.outputPathArgumentSupported && !request.outputPath.trimmed().isEmpty() && plan.expectedOutputPath != plan.inputPath) {
		plan.warnings << profileText("This compiler profile normally updates the BSP in place; output path is recorded as an expected artifact only.");
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
	lines << profileText("Compiler command plan");
	lines << profileText("Profile: %1").arg(plan.profileFound ? plan.profile.id : profileText("(unknown)"));
	if (plan.profileFound) {
		lines << profileText("Tool: %1").arg(plan.profile.toolId);
		lines << profileText("Stage: %1").arg(plan.profile.stageId);
		lines << profileText("Engine: %1").arg(plan.profile.engineFamily);
	}
	lines << profileText("State: %1").arg(operationStateId(plan.state()));
	lines << profileText("Runnable: %1").arg(plan.isRunnable() ? profileText("yes") : profileText("no"));
	lines << profileText("Program: %1").arg(plan.program.isEmpty() ? profileText("(not resolved)") : QDir::toNativeSeparators(plan.program));
	lines << profileText("Working directory: %1").arg(QDir::toNativeSeparators(plan.workingDirectory));
	lines << profileText("Input: %1").arg(QDir::toNativeSeparators(plan.inputPath));
	lines << profileText("Expected output: %1").arg(QDir::toNativeSeparators(plan.expectedOutputPath));
	lines << profileText("Command line: %1").arg(plan.commandLine);
	if (!plan.warnings.isEmpty()) {
		lines << profileText("Warnings");
		for (const QString& warning : plan.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	if (!plan.errors.isEmpty()) {
		lines << profileText("Errors");
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
	if (!plan.inputPath.isEmpty()) {
		manifest.inputPaths << plan.inputPath;
		manifest.inputHashes.push_back(compilerFileHash(plan.inputPath));
	}
	if (!plan.expectedOutputPath.isEmpty()) {
		manifest.expectedOutputPaths << plan.expectedOutputPath;
		manifest.outputHashes.push_back(compilerFileHash(plan.expectedOutputPath));
	}
	manifest.warnings = plan.warnings;
	manifest.errors = plan.errors;

	manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), profileText("Compiler command plan created.")));
	if (!manifest.profileId.isEmpty()) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), profileText("Profile: %1").arg(manifest.profileId)));
	}
	if (!manifest.program.isEmpty()) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), profileText("Program: %1").arg(manifest.program)));
	}
	for (const QString& input : manifest.inputPaths) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), profileText("Input: %1").arg(input)));
	}
	for (const QString& output : manifest.expectedOutputPaths) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("info"), profileText("Expected output: %1").arg(output)));
	}
	for (const QString& warning : manifest.warnings) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("warning"), warning));
	}
	for (const QString& error : manifest.errors) {
		manifest.taskLog.push_back(taskLogEntry(QStringLiteral("error"), error));
	}
	manifest.taskLog.push_back(taskLogEntry(manifest.runnable ? QStringLiteral("info") : QStringLiteral("warning"), manifest.runnable ? profileText("Plan is runnable.") : profileText("Plan is not runnable yet.")));
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
	object.insert(QStringLiteral("registeredOutputPaths"), stringArrayJson(manifest.registeredOutputPaths));
	object.insert(QStringLiteral("inputHashes"), fileHashesJson(manifest.inputHashes));
	object.insert(QStringLiteral("outputHashes"), fileHashesJson(manifest.outputHashes));
	object.insert(QStringLiteral("diagnostics"), diagnosticsJson(manifest.diagnostics));
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
	lines << profileText("Compiler command manifest");
	lines << profileText("Schema: %1").arg(manifest.schemaVersion);
	lines << profileText("Manifest ID: %1").arg(manifest.manifestId);
	lines << profileText("Created UTC: %1").arg(manifest.createdUtc.toUTC().toString(Qt::ISODate));
	lines << profileText("Profile: %1").arg(manifest.profileId.isEmpty() ? profileText("(unknown)") : manifest.profileId);
	lines << profileText("Tool: %1").arg(manifest.toolId.isEmpty() ? profileText("(unknown)") : manifest.toolId);
	lines << profileText("State: %1").arg(operationStateId(manifest.state));
	lines << profileText("Runnable: %1").arg(manifest.runnable ? profileText("yes") : profileText("no"));
	lines << profileText("Exit code: %1").arg(manifest.exitCode >= 0 ? QString::number(manifest.exitCode) : profileText("not run"));
	lines << profileText("Duration: %1 ms").arg(manifest.durationMs >= 0 ? QString::number(manifest.durationMs) : profileText("not run"));
	lines << profileText("Command line: %1").arg(manifest.commandLine);
	lines << profileText("Working directory: %1").arg(QDir::toNativeSeparators(manifest.workingDirectory));
	if (!manifest.environmentSubset.isEmpty()) {
		lines << profileText("Environment subset");
		for (auto it = manifest.environmentSubset.cbegin(); it != manifest.environmentSubset.cend(); ++it) {
			QString value = it.value();
			if (value.size() > 240) {
				value = QStringLiteral("%1...").arg(value.left(237));
			}
			lines << QStringLiteral("- %1=%2").arg(it.key(), value);
		}
	}
	if (!manifest.inputPaths.isEmpty()) {
		lines << profileText("Inputs");
		for (const QString& input : manifest.inputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(input));
		}
	}
	if (!manifest.expectedOutputPaths.isEmpty()) {
		lines << profileText("Expected outputs");
		for (const QString& output : manifest.expectedOutputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(output));
		}
	}
	if (!manifest.registeredOutputPaths.isEmpty()) {
		lines << profileText("Registered outputs");
		for (const QString& output : manifest.registeredOutputPaths) {
			lines << QStringLiteral("- %1").arg(QDir::toNativeSeparators(output));
		}
	}
	if (!manifest.inputHashes.isEmpty() || !manifest.outputHashes.isEmpty()) {
		lines << profileText("File hashes");
		for (const CompilerFileHash& hash : manifest.inputHashes) {
			lines << QStringLiteral("- %1 %2 %3")
				.arg(hash.exists ? profileText("present") : profileText("missing"), hash.sha256.isEmpty() ? profileText("(no hash)") : hash.sha256, QDir::toNativeSeparators(hash.path));
		}
		for (const CompilerFileHash& hash : manifest.outputHashes) {
			lines << QStringLiteral("- %1 %2 %3")
				.arg(hash.exists ? profileText("present") : profileText("missing"), hash.sha256.isEmpty() ? profileText("(no hash)") : hash.sha256, QDir::toNativeSeparators(hash.path));
		}
	}
	if (!manifest.diagnostics.isEmpty()) {
		lines << profileText("Diagnostics");
		for (const CompilerDiagnostic& diagnostic : manifest.diagnostics) {
			lines << QStringLiteral("- %1: %2%3")
				.arg(diagnostic.level, diagnostic.message, diagnostic.filePath.isEmpty() ? QString() : QStringLiteral(" (%1:%2)").arg(QDir::toNativeSeparators(diagnostic.filePath)).arg(diagnostic.line));
		}
	}
	if (!manifest.stdoutText.trimmed().isEmpty()) {
		lines << profileText("Stdout");
		lines << manifest.stdoutText.trimmed();
	}
	if (!manifest.stderrText.trimmed().isEmpty()) {
		lines << profileText("Stderr");
		lines << manifest.stderrText.trimmed();
	}
	if (!manifest.taskLog.isEmpty()) {
		lines << profileText("Task log");
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
			*error = profileText("Manifest path is required.");
		}
		return false;
	}
	const QFileInfo info(path);
	const QString parentPath = info.absolutePath();
	if (!QDir().mkpath(parentPath)) {
		if (error) {
			*error = profileText("Failed to create manifest directory: %1").arg(parentPath);
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
			*error = profileText("Manifest path is required.");
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
			*error = profileText("Compiler manifest JSON is invalid: %1").arg(parseError.errorString());
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
	loaded.registeredOutputPaths = stringArrayFromJson(object.value(QStringLiteral("registeredOutputPaths")));
	loaded.inputHashes = fileHashesFromJson(object.value(QStringLiteral("inputHashes")));
	loaded.outputHashes = fileHashesFromJson(object.value(QStringLiteral("outputHashes")));
	loaded.diagnostics = diagnosticsFromJson(object.value(QStringLiteral("diagnostics")));
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
