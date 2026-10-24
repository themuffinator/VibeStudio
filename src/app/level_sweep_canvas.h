#pragma once
#include "app/level_sweep_gizmo.h"
#include <QKeySequence>
#include <QPointer>
#include <QWidget>

namespace vibestudio
{
// Owned transparent child overlay and local input routing. The map itself is
// never replaced by a preview. Camera navigation continues through the parent.
class LevelSweepCanvas final : public QWidget
{
	Q_OBJECT
  public:
	enum class Command { None, Apply, Cancel, DecreaseScale, IncreaseScale };
	LevelSweepCanvas(QWidget* view, LevelSweepGizmo* gizmo, std::function<LevelComponentGizmoFrame()> frame, bool highContrast);
	void stop();
	void setCommandKeys(Command command, const QList<QKeySequence>& keys);
	QList<QKeySequence> commandKeys(Command command) const
	{
		return m_keys.value(command);
	}
	LevelComponentGizmoFrame frame() const
	{
		return m_frame();
	}
	Command commandForKey(QKeyCombination combination) const;
	void executeCommand(Command command);
  Q_SIGNALS:
	void applyRequested();
	void leaveRequested();

  protected:
	void paintEvent(QPaintEvent*) override;
	bool eventFilter(QObject*, QEvent*) override;

  private:
	QPointer<QWidget> m_view;
	QPointer<LevelSweepGizmo> m_gizmo;
	std::function<LevelComponentGizmoFrame()> m_frame;
	QPointF m_lastPoint;
	bool m_highContrast = false, m_ownedDrag = false;
	QString m_originalDescription, m_lastDescription;
	QMap<Command, QList<QKeySequence>> m_keys;
};
} // namespace vibestudio
