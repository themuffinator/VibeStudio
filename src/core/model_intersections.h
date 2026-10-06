#pragma once

#include "core/model_triangle_contact.h"

namespace vibestudio
{
inline constexpr qint64 modelIntersectionMaxPairs = 67108864;
inline constexpr qint64 modelIntersectionMaxNodeChecks = 268435456;
inline constexpr qint64 modelIntersectionMaxFacePoses = 4194304;
inline constexpr int modelIntersectionMaxFindings = 65536;

struct ModelIntersectionOptions
{
	int frame = -1;	  // -1 scans all stored poses, otherwise one zero-based pose.
	int surface = -1; // -1 includes all pairs; otherwise at least one face must belong to this surface.
	// Callers may lower these ceilings. Exhaustion fails atomically, never returns a partial clean report.
	qint64 pairLimit = modelIntersectionMaxPairs;
	qint64 nodeLimit = modelIntersectionMaxNodeChecks;
	int findingLimit = modelIntersectionMaxFindings;
};
struct ModelIntersectionFinding
{
	int frame = -1, firstSurface = -1, firstFace = -1, secondSurface = -1, secondFace = -1;
	ModelTriangleContact kind = ModelTriangleContact::None;
	bool operator==(const ModelIntersectionFinding &) const = default;
};
struct ModelIntersectionReport
{
	QVector<ModelIntersectionFinding> findings;
	int framesScanned = 0;
	qint64 facePoses = 0, candidatePairs = 0, nodeChecks = 0;
};

// Read-only scan of validated editable geometry, including self and cross-surface
// intersections. Rebuilds a bounded spatial hierarchy per stored pose. Results
// are ordered by frame, surface and face indices, independent of traversal order.
// Geometry between stored poses, solid containment, isolated point contacts and
// coincident boundary segments are outside this report. No geometry is repaired.
// Failure/cancellation leaves result untouched. Source, metadata and selection are never changed.
bool inspectModelIntersections(const ModelMesh &mesh, const ModelIntersectionOptions &options, ModelIntersectionReport *result,
							   QString *error = nullptr, const ModelWorkControl &control = {});
QString modelIntersectionKindId(ModelTriangleContact kind);
} // namespace vibestudio
