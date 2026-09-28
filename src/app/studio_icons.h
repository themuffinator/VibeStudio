#pragma once

// Theme-aware line icons for the studio chrome.
//
// Each icon is a small vector glyph painted with QPainter on a 24-unit grid,
// so it stays crisp at any size or device pixel ratio and needs no image
// files or extra Qt modules. Icons read their colour from the current studio
// theme when they are painted, so a theme switch recolours every icon on the
// next repaint, and disabled/selected states follow the theme automatically.

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

namespace vibestudio {

enum class StudioIconTone {
	Normal,   // Chrome icons: toolbars, menus, rails.
	Muted,    // Decorative icons next to secondary text.
	Accent,   // Draws attention: empty-state artwork, primary affordances.
	OnAccent, // Icons drawn on an accent-filled primary button.
	Success,
	Warning,
	Danger,
};

// Where the glyph sits when the icon is drawn into a wider-than-tall rect.
// Leading leaves the extra width after the glyph (before it in right-to-left
// layouts), which is how text-beside-icon buttons get a readable icon gap:
// give them an icon size a few pixels wider than tall.
enum class StudioIconAlignment {
	Centre,
	Leading,
};

[[nodiscard]] QIcon studioIcon(const QString& name, StudioIconTone tone = StudioIconTone::Normal, StudioIconAlignment alignment = StudioIconAlignment::Centre);
[[nodiscard]] QPixmap studioIconPixmap(const QString& name, int logicalSize, qreal devicePixelRatio, const QColor& color);
[[nodiscard]] bool studioIconExists(const QString& name);
[[nodiscard]] QStringList studioIconNames();

} // namespace vibestudio
