// RTCW skeletal MDS (MDSW 4), Wolfenstein: Enemy Territory MDM (MDMW 3) meshes
// with the MDX (MDXW 2) that holds their bones and frames, and MDX on its own.
//
// Layouts: mdsHeader_t, mdsFrame_t, mdsBoneFrameCompressed_t, mdsBoneInfo_t,
// mdsSurface_t, mdsVertex_t, mdsWeight_t and mdsTag_t from the GPL Return to
// Castle Wolfenstein source (src/qcommon/qfiles.h); mdmHeader_t, mdmSurface_t,
// mdmVertex_t, mdmWeight_t, mdmTag_t, mdxHeader_t, mdxFrame_t,
// mdxBoneFrameCompressed_t and mdxBoneInfo_t from the GPL Wolfenstein: Enemy
// Territory source (src/qcommon/qfiles.h). The bone and skinning maths follow
// R_CalcBone, R_CalcBones, RB_SurfaceAnim and R_GetBoneTag (RTCW
// src/renderer/tr_animation.c) and their MDM/MDX counterparts in ET
// src/renderer/tr_animation_mdm.c (RB_MDM_SurfaceAnim, R_MDM_GetBoneTag).
//
// What the renderer does, and so what this decoder reproduces:
// - Each bone's rotation is absolute: AnglesToAxis of its own angles in every
//   frame (SHORT2ANGLE of angles[0..2]; the fourth short is padding). Children
//   inherit only position: a child sits at its parent's position plus
//   parentDist along the forward vector of its own ofsAngles (pitch, yaw), and
//   the root sits at the frame's parentOffset.
// - Vertices are sum(boneWeight * (matrix * offset + translation)), where
//   `matrix` holds the AnglesToAxis vectors (forward, left, up) as its ROWS and
//   multiplies the offset as a column vector (LocalAddScaledMatrixTransform-
//   VectorTranslate). The joint matrices here are exactly that matrix, so the
//   offsets need no conversion and the weights stay as the file has them.
// - Normals are rotated by the first weight's bone only, so each vertex's
//   normal sits on its first influence and the others carry none.
// - MDS tags (R_GetBoneTag) copy the same rows out as the tag's axes, which in
//   column-vector terms is the transpose of the vertex matrix. A bone that
//   moves no vertices and is a tag (BONEFLAG_TAG, or named by an mdsTag_t)
//   therefore keeps its axes as matrix columns instead, so a tag with an
//   identity offset reproduces the game's tag exactly. Rotations are absolute
//   and children take only the parent's position, so the choice touches
//   nothing else.
// - MDM tags (R_MDM_GetBoneTag) apply the vertex matrix to their own axes and
//   offset, which is joint * tagOffset here with the axes as columns.
// - Torso blending (torsoParent, torsoWeight and the entity's torsoAxis) mixes
//   a second "torso" frame into the upper body. Decoding uses the same frame
//   for torso and legs with an identity torso axis; then every blend step in
//   R_CalcBone and R_CalcBones (angle lerp, SLerp_Normal of the offset
//   direction, the scaled torso rotation about the torso parent) returns the
//   legs result unchanged.
// - Only the full level of detail is decoded: the collapse maps are not
//   applied, and each surface's minLod is listed in the details. fixedParent
//   and fixedDist (mdsVertex_t) are not read by the renderer and are ignored.
// - Winding: these Q3-lineage renderers cull with GL_Cull (tr_backend.c), which
//   culls GL_FRONT for front-sided shaders exactly as for MD3, so the files
//   store clockwise front faces like MD3. Triangles are reversed here to the
//   counter-clockwise order the studio uses.
// - The weights' boneIndex is the global bone number: the renderer indexes
//   bones[] with it directly, whatever the old comment in qfiles.h says.
#include "core/model_formats_p.h"

#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio::model_formats {

