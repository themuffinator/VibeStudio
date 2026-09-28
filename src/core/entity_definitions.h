#pragma once

// Entity definition catalogues.
//
// A map is not just geometry: every point and brush entity has a classname, a
// set of keys, and spawnflags whose meaning lives outside the map file. Without
// a catalogue the studio can show a `classname` string but cannot say whether it
// is real, what its keys mean, or which spawnflag bit is which.
//
// Formats parsed, all of them plain text and all publicly documented:
// - Radiant `.def`: the `/*QUAKED <classname> (r g b) (mins) (maxs) FLAGS`
//   comment block used by GtkRadiant, NetRadiant and q3map2
//   (https://github.com/TTimo/GtkRadiant). QuakeC `.qc` sources carry the same
//   block, so they parse through the same path.
// - Valve `.fgd`: the Forge Game Data format used by Hammer, TrenchBroom and
//   J.A.C.K. (https://developer.valvesoftware.com/wiki/FGD).
// - Quake III `.ent`: the entity definition list q3map2 emits.
//
// Nothing here ships a game's definitions; the studio reads whatever the user
// points it at.

#include "core/level_map.h"
#include "core/operation_state.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class EntityDefinitionFormat {
	Unknown,
	RadiantDef,   // /*QUAKED ... */ blocks, in .def or .qc
	ValveFgd,
	Quake3Ent,
};

enum class EntityClassKind {
	Unknown,
	Point,
	Brush,
	Base,    // an FGD @BaseClass, inherited but never placed
};

enum class EntityKeyType {
	String,
	Integer,
	Real,
	Choices,
	Flags,
	TargetSource,
	TargetDestination,
	Color,
	Vector,
	Angle,
	Sound,
	Model,
	Texture,
	Boolean,
};

struct EntityKeyChoice {
	QString value;
	QString label;
};

struct EntityKeyDefinition {
	QString key;
	EntityKeyType type = EntityKeyType::String;
	QString typeId;
	QString displayName;
	QString description;
	QString defaultValue;
	bool required = false;
	QVector<EntityKeyChoice> choices;
};

struct EntitySpawnflagDefinition {
	// Bit index, 0-31. The map stores the value as a summed integer.
	int bit = 0;
	QString name;
	QString description;
	bool defaultOn = false;
};

struct EntityClassDefinition {
	QString className;
	EntityClassKind kind = EntityClassKind::Unknown;
	QString description;
	QStringList baseClasses;
	QVector<EntityKeyDefinition> keys;
	QVector<EntitySpawnflagDefinition> spawnflags;
	// Editor hints. `hasSize` is false when the format gave no bounding box.
	bool hasSize = false;
	double mins[3] = {-8.0, -8.0, -8.0};
	double maxs[3] = {8.0, 8.0, 8.0};
	bool hasColor = false;
	int color[3] = {220, 220, 220};
	QString modelHint;
	QString sourcePath;
	int sourceLine = 0;

	[[nodiscard]] bool keyForName(const QString& key, EntityKeyDefinition* out = nullptr) const;
};

struct EntityDefinitionCatalogue {
	QStringList sourcePaths;
	QVector<EntityDefinitionFormat> sourceFormats;
	// Sorted by class name, with base classes already folded into the classes
	// that inherit them.
	QVector<EntityClassDefinition> classes;
	int pointClassCount = 0;
	int brushClassCount = 0;
	int baseClassCount = 0;
	QStringList warnings;
	QString error;

	[[nodiscard]] bool isEmpty() const;
	[[nodiscard]] bool classForName(const QString& className, EntityClassDefinition* out = nullptr) const;
	[[nodiscard]] QStringList classNames() const;
};

enum class EntityIssueSeverity {
	Info,
	Warning,
	Error,
};

struct EntityValidationIssue {
	EntityIssueSeverity severity = EntityIssueSeverity::Warning;
	QString code;
	QString message;
	int entityId = -1;
	QString className;
	QString key;
	int line = 0;
};

struct EntityValidationReport {
	QString mapName;
	int entityCount = 0;
	int knownClassCount = 0;
	int unknownClassCount = 0;
	int issueCount = 0;
	int warningCount = 0;
	int errorCount = 0;
	// Classnames used by the map that the catalogue does not define.
	QStringList unknownClassNames;
	// Targets referenced with no matching targetname, and the reverse.
	QStringList danglingTargets;
	QStringList unreachableTargetNames;
	QVector<EntityValidationIssue> issues;
	QStringList warnings;

	[[nodiscard]] OperationState state() const;
};

QString entityDefinitionFormatId(EntityDefinitionFormat format);
QString entityDefinitionFormatDisplayName(EntityDefinitionFormat format);
QString entityClassKindId(EntityClassKind kind);
QString entityKeyTypeId(EntityKeyType type);
EntityKeyType entityKeyTypeFromId(const QString& id);

EntityDefinitionFormat detectEntityDefinitionFormat(const QString& path, const QByteArray& bytes);

// Parses one file. `format` may be Unknown to auto-detect.
EntityDefinitionCatalogue parseEntityDefinitions(const QString& path, const QByteArray& bytes, EntityDefinitionFormat format = EntityDefinitionFormat::Unknown);

// Loads every `.def`, `.fgd`, `.ent` and `.qc` file under the given paths
// (files or directories) and merges them, resolving FGD base-class inheritance.
EntityDefinitionCatalogue loadEntityDefinitions(const QStringList& paths, bool recursive = true);

// Ordered directories a project would keep definitions in, relative to a
// project root: used so the studio can find them without being told.
QStringList entityDefinitionSearchPaths(const QString& projectRootPath);

EntityValidationReport validateLevelMapEntities(const LevelMapDocument& document, const EntityDefinitionCatalogue& catalogue);

// Human-readable help for one key of one class, for the entity inspector.
QString entityKeyHelpText(const EntityClassDefinition& definition, const QString& key);
QStringList entityClassSummaryLines(const EntityClassDefinition& definition);
QStringList entityValidationLines(const EntityValidationReport& report);
QString entityValidationText(const EntityValidationReport& report);
QJsonObject entityDefinitionCatalogueJson(const EntityDefinitionCatalogue& catalogue);
QJsonObject entityValidationReportJson(const EntityValidationReport& report);

} // namespace vibestudio
