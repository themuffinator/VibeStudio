#include "core/model_document.h"
#include "core/model_mdl.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &text)
{
	if (error)
	{
		*error = text;
	}
	return false;
}
bool validTimes(const QVector<float> &times, int count)
{
	if (times.isEmpty())
	{
		return count == 1;
	}
	if (times.size() != count)
	{
		return false;
	}
	float previous = 0;
	for (float value : times)
	{
		if (!std::isfinite(value) || value <= previous)
		{
			return false;
		}
		previous = value;
	}
	return true;
}
double cycleTime(double seconds, double phase, double cycle)
{
	// Reduce separately to avoid overflow for any finite nonnegative timestamp.
	return std::fmod(std::fmod(seconds, cycle) + std::fmod(phase, cycle), cycle);
}
int storedMember(const QVector<float> &times, double seconds, double phase)
{
	if (times.isEmpty())
	{
		return 0;
	}
	const double time = cycleTime(seconds, phase, times.back());
	return int(std::upper_bound(times.cbegin(), times.cend(), time) - times.cbegin());
}
} // namespace

// Original implementation from the timing rules in id Software Quake's
// WinQuake/r_alias.c (R_AliasSetupSkin/Frame), gl_rmain.c (R_SetupAliasFrame,
// R_DrawAliasModel) and gl_model.c (Mod_LoadAllSkins), master reviewed 2026-10-04.
// https://github.com/id-Software/Quake/tree/master/WinQuake
// GPL-2.0-or-later is compatible with this repository's GPL-3.0. No engine code
// is copied. Stable double modular arithmetic avoids the engines' int overflow
// at large times; this samples their schedules, not their floating-point bugs.
bool sampleModelMdl(const ModelMesh &mesh, const ModelMdlPlayback &playback, ModelMdlPlaybackSample *output, QString *error)
{
	if (!output || !mesh.mdl.enabled || playback.nativeFrame < 0 || playback.nativeFrame >= mesh.mdl.frameGroups.size() ||
		playback.skin < 0 || playback.skin >= mesh.embeddedSkins.size() || !std::isfinite(playback.seconds) || playback.seconds < 0 ||
		!std::isfinite(playback.syncPhase) || playback.syncPhase < 0 || playback.syncPhase > 1 ||
		(playback.timing != ModelMdlTiming::Stored && playback.timing != ModelMdlTiming::GlQuake) ||
		(mesh.mdl.syncType != 0 && mesh.mdl.syncType != 1))
	{
		return fail(error,
					QCoreApplication::translate(
						"VibeStudioModelMdl",
						"Choose an existing native MDL frame and skin, a nonnegative finite time and a sync phase from 0 to 1 second."));
	}
	const auto &group = mesh.mdl.frameGroups[playback.nativeFrame];
	const auto &skin = mesh.embeddedSkins[playback.skin];
	const int count = group.frameCount(), members = int(skin.indexedFrames.size());
	if (count < 1 || count > modelDocumentMaxFrames || group.firstFrame < 0 || group.firstFrame > mesh.frames.size() - count ||
		members < 1 || members > modelMdlMaxSkinMembers || !validTimes(group.intervals, count) || !validTimes(skin.intervals, members))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelMdl",
													   "Native playback requires valid cumulative pose and skin group times."));
	}
	ModelMdlPlaybackSample sample;
	sample.frame = group.firstFrame;
	if (playback.timing == ModelMdlTiming::Stored)
	{
		const double phase = mesh.mdl.syncType == 1 ? playback.syncPhase : 0;
		sample.frame += storedMember(group.intervals, playback.seconds, phase);
		sample.skinMember = storedMember(skin.intervals, playback.seconds, phase);
		sample.frameCycle = group.intervals.isEmpty() ? 0 : group.intervals.back();
		sample.skinCycle = skin.intervals.isEmpty() ? 0 : skin.intervals.back();
	}
	else
	{
		if (count > 1)
		{
			sample.frameCycle = double(group.intervals.front()) * count;
			sample.frame += std::min(count - 1, int(cycleTime(playback.seconds, 0, sample.frameCycle) / group.intervals.front()));
		}
		if (members > 1)
		{
			// GLQuake cycles four slots at 10 Hz, ignoring native skin times.
			// Short groups repeat; longer groups overwrite each j & 3 slot.
			sample.skinCycle = .4;
			// Scale before wrapping, as the original renderer does. Wrapping at
			// 0.4 first loses a tick at ordinary boundaries such as 0.6 seconds.
			// Beyond multiplication's finite range, doubles represent whole even
			// seconds, so an exact two-second reduction preserves the slot.
			const double ticks =
				playback.seconds < std::numeric_limits<double>::max() / 10 ? playback.seconds * 10 : std::fmod(playback.seconds, 2) * 10;
			const int slot = int(std::fmod(ticks, 4));
			sample.skinMember = members < 4 ? slot % members : slot + 4 * ((members - 1 - slot) / 4);
		}
	}
	*output = sample;
	if (error)
	{
		error->clear();
	}
	return true;
}

bool prepareModelMdlPlaybackSkins(const ModelMesh &mesh, int skin, QVector<QImage> *output, QString *error, const ModelWorkControl &control)
{
	ModelMdlPlaybackSample sample;
	ModelMdlPlayback request;
	request.skin = skin;
	if (!output || !sampleModelMdl(mesh, request, &sample, error))
	{
		return false;
	}
	const auto &source = mesh.embeddedSkins[skin];
	const qint64 pixels = qint64(mesh.mdl.skinSize.width()) * mesh.mdl.skinSize.height();
	if (mesh.mdl.skinSize.width() < 1 || mesh.mdl.skinSize.height() < 1 || pixels > modelMdlMaxSkinPixels / source.indexedFrames.size())
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Native playback skins must total at most 16 megapixels."));
	}
	QVector<QImage> images;
	for (const auto &indexed : source.indexedFrames)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, images.size(), source.indexedFrames.size(), error))
		{
			return false;
		}
		auto image = modelMdlSkinImage(indexed, mesh.mdl.skinSize, mesh.mdl.palette, control);
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, images.size(), source.indexedFrames.size(), error))
		{
			return false;
		}
		// RGB32 is already opaque, with the identical byte layout. No conversion
		// allocation is needed and no transparent index is introduced.
		if (image.isNull() || !image.reinterpretAsFormat(QImage::Format_ARGB32_Premultiplied))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "Unable to prepare an indexed MDL skin for playback."));
		}
		images.append(std::move(image));
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, images.size(), images.size(), error))
	{
		return false;
	}
	*output = std::move(images);
	return true;
}
} // namespace vibestudio
