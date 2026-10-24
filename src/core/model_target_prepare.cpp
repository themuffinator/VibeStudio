#include "core/model_target_prepare.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTargets)
};

bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}

int triangleCount(const ModelMesh &mesh)
{
	int count = 0;
	for (const auto &surface : mesh.surfaces)
		count += surface.triangles.size();
	return count;
}

bool hasIssue(const ModelTargetReport &report, const QString &limit, int surface = -2)
{
	return std::any_of(report.issues.cbegin(), report.issues.cend(),
					   [&](const ModelTargetIssue &issue) { return issue.limit == limit && (surface == -2 || issue.surface == surface); });
}

// The value an issue measured, or 0.
qint64 issueValue(const ModelTargetReport &report, const QString &limit)
{
	for (const auto &issue : report.issues)
	{
		if (issue.limit == limit)
			return issue.value;
	}
	return 0;
}

// A surface name that is new, fits 63 characters, and does not merge with
// another after Quake III's normalisation (lower case, a final "_X" removed):
// "body" splits into "body2", "body3" and so on.
QString partName(const ModelMesh &mesh, const QString &base, int part)
{
	const auto engine = [](QString name)
	{
		name = name.toLower();
		if (name.size() > 2 && name[name.size() - 2] == QLatin1Char('_'))
			name.chop(2);
		return name;
	};
	QSet<QString> taken, engineTaken;
	for (const auto &surface : mesh.surfaces)
	{
		taken.insert(surface.name);
		engineTaken.insert(engine(surface.name));
	}
	for (int n = part;; ++n)
	{
		const auto suffix = QString::number(n);
		auto name = base.left(63 - int(suffix.size())) + suffix;
		if (!taken.contains(name) && !engineTaken.contains(engine(name)))
			return name;
	}
}

class Preparer
{
  public:
	Preparer(const ModelTargetProfile &profile, const ModelTargetPrepareOptions &options, ModelTargetPreparation *result, QString *error,
			 const ModelWorkControl &control)
		: m_profile(profile), m_limits(profile.limits), m_options(options), m_result(result), m_error(error), m_control(control)
	{
	}

	bool run()
	{
		const auto &format = m_profile.format;
		const bool single = format == QStringLiteral("mdl") || format == QStringLiteral("md2");
		const bool perSurface = format == QStringLiteral("md3") || format == QStringLiteral("iqm") || format == QStringLiteral("md5mesh");
		QStringList surfacesBefore;
		for (const auto &surface : std::as_const(mesh().surfaces))
			surfacesBefore << surface.name;
		if (single && m_options.join && !join())
			return false;
		if (format == QStringLiteral("md3") && m_options.fit && !fit())
			return false;
		if (perSurface && !budgets())
			return false;
		if (single && m_options.decimate && !wholeModel())
			return false;
		if ((format == QStringLiteral("iqm") || format == QStringLiteral("md5mesh")) && m_options.influences && !influences())
			return false;
		// Generated detail levels are rebuilt from the prepared model; hand-made
		// ones were made for the surfaces it had (core/model_lod.h).
		QStringList surfacesAfter;
		for (const auto &surface : std::as_const(mesh().surfaces))
			surfacesAfter << surface.name;
		QStringList handMade;
		for (const auto &level : std::as_const(mesh().detailLevels))
		{
			if (!level.generated)
				handMade << QString::number(level.level);
		}
		if (surfacesAfter != surfacesBefore && !handMade.isEmpty())
		{
			const int count = int(handMade.size());
			m_result->notes << Text::tr("The source stores %n hand-made detail level(s) (%1), made for the surfaces it had before. Rebuild them with the new "
										"surfaces in Detail Levels…, or those parts disappear at a distance.",
										nullptr, count)
								   .arg(handMade.join(QStringLiteral(", ")));
		}
		return true;
	}

  private:
	ModelMesh &mesh() { return m_result->mesh; }

	bool assess(ModelTargetReport *report) { return assessModelTarget(mesh(), m_profile, report, m_error, m_control); }

