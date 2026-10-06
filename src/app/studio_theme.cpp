#include "app/studio_theme.h"

#include "app/studio_icons.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleOption>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace vibestudio {

namespace {

QColor mix(const QColor& from, const QColor& to, double amount)
{
	const double t = std::clamp(amount, 0.0, 1.0);
	return QColor::fromRgbF(
		static_cast<float>(from.redF() + (to.redF() - from.redF()) * t),
		static_cast<float>(from.greenF() + (to.greenF() - from.greenF()) * t),
		static_cast<float>(from.blueF() + (to.blueF() - from.blueF()) * t),
		1.0f);
}

QString hex(const QColor& color)
{
	return color.name(QColor::HexRgb);
}

// The default studio look takes its cue from idStudio: neutral charcoal
// panels rather than blue-tinted ones, black-backed viewports, and an orange
// accent. Text on orange fills is near-black. The selection colour, which
// canvases (map, model, UV, brush, and waveform views) mark selected elements
// with, and progress bars fill with, is a deeper burnt orange so white text on
// it still clears WCAG AA (4.5:1). Selected rows in lists, trees, and menus
// take a quieter ember instead, so a long list with one row picked still reads
// as calm chrome.
StudioThemeColors darkColors()
{
	StudioThemeColors c;
	c.appBackground = QColor(0x24, 0x24, 0x24);
	c.surface = QColor(0x2b, 0x2b, 0x2b);
	c.panel = QColor(0x33, 0x33, 0x33);
	c.panelRaised = QColor(0x3e, 0x3e, 0x3e);
	c.input = QColor(0x22, 0x22, 0x22);
	c.borderSubtle = QColor(0x3c, 0x3c, 0x3c);
	c.border = QColor(0x4a, 0x4a, 0x4a);
	c.borderStrong = QColor(0x60, 0x60, 0x60);
	c.text = QColor(0xe2, 0xe2, 0xe2);
	c.textMuted = QColor(0xab, 0xab, 0xab);
	c.textFaint = QColor(0x7c, 0x7c, 0x7c);
	c.accent = QColor(0xe8, 0x84, 0x1a);
	c.accentHover = QColor(0xf5, 0x97, 0x3a);
	c.accentPressed = QColor(0xcc, 0x6f, 0x0f);
	c.accentText = QColor(0x1a, 0x1a, 0x1a);
	c.accentSubtle = QColor(0x4b, 0x3b, 0x2a);
	c.selection = QColor(0xa3, 0x53, 0x0b);
	c.selectionText = QColor(0xff, 0xff, 0xff);
	c.rowSelection = QColor(0x62, 0x40, 0x1f);
	c.focus = QColor(0x4e, 0xa3, 0xff);
	c.success = QColor(0x6c, 0xc0, 0x70);
	c.warning = QColor(0xf2, 0xb4, 0x41);
	c.danger = QColor(0xef, 0x5f, 0x55);
	c.info = QColor(0x5a, 0xa7, 0xf2);
	return c;
}

StudioThemeColors lightColors()
{
	StudioThemeColors c;
	c.appBackground = QColor(0xe4, 0xe4, 0xe4);
	c.surface = QColor(0xef, 0xef, 0xef);
	c.panel = QColor(0xfb, 0xfb, 0xfb);
	c.panelRaised = QColor(0xe8, 0xe8, 0xe8);
	c.input = QColor(0xff, 0xff, 0xff);
	c.borderSubtle = QColor(0xdc, 0xdc, 0xdc);
	c.border = QColor(0xc6, 0xc6, 0xc6);
	c.borderStrong = QColor(0xa5, 0xa5, 0xa5);
	c.text = QColor(0x1c, 0x1c, 0x1c);
	c.textMuted = QColor(0x55, 0x55, 0x55);
	c.textFaint = QColor(0x8c, 0x8c, 0x8c);
	c.accent = QColor(0xb3, 0x59, 0x00);
	c.accentHover = QColor(0xc4, 0x66, 0x07);
	c.accentPressed = QColor(0x94, 0x49, 0x00);
	c.accentText = QColor(0xff, 0xff, 0xff);
	c.accentSubtle = QColor(0xf6, 0xe3, 0xcf);
	c.selection = QColor(0xf9, 0xd4, 0xad);
	c.selectionText = QColor(0x1c, 0x1c, 0x1c);
	c.rowSelection = c.selection;
	c.focus = QColor(0x1f, 0x6f, 0xd1);
	c.success = QColor(0x1d, 0x7a, 0x45);
	c.warning = QColor(0x94, 0x57, 0x00);
	c.danger = QColor(0xc4, 0x30, 0x2b);
	c.info = QColor(0x1f, 0x6f, 0xd1);
	return c;
}

// High-visibility themes use pure black and white with a single saturated
// accent, full-strength outlines on every panel, and a focus colour that is
// distinct from the accent so focus never hides inside a selection.
StudioThemeColors highContrastDarkColors()
{
	StudioThemeColors c;
	c.appBackground = QColor(0x00, 0x00, 0x00);
	c.surface = QColor(0x00, 0x00, 0x00);
	c.panel = QColor(0x00, 0x00, 0x00);
	c.panelRaised = QColor(0x1c, 0x1c, 0x1c);
	c.input = QColor(0x00, 0x00, 0x00);
	c.borderSubtle = QColor(0xff, 0xff, 0xff);
	c.border = QColor(0xff, 0xff, 0xff);
	c.borderStrong = QColor(0xff, 0xff, 0xff);
	c.text = QColor(0xff, 0xff, 0xff);
	c.textMuted = QColor(0xff, 0xff, 0xff);
	// Disabled text: legible on black (6:1) but plainly not white, the way
	// the system high-contrast themes set grey text apart.
	c.textFaint = QColor(0x8c, 0x8c, 0x8c);
	c.accent = QColor(0xff, 0xd8, 0x00);
	c.accentHover = QColor(0xff, 0xf0, 0x6a);
	c.accentPressed = QColor(0xe6, 0xc2, 0x00);
	c.accentText = QColor(0x00, 0x00, 0x00);
	c.accentSubtle = QColor(0xff, 0xd8, 0x00);
	c.selection = QColor(0xff, 0xd8, 0x00);
	c.selectionText = QColor(0x00, 0x00, 0x00);
	c.rowSelection = c.selection;
	c.focus = QColor(0x00, 0xe5, 0xff);
	c.success = QColor(0x95, 0xff, 0x95);
	c.warning = QColor(0xff, 0xd8, 0x00);
	c.danger = QColor(0xff, 0x6b, 0x6b);
	c.info = QColor(0x6a, 0xb7, 0xff);
	return c;
}

StudioThemeColors highContrastLightColors()
{
	StudioThemeColors c;
	c.appBackground = QColor(0xff, 0xff, 0xff);
	c.surface = QColor(0xff, 0xff, 0xff);
	c.panel = QColor(0xff, 0xff, 0xff);
	c.panelRaised = QColor(0xeb, 0xeb, 0xeb);
	c.input = QColor(0xff, 0xff, 0xff);
	c.borderSubtle = QColor(0x00, 0x00, 0x00);
	c.border = QColor(0x00, 0x00, 0x00);
	c.borderStrong = QColor(0x00, 0x00, 0x00);
	c.text = QColor(0x00, 0x00, 0x00);
	c.textMuted = QColor(0x10, 0x10, 0x10);
	// Disabled text: 4.5:1 on white, and plainly not black.
	c.textFaint = QColor(0x76, 0x76, 0x76);
	c.accent = QColor(0x00, 0x33, 0xcc);
	c.accentHover = QColor(0x00, 0x20, 0x80);
	c.accentPressed = QColor(0x00, 0x1a, 0x66);
	c.accentText = QColor(0xff, 0xff, 0xff);
	c.accentSubtle = QColor(0x00, 0x33, 0xcc);
	c.selection = QColor(0x00, 0x33, 0xcc);
	c.selectionText = QColor(0xff, 0xff, 0xff);
	c.rowSelection = c.selection;
	c.focus = QColor(0xc0, 0x00, 0x60);
	c.success = QColor(0x00, 0x60, 0x00);
	c.warning = QColor(0x8a, 0x4b, 0x00);
	c.danger = QColor(0xa0, 0x00, 0x00);
	c.info = QColor(0x00, 0x33, 0xcc);
	return c;
}

StudioThemeMetrics metricsFor(UiDensity density, int textScalePercent, bool highContrast)
{
	StudioThemeMetrics m;
	const double scale = std::clamp(textScalePercent, 50, 400) / 100.0;
	m.baseFontPoints = 10.5 * scale;
	m.smallFontPoints = 9.5 * scale;
	m.headingFontPoints = 11.0 * scale;
	m.titleFontPoints = 13.0 * scale;
	m.displayFontPoints = 20.0 * scale;
	m.controlPaddingVertical = 4;
	m.controlHeight = 26;
	m.itemPaddingVertical = 4;
	switch (density) {
	case UiDensity::Compact:
		m.controlPaddingVertical = 2;
		m.controlPaddingHorizontal = 8;
		m.itemPaddingVertical = 2;
		m.itemPaddingHorizontal = 6;
		m.spacing = 6;
		m.controlHeight = 24;
		break;
	case UiDensity::Comfortable:
		m.controlPaddingVertical = 7;
		m.controlPaddingHorizontal = 14;
		m.itemPaddingVertical = 8;
		m.itemPaddingHorizontal = 10;
		m.spacing = 10;
		m.controlHeight = 34;
		break;
	case UiDensity::Standard:
		break;
	}
	if (highContrast) {
		m.borderWidth = 2;
		m.focusWidth = 2;
	}
	return m;
}

// Fusion paints check boxes and radio buttons with a frame derived from the
// window colour, which all but disappears on dark panels. This proxy draws
// them from the theme tokens instead: a clearly outlined box, an accent fill
// when checked, and a contrasting mark, in every theme. Everything else is
// left to Fusion.
class StudioProxyStyle final : public QProxyStyle {
public:
	using QProxyStyle::QProxyStyle;

