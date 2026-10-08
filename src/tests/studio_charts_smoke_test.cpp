#include "app/studio_charts.h"
#include "app/studio_theme.h"

#include <QApplication>
#include <QDir>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPair>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}

QImage render(QWidget& widget)
{
	widget.ensurePolished();
	QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent); widget.render(&image);
	return image;
}

// Paints a widget the way the screen does: grab(), like on-screen painting,
// starts each QPainter in the application's layout direction, where render()
// into an image starts it in the widget's and so would hide a chart that never
// sets its painter's direction. The direction checks run with a left-to-right
// application.
QImage grabbed(QWidget& widget)
{
	widget.ensurePolished();
	return widget.grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

bool capture(const QImage& image, const QString& name)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
	return root.isEmpty() || image.save(QDir(root).filePath(name + QStringLiteral(".png")));
}

// Measure the first painted bar at a column clear of its centered label. This
// checks the actual raster, independently of the chart's layout constants.
int paintedBarHeight(const QImage& image)
{
	const auto background = image.pixel(1, 1);
	int first = -1;
	for (int y = 0; y < image.height(); ++y) {
		if (image.pixel(12, y) != background) {
			if (first < 0) { first = y; }
		} else if (first >= 0) { return y - first; }
	}
	return 0;
}

bool readablePercentageField(const QImage& image)
{
	int top = 0;
	while (top < image.height() && image.pixel(12, top) == image.pixel(1, 1)) { ++top; }
	const int bottom = top + paintedBarHeight(image);
	bool dark = false, light = false;
	for (int y = top + 1; y < bottom - 1; ++y) {
		for (int x = image.width() / 4; x < image.width() * 3 / 4; ++x) {
			const auto pixel = image.pixelColor(x, y);
			dark |= pixel.red() < 20 && pixel.green() < 20 && pixel.blue() < 20;
			light |= pixel.red() > 235 && pixel.green() > 235 && pixel.blue() > 235;
		}
	}
	return dark && light;
}

// The first and last marked column or row; -1 when none is.
struct Extent {
	int first = -1;
	int last = -1;
};

// Marks the columns whose rows from `top` to `bottom` hold `color` exactly, or,
// when `match` is false, anything other than `color`.
QVector<bool> markedColumns(const QImage& image, int top, int bottom, QRgb color, bool match = true)
{
	QVector<bool> columns(image.width(), false);
	for (int y = std::max(0, top); y <= std::min(bottom, image.height() - 1); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			columns[x] = columns[x] || ((image.pixel(x, y) == color) == match);
		}
	}
	return columns;
}

Extent columnExtent(const QVector<bool>& columns)
{
	Extent extent;
	for (int x = 0; x < columns.size(); ++x) {
		if (columns.at(x)) {
			extent.first = extent.first < 0 ? x : extent.first;
			extent.last = x;
		}
	}
	return extent;
}

// The runs of marked columns, left to right. Columns at most `join` apart
// share a run, so a glyph whose strokes leave a gap is still one mark.
QVector<Extent> markedRuns(const QVector<bool>& columns, int join = 1)
{
	QVector<Extent> runs;
	for (int x = 0; x < columns.size(); ++x) {
		if (!columns.at(x)) { continue; }
		if (!runs.isEmpty() && x - runs.last().last <= join) { runs.last().last = x; } else { runs.append({x, x}); }
	}
	return runs;
}

// The bands of adjacent rows that hold `color` anywhere, top to bottom.
QVector<Extent> rowBands(const QImage& image, QRgb color)
{
	QVector<Extent> bands;
	for (int y = 0; y < image.height(); ++y) {
		bool held = false;
		for (int x = 0; x < image.width() && !held; ++x) { held = image.pixel(x, y) == color; }
		if (!held) { continue; }
		if (!bands.isEmpty() && bands.last().last == y - 1) { bands.last().last = y; } else { bands.append({y, y}); }
	}
	return bands;
}

// The rows around (`x`, `y`) that differ from `background` in column `x`.
Extent paintedRows(const QImage& image, int x, int y, QRgb background)
{
	Extent rows;
	if (x < 0 || x >= image.width() || y < 0 || y >= image.height() || image.pixel(x, y) == background) { return rows; }
	rows.first = rows.last = y;
	while (rows.first > 0 && image.pixel(x, rows.first - 1) != background) { --rows.first; }
	while (rows.last + 1 < image.height() && image.pixel(x, rows.last + 1) != background) { ++rows.last; }
	return rows;
}

// The colour most of a region is painted in, such as a box's fill under its
// text.
QRgb commonColor(const QImage& image, const Extent& columns, const Extent& rows)
{
	QHash<QRgb, int> counts;
	for (int y = std::max(0, rows.first); y <= std::min(rows.last, image.height() - 1); ++y) {
		for (int x = std::max(0, columns.first); x <= std::min(columns.last, image.width() - 1); ++x) { ++counts[image.pixel(x, y)]; }
	}
	QRgb common = 0;
	int most = -1;
	for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
		if (it.value() > most) { common = it.key(); most = it.value(); }
	}
	return common;
}

