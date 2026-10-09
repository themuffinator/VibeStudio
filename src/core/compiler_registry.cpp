#include "core/compiler_registry.h"

#include "core/studio_manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStringDecoder>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace vibestudio {

namespace {

QString executableName(const QString& baseName)
{
#if defined(Q_OS_WIN)
	return baseName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) ? baseName : QStringLiteral("%1.exe").arg(baseName);
#else
	return baseName;
#endif
}

QString normalizedId(const QString& value)
{
	return value.trimmed().toLower().replace('_', '-');
}

QString absolutePath(const QString& rootPath, const QString& relativePath)
{
	const QString root = rootPath.trimmed().isEmpty() ? QDir::currentPath() : rootPath;
	return QDir::cleanPath(QDir(root).absoluteFilePath(relativePath));
}

QString absoluteExecutableOverride(const QString& executablePath, const QString& rootPath)
{
	const QString trimmed = executablePath.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}
	const QFileInfo info(trimmed);
	const QString absolutePath = info.isAbsolute() ? info.absoluteFilePath() : QDir(rootPath).absoluteFilePath(trimmed);
	return QDir::cleanPath(absolutePath);
}

QString findExecutableOverride(const CompilerToolDescriptor& descriptor, const QString& rootPath, const QVector<CompilerToolPathOverride>& overrides, QStringList* warnings)
{
	const QString descriptorId = normalizedId(descriptor.id);
	for (const CompilerToolPathOverride& override : overrides) {
		if (normalizedId(override.toolId) != descriptorId) {
			continue;
		}
		const QString candidate = absoluteExecutableOverride(override.executablePath, rootPath);
		if (candidate.isEmpty()) {
			continue;
		}
		if (QFileInfo(candidate).isFile()) {
			return candidate;
		}
		if (warnings) {
			warnings->push_back(QCoreApplication::translate("VibeStudioCompilerRegistry", "Configured compiler executable does not exist: %1").arg(QDir::toNativeSeparators(candidate)));
		}
		return {};
	}
	return {};
}

