#pragma once

#include "core/audio_delivery.h"
#include "core/level_map.h"

namespace vibestudio
{

// Quake and Quake II share a map grammar. Empty game means the user must
// explicitly choose Quake II; engineFamily alone is not sufficient evidence.
struct LevelSoundTarget {
	LevelMapFormat format = LevelMapFormat::Unknown;
	QString game;
};

struct LevelSoundRequest {
	QString game; // quake2 or quake3; empty uses the document's known target.
	QString virtualPath;
	LevelMapVec3 origin;
	QString mode = QStringLiteral("loop-on"); // loop-on, loop-off, triggered
	QString targetName{};
};

struct LevelSoundPlan {
	QString game;
	QString className;
	QString soundReference;
	QVector<LevelMapProperty> properties;
	QString error;
	[[nodiscard]] bool valid() const { return error.isEmpty() && !className.isEmpty(); }
};

LevelSoundTarget levelSoundTarget(const LevelMapDocument& document);
LevelSoundPlan planLevelSound(const LevelSoundTarget& target, const LevelSoundRequest& request);
// Bounded RIFF/marker validation skips sample bytes. Uses the conservative
// shared Quake II/III delivery contract: mono, 22050 Hz, legacy PCM16 WAV.
bool validateLevelSoundWav(const QByteArray& wav, QString* error = nullptr);
bool placeLevelSound(LevelMapDocument* document, const LevelSoundRequest& request, const QByteArray& wav,
                     int* entityId = nullptr, QString* error = nullptr);
// Both edits commit to memory together or neither changes. Each surface retains
// its normal undo history and explicit save; this never writes a map/package.
bool stageLevelSound(PackageStagingModel* staging, LevelMapDocument* document,
                     const LevelSoundRequest& request, const QByteArray& wav, bool replace,
                     int* entityId = nullptr, QString* error = nullptr);

} // namespace vibestudio
