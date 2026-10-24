#pragma once

#include "core/level_map.h"
#include <QJsonObject>

namespace vibestudio
{
struct LevelLinkedExtractionReport {
	QString sourceGroup, extractedGroup;
	QStringList createdGroups;
	// Nested groups moved into the new groups, counted in every instance.
	int nestedGroups = 0;
	int splitEntities = 0;
};

// A proper subset inside an open linked group: its own objects, and closed
// groups nested in it, each whole. Every instance must have known
// correspondence, editable ownership, and contents on both sides of the split;
// the linked groups around the instances must each hold as many of them.
bool canExtractLevelLinkedGroups(const LevelMapDocument& document, QString* error = nullptr);
// Split each instance independently, preserving protected properties and its
// native geometry. A partially selected brush entity is retained on both sides.
// Selected nested groups move into the new group with everything in them,
// keeping their links, and linked groups around the instances stay in sync.
// Success selects the new group corresponding to the open source, in its parent
// context. One undo restores geometry, scene, selection and editing context.
// Failure leaves the document and caller report unchanged.
bool extractLevelLinkedGroups(LevelMapDocument* document, LevelLinkedExtractionReport* report = nullptr, QString* error = nullptr);
QJsonObject levelLinkedExtractionReportJson(const LevelLinkedExtractionReport& report);
} // namespace vibestudio