namespace {

constexpr qint64 kMdsHeaderBytes = 120;       // mdsHeader_t
constexpr qint64 kMdmHeaderBytes = 100;       // mdmHeader_t
constexpr qint64 kMdxHeaderBytes = 96;        // mdxHeader_t
constexpr qint64 kWolfNameBytes = 64;         // MAX_QPATH
constexpr qint64 kWolfBoneInfoBytes = 80;     // mdsBoneInfo_t, mdxBoneInfo_t
constexpr qint64 kWolfFrameHeaderBytes = 52;  // mdsFrame_t / mdxFrame_t before the bones
constexpr qint64 kWolfBoneFrameBytes = 12;    // mdsBoneFrameCompressed_t, mdxBoneFrameCompressed_t
constexpr qint64 kWolfSurfaceBytes = 176;     // mdsSurface_t, mdmSurface_t
constexpr qint64 kMdsVertexBytes = 32;        // mdsVertex_t before its weights
constexpr qint64 kMdmVertexBytes = 24;        // mdmVertex_t before its weights
constexpr qint64 kWolfWeightBytes = 20;       // mdsWeight_t, mdmWeight_t
constexpr qint64 kWolfTriangleBytes = 12;     // mdsTriangle_t, mdmTriangle_t
constexpr qint64 kMdsTagBytes = 72;           // mdsTag_t
constexpr qint64 kMdmTagBytes = 128;          // mdmTag_t before its bone references
constexpr quint32 kBoneFlagTag = 1;           // BONEFLAG_TAG
constexpr int kGameMaxBones = 128;            // MDS_MAX_BONES, MDX_MAX_BONES
// Frames times bones kept as matrices (48 bytes each), which bounds memory.
constexpr qint64 kMaxBoneFrameSlots = 4LL * 1024LL * 1024LL;
constexpr double kShortToDegrees = 360.0 / 65536.0; // SHORT2ANGLE
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;
constexpr const char* kEtDefaultMdx = "animations/human/base/body.mdx";

struct WolfBone {
	QString name;
	int parent = -1;
	float torsoWeight = 0.0f;
	float parentDist = 0.0f;
	quint32 flags = 0;
};

// The bones and frames an MDS holds itself and an MDX holds for an MDM.
struct WolfAnimation {
	QString internalName;
	QVector<WolfBone> bones;
	int torsoParent = -1;
	int declaredFrames = 0;
	ModelSkeletalClip clip;
};

struct WolfSurface {
	ModelSurface surface;
	int minLod = 0;
	int boneReferences = 0;
};

struct WolfTag {
	QString name;
	int bone = 0;
	float torsoWeight = 0.0f;
	// MDM tags carry their own axes (rows of mdmTag_t::axis) and offset.
	bool hasTransform = false;
	ModelVec3 axis[3];
	ModelVec3 offset;
};

ModelJointMatrix transposed3x3(const ModelJointMatrix& in)
{
	ModelJointMatrix out = in;
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			out.m[row * 4 + column] = in.m[column * 4 + row];
		}
	}
	return out;
}

// Unique joint names for validateModelSkeleton, which compares them
// case-insensitively.
QString uniqueJointName(const QString& wanted, int index, QSet<QString>* used, bool* renamed)
{
	QString name = wanted.isEmpty() ? QStringLiteral("bone%1").arg(index) : wanted;
	if (used->contains(name.toLower())) {
		*renamed = true;
		const QString stem = name;
		for (int suffix = 2; used->contains(name.toLower()); ++suffix) {
			name = QStringLiteral("%1_%2").arg(stem).arg(suffix);
		}
	}
	used->insert(name.toLower());
	return name;
}

// mdsBoneInfo_t / mdxBoneInfo_t. Parents must come before their children.
bool readWolfBones(const QByteArray& bytes, const QString& format, qint64 offset, int count, QVector<WolfBone>* bones, QString* error,
	ModelWorkProgress& work)
{
	if (!rangeOk(bytes, offset, qint64(count) * kWolfBoneInfoBytes)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The %1 bone block lies outside the file.").arg(format);
		return false;
	}
	bones->clear();
	bones->reserve(count);
	for (int index = 0; index < count; ++index) {
		if (!work.step()) { return false; }
		const qint64 base = offset + qint64(index) * kWolfBoneInfoBytes;
		WolfBone bone;
		bone.name = readFixedName(bytes, base, kWolfNameBytes);
		bone.parent = readI32(bytes, base + 64);
		bone.torsoWeight = readF32(bytes, base + 68);
		bone.parentDist = readF32(bytes, base + 72);
		bone.flags = readU32(bytes, base + 76);
		if (bone.parent < -1 || bone.parent >= index) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "Bone %1 (%2) of the %3 names a parent that does not come before it.")
				.arg(index).arg(bone.name, format);
			return false;
		}
		if (!std::isfinite(bone.parentDist)) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "Bone %1 (%2) of the %3 has a parent distance that is not a finite number.")
				.arg(index).arg(bone.name, format);
			return false;
		}
		bones->append(bone);
	}
	return true;
}

