// Build-engine KVX voxel models, as ZDoom-family ports (GZDoom through
// VOXELDEF) load and place them, meshed into coloured faces.
//
// Layout: Ken Silverman's KVX description (slab6.txt) as GZDoom reads it in
// R_LoadKVX (src/common/models/voxels.cpp, BSD licence):
//   int32 numbytes                 bytes of this mip level after this field
//   int32 xsiz, ysiz, zsiz
//   int32 xpivot, ypivot, zpivot   8.8 fixed point, in voxels
//   int32 xoffset[xsiz + 1]        from the start of xoffset; xoffset[0] is
//                                  the size of both offset tables
//   uint16 xyoffset[xsiz][ysiz + 1] from the start of column x's slabs
//   slabs: uint8 ztop, uint8 zleng, uint8 vis, uint8 colour[zleng]
//   further mip levels (each with its own numbytes), then the 768-byte
//   palette of 6-bit VGA components that ends the file.
// Only the first mip level is decoded. The later ones are walked the way
// GZDoom walks them; GZDoom refuses a file whose levels do not end exactly
// where the palette starts, and the decoder warns about one.
//
// Faces follow FVoxelModel::MakeSlabPolys (src/common/models/models_voxel.cpp):
// vis bit 1 = -x side, 2 = +x, 4 = -y, 8 = +y, 16 = the top face (low z;
// Build's z points down) of the slab's first voxel, 32 = the bottom face of
// its last voxel. Side bits apply to every voxel of the slab.
//
// Placement: FVoxelModel::AddFace puts a voxel corner (x, y, z) at model
// (x - xpivot, -(y - ypivot), -(z - zpivot)) in game axes (GZDoom's vertex
// x, z, y are game x, y, z, as its MD3 loader shows by writing MD3's (x, y, z)
// as (x, z, y)). VOXELDEF always adds 90 degrees to AngleOffset (VoxelOptions
// starts at DAngle90 and "angleoffset" parses as value + 90), and the model
// renderer turns the model by -angleoffset about its vertical axis, which is
// a quarter turn anticlockwise seen from above in game axes. The studio
// applies both, so a voxel corner lands at
//     (y - ypivot, x - xpivot, zpivot - z)
// and +X is the way an actor at angle 0 faces, as it is for MD3 models. One
// voxel is one map unit (VOXELDEF Scale defaults to 1). GZDoom also draws
// voxels with the map's vertical pixel stretch, as it draws sprites; that is a
// display aspect, not part of the model, and is not applied.
//
// Colours: GZDoom's FVoxelTexture is a 16x16 image with one texel per palette
// entry (index i at column i & 15, row i >> 4; components scaled
// (c << 2) | (c >> 4)), and every corner of a face takes the UV of its
// colour's texel centre. The decoder builds the same image as an embedded
// skin. Coplanar faces of one colour are merged into rectangles (greedy
// meshing: runs along a row, then identical runs stacked over rows), which
// keeps the triangle count down without changing what is drawn. Triangles are
// emitted counter-clockwise seen from outside (GZDoom's own order is
// clockwise, like its MD3 surfaces).
#include "core/model_formats_p.h"

#include <QCoreApplication>
#include <QHash>
#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio::model_formats {

