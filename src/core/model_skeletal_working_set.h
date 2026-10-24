#pragma once

// The skeletal working set: the retained clips a skeletal model bakes into
// editable frames.
//
// Editable sources hold at most modelDocumentMaxFrames frames and
// modelDocumentMaxFrameVertices frame vertices (vertices of every surface
// times frames, core/model_document.h). A dense model with many clips keeps
// every clip in its skeleton and bakes only some of them. Clips outside the
// working set stay exact and keep their curves; pose, curve, key, transfer and
// IK edits still work on them through the skeletal services, and tool
// previews skin them one sample at a time (modelSkeletalSamplePreview). The
// skeletal exports read the skeleton, so MD5 and IQM write every clip.
//
// The working set needs no field of its own: it is the set of clips with
// baked frames (ModelSkeleton::bakedClipForFrame), which the source saves.
// ModelEditKind::SetSkeletalWorkingSet changes it as one undoable edit, and
// importEditableModel fits an oversized skeletal import into it.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct ModelSkeletalBudget {
	int frames = 0;
	qint64 frameVertices = 0;
	bool operator==(const ModelSkeletalBudget&) const = default;
};

// The whole editable budget: modelDocumentMaxFrames frames and
// modelDocumentMaxFrameVertices frame vertices.
[[nodiscard]] ModelSkeletalBudget modelSkeletalEditableBudget();
// What an automatic choice fills (an oversized import, or Fit Automatically):
// every frame, but three quarters of the frame vertices, so mesh tools that
// add vertices still have room in every frame.
[[nodiscard]] ModelSkeletalBudget modelSkeletalAutomaticBudget();

enum class ModelSkeletalClipState {
	// In the planned working set.
	Baked,
	// Left out by choice.
	Outside,
	// Tried by an automatic choice, but it did not fit what remained.
	OverBudget,
	// Its frames alone, with the bind frame, exceed the whole budget: it can
	// only be edited through the skeletal tools.
	TooLarge,
	// It has no poses to bake.
	Empty,
};

struct ModelSkeletalClipCost {
	int clip = -1;
	QString name;
	int frames = 0;
	// frames times the model's vertices.
	qint64 frameVertices = 0;
	// In the working set before the plan.
	bool baked = false;
	ModelSkeletalClipState state = ModelSkeletalClipState::Outside;
};

struct ModelSkeletalWorkingSetPlan {
	ModelSkeletalBudget budget;
	// Vertices per frame, over every surface.
	qint64 vertices = 0;
	// The plan bakes a bind-pose frame first.
	bool bindFrame = false;
	// What the planned working set bakes, the bind frame included.
	int frames = 0;
	qint64 frameVertices = 0;
	// The planned clips in skeleton order.
	QVector<int> clips;
	// Every clip of the skeleton, in order.
	QVector<ModelSkeletalClipCost> costs;
	// False when an explicit choice exceeds the budget.
	bool fits = true;
	// The plan differs from the current working set.
	[[nodiscard]] bool changes() const;
	// How many more vertices every planned frame could hold within the budget.
	[[nodiscard]] qint64 vertexRoom() const;
	[[nodiscard]] QStringList names(ModelSkeletalClipState state) const;
};

// The working set `mesh` bakes now, measured against `budget`.
[[nodiscard]] ModelSkeletalWorkingSetPlan modelSkeletalWorkingSet(const ModelMesh& mesh,
	const ModelSkeletalBudget& budget = modelSkeletalEditableBudget());
// Plans a working set. With `clips`, exactly those clip indexes (fits is false
// when they exceed `budget`); without, every clip that still fits `budget`,
// in skeleton order, the way the bake fills its limits. A plan keeps the
// current bind-pose frame and adds one when no clip is baked.
[[nodiscard]] ModelSkeletalWorkingSetPlan planModelSkeletalWorkingSet(const ModelMesh& mesh, const QVector<int>* clips,
	const ModelSkeletalBudget& budget);
// The plan as text for reports, the CLI and the editor's details, in a fixed
// order: working set, budget use, room left, then each group of clips left out.
// Numbers use `locale`: the C locale keeps CLI text deterministic, and the
// editor passes QLocale().
[[nodiscard]] QStringList modelSkeletalWorkingSetLines(const ModelSkeletalWorkingSetPlan& plan, const QLocale& locale = QLocale::c());
[[nodiscard]] QJsonObject modelSkeletalWorkingSetJson(const ModelSkeletalWorkingSetPlan& plan);
// The stable identifier of a clip state in JSON: baked, outside, over-budget,
// too-large or empty.
[[nodiscard]] QString modelSkeletalClipStateId(ModelSkeletalClipState state);

// Resolves clip names or zero-based indexes (modelSkeletalClipIndex) into
// sorted, distinct clip indexes. False with an error naming the first text
// that names no clip, or a clip named twice.
bool modelSkeletalClipIndexes(const ModelSkeleton& skeleton, const QStringList& texts, QVector<int>* clips, QString* error = nullptr);

// Re-bakes an imported skeletal model's frames from its skeleton so they fit
// the editable budget: exactly `clips` when given (refused when they exceed
// the whole editable budget), else every clip that fits the automatic budget
// in skeleton order. The skeleton keeps every clip. Fills `plan` with what was
// baked and what was left out. Leaves `mesh` unchanged on failure.
bool fitModelSkeletalWorkingSet(ModelMesh* mesh, const QVector<int>* clips, ModelSkeletalWorkingSetPlan* plan = nullptr,
	QString* error = nullptr, const ModelWorkControl& control = {});

// The mesh a tool preview shows for sample `sample` of clip `clip`, and the
// frame to show in it. A clip in the working set shows its baked frame in
// `mesh` itself. Any other clip is skinned on demand into a one-frame copy
// (skeletal attachments follow; manual attachments and collision are not
// shown), so clips outside the working set preview without being baked.
// *frame is -1, and the result is `mesh`, when the clip or sample does not
// exist or cannot be skinned.
[[nodiscard]] ModelMesh modelSkeletalSamplePreview(const ModelMesh& mesh, int clip, int sample, int* frame);

} // namespace vibestudio