// R_CalcBone for every bone of `frameCount` frames, with the torso frame equal
// to the legs frame (see the file comment). `columnAxes[bone]` stores that
// bone's AnglesToAxis vectors as matrix columns instead of rows.
bool readWolfFrames(const QByteArray& bytes, const QString& format, qint64 offset, int frameCount, const QVector<WolfBone>& bones,
	const QVector<bool>& columnAxes, ModelSkeletalClip* clip, QString* error, ModelWorkProgress& work)
{
	const int boneCount = bones.size();
	const qint64 stride = kWolfFrameHeaderBytes + qint64(boneCount) * kWolfBoneFrameBytes;
	if (!rangeOk(bytes, offset, qint64(frameCount) * stride)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The %1 frame block lies outside the file.").arg(format);
		return false;
	}
	clip->frames.reserve(frameCount);
	clip->frameMins.reserve(frameCount);
	clip->frameMaxs.reserve(frameCount);
	for (int frame = 0; frame < frameCount; ++frame) {
		if (!work.step()) { return false; }
		const qint64 base = offset + qint64(frame) * stride;
		ModelVec3 mins;
		ModelVec3 maxs;
		ModelVec3 parentOffset;
		readVec3(bytes, base, &mins);
		readVec3(bytes, base + 12, &maxs);
		readVec3(bytes, base + 40, &parentOffset);
		QVector<ModelJointMatrix> pose(boneCount);
		for (int index = 0; index < boneCount; ++index) {
			if (!work.step()) { return false; }
			const qint64 boneBase = base + kWolfFrameHeaderBytes + qint64(index) * kWolfBoneFrameBytes;
			const float pitch = float(readI16(bytes, boneBase) * kShortToDegrees);
			const float yaw = float(readI16(bytes, boneBase + 2) * kShortToDegrees);
			const float roll = float(readI16(bytes, boneBase + 4) * kShortToDegrees);
			const WolfBone& bone = bones.at(index);
			ModelVec3 translation = parentOffset;
			if (bone.parent >= 0) {
				// LocalAngleVector: the forward vector of (ofsAngles[0], ofsAngles[1], 0).
				const double ofsPitch = readI16(bytes, boneBase + 8) * kShortToDegrees * kDegreesToRadians;
				const double ofsYaw = readI16(bytes, boneBase + 10) * kShortToDegrees * kDegreesToRadians;
				const double direction[3]{std::cos(ofsPitch) * std::cos(ofsYaw), std::cos(ofsPitch) * std::sin(ofsYaw), -std::sin(ofsPitch)};
				const ModelVec3 parent = modelJointTranslation(pose.at(bone.parent));
				translation = {float(parent.x + double(bone.parentDist) * direction[0]), float(parent.y + double(bone.parentDist) * direction[1]),
					float(parent.z + double(bone.parentDist) * direction[2])};
			}
			// The columns are AnglesToAxis's forward, left and up vectors.
			const ModelJointMatrix axes = modelJointFromQuakeAngles(pitch, yaw, roll, translation);
			pose[index] = columnAxes.value(index) ? axes : transposed3x3(axes);
		}
		clip->frames.append(pose);
		clip->frameMins.append(mins);
		clip->frameMaxs.append(maxs);
	}
	return true;
}