	// One editor edit on the prepared model; skin weights, seams and poses
	// follow as they do in the editor.
	bool apply(const ModelEdit &edit, ModelSelection *selection = nullptr)
	{
		auto candidate = mesh();
		QString failure;
		if (!applyModelEdit(&candidate, edit, selection, &failure, m_control))
		{
			if (!modelWorkCheckpoint(m_control, ModelWorkPhase::Editing, 0, 0, m_error))
				return false;
			m_failure = failure;
			return false;
		}
		mesh() = std::move(candidate);
		return true;
	}
	// A failed edit that was not cancellation leaves the model as it was, with a
	// note, and preparation goes on.
	bool skipped(const QString &what)
	{
		if (m_failure.isEmpty())
			return false;
		m_result->notes << Text::tr("%1 was skipped: %2").arg(what, m_failure);
		m_failure.clear();
		return true;
	}

	void record(ModelTargetFixKind kind, const QString &limit, const QString &element, const QString &summary, qint64 before, qint64 after, int surface)
	{
		m_result->fixes.append({kind, limit, element, summary, before, after});
		if (surface >= 0)
			m_touched.insert(surface);
	}

	// MDL and MD2 hold one surface: join the rest into the first.
	bool join()
	{
		const int surfaces = int(mesh().surfaces.size());
		if (surfaces < 2)
			return true;
		ModelEdit edit;
		edit.kind = ModelEditKind::JoinSurfaces;
		edit.selection.surface = 0;
		edit.targetSurface = 0;
		QStringList replaced;
		for (int s = 0; s < surfaces; ++s)
		{
			edit.surfaces.insert(s);
			if (mesh().surfaces[s].skinPaths != mesh().surfaces[0].skinPaths)
				replaced << mesh().surfaces[s].name;
		}
		edit.adoptTargetMaterials = !replaced.isEmpty();
		const auto first = mesh().surfaces[0].name;
		QSet<QString> joined;
		for (int s = 1; s < surfaces; ++s)
			joined.insert(mesh().surfaces[s].name);
		if (!apply(edit))
			return skipped(Text::tr("Joining the surfaces"));
		record(ModelTargetFixKind::Join, QStringLiteral("single-surface"), first,
			   Text::tr("Joined %n surface(s) into \"%1\", which %2 needs as its one surface.", nullptr, surfaces).arg(first, m_profile.format.toUpper()),
			   surfaces, 1, 0);
		if (!replaced.isEmpty())
		{
			const int count = int(replaced.size());
			m_result->notes << Text::tr("Surfaces that used other materials now use \"%1\"'s (%n): %2. Their UVs must lie in the same skin.", nullptr, count)
								   .arg(first, replaced.join(QStringLiteral(", ")));
		}
		// Skin variants follow the materials: the joined faces wear what the
		// first surface wears in each variant, so bindings to the surfaces
		// joined away go, rather than naming surfaces the model no longer has.
		QStringList dropped;
		for (auto &variant : mesh().skinVariants)
		{
			QStringList names;
			for (const auto &binding : std::as_const(variant.bindings))
			{
				if (joined.contains(binding.surface))
					names << binding.surface;
			}
			if (names.isEmpty())
				continue;
			variant.bindings.erase(std::remove_if(variant.bindings.begin(), variant.bindings.end(),
												  [&](const ModelSkinVariantBinding &binding) { return joined.contains(binding.surface); }),
								   variant.bindings.end());
			dropped << Text::tr("%1 (%2)").arg(variant.name, names.join(QStringLiteral(", ")));
		}
		if (!dropped.isEmpty())
		{
			const int count = int(dropped.size());
			m_result->notes << Text::tr("Skin variants that bound joined surfaces (%n): %1. Those faces now wear \"%2\"'s material in each variant.", nullptr, count)
								   .arg(dropped.join(QStringLiteral("; ")), first);
		}
		return true;
	}

