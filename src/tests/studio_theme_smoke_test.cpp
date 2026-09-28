#include "app/studio_icons.h"
#include "app/studio_theme.h"

#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const std::string& message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

// WCAG 2.x relative luminance and contrast ratio.
double channel(double value)
{
	return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& color)
{
	return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

double contrast(const QColor& first, const QColor& second)
{
	const double a = luminance(first);
	const double b = luminance(second);
	return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

std::string themeName(StudioTheme theme)
{
	return themeId(theme).toStdString();
}

bool checkContrast(StudioTheme theme, const char* what, const QColor& foreground, const QColor& background, double minimum)
{
	const double ratio = contrast(foreground, background);
	return expect(ratio + 1e-6 >= minimum,
		themeName(theme) + ": " + what + " contrast " + std::to_string(ratio) + " is below " + std::to_string(minimum));
}

} // namespace

int main(int argc, char** argv)
{
	QGuiApplication app(argc, argv);
	bool ok = true;

	const QVector<StudioTheme> themes = {StudioTheme::Dark, StudioTheme::Light, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight};
	for (const StudioTheme theme : themes) {
		const StudioThemeTokens tokens = studioThemeTokens(theme, UiDensity::Standard, 100);
		const StudioThemeColors& c = tokens.colors;
		ok &= expect(tokens.theme == theme, themeName(theme) + ": tokens should keep an explicit theme.");

		// Body and secondary text on every surface they sit on (WCAG AA, 4.5:1).
		for (const QColor& surface : {c.appBackground, c.surface, c.panel, c.input}) {
			ok &= checkContrast(theme, "text", c.text, surface, 4.5);
			ok &= checkContrast(theme, "muted text", c.textMuted, surface, 4.5);
		}
		// Selected rows, primary buttons, and checked indicators carry text or a
		// mark on a fill.
		ok &= checkContrast(theme, "selection text", c.selectionText, c.selection, 4.5);
		ok &= checkContrast(theme, "accent text", c.accentText, c.accent, 4.5);
		// State colours are used as text in chips and list rows.
		for (const QColor& state : {c.success, c.warning, c.danger}) {
			ok &= checkContrast(theme, "state text", state, c.panel, 3.0);
		}
		// Focus rings and control outlines are non-text UI components (3:1).
		ok &= checkContrast(theme, "focus ring", c.focus, c.surface, 3.0);
		ok &= expect(c.focus != c.selection, themeName(theme) + ": focus must stay distinguishable from the selection fill.");

		const QString sheet = studioStyleSheet(tokens);
		ok &= expect(!sheet.contains(QLatin1Char('@')), themeName(theme) + ": every stylesheet token should be substituted.");
		ok &= expect(sheet.contains(QStringLiteral("QToolButton#modeButton")) && sheet.contains(QStringLiteral("QLabel#statusChip")),
			themeName(theme) + ": the stylesheet should style the rail and the status chips.");
	}

	const StudioThemeTokens hc = studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 100);
	ok &= expect(hc.highContrast && hc.metrics.focusWidth >= 2, "High-visibility themes should draw 2px focus rings.");
	const StudioThemeTokens scaled = studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 200);
	ok &= expect(std::abs(scaled.metrics.baseFontPoints - 2.0 * studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100).metrics.baseFontPoints) < 0.01,
		"Text scale should scale the base font.");
	ok &= expect(studioThemeTokens(StudioTheme::Dark, UiDensity::Compact, 100).metrics.controlHeight
			< studioThemeTokens(StudioTheme::Dark, UiDensity::Comfortable, 100).metrics.controlHeight,
		"Compact density should be tighter than comfortable.");
	ok &= expect(effectiveStudioTheme(StudioTheme::System) != StudioTheme::System, "System should resolve to a concrete theme.");

	// Every glyph the shell and command registry name must exist, so no action
	// silently loses its icon.
	const QStringList names = {
		QStringLiteral("home"), QStringLiteral("map"), QStringLiteral("cube"), QStringLiteral("image"), QStringLiteral("waveform"),
		QStringLiteral("package"), QStringLiteral("code"), QStringLiteral("layers"), QStringLiteral("hammer"), QStringLiteral("settings"),
		QStringLiteral("folder"), QStringLiteral("folder-open"), QStringLiteral("archive"), QStringLiteral("file"), QStringLiteral("save"),
		QStringLiteral("close"), QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("edit"), QStringLiteral("move"),
		QStringLiteral("search"), QStringLiteral("command"), QStringLiteral("grid"), QStringLiteral("export"), QStringLiteral("import"),
		QStringLiteral("add"), QStringLiteral("delete"), QStringLiteral("play"), QStringLiteral("pause"), QStringLiteral("stop"),
		QStringLiteral("terminal"), QStringLiteral("info"), QStringLiteral("copy"), QStringLiteral("plugin"), QStringLiteral("gamepad"),
		QStringLiteral("activity"), QStringLiteral("sidebar-right"), QStringLiteral("chevron-left"), QStringLiteral("chevron-right"),
		QStringLiteral("chevron-up"), QStringLiteral("frame"), QStringLiteral("filter"), QStringLiteral("refresh"), QStringLiteral("compare"),
		QStringLiteral("trash"), QStringLiteral("check"), QStringLiteral("warning"), QStringLiteral("list"), QStringLiteral("tree"),
		QStringLiteral("clock"), QStringLiteral("sparkle"), QStringLiteral("palette"), QStringLiteral("film"), QStringLiteral("hash"),
		QStringLiteral("external"), QStringLiteral("minus"), QStringLiteral("plus"),
	};
	for (const QString& name : names) {
		ok &= expect(studioIconExists(name), "Missing studio glyph: " + name.toStdString());
		const QPixmap pixmap = studioIconPixmap(name, 24, 1.0, QColor(Qt::white));
		ok &= expect(!pixmap.isNull() && pixmap.width() == 24, "Glyph did not render: " + name.toStdString());
	}
	ok &= expect(!studioIconExists(QStringLiteral("no-such-glyph")) && studioIcon(QStringLiteral("no-such-glyph")).isNull(),
		"Unknown glyph names should produce a null icon, not a misleading one.");
	ok &= expect(!studioIcon(QStringLiteral("map")).isNull(), "A known glyph should produce an icon.");

	return ok ? 0 : 1;
}
