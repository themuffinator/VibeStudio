#include "app/level_preview_worker.h"
#include "app/map_viewport.h"
#include "tests/level_geometry_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop; QTimer::singleShot(10, &loop, &QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
bool counts(const MapViewport& view, int solved, int reused)
{
	const auto stats = view.geometryCacheStatistics();
	return expect(stats.solved == solved && stats.reused == reused, "viewport solve/reuse counts");
}
class GatedReader final : public PackageArchiveReader {
public:
	PackageArchive archive;
	mutable std::atomic_bool gate{false}, entered{false}, release{false};
	PackageArchiveFormat format() const override { return archive.format(); }
	QString sourcePath() const override { return archive.sourcePath(); }
	bool isOpen() const override { return archive.isOpen(); }
	QVector<PackageEntry> entries() const override { return archive.entries(); }
	bool pause() const {
		if (!gate) { return true; }
		entered = true; QElapsedTimer timer; timer.start();
		while (!release && timer.elapsed() < 10000) { QThread::msleep(1); }
		return release;
	}
	bool readEntryBytes(const QString& name, QByteArray* out, QString* error, qint64 cap) const override
	{
		return pause() && archive.readEntryBytes(name, out, error, cap);
	}
	bool readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 cap) const override
	{
		return pause() && archive.readEntryAt(index, out, error, cap);
	}
};
} // namespace
int main(int argc, char** argv)
{
	// Semantic Qt calls and widget render targets; no mouse/keyboard injection.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp;
	LevelMapDocument map; QString error;
	if (!temp.isValid() || !tests::createGeometryFixture(12, &map, &error)) { return 1; }
	MapViewport plan, side; plan.resize(800, 600); side.resize(800, 600);
	side.setProjection(MapViewportProjection::SideZY);
	plan.setDocument(map);
	bool ok = counts(plan, 12, 0);
	side.synchronizeSceneFrom(plan, true);
	// Read-only fitting and scene synchronization must not duplicate whole
	// record arrays merely by taking mutable Qt iterators.
	ok &= expect(plan.displayDocument().brushes.constData() == map.brushes.constData() &&
		side.displayDocument().brushes.constData() == map.brushes.constData() &&
		plan.displayDocument().entities.constData() == map.entities.constData() &&
		side.displayDocument().entities.constData() == map.entities.constData(), "unfiltered panes retain shared immutable map records");
	const auto navigation = side.navigationState(); side.setClipMode(true);
	ok &= setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 4}}, &error);
	ok &= moveLevelMapSelection(&map, 8, 0, 0, &error);
	side.updateDocument(map);
	ok &= counts(side, 1, 11);
	ok &= expect(side.clipMode() && side.navigationState().center == navigation.center
		&& side.navigationState().zoom == navigation.zoom, "cache handoff preserves local navigation and tools");
	plan.synchronizeSceneFrom(side);
	ok &= undoLevelMapEdit(&map, &error); plan.updateDocument(map);
	ok &= counts(plan, 1, 11);
	ok &= expect(plan.hideSelection() == 1 && plan.displayDocument().brushes.size() == 11, "hide remains a view operation");
	ok &= counts(plan, 0, 11);
	side.synchronizeSceneFrom(plan);
	ok &= expect(side.hiddenCount() == 1 && side.displayDocument().brushes.size() == 11, "hidden geometry shared with sibling");
	plan.showAllHidden();
	ok &= counts(plan, 1, 11);
	ok &= expect(plan.displayDocument().brushes.size() == 12 && map.brushes.size() == 12, "show restores geometry without editing source");
	plan.setSelectionSet(map.selection);

	// Rendering after reuse must match a fresh solve at both scale extremes and
	// high contrast. Hidden widgets are rendered directly to QImage, never OS capture.
	for (const int scale : {1, 2}) {
		for (const bool highContrast : {false, true}) {
			MapViewport fresh; const QSize size(800 * scale, 600 * scale);
			plan.resize(size); fresh.resize(size);
			auto font = app.font(); font.setPointSizeF(10.0 * scale); plan.setFont(font); fresh.setFont(font);
			plan.setHighContrast(highContrast); fresh.setHighContrast(highContrast);
			fresh.setDocument(map); fresh.restoreNavigationState(plan.navigationState());
			QImage warm(size, QImage::Format_ARGB32_Premultiplied), cold(size, QImage::Format_ARGB32_Premultiplied);
			warm.fill(Qt::transparent); cold.fill(Qt::transparent); plan.render(&warm); fresh.render(&cold);
			ok &= expect(warm == cold, "cached and freshly solved plans render identical pixels");
			if (argc > 1) {
				const auto path = QDir(QString::fromLocal8Bit(argv[1])).filePath(QStringLiteral("geometry-%1-%2.png").arg(scale).arg(highContrast));
				ok &= expect(warm.save(path), "save verification render");
			}
		}
	}
	plan.clearDocument();
	ok &= expect(plan.geometryCacheStatistics().retainedBrushes == 0 && !plan.hasDocument(), "closing map clears viewport cache");

	const auto assetPath = temp.filePath(QStringLiteral("assets/textures/studio")); QDir().mkpath(assetPath);
	QImage texture(64, 64, QImage::Format_RGB32); texture.fill(QColor(70, 140, 190));
	ok &= texture.save(QDir(assetPath).filePath(QStringLiteral("cache.png")));
	auto reader = std::make_shared<GatedReader>(); ok &= reader->archive.load(temp.filePath(QStringLiteral("assets")), &error);
	LevelPreviewWorker worker; LevelPreviewResult latest; int publications = 0;
	worker.completed = [&](const auto& result) { latest = result; ++publications; };
	LevelPreviewRequest request; request.document = map; request.archive = reader;
	request.sourceKey = QStringLiteral("geometry-fixture"); request.assetKey = QStringLiteral("first"); request.loadSerial = 10;
	const auto run = [&](int solved, int reused) {
		const int before = publications; worker.request(request);
		return expect(until([&] { return !worker.busy(); }) && publications == before + 1 && latest.error.isEmpty()
			&& !latest.preview.cancelled && !latest.preview.truncated && latest.preview.triangles == request.document.brushes.size() * 12
			&& latest.assets.readyCount() == 1 && latest.geometryCache.solved == solved && latest.geometryCache.reused == reused,
			"worker publishes complete mesh and expected cache diagnostics");
	};
	ok &= run(12, 0);
	ok &= moveLevelMapSelection(&request.document, 8, 0, 0, &error);
	ok &= run(1, 11);
	const auto uv = latest.preview.mesh.surfaces.value(0).texCoords.value(0);
	texture = QImage(128, 64, QImage::Format_RGB32); texture.fill(Qt::red);
	ok &= texture.save(QDir(assetPath).filePath(QStringLiteral("cache.png")));
	ok &= reader->archive.load(temp.filePath(QStringLiteral("assets")), &error);
	request.assetKey = QStringLiteral("new-image");
	ok &= run(0, 12);
	const auto newUv = latest.preview.mesh.surfaces.value(0).texCoords.value(0);
	ok &= expect(uv.u != newUv.u && levelPreviewTextureSizes(latest.assets).value(QStringLiteral("studio/cache")) == QSize(128, 64),
		"package replacement refreshes dimensions and UVs with cached polygons");

	// Hold an actual package read in flight, replace the request, then let the
	// retired job finish. Its changed geometry must never become the live cache.
	reader->gate = true; request.assetKey = QStringLiteral("retired");
	auto retired = request;
	for (auto& brush : retired.document.brushes) {
		for (auto& face : brush.faces) { face.p0.z += 128; face.p1.z += 128; face.p2.z += 128; }
	}
	const int before = publications; worker.request(retired);
	ok &= expect(until([&] { return reader->entered.load(); }), "retired package read in flight");
	request.assetKey = QStringLiteral("new-image"); request.loadSerial = 11;
	ok &= moveLevelMapSelection(&request.document, 8, 0, 0, &error);
	worker.request(request); reader->release = true;
	ok &= expect(until([&] { return !worker.busy(); }) && publications == before + 1 && latest.loadSerial == 11
		&& latest.geometryCache.solved == 1 && latest.geometryCache.reused == 11, "retired result cannot publish geometry cache");
	worker.request(request); worker.cancel();
	ok &= expect(latest.assets.cancelled && until([&] { return !worker.busy(); }), "queued cancellation is explicit");
	ok &= run(0, 12);
	request.document.brushes = {map.brushes[0]}; request.document.brushes[0].id = 500;
	request.sourceKey = QStringLiteral("other-map");
	ok &= run(1, 0);
	ok &= expect(latest.geometryCache.retainedBrushes == 1, "new map prunes departed cache entries");
	if (!ok && !error.isEmpty()) { std::cerr << error.toStdString() << '\n'; }
	return ok ? 0 : 1;
}
