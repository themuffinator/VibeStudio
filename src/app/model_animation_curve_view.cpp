#include "app/model_animation_curve_view.h"
#include "app/locale_number.h"
#include <QAccessible>
#include <QAccessibleWidget>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationCurveView)
};
class Accessible final : public QAccessibleWidget, public QAccessibleValueInterface
{
  public:
	explicit Accessible(ModelAnimationCurveView *view) : QAccessibleWidget(view, QAccessible::Graphic), m_view(view) {}
	void *interface_cast(QAccessible::InterfaceType type) override
	{
		return type == QAccessible::ValueInterface ? static_cast<QAccessibleValueInterface *>(this)
												   : QAccessibleWidget::interface_cast(type);
	}
	QString text(QAccessible::Text t) const override
	{
		// The fixed name and help stay; the selected key joins them.
		switch (t)
		{
		case QAccessible::Name:
			return Text::tr("%1, %2", "the graph's name and the selected key").arg(m_view->accessibleName(), m_view->keyPositionText());
		case QAccessible::Description:
			return m_view->keyDetailText() + QLatin1Char(' ') + m_view->accessibleDescription();
		case QAccessible::Value:
			return localeNumber(m_view->selectedValue(), 12);
		default:
			return QAccessibleWidget::text(t);
		}
	}
	QVariant currentValue() const override
	{
		return m_view->selectedValue();
	}
	void setCurrentValue(const QVariant &value) override
	{
		bool ok = false;
		const auto n = value.toDouble(&ok);
		if (ok && std::isfinite(n))
			m_view->editSelected(n);
	}
	QVariant maximumValue() const override
	{
		return m_view->maximumValue();
	}
	QVariant minimumValue() const override
	{
		return m_view->minimumValue();
	}
	QVariant minimumStepSize() const override
	{
		return .1;
	}

