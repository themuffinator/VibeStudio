#pragma once

// Wall options for Hollow, Hollow Touching, Make Room and the CSG Tool, the
// game caulk material they use, the CSG Tool's face policies and its Shrink
// and Expand. The walls themselves are built by hollowLevelMapSelection() in
// core/level_map.cpp, which these options extend.
//
// Workflow reference: NetRadiant Custom CSG_MakeRoom, CSG_Tool,
// HollowSettings, brush_extrudeDiag and CSGdlg_BrushShrink/Expand
// (radiant/csg.cpp) and GetCommonShader (radiant/filterbar.cpp) at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "core/level_map.h"
#include "core/level_surface.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// `caulkTexture`, when set, goes on every wall face except the one facing the
// room and those lying on the plane of an open face, which stay in sight
// through the opening; renaming keeps the faces' flags and alignment, as
// applying a material does. `openFaces` (brush id, zero-based face index) get
// no wall, so they are left open as doorways, and the walls beside them stop
// flush with them; faces of brushes the edit does not reach are ignored.
//
// `keepInner` keeps the brush the walls grow from, as NetRadiant Custom's CSG
// Tool does with Remove inner brush off: Make Room, Diagonal and Extrude walls
// go round the brush as it is, and Overlap and Touch walls round what is left
// of it inside them, which keeps the brush's number. The room is then solid,
// so with caulk the hidden faces are those between the walls and the inner
// brush, and between walls: the walls' inner faces and the inner brush's
// covered faces take caulk, and the outside keeps its materials.
struct LevelMapHollowOptions {
	QString caulkTexture;
	QVector<LevelSurfaceFace> openFaces;
	bool keepInner = false;
};

struct LevelMapHollowReport {
	// Brushes made into walls, and the walls built for them.
	int hollowed = 0;
	int walls = 0;
	// Inner brushes kept (keepInner).
	int innerKept = 0;
	// Wall faces that ended up with the caulk material.
	int caulkedFaces = 0;
	// Faces of the hollowed brushes left open.
	int openFaces = 0;
	// Why any other selected brush was left as it was.
	QStringList skipped;
};

// The material Radiant's caulk options use on this map's game: common/caulk in
// Quake III (textures/common/caulk when its brushes are brushDef3), and skip in
// Quake, whose compilers leave skip faces out as idTech3 compilers leave out
// caulk (the same rule the Caulk view filter follows). Empty for Quake II and
// Doom, which ship no caulk material: name one instead.
QString levelMapCaulkTexture(const LevelMapDocument& document);

// hollowLevelMapSelection() with wall options. Fails, changing nothing, for a
// caulk name a map cannot hold or an open face index outside its brush; a
// brush with every face open is left as it was, with its reason in `skipped`.
// Hidden brushes are left as they were too. The walls are selected. One undo
// command.
bool hollowLevelMapSelection(LevelMapDocument* document, double thickness, LevelMapHollowMode mode, const LevelMapHollowOptions& options,
	LevelMapHollowReport* report = nullptr, QString* error = nullptr);

// The CSG Tool's face policies, after NetRadiant Custom's -faces, +faces,
// -proj and -cam: which faces of each brush an edit leaves alone (no wall,
// or not moved by Shrink and Expand). The picked faces; every face but the
// picked ones; or, at either end of each brush, the faces most nearly facing
// along the active 2D view's axis or along the 3D camera's view.
enum class LevelCsgFacePolicy { ExcludePicked, OnlyPicked, ExcludeFacingView, ExcludeFacingCamera };
// exclude-picked, only-picked, exclude-view and exclude-camera.
QString levelCsgFacePolicyId(LevelCsgFacePolicy policy);
bool parseLevelCsgFacePolicy(const QString& id, LevelCsgFacePolicy* policy);

