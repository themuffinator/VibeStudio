// The UV Editor's texture-space view. Workflow reference: NetRadiant
// Custom's TexTool plugin (plugins/textool/TexTool.cpp,
// ControlPointsManager.cpp and 2DView.cpp at 449778b3, GPL-2.0-or-later):
// points drawn over the tiled texture, a dragged face point that becomes the
// anchor for turns, a right-drag pan, Insert and Delete zoom and a
// right-click menu. Original Qt implementation; see docs/CREDITS.md.

#include "app/level_uv_view.h"
#include "app/locale_number.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSet>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace vibestudio {
namespace {

constexpr double kMinZoom = 1e-4, kMaxZoom = 1e4;
// Arrow keys with Shift nudge this many snap steps.
constexpr double kCoarseNudge = 8;
// Ctrl+arrows pan this many pixels.
constexpr double kKeyPan = 32;
// At most this many repeat or grid lines each way.
constexpr int kLineLimit = 400;

bool finitePoint(QPointF p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}

bool sameEdit(const LevelUvEdit& a, const LevelUvEdit& b)
{
	const LevelUvTransform& x = a.transform;
	const LevelUvTransform& y = b.transform;
	return x.xx == y.xx && x.xy == y.xy && x.yx == y.yx && x.yy == y.yy && x.dx == y.dx && x.dy == y.dy && a.patchUv == b.patchUv && a.anchor == b.anchor;
}

// The keys the view handles itself, so window shortcuts do not take them.
bool viewKey(const QKeyEvent* event, bool dragging)
{
	if (dragging) {
		return true;
	}
	const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
	const bool plain = modifiers == Qt::NoModifier, shift = modifiers == Qt::ShiftModifier, control = modifiers == Qt::ControlModifier;
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down:
		return plain || shift || control;
	case Qt::Key_BracketLeft:
	case Qt::Key_BracketRight:
	case Qt::Key_BraceLeft:
	case Qt::Key_BraceRight:
	case Qt::Key_Plus:
	case Qt::Key_Equal:
	case Qt::Key_Minus:
	case Qt::Key_Underscore:
		return plain || shift;
	case Qt::Key_Insert:
	case Qt::Key_Delete:
	case Qt::Key_Home:
	case Qt::Key_F:
		return plain;
	case Qt::Key_N:
		return plain || shift;
	case Qt::Key_A:
		return plain || control;
	case Qt::Key_Z:
		return control || modifiers == (Qt::ControlModifier | Qt::ShiftModifier);
	case Qt::Key_Y:
		return control;
	default:
		return false;
	}
}

} // namespace

LevelUvView::LevelUvView(QWidget* parent) : QWidget(parent)
{
	static const bool accessibleInstalled = [] {
		QAccessible::installFactory([](const QString&, QObject* object) -> QAccessibleInterface* {
			if (auto* view = qobject_cast<LevelUvView*>(object)) {
				return new QAccessibleWidget(view, QAccessible::Graphic);
			}
			return nullptr;
		});
		return true;
	}();
	Q_UNUSED(accessibleInstalled);
	setObjectName(QStringLiteral("levelUvView"));
	setFocusPolicy(Qt::StrongFocus);
	// Texture space reads the same way in every layout direction.
	setLayoutDirection(Qt::LeftToRight);
	setAccessibleName(tr("UV Editor texture space"));
	setToolTip(tr("Drag points over the texture. A face's texture follows the dragged point and turns about the anchor, the point dragged "
				  "last; a patch's points move on their own. Arrow keys nudge, [ and ] turn, the wheel zooms, a right-drag pans and a "
				  "right-click opens a menu."));
	m_checker = QImage(2, 2, QImage::Format_RGB32);
	m_checker.setPixelColor(0, 0, QColor(92, 98, 106));
	m_checker.setPixelColor(1, 1, QColor(92, 98, 106));
	m_checker.setPixelColor(1, 0, QColor(150, 156, 164));
	m_checker.setPixelColor(0, 1, QColor(150, 156, 164));
	buildMenu();
	announce();
}

LevelUvView::~LevelUvView() = default;

