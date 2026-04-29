#include "core/studio_manifest.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);

	const QVector<vibestudio::StudioModule> modules = vibestudio::plannedModules();
	if (modules.size() < 6) {
		std::cerr << "Expected at least six planned studio modules.\n";
		return EXIT_FAILURE;
	}

	const QVector<vibestudio::CompilerIntegration> compilers = vibestudio::compilerIntegrations();
	if (compilers.size() < 4) {
		std::cerr << "Expected imported compiler integrations.\n";
		return EXIT_FAILURE;
	}

	bool hasQ3Map2 = false;
	bool hasIdTech1 = false;
	for (const vibestudio::CompilerIntegration& compiler : compilers) {
		hasQ3Map2 = hasQ3Map2 || compiler.id == "q3map2-nrc";
		hasIdTech1 = hasIdTech1 || compiler.engines.contains("idTech1");
		if (compiler.upstreamUrl.isEmpty() || compiler.pinnedRevision.size() < 12) {
			std::cerr << "Compiler manifest entry is missing provenance.\n";
			return EXIT_FAILURE;
		}
	}

	if (!hasQ3Map2 || !hasIdTech1) {
		std::cerr << "Compiler manifest is missing q3map2 or idTech1 coverage.\n";
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