// True when `rtl` is `ltr` reflected across an image `width` columns wide.
bool mirrors(const Extent& ltr, const Extent& rtl, int width, int tolerance)
{
	return ltr.first >= 0 && rtl.first >= 0 && std::abs(rtl.first - (width - 1 - ltr.last)) <= tolerance
		&& std::abs(rtl.last - (width - 1 - ltr.first)) <= tolerance;
}

bool anyPainted(const QVector<bool>& columns, int from, int to)
{
	for (int x = std::max(0, std::min(from, to)); x <= std::min(static_cast<int>(columns.size()) - 1, std::max(from, to)); ++x) {
		if (columns.at(x)) { return true; }
	}
	return false;
}

// True when the columns `edge + step * offset`, for offsets from `from` to
// `to`, include `run` unpainted ones in a row.
bool clearRun(const QVector<bool>& columns, int edge, int step, int from, int to, int run)
{
	int clear = 0;
	for (int offset = from; offset <= to; ++offset) {
		const int x = edge + step * offset;
		clear = x >= 0 && x < columns.size() && columns.at(x) ? 0 : clear + 1;
		if (clear >= run) { return true; }
	}
	return false;
}

// How far into a line of text its widest gap starts, counted from its first
// marked column; -1 for a line without one. For "12.5 s" that gap is the
// space before the unit, so it moves if the line is drawn as "s 12.5".
int widestGapOffset(const QVector<bool>& columns)
{
	const Extent ink = columnExtent(columns);
	int widest = 0, offset = -1, run = 0;
	for (int x = ink.first; ink.first >= 0 && x <= ink.last; ++x) {
		run = columns.at(x) ? 0 : run + 1;
		if (run > widest) {
			widest = run;
			offset = x - run + 1 - ink.first;
		}
	}
	return offset;
}

QPoint centreOf(const Extent& columns, const Extent& rows)
{
	return QPoint((columns.first + columns.last) / 2, (rows.first + rows.last) / 2);
}

// A left-button press at `point`, as a click on a chart delivers it.
void click(QWidget& widget, const QPoint& point)
{
	QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), widget.mapToGlobal(QPointF(point)), Qt::LeftButton, Qt::LeftButton,
		Qt::NoModifier);
	QCoreApplication::sendEvent(&widget, &press);
}

void pressKey(QWidget& widget, Qt::Key key)
{
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
	QCoreApplication::sendEvent(&widget, &press);
}

// Where one timeline render put its parts, found from the raster: the first
// row's state glyph and text line, and the second row's duration fill.
struct TimelineMarks {
	Extent glyph;
	Extent secondFill;
	QVector<bool> firstLine;
	bool found = false;
};

TimelineMarks timelineMarks(const QImage& image, QRgb accent)
{
	// Rows dense with the state colour are duration fills; sparse ones above
	// the first fill are the first row's glyph.
	QVector<int> counts(image.height(), 0);
	for (int y = 0; y < image.height(); ++y) {
		for (int x = 0; x < image.width(); ++x) {
			counts[y] += image.pixel(x, y) == accent ? 1 : 0;
		}
	}
	constexpr int kFillRow = 48;
	QVector<QPair<int, int>> fills;
	for (int y = 0; y < image.height(); ++y) {
		if (counts.at(y) < kFillRow) { continue; }
		if (!fills.isEmpty() && fills.last().second == y - 1) { fills.last().second = y; } else { fills.append({y, y}); }
	}
	TimelineMarks marks;
	if (fills.size() != 2) { return marks; }
	int glyphTop = -1, glyphBottom = -1;
	for (int y = 0; y < fills.first().first; ++y) {
		if (counts.at(y) > 0) {
			glyphTop = glyphTop < 0 ? y : glyphTop;
			glyphBottom = y;
		}
	}
	if (glyphTop < 0) { return marks; }
	marks.glyph = columnExtent(markedColumns(image, glyphTop, glyphBottom, accent));
	marks.secondFill = columnExtent(markedColumns(image, fills.last().first, fills.last().second, accent));
	// The render includes the window background, which the chart's margin
	// leaves showing at its corner.
	marks.firstLine = markedColumns(image, glyphTop - 1, glyphBottom + 1, image.pixel(0, 0), false);
	marks.found = marks.glyph.first >= 0 && marks.secondFill.first >= 0;
	return marks;
}