	// MD3 stores positions in 1/64 units in 16 bits: -512 to 511.984375.
	bool fit()
	{
		ModelTargetReport report;
		if (!assess(&report))
			return false;
		if (!hasIssue(report, QStringLiteral("coordinate-range")))
			return true;
		if (!mesh().skeleton.isEmpty())
		{
			m_result->notes << Text::tr("The model is outside MD3's range, but it follows a skeleton, so it was not moved: move or scale the skeleton and its "
								  "surfaces together on the Skeleton page.");
			return true;
		}
		constexpr double low = -512.0, high = 32767.0 / 64.0, margin = 1.0 / 64.0;
		double mins[3]{1e30, 1e30, 1e30}, maxs[3]{-1e30, -1e30, -1e30};
		for (const auto &surface : mesh().surfaces)
		{
			for (const auto &frame : surface.frames)
			{
				for (const auto &p : frame.positions)
				{
					const double v[3]{p.x, p.y, p.z};
					for (int axis = 0; axis < 3; ++axis)
					{
						mins[axis] = std::min(mins[axis], v[axis]);
						maxs[axis] = std::max(maxs[axis], v[axis]);
					}
				}
			}
		}
		// Scale only when an axis is too long to fit, about the bounds' centre,
		// then move each axis the least that brings it into range.
		double scale = 1;
		double centre[3];
		for (int axis = 0; axis < 3; ++axis)
		{
			centre[axis] = (mins[axis] + maxs[axis]) / 2;
			const double extent = maxs[axis] - mins[axis], room = high - low - 2 * margin;
			if (extent > room)
				scale = std::min(scale, room / extent);
		}
		double shift[3]{0, 0, 0};
		for (int axis = 0; axis < 3; ++axis)
		{
			const double lowAfter = centre[axis] + (mins[axis] - centre[axis]) * scale;
			const double highAfter = centre[axis] + (maxs[axis] - centre[axis]) * scale;
			if (lowAfter < low + margin)
				shift[axis] = low + margin - lowAfter;
			else if (highAfter > high - margin)
				shift[axis] = high - margin - highAfter;
		}
		const auto move = [&](ModelVec3 p)
		{
			const double v[3]{p.x, p.y, p.z};
			double r[3];
			for (int axis = 0; axis < 3; ++axis)
				r[axis] = centre[axis] + (v[axis] - centre[axis]) * scale + shift[axis];
			return ModelVec3{float(r[0]), float(r[1]), float(r[2])};
		};
		const auto size = [&](ModelVec3 s) { return ModelVec3{float(s.x * scale), float(s.y * scale), float(s.z * scale)}; };
		ModelWorkProgress work(m_control, ModelWorkPhase::Editing, m_error);
		for (auto &surface : mesh().surfaces)
		{
			for (auto &frame : surface.frames)
			{
				for (auto &p : frame.positions)
				{
					if (!work.step())
						return false;
					p = move(p);
				}
			}
		}
		// Attachments, frame origins and collision boxes move with the model;
		// a uniform scale keeps every normal and tag axis.
		for (auto &tag : mesh().tags)
			tag.origin = move(tag.origin);
		for (auto &frame : mesh().frames)
			frame.origin = move(frame.origin);
		for (auto &box : mesh().collisionBoxes)
		{
			box.centre = move(box.centre);
			box.size = size(box.size);
			for (auto &pose : box.framePoses)
			{
				pose.centre = move(pose.centre);
				pose.size = size(pose.size);
			}
		}
		updateEditableModelMetadata(&mesh());
		const auto errors = validateEditableModel(mesh(), m_control);
		if (!errors.isEmpty())
			return fail(m_error, errors.join(QLatin1Char('\n')));
		const qint64 outside = issueValue(report, QStringLiteral("coordinate-range"));
		const auto offset = QStringLiteral("%1, %2, %3").arg(shift[0], 0, 'f', 3).arg(shift[1], 0, 'f', 3).arg(shift[2], 0, 'f', 3);
		const auto summary = scale < 1 ? Text::tr("Scaled the model by %1 about its centre and moved it by %2 units, so every pose fits MD3's range.")
											 .arg(scale, 0, 'f', 4)
											 .arg(offset)
									   : Text::tr("Moved the model by %1 units, so every pose fits MD3's range.").arg(offset);
		record(ModelTargetFixKind::Fit, QStringLiteral("coordinate-range"), {}, summary, outside, 0, -1);
		m_result->notes << (scale < 1 ? Text::tr("The model is smaller and its origin moved: entities placing it, and attachments on it, see the change.")
									  : Text::tr("The model's origin moved: entities placing it see the change."));
		return true;
	}

