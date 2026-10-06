#pragma once

#include "core/model_mesh.h"
#include <QMetaType>
#include <QSet>

#include <array>

namespace vibestudio
{
enum class ModelTransformTool
{
	Move,
	Rotate,
	Scale
};
enum class ModelTransformPivot
{
	Origin,
	SelectionCentre,
	Custom
};
enum class ModelTransformSpace
{
	World,
	Selection,
	Custom
};
struct ModelTransformBasis
{
	std::array<ModelVec3, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
	// Preserve the existing world-space arithmetic and its fast path.
	bool world = true;
};
struct ModelTransform
{
	ModelVec3 translation, rotation, scale{1, 1, 1}, pivot;
	ModelTransformBasis basis;
};

// Euler rotation in X, Y, Z order in the chosen orthonormal basis, after
// scaling about the world-space pivot. Translation uses that same basis.
// These helpers are shared by document commits and viewport previews.
ModelVec3 rotateModelVector(ModelVec3 point, ModelVec3 degrees);
bool validModelTransformBasis(const ModelTransformBasis &basis);
ModelVec3 modelBasisToWorld(ModelVec3 point, const ModelTransformBasis &basis);
ModelVec3 modelBasisFromWorld(ModelVec3 point, const ModelTransformBasis &basis);
ModelVec3 rotateModelTransformVector(ModelVec3 point, const ModelTransform &transform);
ModelVec3 transformModelPoint(ModelVec3 point, const ModelTransform &transform);
ModelVec3 transformModelNormal(ModelVec3 normal, const ModelTransform &transform);
bool modelTransformPivot(const QVector<ModelVec3> &positions, const QSet<int> &vertices, ModelTransformPivot mode, ModelVec3 custom,
						 ModelVec3 *result);
bool snapModelRotation(ModelVec3 degrees, double step, ModelVec3 *result, QString *error = nullptr);
// Factors snap relative to identity (1), so enabling snapping never alters a
// neutral transform. Negative factors remain available for explicit mirroring.
bool snapModelScale(ModelVec3 factors, double step, ModelVec3 *result, QString *error = nullptr);
// Grid snapping applies to the translation delta, preserving relative spacing.
// Zero disables it; otherwise the step is 0.000001 through 1,000,000 units.
bool snapModelTranslation(ModelVec3 translation, double grid, ModelVec3 *result, QString *error = nullptr);

enum class ModelMoveConstraint
{
	X,
	Y,
	Z,
	ViewPlane
};
struct ModelPickRay
{
	ModelVec3 origin, direction;
	bool forwardOnly = false;
};
struct ModelMoveDrag
{
	std::array<double, 3> origin{}, normal{}, axis{}, start{};
	ModelMoveConstraint constraint = ModelMoveConstraint::ViewPlane;
	double grid = 0;
};

// Pure geometry used by the viewport and tests. No document, camera or input
// state is changed. A ray parallel to the drag plane is rejected atomically.
bool beginModelMoveDrag(ModelVec3 pivot, ModelVec3 viewDirection, ModelMoveConstraint constraint, ModelPickRay ray, double grid,
						ModelMoveDrag *result);
bool modelMoveDragDelta(const ModelMoveDrag &drag, ModelPickRay ray, ModelVec3 *translation);

struct ModelRotateDrag
{
	ModelMoveDrag plane;
	std::array<double, 3> previous{};
	int axis = 0;
	double degrees = 0, grid = 0;
};
// Accumulates signed angular steps across the +/-180-degree boundary. Invalid
// rays or a ray through the pivot leave both drag state and output unchanged.
bool beginModelRotateDrag(ModelVec3 pivot, int axis, ModelPickRay ray, double grid, ModelRotateDrag *result);
bool modelRotateDragAngle(ModelRotateDrag *drag, ModelPickRay ray, double *degrees);
} // namespace vibestudio

Q_DECLARE_METATYPE(vibestudio::ModelTransform)
