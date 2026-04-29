#include "cli/cli.h"

#include "core/studio_manifest.h"
#include "vibestudio_config.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QSysInfo>

#include <cstddef>
#include <iostream>
#include <string>

namespace vibestudio::cli {

namespace {

std::string text(const QString& value)
{
	const QByteArray bytes = value.toUtf8();
	return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

void printHelp()
{
	std::cout << "VibeStudio " << text(versionString()) << "\n";
	std::cout << "Usage: vibestudio --cli [options]\n\n";
	std::cout << "Options:\n";
	std::cout << "  --version           Print the application version.\n";
	std::cout << "  --help              Print this help text.\n";
	std::cout << "  --studio-report     Print planned studio modules.\n";
	std::cout << "  --compiler-report   Print imported compiler integrations.\n";
	std::cout << "  --platform-report   Print platform and Qt runtime details.\n";
}

void printStudioReport()
{
	std::cout << "VibeStudio planned modules\n";
	for (const StudioModule& module : plannedModules()) {
		std::cout << "- " << text(module.name) << " [" << text(module.maturity) << "]\n";
		std::cout << "  Category: " << text(module.category) << "\n";
		std::cout << "  Engines: " << text(module.engines.join(", ")) << "\n";
		std::cout << "  " << text(module.description) << "\n";
	}
}

void printCompilerReport()
{
	std::cout << "VibeStudio compiler integrations\n";
	for (const CompilerIntegration& compiler : compilerIntegrations()) {
		std::cout << "- " << text(compiler.displayName) << "\n";
		std::cout << "  Engines: " << text(compiler.engines) << "\n";
		std::cout << "  Role: " << text(compiler.role) << "\n";
		std::cout << "  Source: " << text(compiler.sourcePath) << "\n";
		std::cout << "  Upstream: " << text(compiler.upstreamUrl) << "\n";
		std::cout << "  Revision: " << text(compiler.pinnedRevision) << "\n";
		std::cout << "  License: " << text(compiler.license) << "\n";
	}
}

void printPlatformReport()
{
	std::cout << "VibeStudio platform report\n";
	std::cout << "Version: " << text(versionString()) << "\n";
	std::cout << "GitHub repo: " << VIBESTUDIO_GITHUB_REPO << "\n";
	std::cout << "Update channel: " << VIBESTUDIO_UPDATE_CHANNEL << "\n";
	std::cout << "Qt runtime: " << qVersion() << "\n";
	std::cout << "Kernel: " << text(QSysInfo::kernelType()) << " " << text(QSysInfo::kernelVersion()) << "\n";
	std::cout << "CPU architecture: " << text(QSysInfo::currentCpuArchitecture()) << "\n";
	std::cout << "Product: " << text(QSysInfo::prettyProductName()) << "\n";
}

} // namespace

int run(const QStringList& args)
{
	if (args.contains("--version")) {
		std::cout << "VibeStudio " << text(versionString()) << "\n";
		return 0;
	}
	if (args.contains("--help") || args.size() <= 1) {
		printHelp();
		return 0;
	}
	if (args.contains("--studio-report")) {
		printStudioReport();
		return 0;
	}
	if (args.contains("--compiler-report")) {
		printCompilerReport();
		return 0;
	}
	if (args.contains("--platform-report")) {
		printPlatformReport();
		return 0;
	}

	std::cerr << "Unknown VibeStudio CLI option. Run --cli --help.\n";
	return 2;
}

} // namespace vibestudio::cli
