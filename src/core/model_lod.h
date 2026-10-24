#pragma once

// Level-of-detail models for Quake III and the engines derived from it.
//
// Quake III loads `name_1.md3` and `name_2.md3` beside `name.md3` as lower
// detail versions, chosen by distance and r_lodbias: MD3_MAX_LODS is 3 in
// code/qcommon/qfiles.h, and R_RegisterModel (code/renderer/tr_model.c in the
// released Quake III Arena source; R_RegisterMD3 in ioquake3) builds
// "_%d.md3" for levels 1 and 2 only, so a third detail file is never loaded.
// Enemy Territory raises MD3_MAX_LODS to 4 (name_3.md3), and Return to Castle
// Wolfenstein multiplayer lowers it to 1. How many levels a game loads is part
// of its MD3 delivery target (ModelTargetLimits::detailLevels in
// core/model_targets.h).
//
// A level is either generated, a quadric decimation of the level above it,
// surface by surface, through the same mesh tool as the editor (so UV seams,
// borders and every animation pose are respected), or hand-made: another model
// file the artist built. An editable source records which levels it owns and
// how in ModelMesh::detailLevels, the recipe.
//
// R_AddMD3Surfaces checks the frame against md3[0] (the base model) only and
// then reads the chosen level's vertices for that frame, and custom skins bind
// surfaces by name, so every level needs the base's frames and surface names:
// validateModelLodSet checks this for hand-made and existing sets.
//
// A detail set is prepared as a whole and then published: preparation builds
// every file's bytes, checks each against the delivery target and reviews
// each destination, so a set that cannot be written completely writes nothing.

