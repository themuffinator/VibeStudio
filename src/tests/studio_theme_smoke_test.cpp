#include "app/studio_icons.h"
#include "app/studio_theme.h"

#include <QColor>
#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <array>
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

// Colour-vision deficiency simulation: the severity 1.0 matrices of Machado,
// Oliveira and Fernandes, "A Physiologically-based Model for Simulation of
// Color Vision Deficiency" (IEEE TVCG 15(6), 2009), applied in linear RGB.
// Numbers only; no code is taken from the paper's materials.
enum class Deficiency { Protanopia, Deuteranopia, Tritanopia };

std::array<double, 3> simulated(const QColor& color, Deficiency deficiency)
{
	static const double protan[3][3] = {{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}};
	static const double deutan[3][3] = {{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}};
	static const double tritan[3][3] = {{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}};
	const auto& m = deficiency == Deficiency::Protanopia ? protan : (deficiency == Deficiency::Deuteranopia ? deutan : tritan);
	const double rgb[3] = {channel(color.redF()), channel(color.greenF()), channel(color.blueF())};
	std::array<double, 3> out {};
	for (int row = 0; row < 3; ++row) {
		out[row] = std::clamp(m[row][0] * rgb[0] + m[row][1] * rgb[1] + m[row][2] * rgb[2], 0.0, 1.0);
	}
	return out;
}

// CIE76 colour difference between two linear-RGB colours (D65).
double deltaE(const std::array<double, 3>& a, const std::array<double, 3>& b)
{
	const auto lab = [](const std::array<double, 3>& c) {
		const double x = (0.4124 * c[0] + 0.3576 * c[1] + 0.1805 * c[2]) / 0.95047;
		const double y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
		const double z = (0.0193 * c[0] + 0.1192 * c[1] + 0.9505 * c[2]) / 1.08883;
		const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
		return std::array<double, 3> {116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z))};
	};
	const std::array<double, 3> p = lab(a);
	const std::array<double, 3> q = lab(b);
	return std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
}

