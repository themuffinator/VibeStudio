#include "core/model_document.h"
#include "core/model_source_decode.h"
#include "core/model_geometry_helpers.h"
#include "core/model_surface_selection.h"
#include "core/model_material_slots.h"
#include "core/model_surfaces.h"
#include "core/model_skin_bindings.h"
#include "core/model_collision.h"
#include "core/model_fingerprint.h"
#include "core/model_animation.h"
#include "core/model_mdl.h"
#include "core/model_tags.h"
#include "core/model_topology_health.h"
#include "core/model_boundary_fill.h"
#include "core/model_boundary_bridge.h"
#include "core/model_transform.h"
#include "core/model_transform_axes.h"
#include "core/model_uv_atlas.h"
#include "core/model_uv_obstacles.h"

#include "core/model_design.h"
#include "core/package_staging.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
bool finite(float n) { return std::isfinite(n) && std::abs(n) <= 1000000.0f; }

bool finite(const ModelVec3 &p) { return finite(p.x) && finite(p.y) && finite(p.z); }
ModelVec3 add(ModelVec3 a, ModelVec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
ModelVec3 subtract(ModelVec3 a, ModelVec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
ModelVec3 multiply(ModelVec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
double lengthSquared(ModelVec3 v) { return double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z; }
ModelVec3 cross(ModelVec3 a, ModelVec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
ModelVec3 normal(ModelVec3 a)
{
	const auto length = std::sqrt(lengthSquared(a));
	return length > 1e-20 ? multiply(a, static_cast<float>(1 / length)) : ModelVec3{0, 0, 1};
}
QJsonArray xyz(ModelVec3 p) { return {p.x, p.y, p.z}; }
QJsonArray uv(ModelTexCoord p) { return {p.u, p.v}; }
bool readNumbers(const QJsonValue &value, int count, float *numbers)
{
	if (!value.isArray())
	{
		return false;
	}
	const auto array = value.toArray();
	if (array.size() != count)
	{
		return false;
	}
	for (int i = 0; i < count; ++i)
	{
		if (!array[i].isDouble())
		{
			return false;
		}
		numbers[i] = static_cast<float>(array[i].toDouble());
		if (!finite(numbers[i]))
		{
			return false;
		}
	}
	return true;
}
bool readVector(const QJsonValue &value, ModelVec3 *p)
{
	float n[3];
	if (!readNumbers(value, 3, n))
	{
		return false;
	}
	*p = {n[0], n[1], n[2]};
	return true;
}
bool nameOk(const QString &name, bool allowEmpty = false)
{
	return (allowEmpty || !name.isEmpty()) && name.size() <= 128 &&
		   std::none_of(name.cbegin(), name.cend(), [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; });
}
QByteArray digest(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	return modelStateFingerprint(mesh, error, control);
}
qint64 storageBytes(const ModelMesh &mesh)
{
	qint64 bytes = modelMdlStorageBytes(mesh);
	for (const auto &surface : mesh.surfaces)
	{
		bytes += surface.triangles.size() * qint64(sizeof(ModelTriangle));
		bytes += surface.texCoords.size() * qint64(sizeof(ModelTexCoord));
		bytes += surface.uvSeams.size() * qint64(sizeof(ModelEdge) + 3 * sizeof(void *));
		for (const auto &frame : surface.frames)
		{
			bytes += (frame.positions.size() + frame.normals.size()) * qint64(sizeof(ModelVec3));
		}
	}
	for (const auto &skin : mesh.embeddedSkins)
	{
		bytes += skin.image.sizeInBytes();
	}
	for (const auto &tag : mesh.tags)
	{
		bytes += sizeof(ModelTag) + tag.name.size() * qint64(sizeof(QChar));
	}
	for (const auto &frame : mesh.frames)
	{
		bytes += sizeof(ModelFrameInfo) + frame.name.size() * qint64(sizeof(QChar));
	}
	for (const auto &clip : mesh.animations)
	{
		bytes += sizeof(ModelAnimation) + clip.name.size() * qint64(sizeof(QChar));
	}
	for (const auto &box : mesh.collisionBoxes)
	{
		bytes += sizeof(ModelCollisionBox) + box.name.size() * qint64(sizeof(QChar)) + box.framePoses.size() * qint64(sizeof(ModelCollisionPose));
	}
	return bytes;
}
qint64 selectionBytes(const ModelSelection &selection)
{
	// Include a conservative per-entry hash-node estimate in retained history.
	return qint64(selection.vertices.size() + selection.faces.size() + selection.surfaces.size()) * (sizeof(int) + 3 * sizeof(void *)) +
		   qint64(selection.edges.size()) * (sizeof(ModelEdge) + 3 * sizeof(void *)) +
		   (selection.tag.size() + selection.collision.size()) * qint64(sizeof(QChar));
}
bool recalculate(ModelSurface *surface, ModelWorkProgress &work, int onlyFrame = -1)
{
	for (int f = 0; f < surface->frames.size(); ++f)
	{
		if (!work.step())
		{
			return false;
		}
		if (onlyFrame >= 0 && onlyFrame != f)
		{
			continue;
		}
		auto &frame = surface->frames[f];
		frame.normals.fill({}, frame.positions.size());
		for (const auto &t : std::as_const(surface->triangles))
		{
			if (!work.step())
			{
				return false;
			}
			const auto n =
				cross(subtract(frame.positions[t.b], frame.positions[t.a]), subtract(frame.positions[t.c], frame.positions[t.a]));
			for (int i : {t.a, t.b, t.c})
			{
				if (!work.step())
				{
					return false;
				}
				frame.normals[i] = add(frame.normals[i], n);
			}
		}
		for (auto &n : frame.normals)
		{
			if (!work.step())
			{
				return false;
			}
			n = normal(n);
		}
	}
	return work.check();
}
QVector<int> sorted(const QSet<int> &set)
{
	auto result = set.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
QSet<int> selectedVertices(const ModelSurface &surface, const ModelSelection &selection)
{
	auto vertices = selection.vertices;
	for (auto edge : selection.edges)
	{
		vertices.insert(edge.first);
		vertices.insert(edge.second);
	}
	for (int f : selection.faces)
	{
		const auto &t = surface.triangles[f];
		vertices.insert(t.a);
		vertices.insert(t.b);
		vertices.insert(t.c);
	}
	return vertices;
}
ModelTexCoord transformUv(ModelTexCoord p, const ModelEdit &edit, ModelTexCoord pivot, ModelTexCoord offset)
{
	const double radians = edit.uvRotation * std::numbers::pi / 180;
	const double u = (double(p.u) - pivot.u) * edit.uvScale.u, v = (double(p.v) - pivot.v) * edit.uvScale.v;
	return {float(u * std::cos(radians) - v * std::sin(radians) + pivot.u + offset.u),
			float(u * std::sin(radians) + v * std::cos(radians) + pivot.v + offset.v)};
}
bool copyUvSeams(ModelSurface *surface, const QMap<int, int> &copies, ModelWorkProgress &work)
{
	const auto original = surface->uvSeams;
	for (auto edge : original)
	{
		if (!work.step())
		{
			return false;
		}
		const int a = copies.value(edge.first, edge.first), b = copies.value(edge.second, edge.second);
		if (a != b)
		{
			surface->uvSeams.insert(modelEdge(a, b));
		}
	}
	return true;
}
// Only remove unreferenced vertices during explicit topology edits. Keeping
// original indices on import preserves seams, normals, and selection identity.
bool compact(ModelSurface *surface, ModelSelection *selection, ModelWorkProgress &work)
{
	QSet<int> used;
	for (const auto &t : surface->triangles)
	{
		if (!work.step())
		{
			return false;
		}
		used.insert(t.a);
		used.insert(t.b);
		used.insert(t.c);
	}
	QVector<int> mapping(surface->vertexCount, -1);
	const auto indices = sorted(used);
	QVector<ModelTexCoord> coords;
	for (int i : indices)
	{
		if (!work.step())
		{
			return false;
		}
		mapping[i] = coords.size();
		coords << surface->texCoords[i];
	}
	for (auto &frame : surface->frames)
	{
		if (!work.step())
		{
			return false;
		}
		QVector<ModelVec3> positions, normals;
		for (int i : indices)
		{
			if (!work.step())
			{
				return false;
			}
			positions << frame.positions[i];
			normals << frame.normals[i];
		}
		frame.positions = std::move(positions);
		frame.normals = std::move(normals);
	}
	for (auto &t : surface->triangles)
	{
		if (!work.step())
		{
			return false;
		}
		t = {mapping[t.a], mapping[t.b], mapping[t.c]};
	}
	QSet<int> vertices;
	for (int i : selection->vertices)
	{
		if (!work.step())
		{
			return false;
		}
		if (mapping[i] >= 0)
		{
			vertices.insert(mapping[i]);
		}
	}
	selection->vertices = std::move(vertices);
	QSet<ModelEdge> edges;
	for (auto edge : selection->edges)
	{
		if (!work.step())
		{
			return false;
		}
		if (mapping[edge.first] >= 0 && mapping[edge.second] >= 0)
		{
			edges.insert(modelEdge(mapping[edge.first], mapping[edge.second]));
		}
	}
	selection->edges = std::move(edges);
	QSet<ModelEdge> seams;
	for (auto edge : surface->uvSeams)
	{
		if (!work.step())
		{
			return false;
		}
		if (mapping[edge.first] >= 0 && mapping[edge.second] >= 0 && mapping[edge.first] != mapping[edge.second])
		{
			seams.insert(modelEdge(mapping[edge.first], mapping[edge.second]));
		}
	}
	surface->uvSeams = std::move(seams);
	surface->texCoords = std::move(coords);
	surface->vertexCount = surface->texCoords.size();
	return work.check();
}
} // namespace

QStringList validateEditableModel(const ModelMesh &mesh, const ModelWorkControl &control)
{
	QStringList errors;
	QString cancelError;
	ModelWorkProgress work(control, ModelWorkPhase::Validating, &cancelError);
	if (!work.check())
	{
		return {cancelError};
	}
	if (!mesh.error.isEmpty() || !mesh.warnings.isEmpty() || !mesh.geometryAvailable)
	{
		errors << QCoreApplication::translate("VibeStudioModelDocument",
											  "An editable model requires complete geometry without decode errors or "
											  "warnings.");
	}
	if (mesh.frames.isEmpty() || mesh.frames.size() > modelDocumentMaxFrames || mesh.surfaces.isEmpty() || mesh.surfaces.size() > modelDocumentMaxSurfaces)
	{
		errors << QCoreApplication::translate("VibeStudioModelDocument", "A model needs 1–1024 frames and 1–32 surfaces.");
		return errors;
	}
	qint64 vertices = 0, triangles = 0;
	if (mesh.md2SkinSize.width() < 1 || mesh.md2SkinSize.width() > 8192 || mesh.md2SkinSize.height() < 1 ||
		mesh.md2SkinSize.height() > 8192)
	{
		errors << QCoreApplication::translate("VibeStudioModelDocument", "MD2 skin dimensions must be between 1 and 8192 pixels.");
	}
	QSet<QString> surfaceNames;
	for (const auto &surface : mesh.surfaces)
	{
		if (!work.step())
		{
			return {cancelError};
		}
		vertices += surface.vertexCount;
		triangles += surface.triangles.size();
		if (!nameOk(surface.name) || surfaceNames.contains(surface.name))
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "Surface names must be unique, nonempty, "
																			 "and at most 128 characters.");
		}
		surfaceNames.insert(surface.name);
		if (!surface.warnings.isEmpty() || surface.vertexCount < 3 || surface.vertexCount > modelDocumentMaxVertices ||
			surface.triangles.isEmpty() || surface.triangles.size() > modelDocumentMaxTriangles ||
			surface.frames.size() != mesh.frames.size() || surface.texCoords.size() != surface.vertexCount ||
			surface.uvSeams.size() > surface.triangles.size() * 3)
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "Surface %1 has incomplete or unsupported topology, UVs, "
																			 "or frames.")
						  .arg(surface.name);
			errors.append(surface.warnings);
			return errors;
		}
		if (vertices > modelDocumentMaxVertices || triangles > modelDocumentMaxTriangles ||
			vertices * mesh.frames.size() > modelDocumentMaxFrameVertices)
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument",
												  "The model exceeds 65,536 vertices, 131,072 triangles, or 1,048,576 "
												  "frame vertices.");
			return errors;
		}
		for (const auto &t : surface.triangles)
		{
			if (!work.step())
			{
				return {cancelError};
			}
			if (t.a < 0 || t.b < 0 || t.c < 0 || t.a >= surface.vertexCount || t.b >= surface.vertexCount || t.c >= surface.vertexCount ||
				t.a == t.b || t.b == t.c || t.c == t.a)
			{
				errors << QCoreApplication::translate("VibeStudioModelDocument", "Surface %1 contains an invalid triangle index.")
							  .arg(surface.name);
				return errors;
			}
		}
		if (!surface.uvSeams.isEmpty())
		{
			const auto edges = modelSurfaceEdges(surface, control);
			if (!work.check())
			{
				return {cancelError};
			}
			for (auto seam : surface.uvSeams)
			{
				if (!work.step())
				{
					return {cancelError};
				}
				if (seam.first >= seam.second || !std::binary_search(edges.cbegin(), edges.cend(), seam))
				{
					errors << QCoreApplication::translate("VibeStudioModelDocument",
														  "UV seam marks must refer to existing canonical edges.");
					return errors;
				}
			}
		}
		for (const auto &frame : surface.frames)
		{
			if (!work.step())
			{
				return {cancelError};
			}
			if (frame.positions.size() != surface.vertexCount || frame.normals.size() != surface.vertexCount)
			{
				errors << QCoreApplication::translate("VibeStudioModelDocument",
													  "Every frame must contain all vertex positions and normals.");
				return errors;
			}
			for (int i = 0; i < surface.vertexCount; ++i)
			{
				if (!work.step())
				{
					return {cancelError};
				}
				if (!finite(frame.positions[i]) || !finite(frame.normals[i]) || lengthSquared(frame.normals[i]) < 1e-12 ||
					!finite(surface.texCoords[i].u) || !finite(surface.texCoords[i].v))
				{
					errors << QCoreApplication::translate("VibeStudioModelDocument",
														  "Coordinates must be finite and within ±1,000,000; normals must "
														  "be nonzero.");
					return errors;
				}
			}
			for (const auto &t : surface.triangles)
			{
				if (!work.step())
				{
					return {cancelError};
				}
				if (model_geometry::collapsedTriangle(frame.positions[t.a], frame.positions[t.b], frame.positions[t.c]))
				{
					errors << QCoreApplication::translate("VibeStudioModelDocument", "Surface %1 contains a collapsed triangle in an "
																					 "animation frame.")
								  .arg(surface.name);
					return errors;
				}
			}
		}
		if (surface.skinPaths.size() > modelMaxMaterialSlots)
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "A surface can reference at most 256 skins.");
		}
		for (const auto &path : surface.skinPaths)
		{
			if (!work.step())
			{
				return {cancelError};
			}
			if (!isSafePackageVirtualPath(path) || path.size() > 255)
			{
				errors << QCoreApplication::translate("VibeStudioModelDocument",
													  "Material references must be safe package-relative paths.");
			}
		}
	}
	for (const auto &frame : mesh.frames)
	{
		if (!work.step())
		{
			return {cancelError};
		}
		if (!nameOk(frame.name, true) || !finite(frame.origin))
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "Frame names or origins are invalid.");
		}
	}
	QVector<QSet<QString>> tagsByFrame(mesh.frames.size());
	for (const auto &tag : mesh.tags)
	{
		if (!work.step())
		{
			return {cancelError};
		}
		if (tag.frameIndex < 0 || tag.frameIndex >= mesh.frames.size() || !nameOk(tag.name) || !finite(tag.origin))
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "Attachment tags require valid names, "
																			 "frame indices, and finite origins.");
			return errors;
		}
		auto &names = tagsByFrame[tag.frameIndex];
		if (names.contains(tag.name) || names.size() >= 16)
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "A frame requires unique tag names and at most 16 tags.");
			return errors;
		}
		names.insert(tag.name);
		for (int row = 0; row < 3; ++row)
		{
			if (!work.step())
			{
				return {cancelError};
			}
			for (int other = 0; other < 3; ++other)
			{
				if (!work.step())
				{
					return {cancelError};
				}
				double dot = 0;
				for (int column = 0; column < 3; ++column)
				{
					if (!work.step())
					{
						return {cancelError};
					}
					dot += double(tag.axis[row * 3 + column]) * tag.axis[other * 3 + column];
				}
				if (!std::isfinite(dot) || std::abs(dot - (row == other ? 1 : 0)) > 0.01)
				{
					errors << QCoreApplication::translate("VibeStudioModelDocument", "Tag axes must form an orthonormal orientation.");
					return errors;
				}
			}
		}
	}
	for (const auto &names : tagsByFrame)
	{
		if (!work.step())
		{
			return {cancelError};
		}
		if (names != tagsByFrame.first())
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "Each animation frame must contain the same attachment tags.");
			break;
		}
	}
	if (mesh.animations.size() > modelDocumentMaxAnimations)
	{
		errors << QCoreApplication::translate("VibeStudioModelDocument", "A model supports at most 1024 animation clips.");
		return errors;
	}
	for (const auto &animation : mesh.animations)
	{
		if (!work.step())
		{
			return {cancelError};
		}
		if (!nameOk(animation.name) || animation.firstFrame < 0 || animation.frameCount < 1 ||
			qint64(animation.firstFrame) + animation.frameCount > mesh.frames.size())
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "Animation ranges must fit the model's frames.");
		}
		if (!std::isfinite(animation.framesPerSecond) || animation.framesPerSecond < 0 || animation.framesPerSecond > 1000 ||
			(animation.framesPerSecond > 0 && animation.framesPerSecond < .001))
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "Clip FPS must be zero (unspecified) or from 0.001 to 1000.");
		}
	}
	qint64 skinPixels = 0;
	if (mesh.embeddedSkins.size() > 256)
	{
		errors << QCoreApplication::translate("VibeStudioModelDocument", "The model contains too many embedded skins.");
	}
	for (const auto &skin : mesh.embeddedSkins)
	{
		if (!work.step())
		{
			return {cancelError};
		}
		skinPixels += qint64(skin.image.width()) * skin.image.height();
		if (skin.image.isNull() || skinPixels > 16777216 || (!mesh.mdl.enabled && skin.groupFrameCount > 1) || !nameOk(skin.name))
		{
			errors << QCoreApplication::translate("VibeStudioModelDocument", "Embedded skins need valid images within 16 megapixels total; "
																			 "animated skin groups require retained MDL data.");
			break;
		}
	}
	errors += validateModelMdl(mesh, control);
	errors += validateModelCollision(mesh, control);
	if (!work.check())
	{
		return {cancelError};
	}
	return errors;
}

