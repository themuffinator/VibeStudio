#include "core/model_skeletal_working_set.h"
#include "core/model_document.h"
#include "core/model_skeletal_export.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QLocale>

#include <algorithm>

namespace vibestudio {

namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(ModelSkeletalWorkingSet)
};

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

qint64 vertexCount(const ModelMesh& mesh)
{
	qint64 vertices = 0;
	for (const ModelSurface& surface : mesh.surfaces) {
		vertices += surface.vertexCount;
	}
	return vertices;
}

bool fits(const ModelSkeletalBudget& budget, qint64 vertices, qint64 frames)
{
	return frames <= budget.frames && frames * vertices <= budget.frameVertices;
}

// The bake's own rule: a bind frame when the model has one baked, or no clips.
bool keepsBindFrame(const ModelMesh& mesh)
{
	return mesh.skeleton.bakedClipForFrame.contains(-1) || mesh.skeleton.clips.isEmpty();
}

ModelSkeletalWorkingSetPlan emptyPlan(const ModelMesh& mesh, const ModelSkeletalBudget& budget)
{
	ModelSkeletalWorkingSetPlan plan;
	plan.budget = budget;
	plan.vertices = vertexCount(mesh);
	plan.bindFrame = keepsBindFrame(mesh);
	for (int index = 0; index < mesh.skeleton.clips.size(); ++index) {
		const ModelSkeletalClip& clip = mesh.skeleton.clips.at(index);
		ModelSkeletalClipCost cost;
		cost.clip = index;
		cost.name = clip.name;
		cost.frames = int(clip.frames.size());
		cost.frameVertices = qint64(cost.frames) * plan.vertices;
		cost.baked = mesh.skeleton.bakedClipForFrame.contains(index);
		plan.costs.append(cost);
	}
	return plan;
}

// Clips left out of a plan: empty, too large for the whole editable budget
// (whatever share of it the plan fills), or simply not chosen.
ModelSkeletalClipState leftOut(const ModelSkeletalWorkingSetPlan& plan, const ModelSkeletalClipCost& cost)
{
	if (cost.frames == 0) {
		return ModelSkeletalClipState::Empty;
	}
	if (!fits(modelSkeletalEditableBudget(), plan.vertices, qint64(cost.frames) + (plan.bindFrame ? 1 : 0))) {
		return ModelSkeletalClipState::TooLarge;
	}
	return ModelSkeletalClipState::Outside;
}

void finish(ModelSkeletalWorkingSetPlan* plan, int frames)
{
	if (frames == 0) {
		// A model always keeps one editable frame: the bind pose.
		plan->bindFrame = true;
		frames = 1;
	}
	plan->frames = frames;
	plan->frameVertices = qint64(frames) * plan->vertices;
	plan->fits = fits(plan->budget, plan->vertices, frames);
}

} // namespace

ModelSkeletalBudget modelSkeletalEditableBudget()
{
	return {modelDocumentMaxFrames, modelDocumentMaxFrameVertices};
}

ModelSkeletalBudget modelSkeletalAutomaticBudget()
{
	return {modelDocumentMaxFrames, modelDocumentMaxFrameVertices / 4 * 3};
}

bool ModelSkeletalWorkingSetPlan::changes() const
{
	return std::any_of(costs.cbegin(), costs.cend(),
		[](const ModelSkeletalClipCost& cost) { return cost.baked != (cost.state == ModelSkeletalClipState::Baked); });
}

qint64 ModelSkeletalWorkingSetPlan::vertexRoom() const
{
	if (frames <= 0) {
		return 0;
	}
	const qint64 room = budget.frameVertices / frames - vertices;
	return std::clamp<qint64>(room, 0, std::max<qint64>(0, modelDocumentMaxVertices - vertices));
}

QStringList ModelSkeletalWorkingSetPlan::names(ModelSkeletalClipState state) const
{
	QStringList result;
	for (const ModelSkeletalClipCost& cost : costs) {
		if (cost.state == state) {
			result << cost.name;
		}
	}
	return result;
}

ModelSkeletalWorkingSetPlan modelSkeletalWorkingSet(const ModelMesh& mesh, const ModelSkeletalBudget& budget)
{
	ModelSkeletalWorkingSetPlan plan = emptyPlan(mesh, budget);
	plan.bindFrame = mesh.skeleton.bakedClipForFrame.contains(-1);
	for (ModelSkeletalClipCost& cost : plan.costs) {
		if (cost.baked) {
			cost.state = ModelSkeletalClipState::Baked;
			plan.clips.append(cost.clip);
		} else {
			cost.state = leftOut(plan, cost);
		}
	}
	// Frames added by frame tools count too: they hold editable geometry.
	plan.frames = int(mesh.frames.size());
	plan.frameVertices = qint64(plan.frames) * plan.vertices;
	plan.fits = fits(budget, plan.vertices, plan.frames);
	return plan;
}

