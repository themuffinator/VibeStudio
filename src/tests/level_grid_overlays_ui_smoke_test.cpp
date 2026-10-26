// Show > Grid Coordinates and Show > Block Grid in the Levels 2D views
// (NetRadiant Custom's Show Coordinates and Show Blocks):
//   - the shared grid helpers: coordinate text, label placement clear of the
//     status tags and of each other, left-to-right numbers on the leading edge
//     in right-to-left layouts, block lines thinned on real boundaries and kept
//     inside a region, and the grid image with and without its lines;
//   - the overlay worker drawing the block grid into the grid image;
//   - a MapViewport: block lines from worldspawn _blocksize in each projection,
//     the region clip, the coordinate labels and the accessible description;
//   - the shell under the NetRadiant Custom and TrenchBroom profiles at 100% and
//     at 200% with high contrast, expanded text and right-to-left layout: the
//     checkable commands, Show menu items and View tab boxes kept in step, every
//     plan pane following, status messages, palette entries, no default keys,
//     persistence in a new shell, a Doom map hiding the block grid, and the map,
//     its revision and undo history never changing.
// Owned offscreen widgets and semantic Qt calls only.

#include "app/application_shell.h"
#include "app/map_grid.h"
#include "app/map_viewport.h"
#include "app/map_viewport_overlay.h"
#include "app/studio_theme.h"
#include "app/viewport_hud.h"
#include "core/level_block_grid.h"
#include "core/level_document.h"
#include "core/level_map.h"
#include "core/studio_semantics.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QMenu>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <tuple>
#include <utility>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label;
		if (!detail.isEmpty()) {
			std::cerr << ": " << qPrintable(detail);
		}
		std::cerr << std::endl;
	}
	return value;
}

QElapsedTimer runClock;

// Progress with elapsed seconds, so a slow run shows where the time went.
void mark(const char* step)
{
	std::cout << "  " << qPrintable(phase) << ": " << step << " at " << runClock.elapsed() / 1000.0 << " s" << std::endl;
}

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

void settle()
{
	QEventLoop loop;
	QTimer::singleShot(40, &loop, &QEventLoop::quit);
	loop.exec();
}

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

QByteArray mapText(const QByteArray& blocksize)
{
	// A 128-unit cube at the origin, with the block size on worldspawn.
	return QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n\"_blocksize\" \"") + blocksize
		+ QByteArrayLiteral("\"\n{\n"
							"( -64 -64 -64 ) ( -64 -63 -64 ) ( -64 -64 -63 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( 64 -64 -64 ) ( 64 -64 -63 ) ( 64 -63 -64 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( -64 -64 -64 ) ( -64 -64 -63 ) ( -63 -64 -64 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( -64 64 -64 ) ( -63 64 -64 ) ( -64 64 -63 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( -64 -64 -64 ) ( -63 -64 -64 ) ( -64 -63 -64 ) base/wall 0 0 0 1 1 0 0 0\n"
							"( -64 -64 64 ) ( -64 -63 64 ) ( -63 -64 64 ) base/wall 0 0 0 1 1 0 0 0\n"
							"}\n}\n");
}

LevelMapDocument load(const QByteArray& blocksize)
{
	LevelMapLoadRequest request;
	request.path = QStringLiteral("grid.map");
	request.engineHint = QStringLiteral("idtech3");
	LevelMapDocument document;
	QString error;
	expect(loadLevelMapBytes(request, mapText(blocksize), &document, &error), "fixture loads", error);
	return document;
}

QImage renderOf(QWidget& view)
{
	QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	view.render(&image);
	return image;
}

// Whether any pixel of the one-pixel column at `x` (or its neighbours, for an
// antialiased two-pixel line) differs between the images from `from` to `to`.
bool columnDiffers(const QImage& a, const QImage& b, int x, int from, int to)
{
	for (int column = x - 1; column <= x + 1; ++column) {
		for (int y = std::max(0, from); y < std::min({to, a.height(), b.height()}); ++y) {
			if (a.pixel(column, y) != b.pixel(column, y)) {
				return true;
			}
		}
	}
	return false;
}