// In a right-to-left layout a timeline row mirrors its left-to-right twin: the
// state glyph and the title lead from the right, the time trails on the left
// with a clear gap before the title, and the duration bar grows from the right
// end of its track. At 200% text the glyph column widens with its glyphs.
bool checkTimelineDirection(QApplication& app)
{
	const qint64 now = 1700000000000;
	QVector<TimelineEvent> events{
		// Long enough to elide against the time beside it.
		{QStringLiteral("map"),
			QStringLiteral("Level map inspected against every compiler profile, mounted package, and game installation in the project"),
			{}, QStringLiteral("level"), OperationState::Warning, now, 2000},
		{QStringLiteral("scan"), QStringLiteral("Package scan"), {}, QStringLiteral("package"), OperationState::Warning, now - 120000, 1000},
	};
	const QRgb accent = studioStateColor(OperationState::Warning, false, false).rgb();
	bool ok = true;
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, scale));
		TimelineMarks marks[2];
		int width = 0, timeWidth = 0;
		for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
			const bool mirrored = direction == Qt::RightToLeft;
			ActivityTimelineChart chart;
			chart.setLayoutDirection(direction);
			chart.setEvents(events);
			chart.resize(640, chart.sizeHint().height());
			const QImage image = grabbed(chart);
			width = image.width();
			timeWidth = chart.fontMetrics().horizontalAdvance(QStringLiteral("just now"));
			ok &= expect(capture(image, QStringLiteral("timeline-dark-%1-%2").arg(scale).arg(mirrored ? QStringLiteral("rtl") : QStringLiteral("ltr"))),
				"write timeline capture");
			TimelineMarks& found = marks[mirrored ? 1 : 0];
			found = timelineMarks(image, accent);
			if (!expect(found.found, "a timeline render shows the first row's glyph and both duration fills")) {
				return false;
			}
			// The time sits at the trailing end of the first line, its own width
			// and then a clear gap away from the title, which starts beside the
			// glyph without touching it.
			const Extent line = columnExtent(found.firstLine);
			const int timeEdge = mirrored ? line.first : line.last;
			ok &= expect(clearRun(found.firstLine, timeEdge, mirrored ? 1 : -1, timeWidth - 2, timeWidth + 8, 3),
				"a timeline row keeps a gap between its title and its time");
			ok &= expect(mirrored ? anyPainted(found.firstLine, found.glyph.first - 24, found.glyph.first - 3)
					: anyPainted(found.firstLine, found.glyph.last + 3, found.glyph.last + 24),
				"a timeline row's title starts beside its state glyph");
			// A glyph column too narrow for the text size clips the glyph at its
			// edge and leaves the title a column or two away.
			ok &= expect(clearRun(found.firstLine, mirrored ? found.glyph.first : found.glyph.last, mirrored ? -1 : 1, 1, 12, 4),
				"a timeline row's state glyph keeps clear of its title");
			ok &= expect(mirrored ? found.glyph.first > width * 3 / 4 : found.glyph.last < width / 4,
				"a timeline row's state glyph leads the row");
		}
		ok &= expect(mirrors(marks[0].glyph, marks[1].glyph, width, 3), "a right-to-left timeline mirrors the state glyph column");
		ok &= expect(mirrors(marks[0].secondFill, marks[1].secondFill, width, 3),
			"a right-to-left duration bar grows from the right end of its track");
		const Extent ltrLine = columnExtent(marks[0].firstLine), rtlLine = columnExtent(marks[1].firstLine);
		ok &= expect(std::abs(rtlLine.first - (width - 1 - ltrLine.last)) <= 3, "a right-to-left timeline puts the time on the left");
	}
	return ok;
}

// One pipeline stage found on a render: its state glyph, drawn exactly in the
// state colour, and the box around it.
struct StageMarks {
	Extent glyph;
	Extent glyphRows;
	Extent box;
	Extent boxRows;
};

// Finds the first glyph drawn in `accent`, then its box: the run of painted
// columns around the glyph in its rows, and the box's rows at a column inside
// its padding, clear of the rounded corners and the text.
bool findStage(const QImage& image, QRgb accent, StageMarks& marks)
{
	const QVector<Extent> bands = rowBands(image, accent);
	if (bands.isEmpty()) { return false; }
	marks.glyphRows = bands.first();
	const QVector<Extent> glyphs = markedRuns(markedColumns(image, marks.glyphRows.first, marks.glyphRows.last, accent), 4);
	if (glyphs.isEmpty()) { return false; }
	marks.glyph = glyphs.first();
	const QRgb background = image.pixel(0, 0);
	for (const Extent& run : markedRuns(markedColumns(image, marks.glyphRows.first, marks.glyphRows.last, background, false))) {
		if (run.first <= marks.glyph.first && marks.glyph.last <= run.last) { marks.box = run; }
	}
	if (marks.box.first < 0) { return false; }
	marks.boxRows = paintedRows(image, marks.box.first + 7, (marks.glyphRows.first + marks.glyphRows.last) / 2, background);
	return marks.boxRows.first >= 0;
}

