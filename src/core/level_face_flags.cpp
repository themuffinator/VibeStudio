// Per-face content and surface flags and value. Workflow reference: NRC
// radiant/surfacedialog.cpp SurfaceInspector::ApplyFlags and OnBtnUnsetFlags
// and brush.h ContentsFlagsValue_assignMasked at 449778b3 (GPL-2.0-or-later),
// and TB FaceAttribsEditor at 4c756be1 (GPL-3.0-or-later). The flag values and
// names are facts from id Software's Quake II qfiles.h and q_shared.h and
// Quake III code/game/surfaceflags.h (GPL-2.0); no code is copied. Original Qt
// implementation; see docs/CREDITS.md.

#include "core/level_face_flags.h"

#include "core/entity_builtin_catalogue.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>

namespace vibestudio {

namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioLevelFaceFlags)
};

struct FlagEntry {
	quint32 bit;
	const char* id;
	const char* label;
	const char* description;
};

// Quake III's surfaceflags.h, CONTENTS_*.
constexpr FlagEntry kQuake3Contents[] = {
	{0x1, "solid", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Solid"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Blocks movement.")},
	{0x8, "lava", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Lava"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Burning liquid.")},
	{0x10, "slime", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Slime"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Harmful liquid.")},
	{0x20, "water", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Water"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Swimmable liquid.")},
	{0x40, "fog", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Fog"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "A fog volume.")},
	{0x80, "notteam1", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Not team 1"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Blocks the first team.")},
	{0x100, "notteam2", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Not team 2"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Blocks the second team.")},
	{0x200, "nobotclip", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No bot clip"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Bots ignore its clipping.")},
	{0x8000, "areaportal", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Area portal"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Splits areas where a door closes.")},
	{0x10000, "playerclip", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Player clip"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Blocks players only.")},
	{0x20000, "monsterclip", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Monster clip"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Blocks monsters only.")},
	{0x40000, "teleporter", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Teleporter"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "A teleporter for bot routes.")},
	{0x80000, "jumppad", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Jump pad"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "A jump pad for bot routes.")},
	{0x100000, "clusterportal", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Cluster portal"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Splits bot route clusters.")},
	{0x200000, "donotenter", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Do not enter"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Bots stay out.")},
	{0x400000, "botclip", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Bot clip"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Blocks bots only.")},
	{0x800000, "mover", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Mover"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Part of a moving brush.")},
	{0x1000000, "origin", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Origin"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Marks a brush model's rotation origin.")},
	{0x2000000, "body", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Body"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Used by the game for bodies, not brushes.")},
	{0x4000000, "corpse", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Corpse"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Used by the game for corpses, not brushes.")},
	{0x8000000, "detail", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Detail"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Left out of the visibility split.")},
	{0x10000000, "structural", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Structural"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Used for the visibility split.")},
	{0x20000000, "translucent", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Translucent"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Does not hide what is behind it.")},
	{0x40000000, "trigger", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Trigger"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "A trigger volume.")},
	{0x80000000, "nodrop", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No drop"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Items and bodies are removed here.")},
};

