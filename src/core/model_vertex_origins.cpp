#include "core/model_vertex_origins.h"

#include "core/model_document.h"
#include "core/model_geometry_helpers.h"
#include "core/model_skeleton.h"
#include "core/model_skinning_tools.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>

namespace vibestudio
{
namespace
{
using namespace model_geometry;

struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinProvenance)
};

bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}

ModelVec3 vec(P3 p)
{
	return {float(p.x), float(p.y), float(p.z)};
}

double distanceBetween(const ModelVec3 &a, const ModelVec3 &b)
{
	return length(point(a) - point(b));
}

bool completeSkinning(const ModelSurface &surface)
{
	return !surface.skinning.isEmpty() && surface.skinning.first.size() == surface.vertexCount &&
		   surface.skinning.count.size() == surface.vertexCount;
}

// A frame vertex's position and normal, bit for bit.
struct FrameVertexKey
{
	std::array<quint32, 6> bits{};
	bool operator==(const FrameVertexKey &) const = default;
};
size_t qHash(const FrameVertexKey &key, size_t seed = 0)
{
	return qHashRange(key.bits.cbegin(), key.bits.cend(), seed);
}
FrameVertexKey frameVertexKey(const ModelFrameGeometry &frame, int vertex)
{
	const auto &p = frame.positions[vertex], &n = frame.normals[vertex];
	return {{std::bit_cast<quint32>(p.x), std::bit_cast<quint32>(p.y), std::bit_cast<quint32>(p.z), std::bit_cast<quint32>(n.x),
		std::bit_cast<quint32>(n.y), std::bit_cast<quint32>(n.z)}};
}

// Joints, weights and offsets equal bit for bit.
bool sameInfluenceBits(const ModelSurfaceSkinning &a, int va, const ModelSurfaceSkinning &b, int vb)
{
	if (a.count[va] != b.count[vb])
		return false;
	const auto same = [](const ModelVec3 &x, const ModelVec3 &y)
	{ return std::bit_cast<quint32>(x.x) == std::bit_cast<quint32>(y.x) && std::bit_cast<quint32>(x.y) == std::bit_cast<quint32>(y.y) &&
			 std::bit_cast<quint32>(x.z) == std::bit_cast<quint32>(y.z); };
	for (int i = 0; i < a.count[va]; ++i)
	{
		const auto &x = a.influences[a.first[va] + i], &y = b.influences[b.first[vb] + i];
		if (x.joint != y.joint || std::bit_cast<quint32>(x.weight) != std::bit_cast<quint32>(y.weight) || !same(x.offset, y.offset) ||
			!same(x.normalOffset, y.normalOffset))
			return false;
	}
	return true;
}

QString rebuiltWithoutRecord(const QString &surface)
{
	return Text::tr("Surface %1 was rebuilt without a record of where its vertices came from, so its skin weights cannot be kept exactly. "
					"No change was made. Unbind the surface, edit it, then bind it again.")
		.arg(surface);
}

double jointCoordinate(const ModelSkeleton &skeleton, int joint, int axis)
{
	const auto origin = modelJointTranslation(skeleton.joints[joint].bind);
	return axis == 0 ? origin.x : axis == 1 ? origin.y : origin.z;
}

QString planesText(quint8 planes)
{
	QStringList names;
	for (int axis = 0; axis < 3; ++axis)
		if (planes & (1 << axis))
			names << QStringLiteral("%1 = 0").arg(QLatin1Char("XYZ"[axis]));
	return names.join(QStringLiteral(", "));
}

void appendOrigin(ModelSurfaceOrigins &origins, const QVector<ModelVertexOrigin> &sources, quint8 flags)
{
	origins.first.append(origins.sources.size());
	origins.count.append(sources.size());
	origins.sources += sources;
	origins.flags.append(flags);
}

// Appends `vertex`'s sources from `source` as a new vertex of `target`.
// `target` may be `source`; entries are copied before they are appended.
void appendOriginOf(ModelSurfaceOrigins &target, const ModelSurfaceOrigins &source, int vertex, quint8 flags)
{
	const int first = vertex >= 0 && vertex < source.first.size() ? source.first[vertex] : 0;
	const int count = vertex >= 0 && vertex < source.first.size() ? source.count[vertex] : 0;
	target.first.append(target.sources.size());
	target.count.append(count);
	for (int i = first; i < first + count; ++i)
	{
		const ModelVertexOrigin entry = source.sources[i];
		target.sources.append(entry);
	}
	target.flags.append(flags);
}

QVector<ModelVertexOrigin> originsOf(const ModelSurfaceOrigins &origins, int vertex)
{
	if (vertex < 0 || vertex >= origins.first.size())
		return {};
	return origins.sources.mid(origins.first[vertex], origins.count[vertex]);
}

// The weighted sum of the vertices' origins, merged by source.
QVector<ModelVertexOrigin> blendedOrigins(const ModelSurfaceOrigins &origins, const QVector<QPair<int, double>> &weights)
{
	QVector<ModelVertexOrigin> merged;
	for (const auto &[vertex, weight] : weights)
	{
		for (const auto &source : originsOf(origins, vertex))
		{
			const double part = source.weight * weight;
			const auto same = std::find_if(merged.begin(), merged.end(), [&](const ModelVertexOrigin &entry)
				{ return entry.surface == source.surface && entry.vertex == source.vertex && entry.mirror == source.mirror; });
			if (same != merged.end())
				same->weight += part;
			else
				merged.append({source.surface, source.vertex, part, source.mirror});
		}
	}
	merged.erase(std::remove_if(merged.begin(), merged.end(), [](const ModelVertexOrigin &entry) { return !(entry.weight > 0); }), merged.end());
	return merged;
}

// The pose of every frame that follows the skeleton (the bind pose or a
// retained clip frame), or null for frames edited independently (-2).
QVector<const QVector<ModelJointMatrix> *> retainedPoses(const ModelMesh &mesh, const QVector<ModelJointMatrix> &bind)
{
	const auto &skeleton = mesh.skeleton;
	QVector<const QVector<ModelJointMatrix> *> poses(mesh.frames.size(), nullptr);
	QVector<int> counts(skeleton.clips.size(), 0);
	for (int frame = 0; frame < mesh.frames.size(); ++frame)
	{
		const int clip = skeleton.bakedClipForFrame.value(frame, -2);
		if (clip == -1)
			poses[frame] = &bind;
		else if (clip >= 0 && clip < skeleton.clips.size())
		{
			const int local = counts[clip]++;
			if (local < skeleton.clips[clip].frames.size() && skeleton.clips[clip].frames[local].size() == skeleton.joints.size())
				poses[frame] = &skeleton.clips[clip].frames[local];
		}
	}
	return poses;
}

ModelVec3 skinnedBindPosition(const QVector<ModelJointInfluence> &influences, int first, int count, const QVector<ModelJointMatrix> &bind)
{
	P3 sum{0, 0, 0};
	for (int i = first; i < first + count; ++i)
	{
		const auto &influence = influences[i];
		if (influence.joint >= 0 && influence.joint < bind.size())
			sum = sum + point(modelJointTransformPoint(bind[influence.joint], influence.offset)) * double(influence.weight);
	}
	return vec(sum);
}

