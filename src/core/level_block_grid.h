#pragma once

// The compiler block grid of a Quake-family map.
//
// q3map2 (and VibeMap3, which derives from it) splits the world at every
// multiple of a block size along each axis before it chooses any face plane,
// so brushes that cross those boundaries are cut there. The size comes from
// the worldspawn "_blocksize" key (or the older "blocksize" and Soldier of
// Fortune II's "chopsize"): one number for all three axes, or three numbers
// for x, y and z, 1024 units when the key is missing, and 0 (or less) to
// leave an axis unsplit. NetRadiant Custom draws these boundaries in its 2D
// views with Show Blocks; the Levels 2D views draw them with Show Block Grid
// and `map render --block-grid` writes them into its SVG picture.
//
// Workflow reference: NRC XYWnd::XY_DrawBlockGrid and the ShowBlocks toggle in
// radiant/xywindow.cpp at 449778b3 (GPL-2.0-or-later); the key is read as
// q3map2's ProcessWorldModel in tools/quake3/q3map2/bsp.cpp of VibeMap3 reads
// it. Original Qt implementation; see docs/CREDITS.md.

#include "core/level_map.h"

#include <QString>
#include <QVector>

#include <array>

namespace vibestudio {

inline constexpr int kLevelBlockSizeDefault = 1024;

struct LevelMapBlockSize {
	// Per world axis (x, y, z): the block size in units, 0 when the compiler
	// does not split that axis.
	std::array<int, 3> axes {kLevelBlockSizeDefault, kLevelBlockSizeDefault, kLevelBlockSizeDefault};
	// The worldspawn key that set it and its text, both empty for the default.
	QString key;
	QString value;
	// How many numbers the compiler reads from the value, 0 to 3. With none
	// the default stands; with one or two the first is used for all three
	// axes, as q3map2 handles the legacy one-number form, so a second number
	// alone is ignored.
	int numbers = 0;
	// The size along world axis 0, 1 or 2; 0 for no split or a bad axis.
	[[nodiscard]] int along(int axis) const;
	// True when at least one axis is split.
	[[nodiscard]] bool anyAxis() const;
	[[nodiscard]] bool operator==(const LevelMapBlockSize& other) const = default;
};

// The block size a key value gives, read as q3map2 does with sscanf "%d %d %d":
// leading spaces, an optional sign and digits per number, stopping at the
// first character that is not part of one. Zero or one number read keeps the
// default (zero) or copies it to every axis (one); two numbers also copy the
// first, as q3map2's legacy rule does; three set x, y and z. Negative sizes
// leave their axis unsplit, as 0 does.
[[nodiscard]] LevelMapBlockSize levelBlockSizeFromValue(const QString& value);

// The block size of a map: the first non-empty value of "_blocksize",
// "blocksize" and "chopsize" on worldspawn, keys matched ignoring case, or
// the default 1024 on every axis. Doom maps have no compiler block grid and
// report the default with no key.
[[nodiscard]] LevelMapBlockSize levelMapBlockSize(const LevelMapDocument& document);

// True for the formats whose compilers have a block grid to show: Quake-family
// .map files. Doom maps have none.
[[nodiscard]] bool levelMapHasBlockGrid(const LevelMapDocument& document);

// Block boundaries, the multiples of `size`, from `low` to `high` inclusive and
// ascending. Empty when `size` is 0 or less, the range is empty or not
// finite, or more than `limit` boundaries would be needed (`truncated` is then
// set, and nothing is returned so a picture never shows a partial grid).
[[nodiscard]] QVector<double> levelBlockBoundaries(double low, double high, int size, int limit = 4096, bool* truncated = nullptr);

// "1024 × 1024 × 1024" style text for messages, with "off" for an unsplit
// axis, translated in the "VibeStudioLevelBlockGrid" context.
[[nodiscard]] QString levelBlockSizeText(const LevelMapBlockSize& size);

// Where the size comes from, for messages: the worldspawn key and its value,
// or the default, saying when the key held no number or a second number that
// the compiler ignores.
[[nodiscard]] QString levelBlockSizeSourceText(const LevelMapBlockSize& size);

} // namespace vibestudio
