// Undo history memory (core/model_document_memory.h): skeletal clips,
// curves and skinning are counted, arrays undo states share are counted once
// (so frame-local edits cost one frame), selection-only and no-op edits add
// nothing, the documented limit holds through edits, undo and redo, steps
// restore exactly after trimming, document copies keep their own accounting,
// and the model/historyBudgetMiB setting round-trips.
#include "core/model_document.h"
#include "core/studio_settings.h"
#include "tests/model_scale_test_helpers.h"
#include "tests/model_skeletal_budget_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int failures = 0;
bool expect(bool value, const char *message, const QString &detail = {})
{
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty())
			std::cerr << ": " << qPrintable(detail);
		std::cerr << std::endl;
	}
	return value;
}
QString mib(qint64 bytes) { return modelMebibytesText(bytes); }

// One frame-local move: four vertices of `frame`, whose positions and normals
// are the only arrays the edit changes.
ModelEdit frameMove(int index, int frame)
{
	ModelEdit edit;
	edit.kind = ModelEditKind::Transform;
	edit.frame = frame;
	edit.translation = {0.5f, 0, 0};
	edit.selection.vertices = {index * 7, index * 7 + 1, index * 7 + 2, index * 7 + 3};
	return edit;
}
// One frame's positions and normals of the 65,536-vertex grid, with their
// allocation headers.
constexpr qint64 gridFrameBytes = 2 * (65536 * qint64(sizeof(ModelVec3)) + 16);

bool sharing()
{
	bool ok = true;
	QString error;
	ModelDocument document;
	if (!expect(document.setMesh(maximumEditableGrid(), &error), "prepare the 16-pose maximum grid", error))
		return false;
	ok &= expect(document.historyBytes() == 0 && document.undoSteps() == 0, "a new document holds no history");
	ModelSelection selection;
	selection.vertices = {1, 2, 3};
	document.setSelection(selection);
	ModelEdit still = frameMove(0, 2);
	still.translation = {};
	ok &= expect(document.historyBytes() == 0 && document.undoSteps() == 0 && document.edit(still, &error) && document.undoSteps() == 0 &&
					 document.historyBytes() == 0,
				 "selection-only and no-op edits add no step and no history", error);
	// Thirty moves in different frames: each costs the one frame it changed,
	// not the 16 frames and topology the old per-state estimate charged.
	for (int i = 0; i < 30; ++i)
	{
		if (!expect(document.edit(frameMove(i + 1, i % 16), &error), "a frame-local move applies", error))
			return false;
	}
	// Detached arrays keep their spare capacity, which is memory they hold.
	const qint64 floor = 30 * gridFrameBytes, ceiling = 30 * (gridFrameBytes * 5 / 4 + 64 * 1024);
	std::cout << "frame-local: " << document.undoSteps() << " steps hold " << qPrintable(mib(document.historyBytes())) << std::endl;
	ok &= expect(document.undoSteps() == 30 && document.discardedHistorySteps() == 0 && document.historyBytes() >= floor &&
					 document.historyBytes() <= ceiling,
				 "frame-local moves keep 30 steps, each holding about the one frame it changed",
				 QStringLiteral("%1 steps, %2").arg(document.undoSteps()).arg(document.historyBytes()));
	const auto before = document.historyBytes();
	selection.vertices = {9, 10};
	document.setSelection(selection);
	ok &= expect(document.undoSteps() == 30 && std::abs(document.historyBytes() - before) <= 4096,
				 "changing the selection adds no step and at most the old selection's storage");
	// Copies share every state; editing one leaves the other's accounting alone.
	ModelDocument copy = document;
	ok &= expect(copy.edit(frameMove(40, 5), &error) && copy.undoSteps() == 31 && document.undoSteps() == 30 &&
					 std::abs(document.historyBytes() - before) <= 4096 && copy.historyBytes() > document.historyBytes(),
				 "a document copy keeps its own history accounting", error);
	// Walking to the oldest step and back reproduces the same accounting.
	const auto newest = document.revisionFingerprint();
	int undone = 0;
	while (document.undo())
	{
		++undone;
		ok &= expect(document.historyBytes() <= modelDocumentHistoryLimit(), "undo stays within the history limit");
	}
	while (document.redo())
		;
	ok &= expect(undone == 30 && document.revisionFingerprint() == newest && std::abs(document.historyBytes() - before) <= 4096,
				 "undoing every step and redoing it restores the newest state and the same history size");
	ok &= expect(document.setMesh(maximumEditableGrid(), &error) && document.historyBytes() == 0 && document.discardedHistorySteps() == 0,
				 "setting a new mesh clears history and its accounting", error);
	return ok;
}

