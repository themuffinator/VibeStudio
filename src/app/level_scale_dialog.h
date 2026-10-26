#pragma once
#include "app/level_transform_dialog.h"
#include <functional>

namespace vibestudio
{
// Scale Selection… (NetRadiant Custom's Arbitrary Scale): X, Y and Z factors
// about the pivot, where a negative factor mirrors, as one undo step.
class LevelScaleDialog final : public LevelTransformDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelScaleDialog)
  public:
	explicit LevelScaleDialog(const LevelMapDocument& source, QWidget* parent = nullptr);
	[[nodiscard]] LevelMapScaleRequest request() const;
	void setRequest(const LevelMapScaleRequest& request);
	void setApplyHandler(std::function<bool(const LevelMapScaleRequest&, QString*)> handler);

  protected:
	[[nodiscard]] LevelPlacementRequest placementRequest() const override;
	bool commit(QString* error) override;
	[[nodiscard]] QString readyText() const override;
	void sourceChanged() override;
	[[nodiscard]] QString requestProblem() const override;

  private:
	std::array<QDoubleSpinBox*, 3> m_factors{};
	std::function<bool(const LevelMapScaleRequest&, QString*)> m_applyHandler;
};
} // namespace vibestudio
