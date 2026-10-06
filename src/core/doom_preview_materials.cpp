#include "core/doom_preview_materials.h"
#include "core/doom_preview_geometry.h"
#include "core/idtech_image.h"

#include <QCoreApplication>
#include <QMap>
#include <QPainter>
#include <QSet>
#include <QtEndian>
#include <algorithm>

namespace vibestudio {
namespace {
QString key(const QString& text) { return text.trimmed().replace('\\', '/').toCaseFolded(); }
qint16 i16(const QByteArray& bytes, qsizetype at) { return qFromLittleEndian<qint16>(bytes.constData() + at); }
qint32 i32(const QByteArray& bytes, qsizetype at) { return qFromLittleEndian<qint32>(bytes.constData() + at); }
QString lumpName(const QByteArray& bytes, qsizetype at) { return QString::fromLatin1(bytes.mid(at, 8).split('\0').first()).trimmed(); }
struct PatchPlacement { int x, y, index; };
struct Definition { QString name, path, layer; QSize size; QVector<PatchPlacement> patches; };
struct Request { QString name; bool flat = false; };

class Resolver {
public:
	Resolver(const PackageArchiveReader& reader, const LevelPreviewAssetOptions& options, LevelPreviewAssets* report, std::function<bool()> tick)
		: reader(reader), options(options), report(report), tick(std::move(tick)), entries(reader.entries())
	{
		for (int i = 0; i < entries.size(); ++i) { if (entries[i].kind == PackageEntryKind::File) { files[key(entries[i].virtualPath)] << i; } }
	}
	// WAD flat/patch/TX namespaces are distinct. A matching flat must never be
	// substituted for a patch or wall merely because the eight-letter name matches.
	int find(const QStringList& paths, const QString& preferredType, QString* error) const
	{
		for (const auto& path : paths) {
			QVector<int> preferred, fallback;
			for (int index : files.value(key(path))) {
				const auto& type = entries[index].typeHint;
				if (!preferredType.isEmpty() && type == preferredType) { preferred << index; }
				else if ((type == QStringLiteral("wad-lump") && (preferredType.isEmpty() || preferredType == QStringLiteral("wad-patch")))
					|| (!type.startsWith(QStringLiteral("wad-")) && (preferredType.isEmpty() || preferredType == QStringLiteral("wad-patch") || path.contains('/')))) {
					fallback << index;
				}
			}
			const auto& matches = preferred.isEmpty() ? fallback : preferred;
			if (matches.size() > 1) { *error = QCoreApplication::translate("DoomPreview", "The asset name is ambiguous within its Doom namespace."); return -2; }
			if (matches.size() == 1) { return matches.first(); }
		}
		return -1;
	}
	QStringList candidates(const QString& name, bool flat, bool patch = false) const
	{
		const QString prefix = flat ? QStringLiteral("flats/") : patch ? QStringLiteral("patches/") : QStringLiteral("textures/");
		QStringList result;
		for (const auto& suffix : {QString(), QStringLiteral(".png"), QStringLiteral(".tga"), QStringLiteral(".lmp"), QStringLiteral(".flat")}) { result << prefix + name + suffix; }
		result << name;
		return result;
	}
	bool readMetadata(const QString& name, QByteArray* bytes, QString* error)
	{
		const int index = find({name}, {}, error);
		if (index < 0) { if (index == -1) { *error = QCoreApplication::translate("DoomPreview", "A Doom texture definition lump is missing: %1.").arg(name); } return false; }
		if (!reader.readEntryAt(index, bytes, error)) { return false; }
		metadataEntries << index; return true;
	}
	void catalog()
	{
		if (catalogRead) { return; }
		catalogRead = true;
		QByteArray bytes;
		if (!readMetadata(QStringLiteral("PNAMES"), &bytes, &catalogError)) { return; }
		if (bytes.size() < 4 || i32(bytes, 0) < 0 || i32(bytes, 0) > 65536 || 4LL + 8LL * i32(bytes, 0) > bytes.size()) {
			catalogError = QCoreApplication::translate("DoomPreview", "PNAMES is malformed or exceeds 65536 patch names."); return;
		}
		for (int i = 0; i < i32(bytes, 0); ++i) { patchNames << lumpName(bytes, 4 + 8 * i); }
		int references = 0, definitionCount = 0;
		for (const auto& name : {QStringLiteral("TEXTURE1"), QStringLiteral("TEXTURE2")}) {
			if (!tick()) { return; }
			QString error;
			const int index = find({name}, {}, &error);
			if (index == -1 && name == QStringLiteral("TEXTURE2")) { continue; }
			if (index < 0 || !reader.readEntryAt(index, &bytes, &error)) {
				catalogError = error.isEmpty() ? QCoreApplication::translate("DoomPreview", "TEXTURE1 is missing.") : error; return;
			}
			metadataEntries << index;
			if (bytes.size() < 4 || i32(bytes, 0) < 0 || i32(bytes, 0) > 65536 || 4LL + 4LL * i32(bytes, 0) > bytes.size()) {
				catalogError = QCoreApplication::translate("DoomPreview", "The Doom texture directory is malformed or exceeds 65536 definitions."); return;
			}
			// Binary layouts and patch ordering: Chocolate Doom 3.1.0 r_data.c,
			// mappatch_t/maptexture_t and R_InitTextures (GPL-2.0-or-later).
			// Copyright (C) 1993-1996 Id Software, Inc.; 2005-2014 Simon Howard.
			// https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/doom/r_data.c
			// Checked Qt parsing/composition below is VibeStudio code.
			for (int i = 0; i < i32(bytes, 0); ++i) {
				if (!tick()) { return; }
				const qint64 offset = i32(bytes, 4 + 4 * i);
				if (++definitionCount > 65536 || offset < 4LL + 4LL * i32(bytes, 0) || offset + 22 > bytes.size()) {
					catalogError = QCoreApplication::translate("DoomPreview", "A Doom texture definition offset is invalid."); return;
				}
				Definition definition;
				definition.name = lumpName(bytes, offset); definition.path = entries[index].virtualPath;
				definition.layer = entries[index].layerId.isEmpty() ? entries[index].sourceArchiveId : entries[index].layerId;
				definition.size = QSize(i16(bytes, offset + 12), i16(bytes, offset + 14));
				const int count = i16(bytes, offset + 20);
				if (definition.name.isEmpty() || definition.size.width() <= 0 || definition.size.height() <= 0 || definition.size.width() > 4096
					|| definition.size.height() > 4096 || count < 0 || count > 4096 || (references += count) > 65536 || offset + 22 + 10LL * count > bytes.size()) {
					catalogError = QCoreApplication::translate("DoomPreview", "A Doom texture has invalid dimensions, patch count, or truncated patch records."); return;
				}
				for (int p = 0; p < count; ++p) {
					const auto at = offset + 22 + 10LL * p;
					const int patch = i16(bytes, at + 4);
					if (patch < 0 || patch >= patchNames.size()) { catalogError = QCoreApplication::translate("DoomPreview", "A Doom texture refers outside PNAMES."); return; }
					definition.patches << PatchPlacement {i16(bytes, at), i16(bytes, at + 2), patch};
				}
				definitions[key(definition.name)] << std::move(definition);
			}
		}
	}
	QImage decode(int index, bool flat, QString* error)
	{
		QByteArray bytes;
		if (!reader.readEntryAt(index, &bytes, error)) { return {}; }
		IdTechImageDecodeContext context;
		context.maximumDimension = 4096;
		context.maximumTotalPixels = context.maximumImagePixels = 16 * 1024 * 1024;
		context.isCancelled = [&] { return !tick(); };
		const auto decoded = decodeIdTechImage(flat ? QStringLiteral("flats/") + entries[index].virtualPath : entries[index].virtualPath, bytes, palette.palette, context);
		if (!decoded.decoded || decoded.image.isNull()) { *error = decoded.error; return {}; }
		return decoded.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}
	LevelPreviewMaterial resolve(const Request& request)
	{
		LevelPreviewMaterial material;
		material.key = doomPreviewMaterialKey(request.name, request.flat); material.name = request.name; material.status = QStringLiteral("missing");
		QSet<int> inputIndexes;
		const auto input = [&](int index, const QString& role) {
			if (index < 0 || inputIndexes.contains(index)) { return; }
			inputIndexes.insert(index);
			const auto& entry = entries[index];
			material.inputs << LevelPreviewInput {index, entry.sourceOrdinal, entry.virtualPath, entry.typeHint,
				entry.layerId.isEmpty() ? entry.sourceArchiveId : entry.layerId, role, entry.sizeBytes};
		};
		if (request.flat && request.name.compare(QStringLiteral("F_SKY1"), Qt::CaseInsensitive) == 0) {
			material.status = QStringLiteral("builtin"); material.note = QCoreApplication::translate("DoomPreview", "Sky is shown as an editable plane; engine sky rendering is not simulated."); return material;
		}
		const int index = find(candidates(request.name, request.flat), request.flat ? QStringLiteral("wad-flat") : QStringLiteral("wad-texture"), &material.note);
		if (index == -2) { material.status = QStringLiteral("ambiguous"); return material; }
		if (index >= 0) {
			input(index, request.flat ? QStringLiteral("flat") : QStringLiteral("wall-image"));
			material.imagePath = entries[index].virtualPath;
			material.sourceLayer = entries[index].layerId.isEmpty() ? entries[index].sourceArchiveId : entries[index].layerId;
			material.image = decode(index, request.flat, &material.note);
			material.status = material.image.isNull() ? QStringLiteral("unreadable") : QStringLiteral("image");
		} else if (!request.flat) {
			catalog();
			for (int entry : metadataEntries) { input(entry, key(entries[entry].virtualPath) == QStringLiteral("pnames") ? QStringLiteral("patch-names") : QStringLiteral("texture-definitions")); }
			if (!catalogError.isEmpty()) { material.status = QStringLiteral("unreadable"); material.note = catalogError; return material; }
			const auto matches = definitions.value(key(request.name));
			if (matches.size() > 1) { material.status = QStringLiteral("ambiguous"); material.note = QCoreApplication::translate("DoomPreview", "Multiple Doom texture definitions use this name."); return material; }
			if (matches.isEmpty()) { material.note = QCoreApplication::translate("DoomPreview", "No Doom texture definition or direct texture image was found."); return material; }
			const auto& definition = matches.first();
			QImage image(definition.size, QImage::Format_ARGB32_Premultiplied);
			if (image.isNull()) { material.status = QStringLiteral("budget"); report->complete = false; return material; }
			image.fill(Qt::transparent);
			QPainter painter(&image);
			for (const auto& placement : definition.patches) {
				if (!tick()) { return material; }
				const auto& name = patchNames[placement.index];
				const int patch = find(candidates(name, false, true), QStringLiteral("wad-patch"), &material.note);
				if (patch < 0) { material.status = patch == -2 ? QStringLiteral("ambiguous") : QStringLiteral("missing"); material.note = QCoreApplication::translate("DoomPreview", "Texture patch %1 is missing or ambiguous.").arg(name); return material; }
				input(patch, QStringLiteral("patch"));
				QImage pixels = patchCache.value(patch);
				if (pixels.isNull()) {
					pixels = decode(patch, false, &material.note);
					if (pixels.isNull()) { material.status = QStringLiteral("unreadable"); return material; }
					if (pixels.sizeInBytes() <= 16 * 1024 * 1024) {
						if (patchCacheBytes + pixels.sizeInBytes() > 16 * 1024 * 1024) { patchCache.clear(); patchCacheBytes = 0; }
						patchCache.insert(patch, pixels); patchCacheBytes += pixels.sizeInBytes();
					}
				}
				const auto clipped = QRect(QPoint(placement.x, placement.y), pixels.size()).intersected(image.rect());
				compositePixels += qint64(clipped.width()) * clipped.height();
				if (compositePixels > 64 * 1024 * 1024) { material.status = QStringLiteral("budget"); report->complete = false; material.note = QCoreApplication::translate("DoomPreview", "Doom texture composition exceeds the 64-megapixel work budget."); return material; }
				painter.drawImage(placement.x, placement.y, pixels);
			}
			painter.end();
			material.image = std::move(image); material.imagePath = definition.path; material.sourceLayer = definition.layer;
			material.status = QStringLiteral("composite"); material.note = QCoreApplication::translate("DoomPreview", "Composed from %n patch(es).", nullptr, int(definition.patches.size()));
		}
		if (material.image.isNull()) {
			if (material.note.isEmpty()) { material.note = QCoreApplication::translate("DoomPreview", "No readable image was found in the matching Doom namespace."); }
			return material;
		}
		material.sourceSize = material.image.size();
		const int dimension = std::clamp(options.previewDimension, 1, 2048);
		if (material.image.width() > dimension || material.image.height() > dimension) { material.image = material.image.scaled(dimension, dimension, Qt::KeepAspectRatio, Qt::SmoothTransformation); }
		if (material.image.sizeInBytes() > std::max<qint64>(0, options.imageByteLimit - report->imageBytes)) {
			material.image = {}; material.status = QStringLiteral("budget"); report->complete = false; material.note = QCoreApplication::translate("DoomPreview", "Material image memory budget reached.");
		} else { report->imageBytes += material.image.sizeInBytes(); }
		material.warnings = palette.warnings;
		if (palette.fromPackage) { QString ignored; input(find({palette.sourceVirtualPath}, {}, &ignored), QStringLiteral("palette")); }
		if (palette.palette.generated) { material.warnings << QCoreApplication::translate("DoomPreview", "A generated palette is used; supply the game's PLAYPAL for accurate colours."); }
		return material;
	}
	const PackageArchiveReader& reader;
	const LevelPreviewAssetOptions& options;
	LevelPreviewAssets* report;
	std::function<bool()> tick;
	QVector<PackageEntry> entries;
	QMap<QString, QVector<int>> files;
	QMap<QString, QVector<Definition>> definitions;
	QStringList patchNames;
	QVector<int> metadataEntries;
	QString catalogError;
	bool catalogRead = false;
	IdTechPaletteResolution palette;
	QHash<int, QImage> patchCache;
	qint64 patchCacheBytes = 0, compositePixels = 0;
};
}

void resolveDoomPreviewAssets(const LevelMapDocument& document, const PackageArchiveReader& reader,
	const LevelPreviewAssetOptions& options, LevelPreviewAssets* report, const std::function<bool()>& tick)
{
	QMap<QString, Request> requests;
	const auto add = [&](const QString& name, bool flat) { if (!name.trimmed().isEmpty() && name != QStringLiteral("-")) { requests.insert(doomPreviewMaterialKey(name, flat), {name, flat}); } };
	for (const auto& side : document.doomSidedefs) { add(side.upperTexture, false); add(side.lowerTexture, false); add(side.middleTexture, false); }
	for (const auto& sector : document.doomSectors) { add(sector.floorTexture, true); add(sector.ceilingTexture, true); }
	report->requestedMaterials = int(requests.size());
	Resolver resolver(reader, options, report, tick);
	resolver.palette = resolveIdTechPalette(reader, options.paletteId.isEmpty() || options.paletteId == QStringLiteral("auto") ? QStringLiteral("doom") : options.paletteId);
	for (const auto& request : requests) {
		if (!tick()) { return; }
		if (report->materials.size() >= std::max(0, options.materialLimit)) { report->complete = false; report->warnings << QCoreApplication::translate("DoomPreview", "Material preview count limit reached; remaining materials use flat shading."); break; }
		report->materials << resolver.resolve(request);
	}
	tick();
}

} // namespace vibestudio
