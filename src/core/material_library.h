#pragma once

// Every material a package or folder holds, across engines: Quake III shader
// scripts and the images they leave to the default shader, Doom 3 material
// and table decls, Doom wall textures and flats with their animations and
// switches, Quake WAD2/WAD3 textures, and Quake II WAL textures with their
// ericw-tools .wal_json sidecars.
//
// Which definition the game uses when a name is defined twice follows each
// engine: ioquake3 concatenates scripts in reverse listing order, so the
// alphabetically last script wins (and the first definition inside one
// file); Doom 3's decl manager keeps the first definition it parses, reading
// files in sorted order. The others are kept and marked as shadowed.

#include "core/material_classic.h"
#include "core/material_eval.h"
#include "core/material_model.h"
#include "core/material_script.h"
#include "core/package_archive.h"
#include "core/studio_query.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace vibestudio {

struct MaterialLibraryEntry {
	QString name;
	MaterialEngine engine = MaterialEngine::Unknown;
	// The definition's kind ("shader", "material", "implicit", "doom-wall",
	// "doom-flat", "quake-miptex", "quake2-wal", "guide").
	QString kind;
	QString sourcePath;
	QString sourceLayer;
	int line = 0;
	// Script entries: indexes into MaterialLibrary::scripts and the
	// script's materials. -1 for entries that carry their own definition.
	int scriptIndex = -1;
	int materialIndex = -1;
	// A cheap image for list thumbnails before the full render.
	QString thumbnailImage;
	bool animated = false;
	bool sky = false;
	bool light = false;
	bool fog = false;
	bool translucent = false;
	bool rejected = false;
	int stageCount = 0;
	int errors = 0;
	int warnings = 0;
	QStringList images;
	// Another definition of the same name wins in game.
	bool shadowed = false;
	QString shadowedBy;
};

struct MaterialLibraryOptions {
	// Images under textures/ that no script defines become implicit
	// Quake III or Doom 3 materials.
	bool includeImplicit = true;
	bool includeClassic = true;
	int maximumScripts = 4096;
	int maximumEntries = 200000;
	qint64 maximumScriptBytes = 32LL * 1024 * 1024;
	qint64 maximumTotalBytes = 512LL * 1024 * 1024;
	// Engines to scan; empty scans all.
	QVector<MaterialEngine> engines;
};

using MaterialLibraryProgress = std::function<bool(int done, int total, const QString& phase)>;

class MaterialLibrary {
public:
	QString sourcePath;
	QVector<MaterialScript> scripts;
	QVector<MaterialLibraryEntry> entries;
	MaterialTableSet tables;
	std::shared_ptr<const DoomMaterialCatalog> doomCatalog;
	QStringList warnings;
	bool complete = true;
	bool cancelled = false;

	// The entry the game would use for a name, preferring an engine.
	[[nodiscard]] int indexOf(const QString& name, MaterialEngine engine = MaterialEngine::Unknown) const;
	[[nodiscard]] MaterialDefinition definition(int entry) const;
	[[nodiscard]] const MaterialScript* script(int entry) const;
	[[nodiscard]] QVector<MaterialEngine> engines() const;
	[[nodiscard]] int count(MaterialEngine engine) const;
	// Entries whose images include `image` (case-insensitive, extension
	// optional).
	[[nodiscard]] QVector<int> entriesUsingImage(const QString& image) const;
	// Replaces one script (after an edit) and rebuilds its entries.
	void replaceScript(int scriptIndex, const MaterialScript& script);
	// Adds a loose script; returns its index.
	int addScript(const MaterialScript& script);
	// Adds a script read from outside the scan as the game would see it at
	// `virtualPath` (see materialScriptVirtualPath): a script already at that
	// path is replaced, so definitions keep the engine's lookup order.
	// Returns the script's index.
	int adoptScript(MaterialScript script, const QString& virtualPath);

	void rebuildEntries();

	// Entries carrying their own definition: classic textures (scriptIndex
	// -1) and implicit shaders for images no script names (scriptIndex -2).
	QVector<MaterialDefinition> ownDefinitions;
	QVector<MaterialDefinition> implicitDefinitions;
	// Candidate images for implicit materials, and their engine.
	QStringList implicitImages;
	MaterialEngine implicitEngine = MaterialEngine::Unknown;

private:
	void resolveShadowing();
};

MaterialLibrary scanMaterialLibrary(const PackageArchiveReader& reader, const MaterialLibraryOptions& options = {},
	const MaterialLibraryProgress& progress = {});

// Where the game reads a script file: its path inside `packageRoot` when it
// lies in that folder, otherwise scripts/<file> (materials/<file> for Doom 3),
// the folder the engine scans.
QString materialScriptVirtualPath(const QString& filePath, const QString& packageRoot, MaterialEngine engine);

// The engine a package most likely belongs to, from its contents.
MaterialEngine detectMaterialEngine(const PackageArchiveReader& reader);

// What the filter reads: name, engine, kind, source, stages, images,
// animated, sky, light, fog, translucent, rejected, errors, warnings,
// shadowed, and each surfaceparm and flag as its own key.
StudioQueryProperties materialLibraryEntryQueryProperties(const MaterialLibraryEntry& entry, const MaterialDefinition& definition);

QJsonObject materialLibraryEntryJson(const MaterialLibraryEntry& entry);
QJsonObject materialLibraryJson(const MaterialLibrary& library, bool includeEntries = true);
QStringList materialLibrarySummaryLines(const MaterialLibrary& library);

} // namespace vibestudio
