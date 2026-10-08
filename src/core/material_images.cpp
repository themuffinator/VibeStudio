#include "core/material_images.h"

#include "core/idtech_image.h"
#include "core/material_script.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

constexpr qint64 kImageReadLimit = 64LL * 1024 * 1024;

QRgb rgba(int r, int g, int b, int a)
{
	return qRgba(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), std::clamp(a, 0, 255));
}

MaterialTexturePtr makeTexture(int width, int height, const std::function<QRgb(int, int)>& pixel, const QString& name,
	const QString& status = QStringLiteral("builtin"))
{
	auto texture = std::make_shared<MaterialTexture>();
	texture->width = width;
	texture->height = height;
	texture->pixels.resize(static_cast<qsizetype>(width) * height);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			texture->pixels[static_cast<qsizetype>(y) * width + x] = pixel(x, y);
		}
	}
	texture->reference = name;
	texture->path = name;
	texture->status = status;
	texture->buildMips();
	return texture;
}

QString suffixOf(const QString& path)
{
	const QString name = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
	const int dot = name.lastIndexOf(QLatin1Char('.'));
	return dot < 0 ? QString() : name.mid(dot + 1).toLower();
}

QString withoutSuffix(const QString& path)
{
	const int slash = path.lastIndexOf(QLatin1Char('/'));
	const int dot = path.lastIndexOf(QLatin1Char('.'));
	return dot > slash ? path.left(dot) : path;
}

QString normalizedPath(QString path)
{
	path = path.trimmed();
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	while (path.startsWith(QLatin1Char('/'))) {
		path.remove(0, 1);
	}
	return path;
}

// A reader's file paths, indexed case-insensitively; the last entry with a
// path wins, as later layers shadow earlier ones.
struct ReaderIndexData {
	QVector<PackageEntry> entries;
	QHash<QString, int> byPath;
	QHash<QString, QVector<int>> byLumpName;
};

std::shared_ptr<const ReaderIndexData> buildReaderIndex(const PackageArchiveReader& reader)
{
	auto data = std::make_shared<ReaderIndexData>();
	data->entries = reader.entries();
	for (int index = 0; index < data->entries.size(); ++index) {
		const PackageEntry& entry = data->entries.at(index);
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		data->byPath.insert(entry.virtualPath.toCaseFolded(), index);
		QString name = packageVirtualPathFileName(entry.virtualPath);
		const int dot = name.lastIndexOf(QLatin1Char('.'));
		if (dot > 0) {
			name.truncate(dot);
		}
		data->byLumpName[name.toUpper()].push_back(index);
	}
	return data;
}

// Readers are immutable snapshots, so their index is built once and shared
// by every material resolved from them (a browser resolves thousands).
std::shared_ptr<const ReaderIndexData> sharedReaderIndex(const std::shared_ptr<const PackageArchiveReader>& reader)
{
	struct Slot {
		std::weak_ptr<const PackageArchiveReader> reader;
		std::shared_ptr<const ReaderIndexData> data;
		quint64 used = 0;
	};
	static QMutex mutex;
	static QHash<const PackageArchiveReader*, Slot> readerSlots;
	static quint64 clock = 0;
	if (!reader) {
		static const auto empty = std::make_shared<const ReaderIndexData>();
		return empty;
	}
	{
		QMutexLocker lock(&mutex);
		const auto found = readerSlots.find(reader.get());
		if (found != readerSlots.end() && found->reader.lock() == reader) {
			found->used = ++clock;
			return found->data;
		}
	}
	std::shared_ptr<const ReaderIndexData> data = buildReaderIndex(*reader);
	QMutexLocker lock(&mutex);
	for (auto it = readerSlots.begin(); it != readerSlots.end();) {
		it = it->reader.expired() ? readerSlots.erase(it) : std::next(it);
	}
	while (readerSlots.size() >= 8) {
		auto oldest = readerSlots.begin();
		for (auto it = readerSlots.begin(); it != readerSlots.end(); ++it) {
			if (it->used < oldest->used) {
				oldest = it;
			}
		}
		readerSlots.erase(oldest);
	}
	readerSlots.insert(reader.get(), Slot {reader, data, ++clock});
	return data;
}

struct ReaderIndex {
	std::shared_ptr<const PackageArchiveReader> reader;
	std::shared_ptr<const ReaderIndexData> data;

	explicit ReaderIndex(std::shared_ptr<const PackageArchiveReader> source)
		: reader(std::move(source))
		, data(sharedReaderIndex(reader))
	{
	}
};

class Resolver {
public:
	Resolver(const MaterialDefinition& definition, const MaterialImageSource& source, std::function<bool()> cancelled, MaterialImageSet* set)
		: m_definition(definition), m_source(source), m_cancelled(std::move(cancelled)), m_set(set)
	{
		m_readers.push_back(ReaderIndex(source.archive));
		for (const auto& fallback : source.fallbacks) {
			m_readers.push_back(ReaderIndex(fallback));
		}
	}

	void run();

private:
	[[nodiscard]] bool cancelled() const
	{
		if (m_set->cancelled) {
			return true;
		}
		if (m_cancelled && m_cancelled()) {
			m_set->cancelled = true;
			m_set->complete = false;
			return true;
		}
		return false;
	}

	// Finds a path in the readers; returns the reader index and entry.
	bool findPath(const QString& path, int* reader, int* entry) const
	{
		const QString key = normalizedPath(path).toCaseFolded();
		for (int index = 0; index < m_readers.size(); ++index) {
			const auto found = m_readers.at(index).data->byPath.constFind(key);
			if (found != m_readers.at(index).data->byPath.constEnd()) {
				*reader = index;
				*entry = *found;
				return true;
			}
		}
		return false;
	}

	// A WAD lump by name, preferring a namespace type.
	bool findLump(const QString& name, const QStringList& preferredTypes, int* reader, int* entry) const
	{
		const QString key = name.trimmed().toUpper();
		for (int index = 0; index < m_readers.size(); ++index) {
			const ReaderIndex& readerIndex = m_readers.at(index);
			const QVector<int> candidates = readerIndex.data->byLumpName.value(key);
			int fallback = -1;
			for (int at = static_cast<int>(candidates.size()) - 1; at >= 0; --at) {
				const PackageEntry& candidate = readerIndex.data->entries.at(candidates.at(at));
				if (preferredTypes.contains(candidate.typeHint)) {
					*reader = index;
					*entry = candidates.at(at);
					return true;
				}
				if (fallback < 0) {
					fallback = candidates.at(at);
				}
			}
			if (fallback >= 0 && preferredTypes.isEmpty()) {
				*reader = index;
				*entry = fallback;
				return true;
			}
		}
		return false;
	}

	bool read(int reader, int entry, QByteArray* bytes, QString* error) const
	{
		const PackageEntry& item = m_readers.at(reader).data->entries.at(entry);
		if (item.sizeBytes > static_cast<quint64>(kImageReadLimit)) {
			*error = Text::tr("The image is larger than 64 MiB.");
			return false;
		}
		return m_readers.at(reader).reader->readEntryAt(entry, bytes, error, kImageReadLimit);
	}

	QString cacheKey(const QString& path) const
	{
		return m_source.revision.isEmpty() ? QString() : m_source.revision + QLatin1Char('|') + m_paletteKey + QLatin1Char('|') + path.toCaseFolded();
	}

	MaterialTexturePtr cached(const QString& path) const
	{
		const QString key = cacheKey(path);
		return key.isEmpty() || !m_source.cache ? MaterialTexturePtr() : m_source.cache->find(key);
	}

	void remember(const QString& path, const MaterialTexturePtr& texture) const
	{
		const QString key = cacheKey(path);
		if (!key.isEmpty() && m_source.cache && texture && texture->usable()) {
			m_source.cache->insert(key, texture);
		}
	}

