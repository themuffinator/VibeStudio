// LightWave LWO2 and LWOB static objects, read the way Doom 3 and Quake 4 load
// them.
//
// Layout: the LightWave object format as the released Doom 3 GPL source reads
// it (neo/renderer/Model_lwo.cpp, Ernie Wright's LWO2 loader: lwGetObject,
// lwGetPoints, lwGetPolygons, lwGetTags, lwGetPolygonTags, lwGetVMap,
// lwGetSurface, lwGetPolyNormals, lwGetVertNormals, and lwGetObject5 /
// lwGetPolygons5 / lwGetSurface5 for LWOB). The conversion into renderable
// surfaces follows idRenderModelStatic::ConvertLWOToModelSurfaces
// (neo/renderer/Model.cpp). This is an independent implementation; no upstream
// code is copied.
//
// Conventions, all taken from ConvertLWOToModelSurfaces:
// - Only the first layer is used ("we only ever use the first layer"), so
//   PNTS / POLS / VMAP / VMAD / PTAG chunks after the second LAYR are ignored.
//   TAGS and SURF chunks are object-wide and read wherever they appear.
// - Positions swap Y and Z: game (x, y, z) = LightWave (x, z, y). LightWave is
//   Y-up; the swap makes it Z-up.
// - Texture coordinates invert V: t = 1 - v. LightWave's V runs up the image,
//   Doom 3's t (and the studio's v) runs down from the top row.
// - Winding: LightWave lists polygon corners clockwise seen from the visible
//   side, which in LightWave's own numbers makes cross(p1 - p0, pn - p0) the
//   visible-side normal (lwGetPolyNormals). The Y/Z swap is a reflection, so in
//   game space Doom 3's corner order has cross(c - a, b - a) facing out, which
//   is the clockwise front its renderer expects (R_DeriveFacePlanes uses
//   d2 x d1). The studio stores counter-clockwise fronts, so each triangle
//   (a, b, c) in LightWave order is emitted as (a, c, b).
// - The material is the surface name (declManager->FindMaterial(lwoSurf->name)),
//   made canonical as the decl manager does: backslashes become slashes and
//   the text from the last '.' is dropped. Case is kept; Doom 3 compares
//   material names without case.
// - Vertex normals follow lwGetVertNormals: a polygon's normal plus the normals
//   of the polygons sharing the point that lie in the same smoothing group and
//   within the surface's smoothing angle (SMAN). Corners merge into one vertex
//   when they share a point, a UV and a normal within Doom 3's r_slopNormal
//   (0.02). Doom 3's position and UV "slop" merges (r_slopVertex,
//   r_slopTexCoord) are not applied; they only join near-coincident corners.
// - A corner with no TXUV entry gets the first TXUV value in the file, as
//   Doom 3 leaves its UV index at zero; with no UV map at all it gets (0, 0).
// - Doom 3 and q3map2's picomodel draw only three-corner polygons and warn
//   about the rest. The studio triangulates larger polygons (a fan when convex,
//   ear clipping when concave) so they can be inspected, and warns that the
//   game will drop them. picomodel (libs/picomodel/pm_lwo.c) uses the same
//   first layer, axis swap and V flip, takes the first word of the surface
//   name without its extension as the shader, and discards non-FACE polygons.
#include "core/model_formats_p.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio::model_formats {

