#include "core/model_delivery.h"

#include "core/model_targets.h"
#include "core/release_plan.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonValue>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
} // namespace

QString modelDeliveryManifestKey() { return QStringLiteral("modelDelivery"); }

QJsonObject modelDeliveryPreferencesJson(const ModelDeliveryPreferences &preferences)
{
	QJsonObject targets;
	for (auto it = preferences.targets.cbegin(); it != preferences.targets.cend(); ++it)
		targets.insert(it.key(), it.value());
	QJsonObject result;
	if (!targets.isEmpty())
		result.insert(QStringLiteral("targets"), targets);
	if (preferences.strictSet)
		result.insert(QStringLiteral("strict"), preferences.strict);
	return result;
}

bool parseModelDeliveryPreferences(const QJsonValue &value, ModelDeliveryPreferences *preferences, QString *error)
{
	if (!value.isObject())
		return fail(error, QCoreApplication::translate("VibeStudioModelDelivery", "The saved delivery targets are not an object."));
	const auto object = value.toObject();
	ModelDeliveryPreferences result;
	for (auto it = object.begin(); it != object.end(); ++it)
	{
		if (it.key() != QStringLiteral("targets") && it.key() != QStringLiteral("strict"))
			return fail(error, QCoreApplication::translate("VibeStudioModelDelivery", "The saved delivery targets have an unknown field \"%1\".").arg(it.key()));
	}
	const auto targets = object.value(QStringLiteral("targets"));
	if (!targets.isUndefined() && !targets.isObject())
		return fail(error, QCoreApplication::translate("VibeStudioModelDelivery", "The saved delivery targets must map formats to target ids."));
	const auto map = targets.toObject();
	for (auto it = map.begin(); it != map.end(); ++it)
	{
		const auto format = it.key().toLower();
		const auto id = it.value().toString().trimmed().toLower();
		if (!it.value().isString() || modelTargetProfiles(format).isEmpty())
			return fail(error, QCoreApplication::translate("VibeStudioModelDelivery", "%1 has no delivery targets.").arg(it.key()));
		if (!findModelTargetProfile(format, id, nullptr) || id.isEmpty())
			return fail(error, QCoreApplication::translate("VibeStudioModelDelivery", "There is no %1 delivery target called \"%2\". Choose one of: %3.")
								   .arg(format, it.value().toString(), modelTargetChoices(format)));
		result.targets.insert(format, id);
	}
	const auto strict = object.value(QStringLiteral("strict"));
	if (!strict.isUndefined())
	{
		if (!strict.isBool())
			return fail(error, QCoreApplication::translate("VibeStudioModelDelivery", "The saved strict-export setting must be true or false."));
		result.strictSet = true;
		result.strict = strict.toBool();
	}
	if (preferences)
		*preferences = std::move(result);
	return true;
}

ModelDeliveryPreferences projectModelDeliveryPreferences(const ProjectManifest &manifest, QStringList *warnings)
{
	const auto value = manifest.extraFields.value(modelDeliveryManifestKey());
	if (value.isUndefined())
		return {};
	ModelDeliveryPreferences result;
	QString error;
	if (parseModelDeliveryPreferences(value, &result, &error))
		return result;
	// Keep what still parses: a target this studio no longer has drops alone.
	if (warnings)
		*warnings << QCoreApplication::translate("VibeStudioModelDelivery", "The project's saved delivery targets were partly ignored: %1").arg(error);
	result = {};
	const auto object = value.toObject();
	const auto targets = object.value(QStringLiteral("targets")).toObject();
	for (auto it = targets.begin(); it != targets.end(); ++it)
	{
		const auto format = it.key().toLower(), id = it.value().toString().trimmed().toLower();
		if (!id.isEmpty() && findModelTargetProfile(format, id, nullptr))
			result.targets.insert(format, id);
	}
	if (object.value(QStringLiteral("strict")).isBool())
	{
		result.strictSet = true;
		result.strict = object.value(QStringLiteral("strict")).toBool();
	}
	return result;
}

void setProjectModelDeliveryPreferences(ProjectManifest *manifest, const ModelDeliveryPreferences &preferences)
{
	if (!manifest)
		return;
	if (preferences.isEmpty())
		manifest->extraFields.remove(modelDeliveryManifestKey());
	else
		manifest->extraFields.insert(modelDeliveryManifestKey(), modelDeliveryPreferencesJson(preferences));
}

