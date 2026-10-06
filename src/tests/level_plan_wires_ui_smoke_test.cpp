#include "app/map_plan_wires.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/map_viewport_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QImage>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label)
{
	if (!value) { std::cerr << label << '\n'; }
	return value;
}
bool exact(QPointF a, QPointF b) { return a.x() == b.x() && a.y() == b.y(); }
bool contains(const MapPlanWires& wires, QPointF a, QPointF b)
{
	for (const auto& batch : wires.batches) {
		for (const auto& line : batch.lines) {
			if ((exact(a, line.p1()) && exact(b, line.p2())) || (exact(a, line.p2()) && exact(b, line.p1()))) { return true; }
		}
	}
	return false;
}
QImage render(MapViewport& view, int deviceScale = 1)
{
	QImage image(view.size() * deviceScale, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(deviceScale);
	if (!tests::settleMapViewport(view, &image)) { std::cerr << "Plan render timed out\n"; std::exit(EXIT_FAILURE); }
	return image;
}
bool matchesFresh(MapViewport& view, const LevelMapDocument& document, bool highContrast, int deviceScale)
{
	MapViewport fresh; fresh.resize(view.size()); fresh.setFont(view.font()); fresh.setLayoutDirection(view.layoutDirection());
	fresh.setHighContrast(highContrast); fresh.setShowGrid(view.showGrid()); fresh.setShowLabels(view.showLabels());
	fresh.setDocument(document); fresh.setSelectionSet(view.selectionSet()); fresh.restoreNavigationState(view.navigationState());
	return expect(render(view, deviceScale) == render(fresh, deviceScale), "reused wire data matches a fresh scene after navigation/theme/scale changes");
}
}

