#pragma once

namespace vibestudio::detail
{
bool levelLinkedUpdatesDeferred();
// Defer linked propagation while a shared service prepares one compound edit.
// The caller folds its commands, leaves this scope, then synchronises once.
class LevelLinkedUpdateScope
{
  public:
	LevelLinkedUpdateScope();
	~LevelLinkedUpdateScope();
	LevelLinkedUpdateScope(const LevelLinkedUpdateScope&) = delete;
	LevelLinkedUpdateScope& operator=(const LevelLinkedUpdateScope&) = delete;
};
} // namespace vibestudio::detail