	void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override
	{
		if (element == PE_IndicatorCheckBox || element == PE_IndicatorItemViewItemCheck) {
			drawCheckIndicator(option, painter);
			return;
		}
		if (element == PE_IndicatorRadioButton) {
			drawRadioIndicator(option, painter);
			return;
		}
		// Fusion caps its arrows at 8 pixels whatever the text size, so a combo
		// box's or a tree's arrow shrank beside 200% text. These are the
		// studio's chevrons instead, sized from the text scale.
		switch (element) {
		case PE_IndicatorArrowDown:
			drawChevron(painter, option, Qt::DownArrow);
			return;
		case PE_IndicatorArrowUp:
			drawChevron(painter, option, Qt::UpArrow);
			return;
		case PE_IndicatorArrowLeft:
			drawChevron(painter, option, Qt::LeftArrow);
			return;
		case PE_IndicatorArrowRight:
			drawChevron(painter, option, Qt::RightArrow);
			return;
		case PE_IndicatorBranch:
			if (option->state.testFlag(State_Children)) {
				const bool open = option->state.testFlag(State_Open);
				const bool rightToLeft = option->direction == Qt::RightToLeft;
				drawChevron(painter, option, open ? Qt::DownArrow : rightToLeft ? Qt::LeftArrow : Qt::RightArrow);
			}
			return;
		default:
			break;
		}
		QProxyStyle::drawPrimitive(element, option, painter, widget);
	}

	int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
	{
		const double scale = std::clamp(currentStudioTheme().textScalePercent, 50, 400) / 100.0;
		const auto scaled = [scale](int size) {
			return static_cast<int>(size * scale + 0.5);
		};
		const int base = QProxyStyle::pixelMetric(metric, option, widget);
		switch (metric) {
		// Fusion gives the overflow button 12 pixels, too narrow for a glyph
		// once the studio's button padding is in; it grows with the text.
		case PM_ToolBarExtensionExtent:
			return std::max(base, scaled(22));
		// Icons the studio does not size itself (in menus, push buttons, tabs,
		// and a field's leading glyph and clear button) grow with the text, as
		// the ones it sizes do.
		case PM_SmallIconSize:
		case PM_ButtonIconSize:
			return scaled(base);
		// Some styles derive these from the small icon size, already scaled
		// above, and some fix them; either way they are scaled once.
		case PM_TabBarIconSize:
		case PM_LineEditIconSize:
			return std::max(base, scaled(16));
		default:
			return base;
		}
	}

