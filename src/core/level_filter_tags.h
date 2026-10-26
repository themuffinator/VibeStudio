#pragma once

// View filters that data defines, on top of the built-in kinds of
// core/level_view_filters.h: tags a game or project defines by texture
// pattern, surfaceparm, content or surface flag, or brush entity class, as
// TrenchBroom's game configurations define smart tags, and one filter for
// each entity class, as TrenchBroom's entity definition list hides a class.
//
// Tags read TrenchBroom's own "tags" layout, so a game configuration's tags
// can be used as they are:
//
//   { "brush": [ { "name": "Trigger", "match": "classname", "pattern": "trigger*" } ],
//     "brushface": [ { "name": "Fog", "match": "surfaceparm", "pattern": "fog" },
//                    { "name": "Trans", "match": "surfaceflag", "flags": [ "trans33", "trans66" ] } ] }
//
// A whole game configuration also works: its "tags" object is read, and its
// "faceattribs" lists name the flag bits in bit order. Without them flag
// names are the map game's (core/level_face_flags.h); numbers work too. A
// plain array of tag objects, with the scope taken from "match", is accepted
// as well. Tag files may be written as TrenchBroom writes its game
// configurations, which are not strict JSON: '//' and '/* */' comments, bare
// names such as modelscale, a comma before a closing bracket, and a
// backslash JSON does not define kept as written, so "\**" is an escaped '*'
// and then any run. Texture patterns match the texture's own name, or its path below
// textures/ when the pattern has a '/'; '*' and '?' are wildcards, '\'
// escapes, and case is ignored. "attribs" is ignored; "texture" is retained
// for classname smart-tag authoring (level_smart_tags.h). Filtering itself
// never edits the map.
//
// A face tag hides a brush whose every face matches, and in the camera the
// matching faces of other brushes one by one, as TrenchBroom hides tagged
// faces; patches go by their texture or shader. A brush whose faces are all
// hidden one by one, under several tags or kinds together, is hidden whole.
// A class tag hides the brush entities of matching classes with their
// brushes. A class filter hides every entity of one class, those added later
// included.
//
// Filter ids are "tag:<slug>" and "class:<class name in lower case>"; a class
// whose name holds a ',' gets no filter, as ',' separates the saved and
// command-line lists. Tag
// ids are kept with the built-in kinds in the View tab's saved list; class
// ids last only for the map session. Each gets a checkable command,
// map.filterTag.<slug> and map.filterClass.<class>, that users can bind.
//
// Workflow reference: TB ActionManager::createTagActions and
// createEntityDefinitionActions, MapViewBase::toggleTagVisible and
// toggleEntityDefinitionVisible, and ViewEditor's tag and entity definition
// lists at 4c756be1 (GPL-3.0-or-later). Original Qt implementation; see
// docs/CREDITS.md.

#include "core/entity_definitions.h"
#include "core/level_map.h"
#include "core/level_surface.h"
#include "core/level_view_filters.h"

#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct ProjectManifest;

enum class LevelFilterTagMatch {
	// The texture's name or path matches a pattern.
	Texture,
	// The face's Quake III shader declares a surfaceparm.
	SurfaceParm,
	// The face has any of the content bits.
	ContentFlag,
	// The face has any of the surface bits.
	SurfaceFlag,
	// A brush entity's class matches a pattern.
	ClassName,
};

struct LevelFilterTag {
	// "tag:" and the name in lower case with '-' for anything but letters and
	// digits: "tag:fog-volume".
	QString id;
	QString name;
	LevelFilterTagMatch match = LevelFilterTagMatch::Texture;
	// Texture or class patterns, or surfaceparm names, lower case.
	QStringList patterns;
	// Optional material applied when enabling a classname tag.
	QString texture;
	// Flag names, resolved when matching, and flag numbers.
	QStringList flagNames;
	quint32 flagBits = 0;
	// The data's own flag names in bit order, when it lists them.
	QStringList contentFlagNames;
	QStringList surfaceFlagNames;
	// The file the tag came from.
	QString source;
};

struct LevelFilterTagSet {
	QVector<LevelFilterTag> tags;
	QStringList sources;
	QStringList warnings;
	[[nodiscard]] bool isEmpty() const { return tags.isEmpty(); }
	[[nodiscard]] const LevelFilterTag* find(const QString& id) const;
	[[nodiscard]] QStringList ids() const;
};

// At most this many tags in one set, as TrenchBroom keeps hidden tags in one
// 64-bit mask.
inline constexpr int kLevelFilterTagLimit = 64;
// Tag files larger than this are refused.
inline constexpr qint64 kLevelFilterTagFileLimit = 4 * 1024 * 1024;

