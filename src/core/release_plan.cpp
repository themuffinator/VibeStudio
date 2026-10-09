#include "core/release_plan.h"

#include "core/advanced_studio.h"
#include "core/bsp_inspect.h"
#include "core/deflate.h"
#include "core/game_installation.h"
#include "core/level_build_workspace.h"
#include "core/level_map.h"
#include "core/model_mesh.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>
#include <numeric>

namespace vibestudio {

namespace {

constexpr qint64 kMaximumModelBytes = 64ll * 1024 * 1024;
constexpr qint64 kMaximumScriptBytes = 16ll * 1024 * 1024;

QString folded(const QString& path)
{
	QString key = path.trimmed();
	key.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return key.toCaseFolded();
}

QString suffixOf(const QString& path)
{
	return QFileInfo(path).suffix().toLower();
}

QJsonArray jsonStrings(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

void appendUnique(QStringList* list, const QString& value)
{
	if (!value.isEmpty() && !list->contains(value)) {
		list->append(value);
	}
}

// Glob patterns are project-relative with '/' separators: `*` and `?` stay
// within one folder, `**` crosses folders, and a pattern without '/' matches
// a file name anywhere. Matching ignores case.
QRegularExpression globExpression(const QString& pattern)
{
	QString source;
	for (qsizetype i = 0; i < pattern.size(); ++i) {
		const QChar ch = pattern[i];
		if (ch == QLatin1Char('*')) {
			if (i + 1 < pattern.size() && pattern[i + 1] == QLatin1Char('*')) {
				source += QStringLiteral(".*");
				++i;
				if (i + 1 < pattern.size() && pattern[i + 1] == QLatin1Char('/')) {
					source += QStringLiteral("/?");
					++i;
				}
			} else {
				source += QStringLiteral("[^/]*");
			}
		} else if (ch == QLatin1Char('?')) {
			source += QStringLiteral("[^/]");
		} else {
			source += QRegularExpression::escape(QString(ch));
		}
	}
	return QRegularExpression(QStringLiteral("^") + source + QStringLiteral("$"), QRegularExpression::CaseInsensitiveOption);
}

bool globMatches(const QStringList& patterns, const QString& virtualPath)
{
	const QString name = QFileInfo(virtualPath).fileName();
	for (QString pattern : patterns) {
		pattern = pattern.trimmed();
		pattern.replace(QLatin1Char('\\'), QLatin1Char('/'));
		if (pattern.isEmpty()) {
			continue;
		}
		if (pattern.endsWith(QLatin1Char('/'))) {
			if (virtualPath.startsWith(pattern, Qt::CaseInsensitive)) {
				return true;
			}
			continue;
		}
		const bool namePattern = !pattern.contains(QLatin1Char('/'));
		if (globExpression(pattern).match(namePattern ? name : virtualPath).hasMatch()) {
			return true;
		}
	}
	return false;
}

enum class FileClass { Content, Source, Intermediate, Document, NativeCode, Executable };

FileClass classifyFile(const QString& virtualPath, const QString& gameKey)
{
	const QString lower = virtualPath.toLower();
	const QString name = QFileInfo(lower).fileName();
	const QString ext = suffixOf(lower);
	static const QSet<QString> sources {
		QStringLiteral("map"), QStringLiteral("rmf"), QStringLiteral("jmf"), QStringLiteral("vmf"), QStringLiteral("psd"), QStringLiteral("xcf"),
		QStringLiteral("kra"), QStringLiteral("ora"), QStringLiteral("blend"), QStringLiteral("blend1"), QStringLiteral("max"), QStringLiteral("fbx"),
		QStringLiteral("ma"), QStringLiteral("mb"), QStringLiteral("ms3d"), QStringLiteral("3ds"), QStringLiteral("c4d"), QStringLiteral("spp"),
		QStringLiteral("sbsar"), QStringLiteral("sbs"), QStringLiteral("aseprite"), QStringLiteral("pdn"), QStringLiteral("afphoto"),
		QStringLiteral("vprefab"), QStringLiteral("vtexture"), QStringLiteral("vsaudio"), QStringLiteral("vsfx"), QStringLiteral("vibeworkspace"),
		QStringLiteral("qc"), QStringLiteral("qh"), QStringLiteral("src"),
	};
	static const QSet<QString> intermediates {
		QStringLiteral("prt"), QStringLiteral("lin"), QStringLiteral("pts"), QStringLiteral("srf"), QStringLiteral("log"), QStringLiteral("bak"),
		QStringLiteral("autosave"), QStringLiteral("tmp"), QStringLiteral("temp"), QStringLiteral("swp"), QStringLiteral("orig"), QStringLiteral("rej"),
		QStringLiteral("vsrecovery"), QStringLiteral("ilk"), QStringLiteral("pdb"), QStringLiteral("obj"), QStringLiteral("o"),
	};
	if (lower.endsWith(QStringLiteral(".mesh.json")) || lower.endsWith(QStringLiteral(".model.json")) || lower.endsWith(QStringLiteral(".assembly.json"))
		|| lower.endsWith(QStringLiteral(".texinfo.json")) || lower.contains(QStringLiteral("/autosave/")) || lower.startsWith(QStringLiteral("autosave/"))) {
		return lower.contains(QStringLiteral("autosave/")) || lower.endsWith(QStringLiteral(".texinfo.json")) ? FileClass::Intermediate : FileClass::Source;
	}
	if (ext == QStringLiteral("dll") || ext == QStringLiteral("so") || ext == QStringLiteral("dylib")) {
		return FileClass::NativeCode;
	}
	if (ext == QStringLiteral("exe") || ext == QStringLiteral("bat") || ext == QStringLiteral("cmd") || ext == QStringLiteral("sh")
		|| ext == QStringLiteral("ps1") || ext == QStringLiteral("app") || ext == QStringLiteral("msi")) {
		return FileClass::Executable;
	}
	if (intermediates.contains(ext) || name.endsWith(QLatin1Char('~'))) {
		return FileClass::Intermediate;
	}
	// Quake compiles WAD2 textures into the BSP; Quake II and III never read
	// texture WADs at run time. Doom-family WADs are the content itself.
	const QString game = normalizedGameKey(gameKey);
	if (ext == QStringLiteral("wad") && (game == QStringLiteral("quake") || game == QStringLiteral("quake2") || game == QStringLiteral("quake3"))) {
		return FileClass::Source;
	}
	if (sources.contains(ext)) {
		return FileClass::Source;
	}
	if (name == QStringLiteral("changelog.md") || name == QStringLiteral("release_notes.md")) {
		return FileClass::Document;
	}
	return FileClass::Content;
}

QString roleForPath(const QString& virtualPath)
{
	const QString lower = virtualPath.toLower();
	const QString ext = suffixOf(lower);
	const QString name = QFileInfo(lower).fileName();
	if (lower.startsWith(QStringLiteral("maps/")) && ext == QStringLiteral("bsp")) {
		return QStringLiteral("map");
	}
	if (lower.startsWith(QStringLiteral("maps/"))
		&& (ext == QStringLiteral("lit") || ext == QStringLiteral("lux") || ext == QStringLiteral("aas") || ext == QStringLiteral("vis")
			|| name.startsWith(QStringLiteral("lm_")))) {
		return QStringLiteral("map-companion");
	}
	if (lower.startsWith(QStringLiteral("levelshots/")) || ext == QStringLiteral("arena") || name == QStringLiteral("arenas.txt")) {
		return QStringLiteral("map-companion");
	}
	if (ext == QStringLiteral("map")) {
		return QStringLiteral("map-source");
	}
	if (ext == QStringLiteral("shader") || ext == QStringLiteral("mtr")) {
		return QStringLiteral("shader");
	}
	if (ext == QStringLiteral("skin")) {
		return QStringLiteral("skin");
	}
	if (lower.startsWith(QStringLiteral("music/"))) {
		return QStringLiteral("music");
	}
	if (lower.startsWith(QStringLiteral("sound/")) || lower.startsWith(QStringLiteral("sounds/"))) {
		return QStringLiteral("sound");
	}
	if (lower.startsWith(QStringLiteral("models/")) || lower.startsWith(QStringLiteral("progs/"))) {
		static const QSet<QString> images {QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("pcx"), QStringLiteral("dds")};
		return images.contains(ext) ? QStringLiteral("texture") : QStringLiteral("model");
	}
	if (lower.startsWith(QStringLiteral("textures/")) || lower.startsWith(QStringLiteral("env/")) || lower.startsWith(QStringLiteral("gfx/"))
		|| lower.startsWith(QStringLiteral("flats/")) || lower.startsWith(QStringLiteral("patches/")) || lower.startsWith(QStringLiteral("sprites/"))
		|| lower.startsWith(QStringLiteral("pics/")) || ext == QStringLiteral("wal")) {
		return QStringLiteral("texture");
	}
	if (name == QStringLiteral("progs.dat") || name == QStringLiteral("qwprogs.dat") || name == QStringLiteral("csprogs.dat")
		|| ext == QStringLiteral("qvm") || ext == QStringLiteral("dll") || ext == QStringLiteral("so") || ext == QStringLiteral("dylib")) {
		return QStringLiteral("code");
	}
	if (ext == QStringLiteral("cfg") || ext == QStringLiteral("rc") || ext == QStringLiteral("menu") || ext == QStringLiteral("deh")
		|| ext == QStringLiteral("bex") || ext == QStringLiteral("def") || ext == QStringLiteral("ent") || lower.startsWith(QStringLiteral("scripts/"))
		|| lower.startsWith(QStringLiteral("ui/"))) {
		return QStringLiteral("script");
	}
	return QStringLiteral("content");
}

QString roleForDependency(const LevelDependency& dependency)
{
	const QString path = dependency.resolvedPath.toLower();
	if (path.endsWith(QStringLiteral(".shader"))) {
		return QStringLiteral("shader");
	}
	if (path.endsWith(QStringLiteral(".skin"))) {
		return QStringLiteral("skin");
	}
	if (dependency.kind == QStringLiteral("model")) {
		return QStringLiteral("model");
	}
	if (dependency.kind == QStringLiteral("sound")) {
		return path.startsWith(QStringLiteral("music/")) ? QStringLiteral("music") : QStringLiteral("sound");
	}
	return QStringLiteral("texture");
}

bool isImageExtension(const QString& ext)
{
	static const QSet<QString> images {
		QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("pcx"), QStringLiteral("wal"),
		QStringLiteral("dds"), QStringLiteral("bmp"), QStringLiteral("ftx"), QStringLiteral("swl"), QStringLiteral("m8"), QStringLiteral("m32"),
		QStringLiteral("lmp"), QStringLiteral("mip"),
	};
	return images.contains(ext);
}

bool isModelExtension(const QString& ext)
{
	static const QSet<QString> models {
		QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"), QStringLiteral("mdr"), QStringLiteral("iqm"),
		QStringLiteral("md5mesh"), QStringLiteral("md5anim"), QStringLiteral("ase"), QStringLiteral("lwo"), QStringLiteral("spr"), QStringLiteral("sp2"),
		QStringLiteral("glm"), QStringLiteral("gla"), QStringLiteral("mds"), QStringLiteral("mdm"), QStringLiteral("mdx"), QStringLiteral("fm"),
		QStringLiteral("kvx"), QStringLiteral("obj"),
	};
	return models.contains(ext);
}

QString worldspawnValue(const LevelMapDocument& document, const QString& key)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
			continue;
		}
		for (const LevelMapProperty& property : entity.properties) {
			if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
				return property.value.trimmed();
			}
		}
	}
	return {};
}

// The compiled BSP for a map source: beside it, then the project's maps
// folder, then the output folder, then registered compiler outputs.
QString compiledMapFor(const ProjectManifest& manifest, const QString& projectRoot, const QString& mapPath)
{
	const QFileInfo info(mapPath);
	const QString bspName = info.completeBaseName() + QStringLiteral(".bsp");
	QStringList candidates {info.absoluteDir().filePath(bspName)};
	if (!projectRoot.isEmpty()) {
		candidates << QDir(projectRoot).filePath(QStringLiteral("maps/") + bspName);
		const QString output = manifest.outputFolder.trimmed().isEmpty() ? QStringLiteral("build") : manifest.outputFolder.trimmed();
		const QString outputPath = QDir::isAbsolutePath(output) ? output : QDir(projectRoot).filePath(output);
		candidates << QDir(outputPath).filePath(bspName) << QDir(outputPath).filePath(QStringLiteral("maps/") + bspName);
		for (const QString& registered : manifest.registeredOutputPaths) {
			if (QFileInfo(registered).fileName().compare(bspName, Qt::CaseInsensitive) == 0) {
				candidates << (QDir::isAbsolutePath(registered) ? registered : QDir(projectRoot).filePath(registered));
			}
		}
	}
	for (const QString& candidate : std::as_const(candidates)) {
		if (QFileInfo(candidate).isFile()) {
			return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
		}
	}
	return {};
}