	QIcon standardIcon(StandardPixmap standard, const QStyleOption* option, const QWidget* widget) const override
	{
		// A tool bar with more than fits keeps the rest behind this button, so
		// it has to show on the studio's dark chrome.
		if (standard == SP_ToolBarHorizontalExtensionButton) {
			return studioIcon(QGuiApplication::layoutDirection() == Qt::RightToLeft ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right"));
		}
		if (standard == SP_ToolBarVerticalExtensionButton) {
			return studioIcon(QStringLiteral("chevron-down"));
		}
		return QProxyStyle::standardIcon(standard, option, widget);
	}

private:
	static void drawChevron(QPainter* painter, const QStyleOption* option, Qt::ArrowType direction)
	{
		const StudioThemeTokens& theme = currentStudioTheme();
		const StudioThemeColors& c = theme.colors;
		const double scale = std::clamp(theme.textScalePercent, 50, 400) / 100.0;
		const QRectF area(option->rect);
		// Half the arrow's width: as large as the text asks, never past the box.
		const qreal half = std::min<qreal>(std::min(area.width(), area.height()) * 0.3, 4.0 * scale);
		if (half <= 0.5) {
			return;
		}
		const QPointF c0 = area.center();
		QPolygonF chevron;
		switch (direction) {
		case Qt::DownArrow:
			chevron << QPointF(c0.x() - half, c0.y() - half * 0.5) << QPointF(c0.x(), c0.y() + half * 0.5) << QPointF(c0.x() + half, c0.y() - half * 0.5);
			break;
		case Qt::UpArrow:
			chevron << QPointF(c0.x() - half, c0.y() + half * 0.5) << QPointF(c0.x(), c0.y() - half * 0.5) << QPointF(c0.x() + half, c0.y() + half * 0.5);
			break;
		case Qt::LeftArrow:
			chevron << QPointF(c0.x() + half * 0.5, c0.y() - half) << QPointF(c0.x() - half * 0.5, c0.y()) << QPointF(c0.x() + half * 0.5, c0.y() + half);
			break;
		default:
			chevron << QPointF(c0.x() - half * 0.5, c0.y() - half) << QPointF(c0.x() + half * 0.5, c0.y()) << QPointF(c0.x() - half * 0.5, c0.y() + half);
			break;
		}
		// On a highlighted row the arrow takes the highlight's text colour:
		// item views mark the row selected, a menu hands its highlighted
		// item's text colour over as the palette's WindowText, and a pressed or
		// checked button its text colour as ButtonText.
		QColor color = option->palette.color(QPalette::WindowText);
		if (!option->state.testFlag(State_Enabled)) {
			color = c.textFaint;
		} else if (option->state.testFlag(State_Selected)) {
			color = option->palette.color(QPalette::HighlightedText);
		} else if (option->state & (State_On | State_Sunken)) {
			color = option->palette.color(QPalette::ButtonText);
		}
		QPen pen(color, std::max<qreal>(1.3, 1.4 * scale));
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->setPen(pen);
		painter->setBrush(Qt::NoBrush);
		painter->drawPolyline(chevron);
		painter->restore();
	}

	static void indicatorColors(const QStyleOption* option, QColor* fill, QColor* outline, QColor* mark)
	{
		const StudioThemeTokens& theme = currentStudioTheme();
		const StudioThemeColors& c = theme.colors;
		const bool enabled = option->state.testFlag(State_Enabled);
		const bool on = option->state.testFlag(State_On) || option->state.testFlag(State_NoChange);
		const bool hover = option->state.testFlag(State_MouseOver);
		if (!enabled) {
			*fill = c.panel;
			*outline = c.borderSubtle;
			*mark = c.textFaint;
			return;
		}
		if (on) {
			*fill = hover ? c.accentHover : c.accent;
			*outline = *fill;
			*mark = c.accentText;
			return;
		}
		*fill = c.input;
		*outline = hover ? c.accent : (theme.highContrast ? c.border : c.borderStrong);
		*mark = c.text;
	}

	static void drawCheckIndicator(const QStyleOption* option, QPainter* painter)
	{
		QColor fill;
		QColor outline;
		QColor mark;
		indicatorColors(option, &fill, &outline, &mark);
		const StudioThemeTokens& theme = currentStudioTheme();
		const qreal side = std::min(option->rect.width(), option->rect.height()) - 1.0;
		const QRectF box(option->rect.x() + (option->rect.width() - side) / 2.0, option->rect.y() + (option->rect.height() - side) / 2.0, side, side);
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->setPen(QPen(outline, theme.highContrast ? 2.0 : 1.2));
		painter->setBrush(fill);
		painter->drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
		QPen markPen(mark, std::max<qreal>(1.6, side / 7.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		painter->setPen(markPen);
		painter->setBrush(Qt::NoBrush);
		if (option->state.testFlag(State_NoChange)) {
			painter->drawLine(QPointF(box.left() + side * 0.28, box.center().y()), QPointF(box.right() - side * 0.28, box.center().y()));
		} else if (option->state.testFlag(State_On)) {
			QPainterPath check;
			check.moveTo(box.left() + side * 0.24, box.top() + side * 0.52);
			check.lineTo(box.left() + side * 0.43, box.top() + side * 0.70);
			check.lineTo(box.left() + side * 0.77, box.top() + side * 0.31);
			painter->drawPath(check);
		}
		painter->restore();
	}

	static void drawRadioIndicator(const QStyleOption* option, QPainter* painter)
	{
		QColor fill;
		QColor outline;
		QColor mark;
		indicatorColors(option, &fill, &outline, &mark);
		const StudioThemeTokens& theme = currentStudioTheme();
		const qreal side = std::min(option->rect.width(), option->rect.height()) - 1.0;
		const QRectF circle(option->rect.x() + (option->rect.width() - side) / 2.0, option->rect.y() + (option->rect.height() - side) / 2.0, side, side);
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		painter->setPen(QPen(outline, theme.highContrast ? 2.0 : 1.2));
		painter->setBrush(fill);
		painter->drawEllipse(circle.adjusted(0.5, 0.5, -0.5, -0.5));
		if (option->state.testFlag(State_On)) {
			painter->setPen(Qt::NoPen);
			painter->setBrush(mark);
			painter->drawEllipse(circle.center(), side * 0.2, side * 0.2);
		}
		painter->restore();
	}
};

StudioThemeTokens& mutableCurrentTheme()
{
	static StudioThemeTokens tokens = studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100);
	return tokens;
}

bool& themeApplied()
{
	static bool applied = false;
	return applied;
}

// Replaces every "@token" in the template. Longer names are substituted first
// so "@panelRaised" is never clobbered by "@panel".
// The chevron a combo box shows, in `color`, for the style sheet to name: a
// style sheet takes an arrow's picture only from a file. Each colour is drawn
// once, large, and scaled down to the arrow's size; the files go when the
// studio closes.
QString comboArrowImage(const QColor& color)
{
	static QTemporaryDir directory;
	if (!directory.isValid()) {
		return {};
	}
	const QString path = directory.filePath(QStringLiteral("chevron-down-%1.png").arg(color.name(QColor::HexArgb).mid(1)));
	if (!QFileInfo::exists(path)) {
		QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		QPen pen(color, 7.0);
		pen.setCapStyle(Qt::RoundCap);
		pen.setJoinStyle(Qt::RoundJoin);
		painter.setPen(pen);
		painter.drawPolyline(QPolygonF {QPointF(14.0, 24.0), QPointF(32.0, 42.0), QPointF(50.0, 24.0)});
		painter.end();
		image.save(path);
	}
	return QDir::fromNativeSeparators(path);
}

QString substituteTokens(QString text, const QHash<QString, QString>& values)
{
	QVector<std::pair<QString, QString>> ordered;
	ordered.reserve(values.size());
	for (auto it = values.cbegin(); it != values.cend(); ++it) {
		ordered.push_back({it.key(), it.value()});
	}
	std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
		return left.first.size() > right.first.size();
	});
	for (const auto& [name, value] : ordered) {
		text.replace(QLatin1Char('@') + name, value);
	}
	return text;
}

} // namespace