bool trimming()
{
	bool ok = true;
	QString error;
	// A 4 MiB limit holds two frame-local steps of the grid.
	setModelDocumentHistoryLimit(4 * 1024 * 1024);
	ModelDocument document;
	if (!expect(document.setMesh(maximumEditableGrid(), &error), "prepare the grid for trimming", error))
		return false;
	QVector<QByteArray> revisions{document.revisionFingerprint()};
	for (int i = 0; i < 10; ++i)
	{
		ok &= expect(document.edit(frameMove(i, i), &error) && document.historyBytes() <= modelDocumentHistoryLimit(),
					 "each edit stays within the lowered limit", error);
		revisions.append(document.revisionFingerprint());
	}
	ok &= expect(document.undoSteps() == 2 && document.discardedHistorySteps() == 8, "history trimmed the eight oldest steps",
				 QStringLiteral("%1 kept, %2 discarded").arg(document.undoSteps()).arg(document.discardedHistorySteps()));
	ok &= expect(document.undo() && document.revisionFingerprint() == revisions[9] && document.undo() &&
					 document.revisionFingerprint() == revisions[8] && !document.undo() && document.historyBytes() <= modelDocumentHistoryLimit(),
				 "undo after trimming restores the kept steps exactly");
	ok &= expect(document.redo() && document.revisionFingerprint() == revisions[9] && document.redo() &&
					 document.revisionFingerprint() == revisions[10] && !document.redo(),
				 "redo after trimming restores the kept steps exactly");
	// The step before the current one is always kept, however large.
	setModelDocumentHistoryLimit(1);
	ok &= expect(modelDocumentHistoryLimit() == 1024 * 1024, "the history limit is at least 1 MiB");
	document.enforceHistoryLimit();
	ok &= expect(document.undoSteps() == 1 && document.undo() && document.revisionFingerprint() == revisions[9],
				 "a limit below one step still keeps the step before the current one");
	setModelDocumentHistoryLimit(modelDocumentDefaultHistoryBytes);
	return ok;
}

