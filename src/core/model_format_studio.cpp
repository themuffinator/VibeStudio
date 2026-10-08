// GoldSrc studio models: Half-Life MDL (IDST 10), with the "<name>T.mdl" file
// that holds the textures of some models and the "<name>NN.mdl" sequence
// group files (IDSQ 10) that hold the animation of others.
//
// Layout: the studio format as publicly described by the Half-Life SDK header
// studio.h (studiohdr_t, studioseqhdr_t, mstudiobone_t, mstudiobodyparts_t,
// mstudiomodel_t, mstudiomesh_t, mstudiotexture_t, mstudioseqdesc_t,
// mstudioseqgroup_t, mstudioanim_t, mstudioanimvalue_t, mstudioattachment_t).
// The SDK's licence is not compatible with the GPL, so the header serves only
// as the description of the byte layout; no SDK code is copied here. Runtime
// behaviour was checked against the GPL-3.0 Xash3D FWGS sources
// (https://github.com/FWGS/xash3d-fwgs, master at 9137964147d8, reviewed
// 2026-10-08):
// public/xash3d_mathlib.c R_StudioCalcBones (animation value runs),
// public/xash3d_mathlib.h AngleQuaternion (Euler order), ref/gl/gl_studio.c
// R_StudioDrawPoints (triangle commands, texture coordinates, culling) and
// engine/common/mod_studio.c (Mod_LoadStudioModel, Mod_StudioTexName,
// R_StudioGetAnim: companion file names).
//
// Conventions decoded here:
// - Bones store their rest pose as value[0..2] (position) and value[3..5]
//   (Euler angles in radians); an animated channel adds its run-length decoded
//   value times scale[] to value[]. The angles are roll about X, pitch about Y
//   and yaw about Z, composed as q = yaw(Z) * pitch(Y) * roll(X).
// - Vertices and normals are stored in the space of the bone their vertinfo or
//   norminfo byte names, so each vertex has one influence with weight 1 whose
//   offset is the stored vertex; the joint bind is the rest pose.
// - Triangle commands are GL strips (positive count) and fans (negative). The
//   renderer culls GL_FRONT (gl_studio.c), so the triangles GL assembles from
//   the strips and fans are clockwise when seen from the front, the Quake
//   convention. They are reversed here to the studio's counter-clockwise fronts.
// - Texture coordinates are texels divided by the texture size, without the
//   half-texel offset Quake's alias models use (gl_studio.c multiplies by
//   1 / width). Chrome textures get theirs from the view at run time.
#include "core/model_formats_p.h"

#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QHash>
#include <QImage>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace vibestudio::model_formats {