void LevelUvView::buildMenu()
{
	m_menu = new QMenu(this);
	m_menu->setObjectName(QStringLiteral("levelUvViewMenu"));
	const auto add = [this](const QString& name, const QString& text, const std::function<void()>& run) {
		QAction* action = m_menu->addAction(text);
		action->setObjectName(name);
		connect(action, &QAction::triggered, this, run);
		return action;
	};
	add(QStringLiteral("levelUvUndo"), tr("Undo"), [this] { undo(); });
	add(QStringLiteral("levelUvRedo"), tr("Redo"), [this] { redo(); });
	add(QStringLiteral("levelUvReset"), tr("Reset"), [this] { resetEdit(); });
	m_menu->addSeparator();
	add(QStringLiteral("levelUvAnchor"), tr("Set Anchor at Current Point"), [this] { setAnchor(m_anchor == m_current ? -1 : m_current); });
	add(QStringLiteral("levelUvTurnAnticlockwise"), tr("Turn Anticlockwise"), [this] { rotate(-m_angleStep); });
	add(QStringLiteral("levelUvTurnClockwise"), tr("Turn Clockwise"), [this] { rotate(m_angleStep); });
	m_menu->addSeparator();
	add(QStringLiteral("levelUvFrame"), tr("Frame All"), [this] { frameAll(); });
	add(QStringLiteral("levelUvZoomIn"), tr("Zoom In"), [this] { zoomBy(2); });
	add(QStringLiteral("levelUvZoomOut"), tr("Zoom Out"), [this] { zoomBy(0.5); });
	connect(m_menu, &QMenu::aboutToShow, this, [this] {
		const auto find = [this](const char* name) { return m_menu->findChild<QAction*>(QString::fromLatin1(name)); };
		find("levelUvUndo")->setEnabled(canUndo());
		find("levelUvRedo")->setEnabled(canRedo());
		find("levelUvReset")->setEnabled(m_hasSource && !sameEdit(m_edit, LevelUvEdit {}));
		QAction* anchor = find("levelUvAnchor");
		anchor->setEnabled(m_hasSource && (m_current >= 0 || m_anchor >= 0));
		anchor->setText(m_anchor >= 0 && m_anchor == m_current ? tr("Clear Anchor") : tr("Set Anchor at Current Point"));
	});
}

void LevelUvView::setSource(const LevelUvLayout& layout, const QImage& image)
{
	m_drag = Drag::None;
	m_layout = layout;
	m_hasSource = true;
	m_image = image;
	m_edit = {};
	m_edit.allowValve220 = m_allowValve220;
	m_result = {};
	m_result.face = layout.face;
	m_result.patch = layout.patch;
	m_result.uv = layout.uv;
	m_error.clear();
	m_undo.clear();
	m_redo.clear();
	m_anchor = -1;
	m_current = layout.uv.isEmpty() ? -1 : 0;
	m_selected.clear();
	m_userCamera = false;
	frameAll();
	announce();
	update();
	Q_EMIT editChanged();
	Q_EMIT stateChanged();
}

void LevelUvView::setImage(const QImage& image)
{
	m_image = image;
	update();
}

QSizeF LevelUvView::tile() const
{
	// Classic and Valve 220 faces without a known image show 64-texel tiles.
	return m_layout.imageSize.width() > 0 && m_layout.imageSize.height() > 0 ? QSizeF(m_layout.imageSize) : QSizeF(64, 64);
}

void LevelUvView::setAllowValve220(bool allow)
{
	if (allow == m_allowValve220) {
		return;
	}
	m_allowValve220 = allow;
	if (m_hasSource) {
		setWorkingEdit(m_edit);
	}
}

void LevelUvView::setSnapStep(double texels)
{
	m_snap = std::isfinite(texels) && texels > 0 ? texels : 0;
	update();
}

void LevelUvView::setAngleStep(double degrees)
{
	if (std::isfinite(degrees) && degrees > 0 && degrees <= 360) {
		m_angleStep = degrees;
	}
}

bool LevelUvView::setAnchor(int point)
{
	if (!m_hasSource || point < -1 || point >= m_result.uv.size()) {
		return false;
	}
	m_anchor = point;
	if (point >= 0) {
		m_current = point;
	}
	announce();
	update();
	Q_EMIT stateChanged();
	return true;
}

void LevelUvView::setCurrentPoint(int point)
{
	if (!m_hasSource || point < 0 || point >= m_result.uv.size() || point == m_current) {
		return;
	}
	m_current = point;
	announce();
	update();
	Q_EMIT stateChanged();
}

void LevelUvView::setSelectedPoints(const QVector<int>& points)
{
	QVector<int> valid;
	for (const int point : points) {
		if (point >= 0 && point < m_result.uv.size() && !valid.contains(point)) {
			valid.append(point);
		}
	}
	if (!m_layout.isPatch() || valid == m_selected) {
		return;
	}
	m_selected = valid;
	if (!valid.isEmpty() && !valid.contains(m_current)) {
		m_current = valid.first();
	}
	announce();
	update();
	Q_EMIT stateChanged();
}