// Undo and redo never discard: stepping back from large new steps to a small
// first state keeps every redo step, even past the limit, and lowering the
// limit discards the oldest undo steps while keeping the step before the
// current one and every redo step.
bool walking()
{
	bool ok = true;
	QString error;
	// Each working-set edit joins a 200-frame clip on 1,000 vertices: about
	// 4.6 MiB of new frames that no earlier state shares.
	setModelDocumentHistoryLimit(8 * 1024 * 1024);
	ModelDocument document;
	const auto mesh = denseSkeletalFixture(1000, {{"a", 200}, {"b", 200}, {"c", 200}}, {});
	if (!expect(!mesh.surfaces.isEmpty() && mesh.frames.size() == 1 && document.setMesh(mesh, &error), "prepare a bind-pose skeletal model", error))
		return false;
	QVector<QByteArray> revisions{document.revisionFingerprint()};
	for (const auto &clips : {QVector<int>{0}, QVector<int>{0, 1}, QVector<int>{0, 1, 2}})
	{
		ModelEdit edit;
		edit.kind = ModelEditKind::SetSkeletalWorkingSet;
		edit.skeletal.workingSet = clips;
		ok &= expect(document.edit(edit, &error), "a clip joins the working set", error);
		revisions.append(document.revisionFingerprint());
	}
	ok &= expect(document.undoSteps() == 3 && document.discardedHistorySteps() == 0 && document.historyBytes() <= modelDocumentHistoryLimit(),
				 "growing edits keep every small earlier state");
	int undone = 0;
	while (document.undo())
		++undone;
	std::cout << "walked back: " << document.redoSteps() << " redo steps hold " << qPrintable(mib(document.historyBytes())) << std::endl;
	ok &= expect(undone == 3 && document.revisionFingerprint() == revisions[0] && document.redoSteps() == 3 && document.discardedHistorySteps() == 0 &&
					 document.historyBytes() > modelDocumentHistoryLimit(),
				 "undoing to the small first state keeps all three larger redo steps, past the limit");
	int redone = 0;
	while (document.redo())
		ok &= expect(document.revisionFingerprint() == revisions[++redone], "redo restores each step in order");
	ok &= expect(redone == 3 && document.revisionFingerprint() == revisions[3], "redo reaches the newest work again");
	// Lowering the limit in the middle of history: the oldest undo step goes,
	// the step before the current one and the redo step stay.
	ok &= expect(document.undo() && document.revisionFingerprint() == revisions[2] && document.undoSteps() == 2 && document.redoSteps() == 1,
				 "step back one");
	setModelDocumentHistoryLimit(1);
	document.enforceHistoryLimit();
	ok &= expect(document.undoSteps() == 1 && document.redoSteps() == 1 && document.discardedHistorySteps() == 1,
				 "a lower limit discards the oldest undo step only",
				 QStringLiteral("%1 undo, %2 redo").arg(document.undoSteps()).arg(document.redoSteps()));
	ok &= expect(document.redo() && document.revisionFingerprint() == revisions[3] && document.undo() && document.undo() &&
					 document.revisionFingerprint() == revisions[1] && !document.undo(),
				 "the newest step and the step before the current one survive the lower limit");
	// The next edit replaces the redo steps and trims within the limit again.
	ok &= expect(document.redo() && document.redo() && document.revisionFingerprint() == revisions[3], "back to the newest step");
	ModelEdit rename;
	rename.kind = ModelEditKind::RenameSkeletalClip;
	rename.skeletal.clip = 2;
	rename.skeletal.name = QStringLiteral("c2");
	ok &= expect(document.edit(rename, &error) && document.redoSteps() == 0 && document.undoSteps() >= 1, "an edit after walking applies", error);
	setModelDocumentHistoryLimit(modelDocumentDefaultHistoryBytes);
	return ok;
}

