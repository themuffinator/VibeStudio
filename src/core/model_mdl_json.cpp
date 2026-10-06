#include "core/model_mdl.h"

#include "core/model_document.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool malformed(QString *error)
{
	if (error)
	{
		*error = QCoreApplication::translate("VibeStudioModelMdl", "The mesh source contains malformed MDL settings or indexed skin data.");
	}
	return false;
}
bool integer(const QJsonValue &value, double maximum)
{
	const double number = value.toDouble(-1);
	return value.isDouble() && number >= 0 && number <= maximum && number == std::floor(number);
}
bool readTimes(const QJsonValue &value, int maximum, QVector<float> *times)
{
	if (!value.isArray() || value.toArray().size() > maximum)
	{
		return false;
	}
	float previous = 0;
	for (const auto &entry : value.toArray())
	{
		const float next = float(entry.toDouble(-1));
		if (!entry.isDouble() || !std::isfinite(next) || next <= previous)
		{
			return false;
		}
		times->append(next);
		previous = next;
	}
	return true;
}
QJsonArray timesJson(const QVector<float> &times)
{
	QJsonArray result;
	for (float time : times)
	{
		result << time;
	}
	return result;
}
bool bytes(const QJsonValue &value, qsizetype expectedSize, QByteArray *result)
{
	if (!value.isString() || value.toString().size() != ((expectedSize + 2) / 3) * 4)
	{
		return false;
	}
	const auto encoded = value.toString().toLatin1();
	auto decoded = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
	if (decoded.size() != expectedSize || decoded.toBase64() != encoded)
	{
		return false;
	}
	*result = std::move(decoded);
	return true;
}
} // namespace

QJsonObject modelMdlSettingsJson(const ModelMdlSettings &settings)
{
	if (!settings.enabled)
	{
		return {};
	}
	QJsonArray groups;
	for (const auto &group : settings.frameGroups)
	{
		groups << QJsonObject{{QStringLiteral("first"), group.firstFrame}, {QStringLiteral("intervals"), timesJson(group.intervals)}};
	}
	return {{QStringLiteral("skinSize"), QJsonArray{settings.skinSize.width(), settings.skinSize.height()}},
			{QStringLiteral("palette"), QString::fromLatin1(settings.palette.toBase64())},
			{QStringLiteral("paletteGenerated"), settings.paletteGenerated},
			{QStringLiteral("eyePosition"), QJsonArray{settings.eyePosition.x, settings.eyePosition.y, settings.eyePosition.z}},
			{QStringLiteral("flags"), double(settings.flags)},
			{QStringLiteral("syncType"), settings.syncType},
			{QStringLiteral("size"), settings.size},
			{QStringLiteral("frameGroups"), groups}};
}

