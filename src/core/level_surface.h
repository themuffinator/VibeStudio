#pragma once

#include "core/level_map.h"
#include <QHash>
#include <QSet>
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
//
// CameraShift and CameraFlip work as the camera sees each face, as
// TrenchBroom's camera texture moves and flips do: for every face (a patch:
// its average plane) the texture axis running most nearly across the screen
// is horizontal and the other vertical (core/level_camera_texture.h). A
// CameraShift moves the texture x texels towards screen right and y texels
// towards screen up, through that face's own U and V offsets; a CameraFlip
// mirrors, about the middle as Scale -1 does, the axis running across the
// screen when x is not 0 and the one running up it when y is not 0.
//
// ClockwiseRotate turns each texture `degrees` clockwise as seen from the
// face's front, the side its outward normal faces (a patch: the side its
// control grid's normal faces, as the views draw it), whatever the face's
// format and whether its texture reads mirrored there; negative degrees turn
// it counter-clockwise. It keeps the offsets, so the texture turns about the
// texture origin without re-centring, as TrenchBroom's Rotate Textures
// commands turn it. It is RadiantRotate with the sign each face needs: a
// positive Radiant turn goes anticlockwise or clockwise on screen depending
// on the face (a classic face turns its projection axes whatever its scale
// signs, a Valve 220 face its stored axes, a brush-primitive face always
// anticlockwise from its front, and a patch its coordinates, so a mirrored
// patch texture turns the other way), so a quarter turn of a copy reads the
// direction first (levelTextureTurnSense() in core/level_camera_texture.h).
//
// Reset and RadiantReset reset every face in one pass, whatever its format,
// at x and y map units per texel with no shift or rotation. Reset is
// TrenchBroom's Reset Texture Alignment, which keeps each face's projection
// kind: a classic face takes Natural, a Valve 220 or brush-primitive face
// FaceAligned, and a patch its natural spread. RadiantReset is NetRadiant
// Custom's Reset Texture (TextureReset/Cap): a classic face takes Natural, a
// Valve 220 or brush-primitive face its plane's own basis
// (levelPlaneBasisTextureProjection() in core/level_texture_mapping.h), and a
// patch Radiant's Cap Texture projection, as FaceAligned gives patches.
enum class LevelSurfaceOperation { Shift, Scale, Rotate, Fit, Align, Natural, FaceAligned, RadiantScale, RadiantRotate, CameraShift, CameraFlip,
	ClockwiseRotate, Reset, RadiantReset };
enum class LevelSurfaceAlignment { Keep, Minimum, Center, Maximum };
struct LevelSurfaceRequest {
	LevelSurfaceOperation operation = LevelSurfaceOperation::Fit;
	// Texels for shift, size multipliers for scale (negative mirrors), repeats
	// for fit (0 keeps that axis), map units per texel for Natural,
	// FaceAligned, Reset and RadiantReset, scale increments for RadiantScale
	// (0 keeps that axis),
	// screen-right and screen-up texels for CameraShift, axes to mirror for
	// CameraFlip.
	double x = 1, y = 1;
	// Rotate and RadiantRotate: the turn Radiant's rotation value takes;
	// ClockwiseRotate: degrees clockwise from each face's front.
	double degrees = 0;
	LevelSurfaceAlignment alignU = LevelSurfaceAlignment::Center;
	LevelSurfaceAlignment alignV = LevelSurfaceAlignment::Center;
	// CameraShift and CameraFlip: the camera's screen-right and screen-up
	// directions in map space. Any length, but they must not be parallel.
	LevelMapVec3 cameraRight;
	LevelMapVec3 cameraUp;
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

// bobToolz's Reset Textures: absolute values for the ticked fields, an
// optional new texture name, for the given surfaces or the whole map,
// optionally only those using one texture. Each face keeps its format: a
// classic face takes the values as they are; a Valve 220 face takes them as
// its offsets and scales, and turns its axes about their normal by the change
// of rotation; a brush-primitive face takes them as Radiant reads such a face
// (its fake shift, scale and rotation, which need the image size). Patches
// store no shift, scale or rotation, so they take only the new name.
struct LevelTextureResetRequest {
	// Every visible, unlocked surface in the open group (and outside closed
	// groups and `excludedObjects`), or else `faces` and `patches`.
	bool wholeMap = false;
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	// Only surfaces using this texture, ignoring case, slash direction and a
	// leading textures/; empty: every targeted surface.
	QString onlyTexture;
	// The texture the targeted surfaces take; empty keeps their names.
	QString renameTo;
	bool setScaleU = false, setScaleV = false, setShiftU = false, setShiftV = false, setRotation = false;
	// Map units per texel (not 0), texels and degrees.
	double scaleU = 1, scaleV = 1, shiftU = 0, shiftV = 0, rotation = 0;
	// Objects view filters hide from the whole map, as levelSceneObjects ids.
	QSet<QString> excludedObjects;
	bool changesValues() const { return setScaleU || setScaleV || setShiftU || setShiftV || setRotation; }
	bool changesSomething() const { return changesValues() || !renameTo.trimmed().isEmpty(); }
};

class LevelSurfaceEditPlan
{
  public:
	int faceCount() const { return m_faceCount; }
	int patchCount() const { return static_cast<int>(m_patchAfter.size()); }
	int convertedFaceCount() const { return m_convertedFaces; }
	int edgeOnFaceCount() const { return m_edgeOnFaces; }
	int brushCount() const { return static_cast<int>(m_after.size()); }
	// Faces and patches a Reset Textures plan renames.
	int renamedCount() const { return m_renamed; }
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
	friend bool prepareLevelTextureReset(const LevelMapDocument&, const LevelTextureResetRequest&, const QHash<QString, QSize>&,
		LevelSurfaceEditPlan*, QString*, const std::function<bool()>&);
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
	int m_renamed = 0;
	bool m_ready = false;
	bool m_paste = false;
	bool m_reset = false;
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

// The surfaces a Reset Textures request targets, after its texture filter,
// and the materials among them whose image size it needs (brush-primitive
// faces with a ticked value), as trimmed, slash-normalized, case-folded keys.
bool levelTextureResetTargets(const LevelMapDocument &document, const LevelTextureResetRequest &request, QVector<LevelSurfaceFace> *faces,
							  QVector<int> *patches, QStringList *sizedMaterials = nullptr, QString *error = nullptr);
// Prepares Reset Textures as one plan that commitLevelSurfaceEdit() makes one
// undo step. At most 262,144 faces and 65,536 patches; a whole map leaves
// hidden, filtered, locked and out-of-context objects alone, and selected
// targets go through the scene-lock guard on commit. Refuses a request that
// changes nothing, a zero scale or an unwritable name; any face that cannot
// take the values rejects the whole reset.
bool prepareLevelTextureReset(const LevelMapDocument &document, const LevelTextureResetRequest &request, const QHash<QString, QSize> &textureSizes,
							  LevelSurfaceEditPlan *plan, QString *error = nullptr, const std::function<bool()> &cancelled = {});
} // namespace vibestudio
