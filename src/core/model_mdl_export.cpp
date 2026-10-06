#include "core/model_mdl.h"
#include "core/model_collision.h"

#include "core/model_document.h"

#include <QCoreApplication>
#include <QHash>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>

namespace vibestudio
{
namespace
{
// Original writer using id Software Quake/WinQuake/modelgen.h, model.c,
// gl_model.{c,h} and gl_draw.c, master reviewed 2026-10-04, GPL-2.0-or-later
// (compatible with this GPL-3.0 project). No engine implementation is copied.
// https://github.com/id-Software/Quake/tree/master/WinQuake
// Retain native group payloads even where original GLQuake's fixed skin cycle
// or first-interval frame playback differs from the software renderer.
QByteArray failure(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return {};
}
void integer(QByteArray &bytes, qsizetype offset, qint32 value) { qToLittleEndian(value, bytes.data() + offset); }
void number(QByteArray &bytes, qsizetype offset, float value)
{
	quint32 bits;
	std::memcpy(&bits, &value, 4);
	qToLittleEndian(bits, bytes.data() + offset);
}
void vector(QByteArray &bytes, qsizetype offset, ModelVec3 value)
{
	number(bytes, offset, value.x);
	number(bytes, offset + 4, value.y);
	number(bytes, offset + 8, value.z);
}
void appendInteger(QByteArray &bytes, qint32 value)
{
	const auto offset = bytes.size();
	bytes.resize(offset + 4);
	integer(bytes, offset, value);
}
void appendNumber(QByteArray &bytes, float value)
{
	const auto offset = bytes.size();
	bytes.resize(offset + 4);
	number(bytes, offset, value);
}
QByteArray uvKey(int identity, int s, int t)
{
	QByteArray key(12, '\0');
	integer(key, 0, identity);
	integer(key, 4, s);
	integer(key, 8, t);
	return key;
}
struct Corner
{
	int source = 0, identity = 0, s = 0, t = 0, seam = -1;
	bool back = false;
};
struct Seam
{
	int front = 0, back = 0;
	bool active = true;
};
struct StoredVertex
{
	int source = 0, s = 0, t = 0;
	bool seam = false;
};
struct Layout
{
	QVector<StoredVertex> vertices;
	QVector<ModelTriangle> triangles;
	QVector<bool> facesFront;
};
bool buildLayout(const ModelMesh &mesh, Layout *layout, ModelExportReport *report, QString *error, ModelWorkProgress &work)
{
	const auto &surface = mesh.surfaces.first();
	const int width = mesh.mdl.skinSize.width(), height = mesh.mdl.skinSize.height();
	QHash<QByteArray, int> identities, coordinates;
	QVector<Corner> corners;
	QVector<int> sourceCorner;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		if (!work.step())
		{
			return false;
		}
		QByteArray key(mesh.frames.size() * 24, '\0');
		for (int f = 0; f < mesh.frames.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			vector(key, f * 24, surface.frames[f].positions[v]);
			vector(key, f * 24 + 12, surface.frames[f].normals[v]);
		}
		int identity;
		const auto found = identities.constFind(key);
		if (found == identities.cend())
		{
			identity = identities.size();
			identities.insert(key, identity);
		}
		else
		{
			identity = found.value();
		}
		const auto uv = surface.texCoords[v];
		if (uv.u < 0 || uv.u > 1 || uv.v < 0 || uv.v > 1)
		{
			failure(error, QCoreApplication::translate("VibeStudioModelMdl", "MDL UV vertex %1 lies outside the 0–1 skin tile.").arg(v));
			return false;
		}
		const int s = std::clamp(int(std::lround(double(uv.u) * width - .5)), 0, width - 1);
		const int t = std::clamp(int(std::lround(double(uv.v) * height - .5)), 0, height - 1);
		report->maxUvError = std::max({report->maxUvError, std::abs((s + .5) / width - uv.u), std::abs((t + .5) / height - uv.v)});
		const auto location = uvKey(identity, s, t);
		const auto existing = coordinates.constFind(location);
		if (existing == coordinates.cend())
		{
			sourceCorner << corners.size();
			coordinates.insert(location, corners.size());
			corners << Corner{v, identity, s, t};
		}
		else
		{
			sourceCorner << existing.value();
		}
	}
	QVector<Seam> seams;
	for (int index = 0; index < corners.size(); ++index)
	{
		auto &front = corners[index];
		if (front.s >= width / 2)
		{
			continue;
		}
		const auto back = coordinates.constFind(uvKey(front.identity, front.s + width / 2, front.t));
		if (back != coordinates.cend())
		{
			front.seam = seams.size();
			corners[back.value()].seam = seams.size();
			corners[back.value()].back = true;
			seams << Seam{index, back.value()};
		}
	}
	// facesfront is per triangle. A face cannot request both halves of active
	// seams; split the minority side deterministically when that occurs. This
	// deterministic packing preserves UVs; it is not a global minimum solver.
	for (const auto &triangle : surface.triangles)
	{
		if (!work.step())
		{
			return false;
		}
		int front = 0, back = 0;
		for (int v : {triangle.a, triangle.b, triangle.c})
		{
			const auto &corner = corners[sourceCorner[v]];
			if (corner.seam >= 0 && seams[corner.seam].active)
			{
				(corner.back ? back : front)++;
			}
		}
		if (front && back)
		{
			for (int v : {triangle.a, triangle.b, triangle.c})
			{
				const auto &corner = corners[sourceCorner[v]];
				if (corner.seam >= 0 && corner.back != (back > front))
				{
					seams[corner.seam].active = false;
				}
			}
		}
	}
	QVector<int> stored(corners.size(), -1);
	for (int index = 0; index < corners.size(); ++index)
	{
		if (stored[index] >= 0)
		{
			continue;
		}
		const auto &corner = corners[index];
		const bool shared = corner.seam >= 0 && seams[corner.seam].active;
		const auto &representative = shared ? corners[seams[corner.seam].front] : corner;
		stored[index] = layout->vertices.size();
		if (shared)
		{
			stored[seams[corner.seam].front] = stored[index];
			stored[seams[corner.seam].back] = stored[index];
		}
		layout->vertices << StoredVertex{representative.source, representative.s, representative.t, shared};
	}
	if (layout->vertices.size() > 1024)
	{
		failure(error,
				QCoreApplication::translate("VibeStudioModelMdl", "MDL exceeds 1024 vertices after sharing exact all-pose positions, "
																  "normals and compatible UV seams. Reduce geometry or UV splits."));
		return false;
	}
	for (const auto &triangle : surface.triangles)
	{
		layout->triangles << ModelTriangle{stored[sourceCorner[triangle.a]], stored[sourceCorner[triangle.b]],
										   stored[sourceCorner[triangle.c]]};
		bool front = true;
		for (int v : {triangle.a, triangle.b, triangle.c})
		{
			const auto &corner = corners[sourceCorner[v]];
			if (corner.seam >= 0 && seams[corner.seam].active && corner.back)
			{
				front = false;
			}
		}
		layout->facesFront << front;
	}
	return work.check();
}
std::array<double, 3> faceNormal(ModelVec3 a, ModelVec3 b, ModelVec3 c)
{
	const double x = double(b.x) - a.x, y = double(b.y) - a.y, z = double(b.z) - a.z;
	const double u = double(c.x) - a.x, v = double(c.y) - a.y, w = double(c.z) - a.z;
	return {y * w - z * v, z * u - x * w, x * v - y * u};
}
} // namespace

