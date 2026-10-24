#pragma once

#include "core/level_repeat_types.h"
#include <QString>

namespace vibestudio
{
struct LevelMapDocument;
struct LevelMapTextureLockOptions;
namespace detail
{
// Nested/internal transforms and selection updates belong to their outer edit.
// Scope state is thread-local, so copying/publishing a document cannot leave a
// suppression counter armed on the result.
class LevelRepeatCaptureScope
{
  public:
	explicit LevelRepeatCaptureScope(LevelMapDocument* document, bool capture = true);
	~LevelRepeatCaptureScope();
	LevelRepeatCaptureScope(const LevelRepeatCaptureScope&) = delete;
	LevelRepeatCaptureScope& operator=(const LevelRepeatCaptureScope&) = delete;
	void commit(const LevelRepeatAction& action, bool selectionChanged = false);
	// Component transforms: updates NetRadiant Custom's repeatable transforms
	// only; Repeat Last Commands replays objects, never components.
	void commitTransform(const LevelRepeatAction& action);

  private:
	LevelMapDocument* m_document;
	bool m_capture;
};
// The repeatable transforms once `action` is recorded: a move replaces the
// translation, a turn the rotation, a diagonal (resize, mirror) the scale and
// a single off-diagonal term the skew. Duplicates and other shapes change nothing.
[[nodiscard]] LevelRepeatTransforms levelRepeatTransformsAfter(const LevelRepeatTransforms& current, const LevelRepeatAction& action);
void levelRepeatSelectionChanged(LevelMapDocument* document);
void levelRepeatHistoryNavigated(LevelMapDocument* document);
bool applyLevelRepeatTransform(LevelMapDocument* document, const LevelRepeatAction& action, const LevelMapTextureLockOptions& textures,
                               QString* error);
} // namespace detail
} // namespace vibestudio
