#include "cli/model_appearance.h"
#include "core/model_appearance.h"
#include "core/model_document.h"
#include "core/model_material_slots.h"
#include "core/level_materials.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>

namespace vibestudio::cli
{
namespace
{
struct Text { Q_DECLARE_TR_FUNCTIONS(ModelAppearanceCli) };
ModelAppearanceCliResult fail(int code, const QString &error) { return {code, error, {}, {}}; }
}
ModelAppearanceCliResult runModelAppearance(const QStringList &arguments)
{
	const QSet<QString> flags{"--cli", "--json", "--quiet", "--verbose"};
	const QSet<QString> options{"--input", "--package", "--palette", "--surface", "--material-slot", "--skin", "--member",
		"--entry", "--entry-index", "--settings-file", "--locale", "--catalog-root"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (int i = 1; i < arguments.size(); ++i) {
		const auto arg = arguments[i];
		if (!arg.startsWith('-')) { positional << arg; continue; }
		const int equal = arg.indexOf('=');
		const auto key = equal < 0 ? arg : arg.left(equal);
		if (seen.contains(key)) { return fail(2, Text::tr("Repeated option: %1.").arg(key)); }
		seen.insert(key);
		if (flags.contains(key) && equal < 0) { continue; }
		if (!options.contains(key)) { return fail(2, Text::tr("Unknown or invalid option: %1.").arg(key)); }
		const auto value = equal < 0 ? arguments.value(++i) : arg.mid(equal + 1);
		if (value.isEmpty() || value.startsWith("--")) { return fail(2, Text::tr("Option %1 requires a value.").arg(key)); }
		values.insert(key, value);
	}
	const bool input = seen.contains("--input");
	if (positional.size() != (input ? 2 : 3) || positional.value(0) != "model" || positional.value(1) != "materials" ||
		!seen.contains("--package")) {
		return fail(2, Text::tr("Provide one model source and --package with an archive, folder or portable draft."));
	}
	ModelAppearance appearance;
	const auto index = [&](const QString &key, int *value) {
		bool ok = false;
		*value = values.value(key).toInt(&ok);
		return ok && *value >= 0 && values.value(key) == QString::number(*value);
	};
	if (seen.contains("--surface") != seen.contains("--material-slot")) {
		return fail(2, Text::tr("Pair --surface N with --material-slot N."));
	}
	int surface = -1, slot = -1, entry = -1;
	if ((seen.contains("--surface") && (!index("--surface", &surface) || !index("--material-slot", &slot))) ||
		(seen.contains("--skin") && !index("--skin", &appearance.mdlSkin)) ||
		(seen.contains("--member") && !index("--member", &appearance.mdlMember)) ||
		(seen.contains("--entry-index") && !index("--entry-index", &entry))) {
		return fail(2, Text::tr("Surface, slot, skin, member and entry indexes must be nonnegative integers."));
	}
	if (surface >= 0) { appearance.materialSlots.insert(surface, slot); }
	if (seen.contains("--entry") || seen.contains("--entry-index")) {
		appearance.skin = ModelSkinSourceReference{values.value("--entry"), entry};
	}
	const auto path = input ? values.value("--input") : positional[2];
	QByteArray bytes;
	QString error;
	ModelMesh mesh;
	PackageArchive archive;
	if (!readModelFile(path, &bytes, &error)) { return fail(1, error); }
	const auto package = values.value("--package");
	if (package.endsWith(".vibepackage", Qt::CaseInsensitive)) {
		PackageStagingModel staging;
		if (!PackageDraft::load(package, &staging, &error)) { return fail(3, error); }
		archive = packagePlannedArchive(staging, &error);
		if (!archive.isOpen()) { return fail(3, error); }
	} else if (!archive.load(package, &error)) { return fail(3, error); }
	IdTechPaletteResolution palette;
	const bool mdl = detectModelMeshFormat(path, bytes) == ModelMeshFormat::QuakeMdl;
	if (mdl) { palette = resolveIdTechPalette(archive, values.value("--palette", QStringLiteral("quake"))); }
	if (!importEditableModel(path, bytes, &mesh, &error, mdl ? &palette.palette : nullptr)) { return fail(4, error); }
	ModelAppearanceSnapshot snapshot;
	if (!prepareModelAppearance(mesh, appearance, &archive, &snapshot, &error)) { return fail(4, error); }
	LevelPreviewAssetOptions limits;
	limits.paletteId = values.value("--palette");
	limits.materialLimit = 32; limits.modelLimit = 0; limits.previewDimension = 1024;
	const auto report = resolveModelPreviewAssets(snapshot.materials, archive, limits);
	ModelAppearanceCliResult result;
	result.exitCode = report.complete && report.problemCount() == 0 ? 0 : 4;
	result.data.insert("source", QFileInfo(path).absoluteFilePath());
	result.data.insert("appearance", snapshot.receipt);
	QJsonArray slotRows;
	for (int i = 0; i < mesh.surfaces.size(); ++i) {
		if (supportsModelMaterialSlots(mesh) && !mesh.surfaces[i].skinPaths.isEmpty()) {
			slotRows.append(QJsonObject{{"surface", i}, {"slot", appearance.materialSlots.value(i, 0)}});
		}
	}
	result.data.insert("previewSlots", slotRows);
	result.data.insert("materials", levelPreviewAssetsJson(report));
	result.text = snapshot.details.join(QLatin1Char('\n'));
	if (!result.text.isEmpty()) { result.text += QLatin1Char('\n'); }
	result.text += levelPreviewAssetsText(report);
	return result;
}
} // namespace vibestudio::cli
