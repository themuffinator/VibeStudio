#include "core/asset_formats.h"
#include "core/texture_export.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QImageWriter>

namespace vibestudio {

const QVector<AssetFormatDescriptor>& assetFormats()
{
	// PakFu's support-matrix distinction between decoding, metadata, conversion
	// and backend-dependent support informs this catalog; no decoder parity is
	// implied. Reference: docs/CREDITS.md, PakFu 13111e4, 2026-10-05.
	static const auto formats = [] {
		using K = AssetPreviewKind;
		QVector<AssetFormatDescriptor> result;
		const auto add = [&](const char* id, const char* suffixes, const char* module, K kind, const char* read,
			const char* outputs = "", const char* limitation = "") {
			result.push_back({QString::fromLatin1(id), QString::fromLatin1(suffixes).split(',', Qt::SkipEmptyParts),
				QString::fromLatin1(module), kind, QString::fromLatin1(read), QString::fromLatin1(outputs).split(',', Qt::SkipEmptyParts),
				QString::fromUtf8(limitation), true, outputs[0] != '\0'});
		};
		add("workspace", "vibeworkspace", "workspace", K::Text, "document", "vibeworkspace");
		add("texture-project", "vtexture", "textures", K::Binary, "document", "vtexture");
		add("audio-project", "vsaudio", "audio", K::Binary, "document", "vsaudio");
		add("audio-session", "vssession", "audio", K::Binary, "document", "vssession,wav");
		add("audio-take", "vstake", "audio", K::Binary, "document", "vsaudio,wav");
		add("mesh-project", "mesh.json", "models", K::Text, "document", "mesh.json,mdl,md2,md3,obj");
		add("model-design", "model.json", "models", K::Text, "document", "model.json,md3,obj");
		add("model-assembly", "assembly.json", "models", K::Text, "document", "assembly.json,md3,obj");
		add("prefab", "vprefab", "levels", K::Text, "document", "vprefab");
		add("package-draft", "vibepackage", "packages", K::Text, "document", "vibepackage,pak,pk3,zip,wad");
		add("pak", "pak", "packages", K::Binary, "archive", "pak,pk3,zip,wad");
		add("zip", "zip,pk3,pk4,pkz", "packages", K::Binary, "archive", "pak,pk3,zip,wad");
		add("wad", "wad,wad2,wad3", "packages", K::Binary, "archive", "wad,pak,pk3,zip",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "WAD variants and lump constraints are validated by the package service."));
		add("map", "map", "levels", K::Text, "document", "map");
		add("bsp", "bsp", "levels", K::Binary, "metadata", "",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "Compiled BSP inspection; not source-map import or decompilation."));
		add("entity-definitions", "def,fgd,ent", "levels", K::Text, "text", "text");
		add("shader", "shader", "shaders", K::Text, "text", "shader");
		for (const auto& entry : QVector<QPair<QByteArray, QStringList>>{
			{"png", {"png"}}, {"jpeg", {"jpg", "jpeg"}}, {"bmp", {"bmp"}}, {"gif", {"gif"}},
			{"tiff", {"tif", "tiff"}}, {"webp", {"webp"}}}) {
			add(entry.first.constData(), entry.second.join(',').toLatin1().constData(), "textures", K::Image, "pixels", "",
				QT_TRANSLATE_NOOP("VibeStudioFormats", "Qt image plugin availability is checked at runtime."));
			auto& format = result.last();
			const auto readers = QImageReader::supportedImageFormats(), writers = QImageWriter::supportedImageFormats();
			format.runtimeRead = readers.contains(entry.first);
			format.runtimeWrite = writers.contains(entry.first);
			if (format.runtimeWrite) { format.exportProfiles << format.id; }
		}
		add("tga", "tga", "textures", K::Image, "pixels", "tga");
		add("pcx", "pcx", "textures", K::Image, "pixels", "pcx",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "8-bit indexed or 3/4-plane input; opaque indexed export."));
		add("wal", "wal", "textures", K::Image, "pixels", "quake2-wal");
		add("miptex", "mip", "textures", K::Image, "pixels", "quake-miptex,quake-wad2");
		add("lmp", "lmp", "textures", K::Image, "pixels", "",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "Content and package namespace determine picture, palette, flat or patch interpretation."));
		add("m8", "m8", "textures", K::Image, "pixels");
		add("m32", "m32", "textures", K::Image, "pixels");
		add("spr", "spr", "textures", K::Image, "pixels", "",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "Quake and Half-Life frames; sprite-container export is unavailable."));
		add("sp2", "sp2,spr2", "textures", K::Image, "pixels", "",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "External frames require the package context."));
		add("dds", "dds", "textures", K::Image, "pixels", "dds",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "2D base mip: BC1–BC5, RXGB and supported RGB masks. Export is uncompressed BGRA8; no cubes, volumes or arrays."));
		add("ftx", "ftx", "textures", K::Image, "pixels", "ftx");
		add("swl", "swl", "textures", K::Image, "pixels", "",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "SiN embedded palette and four mip levels; convert to an available texture export profile."));
		add("obj", "obj", "models", K::Model, "geometry", "obj");
		add("mdl", "mdl", "models", K::Model, "geometry", "mdl,obj");
		add("md2", "md2", "models", K::Model, "geometry", "md2,obj");
		add("md3", "md3", "models", K::Model, "geometry", "md3,obj");
		for (const auto* id : {"mdc", "mdr", "iqm"}) {
			add(id, id, "models", K::Model, "metadata", "",
				QT_TRANSLATE_NOOP("VibeStudioFormats", "Header inspection only; geometry editing and export are unavailable."));
		}
		add("wav", "wav", "audio", K::Audio, "samples", "wav,dmx,vsaudio");
		add("dmx", "dmx", "audio", K::Audio, "samples", "wav,dmx,vsaudio",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "Digital DMX only; PC-speaker lumps remain metadata-only."));
		add("vorbis", "ogg", "audio", K::Audio, "samples", "wav,dmx,vsaudio");
		add("mp3", "mp3", "audio", K::Audio, "samples", "wav,dmx,vsaudio");
		add("flac", "flac", "audio", K::Audio, "samples", "wav,dmx,vsaudio");
		add("opus", "opus", "audio", K::Audio, "metadata", "",
			QT_TRANSLATE_NOOP("VibeStudioFormats", "Playback depends on the host backend; sample editing and conversion are unavailable."));
		return result;
	}();
	return formats;
}