ModelSkeletalWorkingSetPlan planModelSkeletalWorkingSet(const ModelMesh& mesh, const QVector<int>* clips, const ModelSkeletalBudget& budget)
{
	ModelSkeletalWorkingSetPlan plan = emptyPlan(mesh, budget);
	int frames = plan.bindFrame ? 1 : 0;
	if (clips) {
		for (ModelSkeletalClipCost& cost : plan.costs) {
			if (clips->contains(cost.clip) && cost.frames > 0) {
				cost.state = ModelSkeletalClipState::Baked;
				plan.clips.append(cost.clip);
				frames += cost.frames;
			}
		}
		finish(&plan, frames);
		for (ModelSkeletalClipCost& cost : plan.costs) {
			if (cost.state != ModelSkeletalClipState::Baked) {
				cost.state = leftOut(plan, cost);
			}
		}
		return plan;
	}
	// The bake fills its limits in skeleton order and skips any clip that does
	// not fit what remains, so later, shorter clips can still join.
	for (ModelSkeletalClipCost& cost : plan.costs) {
		cost.state = leftOut(plan, cost);
		if (cost.state != ModelSkeletalClipState::Outside) {
			continue;
		}
		if (fits(budget, plan.vertices, qint64(frames) + cost.frames)) {
			cost.state = ModelSkeletalClipState::Baked;
			plan.clips.append(cost.clip);
			frames += cost.frames;
		} else {
			cost.state = ModelSkeletalClipState::OverBudget;
		}
	}
	finish(&plan, frames);
	return plan;
}

QString modelSkeletalClipStateId(ModelSkeletalClipState state)
{
	switch (state) {
	case ModelSkeletalClipState::Baked:
		return QStringLiteral("baked");
	case ModelSkeletalClipState::Outside:
		return QStringLiteral("outside");
	case ModelSkeletalClipState::OverBudget:
		return QStringLiteral("over-budget");
	case ModelSkeletalClipState::TooLarge:
		return QStringLiteral("too-large");
	case ModelSkeletalClipState::Empty:
		return QStringLiteral("empty");
	}
	return QStringLiteral("outside");
}

QStringList modelSkeletalWorkingSetLines(const ModelSkeletalWorkingSetPlan& plan, const QLocale& locale)
{
	// Counts follow labels, so no sentence needs a plural form for a number,
	// and every number uses `locale`.
	const auto number = [&locale](qint64 value) { return locale.toString(value); };
	QStringList lines;
	lines << Text::tr("Skeletal clips baked into editable frames: %1 of %2.").arg(number(plan.clips.size()), number(plan.costs.size()));
	const qint64 percent = plan.budget.frameVertices > 0 ? plan.frameVertices * 100 / plan.budget.frameVertices : 0;
	lines << Text::tr("Frames used: %1 of %2. Frame vertices used: %3 of %4 (%5%).")
				 .arg(number(plan.frames), number(plan.budget.frames), number(plan.frameVertices), number(plan.budget.frameVertices),
					 number(percent));
	if (!plan.fits) {
		lines << Text::tr("The chosen clips are over the budget. Choose fewer or shorter clips.");
	} else {
		lines << Text::tr("Vertices mesh tools can still add to every frame: %1.").arg(number(plan.vertexRoom()));
	}
	const auto group = [&](ModelSkeletalClipState state, const QString& line) {
		const QStringList clipNames = plan.names(state);
		if (!clipNames.isEmpty()) {
			lines << line.arg(clipNames.join(QStringLiteral(", ")));
		}
	};
	group(ModelSkeletalClipState::Outside, Text::tr("Outside the working set: %1."));
	group(ModelSkeletalClipState::OverBudget, Text::tr("Left out to fit the budget: %1."));
	group(ModelSkeletalClipState::TooLarge,
		Text::tr("Too long to bake within the budget, so edit them through the skeletal tools, which preview one sample at a time: %1."));
	group(ModelSkeletalClipState::Empty, Text::tr("No poses to bake: %1."));
	return lines;
}

QJsonObject modelSkeletalWorkingSetJson(const ModelSkeletalWorkingSetPlan& plan)
{
	QJsonArray clips;
	for (const ModelSkeletalClipCost& cost : plan.costs) {
		clips.append(QJsonObject{{QStringLiteral("index"), cost.clip},
			{QStringLiteral("name"), cost.name},
			{QStringLiteral("frames"), cost.frames},
			{QStringLiteral("frameVertices"), double(cost.frameVertices)},
			{QStringLiteral("inWorkingSet"), cost.state == ModelSkeletalClipState::Baked},
			{QStringLiteral("state"), modelSkeletalClipStateId(cost.state)}});
	}
	QJsonArray chosen;
	for (int clip : plan.clips) {
		chosen.append(clip);
	}
	return QJsonObject{{QStringLiteral("budget"),
						   QJsonObject{{QStringLiteral("frames"), plan.budget.frames},
							   {QStringLiteral("frameVertices"), double(plan.budget.frameVertices)}}},
		{QStringLiteral("vertices"), double(plan.vertices)},
		{QStringLiteral("bindFrame"), plan.bindFrame},
		{QStringLiteral("frames"), plan.frames},
		{QStringLiteral("frameVertices"), double(plan.frameVertices)},
		{QStringLiteral("fits"), plan.fits},
		{QStringLiteral("vertexRoom"), double(plan.fits ? plan.vertexRoom() : 0)},
		{QStringLiteral("clipIndexes"), chosen},
		{QStringLiteral("clips"), clips}};
}

