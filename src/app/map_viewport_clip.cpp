#include "app/map_viewport.h"
#include "app/map_viewport_p.h"
#include "app/viewport_hud.h"
#include <QAccessible>
#include <QFontMetricsF>
#include <QPainter>
#include <QScopedValueRollback>
#include <limits>

namespace vibestudio {
namespace {
LevelMapVec3 inPlane(MapViewportProjection projection, QPointF point, LevelMapVec3 p = {0,0,0,true})
{
	if (projection == MapViewportProjection::TopXY) { p.x = point.x(); p.y = point.y(); }
	else if (projection == MapViewportProjection::FrontXZ) { p.x = point.x(); p.z = point.y(); }
	else { p.y = point.x(); p.z = point.y(); }
	return p;
}
void setAxis(LevelClipState& state, MapViewportProjection projection)
{
	state.axis = projection == MapViewportProjection::TopXY ? 2 : projection == MapViewportProjection::FrontXZ ? 1 : 0;
	state.direction = state.axis == 1 ? -1 : 1;
}
}

void MapViewport::setClipMode(bool enabled)
{
	if (m_clipMode == enabled) { return; }
	cancelInteraction(); m_clipMode = enabled; m_clipState.points.clear(); m_clipPreview = {}; m_hoverHandle = 0;
	if (enabled) { setCursor(Qt::CrossCursor); } else { unsetCursor(); }
	setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT hoverChanged(enabled ? clipSummary() : hoverSummary()); Q_EMIT clipModeChanged(enabled);
}
bool MapViewport::clipMode() const { return m_clipMode; }
bool MapViewport::setClipState(const LevelClipState& state)
{
	if (!state.valid()) { return false; }
	if (state == m_clipState) { return true; }
	if (!m_clipChanging) { m_clipDrawing = false; m_clipGrabbed = -1; }
	m_clipState = state; m_clipPreview = {};
	setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT clipStateChanged(); Q_EMIT hoverChanged(clipSummary());
	return true;
}
void MapViewport::setClipKeep(LevelMapClipKeep keep) { auto state = m_clipState; state.keep = keep; setClipState(state); }
LevelMapClipKeep MapViewport::clipKeep() const { return m_clipState.keep; }
bool MapViewport::hasClipLine() const { return m_clipState.plane(nullptr,nullptr,nullptr); }
bool MapViewport::clipPlanePoints(LevelMapVec3* a, LevelMapVec3* b, LevelMapVec3* c) const { return m_clipState.plane(a,b,c); }
void MapViewport::setClipLine(const QPointF& from, const QPointF& to)
{
	finishClipPoint(false); auto state = m_clipState; setAxis(state,m_projection);
	state.points = {inPlane(m_projection,from),inPlane(m_projection,to)}; setClipState(state);
}
void MapViewport::clearClipLine()
{
	finishClipPoint(false); auto state = m_clipState; state.points.clear(); setClipState(state); m_clipPreview = {}; update();
}
void MapViewport::setClipPreview(const LevelClipPreview& preview)
{
	m_clipPreview = preview; setAccessibleDescription(accessibleSummary()); update(); Q_EMIT hoverChanged(clipSummary());
	QAccessibleEvent changed(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
}
QPointF MapViewport::clipPointPosition(int index) const
{
	if (index < 0 || index >= m_clipState.points.size()) { return {std::numeric_limits<double>::quiet_NaN(),0}; }
	const auto p = mapViewportProjectPoint(m_projection,m_clipState.points[index]); return worldToView(p.x(),p.y());
}
int MapViewport::clipPointAt(const QPointF& position) const
{
	int best = -1; double distance = 100;
	for (int i = 0; i < m_clipState.points.size(); ++i) {
		const auto delta = clipPointPosition(i)-position; const double d = QPointF::dotProduct(delta,delta);
		if (d <= distance) { distance = d; best = i; }
	}
	return best;
}
bool MapViewport::beginClipPoint(const QPointF& position)
{
	if (!m_clipMode || !isEnabled() || !std::isfinite(position.x()) || !std::isfinite(position.y())) { return false; }
	finishClipPoint(false); m_clipBefore = m_clipState; m_clipGrabbed = clipPointAt(position); m_clipAdding = m_clipGrabbed < 0;
	auto state = m_clipState;
	if (m_clipAdding) {
		if (state.points.size() >= (m_clipPreferences.twoPointsInPlan ? 2 : 3)) { state.points.clear(); }
		LevelMapVec3 p{0,0,0,true},low,high;
		if (!state.points.isEmpty()) { p = state.points.last(); }
		else {
			if (selectionTransformBounds(&low,&high)) { p = {(low.x+high.x)/2,(low.y+high.y)/2,(low.z+high.z)/2,true}; }
			setAxis(state,m_projection);
		}
		state.points << inPlane(m_projection,snappedPlanePoint(position),p); m_clipGrabbed = int(state.points.size()-1);
	}
	if (!state.valid()) { m_clipGrabbed = -1; return false; }
	m_clipDrawing = true; const QScopedValueRollback guard(m_clipChanging,true); setClipState(state); return true;
}
bool MapViewport::moveClipPoint(const QPointF& position)
{
	if (!m_clipDrawing || !std::isfinite(position.x()) || !std::isfinite(position.y())) { return false; }
	auto state = m_clipState;
	const auto p = inPlane(m_projection,snappedPlanePoint(position),state.points[m_clipGrabbed]);
	if (!LevelClipState::finite(p)) { return false; }
	if (m_clipAdding && state.points.size() == 1) {
		if (QLineF(position,clipPointPosition(0)).length() < 4) { return true; }
		state.points << p; m_clipGrabbed = 1;
	} else { state.points[m_clipGrabbed] = p; }
	const QScopedValueRollback guard(m_clipChanging,true); return setClipState(state);
}
void MapViewport::finishClipPoint(bool accept)
{
	if (!m_clipDrawing) { return; }
	m_clipDrawing = false; m_clipGrabbed = -1; if (!accept) { setClipState(m_clipBefore); } update();
}
void MapViewport::removeLastClipPoint()
{
	finishClipPoint(false); auto state = m_clipState; if (!state.points.isEmpty()) { state.points.removeLast(); setClipState(state); }
}
void MapViewport::requestClip()
{
	if (!m_clipMode) { return; } finishClipPoint(true);
	LevelMapVec3 a,b,c; clipPlanePoints(&a,&b,&c);
	if (m_clipState.actionable(m_clipPreferences.wrapMerge)) { Q_EMIT clipRequested(a,b,c,int(m_clipState.keep)); }
}
bool MapViewport::beginQuickClipPoint(const QPointF& position, Qt::KeyboardModifiers modifiers)
{
	if (m_clipMode || !isEnabled() || m_componentMode != ComponentMode::Off || m_drawMode || m_shapeDrawMode || m_regionDrawMode
		|| m_handMode || m_panHoldActive || m_controls.quickClipModifiers == Qt::NoModifier || modifiers != m_controls.quickClipModifiers
		|| !std::isfinite(position.x()) || !std::isfinite(position.y())) { return false; }
	const bool brushes = std::any_of(m_selection.cbegin(),m_selection.cend(),[this](const auto& ref) {
		return ref.kind == LevelMapSelectionKind::QuakeBrush || (ref.kind == LevelMapSelectionKind::Entity
			&& std::any_of(m_sourceDocument.brushes.cbegin(),m_sourceDocument.brushes.cend(),[&](const auto& b) { return b.entityId == ref.objectId; }));
	});
	if (!brushes) { return false; }
	Q_EMIT quickClipRequested(); return m_clipMode && beginClipPoint(position);
}
bool MapViewport::activateClipDoubleClick()
{
	if (!m_clipMode || !isEnabled() || m_clipPreferences.doubleClick == LevelClipDoubleClick::None || !m_clipState.actionable(m_clipPreferences.wrapMerge)) { return false; }
	finishClipPoint(true);
	setClipKeep(m_clipPreferences.doubleClick == LevelClipDoubleClick::Split ? LevelMapClipKeep::Both
		: m_clipState.keep == LevelMapClipKeep::Both ? LevelMapClipKeep::Back : m_clipState.keep);
	requestClip(); return true;
}
QString MapViewport::clipSummary() const
{
	if (!m_clipPreview.message.isEmpty()) { return m_clipPreview.message; }
	return m_clipState.points.isEmpty() ? tr("Clip: place or drag points; Tab chooses sides; Enter cuts; Escape leaves.")
		: tr("Clip: %n point(s); drag numbered handles; Backspace removes the last point.",nullptr,int(m_clipState.points.size()));
}
void MapViewport::paintClip(QPainter& painter, const Palette& palette) const
{
	if (!m_clipMode) { return; }
	painter.save(); painter.setFont(viewportHudFont(font()));
	const auto screen = [this](LevelMapVec3 p) { const auto q = mapViewportProjectPoint(m_projection,p); return worldToView(q.x(),q.y()); };
	LevelMapVec3 a,b,c;
	if (m_clipState.plane(&a,&b,&c)) {
		const auto plane = planeFromPoints(b,a,c);
		// Intersect with the plane through the first handle, parallel to this
		// view. For oblique cuts the validated wire gives the full depth result.
		const auto n = mapViewportProjectPoint(m_projection,{plane.normalX,plane.normalY,plane.normalZ,true});
		const double length = std::hypot(n.x(),n.y());
		if (length > 1e-6) {
			const auto origin = screen(a); const QPointF normal(n.x()/length,-n.y()/length), along(-normal.y(),normal.x());
			const double reach = std::hypot(double(width()),double(height()))*2;
			const auto start = origin-along*reach, end = origin+along*reach;
			if (!m_clipPreview.wrapped && m_clipState.keep != LevelMapClipKeep::Both) {
				const auto away = normal*reach*(m_clipState.keep == LevelMapClipKeep::Back ? 1 : -1);
				auto colour = palette.warning; colour.setAlpha(m_highContrast ? 140 : 85);
				painter.setPen(Qt::NoPen); painter.setBrush(QBrush(colour,Qt::BDiagPattern)); painter.drawPolygon(QPolygonF{start,end,end+away,start+away});
			}
			painter.setPen(QPen(palette.warning,2)); painter.drawLine(start,end);
		}
	}
	painter.setBrush(Qt::NoBrush); painter.setPen(QPen(palette.selection,m_highContrast ? 3 : 2,Qt::DashLine));
	for (const auto& edge : m_clipPreview.edges) { painter.drawLine(screen(edge.first),screen(edge.second)); }
	for (int i = 0; i < m_clipState.points.size(); ++i) {
		const auto p = clipPointPosition(i); painter.setPen(QPen(palette.text,2)); painter.setBrush(palette.background); painter.drawEllipse(p,6,6);
		int stacked = 0;
		for (int j = 0; j < i; ++j) { if (QLineF(p,clipPointPosition(j)).length() < 12) { ++stacked; } }
		painter.drawText(p+QPointF(10,-10-stacked*QFontMetricsF(painter.font()).height()),QString::number(i+1));
	}
	painter.restore();
}
} // namespace vibestudio
