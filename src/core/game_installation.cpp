#include "core/game_installation.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace vibestudio {

namespace {

QString installText(const char* source)
{
	return QCoreApplication::translate("VibeStudioGameInstallation", source);
}

QString normalizedId(const QString& id)
{
	QString normalized = id.trimmed().toLower();
	normalized.replace('_', '-');
	normalized.replace(' ', '-');
	while (normalized.contains(QStringLiteral("--"))) {
		normalized.replace(QStringLiteral("--"), QStringLiteral("-"));
	}
	return normalized.trimmed();
}

QString normalizedPathForHash(const QString& path)
{
#if defined(Q_OS_WIN)
	return path.toLower();
#else
	return path;
#endif
}

QString shortPathHash(const QString& path)
{
	const QByteArray bytes = normalizedPathForHash(path).toUtf8();
	return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(10));
}

QStringList normalizedPathList(const QStringList& paths, const QString& rootPath)
{
	QStringList result;
	for (const QString& path : paths) {
		const QString normalized = normalizedInstallationPath(path, rootPath);
		if (!normalized.isEmpty() && !result.contains(normalized)) {
			result.push_back(normalized);
		}
	}
	return result;
}

bool pathExistsUnderRoot(const QString& rootPath, const QString& relativePath)
{
	return QFileInfo::exists(QDir(rootPath).filePath(relativePath));
}

struct GameDetectionHint {
	QString gameKey;
	QStringList folderNames;
	QStringList executableNames;
};

QVector<GameDetectionHint> gameDetectionHints()
{
	return {
		{
			QStringLiteral("doom"),
			{
				QStringLiteral("Doom"),
				QStringLiteral("Doom 2"),
				QStringLiteral("Ultimate Doom"),
				QStringLiteral("Final Doom"),
				QStringLiteral("DOOM"),
			},
			{
				QStringLiteral("doom.exe"),
				QStringLiteral("doom2.exe"),
				QStringLiteral("chocolate-doom.exe"),
				QStringLiteral("gzdoom.exe"),
				QStringLiteral("doom"),
				QStringLiteral("chocolate-doom"),
				QStringLiteral("gzdoom"),
			},
		},
		{
			QStringLiteral("heretic-hexen"),
			{
				QStringLiteral("Heretic"),
				QStringLiteral("Hexen"),
				QStringLiteral("Heretic Shadow of the Serpent Riders"),
				QStringLiteral("Hexen Beyond Heretic"),
			},
			{
				QStringLiteral("heretic.exe"),
				QStringLiteral("hexen.exe"),
				QStringLiteral("chocolate-heretic.exe"),
				QStringLiteral("chocolate-hexen.exe"),
				QStringLiteral("heretic"),
				QStringLiteral("hexen"),
			},
		},
		{
			QStringLiteral("quake"),
			{
				QStringLiteral("Quake"),
				QStringLiteral("Quake Enhanced"),
				QStringLiteral("Quake Mission Pack 1"),
				QStringLiteral("Quake Mission Pack 2"),
			},
			{
				QStringLiteral("quake.exe"),
				QStringLiteral("winquake.exe"),
				QStringLiteral("glquake.exe"),
				QStringLiteral("ironwail.exe"),
				QStringLiteral("quakespasm.exe"),
				QStringLiteral("quake"),
				QStringLiteral("ironwail"),
				QStringLiteral("quakespasm"),
			},
		},
		{
			QStringLiteral("quake2"),
			{
				QStringLiteral("Quake 2"),
				QStringLiteral("Quake II"),
				QStringLiteral("Quake II RTX"),
				QStringLiteral("quake2"),
			},
			{
				QStringLiteral("quake2.exe"),
				QStringLiteral("q2pro.exe"),
				QStringLiteral("yquake2.exe"),
				QStringLiteral("quake2"),
				QStringLiteral("q2pro"),
				QStringLiteral("yquake2"),
			},
		},
		{
			QStringLiteral("quake3"),
			{
				QStringLiteral("Quake 3 Arena"),
				QStringLiteral("Quake III Arena"),
				QStringLiteral("Quake III"),
				QStringLiteral("quake3"),
			},
			{
				QStringLiteral("quake3.exe"),
				QStringLiteral("quake3e.exe"),
				QStringLiteral("ioquake3.exe"),
				QStringLiteral("quake3"),
				QStringLiteral("quake3e"),
				QStringLiteral("ioquake3"),
			},
		},
	};
}