// The brushes the selection reaches: selected brushes and those of selected
// entities, in map order.
QVector<int> levelCsgReachedBrushIds(const LevelMapDocument& document);
// The faces of the brushes the selection reaches that `policy` leaves alone.
// `picked` are the picked faces (others are ignored); `axis` is the view or
// camera direction for the two facing policies (any length but zero).
QVector<LevelSurfaceFace> levelCsgExcludedFaces(const LevelMapDocument& document, LevelCsgFacePolicy policy,
	const QVector<LevelSurfaceFace>& picked, const LevelMapVec3& axis = {});
// For each brush the selection reaches, its faces whose normal lies nearest
// to `axis` and nearest to the opposite direction (within 0.001), as -proj
// and -cam pick them. Empty for a zero axis.
QVector<LevelSurfaceFace> levelMapFacesFacingAxis(const LevelMapDocument& document, const LevelMapVec3& axis);

struct LevelMapBrushOffsetReport {
	// Brushes changed, and faces moved in them.
	int changed = 0;
	int movedFaces = 0;
	// Why any other brush the selection reaches was left as it was.
	QStringList skipped;
};

// The CSG Tool's Expand (positive `offset`) and Shrink (negative): every face
// of each brush the selection reaches, except the `excluded` ones, moves
// `offset` units along its outward normal. Textures stay where they are in
// the world. A brush a shrink would flatten or rob of a face, one with every
// face excluded, and a hidden one are left as they were, with the reason in
// `skipped`; fails, changing nothing, when no brush can change. One undo step.
bool offsetLevelMapSelectionFaces(LevelMapDocument* document, double offset, const QVector<LevelSurfaceFace>& excluded,
	LevelMapBrushOffsetReport* report = nullptr, QString* error = nullptr);

// The CSG Tool's options in the Levels editor, saved between sessions:
// NetRadiant Custom's defaults are a thickness of 16, picked faces excluded,
// caulk on and the inner brush removed. Caulk uses the material Make Room
// Options names, or the game's own.
struct LevelCsgToolPreferences {
	double thickness = 16.0;
	LevelCsgFacePolicy facePolicy = LevelCsgFacePolicy::ExcludePicked;
	bool caulk = true;
	bool keepInner = false;
	bool operator==(const LevelCsgToolPreferences&) const = default;
};
// The CSG Tool's thickness limits, in map units.
constexpr double kLevelCsgMinimumThickness = 0.001;
constexpr double kLevelCsgMaximumThickness = 4096.0;
// Why `preferences` cannot be saved (a thickness out of range), or empty.
QString levelCsgToolPreferencesProblem(const LevelCsgToolPreferences& preferences);

// Make Room's options in the Levels editor. NetRadiant Custom's Make Room
// always caulks and always leaves picked faces open, so both start on.
// An empty `caulkTexture` means the game's own (levelMapCaulkTexture()).
struct LevelRoomPreferences {
	bool caulk = true;
	QString caulkTexture;
	bool openPickedFaces = true;
	bool operator==(const LevelRoomPreferences&) const = default;
};

// The caulk material these preferences give on `document`: empty when caulk
// is off, or when it is on but neither the preferences nor the game name one.
QString levelRoomCaulkTexture(const LevelMapDocument& document, const LevelRoomPreferences& preferences);

// Why `preferences` cannot be saved (a caulk name no map could hold), or
// empty when they can. Make Room Options and `editor make-room` both use it.
QString levelRoomPreferencesProblem(const LevelRoomPreferences& preferences);

// The preferences as `editor make-room` shows them: caulk, caulkTexture
// (empty for the game's own) and openPickedFaces.
QJsonObject levelRoomPreferencesJson(const LevelRoomPreferences& preferences);

// Applies `values` (any of the fields above, booleans for caulk and
// openPickedFaces) to `preferences`; fails, changing nothing, for an unknown
// field, a value of the wrong type or preferences that cannot be saved.
bool updateLevelRoomPreferences(const QJsonObject& values, LevelRoomPreferences* preferences, QString* error = nullptr);

} // namespace vibestudio
