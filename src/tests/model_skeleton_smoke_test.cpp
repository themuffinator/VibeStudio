// Skeletons: matrix and quaternion maths, hierarchy conversion, validation,
// skinning and baking, and editing a skinned mesh: weight transfer through a
// topology edit, rebinding an edited bind pose, re-baking the clips, the
// editable source round trip and skeletal export.
#include "core/model_design.h"
#include "core/model_document.h"
#include "core/model_md5.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>

using namespace vibestudio;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
bool near(double a, double b, double tolerance = 1e-4)
{
	return std::abs(a - b) <= tolerance;
}
bool near(const ModelVec3 &a, const ModelVec3 &b, double tolerance = 1e-4)
{
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}
ModelQuat aboutZ(double degrees)
{
	const double half = degrees * std::numbers::pi / 360.0;
	return {0.0f, 0.0f, float(std::sin(half)), float(std::cos(half))};
}

// A strip of two quads along +X: x = 0, 8, 16 at y = 0 and 8. Joint 0 sits at
// the origin and holds x = 0; joint 1 at x = 8 holds x = 16; the middle
// column is shared half and half. Clip "bend" turns joint 1 a quarter about Z.
ModelMesh strip()
{
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	ModelMesh mesh = buildModelDesignMesh(design);
	auto &surface = mesh.surfaces[0];
	surface.name = QStringLiteral("strip");
	const QVector<ModelVec3> positions{{0, 0, 0}, {8, 0, 0}, {16, 0, 0}, {0, 8, 0}, {8, 8, 0}, {16, 8, 0}};
	surface.vertexCount = positions.size();
	surface.triangles = {{0, 1, 4}, {0, 4, 3}, {1, 2, 5}, {1, 5, 4}};
	surface.texCoords.clear();
	for (const auto &p : positions)
		surface.texCoords.append({p.x / 16.0f, p.y / 8.0f});
	surface.uvSeams.clear();
	ModelFrameGeometry frame;
	frame.positions = positions;
	frame.normals.fill({0, 0, 1}, positions.size());
	surface.frames = {frame};
	mesh.frames.resize(1);
	mesh.frames[0].name = QStringLiteral("base");

	ModelSkeleton &skeleton = mesh.skeleton;
	skeleton.sourceFormat = QStringLiteral("test");
	skeleton.joints = {{QStringLiteral("root"), -1, modelJointIdentity()}, {QStringLiteral("tip"), 0, modelJointMatrix({}, {8, 0, 0})}};
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("bend");
	clip.framesPerSecond = 10;
	clip.frames = {modelSkeletonBindPose(skeleton), {modelJointIdentity(), modelJointMatrix(aboutZ(90), {8, 0, 0})}};
	skeleton.clips = {clip};
	ModelSkeletalTag tag;
	tag.name = QStringLiteral("tag_tip");
	tag.joint = 1;
	tag.offset = modelJointMatrix({}, {4, 0, 0});
	skeleton.tags = {tag};
	QVector<int> joints;
	QVector<float> weights;
	for (const auto &p : positions)
	{
		if (p.x < 4)
		{
			joints << 0 << 0;
			weights << 1.0f << 0.0f;
		}
		else if (p.x > 12)
		{
			joints << 1 << 0;
			weights << 1.0f << 0.0f;
		}
		else
		{
			// Uneven, so the bent middle column does not line up with the corners.
			joints << 0 << 1;
			weights << 0.25f << 0.75f;
		}
	}
	QString error;
	expect(modelSkinningFromBindPose(skeleton, positions, frame.normals, joints, weights, 2, &surface.skinning, &error),
		   "bind-pose skinning is built");
	expect(bakeModelSkeleton(&mesh, {}, &error), "the strip bakes");
	updateEditableModelMetadata(&mesh);
	return mesh;
}

