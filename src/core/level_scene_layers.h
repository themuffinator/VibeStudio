#pragma once

#include "core/level_map.h"

#include <QHash>
#include <QSet>

namespace vibestudio {
// Layer commands over the scene tree of core/level_scene.h: NetRadiant Custom's
// Expand Selection to Layers and its layers menu, which TrenchBroom offers as
// Move to Layer. Layers sit at the scene root; groups nest beneath a layer or
// at the root, which is the implicit Default layer (empty id).

// The layer holding each scene object (levelSceneObjects): its own membership,
// else its non-worldspawn owner's, walked up through its groups. `scene` may be
// a proposed state for `document`'s records.
QHash<QString, QString> levelSceneObjectLayers(const LevelMapDocument& document, const LevelSceneState& scene);
// Every node's containing layer: the node itself for a layer, else the layer at
// the top of its parents. Root groups and broken chains give Default.
QHash<QString, QString> levelSceneNodeLayers(const LevelSceneState& scene);

struct LevelSceneLayerSelectionRequest {
	// Objects the caller's view filters hide. Hidden, locked and out-of-context
	// objects are always left out.
	QSet<QString> excludedObjects;
	bool undoSelection = false;
};
struct LevelSceneLayerSelectionReport {
	// The layers expanded, Default ("") first and then in scene order.
	QStringList layers;
	QVector<LevelMapSelectionRef> selection;
	int added = 0;
	// Closed groups left out because a member is hidden, locked or filtered.
	int skippedGroups = 0;
};
// Expand Selection to Layers. Adds every visible, unlocked object of the layers
// that hold the selected objects, nested groups included, to the selection.
// Closed groups stay whole selection units and an open group limits the result
// to its contents; brush entities are represented by their selectable brushes
// and patches, as Select Siblings does. Worldspawn has no layer. Records one
// selection undo step when asked; a failure or cancellation changes nothing.
bool expandLevelMapSelectionToLayers(LevelMapDocument* document, const LevelSceneLayerSelectionRequest& request,
	LevelSceneLayerSelectionReport* report = nullptr, QString* error = nullptr);

struct LevelSceneLayerMoveReport {
	QString layer;
	// Objects given explicit membership of the layer.
	QStringList objects;
	// Brushes and patches whose own membership was cleared so they follow the
	// entity that moved with them.
	QStringList released;
	// Closed groups moved whole beneath the layer.
	QStringList groups;
	// Selected units already in the layer, and worldspawn, which has none.
	int alreadyThere = 0;
	int worldspawn = 0;
	// Moved objects the destination hides, dropped from the selection.
	int deselected = 0;
	bool changed = false;
};
// Why the selection cannot move to the layer (empty id: Default), or an empty
// string when it can. Checks the same plan moveLevelMapSelectionToLayer runs.
QString levelMapSelectionLayerMoveBlockedReason(const LevelMapDocument& document, const QString& layerId);
// The same for several layers at once, in order, sharing one selection pass.
QStringList levelMapSelectionLayerMoveBlockedReasons(const LevelMapDocument& document, const QStringList& layerIds);
// Move to Layer: one scene undo step. A selected closed group moves whole; a
// brush or patch of an ungrouped brush entity takes its entity along; other
// objects get explicit membership of the layer. Refused while a group is open,
// for locked content or a locked layer, and for stale scene metadata. A
// selection already in the layer succeeds with `changed` false and no undo step.
bool moveLevelMapSelectionToLayer(LevelMapDocument* document, const QString& layerId, LevelSceneLayerMoveReport* report = nullptr,
	QString* error = nullptr);
} // namespace vibestudio
