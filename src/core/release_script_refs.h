#pragma once

// Files that a project's game code and scripts load by name.
//
// Maps, models and shader scripts name the files they use, and the release
// planner follows them. Game code and scripts name others that matter only
// once the game runs: QuakeC precaches and plays models and sounds, Quake III
// menus load images, models, sounds, videos and other menus, configs exec
// each other and play sounds, and ZDoom-family DECORATE, ZScript, SNDINFO and
// MAPINFO name sounds, sprites, music and graphics and give actor classes
// their editor numbers.
//
// The scanner reads those sources as text and never runs anything. It keeps
// only string literals in known places (a precache call's argument, a menu
// keyword's value, a SNDINFO definition) that look like game paths or names,
// and records the QuakeC function, actor class or MAPINFO map each one sits
// in, so a map release can follow the code its entities use. Every read is
// bounded and the scan can be cancelled between scripts.
//
// Format knowledge comes from public sources (see docs/CREDITS.md, "Project
// Releases And The Game Asset Index"):
// - QuakeC builtins and the sound/ folder: the Quake GPL release,
//   WinQuake/pr_cmds.c (precache_*, setmodel, sound, ambientsound) and
//   WinQuake/snd_mem.c.
// - Quake III menus and configs: the Quake III Arena GPL release,
//   code/ui/ui_shared.c and code/ui/ui_main.c (menu keywords and loadMenu),
//   code/client/cl_cin.c (the video/ folder), code/qcommon/cmd.c (exec and
//   its .cfg default), code/client/snd_dma.c (play and music) and
//   code/renderer/tr_image.c (a .jpg for a missing .tga); ioquake3's
//   code/client/snd_codec.c (another sound format for a missing file).
// - DECORATE, ZScript, SNDINFO and MAPINFO: the ZDoom wiki pages of those
//   names; standard Doom editor numbers from the Doom Wiki thing types, with
//   the class names of the ZDoom wiki's standard editor numbers.

#include "core/package_archive.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace vibestudio {

// One asset a script names. Kind ids are stable and untranslated: sound,
// music, model, image, video, sprite, graphic, texture, script, map, file.
struct ScriptAssetReference {
	QString kind;
	// As written: "weapons/rocket1i.wav", "fireimp/sight", "FIMP".
	QString reference;
	// Game paths to try, in order. Empty when the reference is a Doom name.
	QStringList candidates;
	// A Doom lump name to look for, or a four-letter sprite prefix.
	QString lumpName;
	// A Quake III shader name to try when no candidate file exists.
	QString shaderName;
	// A ZDoom logical sound name, looked up through SNDINFO.
	QString soundName;
	// quakec, menu, config, decorate, zscript, sndinfo, mapinfo.
	QString language;
	// The script's virtual path, or "<wad>:<LUMP>" for a WAD lump.
	QString script;
	int line = 0;
	// The QuakeC function, actor class or MAPINFO map it sits in, if any.
	QString owner;
	// What names it: "precache_sound", "background", "SeeSound", "exec"...
	QString via;
};

// A QuakeC function, a ZDoom actor class, or a MAPINFO map block.
struct ScriptCodeUnit {
	// function, class or map.
	QString kind;
	QString name;
	QString language;
	QString script;
	int line = 0;
	// Every script that defines or extends it, `script` first.
	QStringList scripts;
	// Classes: the parent, the class replaced, and the DECORATE editor number.
	QString parent;
	QString replaces;
	int editorNumber = -1;
	// Functions it calls, or (for classes) every name it mentions that may be
	// another class: spawned actors, drops, projectiles, mixins.
	QStringList uses;
	// Indexes into ScriptReferenceScan::references.
	QVector<int> references;
};

// A SNDINFO definition: a logical name and the lump or file it plays, or
// the logical names a $random or $alias stands for.
struct ScriptSoundDefinition {
	QString name;
	QString target;
	QStringList aliases;
	QString script;
	int line = 0;
	// The target's ScriptAssetReference, or -1.
	int reference = -1;
};

// A MAPINFO DoomEdNums entry: an editor number given to a class.
struct ScriptEditorNumber {
	int number = 0;
	QString className;
	QString script;
	int line = 0;
};

// A script the scan read.
struct ScriptSource {
	QString label;
	QString language;
	// The script whose #include or include named this one, if any.
	QString includedBy;
	// For WAD lumps: the WAD's virtual path and the lump's directory position.
	QString wad;
	qint64 lumpOrdinal = -1;
	qint64 sizeBytes = 0;
};

