#include "core/model_ase.h"
#include "core/model_iqm.h"
#include "core/model_md5.h"
#include "core/model_skeletal_export.h"
#include "core/model_skeleton.h"
#include "core/model_document.h"
#include "core/model_collision.h"
#include "core/model_mdl.h"
#include "core/model_targets.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
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
bool over(qint64 value, qint64 maximum) { return maximum > 0 && value > maximum; }

QByteArray md2(const ModelMesh &mesh, const ModelTargetLimits &limits, QString *error, const ModelWorkControl &control,
			   ModelExportReport *report)
{
	ModelWorkProgress work(control, ModelWorkPhase::Serializing, error);
	ModelExportReport result;
	if (!work.check())
	{
		return {};
	}
	if (mesh.surfaces.size() != 1 || over(mesh.frames.size(), limits.frames) || !mesh.tags.isEmpty() || !mesh.embeddedSkins.isEmpty())
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
														  "MD2 requires one surface, at most %1 frames for the delivery target, and no "
														  "attachment tags or embedded skins. Keep these in the editable source or export MD3.")
								  .arg(limits.frames));
	}
	const auto &surface = mesh.surfaces[0];
	const int width = mesh.md2SkinSize.width(), height = mesh.md2SkinSize.height();
	if (over(width, limits.skinWidth) || over(height, limits.skinHeight))
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
														  "The delivery target loads MD2 skins of at most %1 by %2 pixels. Resize the skin "
														  "and use matching PCX images.")
								  .arg(limits.skinWidth)
								  .arg(limits.skinHeight));
	}
	if (over(surface.triangles.size(), limits.triangles) || over(surface.skinPaths.size(), limits.skins) || surface.vertexCount > 32767)
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
														  "The delivery target allows at most %1 triangles and %2 skin slots, and MD2 "
														  "indexes at most 32767 UV vertices.")
								  .arg(limits.triangles)
								  .arg(limits.skins));
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
	QVector<int> representative, positionIndex;
	if (!modelMd2PositionIndex(mesh, &positionIndex, &representative, error, control))
	{
		return {};
	}
	if (over(representative.size(), limits.vertices))
	{
		return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
														  "MD2 stores %1 position/normal vertices after sharing exact UV seams across all "
														  "poses; the delivery target allows %2. Reduce geometry or hard-normal splits.")
								  .arg(representative.size())
								  .arg(limits.vertices));
	}
	QVector<QPair<int, int>> st;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		if (!work.step())
		{
			return {};
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
	if (limits.renderVertices > 0)
	{
		// Engines such as Q2PRO build one drawn vertex per position and texel.
		QSet<qint64> drawn;
		for (const auto &face : surface.triangles)
		{
			for (int v : {face.a, face.b, face.c})
			{
				drawn.insert((qint64(positionIndex[v]) << 32) | (qint64(st[v].first) << 16) | st[v].second);
			}
		}
		if (over(drawn.size(), limits.renderVertices))
		{
			return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
															  "The delivery target builds %1 drawn vertices for this MD2, one per distinct position "
															  "and texel, and holds at most %2. Reduce geometry or UV splits.")
									  .arg(drawn.size())
									  .arg(limits.renderVertices));
		}
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
	// Guidance for every MD2; the clips and frame origins it actually leaves
	// out are reported with exportEditableModel's shared notes.
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
QByteArray md3(const ModelMesh &mesh, const ModelTargetLimits &limits, QString *error, const ModelWorkControl &control)
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
		if (!asciiName(surface.name, 64))
		{
			return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
															  "MD3 surface names must fit 63 printable ASCII characters."));
		}
		if (over(surface.vertexCount, limits.vertices) || over(surface.triangles.size(), limits.triangles))
		{
			return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
															  "The delivery target draws MD3 surfaces of at most %1 vertices and %2 triangles; "
															  "surface \"%3\" has %4 and %5. Split larger surfaces before export.")
									  .arg(limits.vertices)
									  .arg(limits.triangles)
									  .arg(surface.name)
									  .arg(surface.vertexCount)
									  .arg(surface.triangles.size()));
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

// An omission is a note about data the target format does not hold; strict
// exports refuse while there is one.
void omit(ModelExportReport *report, const QString &note)
{
	report->notes << note;
	report->omissions << note;
}