bool LevelUvView::setWorkingEdit(const LevelUvEdit& edit)
{
	LevelUvEdit candidate = edit;
	candidate.allowValve220 = m_allowValve220;
	LevelUvResult result;
	QString error;
	if (!applyLevelUvEdit(m_layout, candidate, &result, &error)) {
		m_error = error;
		announce();
		update();
		Q_EMIT editChanged();
		return false;
	}
	m_error.clear();
	m_edit = candidate;
	m_result = std::move(result);
	announce();
	update();
	Q_EMIT editChanged();
	return true;
}

void LevelUvView::record(const LevelUvEdit& before)
{
	m_undo.append(before);
	if (m_undo.size() > 256) {
		m_undo.removeFirst();
	}
	m_redo.clear();
	Q_EMIT stateChanged();
}

QVector<int> LevelUvView::turningPoints() const
{
	if (m_layout.isPatch() && !m_selected.isEmpty()) {
		return m_selected;
	}
	QVector<int> all;
	for (int i = 0; i < m_result.uv.size(); ++i) {
		all.append(i);
	}
	return all;
}

QPointF LevelUvView::pivotFor(const QVector<int>& points) const
{
	if (m_anchor >= 0 && m_anchor < m_result.uv.size()) {
		return m_result.uv[m_anchor];
	}
	QPointF sum;
	for (const int point : points) {
		sum += m_result.uv.value(point);
	}
	return points.isEmpty() ? sum : sum / static_cast<double>(points.size());
}

bool LevelUvView::nudge(QPointF texels)
{
	if (!m_hasSource || dragging() || !finitePoint(texels) || (texels.x() == 0 && texels.y() == 0)) {
		return false;
	}
	const LevelUvEdit before = m_edit;
	LevelUvEdit next = m_edit;
	const LevelUvTransform move = LevelUvTransform::translation(texels);
	if (m_layout.isPatch()) {
		next.patchUv = levelUvPatchTransformed(m_result.uv, m_selected, move);
	} else {
		next.transform = m_edit.transform.then(move);
	}
	if (!setWorkingEdit(next)) {
		return false;
	}
	record(before);
	return true;
}

bool LevelUvView::rotate(double clockwiseDegrees)
{
	if (!m_hasSource || dragging() || !std::isfinite(clockwiseDegrees) || clockwiseDegrees == 0) {
		return false;
	}
	const LevelUvEdit before = m_edit;
	LevelUvEdit next = m_edit;
	const QVector<int> points = turningPoints();
	const LevelUvTransform turn = LevelUvTransform::rotation(pivotFor(points), clockwiseDegrees);
	if (m_layout.isPatch()) {
		next.patchUv = levelUvPatchTransformed(m_result.uv, m_selected, turn);
	} else {
		next.transform = m_edit.transform.then(turn);
		next.anchor = m_anchor;
	}
	if (!setWorkingEdit(next)) {
		return false;
	}
	record(before);
	return true;
}

bool LevelUvView::movePointTo(int point, QPointF texel)
{
	if (!m_hasSource || dragging() || point < 0 || point >= m_result.uv.size() || !finitePoint(texel) || m_result.uv[point] == texel) {
		return false;
	}
	const LevelUvEdit before = m_edit;
	LevelUvEdit next = m_edit;
	if (m_layout.isPatch()) {
		next.patchUv = m_result.uv;
		next.patchUv[point] = texel;
	} else {
		next.transform = m_edit.transform.then(LevelUvTransform::translation(texel - m_result.uv[point]));
	}
	if (!setWorkingEdit(next)) {
		return false;
	}
	m_current = point;
	record(before);
	return true;
}

bool LevelUvView::resetEdit()
{
	if (!m_hasSource || dragging() || sameEdit(m_edit, LevelUvEdit {})) {
		return false;
	}
	const LevelUvEdit before = m_edit;
	if (!setWorkingEdit({})) {
		return false;
	}
	record(before);
	return true;
}

bool LevelUvView::undo()
{
	if (m_undo.isEmpty() || dragging()) {
		return false;
	}
	const LevelUvEdit previous = m_undo.takeLast();
	const LevelUvEdit current = m_edit;
	if (!setWorkingEdit(previous)) {
		m_undo.append(previous);
		return false;
	}
	m_redo.append(current);
	Q_EMIT stateChanged();
	return true;
}