// How wide `text` comes out in `color` where nothing elides or clips it,
// counting the columns that hold `color` exactly or, when `exact` is false,
// every column the text touches.
int drawnWidth(const QFont& font, const QString& text, const QColor& color, bool exact)
{
	QImage image(640, 160, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::black);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	painter.setFont(font);
	painter.setPen(color);
	painter.drawText(QRectF(20.0, 0.0, 600.0, 160.0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, text);
	painter.end();
	const Extent extent = columnExtent(exact ? markedColumns(image, 0, image.height() - 1, color.rgb())
		: markedColumns(image, 0, image.height() - 1, qRgb(0, 0, 0), false));
	return extent.first < 0 ? 0 : extent.last - extent.first + 1;
}

// In a right-to-left layout the pipeline mirrors its left-to-right twin: the
// chain runs from the right and its arrows point left, each stage's glyph
// leads from the right edge of its box with the label beside it, the active
// stage's marker sits on that edge, and the title starts at the right, while
// a badge still reads "12.5 s". Clicks land on the stage drawn under them,
// before and after the first paint, and Left steps forward. At 200% text the
// glyph column widens with its glyphs, so a glyph is neither clipped nor
// pressed against its label.
bool checkPipelineDirection(QApplication& app)
{
	const QVector<PipelineStageNode> stages{
		{QStringLiteral("bsp"), QStringLiteral("Build BSP"), {}, OperationState::Running, QStringLiteral("12.5 s")},
		{QStringLiteral("vis"), QStringLiteral("Visibility"), {}, OperationState::Completed},
		{QStringLiteral("light"), QStringLiteral("Lighting"), {}, OperationState::Warning, {}, true},
	};
	bool ok = true;
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, scale));
		const QColor warning = studioStateColor(OperationState::Warning, false, false);
		const QRgb running = studioStateColor(OperationState::Running, false, false).rgb();
		const QRgb completed = studioStateColor(OperationState::Completed, false, false).rgb();
		// Wide enough for the whole chain on one row at either text size.
		const int width = 760 * scale / 100;
		StageMarks firstStage[2], lastStage[2];
		Extent title[2];
		int badgeSpace[2] = {-1, -1};
		bool found[2] = {false, false};
		for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
			const bool mirrored = direction == Qt::RightToLeft;
			const int side = mirrored ? 1 : 0;
			const auto configure = [&](PipelineChart& chart) {
				chart.setLayoutDirection(direction);
				chart.setTitle(QStringLiteral("Pipeline stages"));
				chart.setStages(stages);
				chart.setActiveStageId(QStringLiteral("vis"));
				chart.resize(width, 10);
				chart.resize(width, chart.sizeHint().height());
			};
			PipelineChart chart;
			configure(chart);
			const QImage image = grabbed(chart);
			ok &= expect(capture(image, QStringLiteral("pipeline-dark-%1-%2").arg(scale).arg(mirrored ? QStringLiteral("rtl") : QStringLiteral("ltr"))),
				"write pipeline capture");
			StageMarks& bsp = firstStage[side];
			StageMarks& light = lastStage[side];
			found[side] = findStage(image, running, bsp) && findStage(image, warning.rgb(), light);
			if (!expect(found[side], "a pipeline render shows the first and last stages' glyphs in their boxes")) {
				ok = false;
				continue;
			}
			const QRgb background = image.pixel(0, 0);
			title[side] = columnExtent(markedColumns(image, 0, bsp.boxRows.first - 3, background, false));

			// A glyph leads its box, inset only by the box padding.
			const auto leadingInset = [mirrored](const StageMarks& stage) {
				return mirrored ? stage.box.last - stage.glyph.last : stage.glyph.first - stage.box.first;
			};
			ok &= expect(leadingInset(bsp) <= 12 && leadingInset(light) <= 12, "a pipeline stage's state glyph leads its box");
			QFont glyphFont = chart.font();
			glyphFont.setBold(true);
			ok &= expect(light.glyph.last - light.glyph.first + 1 >= drawnWidth(glyphFont, studioStateGlyph(OperationState::Warning), warning, true) - 2,
				"a pipeline stage's state glyph is drawn whole at every text size");
			// Against the box fill, the glyph keeps a clear gap before its label,
			// which starts close beside it.
			const QVector<bool> line = markedColumns(image, light.glyphRows.first - 1, light.glyphRows.last + 1, commonColor(image, light.box, light.glyphRows), false);
			const int glyphEdge = mirrored ? light.glyph.first : light.glyph.last;
			ok &= expect(clearRun(line, glyphEdge, mirrored ? -1 : 1, 1, 12, 4), "a pipeline stage's state glyph keeps clear of its label");
			ok &= expect(mirrored ? anyPainted(line, glyphEdge - 24, glyphEdge - 3) : anyPainted(line, glyphEdge + 3, glyphEdge + 24),
				"a pipeline stage's label starts beside its state glyph");
			// The first stage's badge, on the box's second line.
			const Extent badgeRows{(bsp.boxRows.first + bsp.boxRows.last + 1) / 2 + 2, bsp.boxRows.last - 3};
			const Extent inside{bsp.box.first + 2, bsp.box.last - 2};
			QVector<bool> badge = markedColumns(image, badgeRows.first, badgeRows.last, commonColor(image, inside, badgeRows), false);
			for (int x = 0; x < badge.size(); ++x) { badge[x] = badge[x] && x >= inside.first && x <= inside.last; }
			badgeSpace[side] = widestGapOffset(badge);

			// Source, three stages, artifact: each arrow's head sits at the end of
			// its gap nearest the next box in the chain, which in a right-to-left
			// render is the box on the left. Rows two and three away from the
			// connector line show only the heads.
			const QVector<Extent> boxes = markedRuns(markedColumns(image, bsp.glyphRows.first, bsp.glyphRows.last, background, false));
			const int connector = (bsp.boxRows.first + bsp.boxRows.last + 1) / 2;
			int pointing = 0;
			for (int index = 1; index < boxes.size(); ++index) {
				const int from = boxes.at(index - 1).last + 2, to = boxes.at(index).first - 2;
				QVector<bool> head(image.width(), false);
				for (const int y : {connector - 3, connector - 2, connector + 2, connector + 3}) {
					for (int x = std::max(0, from); x <= std::min(to, image.width() - 1); ++x) { head[x] = head[x] || image.pixel(x, y) != background; }
				}
				const Extent heads = columnExtent(head);
				const int middle = (from + to) / 2;
				pointing += heads.first >= 0 && (mirrored ? heads.last < middle : heads.first > middle) ? 1 : 0;
			}
			ok &= expect(boxes.size() == 5 && pointing == 4, "every pipeline arrow points at the next box in the chain");

			// The active stage (the middle box either way) carries a full-height
			// marker on its leading edge, beside the outline every box has.
			int leading = 0, trailing = 0;
			if (boxes.size() == 5) {
				const Extent active = boxes.at(2);
				const int top = bsp.boxRows.first + 8, bottom = bsp.boxRows.last - 8;
				for (int x = active.first; x <= active.last; ++x) {
					int count = 0;
					for (int y = top; y <= bottom; ++y) { count += image.pixel(x, y) == completed ? 1 : 0; }
					if (count == bottom - top + 1) { ++((x < (active.first + active.last) / 2) != mirrored ? leading : trailing); }
				}
			}
			ok &= expect(leading > trailing, "the active pipeline stage's marker sits on its leading edge");

			// Clicks resolve through the drawn boxes, and through a fresh layout
			// before the first paint.
			QString activated;
			const auto stageUnder = [&activated](PipelineChart& target, const StageMarks& stage) {
				activated.clear();
				click(target, centreOf(stage.glyph, stage.glyphRows));
				return activated;
			};
			const auto record = [&activated](const QString& id) { activated = id; };
			QObject::connect(&chart, &PipelineChart::stageActivated, record);
			ok &= expect(stageUnder(chart, bsp) == QStringLiteral("bsp") && stageUnder(chart, light) == QStringLiteral("light"),
				"a click lands on the pipeline stage drawn under it");
			PipelineChart unpainted;
			configure(unpainted);
			QObject::connect(&unpainted, &PipelineChart::stageActivated, record);
			ok &= expect(stageUnder(unpainted, bsp) == QStringLiteral("bsp") && stageUnder(unpainted, light) == QStringLiteral("light"),
				"a click before the first paint lands on the stage that will be drawn there");

			// Left and Right move through the stages as drawn.
			QString hovered;
			QObject::connect(&chart, &PipelineChart::hoverChanged, [&hovered](const QString& summary) { hovered = summary; });
			pressKey(chart, Qt::Key_Home);
			pressKey(chart, mirrored ? Qt::Key_Left : Qt::Key_Right);
			const bool forward = hovered == chart.summaryLines().value(1);
			pressKey(chart, mirrored ? Qt::Key_Right : Qt::Key_Left);
			ok &= expect(forward && hovered == chart.summaryLines().value(0), "the arrow keys step through the pipeline in its drawn direction");
		}
		if (found[0] && found[1]) {
			ok &= expect(mirrors(firstStage[0].glyph, firstStage[1].glyph, width, 3) && mirrors(lastStage[0].glyph, lastStage[1].glyph, width, 3),
				"a right-to-left pipeline mirrors each stage's state glyph");
			ok &= expect(mirrors(firstStage[0].box, firstStage[1].box, width, 2) && mirrors(lastStage[0].box, lastStage[1].box, width, 2),
				"a right-to-left pipeline mirrors each stage's box");
			ok &= expect(firstStage[1].box.first > lastStage[1].box.last, "a right-to-left pipeline runs from the right");
			ok &= expect(mirrors(title[0], title[1], width, 3), "a right-to-left pipeline title starts at the right");
			ok &= expect(badgeSpace[0] > 0 && std::abs(badgeSpace[1] - badgeSpace[0]) <= 2,
				"a right-to-left pipeline badge keeps a number before its unit");
		}
	}
	return ok;
}

