#include "core/model_document.h"
#include "core/model_collision.h"
#include "core/model_mdl.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
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
QByteArray failure(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return {};
}
void integer(QByteArray &bytes, int offset, int value) { qToLittleEndian(qint32(value), bytes.data() + offset); }
void number(QByteArray &bytes, int offset, float value)
{
	quint32 bits;
	std::memcpy(&bits, &value, 4);
	qToLittleEndian(bits, bytes.data() + offset);
}
void vector(QByteArray &bytes, int offset, ModelVec3 value)
{
	number(bytes, offset, value.x);
	number(bytes, offset + 4, value.y);
	number(bytes, offset + 8, value.z);
}
void text(QByteArray &bytes, int offset, const QString &value)
{
	const auto ascii = value.toLatin1();
	std::memcpy(bytes.data() + offset, ascii.constData(), ascii.size());
}
bool asciiName(const QString &value, int limit, bool allowEmpty = false)
{
	return (allowEmpty || !value.isEmpty()) && value.size() < limit &&
		   std::all_of(value.cbegin(), value.cend(), [](QChar c) { return c.unicode() >= 32 && c.unicode() < 127; });
}
ModelVec3 quantize(ModelVec3 p) { return {float(std::round(p.x * 64.0)), float(std::round(p.y * 64.0)), float(std::round(p.z * 64.0))}; }
bool collapsed(ModelVec3 a, ModelVec3 b, ModelVec3 c)
{
	const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
	return uy * vz == uz * vy && uz * vx == ux * vz && ux * vy == uy * vx;
}

