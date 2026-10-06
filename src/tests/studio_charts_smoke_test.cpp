#include "app/studio_charts.h"
#include "app/studio_theme.h"

#include <QApplication>
#include <QDir>
#include <QFontMetrics>
#include <QImage>
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

// The first and last marked column; -1 when none is.
struct ColumnExtent {
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

ColumnExtent columnExtent(const QVector<bool>& columns)
{
	ColumnExtent extent;
	for (int x = 0; x < columns.size(); ++x) {
		if (columns.at(x)) {
			extent.first = extent.first < 0 ? x : extent.first;
			extent.last = x;
		}
	}
	return extent;
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

// Where one timeline render put its parts, found from the raster: the first
// row's state glyph and text line, and the second row's duration fill.
struct TimelineMarks {
	ColumnExtent glyph;
	ColumnExtent secondFill;
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
			const QImage image = render(chart);
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
			const ColumnExtent line = columnExtent(found.firstLine);
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
		const auto mirrors = [width](const ColumnExtent& ltr, const ColumnExtent& rtl, int tolerance) {
			return std::abs(rtl.first - (width - 1 - ltr.last)) <= tolerance && std::abs(rtl.last - (width - 1 - ltr.first)) <= tolerance;
		};
		ok &= expect(mirrors(marks[0].glyph, marks[1].glyph, 3), "a right-to-left timeline mirrors the state glyph column");
		ok &= expect(mirrors(marks[0].secondFill, marks[1].secondFill, 3),
			"a right-to-left duration bar grows from the right end of its track");
		const ColumnExtent ltrLine = columnExtent(marks[0].firstLine), rtlLine = columnExtent(marks[1].firstLine);
		ok &= expect(std::abs(rtlLine.first - (width - 1 - ltrLine.last)) <= 3, "a right-to-left timeline puts the time on the left");
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
	return ok ? 0 : 1;
}