bool overlaps(const QVector<MapGridCoordinateLabel>& labels)
{
	for (qsizetype i = 0; i < labels.size(); ++i) {
		for (qsizetype j = i + 1; j < labels.size(); ++j) {
			if (labels.at(i).bounds.intersects(labels.at(j).bounds)) {
				return true;
			}
		}
	}
	return false;
}

void gridHelpers()
{
	phase = QStringLiteral("grid helpers");
	const std::tuple<double, double, const char*> texts[] = {
		{-128, 64, "-128"}, {0, 64, "0"}, {-0.0, 1, "0"}, {0.375, 0.125, "0.375"}, {0.5, 0.125, "0.5"}, {1, 0.25, "1"}, {-2.5, 0.5, "-2.5"},
		{65536, 1024, "65536"}, {-0.0001, 0.125, "0"}};
	for (const auto& [value, step, text] : texts) {
		expect(mapGridCoordinateText(value, step) == QString::fromLatin1(text), "coordinate text", mapGridCoordinateText(value, step));
	}

	MapGridView view;
	view.viewport = QSize(800, 600);
	view.center = QPointF(0, 0);
	view.zoom = 0.5;
	view.units = 64;
	expect(mapBlockGridLines(view).isEmpty(), "no block size, no block lines");
	view.blockX = 512;
	view.blockY = 512;
	// x from -800 to 800 and y from -600 to 600: three boundaries a side.
	auto lines = mapBlockGridLines(view);
	expect(lines.size() == 6, "block lines in view", QString::number(lines.size()));
	view.blockBounds = QRectF(QPointF(0, 0), QPointF(1000, 1000));
	lines = mapBlockGridLines(view);
	bool clipped = lines.size() == 4;
	for (const QLineF& line : lines) {
		// The region's corner at the origin is at (400, 300); everything stays
		// up and to the right of it.
		clipped = clipped && std::min(line.x1(), line.x2()) >= 399.5 && std::max(line.y1(), line.y2()) <= 300.5;
	}
	expect(clipped, "block lines stay inside the region", QString::number(lines.size()));
	view.blockBounds = QRectF(QPointF(5000, 5000), QPointF(6000, 6000));
	expect(mapBlockGridLines(view).isEmpty(), "a region out of view has no block lines");
	view.clipBlocks = true;
	view.blockBounds = QRectF(QPointF(0, -512), QPointF(0, 512));
	lines = mapBlockGridLines(view);
	expect(!lines.isEmpty() && std::all_of(lines.cbegin(), lines.cend(), [](const QLineF& line) {
		return line.x1() == 400 && line.x2() == 400 && line.y1() >= 44 && line.y2() <= 556;
	}), "a flat region clips block lines instead of filling the view");
	view.blockBounds = QRectF(QPointF(5000, 0), QPointF(5000, 512));
	expect(mapBlockGridLines(view).isEmpty(), "a flat region outside the view has no block lines");
	view.blockBounds = QRectF();
	MapGridView unclipped = view;
	unclipped.clipBlocks = false;
	expect(!sameMapGridView(view, unclipped), "the grid cache distinguishes a point region from no region");
	view.clipBlocks = false;
	view.blockBounds = QRectF();
	view.blockY = 0;
	expect(mapBlockGridLines(view).size() == 3, "an unsplit axis draws no lines");
	view.zoom = 0.0005;
	view.blockX = 256;
	view.blockY = 256;
	lines = mapBlockGridLines(view);
	bool real = !lines.isEmpty() && lines.size() <= 2048;
	for (const QLineF& line : lines) {
		const double world = line.x1() == line.x2() ? (line.x1() - 400) / view.zoom : (300 - line.y1()) / view.zoom;
		real = real && std::abs(std::remainder(world, 256.0)) < 1e-3;
	}
	expect(real, "zoomed out, the thinned lines are still boundaries", QString::number(lines.size()));

	// Image: the dashed block lines over the grid, or alone with the grid off.
	view.zoom = 0.5;
	view.blockX = 512;
	view.blockY = 0;
	view.minor = qRgb(40, 44, 52);
	view.major = qRgb(58, 64, 76);
	view.axis = qRgb(96, 108, 128);
	view.block = qRgb(74, 120, 194);
	MapGridFrame withBlocks;
	expect(renderMapGrid(view, &withBlocks), "grid image with blocks");
	MapGridView plain = view;
	plain.blockX = 0;
	expect(!sameMapGridView(plain, view), "block size is part of the image key");
	MapGridFrame without;
	renderMapGrid(plain, &without);
	// x = 512 is at 656 px; x = 96 (no line) at 448 px.
	expect(columnDiffers(withBlocks.image, without.image, 656, 100, 160) && !columnDiffers(withBlocks.image, without.image, 448, 0, 600),
		"block lines change only their own pixels");
	MapGridView alone = view;
	alone.lines = false;
	MapGridFrame blocksOnly;
	renderMapGrid(alone, &blocksOnly);
	int opaque = 0;
	int clear = 0;
	for (int y = 0; y < 600; ++y) {
		(qAlpha(blocksOnly.image.pixel(656, y)) > 0 ? opaque : clear) += 1;
	}
	bool gridless = true;
	for (int y = 0; y < 600; ++y) {
		gridless = gridless && qAlpha(blocksOnly.image.pixel(432, y)) == 0;
	}
	expect(gridless && opaque > 200 && clear > 100, "with Grid off only the dashed block lines are drawn",
		QStringLiteral("%1 opaque, %2 clear").arg(opaque).arg(clear));

	// Labels: clear of obstacles and each other, numbers on whole steps.
	const QFont font(QStringLiteral("Segoe UI"), 9);
	const QFontMetricsF metrics(font);
	MapGridView labelled;
	labelled.viewport = QSize(640, 480);
	labelled.zoom = 1;
	labelled.units = 16;
	const QRectF tag(8, 8, 300, 24);
	for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
		const QRectF obstacle = direction == Qt::RightToLeft ? QRectF(640 - 8 - 300, 8, 300, 24) : tag;
		const MapGridCoordinateLayout layout = mapGridCoordinateLabels(labelled, metrics, direction, {obstacle});
		int across = 0;
		int up = 0;
		bool placed = layout.acrossStep >= 16 && layout.upStep >= 32;
		for (const MapGridCoordinateLabel& label : layout.labels) {
			const double step = label.across ? layout.acrossStep : layout.upStep;
			placed = placed && !label.bounds.intersects(obstacle) && QRectF(0, 0, 640, 480).contains(label.bounds)
				&& std::abs(std::remainder(label.value, step)) < 1e-9 && label.text == mapGridCoordinateText(label.value, step);
			if (label.across) {
				++across;
				placed = placed && std::abs(label.bounds.bottom() - 476) < 0.5
					&& std::abs(label.bounds.center().x() - (320 + label.value)) < 0.5;
			} else {
				++up;
				placed = placed && (direction == Qt::RightToLeft ? std::abs(label.bounds.right() - 636) < 0.5 : std::abs(label.bounds.left() - 4) < 0.5)
					&& std::abs(label.bounds.center().y() - (240 - label.value)) < 0.5;
			}
		}
		expect(across >= 5 && up >= 5 && placed && !overlaps(layout.labels), "labels along the bottom and leading edges",
			QStringLiteral("%1 across, %2 up").arg(across).arg(up));
		bool negative = false;
		for (const MapGridCoordinateLabel& label : layout.labels) {
			negative = negative || (label.value < 0 && label.text.startsWith(QLatin1Char('-')));
		}
		expect(negative, "negative coordinates start with their sign");
	}
	labelled.units = 0.125;
	labelled.zoom = 256;
	const MapGridCoordinateLayout fine = mapGridCoordinateLabels(labelled, metrics, Qt::LeftToRight);
	bool fractions = !fine.labels.isEmpty();
	for (const MapGridCoordinateLabel& label : fine.labels) {
		fractions = fractions && std::abs(label.text.toDouble() - label.value) < 1e-9;
	}
	expect(fractions && fine.acrossStep < 1 && fine.upStep < 1 && !overlaps(fine.labels), "fractional grids print their fractions",
		QStringLiteral("%1 / %2").arg(fine.acrossStep).arg(fine.upStep));
	labelled.viewport = QSize(40, 30);
	expect(mapGridCoordinateLabels(labelled, metrics, Qt::LeftToRight).labels.isEmpty(), "a tiny pane has no labels");
}

