#include "core/model_design.h"
#include "core/model_file_io.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}
bool finite(const ModelVec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
QJsonArray vectorJson(const ModelVec3& v) { return {v.x, v.y, v.z}; }
bool readVector(const QJsonValue& value, ModelVec3* result)
{
	const QJsonArray a = value.toArray();
	if (a.size() != 3 || !a[0].isDouble() || !a[1].isDouble() || !a[2].isDouble()) {
		return false;
	}
	*result = {static_cast<float>(a[0].toDouble()), static_cast<float>(a[1].toDouble()), static_cast<float>(a[2].toDouble())};
	return finite(*result);
}
bool readUv(const QJsonValue& value, ModelTexCoord* result)
{
	const auto a = value.toArray();
	if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble()) {
		return false;
	}
	*result = {static_cast<float>(a[0].toDouble()), static_cast<float>(a[1].toDouble())};
	return std::isfinite(result->u) && std::isfinite(result->v);
}


} // namespace

QStringList validateModelDesign(const ModelDesign& design)
{
	QStringList errors;
	const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9_-]{1,48}$"));
	const QRegularExpression material(QStringLiteral("^[A-Za-z0-9_./-]{1,63}$"));
	if (!identifier.match(design.name).hasMatch()) {
		errors << QCoreApplication::translate("VibeStudioModelDesign",
		                                      "Design names need 1 to 48 ASCII letters, digits, underscores, or hyphens.");
	}
	if (design.parts.isEmpty() || design.parts.size() > 32) {
		errors << QCoreApplication::translate("VibeStudioModelDesign", "A design needs between 1 and 32 parts.");
		return errors;
	}
	QSet<QString> names;
	for (const auto& part : design.parts) {
		if (!identifier.match(part.name).hasMatch() || names.contains(part.name.toCaseFolded())) {
			errors << QCoreApplication::translate("VibeStudioModelDesign", "Part names must be unique identifiers of up to 48 characters.");
		}
		names.insert(part.name.toCaseFolded());
		if (part.primitive != QStringLiteral("box") && part.primitive != QStringLiteral("cylinder") &&
		    part.primitive != QStringLiteral("plane")) {
			errors << QCoreApplication::translate("VibeStudioModelDesign", "Unknown primitive: %1").arg(part.primitive);
		}
		if (!finite(part.size) || part.size.x < 1.0f / 64 || part.size.y < 1.0f / 64 || part.size.z < 1.0f / 64 || part.size.x > 1000 ||
		    part.size.y > 1000 || part.size.z > 1000 || !finite(part.origin) || std::abs(part.origin.x) > 10000 ||
		    std::abs(part.origin.y) > 10000 || std::abs(part.origin.z) > 10000) {
			errors << QCoreApplication::translate(
			    "VibeStudioModelDesign", "Part dimensions must be between 1/64 and 1000; positions must be finite and within ±10000.");
		}
		for (double angle : {part.roll, part.pitch, part.yaw, part.uvRotation}) {
			if (!std::isfinite(angle) || std::abs(angle) > 36000) {
				errors << QCoreApplication::translate("VibeStudioModelDesign",
				                                      "Part and UV rotations must be finite and within ±36000 degrees.");
				break;
			}
		}
		for (float scale : {part.uvScale.u, part.uvScale.v}) {
			if (!std::isfinite(scale) || std::abs(scale) < 1.0f / 64 || std::abs(scale) > 64) {
				errors << QCoreApplication::translate("VibeStudioModelDesign",
				                                      "UV scale magnitudes must be between 1/64 and 64; use negative values to mirror.");
				break;
			}
		}
		if (!std::isfinite(part.uvOffset.u) || !std::isfinite(part.uvOffset.v) || std::abs(part.uvOffset.u) > 1024 ||
		    std::abs(part.uvOffset.v) > 1024) {
			errors << QCoreApplication::translate("VibeStudioModelDesign", "UV offsets must be finite and within ±1024.");
		}
		if (part.segments < 3 || part.segments > 64) {
			errors << QCoreApplication::translate("VibeStudioModelDesign", "Cylinder segments must be between 3 and 64.");
		}
		if (!material.match(part.material).hasMatch() || !isSafePackageVirtualPath(part.material)) {
			errors << QCoreApplication::translate("VibeStudioModelDesign",
			                                      "Materials must be safe package paths of up to 63 ASCII characters.");
		}
	}
	return errors;
}