// Whether the box crossing column `lead` in `rows` shows its label whole: the
// label's ink beside the glyph on its first line is as wide as the unelided
// text's, `wholeWidth`.
bool labelDrawnWhole(const QImage& image, int lead, const Extent& rows, bool mirrored, int wholeWidth)
{
	const Extent band{rows.first + 5, (rows.first + rows.last + 1) / 2 - 2};
	Extent box;
	for (const Extent& run : markedRuns(markedColumns(image, band.first, band.last, image.pixel(0, 0), false))) {
		if (run.first <= lead && lead <= run.last) { box = run; }
	}
	if (box.first < 0) { return false; }
	// Inside the outline, against the fill, the glyph leads and everything
	// after it on the line is the label.
	const Extent inside{box.first + 2, box.last - 2};
	QVector<bool> ink = markedColumns(image, band.first, band.last, commonColor(image, inside, band), false);
	for (int x = 0; x < ink.size(); ++x) { ink[x] = ink[x] && x >= inside.first && x <= inside.last; }
	const QVector<Extent> marks = markedRuns(ink);
	if (marks.size() < 2) { return false; }
	const Extent label = mirrored ? Extent{marks.first().first, marks.at(marks.size() - 2).last} : Extent{marks.at(1).first, marks.last().last};
	return label.last - label.first + 1 >= wholeWidth - 3;
}