bool modelSkeletalClipIndexes(const ModelSkeleton& skeleton, const QStringList& texts, QVector<int>* clips, QString* error)
{
	QVector<int> result;
	for (const QString& text : texts) {
		const int clip = modelSkeletalClipIndex(skeleton, text);
		if (clip < 0) {
			return fail(error, Text::tr("No skeletal clip is named or numbered %1.").arg(text.trimmed()));
		}
		if (result.contains(clip)) {
			return fail(error, Text::tr("Skeletal clip %1 is chosen twice.").arg(text.trimmed()));
		}
		result.append(clip);
	}
	std::sort(result.begin(), result.end());
	if (clips) {
		*clips = result;
	}
	return true;
}

bool fitModelSkeletalWorkingSet(ModelMesh* mesh, const QVector<int>* clips, ModelSkeletalWorkingSetPlan* plan, QString* error,
	const ModelWorkControl& control)
{
	if (!mesh || mesh->skeleton.isEmpty()) {
		return fail(error, Text::tr("Only a skeletal model has clips to choose for its working set."));
	}
	const ModelSkeletalBudget editable = modelSkeletalEditableBudget();
	ModelSkeletalWorkingSetPlan planned = planModelSkeletalWorkingSet(*mesh, clips, clips ? editable : modelSkeletalAutomaticBudget());
	if (!planned.fits) {
		return fail(error, modelSkeletalWorkingSetLines(planned).join(QLatin1Char('\n')));
	}
	ModelMesh candidate = *mesh;
	if (!rebakeModelSkeleton(&candidate, error, control, editable.frames, editable.frameVertices, &planned.clips)) {
		return false;
	}
	if (candidate.frames.size() != planned.frames) {
		return fail(error, Text::tr("The skeletal working set could not be baked as planned."));
	}
	for (ModelSkeletalClipCost& cost : planned.costs) {
		cost.baked = cost.state == ModelSkeletalClipState::Baked;
	}
	// The receipt measures the result against the whole editable budget, as
	// the Skeleton page, the dialog and model pose do; the automatic budget
	// only chose the clips.
	planned.budget = editable;
	planned.fits = fits(editable, planned.vertices, planned.frames);
	*mesh = std::move(candidate);
	if (plan) {
		*plan = std::move(planned);
	}
	return true;
}

ModelMesh modelSkeletalSamplePreview(const ModelMesh& mesh, int clip, int sample, int* frame)
{
	if (frame) {
		*frame = -1;
	}
	const ModelSkeleton& skeleton = mesh.skeleton;
	if (clip < 0 || clip >= skeleton.clips.size() || sample < 0 || sample >= skeleton.clips.at(clip).frames.size()) {
		return mesh;
	}
	const QVector<int>& map = skeleton.bakedClipForFrame;
	const int first = map.size() == mesh.frames.size() ? int(map.indexOf(clip)) : -1;
	if (first >= 0) {
		if (first + sample < map.size() && map.at(first + sample) == clip) {
			if (frame) {
				*frame = first + sample;
			}
		}
		return mesh;
	}
	// Outside the working set: skin just this sample. The copy's skeleton holds
	// that one pose as its only clip, so the joint overlay finds it too.
	ModelMesh preview = mesh;
	ModelSkeletalClip shown = skeleton.clips.at(clip);
	shown.frames = {shown.frames.at(sample)};
	shown.frameMins = sample < shown.frameMins.size() ? QVector<ModelVec3>{shown.frameMins.at(sample)} : QVector<ModelVec3>{};
	shown.frameMaxs = sample < shown.frameMaxs.size() ? QVector<ModelVec3>{shown.frameMaxs.at(sample)} : QVector<ModelVec3>{};
	shown.curves.clear();
	preview.skeleton.clips = {shown};
	preview.collisionBoxes.clear();
	if (preview.mdl.enabled) {
		preview.mdl.frameGroups = {ModelMdlFrameGroup{}};
	}
	ModelSkeletonBakeOptions options;
	options.includeBindPose = false;
	if (!bakeModelSkeleton(&preview, options)) {
		return mesh;
	}
	updateEditableModelMetadata(&preview);
	if (frame) {
		*frame = 0;
	}
	return preview;
}

} // namespace vibestudio
