// Hexen II mission-pack alias models (RAPO 50).
//
// Layout: the GPL Hexen II source, as maintained in uHexen2
// (https://github.com/sezero/uhexen2, master at 475c048b1c8b, reviewed
// 2026-10-08, GPL-2.0-or-later): common/genmodel.h (newmdl_t, stvert_t, dnewtriangle_t,
// daliasframe_t, daliasgroup_t, trivertx_t), engine/h2shared/gl_model.c
// (Mod_LoadAliasModelNew, Mod_LoadAllSkins, Mod_LoadAliasFrame,
// Mod_LoadAliasGroup), engine/h2shared/gl_mesh.c (BuildTris: texture
// coordinates) and engine/h2shared/gl_draw.c (GL_Upload8: skin transparency).
// Written independently from those descriptions.
//
// RAPO 50 is Quake's IDPO 6 with one more header field, num_st_verts, and
// triangles that index positions and texture coordinates separately
// (vertindex[3] and stindex[3]), so seams are explicit. GLQuake's on-seam rule
// still applies per texture coordinate: a back-facing triangle adds half the
// skin width to an on-seam coordinate (BuildTris). Triangles are stored with
// clockwise front faces, as in Quake (the GL renderer culls GL_FRONT), and are
// reversed here to counter-clockwise fronts.
#include "core/model_formats_p.h"

#include <QCoreApplication>
#include <QHash>
#include <QImage>

#include <algorithm>
#include <cmath>

namespace vibestudio::model_formats {

namespace {

constexpr qint64 kHeaderBytes = 88;
constexpr qint64 kStVertBytes = 12;
constexpr qint64 kTriangleBytes = 16;
constexpr qint64 kVertexBytes = 4;
constexpr qint64 kFrameNameBytes = 16;
constexpr int kVersion = 50;
constexpr int kMaxSkinDimension = 8192;
constexpr qint64 kMaxSkinPixels = 64LL * 1024LL * 1024LL;
constexpr int kMaxSkinGroupFrames = 1024;

// Hexen II's model flags (engine/h2shared/gl_model.h, EF_*).
constexpr quint32 kFlagTransparent = 1u << 12;   // EF_TRANSPARENT
constexpr quint32 kFlagHoley = 1u << 14;         // EF_HOLEY
constexpr quint32 kFlagSpecialTrans = 1u << 15;  // EF_SPECIAL_TRANS
constexpr quint32 kFlagFaceView = 1u << 16;      // EF_FACE_VIEW
constexpr quint32 kFlagMipMap = 1u << 10;        // EF_MIP_MAP
constexpr quint32 kFlagRotate = 1u << 3;         // EF_ROTATE

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

QString surfaceName(const QString& sourcePath)
{
	QString cleaned;
	for (const QChar ch : pathStem(sourcePath)) {
		if (ch.isSpace()) {
			cleaned.append(QLatin1Char('_'));
		} else if (ch.isPrint()) {
			cleaned.append(ch);
		}
	}
	return cleaned.isEmpty() ? QStringLiteral("surface0") : cleaned;
}

// Frame names encode animations as a shared stem with a trailing number
// ("stand1".."stand5"), as for Quake MDL.
QString animationStem(const QString& name)
{
	QString stem = name.trimmed();
	while (!stem.isEmpty() && stem.back().isDigit()) {
		stem.chop(1);
	}
	while (!stem.isEmpty() && (stem.back() == QLatin1Char('_') || stem.back() == QLatin1Char('-') || stem.back().isSpace())) {
		stem.chop(1);
	}
	return stem.isEmpty() ? name.trimmed() : stem;
}

QVector<ModelAnimation> inferAnimations(const QVector<ModelFrameInfo>& frames, ModelWorkProgress& work)
{
	QVector<ModelAnimation> animations;
	for (int index = 0; index < frames.size(); ++index) {
		if (!work.step()) { return {}; }
		QString stem = animationStem(frames.at(index).name);
		if (stem.isEmpty()) {
			stem = QStringLiteral("frame");
		}
		if (!animations.isEmpty() && animations.last().name == stem && animations.last().firstFrame + animations.last().frameCount == index) {
			animations.last().frameCount += 1;
			continue;
		}
		ModelAnimation animation;
		animation.name = stem;
		animation.firstFrame = index;
		animation.frameCount = 1;
		animations.append(animation);
	}
	return animations;
}

// Colours one indexed skin. Hexen II's transparent, holey and special
// translucent modes make index 255 transparent; transparent and holey skins
// also cut out index 0 (GL_Upload8).
QImage indexedSkinImage(const QByteArray& bytes, qint64 offset, int width, int height, const IdTechPalette& palette, quint32 flags,
	ModelWorkProgress& work)
{
	QImage image(width, height, QImage::Format_ARGB32);
	if (image.isNull()) {
		return {};
	}
	const bool alphaMode = (flags & (kFlagTransparent | kFlagHoley | kFlagSpecialTrans)) != 0;
	const bool cutZero = (flags & (kFlagTransparent | kFlagHoley)) != 0;
	const uchar* source = reinterpret_cast<const uchar*>(bytes.constData() + offset);
	for (int y = 0; y < height; ++y) {
		if (!work.step()) { return {}; }
		QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < width; ++x) {
			const int index = source[qint64(y) * width + x];
			const QRgb colour = palette.colorAt(index);
			const bool clear = (alphaMode && index == 255) || (cutZero && index == 0);
			row[x] = qRgba(qRed(colour), qGreen(colour), qBlue(colour), clear ? 0 : 255);
		}
	}
	return image;
}

struct Pose {
	QString name;
	qint64 vertexOffset = 0;
	ModelVec3 mins;
	ModelVec3 maxs;
};

} // namespace