QJsonObject modelDesignJson(const ModelDesign& design)
{
	QJsonArray parts;
	for (const auto& p : design.parts) {
		parts << QJsonObject{{QStringLiteral("name"), p.name},
		                     {QStringLiteral("primitive"), p.primitive},
		                     {QStringLiteral("size"), vectorJson(p.size)},
		                     {QStringLiteral("origin"), vectorJson(p.origin)},
		                     {QStringLiteral("roll"), p.roll},
		                     {QStringLiteral("pitch"), p.pitch},
		                     {QStringLiteral("yaw"), p.yaw},
		                     {QStringLiteral("uvScale"), QJsonArray{p.uvScale.u, p.uvScale.v}},
		                     {QStringLiteral("uvOffset"), QJsonArray{p.uvOffset.u, p.uvOffset.v}},
		                     {QStringLiteral("uvRotation"), p.uvRotation},
		                     {QStringLiteral("segments"), p.segments},
		                     {QStringLiteral("material"), p.material}};
	}
	return {{QStringLiteral("schemaVersion"), 2}, {QStringLiteral("name"), design.name}, {QStringLiteral("parts"), parts}};
}

bool parseModelDesign(const QByteArray& json, ModelDesign* design, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!design || json.size() > 1024 * 1024) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Design input is unavailable or exceeds 1 MiB."));
	}
	QJsonParseError parseError;
	const auto doc = QJsonDocument::fromJson(json, &parseError);
	const auto object = doc.object();
	const auto version = object.value(QStringLiteral("schemaVersion"));
	if (parseError.error != QJsonParseError::NoError || !doc.isObject() || (version != QJsonValue(1) && version != QJsonValue(2)) ||
	    !object.value(QStringLiteral("name")).isString() || !object.value(QStringLiteral("parts")).isArray()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign",
		                                               "Expected a model design object with schemaVersion 1 or 2, name, and parts."));
	}
	ModelDesign result;
	result.name = object.value(QStringLiteral("name")).toString();
	const auto parts = object.value(QStringLiteral("parts")).toArray();
	if (parts.size() > 32) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "A design needs between 1 and 32 parts."));
	}
	for (const auto& value : parts) {
		const auto p = value.toObject();
		ModelDesignPart part;
		part.name = p.value(QStringLiteral("name")).toString();
		part.primitive = p.value(QStringLiteral("primitive")).toString();
		part.material = p.value(QStringLiteral("material")).toString();
		if (!readVector(p.value(QStringLiteral("size")), &part.size) || !readVector(p.value(QStringLiteral("origin")), &part.origin) ||
		    !p.value(QStringLiteral("yaw")).isDouble() || !p.value(QStringLiteral("segments")).isDouble()) {
			return fail(error, QCoreApplication::translate("VibeStudioModelDesign",
			                                               "Each part needs size/origin vectors, numeric yaw, and integer segments."));
		}
		part.yaw = p.value(QStringLiteral("yaw")).toDouble();
		if (version == QJsonValue(2)) {
			if (!p.value(QStringLiteral("roll")).isDouble() || !p.value(QStringLiteral("pitch")).isDouble() ||
			    !p.value(QStringLiteral("uvRotation")).isDouble() || !readUv(p.value(QStringLiteral("uvScale")), &part.uvScale) ||
			    !readUv(p.value(QStringLiteral("uvOffset")), &part.uvOffset)) {
				return fail(error, QCoreApplication::translate(
				                       "VibeStudioModelDesign",
				                       "Version 2 parts need numeric roll, pitch, and uvRotation, plus uvScale and uvOffset pairs."));
			}
			part.roll = p.value(QStringLiteral("roll")).toDouble();
			part.pitch = p.value(QStringLiteral("pitch")).toDouble();
			part.uvRotation = p.value(QStringLiteral("uvRotation")).toDouble();
		}
		const double segments = p.value(QStringLiteral("segments")).toDouble();
		if (segments < 3 || segments > 64 || std::floor(segments) != segments) {
			return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Cylinder segments must be between 3 and 64."));
		}
		part.segments = static_cast<int>(segments);
		result.parts << part;
	}
	const auto errors = validateModelDesign(result);
	if (!errors.isEmpty()) {
		return fail(error, errors.join(QLatin1Char('\n')));
	}
	*design = result;
	return true;
}