// Quake III's surfaceflags.h, SURF_*.
constexpr FlagEntry kQuake3Surface[] = {
	{0x1, "nodamage", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No damage"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No falling damage.")},
	{0x2, "slick", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Slick"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Reduced friction for players.")},
	{0x4, "sky", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Sky"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Lit from the environment as sky.")},
	{0x8, "ladder", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Ladder"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Climbable.")},
	{0x10, "noimpact", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No impact"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Missiles do not explode against it.")},
	{0x20, "nomarks", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No marks"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Missiles leave no marks.")},
	{0x40, "flesh", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Flesh"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Flesh sounds and effects.")},
	{0x80, "nodraw", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No draw"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Never drawn.")},
	{0x100, "hint", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Hint"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Compiler hint for the visibility split.")},
	{0x200, "skip", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Skip"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Ignored by the compiler.")},
	{0x400, "nolightmap", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No lightmap"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Drawn without a lightmap.")},
	{0x800, "pointlight", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Point light"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Lit at its vertices.")},
	{0x1000, "metalsteps", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Metal steps"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Clanking footsteps.")},
	{0x2000, "nosteps", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No steps"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Silent footsteps.")},
	{0x4000, "nonsolid", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Non-solid"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Nothing collides with it.")},
	{0x8000, "lightfilter", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Light filter"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Filters light passing through when lighting.")},
	{0x10000, "alphashadow", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Alpha shadow"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Casts shadows by its alpha channel.")},
	{0x20000, "nodlight", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "No dynamic light"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Dynamic lights do not reach it.")},
	{0x40000, "dust", QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Dust"), QT_TRANSLATE_NOOP("VibeStudioLevelFaceFlags", "Leaves a dust trail when walked on.")},
};

template <std::size_t N>
QVector<MaterialFlagDescriptor> descriptors(const FlagEntry (&entries)[N])
{
	QVector<MaterialFlagDescriptor> result;
	result.reserve(static_cast<qsizetype>(N));
	for (const FlagEntry& entry : entries) {
		result.push_back({entry.bit, QString::fromLatin1(entry.id), QCoreApplication::translate("VibeStudioLevelFaceFlags", entry.label),
			QCoreApplication::translate("VibeStudioLevelFaceFlags", entry.description)});
	}
	return result;
}

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

// The low 32 bits of a stored flag word: maps write them signed or unsigned.
quint32 word(qint64 stored)
{
	return static_cast<quint32>(static_cast<quint64>(stored) & 0xffffffffULL);
}

// A changed word, written as a signed 32-bit number, as NRC's brush writer
// and TrenchBroom write face flags. Bit 31 therefore reads back negative
// (-2147483648), which every compiler parses: VibeMap2 (from ericw-tools)
// reads the numbers with std::stoi, which throws on 2147483648, and q3map2 reads
// contents with atoi, which on Windows saturates 2147483648 to 0x7fffffff and
// so adds detail and every other bit. An unchanged word keeps its stored text.
qint64 stored(qint64 before, quint32 bits)
{
	if (word(before) == bits) {
		return before;
	}
	return static_cast<qint64>(static_cast<qint32>(bits));
}

QString objectName(LevelMapSelectionKind kind, int id)
{
	return levelMapSelectionRefId({kind, id});
}

} // namespace

LevelFaceFlagGame levelFaceFlagGame(const LevelMapDocument& document)
{
	if (document.format == LevelMapFormat::Quake3Map) {
		return LevelFaceFlagGame::Quake3;
	}
	if (document.format != LevelMapFormat::QuakeMap) {
		return LevelFaceFlagGame::None;
	}
	// Optional Quake II face fields can be absent. Use explicit game headers
	// and entity classes, but omit the catalogue's soft texture-folder hint:
	// modern Quake maps also use folders, so it cannot choose edit semantics.
	LevelMapDocument evidence;
	evidence.format = document.format;
	evidence.originalText = document.originalText;
	evidence.entities = document.entities;
	return levelMapUsesFaceFlags(document) || builtinEntityGameForMap(evidence) == BuiltinEntityGame::Quake2 ? LevelFaceFlagGame::Quake2
																											   : LevelFaceFlagGame::None;
}

QVector<MaterialFlagDescriptor> levelFaceFlagNames(LevelFaceFlagGame game, LevelFaceFlagWord word)
{
	switch (game) {
	case LevelFaceFlagGame::Quake2:
		return word == LevelFaceFlagWord::Content ? quake2ContentFlagDescriptors() : quake2SurfaceFlagDescriptors();
	case LevelFaceFlagGame::Quake3:
		return word == LevelFaceFlagWord::Content ? descriptors(kQuake3Contents) : descriptors(kQuake3Surface);
	case LevelFaceFlagGame::None:
		break;
	}
	return {};
}

QStringList levelFaceFlagIds(LevelFaceFlagGame game, LevelFaceFlagWord word, quint32 bits)
{
	QStringList ids;
	quint32 named = 0;
	for (const MaterialFlagDescriptor& flag : levelFaceFlagNames(game, word)) {
		named |= flag.bit;
		if ((bits & flag.bit) != 0) {
			ids << flag.id;
		}
	}
	if (const quint32 other = bits & ~named; other != 0) {
		ids << QStringLiteral("0x%1").arg(other, 0, 16);
	}
	return ids;
}