	bool charge(const MaterialTexture& texture)
	{
		const qint64 bytes = static_cast<qint64>(texture.pixels.size()) * 5;
		if (m_bytes + bytes > m_source.byteBudget) {
			m_set->complete = false;
			if (!m_budgetWarned) {
				m_set->warnings << Text::tr("Image memory budget reached; some images are not shown.");
				m_budgetWarned = true;
			}
			return false;
		}
		m_bytes += bytes;
		return true;
	}

	MaterialTexturePtr missing(const QString& reference, const QStringList& candidates, const QString& note = QString())
	{
		auto texture = std::make_shared<MaterialTexture>();
		texture->reference = reference;
		texture->status = QStringLiteral("missing");
		texture->note = note.isEmpty() ? Text::tr("Not found. Looked for: %1").arg(candidates.join(QStringLiteral(", "))) : note;
		return texture;
	}

	// Decodes a file at `path` through the shared idTech decoders.
	MaterialTexturePtr decodePath(const QString& reference, const QString& path, int reader, int entry, const QString& decodePath = QString())
	{
		if (const MaterialTexturePtr hit = cached(path)) {
			return hit;
		}
		QByteArray bytes;
		QString error;
		if (!read(reader, entry, &bytes, &error)) {
			auto texture = std::make_shared<MaterialTexture>();
			texture->reference = reference;
			texture->path = path;
			texture->status = QStringLiteral("unreadable");
			texture->note = error;
			return texture;
		}
		IdTechImageDecodeContext context;
		context.archive = m_readers.at(reader).reader.get();
		context.maximumDimension = 8192;
		context.isCancelled = m_cancelled;
		const IdTechImageDecodeResult decoded = decodeIdTechImage(decodePath.isEmpty() ? path : decodePath, bytes, m_palette, context);
		if (!decoded.decoded || decoded.image.isNull()) {
			auto texture = std::make_shared<MaterialTexture>();
			texture->reference = reference;
			texture->path = path;
			texture->status = QStringLiteral("unreadable");
			texture->note = decoded.error;
			return texture;
		}
		QImage image = decoded.image;
		if (image.width() > m_source.maximumDimension || image.height() > m_source.maximumDimension) {
			// Keep indexed images whole; they are small. Scale the others.
			if (image.format() != QImage::Format_Indexed8) {
				image = image.convertToFormat(QImage::Format_ARGB32)
							.scaled(m_source.maximumDimension, m_source.maximumDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			}
		}
		auto texture = std::make_shared<MaterialTexture>(materialTextureFromImage(image, reference, path, QStringLiteral("image")));
		if (!charge(*texture)) {
			return missing(reference, {path}, Text::tr("Image memory budget reached."));
		}
		texture->buildMips();
		remember(path, texture);
		return texture;
	}

	MaterialTexturePtr loadByCandidates(const QString& reference, const QStringList& candidates)
	{
		for (const QString& candidate : candidates) {
			int reader = -1;
			int entry = -1;
			if (findPath(candidate, &reader, &entry)) {
				return decodePath(reference, m_readers.at(reader).data->entries.at(entry).virtualPath, reader, entry);
			}
		}
		return missing(reference, candidates);
	}

	void store(const QString& reference, const MaterialTexturePtr& texture)
	{
		if (!reference.trimmed().isEmpty() && texture) {
			m_set->textures.insert(materialImageKey(reference), texture);
		}
	}

	void resolveQuake3();
	void resolveDoom3();
	void resolveDoom();
	void resolveQuake();
	void resolveQuake2();
	void resolvePalette(const QString& defaultId);
	void resolveColormaps();
	void resolveFile(const QString& reference);
	void resolveSkyBox(const QString& base, bool underscore, const QStringList& extensions);
	void resolveDoom3Cube(const QString& reference, bool camera);
	QImage evaluateProgram(int node, QString* status, QString* note, int depth = 0);
	QImage loadDoom3Leaf(const QString& path, QString* status, QString* note);
	MaterialTexturePtr doomComposite(const QString& name);
	MaterialTexturePtr doomFlat(const QString& name);
	MaterialTexturePtr doomPatchTexture(const QString& name);

	const MaterialDefinition& m_definition;
	const MaterialImageSource& m_source;
	std::function<bool()> m_cancelled;
	MaterialImageSet* m_set = nullptr;
	QVector<ReaderIndex> m_readers;
	IdTechPalette m_palette;
	QString m_paletteKey;
	qint64 m_bytes = 0;
	bool m_budgetWarned = false;
	std::shared_ptr<const DoomMaterialCatalog> m_doomCatalog;
	QHash<QString, QImage> m_leafCache;
};

void Resolver::resolvePalette(const QString& defaultId)
{
	QString paletteId = m_source.paletteId.trimmed();
	if (paletteId.isEmpty() || paletteId == QStringLiteral("auto")) {
		paletteId = m_source.archive ? idTechPaletteIdInPackage(*m_source.archive) : QString();
		if (paletteId.isEmpty()) {
			for (const auto& fallback : m_source.fallbacks) {
				if (fallback) {
					paletteId = idTechPaletteIdInPackage(*fallback);
					if (!paletteId.isEmpty()) {
						break;
					}
				}
			}
		}
		if (paletteId.isEmpty()) {
			paletteId = defaultId;
		}
	}
	IdTechPaletteResolution resolution;
	bool found = false;
	for (const ReaderIndex& reader : m_readers) {
		if (!reader.reader) {
			continue;
		}
		resolution = resolveIdTechPalette(*reader.reader, paletteId);
		if (resolution.fromPackage) {
			found = true;
			break;
		}
	}
	if (!found) {
		resolution.palette = generatedIdTechPalette(paletteId);
		// Quake III and Doom 3 images carry their own colours (a PCX its own
		// palette), so only the paletted engines miss a game palette.
		if (defaultId != QStringLiteral("generic")) {
			m_set->warnings << Text::tr("No game palette was found; a generated palette stands in, so colours are not the game's.");
		}
	}
	m_palette = resolution.palette;
	m_paletteKey = paletteId + (found ? QStringLiteral(":real") : QStringLiteral(":generated"));
	m_set->palette = m_palette.colors;
	m_set->paletteGenerated = !found || m_palette.generated;
	m_set->paletteSource = found ? resolution.sourceVirtualPath : m_palette.displayName;
}

// Nearest palette entry to a colour, ignoring the transparent index.
int nearestIndex(const QVector<QRgb>& palette, int r, int g, int b, int skip = -1)
{
	int best = 0;
	int bestDistance = std::numeric_limits<int>::max();
	for (int index = 0; index < palette.size(); ++index) {
		if (index == skip) {
			continue;
		}
		const QRgb color = palette.at(index);
		const int dr = qRed(color) - r;
		const int dg = qGreen(color) - g;
		const int db = qBlue(color) - b;
		const int distance = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (distance < bestDistance) {
			bestDistance = distance;
			best = index;
		}
	}
	return best;
}

void Resolver::resolveColormaps()
{
	const QVector<QRgb>& palette = m_set->palette;
	if (palette.size() < 256) {
		return;
	}
	if (m_definition.engine == MaterialEngine::Doom) {
		int reader = -1;
		int entry = -1;
		QByteArray bytes;
		QString error;
		if (findLump(QStringLiteral("COLORMAP"), {}, &reader, &entry) && read(reader, entry, &bytes, &error) && bytes.size() >= 34 * 256) {
			m_set->doomColormap = bytes.left(34 * 256);
			m_set->doomColormapGenerated = false;
			return;
		}
		// A stand-in: 32 rows darkening linearly towards black, an inverse
		// grey row for invulnerability, and a black row.
		QByteArray colormap(34 * 256, '\0');
		for (int row = 0; row < 32; ++row) {
			const double factor = 1.0 - row / 32.0;
			for (int index = 0; index < 256; ++index) {
				const QRgb color = palette.at(index);
				colormap[row * 256 + index] = static_cast<char>(nearestIndex(palette, static_cast<int>(qRed(color) * factor),
					static_cast<int>(qGreen(color) * factor), static_cast<int>(qBlue(color) * factor)));
			}
		}
		for (int index = 0; index < 256; ++index) {
			const QRgb color = palette.at(index);
			const int grey = 255 - qGray(color);
			colormap[32 * 256 + index] = static_cast<char>(nearestIndex(palette, grey, grey, grey));
			colormap[33 * 256 + index] = static_cast<char>(nearestIndex(palette, 0, 0, 0));
		}
		m_set->doomColormap = colormap;
		m_set->doomColormapGenerated = true;
		m_set->warnings << Text::tr("No COLORMAP lump was found; lighting uses a generated colormap.");
		return;
	}
	if (m_definition.engine == MaterialEngine::Quake) {
		int reader = -1;
		int entry = -1;
		QByteArray bytes;
		QString error;
		if (findPath(QStringLiteral("gfx/colormap.lmp"), &reader, &entry) && read(reader, entry, &bytes, &error) && bytes.size() >= 64 * 256) {
			m_set->quakeColormap = bytes.left(64 * 256);
			m_set->quakeColormapGenerated = false;
			return;
		}
		// Rows 0-63 run from about twice as bright to black, row 32 normal;
		// the fullbright range 224-255 is copied unchanged into every row,
		// as qlumpy's GrabColormap does.
		QByteArray colormap(64 * 256, '\0');
		for (int row = 0; row < 64; ++row) {
			const double factor = (64 - row) / 32.0;
			for (int index = 0; index < 256; ++index) {
				if (index >= 224) {
					colormap[row * 256 + index] = static_cast<char>(index);
					continue;
				}
				const QRgb color = palette.at(index);
				colormap[row * 256 + index] = static_cast<char>(nearestIndex(palette, static_cast<int>(qRed(color) * factor),
					static_cast<int>(qGreen(color) * factor), static_cast<int>(qBlue(color) * factor), 255));
			}
		}
		m_set->quakeColormap = colormap;
		m_set->quakeColormapGenerated = true;
	}
}

void Resolver::resolveFile(const QString& reference)
{
	if (reference.trimmed().isEmpty() || m_set->textures.contains(materialImageKey(reference))) {
		return;
	}
	const QString key = reference.trimmed().toLower();
	if (key == QStringLiteral("$whiteimage") || key == QStringLiteral("*white") || key == QStringLiteral("$lightmap")) {
		store(reference, materialBuiltinTexture(QStringLiteral("_white")));
		return;
	}
	if (MaterialTexturePtr builtin = materialBuiltinTexture(key, m_source.developerDefault)) {
		store(reference, builtin);
		return;
	}
	const QStringList candidates = materialImageCandidates(m_definition.engine, reference, m_source.ioquake3Extensions);
	store(reference, loadByCandidates(reference, candidates));
}

void Resolver::resolveSkyBox(const QString& base, bool underscore, const QStringList& extensions)
{
	if (base.isEmpty() || base == QStringLiteral("-") || m_set->cubes.contains(materialImageKey(base))) {
		return;
	}
	auto cube = std::make_shared<MaterialCubeTexture>();
	cube->reference = base;
	cube->status = QStringLiteral("quake-sky");
	// rt bk lf ft up dn are +X +Y -X -Y +Z -Z; stored in +X -X +Y -Y +Z -Z.
	const QStringList suffixes = quake3SkyFaceSuffixes();
	const int faceOf[6] = {0, 2, 1, 3, 4, 5};
	int ready = 0;
	for (int index = 0; index < 6; ++index) {
		const QString name = base + (underscore ? QStringLiteral("_") : QString()) + suffixes.at(index);
		QStringList candidates;
		for (const QString& extension : extensions) {
			candidates << name + QLatin1Char('.') + extension;
		}
		MaterialTexturePtr texture = loadByCandidates(name, candidates);
		ready += texture && texture->usable() ? 1 : 0;
		cube->faces[static_cast<size_t>(faceOf[index])] = texture;
	}
	if (ready < 6) {
		cube->note = Text::tr("%1 of 6 sky box faces were found.").arg(ready);
	}
	m_set->cubes.insert(materialImageKey(base), cube);
}

void Resolver::resolveQuake3()
{
	resolvePalette(QStringLiteral("generic"));
	for (const MaterialStage& stage : m_definition.stages) {
		if (cancelled()) {
			return;
		}
		switch (stage.imageKind) {
		case MaterialImageKind::File:
			resolveFile(stage.imagePath);
			break;
		case MaterialImageKind::Animation:
			for (const QString& frame : stage.animationFrames) {
				resolveFile(frame);
			}
			break;
		case MaterialImageKind::Video:
			store(stage.imagePath, materialVideoPlaceholder());
			break;
		default:
			break;
		}
	}
	if (m_definition.sky.present) {
		resolveSkyBox(m_definition.sky.farBox, true, {QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("png")});
	}
	if (!m_definition.editorImage.isEmpty()) {
		resolveFile(m_definition.editorImage);
	}
	store(QStringLiteral("$quake3default"), quake3DefaultTexture());
}

QImage Resolver::loadDoom3Leaf(const QString& path, QString* status, QString* note)
{
	const QString key = path.trimmed().toLower();
	if (m_leafCache.contains(key)) {
		return m_leafCache.value(key);
	}
	if (MaterialTexturePtr builtin = materialBuiltinTexture(key, m_source.developerDefault)) {
		const QImage image = builtin->toImage();
		m_leafCache.insert(key, image);
		return image;
	}
	const QStringList candidates = materialImageCandidates(MaterialEngine::Doom3, path, false);
	const MaterialTexturePtr texture = loadByCandidates(path, candidates);
	if (!texture->usable()) {
		*status = texture->status;
		*note = texture->note;
		return {};
	}
	const QImage image = texture->toImage();
	m_leafCache.insert(key, image);
	return image;
}

QImage Resolver::evaluateProgram(int nodeIndex, QString* status, QString* note, int depth)
{
	if (nodeIndex < 0 || nodeIndex >= m_definition.imagePrograms.size() || depth > 16 || cancelled()) {
		return {};
	}
	const MaterialImageProgramNode& node = m_definition.imagePrograms.at(nodeIndex);
	if (node.function.isEmpty()) {
		return loadDoom3Leaf(node.path, status, note);
	}
	QImage first = evaluateProgram(node.children.value(0, -1), status, note, depth + 1);
	if (first.isNull()) {
		return {};
	}
	first = first.convertToFormat(QImage::Format_ARGB32);
	const QString function = node.function;
	if (function == QStringLiteral("heightmap")) {
		return doom3HeightmapToNormalMap(first, node.numbers.value(0, 1.0));
	}
	if (function == QStringLiteral("addnormals") || function == QStringLiteral("add")) {
		QImage second = evaluateProgram(node.children.value(1, -1), status, note, depth + 1);
		if (second.isNull()) {
			return {};
		}
		second = second.convertToFormat(QImage::Format_ARGB32);
		if (function == QStringLiteral("addnormals")) {
			return doom3AddNormalMaps(first, second);
		}
		// R_ImageAdd: a saturating add per channel, the second image point
		// sampled to the first's size.
		if (second.size() != first.size()) {
			second = second.scaled(first.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
		}
		for (int y = 0; y < first.height(); ++y) {
			auto* a = reinterpret_cast<QRgb*>(first.scanLine(y));
			const auto* b = reinterpret_cast<const QRgb*>(second.constScanLine(y));
			for (int x = 0; x < first.width(); ++x) {
				a[x] = rgba(qRed(a[x]) + qRed(b[x]), qGreen(a[x]) + qGreen(b[x]), qBlue(a[x]) + qBlue(b[x]), qAlpha(a[x]) + qAlpha(b[x]));
			}
		}
		return first;
	}
	if (function == QStringLiteral("smoothnormals")) {
		return doom3SmoothNormalMap(first);
	}
	for (int y = 0; y < first.height(); ++y) {
		auto* line = reinterpret_cast<QRgb*>(first.scanLine(y));
		for (int x = 0; x < first.width(); ++x) {
			const QRgb c = line[x];
			if (function == QStringLiteral("scale")) {
				// R_ImageScale casts to a byte before clamping, so values
				// above 255 wrap.
				const auto scaled = [&](int value, int at) {
					return static_cast<int>(static_cast<unsigned char>(static_cast<int>(value * node.numbers.value(at, 1.0))));
				};
				line[x] = qRgba(scaled(qRed(c), 0), scaled(qGreen(c), 1), scaled(qBlue(c), 2), scaled(qAlpha(c), 3));
			} else if (function == QStringLiteral("invertalpha")) {
				line[x] = qRgba(qRed(c), qGreen(c), qBlue(c), 255 - qAlpha(c));
			} else if (function == QStringLiteral("invertcolor")) {
				line[x] = qRgba(255 - qRed(c), 255 - qGreen(c), 255 - qBlue(c), qAlpha(c));
			} else if (function == QStringLiteral("makeintensity")) {
				line[x] = qRgba(qRed(c), qRed(c), qRed(c), qRed(c));
			} else if (function == QStringLiteral("makealpha")) {
				line[x] = qRgba(255, 255, 255, (qRed(c) + qGreen(c) + qBlue(c)) / 3);
			}
		}
	}
	return first;
}

void Resolver::resolveDoom3Cube(const QString& reference, bool camera)
{
	if (reference.isEmpty() || m_set->cubes.contains(materialImageKey(reference))) {
		return;
	}
	auto cube = std::make_shared<MaterialCubeTexture>();
	cube->reference = reference;
	cube->status = camera ? QStringLiteral("camera") : QStringLiteral("gl");
	const QStringList axis {QStringLiteral("_px"), QStringLiteral("_nx"), QStringLiteral("_py"), QStringLiteral("_ny"),
		QStringLiteral("_pz"), QStringLiteral("_nz")};
	const QStringList sides {QStringLiteral("_forward"), QStringLiteral("_back"), QStringLiteral("_left"), QStringLiteral("_right"),
		QStringLiteral("_up"), QStringLiteral("_down")};
	int ready = 0;
	for (int face = 0; face < 6; ++face) {
		const QString name = reference + (camera ? sides.at(face) : axis.at(face));
		QString status;
		QString note;
		QImage image = loadDoom3Leaf(name, &status, &note);
		if (image.isNull()) {
			auto texture = std::make_shared<MaterialTexture>();
			texture->reference = name;
			texture->status = QStringLiteral("missing");
			texture->note = note;
			cube->faces[static_cast<size_t>(face)] = texture;
			continue;
		}
		image = image.convertToFormat(QImage::Format_ARGB32);
		if (camera && image.width() == image.height()) {
			// Image_load.cpp turns camera shots into cube faces: forward,
			// up and down are transposed, back is transposed and flipped
			// both ways, left is flipped vertically, right horizontally.
			const auto transpose = [](const QImage& source) {
				QImage result(source.height(), source.width(), QImage::Format_ARGB32);
				for (int y = 0; y < source.height(); ++y) {
					const auto* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
					for (int x = 0; x < source.width(); ++x) {
						reinterpret_cast<QRgb*>(result.scanLine(x))[y] = line[x];
					}
				}
				return result;
			};
			switch (face) {
			case 0:
			case 4:
			case 5:
				image = transpose(image);
				break;
			case 1:
				image = transpose(image).flipped(Qt::Horizontal | Qt::Vertical);
				break;
			case 2:
				image = image.flipped(Qt::Vertical);
				break;
			case 3:
				image = image.flipped(Qt::Horizontal);
				break;
			default:
				break;
			}
		}
		auto texture = std::make_shared<MaterialTexture>(materialTextureFromImage(image, name, name, QStringLiteral("image")));
		texture->buildMips();
		cube->faces[static_cast<size_t>(face)] = texture;
		++ready;
	}
	if (ready < 6) {
		cube->note = Text::tr("%1 of 6 cube faces were found.").arg(ready);
	}
	m_set->cubes.insert(materialImageKey(reference), cube);
}

void Resolver::resolveDoom3()
{
	resolvePalette(QStringLiteral("generic"));
	const auto resolveProgram = [&](int program, const QString& reference) {
		if (program < 0 || reference.isEmpty() || m_set->textures.contains(materialImageKey(reference))) {
			return;
		}
		QString status = QStringLiteral("program");
		QString note;
		const QImage image = evaluateProgram(program, &status, &note);
		if (image.isNull()) {
			auto texture = std::make_shared<MaterialTexture>();
			texture->reference = reference;
			texture->status = status == QStringLiteral("program") ? QStringLiteral("missing") : status;
			texture->note = note;
			store(reference, texture);
			return;
		}
		const bool leaf = m_definition.imagePrograms.at(program).function.isEmpty();
		const QString key = reference.trimmed().toLower();
		const bool builtin = leaf && key.startsWith(QLatin1Char('_'));
		auto texture = std::make_shared<MaterialTexture>(materialTextureFromImage(image, reference, reference,
			builtin ? QStringLiteral("builtin") : (leaf ? QStringLiteral("image") : QStringLiteral("program"))));
		if (!charge(*texture)) {
			return;
		}
		texture->buildMips();
		store(reference, texture);
	};
	for (const MaterialStage& stage : m_definition.stages) {
		if (cancelled()) {
			return;
		}
		if (stage.imageKind == MaterialImageKind::CubeMap) {
			resolveDoom3Cube(stage.imagePath, stage.cameraCubeMap);
			continue;
		}
		if (stage.imageKind == MaterialImageKind::Video) {
			store(stage.imagePath, materialVideoPlaceholder());
			continue;
		}
		if (stage.imageProgram >= 0) {
			resolveProgram(stage.imageProgram, materialImageProgramText(m_definition.imagePrograms, stage.imageProgram));
		}
	}
	// lightFalloffImage is itself an image program text.
	if (!m_definition.lightFalloffImage.isEmpty()) {
		for (int index = 0; index < m_definition.imagePrograms.size(); ++index) {
			if (materialImageProgramText(m_definition.imagePrograms, index) == m_definition.lightFalloffImage) {
				resolveProgram(index, m_definition.lightFalloffImage);
				break;
			}
		}
	}
	for (const QString& builtin : {QStringLiteral("_flat"), QStringLiteral("_white"), QStringLiteral("_black"), QStringLiteral("_default")}) {
		store(builtin, materialBuiltinTexture(builtin, m_source.developerDefault));
	}
	if (!m_definition.editorImage.isEmpty()) {
		QString status;
		QString note;
		const QImage image = loadDoom3Leaf(m_definition.editorImage, &status, &note);
		if (!image.isNull()) {
			auto texture = std::make_shared<MaterialTexture>(materialTextureFromImage(image, m_definition.editorImage, m_definition.editorImage,
				QStringLiteral("image")));
			texture->buildMips();
			store(m_definition.editorImage, texture);
		}
	}
}

MaterialTexturePtr Resolver::doomPatchTexture(const QString& name)
{
	int reader = -1;
	int entry = -1;
	if (!findLump(name, {QStringLiteral("wad-patch")}, &reader, &entry) && !findLump(name, {}, &reader, &entry)) {
		return missing(name, {name});
	}
	QByteArray bytes;
	QString error;
	if (!read(reader, entry, &bytes, &error)) {
		return missing(name, {name}, error);
	}
	QVector<int> indices;
	QSize size;
	if (!decodeDoomPatchIndices(bytes, &indices, &size, nullptr, &error)) {
		// A PNG or other picture in a port's namespace.
		return decodePath(name, m_readers.at(reader).data->entries.at(entry).virtualPath, reader, entry);
	}
	auto texture = std::make_shared<MaterialTexture>();
	texture->width = size.width();
	texture->height = size.height();
	texture->pixels.resize(indices.size());
	texture->indices.resize(indices.size());
	texture->palette = m_set->palette;
	for (qsizetype at = 0; at < indices.size(); ++at) {
		const int index = indices.at(at);
		texture->indices[at] = static_cast<quint8>(std::max(0, index));
		texture->pixels[at] = index < 0 ? qRgba(0, 0, 0, 0) : (m_set->palette.value(index) | 0xff000000u);
	}
	texture->reference = name;
	texture->path = m_readers.at(reader).data->entries.at(entry).virtualPath;
	texture->status = QStringLiteral("image");
	return texture;
}

MaterialTexturePtr Resolver::doomComposite(const QString& name)
{
	const QString key = name.trimmed().toUpper();
	if (const MaterialTexturePtr hit = cached(QStringLiteral("doom-composite:") + key)) {
		return hit;
	}
	const int index = m_doomCatalog ? m_doomCatalog->textureIndex(key) : -1;
	if (index < 0) {
		// Ports also take walls straight from TX_ lumps or textures/ files.
		int reader = -1;
		int entry = -1;
		if (findLump(key, {QStringLiteral("wad-texture")}, &reader, &entry)) {
			return decodePath(name, m_readers.at(reader).data->entries.at(entry).virtualPath, reader, entry);
		}
		return missing(name, {QStringLiteral("TEXTURE1/TEXTURE2")}, Text::tr("No TEXTURE1/TEXTURE2 definition or direct texture is named %1.").arg(key));
	}
	const DoomCompositeTexture& composite = m_doomCatalog->textures.at(index);
	if (composite.size.width() <= 0 || composite.size.height() <= 0 || composite.size.width() > 4096 || composite.size.height() > 4096) {
		return missing(name, {}, Text::tr("Texture %1 has an invalid size.").arg(key));
	}
	const int width = composite.size.width();
	const int height = composite.size.height();
	QVector<int> indices(static_cast<qsizetype>(width) * height, -1);
	QStringList problems;
	for (const MaterialPatchPlacement& placement : composite.patches) {
		if (cancelled()) {
			return {};
		}
		const MaterialTexturePtr patch = doomPatchTexture(placement.patch);
		if (!patch || !patch->usable()) {
			problems << placement.patch;
			continue;
		}
		for (int y = 0; y < patch->height; ++y) {
			const int targetY = placement.y + y;
			if (targetY < 0 || targetY >= height) {
				continue;
			}
			for (int x = 0; x < patch->width; ++x) {
				const int targetX = placement.x + x;
				if (targetX < 0 || targetX >= width) {
					continue;
				}
				const qsizetype source = static_cast<qsizetype>(y) * patch->width + x;
				if (qAlpha(patch->pixels.at(source)) == 0) {
					continue;
				}
				indices[static_cast<qsizetype>(targetY) * width + targetX] = patch->hasIndices()
					? patch->indices.at(source)
					: nearestIndex(m_set->palette, qRed(patch->pixels.at(source)), qGreen(patch->pixels.at(source)), qBlue(patch->pixels.at(source)));
			}
		}
	}
	auto texture = std::make_shared<MaterialTexture>();
	texture->width = width;
	texture->height = height;
	texture->pixels.resize(indices.size());
	texture->indices.resize(indices.size());
	texture->palette = m_set->palette;
	for (qsizetype at = 0; at < indices.size(); ++at) {
		const int value = indices.at(at);
		texture->indices[at] = static_cast<quint8>(std::max(0, value));
		texture->pixels[at] = value < 0 ? qRgba(0, 0, 0, 0) : (m_set->palette.value(value) | 0xff000000u);
	}
	texture->reference = name;
	texture->path = composite.lump;
	texture->status = QStringLiteral("composite");
	texture->note = problems.isEmpty() ? Text::tr("Composed from %1 patch(es) in %2.").arg(composite.patches.size()).arg(composite.lump)
									   : Text::tr("Missing patches: %1").arg(problems.join(QStringLiteral(", ")));
	if (!problems.isEmpty()) {
		texture->status = QStringLiteral("composite");
	}
	if (!charge(*texture)) {
		return missing(name, {}, Text::tr("Image memory budget reached."));
	}
	remember(QStringLiteral("doom-composite:") + key, texture);
	return texture;
}

MaterialTexturePtr Resolver::doomFlat(const QString& name)
{
	int reader = -1;
	int entry = -1;
	if (!findLump(name, {QStringLiteral("wad-flat")}, &reader, &entry)) {
		// ZDoom PK3s keep flats in flats/.
		bool found = false;
		const QString key = name.trimmed().toUpper();
		for (int index = 0; index < m_readers.size() && !found; ++index) {
			for (int candidate : m_readers.at(index).data->byLumpName.value(key)) {
				if (packageVirtualPathParent(m_readers.at(index).data->entries.at(candidate).virtualPath).compare(QStringLiteral("flats"), Qt::CaseInsensitive) == 0) {
					reader = index;
					entry = candidate;
					found = true;
					break;
				}
			}
		}
		if (!found) {
			return missing(name, {QStringLiteral("F_START..F_END"), QStringLiteral("flats/") + name});
		}
	}
	const QString path = m_readers.at(reader).data->entries.at(entry).virtualPath;
	return decodePath(name, path, reader, entry, QStringLiteral("flats/") + packageVirtualPathFileName(path));
}

void Resolver::resolveDoom()
{
	resolvePalette(QStringLiteral("doom"));
	m_doomCatalog = m_source.doomCatalog;
	if (!m_doomCatalog && m_source.archive) {
		m_doomCatalog = std::make_shared<DoomMaterialCatalog>(readDoomMaterialCatalog(*m_source.archive));
	}
	resolveColormaps();
	const bool flat = m_definition.kind == QStringLiteral("doom-flat");
	QStringList names {m_definition.name};
	for (const MaterialFrame& frame : m_definition.classic.frames) {
		if (!names.contains(frame.name, Qt::CaseInsensitive)) {
			names << frame.name;
		}
	}
	if (!m_definition.classic.switchPartner.isEmpty()) {
		names << m_definition.classic.switchPartner;
	}
	for (const QString& name : names) {
		if (cancelled()) {
			return;
		}
		store(name, flat ? doomFlat(name) : doomComposite(name));
	}
	if (flat && m_definition.name.compare(QStringLiteral("F_SKY1"), Qt::CaseInsensitive) == 0) {
		const QString sky = m_source.doomSky.isEmpty() ? QStringLiteral("SKY1") : m_source.doomSky;
		store(QStringLiteral("$doomsky"), doomComposite(sky));
	}
}

void Resolver::resolveQuake()
{
	resolvePalette(QStringLiteral("quake"));
	resolveColormaps();
	QStringList names {m_definition.name};
	for (const MaterialFrame& frame : m_definition.classic.frames + m_definition.classic.alternateFrames) {
		if (!names.contains(frame.name, Qt::CaseInsensitive)) {
			names << frame.name;
		}
	}
	for (const QString& name : names) {
		if (cancelled()) {
			return;
		}
		int reader = -1;
		int entry = -1;
		// WAD2/WAD3 lumps first, then a port's loose textures/<name>.
		if (findLump(name, {QStringLiteral("wad-texture")}, &reader, &entry)) {
			store(name, decodePath(name, m_readers.at(reader).data->entries.at(entry).virtualPath, reader, entry));
			continue;
		}
		QStringList candidates;
		for (const QString& extension : {QStringLiteral("tga"), QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("pcx")}) {
			candidates << QStringLiteral("textures/%1.%2").arg(name, extension);
		}
		store(name, loadByCandidates(name, candidates));
	}
	for (const QString& suffix : quakeCompanionSuffixes()) {
		const QString name = m_definition.name + suffix;
		QStringList candidates;
		for (const QString& extension : {QStringLiteral("tga"), QStringLiteral("png"), QStringLiteral("jpg")}) {
			candidates << QStringLiteral("textures/%1.%2").arg(name, extension);
		}
		int reader = -1;
		int entry = -1;
		for (const QString& candidate : candidates) {
			if (findPath(candidate, &reader, &entry)) {
				store(name, decodePath(name, m_readers.at(reader).data->entries.at(entry).virtualPath, reader, entry));
				break;
			}
		}
	}
}

void Resolver::resolveQuake2()
{
	resolvePalette(QStringLiteral("quake2"));
	QStringList names {m_definition.name};
	for (const MaterialFrame& frame : m_definition.classic.frames) {
		if (!names.contains(frame.name, Qt::CaseInsensitive)) {
			names << frame.name;
		}
	}
	for (const QString& name : names) {
		if (cancelled()) {
			return;
		}
		const QString wal = quake2WalPath(name);
		const QString base = wal.left(wal.size() - 4);
		store(name, loadByCandidates(name, {wal, base + QStringLiteral(".tga"), base + QStringLiteral(".png"), base + QStringLiteral(".jpg")}));
	}
	if ((m_definition.classic.surfaceFlags & kQuake2SurfSky) != 0) {
		QString sky = m_source.quake2Sky;
		if (sky.isEmpty()) {
			// The first sky box the package ships.
			for (const ReaderIndex& reader : m_readers) {
				for (const PackageEntry& entry : reader.data->entries) {
					const QString path = entry.virtualPath.toLower();
					if (path.startsWith(QStringLiteral("env/")) && (path.endsWith(QStringLiteral("rt.tga")) || path.endsWith(QStringLiteral("rt.pcx")))) {
						sky = entry.virtualPath.mid(4, entry.virtualPath.size() - 4 - 6);
						break;
					}
				}
				if (!sky.isEmpty()) {
					break;
				}
			}
		}
		if (!sky.isEmpty()) {
			resolveSkyBox(QStringLiteral("env/") + sky, false, {QStringLiteral("tga"), QStringLiteral("pcx")});
			store(QStringLiteral("$quake2sky"), materialBuiltinTexture(QStringLiteral("_white")));
			m_set->warnings << Text::tr("Sky surfaces show the env/%1 sky box.").arg(sky);
		}
	}
}

void Resolver::run()
{
	switch (m_definition.engine) {
	case MaterialEngine::Quake3:
		resolveQuake3();
		break;
	case MaterialEngine::Doom3:
		resolveDoom3();
		break;
	case MaterialEngine::Doom:
		resolveDoom();
		break;
	case MaterialEngine::Quake:
		resolveQuake();
		break;
	case MaterialEngine::Quake2:
		resolveQuake2();
		break;
	case MaterialEngine::Unknown:
		break;
	}
	for (auto it = m_set->textures.cbegin(); it != m_set->textures.cend(); ++it) {
		if (it.value() && !it.value()->usable()) {
			m_set->complete = false;
		}
	}
}

} // namespace

void MaterialTexture::buildMips()
{
	mipPixels.clear();
	mipSizes.clear();
	if (!isValid()) {
		return;
	}
	int width0 = width;
	int height0 = height;
	const QVector<QRgb>* source = &pixels;
	while (width0 > 1 || height0 > 1) {
		const int nextWidth = std::max(1, width0 / 2);
		const int nextHeight = std::max(1, height0 / 2);
		QVector<QRgb> next(static_cast<qsizetype>(nextWidth) * nextHeight);
		for (int y = 0; y < nextHeight; ++y) {
			for (int x = 0; x < nextWidth; ++x) {
				int r = 0;
				int g = 0;
				int b = 0;
				int a = 0;
				int count = 0;
				for (int dy = 0; dy < 2; ++dy) {
					for (int dx = 0; dx < 2; ++dx) {
						const int sx = std::min(width0 - 1, x * 2 + dx);
						const int sy = std::min(height0 - 1, y * 2 + dy);
						const QRgb c = source->at(static_cast<qsizetype>(sy) * width0 + sx);
						r += qRed(c);
						g += qGreen(c);
						b += qBlue(c);
						a += qAlpha(c);
						++count;
					}
				}
				next[static_cast<qsizetype>(y) * nextWidth + x] = qRgba(r / count, g / count, b / count, a / count);
			}
		}
		mipPixels.push_back(next);
		mipSizes.push_back(QSize(nextWidth, nextHeight));
		source = &mipPixels.last();
		width0 = nextWidth;
		height0 = nextHeight;
		if (mipPixels.size() > 16) {
			break;
		}
	}
}

QImage MaterialTexture::toImage() const
{
	if (!isValid()) {
		return {};
	}
	QImage image(width, height, QImage::Format_ARGB32);
	for (int y = 0; y < height; ++y) {
		std::copy_n(pixels.constData() + static_cast<qsizetype>(y) * width, width, reinterpret_cast<QRgb*>(image.scanLine(y)));
	}
	return image;
}

bool MaterialCubeTexture::isValid() const
{
	for (const MaterialTexturePtr& face : faces) {
		if (!face || !face->usable()) {
			return false;
		}
	}
	return true;
}

MaterialTexture materialTextureFromImage(const QImage& image, const QString& reference, const QString& path, const QString& status)
{
	MaterialTexture texture;
	texture.reference = reference;
	texture.path = path;
	texture.status = status;
	if (image.isNull()) {
		texture.status = QStringLiteral("unreadable");
		return texture;
	}
	texture.width = image.width();
	texture.height = image.height();
	texture.pixels.resize(static_cast<qsizetype>(texture.width) * texture.height);
	if (image.format() == QImage::Format_Indexed8) {
		QVector<QRgb> table = image.colorTable();
		table.resize(256);
		texture.palette = table;
		texture.indices.resize(texture.pixels.size());
		for (int y = 0; y < texture.height; ++y) {
			const uchar* line = image.constScanLine(y);
			for (int x = 0; x < texture.width; ++x) {
				const qsizetype at = static_cast<qsizetype>(y) * texture.width + x;
				texture.indices[at] = line[x];
				texture.pixels[at] = table.at(line[x]);
			}
		}
		return texture;
	}
	const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < texture.height; ++y) {
		std::copy_n(reinterpret_cast<const QRgb*>(argb.constScanLine(y)), texture.width,
			texture.pixels.data() + static_cast<qsizetype>(y) * texture.width);
	}
	return texture;
}