QStringList uniqueNormalizedPaths(const QStringList& paths)
{
	QStringList result;
	for (const QString& path : paths) {
		const QString normalized = normalizedInstallationPath(path);
		if (!normalized.isEmpty() && !result.contains(normalized)) {
			result.push_back(normalized);
		}
	}
	return result;
}

QStringList existingChildPaths(const QString& rootPath, const QStringList& relativePaths)
{
	QStringList paths;
	const QDir root(rootPath);
	for (const QString& relativePath : relativePaths) {
		const QString absolutePath = QDir::cleanPath(root.filePath(relativePath));
		if (QFileInfo::exists(absolutePath)) {
			paths.push_back(absolutePath);
		}
	}
	return paths;
}

QString findExecutablePath(const QString& rootPath, const QStringList& executableNames)
{
	const QDir root(rootPath);
	for (const QString& executableName : executableNames) {
		const QString directPath = QDir::cleanPath(root.filePath(executableName));
		const QFileInfo directInfo(directPath);
		if (directInfo.exists() && directInfo.isFile()) {
			return directInfo.absoluteFilePath();
		}
	}

	QDirIterator iterator(rootPath, executableNames, QDir::Files, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		const QFileInfo info(iterator.next());
		if (info.isFile()) {
			return info.absoluteFilePath();
		}
	}
	return {};
}

bool folderNameMatchesHint(const QString& folderName, const GameDetectionHint& hint)
{
	const QString normalizedFolder = normalizedId(folderName);
	if (normalizedFolder.isEmpty()) {
		return false;
	}
	for (const QString& name : hint.folderNames) {
		const QString normalizedName = normalizedId(name);
		if (normalizedFolder == normalizedName || normalizedFolder.contains(normalizedName) || normalizedName.contains(normalizedFolder)) {
			return true;
		}
	}
	return normalizedFolder == normalizedId(hint.gameKey);
}

QVector<GameInstallationDetectionCandidate> candidatesForDirectory(const QString& rootPath, const QString& sourceId, const QString& sourceName)
{
	QVector<GameInstallationDetectionCandidate> candidates;
	const QFileInfo rootInfo(rootPath);
	if (!rootInfo.exists() || !rootInfo.isDir()) {
		return candidates;
	}

	for (const GameDetectionHint& hint : gameDetectionHints()) {
		const GameDefinition definition = gameDefinitionForKey(hint.gameKey);
		const bool folderMatch = folderNameMatchesHint(rootInfo.fileName(), hint);
		const QStringList packages = existingChildPaths(rootInfo.absoluteFilePath(), definition.expectedBasePackages);
		if (!folderMatch && packages.isEmpty()) {
			continue;
		}

		GameInstallationProfile profile;
		profile.rootPath = rootInfo.absoluteFilePath();
		profile.gameKey = definition.gameKey;
		profile.engineFamily = definition.engineFamily;
		profile.displayName = QStringLiteral("%1 (%2)").arg(defaultGameInstallationDisplayName(profile.rootPath, profile.gameKey), sourceName);
		profile.executablePath = findExecutablePath(profile.rootPath, hint.executableNames);
		profile.basePackagePaths = packages;
		profile.readOnly = true;
		profile.manual = false;
		profile = normalizedGameInstallationProfile(profile);

		GameInstallationDetectionCandidate candidate;
		candidate.sourceId = sourceId;
		candidate.sourceName = sourceName;
		candidate.profile = profile;
		candidate.confidencePercent = packages.isEmpty() ? 62 : (folderMatch ? 96 : 84);
		candidate.matchedPaths = packages;
		if (packages.isEmpty()) {
			candidate.warnings << installText("Folder name matched, but no expected base package was found.");
		}
		if (profile.executablePath.isEmpty()) {
			candidate.warnings << installText("Executable was not found in the detected folder.");
		}
		candidates.push_back(candidate);
	}
	return candidates;
}

