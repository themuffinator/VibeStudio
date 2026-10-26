#pragma once

// Display filters for the level views: kinds of objects the views can stop
// drawing without changing the map, the way Radiant's Filter menu and
// TrenchBroom's view options hide clip brushes, triggers or lights.
//
// A filter names objects; the views hide what any switched-off filter names,
// on top of what the user hid by hand and what hidden scene layers hold. The
// Levels View tab, the View > Filters commands and their profile keys switch
// them; `map filters` reports the same matches.
//
// World Brushes and Entities split the map as Radiant's World and Entities
// filters do: worldspawn, func_group and func_detail count as world. Point
// Entities leaves lights and models to their own filters, and Brushes names
// every brush whatever its owner, as TrenchBroom's Show Brushes does. An
// entity is a brush entity when it owns brushes or patches or the built-in
// catalogues define its class as made of brushes, so a door with no brushes
// yet is never a point entity.
//
// The texture kinds read texture names and the content bits a face carries:
// Area Portals (any areaportal face or the area portal bit, and Quake II's
// func_areaportal), Cluster Portals, Light Grid and Bot Clip (every face),
// Decals (any face under decals/) and Structural, the inverse of Detail:
// brushes with no detail face outside func_detail on Quake II and III maps,
// and worldspawn with every brush entity but func_detail* on Quake maps.
// Caulk also hides caulk faces one by one on brushes it does not hide whole,
// and on Quake maps Quake's skip counts as caulk. Hint takes brushes with a
// hint face; Skip takes brushes skipped all over and, like Caulk, hides the
// skip faces of other brushes one by one, so hiding it leaves a hint brush's
// hint face. Fog takes Quake III fog volumes by shader, content bit or name.
//
// The material kinds also read Quake III shader scripts through the material
// library (LevelViewFilterMaterials): Translucent (qer_trans, qer_alphafunc, a
// stage's alphaFunc, fog), Liquids (surfaceparm water, slime or lava), Clip
// with its Player Clip, Monster Clip and Weapon Clip parts (surfaceparm
// playerclip and monsterclip), Bot Clip (botclip, donotenter), Sky (skyparms
// or surfaceparm sky) and Nodraw (surfaceparm nodraw). On a Quake III map a
// face whose shader the package defines, or whose image it holds without a
// script, goes by that shader; other faces keep the name rules. Quake II maps
// read the face's own surface and content flags (SURF_TRANS33/66, SURF_NODRAW,
// the clip contents) and Quake maps the texture names ({fence textures, *water).
// Clip, its parts and Translucent take patches by their shader too.
// Workflow reference: NRC filters.cpp, brushmanip.cpp BrushFilters_construct,
// patchmanip.cpp PatchFilters_construct, plugins/shaders/shaders.cpp and
// plugins/entity/entity.cpp at 449778b3 (GPL-2.0-or-later) / TB ActionManager
// "View Filter > Toggle show brushes" at 4c756be1 (GPL-3.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.

#include "core/level_map.h"
#include "core/level_surface.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

class MaterialLibrary;
class PackageArchiveReader;
struct MaterialDefinition;

struct LevelViewFilter {
	QString id;
	QString title;
	QString description;
};

// What one Quake III shader says about the faces that name it, read the way
// Radiant's shader parser sets its editor flags.
struct LevelViewFilterShader {
	// qer_trans, surfaceparm fog or fogParms, or Doom 3's translucent.
	bool translucent = false;
	// qer_alphafunc, or alphaFunc in a stage.
	bool alphaTested = false;
	// surfaceparm water, slime or lava.
	bool liquid = false;
	bool playerClip = false;
	bool monsterClip = false;
	// surfaceparm botclip or donotenter.
	bool botClip = false;
	// skyParms or surfaceparm sky.
	bool sky = false;
	bool nodraw = false;
	// surfaceparm fog or fogParms.
	bool fog = false;
	// Every surfaceparm the shader declares, lower case, for the tags game or
	// project data define by surfaceparm (core/level_filter_tags.h).
	QStringList surfaceParms;
};

// The shaders of the open package, keyed by materialLookupKey of the shader
// name ("textures/base_wall/glass"). Images under textures/ that no script
// defines are there too, with no flags, as Quake III's default shader has none.
struct LevelViewFilterMaterials {
	QString sourcePath;
	QHash<QString, LevelViewFilterShader> shaders;
	int scriptCount = 0;
	// Shaders a script defines, not counting images without one.
	int scriptedShaderCount = 0;
	bool complete = true;
	bool cancelled = false;
	QStringList warnings;
	[[nodiscard]] bool isEmpty() const { return shaders.isEmpty(); }
	// The shader a map face names: "base_wall/glass" is textures/base_wall/glass.
	[[nodiscard]] const LevelViewFilterShader* find(const QString& textureName) const;
};