StudioTheme effectiveStudioTheme(StudioTheme preference)
{
	if (preference != StudioTheme::System) {
		return preference;
	}
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
	if (const QStyleHints* hints = QGuiApplication::styleHints()) {
		switch (hints->colorScheme()) {
		case Qt::ColorScheme::Light:
			return StudioTheme::Light;
		case Qt::ColorScheme::Dark:
			return StudioTheme::Dark;
		case Qt::ColorScheme::Unknown:
			break;
		}
	}
#endif
	return StudioTheme::Dark;
}

StudioThemeTokens studioThemeTokens(StudioTheme preference, UiDensity density, int textScalePercent)
{
	StudioThemeTokens tokens;
	tokens.theme = effectiveStudioTheme(preference);
	tokens.density = density;
	tokens.textScalePercent = textScalePercent;
	tokens.highContrast = tokens.theme == StudioTheme::HighContrastDark || tokens.theme == StudioTheme::HighContrastLight;
	tokens.light = tokens.theme == StudioTheme::Light || tokens.theme == StudioTheme::HighContrastLight;
	switch (tokens.theme) {
	case StudioTheme::Light:
		tokens.colors = lightColors();
		break;
	case StudioTheme::HighContrastDark:
		tokens.colors = highContrastDarkColors();
		break;
	case StudioTheme::HighContrastLight:
		tokens.colors = highContrastLightColors();
		break;
	case StudioTheme::Dark:
	case StudioTheme::System:
		tokens.colors = darkColors();
		break;
	}
	tokens.metrics = metricsFor(density, textScalePercent, tokens.highContrast);
	return tokens;
}

QPalette studioPalette(const StudioThemeTokens& tokens)
{
	const StudioThemeColors& c = tokens.colors;
	QPalette palette;
	palette.setColor(QPalette::Window, c.surface);
	palette.setColor(QPalette::WindowText, c.text);
	palette.setColor(QPalette::Base, c.input);
	palette.setColor(QPalette::AlternateBase, c.panel);
	palette.setColor(QPalette::ToolTipBase, c.panelRaised);
	palette.setColor(QPalette::ToolTipText, c.text);
	palette.setColor(QPalette::PlaceholderText, c.textFaint);
	palette.setColor(QPalette::Text, c.text);
	palette.setColor(QPalette::Button, c.panelRaised);
	palette.setColor(QPalette::ButtonText, c.text);
	palette.setColor(QPalette::BrightText, tokens.light ? QColor(Qt::black) : QColor(Qt::white));
	palette.setColor(QPalette::Highlight, c.selection);
	palette.setColor(QPalette::HighlightedText, c.selectionText);
	palette.setColor(QPalette::Link, c.accent);
	palette.setColor(QPalette::LinkVisited, c.accentPressed);
	palette.setColor(QPalette::Light, tokens.light ? QColor(Qt::white) : mix(c.panelRaised, c.text, 0.12));
	palette.setColor(QPalette::Midlight, mix(c.panelRaised, c.border, 0.5));
	palette.setColor(QPalette::Mid, c.border);
	palette.setColor(QPalette::Dark, tokens.light ? c.borderStrong : c.appBackground);
	palette.setColor(QPalette::Shadow, tokens.light ? c.borderStrong : QColor(Qt::black));

	for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
		palette.setColor(QPalette::Disabled, role, c.textFaint);
	}
	palette.setColor(QPalette::Disabled, QPalette::Highlight, c.panelRaised);
	palette.setColor(QPalette::Disabled, QPalette::HighlightedText, c.textFaint);
	palette.setColor(QPalette::Disabled, QPalette::Button, c.panel);
	palette.setColor(QPalette::Disabled, QPalette::Base, c.panel);
	return palette;
}

