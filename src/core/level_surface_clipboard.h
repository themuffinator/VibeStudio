#pragma once

#include "core/level_surface.h"
#include "core/level_material_paint.h"

namespace vibestudio {
// A portable value, independent of source document lifetime and object IDs.
// It retains a source plane for projection, but no brush geometry, file paths,
// package data or system clipboard access.
class LevelSurfaceClipboard {
public:
	bool ready() const { return m_ready; }
	QString material() const { return m_face.textureName; }
	QString mappingKind() const;
	const LevelMapBrushFace& face() const { return m_face; }
private:
	friend bool copyLevelSurface(const LevelMapDocument&, LevelSurfaceFace, LevelSurfaceClipboard*, QString*);
	friend bool parseLevelSurfaceClipboard(const QByteArray&, LevelSurfaceClipboard*, QString*);
	LevelMapBrushFace m_face;
	bool m_ready = false;
};
enum class LevelSurfacePasteMode { Parameters, Project, Seamless, RadiantValues, RadiantProject };
struct LevelSurfacePasteOptions {
	// Parameters reproduces the stored values in the same mapping family.
	// Project preserves the source's world-space UV function on each target.
	// Seamless rotates it around the source/target planes' intersection.
	// RadiantValues keeps Valve axes and transfers primitive texel density.
	// RadiantProject copies classic/Valve values, projects primitive matrices
	// and patch UVs, and permits the native edge-on brush mapping result.
	LevelSurfacePasteMode mode = LevelSurfacePasteMode::Parameters;
	bool mappingOnly = false; // Keep each target's material and flags.
	// Apply values/material to selected brushes and patches too. Explicit hit
	// faces also receive copied flags; other selected faces retain their flags.
	bool includeSelection = false;
	QHash<QString, QSize> materialSizes; // Normalized material keys; source/targets.
	// If projection needs explicit axes, convert every classic face in the map
	// in this same plan, preserving unpasted surfaces and respecting their locks.
	bool allowValve220 = false;
	QSize textureSize; // Original source image size; overrides its materialSizes entry.
};

bool copyLevelSurface(const LevelMapDocument& document, LevelSurfaceFace face, LevelSurfaceClipboard* clipboard, QString* error = nullptr);
QByteArray serializeLevelSurfaceClipboard(const LevelSurfaceClipboard& clipboard);
bool parseLevelSurfaceClipboard(const QByteArray& bytes, LevelSurfaceClipboard* clipboard, QString* error = nullptr);
bool prepareLevelSurfacePaste(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces,
	const LevelSurfaceClipboard& clipboard, const LevelSurfacePasteOptions& options, LevelSurfaceEditPlan* plan,
	QString* error = nullptr, const std::function<bool()>& cancelled = {});
QVector<LevelMaterialTarget> levelSurfacePasteSelectionTargets(const LevelMapDocument& document);
struct LevelSurfaceTransferMaterials {
	bool sourceSizeRequired = false;
	QStringList targets;
};
LevelSurfaceTransferMaterials levelSurfaceTransferRequiredMaterials(const LevelMapDocument& document, const QVector<LevelMaterialTarget>& targets,
	const LevelSurfaceClipboard& clipboard, const LevelSurfacePasteOptions& options);
// Brush mapping and patch material/UV targets share one atomic edit plan.
bool prepareLevelSurfaceTransfer(const LevelMapDocument& document, const QVector<LevelMaterialTarget>& targets,
	const LevelSurfaceClipboard& clipboard, const LevelSurfacePasteOptions& options, LevelSurfaceEditPlan* plan,
	QString* error = nullptr, const std::function<bool()>& cancelled = {});

// An ordered, private preview transaction. Each accepted hit uses the previous
// preview and seamless wrapping advances the source. Only the first hit can
// include the selection. Commit publishes the net change as one ordinary undo
// command; dropping the stroke cancels it without touching the source document.
// Copies are value snapshots, suitable for passing between GUI and worker.
class LevelSurfaceStroke {
public:
	LevelSurfaceStroke(const LevelMapDocument& document, const LevelSurfaceClipboard& clipboard);
	const LevelMapDocument& document() const { return m_document; }
	const LevelSurfaceClipboard& clipboard() const { return m_clipboard; }
	const LevelSurfaceEditPlan& plan() const { return m_plan; }
	bool clipboardFromDocument() const { return m_clipboardFromDocument; }
	int stepCount() const { return m_steps; }
	// textureSize always describes the original clipboard. After wrapping,
	// materialSizes supplies dimensions from the destination document's package.
	bool append(const QVector<LevelMaterialTarget>& targets, LevelSurfacePasteOptions options,
		QString* error = nullptr, const std::function<bool()>& cancelled = {});
	bool commit(LevelMapDocument* document, QString* error = nullptr) const;
private:
	LevelMapDocument m_document;
	LevelSurfaceClipboard m_clipboard;
	LevelSurfaceEditPlan m_plan, m_accumulated;
	int m_steps = 0;
	bool m_clipboardFromDocument = false;
};
} // namespace vibestudio