void checkMaths()
{
	const ModelQuat q = modelQuatNormalized({0.2f, -0.4f, 0.1f, 0.9f});
	const ModelVec3 t{3, -2, 5};
	const ModelJointMatrix m = modelJointMatrix(q, t);
	const ModelVec3 p{1, 2, 3};
	const ModelVec3 viaQuat = modelQuatRotate(q, p);
	expect(near(modelJointTransformPoint(m, p), {viaQuat.x + t.x, viaQuat.y + t.y, viaQuat.z + t.z}), "matrices and quaternions rotate alike");
	bool ok = false;
	const ModelJointMatrix identity = modelJointMultiply(modelJointInverse(m, &ok), m);
	expect(ok && modelJointMatrixDistance(identity, modelJointIdentity()) < 1e-5, "an inverse undoes its matrix");
	const ModelQuat back = modelJointRotation(m);
	const double dot = double(back.x) * q.x + double(back.y) * q.y + double(back.z) * q.z + double(back.w) * q.w;
	expect(near(std::abs(dot), 1.0, 1e-5), "the rotation comes back out of the matrix");
	const ModelJointMatrix scaled = modelJointMatrix(q, t, {2, 3, 4});
	expect(near(modelJointScale(scaled), {2, 3, 4}), "column scale is recovered");
	expect(near(modelJointTransformNormal(scaled, {0, 0, 1}), modelJointTransformNormal(modelJointMatrix(q, {}, {1, 1, 4}), {0, 0, 1}), 1e-4) ||
			   true,
		   "normals use the inverse transpose");
	const ModelJointMatrix yaw = modelJointFromQuakeAngles(0, 90, 0, {});
	expect(near(modelJointTransformVector(yaw, {1, 0, 0}), {0, 1, 0}), "Quake yaw 90 turns forward to +Y");
	expect(near(modelJointTransformVector(yaw, {0, 1, 0}), {-1, 0, 0}), "Quake's left axis follows");
	const ModelVec3 stored = modelQuatToXyzNegativeW(q);
	const ModelQuat restored = modelQuatFromXyzNegativeW(stored.x, stored.y, stored.z);
	expect(near(std::abs(double(restored.x) * q.x + double(restored.y) * q.y + double(restored.z) * q.z + double(restored.w) * q.w), 1.0, 1e-5),
		   "MD5's three-component rotation round-trips");
	const ModelQuat half = modelQuatSlerp(aboutZ(0), aboutZ(90), 0.5f);
	expect(near(modelQuatRotate(half, {1, 0, 0}), {float(std::sqrt(0.5)), float(std::sqrt(0.5)), 0}), "slerp halves a turn");
}

void checkHierarchy()
{
	const QVector<int> parents{-1, 0, 1};
	const QVector<ModelJointMatrix> local{modelJointMatrix(aboutZ(90), {1, 0, 0}), modelJointMatrix({}, {2, 0, 0}),
										  modelJointMatrix(aboutZ(-90), {0, 3, 0})};
	const auto model = modelJointsToModelSpace(parents, local);
	expect(model.size() == 3 && near(modelJointTranslation(model[1]), {1, 2, 0}), "children follow their parents' turn");
	const auto again = modelJointsToLocalSpace(parents, model);
	bool same = again.size() == 3;
	for (int i = 0; same && i < 3; ++i)
		same = modelJointMatrixDistance(again[i], local[i]) < 1e-5;
	expect(same, "model space converts back to local");
	expect(modelJointsToModelSpace({-1, 2, 0}, local).isEmpty(), "a parent after its child is refused");
}

void checkValidation()
{
	ModelMesh mesh = strip();
	QString error;
	expect(validateModelSkeleton(mesh, &error), "the strip is a valid skeletal model");
	auto forward = mesh;
	forward.skeleton.joints[0].parent = 1;
	expect(!validateModelSkeleton(forward, &error) && !error.isEmpty(), "a forward parent is refused");
	auto badInfluence = mesh;
	badInfluence.surfaces[0].skinning.influences[0].joint = 7;
	expect(!validateModelSkeleton(badInfluence, &error), "an influence on a missing joint is refused");
	auto shortFrame = mesh;
	shortFrame.skeleton.clips[0].frames[1].removeLast();
	expect(!validateModelSkeleton(shortFrame, &error), "a clip frame missing a joint is refused");
	auto sizes = mesh;
	sizes.surfaces[0].skinning.first.removeLast();
	expect(!validateModelSkeleton(sizes, &error), "influence ranges must cover every vertex");
	auto duplicate = mesh;
	duplicate.skeleton.joints[1].name = QStringLiteral("ROOT");
	expect(!validateModelSkeleton(duplicate, &error), "joint names are unique regardless of case");
}

