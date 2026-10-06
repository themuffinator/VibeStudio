#include "core/model_obj.h"
#include "core/model_archive.h"

#include "core/model_document.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QStringDecoder>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <tuple>

// Original implementation of the polygon syntax in Wavefront's Advanced
// Visualizer 3.0 Appendix B1, reviewed 2026-10-05:
// https://www.martinreddy.net/gfx/3d/OBJ.spec
// Format facts only; no upstream implementation or specification text copied.
namespace vibestudio
{
namespace
{
constexpr int maxLineBytes = 65536, maxPolygonCorners = 1024;
constexpr qint64 maxGeometryChecks = 16000000;
struct Vec
{
	double x = 0, y = 0, z = 0;
};
Vec vector(ModelVec3 p) { return {p.x, p.y, p.z}; }
Vec subtract(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec add(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
double length(Vec a) { return std::sqrt(dot(a, a)); }
ModelVec3 normalized(Vec a)
{
	const double size = length(a);
	return {float(a.x / size), float(a.y / size), float(a.z / size)};
}
struct Point
{
	double x, y;
};
double orient(Point a, Point b, Point c)
{
	const double left = (b.x - a.x) * (c.y - a.y), right = (b.y - a.y) * (c.x - a.x);
	const double determinant = left - right;
	// Compare against the roundoff in this determinant, not a polygon-wide
	// area tolerance: a long, thin valid face must not collapse on import.
	const double uncertainty = 8 * std::numeric_limits<double>::epsilon() * (std::abs(left) + std::abs(right));
	return std::abs(determinant) <= uncertainty ? 0 : determinant;
}
struct Corner
{
	int position = -1, uv = -1, normal = -1;
};
struct Face
{
	QVector<Corner> corners;
	QVector<ModelTriangle> triangles;
	Vec normal;
	int surface = 0, group = 0, line = 0;
};
using VertexKey = std::tuple<int, int, int, int>;
using SurfaceKey = std::tuple<QString, QString, QString>;

class Reader
{
  public:
	Reader(const QString &path, const ModelWorkControl &control) : m_control(control), work(control, ModelWorkPhase::Reading, &mesh.error)
	{
		mesh.sourcePath = path;
		mesh.format = ModelMeshFormat::WavefrontObj;
		mesh.formatId = QStringLiteral("obj");
		mesh.formatName = QStringLiteral("Wavefront OBJ");
	}
	ModelMesh run(const QByteArray &bytes)
	{
		if (!work.check())
		{
			return mesh;
		}
		if (bytes.isEmpty() || bytes.size() > modelDocumentMaxSourceBytes)
		{
			fail(QCoreApplication::translate("VibeStudioModelObj", "OBJ source must contain between 1 byte and 64 MiB."));
			return mesh;
		}
		qsizetype cursor = bytes.startsWith("\xEF\xBB\xBF") ? 3 : 0;
		int physicalLine = 1;
		QByteArray logical;
		while (cursor < bytes.size())
		{
			if (!work.step())
			{
				return failed();
			}
			if (logical.isEmpty())
			{
				line = physicalLine;
			}
			const qsizetype end = bytes.indexOf('\n', cursor);
			const qsizetype count = (end < 0 ? bytes.size() : end) - cursor;
			if (count + logical.size() > maxLineBytes)
			{
				fail(QCoreApplication::translate("VibeStudioModelObj", "An OBJ logical line exceeds 64 KiB."));
				return failed();
			}
			QByteArrayView part(bytes.constData() + cursor, count);
			for (char c : part)
			{
				if (!work.step())
				{
					return failed();
				}
				if ((uchar(c) < 32 && c != '\r' && c != '\t') || uchar(c) == 127)
				{
					fail(QCoreApplication::translate("VibeStudioModelObj", "OBJ contains a binary or control character."));
					return failed();
				}
			}
			const auto comment = part.indexOf('#');
			if (comment >= 0)
			{
				part = part.first(comment);
			}
			part = part.trimmed();
			const bool continued = part.endsWith('\\');
			if (continued)
			{
				part = part.first(part.size() - 1);
			}
			logical.append(part.data(), part.size());
			cursor += count + (end >= 0 ? 1 : 0);
			++physicalLine;
			if (continued)
			{
				logical.append(' ');
				if (cursor == bytes.size())
				{
					fail(QCoreApplication::translate("VibeStudioModelObj", "OBJ ends during a continued line."));
					return failed();
				}
				continue;
			}
			if (!readLine(logical))
			{
				return failed();
			}
			logical.clear();
		}
		if (faces.isEmpty())
		{
			fail(QCoreApplication::translate("VibeStudioModelObj", "OBJ contains no polygon faces."));
			return failed();
		}
		if (usedPositions.size() != positions.size())
		{
			fail(QCoreApplication::translate("VibeStudioModelObj",
											 "OBJ contains loose vertices. Remove vertices unused by faces before importing."));
			return failed();
		}
		if (!build() || !work.check())
		{
			return failed();
		}
		mesh.geometryAvailable = true;
		ModelFrameInfo frame;
		frame.name = QStringLiteral("frame0");
		mesh.frames.append(frame);
		updateEditableModelMetadata(&mesh);
		const auto errors = validateEditableModel(mesh, m_control);
		if (!errors.isEmpty())
		{
			fail(errors.join(QLatin1Char('\n')));
			return failed();
		}
		if (!work.check())
		{
			return failed();
		}
		return mesh;
	}

  private:
	const ModelWorkControl &m_control;
	ModelMesh mesh;
	ModelWorkProgress work;
	QVector<ModelVec3> positions, normals;
	QVector<ModelTexCoord> uvs;
	QVector<Face> faces;
	QSet<int> usedPositions;
	std::map<SurfaceKey, int> surfaceIndices;
	QSet<QString> surfaceNames;
	std::map<std::pair<int, int>, Vec> smoothNormals;
	QString object, groups, material;
	int smoothing = 0, line = 1, triangleCount = 0;
	qint64 checks = 0;
	bool fail(const QString &reason)
	{
		mesh.error = QCoreApplication::translate("VibeStudioModelObj", "OBJ line %1: %2").arg(line).arg(reason);
		return false;
	}
	ModelMesh failed()
	{
		mesh.surfaces.clear();
		mesh.geometryAvailable = false;
		return mesh;
	}
	bool geometryCheck()
	{
		if (!work.step())
		{
			return false;
		}
		return ++checks <= maxGeometryChecks ||
			   fail(QCoreApplication::translate(
				   "VibeStudioModelObj", "Polygon validation exceeds its work limit. Triangulate complex polygons before importing."));
	}
	bool number(QByteArrayView token, double *out)
	{
		bool valid = false;
		*out = token.toDouble(&valid);
		return (valid && std::isfinite(*out) && std::abs(*out) <= 1000000) ||
			   fail(QCoreApplication::translate("VibeStudioModelObj", "Coordinates must be finite numbers within ±1,000,000."));
	}
	bool name(const QVector<QByteArrayView> &tokens, QString *out)
	{
		QByteArray bytes;
		for (int i = 1; i < tokens.size(); ++i)
		{
			if (i > 1)
			{
				bytes += ' ';
			}
			bytes.append(tokens[i].data(), tokens[i].size());
		}
		QStringDecoder decoder(QStringDecoder::Utf8);
		*out = decoder(bytes);
		if (decoder.hasError() || out->size() > 128 ||
			std::any_of(out->begin(), out->end(), [](QChar c) { return c.category() == QChar::Other_Control; }))
		{
			return fail(QCoreApplication::translate("VibeStudioModelObj",
													"Names must be valid UTF-8 with at most 128 characters and no controls."));
		}
		return true;
	}
	bool index(QByteArrayView token, qsizetype count, int *out)
	{
		bool valid = false;
		const qint64 value = token.toLongLong(&valid);
		if (!valid || !value || value > count || value < -count)
		{
			return fail(QCoreApplication::translate(
				"VibeStudioModelObj", "A face index is zero, malformed, forward-referencing, or outside the declared vertex list."));
		}
		*out = int(value > 0 ? value - 1 : count + value);
		return true;
	}
	bool corner(QByteArrayView token, Corner *out)
	{
		const auto first = token.indexOf('/');
		if (first < 0)
		{
			return index(token, positions.size(), &out->position);
		}
		if (!index(token.first(first), positions.size(), &out->position))
		{
			return false;
		}
		const auto second = token.indexOf('/', first + 1);
		if (second < 0)
		{
			return index(token.sliced(first + 1), uvs.size(), &out->uv);
		}
		if (second > first + 1 && !index(token.sliced(first + 1, second - first - 1), uvs.size(), &out->uv))
		{
			return false;
		}
		return index(token.sliced(second + 1), normals.size(), &out->normal);
	}
	bool readLine(QByteArrayView bytes)
	{
		QVector<QByteArrayView> tokens;
		for (qsizetype i = 0; i < bytes.size();)
		{
			if (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\r')
			{
				++i;
				continue;
			}
			const auto start = i;
			while (i < bytes.size() && bytes[i] != ' ' && bytes[i] != '\t' && bytes[i] != '\r')
			{
				++i;
			}
			tokens.append(bytes.sliced(start, i - start));
			if (tokens.size() > maxPolygonCorners + 1)
			{
				return fail(QCoreApplication::translate("VibeStudioModelObj", "An OBJ record exceeds 1,024 values or polygon corners."));
			}
		}
		if (tokens.isEmpty())
		{
			return true;
		}
		const auto command = tokens[0];
		if (command == "v" || command == "vt" || command == "vn")
		{
			const int count = tokens.size() - 1;
			if ((command == "v" && (count < 3 || count > 4)) || (command == "vn" && count != 3) ||
				(command == "vt" && (count < 1 || count > 3)))
			{
				return fail(QCoreApplication::translate("VibeStudioModelObj",
														"A vertex record has unsupported components; vertex colours are not supported."));
			}
			std::array<double, 4> values{0, 0, 0, 1};
			for (int i = 0; i < count; ++i)
			{
				if (!number(tokens[i + 1], &values[i]))
				{
					return false;
				}
			}
			if ((command == "v" && values[3] != 1) || (command == "vt" && values[2] != 0))
			{
				return fail(QCoreApplication::translate(
					"VibeStudioModelObj",
					"Weighted positions and 3D texture coordinates cannot be represented. Export polygon positions and 2D UVs."));
			}
			if (command == "v")
			{
				positions.append({float(values[0]), float(values[1]), float(values[2])});
			}
			else if (command == "vt")
			{
				uvs.append({float(values[0]), float(1 - values[1])});
			}
			else
			{
				const Vec n{values[0], values[1], values[2]};
				if (length(n) < 1e-12)
				{
					return fail(QCoreApplication::translate("VibeStudioModelObj", "Vertex normals must be nonzero."));
				}
				normals.append(normalized(n));
			}
			return (positions.size() <= modelDocumentMaxVertices && uvs.size() <= modelDocumentMaxVertices &&
					normals.size() <= modelDocumentMaxVertices) ||
				   fail(QCoreApplication::translate("VibeStudioModelObj", "An OBJ position, UV, or normal list exceeds 65,536 entries."));
		}
		if (command == "o")
		{
			return name(tokens, &object);
		}
		if (command == "g")
		{
			return name(tokens, &groups);
		}
		if (command == "usemtl")
		{
			if (!name(tokens, &material))
			{
				return false;
			}
			return material.isEmpty() || isSafePackageVirtualPath(material) ||
				   fail(QCoreApplication::translate("VibeStudioModelObj",
													"A direct material assignment must be a safe package-relative path."));
		}
		if (command == "s")
		{
			if (tokens.size() != 2)
			{
				return fail(
					QCoreApplication::translate("VibeStudioModelObj", "Smoothing requires off, on, zero, or a positive group number."));
			}
			if (tokens[1] == "off" || tokens[1] == "0")
			{
				smoothing = 0;
				return true;
			}
			if (tokens[1] == "on")
			{
				smoothing = 1;
				return true;
			}
			bool valid = false;
			smoothing = tokens[1].toInt(&valid);
			return (valid && smoothing > 0) ||
				   fail(QCoreApplication::translate("VibeStudioModelObj", "Smoothing requires off, on, zero, or a positive group number."));
		}
		if (command == "mtllib")
		{
			return fail(QCoreApplication::translate("VibeStudioModelObj",
													"Wavefront material libraries are not yet supported. Export without "
													"materials, then assign package materials in the modeller."));
		}
		if (command != "f")
		{
			return fail(
				QCoreApplication::translate(
					"VibeStudioModelObj",
					"Unsupported OBJ record '%1'. Export polygon faces without free-form geometry, lines, points, or external commands.")
					.arg(QString::fromLatin1(command.data(), command.size())));
		}
		if (tokens.size() < 4 || triangleCount + tokens.size() - 3 > modelDocumentMaxTriangles)
		{
			return fail(QCoreApplication::translate(
				"VibeStudioModelObj", "Faces need at least three corners and the model may contain at most 131,072 triangles."));
		}
		Face face;
		face.line = line;
		face.group = smoothing ? smoothing : -int(faces.size()) - 1;
		for (int i = 1; i < tokens.size(); ++i)
		{
			Corner c;
			if (!corner(tokens[i], &c))
			{
				return false;
			}
			if (!face.corners.isEmpty() && ((c.uv < 0) != (face.corners[0].uv < 0) || (c.normal < 0) != (face.corners[0].normal < 0)))
			{
				return fail(QCoreApplication::translate("VibeStudioModelObj",
														"Every corner of a face must use the same position/UV/normal index form."));
			}
			face.corners.append(c);
			usedPositions.insert(c.position);
		}
		if (!triangulate(&face))
		{
			return false;
		}
		const SurfaceKey key{object, groups, material};
		auto found = surfaceIndices.find(key);
		if (found == surfaceIndices.end())
		{
			if (mesh.surfaces.size() >= 32)
			{
				return fail(
					QCoreApplication::translate("VibeStudioModelObj", "OBJ object/group/material combinations exceed 32 surfaces."));
			}
			ModelSurface surface;
			QString base = groups.isEmpty() ? object : groups;
			if (base.isEmpty())
			{
				base = QStringLiteral("surface");
			}
			surface.name = base;
			for (int n = 2; surfaceNames.contains(surface.name); ++n)
			{
				const auto suffix = QStringLiteral("_%1").arg(n);
				surface.name = base.left(128 - suffix.size()) + suffix;
			}
			surfaceNames.insert(surface.name);
			if (!material.isEmpty())
			{
				surface.skinPaths.append(material);
			}
			surface.frames.append(ModelFrameGeometry{});
			found = surfaceIndices.emplace(key, int(mesh.surfaces.size())).first;
			mesh.surfaces.append(std::move(surface));
		}
		face.surface = found->second;
		for (const auto &c : face.corners)
		{
			if (!work.step())
			{
				return false;
			}
			auto &sum = smoothNormals[{c.position, face.group}];
			sum = add(sum, face.normal);
		}
		triangleCount += face.triangles.size();
		faces.append(std::move(face));
		return true;
	}
	bool triangulate(Face *face)
	{
		const int count = face->corners.size();
		const auto position = [&](int i) { return vector(positions[face->corners[i].position]); };
		const Vec origin = position(0);
		Vec n;
		double extent = 0;
		for (int i = 0; i < count; ++i)
		{
			const auto p = subtract(position(i), origin), q = subtract(position((i + 1) % count), origin);
			n = add(n, cross(p, q));
			extent = std::max(extent, length(p));
		}
		const double normalLength = length(n);
		if (normalLength <= 1e-10)
		{
			return fail(QCoreApplication::translate("VibeStudioModelObj", "A polygon is collapsed or has inconsistent winding."));
		}
		face->normal = n;
		const double planarTolerance = std::max(1e-7, extent * 1e-6);
		const int drop = std::abs(n.x) >= std::abs(n.y) && std::abs(n.x) >= std::abs(n.z) ? 0 : (std::abs(n.y) >= std::abs(n.z) ? 1 : 2);
		QVector<Point> points;
		for (int i = 0; i < count; ++i)
		{
			const auto p = subtract(position(i), origin);
			if (std::abs(dot(p, n)) / normalLength > planarTolerance)
			{
				return fail(QCoreApplication::translate(
					"VibeStudioModelObj", "A polygon is not planar. Triangulate it in the source modeller before importing."));
			}
			points.append(drop == 0 ? Point{p.y, p.z} : (drop == 1 ? Point{p.x, p.z} : Point{p.x, p.y}));
		}
		const auto onSegment = [&](Point a, Point b, Point p)
		{
			return orient(a, b, p) == 0 && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
				   p.y <= std::max(a.y, b.y);
		};
		for (int i = 0; i < count; ++i)
		{
			for (int j = i + 1; j < count; ++j)
			{
				if (!geometryCheck())
				{
					return false;
				}
				if (length(subtract(position(i), position(j))) == 0)
				{
					return fail(
						QCoreApplication::translate("VibeStudioModelObj", "A polygon repeats a position or contains a zero-length edge."));
				}
				if (j == i + 1 || (i == 0 && j == count - 1))
				{
					continue;
				}
				const auto a = points[i], b = points[(i + 1) % count], c = points[j], d = points[(j + 1) % count];
				const double abC = orient(a, b, c), abD = orient(a, b, d), cdA = orient(c, d, a), cdB = orient(c, d, b);
				if (((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) && ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0)))
				{
					return fail(QCoreApplication::translate("VibeStudioModelObj", "A polygon intersects itself."));
				}
				if (onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b))
				{
					return fail(
						QCoreApplication::translate("VibeStudioModelObj", "A polygon has overlapping or touching non-adjacent edges."));
				}
			}
		}
		double area = 0;
		for (int i = 0; i < count; ++i)
		{
			area += orient({0, 0}, points[i], points[(i + 1) % count]);
		}
		const double sign = area > 0 ? 1 : -1;
		QVector<int> remaining(count);
		std::iota(remaining.begin(), remaining.end(), 0);
		while (remaining.size() > 3)
		{
			bool clipped = false;
			for (int i = 0; i < remaining.size(); ++i)
			{
				if (!geometryCheck())
				{
					return false;
				}
				const int a = remaining[(i + remaining.size() - 1) % remaining.size()], b = remaining[i],
						  c = remaining[(i + 1) % remaining.size()];
				if (sign * orient(points[a], points[b], points[c]) <= 0)
				{
					continue;
				}
				bool contains = false;
				for (int p : remaining)
				{
					if (!geometryCheck())
					{
						return false;
					}
					if (p == a || p == b || p == c)
					{
						continue;
					}
					if (sign * orient(points[a], points[b], points[p]) >= 0 && sign * orient(points[b], points[c], points[p]) >= 0 &&
						sign * orient(points[c], points[a], points[p]) >= 0)
					{
						contains = true;
						break;
					}
				}
				if (contains)
				{
					continue;
				}
				face->triangles.append({a, b, c});
				remaining.removeAt(i);
				clipped = true;
				break;
			}
			if (!clipped)
			{
				return fail(QCoreApplication::translate("VibeStudioModelObj",
														"A polygon cannot be triangulated without losing or collapsing corners."));
			}
		}
		if (sign * orient(points[remaining[0]], points[remaining[1]], points[remaining[2]]) <= 0)
		{
			return fail(QCoreApplication::translate("VibeStudioModelObj", "A polygon ends in a collapsed triangle."));
		}
		face->triangles.append({remaining[0], remaining[1], remaining[2]});
		return true;
	}
	bool build()
	{
		QVector<std::map<VertexKey, int>> vertices(mesh.surfaces.size());
		int totalVertices = 0;
		for (const auto &face : faces)
		{
			line = face.line;
			auto &surface = mesh.surfaces[face.surface];
			auto &lookup = vertices[face.surface];
			QVector<int> indices;
			for (const auto &c : face.corners)
			{
				if (!work.step())
				{
					return false;
				}
				const VertexKey key{c.position, c.uv, c.normal, c.normal >= 0 ? 0 : face.group};
				auto found = lookup.find(key);
				if (found == lookup.end())
				{
					if (++totalVertices > modelDocumentMaxVertices)
					{
						return fail(QCoreApplication::translate(
							"VibeStudioModelObj", "UV, normal, smoothing, or material splits exceed 65,536 editable vertices."));
					}
					ModelVec3 normal;
					if (c.normal >= 0)
					{
						normal = normals[c.normal];
					}
					else
					{
						const auto sum = smoothNormals.at({c.position, face.group});
						if (length(sum) <= 1e-10)
						{
							return fail(QCoreApplication::translate(
								"VibeStudioModelObj",
								"A smoothing group produces a zero normal. Correct winding or supply explicit normals."));
						}
						normal = normalized(sum);
					}
					found = lookup.emplace(key, int(surface.texCoords.size())).first;
					surface.frames[0].positions.append(positions[c.position]);
					surface.frames[0].normals.append(normal);
					surface.texCoords.append(c.uv >= 0 ? uvs[c.uv] : ModelTexCoord{});
				}
				indices.append(found->second);
			}
			for (const auto &t : face.triangles)
			{
				if (!work.step())
				{
					return false;
				}
				surface.triangles.append({indices[t.a], indices[t.b], indices[t.c]});
			}
		}
		return true;
	}
};
} // namespace

ModelMesh decodeModelObj(const QString &path, const QByteArray &bytes, const ModelWorkControl &control)
{
	return Reader(path, control).run(bytes);
}
ModelMesh decodeModelObjFromArchive(const PackageArchiveReader &archive, const QString &path, const ModelWorkControl &control)
{
	ModelMesh mesh;
	mesh.sourcePath = path;
	mesh.format = ModelMeshFormat::WavefrontObj;
	mesh.formatId = QStringLiteral("obj");
	mesh.formatName = QStringLiteral("Wavefront OBJ");
	QByteArray bytes;
	const ModelArchiveReader reader(archive, control);
	if (!reader.readEntryBytes(path, &bytes, &mesh.error))
	{
		return mesh;
	}
	return decodeModelObj(path, bytes, control);
}
} // namespace vibestudio
