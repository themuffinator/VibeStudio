// Inter-Quake Model (IQM version 2): decoding and writing.
//
// Layouts follow iqmheader, iqmmesh, iqmvertexarray, iqmtriangle, iqmjoint,
// iqmpose, iqmanim and iqmbounds in the IQM specification (iqm.txt and iqm.h
// by Lee Salzman, https://github.com/lsalzman/iqm). Loading follows ioquake3's
// R_LoadIQM, ComputePoseMats and RB_IQMSurfaceAnim
// (code/renderergl1/tr_model_iqm.c, GPL).
//
// Conventions, from the specification and ioquake3:
// - Positions are used as stored: ioquake3 draws them in Quake's Z-up game
//   space without conversion, and the reference Blender exporter writes
//   Blender's Z-up coordinates unchanged.
// - Triangles wind clockwise: the reference exporter turns Blender's
//   counter-clockwise faces into (v0, v[i], v[i-1]) and ioquake3 draws them
//   through the MD3 back end (GL_FRONT culling). They are reversed on import
//   and again on export.
// - Texture coordinates use v = 0 at the top of the image (the exporter writes
//   1 - v), like MD3 STs, so they are kept as stored.
// - Joints and poses are parent-relative translate / rotate (x, y, z, w) /
//   scale with output = (input * scale) * rotation + translation, i.e.
//   M = T * R * S with the scale applied to R's columns as the reference
//   Matrix3x3(rotation, scale) does. ioquake3's JointToMatrix scales the rows
//   instead, which only differs for non-uniform scale; the specification is
//   followed here.
// - Vertices are stored in the bind pose and expressed in each joint's bind
//   space on import (modelSkinningFromBindPose); a vertex whose blend weights
//   are all zero is left in place by ioquake3, so a mesh whose every vertex
//   has no weight stays unskinned.

#include "core/model_iqm.h"

#include "core/model_formats_p.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace vibestudio {

