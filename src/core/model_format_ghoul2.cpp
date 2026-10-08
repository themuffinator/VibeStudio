// Ghoul 2 skeletal models (Jedi Outcast, Jedi Academy, Soldier of Fortune II):
// GLM meshes (2LGM version 6) with the GLA (2LGA version 6) they name, and a
// GLA on its own.
//
// Layouts follow OpenJK's GPL mdx_format.h (codemp/rd-common/mdx_format.h):
// mdxmHeader_t, mdxmHierarchyOffsets_t, mdxmSurfHierarchy_t, mdxmLOD_t,
// mdxmLODSurfOffset_t, mdxmSurface_t, mdxmTriangle_t, mdxmVertex_t (with
// G2_GetVertWeights, G2_GetVertBoneIndex and G2_GetVertBoneWeight
// reimplemented below), mdxmVertexTexCoord_t, mdxaHeader_t, mdxaSkelOffsets_t,
// mdxaSkel_t, mdxaIndex_t and mdxaCompQuatBone_t. The bone maths follows
// G2_TransformBone, UnCompressBone, G2_GetBonePoolIndex, RootMatrix,
// RB_SurfaceGhoul and G2_ProcessSurfaceBolt2 in OpenJK
// codemp/rd-vanilla/tr_ghoul2.cpp, and MC_UnCompressQuat in
// codemp/qcommon/matcomp.cpp.
//
// What the game does, and so what this decoder reproduces:
// - Each GLA frame holds, per bone, an index into a pool of compressed
//   quaternion-plus-translation matrices U. G2_TransformBone chains them down
//   the hierarchy: final[0] = root * U[0], final[child] = final[parent] *
//   U[child]. RB_SurfaceGhoul then skins the GLM's bind-pose vertices with
//   sum(weight * final * vertex), so final = M * BasePoseMatInv, where M is
//   the bone's model-space matrix in that frame (bolts read it back as final *
//   BasePoseMat in G2_GetBoltMatrixLow). The clips here hold
//   M = final * BasePoseMat, the joints' bind is BasePoseMat, and the GLM
//   vertices become offsets in each bone's bind space through
//   modelSkinningFromBindPose, so skinning reproduces final * vertex.
// - The root matrix: RootMatrix and G2_ConstructGhoulSkeleton hand
//   G2_TransformBone tr_ghoul2.cpp's "identityMatrix", which is not the
//   identity but a quarter turn about +Z (x' = -y, y' = x); the same constant
//   sits in the single-player renderer (code/rd-vanilla/tr_ghoul2.cpp), and
//   G2API_GetBoltMatrix's comment calls it "this 90 degree offset thing". The
//   files are already Z-up in Quake units: nothing else rotates the vertices
//   before the entity's own AnglesToAxis, exactly as for MD3. So the only
//   conversion is that turn, applied to the bind pose, every frame and the
//   vertices, which leaves the model facing +X the way the game shows it.
// - Weights: up to four per vertex, the last being one minus the others,
//   which carry ten bits each (BoneWeightings plus two bits packed at 20+).
// - Bolt ("tag") surfaces (G2SURFACEFLAG_ISBOLT) are attachment triangles,
//   not geometry. Their bolt is built from the first three vertices as
//   G2_ProcessSurfaceBolt2 does; when those follow one bone it is exact as a
//   skeletal tag on that bone.
// - Winding: the Ghoul 2 renderer culls through the same GL_Cull as Quake III
//   (GL_FRONT for front-sided shaders), so GLM triangles are clockwise fronts
//   like MD3 and are reversed here to counter-clockwise.
// - Jedi Academy remaps the bone references of 72-bone Jedi Outcast humanoid
//   meshes onto its own humanoid skeleton with a table in its renderer
//   (R_LoadMDXM). OpenJK carries that table under GPL-2.0 only, which cannot
//   be combined with VibeStudio's GPL-3.0, so it is not reproduced: such a mesh
//   needs the Jedi Outcast GLA it was built for, and says so when it is absent.
#include "core/model_formats_p.h"

#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio::model_formats {

