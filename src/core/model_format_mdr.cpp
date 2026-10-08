// ioquake3 / Elite Force skeletal models (MDR, "RDM5" version 2).
//
// Layouts follow mdrHeader_t, mdrFrame_t, mdrCompFrame_t, mdrCompBone_t,
// mdrBone_t, mdrLOD_t, mdrSurface_t, mdrVertex_t, mdrWeight_t, mdrTriangle_t
// and mdrTag_t in ioquake3's code/qcommon/qfiles.h (GPL). Decoding follows
// R_LoadMDR and R_GetAnimTag (code/renderergl1/tr_model.c) and
// RB_MDRSurfaceAnim (code/renderergl1/tr_animation.c); compressed bones are
// expanded the way that file's MC_UnCompress does (from Raven's GPL md3view).
//
// Each frame stores one model-space 3x4 matrix per bone, and each vertex is
// sum(weight * (bone matrix * offset)). The file stores no hierarchy and no
// separate bind pose, so the joints are roots named "bone<i>", the bind pose
// is the first frame, and every frame forms one clip whose timing comes from
// the game's scripts. Only the first level of detail is decoded.

#include "core/model_formats_p.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>

namespace vibestudio::model_formats {

namespace {

constexpr qint64 kMdrHeaderBytes = 104;
constexpr qint64 kMdrNameBytes = 64;
constexpr qint64 kMdrFrameHeaderBytes = 56;     // bounds, origin, radius, name[16]
constexpr qint64 kMdrCompFrameHeaderBytes = 40; // bounds, origin, radius
constexpr qint64 kMdrBoneBytes = 48;
constexpr qint64 kMdrCompBoneBytes = 24;
constexpr qint64 kMdrLodBytes = 12;
constexpr qint64 kMdrSurfaceBytes = 168;
constexpr qint64 kMdrVertexBytes = 24; // normal, texCoords, numWeights
constexpr qint64 kMdrWeightBytes = 20;
constexpr qint64 kMdrTriangleBytes = 12;
constexpr qint64 kMdrTagBytes = 36;
constexpr int kMdrMaxLods = 16;
// RB_MDRSurfaceAnim lerps into a fixed MDR_MAX_BONES array.
constexpr int kMdrEngineMaxBones = 128;
// Frames times bones held in memory at once.
constexpr qint64 kMdrMaxBoneSlots = 2LL * 1024LL * 1024LL;

// MC_UnCompress: twelve little-endian unsigned shorts, biased by half their
// range; translation in 1/64 units, rotation in 1/32766 steps.
ModelJointMatrix mdrUncompressBone(const QByteArray& bytes, qint64 offset)
{
	ModelJointMatrix matrix;
	const auto value = [&](int index) { return double(int(readU16(bytes, offset + (qint64(index) * 2))) - 32768); };
	constexpr double kTranslationScale = 1.0 / 64.0;
	constexpr double kVectorScale = 1.0 / 32766.0;
	matrix.m[3] = float(value(0) * kTranslationScale);
	matrix.m[7] = float(value(1) * kTranslationScale);
	matrix.m[11] = float(value(2) * kTranslationScale);
	matrix.m[0] = float(value(3) * kVectorScale);
	matrix.m[1] = float(value(4) * kVectorScale);
	matrix.m[2] = float(value(5) * kVectorScale);
	matrix.m[4] = float(value(6) * kVectorScale);
	matrix.m[5] = float(value(7) * kVectorScale);
	matrix.m[6] = float(value(8) * kVectorScale);
	matrix.m[8] = float(value(9) * kVectorScale);
	matrix.m[9] = float(value(10) * kVectorScale);
	matrix.m[10] = float(value(11) * kVectorScale);
	return matrix;
}

} // namespace

void decodeMdr(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(bytes, 0, kMdrHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 2) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MDR version %1; only RDM5 version 2 is decoded.").arg(mesh->version);
		return;
	}

	// mdrHeader_t
	const QString internalName = readFixedName(bytes, 8, kMdrNameBytes);
	const int frameCount = readI32(bytes, 72);
	const int boneCount = readI32(bytes, 76);
	const qint64 frameOffset = readI32(bytes, 80);
	const int lodCount = readI32(bytes, 84);
	const qint64 lodOffset = readI32(bytes, 88);
	const int tagCount = readI32(bytes, 92);
	const qint64 tagOffset = readI32(bytes, 96);
	const qint64 endOffset = readI32(bytes, 100);

