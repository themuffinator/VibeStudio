#include "core/model_mdl.h"

#include "core/model_document.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
bool intervalsValid(const QVector<float> &values)
{
	float previous = 0;
	for (float value : values)
	{
		if (!std::isfinite(value) || value <= previous)
		{
			return false;
		}
		previous = value;
	}
	return true;
}
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
} // namespace

QByteArray modelMdlPaletteBytes(const IdTechPalette &palette)
{
	if (!palette.isValid())
	{
		return {};
	}
	QByteArray result(768, '\0');
	for (int index = 0; index < 256; ++index)
	{
		const auto color = palette.colorAt(index);
		result[index * 3] = char(qRed(color));
		result[index * 3 + 1] = char(qGreen(color));
		result[index * 3 + 2] = char(qBlue(color));
	}
	return result;
}

QImage modelMdlSkinImage(const QByteArray &pixels, QSize size, const QByteArray &palette, const ModelWorkControl &control)
{
	const qint64 count = qint64(size.width()) * size.height();
	if (size.width() <= 0 || size.height() <= 0 || count > modelMdlMaxSkinPixels || pixels.size() != count || palette.size() != 768 ||
		!modelWorkCheckpoint(control, ModelWorkPhase::Reading))
	{
		return {};
	}
	QImage image(size, QImage::Format_RGB32);
	if (image.isNull())
	{
		return {};
	}
	QRgb colors[256];
	for (int index = 0; index < 256; ++index)
	{
		colors[index] = qRgb(quint8(palette[index * 3]), quint8(palette[index * 3 + 1]), quint8(palette[index * 3 + 2]));
	}
	for (int y = 0; y < size.height(); ++y)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, y, size.height()))
		{
			return {};
		}
		auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
		for (int x = 0; x < size.width(); ++x)
		{
			row[x] = colors[quint8(pixels[y * size.width() + x])];
		}
	}
	return modelWorkCheckpoint(control, ModelWorkPhase::Reading, size.height(), size.height()) ? image : QImage{};
}

QStringList validateModelMdl(const ModelMesh &mesh, const ModelWorkControl &control)
{
	QString cancelled;
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &cancelled);
	if (!work.check())
	{
		return {cancelled};
	}
	if (!mesh.mdl.enabled)
	{
		if (mesh.format == ModelMeshFormat::QuakeMdl)
		{
			return {QCoreApplication::translate("VibeStudioModelMdl",
												"MDL authoring requires complete native groups, indexed skins and header settings.")};
		}
		for (const auto &skin : mesh.embeddedSkins)
		{
			if (!skin.indexedFrames.isEmpty() || !skin.intervals.isEmpty())
			{
				return {
					QCoreApplication::translate("VibeStudioModelMdl", "Indexed skin data requires MDL settings and a preview palette.")};
			}
		}
		return {};
	}
	const auto &settings = mesh.mdl;
	const auto size = settings.skinSize;
	if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192 || settings.palette.size() != 768 ||
		(settings.syncType != 0 && settings.syncType != 1) || !std::isfinite(settings.size) || settings.size < 0 ||
		!std::isfinite(settings.eyePosition.x) || !std::isfinite(settings.eyePosition.y) || !std::isfinite(settings.eyePosition.z))
	{
		return {QCoreApplication::translate(
			"VibeStudioModelMdl",
			"MDL settings require a 256-colour palette, dimensions from 1 to 8192, a valid sync type and finite header values.")};
	}
	int next = 0;
	if (settings.frameGroups.isEmpty() || settings.frameGroups.size() > modelDocumentMaxFrames)
	{
		return {QCoreApplication::translate("VibeStudioModelMdl", "MDL native frames must cover every pose in order.")};
	}
	for (const auto &group : settings.frameGroups)
	{
		if (!work.step())
		{
			return {cancelled};
		}
		if (group.firstFrame != next || group.intervals.size() > modelDocumentMaxFrames || group.frameCount() > mesh.frames.size() - next ||
			!intervalsValid(group.intervals))
		{
			return {QCoreApplication::translate(
				"VibeStudioModelMdl",
				"MDL native frames must cover every pose in order, with strictly increasing positive group end times.")};
		}
		next += group.frameCount();
	}
	if (next != mesh.frames.size())
	{
		return {QCoreApplication::translate("VibeStudioModelMdl", "MDL native frames must cover every pose in order.")};
	}
	const qint64 perImage = qint64(size.width()) * size.height();
	qint64 total = 0;
	for (const auto &skin : mesh.embeddedSkins)
	{
		if (!work.step())
		{
			return {cancelled};
		}
		const auto members = skin.indexedFrames.size();
		if (members < 1 || members > modelMdlMaxSkinMembers || members != skin.groupFrameCount || skin.image.size() != size ||
			(skin.intervals.isEmpty() ? members != 1 : skin.intervals.size() != members) || !intervalsValid(skin.intervals))
		{
			return {QCoreApplication::translate(
				"VibeStudioModelMdl", "MDL skins need matching indexed images, a first-member preview and valid cumulative group times.")};
		}
		for (const auto &pixels : skin.indexedFrames)
		{
			total += perImage;
			if (!work.step())
			{
				return {cancelled};
			}
			if (pixels.size() != perImage || total > modelMdlMaxSkinPixels)
			{
				return {QCoreApplication::translate("VibeStudioModelMdl",
													"MDL skin members must match the skin dimensions and total at most 16 megapixels.")};
			}
		}
	}
	return work.check() ? QStringList{} : QStringList{cancelled};
}