MaterialImageCache::MaterialImageCache(qint64 byteLimit)
	: m_limit(std::max<qint64>(16LL * 1024 * 1024, byteLimit))
{
}

MaterialTexturePtr MaterialImageCache::find(const QString& key) const
{
	QMutexLocker locker(&m_mutex);
	const auto found = m_slots.constFind(key);
	if (found == m_slots.constEnd()) {
		return {};
	}
	found->used = ++m_clock;
	return found->texture;
}

void MaterialImageCache::insert(const QString& key, const MaterialTexturePtr& texture)
{
	if (!texture) {
		return;
	}
	QMutexLocker locker(&m_mutex);
	qint64 bytes = static_cast<qint64>(texture->pixels.size()) * 4 + static_cast<qint64>(texture->indices.size());
	for (const auto& mip : texture->mipPixels) {
		bytes += static_cast<qint64>(mip.size()) * 4;
	}
	if (bytes > m_limit) {
		return;
	}
	if (m_slots.contains(key)) {
		m_bytes -= m_slots.value(key).bytes;
	}
	while (m_bytes + bytes > m_limit && !m_slots.isEmpty()) {
		auto oldest = m_slots.begin();
		for (auto it = m_slots.begin(); it != m_slots.end(); ++it) {
			if (it->used < oldest->used) {
				oldest = it;
			}
		}
		m_bytes -= oldest->bytes;
		m_slots.erase(oldest);
	}
	Slot slot;
	slot.texture = texture;
	slot.bytes = bytes;
	slot.used = ++m_clock;
	m_slots.insert(key, slot);
	m_bytes += bytes;
}