namespace {

constexpr quint32 lwId(char a, char b, char c, char d)
{
	return (quint32(quint8(a)) << 24) | (quint32(quint8(b)) << 16) | (quint32(quint8(c)) << 8) | quint32(quint8(d));
}

constexpr quint32 kIdForm = lwId('F', 'O', 'R', 'M');
constexpr quint32 kIdLwo2 = lwId('L', 'W', 'O', '2');
constexpr quint32 kIdLwob = lwId('L', 'W', 'O', 'B');
constexpr quint32 kIdLwlo = lwId('L', 'W', 'L', 'O');
constexpr quint32 kIdLayr = lwId('L', 'A', 'Y', 'R');
constexpr quint32 kIdPnts = lwId('P', 'N', 'T', 'S');
constexpr quint32 kIdPols = lwId('P', 'O', 'L', 'S');
constexpr quint32 kIdVmap = lwId('V', 'M', 'A', 'P');
constexpr quint32 kIdVmad = lwId('V', 'M', 'A', 'D');
constexpr quint32 kIdPtag = lwId('P', 'T', 'A', 'G');
constexpr quint32 kIdTags = lwId('T', 'A', 'G', 'S');
constexpr quint32 kIdSrfs = lwId('S', 'R', 'F', 'S');
constexpr quint32 kIdSurf = lwId('S', 'U', 'R', 'F');
constexpr quint32 kIdSmgp = lwId('S', 'M', 'G', 'P');
constexpr quint32 kIdTxuv = lwId('T', 'X', 'U', 'V');
constexpr quint32 kIdSman = lwId('S', 'M', 'A', 'N');
constexpr quint32 kIdFlag = lwId('F', 'L', 'A', 'G');
constexpr quint32 kIdFace = lwId('F', 'A', 'C', 'E');
constexpr quint32 kIdPtch = lwId('P', 'T', 'C', 'H');
constexpr quint32 kIdSubd = lwId('S', 'U', 'B', 'D');

constexpr int kMaxLwTags = 65536;
constexpr qint64 kMaxLwCorners = 3LL * kMaxSurfaceTriangles;
// Ear clipping is quadratic per clipped ear; larger concave polygons, and
// files whose concave polygons add up to more work than this, are refused.
constexpr int kMaxConcaveCorners = 256;
constexpr qint64 kMaxEarClipWork = 64LL * 1024LL * 1024LL;
// Smoothing visits every polygon pair sharing a point.
constexpr qint64 kMaxSmoothingWork = 64LL * 1024LL * 1024LL;
// 1 - r_slopNormal (neo/renderer/Model.cpp).
constexpr float kNormalMergeDot = 0.98f;
// LWOB FLAG bit 4 (smoothing on) sets this angle in lwGetSurface5.
constexpr float kLwobSmoothAngle = 1.56207f;
constexpr double kPi = 3.14159265358979323846;

struct Vec3d {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

Vec3d toD(const ModelVec3& v)
{
	return {double(v.x), double(v.y), double(v.z)};
}

Vec3d subD(const Vec3d& a, const Vec3d& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3d crossD(const Vec3d& a, const Vec3d& b)
{
	return {(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}

double dotD(const Vec3d& a, const Vec3d& b)
{
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

ModelVec3 normalisedF(const Vec3d& v)
{
	const double length = std::sqrt(dotD(v, v));
	if (!(length > 0.0) || !std::isfinite(length)) {
		return {};
	}
	return {float(v.x / length), float(v.y / length), float(v.z / length)};
}

float dotF(const ModelVec3& a, const ModelVec3& b)
{
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

QString chunkName(quint32 id)
{
	QString name;
	for (int shift = 24; shift >= 0; shift -= 8) {
		const char ch = char((id >> shift) & 0xFF);
		name.append((ch >= 32 && ch < 127) ? QChar::fromLatin1(ch) : QLatin1Char('?'));
	}
	return name;
}

// The canonical material name Doom 3's decl manager makes of a surface name
// (idDeclManagerLocal::MakeNameCanonical): '\' becomes '/', and the text from
// the last '.' on is dropped. Case is kept.
QString canonicalMaterialName(const QString& name)
{
	QString canonical = name;
	canonical.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const qsizetype dot = canonical.lastIndexOf(QLatin1Char('.'));
	if (dot >= 0) {
		canonical.truncate(dot);
	}
	return canonical;
}

// A bounded reader over one chunk (or sub-chunk). Any read past `end` clears
// `ok` and returns zero, so callers check `ok` once after a record.
struct LwCursor {
	const QByteArray& bytes;
	qint64 position = 0;
	qint64 end = 0;
	bool ok = true;

	[[nodiscard]] qint64 remaining() const { return end - position; }
	[[nodiscard]] bool atEnd() const { return position >= end; }

	bool take(qint64 length)
	{
		if (!ok || length < 0 || length > end - position || !rangeOk(bytes, position, length)) {
			ok = false;
			return false;
		}
		return true;
	}
	quint8 u1()
	{
		if (!take(1)) { return 0; }
		const quint8 value = readU8(bytes, position);
		position += 1;
		return value;
	}
	quint16 u2()
	{
		if (!take(2)) { return 0; }
		const quint16 value = readU16BE(bytes, position);
		position += 2;
		return value;
	}
	qint16 i2() { return qint16(u2()); }
	quint32 u4()
	{
		if (!take(4)) { return 0; }
		const quint32 value = readU32BE(bytes, position);
		position += 4;
		return value;
	}
	float f4()
	{
		if (!take(4)) { return 0.0f; }
		const float value = readF32BE(bytes, position);
		position += 4;
		return value;
	}
	// LWO2 variable-length index: two bytes when the first is not 0xFF
	// (values below 0xFF00), else four bytes with the top byte masked off.
	int vx()
	{
		if (!take(2)) { return 0; }
		if (readU8(bytes, position) != 0xFF) {
			const int value = int(readU16BE(bytes, position));
			position += 2;
			return value;
		}
		if (!take(4)) { return 0; }
		const int value = int(readU32BE(bytes, position) & 0x00FFFFFFu);
		position += 4;
		return value;
	}
	// A NUL-terminated string padded to an even length.
	QString s0()
	{
		if (!ok) { return {}; }
		qint64 nul = position;
		while (nul < end && rangeOk(bytes, nul, 1) && bytes.at(qsizetype(nul)) != '\0') {
			++nul;
		}
		if (nul >= end || !rangeOk(bytes, nul, 1)) {
			ok = false;
			return {};
		}
		const QString text = QString::fromLatin1(bytes.constData() + position, qsizetype(nul - position));
		qint64 length = (nul - position) + 1;
		length += length & 1;
		position = std::min(position + length, end);
		return text;
	}
};

struct LwPolygon {
	int first = 0;
	int count = 0;
	quint32 type = 0;
	// Doom 3 leaves an untagged polygon on tag 0 (its surface pointer starts
	// as zero and lwResolvePolySurfaces treats it as an index).
	int tag = 0;
	int smoothGroup = 0;
	int slot = -1;
	ModelVec3 normal;
};

struct LwUvMap {
	bool perPolygon = false;
	QVector<int> points;
	QVector<int> polygons;
	QVector<ModelTexCoord> values;
};

struct LwSurfaceSlot {
	QString name;
	float smoothing = 0.0f;
	bool fromChunk = false;
};

struct LwVertexKey {
	int point = 0;
	quint32 u = 0;
	quint32 v = 0;
	bool operator==(const LwVertexKey& other) const { return point == other.point && u == other.u && v == other.v; }
};

size_t qHash(const LwVertexKey& key, size_t seed = 0)
{
	return qHashMulti(seed, key.point, key.u, key.v);
}

quint32 floatBits(float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	return raw;
}

struct LwObject {
	bool lwob = false;
	QVector<ModelVec3> points;
	QVector<int> corners;
	QVector<LwPolygon> polygons;
	QVector<LwUvMap> uvMaps;
	QStringList tags;
	QVector<LwSurfaceSlot> surfaceChunks;
	int pointOffset = 0;
	int polygonOffset = 0;
	int tagOffset = 0;
	int layers = 0;
	bool ignoringLayers = false;
	int ignoredChunks = 0;
	int narrowUvMaps = 0;
	int otherPolygonTypes = 0;
	QString firstLayerName;
};

// Splits one polygon into triangles of the polygon's own winding, as corner
// indices into `points`. A convex polygon becomes a fan; a concave one is ear
// clipped in the plane of its Newell normal. False when the polygon has no
// area, crosses itself, or is too large to clip.
bool triangulatePolygon(const QVector<Vec3d>& points, QVector<std::array<int, 3>>* triangles, qint64* earBudget, ModelWorkProgress& work)
{
	const int count = int(points.size());
	triangles->clear();
	if (count < 3) {
		return false;
	}
	Vec3d normal;
	double extent = 0.0;
	for (int index = 0; index < count; ++index) {
		const Vec3d& a = points.at(index);
		const Vec3d& b = points.at((index + 1) % count);
		normal.x += (a.y - b.y) * (a.z + b.z);
		normal.y += (a.z - b.z) * (a.x + b.x);
		normal.z += (a.x - b.x) * (a.y + b.y);
		const Vec3d offset = subD(a, points.at(0));
		extent = std::max({extent, std::fabs(offset.x), std::fabs(offset.y), std::fabs(offset.z)});
	}
	const double normalLength = std::sqrt(dotD(normal, normal));
	if (!std::isfinite(normalLength) || !std::isfinite(extent) || extent <= 0.0 || normalLength <= 1.0e-9 * extent * extent) {
		return false;
	}
	// Project onto the plane of the dominant normal axis. With these cyclic
	// axis pairs the projected signed area has the sign of that component.
	int axis = 2;
	if (std::fabs(normal.x) >= std::fabs(normal.y) && std::fabs(normal.x) >= std::fabs(normal.z)) {
		axis = 0;
	} else if (std::fabs(normal.y) >= std::fabs(normal.z)) {
		axis = 1;
	}
	const double axisComponent = axis == 0 ? normal.x : (axis == 1 ? normal.y : normal.z);
	const double orientation = axisComponent > 0.0 ? 1.0 : -1.0;
	QVector<double> u(count);
	QVector<double> v(count);
	for (int index = 0; index < count; ++index) {
		const Vec3d& p = points.at(index);
		if (axis == 0) {
			u[index] = p.y;
			v[index] = p.z;
		} else if (axis == 1) {
			u[index] = p.z;
			v[index] = p.x;
		} else {
			u[index] = p.x;
			v[index] = p.y;
		}
	}
	const double epsilon = 1.0e-10 * extent * extent;
	const auto turn = [&](int a, int b, int c) {
		return (((u[b] - u[a]) * (v[c] - v[a])) - ((v[b] - v[a]) * (u[c] - u[a]))) * orientation;
	};

	if (count == 3) {
		if (turn(0, 1, 2) <= epsilon) {
			return false;
		}
		triangles->append({0, 1, 2});
		return true;
	}

	// Convex when every corner turns the same way and the turns add up to one
	// full revolution (a pentagram turns the same way twice round).
	bool convex = true;
	double turning = 0.0;
	for (int index = 0; index < count && convex; ++index) {
		const int previous = (index + count - 1) % count;
		const int next = (index + 1) % count;
		const double cross = turn(previous, index, next);
		if (cross < -epsilon) {
			convex = false;
			break;
		}
		const double inU = u[index] - u[previous];
		const double inV = v[index] - v[previous];
		const double outU = u[next] - u[index];
		const double outV = v[next] - v[index];
		turning += std::atan2(((inU * outV) - (inV * outU)) * orientation, (inU * outU) + (inV * outV));
	}
	if (convex && std::fabs(turning) < (2.0 * kPi) + 1.0e-3) {
		for (int index = 1; index + 1 < count; ++index) {
			if (turn(0, index, index + 1) > epsilon) {
				triangles->append({0, index, index + 1});
			}
		}
		return !triangles->isEmpty();
	}

	if (count > kMaxConcaveCorners) {
		return false;
	}
	const qint64 cost = qint64(count) * qint64(count) * qint64(count);
	if (cost > *earBudget) {
		return false;
	}
	*earBudget -= cost;

	const auto inside = [&](int point, int a, int b, int c) {
		if ((u[point] == u[a] && v[point] == v[a]) || (u[point] == u[b] && v[point] == v[b]) || (u[point] == u[c] && v[point] == v[c])) {
			return false;
		}
		return turn(a, b, point) >= -epsilon && turn(b, c, point) >= -epsilon && turn(c, a, point) >= -epsilon;
	};
	QVector<int> ring(count);
	for (int index = 0; index < count; ++index) {
		ring[index] = index;
	}
	while (ring.size() > 3) {
		if (!work.step()) { return false; }
		const int size = int(ring.size());
		int clip = -1;
		for (int k = 0; k < size && clip < 0; ++k) {
			const int a = ring.at((k + size - 1) % size);
			const int b = ring.at(k);
			const int c = ring.at((k + 1) % size);
			if (turn(a, b, c) <= epsilon) {
				continue;
			}
			bool blocked = false;
			for (int other = 0; other < size && !blocked; ++other) {
				const int candidate = ring.at(other);
				if (candidate != a && candidate != b && candidate != c && inside(candidate, a, b, c)) {
					blocked = true;
				}
			}
			if (!blocked) {
				clip = k;
				triangles->append({a, b, c});
			}
		}
		if (clip < 0) {
			// A straight corner adds no area; dropping it may free an ear.
			for (int k = 0; k < size && clip < 0; ++k) {
				const int a = ring.at((k + size - 1) % size);
				const int b = ring.at(k);
				const int c = ring.at((k + 1) % size);
				if (std::fabs(turn(a, b, c)) <= epsilon) {
					clip = k;
				}
			}
		}
		if (clip < 0) {
			return false;
		}
		ring.removeAt(clip);
	}
	if (turn(ring.at(0), ring.at(1), ring.at(2)) > epsilon) {
		triangles->append({ring.at(0), ring.at(1), ring.at(2)});
	}
	return !triangles->isEmpty();
}

void fillStaticFrame(ModelMesh* mesh)
{
	ModelFrameInfo frame;
	frame.index = 0;
	frame.name = QStringLiteral("frame0");
	bool first = true;
	float radius = 0.0f;
	for (const ModelSurface& surface : mesh->surfaces) {
		for (const ModelVec3& p : surface.frames.constFirst().positions) {
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
			radius = std::max(radius, std::sqrt(dotF(p, p)));
		}
	}
	frame.radius = radius;
	mesh->frames.append(frame);
}

class LightWaveDecoder {
public:
	LightWaveDecoder(const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
		: m_bytes(bytes)
		, m_mesh(mesh)
		, m_work(control, ModelWorkPhase::Validating, &mesh->error)
	{
	}

	void run()
	{
		if (!m_work.check()) { return; }
		if (!readChunks()) { return; }
		if (!m_work.check()) { return; }
		build();
	}

private:
	bool fail(const QString& message)
	{
		if (m_mesh->error.isEmpty()) {
			m_mesh->error = message;
		}
		return false;
	}

	bool readChunks()
	{
		if (!rangeOk(m_bytes, 0, 12) || readU32BE(m_bytes, 0) != kIdForm) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave FORM header is truncated."));
		}
		const quint32 type = readU32BE(m_bytes, 8);
		if (type == kIdLwlo) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "LightWave LWLO layered objects are not decoded; Doom 3 and q3map2 read only LWO2 and LWOB."));
		}
		if (type != kIdLwo2 && type != kIdLwob) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The IFF form type %1 is not a LightWave object.").arg(chunkName(type)));
		}
		m_object.lwob = type == kIdLwob;
		m_mesh->version = m_object.lwob ? 1 : 2;
		const qint64 formEnd = 8 + qint64(readU32BE(m_bytes, 4));
		if (formEnd < 12) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave FORM declares a size too small for its type."));
		}
		if (formEnd > m_bytes.size()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave file is truncated: the FORM declares %1 bytes but the file holds %2.")
				.arg(formEnd).arg(m_bytes.size()));
		}
		if (formEnd < m_bytes.size()) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "%1 byte(s) after the LightWave FORM were ignored.").arg(m_bytes.size() - formEnd);
		}

