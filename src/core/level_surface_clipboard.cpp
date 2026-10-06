#include "core/level_surface_clipboard.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include "core/level_patch.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <cmath>

namespace vibestudio {
namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(LevelSurfaceClipboard) };
bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }
QString materialKey(QString name) { return name.trimmed().replace('\\', '/').toCaseFolded(); }
std::pair<bool, bool> sizeNeeds(const LevelMapBrushFace& source, const LevelMapBrushFace& target, const LevelSurfacePasteOptions& options)
{
	// Native Values stores a texel-scaled clipboard matrix, then normalizes it
	// for the destination image (NRC brush_primit.h removeScale/addScale;
	// pinned 68ecbed6, GPL-2.0-or-later behavior reference; see CREDITS.md).
	const bool values = options.mode == LevelSurfacePasteMode::RadiantValues || options.mode == LevelSurfacePasteMode::RadiantProject;
	return {source.explicitTextureMatrix && (!target.explicitTextureMatrix || options.mappingOnly || values),
		target.explicitTextureMatrix && (!source.explicitTextureMatrix || options.mappingOnly || values)};
}
bool transferFactors(const LevelMapBrushFace& source, const LevelMapBrushFace& target, const LevelSurfacePasteOptions& options,
	double* u, double* v, QString* error)
{
	*u = 1; *v = 1;
	const auto [from, to] = sizeNeeds(source, target, options);
	const auto dimensions = [&](const QString& material, bool copied, QSize* size) {
		*size = copied && options.textureSize.isValid()
			? options.textureSize : options.materialSizes.value(materialKey(material));
		// Portable callers may supply one shared size for a full-material paste.
		// GUI callers verify destination sizes independently before preparation.
		if (!copied && !options.mappingOnly && !size->isValid()) { *size = options.textureSize; }
		return (size->width() > 0 && size->height() > 0 && size->width() <= 65536 && size->height() <= 65536)
			|| fail(error, Text::tr("Surface transfer needs the actual image dimensions of %1.").arg(material));
	};
	QSize size;
	if (from) { if (!dimensions(source.textureName, true, &size)) { return false; } *u *= size.width(); *v *= size.height(); }
	if (to) {
		if (!dimensions(options.mappingOnly ? target.textureName : source.textureName, false, &size)) { return false; }
		*u /= size.width(); *v /= size.height();
	}
	return true;
}
QString kind(const LevelMapBrushFace& face)
{
	return face.explicitTextureMatrix ? QStringLiteral("matrix") : face.explicitTextureAxes ? QStringLiteral("valve220") : QStringLiteral("classic");
}
MapPlane plane(const LevelMapBrushFace& face)
{
	if (!face.explicitPlane) { return planeFromPoints(face.p0, face.p1, face.p2); }
	const auto& n = face.planeNormal;
	const double length = std::hypot(n.x, n.y, n.z);
	if (!n.valid || !std::isfinite(length) || length < 1e-12 || !std::isfinite(face.planeDistance)) { return {}; }
	return {n.x / length, n.y / length, n.z / length, -face.planeDistance / length, true};
}
double mappingArea(const LevelMapBrushFace& face)
{
	const auto p = plane(face); const auto uv = levelTextureProjection(face);
	return (uv.u.y * uv.v.z - uv.u.z * uv.v.y) * p.normalX
		+ (uv.u.z * uv.v.x - uv.u.x * uv.v.z) * p.normalY + (uv.u.x * uv.v.y - uv.u.y * uv.v.x) * p.normalZ;
}
bool valid(const LevelMapBrushFace& face, QString* error, bool allowEdgeOn = false)
{
	if (face.textureName.isEmpty() || face.textureName.size() > 1024 || face.textureName.contains(QStringLiteral("//"))
		|| face.textureName.contains(QStringLiteral("/*")) || face.textureName.contains(QStringLiteral("*/"))) {
		return fail(error, Text::tr("The copied surface needs a valid material name of at most 1,024 characters."));
	}
	for (const auto c : face.textureName) {
		if (c.isSpace() || c.category() == QChar::Other_Control || QStringLiteral("\"{}()[]").contains(c)) {
			return fail(error, Text::tr("The copied material name contains a character that cannot be written to a map."));
		}
	}
	const auto number = [](double value) { return std::isfinite(value) && std::abs(value) <= 1e6; };
	if (face.explicitTextureMatrix) {
		for (auto value : face.textureMatrix) { if (!number(value)) { return fail(error, Text::tr("Copied mapping values must be finite and within a million.")); } }
	} else {
		for (auto value : {face.rotation, face.scaleX, face.scaleY, face.explicitTextureAxes ? face.uOffset : face.shiftX,
			face.explicitTextureAxes ? face.vOffset : face.shiftY}) {
			if (!number(value)) { return fail(error, Text::tr("Copied mapping values must be finite and within a million.")); }
		}
		if (face.explicitTextureAxes) {
			for (auto value : {face.uAxis.x, face.uAxis.y, face.uAxis.z, face.vAxis.x, face.vAxis.y, face.vAxis.z}) {
				if (!number(value)) { return fail(error, Text::tr("Copied mapping values must be finite and within a million.")); }
			}
		}
	}
	for (qint64 value : {face.contentFlags, face.surfaceFlags}) {
		if (value < -2147483648LL || value > 4294967295LL) { return fail(error, Text::tr("Surface flags must fit a 32-bit map field.")); }
	}
	if (face.surfaceValue < -2147483648LL || face.surfaceValue > 2147483647LL) { return fail(error, Text::tr("The surface value must fit a signed 32-bit map field.")); }
	const auto p = plane(face);
	const auto uv = levelTextureProjection(face);
	const double area = mappingArea(face);
	if (!p.valid || std::abs(p.distance) > 1e9 || !uv.valid || !std::isfinite(area) || (!allowEdgeOn && std::abs(area) <= 1e-14)) {
		return fail(error, Text::tr("The surface mapping is invalid or collapses on the face. Choose a different source or projection."));
	}
	return true;
}
QJsonObject settings(const LevelMapBrushFace& face)
{
	QJsonObject mapping{{"kind", kind(face)}};
	if (face.explicitTextureMatrix) {
		QJsonArray matrix; for (auto n : face.textureMatrix) { matrix.append(n); }
		mapping.insert(QStringLiteral("matrix"), matrix);
	} else {
		mapping.insert(QStringLiteral("rotation"), face.rotation);
		mapping.insert(QStringLiteral("scale"), QJsonArray{face.scaleX, face.scaleY});
		if (face.explicitTextureAxes) {
			mapping.insert(QStringLiteral("u"), QJsonArray{face.uAxis.x, face.uAxis.y, face.uAxis.z, face.uOffset});
			mapping.insert(QStringLiteral("v"), QJsonArray{face.vAxis.x, face.vAxis.y, face.vAxis.z, face.vOffset});
		} else { mapping.insert(QStringLiteral("shift"), QJsonArray{face.shiftX, face.shiftY}); }
	}
	return {{"material", face.textureName}, {"mapping", mapping}, {"flags", QJsonArray{face.contentFlags, face.surfaceFlags, face.surfaceValue}}};
}
bool keys(const QJsonObject& value, const QStringList& expected)
{
	const auto actual = value.keys();
	return QSet<QString>(actual.cbegin(), actual.cend()) == QSet<QString>(expected.cbegin(), expected.cend());
}
bool numbers(const QJsonValue& value, int count, QVector<double>* out)
{
	if (!value.isArray() || value.toArray().size() != count) { return false; }
	out->clear();
	for (const auto entry : value.toArray()) { if (!entry.isDouble() || !std::isfinite(entry.toDouble())) { return false; } out->append(entry.toDouble()); }
	return true;
}
} // namespace

