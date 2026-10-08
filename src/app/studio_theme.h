#pragma once

// Studio design tokens, palette, and stylesheet.
//
// Every colour, radius, and padding the shell paints with is derived here from
// three preferences: the theme, the UI density, and the text scale. Widgets
// never hard-code chrome colours; they pick a role (objectName or a "variant"
// property) and the stylesheet generated below styles that role for every
// theme, including both high-visibility themes. Custom-painted widgets read
// the same tokens through the QPalette this module installs.

#include "core/studio_settings.h"

#include <QColor>
#include <QPalette>
#include <QString>

class QApplication;
class QWidget;

namespace vibestudio {

struct StudioThemeColors {
	QColor appBackground;   // Frame chrome: mode rail, toolbars, status bar.
	QColor surface;         // Page background behind cards and panels.
	QColor panel;           // Cards, docks, list backgrounds.
	QColor panelRaised;     // Headers, hovered rows, secondary buttons.
	QColor input;           // Editable fields and item-view bases.
	QColor borderSubtle;    // Dividers inside a panel.
	QColor border;          // Panel and control outlines.
	QColor borderStrong;    // Hovered controls and emphasised outlines.
	QColor text;
	QColor textMuted;       // Secondary labels and metadata.
	QColor textFaint;       // Placeholders and disabled text.
	QColor accent;
	QColor accentHover;
	QColor accentPressed;
	QColor accentText;      // Text drawn on an accent fill.
	QColor accentSubtle;    // Checked tool buttons and the current mode.
	QColor selection;       // The palette's Highlight: what canvases mark as selected, and progress fills.
	QColor selectionText;   // Text on the selection and row selection fills.
	QColor rowSelection;    // Selected rows in lists, trees, and menus: quieter than selection.
	QColor focus;           // Keyboard focus ring.
	QColor success;
	QColor warning;
	QColor danger;
	QColor info;
};

struct StudioThemeMetrics {
	double baseFontPoints = 10.5;
	double smallFontPoints = 9.5;
	double headingFontPoints = 11.5;
	double titleFontPoints = 15.0;
	double displayFontPoints = 21.0;
	int controlPaddingVertical = 5;
	int controlPaddingHorizontal = 10;
	int itemPaddingVertical = 5;
	int itemPaddingHorizontal = 8;
	int spacing = 8;
	int radius = 8;
	int radiusSmall = 5;
	int borderWidth = 1;
	int focusWidth = 1;
	int controlHeight = 28;
};

struct StudioThemeTokens {
	StudioTheme theme = StudioTheme::Dark;   // Always resolved: never System.
	UiDensity density = UiDensity::Standard;
	int textScalePercent = 100;
	bool light = false;
	bool highContrast = false;
	// Accessibility adjustments, already folded into colors and metrics.
	ColorVision colorVision = ColorVision::Typical;
	bool reducedSaturation = false;
	bool thickFocusIndicator = false;
	bool thickTextCursor = false;
	bool steadyTextCursor = false;
	QString fontFamily;      // empty: the system's interface typeface
	bool wideTextSpacing = false;
	StudioThemeColors colors;
	StudioThemeMetrics metrics;
};

// How far reduced saturation moves chromatic colours toward grey.
inline constexpr double kReducedSaturationAmount = 0.6;

// The padding around the mode rail's pin on every side. The style sheet pads
// the pin with it, and right to left the pin places its own glyph from it.
inline constexpr int kRailPinPadding = 6;

// Resolves StudioTheme::System from the platform: a high-contrast desktop
// gives the matching high-visibility theme (Qt 6.10's contrast preference),
// otherwise the light or dark colour scheme. Every other theme passes
// through unchanged.
[[nodiscard]] StudioTheme effectiveStudioTheme(StudioTheme preference);

// Theme, density, and text scale alone, with no accessibility adjustments.
[[nodiscard]] StudioThemeTokens studioThemeTokens(StudioTheme preference, UiDensity density, int textScalePercent);
// Everything the preferences ask for: colour vision, reduced saturation,
// focus and cursor thickness, typeface, and spacing as well.
[[nodiscard]] StudioThemeTokens studioThemeTokens(const AccessibilityPreferences& preferences);

// Moves a colour toward the grey of the same relative luminance; 1 is fully
// grey. Contrast ratios are unchanged, since luminance is.
[[nodiscard]] QColor studioDesaturatedColor(const QColor& color, double amount);
// A state colour a widget chose itself, passed through the current theme's
// monochrome or reduced-saturation setting.
[[nodiscard]] QColor studioAdjustedStateColor(const QColor& color);

[[nodiscard]] QPalette studioPalette(const StudioThemeTokens& tokens);
[[nodiscard]] QString studioStyleSheet(const StudioThemeTokens& tokens);

// Installs the Fusion style (the only built-in style that honours a custom
// palette identically on Windows, macOS, and Linux), the palette, the base
// font size, and the stylesheet on the whole application, then records the
// tokens so icons and custom-painted widgets can read them. An identical sheet
// is retained; a changed sheet is detached before replacement to avoid repeated
// recursive restyling of nested widgets.
void applyStudioTheme(QApplication& app, const StudioThemeTokens& tokens);

// The tokens most recently applied. Defaults to the dark theme before the
// first applyStudioTheme() call so painting code never sees an empty set.
[[nodiscard]] const StudioThemeTokens& currentStudioTheme();

// True once applyStudioTheme() has run with tokens resolving to the same
// theme, density, and text scale, so callers can skip a redundant (and
// expensive) application-wide repolish.
[[nodiscard]] bool studioThemeIsApplied(const StudioThemeTokens& tokens);

// Colour for an operation-state accent (chips, glyphs, list foregrounds), or
// an invalid QColor for states that should keep the normal text colour.
[[nodiscard]] QColor studioThemeStateColor(const StudioThemeTokens& tokens, const QString& operationStateId);

// Marks a push button with a visual role the stylesheet understands:
// "primary" (one per surface), "danger", "ghost", or empty for the default
// secondary look.
void setButtonVariant(QWidget* button, const QString& variant);

} // namespace vibestudio
