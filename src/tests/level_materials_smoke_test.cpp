#include "core/level_materials.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include "core/package_staging.h"
#include "tests/level_material_test_helpers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &error = {})
{
	if (!value) {
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return value;
}
QByteArray indexedTexture(bool wal)
{
	// Original fixture using the layouts already covered by idtech_image tests.
	QByteArray bytes(wal ? 100 : 40, '\0');
	bytes.replace(0, 5, "stone");
	const int dimensions = wal ? 32 : 16;
	const auto u32 = [&](int at, quint32 value) { qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data() + at)); };
	u32(dimensions, 32);
	u32(dimensions + 4, 16);
	for (int mip = 0; mip < 4; ++mip) {
		u32(dimensions + 8 + mip * 4, static_cast<quint32>(bytes.size()));
		bytes.append(QByteArray((32 >> mip) * (16 >> mip), static_cast<char>(7)));
	}
	return bytes;
}
// Deliberately repeats an identity; a preview must not silently pick an image.
class DuplicateReader final : public PackageArchiveReader
{
  public:
	explicit DuplicateReader(const PackageArchiveReader &reader) : source(reader) {}
	PackageArchiveFormat format() const override { return source.format(); }
	QString sourcePath() const override { return source.sourcePath(); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override
	{
		auto list = source.entries();
		for (const auto &entry : source.entries()) {
			if (entry.virtualPath.endsWith(QStringLiteral("grid.png"))) {
				list << entry;
			}
		}
		return list;
	}
	bool readEntryBytes(const QString &name, QByteArray *bytes, QString *error, qint64 cap) const override
	{
		return source.readEntryBytes(name, bytes, error, cap);
	}

  private:
	const PackageArchiveReader &source;
};
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	QString error;
	bool ok = temp.isValid();
	LevelMapDocument document;
	ok &= expect(tests::createMaterialFixture(temp.path(), &document, &error), "synthetic material fixture", error);
	PackageArchive archive;
	ok &= expect(archive.load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error), "open assets", error);
	LevelPreviewAssetOptions limits;
	limits.previewDimension = 32;
	const auto assets = resolveLevelPreviewAssets(document, archive, limits);
	for (const bool wal : {false, true}) {
		const QString root = QDir(temp.path()).filePath(wal ? QStringLiteral("native-q2") : QStringLiteral("native-q1"));
		QByteArray palette;
		for (int i = 0; i < 256; ++i) {
			palette.append(static_cast<char>(i));
			palette.append(static_cast<char>(255 - i));
			palette.append(static_cast<char>(i / 2));
		}
		ok &= tests::putMaterialFile(QDir(root).filePath(QStringLiteral("gfx/palette.lmp")), palette);
		ok &= tests::putMaterialFile(QDir(root).filePath(wal ? QStringLiteral("textures/stone.wal") : QStringLiteral("textures/stone.mip")),
									 indexedTexture(wal));
		LevelMapCreateRequest create;
		create.game = wal ? QStringLiteral("quake2") : QStringLiteral("quake");
		create.starterRoom = false;
		LevelMapDocument native;
		PackageArchive images;
		LevelPreviewAssetOptions nativeOptions;
		nativeOptions.paletteId = QStringLiteral("quake");
		ok &= createLevelMap(create, &native, &error) &&
			  addLevelMapBoxBrush(&native, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("stone"));
		ok &= images.load(root, &error);
		const auto report = resolveLevelPreviewAssets(native, images, nativeOptions);
		const auto *stone = tests::materialNamed(report, QStringLiteral("stone"));
		ok &= expect(stone && stone->ready() && stone->sourceSize == QSize(32, 16) && stone->warnings.isEmpty() &&
						 stone->image.pixelColor(0, 0) == QColor(7, 248, 3),
					 "Quake miptex and Quake II WAL use package palette and dimensions", levelPreviewAssetsText(report));
	}
	ok &=
		expect(assets.complete && !assets.cancelled && assets.readyCount() == 4 && assets.problemCount() == 0 && assets.models.size() == 1,
			   "images, shader and placed model resolve", levelPreviewAssetsText(assets));
	const auto *grid = tests::materialNamed(assets, QStringLiteral("studio/grid"));
	const auto *shader = tests::materialNamed(assets, QStringLiteral("studio/shader"));
	const auto *animated = tests::materialNamed(assets, QStringLiteral("studio/animated"));
	ok &= expect(grid && grid->sourceSize == QSize(128, 64) && grid->image.size() == QSize(32, 16),
				 "retain original dimensions after downsampling");
	ok &= expect(shader && shader->status == QStringLiteral("editor-image") && shader->imagePath.endsWith(QStringLiteral("editor.png")) &&
					 !shader->note.isEmpty(),
				 "editor image takes priority over stage");
	ok &= expect(animated && animated->status == QStringLiteral("stage-image") && animated->imagePath.endsWith(QStringLiteral("stage.png")),
				 "first animation frame skips lightmap");
	LevelMapPreviewMeshOptions options;
	options.modelMeshes = assets.models;
	options.textureSizes = levelPreviewTextureSizes(assets);
	const auto preview = buildLevelMapPreviewMesh(document, options);
	{
		auto normalized = document;
		for (auto &entity : normalized.entities) {
			for (auto &property : entity.properties) {
				if (property.key == QStringLiteral("model")) {
					property.value = QStringLiteral("MODELS\\studio\\prop.md3");
				}
			}
		}
		ok &= expect(buildLevelMapPreviewMesh(normalized, options).modelInstances == 1,
					 "mesh and asset resolver normalize model paths consistently");
	}
	ok &= expect(preview.modelInstances == 1 && preview.patches == 1 && preview.owners.size() == preview.triangles &&
					 preview.ownerFaces.size() == preview.triangles,
				 "camera geometry retains object and face ownership");
	ok &= expect(levelPreviewSurfaceImages(preview.mesh, assets).size() == 4, "one real skin per surface including model");
	// Check classic, explicit Valve axes and primitive matrices at solved points,
	// not only control-plane points, including non-square source dimensions.
	for (int dialect = 0; dialect < 3; ++dialect) {
		auto map = document;
		map.brushes = {document.brushes.first()};
		map.patches.clear();
		map.entities.clear();
		for (auto &face : map.brushes[0].faces) {
			face.shiftX = 11;
			face.shiftY = -7;
			face.rotation = 23;
			face.scaleX = 0.75;
			face.scaleY = -1.25;
			if (dialect == 1) {
				face.explicitTextureAxes = true;
				face.uAxis = {1, 0.2, 0.4, true};
				face.vAxis = {0.3, -1, 0.5, true};
				face.uOffset = 19;
				face.vOffset = -5;
			}
			if (dialect == 2) {
				face.explicitTextureMatrix = true;
				face.textureMatrix = {0.015, 0.004, 0.25, -0.006, 0.02, -0.1};
			}
		}
		const auto mesh = buildLevelMapPreviewMesh(map, options);
		int flattened = 0;
		for (const auto &surface : mesh.mesh.surfaces) {
			for (const auto &triangle : surface.triangles) {
				const auto projection = levelTextureProjection(map.brushes.first().faces[mesh.ownerFaces[flattened++]]);
				for (int vertex : {triangle.a, triangle.b, triangle.c}) {
					const auto position = surface.frames.first().positions[vertex];
					auto uv = projection.at({position.x, position.y, position.z, true});
					if (!projection.normalizedCoordinates) {
						uv = {uv.x() / 128, uv.y() / 64};
					}
					const auto actual = surface.texCoords[vertex];
					ok &= expect(std::abs(actual.u - uv.x()) < 1e-5 && std::abs(actual.v - uv.y()) < 1e-5,
								 "brush UV uses source texels or primitive repeats");
				}
			}
		}
	}
	// Patch and model UVs are already in repeats, never divided by image size.
	for (const auto &surface : preview.mesh.surfaces) {
		if (surface.name == QStringLiteral("studio/animated")) {
			bool reachesOne = false;
			for (const auto &uv : surface.texCoords) {
				reachesOne |= std::abs(uv.u - 1) < 1e-5;
			}
			ok &= expect(reachesOne, "patch UV repeats preserved");
		}
		if (surface.name.contains(QStringLiteral(":surface:"))) {
			bool nonzero = false;
			for (const auto &uv : surface.texCoords) {
				nonzero |= uv.u != 0 || uv.v != 0;
			}
			ok &= expect(nonzero, "placed model UVs retained");
		}
	}
	PackageStagingModel staging;
	ok &= staging.loadBaseArchive(archive, &error);
	const PackageStagingArchive before(staging);
	ok &= staging.addBytes(tests::materialImage(64, 128, true), QStringLiteral("textures/studio/grid.png"), &error,
						   PackageStageConflictResolution::ReplaceExisting);
	const PackageStagingArchive after(staging);
	const auto oldAssets = resolveLevelPreviewAssets(document, before);
	const auto newAssets = resolveLevelPreviewAssets(document, after);
	ok &= expect(tests::materialNamed(oldAssets, QStringLiteral("studio/grid"))->sourceSize == QSize(128, 64) &&
					 tests::materialNamed(newAssets, QStringLiteral("studio/grid"))->sourceSize == QSize(64, 128),
				 "immutable staged replacement changes dimensions");
	PackageWriteRequest write;
	write.destinationPath = QDir(temp.path()).filePath(QStringLiteral("fixture.pk3"));
	ok &= expect(staging.writeArchive(write).succeeded(), "save staged PK3");
	PackageArchive packed;
	ok &= packed.load(write.destinationPath, &error);
	const auto packedAssets = resolveLevelPreviewAssets(document, packed);
	ok &= expect(packedAssets.complete && packedAssets.readyCount() == 4 &&
					 tests::materialNamed(packedAssets, QStringLiteral("studio/grid"))->sourceSize == QSize(64, 128),
				 "PK3 and staging preview parity");
	const auto duplicate = resolveLevelPreviewAssets(document, DuplicateReader(archive));
	ok &= expect(tests::materialNamed(duplicate, QStringLiteral("studio/grid"))->status == QStringLiteral("ambiguous"),
				 "duplicate image names are refused");
	{
		PackageStagingModel duplicateShaders;
		ok &= duplicateShaders.loadBaseArchive(archive, &error);
		ok &= duplicateShaders.addBytes("textures/studio/shader\n{\n{ map textures/studio/grid.png }\n}\n",
										QStringLiteral("scripts/duplicate.shader"), &error);
		const auto report = resolveLevelPreviewAssets(document, PackageStagingArchive(duplicateShaders));
		ok &= expect(tests::materialNamed(report, QStringLiteral("studio/shader"))->status == QStringLiteral("ambiguous") &&
						 report.problemCount() > 0,
					 "duplicate shader declarations cannot choose an arbitrary owner");
	}
	{
		PackageStagingModel missingImage;
		ok &= missingImage.loadBaseArchive(archive, &error);
		ok &= missingImage.deleteEntry(QStringLiteral("textures/studio/editor.png"), &error);
		const auto report = resolveLevelPreviewAssets(document, PackageStagingArchive(missingImage));
		ok &= expect(tests::materialNamed(report, QStringLiteral("studio/shader"))->status == QStringLiteral("missing"),
					 "declared editor image does not silently fall back to a different stage");
		ok &= missingImage.addBytes("textures/studio/broken {", QStringLiteral("scripts/broken.shader"), &error);
		const auto malformed = resolveLevelPreviewAssets(document, PackageStagingArchive(missingImage));
		ok &= expect(!malformed.complete && malformed.readyCount() == 0, "incomplete shader catalog is explicit");
	}
	limits.materialLimit = 1;
	const auto limited = resolveLevelPreviewAssets(document, archive, limits);
	ok &= expect(!limited.complete && limited.materials.size() == 1 && limited.requestedMaterials == 4, "material count budget visible");
	limits = {};
	limits.totalReadByteLimit = 4;
	const auto unread = resolveLevelPreviewAssets(document, archive, limits);
	ok &= expect(!unread.complete && unread.readBytes <= 4 && unread.readyCount() == 0, "aggregate read budget");
	limits = {};
	limits.imageByteLimit = 1;
	const auto noImages = resolveLevelPreviewAssets(document, archive, limits);
	ok &= expect(!noImages.complete && noImages.readyCount() == 0 && noImages.imageBytes == 0, "image memory budget");
	int ticks = 0;
	const auto cancelled = resolveLevelPreviewAssets(document, archive, {}, [&](int, int) { return ++ticks < 3; });
	ok &= expect(cancelled.cancelled && !cancelled.complete, "asset cancellation");
	options.isCancelled = [] { return true; };
	ok &= expect(buildLevelMapPreviewMesh(document, options).cancelled, "geometry cancellation");
	auto doom = document;
	doom.format = LevelMapFormat::DoomWad;
	const auto unsupported = resolveLevelPreviewAssets(doom, archive);
	ok &= expect(unsupported.readyCount() == 0, "Doom composite textures not misrepresented");
	// Source identity conflicts must not show a fresh file against old metadata.
	ok &=
		tests::putMaterialFile(QDir(temp.path()).filePath(QStringLiteral("assets/textures/studio/grid.png")), tests::materialImage(16, 16));
	const auto changed = resolveLevelPreviewAssets(document, archive);
	ok &= expect(tests::materialNamed(changed, QStringLiteral("studio/grid"))->status == QStringLiteral("unreadable"),
				 "changed source rejected");
	if (argc > 1) {
		LevelDocumentSaveRequest save;
		save.path = QDir(temp.path()).filePath(QStringLiteral("fixture.map"));
		ok &= expect(writeLevelDocument(document, save).succeeded(), "save CLI map");
		QProcess cli;
		cli.start(QString::fromLocal8Bit(argv[1]),
				  {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("materials"), save.path, QStringLiteral("--package"),
				   write.destinationPath, QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--json")});
		ok &= expect(cli.waitForFinished(45000) && cli.exitCode() == 0, "material CLI succeeds",
					 QString::fromUtf8(cli.readAllStandardError()));
		const auto json = QJsonDocument::fromJson(cli.readAllStandardOutput()).object().value(QStringLiteral("materials")).toObject();
		ok &= expect(json.value(QStringLiteral("readyMaterials")).toInt() == 4 &&
						 json.value(QStringLiteral("materials")).toArray().size() == 4,
					 "CLI returns shared material evidence");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