// mdsSurface_t / mdmSurface_t chains with their variable-sized vertices.
// `boneCount` < 0 leaves bone indexes unchecked (an MDM before its MDX is
// known); `highestBone` collects the largest index used either way.
bool readWolfSurfaces(const QByteArray& bytes, const QString& format, bool mdm, qint64 offset, int count, int boneCount,
	QVector<WolfSurface>* surfaces, int* highestBone, QString* error, ModelWorkProgress& work)
{
	const qint64 vertexBytes = mdm ? kMdmVertexBytes : kMdsVertexBytes;
	const auto boneOk = [&](int bone) {
		if (bone < 0 || (boneCount >= 0 && bone >= boneCount)) {
			return false;
		}
		*highestBone = std::max(*highestBone, bone);
		return true;
	};
	for (int index = 0; index < count; ++index) {
		if (!work.step()) { return false; }
		if (!rangeOk(bytes, offset, kWolfSurfaceBytes)) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "%1 surface %2 lies outside the file.").arg(format).arg(index);
			return false;
		}
		const QString name = readFixedName(bytes, offset + 4, kWolfNameBytes);
		const QString shader = readFixedName(bytes, offset + 68, kWolfNameBytes);
		const int minLod = readI32(bytes, offset + 136);
		const int vertexCount = readI32(bytes, offset + 144);
		const int vertexOffset = readI32(bytes, offset + 148);
		const int triangleCount = readI32(bytes, offset + 152);
		const int triangleOffset = readI32(bytes, offset + 156);
		const int referenceCount = readI32(bytes, offset + 164);
		const int referenceOffset = readI32(bytes, offset + 168);
		const int endOffset = readI32(bytes, offset + 172);
		if (vertexCount < 0 || vertexCount > kMaxSurfaceVertices || triangleCount < 0 || triangleCount > kMaxSurfaceTriangles
			|| referenceCount < 0 || referenceCount > kMaxJoints) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "%1 surface %2 declares counts outside the supported range.")
				.arg(format).arg(index);
			return false;
		}
		const auto blockOk = [&](int blockOffset, qint64 length) {
			return length == 0 || (blockOffset >= 0 && rangeOk(bytes, offset + blockOffset, length));
		};
		if (!blockOk(triangleOffset, qint64(triangleCount) * kWolfTriangleBytes) || !blockOk(referenceOffset, qint64(referenceCount) * 4)
			|| !blockOk(vertexOffset, qint64(vertexCount) * vertexBytes)) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "%1 surface %2 has a data block outside the file.").arg(format).arg(index);
			return false;
		}

		WolfSurface parsed;
		parsed.minLod = minLod;
		parsed.boneReferences = referenceCount;
		ModelSurface& surface = parsed.surface;
		surface.name = name.isEmpty() ? QStringLiteral("surface%1").arg(index) : name;
		surface.vertexCount = vertexCount;
		if (!shader.isEmpty()) {
			surface.skinPaths.append(shader);
		}

		// Bone references list every bone the surface's weights need.
		for (int reference = 0; reference < referenceCount; ++reference) {
			if (!work.step()) { return false; }
			if (!boneOk(readI32(bytes, offset + referenceOffset + qint64(reference) * 4))) {
				*error = QCoreApplication::translate("VibeStudioModelMesh", "%1 surface \"%2\" refers to a bone outside the skeleton.")
					.arg(format, surface.name);
				return false;
			}
		}

		ModelSurfaceSkinning& skinning = surface.skinning;
		skinning.first.reserve(vertexCount);
		skinning.count.reserve(vertexCount);
		surface.texCoords.reserve(vertexCount);
		qint64 cursor = offset + vertexOffset;
		for (int vertex = 0; vertex < vertexCount; ++vertex) {
			if (!work.step()) { return false; }
			if (!rangeOk(bytes, cursor, vertexBytes)) {
				*error = QCoreApplication::translate("VibeStudioModelMesh", "The vertices of %1 surface \"%2\" run past the end of the file.")
					.arg(format, surface.name);
				return false;
			}
			ModelVec3 normal;
			readVec3(bytes, cursor, &normal);
			ModelTexCoord coord;
			coord.u = readF32(bytes, cursor + 12);
			coord.v = readF32(bytes, cursor + 16);
			surface.texCoords.append(coord);
			const int weightCount = readI32(bytes, cursor + 20);
			if (weightCount < 1 || weightCount > kMaxInfluencesPerVertex) {
				*error = QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of %2 surface \"%3\" declares %4 bone weights.")
					.arg(QString::number(vertex), format, surface.name, QString::number(weightCount));
				return false;
			}
			const qint64 weightBase = cursor + vertexBytes;
			if (!rangeOk(bytes, weightBase, qint64(weightCount) * kWolfWeightBytes)) {
				*error = QCoreApplication::translate("VibeStudioModelMesh", "The vertices of %1 surface \"%2\" run past the end of the file.")
					.arg(format, surface.name);
				return false;
			}
			skinning.first.append(skinning.influences.size());
			skinning.count.append(weightCount);
			for (int weight = 0; weight < weightCount; ++weight) {
				const qint64 base = weightBase + qint64(weight) * kWolfWeightBytes;
				ModelJointInfluence influence;
				influence.joint = readI32(bytes, base);
				if (!boneOk(influence.joint)) {
					*error = QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of %2 surface \"%3\" is weighted to a bone outside the skeleton.")
						.arg(vertex).arg(format, surface.name);
					return false;
				}
				influence.weight = readF32(bytes, base + 4);
				readVec3(bytes, base + 8, &influence.offset);
				// RB_SurfaceAnim rotates the normal by the first weight's bone.
				if (weight == 0) {
					influence.normalOffset = normal;
				}
				skinning.influences.append(influence);
			}
			cursor = weightBase + qint64(weightCount) * kWolfWeightBytes;
		}

		int dropped = 0;
		surface.triangles.reserve(triangleCount);
		for (int triangle = 0; triangle < triangleCount; ++triangle) {
			if (!work.step()) { return false; }
			const qint64 base = offset + triangleOffset + qint64(triangle) * kWolfTriangleBytes;
			const int a = readI32(bytes, base);
			const int b = readI32(bytes, base + 4);
			const int c = readI32(bytes, base + 8);
			if (a < 0 || a >= vertexCount || b < 0 || b >= vertexCount || c < 0 || c >= vertexCount) {
				++dropped;
				continue;
			}
			// Clockwise in the file (see the file comment), counter-clockwise here.
			surface.triangles.append(ModelTriangle{a, c, b});
		}
		if (dropped > 0) {
			surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.")
				.arg(dropped);
		}
		surfaces->append(parsed);

		if (index + 1 < count) {
			if (endOffset < kWolfSurfaceBytes) {
				*error = QCoreApplication::translate("VibeStudioModelMesh", "%1 surface \"%2\" declares an end offset that does not reach the next surface.")
					.arg(format, surface.name);
				return false;
			}
			offset += endOffset;
		}
	}
	return true;
}