	// R_LoadMDR refuses an end offset past the file ("wrong filesize declared").
	if (endOffset < kMdrHeaderBytes || endOffset > bytes.size()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR end offset (%1) does not match the file size (%2).")
			.arg(endOffset).arg(bytes.size());
		return;
	}
	if (endOffset < bytes.size()) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MDR ends at byte %1 of %2; the bytes after it are ignored.")
			.arg(endOffset).arg(bytes.size());
	}
	const QByteArray data = endOffset < bytes.size() ? bytes.left(qsizetype(endOffset)) : bytes;

	if (frameCount <= 0 || frameCount > kMaxFrames || boneCount <= 0 || boneCount > kMaxJoints || lodCount < 0 || lodCount > kMdrMaxLods
		|| tagCount < 0 || tagCount > kMaxTags) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR header declares counts outside the supported range.");
		return;
	}
	if (lodCount == 0) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR holds no level of detail.");
		return;
	}
	if (qint64(frameCount) * qint64(boneCount) > kMdrMaxBoneSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR declares more frame bones than can be decoded.");
		return;
	}

	// A negative ofsFrames marks compressed frames (mdrCompFrame_t) at -ofsFrames.
	const bool compressed = frameOffset < 0;
	const qint64 framesStart = compressed ? -frameOffset : frameOffset;
	const qint64 frameBytes = compressed ? kMdrCompFrameHeaderBytes + (qint64(boneCount) * kMdrCompBoneBytes)
		: kMdrFrameHeaderBytes + (qint64(boneCount) * kMdrBoneBytes);
	if (framesStart < kMdrHeaderBytes || !rangeOk(data, framesStart, qint64(frameCount) * frameBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR frame block lies outside the file.");
		return;
	}
	if (tagCount > 0 && (tagOffset < kMdrHeaderBytes || !rangeOk(data, tagOffset, qint64(tagCount) * kMdrTagBytes))) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR tag block lies outside the file.");
		return;
	}

	if (!internalName.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Internal name: %1").arg(internalName);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Bones: %1").arg(boneCount);
	if (compressed) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Frames are stored with compressed bone matrices.");
	}
	if (boneCount > kMdrEngineMaxBones) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MDR declares %1 bones; ioquake3 renders at most %2.")
			.arg(boneCount).arg(kMdrEngineMaxBones);
	}

	// Frames: one model-space matrix per bone (mdrBone_t's matrix[3][4] is
	// row-major with the translation in the last column, as ModelJointMatrix).
	ModelSkeletalClip clip;
	clip.name = QStringLiteral("all");
	clip.framesPerSecond = 0.0;
	clip.loops = true;
	clip.frames.reserve(frameCount);
	clip.frameMins.reserve(frameCount);
	clip.frameMaxs.reserve(frameCount);
	QStringList frameNames;
	for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
		if (!work.step()) { return; }
		const qint64 base = framesStart + (qint64(frameIndex) * frameBytes);
		ModelVec3 mins, maxs;
		readVec3(data, base, &mins);
		readVec3(data, base + 12, &maxs);
		clip.frameMins.append(mins);
		clip.frameMaxs.append(maxs);
		if (!compressed) {
			frameNames.append(readFixedName(data, base + 40, 16));
		}
		QVector<ModelJointMatrix> pose(boneCount);
		for (int bone = 0; bone < boneCount; ++bone) {
			if (!work.step()) { return; }
			if (compressed) {
				pose[bone] = mdrUncompressBone(data, base + kMdrCompFrameHeaderBytes + (qint64(bone) * kMdrCompBoneBytes));
				continue;
			}
			const qint64 boneBase = base + kMdrFrameHeaderBytes + (qint64(bone) * kMdrBoneBytes);
			for (int index = 0; index < 12; ++index) {
				pose[bone].m[index] = readF32(data, boneBase + (qint64(index) * 4));
			}
		}
		clip.frames.append(pose);
	}

	ModelSkeleton& skeleton = mesh->skeleton;
	skeleton.sourceFormat = QStringLiteral("mdr");
	skeleton.joints.reserve(boneCount);
	for (int bone = 0; bone < boneCount; ++bone) {
		ModelJoint joint;
		joint.name = QStringLiteral("bone%1").arg(bone);
		joint.parent = -1;
		joint.bind = clip.frames.first().at(bone);
		skeleton.joints.append(joint);
	}

	// mdrTag_t: a name and the bone it follows; R_GetAnimTag uses the bone's
	// matrix as the tag directly, so the offset is the identity.
	for (int index = 0; index < tagCount; ++index) {
		if (!work.step()) { return; }
		const qint64 base = tagOffset + (qint64(index) * kMdrTagBytes);
		const qint32 bone = readI32(data, base);
		if (bone < 0 || bone >= boneCount) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDR tag %1 follows bone %2, which does not exist.").arg(index).arg(bone);
			return;
		}
		ModelSkeletalTag tag;
		tag.name = readFixedName(data, base + 4, 32);
		if (tag.name.isEmpty()) {
			tag.name = QStringLiteral("tag%1").arg(index);
		}
		tag.joint = bone;
		skeleton.tags.append(tag);
	}

	// Levels of detail: mdrLOD_t, each followed by its surfaces. Offsets in a
	// LOD are relative to the LOD; offsets in a surface to the surface.
	QSet<QString> skinNames;
	qint64 totalVertices = 0;
	qint64 lodBase = lodOffset;
	for (int lod = 0; lod < lodCount; ++lod) {
		if (!work.step()) { return; }
		if (lodBase < kMdrHeaderBytes || !rangeOk(data, lodBase, kMdrLodBytes)) {
			if (lod == 0) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR level of detail block lies outside the file.");
				return;
			}
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "MDR level of detail %1 lies outside the file; later levels are not listed.").arg(lod);
			break;
		}
		const int surfaceCount = readI32(data, lodBase);
		const qint64 surfacesOffset = readI32(data, lodBase + 4);
		const qint64 lodEnd = readI32(data, lodBase + 8);
		if (surfaceCount < 0 || surfaceCount > kMaxSurfaces) {
			if (lod == 0) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR level of detail declares counts outside the supported range.");
				return;
			}
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "MDR level of detail %1 declares counts outside the supported range; later levels are not listed.").arg(lod);
			break;
		}
		if (lod > 0) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Level of detail %1: %2 surface(s), not decoded.").arg(lod).arg(surfaceCount);
		} else {
			qint64 surfaceBase = lodBase + surfacesOffset;
			for (int walked = 0; walked < surfaceCount; ++walked) {
				if (!work.step()) { return; }
				if (surfaceBase < kMdrHeaderBytes || !rangeOk(data, surfaceBase, kMdrSurfaceBytes)) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDR surface %1 lies outside the file.").arg(walked);
					return;
				}
				// mdrSurface_t
				const QString name = readFixedName(data, surfaceBase + 4, kMdrNameBytes);
				const QString shader = readFixedName(data, surfaceBase + 68, kMdrNameBytes);
				const int vertexCount = readI32(data, surfaceBase + 140);
				const qint64 vertexOffset = readI32(data, surfaceBase + 144);
				const int triangleCount = readI32(data, surfaceBase + 148);
				const qint64 triangleOffset = readI32(data, surfaceBase + 152);
				const qint64 surfaceEnd = readI32(data, surfaceBase + 164);
				if (surfaceEnd < kMdrSurfaceBytes || !rangeOk(data, surfaceBase, surfaceEnd)) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDR surface %1 declares an end offset outside the file.").arg(walked);
					return;
				}
				if (vertexCount <= 0 || vertexCount > kMaxSurfaceVertices || triangleCount < 0 || triangleCount > kMaxSurfaceTriangles) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDR surface %1 declares counts outside the supported range.").arg(walked);
					return;
				}
				totalVertices += vertexCount;
				if (totalVertices * qint64(frameCount) > kMaxVertexSlots) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR declares more frame vertices than can be decoded.");
					return;
				}
				const auto inRecord = [&](qint64 blockOffset, qint64 blockLength) {
					if (blockLength == 0) {
						return true;
					}
					if (blockOffset < kMdrSurfaceBytes || blockLength > surfaceEnd - blockOffset) {
						return false;
					}
					return rangeOk(data, surfaceBase + blockOffset, blockLength);
				};
				if (!inRecord(triangleOffset, qint64(triangleCount) * kMdrTriangleBytes) || !inRecord(vertexOffset, kMdrVertexBytes)) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDR surface %1 has a data block outside its record.").arg(walked);
					return;
				}

				ModelSurface surface;
				surface.index = mesh->surfaces.size();
				surface.name = name.isEmpty() ? QStringLiteral("surface%1").arg(surface.index) : name;
				surface.vertexCount = vertexCount;
				if (!shader.isEmpty()) {
					surface.skinPaths.append(shader);
					if (!skinNames.contains(shader)) {
						skinNames.insert(shader);
						mesh->skinPaths.append(shader);
					}
				}

				// mdrVertex_t, variable sized: normal, texCoords, numWeights,
				// then numWeights mdrWeight_t. RB_MDRSurfaceAnim rotates the
				// stored normal by each weighting bone exactly like the
				// offsets, so it is the normal in every bone's space.
				surface.texCoords.reserve(vertexCount);
				surface.skinning.first.reserve(vertexCount);
				surface.skinning.count.reserve(vertexCount);
				qint64 cursor = vertexOffset;
				for (int vertex = 0; vertex < vertexCount; ++vertex) {
					if (!work.step()) { return; }
					if (!inRecord(cursor, kMdrVertexBytes)) {
						mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of MDR surface %2 lies outside its record.").arg(vertex).arg(walked);
						return;
					}
					ModelVec3 normal;
					readVec3(data, surfaceBase + cursor, &normal);
					const ModelTexCoord texCoord{readF32(data, surfaceBase + cursor + 12), readF32(data, surfaceBase + cursor + 16)};
					const qint32 weightCount = readI32(data, surfaceBase + cursor + 20);
					if (weightCount < 0 || weightCount > kMaxInfluencesPerVertex
						|| !inRecord(cursor + kMdrVertexBytes, qint64(weightCount) * kMdrWeightBytes)) {
						mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of MDR surface %2 declares weights outside its record.").arg(vertex).arg(walked);
						return;
					}
					surface.texCoords.append(texCoord);
					surface.skinning.first.append(surface.skinning.influences.size());
					surface.skinning.count.append(weightCount);
					for (int weight = 0; weight < weightCount; ++weight) {
						const qint64 weightBase = surfaceBase + cursor + kMdrVertexBytes + (qint64(weight) * kMdrWeightBytes);
						ModelJointInfluence influence;
						influence.joint = readI32(data, weightBase);
						if (influence.joint < 0 || influence.joint >= boneCount) {
							mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Vertex %1 of MDR surface %2 follows bone %3, which does not exist.")
								.arg(vertex).arg(walked).arg(influence.joint);
							return;
						}
						// The renderer sums raw weights, so they are kept as stored.
						influence.weight = readF32(data, weightBase + 4);
						readVec3(data, weightBase + 8, &influence.offset);
						influence.normalOffset = normal;
						surface.skinning.influences.append(influence);
					}
					cursor += kMdrVertexBytes + (qint64(weightCount) * kMdrWeightBytes);
				}

				// mdrTriangle_t. The renderer draws MDR through the same back
				// end as MD3 (GL_FRONT culling, clockwise front faces), so the
				// order is reversed for counter-clockwise fronts.
				int skippedTriangles = 0;
				surface.triangles.reserve(triangleCount);
				for (int index = 0; index < triangleCount; ++index) {
					if (!work.step()) { return; }
					const qint64 base = surfaceBase + triangleOffset + (qint64(index) * kMdrTriangleBytes);
					const qint32 a = readI32(data, base);
					const qint32 b = readI32(data, base + 4);
					const qint32 c = readI32(data, base + 8);
					if (a < 0 || a >= vertexCount || b < 0 || b >= vertexCount || c < 0 || c >= vertexCount) {
						++skippedTriangles;
						continue;
					}
					surface.triangles.append(ModelTriangle{a, c, b});
				}
				if (skippedTriangles > 0) {
					surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.").arg(skippedTriangles);
				}
				mesh->surfaces.append(surface);
				surfaceBase += surfaceEnd;
			}
		}
		if (lod + 1 < lodCount) {
			if (lodEnd < kMdrLodBytes) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "MDR level of detail %1 declares an end offset outside the file; later levels are not listed.").arg(lod);
				break;
			}
			lodBase += lodEnd;
		}
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Levels of detail: %1; the first is decoded.").arg(lodCount);

	// Frame names live only in uncompressed frames; the baked frames are named
	// after the clip, so keep the stored names visible.
	bool namedFrames = false;
	for (const QString& frameName : frameNames) {
		if (!frameName.isEmpty()) {
			namedFrames = true;
			break;
		}
	}
	if (namedFrames) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Stored frame names: %1").arg(frameNames.join(QStringLiteral(", ")).left(512));
	}

	skeleton.clips.append(clip);
	ModelSkeletonBakeOptions options;
	options.includeBindPose = false;
	QString problem;
	if (!bakeModelSkeleton(mesh, options, &problem, control)) {
		mesh->error = problem.isEmpty() ? mesh->error : problem;
		if (mesh->error.isEmpty()) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDR skeleton could not be baked into frames.");
		}
		return;
	}
	mesh->skinCount = mesh->skinPaths.size();
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
	if (!mesh->geometryAvailable) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MDR holds no surfaces in its first level of detail.");
	}
}

} // namespace vibestudio::model_formats