bool skeletal()
{
	bool ok = true;
	QString error;
	// The audit fixture: 8,192 vertices with four influences each, 64 joints,
	// six 30-frame clips with curves on the first. Three clips are baked.
	const auto mesh = realisticSkeletalDocument(6, 30, 1);
	ModelDocument document;
	if (!expect(!mesh.surfaces.isEmpty() && document.setMesh(mesh, &error), "prepare the skinned fixture", error))
		return false;
	int outside = -1;
	for (int clip = 0; clip < mesh.skeleton.clips.size(); ++clip)
		if (!mesh.skeleton.bakedClipForFrame.contains(clip))
			outside = clip;
	if (!expect(outside > 0, "a clip is outside the working set"))
		return false;
	// A pose edit on a clip outside the working set changes one frame of
	// joint matrices, which history now holds; frames and topology are shared.
	ModelEdit pose;
	pose.kind = ModelEditKind::TransformSkeletalPose;
	pose.skeletal.clip = outside;
	pose.skeletal.first = 0;
	pose.skeletal.last = 0;
	pose.skeletal.joint = QStringLiteral("joint10");
	pose.skeletal.rotation = {0, 0, 5};
	ok &= expect(document.edit(pose, &error), "a pose edit on a clip outside the working set applies", error);
	const auto poseBytes = document.historyBytes();
	std::cout << "skeletal pose: " << qPrintable(mib(poseBytes)) << std::endl;
	ok &= expect(poseBytes >= 64 * qint64(sizeof(ModelJointMatrix)) && poseBytes <= 256 * 1024,
				 "a pose edit holds the clip matrices it changed and nothing it shares", QString::number(poseBytes));
	// A curve key edit holds the changed channel.
	ModelEdit curve;
	curve.kind = ModelEditKind::EditAnimationCurves;
	curve.curves.action = ModelCurveAction::SetKey;
	curve.curves.clip = 0;
	curve.curves.joint = mesh.skeleton.clips[0].curves[3].joint;
	curve.curves.channel = 5;
	curve.curves.frame = 4;
	curve.curves.key = sampleModelCurveKey(mesh.skeleton.clips[0].curves[3].channels[5], 4);
	curve.curves.key.value += 2.5;
	const auto beforeCurve = document.historyBytes();
	ok &= expect(document.edit(curve, &error) && document.historyBytes() > beforeCurve, "a curve key edit holds the curves it changed", error);
	// Weight edits change influences and every baked frame of the moved
	// vertices: history holds them, within the limit, trimming as it fills.
	const auto influenceBytes = mesh.surfaces[0].skinning.influences.size() * qint64(sizeof(ModelJointInfluence));
	for (int i = 0; i < 24; ++i)
	{
		ModelEdit weight;
		weight.kind = ModelEditKind::SetJointWeight;
		weight.rig.joint = QStringLiteral("joint%1").arg(8 + i % 40);
		weight.rig.weight = 0.25 + 0.01 * i;
		weight.selection.vertices = {i * 256, i * 256 + 1, i * 256 + 2};
		const auto history = document.historyBytes();
		if (!expect(document.edit(weight, &error), "a weight edit applies", error))
			break;
		ok &= expect(document.historyBytes() <= modelDocumentHistoryLimit(), "weight edits stay within the history limit");
		if (i == 0)
			ok &= expect(document.historyBytes() >= history + influenceBytes, "a weight edit holds the influences it replaced",
						 QStringLiteral("%1 -> %2").arg(history).arg(document.historyBytes()));
	}
	std::cout << "weights: " << document.undoSteps() << " steps hold " << qPrintable(mib(document.historyBytes())) << ", "
			  << document.discardedHistorySteps() << " discarded" << std::endl;
	ok &= expect(document.undoSteps() >= 1 && document.discardedHistorySteps() > 0, "weight edits trim old steps once history is full");
	return ok;
}

bool settings(const QDir &dir)
{
	bool ok = true;
	const auto path = dir.filePath(QStringLiteral("settings.ini"));
	{
		StudioSettings settings(path);
		ok &= expect(settings.modelHistoryBudgetMiB() == 64, "the history limit defaults to 64 MiB");
		settings.setModelHistoryBudgetMiB(256);
		settings.sync();
	}
	StudioSettings reread(path, StudioSettings::AccessMode::ReadOnly);
	ok &= expect(reread.modelHistoryBudgetMiB() == 256, "model/historyBudgetMiB round-trips");
	StudioSettings bounded(path);
	bounded.setModelHistoryBudgetMiB(1);
	ok &= expect(bounded.modelHistoryBudgetMiB() == 16, "the history limit is at least 16 MiB");
	bounded.setModelHistoryBudgetMiB(100000);
	ok &= expect(bounded.modelHistoryBudgetMiB() == 4096, "the history limit is at most 4096 MiB");
	return ok;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("history-budget-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	expect(modelDocumentHistoryLimit() == modelDocumentDefaultHistoryBytes && modelDocumentDefaultHistoryBytes == 64LL * 1024 * 1024,
		   "the documented 64 MiB history limit is the default");
	sharing();
	trimming();
	walking();
	skeletal();
	settings(QDir(temporary.path()));
	if (failures == 0)
		std::cout << "model history budget smoke passed" << std::endl;
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