// The project manifest key that holds a project's tags, or names a tag file
// relative to the project folder.
QString levelFilterTagsManifestKey();
// Reads tags from JSON in any of the layouts above, adding them to `set`.
// Tags with problems are skipped with a warning; false, with a reason, when
// the value holds no tag layout at all.
bool parseLevelFilterTags(const QJsonValue& value, const QString& source, LevelFilterTagSet* set, QString* error = nullptr);
// Reads the text of a tag file: JSON, or a TrenchBroom game configuration
// with its comments and bare names; a parse error names its line.
bool parseLevelFilterTagText(const QByteArray& text, const QString& source, LevelFilterTagSet* set, QString* error = nullptr);
// Reads a tag file or a TrenchBroom game configuration.
bool loadLevelFilterTagFile(const QString& path, LevelFilterTagSet* set, QString* error = nullptr);
// The tags a project's manifest holds or names; empty, with any problem in
// its warnings, when it has none.
LevelFilterTagSet levelFilterTagsForProject(const ProjectManifest& manifest);
// Adds the tags of `from` whose ids `into` does not have yet; a repeat stays
// out with a warning.
void mergeLevelFilterTags(LevelFilterTagSet* into, const LevelFilterTagSet& from);

QString levelFilterTagId(const QString& name);
[[nodiscard]] bool isLevelFilterTagId(const QString& id);
// map.filterTag.<slug>; empty for an id that is not a tag id.
QString levelFilterTagCommandId(const QString& tagId);
// The tag id a generated command names, from either spelling the registry
// accepts ("map.filterTag.fog" or "map.filter-tag.fog").
bool levelFilterTagForCommand(const QString& commandId, QString* tagId = nullptr);

QString levelFilterClassId(const QString& className);
bool levelFilterClassForId(const QString& id, QString* className = nullptr);
// map.filterClass.<class>; empty for a name a map cannot hold.
QString levelFilterClassCommandId(const QString& className);
bool levelFilterClassForCommand(const QString& commandId, QString* className = nullptr);

// What the tag matches, translated, for tooltips and the CLI.
QString levelFilterTagDescription(const LevelFilterTag& tag);
// Shared with smart-tag authoring: test a proposed material or classname.
bool levelFilterTagMatchesName(const LevelFilterTag& tag, const QString& name, const LevelViewFilterMaterials& materials = {});
// The tag's flag bits on this map: its numbers, its names looked up in the
// data's own lists or else the map game's. Names neither knows go to
// `unresolved`.
quint32 levelFilterTagFlagBits(const LevelFilterTag& tag, const LevelMapDocument& document, QStringList* unresolved = nullptr);
// Whether the map's format takes tags and class filters: Quake-family maps.
bool levelFilterTagsApply(const LevelMapDocument& document);
// Brushes whose every face matches, patches that match and, for a class tag,
// brush entities; `materials` holds the package's shaders for surfaceparm
// tags on Quake III maps.
QVector<LevelMapSelectionRef> levelFilterTagMatches(const LevelMapDocument& document, const LevelFilterTag& tag,
	const LevelViewFilterMaterials& materials = LevelViewFilterMaterials());
// Matching faces of brushes the tag does not hide whole, zero-based.
QVector<LevelSurfaceFace> levelFilterTagFaceMatches(const LevelMapDocument& document, const LevelFilterTag& tag,
	const LevelViewFilterMaterials& materials = LevelViewFilterMaterials());

struct LevelFilterClass {
	QString className;
	// "class:" and the class in lower case.
	QString id;
	// The part before the first '_', lower case, as TrenchBroom groups them.
	QString group;
	EntityClassKind kind = EntityClassKind::Unknown;
	// Entities of the class in the map.
	int count = 0;
	// Whether the definitions name it, rather than only the map.
	bool defined = false;
};
// Every point and brush class of the definitions, and every class the map
// uses, worldspawn aside, one per class whatever its case, sorted ignoring
// case.
QVector<LevelFilterClass> levelFilterClasses(const LevelMapDocument& document, const EntityDefinitionCatalogue& catalogue);
// Every entity of the class, whatever its case.
QVector<LevelMapSelectionRef> levelFilterClassMatches(const LevelMapDocument& document, const QString& className);

// Everything a switched-off list hides: built-in kinds, tags and classes. A
// brush whose faces would all go one by one, under several kinds or tags
// together, is among the objects instead.
struct LevelViewFilterHidden {
	QVector<LevelMapSelectionRef> objects;
	// Faces the camera leaves out one by one, on brushes not hidden whole.
	QVector<LevelSurfaceFace> faces;
};
LevelViewFilterHidden levelViewFilterHidden(const LevelMapDocument& document, const QStringList& hiddenIds,
	const LevelViewFilterMaterials& materials = LevelViewFilterMaterials(), const LevelFilterTagSet& tags = LevelFilterTagSet());
// The list without class ids, which last only for the map session.
QStringList levelViewFiltersWithoutClasses(const QStringList& hiddenIds);
// The list after Invert Filters flips every tag of the set as well.
QStringList levelFilterTagsInverted(const LevelFilterTagSet& tags, const QStringList& hiddenIds);

} // namespace vibestudio