// Original MD2 writer based on id Software's Quake II qcommon/qfiles.h,
// ref_gl/gl_mesh.c and ref_soft/{r_model,r_image,r_polyse}.c at master,
// reviewed 2026-10-04, GPL-2.0-or-later (compatible with this GPL-3.0 project).
// https://github.com/id-Software/Quake-2
// Texel-centre convention: Quake-2-Tools/qdata/models.c (same licence/date).
// https://github.com/id-Software/Quake-2-Tools/blob/master/qdata/models.c
// No compiler implementation is copied. Each face is an independent GL strip;
// STs and GL commands both sample texel centres. Software-compatible exports
// keep UVs inside the skin because ref_soft indexes pixels without wrapping.
QByteArray md2(const ModelMesh &mesh, QString *error, const ModelWorkControl &control, ModelExportReport *report)
{
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	ModelExportReport result;
	if (!work.check())
	{
		return {};
	}
	if (mesh.surfaces.size() != 1 || mesh.frames.size() > 512 || !mesh.tags.isEmpty() || !mesh.embeddedSkins.isEmpty())
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
														  "MD2 requires one surface, at most 512 frames, and no attachment tags or "
														  "embedded skins. Keep these in the editable source or export MD3."));
	}
	const auto &surface = mesh.surfaces[0];
	const int width = mesh.md2SkinSize.width(), height = mesh.md2SkinSize.height();
	if (width > 640 || height > 480)
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
														  "MD2 export targets Quake II's original renderers. Set skin dimensions at most "
														  "640 by 480 pixels and use matching PCX images."));
	}
	if (surface.triangles.size() > 4096 || surface.skinPaths.size() > 32 || surface.vertexCount > 32767)
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
														  "MD2 supports at most 4096 triangles, 32 skin slots, and 32767 UV vertices."));
	}
	for (const auto &path : surface.skinPaths)
	{
		if (!asciiName(path, 64) || !path.endsWith(QStringLiteral(".pcx")))
		{
			return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
															  "MD2 skin slots must reference package-relative PCX paths of at most 63 "
															  "printable ASCII characters. Assign a PCX material before exporting."));
		}
	}
	for (const auto &frame : mesh.frames)
	{
		if (!asciiName(frame.name, 16, true))
		{
			return failure(
				error, QCoreApplication::translate("VibeStudioModelDocument", "MD2 frame names must fit 15 printable ASCII characters."));
		}
	}

	// Recombine only exact all-pose position/normal duplicates, allowing UV seams
	// to share MD2 XYZ indices. Keep distinct ST indices to preserve authoring
	// corner identity even when coordinates quantize to the same texel.
	QHash<QByteArray, int> positions;
	QVector<int> representative, positionIndex;
	QVector<QPair<int, int>> st;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		if (!work.step())
		{
			return {};
		}
		QByteArray key(mesh.frames.size() * 24, '\0');
		for (int f = 0; f < mesh.frames.size(); ++f)
		{
			if (!work.step())
			{
				return {};
			}
			vector(key, f * 24, surface.frames[f].positions[v]);
			vector(key, f * 24 + 12, surface.frames[f].normals[v]);
		}
		const auto found = positions.constFind(key);
		if (found == positions.cend())
		{
			positionIndex << representative.size();
			positions.insert(key, representative.size());
			representative << v;
			if (representative.size() > 2048)
			{
				return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																  "MD2 exceeds 2048 position/normal vertices after sharing exact UV seams "
																  "across all poses. Reduce geometry or hard-normal splits."));
			}
		}
		else
		{
			positionIndex << found.value();
		}
		const auto uv = surface.texCoords[v];
		if (uv.u < 0 || uv.u > 1 || uv.v < 0 || uv.v > 1)
		{
			return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
															  "MD2 UV vertex %1 lies outside the 0–1 skin tile. Move or scale the UVs into "
															  "the tile for Quake II software-renderer compatibility.")
									  .arg(v));
		}
		const int s = std::clamp(int(std::lround(double(uv.u) * width - 0.5)), 0, width - 1);
		const int t = std::clamp(int(std::lround(double(uv.v) * height - 0.5)), 0, height - 1);
		st << qMakePair(s, t);
		result.maxUvError = std::max({result.maxUvError, std::abs((s + 0.5) / width - uv.u), std::abs((t + 0.5) / height - uv.v)});
	}
	const int vertices = representative.size(), triangles = surface.triangles.size();
	const int frameSize = 40 + 4 * vertices, skinOffset = 68;
	const int stOffset = skinOffset + surface.skinPaths.size() * 64;
	const int triangleOffset = stOffset + st.size() * 4;
	const int frameOffset = triangleOffset + triangles * 12;
	const int commandsOffset = frameOffset + mesh.frames.size() * frameSize;
	const int commandWords = triangles * 10 + 1, end = commandsOffset + commandWords * 4;
	QByteArray bytes(end, '\0');
	const int header[]{int(0x32504449),
					   8,
					   width,
					   height,
					   frameSize,
					   int(surface.skinPaths.size()),
					   vertices,
					   int(st.size()),
					   triangles,
					   commandWords,
					   int(mesh.frames.size()),
					   skinOffset,
					   stOffset,
					   triangleOffset,
					   frameOffset,
					   commandsOffset,
					   end};
	for (int i = 0; i < 17; ++i)
	{
		integer(bytes, i * 4, header[i]);
	}
	for (int i = 0; i < surface.skinPaths.size(); ++i)
	{
		text(bytes, skinOffset + i * 64, surface.skinPaths[i]);
	}
	for (int i = 0; i < st.size(); ++i)
	{
		if (!work.step())
		{
			return {};
		}
		qToLittleEndian(qint16(st[i].first), bytes.data() + stOffset + i * 4);
		qToLittleEndian(qint16(st[i].second), bytes.data() + stOffset + i * 4 + 2);
	}
	for (int i = 0; i < triangles; ++i)
	{
		if (!work.step())
		{
			return {};
		}
		const auto &face = surface.triangles[i];
		// Both native MD2 render streams use clockwise front faces.
		const int corners[]{face.a, face.c, face.b};
		integer(bytes, commandsOffset + i * 40, 3);
		for (int c = 0; c < 3; ++c)
		{
			const int v = corners[c], cmd = commandsOffset + i * 40 + 4 + c * 12;
			qToLittleEndian(qint16(positionIndex[v]), bytes.data() + triangleOffset + i * 12 + c * 2);
			qToLittleEndian(qint16(v), bytes.data() + triangleOffset + i * 12 + 6 + c * 2);
			number(bytes, cmd, (st[v].first + 0.5f) / width);
			number(bytes, cmd + 4, (st[v].second + 0.5f) / height);
			integer(bytes, cmd + 8, positionIndex[v]);
		}
	}
	for (int f = 0; f < mesh.frames.size(); ++f)
	{
		const auto &pose = surface.frames[f];
		ModelVec3 mins = pose.positions[0], maxs = mins;
		for (const auto &p : pose.positions)
		{
			if (!work.step())
			{
				return {};
			}
			mins = {std::min(mins.x, p.x), std::min(mins.y, p.y), std::min(mins.z, p.z)};
			maxs = {std::max(maxs.x, p.x), std::max(maxs.y, p.y), std::max(maxs.z, p.z)};
		}
		const auto step = [](float a, float b) { return float((double(b) - a) / 255.0); };
		const ModelVec3 scale{step(mins.x, maxs.x), step(mins.y, maxs.y), step(mins.z, maxs.z)};
		const int base = frameOffset + f * frameSize;
		vector(bytes, base, scale);
		vector(bytes, base + 12, mins);
		text(bytes, base + 24, mesh.frames[f].name);
		QVector<ModelVec3> decoded(vertices);
		for (int i = 0; i < vertices; ++i)
		{
			if (!work.step())
			{
				return {};
			}
			const auto p = pose.positions[representative[i]], n = pose.normals[representative[i]];
			const float coordinates[]{p.x, p.y, p.z}, offsets[]{mins.x, mins.y, mins.z}, scales[]{scale.x, scale.y, scale.z};
			float restored[3]{};
			double errorSquared = 0;
			for (int axis = 0; axis < 3; ++axis)
			{
				const int quantized =
					scales[axis] > 0 ? std::clamp(int(std::lround((double(coordinates[axis]) - offsets[axis]) / scales[axis])), 0, 255) : 0;
				bytes[base + 40 + i * 4 + axis] = char(quantized);
				restored[axis] = quantized * scales[axis] + offsets[axis];
				const double difference = double(restored[axis]) - coordinates[axis];
				errorSquared += difference * difference;
			}
			decoded[i] = {restored[0], restored[1], restored[2]};
			result.maxPositionError = std::max(result.maxPositionError, std::sqrt(errorSquared));
			const int normal = modelAliasNormalIndex(n);
			bytes[base + 40 + i * 4 + 3] = char(normal);
			const auto match = modelAliasNormal(normal);
			const double lengths = std::sqrt((double(n.x) * n.x + double(n.y) * n.y + double(n.z) * n.z) *
											 (double(match.x) * match.x + double(match.y) * match.y + double(match.z) * match.z));
			const double cosine = (double(n.x) * match.x + double(n.y) * match.y + double(n.z) * match.z) / lengths;
			result.maxNormalAngleDegrees =
				std::max(result.maxNormalAngleDegrees, std::acos(std::clamp(cosine, -1.0, 1.0)) * 180 / std::numbers::pi);
		}
		for (int i = 0; i < triangles; ++i)
		{
			if (!work.step())
			{
				return {};
			}
			const auto t = surface.triangles[i];
			const auto a = decoded[positionIndex[t.a]], b = decoded[positionIndex[t.b]], c = decoded[positionIndex[t.c]];
			const auto normal = [](ModelVec3 x, ModelVec3 y, ModelVec3 z)
			{
				const double u[]{double(y.x) - x.x, double(y.y) - x.y, double(y.z) - x.z};
				const double v[]{double(z.x) - x.x, double(z.y) - x.y, double(z.z) - x.z};
				return std::array<double, 3>{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
			};
			const auto before = normal(pose.positions[t.a], pose.positions[t.b], pose.positions[t.c]), after = normal(a, b, c);
			if (before[0] * after[0] + before[1] * after[1] + before[2] * after[2] <= 0)
			{
				return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																  "MD2 byte precision collapses or reverses triangle %1 in frame %2. "
																  "Increase the feature's size relative to the model or export MD3.")
										  .arg(i)
										  .arg(f));
			}
		}
	}
	result.storedVertices = vertices;
	result.notes << QCoreApplication::translate("VibeStudioModelDocument",
												"MD2 quantizes every pose to 8-bit positions, normals to 162 directions, and UVs to skin "
												"texel centres. Maximum errors: %1 model units, %2 degrees, %3 UV units.")
						.arg(result.maxPositionError, 0, 'g', 6)
						.arg(result.maxNormalAngleDegrees, 0, 'g', 6)
						.arg(result.maxUvError, 0, 'g', 6);
	result.notes << QCoreApplication::translate(
		"VibeStudioModelDocument", "Surface names, frame origins, custom animation ranges/FPS and seam marks remain in the editable source. "
								   "MD2 animation groups are inferred from frame names. Supply matching PCX skins through the package.");
	if (!work.check())
	{
		return {};
	}
	if (report)
	{
		*report = std::move(result);
	}
	return bytes;
}