// The first few names, then how many more there are.
QString listed(const QStringList &names, int shown = 8)
{
	if (names.size() <= shown)
	{
		return names.join(QStringLiteral(", "));
	}
	const int more = int(names.size()) - shown;
	return QCoreApplication::translate("VibeStudioModelDocument", "%1 and %n more", nullptr, more).arg(names.mid(0, shown).join(QStringLiteral(", ")));
}

QString formatTitle(const QString &format)
{
	if (format == QStringLiteral("md5mesh"))
	{
		return QCoreApplication::translate("VibeStudioModelDocument", "MD5 mesh");
	}
	if (format == QStringLiteral("md5anim"))
	{
		return QCoreApplication::translate("VibeStudioModelDocument", "MD5 animation");
	}
	return format.toUpper();
}

// The clips a reader of a format without a clip table (MDL, MD2, MD3) does not
// rebuild from the frame names: renamed or regrouped clips, and clips with a
// frame rate.
QStringList clipsLostByFrameNames(const ModelMesh &mesh)
{
	const auto rebuilt = modelAnimationsFromFrameNames(mesh.frames);
	QStringList lost;
	for (const auto &clip : mesh.animations)
	{
		const bool kept = !(clip.framesPerSecond > 0) &&
						  std::any_of(rebuilt.cbegin(), rebuilt.cend(), [&clip](const ModelAnimation &candidate)
									  { return candidate.name == clip.name && candidate.firstFrame == clip.firstFrame && candidate.frameCount == clip.frameCount; });
		if (!kept && !lost.contains(clip.name))
		{
			lost << clip.name;
		}
	}
	return lost;
}

// Frames whose origin was authored: neither unset (zero) nor the centre of the
// frame's bounds, which the MDL and MD2 readers give every frame.
int authoredFrameOrigins(const ModelMesh &mesh)
{
	int authored = 0;
	for (int f = 0; f < mesh.frames.size(); ++f)
	{
		const auto origin = mesh.frames.at(f).origin;
		if (origin.x == 0 && origin.y == 0 && origin.z == 0)
		{
			continue;
		}
		const double infinity = std::numeric_limits<double>::infinity();
		double mins[3]{infinity, infinity, infinity}, maxs[3]{-infinity, -infinity, -infinity};
		for (const auto &surface : mesh.surfaces)
		{
			for (const auto &p : surface.frames.value(f).positions)
			{
				const double values[]{p.x, p.y, p.z};
				for (int axis = 0; axis < 3; ++axis)
				{
					mins[axis] = std::min(mins[axis], values[axis]);
					maxs[axis] = std::max(maxs[axis], values[axis]);
				}
			}
		}
		const double values[]{origin.x, origin.y, origin.z};
		bool centred = mins[0] <= maxs[0];
		for (int axis = 0; centred && axis < 3; ++axis)
		{
			// The readers rebuild the bounds from stored positions, within their precision.
			centred = std::abs(values[axis] - (mins[axis] + maxs[axis]) * 0.5) <= 1e-3 + 1e-4 * (maxs[axis] - mins[axis]);
		}
		authored += !centred;
	}
	return authored;
}

