#pragma once
#include "core/level_brush.h"
#include "core/level_surface.h"
#include <QSet>

namespace vibestudio {
inline constexpr qsizetype kLevelTexturedSelectionLimit = 16384;
struct LevelTexturedSelectionRequest {
	QString material;
	// Independent face picks, in selection order. These do not select parents
	// and are separate from the GUI's vertex/edge/face manipulation handles.
	QVector<LevelSurfaceFace> faces;
	QSet<QString> excludedObjects;
	bool undoSelection = false;
	// Find and Replace Textures' Find within the selection: new matches only on
	// selected brushes and patches and on those of selected entities.
	bool withinSelection = false;
};
struct LevelTexturedSelectionReport {
	QVector<LevelMapSelectionRef> selection;
	QVector<LevelSurfaceFace> faces;
	QVector<int> matchedPatches;
	QHash<int,QByteArray> brushGeometry;
	int addedFaces = 0, addedPatches = 0;
};

// Exact ordered plane geometry and ownership; excludes material, UVs, flags,
// selection and save-back bookkeeping. Surface-only edits retain identity;
// geometry changes or face reordering invalidate stored face indices.
QByteArray levelSurfaceSelectionGeometryKey(const LevelMapBrush& brush);

// NRC Select Textured: additive case-insensitive matching of the current
// material (whitespace is significant). Adds independent brush faces and whole
// patch objects. It never changes manipulation handles or their mode.
// New matches must be visible, unlocked, in the open group and outside closed
// groups. Existing picks are preserved, including temporarily hidden picks;
// authoring callers must revalidate their editability and geometry identity.
// At most 16,384 combined independent faces and matching patches; refuse the
// complete query on overflow, invalid geometry or cancellation. A failure
// leaves both document and report untouched. Only added patch object picks can
// create a selection-history entry; map contents and revision never change.
bool selectLevelMapTextured(LevelMapDocument* document, const LevelTexturedSelectionRequest& request,
	LevelTexturedSelectionReport* report = nullptr, QString* error = nullptr);

// Independent face picks as an authoring caller holds them.
struct LevelSurfaceFacePicks {
	QVector<LevelSurfaceFace> faces;
	// Optional identities returned by the query; a listed brush must still match.
	QHash<int,QByteArray> brushGeometry;
	// View filters. Hidden, locked, closed-group and out-of-context brushes are
	// always refused.
	QSet<QString> excludedObjects;
};
struct LevelSurfaceFaceTransformReport {
	LevelMapComponentReport components;
	// The picks after the edit, in their original order. Each follows the face
	// that carries its transformed plane; a face the rebuild removes is retired.
	QVector<LevelSurfaceFace> faces;
	QHash<int,QByteArray> brushGeometry;
	QVector<LevelMapComponentSet> sets;
	int retiredFaces = 0;
};
// Merge independent face picks into component sets without selecting their
// parent brushes. A pick joins an existing Face set of its brush; a brush with
// Vertex or Edge picks receives the face's corners as vertices instead. Refuses
// stale, missing, hidden, filtered, locked or out-of-context picks, more than
// 16,384 picks and invalid brush geometry. The document is not changed.
bool levelSurfaceFaceComponentSets(const LevelMapDocument& document, const LevelSurfaceFacePicks& picks,
	const QVector<LevelMapComponentSet>& components, QVector<LevelMapComponentSet>* sets, QString* error = nullptr);
// Move, rotate, scale or shear independent face picks, with any ordinary
// component picks, through transformLevelMapComponents(): one shared pivot,
// one atomic undo step, the request's UV lock and scene-lock guard. A failure
// leaves document and report untouched.
bool transformLevelMapSurfaceFaces(LevelMapDocument* document, const LevelSurfaceFacePicks& picks,
	const QVector<LevelMapComponentSet>& components, const LevelComponentTransformRequest& request,
	LevelSurfaceFaceTransformReport* report = nullptr, QString* error = nullptr);
} // namespace vibestudio