void decodeHexen2Mdl(const QByteArray& bytes, const IdTechPalette& palette, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	// newmdl_t, 88 bytes.
	if (!rangeOk(bytes, 0, kHeaderBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL header is truncated.");
		return;
	}
	mesh->version = readI32(bytes, 4);
	if (bytes.left(4) != QByteArrayLiteral("RAPO") || mesh->version != kVersion) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported Hexen II MDL version %1; only RAPO version 50 is decoded.").arg(mesh->version);
		return;
	}
	ModelVec3 scale;
	ModelVec3 translate;
	ModelVec3 eyePosition;
	readVec3(bytes, 8, &scale);
	readVec3(bytes, 20, &translate);
	const float declaredRadius = readF32(bytes, 32);
	readVec3(bytes, 36, &eyePosition);
	const int skinCount = readI32(bytes, 48);
	const int skinWidth = readI32(bytes, 52);
	const int skinHeight = readI32(bytes, 56);
	const int vertexCount = readI32(bytes, 60);
	const int triangleCount = readI32(bytes, 64);
	const int frameCount = readI32(bytes, 68);
	const int syncType = readI32(bytes, 72);
	const quint32 flags = readU32(bytes, 76);
	const float declaredSize = readF32(bytes, 80);
	const int stCount = readI32(bytes, 84);

	if (!isFinite(scale) || !isFinite(translate)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL header has a non-finite scale or translation.");
		return;
	}
	if (skinCount < 0 || skinCount > kMaxSkins || vertexCount <= 0 || vertexCount > kMaxSurfaceVertices || stCount <= 0 || stCount > kMaxSurfaceVertices
		|| triangleCount <= 0 || triangleCount > kMaxSurfaceTriangles || frameCount <= 0 || frameCount > kMaxFrames) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL header declares counts outside the supported range.");
		return;
	}
	const qint64 skinPixels = qint64(std::max(0, skinWidth)) * qint64(std::max(0, skinHeight));
	if (skinCount > 0 && (skinWidth <= 0 || skinHeight <= 0 || skinWidth > kMaxSkinDimension || skinHeight > kMaxSkinDimension || skinPixels > kMaxSkinPixels)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL skin size is outside the supported range.");
		return;
	}
	if (qint64(vertexCount) * qint64(frameCount) > kMaxVertexSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL declares more frame vertices than can be decoded.");
		return;
	}

	mesh->skinCount = skinCount;
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Positions: %1").arg(vertexCount);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Texture coordinates: %1").arg(stCount);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Scale: %1").arg(formatVector(scale));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Translation: %1").arg(formatVector(translate));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Eye position: %1").arg(formatVector(eyePosition));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Declared bounding radius: %1").arg(formatCoordinate(declaredRadius));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Declared size: %1").arg(formatCoordinate(declaredSize));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Sync type: %1").arg(syncType);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Flags: 0x%1").arg(QString::number(flags, 16));
	if (flags & kFlagTransparent) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Translucent (EF_TRANSPARENT): index 0 is cut out and odd indices blend.");
	}
	if (flags & kFlagHoley) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Holey (EF_HOLEY): skin index 0 is transparent.");
	}
	if (flags & kFlagSpecialTrans) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Special translucency (EF_SPECIAL_TRANS): the game blends the skin through its translucency table.");
	}
	if (flags & kFlagFaceView) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Faces the viewer (EF_FACE_VIEW).");
	}
	if (flags & kFlagRotate) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Rotates in the game (EF_ROTATE).");
	}
	if (flags & kFlagMipMap) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Uses mip-mapped skins (EF_MIP_MAP).");
	}

	qint64 offset = kHeaderBytes;
	qint64 totalSkinPixels = 0;
	// Skins, as in Quake: a type, then the pixels of a single skin, or a member
	// count, one end time per member and the member pixel blocks.
	for (int index = 0; index < skinCount; ++index) {
		if (!work.step()) { return; }
		if (!rangeOk(bytes, offset, 4)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL skin data is truncated.");
			return;
		}
		const int type = readI32(bytes, offset);
		offset += 4;
		if (type != 0 && type != 1) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL skin type is neither a single image nor a group.");
			return;
		}
		ModelEmbeddedSkin skin;
		int members = 1;
		if (type == 1) {
			if (!rangeOk(bytes, offset, 4)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL skin data is truncated.");
				return;
			}
			members = readI32(bytes, offset);
			offset += 4;
			if (members <= 0 || members > kMaxSkinGroupFrames) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A Hexen II MDL skin group declares an unsupported frame count.");
				return;
			}
			if (!rangeOk(bytes, offset, qint64(members) * 4)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL skin data is truncated.");
				return;
			}
			float previous = 0.0f;
			for (int member = 0; member < members; ++member) {
				if (!work.step()) { return; }
				const float interval = readF32(bytes, offset + qint64(member) * 4);
				if (!std::isfinite(interval) || interval <= previous) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Hexen II MDL skin group end times must be finite, positive and strictly increasing.");
					return;
				}
				skin.intervals.append(interval);
				previous = interval;
			}
			offset += qint64(members) * 4;
		}
		const qint64 block = skinPixels * members;
		if (!rangeOk(bytes, offset, block)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL skin data is truncated.");
			return;
		}
		if (block > kMaxSkinPixels - totalSkinPixels) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Hexen II MDL skin members exceed the 64-megapixel preview limit.");
			return;
		}
		totalSkinPixels += block;
		skin.index = index;
		skin.name = QStringLiteral("skin%1").arg(index);
		skin.groupFrameCount = members;
		for (int member = 0; member < members; ++member) {
			if (!work.step()) { return; }
			skin.indexedFrames.append(bytes.mid(qsizetype(offset + member * skinPixels), qsizetype(skinPixels)));
		}
		skin.image = indexedSkinImage(bytes, offset, skinWidth, skinHeight, palette, flags, work);
		if (!mesh->error.isEmpty()) { return; }
		if (skin.image.isNull()) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Embedded skin %1 could not be decoded.").arg(index);
		}
		mesh->embeddedSkins.append(skin);
		offset += block;
	}

	// Texture coordinates (stvert_t: onseam, s, t).
	if (!rangeOk(bytes, offset, qint64(stCount) * kStVertBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL texture coordinates are truncated.");
		return;
	}
	const qint64 stOffset = offset;
	offset += qint64(stCount) * kStVertBytes;

	// Triangles (dnewtriangle_t: facesfront, vertindex[3], stindex[3]).
	if (!rangeOk(bytes, offset, qint64(triangleCount) * kTriangleBytes)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL triangle list is truncated.");
		return;
	}
	ModelSurface surface;
	surface.index = 0;
	surface.name = surfaceName(mesh->sourcePath);
	const float skinWidthF = skinWidth > 0 ? float(skinWidth) : 1.0f;
	const float skinHeightF = skinHeight > 0 ? float(skinHeight) : 1.0f;
	// Corners are unique (position, texture coordinate, back-side seam) triples.
	QVector<int> positionForCorner;
	QHash<qint64, int> cornerForKey;
	int skippedTriangles = 0;
	for (int index = 0; index < triangleCount; ++index) {
		if (!work.step()) { return; }
		const qint64 base = offset + qint64(index) * kTriangleBytes;
		const bool facesFront = readI32(bytes, base) != 0;
		int corners[3] = {0, 0, 0};
		bool valid = true;
		for (int corner = 0; corner < 3; ++corner) {
			const int position = readU16(bytes, base + 4 + corner * 2);
			const int st = readU16(bytes, base + 10 + corner * 2);
			if (position >= vertexCount || st >= stCount) {
				valid = false;
				break;
			}
			const qint64 stBase = stOffset + qint64(st) * kStVertBytes;
			const bool back = !facesFront && readI32(bytes, stBase) != 0;
			const qint64 key = ((qint64(position) * stCount + st) << 1) | (back ? 1 : 0);
			const auto found = cornerForKey.constFind(key);
			if (found != cornerForKey.constEnd()) {
				corners[corner] = found.value();
				continue;
			}
			const int combined = int(positionForCorner.size());
			positionForCorner.append(position);
			cornerForKey.insert(key, combined);
			// GLQuake's texel-centre rule, with the back-side seam shift
			// (gl_mesh.c BuildTris), matching the Quake MDL decoder.
			float s = float(readI32(bytes, stBase + 4));
			if (back) {
				s += float(skinWidth / 2);
			}
			ModelTexCoord coord;
			coord.u = (s + 0.5f) / skinWidthF;
			coord.v = (float(readI32(bytes, stBase + 8)) + 0.5f) / skinHeightF;
			surface.texCoords.append(coord);
			corners[corner] = combined;
		}
		if (!valid) {
			++skippedTriangles;
			continue;
		}
		ModelTriangle triangle;
		triangle.a = corners[0];
		triangle.b = corners[2];
		triangle.c = corners[1];
		surface.triangles.append(triangle);
	}
	if (skippedTriangles > 0) {
		surface.warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.").arg(skippedTriangles);
	}
	if (surface.triangles.isEmpty()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL contains no usable triangles.");
		return;
	}
	offset += qint64(triangleCount) * kTriangleBytes;
	const int combinedCount = int(positionForCorner.size());
	surface.vertexCount = combinedCount;

	// Frames: single poses and groups of poses, as in Quake.
	QVector<Pose> poses;
	for (int group = 0; group < frameCount; ++group) {
		if (!work.step()) { return; }
		if (!rangeOk(bytes, offset, 4)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL frame data is truncated.");
			return;
		}
		const int type = readI32(bytes, offset);
		offset += 4;
		if (type != 0 && type != 1) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL frame type is neither a single pose nor a group.");
			return;
		}
		int members = 1;
		if (type == 1) {
			if (!rangeOk(bytes, offset, 12)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL frame data is truncated.");
				return;
			}
			members = readI32(bytes, offset);
			offset += 12; // numframes, then the group's bounding box
			if (members <= 0 || members > kMaxFrames || poses.size() + members > kMaxFrames) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A Hexen II MDL frame group declares an unsupported frame count.");
				return;
			}
			if (!rangeOk(bytes, offset, qint64(members) * 4)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL frame data is truncated.");
				return;
			}
			float previous = 0.0f;
			for (int member = 0; member < members; ++member) {
				if (!work.step()) { return; }
				const float interval = readF32(bytes, offset + qint64(member) * 4);
				if (!std::isfinite(interval) || interval <= previous) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Hexen II MDL frame group end times must be finite, positive and strictly increasing.");
					return;
				}
				previous = interval;
			}
			offset += qint64(members) * 4;
		}
		for (int member = 0; member < members; ++member) {
			if (!work.step()) { return; }
			const qint64 poseBytes = 8 + kFrameNameBytes + qint64(vertexCount) * kVertexBytes;
			if (!rangeOk(bytes, offset, poseBytes)) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL frame data is truncated.");
				return;
			}
			Pose pose;
			pose.mins = ModelVec3{scale.x * float(readU8(bytes, offset)) + translate.x, scale.y * float(readU8(bytes, offset + 1)) + translate.y,
				scale.z * float(readU8(bytes, offset + 2)) + translate.z};
			pose.maxs = ModelVec3{scale.x * float(readU8(bytes, offset + 4)) + translate.x, scale.y * float(readU8(bytes, offset + 5)) + translate.y,
				scale.z * float(readU8(bytes, offset + 6)) + translate.z};
			pose.name = readFixedName(bytes, offset + 8, kFrameNameBytes);
			pose.vertexOffset = offset + 8 + kFrameNameBytes;
			poses.append(pose);
			offset += poseBytes;
			if (poses.size() > kMaxFrames) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL declares more frames than can be decoded.");
				return;
			}
		}
	}
	if (qint64(combinedCount) * poses.size() > kMaxVertexSlots) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL declares more frame vertices than can be decoded.");
		return;
	}

	int invalidNormals = 0;
	surface.frames.reserve(poses.size());
	for (int poseIndex = 0; poseIndex < poses.size(); ++poseIndex) {
		if (!work.step()) { return; }
		const Pose& pose = poses.at(poseIndex);
		ModelFrameGeometry geometry;
		geometry.positions.resize(combinedCount);
		geometry.normals.resize(combinedCount);
		for (int index = 0; index < combinedCount; ++index) {
			if (!work.step()) { return; }
			// trivertx_t: position = scale * byte + scale_origin.
			const qint64 vertex = pose.vertexOffset + qint64(positionForCorner.at(index)) * kVertexBytes;
			geometry.positions[index] = ModelVec3{scale.x * float(readU8(bytes, vertex)) + translate.x, scale.y * float(readU8(bytes, vertex + 1)) + translate.y,
				scale.z * float(readU8(bytes, vertex + 2)) + translate.z};
			const int normalIndex = readU8(bytes, vertex + 3);
			invalidNormals += normalIndex >= 162 ? 1 : 0;
			geometry.normals[index] = modelAliasNormal(normalIndex);
		}
		surface.frames.append(geometry);

		ModelFrameInfo info;
		info.index = poseIndex;
		info.name = pose.name;
		info.mins = pose.mins;
		info.maxs = pose.maxs;
		info.origin = ModelVec3{(pose.mins.x + pose.maxs.x) * 0.5f, (pose.mins.y + pose.maxs.y) * 0.5f, (pose.mins.z + pose.maxs.z) * 0.5f};
		info.radius = declaredRadius;
		mesh->frames.append(info);
	}
	if (invalidNormals > 0) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The Hexen II MDL contains invalid normal indices; preview uses fallback normals.");
	}
	mesh->surfaces.append(surface);
	mesh->animations = inferAnimations(mesh->frames, work);
	if (!mesh->error.isEmpty()) { return; }
	mesh->geometryAvailable = true;
}

} // namespace vibestudio::model_formats