// What MDL, MD2, MD3 and OBJ leave in the editable source besides their own
// notes: clips the frame names do not rebuild (none of them has a clip table),
// frame origins MDL, MD2 and OBJ do not store, and the skeleton, since all four
// store vertex positions only.
void appendVertexFormatNotes(const ModelMesh &mesh, const QString &format, ModelExportReport *report)
{
	const QString title = formatTitle(format);
	const bool md3 = format == QStringLiteral("md3");
	if (format != QStringLiteral("obj"))
	{
		const auto lost = clipsLostByFrameNames(mesh);
		if (!lost.isEmpty())
		{
			omit(report, QCoreApplication::translate("VibeStudioModelDocument",
													 "%1 has no animation clip table: readers rebuild clips from the frame names, so the names, "
													 "ranges or frame rates of these clips stay in the editable source (%n): %2. Name each clip's "
													 "frames after it (stand1, stand2 and so on), and set playback rates in the target game's "
													 "animation files.",
													 nullptr, int(lost.size()))
							 .arg(title, listed(lost)));
		}
		else if (md3 && !mesh.animations.isEmpty())
		{
			report->notes << QCoreApplication::translate(
				"VibeStudioModelDocument", "MD3 keeps every frame pose, frame name and attachment tag. It has no clip table, so readers rebuild "
										   "the clips from the frame names; configure animation ranges and timing for the target game separately.");
		}
	}
	if (!md3)
	{
		const int origins = authoredFrameOrigins(mesh);
		if (origins > 0)
		{
			omit(report, QCoreApplication::translate("VibeStudioModelDocument",
													 "%1 stores no frame origins, and readers place each one at the centre of the frame's bounds. "
													 "Frames whose authored origin stays in the editable source: %n. Export MD3 to keep them.",
													 nullptr, origins)
							 .arg(title));
		}
	}
	if (!mesh.skeleton.isEmpty())
	{
		omit(report, QCoreApplication::translate("VibeStudioModelDocument",
												 "%1 stores vertex positions only, so the skeleton stays in the editable source: its joints (%n), "
												 "weights and skeletal clips. Export MD5 or IQM to keep the skeleton.",
												 nullptr, int(mesh.skeleton.joints.size()))
						 .arg(title));
		// Vertex formats write the baked frames, so clips outside the
		// working set have no motion in the file at all.
		QStringList outside;
		for (int clip = 0; clip < mesh.skeleton.clips.size(); ++clip)
		{
			if (!mesh.skeleton.clips[clip].frames.isEmpty() && !mesh.skeleton.bakedClipForFrame.contains(clip))
			{
				outside << mesh.skeleton.clips[clip].name;
			}
		}
		if (!outside.isEmpty())
		{
			omit(report, QCoreApplication::translate("VibeStudioModelDocument",
													 "%1 writes only the frames of the skeletal working set, so these clips are not in the file: %2. "
													 "Add them with Skeleton > Working Set or model pose --operation working-set, or export MD5 "
													 "or IQM, which write every clip.")
							 .arg(title, outside.join(QStringLiteral(", "))));
		}
	}
}

