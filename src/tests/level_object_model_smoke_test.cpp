#include "tests/level_object_test_helpers.h"
#include "tests/level_geometry_test_helpers.h"
#include "app/studio_theme.h"
#include "core/level_primitive.h"
#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QTimer>
#include <QThread>
#include <QTranslator>
#include <atomic>
#include <cstring>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; ok = false; }
	return condition;
}
void drain(int ms = 30)
{
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec(QEventLoop::ExcludeUserInputEvents);
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 60000) { drain(10); }
	return ready();
}
class CountingTranslator final : public QTranslator {
public:
	mutable std::atomic_int brushLabels{0};
	mutable std::atomic_int workerBrushLabels{0};
	bool expanded = false;
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		if (std::strcmp(source, "Brush %1") == 0) {
			++brushLabels;
			if (QThread::currentThread() != qApp->thread()) { ++workerBrushLabels; }
		}
		return expanded ? QStringLiteral("[%1 %2]").arg(QString::fromUtf8(source), QString(20, QLatin1Char('~'))) : QString();
	}
};
QStringList kept(const LevelObjectList& list)
{
	QStringList values;
	for (int row = 0; row < list.objectModel()->objectCount(); ++row) {
		if (!list.isRowHidden(row)) { values << list.model()->index(row, 0).data(Qt::UserRole).toString(); }
	}
	return values;
}
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	CountingTranslator translations;
	app.installTranslator(&translations);
	QString error;
	LevelMapDocument source;
	if (!expect(tests::createGeometryFixture(50000, &source, &error), "generated fixture")) { return 1; }
	for (int i = 0; i < source.brushes.size(); ++i) { source.brushes[i].id = i * 3 + 17; }
	const auto lastId = source.brushes.last().id;
	LevelMapEntity light; light.id = 51; light.className = QStringLiteral("light");
	light.origin = {24, 24, 64, true}; light.properties = {{"targetname", "first\nsecond"}, {"light", "300"}};
	source.entities.append(light);
	LevelMapPatch patch; patch.id = 17; patch.entityId = 0; patch.width = 3; patch.height = 5; patch.textureName = "test/patch";
	source.patches.append(patch);

	LevelObjectList list;
	list.setAccessibleName(QStringLiteral("Level map objects"));
	list.resize(280, 480);
	QElapsedTimer elapsed; elapsed.start();
	list.setDocument(source);
	const double resetMs = elapsed.nsecsElapsed() / 1e6;
	expect(translations.brushLabels == 0, "reset does not format any brush labels");
	expect(list.objectModel()->objectCount() == 50003 && list.rowForSelector("brush:17") == 2 &&
		list.rowForSelector("patch:17") == 50002 && list.rowForSelector("brush:18") == -1, "complete rows and sparse identity lookup");
	const auto initial = list.objectModel()->snapshot();
	list.setSelectedReferences({{LevelMapSelectionKind::QuakeBrush, lastId}, {LevelMapSelectionKind::Entity, 51}},
		{LevelMapSelectionKind::QuakeBrush, lastId});
	expect(list.selectedReferences() == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::Entity, 51}, {LevelMapSelectionKind::QuakeBrush, lastId}},
		"multi-selection retains primary ordering and reaches tail IDs");
	expect(translations.brushLabels < 10, "selection lookup does not format the whole map");

	QJsonArray layouts;
	for (const int scale : {100, 200}) {
		translations.expanded = scale == 200;
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		list.ensurePolished();
		list.show();
		drain(40);
		translations.brushLabels = 0;
		elapsed.restart();
		list.doItemsLayout();
		list.scrollTo(list.model()->index(list.rowForSelector(QStringLiteral("brush:%1").arg(lastId)), 0));
		drain(40);
		const double layoutMs = elapsed.nsecsElapsed() / 1e6;
		expect(list.verticalScrollBar()->value() > 0 && list.visualRect(list.currentIndex()).intersects(list.viewport()->rect()), "tail row is reachable and visible");
		const auto height = list.visualRect(list.model()->index(0, 0)).height();
		expect(height > 0 && list.visualRect(list.model()->index(1, 0)).height() == height &&
			list.visualRect(list.model()->index(2, 0)).height() == height, "uniform two-line rows including multiline source fields");
		const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!capture.isEmpty()) {
			QDir().mkpath(capture);
			QImage image(list.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); list.render(&image);
			expect(image.save(QDir(capture).filePath(QStringLiteral("outliner-tail-%1.png").arg(scale))), "owned outliner render");
		}
		expect(translations.brushLabels < 500, "layout and painting request only a bounded visible subset");
		layouts << QJsonObject{{"scale", scale}, {"layout_and_drain_ms", layoutMs}, {"brush_labels_requested", translations.brushLabels.load()}};
	}
	translations.expanded = false; app.setLayoutDirection(Qt::LeftToRight);
	list.refreshHidden([](auto ref) { return ref.kind == LevelMapSelectionKind::Entity && ref.objectId == 51; });
	const auto entity = list.model()->index(list.rowForSelector("entity:51"), 0);
	expect(entity.data(Qt::DisplayRole).toString().count(QLatin1Char('\n')) == 1 &&
		entity.data(Qt::ToolTipRole).toString().contains("first\nsecond") &&
		entity.data(Qt::AccessibleTextRole).toString().contains("hidden"), "compact display preserves complete accessible/tooltip fields and hidden state");
	const auto* accessible = QAccessible::queryAccessibleInterface(&list);
	expect(accessible && accessible->role() == QAccessible::List && list.focusPolicy() != Qt::NoFocus, "native accessible list and focus");

	const auto query = [&](const QString& text) {
		list.setFilterText(text);
		expect(until([&] { return !list.isFiltering(); }), "filter completion");
	};
	const auto selected = list.selectedReferences();
	query("class=light light>200");
	expect(kept(list) == QStringList{"entity:51"} && list.selectedReferences() == selected, "filter uses shared property semantics without editing selection");
	query("hidden"); expect(kept(list) == QStringList{"entity:51"}, "hidden marker remains searchable");
	query("brush:17"); expect(kept(list) == QStringList{"brush:17"}, "selector query retains sparse identity");
	query("claass=light"); expect(list.matchingCount() == 0 && list.knownQueryKeys().contains("class"), "unknown key diagnostics retain shared index keys");
	query("Entity 0 / 6"); expect(list.matchingCount() == 50000, "plain text keeps exact phrase semantics");
	query({});

	// Observe work through translated labels rather than a sleep assumption.
	translations.workerBrushLabels = 0;
	list.setFilterText("no-match-in-a-brush");
	expect(until([&] { return translations.workerBrushLabels > 100; }) && list.isFiltering(), "large query has started on the worker");
	elapsed.restart(); list.setFilterText({}); const double cancelMs = elapsed.nsecsElapsed() / 1e6;
	expect(!list.isFiltering() && list.matchingCount() == 50003 && list.selectedReferences() == selected, "clear cancels immediately and preserves selection");
	drain(100);
	expect(list.matchingCount() == 50003, "cancelled filter cannot publish a late result");
	list.setFilterText("class=light"); list.setFilterText("brush:17"); list.setFilterText("patch:17");
	expect(until([&] { return !list.isFiltering(); }) && kept(list) == QStringList{"patch:17"}, "rapid edits coalesce to the latest query");
	list.setFilterText("class=light");
	LevelMapDocument replacement; replacement.format = LevelMapFormat::QuakeMap;
	light.id = 88; replacement.entities.append(light); list.setDocument(replacement);
	expect(until([&] { return !list.isFiltering(); }) && kept(list) == QStringList{"entity:88"}, "source replacement invalidates old filter/cache/row identities");
	expect(initial.rows.size() == 50003 && initial.document.brushes.last().id == lastId, "worker snapshot remains independent of later models");

	int probes = 0;
	expect(levelMapQueryProperties(source, [&] { return ++probes > 50; }).isEmpty() && probes > 50, "query index cancellation discards partial properties");
	LevelMapDocument edited;
	expect(tests::createGeometryFixture(3, &edited, &error), "history fixture");
	const QVector<LevelMapSelectionRef> previous{{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 2}};
	setLevelMapSelection(&edited, previous);
	LevelBrushPrimitiveRequest primitive; primitive.texture = "studio/history";
	expect(addLevelMapBrushPrimitive(&edited, primitive, nullptr, &error) && undoLevelMapEdit(&edited, &error) &&
		edited.selection == previous && edited.selectedObjectId == 2 && redoLevelMapEdit(&edited, &error) &&
		edited.selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::QuakeBrush, 3}}, "brush undo/redo restores the exact selection set and primary");
	LevelMapCreateRequest doomRequest; doomRequest.game = QStringLiteral("doom");
	LevelMapDocument doom; expect(createLevelMap(doomRequest, &doom, &error), "Doom fixture");
	list.setFilterText({}); list.setDocument(doom);
	expect(list.rowForSelector("entity:0") == -1 && list.rowForSelector("thing:0") >= 0 && list.rowForSelector("sector:0") >= 0,
		"Doom things are not duplicated through their entity mirrors");
	query("kind=sector"); expect(list.matchingCount() == doom.doomSectors.size(), "Doom filter semantics");
	list.setDocument({});
	expect(list.model()->rowCount() == 1 && list.objectModel()->objectCount() == 0 && list.model()->index(0, 0).flags() == Qt::NoItemFlags,
		"empty state remains a nonselectable accessible note");
	std::cout << QJsonDocument(QJsonObject{{"objects", 50003}, {"reset_ms", resetMs}, {"cancel_return_ms", cancelMs}, {"layouts", layouts}})
		.toJson(QJsonDocument::Compact).constData() << '\n';
	app.removeTranslator(&translations);
	return ok ? 0 : 1;
}
