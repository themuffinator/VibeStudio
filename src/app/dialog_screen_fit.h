#pragma once

// Window sizes that fit the screen. A dialog asks for the size its content
// reads best at; at large text sizes, on a laptop screen or with heavy OS
// scaling (1920x1080 at 150% is 1280x720 logical), that can be larger than
// the screen, which puts the buttons that accept, apply or cancel out of
// reach. These helpers bound the request to the room the screen leaves.

#include <QRect>
#include <QSize>

class QVBoxLayout;
class QWidget;

namespace vibestudio {

// The room kept between a fitted window and each edge of the screen's
// available area, so its frame and title bar stay on the screen too.
inline constexpr int dialogScreenMargin = 20;

// `preferred` bounded to `available` less the margin on every side, but never
// smaller than `minimum`: below its minimum a dialog cannot lay out its
// controls at all.
[[nodiscard]] QSize boundedDialogSize(const QSize& preferred, const QSize& minimum, const QSize& available);

// The available geometry (the screen less task bars and docks) of the screen a
// dialog opens on: its parent window's screen, else the screen under the
// pointer, else the primary screen. Empty when there is no screen, and on the
// offscreen platform, whose placeholder screen is not one anybody sees.
[[nodiscard]] QRect dialogScreenArea(const QWidget* dialog);

// For tests: dialogScreenArea() returns `area` until this is called again
// with an empty rectangle.
void setDialogScreenAreaOverride(const QRect& area);

// Resizes a window to `preferred`, bounded to dialogScreenArea() and never
// below its minimum size hint. A body from addScrollingDialogBody() widens
// the request to the width its content needs, so it scrolls down rather than
// sideways where the screen has room. Call it once the layout is built, so
// the minimum is known. QDialog centres itself on its parent and keeps to the
// screen when shown, so the position is left alone.
void fitDialogToScreen(QWidget* dialog, const QSize& preferred);

// Adds a frameless scroll area to a dialog's top layout and returns the
// layout to build its content in. Add the button box to `dialogLayout`
// afterwards: when the content is taller than the screen allows, it scrolls
// and the buttons stay in view.
QVBoxLayout* addScrollingDialogBody(QVBoxLayout* dialogLayout);

} // namespace vibestudio