QString LevelSurfaceClipboard::mappingKind() const { return ready() ? kind(m_face) : QString(); }
bool copyLevelSurface(const LevelMapDocument& document, LevelSurfaceFace ref, LevelSurfaceClipboard* clipboard, QString* error)
{
	if (error) { error->clear(); }
	if (!clipboard) { return fail(error, Text::tr("Missing surface clipboard.")); }
	*clipboard = {};
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error, Text::tr("Surface copy requires a brush face in a Quake-family text map."));
	}
	for (const auto& brush : document.brushes) {
		if (brush.id != ref.brushId) { continue; }
		if (ref.faceIndex < 0 || ref.faceIndex >= brush.faces.size()) { break; }
		auto face = brush.faces[ref.faceIndex];
		if (!valid(face, error)) { return false; }
		const auto p = plane(face);
		face.p0 = {}; face.p1 = {}; face.p2 = {}; face.id = -1; face.line = 0;
		face.explicitPlane = true; face.planeNormal = {p.normalX, p.normalY, p.normalZ, true}; face.planeDistance = -p.distance;
		face.textureDirty = false; face.textureParametersDirty = false;
		clipboard->m_face = face; clipboard->m_ready = true; return true;
	}
	return fail(error, Text::tr("The source brush face does not exist."));
}
QByteArray serializeLevelSurfaceClipboard(const LevelSurfaceClipboard& clipboard)
{
	if (!clipboard.ready()) { return {}; }
	auto object = settings(clipboard.face());
	object.insert(QStringLiteral("schema"), QStringLiteral("vibestudio.surface-clipboard")); object.insert(QStringLiteral("version"), 1);
	const auto p = plane(clipboard.face());
	object.insert(QStringLiteral("plane"), QJsonArray{p.normalX, p.normalY, p.normalZ, -p.distance});
	return QJsonDocument(object).toJson(QJsonDocument::Indented);
}
bool parseLevelSurfaceClipboard(const QByteArray& bytes, LevelSurfaceClipboard* clipboard, QString* error)
{
	if (error) { error->clear(); }
	if (!clipboard) { return fail(error, Text::tr("Missing surface clipboard.")); }
	*clipboard = {};
	const auto bad = [&] { return fail(error, Text::tr("Invalid surface clipboard. Use a version 1 VibeStudio surface file with complete numeric fields.")); };
	if (bytes.isEmpty() || bytes.size() > 16384) { return bad(); }
	QJsonParseError parse; const auto json = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !json.isObject()) { return bad(); }
	const auto object = json.object();
	if (!keys(object, {"schema", "version", "material", "mapping", "plane", "flags"})
		|| object.value("schema") != QStringLiteral("vibestudio.surface-clipboard") || object.value("version") != 1
		|| !object.value("material").isString() || !object.value("mapping").isObject()) { return bad(); }
	LevelMapBrushFace face; face.textureName = object.value("material").toString();
	QVector<double> values;
	if (!numbers(object.value("plane"), 4, &values)) { return bad(); }
	face.explicitPlane = true; face.planeNormal = {values[0], values[1], values[2], true}; face.planeDistance = values[3];
	if (!numbers(object.value("flags"), 3, &values)) { return bad(); }
	for (auto n : values) { if (n != std::trunc(n) || n < -2147483648.0 || n > 4294967295.0) { return bad(); } }
	face.contentFlags = static_cast<qint64>(values[0]); face.surfaceFlags = static_cast<qint64>(values[1]); face.surfaceValue = static_cast<qint64>(values[2]);
	const auto mapping = object.value("mapping").toObject(); const auto family = mapping.value("kind").toString();
	if (family == QLatin1String("matrix")) {
		if (!keys(mapping, {"kind", "matrix"}) || !numbers(mapping.value("matrix"), 6, &values)) { return bad(); }
		face.explicitTextureMatrix = true;
		for (int i = 0; i < 6; ++i) { face.textureMatrix[i] = values[i]; }
	} else {
		if (family != QLatin1String("classic") && family != QLatin1String("valve220")) { return bad(); }
		if (!keys(mapping, family == QLatin1String("classic") ? QStringList{"kind", "rotation", "scale", "shift"}
			: QStringList{"kind", "rotation", "scale", "u", "v"}) || !mapping.value("rotation").isDouble()
			|| !numbers(mapping.value("scale"), 2, &values)) { return bad(); }
		face.rotation = mapping.value("rotation").toDouble(); face.scaleX = values[0]; face.scaleY = values[1];
		face.explicitTextureAxes = family == QLatin1String("valve220");
		if (face.explicitTextureAxes) {
			if (!numbers(mapping.value("u"), 4, &values)) { return bad(); }
			face.uAxis = {values[0], values[1], values[2], true}; face.uOffset = values[3];
			if (!numbers(mapping.value("v"), 4, &values)) { return bad(); }
			face.vAxis = {values[0], values[1], values[2], true}; face.vOffset = values[3];
		} else {
			if (!numbers(mapping.value("shift"), 2, &values)) { return bad(); }
			face.shiftX = values[0]; face.shiftY = values[1];
		}
	}
	if (!valid(face, error)) { return false; }
	clipboard->m_face = face; clipboard->m_ready = true; return true;
}
QVector<LevelMaterialTarget> levelSurfacePasteSelectionTargets(const LevelMapDocument& document)
{
	QVector<LevelMaterialTarget> result;
	for (const auto& face : levelMapSelectedSurfaces(document)) { result.append({LevelMaterialKind::BrushFace, face.brushId, face.faceIndex}); }
	QSet<int> patches, entities;
	for (const auto& selected : document.selection) {
		if (selected.kind == LevelMapSelectionKind::QuakePatch) { patches.insert(selected.objectId); }
		if (selected.kind == LevelMapSelectionKind::Entity) { entities.insert(selected.objectId); }
	}
	for (const auto& patch : document.patches) {
		if (patches.contains(patch.id) || entities.contains(patch.entityId)) { result.append({LevelMaterialKind::Patch, patch.id, 0}); }
	}
	return result;
}
LevelSurfaceTransferMaterials levelSurfaceTransferRequiredMaterials(const LevelMapDocument& document, const QVector<LevelMaterialTarget>& targets,
	const LevelSurfaceClipboard& clipboard, const LevelSurfacePasteOptions& options)
{
	if (!clipboard.ready()) { return {}; }
	QSet<LevelMaterialTarget> wanted(targets.cbegin(), targets.cend());
	if (options.includeSelection) { for (const auto& target : levelSurfacePasteSelectionTargets(document)) { wanted.insert(target); } }
	QSet<QString> names;
	LevelSurfaceTransferMaterials result;
	for (const auto& brush : document.brushes) {
		for (int i = 0; i < brush.faces.size(); ++i) {
			if (!wanted.contains({LevelMaterialKind::BrushFace, brush.id, i})) { continue; }
			const auto& face = brush.faces[i];
			const auto [from, to] = sizeNeeds(clipboard.face(), face, options);
			result.sourceSizeRequired |= from;
			if (to) { names.insert(materialKey(options.mappingOnly ? face.textureName : clipboard.material())); }
		}
	}
	if (options.mode == LevelSurfacePasteMode::RadiantProject) {
		for (const auto& patch : document.patches) {
			if (!wanted.contains({LevelMaterialKind::Patch, patch.id, 0})) { continue; }
			result.sourceSizeRequired |= clipboard.face().explicitTextureMatrix;
			names.insert(materialKey(options.mappingOnly ? patch.textureName : clipboard.material()));
		}
	}
	result.targets = names.values(); result.targets.sort(); return result;
}
bool prepareLevelSurfacePaste(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces,
	const LevelSurfaceClipboard& clipboard, const LevelSurfacePasteOptions& options, LevelSurfaceEditPlan* plan,
	QString* error, const std::function<bool()>& cancelled)
{
	QVector<LevelMaterialTarget> targets; targets.reserve(faces.size());
	for (const auto& face : faces) { targets.append({LevelMaterialKind::BrushFace, face.brushId, face.faceIndex}); }
	return prepareLevelSurfaceTransfer(document, targets, clipboard, options, plan, error, cancelled);
}
bool prepareLevelSurfaceTransfer(const LevelMapDocument& document, const QVector<LevelMaterialTarget>& explicitTargets,
	const LevelSurfaceClipboard& clipboard, const LevelSurfacePasteOptions& options, LevelSurfaceEditPlan* plan,
	QString* error, const std::function<bool()>& cancelled)
{
	if (error) { error->clear(); }
	if (!plan) { return fail(error, Text::tr("Missing surface edit plan.")); }
	*plan = {};
	if (!clipboard.ready()) { return fail(error, Text::tr("Copy a brush surface before pasting.")); }
	if (!valid(clipboard.face(), error)) { return false; }
	if (document.format != LevelMapFormat::QuakeMap && document.format != LevelMapFormat::Quake3Map) {
		return fail(error, Text::tr("Surface paste requires a Quake-family text map."));
	}
	if (options.mode != LevelSurfacePasteMode::Parameters && options.mode != LevelSurfacePasteMode::Project
		&& options.mode != LevelSurfacePasteMode::Seamless && options.mode != LevelSurfacePasteMode::RadiantValues && options.mode != LevelSurfacePasteMode::RadiantProject) {
		return fail(error, Text::tr("Unknown surface paste mode."));
	}
	const bool parameters = options.mode == LevelSurfacePasteMode::Parameters || options.mode == LevelSurfacePasteMode::RadiantValues;
	const bool nativeProject = options.mode == LevelSurfacePasteMode::RadiantProject;
	const bool selected = parameters || nativeProject;
	if (explicitTargets.isEmpty() || explicitTargets.size() > 16384 || (selected && options.allowValve220)) {
		return fail(error, Text::tr("Paste needs 1–16,384 surfaces. Valve 220 conversion requires world projection or seamless wrapping."));
	}
	if (options.includeSelection && !selected) { return fail(error, Text::tr("Selection-inclusive paste requires parameters, Radiant values or Radiant projection.")); }
	const QSet<LevelMaterialTarget> explicitSet(explicitTargets.cbegin(), explicitTargets.cend());
	auto all = explicitSet;
	if (options.includeSelection) { for (const auto& target : levelSurfacePasteSelectionTargets(document)) { all.insert(target); } }
	if (all.size() > 16384) { return fail(error, Text::tr("Surface transfer exceeds 16,384 targets, including the selection.")); }
	QMap<int, QSet<int>> targets;
	QSet<int> patchTargets;
	for (const auto& ref : all) {
		if (ref.kind == LevelMaterialKind::BrushFace) { targets[ref.objectId].insert(ref.faceIndex); }
		else if (ref.kind == LevelMaterialKind::Patch && selected && ref.faceIndex == 0) { patchTargets.insert(ref.objectId); }
		else { return fail(error, Text::tr("Transfer needs brush faces. Parameter modes support patch materials; Radiant projection also projects patch UVs.")); }
	}
	if (patchTargets.size() > 256) { return fail(error, Text::tr("Surface transfer exceeds 256 patches.")); }
	QHash<int, const LevelMapBrush*> brushes;
	for (const auto& brush : document.brushes) {
		if (cancelled && cancelled()) { return fail(error, Text::tr("Surface paste cancelled.")); }
		if (targets.contains(brush.id)) { brushes.insert(brush.id, &brush); }
	}
	LevelSurfaceEditPlan result;
	result.m_revision = document.revision; result.m_sourcePath = document.sourcePath; result.m_sourceHash = document.sourceContentHash;
	result.m_format = document.format; result.m_paste = true;
	result.m_bindSelection = options.includeSelection; result.m_selection = document.selection;
	const auto& source = clipboard.face();
	bool convertsClassic = false;
	for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
		if (cancelled && cancelled()) { return fail(error, Text::tr("Surface paste cancelled.")); }
		const auto* found = brushes.value(it.key());
		if (!found || found->faces.size() > 128) { return fail(error, Text::tr("Brush %1 is missing or exceeds the 128-face paste limit.").arg(it.key())); }
		auto after = *found; int changed = 0;
		for (int index : *it) {
			if (cancelled && cancelled()) { return fail(error, Text::tr("Surface paste cancelled.")); }
			if (index < 0 || index >= after.faces.size()) { return fail(error, Text::tr("Brush %1 has no face %2.").arg(it.key()).arg(index + 1)); }
			auto& target = after.faces[index]; const auto& before = found->faces[index];
			if (selected && kind(target) != kind(source)) { return fail(error, Text::tr("Native paste requires matching brush mapping formats. Choose World Projection to convert this surface.")); }
			double u = 1, v = 1;
			if (!transferFactors(source, target, options, &u, &v, error)) { return false; }
			if (!parameters && (!nativeProject || source.explicitTextureMatrix)) {
				auto projection = options.mode == LevelSurfacePasteMode::Seamless ? wrappedLevelTextureProjection(source, target) : levelTextureProjection(source);
				projection.u.x *= u; projection.u.y *= u; projection.u.z *= u; projection.offsetU *= u;
				projection.v.x *= v; projection.v.y *= v; projection.v.z *= v; projection.offsetV *= v;
				projection.normalizedCoordinates = target.explicitTextureMatrix;
				if (!setLevelTextureProjection(&target, projection, options.allowValve220, error)) { return false; }
				if (nativeProject) {
					// NRC BPTexdef_fromST011 normalizes projected matrix offsets by
					// whole repeats (brush_primit.cpp, pinned 68ecbed6; see credits).
					for (int offset : {2, 5}) { target.textureMatrix[offset] -= std::floor(target.textureMatrix[offset]); }
				}
			} else {
				target.shiftX = source.shiftX; target.shiftY = source.shiftY; target.rotation = source.rotation;
				target.scaleX = source.scaleX; target.scaleY = source.scaleY;
				// NRC Texdef_Assign(..., false) retains Valve axes. See credits.
				if (options.mode != LevelSurfacePasteMode::RadiantValues) { target.uAxis = source.uAxis; target.vAxis = source.vAxis; }
				target.uOffset = source.uOffset; target.vOffset = source.vOffset;
				target.textureMatrix = source.textureMatrix;
				if (target.explicitTextureMatrix) { for (int i = 0; i < 3; ++i) { target.textureMatrix[i] *= u; target.textureMatrix[i + 3] *= v; } }
			}
			if (!options.mappingOnly) {
				target.textureName = source.textureName;
				// Native selection-wide value paste applies material/UVs to the
				// selection, and the copied flags only to the explicit hit faces.
				if (explicitSet.contains({LevelMaterialKind::BrushFace, it.key(), index})) {
					target.contentFlags = source.contentFlags; target.surfaceFlags = source.surfaceFlags; target.surfaceValue = source.surfaceValue;
				}
			}
			if (!valid(target, error, nativeProject)) { return false; }
			convertsClassic |= !before.explicitTextureAxes && !before.explicitTextureMatrix && target.explicitTextureAxes;
			if (settings(target) == settings(before)) { target = before; continue; }
			if (nativeProject && std::abs(mappingArea(target)) <= 1e-14) { ++result.m_edgeOnFaces; }
			target.textureParametersDirty = true;
			target.textureDirty |= target.textureName != before.textureName;
			++changed;
		}
		if (changed > 0) {
			after.textureParametersDirty = true; after.texturesDirty = true;
			result.m_before << *found; result.m_after << after; result.m_faceCount += changed;
		}
	}
	if (convertsClassic) {
		// q3map2 selects Quake/Valve syntax from the first face. Follow the
		// studio transform policy: one map-wide representation and undo step.
		// Reference: q3map2/map.cpp, ParseBrush, 68ecbed6; see docs/CREDITS.md.
		QHash<int, int> results;
		for (int i = 0; i < result.m_after.size(); ++i) { results.insert(result.m_after[i].id, i); }
		for (const auto& before : document.brushes) {
			if (cancelled && cancelled()) { return fail(error, Text::tr("Surface paste cancelled.")); }
			const int index = results.value(before.id, -1);
			auto after = index < 0 ? before : result.m_after[index];
			bool converted = false;
			for (int f = 0; f < after.faces.size(); ++f) {
				if (cancelled && cancelled()) { return fail(error, Text::tr("Surface paste cancelled.")); }
				auto& face = after.faces[f];
				if (face.explicitTextureMatrix) { return fail(error, Text::tr("Map-wide Valve 220 conversion cannot include brush-primitive matrices.")); }
				if (before.faces[f].explicitTextureAxes) { continue; }
				if (after.faces.size() > 128 || ++result.m_convertedFaces > 16384) {
					return fail(error, Text::tr("Map-wide conversion exceeds the surface limit: 16,384 faces and 128 faces per brush."));
				}
				if (!face.explicitTextureAxes) {
					const auto projection = levelTextureProjection(face);
					face.explicitTextureAxes = true;
					if (!setLevelTextureProjection(&face, projection, false, error) || !valid(face, error)) { return false; }
				}
				face.textureParametersDirty = true; converted = true;
			}
			if (!converted) { continue; }
			after.textureParametersDirty = true; after.primitiveKind = QStringLiteral("valve220");
			if (index < 0) { result.m_before << before; result.m_after << after; }
			else { result.m_after[index] = after; }
		}
		result.m_faceCount = 0;
		for (int b = 0; b < result.m_after.size(); ++b) {
			for (int f = 0; f < result.m_after[b].faces.size(); ++f) {
				result.m_faceCount += settings(result.m_before[b].faces[f]) != settings(result.m_after[b].faces[f]);
			}
		}
		if (result.m_faceCount > 16384) { return fail(error, Text::tr("Surface paste and map-wide conversion exceed 16,384 changed faces.")); }
	}
	for (const auto& patch : document.patches) {
		if (cancelled && cancelled()) { return fail(error, Text::tr("Surface paste cancelled.")); }
		if (!patchTargets.remove(patch.id)) { continue; }
		if (options.mappingOnly && !nativeProject) { continue; } // Value paste leaves patch UVs unchanged.
		auto after = patch;
		if (!options.mappingOnly) { after.textureName = levelPatchMaterialToken(source.textureName, patch.fixedSubdivisions); }
		if (nativeProject) {
			if (!validateLevelPatch(patch, error)) { return false; }
			// NRC Patch::ProjectTexture evaluates the copied world projection at
			// every control point, in destination image repeats (patch.cpp at
			// 68ecbed6, GPL-2.0-or-later behavior reference; see CREDITS.md).
			LevelMapBrushFace target; target.textureName = patch.textureName; target.explicitTextureMatrix = true;
			double u = 1, v = 1; if (!transferFactors(source, target, options, &u, &v, error)) { return false; }
			const auto projection = levelTextureProjection(source);
			for (int i = 0; i < after.controlPoints.size(); ++i) {
				if (cancelled && cancelled()) { return fail(error, Text::tr("Surface paste cancelled.")); }
				const auto uv = projection.at(after.controlPoints[i]); after.controlU[i] = uv.x() * u; after.controlV[i] = uv.y() * v;
			}
			if (!validateLevelPatch(after, error)) { return false; }
			if (after.controlU != patch.controlU || after.controlV != patch.controlV) {
				after.definitionDirty = after.geometryDirty = true;
				if (patch.startLine > 0) {
					after.sourceLines = document.textLines.mid(patch.startLine - 1, patch.endLine - patch.startLine + 1);
					after.sourceFirstLine = patch.startLine;
				}
			}
		}
		if (after.textureName == patch.textureName && after.controlU == patch.controlU && after.controlV == patch.controlV) { continue; }
		after.textureDirty |= after.textureName != patch.textureName;
		result.m_patchBefore << patch; result.m_patchAfter << after;
	}
	if (!patchTargets.isEmpty()) { return fail(error, Text::tr("A target patch does not exist.")); }
	if (result.faceCount() + result.patchCount() > 16384) { return fail(error, Text::tr("Surface transfer exceeds 16,384 changed surfaces.")); }
	result.m_ready = true; *plan = std::move(result); return true;
}