QString gameTargetForBspFamily(BspFamily family)
{
	switch (family) {
	case BspFamily::Quake:
		return QStringLiteral("quake");
	case BspFamily::Quake2:
		return QStringLiteral("quake2");
	case BspFamily::Quake3:
		return QStringLiteral("quake3");
	case BspFamily::Unknown:
		break;
	}
	return {};
}

// A document holding what a compiled map names: its entities, and one face
// per texture for Quake II and III (Quake compiles textures into the BSP).
LevelMapDocument documentFromBsp(const BspInspection& bsp, const QString& mapName)
{
	LevelMapDocument document;
	document.sourcePath = bsp.sourcePath;
	document.mapName = mapName;
	document.format = bsp.family == BspFamily::Quake3 ? LevelMapFormat::Quake3Map : LevelMapFormat::QuakeMap;
	document.engineFamily = bsp.family == BspFamily::Quake3 ? QStringLiteral("idTech3") : QStringLiteral("idTech2");
	int id = 0;
	for (const BspEntitySummary& summary : bsp.entities) {
		LevelMapEntity entity;
		entity.id = id++;
		entity.className = summary.className;
		for (const BspEntityKeyValue& pair : summary.properties) {
			entity.properties.push_back({pair.key, pair.value, 0});
		}
		document.entities.push_back(entity);
	}
	if (bsp.family != BspFamily::Quake) {
		int brushId = 0;
		for (const BspTextureSummary& texture : bsp.textures) {
			if (texture.name.trimmed().isEmpty()) {
				continue;
			}
			LevelMapBrush brush;
			brush.id = brushId++;
			brush.entityId = 0;
			LevelMapBrushFace face;
			face.id = 0;
			face.textureName = texture.name;
			brush.faces.push_back(face);
			brush.faceCount = 1;
			brush.textureNames = {texture.name};
			document.brushes.push_back(brush);
		}
	}
	return document;
}

class Planner {
public:
	explicit Planner(const ReleaseRequest& request) : m_request(request), m_stock(request.stock.get())
	{
		m_plan.gameKey = normalizedGameKey(request.gameKey);
		m_plan.scope = request.scope;
		m_plan.items = request.items;
		m_plan.release = request.release;
		m_plan.plannedUtc = QDateTime::currentDateTimeUtc();
		m_plan.stockChecked = m_stock != nullptr;
		m_projectRoot = request.manifest.rootPath.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(request.manifest.rootPath).absoluteFilePath());
	}

	ReleasePlan run()
	{
		if (!chooseFormat()) {
			return finish();
		}
		if (!loadContent()) {
			return finish();
		}
		switch (m_request.scope) {
		case ReleaseScope::Project:
			planProject();
			break;
		case ReleaseScope::Maps:
			for (int i = 0; i < m_request.items.size() && !m_plan.cancelled; ++i) {
				tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking maps"), i, int(m_request.items.size()));
				planMapItem(m_request.items[i]);
			}
			break;
		case ReleaseScope::Models:
			for (int i = 0; i < m_request.items.size() && !m_plan.cancelled; ++i) {
				tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking models"), i, int(m_request.items.size()));
				planModelItem(m_request.items[i]);
			}
			break;
		case ReleaseScope::Textures:
			planTextures();
			break;
		}
		if (m_request.scope != ReleaseScope::Project && m_request.items.isEmpty()) {
			addProblem(QStringLiteral("empty"), QString(), QCoreApplication::translate("VibeStudioReleasePlan", "Choose at least one item to release."), {}, true);
		}
		return finish();
	}