bool parseLevelFaceFlagBits(LevelFaceFlagGame game, LevelFaceFlagWord word, const QString& text, quint32* bits, QString* error)
{
	if (!bits) {
		return fail(error, Text::tr("Missing flag result."));
	}
	*bits = 0;
	const QVector<MaterialFlagDescriptor> names = levelFaceFlagNames(game, word);
	const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
	if (parts.isEmpty()) {
		return fail(error, Text::tr("Give flag names or numbers, separated by commas."));
	}
	for (const QString& raw : parts) {
		const QString part = raw.trimmed().toLower();
		bool ok = false;
		const qulonglong number = part.startsWith(QLatin1String("0x")) ? part.mid(2).toULongLong(&ok, 16) : part.toULongLong(&ok, 10);
		if (ok) {
			if (number > 0xffffffffULL) {
				return fail(error, Text::tr("%1 does not fit a 32-bit flag word.").arg(raw.trimmed()));
			}
			*bits |= static_cast<quint32>(number);
			continue;
		}
		const auto found = std::find_if(names.cbegin(), names.cend(), [&part](const MaterialFlagDescriptor& flag) { return flag.id == part; });
		if (found == names.cend()) {
			QStringList known;
			for (const MaterialFlagDescriptor& flag : names) {
				known << flag.id;
			}
			return fail(error, known.isEmpty() ? Text::tr("This map's faces have no named flags: %1.").arg(raw.trimmed())
											   : Text::tr("Unknown flag %1. Known flags: %2.").arg(raw.trimmed(), known.join(QStringLiteral(", "))));
		}
		*bits |= found->bit;
	}
	return true;
}

LevelFaceFlagsSummary levelFaceFlagsSummary(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces)
{
	LevelFaceFlagsSummary summary;
	if (faces.isEmpty()) {
		return summary;
	}
	QHash<int, const LevelMapBrush*> brushes;
	for (const LevelMapBrush& brush : document.brushes) {
		brushes.insert(brush.id, &brush);
	}
	QSet<QPair<int, int>> seen;
	bool first = true;
	for (const LevelSurfaceFace& target : faces) {
		const LevelMapBrush* brush = brushes.value(target.brushId);
		if (!brush || target.faceIndex < 0 || target.faceIndex >= brush->faces.size() || seen.contains({target.brushId, target.faceIndex})) {
			continue;
		}
		seen.insert({target.brushId, target.faceIndex});
		const LevelMapBrushFace& face = brush->faces[target.faceIndex];
		const quint32 content = word(face.contentFlags), surface = word(face.surfaceFlags);
		if (first) {
			summary.contentAll = content;
			summary.surfaceAll = surface;
			summary.value = face.surfaceValue;
			first = false;
		} else {
			summary.contentAll &= content;
			summary.surfaceAll &= surface;
			summary.valueMixed |= face.surfaceValue != summary.value;
		}
		summary.contentAny |= content;
		summary.surfaceAny |= surface;
		++summary.faces;
	}
	return summary;
}

LevelFaceFlagsEdit LevelFaceFlagsEdit::clearAll()
{
	LevelFaceFlagsEdit edit;
	edit.clearContent = ~kLevelFaceDetailContent;
	edit.clearSurface = 0xffffffffU;
	edit.setValue = true;
	edit.value = 0;
	return edit;
}