void MaterialImageCache::clear()
{
	QMutexLocker locker(&m_mutex);
	m_slots.clear();
	m_bytes = 0;
}

qint64 MaterialImageCache::bytes() const
{
	QMutexLocker locker(&m_mutex);
	return m_bytes;
}

MaterialTexturePtr MaterialImageSet::find(const QString& reference) const
{
	return textures.value(materialImageKey(reference));
}

MaterialCubeTexturePtr MaterialImageSet::findCube(const QString& reference) const
{
	return cubes.value(materialImageKey(reference));
}

QStringList MaterialImageSet::missing() const
{
	QStringList list;
	for (auto it = textures.cbegin(); it != textures.cend(); ++it) {
		if (it.value() && !it.value()->usable() && !it.value()->reference.startsWith(QLatin1Char('$'))) {
			list << it.value()->reference;
		}
	}
	for (auto it = cubes.cbegin(); it != cubes.cend(); ++it) {
		for (const MaterialTexturePtr& face : it.value()->faces) {
			if (face && !face->usable()) {
				list << face->reference;
			}
		}
	}
	list.removeDuplicates();
	list.sort(Qt::CaseInsensitive);
	return list;
}

int MaterialImageSet::readyCount() const
{
	int count = 0;
	for (auto it = textures.cbegin(); it != textures.cend(); ++it) {
		count += it.value() && it.value()->usable() ? 1 : 0;
	}
	return count;
}