ModelMesh buildModelDesignMesh(const ModelDesign& design)
{
	ModelMesh mesh;
	const auto errors = validateModelDesign(design);
	if (!errors.isEmpty()) {
		mesh.error = errors.join(QLatin1Char('\n'));
		return mesh;
	}
	mesh.sourcePath = design.name;
	mesh.format = ModelMeshFormat::Quake3Md3;
	mesh.formatId = QStringLiteral("md3");
	mesh.formatName = QCoreApplication::translate("VibeStudioModelDesign", "Static model design");
	mesh.version = 15;
	mesh.mins = {100000, 100000, 100000};
	mesh.maxs = {-100000, -100000, -100000};
	for (const auto& part : design.parts) {
		ModelSurface surface;
		surface.index = mesh.surfaces.size();
		surface.name = part.name;
		surface.skinPaths << part.material;
		ModelFrameGeometry geometry;
		const double a = part.yaw * std::numbers::pi / 180;
		const float c = std::cos(a), s = std::sin(a);
		const double roll = part.roll * std::numbers::pi / 180, pitch = part.pitch * std::numbers::pi / 180;
		const float cr = std::cos(roll), sr = std::sin(roll), cp = std::cos(pitch), sp = std::sin(pitch);
		const auto rotate = [&](ModelVec3 v) {
			v = {v.x, cr * v.y - sr * v.z, sr * v.y + cr * v.z};
			v = {cp * v.x + sp * v.z, v.y, -sp * v.x + cp * v.z};
			return ModelVec3{c * v.x - s * v.y, s * v.x + c * v.y, v.z};
		};
		const double uvAngle = part.uvRotation * std::numbers::pi / 180;
		const float cu = std::cos(uvAngle), su = std::sin(uvAngle);
		const auto vertex = [&](ModelVec3 p, ModelVec3 n, ModelTexCoord uv) {
			p = {p.x * part.size.x * 0.5f, p.y * part.size.y * 0.5f, p.z * part.size.z * 0.5f};
			p = rotate(p);
			p = {p.x + part.origin.x, p.y + part.origin.y, p.z + part.origin.z};
			n = {n.x / part.size.x, n.y / part.size.y, n.z / part.size.z};
			const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
			n = rotate({n.x / length, n.y / length, n.z / length});
			uv = {uv.u * part.uvScale.u, uv.v * part.uvScale.v};
			uv = {cu * uv.u - su * uv.v + part.uvOffset.u, su * uv.u + cu * uv.v + part.uvOffset.v};
			mesh.mins = {std::min(mesh.mins.x, p.x), std::min(mesh.mins.y, p.y), std::min(mesh.mins.z, p.z)};
			mesh.maxs = {std::max(mesh.maxs.x, p.x), std::max(mesh.maxs.y, p.y), std::max(mesh.maxs.z, p.z)};
			geometry.positions << p;
			geometry.normals << n;
			surface.texCoords << uv;
		};
		const auto quad = [&](ModelVec3 p, ModelVec3 q, ModelVec3 r, ModelVec3 t, ModelVec3 n) {
			const int base = geometry.positions.size();
			vertex(p, n, {0, 1});
			vertex(q, n, {1, 1});
			vertex(r, n, {1, 0});
			vertex(t, n, {0, 0});
			surface.triangles << ModelTriangle{base, base + 1, base + 2} << ModelTriangle{base, base + 2, base + 3};
		};
		if (part.primitive == QStringLiteral("box")) {
			quad({1, -1, -1}, {1, 1, -1}, {1, 1, 1}, {1, -1, 1}, {1, 0, 0});
			quad({-1, 1, -1}, {-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 0, 0});
			quad({1, 1, -1}, {-1, 1, -1}, {-1, 1, 1}, {1, 1, 1}, {0, 1, 0});
			quad({-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}, {0, -1, 0});
			quad({-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}, {0, 0, 1});
			quad({-1, 1, -1}, {1, 1, -1}, {1, -1, -1}, {-1, -1, -1}, {0, 0, -1});
		} else if (part.primitive == QStringLiteral("plane")) {
			quad({-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}, {0, 0, 1});
		} else {
			for (int i = 0; i < part.segments; ++i) {
				const double first = i * 2 * std::numbers::pi / part.segments, second = (i + 1) * 2 * std::numbers::pi / part.segments;
				const float x = std::cos(first), y = std::sin(first), u = std::cos(second), v = std::sin(second);
				int base = geometry.positions.size();
				const float left = float(i) / part.segments, right = float(i + 1) / part.segments;
				vertex({x, y, -1}, {x, y, 0}, {left, 1});
				vertex({u, v, -1}, {u, v, 0}, {right, 1});
				vertex({u, v, 1}, {u, v, 0}, {right, 0});
				vertex({x, y, 1}, {x, y, 0}, {left, 0});
				surface.triangles << ModelTriangle{base, base + 1, base + 2} << ModelTriangle{base, base + 2, base + 3};
				for (float z : {-1.0f, 1.0f}) {
					base = geometry.positions.size();
					vertex({0, 0, z}, {0, 0, z}, {0.5f, 0.5f});
					vertex({x, y, z}, {0, 0, z}, {(x + 1) / 2, (1 - y) / 2});
					vertex({u, v, z}, {0, 0, z}, {(u + 1) / 2, (1 - v) / 2});
					surface.triangles << (z > 0 ? ModelTriangle{base, base + 1, base + 2} : ModelTriangle{base, base + 2, base + 1});
				}
			}
		}
		surface.vertexCount = geometry.positions.size();
		surface.frames << geometry;
		mesh.vertexCount += surface.vertexCount;
		mesh.triangleCount += surface.triangles.size();
		mesh.surfaces << surface;
		if (!mesh.skinPaths.contains(part.material)) {
			mesh.skinPaths << part.material;
		}
	}
	ModelFrameInfo frame;
	frame.index = 0;
	frame.name = QStringLiteral("design");
	frame.mins = mesh.mins;
	frame.maxs = mesh.maxs;
	frame.radius = mesh.boundingRadius();
	mesh.frames << frame;
	mesh.frameCount = 1;
	mesh.surfaceCount = mesh.surfaces.size();
	mesh.skinCount = mesh.skinPaths.size();
	mesh.geometryAvailable = true;
	return mesh;
}