LevelSurfaceStroke::LevelSurfaceStroke(const LevelMapDocument& document, const LevelSurfaceClipboard& clipboard)
	: m_document(document), m_clipboard(clipboard)
{
	// Preview steps do not retain history. The final plan is bound to the
	// caller's original document, leaving its history/save point untouched
	// until commit applies the normal edit or no-op history policy.
	m_document.undoStack.clear(); m_document.redoStack.clear(); m_document.savedUndoDepth = 0;
	m_plan.m_revision = document.revision; m_plan.m_sourcePath = document.sourcePath;
	m_plan.m_sourceHash = document.sourceContentHash; m_plan.m_format = document.format;
	m_plan.m_selection = document.selection; m_plan.m_paste = true;
	m_accumulated = m_plan;
}

bool LevelSurfaceStroke::append(const QVector<LevelMaterialTarget>& targets, LevelSurfacePasteOptions options,
	QString* error, const std::function<bool()>& cancelled)
{
	if (error) { error->clear(); }
	if (m_steps >= 4096) { return fail(error, Text::tr("A surface stroke supports at most 4,096 hits. Release to finish this stroke.")); }
	if (cancelled && cancelled()) { return fail(error, Text::tr("Surface stroke cancelled.")); }
	// Native held-button ordering: selection on mouse-down only; later hits
	// use the current modifiers and the last wrapped face. Behavior reference:
	// NRC TexManipulator_ in radiant/selection.cpp, 68ecbed6 (GPL-2.0-or-later).
	// See docs/CREDITS.md. No input events or global undo grouping are required.
	if (m_steps > 0) { options.includeSelection = false; }
	if (m_clipboardFromDocument) { options.textureSize = options.materialSizes.value(materialKey(m_clipboard.material())); }
	if (options.mode == LevelSurfacePasteMode::Seamless && targets.size() != 1) {
		return fail(error, Text::tr("Each seamless stroke hit needs exactly one brush face."));
	}
	LevelSurfaceEditPlan step;
	if (!prepareLevelSurfaceTransfer(m_document, targets, m_clipboard, options, &step, error, cancelled)) { return false; }
	auto candidate = m_document;
	if (cancelled && cancelled()) { return fail(error, Text::tr("Surface stroke cancelled.")); }
	if (!commitLevelSurfaceEdit(&candidate, step, error)) { return false; }
	auto clipboard = m_clipboard;
	const bool wrapped = options.mode == LevelSurfacePasteMode::Seamless;
	if (wrapped && !copyLevelSurface(candidate, {targets.first().objectId, targets.first().faceIndex}, &clipboard, error)) { return false; }
	auto combined = m_accumulated;
	const auto merge = [](auto& before, auto& after, const auto& stepBefore, const auto& stepAfter) {
		QHash<int, int> indices;
		for (int i = 0; i < after.size(); ++i) { indices.insert(after[i].id, i); }
		for (int i = 0; i < stepAfter.size(); ++i) {
			const int index = indices.value(stepAfter[i].id, -1);
			if (index < 0) { before << stepBefore[i]; after << stepAfter[i]; }
			else { after[index] = stepAfter[i]; }
		}
	};
	merge(combined.m_before, combined.m_after, step.m_before, step.m_after);
	merge(combined.m_patchBefore, combined.m_patchAfter, step.m_patchBefore, step.m_patchAfter);
	combined.m_bindSelection |= step.m_bindSelection;
	const auto accumulated = combined; // Keep original bindings even after a net-zero revisit.
	combined.m_faceCount = combined.m_convertedFaces = combined.m_edgeOnFaces = 0;
	for (int b = static_cast<int>(combined.m_after.size()) - 1; b >= 0; --b) {
		if (cancelled && cancelled()) { return fail(error, Text::tr("Surface stroke cancelled.")); }
		const auto& before = combined.m_before[b]; const auto& after = combined.m_after[b];
		int changed = 0;
		for (int f = 0; f < before.faces.size(); ++f) {
			if (settings(before.faces[f]) == settings(after.faces[f])) { continue; }
			++changed;
			combined.m_convertedFaces += !before.faces[f].explicitTextureAxes && !before.faces[f].explicitTextureMatrix && after.faces[f].explicitTextureAxes;
			combined.m_edgeOnFaces += std::abs(mappingArea(after.faces[f])) <= 1e-14;
		}
		combined.m_faceCount += changed;
		if (changed == 0) { combined.m_before.removeAt(b); combined.m_after.removeAt(b); }
	}
	for (int p = static_cast<int>(combined.m_patchAfter.size()) - 1; p >= 0; --p) {
		const auto& before = combined.m_patchBefore[p]; const auto& after = combined.m_patchAfter[p];
		if (before.textureName == after.textureName && before.controlU == after.controlU && before.controlV == after.controlV) {
			combined.m_patchBefore.removeAt(p); combined.m_patchAfter.removeAt(p);
		}
	}
	if (combined.faceCount() + combined.patchCount() > 16384 || combined.patchCount() > 256) {
		return fail(error, Text::tr("A surface stroke supports at most 16,384 changed surfaces, including at most 256 patches."));
	}
	if (cancelled && cancelled()) { return fail(error, Text::tr("Surface stroke cancelled.")); }
	combined.m_bindSelection |= step.m_bindSelection; combined.m_ready = true;
	candidate.undoStack.clear(); candidate.redoStack.clear();
	m_document = std::move(candidate); m_plan = std::move(combined); m_clipboard = std::move(clipboard);
	m_accumulated = accumulated;
	m_clipboardFromDocument |= wrapped; ++m_steps; return true;
}

bool LevelSurfaceStroke::commit(LevelMapDocument* document, QString* error) const
{
	return commitLevelSurfaceEdit(document, m_plan, error);
}
} // namespace vibestudio