// What an MD5, IQM or ASE export leaves in the editable source: attachments,
// collision, MDL settings, embedded skins, alternate material slots, frames a
// format cannot animate, and baked frames that no longer follow the skeleton.
// `clip` is the md5anim clip, else -1. False only on cancellation.
bool appendSourceNotes(const ModelMesh &mesh, const QString &format, int clip, ModelExportReport *report, QString *error,
					   const ModelWorkControl &control)
{
	const bool md5mesh = format == QStringLiteral("md5mesh"), md5anim = format == QStringLiteral("md5anim");
	const bool iqm = format == QStringLiteral("iqm"), ase = format == QStringLiteral("ase");
	const QString title = formatTitle(format);
	const auto &skeleton = mesh.skeleton;
	if (!md5anim)
	{
		QStringList tags;
		for (const auto &tag : skeleton.tags)
		{
			if (!tags.contains(tag.name)) { tags << tag.name; }
		}
		for (const auto &tag : mesh.tags)
		{
			if (!tags.contains(tag.name)) { tags << tag.name; }
		}
		if (!tags.isEmpty())
		{
			const int count = int(tags.size());
			omit(report, (md5mesh ? QCoreApplication::translate("VibeStudioModelDocument",
																"MD5 meshes have no attachment tags, so these stay in the editable source (%n): %1. "
																"Doom 3 attaches entities to joints by name; add joints at the attachment points, "
																"or export MD3 to keep the tags.",
																nullptr, count)
						   : iqm   ? QCoreApplication::translate("VibeStudioModelDocument",
																 "IQM has no attachment tags, so these stay in the editable source (%n): %1. Attach "
																 "to joints in the game code, or export MD3 to keep the tags.",
																 nullptr, count)
								   : QCoreApplication::translate("VibeStudioModelDocument",
																 "ASE stores static geometry only, so its attachment tags stay in the editable "
																 "source (%n): %1. Export MD3 to keep the tags.",
																 nullptr, count))
							 .arg(listed(tags)));
		}
		if (!mesh.collisionBoxes.isEmpty())
		{
			omit(report, modelCollisionOmissionNote());
		}
		if (mesh.mdl.enabled)
		{
			omit(report, QCoreApplication::translate(
							 "VibeStudioModelDocument",
							 "MDL native groups, timing, palette and header settings remain in the editable source; %1 does not store them.")
							 .arg(title));
		}
		if (!mesh.embeddedSkins.isEmpty())
		{
			omit(report, QCoreApplication::translate("VibeStudioModelDocument",
													 "%1 names materials by path, so export the embedded skins as textures and assign their paths "
													 "as materials. Embedded skin images that stay in the editable source: %n.",
													 nullptr, int(mesh.embeddedSkins.size()))
							 .arg(title));
		}
		QStringList alternates;
		for (const auto &surface : mesh.surfaces)
		{
			const auto assigned = std::count_if(surface.skinPaths.cbegin(), surface.skinPaths.cend(),
												[](const QString &path) { return !path.trimmed().isEmpty(); });
			if (assigned > 1) { alternates << surface.name; }
		}
		if (!alternates.isEmpty())
		{
			omit(report, QCoreApplication::translate("VibeStudioModelDocument",
													 "Surfaces with alternate material slots that %1 cannot hold, so only the first slot of each "
													 "is written (%n): %2. Provide the alternates as skin files for the target game.",
													 nullptr, int(alternates.size()))
							 .arg(title, listed(alternates)));
		}
	}
	if (ase && mesh.frames.size() > 1)
	{
		omit(report, QCoreApplication::translate("VibeStudioModelDocument",
												 "ASE holds one static frame; Doom 3, Quake 4 and q3map2 misc_model read it as a static model."));
	}
	if (ase && !skeleton.isEmpty())
	{
		omit(report, QCoreApplication::translate("VibeStudioModelDocument",
												 "ASE stores no joints or weights, so the skeleton's %n joint(s) and its clips stay in the "
												 "editable source. Export MD5 or IQM to keep them.",
												 nullptr, int(skeleton.joints.size())));
	}
	if (ase)
	{
		return true;
	}
	if (skeleton.isEmpty())
	{
		// lupdate stalls on arithmetic in a plural count, so it is hoisted.
		const int later = int(mesh.frames.size()) - 1;
		if (md5mesh && later > 0)
		{
			omit(report, QCoreApplication::translate("VibeStudioModelDocument",
													 "An md5mesh holds one pose and this model animates by frames, so it writes the first frame "
													 "and leaves out %n later frame(s). Export MD3 for vertex animation, or add joints and "
													 "skeletal clips.",
													 nullptr, later));
		}
		if (iqm && mesh.frames.size() > 1)
		{
			omit(report, QCoreApplication::translate(
							 "VibeStudioModelDocument",
							 "IQM animates through joints; this model has none, so the first frame is written as a static mesh."));
		}
		return true;
	}
	if (md5mesh)
	{
		return true;
	}
	ModelSkeletalFrameAudit audit;
	if (!auditModelSkeletalFrames(mesh, &audit, error, control))
	{
		return false;
	}
	QStringList editedClips;
	int edited = 0;
	for (int index = 0; index < audit.editedFrames.size(); ++index)
	{
		const int frameClip = audit.editedFrameClips.at(index);
		if (md5anim && frameClip != clip) { continue; }
		++edited;
		const auto name = skeleton.clips.at(frameClip).name;
		if (!editedClips.contains(name)) { editedClips << name; }
	}
	if (edited > 0)
	{
		omit(report, QCoreApplication::translate("VibeStudioModelDocument",
												 "Baked frames that differ from the poses the skeleton produces (%n, in %1): edits made apart "
												 "from the skeleton, or a bind-pose edit the clips do not show yet. %2 writes the "
												 "skeleton's poses, so these differences are not exported; export MD3 to keep the edited frames.",
												 nullptr, edited)
						 .arg(listed(editedClips), title));
	}
	// Frames added or removed by frame edits are no clip sample; the clips no
	// longer know their frames, so an md5anim reports them too, whichever clip
	// it writes.
	if (!audit.unmappedFrames.isEmpty())
	{
		QStringList frames;
		for (int frame : audit.unmappedFrames) { frames << mesh.frames.at(frame).name; }
		omit(report, QCoreApplication::translate("VibeStudioModelDocument",
												 "Frames that are not samples of a skeletal clip, added or rearranged with frame edits (%n): %1. "
												 "%2 writes the skeletal clips, so these frames are not exported; export MD3 to keep them.",
												 nullptr, int(audit.unmappedFrames.size()))
						 .arg(listed(frames), title));
	}
	if (md5anim && clip >= 0 && clip < skeleton.clips.size() && !skeleton.clips.at(clip).frames.isEmpty())
	{
		bool sampled = false;
		for (int frame = 0; frame < mesh.frames.size() && !sampled; ++frame)
		{
			sampled = modelSkeletalClipForFrame(mesh, frame) == clip;
		}
		if (!sampled)
		{
			omit(report, QCoreApplication::translate("VibeStudioModelDocument",
													 "No baked frame is a sample of clip \"%1\" any more (frames were added or removed with frame "
													 "edits), so edits to its frames cannot be checked. The md5anim writes the clip as the skeleton "
													 "holds it.")
							 .arg(skeleton.clips.at(clip).name));
		}
	}
	QStringList changed;
	for (const auto &animation : mesh.animations)
	{
		if (!audit.changedAnimations.contains(animation.name)) { continue; }
		// An md5anim matches the frame animation by its clip's name, or by the
		// clip its frames are samples of.
		bool relevant = !md5anim || animation.name == skeleton.clips.at(clip).name;
		for (int frame = animation.firstFrame; md5anim && !relevant && frame < animation.firstFrame + animation.frameCount; ++frame)
		{
			relevant = modelSkeletalClipForFrame(mesh, frame) == clip;
		}
		if (relevant && !changed.contains(animation.name)) { changed << animation.name; }
	}
	if (!changed.isEmpty())
	{
		omit(report, QCoreApplication::translate("VibeStudioModelDocument",
												 "Frame animations that no longer match a skeletal clip's name, length or rate (%n): %1. %2 "
												 "writes the skeletal clips' names and rates; change clips with Edit Skeletal Animation.",
												 nullptr, int(changed.size()))
						 .arg(listed(changed), title));
	}
	return true;
}

