#include "core/model_material_slots.h"
#include "core/package_archive.h"
#include <QCoreApplication>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelMaterialSlots)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool valid(const QStringList &paths, QString *error)
{
	if (paths.size() > modelMaxMaterialSlots)
		return fail(error, Text::tr("A surface can reference at most 256 material slots."));
	for (const auto &path : paths)
		if (!isSafePackageVirtualPath(path) || path.size() > 255)
			return fail(error, Text::tr("Each material slot requires a safe package-relative path of at most 255 characters."));
	return true;
}
} // namespace
bool supportsModelMaterialSlots(const ModelMesh &mesh)
{
	return !mesh.mdl.enabled && mesh.embeddedSkins.isEmpty();
}
bool editModelMaterialSlots(const QStringList &source, const ModelMaterialSlotEdit &edit, QStringList *result, QString *error)
{
	if (error)
		error->clear();
	if (!result)
		return fail(error, Text::tr("No material slot output was provided."));
	if (!valid(source, error) || !valid(edit.materials, error))
		return false;
	const bool indexed = edit.action == ModelMaterialSlotAction::Set || edit.action == ModelMaterialSlotAction::Insert ||
						 edit.action == ModelMaterialSlotAction::Remove || edit.action == ModelMaterialSlotAction::Move;
	if (indexed ? (edit.slot < 0 || edit.slot >= source.size() + int(edit.action == ModelMaterialSlotAction::Insert)) : edit.slot != -1)
		return fail(error, Text::tr("Choose an existing material slot, or an insertion index from zero through the slot count."));
	if (edit.action == ModelMaterialSlotAction::Move ? (edit.destination < 0 || edit.destination >= source.size()) : edit.destination != -1)
		return fail(error, Text::tr("Only Move accepts a destination; it must be an existing slot index."));
	const bool single = edit.action == ModelMaterialSlotAction::Set || edit.action == ModelMaterialSlotAction::Insert;
	if ((single && edit.materials.size() != 1) || (!single && edit.action != ModelMaterialSlotAction::Replace && !edit.materials.isEmpty()))
		return fail(error, Text::tr("Set and Insert require one material; only Replace accepts a list."));
	auto candidate = source;
	switch (edit.action)
	{
	case ModelMaterialSlotAction::Replace:
		candidate = edit.materials;
		break;
	case ModelMaterialSlotAction::Set:
		candidate[edit.slot] = edit.materials.first();
		break;
	case ModelMaterialSlotAction::Insert:
		candidate.insert(edit.slot, edit.materials.first());
		break;
	case ModelMaterialSlotAction::Remove:
		candidate.removeAt(edit.slot);
		break;
	case ModelMaterialSlotAction::Move:
		candidate.move(edit.slot, edit.destination);
		break;
	case ModelMaterialSlotAction::Clear:
		candidate.clear();
		break;
	default:
		return fail(error, Text::tr("Unknown material slot operation."));
	}
	if (!valid(candidate, error))
		return false;
	*result = std::move(candidate);
	return true;
}
bool modelMaterialPreviewSnapshot(const ModelMesh &mesh, const QHash<int, int> &selectedSlots, ModelMesh *result, QString *error)
{
	if (error)
		error->clear();
	if (!result)
		return fail(error, Text::tr("No material preview output was provided."));
	if (!selectedSlots.isEmpty() && !supportsModelMaterialSlots(mesh))
		return fail(error, Text::tr("Embedded MDL skins use the Animation inspector's skin controls."));
	for (auto it = selectedSlots.cbegin(); it != selectedSlots.cend(); ++it)
		if (it.key() < 0 || it.key() >= mesh.surfaces.size() || it.value() < 0 || it.value() >= mesh.surfaces[it.key()].skinPaths.size())
			return fail(error, Text::tr("Choose an existing surface and material slot for preview."));
	ModelMesh snapshot;
	snapshot.sourcePath = mesh.sourcePath;
	snapshot.format = mesh.format;
	// Every embedded skin travels with its name, so surfaces that name theirs
	// (Half-Life textures) find it; MDL's indexed members stay behind.
	for (const auto &skin : mesh.embeddedSkins)
	{
		snapshot.embeddedSkins.append(skin);
		snapshot.embeddedSkins.last().indexedFrames.clear();
		snapshot.embeddedSkins.last().intervals.clear();
	}
	for (int i = 0; i < mesh.surfaces.size(); ++i)
	{
		const auto &surface = mesh.surfaces[i];
		ModelSurface copy;
		copy.index = i;
		copy.name = surface.name;
		if (!surface.skinPaths.isEmpty())
			copy.skinPaths.append(surface.skinPaths[selectedSlots.value(i, 0)]);
		snapshot.surfaces.append(std::move(copy));
	}
	*result = std::move(snapshot);
	return true;
}
} // namespace vibestudio