QByteArray exportModelMdl(const ModelMesh &mesh, QString *error, const ModelWorkControl &control, ModelExportReport *report)
{
	if (error)
	{
		error->clear();
	}
	if (report)
	{
		*report = {};
	}
	const auto errors = validateEditableModel(mesh, control);
	if (!errors.isEmpty())
	{
		return failure(error, errors.join(QLatin1Char('\n')));
	}
	if (!mesh.mdl.enabled || mesh.embeddedSkins.isEmpty())
	{
		return failure(error,
					   QCoreApplication::translate("VibeStudioModelMdl", "Add an indexed MDL skin and its palette before exporting MDL."));
	}
	if (mesh.surfaces.size() != 1 || mesh.frames.size() > 256 || mesh.surfaces[0].triangles.size() > 2048 ||
		mesh.embeddedSkins.size() > 32 || !mesh.tags.isEmpty())
	{
		return failure(error,
					   QCoreApplication::translate("VibeStudioModelMdl", "Original Quake MDL export requires one surface, at most 256 "
																		 "poses, 2048 triangles, 32 skin slots and no attachment tags."));
	}
	const auto size = mesh.mdl.skinSize;
	if (size.width() % 4 != 0 || size.height() > 480 || qint64(size.width()) * size.height() > 640 * 480)
	{
		return failure(error,
					   QCoreApplication::translate(
						   "VibeStudioModelMdl",
						   "Original Quake skins need a width divisible by four, height at most 480 and at most 307200 pixels per image."));
	}
	for (const auto &frame : mesh.frames)
	{
		if (frame.name.size() > 15 ||
			std::any_of(frame.name.cbegin(), frame.name.cend(), [](QChar c) { return c.unicode() < 32 || c.unicode() > 126; }))
		{
			return failure(error,
						   QCoreApplication::translate("VibeStudioModelMdl", "MDL frame names must fit 15 printable ASCII characters."));
		}
	}
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	ModelExportReport result;
	Layout layout;
	if (!work.check() || !buildLayout(mesh, &layout, &result, error, work))
	{
		return {};
	}
	const auto &surface = mesh.surfaces[0];
	std::array<float, 3> minimum, maximum, scale;
	minimum.fill(std::numeric_limits<float>::infinity());
	maximum.fill(-std::numeric_limits<float>::infinity());
	for (const auto &pose : surface.frames)
	{
		for (const auto &vertex : layout.vertices)
		{
			if (!work.step())
			{
				return {};
			}
			const auto p = pose.positions[vertex.source];
			const float values[]{p.x, p.y, p.z};
			for (int axis = 0; axis < 3; ++axis)
			{
				minimum[axis] = std::min(minimum[axis], values[axis]);
				maximum[axis] = std::max(maximum[axis], values[axis]);
			}
		}
	}
	for (int axis = 0; axis < 3; ++axis)
	{
		scale[axis] = maximum[axis] == minimum[axis] ? 1.f : float((double(maximum[axis]) - minimum[axis]) / 255);
		if (!std::isfinite(scale[axis]) || scale[axis] <= 0)
		{
			return failure(error, QCoreApplication::translate("VibeStudioModelMdl",
															  "The model extent cannot be represented by MDL's global byte scale."));
		}
	}
	QVector<QByteArray> poses;
	double radiusSquared = 0;
	for (int f = 0; f < mesh.frames.size(); ++f)
	{
		QByteArray pose(24 + layout.vertices.size() * 4, '\0');
		for (int axis = 0; axis < 3; ++axis)
		{
			pose[axis] = char(255);
		}
		const auto name = mesh.frames[f].name.toLatin1();
		std::memcpy(pose.data() + 8, name.constData(), name.size());
		QVector<ModelVec3> decodedPose;
		for (int v = 0; v < layout.vertices.size(); ++v)
		{
			if (!work.step())
			{
				return {};
			}
			const auto p = surface.frames[f].positions[layout.vertices[v].source];
			const float original[]{p.x, p.y, p.z};
			std::array<float, 3> decodedPosition;
			double errorSquared = 0, distanceSquared = 0;
			for (int axis = 0; axis < 3; ++axis)
			{
				const int value = std::clamp(int(std::lround((double(original[axis]) - minimum[axis]) / scale[axis])), 0, 255);
				pose[24 + v * 4 + axis] = char(value);
				pose[axis] = char(std::min(int(quint8(pose[axis])), value));
				pose[4 + axis] = char(std::max(int(quint8(pose[4 + axis])), value));
				const float decoded = scale[axis] * float(value) + minimum[axis];
				decodedPosition[axis] = decoded;
				const double difference = double(decoded) - original[axis];
				errorSquared += difference * difference;
				distanceSquared += double(decoded) * decoded;
			}
			decodedPose << ModelVec3{decodedPosition[0], decodedPosition[1], decodedPosition[2]};
			result.maxPositionError = std::max(result.maxPositionError, std::sqrt(errorSquared));
			radiusSquared = std::max(radiusSquared, distanceSquared);
			const auto normal = surface.frames[f].normals[layout.vertices[v].source];
			const int normalIndex = modelAliasNormalIndex(normal);
			pose[24 + v * 4 + 3] = char(normalIndex);
			const auto decoded = modelAliasNormal(normalIndex);
			const double a = double(normal.x) * normal.x + double(normal.y) * normal.y + double(normal.z) * normal.z;
			const double b = double(decoded.x) * decoded.x + double(decoded.y) * decoded.y + double(decoded.z) * decoded.z;
			const double cosine =
				(double(normal.x) * decoded.x + double(normal.y) * decoded.y + double(normal.z) * decoded.z) / std::sqrt(a * b);
			result.maxNormalAngleDegrees =
				std::max(result.maxNormalAngleDegrees, std::acos(std::clamp(cosine, -1., 1.)) * 180 / std::numbers::pi);
		}
		for (int index = 0; index < layout.triangles.size(); ++index)
		{
			const auto triangle = layout.triangles[index];
			if (!work.step())
			{
				return {};
			}
			const auto original = surface.triangles[index];
			const auto &positions = surface.frames[f].positions;
			const auto before = faceNormal(positions[original.a], positions[original.b], positions[original.c]);
			const auto after = faceNormal(decodedPose[triangle.a], decodedPose[triangle.b], decodedPose[triangle.c]);
			if (before[0] * after[0] + before[1] * after[1] + before[2] * after[2] <= 0)
			{
				return failure(
					error, QCoreApplication::translate(
							   "VibeStudioModelMdl",
							   "MDL byte quantization collapses or reverses triangle %1 in pose %2. Adjust the geometry or model extent.")
							   .arg(index)
							   .arg(f));
			}
		}
		poses << std::move(pose);
	}
	QByteArray output(84, '\0');
	output.replace(0, 4, "IDPO");
	integer(output, 4, 6);
	vector(output, 8, {scale[0], scale[1], scale[2]});
	vector(output, 20, {minimum[0], minimum[1], minimum[2]});
	number(output, 32, float(std::sqrt(radiusSquared)));
	vector(output, 36, mesh.mdl.eyePosition);
	integer(output, 48, mesh.embeddedSkins.size());
	integer(output, 52, size.width());
	integer(output, 56, size.height());
	integer(output, 60, layout.vertices.size());
	integer(output, 64, layout.triangles.size());
	integer(output, 68, mesh.mdl.frameGroups.size());
	integer(output, 72, mesh.mdl.syncType);
	integer(output, 76, qint32(mesh.mdl.flags));
	number(output, 80, mesh.mdl.size);
	bool animatedSkins = false, groupedFrames = false;
	for (const auto &skin : mesh.embeddedSkins)
	{
		appendInteger(output, skin.intervals.isEmpty() ? 0 : 1);
		if (!skin.intervals.isEmpty())
		{
			animatedSkins = true;
			appendInteger(output, skin.indexedFrames.size());
			for (float interval : skin.intervals)
			{
				appendNumber(output, interval);
			}
		}
		for (const auto &pixels : skin.indexedFrames)
		{
			if (!work.check())
			{
				return {};
			}
			output += pixels;
		}
	}
	for (const auto &vertex : layout.vertices)
	{
		appendInteger(output, vertex.seam ? 0x20 : 0);
		appendInteger(output, vertex.s);
		appendInteger(output, vertex.t);
	}
	for (int index = 0; index < layout.triangles.size(); ++index)
	{
		const auto &triangle = layout.triangles[index];
		appendInteger(output, layout.facesFront[index] ? 1 : 0);
		appendInteger(output, triangle.a);
		appendInteger(output, triangle.c);
		appendInteger(output, triangle.b); // Native Quake front faces are clockwise.
	}
	for (const auto &group : mesh.mdl.frameGroups)
	{
		if (!work.step())
		{
			return {};
		}
		appendInteger(output, group.intervals.isEmpty() ? 0 : 1);
		if (!group.intervals.isEmpty())
		{
			groupedFrames = true;
			appendInteger(output, group.frameCount());
			QByteArray bounds(8, '\0');
			for (int axis = 0; axis < 3; ++axis)
			{
				bounds[axis] = char(255);
				for (int f = group.firstFrame; f < group.firstFrame + group.frameCount(); ++f)
				{
					bounds[axis] = char(std::min(int(quint8(bounds[axis])), int(quint8(poses[f][axis]))));
					bounds[4 + axis] = char(std::max(int(quint8(bounds[4 + axis])), int(quint8(poses[f][4 + axis]))));
				}
			}
			output += bounds;
			for (float interval : group.intervals)
			{
				appendNumber(output, interval);
			}
		}
		for (int f = group.firstFrame; f < group.firstFrame + group.frameCount(); ++f)
		{
			output += poses[f];
		}
	}
	if (output.size() > modelFileByteLimit)
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelMdl", "The exported MDL exceeds the model file size limit."));
	}
	if (!work.check())
	{
		return {};
	}
	result.storedVertices = layout.vertices.size();
	result.notes << QCoreApplication::translate("VibeStudioModelMdl",
												"MDL retains indexed skins, native frame groups, group times and model flags. Its game "
												"palette is external; the source preview palette is not embedded.");
	if (mesh.mdl.paletteGenerated)
	{
		result.notes << QCoreApplication::translate(
			"VibeStudioModelMdl",
			"The source uses a generated preview palette. Verify the exported skin indices with the target game's palette.");
	}
	if (!mesh.animations.isEmpty() || !surface.skinPaths.isEmpty())
	{
		result.notes << QCoreApplication::translate("VibeStudioModelMdl",
													"Editor clip names/ranges/FPS and external material paths remain in the mesh source. MDL "
													"uses its embedded skin slots and native frame numbers.");
	}
	if (animatedSkins || groupedFrames)
	{
		result.notes << QCoreApplication::translate(
			"VibeStudioModelMdl", "Original GLQuake uses a four-slot skin cycle and the first frame-group interval. Software Quake uses "
								  "the stored cumulative times; test animated groups in the target engine.");
	}
	if (report)
	{
		if (!mesh.collisionBoxes.isEmpty()) { result.notes << modelCollisionOmissionNote(); }
		*report = std::move(result);
	}
	return output;
}
} // namespace vibestudio
