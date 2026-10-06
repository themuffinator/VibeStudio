#pragma once
#include "core/build_pipeline.h"
#include <QCoreApplication>
#include <QDialog>

namespace vibestudio {
struct GameLaunchTaskResult {
	GameLaunchPlan plan;
	qint64 pid = 0;
	QString error;
	bool started = false, cancelled = false;
};
class GameLaunchTaskDialog final : public QDialog {
	Q_DECLARE_TR_FUNCTIONS(GameLaunchTaskDialog)
  public:
	static GameLaunchPlan prepare(QWidget* parent, const GameLaunchRequest& request, const GameInstallationProfile& installation);
	static GameLaunchTaskResult start(QWidget* parent, const GameLaunchPlan& plan);
	void reject() override;

  protected:
	void closeEvent(QCloseEvent* event) override;

  private:
	explicit GameLaunchTaskDialog(QWidget* parent);
	static GameLaunchTaskResult run(QWidget* parent, bool starting,
									const std::function<void(GameLaunchTaskResult&, const std::function<bool()>&)>& action);
	std::function<void()> m_cancel;
};
} // namespace vibestudio
