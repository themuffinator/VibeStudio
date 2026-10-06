#include "core/model_skin_source.h"

#include "core/asset_tools.h"
#include "core/model_file_io.h"

#include <QCoreApplication>
#include <QCryptographicHash>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
bool readEntry(const PackageArchiveReader &archive, qsizetype index, QByteArray *output, QString *error, const ModelWorkControl &control)
{
	const auto entry = archive.entries()[index];
	if (entry.sizeBytes == 0 || entry.sizeBytes > quint64(modelFileByteLimit) || entry.compressedSizeBytes > quint64(modelFileByteLimit))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelMdl",
													   "The package texture or palette is empty or exceeds the model input byte limit."));
	}
	QByteArray bytes;
	QString sinkError;
	const bool read = archive.streamEntryAt(
		index,
		[&](QByteArrayView chunk)
		{
			if (chunk.size() > modelFileByteLimit - bytes.size() || quint64(bytes.size() + chunk.size()) > entry.sizeBytes)
			{
				return fail(&sinkError,
							QCoreApplication::translate("VibeStudioModelMdl", "The package payload exceeds its recorded size."));
			}
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, bytes.size(), qint64(entry.sizeBytes), &sinkError))
			{
				return false;
			}
			bytes.append(chunk.data(), chunk.size());
			return true;
		},
		error, control.cancelled);
	if (!sinkError.isEmpty())
	{
		return fail(error, sinkError);
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, bytes.size(), qint64(entry.sizeBytes), error))
	{
		return false;
	}
	if (!read)
	{
		return error && !error->isEmpty()
				   ? false
				   : fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Unable to verify the complete package payload."));
	}
	if (quint64(bytes.size()) != entry.sizeBytes)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "The package payload does not match its recorded size."));
	}
	*output = std::move(bytes);
	return true;
}
} // namespace

bool resolveModelSkinSource(const PackageArchiveReader &archive, const ModelSkinSourceReference &reference, qsizetype *entryIndex,
							QString *error)
{
	if (!entryIndex || !archive.isOpen() || reference.entryIndex < -1 || (reference.entryIndex < 0 && reference.path.isEmpty()))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Choose a texture from an open package snapshot."));
	}
	const auto entries = archive.entries();
	qsizetype selected = reference.entryIndex;
	if (selected < 0)
	{
		for (qsizetype i = 0; i < entries.size(); ++i)
		{
			if (entries[i].kind == PackageEntryKind::File && entries[i].virtualPath.compare(reference.path, Qt::CaseInsensitive) == 0)
			{
				if (selected >= 0)
				{
					return fail(error,
								QCoreApplication::translate("VibeStudioModelMdl",
															"The package contains repeated texture names. Select an exact entry index."));
				}
				selected = i;
			}
		}
	}
	if (selected < 0 || selected >= entries.size() || entries[selected].kind != PackageEntryKind::File || !entries[selected].readable ||
		(!reference.path.isEmpty() && entries[selected].virtualPath.compare(reference.path, Qt::CaseInsensitive) != 0))
	{
		return fail(error,
					QCoreApplication::translate(
						"VibeStudioModelMdl", "The selected package texture is missing, unreadable or no longer matches its entry index."));
	}
	*entryIndex = selected;
	if (error)
	{
		error->clear();
	}
	return true;
}

bool readModelSkinSource(const PackageArchiveReader &archive, const ModelSkinSourceReference &reference,
						 const IdTechPalette &fallbackPalette, const QString &paletteId, ModelMdlSkinInput *skin,
						 ModelSkinSourceReceipt *receipt, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	const auto selectedPalette = paletteId.isEmpty() ? QStringLiteral("quake") : paletteId;
	if (!skin || !idTechPaletteDescriptorForId(selectedPalette))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Choose a valid skin output and palette family."));
	}
	qsizetype index = -1;
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error) || !resolveModelSkinSource(archive, reference, &index, error))
	{
		return false;
	}
	const auto entry = archive.entries()[index];
	QByteArray bytes;
	if (!readEntry(archive, index, &bytes, error, control))
	{
		return false;
	}
	const auto decodePath = assetDetectionPath(entry.virtualPath, entry.typeHint);
	auto palette = fallbackPalette;
	// A raw Quake palette's transparent picture index is opaque in an MDL skin.
	// Explicit transparency embedded in PNG/PCX/native images remains an error.
	palette.transparentIndex = -1;
	for (auto &color : palette.colors)
	{
		color = qRgb(qRed(color), qGreen(color), qBlue(color));
	}
	ModelMdlSkinInput imported;
	if (!decodeModelMdlSkin(decodePath, bytes, palette, &imported, error, control))
	{
		return false;
	}
	ModelSkinSourceReceipt result;
	result.path = entry.virtualPath;
	result.entryIndex = index;
	result.bytes = bytes.size();
	result.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	result.paletteKind = imported.paletteEmbedded ? QStringLiteral("embedded") : QStringLiteral("model");
	if (!imported.paletteEmbedded)
	{
		// Preview palette lookup may skip corrupt candidates. Authoring must instead
		// fail on an existing unreadable/ambiguous palette, never silently recolour.
		const auto entries = archive.entries();
		ModelWorkProgress search(control, ModelWorkPhase::Reading, error);
		for (const auto &candidate : idTechPaletteCandidatePaths(selectedPalette))
		{
			bool present = false;
			for (const auto &item : entries)
			{
				if (!search.step())
				{
					return false;
				}
				present |= item.kind == PackageEntryKind::File && item.virtualPath.compare(candidate, Qt::CaseInsensitive) == 0;
			}
			if (!present)
			{
				continue;
			}
			qsizetype paletteIndex = -1;
			QByteArray paletteBytes;
			if (!resolveModelSkinSource(archive, {candidate, -1}, &paletteIndex, error))
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelMdl",
															   "Package palette %1 is ambiguous or unreadable. Resolve its entries in "
															   "Packages, or use an image with an embedded palette.")
									   .arg(candidate));
			}
			QString paletteError;
			if (!readEntry(archive, paletteIndex, &paletteBytes, &paletteError, control))
			{
				return fail(
					error,
					QCoreApplication::translate("VibeStudioModelMdl", "Cannot read package palette %1: %2").arg(candidate, paletteError));
			}
			const bool parsed = candidate.endsWith(QStringLiteral(".pcx"), Qt::CaseInsensitive)
									? parsePcxPalette(paletteBytes, selectedPalette, &palette, &paletteError)
									: parseIdTechPaletteBytes(paletteBytes, selectedPalette, &palette, &paletteError);
			if (!parsed)
			{
				return fail(
					error,
					QCoreApplication::translate("VibeStudioModelMdl", "Invalid package palette %1: %2").arg(candidate, paletteError));
			}
			imported.palette = modelMdlPaletteBytes(palette);
			imported.paletteGenerated = palette.generated;
			result.paletteKind = QStringLiteral("package");
			result.paletteSource = entries[paletteIndex].virtualPath;
			result.paletteEntryIndex = paletteIndex;
			break;
		}
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.bytes, result.bytes, error))
	{
		return false;
	}
	*skin = std::move(imported);
	if (receipt)
	{
		*receipt = std::move(result);
	}
	return true;
}
} // namespace vibestudio
