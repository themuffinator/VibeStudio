#pragma once
#include "core/level_material_paint.h"
#include "core/level_surface.h"
#include <QSet>

// Reveal Material and Find and Replace Textures: the selection's shared
// material, and finding or replacing one material within a scope. Workflow
// references: TrenchBroom MapWindow::revealMaterial (v2026.2, 4c756be1,
// GPL-3.0-or-later) and NetRadiant Custom FindReplaceTextures (449778b3,
// GPL-2.0-or-later). Original implementation; see docs/CREDITS.md.
namespace vibestudio {

// The one material every surface in scope uses, for Reveal Material.
struct LevelSelectionMaterial {
	enum class State { None, Single, Mixed };
	State state = State::None;
	// Single: the shared material as the first surface spells it. Mixed: the
	// first material found, and `other`, a second, different one.
	QString material;
	QString other;
	// Doom: whether the material is used as a flat, on walls, or both.
	bool flat = false;
	bool wall = false;
	// Surfaces read; Mixed stops at the first surface with a second material.
	int surfaces = 0;
	// The picked faces and patches were the scope.
	bool fromPicks = false;
};
// Picked faces and picked whole patches are the scope when there are any, as
// TrenchBroom's face selection is; otherwise the selected brushes and patches,
// those of selected entities, selected linedefs' sides and selected sectors.
// Names compare trimmed and without case; empty names and Doom's "-" are no
// material. Picks that no longer exist are ignored.
LevelSelectionMaterial levelSelectionMaterial(const LevelMapDocument& document,
	const QVector<LevelSurfaceFace>& pickedFaces = {}, const QVector<int>& pickedPatches = {});

// Where Find and Replace Textures looks: the whole map, the selected objects
// (as levelSelectionMaterial reads them), or the picked faces and patches.
enum class LevelMaterialFindScope { Map, Selection, Picks };

struct LevelMaterialReplaceRequest {
	QString find;
	// Empty previews Find only; Replace requires a name.
	QString replace;
	LevelMaterialFindScope scope = LevelMaterialFindScope::Map;
	// The Picks scope: independent or camera-picked faces and whole patches.
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	// Objects the view hides (selectors such as brush:3), left unchanged with
	// the map's own hidden and locked objects and anything outside the open
	// group. Members of closed groups inside the open context are editable.
	QSet<QString> excludedObjects;
};

struct LevelMaterialReplaceReport {
	// Editable surfaces in scope that use the material, in map order.
	QVector<LevelMaterialTarget> targets;
	// Of those, how many Replace changes (a name already equal to the
	// replacement stays) and how many sit in closed groups, which Find skips
	// until the group is opened.
	int changes = 0;
	int inClosedGroups = 0;
	// Uses in scope on hidden, locked or out-of-context objects, left alone.
	int skipped = 0;
	// Set by replaceLevelMapMaterial.
	int replaced = 0;
};

// Counts what Find or Replace would touch, without changing the document.
// Refuses an empty name, an empty Selection or Picks scope, picks that no
// longer exist, picks on a Doom map and more than 16,384 picks.
bool previewLevelMaterialReplace(const LevelMapDocument& document, const LevelMaterialReplaceRequest& request,
	LevelMaterialReplaceReport* report, QString* error = nullptr);
// Replaces `find` with `replace` on every editable surface in scope as one
// undo command, through replaceLevelMapTextureUses. Hidden, locked and
// out-of-context uses are skipped and counted; nothing changes when no
// editable surface uses `find`. A failure leaves document and report alone.
bool replaceLevelMapMaterial(LevelMapDocument* document, const LevelMaterialReplaceRequest& request,
	LevelMaterialReplaceReport* report = nullptr, QString* error = nullptr);

} // namespace vibestudio