void fillWolfSkeleton(ModelMesh* mesh, const QString& sourceFormat, const WolfAnimation& animation)
{
	ModelSkeleton& skeleton = mesh->skeleton;
	skeleton.sourceFormat = sourceFormat;
	QSet<QString> used;
	bool renamed = false;
	const QVector<ModelJointMatrix>& firstFrame = animation.clip.frames.first();
	for (int index = 0; index < animation.bones.size(); ++index) {
		const WolfBone& bone = animation.bones.at(index);
		ModelJoint joint;
		joint.name = uniqueJointName(bone.name, index, &used, &renamed);
		joint.parent = bone.parent;
		joint.flags = bone.flags;
		// No separate bind pose: the first frame stands in for it.
		joint.bind = firstFrame.at(index);
		skeleton.joints.append(joint);
	}
	skeleton.clips.append(animation.clip);
	if (renamed) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Some bones share a name; the repeats were given a numbered suffix.");
	}
}

void addWolfAnimationDetails(ModelMesh* mesh, const WolfAnimation& animation)
{
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skeleton: %1 bone(s), %2 frame(s)")
		.arg(animation.bones.size()).arg(animation.clip.frames.size());
	if (animation.bones.size() > kGameMaxBones) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh",
			"The skeleton has %1 bones; the RTCW and Enemy Territory renderers hold at most %2.")
			.arg(animation.bones.size()).arg(kGameMaxBones);
	}
	if (animation.torsoParent >= 0 && animation.torsoParent < animation.bones.size()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Torso parent: %1")
			.arg(mesh->skeleton.joints.at(animation.torsoParent).name);
	} else if (animation.torsoParent != -1) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The torso parent bone %1 is outside the skeleton.").arg(animation.torsoParent);
	}
	int tagBones = 0;
	for (const WolfBone& bone : animation.bones) {
		if (bone.flags & kBoneFlagTag) {
			++tagBones;
		}
	}
	if (tagBones > 0) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Tag bones (BONEFLAG_TAG): %1").arg(tagBones);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh",
		"Torso and legs are decoded from the same frame, where the game's torso blending leaves every bone unchanged.");
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh",
		"Frame timing and clip names come from the game's animation scripts, so every frame is listed as one clip with no rate.");
	if (animation.declaredFrames > animation.clip.frames.size()) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The skeleton declares %1 frames; only the first %2 were kept to bound memory.")
			.arg(animation.declaredFrames).arg(animation.clip.frames.size());
	}
}

// How many frames of `boneCount` bones fit the matrix budget.
int keptFrameCount(int declared, int boneCount)
{
	const qint64 limit = std::max<qint64>(1, kMaxBoneFrameSlots / std::max(1, boneCount));
	return int(std::min<qint64>(declared, limit));
}

// mdxHeader_t and its bones and frames.
bool parseMdx(const QByteArray& bytes, WolfAnimation* animation, int* version, QString* error, ModelWorkProgress& work)
{
	const QString format = QStringLiteral("MDX");
	if (!rangeOk(bytes, 0, kMdxHeaderBytes)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The MDX header is truncated.");
		return false;
	}
	if (bytes.left(4) != QByteArrayLiteral("MDXW")) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The file is not an MDX (MDXW) skeleton.");
		return false;
	}
	*version = readI32(bytes, 4);
	if (*version != 2) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MDX version %1; only MDXW version 2 is decoded.").arg(*version);
		return false;
	}
	animation->internalName = readFixedName(bytes, 8, kWolfNameBytes);
	const int frameCount = readI32(bytes, 72);
	const int boneCount = readI32(bytes, 76);
	const int frameOffset = readI32(bytes, 80);
	const int boneOffset = readI32(bytes, 84);
	animation->torsoParent = readI32(bytes, 88);
	if (frameCount <= 0 || boneCount <= 0 || boneCount > kMaxJoints) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The MDX header declares counts outside the supported range.");
		return false;
	}
	if (!readWolfBones(bytes, format, boneOffset, boneCount, &animation->bones, error, work)) {
		return false;
	}
	const qint64 stride = kWolfFrameHeaderBytes + qint64(boneCount) * kWolfBoneFrameBytes;
	if (!rangeOk(bytes, frameOffset, qint64(frameCount) * stride)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The %1 frame block lies outside the file.").arg(format);
		return false;
	}
	animation->declaredFrames = frameCount;
	animation->clip.name = QStringLiteral("all");
	// MDM tags read the vertex matrix (R_MDM_GetBoneTag), so every bone keeps
	// its axes as rows.
	return readWolfFrames(bytes, format, frameOffset, keptFrameCount(frameCount, boneCount), animation->bones, QVector<bool>(boneCount, false),
		&animation->clip, error, work);
}