namespace {

constexpr qint64 kGlmHeaderBytes = 164;        // mdxmHeader_t
constexpr qint64 kGlmHierarchyBytes = 144;     // mdxmSurfHierarchy_t before its child indexes
constexpr qint64 kGlmSurfaceBytes = 40;        // mdxmSurface_t
constexpr qint64 kGlmVertexBytes = 32;         // mdxmVertex_t
constexpr qint64 kGlmTexCoordBytes = 8;        // mdxmVertexTexCoord_t
constexpr qint64 kGlmTriangleBytes = 12;       // mdxmTriangle_t
constexpr qint64 kGlaHeaderBytes = 100;        // mdxaHeader_t
constexpr qint64 kGlaSkelBytes = 172;          // mdxaSkel_t before its children
constexpr qint64 kGlaCompBoneBytes = 14;       // mdxaCompQuatBone_t
constexpr qint64 kGlaIndexBytes = 3;           // mdxaIndex_t
constexpr qint64 kNameBytes = 64;              // MAX_QPATH
constexpr int kMaxLods = 64;
constexpr int kWeightsPerVertex = 4;           // iMAX_G2_BONEWEIGHTS_PER_VERT
constexpr int kBoneRefBits = 5;                // iG2_BITS_PER_BONEREF
constexpr int kWeightTopBitsShift = 12;        // iG2_BONEWEIGHT_TOPBITS_SHIFT
constexpr quint32 kWeightTopBitsMask = 0x300;  // iG2_BONEWEIGHT_TOPBITS_AND
constexpr quint32 kSurfaceFlagBolt = 0x1;      // G2SURFACEFLAG_ISBOLT
constexpr quint32 kSurfaceFlagOff = 0x2;       // G2SURFACEFLAG_OFF
constexpr int kBoltOrigin = 2;                 // MDX_TAG_ORIGIN
// Frames times bones kept as matrices (48 bytes each), which bounds memory.
constexpr qint64 kMaxBoneFrameSlots = 4LL * 1024LL * 1024LL;

// Bones in the Jedi Outcast humanoid skeleton.
constexpr int kOldHumanoidBones = 72;

// tr_ghoul2.cpp's root "identityMatrix": a quarter turn about +Z.
ModelJointMatrix ghoul2RootMatrix()
{
	ModelJointMatrix root;
	root.m[0] = 0.0f;
	root.m[1] = -1.0f;
	root.m[4] = 1.0f;
	root.m[5] = 0.0f;
	return root;
}

// mdxaBone_t: row-major 3x4 with the translation in the last column, acting
// on column vectors, which is ModelJointMatrix's own layout.
ModelJointMatrix readBoneMatrix(const QByteArray& bytes, qint64 offset)
{
	ModelJointMatrix matrix;
	for (int index = 0; index < 12; ++index) {
		matrix.m[index] = readF32(bytes, offset + qint64(index) * 4);
	}
	return matrix;
}

// MC_UnCompressQuat (OpenJK codemp/qcommon/matcomp.cpp): seven little-endian
// 16-bit values; w, x, y, z = value / 16383 - 2 and the translation = value /
// 64 - 512. The rotation is built from the quaternion as stored, without
// renormalising it, as the game does.
ModelJointMatrix uncompressBone(const QByteArray& bytes, qint64 offset)
{
	const float w = float(readU16(bytes, offset)) / 16383.0f - 2.0f;
	const float x = float(readU16(bytes, offset + 2)) / 16383.0f - 2.0f;
	const float y = float(readU16(bytes, offset + 4)) / 16383.0f - 2.0f;
	const float z = float(readU16(bytes, offset + 6)) / 16383.0f - 2.0f;
	const float tx = 2.0f * x;
	const float ty = 2.0f * y;
	const float tz = 2.0f * z;
	const float twx = tx * w;
	const float twy = ty * w;
	const float twz = tz * w;
	const float txx = tx * x;
	const float txy = ty * x;
	const float txz = tz * x;
	const float tyy = ty * y;
	const float tyz = tz * y;
	const float tzz = tz * z;
	ModelJointMatrix matrix;
	matrix.m[0] = 1.0f - (tyy + tzz);
	matrix.m[1] = txy - twz;
	matrix.m[2] = txz + twy;
	matrix.m[3] = float(readU16(bytes, offset + 8)) / 64.0f - 512.0f;
	matrix.m[4] = txy + twz;
	matrix.m[5] = 1.0f - (txx + tzz);
	matrix.m[6] = tyz - twx;
	matrix.m[7] = float(readU16(bytes, offset + 10)) / 64.0f - 512.0f;
	matrix.m[8] = txz - twy;
	matrix.m[9] = tyz + twx;
	matrix.m[10] = 1.0f - (txx + tyy);
	matrix.m[11] = float(readU16(bytes, offset + 12)) / 64.0f - 512.0f;
	return matrix;
}

ModelVec3 subtract(const ModelVec3& a, const ModelVec3& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

double dot(const ModelVec3& a, const ModelVec3& b)
{
	return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
}

ModelVec3 cross(const ModelVec3& a, const ModelVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool normalizedInto(const ModelVec3& v, ModelVec3* out)
{
	const double length = std::sqrt(dot(v, v));
	if (!(length > 1e-12) || !std::isfinite(length)) {
		return false;
	}
	*out = {float(v.x / length), float(v.y / length), float(v.z / length)};
	return true;
}

// G2_ProcessSurfaceBolt2's model-tag branch: the bolt of a triangle, origin at
// vertex MDX_TAG_ORIGIN, axes from its longest (0) and shortest (2) sides.
bool boltFromTriangle(const ModelVec3 corner[3], ModelJointMatrix* out)
{
	ModelVec3 sides[3];
	for (int index = 0; index < 3; ++index) {
		sides[index] = subtract(corner[(index + 1) % 3], corner[index]);
	}
	ModelVec3 longest;
	ModelVec3 shortest;
	ModelVec3 normal;
	if (!normalizedInto(sides[0], &longest) || !normalizedInto(sides[2], &shortest)) {
		return false;
	}
	const double d = dot(longest, shortest);
	if (!normalizedInto({float(longest.x - d * shortest.x), float(longest.y - d * shortest.y), float(longest.z - d * shortest.z)}, &longest)
		|| !normalizedInto(cross(sides[0], sides[2]), &normal)) {
		return false;
	}
	// "orient minus Y to positive X": columns are shortest, longest, -normal.
	const ModelVec3 columns[3]{shortest, longest, {-normal.x, -normal.y, -normal.z}};
	for (int column = 0; column < 3; ++column) {
		out->m[0 * 4 + column] = columns[column].x;
		out->m[1 * 4 + column] = columns[column].y;
		out->m[2 * 4 + column] = columns[column].z;
	}
	out->m[3] = corner[kBoltOrigin].x;
	out->m[7] = corner[kBoltOrigin].y;
	out->m[11] = corner[kBoltOrigin].z;
	return true;
}

struct GlaSkeleton {
	int version = 0;
	QString internalName;
	float scale = 0.0f;
	qint64 poolEntries = 0;
	int declaredFrames = 0;
	int extraRoots = 0;
	bool renamed = false;
	QVector<ModelJoint> joints;
	ModelSkeletalClip clip;
};

// mdxaHeader_t, the skeleton and every frame.
bool parseGla(const QByteArray& bytes, GlaSkeleton* out, QString* error, ModelWorkProgress& work)
{
	if (!rangeOk(bytes, 0, kGlaHeaderBytes)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The GLA header is truncated.");
		return false;
	}
	if (bytes.left(4) != QByteArrayLiteral("2LGA")) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The file is not a Ghoul 2 GLA (2LGA) skeleton.");
		return false;
	}
	out->version = readI32(bytes, 4);
	if (out->version != 6) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported GLA version %1; only 2LGA version 6 is decoded.").arg(out->version);
		return false;
	}
	out->internalName = readFixedName(bytes, 8, kNameBytes);
	out->scale = readF32(bytes, 72);
	const int frameCount = readI32(bytes, 76);
	const int frameOffset = readI32(bytes, 80);
	const int boneCount = readI32(bytes, 84);
	const int poolOffset = readI32(bytes, 88);
	if (frameCount <= 0 || boneCount <= 0 || boneCount > kMaxJoints) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The GLA header declares counts outside the supported range.");
		return false;
	}
	// The renderer finds the skeleton offsets straight after the header
	// (sizeof(mdxaHeader_t)), each relative to that point.
	if (!rangeOk(bytes, kGlaHeaderBytes, qint64(boneCount) * 4)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The GLA skeleton offsets lie outside the file.");
		return false;
	}
	if (!rangeOk(bytes, frameOffset, qint64(frameCount) * boneCount * kGlaIndexBytes)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The GLA frame block lies outside the file.");
		return false;
	}
	if (poolOffset < 0 || poolOffset >= bytes.size()) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The GLA compressed bone pool lies outside the file.");
		return false;
	}
	out->poolEntries = (qint64(bytes.size()) - poolOffset) / kGlaCompBoneBytes;
	if (out->poolEntries <= 0) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The GLA compressed bone pool lies outside the file.");
		return false;
	}

	const ModelJointMatrix root = ghoul2RootMatrix();
	QVector<ModelJointMatrix> basePose;
	basePose.reserve(boneCount);
	QSet<QString> used;
	for (int index = 0; index < boneCount; ++index) {
		if (!work.step()) { return false; }
		const qint64 relative = readI32(bytes, kGlaHeaderBytes + qint64(index) * 4);
		const qint64 base = kGlaHeaderBytes + relative;
		if (relative < 0 || !rangeOk(bytes, base, kGlaSkelBytes)) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "GLA bone %1 lies outside the file.").arg(index);
			return false;
		}
		ModelJoint joint;
		const QString name = readFixedName(bytes, base, kNameBytes);
		joint.flags = readU32(bytes, base + 64);
		joint.parent = readI32(bytes, base + 68);
		if (joint.parent < -1 || joint.parent >= index) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "Bone %1 (%2) of the GLA names a parent that does not come before it.")
				.arg(index).arg(name);
			return false;
		}
		if (joint.parent < 0 && index > 0) {
			++out->extraRoots;
		}
		QString unique = name.isEmpty() ? QStringLiteral("bone%1").arg(index) : name;
		if (used.contains(unique.toLower())) {
			out->renamed = true;
			const QString stem = unique;
			for (int suffix = 2; used.contains(unique.toLower()); ++suffix) {
				unique = QStringLiteral("%1_%2").arg(stem).arg(suffix);
			}
		}
		used.insert(unique.toLower());
		joint.name = unique;
		const ModelJointMatrix pose = readBoneMatrix(bytes, base + 72);
		basePose.append(pose);
		joint.bind = modelJointMultiply(root, pose);
		out->joints.append(joint);
	}

	out->declaredFrames = frameCount;
	const int keptFrames = int(std::min<qint64>(frameCount, std::max<qint64>(1, kMaxBoneFrameSlots / boneCount)));
	out->clip.name = QStringLiteral("all");
	out->clip.frames.reserve(keptFrames);
	QVector<ModelJointMatrix> chain(boneCount);
	for (int frame = 0; frame < keptFrames; ++frame) {
		if (!work.step()) { return false; }
		QVector<ModelJointMatrix> pose(boneCount);
		for (int index = 0; index < boneCount; ++index) {
			if (!work.step()) { return false; }
			// G2_GetBonePoolIndex: a little-endian 24-bit pool index.
			const qint64 at = frameOffset + (qint64(frame) * boneCount + index) * kGlaIndexBytes;
			const qint64 entry = qint64(readU8(bytes, at)) | (qint64(readU8(bytes, at + 1)) << 8) | (qint64(readU8(bytes, at + 2)) << 16);
			if (entry >= out->poolEntries) {
				*error = QCoreApplication::translate("VibeStudioModelMesh", "Frame %1 of the GLA points bone %2 outside the compressed bone pool.")
					.arg(frame).arg(index);
				return false;
			}
			const ModelJointMatrix local = uncompressBone(bytes, poolOffset + entry * kGlaCompBoneBytes);
			const int parent = out->joints.at(index).parent;
			chain[index] = modelJointMultiply(parent < 0 ? root : chain.at(parent), local);
			pose[index] = modelJointMultiply(chain.at(index), basePose.at(index));
		}
		out->clip.frames.append(pose);
	}
	return true;
}