struct ScriptReferenceScan {
	QVector<ScriptAssetReference> references;
	QVector<ScriptCodeUnit> units;
	QVector<ScriptSoundDefinition> sounds;
	QVector<ScriptEditorNumber> editorNumbers;
	QVector<ScriptSource> scripts;
	QStringList warnings;
	bool complete = true;
	bool cancelled = false;
	qint64 bytesRead = 0;

	// Lookups ignore case. Later definitions win, as in the engines.
	[[nodiscard]] const ScriptCodeUnit* unit(const QString& kind, const QString& name) const;
	[[nodiscard]] const ScriptSource* source(const QString& label) const;
	[[nodiscard]] const ScriptSoundDefinition* soundDefinition(const QString& name) const;
	// The class a Doom thing type spawns, from MAPINFO DoomEdNums, then
	// DECORATE headers, then the standard Doom classes. Empty when unknown.
	[[nodiscard]] QString classForEditorNumber(int number) const;
	// Rebuilds the lookup tables. Call after changing the public vectors.
	void rebuildIndex();
	// Adds units[index] to the unit lookup, as scanners do while they read.
	void indexUnit(int index);

private:
	QHash<QString, int> m_unitIndex;
	QHash<QString, int> m_sourceIndex;
	QHash<QString, int> m_soundIndex;
	QHash<int, QString> m_editorNumberIndex;
};

// A project WAD whose DECORATE, ZSCRIPT, SNDINFO, MAPINFO and ZMAPINFO lumps
// are scanned, and whose lumps Doom names resolve against.
struct ScriptWadSource {
	// The WAD's virtual path in the project.
	QString label;
	std::shared_ptr<const PackageArchiveReader> reader;
};

struct ScriptReferenceScanOptions {
	// quake, quake2, quake3, doom, heretic-hexen...: decides where a config's
	// play command looks first and whether map commands count.
	QString gameKey;
	qint64 maximumScriptBytes = 8ll * 1024 * 1024;
	qint64 maximumTotalBytes = 96ll * 1024 * 1024;
	int maximumScripts = 4096;
	int maximumReferences = 200000;
	int maximumIncludeDepth = 16;
	// Called before each script; returning false cancels.
	std::function<bool(int completed, int total)> progress;
	std::function<bool()> isCancelled;
};

// Reads every script in the project: QuakeC (*.qc, *.qh), Quake III menus
// (*.menu, and ui/*.txt menu lists), configs (*.cfg, *.rc), root DECORATE,
// ZSCRIPT, SNDINFO, MAPINFO and ZMAPINFO files with what they include, and
// the same lumps inside the given WADs.
ScriptReferenceScan scanScriptReferences(const PackageArchiveReader& files, const QVector<ScriptWadSource>& wads = {},
	const ScriptReferenceScanOptions& options = {});

// One script's text, for tests and tools: references, units and definitions
// are appended to `scan`, and includes to `includes` (as written).
void scanScriptText(const QString& language, const QString& text, const QString& label, const ScriptReferenceScanOptions& options,
	ScriptReferenceScan* scan, QStringList* includes = nullptr);

// The script language a project file is read as, or empty.
QString scriptLanguageForPath(const QString& virtualPath);
// The script language of a WAD lump name (DECORATE, SNDINFO...), or empty.
QString scriptLanguageForLump(const QString& lumpName);

// What a map's code needs: the references reached, the functions or classes
// that reach them, and the scripts that define those (with their includers).
struct ScriptReachable {
	QVector<int> references;
	QStringList units;
	QStringList scripts;
};

// QuakeC: the spawn functions of the map's entity classes that the project
// defines, and every project function they call, up to a bounded depth.
ScriptReachable scriptReferencesForEntityClasses(const ScriptReferenceScan& scan, const QStringList& classNames);

// ZDoom family: the classes the map's thing types spawn (editor numbers,
// replacements), their parents and the classes they name, followed up to a
// bounded count; the logical sounds they use through SNDINFO; and the
// MAPINFO blocks of the named maps and the default map.
ScriptReachable scriptReferencesForDoomThings(const ScriptReferenceScan& scan, const QSet<int>& thingTypes, const QStringList& mapNames);

// SNDINFO targets a logical sound plays, following $random and $alias; the
// result indexes ScriptReferenceScan::references. `scripts` receives the
// SNDINFO scripts involved.
QVector<int> scriptSoundTargets(const ScriptReferenceScan& scan, const QString& logicalName, QStringList* scripts = nullptr);

// The standard Doom and Doom II class a thing type spawns, or empty.
QString doomStandardClassForEditorNumber(int number);

} // namespace vibestudio