const AssetFormatDescriptor* assetFormatForPath(const QString& path)
{
	static const auto bySuffix = [] {
		QHash<QString, const AssetFormatDescriptor*> index;
		for (const auto& format : assetFormats()) { for (const auto& suffix : format.suffixes) { index.insert(suffix, &format); } }
		return index;
	}();
	const auto name = QFileInfo(path).fileName().toLower();
	// Longest suffix first, so .mesh.json never opens as generic JSON.
	for (auto dot = name.indexOf('.'); dot >= 0; dot = name.indexOf('.', dot + 1)) {
		const auto found = bySuffix.constFind(name.mid(dot + 1));
		if (found != bySuffix.cend()) { return found.value(); }
	}
	return nullptr;
}

QStringList assetFormatSuffixes(AssetPreviewKind kind)
{
	QStringList suffixes;
	for (const auto& format : assetFormats()) { if (format.preview == kind && format.runtimeRead) { suffixes << format.suffixes; } }
	return suffixes;
}

QJsonObject assetFormatJson(const AssetFormatDescriptor& format)
{
	QJsonArray conversions;
	if (format.preview == AssetPreviewKind::Image && format.runtimeRead) {
		for (const auto& profile : textureExportProfiles()) { conversions.append(profile.id); }
	}
	return {{"id", format.id}, {"suffixes", QJsonArray::fromStringList(format.suffixes)}, {"module", format.module},
		{"preview", assetPreviewKindId(format.preview)}, {"read", format.readCapability},
		{"exportProfiles", QJsonArray::fromStringList(format.exportProfiles)}, {"conversionProfiles", conversions}, {"availableRead", format.runtimeRead},
		{"availableWrite", format.runtimeWrite}, {"limitation", QCoreApplication::translate("VibeStudioFormats", format.limitation.toUtf8().constData())}};
}
QJsonArray assetFormatsJson() { QJsonArray result; for (const auto& format : assetFormats()) { result.append(assetFormatJson(format)); } return result; }

QString assetImageOpenFilter(bool includeDocuments)
{
	QStringList patterns;
	if (includeDocuments) { patterns << QStringLiteral("*.vtexture"); }
	for (const auto& suffix : assetFormatSuffixes(AssetPreviewKind::Image)) { patterns << QStringLiteral("*.") + suffix; }
	return QCoreApplication::translate("VibeStudioFormats", "Textures (%1);;All files (*)").arg(patterns.join(' '));
}

QString assetPackageOpenFilter()
{
	QStringList patterns;
	for (const auto& format : assetFormats()) {
		if (format.readCapability != QStringLiteral("archive") || !format.runtimeRead) { continue; }
		for (const auto& suffix : format.suffixes) { patterns << QStringLiteral("*.") + suffix; }
	}
	return QCoreApplication::translate("VibeStudioFormats", "Package archives (%1);;All files (*)").arg(patterns.join(' '));
}
} // namespace vibestudio