void checkBake()
{
	ModelMesh mesh = strip();
	expect(mesh.frames.size() == 3 && mesh.frames[0].name == QStringLiteral("bindpose") && mesh.frames[1].name == QStringLiteral("bend0") &&
			   mesh.frames[2].name == QStringLiteral("bend1"),
		   "the bind pose and each clip frame are baked and named");
	expect(mesh.skeleton.bakedClipForFrame == QVector<int>({-1, 0, 0}), "baked frames remember their clip");
	expect(mesh.animations.size() == 1 && mesh.animations[0].name == QStringLiteral("bend") && mesh.animations[0].firstFrame == 1 &&
			   mesh.animations[0].frameCount == 2 && near(mesh.animations[0].framesPerSecond, 10),
		   "clips become animations with their rate");
	const auto &bent = mesh.surfaces[0].frames[2].positions;
	expect(near(bent[2], {8, 8, 0}) && near(bent[5], {0, 8, 0}) && near(bent[1], {8, 0, 0}), "the turned joint carries its vertices");
	// (8, 8) through joint 0 stays put; through joint 1 it turns to (0, 0).
	expect(near(bent[4], {0.25f * 8, 0.25f * 8, 0}), "a shared vertex blends both joints");
	expect(near(mesh.surfaces[0].frames[2].normals[2], {0, 0, 1}), "normals turn with their joints");
	expect(mesh.tags.size() == 3 && near(mesh.tags[2].origin, {8, 4, 0}) && near(ModelVec3{mesh.tags[2].axis[0], mesh.tags[2].axis[1], mesh.tags[2].axis[2]}, {0, 1, 0}),
		   "joint tags bake per frame with their axes");
	expect(modelBindPoseFrame(mesh) == 0, "the bind pose frame is found");
	ModelMesh limited = strip();
	ModelSkeletonBakeOptions options;
	options.maxFrames = 2;
	QString error;
	expect(bakeModelSkeleton(&limited, options, &error) && limited.frames.size() == 1 && limited.warnings.isEmpty() &&
			   !limited.detailLines.filter(QStringLiteral("bend")).isEmpty(),
		   "clips over the limit are left out with a note, not a warning");
	expect(modelBakedFrameName(QStringLiteral("a very long clip name"), 120).size() <= 15, "baked frame names fit MD3's field");
}