QVector<GameInstallationDetectionCandidate> deduplicateCandidates(QVector<GameInstallationDetectionCandidate> candidates)
{
	QVector<GameInstallationDetectionCandidate> deduplicated;
	for (const GameInstallationDetectionCandidate& candidate : candidates) {
		auto existing = std::find_if(deduplicated.begin(), deduplicated.end(), [&candidate](const GameInstallationDetectionCandidate& item) {
			return sameGameInstallationId(item.profile.id, candidate.profile.id);
		});
		if (existing == deduplicated.end()) {
			deduplicated.push_back(candidate);
		} else if (candidate.confidencePercent > existing->confidencePercent) {
			*existing = candidate;
		}
	}
	std::sort(deduplicated.begin(), deduplicated.end(), [](const GameInstallationDetectionCandidate& left, const GameInstallationDetectionCandidate& right) {
		if (left.confidencePercent == right.confidencePercent) {
			return left.profile.displayName < right.profile.displayName;
		}
		return left.confidencePercent > right.confidencePercent;
	});
	return deduplicated;
}

QStringList oneLevelCandidateDirectories(const QStringList& roots)
{
	QStringList directories;
	for (const QString& rootPath : uniqueNormalizedPaths(roots)) {
		const QFileInfo rootInfo(rootPath);
		if (!rootInfo.exists() || !rootInfo.isDir()) {
			continue;
		}
		if (!directories.contains(rootInfo.absoluteFilePath())) {
			directories.push_back(rootInfo.absoluteFilePath());
		}
		const QFileInfoList children = QDir(rootInfo.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
		for (const QFileInfo& child : children) {
			const QString childPath = child.absoluteFilePath();
			if (!directories.contains(childPath)) {
				directories.push_back(childPath);
			}
		}
	}
	return directories;
}

QStringList steamCommonRootsFromRoot(const QString& rootPath)
{
	QStringList roots;
	const QFileInfo info(rootPath);
	if (!info.exists() || !info.isDir()) {
		return roots;
	}

	const QDir root(info.absoluteFilePath());
	if (info.fileName().compare(QStringLiteral("common"), Qt::CaseInsensitive) == 0) {
		roots.push_back(info.absoluteFilePath());
	}
	if (info.fileName().compare(QStringLiteral("steamapps"), Qt::CaseInsensitive) == 0) {
		roots.push_back(root.filePath(QStringLiteral("common")));
	}
	if (QFileInfo::exists(root.filePath(QStringLiteral("steamapps")))) {
		roots.push_back(root.filePath(QStringLiteral("steamapps/common")));
	}

	const QString libraryFoldersPath = info.fileName().compare(QStringLiteral("steamapps"), Qt::CaseInsensitive) == 0
		? root.filePath(QStringLiteral("libraryfolders.vdf"))
		: root.filePath(QStringLiteral("steamapps/libraryfolders.vdf"));
	QFile file(libraryFoldersPath);
	if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		const QString content = QString::fromUtf8(file.readAll());
		const QRegularExpression pathPattern(QStringLiteral("\"path\"\\s+\"([^\"]+)\""));
		QRegularExpressionMatchIterator matches = pathPattern.globalMatch(content);
		while (matches.hasNext()) {
			const QRegularExpressionMatch match = matches.next();
			const QString libraryPath = match.captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("/"));
			if (!libraryPath.trimmed().isEmpty()) {
				roots.push_back(QDir(libraryPath).filePath(QStringLiteral("steamapps/common")));
			}
		}
	}

	return uniqueNormalizedPaths(roots);
}

} // namespace

bool GameInstallationValidation::isUsable() const
{
	return errors.isEmpty() && rootExists && rootIsDirectory;
}

QString gameEngineFamilyId(GameEngineFamily family)
{
	switch (family) {
	case GameEngineFamily::IdTech1:
		return QStringLiteral("idtech1");
	case GameEngineFamily::IdTech2:
		return QStringLiteral("idtech2");
	case GameEngineFamily::IdTech3:
		return QStringLiteral("idtech3");
	case GameEngineFamily::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString gameEngineFamilyDisplayName(GameEngineFamily family)
{
	switch (family) {
	case GameEngineFamily::IdTech1:
		return installText("idTech1 / Doom-family");
	case GameEngineFamily::IdTech2:
		return installText("idTech2 / Quake-family");
	case GameEngineFamily::IdTech3:
		return installText("idTech3 / Quake III-family");
	case GameEngineFamily::Unknown:
		break;
	}
	return installText("Unknown");
}

GameEngineFamily gameEngineFamilyFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("idtech1") || normalized == QStringLiteral("doom")) {
		return GameEngineFamily::IdTech1;
	}
	if (normalized == QStringLiteral("idtech2") || normalized == QStringLiteral("quake")) {
		return GameEngineFamily::IdTech2;
	}
	if (normalized == QStringLiteral("idtech3") || normalized == QStringLiteral("quake3")) {
		return GameEngineFamily::IdTech3;
	}
	return GameEngineFamily::Unknown;
}