qint64 modelMdlStorageBytes(const ModelMesh &mesh)
{
	qint64 bytes = mesh.mdl.palette.size();
	for (const auto &group : mesh.mdl.frameGroups)
	{
		bytes += sizeof(ModelMdlFrameGroup) + group.intervals.size() * qint64(sizeof(float));
	}
	for (const auto &skin : mesh.embeddedSkins)
	{
		bytes += skin.intervals.size() * qint64(sizeof(float)) + skin.indexedFrames.size() * qint64(sizeof(QByteArray));
		for (const auto &pixels : skin.indexedFrames)
		{
			bytes += pixels.size();
		}
	}
	return bytes;
}

bool changeModelMdlFrames(ModelMesh *mesh, ModelMdlFrameChange change, int frame, int count, QString *error)
{
	if (error)
	{
		error->clear();
	}
	const auto invalid = [&]()
	{ return fail(error, QCoreApplication::translate("VibeStudioModelMdl", "The requested MDL pose or native-frame change is invalid.")); };
	if (!mesh || frame < 0 || frame >= mesh->frames.size() || count < 1 || count > modelDocumentMaxFrames ||
		(change != ModelMdlFrameChange::Duplicate && change != ModelMdlFrameChange::Delete &&
		 change != ModelMdlFrameChange::InsertInbetweens) ||
		(change != ModelMdlFrameChange::InsertInbetweens && count != 1) ||
		(change == ModelMdlFrameChange::InsertInbetweens && frame + 1 >= mesh->frames.size()))
	{
		return invalid();
	}
	if (!mesh->mdl.enabled)
	{
		return true;
	}
	const auto errors = validateModelMdl(*mesh);
	if (!errors.isEmpty())
	{
		return fail(error, errors.join(QLatin1Char('\n')));
	}
	auto groups = mesh->mdl.frameGroups;
	int index = 0;
	while (index < groups.size() && frame >= groups[index].firstFrame + groups[index].frameCount())
	{
		++index;
	}
	if (index == groups.size())
	{
		return invalid();
	}
	auto &group = groups[index];
	const int member = frame - group.firstFrame;
	const bool timed = !group.intervals.isEmpty();
	const bool insertInside = timed && member + 1 < group.frameCount();
	if (change == ModelMdlFrameChange::Delete)
	{
		if (group.frameCount() == 1)
		{
			groups.removeAt(index);
		}
		else
		{
			const double duration = double(group.intervals[member]) - (member ? group.intervals[member - 1] : 0);
			group.intervals.removeAt(member);
			for (int i = member; i < group.intervals.size(); ++i)
			{
				group.intervals[i] = float(double(group.intervals[i]) - duration);
			}
		}
	}
	else if (change == ModelMdlFrameChange::Duplicate && timed)
	{
		const double duration = double(group.intervals[member]) - (member ? group.intervals[member - 1] : 0);
		group.intervals.insert(member + 1, group.intervals[member]);
		for (int i = member + 1; i < group.intervals.size(); ++i)
		{
			group.intervals[i] = float(double(group.intervals[i]) + duration);
		}
	}
	else if (change == ModelMdlFrameChange::InsertInbetweens && insertInside)
	{
		const float end = group.intervals[member];
		const double start = member ? group.intervals[member - 1] : 0;
		for (int i = 0; i < count; ++i)
		{
			group.intervals.insert(member + i, float(start + (double(end) - start) * (i + 1) / (count + 1)));
		}
	}
	else
	{
		for (int i = 0; i < count; ++i)
		{
			groups.insert(index + 1 + i, ModelMdlFrameGroup{});
		}
	}
	int next = 0;
	for (auto &entry : groups)
	{
		entry.firstFrame = next;
		next += entry.frameCount();
		if (!intervalsValid(entry.intervals))
		{
			return fail(
				error,
				QCoreApplication::translate(
					"VibeStudioModelMdl",
					"The pose edit would produce indistinguishable or non-finite MDL group times. Adjust the native frame timing first."));
		}
	}
	if (next < 1 || next > modelDocumentMaxFrames)
	{
		return invalid();
	}
	mesh->mdl.frameGroups = std::move(groups);
	return true;
}
} // namespace vibestudio