// Original writer from the public MD3 layout in id Software's qfiles.h,
// GPL-2.0-or-later, reviewed 2026-10-04 (compatible with this GPL-3.0 project):
// https://github.com/id-Software/Quake-III-Arena/blob/master/code/qcommon/qfiles.h
// Vanilla renderer limits are stricter than the nominal MD3 limits:
// https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_model.c
// No upstream implementation is copied. All frames, UVs, skin references and
// attachment tags are emitted. Float positions are rounded to 1/64 units.
QByteArray md3(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	if (!work.check())
	{
		return {};
	}
	if (!mesh.embeddedSkins.isEmpty())
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument", "MD3 cannot embed skin images. Export and assign "
																					 "project textures before converting."));
	}
	for (const auto &frame : mesh.frames)
	{
		if (!work.step())
		{
			return {};
		}
		if (!asciiName(frame.name, 16, true))
		{
			return failure(
				error, QCoreApplication::translate("VibeStudioModelDocument", "MD3 frame names must fit 15 printable ASCII characters."));
		}
	}
	QStringList tagNames;
	for (const auto &tag : mesh.tags)
	{
		if (!work.step())
		{
			return {};
		}
		if (!asciiName(tag.name, 64))
		{
			return failure(error,
						   QCoreApplication::translate("VibeStudioModelDocument", "MD3 tag names must fit 63 printable ASCII characters."));
		}
		if (tag.frameIndex == 0)
		{
			tagNames << tag.name;
		}
	}
	std::sort(tagNames.begin(), tagNames.end());
	const int frames = mesh.frames.size(), tagCount = tagNames.size();
	const int tagOffset = 108 + frames * 56, surfaceOffset = tagOffset + frames * tagCount * 112;
	qint64 total = surfaceOffset;
	QSet<QString> engineSurfaceNames;
	for (const auto &surface : mesh.surfaces)
	{
		if (!work.step())
		{
			return {};
		}
		// R_LoadMD3 lowercases names and strips a final underscore-plus-character.
		// Keep authored names intact, but refuse identities that the game merges.
		auto engineName = surface.name.toLower();
		if (engineName.size() > 2 && engineName[engineName.size() - 2] == QLatin1Char('_'))
		{
			engineName.chop(2);
		}
		if (engineSurfaceNames.contains(engineName))
		{
			return failure(
				error,
				QCoreApplication::translate(
					"VibeStudioModelDocument",
					"MD3 surface names collide after Quake III's case and suffix normalization. Rename the surfaces before export."));
		}
		engineSurfaceNames.insert(engineName);
		if (!asciiName(surface.name, 64) || surface.vertexCount > 1000 || surface.triangles.size() > 2000)
		{
			return failure(error,
						   QCoreApplication::translate("VibeStudioModelDocument", "Quake III-compatible MD3 surfaces need names up to 63 "
																				  "ASCII characters, at most 1000 vertices, and at most "
																				  "2000 triangles. Split larger surfaces before export."));
		}
		for (const auto &material : surface.skinPaths)
		{
			if (!work.step())
			{
				return {};
			}
			if (!asciiName(material, 64))
			{
				return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																  "MD3 material paths must fit 63 printable ASCII characters."));
			}
		}
		total += 108 + surface.skinPaths.size() * 68 + surface.triangles.size() * 12 + surface.vertexCount * 8 +
				 qint64(surface.vertexCount) * frames * 8;
		for (const auto &frame : surface.frames)
		{
			if (!work.step())
			{
				return {};
			}
			for (const auto &p : frame.positions)
			{
				if (!work.step())
				{
					return {};
				}
				const auto q = quantize(p);
				if (q.x < -32768 || q.x > 32767 || q.y < -32768 || q.y > 32767 || q.z < -32768 || q.z > 32767)
				{
					return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																	  "Every MD3 frame must fit coordinates -512 through "
																	  "511.984375. Resize or recenter the model."));
				}
			}
			for (const auto &t : surface.triangles)
			{
				if (!work.step())
				{
					return {};
				}
				if (collapsed(quantize(frame.positions[t.a]), quantize(frame.positions[t.b]), quantize(frame.positions[t.c])))
				{
					return failure(error, QCoreApplication::translate("VibeStudioModelDocument", "MD3 precision would collapse a triangle "
																								 "in an animation frame."));
				}
			}
		}
	}
	if (total > modelDocumentMaxSourceBytes)
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument", "The MD3 output exceeds 64 MiB."));
	}
	QByteArray bytes(static_cast<int>(total), '\0');
	text(bytes, 0, QStringLiteral("IDP3"));
	integer(bytes, 4, 15);
	text(bytes, 8, QStringLiteral("vibestudio"));
	integer(bytes, 76, frames);
	integer(bytes, 80, tagCount);
	integer(bytes, 84, mesh.surfaces.size());
	integer(bytes, 92, 108);
	integer(bytes, 96, tagOffset);
	integer(bytes, 100, surfaceOffset);
	integer(bytes, 104, total);
	for (int f = 0; f < frames; ++f)
	{
		if (!work.step())
		{
			return {};
		}
		const float infinity = std::numeric_limits<float>::infinity();
		ModelVec3 mins{infinity, infinity, infinity}, maxs{-infinity, -infinity, -infinity};
		double radius = 0;
		for (const auto &surface : mesh.surfaces)
		{
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, error))
			{
				return {};
			}
			if (!work.step())
			{
				return {};
			}
			for (const auto &p : surface.frames[f].positions)
			{
				if (!work.step())
				{
					return {};
				}
				auto q = quantize(p);
				q = {q.x / 64, q.y / 64, q.z / 64};
				mins = {std::min(mins.x, q.x), std::min(mins.y, q.y), std::min(mins.z, q.z)};
				maxs = {std::max(maxs.x, q.x), std::max(maxs.y, q.y), std::max(maxs.z, q.z)};
				const auto origin = mesh.frames[f].origin;
				const double x = q.x - origin.x, y = q.y - origin.y, z = q.z - origin.z;
				radius = std::max(radius, x * x + y * y + z * z);
			}
		}
		const int offset = 108 + f * 56;
		vector(bytes, offset, mins);
		vector(bytes, offset + 12, maxs);
		vector(bytes, offset + 24, mesh.frames[f].origin);
		number(bytes, offset + 36, float(std::sqrt(radius)));
		text(bytes, offset + 40, mesh.frames[f].name);
		for (int t = 0; t < tagCount; ++t)
		{
			if (!work.step())
			{
				return {};
			}
			const auto found = std::find_if(mesh.tags.cbegin(), mesh.tags.cend(),
											[&](const ModelTag &tag) { return tag.frameIndex == f && tag.name == tagNames[t]; });
			const int base = tagOffset + (f * tagCount + t) * 112;
			text(bytes, base, found->name);
			vector(bytes, base + 64, found->origin);
			for (int i = 0; i < 9; ++i)
			{
				if (!work.step())
				{
					return {};
				}
				number(bytes, base + 76 + i * 4, found->axis[i]);
			}
		}
	}
	int offset = surfaceOffset;
	for (const auto &surface : mesh.surfaces)
	{
		if (!work.step())
		{
			return {};
		}
		const int triangles = surface.triangles.size(), vertices = surface.vertexCount, shaders = surface.skinPaths.size();
		const int triangleOffset = 108 + shaders * 68, uvOffset = triangleOffset + triangles * 12, vertexOffset = uvOffset + vertices * 8;
		const int end = vertexOffset + vertices * frames * 8;
		text(bytes, offset, QStringLiteral("IDP3"));
		text(bytes, offset + 4, surface.name);
		integer(bytes, offset + 72, frames);
		integer(bytes, offset + 76, shaders);
		integer(bytes, offset + 80, vertices);
		integer(bytes, offset + 84, triangles);
		integer(bytes, offset + 88, triangleOffset);
		integer(bytes, offset + 92, 108);
		integer(bytes, offset + 96, uvOffset);
		integer(bytes, offset + 100, vertexOffset);
		integer(bytes, offset + 104, end);
		for (int i = 0; i < shaders; ++i)
		{
			if (!work.step())
			{
				return {};
			}
			text(bytes, offset + 108 + i * 68, surface.skinPaths[i]);
		}
		for (int i = 0; i < triangles; ++i)
		{
			if (!work.step())
			{
				return {};
			}
			const auto &t = surface.triangles[i];
			const int base = offset + triangleOffset + i * 12;
			integer(bytes, base, t.a);
			// Native MD3 render triangles use clockwise front faces.
			integer(bytes, base + 4, t.c);
			integer(bytes, base + 8, t.b);
		}
		for (int i = 0; i < vertices; ++i)
		{
			if (!work.step())
			{
				return {};
			}
			number(bytes, offset + uvOffset + i * 8, surface.texCoords[i].u);
			number(bytes, offset + uvOffset + i * 8 + 4, surface.texCoords[i].v);
		}
		for (int f = 0; f < frames; ++f)
		{
			if (!work.step())
			{
				return {};
			}
			for (int i = 0; i < vertices; ++i)
			{
				if (!work.step())
				{
					return {};
				}
				const int base = offset + vertexOffset + (f * vertices + i) * 8;
				const auto p = quantize(surface.frames[f].positions[i]);
				qToLittleEndian(qint16(p.x), bytes.data() + base);
				qToLittleEndian(qint16(p.y), bytes.data() + base + 2);
				qToLittleEndian(qint16(p.z), bytes.data() + base + 4);
				const auto n = surface.frames[f].normals[i];
				const double length = std::sqrt(double(n.x) * n.x + double(n.y) * n.y + double(n.z) * n.z);
				const int latitude = int(std::lround(std::atan2(n.y, n.x) * 256 / (2 * std::numbers::pi))) & 255;
				const int longitude = int(std::lround(std::acos(std::clamp(n.z / length, -1.0, 1.0)) * 256 / (2 * std::numbers::pi))) & 255;
				qToLittleEndian(quint16((latitude << 8) | longitude), bytes.data() + base + 6);
			}
		}
		offset += end;
	}
	if (!work.check())
	{
		return {};
	}
	return bytes;
}
} // namespace