// The colour-vision palettes keep success, warning, and danger apart for the
// readers they are for, keep 3:1 on every background, and reduced
// saturation and monochrome keep every contrast ratio as it was.
bool checkColorVision()
{
	bool ok = true;
	const QVector<StudioTheme> themes = {StudioTheme::Dark, StudioTheme::Light, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight};
	for (const StudioTheme theme : themes) {
		AccessibilityPreferences preferences;
		preferences.theme = theme;
		const StudioThemeTokens typical = studioThemeTokens(preferences);
		for (const ColorVision vision : {ColorVision::RedGreen, ColorVision::BlueYellow}) {
			preferences.colorVision = vision;
			const StudioThemeTokens tokens = studioThemeTokens(preferences);
			const StudioThemeColors& c = tokens.colors;
			const std::string name = themeName(theme) + "/" + colorVisionId(vision).toStdString();
			for (const QColor& surface : {c.appBackground, c.surface, c.panel, c.panelRaised, c.input}) {
				for (const QColor& state : {c.success, c.warning, c.danger, c.info}) {
					ok &= expect(contrast(state, surface) + 1e-6 >= 3.0, name + ": a state colour falls below 3:1 on a background.");
				}
			}
			const QVector<Deficiency> deficiencies = vision == ColorVision::RedGreen
				? QVector<Deficiency> {Deficiency::Protanopia, Deficiency::Deuteranopia}
				: QVector<Deficiency> {Deficiency::Tritanopia};
			for (const Deficiency deficiency : deficiencies) {
				const auto success = simulated(c.success, deficiency);
				const auto warning = simulated(c.warning, deficiency);
				const auto danger = simulated(c.danger, deficiency);
				ok &= expect(deltaE(success, danger) >= 20.0 && deltaE(warning, danger) >= 20.0 && deltaE(success, warning) >= 20.0,
					name + ": success, warning, and danger should stay apart (CIE76 20 or more) for the vision the palette is for.");
			}
			ok &= expect(c.text == typical.colors.text && c.accent == typical.colors.accent && c.focus == typical.colors.focus,
				name + ": colour vision should change state colours only.");
		}
		for (const ColorVision vision : {ColorVision::Typical, ColorVision::Monochrome}) {
			for (const bool reduced : {false, true}) {
				preferences.colorVision = vision;
				preferences.reducedSaturation = reduced;
				const StudioThemeTokens tokens = studioThemeTokens(preferences);
				const StudioThemeColors& c = tokens.colors;
				const std::string name = themeName(theme) + "/" + colorVisionId(vision).toStdString() + (reduced ? "/reduced" : "");
				ok &= checkContrast(theme, (name + " selection text").c_str(), c.selectionText, c.selection, 4.5);
				ok &= checkContrast(theme, (name + " accent text").c_str(), c.accentText, c.accent, 4.5);
				for (const QColor& state : {c.success, c.warning, c.danger}) {
					ok &= checkContrast(theme, (name + " state").c_str(), state, c.panel, 3.0);
				}
				ok &= expect(std::abs(luminance(c.accent) - luminance(typical.colors.accent)) < 0.003,
					name + ": desaturation should keep the accent's luminance.");
				if (vision == ColorVision::Monochrome) {
					ok &= expect(c.accent.hsvSaturation() <= 3 && c.danger.hsvSaturation() <= 3, name + ": monochrome should leave no colour.");
				}
			}
		}
		preferences = AccessibilityPreferences {};
		preferences.theme = theme;
		preferences.thickFocusIndicator = true;
		const StudioThemeTokens thick = studioThemeTokens(preferences);
		ok &= expect(thick.metrics.focusWidth == 3 && thick.thickFocusIndicator, themeName(theme) + ": a thick focus ring is 3px.");
		ok &= expect(studioStyleSheet(thick).contains(QStringLiteral("padding:")), themeName(theme) + ": a thick ring takes its width out of the padding.");
		ok &= expect(!studioStyleSheet(typical).contains(QStringLiteral("solid @")), themeName(theme) + ": every token is substituted.");
	}
	ok &= expect(colorVisionFromId(QStringLiteral("deuteranopia")) == ColorVision::RedGreen && colorVisionFromId(QStringLiteral("tritanopia")) == ColorVision::BlueYellow,
		"The deficiency names should select their palettes.");
	return ok;
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
		ok &= checkContrast(theme, "row selection text", c.selectionText, c.rowSelection, 4.5);
		ok &= checkContrast(theme, "accent text", c.accentText, c.accent, 4.5);
		// State colours are used as text in chips and list rows.
		for (const QColor& state : {c.success, c.warning, c.danger}) {
			ok &= checkContrast(theme, "state text", state, c.panel, 3.0);
		}
		// Focus rings and control outlines are non-text UI components (3:1).
		ok &= checkContrast(theme, "focus ring", c.focus, c.surface, 3.0);
		ok &= expect(c.focus != c.selection && c.focus != c.rowSelection, themeName(theme) + ": focus must stay distinguishable from the selection fills.");
		// A selected row must read as selected against the list it sits in.
		ok &= expect(contrast(c.rowSelection, c.input) >= 1.35 || tokens.highContrast,
			themeName(theme) + ": a selected row should stand apart from the list background.");

		const QString sheet = studioStyleSheet(tokens);
		ok &= expect(!sheet.contains(QLatin1Char('@')), themeName(theme) + ": every stylesheet token should be substituted.");
		ok &= expect(sheet.contains(QStringLiteral("QToolButton#modeButton")) && sheet.contains(QStringLiteral("QToolButton#statusChip")),
			themeName(theme) + ": the stylesheet should style the rail and the status chips.");
		// Style sheet borders do not mirror, so state edges are painted on the
		// leading side by the widgets themselves.
		for (const QString& line : sheet.split(QLatin1Char('\n'))) {
			if (line.contains(QStringLiteral("#noticeBar")) || line.contains(QStringLiteral("#loadingPane"))) {
				ok &= expect(!line.contains(QStringLiteral("border-left")) && !line.contains(QStringLiteral("border-right")),
					themeName(theme) + ": notice and loading strips should paint their state edge, not set a one-sided border: "
						+ line.trimmed().toStdString());
			}
		}
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
	ok &= checkColorVision();

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
		QStringLiteral("external"), QStringLiteral("minus"), QStringLiteral("plus"), QStringLiteral("accessibility"), QStringLiteral("eye"),
		QStringLiteral("key"),
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
