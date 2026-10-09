#include "core/compiler_registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool touchFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	file.write("tool");
	return true;
}

QString platformExecutableName(const QString& baseName)
{
#if defined(Q_OS_WIN)
	return QStringLiteral("%1.exe").arg(baseName);
#else
	return baseName;
#endif
}

const vibestudio::CompilerToolDescriptor* descriptorById(const QVector<vibestudio::CompilerToolDescriptor>& descriptors, const QString& id)
{
	for (const vibestudio::CompilerToolDescriptor& descriptor : descriptors) {
		if (descriptor.id == id) {
			return &descriptor;
		}
	}
	return nullptr;
}

const vibestudio::CompilerToolDiscovery* discoveryById(const QVector<vibestudio::CompilerToolDiscovery>& discoveries, const QString& id)
{
	for (const vibestudio::CompilerToolDiscovery& discovery : discoveries) {
		if (discovery.descriptor.id == id) {
			return &discovery;
		}
	}
	return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QStringList appArgs = QCoreApplication::arguments();
	// ZDBSP supports "-V/--version" (external/compilers/zdbsp/main.cpp) and VibeMap3 "--version";
	// this stand-in answers VibeMap3's probe without a version banner.
	if (appArgs.contains(QStringLiteral("-V"))) {
		std::cout << "ZDBSP 1.19 (fake build)\n";
		return EXIT_SUCCESS;
	}
	if (appArgs.contains(QStringLiteral("-help")) || appArgs.contains(QStringLiteral("--version"))) {
		std::cout << "this fake tool prints no version information\n";
		return EXIT_SUCCESS;
	}

	const QVector<vibestudio::CompilerToolDescriptor> descriptors = vibestudio::compilerToolDescriptors();
	if (descriptors.size() < 9) {
		return fail("Expected compiler descriptors for imported tools.");
	}

	const vibestudio::CompilerToolDescriptor* qbsp = descriptorById(descriptors, QStringLiteral("vibemap2-bsp"));
	const vibestudio::CompilerToolDescriptor* bspinfo = descriptorById(descriptors, QStringLiteral("vibemap2-bspinfo"));
	const vibestudio::CompilerToolDescriptor* bsputil = descriptorById(descriptors, QStringLiteral("vibemap2-bsputil"));
	const vibestudio::CompilerToolDescriptor* lightpreview = descriptorById(descriptors, QStringLiteral("vibemap2-hub"));
	const vibestudio::CompilerToolDescriptor* zdbsp = descriptorById(descriptors, QStringLiteral("zdbsp"));
	if (!qbsp || !bspinfo || !bsputil || !lightpreview || !zdbsp) {
		return fail("Expected first-class VibeMap2 helper descriptors.");
	}
	// VibeMap2, like ericw-tools, has no --version: an unknown option prints help and still exits 0, and
	// "bspinfo --help" would try to open "--help.bsp". Probing with no arguments is the honest form.
	if (!qbsp->versionProbeArguments.isEmpty() || !qbsp->versionProbeSupported) {
		return fail("Expected the VibeMap2 bsp probe to run with no arguments.");
	}
	if (!bspinfo->versionProbeArguments.isEmpty() || !bspinfo->versionProbeSupported) {
		return fail("Expected the bspinfo probe to stop passing --help.");
	}
	if (!bsputil->versionProbeArguments.isEmpty() || !bsputil->versionProbeSupported) {
		return fail("Expected the bsputil probe to stop passing --help.");
	}
	if (zdbsp->versionProbeArguments != QStringList{QStringLiteral("-V")}) {
		return fail("Expected ZDBSP to be probed with its real version flag.");
	}
	if (!bspinfo->capabilityFlags.contains(QStringLiteral("upstream-issue-225"))
		|| !bspinfo->capabilityFlags.contains(QStringLiteral("upstream-issue-289"))) {
		return fail("Expected bspinfo issue capabilities.");
	}
	if (!bsputil->capabilityFlags.contains(QStringLiteral("argument-parser-risk"))
		|| !bsputil->capabilityFlags.contains(QStringLiteral("upstream-issue-435"))
		|| bsputil->readinessWarnings.isEmpty()) {
		return fail("Expected bsputil argument parsing readiness warning.");
	}
	if (!lightpreview->capabilityFlags.contains(QStringLiteral("platform-launch-risk"))
		|| !lightpreview->capabilityFlags.contains(QStringLiteral("gui-helper"))
		|| lightpreview->versionProbeSupported
		|| lightpreview->readinessWarnings.isEmpty()
		|| !lightpreview->executableNames.join(QLatin1Char(' ')).contains(QStringLiteral("vibemap2-hub"))) {
		return fail("Expected hub launch readiness metadata without an unsafe probe.");
	}
	// The pre-rename VibeyMapTools binaries stay discoverable; stock ericw-tools names do not.
	if (!qbsp->executableNames.join(QLatin1Char(' ')).contains(QStringLiteral("vmt-bsp"))
		|| qbsp->executableNames.join(QLatin1Char(' ')).contains(QStringLiteral("qbsp"))) {
		return fail("Expected VibeMap2 bsp to look for vibemap2-bsp and vmt-bsp only.");
	}

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary workspace.");
	}
	QDir root(tempDir.path());
	// VibeMap2 builds each tool under build/src/<tool directory>.
	if (!root.mkpath(QStringLiteral("external/compilers/vibemap2/build/src/qbsp")) || !root.mkpath(QStringLiteral("external/compilers/vibemap2/build/src/bspinfo"))) {
		return fail("Expected fake compiler build directory.");
	}

	const QString qbspName = platformExecutableName(QStringLiteral("vibemap2-bsp"));
	if (!touchFile(root.filePath(QStringLiteral("external/compilers/vibemap2/build/src/qbsp/%1").arg(qbspName)))) {
		return fail("Expected fake vibemap2-bsp executable.");
	}
	const QString bspinfoName = platformExecutableName(QStringLiteral("vibemap2-bspinfo"));
	const QString fakeBspinfoPath = root.filePath(QStringLiteral("external/compilers/vibemap2/build/src/bspinfo/%1").arg(bspinfoName));
	if (!touchFile(fakeBspinfoPath)) {
		return fail("Expected fake bspinfo executable.");
	}

	vibestudio::CompilerRegistryOptions discoveryOptions;
	discoveryOptions.workspaceRootPath = tempDir.path();
	discoveryOptions.probeVersions = false;
	const vibestudio::CompilerRegistrySummary summary = vibestudio::discoverCompilerTools(discoveryOptions);
	if (summary.tools.size() != descriptors.size()) {
		return fail("Expected one discovery result per descriptor.");
	}
	if (summary.sourceAvailableCount < 6 || summary.executableAvailableCount < 2) {
		return fail("Expected fake VibeMap2 source and helper executable discovery.");
	}
	if (summary.overallState() != vibestudio::OperationState::Warning) {
		return fail("Expected partial compiler discovery to warn.");
	}
	if (!vibestudio::compilerRegistrySummaryText(summary).contains(QStringLiteral("VibeMap2 bsp"))) {
		return fail("Expected registry text to include VibeMap2 bsp.");
	}
	const vibestudio::CompilerToolDiscovery* bspinfoDiscovery = discoveryById(summary.tools, QStringLiteral("vibemap2-bspinfo"));
	if (!bspinfoDiscovery
		|| !bspinfoDiscovery->executableAvailable
		|| bspinfoDiscovery->executablePath != QDir::cleanPath(fakeBspinfoPath)
		|| bspinfoDiscovery->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::NotAttempted
		|| !bspinfoDiscovery->warnings.join('\n').contains(QStringLiteral("operation-level bspinfo diagnostics"))) {
		return fail("Expected fake bspinfo helper discovery with readiness warning.");
	}
	vibestudio::CompilerToolDescriptor vibemap3;
	if (!vibestudio::compilerToolDescriptorForId(QStringLiteral("vibemap3"), &vibemap3) || !vibemap3.capabilityFlags.contains(QStringLiteral("idtech3-bsp"))
		|| vibemap3.versionProbeArguments != QStringList{QStringLiteral("--version")}) {
		return fail("Expected VibeMap3 capability flags and its --version probe.");
	}
	// Retired ids are explained, never resolved silently.
	if (vibestudio::compilerToolDescriptorForId(QStringLiteral("q3map2"))
		|| vibestudio::renamedCompilerId(QStringLiteral("q3map2")) != QStringLiteral("vibemap3")
		|| vibestudio::renamedCompilerId(QStringLiteral("ERICW_QBSP")) != QStringLiteral("vibemap2-bsp")
		|| !vibestudio::renamedCompilerId(QStringLiteral("zdbsp")).isEmpty()
		|| !vibestudio::unknownCompilerToolIdText(QStringLiteral("ericw-light")).contains(QStringLiteral("vibemap2-light"))) {
		return fail("Expected retired ericw-tools/q3map2 ids to map to their VibeMap2/VibeMap3 names in messages only.");
	}

	const QString overridePath = root.filePath(QStringLiteral("custom-qbsp"));
	if (!touchFile(overridePath)) {
		return fail("Expected fake override executable.");
	}
	vibestudio::CompilerRegistryOptions options;
	options.workspaceRootPath = tempDir.path();
	options.probeVersions = false;
	options.executableOverrides.push_back({QStringLiteral("vibemap2-bsp"), overridePath});
	const vibestudio::CompilerRegistrySummary overrideSummary = vibestudio::discoverCompilerTools(options);
	bool overrideApplied = false;
	for (const vibestudio::CompilerToolDiscovery& discovery : overrideSummary.tools) {
		if (discovery.descriptor.id == QStringLiteral("vibemap2-bsp")) {
			overrideApplied = discovery.executablePathOverridden && discovery.executablePath == QDir::cleanPath(overridePath);
		}
	}
	if (!overrideApplied) {
		return fail("Expected configured executable override to win discovery.");
	}

	const QString notAnExecutable = root.filePath(QStringLiteral("not-an-executable.bin"));
	if (!touchFile(notAnExecutable)) {
		return fail("Expected non-executable fixture.");
	}
	vibestudio::CompilerRegistryOptions probeOptions;
	probeOptions.workspaceRootPath = tempDir.path();
	probeOptions.probeVersions = true;
	probeOptions.versionProbeStartTimeoutMs = 750;
	probeOptions.versionProbeTimeoutMs = 4000;
	probeOptions.executableOverrides.push_back({QStringLiteral("zdbsp"), QCoreApplication::applicationFilePath()});
	probeOptions.executableOverrides.push_back({QStringLiteral("vibemap3"), QCoreApplication::applicationFilePath()});
	probeOptions.executableOverrides.push_back({QStringLiteral("zokumbsp"), notAnExecutable});
	const vibestudio::CompilerRegistrySummary probeSummary = vibestudio::discoverCompilerTools(probeOptions);

	const vibestudio::CompilerToolDiscovery* zdbspProbe = discoveryById(probeSummary.tools, QStringLiteral("zdbsp"));
	if (!zdbspProbe
		|| zdbspProbe->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::Probed
		|| !zdbspProbe->versionAvailable
		|| !zdbspProbe->versionText.contains(QStringLiteral("ZDBSP 1.19"))) {
		return fail("Expected a recognizable version banner to be reported as probed.");
	}
	const vibestudio::CompilerToolDiscovery* vibemap3Probe = discoveryById(probeSummary.tools, QStringLiteral("vibemap3"));
	if (!vibemap3Probe
		|| vibemap3Probe->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::Inferred
		|| vibemap3Probe->versionAvailable
		|| vibemap3Probe->versionProbeExitCode != 0) {
		return fail("Expected output without a version banner to be reported as inferred, not successful.");
	}
	const vibestudio::CompilerToolDiscovery* zokumProbe = discoveryById(probeSummary.tools, QStringLiteral("zokumbsp"));
	if (!zokumProbe
		|| zokumProbe->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::Failed
		|| zokumProbe->versionAvailable) {
		return fail("Expected an unusable probe target to be reported as failed.");
	}
	const vibestudio::CompilerToolDiscovery* lightpreviewProbe = discoveryById(probeSummary.tools, QStringLiteral("vibemap2-hub"));
	if (!lightpreviewProbe
		|| lightpreviewProbe->versionProbeAttempted
		|| lightpreviewProbe->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::NotAttempted) {
		return fail("Expected the GUI hub helper never to be launched by a probe.");
	}
	if (!vibestudio::compilerRegistrySummaryText(probeSummary).contains(vibestudio::compilerVersionProbeOutcomeText(vibestudio::CompilerVersionProbeOutcome::Probed))) {
		return fail("Expected the registry report to state the version probe outcome.");
	}

	return EXIT_SUCCESS;
}
