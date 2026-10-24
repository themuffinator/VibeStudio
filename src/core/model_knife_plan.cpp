#include "core/model_knife.h"

#include "core/model_geometry_helpers.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <functional>

namespace vibestudio
{
namespace
{
using namespace model_geometry;
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelKnife)
};
constexpr double tolerance = 1e-10, nearDepth = 1e-4;
P3 vector(const std::array<double, 3> &a) { return {a[0], a[1], a[2]}; }
P2 vector(const std::array<double, 2> &a) { return {a[0], a[1]}; }
P2 mix(P2 a, P2 b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
struct Face
{
	int surface = -1, index = -1;
	P3 points[3], normal;
	double plane = 0;
	QVector<P2> outline;
	bool target = false;
};
struct Span
{
	double lo = 0, hi = 0, slope = 0, intercept = 0;
	int face = -1;
	double score(double t) const { return slope * t + intercept; }
};
using Envelope = QVector<Span>;

bool barycentric(const Face &face, P2 screen, bool perspective, std::array<double, 2> *out)
{
	const P3 n = face.normal;
	const double denominator = perspective ? n.x * screen.x + n.y * screen.y + n.z : n.z;
	if (std::abs(denominator) <= 1e-300)
		return false;
	const double z = perspective ? -face.plane / denominator : -(n.x * screen.x + n.y * screen.y + face.plane) / denominator;
	const P3 hit = perspective ? P3{screen.x * z, screen.y * z, z} : P3{screen.x, screen.y, z};
	const int axis = std::abs(n.x) > std::abs(n.y) ? (std::abs(n.x) > std::abs(n.z) ? 0 : 2) : (std::abs(n.y) > std::abs(n.z) ? 1 : 2);
	const P2 a = project(face.points[0], axis), b = project(face.points[1], axis), c = project(face.points[2], axis),
			 p = project(hit, axis);
	const double area = orient(a, b, c);
	double weights[3]{orient(b, c, p) / area, orient(c, a, p) / area, orient(a, b, p) / area};
	double total = 0;
	for (double &weight : weights)
	{
		if (!std::isfinite(weight) || weight < -1e-7 || weight > 1 + 1e-7)
			return false;
		weight = std::clamp(weight, 0.0, 1.0);
		if (weight < tolerance)
			weight = 0;
		total += weight;
	}
	*out = {weights[1] / total, weights[2] / total};
	return true;
}

bool interval(const Face &face, P2 a, P2 b, bool perspective, Span *span)
{
	double lo = 0, hi = 1;
	const double winding = area(face.outline) > 0 ? 1 : -1;
	for (int i = 0; i < face.outline.size(); ++i)
	{
		const auto p = face.outline[i], q = face.outline[(i + 1) % face.outline.size()];
		const double da = winding * orient(p, q, a), db = winding * orient(p, q, b);
		if (da < 0 && db < 0)
			return false;
		if ((da < 0) != (db < 0))
		{
			const double t = da / (da - db);
			if (da < 0)
				lo = std::max(lo, t);
			else
				hi = std::min(hi, t);
		}
	}
	if (hi - lo <= tolerance)
		return false;
	span->lo = lo;
	span->hi = hi;
	// The nearest-depth score is linear along the screen segment: -z for
	// orthographic projection, reciprocal z for perspective projection.
	const auto score = [&](P2 p)
	{
		return perspective ? -(face.normal.x * p.x + face.normal.y * p.y + face.normal.z) / face.plane
						   : (face.normal.x * p.x + face.normal.y * p.y + face.plane) / face.normal.z;
	};
	span->intercept = score(a);
	span->slope = score(b) - span->intercept;
	return std::isfinite(span->intercept) && std::isfinite(span->slope);
}

// Merge two continuous depth envelopes. Unlike midpoint-only visibility,
// this retains switches between overlapping faces inside an interval. Each
// input contains disjoint ordered spans; gaps have no covering geometry.
bool merge(const Envelope &a, const Envelope &b, Envelope *out, ModelWorkProgress &work)
{
	QVector<double> events;
	events.reserve((a.size() + b.size()) * 2);
	for (const auto &list : {&a, &b})
		for (const auto &s : *list)
		{
			events.append(s.lo);
			events.append(s.hi);
		}
	std::sort(events.begin(), events.end());
	events.erase(std::unique(events.begin(), events.end()), events.end());
	int ai = 0, bi = 0;
	const auto append = [&](const Span &span, double lo, double hi)
	{
		if (hi - lo <= tolerance)
			return;
		if (!out->isEmpty() && out->last().face == span.face && out->last().hi == lo)
			out->last().hi = hi;
		else
		{
			auto part = span;
			part.lo = lo;
			part.hi = hi;
			out->append(part);
		}
	};
	for (int i = 0; i + 1 < events.size(); ++i)
	{
		if (!work.step())
			return false;
		const double lo = events[i], hi = events[i + 1], mid = (lo + hi) / 2;
		while (ai < a.size() && a[ai].hi <= lo)
			++ai;
		while (bi < b.size() && b[bi].hi <= lo)
			++bi;
		const Span *sa = ai < a.size() && a[ai].lo <= mid && a[ai].hi >= mid ? &a[ai] : nullptr;
		const Span *sb = bi < b.size() && b[bi].lo <= mid && b[bi].hi >= mid ? &b[bi] : nullptr;
		if (!sa && !sb)
			continue;
		if (!sa || !sb)
		{
			append(sa ? *sa : *sb, lo, hi);
			continue;
		}
		const auto nearer = [&](double t) -> const Span &
		{
			const double difference = sa->score(t) - sb->score(t);
			return difference > 0 || (difference == 0 && sa->face < sb->face) ? *sa : *sb;
		};
		const double slope = sa->slope - sb->slope;
		const double cross = slope == 0 ? lo : (sb->intercept - sa->intercept) / slope;
		if (cross > lo + tolerance && cross < hi - tolerance)
		{
			append(nearer((lo + cross) / 2), lo, cross);
			append(nearer((cross + hi) / 2), cross, hi);
		}
		else
			append(nearer(mid), lo, hi);
	}
	return true;
}
} // namespace

