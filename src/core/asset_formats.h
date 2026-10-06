#pragma once

#include "core/asset_tools.h"
#include <QJsonArray>

namespace vibestudio {

// Capability catalog, not a promise that any file with a matching suffix is valid.
// Content parsers remain authoritative. Extraction of original bytes is separate
// from authoring/export, and metadata inspection does not imply mesh decoding.
struct AssetFormatDescriptor {
	QString id;
	QStringList suffixes;
	QString module;
	AssetPreviewKind preview = AssetPreviewKind::Unknown;
	QString readCapability;
	QStringList exportProfiles;
	QString limitation;
	bool runtimeRead = true;
	bool runtimeWrite = false;
};

const QVector<AssetFormatDescriptor>& assetFormats();
const AssetFormatDescriptor* assetFormatForPath(const QString& path);
QStringList assetFormatSuffixes(AssetPreviewKind kind);
QJsonObject assetFormatJson(const AssetFormatDescriptor& format);
QJsonArray assetFormatsJson();
QString assetImageOpenFilter(bool includeDocuments = false);
QString assetPackageOpenFilter();

} // namespace vibestudio
