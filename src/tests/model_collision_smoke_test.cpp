#include "core/level_brush.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/model_collision.h"
#include "core/model_fingerprint.h"
#include "core/model_recovery.h"
#include "tests/model_assembly_test_helpers.h"
#include "tests/model_mdl_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << " " << error.toStdString() << '\n';
	}
	return value;
}
QByteArray source(const ModelMesh &mesh) { return QJsonDocument(editableModelJson(mesh)).toJson(QJsonDocument::Compact); }
double volume(const LevelMapBrush &brush)
{
	const auto geometry = solveBrushGeometry(brush.faces);
	double value = 0;
	for (const auto &face : geometry.faces)
	{
		for (int i = 1; i + 1 < face.points.size(); ++i)
		{
			const auto a = face.points[0], b = face.points[i], c = face.points[i + 1];
			value += a.x * (b.y * c.z - b.z * c.y) + a.y * (b.z * c.x - b.x * c.z) + a.z * (b.x * c.y - b.y * c.x);
		}
	}
	return std::abs(value / 6);
}
// Independent double-precision corner oracle. Inverse Euler rotations reverse
// both the signs and the order; this does not call the production transform.
using Point = std::array<double, 3>;
Point turned(Point p, ModelVec3 degrees, bool inverse = false)
{
	const double angles[]{degrees.x, degrees.y, degrees.z};
	for (int step = 0; step < 3; ++step)
	{
		const int axis = inverse ? 2 - step : step, a = (axis + 1) % 3, b = (axis + 2) % 3;
		const auto angle = angles[axis] * std::numbers::pi / 180 * (inverse ? -1 : 1);
		const auto u = p[a], v = p[b];
		p[a] = u * std::cos(angle) - v * std::sin(angle);
		p[b] = u * std::sin(angle) + v * std::cos(angle);
	}
	return p;
}
bool transforms()
{
	bool ok = true;
	QString error;
	const ModelCollisionBox box{"body", {24, -13, 7}, {16, 28, 40}, {17, 41, -63}};
	for (int index = 0; index < 64; ++index)
	{
		auto original = box;
		if (index < 4)
		{
			original.rotation = {};
		}
		ModelTransform edit{{8, -3, 5},
							index < 4 ? ModelVec3{23, index % 2 ? -90.f : 90.f, 47}
									  : ModelVec3{float(index * 11 - 200), float(index * 13 - 300), float(index * 7 - 160)},
							{index % 2 ? -2.f : 2.f, .5f, 1.25f},
							index % 3 ? ModelVec3{-4, 10, 3} : original.centre, {}};
		ModelCollisionBox result;
		ok &= expect(transformModelCollisionBox(original, edit, &result, &error), "composed transform accepts finite box", error);
		const auto actual = modelCollisionCorners(result);
		for (int corner = 0; corner < 8; ++corner)
		{
			auto p = turned({original.size.x * (corner & 1 ? .5 : -.5), original.size.y * (corner & 2 ? .5 : -.5),
							 original.size.z * (corner & 4 ? .5 : -.5)},
							original.rotation);
			p = turned(
				{p[0] + original.centre.x - edit.pivot.x, p[1] + original.centre.y - edit.pivot.y, p[2] + original.centre.z - edit.pivot.z},
				original.rotation, true);
			p = turned({p[0] * edit.scale.x, p[1] * edit.scale.y, p[2] * edit.scale.z}, original.rotation);
			p = turned(p, edit.rotation);
			p = {p[0] + edit.pivot.x + edit.translation.x, p[1] + edit.pivot.y + edit.translation.y,
				 p[2] + edit.pivot.z + edit.translation.z};
			ok &= expect(std::any_of(actual.begin(), actual.end(),
									 [&](ModelVec3 q) { return std::hypot(p[0] - q.x, p[1] - q.y, p[2] - q.z) < .0002; }),
						 "local nonuniform scaling, mirrors and composed rotations match independent corner oracle, including gimbal lock");
		}
	}
	ModelDocument document;
	auto mesh = tests::assemblyModel();
	mesh.collisionBoxes = {box};
	ok &= expect(document.setMesh(mesh, &error), "transform document fixture", error);
	document.setSelection({0, {}, {}, {}, {}, "body"});
	const auto before = document.revisionFingerprint();
	ModelEdit edit;
	edit.kind = ModelEditKind::TransformCollisionBox;
	edit.selection = document.selection();
	edit.pivotMode = ModelTransformPivot::SelectionCentre;
	edit.translation = {1.4f, 0, 0};
	edit.translationGrid = 1;
	edit.rotation = {0, 0, 43};
	edit.rotationGrid = 15;
	edit.scale = {1.37f, 1, 1};
	edit.scaleGrid = .25;
	ok &= expect(document.edit(edit, &error), "document transform snaps and commits", error);
	const auto after = document.revisionFingerprint();
	const auto &changed = document.mesh().collisionBoxes[0];
	ok &= expect(changed.centre.x == 25 && changed.centre.y == -13 && changed.size.x == 20 && changed.size.y == 28 &&
					 std::abs(changed.rotation.z + 18) < .0001,
				 "world deltas and local sizes snap around selected box centre");
	auto renderOnly = document.mesh();
	renderOnly.collisionBoxes = mesh.collisionBoxes;
	ok &= expect(source(renderOnly) == source(mesh), "box transform preserves render surfaces, all poses and tags");
	ok &= expect(document.undo() && document.revisionFingerprint() == before && document.selection().collision == "body" &&
					 document.redo() && document.revisionFingerprint() == after,
				 "one undo/redo retains box selection and transform");
	for (int bad = 0; bad < 7; ++bad)
	{
		auto invalid = edit;
		if (bad == 0)
		{
			invalid.scale.x = 0;
		}
		if (bad == 1)
		{
			invalid.scaleGrid = 0;
			invalid.scale.x = .01f;
		}
		if (bad == 2)
		{
			invalid.translation.x = 32768;
		}
		if (bad == 3)
		{
			invalid.pivot.x = std::numeric_limits<float>::infinity();
		}
		if (bad == 4)
		{
			invalid.frame = 0;
		}
		if (bad == 5)
		{
			invalid.selection.collision = "missing";
		}
		if (bad == 6)
		{
			invalid.pivotMode = ModelTransformPivot(100);
		}
		ok &= expect(!document.edit(invalid, &error) && !error.isEmpty() && document.revisionFingerprint() == after,
					 "invalid transform fails atomically with diagnostics");
	}
	ModelCollisionBox sentinel{"sentinel", {}, {}, {}};
	ok &= expect(!transformModelCollisionBox(box, {{}, {}, {0, 1, 1}, {}, {}}, &sentinel, &error) && sentinel.name == "sentinel",
				 "pure preview transform does not publish invalid output");
	ModelCollisionBox translated;
	auto fractional = box;
	fractional.centre = {.123f, -.456f, .789f};
	ok &= expect(transformModelCollisionBox(fractional, {{}, {}, {1, 1, 1}, {1000000, -1000000, 1000000}, {}}, &translated, &error) &&
					 translated.centre.x == fractional.centre.x && translated.centre.y == fractional.centre.y &&
					 translated.centre.z == fractional.centre.z,
				 "neutral transform preserves fractional centres even with a distant custom pivot");
	ok &= expect(transformModelCollisionBox(box, {{2, 4, -1}, {}, {1, 1, 1}, {}, {}}, &translated, &error) &&
					 translated.rotation.x == box.rotation.x && translated.rotation.y == box.rotation.y &&
					 translated.rotation.z == box.rotation.z,
				 "moving retains authored Euler values exactly");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("model-collision-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	QString error;
	const auto original = tests::assemblyModel();
	ModelDocument document;
	bool ok = transforms() && expect(document.setMesh(original, &error), "fixture", error);
	const auto initial = document.revisionFingerprint();
	ModelEdit edit;
	edit.kind = ModelEditKind::AddCollisionBox;
	edit.collisionBox = {"body", {8, -4, 6}, {16, 24, 32}, {15, 25, 45}};
	ok &= expect(document.edit(edit, &error) && document.selection().collision == "body" && document.mesh().collisionBoxes.size() == 1 &&
					 document.revisionFingerprint() != initial && document.isModified(),
				 "collision changes participate in document identity and selection", error);
	auto renderOnly = document.mesh();
	renderOnly.collisionBoxes.clear();
	ok &= expect(source(renderOnly) == source(original), "collision authoring leaves all render geometry, tags and animation unchanged");
	const auto added = document.revisionFingerprint();
	ok &= expect(document.undo() && document.mesh().collisionBoxes.isEmpty() && document.selection().collision.isEmpty() &&
					 document.redo() && document.revisionFingerprint() == added && document.selection().collision == "body",
				 "collision undo redo restores selection and state");
	edit.kind = ModelEditKind::UpdateCollisionBox;
	edit.selection = document.selection();
	edit.collisionBox.name = "hull";
	edit.collisionBox.centre.x = 9;
	ok &= expect(document.edit(edit, &error) && !findModelCollisionBox(document.mesh(), "body") && document.selection().collision == "hull",
				 "rename and geometry update are one edit", error);
	const auto renamed = document.revisionFingerprint();
	for (int kind = 0; kind < 7; ++kind)
	{
		auto bad = edit;
		bad.selection = document.selection();
		if (kind == 0)
		{
			bad.collisionBox.name = " bad ";
		}
		if (kind == 1)
		{
			bad.collisionBox.size.x = .5;
		}
		if (kind == 2)
		{
			bad.collisionBox.centre.x = 32768;
		}
		if (kind == 3)
		{
			bad.collisionBox.rotation.y = std::numeric_limits<float>::infinity();
		}
		if (kind == 4)
		{
			bad.frame = 0;
		}
		if (kind == 5)
		{
			bad.selection.vertices << 0;
		}
		if (kind == 6)
		{
			bad.kind = ModelEditKind::Transform;
		}
		ok &= expect(!document.edit(bad, &error) && document.revisionFingerprint() == renamed,
					 "malformed, mixed-selection and mis-scoped edits are atomic");
	}
	edit.kind = ModelEditKind::DuplicateCollisionBox;
	edit.selection = document.selection();
	edit.collisionBox.name = "copy";
	ok &=
		expect(document.edit(edit, &error) && document.mesh().collisionBoxes.size() == 2 && document.mesh().collisionBoxes[1].centre.x == 9,
			   "duplicate preserves box geometry", error);
	edit = {};
	edit.kind = ModelEditKind::FitCollisionBox;
	edit.collisionBox.name = "fitted";
	ok &= expect(document.edit(edit, &error), "fit all animation frames", error);
	const auto fitted = document.mesh().collisionBoxes.last();
	ok &= expect(fitted.centre.x == 2 && fitted.centre.y == 2 && fitted.centre.z == .5f && fitted.size.x == 4 && fitted.size.z == 1,
				 "fit spans all poses and clamps planar dimensions");
	edit.collisionBox.name = "vertex";
	edit.selection.vertices = {2};
	edit.frame = 1;
	ok &= expect(document.edit(edit, &error) && document.mesh().collisionBoxes.last().centre.y == 4 &&
					 document.mesh().collisionBoxes.last().centre.z == 1 && document.mesh().collisionBoxes.last().size.x == 1,
				 "one selected vertex in one pose fits a usable unit box", error);
	const auto beforeCancel = document.revisionFingerprint();
	edit.collisionBox.name = "cancelled";
	bool stop = false;
	ModelWorkControl cancel{[&] { return stop; },
							[&](ModelWorkPhase phase, qint64, qint64)
							{
								if (phase == ModelWorkPhase::Editing)
								{
									stop = true;
								}
							}};
	ok &= expect(!document.edit(edit, &error, cancel) && document.revisionFingerprint() == beforeCancel,
				 "cancelled fit keeps whole document and selection");
	ok &= expect(document.save(path("source.mesh.json"), false, &error), "save collision source", error);
	ModelDocument reopened;
	ok &= expect(reopened.load(path("source.mesh.json"), &error) && reopened.revisionFingerprint() == document.revisionFingerprint(),
				 "source reopen preserves collision metadata", error);
	const auto json = editableModelJson(document.mesh());
	ok &= expect(json.value("version").toInt() == 5, "collision source has explicit version 5");
	auto maximum = document.mesh();
	while (maximum.collisionBoxes.size() < modelCollisionBoxLimit)
	{
		ModelCollisionBox added;
		added.name = QStringLiteral("limit_%1").arg(maximum.collisionBoxes.size());
		maximum.collisionBoxes << added;
	}
	ModelDocument bounded;
	ok &= expect(bounded.setMesh(maximum, &error), "64 collision boxes are valid", error);
	const auto boundedBefore = bounded.revisionFingerprint();
	ModelEdit overLimit;
	overLimit.kind = ModelEditKind::AddCollisionBox;
	overLimit.collisionBox.name = "too_many";
	ok &= expect(!bounded.edit(overLimit, &error) && bounded.revisionFingerprint() == boundedBefore && !bounded.canUndo(),
				 "box limit refuses without allocating history or changing source");
	for (int kind = 0; kind < 6; ++kind)
	{
		auto malformed = json;
		if (kind == 0)
		{
			malformed.remove("collisionBoxes");
		}
		if (kind == 1)
		{
			malformed["version"] = 4;
		}
		if (kind == 2)
		{
			malformed["collisionBoxes"] = "not an array";
		}
		if (kind == 3 || kind == 4)
		{
			auto array = malformed["collisionBoxes"].toArray();
			auto box = array[0].toObject();
			if (kind == 3)
			{
				box["unsupported"] = true;
			}
			else
			{
				box["size"] = QJsonArray{0, 1, 1};
			}
			array[0] = box;
			malformed["collisionBoxes"] = array;
		}
		if (kind == 5)
		{
			QJsonArray array;
			for (int i = 0; i < 65; ++i)
			{
				array << json["collisionBoxes"].toArray()[0];
			}
			malformed["collisionBoxes"] = array;
		}
		auto sentinel = original;
		ok &= expect(!parseEditableModel(QJsonDocument(malformed).toJson(), &sentinel, &error) && source(sentinel) == source(original),
					 "invalid collision JSON cannot partially replace mesh");
	}
	ModelRecoverySnapshot recovery;
	recovery.mesh = document.mesh();
	recovery.selection = document.selection();
	recovery.frame = 1;
	recovery.title = "Collision";
	const auto recoveryPath = writeModelRecovery(recovery, path("recovery"), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ModelRecoverySnapshot restored;
	ok &= expect(!recoveryPath.isEmpty() && inspectModelRecovery(recoveryPath, &restored).isValid() &&
					 restored.selection == recovery.selection && source(restored.mesh) == source(recovery.mesh),
				 "recovery retains selected collider and complete source", error);
	for (const auto &format : {QStringLiteral("obj"), QStringLiteral("md2"), QStringLiteral("md3")})
	{
		auto native = document.mesh();
		if (format == "md2")
		{
			native.tags.clear();
			native.surfaces[0].skinPaths = {QStringLiteral("textures/assembly.pcx")};
			updateEditableModelMetadata(&native);
		}
		ModelExportReport report;
		ok &= expect(!exportEditableModel(native, format, 0, &error, {}, &report).isEmpty() &&
						 report.notes.contains(modelCollisionOmissionNote()),
					 "native and OBJ exports disclose collision omission", error);
	}
	ModelMesh mdl;
	ok &= expect(importEditableModel("fixture.mdl", tests::groupedMdlFixture().bytes, &mdl, &error), "native MDL fixture", error);
	mdl.collisionBoxes = document.mesh().collisionBoxes;
	ModelMesh parsedMdl;
	ok &= expect(parseEditableModel(source(mdl), &parsedMdl, &error) && source(parsedMdl) == source(mdl),
				 "schema 5 preserves native MDL settings alongside collision", error);
	ModelExportReport mdlReport;
	ok &= expect(!exportModelMdl(mdl, &error, {}, &mdlReport).isEmpty() && mdlReport.notes.contains(modelCollisionOmissionNote()),
				 "direct native MDL export reports omitted collision", error);
	const auto assembly = tests::assemblyRecipe(path("source.mesh.json"));
	ModelAssemblyResolved resolved;
	ModelAssemblyPose pose;
	ok &= expect(resolveModelAssembly(assembly, {temporary.path(), {}, {}}, &resolved, &error) &&
					 sampleModelAssembly(assembly, resolved, 0, &pose, &error) && pose.mesh.collisionBoxes.isEmpty() &&
					 pose.notes.join(' ').contains("Input collision boxes remain"),
				 "assembly bake discloses omission of input collision", error);
	for (const auto &target : {QStringLiteral("quake"), QStringLiteral("quake2"), QStringLiteral("quake3")})
	{
		ModelCollisionExport request{target, target == "quake3" ? "common/playerclip" : QString(), {40, 50, 60}};
		ModelCollisionMap exported, again;
		ok &= expect(exportModelCollisionMap(document.mesh(), request, &exported, &error) &&
						 exportModelCollisionMap(document.mesh(), request, &again, &error) && exported.bytes == again.bytes,
					 "deterministic clip map export", error);
		LevelMapDocument map;
		ok &= expect(
			loadLevelMapBytes({path("collision.map"), {}, target == "quake3" ? "idtech3" : "idtech2"}, exported.bytes, &map, &error) &&
				map.brushes.size() == 4,
			"shared map parser accepts all collision boxes", error);
		for (int i = 0; i < map.brushes.size(); ++i)
		{
			const auto size = document.mesh().collisionBoxes[i].size;
			ok &= expect(map.brushes[i].faces.size() == 6 && map.brushes[i].boundsSolved &&
							 std::abs(volume(map.brushes[i]) - double(size.x) * size.y * size.z) < .1,
						 "rotated clip brush has six closed faces and expected independent volume");
			for (const auto &face : map.brushes[i].faces)
			{
				ok &= expect(face.contentFlags == (target == "quake2" ? 65536 : 0) && face.textureName == exported.material,
							 "target-specific clip contents and material");
			}
		}
		const auto before = serializeLevelMap(map).bytes;
		auto placed = prepareModelCollisionPlacement(document.mesh(), request, map);
		ok &= expect(placed.succeeded && placed.document.brushes.size() == 8 && map.brushes.size() == 4 &&
						 undoLevelMapEdit(&placed.document, &error) && serializeLevelMap(placed.document).bytes == before &&
						 redoLevelMapEdit(&placed.document, &error) && placed.document.brushes.size() == 8,
					 "placement is isolated and one reversible level edit", placed.error + error);
		const auto cancelled = prepareModelCollisionPlacement(document.mesh(), request, map, {[] { return true; }, {}});
		ok &= expect(!cancelled.succeeded && cancelled.cancelled && serializeLevelMap(map).bytes == before,
					 "placement cancellation protects map");
		bool cancelPlacement = false;
		const auto lateCancel =
			prepareModelCollisionPlacement(document.mesh(), request, map,
										   {[&] { return cancelPlacement; }, [&](ModelWorkPhase phase, qint64 completed, qint64)
											{ cancelPlacement = phase == ModelWorkPhase::Editing && completed > 0; }});
		ok &= expect(cancelPlacement && lateCancel.cancelled && !lateCancel.succeeded && serializeLevelMap(map).bytes == before,
					 "cancellation after export and placement progress cannot publish a partial candidate");
		QString layer;
		ok &= expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, "Prop collision", {}, &layer, &error),
					 "create destination layer", error);
		map.activeSceneNode = layer;
		auto grouped = prepareModelCollisionPlacement(document.mesh(), request, map);
		ok &= expect(grouped.succeeded && grouped.document.selection.size() == 4, "placement selects inserted volumes", grouped.error);
		for (const auto &selection : grouped.document.selection)
		{
			ok &= expect(levelSceneMembership(grouped.document.scene, levelMapSelectionRefId(selection)) == layer,
						 "collision enters active scene layer");
		}
		ok &= expect(setLevelSceneLocked(&map, layer, true, &error), "lock collision destination", error);
		const auto lockedBefore = serializeLevelMap(map).bytes;
		const auto locked = prepareModelCollisionPlacement(document.mesh(), request, map);
		ok &= expect(!locked.succeeded && !locked.error.isEmpty() && serializeLevelMap(map).bytes == lockedBefore,
					 "locked destination refuses collision placement without changing map or scene");
	}
	ModelCollisionMap sentinel;
	sentinel.bytes = "unchanged";
	bool cancelExport = false;
	ok &= expect(!exportModelCollisionMap(document.mesh(), {"quake", {}, {}}, &sentinel, &error,
										  {[&] { return cancelExport; }, [&](ModelWorkPhase phase, qint64 completed, qint64)
										   { cancelExport = phase == ModelWorkPhase::Serializing && completed > 0; }}) &&
					 cancelExport && sentinel.bytes == "unchanged",
				 "cancelled brush serialization preserves caller output");
	for (const auto &request : QVector<ModelCollisionExport>{{"doom", {}, {}},
															 {"quake3", {}, {}},
															 {"quake3", "textures/common/clip", {}},
															 {"quake3", "common/../clip", {}},
															 {"quake2", "common/clip", {}},
															 {"quake", {}, {32768, 0, 0}}})
	{
		ok &= expect(!exportModelCollisionMap(document.mesh(), request, &sentinel, &error) && sentinel.bytes == "unchanged",
					 "bad collision target or placement cannot publish bytes");
	}
	edit = {};
	edit.kind = ModelEditKind::DeleteCollisionBox;
	edit.selection = document.selection();
	ok &= expect(document.edit(edit, &error) && document.selection().collision.isEmpty() && document.undo() &&
					 document.selection().collision == "vertex",
				 "delete undo restores selected box", error);
	std::cout << (ok ? "Collision document, recovery, format and placement checks passed.\n" : "Collision checks failed.\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