void overlayWorker()
{
	phase = QStringLiteral("overlay worker");
	MapViewportOverlayRequest request;
	request.key.view.viewport = QSize(400, 300);
	request.key.view.zoom = 0.25;
	request.key.view.units = 64;
	request.key.view.blockX = 512;
	request.key.view.blockY = 1024;
	request.key.view.block = qRgb(200, 100, 50);
	request.key.view.lines = false;
	request.key.showGrid = true;
	MapViewportOverlayResult result;
	MapGridFrame expected;
	expect(renderMapViewportOverlays(request, &result) && result.ready && renderMapGrid(request.key.view, &expected)
			&& !result.grid.image.isNull() && result.grid.image == expected.image,
		"the worker draws the block grid into the grid image");
}

void viewport()
{
	phase = QStringLiteral("viewport");
	MapViewport view;
	view.resize(900, 700);
	view.setShowLabels(false);
	view.setDocument(load("512 512 0"));
	view.restoreNavigationState({0, {0, 0}, 0.5});
	expect(!view.showBlockGrid() && !view.drawsBlockGrid() && !view.showGridCoordinates() && view.gridCoordinateLabels().labels.isEmpty(),
		"both overlays start off");
	const LevelMapBlockSize blocks = view.blockGridSize();
	expect(blocks.along(0) == 512 && blocks.along(1) == 512 && blocks.along(2) == 0 && blocks.key == QStringLiteral("_blocksize"), "block size from worldspawn");
	const QImage off = renderOf(view);
	view.setShowBlockGrid(true);
	expect(view.drawsBlockGrid() && view.accessibleDescription().contains(QStringLiteral("every 512 units across this view and every 512 units up it")),
		"block grid drawn and described", view.accessibleDescription());
	const QImage on = renderOf(view);
	// x = 512 is drawn at 706 px; x = 96 at 498 px has no line. Rows 150 to
	// 250 are clear of the status tags and the cube.
	expect(columnDiffers(on, off, 706, 150, 250) && !columnDiffers(on, off, 498, 150, 250), "block lines in the view");
	view.setShowGrid(false);
	const QImage alone = renderOf(view);
	view.setShowBlockGrid(false);
	const QImage nothing = renderOf(view);
	view.setShowBlockGrid(true);
	expect(columnDiffers(alone, nothing, 706, 150, 250) && !columnDiffers(alone, nothing, 498 + 16, 150, 250),
		"the block grid stays with the grid hidden");
	view.setShowGrid(true);
	view.setRegionBox({-256, -256, -256, true}, {256, 256, 256, true});
	const QImage regionOn = renderOf(view);
	view.setShowBlockGrid(false);
	const QImage regionOff = renderOf(view);
	view.setShowBlockGrid(true);
	// The region spans 322 to 578 px each way; x = 0 is at 450 px.
	expect(!columnDiffers(regionOn, regionOff, 706, 0, 700) && columnDiffers(regionOn, regionOff, 450, 250, 310)
			&& !columnDiffers(regionOn, regionOff, 450, 120, 218),
		"block lines are kept inside the region");
	view.setRegionBox();
	view.setProjection(MapViewportProjection::FrontXZ);
	expect(view.drawsBlockGrid() && view.accessibleDescription().contains(QStringLiteral("every 512 units across this view.")),
		"front view: z is not split", view.accessibleDescription());
	view.setDocument(load("0 0 256"));
	view.setProjection(MapViewportProjection::TopXY);
	expect(view.showBlockGrid() && !view.drawsBlockGrid() && !view.accessibleDescription().contains(QStringLiteral("block grid")),
		"top view: neither x nor y is split");
	view.setProjection(MapViewportProjection::SideZY);
	expect(view.drawsBlockGrid() && view.accessibleDescription().contains(QStringLiteral("every 256 units up this view.")), "side view: z split",
		view.accessibleDescription());
	// An edit to worldspawn reaches the drawn blocks without reopening the map.
	view.updateDocument(load("128"));
	expect(view.blockGridSize().along(1) == 128
			&& view.accessibleDescription().contains(QStringLiteral("every 128 units across this view and every 128 units up it")),
		"an edited _blocksize is read again", view.accessibleDescription());
	view.setShowBlockGrid(false);

	view.setProjection(MapViewportProjection::TopXY);
	view.setDocument(load("1024"));
	view.restoreNavigationState({0, {0, 0}, 0.5});
	const QFontMetricsF metrics(viewportHudFont(view.font()), &view);
	const double hudBottom = 8 + std::ceil(metrics.height()) + 6;
	for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
		view.setLayoutDirection(direction);
		const QImage before = renderOf(view);
		view.setShowGridCoordinates(true);
		const MapGridCoordinateLayout layout = view.gridCoordinateLabels();
		bool clear = layout.labels.size() >= 8 && !overlaps(layout.labels);
		int up = 0;
		for (const MapGridCoordinateLabel& label : layout.labels) {
			if (!label.across) {
				++up;
				clear = clear && label.bounds.top() >= hudBottom
					&& (direction == Qt::RightToLeft ? label.bounds.right() > 850 : label.bounds.left() < 50);
			}
		}
		expect(clear && up >= 3, "labels clear of the status tags on the leading edge", QString::number(layout.labels.size()));
		expect(view.accessibleDescription().contains(QStringLiteral("Grid coordinates label the lines across from")),
			"coordinates described", view.accessibleDescription());
		const QImage after = renderOf(view);
		int changed = 0;
		for (int x = 0; x < after.width(); x += 2) {
			changed += after.pixel(x, 690) != before.pixel(x, 690) ? 1 : 0;
		}
		expect(changed > 0, "labels are painted along the bottom");
		view.setShowGridCoordinates(false);
	}
	view.setLayoutDirection(Qt::LeftToRight);
	// A coarser grid labels coarser lines, and the description follows it.
	view.setShowGridCoordinates(true);
	view.setGridSize(512);
	expect(view.accessibleDescription().contains(QStringLiteral("across from -512 to 512 and up from -512 to 512")),
		"the description follows the grid size", view.accessibleDescription());
	view.setGridSize(64);
	const QString beforeNavigation = view.accessibleDescription();
	view.restoreNavigationState({0, {4096, 8192}, 0.5});
	expect(view.accessibleDescription() != beforeNavigation && view.accessibleDescription().contains(QStringLiteral("Grid coordinates label")),
		"the coordinate description follows a changed view centre", view.accessibleDescription());
	const QString beforeFit = view.accessibleDescription();
	view.zoomToFit();
	expect(view.accessibleDescription() != beforeFit, "fit refreshes the coordinate description");
	view.restoreNavigationState({0, {0, 0}, 0.5});
	const QString beforeResize = view.accessibleDescription();
	view.resize(600, 400);
	renderOf(view); // Deliver the pending resize of this hidden offscreen widget.
	expect(view.accessibleDescription() != beforeResize, "resize refreshes the coordinate description");
	view.setShowGridCoordinates(false);
	LevelMapDocument doom;
	doom.format = LevelMapFormat::DoomWad;
	view.setShowBlockGrid(true);
	view.setDocument(doom);
	expect(!view.drawsBlockGrid(), "Doom maps draw no block grid");
}