// Name transformations that may yield a joint's left/right partner, in order.
QStringList partnerCandidates(const QString &name)
{
	QStringList candidates;
	// Left and Right as words anywhere in the name, keeping their case style.
	for (const auto &[from, to] : {std::pair{QStringLiteral("left"), QStringLiteral("right")}, std::pair{QStringLiteral("right"), QStringLiteral("left")}})
	{
		const int at = name.indexOf(from, 0, Qt::CaseInsensitive);
		if (at < 0)
			continue;
		const auto found = name.mid(at, from.size());
		QString replacement = to;
		if (found == found.toUpper())
			replacement = to.toUpper();
		else if (found.at(0).isUpper())
			replacement[0] = replacement[0].toUpper();
		candidates << name.left(at) + replacement + name.mid(at + from.size());
	}
	// A lone L or R token between separators: "Bip01 L Thigh", "l_hand", "hand.L".
	const auto separator = [](QChar c) { return c == QLatin1Char(' ') || c == QLatin1Char('_') || c == QLatin1Char('.') || c == QLatin1Char('-'); };
	for (int at = 0; at < name.size(); ++at)
	{
		const QChar c = name.at(at);
		if (c.toLower() != QLatin1Char('l') && c.toLower() != QLatin1Char('r'))
			continue;
		const bool before = at == 0 || separator(name.at(at - 1)), after = at + 1 == name.size() || separator(name.at(at + 1));
		if (!before || !after || name.size() == 1)
			continue;
		QString swapped = name;
		const QChar other = c.toLower() == QLatin1Char('l') ? QLatin1Char('r') : QLatin1Char('l');
		swapped[at] = c.isUpper() ? other.toUpper() : other;
		candidates << swapped;
	}
	// Doom 3 style: a leading L or R before the rest of the name ("Lhand").
	if (name.size() > 1 && name.at(1).isLetter())
	{
		const QChar c = name.at(0);
		if (c == QLatin1Char('L') || c == QLatin1Char('R'))
			candidates << QString(c == QLatin1Char('L') ? QLatin1Char('R') : QLatin1Char('L')) + name.mid(1);
		else if ((c == QLatin1Char('l') || c == QLatin1Char('r')) && name.at(1).isUpper())
			candidates << QString(c == QLatin1Char('l') ? QLatin1Char('r') : QLatin1Char('l')) + name.mid(1);
	}
	return candidates;
}

QString partnerOf(const QString &name, const QSet<QString> &names)
{
	for (const auto &candidate : partnerCandidates(name))
		if (candidate != name && names.contains(candidate))
			return candidate;
	return {};
}

QString countsText(const ModelSkinProvenance &provenance, bool includeKept)
{
	QStringList parts;
	if (provenance.authored)
		parts << Text::tr("written exactly by the tool");
	if (includeKept && provenance.kept > 0)
		parts << Text::tr("%n vertices kept exactly", nullptr, provenance.kept);
	if (provenance.interpolated > 0)
		parts << Text::tr("%n vertices interpolated", nullptr, provenance.interpolated);
	if (provenance.mirrored > 0)
		parts << Text::tr("%n vertices mirrored through the joint map", nullptr, provenance.mirrored);
	if (provenance.rebound > 0)
		parts << Text::tr("%n moved vertices rebound to the skeleton", nullptr, provenance.rebound);
	if (provenance.merged > 0)
		parts << Text::tr("%n vertices merged with averaged weights", nullptr, provenance.merged);
	if (provenance.bound > 0)
		parts << Text::tr("%n new vertices bound to the chosen joints", nullptr, provenance.bound);
	if (provenance.widened > 0)
		parts << Text::tr("%n vertices with more joint influences than the surface had (up to %1, previously %2): Reduce and normalise weights "
						  "can limit the count",
					 nullptr, provenance.widened)
					 .arg(provenance.widest)
					 .arg(provenance.previousLimit);
	return parts.join(QStringLiteral("; "));
}

void addProvenance(ModelSkinProvenance *total, const ModelSkinProvenance &entry)
{
	total->kept += entry.kept;
	total->interpolated += entry.interpolated;
	total->mirrored += entry.mirrored;
	total->rebound += entry.rebound;
	total->merged += entry.merged;
	total->bound += entry.bound;
	total->widened += entry.widened;
	total->widest = std::max(total->widest, entry.widest);
	total->previousLimit = std::max(total->previousLimit, entry.previousLimit);
}

QString clipNameOf(const ModelMesh &mesh, int clip)
{
	return clip >= 0 && clip < mesh.skeleton.clips.size() ? mesh.skeleton.clips[clip].name : QString::number(clip);
}
} // namespace

void seedModelVertexOrigins(ModelMesh *mesh)
{
	if (!mesh)
		return;
	for (int s = 0; s < mesh->surfaces.size(); ++s)
	{
		auto &surface = mesh->surfaces[s];
		ModelSurfaceOrigins origins;
		origins.tracked = true;
		const int count = surface.vertexCount;
		origins.first.resize(count);
		origins.count.fill(1, count);
		origins.flags.fill(0, count);
		origins.sources.resize(count);
		for (int v = 0; v < count; ++v)
		{
			origins.first[v] = v;
			origins.sources[v] = {s, v, 1.0, 0};
		}
		surface.editOrigins = std::move(origins);
	}
}

void clearModelVertexOrigins(ModelMesh *mesh)
{
	if (!mesh)
		return;
	for (auto &surface : mesh->surfaces)
		surface.editOrigins = {};
}

void markModelSurfaceOriginsAuthored(ModelSurface *surface)
{
	if (!surface)
		return;
	surface->editOrigins = {};
	surface->editOrigins.authored = true;
}

void appendModelVertexOriginCopy(ModelSurface *surface, int source)
{
	auto &origins = surface->editOrigins;
	if (!origins.tracked)
		return;
	const quint8 flags = quint8(origins.flags.value(source, 0) | ModelVertexOriginCreated);
	appendOriginOf(origins, origins, source, flags);
}

void appendModelVertexOriginBlend(ModelSurface *surface, const QVector<QPair<int, double>> &weights)
{
	auto &origins = surface->editOrigins;
	if (!origins.tracked)
		return;
	appendOrigin(origins, blendedOrigins(origins, weights), ModelVertexOriginCreated);
}

void mergeModelVertexOrigins(ModelSurface *surface, const QVector<QPair<int, double>> &members, const QVector<int> &targets)
{
	auto &origins = surface->editOrigins;
	if (!origins.tracked || targets.isEmpty())
		return;
	const auto merged = blendedOrigins(origins, members);
	const int first = origins.sources.size();
	origins.sources += merged;
	for (int target : targets)
	{
		if (target < 0 || target >= origins.first.size())
			continue;
		// The entries live once at the end; every target shares them.
		origins.first[target] = first;
		origins.count[target] = merged.size();
		origins.flags[target] |= ModelVertexOriginMerged | ModelVertexOriginRepose;
	}
}

void startModelSurfaceOrigins(ModelSurface *surface)
{
	surface->editOrigins = {};
	surface->editOrigins.tracked = true;
}

bool sameModelSkinWeights(const ModelSurface &surface, int a, int b, double tolerance)
{
	if (!completeSkinning(surface) || a < 0 || b < 0 || a >= surface.vertexCount || b >= surface.vertexCount)
		return true;
	const auto weightsOf = [&](int vertex)
	{
		QMap<int, double> weights;
		const auto &skinning = surface.skinning;
		for (int i = skinning.first[vertex]; i < skinning.first[vertex] + skinning.count[vertex]; ++i)
			weights[skinning.influences[i].joint] += skinning.influences[i].weight;
		return weights;
	};
	const auto x = weightsOf(a), y = weightsOf(b);
	if (x.keys() != y.keys())
		return false;
	for (auto i = x.cbegin(); i != x.cend(); ++i)
		if (std::abs(i.value() - y.value(i.key())) > tolerance)
			return false;
	return true;
}

