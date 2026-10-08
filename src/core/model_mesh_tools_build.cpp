#include "core/model_mesh_tools_p.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <tuple>

namespace vibestudio::model_tools
{
namespace
{
constexpr double pi = std::numbers::pi;
// Local primitive geometry: up is +Z, faces wind counter-clockwise seen from
// outside, and UV V grows downwards like the studio's image rows.
struct Built
{
	QVector<P3> positions, normals;
	QVector<ModelTexCoord> uvs;
	QVector<ModelTriangle> triangles;
	int add(P3 p, P3 n, double u, double v)
	{
		positions.append(p);
		normals.append(unit(n));
		uvs.append({float(u), float(v)});
		return positions.size() - 1;
	}
	void tri(int a, int b, int c) { triangles.append({a, b, c}); }
	void quad(int a, int b, int c, int d)
	{
		tri(a, b, c);
		tri(a, c, d);
	}
};
P3 ellipsoidNormal(P3 p, double hx, double hy, double hz)
{
	return unit({p.x / (hx * hx), p.y / (hy * hy), p.z / (hz * hz)});
}
void plane(Built &b, double sx, double sy)
{
	const double hx = sx / 2, hy = sy / 2;
	const P3 n{0, 0, 1};
	b.quad(b.add({-hx, -hy, 0}, n, 0, 1), b.add({hx, -hy, 0}, n, 1, 1), b.add({hx, hy, 0}, n, 1, 0), b.add({-hx, hy, 0}, n, 0, 0));
}
void grid(Built &b, double sx, double sy, int nx, int ny)
{
	const P3 n{0, 0, 1};
	const int first = b.positions.size();
	for (int j = 0; j <= ny; ++j)
	{
		for (int i = 0; i <= nx; ++i)
			b.add({-sx / 2 + sx * i / nx, -sy / 2 + sy * j / ny, 0}, n, double(i) / nx, 1 - double(j) / ny);
	}
	const auto at = [&](int i, int j) { return first + j * (nx + 1) + i; };
	for (int j = 0; j < ny; ++j)
	{
		for (int i = 0; i < nx; ++i)
			b.quad(at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
	}
}
void cube(Built &b, double sx, double sy, double sz)
{
	const double hx = sx / 2, hy = sy / 2, hz = sz / 2;
	struct Face
	{
		P3 normal, corner, s, t;
		int column, row;
	};
	const Face faces[]{
		{{1, 0, 0}, {hx, -hy, -hz}, {0, sy, 0}, {0, 0, sz}, 0, 0},	 {{-1, 0, 0}, {-hx, hy, -hz}, {0, -sy, 0}, {0, 0, sz}, 1, 0},
		{{0, 1, 0}, {hx, hy, -hz}, {-sx, 0, 0}, {0, 0, sz}, 2, 0},	 {{0, -1, 0}, {-hx, -hy, -hz}, {sx, 0, 0}, {0, 0, sz}, 0, 1},
		{{0, 0, 1}, {-hx, -hy, hz}, {sx, 0, 0}, {0, sy, 0}, 1, 1},	 {{0, 0, -1}, {-hx, hy, -hz}, {sx, 0, 0}, {0, -sy, 0}, 2, 1},
	};
	for (const auto &f : faces)
	{
		const auto uv = [&](double s, double t) { return std::pair{(f.column + s) / 3.0, (f.row + 1 - t) / 2.0}; };
		const auto [u0, v0] = uv(0, 0);
		const auto [u1, v1] = uv(1, 0);
		const auto [u2, v2] = uv(1, 1);
		const auto [u3, v3] = uv(0, 1);
		b.quad(b.add(f.corner, f.normal, u0, v0), b.add(f.corner + f.s, f.normal, u1, v1), b.add(f.corner + f.s + f.t, f.normal, u2, v2),
			   b.add(f.corner + f.t, f.normal, u3, v3));
	}
}
// A cap fan centred in a UV disc. `up` selects the winding and normal.
void cap(Built &b, double hx, double hy, double z, int n, bool up, double centreU, double centreV)
{
	const P3 normal{0, 0, up ? 1.0 : -1.0};
	const int centre = b.add({0, 0, z}, normal, centreU, centreV);
	QVector<int> rim;
	for (int i = 0; i < n; ++i)
	{
		const double angle = 2 * pi * i / n;
		rim.append(b.add({std::cos(angle) * hx, std::sin(angle) * hy, z}, normal, centreU + 0.24 * std::cos(angle),
						 centreV - 0.24 * std::sin(angle) * (up ? 1 : -1)));
	}
	for (int i = 0; i < n; ++i)
	{
		if (up)
			b.tri(centre, rim[i], rim[(i + 1) % n]);
		else
			b.tri(centre, rim[(i + 1) % n], rim[i]);
	}
}
void circle(Built &b, double sx, double sy, int n)
{
	const P3 normal{0, 0, 1};
	const int centre = b.add({0, 0, 0}, normal, 0.5, 0.5);
	QVector<int> rim;
	for (int i = 0; i < n; ++i)
	{
		const double angle = 2 * pi * i / n;
		rim.append(b.add({std::cos(angle) * sx / 2, std::sin(angle) * sy / 2, 0}, normal, 0.5 + 0.5 * std::cos(angle), 0.5 - 0.5 * std::sin(angle)));
	}
	for (int i = 0; i < n; ++i)
		b.tri(centre, rim[i], rim[(i + 1) % n]);
}
void cylinder(Built &b, double sx, double sy, double sz, int n, bool cone)
{
	const double hx = sx / 2, hy = sy / 2, hz = sz / 2;
	QVector<int> bottom, top;
	for (int i = 0; i <= n; ++i)
	{
		// The seam column (i == n) repeats angle 0 exactly so its vertices coincide.
		const double angle = 2 * pi * (i % n) / n, c = std::cos(angle), s = std::sin(angle);
		const P3 radial = cone ? P3{c * hy * sz, s * hx * sz, hx * hy} : P3{c / hx, s / hy, 0};
		bottom.append(b.add({c * hx, s * hy, -hz}, radial, double(i) / n, 0.5));
		if (cone)
		{
			if (i < n)
			{
				const double mid = 2 * pi * (i + 0.5) / n;
				const P3 tipNormal{std::cos(mid) * hy * sz, std::sin(mid) * hx * sz, hx * hy};
				top.append(b.add({0, 0, hz}, tipNormal, (i + 0.5) / n, 0));
			}
		}
		else
		{
			top.append(b.add({c * hx, s * hy, hz}, radial, double(i) / n, 0));
		}
	}
	for (int i = 0; i < n; ++i)
	{
		if (cone)
			b.tri(bottom[i], bottom[i + 1], top[i]);
		else
			b.quad(bottom[i], bottom[i + 1], top[i + 1], top[i]);
	}
	if (!cone)
		cap(b, hx, hy, hz, n, true, 0.25, 0.75);
	cap(b, hx, hy, -hz, n, false, 0.75, 0.75);
}
void uvSphere(Built &b, double hx, double hy, double hz, int n, int rings)
{
	const int first = b.positions.size();
	for (int j = 0; j <= rings; ++j)
	{
		const double phi = pi * j / rings;
		for (int i = 0; i <= n; ++i)
		{
			const double theta = 2 * pi * (i % n) / n;
			const bool pole = j == 0 || j == rings;
			// Poles sit exactly on the axis; sin(pi) is not exactly zero.
			const double ring = pole ? 0.0 : std::sin(phi), height = j == 0 ? 1.0 : j == rings ? -1.0 : std::cos(phi);
			const P3 p{ring * std::cos(theta) * hx, ring * std::sin(theta) * hy, height * hz};
			const P3 normal = pole ? P3{0, 0, j == 0 ? 1.0 : -1.0} : ellipsoidNormal(p, hx, hy, hz);
			b.add(p, normal, pole ? (i + 0.5) / n : double(i) / n, double(j) / rings);
		}
	}
	const auto at = [&](int i, int j) { return first + j * (n + 1) + i; };
	for (int j = 0; j < rings; ++j)
	{
		for (int i = 0; i < n; ++i)
		{
			if (j == 0)
				b.tri(at(i, j), at(i, j + 1), at(i + 1, j + 1));
			else if (j == rings - 1)
				b.tri(at(i, j), at(i, j + 1), at(i + 1, j));
			else
				b.quad(at(i, j), at(i, j + 1), at(i + 1, j + 1), at(i + 1, j));
		}
	}
}
void icoSphere(Built &b, double hx, double hy, double hz, int subdivisions)
{
	const double t = (1 + std::sqrt(5.0)) / 2;
	QVector<P3> points{{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
					   {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
	for (auto &p : points)
		p = unit(p);
	QVector<std::array<int, 3>> faces{{0, 11, 5}, {0, 5, 1},  {0, 1, 7},	{0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
									  {11, 10, 2}, {10, 7, 6}, {7, 1, 8},	{3, 9, 4},	{3, 4, 2},	 {3, 2, 6}, {3, 6, 8},
									  {3, 8, 9},  {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},  {9, 8, 1}};
	for (int level = 0; level < subdivisions; ++level)
	{
		std::map<std::pair<int, int>, int> midpoints;
		const auto midpoint = [&](int a, int c)
		{
			const auto key = std::pair{std::min(a, c), std::max(a, c)};
			const auto found = midpoints.find(key);
			if (found != midpoints.end())
				return found->second;
			points.append(unit(points[a] + points[c]));
			midpoints.emplace(key, points.size() - 1);
			return int(points.size() - 1);
		};
		QVector<std::array<int, 3>> next;
		for (const auto &f : std::as_const(faces))
		{
			const int ab = midpoint(f[0], f[1]), bc = midpoint(f[1], f[2]), ca = midpoint(f[2], f[0]);
			next << std::array<int, 3>{f[0], ab, ca} << std::array<int, 3>{f[1], bc, ab} << std::array<int, 3>{f[2], ca, bc}
				 << std::array<int, 3>{ab, bc, ca};
		}
		faces = next;
	}
	// Spherical UVs; corners on the far side of the U seam or at a pole get
	// their own vertices so no face stretches across the texture.
	std::map<std::tuple<int, double, double>, int> shared;
	const auto corner = [&](int index, double u, double v)
	{
		const auto key = std::tuple{index, u, v};
		const auto found = shared.find(key);
		if (found != shared.end())
			return found->second;
		const P3 p{points[index].x * hx, points[index].y * hy, points[index].z * hz};
		const int created = b.add(p, ellipsoidNormal(p, hx, hy, hz), u, v);
		shared.emplace(key, created);
		return created;
	};
	for (const auto &f : std::as_const(faces))
	{
		std::array<double, 3> u{}, v{};
		std::array<bool, 3> pole{};
		for (int k = 0; k < 3; ++k)
		{
			const P3 p = points[f[k]];
			pole[k] = std::abs(p.z) > 1 - 1e-9;
			u[k] = 0.5 + std::atan2(p.y, p.x) / (2 * pi);
			v[k] = std::acos(std::clamp(p.z, -1.0, 1.0)) / pi;
		}
		const double high = *std::max_element(u.cbegin(), u.cend());
		for (int k = 0; k < 3; ++k)
		{
			if (!pole[k] && high - u[k] > 0.5)
				u[k] += 1;
		}
		for (int k = 0; k < 3; ++k)
		{
			if (pole[k])
			{
				double sum = 0;
				int count = 0;
				for (int other = 0; other < 3; ++other)
				{
					if (!pole[other])
					{
						sum += u[other];
						++count;
					}
				}
				u[k] = count ? sum / count : 0.5;
			}
		}
		b.tri(corner(f[0], u[0], v[0]), corner(f[1], u[1], v[1]), corner(f[2], u[2], v[2]));
	}
}
bool torus(Built &b, double sx, double sz, int n, int m, QString *error)
{
	const double r = sz / 2, major = sx / 2 - r;
	if (major <= r * 1.001)
		return fail(error, Text::tr("The torus tube would cross its own centre. Make the width more than twice the height."));
	const int first = b.positions.size();
	for (int i = 0; i <= n; ++i)
	{
		const double theta = 2 * pi * (i % n) / n;
		for (int j = 0; j <= m; ++j)
		{
			const double phi = 2 * pi * (j % m) / m;
			const P3 normal{std::cos(phi) * std::cos(theta), std::cos(phi) * std::sin(theta), std::sin(phi)};
			const P3 p{(major + r * std::cos(phi)) * std::cos(theta), (major + r * std::cos(phi)) * std::sin(theta), r * std::sin(phi)};
			b.add(p, normal, double(i) / n, 1 - double(j) / m);
		}
	}
	const auto at = [&](int i, int j) { return first + i * (m + 1) + j; };
	for (int i = 0; i < n; ++i)
	{
		for (int j = 0; j < m; ++j)
			b.quad(at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
	}
	return true;
}
// Turns local +Z to the chosen axis with a cyclic (handedness-preserving) permutation.
P3 orient(P3 p, int axis)
{
	return axis == 0 ? P3{p.z, p.x, p.y} : axis == 1 ? P3{p.y, p.z, p.x} : p;
}
} // namespace

bool addPrimitive(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto &options = edit.tool;
	const double sx = options.size[0], sy = options.size[1], sz = options.size[2];
	const bool flat = options.primitive == ModelPrimitive::Plane || options.primitive == ModelPrimitive::Circle ||
					  options.primitive == ModelPrimitive::Grid;
	if (!(sx > 0) || !(sy > 0) || (!flat && !(sz > 0)))
		return fail(error, Text::tr("Primitive sizes must be positive."));
	Built built;
	switch (options.primitive)
	{
	case ModelPrimitive::Plane:
		plane(built, sx, sy);
		break;
	case ModelPrimitive::Cube:
		cube(built, sx, sy, sz);
		break;
	case ModelPrimitive::Circle:
		circle(built, sx, sy, options.segments);
		break;
	case ModelPrimitive::Grid:
		grid(built, sx, sy, std::max(1, options.segments), std::max(1, options.rings));
		break;
	case ModelPrimitive::Cylinder:
		cylinder(built, sx, sy, sz, options.segments, false);
		break;
	case ModelPrimitive::Cone:
		cylinder(built, sx, sy, sz, options.segments, true);
		break;
	case ModelPrimitive::UvSphere:
		uvSphere(built, sx / 2, sy / 2, sz / 2, options.segments, std::max(2, options.rings));
		break;
	case ModelPrimitive::IcoSphere:
		icoSphere(built, sx / 2, sy / 2, sz / 2, std::clamp(options.rings, 1, 5));
		break;
	case ModelPrimitive::Torus:
		if (!torus(built, sx, sz, options.segments, std::max(3, options.rings), error))
			return false;
		break;
	}
	if (!reserveCapacity(*mesh, selection->surface, built.positions.size(), built.triangles.size(), error))
		return false;
	auto &surface = mesh->surfaces[selection->surface];
	const int first = surface.vertexCount;
	const P3 centre = arrayPoint(options.point);
	for (int i = 0; i < built.positions.size(); ++i)
	{
		if (!work.step())
			return false;
		surface.texCoords.append(built.uvs[i]);
		const auto p = vec(orient(built.positions[i], options.axis) + centre), n = vec(orient(built.normals[i], options.axis));
		for (auto &frame : surface.frames)
		{
			frame.positions.append(p);
			frame.normals.append(n);
		}
	}
	surface.vertexCount = surface.texCoords.size();
	QSet<int> created;
	for (const auto &t : std::as_const(built.triangles))
	{
		created.insert(surface.triangles.size());
		surface.triangles.append({t.a + first, t.b + first, t.c + first});
	}
	selection->faces = created;
	selection->vertices.clear();
	selection->edges.clear();
	return work.check();
}
} // namespace vibestudio::model_tools