private:
	bool tick(const QString& phase, int completed, int total)
	{
		if (m_plan.cancelled) {
			return false;
		}
		if (m_request.progress && !m_request.progress(phase, completed, total)) {
			m_plan.cancelled = true;
			m_plan.complete = false;
			return false;
		}
		return true;
	}

	bool lumpMode() const { return m_plan.formatId == QStringLiteral("wad"); }

	bool chooseFormat()
	{
		QString format = m_request.release.packageFormat.trimmed().toLower();
		if (format.isEmpty()) {
			format = defaultReleaseFormatId(m_plan.gameKey, m_request.scope);
		}
		static const QStringList known {QStringLiteral("pk3"), QStringLiteral("pak"), QStringLiteral("wad"), QStringLiteral("zip")};
		if (!known.contains(format)) {
			addProblem(QStringLiteral("format"), format, QCoreApplication::translate("VibeStudioReleasePlan", "Unknown package format %1. Choose pk3, pak, wad or zip.").arg(format), {}, true);
			return false;
		}
		const bool doomFamily = m_plan.gameKey == QStringLiteral("doom") || m_plan.gameKey == QStringLiteral("heretic-hexen");
		if (format == QStringLiteral("wad") && !doomFamily) {
			addProblem(QStringLiteral("format"), format, QCoreApplication::translate("VibeStudioReleasePlan", "Only Doom-family games load WAD packages. Choose pk3, pak or zip."), {}, true);
			return false;
		}
		if (format == QStringLiteral("pak") && (m_plan.gameKey == QStringLiteral("quake3") || doomFamily)) {
			addProblem(QStringLiteral("format"), format, QCoreApplication::translate("VibeStudioReleasePlan", "%1 does not load PAK packages.").arg(gameDefinitionForKey(m_plan.gameKey).displayName), {}, true);
			return false;
		}
		m_plan.formatId = format;
		m_plan.format = format == QStringLiteral("pk3") ? PackageArchiveFormat::Pk3 : format == QStringLiteral("pak") ? PackageArchiveFormat::Pak
			: format == QStringLiteral("wad") ? PackageArchiveFormat::Wad : PackageArchiveFormat::Zip;
		const QString packageName = m_request.release.packageName.isEmpty() ? QStringLiteral("release") : m_request.release.packageName;
		m_plan.packageFolder = m_request.release.gameFolder;
		const QString baseFolder = gameDefinitionForKey(m_plan.gameKey).baseGameDirectory;
		if (format == QStringLiteral("pak")) {
			// Quake and Quake II load only numbered packs from a game folder.
			if (!m_plan.packageFolder.isEmpty() && m_plan.packageFolder.compare(baseFolder, Qt::CaseInsensitive) != 0) {
				m_plan.packageFileName = QStringLiteral("pak0.pak");
			} else {
				m_plan.packageFileName = packageName + QStringLiteral(".pak");
				m_plan.warnings << QCoreApplication::translate("VibeStudioReleasePlan", "%1 loads only numbered packs (pak0.pak, pak1.pak...). Players must rename %2 to a free number, which can clash with other releases. Release into a mod folder, or as loose files (zip), instead.")
										.arg(gameDefinitionForKey(m_plan.gameKey).displayName, m_plan.packageFileName);
			}
		} else {
			m_plan.packageFileName = packageName + QLatin1Char('.') + format;
		}
		return true;
	}

	bool loadContent()
	{
		QVector<ProjectContentRoot> roots;
		if (!m_projectRoot.isEmpty()) {
			roots = projectContentRoots(m_request.manifest);
		} else {
			// A loose map or model: the folder above maps/ (or models/...)
			// plays the project's part.
			for (const QString& item : m_request.items) {
				const QFileInfo info(item);
				QDir dir = info.isDir() ? QDir(info.absoluteFilePath()) : info.absoluteDir();
				if (dir.dirName().compare(QStringLiteral("maps"), Qt::CaseInsensitive) == 0) {
					dir.cdUp();
				}
				m_projectRoot = QDir::cleanPath(dir.absolutePath());
				break;
			}
			if (!m_projectRoot.isEmpty()) {
				ProjectContentRoot root;
				root.path = m_projectRoot;
				root.layerId = QStringLiteral("project");
				root.label = QFileInfo(m_projectRoot).fileName();
				roots << root;
			}
		}
		tick(QCoreApplication::translate("VibeStudioReleasePlan", "Reading the project"), 0, 1);
		auto content = std::make_shared<ProjectContentReader>();
		ProjectContentOptions options = m_request.contentOptions;
		const auto previous = options.isCancelled;
		options.isCancelled = [this, previous]() {
			return m_plan.cancelled || (previous && previous());
		};
		QString error;
		if (!content->load(roots, &error, options)) {
			if (!m_plan.cancelled) {
				addProblem(QStringLiteral("incomplete"), QString(), error, {}, true);
			}
			m_plan.complete = false;
			return false;
		}
		for (const QString& warning : content->warnings()) {
			appendUnique(&m_plan.warnings, warning);
		}
		m_content = content;
		QVector<LayeredPackageReader::Layer> layers;
		layers << LayeredPackageReader::Layer {QStringLiteral("project"), QFileInfo(m_projectRoot).fileName(), content};
		for (const auto& layer : m_request.extraLayers) {
			layers << layer;
		}
		m_reader = std::make_shared<LayeredPackageReader>(layers);
		m_plan.reader = m_reader;
		return true;
	}

	QString absoluteItemPath(const QString& item) const
	{
		const QFileInfo info(item.trimmed());
		if (info.isAbsolute()) {
			return QDir::cleanPath(info.absoluteFilePath());
		}
		if (!m_projectRoot.isEmpty() && QFileInfo::exists(QDir(m_projectRoot).absoluteFilePath(item))) {
			return QDir::cleanPath(QDir(m_projectRoot).absoluteFilePath(item));
		}
		return QDir::cleanPath(info.absoluteFilePath());
	}

	// The virtual path of a project file on disk, or empty when it is outside
	// every content root.
	QString virtualPathFor(const QString& absolutePath) const
	{
		if (!m_content) {
			return {};
		}
		for (const ProjectContentRoot& root : m_content->roots()) {
			const QString relative = QDir(root.path).relativeFilePath(absolutePath);
			if (!relative.startsWith(QStringLiteral("..")) && !QDir::isAbsolutePath(relative)) {
				const qsizetype index = m_content->indexOf(relative);
				if (index >= 0 && QDir::cleanPath(m_content->absolutePath(index)).compare(QDir::cleanPath(absolutePath), Qt::CaseInsensitive) == 0) {
					return m_content->entries().at(index).virtualPath;
				}
			}
		}
		return {};
	}

	void addProblem(const QString& kind, const QString& reference, const QString& message, const QStringList& requiredBy, bool blocking)
	{
		const QString key = kind + QLatin1Char('|') + folded(reference) + QLatin1Char('|') + (reference.isEmpty() ? message : QString());
		const auto existing = m_problemIndex.constFind(key);
		if (existing != m_problemIndex.cend()) {
			ReleaseProblem& problem = m_plan.problems[*existing];
			for (const QString& item : requiredBy) {
				appendUnique(&problem.requiredBy, item);
			}
			problem.blocking = problem.blocking || blocking;
			return;
		}
		m_problemIndex.insert(key, int(m_plan.problems.size()));
		m_plan.problems.push_back({kind, reference, message, requiredBy, blocking});
	}

	void addStockReference(const QString& kind, const QString& reference, const QString& path, const QString& source, const QStringList& requiredBy, bool identicalCopy)
	{
		const QString key = folded(reference) + QLatin1Char('|') + folded(path);
		const auto existing = m_stockIndex.constFind(key);
		if (existing != m_stockIndex.cend()) {
			ReleaseReference& stock = m_plan.stock[*existing];
			for (const QString& item : requiredBy) {
				appendUnique(&stock.requiredBy, item);
			}
			return;
		}
		m_stockIndex.insert(key, int(m_plan.stock.size()));
		m_plan.stock.push_back({kind, reference, path, source, requiredBy, identicalCopy});
	}

	// CRC-32 of a reader entry: the ZIP directory's when it has one, else streamed.
	bool entryCrc(qsizetype readerIndex, quint32* crc, QString* error) const
	{
		const PackageEntry& entry = m_reader->entries().at(readerIndex);
		if (entry.hasCrc32) {
			*crc = entry.crc32;
			return true;
		}
		const int layer = m_reader->layerOf(readerIndex);
		if (layer == 0) {
			return fileCrc32(m_content->absolutePath(m_reader->layerIndexOf(readerIndex)), crc, error, [this]() { return m_plan.cancelled; });
		}
		quint32 value = 0;
		const bool streamed = m_reader->streamEntryAt(readerIndex, [&value](QByteArrayView chunk) {
			value = crc32View(chunk, value);
			return true;
		}, error);
		*crc = value;
		return streamed;
	}

	// Compares a project file with the game's file at the same path.
	GameAssetMatch stockMatch(qsizetype readerIndex, QString* stockSource, QString* error) const
	{
		if (!m_stock) {
			return GameAssetMatch::None;
		}
		const PackageEntry& entry = m_reader->entries().at(readerIndex);
		const GameAssetRegisterFile* file = m_stock->file(entry.virtualPath);
		if (!file) {
			return GameAssetMatch::None;
		}
		if (stockSource) {
			*stockSource = m_stock->sourceLabel(file->source);
		}
		if (file->sizeBytes != entry.sizeBytes) {
			return GameAssetMatch::Different;
		}
		quint32 crc = 0;
		if (!entryCrc(readerIndex, &crc, error)) {
			// Unreadable now: treat as the project's own, so it is not silently dropped.
			return GameAssetMatch::Different;
		}
		return crc == file->crc32 ? GameAssetMatch::Identical : GameAssetMatch::Different;
	}

	// Adds a reader entry, unless the game already ships the same file.
	// Returns the entry index, or -1 when it was left out.
	int addReaderEntry(qsizetype readerIndex, const QString& role, const QString& requiredBy, const QString& note = {})
	{
		if (readerIndex < 0 || readerIndex >= m_reader->entries().size()) {
			return -1;
		}
		const PackageEntry& source = m_reader->entries().at(readerIndex);
		const QString virtualPath = source.virtualPath;
		const QString key = folded(virtualPath);
		const auto existing = m_entryIndex.constFind(key);
		if (existing != m_entryIndex.cend()) {
			if (*existing >= 0) {
				appendUnique(&m_plan.entries[*existing].requiredBy, requiredBy);
			} else {
				for (ReleaseReference& stock : m_plan.stock) {
					if (stock.identicalCopy && folded(stock.path) == key) {
						appendUnique(&stock.requiredBy, requiredBy);
					}
				}
			}
			return *existing;
		}
		QString stockSource;
		QString error;
		const GameAssetMatch match = stockMatch(readerIndex, &stockSource, &error);
		if (match == GameAssetMatch::Identical) {
			m_entryIndex.insert(key, -1);
			addStockReference(role, virtualPath, virtualPath, stockSource, requiredBy.isEmpty() ? QStringList() : QStringList {requiredBy}, true);
			return -1;
		}
		ReleaseEntry entry;
		entry.virtualPath = virtualPath;
		entry.role = role;
		entry.sizeBytes = source.sizeBytes;
		const int layer = m_reader->layerOf(readerIndex);
		if (layer == 0) {
			entry.sourcePath = m_content->absolutePath(m_reader->layerIndexOf(readerIndex));
		} else {
			entry.layer = layer;
			entry.layerIndex = m_reader->layerIndexOf(readerIndex);
			if (const auto* info = m_reader->layer(layer)) {
				entry.note = QCoreApplication::translate("VibeStudioReleasePlan", "From %1").arg(info->label);
			}
		}
		if (!requiredBy.isEmpty()) {
			entry.requiredBy << requiredBy;
		}
		if (match == GameAssetMatch::Different) {
			entry.replacesStock = true;
			entry.stockSource = stockSource;
		}
		if (!note.isEmpty()) {
			entry.note = entry.note.isEmpty() ? note : entry.note + QStringLiteral("; ") + note;
		}
		entry.loose = role == QStringLiteral("code") && QStringList {QStringLiteral("dll"), QStringLiteral("so"), QStringLiteral("dylib")}.contains(suffixOf(virtualPath));
		m_entryIndex.insert(key, int(m_plan.entries.size()));
		m_plan.entries.push_back(entry);
		return int(m_plan.entries.size() - 1);
	}

	int addDiskEntry(const QString& absolutePath, const QString& virtualPath, const QString& role, const QString& requiredBy, const QString& note = {})
	{
		const QString inProject = virtualPathFor(absolutePath);
		if (!inProject.isEmpty() && inProject.compare(virtualPath, Qt::CaseInsensitive) == 0) {
			return addReaderEntry(m_reader->indexOf(inProject), role, requiredBy, note);
		}
		const QString key = folded(virtualPath);
		const auto existing = m_entryIndex.constFind(key);
		if (existing != m_entryIndex.cend()) {
			if (*existing >= 0) {
				appendUnique(&m_plan.entries[*existing].requiredBy, requiredBy);
			}
			return *existing;
		}
		const QFileInfo info(absolutePath);
		ReleaseEntry entry;
		entry.virtualPath = virtualPath;
		entry.role = role;
		entry.sourcePath = QDir::cleanPath(info.absoluteFilePath());
		entry.sizeBytes = quint64(info.size());
		if (!requiredBy.isEmpty()) {
			entry.requiredBy << requiredBy;
		}
		entry.note = note;
		m_entryIndex.insert(key, int(m_plan.entries.size()));
		m_plan.entries.push_back(entry);
		return int(m_plan.entries.size() - 1);
	}

	// Takes a dependency report into the plan: project files ship, the game's
	// own stay out, anything else is a problem.
	void absorbDependencies(const LevelDependencyReport& report, const QString& requiredBy, bool skipTextures)
	{
		for (const LevelDependency& dependency : report.dependencies) {
			if (skipTextures && dependency.kind == QStringLiteral("texture")) {
				continue;
			}
			switch (dependency.status) {
			case LevelDependencyStatus::Resolved: {
				const qsizetype index = m_reader->indexOf(dependency.resolvedPath);
				if (index < 0) {
					addProblem(QStringLiteral("unreadable"), dependency.reference,
						QCoreApplication::translate("VibeStudioReleasePlan", "%1 resolved to %2, which the project no longer lists.").arg(dependency.reference, dependency.resolvedPath), {requiredBy}, true);
					break;
				}
				addReaderEntry(index, roleForDependency(dependency), requiredBy);
				break;
			}
			case LevelDependencyStatus::Stock:
				addStockReference(dependency.kind, dependency.reference, dependency.resolvedPath, dependency.stockSource, {requiredBy}, false);
				break;
			case LevelDependencyStatus::Builtin:
				break;
			case LevelDependencyStatus::Missing:
				if (m_stock) {
					addProblem(QStringLiteral("missing"), dependency.reference,
						QCoreApplication::translate("VibeStudioReleasePlan", "%1 is neither in the project nor in the game's own files. Searched: %2")
							.arg(dependency.reference, dependency.candidates.join(QStringLiteral(", "))), {requiredBy}, true);
				} else {
					addProblem(QStringLiteral("unverified"), dependency.reference,
						QCoreApplication::translate("VibeStudioReleasePlan", "%1 is not in the project. It is assumed to come with the game; index the game's assets to check.")
							.arg(dependency.reference), {requiredBy}, false);
				}
				break;
			case LevelDependencyStatus::Ambiguous:
				addProblem(QStringLiteral("ambiguous"), dependency.reference,
					QCoreApplication::translate("VibeStudioReleasePlan", "%1 matches more than one file or shader declaration: %2").arg(dependency.reference, dependency.candidates.join(QStringLiteral(", "))), {requiredBy}, true);
				break;
			case LevelDependencyStatus::Unreadable:
				addProblem(QStringLiteral("unreadable"), dependency.reference,
					QCoreApplication::translate("VibeStudioReleasePlan", "%1 could not be read: %2").arg(dependency.reference, dependency.note), {requiredBy}, true);
				break;
			case LevelDependencyStatus::Unsafe:
				addProblem(QStringLiteral("unsafe"), dependency.reference,
					QCoreApplication::translate("VibeStudioReleasePlan", "%1 is not a safe package path.").arg(dependency.reference), {requiredBy}, true);
				break;
			}
		}
		if (report.cancelled) {
			m_plan.cancelled = true;
			m_plan.complete = false;
		} else if (!report.complete) {
			addProblem(QStringLiteral("incomplete"), requiredBy,
				QCoreApplication::translate("VibeStudioReleasePlan", "The dependency check for %1 could not finish, so a file it needs may be missing from the release: %2")
					.arg(requiredBy, report.warnings.isEmpty() ? QString() : report.warnings.first()), {requiredBy}, false);
		}
		appendUnique(&m_plan.limitations, QCoreApplication::translate("VibeStudioReleasePlan",
			"Checks what maps, models and shaders name directly: textures, shader images, models, skins, sounds and music. Files that game code chooses at run time are not followed; add them with the release's include patterns."));
	}

	QString findCompiledMap(const QString& mapPath) const
	{
		return compiledMapFor(m_request.manifest, m_projectRoot, mapPath);
	}

	// One of several names a game may load, from the project or the game.
	// Returns true when found in either.
	bool resolveOptional(const QStringList& candidates, const QString& role, const QString& kind, const QString& requiredBy)
	{
		for (const QString& candidate : candidates) {
			const qsizetype index = m_reader->indexOf(candidate);
			if (index >= 0) {
				addReaderEntry(index, role, requiredBy);
				return true;
			}
		}
		if (m_stock) {
			for (const QString& candidate : candidates) {
				if (const GameAssetRegisterFile* file = m_stock->file(candidate)) {
					addStockReference(kind, candidate, file->path, m_stock->sourceLabel(file->source), {requiredBy}, false);
					return true;
				}
			}
		}
		return false;
	}

	void planMapItem(const QString& item)
	{
		const QString path = absoluteItemPath(item);
		const QFileInfo info(path);
		if (!info.isFile()) {
			addProblem(QStringLiteral("missing"), item, QCoreApplication::translate("VibeStudioReleasePlan", "Map file not found: %1").arg(QDir::toNativeSeparators(path)), {}, true);
			return;
		}
		const QString ext = info.suffix().toLower();
		if (ext == QStringLiteral("wad")) {
			planDoomWad(path);
			return;
		}
		if (ext == QStringLiteral("bsp")) {
			planCompiledMap(path, QString());
			return;
		}
		planSourceMap(path);
	}

	void planSourceMap(const QString& path)
	{
		const QFileInfo info(path);
		const QString name = info.completeBaseName();
		LevelMapLoadRequest load;
		load.path = path;
		// The project's game settles what the text alone cannot: classic brush
		// lines read the same in Quake II and III maps.
		if (m_plan.gameKey == QStringLiteral("quake3")) {
			load.engineHint = QStringLiteral("idTech3");
		} else if (m_plan.gameKey == QStringLiteral("quake") || m_plan.gameKey == QStringLiteral("quake2")) {
			load.engineHint = QStringLiteral("idTech2");
		}
		load.isCancelled = [this]() { return m_plan.cancelled; };
		LevelMapDocument document;
		QString error;
		if (!loadLevelMap(load, &document, &error)) {
			addProblem(QStringLiteral("unreadable"), name, QCoreApplication::translate("VibeStudioReleasePlan", "Unable to read map %1: %2").arg(QDir::toNativeSeparators(path), error), {name}, true);
			return;
		}
		if (document.format == LevelMapFormat::DoomWad) {
			planDoomWad(path);
			return;
		}
		ReleaseMapInfo map;
		map.name = name;
		map.sourcePath = path;
		map.title = worldspawnValue(document, QStringLiteral("message"));
		map.formatId = levelMapFormatId(document.format);
		map.entityCount = int(document.entities.size());
		map.brushCount = int(document.brushes.size());
		QString target = levelBuildTargetForDocument(document);
		// Quake and Quake II share the MAP grammar; the project says which.
		if (document.format == LevelMapFormat::QuakeMap && (m_plan.gameKey == QStringLiteral("quake") || m_plan.gameKey == QStringLiteral("quake2"))) {
			target = m_plan.gameKey;
		}
		if (!target.isEmpty() && target != m_plan.gameKey && knownGameKeys().contains(m_plan.gameKey)) {
			appendUnique(&m_plan.warnings, QCoreApplication::translate("VibeStudioReleasePlan", "%1 is a %2 map, but the release targets %3.")
				.arg(name, gameDefinitionForKey(target).displayName, gameDefinitionForKey(m_plan.gameKey).displayName));
		}
		map.compiledPath = findCompiledMap(path);
		map.built = !map.compiledPath.isEmpty();
		if (map.built) {
			map.stale = QFileInfo(map.compiledPath).lastModified() < info.lastModified();
		}
		if (!map.built) {
			addProblem(QStringLiteral("unbuilt-map"), name, QCoreApplication::translate("VibeStudioReleasePlan", "%1 has not been built. Build it, then package again.").arg(name), {name}, true);
		} else if (map.stale) {
			addProblem(QStringLiteral("stale-map"), name, QCoreApplication::translate("VibeStudioReleasePlan", "%1 changed after it was last built. Rebuild it so the release matches the source.").arg(name), {name}, false);
		}
		m_plan.maps.push_back(map);
		if (map.built) {
			addCompiledMapFiles(map.compiledPath, name, target.isEmpty() ? m_plan.gameKey : target);
		}
		if (m_request.release.includeSources) {
			const QString virtualPath = virtualPathFor(path);
			addDiskEntry(path, virtualPath.isEmpty() ? QStringLiteral("maps/") + info.fileName() : virtualPath, QStringLiteral("map-source"), name);
		}
		LevelDependencyOptions options;
		options.buildTarget = target;
		options.stock = m_request.stock;
		options.quakeTexturesEmbedded = (target.isEmpty() ? m_plan.gameKey : target) == QStringLiteral("quake");
		const LevelDependencyReport report = inspectLevelDependencies(document, *m_reader, [this, name](int completed, int total) {
			return tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking %1").arg(name), completed, total);
		}, options);
		absorbDependencies(report, name, options.quakeTexturesEmbedded);
		planMapExtras(document, name, target.isEmpty() ? m_plan.gameKey : target);
	}

	void planCompiledMap(const QString& bspPath, const QString& sourceName)
	{
		const QString name = sourceName.isEmpty() ? QFileInfo(bspPath).completeBaseName() : sourceName;
		const BspInspection bsp = inspectBspFile(bspPath);
		if (!bsp.valid) {
			addProblem(QStringLiteral("unreadable"), name, QCoreApplication::translate("VibeStudioReleasePlan", "%1 is not a readable BSP: %2").arg(QDir::toNativeSeparators(bspPath), bsp.error), {name}, true);
			return;
		}
		ReleaseMapInfo map;
		map.name = name;
		map.compiledPath = bspPath;
		map.built = true;
		map.title = bsp.worldspawnMessage;
		map.formatId = bsp.familyId;
		map.entityCount = bsp.entityCount;
		m_plan.maps.push_back(map);
		const QString target = gameTargetForBspFamily(bsp.family);
		if (!target.isEmpty() && target != m_plan.gameKey && knownGameKeys().contains(m_plan.gameKey)) {
			appendUnique(&m_plan.warnings, QCoreApplication::translate("VibeStudioReleasePlan", "%1 is a %2 map, but the release targets %3.")
				.arg(name, gameDefinitionForKey(target).displayName, gameDefinitionForKey(m_plan.gameKey).displayName));
		}
		addCompiledMapFiles(bspPath, name, target.isEmpty() ? m_plan.gameKey : target);
		const LevelMapDocument document = documentFromBsp(bsp, name);
		LevelDependencyOptions options;
		options.buildTarget = target;
		options.stock = m_request.stock;
		options.quakeTexturesEmbedded = target == QStringLiteral("quake");
		const LevelDependencyReport report = inspectLevelDependencies(document, *m_reader, [this, name](int completed, int total) {
			return tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking %1").arg(name), completed, total);
		}, options);
		absorbDependencies(report, name, options.quakeTexturesEmbedded);
		planMapExtras(document, name, target.isEmpty() ? m_plan.gameKey : target);
	}

	// The BSP and the files the engine loads next to it.
	void addCompiledMapFiles(const QString& bspPath, const QString& name, const QString& target)
	{
		const QFileInfo bsp(bspPath);
		const QDir dir = bsp.absoluteDir();
		addDiskEntry(bspPath, QStringLiteral("maps/") + name + QStringLiteral(".bsp"), QStringLiteral("map"), name);
		QStringList companions;
		if (target == QStringLiteral("quake")) {
			companions << QStringLiteral(".lit") << QStringLiteral(".lux");
		} else if (target == QStringLiteral("quake3")) {
			companions << QStringLiteral(".aas");
		}
		for (const QString& suffix : std::as_const(companions)) {
			const QString companion = dir.filePath(name + suffix);
			if (QFileInfo(companion).isFile()) {
				addDiskEntry(companion, QStringLiteral("maps/") + name + suffix, QStringLiteral("map-companion"), name);
			} else if (const qsizetype index = m_reader->indexOf(QStringLiteral("maps/") + name + suffix); index >= 0) {
				addReaderEntry(index, QStringLiteral("map-companion"), name);
			}
		}
		if (target != QStringLiteral("quake3")) {
			return;
		}
		// q3map2 external lightmaps: maps/<name>/lm_NNNN.tga beside the BSP.
		const QDir lightmaps(dir.filePath(name));
		if (lightmaps.exists()) {
			for (const QFileInfo& file : lightmaps.entryInfoList({QStringLiteral("lm_*")}, QDir::Files, QDir::Name)) {
				addDiskEntry(file.absoluteFilePath(), QStringLiteral("maps/%1/%2").arg(name, file.fileName()), QStringLiteral("map-companion"), name);
			}
		}
		// The shader q3map2 generates for this map, in the project or beside the build.
		const QString generated = QStringLiteral("scripts/q3map2_%1.shader").arg(name);
		if (const qsizetype index = m_reader->indexOf(generated); index >= 0) {
			addReaderEntry(index, QStringLiteral("shader"), name);
		} else {
			QDir up = dir;
			if (up.cdUp() && QFileInfo(up.filePath(generated)).isFile()) {
				addDiskEntry(up.filePath(generated), generated, QStringLiteral("shader"), name);
			}
		}
	}

	void planMapExtras(const LevelMapDocument& document, const QString& name, const QString& target)
	{
		static const QStringList skyFaces {QStringLiteral("rt"), QStringLiteral("bk"), QStringLiteral("lf"), QStringLiteral("ft"), QStringLiteral("up"), QStringLiteral("dn")};
		if (target == QStringLiteral("quake3")) {
			// Menus show a levelshot; bots and the arena list read the .arena script.
			if (!resolveOptional({QStringLiteral("levelshots/%1.jpg").arg(name), QStringLiteral("levelshots/%1.tga").arg(name), QStringLiteral("levelshots/%1.png").arg(name)},
					QStringLiteral("map-companion"), QStringLiteral("levelshot"), name)) {
				appendUnique(&m_plan.warnings, QCoreApplication::translate("VibeStudioReleasePlan", "%1 has no levelshot (levelshots/%1.jpg); the map menu will show a placeholder.").arg(name));
			}
			if (!resolveOptional({QStringLiteral("scripts/%1.arena").arg(name)}, QStringLiteral("map-companion"), QStringLiteral("arena"), name)) {
				if (const qsizetype index = m_reader->indexOf(QStringLiteral("scripts/arenas.txt")); index >= 0) {
					addReaderEntry(index, QStringLiteral("map-companion"), name);
				} else {
					appendUnique(&m_plan.warnings, QCoreApplication::translate("VibeStudioReleasePlan", "%1 has no scripts/%1.arena, so it will not appear in the game's map list or for bots.").arg(name));
				}
			}
			return;
		}
		if (target != QStringLiteral("quake") && target != QStringLiteral("quake2")) {
			return;
		}
		// Sky boxes named by worldspawn: Quake II env/, and Quake source ports' gfx/env/.
		const QString sky = worldspawnValue(document, QStringLiteral("sky"));
		if (!sky.isEmpty()) {
			const QString folder = target == QStringLiteral("quake2") ? QStringLiteral("env/") : QStringLiteral("gfx/env/");
			int found = 0;
			for (const QString& face : skyFaces) {
				QStringList candidates {folder + sky + face + QStringLiteral(".tga"), folder + sky + face + QStringLiteral(".png")};
				if (target == QStringLiteral("quake2")) {
					candidates << folder + sky + face + QStringLiteral(".pcx");
				}
				found += resolveOptional(candidates, QStringLiteral("texture"), QStringLiteral("sky"), name) ? 1 : 0;
			}
			if (found < skyFaces.size()) {
				appendUnique(&m_plan.warnings, QCoreApplication::translate("VibeStudioReleasePlan", "%1 names the sky %2, but only %3 of its 6 images were found in the project or the game.").arg(name, sky).arg(found));
			}
		}
		// A CD track number plays music/trackNN when a port finds the file.
		bool ok = false;
		const int track = worldspawnValue(document, QStringLiteral("sounds")).toInt(&ok);
		if (ok && track > 1) {
			const QString stem = QStringLiteral("music/track%1").arg(track, 2, 10, QLatin1Char('0'));
			resolveOptional({stem + QStringLiteral(".ogg"), stem + QStringLiteral(".mp3"), stem + QStringLiteral(".wav"), stem + QStringLiteral(".flac")},
				QStringLiteral("music"), QStringLiteral("music"), name);
		}
	}

	// Doom maps live in WADs. A WAD release ships the map WAD's own lumps and
	// those of every project WAD that provides a texture or flat it uses, less
	// lumps identical to the IWAD's.
	void planDoomWad(const QString& path)
	{
		const QString label = QFileInfo(path).fileName();
		QString error;
		const QStringList mapNames = levelMapNamesInWad(path, &error);
		if (mapNames.isEmpty()) {
			addProblem(QStringLiteral("unreadable"), label, error.isEmpty() ? QCoreApplication::translate("VibeStudioReleasePlan", "%1 holds no maps.").arg(label) : error, {label}, true);
			return;
		}
		ReleaseMapInfo map;
		map.name = QFileInfo(path).completeBaseName();
		map.sourcePath = path;
		map.compiledPath = path;
		map.built = true;
		map.formatId = QStringLiteral("doom-wad");
		map.doomMaps = mapNames;
		m_plan.maps.push_back(map);
		// Every Doom-family WAD in the project is a candidate resource; the map's
		// own goes last so it shadows the others, as the engine loads PWADs.
		QVector<LayeredPackageReader::Layer> layers;
		QHash<QString, QString> layerFiles;
		for (const PackageEntry& entry : m_content->entries()) {
			if (suffixOf(entry.virtualPath) != QStringLiteral("wad")) {
				continue;
			}
			const QString absolute = m_content->absolutePathFor(entry.virtualPath);
			if (QDir::cleanPath(absolute).compare(QDir::cleanPath(path), Qt::CaseInsensitive) == 0) {
				continue;
			}
			auto archive = std::make_shared<PackageArchive>();
			if (!archive->load(absolute) || (archive->wadMagic() != QStringLiteral("PWAD") && archive->wadMagic() != QStringLiteral("IWAD"))) {
				continue;
			}
			layers << LayeredPackageReader::Layer {entry.virtualPath, entry.virtualPath, archive};
			layerFiles.insert(entry.virtualPath, absolute);
		}
		auto mapArchive = std::make_shared<PackageArchive>();
		if (!mapArchive->load(path, &error)) {
			addProblem(QStringLiteral("unreadable"), label, error, {label}, true);
			return;
		}
		const QString mapLayer = virtualPathFor(path).isEmpty() ? label : virtualPathFor(path);
		layers << LayeredPackageReader::Layer {mapLayer, mapLayer, mapArchive};
		layerFiles.insert(mapLayer, path);
		const LayeredPackageReader resources(layers);
		QSet<QString> neededWads {mapLayer};
		for (const QString& mapName : mapNames) {
			if (!tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking %1").arg(mapName), 0, 1)) {
				return;
			}
			LevelMapLoadRequest load;
			load.path = path;
			load.mapName = mapName;
			load.isCancelled = [this]() { return m_plan.cancelled; };
			LevelMapDocument document;
			if (!loadLevelMap(load, &document, &error)) {
				addProblem(QStringLiteral("unreadable"), mapName, QCoreApplication::translate("VibeStudioReleasePlan", "Unable to read %1 in %2: %3").arg(mapName, label, error), {mapName}, true);
				continue;
			}
			LevelDependencyOptions options;
			options.stock = m_request.stock;
			const LevelDependencyReport report = inspectLevelDependencies(document, resources, [this, mapName](int completed, int total) {
				return tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking %1").arg(mapName), completed, total);
			}, options);
			for (const LevelDependency& dependency : report.dependencies) {
				if (dependency.kind == QStringLiteral("doom-input")) {
					if (!dependency.sourceLayer.isEmpty() && layerFiles.contains(dependency.sourceLayer)) {
						neededWads.insert(dependency.sourceLayer);
					}
					continue;
				}
				switch (dependency.status) {
				case LevelDependencyStatus::Stock:
					addStockReference(dependency.kind, dependency.reference, dependency.resolvedPath, dependency.stockSource, {mapName}, false);
					break;
				case LevelDependencyStatus::Resolved:
				case LevelDependencyStatus::Builtin:
					if (!dependency.sourceLayer.isEmpty() && layerFiles.contains(dependency.sourceLayer)) {
						neededWads.insert(dependency.sourceLayer);
					}
					break;
				case LevelDependencyStatus::Missing:
				case LevelDependencyStatus::Unreadable:
				case LevelDependencyStatus::Ambiguous:
				case LevelDependencyStatus::Unsafe:
					if (m_stock) {
						addProblem(QStringLiteral("missing"), dependency.reference,
							QCoreApplication::translate("VibeStudioReleasePlan", "%1 is neither defined by the project's WADs nor by the IWAD: %2").arg(dependency.reference, dependency.note), {mapName}, true);
					} else {
						addProblem(QStringLiteral("unverified"), dependency.reference,
							QCoreApplication::translate("VibeStudioReleasePlan", "%1 is not defined by the project's WADs. It is assumed to come with the IWAD; index the game's assets to check.").arg(dependency.reference), {mapName}, false);
					}
					break;
				}
			}
			if (report.cancelled) {
				m_plan.cancelled = true;
				m_plan.complete = false;
				return;
			}
		}
		// Ship the needed WADs: as lumps in a WAD release, as files otherwise.
		QStringList ordered = QStringList(neededWads.cbegin(), neededWads.cend());
		std::sort(ordered.begin(), ordered.end(), [&](const QString& left, const QString& right) {
			// Resources before the map WAD, matching the layer order above.
			if (left == mapLayer || right == mapLayer) { return right == mapLayer && left != mapLayer; }
			return left.compare(right, Qt::CaseInsensitive) < 0;
		});
		for (const QString& wad : std::as_const(ordered)) {
			if (lumpMode()) {
				addWadLumps(layerFiles.value(wad), wad, map.name);
			} else {
				const QString virtualPath = virtualPathFor(layerFiles.value(wad));
				addDiskEntry(layerFiles.value(wad), virtualPath.isEmpty() ? QFileInfo(layerFiles.value(wad)).fileName() : virtualPath, QStringLiteral("map"), map.name);
			}
		}
	}

	// A WAD's lumps in directory order, less those identical to the IWAD's.
	// Map lumps and namespace markers always stay, so groups keep their shape.
	void addWadLumps(const QString& path, const QString& label, const QString& requiredBy)
	{
		const QString key = folded(QStringLiteral("wad:") + path);
		if (m_wadsAdded.contains(key)) {
			return;
		}
		m_wadsAdded.insert(key);
		auto archive = std::make_shared<PackageArchive>();
		QString error;
		if (!archive->load(path, &error)) {
			addProblem(QStringLiteral("unreadable"), label, error, {requiredBy}, true);
			return;
		}
		const int lumpSource = int(m_plan.lumpSources.size());
		m_plan.lumpSources << archive;
		const QVector<PackageEntry> entries = archive->entries();
		// Listings sort by name; the WAD's own order is its source ordinals,
		// and the release keeps that order so namespaces and maps hold together.
		QVector<qsizetype> order(entries.size());
		std::iota(order.begin(), order.end(), qsizetype(0));
		std::stable_sort(order.begin(), order.end(), [&entries](qsizetype left, qsizetype right) {
			return entries[left].sourceOrdinal < entries[right].sourceOrdinal;
		});
		// A map group is its marker followed by the map lumps, through ENDMAP
		// for UDMF; GL node groups (GL_xxxx) follow the same shape.
		static const QSet<QString> mapLumpNames {
			QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"), QStringLiteral("VERTEXES"), QStringLiteral("SEGS"),
			QStringLiteral("SSECTORS"), QStringLiteral("NODES"), QStringLiteral("SECTORS"), QStringLiteral("REJECT"), QStringLiteral("BLOCKMAP"),
			QStringLiteral("BEHAVIOR"), QStringLiteral("SCRIPTS"), QStringLiteral("TEXTMAP"), QStringLiteral("ZNODES"), QStringLiteral("DIALOGUE"),
			QStringLiteral("ENDMAP"), QStringLiteral("GL_VERT"), QStringLiteral("GL_SEGS"), QStringLiteral("GL_SSECT"), QStringLiteral("GL_NODES"),
			QStringLiteral("GL_PVS"),
		};
		QSet<qsizetype> mapLumps;
		for (qsizetype position = 0; position + 1 < order.size(); ++position) {
			const QString next = entries[order[position + 1]].virtualPath.toUpper();
			if (next != QStringLiteral("THINGS") && next != QStringLiteral("TEXTMAP") && next != QStringLiteral("GL_VERT")) {
				continue;
			}
			mapLumps.insert(order[position]);
			for (qsizetype after = position + 1; after < order.size(); ++after) {
				const QString lump = entries[order[after]].virtualPath.toUpper();
				if (!mapLumpNames.contains(lump)) {
					break;
				}
				mapLumps.insert(order[after]);
				if (lump == QStringLiteral("ENDMAP")) {
					break;
				}
			}
		}
		for (const qsizetype i : std::as_const(order)) {
			const PackageEntry& entry = entries[i];
			if (entry.kind != PackageEntryKind::File) {
				continue;
			}
			const bool marker = entry.typeHint == QStringLiteral("wad-marker");
			const bool mapLump = mapLumps.contains(i);
			if (!marker && !mapLump && m_stock) {
				if (const GameAssetRegisterFile* stock = m_stock->file(entry.virtualPath); stock && stock->sizeBytes == entry.sizeBytes) {
					quint32 crc = 0;
					QString readError;
					const bool read = archive->streamEntryAt(i, [&crc](QByteArrayView chunk) {
						crc = crc32View(chunk, crc);
						return true;
					}, &readError);
					if (read && crc == stock->crc32) {
						addStockReference(QStringLiteral("lump"), entry.virtualPath, stock->path, m_stock->sourceLabel(stock->source), {requiredBy}, true);
						continue;
					}
				}
			}
			ReleaseEntry release;
			release.virtualPath = entry.virtualPath;
			release.role = mapLump ? QStringLiteral("map")
				: marker ? QStringLiteral("content")
				: entry.typeHint == QStringLiteral("wad-flat") || entry.typeHint == QStringLiteral("wad-patch") || entry.typeHint == QStringLiteral("wad-texture")
					|| entry.typeHint == QStringLiteral("wad-sprite") ? QStringLiteral("texture")
				: entry.typeHint == QStringLiteral("wad-sound") ? QStringLiteral("sound")
				: entry.virtualPath.startsWith(QStringLiteral("D_"), Qt::CaseInsensitive) ? QStringLiteral("music") : QStringLiteral("content");
			release.sizeBytes = entry.sizeBytes;
			release.lumpSource = lumpSource;
			release.layerIndex = i;
			release.requiredBy << requiredBy;
			release.note = label;
			m_plan.entries.push_back(release);
		}
	}

	void planModelItem(const QString& item)
	{
		const QString path = absoluteItemPath(item);
		const QFileInfo info(path);
		QStringList models;
		if (info.isDir()) {
			QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
			while (it.hasNext()) {
				const QString file = it.next();
				if (isModelExtension(suffixOf(file))) {
					models << file;
				}
			}
			std::sort(models.begin(), models.end());
			if (models.isEmpty()) {
				addProblem(QStringLiteral("missing"), item, QCoreApplication::translate("VibeStudioReleasePlan", "No models were found in %1.").arg(QDir::toNativeSeparators(path)), {}, true);
			}
		} else if (info.isFile()) {
			models << path;
		} else {
			addProblem(QStringLiteral("missing"), item, QCoreApplication::translate("VibeStudioReleasePlan", "Model not found: %1").arg(QDir::toNativeSeparators(path)), {}, true);
			return;
		}
		for (const QString& model : std::as_const(models)) {
			if (!tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking models"), 0, 1)) {
				return;
			}
			const QString virtualPath = virtualPathFor(model);
			if (virtualPath.isEmpty()) {
				addProblem(QStringLiteral("outside-project"), model, QCoreApplication::translate("VibeStudioReleasePlan", "%1 is outside the project's content folders, so its game path is unknown. Move it under models/ in the project.")
					.arg(QDir::toNativeSeparators(model)), {}, true);
				continue;
			}
			const qsizetype index = m_reader->indexOf(virtualPath);
			addReaderEntry(index, QStringLiteral("model"), virtualPath);
			ModelWorkControl control;
			control.cancelled = [this]() { return m_plan.cancelled; };
			if (m_reader->entries().at(index).sizeBytes > quint64(kMaximumModelBytes)) {
				addProblem(QStringLiteral("incomplete"), virtualPath, QCoreApplication::translate("VibeStudioReleasePlan", "%1 is too large to inspect; its materials were not checked.").arg(virtualPath), {virtualPath}, false);
				continue;
			}
			const ModelMesh mesh = decodeModelMeshFromArchive(*m_reader, virtualPath, QString(), control);
			if (!mesh.error.isEmpty()) {
				addProblem(QStringLiteral("unreadable"), virtualPath, QCoreApplication::translate("VibeStudioReleasePlan", "%1 could not be decoded: %2").arg(virtualPath, mesh.error), {virtualPath}, true);
				continue;
			}
			const LevelDependencyReport report = inspectModelMaterialDependencies(mesh, *m_reader, [this, virtualPath](int completed, int total) {
				return tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking %1").arg(virtualPath), completed, total);
			}, m_request.stock);
			absorbDependencies(report, virtualPath, false);
			planModelSkins(virtualPath);
		}
	}

	// Quake III .skin files beside an MD3 (<model>_<skin>.skin) and the images they name.
	void planModelSkins(const QString& modelPath)
	{
		const QString folder = packageVirtualPathParent(modelPath);
		const QString stem = QFileInfo(modelPath).completeBaseName().toLower();
		for (qsizetype i = 0; i < m_reader->entries().size(); ++i) {
			const PackageEntry& entry = m_reader->entries().at(i);
			if (suffixOf(entry.virtualPath) != QStringLiteral("skin") || packageVirtualPathParent(entry.virtualPath).compare(folder, Qt::CaseInsensitive) != 0
				|| !QFileInfo(entry.virtualPath).fileName().toLower().startsWith(stem + QLatin1Char('_'))) {
				continue;
			}
			addReaderEntry(i, QStringLiteral("skin"), modelPath);
			QByteArray bytes;
			QString error;
			if (!m_reader->readEntryAt(i, &bytes, &error, 1024 * 1024)) {
				continue;
			}
			for (const QByteArray& rawLine : bytes.split('\n')) {
				const QString line = QString::fromLatin1(rawLine).trimmed();
				const qsizetype comma = line.indexOf(QLatin1Char(','));
				if (comma < 0) {
					continue;
				}
				const QString image = line.mid(comma + 1).trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
				if (image.isEmpty() || line.left(comma).startsWith(QStringLiteral("tag_"), Qt::CaseInsensitive)) {
					continue;
				}
				QStringList candidates {image};
				const QString base = image.left(image.lastIndexOf(QLatin1Char('.')) > 0 ? image.lastIndexOf(QLatin1Char('.')) : image.size());
				for (const QString& ext : {QStringLiteral(".tga"), QStringLiteral(".jpg"), QStringLiteral(".png")}) {
					appendUnique(&candidates, base + ext);
				}
				if (!resolveOptional(candidates, QStringLiteral("texture"), QStringLiteral("skin-image"), modelPath)) {
					addProblem(m_stock ? QStringLiteral("missing") : QStringLiteral("unverified"), image,
						QCoreApplication::translate("VibeStudioReleasePlan", "%1 names %2, which is neither in the project nor in the game's own files.").arg(entry.virtualPath, image), {modelPath}, m_stock != nullptr);
				}
			}
		}
	}

	void planTextures()
	{
		// Each item is a project file or folder; folders take every image in them.
		QStringList prefixes;
		QVector<qsizetype> images;
		QSet<qsizetype> seen;
		for (const QString& item : m_request.items) {
			const QString path = absoluteItemPath(item);
			const QFileInfo info(path);
			QString virtualPath;
			if (info.isDir()) {
				for (const ProjectContentRoot& root : m_content->roots()) {
					const QString relative = QDir(root.path).relativeFilePath(path);
					if (!relative.startsWith(QStringLiteral(".."))) {
						virtualPath = relative == QStringLiteral(".") ? QString() : relative;
						break;
					}
				}
				const QString prefix = virtualPath.isEmpty() ? QString() : virtualPath + QLatin1Char('/');
				prefixes << prefix;
				for (qsizetype i = 0; i < m_reader->entries().size(); ++i) {
					const PackageEntry& entry = m_reader->entries().at(i);
					if (entry.virtualPath.startsWith(prefix, Qt::CaseInsensitive) && isImageExtension(suffixOf(entry.virtualPath)) && !seen.contains(i)) {
						seen.insert(i);
						images << i;
					}
				}
			} else {
				virtualPath = virtualPathFor(path);
				if (virtualPath.isEmpty()) {
					addProblem(QStringLiteral("outside-project"), item, QCoreApplication::translate("VibeStudioReleasePlan", "%1 is outside the project's content folders, so its game path is unknown.")
						.arg(QDir::toNativeSeparators(path)), {}, true);
					continue;
				}
				const qsizetype index = m_reader->indexOf(virtualPath);
				if (!seen.contains(index)) {
					seen.insert(index);
					images << index;
				}
				const QString parent = packageVirtualPathParent(virtualPath);
				prefixes << (parent.isEmpty() ? QString() : parent + QLatin1Char('/')) + QFileInfo(virtualPath).completeBaseName();
			}
		}
		for (qsizetype index : std::as_const(images)) {
			addReaderEntry(index, QStringLiteral("texture"), QStringLiteral("textures"));
		}
		// Shader scripts that declare shaders under the chosen folders ship with
		// them, along with the images those shaders use.
		for (qsizetype i = 0; i < m_reader->entries().size(); ++i) {
			const PackageEntry& entry = m_reader->entries().at(i);
			const QString lower = entry.virtualPath.toLower();
			if (!lower.startsWith(QStringLiteral("scripts/")) || !lower.endsWith(QStringLiteral(".shader")) || entry.sizeBytes > quint64(kMaximumScriptBytes)) {
				continue;
			}
			QByteArray bytes;
			QString error;
			if (!m_reader->readEntryAt(i, &bytes, &error, kMaximumScriptBytes)) {
				continue;
			}
			const ShaderDocument shaders = parseShaderScriptText(QString::fromUtf8(bytes), entry.virtualPath);
			bool relevant = false;
			QStringList imageReferences;
			for (const ShaderDefinition& shader : shaders.shaders) {
				bool matches = false;
				for (const QString& prefix : std::as_const(prefixes)) {
					matches = matches || prefix.isEmpty() || shader.name.startsWith(prefix, Qt::CaseInsensitive);
				}
				if (matches) {
					relevant = true;
					imageReferences += shader.textureReferences;
				}
			}
			if (!relevant) {
				continue;
			}
			addReaderEntry(i, QStringLiteral("shader"), QStringLiteral("textures"));
			for (const QString& reference : std::as_const(imageReferences)) {
				const QString clean = reference.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
				if (clean.isEmpty() || clean.startsWith(QLatin1Char('$')) || clean.startsWith(QLatin1Char('*'))) {
					continue;
				}
				QStringList candidates {clean};
				const qsizetype dot = clean.lastIndexOf(QLatin1Char('.'));
				const QString base = dot > clean.lastIndexOf(QLatin1Char('/')) ? clean.left(dot) : clean;
				for (const QString& ext : {QStringLiteral(".tga"), QStringLiteral(".jpg"), QStringLiteral(".png")}) {
					appendUnique(&candidates, base + ext);
				}
				if (!resolveOptional(candidates, QStringLiteral("texture"), QStringLiteral("shader-image"), entry.virtualPath)) {
					addProblem(m_stock ? QStringLiteral("missing") : QStringLiteral("unverified"), clean,
						QCoreApplication::translate("VibeStudioReleasePlan", "%1 uses %2, which is neither in the project nor in the game's own files.").arg(entry.virtualPath, clean),
						{entry.virtualPath}, m_stock != nullptr);
				}
			}
		}
	}

	void planProject()
	{
		const QVector<PackageEntry> entries = m_reader->entries();
		int sources = 0, intermediates = 0, documents = 0, excluded = 0;
		QStringList mapSources;
		QStringList compiledMaps;
		QStringList doomWads;
		QSet<QString> sourceNames;
		for (qsizetype i = 0; i < entries.size(); ++i) {
			if (i % 64 == 0 && !tick(QCoreApplication::translate("VibeStudioReleasePlan", "Sorting project files"), int(i), int(entries.size()))) {
				return;
			}
			const PackageEntry& entry = entries[i];
			const QString& path = entry.virtualPath;
			if (globMatches(m_request.release.exclude, path)) {
				++excluded;
				continue;
			}
			const bool forced = globMatches(m_request.release.include, path);
			const FileClass fileClass = classifyFile(path, m_plan.gameKey);
			const QString ext = suffixOf(path);
			const bool onDisk = m_reader->layerOf(i) == 0;
			if (ext == QStringLiteral("map") && onDisk) {
				mapSources << m_content->absolutePath(m_reader->layerIndexOf(i));
				sourceNames.insert(QFileInfo(path).completeBaseName().toCaseFolded());
			}
			// Compiled maps are placed by map planning at maps/<name>.bsp, the
			// only place the engines load them from.
			if (ext == QStringLiteral("bsp")) {
				const QString lower = path.toLower();
				if (lower.startsWith(QStringLiteral("maps/")) && lower.count(QLatin1Char('/')) == 1 && onDisk) {
					compiledMaps << m_content->absolutePath(m_reader->layerIndexOf(i));
				} else {
					appendUnique(&m_plan.warnings, QCoreApplication::translate("VibeStudioReleasePlan", "%1 was left out: engines load compiled maps only from maps/.").arg(path));
				}
				continue;
			}
			if (lumpMode() && ext == QStringLiteral("wad")) {
				if (onDisk) {
					doomWads << m_content->absolutePath(m_reader->layerIndexOf(i));
				}
				continue;
			}
			if (!forced) {
				switch (fileClass) {
				case FileClass::Source:
					if (!m_request.release.includeSources) {
						++sources;
						continue;
					}
					break;
				case FileClass::Intermediate:
					++intermediates;
					continue;
				case FileClass::Document:
					++documents;
					continue;
				case FileClass::Executable:
					appendUnique(&m_plan.warnings, QCoreApplication::translate("VibeStudioReleasePlan", "%1 is a program or script and was left out. Add it to the release's include patterns to ship it.").arg(path));
					continue;
				case FileClass::NativeCode:
				case FileClass::Content:
					break;
				}
			}
			QString role = roleForPath(path);
			if (fileClass == FileClass::Source && role == QStringLiteral("content")) {
				role = QStringLiteral("map-source");
			}
			const int added = addReaderEntry(i, role, QStringLiteral("project"));
			if (added < 0) {
				continue;
			}
			// A WAD release can only hold lumps: other files ship beside it.
			if (lumpMode()) {
				m_plan.entries[added].loose = true;
			}
			if (m_plan.entries[added].loose && role == QStringLiteral("code")) {
				appendUnique(&m_plan.warnings, QCoreApplication::translate("VibeStudioReleasePlan", "%1 is native game code; engines cannot load it from a package, so it ships beside the package.").arg(path));
			}
		}
		if (lumpMode()) {
			// Doom family: each project WAD with maps is checked, then every
			// project WAD's own lumps merge into the release in name order.
			std::sort(doomWads.begin(), doomWads.end(), [](const QString& left, const QString& right) { return left.compare(right, Qt::CaseInsensitive) < 0; });
			for (const QString& wad : std::as_const(doomWads)) {
				QString error;
				if (!levelMapNamesInWad(wad, &error).isEmpty()) {
					planDoomWad(wad);
				}
			}
			for (const QString& wad : std::as_const(doomWads)) {
				addWadLumps(wad, QFileInfo(wad).fileName(), QStringLiteral("project"));
			}
		} else {
			const int total = int(mapSources.size() + compiledMaps.size());
			int done = 0;
			for (const QString& map : std::as_const(mapSources)) {
				if (!tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking maps"), done++, total)) {
					return;
				}
				planSourceMap(map);
			}
			for (const QString& bsp : std::as_const(compiledMaps)) {
				if (!tick(QCoreApplication::translate("VibeStudioReleasePlan", "Checking maps"), done++, total)) {
					return;
				}
				if (!sourceNames.contains(QFileInfo(bsp).completeBaseName().toCaseFolded())) {
					planCompiledMap(bsp, QString());
				}
			}
		}
		if (sources > 0) {
			m_plan.limitations << QCoreApplication::translate("VibeStudioReleasePlan", "%1 source file(s) such as maps and source art were left out. Turn on Include sources to ship them.").arg(sources);
		}
		if (intermediates > 0) {
			m_plan.limitations << QCoreApplication::translate("VibeStudioReleasePlan", "%1 compiler, backup or editor scratch file(s) were left out.").arg(intermediates);
		}
		if (documents > 0) {
			m_plan.limitations << QCoreApplication::translate("VibeStudioReleasePlan", "The project's changelog and release notes are written beside the package, not inside it.");
		}
		if (excluded > 0) {
			m_plan.limitations << QCoreApplication::translate("VibeStudioReleasePlan", "%1 file(s) matched the release's exclude patterns.").arg(excluded);
		}
		m_plan.limitations << QCoreApplication::translate("VibeStudioReleasePlan", "Files that game code loads by name (QuakeC, QVM or DECORATE) are shipped when they are in the project, but references to them are not checked.");
	}

	ReleasePlan finish()
	{
		if (!lumpMode()) {
			std::sort(m_plan.entries.begin(), m_plan.entries.end(), [](const ReleaseEntry& left, const ReleaseEntry& right) {
				return left.virtualPath.compare(right.virtualPath, Qt::CaseInsensitive) < 0;
			});
		}
		std::sort(m_plan.stock.begin(), m_plan.stock.end(), [](const ReleaseReference& left, const ReleaseReference& right) {
			return left.path.compare(right.path, Qt::CaseInsensitive) < 0;
		});
		std::stable_sort(m_plan.problems.begin(), m_plan.problems.end(), [](const ReleaseProblem& left, const ReleaseProblem& right) {
			return left.blocking && !right.blocking;
		});
		m_plan.totalBytes = 0;
		m_plan.overrideCount = 0;
		for (const ReleaseEntry& entry : std::as_const(m_plan.entries)) {
			m_plan.totalBytes += entry.sizeBytes;
			m_plan.overrideCount += entry.replacesStock ? 1 : 0;
		}
		if (m_plan.overrideCount > 0) {
			m_plan.warnings.prepend(QCoreApplication::translate("VibeStudioReleasePlan",
				"%1 file(s) replace the game's own files of the same name. Every map players load will use these versions; rename them unless that is the intent.").arg(m_plan.overrideCount));
		}
		if (!m_plan.cancelled && m_plan.entries.isEmpty() && m_plan.problems.isEmpty()) {
			addProblem(QStringLiteral("empty"), QString(), QCoreApplication::translate("VibeStudioReleasePlan", "Nothing to release: everything chosen is either the game's own content or not content."), {}, true);
		}
		if (!m_stock) {
			m_plan.limitations.prepend(QCoreApplication::translate("VibeStudioReleasePlan",
				"The game's own files are unknown, so nothing was checked against them. Index the game's assets on the Workspace page to leave stock files out and find missing ones."));
		}
		return m_plan;
	}

	const ReleaseRequest& m_request;
	const GameAssetRegister* m_stock = nullptr;
	ReleasePlan m_plan;
	QString m_projectRoot;
	std::shared_ptr<ProjectContentReader> m_content;
	std::shared_ptr<LayeredPackageReader> m_reader;
	QSet<QString> m_wadsAdded;
	QHash<QString, int> m_entryIndex;
	QHash<QString, int> m_stockIndex;
	QHash<QString, int> m_problemIndex;
};

} // namespace