QString materialImageKey(const QString& reference)
{
	return normalizedPath(reference).toCaseFolded();
}

QStringList materialImageCandidates(MaterialEngine engine, const QString& reference, bool ioquake3Extensions)
{
	const QString path = normalizedPath(reference);
	QStringList candidates;
	const auto add = [&](const QString& candidate) {
		if (!candidate.isEmpty() && !candidates.contains(candidate, Qt::CaseInsensitive)) {
			candidates << candidate;
		}
	};
	const QString suffix = suffixOf(path);
	const QString base = suffix.isEmpty() ? path : withoutSuffix(path);
	if (engine == MaterialEngine::Doom3) {
		// R_LoadImage: lower case, .tga by default with a .jpg retry.
		const QString lower = path.toLower();
		if (suffix.isEmpty() || suffix == QStringLiteral("tga")) {
			add(base.toLower() + QStringLiteral(".tga"));
			add(base.toLower() + QStringLiteral(".jpg"));
		} else {
			add(lower);
		}
		return candidates;
	}
	if (engine == MaterialEngine::Quake3) {
		if (suffix.isEmpty()) {
			if (ioquake3Extensions) {
				for (const QString& extension : {QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
						 QStringLiteral("pcx"), QStringLiteral("bmp")}) {
					add(base + QLatin1Char('.') + extension);
				}
			}
			return candidates;
		}
		add(path);
		if (suffix == QStringLiteral("tga")) {
			add(base + QStringLiteral(".jpg"));
		}
		if (ioquake3Extensions) {
			for (const QString& extension : {QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
					 QStringLiteral("pcx"), QStringLiteral("bmp")}) {
				add(base + QLatin1Char('.') + extension);
			}
		}
		return candidates;
	}
	add(path);
	return candidates;
}