// A chain too long for its chart wraps onto more rows. In a right-to-left
// layout each row starts at the right, and the dashed stubs that carry the
// chain across a wrap leave a row towards the left edge and enter the next
// from the right edge, so every row's connector line mirrors its
// left-to-right twin. Boxes sized to their text keep room for the whole
// label beside a glyph column widened for large text.
bool checkWrappedPipelineDirection(QApplication& app)
{
	const QVector<PipelineStageNode> stages{
		{QStringLiteral("bsp"), QStringLiteral("Build BSP"), {}, OperationState::Warning},
		{QStringLiteral("vis"), QStringLiteral("Visibility"), {}, OperationState::Warning},
		{QStringLiteral("light"), QStringLiteral("Lighting"), {}, OperationState::Warning},
	};
	constexpr int kWidth = 400;
	bool ok = true;
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, scale));
		QVector<Extent> connectors[2];
		for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
			const bool mirrored = direction == Qt::RightToLeft;
			PipelineChart chart;
			chart.setLayoutDirection(direction);
			chart.setStages(stages);
			chart.resize(kWidth, 10);
			chart.resize(kWidth, chart.sizeHint().height());
			const QImage image = grabbed(chart);
			ok &= expect(capture(image, QStringLiteral("pipeline-wrapped-dark-%1-%2").arg(scale).arg(mirrored ? QStringLiteral("rtl") : QStringLiteral("ltr"))),
				"write wrapped pipeline capture");
			// Every row starts with a box at its leading margin; a column inside
			// that box's padding crosses one box per row.
			const QRgb background = image.pixel(0, 0);
			const int lead = mirrored ? image.width() - 16 : 15;
			const QFontMetrics metrics(chart.font());
			const int boxHeight = 2 * metrics.height() + 16;
			QVector<Extent>& found = connectors[mirrored ? 1 : 0];
			for (int y = 0; y < image.height(); ++y) {
				const Extent rows = paintedRows(image, lead, y, background);
				if (rows.first < 0) { continue; }
				if (rows.last - rows.first + 1 >= boxHeight - 4) {
					const int line = (rows.first + rows.last + 1) / 2;
					// The first row leads with the source box.
					if (found.isEmpty()) {
						ok &= expect(labelDrawnWhole(image, lead, rows, mirrored, drawnWidth(chart.font(), QStringLiteral("Source"), Qt::white, false)),
							"a pipeline box sized to its text keeps room for the whole label");
					}
					found.append(columnExtent(markedColumns(image, line - 1, line, background, false)));
				}
				y = rows.last;
			}
		}
		bool mirroredRows = connectors[0].size() >= 2 && connectors[0].size() == connectors[1].size();
		for (int row = 0; mirroredRows && row < connectors[0].size(); ++row) {
			mirroredRows = mirrors(connectors[0].at(row), connectors[1].at(row), kWidth, 2);
		}
		ok &= expect(mirroredRows, "a wrapped right-to-left pipeline mirrors every row's connectors and wrap stubs");
	}
	return ok;
}

// The colour a composition slice is drawn in on the dark theme: its state's,
// lightened or darkened by position the way the chart separates neighbours.
QRgb sliceColor(OperationState state, int index)
{
	QColor color = studioStateColor(state, false, false);
	if (index % 3 == 1) {
		color = color.lighter(124);
	} else if (index % 3 == 2) {
		color = color.darker(118);
	}
	return color.rgb();
}