namespace {

constexpr qint64 kHeaderBytes = 244;
constexpr qint64 kSequenceHeaderBytes = 76;
constexpr qint64 kBoneBytes = 112;
constexpr qint64 kSequenceBytes = 176;
constexpr qint64 kSequenceGroupBytes = 104;
constexpr qint64 kTextureBytes = 80;
constexpr qint64 kBodyPartBytes = 76;
constexpr qint64 kModelBytes = 112;
constexpr qint64 kMeshBytes = 20;
constexpr qint64 kAttachmentBytes = 88;
constexpr qint64 kAnimBytes = 12;
constexpr qint64 kPaletteBytes = 768;
constexpr qint64 kTriangleCommandBytes = 8;
constexpr int kStudioVersion = 10;

// GoldSrc's own limits (MAXSTUDIOBONES 128, MAXSTUDIOSKINS 256, MAXSTUDIOGROUPS
// 16, ...) with room to spare; anything past these is treated as damage.
constexpr int kMaxBones = 512;
constexpr int kMaxSequences = 4096;
constexpr int kMaxSequenceGroups = 100;
constexpr int kMaxTextures = 1024;
constexpr int kMaxSkinReferences = 1024;
constexpr int kMaxSkinFamilies = 256;
constexpr int kMaxBodyParts = 256;
constexpr int kMaxModelsPerPart = 256;
constexpr int kMaxMeshes = 4096;
constexpr int kMaxModelVertices = 65536;
constexpr int kMaxAttachments = 1024;
constexpr int kMaxControllers = 1024;
constexpr int kMaxHitboxes = 65536;
constexpr int kMaxTextureDimension = 4096;
constexpr qint64 kMaxTexturePixels = 64LL * 1024LL * 1024LL;
// Joint poses kept across every decoded sequence (48 bytes each).
constexpr qint64 kMaxClipMatrices = 1024LL * 1024LL;

constexpr quint32 kTextureChrome = 0x0002;      // STUDIO_NF_CHROME
constexpr quint32 kTextureMasked = 0x0040;      // STUDIO_NF_MASKED: index 255 is transparent
constexpr quint32 kTextureHalfFloatUv = 0x80000000u; // Xash3D STUDIO_NF_UV_COORDS: half-float UVs
constexpr int kSequenceLooping = 0x0001;        // STUDIO_LOOPING
constexpr int kMotionX = 0x0001;                // STUDIO_X, STUDIO_Y, STUDIO_Z
constexpr int kMotionY = 0x0002;
constexpr int kMotionZ = 0x0004;

QString formatCoordinate(double value)
{
	if (!std::isfinite(value)) {
		return QStringLiteral("0.00");
	}
	return QString::number(value, 'f', 2);
}

QString formatVector(const ModelVec3& value)
{
	return QStringLiteral("%1 %2 %3").arg(formatCoordinate(value.x), formatCoordinate(value.y), formatCoordinate(value.z));
}

QString hex(quint32 value)
{
	return QString::number(value, 16);
}

bool tableOk(const QByteArray& bytes, qint64 offset, int count, qint64 recordBytes)
{
	if (count < 0) {
		return false;
	}
	return count == 0 || rangeOk(bytes, offset, qint64(count) * recordBytes);
}

// IEEE 754 binary16 to float, for Xash3D's half-float texture coordinates.
float halfToFloat(quint16 half)
{
	const int sign = (half >> 15) & 1;
	const int exponent = (half >> 10) & 0x1F;
	const int mantissa = half & 0x3FF;
	double value = 0.0;
	if (exponent == 0) {
		value = std::ldexp(double(mantissa), -24);
	} else if (exponent == 31) {
		value = 0.0; // infinities and NaNs carry no usable coordinate
	} else {
		value = std::ldexp(double(mantissa | 0x400), exponent - 25);
	}
	return float(sign ? -value : value);
}

// GoldSrc's studio AngleQuaternion: angles[0] rolls about X, angles[1] pitches
// about Y and angles[2] yaws about Z, applied roll first: q = qz * qy * qx.
ModelQuat studioQuat(float roll, float pitch, float yaw)
{
	const double sr = std::sin(double(roll) * 0.5), cr = std::cos(double(roll) * 0.5);
	const double sp = std::sin(double(pitch) * 0.5), cp = std::cos(double(pitch) * 0.5);
	const double sy = std::sin(double(yaw) * 0.5), cy = std::cos(double(yaw) * 0.5);
	ModelQuat q;
	q.x = float(sr * cp * cy - cr * sp * sy);
	q.y = float(cr * sp * cy + sr * cp * sy);
	q.z = float(cr * cp * sy - sr * sp * cy);
	q.w = float(cr * cp * cy + sr * sp * sy);
	return q;
}

struct Header {
	QString name;
	qint64 length = 0;
	ModelVec3 eye;
	ModelVec3 hullMin;
	ModelVec3 hullMax;
	ModelVec3 clipMin;
	ModelVec3 clipMax;
	quint32 flags = 0;
	int bones = 0;
	qint64 boneOffset = 0;
	int controllers = 0;
	int hitboxes = 0;
	int sequences = 0;
	qint64 sequenceOffset = 0;
	int groups = 0;
	qint64 groupOffset = 0;
	int textures = 0;
	qint64 textureOffset = 0;
	int skinReferences = 0;
	int skinFamilies = 0;
	qint64 skinOffset = 0;
	int bodyParts = 0;
	qint64 bodyPartOffset = 0;
	int attachments = 0;
	qint64 attachmentOffset = 0;
};

// studiohdr_t, 244 bytes.
Header readHeader(const QByteArray& bytes)
{
	Header header;
	header.name = readFixedName(bytes, 8, 64);
	header.length = readI32(bytes, 72);
	readVec3(bytes, 76, &header.eye);
	readVec3(bytes, 88, &header.hullMin);
	readVec3(bytes, 100, &header.hullMax);
	readVec3(bytes, 112, &header.clipMin);
	readVec3(bytes, 124, &header.clipMax);
	header.flags = readU32(bytes, 136);
	header.bones = readI32(bytes, 140);
	header.boneOffset = readI32(bytes, 144);
	header.controllers = readI32(bytes, 148);
	header.hitboxes = readI32(bytes, 156);
	header.sequences = readI32(bytes, 164);
	header.sequenceOffset = readI32(bytes, 168);
	header.groups = readI32(bytes, 172);
	header.groupOffset = readI32(bytes, 176);
	header.textures = readI32(bytes, 180);
	header.textureOffset = readI32(bytes, 184);
	header.skinReferences = readI32(bytes, 192);
	header.skinFamilies = readI32(bytes, 196);
	header.skinOffset = readI32(bytes, 200);
	header.bodyParts = readI32(bytes, 204);
	header.bodyPartOffset = readI32(bytes, 208);
	header.attachments = readI32(bytes, 212);
	header.attachmentOffset = readI32(bytes, 216);
	return header;
}

struct Bone {
	QString name;
	int parent = -1;
	quint32 flags = 0;
	float value[6] = {0, 0, 0, 0, 0, 0};
	float scale[6] = {0, 0, 0, 0, 0, 0};
};

struct Texture {
	QString name;
	quint32 flags = 0;
	int width = 0;
	int height = 0;
};

struct TextureSet {
	QVector<Texture> textures;
	// Skin family 0: skin reference -> texture index, -1 when out of range.
	QVector<int> family;
	QVector<ModelEmbeddedSkin> skins;
	int families = 0;
	int references = 0;
};

// The texture table and skin families of `file`, which is the model itself or
// its texture file. mstudiotexture_t is 80 bytes; each texture's 8-bit pixels
// are followed by a 256-entry RGB palette.
bool readTextures(const QByteArray& file, const Header& header, TextureSet* out, QString* error, ModelWorkProgress& work)
{
	if (header.textures < 0 || header.textures > kMaxTextures || header.skinReferences < 0 || header.skinReferences > kMaxSkinReferences
		|| header.skinFamilies < 0 || header.skinFamilies > kMaxSkinFamilies) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life texture table declares counts outside the supported range.");
		return false;
	}
	if (!tableOk(file, header.textureOffset, header.textures, kTextureBytes)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life texture table lies outside the file.");
		return false;
	}
	const qint64 referenceCount = qint64(header.skinReferences) * qint64(header.skinFamilies);
	if (referenceCount > 0 && !rangeOk(file, header.skinOffset, referenceCount * 2)) {
		*error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life skin families lie outside the file.");
		return false;
	}
	TextureSet set;
	set.families = header.skinFamilies;
	set.references = header.skinReferences;
	qint64 totalPixels = 0;
	for (int index = 0; index < header.textures; ++index) {
		if (!work.step()) { return false; }
		const qint64 base = header.textureOffset + qint64(index) * kTextureBytes;
		Texture texture;
		texture.name = readFixedName(file, base, 64);
		texture.flags = readU32(file, base + 64);
		texture.width = readI32(file, base + 68);
		texture.height = readI32(file, base + 72);
		const qint64 pixelOffset = readI32(file, base + 76);
		if (texture.width <= 0 || texture.height <= 0 || texture.width > kMaxTextureDimension || texture.height > kMaxTextureDimension) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "Texture %1 (%2) has a size outside the supported range.").arg(index).arg(texture.name);
			return false;
		}
		const qint64 pixels = qint64(texture.width) * qint64(texture.height);
		if (!rangeOk(file, pixelOffset, pixels + kPaletteBytes)) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "The pixels of texture %1 (%2) lie outside the file.").arg(index).arg(texture.name);
			return false;
		}
		if (pixels > kMaxTexturePixels - totalPixels) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life textures exceed the 64-megapixel preview limit.");
			return false;
		}
		totalPixels += pixels;

		ModelEmbeddedSkin skin;
		skin.index = index;
		skin.name = texture.name;
		skin.groupFrameCount = 1;
		const QByteArray indexed = file.mid(qsizetype(pixelOffset), qsizetype(pixels));
		skin.indexedFrames.append(indexed);
		QImage image(texture.width, texture.height, QImage::Format_ARGB32);
		if (!image.isNull()) {
			const uchar* palette = reinterpret_cast<const uchar*>(file.constData() + pixelOffset + pixels);
			const uchar* source = reinterpret_cast<const uchar*>(indexed.constData());
			const bool masked = (texture.flags & kTextureMasked) != 0;
			for (int y = 0; y < texture.height; ++y) {
				if (!work.step()) { return false; }
				QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
				for (int x = 0; x < texture.width; ++x) {
					const int colour = source[qint64(y) * texture.width + x];
					const int alpha = masked && colour == 255 ? 0 : 255;
					row[x] = qRgba(palette[colour * 3], palette[colour * 3 + 1], palette[colour * 3 + 2], alpha);
				}
			}
		}
		skin.image = image;
		set.textures.append(texture);
		set.skins.append(skin);
	}
	if (header.skinFamilies > 0) {
		for (int reference = 0; reference < header.skinReferences; ++reference) {
			if (!work.step()) { return false; }
			const int texture = readI16(file, header.skinOffset + qint64(reference) * 2);
			set.family.append(texture >= 0 && texture < set.textures.size() ? texture : -1);
		}
	}
	*out = set;
	return true;
}

