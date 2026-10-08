// Return to Castle Wolfenstein / Wolfenstein: Enemy Territory compressed MD3
// (MDC, "IDPC" version 2).
//
// Layouts follow mdcHeader_t, mdcSurface_t, mdcTagName_t, mdcTag_t and
// mdcXyzCompressed_t in the GPL Return to Castle Wolfenstein source
// (src/qcommon/qfiles.h), with md3Frame_t, md3Shader_t, md3Triangle_t,
// md3St_t and md3XyzNormal_t shared with MD3. Decoding follows R_LoadMDC and
// R_LerpTag's MDC branch (src/renderer/tr_model.c), LerpCMeshVertexes
// (src/renderer/tr_surface.c), and R_MDC_DecodeXyzCompressed with the
// MDC_DIST_SCALE / MDC_MAX_OFS constants (src/renderer/tr_local.h).
//
// Each surface stores full base frames (md3XyzNormal_t) and compressed frames
// (one 32-bit word per vertex: three biased byte offsets and a normal index).
// frameBaseFrames and frameCompFrames (one short per model frame, -1 for "no
// compressed frame") pick which pair a model frame uses; the position is the
// base position plus the decoded offset, and the normal comes from the
// compressed word's 256-entry table when a compressed frame is used.

#include "core/model_formats_p.h"
#include "core/model_skeleton.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio::model_formats {

namespace {

constexpr qint64 kMdcHeaderBytes = 112;
constexpr qint64 kMdcSurfaceHeaderBytes = 124;
constexpr qint64 kMdcNameBytes = 64;
constexpr qint64 kMdcFrameBytes = 56;
constexpr qint64 kMdcTagNameBytes = 64;
constexpr qint64 kMdcTagBytes = 12;
constexpr qint64 kMdcShaderBytes = 68;
constexpr qint64 kMdcTriangleBytes = 12;
constexpr qint64 kMdcStBytes = 8;
constexpr qint64 kMdcXyzNormalBytes = 8;
constexpr qint64 kMdcXyzCompressedBytes = 4;
// MD3_XYZ_SCALE, MDC_DIST_SCALE, MDC_MAX_OFS and MDC_TAG_ANGLE_SCALE.
constexpr double kMdcXyzScale = 1.0 / 64.0;
constexpr double kMdcDistScale = 0.05;
constexpr double kMdcMaxOffset = 127.0;
constexpr double kMdcTagAngleScale = 360.0 / 32700.0;
constexpr double kMdcPi = 3.14159265358979323846;

// The 256-entry normal table compressed frames index (r_anormals, from
// src/renderer/anorms256.h). The published table is regular: rings of
// 32 - 4|i| directions at latitude i * 11.25 degrees, the equator first, then
// the southern rings (i = -1 .. -7), then the northern rings (i = 1 .. 7),
// each ring starting at +X and turning towards +Y. It is rebuilt from that
// rule rather than copied; the entries agree with the published six-decimal
// values to within their float rounding (about 1e-6).
const std::array<ModelVec3, 256>& mdcNormalTable()
{
	static const std::array<ModelVec3, 256> table = [] {
		std::array<ModelVec3, 256> normals{};
		const int rings[15] = {0, -1, -2, -3, -4, -5, -6, -7, 1, 2, 3, 4, 5, 6, 7};
		int index = 0;
		for (const int ring : rings) {
			const double latitude = double(ring) * (kMdcPi / 16.0);
			const int count = 32 - (4 * std::abs(ring));
			for (int step = 0; step < count && index < 256; ++step) {
				const double angle = double(step) * (2.0 * kMdcPi / double(count));
				normals[size_t(index)] = ModelVec3{float(std::cos(latitude) * std::cos(angle)),
					float(std::cos(latitude) * std::sin(angle)), float(std::sin(latitude))};
				++index;
			}
		}
		return normals;
	}();
	return table;
}

// md3XyzNormal_t's packed latitude/longitude normal, as the renderer rebuilds it.
ModelVec3 mdcLatLongNormal(quint16 packed)
{
	const double lat = double((packed >> 8) & 0xFF) * (2.0 * kMdcPi) / 256.0;
	const double lng = double(packed & 0xFF) * (2.0 * kMdcPi) / 256.0;
	return ModelVec3{float(std::cos(lat) * std::sin(lng)), float(std::sin(lat) * std::sin(lng)), float(std::cos(lng))};
}

// Frame names carry animations the way MD3 frames do: consecutive frames
// sharing a stem with a trailing number. Mirrors model_mesh.cpp's inference.
QString mdcAnimationStem(const QString& name)
{
	QString stem = name.trimmed();
	while (!stem.isEmpty() && stem.back().isDigit()) {
		stem.chop(1);
	}
	while (!stem.isEmpty() && (stem.back() == QLatin1Char('_') || stem.back() == QLatin1Char('-') || stem.back().isSpace())) {
		stem.chop(1);
	}
	if (stem.isEmpty()) {
		stem = name.trimmed();
	}
	if (stem.isEmpty()) {
		stem = QStringLiteral("frame");
	}
	return stem;
}

} // namespace