	// Per-surface budgets (MD3, IQM, MD5): split what fits within the surface
	// limit, and decimate the rest.
	bool budgets()
	{
		const int vertexBudget = m_limits.vertices, triangleBudget = m_limits.triangles;
		if (vertexBudget <= 0 && triangleBudget <= 0)
			return true;
		const int surfaceLimit = m_limits.surfaces > 0 ? std::min(m_limits.surfaces, modelDocumentMaxSurfaces) : modelDocumentMaxSurfaces;
		const int original = int(mesh().surfaces.size());
		for (int s = 0; s < original; ++s)
		{
			const auto over = [&]
			{
				const auto &surface = mesh().surfaces[s];
				return (vertexBudget > 0 && surface.vertexCount > vertexBudget) || (triangleBudget > 0 && surface.triangles.size() > triangleBudget);
			};
			if (!over())
				continue;
			if (m_options.split && !split(s, surfaceLimit))
				return false;
			if (over() && m_options.decimate && !decimateSurface(s))
				return false;
		}
		return true;
	}

	bool split(int s, int surfaceLimit)
	{
		auto name = mesh().surfaces[s].name;
		// Unused vertices count against the budget and stay with the first part.
		int unused = mesh().surfaces[s].vertexCount;
		{
			QVector<bool> used(mesh().surfaces[s].vertexCount, false);
			for (const auto &t : mesh().surfaces[s].triangles)
			{
				for (int v : {t.a, t.b, t.c})
				{
					unused -= !used[v];
					used[v] = true;
				}
			}
		}
		if (unused > 0)
		{
			const int before = mesh().surfaces[s].vertexCount;
			ModelEdit edit;
			edit.kind = ModelEditKind::RemoveUnusedVertices;
			edit.selection.surface = s;
			if (apply(edit))
			{
				// A change of its own: it may be all the surface needed.
				const int after = mesh().surfaces[s].vertexCount;
				record(ModelTargetFixKind::Split, QStringLiteral("vertices"), name,
					   Text::tr("Removed %n unused vertex(es) from \"%1\" (%2 to %3 vertices); no face used them, but they counted against its budget.",
								nullptr, before - after)
						   .arg(name)
						   .arg(before)
						   .arg(after),
					   before, after, s);
			}
			else if (!skipped(Text::tr("Removing unused vertices from \"%1\"").arg(name)))
				return false;
		}
		const auto &surface = mesh().surfaces[s];
		const int vertices = surface.vertexCount, triangles = int(surface.triangles.size());
		QVector<int> parts;
		int count = 0;
		QString problem;
		if (!modelTargetSurfaceParts(surface, m_limits.vertices, m_limits.triangles, &parts, &count, &problem, m_control))
		{
			if (!modelWorkCheckpoint(m_control, ModelWorkPhase::Editing, 0, 0, m_error))
				return false;
			m_result->notes << Text::tr("\"%1\" could not be split: %2").arg(name, problem);
			return true;
		}
		if (count < 2)
			return true;
		if (mesh().surfaces.size() + count - 1 > surfaceLimit)
		{
			// Decimation, when allowed, then reduces the surface instead.
			m_result->notes << (m_options.decimate ? Text::tr("Splitting \"%1\" into %n part(s) would exceed %2 surfaces, so it is reduced instead.", nullptr, count)
													   .arg(name)
													   .arg(surfaceLimit)
												   : Text::tr("Splitting \"%1\" into %n part(s) would exceed %2 surfaces, so it was left as it is.", nullptr, count)
														 .arg(name)
														 .arg(surfaceLimit));
			return true;
		}
		// Separate parts 1 ... in order; the faces left behind keep their order,
		// so each part's faces are found by their position among the remainder.
		QVector<int> remaining(parts.size());
		std::iota(remaining.begin(), remaining.end(), 0);
		int largestVertices = 0, largestTriangles = 0;
		QStringList created;
		for (int part = 1; part < count; ++part)
		{
			ModelEdit edit;
			edit.kind = ModelEditKind::SeparateFaces;
			edit.selection.surface = s;
			QVector<int> kept;
			for (int index = 0; index < remaining.size(); ++index)
			{
				if (parts[remaining[index]] == part)
					edit.selection.faces.insert(index);
				else
					kept << remaining[index];
			}
			edit.text = partName(mesh(), name, part + 1);
			if (!apply(edit))
			{
				if (!skipped(Text::tr("Splitting \"%1\"").arg(name)))
					return false;
				break;
			}
			remaining = kept;
			created << edit.text;
			const auto &added = mesh().surfaces.last();
			largestVertices = std::max(largestVertices, added.vertexCount);
			largestTriangles = std::max(largestTriangles, int(added.triangles.size()));
			m_touched.insert(int(mesh().surfaces.size()) - 1);
		}
		if (created.isEmpty())
			return true;
		// Separating gives each new surface its origin's binding in every skin
		// variant (core/model_surfaces.cpp), so the variants still cover it.
		QStringList variants;
		for (const auto &variant : std::as_const(mesh().skinVariants))
		{
			if (std::any_of(variant.bindings.cbegin(), variant.bindings.cend(), [&](const ModelSkinVariantBinding &binding) { return binding.surface == name; }))
				variants << variant.name;
		}
		if (!variants.isEmpty())
			m_result->notes << Text::tr("Skin variants %1 give the new surfaces %2 the material they give \"%3\".")
								   .arg(variants.join(QStringLiteral(", ")), created.join(QStringLiteral(", ")), name);
		const auto &rest = mesh().surfaces[s];
		largestVertices = std::max(largestVertices, rest.vertexCount);
		largestTriangles = std::max(largestTriangles, int(rest.triangles.size()));
		const bool byVertices = m_limits.vertices > 0 && vertices > m_limits.vertices;
		const int partsMade = int(created.size()) + 1;
		record(ModelTargetFixKind::Split, byVertices ? QStringLiteral("vertices") : QStringLiteral("triangles"), name,
			   Text::tr("Split \"%1\" (%2 vertices, %3 triangles) into %n surfaces of at most %4 vertices and %5 triangles: %6.", nullptr, partsMade)
				   .arg(name)
				   .arg(vertices)
				   .arg(triangles)
				   .arg(largestVertices)
				   .arg(largestTriangles)
				   .arg((QStringList{name} + created).join(QStringLiteral(", "))),
			   byVertices ? vertices : triangles, byVertices ? largestVertices : largestTriangles, s);
		return true;
	}

