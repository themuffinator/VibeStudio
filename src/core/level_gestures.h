#pragma once

#include "core/level_editor_controls.h"
#include <QJsonObject>

namespace vibestudio {
struct LevelGestureOption { QString id; QString label; int value = 0; };
enum class LevelGestureFieldKind { Choice, MotionKey, ToggleKey, HoldKey };
struct LevelGestureField {
	QString id; QString label; QString section; QVector<LevelGestureOption> options;
	LevelGestureFieldKind kind = LevelGestureFieldKind::Choice;
};

// Stable, portable choices shared by the GUI, CLI and versioned gesture files.
// Layout, grid and command shortcuts keep their own stores.
QString levelGestureFieldKindId(LevelGestureFieldKind kind);
QVector<LevelGestureField> levelGestureFields();
bool levelGestureFieldAvailable(const QString& id, const LevelEditorControls& controls);
QJsonObject levelGestureValues(const LevelEditorControls& controls);
bool applyLevelGestureOverrides(const LevelEditorControls& base, const QJsonObject& overrides,
	LevelEditorControls* result, QJsonObject* normalized = nullptr, QString* error = nullptr);
QByteArray serializeLevelGestures(const QString& profile, const QJsonObject& overrides);
bool parseLevelGestures(const QByteArray& bytes, QString* profile, QJsonObject* overrides, QString* error = nullptr);
bool readLevelGestures(const QString& path, QString* profile, QJsonObject* overrides, QString* error = nullptr);
bool writeLevelGestures(const QString& path, const QString& profile, const QJsonObject& overrides,
	bool overwrite, QString* error = nullptr, const QString& settingsPath = {});
} // namespace vibestudio
