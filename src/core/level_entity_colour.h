#pragma once

// Entity colour keys such as _color: reading and writing the two ways
// Quake-family tools spell them ("1 0.5 0" in 0-1 fractions, "255 128 0" in
// 0-255), and NetRadiant Custom's entity colour commands: Set Entity Colour
// (K) writes one colour to every selected entity and to the entities owning
// the selected brushes and patches, and Normalise Entity Colour scales the
// primary entity's colour so its brightest channel is 1 (black becomes white)
// and writes it to the same entities. Each edit is one undo step through
// setLevelMapEntitiesProperty, so scene locks, the open group and linked
// copies are handled as for any key edit.
//
// Workflow reference: NRC radiant/entity.cpp Entity_setColour,
// Entity_normalizeColor and NormalizeColor at 449778b3 (GPL-2.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.

#include "core/level_map.h"

#include <QColor>
#include <QString>
#include <QVector>

#include <array>

namespace vibestudio {

// The key NetRadiant Custom's colour commands write.
[[nodiscard]] QString levelEntityColourKey();
// Whether a key holds a colour by its name alone: _color or color, in any case.
// Entity definitions can type other keys as colours too.
[[nodiscard]] bool isLevelEntityColourKeyName(const QString& key);

// A colour key's value: three channels as written, with anything after them,
// such as a brightness, kept as text.
struct LevelEntityColour {
	std::array<double, 3> channels {1.0, 1.0, 1.0};
	// True when every channel is within 0-1, the fraction form; otherwise the
	// channels are 0-255. "1 1 1" and "0 0 0" read as fractions.
	bool fractions = true;
	QString rest;

	// Each channel within 0-1 (divided by 255 in the 0-255 form), unclamped.
	[[nodiscard]] std::array<double, 3> unit() const;
	// The colour in 8 bits a channel, clamped.
	[[nodiscard]] QColor color() const;
};

// Reads "r g b" and keeps any trailing text. Fails on fewer than three
// numbers or on a channel that is not a finite number of zero or more.
bool parseLevelEntityColour(const QString& value, LevelEntityColour* parsed);
// Writes the colour in its own form: fractions with up to six decimals and no
// trailing zeros, or whole numbers 0-255; the trailing text follows.
[[nodiscard]] QString formatLevelEntityColour(const LevelEntityColour& colour);
// `unit` (0-1 a channel) in the form of `like`, keeping its trailing text, so
// a key written as 0-255 stays 0-255. Without `like`, fractions.
[[nodiscard]] QString formatLevelEntityColourLike(const std::array<double, 3>& unit, const LevelEntityColour* like = nullptr);
// The colour scaled so its brightest channel is 1, in fraction form with the
// same trailing text; black becomes 1 1 1.
[[nodiscard]] LevelEntityColour normalisedLevelEntityColour(const LevelEntityColour& colour);

// Which entities the commands write for the current selection: each selected
// entity, and the entity holding each selected brush or patch, once each and
// in selection order. A world brush or patch adds nothing: worldspawn is
// written only when it is selected itself. Hidden entities are left out.
struct LevelEntityColourTargets {
	QVector<int> entities;
	// The entity of the last-selected item that reaches a target: normally the
	// primary item's own entity. -1 without targets.
	int primaryEntity = -1;
	// World brushes and patches in the selection.
	int worldPrimitives = 0;
	// Targets left out because they are hidden.
	int hidden = 0;
};
[[nodiscard]] LevelEntityColourTargets levelMapEntityColourTargets(const LevelMapDocument& document);
// The primary target's colour under `key`, when it has one that parses.
bool levelMapPrimaryEntityColour(const LevelMapDocument& document, const QString& key, LevelEntityColour* colour, int* entityId = nullptr);
// Why the commands would refuse these targets, so the picker need not open:
// none to colour, one in a locked layer or group, or one outside the open
// group. Empty when they may go ahead; the commands check the same again
// through the scene guard when they write.
[[nodiscard]] QString levelMapEntityColourBlockedReason(const LevelMapDocument& document, const LevelEntityColourTargets& targets);

struct LevelEntityColourReport {
	// Every entity the command wrote or would have written.
	QVector<int> entities;
	// The entities whose value changed.
	QVector<int> changed;
	// The entity whose colour was read: the primary for Normalise.
	int sourceEntity = -1;
	// The value written to the primary target.
	QString value;
	int worldPrimitives = 0;
	int hidden = 0;
	// Normalise Each: targets without a colour to normalise, left alone.
	QVector<int> skipped;
};

struct LevelEntityColourRequest {
	// The key to write; the commands write _color.
	QString key = levelEntityColourKey();
	// The new colour, 0-1 a channel.
	std::array<double, 3> unit {1.0, 1.0, 1.0};
};

// Writes the colour to every target as one undo step. A target whose key is
// already a colour keeps its form (0-1 or 0-255) and trailing text; a new key
// is written in fractions. Succeeds without a step when nothing changes.
bool setLevelMapSelectionEntityColour(LevelMapDocument* document, const LevelEntityColourRequest& request, LevelEntityColourReport* report = nullptr,
	QString* error = nullptr);

enum class LevelEntityColourNormalise {
	// NetRadiant Custom: the primary target's colour, normalised, goes to every
	// target. Fails when the primary has no colour under the key.
	Primary,
	// Each target normalises its own colour; targets without one are skipped.
	Each,
};

// Normalises as one undo step; the written values are fractions with each
// target's trailing text kept. Succeeds without a step when every value is
// already normalised.
bool normaliseLevelMapSelectionEntityColour(LevelMapDocument* document, LevelEntityColourNormalise mode, const QString& key = levelEntityColourKey(),
	LevelEntityColourReport* report = nullptr, QString* error = nullptr);

// Cheap checks for menus: whether the selection reaches an entity to colour,
// and whether the primary target has a colour to normalise. Hidden objects
// are not looked up here; the commands leave them out.
[[nodiscard]] bool levelMapSelectionHasEntityColourTargets(const LevelMapDocument& document);
[[nodiscard]] bool levelMapSelectionEntityColourNormalisable(const LevelMapDocument& document, const QString& key = levelEntityColourKey());

} // namespace vibestudio
