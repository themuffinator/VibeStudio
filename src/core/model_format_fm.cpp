// Heretic II flexible models (.fm).
//
// Layout: Raven's FM format as published with the GPL qdata_heretic2 tool in
// GtkRadiant (https://github.com/TTimo/GtkRadiant, 1.6-release at
// 270af88f3c24, reviewed 2026-10-08, GPL-2.0-or-later): tools/quake2/qdata_heretic2/qcommon/fmodel.h (fmheader_t,
// fmstvert_t, fmtriangle_t, fmmeshnode_t, fmaliasframe_t, the chunk names,
// versions and MAX_FM_* limits), qcommon/flex.h (header_t: the chunk header)
// and fmodels.c (WriteModelFile, BuildGlCmds); loader behaviour from the
// GPL-3.0 Heretic2R port (https://github.com/m-x-d/Heretic2R, main at
// 4d677156a458, reviewed 2026-10-08): src/ref_gl1/src/gl1_FlexModel.c (Mod_LoadFlexModel and its
// block loaders). Written independently from those descriptions.
//
// A file is a run of chunks, each a 40-byte header_t (char ident[32], int
// version, int size) and `size` bytes of data. Triangles and frames follow
// Quake II's MD2: clockwise front faces (the renderer culls GL_FRONT), byte
// positions scaled per frame, and indices into the 162-entry normal table.
// They are reversed here to counter-clockwise fronts. Unlike MD2, qdata_heretic2
// writes GL command texture coordinates as s / skinwidth without the half-texel
// offset (fmodels.c BuildGlCmds), so texture coordinates are decoded that way.
// Mesh nodes select triangles through a 2048-bit mask; each becomes a surface.
#include "core/model_formats_p.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio::model_formats {

namespace {

constexpr qint64 kChunkHeaderBytes = 40;
constexpr qint64 kChunkIdentBytes = 32;
constexpr qint64 kHeaderFieldBytes = 40;
constexpr qint64 kSkinNameBytes = 64;      // MAX_FM_SKINNAME
constexpr qint64 kStBytes = 4;
constexpr qint64 kTriangleBytes = 12;
constexpr qint64 kFrameHeaderBytes = 40;
constexpr qint64 kFrameNameBytes = 16;
constexpr qint64 kVertexBytes = 4;
constexpr qint64 kMeshNodeBytes = 516;     // tris[256], verts[256], start_glcmds, num_glcmds
constexpr int kMeshNodeTriangleBits = 2048; // MAX_FM_TRIANGLES
constexpr int kMaxChunks = 256;
constexpr int kMaxSkinDimension = 8192;
constexpr int kMaxMeshNodes = 256;

constexpr int kHeaderVersion = 2;   // FM_HEADER_VER
constexpr int kSkinVersion = 1;     // FM_SKIN_VER
constexpr int kStVersion = 1;       // FM_ST_VER
constexpr int kTriangleVersion = 1; // FM_TRI_VER
constexpr int kFrameVersion = 1;    // FM_FRAME_VER
constexpr int kGlCmdVersion = 1;    // FM_GLCMDS_VER
constexpr int kMeshVersion = 3;     // FM_MESH_VER

struct Chunk {
	QString ident;
	int version = 0;
	qint64 offset = 0;
	qint64 size = 0;
};

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
// ("walk1".."walk8"), as for MD2.
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

const Chunk* findChunk(const QVector<Chunk>& chunks, const char* ident)
{
	for (const Chunk& chunk : chunks) {
		if (chunk.ident.compare(QLatin1String(ident), Qt::CaseInsensitive) == 0) {
			return &chunk;
		}
	}
	return nullptr;
}

} // namespace

