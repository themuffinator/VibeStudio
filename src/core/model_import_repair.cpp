#include "core/model_import_repair.h"
#include "core/model_source_decode.h"
#include "core/model_geometry_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelImportRepair)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool coordinate(float x)
{
	return std::isfinite(x) && std::abs(x) <= 1000000.f;
}
bool coordinate(ModelVec3 p)
{
	return coordinate(p.x) && coordinate(p.y) && coordinate(p.z);
}
bool usableNormal(ModelVec3 n)
{
	return coordinate(n) && double(n.x) * n.x + double(n.y) * n.y + double(n.z) * n.z >= 1e-12;
}
bool sameSourceLocation(const ModelImportRepairPlan &plan)
{
#ifdef Q_OS_WIN
	constexpr auto sensitivity = Qt::CaseInsensitive;
#else
	constexpr auto sensitivity = Qt::CaseSensitive;
#endif
	return !plan.resolvedPath.isEmpty() && QFileInfo(plan.inputPath).canonicalFilePath().compare(plan.resolvedPath, sensitivity) == 0;
}
bool prepareGeometry(ModelImportRepairPlan *plan, QString *error, const ModelWorkControl &control)
{
	auto &mesh = plan->mesh;
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!mesh.error.isEmpty() || !mesh.warnings.isEmpty() || !mesh.geometryAvailable || mesh.frames.isEmpty() ||
		mesh.frames.size() > modelDocumentMaxFrames || mesh.surfaces.isEmpty() || mesh.surfaces.size() > modelDocumentMaxSurfaces)
		return fail(error, Text::tr("Repair requires a complete decoded model within the editable surface and pose limits. Decode errors "
									"or warnings cannot be discarded."));
	qint64 vertices = 0, triangles = 0;
	for (const auto &surface : mesh.surfaces)
	{
		vertices += surface.vertexCount;
		triangles += surface.triangles.size();
		if (!surface.warnings.isEmpty() || surface.vertexCount < 3 || surface.vertexCount > modelDocumentMaxVertices ||
			surface.texCoords.size() != surface.vertexCount || surface.frames.size() != mesh.frames.size() || surface.triangles.isEmpty() ||
			surface.uvSeams.size() > surface.triangles.size() * 3)
			return fail(
				error,
				Text::tr("Surface %1 has incomplete geometry or decode warnings; repair cannot invent missing data.").arg(surface.name));
	}
	if (vertices > modelDocumentMaxVertices || triangles > modelDocumentMaxTriangles ||
		vertices * mesh.frames.size() > modelDocumentMaxFrameVertices || triangles * mesh.frames.size() > modelImportRepairMaxFacePoses)
		return fail(error,
					Text::tr("Import repair exceeds the editable model limits or %1 face poses. Reduce source complexity before repairing.")
						.arg(modelImportRepairMaxFacePoses));
	for (int s = 0; s < mesh.surfaces.size(); ++s)
	{
		auto &surface = mesh.surfaces[s];
		for (const auto uv : surface.texCoords)
		{
			if (!work.step())
				return false;
			if (!coordinate(uv.u) || !coordinate(uv.v))
				return fail(error, Text::tr("Repair requires finite positions and UVs within the editable coordinate range."));
		}
		for (const auto &frame : surface.frames)
		{
			if (frame.positions.size() != surface.vertexCount || frame.normals.size() != surface.vertexCount)
				return fail(error, Text::tr("Every pose must retain complete position and normal arrays before repair."));
			for (auto p : frame.positions)
			{
				if (!work.step())
					return false;
				if (!coordinate(p))
					return fail(error, Text::tr("Repair requires finite positions and UVs within the editable coordinate range."));
			}
		}
		QVector<ModelTriangle> kept;
		kept.reserve(surface.triangles.size());
		for (int f = 0; f < surface.triangles.size(); ++f)
		{
			if (!work.step())
				return false;
			const auto t = surface.triangles[f];
			ModelImportRepairChange change{ModelImportRepairKind::InvalidIndex, s, -1, f};
			bool remove =
				t.a < 0 || t.b < 0 || t.c < 0 || t.a >= surface.vertexCount || t.b >= surface.vertexCount || t.c >= surface.vertexCount;
			if (!remove && (t.a == t.b || t.b == t.c || t.c == t.a))
			{
				remove = true;
				change.kind = ModelImportRepairKind::RepeatedVertex;
			}
			for (int pose = 0; !remove && pose < surface.frames.size(); ++pose)
			{
				if (!work.step())
					return false;
				const auto &positions = surface.frames[pose].positions;
				if (model_geometry::collapsedTriangle(positions[t.a], positions[t.b], positions[t.c]))
				{
					remove = true;
					change.kind = ModelImportRepairKind::CollapsedFace;
					change.frame = pose;
				}
			}
			if (remove)
			{
				plan->changes.append(change);
				++plan->removedFaces;
			}
			else
				kept.append(t);
		}
		if (kept.isEmpty())
			return fail(
				error,
				Text::tr("All faces of surface %1 would be removed. Repair its geometry in the source; no surface or pose was discarded.")
					.arg(surface.name));
		surface.triangles = std::move(kept);
		for (int pose = 0; pose < surface.frames.size(); ++pose)
		{
			auto &frame = surface.frames[pose];
			QVector<int> invalid;
			for (int v = 0; v < frame.normals.size(); ++v)
			{
				if (!work.step())
					return false;
				if (!usableNormal(frame.normals[v]))
					invalid.append(v);
			}
			if (invalid.isEmpty())
				continue;
			QVector<model_geometry::P3> sums(surface.vertexCount, {0, 0, 0});
			for (const auto &t : surface.triangles)
			{
				if (!work.step())
					return false;
				using namespace model_geometry;
				const auto n = cross(point(frame.positions[t.b]) - point(frame.positions[t.a]),
									 point(frame.positions[t.c]) - point(frame.positions[t.a]));
				for (int v : {t.a, t.b, t.c})
					sums[v] = sums[v] + n;
			}
			for (int v : invalid)
			{
				if (!work.step())
					return false;
				const auto size = model_geometry::length(sums[v]);
				const bool fallback = size <= 1e-20;
				const auto n = fallback ? model_geometry::P3{0, 0, 1} : sums[v] * (1 / size);
				frame.normals[v] = {float(n.x), float(n.y), float(n.z)};
				plan->changes.append({ModelImportRepairKind::RebuildNormal, s, pose, v, -1, fallback});
				++plan->rebuiltNormals;
				if (fallback)
					++plan->fallbackNormals;
			}
		}
		const auto edges = modelSurfaceEdges(surface, control);
		if (!work.check())
			return false;
		auto seams = surface.uvSeams.values();
		std::sort(seams.begin(), seams.end());
		for (auto edge : seams)
		{
			if (!work.step())
				return false;
			if (!std::binary_search(edges.begin(), edges.end(), edge))
			{
				surface.uvSeams.remove(edge);
				plan->changes.append({ModelImportRepairKind::RemoveSeam, s, -1, edge.first, edge.second});
				++plan->removedSeams;
			}
		}
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Editing, s + 1, mesh.surfaces.size(), error))
			return false;
	}
	const auto problems = validateEditableModel(mesh, control);
	if (!problems.isEmpty())
		return fail(error, problems.join(QLatin1Char('\n')));
	updateEditableModelMetadata(&mesh);
	return work.check();
}
} // namespace

