// Modeller preferences: the controls profile, per-profile overrides and the
// sidebar state per profile family.
#include "core/studio_settings.h"

#include "core/model_editor_controls.h"

#include <QCoreApplication>
#include <QJsonDocument>

namespace vibestudio {

namespace {

QString profileKey()
{
	return QStringLiteral("modeller/profileId");
}

QString overridesKey(const QString& profile)
{
	return QStringLiteral("modeller/controlOverrides/%1").arg(profile);
}

QString sidebarKey(const QString& family)
{
	return QStringLiteral("modeller/sidebar/%1").arg(family);
}

QJsonObject readObject(const QVariant& value)
{
	const QByteArray bytes = value.toByteArray();
	if (bytes.isEmpty()) {
		return {};
	}
	const QJsonDocument document = QJsonDocument::fromJson(bytes);
	return document.isObject() ? document.object() : QJsonObject{};
}

} // namespace

QString StudioSettings::modelEditorProfileId() const
{
	const QString stored = normalizedModelEditorProfileId(m_settings->value(profileKey()).toString());
	if (!stored.isEmpty()) {
		return stored;
	}
	return modelEditorProfileForLevelProfile(selectedEditorProfileId());
}

void StudioSettings::setModelEditorProfileId(const QString& id)
{
	const QString normalized = normalizedModelEditorProfileId(id);
	if (normalized.isEmpty()) {
		return;
	}
	writeValue(profileKey(), normalized);
}

QJsonObject StudioSettings::modelEditorControlOverrides(const QString& profile) const
{
	const QString normalized = normalizedModelEditorProfileId(profile);
	if (normalized.isEmpty()) {
		return {};
	}
	return readObject(m_settings->value(overridesKey(normalized)));
}

bool StudioSettings::setModelEditorControlOverrides(const QString& profile, const QJsonObject& overrides, QString* error)
{
	if (error) { error->clear(); }
	const QString normalized = normalizedModelEditorProfileId(profile);
	if (normalized.isEmpty()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioModelControls", "Unknown modeller profile: %1.").arg(profile); }
		return false;
	}
	if (isReadOnly()) {
		++m_discardedWrites;
		if (error) { *error = QCoreApplication::translate("VibeStudioModelControls", "The settings store is read-only."); }
		return false;
	}
	// Store only what still differs from the profile once applied, so a
	// profile update reaches every setting the user left alone.
	const ModelEditorControls base = modelEditorControlsForProfile(normalized);
	ModelEditorControls edited;
	applyModelEditorControlOverrides(base, overrides, &edited);
	const QJsonObject cleaned = vibestudio::modelEditorControlOverrides(base, edited);
	const QString key = overridesKey(normalized);
	if (cleaned.isEmpty()) {
		removeKey(key);
	} else {
		writeValue(key, QJsonDocument(cleaned).toJson(QJsonDocument::Compact));
	}
	sync();
	if (status() != QSettings::NoError) {
		if (error) { *error = QCoreApplication::translate("VibeStudioModelControls", "Unable to save the modeller controls."); }
		return false;
	}
	return true;
}

ModelEditorControls StudioSettings::effectiveModelEditorControls(const QString& profile, QStringList* warnings) const
{
	const ModelEditorControls base = modelEditorControlsForProfile(profile);
	ModelEditorControls result = base;
	applyModelEditorControlOverrides(base, modelEditorControlOverrides(profile), &result, warnings);
	return result;
}

QJsonObject StudioSettings::modelSidebarState(const QString& family) const
{
	return readObject(m_settings->value(sidebarKey(family)));
}

void StudioSettings::setModelSidebarState(const QString& family, const QJsonObject& state)
{
	if (state.isEmpty()) {
		removeKey(sidebarKey(family));
		return;
	}
	writeValue(sidebarKey(family), QJsonDocument(state).toJson(QJsonDocument::Compact));
}

} // namespace vibestudio
