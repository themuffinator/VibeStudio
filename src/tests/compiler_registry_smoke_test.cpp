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

} // namespace

int main()
{
	const QVector<vibestudio::CompilerToolDescriptor> descriptors = vibestudio::compilerToolDescriptors();
	if (descriptors.size() < 6) {
		return fail("Expected compiler descriptors for imported tools.");
	}

	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary workspace.");
	}
	QDir root(tempDir.path());
	if (!root.mkpath(QStringLiteral("external/compilers/ericw-tools/build/bin"))) {
		return fail("Expected fake compiler build directory.");
	}

#if defined(Q_OS_WIN)
	const QString qbspName = QStringLiteral("qbsp.exe");
#else
	const QString qbspName = QStringLiteral("qbsp");
#endif
	if (!touchFile(root.filePath(QStringLiteral("external/compilers/ericw-tools/build/bin/%1").arg(qbspName)))) {
		return fail("Expected fake qbsp executable.");
	}

	const vibestudio::CompilerRegistrySummary summary = vibestudio::discoverCompilerTools(tempDir.path());
	if (summary.tools.size() != descriptors.size()) {
		return fail("Expected one discovery result per descriptor.");
	}
	if (summary.sourceAvailableCount < 1 || summary.executableAvailableCount < 1) {
		return fail("Expected fake ericw source and executable discovery.");
	}
	if (summary.overallState() != vibestudio::OperationState::Warning) {
		return fail("Expected partial compiler discovery to warn.");
	}
	if (!vibestudio::compilerRegistrySummaryText(summary).contains(QStringLiteral("ericw-tools qbsp"))) {
		return fail("Expected registry text to include qbsp.");
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
