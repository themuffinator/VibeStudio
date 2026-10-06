#include "core/model_document.h"
#include "core/model_skin_source.h"
#include "core/package_staging.h"
#include "core/texture_export.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray source(const ModelDocument &document)
{
	return QJsonDocument(editableModelJson(document.mesh())).toJson(QJsonDocument::Compact);
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	auto palette = modelMdlPreviewPalette({});
	palette.colors[1] = palette.colors[0];
	palette.generated = false;
	const auto paletteBytes = modelMdlPaletteBytes(palette);
	const auto lump = tests::skinLump();
	tests::SkinReader reader;
	reader.add("skin.lmp", lump);
	reader.add("skin.lmp", tests::skinLump(27));
	reader.add("gfx/palette.lmp", paletteBytes);
	ModelMdlSkinInput input;
	ModelSkinSourceReceipt receipt;
	qsizetype index = 97;
	ok &= expect(!resolveModelSkinSource(reader, {"skin.lmp", -1}, &index, &error) && index == 97,
				 "ambiguous paths leave the selection untouched");
	ok &= expect(!resolveModelSkinSource(reader, {"other.lmp", 1}, &index, &error), "stale path/index pair is refused");
	ok &= expect(readModelSkinSource(reader, {"skin.lmp", 1}, palette, "quake", &input, &receipt, &error) &&
					 input.pixels == tests::skinLump(27).mid(8) && input.palette == paletteBytes && receipt.entryIndex == 1 &&
					 receipt.paletteKind == "package" && receipt.paletteEntryIndex == 2 &&
					 receipt.sha256 == QCryptographicHash::hash(tests::skinLump(27), QCryptographicHash::Sha256),
				 "exact occurrence streams verified indices and records source hash and package palette");
	const auto originalInput = input.pixels;
	const auto originalReceipt = receipt.sha256;
	const auto unchanged = [&] { return input.pixels == originalInput && receipt.sha256 == originalReceipt; };
	reader.lateFailure = true;
	ok &= expect(!readModelSkinSource(reader, {"", 0}, palette, "quake", &input, &receipt, &error) && unchanged(),
				 "late checksum failure publishes nothing");
	reader.lateFailure = false;
	bool cancel = false;
	ModelWorkControl control;
	control.cancelled = [&] { return cancel; };
	control.progress = [&](ModelWorkPhase, qint64 completed, qint64)
	{
		if (completed >= 11)
		{
			cancel = true;
		}
	};
	ok &= expect(!readModelSkinSource(reader, {"", 0}, palette, "quake", &input, &receipt, &error, control) && unchanged(),
				 "mid-stream cancellation publishes nothing");
	reader.metadata[0].sizeBytes--;
	ok &= expect(!readModelSkinSource(reader, {"", 0}, palette, "quake", &input, &receipt, &error) && unchanged(),
				 "oversized stream refuses recorded-size mismatch");
	reader.metadata[0].sizeBytes += 2;
	ok &= expect(!readModelSkinSource(reader, {"", 0}, palette, "quake", &input, &receipt, &error) && unchanged(),
				 "truncated stream refuses recorded-size mismatch");
	reader.metadata[0].sizeBytes--;
	reader.metadata[0].sizeBytes = quint64(modelFileByteLimit) + 1;
	reader.entered = false;
	ok &= expect(!readModelSkinSource(reader, {"", 0}, palette, "quake", &input, &receipt, &error) && unchanged() && !reader.entered,
				 "oversized metadata is rejected before reading payloads");
	reader.metadata[0].sizeBytes = quint64(lump.size());
	reader.payloads[2] = QByteArray(768, '\0');
	reader.metadata[2].readable = false;
	ok &= expect(!readModelSkinSource(reader, {"", 0}, palette, "quake", &input, &receipt, &error) && unchanged(),
				 "unreadable package palette cannot fall back silently");
	reader.metadata[2].readable = true;
	reader.payloads[2].chop(1);
	reader.metadata[2].sizeBytes--;
	ok &= expect(!readModelSkinSource(reader, {"", 0}, palette, "quake", &input, &receipt, &error) && unchanged(),
				 "malformed package palette cannot fall back silently");
	reader.payloads[2] = paletteBytes;
	reader.metadata[2].sizeBytes = quint64(paletteBytes.size());
	reader.add("gfx/palette.lmp", paletteBytes);
	ok &= expect(!readModelSkinSource(reader, {"", 0}, palette, "quake", &input, &receipt, &error) && unchanged(),
				 "ambiguous package palette cannot recolour pixels silently");
	ok &= expect(error.contains("gfx/palette.lmp") && !error.contains("exact entry index"),
				 "palette failures identify the palette and do not suggest an unavailable palette index selector");
	reader.add("skin.png", tests::skinPng(tests::skinImage(palette, 8, 4)));
	ok &= expect(readModelSkinSource(reader, {"skin.png", -1}, palette, "quake", &input, &receipt, &error) && input.paletteEmbedded &&
					 input.pixels == lump.mid(8) && receipt.paletteKind == "embedded",
				 "embedded PNG palette wins even when it equals the fallback and package palette is ambiguous");

	PackageStagingModel staging;
	ok &= expect(staging.createEmpty(PackageArchiveFormat::Zip, {}, &error) && staging.addBytes(lump, "skin.lmp", &error),
				 "create staged texture");
	const PackageStagingArchive snapshot(staging);
	ok &= expect(staging.addBytes(tests::skinLump(27), "skin.lmp", &error, PackageStageConflictResolution::ReplaceExisting),
				 "replace the live texture plan");
	ok &= expect(readModelSkinSource(snapshot, {"skin.lmp", -1}, palette, "quake", &input, &receipt, &error) &&
					 input.pixels == lump.mid(8) && receipt.paletteKind == "model",
				 "captured snapshot survives later plan replacement without losing opaque index 255");
	ok &= expect(staging.deleteEntry("skin.lmp", &error) &&
					 !readModelSkinSource(PackageStagingArchive(staging), {"skin.lmp", -1}, palette, "quake", &input, &receipt, &error),
				 "deleted staged entries do not fall back to old bytes");

	// Exercise the texture author's native outputs, retaining duplicate-colour indices.
	IdTechPaletteResolution resolution;
	resolution.palette = palette;
	resolution.fromPackage = true;
	const auto image = tests::skinImage(palette, 16, 16);
	for (auto format :
		 {TextureExportFormat::IndexedPng, TextureExportFormat::Pcx, TextureExportFormat::QuakeMiptex, TextureExportFormat::Quake2Wal})
	{
		TextureExportOptions options;
		options.format = format;
		options.name = QStringLiteral("skin");
		const auto encoded = encodeTextureExport(image, options, resolution);
		const auto name = QStringLiteral("skin.") + textureExportSuffix(format);
		const bool decoded = encoded.succeeded && decodeModelMdlSkin(name, encoded.bytes, palette, &input, &error);
		// WAL export reserves Quake II's transparent index. Skin intake must preserve
		// the actual staged bytes, including any conversion already chosen there.
		const auto baseOffset = format == TextureExportFormat::Quake2Wal && encoded.bytes.size() >= 100
									? qFromLittleEndian<quint32>(encoded.bytes.constData() + 40)
									: 0;
		const auto expected =
			format == TextureExportFormat::Quake2Wal ? encoded.bytes.mid(baseOffset, 16 * 16) : tests::skinLump(1, 16, 16).mid(8);
		if (!decoded || input.pixels != expected || input.palette != paletteBytes)
		{
			std::cerr << name.toStdString() << " encoded=" << encoded.succeeded << " decoded=" << decoded
					  << " preserved=" << encoded.preservedIndices << " pixels=" << input.pixels.left(12).toHex().toStdString()
					  << " paletteEqual=" << (input.palette == paletteBytes) << " error=" << error.toStdString()
					  << " exportError=" << encoded.error.toStdString() << '\n';
		}
		ok &= expect(decoded && input.size == image.size() && input.pixels == expected && input.palette == paletteBytes,
					 "texture encoder output imports raw base-level indices without resampling or nearest-colour remapping");
		PackageStagingModel texturePlan;
		const bool wadTexture = format == TextureExportFormat::QuakeMiptex;
		const auto entryPath = wadTexture ? QStringLiteral("skin") : QStringLiteral("textures/") + name;
		ok &= expect(texturePlan.createEmpty(wadTexture ? PackageArchiveFormat::Wad : PackageArchiveFormat::Zip,
											 wadTexture ? QStringLiteral("WAD2") : QString(), &error) &&
						 stageTextureExport(encoded, options, entryPath, &texturePlan, false, &error),
					 "stage actual texture-authoring output");
		const auto planRevision = texturePlan.revision();
		const auto planned = packagePlannedArchive(texturePlan);
		ok &= expect(readModelSkinSource(planned, {entryPath, -1}, palette, "quake", &input, &receipt, &error) &&
						 input.pixels == expected && input.palette == paletteBytes && texturePlan.revision() == planRevision,
					 "texture stage to model skin uses the real planned-package adapter without mutating the plan");
		if (format == TextureExportFormat::Quake2Wal && encoded.succeeded)
		{
			auto opaqueWal = encoded.bytes;
			opaqueWal.replace(baseOffset, 16 * 16, tests::skinLump(1, 16, 16).mid(8));
			ok &= expect(decodeModelMdlSkin(name, opaqueWal, palette, &input, &error) && input.pixels == tests::skinLump(1, 16, 16).mid(8),
						 "native WAL index 255 is opaque and remains exact in an MDL skin");
		}
		if (format == TextureExportFormat::QuakeMiptex)
		{
			const auto embedded = encoded.bytes + QByteArray::fromHex("0001") + paletteBytes;
			ok &= expect(decodeModelMdlSkin(name, embedded, palette, &input, &error) && input.paletteEmbedded,
						 "WAD3 identical palette retains embedded provenance");
			auto transparent = embedded;
			transparent[0] = '{';
			ok &= expect(!decodeModelMdlSkin(name, transparent, palette, &input, &error),
						 "WAD3 fence transparency is not silently made opaque");
			ok &= expect(!decodeModelMdlSkin(name, encoded.bytes.chopped(1), palette, &input, &error),
						 "truncated native mip chain is refused");
		}
	}
	QByteArray m8(1040, '\0');
	for (auto item : {QPair{0, 2}, QPair{36, 8}, QPair{100, 4}, QPair{164, 1040}})
	{
		qToLittleEndian<qint32>(item.second, m8.data() + item.first);
	}
	m8.replace(260, 768, paletteBytes);
	m8 += lump.mid(8);
	ok &= expect(decodeModelMdlSkin("skin.m8", m8, palette, &input, &error) && input.paletteEmbedded && input.pixels == lump.mid(8),
				 "M8 base pixels and identical embedded palette provenance survive");

	ModelDocument document;
	const auto mesh = decodeModelMesh("fixture.mdl", tests::groupedMdlFixture().bytes, &palette);
	ok &= expect(document.setMesh(mesh, &error), "open native model");
	const auto before = source(document);
	ModelEdit edit;
	edit.kind = ModelEditKind::ReplaceMdlSkinMember;
	edit.mdlSkinSlot = 0;
	edit.mdlSkinMember = 1;
	ok &= expect(readModelSkinSource(snapshot, {"skin.lmp", -1}, palette, "quake", &edit.mdlSkin, &receipt, &error) &&
					 document.edit(edit, &error) && document.undo() && source(document) == before,
				 "handoff participates in normal whole-document undo including poses and other skins");
	edit.mdlSkin.palette[9] ^= 1;
	ok &= expect(!document.edit(edit, &error) && source(document) == before && !document.canUndo(),
				 "palette mismatch does not alter source or history");
	edit.mdlSkin.palette = paletteBytes;
	edit.mdlSkin.size = QSize(4, 8);
	ok &= expect(!document.edit(edit, &error) && source(document) == before, "matching pixel counts with different dimensions still fail");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