		qint64 offset = 12;
		while (offset < formEnd) {
			if (!m_work.step()) { return false; }
			if (formEnd - offset < 8) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave chunk header at byte %1 is truncated.").arg(offset));
			}
			const quint32 id = readU32BE(m_bytes, offset);
			const qint64 size = qint64(readU32BE(m_bytes, offset + 4));
			const qint64 data = offset + 8;
			if (size > formEnd - data) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave %1 chunk at byte %2 runs past the end of the file.")
					.arg(chunkName(id)).arg(offset));
			}
			LwCursor cursor{m_bytes, data, data + size};
			if (!readChunk(id, cursor, offset)) {
				return false;
			}
			offset = data + size + (size & 1);
		}
		return true;
	}

	bool readChunk(quint32 id, LwCursor& cursor, qint64 offset)
	{
		const bool geometry = id == kIdPnts || id == kIdPols || id == kIdVmap || id == kIdVmad || id == kIdPtag;
		if (geometry && m_object.ignoringLayers) {
			++m_object.ignoredChunks;
			return true;
		}
		if (id == kIdLayr && !m_object.lwob) {
			++m_object.layers;
			if (m_object.layers > 1) {
				m_object.ignoringLayers = true;
			} else {
				cursor.u2();
				cursor.u2();
				cursor.f4();
				cursor.f4();
				cursor.f4();
				const QString name = cursor.s0();
				if (cursor.ok) {
					m_object.firstLayerName = name;
				}
			}
			return true;
		}
		if (id == kIdPnts) {
			return readPoints(cursor);
		}
		if (id == kIdPols) {
			return m_object.lwob ? readPolygonsLwob(cursor) : readPolygonsLwo2(cursor);
		}
		if ((id == kIdVmap || id == kIdVmad) && !m_object.lwob) {
			return readUvMap(cursor, id == kIdVmad);
		}
		if (id == kIdPtag && !m_object.lwob) {
			return readPolygonTags(cursor);
		}
		if ((id == kIdTags && !m_object.lwob) || (id == kIdSrfs && m_object.lwob)) {
			return readTags(cursor, offset);
		}
		if (id == kIdSurf) {
			return readSurface(cursor, offset);
		}
		return true;
	}

	bool readPoints(LwCursor& cursor)
	{
		if (cursor.remaining() % 12 != 0) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave point list is not a whole number of points."));
		}
		const qint64 count = cursor.remaining() / 12;
		if (count > kMaxVertexSlots - qint64(m_object.points.size())) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object declares more points than can be decoded."));
		}
		m_object.pointOffset = int(m_object.points.size());
		m_object.points.reserve(m_object.points.size() + qsizetype(count));
		for (qint64 index = 0; index < count; ++index) {
			if (!m_work.step()) { return false; }
			ModelVec3 point;
			point.x = cursor.f4();
			point.y = cursor.f4();
			point.z = cursor.f4();
			if (!cursor.ok || !isFinite(point)) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "LightWave point %1 is not a finite position.").arg(m_object.points.size()));
			}
			m_object.points.append(point);
		}
		return true;
	}

	bool appendPolygon(const LwPolygon& polygon)
	{
		if (m_object.polygons.size() >= kMaxSurfaceTriangles) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object declares more polygons than can be decoded."));
		}
		m_object.polygons.append(polygon);
		return true;
	}

	bool appendCorner(int point, int polygonIndex)
	{
		if (point < 0 || point >= m_object.points.size()) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "LightWave polygon %1 refers to point %2, beyond the %3 point(s) of its layer.")
				.arg(polygonIndex).arg(point).arg(m_object.points.size()));
		}
		if (m_object.corners.size() >= kMaxLwCorners) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object declares more polygon corners than can be decoded."));
		}
		m_object.corners.append(point);
		return true;
	}

	// POLS in LWO2: type[ID4], ( numvert+flags[U2], vert[VX] # numvert )*.
	bool readPolygonsLwo2(LwCursor& cursor)
	{
		const quint32 type = cursor.u4();
		if (!cursor.ok) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave polygon chunk is truncated."));
		}
		m_object.polygonOffset = int(m_object.polygons.size());
		while (!cursor.atEnd()) {
			if (!m_work.step()) { return false; }
			LwPolygon polygon;
			polygon.type = type;
			polygon.count = int(cursor.u2() & 0x03FF);
			polygon.first = int(m_object.corners.size());
			const int polygonIndex = int(m_object.polygons.size());
			for (int corner = 0; corner < polygon.count; ++corner) {
				const int point = cursor.vx();
				if (!cursor.ok) {
					break;
				}
				if (!appendCorner(point + m_object.pointOffset, polygonIndex)) {
					return false;
				}
			}
			if (!cursor.ok) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "LightWave polygon %1 is truncated.").arg(polygonIndex));
			}
			if (!appendPolygon(polygon)) {
				return false;
			}
		}
		return true;
	}

	// POLS in LWOB: ( numvert[U2], vert[U2] # numvert, surf[I2] )*, where a
	// negative surface marks detail polygons: a count[U2] follows and the
	// detail polygons continue inline (lwGetPolygons5).
	bool readPolygonsLwob(LwCursor& cursor)
	{
		m_object.polygonOffset = int(m_object.polygons.size());
		while (!cursor.atEnd()) {
			if (!m_work.step()) { return false; }
			LwPolygon polygon;
			polygon.type = kIdFace;
			polygon.count = int(cursor.u2());
			polygon.first = int(m_object.corners.size());
			const int polygonIndex = int(m_object.polygons.size());
			for (int corner = 0; corner < polygon.count && cursor.ok; ++corner) {
				const int point = int(cursor.u2());
				if (cursor.ok && !appendCorner(point + m_object.pointOffset, polygonIndex)) {
					return false;
				}
			}
			int surface = int(cursor.i2());
			if (surface < 0) {
				surface = -surface;
				cursor.u2();
			}
			if (!cursor.ok) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "LightWave polygon %1 is truncated.").arg(polygonIndex));
			}
			if (surface == 0) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "LWOB polygon %1 names surface 0; surfaces count from 1.").arg(polygonIndex));
			}
			polygon.tag = surface - 1;
			if (!appendPolygon(polygon)) {
				return false;
			}
		}
		return true;
	}

	// VMAP: type[ID4], dimension[U2], name[S0], ( vert[VX], value[F4] # dimension )*.
	// VMAD adds poly[VX] after vert: a per-polygon (discontinuous) value.
	bool readUvMap(LwCursor& cursor, bool perPolygon)
	{
		const quint32 type = cursor.u4();
		const int dimension = int(cursor.u2());
		cursor.s0();
		if (!cursor.ok) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave vertex map header is truncated."));
		}
		if (type != kIdTxuv) {
			return true;
		}
		if (dimension < 2) {
			++m_object.narrowUvMaps;
			return true;
		}
		LwUvMap map;
		map.perPolygon = perPolygon;
		while (!cursor.atEnd()) {
			if (!m_work.step()) { return false; }
			const int point = cursor.vx() + m_object.pointOffset;
			const int polygon = perPolygon ? cursor.vx() + m_object.polygonOffset : -1;
			ModelTexCoord value;
			value.u = cursor.f4();
			value.v = cursor.f4();
			for (int extra = 2; extra < dimension && cursor.ok; ++extra) {
				cursor.f4();
			}
			if (!cursor.ok) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "A LightWave UV map record is truncated."));
			}
			if (point < 0 || point >= m_object.points.size()) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "A LightWave UV map refers to point %1, beyond the %2 point(s) of its layer.")
					.arg(point).arg(m_object.points.size()));
			}
			if (perPolygon && (polygon < 0 || polygon >= m_object.polygons.size())) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "A LightWave per-polygon UV map refers to polygon %1, beyond the %2 polygon(s) of its layer.")
					.arg(polygon).arg(m_object.polygons.size()));
			}
			if (!std::isfinite(value.u) || !std::isfinite(value.v)) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "A LightWave UV map holds a value that is not finite."));
			}
			map.points.append(point);
			if (perPolygon) {
				map.polygons.append(polygon);
			}
			map.values.append(value);
		}
		m_object.uvMaps.append(map);
		return true;
	}

	// PTAG: type[ID4], ( poly[VX], tag[U2] )*. SURF tags index TAGS (relative
	// to the latest TAGS chunk, as Doom 3 adds tlist->offset); SMGP values are
	// smoothing groups.
	bool readPolygonTags(LwCursor& cursor)
	{
		const quint32 type = cursor.u4();
		if (!cursor.ok) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave polygon tag chunk is truncated."));
		}
		if (type != kIdSurf && type != kIdSmgp) {
			return true;
		}
		while (!cursor.atEnd()) {
			if (!m_work.step()) { return false; }
			const int polygon = cursor.vx() + m_object.polygonOffset;
			const int tag = int(cursor.u2());
			if (!cursor.ok) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "A LightWave polygon tag record is truncated."));
			}
			if (polygon < 0 || polygon >= m_object.polygons.size()) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "A LightWave polygon tag refers to polygon %1, beyond the %2 polygon(s) of its layer.")
					.arg(polygon).arg(m_object.polygons.size()));
			}
			if (type == kIdSurf) {
				m_object.polygons[polygon].tag = tag + m_object.tagOffset;
			} else {
				m_object.polygons[polygon].smoothGroup = tag;
			}
		}
		return true;
	}

	bool readTags(LwCursor& cursor, qint64 offset)
	{
		m_object.tagOffset = int(m_object.tags.size());
		while (!cursor.atEnd()) {
			if (!m_work.step()) { return false; }
			const QString tag = cursor.s0();
			if (!cursor.ok) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave tag list at byte %1 holds an unterminated name.").arg(offset));
			}
			if (m_object.tags.size() >= kMaxLwTags) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object declares more tags than can be decoded."));
			}
			m_object.tags.append(tag);
		}
		return true;
	}

	// SURF: name[S0], (LWO2 only) source[S0], then sub-chunks of
	// id[ID4] size[U2] padded to even sizes. Only the smoothing angle matters
	// to geometry (SMAN, or the LWOB FLAG smoothing bit).
	bool readSurface(LwCursor& cursor, qint64 offset)
	{
		LwSurfaceSlot slot;
		slot.fromChunk = true;
		slot.name = cursor.s0();
		if (!m_object.lwob) {
			cursor.s0();
		}
		if (!cursor.ok) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave surface at byte %1 has an unterminated name.").arg(offset));
		}
		while (cursor.remaining() >= 6) {
			if (!m_work.step()) { return false; }
			const quint32 id = cursor.u4();
			const qint64 size = qint64(cursor.u2());
			if (size > cursor.remaining()) {
				return fail(QCoreApplication::translate("VibeStudioModelMesh", "LightWave surface %1 has a %2 sub-chunk that runs past its chunk.")
					.arg(slot.name, chunkName(id)));
			}
			const qint64 subEnd = cursor.position + size;
			if (id == kIdSman && size >= 4) {
				const float angle = readF32BE(m_bytes, cursor.position);
				slot.smoothing = std::isfinite(angle) ? angle : 0.0f;
			} else if (id == kIdFlag && m_object.lwob && size >= 2) {
				if ((readU16BE(m_bytes, cursor.position) & 4) != 0) {
					slot.smoothing = kLwobSmoothAngle;
				}
			}
			cursor.position = std::min(subEnd + (size & 1), cursor.end);
		}
		for (const LwSurfaceSlot& existing : m_object.surfaceChunks) {
			if (existing.name == slot.name) {
				return true;
			}
		}
		if (m_object.surfaceChunks.size() >= kMaxLwTags) {
			return fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object declares more surfaces than can be decoded."));
		}
		m_object.surfaceChunks.append(slot);
		return true;
	}

	void build()
	{
		LwObject& object = m_object;
		if (object.points.isEmpty() || object.polygons.isEmpty()) {
			fail(QCoreApplication::translate("VibeStudioModelMesh", "The first LightWave layer holds no polygons."));
			return;
		}
		if (object.lwob) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "LightWave 5 object (LWOB)");
		} else {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "LightWave object (LWO2)");
		}
		if (!object.firstLayerName.isEmpty()) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Layer: %1").arg(object.firstLayerName);
		}
		m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Points: %1").arg(object.points.size());
		m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "Polygons: %1").arg(object.polygons.size());
		if (object.layers > 1) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "Only the first of %1 LightWave layers is decoded; Doom 3 and q3map2 ignore the others.")
				.arg(object.layers);
		}
		if (object.narrowUvMaps > 0) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 LightWave UV map(s) have fewer than two values per entry and were ignored.")
				.arg(object.narrowUvMaps);
		}

		// Polygon normals in LightWave space: the cross product of the first
		// and last edges (lwGetPolyNormals).
		for (LwPolygon& polygon : object.polygons) {
			if (!m_work.step()) { return; }
			if (polygon.count < 3) {
				continue;
			}
			const Vec3d p0 = toD(object.points.at(object.corners.at(polygon.first)));
			const Vec3d p1 = toD(object.points.at(object.corners.at(polygon.first + 1)));
			const Vec3d pn = toD(object.points.at(object.corners.at(polygon.first + polygon.count - 1)));
			polygon.normal = normalisedF(crossD(subD(p1, p0), subD(pn, p0)));
			if (dotF(polygon.normal, polygon.normal) <= 0.0f && polygon.count > 3) {
				// Straight first and last edges: use the Newell normal instead, so
				// the polygon can still be triangulated.
				Vec3d newell;
				for (int corner = 0; corner < polygon.count; ++corner) {
					const ModelVec3& a = object.points.at(object.corners.at(polygon.first + corner));
					const ModelVec3& b = object.points.at(object.corners.at(polygon.first + ((corner + 1) % polygon.count)));
					newell.x += (double(a.y) - b.y) * (double(a.z) + b.z);
					newell.y += (double(a.z) - b.z) * (double(a.x) + b.x);
					newell.z += (double(a.x) - b.x) * (double(a.y) + b.y);
				}
				polygon.normal = normalisedF(newell);
			}
		}

		// Surfaces in Doom 3's order: SURF chunks as they appear, then a default
		// surface for each tag no SURF chunk names, in the order polygons first
		// use it (lwResolvePolySurfaces).
		QVector<LwSurfaceSlot> surfaceSlots = object.surfaceChunks;
		QHash<QString, int> slotByName;
		for (int index = 0; index < surfaceSlots.size(); ++index) {
			slotByName.insert(surfaceSlots.at(index).name, index);
		}
		bool untaggedDefault = false;
		for (int index = 0; index < object.polygons.size(); ++index) {
			if (!m_work.step()) { return; }
			LwPolygon& polygon = object.polygons[index];
			QString name;
			if (object.tags.isEmpty()) {
				name = QStringLiteral("_default");
				untaggedDefault = true;
			} else if (polygon.tag < 0 || polygon.tag >= object.tags.size()) {
				fail(QCoreApplication::translate("VibeStudioModelMesh", "LightWave polygon %1 refers to surface tag %2, beyond the %3 tag(s).")
					.arg(index).arg(polygon.tag).arg(object.tags.size()));
				return;
			} else {
				name = object.tags.at(polygon.tag);
			}
			auto found = slotByName.constFind(name);
			if (found == slotByName.constEnd()) {
				LwSurfaceSlot slot;
				slot.name = name;
				surfaceSlots.append(slot);
				found = slotByName.insert(name, int(surfaceSlots.size()) - 1);
			}
			polygon.slot = found.value();
		}
		if (untaggedDefault) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object has no surface tags; its polygons use the default material.");
		}

		// Point -> polygon adjacency for smoothing (lwGetPointPolygons).
		const int pointCount = int(object.points.size());
		QVector<int> adjacencyStart(pointCount + 1, 0);
		for (int corner : object.corners) {
			++adjacencyStart[corner + 1];
		}
		qint64 smoothingWork = 0;
		for (int point = 0; point < pointCount; ++point) {
			const qint64 degree = adjacencyStart.at(point + 1);
			smoothingWork += degree * degree;
			adjacencyStart[point + 1] += adjacencyStart.at(point);
		}
		QVector<int> adjacency(object.corners.size());
		{
			QVector<int> fill = adjacencyStart;
			for (int index = 0; index < object.polygons.size(); ++index) {
				if (!m_work.step()) { return; }
				const LwPolygon& polygon = object.polygons.at(index);
				for (int corner = 0; corner < polygon.count; ++corner) {
					const int point = object.corners.at(polygon.first + corner);
					adjacency[fill[point]++] = index;
				}
			}
		}
		const bool smoothingAllowed = smoothingWork <= kMaxSmoothingWork;
		if (!smoothingAllowed) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The LightWave points are shared too widely to smooth; polygon normals are used instead.");
		}

		// Corner UVs as Doom 3 resolves them: the last point map value, then the
		// last per-polygon value for that corner.
		bool haveUvMap = false;
		ModelTexCoord fallbackUv;
		for (const LwUvMap& map : object.uvMaps) {
			if (!map.values.isEmpty()) {
				fallbackUv.u = map.values.constFirst().u;
				fallbackUv.v = 1.0f - map.values.constFirst().v;
				haveUvMap = true;
				break;
			}
		}
		QVector<ModelTexCoord> pointUv(pointCount);
		QVector<bool> pointHasUv(pointCount, false);
		QVector<ModelTexCoord> cornerUv(object.corners.size());
		QVector<bool> cornerHasUv(object.corners.size(), false);
		int unmatchedPolygonUvs = 0;
		for (const LwUvMap& map : object.uvMaps) {
			for (int record = 0; record < map.values.size(); ++record) {
				if (!m_work.step()) { return; }
				const int point = map.points.at(record);
				if (!map.perPolygon) {
					pointUv[point] = map.values.at(record);
					pointHasUv[point] = true;
					continue;
				}
				const LwPolygon& polygon = object.polygons.at(map.polygons.at(record));
				bool matched = false;
				for (int corner = 0; corner < polygon.count && !matched; ++corner) {
					if (object.corners.at(polygon.first + corner) == point) {
						cornerUv[polygon.first + corner] = map.values.at(record);
						cornerHasUv[polygon.first + corner] = true;
						matched = true;
					}
				}
				if (!matched) {
					++unmatchedPolygonUvs;
				}
			}
		}
		if (unmatchedPolygonUvs > 0) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 per-polygon UV record(s) name a point their polygon does not use and were ignored.")
				.arg(unmatchedPolygonUvs);
		}

		struct SurfaceBuild {
			ModelSurface surface;
			QHash<LwVertexKey, int> firstVertex;
			QVector<int> nextVertex;
		};
		QVector<SurfaceBuild> builds;
		QVector<int> buildForSlot(surfaceSlots.size(), -1);
		int triangulated = 0;
		int refused = 0;
		int shortPolygons = 0;
		int otherTypes = 0;
		int unmappedCorners = 0;
		qint64 earBudget = kMaxEarClipWork;
		qint64 totalVertices = 0;
		QVector<Vec3d> polygonPoints;
		QVector<std::array<int, 3>> triangles;
		QVector<ModelVec3> cornerNormals;
		QVector<ModelTexCoord> polygonUvs;

		for (int polygonIndex = 0; polygonIndex < object.polygons.size(); ++polygonIndex) {
			if (!m_work.step()) { return; }
			const LwPolygon& polygon = object.polygons.at(polygonIndex);
			if (polygon.type != kIdFace && polygon.type != kIdPtch && polygon.type != kIdSubd) {
				++otherTypes;
				continue;
			}
			if (polygon.count < 3) {
				++shortPolygons;
				continue;
			}
			const LwSurfaceSlot& slot = surfaceSlots.at(polygon.slot);
			polygonPoints.resize(polygon.count);
			for (int corner = 0; corner < polygon.count; ++corner) {
				polygonPoints[corner] = toD(object.points.at(object.corners.at(polygon.first + corner)));
			}
			if (dotF(polygon.normal, polygon.normal) <= 0.0f || !triangulatePolygon(polygonPoints, &triangles, &earBudget, m_work)) {
				if (!m_mesh->error.isEmpty()) { return; }
				++refused;
				continue;
			}
			if (polygon.count > 3) {
				++triangulated;
			}

			// Corner normals (lwGetVertNormals), still in LightWave space.
			cornerNormals.resize(polygon.count);
			for (int corner = 0; corner < polygon.count; ++corner) {
				Vec3d sum = toD(polygon.normal);
				if (slot.smoothing > 0.0f && smoothingAllowed) {
					const int point = object.corners.at(polygon.first + corner);
					for (int entry = adjacencyStart.at(point); entry < adjacencyStart.at(point + 1); ++entry) {
						if (!m_work.step()) { return; }
						const int other = adjacency.at(entry);
						if (other == polygonIndex) {
							continue;
						}
						const LwPolygon& neighbour = object.polygons.at(other);
						if (neighbour.smoothGroup != polygon.smoothGroup) {
							continue;
						}
						const float cosine = std::clamp(dotF(polygon.normal, neighbour.normal), -1.0f, 1.0f);
						if (std::acos(cosine) > slot.smoothing) {
							continue;
						}
						sum.x += neighbour.normal.x;
						sum.y += neighbour.normal.y;
						sum.z += neighbour.normal.z;
					}
				}
				ModelVec3 normal = normalisedF(sum);
				if (dotF(normal, normal) <= 0.0f) {
					normal = polygon.normal;
				}
				cornerNormals[corner] = normal;
			}

			if (buildForSlot.at(polygon.slot) < 0) {
				if (builds.size() >= kMaxSurfaces) {
					fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object uses more surfaces than can be decoded."));
					return;
				}
				buildForSlot[polygon.slot] = int(builds.size());
				builds.append(SurfaceBuild{});
			}
			// Corner UVs, V inverted (ConvertLWOToModelSurfaces: "invert the t").
			polygonUvs.resize(polygon.count);
			for (int corner = 0; corner < polygon.count; ++corner) {
				const int cornerIndex = polygon.first + corner;
				const int point = object.corners.at(cornerIndex);
				ModelTexCoord& uv = polygonUvs[corner];
				if (cornerHasUv.at(cornerIndex)) {
					uv.u = cornerUv.at(cornerIndex).u;
					uv.v = 1.0f - cornerUv.at(cornerIndex).v;
				} else if (pointHasUv.at(point)) {
					uv.u = pointUv.at(point).u;
					uv.v = 1.0f - pointUv.at(point).v;
				} else {
					uv = fallbackUv;
					if (haveUvMap) {
						++unmappedCorners;
					}
				}
			}

			SurfaceBuild& build = builds[buildForSlot.at(polygon.slot)];
			ModelSurface& surface = build.surface;
			const auto vertexFor = [&](int corner) -> int {
				const int point = object.corners.at(polygon.first + corner);
				const ModelTexCoord& uv = polygonUvs.at(corner);
				const ModelVec3& lwNormal = cornerNormals.at(corner);
				const ModelVec3 normal{lwNormal.x, lwNormal.z, lwNormal.y};
				const LwVertexKey key{point, floatBits(uv.u), floatBits(uv.v)};
				const auto found = build.firstVertex.constFind(key);
				int last = -1;
				if (found != build.firstVertex.constEnd()) {
					for (int vertex = found.value(); vertex >= 0; vertex = build.nextVertex.at(vertex)) {
						if (dotF(surface.frames.constFirst().normals.at(vertex), normal) > kNormalMergeDot) {
							return vertex;
						}
						last = vertex;
					}
				}
				if (surface.vertexCount >= kMaxSurfaceVertices || totalVertices >= kMaxVertexSlots) {
					return -1;
				}
				const int vertex = surface.vertexCount++;
				++totalVertices;
				const ModelVec3& p = object.points.at(point);
				surface.frames.first().positions.append(ModelVec3{p.x, p.z, p.y});
				surface.frames.first().normals.append(normal);
				surface.texCoords.append(uv);
				build.nextVertex.append(-1);
				if (last >= 0) {
					build.nextVertex[last] = vertex;
				} else {
					build.firstVertex.insert(key, vertex);
				}
				return vertex;
			};
			if (surface.frames.isEmpty()) {
				surface.frames.append(ModelFrameGeometry{});
			}
			for (const std::array<int, 3>& triangle : triangles) {
				if (!m_work.step()) { return; }
				// LightWave order (a, b, c) becomes (a, c, b): see the header.
				const int a = vertexFor(triangle[0]);
				const int b = vertexFor(triangle[2]);
				const int c = vertexFor(triangle[1]);
				if (a < 0 || b < 0 || c < 0 || surface.triangles.size() >= kMaxSurfaceTriangles) {
					fail(QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object has more vertices or triangles than can be decoded."));
					return;
				}
				surface.triangles.append(ModelTriangle{a, b, c});
			}
		}

		int defaultSurfaces = 0;
		for (int index = 0; index < surfaceSlots.size(); ++index) {
			if (!m_work.step()) { return; }
			if (buildForSlot.at(index) < 0) {
				continue;
			}
			ModelSurface& surface = builds[buildForSlot.at(index)].surface;
			if (surface.triangles.isEmpty()) {
				continue;
			}
			const LwSurfaceSlot& slot = surfaceSlots.at(index);
			surface.index = int(m_mesh->surfaces.size());
			surface.name = slot.name.isEmpty() ? QStringLiteral("surface%1").arg(surface.index) : slot.name;
			const QString material = canonicalMaterialName(slot.name);
			if (!material.isEmpty()) {
				surface.skinPaths.append(material);
				if (!m_mesh->skinPaths.contains(material)) {
					m_mesh->skinPaths.append(material);
				}
			}
			if (!slot.fromChunk && !untaggedDefault) {
				++defaultSurfaces;
			}
			m_mesh->surfaces.append(surface);
		}
		if (defaultSurfaces > 0) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "%1 surface(s) have no SURF chunk and use LightWave's defaults.").arg(defaultSurfaces);
		}

		if (triangulated > 0) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh",
				"%1 polygon(s) with more than three corners were triangulated here; Doom 3 and q3map2 drop such polygons, so triangulate them in the modeller.")
				.arg(triangulated);
		}
		if (refused > 0) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 degenerate or self-crossing polygon(s) were skipped.").arg(refused);
		}
		if (shortPolygons > 0) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "%1 polygon(s) with fewer than three corners were skipped, as the game skips them.")
				.arg(shortPolygons);
		}
		if (otherTypes > 0) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "%1 curve, bone or metaball polygon(s) were skipped.").arg(otherTypes);
		}
		if (!haveUvMap) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "The LightWave object has no UV map; Doom 3 draws it with zero texture coordinates.");
		} else if (unmappedCorners > 0) {
			m_mesh->warnings << QCoreApplication::translate("VibeStudioModelMesh", "%1 polygon corner(s) have no UV; Doom 3 gives them the first UV in the file.")
				.arg(unmappedCorners);
		}
		if (object.ignoredChunks > 0) {
			m_mesh->detailLines << QCoreApplication::translate("VibeStudioModelMesh", "%1 geometry chunk(s) of later layers were ignored.").arg(object.ignoredChunks);
		}

		if (m_mesh->surfaces.isEmpty()) {
			fail(QCoreApplication::translate("VibeStudioModelMesh", "No LightWave polygon could be turned into triangles."));
			return;
		}
		fillStaticFrame(m_mesh);
		m_mesh->skinCount = int(m_mesh->skinPaths.size());
		m_mesh->geometryAvailable = true;
	}

	const QByteArray& m_bytes;
	ModelMesh* m_mesh;
	ModelWorkProgress m_work;
	LwObject m_object;
};

} // namespace

void decodeLightWave(const QString& path, const QByteArray& bytes, ModelMesh* mesh, const ModelWorkControl& control)
{
	Q_UNUSED(path);
	LightWaveDecoder decoder(bytes, mesh, control);
	decoder.run();
}

} // namespace vibestudio::model_formats