QStringList gameEngineFamilyIds()
{
	return {
		gameEngineFamilyId(GameEngineFamily::Unknown),
		gameEngineFamilyId(GameEngineFamily::IdTech1),
		gameEngineFamilyId(GameEngineFamily::IdTech2),
		gameEngineFamilyId(GameEngineFamily::IdTech3),
	};
}

QVector<GameDefinition> knownGameDefinitions()
{
	// The profile-driven shape mirrors PakFu's durable game/install model while
	// keeping VibeStudio-owned defaults and Qt-native validation.
	return {
		{
			QStringLiteral("custom"),
			installText("Custom / Unknown"),
			GameEngineFamily::Unknown,
			{},
			{},
			{},
		},
		{
			QStringLiteral("doom"),
			installText("Doom-family"),
			GameEngineFamily::IdTech1,
			{
				QStringLiteral("doom.wad"),
				QStringLiteral("doom2.wad"),
				QStringLiteral("plutonia.wad"),
				QStringLiteral("tnt.wad"),
			},
			QStringLiteral("doom"),
			QStringLiteral("zdbsp"),
		},
		{
			QStringLiteral("heretic-hexen"),
			installText("Heretic / Hexen-family"),
			GameEngineFamily::IdTech1,
			{
				QStringLiteral("heretic.wad"),
				QStringLiteral("hexen.wad"),
			},
			QStringLiteral("doom"),
			QStringLiteral("zdbsp"),
		},
		{
			QStringLiteral("quake"),
			installText("Quake"),
			GameEngineFamily::IdTech2,
			{
				QStringLiteral("id1/pak0.pak"),
			},
			QStringLiteral("quake"),
			QStringLiteral("ericw-tools"),
		},
		{
			QStringLiteral("quake2"),
			installText("Quake II"),
			GameEngineFamily::IdTech2,
			{
				QStringLiteral("baseq2/pak0.pak"),
			},
			QStringLiteral("quake2"),
			QStringLiteral("ericw-tools"),
		},
		{
			QStringLiteral("quake3"),
			installText("Quake III Arena"),
			GameEngineFamily::IdTech3,
			{
				QStringLiteral("baseq3/pak0.pk3"),
			},
			QStringLiteral("quake3"),
			QStringLiteral("q3map2"),
		},
	};
}

QStringList knownGameKeys()
{
	QStringList keys;
	for (const GameDefinition& definition : knownGameDefinitions()) {
		keys.push_back(definition.gameKey);
	}
	return keys;
}

GameDefinition gameDefinitionForKey(const QString& gameKey)
{
	const QString normalized = normalizedGameKey(gameKey);
	for (const GameDefinition& definition : knownGameDefinitions()) {
		if (definition.gameKey == normalized) {
			return definition;
		}
	}
	return knownGameDefinitions().front();
}

QString normalizedGameKey(const QString& gameKey)
{
	const QString normalized = normalizedId(gameKey);
	if (normalized.isEmpty()) {
		return QStringLiteral("custom");
	}
	for (const GameDefinition& definition : knownGameDefinitions()) {
		if (definition.gameKey == normalized) {
			return normalized;
		}
	}
	return normalized;
}

QString normalizedInstallationPath(const QString& path, const QString& rootPath)
{
	const QString trimmed = path.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}

	const QFileInfo info(trimmed);
	const QString absolutePath = info.isAbsolute()
		? info.absoluteFilePath()
		: (rootPath.trimmed().isEmpty() ? QDir::current().absoluteFilePath(trimmed) : QDir(rootPath).absoluteFilePath(trimmed));
	return QDir::cleanPath(absolutePath);
}

