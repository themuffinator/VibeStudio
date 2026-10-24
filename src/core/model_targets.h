#pragma once

// Delivery targets: the engines and source ports a native model export is
// meant for, with the limits each one's model loader and renderer enforce.
//
// Each export format has a default target that keeps the studio's original
// behaviour (the id Software engines; for IQM, the format's own limits as the
// writer stores them), and further targets where a source port's limits
// differ. The table is data: every limit cites the loader it was read from
// (see modelTargetProfiles and docs/MODEL_TARGETS.md).
//
// assessModelTarget is the preflight: it reports every limit a model exceeds
// for a target, with the surface or frame concerned and a fix, rather than the
// first one. exportEditableModel runs it before writing (ModelExportOptions::
// target), and the writers read their count limits from the same profile.

#include "core/model_mesh.h"
#include "core/model_work.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio
{
// Zero (or -1 for detailLevels) means the target sets no limit of its own;
// the editable source's limits (core/model_document.h) still apply.
struct ModelTargetLimits
{
	int surfaces = 0;
	// Per surface for MD3 and IQM. For MDL and MD2, which hold one surface, the
	// vertices the writer stores after sharing seams and exact duplicates.
	int vertices = 0;
	// The vertices the engine builds for drawing: an MDL's stored vertices plus
	// the copies for back-side seams (FTE), or an MD2's distinct position and
	// texel pairs (Q2PRO).
	int renderVertices = 0;
	// Per surface.
	int triangles = 0;
	// MDL poses, MD2 and MD3 frames.
	int frames = 0;
	// MDL skin images, MD2 skin slots.
	int skins = 0;
	int skinWidth = 0;
	int skinHeight = 0;
	qint64 skinPixels = 0;
	int skinWidthMultiple = 0;
	int joints = 0;
	// Joint influences per vertex the format or engine keeps (IQM: four).
	// More are not a blocker: the writer keeps the strongest and reports the
	// rest, and Prepare for target can reduce them in the source.
	int influences = 0;
	// Weights per MD5 mesh.
	int weights = 0;
	int tagsPerFrame = 0;
	// Lower-detail models loaded beside the base model (MD3 name_1, name_2 ...).
	int detailLevels = -1;
};

struct ModelTargetProfile
{
	// Stable identifiers: `id` is unique within its format.
	QString id;
	QString format;
	QString label;
	// What the target covers and anything notable about its limits.
	QString description;
	bool isDefault = false;
	ModelTargetLimits limits;
	// Where the limits come from: source files with a revision or review date.
	QStringList sources;
};

// Every target, grouped by format in modelExportFormatIds order.
[[nodiscard]] QVector<ModelTargetProfile> modelTargetProfiles();
// The targets for one format (case-insensitive); empty for OBJ and ASE.
[[nodiscard]] QVector<ModelTargetProfile> modelTargetProfiles(const QString &format);
// The target `id` for `format`, or the format's default when `id` is empty.
// False when the format has no targets or none is called `id`.
bool findModelTargetProfile(const QString &format, const QString &id, ModelTargetProfile *profile);
// "id (label)" for every target of `format`, for refusals and help text.
[[nodiscard]] QString modelTargetChoices(const QString &format);

struct ModelTargetIssue
{
	// A stable identifier such as "vertices", "triangles" or "uv-range".
	QString limit;
	// The surface, frame or file the issue is about; empty for the whole model.
	QString element;
	// The surface index for per-surface issues, else -1.
	int surface = -1;
	qint64 value = 0;
	qint64 maximum = 0;
	QString message;
	// What to change in the editor.
	QString fix;
	// The faces or vertices of `surface` the issue is about, ascending: UVs
	// outside the tile, positions outside MD3's range, collapsing triangles,
	// or the faces past a surface's budget (those Separate selected faces
	// would move). Empty when the issue is about a whole surface or model.
	QVector<int> faces{};
	QVector<int> vertices{};
};

// The largest value measured for a limit, so a passing check shows headroom.
struct ModelTargetMeasure
{
	QString limit;
	QString element;
	qint64 value = 0;
	qint64 maximum = 0;
	// The source count, which the writer can only lower by sharing vertices;
	// set by a counts-only assessment for MDL and MD2 vertices.
	bool upperBound = false;
};

struct ModelTargetReport
{
	QString format;
	QString target;
	QString label;
	QVector<ModelTargetIssue> issues;
	QVector<ModelTargetMeasure> measures;
	// Limits a counts-only assessment did not check, such as MD3's coordinate
	// range and 1/64-unit precision ("coordinate-range", "precision"); a full
	// assessment leaves this empty. Ready then means ready as far as checked.
	QStringList unchecked;
	[[nodiscard]] bool ready() const { return issues.isEmpty(); }
};

enum class ModelTargetDepth
{
	// Every check the writer makes before it writes, across every frame.
	Full,
	// Counts, names and sizes only, for a live summary: no per-frame scans
	// (listed in ModelTargetReport::unchecked), and MDL and MD2 vertices
	// measured before sharing (an upper bound).
	Counts,
};

// The vertex positions across every frame up to which a live summary can run
// MD3's full frame scan without slowing editing.
inline constexpr qint64 modelTargetLiveScanPositions = 500000;

// Checks `mesh`, which must pass validateEditableModel, against every limit
// of `profile`. False only on cancellation.
bool assessModelTarget(const ModelMesh &mesh, const ModelTargetProfile &profile, ModelTargetReport *report, QString *error = nullptr,
					   const ModelWorkControl &control = {}, ModelTargetDepth depth = ModelTargetDepth::Full);

// One line per issue: the message, then the fix.
[[nodiscard]] QStringList modelTargetIssueLines(const ModelTargetReport &report);
// The translated name of a limit identifier, such as "drawn vertices".
[[nodiscard]] QString modelTargetLimitName(const QString &limit);
// "vertices 812 of 999 (surface body)" for each measure; upper bounds say so.
[[nodiscard]] QStringList modelTargetMeasureLines(const ModelTargetReport &report);
[[nodiscard]] QJsonObject modelTargetProfileJson(const ModelTargetProfile &profile);
[[nodiscard]] QJsonObject modelTargetIssueJson(const ModelTargetIssue &issue);
[[nodiscard]] QJsonObject modelTargetReportJson(const ModelTargetReport &report);

// Splits a surface's faces into connected parts that each fit `vertices`
// distinct vertices and `triangles` triangles (zero: no limit), growing each
// part from its lowest unassigned face across shared vertices. Part 0 also
// counts the surface's unused vertices, which stay with it. `partOfFace` gets
// each face's part and `parts` the number of parts (1 when it already fits).
// False only on cancellation, or when one face alone cannot fit.
bool modelTargetSurfaceParts(const ModelSurface &surface, int vertices, int triangles, QVector<int> *partOfFace, int *parts,
							 QString *error = nullptr, const ModelWorkControl &control = {});

// What the MD2 writer stores: for each source vertex, the index of the
// position/normal vertex it shares across every frame, and one source vertex
// per stored vertex. False only on cancellation.
bool modelMd2PositionIndex(const ModelMesh &mesh, QVector<int> *positionIndex, QVector<int> *representative, QString *error = nullptr,
						   const ModelWorkControl &control = {});
} // namespace vibestudio
