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
	// ZDBSP supports "-V/--version" (external/compilers/zdbsp/main.cpp); q3map2 only has "-help".
	if (appArgs.contains(QStringLiteral("-V"))) {
		std::cout << "ZDBSP 1.19 (fake build)\n";
		return EXIT_SUCCESS;
	}
	if (appArgs.contains(QStringLiteral("-help"))) {
		std::cout << "this fake tool prints no version information\n";
		return EXIT_SUCCESS;
	}

	const QVector<vibestudio::CompilerToolDescriptor> descriptors = vibestudio::compilerToolDescriptors();
	if (descriptors.size() < 9) {
		return fail("Expected compiler descriptors for imported tools.");
	}

	const vibestudio::CompilerToolDescriptor* qbsp = descriptorById(descriptors, QStringLiteral("ericw-qbsp"));
	const vibestudio::CompilerToolDescriptor* bspinfo = descriptorById(descriptors, QStringLiteral("ericw-bspinfo"));
	const vibestudio::CompilerToolDescriptor* bsputil = descriptorById(descriptors, QStringLiteral("ericw-bsputil"));
	const vibestudio::CompilerToolDescriptor* lightpreview = descriptorById(descriptors, QStringLiteral("ericw-lightpreview"));
	const vibestudio::CompilerToolDescriptor* zdbsp = descriptorById(descriptors, QStringLiteral("zdbsp"));
	if (!qbsp || !bspinfo || !bsputil || !lightpreview || !zdbsp) {
		return fail("Expected first-class ericw helper descriptors.");
	}
	// ericw-tools has no --version: an unknown option prints help and still exits 0, and
	// "bspinfo --help" would try to open "--help.bsp". Probing with no arguments is the honest form.
	if (!qbsp->versionProbeArguments.isEmpty() || !qbsp->versionProbeSupported) {
		return fail("Expected the ericw qbsp probe to run with no arguments.");
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
		|| !lightpreview->capabilityFlags.contains(QStringLiteral("temp-dir-risk"))
		|| lightpreview->versionProbeSupported
		|| lightpreview->readinessWarnings.isEmpty()) {
		return fail("Expected lightpreview launch/temp readiness metadata without an unsafe probe.");
	}

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary workspace.");
	}
	QDir root(tempDir.path());
	if (!root.mkpath(QStringLiteral("external/compilers/ericw-tools/build/bin"))) {
		return fail("Expected fake compiler build directory.");
	}

	const QString qbspName = platformExecutableName(QStringLiteral("qbsp"));
	if (!touchFile(root.filePath(QStringLiteral("external/compilers/ericw-tools/build/bin/%1").arg(qbspName)))) {
		return fail("Expected fake qbsp executable.");
	}
	const QString bspinfoName = platformExecutableName(QStringLiteral("bspinfo"));
	const QString fakeBspinfoPath = root.filePath(QStringLiteral("external/compilers/ericw-tools/build/bin/%1").arg(bspinfoName));
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
		return fail("Expected fake ericw source and helper executable discovery.");
	}
	if (summary.overallState() != vibestudio::OperationState::Warning) {
		return fail("Expected partial compiler discovery to warn.");
	}
	if (!vibestudio::compilerRegistrySummaryText(summary).contains(QStringLiteral("ericw-tools qbsp"))) {
		return fail("Expected registry text to include qbsp.");
	}
	const vibestudio::CompilerToolDiscovery* bspinfoDiscovery = discoveryById(summary.tools, QStringLiteral("ericw-bspinfo"));
	if (!bspinfoDiscovery
		|| !bspinfoDiscovery->executableAvailable
		|| bspinfoDiscovery->executablePath != QDir::cleanPath(fakeBspinfoPath)
		|| bspinfoDiscovery->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::NotAttempted
		|| !bspinfoDiscovery->warnings.join('\n').contains(QStringLiteral("operation-level bspinfo diagnostics"))) {
		return fail("Expected fake bspinfo helper discovery with readiness warning.");
	}
	vibestudio::CompilerToolDescriptor q3map2;
	if (!vibestudio::compilerToolDescriptorForId(QStringLiteral("q3map2"), &q3map2) || !q3map2.capabilityFlags.contains(QStringLiteral("idtech3-bsp"))) {
		return fail("Expected q3map2 capability flags.");
	}

	const QString overridePath = root.filePath(QStringLiteral("custom-qbsp"));
	if (!touchFile(overridePath)) {
		return fail("Expected fake override executable.");
	}
	vibestudio::CompilerRegistryOptions options;
	options.workspaceRootPath = tempDir.path();
	options.probeVersions = false;
	options.executableOverrides.push_back({QStringLiteral("ericw-qbsp"), overridePath});
	const vibestudio::CompilerRegistrySummary overrideSummary = vibestudio::discoverCompilerTools(options);
	bool overrideApplied = false;
	for (const vibestudio::CompilerToolDiscovery& discovery : overrideSummary.tools) {
		if (discovery.descriptor.id == QStringLiteral("ericw-qbsp")) {
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
	probeOptions.executableOverrides.push_back({QStringLiteral("q3map2"), QCoreApplication::applicationFilePath()});
	probeOptions.executableOverrides.push_back({QStringLiteral("zokumbsp"), notAnExecutable});
	const vibestudio::CompilerRegistrySummary probeSummary = vibestudio::discoverCompilerTools(probeOptions);

	const vibestudio::CompilerToolDiscovery* zdbspProbe = discoveryById(probeSummary.tools, QStringLiteral("zdbsp"));
	if (!zdbspProbe
		|| zdbspProbe->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::Probed
		|| !zdbspProbe->versionAvailable
		|| !zdbspProbe->versionText.contains(QStringLiteral("ZDBSP 1.19"))) {
		return fail("Expected a recognizable version banner to be reported as probed.");
	}
	const vibestudio::CompilerToolDiscovery* q3map2Probe = discoveryById(probeSummary.tools, QStringLiteral("q3map2"));
	if (!q3map2Probe
		|| q3map2Probe->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::Inferred
		|| q3map2Probe->versionAvailable
		|| q3map2Probe->versionProbeExitCode != 0) {
		return fail("Expected output without a version banner to be reported as inferred, not successful.");
	}
	const vibestudio::CompilerToolDiscovery* zokumProbe = discoveryById(probeSummary.tools, QStringLiteral("zokumbsp"));
	if (!zokumProbe
		|| zokumProbe->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::Failed
		|| zokumProbe->versionAvailable) {
		return fail("Expected an unusable probe target to be reported as failed.");
	}
	const vibestudio::CompilerToolDiscovery* lightpreviewProbe = discoveryById(probeSummary.tools, QStringLiteral("ericw-lightpreview"));
	if (!lightpreviewProbe
		|| lightpreviewProbe->versionProbeAttempted
		|| lightpreviewProbe->versionProbeOutcome != vibestudio::CompilerVersionProbeOutcome::NotAttempted) {
		return fail("Expected the GUI lightpreview helper never to be launched by a probe.");
	}
	if (!vibestudio::compilerRegistrySummaryText(probeSummary).contains(vibestudio::compilerVersionProbeOutcomeText(vibestudio::CompilerVersionProbeOutcome::Probed))) {
		return fail("Expected the registry report to state the version probe outcome.");
	}

	return EXIT_SUCCESS;
}
