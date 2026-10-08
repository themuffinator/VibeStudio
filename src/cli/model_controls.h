#pragma once

#include "cli/model_tools.h"

namespace vibestudio::cli
{
// `model profiles`: the modeller's controls profiles (Blender, 3ds Max,
// MilkShape 3D and the studio's own) with what each changes and where it
// differs from the reference editor.
ModelToolsCliResult runModelProfiles(const QStringList &arguments);
// `model controls`: every gesture and key of a modeller profile with the
// user's saved overrides, checks for conflicts, and exports or imports a
// shareable controls file.
ModelToolsCliResult runModelControls(const QStringList &arguments);
// `model formats`: every model format the studio reads and writes, with the
// games that use it and what each decoder keeps.
ModelToolsCliResult runModelFormats(const QStringList &arguments);
} // namespace vibestudio::cli
