#include "core/level_materials.h"
#include "core/doom_preview_materials.h"
#include "core/level_model_appearance.h"

#include "core/advanced_studio.h"
#include "core/idtech_image.h"
#include "core/map_assets.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>

namespace vibestudio
{
namespace
{
QString keyFor(QString path) { return path.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded(); }
QString unquote(QString value)
{
	value = value.trimmed();
	if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
		value = value.mid(1, value.size() - 2);
	}
	return value;
}
QStringList imageCandidates(const QString &name)
{
	QStringList paths{name};
	QString base = name;
	const QString extension = QFileInfo(name).suffix().toLower();
	const QStringList extensions{QStringLiteral("tga"), QStringLiteral("jpg"), QStringLiteral("png"), QStringLiteral("jpeg")};
	if (extensions.contains(extension)) {
		base.chop(extension.size() + 1);
	} else if (!extension.isEmpty()) {
		return paths;
	}
	for (const QString &suffix : extensions) {
		const QString path = base + QLatin1Char('.') + suffix;
		if (!paths.contains(path)) {
			paths << path;
		}
	}
	return paths;
}

// Caps even palette lookups made by the shared decoder. Ambiguous names cannot
// silently choose a different image from dependency review. Reads are complete:
// a capped prefix must never masquerade as a valid image or shader script.
class BoundedReader final : public PackageArchiveReader
{
  public:
	BoundedReader(const PackageArchiveReader &source, const LevelPreviewAssetOptions &limits, LevelPreviewAssets *report,
				  std::function<bool()> tick)
		: m_source(source), m_limits(limits), m_report(report), m_tick(std::move(tick)), m_entries(source.entries())
	{
		for (qsizetype i = 0; i < m_entries.size(); ++i) {
			if (m_entries[i].kind == PackageEntryKind::File) {
				files[keyFor(m_entries[i].virtualPath)].push_back(i);
			}
		}
	}
	PackageArchiveFormat format() const override { return m_source.format(); }
	QString sourcePath() const override { return m_source.sourcePath(); }
	bool isOpen() const override { return m_source.isOpen(); }
	QVector<PackageEntry> entries() const override { return m_entries; }
	bool readEntryBytes(const QString &path, QByteArray *out, QString *error, qint64 maxBytes = -1) const override
	{
		const auto indexes = files.value(keyFor(path));
		if (indexes.size() != 1) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMaterials", "The asset is missing or its name is ambiguous.");
			}
			return false;
		}
		return readEntryAt(indexes.first(), out, error, maxBytes);
	}
	bool readEntryAt(qsizetype index, QByteArray *out, QString *error, qint64 maxBytes = -1) const override
	{
		if (out) {
			out->clear();
		}
		if (!out || index < 0 || index >= m_entries.size() || !m_tick()) {
			return false;
		}
		const auto &entry = m_entries[index];
		const qint64 remaining = std::max<qint64>(0, m_limits.totalReadByteLimit - m_report->readBytes);
		qint64 cap = std::min(std::max<qint64>(0, m_limits.entryByteLimit), remaining);
		if (maxBytes >= 0) {
			cap = std::min(cap, maxBytes);
		}
		if (!entry.readable || !isSafePackageVirtualPath(entry.virtualPath)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMaterials", "The asset is unreadable or has an unsafe package path.");
			}
			return false;
		}
		if (entry.sizeBytes > static_cast<quint64>(cap)) {
			m_report->complete = false;
			if (error) {
				*error = QCoreApplication::translate("VibeStudioLevelMaterials", "The asset exceeds the preview read budget.");
			}
			return false;
		}
		m_report->readBytes += static_cast<qint64>(entry.sizeBytes);
		if (!m_source.readEntryAt(index, out, error, cap + 1) || out->size() != static_cast<qint64>(entry.sizeBytes) || !m_tick()) {
			out->clear();
			if (error && error->isEmpty()) {
				*error = QCoreApplication::translate("VibeStudioLevelMaterials",
													 "The asset changed, was cancelled, or could not be read completely.");
			}
			return false;
		}
		return true;
	}
	const PackageEntry &entry(qsizetype index) const { return m_entries[index]; }
	QMap<QString, QVector<qsizetype>> files;

  private:
	const PackageArchiveReader &m_source;
	const LevelPreviewAssetOptions &m_limits;
	LevelPreviewAssets *m_report;
	std::function<bool()> m_tick;
	QVector<PackageEntry> m_entries;
};
struct ShaderOwner {
	QString path;
	ShaderDefinition shader;
};
struct Request {
	QString key, name, modelPath;
	QImage embedded;
	bool authored = false;
};
} // namespace