namespace {

constexpr qint64 kIqmHeaderBytes = 124;
constexpr qint64 kIqmMeshBytes = 24;
constexpr qint64 kIqmVertexArrayBytes = 20;
constexpr qint64 kIqmTriangleBytes = 12;
constexpr qint64 kIqmJointBytes = 48;
constexpr qint64 kIqmPoseBytes = 88;
constexpr qint64 kIqmAnimBytes = 20;
constexpr qint64 kIqmBoundsBytes = 32;
constexpr int kIqmMaxVertexArrays = 256;
constexpr int kIqmMaxComponents = 16;
constexpr int kIqmMaxNameBytes = 1024;
// Frames times joints held in memory at once, on import and export.
constexpr qint64 kIqmMaxPoseSlots = 2LL * 1024LL * 1024LL;
constexpr quint32 kIqmLoop = 1u;
constexpr quint32 kIqmFullChannelMask = 0x3FFu;

// iqm.txt vertex array types and component formats.
enum IqmArrayType : quint32 {
	IqmPosition = 0,
	IqmTexCoord = 1,
	IqmNormal = 2,
	IqmTangent = 3,
	IqmBlendIndexes = 4,
	IqmBlendWeights = 5,
	IqmColor = 6,
	IqmCustom = 0x10,
};

enum IqmFormat : quint32 {
	IqmByte = 0,
	IqmUByte = 1,
	IqmShort = 2,
	IqmUShort = 3,
	IqmInt = 4,
	IqmUInt = 5,
	IqmHalf = 6,
	IqmFloat = 7,
	IqmDouble = 8,
};

int iqmFormatBytes(quint32 format)
{
	switch (format) {
	case IqmByte:
	case IqmUByte:
		return 1;
	case IqmShort:
	case IqmUShort:
	case IqmHalf:
		return 2;
	case IqmInt:
	case IqmUInt:
	case IqmFloat:
		return 4;
	case IqmDouble:
		return 8;
	default:
		return 0;
	}
}

bool iqmFormatIsReal(quint32 format)
{
	return format == IqmHalf || format == IqmFloat || format == IqmDouble;
}

bool iqmFormatIsInteger(quint32 format)
{
	return format <= IqmUInt;
}

QString iqmFormatName(quint32 format)
{
	static const char* const kNames[] = {"byte", "ubyte", "short", "ushort", "int", "uint", "half", "float", "double"};
	return format <= IqmDouble ? QString::fromLatin1(kNames[format]) : QStringLiteral("format%1").arg(format);
}

QString iqmTypeName(quint32 type)
{
	static const char* const kNames[] = {"position", "texcoord", "normal", "tangent", "blendindexes", "blendweights", "color"};
	if (type <= IqmColor) {
		return QString::fromLatin1(kNames[type]);
	}
	return type >= IqmCustom ? QStringLiteral("custom") : QStringLiteral("type%1").arg(type);
}

float iqmHalfToFloat(quint16 half)
{
	const int exponent = (half >> 10) & 0x1F;
	const int mantissa = half & 0x3FF;
	float value = 0.0f;
	if (exponent == 0) {
		value = std::ldexp(float(mantissa), -24);
	} else if (exponent == 31) {
		value = mantissa != 0 ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
	} else {
		value = std::ldexp(float(mantissa | 0x400), exponent - 25);
	}
	return (half & 0x8000) != 0 ? -value : value;
}

double iqmReadComponent(const QByteArray& bytes, qint64 offset, quint32 format)
{
	using namespace model_formats;
	switch (format) {
	case IqmByte:
		return readI8(bytes, offset);
	case IqmUByte:
		return readU8(bytes, offset);
	case IqmShort:
		return readI16(bytes, offset);
	case IqmUShort:
		return readU16(bytes, offset);
	case IqmInt:
		return readI32(bytes, offset);
	case IqmUInt:
		return readU32(bytes, offset);
	case IqmHalf:
		return iqmHalfToFloat(readU16(bytes, offset));
	case IqmFloat:
		return readF32(bytes, offset);
	case IqmDouble: {
		const quint64 raw = quint64(readU32(bytes, offset)) | (quint64(readU32(bytes, offset + 4)) << 32);
		double value = 0.0;
		std::memcpy(&value, &raw, sizeof(value));
		return value;
	}
	default:
		return 0.0;
	}
}

// The largest value of an unsigned weight format, which reads as weight 1.
double iqmWeightScale(quint32 format)
{
	switch (format) {
	case IqmUByte:
		return 255.0;
	case IqmUShort:
		return 65535.0;
	case IqmUInt:
		return 4294967295.0;
	default:
		return 1.0;
	}
}

struct IqmHeader {
	quint32 version = 0, fileSize = 0, flags = 0;
	quint32 numText = 0, ofsText = 0;
	quint32 numMeshes = 0, ofsMeshes = 0;
	quint32 numVertexArrays = 0, numVertexes = 0, ofsVertexArrays = 0;
	quint32 numTriangles = 0, ofsTriangles = 0, ofsAdjacency = 0;
	quint32 numJoints = 0, ofsJoints = 0;
	quint32 numPoses = 0, ofsPoses = 0;
	quint32 numAnims = 0, ofsAnims = 0;
	quint32 numFrames = 0, numFrameChannels = 0, ofsFrames = 0, ofsBounds = 0;
	quint32 numComment = 0, ofsComment = 0;
	quint32 numExtensions = 0, ofsExtensions = 0;
};

struct IqmArray {
	bool present = false;
	quint32 format = 0;
	int size = 0;
	qint64 offset = 0;
};

// A NUL-terminated string at `index` in the text block; false when the index
// lies outside it.
bool iqmText(const QByteArray& data, const IqmHeader& header, quint32 index, QString* out)
{
	if (index >= header.numText) {
		return false;
	}
	QByteArray name;
	const qint64 end = qint64(header.ofsText) + qint64(header.numText);
	for (qint64 at = qint64(header.ofsText) + index; at < end && name.size() < kIqmMaxNameBytes; ++at) {
		const char ch = data.at(qsizetype(at));
		if (ch == '\0') {
			break;
		}
		name.append(ch);
	}
	*out = QString::fromUtf8(name).trimmed();
	return true;
}

// Unique, non-empty joint names (validateModelSkeleton compares them without case).
QString iqmUniqueJointName(const QString& wanted, int index, QSet<QString>* used)
{
	QString name = wanted.isEmpty() ? QStringLiteral("joint%1").arg(index) : wanted;
	if (used->contains(name.toLower())) {
		const QString stem = name;
		int suffix = index;
		do {
			name = QStringLiteral("%1_%2").arg(stem).arg(suffix);
			++suffix;
		} while (used->contains(name.toLower()));
	}
	used->insert(name.toLower());
	return name;
}

ModelVec3 vecMin(const ModelVec3& a, const ModelVec3& b)
{
	return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

ModelVec3 vecMax(const ModelVec3& a, const ModelVec3& b)
{
	return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

double determinantOf(const ModelJointMatrix& a)
{
	const float* m = a.m;
	return double(m[0]) * (double(m[5]) * m[10] - double(m[6]) * m[9]) - double(m[1]) * (double(m[4]) * m[10] - double(m[6]) * m[8])
		+ double(m[2]) * (double(m[4]) * m[9] - double(m[5]) * m[8]);
}

// Translation, rotation and scale of an affine matrix, with a mirror carried
// in the X scale so modelJointMatrix(rotation, translation, scale) rebuilds it.
void decomposeJoint(const ModelJointMatrix& matrix, float channels[10])
{
	const ModelVec3 translation = modelJointTranslation(matrix);
	const ModelQuat rotation = modelJointRotation(matrix);
	ModelVec3 scale = modelJointScale(matrix);
	if (determinantOf(matrix) < 0) {
		scale.x = -scale.x;
	}
	channels[0] = translation.x;
	channels[1] = translation.y;
	channels[2] = translation.z;
	channels[3] = rotation.x;
	channels[4] = rotation.y;
	channels[5] = rotation.z;
	channels[6] = rotation.w;
	channels[7] = scale.x;
	channels[8] = scale.y;
	channels[9] = scale.z;
}

void appendU16(QByteArray& out, quint16 value)
{
	out.append(char(value & 0xFF));
	out.append(char((value >> 8) & 0xFF));
}

void appendU32(QByteArray& out, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8) {
		out.append(char((value >> shift) & 0xFF));
	}
}

void appendF32(QByteArray& out, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	appendU32(out, raw);
}

void alignTo4(QByteArray& out)
{
	while ((out.size() & 3) != 0) {
		out.append('\0');
	}
}

void patchU32(QByteArray& out, qint64 offset, quint32 value)
{
	for (int index = 0; index < 4; ++index) {
		out[qsizetype(offset + index)] = char((value >> (index * 8)) & 0xFF);
	}
}

} // namespace

namespace model_formats {

void decodeIqm(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(bytes, 0, kIqmHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM header is truncated.");
		return;
	}
	if (bytes.left(16) != QByteArray("INTERQUAKEMODEL\0", 16)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM identifier is malformed.");
		return;
	}

	// iqmheader: 27 little-endian uints after the 16-byte magic.
	IqmHeader header;
	quint32* const fields[] = {&header.version, &header.fileSize, &header.flags, &header.numText, &header.ofsText, &header.numMeshes,
		&header.ofsMeshes, &header.numVertexArrays, &header.numVertexes, &header.ofsVertexArrays, &header.numTriangles,
		&header.ofsTriangles, &header.ofsAdjacency, &header.numJoints, &header.ofsJoints, &header.numPoses, &header.ofsPoses,
		&header.numAnims, &header.ofsAnims, &header.numFrames, &header.numFrameChannels, &header.ofsFrames, &header.ofsBounds,
		&header.numComment, &header.ofsComment, &header.numExtensions, &header.ofsExtensions};
	for (int index = 0; index < int(sizeof(fields) / sizeof(fields[0])); ++index) {
		*fields[index] = readU32(bytes, 16 + (qint64(index) * 4));
	}
	mesh->version = int(std::min<quint32>(header.version, quint32(std::numeric_limits<int>::max())));
	if (header.version != 2) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported IQM version %1; only version 2 is decoded.").arg(header.version);
		return;
	}
	if (header.fileSize < quint32(kIqmHeaderBytes) || qint64(header.fileSize) > qint64(bytes.size())) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM header declares %1 bytes but the file holds %2.")
			.arg(header.fileSize).arg(bytes.size());
		return;
	}
	if (qint64(header.fileSize) < qint64(bytes.size())) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The IQM declares %1 bytes; the %2 byte(s) after them are ignored.")
			.arg(header.fileSize).arg(qint64(bytes.size()) - qint64(header.fileSize));
	}
	const QByteArray data = qint64(header.fileSize) < qint64(bytes.size()) ? bytes.left(qsizetype(header.fileSize)) : bytes;

	if (qint64(header.numText) > kMaxTextBytes || header.numMeshes > quint32(kMaxSurfaces) || header.numVertexArrays > quint32(kIqmMaxVertexArrays)
		|| header.numVertexes > quint32(kMaxSurfaceVertices) || header.numTriangles > quint32(kMaxSurfaceTriangles)
		|| header.numJoints > quint32(kMaxJoints) || header.numPoses > quint32(kMaxJoints) || header.numAnims > quint32(kMaxFrames)
		|| header.numFrames > quint32(kMaxFrames) || qint64(header.numFrameChannels) > qint64(header.numPoses) * 10
		|| qint64(header.numComment) > kMaxTextBytes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM header declares counts outside the supported range.");
		return;
	}
	if (header.numMeshes == 0 && header.numJoints == 0) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM holds neither meshes nor joints.");
		return;
	}
	if (header.numPoses != 0 && header.numPoses != header.numJoints) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM has %1 pose(s) for %2 joint(s); it needs one pose per joint or none.")
			.arg(header.numPoses).arg(header.numJoints);
		return;
	}
	if (qint64(header.numFrames) * qint64(std::max<quint32>(header.numJoints, 1)) > kIqmMaxPoseSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM declares more frame poses than can be decoded.");
		return;
	}

	// Every block is empty or lies after the header inside the declared size.
	struct Block {
		const char* field;
		quint32 offset;
		qint64 length;
	};
	const Block blocks[] = {
		{"ofs_text", header.ofsText, qint64(header.numText)},
		{"ofs_meshes", header.ofsMeshes, qint64(header.numMeshes) * kIqmMeshBytes},
		{"ofs_vertexarrays", header.ofsVertexArrays, qint64(header.numVertexArrays) * kIqmVertexArrayBytes},
		{"ofs_triangles", header.ofsTriangles, qint64(header.numTriangles) * kIqmTriangleBytes},
		{"ofs_joints", header.ofsJoints, qint64(header.numJoints) * kIqmJointBytes},
		{"ofs_poses", header.ofsPoses, qint64(header.numPoses) * kIqmPoseBytes},
		{"ofs_anims", header.ofsAnims, qint64(header.numAnims) * kIqmAnimBytes},
		{"ofs_frames", header.ofsFrames, qint64(header.numFrames) * qint64(header.numFrameChannels) * 2},
		{"ofs_bounds", header.ofsBounds, header.ofsBounds != 0 ? qint64(header.numFrames) * kIqmBoundsBytes : 0},
		{"ofs_comment", header.ofsComment, qint64(header.numComment)},
	};
	for (const Block& block : blocks) {
		if (block.length != 0 && (qint64(block.offset) < kIqmHeaderBytes || !rangeOk(data, block.offset, block.length))) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM block %1 lies outside the file.").arg(QString::fromLatin1(block.field));
			return;
		}
	}

	// Vertex arrays: the first array of each known type is used.
	IqmArray arrays[IqmColor + 1];
	QStringList arrayLines;
	for (quint32 index = 0; index < header.numVertexArrays; ++index) {
		if (!work.step()) { return; }
		const qint64 base = qint64(header.ofsVertexArrays) + (qint64(index) * kIqmVertexArrayBytes);
		const quint32 type = readU32(data, base);
		const quint32 format = readU32(data, base + 8);
		const quint32 size = readU32(data, base + 12);
		const quint32 offset = readU32(data, base + 16);
		if (arrayLines.size() < 16) {
			arrayLines << QStringLiteral("%1 %2 x%3").arg(iqmTypeName(type), iqmFormatName(format)).arg(size);
		}
		if (type > IqmBlendWeights || type == IqmTangent || arrays[type].present) {
			continue;
		}
		bool formatOk = size >= 1 && size <= quint32(kIqmMaxComponents) && iqmFormatBytes(format) > 0;
		switch (type) {
		case IqmPosition:
		case IqmNormal:
			formatOk = formatOk && iqmFormatIsReal(format) && size >= 3;
			break;
		case IqmTexCoord:
			formatOk = formatOk && iqmFormatIsReal(format) && size >= 2;
			break;
		case IqmBlendIndexes:
			formatOk = formatOk && iqmFormatIsInteger(format);
			break;
		case IqmBlendWeights:
			formatOk = formatOk && (iqmFormatIsReal(format) || format == IqmUByte || format == IqmUShort || format == IqmUInt);
			break;
		default:
			break;
		}
		if (!formatOk) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM %1 array uses a format or size this decoder does not read (%2 x%3).")
				.arg(iqmTypeName(type), iqmFormatName(format)).arg(size);
			return;
		}
		const qint64 length = qint64(header.numVertexes) * qint64(size) * iqmFormatBytes(format);
		if (length != 0 && (qint64(offset) < kIqmHeaderBytes || !rangeOk(data, offset, length))) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM %1 array lies outside the file.").arg(iqmTypeName(type));
			return;
		}
		arrays[type] = IqmArray{true, format, int(size), qint64(offset)};
	}
	if (!arrayLines.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Vertex arrays: %1").arg(arrayLines.join(QStringLiteral(", ")));
	}
	if (header.numMeshes > 0 && header.numVertexes > 0 && !arrays[IqmPosition].present) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM has meshes but no position array.");
		return;
	}
	const auto component = [&data](const IqmArray& array, qint64 vertex, int index) {
		const qint64 stride = qint64(array.size) * iqmFormatBytes(array.format);
		return iqmReadComponent(data, array.offset + (vertex * stride) + (qint64(index) * iqmFormatBytes(array.format)), array.format);
	};

	// Joints, parent-relative; the bind pose is their concatenation.
	ModelSkeleton& skeleton = mesh->skeleton;
	QVector<int> parents;
	QVector<ModelJointMatrix> localBind;
	parents.reserve(int(header.numJoints));
	localBind.reserve(int(header.numJoints));
	QSet<QString> usedNames;
	for (quint32 index = 0; index < header.numJoints; ++index) {
		if (!work.step()) { return; }
		const qint64 base = qint64(header.ofsJoints) + (qint64(index) * kIqmJointBytes);
		QString name;
		if (!iqmText(data, header, readU32(data, base), &name)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "IQM joint %1 names text outside the text block.").arg(index);
			return;
		}
		const qint32 parent = readI32(data, base + 4);
		if (parent < -1 || parent >= qint32(index)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "IQM joint %1 names a parent that does not come before it.").arg(index);
			return;
		}
		ModelVec3 translate, scale;
		readVec3(data, base + 8, &translate);
		const ModelQuat rotate{readF32(data, base + 20), readF32(data, base + 24), readF32(data, base + 28), readF32(data, base + 32)};
		readVec3(data, base + 36, &scale);
		ModelJoint joint;
		const QString unique = iqmUniqueJointName(name, int(index), &usedNames);
		if (!name.isEmpty() && unique != name) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "IQM joint %1 repeats the name \"%2\" and was renamed \"%3\".").arg(index).arg(name, unique);
		}
		joint.name = unique;
		joint.parent = parent;
		skeleton.joints.append(joint);
		parents.append(parent);
		localBind.append(modelJointMatrix(rotate, translate, scale));
	}
	if (header.numJoints > 0) {
		const QVector<ModelJointMatrix> bind = modelJointsToModelSpace(parents, localBind);
		if (bind.size() != skeleton.joints.size()) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM joint hierarchy could not be resolved.");
			return;
		}
		for (int index = 0; index < bind.size(); ++index) {
			skeleton.joints[index].bind = bind.at(index);
		}
		skeleton.sourceFormat = QStringLiteral("iqm");
	}

	// Poses and frames: each pose's masked channels are ushorts scaled by the
	// pose's channelscale and added to its channeloffset (ComputePoseMats).
	QVector<QVector<ModelJointMatrix>> framePoses;
	if (header.numJoints > 0 && header.numFrames > 0) {
		framePoses.reserve(int(header.numFrames));
		if (header.numPoses == 0) {
			// ioquake3 uses the bind pose for every frame of a pose-less model.
			const QVector<ModelJointMatrix> bindPose = modelSkeletonBindPose(skeleton);
			for (quint32 frame = 0; frame < header.numFrames; ++frame) {
				framePoses.append(bindPose);
			}
		} else {
			struct Pose {
				quint32 mask = 0;
				float offset[10] = {};
				float scale[10] = {};
			};
			QVector<Pose> poses(int(header.numPoses));
			qint64 channelCount = 0;
			bool parentsMatch = true;
			for (quint32 index = 0; index < header.numPoses; ++index) {
				if (!work.step()) { return; }
				const qint64 base = qint64(header.ofsPoses) + (qint64(index) * kIqmPoseBytes);
				Pose& pose = poses[int(index)];
				if (readI32(data, base) != parents.at(int(index))) {
					parentsMatch = false;
				}
				pose.mask = readU32(data, base + 4) & kIqmFullChannelMask;
				for (int channel = 0; channel < 10; ++channel) {
					pose.offset[channel] = readF32(data, base + 8 + (qint64(channel) * 4));
					pose.scale[channel] = readF32(data, base + 48 + (qint64(channel) * 4));
					if ((pose.mask & (1u << channel)) != 0) {
						++channelCount;
					}
				}
			}
			if (channelCount != qint64(header.numFrameChannels)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM frame channel count (%1) does not match its poses (%2).")
					.arg(header.numFrameChannels).arg(channelCount);
				return;
			}
			if (!parentsMatch) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "IQM pose parents differ from the joint parents; the joint parents are used.");
			}
			qint64 cursor = header.ofsFrames;
			for (quint32 frame = 0; frame < header.numFrames; ++frame) {
				if (!work.step()) { return; }
				QVector<ModelJointMatrix> local(int(header.numPoses));
				for (int index = 0; index < poses.size(); ++index) {
					if (!work.step()) { return; }
					const Pose& pose = poses.at(index);
					float channels[10];
					for (int channel = 0; channel < 10; ++channel) {
						channels[channel] = pose.offset[channel];
						if ((pose.mask & (1u << channel)) != 0) {
							channels[channel] += float(readU16(data, cursor)) * pose.scale[channel];
							cursor += 2;
						}
					}
					local[index] = modelJointMatrix(ModelQuat{channels[3], channels[4], channels[5], channels[6]},
						ModelVec3{channels[0], channels[1], channels[2]}, ModelVec3{channels[7], channels[8], channels[9]});
				}
				framePoses.append(modelJointsToModelSpace(parents, local));
			}
		}
	}

	// iqmbounds, one per frame, kept with the clips.
	QVector<ModelVec3> boundsMins, boundsMaxs;
	if (header.ofsBounds != 0 && header.numFrames > 0) {
		boundsMins.reserve(int(header.numFrames));
		boundsMaxs.reserve(int(header.numFrames));
		for (quint32 frame = 0; frame < header.numFrames; ++frame) {
			const qint64 base = qint64(header.ofsBounds) + (qint64(frame) * kIqmBoundsBytes);
			ModelVec3 mins, maxs;
			readVec3(data, base, &mins);
			readVec3(data, base + 12, &maxs);
			boundsMins.append(mins);
			boundsMaxs.append(maxs);
		}
	}

	// iqmanim: a name, a frame range, a rate and the loop flag. Animations may
	// overlap, so the frames they hold together are capped like baked frames.
	if (!framePoses.isEmpty()) {
		qint64 clipFrames = 0;
		int clipsDropped = 0;
		for (quint32 index = 0; index < header.numAnims; ++index) {
			if (!work.step()) { return; }
			const qint64 base = qint64(header.ofsAnims) + (qint64(index) * kIqmAnimBytes);
			// ioquake3 never reads animation names, so a bad one only loses the name.
			QString name;
			if (!iqmText(data, header, readU32(data, base), &name)) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "IQM animation %1 names text outside the text block.").arg(index);
				name.clear();
			}
			if (name.isEmpty()) {
				name = QStringLiteral("anim%1").arg(index);
			}
			const quint32 first = readU32(data, base + 4);
			const quint32 count = readU32(data, base + 8);
			const float rate = readF32(data, base + 12);
			const quint32 animFlags = readU32(data, base + 16);
			if (count == 0) {
				continue;
			}
			if (qint64(first) + qint64(count) > qint64(framePoses.size())) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "IQM animation \"%1\" runs past the last frame and was left out.").arg(name);
				continue;
			}
			if (clipFrames + qint64(count) > qint64(kMaxFrames)) {
				++clipsDropped;
				continue;
			}
			clipFrames += count;
			ModelSkeletalClip clip;
			clip.name = name;
			clip.framesPerSecond = std::isfinite(rate) && rate > 0.0f ? double(rate) : 0.0;
			clip.loops = (animFlags & kIqmLoop) != 0;
			clip.frames = framePoses.mid(int(first), int(count));
			if (!boundsMins.isEmpty()) {
				clip.frameMins = boundsMins.mid(int(first), int(count));
				clip.frameMaxs = boundsMaxs.mid(int(first), int(count));
			}
			skeleton.clips.append(clip);
		}
		if (clipsDropped > 0) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 IQM animation(s) were left out to keep the clips within %2 frames.")
				.arg(clipsDropped).arg(kMaxFrames);
		}
		if (header.numAnims == 0) {
			ModelSkeletalClip clip;
			clip.name = QStringLiteral("all");
			clip.frames = framePoses;
			clip.frameMins = boundsMins;
			clip.frameMaxs = boundsMaxs;
			skeleton.clips.append(clip);
		}
	}

	// Blend arrays drive skinning only when the model has joints.
	const bool skinned = header.numJoints > 0 && arrays[IqmBlendIndexes].present && arrays[IqmBlendWeights].present;
	if (header.numJoints > 0 && header.numMeshes > 0 && !skinned) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The IQM has joints but no blend indexes or weights; its meshes stay in the bind pose.");
	}
	const int perVertex = skinned ? std::min(arrays[IqmBlendIndexes].size, arrays[IqmBlendWeights].size) : 0;

	// iqmmesh: one surface each, over a range of the shared vertex and
	// triangle lists. Triangle indices are absolute.
	QSet<QString> skinNames;
	for (quint32 meshIndex = 0; meshIndex < header.numMeshes; ++meshIndex) {
		if (!work.step()) { return; }
		const qint64 base = qint64(header.ofsMeshes) + (qint64(meshIndex) * kIqmMeshBytes);
		QString name, material;
		if (!iqmText(data, header, readU32(data, base), &name) || !iqmText(data, header, readU32(data, base + 4), &material)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "IQM mesh %1 names text outside the text block.").arg(meshIndex);
			return;
		}
		const qint64 firstVertex = readU32(data, base + 8);
		const qint64 vertexCount = readU32(data, base + 12);
		const qint64 firstTriangle = readU32(data, base + 16);
		const qint64 triangleCount = readU32(data, base + 20);
		if (firstVertex + vertexCount > qint64(header.numVertexes) || firstTriangle + triangleCount > qint64(header.numTriangles)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "IQM mesh %1 names vertices or triangles outside the model's lists.").arg(meshIndex);
			return;
		}
		if (vertexCount == 0) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "IQM mesh %1 holds no vertices and was left out.").arg(meshIndex);
			continue;
		}

		ModelSurface surface;
		surface.index = mesh->surfaces.size();
		surface.name = name.isEmpty() ? QStringLiteral("mesh%1").arg(meshIndex) : name;
		surface.vertexCount = int(vertexCount);
		if (!material.isEmpty()) {
			surface.skinPaths.append(material);
			if (!skinNames.contains(material)) {
				skinNames.insert(material);
				mesh->skinPaths.append(material);
			}
		}

		ModelFrameGeometry geometry;
		geometry.positions.resize(int(vertexCount));
		QVector<ModelVec3> normals;
		if (arrays[IqmNormal].present) {
			normals.resize(int(vertexCount));
		}
		surface.texCoords.resize(int(vertexCount));
		for (qint64 vertex = 0; vertex < vertexCount; ++vertex) {
			if (!work.step()) { return; }
			const qint64 source = firstVertex + vertex;
			const IqmArray& position = arrays[IqmPosition];
			geometry.positions[int(vertex)] = ModelVec3{float(component(position, source, 0)), float(component(position, source, 1)),
				float(component(position, source, 2))};
			if (!isFinite(geometry.positions.at(int(vertex)))) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "IQM mesh %1 has a vertex position that is not a finite number.").arg(meshIndex);
				return;
			}
			if (arrays[IqmNormal].present) {
				const IqmArray& normal = arrays[IqmNormal];
				normals[int(vertex)] = ModelVec3{float(component(normal, source, 0)), float(component(normal, source, 1)), float(component(normal, source, 2))};
				if (!isFinite(normals.at(int(vertex)))) {
					normals[int(vertex)] = ModelVec3{0.0f, 0.0f, 0.0f};
				}
			}
			if (arrays[IqmTexCoord].present) {
				const IqmArray& texCoord = arrays[IqmTexCoord];
				surface.texCoords[int(vertex)] = ModelTexCoord{float(component(texCoord, source, 0)), float(component(texCoord, source, 1))};
			}
		}

		int skippedTriangles = 0;
		surface.triangles.reserve(int(triangleCount));
		for (qint64 triangle = 0; triangle < triangleCount; ++triangle) {
			if (!work.step()) { return; }
			const qint64 at = qint64(header.ofsTriangles) + ((firstTriangle + triangle) * kIqmTriangleBytes);
			const qint64 a = qint64(readU32(data, at)) - firstVertex;
			const qint64 b = qint64(readU32(data, at + 4)) - firstVertex;
			const qint64 c = qint64(readU32(data, at + 8)) - firstVertex;
			if (a < 0 || a >= vertexCount || b < 0 || b >= vertexCount || c < 0 || c >= vertexCount) {
				++skippedTriangles;
				continue;
			}
			// Clockwise in the file; counter-clockwise here.
			surface.triangles.append(ModelTriangle{int(a), int(c), int(b)});
		}
		if (skippedTriangles > 0) {
			surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.").arg(skippedTriangles);
		}

		// Missing or zero normals are rebuilt from the faces.
		const QVector<ModelVec3> faceNormals = modelFaceNormals(surface, geometry.positions);
		geometry.normals = normals.isEmpty() ? faceNormals : normals;
		for (int vertex = 0; vertex < geometry.normals.size(); ++vertex) {
			const ModelVec3& normal = geometry.normals.at(vertex);
			if (normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f) {
				geometry.normals[vertex] = faceNormals.at(vertex);
			}
		}
		surface.frames.append(geometry);

		if (skinned) {
			const IqmArray& indexArray = arrays[IqmBlendIndexes];
			const IqmArray& weightArray = arrays[IqmBlendWeights];
			const double weightScale = iqmWeightScale(weightArray.format);
			QVector<int> joints(int(vertexCount) * perVertex, 0);
			QVector<float> weights(int(vertexCount) * perVertex, 0.0f);
			QVector<int> unweighted;
			for (qint64 vertex = 0; vertex < vertexCount; ++vertex) {
				if (!work.step()) { return; }
				const qint64 source = firstVertex + vertex;
				bool any = false;
				for (int slot = 0; slot < perVertex; ++slot) {
					const double joint = component(indexArray, source, slot);
					const double weight = component(weightArray, source, slot) / weightScale;
					const int at = int(vertex) * perVertex + slot;
					joints[at] = joint >= 0.0 && joint < double(std::numeric_limits<int>::max()) ? int(joint) : -1;
					weights[at] = std::isfinite(weight) ? float(weight) : 0.0f;
					if (weights.at(at) > 0.0f) {
						any = true;
					}
				}
				if (!any) {
					unweighted.append(int(vertex));
				}
			}
			if (unweighted.size() < int(vertexCount)) {
				if (!unweighted.isEmpty()) {
					// The skeleton cannot hold a vertex that ignores every
					// joint, so it follows the first joint instead.
					for (const int vertex : unweighted) {
						joints[vertex * perVertex] = 0;
						weights[vertex * perVertex] = 1.0f;
					}
					mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 vertex(es) of IQM mesh \"%2\" carry no blend weight; they follow joint %3.")
						.arg(unweighted.size()).arg(surface.name, skeleton.joints.first().name);
				}
				QString problem;
				if (!modelSkinningFromBindPose(skeleton, geometry.positions, normals, joints, weights, perVertex, &surface.skinning, &problem)) {
					mesh->error = problem;
					return;
				}
			}
		}
		mesh->surfaces.append(surface);
	}

	if (header.numComment > 0) {
		QByteArray comment = data.mid(qsizetype(header.ofsComment), qsizetype(std::min<quint32>(header.numComment, 4096)));
		const qsizetype end = comment.indexOf('\0');
		if (end >= 0) {
			comment.truncate(end);
		}
		const QString text = QString::fromUtf8(comment).section(QLatin1Char('\n'), 0, 0).trimmed().left(200);
		if (!text.isEmpty()) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Comment: %1").arg(text);
		}
	}
	if (header.numExtensions > 0) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Extensions: %1, not read.").arg(header.numExtensions);
	}
	mesh->skinCount = mesh->skinPaths.size();

	if (!skeleton.isEmpty()) {
		QString problem;
		if (!bakeModelSkeleton(mesh, ModelSkeletonBakeOptions{}, &problem, control)) {
			mesh->error = problem;
			if (mesh->error.isEmpty()) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The IQM skeleton could not be baked into frames.");
			}
			return;
		}
	} else {
		// A static model: one frame.
		ModelFrameInfo info;
		info.index = 0;
		info.name = QStringLiteral("frame0");
		bool haveBounds = false;
		for (const ModelSurface& surface : mesh->surfaces) {
			for (const ModelVec3& position : surface.frames.first().positions) {
				info.mins = haveBounds ? vecMin(info.mins, position) : position;
				info.maxs = haveBounds ? vecMax(info.maxs, position) : position;
				haveBounds = true;
			}
		}
		info.origin = ModelVec3{(info.mins.x + info.maxs.x) * 0.5f, (info.mins.y + info.maxs.y) * 0.5f, (info.mins.z + info.maxs.z) * 0.5f};
		const ModelVec3 half{info.maxs.x - info.origin.x, info.maxs.y - info.origin.y, info.maxs.z - info.origin.z};
		info.radius = std::sqrt(half.x * half.x + half.y * half.y + half.z * half.z);
		mesh->frames.append(info);
	}
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
}

} // namespace model_formats

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