bool LevelUvView::redo()
{
	if (m_redo.isEmpty() || dragging()) {
		return false;
	}
	const LevelUvEdit next = m_redo.takeLast();
	const LevelUvEdit current = m_edit;
	if (!setWorkingEdit(next)) {
		m_redo.append(next);
		return false;
	}
	m_undo.append(current);
	Q_EMIT stateChanged();
	return true;
}

QRectF LevelUvView::contentBounds() const
{
	// QRectF::united() skips empty rectangles, so gather the extremes.
	double left = std::numeric_limits<double>::infinity(), top = left, right = -left, bottom = -left;
	for (const QVector<QPointF>* list : {&m_layout.uv, &m_result.uv}) {
		for (const QPointF& point : *list) {
			if (finitePoint(point)) {
				left = std::min(left, point.x());
				right = std::max(right, point.x());
				top = std::min(top, point.y());
				bottom = std::max(bottom, point.y());
			}
		}
	}
	return left <= right ? QRectF(QPointF(left, top), QPointF(right, bottom)) : QRectF();
}

void LevelUvView::frameAll()
{
	if (!m_hasSource) {
		return;
	}
	QRectF bounds = contentBounds();
	// At least a few texels, and a fifth more than the points, as
	// TexTool's view frames them.
	const QPointF centre = bounds.center();
	const double width = std::max(bounds.width(), 8.0) * 1.2, height = std::max(bounds.height(), 8.0) * 1.2;
	bounds = QRectF(centre.x() - width / 2, centre.y() - height / 2, width, height);
	m_centre = centre;
	// Framed again on every resize until the user zooms or pans.
	m_userCamera = false;
	if (this->width() < 8 || this->height() < 8) {
		return;
	}
	m_zoom = std::clamp(std::min(this->width() / bounds.width(), this->height() / bounds.height()), kMinZoom, kMaxZoom);
	update();
}

void LevelUvView::zoomBy(double factor, QPointF screenPoint)
{
	if (!std::isfinite(factor) || factor <= 0) {
		return;
	}
	if (!finitePoint(screenPoint)) {
		screenPoint = QRectF(rect()).center();
	}
	const QPointF anchored = screenToTexel(screenPoint);
	m_zoom = std::clamp(m_zoom * factor, kMinZoom, kMaxZoom);
	m_centre = anchored - (screenPoint - QRectF(rect()).center()) / m_zoom;
	m_userCamera = true;
	update();
}

void LevelUvView::zoomBy(double factor)
{
	zoomBy(factor, QPointF(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()));
}

void LevelUvView::panBy(QPointF screenDelta)
{
	if (!finitePoint(screenDelta)) {
		return;
	}
	m_centre -= screenDelta / m_zoom;
	m_userCamera = true;
	update();
}

QPointF LevelUvView::texelToScreen(QPointF texel) const
{
	return (texel - m_centre) * m_zoom + QRectF(rect()).center();
}

QPointF LevelUvView::screenToTexel(QPointF screen) const
{
	return (screen - QRectF(rect()).center()) / m_zoom + m_centre;
}

int LevelUvView::pointAt(QPointF screen, double tolerance) const
{
	int found = -1;
	double best = tolerance * tolerance;
	for (int i = 0; i < m_result.uv.size(); ++i) {
		const QPointF delta = texelToScreen(m_result.uv[i]) - screen;
		const double distance = QPointF::dotProduct(delta, delta);
		if (distance < best || (distance == best && i == m_current)) {
			best = distance;
			found = i;
		}
	}
	return found;
}

QString LevelUvView::summary() const
{
	if (!m_hasSource) {
		return tr("No face or patch is open in the UV Editor.");
	}
	const int count = static_cast<int>(m_result.uv.size());
	QString text = m_layout.isPatch()
		? tr("Patch %1, %2 by %3 control points, over %4.").arg(m_layout.target.objectId).arg(m_layout.columns).arg(m_layout.rows).arg(m_layout.material)
		: tr("Brush %1, face %2: %n point(s) over %3.", nullptr, count).arg(m_layout.target.objectId).arg(m_layout.target.faceIndex + 1).arg(m_layout.material);
	if (m_current >= 0 && m_current < count) {
		const QPointF at = m_result.uv[m_current];
		text += QLatin1Char(' ') + tr("Point %1 at %2, %3 texels.").arg(m_current + 1).arg(localeNumber(at.x(), 7), localeNumber(at.y(), 7));
	}
	if (m_anchor >= 0) {
		text += QLatin1Char(' ') + tr("Anchor: point %1.").arg(m_anchor + 1);
	}
	if (m_layout.isPatch()) {
		const int selected = static_cast<int>(m_selected.size());
		text += QLatin1Char(' ') + tr("%n point(s) selected.", nullptr, selected);
	}
	if (!m_error.isEmpty()) {
		return text + QLatin1Char(' ') + m_error;
	}
	switch (m_result.fit) {
	case LevelUvFit::Unchanged:
		text += QLatin1Char(' ') + tr("No change yet.");
		break;
	case LevelUvFit::Exact:
		text += QLatin1Char(' ') + tr("Changed; Apply makes it one undo step.");
		break;
	case LevelUvFit::Converted:
		text += QLatin1Char(' ') + tr("Changed; this classic face becomes a Valve 220 face to hold the turn.");
		break;
	case LevelUvFit::Approximated:
		text += QLatin1Char(' ')
			+ tr("Changed, as nearly as a classic face can hold the turn. Allow Valve 220 conversion to keep it exactly.");
		break;
	}
	return text;
}

