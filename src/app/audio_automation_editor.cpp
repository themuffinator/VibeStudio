#include "app/audio_automation_editor.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
namespace vibestudio
{
class AudioAutomationPlot final : public QWidget {
  public:
	explicit AudioAutomationPlot(AudioAutomationEditor *editor) : QWidget(editor), m_editor(editor)
	{
		setObjectName("automationPlot");
		setLayoutDirection(Qt::LeftToRight);
		setMinimumHeight(140);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		setAccessibleName(AudioAutomationEditor::tr("Automation curve preview"));
		setAccessibleDescription(AudioAutomationEditor::tr(
		    "Time increases from left to right. Select or drag a point, or double-click to add. Every point can also "
		    "be selected in the table and edited with the controls below."));
		setToolTip(accessibleDescription());
	}

  protected:
	QRectF area() const
	{
		const auto metrics = fontMetrics();
		const int left = std::max(metrics.horizontalAdvance(QString::number(m_editor->m_minimum, 'g', 4)),
		                          metrics.horizontalAdvance(QString::number(m_editor->m_maximum, 'g', 4))) +
		                 12;
		const int top = metrics.height() / 2 + 4;
		return QRectF(left, top, std::max(1, width() - left - 12), std::max(1, height() - top - metrics.height() - 12));
	}
	qint64 end() const
	{
		return std::max(
		    {qint64(1), m_editor->m_end, m_editor->m_points.isEmpty() ? qint64(0) : m_editor->m_points.last().frame});
	}
	QPointF position(qint64 frame, double value) const
	{
		const auto box = area();
		return {box.left() + double(frame) / double(end()) * box.width(),
		        box.bottom() -
		            (value - m_editor->m_minimum) / (m_editor->m_maximum - m_editor->m_minimum) * box.height()};
	}
	AudioAutomationPoint point(QPointF pos) const
	{
		const auto box = area();
		return {qint64(std::llround(std::clamp((pos.x() - box.left()) / box.width(), 0.0, 1.0) * double(end()))),
		        m_editor->m_minimum + std::clamp((box.bottom() - pos.y()) / box.height(), 0.0, 1.0) *
		                                  (m_editor->m_maximum - m_editor->m_minimum)};
	}
	int nearest(QPointF pos) const
	{
		int selected = -1;
		double distance = 144;
		for (int i = 0; i < m_editor->m_points.size(); ++i) {
			const auto &p = std::as_const(m_editor->m_points)[i];
			const auto d = position(p.frame, p.value) - pos;
			const double square = d.x() * d.x() + d.y() * d.y();
			if (square <= distance) {
				selected = i;
				distance = square;
			}
		}
		return selected;
	}
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setLayoutDirection(Qt::LeftToRight);
		painter.setRenderHint(QPainter::Antialiasing);
		const auto box = area();
		painter.fillRect(box, palette().base());
		painter.setPen(palette().mid().color());
		painter.drawRect(box);
		painter.setPen(palette().text().color());
		const int labelHeight = fontMetrics().height() + 4;
		painter.drawText(QRectF(0, box.top() - labelHeight / 2, box.left() - 6, labelHeight),
		                 Qt::AlignRight | Qt::AlignVCenter | Qt::AlignAbsolute,
		                 QString::number(m_editor->m_maximum, 'g', 4));
		painter.drawText(QRectF(0, box.bottom() - labelHeight / 2, box.left() - 6, labelHeight),
		                 Qt::AlignRight | Qt::AlignVCenter | Qt::AlignAbsolute,
		                 QString::number(m_editor->m_minimum, 'g', 4));
		painter.drawText(QRectF(box.left(), box.bottom() + 5, box.width(), labelHeight),
		                 Qt::AlignLeft | Qt::AlignAbsolute, QStringLiteral("0"));
		painter.drawText(QRectF(box.left(), box.bottom() + 5, box.width(), labelHeight),
		                 Qt::AlignRight | Qt::AlignAbsolute, AudioAutomationEditor::tr("Frame %1").arg(end()));
		painter.save();
		painter.setClipRect(box.adjusted(-5, -5, 5, 5));
		const auto &points = std::as_const(m_editor->m_points);
		QPainterPath path;
		path.moveTo(position(0, audioAutomationValue(points, 0, m_editor->m_fallback)));
		for (int i = 0; i < points.size(); ++i) {
			const auto &p = points[i];
			if (i > 0) {
				const auto &previous = points[i - 1];
				if (previous.curve == AudioAutomationCurve::Step)
					path.lineTo(position(p.frame, previous.value));
				else {
					const auto length = p.frame - previous.frame;
					const auto shape = previous.shape.frames ? previous.shape
					                                         : AudioAutomationShape{0, length, previous.value, p.value};
					const double u = double(shape.offset) / double(shape.frames);
					const double v = double(shape.offset + length) / double(shape.frames);
					const double delta = shape.last - shape.first;
					const bool smooth = previous.curve == AudioAutomationCurve::Smooth;
					const double last = shape.first + delta * (smooth ? v * v * (3 - 2 * v) : v);
					const auto a = position(previous.frame, previous.value), b = position(p.frame, last);
					if (smooth) {
						// A cut smoothstep retains its original tangents. The segment
						// can end at a different value from the next point at a splice.
						const double scale = delta * double(length) / double(shape.frames) * 2;
						path.cubicTo(
						    QPointF(a.x() + (b.x() - a.x()) / 3,
						            position(previous.frame, previous.value + scale * u * (1 - u)).y()),
						    QPointF(b.x() - (b.x() - a.x()) / 3, position(p.frame, last - scale * v * (1 - v)).y()), b);
					} else
						path.lineTo(b);
				}
			}
			path.lineTo(position(p.frame, p.value));
		}
		path.lineTo(position(end(), audioAutomationValue(points, end(), m_editor->m_fallback)));
		painter.setPen(QPen(palette().text().color(), 2));
		painter.drawPath(path);
		for (int i = 0; i < points.size(); ++i) {
			const auto p = position(points[i].frame, points[i].value);
			const bool selected = i == m_editor->m_table->currentRow();
			painter.setBrush(selected ? palette().highlight() : palette().base());
			painter.setPen(QPen(palette().text().color(), selected ? 2 : 1));
			if (selected)
				painter.drawRect(QRectF(p.x() - 5, p.y() - 5, 10, 10));
			else
				painter.drawEllipse(p, 3, 3);
		}
		painter.restore();
	}
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton)
			return;
		m_drag = nearest(event->position());
		if (m_drag >= 0)
			m_editor->selectPoint(m_drag);
	}
	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (m_drag < 0 || !(event->buttons() & Qt::LeftButton))
			return;
		auto p = point(event->position());
		const auto &points = std::as_const(m_editor->m_points);
		p.frame = std::clamp(p.frame, m_drag > 0 ? points[m_drag - 1].frame + 1 : qint64(0),
		                     m_drag + 1 < points.size() ? points[m_drag + 1].frame - 1 : AudioAutomationFrameLimit);
		p.curve = points[m_drag].curve;
		m_editor->setPoint(m_drag, p);
	}
	void mouseReleaseEvent(QMouseEvent *) override { m_drag = -1; }
	void mouseDoubleClickEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton && area().contains(event->position())) {
			const auto p = point(event->position());
			m_editor->addPoint(p.frame, p.value);
		}
	}

  private:
	AudioAutomationEditor *m_editor;
	int m_drag = -1;
};
AudioAutomationEditor::AudioAutomationEditor(const QString &label, double minimum, double maximum, double fallback,
                                             qint64 cursor, qint64 end, QWidget *parent)
    : QWidget(parent), m_minimum(minimum), m_maximum(maximum), m_fallback(fallback), m_cursor(cursor),
      m_end(std::clamp(end, qint64(1), AudioAutomationFrameLimit))
{
	setAccessibleName(label);
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	m_plot = new AudioAutomationPlot(this);
	layout->addWidget(m_plot);
	m_table = new QTableWidget(0, 3);
	m_table->setObjectName("automationPoints");
	m_table->setAccessibleName(tr("%1 automation points").arg(label));
	m_table->setHorizontalHeaderLabels({tr("Frame"), tr("Value"), tr("Curve")});
	m_table->horizontalHeaderItem(1)->setToolTip(label);
	m_table->horizontalHeaderItem(2)->setToolTip(tr("Outgoing curve"));
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setMinimumHeight(110);
	layout->addWidget(m_table, 1);
	auto *form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_frame = new QDoubleSpinBox;
	m_frame->setObjectName("automationFrame");
	m_frame->setDecimals(0);
	m_frame->setRange(0, double(AudioAutomationFrameLimit));
	m_frame->setAccessibleName(tr("Point frame"));
	m_frame->setKeyboardTracking(false);
	m_value = new QDoubleSpinBox;
	m_value->setObjectName("automationValue");
	m_value->setDecimals(9);
	m_value->setRange(minimum, maximum);
	m_value->setAccessibleName(label);
	m_value->setKeyboardTracking(false);
	m_curve = new QComboBox;
	m_curve->setObjectName("automationCurve");
	m_curve->setAccessibleName(tr("Outgoing curve"));
	m_curve->setToolTip(tr("Editing a point's frame or value redefines its adjacent curves. Changing its curve "
	                       "replaces any inherited outgoing segment."));
	for (auto curve : {AudioAutomationCurve::Linear, AudioAutomationCurve::Step, AudioAutomationCurve::Smooth})
		m_curve->addItem(audioAutomationCurveName(curve), int(curve));
	form->addRow(tr("Point &frame"), m_frame);
	form->addRow(label, m_value);
	form->addRow(tr("Outgoing &curve"), m_curve);
	layout->addLayout(form);
	auto *actions = new QFormLayout;
	actions->setRowWrapPolicy(QFormLayout::WrapLongRows);
	actions->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_add = new QPushButton(tr("Add at Cursor"));
	m_add->setObjectName("automationAdd");
	m_remove = new QPushButton(tr("Remove Point"));
	m_remove->setObjectName("automationRemove");
	m_clear = new QPushButton(tr("Clear"));
	m_clear->setObjectName("automationClear");
	for (auto *button : {m_add, m_remove, m_clear}) {
		button->setAccessibleName(button->text());
	}
	actions->addRow(m_add, m_remove);
	actions->addRow(m_clear);
	layout->addLayout(actions);
	m_status = new QLabel;
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Automation status"));
	layout->addWidget(m_status);
	connect(m_table, &QTableWidget::currentCellChanged, this, [this] { selectionChanged(); });
	connect(m_frame, &QDoubleSpinBox::valueChanged, this, [this] { updateSelected(); });
	connect(m_value, &QDoubleSpinBox::valueChanged, this, [this] { updateSelected(); });
	connect(m_curve, &QComboBox::currentIndexChanged, this, [this] { updateSelected(); });
	connect(m_add, &QPushButton::clicked, this,
	        [this] { addPoint(m_cursor, audioAutomationValue(m_points, m_cursor, m_fallback)); });
	connect(m_remove, &QPushButton::clicked, this, [this] {
		const int row = m_table->currentRow();
		if (row >= 0) {
			m_points.removeAt(row);
			if (row > 0)
				m_points[row - 1].shape = {};
			refresh(std::min(row, int(m_points.size()) - 1));
			emit changed();
		}
	});
	connect(m_clear, &QPushButton::clicked, this, [this] { setPoints({}); });
	refresh(-1);
}
bool AudioAutomationEditor::setPoints(const QVector<AudioAutomationPoint> &points)
{
	if (!validAudioAutomation(points, m_minimum, m_maximum))
		return false;
	m_points = points;
	refresh(points.isEmpty() ? -1 : 0);
	emit changed();
	return true;
}
bool AudioAutomationEditor::setPoint(int index, AudioAutomationPoint point)
{
	if (index < 0 || index >= m_points.size())
		return false;
	auto next = m_points;
	const auto &old = m_points.at(index);
	const bool moved = point.frame != old.frame || point.value != old.value;
	point.shape = moved || point.curve != old.curve ? AudioAutomationShape{} : old.shape;
	if (moved && index > 0)
		next[index - 1].shape = {};
	next[index] = point;
	if (!validAudioAutomation(next, m_minimum, m_maximum))
		return false;
	m_points = std::move(next);
	refresh(index);
	emit changed();
	return true;
}
bool AudioAutomationEditor::addPoint(qint64 frame, double value)
{
	const auto at = std::lower_bound(m_points.cbegin(), m_points.cend(), frame,
	                                 [](const auto &p, qint64 f) { return p.frame < f; });
	const int row = int(at - m_points.cbegin());
	if (at != m_points.cend() && at->frame == frame) {
		selectPoint(row);
		return false;
	}
	auto next = m_points;
	AudioAutomationPoint point{frame, value};
	if (value == audioAutomationValue(m_points, frame, m_fallback)) {
		point = audioAutomationBoundary(m_points, frame, m_fallback);
		if (row > 0)
			next[row - 1] = audioAutomationBoundary(m_points, next.at(row - 1).frame, m_fallback);
	} else if (row > 0)
		next[row - 1].shape = {};
	next.insert(row, point);
	if (!validAudioAutomation(next, m_minimum, m_maximum))
		return false;
	m_points = std::move(next);
	refresh(row);
	emit changed();
	return true;
}
void AudioAutomationEditor::selectPoint(int index)
{
	if (index >= 0 && index < m_points.size())
		m_table->setCurrentCell(index, 0);
}
void AudioAutomationEditor::refresh(int selected)
{
	m_updating = true;
	m_table->setRowCount(int(m_points.size()));
	for (int i = 0; i < m_points.size(); ++i) {
		const auto &p = std::as_const(m_points)[i];
		const auto numeric = [](const QString &text) { return QChar(0x2066) + text + QChar(0x2069); };
		const QStringList values{numeric(QString::number(p.frame)), numeric(QString::number(p.value, 'g', 15)),
		                         p.shape.frames ? tr("%1 (segment)").arg(audioAutomationCurveName(p.curve))
		                                        : audioAutomationCurveName(p.curve)};
		for (int c = 0; c < 3; ++c) {
			if (!m_table->item(i, c))
				m_table->setItem(i, c, new QTableWidgetItem);
			m_table->item(i, c)->setText(values[c]);
			m_table->item(i, c)->setToolTip(
			    p.shape.frames ? tr("Preserved curve: offset %1 of %2 frames.").arg(p.shape.offset).arg(p.shape.frames)
			                   : QString());
		}
	}
	m_table->setCurrentCell(selected, 0);
	m_updating = false;
	selectionChanged();
	m_add->setEnabled(m_points.size() < AudioAutomationPointLimit);
	m_clear->setEnabled(!m_points.isEmpty());
	m_status->setText(
	    m_points.isEmpty()
	        ? tr("No points — using the static value.")
	        : tr("%1 points. Values hold before the first and after the last point.").arg(m_points.size()));
}
void AudioAutomationEditor::selectionChanged()
{
	if (m_updating)
		return;
	m_updating = true;
	const int row = m_table->currentRow();
	const bool valid = row >= 0 && row < m_points.size();
	for (QWidget *widget : std::array<QWidget *, 4>{m_frame, m_value, m_curve, m_remove})
		widget->setEnabled(valid);
	if (valid) {
		const auto &p = std::as_const(m_points)[row];
		m_frame->setRange(row > 0 ? double(m_points.at(row - 1).frame + 1) : 0,
		                  row + 1 < m_points.size() ? double(m_points.at(row + 1).frame - 1)
		                                            : double(AudioAutomationFrameLimit));
		m_frame->setValue(double(p.frame));
		m_value->setValue(p.value);
		m_curve->setCurrentIndex(m_curve->findData(int(p.curve)));
	}
	m_updating = false;
	m_plot->update();
}
void AudioAutomationEditor::updateSelected()
{
	if (m_updating)
		return;
	const int row = m_table->currentRow();
	if (row < 0 || row >= m_points.size())
		return;
	auto point = m_points.at(row);
	if (sender() == m_frame)
		point.frame = qint64(m_frame->value());
	if (sender() == m_value)
		point.value = m_value->value();
	if (sender() == m_curve)
		point.curve = AudioAutomationCurve(m_curve->currentData().toInt());
	setPoint(row, point);
}
} // namespace vibestudio
