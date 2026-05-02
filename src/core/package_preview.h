#pragma once

#include "core/package_archive.h"

#include <QSize>
#include <QString>
#include <QStringList>

namespace vibestudio {

enum class PackagePreviewKind {
	Unavailable,
	Directory,
	Text,
	Image,
	Model,
	Audio,
	Binary,
};

struct PackagePreview {
	QString virtualPath;
	PackagePreviewKind kind = PackagePreviewKind::Unavailable;
	QString title;
	QString summary;
	QString body;
	QStringList detailLines;
	QStringList rawLines;
	bool truncated = false;
	qint64 bytesRead = 0;
	qint64 totalBytes = 0;
	QString error;
	QString assetKindId;
	QStringList assetDetailLines;
	QStringList assetRawLines;
	QString imageFormat;
	QSize imageSize;
	int imageDepth = 0;
	int imageColorCount = 0;
	bool imagePaletteAware = false;
	QStringList imagePaletteLines;
	QString modelFormat;
	QStringList modelViewportLines;
	QStringList modelMaterialLines;
	QStringList modelAnimationLines;
	QString audioFormat;
	QStringList audioWaveformLines;
	QString textLanguageId;
	QString textLanguageName;
	QStringList textHighlightLines;
	QStringList textDiagnosticLines;
	QString textSaveState;
};

QString packagePreviewKindId(PackagePreviewKind kind);
QString packagePreviewKindDisplayName(PackagePreviewKind kind);
PackagePreview buildPackageEntryPreview(const PackageArchive& archive, const QString& virtualPath, qint64 byteLimit = 65536);

} // namespace vibestudio
