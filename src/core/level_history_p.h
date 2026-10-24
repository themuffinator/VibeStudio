#pragma once

#include "core/level_map.h"

namespace vibestudio
{
struct LevelMapDocument;

namespace detail
{
// The caller has applied a validated, session-only selection/context change.
// Records it without scene propagation or invalidating geometry caches.
void recordLevelMapSelectionHistory(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& before,
	const LevelSceneEditingContext& contextBefore, const QString& description);
void recordLevelMapVisibilityHistory(LevelMapDocument* document, const LevelMapDocument& before, const QString& description);

// Keep the intermediate undo commands of a compound operation until its
// caller folds them, or rolls them back. Restore the user's limit on every
// exit and only then evict old history. Nested scopes are supported.
class LevelMapHistoryScope
{
  public:
	explicit LevelMapHistoryScope(LevelMapDocument& document);
	~LevelMapHistoryScope();
	LevelMapHistoryScope(const LevelMapHistoryScope&) = delete;
	LevelMapHistoryScope& operator=(const LevelMapHistoryScope&) = delete;

  private:
	LevelMapDocument& m_document;
	int m_limit;
};
} // namespace detail
} // namespace vibestudio