void appendModelVertexOriginNone(ModelSurface *surface)
{
	auto &origins = surface->editOrigins;
	if (origins.tracked)
		appendOrigin(origins, {}, ModelVertexOriginCreated);
}

void mirrorModelVertexOrigin(ModelSurface *surface, int vertex, int axis)
{
	auto &origins = surface->editOrigins;
	if (!origins.tracked || vertex < 0 || vertex >= origins.first.size() || axis < 0 || axis > 2)
		return;
	for (int i = origins.first[vertex]; i < origins.first[vertex] + origins.count[vertex]; ++i)
		origins.sources[i].mirror ^= quint8(1 << axis);
	origins.flags[vertex] |= ModelVertexOriginCreated;
}

void markModelVertexOriginRepose(ModelSurface *surface, int vertex)
{
	auto &origins = surface->editOrigins;
	if (origins.tracked && vertex >= 0 && vertex < origins.flags.size())
		origins.flags[vertex] |= ModelVertexOriginRepose;
}

void markModelVertexOriginNormals(ModelSurface *surface, int vertex)
{
	auto &origins = surface->editOrigins;
	if (origins.tracked && vertex >= 0 && vertex < origins.flags.size())
		origins.flags[vertex] |= ModelVertexOriginNormals;
}

void rebuildModelVertexOrigins(ModelSurface *surface, const ModelSurfaceOrigins &previous, const QVector<int> &sourceOf)
{
	if (!previous.tracked)
	{
		surface->editOrigins = {};
		return;
	}
	ModelSurfaceOrigins result;
	result.tracked = true;
	result.first.reserve(sourceOf.size());
	result.count.reserve(sourceOf.size());
	result.flags.reserve(sourceOf.size());
	QVector<bool> used(previous.first.size(), false);
	for (int old : sourceOf)
	{
		if (old < 0 || old >= previous.first.size())
		{
			appendOrigin(result, {}, ModelVertexOriginCreated);
			continue;
		}
		appendOriginOf(result, previous, old, quint8(previous.flags[old] | (used[old] ? ModelVertexOriginCreated : 0)));
		used[old] = true;
	}
	surface->editOrigins = std::move(result);
}

void appendModelSurfaceOrigins(ModelSurface *target, const ModelSurfaceOrigins &source)
{
	auto &origins = target->editOrigins;
	if (!origins.tracked && !source.tracked)
		return;
	if (!origins.tracked || !source.tracked)
	{
		// Half-described vertex data cannot be trusted: reconciliation refuses
		// it on a skinned surface.
		origins = {};
		return;
	}
	const int offset = origins.sources.size();
	for (int v = 0; v < source.first.size(); ++v)
	{
		origins.first.append(source.first[v] + offset);
		origins.count.append(source.count[v]);
	}
	origins.sources += source.sources;
	origins.flags += source.flags;
}

ModelSkinBlendStatus blendModelSkinInfluences(const QVector<ModelSkinBlendSource> &sources, QVector<ModelJointInfluence> *out,
	ModelWorkProgress *work)
{
	struct Influence
	{
		double weight = 0;
		P3 offset{0, 0, 0}, normal{0, 0, 0};
	};
	QMap<int, Influence> combined;
	for (const auto &source : sources)
	{
		const auto &skinning = *source.skinning;
		const int first = skinning.first[source.vertex], count = skinning.count[source.vertex];
		for (int i = first; i < first + count; ++i)
		{
			if (work && !work->step())
				return ModelSkinBlendStatus::Cancelled;
			const auto &influence = skinning.influences[i];
			if (influence.weight < 0)
				return ModelSkinBlendStatus::NegativeWeight;
			const double weight = influence.weight * source.weight;
			if (weight <= 0)
				continue;
			const int joint = source.joints ? source.joints->value(influence.joint, -1) : influence.joint;
			auto &target = combined[joint];
			target.weight += weight;
			target.offset = target.offset + point(influence.offset) * weight;
			target.normal = target.normal + point(influence.normalOffset) * weight;
		}
	}
	if (combined.isEmpty() || combined.size() > 64)
		return ModelSkinBlendStatus::InfluenceCount;
	out->clear();
	out->reserve(combined.size());
	for (auto i = combined.cbegin(); i != combined.cend(); ++i)
		out->append({i.key(), float(i->weight), vec(i->offset * (1 / i->weight)), vec(i->normal * (1 / i->weight))});
	return ModelSkinBlendStatus::Blended;
}

QMap<QString, QString> detectModelMirrorJointPairs(const ModelSkeleton &skeleton)
{
	QSet<QString> names;
	for (const auto &joint : skeleton.joints)
		names.insert(joint.name);
	QMap<QString, QString> pairs;
	for (const auto &joint : skeleton.joints)
	{
		const auto partner = partnerOf(joint.name, names);
		// Only mutual pairs: each name must lead back to the other.
		if (!partner.isEmpty() && partnerOf(partner, names) == joint.name)
			pairs.insert(joint.name, partner);
	}
	return pairs;
}

QString modelMirrorJointMapText(const QMap<QString, QString> &map)
{
	QStringList pairs;
	QSet<QString> written;
	for (auto i = map.cbegin(); i != map.cend(); ++i)
	{
		if (written.contains(i.key()))
			continue;
		written << i.key() << i.value();
		pairs << QStringLiteral("%1=%2").arg(i.key(), i.value());
	}
	return pairs.join(QStringLiteral(", "));
}

bool parseModelMirrorJointMap(const QString &text, QMap<QString, QString> *map, QString *error)
{
	QMap<QString, QString> result;
	for (const auto &token : text.split(QLatin1Char(',')))
	{
		const auto parts = token.split(QLatin1Char('='));
		const auto a = parts.value(0).trimmed(), b = parts.value(1).trimmed();
		if (parts.size() != 2 || a.isEmpty() || b.isEmpty())
			return fail(error, Text::tr("Write joint mirror pairs as left=right, separated by commas."));
		for (const auto &[from, to] : {std::pair{a, b}, std::pair{b, a}})
		{
			if (result.contains(from) && result.value(from) != to)
				return fail(error, Text::tr("Joint %1 cannot mirror to both %2 and %3.").arg(from, result.value(from), to));
			result.insert(from, to);
		}
	}
	*map = std::move(result);
	return true;
}

QStringList modelOffCentreJoints(const ModelMesh &mesh, int surface, int axis, double tolerance)
{
	QStringList names;
	if (surface < 0 || surface >= mesh.surfaces.size() || axis < 0 || axis > 2)
		return names;
	const auto &skeleton = mesh.skeleton;
	QVector<bool> used(skeleton.joints.size(), false);
	for (const auto &influence : mesh.surfaces[surface].skinning.influences)
		if (influence.joint >= 0 && influence.joint < used.size() && influence.weight > 0)
			used[influence.joint] = true;
	for (int joint = 0; joint < skeleton.joints.size(); ++joint)
		if (used[joint] && std::abs(jointCoordinate(skeleton, joint, axis)) > tolerance)
			names << skeleton.joints[joint].name;
	return names;
}