void updateEditableModelMetadata(ModelMesh *mesh)
{
	if (!mesh)
	{
		return;
	}
	mesh->frameCount = mesh->frames.size();
	mesh->surfaceCount = mesh->surfaces.size();
	mesh->vertexCount = 0;
	mesh->triangleCount = 0;
	mesh->skinPaths.clear();
	const float infinity = std::numeric_limits<float>::infinity();
	mesh->mins = {infinity, infinity, infinity};
	mesh->maxs = {-infinity, -infinity, -infinity};
	auto extend = [](ModelVec3 *mins, ModelVec3 *maxs, ModelVec3 p)
	{
		*mins = {std::min(mins->x, p.x), std::min(mins->y, p.y), std::min(mins->z, p.z)};
		*maxs = {std::max(maxs->x, p.x), std::max(maxs->y, p.y), std::max(maxs->z, p.z)};
	};
	for (int f = 0; f < mesh->frameCount; ++f)
	{
		auto &info = mesh->frames[f];
		info.index = f;
		info.mins = {infinity, infinity, infinity};
		info.maxs = {-infinity, -infinity, -infinity};
		double radius = 0;
		for (const auto &surface : mesh->surfaces)
		{
			if (f >= surface.frames.size())
			{
				continue;
			}
			for (const auto &p : surface.frames[f].positions)
			{
				extend(&info.mins, &info.maxs, p);
				extend(&mesh->mins, &mesh->maxs, p);
				radius = std::max(radius, lengthSquared(subtract(p, info.origin)));
			}
		}
		info.radius = static_cast<float>(std::sqrt(radius));
	}
	for (int s = 0; s < mesh->surfaceCount; ++s)
	{
		auto &surface = mesh->surfaces[s];
		surface.index = s;
		surface.vertexCount = surface.texCoords.size();
		mesh->vertexCount += surface.vertexCount;
		mesh->triangleCount += surface.triangles.size();
		mesh->skinPaths += surface.skinPaths;
	}
	mesh->skinPaths.removeDuplicates();
	mesh->skinCount = std::max(mesh->skinPaths.size(), mesh->embeddedSkins.size());
	mesh->tagCount = mesh->frameCount ? mesh->tags.size() / mesh->frameCount : 0;
	mesh->detailLines.clear();
}

bool importEditableModel(const QString &virtualPath, const QByteArray &bytes, ModelMesh *mesh, QString *error, const IdTechPalette *palette,
						 const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	if (!mesh || bytes.size() > modelDocumentMaxSourceBytes)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Model import requires an output and at most 64 MiB "
																				  "of source data."));
	}
	if (virtualPath.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive))
	{
		return parseEditableModel(bytes, mesh, error, control);
	}
	if (virtualPath.endsWith(QStringLiteral(".model.json"), Qt::CaseInsensitive))
	{
		ModelDesign design;
		if (!parseModelDesign(bytes, &design, error))
		{
			return false;
		}
		auto candidate = buildModelDesignMesh(design);
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 1, 1, error))
		{
			return false;
		}
		*mesh = std::move(candidate);
		return true;
	}
	auto result = decodeModelMesh(virtualPath, bytes, palette, control);
	if (!result.error.isEmpty())
	{
		return fail(error, result.error);
	}
	const auto errors = validateEditableModel(result, control);
	if (!errors.isEmpty())
	{
		return fail(error, errors.join(QLatin1Char('\n')));
	}
	updateEditableModelMetadata(&result);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 1, 1, error))
	{
		return false;
	}
	*mesh = std::move(result);
	return true;
}

