#include "core/texture_handoff.h"

#include "core/package_staging.h"

#include <QCoreApplication>

namespace vibestudio {

QString textureExportMapReference(const TextureExportOptions& options, const QString& path,
	LevelMapFormat mapFormat, const QString& wadMagic, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) -> QString { if (error) { *error = message; } return {}; };
	const auto safe = normalizePackageVirtualPath(path, false);
	if (!safe.isSafe()) { return fail(QCoreApplication::translate("VibeStudioTextureHandoff", "Choose a safe relative package path for the texture.")); }
	if (mapFormat == LevelMapFormat::QuakeMap && wadMagic == QStringLiteral("WAD2") && options.format == TextureExportFormat::QuakeMiptex) {
		if (safe.normalizedPath != options.name || safe.normalizedPath.contains(QLatin1Char('/'))) {
			return fail(QCoreApplication::translate("VibeStudioTextureHandoff", "Use the embedded texture name as the WAD2 lump name."));
		}
		return safe.normalizedPath;
	}
	const bool fileProfile = wadMagic.isEmpty() &&
		((mapFormat == LevelMapFormat::Quake3Map && textureExportSupportsQuake3Map(options.format)) ||
		 (mapFormat == LevelMapFormat::QuakeMap && options.format == TextureExportFormat::Quake2Wal));
	if (!fileProfile) {
		return fail(QCoreApplication::translate("VibeStudioTextureHandoff", "Apply PNG/TGA to a Quake III map, a WAL to a Quake-family map, or a WAD2 miptexture to a Quake-family map. Other profiles can be staged separately."));
	}
	const QString prefix = QStringLiteral("textures/");
	const QString suffix = QLatin1Char('.') + textureExportSuffix(options.format);
	if (!safe.normalizedPath.startsWith(prefix) || !safe.normalizedPath.endsWith(suffix) || safe.normalizedPath.size() <= prefix.size() + suffix.size()) {
		return fail(QCoreApplication::translate("VibeStudioTextureHandoff", "Use the lower-case textures/ prefix and %1 suffix for the package path.").arg(suffix));
	}
	// Quake II and q3map2 add textures/ and the image suffix to map tokens.
	return safe.normalizedPath.mid(prefix.size(), safe.normalizedPath.size() - prefix.size() - suffix.size());
}

bool stageAndApplyTextureExport(const TextureExportResult& result, const TextureExportOptions& options, const QString& path,
	PackageStagingModel* staging, bool replaceExisting, LevelMapDocument* map, QString* mapReference, QString* error)
{
	if (error) { error->clear(); }
	if (mapReference) { mapReference->clear(); }
	if (!staging || !map) {
		if (error) { *error = QCoreApplication::translate("VibeStudioTextureHandoff", "Open a package and a map before staging and applying a texture."); }
		return false;
	}
	const QString reference = textureExportMapReference(options, path, map->format, staging->sourceWadMagic(), error);
	if (reference.isEmpty()) { return false; }
	auto planned = *staging;
	auto updated = *map;
	if (!stageTextureExport(result, options, path, &planned, replaceExisting, error) ||
		!applyLevelMapTexture(&updated, reference, nullptr, error, true)) { return false; }
	*staging = std::move(planned);
	*map = std::move(updated);
	if (mapReference) { *mapReference = reference; }
	return true;
}

} // namespace vibestudio
