#pragma once
#include "core/level_map.h"
#include "core/model_transform.h"
#include <Qt>

namespace vibestudio {
// World-axis component movement, after the behaviour of TrenchBroom v2026.2
// MoveHandleDragTracker.h and HandleDragTracker.cpp (GPL-3.0-or-later).
// Original implementation; references and licence review: docs/CREDITS.md.
// Geometry only: no input injection, document mutation or GUI dependency.
struct LevelComponentDragOptions {
	bool perspective = false;
	bool absoluteAllowed = true;
	int fixedAxis = -1; // -1: profile plane; 0/1/2: explicit X/Y/Z handle.
	double grid = 0;
	ModelVec3 viewDirection;
};
class LevelComponentDrag {
public:
	bool begin(LevelMapVec3 origin, ModelPickRay ray, const LevelComponentDragOptions& options, Qt::KeyboardModifiers modifiers);
	bool update(ModelPickRay ray, Qt::KeyboardModifiers modifiers, LevelMapVec3* delta);
	// Only a checked preview advances the anchor used for a new constraint.
	void accept(const LevelMapVec3& delta);
	[[nodiscard]] int axis() const { return m_axis; }
	[[nodiscard]] bool absolute() const { return m_absolute; }
	[[nodiscard]] bool vertical() const { return m_vertical; }
	[[nodiscard]] LevelMapVec3 origin() const { return m_origin; }
	[[nodiscard]] LevelMapVec3 traceOrigin() const { return m_segmentOrigin; }
	[[nodiscard]] QString description() const;
private:
	bool pick(ModelPickRay ray, LevelMapVec3* position) const;
	LevelComponentDragOptions m_options;
	LevelMapVec3 m_origin, m_segmentOrigin, m_accepted, m_proposed, m_anchor, m_offset;
	Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;
	int m_planeAxis = 2, m_axis = -1;
	bool m_started = false, m_vertical = false, m_absolute = false;
};
// Native click and drag share chords. Only the vertex tool admits Ctrl/Shift
// on initial drags; edge/face tools keep their Ctrl selection click.
bool levelComponentDragChord(int componentMode, Qt::KeyboardModifiers modifiers);
// Qt modifier-event flags differ on press/release; explicitly include/exclude
// the changed key without querying or taking over the user's input devices.
Qt::KeyboardModifiers levelComponentModifierState(int key, Qt::KeyboardModifiers modifiers, bool pressed);
bool levelComponentModifierKey(int key);
} // namespace vibestudio