struct Sequence {
	QString label;
	float fps = 0.0f;
	int flags = 0;
	int frames = 0;
	int motionType = 0;
	int motionBone = -1;
	int blends = 0;
	qint64 animOffset = 0;
	int group = 0;
};

// Decodes the first blend of one sequence: `animOffset` is its mstudioanim_t
// array (one per bone) inside `file`. Each of a bone's six channels is either
// absent (offset 0: the rest value holds for every frame) or a list of
// mstudioanimvalue_t runs relative to the bone's mstudioanim_t: a header
// {valid, total} covering `total` frames followed by `valid` values; frames
// past `valid` repeat the last value.
bool decodeSequence(const QByteArray& file, const Sequence& sequence, int sequenceIndex, const QVector<Bone>& bones, const QVector<int>& parents,
	QVector<QVector<ModelJointMatrix>>* framesOut, QString* error, ModelWorkProgress& work)
{
	const int frameCount = sequence.frames;
	// Parent-relative poses first, converted to model space frame by frame.
	QVector<QVector<ModelJointMatrix>> frames(frameCount, QVector<ModelJointMatrix>(bones.size()));
	QVector<float> channels[6];
	for (int bone = 0; bone < bones.size(); ++bone) {
		if (!work.step()) { return false; }
		const Bone& info = bones.at(bone);
		const qint64 animBase = sequence.animOffset + qint64(bone) * kAnimBytes;
		for (int channel = 0; channel < 6; ++channel) {
			if (!work.step()) { return false; }
			QVector<float>& values = channels[channel];
			values.fill(info.value[channel], frameCount);
			const quint16 relative = readU16(file, animBase + channel * 2);
			if (relative == 0) {
				continue;
			}
			qint64 run = animBase + relative;
			int frameInRun = 0;
			for (int frame = 0; frame < frameCount; ++frame) {
				if (!work.step()) { return false; }
				int valid = 0;
				for (;;) {
					if (!work.step()) { return false; }
					if (!rangeOk(file, run, 2)) {
						*error = QCoreApplication::translate("VibeStudioModelMesh", "The animation values of sequence %1 (%2) run past the end of their file.")
							.arg(sequenceIndex).arg(sequence.label);
						return false;
					}
					valid = readU8(file, run);
					const int total = readU8(file, run + 1);
					if (total == 0) {
						*error = QCoreApplication::translate("VibeStudioModelMesh", "The animation of sequence %1 (%2) holds a value run that covers no frames.")
							.arg(sequenceIndex).arg(sequence.label);
						return false;
					}
					if (frameInRun < total) {
						break;
					}
					frameInRun -= total;
					run += 2 * (qint64(valid) + 1);
				}
				const qint64 at = run + 2 * (valid > frameInRun ? qint64(frameInRun) + 1 : qint64(valid));
				if (!rangeOk(file, at, 2)) {
					*error = QCoreApplication::translate("VibeStudioModelMesh", "The animation values of sequence %1 (%2) run past the end of their file.")
						.arg(sequenceIndex).arg(sequence.label);
					return false;
				}
				values[frame] = info.value[channel] + float(readI16(file, at)) * info.scale[channel];
				++frameInRun;
			}
		}
		// The renderer keeps the motion bone in place along the axes the
		// sequence moves the entity by (Xash3D gl_studio.c R_StudioCalcRotations).
		if (bone == sequence.motionBone) {
			if (sequence.motionType & kMotionX) { channels[0].fill(0.0f); }
			if (sequence.motionType & kMotionY) { channels[1].fill(0.0f); }
			if (sequence.motionType & kMotionZ) { channels[2].fill(0.0f); }
		}
		for (int frame = 0; frame < frameCount; ++frame) {
			if (!work.step()) { return false; }
			const ModelVec3 position{channels[0].at(frame), channels[1].at(frame), channels[2].at(frame)};
			frames[frame][bone] = modelJointMatrix(studioQuat(channels[3].at(frame), channels[4].at(frame), channels[5].at(frame)), position);
		}
	}
	for (int frame = 0; frame < frameCount; ++frame) {
		if (!work.step()) { return false; }
		frames[frame] = modelJointsToModelSpace(parents, frames.at(frame));
	}
	*framesOut = std::move(frames);
	return true;
}