QStringList quake3SkyFaceSuffixes()
{
	return {QStringLiteral("rt"), QStringLiteral("bk"), QStringLiteral("lf"), QStringLiteral("ft"), QStringLiteral("up"), QStringLiteral("dn")};
}

MaterialTexturePtr quake3DefaultTexture()
{
	// R_CreateDefaultImage: a 16x16 box, value 32 inside, 255 on the edge.
	static const MaterialTexturePtr texture = makeTexture(16, 16, [](int x, int y) {
		const bool edge = x == 0 || y == 0 || x == 15 || y == 15;
		return edge ? qRgba(255, 255, 255, 255) : qRgba(32, 32, 32, 32);
	}, QStringLiteral("*default"));
	return texture;
}

MaterialTexturePtr materialVideoPlaceholder()
{
	static const MaterialTexturePtr texture = makeTexture(64, 64, [](int x, int y) {
		const bool sprocket = (x < 6 || x >= 58) && (y % 8) >= 2 && (y % 8) < 6;
		const int shade = sprocket ? 10 : 40 + ((x / 8 + y / 8) % 2) * 12;
		return qRgba(shade, shade, shade + 6, 255);
	}, QStringLiteral("$video"), QStringLiteral("video"));
	return texture;
}

MaterialTexturePtr materialBuiltinTexture(const QString& name, bool developerDefault)
{
	const QString key = name.trimmed().toLower();
	static QMutex mutex;
	static QHash<QString, MaterialTexturePtr> cache;
	const QString cacheKey = key + (developerDefault ? QStringLiteral("|dev") : QString());
	{
		QMutexLocker locker(&mutex);
		if (cache.contains(cacheKey)) {
			return cache.value(cacheKey);
		}
	}
	MaterialTexturePtr texture;
	// Image_init.cpp R_InitImages built-ins.
	if (key == QStringLiteral("_white") || key == QStringLiteral("$whiteimage") || key == QStringLiteral("*white")) {
		texture = makeTexture(16, 16, [](int, int) { return qRgba(255, 255, 255, 255); }, key);
	} else if (key == QStringLiteral("_black")) {
		texture = makeTexture(16, 16, [](int, int) { return qRgba(0, 0, 0, 0); }, key);
	} else if (key == QStringLiteral("_flat")) {
		texture = makeTexture(2, 2, [](int, int) { return qRgba(128, 128, 255, 255); }, key);
	} else if (key == QStringLiteral("_default")) {
		texture = makeTexture(16, 16, [developerDefault](int x, int y) {
			if (!developerDefault) {
				return qRgba(0, 0, 0, 0);
			}
			const bool edge = x == 0 || y == 0 || x == 15 || y == 15;
			return edge ? qRgba(255, 255, 255, 255) : qRgba(32, 32, 32, 255);
		}, key);
	} else if (key == QStringLiteral("_quadratic")) {
		texture = makeTexture(32, 4, [](int x, int) {
			double d = std::abs(x - 15.5) - 0.5;
			d = std::max(0.0, d);
			const double value = 1.0 - d / 16.0;
			const int v = static_cast<int>(255.0 * value * value);
			return rgba(v, v, v, 255);
		}, key);
	} else if (key == QStringLiteral("_nofalloff")) {
		texture = makeTexture(64, 16, [](int x, int y) {
			const bool border = x == 0 || y == 0 || x == 63 || y == 15;
			return border ? qRgba(255, 255, 255, 0) : qRgba(255, 255, 255, 255);
		}, key);
	} else if (key == QStringLiteral("_fog")) {
		texture = makeTexture(128, 128, [](int x, int y) {
			const double b = std::sqrt(static_cast<double>((x - 64) * (x - 64) + (y - 64) * (y - 64)));
			return rgba(255, 255, 255, static_cast<int>(255.0 * (1.0 - std::pow(0.982, b))));
		}, key);
	} else if (key == QStringLiteral("_ambient")) {
		texture = makeTexture(2, 2, [](int, int) { return rgba(127, 29, 227, 255); }, key);
	} else if (key == QStringLiteral("_ramp") || key == QStringLiteral("_alpharamp")) {
		texture = makeTexture(256, 1, [](int x, int) { return qRgba(x, x, x, x); }, key);
	} else if (key == QStringLiteral("_alphanotch")) {
		texture = makeTexture(2, 1, [](int x, int) { return qRgba(255, 255, 255, x == 0 ? 0 : 255); }, key);
	} else if (key == QStringLiteral("_speculartable")) {
		texture = makeTexture(256, 1, [](int x, int) {
			const double f = std::max(0.0, 4.0 * x / 255.0 - 3.0);
			const int v = static_cast<int>(255.0 * f * f);
			return rgba(v, v, v, 255);
		}, key);
	} else if (key == QStringLiteral("_currentrender") || key == QStringLiteral("_scratch") || key == QStringLiteral("_scratch2")
		|| key == QStringLiteral("_accum") || key == QStringLiteral("_cinematic")) {
		texture = makeTexture(16, 16, [](int, int) { return qRgba(128, 128, 128, 255); }, key, QStringLiteral("render-target"));
	}
	if (texture) {
		QMutexLocker locker(&mutex);
		cache.insert(cacheKey, texture);
	}
	return texture;
}

