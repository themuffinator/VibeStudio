#pragma once

#include "core/model_pose.h"

#include <QJsonObject>

#include <array>

namespace vibestudio
{
inline constexpr int modelQ3AnimationCount = 31;
inline constexpr int modelQ3AnimationMaxBytes = 19998;
struct ModelQ3AnimationClip
{
	// Native animation.cfg coordinates. Leg ranges are adjusted by the shared
	// TORSO_GESTURE / LEGS_WALKCR offset before indexing lower.md3.
	int firstFrame = 0, frameCount = 1, loopFrames = 1;
	float framesPerSecond = 10;
	bool reversed = false;
};
struct ModelQ3AnimationConfig
{
	std::array<ModelQ3AnimationClip, modelQ3AnimationCount> clips{};
	QString footsteps = QStringLiteral("normal");
	QString sex = QStringLiteral("m");
	ModelVec3 headOffset;
	bool fixedLegs = false, fixedTorso = false;
};
// Stable native identifiers, deliberately not translated.
QString modelQ3AnimationName(int slot);
int modelQ3AnimationSlot(const QString &name);
bool modelQ3AnimationUsesLower(int slot);
bool modelQ3AnimationUsesUpper(int slot);
int modelQ3AnimationFirstFrame(const ModelQ3AnimationConfig &config, int slot);
int modelQ3AnimationPeriod(const ModelQ3AnimationClip &clip);
bool validateModelQ3Animation(const ModelQ3AnimationConfig &config, QString *error = nullptr, int lowerFrames = -1, int upperFrames = -1);
// Strict, bounded import. Unsupported directives and extra rows fail atomically;
// legacy missing gesture rows and zero FPS are normalized with explicit notes.
bool parseModelQ3Animation(const QByteArray &bytes, ModelQ3AnimationConfig *config, QString *error = nullptr, QStringList *notes = nullptr);
QByteArray exportModelQ3Animation(const ModelQ3AnimationConfig &config, QString *error = nullptr);
// Structured diagnostics, including effective local frame ranges and periods.
QJsonObject modelQ3AnimationJson(const ModelQ3AnimationConfig &config);
// Steady clip phase measured from its first pose. Includes reversed ranges, a
// once-only introduction and a loop tail; excludes game-state transition blends.
bool sampleModelQ3Animation(const ModelQ3AnimationConfig &config, int slot, double seconds, bool interpolate, ModelAnimationSample *sample);
} // namespace vibestudio