// In a right-to-left layout the composition chart mirrors its left-to-right
// twin: the first slice fills the bar from the right, the legend flows from
// the right with each swatch leading its label, and the title starts at the
// right. Clicks on a slice or a legend entry pick that slice, and Left steps
// forward.
bool checkCompositionDirection(QApplication& app)
{
	// Distinct states give every slice its own colour; diagonal hatches leave
	// that colour showing in every column.
	const QVector<StudioChartSlice> slices{
		{QStringLiteral("textures"), QStringLiteral("Textures"), 50, QStringLiteral("50 MiB"), {}, OperationState::Completed, 0},
		{QStringLiteral("maps"), QStringLiteral("Maps"), 30, QStringLiteral("30 MiB"), {}, OperationState::Running, 1},
		{QStringLiteral("audio"), QStringLiteral("Audio"), 12, QStringLiteral("12 MiB"), {}, OperationState::Warning, 0},
		{QStringLiteral("models"), QStringLiteral("Models"), 7, QStringLiteral("7 MiB"), {}, OperationState::Queued, 1},
		{QStringLiteral("scripts"), QStringLiteral("Scripts"), 1, QStringLiteral("1 MiB"), {}, OperationState::Failed, 0},
	};
	constexpr int kWidth = 640;
	const int count = static_cast<int>(slices.size());
	bool ok = true;
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, scale));
		QVector<QRgb> colors;
		for (int index = 0; index < count; ++index) { colors.append(sliceColor(slices.at(index).state, index)); }
		QVector<Extent> sliceColumns[2], swatchColumns[2];
		Extent title[2];
		for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
			const bool mirrored = direction == Qt::RightToLeft;
			const int side = mirrored ? 1 : 0;
			CompositionChart chart;
			chart.setLayoutDirection(direction);
			chart.setTitle(QStringLiteral("Package composition"));
			chart.setSlices(slices);
			chart.resize(kWidth, chart.heightForWidth(kWidth));
			const QImage image = grabbed(chart);
			ok &= expect(capture(image, QStringLiteral("composition-direction-dark-%1-%2").arg(scale).arg(mirrored ? QStringLiteral("rtl") : QStringLiteral("ltr"))),
				"write composition direction capture");
			const QRgb background = image.pixel(0, 0);
			// Rows dense with the first slice's colour are the bar; the same colours
			// further down are the legend swatches.
			Extent bar;
			for (int y = 0; y < image.height(); ++y) {
				int held = 0;
				for (int x = 0; x < image.width(); ++x) { held += image.pixel(x, y) == colors.first() ? 1 : 0; }
				if (held >= 40) {
					bar.first = bar.first < 0 ? y : bar.first;
					bar.last = y;
				}
			}
			if (!expect(bar.first >= 0, "a composition render shows its bar")) {
				ok = false;
				continue;
			}
			title[side] = columnExtent(markedColumns(image, 0, bar.first - 3, background, false));
			QVector<Extent> swatchRows;
			for (int index = 0; index < count; ++index) {
				sliceColumns[side].append(columnExtent(markedColumns(image, bar.first, bar.last, colors.at(index))));
				Extent rows;
				for (const Extent& band : rowBands(image, colors.at(index))) {
					if (band.first > bar.last && rows.first < 0) { rows = band; }
				}
				swatchRows.append(rows);
				swatchColumns[side].append(rows.first < 0 ? Extent{} : columnExtent(markedColumns(image, rows.first, rows.last, colors.at(index))));
			}
			bool drawn = true;
			for (int index = 0; index < count; ++index) { drawn &= sliceColumns[side].at(index).first >= 0 && swatchColumns[side].at(index).first >= 0; }
			if (!expect(drawn, "a composition render shows every slice and its legend swatch")) {
				ok = false;
				continue;
			}

			// Every legend label follows its swatch on the trailing side, close
			// beside it, and the first entry starts at the leading margin. The
			// label's line is taller than the swatch at large text sizes.
			bool swatchesLead = true;
			const int halfLine = chart.fontMetrics().height() / 2;
			for (int index = 0; index < count; ++index) {
				const Extent swatch = swatchColumns[side].at(index);
				const int middle = (swatchRows.at(index).first + swatchRows.at(index).last) / 2;
				const QVector<bool> legendLine = markedColumns(image, middle - halfLine, middle + halfLine, background, false);
				swatchesLead &= mirrored ? anyPainted(legendLine, swatch.first - 12, swatch.first - 3) : anyPainted(legendLine, swatch.last + 3, swatch.last + 12);
				if (index == 0) {
					swatchesLead &= mirrored ? !anyPainted(legendLine, swatch.last + 3, kWidth - 1) : !anyPainted(legendLine, 0, swatch.first - 3);
				}
			}
			ok &= expect(swatchesLead, "a composition legend entry leads with its swatch, its label following");

			// Clicks on a slice or on its legend entry pick that slice.
			QString activated;
			QObject::connect(&chart, &CompositionChart::sliceActivated, [&activated](const QString& id) { activated = id; });
			bool picked = true;
			for (int index = 0; index < count; ++index) {
				activated.clear();
				click(chart, centreOf(sliceColumns[side].at(index), bar));
				picked &= activated == slices.at(index).id;
				activated.clear();
				click(chart, centreOf(swatchColumns[side].at(index), swatchRows.at(index)));
				picked &= activated == slices.at(index).id;
			}
			ok &= expect(picked, "a click on a composition slice or legend entry picks the slice drawn there");

			// Left and Right move through the slices as drawn.
			QString hovered;
			QObject::connect(&chart, &CompositionChart::hoverChanged, [&hovered](const QString& summary) { hovered = summary; });
			pressKey(chart, Qt::Key_Home);
			pressKey(chart, mirrored ? Qt::Key_Left : Qt::Key_Right);
			const bool forward = hovered == chart.summaryLines().value(1);
			pressKey(chart, mirrored ? Qt::Key_Right : Qt::Key_Left);
			ok &= expect(forward && hovered == chart.summaryLines().value(0), "the arrow keys step through the composition in its drawn direction");
		}
		if (sliceColumns[0].size() == count && sliceColumns[1].size() == count) {
			bool slicesMirror = true, swatchesMirror = true;
			for (int index = 0; index < count; ++index) {
				slicesMirror &= mirrors(sliceColumns[0].at(index), sliceColumns[1].at(index), kWidth, 2);
				swatchesMirror &= mirrors(swatchColumns[0].at(index), swatchColumns[1].at(index), kWidth, 2);
			}
			ok &= expect(slicesMirror, "a right-to-left composition bar mirrors every slice");
			ok &= expect(sliceColumns[1].at(0).first > sliceColumns[1].at(1).last, "a right-to-left composition bar starts at the right");
			ok &= expect(swatchesMirror, "a right-to-left composition legend mirrors every swatch");
			ok &= expect(mirrors(title[0], title[1], kWidth, 3), "a right-to-left composition title starts at the right");
		}
	}
	return ok;
}
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	// Match main.cpp: install the initial studio theme before creating widgets.
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	const QFontMetrics initialMetrics(app.font());
	bool ok = expect(initialMetrics.inFont(QChar('A')) && initialMetrics.inFont(QChar('%')),
		"chart renders require a real font with label and percentage glyphs");
	QWidget host;
	auto* layout = new QVBoxLayout(&host);
	auto* chart = new CompositionChart;
	layout->addWidget(chart);
	chart->setTitle(QStringLiteral("Package composition"));
	chart->setSlices({
		{QStringLiteral("textures"), QStringLiteral("Textures — expanded category label"), 50, QStringLiteral("50 MiB"), {}, OperationState::Completed, 0},
		{QStringLiteral("maps"), QStringLiteral("Maps — expanded category label"), 30, QStringLiteral("30 MiB"), {}, OperationState::Completed, 1},
		{QStringLiteral("audio"), QStringLiteral("Audio — expanded category label"), 12, QStringLiteral("12 MiB"), {}, OperationState::Completed, 2},
		{QStringLiteral("models"), QStringLiteral("Models"), 7, QStringLiteral("7 MiB"), {}, OperationState::Completed, 3},
		{QStringLiteral("scripts"), QStringLiteral("Scripts"), 1, QStringLiteral("1 MiB"), {}, OperationState::Completed, 4},
	});
	const auto originalSummary = chart->accessibleSummary();
	CompositionChart bar;
	bar.setAutoFillBackground(true);
	bar.setSlices({{QStringLiteral("all"), QStringLiteral("Complete category"), 1, QStringLiteral("1 byte")}});
	bar.resize(420, 200);
	for (const auto theme : {StudioTheme::Dark, StudioTheme::Light, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight}) {
		int normalHeight = 0;
		for (const int scale : {100, 200, 100}) {
			const auto tokens = studioThemeTokens(theme, UiDensity::Standard, scale);
			applyStudioTheme(app, tokens);
			const auto direction = scale == 200 ? Qt::RightToLeft : Qt::LeftToRight;
			host.setLayoutDirection(direction); bar.setLayoutDirection(direction);
			const bool highContrast = theme == StudioTheme::HighContrastDark || theme == StudioTheme::HighContrastLight;
			chart->setHighContrast(highContrast); bar.setHighContrast(highContrast);
			host.ensurePolished(); chart->ensurePolished(); bar.ensurePolished();
			host.show();
			app.processEvents();
			ok &= expect(qAbs(chart->font().pointSizeF() - tokens.metrics.baseFontPoints) < 0.1,
				"visible chart inherits the requested application text scale");
			const int narrowHeight = chart->heightForWidth(360), wideHeight = chart->heightForWidth(1500);
			ok &= expect(chart->hasHeightForWidth() && narrowHeight > wideHeight && wideHeight > 0,
				"composition legend advertises its full wrapped height to parent layouts");
			if (scale == 100 && normalHeight == 0) { normalHeight = narrowHeight; }
			ok &= expect(scale == 200 ? narrowHeight > normalHeight : narrowHeight == normalHeight,
				"chart layout follows live text scaling in both directions");
			host.resize(scale == 200 ? 580 : 480, std::max(100, layout->totalHeightForWidth(scale == 200 ? 580 : 480)));
			host.show(); app.processEvents(); layout->activate();
			ok &= expect(chart->height() >= chart->heightForWidth(chart->width()) && chart->height() > 0,
				"parent layout allocates the complete composition legend height");
			ok &= expect(chart->accessibleSummary() == originalSummary && chart->toolTip() == originalSummary && chart->summaryLines().size() == 5
				&& chart->focusPolicy() == Qt::StrongFocus, "theme and layout changes retain complete accessible chart data and focusability");
			const auto barImage = render(bar);
			const int paintedHeight = paintedBarHeight(barImage);
			std::cerr << themeId(theme).toStdString() << ' ' << scale << "%: bar=" << paintedHeight
				<< " font=" << bar.fontMetrics().height() << " narrow=" << narrowHeight << " wide=" << wideHeight << '\n';
			ok &= expect(paintedHeight >= bar.fontMetrics().height() + 2,
				"painted composition bar fits its percentage font and borders at the current scale");
			ok &= expect(!highContrast || readablePercentageField(barImage),
				"high-contrast percentage text has a light/dark field independent of colored hatching");
			const auto name = QStringLiteral("composition-%1-%2").arg(themeId(theme)).arg(scale);
			ok &= expect(capture(render(host), name) && capture(barImage, name + QStringLiteral("-bar")), "write chart widget captures");
		}
	}
	ok &= checkTimelineDirection(app);
	ok &= checkPipelineDirection(app);
	ok &= checkWrappedPipelineDirection(app);
	ok &= checkCompositionDirection(app);
	return ok ? 0 : 1;
}
