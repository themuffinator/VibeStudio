#pragma once
#include "core/level_hollow.h"
#include <QJsonObject>
#include <QStringList>

namespace vibestudio::cli {
// map hollow's wall options, read after the map is loaded and --object
// selected: `caulk` (--caulk) uses the game's caulk material, `caulkTexture`
// (--caulk-texture NAME, when `caulkTextureGiven`) names one and implies
// --caulk, and each of `openFaceSelectors` (--open-face face:brushId:faceNumber,
// one-based, as map select-textured prints it) is a face of a brush --object
// reaches, which is left open.
bool levelHollowOptionsFromArgs(const LevelMapDocument& document, bool caulk, bool caulkTextureGiven, const QString& caulkTexture,
	const QStringList& openFaceSelectors, LevelMapHollowOptions* options, QString* error);
// The CSG Tool's face policies on the command line, for map hollow,
// shrink-brushes and expand-brushes, read after --object is selected: each
// of `excludeSelectors` (--exclude-face face:brushId:faceNumber, one-based)
// is left alone; with `onlySelectors` (--only-face) every other face of the
// brushes --object reaches is; and `facing` (--exclude-facing x|y|z or
// dx,dy,dz, when `facingGiven`) leaves out each brush's faces most nearly
// facing that way at either end. Fills `excluded` with all of them.
bool levelCsgExcludedFacesFromArgs(const LevelMapDocument& document, const QStringList& excludeSelectors, const QStringList& onlySelectors,
	bool facingGiven, const QString& facing, QVector<LevelSurfaceFace>* excluded, QString* error);
// Adds caulkTexture, caulkedFaces, wallCount, openFaces (face selectors),
// keepInner and innerKept to a map hollow result.
void addLevelHollowDetails(const LevelMapHollowOptions& options, const LevelMapHollowReport& report, QJsonObject* details);
// The text lines that follow "Hollowed ..." for these options.
QStringList levelHollowDetailLines(const LevelMapHollowOptions& options, const LevelMapHollowReport& report);

struct LevelRoomOptionsCliResult {
	int exitCode = 0;
	QString error;
	QJsonObject payload;
	QStringList lines;
};
// editor make-room: shows the options Make Room uses in the Levels editor
// (Make Room Options...), or changes them with repeated --set caulk=on|off,
// caulkTexture=NAME (nothing after = for the game's own caulk) and
// openPickedFaces=on|off, or --reset; --dry-run previews a change.
LevelRoomOptionsCliResult runLevelRoomOptions(const QStringList& arguments);

// map shrink-brushes and map expand-brushes (`shrink` picks which): the CSG
// Tool's Shrink and Expand. Moves every face of the --object brushes, except
// those --exclude-face, --only-face and --exclude-facing leave alone,
// --thickness units in or out, as one undo step (core/level_hollow.h);
// --context opens a group first. Requires --output; --dry-run and
// --overwrite as for other map commands.
LevelRoomOptionsCliResult runLevelBrushOffset(const QStringList& arguments, bool shrink);
} // namespace vibestudio::cli