QJsonObject editableModelJson(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	if (!work.check())
	{
		return {};
	}
	QJsonArray surfaces, frames, animations, tags, skins;
	for (const auto &surface : mesh.surfaces)
	{
		if (!work.step())
		{
			return {};
		}
		QJsonArray triangles, coords, poses, seams;
		auto seamEdges = surface.uvSeams.values();
		std::sort(seamEdges.begin(), seamEdges.end());
		for (auto edge : seamEdges)
		{
			if (!work.step())
			{
				return {};
			}
			seams << QJsonArray{edge.first, edge.second};
		}
		for (const auto &t : surface.triangles)
		{
			if (!work.step())
			{
				return {};
			}
			triangles << QJsonArray{t.a, t.b, t.c};
		}
		for (const auto &p : surface.texCoords)
		{
			if (!work.step())
			{
				return {};
			}
			coords << uv(p);
		}
		for (const auto &pose : surface.frames)
		{
			if (!work.step())
			{
				return {};
			}
			QJsonArray positions, normals;
			for (const auto &p : pose.positions)
			{
				if (!work.step())
				{
					return {};
				}
				positions << xyz(p);
			}
			for (const auto &n : pose.normals)
			{
				if (!work.step())
				{
					return {};
				}
				normals << xyz(n);
			}
			poses << QJsonObject{{QStringLiteral("positions"), positions}, {QStringLiteral("normals"), normals}};
		}
		surfaces << QJsonObject{
			{QStringLiteral("name"), surface.name},	  {QStringLiteral("materials"), QJsonArray::fromStringList(surface.skinPaths)},
			{QStringLiteral("triangles"), triangles}, {QStringLiteral("uvs"), coords},
			{QStringLiteral("uvSeams"), seams},		  {QStringLiteral("frames"), poses}};
	}
	for (const auto &frame : mesh.frames)
	{
		if (!work.step())
		{
			return {};
		}
		frames << QJsonObject{{QStringLiteral("name"), frame.name}, {QStringLiteral("origin"), xyz(frame.origin)}};
	}
	for (const auto &animation : mesh.animations)
	{
		if (!work.step())
		{
			return {};
		}
		QJsonObject clip{{QStringLiteral("name"), animation.name},
								  {QStringLiteral("first"), animation.firstFrame},
								  {QStringLiteral("count"), animation.frameCount}};
		if (animation.framesPerSecond > 0)
		{
			clip.insert(QStringLiteral("framesPerSecond"), animation.framesPerSecond);
		}
		animations << clip;
	}
	for (const auto &tag : mesh.tags)
	{
		if (!work.step())
		{
			return {};
		}
		QJsonArray axis;
		for (float n : tag.axis)
		{
			if (!work.step())
			{
				return {};
			}
			axis << n;
		}
		tags << QJsonObject{{QStringLiteral("name"), tag.name},
							{QStringLiteral("frame"), tag.frameIndex},
							{QStringLiteral("origin"), xyz(tag.origin)},
							{QStringLiteral("axis"), axis}};
	}
	for (const auto &skin : mesh.embeddedSkins)
	{
		if (!work.step())
		{
			return {};
		}
		if (mesh.mdl.enabled)
		{
			skins << modelMdlSkinJson(skin);
			continue;
		}
		QByteArray png;
		QBuffer buffer(&png);
		if (!work.check())
		{
			return {};
		}
		if (!buffer.open(QIODevice::WriteOnly) || !skin.image.save(&buffer, "PNG"))
		{
			fail(error, QCoreApplication::translate("VibeStudioModelDocument", "An embedded skin could not be encoded as PNG."));
			return {};
		}
		if (!work.check())
		{
			return {};
		}
		skins << QJsonObject{{QStringLiteral("name"), skin.name}, {QStringLiteral("png"), QString::fromLatin1(png.toBase64())}};
	}
	if (!work.check())
	{
		return {};
	}
	const bool timed = std::any_of(mesh.animations.cbegin(), mesh.animations.cend(), [](const auto &clip) { return clip.framesPerSecond > 0; });
	const bool collisionAnimation = std::any_of(mesh.collisionBoxes.cbegin(), mesh.collisionBoxes.cend(), [](const auto &box) { return !box.framePoses.isEmpty(); });
	QJsonObject result{{QStringLiteral("schema"), QStringLiteral("vibestudio.mesh")},
					   {QStringLiteral("version"), collisionAnimation ? 7 : timed ? 6 : !mesh.collisionBoxes.isEmpty() ? 5 : mesh.mdl.enabled ? 4 : 3},
					   {QStringLiteral("md2SkinSize"), QJsonArray{mesh.md2SkinSize.width(), mesh.md2SkinSize.height()}},
					   {QStringLiteral("source"), mesh.sourcePath},
					   {QStringLiteral("surfaces"), surfaces},
					   {QStringLiteral("frames"), frames},
					   {QStringLiteral("animations"), animations},
					   {QStringLiteral("tags"), tags},
					   {QStringLiteral("skins"), skins}};
	if (mesh.mdl.enabled)
	{
		result.insert(QStringLiteral("mdl"), modelMdlSettingsJson(mesh.mdl));
	}
	if (!mesh.collisionBoxes.isEmpty())
	{
		const auto boxes = modelCollisionJson(mesh, error, control);
		if (boxes.size() != mesh.collisionBoxes.size()) return {};
		result.insert(QStringLiteral("collisionBoxes"), boxes);
	}
	return work.check() ? result : QJsonObject{};
}

