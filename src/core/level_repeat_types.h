#pragma once

#include <QVector>
#include <QtGlobal>
#include <array>

namespace vibestudio
{
// Session-only, value-based operations. No object IDs, document pointers or
// native undo snapshots are captured: replay targets the current selection.
struct LevelRepeatAction {
	enum class Kind { Transform, Move, Duplicate, DuplicateUnique };
	Kind kind = Kind::Transform;
	// Row-major 3x4 affine transform in world coordinates. Duplication uses
	// its translation column only; its linear part must be the identity.
	std::array<double, 12> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
	// Existing entity-angle behaviour is retained alongside the geometry.
	int rotationAxis = -1;
	double rotationDegrees = 0;
	std::array<double, 3> rotationPivot{};
	bool transformYaw = false;
	std::array<double, 2> yaw{1, 0};
	bool mirror = false;
	bool doomAllowed = true;
	bool operator==(const LevelRepeatAction&) const = default;
};

// NetRadiant Custom's repeatable transforms: the last translation, rotation,
// scale and skew, each kept until the next one of its kind replaces it. They
// carry no pivot: Repeat Transforms applies them about the current one. They
// survive selection changes and undo, as NetRadiant Custom's do.
struct LevelRepeatTransforms {
	std::array<double, 3> translation{0, 0, 0};
	// A right-handed turn about rotationAxis (0 = X); -1 for none.
	int rotationAxis = -1;
	double rotationDegrees = 0;
	// Arbitrary Rotation's Euler turn instead, when set: about world X, then
	// Y, then Z by these degrees (LevelMapRotationRequest::euler).
	bool rotationEulerXyz = false;
	std::array<double, 3> rotationEuler{0, 0, 0};
	std::array<double, 3> scale{1, 1, 1};
	// Points slide along skewAxis by skewAmount times their skewAlong
	// coordinate; -1 for none.
	int skewAxis = -1;
	int skewAlong = -1;
	double skewAmount = 0;
	[[nodiscard]] bool hasTranslation() const { return translation != std::array<double, 3>{0, 0, 0}; }
	[[nodiscard]] bool hasRotation() const
	{
		return rotationEulerXyz ? rotationEuler != std::array<double, 3>{0, 0, 0} : rotationAxis >= 0 && rotationDegrees != 0;
	}
	[[nodiscard]] bool hasScale() const { return scale != std::array<double, 3>{1, 1, 1}; }
	[[nodiscard]] bool hasSkew() const { return skewAxis >= 0 && skewAlong >= 0 && skewAmount != 0; }
	[[nodiscard]] bool isIdentity() const { return !hasTranslation() && !hasRotation() && !hasScale() && !hasSkew(); }
	bool operator==(const LevelRepeatTransforms&) const = default;
};

struct LevelRepeatState {
	QVector<LevelRepeatAction> actions;
	LevelRepeatTransforms transforms;
	bool clearOnNextAction = false;
	// Changes to transient repeat state must invalidate pending GUI candidates,
	// even when no map bytes or undo entries changed (for example Clear).
	quint64 revision = 0;
	bool operator==(const LevelRepeatState&) const = default;
};
} // namespace vibestudio