QByteArray exportModelIqm(const ModelMesh& mesh, QString* error, const ModelWorkControl& control)
{
	QString problem;
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, &problem);
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return QByteArray();
	};
	if (!work.check()) {
		return fail(problem);
	}
	const ModelSkeleton& skeleton = mesh.skeleton;
	const bool skeletal = !skeleton.isEmpty();
	if (skeletal) {
		if (!validateModelSkeleton(mesh, &problem)) {
			return fail(problem);
		}
		if (skeleton.joints.size() > 256) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The IQM writer stores joint indexes in one byte, so it writes at most 256 joints; the model has %1.")
				.arg(skeleton.joints.size()));
		}
	}
	const QVector<int> parents = modelJointParents(skeleton);
	const QVector<ModelJointMatrix> bindPose = modelSkeletonBindPose(skeleton);
	const bool frameZeroIsBind = !skeleton.bakedClipForFrame.isEmpty() && skeleton.bakedClipForFrame.first() == -1;

	// Bind-pose geometry per written surface.
	struct Written {
		const ModelSurface* surface = nullptr;
		ModelFrameGeometry bind;
		bool skinned = false;
	};
	QVector<Written> written;
	qint64 totalVertices = 0;
	qint64 totalTriangles = 0;
	for (const ModelSurface& surface : mesh.surfaces) {
		if (!work.step()) { return fail(problem); }
		if (surface.vertexCount <= 0) {
			continue;
		}
		Written entry;
		entry.surface = &surface;
		entry.skinned = skeletal && !surface.skinning.isEmpty();
		const bool frameUsable = !surface.frames.isEmpty() && surface.frames.first().positions.size() == surface.vertexCount;
		if (entry.skinned && !(frameZeroIsBind && frameUsable)) {
			if (!skinModelSurface(surface, bindPose, &entry.bind, &problem)) {
				return fail(problem);
			}
		} else if (frameUsable) {
			entry.bind = surface.frames.first();
		} else {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "Surface \"%1\" has no frame geometry to write.").arg(surface.name));
		}
		if (entry.bind.normals.size() != surface.vertexCount) {
			entry.bind.normals = modelFaceNormals(surface, entry.bind.positions);
		}
		for (const ModelTriangle& triangle : surface.triangles) {
			if (triangle.a < 0 || triangle.b < 0 || triangle.c < 0 || triangle.a >= surface.vertexCount || triangle.b >= surface.vertexCount
				|| triangle.c >= surface.vertexCount) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "Surface \"%1\" has a triangle outside its vertex list.").arg(surface.name));
			}
		}
		totalVertices += surface.vertexCount;
		totalTriangles += surface.triangles.size();
		written.append(entry);
	}
	if (written.isEmpty() && !skeletal) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model has no geometry to write as IQM."));
	}
	if (totalVertices > qint64(std::numeric_limits<qint32>::max()) || totalTriangles > qint64(std::numeric_limits<qint32>::max())) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model has more vertices or triangles than an IQM file holds."));
	}

	// Animation frames: every clip in order, as parent-relative channels.
	struct WrittenClip {
		const ModelSkeletalClip* clip = nullptr;
		int firstFrame = 0;
	};
	QVector<WrittenClip> clips;
	QVector<const QVector<ModelJointMatrix>*> framePoses;
	if (skeletal) {
		for (const ModelSkeletalClip& clip : skeleton.clips) {
			if (clip.frames.isEmpty()) {
				continue;
			}
			clips.append(WrittenClip{&clip, int(framePoses.size())});
			for (const QVector<ModelJointMatrix>& frame : clip.frames) {
				framePoses.append(&frame);
			}
		}
	}
	const int jointCount = skeleton.joints.size();
	const qint64 frameCount = framePoses.size();
	if (frameCount * qint64(std::max(jointCount, 1)) > kIqmMaxPoseSlots) {
		return fail(QCoreApplication::translate("VibeStudioModelMesh", "The model has more animation frames than the IQM writer handles at once."));
	}
	QVector<float> channels(int(frameCount * jointCount * 10));
	for (qint64 frame = 0; frame < frameCount; ++frame) {
		if (!work.step()) { return fail(problem); }
		const QVector<ModelJointMatrix> local = modelJointsToLocalSpace(parents, *framePoses.at(int(frame)));
		if (local.size() != jointCount) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The joint hierarchy could not be written as IQM poses."));
		}
		for (int joint = 0; joint < jointCount; ++joint) {
			decomposeJoint(local.at(joint), channels.data() + ((frame * jointCount + joint) * 10));
		}
	}
	// Per joint and channel, offset = the smallest value and scale = the range
	// over 65535 steps, as the reference exporter quantizes them.
	QVector<float> channelOffset(jointCount * 10, 0.0f);
	QVector<float> channelScale(jointCount * 10, 0.0f);
	if (frameCount > 0) {
		for (int joint = 0; joint < jointCount; ++joint) {
			for (int channel = 0; channel < 10; ++channel) {
				float low = std::numeric_limits<float>::max();
				float high = std::numeric_limits<float>::lowest();
				for (qint64 frame = 0; frame < frameCount; ++frame) {
					const float value = channels.at(int((frame * jointCount + joint) * 10 + channel));
					low = std::min(low, value);
					high = std::max(high, value);
				}
				if (!std::isfinite(low) || !std::isfinite(high)) {
					return fail(QCoreApplication::translate("VibeStudioModelMesh", "A joint pose is not a finite number and cannot be written as IQM."));
				}
				channelOffset[joint * 10 + channel] = low;
				const double range = double(high) - double(low);
				channelScale[joint * 10 + channel] = range >= 1e-10 ? float(range / 65535.0) : 0.0f;
			}
		}
	}

	// Text block: the empty string first, then each distinct name once.
	QByteArray text(1, '\0');
	QHash<QByteArray, quint32> textOffsets;
	textOffsets.insert(QByteArray(), 0);
	const auto addText = [&text, &textOffsets](const QString& value) {
		const QByteArray utf8 = value.toUtf8();
		const auto found = textOffsets.constFind(utf8);
		if (found != textOffsets.constEnd()) {
			return found.value();
		}
		const quint32 at = quint32(text.size());
		text.append(utf8);
		text.append('\0');
		textOffsets.insert(utf8, at);
		return at;
	};
	struct MeshEntry {
		quint32 name = 0, material = 0, firstVertex = 0, vertexCount = 0, firstTriangle = 0, triangleCount = 0;
	};
	QVector<MeshEntry> meshes;
	quint32 vertexCursor = 0;
	quint32 triangleCursor = 0;
	for (const Written& entry : written) {
		MeshEntry row;
		row.name = addText(entry.surface->name);
		QString material = entry.surface->skinPaths.value(0);
		if (material.isEmpty()) {
			material = mesh.skinPaths.value(0);
		}
		row.material = addText(material);
		row.firstVertex = vertexCursor;
		row.vertexCount = quint32(entry.surface->vertexCount);
		row.firstTriangle = triangleCursor;
		row.triangleCount = quint32(entry.surface->triangles.size());
		vertexCursor += row.vertexCount;
		triangleCursor += row.triangleCount;
		meshes.append(row);
	}
	QVector<quint32> jointNames;
	for (const ModelJoint& joint : skeleton.joints) {
		jointNames.append(addText(joint.name));
	}
	QVector<quint32> clipNames;
	for (int index = 0; index < clips.size(); ++index) {
		const QString name = clips.at(index).clip->name;
		clipNames.append(addText(name.isEmpty() ? QStringLiteral("anim%1").arg(index) : name));
	}

	QByteArray out(int(kIqmHeaderBytes), '\0');
	std::memcpy(out.data(), "INTERQUAKEMODEL\0", 16);
	const auto headerField = [&out](int index, quint32 value) { patchU32(out, 16 + (qint64(index) * 4), value); };
	headerField(0, 2); // version

	if (text.size() > 1) {
		alignTo4(out);
		headerField(3, quint32(text.size()));
		headerField(4, quint32(out.size()));
		out.append(text);
	}

	if (!meshes.isEmpty()) {
		alignTo4(out);
		headerField(5, quint32(meshes.size()));
		headerField(6, quint32(out.size()));
		for (const MeshEntry& row : meshes) {
			appendU32(out, row.name);
			appendU32(out, row.material);
			appendU32(out, row.firstVertex);
			appendU32(out, row.vertexCount);
			appendU32(out, row.firstTriangle);
			appendU32(out, row.triangleCount);
		}

		// Vertex arrays in the specification's order, then their data.
		struct ArraySpec {
			quint32 type, format, size;
		};
		QVector<ArraySpec> specs = {{IqmPosition, IqmFloat, 3}, {IqmTexCoord, IqmFloat, 2}, {IqmNormal, IqmFloat, 3}};
		if (skeletal) {
			specs.append({IqmBlendIndexes, IqmUByte, 4});
			specs.append({IqmBlendWeights, IqmUByte, 4});
		}
		alignTo4(out);
		headerField(7, quint32(specs.size()));
		headerField(8, vertexCursor);
		headerField(9, quint32(out.size()));
		const qint64 arrayTable = out.size();
		for (int index = 0; index < specs.size(); ++index) {
			appendU32(out, specs.at(index).type);
			appendU32(out, 0); // flags
			appendU32(out, specs.at(index).format);
			appendU32(out, specs.at(index).size);
			appendU32(out, 0); // offset, patched below
		}
		const auto beginArray = [&out, arrayTable](int index) {
			alignTo4(out);
			patchU32(out, arrayTable + (qint64(index) * kIqmVertexArrayBytes) + 16, quint32(out.size()));
		};
		beginArray(0);
		for (const Written& entry : written) {
			for (const ModelVec3& position : entry.bind.positions) {
				if (!work.step()) { return fail(problem); }
				appendF32(out, position.x);
				appendF32(out, position.y);
				appendF32(out, position.z);
			}
		}
		beginArray(1);
		for (const Written& entry : written) {
			for (int vertex = 0; vertex < entry.surface->vertexCount; ++vertex) {
				if (!work.step()) { return fail(problem); }
				const ModelTexCoord coord = entry.surface->texCoords.value(vertex);
				appendF32(out, coord.u);
				appendF32(out, coord.v);
			}
		}
		beginArray(2);
		for (const Written& entry : written) {
			for (const ModelVec3& normal : entry.bind.normals) {
				if (!work.step()) { return fail(problem); }
				appendF32(out, normal.x);
				appendF32(out, normal.y);
				appendF32(out, normal.z);
			}
		}
		if (skeletal) {
			// Up to four strongest influences, renormalized to sum to 255. A
			// vertex with none (or an unskinned surface) gets zero weights,
			// which ioquake3 leaves in the bind pose.
			QByteArray indexes;
			QByteArray weights;
			for (const Written& entry : written) {
				const ModelSurfaceSkinning& skinning = entry.surface->skinning;
				for (int vertex = 0; vertex < entry.surface->vertexCount; ++vertex) {
					if (!work.step()) { return fail(problem); }
					QVector<ModelJointInfluence> influences;
					if (entry.skinned) {
						const int first = skinning.first.at(vertex);
						for (int index = first; index < first + skinning.count.at(vertex); ++index) {
							const ModelJointInfluence& influence = skinning.influences.at(index);
							if (influence.weight > 0.0f && std::isfinite(influence.weight)) {
								influences.append(influence);
							}
						}
					}
					std::stable_sort(influences.begin(), influences.end(),
						[](const ModelJointInfluence& a, const ModelJointInfluence& b) { return a.weight > b.weight; });
					if (influences.size() > 4) {
						influences.resize(4);
					}
					double sum = 0.0;
					for (const ModelJointInfluence& influence : influences) {
						sum += influence.weight;
					}
					int quantized[4] = {0, 0, 0, 0};
					int total = 0;
					for (int slot = 0; slot < influences.size(); ++slot) {
						quantized[slot] = int(std::lround(double(influences.at(slot).weight) / sum * 255.0));
						total += quantized[slot];
					}
					if (!influences.isEmpty()) {
						quantized[0] = std::clamp(quantized[0] + (255 - total), 0, 255);
					}
					for (int slot = 0; slot < 4; ++slot) {
						const bool used = slot < influences.size() && quantized[slot] > 0;
						indexes.append(char(used ? influences.at(slot).joint : 0));
						weights.append(char(used ? quantized[slot] : 0));
					}
				}
			}
			beginArray(3);
			out.append(indexes);
			beginArray(4);
			out.append(weights);
		}

		// Triangles with absolute indices, back to IQM's clockwise order.
		alignTo4(out);
		headerField(10, triangleCursor);
		headerField(11, quint32(out.size()));
		for (int index = 0; index < written.size(); ++index) {
			const quint32 base = meshes.at(index).firstVertex;
			for (const ModelTriangle& triangle : written.at(index).surface->triangles) {
				if (!work.step()) { return fail(problem); }
				appendU32(out, base + quint32(triangle.a));
				appendU32(out, base + quint32(triangle.c));
				appendU32(out, base + quint32(triangle.b));
			}
		}
	}

	if (skeletal) {
		// Joints: the bind pose relative to each parent.
		const QVector<ModelJointMatrix> localBind = modelJointsToLocalSpace(parents, bindPose);
		if (localBind.size() != jointCount) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The joint hierarchy could not be written as IQM poses."));
		}
		alignTo4(out);
		headerField(13, quint32(jointCount));
		headerField(14, quint32(out.size()));
		for (int joint = 0; joint < jointCount; ++joint) {
			float values[10];
			decomposeJoint(localBind.at(joint), values);
			appendU32(out, jointNames.at(joint));
			appendU32(out, quint32(qint32(parents.at(joint))));
			for (const float value : values) {
				appendF32(out, value);
			}
		}

		if (frameCount > 0) {
			alignTo4(out);
			headerField(15, quint32(jointCount));
			headerField(16, quint32(out.size()));
			for (int joint = 0; joint < jointCount; ++joint) {
				appendU32(out, quint32(qint32(parents.at(joint))));
				appendU32(out, kIqmFullChannelMask);
				for (int channel = 0; channel < 10; ++channel) {
					appendF32(out, channelOffset.at(joint * 10 + channel));
				}
				for (int channel = 0; channel < 10; ++channel) {
					appendF32(out, channelScale.at(joint * 10 + channel));
				}
			}

			alignTo4(out);
			headerField(17, quint32(clips.size()));
			headerField(18, quint32(out.size()));
			for (int index = 0; index < clips.size(); ++index) {
				const ModelSkeletalClip& clip = *clips.at(index).clip;
				appendU32(out, clipNames.at(index));
				appendU32(out, quint32(clips.at(index).firstFrame));
				appendU32(out, quint32(clip.frames.size()));
				appendF32(out, clip.framesPerSecond > 0 && std::isfinite(clip.framesPerSecond) ? float(clip.framesPerSecond) : 0.0f);
				appendU32(out, clip.loops ? kIqmLoop : 0u);
			}

			alignTo4(out);
			headerField(19, quint32(frameCount));
			headerField(20, quint32(jointCount * 10));
			headerField(21, quint32(out.size()));
			for (qint64 frame = 0; frame < frameCount; ++frame) {
				if (!work.step()) { return fail(problem); }
				for (int joint = 0; joint < jointCount; ++joint) {
					for (int channel = 0; channel < 10; ++channel) {
						const float scale = channelScale.at(joint * 10 + channel);
						const float value = channels.at(int((frame * jointCount + joint) * 10 + channel));
						const double steps = scale > 0.0f ? (double(value) - double(channelOffset.at(joint * 10 + channel))) / double(scale) : 0.0;
						appendU16(out, quint16(std::clamp<long>(std::lround(steps), 0, 65535)));
					}
				}
			}

			// iqmbounds per frame, from the skinned vertices when that stays
			// within the vertex budget; ioquake3 tolerates their absence.
			if (totalVertices * frameCount <= model_formats::kMaxVertexSlots) {
				alignTo4(out);
				const qint64 boundsOffset = out.size();
				bool boundsOk = true;
				QByteArray boundsBlock;
				for (qint64 frame = 0; frame < frameCount && boundsOk; ++frame) {
					if (!work.step()) { return fail(problem); }
					bool have = false;
					ModelVec3 mins{0.0f, 0.0f, 0.0f}, maxs{0.0f, 0.0f, 0.0f};
					double xyRadius = 0.0, radius = 0.0;
					for (const Written& entry : written) {
						ModelFrameGeometry posed;
						const QVector<ModelVec3>* positions = &entry.bind.positions;
						if (entry.skinned) {
							if (!skinModelSurface(*entry.surface, *framePoses.at(int(frame)), &posed, &problem)) {
								boundsOk = false;
								break;
							}
							positions = &posed.positions;
						}
						for (const ModelVec3& p : *positions) {
							mins = have ? vecMin(mins, p) : p;
							maxs = have ? vecMax(maxs, p) : p;
							have = true;
							const double xy = double(p.x) * p.x + double(p.y) * p.y;
							xyRadius = std::max(xyRadius, xy);
							radius = std::max(radius, xy + double(p.z) * p.z);
						}
					}
					appendF32(boundsBlock, mins.x);
					appendF32(boundsBlock, mins.y);
					appendF32(boundsBlock, mins.z);
					appendF32(boundsBlock, maxs.x);
					appendF32(boundsBlock, maxs.y);
					appendF32(boundsBlock, maxs.z);
					appendF32(boundsBlock, float(std::sqrt(xyRadius)));
					appendF32(boundsBlock, float(std::sqrt(radius)));
				}
				if (boundsOk) {
					headerField(22, quint32(boundsOffset));
					out.append(boundsBlock);
				}
				problem.clear();
			}
		}
	}

	alignTo4(out);
	headerField(1, quint32(out.size()));
	if (!work.check()) {
		return fail(problem);
	}
	if (error) {
		error->clear();
	}
	return out;
}

} // namespace vibestudio