bool decodeEditableModelSource(const QByteArray &bytes, ModelMesh *mesh, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Reading, error);
	if (!work.check())
	{
		return false;
	}
	const auto malformed = [&]()
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
													   "The mesh source has malformed, missing, or unsupported fields."));
	};
	if (!mesh || bytes.size() > modelDocumentMaxSourceBytes)
	{
		return malformed();
	}
	QJsonParseError parseError;
	const auto json = QJsonDocument::fromJson(bytes, &parseError);
	if (!work.check())
	{
		return false;
	}
	const auto root = json.object();
	if (parseError.error != QJsonParseError::NoError || !json.isObject() ||
		root.value(QStringLiteral("schema")) != QStringLiteral("vibestudio.mesh") ||
		(root.value(QStringLiteral("version")) != QJsonValue(1) && root.value(QStringLiteral("version")) != QJsonValue(2) &&
		 root.value(QStringLiteral("version")) != QJsonValue(3) && root.value(QStringLiteral("version")) != QJsonValue(4) &&
		 root.value(QStringLiteral("version")) != QJsonValue(5) && root.value(QStringLiteral("version")) != QJsonValue(6) &&
		 root.value(QStringLiteral("version")) != QJsonValue(7)) ||
		!root.value(QStringLiteral("source")).isString())
	{
		return malformed();
	}
	for (const auto *key : {"surfaces", "frames", "animations", "tags", "skins"})
	{
		if (!work.step())
		{
			return false;
		}
		if (!root.value(QLatin1String(key)).isArray())
		{
			return malformed();
		}
	}
	if (root.value(QStringLiteral("animations")).toArray().size() > modelDocumentMaxAnimations)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "A model supports at most 1024 animation clips."));
	}
	ModelMesh result;
	result.geometryAvailable = true;
	result.formatId = QStringLiteral("mesh");
	result.version = root.value(QStringLiteral("version")).toInt();
	if (result.version >= 3 || root.contains(QStringLiteral("md2SkinSize")))
	{
		const auto size = root.value(QStringLiteral("md2SkinSize")).toArray();
		if (result.version < 3 || size.size() != 2 || !size[0].isDouble() || !size[1].isDouble() ||
			size[0].toDouble() != size[0].toInt(-1) || size[1].toDouble() != size[1].toInt(-1))
		{
			return malformed();
		}
		result.md2SkinSize = {size[0].toInt(), size[1].toInt()};
	}
	if (result.version == 4 || (result.version >= 5 && root.contains(QStringLiteral("mdl"))))
	{
		if (!parseModelMdlSettings(root.value(QStringLiteral("mdl")), &result.mdl, error, control))
		{
			return false;
		}
	}
	else if (root.contains(QStringLiteral("mdl")))
	{
		return malformed();
	}
	result.formatName = QStringLiteral("VibeStudio Mesh");
	result.sourcePath = root.value(QStringLiteral("source")).toString();
	if (result.sourcePath.size() > 4096)
	{
		return malformed();
	}
	const auto frames = root.value(QStringLiteral("frames")).toArray();
	const auto surfaces = root.value(QStringLiteral("surfaces")).toArray();
	if (frames.isEmpty() || frames.size() > modelDocumentMaxFrames || surfaces.isEmpty() || surfaces.size() > modelDocumentMaxSurfaces)
	{
		return malformed();
	}
	for (const auto &value : frames)
	{
		if (!work.step())
		{
			return false;
		}
		const auto object = value.toObject();
		ModelFrameInfo frame;
		if (!object.value(QStringLiteral("name")).isString() || !readVector(object.value(QStringLiteral("origin")), &frame.origin))
		{
			return malformed();
		}
		frame.name = object.value(QStringLiteral("name")).toString();
		result.frames << frame;
	}
	if (result.version == 5 || result.version == 7 || (result.version == 6 && root.contains(QStringLiteral("collisionBoxes"))))
	{
		if (!parseModelCollision(root.value(QStringLiteral("collisionBoxes")), &result, error, control)) return false;
	}
	else if (root.contains(QStringLiteral("collisionBoxes"))) return malformed();
	qint64 vertexCount = 0, triangleCount = 0;
	for (const auto &value : surfaces)
	{
		if (!work.step())
		{
			return false;
		}
		const auto object = value.toObject();
		ModelSurface surface;
		for (const auto *key : {"triangles", "uvs", "frames", "materials"})
		{
			if (!work.step())
			{
				return false;
			}
			if (!object.value(QLatin1String(key)).isArray())
			{
				return malformed();
			}
		}
		if (!object.value(QStringLiteral("name")).isString())
		{
			return malformed();
		}
		surface.name = object.value(QStringLiteral("name")).toString();
		const auto coords = object.value(QStringLiteral("uvs")).toArray(), triangles = object.value(QStringLiteral("triangles")).toArray(),
				   poses = object.value(QStringLiteral("frames")).toArray();
		vertexCount += coords.size();
		triangleCount += triangles.size();
		if (vertexCount > modelDocumentMaxVertices || triangleCount > modelDocumentMaxTriangles ||
			vertexCount * frames.size() > modelDocumentMaxFrameVertices || poses.size() != frames.size())
		{
			return malformed();
		}
		for (const auto &coord : coords)
		{
			if (!work.step())
			{
				return false;
			}
			float n[2];
			if (!readNumbers(coord, 2, n))
			{
				return malformed();
			}
			surface.texCoords << ModelTexCoord{n[0], n[1]};
		}
		surface.vertexCount = surface.texCoords.size();
		const auto seamsValue = object.value(QStringLiteral("uvSeams"));
		if ((result.version >= 2 && !seamsValue.isArray()) || (!seamsValue.isUndefined() && !seamsValue.isArray()) ||
			seamsValue.toArray().size() > triangles.size() * 3)
		{
			return malformed();
		}
		for (const auto &seam : seamsValue.toArray())
		{
			if (!work.step())
			{
				return false;
			}
			const auto edge = seam.toArray();
			if (edge.size() != 2 || edge[0].toInt(-1) < 0 || edge[1].toInt(-1) <= edge[0].toInt(-1) ||
				edge[0] != QJsonValue(edge[0].toInt(-1)) || edge[1] != QJsonValue(edge[1].toInt(-1)) ||
				surface.uvSeams.contains({edge[0].toInt(), edge[1].toInt()}))
			{
				return malformed();
			}
			surface.uvSeams.insert({edge[0].toInt(), edge[1].toInt()});
		}
		if (root.value(QStringLiteral("version")) == QJsonValue(1) && !surface.uvSeams.isEmpty())
		{
			return malformed();
		}
		for (const auto &triangle : triangles)
		{
			if (!work.step())
			{
				return false;
			}
			const auto a = triangle.toArray();
			if (a.size() != 3)
			{
				return malformed();
			}
			int indices[3];
			for (int i = 0; i < 3; ++i)
			{
				if (!work.step())
				{
					return false;
				}
				indices[i] = a[i].toInt(-1);
				if (QJsonValue(indices[i]) != a[i])
				{
					return malformed();
				}
			}
			surface.triangles << ModelTriangle{indices[0], indices[1], indices[2]};
		}
		for (const auto &valuePose : poses)
		{
			if (!work.step())
			{
				return false;
			}
			const auto pose = valuePose.toObject();
			ModelFrameGeometry frame;
			for (const auto *key : {"positions", "normals"})
			{
				if (!work.step())
				{
					return false;
				}
				if (!pose.value(QLatin1String(key)).isArray())
				{
					return malformed();
				}
				const auto array = pose.value(QLatin1String(key)).toArray();
				if (array.size() != coords.size())
				{
					return malformed();
				}
				auto &target = key[0] == 'p' ? frame.positions : frame.normals;
				for (const auto &item : array)
				{
					if (!work.step())
					{
						return false;
					}
					ModelVec3 p;
					if (!readVector(item, &p))
					{
						return malformed();
					}
					target << p;
				}
			}
			surface.frames << frame;
		}
		for (const auto &material : object.value(QStringLiteral("materials")).toArray())
		{
			if (!work.step())
			{
				return false;
			}
			if (!material.isString())
			{
				return malformed();
			}
			surface.skinPaths << material.toString();
		}
		result.surfaces << surface;
	}
	auto integer = [](const QJsonValue &value, int *output)
	{
		*output = value.toInt(-1);
		return *output >= 0 && QJsonValue(*output) == value;
	};
	for (const auto &value : root.value(QStringLiteral("animations")).toArray())
	{
		if (!work.step())
		{
			return false;
		}
		const auto object = value.toObject();
		ModelAnimation a;
		if (!object.value(QStringLiteral("name")).isString() || !integer(object.value(QStringLiteral("first")), &a.firstFrame) ||
			!integer(object.value(QStringLiteral("count")), &a.frameCount))
		{
			return malformed();
		}
		a.name = object.value(QStringLiteral("name")).toString();
		if (object.contains(QStringLiteral("framesPerSecond")))
		{
			const auto rate = object.value(QStringLiteral("framesPerSecond"));
			if (result.version < 6 || !rate.isDouble() || rate.toDouble() < .001 || rate.toDouble() > 1000)
			{
				return malformed();
			}
			a.framesPerSecond = rate.toDouble();
		}
		result.animations << a;
	}
	for (const auto &value : root.value(QStringLiteral("tags")).toArray())
	{
		if (!work.step())
		{
			return false;
		}
		const auto object = value.toObject();
		ModelTag tag;
		if (!object.value(QStringLiteral("name")).isString() || !integer(object.value(QStringLiteral("frame")), &tag.frameIndex) ||
			!readVector(object.value(QStringLiteral("origin")), &tag.origin) ||
			!readNumbers(object.value(QStringLiteral("axis")), 9, tag.axis))
		{
			return malformed();
		}
		tag.name = object.value(QStringLiteral("name")).toString();
		result.tags << tag;
		if (result.tags.size() > 16 * modelDocumentMaxFrames)
		{
			return malformed();
		}
	}
	qint64 pixels = 0;
	for (const auto &value : root.value(QStringLiteral("skins")).toArray())
	{
		if (!work.step())
		{
			return false;
		}
		const auto object = value.toObject();
		ModelEmbeddedSkin skin;
		if (result.mdl.enabled)
		{
			if (result.embeddedSkins.size() >= 256)
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model contains too many embedded skins."));
			}
			if (!parseModelMdlSkin(value, result.mdl, &skin, &pixels, error, control))
			{
				return false;
			}
			skin.index = result.embeddedSkins.size();
			result.embeddedSkins << skin;
			continue;
		}
		if (!object.value(QStringLiteral("name")).isString() || !object.value(QStringLiteral("png")).isString())
		{
			return malformed();
		}
		skin.name = object.value(QStringLiteral("name")).toString();
		skin.index = result.embeddedSkins.size();
		skin.groupFrameCount = 1;
		QByteArray png =
			QByteArray::fromBase64(object.value(QStringLiteral("png")).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
		QBuffer buffer(&png);
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer, "PNG");
		const auto size = reader.size();
		pixels += qint64(size.width()) * size.height();
		if (!size.isValid() || pixels > 16777216 || result.embeddedSkins.size() >= 256)
		{
			return malformed();
		}
		if (!work.check())
		{
			return false;
		}
		skin.image = reader.read();
		if (!work.check())
		{
			return false;
		}
		if (skin.image.isNull())
		{
			return malformed();
		}
		result.embeddedSkins << skin;
	}
	updateEditableModelMetadata(&result);
	if (!work.check())
	{
		return false;
	}
	*mesh = std::move(result);
	return true;
}

bool parseEditableModel(const QByteArray &bytes, ModelMesh *mesh, QString *error, const ModelWorkControl &control)
{
	ModelMesh result;
	if (!decodeEditableModelSource(bytes, mesh ? &result : nullptr, error, control))
		return false;
	const auto errors = validateEditableModel(result, control);
	if (!errors.isEmpty())
		return fail(error, errors.join(QLatin1Char('\n')));
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 1, 1, error))
		return false;
	*mesh = std::move(result);
	return true;
}

