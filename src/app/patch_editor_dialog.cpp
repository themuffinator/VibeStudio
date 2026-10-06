#include "app/patch_editor_dialog.h"

#include "app/model_viewport.h"
#include "core/map_geometry.h"
#include "core/map_preview_mesh.h"
#include "core/studio_settings.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vibestudio {

PatchControlView::PatchControlView(QWidget* parent) : QWidget(parent)
{
	static const bool accessibleInstalled = []() {
		QAccessible::installFactory([](const QString&, QObject* object) -> QAccessibleInterface* {
			if (auto* view = qobject_cast<PatchControlView*>(object)) {
				return new QAccessibleWidget(view, QAccessible::Graphic);
			}
			return nullptr;
		});
		return true;
	}();
	Q_UNUSED(accessibleInstalled);
	setObjectName(QStringLiteral("patchControlView"));
	setAccessibleName(tr("Patch control grid"));
	setFocusPolicy(Qt::StrongFocus);
	setToolTip(tr("Click a control point; Ctrl-click adds or removes points. Drag or use arrow keys to move the selection. Escape cancels "
	              "a drag. The point table provides the same selection and editing controls."));
}

void PatchControlView::setPatch(const LevelMapPatch& patch)
{
	m_patch = patch;
	m_dragging = false;
	m_previewDelta = {};
	setSelectedPoints(m_selected);
	if (m_scale <= 0) {
		framePatch();
	}
	update();
}

void PatchControlView::setSelectedPoints(const QVector<int>& selected)
{
	QVector<int> valid;
	for (int i : selected) {
		if (i >= 0 && i < m_patch.controlPoints.size() && !valid.contains(i)) {
			valid << i;
		}
	}
	m_selected = valid;
	setAccessibleDescription(
	    tr("%1 by %2 control grid, %3 points selected. Use the adjacent point table for row, column and coordinate details.")
	        .arg(m_patch.width)
	        .arg(m_patch.height)
	        .arg(m_selected.size()));
	QAccessibleEvent event(this, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&event);
	update();
}

QPointF PatchControlView::project(const LevelMapVec3& p) const
{
	if (m_plane == QStringLiteral("xz")) {
		return {p.x, p.z};
	}
	if (m_plane == QStringLiteral("yz")) {
		return {p.y, p.z};
	}
	return {p.x, p.y};
}

LevelMapVec3 PatchControlView::delta(const QPointF& p) const
{
	const double x = snapLevelMapCoordinate(p.x() / std::max(0.0001, m_scale), m_grid);
	const double y = snapLevelMapCoordinate(-p.y() / std::max(0.0001, m_scale), m_grid);
	if (m_plane == QStringLiteral("xz")) {
		return {x, 0, y, true};
	}
	if (m_plane == QStringLiteral("yz")) {
		return {0, x, y, true};
	}
	return {x, y, 0, true};
}

QPointF PatchControlView::toView(const QPointF& p) const
{
	return {width() * 0.5 + (p.x() - m_center.x()) * m_scale, height() * 0.5 - (p.y() - m_center.y()) * m_scale};
}

void PatchControlView::setPlane(const QString& plane)
{
	if (plane != QStringLiteral("xy") && plane != QStringLiteral("xz") && plane != QStringLiteral("yz")) {
		return;
	}
	m_plane = plane;
	m_dragging = false;
	m_previewDelta = {};
	framePatch();
}

void PatchControlView::framePatch()
{
	if (m_patch.controlPoints.isEmpty()) {
		return;
	}
	QPointF low = project(m_patch.controlPoints.first()), high = low;
	for (const auto& point : m_patch.controlPoints) {
		const auto p = project(point);
		low.setX(std::min(low.x(), p.x()));
		low.setY(std::min(low.y(), p.y()));
		high.setX(std::max(high.x(), p.x()));
		high.setY(std::max(high.y(), p.y()));
	}
	m_center = (low + high) * 0.5;
	m_scale = std::max(0.0001, std::min(std::max(64, width() - 80) / std::max(64.0, high.x() - low.x()),
	                                    std::max(64, height() - 80) / std::max(64.0, high.y() - low.y())));
	update();
}

QPointF PatchControlView::pointPosition(int index) const
{
	if (index < 0 || index >= m_patch.controlPoints.size()) {
		return {-1, -1};
	}
	auto p = m_patch.controlPoints.at(index);
	if (m_dragging && m_selected.contains(index)) {
		const auto d = delta(m_previewDelta);
		p.x += d.x;
		p.y += d.y;
		p.z += d.z;
	}
	return toView(project(p));
}

