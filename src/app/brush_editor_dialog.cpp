#include "app/brush_editor_dialog.h"
#include "app/model_viewport.h"
#include "core/studio_settings.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace vibestudio
{
namespace
{
int componentCount(const LevelBrushTopology &t, LevelBrushComponent kind)
{
	return static_cast<int>(kind == LevelBrushComponent::Vertex ? t.vertices.size()
							: kind == LevelBrushComponent::Edge ? t.edges.size()
																: t.faces.size());
}
LevelMapVec3 center(const LevelBrushTopology &t, LevelBrushComponent kind, int id)
{
	const auto ids = levelBrushComponentVertices(t, kind, {id});
	LevelMapVec3 p{0, 0, 0, true};
	for (int v : ids) {
		p.x += t.vertices[v].x;
		p.y += t.vertices[v].y;
		p.z += t.vertices[v].z;
	}
	if (!ids.isEmpty()) {
		p.x /= ids.size();
		p.y /= ids.size();
		p.z /= ids.size();
	}
	return p;
}
double segmentDistance(const QPointF &p, const QPointF &a, const QPointF &b)
{
	const QPointF ab = b - a;
	const double squared = QPointF::dotProduct(ab, ab);
	const double t = squared > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / squared, 0.0, 1.0) : 0;
	return QLineF(p, a + ab * t).length();
}
} // namespace