bool applyModelEdit(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *resultingSelection, QString *error,
					const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
	{
		return false;
	}
	if (!mesh)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "No editable model is open."));
	}
	const auto originalErrors = validateEditableModel(*mesh, control);
	if (!originalErrors.isEmpty())
	{
		return fail(error, originalErrors.join(QLatin1Char('\n')));
	}
	if (!validModelTransformAxesOptions(edit, error))
		return false;
	if (edit.kind != ModelEditKind::UpdateCollisionBox && edit.collisionFields != 7)
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Collision field masks apply only to box updates."));
	const bool frameOperation = edit.kind == ModelEditKind::DuplicateFrame || edit.kind == ModelEditKind::DeleteFrame ||
								edit.kind == ModelEditKind::RenameFrame || edit.kind == ModelEditKind::InsertInbetweens ||
								edit.kind == ModelEditKind::CopyFramePose;
	if (edit.kind != ModelEditKind::BridgeBoundaryLoops && edit.bridgeTwist != 0)
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "Bridge twist applies only to boundary bridging."));
	if (edit.kind != ModelEditKind::SetMaterialSlots && !edit.materialSlots.isEmpty())
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Material slot lists apply only to material slot edits."));
	if (!isModelSurfaceEdit(edit.kind) && (!edit.surfaces.isEmpty() || edit.targetSurface != -1 || edit.adoptTargetMaterials))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Surface targets and material adoption apply only to surface operations."));
	}
	const bool uvOperation = edit.kind == ModelEditKind::TransformUv || edit.kind == ModelEditKind::ProjectUv ||
							 edit.kind == ModelEditKind::DetachUv || edit.kind == ModelEditKind::UnwrapUv ||
							 edit.kind == ModelEditKind::PackUv || edit.kind == ModelEditKind::PackUvAround;
	if (edit.uvPreserveScale && edit.kind != ModelEditKind::PackUvAround)
		return fail(error, QCoreApplication::translate("ModelUvObstacles", "Preserving atlas scale applies only to Pack Around Unselected."));
	if (edit.uvTranslationGrid != 0 && edit.kind != ModelEditKind::TransformUv && edit.kind != ModelEditKind::ProjectUv)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
													   "UV grid snapping applies only to UV transforms and projection."));
	}
	if (edit.uvIslands && !uvOperation)
	{
		return fail(error, QCoreApplication::translate(
							   "VibeStudioModelDocument",
							   "Island expansion applies only to UV transforms, projection, detaching, unwrapping, or packing."));
	}
	if (edit.uvPivotMode == ModelUvPivot::IndividualIslands && edit.kind != ModelEditKind::TransformUv && edit.kind != ModelEditKind::ProjectUv)
	{
		return fail(error, QCoreApplication::translate("ModelUvTransform", "Individual island pivots apply only to UV transforms and projection."));
	}
	if (edit.kind != ModelEditKind::Transform && edit.kind != ModelEditKind::TransformTag && edit.kind != ModelEditKind::TransformCollisionBox &&
		(edit.translationGrid != 0 || edit.rotationGrid != 0 || edit.scaleGrid != 0))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Grid snapping applies only to position transforms."));
	}
	if (edit.selection.surface < 0 || edit.selection.surface >= mesh->surfaces.size() || edit.frame < -1 ||
		edit.frame >= mesh->frames.size() || (frameOperation && edit.frame < 0))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Select a valid surface and animation frame."));
	}
	if (!validModelSurfaceSelection(*mesh, edit.selection))
	{
		return fail(error, QCoreApplication::translate("ModelSurfaceSelection", "Select whole surfaces without mixing them with mesh components."));
	}
	if (!edit.selection.surfaces.isEmpty() && edit.kind != ModelEditKind::Transform && edit.kind != ModelEditKind::JoinSurfaces &&
		!frameOperation && !isModelAnimationEdit(edit.kind) && !isModelMdlEdit(edit.kind) &&
		edit.kind != ModelEditKind::SetMd2SkinSize && edit.kind != ModelEditKind::ApplySkinBindings && edit.kind != ModelEditKind::SetMaterialSlots)
	{
		return fail(error, QCoreApplication::translate("ModelSurfaceSelection", "This operation requires component selection. Switch to Faces, Vertices or Edges first."));
	}
	if (!validModelTagSelection(*mesh, edit.selection) || !validModelCollisionSelection(*mesh, edit.selection))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
													   "Select an existing attachment or collision box without mixing it with mesh components."));
	}
	if (!edit.selection.collision.isEmpty() && !isModelCollisionEdit(edit.kind) && !isModelAnimationEdit(edit.kind) && !frameOperation &&
		edit.kind != ModelEditKind::SetMd2SkinSize && edit.kind != ModelEditKind::ApplySkinBindings && edit.kind != ModelEditKind::SetMaterialSlots && !isModelMdlEdit(edit.kind))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
					"Select mesh components before editing geometry or UVs. A collision box is selected."));
	}
	if (!edit.selection.tag.isEmpty() && !isModelTagEdit(edit.kind) && !isModelAnimationEdit(edit.kind) && !frameOperation &&
		edit.kind != ModelEditKind::SetMd2SkinSize && edit.kind != ModelEditKind::ApplySkinBindings && edit.kind != ModelEditKind::SetMaterialSlots && !isModelMdlEdit(edit.kind) && !isModelCollisionEdit(edit.kind))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelDocument",
												"Select mesh components before editing geometry or UVs. An attachment tag is selected."));
	}
	const auto &source = mesh->surfaces[edit.selection.surface];
	for (int vertex : edit.selection.vertices)
	{
		if (!work.step())
		{
			return false;
		}
		if (vertex < 0 || vertex >= source.vertexCount)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "A selected vertex no longer exists."));
		}
	}
	for (int face : edit.selection.faces)
	{
		if (!work.step())
		{
			return false;
		}
		if (face < 0 || face >= source.triangles.size())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "A selected face no longer exists."));
		}
	}
	if (!edit.selection.edges.isEmpty())
	{
		const auto available = modelSurfaceEdges(source, control);
		if (!work.check())
		{
			return false;
		}
		for (auto edge : edit.selection.edges)
		{
			if (!work.step())
			{
				return false;
			}
			if (!std::binary_search(available.cbegin(), available.cend(), edge))
			{
				return fail(error,
							QCoreApplication::translate("VibeStudioModelDocument",
														"A selected edge no longer exists or its endpoints are not in ascending order."));
			}
		}
	}
	if (!finite(edit.translation) || !finite(edit.rotation) || !finite(edit.pivot) || !finite(edit.scale) || !finite(edit.uvScale.u) ||
		!finite(edit.uvScale.v) || !finite(edit.uvOffset.u) || !finite(edit.uvOffset.v) || !std::isfinite(edit.uvRotation) ||
		std::abs(edit.uvRotation) > 36000 || !finite(edit.uvPivot.u) || !finite(edit.uvPivot.v) || int(edit.uvPivotMode) < 0 ||
		int(edit.uvPivotMode) > int(ModelUvPivot::IndividualIslands))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Edit values must be finite and within the "
																				  "supported coordinate range."));
	}
	auto candidate = *mesh;
	auto selection = edit.selection;
	const bool surfaceTransform = edit.kind == ModelEditKind::Transform && !selection.surfaces.isEmpty();
	if (surfaceTransform || isModelSurfaceEdit(edit.kind) || isModelTagEdit(edit.kind) || isModelAnimationEdit(edit.kind) || isModelMdlEdit(edit.kind) || isModelCollisionEdit(edit.kind))
	{
		if (!(surfaceTransform ? applyModelSurfaceTransform(&candidate, edit, error, control)
			  : isModelSurfaceEdit(edit.kind) ? applyModelSurfaceEdit(&candidate, edit, &selection, error, control)
			  : isModelCollisionEdit(edit.kind) ? applyModelCollisionEdit(&candidate, edit, &selection, error, control)
			  : isModelTagEdit(edit.kind) ? applyModelTagEdit(&candidate, edit, &selection, error, control)
			  : isModelMdlEdit(edit.kind) ? applyModelMdlEdit(&candidate, edit, error, control)
										  : applyModelAnimationEdit(&candidate, edit, error, control)))
		{
			return false;
		}
		const auto errors = validateEditableModel(candidate, control);
		if (!errors.isEmpty())
		{
			return fail(error, errors.join(QLatin1Char('\n')));
		}
		updateEditableModelMetadata(&candidate);
		if (!work.check())
		{
			return false;
		}
		*mesh = std::move(candidate);
		if (resultingSelection)
		{
			*resultingSelection = std::move(selection);
		}
		return true;
	}
	auto &surface = candidate.surfaces[selection.surface];
	if (edit.uvIslands)
	{
		ModelUvTopology topology;
		QSet<int> expanded;
		if (!buildModelUvTopology(surface, &topology, error, control) ||
			!expandModelUvIslands(surface, topology, selection.faces, selection.vertices, selection.edges, &expanded, error, control))
		{
			return false;
		}
		selection = {selection.surface, {}, std::move(expanded)};
	}
	qint64 originalVertices = 0, otherTriangles = 0;
	for (int s = 0; s < mesh->surfaces.size(); ++s)
	{
		if (!work.step())
		{
			return false;
		}
		originalVertices += mesh->surfaces[s].vertexCount;
		if (s != selection.surface)
		{
			otherTriangles += mesh->surfaces[s].triangles.size();
		}
	}
	const auto vertexCapacity = [&](qint64 surfaceVertices)
	{
		const auto total = originalVertices - source.vertexCount + surfaceVertices;
		return (total <= modelDocumentMaxVertices && total * candidate.frames.size() <= modelDocumentMaxFrameVertices) ||
			   fail(error, QCoreApplication::translate("VibeStudioModelDocument",
													   "The edit's working geometry would exceed the vertex or animation storage limit."));
	};
	const auto triangleCapacity = [&](qint64 surfaceTriangles)
	{
		return otherTriangles + surfaceTriangles <= modelDocumentMaxTriangles ||
			   fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The edit would exceed the triangle limit."));
	};
	auto vertices = selectedVertices(surface, selection);
	const auto faces = sorted(selection.faces);
	const auto requireFaces = [&]()
	{ return !faces.isEmpty() || fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Select at least one face.")); };
	const auto requireVertices = [&]()
	{
		return !vertices.isEmpty() ||
			   fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Select at least one vertex, edge, or face."));
	};
	auto appendCopy = [&](int old)
	{
		if (!vertexCapacity(surface.texCoords.size() + 1))
		{
			return -1;
		}
		const int index = surface.texCoords.size();
		surface.texCoords << surface.texCoords[old];
		for (auto &frame : surface.frames)
		{
			if (!work.step())
			{
				return -1;
			}
			frame.positions << frame.positions[old];
			frame.normals << frame.normals[old];
		}
		return index;
	};
	switch (edit.kind)
	{
	case ModelEditKind::Transform:
	{
		ModelTransform transform;
		if (!snapModelTranslation(edit.translation, edit.translationGrid, &transform.translation, error) ||
			!snapModelRotation(edit.rotation, edit.rotationGrid, &transform.rotation, error) ||
			!snapModelScale(edit.scale, edit.scaleGrid, &transform.scale, error) ||
			!resolveModelTransformAxes(candidate, edit, &transform.basis, error, control))
		{
			return false;
		}
		if (!requireVertices())
		{
			return false;
		}
		const int reference = edit.frame >= 0 ? edit.frame : edit.pivotFrame;
		if (reference < 0 || reference >= surface.frames.size() ||
			!modelTransformPivot(surface.frames[reference].positions, vertices, edit.pivotMode, edit.pivot, &transform.pivot))
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelDocument", "Choose a valid transform pivot and reference frame."));
		}
		const bool mirrored = double(transform.scale.x) * transform.scale.y * transform.scale.z < 0;
		if (mirrored && (edit.frame >= 0 || vertices.size() != surface.vertexCount))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Mirroring requires the whole surface and all "
																					  "frames so winding remains consistent."));
		}
		for (int f = 0; f < surface.frames.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			if (edit.frame >= 0 && edit.frame != f)
			{
				continue;
			}
			auto &frame = surface.frames[f];
			for (int i : vertices)
			{
				if (!work.step())
				{
					return false;
				}
				frame.positions[i] = transformModelPoint(frame.positions[i], transform);
				frame.normals[i] = transformModelNormal(frame.normals[i], transform);
			}
			if (vertices.size() != surface.vertexCount)
			{
				if (!recalculate(&surface, work, f))
				{
					return false;
				}
			}
		}
		if (mirrored)
		{
			for (auto &t : surface.triangles)
			{
				if (!work.step())
				{
					return false;
				}
				std::swap(t.b, t.c);
			}
		}
		break;
	}
	case ModelEditKind::Extrude:
	case ModelEditKind::DuplicateFaces:
	{
		if (!requireFaces())
		{
			return false;
		}
		ModelTransformBasis axes;
		if (!resolveModelTransformAxes(candidate, edit, &axes, error, control))
			return false;
		const auto offset = modelBasisToWorld(edit.translation, axes);
		if (edit.kind == ModelEditKind::Extrude && lengthSquared(edit.translation) < 1e-12)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Extrusion requires a nonzero offset."));
		}
		QMap<QPair<int, int>, QVector<QPair<int, int>>> edges;
		for (int f : faces)
		{
			if (!work.step())
			{
				return false;
			}
			const auto t = surface.triangles[f];
			for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				if (!work.step())
				{
					return false;
				}
				edges[qMakePair(std::min(edge.first, edge.second), std::max(edge.first, edge.second))] << edge;
			}
		}
		if (edit.kind == ModelEditKind::Extrude)
		{
			for (const auto &edge : edges)
			{
				if (!work.step())
				{
					return false;
				}
				if (edge.size() > 2 || (edge.size() == 2 && edge[0] == edge[1]))
				{
					return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Extrusion requires consistently wound faces "
																							  "with at most two faces per edge."));
				}
			}
		}
		const qint64 addedTriangles =
			edit.kind == ModelEditKind::DuplicateFaces
				? faces.size()
				: 2 * std::count_if(edges.cbegin(), edges.cend(), [](const auto &edge) { return edge.size() == 1; });
		if (!triangleCapacity(surface.triangles.size() + addedTriangles))
		{
			return false;
		}
		QMap<int, int> copies;
		// Face operations use only their faces; an unrelated vertex selection
		// must not add disconnected vertices to a duplicated region.
		vertices = selectedVertices(surface, {selection.surface, {}, selection.faces});
		for (int old : sorted(vertices))
		{
			if (!work.step())
			{
				return false;
			}
			const int added = appendCopy(old);
			if (added < 0)
			{
				return false;
			}
			copies.insert(old, added);
			for (auto &frame : surface.frames)
			{
				if (!work.step())
				{
					return false;
				}
				frame.positions[added] = add(frame.positions[added], offset);
			}
		}
		QVector<ModelTriangle> triangles;
		selection.faces.clear();
		selection.vertices.clear();
		selection.edges.clear();
		for (int f = 0; f < surface.triangles.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			if (edit.kind == ModelEditKind::DuplicateFaces || !edit.selection.faces.contains(f))
			{
				triangles << surface.triangles[f];
			}
		}
		for (int f : faces)
		{
			if (!work.step())
			{
				return false;
			}
			const auto t = surface.triangles[f];
			selection.faces.insert(triangles.size());
			triangles << ModelTriangle{copies[t.a], copies[t.b], copies[t.c]};
		}
		if (edit.kind == ModelEditKind::Extrude)
		{
			for (const auto &edge : edges)
			{
				if (!work.step())
				{
					return false;
				}
				if (edge.size() != 1)
				{
					continue;
				}
				const int a = edge.first().first, b = edge.first().second;
				triangles << ModelTriangle{a, b, copies[b]} << ModelTriangle{a, copies[b], copies[a]};
			}
		}
		surface.triangles = std::move(triangles);
		surface.vertexCount = surface.texCoords.size();
		if (!copyUvSeams(&surface, copies, work))
		{
			return false;
		}
		if (!compact(&surface, &selection, work))
		{
			return false;
		}
		if (!recalculate(&surface, work))
		{
			return false;
		}
		break;
	}
	case ModelEditKind::Subdivide:
	case ModelEditKind::SplitEdges:
	{
		const bool splitEdges = edit.kind == ModelEditKind::SplitEdges;
		if (splitEdges && selection.edges.isEmpty())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Select at least one edge to split."));
		}
		if (!splitEdges && !requireFaces())
		{
			return false;
		}
		// Shared edge midpoints prevent cracks between subdivided neighbours.
		// Unselected neighbouring triangles are split along touched edges too,
		// preserving a conforming triangle mesh rather than creating T-junctions.
		QMap<ModelEdge, int> midpoints;
		QVector<ModelEdge> edgesToSplit;
		if (splitEdges)
		{
			edgesToSplit = selection.edges.values().toVector();
			std::sort(edgesToSplit.begin(), edgesToSplit.end());
		}
		else
		{
			for (int f : faces)
			{
				if (!work.step())
				{
					return false;
				}
				const auto t = surface.triangles[f];
				edgesToSplit << modelEdge(t.a, t.b) << modelEdge(t.b, t.c) << modelEdge(t.c, t.a);
			}
		}
		for (const auto &edge : edgesToSplit)
		{
			if (!work.step())
			{
				return false;
			}
			{
				if (!work.step())
				{
					return false;
				}
				const auto key = qMakePair(std::min(edge.first, edge.second), std::max(edge.first, edge.second));
				if (midpoints.contains(key))
				{
					continue;
				}
				const int a = edge.first, b = edge.second, index = surface.texCoords.size();
				if (!vertexCapacity(qint64(index) + 1))
				{
					return false;
				}
				surface.texCoords << ModelTexCoord{(surface.texCoords[a].u + surface.texCoords[b].u) * 0.5f,
												   (surface.texCoords[a].v + surface.texCoords[b].v) * 0.5f};
				for (auto &frame : surface.frames)
				{
					if (!work.step())
					{
						return false;
					}
					frame.positions << multiply(add(frame.positions[a], frame.positions[b]), 0.5f);
					frame.normals << normal(add(frame.normals[a], frame.normals[b]));
				}
				midpoints.insert(key, index);
			}
		}
		QVector<ModelTriangle> triangles;
		selection.faces.clear();
		for (int f = 0; f < surface.triangles.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			const auto t = surface.triangles[f];
			const int a = midpoints.value(qMakePair(std::min(t.a, t.b), std::max(t.a, t.b)), -1);
			const int b = midpoints.value(qMakePair(std::min(t.b, t.c), std::max(t.b, t.c)), -1);
			const int c = midpoints.value(qMakePair(std::min(t.c, t.a), std::max(t.c, t.a)), -1);
			const int start = triangles.size();
			if (a >= 0 && b >= 0 && c >= 0)
			{
				triangles << ModelTriangle{t.a, a, c} << ModelTriangle{a, t.b, b} << ModelTriangle{c, b, t.c} << ModelTriangle{a, b, c};
			}
			else if (a >= 0 && b >= 0)
			{
				triangles << ModelTriangle{a, t.b, b} << ModelTriangle{t.a, a, b} << ModelTriangle{t.a, b, t.c};
			}
			else if (b >= 0 && c >= 0)
			{
				triangles << ModelTriangle{b, t.c, c} << ModelTriangle{t.b, b, c} << ModelTriangle{t.b, c, t.a};
			}
			else if (c >= 0 && a >= 0)
			{
				triangles << ModelTriangle{c, t.a, a} << ModelTriangle{t.c, c, a} << ModelTriangle{t.c, a, t.b};
			}
			else if (a >= 0)
			{
				triangles << ModelTriangle{t.a, a, t.c} << ModelTriangle{a, t.b, t.c};
			}
			else if (b >= 0)
			{
				triangles << ModelTriangle{t.b, b, t.a} << ModelTriangle{b, t.c, t.a};
			}
			else if (c >= 0)
			{
				triangles << ModelTriangle{t.c, c, t.b} << ModelTriangle{c, t.a, t.b};
			}
			else
			{
				triangles << t;
			}
			if (!triangleCapacity(triangles.size()))
			{
				return false;
			}
			if (edit.selection.faces.contains(f))
			{
				for (int i = start; i < triangles.size(); ++i)
				{
					if (!work.step())
					{
						return false;
					}
					selection.faces.insert(i);
				}
			}
		}
		QSet<ModelEdge> remappedEdges;
		for (auto edge : selection.edges)
		{
			if (!work.step())
			{
				return false;
			}
			const auto mid = midpoints.constFind(edge);
			if (mid == midpoints.cend())
			{
				remappedEdges.insert(edge);
			}
			else
			{
				remappedEdges.insert(modelEdge(edge.first, mid.value()));
				remappedEdges.insert(modelEdge(mid.value(), edge.second));
			}
		}
		selection.edges = std::move(remappedEdges);
		QSet<ModelEdge> seams;
		for (auto edge : surface.uvSeams)
		{
			if (!work.step())
			{
				return false;
			}
			const auto mid = midpoints.constFind(edge);
			if (mid == midpoints.cend())
			{
				seams.insert(edge);
			}
			else
			{
				seams.insert(modelEdge(edge.first, mid.value()));
				seams.insert(modelEdge(mid.value(), edge.second));
			}
		}
		surface.uvSeams = std::move(seams);
		surface.triangles = std::move(triangles);
		surface.vertexCount = surface.texCoords.size();
		break;
	}
	case ModelEditKind::WeldVertices:
	{
		QVector<int> vertexMap, faceMap;
		if (!weldModelSurface(&surface, vertices, edit.weldDistance, edit.preserveSeams, &vertexMap, &faceMap, error, control))
		{
			return false;
		}
		QSet<int> selectedPoints, selectedFaces;
		QSet<ModelEdge> selectedEdges;
		for (int vertex : selection.vertices)
		{
			if (!work.step())
			{
				return false;
			}
			selectedPoints.insert(vertexMap[vertex]);
		}
		for (int face : selection.faces)
		{
			if (!work.step())
			{
				return false;
			}
			if (faceMap[face] >= 0)
			{
				selectedFaces.insert(faceMap[face]);
			}
		}
		for (auto edge : selection.edges)
		{
			if (!work.step())
			{
				return false;
			}
			if (vertexMap[edge.first] != vertexMap[edge.second])
			{
				selectedEdges.insert(modelEdge(vertexMap[edge.first], vertexMap[edge.second]));
			}
		}
		selection.vertices = std::move(selectedPoints);
		selection.faces = std::move(selectedFaces);
		selection.edges = std::move(selectedEdges);
		if (!compact(&surface, &selection, work))
		{
			return false;
		}
		break;
	}
	case ModelEditKind::FillBoundaryLoops:
	case ModelEditKind::BridgeBoundaryLoops:
	{
		if (edit.frame != -1 || selection.edges.isEmpty() || !selection.vertices.isEmpty() || !selection.faces.isEmpty() || edit.uvIslands)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
				"Boundary filling and bridging require only edge selection and apply to every frame. Choose the reference pose before editing."));
		}
		ModelSurface filled;
		ModelBoundaryFillReport report;
		if (edit.kind == ModelEditKind::BridgeBoundaryLoops)
		{
			ModelBoundaryBridgeReport bridge;
			if (!bridgeModelBoundaryLoops(surface, selection.edges, edit.sourceFrame, edit.bridgeTwist,
				int(modelDocumentMaxTriangles - otherTriangles), &filled, &bridge, error, control))
				return false;
			report.faces = std::move(bridge.faces);
		}
		else if (!fillModelBoundaryLoops(surface, selection.edges, edit.sourceFrame, int(modelDocumentMaxTriangles - otherTriangles),
									&filled, &report, error, control))
		{
			return false;
		}
		surface = std::move(filled);
		selection = {selection.surface, {}, QSet<int>(report.faces.cbegin(), report.faces.cend())};
		break;
	}
	case ModelEditKind::RemoveDuplicateFaces:
	case ModelEditKind::RemoveUnusedVertices:
	case ModelEditKind::SplitDisconnectedFans:
	case ModelEditKind::OrientFaces:
	case ModelEditKind::SplitNonmanifoldEdges:
	{
		if (edit.frame != -1)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Topology repairs require every animation pose."));
		}
		const auto repair = edit.kind == ModelEditKind::RemoveDuplicateFaces	? ModelTopologyRepair::RemoveDuplicateFaces
							: edit.kind == ModelEditKind::RemoveUnusedVertices	? ModelTopologyRepair::RemoveUnusedVertices
							: edit.kind == ModelEditKind::SplitDisconnectedFans ? ModelTopologyRepair::SplitDisconnectedFans
							: edit.kind == ModelEditKind::SplitNonmanifoldEdges ? ModelTopologyRepair::SplitNonmanifoldEdges
																				: ModelTopologyRepair::OrientFaces;
		const int limit = int(std::min<qint64>(modelDocumentMaxVertices, modelDocumentMaxFrameVertices / candidate.frames.size()) -
							  (originalVertices - source.vertexCount));
		ModelSurface repaired;
		QVector<QVector<int>> vertexMap;
		QVector<int> faceMap;
		if (!repairModelTopology(surface, repair, limit, &repaired, &vertexMap, &faceMap, error, control))
		{
			return false;
		}
		ModelSelection remapped;
		remapped.surface = selection.surface;
		QVector<int> origins(repaired.vertexCount, -1);
		for (int old = 0; old < vertexMap.size(); ++old)
		{
			for (int copy : vertexMap[old])
			{
				if (!work.step())
				{
					return false;
				}
				origins[copy] = old;
				if (selection.vertices.contains(old))
				{
					remapped.vertices.insert(copy);
				}
			}
		}
		for (int face : selection.faces)
		{
			if (!work.step())
			{
				return false;
			}
			if (faceMap[face] >= 0)
			{
				remapped.faces.insert(faceMap[face]);
			}
		}
		for (const auto &t : repaired.triangles)
		{
			if (!work.step())
			{
				return false;
			}
			for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			{
				if (selection.edges.contains(modelEdge(origins[edge.first], origins[edge.second])))
				{
					remapped.edges.insert(edge);
				}
			}
		}
		surface = std::move(repaired);
		selection = std::move(remapped);
		break;
	}
	case ModelEditKind::DeleteFaces:
	{
		if (!requireFaces())
		{
			return false;
		}
		if (faces.size() == surface.triangles.size())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "A surface must retain at least one face."));
		}
		QVector<ModelTriangle> triangles;
		for (int f = 0; f < surface.triangles.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			if (!selection.faces.contains(f))
			{
				triangles << surface.triangles[f];
			}
		}
		surface.triangles = std::move(triangles);
		selection.faces.clear();
		if (!compact(&surface, &selection, work))
		{
			return false;
		}
		if (!recalculate(&surface, work))
		{
			return false;
		}
		break;
	}
	case ModelEditKind::FlipFaces:
		if (!requireFaces())
		{
			return false;
		}
		for (int f : faces)
		{
			if (!work.step())
			{
				return false;
			}
			std::swap(surface.triangles[f].b, surface.triangles[f].c);
		}
		if (!recalculate(&surface, work))
		{
			return false;
		}
		break;
	case ModelEditKind::RecalculateNormals:
		if (!recalculate(&surface, work, edit.frame))
		{
			return false;
		}
		break;
	case ModelEditKind::MarkUvSeams:
	case ModelEditKind::ClearUvSeams:
		if (selection.edges.isEmpty())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Select edges to mark or clear UV seams."));
		}
		for (auto edge : selection.edges)
		{
			if (!work.step())
			{
				return false;
			}
			if (edit.kind == ModelEditKind::MarkUvSeams)
			{
				surface.uvSeams.insert(edge);
			}
			else
			{
				surface.uvSeams.remove(edge);
			}
		}
		break;
	case ModelEditKind::DetachUv:
	case ModelEditKind::TransformUv:
	case ModelEditKind::ProjectUv:
	{
		ModelVec3 snapped;
		if (!snapModelTranslation({edit.uvOffset.u, edit.uvOffset.v, 0}, edit.uvTranslationGrid, &snapped, error))
		{
			return false;
		}
		if (!requireVertices())
		{
			return false;
		}
		if (edit.kind == ModelEditKind::DetachUv && !requireFaces())
		{
			return false;
		}
		if (edit.kind != ModelEditKind::DetachUv &&
			(edit.projection < 0 || edit.projection > 2 || std::abs(edit.uvScale.u) < 1e-6f || std::abs(edit.uvScale.v) < 1e-6f))
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelDocument", "Choose a valid UV projection and nonzero UV scales."));
		}
		// UV operations on faces split corners shared with unselected faces,
		// preserving their existing texture mapping and every animation pose.
		if (edit.uvPivotMode == ModelUvPivot::IndividualIslands)
		{
			if (!requireFaces() || !selection.vertices.isEmpty() || !selection.edges.isEmpty())
				return fail(error, QCoreApplication::translate("ModelUvTransform", "Select complete UV islands. Use Select Islands to expand the current components first."));
			ModelUvIslandTransform transform;
			transform.scale = edit.uvScale;
			transform.offset = {snapped.x, snapped.y};
			transform.rotation = edit.uvRotation;
			transform.projection = edit.kind == ModelEditKind::ProjectUv ? edit.projection : -1;
			transform.referenceFrame = std::max(0, edit.frame);
			transform.vertexLimit = int(std::min(qint64(modelDocumentMaxVertices), modelDocumentMaxFrameVertices / candidate.frames.size()) -
									 (originalVertices - source.vertexCount));
			ModelSurface transformed;
			if (!transformModelUvIslands(surface, selection.faces, transform, &transformed, error, control))
				return false;
			surface = std::move(transformed);
			break;
		}
		QSet<int> outside;
		for (int f = 0; f < surface.triangles.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			if (!selection.faces.contains(f))
			{
				const auto t = surface.triangles[f];
				outside.insert(t.a);
				outside.insert(t.b);
				outside.insert(t.c);
			}
		}
		QMap<int, int> copies;
		QSet<int> explicitVertices = selection.vertices;
		for (auto edge : selection.edges)
		{
			if (!work.step())
			{
				return false;
			}
			explicitVertices.insert(edge.first);
			explicitVertices.insert(edge.second);
		}
		if (edit.kind == ModelEditKind::DetachUv)
		{
			explicitVertices.clear();
			selection.vertices.clear();
			selection.edges.clear();
		}
		for (int f : faces)
		{
			if (!work.step())
			{
				return false;
			}
			auto &t = surface.triangles[f];
			for (int *corner : {&t.a, &t.b, &t.c})
			{
				if (!work.step())
				{
					return false;
				}
				const int old = *corner;
				if (!outside.contains(old) || explicitVertices.contains(old))
				{
					continue;
				}
				if (!copies.contains(old))
				{
					const int added = appendCopy(old);
					if (added < 0)
					{
						return false;
					}
					copies.insert(old, added);
				}
				*corner = copies[old];
			}
		}
		vertices = selectedVertices(surface, selection);
		if (!copyUvSeams(&surface, copies, work))
		{
			return false;
		}
		const auto &positions = surface.frames[std::max(0, edit.frame)].positions;
		const auto coordinate = [&](int vertex)
		{
			if (edit.kind != ModelEditKind::ProjectUv)
			{
				return surface.texCoords[vertex];
			}
			const auto p = positions[vertex];
			return edit.projection == 0	  ? ModelTexCoord{p.x, -p.y}
				   : edit.projection == 1 ? ModelTexCoord{p.x, -p.z}
										  : ModelTexCoord{p.y, -p.z};
		};
		ModelTexCoord pivot = edit.uvPivotMode == ModelUvPivot::Custom ? edit.uvPivot : ModelTexCoord{};
		if (edit.uvPivotMode == ModelUvPivot::SelectionCentre)
		{
			ModelTexCoord lo{1000000, 1000000}, hi{-1000000, -1000000};
			for (int vertex : vertices)
			{
				if (!work.step())
				{
					return false;
				}
				const auto uv = coordinate(vertex);
				lo.u = std::min(lo.u, uv.u);
				lo.v = std::min(lo.v, uv.v);
				hi.u = std::max(hi.u, uv.u);
				hi.v = std::max(hi.v, uv.v);
			}
			pivot = {float((double(lo.u) + hi.u) / 2), float((double(lo.v) + hi.v) / 2)};
		}
		for (int i : vertices)
		{
			if (!work.step())
			{
				return false;
			}
			if (edit.kind != ModelEditKind::DetachUv)
			{
				surface.texCoords[i] = transformUv(coordinate(i), edit, pivot, {snapped.x, snapped.y});
			}
		}
		surface.vertexCount = surface.texCoords.size();
		break;
	}
	case ModelEditKind::UnwrapUv:
	case ModelEditKind::PackUv:
	case ModelEditKind::PackUvAround:
	{
		if (!requireFaces() || !selection.vertices.isEmpty() || !selection.edges.isEmpty())
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelDocument",
													"Select faces for an atlas operation, or expand components to UV islands first."));
		}
		ModelUvAtlasOptions options;
		options.resolution = edit.uvAtlasResolution;
		options.height = edit.uvAtlasHeight;
		options.padding = edit.uvAtlasPadding;
		options.vertexLimit = int(std::min(qint64(modelDocumentMaxVertices), modelDocumentMaxFrameVertices / candidate.frames.size()) -
								  (originalVertices - source.vertexCount));
		ModelSurface mapped;
		const bool packed = edit.kind == ModelEditKind::PackUvAround
			? packModelUvAround(candidate, selection.surface, selection.faces, {options, edit.uvPreserveScale}, &mapped, error, control)
			: atlasModelUv(surface, selection.faces, std::max(0, edit.frame), edit.kind == ModelEditKind::UnwrapUv, options, &mapped, error, control);
		if (!packed || !vertexCapacity(mapped.vertexCount))
		{
			return false;
		}
		surface = std::move(mapped);
		break;
	}
	case ModelEditKind::SetMd2SkinSize:
		candidate.md2SkinSize = edit.md2SkinSize;
		break;
	case ModelEditKind::ApplySkinBindings:
	{
		ModelSkinBindingPlan plan;
		if (!planModelSkinBindings(candidate, edit.skinBindings, &plan, error, control)) { return false; }
		for (const auto &item : plan.assignments)
		{
			auto &paths = candidate.surfaces[item.surface].skinPaths;
			if (paths.isEmpty()) { paths.append(item.material); }
			else { paths[0] = item.material; }
		}
		break;
	}
	case ModelEditKind::SetMaterialSlots:
	{
		if (edit.frame != -1 || !edit.text.isEmpty() || !edit.skinBindings.isEmpty())
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Material slots span every pose; omit frame, text and skin binding arguments."));
		if (!supportsModelMaterialSlots(candidate))
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Embedded MDL skins use the Animation inspector's skin controls."));
		ModelMaterialSlotEdit replacement;
		replacement.materials = edit.materialSlots;
		if (!editModelMaterialSlots(surface.skinPaths, replacement, &surface.skinPaths, error)) return false;
		break;
	}
	case ModelEditKind::SetMaterial:
		if (!isSafePackageVirtualPath(edit.text) || edit.text.size() > 255)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Choose a safe package-relative material path."));
		}
		if (surface.skinPaths.isEmpty())
		{
			surface.skinPaths << edit.text;
		}
		else
		{
			surface.skinPaths[0] = edit.text;
		}
		break;
	case ModelEditKind::DuplicateFrame:
	{
		if (originalVertices * (candidate.frames.size() + 1) > modelDocumentMaxFrameVertices)
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelDocument", "The new frame would exceed the animation storage limit."));
		}
		if (candidate.frames.size() >= modelDocumentMaxFrames)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model already has 1024 frames."));
		}
		if (!changeModelMdlFrames(&candidate, ModelMdlFrameChange::Duplicate, edit.frame, 1, error))
		{
			return false;
		}
		const int inserted = edit.frame + 1;
		auto info = candidate.frames[edit.frame];
		if (!edit.text.isEmpty())
		{
			info.name = edit.text;
		}
		candidate.frames.insert(inserted, info);
		for (auto &box : candidate.collisionBoxes)
		{
			if (!work.step()) return false;
			if (box.framePoses.isEmpty()) continue;
			auto poses = box.framePoses;
			poses.insert(inserted, poses[edit.frame]);
			setModelCollisionFrames(&box, std::move(poses));
		}
		for (auto &s : candidate.surfaces)
		{
			if (!work.step())
			{
				return false;
			}
			s.frames.insert(inserted, s.frames[edit.frame]);
		}
		QVector<ModelTag> copies;
		for (auto &tag : candidate.tags)
		{
			if (!work.step())
			{
				return false;
			}
			if (tag.frameIndex == edit.frame)
			{
				auto copy = tag;
				copy.frameIndex = inserted;
				copies << copy;
			}
			else if (tag.frameIndex >= inserted)
			{
				++tag.frameIndex;
			}
		}
		candidate.tags += copies;
		for (auto &a : candidate.animations)
		{
			if (!work.step())
			{
				return false;
			}
			if (a.firstFrame >= inserted)
			{
				++a.firstFrame;
			}
			else if (a.firstFrame + a.frameCount >= inserted)
			{
				++a.frameCount;
			}
		}
		break;
	}
	case ModelEditKind::DeleteFrame:
		if (candidate.frames.size() == 1)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "A model must retain at least one frame."));
		}
		if (!changeModelMdlFrames(&candidate, ModelMdlFrameChange::Delete, edit.frame, 1, error))
		{
			return false;
		}
		candidate.frames.removeAt(edit.frame);
		for (auto &box : candidate.collisionBoxes)
		{
			if (!work.step()) return false;
			if (box.framePoses.isEmpty()) continue;
			auto poses = box.framePoses;
			poses.removeAt(edit.frame);
			setModelCollisionFrames(&box, std::move(poses));
		}
		for (auto &s : candidate.surfaces)
		{
			if (!work.step())
			{
				return false;
			}
			s.frames.removeAt(edit.frame);
		}
		candidate.tags.removeIf([&](const ModelTag &tag) { return tag.frameIndex == edit.frame; });
		for (auto &tag : candidate.tags)
		{
			if (!work.step())
			{
				return false;
			}
			if (tag.frameIndex > edit.frame)
			{
				--tag.frameIndex;
			}
		}
		for (auto &a : candidate.animations)
		{
			if (!work.step())
			{
				return false;
			}
			if (a.firstFrame > edit.frame)
			{
				--a.firstFrame;
			}
			else if (a.firstFrame + a.frameCount > edit.frame)
			{
				--a.frameCount;
			}
		}
		candidate.animations.removeIf([](const ModelAnimation &a) { return a.frameCount == 0; });
		break;
	case ModelEditKind::RenameFrame:
		candidate.frames[edit.frame].name = edit.text;
		break;
	default:
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "Unknown model edit operation."));
	}
	if (!selection.edges.isEmpty() || !surface.uvSeams.isEmpty())
	{
		const auto available = modelSurfaceEdges(surface, control);
		if (!work.check())
		{
			return false;
		}
		selection.edges.removeIf([&](auto edge) { return !std::binary_search(available.cbegin(), available.cend(), edge); });
		surface.uvSeams.removeIf([&](auto edge) { return !std::binary_search(available.cbegin(), available.cend(), edge); });
	}
	const auto errors = validateEditableModel(candidate, control);
	if (!errors.isEmpty())
	{
		return fail(error, errors.join(QLatin1Char('\n')));
	}
	updateEditableModelMetadata(&candidate);
	if (!work.check())
	{
		return false;
	}
	*mesh = std::move(candidate);
	if (resultingSelection)
	{
		*resultingSelection = std::move(selection);
	}
	return true;
}