void fillGhoul2Skeleton(ModelMesh* mesh, const GlaSkeleton& gla, const QString& sourcePath)
{
	ModelSkeleton& skeleton = mesh->skeleton;
	skeleton.sourceFormat = QStringLiteral("ghoul2");
	skeleton.joints = gla.joints;
	skeleton.clips.append(gla.clip);
	skeleton.clips.last().sourcePath = sourcePath;
	if (!gla.internalName.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skeleton name: %1").arg(gla.internalName);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skeleton: %1 bone(s), %2 frame(s), %3 compressed bone(s) in the pool")
		.arg(gla.joints.size()).arg(gla.clip.frames.size()).arg(gla.poolEntries);
	if (gla.scale != 0.0f) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Scale the skeleton was built with: %1 (not applied by the game)")
			.arg(gla.scale);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh",
		"The game turns every Ghoul 2 skeleton a quarter turn about Z at its root; the turn is applied, so the model faces +X.");
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh",
		"Frame timing and clip names come from the game's animation.cfg, so every frame is listed as one clip with no rate.");
	if (gla.declaredFrames > gla.clip.frames.size()) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The skeleton declares %1 frames; only the first %2 were kept to bound memory.")
			.arg(gla.declaredFrames).arg(gla.clip.frames.size());
	}
	if (gla.extraRoots > 0) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh",
			"%1 bone(s) besides the first have no parent; the game expects a single root, and they were rooted like it.").arg(gla.extraRoots);
	}
	if (gla.renamed) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Some bones share a name; the repeats were given a numbered suffix.");
	}
}