QString releaseScopeId(ReleaseScope scope)
{
	switch (scope) {
	case ReleaseScope::Project:
		return QStringLiteral("project");
	case ReleaseScope::Maps:
		return QStringLiteral("maps");
	case ReleaseScope::Models:
		return QStringLiteral("models");
	case ReleaseScope::Textures:
		return QStringLiteral("textures");
	}
	return QStringLiteral("project");
}

ReleaseScope releaseScopeFromId(const QString& id, bool* ok)
{
	const QString key = id.trimmed().toLower();
	if (ok) {
		*ok = true;
	}
	if (key == QStringLiteral("project") || key == QStringLiteral("mod")) {
		return ReleaseScope::Project;
	}
	if (key == QStringLiteral("maps") || key == QStringLiteral("map")) {
		return ReleaseScope::Maps;
	}
	if (key == QStringLiteral("models") || key == QStringLiteral("model")) {
		return ReleaseScope::Models;
	}
	if (key == QStringLiteral("textures") || key == QStringLiteral("texture")) {
		return ReleaseScope::Textures;
	}
	if (ok) {
		*ok = false;
	}
	return ReleaseScope::Project;
}

QString releaseScopeDisplayName(ReleaseScope scope)
{
	switch (scope) {
	case ReleaseScope::Project:
		return QCoreApplication::translate("VibeStudioReleasePlan", "Whole project");
	case ReleaseScope::Maps:
		return QCoreApplication::translate("VibeStudioReleasePlan", "Maps");
	case ReleaseScope::Models:
		return QCoreApplication::translate("VibeStudioReleasePlan", "Models");
	case ReleaseScope::Textures:
		return QCoreApplication::translate("VibeStudioReleasePlan", "Textures");
	}
	return {};
}