[[nodiscard]] LevelViewFilterShader levelViewFilterShader(const MaterialDefinition& definition);
// The winning Quake III definition of every name in a scanned library.
[[nodiscard]] LevelViewFilterMaterials levelViewFilterMaterials(const MaterialLibrary& library);
// Reads a package's Quake III shader scripts (scripts/*.shader) and the
// images under textures/ through the material library. `cancelled` is polled
// between scripts; a cancelled scan says so and holds nothing.
[[nodiscard]] LevelViewFilterMaterials scanLevelViewFilterMaterials(const PackageArchiveReader& reader,
	const std::function<bool()>& cancelled = {});
// Whether a kind reads shader scripts on Quake III maps.
[[nodiscard]] bool levelViewFilterReadsMaterials(const QString& filterId);

// Every Quake-family kind in menu order, whatever the open map's format; the
// shell gives each one a checkable command. Doom thing categories come from
// the open map's thing table, so they have none.
[[nodiscard]] QVector<LevelViewFilter> levelViewFilterKinds();
// The command that shows or hides one kind: map.filterWorld for "world",
// map.filterBrushEntities for "brush-entities"; empty for Doom categories.
[[nodiscard]] QString levelViewFilterCommandId(const QString& filterId);
// Whether a class counts as world for World Brushes and Entities, as
// Radiant counts it: worldspawn, func_group and func_detail*.
[[nodiscard]] bool levelViewFilterWorldClass(const QString& className);

// The filters that apply to a map's format, in menu order. Quake-family maps
// filter brush and entity kinds; Doom maps filter things by category.
[[nodiscard]] QVector<LevelViewFilter> levelViewFilters(const LevelMapDocument& document);
[[nodiscard]] bool levelViewFilterForId(const LevelMapDocument& document, const QString& id, LevelViewFilter* out = nullptr);

// What one filter names: whole entities (their brushes and patches go with
// them), brushes, patches, and Doom things. `materials` holds the open
// package's shaders for the material kinds on Quake III maps; without them
// those kinds go by texture names.
[[nodiscard]] QVector<LevelMapSelectionRef> levelViewFilterMatches(const LevelMapDocument& document, const QString& filterId,
	const LevelViewFilterMaterials& materials = LevelViewFilterMaterials());
// How many objects each filter names, keyed by filter id.
[[nodiscard]] QHash<QString, int> levelViewFilterCounts(const LevelMapDocument& document,
	const LevelViewFilterMaterials& materials = LevelViewFilterMaterials());
// Everything the switched-off filters name, without repeats.
[[nodiscard]] QVector<LevelMapSelectionRef> levelViewFilteredObjects(const LevelMapDocument& document, const QStringList& hiddenFilterIds,
	const LevelViewFilterMaterials& materials = LevelViewFilterMaterials());
// The faces one kind hides one at a time, on brushes it does not name whole:
// Caulk's caulk faces (and skip faces on a Quake map) and Skip's skip faces on
// otherwise textured brushes, which the camera leaves out as Radiant's caulk
// face filter does. Empty for every other kind; faces are zero-based, in
// document order.
[[nodiscard]] QVector<LevelSurfaceFace> levelViewFilterFaceMatches(const LevelMapDocument& document, const QString& filterId,
	const LevelViewFilterMaterials& materials = LevelViewFilterMaterials());
// Every face the switched-off kinds hide one at a time, leaving out brushes
// they hide whole, without repeats.
[[nodiscard]] QVector<LevelSurfaceFace> levelViewFilteredFaces(const LevelMapDocument& document, const QStringList& hiddenFilterIds,
	const LevelViewFilterMaterials& materials = LevelViewFilterMaterials());

// The switched-off list after Invert Filters: every kind the document's format
// offers flips, as Radiant's Invert filters toggles each filter in turn. Ids
// the format does not offer, kept for maps of other formats, stay as they are.
[[nodiscard]] QStringList levelViewFiltersInverted(const LevelMapDocument& document, const QStringList& hiddenFilterIds);
// The switched-off kinds the studio settings keep for the View tab, under
// this shell layout key, as a comma-separated list.
[[nodiscard]] QString levelViewFiltersSettingsKey();
[[nodiscard]] QStringList levelViewFiltersFromSaved(const QByteArray& saved);
[[nodiscard]] QByteArray levelViewFiltersSavedValue(const QStringList& hiddenFilterIds);

} // namespace vibestudio