int LevelPreviewAssets::readyCount() const
{
	return static_cast<int>(std::count_if(materials.cbegin(), materials.cend(), [](const auto &m) { return m.ready(); }));
}
int LevelPreviewAssets::problemCount() const
{
	return unavailableModels + static_cast<int>(std::count_if(materials.cbegin(), materials.cend(), [](const auto &m) {
			   return !m.ready() && m.status != QStringLiteral("builtin");
		   }));
}

namespace
{
LevelPreviewAssets resolvePreviewAssets(const LevelMapDocument &document, const ModelMesh *authoredMesh, const PackageArchiveReader &archive,
									   const LevelPreviewAssetOptions &options, LevelPreviewAssetProgress progress)
{
	LevelPreviewAssets report;
	report.sourcePath = archive.sourcePath();
	if (!archive.isOpen()) {
		report.warnings << QCoreApplication::translate(
			"VibeStudioLevelMaterials",
			"No readable package snapshot is open. Open an asset folder or package, and resolve any staging conflicts.");
	}
	int completed = 0, total = 0;
	const auto tick = [&] {
		if (report.cancelled || (progress && !progress(completed, total))) {
			report.cancelled = true;
			report.complete = false;
			return false;
		}
		return true;
	};
	if (!tick()) {
		return report;
	}
	BoundedReader reader(archive, options, &report, tick);
	if (document.format == LevelMapFormat::DoomWad) {
		resolveDoomPreviewAssets(document, reader, options, &report, [&] {
			completed = int(report.materials.size()); total = report.requestedMaterials;
			return tick();
		});
		return report;
	}
	const bool quake3 = document.engineFamily.compare(QStringLiteral("idTech3"), Qt::CaseInsensitive) == 0;
	QMap<QString, Request> requests;
	for (const auto &use : levelMapTextureUsage(document)) {
		if (!isMapTexturePlaceholder(use.name, document.format)) {
			requests.insert(keyFor(use.name), {keyFor(use.name), use.name, {}, {}});
		}
	}
	if (authoredMesh) {
		for (qsizetype s = 0; s < authoredMesh->surfaces.size(); ++s) {
			if (!tick()) { return report; }
			const auto &surface = authoredMesh->surfaces[s];
			Request request;
			request.key = modelPreviewSurfaceMaterialKey(static_cast<int>(s));
			// Editable metadata aggregates other surfaces into mesh.skinPaths;
			// an unassigned surface must not inherit a neighbour's material.
			request.name = surface.skinPaths.value(0);
			request.authored = true;
			// Only package-relative provenance supplies relative image candidates.
			request.modelPath = isSafePackageVirtualPath(authoredMesh->sourcePath)
				? authoredMesh->sourcePath : QString();
			if (!authoredMesh->embeddedSkins.isEmpty()) {
				request.embedded = authoredMesh->embeddedSkins.first().image;
			}
			requests.insert(request.key, std::move(request));
		}
	}
	QHash<QString, IdTechPaletteResolution> palettes;
	const auto paletteFor = [&](const QString &path, const QByteArray &bytes) {
		QString id = options.paletteId;
		if (id.isEmpty() || id == QStringLiteral("auto")) {
			id = path.endsWith(QStringLiteral(".mdl"), Qt::CaseInsensitive)
					 ? QStringLiteral("quake")
					 : defaultIdTechPaletteIdForImage(detectIdTechImageFormat(path, bytes), bytes.size());
		}
		if (!palettes.contains(id)) {
			palettes.insert(id, resolveIdTechPalette(reader, id));
		}
		return palettes.value(id);
	};
	// Placed models use the same read-only package snapshot, including generated
	// or replaced staging entries. Embedded skins stay attached to their surface.
	QMap<QString, LevelModelAppearance> modelPaths;
	QHash<QString, QStringList> modelSelectors;
	for (const auto &entity : document.entities) {
		const auto appearance = levelModelAppearance(document, entity);
		if (!appearance.modelPath.isEmpty() && !appearance.modelPath.startsWith(QLatin1Char('*'))) {
			modelPaths.insert(appearance.cacheKey, appearance);
			modelSelectors[appearance.cacheKey] << QStringLiteral("entity:%1").arg(entity.id);
		}
	}
	report.requestedModels = static_cast<int>(modelPaths.size());
	total = report.requestedModels;
	int attemptedModels = 0;
	int modelVertices = 0, modelTriangles = 0;
	QString decodedPath, decodeError, modelHash;
	ModelMesh decoded;
	ModelWorkControl control; control.cancelled = [&] { return !tick(); };
	for (auto it = modelPaths.cbegin(); it != modelPaths.cend(); ++it) {
		if (!tick()) {
			return report;
		}
		++completed;
		const auto& appearance = it.value();
		QString error = appearance.error;
		QJsonObject receipt{{QStringLiteral("key"), it.key()}, {QStringLiteral("modelPath"), appearance.modelPath},
			{QStringLiteral("frame"), appearance.frame}, {QStringLiteral("skin"), appearance.skin}, {QStringLiteral("skinPath"), appearance.skinPath}};
		if (++attemptedModels > std::max(0, options.modelLimit)) {
			report.complete = false;
			error = QCoreApplication::translate("VibeStudioLevelMaterials", "Model preview count limit reached.");
		} else if (error.isEmpty()) {
			if (decodedPath != keyFor(appearance.modelPath)) {
				decodedPath = keyFor(appearance.modelPath); decoded = {}; decodeError.clear(); modelHash.clear();
				QByteArray bytes;
				if (reader.readEntryBytes(appearance.modelPath, &bytes, &decodeError, 4 * 1024 * 1024)) {
					const auto palette = paletteFor(appearance.modelPath, bytes);
					modelHash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
					decoded = decodeModelMesh(appearance.modelPath, bytes, &palette.palette, control);
					if (!decoded.embeddedSkins.isEmpty() && palette.palette.generated) {
						report.warnings << QCoreApplication::translate("VibeStudioLevelMaterials", "%1 uses a generated skin palette; supply the game's palette for accurate colours.").arg(appearance.modelPath);
					}
				}
			}
			auto prepared = prepareLevelModelAppearance(decoded, appearance, reader, control);
			receipt = prepared.receipt;
			receipt.insert(QStringLiteral("modelSha256"), modelHash);
			error = decodeError.isEmpty() ? prepared.error : decodeError;
			auto model = std::move(prepared.mesh);
			if (error.isEmpty() && prepared.succeeded() && model.triangleCount <= 150000 - modelTriangles && model.vertexCount <= 450000 - modelVertices) {
				modelVertices += model.vertexCount;
				modelTriangles += model.triangleCount;
				if (receipt.value(QStringLiteral("omittedSurfaces")).toInt() > 0) {
					report.warnings << QCoreApplication::translate("VibeStudioLevelMaterials", "%1: compiler skin omitted %2 surface(s).")
						.arg(appearance.modelPath).arg(receipt.value(QStringLiteral("omittedSurfaces")).toInt());
				}
				for (const auto &warning : model.warnings) {
					report.warnings << appearance.modelPath + QStringLiteral(": ") + warning;
				}
				for (auto &surface : model.surfaces) {
					if (surface.frames.size() > 1) {
						surface.frames = {surface.frames.first()};
					}
					Request request;
					request.key = levelModelSurfaceMaterialKey(it.key(), surface.index);
					request.name = surface.skinPaths.value(0, model.skinPaths.value(0));
					request.modelPath = appearance.modelPath;
					if (!model.embeddedSkins.isEmpty()) {
						request.embedded = model.embeddedSkins.first().image;
					}
					if (!request.name.isEmpty() || !request.embedded.isNull()) {
						requests.insert(request.key, request);
					}
				}
				if (model.frames.size() > 1) {
					model.frames = {model.frames.first()};
				}
				model.frameCount = 1;
				model.embeddedSkins.clear();
				receipt.insert(QStringLiteral("selectors"), QJsonArray::fromStringList(modelSelectors.value(it.key())));
				report.modelAppearances.append(receipt);
				report.models.insert(it.key(), std::move(model));
				continue;
			}
			if (error.isEmpty()) { error = model.error.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMaterials",
																		"Model geometry is unsupported or exceeds the preview limit.")
										  : model.error; }
		}
		++report.unavailableModels;
		report.complete = false;
		receipt.insert(QStringLiteral("status"), QStringLiteral("unavailable")); receipt.insert(QStringLiteral("error"), error);
		receipt.insert(QStringLiteral("selectors"), QJsonArray::fromStringList(modelSelectors.value(it.key())));
		report.modelAppearances.append(receipt);
		report.warnings << QCoreApplication::translate("VibeStudioLevelMaterials", "%1: %2").arg(appearance.modelPath, error);
	}
	QMap<QString, QVector<ShaderOwner>> shaders;
	bool scriptsComplete = true;
	if (quake3 && !requests.isEmpty()) {
		int scriptCount = 0, shaderCount = 0;
		qint64 scriptBytes = 0;
		for (auto it = reader.files.cbegin(); it != reader.files.cend(); ++it) {
			if (!it.key().startsWith(QStringLiteral("scripts/")) || !it.key().endsWith(QStringLiteral(".shader"))) {
				continue;
			}
			if (!tick()) {
				return report;
			}
			const auto &entry = reader.entry(it.value().first());
			QByteArray bytes;
			QString error;
			if (++scriptCount > 2048 || entry.sizeBytes > 4 * 1024 * 1024 ||
				scriptBytes + static_cast<qint64>(entry.sizeBytes) > 16 * 1024 * 1024) {
				scriptsComplete = false;
				break;
			}
			scriptBytes += static_cast<qint64>(entry.sizeBytes);
			if (it.value().size() != 1 || !reader.readEntryAt(it.value().first(), &bytes, &error)) {
				scriptsComplete = false;
				continue;
			}
			const auto script = parseShaderScriptText(QString::fromUtf8(bytes), entry.virtualPath);
			if (!script.issues.isEmpty()) {
				scriptsComplete = false;
			}
			for (const auto &shader : script.shaders) {
				if (++shaderCount > 65536) {
					scriptsComplete = false;
					break;
				}
				shaders[keyFor(shader.name)].push_back({entry.virtualPath, shader});
			}
			if (shaderCount > 65536) {
				break;
			}
		}
	}
	if (!scriptsComplete) {
		report.complete = false;
		report.warnings << QCoreApplication::translate("VibeStudioLevelMaterials",
													   "Shader scripts are unreadable, ambiguous, malformed, or exceed the scan budget. "
													   "Material previews are withheld until shader lookup is reliable.");
	}
	report.requestedMaterials = static_cast<int>(requests.size());
	total = completed + report.requestedMaterials;
	QHash<QString, LevelPreviewMaterial> decodedImages;
	for (const Request &request : std::as_const(requests)) {
		if (!tick()) {
			return report;
		}
		++completed;
		if (report.materials.size() >= std::max(0, options.materialLimit)) {
			report.complete = false;
			report.warnings << QCoreApplication::translate("VibeStudioLevelMaterials",
														   "Material preview count limit reached; remaining materials use flat shading.");
			break;
		}
		LevelPreviewMaterial material;
		material.key = request.key;
		material.name = request.name;
		material.status = QStringLiteral("missing");
		const auto resolve = [&] {
			if (!request.embedded.isNull()) {
				material.image = request.embedded;
				material.sourceSize = material.image.size();
				material.status = QStringLiteral("embedded");
				return;
			}
			if (request.authored && request.name.isEmpty()) {
				material.note = QCoreApplication::translate("VibeStudioModelDocument", "No material is assigned to this surface.");
				return;
			}
			if (!isSafePackageVirtualPath(request.name)) {
				material.status = QStringLiteral("unreadable");
				return;
			}
			if (!scriptsComplete) {
				material.status = QStringLiteral("unreadable");
				material.note = report.warnings.last();
				return;
			}
			const bool modelMaterial = request.authored || !request.modelPath.isEmpty();
			QStringList candidates = !modelMaterial
									 ? mapTextureCandidatePaths(request.name, document.format, document.engineFamily)
									 : imageCandidates(request.name);
			if (authoredMesh && (authoredMesh->format == ModelMeshFormat::Quake2Md2 || authoredMesh->format == ModelMeshFormat::Quake3Md3)) {
				candidates = modelSkinCandidatePaths(request.name);
			}
			if (!request.modelPath.isEmpty() && !request.name.contains(QLatin1Char('/'))) {
				const auto relative = candidates;
				for (const auto &path : relative) {
					candidates << packageVirtualPathParent(request.modelPath) + QLatin1Char('/') + path;
				}
			}
			const QStringList names = !modelMaterial
										  ? mapTextureMaterialCandidates(request.name, document.format, document.engineFamily)
										  : QStringList{request.name};
			for (const auto &name : names) {
				const auto owners = shaders.value(keyFor(name));
				if (owners.isEmpty()) {
					continue;
				}
				if (owners.size() != 1) {
					material.status = QStringLiteral("ambiguous");
					material.note = QCoreApplication::translate("VibeStudioLevelMaterials", "Multiple declarations use this shader name.");
					return;
				}
				material.shaderPath = owners.first().path;
				QString imageName;
				const auto &shader = owners.first().shader;
				for (const auto &directive : shader.directives) {
					static const QRegularExpression editorImage(QStringLiteral("^qer_editorimage\\s+(.+)$"),
																QRegularExpression::CaseInsensitiveOption);
					const auto match = editorImage.match(directive);
					if (match.hasMatch()) {
						imageName = unquote(match.captured(1));
						material.status = QStringLiteral("editor-image");
						break;
					}
				}
				if (imageName.isEmpty()) {
					for (const auto &stage : shader.stages) {
						for (const auto &path : stage.textureReferences) {
							if (!path.startsWith(QLatin1Char('$'))) {
								imageName = path;
								break;
							}
						}
						if (!imageName.isEmpty()) {
							material.status = QStringLiteral("stage-image");
							break;
						}
					}
				}
				material.note = QCoreApplication::translate("VibeStudioLevelMaterials",
															"Static shader image only; lighting, blending, animation, deformation and "
															"texture-coordinate effects are not simulated.");
				if (imageName.isEmpty()) {
					material.status = QStringLiteral("unsupported");
					return;
				}
				candidates = imageCandidates(imageName);
				break;
			}
			for (const auto &candidate : candidates) {
				const auto indexes = reader.files.value(keyFor(candidate));
				if (indexes.isEmpty()) {
					continue;
				}
				if (indexes.size() != 1) {
					material.status = QStringLiteral("ambiguous");
					return;
				}
				const auto &entry = reader.entry(indexes.first());
				material.imagePath = entry.virtualPath;
				material.sourceLayer = entry.layerId.isEmpty() ? entry.sourceArchiveId : entry.layerId;
				if (decodedImages.contains(keyFor(candidate))) {
					const auto cached = decodedImages.value(keyFor(candidate));
					material.image = cached.image;
					material.sourceSize = cached.sourceSize;
					material.warnings = cached.warnings;
					if (material.status == QStringLiteral("missing")) {
						material.status = QStringLiteral("image");
					}
					return;
				}
				QByteArray bytes;
				QString error;
				if (!reader.readEntryAt(indexes.first(), &bytes, &error)) {
					material.status = QStringLiteral("unreadable");
					material.note = error;
					return;
				}
				// Qt formats need a header check before allocating a decoded image.
				QBuffer buffer(&bytes);
				buffer.open(QIODevice::ReadOnly);
				QImageReader probe(&buffer);
				if (probe.canRead() &&
					(!probe.size().isValid() || static_cast<qint64>(probe.size().width()) * probe.size().height() > 4096 * 4096)) {
					material.status = QStringLiteral("budget");
					report.complete = false;
					material.note =
						QCoreApplication::translate("VibeStudioLevelMaterials", "Image exceeds the 16-megapixel preview decode limit.");
					return;
				}
				const auto palette = paletteFor(entry.virtualPath, bytes);
				const auto decoded = decodeIdTechImage(entry.virtualPath, bytes, palette.palette);
				if (!decoded.decoded || decoded.image.isNull()) {
					material.status = QStringLiteral("unsupported");
					material.note = decoded.error;
					return;
				}
				material.image = decoded.image;
				material.sourceSize = decoded.image.size();
				material.warnings = decoded.warnings;
				if (decoded.paletted && palette.palette.generated) {
					material.warnings << QCoreApplication::translate(
						"VibeStudioLevelMaterials", "A generated palette is used; supply the game's palette for accurate colours.");
				}
				if (material.status == QStringLiteral("missing")) {
					material.status = QStringLiteral("image");
				}
				return;
			}
			material.status =
				isEngineHandledMapTexture(request.name, document.format, document.engineFamily) && material.shaderPath.isEmpty()
					? QStringLiteral("builtin")
					: QStringLiteral("missing");
		};
		resolve();
		if (material.ready()) {
			const int dimension = std::clamp(options.previewDimension, 1, 2048);
			if (material.image.width() > dimension || material.image.height() > dimension) {
				material.image = material.image.scaled(dimension, dimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
			}
			material.image = material.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
			const bool cached = !material.imagePath.isEmpty() && decodedImages.contains(keyFor(material.imagePath));
			if (!cached && material.image.sizeInBytes() > std::max<qint64>(0, options.imageByteLimit - report.imageBytes)) {
				material.image = {};
				material.status = QStringLiteral("budget");
				report.complete = false;
				material.note = QCoreApplication::translate("VibeStudioLevelMaterials", "Material image memory budget reached.");
			} else if (!cached) {
				report.imageBytes += material.image.sizeInBytes();
				if (!material.imagePath.isEmpty()) {
					decodedImages.insert(keyFor(material.imagePath), material);
				}
			}
		}
		report.materials.push_back(std::move(material));
	}
	tick();
	return report;
}
} // namespace