QString studioStyleSheet(const StudioThemeTokens& tokens)
{
	const StudioThemeColors& c = tokens.colors;
	const StudioThemeMetrics& m = tokens.metrics;
	const bool hc = tokens.highContrast;

	auto points = [](double value) {
		return QString::number(value, 'f', 1) + QStringLiteral("pt");
	};
	auto pixels = [](int value) {
		return QString::number(value) + QStringLiteral("px");
	};

	// Layouts may shrink controls to their stylesheet minimum inside a scroll
	// area. Keep that minimum tall enough for the scaled application font.
	QFont controlFont = QGuiApplication::font();
	controlFont.setPointSizeF(m.baseFontPoints);
	const int controlContent = std::max(QFontMetrics(controlFont).height(),
		std::max(12, m.controlHeight - 2 * m.controlPaddingVertical - 2 * m.borderWidth));

	QHash<QString, QString> values;
	values.insert(QStringLiteral("appBackground"), hex(c.appBackground));
	values.insert(QStringLiteral("surface"), hex(c.surface));
	values.insert(QStringLiteral("panelRaised"), hex(c.panelRaised));
	values.insert(QStringLiteral("panelHover"), hex(hc ? c.panelRaised : mix(c.panelRaised, c.text, tokens.light ? 0.05 : 0.07)));
	values.insert(QStringLiteral("panel"), hex(c.panel));
	values.insert(QStringLiteral("input"), hex(c.input));
	values.insert(QStringLiteral("rowHover"), hex(hc ? c.panelRaised : mix(c.input, c.text, tokens.light ? 0.045 : 0.06)));
	values.insert(QStringLiteral("rowAlternate"), hex(hc ? c.input : mix(c.input, c.text, tokens.light ? 0.025 : 0.03)));
	values.insert(QStringLiteral("borderSubtle"), hex(c.borderSubtle));
	values.insert(QStringLiteral("borderStrong"), hex(c.borderStrong));
	values.insert(QStringLiteral("border"), hex(c.border));
	values.insert(QStringLiteral("textMuted"), hex(c.textMuted));
	values.insert(QStringLiteral("textFaint"), hex(c.textFaint));
	values.insert(QStringLiteral("text"), hex(c.text));
	values.insert(QStringLiteral("accentHover"), hex(c.accentHover));
	values.insert(QStringLiteral("accentPressed"), hex(c.accentPressed));
	values.insert(QStringLiteral("accentText"), hex(c.accentText));
	values.insert(QStringLiteral("accentSubtle"), hex(c.accentSubtle));
	values.insert(QStringLiteral("selectionFill"), hex(c.rowSelection));
	values.insert(QStringLiteral("accent"), hex(c.accent));
	values.insert(QStringLiteral("selectionText"), hex(c.selectionText));
	// Fusion brightens its progress gradient. Reserve a little contrast margin
	// for white labels on the standard dark theme's fill.
	values.insert(QStringLiteral("progressFill"), hex(tokens.light || hc ? c.selection : c.selection.darker(110)));
	values.insert(QStringLiteral("focus"), hex(c.focus));
	values.insert(QStringLiteral("stateSuccess"), hex(c.success));
	values.insert(QStringLiteral("stateWarning"), hex(c.warning));
	values.insert(QStringLiteral("stateInfo"), hex(c.info));
	values.insert(QStringLiteral("dangerSubtle"), hex(hc ? c.panel : mix(c.panel, c.danger, 0.16)));
	values.insert(QStringLiteral("danger"), hex(c.danger));
	values.insert(QStringLiteral("scrollHandleHover"), hex(hc ? c.text : mix(c.panelRaised, c.text, 0.38)));
	values.insert(QStringLiteral("scrollHandle"), hex(hc ? c.textFaint : mix(c.panelRaised, c.text, 0.2)));
	values.insert(QStringLiteral("skeletonBorder"), hex(hc ? c.border : mix(c.panel, c.text, 0.18)));
	values.insert(QStringLiteral("skeleton"), hex(hc ? c.panel : mix(c.panel, c.text, 0.05)));
	values.insert(QStringLiteral("smallPt"), points(m.smallFontPoints));
	values.insert(QStringLiteral("headingPt"), points(m.headingFontPoints));
	values.insert(QStringLiteral("titlePt"), points(m.titleFontPoints));
	values.insert(QStringLiteral("displayPt"), points(m.displayFontPoints));
	values.insert(QStringLiteral("padV"), pixels(m.controlPaddingVertical));
	values.insert(QStringLiteral("padH"), pixels(m.controlPaddingHorizontal));
	values.insert(QStringLiteral("itemV"), pixels(m.itemPaddingVertical));
	values.insert(QStringLiteral("itemH"), pixels(m.itemPaddingHorizontal));
	values.insert(QStringLiteral("radiusSmall"), pixels(m.radiusSmall));
	values.insert(QStringLiteral("radius"), pixels(m.radius));
	values.insert(QStringLiteral("bw"), pixels(m.borderWidth));
	values.insert(QStringLiteral("fw"), pixels(m.focusWidth));
	values.insert(QStringLiteral("controlContent"), pixels(controlContent));
	// A combo box's arrow area grows with the text, so its chevron can.
	const double textScale = std::clamp(tokens.textScalePercent, 50, 400) / 100.0;
	values.insert(QStringLiteral("dropWidth"), pixels(static_cast<int>(20 * textScale + 0.5)));
	values.insert(QStringLiteral("arrowSize"), pixels(static_cast<int>(10 * textScale + 0.5)));
	values.insert(QStringLiteral("comboArrowDisabled"), comboArrowImage(c.textFaint));
	values.insert(QStringLiteral("comboArrow"), comboArrowImage(hc ? c.text : c.textMuted));

	// Leave progress borders and chunks to Fusion: Qt's stylesheet renderer
	// otherwise paints a single text color over both sides of the fill. Native
	// painting uses the contrasting Text/HighlightedText palette roles. See
	// Qt 6.10.1 qstylesheetstyle.cpp and qfusionstyle.cpp (GPL-3.0/LGPL-3.0),
	// linked in docs/CREDITS.md; no upstream implementation is copied here.
	QString sheet = QStringLiteral(R"(
QMainWindow { background: @surface; }
QMainWindow::separator { background: @borderSubtle; width: 1px; height: 1px; }
QWidget#modeStack, QWidget#studioPage, QWidget#studioPageBody { background: @surface; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget#scrollContent { background: transparent; }
QToolTip { background: @panelRaised; color: @text; border: @bw solid @borderStrong; border-radius: @radiusSmall; padding: 4px 8px; }

QMenuBar { background: @appBackground; color: @text; border-bottom: 1px solid @borderSubtle; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 4px 9px; border-radius: @radiusSmall; }
QMenuBar::item:selected { background: @panelRaised; }
QMenuBar::item:pressed { background: @panelHover; color: @text; }
QMenu { background: @panel; color: @text; border: @bw solid @border; border-radius: @radius; padding: 5px; }
QMenu::item { padding: 6px 28px 6px 30px; border-radius: @radiusSmall; }
QMenu::item:selected { background: @selectionFill; color: @selectionText; }
QMenu::item:disabled { color: @textFaint; }
QMenu::separator { height: 1px; background: @borderSubtle; margin: 5px 10px; }

QToolBar { background: @appBackground; border: none; border-bottom: 1px solid @borderSubtle; padding: 3px 8px; spacing: 2px; }
QToolBar::separator { background: @border; width: 1px; margin: 7px 6px; }
QToolBar QToolButton { background: transparent; color: @text; border: @bw solid transparent; border-radius: @radiusSmall; padding: 4px 6px; }
QToolBar QToolButton:hover { background: @panelRaised; border-color: @panelRaised; }
QToolBar QToolButton:pressed { background: @accentSubtle; color: @selectionText; }
QToolBar QToolButton:checked { background: @accentSubtle; color: @selectionText; border-color: @accent; }
QToolBar QToolButton:focus { border-color: @focus; }
QToolBar QToolButton:disabled { color: @textFaint; }
QToolBar#pageToolBar { background: @surface; border-bottom: 1px solid @borderSubtle; padding: 4px 14px; spacing: 4px; }
QToolBar QLabel { color: @textMuted; padding: 0 4px; }
QToolBar#studioToolBar { padding: 3px 8px 3px 4px; spacing: 1px; }
QToolBar#studioToolBar QMenuBar { background: transparent; border: none; padding: 0px; }
QToolBar#studioToolBar QToolButton[runCommand="true"] { padding: 4px 10px 4px 8px; }

QStatusBar { background: @appBackground; color: @textMuted; border-top: 1px solid @borderSubtle; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @textMuted; }
QStatusBar QToolButton { background: transparent; color: @textMuted; border: @bw solid transparent; border-radius: @radiusSmall; padding: 1px 8px; }
QStatusBar QToolButton:hover { background: @panelRaised; color: @text; }
QStatusBar QToolButton:checked { color: @text; background: @panelRaised; }

QWidget#modeRail { background: @appBackground; border-right: 1px solid @borderSubtle; }
QToolButton#modeButton { background: transparent; color: @textMuted; border: @bw solid transparent; border-radius: @radiusSmall; margin: 1px 6px; padding: 7px 6px 7px 7px; text-align: left; }
QToolButton#modeButton:hover { background: @panelRaised; color: @text; }
QToolButton#modeButton:checked { background: @accentSubtle; color: @selectionText; font-weight: 600; }
QToolButton#modeButton:focus { border: @fw solid @focus; }
QToolButton#railToggle { background: transparent; color: @textFaint; border: @bw solid transparent; border-radius: @radiusSmall; padding: 6px; }
QToolButton#railToggle:hover { background: @panelRaised; color: @text; }
QToolButton#railToggle:checked { background: @accentSubtle; }
QToolButton#railToggle:focus { border: @fw solid @focus; }
QFrame#railDivider { background: @borderSubtle; max-height: 1px; min-height: 1px; border: none; margin: 7px 14px; }
QFrame#verticalDivider { background: @border; min-width: 1px; max-width: 1px; border: none; margin: 0px 4px; }

QWidget#pageHeader { background: @surface; border-bottom: 1px solid @borderSubtle; }
QLabel#pageTitle { font-size: @titlePt; font-weight: 600; color: @text; }
QLabel#pageSubtitle { color: @textMuted; }
QFrame#pageHeaderDivider { background: @border; min-width: 1px; max-width: 1px; border: none; margin: 0px; }

QFrame#card, QFrame#modulePanel, QFrame#setupPanel, QFrame#preferencesPanel { background: @panel; border: @bw solid @borderSubtle; border-radius: @radius; }
QLabel#cardTitle { font-size: @headingPt; font-weight: 600; color: @text; }
QLabel#cardMeta, QLabel#panelMeta, QLabel#moduleMeta, QLabel#appSubtitle, QLabel#fieldHint, QLabel#settingsNoMatch { color: @textMuted; }
QLabel#sectionLabel { color: @textMuted; font-size: @smallPt; font-weight: 700; }
QLabel#appTitle { font-size: @displayPt; font-weight: 700; color: @text; }
QLabel#moduleTitle { font-size: @headingPt; font-weight: 600; }
QLabel#viewportReadout { color: @textMuted; background: @appBackground; border-top: 1px solid @borderSubtle; padding: 3px 10px; }
QToolButton#codeZoomReadout { color: @textMuted; background: @appBackground; border: none; border-top: 1px solid @borderSubtle; border-radius: 0px; padding: 3px 10px; }
QToolButton#codeZoomReadout:hover { color: @text; background: @panelRaised; }
QToolButton#codeZoomReadout:focus { border: @fw solid @focus; }

QFrame#tile { background: @panel; border: @bw solid @borderSubtle; border-radius: @radius; }
QFrame#tile:hover { border-color: @borderStrong; background: @panelHover; }
QToolButton#tileButton { background: @panel; color: @text; border: @bw solid @borderSubtle; border-radius: @radius; padding: 10px 12px; text-align: left; }
QToolButton#tileButton:hover { background: @panelHover; border-color: @borderStrong; }
QToolButton#tileButton:focus { border: @fw solid @focus; }

QWidget#emptyState { background: transparent; }
QLabel#emptyTitle { font-size: @titlePt; font-weight: 600; color: @text; }
QLabel#emptyBody { color: @textMuted; }

QPushButton { background: @panelRaised; color: @text; border: @bw solid @border; border-radius: @radiusSmall; padding: @padV @padH; min-height: @controlContent; }
QPushButton:hover { background: @panelHover; border-color: @borderStrong; }
QPushButton:pressed { background: @panel; }
QPushButton:focus { border-color: @focus; }
QPushButton:disabled { background: @panel; color: @textFaint; border-color: @borderSubtle; }
QPushButton[variant="primary"] { background: @accent; color: @accentText; border-color: @accent; font-weight: 600; }
QPushButton[variant="primary"]:hover { background: @accentHover; border-color: @accentHover; }
QPushButton[variant="primary"]:pressed { background: @accentPressed; border-color: @accentPressed; }
QPushButton[variant="primary"]:focus { border: @fw solid @focus; }
QPushButton[variant="primary"]:disabled { background: @panelRaised; color: @textFaint; border-color: @borderSubtle; }
QPushButton[variant="danger"] { color: @danger; }
QPushButton[variant="danger"]:hover { background: @dangerSubtle; border-color: @danger; }
QPushButton[variant="ghost"] { background: transparent; border-color: transparent; }
QPushButton[variant="ghost"]:hover { background: @panelRaised; border-color: @border; }
QPushButton[variant="ghost"]:focus { border-color: @focus; }

QToolButton { background: transparent; color: @text; border: @bw solid transparent; border-radius: @radiusSmall; padding: 3px 5px; }
QToolButton:hover { background: @panelRaised; border-color: @border; }
QToolButton:pressed { background: @accentSubtle; }
QToolButton:checked { background: @accentSubtle; color: @selectionText; border-color: @accent; }
QToolButton:focus { border-color: @focus; }
QToolButton:disabled { color: @textFaint; }
QToolButton#qt_toolbar_ext_button { padding: 0px; margin: 0px; }
QToolButton[segment="true"] { background: @input; border: @bw solid @border; border-radius: 0px; padding: 3px 10px; }
QToolButton[segment="true"]:hover { background: @rowHover; }
QToolButton[segment="true"]:checked { background: @accentSubtle; color: @selectionText; border-color: @accent; }
QToolButton[segmentPosition="first"] { border-top-left-radius: @radiusSmall; border-bottom-left-radius: @radiusSmall; }
QToolButton[segmentPosition="last"] { border-top-right-radius: @radiusSmall; border-bottom-right-radius: @radiusSmall; }

QLineEdit, QAbstractSpinBox { background: @input; color: @text; border: @bw solid @border; border-radius: @radiusSmall; padding: @padV 8px; min-height: @controlContent; selection-background-color: @accent; selection-color: @accentText; }
QPlainTextEdit, QTextEdit { background: @input; color: @text; border: @bw solid @borderSubtle; border-radius: @radiusSmall; selection-background-color: @accent; selection-color: @accentText; }
QLineEdit:hover, QAbstractSpinBox:hover { border-color: @borderStrong; }
QLineEdit:focus, QAbstractSpinBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border: @fw solid @focus; }
QLineEdit:disabled, QAbstractSpinBox:disabled { background: @panel; color: @textFaint; border-color: @borderSubtle; }
QLineEdit[searchField="true"] { padding-left: 8px; }

QComboBox { background: @input; color: @text; border: @bw solid @border; border-radius: @radiusSmall; padding: @padV 8px; min-height: @controlContent; }
QComboBox:hover { border-color: @borderStrong; }
QComboBox:focus { border: @fw solid @focus; }
QComboBox:disabled { background: @panel; color: @textFaint; border-color: @borderSubtle; }
QComboBox:on { border-color: @accent; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: @dropWidth; border: none; background: transparent; }
QComboBox::down-arrow { image: url(@comboArrow); width: @arrowSize; height: @arrowSize; }
QComboBox::down-arrow:disabled { image: url(@comboArrowDisabled); }
QComboBox QAbstractItemView { background: @panel; color: @text; border: @bw solid @border; selection-background-color: @selectionFill; selection-color: @selectionText; outline: 0px; padding: 2px; }

QCheckBox, QRadioButton { color: @text; background: transparent; spacing: 8px; }
QToolBar QCheckBox { margin: 0px 6px; }
QCheckBox:disabled, QRadioButton:disabled { color: @textFaint; }
QCheckBox:focus, QRadioButton:focus { color: @text; }

QAbstractItemView { background: @input; color: @text; border: @bw solid @borderSubtle; border-radius: @radiusSmall; selection-background-color: @selectionFill; selection-color: @selectionText; alternate-background-color: @rowAlternate; outline: 0px; }
QAbstractItemView:focus { border: @fw solid @focus; }
QAbstractItemView[flat="true"] { background: transparent; border: none; }
QAbstractItemView[flat="true"]:focus { border: @fw solid @focus; }
QListView::item { padding: @itemV @itemH; border-radius: @radiusSmall; }
QTreeView::item { padding: 2px 4px; }
QListView::item:hover, QTreeView::item:hover { background: @rowHover; }
QListView::item:selected, QTreeView::item:selected { background: @selectionFill; color: @selectionText; }
QListView::item:disabled { color: @textFaint; }
QHeaderView { background: transparent; border: none; }
QHeaderView::section { background: @panel; color: @textMuted; border: none; border-bottom: 1px solid @borderSubtle; padding: 5px 8px; font-weight: 600; }
QTableCornerButton::section { background: @panel; border: none; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 0px; border: none; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0px; border: none; }
QScrollBar::handle:vertical { background: @scrollHandle; min-height: 28px; border-radius: 3px; margin: 2px 3px; }
QScrollBar::handle:horizontal { background: @scrollHandle; min-width: 28px; border-radius: 3px; margin: 3px 2px; }
QScrollBar::handle:hover, QScrollBar::handle:pressed { background: @scrollHandleHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QAbstractScrollArea::corner { background: transparent; border: none; }

QTabWidget::pane { border: none; border-top: 1px solid @borderSubtle; background: transparent; top: -1px; }
QTabWidget::tab-bar { left: 0px; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: @textMuted; border: none; border-bottom: 2px solid transparent; padding: 6px 12px; margin-right: 2px; }
QTabBar::tab:hover { color: @text; background: @panelRaised; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; font-weight: 600; }
QTabBar::tab:disabled { color: @textFaint; }
QTabBar::scroller { width: 22px; }
QTabBar::tab:bottom { border-bottom: none; border-top: 2px solid transparent; padding: 5px 12px; }
QTabBar::tab:bottom:selected { border-bottom: none; border-top: 2px solid @accent; background: @panel; }
QTabBar[compactTabs="true"]::tab { padding: 6px 6px; }
QTabBar[compactTabs="true"]::tab:bottom { padding: 5px 6px; }
QTabWidget[tabsAtBottom="true"]::pane { border-top: none; border-bottom: 1px solid @borderSubtle; }

QGroupBox { background: @panel; border: @bw solid @borderSubtle; border-radius: @radius; margin-top: 22px; padding: 14px 12px 12px 12px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 2px; top: 0px; padding: 0px 4px; color: @text; font-weight: 600; font-size: @headingPt; }

QProgressBar { font-size: @smallPt; selection-background-color: @progressFill; selection-color: @selectionText; }
QProgressBar:disabled { selection-background-color: @panelRaised; selection-color: @textFaint; }

QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: @accentSubtle; }
QSplitter::handle:horizontal { width: 6px; }
QSplitter::handle:vertical { height: 6px; }

QDockWidget { color: @text; }
QDockWidget::title { background: @appBackground; color: @text; padding: 6px 10px; border-bottom: 1px solid @borderSubtle; text-align: left; }
QDockWidget::close-button, QDockWidget::float-button { background: transparent; border: none; border-radius: @radiusSmall; padding: 2px; }
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: @panelRaised; }
QWidget#dockTitleBar { background: @surface; border-bottom: 1px solid @borderSubtle; }
QLabel#dockTitle { color: @text; font-weight: 600; }
QToolButton#dockTitleButton { padding: 3px; }
QWidget#dockBody { background: @panel; }