bool modelJointsMirrorAcross(const ModelSkeleton &skeleton, int a, int b, quint8 planes, double tolerance)
{
	if (a < 0 || b < 0 || a >= skeleton.joints.size() || b >= skeleton.joints.size())
		return false;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (!(planes & (1 << axis)))
			continue;
		const double ca = jointCoordinate(skeleton, a, axis), cb = jointCoordinate(skeleton, b, axis);
		const bool offA = std::abs(ca) > tolerance, offB = std::abs(cb) > tolerance;
		if (offA != offB || (offA && (ca > 0) == (cb > 0)))
			return false;
	}
	return true;
}

QMap<QString, QString> modelMirrorJointPairsForAxis(const ModelMesh &mesh, const QMap<QString, QString> &map, int axis, int surface,
	double tolerance)
{
	QMap<QString, QString> pairs;
	if (axis < 0 || axis > 2)
		return pairs;
	const auto &skeleton = mesh.skeleton;
	QSet<int> used;
	if (surface >= 0 && surface < mesh.surfaces.size())
		for (const auto &influence : mesh.surfaces[surface].skinning.influences)
			if (influence.weight > 0)
				used.insert(influence.joint);
	for (auto i = map.cbegin(); i != map.cend(); ++i)
	{
		const int a = modelJointIndex(skeleton, i.key(), Qt::CaseSensitive), b = modelJointIndex(skeleton, i.value(), Qt::CaseSensitive);
		if (a < 0 || b < 0 || a == b || std::abs(jointCoordinate(skeleton, a, axis)) <= tolerance ||
			!modelJointsMirrorAcross(skeleton, a, b, quint8(1 << axis), tolerance))
			continue;
		if (surface >= 0 && !used.contains(a) && !used.contains(b))
			continue;
		pairs.insert(i.key(), i.value());
	}
	return pairs;
}

bool completeModelMirrorJointMap(const ModelSkeleton &skeleton, const QMap<QString, QString> &map, QMap<QString, QString> *complete,
	QString *error)
{
	QMap<QString, QString> result = map;
	for (auto i = map.cbegin(); i != map.cend(); ++i)
	{
		for (const auto &name : {i.key(), i.value()})
			if (modelJointIndex(skeleton, name, Qt::CaseSensitive) < 0)
				return fail(error, Text::tr("The joint mirror map names %1, which the skeleton does not have.").arg(name));
		if (result.contains(i.value()) && result.value(i.value()) != i.key())
			return fail(error, Text::tr("Joint %1 cannot mirror to both %2 and %3.").arg(i.value(), result.value(i.value()), i.key()));
		result.insert(i.value(), i.key());
	}
	*complete = std::move(result);
	return true;
}

ModelEditSkinHandling modelEditSkinHandling(ModelEditKind kind)
{
	// No default: a new edit kind must be classified here, and the provenance
	// test checks every kind.
	switch (kind)
	{
	case ModelEditKind::AddJoint:
	case ModelEditKind::RenameJoint:
	case ModelEditKind::ReparentJoint:
	case ModelEditKind::DeleteJoint:
	case ModelEditKind::SetJointWeight:
	case ModelEditKind::TransformBindJoint:
	case ModelEditKind::AddSkeletalClip:
	case ModelEditKind::DuplicateSkeletalClip:
	case ModelEditKind::RenameSkeletalClip:
	case ModelEditKind::DeleteSkeletalClip:
	case ModelEditKind::ResizeSkeletalClip:
	case ModelEditKind::SetSkeletalClipTiming:
	case ModelEditKind::TransformSkeletalPose:
	case ModelEditKind::CopySkeletalPose:
	case ModelEditKind::InterpolateSkeletalPoses:
	case ModelEditKind::BindSkin:
	case ModelEditKind::RebindSkin:
	case ModelEditKind::UnbindSkin:
	case ModelEditKind::PruneSkinWeights:
	case ModelEditKind::PaintSkinWeights:
	case ModelEditKind::TransferSkinWeights:
	case ModelEditKind::SolveTwoBoneIk:
	case ModelEditKind::EditAnimationCurves:
	case ModelEditKind::EditCurveKeys:
	case ModelEditKind::TransferAnimationCurves:
	case ModelEditKind::SetSkeletalWorkingSet:
		return ModelEditSkinHandling::Direct;
	case ModelEditKind::BevelEdges:
	case ModelEditKind::Knife:
	case ModelEditKind::Spin:
	case ModelEditKind::Rip:
		return ModelEditSkinHandling::Authored;
	case ModelEditKind::AddPrimitive:
		return ModelEditSkinHandling::Binding;
	case ModelEditKind::DuplicateFrame:
	case ModelEditKind::DeleteFrame:
	case ModelEditKind::InsertInbetweens:
	case ModelEditKind::CopyFramePose:
		return ModelEditSkinHandling::Frames;
	case ModelEditKind::Transform:
	case ModelEditKind::Extrude:
	case ModelEditKind::Subdivide:
	case ModelEditKind::DuplicateFaces:
	case ModelEditKind::DeleteFaces:
	case ModelEditKind::FlipFaces:
	case ModelEditKind::RecalculateNormals:
	case ModelEditKind::TransformUv:
	case ModelEditKind::ProjectUv:
	case ModelEditKind::SetMaterial:
	case ModelEditKind::RenameFrame:
	case ModelEditKind::SplitEdges:
	case ModelEditKind::WeldVertices:
	case ModelEditKind::MarkUvSeams:
	case ModelEditKind::ClearUvSeams:
	case ModelEditKind::DetachUv:
	case ModelEditKind::SetMd2SkinSize:
	case ModelEditKind::AddTag:
	case ModelEditKind::DuplicateTag:
	case ModelEditKind::RenameTag:
	case ModelEditKind::DeleteTag:
	case ModelEditKind::SetTagOrigin:
	case ModelEditKind::ResetTagOrientation:
	case ModelEditKind::TransformTag:
	case ModelEditKind::CopyTagPose:
	case ModelEditKind::UnwrapUv:
	case ModelEditKind::PackUv:
	case ModelEditKind::RemoveDuplicateFaces:
	case ModelEditKind::RemoveUnusedVertices:
	case ModelEditKind::SplitDisconnectedFans:
	case ModelEditKind::OrientFaces:
	case ModelEditKind::AddAnimation:
	case ModelEditKind::RenameAnimation:
	case ModelEditKind::SetAnimationRange:
	case ModelEditKind::DeleteAnimation:
	case ModelEditKind::AddMdlSkin:
	case ModelEditKind::ReplaceMdlSkinMember:
	case ModelEditKind::AppendMdlSkinMember:
	case ModelEditKind::RemoveMdlSkin:
	case ModelEditKind::RemoveMdlSkinMember:
	case ModelEditKind::SetMdlSkinDuration:
	case ModelEditKind::SetMdlHeader:
	case ModelEditKind::SetMdlPalette:
	case ModelEditKind::GroupMdlFrames:
	case ModelEditKind::UngroupMdlFrames:
	case ModelEditKind::SetMdlFrameDuration:
	case ModelEditKind::AddCollisionBox:
	case ModelEditKind::FitCollisionBox:
	case ModelEditKind::UpdateCollisionBox:
	case ModelEditKind::DuplicateCollisionBox:
	case ModelEditKind::DeleteCollisionBox:
	case ModelEditKind::TransformCollisionBox:
	case ModelEditKind::ApplySkinBindings:
	case ModelEditKind::FillBoundaryLoops:
	case ModelEditKind::RenameSurface:
	case ModelEditKind::SeparateFaces:
	case ModelEditKind::MoveFacesToSurface:
	case ModelEditKind::DuplicateSurface:
	case ModelEditKind::DeleteSurface:
	case ModelEditKind::JoinSurfaces:
	case ModelEditKind::SetMaterialSlots:
	case ModelEditKind::SetAnimationRate:
	case ModelEditKind::BridgeBoundaryLoops:
	case ModelEditKind::SplitNonmanifoldEdges:
	case ModelEditKind::PackUvAround:
	case ModelEditKind::AnimateCollisionBox:
	case ModelEditKind::FreezeCollisionBox:
	case ModelEditKind::FitAnimatedCollisionBox:
	case ModelEditKind::RotateEdges:
	case ModelEditKind::MergeVertices:
	case ModelEditKind::DissolveVertices:
	case ModelEditKind::DissolveFaces:
	case ModelEditKind::PokeFaces:
	case ModelEditKind::BeautifyFaces:
	case ModelEditKind::MakeFace:
	case ModelEditKind::ExtrudeEdges:
	case ModelEditKind::InsetFaces:
	case ModelEditKind::ShrinkFatten:
	case ModelEditKind::SmoothVertices:
	case ModelEditKind::WeightedTransform:
	case ModelEditKind::ShadeFlat:
	case ModelEditKind::ShadeSmooth:
	case ModelEditKind::ShadeAutoSmooth:
	case ModelEditKind::Bisect:
	case ModelEditKind::Symmetrize:
	case ModelEditKind::LoopCut:
	case ModelEditKind::Decimate:
	case ModelEditKind::BevelVertices:
	case ModelEditKind::Solidify:
	case ModelEditKind::ProjectUvMapping:
	case ModelEditKind::SlideEdges:
	case ModelEditKind::SetUvDensity:
		return ModelEditSkinHandling::Provenance;
	}
	return ModelEditSkinHandling::Provenance;
}