void LevelUvView::announce()
{
	setAccessibleDescription(summary());
}

QSize LevelUvView::minimumSizeHint() const
{
	return {240, 200};
}

QSize LevelUvView::sizeHint() const
{
	return {640, 520};
}

void LevelUvView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.fillRect(rect(), palette().color(QPalette::Base));
	if (!m_hasSource) {
		painter.setPen(palette().color(QPalette::Text));
		painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, summary());
		return;
	}
	// The image, tiled as the map repeats it: image pixels to texels to the
	// screen. Nearest sampling keeps texels crisp when zoomed in.
	const QSizeF tileSize = tile();
	const QTransform toScreen = QTransform::fromTranslate(-m_centre.x(), -m_centre.y()) * QTransform::fromScale(m_zoom, m_zoom)
		* QTransform::fromTranslate(width() / 2.0, height() / 2.0);
	const QImage& source = m_image.isNull() ? m_checker : m_image;
	QBrush brush(source);
	brush.setTransform(QTransform::fromScale(tileSize.width() / source.width(), tileSize.height() / source.height()) * toScreen);
	painter.fillRect(rect(), brush);

	const QPointF topLeft = screenToTexel(QPointF(0, 0)), bottomRight = screenToTexel(QPointF(width(), height()));
	const auto lines = [&](double step, const QPen& pen) {
		if (step * m_zoom < 8 || (bottomRight.x() - topLeft.x()) / step > kLineLimit || (bottomRight.y() - topLeft.y()) / step > kLineLimit) {
			return;
		}
		painter.setPen(pen);
		for (double s = std::ceil(topLeft.x() / step) * step; s <= bottomRight.x(); s += step) {
			const double x = texelToScreen(QPointF(s, 0)).x();
			painter.drawLine(QPointF(x, 0), QPointF(x, height()));
		}
		for (double t = std::ceil(topLeft.y() / step) * step; t <= bottomRight.y(); t += step) {
			const double y = texelToScreen(QPointF(0, t)).y();
			painter.drawLine(QPointF(0, y), QPointF(width(), y));
		}
	};
	// The snap grid faintly, then each repeat's edge.
	if (m_snap > 0 && m_snap * m_zoom >= 12) {
		lines(m_snap, QPen(QColor(255, 255, 255, 40), 0));
	}
	if (tileSize.width() == tileSize.height()) {
		lines(tileSize.width(), QPen(QColor(0, 0, 0, 120), 0, Qt::DashLine));
	} else {
		// Unequal repeats: each axis on its own.
		painter.setPen(QPen(QColor(0, 0, 0, 120), 0, Qt::DashLine));
		if (tileSize.width() * m_zoom >= 8 && (bottomRight.x() - topLeft.x()) / tileSize.width() <= kLineLimit) {
			for (double s = std::ceil(topLeft.x() / tileSize.width()) * tileSize.width(); s <= bottomRight.x(); s += tileSize.width()) {
				const double x = texelToScreen(QPointF(s, 0)).x();
				painter.drawLine(QPointF(x, 0), QPointF(x, height()));
			}
		}
		if (tileSize.height() * m_zoom >= 8 && (bottomRight.y() - topLeft.y()) / tileSize.height() <= kLineLimit) {
			for (double t = std::ceil(topLeft.y() / tileSize.height()) * tileSize.height(); t <= bottomRight.y(); t += tileSize.height()) {
				const double y = texelToScreen(QPointF(0, t)).y();
				painter.drawLine(QPointF(0, y), QPointF(width(), y));
			}
		}
	}

	painter.setRenderHint(QPainter::Antialiasing, true);
	const auto outline = [&](const QVector<QPointF>& uv) {
		QVector<QLineF> segments;
		if (m_layout.isPatch()) {
			for (int row = 0; row < m_layout.rows; ++row) {
				for (int column = 0; column < m_layout.columns; ++column) {
					const int at = row * m_layout.columns + column;
					if (column + 1 < m_layout.columns) {
						segments.append({texelToScreen(uv.value(at)), texelToScreen(uv.value(at + 1))});
					}
					if (row + 1 < m_layout.rows) {
						segments.append({texelToScreen(uv.value(at)), texelToScreen(uv.value(at + m_layout.columns))});
					}
				}
			}
		} else {
			for (int i = 0; i < uv.size(); ++i) {
				segments.append({texelToScreen(uv[i]), texelToScreen(uv[(i + 1) % uv.size()])});
			}
		}
		return segments;
	};
	// Where the points started, dashed, once they have moved.
	if (changed()) {
		painter.setPen(QPen(QColor(0, 0, 0, 160), 1.5, Qt::DashLine));
		painter.drawLines(outline(m_layout.uv));
	}
	// The current winding or grid: dark under light, readable over any image.
	const QVector<QLineF> current = outline(m_result.uv);
	painter.setPen(QPen(QColor(0, 0, 0, 220), 3.5));
	painter.drawLines(current);
	painter.setPen(QPen(QColor(255, 255, 255), 1.5));
	painter.drawLines(current);

	const QColor highlight = palette().color(QPalette::Highlight);
	const QColor text = palette().color(QPalette::Text);
	const QColor base = palette().color(QPalette::Base);
	QFont labelFont = font();
	labelFont.setPointSizeF(std::max(6.0, font().pointSizeF() * 0.85));
	painter.setFont(labelFont);
	for (int i = 0; i < m_result.uv.size(); ++i) {
		const QPointF at = texelToScreen(m_result.uv[i]);
		const bool selected = m_layout.isPatch() ? m_selected.contains(i) : (m_drag == Drag::Point && i == m_dragPoint);
		const double half = selected ? 5.5 : 4;
		// Selected points are filled; others hollow, so state never rests on
		// colour alone.
		painter.setPen(QPen(QColor(0, 0, 0), 3));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(at.x() - half, at.y() - half, half * 2, half * 2));
		painter.setPen(QPen(selected ? highlight : text, 1.5));
		painter.setBrush(selected ? QBrush(highlight) : QBrush(base));
		painter.drawRect(QRectF(at.x() - half, at.y() - half, half * 2, half * 2));
		if (i == m_anchor) {
			// The anchor is a ringed cross.
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(QColor(0, 0, 0), 4));
			painter.drawEllipse(at, 10, 10);
			painter.setPen(QPen(highlight, 2));
			painter.drawEllipse(at, 10, 10);
			painter.drawLine(at + QPointF(-14, 0), at + QPointF(-7, 0));
			painter.drawLine(at + QPointF(7, 0), at + QPointF(14, 0));
			painter.drawLine(at + QPointF(0, -14), at + QPointF(0, -7));
			painter.drawLine(at + QPointF(0, 7), at + QPointF(0, 14));
		}
		if (i == m_current && hasFocus()) {
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(text, 1, Qt::DotLine));
			painter.drawRect(QRectF(at.x() - half - 4, at.y() - half - 4, half * 2 + 8, half * 2 + 8));
		}
		// Face points are numbered, as the point list and map edit-uv count them.
		if (!m_layout.isPatch()) {
			const QString label = QString::number(i + 1);
			const QRectF box = painter.fontMetrics().boundingRect(label).adjusted(-3, -1, 3, 1).translated((at + QPointF(9, -9)).toPoint());
			painter.setPen(Qt::NoPen);
			painter.setBrush(base);
			painter.drawRect(box);
			painter.setPen(text);
			painter.drawText(box, Qt::AlignCenter, label);
		}
	}
	if (m_drag == Drag::Band && m_moved) {
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(highlight, 1, Qt::DashLine));
		painter.drawRect(QRectF(m_press, m_last).normalized());
	}
	if (hasFocus()) {
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(highlight, 2, Qt::DashLine));
		painter.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}

