#include "core/level_surface.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { std::cerr << message << ": " << error.toStdString() << '\n'; }
	return value;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true; QString error;
	const QHash<QString, QSize> sizes{{QStringLiteral("studio/grid"), QSize(128, 64)}};
	for (const auto& dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		LevelMapDocument source;
		if (!expect(loadLevelMapBytes({"batch.map", {}, "idtech3"}, tests::surfaceFixture(QString::fromLatin1(dialect)), &source, &error), "fixture", error)) { return 1; }
		const QVector<LevelSurfaceFace> faces{{0, 0}, {0, 3}};
		const QVector<LevelSurfaceRequest> steps{{LevelSurfaceOperation::Shift, 8, -3}, {LevelSurfaceOperation::Rotate, 1, 1, 15},
			{LevelSurfaceOperation::Scale, 1.1, 1}, {LevelSurfaceOperation::Shift, -2, 7}};
		auto sequential = source, batch = source;
		for (const auto& step : steps) {
			LevelSurfaceEditPlan plan;
			ok &= expect(prepareLevelSurfaceEdit(sequential, faces, step, sizes, &plan, &error) && commitLevelSurfaceEdit(&sequential, plan, &error), "sequential reference", error);
		}
		LevelSurfaceEditPlan plan;
		ok &= expect(prepareLevelSurfaceEdits(batch, faces, steps, sizes, &plan, &error) && commitLevelSurfaceEdit(&batch, plan, &error), "batch", error);
		const auto result = serializeLevelMap(batch).bytes;
		ok &= expect(result == serializeLevelMap(sequential).bytes && batch.undoStack.size() == 1 && plan.faceCount() == 2,
			"ordered steps match existing single operations with one undo");
		ok &= expect(undoLevelMapEdit(&batch, &error) && serializeLevelMap(batch).bytes == serializeLevelMap(source).bytes,
			"exact source undo including comments and untargeted faces", error);
		ok &= expect(redoLevelMapEdit(&batch, &error) && serializeLevelMap(batch).bytes == result, "exact redo", error);
		auto invalid = steps; invalid << LevelSurfaceRequest{LevelSurfaceOperation::Scale, 0, 1};
		ok &= expect(!prepareLevelSurfaceEdits(source, faces, invalid, sizes, &plan, &error) && !plan.ready(), "invalid final step rejects complete batch");
		ok &= expect(!prepareLevelSurfaceEdits(source, faces, {}, sizes, &plan, &error), "empty batch rejected");
		ok &= expect(!prepareLevelSurfaceEdits(source, faces, QVector<LevelSurfaceRequest>(65, steps.first()), sizes, &plan, &error), "queue bound enforced in core");
		int checks = 0;
		ok &= expect(!prepareLevelSurfaceEdits(source, faces, steps, sizes, &plan, &error, [&] { return ++checks > 3; }) && !plan.ready(), "cancellation within face batch");
		// The existing writer rounds to six decimals. Check an inverse pair
		// after a real edit has made all coefficients representable at that
		// precision (the raw matrix fixture intentionally has seven decimals).
		auto inverse = sequential;
		const auto inverseRevision = inverse.revision;
		const auto inverseHistory = inverse.undoStack.size();
		ok &= expect(prepareLevelSurfaceEdits(inverse, faces, {{LevelSurfaceOperation::Shift, 8, -4}, {LevelSurfaceOperation::Shift, -8, 4}}, sizes, &plan, &error)
			&& commitLevelSurfaceEdit(&inverse, plan, &error) && inverse.undoStack.size() == inverseHistory && inverse.revision == inverseRevision,
			"inverse burst leaves no edit history", error);
		QString layer;
		ok &= createLevelSceneNode(&source, LevelSceneNodeKind::Layer, "Locked", {}, &layer, &error);
		ok &= assignLevelSceneObjects(&source, layer, {"brush:0"}, &error);
		ok &= setLevelSceneLocked(&source, layer, true, &error);
		const auto locked = serializeLevelMap(source).bytes;
		const auto revision = source.revision;
		ok &= expect(prepareLevelSurfaceEdits(source, faces, steps, sizes, &plan, &error) && !commitLevelSurfaceEdit(&source, plan, &error)
			&& source.revision == revision && serializeLevelMap(source).bytes == locked, "lock rejects entire prepared batch", error);
	}
	return ok ? 0 : 1;
}
