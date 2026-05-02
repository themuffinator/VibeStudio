#include "core/project_manifest.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary project directory.");
	}

	QDir root(tempDir.path());
	if (!root.mkpath(QStringLiteral("src")) || !root.mkpath(QStringLiteral("packages")) || !root.mkpath(QStringLiteral("build"))) {
		return fail("Expected project fixture directories.");
	}

	vibestudio::ProjectManifest manifest = vibestudio::defaultProjectManifest(tempDir.path(), QStringLiteral("Manifest Test"));
	manifest.sourceFolders = {QStringLiteral("src")};
	manifest.packageFolders = {QStringLiteral("packages")};
	manifest.selectedInstallationId = QStringLiteral("quake-test");
	manifest.compilerSearchPaths = {QStringLiteral("tools")};
	manifest.compilerToolOverrides = {{QStringLiteral("ericw-qbsp"), QStringLiteral("tools/qbsp")}};
	manifest.settingsOverrides.selectedInstallationId = QStringLiteral("quake-test");
	manifest.settingsOverrides.editorProfileId = QStringLiteral("trenchbroom");
	manifest.settingsOverrides.paletteId = QStringLiteral("quake");
	manifest.settingsOverrides.compilerProfileId = QStringLiteral("ericw-qbsp");
	manifest.settingsOverrides.aiFreeModeSet = true;
	manifest.settingsOverrides.aiFreeMode = true;

	QString error;
	if (!vibestudio::saveProjectManifest(manifest, &error)) {
		std::cerr << error.toStdString() << "\n";
		return fail("Expected project manifest save.");
	}
	if (!QFileInfo::exists(vibestudio::projectManifestPath(tempDir.path()))) {
		return fail("Expected project manifest file.");
	}

	vibestudio::ProjectManifest loaded;
	if (!vibestudio::loadProjectManifest(tempDir.path(), &loaded, &error)) {
		std::cerr << error.toStdString() << "\n";
		return fail("Expected project manifest load.");
	}
	if (loaded.displayName != QStringLiteral("Manifest Test") || loaded.projectId.isEmpty()) {
		return fail("Expected loaded manifest metadata.");
	}
	if (loaded.sourceFolders != QStringList {QStringLiteral("src")} || loaded.packageFolders != QStringList {QStringLiteral("packages")}) {
		return fail("Expected loaded manifest folders.");
	}
	if (loaded.settingsOverrides.editorProfileId != QStringLiteral("trenchbroom") || !loaded.settingsOverrides.aiFreeModeSet || !loaded.settingsOverrides.aiFreeMode) {
		return fail("Expected loaded project-local settings overrides.");
	}
	if (loaded.compilerSearchPaths != QStringList {QStringLiteral("tools")} || loaded.compilerToolOverrides.size() != 1) {
		return fail("Expected loaded project compiler overrides.");
	}
	if (vibestudio::effectiveProjectInstallationId(loaded, QStringLiteral("global")) != QStringLiteral("quake-test")) {
		return fail("Expected project installation override to win over global fallback.");
	}
	if (vibestudio::effectiveProjectEditorProfileId(loaded, QStringLiteral("radiant")) != QStringLiteral("trenchbroom")) {
		return fail("Expected project editor profile override.");
	}
	if (vibestudio::effectiveProjectPaletteId(loaded, QStringLiteral("doom")) != QStringLiteral("quake")) {
		return fail("Expected project palette override.");
	}
	if (vibestudio::effectiveProjectCompilerProfileId(loaded, QStringLiteral("zdbsp")) != QStringLiteral("ericw-qbsp")) {
		return fail("Expected project compiler profile override.");
	}
	if (!vibestudio::effectiveProjectAiFreeMode(loaded, false)) {
		return fail("Expected project AI-free override to win over global fallback.");
	}
	if (vibestudio::effectiveProjectCompilerSearchPaths(loaded).isEmpty()) {
		return fail("Expected effective project compiler search paths.");
	}
	QVector<vibestudio::CompilerToolPathOverride> fallbackOverrides = {{QStringLiteral("ericw-vis"), QStringLiteral("global-vis")}};
	if (vibestudio::effectiveProjectCompilerToolOverrides(loaded, fallbackOverrides).size() != 2) {
		return fail("Expected project compiler executable overrides to merge with global overrides.");
	}
	if (vibestudio::registerProjectOutputPath(&loaded, root.filePath(QStringLiteral("build/start.bsp"))) != true || vibestudio::registerProjectOutputPath(&loaded, root.filePath(QStringLiteral("build/start.bsp"))) != false) {
		return fail("Expected project output registration to de-duplicate paths.");
	}

	const vibestudio::ProjectHealthSummary health = vibestudio::buildProjectHealthSummary(loaded);
	if (health.overallState() != vibestudio::OperationState::Warning || health.failedCount != 0 || health.readyCount == 0) {
		return fail("Expected mostly healthy project with output/temp warnings.");
	}
	if (!vibestudio::projectManifestToText(loaded).contains(QStringLiteral("Manifest Test"))) {
		return fail("Expected manifest text report.");
	}

	vibestudio::ProjectManifest missing = vibestudio::defaultProjectManifest(root.filePath(QStringLiteral("missing")));
	if (vibestudio::saveProjectManifest(missing, &error)) {
		return fail("Expected missing project root save to fail.");
	}

	return EXIT_SUCCESS;
}