QFrame#loadingPane { background: @panel; border: @bw solid @borderSubtle; border-radius: @radiusSmall; }
QLabel#statusChip { font-weight: 600; color: @textMuted; }
QLabel#statusChip[operationState="completed"] { color: @stateSuccess; }
QLabel#statusChip[operationState="warning"], QLabel#statusChip[operationState="cancelled"] { color: @stateWarning; }
QLabel#statusChip[operationState="failed"] { color: @danger; }
QLabel#statusChip[operationState="running"], QLabel#statusChip[operationState="loading"], QLabel#statusChip[operationState="queued"] { color: @stateInfo; }
QLabel#codeFilesStatus, QLabel#codeFilesFilterStatus { color: @textMuted; font-size: @smallPt; padding: 0px 2px; }
QLabel#loadingTitle { font-weight: 600; color: @text; }
QLabel#loadingDetail { color: @textMuted; }
QLabel#skeletonRow { background: @skeleton; border: @bw solid @skeleton; border-radius: @radiusSmall; color: @textFaint; padding: 5px 10px; }
QFrame#detailDrawer { background: @panel; border: @bw solid @borderSubtle; border-radius: @radius; }
QFrame#detailDrawer[embedded="true"] { background: transparent; border: none; border-radius: 0px; }
QLabel#drawerTitle { font-weight: 600; font-size: @headingPt; color: @text; }
QLabel#drawerSubtitle, QLabel#drawerEmpty { color: @textMuted; }
QTextEdit#detailContent, QTextEdit#inspector { background: @input; border: @bw solid @borderSubtle; }