QString findExecutableCandidate(const CompilerToolDescriptor& descriptor, const QString& rootPath, const QStringList& extraSearchPaths)
{
	for (const QString& relativePath : descriptor.candidateRelativePaths) {
		const QString candidate = absolutePath(rootPath, relativePath);
		if (QFileInfo(candidate).isFile()) {
			return candidate;
		}
	}

	for (const QString& searchPath : extraSearchPaths) {
		for (const QString& executable : descriptor.executableNames) {
			const QString candidate = QDir(searchPath).absoluteFilePath(executable);
			if (QFileInfo(candidate).isFile()) {
				return QDir::cleanPath(candidate);
			}
		}
	}

	for (const QString& executable : descriptor.executableNames) {
		const QString found = QStandardPaths::findExecutable(executable);
		if (!found.isEmpty()) {
			return QDir::cleanPath(found);
		}
	}

	return {};
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

QString commandLineText(const QString& program, const QStringList& arguments)
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

// VibeMap2 builds each tool under build/src/<tool directory>, inside a Release folder for
// multi-config generators, and installs every executable flat into the install prefix
// (external/compilers/vibemap2/src/*/CMakeLists.txt).
QStringList vibemap2ToolCandidatePaths(const QString& sourceDirectory, const QStringList& baseNames)
{
	const QStringList directories = {
		QStringLiteral("external/compilers/vibemap2/build/src/%1").arg(sourceDirectory),
		QStringLiteral("external/compilers/vibemap2/build/src/%1/Release").arg(sourceDirectory),
		QStringLiteral("external/compilers/vibemap2/install"),
	};
	QStringList paths;
	for (const QString& directory : directories) {
		for (const QString& baseName : baseNames) {
			paths << QStringLiteral("%1/%2.exe").arg(directory, baseName) << QStringLiteral("%1/%2").arg(directory, baseName);
		}
	}
	return paths;
}

// VibeMap3 presets build into build/<preset>/bin (external/compilers/vibemap3/CMakePresets.json)
// and install into <prefix>/bin.
QStringList vibemap3CandidatePaths(const QStringList& baseNames)
{
	const QStringList directories = {
		QStringLiteral("external/compilers/vibemap3/build/release/bin"),
		QStringLiteral("external/compilers/vibemap3/build/cli/bin"),
		QStringLiteral("external/compilers/vibemap3/build/cpu-only/bin"),
		QStringLiteral("external/compilers/vibemap3/install/bin"),
	};
	QStringList paths;
	for (const QString& directory : directories) {
		for (const QString& baseName : baseNames) {
			paths << QStringLiteral("%1/%2.exe").arg(directory, baseName) << QStringLiteral("%1/%2").arg(directory, baseName);
		}
	}
	return paths;
}

QString firstUsefulProbeLine(const QString& text)
{
	const QStringList lines = text.split('\n');
	for (QString line : lines) {
		line = line.trimmed();
		if (!line.isEmpty()) {
			return line.left(240);
		}
	}
	return {};
}

// VibeMap2 prints "---- <tool> / VibeMap2 <version> ----" (src/common/settings.cc), VibeMap3
// answers --version with "VibeMap3 <version> (NRC <revision>)" (tools/quake3/q3map2/main.cpp),
// ZDBSP prints "ZDBSP <version> (...)" and ZokumBSP prints "ZokumBSP Version: <version> ...".
// The pre-rename banners (VibeyMapTools, q3mapx) and their upstreams' (ericw-tools, q3map2)
// are still recognised for executables configured by hand.
QString decodeProbeOutput(const QByteArray& bytes)
{
	if (bytes.isEmpty()) {
		return {};
	}
	QStringDecoder utf8(QStringConverter::Utf8);
	const QString decoded = utf8.decode(bytes);
	if (utf8.hasError()) {
		return QString::fromLocal8Bit(bytes);
	}
	return decoded;
}

QString versionBannerLine(const QString& text)
{
	static const QRegularExpression bannerPattern(QStringLiteral(R"regex((?:vibemap2|vibemap3|vibeymaptools|q3mapx|ericw-tools|zdbsp|zokumbsp|zennode|q3map|netradiant|version)\b)regex"), QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression numberPattern(QStringLiteral(R"regex(\b\d+\.\d+)regex"));
	const QStringList lines = text.split('\n');
	for (QString line : lines) {
		line = line.trimmed();
		if (line.isEmpty()) {
			continue;
		}
		if (bannerPattern.match(line).hasMatch() && numberPattern.match(line).hasMatch()) {
			return line.left(240);
		}
	}
	return {};
}

void probeCompilerVersion(CompilerToolDiscovery* discovery, int startTimeoutMs, int finishTimeoutMs)
{
	if (!discovery || !discovery->executableAvailable || !discovery->descriptor.versionProbeSupported) {
		return;
	}

	discovery->versionProbeAttempted = true;
	discovery->versionProbeCommandLine = commandLineText(discovery->executablePath, discovery->descriptor.versionProbeArguments);

	QProcess process;
	process.setProgram(discovery->executablePath);
	process.setArguments(discovery->descriptor.versionProbeArguments);
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start();
	if (!process.waitForStarted(std::max(100, startTimeoutMs))) {
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Failed;
		discovery->warnings << QCoreApplication::translate("VibeStudioCompilerRegistry", "Version probe could not start.");
		return;
	}
	if (!process.waitForFinished(std::max(250, finishTimeoutMs))) {
		process.kill();
		process.waitForFinished(500);
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Failed;
		discovery->warnings << QCoreApplication::translate("VibeStudioCompilerRegistry", "Version probe timed out.");
		return;
	}

	discovery->versionProbeExitCode = process.exitCode();
	// VibeMap2 formats its banner with fmt and emits UTF-8; VibeMap3 (like q3map2) echoes
	// the narrow argv it was handed, which on Windows is the ANSI codepage. Decoding
	// UTF-8 first with a local-8-bit fallback reads both correctly, and matches
	// what the compiler runner does with streamed output.
	const QString output = decodeProbeOutput(process.readAllStandardOutput());
	if (process.exitStatus() != QProcess::NormalExit) {
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Failed;
		discovery->warnings << QCoreApplication::translate("VibeStudioCompilerRegistry", "Version probe crashed.");
		return;
	}

	const QString banner = versionBannerLine(output);
	if (!banner.isEmpty()) {
		discovery->versionText = banner;
		discovery->versionAvailable = true;
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Probed;
		return;
	}

	const QString usefulLine = firstUsefulProbeLine(output);
	if (!usefulLine.isEmpty()) {
		// Output arrived but no recognisable banner; report it as inferred rather than claiming success.
		discovery->versionText = usefulLine;
		discovery->versionAvailable = false;
		discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Inferred;
		discovery->warnings << QCoreApplication::translate("VibeStudioCompilerRegistry", "Version probe produced output without a recognizable version banner; the recorded text is a best guess.");
		return;
	}

	discovery->versionProbeOutcome = CompilerVersionProbeOutcome::Failed;
	discovery->warnings << QCoreApplication::translate("VibeStudioCompilerRegistry", "Version probe produced no usable output (exit code %1).").arg(discovery->versionProbeExitCode);
}

CompilerToolDescriptor tool(
	const QString& id,
	const QString& integrationId,
	const QString& displayName,
	const QString& engineFamily,
	const QString& role,
	const QString& sourcePath,
	const QStringList& baseExecutableNames,
	const QStringList& candidateRelativePaths,
	const QStringList& versionProbeArguments,
	const QStringList& capabilityFlags,
	const QStringList& readinessWarnings = {},
	bool versionProbeSupported = true)
{
	QStringList executableNames;
	for (const QString& name : baseExecutableNames) {
		executableNames << executableName(name);
	}
	return {id, integrationId, displayName, engineFamily, role, sourcePath, executableNames, candidateRelativePaths, versionProbeArguments, capabilityFlags, readinessWarnings, versionProbeSupported};
}

} // namespace

QString compilerVersionProbeOutcomeId(CompilerVersionProbeOutcome outcome)
{
	switch (outcome) {
	case CompilerVersionProbeOutcome::Probed:
		return QStringLiteral("probed");
	case CompilerVersionProbeOutcome::Inferred:
		return QStringLiteral("inferred");
	case CompilerVersionProbeOutcome::Failed:
		return QStringLiteral("failed");
	case CompilerVersionProbeOutcome::NotAttempted:
		break;
	}
	return QStringLiteral("not-attempted");
}

QString compilerVersionProbeOutcomeText(CompilerVersionProbeOutcome outcome)
{
	switch (outcome) {
	case CompilerVersionProbeOutcome::Probed:
		return QCoreApplication::translate("VibeStudioCompilerRegistry", "Probed");
	case CompilerVersionProbeOutcome::Inferred:
		return QCoreApplication::translate("VibeStudioCompilerRegistry", "Inferred");
	case CompilerVersionProbeOutcome::Failed:
		return QCoreApplication::translate("VibeStudioCompilerRegistry", "Failed");
	case CompilerVersionProbeOutcome::NotAttempted:
		break;
	}
	return QCoreApplication::translate("VibeStudioCompilerRegistry", "Not attempted");
}

OperationState CompilerToolDiscovery::state() const
{
	if (executableAvailable) {
		return OperationState::Completed;
	}
	if (sourceAvailable) {
		return OperationState::Warning;
	}
	return OperationState::Failed;
}

OperationState CompilerRegistrySummary::overallState() const
{
	if (tools.isEmpty()) {
		return OperationState::Idle;
	}
	if (executableAvailableCount == tools.size()) {
		return OperationState::Completed;
	}
	if (sourceAvailableCount > 0 || executableAvailableCount > 0) {
		return OperationState::Warning;
	}
	return OperationState::Failed;
}

QVector<CompilerToolDescriptor> compilerToolDescriptors()
{
	return {
		tool(QStringLiteral("vibemap2-bsp"), QStringLiteral("vibemap2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "VibeMap2 bsp"), QStringLiteral("idTech2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Quake BSP compiler"), QStringLiteral("external/compilers/vibemap2"), {QStringLiteral("vibemap2-bsp"), QStringLiteral("vmt-bsp")}, vibemap2ToolCandidatePaths(QStringLiteral("qbsp"), {QStringLiteral("vibemap2-bsp"), QStringLiteral("vmt-bsp")}), {}, {QStringLiteral("quake-map-to-bsp"), QStringLiteral("bsp2"), QStringLiteral("lit-support"), QStringLiteral("vibemap2")}),
		tool(QStringLiteral("vibemap2-vis"), QStringLiteral("vibemap2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "VibeMap2 vis"), QStringLiteral("idTech2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Quake visibility compiler"), QStringLiteral("external/compilers/vibemap2"), {QStringLiteral("vibemap2-vis"), QStringLiteral("vmt-vis")}, vibemap2ToolCandidatePaths(QStringLiteral("vis"), {QStringLiteral("vibemap2-vis"), QStringLiteral("vmt-vis")}), {}, {QStringLiteral("quake-vis"), QStringLiteral("fastvis"), QStringLiteral("vibemap2")}),
		tool(QStringLiteral("vibemap2-light"), QStringLiteral("vibemap2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "VibeMap2 light"), QStringLiteral("idTech2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Quake light compiler"), QStringLiteral("external/compilers/vibemap2"), {QStringLiteral("vibemap2-light"), QStringLiteral("vmt-light")}, vibemap2ToolCandidatePaths(QStringLiteral("light"), {QStringLiteral("vibemap2-light"), QStringLiteral("vmt-light")}), {}, {QStringLiteral("quake-light"), QStringLiteral("bounce-light"), QStringLiteral("lit-output"), QStringLiteral("vibemap2")}),
		tool(QStringLiteral("vibemap2-bspinfo"), QStringLiteral("vibemap2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "VibeMap2 bspinfo"), QStringLiteral("idTech2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Quake BSP inspection helper"), QStringLiteral("external/compilers/vibemap2"), {QStringLiteral("vibemap2-bspinfo"), QStringLiteral("vmt-bspinfo")}, vibemap2ToolCandidatePaths(QStringLiteral("bspinfo"), {QStringLiteral("vibemap2-bspinfo"), QStringLiteral("vmt-bspinfo")}), {}, {QStringLiteral("bsp-inspection"), QStringLiteral("bsp-metadata"), QStringLiteral("captured-output-log"), QStringLiteral("vibemap2-helper"), QStringLiteral("vibemap2"), QStringLiteral("helper-probe-limited"), QStringLiteral("upstream-issue-225"), QStringLiteral("upstream-issue-289")}, {QCoreApplication::translate("VibeStudioCompilerRegistry", "Helper discovery currently verifies executable presence and help/version output only; operation-level bspinfo diagnostics still need smoke-test coverage before automation depends on them.")}),
		tool(QStringLiteral("vibemap2-bsputil"), QStringLiteral("vibemap2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "VibeMap2 bsputil"), QStringLiteral("idTech2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Quake BSP utility helper"), QStringLiteral("external/compilers/vibemap2"), {QStringLiteral("vibemap2-bsputil"), QStringLiteral("vmt-bsputil")}, vibemap2ToolCandidatePaths(QStringLiteral("bsputil"), {QStringLiteral("vibemap2-bsputil"), QStringLiteral("vmt-bsputil")}), {}, {QStringLiteral("bsp-utility"), QStringLiteral("bsp-inspection"), QStringLiteral("bsp-mutation-risk"), QStringLiteral("vibemap2-helper"), QStringLiteral("vibemap2"), QStringLiteral("helper-probe-limited"), QStringLiteral("argument-parser-risk"), QStringLiteral("upstream-issue-289"), QStringLiteral("upstream-issue-435")}, {QCoreApplication::translate("VibeStudioCompilerRegistry", "bsputil has a known argument parsing risk inherited from ericw-tools (#435); VibeStudio should keep BSP-changing operations behind explicit operation smoke tests."), QCoreApplication::translate("VibeStudioCompilerRegistry", "Helper discovery currently verifies executable presence and help/version output only; operation-level bsputil diagnostics still need smoke-test coverage before automation depends on them.")}),
		tool(QStringLiteral("vibemap2-hub"), QStringLiteral("vibemap2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "VibeMap2 hub"), QStringLiteral("idTech2"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Quake build and lighting preview hub"), QStringLiteral("external/compilers/vibemap2"), {QStringLiteral("vibemap2-hub"), QStringLiteral("vmt-hub")}, vibemap2ToolCandidatePaths(QStringLiteral("hub"), {QStringLiteral("vibemap2-hub"), QStringLiteral("vmt-hub")}), {}, {QStringLiteral("build-hub"), QStringLiteral("lighting-preview"), QStringLiteral("gui-helper"), QStringLiteral("platform-launch-risk"), QStringLiteral("vibemap2-helper"), QStringLiteral("vibemap2"), QStringLiteral("helper-probe-limited"), QStringLiteral("upstream-issue-480")}, {QCoreApplication::translate("VibeStudioCompilerRegistry", "The hub's launch readiness is not smoke-tested because platform OpenGL/Qt setup can fail on some systems (#480)."), QCoreApplication::translate("VibeStudioCompilerRegistry", "Helper discovery currently verifies executable presence only for the hub; VibeStudio does not launch GUI helpers during registry probes.")}, false),
		tool(QStringLiteral("vibemap3"), QStringLiteral("vibemap3"), QCoreApplication::translate("VibeStudioCompilerRegistry", "VibeMap3"), QStringLiteral("idTech3"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Quake III BSP compiler"), QStringLiteral("external/compilers/vibemap3"), {QStringLiteral("vibemap3"), QStringLiteral("q3mapx")}, vibemap3CandidatePaths({QStringLiteral("vibemap3"), QStringLiteral("q3mapx")}), {QStringLiteral("--version")}, {QStringLiteral("idtech3-bsp"), QStringLiteral("meta"), QStringLiteral("vis"), QStringLiteral("light"), QStringLiteral("shader-aware"), QStringLiteral("vibemap3")}),
		tool(QStringLiteral("zdbsp"), QStringLiteral("zdbsp"), QCoreApplication::translate("VibeStudioCompilerRegistry", "ZDBSP"), QStringLiteral("idTech1"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Doom node builder"), QStringLiteral("external/compilers/zdbsp"), {QStringLiteral("zdbsp")}, {QStringLiteral("external/compilers/zdbsp/build/zdbsp.exe"), QStringLiteral("external/compilers/zdbsp/build/zdbsp"), QStringLiteral("external/compilers/zdbsp/zdbsp.exe"), QStringLiteral("external/compilers/zdbsp/zdbsp")}, {QStringLiteral("-V")}, {QStringLiteral("doom-nodes"), QStringLiteral("extended-nodes"), QStringLiteral("gl-nodes")}),
		tool(QStringLiteral("zokumbsp"), QStringLiteral("zokumbsp"), QCoreApplication::translate("VibeStudioCompilerRegistry", "ZokumBSP"), QStringLiteral("idTech1"), QCoreApplication::translate("VibeStudioCompilerRegistry", "Doom node/blockmap/reject builder"), QStringLiteral("external/compilers/zokumbsp"), {QStringLiteral("zokumbsp"), QStringLiteral("zennode")}, {QStringLiteral("external/compilers/zokumbsp/build/zokumbsp.exe"), QStringLiteral("external/compilers/zokumbsp/build/zokumbsp"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zokumbsp.exe"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zokumbsp"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zennode.exe"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zennode")}, {}, {QStringLiteral("doom-nodes"), QStringLiteral("blockmap"), QStringLiteral("reject"), QStringLiteral("visplane-aware")}),
	};
}

bool compilerToolDescriptorForId(const QString& id, CompilerToolDescriptor* out)
{
	const QString requested = normalizedId(id);
	for (const CompilerToolDescriptor& descriptor : compilerToolDescriptors()) {
		if (normalizedId(descriptor.id) == requested) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

QString renamedCompilerId(const QString& retiredId)
{
	static const QHash<QString, QString> renames = {
		{QStringLiteral("ericw-qbsp"), QStringLiteral("vibemap2-bsp")},
		{QStringLiteral("ericw-vis"), QStringLiteral("vibemap2-vis")},
		{QStringLiteral("ericw-light"), QStringLiteral("vibemap2-light")},
		{QStringLiteral("ericw-bspinfo"), QStringLiteral("vibemap2-bspinfo")},
		{QStringLiteral("ericw-bsputil"), QStringLiteral("vibemap2-bsputil")},
		{QStringLiteral("ericw-bsputil-check"), QStringLiteral("vibemap2-bsputil-check")},
		{QStringLiteral("ericw-bsputil-extract-entities"), QStringLiteral("vibemap2-bsputil-extract-entities")},
		{QStringLiteral("ericw-bsputil-extract-textures"), QStringLiteral("vibemap2-bsputil-extract-textures")},
		{QStringLiteral("ericw-lightpreview"), QStringLiteral("vibemap2-hub")},
		{QStringLiteral("q3map2"), QStringLiteral("vibemap3")},
		{QStringLiteral("q3map2-probe"), QStringLiteral("vibemap3-probe")},
		{QStringLiteral("q3map2-bsp"), QStringLiteral("vibemap3-bsp")},
		{QStringLiteral("q3map2-vis"), QStringLiteral("vibemap3-vis")},
		{QStringLiteral("q3map2-light"), QStringLiteral("vibemap3-light")},
		{QStringLiteral("q3map2-convert"), QStringLiteral("vibemap3-convert")},
		{QStringLiteral("q3map2-pk3"), QStringLiteral("vibemap3-pk3")},
	};
	return renames.value(normalizedId(retiredId));
}

QString unknownCompilerToolIdText(const QString& id)
{
	const QString renamed = renamedCompilerId(id);
	if (!renamed.isEmpty()) {
		return QCoreApplication::translate("VibeStudioCompilerRegistry", "Unknown compiler tool id: %1. It was renamed %2 when VibeStudio moved to VibeMap2 and VibeMap3.").arg(id, renamed);
	}
	return QCoreApplication::translate("VibeStudioCompilerRegistry", "Unknown compiler tool id: %1").arg(id);
}

CompilerRegistrySummary discoverCompilerTools(const QString& workspaceRootPath, const QStringList& extraSearchPaths)
{
	CompilerRegistryOptions options;
	options.workspaceRootPath = workspaceRootPath;
	options.extraSearchPaths = extraSearchPaths;
	return discoverCompilerTools(options);
}

CompilerRegistrySummary discoverCompilerTools(const CompilerRegistryOptions& options)
{
	CompilerRegistrySummary summary;
	const QString rootPath = options.workspaceRootPath.trimmed().isEmpty() ? QDir::currentPath() : options.workspaceRootPath;

	for (const CompilerToolDescriptor& descriptor : compilerToolDescriptors()) {
		CompilerToolDiscovery discovery;
		discovery.descriptor = descriptor;
		discovery.capabilityFlags = descriptor.capabilityFlags;
		discovery.warnings.append(descriptor.readinessWarnings);
		discovery.sourcePath = absolutePath(rootPath, descriptor.sourcePath);
		discovery.sourceAvailable = QFileInfo(discovery.sourcePath).isDir();
		discovery.executablePath = findExecutableOverride(descriptor, rootPath, options.executableOverrides, &discovery.warnings);
		discovery.executablePathOverridden = !discovery.executablePath.isEmpty();
		if (discovery.executablePath.isEmpty()) {
			discovery.executablePath = findExecutableCandidate(descriptor, rootPath, options.extraSearchPaths);
		}
		discovery.executableAvailable = !discovery.executablePath.isEmpty();
		if (options.probeVersions) {
			probeCompilerVersion(&discovery, options.versionProbeStartTimeoutMs, options.versionProbeTimeoutMs);
		}
		if (!discovery.sourceAvailable) {
			discovery.warnings << QCoreApplication::translate("VibeStudioCompilerRegistry", "Compiler source directory is missing.");
		}
		if (!discovery.executableAvailable) {
			discovery.warnings << QCoreApplication::translate("VibeStudioCompilerRegistry", "Compiler executable was not found in known build paths or PATH.");
		}

		if (discovery.sourceAvailable) {
			++summary.sourceAvailableCount;
		}
		if (discovery.executableAvailable) {
			++summary.executableAvailableCount;
		}
		summary.warningCount += discovery.warnings.size();
		summary.tools.push_back(discovery);
	}

	return summary;
}

QString compilerRegistrySummaryText(const CompilerRegistrySummary& summary)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Compiler registry");
	lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Tools: %1").arg(summary.tools.size());
	lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Source available: %1").arg(summary.sourceAvailableCount);
	lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Executables available: %1").arg(summary.executableAvailableCount);
	lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Warnings: %1").arg(summary.warningCount);
	for (const CompilerToolDiscovery& discovery : summary.tools) {
		lines << QString();
		lines << QStringLiteral("%1 [%2]").arg(discovery.descriptor.displayName, discovery.descriptor.id);
		lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Engine: %1").arg(discovery.descriptor.engineFamily);
		lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Role: %1").arg(discovery.descriptor.role);
		lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Source: %1").arg(QDir::toNativeSeparators(discovery.sourcePath));
		lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Executable: %1").arg(discovery.executablePath.isEmpty() ? QCoreApplication::translate("VibeStudioCompilerRegistry", "not found") : QDir::toNativeSeparators(discovery.executablePath));
		lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Executable override: %1").arg(discovery.executablePathOverridden ? QCoreApplication::translate("VibeStudioCompilerRegistry", "yes") : QCoreApplication::translate("VibeStudioCompilerRegistry", "no"));
		lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Version: %1").arg(discovery.versionText.isEmpty() ? QCoreApplication::translate("VibeStudioCompilerRegistry", "unknown") : discovery.versionText);
		lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Version probe: %1").arg(compilerVersionProbeOutcomeText(discovery.versionProbeOutcome));
		lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Capabilities: %1").arg(discovery.capabilityFlags.isEmpty() ? QCoreApplication::translate("VibeStudioCompilerRegistry", "none") : discovery.capabilityFlags.join(QStringLiteral(", ")));
		for (const QString& warning : discovery.warnings) {
			lines << QCoreApplication::translate("VibeStudioCompilerRegistry", "Warning: %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
