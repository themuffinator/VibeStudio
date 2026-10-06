#include "app/map_viewport.h"
#include "app/studio_theme.h"
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
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool close(double a, double b) { return std::abs(a - b) < 1e-6; }
QPointF projected(const LevelMapVec3& point, MapViewportProjection projection)
{
	if (projection == MapViewportProjection::FrontXZ) { return {point.x, point.z}; }
	if (projection == MapViewportProjection::SideZY) { return {point.y, point.z}; }
	return {point.x, point.y};
}
bool matchesBounds(MapViewport& view, const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& selection)
{
	LevelMapVec3 low, high;
	if (!levelMapObjectsBounds(document, selection, &low, &high)) { return false; }
	const auto a = projected(low, view.projection()), b = projected(high, view.projection());
	const auto extent = view.selectionExtent();
	return expect(close(extent.width(), b.x() - a.x()) && close(extent.height(), b.y() - a.y()), "selection extent matches source geometry");
}
bool matchesFresh(MapViewport& view, const LevelMapDocument& document, const char* label)
{
	MapViewport fresh; fresh.resize(view.size()); fresh.setFont(view.font());
	fresh.setLayoutDirection(view.layoutDirection()); fresh.setHighContrast(view.property("testHighContrast").toBool());
	fresh.setSnapToGrid(view.snapToGrid());
	fresh.setDocument(document); fresh.setSelectionSet(view.selectionSet()); fresh.restoreNavigationState(view.navigationState());
	QImage actual(view.size(), QImage::Format_ARGB32_Premultiplied), expected(view.size(), QImage::Format_ARGB32_Premultiplied);
	if (!tests::settleMapViewport(view, &actual) || !tests::settleMapViewport(fresh, &expected)) { return expect(false, "Plan render timed out"); }
	return expect(view.selectionExtent() == fresh.selectionExtent() && view.hasResizeHandles() == fresh.hasResizeHandles()
		&& actual == expected, label);
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
}
int main(int argc, char** argv)
{
	// No input injection, desktop pointer access or OS screen capture.
	qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	LevelMapDocument map; QString error;
	if (!tests::createGeometryFixture(257, &map, &error)) { return 1; }
	for (auto& brush : map.brushes) { brush.id = brush.id * 3 + 101; }
	QVector<LevelMapSelectionRef> all;
	for (const auto& brush : map.brushes) { all.append({LevelMapSelectionKind::QuakeBrush, brush.id}); }
	MapViewport view, sibling; view.resize(960, 640); sibling.resize(960, 640);
	view.setDocument(map); view.setSelectionSet(all);
	bool ok = true;
	for (const auto projection : {MapViewportProjection::TopXY, MapViewportProjection::FrontXZ, MapViewportProjection::SideZY}) {
		view.setProjection(projection); view.zoomToSelection();
		ok &= matchesBounds(view, map, all) && matchesFresh(view, map, "large sparse selection matches a fresh viewport");
		ok &= matchesBounds(view, map, all); // Repeated requests use the same geometric result.
	}
	sibling.synchronizeSceneFrom(view, true);
	ok &= matchesBounds(sibling, map, all);
	const QVector<LevelMapSelectionRef> pair {all.first(), all.last()};
	view.setSelectionSet(pair); view.setProjection(MapViewportProjection::TopXY);
	ok &= matchesBounds(view, map, pair);
	ok &= setLevelMapSelection(&map, {all.last()}, &error) && moveLevelMapSelection(&map, 48, 96, 24, &error);
	view.updateDocument(map); view.setSelectionSet(pair);
	ok &= matchesBounds(view, map, pair) && matchesFresh(view, map, "edit invalidates extent, markers and resize bounds");
	sibling.synchronizeSceneFrom(view);
	ok &= matchesBounds(sibling, map, pair) && matchesFresh(sibling, map, "sibling adopts new geometry and selection caches");
	ok &= undoLevelMapEdit(&map, &error); view.updateDocument(map); view.setSelectionSet(pair);
	ok &= matchesBounds(view, map, pair) && matchesFresh(view, map, "undo invalidates selection geometry");
	ok &= redoLevelMapEdit(&map, &error); view.updateDocument(map); view.setSelectionSet(pair);
	ok &= matchesBounds(view, map, pair) && matchesFresh(view, map, "redo invalidates selection geometry");

	view.setSelectionSet({all.first()}); ok &= expect(view.hideSelection() == 1, "hide sparse brush");
	view.setSelectionSet({all.last()}); view.zoomToSelection();
	ok &= matchesBounds(view, map, {all.last()});
	view.showAllHidden(); view.setSelectionSet(pair);
	ok &= matchesFresh(view, map, "show rebuilds filtered lookup without changing the source");
	ok &= deleteLevelMapObjects(&map, {all[1]}, &error); view.updateDocument(map); view.setSelectionSet(pair);
	ok &= matchesBounds(view, map, pair) && matchesFresh(view, map, "deletion shifts sparse storage without stale index hits");
	// A same-path reload with the same revision still replaces the scene.
	map.brushes = {map.brushes.last()}; view.setDocument(map); view.setSelectionSet(pair);
	ok &= matchesBounds(view, map, pair) && matchesFresh(view, map, "reload discards indexes for departed objects");
	view.clearDocument(); sibling.synchronizeSceneFrom(view);
	ok &= expect(!view.hasDocument() && !sibling.hasDocument() && view.selectionExtent().isEmpty() && !view.hasResizeHandles(), "closing clears scene and selection caches");

	// Hidden brushes still participate in their owner's authoring bounds.
	LevelMapDocument owned;
	ok &= tests::createGeometryFixture(2, &owned, &error);
	LevelMapEntity owner; owner.id = 71; owner.className = "func_group";
	owned.entities.append(owner);
	for (auto& brush : owned.brushes) { brush.entityId = owner.id; }
	view.setDocument(owned); view.setSelectionSet({{LevelMapSelectionKind::Entity, owner.id}}); view.zoomToSelection();
	ok &= matchesBounds(view, owned, view.selectionSet()) && matchesFresh(view, owned, "owner without an origin frames its geometry");
	ok &= expect(view.navigationState().center == QPointF(24, 0), "owner frames geometry instead of falling back to the whole map");
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 1}}); view.hideSelection();
	view.setSelectionSet({{LevelMapSelectionKind::Entity, owner.id}}); view.setSnapToGrid(false);
	view.zoomToSelection();
	ok &= expect(view.selectionExtent() == QSizeF(32, 32) && view.navigationState().center == QPointF(0, 0), "owner framing excludes hidden children");
	LevelMapVec3 low, high; ok &= levelMapObjectsBounds(owned, view.selectionSet(), &low, &high);
	const auto handle = view.worldPositionAt(view.resizeHandlePosition(MapViewport::ResizeMaxHorizontal | MapViewport::ResizeMaxVertical), 0);
	ok &= expect(view.displayDocument().brushes.size() == 1 && view.hasResizeHandles() && close(handle.x, high.x) && close(handle.y, high.y),
		"resize uses complete shared authoring bounds including a hidden owned brush");

	// Point-like objects and straight lines must not disappear from a union.
	LevelMapDocument points; points.format = LevelMapFormat::QuakeMap;
	LevelMapEntity first; first.id = 41; first.className = "light"; first.origin = {-64, -32, 8, true};
	LevelMapEntity second = first; second.id = 95; second.origin = {128, 160, 72, true};
	points.entities = {first, second}; points.selection = {{LevelMapSelectionKind::Entity, 41}, {LevelMapSelectionKind::Entity, 95}};
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		Expanded expansion; if (scale == 200) { app.installTranslator(&expansion); }
		view.setFont(app.font()); view.resize(960 * scale / 100, 640 * scale / 100); view.setHighContrast(scale == 200);
		view.setProperty("testHighContrast", scale == 200); view.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		view.setDocument(points);
		for (const auto projection : {MapViewportProjection::TopXY, MapViewportProjection::FrontXZ, MapViewportProjection::SideZY}) {
			view.setProjection(projection); view.zoomToSelection();
			ok &= matchesBounds(view, points, points.selection) && matchesFresh(view, points, "point selection frames both points at expanded scale");
			const auto expectedCenter = (projected(first.origin, projection) + projected(second.origin, projection)) * 0.5;
			ok &= expect(view.navigationState().center == expectedCenter, "framing centers both point objects");
			const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
			if (!captures.isEmpty()) {
				QDir().mkpath(captures); QImage image(view.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); view.render(&image);
				ok &= image.save(QDir(captures).filePath(QStringLiteral("point-selection-%1-%2.png").arg(scale).arg(int(projection))));
			}
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	LevelMapCreateRequest doomRequest; doomRequest.game = "doom";
	LevelMapDocument doom; ok &= createLevelMap(doomRequest, &doom, &error);
	if (!doom.doomVertices.isEmpty()) {
		QVector<LevelMapSelectionRef> vertices;
		for (const auto& vertex : doom.doomVertices) { vertices.append({LevelMapSelectionKind::DoomVertex, vertex.id}); }
		view.setProjection(MapViewportProjection::TopXY); view.setDocument(doom); view.setSelectionSet(vertices);
		ok &= matchesBounds(view, doom, vertices) && matchesFresh(view, doom, "Doom vertices retain their combined extent");
		view.setSelectionSet({{LevelMapSelectionKind::DoomSector, doom.doomSectors.first().id}});
		ok &= matchesFresh(view, doom, "Doom sector lookup retains outline bounds");
		view.setSelectionSet({{LevelMapSelectionKind::DoomLinedef, doom.doomLinedefs.first().id}});
		ok &= matchesBounds(view, doom, view.selectionSet()) && matchesFresh(view, doom, "straight Doom line remains a frameable extent");
	}
	if (!ok) { std::cerr << error.toStdString() << '\n'; }
	return ok ? 0 : 1;
}