#include "core/model_file_io.h"
#include "core/model_mesh.h"
#include "core/model_targets.h"
#include "core/model_work.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio
{
struct ModelLodLevel
{
	ModelMesh mesh;
	int triangles = 0;
	// Surfaces that could not be reduced further keep their previous detail.
	QStringList notes;
	// A hand-made level's model file (absolute), else empty.
	QString source;
};
// The most detail levels any supported target loads (Enemy Territory's
// name_1.md3 to name_3.md3); a target can load fewer.
inline constexpr int modelLodMaxLevels = 3;
// Builds `levels` (1 to modelLodMaxLevels) reductions, each keeping `ratio`
// (0.05-0.95) of the previous level's triangles.
bool buildModelLods(const ModelMesh &source, int levels, double ratio, QVector<ModelLodLevel> *result, QString *error = nullptr,
					const ModelWorkControl &control = {});
// `models/prop.md3` with level 2 becomes `models/prop_2.md3`.
QString modelLodPath(const QString &basePath, int level);

// The recipe `model lod --levels N --ratio R` describes: generated levels,
// the first keeping its borders.
[[nodiscard]] QVector<ModelLodRecipeLevel> modelLodGeneratedRecipe(int levels, double ratio);
// Levels numbered 1, 2 ... in order, at most `maxLevels` of them (the
// delivery target's; modelLodMaxLevels when negative), generated levels with a
// ratio of 0.05-0.95 or a positive triangle target, hand-made levels with a
// source path.
bool validateModelLodRecipe(const QVector<ModelLodRecipeLevel> &recipe, int maxLevels = -1, QString *error = nullptr);
// The recipe as stored in an editable source: one object per level with
// `level`, `mode` ("generated" or "source"), and `ratio`, `targetTriangles`
// and `preserveBoundary`, or `source`.
[[nodiscard]] QJsonArray modelLodRecipeJson(const QVector<ModelLodRecipeLevel> &recipe);
bool parseModelLodRecipe(const QJsonValue &value, QVector<ModelLodRecipeLevel> *recipe, QString *error = nullptr);
// A hand-made level's model path: `source` itself when absolute, else
// resolved against `baseDirectory` (the base source's folder).
[[nodiscard]] QString modelLodRecipeSourcePath(const ModelLodRecipeLevel &level, const QString &baseDirectory);
// An editable source file stores hand-made level paths relative to its own
// folder; an open document holds them absolute (ModelDocument::load), and a
// save writes them relative to the folder it saves into, so saving the source
// somewhere else keeps every level pointing at its file. Paths on another
// drive stay absolute.
void absoluteModelLodSources(QVector<ModelLodRecipeLevel> *recipe, const QString &directory);
void relativeModelLodSources(QVector<ModelLodRecipeLevel> *recipe, const QString &directory);
// Reads a hand-made level's model: an editable source, or a native model
// imported as one.
bool loadModelLodSource(const QString &path, ModelMesh *mesh, QString *error = nullptr, const ModelWorkControl &control = {});
// Builds every level of `recipe` in order: a generated level reduces the one
// above it (the base for level 1), and a hand-made level is read from its
// source. Refuses an invalid recipe.
bool buildModelLodsFromRecipe(const ModelMesh &base, const QVector<ModelLodRecipeLevel> &recipe, const QString &baseDirectory,
							  QVector<ModelLodLevel> *result, QString *error = nullptr, const ModelWorkControl &control = {});

enum class ModelLodSeverity
{
	// The game would read the set wrongly or the target refuses a file.
	Error,
	// Loads, but likely not as intended.
	Warning,
	// Information, such as data the game ignores in detail levels.
	Note,
};

struct ModelLodFinding
{
	ModelLodSeverity severity = ModelLodSeverity::Error;
	// The detail level (1 ...) the finding is about; 0 for the whole set.
	int level = 0;
	// A stable identifier: "frames", "frame-names", "surfaces",
	// "surface-names", "target", "triangles", "tags", "materials".
	QString code;
	QString message;
};

struct ModelLodValidation
{
	QVector<ModelLodFinding> findings;
	[[nodiscard]] int count(ModelLodSeverity severity) const;
	[[nodiscard]] bool ok() const { return count(ModelLodSeverity::Error) == 0; }
};

// Checks detail levels (`levels[0]` is level 1) against `base` the way the
// engine reads them, and each level against the MD3 delivery `target`:
// - frames: every level needs the base's frame count (an error, since the
//   engine reads the level's vertices for a frame checked against the base),
//   and the same frame names in order (a warning);
// - surfaces: each level's surfaces must match the base's after Quake III's
//   name normalisation, so .skin files bind on every level (an error);
// - each level within the target's limits (errors, from assessModelTarget);
// - triangles that do not decrease from level to level (a warning);
// - tags and materials that differ (notes: R_LerpTag reads the base model's
//   tags only, and a level's own shaders show unless a .skin overrides them).
// `levelNumbers` gives each entry of `levels` its detail level when they are
// not 1, 2 ... in order (an existing set with a gap); findings then name the
// real level. False only on cancellation.
bool validateModelLodSet(const ModelMesh &base, const QVector<ModelMesh> &levels, const ModelTargetProfile &target,
						 ModelLodValidation *validation, QString *error = nullptr, const ModelWorkControl &control = {},
						 const QVector<int> &levelNumbers = {});
// "Error · level 2: ..." per finding, errors first.
[[nodiscard]] QStringList modelLodValidationLines(const ModelLodValidation &validation);
[[nodiscard]] QJsonArray modelLodValidationJson(const ModelLodValidation &validation);

// One row per base surface, with its triangles on the base and on each
// level (-1 when the level lacks it), then rows for level surfaces the base
// does not have. Entry k of `triangles` is level k; with `levelNumbers` (as
// for validateModelLodSet) a level the set lacks is a column of -1.
struct ModelLodSurfaceRow
{
	QString surface;
	QVector<int> triangles;
};
[[nodiscard]] QVector<ModelLodSurfaceRow> modelLodSurfaceTable(const ModelMesh &base, const QVector<ModelMesh> &levels,
															   const QVector<int> &levelNumbers = {});

// Reads one file of a detail set: true with its bytes, or false. A missing
// file leaves `error` empty; an unreadable one explains why.
using ModelLodFileReader = std::function<bool(const QString &path, QByteArray *bytes, QString *error)>;
// Reads loose files.
[[nodiscard]] ModelLodFileReader modelLodDiskReader();

struct ModelLodInspectedFile
{
	int level = 0;
	QString path;
	bool present = false;
	int frames = 0;
	int surfaces = 0;
	int triangles = 0;
	// Why a present file could not be decoded.
	QString error;
};

struct ModelLodInspection
{
	QString target;
	QString targetLabel;
	int targetLevels = 0;
	// The base model and every level the target loads, present or not.
	QVector<ModelLodInspectedFile> files;
	ModelLodValidation validation;
	QVector<ModelLodSurfaceRow> surfaces;
	// Switch depths for the levels present (modelQuake3LodSwitchDistances
	// with the default view), from the base model's first frame.
	QVector<double> switchDistances;
	[[nodiscard]] int levelsPresent() const;
};

// Inspects an existing set: `basePath` (an MD3) and each `name_N.md3` the MD3
// delivery `target` loads, read through `read` (loose files or package
// entries). Missing levels are reported, not refused (the engine skips them);
// a missing or unreadable base, and levels that fail to decode, are errors in
// the validation. On targets that load `name.mdc` before `name.md3` (Return to
// Castle Wolfenstein, Enemy Territory: R_RegisterModel tries the MDC first for
// the base and every level), each MDC found is a warning, and a level present
// only as an MDC loads, so it is no gap. False only on cancellation or when
// the base cannot be read.
bool inspectModelLodSet(const QString &basePath, const ModelLodFileReader &read, const ModelTargetProfile &target, ModelLodInspection *inspection,
						QString *error = nullptr, const ModelWorkControl &control = {});
[[nodiscard]] QJsonObject modelLodInspectionJson(const ModelLodInspection &inspection);
[[nodiscard]] QStringList modelLodInspectionLines(const ModelLodInspection &inspection);
// `name_2.md3` with its last character changed to `c` (`name_2.mdc`), the
// file the MDC-first targets try before it.
[[nodiscard]] QString modelLodMdcPath(const QString &md3Path);
// True for the MD3 delivery targets whose loader tries `.mdc` first.
[[nodiscard]] bool modelLodTargetLoadsMdcFirst(const ModelTargetProfile &target);

// Writing a base MD3 replaces only the files written: detail files already
// beside it stay, and the game still loads them with the new model. The
// review lists the files a write of the base and its first `writtenLevels`
// levels leaves in place although `target` loads them (levels past those up
// to the target's detail levels, and on MDC-first targets the `.mdc` of the
// base and of every level, which load in place of the new MD3), and checks
// each MD3 left in place against `base` as validateModelLodSet checks a level.
struct ModelLodSiblingReview
{
	QString target;
	QString targetLabel;
	// The files left in place, in level order; `level` 0 is the base's MDC.
	QVector<ModelLodInspectedFile> files;
	// Their consistency with `base`; errors mean the game would read them
	// wrongly with it.
	ModelLodValidation validation;
	[[nodiscard]] bool isEmpty() const { return files.isEmpty(); }
};
// `read` lists the destination: loose files (modelLodDiskReader) or package
// entries. False only on cancellation.
bool reviewModelLodSiblings(const ModelMesh &base, const QString &basePath, int writtenLevels, const ModelLodFileReader &read,
							const ModelTargetProfile &target, ModelLodSiblingReview *review, QString *error = nullptr,
							const ModelWorkControl &control = {});
// One warning per file left in place, saying why the game still loads it and
// how to replace or remove it, then each consistency finding; empty when
// nothing stays.
[[nodiscard]] QStringList modelLodSiblingLines(const ModelLodSiblingReview &review);
// One sentence for a status line: how many files stay and whether the game
// would read them wrongly; empty when nothing stays.
[[nodiscard]] QString modelLodSiblingSummary(const ModelLodSiblingReview &review);
// {target, files: [{level, path, mdc, frames, surfaces, triangles, error}],
// findings, consistent}.
[[nodiscard]] QJsonObject modelLodSiblingJson(const ModelLodSiblingReview &review);
// A reader over files already read (path to bytes), matching paths without
// case as the game's file system does; for package entries copied out first.
[[nodiscard]] ModelLodFileReader modelLodMemoryReader(const QHash<QString, QByteArray> &files);
// The paths reviewModelLodSiblings may read beside `basePath`: each level's
// MD3 and the MDC of the base and each level, up to modelLodMaxLevels.
[[nodiscard]] QStringList modelLodSiblingPaths(const QString &basePath);

// How the game views the model, for the switch distances: the horizontal field
// of view and aspect ratio (cg_fov 90 at 4:3 by default) and the r_lodscale
// and r_lodbias cvars (5 and 0).
struct ModelLodView
{
	double fovX = 90;
	double aspect = 4.0 / 3.0;
	double lodScale = 5;
	int lodBias = 0;
};
// RadiusFromBounds of `frame`'s bounds, as R_ComputeLOD measures the base
// model.
[[nodiscard]] double modelLodRadius(const ModelMesh &mesh, int frame = 0);
// The view depth at which each detail level starts to show, as R_ComputeLOD
// picks among `loadedModels` (the base plus its levels) for a model of
// `radius`: entry k is where level k begins (entry 0 is 0). A level r_lodbias
// skips past starts at 0 too, and with fewer than two models every entry is 0.
[[nodiscard]] QVector<double> modelQuake3LodSwitchDistances(double radius, int loadedModels, const ModelLodView &view = {});

enum class ModelLodCommit
{
	// Not written: the set was a dry run, or publication stopped earlier.
	Pending,
	Written,
	Failed,
};

struct ModelLodFile
{
	// 0 is the base model.
	int level = 0;
	QString path;
	QByteArray bytes;
	int triangles = 0;
	// Surfaces the decimation could not reduce further (detail levels only).
	QStringList notes;
	// What the MD3 export reported for this file, and the part of it that is
	// data the file leaves out (the export's notes and omissions).
	QStringList exportNotes;
	QStringList exportOmissions;
	ModelWriteTarget destination;
	ModelLodCommit commit = ModelLodCommit::Pending;
	// Why the file failed to publish.
	QString error;
	// A hand-made level's source model, else empty.
	QString source;
};

struct ModelLodSet
{
	// The MD3 delivery target and how many detail levels it loads.
	QString target;
	QString targetLabel;
	int targetLevels = 0;
	int levels = 0;
	double ratio = 0.5;
	// The recipe the levels were built from.
	QVector<ModelLodRecipeLevel> recipe;
	QVector<ModelLodFile> files;
	// The set's consistency (validateModelLodSet); a prepared set has no errors.
	ModelLodValidation validation;
	// Detail files beside the base that the target loads but the set does not
	// replace (levels past it, MDC files), from prepareModelLodSet's review of
	// the destinations; publishing leaves them as they are.
	ModelLodSiblingReview stale;
	// The destinations that already exist and would be replaced.
	[[nodiscard]] QStringList existing() const;
	[[nodiscard]] int written() const;
};

// Builds the base model and `levels` generated detail levels as MD3 for the
// delivery target `target` (empty: Quake III), and reviews every destination
// and the detail files beside them that the set leaves in place (`stale`).
// Refuses, preparing nothing, more levels than the target loads, outputs that
// are the same file as each other or as one of `protectedInputs`, destinations
// that are links or folders, any file the target's preflight blocks and, when
// `strict`, any file whose export would leave out data (as
// ModelExportOptions::strict). Each file keeps its export's notes.
bool prepareModelLodSet(const ModelMesh &mesh, int levels, double ratio, const QString &basePath, const QString &target,
						const QStringList &protectedInputs, bool strict, ModelLodSet *set, QString *error = nullptr,
						const ModelWorkControl &control = {});
// The same for a recipe, whose hand-made sources resolve against
// `baseDirectory` and are protected inputs too. Also refuses a set whose
// levels fail validateModelLodSet.
bool prepareModelLodSet(const ModelMesh &mesh, const QVector<ModelLodRecipeLevel> &recipe, const QString &baseDirectory, const QString &basePath,
						const QString &target, const QStringList &protectedInputs, bool strict, ModelLodSet *set, QString *error = nullptr,
						const ModelWorkControl &control = {});
// The files of a recipe's set, built and checked as prepareModelLodSet does
// but with no destinations reviewed: `basePath` is a package path, for
// staging the set into a package (core/model_design.h stageModelExports).
bool prepareModelLodFiles(const ModelMesh &mesh, const QVector<ModelLodRecipeLevel> &recipe, const QString &baseDirectory, const QString &basePath,
						  const QString &target, bool strict, ModelLodSet *set, QString *error = nullptr, const ModelWorkControl &control = {});
// Rechecks every reviewed destination first, so nothing is written when one
// changed since review or exists without `overwrite`. New files are then
// published without replacement and existing ones through QSaveFile, in
// level order; each file's commit state records what happened, and a failure
// part way names the files already written.
bool publishModelLodSet(ModelLodSet *set, bool overwrite, QString *error = nullptr, const ModelWorkControl &control = {});
// One object per file: path, level, triangles, bytes, existed, committed
// ("pending", "written" or "failed"), notes, exportNotes and exportOmissions,
// and source for a hand-made level.
[[nodiscard]] QJsonArray modelLodSetJson(const ModelLodSet &set);
} // namespace vibestudio