QString modelSkeletalFrameEditRefusal(const ModelMesh &mesh, ModelEditKind kind, int frame)
{
	const auto &map = mesh.skeleton.bakedClipForFrame;
	if (mesh.skeleton.isEmpty() || map.size() != mesh.frames.size())
		return {};
	// Once every surface is unbound, nothing follows the skeleton and the
	// frames are ordinary vertex animation.
	if (std::none_of(mesh.surfaces.cbegin(), mesh.surfaces.cend(), completeSkinning))
		return {};
	// The bind frame (-1) and clip frames (>= 0) follow the skeleton.
	const auto retained = [&](int f) { return f >= 0 && f < map.size() && map[f] >= -1; };
	const auto role = [&](int f)
	{
		return map[f] == -1 ? Text::tr("Frame %1 is the skeleton's bind pose.").arg(f)
							: Text::tr("Frame %1 is baked from skeletal clip %2.").arg(f).arg(clipNameOf(mesh, map[f]));
	};
	switch (kind)
	{
	case ModelEditKind::DuplicateFrame:
		if (retained(frame))
			return role(frame) + QLatin1Char(' ') +
				   Text::tr("A copy would not follow the skeleton and would block skeletal animation editing. Use Skeleton > Edit Skeletal "
							"Animation (model pose --operation resize or copy on the command line) to resize the clip or copy its poses "
							"instead.");
		break;
	case ModelEditKind::DeleteFrame:
		if (retained(frame))
			return role(frame) + QLatin1Char(' ') +
				   (map[frame] == -1 ? Text::tr("Skin weights and skeletal clips depend on it, so it cannot be deleted. Unbind every surface "
												"first (model skinning --operation unbind on the command line) to edit the frames freely.")
									 : Text::tr("Use Skeleton > Edit Skeletal Animation (model pose --operation resize or remove on the "
												"command line) to resize or delete the clip instead."));
		break;
	case ModelEditKind::InsertInbetweens:
		if (retained(frame) && retained(frame + 1))
			return Text::tr("Frames %1 and %2 both follow the skeleton, and in-between frames would not. Use Skeleton > Edit Skeletal "
							"Animation (model pose --operation resize, then interpolate, on the command line) to resize the clip and "
							"interpolate its poses instead.")
				.arg(frame)
				.arg(frame + 1);
		break;
	case ModelEditKind::CopyFramePose:
		if (retained(frame))
			return role(frame) + QLatin1Char(' ') +
				   (map[frame] == -1 ? Text::tr("Copying a pose into it would change the bind geometry. Move bind joints in Skeleton > "
												"Edit Skeletal Animation (model pose --operation bind on the command line) instead.")
									 : Text::tr("Copying a pose into it would detach it from the skeleton. Use Copy whole pose in "
												"Skeleton > Edit Skeletal Animation (model pose --operation copy on the command line) "
												"instead."));
		break;
	default:
		break;
	}
	return {};
}

QVector<int> modelSkeletalFrameOrigins(const ModelMesh &mesh, ModelEditKind kind, int frame, int inserted)
{
	QVector<int> origins;
	const int count = mesh.frames.size();
	if (frame < 0 || frame >= count)
		return origins;
	switch (kind)
	{
	case ModelEditKind::DuplicateFrame:
	case ModelEditKind::InsertInbetweens:
		for (int f = 0; f <= frame; ++f)
			origins.append(f);
		for (int n = 0; n < (kind == ModelEditKind::DuplicateFrame ? 1 : inserted); ++n)
			origins.append(-1);
		for (int f = frame + 1; f < count; ++f)
			origins.append(f);
		break;
	case ModelEditKind::DeleteFrame:
		for (int f = 0; f < count; ++f)
			if (f != frame)
				origins.append(f);
		break;
	case ModelEditKind::CopyFramePose:
		// The destination holds another pose's geometry: a new frame.
		for (int f = 0; f < count; ++f)
			origins.append(f == frame ? -1 : f);
		break;
	default:
		break;
	}
	return origins;
}