QByteArray exportModelDesign(const ModelDesign& design, const QString& format, QString* error)
{
	if (error) {
		error->clear();
	}
	const auto mesh = buildModelDesignMesh(design);
	if (!mesh.error.isEmpty()) {
		fail(error, mesh.error);
		return {};
	}
	if (format == QStringLiteral("md3") || format == QStringLiteral("md2")) {
		return exportEditableModel(mesh, format, 0, error);
	}
	if (format == QStringLiteral("obj")) {
		// Keep material assignments per part; the existing single-frame exporter
		// preserves group names, coordinates, UV orientation, and vertex normals.
		QString obj = exportModelFrameObj(mesh, 0);
		for (const auto& p : design.parts) {
			obj.replace(QStringLiteral("g %1\n").arg(p.name), QStringLiteral("g %1\nusemtl %2\n").arg(p.name, p.material));
		}
		return obj.toUtf8();
	}
	fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Model design output must be MD2, MD3, or OBJ."));
	return {};
}

bool saveModelDesignBytes(const QString& path, const QByteArray& bytes, bool overwrite, QString* error, const QString& protectedSource,
                          const ModelWorkControl& control)
{
	if (error) {
		error->clear();
	}
	const auto target = inspectModelWriteTarget(path, control);
	if (path.trimmed().isEmpty() || bytes.isEmpty()) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Choose an output path and a valid design first."));
	}
	if (!target.isValid()) { return fail(error, target.error); }
	if (modelPathsReferToSameFile(path, protectedSource)) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "An exported model cannot overwrite its design source."));
	}
	if (target.existed && !overwrite) {
		return fail(error, QCoreApplication::translate(
		                       "VibeStudioModelDesign",
		                       "The output exists or is a symbolic link; choose a new path or explicitly enable overwrite."));
	}
	return writeModelFile(target, bytes, error, control);
}