LevelPreviewAssets resolveLevelPreviewAssets(const LevelMapDocument &document, const PackageArchiveReader &archive,
	const LevelPreviewAssetOptions &options, LevelPreviewAssetProgress progress)
{
	return resolvePreviewAssets(document, nullptr, archive, options, std::move(progress));
}

QString modelPreviewSurfaceMaterialKey(int surface) { return QStringLiteral("authored-surface:%1").arg(surface); }

LevelPreviewAssets resolveModelPreviewAssets(const ModelMesh &mesh, const PackageArchiveReader &archive,
	const LevelPreviewAssetOptions &options, LevelPreviewAssetProgress progress)
{
	LevelMapDocument context;
	const bool quake2 = mesh.format == ModelMeshFormat::Quake2Md2 || mesh.sourcePath.endsWith(QStringLiteral(".md2"), Qt::CaseInsensitive);
	const bool quake1 = mesh.format == ModelMeshFormat::QuakeMdl || mesh.sourcePath.endsWith(QStringLiteral(".mdl"), Qt::CaseInsensitive);
	context.format = quake1 || quake2 ? LevelMapFormat::QuakeMap : LevelMapFormat::Quake3Map;
	context.engineFamily = quake1 ? QStringLiteral("idTech1") : quake2 ? QStringLiteral("idTech2") : QStringLiteral("idTech3");
	return resolvePreviewAssets(context, &mesh, archive, options, std::move(progress));
}