QByteArray exportEditableModel(const ModelMesh &mesh, const QString &format, int frame, QString *error, const ModelWorkControl &control,
							   ModelExportReport *report)
{
	if (format.compare(QStringLiteral("mdl"), Qt::CaseInsensitive) == 0)
	{
		return exportModelMdl(mesh, error, control, report);
	}
	if (report)
	{
		*report = {};
	}
	if (error)
	{
		error->clear();
	}
	const auto errors = validateEditableModel(mesh, control);
	if (!errors.isEmpty())
	{
		return failure(error, errors.join(QLatin1Char('\n')));
	}
	if (format.compare(QStringLiteral("md3"), Qt::CaseInsensitive) == 0)
	{
		const auto result = md3(mesh, error, control);
		if (!result.isEmpty() && report && !mesh.collisionBoxes.isEmpty()) { report->notes << modelCollisionOmissionNote(); }
		if (!result.isEmpty() && report && mesh.mdl.enabled)
		{
			report->notes << QCoreApplication::translate(
				"VibeStudioModelDocument",
				"MDL native groups, timing, palette and header settings remain in the editable source; MD3 does not store them.");
		}
		if (!result.isEmpty() && report && !mesh.animations.isEmpty())
		{
			report->notes << QCoreApplication::translate(
				"VibeStudioModelDocument",
				"MD3 preserves frame poses and attachment tags, but custom animation clip names, ranges and FPS remain "
				"in the editable source. Configure animation ranges and timing separately for the target game.");
		}
		return result;
	}
	if (format.compare(QStringLiteral("md2"), Qt::CaseInsensitive) == 0)
	{
		const auto result = md2(mesh, error, control, report);
		if (!result.isEmpty() && report && !mesh.collisionBoxes.isEmpty()) { report->notes << modelCollisionOmissionNote(); }
		if (!result.isEmpty() && report && mesh.mdl.enabled)
		{
			report->notes << QCoreApplication::translate(
				"VibeStudioModelDocument",
				"MDL native groups, timing, palette and header settings remain in the editable source; MD2 does not store them.");
		}
		return result;
	}
	if (format.compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0)
	{
		if (frame < 0 || frame >= mesh.frames.size())
		{
			return failure(error, QCoreApplication::translate("VibeStudioModelDocument", "Select a valid frame for OBJ export."));
		}
		// OBJ group/material names are tokens. Refuse ambiguous names instead
		// of silently renaming groups or creating extra material assignments.
		const QRegularExpression token(QStringLiteral("^[A-Za-z0-9_./-]+$"));
		for (const auto &surface : mesh.surfaces)
		{
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, error))
			{
				return {};
			}
			if (!token.match(surface.name).hasMatch() ||
				(!surface.skinPaths.isEmpty() && !token.match(surface.skinPaths.first()).hasMatch()))
			{
				return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																  "OBJ surface and material names must use ASCII letters, "
																  "digits, underscores, dots, slashes, or hyphens."));
			}
		}
		auto result = exportModelFrameObj(mesh, frame, {}, control, error);
		if (!mesh.collisionBoxes.isEmpty())
		{
			result.prepend(QStringLiteral("# Collision boxes remain in the mesh source; export collision brushes separately.\n"));
			if (report) { report->notes << modelCollisionOmissionNote(); }
		}
		for (const auto &surface : mesh.surfaces)
		{
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 0, 0, error))
			{
				return {};
			}
			// Material state persists across OBJ groups. Explicitly clear it for
			// an unassigned surface following an assigned one.
			result.replace(QStringLiteral("g %1\n").arg(surface.name),
				QStringLiteral("g %1\nusemtl %2\n").arg(surface.name, surface.skinPaths.value(0)));
		}
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 1, 1, error))
		{
			return {};
		}
		if (!mesh.tags.isEmpty())
		{
			result.prepend(QStringLiteral(
				"# Attachment tags are omitted: OBJ stores geometry only. Retain the mesh source or use MD3 for attachments.\n"));
			if (report)
			{
				report->notes << QCoreApplication::translate("VibeStudioModelDocument",
															 "OBJ exports geometry only; attachment tags are omitted. The editable source "
															 "retains them. Use MD3 to export attachments.");
			}
		}
		if (report && (mesh.frames.size() > 1 || !mesh.animations.isEmpty()))
		{
			report->notes << QCoreApplication::translate(
				"VibeStudioModelDocument",
				"OBJ exports only the selected frame. Other poses and animation clips remain in the editable source.");
		}
		if (mesh.mdl.enabled)
		{
			result.prepend(QStringLiteral("# MDL indexed skins, native groups and header settings remain in the mesh source.\n"));
			if (report)
			{
				report->notes << QCoreApplication::translate(
					"VibeStudioModelDocument",
					"OBJ omits MDL indexed skins, native groups and header settings. Retain the editable source or export MDL.");
			}
		}
		return result.toUtf8();
	}
	return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
													  "Editable model export supports MDL, MD2, MD3, or a single OBJ frame."));
}
} // namespace vibestudio