MaterialImageSet resolveMaterialImages(const MaterialDefinition& definition, const MaterialImageSource& source, const std::function<bool()>& cancelled)
{
	MaterialImageSet set;
	Resolver resolver(definition, source, cancelled, &set);
	resolver.run();
	return set;
}

QImage doom3HeightmapToNormalMap(const QImage& heightmap, double scale)
{
	// R_HeightmapToNormalMap: grey = (R+G+B)/3, neighbours wrap, two
	// gradient estimates averaged, packed as 127n + 128.
	const QImage source = heightmap.convertToFormat(QImage::Format_ARGB32);
	const int width = source.width();
	const int height = source.height();
	QImage result(width, height, QImage::Format_ARGB32);
	if (width <= 0 || height <= 0) {
		return result;
	}
	QVector<int> depth(static_cast<qsizetype>(width) * height);
	for (int y = 0; y < height; ++y) {
		const auto* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
		for (int x = 0; x < width; ++x) {
			depth[static_cast<qsizetype>(y) * width + x] = (qRed(line[x]) + qGreen(line[x]) + qBlue(line[x])) / 3;
		}
	}
	const double s = scale / 256.0;
	const auto at = [&](int x, int y) { return depth[static_cast<qsizetype>((y % height + height) % height) * width + (x % width + width) % width]; };
	const auto normalize = [](double& x, double& y, double& z) {
		const double length = std::sqrt(x * x + y * y + z * z);
		if (length > 0.0) {
			x /= length;
			y /= length;
			z /= length;
		}
	};
	for (int y = 0; y < height; ++y) {
		auto* out = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (int x = 0; x < width; ++x) {
			const int c = at(x, y);
			const int right = at(x + 1, y);
			const int below = at(x, y + 1);
			const int belowRight = at(x + 1, y + 1);
			double ax = -(right - c) * s;
			double ay = -(below - c) * s;
			double az = 1.0;
			normalize(ax, ay, az);
			double bx = -(belowRight - below) * s;
			double by = (c - below) * s;
			double bz = 1.0;
			normalize(bx, by, bz);
			double nx = ax + bx;
			double ny = ay + by;
			double nz = az + bz;
			normalize(nx, ny, nz);
			out[x] = rgba(static_cast<int>(nx * 127 + 128), static_cast<int>(ny * 127 + 128), static_cast<int>(nz * 127 + 128), 255);
		}
	}
	return result;
}