int PatchControlView::pointAt(const QPointF& position) const
{
	int best = -1;
	double distance = 12.0;
	for (int i = 0; i < m_patch.controlPoints.size(); ++i) {
		const auto d = pointPosition(i) - position;
		const double current = std::hypot(d.x(), d.y());
		if (current < distance) {
			distance = current;
			best = i;
		}
	}
	return best;
}

void PatchControlView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.fillRect(rect(), palette().brush(QPalette::Base));
	painter.setRenderHint(QPainter::Antialiasing);
	const QColor muted = palette().color(QPalette::Mid);
	if (m_grid > 0 && m_scale > 0) {
		double spacing = m_grid;
		while (spacing * m_scale < 16) {
			spacing *= 2;
		}
		painter.setPen(QPen(muted, 0.5));
		const double left = m_center.x() - width() * 0.5 / m_scale, bottom = m_center.y() - height() * 0.5 / m_scale;
		for (double x = std::floor(left / spacing) * spacing; x < left + width() / m_scale; x += spacing) {
			const auto v = toView({x, 0});
			painter.drawLine(QPointF(v.x(), 0), QPointF(v.x(), height()));
		}
		for (double y = std::floor(bottom / spacing) * spacing; y < bottom + height() / m_scale; y += spacing) {
			const auto v = toView({0, y});
			painter.drawLine(QPointF(0, v.y()), QPointF(width(), v.y()));
		}
	}
	LevelMapPatch shown = m_patch;
	// Control-grid interaction uses a bounded display mesh; the surface tab
	// shows the shared renderer's fixed subdivision result.
	shown.fixedSubdivisions = false;
	if (m_dragging) {
		moveLevelPatchPoints(&shown, m_selected, delta(m_previewDelta));
	}
	const auto mesh = tessellatePatchMesh(shown, 6);
	painter.setPen(QPen(palette().color(QPalette::Text), 1));
	for (int row = 0; row < mesh.size(); ++row) {
		for (int col = 0; col < mesh.at(row).size(); ++col) {
			const auto p = toView(project(mesh.at(row).at(col)));
			if (col > 0) {
				painter.drawLine(p, toView(project(mesh.at(row).at(col - 1))));
			}
			if (row > 0) {
				painter.drawLine(p, toView(project(mesh.at(row - 1).at(col))));
			}
		}
	}
	painter.setPen(QPen(muted, 1, Qt::DashLine));
	for (int row = 0; row < m_patch.height; ++row) {
		for (int col = 0; col < m_patch.width; ++col) {
			const int i = row * m_patch.width + col;
			if (col > 0) {
				painter.drawLine(pointPosition(i), pointPosition(i - 1));
			}
			if (row > 0) {
				painter.drawLine(pointPosition(i), pointPosition(i - m_patch.width));
			}
		}
	}
	for (int i = 0; i < m_patch.controlPoints.size(); ++i) {
		const bool selected = m_selected.contains(i);
		painter.setPen(QPen(palette().color(selected ? QPalette::HighlightedText : QPalette::Text), selected ? 2 : 1));
		painter.setBrush(palette().brush(selected ? QPalette::Highlight : QPalette::Base));
		const auto p = pointPosition(i);
		if (selected) {
			painter.drawRect(QRectF(p.x() - 5, p.y() - 5, 10, 10));
		} else {
			painter.drawEllipse(p, 3.5, 3.5);
		}
	}
	if (hasFocus()) {
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
		painter.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}

void PatchControlView::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	if (event->button() == Qt::MiddleButton) {
		m_panning = true;
		m_last = event->position();
		return;
	}
	if (event->button() != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	const int hit = pointAt(event->position());
	auto selected = m_selected;
	const bool toggle = event->modifiers().testFlag(Qt::ControlModifier);
	if (hit < 0) {
		if (!toggle) {
			selected.clear();
		}
	} else if (toggle) {
		if (selected.contains(hit)) {
			selected.removeAll(hit);
		} else {
			selected << hit;
		}
	} else if (!selected.contains(hit)) {
		selected = {hit};
	}
	setSelectedPoints(selected);
	Q_EMIT selectionChanged(m_selected);
	m_dragging = hit >= 0 && m_selected.contains(hit);
	m_press = event->position();
	m_previewDelta = {};
}