	// Decimates surface `s` until it fits its budgets, a few passes at most:
	// the vertex count follows the triangle count only roughly.
	bool decimateSurface(int s)
	{
		const auto name = mesh().surfaces[s].name;
		const int beforeTriangles = int(mesh().surfaces[s].triangles.size()), beforeVertices = mesh().surfaces[s].vertexCount;
		for (int pass = 0; pass < 4; ++pass)
		{
			const auto &surface = mesh().surfaces[s];
			const int triangles = int(surface.triangles.size()), vertices = surface.vertexCount;
			int target = triangles;
			if (m_limits.triangles > 0 && triangles > m_limits.triangles)
				target = m_limits.triangles;
			if (m_limits.vertices > 0 && vertices > m_limits.vertices)
				target = std::min(target, int(std::floor(double(triangles) * m_limits.vertices / vertices * 0.97)));
			if (target >= triangles)
				break;
			ModelEdit edit;
			edit.kind = ModelEditKind::Decimate;
			edit.selection.surface = s;
			for (int face = 0; face < triangles; ++face)
				edit.selection.faces.insert(face);
			edit.tool.targetTriangles = std::max(1, target);
			// A pass that removes nothing is taken back, so every change the
			// prepared model holds is one the review lists.
			const auto previous = mesh();
			if (!apply(edit))
			{
				if (!skipped(Text::tr("Reducing \"%1\"").arg(name)))
					return false;
				break;
			}
			if (mesh().surfaces[s].triangles.size() >= triangles)
			{
				mesh() = previous;
				break;
			}
		}
		const int afterTriangles = int(mesh().surfaces[s].triangles.size());
		if (afterTriangles < beforeTriangles)
			record(ModelTargetFixKind::Decimate, QStringLiteral("triangles"), name,
				   Text::tr("Reduced \"%1\" from %2 to %3 triangles (%4 to %5 vertices).")
					   .arg(name)
					   .arg(beforeTriangles)
					   .arg(afterTriangles)
					   .arg(beforeVertices)
					   .arg(mesh().surfaces[s].vertexCount),
				   beforeTriangles, afterTriangles, s);
		return true;
	}

