#pragma once

#include "core/level_map.h"
#include <QHash>
#include <QSize>
#include <functional>

namespace vibestudio
{
class LevelSurfaceClipboard;
struct LevelSurfacePasteOptions;
struct LevelMaterialTarget;
struct LevelSurfaceFace {
	int brushId = -1;
	int faceIndex = -1; // Zero-based source face, as used by camera picking.
	bool operator==(const LevelSurfaceFace &) const = default;
};
// Natural resets a mapping to the default projection along the world axes
// (a patch: its natural spread along the surface); FaceAligned projects in
// the surface's own plane instead. Later values keep the earlier ones' numbers,
// which the dialog and the command line store.
//
// Rotate turns each face's mapping about its centre and RadiantRotate about
// the texture origin (where the world origin projects; a patch: coordinate
// 0,0), both with NetRadiant Custom's turn for each face format: a classic
// face adds to its rotation, a Valve 220 face also turns its axes about their
// own normal, a brush-primitive matrix turns the plane coordinates it maps,
// and a patch turns its coordinates in texels. RadiantScale is NetRadiant
// Custom's additive scale step about the texture origin: x and y are added to
// each face's texture scale values (levelSurfaceRadiantScaleStep()), and a
// patch multiplies its coordinates by a positive step or divides them by a
// negative one, as that editor does.
enum class LevelSurfaceOperation { Shift, Scale, Rotate, Fit, Align, Natural, FaceAligned, RadiantScale, RadiantRotate };
enum class LevelSurfaceAlignment { Keep, Minimum, Center, Maximum };
struct LevelSurfaceRequest {
	LevelSurfaceOperation operation = LevelSurfaceOperation::Fit;
	// Texels for shift, size multipliers for scale (negative mirrors), repeats
	// for fit (0 keeps that axis), map units per texel for Natural and
	// FaceAligned, scale increments for RadiantScale (0 keeps that axis).
	double x = 1, y = 1;
	double degrees = 0;
	LevelSurfaceAlignment alignU = LevelSurfaceAlignment::Center;
	LevelSurfaceAlignment alignV = LevelSurfaceAlignment::Center;
};

// The map units per texel a reset mapping uses: 0.5 for Quake III, as its
// Radiant editors default to, and 1 for Quake and Quake II.
double levelSurfaceDefaultScale(const LevelMapDocument& document);

// A texture scale value after one Radiant scale step: value + increment, or
// -value where the sum would come within 1e-5 of zero, so a step mirrors the
// texture instead of collapsing it (NetRadiant Custom's Texdef_Scale). A
// legacy zero scale counts as one, as q3map2 reads it.
double levelSurfaceRadiantScaleStep(double value, double increment);

// Selected brushes, including brushes owned by selected entities. Patches
// are listed by levelMapSelectedPatchIds() (core/level_patch_tools.h).
QVector<LevelSurfaceFace> levelMapSelectedSurfaces(const LevelMapDocument &document);

class LevelSurfaceEditPlan
{
  public:
	int faceCount() const { return m_faceCount; }
	int patchCount() const { return static_cast<int>(m_patchAfter.size()); }
	int convertedFaceCount() const { return m_convertedFaces; }
	int edgeOnFaceCount() const { return m_edgeOnFaces; }
	int brushCount() const { return static_cast<int>(m_after.size()); }
	bool ready() const { return m_ready; }

  private:
	friend bool prepareLevelSurfaceEdits(const LevelMapDocument &, const QVector<LevelSurfaceFace> &, const QVector<int> &,
										 const QVector<LevelSurfaceRequest> &, const QHash<QString, QSize> &, LevelSurfaceEditPlan *, QString *,
										 const std::function<bool()> &);
	friend bool prepareLevelSurfaceEdit(const LevelMapDocument &, const QVector<LevelSurfaceFace> &, const LevelSurfaceRequest &,
										const QHash<QString, QSize> &, LevelSurfaceEditPlan *, QString *, const std::function<bool()> &);
	friend bool commitLevelSurfaceEdit(LevelMapDocument *, const LevelSurfaceEditPlan &, QString *);
	friend class LevelSurfaceStroke;
	friend bool prepareLevelSurfacePaste(const LevelMapDocument&, const QVector<LevelSurfaceFace>&, const LevelSurfaceClipboard&,
		const LevelSurfacePasteOptions&, LevelSurfaceEditPlan*, QString*, const std::function<bool()>&);
	friend bool prepareLevelSurfaceTransfer(const LevelMapDocument&, const QVector<LevelMaterialTarget>&, const LevelSurfaceClipboard&,
		const LevelSurfacePasteOptions&, LevelSurfaceEditPlan*, QString*, const std::function<bool()>&);
	quint64 m_revision = 0;
	QString m_sourcePath;
	QByteArray m_sourceHash;
	LevelMapFormat m_format = LevelMapFormat::Unknown;
	QVector<LevelMapBrush> m_before, m_after;
	QVector<LevelMapPatch> m_patchBefore, m_patchAfter;
	QVector<LevelMapSelectionRef> m_selection;
	bool m_bindSelection = false;
	int m_faceCount = 0;
	int m_convertedFaces = 0;
	int m_edgeOnFaces = 0;
	bool m_ready = false;
	bool m_paste = false;
};

// Work on an immutable document, normally on a worker. Each face keeps its
// dialect, geometry, material and flags. Required image sizes must be explicit
// or come from the package material resolver; there is no guessed 64x64 size.
// Keys are trimmed, slash-normalized and case-folded virtual material names.
bool prepareLevelSurfaceEdit(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces, const LevelSurfaceRequest &request,
							 const QHash<QString, QSize> &textureSizes, LevelSurfaceEditPlan *plan, QString *error = nullptr,
							 const std::function<bool()> &cancelled = {});
// Ordered quick adjustments share one geometry solve and one atomic undo step.
// At most 64 adjustments per batch; any invalid intermediate result rejects all.
bool prepareLevelSurfaceEdits(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces,
							  const QVector<LevelSurfaceRequest> &requests, const QHash<QString, QSize> &textureSizes,
							  LevelSurfaceEditPlan *plan, QString *error = nullptr, const std::function<bool()> &cancelled = {});
// The same for brush faces and whole patches together. Patch coordinates
// count repeats, so a shift, a turn, Natural and FaceAligned need the
// patch's image size; scale, fit and alignment do not.
bool prepareLevelSurfaceEdits(const LevelMapDocument &document, const QVector<LevelSurfaceFace> &faces, const QVector<int> &patches,
							  const QVector<LevelSurfaceRequest> &requests, const QHash<QString, QSize> &textureSizes,
							  LevelSurfaceEditPlan *plan, QString *error = nullptr, const std::function<bool()> &cancelled = {});
// Whether `operation` may need the material's image size on a patch or a
// brush-primitive face (`repeatsCoordinates`), or on a classic or Valve 220
// face, so callers can resolve it first. A turn needs it only on a patch and
// a Radiant scale step only on a brush-primitive face.
bool levelSurfaceOperationNeedsSize(LevelSurfaceOperation operation, bool repeatsCoordinates);
// Lightweight, source-validated commit. All changed faces are one undo step.
// A no-op leaves revision, undo, redo and the saved state unchanged.
bool commitLevelSurfaceEdit(LevelMapDocument *document, const LevelSurfaceEditPlan &plan, QString *error = nullptr);
} // namespace vibestudio
