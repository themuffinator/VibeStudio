#pragma once

#include "core/box_resize.h"
#include "core/level_map.h"
#include <QHash>
#include <QSet>

namespace vibestudio
{

// A value-only snapshot: placement workers never query a widget or the cursor.
enum class LevelPasteMode { Offset, Camera, Plane, ViewRay };
struct LevelPastePlacement {
	LevelPasteMode mode = LevelPasteMode::Offset;
	LevelMapVec3 offset{0, 0, 0, true};
	// Camera centre, or a point on the target plane (the cursor intersection).
	LevelMapVec3 point;
	LevelMapVec3 normal;
	LevelMapVec3 direction;
	LevelMapVec3 rayOrigin;
	// Perspective misses use a plane 256 units along the ray, facing the view.
	LevelMapVec3 viewDirection;
	// Orthographic rays span both sides of their focus plane. A miss stays on
	// that plane rather than applying a perspective camera distance.
	bool rayForwardOnly = true;
	double grid = 0;
	// Plan-view paste uses the selected assembly's far depth, with point's
	// existing hidden coordinate as the remembered-selection fallback.
	int referenceAxis = -1;
	QHash<QString, ResizeBox> entityBounds;
	// Optional snapshot of the camera's visible brushes, including its filters.
	bool filterBrushes = false;
	QSet<int> visibleBrushes;
};

// Brush entities use their primitives, not their optional origin key. Point
// entities use definition bounds, falling back to an origin-relative 16-unit box.
bool levelPasteBounds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects,
                      const QHash<QString, ResizeBox>& entityBounds, LevelMapVec3* mins, LevelMapVec3* maxs, QString* error = nullptr);

// Resolves against the destination before insertion, so the new geometry cannot
// intercept its own placement ray. Atomic output; finite bounded world values.
bool levelPasteOffset(const LevelMapDocument& destination, const LevelMapDocument& clipboard, const QVector<LevelMapSelectionRef>& objects,
                      const LevelPastePlacement& placement, LevelMapVec3* offset, QString* error = nullptr);

bool pasteLevelMapTextAt(LevelMapDocument* document, const QString& text, const LevelPastePlacement& placement,
                         const LevelMapTextureLockOptions& textures, QString* error = nullptr, LevelMapVec3* appliedOffset = nullptr);

// Move to Camera: Paste to Camera's centre rule for the live selection. The
// centre of the selection's bounds (levelPasteBounds(), with point entities
// sized by placement.entityBounds) goes to placement.point, by a translation
// snapped to placement.grid (0 for none), as one moveLevelMapSelection() step:
// texture lock, scene locks, open groups and Repeat behave as for any move.
// The objects keep their identities, order and source lines. placement.mode
// is ignored. Fails, changing nothing, when the translation is zero.
//
// Workflow reference: NetRadiant Custom radiant/select.cpp MoveToCamera and
// TranslateToCamera at 449778b3 (GPL-2.0-or-later). Original implementation;
// see docs/CREDITS.md.
bool levelMoveToCameraOffset(const LevelMapDocument& document, const LevelPastePlacement& placement, LevelMapVec3* offset,
                             QString* error = nullptr);
bool moveLevelMapSelectionToCamera(LevelMapDocument* document, const LevelPastePlacement& placement,
                                   const LevelMapTextureLockOptions& textures, LevelMapVec3* appliedOffset = nullptr,
                                   QString* error = nullptr);

} // namespace vibestudio