QString defaultGameInstallationDisplayName(const QString& rootPath, const QString& gameKey, const QString& preferredName)
{
	const QString trimmedName = preferredName.trimmed();
	if (!trimmedName.isEmpty()) {
		return trimmedName;
	}

	const GameDefinition definition = gameDefinitionForKey(gameKey);
	const QString folderName = QFileInfo(rootPath).fileName();
	if (!folderName.isEmpty() && definition.gameKey != QStringLiteral("custom")) {
		return QStringLiteral("%1 - %2").arg(definition.displayName, folderName);
	}
	if (!folderName.isEmpty()) {
		return folderName;
	}
	return rootPath.trimmed().isEmpty() ? definition.displayName : QDir::toNativeSeparators(rootPath);
}

QString stableGameInstallationId(const GameInstallationProfile& profile)
{
	const QString requested = normalizedId(profile.id);
	if (!requested.isEmpty()) {
		return requested;
	}

	const QString rootPath = normalizedInstallationPath(profile.rootPath);
	const QString gameKey = normalizedGameKey(profile.gameKey);
	if (rootPath.isEmpty()) {
		return QStringLiteral("%1-manual").arg(gameKey);
	}
	return QStringLiteral("%1-%2").arg(gameKey, shortPathHash(rootPath));
}

GameInstallationProfile normalizedGameInstallationProfile(const GameInstallationProfile& profile)
{
	GameInstallationProfile normalized = profile;
	const GameDefinition definition = gameDefinitionForKey(profile.gameKey);

	normalized.rootPath = normalizedInstallationPath(profile.rootPath);
	normalized.gameKey = normalizedGameKey(profile.gameKey);
	if (profile.engineFamily == GameEngineFamily::Unknown && definition.engineFamily != GameEngineFamily::Unknown) {
		normalized.engineFamily = definition.engineFamily;
	}
	if (normalized.paletteId.trimmed().isEmpty()) {
		normalized.paletteId = definition.defaultPaletteId;
	}
	if (normalized.compilerProfileId.trimmed().isEmpty()) {
		normalized.compilerProfileId = definition.defaultCompilerProfileId;
	}
	normalized.displayName = defaultGameInstallationDisplayName(normalized.rootPath, normalized.gameKey, profile.displayName);
	normalized.executablePath = normalizedInstallationPath(profile.executablePath, normalized.rootPath);
	normalized.basePackagePaths = normalizedPathList(profile.basePackagePaths, normalized.rootPath);
	normalized.modPackagePaths = normalizedPathList(profile.modPackagePaths, normalized.rootPath);
	normalized.id = stableGameInstallationId(normalized);
	normalized.createdUtc = profile.createdUtc.isValid() ? profile.createdUtc.toUTC() : QDateTime();
	normalized.updatedUtc = profile.updatedUtc.isValid() ? profile.updatedUtc.toUTC() : QDateTime();
	return normalized;
}

GameInstallationValidation validateGameInstallationProfile(const GameInstallationProfile& profile)
{
	const GameInstallationProfile normalized = normalizedGameInstallationProfile(profile);
	GameInstallationValidation validation;

	if (normalized.rootPath.isEmpty()) {
		validation.errors << installText("Installation root is empty.");
		return validation;
	}

	const QFileInfo rootInfo(normalized.rootPath);
	validation.rootExists = rootInfo.exists();
	validation.rootIsDirectory = rootInfo.isDir();
	if (!validation.rootExists) {
		validation.errors << installText("Installation root does not exist.");
	} else if (!validation.rootIsDirectory) {
		validation.errors << installText("Installation root is not a directory.");
	}

	if (!normalized.executablePath.isEmpty()) {
		const QFileInfo executableInfo(normalized.executablePath);
		validation.executableExists = executableInfo.exists();
		validation.executableIsFile = executableInfo.isFile();
		if (!validation.executableExists) {
			validation.warnings << installText("Executable path does not exist yet.");
		} else if (!validation.executableIsFile) {
			validation.warnings << installText("Executable path is not a file.");
		}
	}

	for (const QString& packagePath : normalized.basePackagePaths + normalized.modPackagePaths) {
		if (!QFileInfo::exists(packagePath)) {
			validation.warnings << installText("Package path is missing: %1").arg(QDir::toNativeSeparators(packagePath));
		}
	}

	const GameDefinition definition = gameDefinitionForKey(normalized.gameKey);
	if (validation.rootExists && validation.rootIsDirectory && normalized.basePackagePaths.isEmpty() && !definition.expectedBasePackages.isEmpty()) {
		bool foundExpectedPackage = false;
		for (const QString& relativePath : definition.expectedBasePackages) {
			if (pathExistsUnderRoot(normalized.rootPath, relativePath)) {
				foundExpectedPackage = true;
				break;
			}
		}
		if (!foundExpectedPackage) {
			validation.warnings << installText("No expected base package was found for %1.").arg(definition.displayName);
		}
	}

	if (normalized.engineFamily == GameEngineFamily::Unknown) {
		validation.warnings << installText("Engine family is unknown; compiler and validation defaults will stay generic.");
	}

	return validation;
}