bool planModelKnife(const ModelMesh &mesh, int surface, int frame, const QSet<int> &selectedFaces, const ModelKnifeOptions &o,
					QVector<ModelKnifeCut> *cuts, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	const auto fail = [&](const QString &message)
	{
		if (error)
			*error = message;
		return false;
	};
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!cuts || !work.check())
		return false;
	if (surface < 0 || surface >= mesh.surfaces.size() || frame < 0 || frame >= mesh.frames.size())
		return fail(Text::tr("Choose an existing surface and reference pose for the knife."));
	if (o.segments.isEmpty() || o.segments.size() > 256)
		return fail(Text::tr("Draw between 1 and 256 knife segments."));
	for (const auto &v : {o.origin, o.right, o.up, o.forward})
		for (double x : v)
			if (!std::isfinite(x) || std::abs(x) > 1000000)
				return fail(Text::tr("Knife camera coordinates must be finite and within ±1,000,000 units."));
	const P3 origin = vector(o.origin), right = vector(o.right), up = vector(o.up), forward = vector(o.forward);
	if (std::abs(length(right) - 1) > 1e-6 || std::abs(length(up) - 1) > 1e-6 || std::abs(length(forward) - 1) > 1e-6 ||
		std::abs(dot(right, up)) > 1e-6 || std::abs(dot(right, forward)) > 1e-6 || std::abs(dot(up, forward)) > 1e-6)
		return fail(Text::tr("Knife projection needs three perpendicular unit camera axes."));
	for (const auto &segment : o.segments)
	{
		for (const auto &p : {segment.a, segment.b})
			for (double value : p)
				if (!std::isfinite(value) || std::abs(value) > 1e9)
					return fail(Text::tr("Knife path coordinates must be finite and within ±1,000,000,000."));
		if (std::hypot(segment.a[0] - segment.b[0], segment.a[1] - segment.b[1]) <= tolerance)
			return fail(Text::tr("A knife segment needs two distinct endpoints."));
	}
	for (auto i = o.hiddenFaces.cbegin(); i != o.hiddenFaces.cend(); ++i)
	{
		if (i.key() < 0 || i.key() >= mesh.surfaces.size())
			return fail(Text::tr("A hidden knife surface does not exist."));
		for (int f : i.value())
			if (f < 0 || f >= mesh.surfaces[i.key()].triangles.size())
				return fail(Text::tr("A hidden knife face does not exist."));
	}
	if (o.selectedOnly && selectedFaces.isEmpty())
		return fail(Text::tr("Select the faces to cut, or disable Selected faces only."));
	for (int f : selectedFaces)
		if (f < 0 || f >= mesh.surfaces[surface].triangles.size())
			return fail(Text::tr("A selected knife face does not exist."));
	QVector<Face> faces;
	for (int s = 0; s < mesh.surfaces.size(); ++s)
	{
		if (o.through && s != surface)
			continue;
		const auto &source = mesh.surfaces[s];
		if (frame >= source.frames.size())
			return fail(Text::tr("A surface is missing the knife reference pose."));
		for (int f = 0; f < source.triangles.size(); ++f)
		{
			if (!work.step())
				return false;
			if (o.hiddenFaces.value(s).contains(f))
				continue;
			Face face;
			face.surface = s;
			face.index = f;
			face.target = s == surface && (!o.selectedOnly || selectedFaces.contains(f));
			if (o.through && !face.target)
				continue;
			const auto &t = source.triangles[f];
			const int corners[3]{t.a, t.b, t.c};
			for (int c = 0; c < 3; ++c)
			{
				if (corners[c] < 0 || corners[c] >= source.frames[frame].positions.size())
					return fail(Text::tr("A knife face has an invalid vertex."));
				const P3 p = point(source.frames[frame].positions[corners[c]]) - origin;
				face.points[c] = {dot(p, right), dot(p, up), dot(p, forward)};
			}
			face.normal = cross(face.points[1] - face.points[0], face.points[2] - face.points[0]);
			face.plane = -dot(face.normal, face.points[0]);
			if ((!o.perspective && std::abs(face.normal.z) <= length(face.normal) * 1e-12) ||
				(o.perspective && std::abs(face.plane) <= length(face.normal) * 1e-12))
				continue;
			QVector<P3> polygon{face.points[0], face.points[1], face.points[2]};
			if (o.perspective)
			{
				QVector<P3> clipped;
				for (int c = 0; c < polygon.size(); ++c)
				{
					const auto a = polygon[c], b = polygon[(c + 1) % polygon.size()];
					if (a.z >= nearDepth)
						clipped.append(a);
					if ((a.z >= nearDepth) != (b.z >= nearDepth))
						clipped.append(a + (b - a) * ((nearDepth - a.z) / (b.z - a.z)));
				}
				polygon = std::move(clipped);
			}
			if (polygon.size() < 3)
				continue;
			for (const auto &p : polygon)
				face.outline.append(o.perspective ? P2{p.x / p.z, p.y / p.z} : P2{p.x, p.y});
			if (std::abs(area(face.outline)) <= 1e-20)
				continue;
			faces.append(std::move(face));
		}
	}
	QVector<ModelKnifeCut> planned;
	for (const auto &segment : o.segments)
	{
		const P2 a = vector(segment.a), b = vector(segment.b);
		Envelope spans;
		for (int i = 0; i < faces.size(); ++i)
		{
			if (!work.step())
				return false;
			Span span;
			span.face = i;
			if (interval(faces[i], a, b, o.perspective, &span))
				spans.append(span);
		}
		if (!o.through && !spans.isEmpty())
		{
			bool ok = true;
			std::function<Envelope(int, int)> envelope = [&](int first, int last) -> Envelope
			{
				if (!ok || first == last)
					return {};
				if (last - first == 1)
					return {spans[first]};
				const int mid = first + (last - first) / 2;
				const auto left = envelope(first, mid), rightSide = envelope(mid, last);
				Envelope joined;
				ok = ok && merge(left, rightSide, &joined, work);
				return joined;
			};
			spans = envelope(0, spans.size());
			if (!ok)
				return false;
		}
		for (const auto &span : spans)
		{
			if (!work.step())
				return false;
			const auto &face = faces[span.face];
			if (!face.target)
				continue;
			ModelKnifeCut cut;
			cut.face = face.index;
			if (!barycentric(face, mix(a, b, span.lo), o.perspective, &cut.a) ||
				!barycentric(face, mix(a, b, span.hi), o.perspective, &cut.b))
				return fail(Text::tr("A knife intersection is too close to the camera or triangle boundary to resolve reliably."));
			if (std::hypot(cut.a[0] - cut.b[0], cut.a[1] - cut.b[1]) <= tolerance)
				continue;
			planned.append(cut);
			if (planned.size() > 262144)
				return fail(Text::tr("The knife path crosses too many faces. Use fewer segments or a smaller selection."));
		}
	}
	if (planned.isEmpty())
		return fail(Text::tr("The knife path does not cross an eligible face."));
	if (!work.check())
		return false;
	*cuts = std::move(planned);
	return true;
}
} // namespace vibestudio
