#include "core/compiler_registry.h"

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

int main()
{
	const QVector<vibestudio::CompilerToolDescriptor> descriptors = vibestudio::compilerToolDescriptors();
	if (descriptors.size() < 9) {
		return fail("Expected compiler descriptors for imported tools.");
	}

	const vibestudio::CompilerToolDescriptor* bspinfo = descriptorById(descriptors, QStringLiteral("ericw-bspinfo"));
	const vibestudio::CompilerToolDescriptor* bsputil = descriptorById(descriptors, QStringLiteral("ericw-bsputil"));
	const vibestudio::CompilerToolDescriptor* lightpreview = descriptorById(descriptors, QStringLiteral("ericw-lightpreview"));
	if (!bspinfo || !bsputil || !lightpreview) {
		return fail("Expected first-class ericw helper descriptors.");
	}
	if (!bspinfo->capabilityFlags.contains(QStringLiteral("upstream-issue-225"))
		|| !bspinfo->capabilityFlags.contains(QStringLiteral("upstream-issue-289"))
		|| bspinfo->versionProbeArguments.isEmpty()) {
		return fail("Expected bspinfo issue capabilities and help probe.");
	}
	if (!bsputil->capabilityFlags.contains(QStringLiteral("argument-parser-risk"))
		|| !bsputil->capabilityFlags.contains(QStringLiteral("upstream-issue-435"))
		|| bsputil->readinessWarnings.isEmpty()) {
		return fail("Expected bsputil argument parsing readiness warning.");
	}
	if (!lightpreview->capabilityFlags.contains(QStringLiteral("platform-launch-risk"))
		|| !lightpreview->capabilityFlags.contains(QStringLiteral("temp-dir-risk"))
		|| !lightpreview->versionProbeArguments.isEmpty()
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

	return EXIT_SUCCESS;
}