qint64 ModelDocument::State::estimatedBytes() const
{
	qint64 result = storageBytes(mesh) + selectionBytes(selection);
	for (const auto &surface : topology)
	{
		result += surface.storageBytes();
	}
	return result;
}

bool ModelDocument::prepareTopology(State *state, QString *error, const ModelWorkControl &control)
{
	state->topology.resize(state->mesh.surfaces.size());
	for (int surface = 0; surface < state->mesh.surfaces.size(); ++surface)
	{
		if (!prepareModelSurfaceTopology(state->mesh.surfaces.at(surface), &state->topology[surface], error, control))
		{
			return false;
		}
	}
	state->bytes = state->estimatedBytes();
	return true;
}

const ModelSurfaceTopology &ModelDocument::surfaceTopology(int surface) const
{
	static const ModelSurfaceTopology empty;
	return surface >= 0 && surface < m_state.topology.size() ? m_state.topology[surface] : empty;
}

bool ModelDocument::setMesh(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	const auto errors = validateEditableModel(mesh, control);
	if (!errors.isEmpty())
	{
		return fail(error, errors.join(QLatin1Char('\n')));
	}
	State candidate{mesh, {}, {}, 0, {}};
	updateEditableModelMetadata(&candidate.mesh);
	candidate.digest = digest(candidate.mesh, error, control);
	if (candidate.digest.isEmpty() || !prepareTopology(&candidate, error, control))
	{
		return false;
	}
	m_state = std::move(candidate);
	m_savedDigest = m_state.digest;
	m_path.clear();
	m_resolvedPath.clear();
	m_sourceFingerprint.clear();
	m_undo.clear();
	m_redo.clear();
	return true;
}
bool ModelDocument::load(const QString &path, QString *error, const ModelWorkControl &control)
{
	const auto resolvedSource = QFileInfo(path).canonicalFilePath();
	QByteArray bytes;
	if (!readModelFile(path, &bytes, error, control))
	{
		return false;
	}
	ModelMesh candidate;
	ModelDocument loaded;
	if (!parseEditableModel(bytes, &candidate, error, control) || !modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error) ||
		!loaded.setMesh(candidate, error, control) || !modelWorkCheckpoint(control, ModelWorkPhase::Validating, 1, 1, error))
	{
		return false;
	}
	if (resolvedSource.isEmpty() || !modelPathsReferToSameFile(path, resolvedSource))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The model source changed or failed while reading."));
	}
	loaded.m_path = QFileInfo(path).absoluteFilePath();
	loaded.m_resolvedPath = resolvedSource;
	loaded.m_sourceFingerprint = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	*this = std::move(loaded);
	return true;
}
bool ModelDocument::restoreDraft(const ModelMesh &mesh, const ModelSelection &selection, QString *error, const ModelWorkControl &control)
{
	ModelDocument candidate;
	if (!candidate.setMesh(mesh, error, control))
	{
		return false;
	}
	candidate.setSelection(selection);
	if (candidate.selection() != selection)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The recovered component selection is invalid."));
	}
	candidate.m_savedDigest.clear();
	*this = std::move(candidate);
	return true;
}
bool ModelDocument::save(const QString &path, bool overwrite, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!path.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelDocument", "Editable model sources must use the .mesh.json extension."));
	}
	const auto target = inspectModelWriteTarget(path, control);
	if (!target.isValid())
	{
		return fail(error, target.error);
	}
	const bool current = modelPathsReferToSameFile(path, m_path);