QString uniqueJointName(const QString& wanted, int index, QSet<QString>* used)
{
	QString name = wanted.isEmpty() ? QStringLiteral("bone%1").arg(index) : wanted;
	if (used->contains(name.toLower())) {
		name = QStringLiteral("%1_%2").arg(name).arg(index);
	}
	while (used->contains(name.toLower())) {
		name += QLatin1Char('_');
	}
	used->insert(name.toLower());
	return name;
}

void decodeStudioModel(const QString& path, const QByteArray& fileBytes, ModelMesh* mesh, const ModelWorkControl& control,
	Companions& companions, const QHash<int, QByteArray>& preloadedGroups)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(fileBytes, 0, kHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life model header is truncated.");
		return;
	}
	mesh->version = readI32(fileBytes, 4);
	if (fileBytes.left(4) != QByteArrayLiteral("IDST") || mesh->version != kStudioVersion) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported Half-Life model version %1; only IDST version 10 is decoded.").arg(mesh->version);
		return;
	}
	const Header header = readHeader(fileBytes);
	// The engine copies `length` bytes of the file (Mod_LoadStudioModel), so
	// nothing past it belongs to the model and a longer length means damage.
	if (header.length < kHeaderBytes || header.length > qint64(fileBytes.size())) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life model declares a length of %1 bytes, but the file holds %2.")
			.arg(header.length).arg(fileBytes.size());
		return;
	}
	const QByteArray bytes = fileBytes.left(qsizetype(header.length));

	if (header.bones < 0 || header.bones > kMaxBones || header.controllers < 0 || header.controllers > kMaxControllers
		|| header.hitboxes < 0 || header.hitboxes > kMaxHitboxes || header.sequences < 0 || header.sequences > kMaxSequences
		|| header.groups < 0 || header.groups > kMaxSequenceGroups || header.bodyParts < 0 || header.bodyParts > kMaxBodyParts
		|| header.attachments < 0 || header.attachments > kMaxAttachments) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life model header declares counts outside the supported range.");
		return;
	}
	if (!tableOk(bytes, header.boneOffset, header.bones, kBoneBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life bone table lies outside the file.");
		return;
	}
	if (!tableOk(bytes, header.sequenceOffset, header.sequences, kSequenceBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life sequence table lies outside the file.");
		return;
	}
	if (!tableOk(bytes, header.groupOffset, header.groups, kSequenceGroupBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life sequence group table lies outside the file.");
		return;
	}
	if (!tableOk(bytes, header.bodyPartOffset, header.bodyParts, kBodyPartBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life body part table lies outside the file.");
		return;
	}
	if (!tableOk(bytes, header.attachmentOffset, header.attachments, kAttachmentBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life attachment table lies outside the file.");
		return;
	}

	if (!header.name.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Internal name: %1").arg(header.name);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Eye position: %1").arg(formatVector(header.eye));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Movement hull: %1 to %2").arg(formatVector(header.hullMin), formatVector(header.hullMax));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Clipping box: %1 to %2").arg(formatVector(header.clipMin), formatVector(header.clipMax));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Flags: 0x%1").arg(hex(header.flags));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Bones: %1").arg(header.bones);
	if (header.controllers > 0) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Bone controllers: %1 (shown at rest)").arg(header.controllers);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Hit boxes: %1").arg(header.hitboxes);

	// Bones (mstudiobone_t, 112 bytes): name[32], parent, flags,
	// bonecontroller[6], value[6], scale[6].
	QVector<Bone> bones;
	QVector<int> parents;
	QVector<ModelJointMatrix> restLocal;
	QSet<QString> usedNames;
	for (int index = 0; index < header.bones; ++index) {
		if (!work.step()) { return; }
		const qint64 base = header.boneOffset + qint64(index) * kBoneBytes;
		Bone bone;
		bone.name = readFixedName(bytes, base, 32);
		bone.parent = readI32(bytes, base + 32);
		bone.flags = readU32(bytes, base + 36);
		for (int channel = 0; channel < 6; ++channel) {
			bone.value[channel] = readF32(bytes, base + 64 + channel * 4);
			bone.scale[channel] = readF32(bytes, base + 88 + channel * 4);
			if (!std::isfinite(bone.value[channel]) || !std::isfinite(bone.scale[channel])) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Bone %1 (%2) has a value that is not a finite number.").arg(index).arg(bone.name);
				return;
			}
		}
		if (bone.parent < -1 || bone.parent >= index) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Bone %1 (%2) names a parent that does not come before it.").arg(index).arg(bone.name);
			return;
		}
		bones.append(bone);
		parents.append(bone.parent);
		restLocal.append(modelJointMatrix(studioQuat(bone.value[3], bone.value[4], bone.value[5]), ModelVec3{bone.value[0], bone.value[1], bone.value[2]}));

		ModelJoint joint;
		joint.name = uniqueJointName(bone.name, index, &usedNames);
		joint.parent = bone.parent;
		joint.flags = bone.flags;
		mesh->skeleton.joints.append(joint);
	}
	const QVector<ModelJointMatrix> restModel = modelJointsToModelSpace(parents, restLocal);
	if (restModel.size() != bones.size()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life bone hierarchy could not be resolved.");
		return;
	}
	for (int index = 0; index < bones.size(); ++index) {
		mesh->skeleton.joints[index].bind = restModel.at(index);
	}

	// Textures: in the model, or in "<name>T.mdl" beside it when the model
	// holds none (Mod_StudioTexName).
	TextureSet textures;
	QString textureSource = path;
	bool texturesAvailable = false;
	if (header.textures > 0) {
		QString problem;
		if (!readTextures(bytes, header, &textures, &problem, work)) {
			if (mesh->error.isEmpty()) { mesh->error = problem; }
			return;
		}
		texturesAvailable = true;
	} else if (header.bodyParts > 0) {
		const QString texturePath = joinPath(pathDirectory(path), pathStem(path) + QStringLiteral("T.mdl"));
		textureSource = texturePath;
		QByteArray textureFile;
		QString problem;
		if (!companions.canRead() || !companions.read(texturePath, &textureFile, &problem)) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The textures live in %1, which could not be read; the model is shown untextured.").arg(texturePath);
		} else if (!rangeOk(textureFile, 0, kHeaderBytes) || textureFile.left(4) != QByteArrayLiteral("IDST") || readI32(textureFile, 4) != kStudioVersion
			|| readI32(textureFile, 72) < kHeaderBytes || readI32(textureFile, 72) > textureFile.size()) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The texture file %1 is not a Half-Life texture file; the model is shown untextured.").arg(texturePath);
		} else {
			const QByteArray trimmed = textureFile.left(readI32(textureFile, 72));
			if (!readTextures(trimmed, readHeader(trimmed), &textures, &problem, work)) {
				if (!mesh->error.isEmpty()) { return; }
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The texture file %1 could not be decoded (%2); the model is shown untextured.").arg(texturePath, problem);
				textures = TextureSet{};
			} else {
				texturesAvailable = true;
			}
		}
	}
	if (texturesAvailable) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Textures: %1 from %2").arg(textures.textures.size()).arg(textureSource);
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skin families: %1 of %2 reference(s); family 0 is shown.").arg(textures.families).arg(textures.references);
		for (int index = 0; index < textures.textures.size(); ++index) {
			const Texture& texture = textures.textures.at(index);
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Texture %1 (%2): %3 x %4, flags 0x%5")
				.arg(index).arg(texture.name).arg(texture.width).arg(texture.height).arg(hex(texture.flags));
			if (texture.flags & kTextureChrome) {
				mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Texture %1 (%2) is chrome: the game generates its texture coordinates at run time.")
					.arg(index).arg(texture.name);
			}
		}
		mesh->embeddedSkins = textures.skins;
		for (const Texture& texture : textures.textures) {
			mesh->skinPaths.append(texture.name);
		}
		mesh->skinCount = int(textures.textures.size());
	}

	// Body parts (mstudiobodyparts_t, 76 bytes): name[64], nummodels, base,
	// modelindex. Body value 0 selects model 0 of every part.
	for (int part = 0; part < header.bodyParts; ++part) {
		if (!work.step()) { return; }
		const qint64 partBase = header.bodyPartOffset + qint64(part) * kBodyPartBytes;
		const QString partName = readFixedName(bytes, partBase, 64);
		const int modelCount = readI32(bytes, partBase + 64);
		const qint64 modelOffset = readI32(bytes, partBase + 72);
		if (modelCount < 0 || modelCount > kMaxModelsPerPart || !tableOk(bytes, modelOffset, modelCount, kModelBytes)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The models of body part %1 (%2) lie outside the file.").arg(part).arg(partName);
			return;
		}
		if (modelCount == 0) {
			continue;
		}
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Body part %1 (%2): %3 model(s); model 0 (%4) is shown.")
			.arg(part).arg(partName).arg(modelCount).arg(readFixedName(bytes, modelOffset, 64));
		for (int other = 1; other < modelCount; ++other) {
			if (!work.step()) { return; }
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Body part %1 (%2) also holds model %3 (%4).")
				.arg(part).arg(partName).arg(other).arg(readFixedName(bytes, modelOffset + qint64(other) * kModelBytes, 64));
		}

		// mstudiomodel_t, 112 bytes.
		const qint64 modelBase = modelOffset;
		const QString modelName = readFixedName(bytes, modelBase, 64);
		const int meshCount = readI32(bytes, modelBase + 72);
		const qint64 meshOffset = readI32(bytes, modelBase + 76);
		const int vertexCount = readI32(bytes, modelBase + 80);
		const qint64 vertexBoneOffset = readI32(bytes, modelBase + 84);
		const qint64 vertexOffset = readI32(bytes, modelBase + 88);
		const int normalCount = readI32(bytes, modelBase + 92);
		const qint64 normalBoneOffset = readI32(bytes, modelBase + 96);
		const qint64 normalOffset = readI32(bytes, modelBase + 100);
		if (meshCount < 0 || meshCount > kMaxMeshes || vertexCount < 0 || vertexCount > kMaxModelVertices || normalCount < 0 || normalCount > kMaxModelVertices) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Model %1 of body part %2 declares counts outside the supported range.").arg(modelName).arg(part);
			return;
		}
		if (!tableOk(bytes, meshOffset, meshCount, kMeshBytes) || !tableOk(bytes, vertexBoneOffset, vertexCount, 1) || !tableOk(bytes, vertexOffset, vertexCount, 12)
			|| !tableOk(bytes, normalBoneOffset, normalCount, 1) || !tableOk(bytes, normalOffset, normalCount, 12)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A data block of model %1 lies outside the file.").arg(modelName);
			return;
		}
		if ((vertexCount > 0 || normalCount > 0) && bones.isEmpty()) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The model's vertices follow bones, but it has none.");
			return;
		}
		QVector<ModelVec3> vertices(vertexCount);
		QVector<int> vertexBones(vertexCount);
		for (int index = 0; index < vertexCount; ++index) {
			if (!work.step()) { return; }
			vertexBones[index] = readU8(bytes, vertexBoneOffset + index);
			readVec3(bytes, vertexOffset + qint64(index) * 12, &vertices[index]);
			if (vertexBones.at(index) >= bones.size()) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A vertex of model %1 follows bone %2, which does not exist.").arg(modelName).arg(vertexBones.at(index));
				return;
			}
			if (!isFinite(vertices.at(index))) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A vertex of model %1 is not a finite number.").arg(modelName);
				return;
			}
		}
		QVector<ModelVec3> normals(normalCount);
		QVector<int> normalBones(normalCount);
		for (int index = 0; index < normalCount; ++index) {
			if (!work.step()) { return; }
			normalBones[index] = readU8(bytes, normalBoneOffset + index);
			readVec3(bytes, normalOffset + qint64(index) * 12, &normals[index]);
			if (normalBones.at(index) >= bones.size()) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A normal of model %1 follows bone %2, which does not exist.").arg(modelName).arg(normalBones.at(index));
				return;
			}
			if (!isFinite(normals.at(index))) {
				normals[index] = ModelVec3{0.0f, 0.0f, 0.0f};
			}
		}

		// mstudiomesh_t, 20 bytes: numtris, triindex, skinref, numnorms, normindex.
		for (int meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
			if (!work.step()) { return; }
			const qint64 meshBase = meshOffset + qint64(meshIndex) * kMeshBytes;
			const qint64 commandOffset = readI32(bytes, meshBase + 4);
			const int skinReference = readI32(bytes, meshBase + 8);

			ModelSurface surface;
			surface.index = int(mesh->surfaces.size());
			surface.name = QStringLiteral("%1/%2/%3").arg(partName, modelName).arg(meshIndex);
			const Texture* texture = nullptr;
			if (texturesAvailable) {
				int textureIndex = -1;
				if (!textures.family.isEmpty()) {
					textureIndex = skinReference >= 0 && skinReference < textures.family.size() ? textures.family.at(skinReference) : -1;
				} else if (skinReference >= 0 && skinReference < textures.textures.size()) {
					textureIndex = skinReference;
				}
				if (textureIndex >= 0) {
					texture = &textures.textures.at(textureIndex);
					surface.skinPaths.append(texture->name);
				} else {
					surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "The mesh names skin reference %1, which the model does not have.").arg(skinReference);
				}
			}
			if (!texture) {
				surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "The mesh's texture is not available, so its texture coordinates stay in texels.");
			}
			const bool halfFloatUv = texture && (texture->flags & kTextureHalfFloatUv) != 0;
			const float width = texture ? float(texture->width) : 1.0f;
			const float height = texture ? float(texture->height) : 1.0f;

			// Corners are unique (vertex, normal, s, t) tuples.
			QHash<quint64, int> cornerForKey;
			QVector<int> cornerVertex;
			QVector<int> cornerNormal;
			QVector<int> command;
			QVector<int> commandVertex;
			qint64 cursor = commandOffset;
			for (;;) {
				if (!work.step()) { return; }
				if (!rangeOk(bytes, cursor, 2)) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The triangle commands of mesh %1 run past the end of the file.").arg(surface.name);
					return;
				}
				const int count = readI16(bytes, cursor);
				cursor += 2;
				if (count == 0) {
					break;
				}
				const bool fan = count < 0;
				const int length = std::abs(count);
				if (!rangeOk(bytes, cursor, qint64(length) * kTriangleCommandBytes)) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The triangle commands of mesh %1 run past the end of the file.").arg(surface.name);
					return;
				}
				command.clear();
				commandVertex.clear();
				for (int entry = 0; entry < length; ++entry) {
					if (!work.step()) { return; }
					const qint64 at = cursor + qint64(entry) * kTriangleCommandBytes;
					const int vertex = readI16(bytes, at);
					const int normal = readI16(bytes, at + 2);
					const quint16 s = readU16(bytes, at + 4);
					const quint16 t = readU16(bytes, at + 6);
					if (vertex < 0 || vertex >= vertexCount || normal < 0 || normal >= normalCount) {
						mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A triangle command of mesh %1 names a vertex or normal outside its model.").arg(surface.name);
						return;
					}
					const quint64 key = quint64(quint16(vertex)) | (quint64(quint16(normal)) << 16) | (quint64(s) << 32) | (quint64(t) << 48);
					auto found = cornerForKey.constFind(key);
					int corner = 0;
					if (found != cornerForKey.constEnd()) {
						corner = found.value();
					} else {
						corner = int(cornerVertex.size());
						if (corner >= kMaxSurfaceVertices) {
							mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Mesh %1 holds more corners or triangles than can be decoded.").arg(surface.name);
							return;
						}
						cornerForKey.insert(key, corner);
						cornerVertex.append(vertex);
						cornerNormal.append(normal);
						ModelTexCoord coord;
						if (halfFloatUv) {
							coord.u = halfToFloat(s);
							coord.v = halfToFloat(t);
						} else {
							coord.u = float(qint16(s)) / width;
							coord.v = float(qint16(t)) / height;
						}
						surface.texCoords.append(coord);
					}
					command.append(corner);
					commandVertex.append(vertex);
				}
				cursor += qint64(length) * kTriangleCommandBytes;
				for (int k = 0; k + 2 < command.size(); ++k) {
					if (!work.step()) { return; }
					// The triangle GL assembles, then reversed to counter-clockwise.
					int a = 0, b = 0, c = 0;
					if (fan) {
						a = 0; b = k + 1; c = k + 2;
					} else if ((k & 1) == 0) {
						a = k; b = k + 1; c = k + 2;
					} else {
						a = k + 1; b = k; c = k + 2;
					}
					if (commandVertex.at(a) == commandVertex.at(b) || commandVertex.at(b) == commandVertex.at(c) || commandVertex.at(a) == commandVertex.at(c)) {
						continue;
					}
					if (surface.triangles.size() >= kMaxSurfaceTriangles) {
						mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Mesh %1 holds more corners or triangles than can be decoded.").arg(surface.name);
						return;
					}
					ModelTriangle triangle;
					triangle.a = command.at(a);
					triangle.b = command.at(c);
					triangle.c = command.at(b);
					surface.triangles.append(triangle);
				}
			}
			if (surface.triangles.isEmpty()) {
				continue;
			}

			surface.vertexCount = int(cornerVertex.size());
			ModelSurfaceSkinning& skinning = surface.skinning;
			skinning.first.reserve(surface.vertexCount);
			skinning.count.reserve(surface.vertexCount);
			skinning.influences.reserve(surface.vertexCount);
			for (int corner = 0; corner < surface.vertexCount; ++corner) {
				if (!work.step()) { return; }
				const int vertex = cornerVertex.at(corner);
				const int normal = cornerNormal.at(corner);
				const int bone = vertexBones.at(vertex);
				ModelJointInfluence influence;
				influence.joint = bone;
				influence.weight = 1.0f;
				influence.offset = vertices.at(vertex);
				influence.normalOffset = normals.at(normal);
				if (normalBones.at(normal) != bone) {
					// The normal follows another bone: carry it into the
					// vertex's bone space through the rest pose.
					bool invertible = true;
					const ModelJointMatrix inverse = modelJointInverse(restModel.at(bone), &invertible);
					const ModelVec3 modelNormal = modelJointTransformVector(restModel.at(normalBones.at(normal)), normals.at(normal));
					influence.normalOffset = invertible ? modelJointTransformVector(inverse, modelNormal) : ModelVec3{0.0f, 0.0f, 0.0f};
				}
				skinning.first.append(int(skinning.influences.size()));
				skinning.count.append(1);
				skinning.influences.append(influence);
			}
			if (mesh->surfaces.size() >= kMaxSurfaces) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life model holds more meshes than can be decoded.");
				return;
			}
			mesh->surfaces.append(surface);
		}
	}

	// Attachments (mstudioattachment_t, 88 bytes): name[32], type, bone,
	// org[3], vectors[3][3]. Only org places the attachment.
	for (int index = 0; index < header.attachments; ++index) {
		if (!work.step()) { return; }
		const qint64 base = header.attachmentOffset + qint64(index) * kAttachmentBytes;
		const QString name = readFixedName(bytes, base, 32);
		const int bone = readI32(bytes, base + 36);
		ModelVec3 origin;
		readVec3(bytes, base + 40, &origin);
		if (bone < 0 || bone >= bones.size()) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Attachment %1 follows bone %2, which does not exist, and was left out.").arg(index).arg(bone);
			continue;
		}
		if (!isFinite(origin)) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Attachment %1 has a position that is not a finite number and was left out.").arg(index);
			continue;
		}
		ModelSkeletalTag tag;
		tag.name = name.isEmpty() ? QStringLiteral("attachment%1").arg(index) : name;
		tag.joint = bone;
		tag.offset = modelJointMatrix(ModelQuat{}, origin);
		mesh->skeleton.tags.append(tag);
	}
	if (header.attachments > 0) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Attachments: %1").arg(header.attachments);
	}

	// Sequences (mstudioseqdesc_t, 176 bytes) and their groups
	// (mstudioseqgroup_t, 104 bytes). Group 0 is the model itself; group N
	// lives in "<name>NN.mdl" (R_StudioGetAnim), whose IDSQ header is followed
	// by animation addressed from the start of that file.
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Sequences: %1 in %2 group(s)").arg(header.sequences).arg(header.groups);
	QHash<int, QByteArray> groupFiles;
	QHash<int, QString> groupPaths;
	QSet<int> missingGroups;
	QSet<int> invalidGroups;
	QHash<int, int> skippedInGroup;
	qint64 matrixBudget = kMaxClipMatrices;
	int frameBudget = kMaxFrames - 1;
	int blendedSequences = 0;
	int budgetSkipped = 0;
	if (!bones.isEmpty()) {
		for (int index = 0; index < header.sequences; ++index) {
			if (!work.step()) { return; }
			const qint64 base = header.sequenceOffset + qint64(index) * kSequenceBytes;
			Sequence sequence;
			sequence.label = readFixedName(bytes, base, 32);
			sequence.fps = readF32(bytes, base + 32);
			sequence.flags = readI32(bytes, base + 36);
			sequence.frames = readI32(bytes, base + 56);
			sequence.motionType = readI32(bytes, base + 68);
			sequence.motionBone = readI32(bytes, base + 72);
			sequence.blends = readI32(bytes, base + 120);
			sequence.animOffset = readI32(bytes, base + 124);
			sequence.group = readI32(bytes, base + 156);
			const QString clipName = sequence.label.isEmpty() ? QStringLiteral("sequence%1").arg(index) : sequence.label;
			if (sequence.frames <= 0) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Sequence %1 (%2) has no frames and was left out.").arg(index).arg(clipName);
				continue;
			}
			if (sequence.frames > kMaxFrames) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Sequence %1 (%2) declares more frames than can be decoded.").arg(index).arg(clipName);
				return;
			}
			if (sequence.group < 0 || sequence.group >= header.groups) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Sequence %1 (%2) names sequence group %3, which the model does not have, and was left out.")
					.arg(index).arg(clipName).arg(sequence.group);
				continue;
			}
			if (sequence.blends > 1) {
				++blendedSequences;
			}
			const qint64 matrices = qint64(sequence.frames) * bones.size();
			if (sequence.frames > frameBudget || matrices > matrixBudget) {
				++budgetSkipped;
				continue;
			}

			const QByteArray* source = &bytes;
			QString sourcePath;
			if (sequence.group > 0) {
				if (missingGroups.contains(sequence.group)) {
					skippedInGroup[sequence.group] += 1;
					continue;
				}
				if (!groupFiles.contains(sequence.group)) {
					const QString groupPath = joinPath(pathDirectory(path), pathStem(path) + QStringLiteral("%1.mdl").arg(sequence.group, 2, 10, QLatin1Char('0')));
					groupPaths.insert(sequence.group, groupPath);
					QByteArray groupFile;
					bool loaded = false;
					if (preloadedGroups.contains(sequence.group)) {
						groupFile = preloadedGroups.value(sequence.group);
						loaded = true;
					} else if (companions.canRead()) {
						QString problem;
						loaded = companions.read(groupPath, &groupFile, &problem);
						if (!loaded && companions.canRead()) {
							// The group's stored name is game-relative ("models/x01.mdl").
							const QString storedName = readFixedName(bytes, header.groupOffset + qint64(sequence.group) * kSequenceGroupBytes + 32, 64);
							if (!storedName.isEmpty() && storedName.compare(groupPath, Qt::CaseInsensitive) != 0) {
								loaded = companions.read(storedName, &groupFile, &problem);
								if (loaded) {
									groupPaths.insert(sequence.group, storedName);
								}
							}
						}
					}
					if (!loaded) {
						missingGroups.insert(sequence.group);
						skippedInGroup[sequence.group] += 1;
						continue;
					}
					if (!rangeOk(groupFile, 0, kSequenceHeaderBytes) || groupFile.left(4) != QByteArrayLiteral("IDSQ") || readI32(groupFile, 4) != kStudioVersion) {
						mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The sequence group file %1 is not a Half-Life sequence group (IDSQ 10); its sequences were left out.")
							.arg(groupPaths.value(sequence.group));
						missingGroups.insert(sequence.group);
						invalidGroups.insert(sequence.group);
						continue;
					}
					groupFiles.insert(sequence.group, groupFile);
				}
				source = &groupFiles[sequence.group];
				sourcePath = groupPaths.value(sequence.group);
			}
			// Damage in the model itself is an error; damage in a sequence
			// group file only loses that file's sequences.
			if (!tableOk(*source, sequence.animOffset, int(bones.size()), kAnimBytes)) {
				const QString problem = QCoreApplication::translate("VibeStudioModelMesh", "The animation of sequence %1 (%2) lies outside its file.").arg(index).arg(clipName);
				if (sequence.group == 0) {
					mesh->error = problem;
					return;
				}
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 The sequence was left out.").arg(problem);
				continue;
			}
			sequence.label = clipName;
			ModelSkeletalClip clip;
			clip.name = clipName;
			clip.sourcePath = sourcePath;
			clip.framesPerSecond = std::isfinite(sequence.fps) && sequence.fps > 0.0f ? double(sequence.fps) : 0.0;
			clip.loops = (sequence.flags & kSequenceLooping) != 0;
			QString problem;
			if (!decodeSequence(*source, sequence, index, bones, parents, &clip.frames, &problem, work)) {
				if (!mesh->error.isEmpty()) {
					return;
				}
				if (sequence.group == 0) {
					mesh->error = problem;
					return;
				}
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 The sequence was left out.").arg(problem);
				continue;
			}
			matrixBudget -= matrices;
			frameBudget -= sequence.frames;
			mesh->skeleton.clips.append(clip);
		}
	}
	for (auto it = skippedInGroup.constBegin(); it != skippedInGroup.constEnd(); ++it) {
		if (groupFiles.contains(it.key()) || invalidGroups.contains(it.key())) {
			continue;
		}
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The sequence group file %1 could not be read; %2 sequence(s) in it were left out.")
			.arg(groupPaths.value(it.key())).arg(it.value());
	}
	if (blendedSequences > 0) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "%1 sequence(s) blend several animations; the first blend is shown.").arg(blendedSequences);
	}
	if (budgetSkipped > 0) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 sequence(s) were left out to keep the decoded animation within %2 frames and %3 joint poses.")
			.arg(budgetSkipped).arg(kMaxFrames).arg(kMaxClipMatrices);
	}

	if (bones.isEmpty()) {
		if (mesh->embeddedSkins.isEmpty()) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life model holds neither bones nor textures.");
			return;
		}
		// A "<name>T.mdl" texture file opened on its own.
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "This file holds textures only; the model that uses it holds the geometry.");
		return;
	}

	mesh->skeleton.sourceFormat = QStringLiteral("studio");
	ModelSkeletonBakeOptions options;
	options.includeBindPose = true;
	QString problem;
	if (!bakeModelSkeleton(mesh, options, &problem, control)) {
		if (mesh->error.isEmpty()) {
			mesh->error = problem;
		}
		return;
	}
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
}

} // namespace

