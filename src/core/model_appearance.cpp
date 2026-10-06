#include "core/model_appearance.h"
#include "core/model_material_slots.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>

#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text { Q_DECLARE_TR_FUNCTIONS(VibeStudioModelAppearance) };
bool fail(QString *error, const QString &message)
{
	if (error) { *error = message; }
	return false;
}
}

QJsonObject modelAppearanceJson(const ModelAppearance &appearance)
{
	QJsonArray slotRows;
	auto surfaces = appearance.materialSlots.keys();
	std::sort(surfaces.begin(), surfaces.end());
	for (int surface : surfaces) {
		slotRows.append(QJsonObject{{"surface", surface}, {"slot", appearance.materialSlots.value(surface)}});
	}
	QJsonObject result{{"materialSlots", slotRows}};
	if (appearance.mdlSkin >= 0 || appearance.mdlMember >= 0) {
		result.insert("embeddedSkin", QJsonObject{{"slot", std::max(0, appearance.mdlSkin)},
			{"member", std::max(0, appearance.mdlMember)}});
	}
	if (appearance.skin) {
		result.insert("skinSource", QJsonObject{{"path", appearance.skin->path}, {"entryIndex", qint64(appearance.skin->entryIndex)}});
	}
	return result;
}

bool prepareModelAppearance(const ModelMesh &mesh, const ModelAppearance &appearance,
							const PackageArchiveReader *archive, ModelAppearanceSnapshot *output,
							QString *error, const ModelWorkControl &control)
{
	if (error) { error->clear(); }
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error)) { return false; }
	if (!output || appearance.mdlSkin < -1 || appearance.mdlMember < -1) {
		return fail(error, Text::tr("Choose a valid model appearance and output."));
	}
	const bool embedded = mesh.mdl.enabled || !mesh.embeddedSkins.isEmpty();
	if ((embedded && (!appearance.materialSlots.isEmpty() || appearance.skin)) ||
		(!embedded && (appearance.mdlSkin >= 0 || appearance.mdlMember >= 0)) ||
		(appearance.skin && !appearance.materialSlots.isEmpty())) {
		return fail(error, Text::tr("Choose embedded MDL skins, external material slots, or a .skin file separately."));
	}
	ModelAppearanceSnapshot candidate;
	if (!modelMaterialPreviewSnapshot(mesh, appearance.materialSlots, &candidate.materials, error)) { return false; }
	candidate.receipt = modelAppearanceJson(appearance);
	candidate.receipt.insert("previewOnly", true);
	if (embedded) {
		const int skin = std::max(0, appearance.mdlSkin), member = std::max(0, appearance.mdlMember);
		if (!mesh.mdl.enabled || skin >= mesh.embeddedSkins.size() ||
			member >= mesh.embeddedSkins[skin].indexedFrames.size()) {
			return fail(error, Text::tr("Choose an existing MDL skin slot and member."));
		}
		const auto image = modelMdlSkinImage(mesh.embeddedSkins[skin].indexedFrames[member], mesh.mdl.skinSize, mesh.mdl.palette, control);
		if (image.isNull()) {
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error)) { return false; }
			return fail(error, Text::tr("The selected MDL skin could not be decoded."));
		}
		candidate.materials.embeddedSkins = {ModelEmbeddedSkin{}};
		candidate.materials.embeddedSkins[0].image = image;
		candidate.receipt.insert("embeddedSkin", QJsonObject{{"slot", skin}, {"member", member},
			{"members", mesh.embeddedSkins[skin].indexedFrames.size()}, {"width", image.width()}, {"height", image.height()},
			{"indexedSha256", QString::fromLatin1(QCryptographicHash::hash(mesh.embeddedSkins[skin].indexedFrames[member], QCryptographicHash::Sha256).toHex())},
			{"paletteSha256", QString::fromLatin1(QCryptographicHash::hash(mesh.mdl.palette, QCryptographicHash::Sha256).toHex())}});
		candidate.details << Text::tr("MDL skin %1, member %2 of %3. This member is held while geometry playback continues.")
			.arg(skin).arg(member).arg(mesh.embeddedSkins[skin].indexedFrames.size());
	} else if (appearance.skin) {
		if (!archive) { return fail(error, Text::tr("Open a package before choosing a .skin preview.")); }
		ModelSkinBindingInput input;
		ModelSkinBindingPlan bindings;
		if (!readModelSkinBindings(*archive, *appearance.skin, &input, error, control) ||
			!planModelSkinBindings(mesh, input.bytes, &bindings, error, control)) { return false; }
		for (const auto &assignment : bindings.assignments) {
			candidate.materials.surfaces[assignment.surface].skinPaths = {assignment.material};
		}
		const auto sha = QString::fromLatin1(QCryptographicHash::hash(input.bytes, QCryptographicHash::Sha256).toHex());
		candidate.receipt.insert("skinSource", QJsonObject{{"path", input.path}, {"entryIndex", qint64(input.entryIndex)},
			{"bytes", input.bytes.size()}, {"sha256", sha}, {"bindings", modelSkinBindingPlanJson(bindings)}});
		candidate.details << input.path << Text::tr("Package entry %1 · SHA-256: %2").arg(input.entryIndex).arg(sha);
		candidate.details += modelSkinBindingPlanText(bindings);
	}
	QJsonArray materials;
	for (int surface = 0; surface < candidate.materials.surfaces.size(); ++surface) {
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, surface, candidate.materials.surfaces.size(), error)) { return false; }
		const auto &item = candidate.materials.surfaces[surface];
		materials.append(QJsonObject{{"surface", surface}, {"name", item.name}, {"material", item.skinPaths.value(0)}});
	}
	candidate.receipt.insert("surfaces", materials);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 1, 1, error)) { return false; }
	*output = std::move(candidate);
	return true;
}
} // namespace vibestudio
