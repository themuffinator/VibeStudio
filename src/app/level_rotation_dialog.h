#pragma once
#include "app/level_transform_dialog.h"
#include <functional>

namespace vibestudio
{
// Rotate Selection… (NetRadiant Custom's Arbitrary Rotation): X, Y and Z
// angles turned as one Euler XYZ rotation about the pivot, as one undo step.
class LevelRotationDialog final : public LevelTransformDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelRotationDialog)
  public:
	// Opens with 45 degrees about `axis` (0 X, 1 Y, 2 Z), the axis of the
	// view it was opened from.
	explicit LevelRotationDialog(const LevelMapDocument& source, int axis = 2, QWidget* parent = nullptr);
	// Always an Euler request; one nonzero angle is a single-axis turn.
	[[nodiscard]] LevelMapRotationRequest request() const;
	void setRequest(const LevelMapRotationRequest& request);
	void setApplyHandler(std::function<bool(const LevelMapRotationRequest&, QString*)> handler);

  protected:
	[[nodiscard]] LevelPlacementRequest placementRequest() const override;
	bool commit(QString* error) override;
	[[nodiscard]] QString readyText() const override;
	void sourceChanged() override;
	[[nodiscard]] QString requestProblem() const override;

  private:
	std::array<QDoubleSpinBox*, 3> m_angles{};
	std::function<bool(const LevelMapRotationRequest&, QString*)> m_applyHandler;
};
} // namespace vibestudio
