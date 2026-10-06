#include "core/studio_settings.h"
#include "core/editor_profiles.h"
#include "core/level_gestures.h"
#include <QCoreApplication>

namespace vibestudio {
namespace {
QString storageKey(const QString& profile) { return QStringLiteral("editor/gestureOverrides/%1").arg(profile); }
bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }
}

QJsonObject StudioSettings::editorGestureOverrides(const QString& profile, QString* error) const
{
	if (error) { error->clear(); }
	EditorProfileDescriptor base;
	if (!editorProfileForId(profile, &base)) {
		fail(error, QCoreApplication::translate("LevelGestures", "Unknown editor profile: %1.").arg(profile)); return {};
	}
	const auto bytes = m_settings->value(storageKey(base.id)).toByteArray();
	if (bytes.isEmpty()) { return {}; }
	QString savedProfile; QJsonObject overrides;
	if (!parseLevelGestures(bytes, &savedProfile, &overrides, error)) { return {}; }
	if (savedProfile != base.id) {
		fail(error, QCoreApplication::translate("LevelGestures", "The saved gestures belong to another profile. Profile defaults are in use.")); return {};
	}
	return overrides;
}

LevelEditorControls StudioSettings::effectiveLevelEditorControls(const QString& profile, QString* error) const
{
	auto controls = levelEditorControlsForProfile(profile);
	QString readError;
	const auto overrides = editorGestureOverrides(profile, &readError);
	if (!readError.isEmpty()) { if (error) { *error = readError; } return controls; }
	applyLevelGestureOverrides(controls, overrides, &controls, nullptr, error);
	return controls;
}

bool StudioSettings::setEditorGestureOverrides(const QString& profile, const QJsonObject& overrides, QString* error)
{
	if (error) { error->clear(); }
	if (isReadOnly()) { ++m_discardedWrites; return fail(error, QCoreApplication::translate("LevelGestures", "The settings store is read-only.")); }
	EditorProfileDescriptor base;
	if (!editorProfileForId(profile, &base)) { return fail(error, QCoreApplication::translate("LevelGestures", "Unknown editor profile: %1.").arg(profile)); }
	QJsonObject cleaned;
	if (!applyLevelGestureOverrides(base.controls, overrides, nullptr, &cleaned, error)) { return false; }
	const auto key = storageKey(base.id);
	const auto previous = m_settings->value(key);
	if (cleaned.isEmpty()) { removeKey(key); }
	else { writeValue(key, serializeLevelGestures(base.id, cleaned)); }
	sync();
	if (status() != QSettings::NoError) {
		// Keep the live settings cache on the previously applied value as well.
		if (previous.isValid()) { writeValue(key, previous); } else { removeKey(key); }
		return fail(error, QCoreApplication::translate("LevelGestures", "Unable to save gesture preferences."));
	}
	return true;
}
} // namespace vibestudio
