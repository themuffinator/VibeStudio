#include "core/compiler_registry.h"

#include "core/studio_manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace vibestudio {

namespace {

QString registryText(const char* source)
{
	return QCoreApplication::translate("VibeStudioCompilerRegistry", source);
}

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
			warnings->push_back(registryText("Configured compiler executable does not exist: %1").arg(QDir::toNativeSeparators(candidate)));
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

QStringList ericwToolCandidatePaths(const QString& baseName)
{
	const QString buildPath = QStringLiteral("external/compilers/ericw-tools/build/bin/%1");
	const QString binPath = QStringLiteral("external/compilers/ericw-tools/bin/%1");
	return {
		buildPath.arg(QStringLiteral("%1.exe").arg(baseName)),
		buildPath.arg(baseName),
		binPath.arg(QStringLiteral("%1.exe").arg(baseName)),
		binPath.arg(baseName),
	};
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

void probeCompilerVersion(CompilerToolDiscovery* discovery, int timeoutMs)
{
	if (!discovery || !discovery->executableAvailable || discovery->descriptor.versionProbeArguments.isEmpty()) {
		return;
	}

	discovery->versionProbeAttempted = true;
	discovery->versionProbeCommandLine = commandLineText(discovery->executablePath, discovery->descriptor.versionProbeArguments);

	QProcess process;
	process.setProgram(discovery->executablePath);
	process.setArguments(discovery->descriptor.versionProbeArguments);
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.start();
	if (!process.waitForStarted(std::max(250, timeoutMs))) {
		discovery->warnings << registryText("Version probe could not start.");
		return;
	}
	if (!process.waitForFinished(std::max(250, timeoutMs))) {
		process.kill();
		process.waitForFinished(500);
		discovery->warnings << registryText("Version probe timed out.");
		return;
	}

	const QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
	const QString usefulLine = firstUsefulProbeLine(output);
	if (!usefulLine.isEmpty()) {
		discovery->versionText = usefulLine;
		discovery->versionAvailable = true;
		return;
	}
	if (process.exitStatus() == QProcess::NormalExit) {
		discovery->versionText = registryText("Probe completed with exit code %1.").arg(process.exitCode());
		discovery->versionAvailable = true;
	} else {
		discovery->warnings << registryText("Version probe crashed.");
	}
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
	const QStringList& readinessWarnings = {})
{
	QStringList executableNames;
	for (const QString& name : baseExecutableNames) {
		executableNames << executableName(name);
	}
	return {id, integrationId, displayName, engineFamily, role, sourcePath, executableNames, candidateRelativePaths, versionProbeArguments, capabilityFlags, readinessWarnings};
}

} // namespace

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
		tool(QStringLiteral("ericw-qbsp"), QStringLiteral("ericw-tools"), registryText("ericw-tools qbsp"), QStringLiteral("idTech2"), registryText("Quake BSP compiler"), QStringLiteral("external/compilers/ericw-tools"), {QStringLiteral("qbsp")}, ericwToolCandidatePaths(QStringLiteral("qbsp")), {QStringLiteral("--version")}, {QStringLiteral("quake-map-to-bsp"), QStringLiteral("bsp2"), QStringLiteral("lit-support"), QStringLiteral("ericw-tools")}),
		tool(QStringLiteral("ericw-vis"), QStringLiteral("ericw-tools"), registryText("ericw-tools vis"), QStringLiteral("idTech2"), registryText("Quake visibility compiler"), QStringLiteral("external/compilers/ericw-tools"), {QStringLiteral("vis")}, ericwToolCandidatePaths(QStringLiteral("vis")), {QStringLiteral("--version")}, {QStringLiteral("quake-vis"), QStringLiteral("fastvis"), QStringLiteral("ericw-tools"), QStringLiteral("generic-binary-name-risk"), QStringLiteral("upstream-issue-335")}),
		tool(QStringLiteral("ericw-light"), QStringLiteral("ericw-tools"), registryText("ericw-tools light"), QStringLiteral("idTech2"), registryText("Quake light compiler"), QStringLiteral("external/compilers/ericw-tools"), {QStringLiteral("light")}, ericwToolCandidatePaths(QStringLiteral("light")), {QStringLiteral("--version")}, {QStringLiteral("quake-light"), QStringLiteral("bounce-light"), QStringLiteral("lit-output"), QStringLiteral("ericw-tools"), QStringLiteral("generic-binary-name-risk"), QStringLiteral("upstream-issue-335")}),
		tool(QStringLiteral("ericw-bspinfo"), QStringLiteral("ericw-tools"), registryText("ericw-tools bspinfo"), QStringLiteral("idTech2"), registryText("Quake BSP inspection helper"), QStringLiteral("external/compilers/ericw-tools"), {QStringLiteral("bspinfo")}, ericwToolCandidatePaths(QStringLiteral("bspinfo")), {QStringLiteral("--help")}, {QStringLiteral("bsp-inspection"), QStringLiteral("bsp-metadata"), QStringLiteral("captured-output-log"), QStringLiteral("ericw-helper"), QStringLiteral("ericw-tools"), QStringLiteral("helper-probe-limited"), QStringLiteral("upstream-issue-225"), QStringLiteral("upstream-issue-289")}, {registryText("Helper discovery currently verifies executable presence and help/version output only; operation-level bspinfo diagnostics still need smoke-test coverage before automation depends on them.")}),
		tool(QStringLiteral("ericw-bsputil"), QStringLiteral("ericw-tools"), registryText("ericw-tools bsputil"), QStringLiteral("idTech2"), registryText("Quake BSP utility helper"), QStringLiteral("external/compilers/ericw-tools"), {QStringLiteral("bsputil")}, ericwToolCandidatePaths(QStringLiteral("bsputil")), {QStringLiteral("--help")}, {QStringLiteral("bsp-utility"), QStringLiteral("bsp-inspection"), QStringLiteral("bsp-mutation-risk"), QStringLiteral("ericw-helper"), QStringLiteral("ericw-tools"), QStringLiteral("helper-probe-limited"), QStringLiteral("argument-parser-risk"), QStringLiteral("upstream-issue-289"), QStringLiteral("upstream-issue-435")}, {registryText("bsputil has known upstream argument parsing risk (#435); VibeStudio should keep BSP-changing operations behind explicit operation smoke tests."), registryText("Helper discovery currently verifies executable presence and help/version output only; operation-level bsputil diagnostics still need smoke-test coverage before automation depends on them.")}),
		tool(QStringLiteral("ericw-lightpreview"), QStringLiteral("ericw-tools"), registryText("ericw-tools lightpreview"), QStringLiteral("idTech2"), registryText("Quake lighting preview helper"), QStringLiteral("external/compilers/ericw-tools"), {QStringLiteral("lightpreview")}, ericwToolCandidatePaths(QStringLiteral("lightpreview")), {}, {QStringLiteral("lighting-preview"), QStringLiteral("gui-helper"), QStringLiteral("platform-launch-risk"), QStringLiteral("temp-dir-risk"), QStringLiteral("ericw-helper"), QStringLiteral("ericw-tools"), QStringLiteral("helper-probe-limited"), QStringLiteral("upstream-issue-463"), QStringLiteral("upstream-issue-480")}, {registryText("lightpreview launch readiness is not smoke-tested because platform OpenGL/Qt setup can fail on some systems (#480)."), registryText("lightpreview may write preview outputs beside the map unless launched through an isolated temporary workflow (#463); registry discovery is presence-only for now."), registryText("Helper discovery currently verifies executable presence only for lightpreview; VibeStudio does not launch GUI preview helpers during registry probes.")}),
		tool(QStringLiteral("q3map2"), QStringLiteral("q3map2-nrc"), registryText("NetRadiant Custom q3map2"), QStringLiteral("idTech3"), registryText("Quake III BSP compiler"), QStringLiteral("external/compilers/q3map2-nrc"), {QStringLiteral("q3map2")}, {QStringLiteral("external/compilers/q3map2-nrc/install/q3map2.exe"), QStringLiteral("external/compilers/q3map2-nrc/install/q3map2"), QStringLiteral("external/compilers/q3map2-nrc/tools/quake3/q3map2/q3map2.exe"), QStringLiteral("external/compilers/q3map2-nrc/tools/quake3/q3map2/q3map2")}, {QStringLiteral("-help")}, {QStringLiteral("idtech3-bsp"), QStringLiteral("meta"), QStringLiteral("vis"), QStringLiteral("light"), QStringLiteral("shader-aware")}),
		tool(QStringLiteral("zdbsp"), QStringLiteral("zdbsp"), registryText("ZDBSP"), QStringLiteral("idTech1"), registryText("Doom node builder"), QStringLiteral("external/compilers/zdbsp"), {QStringLiteral("zdbsp")}, {QStringLiteral("external/compilers/zdbsp/build/zdbsp.exe"), QStringLiteral("external/compilers/zdbsp/build/zdbsp"), QStringLiteral("external/compilers/zdbsp/zdbsp.exe"), QStringLiteral("external/compilers/zdbsp/zdbsp")}, {QStringLiteral("-h")}, {QStringLiteral("doom-nodes"), QStringLiteral("extended-nodes"), QStringLiteral("gl-nodes")}),
		tool(QStringLiteral("zokumbsp"), QStringLiteral("zokumbsp"), registryText("ZokumBSP"), QStringLiteral("idTech1"), registryText("Doom node/blockmap/reject builder"), QStringLiteral("external/compilers/zokumbsp"), {QStringLiteral("zokumbsp"), QStringLiteral("zennode")}, {QStringLiteral("external/compilers/zokumbsp/build/zokumbsp.exe"), QStringLiteral("external/compilers/zokumbsp/build/zokumbsp"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zokumbsp.exe"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zokumbsp"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zennode.exe"), QStringLiteral("external/compilers/zokumbsp/src/zokumbsp/zennode")}, {QStringLiteral("-h")}, {QStringLiteral("doom-nodes"), QStringLiteral("blockmap"), QStringLiteral("reject"), QStringLiteral("visplane-aware")}),
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
			probeCompilerVersion(&discovery, options.versionProbeTimeoutMs);
		}
		if (!discovery.sourceAvailable) {
			discovery.warnings << registryText("Compiler source directory is missing.");
		}
		if (!discovery.executableAvailable) {
			discovery.warnings << registryText("Compiler executable was not found in known build paths or PATH.");
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
	lines << registryText("Compiler registry");
	lines << registryText("Tools: %1").arg(summary.tools.size());
	lines << registryText("Source available: %1").arg(summary.sourceAvailableCount);
	lines << registryText("Executables available: %1").arg(summary.executableAvailableCount);
	lines << registryText("Warnings: %1").arg(summary.warningCount);
	for (const CompilerToolDiscovery& discovery : summary.tools) {
		lines << QString();
		lines << QStringLiteral("%1 [%2]").arg(discovery.descriptor.displayName, discovery.descriptor.id);
		lines << registryText("Engine: %1").arg(discovery.descriptor.engineFamily);
		lines << registryText("Role: %1").arg(discovery.descriptor.role);
		lines << registryText("Source: %1").arg(QDir::toNativeSeparators(discovery.sourcePath));
		lines << registryText("Executable: %1").arg(discovery.executablePath.isEmpty() ? registryText("not found") : QDir::toNativeSeparators(discovery.executablePath));
		lines << registryText("Executable override: %1").arg(discovery.executablePathOverridden ? registryText("yes") : registryText("no"));
		lines << registryText("Version: %1").arg(discovery.versionText.isEmpty() ? registryText("unknown") : discovery.versionText);
		lines << registryText("Capabilities: %1").arg(discovery.capabilityFlags.isEmpty() ? registryText("none") : discovery.capabilityFlags.join(QStringLiteral(", ")));
		for (const QString& warning : discovery.warnings) {
			lines << registryText("Warning: %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