ModelDeliveryPreferences suggestedModelDeliveryPreferences(const GameInstallationProfile &installation)
{
	// The executable's name without its extension or a 64-bit suffix.
	auto name = QFileInfo(installation.executablePath).fileName().toLower();
	if (name.endsWith(QStringLiteral(".exe")))
		name.chop(4);
	for (const auto *suffix : {"-x64", "_x64", ".x64", ".x86_64", "-x86_64", "_x86_64", ".x86", "64"})
	{
		const auto text = QLatin1String(suffix);
		if (name.endsWith(text) && name.size() > text.size())
		{
			name.chop(text.size());
			break;
		}
	}
	ModelDeliveryPreferences result;
	const auto set = [&result](const char *format, const char *target) { result.targets.insert(QLatin1String(format), QLatin1String(target)); };
	if (name.startsWith(QStringLiteral("quakespasm")))
		set("mdl", "quakespasm");
	else if (name.startsWith(QStringLiteral("fteqw")) || name.startsWith(QStringLiteral("fteqtv")) || name == QStringLiteral("fte"))
	{
		set("mdl", "fte");
		set("iqm", "fte");
	}
	else if (name.startsWith(QStringLiteral("darkplaces")))
		set("mdl", "darkplaces");
	else if (name.startsWith(QStringLiteral("q2pro")))
	{
		set("md2", "q2pro");
		set("md3", "q2pro");
		set("md5mesh", "q2pro");
		set("md5anim", "q2pro");
	}
	else if (name.startsWith(QStringLiteral("ioquake3")) || name.startsWith(QStringLiteral("quake3e")))
	{
		set("md3", "ioquake3");
		set("iqm", "ioquake3");
	}
	else if (name == QStringLiteral("wolfsp"))
		set("md3", "rtcw");
	else if (name == QStringLiteral("wolfmp") || name == QStringLiteral("wolf"))
		set("md3", "rtcw-mp");
	else if (name == QStringLiteral("et"))
		set("md3", "et");
	else if (name.startsWith(QStringLiteral("gzdoom")))
	{
		set("md3", "gzdoom");
		set("iqm", "gzdoom");
	}
	return result;
}

ModelDeliveryPreferences installationModelDeliveryPreferences(const ProjectManifest &manifest, const QVector<GameInstallationProfile> &installations,
															  const QString &selectedInstallationId, QString *installationName)
{
	if (installationName)
		installationName->clear();
	GameInstallationProfile installation;
	if (!releaseInstallationFor(manifest, installations, selectedInstallationId, &installation))
		return {};
	if (installationName)
		*installationName = installation.displayName;
	return suggestedModelDeliveryPreferences(installation);
}

ModelDeliveryChoice resolveModelDeliveryTarget(const QString &format, const QString &explicitTarget, const ModelDeliveryPreferences &project,
											   const ModelDeliveryPreferences &installation)
{
	const auto key = format.toLower();
	if (!explicitTarget.trimmed().isEmpty())
		return {explicitTarget.trimmed().toLower(), ModelDeliveryOrigin::Explicit};
	if (project.targets.contains(key))
		return {project.targets.value(key), ModelDeliveryOrigin::Project};
	if (installation.targets.contains(key))
		return {installation.targets.value(key), ModelDeliveryOrigin::Installation};
	ModelTargetProfile profile;
	return {findModelTargetProfile(key, {}, &profile) ? profile.id : QString(), ModelDeliveryOrigin::Default};
}

QString modelDeliveryOriginId(ModelDeliveryOrigin origin)
{
	switch (origin)
	{
	case ModelDeliveryOrigin::Explicit:
		return QStringLiteral("explicit");
	case ModelDeliveryOrigin::Project:
		return QStringLiteral("project");
	case ModelDeliveryOrigin::Installation:
		return QStringLiteral("installation");
	case ModelDeliveryOrigin::Default:
		return QStringLiteral("default");
	}
	return {};
}

QString modelDeliveryOriginName(ModelDeliveryOrigin origin)
{
	switch (origin)
	{
	case ModelDeliveryOrigin::Explicit:
		return QCoreApplication::translate("VibeStudioModelDelivery", "chosen for this export");
	case ModelDeliveryOrigin::Project:
		return QCoreApplication::translate("VibeStudioModelDelivery", "saved with the project");
	case ModelDeliveryOrigin::Installation:
		return QCoreApplication::translate("VibeStudioModelDelivery", "suggested by the game installation");
	case ModelDeliveryOrigin::Default:
		return QCoreApplication::translate("VibeStudioModelDelivery", "the format's default");
	}
	return {};
}

QString findModelProjectRoot(const QString &path)
{
	if (path.trimmed().isEmpty())
		return {};
	QFileInfo info(path);
	QDir dir = info.isDir() ? QDir(info.absoluteFilePath()) : info.absoluteDir();
	for (int depth = 0; depth < 64; ++depth)
	{
		if (QFileInfo::exists(projectManifestPath(dir.absolutePath())))
			return normalizedProjectRootPath(dir.absolutePath());
		if (!dir.cdUp())
			break;
	}
	return {};
}
} // namespace vibestudio