void decodeHereticFm(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }

	// Walk the chunk list; every chunk must fit inside the file.
	QVector<Chunk> chunks;
	qint64 offset = 0;
	while (offset < qint64(bytes.size())) {
		if (!work.step()) { return; }
		if (!rangeOk(bytes, offset, kChunkHeaderBytes)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A Heretic II FM chunk header is truncated.");
			return;
		}
		Chunk chunk;
		chunk.ident = readFixedName(bytes, offset, kChunkIdentBytes);
		chunk.version = readI32(bytes, offset + kChunkIdentBytes);
		chunk.size = readI32(bytes, offset + kChunkIdentBytes + 4);
		chunk.offset = offset + kChunkHeaderBytes;
		if (chunk.size < 0 || !rangeOk(bytes, chunk.offset, chunk.size)) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM chunk \"%1\" runs past the end of the file.").arg(chunk.ident);
			return;
		}
		if (chunks.size() >= kMaxChunks) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM holds more chunks than can be decoded.");
			return;
		}
		chunks.append(chunk);
		offset = chunk.offset + chunk.size;
	}
	if (chunks.isEmpty() || chunks.first().ident.compare(QStringLiteral("header"), Qt::CaseInsensitive) != 0) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM does not open with a header chunk.");
		return;
	}

	// fmheader_t: skinwidth, skinheight, framesize, num_skins, num_xyz, num_st,
	// num_tris, num_glcmds, num_frames, num_mesh_nodes.
	const Chunk& headerChunk = chunks.first();
	mesh->version = headerChunk.version;
	if (headerChunk.version != kHeaderVersion) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported Heretic II FM header version %1; only version 2 is decoded.").arg(headerChunk.version);
		return;
	}
	if (headerChunk.size < kHeaderFieldBytes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM header chunk is truncated.");
		return;
	}
	const qint64 h = headerChunk.offset;
	const int skinWidth = readI32(bytes, h);
	const int skinHeight = readI32(bytes, h + 4);
	const int frameSize = readI32(bytes, h + 8);
	const int skinCount = readI32(bytes, h + 12);
	const int positionCount = readI32(bytes, h + 16);
	const int stCount = readI32(bytes, h + 20);
	const int triangleCount = readI32(bytes, h + 24);
	const int glCommandCount = readI32(bytes, h + 28);
	const int frameCount = readI32(bytes, h + 32);
	const int meshNodeCount = readI32(bytes, h + 36);
	if (skinWidth <= 0 || skinHeight <= 0 || skinWidth > kMaxSkinDimension || skinHeight > kMaxSkinDimension) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM skin size is outside the supported range.");
		return;
	}
	if (skinCount < 0 || skinCount > kMaxSkins || positionCount <= 0 || positionCount > kMaxSurfaceVertices || stCount <= 0 || stCount > kMaxSurfaceVertices
		|| triangleCount <= 0 || triangleCount > kMaxSurfaceTriangles || frameCount <= 0 || frameCount > kMaxFrames || glCommandCount < 0
		|| meshNodeCount < 0 || meshNodeCount > kMaxMeshNodes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM header declares counts outside the supported range.");
		return;
	}
	const qint64 minimumFrameBytes = kFrameHeaderBytes + qint64(positionCount) * kVertexBytes;
	if (frameSize < minimumFrameBytes || frameSize > minimumFrameBytes + 4096) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM frame size does not match the declared vertex count.");
		return;
	}

	QStringList chunkNames;
	for (const Chunk& chunk : chunks) {
		chunkNames << chunk.ident;
	}
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Positions: %1").arg(positionCount);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Texture coordinates: %1").arg(stCount);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "GL commands: %1").arg(glCommandCount);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Mesh nodes: %1").arg(meshNodeCount);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Chunks: %1").arg(chunkNames.join(QStringLiteral(", ")));

	// Chunks this decoder does not use are listed; unknown ones are skipped,
	// as the game skips them.
	static const char* const kListedChunks[] = {"short frames", "normals", "comp data", "skeleton", "references"};
	static const char* const kDecodedChunks[] = {"header", "skin", "st coord", "tris", "frames", "glcmds", "mesh nodes"};
	QSet<QString> seen;
	for (const Chunk& chunk : chunks) {
		if (!work.step()) { return; }
		const QString key = chunk.ident.toLower();
		if (seen.contains(key)) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM repeats the chunk \"%1\"; the first one is used.").arg(chunk.ident);
			continue;
		}
		seen.insert(key);
		bool known = false;
		for (const char* name : kDecodedChunks) {
			known = known || key == QLatin1String(name);
		}
		bool listed = false;
		for (const char* name : kListedChunks) {
			listed = listed || key == QLatin1String(name);
		}
		if (listed) {
			mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Chunk \"%1\" (version %2, %3 bytes) is kept in the file but not decoded.")
				.arg(chunk.ident).arg(chunk.version).arg(chunk.size);
		} else if (!known) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM holds an unknown chunk \"%1\", which was skipped.").arg(chunk.ident);
		}
	}

	const Chunk* skinChunk = findChunk(chunks, "skin");
	const Chunk* stChunk = findChunk(chunks, "st coord");
	const Chunk* triangleChunk = findChunk(chunks, "tris");
	const Chunk* frameChunk = findChunk(chunks, "frames");
	const Chunk* glCommandChunk = findChunk(chunks, "glcmds");
	const Chunk* meshChunk = findChunk(chunks, "mesh nodes");
	if (!frameChunk && findChunk(chunks, "comp data")) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM stores only compressed frames, which are not decoded.");
		return;
	}
	if (!stChunk || !triangleChunk || !frameChunk) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM lacks its texture coordinate, triangle or frame chunk.");
		return;
	}
	const auto versionOk = [mesh](const Chunk* chunk, int expected) {
		if (chunk->version == expected) {
			return true;
		}
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "Unsupported Heretic II FM \"%1\" chunk version %2; version %3 is decoded.")
			.arg(chunk->ident).arg(chunk->version).arg(expected);
		return false;
	};
	if ((skinChunk && !versionOk(skinChunk, kSkinVersion)) || !versionOk(stChunk, kStVersion) || !versionOk(triangleChunk, kTriangleVersion)
		|| !versionOk(frameChunk, kFrameVersion) || (meshChunk && !versionOk(meshChunk, kMeshVersion))) {
		return;
	}
	if (stChunk->size < qint64(stCount) * kStBytes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM texture coordinate chunk is shorter than the header declares.");
		return;
	}
	if (triangleChunk->size < qint64(triangleCount) * kTriangleBytes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM triangle chunk is shorter than the header declares.");
		return;
	}
	if (frameChunk->size < qint64(frameCount) * qint64(frameSize)) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM frame chunk is shorter than the header declares.");
		return;
	}
	if (glCommandChunk && glCommandChunk->version == kGlCmdVersion && glCommandChunk->size < qint64(glCommandCount) * 4) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM GL command chunk is shorter than the header declares.");
	}

	// Skin names. The game refuses a skin chunk that does not hold exactly
	// num_skins names, but loads a model without one (and so without skins).
	if (skinCount > 0 && !skinChunk) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM declares %1 skin(s) but holds no skin chunk.").arg(skinCount);
	} else if (skinCount > 0) {
		if (skinChunk->size != qint64(skinCount) * kSkinNameBytes) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM skin chunk does not hold the %1 skin name(s) the header declares.").arg(skinCount);
			return;
		}
		for (int index = 0; index < skinCount; ++index) {
			if (!work.step()) { return; }
			// Skin slots are indexed by the game; keep empty ones in place.
			mesh->skinPaths.append(readFixedName(bytes, skinChunk->offset + qint64(index) * kSkinNameBytes, kSkinNameBytes));
		}
	}
	mesh->skinCount = skinCount;

	// Mesh nodes: which triangles each node draws.
	QVector<QVector<int>> nodeTriangles;
	bool haveNodes = false;
	if (meshNodeCount > 0) {
		if (!meshChunk) {
			mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM declares %1 mesh node(s) but holds no mesh node chunk; it is shown as one surface.").arg(meshNodeCount);
		} else if (meshChunk->size < qint64(meshNodeCount) * kMeshNodeBytes) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM mesh node chunk is shorter than the header declares.");
			return;
		} else {
			haveNodes = true;
			const int maskedTriangles = std::min(triangleCount, kMeshNodeTriangleBits);
			QVector<bool> used(triangleCount, false);
			for (int node = 0; node < meshNodeCount; ++node) {
				if (!work.step()) { return; }
				const qint64 base = meshChunk->offset + qint64(node) * kMeshNodeBytes;
				QVector<int> selected;
				for (int triangle = 0; triangle < maskedTriangles; ++triangle) {
					if (!work.step()) { return; }
					if (readU8(bytes, base + (triangle >> 3)) & (1 << (triangle & 7))) {
						selected.append(triangle);
						used[triangle] = true;
					}
				}
				mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Mesh node %1: %2 triangle(s), GL commands from %3 (%4)")
					.arg(node).arg(selected.size()).arg(readI16(bytes, base + 512)).arg(readI16(bytes, base + 514));
				nodeTriangles.append(selected);
			}
			const int unused = int(std::count(used.cbegin(), used.cend(), false));
			if (unused > 0) {
				mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) belong to no mesh node; the game does not draw them, so they are left out.").arg(unused);
			}
		}
	}
	if (!haveNodes) {
		QVector<int> all;
		all.reserve(triangleCount);
		for (int triangle = 0; triangle < triangleCount; ++triangle) {
			all.append(triangle);
		}
		nodeTriangles.append(all);
	}

	const float skinWidthF = float(skinWidth);
	const float skinHeightF = float(skinHeight);
	QVector<ModelVec3> frameScale(frameCount);
	QVector<ModelVec3> frameTranslate(frameCount);
	for (int frame = 0; frame < frameCount; ++frame) {
		if (!work.step()) { return; }
		// fmaliasframe_t: scale[3], translate[3], name[16], verts[num_xyz].
		const qint64 base = frameChunk->offset + qint64(frame) * frameSize;
		readVec3(bytes, base, &frameScale[frame]);
		readVec3(bytes, base + 12, &frameTranslate[frame]);
		if (!isFinite(frameScale.at(frame)) || !isFinite(frameTranslate.at(frame))) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A Heretic II FM frame has a non-finite scale or translation.");
			return;
		}
		ModelFrameInfo info;
		info.index = frame;
		info.name = readFixedName(bytes, base + 24, kFrameNameBytes);
		info.mins = frameTranslate.at(frame);
		info.maxs = ModelVec3{frameScale.at(frame).x * 255.0f + info.mins.x, frameScale.at(frame).y * 255.0f + info.mins.y,
			frameScale.at(frame).z * 255.0f + info.mins.z};
		info.origin = ModelVec3{(info.mins.x + info.maxs.x) * 0.5f, (info.mins.y + info.maxs.y) * 0.5f, (info.mins.z + info.maxs.z) * 0.5f};
		mesh->frames.append(info);
	}

	qint64 vertexSlots = 0;
	int skippedTriangles = 0;
	int invalidNormals = 0;
	for (int node = 0; node < nodeTriangles.size(); ++node) {
		if (!work.step()) { return; }
		const QVector<int>& selected = nodeTriangles.at(node);
		if (selected.isEmpty()) {
			continue;
		}
		ModelSurface surface;
		surface.index = int(mesh->surfaces.size());
		surface.name = haveNodes ? QStringLiteral("node%1").arg(node) : surfaceName(mesh->sourcePath);
		surface.skinPaths = mesh->skinPaths;
		// Corners are unique (position, texture coordinate) pairs.
		QVector<int> positionForCorner;
		QHash<qint64, int> cornerForKey;
		for (const int triangleIndex : selected) {
			if (!work.step()) { return; }
			// fmtriangle_t: short index_xyz[3], short index_st[3].
			const qint64 base = triangleChunk->offset + qint64(triangleIndex) * kTriangleBytes;
			int corners[3] = {0, 0, 0};
			bool valid = true;
			for (int corner = 0; corner < 3 && valid; ++corner) {
				const int position = readI16(bytes, base + corner * 2);
				const int st = readI16(bytes, base + 6 + corner * 2);
				if (position < 0 || position >= positionCount || st < 0 || st >= stCount) {
					valid = false;
					break;
				}
				const qint64 key = qint64(position) * stCount + st;
				const auto found = cornerForKey.constFind(key);
				if (found != cornerForKey.constEnd()) {
					corners[corner] = found.value();
					continue;
				}
				corners[corner] = int(positionForCorner.size());
				positionForCorner.append(position);
				cornerForKey.insert(key, corners[corner]);
				ModelTexCoord coord;
				coord.u = float(readI16(bytes, stChunk->offset + qint64(st) * kStBytes)) / skinWidthF;
				coord.v = float(readI16(bytes, stChunk->offset + qint64(st) * kStBytes + 2)) / skinHeightF;
				surface.texCoords.append(coord);
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
		if (surface.triangles.isEmpty()) {
			continue;
		}
		surface.vertexCount = int(positionForCorner.size());
		vertexSlots += qint64(surface.vertexCount) * frameCount;
		if (vertexSlots > kMaxVertexSlots) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM declares more frame vertices than can be decoded.");
			return;
		}
		surface.frames.reserve(frameCount);
		for (int frame = 0; frame < frameCount; ++frame) {
			if (!work.step()) { return; }
			const qint64 vertexBase = frameChunk->offset + qint64(frame) * frameSize + kFrameHeaderBytes;
			const ModelVec3& scale = frameScale.at(frame);
			const ModelVec3& translate = frameTranslate.at(frame);
			ModelFrameGeometry geometry;
			geometry.positions.resize(surface.vertexCount);
			geometry.normals.resize(surface.vertexCount);
			for (int corner = 0; corner < surface.vertexCount; ++corner) {
				if (!work.step()) { return; }
				// fmtrivertx_t: v[3] scaled by the frame, lightnormalindex.
				const qint64 vertex = vertexBase + qint64(positionForCorner.at(corner)) * kVertexBytes;
				geometry.positions[corner] = ModelVec3{scale.x * float(readU8(bytes, vertex)) + translate.x, scale.y * float(readU8(bytes, vertex + 1)) + translate.y,
					scale.z * float(readU8(bytes, vertex + 2)) + translate.z};
				const int normalIndex = readU8(bytes, vertex + 3);
				invalidNormals += normalIndex >= 162 ? 1 : 0;
				geometry.normals[corner] = modelAliasNormal(normalIndex);
			}
			surface.frames.append(geometry);
		}
		if (mesh->surfaces.size() >= kMaxSurfaces) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM holds more surfaces than can be decoded.");
			return;
		}
		mesh->surfaces.append(surface);
	}
	if (skippedTriangles > 0) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 triangle(s) referenced out-of-range indices and were dropped.").arg(skippedTriangles);
	}
	if (invalidNormals > 0) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM has %1 invalid normal indices; preview substitutes +Z.").arg(invalidNormals);
	}
	if (mesh->surfaces.isEmpty()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The Heretic II FM contains no usable triangles.");
		return;
	}
	mesh->animations = inferAnimations(mesh->frames, work);
	if (!mesh->error.isEmpty()) { return; }
	mesh->geometryAvailable = true;
}

} // namespace vibestudio::model_formats