// The export itself; `report` is fresh and the caller applies strictness and
// the delivery target's preflight. `limits` are the target's; `validated`
// says the caller has already validated the mesh.
QByteArray exportNative(const ModelMesh &mesh, const QString &format, const ModelExportOptions &options, const ModelTargetLimits &limits,
						bool validated, QString *error, const ModelWorkControl &control, ModelExportReport *report)
{
	const int frame = options.frame;
	if (format.compare(QStringLiteral("mdl"), Qt::CaseInsensitive) == 0)
	{
		auto result = exportModelMdl(mesh, limits, error, control, report);
		if (!result.isEmpty())
		{
			appendVertexFormatNotes(mesh, QStringLiteral("mdl"), report);
		}
		return result;
	}
	if (error)
	{
		error->clear();
	}
	if (!validated)
	{
		const auto errors = validateEditableModel(mesh, control);
		if (!errors.isEmpty())
		{
			return failure(error, errors.join(QLatin1Char('\n')));
		}
	}
	const QString id = format.toLower();
	if (id == QStringLiteral("md5mesh") || id == QStringLiteral("md5anim") || id == QStringLiteral("iqm"))
	{
		// Skeletal exports pose the edited bind pose: vertices moved there are
		// rebound to their joints first, and untouched ones keep their offsets.
		ModelMesh prepared = mesh;
		const int bindFrame = modelBindPoseFrame(prepared);
		int rebound = 0;
		if (!prepared.skeleton.isEmpty() && bindFrame >= 0)
		{
			rebound = rebindModelSkinning(&prepared, bindFrame);
		}
		const auto reboundNote = [&]()
		{
			if (rebound > 0)
			{
				report->notes << QCoreApplication::translate("VibeStudioModelDocument",
															 "Bind-pose edits to %n vertex(es) were re-bound to their joints for this "
															 "export, so every clip follows them.",
															 nullptr, rebound);
			}
		};
		QByteArray result;
		int clip = -1;
		if (id == QStringLiteral("md5mesh"))
		{
			result = exportModelMd5Mesh(prepared, {}, error, control, report);
			if (!result.isEmpty() && prepared.skeleton.isEmpty())
			{
				report->notes << QCoreApplication::translate("VibeStudioModelDocument",
															 "The model has no joints, so the md5mesh binds every vertex to one origin joint.");
			}
			if (!result.isEmpty())
			{
				reboundNote();
			}
		}
		else if (id == QStringLiteral("md5anim"))
		{
			const auto &clips = prepared.skeleton.clips;
			if (prepared.skeleton.isEmpty() || clips.isEmpty())
			{
				return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																  "An md5anim needs a skeletal clip; this model animates by frames."));
			}
			clip = options.clip;
			if (clip < 0)
			{
				// The clip the frame is a sample of, as the editor shows it.
				if (frame < 0 || frame >= mesh.frames.size())
				{
					return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																	  "Select a valid frame or skeletal clip for MD5 animation export."));
				}
				clip = modelSkeletalClipForFrame(mesh, frame);
				if (clip < 0 && clips.size() == 1)
				{
					clip = 0;
				}
				if (clip < 0)
				{
					return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																	  "Frame %1 is not a sample of a skeletal clip, and the model has %n "
																	  "clip(s). Choose the clip to write as an md5anim: %2.",
																	  nullptr, int(clips.size()))
											  .arg(QString::number(frame), modelSkeletalClipSummaries(prepared.skeleton).join(QStringLiteral("; "))));
				}
			}
			if (clip >= clips.size())
			{
				return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
																  "The model has %n skeletal clip(s), so there is no clip %1. Choose one of: %2.",
																  nullptr, int(clips.size()))
										  .arg(QString::number(clip), modelSkeletalClipSummaries(prepared.skeleton).join(QStringLiteral("; "))));
			}
			ModelMd5ExportOptions md5;
			md5.frameRate = options.md5FrameRate;
			result = exportModelMd5Anim(prepared, clip, md5, error, control, report);
		}
		else
		{
			result = exportModelIqm(prepared, limits, error, control, report);
			if (!result.isEmpty())
			{
				reboundNote();
			}
		}
		if (!result.isEmpty() && !appendSourceNotes(mesh, id, clip, report, error, control))
		{
			return {};
		}
		return result;
	}
	if (id == QStringLiteral("ase"))
	{
		if (frame < 0 || frame >= mesh.frames.size())
		{
			return failure(error, QCoreApplication::translate("VibeStudioModelDocument", "Select a valid frame for ASE export."));
		}
		auto result = exportModelAse(mesh, frame, error, control, report);
		if (!result.isEmpty() && !appendSourceNotes(mesh, id, -1, report, error, control))
		{
			return {};
		}
		return result;
	}
	if (id == QStringLiteral("md3"))
	{
		const auto result = md3(mesh, limits, error, control);
		if (!result.isEmpty() && !mesh.collisionBoxes.isEmpty()) { omit(report, modelCollisionOmissionNote()); }
		if (!result.isEmpty() && mesh.mdl.enabled)
		{
			omit(report, QCoreApplication::translate(
				"VibeStudioModelDocument",
				"MDL native groups, timing, palette and header settings remain in the editable source; MD3 does not store them."));
		}
		if (!result.isEmpty())
		{
			appendVertexFormatNotes(mesh, id, report);
		}
		return result;
	}
	if (id == QStringLiteral("md2"))
	{
		const auto result = md2(mesh, limits, error, control, report);
		if (!result.isEmpty() && !mesh.collisionBoxes.isEmpty()) { omit(report, modelCollisionOmissionNote()); }
		if (!result.isEmpty() && mesh.mdl.enabled)
		{
			omit(report, QCoreApplication::translate(
				"VibeStudioModelDocument",
				"MDL native groups, timing, palette and header settings remain in the editable source; MD2 does not store them."));
		}
		if (!result.isEmpty())
		{
			appendVertexFormatNotes(mesh, id, report);
		}
		return result;
	}
	if (id == QStringLiteral("obj"))
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
			omit(report, modelCollisionOmissionNote());
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
			omit(report, QCoreApplication::translate("VibeStudioModelDocument",
													 "OBJ exports geometry only; attachment tags are omitted. The editable source "
													 "retains them. Use MD3 to export attachments."));
		}
		// One frame holds every pose a one-frame model has, so only other frames,
		// and the clips across them, are left out.
		if (mesh.frames.size() > 1)
		{
			omit(report, QCoreApplication::translate(
				"VibeStudioModelDocument",
				"OBJ exports only the selected frame. Other poses and animation clips remain in the editable source."));
		}
		appendVertexFormatNotes(mesh, id, report);
		if (mesh.mdl.enabled)
		{
			result.prepend(QStringLiteral("# MDL indexed skins, native groups and header settings remain in the mesh source.\n"));
			omit(report, QCoreApplication::translate(
				"VibeStudioModelDocument",
				"OBJ omits MDL indexed skins, native groups and header settings. Retain the editable source or export MDL."));
		}
		return result.toUtf8();
	}
	return failure(error, QCoreApplication::translate("VibeStudioModelDocument",
													  "Editable model export supports MDL, MD2, MD3, MD5 mesh or animation, IQM, ASE, or a "
													  "single OBJ frame."));
}
} // namespace