	// MDL and MD2: triangles, stored and drawn vertices, measured by the
	// target's own counts after each pass.
	bool wholeModel()
	{
		if (mesh().surfaces.size() != 1)
			return true;
		const int beforeTriangles = triangleCount(mesh());
		QString limit;
		for (int pass = 0; pass < 6; ++pass)
		{
			ModelTargetReport report;
			if (!assess(&report))
				return false;
			const int triangles = int(mesh().surfaces[0].triangles.size());
			double share = 1;
			for (const auto &issue : std::as_const(report.issues))
			{
				if ((issue.limit == QStringLiteral("triangles") || issue.limit == QStringLiteral("vertices") ||
					 issue.limit == QStringLiteral("render-vertices")) &&
					issue.value > 0 && issue.maximum > 0)
				{
					const double wanted = double(issue.maximum) / issue.value * (issue.limit == QStringLiteral("triangles") ? 1.0 : 0.97);
					if (wanted < share)
					{
						share = wanted;
						limit = issue.limit;
					}
				}
			}
			if (share >= 1)
				break;
			const int target = std::max(1, int(std::floor(triangles * share)));
			if (target >= triangles)
				break;
			ModelEdit edit;
			edit.kind = ModelEditKind::Decimate;
			edit.selection.surface = 0;
			for (int face = 0; face < triangles; ++face)
				edit.selection.faces.insert(face);
			edit.tool.targetTriangles = target;
			const auto previous = mesh();
			if (!apply(edit))
			{
				if (!skipped(Text::tr("Reducing the model")))
					return false;
				break;
			}
			if (mesh().surfaces[0].triangles.size() >= triangles)
			{
				mesh() = previous;
				break;
			}
		}
		const int afterTriangles = triangleCount(mesh());
		if (afterTriangles < beforeTriangles)
			record(ModelTargetFixKind::Decimate, limit.isEmpty() ? QStringLiteral("triangles") : limit, mesh().surfaces[0].name,
				   Text::tr("Reduced the model from %1 to %2 triangles for %3's %4 limit.")
					   .arg(beforeTriangles)
					   .arg(afterTriangles)
					   .arg(m_profile.label, modelTargetLimitName(limit.isEmpty() ? QStringLiteral("triangles") : limit)),
				   beforeTriangles, afterTriangles, 0);
		return true;
	}