bool reconcileModelSkinningFromOrigins(const ModelMesh &before, ModelMesh *after, const ModelSkinReconcileOptions &options,
	QMap<int, ModelSkinProvenance> *report, QString *error, const ModelWorkControl &control)
{
	if (report)
		report->clear();
	if (!after)
		return true;
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (after->skeleton.isEmpty())
	{
		clearModelVertexOrigins(after);
		return work.check();
	}
	const bool resized = after->skeleton.bakedClipForFrame.size() != after->frames.size();
	if (resized || !options.frameOrigins.isEmpty())
	{
		const auto &previous = before.skeleton.bakedClipForFrame;
		QVector<int> map(after->frames.size(), -2);
		if (previous.size() == before.frames.size() && !previous.isEmpty())
		{
			// Frames the edit kept keep their clip mapping; new and overwritten
			// frames are independent edits.
			if (options.frameOrigins.size() != after->frames.size())
				return fail(error, Text::tr("The edit changed the frame list without recording where each frame came from, so skeletal clip "
											"frames cannot be kept. No change was made."));
			for (int index = 0; index < map.size(); ++index)
				if (options.frameOrigins[index] >= 0)
					map[index] = previous.value(options.frameOrigins[index], -2);
			// A clip baked into frames is posed in frame order. One that lost a
			// frame (deleted, or overwritten by Copy Full Pose, which only an
			// unbound model allows) no longer matches its poses, so its other
			// frames become independent too.
			QHash<int, int> was, now;
			for (int clip : previous)
				if (clip >= 0)
					++was[clip];
			for (int clip : std::as_const(map))
				if (clip >= 0)
					++now[clip];
			for (int &clip : map)
				if (clip >= 0 && now.value(clip) < was.value(clip))
					clip = -2;
			after->skeleton.bakedClipForFrame = map;
		}
		else if (resized)
		{
			// No retained mapping to keep: only a frame named bindpose is known.
			for (int index = 0; index < after->frames.size(); ++index)
				if (after->frames.at(index).name == QLatin1String("bindpose"))
					map[index] = -1;
			after->skeleton.bakedClipForFrame = map;
		}
	}
	const auto &skeleton = after->skeleton;
	const auto bind = modelSkeletonBindPose(skeleton);
	const auto inverse = modelSkeletonInverseBindPose(skeleton);
	const auto poses = retainedPoses(*after, bind);
	const int beforeBind = modelBindPoseFrame(before), afterBind = modelBindPoseFrame(*after);
	const int legacyBefore = std::max(0, beforeBind), legacyAfter = std::max(0, afterBind);
	int bindJoint = -1;
	if (options.newGeometry == ModelNewGeometryBinding::Joint && (bindJoint = modelJointIndex(skeleton, options.bindJoint, Qt::CaseSensitive)) < 0)
		return fail(error, Text::tr("The skeleton has no joint named %1 to bind the new geometry to.").arg(options.bindJoint));
	QVector<int> allJoints(skeleton.joints.size());
	std::iota(allJoints.begin(), allJoints.end(), 0);
	const auto noBindFrame = [&](const QString &name)
	{
		return fail(error, Text::tr("Surface %1 needs a retained bind frame to rebind moved, mirrored, merged or new vertices. Bake the "
									"skeleton's bind pose into a frame first. No change was made.")
							   .arg(name));
	};
	QMap<QString, QString> mirrorNames;
	bool mirrorNamesReady = false;
	// Per mirror plane set: each joint's partner, -1 when it has none, or
	// -2 - partner when the map names a partner on the wrong side.
	QHash<int, QVector<int>> mirrorJoints;
	const auto mirrorMap = [&](quint8 planes) -> const QVector<int> *
	{
		if (!mirrorNamesReady)
		{
			if (!completeModelMirrorJointMap(skeleton, options.mirrorJoints, &mirrorNames, error))
				return nullptr;
			mirrorNamesReady = true;
		}
		auto found = mirrorJoints.find(planes);
		if (found == mirrorJoints.end())
		{
			QVector<int> map(skeleton.joints.size(), -1);
			for (int joint = 0; joint < skeleton.joints.size(); ++joint)
			{
				// A joint on the mirror plane keeps its name, whatever the map
				// says: across Y, arm.L stays arm.L.
				if (modelJointsMirrorAcross(skeleton, joint, joint, planes, options.centreTolerance))
				{
					map[joint] = joint;
					continue;
				}
				const auto &name = skeleton.joints[joint].name;
				if (mirrorNames.contains(name))
				{
					const int partner = modelJointIndex(skeleton, mirrorNames.value(name), Qt::CaseSensitive);
					map[joint] = modelJointsMirrorAcross(skeleton, joint, partner, planes, options.centreTolerance) ? partner : -2 - partner;
					continue;
				}
				if (options.keepJointNames)
					map[joint] = joint;
			}
			found = mirrorJoints.insert(planes, map);
		}
		return &found.value();
	};
	// The largest influence count of a vertex on each surface before the edit.
	QVector<int> beforeLimits(before.surfaces.size(), -1);
	const auto beforeLimit = [&](int s)
	{
		if (beforeLimits[s] < 0)
		{
			beforeLimits[s] = 0;
			for (int count : before.surfaces[s].skinning.count)
				beforeLimits[s] = std::max(beforeLimits[s], count);
		}
		return beforeLimits[s];
	};
	for (int index = 0; index < after->surfaces.size(); ++index)
	{
		if (!work.step())
			return false;
		auto &surface = after->surfaces[index];
		const ModelSurfaceOrigins origins = std::move(surface.editOrigins);
		surface.editOrigins = {};
		const int vertexCount = surface.vertexCount;
		ModelSkinProvenance provenance;
		const bool described = origins.tracked && origins.first.size() == vertexCount && origins.count.size() == vertexCount &&
							   origins.flags.size() == vertexCount;
		const ModelSurface *previous = index < before.surfaces.size() ? &before.surfaces[index] : nullptr;
		if (origins.authored)
		{
			// Edge bevel, knife, spin and rip wrote the influences themselves.
			if (!completeSkinning(surface))
			{
				if (!surface.skinning.isEmpty() || (previous && completeSkinning(*previous)))
					return fail(error, rebuiltWithoutRecord(surface.name));
				surface.skinning = {};
				continue;
			}
			// The tool's influences stand. Where its normals in a retained
			// skeletal pose differ from the skin's (edge bevel rebuilds band
			// normals from the faces), the skin's normal is used, so skin
			// authoring sees no independent normals afterwards. Vertices the
			// tool left exactly as they were keep their normals, including
			// normals edited by hand in one frame.
			const bool comparable = previous && completeSkinning(*previous);
			for (int frame = 0; frame < surface.frames.size() && frame < poses.size(); ++frame)
			{
				if (!work.step())
					return false;
				if (!poses[frame])
					continue;
				ModelFrameGeometry skinned;
				QString problem;
				if (!skinModelSurface(surface, *poses[frame], &skinned, &problem))
					return fail(error, problem);
				auto &geometry = surface.frames[frame];
				QHash<FrameVertexKey, QVector<int>> unchanged;
				bool indexed = false;
				for (int vertex = 0; vertex < vertexCount; ++vertex)
				{
					if (!work.step())
						return false;
					const auto &normal = geometry.normals[vertex], &skin = skinned.normals[vertex];
					if (distanceBetween(geometry.positions[vertex], skinned.positions[vertex]) > 1e-3 ||
						(std::abs(normal.x - skin.x) <= 1e-4 && std::abs(normal.y - skin.y) <= 1e-4 && std::abs(normal.z - skin.z) <= 1e-4))
						continue;
					if (comparable && frame < previous->frames.size())
					{
						if (!indexed)
						{
							for (int v = 0; v < previous->vertexCount; ++v)
							{
								if (!work.step())
									return false;
								unchanged[frameVertexKey(previous->frames[frame], v)].append(v);
							}
							indexed = true;
						}
						const auto found = unchanged.constFind(frameVertexKey(geometry, vertex));
						if (found != unchanged.cend() && std::any_of(found->cbegin(), found->cend(), [&](int v)
															 { return sameInfluenceBits(surface.skinning, vertex, previous->skinning, v); }))
							continue;
					}
					geometry.normals[vertex] = skin;
				}
			}
			provenance.authored = true;
			if (report)
				report->insert(index, provenance);
			continue;
		}
		if (!described)
		{
			// No provenance on a skinned surface: nothing is guessed.
			if (!surface.skinning.isEmpty() || (previous && completeSkinning(*previous)))
				return fail(error, rebuiltWithoutRecord(surface.name));
			surface.skinning = {};
			continue;
		}
		// Which original surfaces the vertices come from, and whether they
		// follow the skeleton.
		int skinnedVertices = 0, plainSurface = -1, newVertices = 0;
		for (int vertex = 0; vertex < vertexCount; ++vertex)
		{
			if (!work.step())
				return false;
			bool skinned = false;
			newVertices += origins.count[vertex] == 0;
			for (int i = origins.first[vertex]; i < origins.first[vertex] + origins.count[vertex]; ++i)
			{
				const auto &source = origins.sources[i];
				if (source.surface < 0 || source.surface >= before.surfaces.size() || source.vertex < 0 ||
					source.vertex >= before.surfaces[source.surface].vertexCount)
					return fail(error, Text::tr("Surface %1 lost track of where its vertices came from. No change was made.").arg(surface.name));
				if (completeSkinning(before.surfaces[source.surface]))
					skinned = true;
				else
					plainSurface = source.surface;
			}
			skinnedVertices += skinned;
		}
		const bool bindNew = newVertices > 0 && options.newGeometry != ModelNewGeometryBinding::None;
		if (newVertices > 0 && !bindNew && skinnedVertices > 0)
			return fail(error, Text::tr("Surface %1 follows the skeleton, but %n new vertices have no vertex to take weights from. Bind them "
										"to a joint or to the nearest bones, or add them as a new surface (--bind-joint, --bind nearest-bones "
										"or --new-surface on the command line). No change was made.",
								   nullptr, newVertices)
								   .arg(surface.name));
		if (bindNew && plainSurface >= 0)
			return fail(error, Text::tr("Surface %1 does not follow the skeleton, so new geometry on it cannot be bound to joints. Add it "
										"without a binding or as a new surface, or bind %1 first. No change was made.")
								   .arg(surface.name));
		if (skinnedVertices == 0 && !bindNew)
		{
			// Geometry from surfaces that never followed the skeleton.
			surface.skinning = {};
			continue;
		}
		if (plainSurface >= 0)
			return fail(error, Text::tr("Surface %1 would combine vertices that follow the skeleton with vertices from %2, which does not. "
										"Bind %2 to the skeleton first, or keep the surfaces separate.")
								   .arg(surface.name, before.surfaces[plainSurface].name));
		const bool canRebind = afterBind >= 0 && afterBind < surface.frames.size();
		if (bindNew && !canRebind)
			return noBindFrame(surface.name);
		// Without a bind frame, movement is measured in the first frame.
		const int movedAfter = canRebind ? afterBind : legacyAfter, movedBefore = beforeBind >= 0 ? beforeBind : legacyBefore;
		ModelSurfaceSkinning skinning;
		skinning.first.reserve(vertexCount);
		skinning.count.reserve(vertexCount);
		// How each vertex is posed from the skeleton in the retained skeletal
		// poses after its influences are derived.
		enum : quint8
		{
			// Always, except in the bind frame the influences were bound to:
			// the tool's own poses cannot follow the skeleton.
			PoseAll = 1,
			// Where the tool's position already agrees with the skeleton, which
			// removes rounding and makes the normal follow the joints.
			PoseAgreeing = 2,
			// Only the normal, where the position agrees.
			PoseNormals = 4,
		};
		QVector<quint8> pose(vertexCount, 0);
		QSet<QString> unmapped, wrongSide;
		quint8 wrongPlanes = 0;
		const auto rebind = [&](int vertex)
		{
			const auto &frame = surface.frames[afterBind];
			rebindModelSkinVertex(&skinning, vertex, inverse, frame.positions[vertex], frame.normals[vertex]);
		};
		// A recomputed normal is bound in the bind pose; the offsets stay.
		const auto rebindNormal = [&](int vertex)
		{
			const auto normal = surface.frames[afterBind].normals[vertex];
			for (int i = skinning.first[vertex]; i < skinning.first[vertex] + skinning.count[vertex]; ++i)
			{
				auto &influence = skinning.influences[i];
				if (influence.joint >= 0 && influence.joint < inverse.size())
					influence.normalOffset = modelJointTransformVector(inverse[influence.joint], normal);
			}
		};
		for (int vertex = 0; vertex < vertexCount; ++vertex)
		{
			if (!work.step())
				return false;
			const int first = origins.first[vertex], count = origins.count[vertex];
			const quint8 flags = origins.flags[vertex];
			skinning.first.append(skinning.influences.size());
			if (count == 0)
			{
				// New geometry follows the edit's binding, bound where the tool
				// put it in the bind frame.
				const auto &frame = surface.frames[afterBind];
				QMap<int, double> weights;
				if (bindJoint >= 0)
					weights.insert(bindJoint, 1);
				else if (!modelSkinBindingWeights(skeleton, bind, allJoints, ModelSkinBindingMethod::NearestBones, 4, 2, frame.positions[vertex],
							 &weights, &work))
					return false;
				double total = 0;
				for (double weight : std::as_const(weights))
					if (weight > 0)
						total += weight;
				if (!(total > 0) || !std::isfinite(total))
					return fail(error, Text::tr("A new vertex on surface %1 found no joint to bind to. Choose a joint instead.").arg(surface.name));
				for (auto w = weights.cbegin(); w != weights.cend(); ++w)
				{
					const float weight = float(w.value() / total);
					if (weight > 0)
						skinning.influences.append({w.key(), weight, modelJointTransformPoint(inverse[w.key()], frame.positions[vertex]),
							modelJointTransformVector(inverse[w.key()], frame.normals[vertex])});
				}
				skinning.count.append(skinning.influences.size() - skinning.first.last());
				pose[vertex] = PoseAll;
				++provenance.bound;
				continue;
			}
			const auto &only = origins.sources[first];
			if (count == 1 && only.mirror == 0 && only.weight == 1.0 && !(flags & ModelVertexOriginMerged))
			{
				const auto &source = before.surfaces[only.surface];
				skinning.influences += source.skinning.influences.mid(source.skinning.first[only.vertex], source.skinning.count[only.vertex]);
				skinning.count.append(skinning.influences.size() - skinning.first.last());
				const bool moved = movedAfter < surface.frames.size() && movedBefore < source.frames.size() &&
								   distanceBetween(surface.frames[movedAfter].positions[vertex], source.frames[movedBefore].positions[only.vertex]) > 1e-5;
				const bool repose = (flags & ModelVertexOriginRepose) != 0;
				if (((flags & ModelVertexOriginCreated) || repose) && moved)
				{
					if (!canRebind)
						return noBindFrame(surface.name);
					rebind(vertex);
					++provenance.rebound;
				}
				else
				{
					++provenance.kept;
					if (canRebind && (flags & ModelVertexOriginNormals))
					{
						rebindNormal(vertex);
						pose[vertex] |= PoseNormals;
					}
				}
				if (canRebind && (((flags & ModelVertexOriginCreated) && moved) || repose))
					pose[vertex] |= PoseAll;
				continue;
			}
			QVector<ModelSkinBlendSource> blend;
			bool mirrored = false;
			int limit = 0;
			for (int i = first; i < first + count; ++i)
			{
				const auto &source = origins.sources[i];
				const auto &skin = before.surfaces[source.surface].skinning;
				const QVector<int> *joints = nullptr;
				if (source.mirror)
				{
					mirrored = true;
					joints = mirrorMap(source.mirror);
					if (!joints)
						return false;
					for (int at = skin.first[source.vertex]; at < skin.first[source.vertex] + skin.count[source.vertex]; ++at)
					{
						const int joint = skin.influences[at].joint;
						if (joint < 0 || joint >= joints->size() || !(skin.influences[at].weight > 0))
							continue;
						const int partner = joints->at(joint);
						if (partner == -1)
							unmapped.insert(skeleton.joints[joint].name);
						else if (partner < -1)
						{
							const auto &other = skeleton.joints[-2 - partner].name;
							wrongSide.insert(QStringLiteral("%1=%2").arg(std::min(skeleton.joints[joint].name, other), std::max(skeleton.joints[joint].name, other)));
							wrongPlanes |= source.mirror;
						}
					}
				}
				blend.append({&skin, source.vertex, source.weight, joints});
				limit = std::max(limit, beforeLimit(source.surface));
			}
			if (!unmapped.isEmpty() || !wrongSide.isEmpty())
			{
				skinning.count.append(0);
				continue;
			}
			QVector<ModelJointInfluence> influences;
			switch (blendModelSkinInfluences(blend, &influences, &work))
			{
			case ModelSkinBlendStatus::Blended:
				break;
			case ModelSkinBlendStatus::Cancelled:
				return false;
			case ModelSkinBlendStatus::NegativeWeight:
				return fail(error, Text::tr("Surface %1 has negative skin weights where the edit interpolates or mirrors vertices. Repair them first.")
									   .arg(surface.name));
			case ModelSkinBlendStatus::InfluenceCount:
				return fail(error, Text::tr("A new vertex on surface %1 would need more than 64 joint influences. Prune the source weights first.")
									   .arg(surface.name));
			}
			skinning.influences += influences;
			skinning.count.append(influences.size());
			// Formats with a lower influence limit keep only the strongest.
			if (count > 1 && influences.size() > limit)
			{
				++provenance.widened;
				provenance.widest = std::max(provenance.widest, int(influences.size()));
				provenance.previousLimit = std::max(provenance.previousLimit, limit);
			}
			if (mirrored)
			{
				if (!canRebind)
					return noBindFrame(surface.name);
				rebind(vertex);
				pose[vertex] = PoseAll;
				++provenance.mirrored;
				continue;
			}
			const bool merged = (flags & ModelVertexOriginMerged) != 0;
			++(merged ? provenance.merged : provenance.interpolated);
			if (!canRebind)
			{
				// A merged vertex moved; an interpolated one follows its sources.
				if (merged)
					return noBindFrame(surface.name);
				continue;
			}
			// The blend already reproduces the tool's bind position when the
			// sources followed the skeleton; otherwise keep the tool's position.
			const bool consistent =
				distanceBetween(skinnedBindPosition(skinning.influences, skinning.first[vertex], skinning.count[vertex], bind),
					surface.frames[afterBind].positions[vertex]) <= 1e-4;
			if (!consistent)
				rebind(vertex);
			else if (flags & ModelVertexOriginNormals)
				rebindNormal(vertex);
			pose[vertex] = (flags & ModelVertexOriginRepose) ? PoseAll : PoseAgreeing;
		}
		if (!wrongSide.isEmpty())
		{
			auto pairs = wrongSide.values();
			std::sort(pairs.begin(), pairs.end());
			return fail(error, Text::tr("Mirroring surface %1 across %2 would use joint pairs that are not on opposite sides of that plane: %3. "
										"Remove them from the joint map (--joint-map on the command line), or choose the axis they mirror "
										"across.")
								   .arg(surface.name, planesText(wrongPlanes), pairs.join(QStringLiteral(", "))));
		}
		if (!unmapped.isEmpty())
		{
			auto names = unmapped.values();
			std::sort(names.begin(), names.end());
			return fail(error, Text::tr("Mirroring surface %1 would give the new half the weights of off-centre joints without a mirror partner: %2. "
										"Map each of them to its mirror joint, or choose to keep the joint names (--joint-map and "
										"--keep-joint-names on the command line).")
								   .arg(surface.name, names.join(QStringLiteral(", "))));
		}
		surface.skinning = std::move(skinning);
		if (std::any_of(pose.cbegin(), pose.cend(), [](quint8 value) { return value != 0; }))
		{
			for (int frame = 0; frame < surface.frames.size() && frame < poses.size(); ++frame)
			{
				if (!work.step())
					return false;
				if (!poses[frame])
					continue;
				ModelFrameGeometry skinned;
				QString problem;
				if (!skinModelSurface(surface, *poses[frame], &skinned, &problem))
					return fail(error, problem);
				auto &geometry = surface.frames[frame];
				for (int vertex = 0; vertex < vertexCount; ++vertex)
				{
					const quint8 mode = pose[vertex];
					if (!mode || (frame == afterBind && !(mode & PoseAgreeing)))
						continue;
					const bool agrees = distanceBetween(geometry.positions[vertex], skinned.positions[vertex]) <= 1e-3;
					if ((mode & PoseAll) || ((mode & PoseAgreeing) && agrees))
					{
						geometry.positions[vertex] = skinned.positions[vertex];
						geometry.normals[vertex] = skinned.normals[vertex];
					}
					else if ((mode & PoseNormals) && agrees)
						geometry.normals[vertex] = skinned.normals[vertex];
				}
			}
		}
		if (report)
			report->insert(index, provenance);
	}
	QString problem;
	if (!validateModelSkeleton(*after, &problem, control))
	{
		if (!work.check())
			return false;
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The edit would leave the skeleton inconsistent: %1").arg(problem));
	}
	return work.check();
}