struct GlmVertex {
	ModelVec3 position;
	ModelVec3 normal;
	int count = 0;
	int reference[kWeightsPerVertex]{};
	float weight[kWeightsPerVertex]{};
};

struct GlmSurface {
	int index = 0;
	QString name;
	QString shader;
	quint32 flags = 0;
	int parent = -1;
	QVector<GlmVertex> vertices;
	QVector<ModelTexCoord> texCoords;
	QVector<ModelTriangle> triangles;
	QVector<int> boneReferences;
	int droppedTriangles = 0;
};

} // namespace

void decodeGla(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	GlaSkeleton gla;
	QString problem;
	if (!parseGla(bytes, &gla, &problem, work)) {
		if (mesh->error.isEmpty()) {
			mesh->error = problem;
		}
		mesh->version = gla.version;
		return;
	}
	mesh->version = gla.version;
	fillGhoul2Skeleton(mesh, gla, QString());
	// Animation only: the bind pose is not one of the frames, and every frame
	// is kept.
	ModelSkeletonBakeOptions options;
	options.includeBindPose = false;
	options.maxFrames = std::max(options.maxFrames, int(gla.clip.frames.size()));
	if (!bakeModelSkeleton(mesh, options, &problem, control)) {
		mesh->error = problem;
	}
}

