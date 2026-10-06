#pragma once
#include "core/level_placement.h"
#include <QCoreApplication>
#include <QDialog>
#include <functional>

namespace vibestudio {
class LevelPlacementTaskDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(LevelPlacementTaskDialog)
  public:
	static LevelPlacementResult prepare(QWidget* parent, const LevelMapDocument& source, const LevelPlacementRequest& request);
	void reject() override;

  private:
	explicit LevelPlacementTaskDialog(QWidget* parent);
	std::function<void()> m_cancel;
};
} // namespace vibestudio