namespace {

constexpr qint64 kKvxPaletteBytes = 768;
constexpr qint64 kKvxHeaderBytes = 24;
constexpr int kKvxMaxMips = 5;
constexpr int kKvxMaxSize = 1024;
constexpr qint64 kKvxMaxFaces = 2LL * 1024LL * 1024LL;

// Face directions in voxel space, in vis-bit order.
enum KvxDirection { MinusX = 0, PlusX, MinusY, PlusY, Top, Bottom };

// A visible voxel face packed for sorting by (direction, plane, row, column):
// direction << 56 | plane << 40 | row << 24 | column << 8 | colour.
quint64 packFace(int direction, int plane, int row, int column, int colour)
{
	return (quint64(direction) << 56) | (quint64(plane) << 40) | (quint64(row) << 24) | (quint64(column) << 8) | quint64(colour);
}

struct KvxRect {
	int direction = 0;
	int plane = 0;
	int u0 = 0;
	int u1 = 0;
	int v0 = 0;
	int v1 = 0;
	int colour = 0;
};

// Voxel-space corner of a face rectangle: (column, row) on `plane`.
std::array<int, 3> voxelCorner(int direction, int plane, int column, int row)
{
	switch (direction) {
	case MinusX:
	case PlusX:
		return {plane, column, row};
	case MinusY:
	case PlusY:
		return {column, plane, row};
	default:
		return {column, row, plane};
	}
}

// Outward normal of each direction in game axes, through the placement
// (x, y, z) -> (y, x, -z) described above.
ModelVec3 gameNormal(int direction)
{
	switch (direction) {
	case MinusX:
		return {0.0f, -1.0f, 0.0f};
	case PlusX:
		return {0.0f, 1.0f, 0.0f};
	case MinusY:
		return {-1.0f, 0.0f, 0.0f};
	case PlusY:
		return {1.0f, 0.0f, 0.0f};
	case Top:
		return {0.0f, 0.0f, 1.0f};
	default:
		return {0.0f, 0.0f, -1.0f};
	}
}

} // namespace