void LevelUvView::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (!m_userCamera) {
		frameAll();
	}
}

void LevelUvView::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	if (!m_hasSource || m_drag != Drag::None) {
		event->accept();
		return;
	}
	m_press = m_last = event->position();
	m_button = event->button();
	m_moved = false;
	const bool control = event->modifiers().testFlag(Qt::ControlModifier);
	if (m_button == Qt::RightButton || m_button == Qt::MiddleButton) {
		m_drag = Drag::Pan;
		event->accept();
		return;
	}
	if (m_button != Qt::LeftButton) {
		QWidget::mousePressEvent(event);
		return;
	}
	const int hit = pointAt(m_press);
	if (m_layout.isPatch()) {
		if (hit < 0) {
			m_drag = Drag::Band;
			m_bandAdd = control;
		} else if (control) {
			QVector<int> selected = m_selected;
			if (!selected.removeOne(hit)) {
				selected.append(hit);
			}
			m_current = hit;
			m_selected = selected;
			announce();
			update();
			Q_EMIT stateChanged();
		} else {
			if (!m_selected.contains(hit)) {
				m_selected = {hit};
			}
			m_current = hit;
			m_drag = Drag::Point;
			m_dragPoint = hit;
			announce();
			Q_EMIT stateChanged();
		}
	} else if (hit >= 0) {
		m_drag = Drag::Point;
		m_dragPoint = hit;
		m_turning = m_anchor >= 0 && hit != m_anchor;
		m_current = hit;
		announce();
		Q_EMIT stateChanged();
	} else if (QPolygonF(m_result.uv).containsPoint(screenToTexel(m_press), Qt::OddEvenFill)) {
		m_drag = Drag::Grab;
	}
	m_dragStartEdit = m_edit;
	m_dragStartUv = m_result.uv;
	update();
	event->accept();
}

