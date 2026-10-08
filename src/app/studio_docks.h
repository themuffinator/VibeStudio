#pragma once

// Where the studio's panels dock, whichever way the interface reads.
//
// QMainWindow keeps its dock areas by side: Qt::RightDockWidgetArea is on the
// right in every layout direction, while the central widget and everything in
// it mirror. A panel docked "on the right" therefore sits beside the page in a
// left-to-right window and beside the mode rail in a right-to-left one. These
// helpers keep panels on the side they belong to relative to the reading
// direction instead: the trailing edge by default, and wherever the user put
// them, mirrored with the rest of the window.

#include <QByteArray>
#include <QString>

#include <optional>

namespace vibestudio {

// The side panels open on by default: the trailing edge, so the right in a
// left-to-right layout and the left in a right-to-left one.
[[nodiscard]] Qt::DockWidgetArea trailingDockArea(Qt::LayoutDirection direction);

// The studio_icons glyph for a panel on that side: a window with a sidebar on
// its right, or on its left in a right-to-left layout.
[[nodiscard]] QString trailingPanelGlyph(Qt::LayoutDirection direction);

// A QMainWindow::saveState() as the mirror image of its window would save it:
// the left and right dock areas trade places with their panels, sizes, tab
// groups and current tabs, the corners they hold follow them, and panels
// side by side in a row swap order. Tab order is left alone, since a tab bar
// mirrors its own tabs. Floating panels keep their place on the screen and
// tool bars theirs. Mirroring twice gives back the same bytes. Empty when the
// bytes are not a state this reads (Qt's own undocumented format, as Qt 6.10.1
// writes it; see studio_docks.cpp), so callers fall back to the state as it is.
[[nodiscard]] std::optional<QByteArray> mirroredWindowState(const QByteArray& state);

// The settings keep a window state as a left-to-right window lays it out. This
// turns a stored state into one for a window laid out in `direction`, and a
// state saved by such a window into the stored form: mirrored for
// right-to-left, unchanged otherwise, and unchanged when it cannot be read.
[[nodiscard]] QByteArray windowStateForDirection(const QByteArray& state, Qt::LayoutDirection direction);

} // namespace vibestudio
