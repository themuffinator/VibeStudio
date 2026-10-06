#include "core/model_mdl.h"

#include "core/model_document.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstring>

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
bool durationValid(double duration) { return std::isfinite(duration) && duration >= .000001 && duration <= 3600; }
void setDuration(QVector<float> *times, int member, double duration)
{
	const double previous = member ? times->at(member - 1) : 0;
	const double difference = duration - (double(times->at(member)) - previous);
	for (int i = member; i < times->size(); ++i)
	{
		(*times)[i] = float(double(times->at(i)) + difference);
	}
}
ModelMdlFrameGroup slice(const ModelMdlFrameGroup &group, int first, int count)
{
	ModelMdlFrameGroup result{first, {}};
	if (!group.intervals.isEmpty())
	{
		const int local = first - group.firstFrame;
		const double start = local ? group.intervals[local - 1] : 0;
		for (int i = local; i < local + count; ++i)
		{
			result.intervals << float(double(group.intervals[i]) - start);
		}
	}
	return result;
}
bool inputValid(const ModelMdlSkinInput &input)
{
	return input.size.width() >= 1 && input.size.height() >= 1 && input.size.width() <= 8192 && input.size.height() <= 8192 &&
		   qint64(input.size.width()) * input.size.height() <= modelMdlMaxSkinPixels &&
		   input.pixels.size() == qint64(input.size.width()) * input.size.height() && input.palette.size() == 768;
}
} // namespace

IdTechPalette modelMdlPreviewPalette(const ModelMesh &mesh)
{
	IdTechPalette palette;
	if (mesh.mdl.enabled && parseIdTechPaletteBytes(mesh.mdl.palette, QStringLiteral("quake"), &palette))
	{
		palette.generated = mesh.mdl.paletteGenerated;
	}
	else
	{
		palette = generatedIdTechPalette(QStringLiteral("quake"));
	}
	// Quake pictures may treat index 255 as transparent; MDL skins do not.
	palette.transparentIndex = -1;
	for (auto &color : palette.colors)
	{
		color = qRgb(qRed(color), qGreen(color), qBlue(color));
	}
	return palette;
}

bool decodeModelMdlSkin(const QString &path, const QByteArray &bytes, const IdTechPalette &fallbackPalette, ModelMdlSkinInput *skin,
						QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!skin || bytes.isEmpty() || bytes.size() > modelFileByteLimit || !fallbackPalette.isValid())
	{
		return fail(
			error, QCoreApplication::translate("VibeStudioModelMdl", "Choose a complete indexed skin image and a valid fallback palette."));
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	const auto format = detectIdTechImageFormat(path, bytes);
	const bool png = format == IdTechImageFormat::QtNative && bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"));
	if (!png && format != IdTechImageFormat::Pcx && format != IdTechImageFormat::QuakeLump &&
		format != IdTechImageFormat::QuakeMipTexture && format != IdTechImageFormat::Quake2Wal && format != IdTechImageFormat::Quake2M8)
	{
		return fail(error, QCoreApplication::translate(
							   "VibeStudioModelMdl",
							   "MDL skin import supports indexed PNG, single-plane PCX, Quake LMP/miptextures, WAL or M8 images."));
	}
	IdTechImageDecodeContext context;
	context.maximumDimension = 8192;
	context.maximumImagePixels = modelMdlMaxSkinPixels;
	context.maximumTotalPixels = modelMdlMaxSkinPixels * 2; // Includes the native mip chain; only level zero becomes a skin.
	context.isCancelled = control.cancelled;
	auto suppliedPalette = fallbackPalette;
	// Keep caller labels from being mistaken for the decoder's embedded-palette
	// identifiers. Pixel colours and generated provenance remain unchanged.
	suppliedPalette.id = QStringLiteral("mdl-supplied");
	const auto decoded = decodeIdTechImage(path, bytes, suppliedPalette, context);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	if (!decoded.decoded || !decoded.error.isEmpty() || !decoded.warnings.isEmpty())
	{
		return fail(error, decoded.error.isEmpty() ? decoded.warnings.join(QLatin1Char('\n')) : decoded.error);
	}
	const auto &image = decoded.image;
	if (image.format() != QImage::Format_Indexed8 || image.colorCount() != 256)
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelMdl", "The skin needs 8-bit indexed pixels and all 256 palette entries. "
																	  "Convert it explicitly in the texture workflow before import."));
	}
	ModelMdlSkinInput result;
	result.size = image.size();
	result.paletteGenerated = decoded.paletteGenerated;
	result.paletteEmbedded = png || decoded.paletteId == QStringLiteral("pcx-embedded") ||
							 decoded.paletteId == QStringLiteral("wad3-embedded") || decoded.paletteId == QStringLiteral("m8-embedded");
	result.palette.resize(768);
	for (int i = 0; i < 256; ++i)
	{
		const auto color = image.color(i);
		if (qAlpha(color) != 255)
		{
			return fail(error, QCoreApplication::translate(
								   "VibeStudioModelMdl",
								   "Original Quake MDL skins are opaque. Remove palette transparency explicitly before import."));
		}
		result.palette[i * 3] = char(qRed(color));
		result.palette[i * 3 + 1] = char(qGreen(color));
		result.palette[i * 3 + 2] = char(qBlue(color));
	}
	result.pixels.resize(qint64(image.width()) * image.height());
	for (int y = 0; y < image.height(); ++y)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, y, image.height(), error))
		{
			return false;
		}
		std::memcpy(result.pixels.data() + qint64(y) * image.width(), image.constScanLine(y), image.width());
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, image.height(), image.height(), error))
	{
		return false;
	}
	*skin = std::move(result);
	return true;
}