QString releaseRoleDisplayName(const QString& role)
{
	if (role == QStringLiteral("map")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Map"); }
	if (role == QStringLiteral("map-companion")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Map extra"); }
	if (role == QStringLiteral("map-source")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Source"); }
	if (role == QStringLiteral("texture")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Texture"); }
	if (role == QStringLiteral("shader")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Shader"); }
	if (role == QStringLiteral("model")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Model"); }
	if (role == QStringLiteral("skin")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Skin"); }
	if (role == QStringLiteral("sound")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Sound"); }
	if (role == QStringLiteral("music")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Music"); }
	if (role == QStringLiteral("script")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Script"); }
	if (role == QStringLiteral("code")) { return QCoreApplication::translate("VibeStudioReleasePlan", "Game code"); }
	return QCoreApplication::translate("VibeStudioReleasePlan", "Other");
}

int ReleasePlan::blockingCount() const
{
	int count = 0;
	for (const ReleaseProblem& problem : problems) {
		count += problem.blocking ? 1 : 0;
	}
	return count;
}

bool ReleasePlan::canPublish() const
{
	return !cancelled && blockingCount() == 0 && !entries.isEmpty() && format != PackageArchiveFormat::Unknown;
}

QVector<ReleaseCompositionRow> ReleasePlan::composition() const
{
	static const QStringList order {
		QStringLiteral("map"), QStringLiteral("map-companion"), QStringLiteral("texture"), QStringLiteral("shader"), QStringLiteral("model"),
		QStringLiteral("skin"), QStringLiteral("sound"), QStringLiteral("music"), QStringLiteral("script"), QStringLiteral("code"),
		QStringLiteral("map-source"), QStringLiteral("content"),
	};
	QVector<ReleaseCompositionRow> rows;
	for (const QString& role : order) {
		ReleaseCompositionRow row;
		row.role = role;
		for (const ReleaseEntry& entry : entries) {
			if (entry.role == role) {
				++row.count;
				row.bytes += entry.sizeBytes;
			}
		}
		if (row.count > 0) {
			rows << row;
		}
	}
	return rows;
}

const ReleaseEntry* ReleasePlan::entry(const QString& virtualPath) const
{
	for (const ReleaseEntry& candidate : entries) {
		if (candidate.virtualPath.compare(virtualPath, Qt::CaseInsensitive) == 0) {
			return &candidate;
		}
	}
	return nullptr;
}

ReleasePlan planRelease(const ReleaseRequest& request)
{
	Planner planner(request);
	return planner.run();
}

QString defaultReleaseFormatId(const QString& gameKey, ReleaseScope scope)
{
	const QString key = normalizedGameKey(gameKey);
	if (key == QStringLiteral("quake3")) {
		return QStringLiteral("pk3");
	}
	if (key == QStringLiteral("doom") || key == QStringLiteral("heretic-hexen")) {
		return QStringLiteral("wad");
	}
	if (key == QStringLiteral("quake") || key == QStringLiteral("quake2")) {
		// Single maps travel as loose files: a numbered PAK would clash with
		// whatever else the player installed.
		return scope == ReleaseScope::Project ? QStringLiteral("pak") : QStringLiteral("zip");
	}
	return QStringLiteral("zip");
}

ReleaseCatalog releaseCatalog(const ProjectManifest& manifest, const QString& gameKey, const ProjectContentOptions& options)
{
	ReleaseCatalog catalog;
	ProjectContentReader content;
	QString error;
	if (!content.load(projectContentRoots(manifest), &error, options)) {
		catalog.warnings << error;
		return catalog;
	}
	catalog.warnings = content.warnings();
	const QString game = normalizedGameKey(gameKey);
	const bool doomFamily = game == QStringLiteral("doom") || game == QStringLiteral("heretic-hexen");
	QSet<QString> textureFolders;
	QHash<QString, QString> sources;
	QHash<QString, QString> compiled;
	for (qsizetype i = 0; i < content.entries().size(); ++i) {
		const PackageEntry& entry = content.entries().at(i);
		const QString lower = entry.virtualPath.toLower();
		const QString ext = suffixOf(lower);
		const QString absolute = content.absolutePath(i);
		if (ext == QStringLiteral("map")) {
			sources.insert(QFileInfo(lower).completeBaseName(), absolute);
		} else if (ext == QStringLiteral("bsp") && lower.startsWith(QStringLiteral("maps/"))) {
			compiled.insert(QFileInfo(lower).completeBaseName(), absolute);
		} else if (ext == QStringLiteral("wad") && doomFamily) {
			QString wadError;
			const QStringList names = levelMapNamesInWad(absolute, &wadError);
			if (!names.isEmpty()) {
				ReleaseMapInfo map;
				map.name = QFileInfo(absolute).completeBaseName();
				map.sourcePath = absolute;
				map.compiledPath = absolute;
				map.built = true;
				map.formatId = QStringLiteral("doom-wad");
				map.doomMaps = names;
				catalog.maps << map;
			}
		} else if (isModelExtension(ext) && (lower.startsWith(QStringLiteral("models/")) || lower.startsWith(QStringLiteral("progs/")))) {
			catalog.models << absolute;
		} else if (isImageExtension(ext) && (lower.startsWith(QStringLiteral("textures/")) || lower.startsWith(QStringLiteral("env/")))) {
			const QString parent = packageVirtualPathParent(entry.virtualPath);
			if (!parent.isEmpty()) {
				// The root that holds this file, plus its game-relative folder.
				for (const ProjectContentRoot& root : content.roots()) {
					const QString candidate = QDir(root.path).filePath(parent);
					if (QFileInfo(candidate).isDir()) {
						textureFolders.insert(QDir::cleanPath(candidate));
						break;
					}
				}
			}
		}
	}
	QStringList names = sources.keys();
	for (const QString& name : compiled.keys()) {
		if (!sources.contains(name)) {
			names << name;
		}
	}
	std::sort(names.begin(), names.end());
	for (const QString& name : std::as_const(names)) {
		ReleaseMapInfo map;
		map.name = name;
		map.sourcePath = sources.value(name);
		if (!map.sourcePath.isEmpty()) {
			const QFileInfo info(map.sourcePath);
			map.compiledPath = compiledMapFor(manifest, manifest.rootPath.trimmed().isEmpty() ? QString() : QDir::cleanPath(manifest.rootPath), map.sourcePath);
			map.built = !map.compiledPath.isEmpty();
			map.stale = map.built && QFileInfo(map.compiledPath).lastModified() < info.lastModified();
		} else {
			map.compiledPath = compiled.value(name);
			map.built = true;
		}
		map.formatId = map.sourcePath.isEmpty() ? QStringLiteral("bsp") : QStringLiteral("map");
		catalog.maps << map;
	}
	std::sort(catalog.models.begin(), catalog.models.end());
	catalog.textureFolders = QStringList(textureFolders.cbegin(), textureFolders.cend());
	std::sort(catalog.textureFolders.begin(), catalog.textureFolders.end());
	return catalog;
}

std::shared_ptr<const GameAssetRegister> mergeGameAssetRegisters(const QVector<std::shared_ptr<const GameAssetRegister>>& registers)
{
	auto merged = std::make_shared<GameAssetRegister>();
	bool first = true;
	for (const auto& reg : registers) {
		if (!reg) {
			continue;
		}
		if (first) {
			merged->gameKey = reg->gameKey;
			merged->installationId = reg->installationId;
			merged->installationName = reg->installationName;
			merged->installationRoot = reg->installationRoot;
			merged->createdUtc = reg->createdUtc;
			merged->studioVersion = reg->studioVersion;
			first = false;
		}
		const int sourceOffset = int(merged->sources.size());
		const int fileOffset = int(merged->files.size());
		merged->sources += reg->sources;
		for (GameAssetRegisterFile file : reg->files) {
			file.source += sourceOffset;
			merged->files << file;
		}
		for (GameAssetRegisterName name : reg->shaders) {
			name.source += sourceOffset;
			name.file = name.file >= 0 ? name.file + fileOffset : -1;
			merged->shaders << name;
		}
		for (auto it = reg->doomNames.cbegin(); it != reg->doomNames.cend(); ++it) {
			for (GameAssetRegisterName name : it.value()) {
				name.source += sourceOffset;
				merged->doomNames[it.key()] << name;
			}
		}
	}
	merged->rebuildIndex();
	return merged;
}

std::shared_ptr<const GameAssetRegister> requirementAssetRegister(const ProjectReleaseRequirement& requirement, const QString& installationRoot,
	QString* error, const PackageReadControl& control)
{
	const QString trimmed = requirement.path.trimmed();
	if (trimmed.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleasePlan", "Requirement %1 has no path.").arg(requirement.name);
		}
		return {};
	}
	const QString absolute = QDir::isAbsolutePath(trimmed) ? QDir::cleanPath(trimmed)
		: QDir::cleanPath(QDir(installationRoot.isEmpty() ? QDir::currentPath() : installationRoot).absoluteFilePath(trimmed));
	const QFileInfo info(absolute);
	if (!info.exists()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleasePlan", "Requirement %1 was not found at %2.").arg(requirement.name, QDir::toNativeSeparators(absolute));
		}
		return {};
	}
	// A folder counts with its loose files and the packages inside it, in the
	// order the engines load them.
	QStringList packages;
	bool cacheable = true;
	if (info.isDir()) {
		packages << absolute;
		cacheable = false;
		QStringList inside;
		for (const QFileInfo& child : QDir(absolute).entryInfoList({QStringLiteral("*.pak"), QStringLiteral("*.pk3"), QStringLiteral("*.wad")}, QDir::Files, QDir::Name)) {
			inside << child.absoluteFilePath();
		}
		packages += inside;
	} else {
		packages << absolute;
	}
	QByteArray identity = absolute.toUtf8();
	for (const QString& package : std::as_const(packages)) {
		const QFileInfo packageInfo(package);
		identity += '|' + QByteArray::number(packageInfo.size()) + '|' + packageInfo.lastModified().toUTC().toString(Qt::ISODateWithMs).toUtf8();
	}
	const QString cachePath = QDir(gameAssetRegisterDirectory()).absoluteFilePath(
		QStringLiteral("requirement-%1.json").arg(QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex())));
	if (cacheable) {
		auto cached = std::make_shared<GameAssetRegister>();
		if (QFileInfo::exists(cachePath) && loadGameAssetRegister(cachePath, cached.get())) {
			return cached;
		}
	}
	GameAssetRegisterBuildRequest build;
	build.installation.id = QStringLiteral("requirement");
	build.installation.displayName = requirement.name.isEmpty() ? info.fileName() : requirement.name;
	build.installation.rootPath = info.isDir() ? absolute : info.absolutePath();
	build.installation.gameKey = QStringLiteral("custom");
	build.packagePaths = packages;
	build.control = control;
	GameAssetRegisterBuildResult result = buildGameAssetRegister(build);
	if (!result.succeeded) {
		if (error) {
			*error = result.error;
		}
		return {};
	}
	for (GameAssetRegisterSource& source : result.registerData.sources) {
		source.role = QStringLiteral("requirement");
		source.label = requirement.name.isEmpty() ? info.fileName() : requirement.name;
	}
	result.registerData.rebuildIndex();
	if (cacheable) {
		saveGameAssetRegister(result.registerData, cachePath);
	}
	return std::make_shared<GameAssetRegister>(std::move(result.registerData));
}

ReleaseStockContext prepareReleaseStock(const GameInstallationProfile* installation, const ProjectReleaseSettings& release, const QString& gameKey,
	const QString& registerFile, const PackageReadControl& control)
{
	ReleaseStockContext context;
	const QString game = normalizedGameKey(gameKey);
	auto loaded = std::make_shared<GameAssetRegister>();
	bool have = false;
	if (!registerFile.trimmed().isEmpty()) {
		QString error;
		if (!loadGameAssetRegister(registerFile, loaded.get(), &error)) {
			context.warnings << error;
		} else {
			have = true;
			context.status.loaded = true;
			context.status.fresh = true;
			context.status.path = registerFile;
		}
	} else if (installation) {
		context.status = gameAssetRegisterStatus(*installation, loaded.get());
		if (!context.status.exists) {
			context.warnings << QCoreApplication::translate("VibeStudioReleasePlan",
				"%1 has not been indexed yet, so the game's own files cannot be told apart from yours. Index its assets on the Workspace page, or run: vibestudio --cli install register build %2")
				.arg(installation->displayName, installation->id);
		} else if (!context.status.loaded) {
			context.warnings << context.status.error;
		} else {
			have = true;
			if (!context.status.fresh) {
				context.warnings << QCoreApplication::translate("VibeStudioReleasePlan", "The asset index for %1 is out of date: %2 Index it again for an exact check.")
					.arg(installation->displayName, context.status.staleReasons.join(QLatin1Char(' ')));
			}
		}
	} else {
		context.warnings << QCoreApplication::translate("VibeStudioReleasePlan", "No game installation is linked to the project, so the game's own files cannot be told apart from yours.");
	}
	if (have && normalizedGameKey(loaded->gameKey) != game && game != QStringLiteral("custom")) {
		context.warnings << QCoreApplication::translate("VibeStudioReleasePlan", "The asset index is for %1, but the release targets %2; it was not used.")
			.arg(gameDefinitionForKey(loaded->gameKey).displayName, gameDefinitionForKey(game).displayName);
		have = false;
	}
	QVector<std::shared_ptr<const GameAssetRegister>> registers;
	if (have) {
		QStringList sources = release.stockSources.isEmpty() ? loaded->defaultSourceIds() : release.stockSources;
		for (const QString& id : release.stockSources) {
			bool known = false;
			for (const GameAssetRegisterSource& source : loaded->sources) {
				known = known || source.id.compare(id, Qt::CaseInsensitive) == 0;
			}
			if (!known) {
				context.warnings << QCoreApplication::translate("VibeStudioReleasePlan", "The release names stock package %1, which the installation does not have.").arg(id);
			}
		}
		auto filtered = std::make_shared<GameAssetRegister>(loaded->filtered(sources));
		QStringList labels;
		for (const GameAssetRegisterSource& source : filtered->sources) {
			labels << source.relativePath;
		}
		context.description = QCoreApplication::translate("VibeStudioReleasePlan", "%1: %2 (%3 files)")
			.arg(gameDefinitionForKey(game).displayName, labels.join(QStringLiteral(", ")), QLocale().toString(filtered->files.size()));
		registers << filtered;
	}
	for (const ProjectReleaseRequirement& requirement : release.requirements) {
		if (requirement.path.trimmed().isEmpty()) {
			continue;
		}
		QString error;
		auto requirementRegister = requirementAssetRegister(requirement, installation ? installation->rootPath : QString(), &error, control);
		if (!requirementRegister) {
			context.warnings << error;
			continue;
		}
		registers << requirementRegister;
		context.description += (context.description.isEmpty() ? QString() : QStringLiteral(" + ")) + (requirement.name.isEmpty() ? requirement.path : requirement.name);
	}
	if (!registers.isEmpty()) {
		context.stock = registers.size() == 1 ? registers.first() : mergeGameAssetRegisters(registers);
		context.available = true;
	}
	return context;
}

bool releaseInstallationFor(const ProjectManifest& manifest, const QVector<GameInstallationProfile>& installations, const QString& selectedId,
	GameInstallationProfile* installation)
{
	const auto pick = [installation](const GameInstallationProfile& profile) {
		if (installation) {
			*installation = profile;
		}
		return true;
	};
	const QString linked = effectiveProjectInstallationId(manifest);
	const GameInstallationProfile* selected = nullptr;
	for (const GameInstallationProfile& profile : installations) {
		if (!linked.isEmpty() && sameGameInstallationId(profile.id, linked)) {
			return pick(profile);
		}
		if (!selectedId.isEmpty() && sameGameInstallationId(profile.id, selectedId)) {
			selected = &profile;
		}
	}
	// A project that names its game is checked against that game, even when
	// another game's installation is the one in use.
	const QString game = manifest.gameKey.trimmed().isEmpty() ? QString() : normalizedGameKey(manifest.gameKey);
	if (!game.isEmpty() && game != QStringLiteral("custom")) {
		if (selected && normalizedGameKey(selected->gameKey) == game) {
			return pick(*selected);
		}
		for (const GameInstallationProfile& profile : installations) {
			if (normalizedGameKey(profile.gameKey) == game) {
				return pick(profile);
			}
		}
	}
	return selected ? pick(*selected) : false;
}

QJsonObject releasePlanJson(const ReleasePlan& plan)
{
	QJsonArray entries;
	for (const ReleaseEntry& entry : plan.entries) {
		QJsonObject object {
			{QStringLiteral("path"), entry.virtualPath},
			{QStringLiteral("role"), entry.role},
			{QStringLiteral("bytes"), double(entry.sizeBytes)},
			{QStringLiteral("requiredBy"), jsonStrings(entry.requiredBy)},
		};
		if (!entry.sourcePath.isEmpty()) {
			object.insert(QStringLiteral("source"), QDir::toNativeSeparators(entry.sourcePath));
		}
		if (entry.replacesStock) {
			object.insert(QStringLiteral("replacesStock"), true);
			object.insert(QStringLiteral("stockSource"), entry.stockSource);
		}
		if (entry.loose) {
			object.insert(QStringLiteral("loose"), true);
		}
		if (!entry.note.isEmpty()) {
			object.insert(QStringLiteral("note"), entry.note);
		}
		entries.append(object);
	}
	QJsonArray stock;
	for (const ReleaseReference& reference : plan.stock) {
		stock.append(QJsonObject {
			{QStringLiteral("kind"), reference.kind},
			{QStringLiteral("reference"), reference.reference},
			{QStringLiteral("path"), reference.path},
			{QStringLiteral("source"), reference.source},
			{QStringLiteral("requiredBy"), jsonStrings(reference.requiredBy)},
			{QStringLiteral("identicalCopy"), reference.identicalCopy},
		});
	}
	QJsonArray problems;
	for (const ReleaseProblem& problem : plan.problems) {
		problems.append(QJsonObject {
			{QStringLiteral("kind"), problem.kind},
			{QStringLiteral("reference"), problem.reference},
			{QStringLiteral("message"), problem.message},
			{QStringLiteral("requiredBy"), jsonStrings(problem.requiredBy)},
			{QStringLiteral("blocking"), problem.blocking},
		});
	}
	QJsonArray maps;
	for (const ReleaseMapInfo& map : plan.maps) {
		maps.append(QJsonObject {
			{QStringLiteral("name"), map.name},
			{QStringLiteral("title"), map.title},
			{QStringLiteral("source"), QDir::toNativeSeparators(map.sourcePath)},
			{QStringLiteral("compiled"), QDir::toNativeSeparators(map.compiledPath)},
			{QStringLiteral("format"), map.formatId},
			{QStringLiteral("built"), map.built},
			{QStringLiteral("stale"), map.stale},
			{QStringLiteral("entities"), map.entityCount},
			{QStringLiteral("brushes"), map.brushCount},
			{QStringLiteral("doomMaps"), jsonStrings(map.doomMaps)},
		});
	}
	QJsonArray composition;
	for (const ReleaseCompositionRow& row : plan.composition()) {
		composition.append(QJsonObject {{QStringLiteral("role"), row.role}, {QStringLiteral("files"), row.count}, {QStringLiteral("bytes"), double(row.bytes)}});
	}
	return {
		{QStringLiteral("schemaVersion"), 1},
		{QStringLiteral("game"), plan.gameKey},
		{QStringLiteral("scope"), releaseScopeId(plan.scope)},
		{QStringLiteral("items"), jsonStrings(plan.items)},
		{QStringLiteral("title"), plan.release.title},
		{QStringLiteral("version"), plan.release.version},
		{QStringLiteral("format"), plan.formatId},
		{QStringLiteral("package"), plan.packageFileName},
		{QStringLiteral("gameFolder"), plan.packageFolder},
		{QStringLiteral("files"), int(plan.entries.size())},
		{QStringLiteral("bytes"), double(plan.totalBytes)},
		{QStringLiteral("bytesExact"), QString::number(plan.totalBytes)},
		{QStringLiteral("stockReferences"), int(plan.stock.size())},
		{QStringLiteral("overrides"), plan.overrideCount},
		{QStringLiteral("blocking"), plan.blockingCount()},
		{QStringLiteral("canPublish"), plan.canPublish()},
		{QStringLiteral("stockChecked"), plan.stockChecked},
		{QStringLiteral("stockDescription"), plan.stockDescription},
		{QStringLiteral("complete"), plan.complete},
		{QStringLiteral("cancelled"), plan.cancelled},
		{QStringLiteral("entries"), entries},
		{QStringLiteral("stock"), stock},
		{QStringLiteral("problems"), problems},
		{QStringLiteral("maps"), maps},
		{QStringLiteral("composition"), composition},
		{QStringLiteral("warnings"), jsonStrings(plan.warnings)},
		{QStringLiteral("limitations"), jsonStrings(plan.limitations)},
	};
}

QString releasePlanText(const ReleasePlan& plan)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioReleasePlan", "%1 %2 (%3): %4 in %5")
				 .arg(plan.release.title, plan.release.version, releaseScopeDisplayName(plan.scope))
				 .arg(QCoreApplication::translate("VibeStudioReleasePlan", "%n file(s)", nullptr, int(plan.entries.size())) + QStringLiteral(", ")
					 + QLocale().formattedDataSize(qint64(std::min<quint64>(plan.totalBytes, quint64(std::numeric_limits<qint64>::max())))))
				 .arg(plan.packageFileName);
	lines << (plan.stockChecked ? QCoreApplication::translate("VibeStudioReleasePlan", "Provided by the game or requirements, not packaged: %1").arg(plan.stock.size())
								: QCoreApplication::translate("VibeStudioReleasePlan", "The game's own files were not checked."));
	if (!plan.stockDescription.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioReleasePlan", "Checked against: %1").arg(plan.stockDescription);
	}
	for (const ReleaseMapInfo& map : plan.maps) {
		lines << QCoreApplication::translate("VibeStudioReleasePlan", "Map %1%2: %3").arg(map.name, map.title.isEmpty() ? QString() : QStringLiteral(" (\"%1\")").arg(map.title),
			!map.built ? QCoreApplication::translate("VibeStudioReleasePlan", "not built") : map.stale ? QCoreApplication::translate("VibeStudioReleasePlan", "built, out of date")
			: QCoreApplication::translate("VibeStudioReleasePlan", "built"));
	}
	for (const ReleaseEntry& entry : plan.entries) {
		lines << QStringLiteral("  + %1  [%2]%3%4").arg(entry.virtualPath, entry.role,
			entry.replacesStock ? QCoreApplication::translate("VibeStudioReleasePlan", "  replaces the game's file") : QString(),
			entry.loose ? QCoreApplication::translate("VibeStudioReleasePlan", "  beside the package") : QString());
	}
	for (const ReleaseProblem& problem : plan.problems) {
		lines << QStringLiteral("%1 %2").arg(problem.blocking ? QStringLiteral("!") : QStringLiteral("~"), problem.message);
	}
	for (const QString& warning : plan.warnings) {
		lines << QStringLiteral("~ ") + warning;
	}
	for (const QString& limitation : plan.limitations) {
		lines << QStringLiteral("  ") + limitation;
	}
	lines << (plan.canPublish() ? QCoreApplication::translate("VibeStudioReleasePlan", "Ready to publish.")
								: QCoreApplication::translate("VibeStudioReleasePlan", "Not ready: %1 blocking problem(s).").arg(plan.blockingCount()));
	return lines.join(QLatin1Char('\n'));
}

} // namespace vibestudio