	// Joint influences past what the format keeps (IQM: four per vertex) and
	// MD5 meshes over their weight limit: keep the strongest, renormalised.
	bool influences()
	{
		if (mesh().skeleton.isEmpty())
			return true;
		for (int s = 0; s < mesh().surfaces.size(); ++s)
		{
			const auto &surface = mesh().surfaces[s];
			if (surface.skinning.isEmpty() || surface.vertexCount == 0)
				continue;
			int most = 0, weights = 0;
			for (int count : surface.skinning.count)
			{
				most = std::max(most, count);
				weights += std::max(1, count);
			}
			int keep = m_limits.influences > 0 && most > m_limits.influences ? m_limits.influences : most;
			if (m_limits.weights > 0 && weights > m_limits.weights)
				keep = std::min(keep, std::max(1, m_limits.weights / surface.vertexCount));
			if (keep >= most)
				continue;
			const auto name = surface.name;
			ModelEdit edit;
			edit.kind = ModelEditKind::PruneSkinWeights;
			edit.selection.surface = s;
			edit.skin.maxInfluences = keep;
			if (!apply(edit))
			{
				if (!skipped(Text::tr("Reducing the joint influences of \"%1\"").arg(name)))
					return false;
				continue;
			}
			record(ModelTargetFixKind::Influences, m_limits.weights > 0 && weights > m_limits.weights ? QStringLiteral("weights") : QStringLiteral("influences"),
				   name,
				   Text::tr("Kept the strongest %n joint influence(s) of each vertex of \"%1\" (it had up to %2), renormalised.", nullptr, keep).arg(name).arg(most),
				   most, keep, s);
		}
		return true;
	}

	const ModelTargetProfile &m_profile;
	const ModelTargetLimits &m_limits;
	const ModelTargetPrepareOptions &m_options;
	ModelTargetPreparation *m_result;
	QString *m_error;
	const ModelWorkControl &m_control;
	QString m_failure;

  public:
	QSet<int> m_touched;
};
} // namespace

bool prepareModelForTarget(const ModelMesh &mesh, const ModelTargetProfile &profile, const ModelTargetPrepareOptions &options,
						   ModelTargetPreparation *preparation, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	const auto errors = validateEditableModel(mesh, control);
	if (!errors.isEmpty())
		return fail(error, errors.join(QLatin1Char('\n')));
	ModelTargetPreparation result;
	result.profile = profile;
	result.mesh = mesh;
	if (!assessModelTarget(mesh, profile, &result.before, error, control))
		return false;
	Preparer preparer(profile, options, &result, error, control);
	if (!result.before.ready() || profile.format == QStringLiteral("iqm") || profile.format == QStringLiteral("md5mesh"))
	{
		if (!preparer.run())
			return false;
	}
	if (!assessModelTarget(result.mesh, profile, &result.after, error, control))
		return false;
	// Leave the changed surfaces selected, so the review's result is in view.
	if (!preparer.m_touched.isEmpty())
	{
		const auto touched = preparer.m_touched;
		result.selection.surface = *std::min_element(touched.cbegin(), touched.cend());
		for (int surface : touched)
		{
			if (surface >= 0 && surface < result.mesh.surfaces.size())
				result.selection.surfaces.insert(surface);
		}
		if (result.selection.surfaces.isEmpty())
			result.selection = {};
	}
	if (preparation)
		*preparation = std::move(result);
	return modelWorkCheckpoint(control, ModelWorkPhase::Editing, 1, 1, error);
}

QString modelTargetFixKindId(ModelTargetFixKind kind)
{
	switch (kind)
	{
	case ModelTargetFixKind::Split:
		return QStringLiteral("split");
	case ModelTargetFixKind::Join:
		return QStringLiteral("join");
	case ModelTargetFixKind::Decimate:
		return QStringLiteral("decimate");
	case ModelTargetFixKind::Fit:
		return QStringLiteral("fit");
	case ModelTargetFixKind::Influences:
		return QStringLiteral("influences");
	}
	return {};
}

