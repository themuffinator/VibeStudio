#include "core/game_installation.h"

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

bool writeFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return file.write("fixture") > 0;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}

	QDir root(tempDir.path());
	if (!root.mkpath(QStringLiteral("id1")) || !writeFile(root.filePath(QStringLiteral("id1/pak0.pak")))) {
		return fail("Expected Quake package fixture.");
	}

	vibestudio::GameInstallationProfile profile;
	profile.rootPath = tempDir.path();
	profile.gameKey = QStringLiteral("quake");
	profile.displayName = QStringLiteral("Manual Quake");
	profile.basePackagePaths = {QStringLiteral("id1/pak0.pak")};
	profile = vibestudio::normalizedGameInstallationProfile(profile);

	if (profile.id.isEmpty() || profile.gameKey != QStringLiteral("quake")) {
		return fail("Expected stable Quake installation id and key.");
	}
	if (profile.engineFamily != vibestudio::GameEngineFamily::IdTech2) {
		return fail("Expected Quake to normalize to idTech2.");
	}
	if (profile.basePackagePaths.size() != 1 || !QFile::exists(profile.basePackagePaths.front())) {
		return fail("Expected relative base package path to normalize under root.");
	}
	if (profile.paletteId != QStringLiteral("quake") || profile.compilerProfileId != QStringLiteral("ericw-tools")) {
		return fail("Expected Quake defaults.");
	}

	const vibestudio::GameInstallationValidation validation = vibestudio::validateGameInstallationProfile(profile);
	if (!validation.isUsable() || !validation.errors.isEmpty()) {
		return fail("Expected valid manual profile.");
	}

	vibestudio::GameInstallationProfile missingRoot;
	missingRoot.rootPath = root.filePath(QStringLiteral("missing"));
	const vibestudio::GameInstallationValidation missingValidation = vibestudio::validateGameInstallationProfile(missingRoot);
	if (missingValidation.isUsable() || missingValidation.errors.isEmpty()) {
		return fail("Expected missing root to block validation.");
	}

	if (vibestudio::gameEngineFamilyFromId(QStringLiteral("QUAKE3")) != vibestudio::GameEngineFamily::IdTech3) {
		return fail("Expected engine family aliases to normalize.");
	}
	if (!vibestudio::knownGameKeys().contains(QStringLiteral("doom")) || !vibestudio::knownGameKeys().contains(QStringLiteral("quake3"))) {
		return fail("Expected known idTech game keys.");
	}

	QTemporaryDir steamDir;
	if (!steamDir.isValid()) {
		return fail("Expected temporary Steam directory.");
	}
	QDir steamRoot(steamDir.path());
	if (!steamRoot.mkpath(QStringLiteral("steamapps/common/Quake/id1")) || !writeFile(steamRoot.filePath(QStringLiteral("steamapps/common/Quake/id1/pak0.pak")))) {
		return fail("Expected Steam Quake fixture.");
	}
	const QVector<vibestudio::GameInstallationDetectionCandidate> steamCandidates = vibestudio::detectSteamGameInstallations({steamDir.path()});
	if (steamCandidates.isEmpty()) {
		return fail("Expected Steam detection candidate.");
	}
	bool foundSteamQuake = false;
	for (const vibestudio::GameInstallationDetectionCandidate& candidate : steamCandidates) {
		if (candidate.sourceId == QStringLiteral("steam") && candidate.profile.gameKey == QStringLiteral("quake") && candidate.profile.rootPath.contains(QStringLiteral("Quake"))) {
			foundSteamQuake = true;
			if (candidate.profile.manual || candidate.confidencePercent < 80) {
				return fail("Expected detected Steam profile metadata.");
			}
			break;
		}
	}
	if (!foundSteamQuake) {
		return fail("Expected Steam Quake detection.");
	}

	QTemporaryDir gogDir;
	if (!gogDir.isValid()) {
		return fail("Expected temporary GOG directory.");
	}
	QDir gogRoot(gogDir.path());
	if (!gogRoot.mkpath(QStringLiteral("Quake III Arena/baseq3")) || !writeFile(gogRoot.filePath(QStringLiteral("Quake III Arena/baseq3/pak0.pk3")))) {
		return fail("Expected GOG Quake III fixture.");
	}
	const QVector<vibestudio::GameInstallationDetectionCandidate> gogCandidates = vibestudio::detectGogGameInstallations({gogDir.path()});
	if (gogCandidates.isEmpty()) {
		return fail("Expected GOG detection candidate.");
	}
	bool foundGogQuake3 = false;
	for (const vibestudio::GameInstallationDetectionCandidate& candidate : gogCandidates) {
		if (candidate.sourceId == QStringLiteral("gog") && candidate.profile.gameKey == QStringLiteral("quake3") && candidate.profile.rootPath.contains(QStringLiteral("Quake III Arena"))) {
			foundGogQuake3 = true;
			break;
		}
	}
	if (!foundGogQuake3) {
		return fail("Expected GOG Quake III detection.");
	}

	return EXIT_SUCCESS;
}