void LevelUvView::mouseMoveEvent(QMouseEvent* event)
{
	if (m_drag == Drag::None) {
		QWidget::mouseMoveEvent(event);
		return;
	}
	const QPointF position = event->position();
	if (!m_moved && (position - m_press).manhattanLength() < QApplication::startDragDistance()) {
		event->accept();
		return;
	}
	m_moved = true;
	switch (m_drag) {
	case Drag::Pan:
		setCursor(Qt::ClosedHandCursor);
		panBy(position - m_last);
		break;
	case Drag::Point: {
		setCursor(Qt::SizeAllCursor);
		QPointF to = screenToTexel(position);
		if (m_snap > 0) {
			to = snapLevelUv(to, m_snap);
		}
		LevelUvEdit next = m_dragStartEdit;
		if (m_layout.isPatch()) {
			next.patchUv = levelUvPatchDrag(m_dragStartUv, m_selected, m_dragPoint, to);
		} else {
			const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? m_angleStep : 0;
			next.transform = m_dragStartEdit.transform.then(levelUvFaceDrag(m_dragStartUv, m_dragPoint, m_turning ? m_anchor : -1, to, step));
			if (m_turning) {
				next.anchor = m_anchor;
			}
		}
		setWorkingEdit(next);
		break;
	}
	case Drag::Grab: {
		setCursor(Qt::ClosedHandCursor);
		QPointF delta = screenToTexel(position) - screenToTexel(m_press);
		if (m_snap > 0) {
			delta = snapLevelUv(delta, m_snap);
		}
		LevelUvEdit next = m_dragStartEdit;
		next.transform = m_dragStartEdit.transform.then(LevelUvTransform::translation(delta));
		setWorkingEdit(next);
		break;
	}
	case Drag::Band:
		update();
		break;
	case Drag::None:
		break;
	}
	m_last = position;
	event->accept();
}

void LevelUvView::finishDrag(bool commit)
{
	const Drag drag = m_drag;
	m_drag = Drag::None;
	m_turning = false;
	unsetCursor();
	if (drag == Drag::Point || drag == Drag::Grab) {
		if (!commit) {
			setWorkingEdit(m_dragStartEdit);
		} else if (!sameEdit(m_edit, m_dragStartEdit)) {
			record(m_dragStartEdit);
		}
	}
	update();
}