BrushComponentView::BrushComponentView(QWidget *parent) : QWidget(parent)
{
	static const bool installed = []() {
		QAccessible::installFactory([](const QString &, QObject *object) -> QAccessibleInterface * {
			if (auto *view = qobject_cast<BrushComponentView *>(object)) {
				return new QAccessibleWidget(view, QAccessible::Graphic);
			}
			return nullptr;
		});
		return true;
	}();
	Q_UNUSED(installed);
	setObjectName(QStringLiteral("brushComponentView"));
	setAccessibleName(tr("Brush component view"));
	setFocusPolicy(Qt::StrongFocus);
	setToolTip(tr("Click to select; Ctrl-click toggles a component. Drag or use "
				  "arrow keys to move. Escape cancels a drag. Use the "
				  "component table for precise selection and coordinates."));
}
void BrushComponentView::setTopology(const LevelBrushTopology &topology)
{
	m_topology = topology;
	m_dragging = false;
	m_drag = {};
	setSelection(m_selected);
	if (m_scale <= 0) {
		frameBrush();
	}
	update();
}
void BrushComponentView::setMode(LevelBrushComponent kind)
{
	m_kind = kind;
	setSelection({});
}
void BrushComponentView::setSelection(const QVector<int> &selection)
{
	QVector<int> valid;
	for (int i : selection) {
		if (i >= 0 && i < componentCount(m_topology, m_kind) && !valid.contains(i)) {
			valid << i;
		}
	}
	m_selected = valid;
	setAccessibleDescription(tr("%1 components, %2 selected. The adjacent table "
								"exposes component identities and coordinates.")
								 .arg(componentCount(m_topology, m_kind))
								 .arg(valid.size()));
	QAccessibleEvent event(this, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&event);
	update();
}
QPointF BrushComponentView::project(const LevelMapVec3 &p) const
{
	return m_plane == 1 ? QPointF(p.x, p.z) : m_plane == 2 ? QPointF(p.y, p.z) : QPointF(p.x, p.y);
}
QPointF BrushComponentView::screen(const LevelMapVec3 &p) const
{
	const auto q = project(p) - m_center;
	return {width() / 2.0 + q.x() * m_scale, height() / 2.0 - q.y() * m_scale};
}
LevelMapVec3 BrushComponentView::movement(const QPointF &p) const
{
	const auto snap = [this](double v) { return m_grid > 0 ? std::round(v / m_grid) * m_grid : v; };
	const double x = snap(p.x() / std::max(m_scale, 0.0001)), y = snap(-p.y() / std::max(m_scale, 0.0001));
	return m_plane == 1 ? LevelMapVec3{x, 0, y, true} : m_plane == 2 ? LevelMapVec3{0, x, y, true} : LevelMapVec3{x, y, 0, true};
}
void BrushComponentView::setPlane(int plane)
{
	if (plane >= 0 && plane <= 2) {
		m_plane = plane;
		frameBrush();
	}
}
void BrushComponentView::frameBrush()
{
	if (m_topology.vertices.isEmpty()) {
		return;
	}
	QPointF lo = project(m_topology.vertices.first()), hi = lo;
	for (const auto &v : m_topology.vertices) {
		const auto p = project(v);
		lo.setX(std::min(lo.x(), p.x()));
		lo.setY(std::min(lo.y(), p.y()));
		hi.setX(std::max(hi.x(), p.x()));
		hi.setY(std::max(hi.y(), p.y()));
	}
	m_center = (lo + hi) / 2;
	m_scale = std::max(0.001, std::min(std::max(1, width() - 80) / std::max(32.0, hi.x() - lo.x()),
									   std::max(1, height() - 80) / std::max(32.0, hi.y() - lo.y())));
	update();
}
QPointF BrushComponentView::componentPosition(int id) const { return screen(center(m_topology, m_kind, id)); }
int BrushComponentView::componentAt(const QPointF &p) const
{
	int best = -1;
	double distance = std::max(11.0, fontMetrics().height() * 0.65), depth = -1e100;
	for (int i = 0; i < componentCount(m_topology, m_kind); ++i) {
		double d = QLineF(p, componentPosition(i)).length();
		if (m_kind == LevelBrushComponent::Edge) {
			const auto e = m_topology.edges[i];
			d = segmentDistance(p, screen(m_topology.vertices[e[0]]), screen(m_topology.vertices[e[1]]));
		} else if (m_kind == LevelBrushComponent::Face) {
			QPolygonF polygon;
			for (int v : m_topology.faces[i]) {
				polygon << screen(m_topology.vertices[v]);
			}
			if (polygon.containsPoint(p, Qt::OddEvenFill)) {
				d = 0;
			}
		}
		const auto c = center(m_topology, m_kind, i);
		const double z = m_plane == 1 ? -c.y : m_plane == 2 ? c.x : c.z;
		if (d < distance - 0.01 || (d <= distance + 0.01 && z > depth)) {
			best = i;
			distance = d;
			depth = z;
		}
	}
	return best;
}
void BrushComponentView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.fillRect(rect(), palette().base());
	const auto delta = movement(m_drag);
	const auto selectedVertices = levelBrushComponentVertices(m_topology, m_kind, m_selected);
	const auto point = [&](int id) {
		auto p = m_topology.vertices[id];
		if (m_dragging && selectedVertices.contains(id)) {
			p.x += delta.x;
			p.y += delta.y;
			p.z += delta.z;
		}
		return screen(p);
	};
	if (m_grid > 0 && m_grid * m_scale >= 8) {
		painter.setPen(palette().mid().color());
		const auto origin = screen({0, 0, 0, true});
		const double step = m_grid * m_scale;
		for (double x = std::fmod(origin.x(), step); x < width(); x += step) {
			painter.drawLine(QPointF(x, 0), QPointF(x, height()));
		}
		for (double y = std::fmod(origin.y(), step); y < height(); y += step) {
			painter.drawLine(QPointF(0, y), QPointF(width(), y));
		}
	}
	if (m_kind == LevelBrushComponent::Face) {
		for (int f : m_selected) {
			QPolygonF polygon;
			for (int v : m_topology.faces[f]) {
				polygon << point(v);
			}
			auto fill = palette().highlight().color();
			fill.setAlpha(70);
			painter.setPen(QPen(palette().highlight(), 3));
			painter.setBrush(fill);
			painter.drawPolygon(polygon);
		}
	}
	for (int i = 0; i < m_topology.edges.size(); ++i) {
		const auto e = m_topology.edges[i];
		painter.setPen(QPen(m_kind == LevelBrushComponent::Edge && m_selected.contains(i) ? palette().highlight() : palette().text(),
							m_kind == LevelBrushComponent::Edge && m_selected.contains(i) ? 3 : 1));
		painter.drawLine(point(e[0]), point(e[1]));
	}
	for (int i = 0; i < componentCount(m_topology, m_kind); ++i) {
		const bool selected = m_selected.contains(i);
		const auto p = m_kind == LevelBrushComponent::Vertex ? point(i) : componentPosition(i);
		painter.setPen(QPen(palette().text(), 1));
		painter.setBrush(selected ? palette().highlight() : palette().base());
		const double radius = std::max(5.0, fontMetrics().height() * 0.28);
		if (selected) {
			painter.drawRect(QRectF(p - QPointF(radius, radius), QSizeF(radius * 2, radius * 2)));
		} else {
			painter.drawEllipse(p, radius * 0.7, radius * 0.7);
		}
		if (selected) {
			painter.drawText(p + QPointF(8, -8), QString::number(i));
		}
	}
	if (hasFocus()) {
		painter.setPen(QPen(palette().highlight(), 2, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(rect().adjusted(2, 2, -3, -3));
	}
}
void BrushComponentView::mousePressEvent(QMouseEvent *event)
{
	setFocus();
	m_press = m_last = event->position();
	m_drag = {};
	if (event->button() == Qt::MiddleButton) {
		m_panning = true;
		return;
	}
	if (event->button() != Qt::LeftButton) {
		return;
	}
	const int id = componentAt(event->position());
	auto selected = m_selected;
	if (event->modifiers().testFlag(Qt::ControlModifier)) {
		if (selected.contains(id)) {
			selected.removeAll(id);
		} else if (id >= 0) {
			selected << id;
		}
	} else if (!selected.contains(id)) {
		selected = id >= 0 ? QVector<int>{id} : QVector<int>{};
	}
	setSelection(selected);
	emit selectionChanged(selected);
	m_dragging = id >= 0 && selected.contains(id);
}
void BrushComponentView::mouseMoveEvent(QMouseEvent *event)
{
	if (m_panning) {
		const auto d = event->position() - m_last;
		m_center += QPointF(-d.x() / m_scale, d.y() / m_scale);
		m_last = event->position();
	}
	if (m_dragging) {
		m_drag = event->position() - m_press;
	}
	update();
}
void BrushComponentView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::MiddleButton) {
		m_panning = false;
	}
	if (event->button() == Qt::LeftButton && m_dragging) {
		const auto d = movement(event->position() - m_press);
		m_dragging = false;
		m_drag = {};
		if (d.x != 0 || d.y != 0 || d.z != 0) {
			emit moveRequested(d);
		}
	}
	update();
}
void BrushComponentView::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_F && event->modifiers() == Qt::NoModifier) {
		frameBrush();
		return;
	}
	if (event->key() == Qt::Key_Escape) {
		m_dragging = false;
		m_panning = false;
		m_drag = {};
		update();
		return;
	}
	if (event->matches(QKeySequence::SelectAll)) {
		QVector<int> all(componentCount(m_topology, m_kind));
		std::iota(all.begin(), all.end(), 0);
		setSelection(all);
		emit selectionChanged(all);
		return;
	}
	QPointF d;
	if (event->key() == Qt::Key_Left) {
		d.setX(-1);
	} else if (event->key() == Qt::Key_Right) {
		d.setX(1);
	} else if (event->key() == Qt::Key_Up) {
		d.setY(-1);
	} else if (event->key() == Qt::Key_Down) {
		d.setY(1);
	} else {
		QWidget::keyPressEvent(event);
		return;
	}
	if (!m_selected.isEmpty()) {
		emit moveRequested(movement(d * std::max(1.0, m_grid) * m_scale));
	}
}
void BrushComponentView::wheelEvent(QWheelEvent *event)
{
	m_scale = std::clamp(m_scale * std::pow(1.2, event->angleDelta().y() / 120.0), 0.001, 1000.0);
	update();
	event->accept();
}

