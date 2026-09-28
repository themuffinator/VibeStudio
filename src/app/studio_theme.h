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
	QColor selection;       // Selected rows and highlighted menu items.
	QColor selectionText;   // Text on the selection fill.
	QColor focus;           // Keyboard focus ring.
	QColor success;
	QColor warning;
	QColor danger;
	QColor info;
};

struct StudioThemeMetrics {
	double baseFontPoints = 10.0;
	double smallFontPoints = 9.0;
	double headingFontPoints = 11.5;
	double titleFontPoints = 15.0;
	double displayFontPoints = 21.0;
	int controlPaddingVertical = 5;
	int controlPaddingHorizontal = 10;
	int itemPaddingVertical = 5;
	int itemPaddingHorizontal = 8;
	int spacing = 8;
	int radius = 6;
	int radiusSmall = 4;
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
	StudioThemeColors colors;
	StudioThemeMetrics metrics;
};

// Resolves StudioTheme::System to Light or Dark from the platform colour
// scheme, and passes every other theme through unchanged.
[[nodiscard]] StudioTheme effectiveStudioTheme(StudioTheme preference);

[[nodiscard]] StudioThemeTokens studioThemeTokens(StudioTheme preference, UiDensity density, int textScalePercent);

[[nodiscard]] QPalette studioPalette(const StudioThemeTokens& tokens);
[[nodiscard]] QString studioStyleSheet(const StudioThemeTokens& tokens);

// Installs the Fusion style (the only built-in style that honours a custom
// palette identically on Windows, macOS, and Linux), the palette, the base
// font size, and the stylesheet on the whole application, then records the
// tokens so icons and custom-painted widgets can read them.
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