void decodeKvx(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &mesh->error);
	if (!work.check()) { return; }
	const qint64 size = bytes.size();
	if (size < 4 + kKvxHeaderBytes + kKvxPaletteBytes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX file is too small to hold a voxel header and its palette.");
		return;
	}
	const qint64 available = size - kKvxPaletteBytes - 4;
	const qint64 numBytes = readI32(bytes, 0);
	if (numBytes < kKvxHeaderBytes || numBytes > available) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The first KVX mip level declares %1 bytes, outside the %2 bytes the file holds before its palette.")
			.arg(numBytes).arg(available);
		return;
	}
	const qint64 base = 4;
	const int xsiz = readI32(bytes, base);
	const int ysiz = readI32(bytes, base + 4);
	const int zsiz = readI32(bytes, base + 8);
	const double pivotX = double(readI32(bytes, base + 12)) / 256.0;
	const double pivotY = double(readI32(bytes, base + 16)) / 256.0;
	const double pivotZ = double(readI32(bytes, base + 20)) / 256.0;
	if (xsiz < 0 || ysiz < 0 || zsiz < 0 || xsiz > kKvxMaxSize || ysiz > kKvxMaxSize || zsiz > kKvxMaxSize) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX voxel dimensions %1 x %2 x %3 are outside the supported range.")
			.arg(xsiz).arg(ysiz).arg(zsiz);
		return;
	}
	const qint64 offsetBytes = (qint64(xsiz) + 1) * 4 + qint64(xsiz) * (qint64(ysiz) + 1) * 2;
	const qint64 voxelBytes = numBytes - kKvxHeaderBytes - offsetBytes;
	if (voxelBytes < 0) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX offset tables run past the end of the first mip level.");
		return;
	}
	const qint64 xOffsetBase = base + kKvxHeaderBytes;
	const qint64 xyOffsetBase = xOffsetBase + (qint64(xsiz) + 1) * 4;
	const qint64 slabBase = xOffsetBase + offsetBytes;
	QVector<qint64> columnStart(xsiz + 1);
	for (int x = 0; x <= xsiz; ++x) {
		if (!work.step()) { return; }
		columnStart[x] = qint64(readI32(bytes, xOffsetBase + qint64(x) * 4)) - offsetBytes;
		if (columnStart.at(x) < 0 || columnStart.at(x) > voxelBytes || (x > 0 && columnStart.at(x) < columnStart.at(x - 1))) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "KVX column offset %1 lies outside the voxel data.").arg(x);
			return;
		}
	}
	if (columnStart.constFirst() != 0 || columnStart.constLast() != voxelBytes) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX column offsets do not span the voxel data.");
		return;
	}

	// Visible faces, from the slabs.
	QVector<quint64> faces;
	qint64 voxels = 0;
	int highestZ = 0;
	for (int x = 0; x < xsiz; ++x) {
		if (!work.step()) { return; }
		for (int y = 0; y < ysiz; ++y) {
			if (!work.step()) { return; }
			const qint64 entry = xyOffsetBase + (qint64(x) * (qint64(ysiz) + 1) + y) * 2;
			const qint64 start = columnStart.at(x) + readU16(bytes, entry);
			const qint64 end = columnStart.at(x) + readU16(bytes, entry + 2);
			if (start > end || end > voxelBytes) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX slab offsets of column %1, %2 lie outside the voxel data.").arg(x).arg(y);
				return;
			}
			qint64 position = start;
			while (position < end) {
				if (!work.step()) { return; }
				if (end - position < 3) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A KVX slab in column %1, %2 is truncated.").arg(x).arg(y);
					return;
				}
				const qint64 at = slabBase + position;
				const int top = readU8(bytes, at);
				const int length = readU8(bytes, at + 1);
				const int visible = readU8(bytes, at + 2);
				if (end - position - 3 < length) {
					mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "A KVX slab in column %1, %2 is truncated.").arg(x).arg(y);
					return;
				}
				for (int index = 0; index < length; ++index) {
					if (!work.step()) { return; }
					const int z = top + index;
					const int colour = readU8(bytes, at + 3 + index);
					highestZ = std::max(highestZ, z + 1);
					++voxels;
					if (faces.size() + 6 > kKvxMaxFaces) {
						mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX model has more than %1 visible voxel faces.").arg(kKvxMaxFaces);
						return;
					}
					// Row and column follow voxelCorner: x faces span (y, z), y faces
					// (x, z), z faces (x, y).
					if (visible & 1) { faces.append(packFace(MinusX, x, z, y, colour)); }
					if (visible & 2) { faces.append(packFace(PlusX, x + 1, z, y, colour)); }
					if (visible & 4) { faces.append(packFace(MinusY, y, z, x, colour)); }
					if (visible & 8) { faces.append(packFace(PlusY, y + 1, z, x, colour)); }
					if ((visible & 16) && index == 0) { faces.append(packFace(Top, z, y, x, colour)); }
					if ((visible & 32) && index == length - 1) { faces.append(packFace(Bottom, z + 1, y, x, colour)); }
				}
				position += 3 + length;
			}
		}
	}

	// Later mip levels, walked as R_LoadKVX walks them.
	int mips = 1;
	qint64 cursor = 4 + numBytes;
	qint64 remaining = available - numBytes;
	while (mips < kKvxMaxMips && rangeOk(bytes, cursor, 4)) {
		const qint64 levelBytes = readI32(bytes, cursor);
		if (levelBytes > remaining - 4 || levelBytes < kKvxHeaderBytes) {
			break;
		}
		cursor += 4 + levelBytes;
		remaining -= levelBytes + 4;
		++mips;
	}
	if (cursor != size - kKvxPaletteBytes) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The KVX mip levels do not end where the palette begins; GZDoom refuses such files.");
	}

	if (faces.isEmpty()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX model has no visible voxel faces.");
		return;
	}
	std::sort(faces.begin(), faces.end());

	// Greedy merge per (direction, plane): runs of one colour along each row,
	// then runs with the same extent and colour stacked on consecutive rows.
	QVector<KvxRect> rects;
	{
		QHash<quint64, int> open;
		QHash<quint64, int> nextOpen;
		int groupDirection = -1;
		int groupPlane = -1;
		qsizetype index = 0;
		while (index < faces.size()) {
			if (!work.step()) { return; }
			const quint64 face = faces.at(index);
			const int direction = int(face >> 56);
			const int plane = int((face >> 40) & 0xFFFF);
			const int row = int((face >> 24) & 0xFFFF);
			if (direction != groupDirection || plane != groupPlane) {
				open.clear();
				groupDirection = direction;
				groupPlane = plane;
			}
			// Collect this row's runs.
			nextOpen.clear();
			while (index < faces.size()) {
				const quint64 first = faces.at(index);
				if (int(first >> 56) != direction || int((first >> 40) & 0xFFFF) != plane || int((first >> 24) & 0xFFFF) != row) {
					break;
				}
				const int column0 = int((first >> 8) & 0xFFFF);
				const int colour = int(first & 0xFF);
				int column1 = column0 + 1;
				++index;
				while (index < faces.size()) {
					if (!work.step()) { return; }
					const quint64 next = faces.at(index);
					if ((next >> 24) != (first >> 24) || int((next >> 8) & 0xFFFF) != column1 || int(next & 0xFF) != colour) {
						break;
					}
					++column1;
					++index;
				}
				const quint64 key = (quint64(column0) << 32) | (quint64(column1) << 8) | quint64(colour);
				const auto found = open.constFind(key);
				if (found != open.constEnd() && rects.at(found.value()).v1 == row) {
					rects[found.value()].v1 = row + 1;
					nextOpen.insert(key, found.value());
				} else {
					KvxRect rect;
					rect.direction = direction;
					rect.plane = plane;
					rect.u0 = column0;
					rect.u1 = column1;
					rect.v0 = row;
					rect.v1 = row + 1;
					rect.colour = colour;
					rects.append(rect);
					nextOpen.insert(key, int(rects.size()) - 1);
				}
			}
			open.swap(nextOpen);
		}
	}

	ModelSurface surface;
	surface.index = 0;
	surface.name = QStringLiteral("voxels");
	surface.frames.append(ModelFrameGeometry{});
	ModelFrameGeometry& geometry = surface.frames.first();
	QHash<quint64, int> vertexFor;
	const auto vertex = [&](const std::array<int, 3>& corner, int direction, int colour) -> int {
		const quint64 key = (quint64(corner[0]) << 40) | (quint64(corner[1]) << 24) | (quint64(corner[2]) << 11) | (quint64(direction) << 8) | quint64(colour);
		const auto found = vertexFor.constFind(key);
		if (found != vertexFor.constEnd()) {
			return found.value();
		}
		if (surface.vertexCount >= kMaxSurfaceVertices) {
			return -1;
		}
		const int index = surface.vertexCount++;
		geometry.positions.append(ModelVec3{float(double(corner[1]) - pivotY), float(double(corner[0]) - pivotX), float(pivotZ - double(corner[2]))});
		geometry.normals.append(gameNormal(direction));
		// The texel centre of the colour in the 16x16 palette image.
		surface.texCoords.append(ModelTexCoord{(float(colour & 15) + 0.5f) / 16.0f, (float(colour >> 4) + 0.5f) / 16.0f});
		vertexFor.insert(key, index);
		return index;
	};
	for (const KvxRect& rect : rects) {
		if (!work.step()) { return; }
		const std::array<std::array<int, 3>, 4> corners = {
			voxelCorner(rect.direction, rect.plane, rect.u0, rect.v0),
			voxelCorner(rect.direction, rect.plane, rect.u1, rect.v0),
			voxelCorner(rect.direction, rect.plane, rect.u1, rect.v1),
			voxelCorner(rect.direction, rect.plane, rect.u0, rect.v1),
		};
		int index[4];
		for (int corner = 0; corner < 4; ++corner) {
			index[corner] = vertex(corners[corner], rect.direction, rect.colour);
			if (index[corner] < 0) {
				mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX model has more vertices than can be decoded.");
				return;
			}
		}
		if (surface.triangles.size() + 2 > kMaxSurfaceTriangles) {
			mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX model has more triangles than can be decoded.");
			return;
		}
		// Wind the quad so cross(b - a, c - a) points along the face's outward
		// normal in game space.
		const ModelVec3& a = geometry.positions.at(index[0]);
		const ModelVec3& b = geometry.positions.at(index[1]);
		const ModelVec3& c = geometry.positions.at(index[2]);
		const ModelVec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
		const ModelVec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
		const ModelVec3 cross{(ab.y * ac.z) - (ab.z * ac.y), (ab.z * ac.x) - (ab.x * ac.z), (ab.x * ac.y) - (ab.y * ac.x)};
		const ModelVec3 normal = gameNormal(rect.direction);
		if ((cross.x * normal.x) + (cross.y * normal.y) + (cross.z * normal.z) > 0.0f) {
			surface.triangles.append(ModelTriangle{index[0], index[1], index[2]});
			surface.triangles.append(ModelTriangle{index[0], index[2], index[3]});
		} else {
			surface.triangles.append(ModelTriangle{index[0], index[2], index[1]});
			surface.triangles.append(ModelTriangle{index[0], index[3], index[2]});
		}
	}

	// The palette as GZDoom's FVoxelTexture builds it.
	QImage palette(16, 16, QImage::Format_ARGB32);
	if (palette.isNull()) {
		mesh->error = QCoreApplication::translate("VibeStudioModelMesh", "The KVX palette image could not be created.");
		return;
	}
	const qint64 paletteOffset = size - kKvxPaletteBytes;
	int brightComponents = 0;
	for (int index = 0; index < 256; ++index) {
		int rgb[3] = {0, 0, 0};
		for (int channel = 0; channel < 3; ++channel) {
			const int component = readU8(bytes, paletteOffset + qint64(index) * 3 + channel);
			if (component > 63) {
				++brightComponents;
			}
			rgb[channel] = ((component << 2) | (component >> 4)) & 0xFF;
		}
		palette.setPixel(index & 15, index >> 4, qRgb(rgb[0], rgb[1], rgb[2]));
	}
	if (brightComponents > 0) {
		mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The KVX palette has %1 component(s) above the 6-bit maximum of 63.").arg(brightComponents);
	}
	ModelEmbeddedSkin skin;
	skin.index = 0;
	skin.name = QStringLiteral("palette");
	skin.image = palette;
	mesh->embeddedSkins.append(skin);
	mesh->skinCount = 1;

	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Voxels: %1 x %2 x %3 (%4 solid)").arg(xsiz).arg(ysiz).arg(zsiz).arg(voxels);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Pivot: %1 %2 %3 voxels")
		.arg(QString::number(pivotX, 'f', 2), QString::number(pivotY, 'f', 2), QString::number(pivotZ, 'f', 2));
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Mip levels: %1").arg(mips);
	mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Visible voxel faces: %1, merged into %2 rectangles").arg(faces.size()).arg(rects.size());
	if (highestZ > zsiz) {
		mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Slabs reach depth %1, below the declared height of %2.").arg(highestZ).arg(zsiz);
	}

	ModelFrameInfo frame;
	frame.index = 0;
	frame.name = QStringLiteral("frame0");
	bool first = true;
	float radius = 0.0f;
	for (const ModelVec3& p : geometry.positions) {
		if (first) {
			frame.mins = p;
			frame.maxs = p;
			first = false;
		}
		frame.mins.x = std::min(frame.mins.x, p.x);
		frame.mins.y = std::min(frame.mins.y, p.y);
		frame.mins.z = std::min(frame.mins.z, p.z);
		frame.maxs.x = std::max(frame.maxs.x, p.x);
		frame.maxs.y = std::max(frame.maxs.y, p.y);
		frame.maxs.z = std::max(frame.maxs.z, p.z);
		radius = std::max(radius, std::sqrt((p.x * p.x) + (p.y * p.y) + (p.z * p.z)));
	}
	frame.radius = radius;
	mesh->frames.append(frame);
	mesh->surfaces.append(surface);
	mesh->geometryAvailable = true;
}

} // namespace vibestudio::model_formats