void decodeGlm(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(bytes, 0, kGlmHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The GLM header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 6) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported GLM version %1; only 2LGM version 6 is decoded.").arg(mesh->version);
		return;
	}
	const QString internalName = readFixedName(bytes, 8, kNameBytes);
	const QString animName = readFixedName(bytes, 72, kNameBytes);
	const int meshBones = readI32(bytes, 140);
	const int lodCount = readI32(bytes, 144);
	const int lodOffset = readI32(bytes, 148);
	const int surfaceCount = readI32(bytes, 152);
	if (surfaceCount < 0 || surfaceCount > kMaxSurfaces || lodCount < 1 || lodCount > kMaxLods) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The GLM header declares counts outside the supported range.");
		return;
	}
	// mdxmHierarchyOffsets_t sits straight after the header, each offset
	// relative to it, as the renderer reads it.
	if (!rangeOk(bytes, kGlmHeaderBytes, qint64(surfaceCount) * 4)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The GLM surface hierarchy lies outside the file.");
		return;
	}
	// LOD 0: mdxmLOD_t, then mdxmLODSurfOffset_t relative to its own start.
	const qint64 lodSurfaceOffsets = qint64(lodOffset) + 4;
	if (lodOffset < 0 || !rangeOk(bytes, lodSurfaceOffsets, qint64(surfaceCount) * 4)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The GLM level of detail block lies outside the file.");
		return;
	}

	QVector<GlmSurface> surfaces;
	surfaces.reserve(surfaceCount);
	for (int index = 0; index < surfaceCount; ++index) {
		if (!work.step()) { return; }
		GlmSurface surface;
		surface.index = index;
		const qint64 hierarchyRelative = readI32(bytes, kGlmHeaderBytes + qint64(index) * 4);
		const qint64 hierarchy = kGlmHeaderBytes + hierarchyRelative;
		if (hierarchyRelative < 0 || !rangeOk(bytes, hierarchy, kGlmHierarchyBytes)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "GLM surface %1 has hierarchy data outside the file.").arg(index);
			return;
		}
		surface.name = readFixedName(bytes, hierarchy, kNameBytes);
		if (surface.name.isEmpty()) {
			surface.name = QStringLiteral("surface%1").arg(index);
		}
		surface.flags = readU32(bytes, hierarchy + 64);
		surface.shader = readFixedName(bytes, hierarchy + 68, kNameBytes);
		surface.parent = readI32(bytes, hierarchy + 136);
		const int childCount = readI32(bytes, hierarchy + 140);
		if (surface.parent < -1 || surface.parent >= surfaceCount || childCount < 0 || childCount > surfaceCount
			|| !rangeOk(bytes, hierarchy + kGlmHierarchyBytes, qint64(childCount) * 4)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "GLM surface \"%1\" has a parent or children outside the surface list.")
				.arg(surface.name);
			return;
		}
		for (int child = 0; child < childCount; ++child) {
			const int childIndex = readI32(bytes, hierarchy + kGlmHierarchyBytes + qint64(child) * 4);
			if (childIndex < 0 || childIndex >= surfaceCount) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "GLM surface \"%1\" has a parent or children outside the surface list.")
					.arg(surface.name);
				return;
			}
		}

		const qint64 surfaceRelative = readI32(bytes, lodSurfaceOffsets + qint64(index) * 4);
		const qint64 base = lodSurfaceOffsets + surfaceRelative;
		if (surfaceRelative < 0 || !rangeOk(bytes, base, kGlmSurfaceBytes)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "GLM surface \"%1\" lies outside the file.").arg(surface.name);
			return;
		}
		const int thisIndex = readI32(bytes, base + 4);
		const int vertexCount = readI32(bytes, base + 12);
		const int vertexOffset = readI32(bytes, base + 16);
		const int triangleCount = readI32(bytes, base + 20);
		const int triangleOffset = readI32(bytes, base + 24);
		const int referenceCount = readI32(bytes, base + 28);
		const int referenceOffset = readI32(bytes, base + 32);
		if (vertexCount < 0 || vertexCount > kMaxSurfaceVertices || triangleCount < 0 || triangleCount > kMaxSurfaceTriangles
			|| referenceCount < 0 || referenceCount > kMaxJoints) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "GLM surface \"%1\" declares counts outside the supported range.")
				.arg(surface.name);
			return;
		}
		const auto blockOk = [&](int blockOffset, qint64 length) {
			return length == 0 || (blockOffset >= 0 && rangeOk(bytes, base + blockOffset, length));
		};
		// The texture coordinates follow the vertices.
		if (!blockOk(vertexOffset, qint64(vertexCount) * (kGlmVertexBytes + kGlmTexCoordBytes))
			|| !blockOk(triangleOffset, qint64(triangleCount) * kGlmTriangleBytes) || !blockOk(referenceOffset, qint64(referenceCount) * 4)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "GLM surface \"%1\" has a data block outside the file.").arg(surface.name);
			return;
		}
		if (thisIndex != index) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "GLM surface \"%1\" calls itself surface %2 but is listed as %3.")
				.arg(surface.name, QString::number(thisIndex), QString::number(index));
		}

		for (int reference = 0; reference < referenceCount; ++reference) {
			const int bone = readI32(bytes, base + referenceOffset + qint64(reference) * 4);
			if (bone < 0) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "GLM surface \"%1\" refers to a bone outside the skeleton.").arg(surface.name);
				return;
			}
			surface.boneReferences.append(bone);
		}

		surface.vertices.reserve(vertexCount);
		surface.texCoords.reserve(vertexCount);
		const qint64 texCoordBase = base + vertexOffset + qint64(vertexCount) * kGlmVertexBytes;
		for (int vertex = 0; vertex < vertexCount; ++vertex) {
			if (!work.step()) { return; }
			const qint64 at = base + vertexOffset + qint64(vertex) * kGlmVertexBytes;
			GlmVertex parsed;
			readVec3(bytes, at, &parsed.normal);
			readVec3(bytes, at + 12, &parsed.position);
			// G2_GetVertWeights, G2_GetVertBoneIndex, G2_GetVertBoneWeight.
			const quint32 packed = readU32(bytes, at + 24);
			parsed.count = int(packed >> 30) + 1;
			float total = 0.0f;
			for (int weight = 0; weight < parsed.count; ++weight) {
				const int reference = int((packed >> (kBoneRefBits * weight)) & ((1u << kBoneRefBits) - 1u));
				if (reference >= referenceCount) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh",
						"Vertex %1 of GLM surface \"%2\" is weighted to a bone outside the surface's bone references.")
						.arg(vertex).arg(surface.name);
					return;
				}
				parsed.reference[weight] = reference;
				if (weight == parsed.count - 1) {
					parsed.weight[weight] = 1.0f - total;
				} else {
					const quint32 value = quint32(readU8(bytes, at + 28 + weight))
						| ((packed >> (kWeightTopBitsShift + weight * 2)) & kWeightTopBitsMask);
					parsed.weight[weight] = float(value) * (1.0f / 1023.0f);
					total += parsed.weight[weight];
				}
			}
			surface.vertices.append(parsed);
			ModelTexCoord coord;
			coord.u = readF32(bytes, texCoordBase + qint64(vertex) * kGlmTexCoordBytes);
			coord.v = readF32(bytes, texCoordBase + qint64(vertex) * kGlmTexCoordBytes + 4);
			surface.texCoords.append(coord);
		}

		surface.triangles.reserve(triangleCount);
		for (int triangle = 0; triangle < triangleCount; ++triangle) {
			if (!work.step()) { return; }
			const qint64 at = base + triangleOffset + qint64(triangle) * kGlmTriangleBytes;
			const int a = readI32(bytes, at);
			const int b = readI32(bytes, at + 4);
			const int c = readI32(bytes, at + 8);
			if (a < 0 || a >= vertexCount || b < 0 || b >= vertexCount || c < 0 || c >= vertexCount) {
				++surface.droppedTriangles;
				continue;
			}
			// Clockwise in the file (see the file comment), counter-clockwise here.
			surface.triangles.append(ModelTriangle{a, c, b});
		}
		surfaces.append(surface);
	}

	// The GLA the mesh names (game-relative, without its extension), then any
	// GLA beside the mesh.
	int highestReference = -1;
	for (const GlmSurface& surface : surfaces) {
		for (int bone : surface.boneReferences) {
			highestReference = std::max(highestReference, bone);
		}
	}
	const bool oldHumanoid = meshBones == kOldHumanoidBones && animName.contains(QStringLiteral("_humanoid"));
	QStringList tried;
	QString glaPath;
	GlaSkeleton gla;
	const auto tryGla = [&](const QString& candidate) {
		if (candidate.isEmpty() || tried.contains(candidate, Qt::CaseInsensitive) || !companions.canRead()) {
			return false;
		}
		tried.append(candidate);
		QByteArray data;
		QString problem;
		if (!companions.read(candidate, &data, &problem)) {
			return false;
		}
		GlaSkeleton parsed;
		if (!parseGla(data, &parsed, &problem, work)) {
			if (mesh->error.isEmpty()) {
				mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skipped GLA %1: %2").arg(candidate, problem);
			}
			return false;
		}
		const int skeletonBones = parsed.joints.size();
		if (highestReference >= skeletonBones) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skipped GLA %1: it has %2 bone(s) and the mesh uses %3.")
				.arg(candidate, QString::number(skeletonBones), QString::number(highestReference + 1));
			return false;
		}
		gla = parsed;
		glaPath = candidate;
		return true;
	};
	bool found = !animName.isEmpty() && tryGla(animName + QStringLiteral(".gla"));
	if (!found && mesh->error.isEmpty()) {
		const QStringList listed = companions.list(pathDirectory(path), {QStringLiteral("gla")});
		for (const QString& candidate : listed) {
			if (!mesh->error.isEmpty() || companions.exhausted()) {
				break;
			}
			if (tryGla(candidate)) {
				found = true;
				break;
			}
		}
	}
	if (!mesh->error.isEmpty()) {
		return;
	}
	if (!found && oldHumanoid) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh",
			"This GLM was built for the 72-bone Jedi Outcast humanoid, and no GLA with that many bones was found at %1.gla or "
			"beside the mesh. Jedi Academy remaps such meshes onto its own skeleton; VibeStudio does not, so open it with the "
			"Jedi Outcast skeleton.")
			.arg(animName);
		return;
	}
	if (!found) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh",
			"The GLM needs its GLA for bones and frames, and no usable GLA was found at %1.gla or beside the mesh.")
			.arg(animName);
		return;
	}

	fillGhoul2Skeleton(mesh, gla, glaPath);
	if (!internalName.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Internal name: %1").arg(internalName);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Bones and frames: %1").arg(glaPath);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Levels of detail: %1; the first is decoded.").arg(lodCount);
	if (meshBones != gla.joints.size()) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The GLM was built for %1 bone(s) and its GLA has %2.")
			.arg(meshBones).arg(gla.joints.size());
	}

	const ModelJointMatrix root = ghoul2RootMatrix();
	QSet<QString> skinNames;
	for (GlmSurface& parsed : surfaces) {
		if (!work.step()) { return; }
		if (parsed.flags & kSurfaceFlagOff) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Surface \"%1\" is off by default (flags 0x%2).")
				.arg(parsed.name, QString::number(parsed.flags, 16));
		}
		const auto jointOf = [&](const GlmVertex& vertex, int weight) { return parsed.boneReferences.at(vertex.reference[weight]); };

		if (parsed.flags & kSurfaceFlagBolt) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Surface \"%1\" is a bolt (tag) surface (flags 0x%2).")
				.arg(parsed.name, QString::number(parsed.flags, 16));
			if (parsed.vertices.size() < 3) {
				mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Bolt surface \"%1\" has fewer than three vertices, so no tag was made.")
					.arg(parsed.name);
				continue;
			}
			// The bone carrying most of the triangle's weight.
			QVector<double> load(gla.joints.size(), 0.0);
			bool rigid = true;
			int firstJoint = jointOf(parsed.vertices.at(0), 0);
			ModelVec3 corners[3];
			for (int corner = 0; corner < 3; ++corner) {
				const GlmVertex& vertex = parsed.vertices.at(corner);
				corners[corner] = vertex.position;
				for (int weight = 0; weight < vertex.count; ++weight) {
					const int joint = jointOf(vertex, weight);
					load[joint] += vertex.weight[weight];
					if (joint != firstJoint && std::abs(vertex.weight[weight]) > 1e-6f) {
						rigid = false;
					}
				}
			}
			const int joint = int(std::max_element(load.begin(), load.end()) - load.begin());
			ModelJointMatrix bolt;
			bool inverted = false;
			const ModelJointMatrix inverseBind = modelJointInverse(gla.joints.at(joint).bind, &inverted);
			if (!boltFromTriangle(corners, &bolt) || !inverted) {
				mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Bolt surface \"%1\" is degenerate, so no tag was made.")
					.arg(parsed.name);
				continue;
			}
			ModelSkeletalTag tag;
			tag.name = parsed.name;
			tag.joint = joint;
			tag.offset = modelJointMultiply(inverseBind, modelJointMultiply(root, bolt));
			mesh->skeleton.tags.append(tag);
			if (!rigid) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh",
					"Bolt surface \"%1\" is weighted to several bones; its tag follows \"%2\" alone and matches the game only where that bone dominates.")
					.arg(parsed.name, gla.joints.at(joint).name);
			}
			continue;
		}
		if (parsed.vertices.isEmpty()) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "GLM surface \"%1\" has no vertices and was left out.").arg(parsed.name);
			continue;
		}

		ModelSurface surface;
		surface.index = mesh->surfaces.size();
		surface.name = parsed.name;
		surface.vertexCount = parsed.vertices.size();
		surface.texCoords = parsed.texCoords;
		surface.triangles = parsed.triangles;
		// Carcass writes "[nomaterial]" for a surface without a material.
		if (!parsed.shader.isEmpty() && parsed.shader.compare(QStringLiteral("[nomaterial]"), Qt::CaseInsensitive) != 0) {
			surface.skinPaths.append(parsed.shader);
			if (!skinNames.contains(parsed.shader)) {
				skinNames.insert(parsed.shader);
				mesh->skinPaths.append(parsed.shader);
			}
		}
		if (parsed.droppedTriangles > 0) {
			surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.")
				.arg(parsed.droppedTriangles);
		}
		QVector<ModelVec3> positions;
		QVector<ModelVec3> normals;
		QVector<int> joints;
		QVector<float> weights;
		positions.reserve(surface.vertexCount);
		normals.reserve(surface.vertexCount);
		joints.reserve(surface.vertexCount * kWeightsPerVertex);
		weights.reserve(surface.vertexCount * kWeightsPerVertex);
		for (const GlmVertex& vertex : parsed.vertices) {
			if (!work.step()) { return; }
			positions.append(modelJointTransformPoint(root, vertex.position));
			normals.append(modelJointTransformVector(root, vertex.normal));
			for (int weight = 0; weight < kWeightsPerVertex; ++weight) {
				if (weight < vertex.count) {
					joints.append(jointOf(vertex, weight));
					weights.append(vertex.weight[weight]);
				} else {
					joints.append(0);
					weights.append(0.0f);
				}
			}
		}
		QString problem;
		if (!modelSkinningFromBindPose(mesh->skeleton, positions, normals, joints, weights, kWeightsPerVertex, &surface.skinning, &problem)) {
			mesh->error = problem;
			return;
		}
		mesh->surfaces.append(surface);
	}

	// A GLM has a real bind pose (its vertices), baked first; the bake's
	// budget decides whether the GLA's long clip fits beside it.
	QString problem;
	if (!bakeModelSkeleton(mesh, ModelSkeletonBakeOptions{}, &problem, control)) {
		mesh->error = problem;
		return;
	}
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
}

} // namespace vibestudio::model_formats
