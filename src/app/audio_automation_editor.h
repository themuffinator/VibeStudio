#pragma once
#include "core/audio_automation.h"
#include <QWidget>
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
namespace vibestudio
{
class AudioAutomationPlot;
// A bounded point model shared by the accessible native controls and optional graphical gestures.
class AudioAutomationEditor final : public QWidget {
	Q_OBJECT
  public:
	AudioAutomationEditor(const QString &label, double minimum, double maximum, double fallback, qint64 cursor,
	                      qint64 end, QWidget *parent = nullptr);
	bool setPoints(const QVector<AudioAutomationPoint> &points);
	const QVector<AudioAutomationPoint> &points() const { return m_points; }
	bool setPoint(int index, AudioAutomationPoint point);
	bool addPoint(qint64 frame, double value);
	void selectPoint(int index);
  signals:
	void changed();

  private:
	friend class AudioAutomationPlot;
	void refresh(int selected);
	void selectionChanged();
	void updateSelected();
	QVector<AudioAutomationPoint> m_points;
	double m_minimum, m_maximum, m_fallback;
	qint64 m_cursor, m_end;
	bool m_updating = false;
	QTableWidget *m_table = nullptr;
	QDoubleSpinBox *m_frame = nullptr, *m_value = nullptr;
	QComboBox *m_curve = nullptr;
	QPushButton *m_add = nullptr, *m_remove = nullptr, *m_clear = nullptr;
	QLabel *m_status = nullptr;
	AudioAutomationPlot *m_plot = nullptr;
};
} // namespace vibestudio