QStringList modelExportFormatIds()
{
	return {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("obj"), QStringLiteral("md5mesh"),
			QStringLiteral("md5anim"), QStringLiteral("iqm"), QStringLiteral("ase")};
}

QByteArray exportEditableModel(const ModelMesh &mesh, const QString &format, int frame, QString *error, const ModelWorkControl &control,
							   ModelExportReport *report)
{
	ModelExportOptions options;
	options.frame = frame;
	return exportEditableModel(mesh, format, options, error, control, report);
}

QByteArray exportEditableModel(const ModelMesh &mesh, const QString &format, const ModelExportOptions &options, QString *error,
							   const ModelWorkControl &control, ModelExportReport *report)
{
	if (report)
	{
		*report = {};
	}
	const QString id = format.toLower();
	ModelTargetProfile profile;
	const bool targeted = findModelTargetProfile(id, options.target, &profile);
	if (!options.target.trimmed().isEmpty() && !targeted)
	{
		const auto choices = modelTargetChoices(id);
		return failure(error, choices.isEmpty()
								  ? QCoreApplication::translate("VibeStudioModelDocument",
																"%1 export has no delivery targets; leave the target out.")
										.arg(formatTitle(id))
								  : QCoreApplication::translate("VibeStudioModelDocument",
																"There is no %1 delivery target called \"%2\". Choose one of: %3.")
										.arg(formatTitle(id), options.target.trimmed(), choices));
	}
	// The preflight: every limit of the delivery target, before writing.
	ModelTargetReport target;
	if (targeted)
	{
		const auto errors = validateEditableModel(mesh, control);
		if (!errors.isEmpty())
		{
			return failure(error, errors.join(QLatin1Char('\n')));
		}
		if (!assessModelTarget(mesh, profile, &target, error, control))
		{
			return {};
		}
		if (!target.ready())
		{
			QStringList items;
			for (const auto &line : modelTargetIssueLines(target))
			{
				items << QStringLiteral("- ") + line;
			}
			const int blockers = int(target.issues.size());
			failure(error, QCoreApplication::translate("VibeStudioModelDocument",
													   "Nothing was written: the model exceeds %n limit(s) of the %1 delivery target %2.",
													   nullptr, blockers)
								   .arg(formatTitle(id), profile.label) +
							   QLatin1Char('\n') + items.join(QLatin1Char('\n')));
			if (report)
			{
				report->target = std::move(target);
			}
			return {};
		}
	}
	ModelExportReport result;
	QByteArray bytes = exportNative(mesh, format, options, profile.limits, targeted, error, control, &result);
	if (bytes.isEmpty())
	{
		return {};
	}
	result.target = std::move(target);
	if (options.strict && !result.omissions.isEmpty())
	{
		QStringList items;
		for (const auto &omission : std::as_const(result.omissions))
		{
			items << QStringLiteral("- ") + omission;
		}
		failure(error, QCoreApplication::translate("VibeStudioModelDocument",
												   "Strict export refused: the output would leave out %n item(s) the editable source holds.",
												   nullptr, int(result.omissions.size())) +
						   QLatin1Char('\n') + items.join(QLatin1Char('\n')));
		if (report)
		{
			*report = std::move(result);
		}
		return {};
	}
	if (report)
	{
		*report = std::move(result);
	}
	return bytes;
}
} // namespace vibestudio