bool prepareLevelFaceFlagsEdit(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces, const LevelFaceFlagsEdit& edit,
	LevelSurfaceEditPlan* plan, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!plan) {
		return fail(error, Text::tr("Missing face flags plan."));
	}
	if (levelFaceFlagGame(document) == LevelFaceFlagGame::None) {
		return fail(error, Text::tr("Face flags belong to Quake II and Quake III maps; this map's faces carry none."));
	}
	if (!edit.changesSomething()) {
		return fail(error, Text::tr("Choose flags to set or clear, or a value, before changing face flags."));
	}
	if (((edit.setContent | edit.clearContent) & kLevelFaceDetailContent) != 0) {
		return fail(error, Text::tr("The detail flag belongs to whole brushes: use Make Detail or Make Structural (map detail) instead."));
	}
	if ((edit.setContent & edit.clearContent) != 0 || (edit.setSurface & edit.clearSurface) != 0) {
		return fail(error, Text::tr("A flag cannot be set and cleared at once."));
	}
	if (edit.setValue && (edit.value < std::numeric_limits<qint32>::min() || edit.value > std::numeric_limits<qint32>::max())) {
		return fail(error, Text::tr("The surface value must fit a signed 32-bit map field, from -2147483648 to 2147483647."));
	}
	if (faces.isEmpty()) {
		return fail(error, Text::tr("Select brushes or pick faces to change their flags."));
	}
	if (faces.size() > 262144) {
		return fail(error, Text::tr("Face flags change at most 262,144 faces at once."));
	}
	QHash<int, int> indexes;
	for (int i = 0; i < document.brushes.size(); ++i) {
		indexes.insert(document.brushes[i].id, i);
	}
	// Every face must exist and be editable, or nothing changes.
	const QSet<QString> hidden = levelMapHiddenObjects(document);
	const QSet<QString> locked = levelSceneLockedObjects(document);
	std::optional<LevelSceneSelectionContext> context;
	if (!document.openSceneGroups.isEmpty()) {
		context = levelSceneSelectionContext(document);
	}
	QMap<int, QSet<int>> targets;
	for (const LevelSurfaceFace& face : faces) {
		const int index = indexes.value(face.brushId, -1);
		if (index < 0 || face.faceIndex < 0 || face.faceIndex >= document.brushes[index].faces.size()) {
			return fail(error, Text::tr("Brush %1 has no face %2.").arg(face.brushId).arg(face.faceIndex + 1));
		}
		targets[face.brushId].insert(face.faceIndex);
	}
	for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
		const LevelMapBrush& brush = document.brushes[indexes.value(it.key())];
		const QString self = objectName(LevelMapSelectionKind::QuakeBrush, brush.id);
		const QString owner = objectName(LevelMapSelectionKind::Entity, brush.entityId);
		if (hidden.contains(self) || hidden.contains(owner)) {
			return fail(error, Text::tr("Brush %1 is hidden. Show it before changing its face flags.").arg(brush.id));
		}
		if (locked.contains(self) || locked.contains(owner)) {
			return fail(error, Text::tr("Brush %1 is in a locked layer or group. Unlock it before changing its face flags.").arg(brush.id));
		}
		if (context && !context->openGroup.isEmpty() && !context->allowedObjects.contains(self)) {
			return fail(error, Text::tr("Brush %1 is outside the open group. Close the group before changing its face flags.").arg(brush.id));
		}
	}

	LevelSurfaceEditPlan result;
	result.m_sourcePath = document.sourcePath;
	result.m_sourceHash = document.sourceContentHash;
	result.m_revision = document.revision;
	result.m_format = document.format;
	result.m_flags = true;
	for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
		const LevelMapBrush& brush = document.brushes[indexes.value(it.key())];
		LevelMapBrush after = brush;
		int changed = 0;
		for (const int index : *it) {
			LevelMapBrushFace& face = after.faces[index];
			// The detail bit was refused above, so it stays as it is, as NRC's
			// ContentsFlagsValue_assignMasked keeps it.
			const quint32 content = (word(face.contentFlags) | edit.setContent) & ~edit.clearContent;
			const quint32 surface = (word(face.surfaceFlags) | edit.setSurface) & ~edit.clearSurface;
			const qint64 value = edit.setValue ? edit.value : face.surfaceValue;
			if (content == word(face.contentFlags) && surface == word(face.surfaceFlags) && value == face.surfaceValue) {
				continue;
			}
			face.contentFlags = stored(face.contentFlags, content);
			face.surfaceFlags = stored(face.surfaceFlags, surface);
			face.surfaceValue = value;
			face.flagsWritten = true;
			face.textureParametersDirty = true;
			++changed;
		}
		if (changed == 0) {
			continue;
		}
		after.textureParametersDirty = true;
		result.m_before.append(brush);
		result.m_after.append(after);
		result.m_faceCount += changed;
	}
	if (result.m_faceCount == 0) {
		return fail(error, Text::tr("The targeted faces already have these flags; nothing changed."));
	}
	result.m_ready = true;
	*plan = std::move(result);
	return true;
}

bool setLevelMapFaceFlags(LevelMapDocument* document, const QVector<LevelSurfaceFace>& faces, const LevelFaceFlagsEdit& edit,
	LevelFaceFlagsReport* report, QString* error)
{
	if (!document) {
		return fail(error, Text::tr("Missing map."));
	}
	LevelSurfaceEditPlan plan;
	if (!prepareLevelFaceFlagsEdit(*document, faces, edit, &plan, error) || !commitLevelSurfaceEdit(document, plan, error)) {
		return false;
	}
	if (report) {
		QSet<QPair<int, int>> unique;
		for (const LevelSurfaceFace& face : faces) {
			unique.insert({face.brushId, face.faceIndex});
		}
		report->faces = static_cast<int>(unique.size());
		report->changedFaces = plan.faceCount();
		report->brushes = plan.brushCount();
	}
	return true;
}

} // namespace vibestudio
