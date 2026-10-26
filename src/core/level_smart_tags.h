#pragma once

// Smart-tag authoring shares the tag definitions and matching rules with
// view filters. Workflow reference: TrenchBroom SmartTag and TagMatcher at
// 4c756be1 (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.
#include "core/level_filter_tags.h"

namespace vibestudio {

class PackageArchiveReader;
// Raw texture names, including WAD textures and image entries below textures/.
QStringList levelSmartTagPackageMaterials(const PackageArchiveReader& reader);

bool levelSmartTagCanDisable(const LevelFilterTag& tag);
QString levelSmartTagCommandId(const QString& tagId, bool enabled);
bool levelSmartTagForCommand(const QString& commandId, QString* tagId = nullptr, bool* enabled = nullptr);
// Material/class choices are filtered by the same matcher used by the views.
// Exact patterns are offered even when the texture/class is not in the map yet.
QStringList levelSmartTagChoices(const LevelFilterTag& tag, const QStringList& materials,
	const EntityDefinitionCatalogue& definitions, const LevelViewFilterMaterials& shaders);

struct LevelSmartTagRequest {
	LevelFilterTag tag;
	bool enabled = true;
	QString choice;
	// Explicit surface targets; otherwise the document's object selection.
	bool surfaceTargets = false;
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	LevelViewFilterMaterials shaders;
	// View-filtered objects are unavailable to GUI authoring too.
	QSet<QString> excludedObjects;
};

// Atomic candidate edit. Failure leaves source, selection and history intact.
// Texture/surfaceparm tags replace materials; flags set/clear their bits;
// classname tags tie selected primitives or return matching owners to world.
// Detail bits require whole brushes. Texture tags cannot be disabled.
bool applyLevelSmartTag(LevelMapDocument* document, const LevelSmartTagRequest& request, QString* error = nullptr);

} // namespace vibestudio