QString modelTargetFixKindName(ModelTargetFixKind kind)
{
	switch (kind)
	{
	case ModelTargetFixKind::Split:
		return QCoreApplication::translate("VibeStudioModelTargets", "Split surfaces");
	case ModelTargetFixKind::Join:
		return QCoreApplication::translate("VibeStudioModelTargets", "Join surfaces");
	case ModelTargetFixKind::Decimate:
		return QCoreApplication::translate("VibeStudioModelTargets", "Decimate");
	case ModelTargetFixKind::Fit:
		return QCoreApplication::translate("VibeStudioModelTargets", "Fit MD3's range");
	case ModelTargetFixKind::Influences:
		return QCoreApplication::translate("VibeStudioModelTargets", "Reduce joint influences");
	}
	return {};
}

bool skipModelTargetFixes(const QString &skip, ModelTargetPrepareOptions *options, QString *error)
{
	ModelTargetPrepareOptions result = options ? *options : ModelTargetPrepareOptions{};
	for (const auto &part : skip.split(QLatin1Char(','), Qt::SkipEmptyParts))
	{
		const auto id = part.trimmed().toLower();
		if (id == QStringLiteral("split"))
			result.split = false;
		else if (id == QStringLiteral("join"))
			result.join = false;
		else if (id == QStringLiteral("decimate"))
			result.decimate = false;
		else if (id == QStringLiteral("fit"))
			result.fit = false;
		else if (id == QStringLiteral("influences"))
			result.influences = false;
		else
			return fail(error, QCoreApplication::translate("VibeStudioModelTargets", "There is no fix called \"%1\". Choose from split, join, decimate, fit and influences.")
								   .arg(part.trimmed()));
	}
	if (options)
		*options = result;
	return true;
}

QStringList modelTargetPreparationLines(const ModelTargetPreparation &preparation)
{
	QStringList lines;
	const auto &profile = preparation.profile;
	const int fixes = int(preparation.fixes.size());
	lines << (fixes == 0 ? QCoreApplication::translate("VibeStudioModelTargets", "Nothing to change for %1 %2.").arg(profile.format.toUpper(), profile.label)
						 : QCoreApplication::translate("VibeStudioModelTargets", "Changes for %1 %2 (%n):", nullptr, fixes).arg(profile.format.toUpper(), profile.label));
	for (const auto &fix : preparation.fixes)
		lines << QStringLiteral("  - ") + fix.summary;
	for (const auto &note : preparation.notes)
		lines << QStringLiteral("  ! ") + note;
	const int left = int(preparation.after.issues.size());
	if (left == 0)
		lines << QCoreApplication::translate("VibeStudioModelTargets", "The prepared model is ready for %1.").arg(profile.label);
	else
	{
		lines << QCoreApplication::translate("VibeStudioModelTargets", "Still blocking %1 after the changes (%n):", nullptr, left).arg(profile.label);
		for (const auto &line : modelTargetIssueLines(preparation.after))
			lines << QStringLiteral("  - ") + line;
	}
	return lines;
}

QJsonObject modelTargetPreparationJson(const ModelTargetPreparation &preparation)
{
	QJsonArray fixes;
	for (const auto &fix : preparation.fixes)
	{
		QJsonObject object{{QStringLiteral("kind"), modelTargetFixKindId(fix.kind)},
						   {QStringLiteral("limit"), fix.limit},
						   {QStringLiteral("summary"), fix.summary},
						   {QStringLiteral("before"), fix.before},
						   {QStringLiteral("after"), fix.after}};
		if (!fix.element.isEmpty())
			object.insert(QStringLiteral("element"), fix.element);
		fixes.append(object);
	}
	return {{QStringLiteral("format"), preparation.profile.format},
			{QStringLiteral("target"), preparation.profile.id},
			{QStringLiteral("label"), preparation.profile.label},
			{QStringLiteral("changed"), preparation.changed()},
			{QStringLiteral("fixes"), fixes},
			{QStringLiteral("notes"), QJsonArray::fromStringList(preparation.notes)},
			{QStringLiteral("before"), modelTargetReportJson(preparation.before)},
			{QStringLiteral("after"), modelTargetReportJson(preparation.after)},
			{QStringLiteral("ready"), preparation.after.ready()}};
}
} // namespace vibestudio