int main(int argc, char** argv)
{
	// Direct semantic calls and widget render targets only; no input injection.
	qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	LevelMapDocument map; QString error;
	if (!tests::createGeometryFixture(25, &map, &error)) { return 1; }
	MapViewportSceneIndex index; index.rebuild(map);
	const auto geometry = buildLevelMapBrushGeometry(map);
	bool ok = true;
	for (const auto projection : {MapViewportProjection::TopXY, MapViewportProjection::FrontXZ, MapViewportProjection::SideZY}) {
		const auto wires = buildMapPlanWires(map, geometry, index, projection, 0);
		ok &= expect(wires.statistics.ready && wires.statistics.retainedBytes > 0, "wire scene is complete and bounded");
		ok &= expect(contains(wires, {-16,-16}, {16,-16}) && contains(wires, {16,-16}, {16,16})
			&& contains(wires, {16,16}, {-16,16}) && contains(wires, {-16,16}, {-16,-16}), "all four known box boundary edges survive projection");
		if (projection != MapViewportProjection::TopXY) {
			ok &= expect(wires.statistics.duplicateEdges > wires.statistics.uniqueEdges, "stacked faces collapse only their drawing edges");
		}
		for (int limit = 0; limit < 4; ++limit) {
			MapPlanWireLimits limits;
			if (limit == 0) { limits.sourceEdges = 1; }
			if (limit == 1) { limits.uniqueEdges = 1; }
			if (limit == 2) { limits.batches = 0; }
			if (limit == 3) { limits.retainedBytes = 1; }
			const auto limited = buildMapPlanWires(map, geometry, index, projection, 0, limits);
			ok &= expect(!limited.statistics.ready && limited.batches.isEmpty(), "exhausted wire budget never publishes partial geometry");
		}
	}

	// Coincident geometry with different ownership keeps its original pen order.
	auto first = geometry.first(), second = first, third = first;
	first.entityId = 0; second.entityId = 9; third.entityId = 0;
	const auto styles = buildMapPlanWires(map, {first, second, third}, index, MapViewportProjection::FrontXZ, 0);
	ok &= expect(styles.statistics.ready && styles.batches.size() == 3 && styles.batches[0].style == MapPlanWireStyle::World
		&& styles.batches[1].style == MapPlanWireStyle::Entity && styles.batches[2].style == MapPlanWireStyle::World,
		"world/entity/world overdraw order is preserved");
	// Exact scalar equality matters: QPointF's fuzzy equality would merge these.
	MapBrushGeometry triangle; triangle.solved = true;
	MapFacePolygon face; face.points = {{1,0,1,true}, {32,0,1,true}, {1,0,32,true}}; triangle.faces = {face};
	auto near = triangle;
	for (auto& point : near.faces[0].points) { point.x += 1e-13; }
	const auto closeEdges = buildMapPlanWires(map, {triangle, near}, index, MapViewportProjection::FrontXZ, 0);
	ok &= expect(closeEdges.statistics.ready && closeEdges.statistics.uniqueEdges == 6 && closeEdges.statistics.duplicateEdges == 0,
		"nearby distinct edges are never rounded or merged");
	ok &= expect(contains(closeEdges, {1,32}, {32,1}), "oblique closing edge remains visible");

	MapViewport view, sibling; view.resize(960, 640); sibling.resize(view.size()); view.setShowGrid(false);
	LevelMapDocument single; ok &= tests::createGeometryFixture(1, &single, &error); view.setDocument(single);
	auto pixels = view.navigationState(); pixels.center = {0,0}; pixels.zoom = 2; view.applyLinkedNavigation(pixels);
	const auto logical = render(view), physical = render(view, 2);
	const auto edgePoint = view.viewPointFor(-16, 0).toPoint();
	ok &= expect(physical.pixel(edgePoint * 2) == qRgb(200,210,225) && logical.pixel(edgePoint) != qRgb(200,210,225),
		"high-DPI rendering rasterizes at physical resolution rather than stretching a logical-pixel image");
	view.setDocument(map); view.setProjection(MapViewportProjection::FrontXZ);
	const auto before = view.objectsAt(view.viewPointFor(-16, 0)); render(view);
	const auto after = view.objectsAt(view.viewPointFor(-16, 0));
	ok &= expect(before == after && before.contains({LevelMapSelectionKind::QuakeBrush, 0})
		&& before.contains({LevelMapSelectionKind::QuakeBrush, 5}), "projected duplicates keep independent picking identities");
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 20}});
	const QRect marker(view.viewPointFor(0, 0).toPoint() - QPoint(12, 12), QSize(24, 24));
	const auto member = render(view).copy(marker);
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 5},
		{LevelMapSelectionKind::QuakeBrush, 10}, {LevelMapSelectionKind::QuakeBrush, 15}, {LevelMapSelectionKind::QuakeBrush, 20}});
	ok &= expect(member == render(view).copy(marker), "coincident member markers share one stroke while the primary remains distinct");
	sibling.setProjection(view.projection()); sibling.synchronizeSceneFrom(view, true);
	ok &= expect(sibling.planWireStatistics().ready && sibling.planWireStatistics().uniqueEdges == view.planWireStatistics().uniqueEdges,
		"same-projection sibling adopts complete reusable drawing data");
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 5}});
	for (const int scale : {1, 2}) {
		applyStudioTheme(app, studioThemeTokens(scale == 1 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale * 100));
		view.resize(960 * scale, 640 * scale); view.setFont(app.font()); view.setHighContrast(scale == 2);
		view.setLayoutDirection(scale == 1 ? Qt::LeftToRight : Qt::RightToLeft);
		for (const auto projection : {MapViewportProjection::TopXY, MapViewportProjection::FrontXZ, MapViewportProjection::SideZY}) {
			view.setProjection(projection); view.zoomToFit(); render(view);
			auto state = view.navigationState(); state.center += QPointF(47.25, -11.75); state.zoom *= 1.375; view.applyLinkedNavigation(state);
			ok &= matchesFresh(view, map, scale == 2, scale);
			const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!output.isEmpty()) {
				QDir().mkpath(output);
				ok &= render(view, scale).save(QDir(output).filePath(QStringLiteral("wires-%1-%2.png").arg(scale).arg(int(projection))));
			}
		}
	}

	// A malformed brush retains its parser-bounds warning box and cross.
	auto invalid = map.brushes.first(); invalid.id = 900; invalid.faces.resize(2);
	invalid.mins = {-96,-16,-16,true}; invalid.maxs = {-64,16,16,true}; invalid.boundsSolved = true;
	map.brushes.append(invalid); index.rebuild(map);
	const auto invalidGeometry = buildLevelMapBrushGeometry(map);
	const auto diagnostics = buildMapPlanWires(map, invalidGeometry, index, MapViewportProjection::FrontXZ, 0);
	ok &= expect(diagnostics.statistics.ready && !diagnostics.batches.isEmpty()
		&& diagnostics.batches.last().style == MapPlanWireStyle::Invalid
		&& diagnostics.batches.last().invalidBrushes.contains(QRectF(-96,-16,32,32)), "unsolved brush diagnostics survive the batched path");
	view.setProjection(MapViewportProjection::FrontXZ); view.setDocument(map); render(view);
	ok &= matchesFresh(view, map, true, 2);
	view.clearDocument(); sibling.synchronizeSceneFrom(view);
	ok &= expect(!view.planWireStatistics().ready && view.planWireStatistics().retainedBytes == 0
		&& !sibling.planWireStatistics().ready, "closing a shared scene releases its wire data");

	LevelMapDocument many; ok &= tests::createGeometryFixture(625, &many, &error);
	view.setHighContrast(false); view.setProjection(MapViewportProjection::TopXY); view.setDocument(many);
	auto nearEnd = view.navigationState(); nearEnd.center = {23 * 48, 24 * 48}; nearEnd.zoom = 4; view.applyLinkedNavigation(nearEnd);
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 623}, {LevelMapSelectionKind::QuakeBrush, 624}});
	const QRect visibleMember(view.viewPointFor(23 * 48, 24 * 48).toPoint() - QPoint(12,12), QSize(24,24));
	const auto visibleReference = render(view).copy(visibleMember);
	QVector<LevelMapSelectionRef> everyone;
	for (const auto& brush : many.brushes) { everyone.append({LevelMapSelectionKind::QuakeBrush, brush.id}); }
	view.setSelectionSet(everyone);
	ok &= expect(visibleReference == render(view).copy(visibleMember), "offscreen members cannot consume the visible selection-marker budget");
	if (!ok) { std::cerr << error.toStdString() << '\n'; }
	return ok ? 0 : 1;
}
