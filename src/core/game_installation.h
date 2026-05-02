#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class GameEngineFamily {
	Unknown,
	IdTech1,
	IdTech2,
	IdTech3,
};

struct GameDefinition {
	QString gameKey;
	QString displayName;
	GameEngineFamily engineFamily = GameEngineFamily::Unknown;
	QStringList expectedBasePackages;
	QString defaultPaletteId;
	QString defaultCompilerProfileId;
};

struct GameInstallationProfile {
	QString id;
	QString gameKey;
	GameEngineFamily engineFamily = GameEngineFamily::Unknown;
	QString displayName;
	QString rootPath;
	QString executablePath;
	QStringList basePackagePaths;
	QStringList modPackagePaths;
	QString paletteId;
	QString compilerProfileId;
	bool readOnly = true;
	bool active = true;
	bool hidden = false;
	bool manual = true;
	QDateTime createdUtc;
	QDateTime updatedUtc;
};

struct GameInstallationValidation {
	bool rootExists = false;
	bool rootIsDirectory = false;
	bool executableExists = false;
	bool executableIsFile = false;
	QStringList warnings;
	QStringList errors;

	[[nodiscard]] bool isUsable() const;
};

struct GameInstallationDetectionCandidate {
	QString sourceId;
	QString sourceName;
	GameInstallationProfile profile;
	int confidencePercent = 0;
	QStringList matchedPaths;
	QStringList warnings;
};

QString gameEngineFamilyId(GameEngineFamily family);
QString gameEngineFamilyDisplayName(GameEngineFamily family);
GameEngineFamily gameEngineFamilyFromId(const QString& id);
QStringList gameEngineFamilyIds();

QVector<GameDefinition> knownGameDefinitions();
QStringList knownGameKeys();
GameDefinition gameDefinitionForKey(const QString& gameKey);

QString normalizedGameKey(const QString& gameKey);
QString normalizedInstallationPath(const QString& path, const QString& rootPath = QString());
QString defaultGameInstallationDisplayName(const QString& rootPath, const QString& gameKey = QString(), const QString& preferredName = QString());
QString stableGameInstallationId(const GameInstallationProfile& profile);
GameInstallationProfile normalizedGameInstallationProfile(const GameInstallationProfile& profile);
GameInstallationValidation validateGameInstallationProfile(const GameInstallationProfile& profile);
bool sameGameInstallationId(const QString& left, const QString& right);
QStringList defaultSteamLibraryRoots();
QStringList defaultGogLibraryRoots();
QVector<GameInstallationDetectionCandidate> detectSteamGameInstallations(const QStringList& additionalRoots = {});
QVector<GameInstallationDetectionCandidate> detectGogGameInstallations(const QStringList& additionalRoots = {});
QVector<GameInstallationDetectionCandidate> detectGameInstallations(const QStringList& additionalSteamRoots = {}, const QStringList& additionalGogRoots = {});

} // namespace vibestudio