void PatchControlView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_panning) {
		const auto d = event->position() - m_last;
		m_last = event->position();
		m_center += QPointF(-d.x() / m_scale, d.y() / m_scale);
		update();
	} else if (m_dragging) {
		m_previewDelta = event->position() - m_press;
		update();
	}
}

void PatchControlView::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::MiddleButton) {
		m_panning = false;
	}
	if (event->button() != Qt::LeftButton || !m_dragging) {
		return;
	}
	const auto d = delta(event->position() - m_press);
	m_dragging = false;
	m_previewDelta = {};
	if (d.x != 0 || d.y != 0 || d.z != 0) {
		Q_EMIT moveRequested(m_selected, d);
	}
	update();
}

void PatchControlView::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Escape) {
		if (m_dragging) {
			m_dragging = false;
			m_previewDelta = {};
			update();
		} else {
			setSelectedPoints({});
			Q_EMIT selectionChanged(m_selected);
		}
		return;
	}
	if (event->matches(QKeySequence::SelectAll)) {
		QVector<int> points;
		for (int i = 0; i < m_patch.controlPoints.size(); ++i) {
			points << i;
		}
		setSelectedPoints(points);
		Q_EMIT selectionChanged(m_selected);
		return;
	}
	QPointF step;
	if (event->key() == Qt::Key_Left) {
		step.setX(-1);
	} else if (event->key() == Qt::Key_Right) {
		step.setX(1);
	} else if (event->key() == Qt::Key_Up) {
		step.setY(-1);
	} else if (event->key() == Qt::Key_Down) {
		step.setY(1);
	} else {
		QWidget::keyPressEvent(event);
		return;
	}
	if (!m_selected.isEmpty()) {
		Q_EMIT moveRequested(m_selected, delta(step * std::max(1.0, m_grid) * m_scale));
	}
}

void PatchControlView::wheelEvent(QWheelEvent* event)
{
	m_scale = std::clamp(m_scale * std::pow(1.2, event->angleDelta().y() / 120.0), 0.0001, 10000.0);
	update();
	event->accept();
}