QString modelImportRepairKindId(ModelImportRepairKind kind)
{
	switch (kind)
	{
	case ModelImportRepairKind::InvalidIndex:
		return QStringLiteral("invalid-index");
	case ModelImportRepairKind::RepeatedVertex:
		return QStringLiteral("repeated-vertex");
	case ModelImportRepairKind::CollapsedFace:
		return QStringLiteral("collapsed-face");
	case ModelImportRepairKind::RebuildNormal:
		return QStringLiteral("rebuild-normal");
	case ModelImportRepairKind::RemoveSeam:
		return QStringLiteral("remove-seam");
	}
	return {};
}
bool prepareModelImportRepair(const QString &path, ModelImportRepairPlan *output, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!output)
		return fail(error, Text::tr("Import repair requires an output plan."));
	ModelImportRepairPlan plan;
	plan.inputPath = QFileInfo(path).absoluteFilePath();
	plan.resolvedPath = QFileInfo(path).canonicalFilePath();
	QByteArray bytes;
	if (!readModelFile(path, &bytes, error, control))
		return false;
	if (path.endsWith(".mesh.json", Qt::CaseInsensitive))
	{
		if (!decodeEditableModelSource(bytes, &plan.mesh, error, control))
			return false;
	}
	else if (path.endsWith(".mdl", Qt::CaseInsensitive) || path.endsWith(".md2", Qt::CaseInsensitive) ||
			 path.endsWith(".md3", Qt::CaseInsensitive))
	{
		plan.mesh = decodeModelMesh(path, bytes, nullptr, control);
		if (!plan.mesh.error.isEmpty())
			return fail(error, plan.mesh.error);
	}
	else
		return fail(error, Text::tr("Repair Import accepts .mesh.json, MDL, MD2 and MD3 sources. Other formats require normal import."));
	if (!prepareGeometry(&plan, error, control))
		return false;
	if (!sameSourceLocation(plan))
		return fail(error, Text::tr("The repair input changed while it was being read."));
	plan.sourceSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 1, 1, error))
		return false;
	*output = std::move(plan);
	return true;
}
bool saveModelImportRepair(const ModelImportRepairPlan &plan, const QString &output, ModelDocument *document, QString *error,
						   const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!document || !output.endsWith(".mesh.json", Qt::CaseInsensitive) || plan.sourceSha256.size() != 32)
		return fail(error, Text::tr("Save a reviewed repair to a new .mesh.json source."));
	if (modelPathsReferToSameFile(output, plan.inputPath) || QFileInfo::exists(output))
		return fail(error, Text::tr("A repaired copy requires a new destination. The input and existing files cannot be replaced."));
	const auto sourceUnchanged = [&](QString *failure) {
		QByteArray bytes;
		if (!readModelFile(plan.inputPath, &bytes, failure, control))
			return false;
		if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != plan.sourceSha256 || !sameSourceLocation(plan))
			return fail(failure, Text::tr("The source changed after repair review. Inspect it again before saving a copy."));
		return true;
	};
	if (!sourceUnchanged(error))
		return false;
	ModelDocument repaired;
	if (!repaired.setMesh(plan.mesh, error, control))
		return false;
	bool rejected = false;
	QString sourceFailure;
	// Recheck after serialization and temporary-file writing, at the shared
	// writer's final cancellable checkpoint. Never fail after publication.
	ModelWorkControl guarded{[&] { return rejected || (control.cancelled && control.cancelled()); },
							 [&](ModelWorkPhase phase, qint64 completed, qint64 total) {
								 if (control.progress)
									 control.progress(phase, completed, total);
								 if (phase == ModelWorkPhase::Committing && completed == 0 && total == 1)
									 rejected = !sourceUnchanged(&sourceFailure);
							 }};
	if (!repaired.save(output, false, error, guarded))
		return rejected ? fail(error, sourceFailure) : false;
	*document = std::move(repaired);
	return true;
}
QJsonObject modelImportRepairJson(const ModelImportRepairPlan &plan)
{
	struct NormalRows
	{
		QJsonArray vertices, fallback;
	};
	struct SurfaceRows
	{
		QJsonArray faces, seams;
		QMap<int, NormalRows> normals;
	};
	QVector<SurfaceRows> rows(plan.mesh.surfaces.size());
	for (const auto &change : plan.changes)
	{
		auto &row = rows[change.surface];
		if (change.kind == ModelImportRepairKind::RebuildNormal)
		{
			auto &normals = row.normals[change.frame];
			normals.vertices.append(change.element);
			if (change.fallback)
				normals.fallback.append(change.element);
		}
		else if (change.kind == ModelImportRepairKind::RemoveSeam)
			row.seams.append(QJsonArray{change.element, change.other});
		else
			row.faces.append(QJsonObject{
				{"face", change.element}, {"reason", modelImportRepairKindId(change.kind)}, {"firstInvalidPose", change.frame}});
	}
	QJsonArray surfaces;
	for (int s = 0; s < rows.size(); ++s)
	{
		QJsonArray normals;
		for (auto it = rows[s].normals.cbegin(); it != rows[s].normals.cend(); ++it)
			normals.append(QJsonObject{{"frame", it.key()}, {"vertices", it->vertices}, {"fallbackVertices", it->fallback}});
		surfaces.append(QJsonObject{{"surface", s},
									{"name", plan.mesh.surfaces[s].name},
									{"removedFaces", rows[s].faces},
									{"rebuiltNormals", normals},
									{"removedSeams", rows[s].seams}});
	}
	return {{"source", plan.inputPath},
			{"sourceSha256", QString::fromLatin1(plan.sourceSha256.toHex())},
			{"complete", true},
			{"removedFaces", plan.removedFaces},
			{"rebuiltNormals", plan.rebuiltNormals},
			{"fallbackNormals", plan.fallbackNormals},
			{"removedSeams", plan.removedSeams},
			{"surfaces", surfaces}};
}
} // namespace vibestudio
