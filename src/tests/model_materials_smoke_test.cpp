#include "app/model_material_worker.h"
#include "core/model_document.h"
#include "core/package_staging.h"
#include "tests/level_material_test_helpers.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include <atomic>
#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message, const QString &detail = {})
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
bool until(const std::function<bool()> &ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 15000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return ready();
}
ModelMesh fixtureMesh()
{
	ModelDesign design;
	for (const auto &name : {QStringLiteral("textures/studio/grid.png"), QStringLiteral("textures/studio/shader"),
							 QStringLiteral("textures/studio/animated"), QStringLiteral("relative.png")})
	{
		ModelDesignPart part;
		part.name = QStringLiteral("part%1").arg(design.parts.size());
		part.material = name;
		design.parts << part;
	}
	auto mesh = buildModelDesignMesh(design);
	mesh.sourcePath = QStringLiteral("models/studio/prop.md3");
	return mesh;
}
QByteArray indexedImage()
{
	QByteArray bytes(40, '\0');
	const auto word = [&](int at, quint32 value) { qToLittleEndian(value, reinterpret_cast<uchar *>(bytes.data() + at)); };
	word(16, 16);
	word(20, 8);
	for (int mip = 0; mip < 4; ++mip)
	{
		word(24 + 4 * mip, static_cast<quint32>(bytes.size()));
		bytes.append(QByteArray((16 >> mip) * (8 >> mip), char(7)));
	}
	return bytes;
}
QByteArray paletteBytes(bool blue)
{
	QByteArray bytes(768, '\0');
	bytes[21] = char(blue ? 20 : 200);
	bytes[22] = char(10);
	bytes[23] = char(blue ? 200 : 20);
	return bytes;
}
// Deliberately blocks one bounded read while the UI continues and newer requests
// arrive. The worker must discard its stale result after the read returns.
class DelayedReader final : public PackageArchiveReader
{
  public:
	mutable std::atomic_bool entered = false, release = false, uiRead = false;
	mutable std::atomic_int reads = 0;
	QByteArray bytes = tests::materialImage(32, 32, true);
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Folder; }
	QString sourcePath() const override { return QStringLiteral("delayed-fixture"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override
	{
		PackageEntry entry;
		entry.virtualPath = QStringLiteral("textures/studio/grid.png");
		entry.kind = PackageEntryKind::File;
		entry.sizeBytes = bytes.size();
		entry.readable = true;
		return {entry};
	}
	bool readEntryBytes(const QString &, QByteArray *out, QString *, qint64) const override
	{
		uiRead = QThread::currentThread() == QCoreApplication::instance()->thread();
		++reads;
		entered = true;
		QElapsedTimer timer;
		timer.start();
		while (!release && timer.elapsed() < 5000)
		{
			QThread::msleep(1);
		}
		*out = bytes;
		return true;
	}
};
class DuplicateReader final : public PackageArchiveReader
{
  public:
	explicit DuplicateReader(const PackageArchiveReader &source) : source(source) {}
	PackageArchiveFormat format() const override { return source.format(); }
	QString sourcePath() const override { return source.sourcePath(); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override
	{
		auto entries = source.entries();
		for (const auto &entry : source.entries())
		{
			if (entry.virtualPath == QStringLiteral("textures/studio/grid.png"))
			{
				entries << entry;
			}
		}
		return entries;
	}
	bool readEntryBytes(const QString &path, QByteArray *out, QString *error, qint64 limit) const override
	{
		return source.readEntryBytes(path, out, error, limit);
	}
	const PackageArchiveReader &source;
};
const LevelPreviewMaterial *surfaceMaterial(const LevelPreviewAssets &assets, int surface = 0)
{
	return tests::materialNamed(assets, modelPreviewSurfaceMaterialKey(surface));
}
} // namespace

int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temp(QDir(root).filePath(QStringLiteral("mesh-materials-XXXXXX")));
	if (!temp.isValid())
	{
		return EXIT_FAILURE;
	}
	QString error;
	LevelMapDocument unused;
	bool ok = tests::createMaterialFixture(temp.path(), &unused, &error);
	ok &= tests::putMaterialFile(QDir(temp.path()).filePath(QStringLiteral("assets/textures/studio/indexed.mip")), indexedImage());
	ok &= tests::putMaterialFile(QDir(temp.path()).filePath(QStringLiteral("assets/gfx/palette.lmp")), paletteBytes(false));
	ok &= tests::putMaterialFile(QDir(temp.path()).filePath(QStringLiteral("assets/models/studio/relative.png")),
								 tests::materialImage(24, 48));
	auto archive = std::make_shared<PackageArchive>();
	ok &= expect(archive->load(QDir(temp.path()).filePath(QStringLiteral("assets")), &error), "open original asset fixture", error);
	auto mesh = fixtureMesh();
	if (!expect(mesh.surfaces.size() == 4, "four valid fixture surfaces"))
	{
		return EXIT_FAILURE;
	}
	const auto report = resolveModelPreviewAssets(mesh, *archive);
	ok &= expect(report.complete && report.readyCount() == 4 && report.problemCount() == 0 && report.requestedModels == 0,
				 "all authored surfaces resolve without decoding the model again", levelPreviewAssetsText(report));
	const auto *shader = surfaceMaterial(report, 1), *animated = surfaceMaterial(report, 2), *relative = surfaceMaterial(report, 3);
	ok &= expect(shader && shader->status == QStringLiteral("editor-image") && animated &&
					 animated->status == QStringLiteral("stage-image") && relative && relative->sourceSize == QSize(24, 48),
				 "shader image precedence and model-relative path");
	auto loose = mesh;
	loose.sourcePath = QDir(temp.path()).filePath(QStringLiteral("prop.md3"));
	ok &= expect(resolveModelPreviewAssets(loose, *archive).readyCount() == 3,
				 "loose filesystem provenance cannot become a package-relative search path");
	auto empty = mesh;
	empty.skinPaths.clear();
	empty.surfaces[0].skinPaths.clear();
	updateEditableModelMetadata(&empty);
	const auto missing = resolveModelPreviewAssets(empty, *archive);
	ok &= expect(missing.problemCount() == 1 && surfaceMaterial(missing) && !surfaceMaterial(missing)->note.isEmpty(),
				 "unassigned surface is explained");
	empty.embeddedSkins.resize(1);
	empty.embeddedSkins[0].image = QImage(16, 16, QImage::Format_RGB32);
	empty.embeddedSkins[0].image.fill(Qt::cyan);
	const auto embedded = resolveModelPreviewAssets(empty, PackageArchive());
	ok &= expect(embedded.readyCount() == 4 && surfaceMaterial(embedded)->status == QStringLiteral("embedded"),
				 "embedded skins need no package");
	DuplicateReader ambiguous(*archive);
	const auto duplicates = resolveModelPreviewAssets(mesh, ambiguous);
	ok &= expect(surfaceMaterial(duplicates) && surfaceMaterial(duplicates)->status == QStringLiteral("ambiguous"),
				 "ambiguous names never guess an image");
	LevelPreviewAssetOptions limits;
	limits.materialLimit = 2;
	const auto limited = resolveModelPreviewAssets(mesh, *archive, limits);
	ok &= expect(!limited.complete && limited.materials.size() == 2 && limited.requestedMaterials == 4,
				 "material count bound retains requested count");
	limits = {};
	limits.imageByteLimit = 1;
	const auto memory = resolveModelPreviewAssets(mesh, *archive, limits);
	ok &= expect(!memory.complete && memory.readyCount() == 0 && memory.imageBytes == 0, "decoded image budget fails closed");
	limits = {};
	limits.totalReadByteLimit = 1;
	const auto reads = resolveModelPreviewAssets(mesh, *archive, limits);
	ok &= expect(!reads.complete && reads.readyCount() == 0 && reads.readBytes <= 1, "read budget includes shader and image reads");
	const auto cancelled = resolveModelPreviewAssets(mesh, *archive, {}, [](int, int) { return false; });
	ok &= expect(cancelled.cancelled && !cancelled.complete && cancelled.readBytes == 0, "cancellation precedes source reads");

	PackageStagingModel staging;
	ok &= expect(staging.loadBaseArchive(*archive, &error), "load staging", error);
	const auto original = std::make_shared<PackageArchive>(packagePlannedArchive(staging));
	ok &= expect(staging.addBytes(tests::materialImage(128, 64, true), QStringLiteral("textures/studio/grid.png"), &error,
								  PackageStageConflictResolution::ReplaceExisting),
				 "stage image replacement", error);
	const auto replacement = std::make_shared<PackageArchive>(packagePlannedArchive(staging));
	ok &= expect(staging.deleteEntry(QStringLiteral("textures/studio/grid.png"), &error), "stage deletion", error);
	const auto deleted = resolveModelPreviewAssets(mesh, PackageStagingArchive(staging));
	ok &= expect(deleted.readyCount() == 3 && surfaceMaterial(deleted)->status == QStringLiteral("missing"),
				 "deletion does not reveal base texture");
	ok &= expect(staging.undo(), "undo texture deletion");
	const auto restored = resolveModelPreviewAssets(mesh, PackageStagingArchive(staging));
	const auto old = resolveModelPreviewAssets(mesh, *original);
	const auto replaced = resolveModelPreviewAssets(mesh, *replacement);
	ok &= expect(surfaceMaterial(restored)->image.pixelColor(10, 2) == QColor("#d65c7c") &&
					 surfaceMaterial(replaced)->image.pixelColor(10, 2) == QColor("#d65c7c") &&
					 surfaceMaterial(old)->image.pixelColor(10, 2) == QColor("#54c6da"),
				 "immutable snapshots preserve staged replacement, deletion and undo");
	{
		auto indexed = mesh;
		indexed.surfaces[0].skinPaths = {QStringLiteral("textures/studio/indexed.mip")};
		LevelPreviewAssetOptions paletteOptions;
		paletteOptions.paletteId = QStringLiteral("quake");
		const auto before = resolveModelPreviewAssets(indexed, *original, paletteOptions);
		ok &= staging.addBytes(paletteBytes(true), QStringLiteral("gfx/palette.lmp"), &error,
							   PackageStageConflictResolution::ReplaceExisting);
		const auto after = resolveModelPreviewAssets(indexed, PackageStagingArchive(staging), paletteOptions);
		ok &= expect(surfaceMaterial(before) && surfaceMaterial(after) && surfaceMaterial(before)->ready() &&
						 surfaceMaterial(after)->ready() && surfaceMaterial(before)->image.pixelColor(0, 0) == QColor(200, 10, 20) &&
						 surfaceMaterial(after)->image.pixelColor(0, 0) == QColor(20, 10, 200),
					 "indexed model image uses palette bytes from its immutable staged snapshot");
	}
	for (const auto mode : {PackageFileImportMode::Snapshot, PackageFileImportMode::VerifyOnly})
	{
		PackageStagingModel external;
		const auto replacementPath = QDir(temp.path()).filePath(QStringLiteral("replacement.png"));
		ok &= tests::putMaterialFile(replacementPath, tests::materialImage(32, 32, true));
		ok &= external.loadBaseArchive(*archive, &error) &&
			  external.replaceFile(QStringLiteral("textures/studio/grid.png"), replacementPath, &error, {}, mode);
		const auto pending = packagePlannedArchive(external);
		ok &= tests::putMaterialFile(replacementPath, QByteArray("externally changed"));
		const auto changed = resolveModelPreviewAssets(mesh, pending);
		if (mode == PackageFileImportMode::Snapshot)
		{
			ok &= expect(surfaceMaterial(changed) && surfaceMaterial(changed)->ready() && changed.readyCount() == 4 &&
							 surfaceMaterial(changed)->image.pixelColor(10, 2) == QColor("#d65c7c"),
						 "planned browser retains imported image bytes after the original file changes");
		}
		else
		{
			ok &= expect(surfaceMaterial(changed) && !surfaceMaterial(changed)->ready() &&
							 surfaceMaterial(changed)->status == QStringLiteral("unreadable") && changed.readyCount() == 3,
						 "read-only verification rejects changed inputs without showing the base image");
		}
	}

	{
		ModelMaterialWorker worker;
		int starts = 0, completions = 0, heartbeats = 0;
		ModelMaterialResult result;
		worker.started = [&] { ++starts; };
		worker.completed = [&](const auto &value)
		{
			result = value;
			++completions;
		};
		QTimer heartbeat;
		QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; });
		heartbeat.start(1);
		auto delayed = std::make_shared<DelayedReader>();
		worker.request(mesh, {delayed, QStringLiteral("slow"), {}});
		ok &= expect(until([&] { return delayed->entered.load(); }) && heartbeats > 1 && !delayed->uiRead,
					 "image IO runs off the responsive UI thread");
		worker.request(mesh, {replacement, QStringLiteral("intermediate"), {}});
		worker.request(mesh, {original, QStringLiteral("latest"), {}});
		delayed->release = true;
		ok &= expect(until([&] { return !worker.busy(); }) && starts == 3 && completions == 1 && result.error.isEmpty() &&
						 surfaceMaterial(result.assets) && surfaceMaterial(result.assets)->image.pixelColor(10, 2) == QColor("#54c6da"),
					 "one pending request wins and stale work cannot publish");
		auto geometry = mesh;
		geometry.surfaces[0].frames[0].positions[0].z += 1;
		worker.request(geometry, {original, QStringLiteral("latest"), {}});
		ok &= expect(!worker.busy() && starts == 3, "geometry-only edits retain the material result");
		worker.request(mesh, {original, QStringLiteral("latest"), QStringLiteral("quake")});
		ok &= expect(until([&] { return !worker.busy(); }) && completions == 2, "palette choice invalidates the material request");
		worker.request(mesh, {original, QStringLiteral("cancelled"), {}});
		worker.cancel();
		worker.request(mesh, {original, QStringLiteral("cancelled"), {}});
		ok &= expect(!worker.busy() && result.assets.cancelled && completions == 3, "Cancel survives ordinary refresh of the same binding");
		worker.reset();
		worker.request(mesh, {original, QStringLiteral("cancelled"), {}});
		ok &= expect(until([&] { return !worker.busy(); }) && completions == 4 && !result.assets.cancelled,
					 "explicit reload retries a cancelled binding");
		delayed = std::make_shared<DelayedReader>();
		worker.request(mesh, {delayed, QStringLiteral("active-cancel"), {}});
		ok &= expect(until([&] { return delayed->entered.load(); }), "active cancellation reaches bounded read");
		worker.cancel();
		delayed->release = true;
		ok &= expect(until([&] { return !worker.busy(); }) && completions == 5 && result.assets.cancelled,
					 "active cancellation suppresses completion after current read finishes");
	}
	{
		auto delayed = std::make_shared<DelayedReader>();
		auto worker = std::make_unique<ModelMaterialWorker>();
		int published = 0;
		worker->completed = [&](const auto &) { ++published; };
		worker->request(mesh, {delayed, QStringLiteral("closing"), {}});
		ok &= expect(until([&] { return delayed->entered.load(); }), "destruction fixture enters the read");
		delayed->release = true;
		worker.reset();
		app.processEvents();
		ok &= expect(published == 0, "destruction joins active work without late callbacks");
	}
	if (argc > 1)
	{
		const auto executable = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
		const auto source = QDir(temp.path()).filePath(QStringLiteral("prop.mesh.json"));
		ok &= tests::putMaterialFile(source, QJsonDocument(editableModelJson(mesh, &error)).toJson());
		const auto invoke = [&](const QStringList &arguments, int expected)
		{
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(executable,
						  QStringList{QStringLiteral("--cli"), QStringLiteral("model"), QStringLiteral("materials")} + arguments +
							  QStringList{QStringLiteral("--json")});
			const bool finished = process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			ok &= expect(finished && process.exitCode() == expected, "CLI material diagnostic exit status",
						 QString::fromUtf8(bytes + process.readAllStandardError()));
			return QJsonDocument::fromJson(bytes).object();
		};
		const auto json = invoke({source, QStringLiteral("--package"), archive->sourcePath()}, 0);
		ok &= expect(json.value(QStringLiteral("materials")).toObject().value(QStringLiteral("readyMaterials")).toInt() == 4,
					 "CLI and editor share material resolution");
		invoke({source}, 2);
		empty.embeddedSkins.clear();
		ok &= tests::putMaterialFile(source, QJsonDocument(editableModelJson(empty, &error)).toJson());
		invoke({source, QStringLiteral("--package"), archive->sourcePath()}, 4);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