QToolButton#statusChip { background: transparent; color: @textMuted; border: @bw solid transparent; border-radius: @radiusSmall; padding: 1px 8px; font-weight: 500; }
QToolButton#statusChip[operationState="completed"] { @chipSuccess }
QToolButton#statusChip[operationState="warning"], QToolButton#statusChip[operationState="cancelled"] { @chipWarning }
QToolButton#statusChip[operationState="failed"] { @chipDanger }
QToolButton#statusChip[operationState="running"], QToolButton#statusChip[operationState="loading"], QToolButton#statusChip[operationState="queued"] { @chipInfo }
QToolButton#statusChip[operationState="idle"] { color: @textMuted; }
QToolButton#statusChip:hover { background: @panelRaised; color: @text; }
QToolButton#statusChip:focus { border: @fw solid @focus; }
QFrame#noticeBar { background: @panelRaised; border: none; border-bottom: @bw solid @borderSubtle; }
QFrame#noticeBar[operationState="failed"] { @noticeDanger }
QFrame#noticeBar[operationState="warning"], QFrame#noticeBar[operationState="cancelled"] { @noticeWarning }
QFrame#noticeBar[operationState="completed"] { @noticeSuccess }
QFrame#noticeBar[operationState="running"], QFrame#noticeBar[operationState="loading"], QFrame#noticeBar[operationState="queued"] { @noticeInfo }
QLabel#noticeTitle { color: @text; font-weight: 600; }
QLabel#noticeText { color: @text; }

