#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_patch.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/map_viewport_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label)
{
	if (!value) { std::cerr << label << '\n'; }
	return value;
}
QPointF projected(LevelMapVec3 point, MapViewportProjection projection)
{
	if (projection == MapViewportProjection::FrontXZ) { return {point.x, point.z}; }
	if (projection == MapViewportProjection::SideZY) { return {point.y, point.z}; }
	return {point.x, point.y};
}
QImage render(MapViewport& view, int scale = 1)
{
	QImage image(view.size() * scale, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(scale);
	if (!tests::settleMapViewport(view, &image)) { std::cerr << "Plan render timed out\n"; std::exit(EXIT_FAILURE); }
	return image;
}
bool highlighted(QRgb pixel, bool highContrast)
{
	return highContrast ? qRed(pixel) > 230 && qGreen(pixel) < 30 && qBlue(pixel) > 230
		: qRed(pixel) > 240 && qGreen(pixel) > 185 && qGreen(pixel) < 225 && qBlue(pixel) < 110;
}
int highlightedAlong(const QImage& image, MapViewport& view, QPointF a, QPointF b, bool highContrast)
{
	int count = 0;
	for (int i = 0; i < 100; ++i) {
		const auto world = a + (b - a) * (i / 99.0);
		const auto point = (view.viewPointFor(world.x(), world.y()) * image.devicePixelRatio()).toPoint();
		if (image.rect().contains(point) && highlighted(image.pixel(point), highContrast)) { ++count; }
	}
	return count;
}
bool matchesFresh(MapViewport& view, const LevelMapDocument& document, bool highContrast = false, int scale = 1, const char* label = "selection outline cache matches a freshly rebuilt scene")
{
	MapViewport fresh; fresh.resize(view.size()); fresh.setFont(view.font()); fresh.setLayoutDirection(view.layoutDirection());
	fresh.setHighContrast(highContrast); fresh.setShowGrid(view.showGrid()); fresh.setShowLabels(view.showLabels());
	fresh.setDocument(document); fresh.setSelectionSet(view.selectionSet()); fresh.restoreNavigationState(view.navigationState());
	const auto actual = render(view, scale), expected = render(fresh, scale);
	if (actual != expected && !qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_DIR")) {
		const QDir output(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR"));
		actual.save(output.filePath("mismatch-actual.png")); expected.save(output.filePath("mismatch-fresh.png"));
	}
	return expect(actual == expected, label);
}
void placeBrush(LevelMapBrush* brush, LevelMapVec3 center)
{
	const double dx = center.x - (brush->mins.x + brush->maxs.x) * 0.5;
	const double dy = center.y - (brush->mins.y + brush->maxs.y) * 0.5;
	const double dz = center.z - (brush->mins.z + brush->maxs.z) * 0.5;
	for (auto& face : brush->faces) {
		for (auto* point : {&face.p0, &face.p1, &face.p2}) { point->x += dx; point->y += dy; point->z += dz; }
	}
	brush->mins = {center.x - 16, center.y - 16, center.z - 16, true};
	brush->maxs = {center.x + 16, center.y + 16, center.z + 16, true};
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
}

int main(int argc, char** argv)
{
	// Semantic methods and QWidget render targets only; no input or OS capture.
	qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	QString error; bool ok = true;
	LevelMapDocument map; if (!tests::createGeometryFixture(2, &map, &error)) { return 1; }
	map.brushes[0].id = 101; map.brushes[1].id = 701;
	placeBrush(&map.brushes[1], {160,160,160,true});
	const double c = std::sqrt(0.5), radius = 32 * c;
	for (auto& face : map.brushes[0].faces) {
		for (auto* point : {&face.p0, &face.p1, &face.p2}) {
			const double x = point->x, y = point->y; point->x = c * (x - y); point->y = c * (x + y);
		}
	}
	map.brushes[0].mins = {-radius,-radius,-16,true}; map.brushes[0].maxs = {radius,radius,16,true};
	const auto serialized = serializeLevelMap(map); ok &= expect(serialized.succeeded(), "synthetic map serializes successfully");
	const auto source = serialized.bytes;
	MapViewport view, sibling; view.resize(1000, 800); view.setShowGrid(false); view.setShowLabels(false);
	for (const int scale : {1, 2}) {
		const bool contrast = scale == 2;
		applyStudioTheme(app, studioThemeTokens(contrast ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale * 100));
		Expanded translator; if (contrast) { app.installTranslator(&translator); }
		view.setFont(app.font()); view.setHighContrast(contrast); view.setLayoutDirection(contrast ? Qt::RightToLeft : Qt::LeftToRight);
		view.setDocument(map);
		for (const auto projection : {MapViewportProjection::TopXY, MapViewportProjection::FrontXZ, MapViewportProjection::SideZY}) {
			view.setProjection(projection); view.setSelectionSet({});
			auto state = view.navigationState(); state.center = {72,72}; state.zoom = 3; view.applyLinkedNavigation(state);
			const auto before = render(view, scale);
			const auto edge = projection == MapViewportProjection::TopXY ? QPointF(-radius * 0.5, radius * 0.5) : QPointF(-radius * 0.5, 16);
			const auto hits = view.objectsAt(view.viewPointFor(edge.x(), edge.y()));
			view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 101}});
			const auto selected = render(view, scale);
			const auto a = projection == MapViewportProjection::TopXY ? QPointF(-radius * 0.8, radius * 0.2) : QPointF(-radius * 0.8, 16);
			const auto b = projection == MapViewportProjection::TopXY ? QPointF(-radius * 0.2, radius * 0.8) : QPointF(-radius * 0.2, 16);
			const auto ink = highlightedAlong(selected, view, a, b, contrast);
			ok &= expect(ink > 25 && ink < 95, "selected actual edges are visibly dashed, including oblique geometry and high DPI");
			const QRect other((view.viewPointFor(144,160) * scale).toPoint() - QPoint(7,7), QSize(14,14));
			ok &= expect(before.copy(other) == selected.copy(other), "unselected brush edges remain unchanged");
			ok &= expect(hits == view.objectsAt(view.viewPointFor(edge.x(), edge.y())) && hits.contains({LevelMapSelectionKind::QuakeBrush,101}),
				"highlighting preserves independent picking identities");
			ok &= matchesFresh(view, map, contrast, scale);
			const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!output.isEmpty()) {
				QDir().mkpath(output); ok &= selected.save(QDir(output).filePath(QStringLiteral("selected-outline-%1-%2.png").arg(scale).arg(int(projection))));
			}
			view.setSelectionSet({});
			ok &= expect(before == render(view, scale) && !view.selectionWireStatistics().ready, "clearing selection removes the whole overlay without rebuilding source drawing");
		}
		if (contrast) { app.removeTranslator(&translator); }
	}
	ok &= expect(source == serializeLevelMap(map).bytes && map.undoStack.isEmpty() && map.redoStack.isEmpty(), "rendering and local selection never mutate source or history");

	// A sparse owner without an origin expands to its visible brushes and patch.
	LevelMapDocument owned; ok &= tests::createGeometryFixture(3, &owned, &error);
	LevelMapEntity owner; owner.id = 71; owner.className = "func_group"; owned.entities.append(owner);
	for (qsizetype i = 0; i < owned.brushes.size(); ++i) {
		auto& brush = owned.brushes[i]; brush.id = 101 + int(i) * 300; brush.entityId = owner.id;
		placeBrush(&brush, {i * 96.0, i * 96.0, i * 96.0, true});
	}
	LevelMapPatch patch; LevelPatchCreateRequest request; request.center = {-96,96,0,true}; request.size = {32,32,32,true};
	ok &= createLevelPatch(request, &patch, &error); patch.id = 509; patch.entityId = owner.id;
	for (int row = 0; row < 3; ++row) { patch.controlPoints[row * 3 + 1].z = 32; }
	refreshLevelPatchBounds(&patch); owned.patches.append(patch);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	view.setHighContrast(false); view.setLayoutDirection(Qt::LeftToRight); view.setFont(app.font()); sibling.setFont(app.font());
	view.setDocument(owned); view.setProjection(MapViewportProjection::TopXY);
	view.setSelectionSet({{LevelMapSelectionKind::Entity, owner.id}}); view.zoomToFit(); render(view);
	const auto ownerWires = view.selectionWireStatistics();
	ok &= expect(ownerWires.ready && ownerWires.uniqueEdges >= 24, "selected owner includes all brush and tessellated patch borders");
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,101}, {LevelMapSelectionKind::QuakePatch,509}, {LevelMapSelectionKind::Entity,owner.id}});
	render(view);
	ok &= expect(view.selectionWireStatistics().sourceEdges == ownerWires.sourceEdges, "owner plus explicit children emits each object once");
	sibling.resize(view.size()); sibling.synchronizeSceneFrom(view);
	ok &= expect(sibling.selectionWireStatistics().ready && sibling.selectionWireStatistics().uniqueEdges == ownerWires.uniqueEdges,
		"same-projection siblings share selection drawing data");
	for (const auto projection : {MapViewportProjection::TopXY, MapViewportProjection::FrontXZ, MapViewportProjection::SideZY}) {
		view.setProjection(projection); view.setSelectionSet({{LevelMapSelectionKind::Entity, owner.id}}); view.zoomToFit();
		const auto selected = render(view);
		const auto a = projected({-112,80,0,true}, projection), b = projected({-112 + 32.0/3,80,128.0/9,true}, projection);
		ok &= expect(highlightedAlong(selected, view, a, b, false) > 10, "owned curved patch border is highlighted in each projection");
		const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!output.isEmpty()) { ok &= selected.save(QDir(output).filePath(QStringLiteral("selected-owner-%1.png").arg(int(projection)))); }
	}
	view.setProjection(MapViewportProjection::TopXY);
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,401}, {LevelMapSelectionKind::QuakePatch,509}});
	ok &= expect(view.hideSelection() == 2, "hide individual brush and patch");
	view.setSelectionSet({{LevelMapSelectionKind::Entity, owner.id}}); render(view);
	ok &= expect(view.displayDocument().brushes.size() == 2 && view.displayDocument().patches.isEmpty()
		&& view.selectionWireStatistics().uniqueEdges == 8, "owner highlighting excludes hidden children");
	view.showAllHidden(); render(view);
	ok &= expect(view.selectionWireStatistics().uniqueEdges == ownerWires.uniqueEdges, "showing children restores their outlines");

	// Limits discard the whole overlay, while its fallback visitor remains complete.
	MapViewportSceneIndex index; index.rebuild(owned); const auto geometry = buildLevelMapBrushGeometry(owned); index.rebuildGeometry(geometry, {});
	for (int limit = 0; limit < 4; ++limit) {
		MapPlanWireLimits limits;
		if (limit == 0) { limits.sourceEdges = 1; }
		if (limit == 1) { limits.uniqueEdges = 1; }
		if (limit == 2) { limits.batches = 0; }
		if (limit == 3) { limits.retainedBytes = 1; }
		const auto wires = buildMapPlanSelectionWires(owned, geometry, index, MapViewportProjection::TopXY, {{LevelMapSelectionKind::Entity,owner.id}}, limits);
		ok &= expect(!wires.statistics.ready && wires.batches.isEmpty(), "selection cache exhaustion never publishes a partial selection");
	}
	int outlines = 0;
	ok &= visitMapPlanSelectionOutlines(owned, geometry, index, MapViewportProjection::TopXY, {{LevelMapSelectionKind::Entity,owner.id}},
		[&](const QPolygonF&) { ++outlines; return true; });
	ok &= expect(outlines >= 4, "complete uncached visitor includes all owned objects");
	view.resize(1600,1400); view.zoomToFit();
	ok &= matchesFresh(view, owned, false, 2, "over-limit selection fallback matches fresh"); // 8.96M physical pixels exercises the complete QPainter fallback.
	ok &= expect(highlightedAlong(render(view, 2), view, {84,112}, {108,112}, false) > 20,
		"over-limit fallback visibly draws selected geometry beyond the owner's resize bounds and markers");
	view.resize(1000,800);
	ok &= setLevelMapSelection(&owned, {{LevelMapSelectionKind::Entity,owner.id}}, &error) && moveLevelMapSelection(&owned,24,48,72,&error);
	view.updateDocument(owned); ok &= matchesFresh(view, owned, false, 1, "edited selection matches fresh");
	ok &= undoLevelMapEdit(&owned, &error); view.updateDocument(owned); ok &= matchesFresh(view, owned, false, 1, "undone selection matches fresh");
	ok &= redoLevelMapEdit(&owned, &error); view.updateDocument(owned); ok &= matchesFresh(view, owned, false, 1, "redone selection matches fresh");
	sibling.synchronizeSceneFrom(view); ok &= matchesFresh(sibling, owned, false, 1, "sibling selection matches fresh");
	ok &= deleteLevelMapObjects(&owned, {{LevelMapSelectionKind::QuakeBrush,401}}, &error);
	view.updateDocument(owned); ok &= matchesFresh(view, owned, false, 1, "deleted selection matches fresh");
	owned.brushes = {owned.brushes.last()}; view.setDocument(owned); view.setSelectionSet({{LevelMapSelectionKind::Entity,owner.id}});
	ok &= matchesFresh(view, owned, false, 1, "reloaded selection matches fresh"); // Reload with unchanged path/revision retires all old drawing data.

	LevelMapDocument many; ok &= tests::createGeometryFixture(625, &many, &error);
	view.setDocument(many); auto nearEnd = view.navigationState(); nearEnd.center = {23 * 48,24 * 48}; nearEnd.zoom = 4; view.applyLinkedNavigation(nearEnd);
	QVector<LevelMapSelectionRef> everyone;
	for (const auto& brush : many.brushes) { everyone.append({LevelMapSelectionKind::QuakeBrush,brush.id}); }
	view.setSelectionSet(everyone);
	ok &= expect(highlightedAlong(render(view), view, {23 * 48 - 16,24 * 48 + 4}, {23 * 48 - 16,24 * 48 + 12}, false) > 20,
		"selected edges beyond the 512-member marker limit remain visible");

	LevelMapDocument broken; ok &= tests::createGeometryFixture(1, &broken, &error);
	broken.brushes[0].faces.resize(2); view.setDocument(broken); view.setProjection(MapViewportProjection::TopXY);
	auto state = view.navigationState(); state.center = {0,0}; state.zoom = 8; view.applyLinkedNavigation(state);
	const auto warning = render(view);
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,0}}); const auto selectedBroken = render(view);
	const QRect cross(view.viewPointFor(-8,-8).toPoint() - QPoint(3,3), QSize(6,6));
	ok &= expect(view.selectionWireStatistics().uniqueEdges == 4 && warning.copy(cross) == selectedBroken.copy(cross),
		"selected unsolved brush keeps its warning cross and highlights only the diagnostic perimeter");
	view.clearDocument(); sibling.synchronizeSceneFrom(view);
	ok &= expect(!view.selectionWireStatistics().ready && !sibling.selectionWireStatistics().ready, "closed scenes release selected drawing data");
	if (!ok) { std::cerr << error.toStdString() << '\n'; }
	return ok ? 0 : 1;
}