#ifdef Q_OS_WIN
	constexpr auto pathSensitivity = Qt::CaseInsensitive;
#else
	constexpr auto pathSensitivity = Qt::CaseSensitive;
#endif
	if (current &&
		(!target.existed || target.sha256 != m_sourceFingerprint || target.resolvedPath.compare(m_resolvedPath, pathSensitivity) != 0))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
													   "The source changed on disk. Save to another path or reload before replacing it."));
	}
	if (target.existed && !current && !overwrite)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument",
													   "The model destination exists. Choose a new path or explicitly enable overwrite."));
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, error))
	{
		return false;
	}
	const auto errors = validateEditableModel(m_state.mesh, control);
	if (!errors.isEmpty())
	{
		return fail(error, errors.join(QLatin1Char('\n')));
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, error))
	{
		return false;
	}
	const auto json = editableModelJson(m_state.mesh, error, control);
	if (json.isEmpty())
	{
		return false;
	}
	const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, bytes.size(), bytes.size(), error))
	{
		return false;
	}
	if (bytes.size() > modelDocumentMaxSourceBytes)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelDocument", "The serialized model exceeds the 64 MiB source limit."));
	}
	// Finish fallible preparation before publication. Afterwards only adopt the
	// already-allocated identity; a committed file must remain a successful save.
	const auto sourceFingerprint = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	if (!writeModelFile(target, bytes, error, control))
	{
		return false;
	}
	m_path = target.path;
	m_resolvedPath = target.resolvedPath;
	m_savedDigest = m_state.digest;
	m_sourceFingerprint = sourceFingerprint;
	return true;
}
bool ModelDocument::edit(const ModelEdit &operation, QString *error, const ModelWorkControl &control)
{
	State candidate = m_state;
	if (!applyModelEdit(&candidate.mesh, operation, &candidate.selection, error, control))
	{
		return false;
	}
	candidate.digest = digest(candidate.mesh, error, control);
	if (candidate.digest.isEmpty())
	{
		return false;
	}
	if (candidate.digest == m_state.digest)
	{
		m_state.selection = candidate.selection;
		m_state.bytes = m_state.estimatedBytes();
		return true;
	}
	if (!prepareTopology(&candidate, error, control)) { return false; }
	m_undo << m_state;
	m_redo.clear();
	m_state = std::move(candidate);
	trimHistory();
	return true;
}
bool ModelDocument::undo()
{
	if (m_undo.isEmpty())
	{
		return false;
	}
	m_redo << m_state;
	m_state = m_undo.takeLast();
	return true;
}
bool ModelDocument::redo()
{
	if (m_redo.isEmpty())
	{
		return false;
	}
	m_undo << m_state;
	m_state = m_redo.takeLast();
	return true;
}
bool ModelDocument::canUndo() const { return !m_undo.isEmpty(); }
bool ModelDocument::canRedo() const { return !m_redo.isEmpty(); }
bool ModelDocument::isModified() const { return m_state.digest != m_savedDigest; }
const ModelMesh &ModelDocument::mesh() const { return m_state.mesh; }
const ModelSelection &ModelDocument::selection() const { return m_state.selection; }
QString ModelDocument::path() const { return m_path; }
QByteArray ModelDocument::sourceFingerprint() const { return m_sourceFingerprint; }
QByteArray ModelDocument::revisionFingerprint() const { return m_state.digest; }
void ModelDocument::setSelection(const ModelSelection &selection)
{
	if (selection == m_state.selection)
	{
		return;
	}
	if (selection.surface < 0 || selection.surface >= m_state.mesh.surfaces.size())
	{
		return;
	}
	if (!validModelSurfaceSelection(m_state.mesh, selection) || !validModelTagSelection(m_state.mesh, selection) || !validModelCollisionSelection(m_state.mesh, selection))
	{
		return;
	}
	const auto &surface = m_state.mesh.surfaces.at(selection.surface);
	if (std::any_of(selection.vertices.cbegin(), selection.vertices.cend(), [&](int i) { return i < 0 || i >= surface.vertexCount; }) ||
		std::any_of(selection.faces.cbegin(), selection.faces.cend(), [&](int i) { return i < 0 || i >= surface.triangles.size(); }))
	{
		return;
	}
	if (!selection.edges.isEmpty())
	{
		const auto &topology = surfaceTopology(selection.surface);
		// A shared complete selection was already validated while preparing the
		// topology. Other callers still get exact edge-membership validation.
		if (selection.edges != topology.allEdges &&
			std::any_of(selection.edges.cbegin(), selection.edges.cend(), [&](auto edge) { return !topology.faceUses.contains(edge); }))
		{
			return;
		}
	}
	m_state.selection = selection;
	m_state.bytes = m_state.estimatedBytes();
}
qint64 ModelDocument::historyBytes() const
{
	qint64 result = 0;
	for (const auto &state : m_undo)
	{
		result += state.bytes;
	}
	for (const auto &state : m_redo)
	{
		result += state.bytes;
	}
	return result;
}
void ModelDocument::trimHistory()
{
	while (m_undo.size() > 100 || (m_undo.size() > 1 && historyBytes() > 64 * 1024 * 1024))
	{
		m_undo.removeFirst();
	}
}

} // namespace vibestudio