QImage doom3AddNormalMaps(const QImage& first, const QImage& second)
{
	// R_AddNormalMaps: rebuild z when the first normal is short, add the
	// second's x and y, renormalise. The second is point sampled.
	QImage result = first.convertToFormat(QImage::Format_ARGB32);
	QImage other = second.convertToFormat(QImage::Format_ARGB32);
	if (other.size() != result.size()) {
		other = other.scaled(result.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
	}
	for (int y = 0; y < result.height(); ++y) {
		auto* a = reinterpret_cast<QRgb*>(result.scanLine(y));
		const auto* b = reinterpret_cast<const QRgb*>(other.constScanLine(y));
		for (int x = 0; x < result.width(); ++x) {
			double nx = (qRed(a[x]) - 128) / 127.0;
			double ny = (qGreen(a[x]) - 128) / 127.0;
			double nz = (qBlue(a[x]) - 128) / 127.0;
			if (std::sqrt(nx * nx + ny * ny + nz * nz) < 1.0) {
				nz = std::sqrt(std::max(0.0, 1.0 - nx * nx - ny * ny));
			}
			nx += (qRed(b[x]) - 128) / 127.0;
			ny += (qGreen(b[x]) - 128) / 127.0;
			const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (length > 0.0) {
				nx /= length;
				ny /= length;
				nz /= length;
			}
			a[x] = rgba(static_cast<int>(nx * 127 + 128), static_cast<int>(ny * 127 + 128), static_cast<int>(nz * 127 + 128), 255);
		}
	}
	return result;
}

QImage doom3SmoothNormalMap(const QImage& normalMap)
{
	// R_SmoothNormalMap: a wrapped 3x3 sum skipping (0,0,0) and
	// (128,128,128), renormalised.
	const QImage source = normalMap.convertToFormat(QImage::Format_ARGB32);
	QImage result = source;
	const int width = source.width();
	const int height = source.height();
	for (int y = 0; y < height; ++y) {
		auto* out = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (int x = 0; x < width; ++x) {
			double nx = 0.0;
			double ny = 0.0;
			double nz = 0.0;
			for (int dy = -1; dy <= 1; ++dy) {
				const auto* line = reinterpret_cast<const QRgb*>(source.constScanLine(((y + dy) % height + height) % height));
				for (int dx = -1; dx <= 1; ++dx) {
					const QRgb c = line[((x + dx) % width + width) % width];
					if ((qRed(c) == 0 && qGreen(c) == 0 && qBlue(c) == 0) || (qRed(c) == 128 && qGreen(c) == 128 && qBlue(c) == 128)) {
						continue;
					}
					nx += qRed(c) - 128;
					ny += qGreen(c) - 128;
					nz += qBlue(c) - 128;
				}
			}
			const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (length > 0.0) {
				nx /= length;
				ny /= length;
				nz /= length;
			}
			out[x] = rgba(static_cast<int>(128 + 127 * nx), static_cast<int>(128 + 127 * ny), static_cast<int>(128 + 127 * nz), qAlpha(out[x]));
		}
	}
	return result;
}

bool decodeDoomPatchIndices(const QByteArray& bytes, QVector<int>* indices, QSize* size, QPoint* offset, QString* error)
{
	// The Doom picture format: int16 width, height, left and top offsets;
	// uint32 column offsets; posts of {topdelta, length, pad, pixels, pad}
	// ending at topdelta 255. A topdelta not above the previous one is
	// relative (DeePsea tall patches).
	if (bytes.size() < 8) {
		if (error) {
			*error = Text::tr("The patch is too short.");
		}
		return false;
	}
	const int width = qFromLittleEndian<qint16>(bytes.constData());
	const int height = qFromLittleEndian<qint16>(bytes.constData() + 2);
	if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || 8LL + 4LL * width > bytes.size()) {
		if (error) {
			*error = Text::tr("The patch header is not a Doom picture.");
		}
		return false;
	}
	if (offset) {
		*offset = QPoint(qFromLittleEndian<qint16>(bytes.constData() + 4), qFromLittleEndian<qint16>(bytes.constData() + 6));
	}
	indices->fill(-1, static_cast<qsizetype>(width) * height);
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	for (int x = 0; x < width; ++x) {
		qint64 position = qFromLittleEndian<quint32>(bytes.constData() + 8 + 4 * x);
		int top = -1;
		for (int guard = 0; guard < 1024; ++guard) {
			if (position < 0 || position >= bytes.size()) {
				if (error) {
					*error = Text::tr("A patch column runs outside the lump.");
				}
				return false;
			}
			const int delta = data[position];
			if (delta == 0xFF) {
				break;
			}
			top = delta <= top ? top + delta : delta;
			if (position + 3 > bytes.size()) {
				return false;
			}
			const int length = data[position + 1];
			if (position + 3 + length > bytes.size()) {
				if (error) {
					*error = Text::tr("A patch post runs outside the lump.");
				}
				return false;
			}
			for (int y = 0; y < length; ++y) {
				const int row = top + y;
				if (row >= 0 && row < height) {
					(*indices)[static_cast<qsizetype>(row) * width + x] = data[position + 3 + y];
				}
			}
			position += 4 + length;
		}
	}
	*size = QSize(width, height);
	return true;
}

} // namespace vibestudio