void LevelUvView::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_drag == Drag::None || event->button() != m_button) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	const Drag drag = m_drag;
	const bool moved = m_moved;
	if (drag == Drag::Band) {
		// Points inside the band, added to the selection with Ctrl.
		QVector<int> selected = m_bandAdd ? m_selected : QVector<int> {};
		if (moved) {
			const QRectF band = QRectF(m_press, event->position()).normalized();
			for (int i = 0; i < m_result.uv.size(); ++i) {
				if (band.contains(texelToScreen(m_result.uv[i])) && !selected.contains(i)) {
					selected.append(i);
				}
			}
		}
		m_drag = Drag::None;
		if (selected != m_selected) {
			m_selected = selected;
			if (!selected.isEmpty() && !selected.contains(m_current)) {
				m_current = selected.first();
			}
			announce();
			Q_EMIT stateChanged();
		}
		update();
	} else if (drag == Drag::Pan) {
		m_drag = Drag::None;
		unsetCursor();
		if (!moved && m_button == Qt::RightButton) {
			m_menu->popup(event->globalPosition().toPoint());
		}
	} else {
		const int point = m_dragPoint;
		finishDrag(true);
		if (drag == Drag::Point && !m_layout.isPatch()) {
			// The point dragged last is the anchor for the next turn; a click
			// on the anchor clears it, as a click elsewhere moves it.
			m_anchor = moved || m_anchor != point ? point : -1;
		}
		announce();
		Q_EMIT stateChanged();
	}
	m_button = Qt::NoButton;
	event->accept();
}

void LevelUvView::wheelEvent(QWheelEvent* event)
{
	if (m_hasSource && m_drag == Drag::None && event->angleDelta().y() != 0) {
		zoomBy(std::pow(1.2, event->angleDelta().y() / 120.0), event->position());
	}
	event->accept();
}

void LevelUvView::keyPressEvent(QKeyEvent* event)
{
	if (m_drag != Drag::None) {
		if (event->key() == Qt::Key_Escape) {
			finishDrag(false);
			m_button = Qt::NoButton;
			announce();
		}
		event->accept();
		return;
	}
	if (!m_hasSource || !viewKey(event, false)) {
		QWidget::keyPressEvent(event);
		return;
	}
	const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
	const bool shift = modifiers == Qt::ShiftModifier, control = modifiers.testFlag(Qt::ControlModifier);
	const int count = static_cast<int>(m_result.uv.size());
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down: {
		const int key = event->key();
		const QPointF direction(key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0, key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0);
		if (control) {
			panBy(-direction * kKeyPan);
		} else {
			nudge(direction * (m_snap > 0 ? m_snap : 1) * (shift ? kCoarseNudge : 1));
		}
		break;
	}
	case Qt::Key_BracketLeft:
		rotate(-m_angleStep);
		break;
	case Qt::Key_BracketRight:
		rotate(m_angleStep);
		break;
	case Qt::Key_BraceLeft:
		rotate(-1);
		break;
	case Qt::Key_BraceRight:
		rotate(1);
		break;
	case Qt::Key_Plus:
	case Qt::Key_Equal:
		zoomBy(1.25);
		break;
	case Qt::Key_Minus:
	case Qt::Key_Underscore:
		zoomBy(0.8);
		break;
	case Qt::Key_Insert:
		// TexTool's keys: Insert zooms out and Delete zooms in.
		zoomBy(0.5);
		break;
	case Qt::Key_Delete:
		zoomBy(2);
		break;
	case Qt::Key_Home:
	case Qt::Key_F:
		frameAll();
		break;
	case Qt::Key_N:
		if (count > 0) {
			const int next = ((m_current < 0 ? 0 : m_current) + (shift ? count - 1 : 1)) % count;
			if (m_layout.isPatch()) {
				m_selected = {next};
			}
			m_current = -1;
			setCurrentPoint(next);
		}
		break;
	case Qt::Key_A:
		if (control) {
			QVector<int> all;
			for (int i = 0; i < count; ++i) {
				all.append(i);
			}
			setSelectedPoints(all);
		} else if (m_current >= 0) {
			setAnchor(m_anchor == m_current ? -1 : m_current);
		}
		break;
	case Qt::Key_Z:
		if (modifiers.testFlag(Qt::ShiftModifier)) {
			redo();
		} else {
			undo();
		}
		break;
	case Qt::Key_Y:
		redo();
		break;
	default:
		break;
	}
	event->accept();
}

void LevelUvView::focusOutEvent(QFocusEvent* event)
{
	if (m_drag == Drag::Point || m_drag == Drag::Grab) {
		finishDrag(false);
	}
	m_drag = Drag::None;
	m_button = Qt::NoButton;
	unsetCursor();
	update();
	QWidget::focusOutEvent(event);
}

void LevelUvView::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange || event->type() == QEvent::LanguageChange) {
		announce();
		update();
	}
}

bool LevelUvView::event(QEvent* event)
{
	if (event->type() == QEvent::ShortcutOverride && m_hasSource) {
		const auto* key = static_cast<QKeyEvent*>(event);
		if (viewKey(key, m_drag != Drag::None)) {
			event->accept();
			return true;
		}
	}
	return QWidget::event(event);
}

} // namespace vibestudio