PatchEditorDialog::PatchEditorDialog(const LevelMapPatch& patch, bool creating, double grid, QWidget* parent)
    : QDialog(parent), m_patch(patch)
{
	setObjectName(QStringLiteral("patchEditorDialog"));
	setWindowTitle(creating ? tr("Add Patch") : tr("Edit Patch %1").arg(patch.id));
	setAccessibleName(windowTitle());
	resize(1180, 800);
	auto* layout = new QVBoxLayout(this);
	auto* tools = new QToolBar(tr("Patch editing actions"), this);
	tools->setAccessibleName(tr("Patch editing actions"));
	tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
	const auto action = [this, tools](const QString& label, const QString& name, auto callback) {
		auto* a = tools->addAction(label);
		a->setObjectName(name);
		connect(a, &QAction::triggered, this, callback);
		return a;
	};
	m_undoAction = action(tr("Undo"), QStringLiteral("patchUndo"), [this]() { undo(); });
	m_redoAction = action(tr("Redo"), QStringLiteral("patchRedo"), [this]() { redo(); });
	m_undoAction->setShortcut(QKeySequence::Undo);
	m_redoAction->setShortcut(QKeySequence::Redo);
	for (auto* a : {m_undoAction, m_redoAction}) {
		a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
		addAction(a);
	}
	action(tr("Invert Facing"), QStringLiteral("patchInvert"), [this]() {
		auto p = m_patch;
		QString error;
		if (invertLevelPatch(&p, &error)) {
			applyPatch(p);
		} else {
			setError(error);
		}
	});
	action(tr("Split Columns"), QStringLiteral("patchSplitColumns"), [this]() {
		auto p = m_patch;
		QString error;
		if (subdivideLevelPatch(&p, true, &error)) {
			applyPatch(p);
		} else {
			setError(error);
		}
	});
	action(tr("Split Rows"), QStringLiteral("patchSplitRows"), [this]() {
		auto p = m_patch;
		QString error;
		if (subdivideLevelPatch(&p, false, &error)) {
			applyPatch(p);
		} else {
			setError(error);
		}
	});
	action(tr("Reset UV"), QStringLiteral("patchResetUv"), [this]() {
		auto p = m_patch;
		for (int row = 0; row < p.height; ++row) {
			for (int col = 0; col < p.width; ++col) {
				const int i = row * p.width + col;
				p.controlU[i] = static_cast<double>(col) / (p.width - 1);
				p.controlV[i] = static_cast<double>(row) / (p.height - 1);
			}
		}
		applyPatch(p);
	});
	layout->addWidget(tools);
	auto* split = new QSplitter(this);
	split->setChildrenCollapsible(false);
	auto* left = new QWidget(split);
	auto* leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	auto* viewTools = new QHBoxLayout;
	auto* plane = new QComboBox(left);
	plane->setObjectName(QStringLiteral("patchViewPlane"));
	plane->setAccessibleName(tr("Control grid projection"));
	plane->addItem(tr("Top XY"), QStringLiteral("xy"));
	plane->addItem(tr("Front XZ"), QStringLiteral("xz"));
	plane->addItem(tr("Side YZ"), QStringLiteral("yz"));
	viewTools->addWidget(plane);
	auto* snap = new QDoubleSpinBox(left);
	snap->setObjectName(QStringLiteral("patchGrid"));
	snap->setAccessibleName(tr("Control point grid size"));
	snap->setRange(0, 1024);
	snap->setDecimals(2);
	snap->setValue(grid);
	snap->setSpecialValueText(tr("No snap"));
	snap->setPrefix(tr("Grid "));
	viewTools->addWidget(snap);
	auto* frame = new QPushButton(tr("Frame"), left);
	frame->setAccessibleName(tr("Frame patch in both views"));
	viewTools->addWidget(frame);
	leftLayout->addLayout(viewTools);
	auto* views = new QTabWidget(left);
	views->setAccessibleName(tr("Patch views"));
	m_controls = new PatchControlView(views);
	m_controls->setGrid(grid);
	m_preview = new ModelViewport(views);
	m_preview->setObjectName(QStringLiteral("patchSurfacePreview"));
	m_preview->setAccessibleName(tr("Patch surface preview with UV checker"));
	m_preview->setBackfaceCulling(false);
	const auto preferences = StudioSettings().accessibilityPreferences();
	m_preview->setHighContrast(preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight);
	m_preview->setReducedMotion(preferences.reducedMotion);
	m_preview->setRenderMode(ModelViewportRenderMode::Textured);
	m_preview->setShowEdges(true);
	QImage checker(128, 128, QImage::Format_RGB32);
	for (int y = 0; y < 128; ++y) {
		for (int x = 0; x < 128; ++x) {
			checker.setPixelColor(x, y, ((x / 16 + y / 16) % 2) ? QColor(64, 88, 110) : QColor(220, 226, 230));
		}
	}
	m_preview->setSkin(checker);
	views->addTab(m_controls, tr("Control Grid"));
	views->addTab(m_preview, tr("Surface / UV Checker"));
	connect(views, &QTabWidget::currentChanged, this, [this, framed = false](int index) mutable {
		if (index == 1 && !framed) {
			m_preview->frameModel();
			framed = true;
		}
	});
	leftLayout->addWidget(views, 1);
	connect(plane, &QComboBox::currentIndexChanged, this, [this, plane]() { m_controls->setPlane(plane->currentData().toString()); });
	if (patch.mins.y == patch.maxs.y) {
		plane->setCurrentIndex(1);
	} else if (patch.mins.x == patch.maxs.x) {
		plane->setCurrentIndex(2);
	}
	connect(snap, &QDoubleSpinBox::valueChanged, m_controls, &PatchControlView::setGrid);
	connect(frame, &QPushButton::clicked, this, [this]() {
		m_controls->framePatch();
		m_preview->frameModel();
	});
	connect(m_controls, &PatchControlView::selectionChanged, this, &PatchEditorDialog::syncSelection);
	connect(m_controls, &PatchControlView::moveRequested, this, [this](const QVector<int>&, const LevelMapVec3& d) { moveSelected(d); });
	auto* scroll = new QScrollArea(split);
	scroll->setWidgetResizable(true);
	scroll->setAccessibleName(tr("Patch properties"));
	auto* properties = new QWidget(scroll);
	auto* fields = new QVBoxLayout(properties);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_texture = new QComboBox(properties);
	m_texture->setEditable(true);
	m_texture->setObjectName(QStringLiteral("patchTexture"));
	m_texture->setAccessibleName(tr("Patch shader or texture"));
	m_texture->addItem(patch.textureName, patch.textureName);
	form->addRow(tr("Material"), m_texture);
	connect(m_texture->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
		if (!m_refreshing) {
			auto p = m_patch;
			p.textureName = materialToken();
			applyPatch(p);
		}
	});
	fields->addLayout(form);
	if (creating) {
		auto* preset = new QFormLayout;
		preset->setRowWrapPolicy(QFormLayout::WrapLongRows);
		auto* shape = new QComboBox(properties);
		shape->setObjectName(QStringLiteral("patchShape"));
		shape->setAccessibleName(tr("Patch shape"));
		shape->addItem(tr("Plane"), QStringLiteral("plane"));
		shape->addItem(tr("Cylinder (open ends)"), QStringLiteral("cylinder"));
		shape->addItem(tr("Cone (open base)"), QStringLiteral("cone"));
		preset->addRow(tr("Shape"), shape);
		auto* surfacePlane = new QComboBox(properties);
		surfacePlane->setObjectName(QStringLiteral("patchPresetPlane"));
		surfacePlane->setAccessibleName(tr("Plane orientation"));
		for (const auto& axis : {QStringLiteral("xy"), QStringLiteral("xz"), QStringLiteral("yz")}) {
			surfacePlane->addItem(axis.toUpper(), axis);
		}
		if (patch.mins.y == patch.maxs.y) {
			surfacePlane->setCurrentIndex(1);
		} else if (patch.mins.x == patch.maxs.x) {
			surfacePlane->setCurrentIndex(2);
		}
		preset->addRow(tr("Orientation"), surfacePlane);
		auto* columns = new QSpinBox(properties);
		auto* rows = new QSpinBox(properties);
		columns->setObjectName(QStringLiteral("patchColumns"));
		rows->setObjectName(QStringLiteral("patchRows"));
		for (auto* n : {columns, rows}) {
			n->setRange(3, 31);
			n->setSingleStep(2);
			n->setValue(3);
		}
		columns->setValue(patch.width);
		rows->setValue(patch.height);
		columns->setAccessibleName(tr("Control columns"));
		rows->setAccessibleName(tr("Control rows"));
		preset->addRow(tr("Columns"), columns);
		preset->addRow(tr("Rows"), rows);
		QVector<QDoubleSpinBox*> sizes;
		for (const auto& axis : {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")}) {
			auto* size = new QDoubleSpinBox(properties);
			size->setRange(1, 65536);
			size->setValue(128);
			size->setObjectName(QStringLiteral("patchSize%1").arg(axis));
			size->setAccessibleName(tr("Size %1").arg(axis));
			preset->addRow(tr("Size %1").arg(axis), size);
			sizes << size;
		}
		connect(shape, &QComboBox::currentIndexChanged, this, [shape, columns, surfacePlane]() {
			const bool flat = shape->currentData() == QStringLiteral("plane");
			columns->setEnabled(flat);
			if (!flat) {
				columns->setValue(9);
			}
			surfacePlane->setEnabled(flat);
		});
		auto* build = new QPushButton(tr("Generate Shape"), properties);
		build->setObjectName(QStringLiteral("patchGenerate"));
		connect(build, &QPushButton::clicked, this, [this, shape, surfacePlane, columns, rows, sizes, patch]() {
			LevelPatchCreateRequest request;
			request.shape = shape->currentData().toString();
			request.plane = surfacePlane->currentData().toString();
			request.columns = columns->value();
			request.rows = rows->value();
			request.center = {(patch.mins.x + patch.maxs.x) * 0.5, (patch.mins.y + patch.maxs.y) * 0.5, (patch.mins.z + patch.maxs.z) * 0.5,
			                  true};
			request.size = {sizes.at(0)->value(), sizes.at(1)->value(), sizes.at(2)->value(), true};
			// The factory accepts asset paths; the current draft stores a map token.
			request.texture = QStringLiteral("textures/") + materialToken();
			LevelMapPatch generated;
			QString error;
			if (createLevelPatch(request, &generated, &error)) {
				applyPatch(generated);
				m_controls->framePatch();
				m_preview->frameModel();
			} else {
				setError(error);
			}
		});
		fields->addLayout(preset);
		fields->addWidget(build);
	}
	m_points = new QTableWidget(properties);
	m_points->setObjectName(QStringLiteral("patchPoints"));
	m_points->setAccessibleName(tr("Patch control point coordinates and UVs"));
	// Map coordinate axes and signed numbers retain their technical order in
	// RTL layouts; labels and the surrounding form remain localized.
	m_points->setLayoutDirection(Qt::LeftToRight);
	m_points->setAccessibleDescription(
	    tr("Select rows to choose control points. Edit X, Y, Z, U or V cells to change one point. Values use the current locale."));
	m_points->setColumnCount(5);
	m_points->setHorizontalHeaderLabels({tr("X"), tr("Y"), tr("Z"), tr("U"), tr("V")});
	m_points->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_points->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_points->horizontalHeader()->setMinimumSectionSize(fontMetrics().horizontalAdvance(QStringLiteral("-1024.00")) + 16);
	m_points->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_points->setMinimumHeight(220);
	fields->addWidget(m_points, 1);
	connect(m_points, &QTableWidget::itemSelectionChanged, this, [this]() {
		if (m_refreshing) {
			return;
		}
		QVector<int> selected;
		for (const auto& row : m_points->selectionModel()->selectedRows()) {
			selected << row.row();
		}
		m_controls->setSelectedPoints(selected);
	});
	connect(m_points, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* cell) {
		if (m_refreshing || !cell) {
			return;
		}
		bool ok = false;
		const double value = locale().toDouble(cell->text(), &ok);
		if (!ok || !std::isfinite(value)) {
			refresh();
			setError(tr("Enter a finite coordinate."));
			return;
		}
		auto p = m_patch;
		const int i = cell->row();
		if (i < 0 || i >= p.controlPoints.size()) {
			return;
		}
		switch (cell->column()) {
		case 0:
			p.controlPoints[i].x = value;
			break;
		case 1:
			p.controlPoints[i].y = value;
			break;
		case 2:
			p.controlPoints[i].z = value;
			break;
		case 3:
			p.controlU[i] = value;
			break;
		case 4:
			p.controlV[i] = value;
			break;
		}
		QString error;
		if (!applyPatch(p, &error)) {
			refresh();
			setError(error);
		}
	});
	auto* moveForm = new QFormLayout;
	moveForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	QVector<QDoubleSpinBox*> amounts;
	for (const auto& axis : {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")}) {
		auto* amount = new QDoubleSpinBox(properties);
		amount->setRange(-1048576, 1048576);
		amount->setDecimals(4);
		amount->setObjectName(QStringLiteral("patchMove%1").arg(axis));
		amount->setAccessibleName(tr("Move selected points along %1").arg(axis));
		moveForm->addRow(tr("Move %1").arg(axis), amount);
		amounts << amount;
	}
	fields->addLayout(moveForm);
	auto* move = new QPushButton(tr("Move Selected"), properties);
	move->setObjectName(QStringLiteral("patchMoveSelected"));
	connect(move, &QPushButton::clicked, this,
	        [this, amounts]() { moveSelected({amounts.at(0)->value(), amounts.at(1)->value(), amounts.at(2)->value(), true}); });
	fields->addWidget(move);
	auto* snapSelected = new QPushButton(tr("Snap Selected to Grid"), properties);
	snapSelected->setObjectName(QStringLiteral("patchSnapSelected"));
	connect(snapSelected, &QPushButton::clicked, this, [this, snap]() { moveSelected({0, 0, 0, true}, snap->value()); });
	fields->addWidget(snapSelected);
	scroll->setWidget(properties);
	split->addWidget(left);
	split->addWidget(scroll);
	split->setSizes({650, 480});
	layout->addWidget(split, 1);
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("patchStatus"));
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Patch edit status"));
	layout->addWidget(m_status);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(creating ? tr("Add Patch") : tr("Apply Patch"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("patchApply"));
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		auto p = m_patch;
		p.textureName = materialToken();
		if (applyPatch(p)) {
			accept();
		}
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	refresh();
}