QPlainTextEdit#codeEditor { border: none; border-radius: 0px; }

QToolBar#viewportToolBar { background: @panel; border-bottom: 1px solid @borderSubtle; padding: 3px 8px; }
QToolButton#commandSearchButton { background: @input; color: @textMuted; border: @bw solid @borderSubtle; border-radius: @radius; padding: 3px 8px 3px 10px; text-align: left; }
QToolButton#commandSearchButton:hover { background: @rowHover; border-color: @border; color: @text; }
QToolButton#commandSearchButton:focus { border: @fw solid @focus; }
QDialog#commandPalette, QDialog#quickOpen { background: @panel; border: @bw solid @borderStrong; border-radius: @radius; }
QDialog#commandPalette[floatingPanel="true"], QDialog#quickOpen[floatingPanel="true"] { background: transparent; border: none; }
QLineEdit#commandPaletteFilter, QLineEdit#quickOpenFilter { font-size: @headingPt; padding: 7px 10px; border-radius: @radius; }
QLabel#commandPaletteHint, QLabel#quickOpenHint { color: @textMuted; font-size: @smallPt; }
QToolButton#breadcrumbButton { color: @textMuted; padding: 2px 6px; border-radius: @radiusSmall; }
QWidget#codeBreadcrumb { background: @appBackground; border-top: 1px solid @borderSubtle; }
QToolButton#breadcrumbButton:hover { color: @text; }
QToolButton#breadcrumbButton[current="true"] { color: @text; font-weight: 600; }
QLabel#breadcrumbSeparator { color: @textFaint; padding: 0px 2px; }
QListWidget#settingsCategories::item { padding: 8px 10px; margin-bottom: 2px; }
QListWidget#settingsCategories::item:hover { background: @rowHover; }
QListWidget#settingsCategories::item:selected { background: @accentSubtle; color: @selectionText; font-weight: 600; }
)");
	// Status items sit flat on the bar; a state that wants attention (a
	// warning or a failure) gains a quiet tint as well as its colour and the
	// mark that names it. High-visibility themes outline every state.
	const auto statusItem = [&](const QColor& state, bool tinted) {
		if (hc) {
			return QStringLiteral("color: %1; background: %2; border-color: %1;").arg(hex(state), hex(c.appBackground));
		}
		const QColor tint = mix(c.appBackground, state, tokens.light ? 0.16 : 0.14);
		return QStringLiteral("color: %1; background: %2;").arg(hex(state), tinted ? hex(tint) : QStringLiteral("transparent"));
	};
	sheet.replace(QStringLiteral("@chipSuccess"), statusItem(c.success, false));
	sheet.replace(QStringLiteral("@chipWarning"), statusItem(c.warning, true));
	sheet.replace(QStringLiteral("@chipDanger"), statusItem(c.danger, true));
	sheet.replace(QStringLiteral("@chipInfo"), statusItem(c.info, false));
	// A notice's tint carries its state; the title names it in words. Its
	// leading edge is painted by NoticeBar, since a border-left would not
	// mirror in a right-to-left layout.
	const auto noticeTint = [&](const QColor& state) {
		return QStringLiteral("background: %1;").arg(hex(hc ? c.panel : mix(c.panel, state, 0.10)));
	};
	sheet.replace(QStringLiteral("@noticeDanger"), noticeTint(c.danger));
	sheet.replace(QStringLiteral("@noticeWarning"), noticeTint(c.warning));
	sheet.replace(QStringLiteral("@noticeSuccess"), noticeTint(c.success));
	sheet.replace(QStringLiteral("@noticeInfo"), noticeTint(c.info));
	return substituteTokens(sheet, values);
}

void applyStudioTheme(QApplication& app, const StudioThemeTokens& tokens)
{
	// Keep the native UI typeface used before the design-system switch.
	// Capture it before installing Fusion, which may replace the style font.
	QFont font = QApplication::font();
	mutableCurrentTheme() = tokens;
	themeApplied() = true;
	// Fusion paints arrows, tree branches, and frames from the palette, so one
	// palette gives consistent results on every platform; the proxy on top of it
	// draws check and radio indicators from the tokens.
	// Installed once: once an application stylesheet exists, style() returns
	// the stylesheet wrapper, so the proxy cannot be detected through it.
	static bool styleInstalled = false;
	if (!styleInstalled) {
		if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
			QApplication::setStyle(new StudioProxyStyle(fusion));
			styleInstalled = true;
		}
	}
	const auto sheet = studioStyleSheet(tokens);
	const bool replaceSheet = app.styleSheet() != sheet;
	// Updating Qt's existing application sheet recursively repolishes cached
	// descendants once per ancestor. Detach it before installing a changed
	// sheet so each widget follows the normal style replacement path instead.
	// Public APIs only; Qt 6.10.1 behavior reviewed in qstylesheetstyle.cpp
	// and qapplication.cpp (see docs/CREDITS.md). No Qt code is copied here.
	// Palette and font follow detachment, which restores the base style.
	if (replaceSheet && !app.styleSheet().isEmpty()) { app.setStyleSheet({}); }
	QApplication::setPalette(studioPalette(tokens));
	font.setPointSizeF(tokens.metrics.baseFontPoints);
	QApplication::setFont(font);
	if (replaceSheet) { app.setStyleSheet(sheet); }
}

const StudioThemeTokens& currentStudioTheme()
{
	return mutableCurrentTheme();
}

bool studioThemeIsApplied(const StudioThemeTokens& tokens)
{
	const StudioThemeTokens& current = mutableCurrentTheme();
	return themeApplied() && current.theme == tokens.theme && current.density == tokens.density
		&& current.textScalePercent == tokens.textScalePercent;
}

QColor studioThemeStateColor(const StudioThemeTokens& tokens, const QString& operationStateId)
{
	const StudioThemeColors& c = tokens.colors;
	if (operationStateId == QStringLiteral("completed")) {
		return c.success;
	}
	if (operationStateId == QStringLiteral("warning") || operationStateId == QStringLiteral("cancelled")) {
		return c.warning;
	}
	if (operationStateId == QStringLiteral("failed")) {
		return c.danger;
	}
	if (operationStateId == QStringLiteral("queued") || operationStateId == QStringLiteral("loading") || operationStateId == QStringLiteral("running")) {
		return c.info;
	}
	return {};
}

void setButtonVariant(QWidget* button, const QString& variant)
{
	if (!button) {
		return;
	}
	button->setProperty("variant", variant);
	if (QStyle* style = button->style()) {
		style->unpolish(button);
		style->polish(button);
	}
}

} // namespace vibestudio
