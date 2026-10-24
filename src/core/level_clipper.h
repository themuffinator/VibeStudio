#pragma once

#include <QJsonObject>
#include <QString>

namespace vibestudio {
// Empty cutTexture inherits the brush's majority material and flags. An explicit
// material preserves brush contents but starts with zero surface flags/value.
// Original faces and their texture coordinates are never rewritten by clipping.
struct LevelMapClipOptions {
	QString cutTexture;
	bool wrapMerge = false;
	QString wrapTexture; // Current material for new hull faces, independent of cutTexture.
	bool operator==(const LevelMapClipOptions&) const = default;
};

enum class LevelClipMaterial { Inherit, Current, Custom };
enum class LevelClipDoubleClick { None, Clip, Split };
// Behaviour reference: NetRadiant Custom radiant/clippertool.cpp and
// selection_mtor_clip.cpp, 449778b3840f8f2062daab15c87cb012f2ac945c,
// GPL-2.0-or-later. Independently implemented; see docs/CREDITS.md.
struct LevelClipperPreferences {
	LevelClipMaterial material = LevelClipMaterial::Inherit;
	QString customTexture = QStringLiteral("common/caulk");
	bool resetPoints = true;
	bool resetSide = false;
	bool twoPointsInPlan = false;
	bool wrapMerge = false;
	LevelClipDoubleClick doubleClick = LevelClipDoubleClick::None;
	bool operator==(const LevelClipperPreferences&) const = default;
};

bool validLevelClipTexture(const QString& texture);
QJsonObject levelClipperPreferencesJson(const LevelClipperPreferences& preferences);
// Applies a strictly typed partial update atomically. Unknown fields fail.
bool updateLevelClipperPreferences(const QJsonObject& values, LevelClipperPreferences* preferences, QString* error = nullptr);
} // namespace vibestudio