QString PatchEditorDialog::materialToken() const
{
	const QString material = m_texture->currentText().trimmed();
	if (material == m_patch.textureName) {
		return material;
	}
	const int index = m_texture->currentIndex();
	if (index >= 0 && m_texture->itemText(index) == material && m_texture->itemData(index).isValid()) {
		return m_texture->itemData(index).toString();
	}
	return levelPatchMaterialToken(material, m_patch.fixedSubdivisions);
}

void PatchEditorDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	if (m_firstShow) {
		m_firstShow = false;
		m_controls->framePatch();
	}
}

void PatchEditorDialog::setError(const QString& message)
{
	m_status->setText(tr("Cannot apply patch: %1").arg(message));
	QAccessibleEvent event(m_status, QAccessible::NameChanged);
	QAccessible::updateAccessibility(&event);
}

bool PatchEditorDialog::applyPatch(const LevelMapPatch& patch, QString* error)
{
	QString why;
	if (!validateLevelPatch(patch, &why)) {
		if (error) {
			*error = why;
		}
		setError(why);
		return false;
	}
	if (error) {
		error->clear();
	}
	if (levelPatchDefinition(patch) == levelPatchDefinition(m_patch)) {
		return true;
	}
	m_undo << m_patch;
	if (m_undo.size() > 200) {
		m_undo.removeFirst();
	}
	m_redo.clear();
	if (m_patch.width != patch.width || m_patch.height != patch.height) {
		m_controls->setSelectedPoints({});
	}
	m_patch = patch;
	m_patch.definitionDirty = m_patch.geometryDirty = true;
	refreshLevelPatchBounds(&m_patch);
	refresh();
	return true;
}

