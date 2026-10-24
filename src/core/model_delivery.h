#pragma once

// Where a model export's delivery target comes from when the author does not
// name one: the choices saved with the project, then what the project's game
// installation suggests from its engine, then the format's default target
// (core/model_targets.h). The strict-export setting (refuse exports that leave
// data out) is saved and resolved the same way.
//
// A project keeps its choices in its manifest under the top-level key
// "modelDelivery", which ProjectManifest keeps as an unknown key, so older
// studios leave it untouched:
//   "modelDelivery": {"targets": {"md3": "ioquake3", "mdl": "quakespasm"}, "strict": true}

#include "core/game_installation.h"
#include "core/project_manifest.h"

#include <QJsonObject>
#include <QMap>
#include <QString>

namespace vibestudio
{
struct ModelDeliveryPreferences
{
	// Export format (mdl, md2, md3, iqm, md5mesh, md5anim) to target id.
	QMap<QString, QString> targets;
	bool strictSet = false;
	bool strict = false;
	[[nodiscard]] bool isEmpty() const { return targets.isEmpty() && !strictSet; }
	bool operator==(const ModelDeliveryPreferences &) const = default;
};

enum class ModelDeliveryOrigin
{
	// Named for this export (--target, or chosen in the editor).
	Explicit,
	Project,
	Installation,
	Default,
};

struct ModelDeliveryChoice
{
	QString target;
	ModelDeliveryOrigin origin = ModelDeliveryOrigin::Default;
};

// The manifest key the preferences live under.
[[nodiscard]] QString modelDeliveryManifestKey();
[[nodiscard]] QJsonObject modelDeliveryPreferencesJson(const ModelDeliveryPreferences &preferences);
// Refuses unknown formats and targets, and a strict value that is not a
// boolean.
bool parseModelDeliveryPreferences(const QJsonValue &value, ModelDeliveryPreferences *preferences, QString *error = nullptr);
// The project's saved preferences; an unreadable or stale entry (a target
// this studio no longer has) is left out, with a note in `warnings`.
[[nodiscard]] ModelDeliveryPreferences projectModelDeliveryPreferences(const ProjectManifest &manifest, QStringList *warnings = nullptr);
// Stores `preferences` in the manifest (removing the key when they are empty);
// the caller saves the manifest.
void setProjectModelDeliveryPreferences(ProjectManifest *manifest, const ModelDeliveryPreferences &preferences);
// The targets an installation's engine suggests from its executable's name:
// QuakeSpasm, FTE and DarkPlaces for Quake, Q2PRO for Quake II, ioquake3 and
// Quake3e for Quake III, the Return to Castle Wolfenstein and Enemy
// Territory executables, and GZDoom. Empty for an engine with no targets of
// its own, which then keeps the format defaults.
[[nodiscard]] ModelDeliveryPreferences suggestedModelDeliveryPreferences(const GameInstallationProfile &installation);
// What the game installation in use suggests for a project, with the
// installation chosen as releases choose it (releaseInstallationFor in
// core/release_plan.h): the one the project links, else one of the project's
// game (the selected one first), else the selected installation, also with
// no project open. The Mesh Editor and the model CLI commands both use it, so
// they check against the same targets. `installationName` receives the
// chosen installation's display name; empty preferences when none applies.
[[nodiscard]] ModelDeliveryPreferences installationModelDeliveryPreferences(const ProjectManifest &manifest,
																			  const QVector<GameInstallationProfile> &installations,
																			  const QString &selectedInstallationId,
																			  QString *installationName = nullptr);
// The target for `format`: `explicitTarget` when set, else the project's, the
// installation's, or the format's default. Empty for formats without targets.
[[nodiscard]] ModelDeliveryChoice resolveModelDeliveryTarget(const QString &format, const QString &explicitTarget,
															 const ModelDeliveryPreferences &project,
															 const ModelDeliveryPreferences &installation);
// "explicit", "project", "installation" or "default".
[[nodiscard]] QString modelDeliveryOriginId(ModelDeliveryOrigin origin);
// The translated origin, such as "saved with the project".
[[nodiscard]] QString modelDeliveryOriginName(ModelDeliveryOrigin origin);
// The project folder that holds `path`: the nearest folder at or above it
// with a project manifest. Empty when there is none.
[[nodiscard]] QString findModelProjectRoot(const QString &path);
} // namespace vibestudio
