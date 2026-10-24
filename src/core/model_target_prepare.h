#pragma once

// Prepare for target: applies the fixes a delivery target's preflight
// (core/model_targets.h) suggests, as ordinary model edits on a copy of the
// model, and reports what each changed and what still blocks the target.
//
// The fixes, each a sequence of the editor's own edits (applyModelEdit), so
// skin weights, seams and every animation pose follow as they do by hand:
// - split: a surface over its vertex or triangle budget (MD3, IQM) is
//   separated into connected parts that fit (Separate selected faces), while
//   the model stays within its surface limit;
// - join: the surfaces of a model bound for MDL or MD2, which hold one, are
//   joined into the first (Join surfaces, adopting its materials);
// - decimate: a model or surface still over a triangle or vertex budget is
//   reduced to fit (Decimate with a triangle target);
// - fit: a model outside MD3's -512 to 511.984375 unit range is moved, and
//   scaled down only when it is too large to fit, with its tags and collision
//   boxes (a surface transform of every pose);
// - influences: vertices with more joint influences than the format keeps
//   (IQM: four) keep their strongest ones (Prune Skin Weights).
// Limits no edit can meet safely (names, skins, frames, joints, precision)
// stay in the report with their fixes, for the author to decide.
//
// The editor adopts the prepared model as one undo step (ModelDocument::adopt)
// after a review; `model prepare` saves it as a new source.

#include "core/model_document.h"
#include "core/model_targets.h"

#include <QJsonObject>
#include <QStringList>
#include <QVector>

namespace vibestudio
{
enum class ModelTargetFixKind
{
	Split,
	Join,
	Decimate,
	Fit,
	Influences,
};

struct ModelTargetFix
{
	ModelTargetFixKind kind = ModelTargetFixKind::Split;
	// The limit the fix addresses, as in ModelTargetIssue::limit.
	QString limit;
	// The surface the fix changed (its name before the fix), or empty.
	QString element;
	// What the fix did, translated.
	QString summary;
	// The measured value before and after the fix: triangles for a
	// decimation, vertices or triangles for a split (the largest part), and
	// so on.
	qint64 before = 0;
	qint64 after = 0;
};

struct ModelTargetPrepareOptions
{
	bool split = true;
	bool join = true;
	bool decimate = true;
	bool fit = true;
	bool influences = true;
	bool operator==(const ModelTargetPrepareOptions &) const = default;
};

struct ModelTargetPreparation
{
	ModelTargetProfile profile;
	// The full assessment before and after the fixes.
	ModelTargetReport before;
	ModelTargetReport after;
	QVector<ModelTargetFix> fixes;
	// What the fixes changed that the author should know, such as materials
	// a join replaced or an origin that moved.
	QStringList notes;
	// The prepared model and the selection it leaves (the surfaces changed).
	ModelMesh mesh;
	ModelSelection selection;
	[[nodiscard]] bool changed() const { return !fixes.isEmpty(); }
};

// Prepares `mesh` (a valid editable model) for `profile`, applying the fixes
// `options` allows. Succeeds, with no fixes, when nothing applies; the
// `after` report then lists what still blocks the target. False only on
// cancellation or when an edit fails unexpectedly.
bool prepareModelForTarget(const ModelMesh &mesh, const ModelTargetProfile &profile, const ModelTargetPrepareOptions &options,
						   ModelTargetPreparation *preparation, QString *error = nullptr, const ModelWorkControl &control = {});

// "split", "join", "decimate", "fit" and "influences".
[[nodiscard]] QString modelTargetFixKindId(ModelTargetFixKind kind);
// The translated name of a fix kind, such as "Split surfaces".
[[nodiscard]] QString modelTargetFixKindName(ModelTargetFixKind kind);
// Turns off the comma-separated fix kinds of `skip` ("split,decimate").
bool skipModelTargetFixes(const QString &skip, ModelTargetPrepareOptions *options, QString *error = nullptr);
// One line per fix, then the notes, then what still blocks the target.
[[nodiscard]] QStringList modelTargetPreparationLines(const ModelTargetPreparation &preparation);
[[nodiscard]] QJsonObject modelTargetPreparationJson(const ModelTargetPreparation &preparation);
} // namespace vibestudio