bool isModelMdlEdit(ModelEditKind kind)
{
	switch (kind)
	{
	case ModelEditKind::AddMdlSkin:
	case ModelEditKind::ReplaceMdlSkinMember:
	case ModelEditKind::AppendMdlSkinMember:
	case ModelEditKind::RemoveMdlSkin:
	case ModelEditKind::RemoveMdlSkinMember:
	case ModelEditKind::SetMdlSkinDuration:
	case ModelEditKind::SetMdlHeader:
	case ModelEditKind::SetMdlPalette:
	case ModelEditKind::GroupMdlFrames:
	case ModelEditKind::UngroupMdlFrames:
	case ModelEditKind::SetMdlFrameDuration:
		return true;
	default:
		return false;
	}
}

bool applyModelMdlEdit(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	if (!mesh || !isModelMdlEdit(edit.kind))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Choose a valid MDL editing operation."));
	}
	const bool add = edit.kind == ModelEditKind::AddMdlSkin;
	const bool imageEdit = add || edit.kind == ModelEditKind::ReplaceMdlSkinMember || edit.kind == ModelEditKind::AppendMdlSkinMember;
	if (!mesh->mdl.enabled && !add)
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelMdl", "Add an indexed MDL skin before editing native model settings."));
	}
	const bool timed = edit.kind == ModelEditKind::AppendMdlSkinMember || edit.kind == ModelEditKind::SetMdlSkinDuration ||
					   edit.kind == ModelEditKind::GroupMdlFrames || edit.kind == ModelEditKind::SetMdlFrameDuration;
	if (timed && !durationValid(edit.mdlDuration))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelMdl", "Native member duration must be from 0.000001 to 3600 seconds."));
	}
	if (imageEdit)
	{
		if (!inputValid(edit.mdlSkin))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelMdl",
														   "The indexed skin payload, dimensions or palette are incomplete."));
		}
		if (!mesh->mdl.enabled)
		{
			if (!mesh->embeddedSkins.isEmpty())
			{
				return fail(
					error, QCoreApplication::translate(
							   "VibeStudioModelMdl", "Existing non-indexed embedded skins need explicit conversion before MDL authoring."));
			}
			mesh->mdl = {};
			mesh->mdl.enabled = true;
			for (int f = 0; f < mesh->frames.size(); ++f)
			{
				mesh->mdl.frameGroups << ModelMdlFrameGroup{f, {}};
			}
		}
		if (add && mesh->embeddedSkins.isEmpty())
		{
			mesh->mdl.skinSize = edit.mdlSkin.size;
			mesh->mdl.palette = edit.mdlSkin.palette;
			mesh->mdl.paletteGenerated = edit.mdlSkin.paletteGenerated;
		}
		if (mesh->mdl.skinSize != edit.mdlSkin.size || mesh->mdl.palette != edit.mdlSkin.palette)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelMdl",
														   "Every MDL skin member must share the model dimensions and "
														   "palette. Resize or convert the image explicitly before import."));
		}
		qint64 pixels = edit.mdlSkin.pixels.size();
		for (int s = 0; s < mesh->embeddedSkins.size(); ++s)
		{
			const auto &skin = mesh->embeddedSkins[s];
			pixels += qint64(skin.indexedFrames.size()) * edit.mdlSkin.pixels.size();
			if (edit.kind == ModelEditKind::ReplaceMdlSkinMember && s == edit.mdlSkinSlot)
			{
				pixels -= edit.mdlSkin.pixels.size();
			}
		}
		if (pixels > modelMdlMaxSkinPixels || (add && mesh->embeddedSkins.size() >= 256))
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelMdl",
													"MDL source skins are limited to 256 slots and 16 megapixels across all members."));
		}
	}
	if (add)
	{
		ModelEmbeddedSkin skin;
		skin.index = mesh->embeddedSkins.size();
		skin.name = edit.text.isEmpty() ? QStringLiteral("skin%1").arg(skin.index) : edit.text;
		skin.groupFrameCount = 1;
		skin.indexedFrames = {edit.mdlSkin.pixels};
		skin.image = modelMdlSkinImage(edit.mdlSkin.pixels, mesh->mdl.skinSize, mesh->mdl.palette, control);
		mesh->embeddedSkins << std::move(skin);
	}
	else if (edit.kind == ModelEditKind::SetMdlHeader)
	{
		mesh->mdl.eyePosition = edit.mdlSettings.eyePosition;
		mesh->mdl.flags = edit.mdlSettings.flags;
		mesh->mdl.syncType = edit.mdlSettings.syncType;
		mesh->mdl.size = edit.mdlSettings.size;
	}
	else if (edit.kind == ModelEditKind::SetMdlPalette)
	{
		if (edit.mdlSettings.palette.size() != 768)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelMdl",
														   "A model palette needs exactly 768 RGB bytes. Skin indices are preserved."));
		}
		mesh->mdl.palette = edit.mdlSettings.palette;
		mesh->mdl.paletteGenerated = edit.mdlSettings.paletteGenerated;
		for (auto &skin : mesh->embeddedSkins)
		{
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, 0, error))
			{
				return false;
			}
			skin.image = modelMdlSkinImage(skin.indexedFrames[0], mesh->mdl.skinSize, mesh->mdl.palette, control);
		}
	}
	else if (edit.kind == ModelEditKind::GroupMdlFrames || edit.kind == ModelEditKind::UngroupMdlFrames)
	{
		if (edit.rangeFirst < 0 || edit.rangeLast < edit.rangeFirst || edit.rangeLast >= mesh->frames.size())
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelMdl", "Choose a valid inclusive pose range for native MDL frames."));
		}
		QVector<ModelMdlFrameGroup> groups;
		const int end = edit.rangeLast + 1;
		bool inserted = false;
		for (const auto &group : mesh->mdl.frameGroups)
		{
			const int groupEnd = group.firstFrame + group.frameCount();
			if (groupEnd <= edit.rangeFirst || group.firstFrame >= end)
			{
				groups << group;
				continue;
			}
			if (group.firstFrame < edit.rangeFirst)
			{
				groups << slice(group, group.firstFrame, edit.rangeFirst - group.firstFrame);
			}
			if (!inserted)
			{
				inserted = true;
				if (edit.kind == ModelEditKind::GroupMdlFrames)
				{
					ModelMdlFrameGroup added{edit.rangeFirst, {}};
					for (int f = edit.rangeFirst; f < end; ++f)
					{
						added.intervals << float((f - edit.rangeFirst + 1) * edit.mdlDuration);
					}
					groups << added;
				}
				else
				{
					for (int f = edit.rangeFirst; f < end; ++f)
					{
						groups << ModelMdlFrameGroup{f, {}};
					}
				}
			}
			if (groupEnd > end)
			{
				groups << slice(group, end, groupEnd - end);
			}
		}
		mesh->mdl.frameGroups = std::move(groups);
	}
	else if (edit.kind == ModelEditKind::SetMdlFrameDuration)
	{
		if (edit.frame < 0 || edit.frame >= mesh->frames.size())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Choose a valid pose for native group timing."));
		}
		for (auto &group : mesh->mdl.frameGroups)
		{
			if (edit.frame < group.firstFrame || edit.frame >= group.firstFrame + group.frameCount())
			{
				continue;
			}
			if (group.intervals.isEmpty())
			{
				group.intervals = {float(edit.mdlDuration)};
			}
			else
			{
				setDuration(&group.intervals, edit.frame - group.firstFrame, edit.mdlDuration);
			}
			break;
		}
	}
	else
	{
		if (edit.mdlSkinSlot < 0 || edit.mdlSkinSlot >= mesh->embeddedSkins.size())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Choose an existing MDL skin slot."));
		}
		auto &skin = mesh->embeddedSkins[edit.mdlSkinSlot];
		if (edit.kind == ModelEditKind::RemoveMdlSkin)
		{
			mesh->embeddedSkins.removeAt(edit.mdlSkinSlot);
		}
		else if (edit.kind == ModelEditKind::AppendMdlSkinMember)
		{
			if (skin.indexedFrames.size() >= modelMdlMaxSkinMembers)
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "An MDL skin group supports at most 256 members."));
			}
			if (skin.intervals.isEmpty())
			{
				skin.intervals = {float(edit.mdlDuration)};
			}
			skin.intervals << float(double(skin.intervals.last()) + edit.mdlDuration);
			skin.indexedFrames << edit.mdlSkin.pixels;
			skin.groupFrameCount = skin.indexedFrames.size();
		}
		else
		{
			if (edit.mdlSkinMember < 0 || edit.mdlSkinMember >= skin.indexedFrames.size())
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Choose an existing MDL skin member."));
			}
			if (edit.kind == ModelEditKind::ReplaceMdlSkinMember)
			{
				skin.indexedFrames[edit.mdlSkinMember] = edit.mdlSkin.pixels;
			}
			else if (edit.kind == ModelEditKind::RemoveMdlSkinMember)
			{
				if (skin.indexedFrames.size() == 1)
				{
					return fail(error, QCoreApplication::translate("VibeStudioModelMdl",
																   "Use Remove Skin to delete the last member of a skin slot."));
				}
				setDuration(&skin.intervals, edit.mdlSkinMember, 0);
				skin.intervals.removeAt(edit.mdlSkinMember);
				skin.indexedFrames.removeAt(edit.mdlSkinMember);
				skin.groupFrameCount = skin.indexedFrames.size();
			}
			else if (edit.kind == ModelEditKind::SetMdlSkinDuration)
			{
				if (skin.intervals.isEmpty())
				{
					skin.intervals = {float(edit.mdlDuration)};
				}
				else
				{
					setDuration(&skin.intervals, edit.mdlSkinMember, edit.mdlDuration);
				}
			}
			if (imageEdit || edit.kind == ModelEditKind::RemoveMdlSkinMember)
			{
				skin.image = modelMdlSkinImage(skin.indexedFrames[0], mesh->mdl.skinSize, mesh->mdl.palette, control);
			}
		}
	}
	for (int i = 0; i < mesh->embeddedSkins.size(); ++i)
	{
		mesh->embeddedSkins[i].index = i;
	}
	return modelWorkCheckpoint(control, ModelWorkPhase::Editing, 1, 1, error);
}
} // namespace vibestudio