void BrushComponentView::focusOutEvent(QFocusEvent *event)
{
	m_dragging = false;
	m_panning = false;
	m_drag = {};
	update();
	QWidget::focusOutEvent(event);
}

BrushEditorDialog::BrushEditorDialog(const LevelMapBrush &brush, double grid, QWidget *parent) : QDialog(parent), m_brush(brush)
{
	setObjectName(QStringLiteral("brushEditorDialog"));
	setWindowTitle(tr("Edit Brush %1 Components").arg(brush.id));
	setAccessibleName(windowTitle());
	resize(1200, 800);
	auto *layout = new QVBoxLayout(this);
	auto *tools = new QToolBar(tr("Brush component actions"), this);
	tools->setAccessibleName(tools->windowTitle());
	tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
	m_undoAction = tools->addAction(tr("Undo"));
	m_undoAction->setObjectName(QStringLiteral("brushComponentUndo"));
	m_redoAction = tools->addAction(tr("Redo"));
	m_redoAction->setObjectName(QStringLiteral("brushComponentRedo"));
	m_undoAction->setShortcut(QKeySequence::Undo);
	m_redoAction->setShortcut(QKeySequence::Redo);
	for (auto *action : {m_undoAction, m_redoAction}) {
		action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		addAction(action);
	}
	connect(m_undoAction, &QAction::triggered, this, &BrushEditorDialog::undo);
	connect(m_redoAction, &QAction::triggered, this, &BrushEditorDialog::redo);
	layout->addWidget(tools);
	auto *split = new QSplitter(this);
	split->setChildrenCollapsible(false);
	layout->addWidget(split, 1);
	auto *left = new QWidget(split);
	auto *leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	auto *viewTools = new QHBoxLayout;
	m_mode = new QComboBox(left);
	m_mode->setObjectName(QStringLiteral("brushComponentMode"));
	m_mode->setAccessibleName(tr("Component selection mode"));
	m_mode->addItems({tr("Vertices"), tr("Edges"), tr("Faces")});
	viewTools->addWidget(m_mode);
	auto *plane = new QComboBox(left);
	plane->setAccessibleName(tr("Brush view projection"));
	plane->addItems({tr("Top XY"), tr("Front XZ"), tr("Side YZ")});
	viewTools->addWidget(plane);
	auto *frame = new QPushButton(tr("Frame"), left);
	frame->setAccessibleName(tr("Frame brush in both views"));
	viewTools->addWidget(frame);
	leftLayout->addLayout(viewTools);
	auto *tabs = new QTabWidget(left);
	tabs->setAccessibleName(tr("Brush views"));
	leftLayout->addWidget(tabs, 1);
	m_view = new BrushComponentView(tabs);
	m_view->setGrid(grid);
	tabs->addTab(m_view, tr("Components"));
	m_preview = new ModelViewport(tabs);
	m_preview->setObjectName(QStringLiteral("brushComponentPreview"));
	m_preview->setAccessibleName(tr("Brush surface preview"));
	m_preview->setShowEdges(true);
	const auto preferences = StudioSettings().accessibilityPreferences();
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(preferences.reducedMotion);
	tabs->addTab(m_preview, tr("Surface"));
	connect(tabs, &QTabWidget::currentChanged, this, [this, first = true](int index) mutable {
		if (index == 1 && first) {
			m_preview->frameModel();
			first = false;
		}
	});
	connect(frame, &QPushButton::clicked, this, [this]() {
		m_view->frameBrush();
		m_preview->frameModel();
	});
	connect(plane, &QComboBox::currentIndexChanged, m_view, &BrushComponentView::setPlane);
	connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (!m_refreshing) {
			m_kind = static_cast<LevelBrushComponent>(index);
			m_selection.clear();
			refresh();
		}
	});
	connect(m_view, &BrushComponentView::selectionChanged, this, &BrushEditorDialog::syncSelection);
	connect(m_view, &BrushComponentView::moveRequested, this, [this](const auto &d) { moveSelected(d); });
	connect(m_preview, &ModelViewport::trianglePicked, this, [this](int, int triangle, int pick) {
		if (triangle < 0 || triangle >= m_mesh.ownerFaces.size()) {
			return;
		}
		const int face = m_mesh.ownerFaces[triangle];
		auto ids = m_kind == LevelBrushComponent::Face ? m_selection : QVector<int>{};
		if (pick == static_cast<int>(ModelViewportPick::Toggle) || pick == static_cast<int>(ModelViewportPick::FaceToggle)) {
			if (ids.contains(face)) {
				ids.removeAll(face);
			} else {
				ids << face;
			}
		} else {
			ids = {face};
		}
		selectComponents(LevelBrushComponent::Face, ids);
	});
	auto *scroll = new QScrollArea(split);
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Brush component properties"));
	auto *properties = new QWidget(scroll);
	scroll->setWidget(properties);
	auto *fields = new QVBoxLayout(properties);
	auto *form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	auto *snap = new QDoubleSpinBox(properties);
	snap->setObjectName(QStringLiteral("brushComponentGrid"));
	snap->setAccessibleName(tr("Component snap grid"));
	snap->setDecimals(3);
	snap->setRange(0, 32768);
	snap->setValue(grid);
	snap->setSpecialValueText(tr("No snap"));
	form->addRow(tr("Grid"), snap);
	connect(snap, &QDoubleSpinBox::valueChanged, m_view, &BrushComponentView::setGrid);
	std::array<QDoubleSpinBox *, 3> delta{};
	for (int i = 0; i < 3; ++i) {
		delta[i] = new QDoubleSpinBox(properties);
		delta[i]->setObjectName(QStringLiteral("brushComponentDelta%1").arg(i));
		delta[i]->setAccessibleName(tr("Move along %1").arg(QStringLiteral("XYZ").mid(i, 1)));
		delta[i]->setRange(-32768, 32768);
		delta[i]->setDecimals(4);
		form->addRow(delta[i]->accessibleName(), delta[i]);
	}
	fields->addLayout(form);
	auto *move = new QPushButton(tr("Move Selected"), properties);
	move->setObjectName(QStringLiteral("brushComponentMove"));
	fields->addWidget(move);
	connect(move, &QPushButton::clicked, this,
			[this, delta, snap]() { moveSelected({delta[0]->value(), delta[1]->value(), delta[2]->value(), true}, snap->value()); });
	auto *snapButton = new QPushButton(tr("Snap Selected"), properties);
	snapButton->setObjectName(QStringLiteral("brushComponentSnap"));
	fields->addWidget(snapButton);
	connect(snapButton, &QPushButton::clicked, this, [this, snap]() { moveSelected({0, 0, 0, true}, snap->value()); });
	m_allowCollapse = new QCheckBox(tr("Allow Vertex Collapse"), properties);
	m_allowCollapse->setObjectName(QStringLiteral("brushAllowCollapse"));
	m_allowCollapse->setToolTip(tr("Allow merged or interior vertices to disappear from the convex "
								   "result. Undo restores them."));
	fields->addWidget(m_allowCollapse);
	m_table = new QTableWidget(properties);
	m_table->setObjectName(QStringLiteral("brushComponents"));
	m_table->setAccessibleName(tr("Brush components"));
	m_table->setAccessibleDescription(tr("Zero-based component IDs. Edit a vertex coordinate or an edge or "
										 "face center coordinate to move that component."));
	m_table->setLayoutDirection(Qt::LeftToRight);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_table->setColumnCount(4);
	m_table->setHorizontalHeaderLabels({tr("Vertices"), QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_table->setMinimumHeight(240);
	fields->addWidget(m_table, 1);
	connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
		if (m_refreshing) {
			return;
		}
		QVector<int> ids;
		for (const auto &row : m_table->selectionModel()->selectedRows()) {
			ids << row.row();
		}
		syncSelection(ids);
	});
	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
		if (m_refreshing || item->column() == 0) {
			return;
		}
		bool ok = false;
		const double value = item->text().toDouble(&ok);
		const int id = item->row();
		const int axis = item->column() - 1;
		if (!ok || !std::isfinite(value)) {
			refresh();
			m_status->setText(tr("Enter a finite coordinate."));
			return;
		}
		const auto p = componentCenter(id);
		LevelMapVec3 d{0, 0, 0, true};
		if (axis == 0) {
			d.x = value - p.x;
		} else if (axis == 1) {
			d.y = value - p.y;
		} else {
			d.z = value - p.z;
		}
		syncSelection({id});
		if (!moveSelected(d)) {
			const auto message = m_status->text();
			refresh();
			m_status->setText(message);
		}
	});
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("brushComponentStatus"));
	m_status->setAccessibleName(tr("Brush edit status"));
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(m_status);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttons);
	connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &BrushEditorDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	refresh();
}
void BrushEditorDialog::setApplyHandler(std::function<bool(const LevelMapBrush &, QString *)> handler)
{
	m_applyHandler = std::move(handler);
}
void BrushEditorDialog::accept()
{
	QString error;
	if (m_applyHandler && !m_applyHandler(m_brush, &error)) {
		m_status->setText(error.isEmpty() ? tr("The brush could not be applied. Your draft is still open.") : error);
		return;
	}
	QDialog::accept();
}
void BrushEditorDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	if (m_firstShow) {
		m_firstShow = false;
		m_view->frameBrush();
		m_preview->frameModel();
	}
}
LevelMapVec3 BrushEditorDialog::componentCenter(int id) const { return center(m_topology, m_kind, id); }
BrushEditorDialog::State BrushEditorDialog::state() const { return {m_brush, m_kind, m_selection}; }
void BrushEditorDialog::restore(const State &s)
{
	m_brush = s.brush;
	m_kind = s.kind;
	m_selection = s.selection;
	refresh();
}
void BrushEditorDialog::undo()
{
	if (!m_undo.isEmpty()) {
		m_redo << state();
		restore(m_undo.takeLast());
	}
}
void BrushEditorDialog::redo()
{
	if (!m_redo.isEmpty()) {
		m_undo << state();
		restore(m_redo.takeLast());
	}
}
void BrushEditorDialog::selectComponents(LevelBrushComponent kind, const QVector<int> &selection)
{
	m_kind = kind;
	m_selection = selection;
	refresh();
}
void BrushEditorDialog::syncSelection(const QVector<int> &selection)
{
	const bool refreshing = m_refreshing;
	m_refreshing = true;
	m_view->setSelection(selection);
	m_selection = m_view->selection();
	m_table->clearSelection();
	for (int id : m_selection) {
		m_table->selectionModel()->select(m_table->model()->index(id, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	}
	const auto vertices = levelBrushComponentVertices(m_topology, m_kind, m_selection);
	QVector<int> triangles;
	for (int i = 0; i < m_mesh.ownerFaces.size(); ++i) {
		const int f = m_mesh.ownerFaces[i];
		if (f < 0 || f >= m_topology.faces.size()) {
			continue;
		}
		if (m_kind == LevelBrushComponent::Face
				? m_selection.contains(f)
				: std::any_of(m_topology.faces[f].cbegin(), m_topology.faces[f].cend(), [&](int v) { return vertices.contains(v); })) {
			triangles << i;
		}
	}
	m_preview->setHighlightedTriangles(triangles);
	m_refreshing = refreshing;
}
void BrushEditorDialog::refresh()
{
	m_refreshing = true;
	QString error;
	if (!levelBrushTopology(m_brush, &m_topology, &error)) {
		m_status->setText(error);
		m_refreshing = false;
		return;
	}
	m_mode->setCurrentIndex(static_cast<int>(m_kind));
	m_table->horizontalHeaderItem(0)->setText(m_kind == LevelBrushComponent::Face ? tr("Material") : tr("Vertices"));
	m_view->setTopology(m_topology);
	m_view->setMode(m_kind);
	const int count = componentCount(m_topology, m_kind);
	m_table->setRowCount(count);
	QStringList rowLabels;
	for (int i = 0; i < count; ++i) {
		rowLabels << QString::number(i);
		QStringList ids;
		for (int v : levelBrushComponentVertices(m_topology, m_kind, {i})) {
			ids << QString::number(v);
		}
		auto *description =
			new QTableWidgetItem(m_kind == LevelBrushComponent::Face ? m_brush.faces[i].textureName : ids.join(QStringLiteral(", ")));
		description->setFlags(description->flags() & ~Qt::ItemIsEditable);
		description->setToolTip(tr("Vertex IDs: %1").arg(ids.join(QStringLiteral(", "))));
		m_table->setItem(i, 0, description);
		const auto p = componentCenter(i);
		const double values[]{p.x, p.y, p.z};
		for (int c = 0; c < 3; ++c) {
			m_table->setItem(i, c + 1, new QTableWidgetItem(QString::number(values[c], 'g', 10)));
		}
	}
	m_table->setVerticalHeaderLabels(rowLabels);
	LevelMapDocument document;
	document.format = LevelMapFormat::QuakeMap;
	document.brushes = {m_brush};
	m_mesh = buildLevelMapPreviewMesh(document);
	m_preview->setMesh(m_mesh.mesh, true);
	m_undoAction->setEnabled(!m_undo.isEmpty());
	m_redoAction->setEnabled(!m_redo.isEmpty());
	syncSelection(m_selection);
	m_status->setText(tr("%1 vertices · %2 edges · %3 faces. Material mapping "
						 "stays in its stored coordinate frame.")
						  .arg(m_topology.vertices.size())
						  .arg(m_topology.edges.size())
						  .arg(m_topology.faces.size()));
	m_refreshing = false;
}
bool BrushEditorDialog::moveSelected(const LevelMapVec3 &delta, double grid)
{
	const auto previous = state();
	auto draft = m_brush;
	LevelBrushEditReport report;
	QString error;
	if (!moveLevelBrushComponents(&draft, m_kind, m_selection, delta, grid, m_allowCollapse->isChecked(), &report, &error)) {
		m_status->setText(error);
		return false;
	}
	if (!report.changed) {
		return true;
	}
	QVector<LevelMapVec3> moved;
	for (int id : levelBrushComponentVertices(m_topology, m_kind, m_selection)) {
		auto p = m_topology.vertices[id];
		p.x += delta.x;
		p.y += delta.y;
		p.z += delta.z;
		moved << snapLevelMapPosition(p, grid);
	}
	m_undo << previous;
	if (m_undo.size() > 200) {
		m_undo.removeFirst();
	}
	m_redo.clear();
	m_brush = draft;
	levelBrushTopology(m_brush, &m_topology);
	m_selection.clear();
	for (int i = 0; i < componentCount(m_topology, m_kind); ++i) {
		const auto ids = levelBrushComponentVertices(m_topology, m_kind, {i});
		if (std::all_of(ids.cbegin(), ids.cend(), [&](int v) {
				const auto p = m_topology.vertices[v];
				return std::any_of(moved.cbegin(), moved.cend(), [&](const auto &q) {
					return std::abs(p.x - q.x) < 0.02 && std::abs(p.y - q.y) < 0.02 && std::abs(p.z - q.z) < 0.02;
				});
			})) {
			m_selection << i;
		}
	}
	refresh();
	m_status->setText(tr("Brush updated: %1 → %2 vertices, %3 → %4 faces; %5 "
						 "vertices collapsed.")
						  .arg(report.verticesBefore)
						  .arg(report.verticesAfter)
						  .arg(report.facesBefore)
						  .arg(report.facesAfter)
						  .arg(report.collapsedVertices));
	return true;
}
} // namespace vibestudio