// Bakes the clip. Without a separate bind pose the first clip frame is the
// bind; when the clip is too long for the bake's budget the first frame is
// baked as the bind pose instead, so the mesh still shows.
void bakeWolfSkeleton(ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelSkeletonBakeOptions options;
	options.includeBindPose = false;
	qint64 totalVertices = 0;
	for (const ModelSurface& surface : mesh->surfaces) {
		totalVertices += surface.vertexCount;
	}
	const qint64 clipFrames = mesh->skeleton.clips.isEmpty() ? 0 : mesh->skeleton.clips.first().frames.size();
	if (totalVertices == 0) {
		// Animation only: keep every frame.
		options.maxFrames = int(std::max<qint64>(options.maxFrames, std::min<qint64>(clipFrames, std::numeric_limits<int>::max())));
	} else if (clipFrames > options.maxFrames || clipFrames * totalVertices > options.maxVertexSlots) {
		options.includeBindPose = true;
	}
	QString problem;
	if (!bakeModelSkeleton(mesh, options, &problem, control)) {
		mesh->error = problem;
		return;
	}
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
}

// Adds the parsed surfaces to the mesh, leaving out any without vertices.
void addWolfSurfaces(ModelMesh* mesh, const QString& format, QVector<WolfSurface>& surfaces)
{
	QSet<QString> skinNames(mesh->skinPaths.begin(), mesh->skinPaths.end());
	for (WolfSurface& parsed : surfaces) {
		ModelSurface& surface = parsed.surface;
		if (surface.vertexCount == 0) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "%1 surface \"%2\" has no vertices and was left out.")
				.arg(format, surface.name);
			continue;
		}
		if (parsed.minLod > 0) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh",
				"Surface \"%1\": %2 vertices, at least %3 at its lowest level of detail (the collapse map is not applied).")
				.arg(surface.name, QString::number(surface.vertexCount), QString::number(parsed.minLod));
		}
		for (const QString& skin : surface.skinPaths) {
			if (!skinNames.contains(skin)) {
				skinNames.insert(skin);
				mesh->skinPaths.append(skin);
			}
		}
		surface.index = mesh->surfaces.size();
		mesh->surfaces.append(surface);
	}
}

} // namespace

void decodeMds(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	const QString format = QStringLiteral("MDS");
	if (!rangeOk(bytes, 0, kMdsHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDS header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 4) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MDS version %1; only MDSW version 4 is decoded.").arg(mesh->version);
		return;
	}
	WolfAnimation animation;
	animation.internalName = readFixedName(bytes, 8, kWolfNameBytes);
	const float lodScale = readF32(bytes, 72);
	const float lodBias = readF32(bytes, 76);
	const int frameCount = readI32(bytes, 80);
	const int boneCount = readI32(bytes, 84);
	const int frameOffset = readI32(bytes, 88);
	const int boneOffset = readI32(bytes, 92);
	animation.torsoParent = readI32(bytes, 96);
	const int surfaceCount = readI32(bytes, 100);
	const int surfaceOffset = readI32(bytes, 104);
	const int tagCount = readI32(bytes, 108);
	const int tagOffset = readI32(bytes, 112);
	if (frameCount <= 0 || boneCount <= 0 || boneCount > kMaxJoints || surfaceCount < 0 || surfaceCount > kMaxSurfaces || tagCount < 0
		|| tagCount > kMaxTags) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDS header declares counts outside the supported range.");
		return;
	}
	if (!readWolfBones(bytes, format, boneOffset, boneCount, &animation.bones, &mesh->error, work)) {
		return;
	}
	const qint64 stride = kWolfFrameHeaderBytes + qint64(boneCount) * kWolfBoneFrameBytes;
	if (!rangeOk(bytes, frameOffset, qint64(frameCount) * stride)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The %1 frame block lies outside the file.").arg(format);
		return;
	}

	QVector<WolfSurface> surfaces;
	int highestBone = -1;
	if (!readWolfSurfaces(bytes, format, false, surfaceOffset, surfaceCount, boneCount, &surfaces, &highestBone, &mesh->error, work)) {
		return;
	}

	// mdsTag_t
	if (tagCount > 0 && !rangeOk(bytes, tagOffset, qint64(tagCount) * kMdsTagBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDS tag block lies outside the file.");
		return;
	}
	QVector<WolfTag> tags;
	for (int index = 0; index < tagCount; ++index) {
		if (!work.step()) { return; }
		const qint64 base = tagOffset + qint64(index) * kMdsTagBytes;
		WolfTag tag;
		tag.name = readFixedName(bytes, base, kWolfNameBytes);
		tag.torsoWeight = readF32(bytes, base + 64);
		tag.bone = readI32(bytes, base + 68);
		if (tag.bone < 0 || tag.bone >= boneCount) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDS tag \"%1\" follows a bone outside the skeleton.").arg(tag.name);
			return;
		}
		tags.append(tag);
	}

	// Tag bones that move no vertices keep the game's tag axes as columns.
	QVector<bool> weighted(boneCount, false);
	for (const WolfSurface& parsed : surfaces) {
		for (const ModelJointInfluence& influence : parsed.surface.skinning.influences) {
			weighted[influence.joint] = true;
		}
	}
	QVector<bool> columnAxes(boneCount, false);
	for (int index = 0; index < boneCount; ++index) {
		columnAxes[index] = (animation.bones.at(index).flags & kBoneFlagTag) != 0;
	}
	for (const WolfTag& tag : tags) {
		columnAxes[tag.bone] = true;
	}
	for (int index = 0; index < boneCount; ++index) {
		columnAxes[index] = columnAxes.at(index) && !weighted.at(index);
	}

	animation.declaredFrames = frameCount;
	animation.clip.name = QStringLiteral("all");
	if (!readWolfFrames(bytes, format, frameOffset, keptFrameCount(frameCount, boneCount), animation.bones, columnAxes, &animation.clip,
			&mesh->error, work)) {
		return;
	}

	fillWolfSkeleton(mesh, QStringLiteral("mds"), animation);
	if (!animation.internalName.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Internal name: %1").arg(animation.internalName);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Level of detail scale %1, bias %2").arg(lodScale).arg(lodBias);
	addWolfAnimationDetails(mesh, animation);
	addWolfSurfaces(mesh, format, surfaces);
	for (const WolfTag& tag : tags) {
		ModelSkeletalTag skeletalTag;
		skeletalTag.name = tag.name;
		skeletalTag.joint = tag.bone;
		mesh->skeleton.tags.append(skeletalTag);
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Tag \"%1\" follows bone \"%2\" (torso weight %3).")
			.arg(tag.name, mesh->skeleton.joints.at(tag.bone).name, QString::number(tag.torsoWeight));
		if (!columnAxes.at(tag.bone)) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh",
				"Tag \"%1\" sits on bone \"%2\", which also moves vertices, so the tag takes the bone's vertex axes; RTCW reads that bone's axes transposed for tags.")
				.arg(tag.name, mesh->skeleton.joints.at(tag.bone).name);
		}
	}
	bakeWolfSkeleton(mesh, control);
}