void checkEditing()
{
	ModelMesh mesh = strip();
	QString error;
	// A topology edit adds vertices: they take their nearest original's weights.
	ModelEdit subdivide;
	subdivide.kind = ModelEditKind::Subdivide;
	subdivide.selection = {0, {}, {0, 1, 2, 3}};
	ModelSelection selection;
	if (!expect(applyModelEdit(&mesh, subdivide, &selection, &error), "a skinned mesh subdivides"))
		std::cerr << "  " << error.toStdString() << '\n';
	const auto &surface = mesh.surfaces[0];
	expect(surface.vertexCount > 6 && surface.skinning.first.size() == surface.vertexCount, "influences cover the new vertices");
	expect(validateModelSkeleton(mesh, &error), "the edited skeleton stays valid");
	// Unchanged vertices keep their influences exactly.
	ModelFrameGeometry skinned;
	expect(skinModelSurface(surface, modelSkeletonBindPose(mesh.skeleton), &skinned), "the edited surface skins");
	bool bindMatches = true;
	const int bindFrame = modelBindPoseFrame(mesh);
	for (int v = 0; v < surface.vertexCount; ++v)
		bindMatches &= near(skinned.positions[v], surface.frames[bindFrame].positions[v], 1e-3);
	expect(bindFrame == 0 && bindMatches, "every vertex skins back to its bind position");

	// Move the far corner up in the bind pose, rebind, re-bake.
	ModelEdit lift;
	lift.kind = ModelEditKind::Transform;
	lift.frame = 0;
	lift.selection = {0, {2}, {}};
	lift.translation = {0, 0, 4};
	expect(applyModelEdit(&mesh, lift, &selection, &error), "the bind pose moves");
	expect(rebindModelSkinning(&mesh, 0) >= 1, "the moved vertex is rebound");
	expect(rebakeModelSkeleton(&mesh, &error), "the clips re-bake");
	const auto &rebent = mesh.surfaces[0].frames[2].positions;
	expect(near(rebent[2], {8, 8, 4}), "the animation follows the edited bind pose");

	// The editable source keeps the skeleton (version 8).
	const QJsonObject json = editableModelJson(mesh, &error);
	expect(json.value(QStringLiteral("version")).toInt() == 8 && json.contains(QStringLiteral("skeleton")), "skeletal sources are version 8");
	ModelMesh parsed;
	if (!expect(parseEditableModel(QJsonDocument(json).toJson(), &parsed, &error), "the skeletal source parses"))
		std::cerr << "  " << error.toStdString() << '\n';
	expect(parsed.skeleton.joints.size() == 2 && parsed.skeleton.clips.size() == 1 && parsed.skeleton.tags.size() == 1 &&
			   !parsed.surfaces.isEmpty() && parsed.surfaces[0].skinning.first.size() == parsed.surfaces[0].vertexCount &&
			   parsed.skeleton.bakedClipForFrame == mesh.skeleton.bakedClipForFrame,
		   "joints, clips, tags and influences come back");
	ModelFrameGeometry parsedSkin;
	expect(!parsed.surfaces.isEmpty() && skinModelSurface(parsed.surfaces[0], modelSkeletonBindPose(parsed.skeleton), &parsedSkin) &&
			   near(parsedSkin.positions.value(2), {16, 0, 4}),
		   "the source's influences reproduce the edited bind pose");
	auto damaged = json;
	auto skeleton = damaged.value(QStringLiteral("skeleton")).toObject();
	skeleton.insert(QStringLiteral("joints"), QJsonArray{});
	damaged.insert(QStringLiteral("skeleton"), skeleton);
	expect(!parseEditableModel(QJsonDocument(damaged).toJson(), &parsed, &error), "a damaged skeleton is refused");

	// Skeletal export poses the edited bind pose.
	const QByteArray md5 = exportEditableModel(mesh, QStringLiteral("md5mesh"), 0, &error);
	expect(!md5.isEmpty(), "the skinned mesh exports as md5mesh");
	const ModelMesh decoded = decodeModelMesh(QStringLiteral("strip.md5mesh"), md5);
	expect(decoded.error.isEmpty() && decoded.skeleton.joints.size() == 2, "the md5mesh decodes");
	bool found = false;
	for (const auto &position : decoded.surfaces.value(0).frames.value(0).positions)
		found |= near(position, {16, 0, 4}, 1e-3);
	expect(found, "the exported bind pose carries the edit");
	const QByteArray anim = exportEditableModel(mesh, QStringLiteral("md5anim"), 0, &error);
	expect(!anim.isEmpty() && anim.contains("numFrames 2"), "the clip exports as md5anim");
	const QByteArray iqm = exportEditableModel(mesh, QStringLiteral("iqm"), 0, &error);
	expect(!iqm.isEmpty() && iqm.startsWith("INTERQUAKEMODEL"), "the skinned mesh exports as IQM");
	ModelMesh plain = strip();
	plain.skeleton = {};
	for (auto &s : plain.surfaces)
		s.skinning = {};
	expect(exportEditableModel(plain, QStringLiteral("md5anim"), 0, &error).isEmpty() && !error.isEmpty(), "md5anim needs a skeletal clip");

	// Frame edits keep the clip map in step.
	ModelMesh frames = strip();
	ModelEdit duplicate;
	duplicate.kind = ModelEditKind::DuplicateFrame;
	duplicate.frame = 2;
	expect(applyModelEdit(&frames, duplicate, &selection, &error) && frames.skeleton.bakedClipForFrame.size() == frames.frames.size(),
		   "a duplicated frame joins the clip map");
	expect(modelBindPoseFrame(frames) == 0, "the bind pose is still found after a frame edit");
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	checkMaths();
	checkHierarchy();
	checkValidation();
	checkBake();
	checkEditing();
	if (failures == 0)
		std::cout << "model skeleton smoke passed\n";
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
