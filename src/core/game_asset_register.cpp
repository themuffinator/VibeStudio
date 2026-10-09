#include "core/game_asset_register.h"

#include "core/deflate.h"
#include "core/map_assets.h"
#include "core/studio_manifest.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>

#include <algorithm>
#include <numeric>

namespace vibestudio {

namespace {

constexpr qint64 kMaximumScriptBytes = 16ll * 1024 * 1024;
constexpr qint64 kMaximumAllScriptBytes = 64ll * 1024 * 1024;
constexpr qint64 kMaximumTableBytes = 16ll * 1024 * 1024;
constexpr qint64 kChunkBytes = 64 * 1024;

QString pathKey(const QString& path)
{
	QString key = path.trimmed();
	key.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return key.toCaseFolded();
}

QString doomKey(const QString& name)
{
	return name.trimmed().toUpper();
}

QString sourceIdForRelativePath(const QString& relativePath)
{
	QString id = QDir::cleanPath(relativePath);
	id.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return id.toLower();
}

// Walks `relativePath` under `root`, matching each component without regard
// to case, so "baseq3/pak0.pk3" finds "BaseQ3/PAK0.PK3" on a case-sensitive
// filesystem. Empty when any component is missing.
QString findCaseInsensitive(const QString& root, const QString& relativePath)
{
	QString current = QDir::cleanPath(root);
	const QStringList parts = QDir::cleanPath(relativePath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
	for (qsizetype i = 0; i < parts.size(); ++i) {
		const QString& part = parts[i];
		const bool last = i == parts.size() - 1;
		const QString direct = QDir(current).filePath(part);
		const QFileInfo directInfo(direct);
		if (directInfo.exists() && (last ? directInfo.isFile() : directInfo.isDir())) {
			current = direct;
			continue;
		}
		const QDir::Filters filters = (last ? QDir::Files : QDir::Dirs) | QDir::NoDotAndDotDot | QDir::Hidden;
		QString found;
		for (const QFileInfo& child : QDir(current).entryInfoList(filters)) {
			if (child.fileName().compare(part, Qt::CaseInsensitive) == 0) {
				found = child.absoluteFilePath();
				break;
			}
		}
		if (found.isEmpty()) {
			return {};
		}
		current = found;
	}
	return QFileInfo(current).absoluteFilePath();
}

QJsonArray jsonStrings(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QString crcHex(quint32 crc)
{
	return QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0'));
}

bool parseCrcHex(const QString& text, quint32* crc)
{
	if (text.size() != 8) {
		return false;
	}
	bool ok = false;
	const quint32 value = text.toUInt(&ok, 16);
	if (ok && crc) {
		*crc = value;
	}
	return ok;
}

qint32 le32(const QByteArray& bytes, qsizetype at)
{
	return qFromLittleEndian<qint32>(bytes.constData() + at);
}

QString lumpNameAt(const QByteArray& bytes, qsizetype at)
{
	return QString::fromLatin1(bytes.mid(at, 8).split('\0').first()).trimmed().toUpper();
}

// TEXTURE1/TEXTURE2 directory names. Layout from Chocolate Doom 3.1.0 r_data.c
// (maptexture_t; GPL-2.0-or-later). Copyright (C) 1993-1996 Id Software, Inc.;
// 2005-2014 Simon Howard. Bounds-checked Qt parsing is VibeStudio code.
QStringList doomTextureTableNames(const QByteArray& bytes, bool* valid)
{
	QStringList names;
	*valid = false;
	if (bytes.size() < 4) {
		return names;
	}
	const qint32 count = le32(bytes, 0);
	if (count < 0 || count > 65536 || 4ll + 4ll * count > bytes.size()) {
		return names;
	}
	for (qint32 i = 0; i < count; ++i) {
		const qint64 offset = le32(bytes, 4 + 4 * qsizetype(i));
		if (offset < 4ll + 4ll * count || offset + 8 > bytes.size()) {
			return {};
		}
		const QString name = lumpNameAt(bytes, offset);
		if (!name.isEmpty()) {
			names << name;
		}
	}
	*valid = true;
	return names;
}

// PNAMES: a count followed by eight-byte patch names (same source as above).
QStringList doomPatchTableNames(const QByteArray& bytes, bool* valid)
{
	QStringList names;
	*valid = false;
	if (bytes.size() < 4) {
		return names;
	}
	const qint32 count = le32(bytes, 0);
	if (count < 0 || count > 65536 || 4ll + 8ll * count > bytes.size()) {
		return names;
	}
	for (qint32 i = 0; i < count; ++i) {
		const QString name = lumpNameAt(bytes, 4 + 8 * qsizetype(i));
		if (!name.isEmpty()) {
			names << name;
		}
	}
	*valid = true;
	return names;
}

bool doomMusicHeader(const QByteArray& head)
{
	return head.startsWith("MUS\x1a") || head.startsWith("MThd") || head.startsWith("OggS") || head.startsWith("ID3") || head.startsWith("fLaC");
}

bool isDoomMapLumpName(const QString& upper)
{
	static const QSet<QString> lumps {
		QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"), QStringLiteral("VERTEXES"), QStringLiteral("SEGS"),
		QStringLiteral("SSECTORS"), QStringLiteral("NODES"), QStringLiteral("SECTORS"), QStringLiteral("REJECT"), QStringLiteral("BLOCKMAP"),
		QStringLiteral("BEHAVIOR"), QStringLiteral("SCRIPTS"), QStringLiteral("TEXTMAP"), QStringLiteral("ZNODES"), QStringLiteral("ENDMAP"),
		QStringLiteral("DIALOGUE"),
	};
	return lumps.contains(upper);
}

QString formatLabel(PackageArchiveFormat format)
{
	return packageArchiveFormatId(format);
}

} // namespace

bool GameAssetRegister::isEmpty() const
{
	return files.isEmpty() && shaders.isEmpty() && doomNames.isEmpty();
}

void GameAssetRegister::rebuildIndex()
{
	m_pathIndex.clear();
	m_shaderIndex.clear();
	m_doomIndex.clear();
	m_pathIndex.reserve(files.size());
	for (int i = 0; i < files.size(); ++i) {
		// Later sources shadow earlier ones, as the engines' search paths do.
		m_pathIndex.insert(pathKey(files[i].path), i);
	}
	for (int i = 0; i < shaders.size(); ++i) {
		m_shaderIndex.insert(pathKey(shaders[i].name), i);
	}
	for (auto it = doomNames.cbegin(); it != doomNames.cend(); ++it) {
		QHash<QString, int>& names = m_doomIndex[it.key()];
		for (int i = 0; i < it.value().size(); ++i) {
			names.insert(doomKey(it.value()[i].name), i);
		}
	}
}

GameAssetRegister GameAssetRegister::filtered(const QStringList& sourceIds) const
{
	GameAssetRegister result;
	result.gameKey = gameKey;
	result.installationId = installationId;
	result.installationName = installationName;
	result.installationRoot = installationRoot;
	result.createdUtc = createdUtc;
	result.studioVersion = studioVersion;
	QSet<QString> wanted;
	for (const QString& id : sourceIds) {
		wanted.insert(sourceIdForRelativePath(id));
	}
	QHash<int, int> sourceMap;
	for (int i = 0; i < sources.size(); ++i) {
		if (wanted.contains(sources[i].id)) {
			sourceMap.insert(i, int(result.sources.size()));
			result.sources << sources[i];
		}
	}
	QHash<int, int> fileMap;
	for (int i = 0; i < files.size(); ++i) {
		const auto mapped = sourceMap.constFind(files[i].source);
		if (mapped == sourceMap.cend()) {
			continue;
		}
		GameAssetRegisterFile file = files[i];
		file.source = *mapped;
		fileMap.insert(i, int(result.files.size()));
		result.files << file;
	}
	for (const GameAssetRegisterName& shader : shaders) {
		const auto mapped = sourceMap.constFind(shader.source);
		if (mapped == sourceMap.cend()) {
			continue;
		}
		GameAssetRegisterName copy = shader;
		copy.source = *mapped;
		copy.file = fileMap.value(shader.file, -1);
		result.shaders << copy;
	}
	for (auto it = doomNames.cbegin(); it != doomNames.cend(); ++it) {
		for (const GameAssetRegisterName& name : it.value()) {
			const auto mapped = sourceMap.constFind(name.source);
			if (mapped == sourceMap.cend()) {
				continue;
			}
			GameAssetRegisterName copy = name;
			copy.source = *mapped;
			result.doomNames[it.key()] << copy;
		}
	}
	result.rebuildIndex();
	return result;
}

QStringList GameAssetRegister::defaultSourceIds() const
{
	QStringList ids;
	for (const GameAssetRegisterSource& source : sources) {
		if (source.role == QStringLiteral("base")) {
			ids << source.id;
		}
	}
	return ids;
}

const GameAssetRegisterFile* GameAssetRegister::file(const QString& virtualPath) const
{
	const auto it = m_pathIndex.constFind(pathKey(virtualPath));
	return it == m_pathIndex.cend() ? nullptr : &files[*it];
}

GameAssetMatch GameAssetRegister::match(const QString& virtualPath, quint64 sizeBytes, quint32 crc32) const
{
	const GameAssetRegisterFile* stock = file(virtualPath);
	if (!stock) {
		return GameAssetMatch::None;
	}
	return stock->sizeBytes == sizeBytes && stock->crc32 == crc32 ? GameAssetMatch::Identical : GameAssetMatch::Different;
}

const GameAssetRegisterFile* GameAssetRegister::shaderScript(const QString& shaderName) const
{
	const auto it = m_shaderIndex.constFind(pathKey(shaderName));
	if (it == m_shaderIndex.cend()) {
		return nullptr;
	}
	const int fileIndex = shaders[*it].file;
	return fileIndex >= 0 && fileIndex < files.size() ? &files[fileIndex] : nullptr;
}

bool GameAssetRegister::hasShader(const QString& shaderName) const
{
	return m_shaderIndex.contains(pathKey(shaderName));
}

bool GameAssetRegister::hasDoomName(const QString& namespaceId, const QString& name) const
{
	return doomNameSource(namespaceId, name) >= 0;
}

int GameAssetRegister::doomNameSource(const QString& namespaceId, const QString& name) const
{
	const auto it = m_doomIndex.constFind(namespaceId);
	if (it == m_doomIndex.cend()) {
		return -1;
	}
	const auto found = it->constFind(doomKey(name));
	if (found == it->cend()) {
		return -1;
	}
	const QVector<GameAssetRegisterName>& names = doomNames.value(namespaceId);
	return *found >= 0 && *found < names.size() ? names[*found].source : -1;
}

const GameAssetRegisterSource* GameAssetRegister::source(int index) const
{
	return index >= 0 && index < sources.size() ? &sources[index] : nullptr;
}

QString GameAssetRegister::sourceLabel(int index) const
{
	const GameAssetRegisterSource* found = source(index);
	if (!found) {
		return {};
	}
	return found->label.isEmpty() ? found->relativePath : QStringLiteral("%1 (%2)").arg(found->label, found->relativePath);
}

QVector<GameStockPackage> gameStockPackageCandidates(const QString& gameKey)
{
	// Engine search order: the base game's numbered packages, lowest first, so
	// a later package shadows an earlier one exactly as the engines load them.
	const QString key = normalizedGameKey(gameKey);
	QVector<GameStockPackage> packages;
	const auto add = [&packages](const QString& path, const QString& role, const QString& label) {
		packages.push_back({path, role, label});
	};
	if (key == QStringLiteral("quake")) {
		const QString quake = QCoreApplication::translate("VibeStudioAssetRegister", "Quake");
		add(QStringLiteral("id1/pak0.pak"), QStringLiteral("base"), quake);
		add(QStringLiteral("id1/pak1.pak"), QStringLiteral("base"), quake);
		add(QStringLiteral("hipnotic/pak0.pak"), QStringLiteral("expansion"), QCoreApplication::translate("VibeStudioAssetRegister", "Scourge of Armagon"));
		add(QStringLiteral("rogue/pak0.pak"), QStringLiteral("expansion"), QCoreApplication::translate("VibeStudioAssetRegister", "Dissolution of Eternity"));
	} else if (key == QStringLiteral("quake2")) {
		const QString quake2 = QCoreApplication::translate("VibeStudioAssetRegister", "Quake II");
		add(QStringLiteral("baseq2/pak0.pak"), QStringLiteral("base"), quake2);
		add(QStringLiteral("baseq2/pak1.pak"), QStringLiteral("base"), quake2);
		add(QStringLiteral("baseq2/pak2.pak"), QStringLiteral("base"), quake2);
		add(QStringLiteral("xatrix/pak0.pak"), QStringLiteral("expansion"), QCoreApplication::translate("VibeStudioAssetRegister", "The Reckoning"));
		add(QStringLiteral("rogue/pak0.pak"), QStringLiteral("expansion"), QCoreApplication::translate("VibeStudioAssetRegister", "Ground Zero"));
	} else if (key == QStringLiteral("quake3")) {
		const QString quake3 = QCoreApplication::translate("VibeStudioAssetRegister", "Quake III Arena");
		for (int i = 0; i <= 8; ++i) {
			add(QStringLiteral("baseq3/pak%1.pk3").arg(i), QStringLiteral("base"), quake3);
		}
		const QString teamArena = QCoreApplication::translate("VibeStudioAssetRegister", "Team Arena");
		for (int i = 0; i <= 3; ++i) {
			add(QStringLiteral("missionpack/pak%1.pk3").arg(i), QStringLiteral("expansion"), teamArena);
		}
	} else if (key == QStringLiteral("doom")) {
		// Each IWAD is a whole game. The first one found is the base; the
		// others count only when a project names them.
		add(QStringLiteral("doom2.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "Doom II"));
		add(QStringLiteral("doom.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "The Ultimate Doom"));
		add(QStringLiteral("tnt.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "TNT: Evilution"));
		add(QStringLiteral("plutonia.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "The Plutonia Experiment"));
		add(QStringLiteral("freedoom2.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "Freedoom: Phase 2"));
		add(QStringLiteral("freedoom1.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "Freedoom: Phase 1"));
		add(QStringLiteral("doom1.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "Doom shareware"));
	} else if (key == QStringLiteral("heretic-hexen")) {
		add(QStringLiteral("heretic.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "Heretic"));
		add(QStringLiteral("hexen.wad"), QStringLiteral("base"), QCoreApplication::translate("VibeStudioAssetRegister", "Hexen"));
		add(QStringLiteral("hexdd.wad"), QStringLiteral("expansion"), QCoreApplication::translate("VibeStudioAssetRegister", "Deathkings of the Dark Citadel"));
	}
	return packages;
}

QVector<GameAssetRegisterSource> discoverGameStockSources(const GameInstallationProfile& installation, QStringList* warnings)
{
	QVector<GameAssetRegisterSource> sources;
	const QString root = QDir::cleanPath(installation.rootPath.trimmed());
	if (root.isEmpty() || !QFileInfo(root).isDir()) {
		if (warnings) {
			*warnings << QCoreApplication::translate("VibeStudioAssetRegister", "The installation folder does not exist: %1").arg(QDir::toNativeSeparators(root));
		}
		return sources;
	}
	QSet<QString> seen;
	const bool iwadGame = installation.engineFamily == GameEngineFamily::IdTech1
		|| normalizedGameKey(installation.gameKey) == QStringLiteral("doom") || normalizedGameKey(installation.gameKey) == QStringLiteral("heretic-hexen");
	bool haveBase = false;
	const auto append = [&](const QString& absolute, const QString& role, const QString& label) {
		const QFileInfo info(absolute);
		const QString relative = QDir(root).relativeFilePath(info.absoluteFilePath());
		const QString id = relative.startsWith(QStringLiteral("..")) ? sourceIdForRelativePath(info.absoluteFilePath()) : sourceIdForRelativePath(relative);
		if (seen.contains(id) || sources.size() >= GameAssetRegister::kMaximumSources) {
			return;
		}
		seen.insert(id);
		GameAssetRegisterSource source;
		source.id = id;
		source.relativePath = relative.startsWith(QStringLiteral("..")) ? info.absoluteFilePath() : QDir::fromNativeSeparators(relative);
		source.absolutePath = info.absoluteFilePath();
		// Doom-family IWADs are alternatives to one another: only the first
		// is the base everybody has.
		source.role = (iwadGame && role == QStringLiteral("base") && haveBase) ? QStringLiteral("alternative") : role;
		haveBase = haveBase || source.role == QStringLiteral("base");
		source.label = label;
		source.format = packageArchiveFormatFromFileName(info.fileName());
		source.sizeBytes = info.size();
		source.modifiedUtc = info.lastModified().toUTC();
		sources.push_back(source);
	};
	for (const GameStockPackage& candidate : gameStockPackageCandidates(installation.gameKey)) {
		const QString found = findCaseInsensitive(root, candidate.relativePath);
		if (!found.isEmpty()) {
			append(found, candidate.role, candidate.label);
		}
	}
	for (const QString& path : installation.basePackagePaths) {
		const QString absolute = normalizedInstallationPath(path, root);
		if (QFileInfo(absolute).isFile()) {
			append(absolute, QStringLiteral("base"), QString());
		} else if (warnings) {
			*warnings << QCoreApplication::translate("VibeStudioAssetRegister", "A saved base package was not found: %1").arg(QDir::toNativeSeparators(absolute));
		}
	}
	return sources;
}

GameAssetRegisterBuildResult buildGameAssetRegister(const GameAssetRegisterBuildRequest& request)
{
	GameAssetRegisterBuildResult result;
	GameAssetRegister& reg = result.registerData;
	const GameInstallationProfile& installation = request.installation;
	reg.gameKey = normalizedGameKey(installation.gameKey);
	reg.installationId = installation.id;
	reg.installationName = installation.displayName;
	reg.installationRoot = QDir::cleanPath(installation.rootPath);
	reg.createdUtc = QDateTime::currentDateTimeUtc();
	reg.studioVersion = versionString();

	const auto cancelled = [&request]() { return request.control.isCancelled && request.control.isCancelled(); };
	const auto fail = [&result](const QString& message) {
		result.succeeded = false;
		result.error = message;
		return result;
	};

	QVector<GameAssetRegisterSource> sources;
	if (request.packagePaths.isEmpty()) {
		sources = discoverGameStockSources(installation, &result.warnings);
	} else {
		for (const QString& path : request.packagePaths) {
			const QString absolute = normalizedInstallationPath(path, reg.installationRoot);
			const QFileInfo info(absolute);
			// A folder counts too: a mod's loose files are part of what it provides.
			if (!info.exists()) {
				return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Stock package not found: %1").arg(QDir::toNativeSeparators(absolute)));
			}
			GameAssetRegisterSource source;
			const QString relative = reg.installationRoot.isEmpty() ? QString() : QDir(reg.installationRoot).relativeFilePath(info.absoluteFilePath());
			const bool inside = !relative.isEmpty() && !relative.startsWith(QStringLiteral(".."));
			source.relativePath = inside ? QDir::fromNativeSeparators(relative) : info.absoluteFilePath();
			source.id = sourceIdForRelativePath(inside ? relative : info.fileName());
			source.absolutePath = info.absoluteFilePath();
			source.role = QStringLiteral("base");
			source.format = info.isDir() ? PackageArchiveFormat::Folder : packageArchiveFormatFromFileName(info.fileName());
			source.sizeBytes = info.isDir() ? 0 : info.size();
			source.modifiedUtc = info.lastModified().toUTC();
			bool duplicate = false;
			for (const GameAssetRegisterSource& existing : std::as_const(sources)) {
				duplicate = duplicate || existing.id == source.id;
			}
			if (duplicate) {
				return fail(QCoreApplication::translate("VibeStudioAssetRegister", "A stock package is listed twice: %1").arg(source.id));
			}
			sources << source;
		}
		if (sources.size() > GameAssetRegister::kMaximumSources) {
			return fail(QCoreApplication::translate("VibeStudioAssetRegister", "A register can index at most %1 packages.").arg(GameAssetRegister::kMaximumSources));
		}
	}
	if (sources.isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "No stock packages were found for %1. Check the installation folder and game, or name the packages explicitly.")
						.arg(gameDefinitionForKey(reg.gameKey).displayName));
	}

	qint64 scriptBytes = 0;
	qsizetype nameCount = 0;
	for (int sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
		if (cancelled()) {
			result.cancelled = true;
			return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Indexing was cancelled."));
		}
		GameAssetRegisterSource source = sources[sourceIndex];
		const QString label = source.label.isEmpty() ? source.relativePath : source.label;
		if (request.control.progress) {
			request.control.progress(QCoreApplication::translate("VibeStudioAssetRegister", "Opening %1").arg(source.relativePath), 0, source.sizeBytes);
		}
		PackageArchive archive;
		QString error;
		PackageReadControl loadControl;
		loadControl.isCancelled = request.control.isCancelled;
		loadControl.progress = [&request, &source](const QString&, qint64 completed, qint64 total) {
			if (request.control.progress) {
				request.control.progress(QCoreApplication::translate("VibeStudioAssetRegister", "Opening %1").arg(source.relativePath), completed, total);
			}
		};
		if (!archive.load(source.absolutePath, &error, loadControl)) {
			if (cancelled()) {
				result.cancelled = true;
				return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Indexing was cancelled."));
			}
			return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Unable to open %1: %2").arg(QDir::toNativeSeparators(source.absolutePath), error));
		}
		source.format = archive.format();
		const QVector<PackageEntry> entries = archive.entries();
		const bool doomWad = archive.format() == PackageArchiveFormat::Wad
			&& (archive.wadMagic() == QStringLiteral("IWAD") || archive.wadMagic() == QStringLiteral("PWAD"));
		const quint64 totalBytes = std::accumulate(entries.cbegin(), entries.cend(), quint64(0), [](quint64 sum, const PackageEntry& entry) {
			return entry.kind == PackageEntryKind::File ? sum + entry.sizeBytes : sum;
		});
		quint64 doneBytes = 0;
		int fileCount = 0;
		// Listings sort by name; a WAD's meaning (namespaces, map groups) is in
		// its directory order, which each entry keeps as its source ordinal.
		QVector<qsizetype> order(entries.size());
		std::iota(order.begin(), order.end(), qsizetype(0));
		if (doomWad) {
			std::stable_sort(order.begin(), order.end(), [&entries](qsizetype left, qsizetype right) {
				return entries[left].sourceOrdinal < entries[right].sourceOrdinal;
			});
		}
		for (qsizetype position = 0; position < order.size(); ++position) {
			const qsizetype index = order[position];
			const PackageEntry& entry = entries[index];
			if (entry.kind != PackageEntryKind::File) {
				continue;
			}
			if (cancelled()) {
				result.cancelled = true;
				return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Indexing was cancelled."));
			}
			if (reg.files.size() >= GameAssetRegister::kMaximumFiles) {
				return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The stock packages hold more than %1 files; the register cannot index them.").arg(GameAssetRegister::kMaximumFiles));
			}
			if (!entry.readable) {
				result.warnings << QCoreApplication::translate("VibeStudioAssetRegister", "%1: %2 cannot be read and was left out.").arg(source.relativePath, entry.virtualPath);
				continue;
			}
			GameAssetRegisterFile file;
			file.path = entry.virtualPath;
			file.sizeBytes = entry.sizeBytes;
			file.source = sourceIndex;
			if (doomWad) {
				file.typeHint = entry.typeHint;
			}
			QByteArray head;
			if (entry.hasCrc32) {
				file.crc32 = entry.crc32;
			} else {
				quint32 crc = 0;
				QString readError;
				const bool streamed = archive.streamEntryAt(index, [&](QByteArrayView chunk) {
					crc = crc32View(chunk, crc);
					if (head.size() < 8) {
						head.append(chunk.left(8 - head.size()).toByteArray());
					}
					return !cancelled();
				}, &readError, cancelled);
				if (!streamed) {
					if (cancelled()) {
						result.cancelled = true;
						return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Indexing was cancelled."));
					}
					return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Unable to read %1 in %2: %3").arg(entry.virtualPath, source.relativePath, readError));
				}
				file.crc32 = crc;
				result.bytesRead += entry.sizeBytes;
			}
			doneBytes += entry.sizeBytes;
			if (request.control.progress && (fileCount % 32 == 0)) {
				request.control.progress(QCoreApplication::translate("VibeStudioAssetRegister", "Indexing %1").arg(label), qint64(doneBytes), qint64(totalBytes));
			}
			const int fileIndex = int(reg.files.size());
			reg.files << file;
			++fileCount;

			const QString lower = entry.virtualPath.toLower();
			if (lower.startsWith(QStringLiteral("scripts/")) && lower.endsWith(QStringLiteral(".shader"))) {
				if (qint64(entry.sizeBytes) > kMaximumScriptBytes || scriptBytes + qint64(entry.sizeBytes) > kMaximumAllScriptBytes) {
					result.warnings << QCoreApplication::translate("VibeStudioAssetRegister", "%1: shader script %2 is over the scan budget; its shaders were not indexed.").arg(source.relativePath, entry.virtualPath);
				} else {
					scriptBytes += qint64(entry.sizeBytes);
					QByteArray bytes;
					QString readError;
					if (!archive.readEntryAt(index, &bytes, &readError, kMaximumScriptBytes + 1)) {
						return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Unable to read %1 in %2: %3").arg(entry.virtualPath, source.relativePath, readError));
					}
					QStringList scriptWarnings;
					PackageReadControl scriptControl;
					scriptControl.isCancelled = request.control.isCancelled;
					for (const QString& name : collectShaderScriptNames(bytes, &scriptWarnings, scriptControl)) {
						if (++nameCount > GameAssetRegister::kMaximumNames) {
							return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The stock packages declare more names than the register can hold."));
						}
						reg.shaders << GameAssetRegisterName {name, sourceIndex, fileIndex};
					}
					for (const QString& warning : std::as_const(scriptWarnings)) {
						result.warnings << QStringLiteral("%1: %2: %3").arg(source.relativePath, entry.virtualPath, warning);
					}
				}
			}
			if (!doomWad) {
				continue;
			}
			const QString upper = entry.virtualPath.toUpper();
			const auto addName = [&](const QString& ns, const QString& name) {
				if (++nameCount > GameAssetRegister::kMaximumNames) {
					return false;
				}
				reg.doomNames[ns] << GameAssetRegisterName {name, sourceIndex, -1};
				return true;
			};
			bool added = true;
			if (entry.typeHint == QStringLiteral("wad-flat")) {
				added = addName(QStringLiteral("flat"), upper);
			} else if (entry.typeHint == QStringLiteral("wad-patch")) {
				added = addName(QStringLiteral("patch"), upper);
			} else if (entry.typeHint == QStringLiteral("wad-sprite")) {
				added = addName(QStringLiteral("sprite"), upper);
			} else if (entry.typeHint == QStringLiteral("wad-sound") || upper.startsWith(QStringLiteral("DP")) || upper.startsWith(QStringLiteral("DS"))) {
				added = addName(QStringLiteral("sound"), upper);
			} else if ((upper.startsWith(QStringLiteral("D_")) || upper.startsWith(QStringLiteral("MUS_"))) && (head.isEmpty() || doomMusicHeader(head))) {
				added = addName(QStringLiteral("music"), upper);
			} else if (upper == QStringLiteral("TEXTURE1") || upper == QStringLiteral("TEXTURE2") || upper == QStringLiteral("PNAMES")) {
				if (qint64(entry.sizeBytes) > kMaximumTableBytes) {
					return fail(QCoreApplication::translate("VibeStudioAssetRegister", "%1 in %2 is too large to index.").arg(upper, source.relativePath));
				}
				QByteArray bytes;
				QString readError;
				if (!archive.readEntryAt(index, &bytes, &readError, kMaximumTableBytes + 1)) {
					return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Unable to read %1 in %2: %3").arg(upper, source.relativePath, readError));
				}
				bool valid = false;
				const bool patches = upper == QStringLiteral("PNAMES");
				const QStringList names = patches ? doomPatchTableNames(bytes, &valid) : doomTextureTableNames(bytes, &valid);
				if (!valid) {
					result.warnings << QCoreApplication::translate("VibeStudioAssetRegister", "%1 in %2 is malformed; its names were not indexed.").arg(upper, source.relativePath);
				}
				for (const QString& name : names) {
					added = added && addName(patches ? QStringLiteral("patch") : QStringLiteral("texture"), name);
				}
			} else if (position + 1 < order.size()) {
				const QString next = entries[order[position + 1]].virtualPath.toUpper();
				if ((next == QStringLiteral("THINGS") || next == QStringLiteral("TEXTMAP")) && !isDoomMapLumpName(upper)) {
					added = addName(QStringLiteral("map"), upper);
				}
			}
			if (!added) {
				return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The stock packages declare more names than the register can hold."));
			}
		}
		source.fileCount = fileCount;
		reg.sources << source;
	}
	reg.rebuildIndex();
	result.succeeded = true;
	if (request.control.progress) {
		request.control.progress(QCoreApplication::translate("VibeStudioAssetRegister", "Indexed %1").arg(reg.installationName), 1, 1);
	}
	return result;
}

QString gameAssetRegisterDirectory()
{
	const QString root = StudioSettings::overrideFilePath().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
		: QFileInfo(StudioSettings::overrideFilePath()).absolutePath();
	return QDir(root).absoluteFilePath(QStringLiteral("asset-registers"));
}

QString gameAssetRegisterPath(const QString& installationId)
{
	QString name = installationId.trimmed().toLower();
	name.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("-"));
	while (name.startsWith(QLatin1Char('.')) || name.startsWith(QLatin1Char('-'))) {
		name.remove(0, 1);
	}
	if (name.isEmpty()) {
		return {};
	}
	return QDir(gameAssetRegisterDirectory()).absoluteFilePath(name.left(120) + QStringLiteral(".json"));
}

QByteArray gameAssetRegisterJsonBytes(const GameAssetRegister& reg)
{
	QJsonObject root;
	root.insert(QStringLiteral("format"), QStringLiteral("vibestudio-asset-register"));
	root.insert(QStringLiteral("version"), GameAssetRegister::kFormatVersion);
	root.insert(QStringLiteral("game"), reg.gameKey);
	root.insert(QStringLiteral("installation"), QJsonObject {
		{QStringLiteral("id"), reg.installationId},
		{QStringLiteral("name"), reg.installationName},
		{QStringLiteral("root"), QDir::fromNativeSeparators(reg.installationRoot)},
	});
	root.insert(QStringLiteral("createdUtc"), reg.createdUtc.toUTC().toString(Qt::ISODateWithMs));
	root.insert(QStringLiteral("studioVersion"), reg.studioVersion);
	QJsonArray sources;
	for (const GameAssetRegisterSource& source : reg.sources) {
		sources.append(QJsonObject {
			{QStringLiteral("id"), source.id},
			{QStringLiteral("path"), source.relativePath},
			{QStringLiteral("role"), source.role},
			{QStringLiteral("label"), source.label},
			{QStringLiteral("format"), formatLabel(source.format)},
			{QStringLiteral("bytes"), double(source.sizeBytes)},
			{QStringLiteral("modifiedUtc"), source.modifiedUtc.toUTC().toString(Qt::ISODateWithMs)},
			{QStringLiteral("files"), source.fileCount},
		});
	}
	root.insert(QStringLiteral("sources"), sources);
	// Compact rows keep a 4,000-file register near 300 KB.
	QJsonArray files;
	for (const GameAssetRegisterFile& file : reg.files) {
		QJsonArray row {file.path, double(file.sizeBytes), crcHex(file.crc32), file.source};
		if (!file.typeHint.isEmpty()) {
			row.append(file.typeHint);
		}
		files.append(row);
	}
	root.insert(QStringLiteral("files"), files);
	QJsonArray shaders;
	for (const GameAssetRegisterName& shader : reg.shaders) {
		shaders.append(QJsonArray {shader.name, shader.source, shader.file});
	}
	root.insert(QStringLiteral("shaders"), shaders);
	QJsonObject doom;
	QStringList namespaces = reg.doomNames.keys();
	std::sort(namespaces.begin(), namespaces.end());
	for (const QString& ns : std::as_const(namespaces)) {
		QJsonArray names;
		for (const GameAssetRegisterName& name : reg.doomNames.value(ns)) {
			names.append(QJsonArray {name.name, name.source});
		}
		doom.insert(ns, names);
	}
	root.insert(QStringLiteral("doom"), doom);
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool parseGameAssetRegister(const QByteArray& bytes, GameAssetRegister* out, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (bytes.size() > GameAssetRegister::kMaximumFileBytes) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The asset register file is too large."));
	}
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The asset register is not valid JSON: %1").arg(parseError.errorString()));
	}
	const QJsonObject root = document.object();
	if (root.value(QStringLiteral("format")).toString() != QStringLiteral("vibestudio-asset-register")) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The file is not a VibeStudio asset register."));
	}
	const int version = root.value(QStringLiteral("version")).toInt(-1);
	if (version != GameAssetRegister::kFormatVersion) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Asset register version %1 is not supported; rebuild it with this version of VibeStudio.").arg(version));
	}
	GameAssetRegister reg;
	reg.gameKey = normalizedGameKey(root.value(QStringLiteral("game")).toString());
	const QJsonObject installation = root.value(QStringLiteral("installation")).toObject();
	reg.installationId = installation.value(QStringLiteral("id")).toString();
	reg.installationName = installation.value(QStringLiteral("name")).toString();
	reg.installationRoot = installation.value(QStringLiteral("root")).toString();
	reg.createdUtc = QDateTime::fromString(root.value(QStringLiteral("createdUtc")).toString(), Qt::ISODateWithMs).toUTC();
	reg.studioVersion = root.value(QStringLiteral("studioVersion")).toString();
	const QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
	if (sources.size() > GameAssetRegister::kMaximumSources) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The asset register lists too many packages."));
	}
	for (const QJsonValue& value : sources) {
		const QJsonObject object = value.toObject();
		GameAssetRegisterSource source;
		source.id = object.value(QStringLiteral("id")).toString();
		source.relativePath = object.value(QStringLiteral("path")).toString();
		source.role = object.value(QStringLiteral("role")).toString(QStringLiteral("base"));
		source.label = object.value(QStringLiteral("label")).toString();
		source.format = packageArchiveFormatFromId(object.value(QStringLiteral("format")).toString());
		source.sizeBytes = qint64(object.value(QStringLiteral("bytes")).toDouble());
		source.modifiedUtc = QDateTime::fromString(object.value(QStringLiteral("modifiedUtc")).toString(), Qt::ISODateWithMs).toUTC();
		source.fileCount = object.value(QStringLiteral("files")).toInt();
		if (source.id.isEmpty() || source.relativePath.isEmpty()) {
			return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The asset register has a package without an id or path."));
		}
		if (!reg.installationRoot.isEmpty() && QDir::isRelativePath(source.relativePath)) {
			source.absolutePath = QDir(reg.installationRoot).absoluteFilePath(source.relativePath);
		} else {
			source.absolutePath = source.relativePath;
		}
		reg.sources << source;
	}
	const QJsonArray files = root.value(QStringLiteral("files")).toArray();
	if (files.size() > GameAssetRegister::kMaximumFiles) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The asset register lists too many files."));
	}
	reg.files.reserve(files.size());
	for (const QJsonValue& value : files) {
		const QJsonArray row = value.toArray();
		GameAssetRegisterFile file;
		file.path = row.at(0).toString();
		const double size = row.at(1).toDouble(-1);
		file.source = row.at(3).toInt(-1);
		file.typeHint = row.at(4).toString();
		if (file.path.isEmpty() || size < 0 || !parseCrcHex(row.at(2).toString(), &file.crc32) || file.source < 0 || file.source >= reg.sources.size()) {
			return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The asset register has a malformed file row."));
		}
		file.sizeBytes = quint64(size);
		reg.files << file;
	}
	qsizetype nameCount = 0;
	for (const QJsonValue& value : root.value(QStringLiteral("shaders")).toArray()) {
		const QJsonArray row = value.toArray();
		GameAssetRegisterName name {row.at(0).toString(), row.at(1).toInt(-1), row.at(2).toInt(-1)};
		if (name.name.isEmpty() || name.source < 0 || name.source >= reg.sources.size() || name.file < -1 || name.file >= reg.files.size()
			|| ++nameCount > GameAssetRegister::kMaximumNames) {
			return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The asset register has a malformed shader row."));
		}
		reg.shaders << name;
	}
	const QJsonObject doom = root.value(QStringLiteral("doom")).toObject();
	for (auto it = doom.constBegin(); it != doom.constEnd(); ++it) {
		for (const QJsonValue& value : it.value().toArray()) {
			const QJsonArray row = value.toArray();
			GameAssetRegisterName name {row.at(0).toString(), row.at(1).toInt(-1), -1};
			if (name.name.isEmpty() || name.source < 0 || name.source >= reg.sources.size() || ++nameCount > GameAssetRegister::kMaximumNames) {
				return fail(QCoreApplication::translate("VibeStudioAssetRegister", "The asset register has a malformed Doom name row."));
			}
			reg.doomNames[it.key()] << name;
		}
	}
	reg.rebuildIndex();
	if (out) {
		*out = std::move(reg);
	}
	return true;
}

bool saveGameAssetRegister(const GameAssetRegister& reg, const QString& path, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (path.trimmed().isEmpty()) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "No asset register path was given."));
	}
	const QFileInfo info(path);
	if (!QDir().mkpath(info.absolutePath())) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Unable to create the asset register folder %1.").arg(QDir::toNativeSeparators(info.absolutePath())));
	}
	QSaveFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Unable to write %1: %2").arg(QDir::toNativeSeparators(info.absoluteFilePath()), file.errorString()));
	}
	const QByteArray bytes = gameAssetRegisterJsonBytes(reg);
	if (file.write(bytes) != bytes.size() || !file.commit()) {
		return fail(QCoreApplication::translate("VibeStudioAssetRegister", "Unable to write %1: %2").arg(QDir::toNativeSeparators(info.absoluteFilePath()), file.errorString()));
	}
	return true;
}

bool loadGameAssetRegister(const QString& path, GameAssetRegister* reg, QString* error)
{
	QFile file(path);
	if (!file.exists()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAssetRegister", "No asset register at %1.").arg(QDir::toNativeSeparators(path));
		}
		return false;
	}
	if (file.size() > GameAssetRegister::kMaximumFileBytes) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAssetRegister", "The asset register file is too large.");
		}
		return false;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAssetRegister", "Unable to read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	return parseGameAssetRegister(file.readAll(), reg, error);
}

GameAssetRegisterStatus gameAssetRegisterFreshness(const GameAssetRegister& reg, const GameInstallationProfile& installation)
{
	GameAssetRegisterStatus status;
	status.installationId = installation.id;
	status.loaded = true;
	status.exists = true;
	status.fresh = true;
	status.sourceCount = int(reg.sources.size());
	status.fileCount = int(reg.files.size());
	status.createdUtc = reg.createdUtc;
	if (normalizedGameKey(reg.gameKey) != normalizedGameKey(installation.gameKey)) {
		status.fresh = false;
		status.staleReasons << QCoreApplication::translate("VibeStudioAssetRegister", "The register was built for %1, but the installation is now %2.")
								   .arg(gameDefinitionForKey(reg.gameKey).displayName, gameDefinitionForKey(installation.gameKey).displayName);
	}
	const QString root = QDir::cleanPath(installation.rootPath);
	QSet<QString> registered;
	for (const GameAssetRegisterSource& source : reg.sources) {
		registered.insert(source.id);
		const QString absolute = QDir::isRelativePath(source.relativePath) ? findCaseInsensitive(root, source.relativePath) : source.relativePath;
		const QFileInfo info(absolute);
		if (absolute.isEmpty() || !info.isFile()) {
			status.fresh = false;
			status.staleReasons << QCoreApplication::translate("VibeStudioAssetRegister", "%1 is no longer in the installation.").arg(source.relativePath);
			continue;
		}
		if (info.size() != source.sizeBytes || info.lastModified().toUTC() != source.modifiedUtc) {
			status.fresh = false;
			status.staleReasons << QCoreApplication::translate("VibeStudioAssetRegister", "%1 changed since it was indexed.").arg(source.relativePath);
		}
	}
	for (const GameAssetRegisterSource& source : discoverGameStockSources(installation)) {
		if (!registered.contains(source.id)) {
			status.fresh = false;
			status.staleReasons << QCoreApplication::translate("VibeStudioAssetRegister", "%1 was added to the installation after indexing.").arg(source.relativePath);
		}
	}
	return status;
}

GameAssetRegisterStatus gameAssetRegisterStatus(const GameInstallationProfile& installation, GameAssetRegister* loaded)
{
	GameAssetRegisterStatus status;
	status.installationId = installation.id;
	status.path = gameAssetRegisterPath(installation.id);
	const QFileInfo info(status.path);
	status.exists = !status.path.isEmpty() && info.exists();
	if (!status.exists) {
		return status;
	}
	// Status-only callers (installation rows, health checks) reuse the parsed
	// header while the register file is unchanged; freshness is still checked.
	struct Header {
		qint64 size = -1;
		QDateTime modified;
		GameAssetRegister header;
		int fileCount = 0;
	};
	static QMutex cacheMutex;
	static QHash<QString, Header> cache;
	if (!loaded) {
		QMutexLocker locker(&cacheMutex);
		const auto cached = cache.constFind(status.path);
		if (cached != cache.cend() && cached->size == info.size() && cached->modified == info.lastModified()) {
			GameAssetRegisterStatus freshness = gameAssetRegisterFreshness(cached->header, installation);
			freshness.path = status.path;
			freshness.fileCount = cached->fileCount;
			return freshness;
		}
	}
	GameAssetRegister reg;
	QString error;
	if (!loadGameAssetRegister(status.path, &reg, &error)) {
		status.error = error;
		return status;
	}
	GameAssetRegisterStatus freshness = gameAssetRegisterFreshness(reg, installation);
	freshness.path = status.path;
	{
		Header header;
		header.size = info.size();
		header.modified = info.lastModified();
		header.fileCount = int(reg.files.size());
		header.header.gameKey = reg.gameKey;
		header.header.installationId = reg.installationId;
		header.header.installationName = reg.installationName;
		header.header.installationRoot = reg.installationRoot;
		header.header.createdUtc = reg.createdUtc;
		header.header.sources = reg.sources;
		QMutexLocker locker(&cacheMutex);
		if (cache.size() > 64) {
			cache.clear();
		}
		cache.insert(status.path, header);
	}
	if (loaded) {
		*loaded = std::move(reg);
	}
	return freshness;
}

QString gameAssetMatchId(GameAssetMatch match)
{
	switch (match) {
	case GameAssetMatch::None:
		return QStringLiteral("none");
	case GameAssetMatch::Identical:
		return QStringLiteral("identical");
	case GameAssetMatch::Different:
		return QStringLiteral("different");
	}
	return QStringLiteral("none");
}

QJsonObject gameAssetRegisterSummaryJson(const GameAssetRegister& reg)
{
	QJsonArray sources;
	for (const GameAssetRegisterSource& source : reg.sources) {
		sources.append(QJsonObject {
			{QStringLiteral("id"), source.id},
			{QStringLiteral("path"), source.relativePath},
			{QStringLiteral("role"), source.role},
			{QStringLiteral("label"), source.label},
			{QStringLiteral("format"), formatLabel(source.format)},
			{QStringLiteral("bytes"), double(source.sizeBytes)},
			{QStringLiteral("files"), source.fileCount},
		});
	}
	QJsonObject doom;
	for (auto it = reg.doomNames.cbegin(); it != reg.doomNames.cend(); ++it) {
		doom.insert(it.key(), int(it.value().size()));
	}
	return {
		{QStringLiteral("game"), reg.gameKey},
		{QStringLiteral("installationId"), reg.installationId},
		{QStringLiteral("installationName"), reg.installationName},
		{QStringLiteral("createdUtc"), reg.createdUtc.toUTC().toString(Qt::ISODate)},
		{QStringLiteral("studioVersion"), reg.studioVersion},
		{QStringLiteral("sources"), sources},
		{QStringLiteral("files"), int(reg.files.size())},
		{QStringLiteral("shaders"), int(reg.shaders.size())},
		{QStringLiteral("doomNames"), doom},
		{QStringLiteral("defaultSources"), jsonStrings(reg.defaultSourceIds())},
	};
}

QJsonObject gameAssetRegisterStatusJson(const GameAssetRegisterStatus& status)
{
	return {
		{QStringLiteral("installationId"), status.installationId},
		{QStringLiteral("path"), QDir::toNativeSeparators(status.path)},
		{QStringLiteral("exists"), status.exists},
		{QStringLiteral("loaded"), status.loaded},
		{QStringLiteral("fresh"), status.fresh},
		{QStringLiteral("usable"), status.usable()},
		{QStringLiteral("sources"), status.sourceCount},
		{QStringLiteral("files"), status.fileCount},
		{QStringLiteral("createdUtc"), status.createdUtc.isValid() ? status.createdUtc.toUTC().toString(Qt::ISODate) : QString()},
		{QStringLiteral("error"), status.error},
		{QStringLiteral("staleReasons"), jsonStrings(status.staleReasons)},
	};
}

QString gameAssetRegisterSummaryText(const GameAssetRegister& reg)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAssetRegister", "%1: %2 stock file(s) in %3 package(s)")
				 .arg(reg.installationName.isEmpty() ? gameDefinitionForKey(reg.gameKey).displayName : reg.installationName)
				 .arg(reg.files.size())
				 .arg(reg.sources.size());
	for (const GameAssetRegisterSource& source : reg.sources) {
		lines << QCoreApplication::translate("VibeStudioAssetRegister", "  %1  [%2]  %3 file(s)").arg(source.relativePath, source.role).arg(source.fileCount);
	}
	if (!reg.shaders.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioAssetRegister", "Shader declarations: %1").arg(reg.shaders.size());
	}
	QStringList namespaces = reg.doomNames.keys();
	std::sort(namespaces.begin(), namespaces.end());
	for (const QString& ns : std::as_const(namespaces)) {
		lines << QCoreApplication::translate("VibeStudioAssetRegister", "Doom %1 names: %2").arg(ns).arg(reg.doomNames.value(ns).size());
	}
	return lines.join(QLatin1Char('\n'));
}

bool fileCrc32(const QString& path, quint32* crc, QString* error, const std::function<bool()>& isCancelled)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioAssetRegister", "Unable to read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	quint32 value = 0;
	while (!file.atEnd()) {
		if (isCancelled && isCancelled()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioAssetRegister", "Cancelled.");
			}
			return false;
		}
		const QByteArray chunk = file.read(kChunkBytes);
		if (chunk.isEmpty() && !file.atEnd()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioAssetRegister", "Unable to read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
			}
			return false;
		}
		value = crc32View(chunk, value);
	}
	if (crc) {
		*crc = value;
	}
	return true;
}

} // namespace vibestudio