void decodeMdm(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	const QString format = QStringLiteral("MDM");
	if (!rangeOk(bytes, 0, kMdmHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDM header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 3) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MDM version %1; only MDMW version 3 is decoded.").arg(mesh->version);
		return;
	}
	const QString internalName = readFixedName(bytes, 8, kWolfNameBytes);
	const float lodScale = readF32(bytes, 72);
	const float lodBias = readF32(bytes, 76);
	const int surfaceCount = readI32(bytes, 80);
	const int surfaceOffset = readI32(bytes, 84);
	const int tagCount = readI32(bytes, 88);
	const int tagOffset = readI32(bytes, 92);
	if (surfaceCount < 0 || surfaceCount > kMaxSurfaces || tagCount < 0 || tagCount > kMaxTags) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDM header declares counts outside the supported range.");
		return;
	}

	QVector<WolfSurface> surfaces;
	int highestBone = -1;
	if (!readWolfSurfaces(bytes, format, true, surfaceOffset, surfaceCount, -1, &surfaces, &highestBone, &mesh->error, work)) {
		return;
	}

	// mdmTag_t: each one walks to the next by its own ofsEnd.
	QVector<WolfTag> tags;
	qint64 tagCursor = tagOffset;
	for (int index = 0; index < tagCount; ++index) {
		if (!work.step()) { return; }
		if (!rangeOk(bytes, tagCursor, kMdmTagBytes)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDM tag %1 lies outside the file.").arg(index);
			return;
		}
		WolfTag tag;
		tag.name = readFixedName(bytes, tagCursor, kWolfNameBytes);
		tag.hasTransform = true;
		for (int axis = 0; axis < 3; ++axis) {
			readVec3(bytes, tagCursor + 64 + qint64(axis) * 12, &tag.axis[axis]);
		}
		tag.bone = readI32(bytes, tagCursor + 100);
		readVec3(bytes, tagCursor + 104, &tag.offset);
		const int referenceCount = readI32(bytes, tagCursor + 116);
		const int referenceOffset = readI32(bytes, tagCursor + 120);
		const int endOffset = readI32(bytes, tagCursor + 124);
		bool bonesOk = tag.bone >= 0 && referenceCount >= 0 && referenceCount <= kMaxJoints
			&& (referenceCount == 0 || (referenceOffset >= 0 && rangeOk(bytes, tagCursor + referenceOffset, qint64(referenceCount) * 4)));
		for (int reference = 0; bonesOk && reference < referenceCount; ++reference) {
			const int bone = readI32(bytes, tagCursor + referenceOffset + qint64(reference) * 4);
			bonesOk = bone >= 0;
			highestBone = std::max(highestBone, bone);
		}
		if (!bonesOk) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDM tag \"%1\" has bone data outside the file or the skeleton.").arg(tag.name);
			return;
		}
		highestBone = std::max(highestBone, tag.bone);
		tags.append(tag);
		if (index + 1 < tagCount) {
			if (endOffset < kMdmTagBytes) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDM tag \"%1\" declares an end offset that does not reach the next tag.")
					.arg(tag.name);
				return;
			}
			tagCursor += endOffset;
		}
	}

	// The bones and frames live in an MDX: the one named like the mesh, then
	// any beside it that covers every bone the mesh uses, then ET's shared
	// human skeleton.
	const QString directory = pathDirectory(path);
	const int bonesNeeded = std::max(1, highestBone + 1);
	QStringList tried;
	QString mdxPath;
	int mdxVersion = 0;
	WolfAnimation animation;
	const auto tryMdx = [&](const QString& candidate) {
		if (candidate.isEmpty() || tried.contains(candidate, Qt::CaseInsensitive) || !companions.canRead()) {
			return false;
		}
		tried.append(candidate);
		QByteArray data;
		QString problem;
		if (!companions.read(candidate, &data, &problem)) {
			return false;
		}
		WolfAnimation parsed;
		int version = 0;
		if (!parseMdx(data, &parsed, &version, &problem, work)) {
			if (mesh->error.isEmpty()) {
				mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skipped MDX %1: %2").arg(candidate, problem);
			}
			return false;
		}
		if (parsed.bones.size() < bonesNeeded) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skipped MDX %1: it has %2 bone(s) and the mesh uses %3.")
				.arg(candidate, QString::number(parsed.bones.size()), QString::number(bonesNeeded));
			return false;
		}
		animation = parsed;
		mdxPath = candidate;
		mdxVersion = version;
		return true;
	};
	bool found = tryMdx(joinPath(directory, pathStem(path) + QStringLiteral(".mdx")));
	if (!found && mesh->error.isEmpty()) {
		const QStringList listed = companions.list(directory, {QStringLiteral("mdx")});
		for (const QString& candidate : listed) {
			if (!mesh->error.isEmpty() || companions.exhausted()) {
				break;
			}
			if (tryMdx(candidate)) {
				found = true;
				break;
			}
		}
	}
	if (!found && mesh->error.isEmpty()) {
		found = tryMdx(QString::fromLatin1(kEtDefaultMdx));
	}
	if (!mesh->error.isEmpty()) {
		return;
	}
	if (!found) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh",
			"The MDM needs its MDX for bones and frames, and no MDX with %1 bone(s) was found beside it or at %2.")
			.arg(bonesNeeded).arg(QString::fromLatin1(kEtDefaultMdx));
		return;
	}
	animation.clip.sourcePath = mdxPath;

	fillWolfSkeleton(mesh, QStringLiteral("mdm"), animation);
	if (!internalName.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Internal name: %1").arg(internalName);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Level of detail scale %1, bias %2").arg(lodScale).arg(lodBias);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Bones and frames: %1 (MDX version %2)").arg(mdxPath, QString::number(mdxVersion));
	addWolfAnimationDetails(mesh, animation);
	addWolfSurfaces(mesh, format, surfaces);
	for (const WolfTag& tag : tags) {
		// R_MDM_GetBoneTag: origin = matrix * offset + translation and each
		// axis = matrix * tag axis, i.e. joint * [axes as columns | offset].
		ModelSkeletalTag skeletalTag;
		skeletalTag.name = tag.name;
		skeletalTag.joint = tag.bone;
		for (int axis = 0; axis < 3; ++axis) {
			skeletalTag.offset.m[0 * 4 + axis] = tag.axis[axis].x;
			skeletalTag.offset.m[1 * 4 + axis] = tag.axis[axis].y;
			skeletalTag.offset.m[2 * 4 + axis] = tag.axis[axis].z;
		}
		skeletalTag.offset.m[3] = tag.offset.x;
		skeletalTag.offset.m[7] = tag.offset.y;
		skeletalTag.offset.m[11] = tag.offset.z;
		mesh->skeleton.tags.append(skeletalTag);
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Tag \"%1\" follows bone \"%2\".")
			.arg(tag.name, mesh->skeleton.joints.at(tag.bone).name);
	}
	bakeWolfSkeleton(mesh, control);
}

void decodeMdx(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	WolfAnimation animation;
	QString problem;
	if (!parseMdx(bytes, &animation, &mesh->version, &problem, work)) {
		if (mesh->error.isEmpty()) {
			mesh->error = problem;
		}
		return;
	}
	fillWolfSkeleton(mesh, QStringLiteral("mdm"), animation);
	if (!animation.internalName.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Internal name: %1").arg(animation.internalName);
	}
	addWolfAnimationDetails(mesh, animation);
	bakeWolfSkeleton(mesh, control);
}

} // namespace vibestudio::model_formats
