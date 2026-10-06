#include "core/model_uv_mapping.h"
#include "core/model_topology.h"
#include <QCoreApplication>
#include <array>

namespace vibestudio
{
bool applyModelUvMapping(const ModelSurface &source, const QVector<int> &faces, const QVector<int> &sourceVertices,
						 const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, int vertexLimit, ModelSurface *result,
						 QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	const auto corners = [](ModelTriangle t) { return std::array<int, 3>{t.a, t.b, t.c}; };
	auto candidate = source;
	const QSet<int> selected(faces.cbegin(), faces.cend());
	QSet<int> outside;
	for (int face = 0; face < source.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		if (!selected.contains(face))
			for (int v : corners(source.triangles[face]))
				outside.insert(v);
	}
	QVector<int> mapping(uv.size(), -1);
	QSet<int> reused;
	for (int v = 0; v < uv.size(); ++v)
	{
		if (!work.step())
			return false;
		const int old = sourceVertices[v];
		int index = old;
		if (outside.contains(old) || reused.contains(old))
		{
			index = candidate.texCoords.size();
			if (index >= vertexLimit || qint64(index + 1) * candidate.frames.size() > 1048576)
			{
				if (error)
					*error = QCoreApplication::translate("VibeStudioModelUvAtlas",
														 "Atlas corner splits would exceed the vertex or animation storage limit.");
				return false;
			}
			candidate.texCoords.append(uv[v]);
			for (int f = 0; f < candidate.frames.size(); ++f)
			{
				if (!work.step())
					return false;
				candidate.frames[f].positions.append(source.frames[f].positions[old]);
				candidate.frames[f].normals.append(source.frames[f].normals[old]);
			}
		}
		else
		{
			candidate.texCoords[index] = uv[v];
			reused.insert(old);
		}
		mapping[v] = index;
	}
	for (int i = 0; i < faces.size(); ++i)
	{
		if (!work.step())
			return false;
		candidate.triangles[faces[i]] = {mapping[indices[i * 3]], mapping[indices[i * 3 + 1]], mapping[indices[i * 3 + 2]]};
	}
	candidate.uvSeams.clear();
	for (int face = 0; face < source.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto before = corners(source.triangles[face]), after = corners(candidate.triangles[face]);
		for (int i = 0; i < 3; ++i)
			if (source.uvSeams.contains(modelEdge(before[i], before[(i + 1) % 3])))
				candidate.uvSeams.insert(modelEdge(after[i], after[(i + 1) % 3]));
	}
	candidate.vertexCount = candidate.texCoords.size();
	if (!work.check())
		return false;
	*result = std::move(candidate);
	return true;
}
} // namespace vibestudio