  private:
	ModelAnimationCurveView *m_view;
};
QAccessibleInterface *factory(const QString &, QObject *object)
{
	if (auto *view = dynamic_cast<ModelAnimationCurveView *>(object))
		return new Accessible(view);
	return nullptr;
}
} // namespace
ModelAnimationCurveView::ModelAnimationCurveView(QWidget *parent) : QWidget(parent)
{
	static const bool installed = []
	{
		QAccessible::installFactory(factory);
		return true;
	}();
	Q_UNUSED(installed);
	setObjectName("animationCurveView");
	setMinimumSize(300, 240);
	setFocusPolicy(Qt::StrongFocus);
	setLayoutDirection(Qt::LeftToRight);
	setAccessibleName(Text::tr("Animation curve"));
	setAccessibleDescription(
		Text::tr("Time increases left to right. Select and drag keys or tangent handles; double-click to insert a key. Left and Right "
				 "select keys, Up and Down change the value, Control with Left or Right moves the sample, Delete removes a key, Escape "
				 "cancels a drag and Home fits the curve. The Keys tab provides numeric equivalents."));
	setToolTip(accessibleDescription());
}
void ModelAnimationCurveView::setCurve(const QVector<ModelPoseCurveKey> &keys, int frames, int channel, int index)
{
	const bool reset = m_frames != frames || m_channel != channel || m_keys.isEmpty();
	m_keys = keys;
	m_frames = std::max(1, frames);
	m_channel = channel;
	m_selected = index >= 0 && index < keys.size() ? index : -1;
	m_drag = 0;
	if (reset)
		fit();
	else
		update();
	// An edit or a new channel changes what the selected key says.
	notifyKeyChanged();
}
void ModelAnimationCurveView::setCursor(int frame)
{
	m_cursor = frame;
	update();
}
void ModelAnimationCurveView::fit()
{
	m_first = 0;
	m_last = std::max(1, m_frames - 1);
	const auto range = modelCurveRange(m_keys);
	const double pad = std::max(.5, (range.second - range.first) * .1);
	m_min = range.first - pad;
	m_max = range.second + pad;
	update();
}
QRectF ModelAnimationCurveView::area() const
{
	const auto metrics = fontMetrics();
	const int left = std::max(metrics.horizontalAdvance(localeNumber(m_min, 5)), metrics.horizontalAdvance(localeNumber(m_max, 5))) + 16;
	return QRectF(left, metrics.height() + 6, std::max(1, width() - left - 18), std::max(1, height() - 2 * metrics.height() - 28));
}
QPointF ModelAnimationCurveView::point(double frame, double value) const
{
	const auto box = area();
	return {box.left() + (frame - m_first) / (m_last - m_first) * box.width(),
			box.bottom() - (value - m_min) / (m_max - m_min) * box.height()};
}
QPointF ModelAnimationCurveView::dataPoint(QPointF p) const
{
	const auto box = area();
	return {m_first + (p.x() - box.left()) / box.width() * (m_last - m_first),
			m_min + (box.bottom() - p.y()) / box.height() * (m_max - m_min)};
}
QPointF ModelAnimationCurveView::keyPoint(int index) const
{
	return index >= 0 && index < m_keys.size() ? point(m_keys[index].frame, m_keys[index].value) : QPointF{};
}
QPair<QPointF, QPointF> ModelAnimationCurveView::tangentPoints() const
{
	if (m_selected < 0 || m_selected >= m_keys.size())
		return {};
	auto displayed = m_keys;
	if (m_drag >= 2)
		displayed[m_selected] = m_pending;
	const auto &key = displayed[m_selected];
	const auto slopes = modelCurveSlopes(displayed, m_selected);
	const double span = std::max(.01, (m_last - m_first) * .08);
	const double left = m_selected ? std::min(span, (key.frame - m_keys[m_selected - 1].frame) / 3.0) : span;
	const double right = m_selected + 1 < m_keys.size() ? std::min(span, (m_keys[m_selected + 1].frame - key.frame) / 3.0) : span;
	return {point(key.frame - left, key.value - slopes.first * left), point(key.frame + right, key.value + slopes.second * right)};
}
double ModelAnimationCurveView::withinRange(double value) const
{
	return std::clamp(value, minimumValue(), maximumValue());
}
double ModelAnimationCurveView::selectedValue() const
{
	return m_selected >= 0 && m_selected < m_keys.size() ? m_keys[m_selected].value : 0;
}
QString ModelAnimationCurveView::keyPositionText() const
{
	if (m_selected < 0 || m_selected >= m_keys.size())
		return Text::tr("no key selected, %Ln key(s)", nullptr, int(m_keys.size()));
	return Text::tr("key %L1 of %L2, frame %L3").arg(m_selected + 1).arg(m_keys.size()).arg(m_keys[m_selected].frame);
}
QString ModelAnimationCurveView::keyDetailText() const
{
	const auto channel = modelCurveChannelId(m_channel);
	if (m_selected < 0 || m_selected >= m_keys.size())
		return Text::tr("Channel %1.").arg(channel);
	const auto &key = m_keys[m_selected];
	const QStringList curves{Text::tr("step"), Text::tr("linear"), Text::tr("cubic"), Text::tr("hold next")};
	const QStringList tangents{Text::tr("automatic"), Text::tr("clamped"), Text::tr("free"), Text::tr("flat")};
	return Text::tr("Channel %1, value %2, %3 curve, %4 tangents.")
		.arg(channel, localeNumber(key.value, 12), curves.value(int(key.interpolation)), tangents.value(int(key.tangent)));
}
void ModelAnimationCurveView::notifyKeyChanged()
{
	// The name carries the selected key, so a screen reader re-reads it.
	QAccessibleEvent name(this, QAccessible::NameChanged);
	QAccessible::updateAccessibility(&name);
	QAccessibleEvent description(this, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&description);
	QAccessibleValueChangeEvent value(this, selectedValue());
	QAccessible::updateAccessibility(&value);
}
void ModelAnimationCurveView::selectKey(int index)
{
	m_selected = index >= 0 && index < m_keys.size() ? index : -1;
	update();
	if (selected)
		selected(m_selected);
	notifyKeyChanged();
}
void ModelAnimationCurveView::editSelected(double value)
{
	if (!isEnabled() || m_selected < 0 || !std::isfinite(value))
		return;
	auto key = m_keys[m_selected];
	key.value = withinRange(value);
	if (edited)
		edited(key.frame, key);
}
int ModelAnimationCurveView::nearest(QPointF p) const
{
	int index = -1;
	double best = 144;
	for (int i = 0; i < m_keys.size(); ++i)
	{
		const double d = std::pow(keyPoint(i).x() - p.x(), 2) + std::pow(keyPoint(i).y() - p.y(), 2);
		if (d < best)
		{
			best = d;
			index = i;
		}
	}
	return index;
}
void ModelAnimationCurveView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.fillRect(rect(), palette().base());
	const auto box = area();
	painter.setPen(palette().mid().color());
	painter.drawRect(box);
	for (int i = 1; i < 5; ++i)
	{
		const double x = box.left() + box.width() * i / 5, y = box.top() + box.height() * i / 5;
		painter.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
		painter.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
	}
	painter.setPen(palette().text().color());
	painter.drawText(QRectF(0, box.top() - fontMetrics().height() / 2, box.left() - 6, fontMetrics().height()), Qt::AlignRight,
					 localeNumber(m_max, 5));
	painter.drawText(QRectF(0, box.bottom() - fontMetrics().height() / 2, box.left() - 6, fontMetrics().height()), Qt::AlignRight,
					 localeNumber(m_min, 5));
	painter.drawText(QRectF(box.left(), box.bottom() + 4, box.width(), fontMetrics().height()), Qt::AlignLeft,
					 localeNumber(m_first, 5));
	painter.drawText(QRectF(box.left(), box.bottom() + 4, box.width(), fontMetrics().height()), Qt::AlignRight,
					 localeNumber(m_last, 5));
	painter.drawText(QRectF(box.left(), 0, box.width(), fontMetrics().height()), Qt::AlignCenter, modelCurveChannelId(m_channel));
	painter.save();
	painter.setClipRect(box.adjusted(-6, -6, 6, 6));
	QPen cursor(palette().text().color(), 1, Qt::DashLine);
	painter.setPen(cursor);
	const double x = point(m_cursor, 0).x();
	painter.drawLine(QPointF(x, box.top()), QPointF(x, box.bottom()));
	if (!m_keys.isEmpty())
	{
		auto displayed = m_keys;
		if (m_drag >= 2 && m_selected >= 0)
			displayed[m_selected] = m_pending;
		QPainterPath path;
		const int steps = std::clamp(int(box.width()), 100, 4096);
		QVector<double> times;
		for (int i = 0; i <= steps; ++i)
			times.append(m_first + (m_last - m_first) * i / steps);
		// Include exact knots and both one-sided values so held segments display
		// their jump at the key time instead of a diagonal across a sampled pixel.
		for (const auto &key : displayed)
			for (double time :
				 {std::nextafter(double(key.frame), -INFINITY), double(key.frame), std::nextafter(double(key.frame), INFINITY)})
				if (time >= m_first && time <= m_last)
					times.append(time);
		std::sort(times.begin(), times.end());
		times.erase(std::unique(times.begin(), times.end()), times.end());
		for (int i = 0; i < times.size(); ++i)
		{
			const double f = times[i];
			const auto p = point(f, evaluateModelCurve(displayed, f));
			if (i == 0)
				path.moveTo(p);
			else
				path.lineTo(p);
		}
		painter.setPen(QPen(palette().text().color(), 2));
		painter.drawPath(path);
		if (m_selected >= 0)
		{
			const auto handles = tangentPoints();
			const auto p = keyPoint(m_selected);
			painter.setPen(QPen(palette().text().color(), 1, Qt::DashLine));
			painter.setBrush(palette().base());
			if (m_selected > 0 && m_keys[m_selected - 1].interpolation == ModelCurveInterpolation::Cubic)
			{
				painter.drawLine(handles.first, p);
				painter.drawRect(QRectF(handles.first - QPointF(4, 4), QSizeF(8, 8)));
			}
			if (m_keys[m_selected].interpolation == ModelCurveInterpolation::Cubic)
			{
				painter.drawLine(p, handles.second);
				painter.drawRect(QRectF(handles.second - QPointF(4, 4), QSizeF(8, 8)));
			}
		}
		for (int i = 0; i < m_keys.size(); ++i)
		{
			const auto p = keyPoint(i);
			if (!box.adjusted(-6, -6, 6, 6).contains(p))
				continue;
			painter.setPen(QPen(palette().text().color(), i == m_selected ? 2 : 1));
			painter.setBrush(i == m_selected ? palette().highlight() : palette().base());
			painter.drawPolygon(QPolygonF{p + QPointF(0, -5), p + QPointF(5, 0), p + QPointF(0, 5), p + QPointF(-5, 0)});
		}
		if (m_drag)
		{
			painter.setPen(QPen(palette().highlight().color(), 2, Qt::DashLine));
			painter.setBrush(Qt::NoBrush);
			painter.drawEllipse(point(m_pending.frame, m_pending.value), 8, 8);
		}
	}
	painter.restore();
	if (hasFocus())
	{
		painter.setPen(QPen(palette().text().color(), 1, Qt::DotLine));
		painter.drawRect(rect().adjusted(2, 2, -3, -3));
	}
}
void ModelAnimationCurveView::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;
	setFocus(Qt::MouseFocusReason);
	if (m_selected >= 0)
	{
		const auto handles = tangentPoints();
		if (m_selected > 0 && m_keys[m_selected - 1].interpolation == ModelCurveInterpolation::Cubic &&
			QLineF(event->position(), handles.first).length() < 9)
			m_drag = 2;
		else if (m_keys[m_selected].interpolation == ModelCurveInterpolation::Cubic &&
				 QLineF(event->position(), handles.second).length() < 9)
			m_drag = 3;
		if (m_drag)
		{
			m_pending = m_keys[m_selected];
			return;
		}
	}
	selectKey(nearest(event->position()));
	if (m_selected >= 0)
	{
		m_drag = 1;
		m_pending = m_keys[m_selected];
	}
}
void ModelAnimationCurveView::mouseMoveEvent(QMouseEvent *event)
{
	if (!m_drag)
		return;
	const auto p = dataPoint(event->position());
	if (m_drag == 1)
	{
		m_pending.frame = std::clamp(int(std::llround(p.x())), 0, m_frames - 1);
		m_pending.value = withinRange(p.y());
	}
	else
	{
		const double dt = p.x() - m_pending.frame;
		if (std::abs(dt) < .00001)
			return;
		const auto slopes = modelCurveSlopes(m_keys, m_selected);
		m_pending.tangent = ModelCurveTangent::Free;
		m_pending.inSlope = slopes.first;
		m_pending.outSlope = slopes.second;
		const double slope = std::clamp((p.y() - m_pending.value) / dt, -1000000.0, 1000000.0);
		if (m_drag == 2)
			m_pending.inSlope = slope;
		else
			m_pending.outSlope = slope;
	}
	update();
}
void ModelAnimationCurveView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !m_drag)
		return;
	m_drag = 0;
	const int old = m_keys[m_selected].frame;
	if (edited)
		edited(old, m_pending);
	update();
}
void ModelAnimationCurveView::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton || !area().contains(event->position()))
		return;
	const auto p = dataPoint(event->position());
	ModelPoseCurveKey key;
	key.frame = std::clamp(int(std::llround(p.x())), 0, m_frames - 1);
	key.value = withinRange(p.y());
	m_drag = 0;
	if (edited)
		edited(-1, key);
}
void ModelAnimationCurveView::wheelEvent(QWheelEvent *event)
{
	const double factor = event->angleDelta().y() > 0 ? .8 : 1.25;
	const auto anchor = dataPoint(event->position());
	if (event->modifiers() & Qt::ControlModifier)
	{
		const double span = std::clamp((m_max - m_min) * factor, .000001, 2000002.0), ratio = (anchor.y() - m_min) / (m_max - m_min);
		m_min = anchor.y() - span * ratio;
		m_max = m_min + span;
	}
	else
	{
		const double span = std::clamp((m_last - m_first) * factor, .25, double(std::max(1, m_frames - 1))),
					 ratio = (anchor.x() - m_first) / (m_last - m_first);
		m_first = std::clamp(anchor.x() - span * ratio, 0.0, std::max(0.0, m_frames - 1 - span));
		m_last = m_first + span;
	}
	event->accept();
	update();
}
void ModelAnimationCurveView::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape && m_drag)
	{
		m_drag = 0;
		update();
		event->accept();
		return;
	}
	if (event->key() == Qt::Key_Home)
	{
		fit();
		event->accept();
		return;
	}
	if (m_keys.isEmpty())
		return;
	if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
	{
		const int delta = event->key() == Qt::Key_Left ? -1 : 1;
		if (event->modifiers() & Qt::ControlModifier)
		{
			if (m_selected >= 0)
			{
				auto key = m_keys[m_selected];
				key.frame = std::clamp(key.frame + delta, 0, m_frames - 1);
				if (edited)
					edited(m_keys[m_selected].frame, key);
			}
		}
		else
			selectKey(std::clamp(m_selected + delta, 0, int(m_keys.size()) - 1));
		event->accept();
		return;
	}
	if (m_selected >= 0 && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down))
	{
		editSelected(selectedValue() + (event->key() == Qt::Key_Up ? 1 : -1) * ((event->modifiers() & Qt::ShiftModifier) ? 1 : .1));
		event->accept();
		return;
	}
	if (m_selected >= 0 && event->key() == Qt::Key_Delete)
	{
		if (removed)
			removed(m_keys[m_selected].frame);
		event->accept();
		return;
	}
	QWidget::keyPressEvent(event);
}
} // namespace vibestudio