bool parseModelMdlSettings(const QJsonValue &value, ModelMdlSettings *settings, QString *error, const ModelWorkControl &control)
{
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	if (!settings || !value.isObject())
	{
		return malformed(error);
	}
	const auto object = value.toObject();
	ModelMdlSettings result;
	if (object.isEmpty())
	{
		*settings = std::move(result);
		return true;
	}
	result.enabled = true;
	const auto size = object.value(QStringLiteral("skinSize")).toArray();
	const auto eye = object.value(QStringLiteral("eyePosition")).toArray();
	if (object.size() != 8 || size.size() != 2 || !integer(size[0], 8192) || !integer(size[1], 8192) || size[0].toInt() < 1 ||
		size[1].toInt() < 1 || eye.size() != 3 || !bytes(object.value(QStringLiteral("palette")), 768, &result.palette) ||
		!object.value(QStringLiteral("paletteGenerated")).isBool() ||
		!integer(object.value(QStringLiteral("flags")), std::numeric_limits<quint32>::max()) ||
		!integer(object.value(QStringLiteral("syncType")), 1) || !object.value(QStringLiteral("size")).isDouble() ||
		!object.value(QStringLiteral("frameGroups")).isArray())
	{
		return malformed(error);
	}
	float coordinates[3];
	for (int i = 0; i < 3; ++i)
	{
		coordinates[i] = float(eye[i].toDouble());
		if (!eye[i].isDouble() || !std::isfinite(coordinates[i]))
		{
			return malformed(error);
		}
	}
	result.eyePosition = {coordinates[0], coordinates[1], coordinates[2]};
	result.skinSize = {size[0].toInt(), size[1].toInt()};
	result.paletteGenerated = object.value(QStringLiteral("paletteGenerated")).toBool();
	result.flags = quint32(object.value(QStringLiteral("flags")).toDouble());
	result.syncType = object.value(QStringLiteral("syncType")).toInt();
	result.size = float(object.value(QStringLiteral("size")).toDouble());
	const auto groups = object.value(QStringLiteral("frameGroups")).toArray();
	if (!std::isfinite(result.size) || result.size < 0 || groups.isEmpty() || groups.size() > modelDocumentMaxFrames)
	{
		return malformed(error);
	}
	int next = 0;
	for (const auto &entry : groups)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, next, 0, error))
		{
			return false;
		}
		const auto groupObject = entry.toObject();
		ModelMdlFrameGroup group;
		if (groupObject.size() != 2 || !integer(groupObject.value(QStringLiteral("first")), modelDocumentMaxFrames - 1) ||
			groupObject.value(QStringLiteral("first")).toInt() != next ||
			!readTimes(groupObject.value(QStringLiteral("intervals")), modelDocumentMaxFrames - next, &group.intervals))
		{
			return malformed(error);
		}
		group.firstFrame = next;
		next += group.frameCount();
		if (next > modelDocumentMaxFrames)
		{
			return malformed(error);
		}
		result.frameGroups << group;
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, next, next, error))
	{
		return false;
	}
	*settings = std::move(result);
	return true;
}

QJsonObject modelMdlSkinJson(const ModelEmbeddedSkin &skin)
{
	QJsonArray frames;
	for (const auto &pixels : skin.indexedFrames)
	{
		frames << QString::fromLatin1(pixels.toBase64());
	}
	return {{QStringLiteral("name"), skin.name},
			{QStringLiteral("indexedFrames"), frames},
			{QStringLiteral("intervals"), timesJson(skin.intervals)}};
}

bool parseModelMdlSkin(const QJsonValue &value, const ModelMdlSettings &settings, ModelEmbeddedSkin *skin, qint64 *totalPixels,
					   QString *error, const ModelWorkControl &control)
{
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	const auto object = value.toObject();
	if (!skin || !totalPixels || !settings.enabled || object.size() != 3 || !object.value(QStringLiteral("name")).isString() ||
		!object.value(QStringLiteral("indexedFrames")).isArray())
	{
		return malformed(error);
	}
	const auto frames = object.value(QStringLiteral("indexedFrames")).toArray();
	const qint64 perImage = qint64(settings.skinSize.width()) * settings.skinSize.height();
	if (settings.skinSize.width() < 1 || settings.skinSize.height() < 1 || perImage > modelMdlMaxSkinPixels || frames.isEmpty() ||
		frames.size() > modelMdlMaxSkinMembers || *totalPixels < 0 || *totalPixels > modelMdlMaxSkinPixels)
	{
		return malformed(error);
	}
	const qint64 count = frames.size() * perImage;
	if (count > modelMdlMaxSkinPixels - *totalPixels)
	{
		return malformed(error);
	}
	ModelEmbeddedSkin result;
	result.name = object.value(QStringLiteral("name")).toString();
	if (!readTimes(object.value(QStringLiteral("intervals")), modelMdlMaxSkinMembers, &result.intervals) ||
		(result.intervals.isEmpty() ? frames.size() != 1 : result.intervals.size() != frames.size()))
	{
		return malformed(error);
	}
	for (const auto &entry : frames)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.indexedFrames.size(), frames.size(), error))
		{
			return false;
		}
		QByteArray pixels;
		if (!bytes(entry, perImage, &pixels))
		{
			return malformed(error);
		}
		result.indexedFrames << std::move(pixels);
	}
	result.image = modelMdlSkinImage(result.indexedFrames.first(), settings.skinSize, settings.palette, control);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, frames.size(), frames.size(), error))
	{
		return false;
	}
	if (result.image.isNull())
	{
		return malformed(error);
	}
	result.groupFrameCount = result.indexedFrames.size();
	*skin = std::move(result);
	*totalPixels += count;
	return true;
}
} // namespace vibestudio