QString modelSkinProvenanceSummary(const QMap<int, ModelSkinProvenance> &report, bool includeKept)
{
	ModelSkinProvenance total;
	for (const auto &entry : report)
		addProvenance(&total, entry);
	const auto counts = countsText(total, includeKept);
	if (counts.isEmpty())
		return {};
	return Text::tr("Skin weights: %1.").arg(counts);
}

QStringList modelSkinProvenanceLines(const ModelMesh &mesh, const QMap<int, ModelSkinProvenance> &report)
{
	QStringList lines;
	for (auto i = report.cbegin(); i != report.cend(); ++i)
	{
		auto counts = countsText(i.value(), true);
		if (counts.isEmpty())
			counts = Text::tr("no vertices");
		lines << Text::tr("Skin weights on surface %1 (%2): %3.").arg(i.key()).arg(mesh.surfaces.value(i.key()).name, counts);
	}
	return lines;
}

QJsonObject modelSkinProvenanceJson(const ModelMesh &mesh, const QMap<int, ModelSkinProvenance> &report)
{
	QJsonArray surfaces;
	ModelSkinProvenance total;
	for (auto i = report.cbegin(); i != report.cend(); ++i)
	{
		const auto &entry = i.value();
		surfaces.append(QJsonObject{{QStringLiteral("index"), i.key()},
									{QStringLiteral("name"), mesh.surfaces.value(i.key()).name},
									{QStringLiteral("authored"), entry.authored},
									{QStringLiteral("kept"), entry.kept},
									{QStringLiteral("interpolated"), entry.interpolated},
									{QStringLiteral("mirrored"), entry.mirrored},
									{QStringLiteral("rebound"), entry.rebound},
									{QStringLiteral("merged"), entry.merged},
									{QStringLiteral("bound"), entry.bound},
									{QStringLiteral("widened"), entry.widened},
									{QStringLiteral("widest"), entry.widest},
									{QStringLiteral("previousLimit"), entry.previousLimit}});
		addProvenance(&total, entry);
	}
	return {{QStringLiteral("surfaces"), surfaces},
			{QStringLiteral("kept"), total.kept},
			{QStringLiteral("interpolated"), total.interpolated},
			{QStringLiteral("mirrored"), total.mirrored},
			{QStringLiteral("rebound"), total.rebound},
			{QStringLiteral("merged"), total.merged},
			{QStringLiteral("bound"), total.bound},
			{QStringLiteral("widened"), total.widened},
			{QStringLiteral("widest"), total.widest}};
}

} // namespace vibestudio