void shells(QApplication& app, const QString& path, const QString& wadPath, StudioSettings& settings)
{
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (const int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Level grid overlays UI " << qPrintable(phase) << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
			settings.setShellLayoutState(QStringLiteral("levelMapShow"), {});
			settings.setTextScalePercent(scale);
			const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
			settings.setTheme(theme);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(scale == 100 ? 1700 : 2600, scale == 100 ? 1050 : 1800);
				shell.show();
				mark("shell shown");
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				QAction* coordinatesCommand = action("map.toggleGridCoordinates");
				QAction* blocksCommand = action("map.toggleBlockGrid");
				QAction* coordinatesItem = action("levelMapShowCoordinates");
				QAction* blocksItem = action("levelMapShowBlockGrid");
				auto* showMenu = shell.findChild<QMenu*>(QStringLiteral("levelMapShowMenu"));
				if (!expect(coordinatesCommand && blocksCommand && coordinatesItem && blocksItem && showMenu, "commands and Show menu items exist")) {
					return;
				}
				shell.openPathFromCommandLine(path);
				action("shell.mode.levels")->trigger();
				action("map.layout.four-views")->trigger();
				auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
				auto* side = shell.findChild<MapViewport*>(QStringLiteral("mapViewport2"));
				if (!expect(top && front && side, "plan panes exist")
					|| !expect(until([&] { return shell.levelDocument().sourcePath == path; }), "map loaded")) {
					return;
				}
				settle();
				mark("map loaded");
				const quint64 revision = shell.levelDocument().revision;
				const qsizetype undo = shell.levelDocument().undoStack.size();
				const QString editState = shell.levelDocument().editState;
				for (QAction* command : {coordinatesCommand, blocksCommand}) {
					expect(command->isCheckable() && !command->isChecked() && command->isEnabled() && !command->statusTip().isEmpty()
							&& command->shortcuts().isEmpty(),
						"checkable command, off, with no default key as in NetRadiant Custom", command->objectName());
					bool inMenu = false;
					for (QObject* owner : command->associatedObjects()) {
						inMenu = inMenu || qobject_cast<QMenu*>(owner);
					}
					expect(inMenu, "the View menu offers the command", command->objectName());
					CommandPaletteEntry entry;
					expect(commandPaletteEntryForCommandId(command->objectName(), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(),
						"palette entry", command->objectName());
				}
				for (QAction* item : {coordinatesItem, blocksItem}) {
					expect(showMenu->actions().contains(item) && item->isCheckable() && !item->isChecked() && item->isVisible() && !item->toolTip().isEmpty(),
						"Show menu item", item->objectName());
				}
				const auto box = [&](const char* name) { return shell.findChild<QCheckBox*>(QString::fromLatin1(name)); };
				QCheckBox* coordinatesBox = box("levelViewShowCoordinates");
				QCheckBox* blocksBox = box("levelViewShowBlockGrid");
				for (QCheckBox* check : {coordinatesBox, blocksBox}) {
					expect(check && !check->isChecked() && !check->text().isEmpty() && !check->accessibleDescription().isEmpty()
							&& check->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(check)
							&& check->layoutDirection() == (scale == 200 ? Qt::RightToLeft : Qt::LeftToRight),
						"View tab box", check ? check->objectName() : QString());
				}
				if (!coordinatesBox || !blocksBox) {
					return;
				}
				const auto panes = [&](const std::function<bool(MapViewport*)>& test) { return test(top) && test(front) && test(side); };

				// Grid Coordinates from the command, the Show menu and the View tab.
				shell.statusBar()->clearMessage();
				coordinatesCommand->trigger();
				settle();
				expect(coordinatesItem->isChecked() && coordinatesCommand->isChecked() && coordinatesBox->isChecked()
						&& panes([](MapViewport* view) { return view->showGridCoordinates() && !view->gridCoordinateLabels().labels.isEmpty(); }),
					"command turns coordinates on in every pane");
				expect(!shell.statusBar()->currentMessage().isEmpty(), "status message", shell.statusBar()->currentMessage());
				coordinatesItem->trigger();
				settle();
				expect(!coordinatesCommand->isChecked() && !coordinatesBox->isChecked() && panes([](MapViewport* view) { return !view->showGridCoordinates(); }),
					"the Show menu item turns them off and the command follows");
				coordinatesBox->setChecked(true);
				settle();
				expect(coordinatesItem->isChecked() && coordinatesCommand->isChecked() && top->showGridCoordinates(), "the View tab box turns them on");

				mark("coordinates checked");
				// Block Grid.
				blocksCommand->trigger();
				settle();
				expect(blocksItem->isChecked() && blocksCommand->isChecked() && blocksBox->isChecked()
						&& panes([](MapViewport* view) { return view->showBlockGrid() && view->drawsBlockGrid(); }),
					"command turns the block grid on in every pane");
				const QString message = shell.statusBar()->currentMessage();
				expect(message.contains(QStringLiteral("512")) && message.contains(QStringLiteral("_blocksize")), "status message names the size and key", message);
				expect(top->accessibleDescription().contains(QStringLiteral("512")), "pane description mentions the blocks", top->accessibleDescription());
				expect(shell.levelDocument().revision == revision && shell.levelDocument().undoStack.size() == undo
						&& shell.levelDocument().editState == editState,
					"the map and its undo history are untouched");
				const QByteArray saved = settings.shellLayoutState(QStringLiteral("levelMapShow"));
				expect(saved.contains("levelMapShowCoordinates") && saved.contains("levelMapShowBlockGrid"), "both choices are saved",
					QString::fromUtf8(saved));
				mark("block grid checked");
			}
			// A new shell restores both, and a Doom map hides the block grid.
			// Shells are slow to build and close, so this runs in the last
			// pass only, which has the expanded right-to-left layout.
			if (QLatin1String(profile) == QLatin1String("trenchbroom") && scale == 200) {
				ApplicationShell shell;
				shell.resize(scale == 100 ? 1700 : 2600, scale == 100 ? 1050 : 1800);
				shell.show();
				mark("shell shown");
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				QAction* coordinatesCommand = action("map.toggleGridCoordinates");
				QAction* blocksCommand = action("map.toggleBlockGrid");
				QAction* blocksItem = action("levelMapShowBlockGrid");
				auto* blocksBox = shell.findChild<QCheckBox*>(QStringLiteral("levelViewShowBlockGrid"));
				if (!expect(coordinatesCommand && blocksCommand && blocksItem && blocksBox, "second shell")) {
					return;
				}
				expect(coordinatesCommand->isChecked() && blocksCommand->isChecked() && blocksItem->isChecked(), "choices persist");
				shell.openPathFromCommandLine(wadPath);
				action("shell.mode.levels")->trigger();
				if (expect(until([&] { return shell.levelDocument().format == LevelMapFormat::DoomWad; }), "Doom map loaded")) {
					settle();
					mark("Doom map loaded");
					auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
					expect(!blocksItem->isVisible() && !blocksCommand->isEnabled() && blocksBox->isHidden() && top && !top->drawsBlockGrid()
							&& coordinatesCommand->isEnabled(),
						"a Doom map hides the block grid");
				}
				coordinatesCommand->trigger();
				blocksItem->trigger();
				settle();
				expect(!coordinatesCommand->isChecked() && !blocksCommand->isChecked(), "turned off again");
				mark("second shell done");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	runClock.start();
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	const QString path = temp.filePath(QStringLiteral("blocks.map"));
	const QString wadPath = temp.filePath(QStringLiteral("room.wad"));
	QFile file(path);
	const QByteArray bytes = mapText("512 512 0");
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "map fixture")) {
		return 1;
	}
	file.close();
	// A Doom starter room, for the hidden block grid.
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	QString error;
	if (!expect(createLevelMap(doomRequest, &doom, &error) && saveLevelMapAs(doom, wadPath).succeeded(), "Doom fixture", error)) {
		return 1;
	}
	gridHelpers();
	overlayWorker();
	viewport();
	mark("helpers, worker and viewport done");
	shells(app, path, wadPath, settings);
	QFile source(path);
	phase = QStringLiteral("end");
	expect(source.open(QIODevice::ReadOnly) && source.readAll() == bytes, "the map file is never written");
	if (ok) {
		std::cout << "Level grid overlays UI: helpers, worker, viewport and shell passed." << std::endl;
	}
	return ok ? 0 : 1;
}