void decodeMdc(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	if (!rangeOk(bytes, 0, kMdcHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDC header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (mesh->version != 2) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported MDC version %1; only IDPC version 2 is decoded.").arg(mesh->version);
		return;
	}

	// mdcHeader_t
	const QString internalName = readFixedName(bytes, 8, kMdcNameBytes);
	const qint32 flags = readI32(bytes, 72);
	const int frameCount = readI32(bytes, 76);
	const int tagCount = readI32(bytes, 80);
	const int surfaceCount = readI32(bytes, 84);
	const int skinCount = readI32(bytes, 88);
	const qint64 frameOffset = readI32(bytes, 92);
	const qint64 tagNameOffset = readI32(bytes, 96);
	const qint64 tagOffset = readI32(bytes, 100);
	const qint64 surfaceOffset = readI32(bytes, 104);
	const qint64 endOffset = readI32(bytes, 108);

	// R_LoadMDC copies ofsEnd bytes, so the end offset bounds everything.
	if (endOffset < kMdcHeaderBytes || endOffset > bytes.size()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDC end offset (%1) does not match the file size (%2).")
			.arg(endOffset).arg(bytes.size());
		return;
	}
	if (endOffset < bytes.size()) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MDC ends at byte %1 of %2; the bytes after it are ignored.")
			.arg(endOffset).arg(bytes.size());
	}
	const QByteArray data = endOffset < bytes.size() ? bytes.left(qsizetype(endOffset)) : bytes;

	if (frameCount <= 0 || frameCount > kMaxFrames || tagCount < 0 || tagCount > kMaxTags || surfaceCount < 0 || surfaceCount > kMaxSurfaces
		|| skinCount < 0 || skinCount > kMaxSkins) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDC header declares counts outside the supported range.");
		return;
	}
	if (qint64(tagCount) * qint64(frameCount) > qint64(kMaxTags) * 16LL) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDC declares more tags than can be decoded.");
		return;
	}
	// A block is fine when it is empty or lies after the header inside the file.
	const auto blockOk = [&data](qint64 offset, qint64 length) {
		return length == 0 || (offset >= kMdcHeaderBytes && rangeOk(data, offset, length));
	};
	if (!blockOk(frameOffset, qint64(frameCount) * kMdcFrameBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDC frame block lies outside the file.");
		return;
	}
	if (!blockOk(tagNameOffset, qint64(tagCount) * kMdcTagNameBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDC tag name block lies outside the file.");
		return;
	}
	if (!blockOk(tagOffset, qint64(tagCount) * qint64(frameCount) * kMdcTagBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDC tag block lies outside the file.");
		return;
	}

	if (!internalName.isEmpty()) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Internal name: %1").arg(internalName);
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Flags: 0x%1").arg(QString::number(quint32(flags), 16));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Declared skins: %1").arg(skinCount);

	// md3Frame_t
	mesh->frames.reserve(frameCount);
	for (int index = 0; index < frameCount; ++index) {
		if (!work.step()) { return; }
		const qint64 base = frameOffset + (qint64(index) * kMdcFrameBytes);
		ModelFrameInfo info;
		info.index = index;
		readVec3(data, base, &info.mins);
		readVec3(data, base + 12, &info.maxs);
		readVec3(data, base + 24, &info.origin);
		info.radius = readF32(data, base + 36);
		info.name = readFixedName(data, base + 40, 16);
		mesh->frames.append(info);
	}

	// Tag names once (mdcTagName_t), then numFrames x numTags compressed tags
	// (mdcTag_t): xyz in MD3_XYZ_SCALE steps and angles in MDC_TAG_ANGLE_SCALE
	// degrees, expanded with AnglesToAxis like R_LerpTag. MD3 tag axes are
	// rows: forward, left and up, which are the columns of the angle matrix.
	QStringList tagNames;
	tagNames.reserve(tagCount);
	for (int index = 0; index < tagCount; ++index) {
		if (!work.step()) { return; }
		tagNames.append(readFixedName(data, tagNameOffset + (qint64(index) * kMdcTagNameBytes), kMdcTagNameBytes));
	}
	mesh->tags.reserve(tagCount * frameCount);
	for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
		if (!work.step()) { return; }
		for (int index = 0; index < tagCount; ++index) {
			if (!work.step()) { return; }
			const qint64 base = tagOffset + ((qint64(frameIndex) * qint64(tagCount) + qint64(index)) * kMdcTagBytes);
			const ModelVec3 origin{float(double(readI16(data, base)) * kMdcXyzScale), float(double(readI16(data, base + 2)) * kMdcXyzScale),
				float(double(readI16(data, base + 4)) * kMdcXyzScale)};
			const float pitch = float(double(readI16(data, base + 6)) * kMdcTagAngleScale);
			const float yaw = float(double(readI16(data, base + 8)) * kMdcTagAngleScale);
			const float roll = float(double(readI16(data, base + 10)) * kMdcTagAngleScale);
			const ModelJointMatrix matrix = modelJointFromQuakeAngles(pitch, yaw, roll, origin);
			ModelTag tag;
			tag.name = tagNames.at(index);
			tag.frameIndex = frameIndex;
			tag.origin = origin;
			for (int axis = 0; axis < 3; ++axis) {
				tag.axis[(axis * 3) + 0] = matrix.m[axis];
				tag.axis[(axis * 3) + 1] = matrix.m[4 + axis];
				tag.axis[(axis * 3) + 2] = matrix.m[8 + axis];
			}
			mesh->tags.append(tag);
		}
	}

	// The surface chain is walked by each surface's own ofsEnd. Every hop is
	// bounds-checked and moves forward, so it cannot loop or leave the file.
	const std::array<ModelVec3, 256>& normalTable = mdcNormalTable();
	QVector<bool> frameUsesCompression(frameCount, false);
	QSet<QString> skinNames;
	qint64 totalVertexSlots = 0;
	qint64 offset = surfaceOffset;
	for (int walked = 0; walked < surfaceCount; ++walked) {
		if (!work.step()) { return; }
		if (offset < kMdcHeaderBytes || !rangeOk(data, offset, kMdcSurfaceHeaderBytes)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDC surface %1 lies outside the file.").arg(walked);
			return;
		}
		// mdcSurface_t
		const QString name = readFixedName(data, offset + 4, kMdcNameBytes);
		const qint32 surfaceFlags = readI32(data, offset + 68);
		const int compFrameCount = readI32(data, offset + 72);
		const int baseFrameCount = readI32(data, offset + 76);
		const int shaderCount = readI32(data, offset + 80);
		const int vertexCount = readI32(data, offset + 84);
		const int triangleCount = readI32(data, offset + 88);
		const qint64 triangleOffset = readI32(data, offset + 92);
		const qint64 shaderOffset = readI32(data, offset + 96);
		const qint64 stOffset = readI32(data, offset + 100);
		const qint64 xyzNormalOffset = readI32(data, offset + 104);
		const qint64 xyzCompressedOffset = readI32(data, offset + 108);
		const qint64 baseFrameTableOffset = readI32(data, offset + 112);
		const qint64 compFrameTableOffset = readI32(data, offset + 116);
		const qint64 surfaceEnd = readI32(data, offset + 120);

		if (surfaceEnd < kMdcSurfaceHeaderBytes || !rangeOk(data, offset, surfaceEnd)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDC surface %1 declares an end offset outside the file.").arg(walked);
			return;
		}
		if (baseFrameCount <= 0 || baseFrameCount > kMaxFrames || compFrameCount < 0 || compFrameCount > kMaxFrames || shaderCount < 0
			|| shaderCount > kMaxSkins || vertexCount <= 0 || vertexCount > kMaxSurfaceVertices || triangleCount < 0
			|| triangleCount > kMaxSurfaceTriangles) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDC surface %1 declares counts outside the supported range.").arg(walked);
			return;
		}
		const qint64 surfaceVertexSlots = qint64(vertexCount) * qint64(frameCount);
		if (surfaceVertexSlots > kMaxVertexSlots - totalVertexSlots) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The MDC declares more frame vertices than can be decoded.");
			return;
		}
		totalVertexSlots += surfaceVertexSlots;
		// Every sub-block offset is relative to the surface and must stay
		// inside the surface record.
		const auto inRecord = [&](qint64 blockOffset, qint64 blockLength) {
			if (blockLength == 0) {
				return true;
			}
			if (blockOffset < kMdcSurfaceHeaderBytes || blockLength > surfaceEnd - blockOffset) {
				return false;
			}
			return rangeOk(data, offset + blockOffset, blockLength);
		};
		// LerpCMeshVertexes ignores frameCompFrames when a surface has no
		// compressed frames, so that table is only required when it is used.
		if (!inRecord(shaderOffset, qint64(shaderCount) * kMdcShaderBytes) || !inRecord(triangleOffset, qint64(triangleCount) * kMdcTriangleBytes)
			|| !inRecord(stOffset, qint64(vertexCount) * kMdcStBytes)
			|| !inRecord(xyzNormalOffset, qint64(vertexCount) * qint64(baseFrameCount) * kMdcXyzNormalBytes)
			|| !inRecord(xyzCompressedOffset, qint64(vertexCount) * qint64(compFrameCount) * kMdcXyzCompressedBytes)
			|| !inRecord(baseFrameTableOffset, qint64(frameCount) * 2)
			|| (compFrameCount > 0 && !inRecord(compFrameTableOffset, qint64(frameCount) * 2))) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "MDC surface %1 has a data block outside its record.").arg(walked);
			return;
		}

		ModelSurface surface;
		surface.index = mesh->surfaces.size();
		surface.name = name.isEmpty() ? QStringLiteral("surface%1").arg(surface.index) : name;
		surface.vertexCount = vertexCount;
		if (surfaceFlags != 0) {
			surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "Surface flags: 0x%1").arg(QString::number(quint32(surfaceFlags), 16));
		}

		// md3Shader_t
		for (int index = 0; index < shaderCount; ++index) {
			if (!work.step()) { return; }
			const QString shader = readFixedName(data, offset + shaderOffset + (qint64(index) * kMdcShaderBytes), kMdcNameBytes);
			if (shader.isEmpty()) {
				continue;
			}
			surface.skinPaths.append(shader);
			if (!skinNames.contains(shader)) {
				skinNames.insert(shader);
				mesh->skinPaths.append(shader);
			}
		}

		// md3St_t: the same image-space coordinates MD3 stores.
		surface.texCoords.reserve(vertexCount);
		for (int index = 0; index < vertexCount; ++index) {
			if (!work.step()) { return; }
			const qint64 base = offset + stOffset + (qint64(index) * kMdcStBytes);
			surface.texCoords.append(ModelTexCoord{readF32(data, base), readF32(data, base + 4)});
		}

		// md3Triangle_t. MDC is MD3 lineage and draws through the same
		// back end, which culls with GL_FRONT (clockwise front faces), so the
		// file's order is reversed here for counter-clockwise fronts.
		int skippedTriangles = 0;
		surface.triangles.reserve(triangleCount);
		for (int index = 0; index < triangleCount; ++index) {
			if (!work.step()) { return; }
			const qint64 base = offset + triangleOffset + (qint64(index) * kMdcTriangleBytes);
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

		surface.frames.reserve(frameCount);
		for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
			if (!work.step()) { return; }
			const int baseFrame = readI16(data, offset + baseFrameTableOffset + (qint64(frameIndex) * 2));
			const int compFrame = compFrameCount > 0 ? int(readI16(data, offset + compFrameTableOffset + (qint64(frameIndex) * 2))) : -1;
			if (baseFrame < 0 || baseFrame >= baseFrameCount) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Frame %1 of MDC surface %2 names base frame %3, which does not exist.")
					.arg(frameIndex).arg(walked).arg(baseFrame);
				return;
			}
			if (compFrame < -1 || compFrame >= compFrameCount) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Frame %1 of MDC surface %2 names compressed frame %3, which does not exist.")
					.arg(frameIndex).arg(walked).arg(compFrame);
				return;
			}
			if (compFrame >= 0) {
				frameUsesCompression[frameIndex] = true;
			}
			ModelFrameGeometry geometry;
			geometry.positions.resize(vertexCount);
			geometry.normals.resize(vertexCount);
			const qint64 baseBlock = offset + xyzNormalOffset + (qint64(baseFrame) * qint64(vertexCount) * kMdcXyzNormalBytes);
			const qint64 compBlock = offset + xyzCompressedOffset + (qint64(std::max(compFrame, 0)) * qint64(vertexCount) * kMdcXyzCompressedBytes);
			for (int index = 0; index < vertexCount; ++index) {
				if (!work.step()) { return; }
				const qint64 base = baseBlock + (qint64(index) * kMdcXyzNormalBytes);
				ModelVec3 position{float(double(readI16(data, base)) * kMdcXyzScale), float(double(readI16(data, base + 2)) * kMdcXyzScale),
					float(double(readI16(data, base + 4)) * kMdcXyzScale)};
				ModelVec3 normal;
				if (compFrame >= 0) {
					// R_MDC_DecodeXyzCompressed: each byte is an offset of
					// (value - MDC_MAX_OFS) * MDC_DIST_SCALE units, and the top
					// byte indexes the normal table.
					const quint32 packed = readU32(data, compBlock + (qint64(index) * kMdcXyzCompressedBytes));
					position.x += float((double(packed & 0xFFu) - kMdcMaxOffset) * kMdcDistScale);
					position.y += float((double((packed >> 8) & 0xFFu) - kMdcMaxOffset) * kMdcDistScale);
					position.z += float((double((packed >> 16) & 0xFFu) - kMdcMaxOffset) * kMdcDistScale);
					normal = normalTable[size_t(packed >> 24)];
				} else {
					normal = mdcLatLongNormal(readU16(data, base + 6));
				}
				geometry.positions[index] = position;
				geometry.normals[index] = normal;
			}
			surface.frames.append(geometry);
		}

		mesh->surfaces.append(surface);
		offset += surfaceEnd;
	}

	int compressedFrames = 0;
	for (const bool compressed : frameUsesCompression) {
		if (compressed) {
			++compressedFrames;
		}
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Compressed frames: %1 of %2 frame(s) add stored offsets to a base frame.")
		.arg(compressedFrames).arg(frameCount);

	mesh->skinCount = std::max(skinCount, int(mesh->skinPaths.size()));
	for (int index = 0; index < mesh->frames.size(); ++index) {
		if (!work.step()) { return; }
		const QString stem = mdcAnimationStem(mesh->frames.at(index).name);
		if (!mesh->animations.isEmpty() && mesh->animations.last().name == stem
			&& mesh->animations.last().firstFrame + mesh->animations.last().frameCount == index) {
			mesh->animations.last().frameCount += 1;
			continue;
		}
		ModelAnimation animation;
		animation.name = stem;
		animation.firstFrame = index;
		animation.frameCount = 1;
		mesh->animations.append(animation);
	}
	mesh->geometryAvailable = !mesh->surfaces.isEmpty();
	if (!mesh->geometryAvailable) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The MDC holds no surfaces.");
	}
}

} // namespace vibestudio::model_formats
