#pragma once
#include "core/model_animation_curves.h"
#include <QWidget>
#include <functional>
namespace vibestudio
{
class ModelAnimationCurveView final : public QWidget
{
  public:
	explicit ModelAnimationCurveView(QWidget *parent = nullptr);
	void setCurve(const QVector<ModelPoseCurveKey> &keys, int frames, int channel, int selected);
	void setCursor(int frame);
	void fit();
	void selectKey(int index);
	void editSelected(double value);
	int selectedKey() const
	{
		return m_selected;
	}
	double selectedValue() const;
	// The values a key on the current channel may hold: scale (channels 6 to
	// 8) stays above zero, as joint capture requires. Dragging a key, adding
	// one with a double-click, editSelected() and a screen reader's value
	// change keep within this range, which the curves dialog's value field
	// and the graph's accessible value range share.
	double minimumValue() const
	{
		return m_channel >= 6 ? .00000101 : -1000000.0;
	}
	double maximumValue() const
	{
		return 1000000.0;
	}
	// What a screen reader hears: the selected key's place ("key 3 of 8,
	// frame 12") as part of the name, and its value, curve and tangents
	// before the help in the description. Numbers follow the locale.
	QString keyPositionText() const;
	QString keyDetailText() const;
	QPointF keyPoint(int index) const;
	QPair<QPointF, QPointF> tangentPoints() const;
	std::function<void(int)> selected;
	std::function<void(int, const ModelPoseCurveKey &)> edited;
	std::function<void(int)> removed;

  protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

  private:
	QVector<ModelPoseCurveKey> m_keys;
	int m_frames = 1, m_channel = 0, m_selected = -1, m_cursor = 0, m_drag = 0;
	double m_first = 0, m_last = 1, m_min = -1, m_max = 1;
	ModelPoseCurveKey m_pending;
	QRectF area() const;
	double withinRange(double value) const;
	QPointF point(double frame, double value) const;
	QPointF dataPoint(QPointF position) const;
	int nearest(QPointF position) const;
	void notifyKeyChanged();
};
} // namespace vibestudio