bool loadModelDesign(const QString& path, ModelDesign* design, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Unable to read the design, or it exceeds 1 MiB."));
	}
	const auto bytes = file.read(1024 * 1024 + 1);
	if (file.error() != QFileDevice::NoError) {
		return fail(error, file.errorString());
	}
	return parseModelDesign(bytes, design, error);
}

bool placeLevelModel(LevelMapDocument* document, const QString& virtualPath, const LevelMapVec3& origin, int* entityId, QString* error)
{
	if (!document || document->format != LevelMapFormat::Quake3Map) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Model placement requires an open Quake III map."));
	}
	const auto normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe() || !normalized.normalizedPath.endsWith(QStringLiteral(".md3"), Qt::CaseInsensitive) ||
	    normalized.normalizedPath.toUtf8().size() >= 64) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Choose a safe MD3 package path shorter than 64 bytes."));
	}
	if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Model placement coordinates must be finite."));
	}
	return addLevelMapEntity(document, QStringLiteral("misc_model"), {origin.x, origin.y, origin.z, true},
	                         {{QStringLiteral("model"), normalized.normalizedPath, 0}}, entityId, error);
}

bool stageModelDesign(const ModelDesign& design, const QString& virtualPath, PackageStagingModel* staging, LevelMapDocument* placeInMap,
                      const LevelMapVec3& origin, bool replaceExisting, QString* error)
{
	const auto problems = validateModelDesign(design);
	if (!problems.isEmpty()) { return fail(error, problems.join(QLatin1Char('\n'))); }
	return stageModelMesh(buildModelDesignMesh(design), virtualPath, staging, placeInMap, origin, replaceExisting, error);
}

bool stageModelMesh(const ModelMesh& mesh, const QString& virtualPath, PackageStagingModel* staging, LevelMapDocument* placeInMap,
                    const LevelMapVec3& origin, bool replaceExisting, QString* error)
{
	const auto bytes = exportEditableModel(mesh, QFileInfo(virtualPath).suffix().toLower(), 0, error);
	return !bytes.isEmpty() && stageModelExport(bytes, virtualPath, staging, placeInMap, origin, replaceExisting, error);
}

bool stageModelExport(const QByteArray& bytes, const QString& virtualPath, PackageStagingModel* staging, LevelMapDocument* placeInMap,
                      const LevelMapVec3& origin, bool replaceExisting, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!staging || !staging->isLoaded() || staging->sourceFormat() == PackageArchiveFormat::Wad) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Open a folder, PAK, ZIP, or PK3 before staging a model."));
	}
	const auto normalized = normalizePackageVirtualPath(virtualPath, false);
	const QString kind = QFileInfo(normalized.normalizedPath).suffix().toLower();
	if (!normalized.isSafe() || (kind != QStringLiteral("mdl") && kind != QStringLiteral("md3") && kind != QStringLiteral("md2")) ||
	    normalized.normalizedPath.toUtf8().size() >= 64) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Choose a safe MDL, MD2 or MD3 package path shorter than 64 bytes."));
	}
	if (placeInMap && kind != QStringLiteral("md3")) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "MDL and MD2 can be staged in the package. Automatic level placement currently requires MD3 and a Quake III map."));
	}
	if (bytes.size() > modelDocumentMaxSourceBytes || bytes.size() < 68 ||
	    !bytes.startsWith(kind == QStringLiteral("mdl") ? QByteArrayLiteral("IDPO") :
	        kind == QStringLiteral("md2") ? QByteArrayLiteral("IDP2") : QByteArrayLiteral("IDP3"))) {
		return fail(error, QCoreApplication::translate("VibeStudioModelDesign", "Prepare an MDL, MD2 or MD3 export matching the package path before staging."));
	}
	auto staged = *staging;
	if (!staged.addBytes(bytes, normalized.normalizedPath, error,
	                     replaceExisting ? PackageStageConflictResolution::ReplaceExisting : PackageStageConflictResolution::Block) ||
	    !staged.summary().canSave) {
		return fail(error, error && !error->isEmpty()
		                       ? *error
		                       : QCoreApplication::translate("VibeStudioModelDesign",
		                                                     "Resolve package staging conflicts before handing off a model."));
	}
	if (placeInMap && !placeLevelModel(placeInMap, normalized.normalizedPath, origin, nullptr, error)) {
		return false;
	}
	*staging = std::move(staged);
	return true;
}

} // namespace vibestudio