QHash<QString, QSize> levelPreviewTextureSizes(const LevelPreviewAssets &assets)
{
	QHash<QString, QSize> sizes;
	for (const auto &material : assets.materials) {
		if (material.ready()) {
			sizes.insert(material.key, material.sourceSize);
		}
	}
	return sizes;
}
QHash<int, QImage> levelPreviewSurfaceImages(const ModelMesh &mesh, const LevelPreviewAssets &assets)
{
	QHash<QString, QImage> byName;
	for (const auto &material : assets.materials) {
		if (material.ready()) {
			byName.insert(material.key, material.image);
		}
	}
	QHash<int, QImage> images;
	for (const auto &surface : mesh.surfaces) {
		const auto found = byName.constFind(keyFor(surface.name));
		if (found != byName.cend()) {
			images.insert(surface.index, *found);
		}
	}
	return images;
}
QJsonObject levelPreviewAssetsJson(const LevelPreviewAssets &assets)
{
	QJsonArray materials;
	for (const auto &material : assets.materials) {
		QJsonArray inputs;
		for (const auto& input : material.inputs) {
			inputs.append(QJsonObject {{QStringLiteral("entryIndex"), qint64(input.entryIndex)}, {QStringLiteral("sourceOrdinal"), input.sourceOrdinal},
				{QStringLiteral("path"), input.path}, {QStringLiteral("namespace"), input.namespaceId}, {QStringLiteral("layer"), input.layer},
				{QStringLiteral("role"), input.role}, {QStringLiteral("bytes"), qint64(input.sizeBytes)}});
		}
		materials.append(QJsonObject{{QStringLiteral("key"), material.key},
									 {QStringLiteral("name"), material.name},
									 {QStringLiteral("status"), material.status},
									 {QStringLiteral("imagePath"), material.imagePath},
									 {QStringLiteral("shaderPath"), material.shaderPath},
									 {QStringLiteral("sourceLayer"), material.sourceLayer},
									 {QStringLiteral("width"), material.sourceSize.width()},
									 {QStringLiteral("height"), material.sourceSize.height()},
									 {QStringLiteral("previewWidth"), material.image.width()},
									 {QStringLiteral("previewHeight"), material.image.height()},
									 {QStringLiteral("note"), material.note},
									 {QStringLiteral("warnings"), QJsonArray::fromStringList(material.warnings)}, {QStringLiteral("inputs"), inputs}});
	}
	return {{QStringLiteral("sourcePath"), assets.sourcePath},
			{QStringLiteral("complete"), assets.complete},
			{QStringLiteral("cancelled"), assets.cancelled},
			{QStringLiteral("requestedMaterials"), assets.requestedMaterials},
			{QStringLiteral("readyMaterials"), assets.readyCount()},
			{QStringLiteral("problems"), assets.problemCount()},
			{QStringLiteral("requestedModels"), assets.requestedModels},
			{QStringLiteral("readyModels"), static_cast<int>(assets.models.size())},
			{QStringLiteral("modelAppearances"), assets.modelAppearances},
			{QStringLiteral("readBytes"), assets.readBytes},
			{QStringLiteral("imageBytes"), assets.imageBytes},
			{QStringLiteral("materials"), materials},
			{QStringLiteral("warnings"), QJsonArray::fromStringList(assets.warnings)}};
}
QString levelPreviewAssetsText(const LevelPreviewAssets &assets)
{
	QStringList lines{QCoreApplication::translate("VibeStudioLevelMaterials", "Materials: %1/%2 ready; models: %3/%4 ready.")
						  .arg(assets.readyCount())
						  .arg(assets.requestedMaterials)
						  .arg(assets.models.size())
						  .arg(assets.requestedModels)};
	for (const auto &material : assets.materials) {
		lines << QStringLiteral("%1 — %2%3")
					 .arg(material.name, material.status,
						  material.imagePath.isEmpty() ? QString()
													   : QStringLiteral(" [%1] %2 × %3")
															 .arg(material.imagePath)
															 .arg(material.sourceSize.width())
															 .arg(material.sourceSize.height()));
		if (!material.note.isEmpty()) {
			lines << material.note;
		}
		lines += material.warnings;
		for (const auto& input : material.inputs) {
			lines << QCoreApplication::translate("VibeStudioLevelMaterials", "  %1: %2 · namespace %3 · directory occurrence %4")
				.arg(input.role, input.path, input.namespaceId).arg(input.sourceOrdinal);
		}
	}
	for (const auto& value : assets.modelAppearances) {
		const auto appearance = value.toObject();
		const auto skin = appearance.value(QStringLiteral("skin")).toString();
		lines << QCoreApplication::translate("VibeStudioLevelMaterials", "Model %1 · frame %2 · skin %3 · %4")
			.arg(appearance.value(QStringLiteral("modelPath")).toString()).arg(appearance.value(QStringLiteral("frame")).toInt())
			.arg(skin.isEmpty() ? QCoreApplication::translate("VibeStudioLevelMaterials", "Default") : skin, appearance.value(QStringLiteral("status")).toString());
		for (const auto& selector : appearance.value(QStringLiteral("selectors")).toArray()) { lines << QStringLiteral("  ") + selector.toString(); }
		for (const auto& item : appearance.value(QStringLiteral("inputs")).toArray()) {
			const auto input = item.toObject();
			lines << QStringLiteral("  %1: %2 [%3] SHA-256 %4").arg(input.value(QStringLiteral("role")).toString(),
				input.value(QStringLiteral("path")).toString(), input.value(QStringLiteral("layer")).toString(), input.value(QStringLiteral("sha256")).toString());
		}
		for (const auto& item : appearance.value(QStringLiteral("surfaces")).toArray()) {
			const auto surface = item.toObject();
			lines << QStringLiteral("  %1: %2 → %3").arg(surface.value(QStringLiteral("name")).toString(), surface.value(QStringLiteral("sourceMaterial")).toString(),
				surface.value(QStringLiteral("retained")).toBool() ? surface.value(QStringLiteral("material")).toString() : QCoreApplication::translate("VibeStudioLevelMaterials", "omitted by compiler skin"));
		}
		if (!appearance.value(QStringLiteral("error")).toString().isEmpty()) { lines << appearance.value(QStringLiteral("error")).toString(); }
	}
	lines += assets.warnings;
	return lines.join(QLatin1Char('\n'));
}
} // namespace vibestudio
