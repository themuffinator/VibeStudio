#pragma once

#include "core/model_mdl.h"
#include "core/package_archive.h"

namespace vibestudio
{
struct ModelSkinSourceReference
{
	QString path;
	qsizetype entryIndex = -1;
};
struct ModelSkinSourceReceipt
{
	QString path, paletteSource;
	QString paletteKind; // Stable identifier: embedded, package or model.
	qsizetype entryIndex = -1;
	qsizetype paletteEntryIndex = -1;
	QByteArray sha256;
	qint64 bytes = 0;
};

// Paths must identify exactly one file. An index identifies the exact row in
// this immutable snapshot; an accompanying path guards against stale indexes.
bool resolveModelSkinSource(const PackageArchiveReader &archive, const ModelSkinSourceReference &reference, qsizetype *entryIndex,
							QString *error = nullptr);
// Copy base-level indexed pixels, never a rendered preview. A package palette
// takes precedence over the supplied model fallback; embedded image palettes
// remain authoritative. No quantization, resizing or package writes occur.
// Failure/cancellation leaves both outputs unchanged. Run on a worker in GUI.
bool readModelSkinSource(const PackageArchiveReader &archive, const ModelSkinSourceReference &reference,
						 const IdTechPalette &fallbackPalette, const QString &paletteId, ModelMdlSkinInput *skin,
						 ModelSkinSourceReceipt *receipt = nullptr, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
