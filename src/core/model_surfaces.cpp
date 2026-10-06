#include "core/model_surfaces.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSurfaces)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool capacity(const ModelMesh &mesh, qint64 extraVertices, qint64 extraTriangles, QString *error)
{
	qint64 vertices = extraVertices, triangles = extraTriangles;
	for (const auto &surface : mesh.surfaces)
	{
		vertices += surface.vertexCount;
		triangles += surface.triangles.size();
	}
	return (vertices <= modelDocumentMaxVertices && vertices * mesh.frames.size() <= modelDocumentMaxFrameVertices &&
			triangles <= modelDocumentMaxTriangles) ||
		   fail(error, Text::tr("The surface edit would exceed the vertex, triangle or animation storage limit."));
}
bool validName(const ModelMesh &mesh, const QString &name, int excluding, QString *error)
{
	if (name.isEmpty() || name.size() > 128 ||
		std::any_of(name.cbegin(), name.cend(), [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; }))
		return fail(error, Text::tr("Enter a surface name of 1–128 characters without control characters."));
	for (int i = 0; i < mesh.surfaces.size(); ++i)
		if (i != excluding && mesh.surfaces[i].name == name)
			return fail(error, Text::tr("A surface named %1 already exists.").arg(name));
	return true;
}
bool selectFaces(ModelSelection *selection, int surface, int first, int count, ModelWorkProgress &work)
{
	*selection = {surface, {}, {}};
	for (int face = first; face < first + count; ++face)
	{
		if (!work.step())
			return false;
		selection->faces.insert(face);
	}
	return true;
}
// Retain original unused vertices in the source; shared boundary vertices are
// copied, never welded or averaged. Maps retain source order deterministically.
bool partition(const ModelSurface &source, const QSet<int> &faces, ModelSurface *remaining, ModelSurface *moved, const ModelMesh &mesh,
			   QString *error, ModelWorkProgress &work)
{
	QVector<unsigned char> used(source.vertexCount, 0);
	for (int i = 0; i < source.triangles.size(); ++i)
	{
		if (!work.step())
			return false;
		const auto t = source.triangles[i];
		const auto flag = faces.contains(i) ? 1 : 2;
		for (int v : {t.a, t.b, t.c})
			used[v] |= flag;
	}
	const auto extra = std::count(used.cbegin(), used.cend(), 3);
	if (!capacity(mesh, extra, 0, error))
		return false;
	for (int part = 0; part < 2; ++part)
	{
		auto &output = part == 0 ? *remaining : *moved;
		output.name = source.name;
		output.skinPaths = source.skinPaths;
		output.frames.resize(source.frames.size());
		QVector<int> map(source.vertexCount, -1);
		for (int v = 0; v < source.vertexCount; ++v)
		{
			if (!work.step())
				return false;
			if (part == 0 ? used[v] == 1 : !(used[v] & 1))
				continue;
			map[v] = output.texCoords.size();
			output.texCoords.append(source.texCoords[v]);
			for (int f = 0; f < source.frames.size(); ++f)
			{
				if (!work.step())
					return false;
				output.frames[f].positions.append(source.frames[f].positions[v]);
				output.frames[f].normals.append(source.frames[f].normals[v]);
			}
		}
		output.vertexCount = output.texCoords.size();
		for (int i = 0; i < source.triangles.size(); ++i)
		{
			if (!work.step())
				return false;
			if (faces.contains(i) != (part == 1))
				continue;
			const auto t = source.triangles[i];
			output.triangles.append({map[t.a], map[t.b], map[t.c]});
			for (const auto &edge : {ModelEdge{t.a, t.b}, ModelEdge{t.b, t.c}, ModelEdge{t.c, t.a}})
			{
				const auto a = std::min(edge.first, edge.second), b = std::max(edge.first, edge.second);
				if (source.uvSeams.contains({a, b}))
					output.uvSeams.insert({map[a], map[b]});
			}
		}
	}
	return true;
}
bool appendSurface(ModelSurface *target, const ModelSurface &source, ModelWorkProgress &work)
{
	const int offset = target->vertexCount;
	for (const auto uv : source.texCoords)
	{
		if (!work.step())
			return false;
		target->texCoords.append(uv);
	}
	for (int f = 0; f < source.frames.size(); ++f)
		for (int v = 0; v < source.vertexCount; ++v)
		{
			if (!work.step())
				return false;
			target->frames[f].positions.append(source.frames[f].positions[v]);
			target->frames[f].normals.append(source.frames[f].normals[v]);
		}
	for (const auto t : source.triangles)
	{
		if (!work.step())
			return false;
		target->triangles.append({t.a + offset, t.b + offset, t.c + offset});
	}
	for (const auto &edge : source.uvSeams)
	{
		if (!work.step())
			return false;
		target->uvSeams.insert({edge.first + offset, edge.second + offset});
	}
	target->vertexCount += source.vertexCount;
	return true;
}
} // namespace
bool isModelSurfaceEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::RenameSurface || kind == ModelEditKind::SeparateFaces || kind == ModelEditKind::MoveFacesToSurface ||
		   kind == ModelEditKind::DuplicateSurface || kind == ModelEditKind::DeleteSurface || kind == ModelEditKind::JoinSurfaces;
}
bool applyModelSurfaceEdit(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error,
						   const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	const int active = edit.selection.surface, target = edit.targetSurface;
	const bool separate = edit.kind == ModelEditKind::SeparateFaces, move = edit.kind == ModelEditKind::MoveFacesToSurface;
	const bool join = edit.kind == ModelEditKind::JoinSurfaces;
	const bool named = separate || edit.kind == ModelEditKind::DuplicateSurface || edit.kind == ModelEditKind::RenameSurface;
	if (!mesh || !selection || !isModelSurfaceEdit(edit.kind) || active < 0 || active >= mesh->surfaces.size())
		return fail(error, Text::tr("Select a valid source surface."));
	if (edit.frame != -1)
		return fail(error, Text::tr("Surface operations must include every animation pose."));
	if (!edit.selection.tag.isEmpty() || !edit.selection.collision.isEmpty())
		return fail(error, Text::tr("Select mesh components before managing surfaces."));
	if ((!join && !edit.surfaces.isEmpty()) || (!(join || move) && (target != -1 || edit.adoptTargetMaterials)) ||
		(!named && !edit.text.isEmpty()))
		return fail(error, Text::tr("The surface operation contains unrelated name, target, material or join options."));
	if ((move || join) && (target < 0 || target >= mesh->surfaces.size() || (move && target == active)))
		return fail(error, Text::tr("Choose a valid target surface; moving faces requires a different surface."));
	const auto source = mesh->surfaces[active];
	if (move || separate)
	{
		if (edit.selection.faces.isEmpty() || !edit.selection.vertices.isEmpty() || !edit.selection.edges.isEmpty())
			return fail(error, Text::tr("Select faces only to separate or move geometry."));
		for (int face : edit.selection.faces)
		{
			if (!work.step())
				return false;
			if (face < 0 || face >= source.triangles.size())
				return fail(error, Text::tr("A selected face no longer exists."));
		}
	}
	const bool all = edit.selection.faces.size() == source.triangles.size();
	if (named && !validName(*mesh, edit.text, edit.kind == ModelEditKind::RenameSurface || (separate && all) ? active : -1, error))
		return false;
	if ((edit.kind == ModelEditKind::DuplicateSurface || (separate && !all)) && mesh->surfaces.size() >= modelDocumentMaxSurfaces)
		return fail(error, Text::tr("The editable model supports at most %1 surfaces.").arg(modelDocumentMaxSurfaces));
	if (join)
	{
		if (edit.surfaces.size() < 2 || !edit.surfaces.contains(target))
			return fail(error, Text::tr("Select at least two surfaces to join, including the target."));
		for (int index : edit.surfaces)
			if (index < 0 || index >= mesh->surfaces.size())
				return fail(error, Text::tr("A selected surface no longer exists."));
	}
	if ((move || join) && !edit.adoptTargetMaterials)
	{
		const auto sources = move ? QSet<int>{active} : edit.surfaces;
		for (int index : sources)
			if (mesh->surfaces[index].skinPaths != mesh->surfaces[target].skinPaths)
				return fail(
					error,
					Text::tr("Surface material bindings differ. Explicitly choose target materials to replace the incoming bindings."));
	}
	if (edit.kind == ModelEditKind::RenameSurface || (separate && all))
	{
		mesh->surfaces[active].name = edit.text;
		*selection = edit.selection;
	}
	else if (edit.kind == ModelEditKind::DuplicateSurface)
	{
		if (!capacity(*mesh, source.vertexCount, source.triangles.size(), error))
			return false;
		auto duplicate = source;
		duplicate.name = edit.text;
		mesh->surfaces.append(std::move(duplicate));
		if (!selectFaces(selection, mesh->surfaces.size() - 1, 0, source.triangles.size(), work))
			return false;
	}
	else if (edit.kind == ModelEditKind::DeleteSurface)
	{
		if (mesh->surfaces.size() == 1)
			return fail(error, Text::tr("The last surface cannot be deleted."));
		mesh->surfaces.removeAt(active);
		*selection = {std::min(active, int(mesh->surfaces.size()) - 1), {}, {}};
	}
	else if (move || separate)
	{
		ModelSurface remaining, moved;
		if (all)
			moved = source; // Preserve unused vertices when the entire surface moves.
		else if (!partition(source, edit.selection.faces, &remaining, &moved, *mesh, error, work))
			return false;
		if (separate)
		{
			moved.name = edit.text;
			mesh->surfaces[active] = std::move(remaining);
			mesh->surfaces.append(std::move(moved));
			if (!selectFaces(selection, mesh->surfaces.size() - 1, 0, edit.selection.faces.size(), work))
				return false;
		}
		else
		{
			const int first = mesh->surfaces[target].triangles.size();
			if (!appendSurface(&mesh->surfaces[target], moved, work))
				return false;
			if (all)
				mesh->surfaces.removeAt(active);
			else
				mesh->surfaces[active] = std::move(remaining);
			if (!selectFaces(selection, target - int(all && active < target), first, edit.selection.faces.size(), work))
				return false;
		}
	}
	else if (join)
	{
		auto combined = mesh->surfaces[target];
		for (int i = 0; i < mesh->surfaces.size(); ++i)
			if (i != target && edit.surfaces.contains(i) && !appendSurface(&combined, mesh->surfaces[i], work))
				return false;
		mesh->surfaces[target] = std::move(combined);
		int newTarget = target;
		for (int i = mesh->surfaces.size() - 1; i >= 0; --i)
			if (i != target && edit.surfaces.contains(i))
			{
				mesh->surfaces.removeAt(i);
				newTarget -= int(i < target);
			}
		if (!selectFaces(selection, newTarget, 0, mesh->surfaces[newTarget].triangles.size(), work))
			return false;
		if (!edit.selection.surfaces.isEmpty())
		{
			selection->faces.clear();
			selection->surfaces = {newTarget};
		}
	}
	return work.check();
}
} // namespace vibestudio