void PatchEditorDialog::selectPoints(const QVector<int>& points)
{
	m_controls->setSelectedPoints(points);
	syncSelection(m_controls->selectedPoints());
}

void PatchEditorDialog::syncSelection(const QVector<int>& points)
{
	const bool was = m_refreshing;
	m_refreshing = true;
	m_points->clearSelection();
	for (int i : points) {
		if (i >= 0 && i < m_points->rowCount()) {
			m_points->selectionModel()->select(m_points->model()->index(i, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
		}
	}
	m_refreshing = was;
}

bool PatchEditorDialog::moveSelected(const LevelMapVec3& delta, double grid)
{
	auto p = m_patch;
	QString error;
	if (!moveLevelPatchPoints(&p, m_controls->selectedPoints(), delta, grid, &error)) {
		setError(error);
		return false;
	}
	return applyPatch(p);
}

void PatchEditorDialog::undo()
{
	if (m_undo.isEmpty()) {
		return;
	}
	m_redo << m_patch;
	m_patch = m_undo.takeLast();
	refresh();
}
void PatchEditorDialog::redo()
{
	if (m_redo.isEmpty()) {
		return;
	}
	m_undo << m_patch;
	m_patch = m_redo.takeLast();
	refresh();
}

void PatchEditorDialog::refresh()
{
	m_refreshing = true;
	const auto selected = m_controls->selectedPoints();
	m_texture->setCurrentText(m_patch.textureName);
	m_points->setRowCount(static_cast<int>(m_patch.controlPoints.size()));
	for (int i = 0; i < m_patch.controlPoints.size(); ++i) {
		const auto p = m_patch.controlPoints.at(i);
		const double values[5]{p.x, p.y, p.z, m_patch.controlU.value(i), m_patch.controlV.value(i)};
		m_points->setVerticalHeaderItem(i, new QTableWidgetItem(tr("%1, %2").arg(i / m_patch.width).arg(i % m_patch.width)));
		for (int c = 0; c < 5; ++c) {
			auto* item = new QTableWidgetItem(locale().toString(values[c], 'g', 15));
			item->setToolTip(tr("Row %1, column %2").arg(i / m_patch.width).arg(i % m_patch.width));
			m_points->setItem(i, c, item);
		}
	}
	m_points->resizeRowsToContents();
	m_controls->setPatch(m_patch);
	selectPoints(selected);
	LevelMapDocument document;
	document.format = LevelMapFormat::Quake3Map;
	document.patches = {m_patch};
	const auto preview = buildLevelMapPreviewMesh(document);
	m_preview->setMesh(preview.mesh, m_preview->hasMesh());
	m_status->setText(tr("%1 × %2 control points · %3 preview triangles").arg(m_patch.width).arg(m_patch.height).arg(preview.triangles));
	if (preview.truncated) {
		m_status->setText(tr("Preview limit reached: showing %1 triangles. The full control grid is retained.").arg(preview.triangles));
	}
	m_undoAction->setEnabled(!m_undo.isEmpty());
	m_redoAction->setEnabled(!m_redo.isEmpty());
	m_refreshing = false;
}
} // namespace vibestudio
