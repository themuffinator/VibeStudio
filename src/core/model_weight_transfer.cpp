#include "core/model_weight_transfer.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/model_skinning_work.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelWeightTransfer)
};
bool fail(QString *error, const QString &text)
{
	if (error)
		*error = text;
	return false;
}
using Vec = std::array<double, 3>;
using Weights = QMap<int, double>;
Vec vec(ModelVec3 p)
{
	return {p.x, p.y, p.z};
}
Vec minus(Vec a, Vec b)
{
	return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Vec plus(Vec a, Vec b)
{
	return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
Vec scale(Vec a, double s)
{
	return {a[0] * s, a[1] * s, a[2] * s};
}
double dot(Vec a, Vec b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
bool near(ModelVec3 a, ModelVec3 b)
{
	const auto d = minus(vec(a), vec(b));
	return std::abs(d[0]) <= .0001 && std::abs(d[1]) <= .0001 && std::abs(d[2]) <= .0001;
}
bool sameWeights(const Weights &a, const Weights &b)
{
	for (auto it = a.cbegin(); it != a.cend(); ++it)
		if (std::abs(it.value() - b.value(it.key())) > 1e-6)
			return false;
	for (auto it = b.cbegin(); it != b.cend(); ++it)
		if (std::abs(it.value() - a.value(it.key())) > 1e-6)
			return false;
	return true;
}
bool readWeights(const ModelSurface &s, int v, Weights *out, QString *error)
{
	double sum = 0;
	for (int i = s.skinning.first[v]; i < s.skinning.first[v] + s.skinning.count[v]; ++i)
	{
		const auto &w = s.skinning.influences[i];
		if (w.weight < 0 || !std::isfinite(w.weight))
			return fail(error, Text::tr("Transfer requires nonnegative, normalised weights. Repair the binding first."));
		if (w.weight > 0)
			(*out)[w.joint] += w.weight;
		sum += w.weight;
	}
	if (std::abs(sum - 1) > .00001)
		return fail(error, Text::tr("Transfer requires nonnegative, normalised weights. Repair the binding first."));
	return true;
}
struct Sample
{
	std::array<int, 3> vertices{};
	int count = 1, index = 0;
	Vec low{}, high{};
};
struct Node
{
	Vec low{}, high{};
	int first = 0, last = 0, left = -1, right = -1;
};
struct Hit
{
	int sample = -1;
	double distance = 0, limit = 0;
	Weights weights;
	bool ambiguous = false;
};
// An index over bind-space primitives keeps correspondence practical for dense
// meshes. Coincident seam candidates remain separate until weights agree.
class Index
{
  public:
	QVector<Sample> samples;
	QVector<Node> nodes;
	const ModelFrameGeometry &geometry;
	const QVector<Weights> &weights;
	ModelWorkProgress &work;
	Index(const ModelFrameGeometry &g, const QVector<Weights> &w, ModelWorkProgress &p) : geometry(g), weights(w), work(p) {}
	int build(int first, int last)
	{
		if (!work.check())
			return -1;
		Node n;
		n.first = first;
		n.last = last;
		n.low = samples[first].low;
		n.high = samples[first].high;
		for (int i = first + 1; i < last; ++i)
		{
			if (!work.step())
				return -1;
			for (int a = 0; a < 3; ++a)
			{
				n.low[a] = std::min(n.low[a], samples[i].low[a]);
				n.high[a] = std::max(n.high[a], samples[i].high[a]);
			}
		}
		const int index = nodes.size();
		nodes.append(n);
		if (last - first > 8)
		{
			const auto extent = minus(n.high, n.low);
			const int axis = int(std::max_element(extent.begin(), extent.end()) - extent.begin());
			const int middle = first + (last - first) / 2;
			std::nth_element(samples.begin() + first, samples.begin() + middle, samples.begin() + last,
							 [axis](const Sample &a, const Sample &b)
							 {
								 const double ca = a.low[axis] + a.high[axis], cb = b.low[axis] + b.high[axis];
								 return ca == cb ? a.index < b.index : ca < cb;
							 });
			const int left = build(first, middle);
			if (left < 0)
				return -1;
			const int right = build(middle, last);
			if (right < 0)
				return -1;
			nodes[index].left = left;
			nodes[index].right = right;
		}
		return index;
	}
	Vec barycentric(const Sample &s, Vec p) const
	{
		if (s.count == 1)
			return {1, 0, 0};
		const Vec a = vec(geometry.positions[s.vertices[0]]), b = vec(geometry.positions[s.vertices[1]]),
				  c = vec(geometry.positions[s.vertices[2]]);
		const auto ab = minus(b, a), ac = minus(c, a), ap = minus(p, a);
		const double aa = dot(ab, ab), bb = dot(ac, ac), cross = dot(ab, ac), den = aa * bb - cross * cross;
		if (den > 1e-24 * std::max(1., aa * bb))
		{
			const double v = (dot(ap, ab) * bb - dot(ap, ac) * cross) / den, w = (dot(ap, ac) * aa - dot(ap, ab) * cross) / den;
			if (v >= 0 && w >= 0 && v + w <= 1)
				return {1 - v - w, v, w};
		}
		const std::array<Vec, 3> corners{a, b, c};
		Vec result{};
		double best = std::numeric_limits<double>::infinity();
		for (int i = 0; i < 3; ++i)
		{
			const int j = (i + 1) % 3;
			const auto edge = minus(corners[j], corners[i]);
			const double length = dot(edge, edge);
			const double t = length > 0 ? std::clamp(dot(minus(p, corners[i]), edge) / length, 0., 1.) : 0;
			const auto delta = minus(p, plus(corners[i], scale(edge, t)));
			const double distance = dot(delta, delta);
			if (distance < best)
			{
				best = distance;
				result = {0, 0, 0};
				result[i] = 1 - t;
				result[j] = t;
			}
		}
		return result;
	}
	double lowerBound(int index, Vec p) const
	{
		const auto &n = nodes[index];
		double distance = 0;
		for (int a = 0; a < 3; ++a)
		{
			const double d = p[a] - std::clamp(p[a], n.low[a], n.high[a]);
			distance += d * d;
		}
		return distance;
	}
	bool query(int index, Vec p, Vec normal, double cosine, bool filter, Hit *hit) const
	{
		if (!work.step())
			return false;
		const double tolerance = 1e-12 * std::max(1., hit->distance);
		if (lowerBound(index, p) > hit->distance + tolerance)
			return true;
		const auto &n = nodes[index];
		if (n.left >= 0)
		{
			int a = n.left, b = n.right;
			if (lowerBound(a, p) > lowerBound(b, p))
				std::swap(a, b);
			return query(a, p, normal, cosine, filter, hit) && query(b, p, normal, cosine, filter, hit);
		}
		for (int i = n.first; i < n.last; ++i)
		{
			if (!work.step())
				return false;
			const auto &s = samples[i];
			const auto bary = barycentric(s, p);
			Vec point{}, sn{};
			for (int k = 0; k < s.count; ++k)
			{
				point = plus(point, scale(vec(geometry.positions[s.vertices[k]]), bary[k]));
				sn = plus(sn, scale(vec(geometry.normals[s.vertices[k]]), bary[k]));
			}
			const auto delta = minus(p, point);
			const double distance = dot(delta, delta);
			const double eps = 1e-12 * std::max(1., std::min(distance, hit->distance));
			if (distance > hit->limit || distance > hit->distance + eps)
				continue;
			const double length = std::sqrt(dot(normal, normal) * dot(sn, sn));
			if (filter && (length <= 1e-20 || dot(normal, sn) / length < cosine - 1e-12))
				continue;
			Weights blended;
			for (int k = 0; k < s.count; ++k)
				if (bary[k] > 0)
					for (auto w = weights[s.vertices[k]].cbegin(); w != weights[s.vertices[k]].cend(); ++w)
						blended[w.key()] += bary[k] * w.value();
			if (hit->sample < 0 || distance < hit->distance - eps)
			{
				hit->weights = std::move(blended);
				hit->sample = s.index;
				hit->distance = distance;
				hit->ambiguous = false;
			}
			else
			{
				if (!sameWeights(hit->weights, blended))
					hit->ambiguous = true;
				// Stable tie-breaking for numerically equivalent samples, independent
				// of BVH partition/traversal order. Conflicting samples still fail.
				if (s.index < hit->sample)
				{
					hit->sample = s.index;
					hit->weights = std::move(blended);
				}
				hit->distance = std::min(hit->distance, distance);
			}
		}
		return true;
	}
};
bool assign(ModelMesh *mesh, int surfaceIndex, int bind, int vertex, const Weights &sample, const ModelWeightTransferOptions &options,
			const QSet<int> &locks, ModelSurfaceSkinning *output, bool *changed, QString *error)
{
	const auto &surface = mesh->surfaces[surfaceIndex];
	const bool bound = !surface.skinning.isEmpty();
	Weights old;
	if (bound && !readWeights(surface, vertex, &old, error))
		return false;
	double locked = 0;
	for (int j : locks)
		locked += old.value(j);
	if (locked > 1.00001)
		return fail(error, Text::tr("Locked weights exceed one at vertex %1.").arg(vertex));
	// Preserve float bindings accepted by the normalisation tolerance, including
	// an entirely locked distribution whose stored sum rounds just above one.
	const double available = std::max(0., 1 - locked);
	double sum = 0;
	for (auto it = sample.cbegin(); it != sample.cend(); ++it)
		if (!locks.contains(it.key()))
			sum += it.value();
	if (sum <= 0 && available > 1e-12)
		return fail(error, Text::tr("The transferred sample has no unlocked influence for vertex %1.").arg(vertex));
	if (!bound && options.strength != 1)
		return fail(error, Text::tr("An unbound destination needs full transfer strength."));
	Weights result = old;
	for (auto it = sample.cbegin(); it != sample.cend(); ++it)
		if (!result.contains(it.key()))
			result[it.key()] = 0;
	for (auto it = result.begin(); it != result.end(); ++it)
		if (!locks.contains(it.key()))
			it.value() =
				old.value(it.key()) * (1 - options.strength) + (sum > 0 ? sample.value(it.key()) / sum * available : 0) * options.strength;
	bool different = !bound;
	for (auto it = result.cbegin(); it != result.cend(); ++it)
		if (float(it.value()) != float(old.value(it.key())))
			different = true;
	const int start = output->influences.size();
	output->first.append(start);
	const int first = bound ? surface.skinning.first[vertex] : 0, end = bound ? first + surface.skinning.count[vertex] : 0;
	bool hasNormals = !bound;
	for (int i = first; i < end; ++i)
	{
		auto w = surface.skinning.influences[i];
		if (!different)
		{
			output->influences.append(w);
			continue;
		}
		if (w.weight > 0 &&
			!near(modelJointTransformPoint(mesh->skeleton.joints[w.joint].bind, w.offset), surface.frames[bind].positions[vertex]))
			return fail(error, Text::tr("Destination vertex %1 has distinct bind offsets. Rebind it before transfer.").arg(vertex));
		if (w.weight > 0 && dot(vec(w.normalOffset), vec(w.normalOffset)) > 0)
			hasNormals = true;
		if (locks.contains(w.joint))
			output->influences.append(w);
		else if (w.weight > 0 && result.value(w.joint) > 0)
		{
			w.weight = float(w.weight * result.value(w.joint) / old.value(w.joint));
			if (w.weight > 0)
				output->influences.append(w);
		}
	}
	if (different)
		for (auto it = result.cbegin(); it != result.cend(); ++it)
			if (it.value() > 0 && old.value(it.key()) <= 0 && !locks.contains(it.key()))
			{
				bool ok = false;
				const auto inverse = modelJointInverse(mesh->skeleton.joints[it.key()].bind, &ok);
				if (!ok || !modelJointMatrixIsFinite(inverse))
					return fail(error, Text::tr("A destination joint has a singular bind transform."));
				output->influences.append(
					{it.key(), float(it.value()), modelJointTransformPoint(inverse, surface.frames[bind].positions[vertex]),
					 hasNormals ? modelJointTransformVector(inverse, surface.frames[bind].normals[vertex]) : ModelVec3{}});
			}
	const int count = output->influences.size() - start;
	if (count < 1 || count > 64)
		return fail(error, Text::tr("Transfer requires 1–64 influences at each vertex. Reduce weights explicitly if needed."));
	output->count.append(count);
	*changed |= different;
	return true;
}
} // namespace
bool applyModelWeightTransfer(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	const auto &o = edit.weightTransfer;
	const auto &selection = edit.selection;
	const int target = selection.surface;
	if (edit.kind != ModelEditKind::TransferSkinWeights || edit.frame != -1 || !selection.tag.isEmpty() || !selection.joint.isEmpty() ||
		!selection.collision.isEmpty() || !selection.surfaces.isEmpty() || target < 0 || target >= mesh->surfaces.size() ||
		o.sourceSurface < 0 || o.sourceSurface >= mesh->surfaces.size() || mesh->surfaces[o.sourceSurface].skinning.isEmpty() ||
		int(o.method) < 0 || int(o.method) > 1 || o.mirrorAxis < -1 || o.mirrorAxis > 2 || int(o.side) < 0 || int(o.side) > 2 ||
		!std::isfinite(o.plane) || std::abs(o.plane) > 1048576 || !std::isfinite(o.centreTolerance) || o.centreTolerance < 0 ||
		o.centreTolerance > 1048576 || !std::isfinite(o.maximumDistance) || o.maximumDistance < 0 || o.maximumDistance > 1048576 ||
		!std::isfinite(o.normalAngle) || o.normalAngle < 0 || o.normalAngle > 180 || !std::isfinite(o.strength) || o.strength <= 0 ||
		o.strength > 1 || (o.mirrorAxis < 0 && (o.side != ModelWeightTransferSide::All || o.plane != 0 || o.centreTolerance != .0001)))
		return fail(error, Text::tr("Choose a bound source, one destination surface, valid transfer settings and no competing selection."));
	QVector<int> mapping;
	for (int j = 0; j < mesh->skeleton.joints.size(); ++j)
		mapping.append(j);
	for (auto it = o.joints.cbegin(); it != o.joints.cend(); ++it)
	{
		const int from = modelJointIndex(mesh->skeleton, it.key(), Qt::CaseSensitive),
				  to = modelJointIndex(mesh->skeleton, it.value(), Qt::CaseSensitive);
		if (from < 0 || to < 0)
			return fail(error, Text::tr("Every joint mapping must name existing source and destination joints."));
		mapping[from] = to;
	}
	QSet<int> locks;
	for (const auto &name : o.lockedJoints)
	{
		const int joint = modelJointIndex(mesh->skeleton, name, Qt::CaseSensitive);
		if (joint < 0 || locks.contains(joint))
			return fail(error, Text::tr("Locked joints must be distinct existing destination joints."));
		locks.insert(joint);
	}
	ModelSkinningWork skin;
	if (!skin.prepare(*mesh, error) || !skin.validateSurface(*mesh, target, error, control))
		return false;
	const auto source = mesh->surfaces[o.sourceSurface]; // Frozen even for same-surface mirroring.
	const auto &geometry = source.frames[skin.bindFrame];
	QVector<Weights> weights(source.vertexCount);
	for (int v = 0; v < source.vertexCount; ++v)
	{
		if (!work.step())
			return false;
		Weights raw;
		if (!readWeights(source, v, &raw, error))
			return false;
		for (auto it = raw.cbegin(); it != raw.cend(); ++it)
			weights[v][mapping[it.key()]] += it.value();
	}
	Index index(geometry, weights, work);
	const int sign = o.side == ModelWeightTransferSide::PositiveToNegative ? 1 : -1;
	const auto sourceSide = [&](int v)
	{ return o.side == ModelWeightTransferSide::All || sign * (vec(geometry.positions[v])[o.mirrorAxis] - o.plane) >= -o.centreTolerance; };
	const int primitives = o.method == ModelWeightTransferMethod::Vertex ? source.vertexCount : source.triangles.size();
	for (int i = 0; i < primitives; ++i)
	{
		if (!work.step())
			return false;
		Sample s;
		s.index = i;
		if (o.method == ModelWeightTransferMethod::Vertex)
			s.vertices = {i, i, i};
		else
		{
			const auto &t = source.triangles[i];
			s.vertices = {t.a, t.b, t.c};
			s.count = 3;
		}
		bool eligible = true;
		for (int k = 0; k < s.count; ++k)
			eligible &= sourceSide(s.vertices[k]);
		if (!eligible)
			continue;
		s.low = s.high = vec(geometry.positions[s.vertices[0]]);
		for (int k = 1; k < s.count; ++k)
			for (int a = 0; a < 3; ++a)
			{
				const double p = vec(geometry.positions[s.vertices[k]])[a];
				s.low[a] = std::min(s.low[a], p);
				s.high[a] = std::max(s.high[a], p);
			}
		index.samples.append(s);
	}
	if (index.samples.isEmpty())
		return fail(error, Text::tr("No source primitives lie within the chosen mirror side."));
	if (index.build(0, index.samples.size()) < 0)
		return false;
	auto &destination = mesh->surfaces[target];
	QSet<int> selected = selection.vertices;
	for (int f : selection.faces)
	{
		if (f < 0 || f >= destination.triangles.size())
			return fail(error, Text::tr("A destination face does not exist."));
		const auto &t = destination.triangles[f];
		selected.insert(t.a);
		selected.insert(t.b);
		selected.insert(t.c);
	}
	QSet<ModelEdge> edges;
	if (!selection.edges.isEmpty())
		for (const auto &edge : modelSurfaceEdges(destination, control))
		{
			if (!work.step())
				return false;
			edges.insert(edge);
		}
	for (const auto &e : selection.edges)
	{
		if (!edges.contains(modelEdge(e.first, e.second)))
			return fail(error, Text::tr("A destination edge does not exist."));
		selected.insert(e.first);
		selected.insert(e.second);
	}
	for (int v : selected)
		if (v < 0 || v >= destination.vertexCount)
			return fail(error, Text::tr("A destination vertex does not exist."));
	ModelSurfaceSkinning output;
	bool changed = false;
	int targets = 0, matched = 0;
	for (int v = 0; v < destination.vertexCount; ++v)
	{
		if (!work.step())
			return false;
		Vec p = vec(destination.frames[skin.bindFrame].positions[v]), normal = vec(destination.frames[skin.bindFrame].normals[v]);
		bool eligible = selected.isEmpty() || selected.contains(v);
		if (o.side != ModelWeightTransferSide::All)
			eligible &= sign * (p[o.mirrorAxis] - o.plane) < -o.centreTolerance;
		Hit hit;
		hit.limit = hit.distance = o.maximumDistance * o.maximumDistance;
		if (eligible)
		{
			++targets;
			if (o.mirrorAxis >= 0)
			{
				p[o.mirrorAxis] = 2 * o.plane - p[o.mirrorAxis];
				normal[o.mirrorAxis] = -normal[o.mirrorAxis];
			}
			if (!index.query(0, p, normal, std::cos(o.normalAngle * std::acos(-1.) / 180), o.normalAngle < 180, &hit))
				return false;
			if (hit.ambiguous)
				return fail(error, Text::tr("Vertex %1 has equally close source samples with different weights. Filter by normals or "
											"isolate the source surface.")
									   .arg(v));
			if (hit.sample < 0 && !o.skipUnmatched)
				return fail(error, Text::tr("Vertex %1 has no source match within the distance and normal limits.").arg(v));
		}
		if (hit.sample >= 0)
		{
			++matched;
			if (!assign(mesh, target, skin.bindFrame, v, hit.weights, o, locks, &output, &changed, error))
				return false;
		}
		else
		{
			if (destination.skinning.isEmpty())
				return fail(error, Text::tr("An unbound destination must receive a match for every vertex."));
			output.first.append(output.influences.size());
			output.count.append(destination.skinning.count[v]);
			for (int i = destination.skinning.first[v]; i < destination.skinning.first[v] + destination.skinning.count[v]; ++i)
				output.influences.append(destination.skinning.influences[i]);
		}
	}
	if (!targets || !matched)
		return fail(error, Text::tr("The transfer has no matched destination vertices."));
	if (changed)
	{
		destination.skinning = std::move(output);
		if (!skin.bakeSurface(mesh, target, error, control))
			return false;
		for (auto &clip : mesh->skeleton.clips)
		{
			clip.frameMins.clear();
			clip.frameMaxs.clear();
		}
	}
	return work.check();
}
} // namespace vibestudio