bool sameGameInstallationId(const QString& left, const QString& right)
{
	return normalizedId(left) == normalizedId(right);
}

QStringList defaultSteamLibraryRoots()
{
	QStringList roots;
	const QString home = QDir::homePath();
	const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
	const QString programFiles = qEnvironmentVariable("ProgramFiles");
	const QString steamPath = qEnvironmentVariable("STEAM_PATH");
	const QString steamHome = qEnvironmentVariable("STEAM_HOME");

	if (!programFilesX86.isEmpty()) {
		roots << QDir(programFilesX86).filePath(QStringLiteral("Steam"));
	}
	if (!programFiles.isEmpty()) {
		roots << QDir(programFiles).filePath(QStringLiteral("Steam"));
	}
	if (!steamPath.isEmpty()) {
		roots << steamPath;
	}
	if (!steamHome.isEmpty()) {
		roots << steamHome;
	}
	roots << QStringLiteral("C:/Program Files (x86)/Steam");
	roots << QStringLiteral("C:/Program Files/Steam");
	roots << QDir(home).filePath(QStringLiteral(".steam/steam"));
	roots << QDir(home).filePath(QStringLiteral(".local/share/Steam"));
	roots << QDir(home).filePath(QStringLiteral("Library/Application Support/Steam"));

	QStringList commonRoots;
	for (const QString& root : roots) {
		commonRoots += steamCommonRootsFromRoot(root);
	}
	return uniqueNormalizedPaths(commonRoots);
}

QStringList defaultGogLibraryRoots()
{
	const QString home = QDir::homePath();
	QStringList roots;
	const QString gogGames = qEnvironmentVariable("GOG_GAMES");
	if (!gogGames.isEmpty()) {
		roots << gogGames;
	}
	roots << QStringLiteral("C:/GOG Games");
	roots << QStringLiteral("C:/Program Files (x86)/GOG Galaxy/Games");
	roots << QStringLiteral("C:/Program Files/GOG Galaxy/Games");
	roots << QDir(home).filePath(QStringLiteral("GOG Games"));
	roots << QDir(home).filePath(QStringLiteral("Games/GOG"));
	roots << QStringLiteral("/Applications");
	return uniqueNormalizedPaths(roots);
}

QVector<GameInstallationDetectionCandidate> detectSteamGameInstallations(const QStringList& additionalRoots)
{
	QStringList roots = defaultSteamLibraryRoots();
	for (const QString& root : additionalRoots) {
		roots += steamCommonRootsFromRoot(root);
		roots << root;
	}

	QVector<GameInstallationDetectionCandidate> candidates;
	for (const QString& directory : oneLevelCandidateDirectories(roots)) {
		candidates += candidatesForDirectory(directory, QStringLiteral("steam"), installText("Steam"));
	}
	return deduplicateCandidates(candidates);
}

QVector<GameInstallationDetectionCandidate> detectGogGameInstallations(const QStringList& additionalRoots)
{
	QStringList roots = defaultGogLibraryRoots();
	roots += additionalRoots;

	QVector<GameInstallationDetectionCandidate> candidates;
	for (const QString& directory : oneLevelCandidateDirectories(roots)) {
		candidates += candidatesForDirectory(directory, QStringLiteral("gog"), installText("GOG"));
	}
	return deduplicateCandidates(candidates);
}

QVector<GameInstallationDetectionCandidate> detectGameInstallations(const QStringList& additionalSteamRoots, const QStringList& additionalGogRoots)
{
	QVector<GameInstallationDetectionCandidate> candidates = detectSteamGameInstallations(additionalSteamRoots);
	candidates += detectGogGameInstallations(additionalGogRoots);
	return deduplicateCandidates(candidates);
}

} // namespace vibestudio