void decodeHalfLifeMdl(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control, Companions& companions)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(bytes, 0, 8)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life model header is truncated.");
		return;
	}
	if (bytes.left(4) != QByteArrayLiteral("IDSQ")) {
		decodeStudioModel(path, bytes, mesh, control, companions, {});
		return;
	}

	// A sequence group file (studioseqhdr_t: id, version, name[64], length)
	// holds only animation for the model "<name>.mdl" it is named after; that
	// model is decoded in its place, with this file as its group NN.
	if (!rangeOk(bytes, 0, kSequenceHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Half-Life sequence group header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != kStudioVersion) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported Half-Life sequence group version %1; only IDSQ version 10 is decoded.").arg(mesh->version);
		return;
	}
	const QString stem = pathStem(path);
	if (stem.size() < 3 || !stem.at(stem.size() - 1).isDigit() || !stem.at(stem.size() - 2).isDigit()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "This Half-Life sequence group file is not named <model>NN.mdl, so its model cannot be found.");
		return;
	}
	const int group = stem.right(2).toInt();
	const QString modelPath = joinPath(pathDirectory(path), stem.left(stem.size() - 2) + QStringLiteral(".mdl"));
	QByteArray modelBytes;
	QString problem;
	if (!companions.canRead() || !companions.read(modelPath, &modelBytes, &problem)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "This file holds only animation for the Half-Life model %1, which could not be read.").arg(modelPath);
		return;
	}
	if (modelBytes.left(4) != QByteArrayLiteral("IDST")) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "This file holds only animation for %1, which is not a Half-Life model.").arg(modelPath);
		return;
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "This file is sequence group %1 of %2; that model is shown with its animation.").arg(group).arg(modelPath);
	QHash<int, QByteArray> preloaded;
	if (group > 0) {
		preloaded.insert(group, bytes);
	}
	decodeStudioModel(modelPath, modelBytes, mesh, control, companions, preloaded);
}

} // namespace vibestudio::model_formats
