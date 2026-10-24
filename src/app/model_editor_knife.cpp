#include "app/model_editor_tools.h"

#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "core/model_geometry_helpers.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <numbers>

namespace vibestudio
{
namespace
{
using namespace model_geometry;
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelKnife)
};
P3 vector(const std::array<double, 3> &v) { return {v[0], v[1], v[2]}; }
std::array<double, 3> array(ModelVec3 v) { return {v.x, v.y, v.z}; }
ModelVec3 vec(P3 p) { return {float(p.x), float(p.y), float(p.z)}; }
QPointF coordinates(const ModelKnifeOptions &o, const ModelPickRay &ray)
{
	const auto p = o.perspective ? point(ray.direction) : point(ray.origin) - vector(o.origin);
	const double divisor = o.perspective ? dot(p, vector(o.forward)) : 1;
	return {dot(p, vector(o.right)) / divisor, dot(p, vector(o.up)) / divisor};
}
QPointF screen(ModelViewport *viewport, const ModelKnifeOptions &o, QPointF p)
{
	const double depth = o.perspective ? 100 : 0;
	const double factor = o.perspective ? depth : 1;
	QPointF result;
	viewport->projectToView(
		vec(vector(o.origin) + vector(o.right) * (p.x() * factor) + vector(o.up) * (p.y() * factor) + vector(o.forward) * depth), &result);
	return result;
}
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
} // namespace

void ModelEditorTools::initialiseKnife(Modal &m)
{
	m.edit = toolEdit(ModelEditKind::Knife);
	m.knifeRevision = m_editor->m_document.revisionFingerprint();
	m.knife.hiddenFaces = m_hiddenFaces;
	m.knife.perspective = m_viewport->isPerspective();
	m.knife.origin = array(m_viewport->viewRay(QPointF(m_viewport->width() / 2., m_viewport->height() / 2.)).origin);
	ModelVec3 right, up;
	m_viewport->viewAxes(&right, &up);
	m.knife.right = array(right);
	m.knife.up = array(up);
	m.knife.forward = array(m_viewport->viewForward());
	m.knifeHover = coordinates(m.knife, m_viewport->viewRay(m.pointer));
	m.knifePaths.append(QPolygonF{});
	const auto &surface = m_editor->m_document.mesh().surfaces[m.selection.surface];
	m.snapWorld = surface.frames[m.edit.tool.referenceFrame].positions;
	m.snapScreen = QVector<QPointF>(surface.vertexCount, QPointF(NAN, NAN));
	QSet<ModelEdge> edges;
	QSet<int> vertices;
	for (int f = 0; f < surface.triangles.size(); ++f)
	{
		if (m.knife.hiddenFaces.value(m.selection.surface).contains(f))
			continue;
		const auto &t = surface.triangles[f];
		vertices << t.a << t.b << t.c;
		edges << modelEdge(t.a, t.b) << modelEdge(t.b, t.c) << modelEdge(t.c, t.a);
	}
	m.knifeSnapVertices = vertices.values();
	std::sort(m.knifeSnapVertices.begin(), m.knifeSnapVertices.end());
	m.knifeSnapEdges = edges.values();
	std::sort(m.knifeSnapEdges.begin(), m.knifeSnapEdges.end());
}

void ModelEditorTools::moveKnife(const QPointF &pointOnView, Qt::KeyboardModifiers modifiers)
{
	if (!m_modal || m_modal->op != ModelModalOperator::Knife || m_editor->m_working)
		return;
	auto &m = *m_modal;
	m.lastActual = pointOnView;
	m.modifiers = modifiers;
	const QPolygonF projection{screen(m_viewport, m.knife, {0, 0}), screen(m_viewport, m.knife, {1, 0}),
							   screen(m_viewport, m.knife, {0, 1})};
	if (projection != m.knifeProjection)
	{
		m.knifeProjection = projection;
		for (int v : m.knifeSnapVertices)
		{
			m.snapScreen[v] = QPointF(NAN, NAN);
			m_viewport->projectToView(m.snapWorld[v], &m.snapScreen[v]);
		}
	}
	QPointF target = pointOnView;
	m.snapped = false;
	if (m.knifeAngle && !m.knifePaths.isEmpty() && !m.knifePaths.last().isEmpty())
	{
		const auto anchor = screen(m_viewport, m.knife, m.knifePaths.last().last());
		const auto delta = target - anchor;
		const double angle = std::atan2(delta.y(), delta.x()), increment = m.knifeAngleStep * std::numbers::pi / 180;
		const double snapped = std::round(angle / increment) * increment, radius = std::hypot(delta.x(), delta.y());
		target = anchor + QPointF(std::cos(snapped) * radius, std::sin(snapped) * radius);
	}
	if (!(modifiers & Qt::ControlModifier))
	{
		double best = 10;
		const auto consider = [&](QPointF candidate, ModelVec3 world, bool needsVisibility)
		{
			if (!finite(candidate))
				return;
			// Geometry snapping must not break an explicit angle constraint.
			if (m.knifeAngle && !m.knifePaths.isEmpty() && !m.knifePaths.last().isEmpty())
			{
				const auto anchor = screen(m_viewport, m.knife, m.knifePaths.last().last());
				const auto ray = target - anchor, offset = candidate - anchor;
				const double radius = std::hypot(ray.x(), ray.y());
				if (radius > 1e-6 && std::abs(ray.x() * offset.y() - ray.y() * offset.x()) / radius > 1e-4)
					return;
			}
			const double distance = QLineF(candidate, target).length();
			if (distance >= best)
				return;
			if (needsVisibility && !m.knife.through)
			{
				// Exact silhouette vertices may fall outside the raster's pixel
				// centres. Inspect a one-pixel neighbourhood, still requiring a
				// matching world depth so rear vertices cannot snap through a face.
				bool visible = false;
				for (const auto &offset : {QPointF{}, QPointF(-1, 0), QPointF(1, 0), QPointF(0, -1), QPointF(0, 1), QPointF(-1, -1),
										   QPointF(1, -1), QPointF(-1, 1), QPointF(1, 1)})
				{
					CameraSurfacePoint hit;
					if (m_viewport->surfacePointAt(candidate + offset, &hit) &&
						length(vector(hit.position) - model_geometry::point(world)) <= std::max(.001, worldPerPixel(world) * 1.5))
					{
						visible = true;
						break;
					}
				}
				if (!visible)
					return;
			}
			best = distance;
			m.snapPoint = candidate;
			m.snapped = true;
		};
		if (!(modifiers & Qt::ShiftModifier))
		{
			for (int v : m.knifeSnapVertices)
				consider(m.snapScreen[v], m.snapWorld[v], true);
			for (const auto &path : m.knifePaths)
				for (const auto &p : path)
					consider(screen(m_viewport, m.knife, p), {}, false);
		}
		// Vertices/path anchors take precedence over nearer arbitrary edge points.
		const bool snappedAnchor = m.snapped;
		for (auto edge : m.knifeSnapEdges)
		{
			if (snappedAnchor)
				break;
			const auto a = m.snapScreen[edge.first], b = m.snapScreen[edge.second];
			if (!finite(a) || !finite(b) || QLineF(a, b).length() < 1e-6)
				continue;
			const auto delta = b - a;
			double t = std::clamp(QPointF::dotProduct(target - a, delta) / QPointF::dotProduct(delta, delta), 0.0, 1.0);
			if (m.knife.perspective)
			{
				const double da = dot(model_geometry::point(m.snapWorld[edge.first]) - vector(m.knife.origin), vector(m.knife.forward));
				const double db = dot(model_geometry::point(m.snapWorld[edge.second]) - vector(m.knife.origin), vector(m.knife.forward));
				if (da <= 0 || db <= 0)
					continue;
				t = (t / db) / ((1 - t) / da + t / db);
			}
			if (modifiers & Qt::ShiftModifier)
				t = .5;
			const auto world =
				vec(model_geometry::point(m.snapWorld[edge.first]) * (1 - t) + model_geometry::point(m.snapWorld[edge.second]) * t);
			QPointF candidate;
			if (m_viewport->projectToView(world, &candidate))
				consider(candidate, world, true);
		}
		if (m.snapped)
			target = m.snapPoint;
	}
	m.pointer = target;
	m.knifeHover = coordinates(m.knife, m_viewport->viewRay(target));
	if (!m.pending)
	{
		m.pending = true;
		QTimer::singleShot(0, this,
						   [this]
						   {
							   if (m_modal && m_modal->op == ModelModalOperator::Knife && m_modal->pending && !m_editor->m_working)
							   {
								   m_modal->pending = false;
								   updateKnifePreview();
							   }
						   });
	}
}

bool ModelEditorTools::addKnifePoint(const QPointF &p, Qt::KeyboardModifiers modifiers)
{
	if (!m_modal || m_modal->op != ModelModalOperator::Knife || m_editor->m_working)
		return false;
	moveKnife(p, modifiers);
	auto &m = *m_modal;
	if (!finite(m.knifeHover))
		return false;
	if (m.knifePaths.isEmpty())
		m.knifePaths.append(QPolygonF{});
	auto &path = m.knifePaths.last();
	if (path.isEmpty() || QLineF(path.last(), m.knifeHover).length() > 1e-10)
		path.append(m.knifeHover);
	m.pending = false;
	updateKnifePreview();
	return true;
}
void ModelEditorTools::newKnifePath()
{
	if (!m_modal || m_modal->op != ModelModalOperator::Knife || m_editor->m_working)
		return;
	if (!m_modal->knifePaths.isEmpty() && !m_modal->knifePaths.last().isEmpty())
		m_modal->knifePaths.append(QPolygonF{});
	m_modal->pending = false;
	updateKnifePreview();
}
void ModelEditorTools::undoKnifePoint()
{
	if (!m_modal || m_modal->op != ModelModalOperator::Knife || m_editor->m_working)
		return;
	auto &paths = m_modal->knifePaths;
	while (paths.size() > 1 && paths.last().isEmpty())
		paths.removeLast();
	if (!paths.isEmpty() && !paths.last().isEmpty())
	{
		paths.last().removeLast();
		if (!paths.last().isEmpty())
		{
			m_modal->knifeHover = paths.last().last();
			m_modal->pointer = m_modal->lastActual = screen(m_viewport, m_modal->knife, m_modal->knifeHover);
		}
	}
	m_modal->snapped = false;
	m_modal->pending = false;
	updateKnifePreview();
}
void ModelEditorTools::setKnifeThrough(bool through)
{
	if (!m_modal || m_modal->op != ModelModalOperator::Knife || m_editor->m_working)
		return;
	m_modal->knife.through = through;
	updateKnifePreview();
}
void ModelEditorTools::setKnifeSelectedOnly(bool selectedOnly)
{
	if (!m_modal || m_modal->op != ModelModalOperator::Knife || m_editor->m_working)
		return;
	m_modal->knife.selectedOnly = selectedOnly;
	updateKnifePreview();
}
void ModelEditorTools::setKnifeAngleSnap(bool enabled, double degrees)
{
	if (!m_modal || m_modal->op != ModelModalOperator::Knife || m_editor->m_working || !std::isfinite(degrees) || degrees < 1 ||
		degrees > 180)
		return;
	m_modal->knifeAngle = enabled;
	m_modal->knifeAngleStep = degrees;
	m_modal->typed.clear();
	moveKnife(m_modal->lastActual, m_modal->modifiers);
}

bool ModelEditorTools::knifeContextUnchanged() const
{
	if (!m_modal)
		return false;
	const auto &o = m_modal->knife;
	ModelVec3 right, up;
	m_viewport->viewAxes(&right, &up);
	return m_editor->m_document.revisionFingerprint() == m_modal->knifeRevision &&
		   std::max(0, m_editor->m_frame->currentIndex()) == m_modal->edit.tool.referenceFrame && m_hiddenFaces == o.hiddenFaces &&
		   m_viewport->isPerspective() == o.perspective && length(point(right) - vector(o.right)) < 1e-5 &&
		   length(point(up) - vector(o.up)) < 1e-5 && length(point(m_viewport->viewForward()) - vector(o.forward)) < 1e-5 &&
		   (!o.perspective || length(point(m_viewport->cameraPosition()) - vector(o.origin)) < 1e-4);
}

void ModelEditorTools::updateKnifePreview()
{
	if (!m_modal || m_modal->op != ModelModalOperator::Knife || m_editor->m_working)
		return;
	auto &m = *m_modal;
	ModelEdit edit;
	edit.kind = ModelEditKind::Knife;
	edit.selection = m.selection;
	edit.tool.referenceFrame = m.edit.tool.referenceFrame;
	edit.tool.knife = m.knife;
	auto paths = m.knifePaths;
	if (!paths.isEmpty() && !paths.last().isEmpty() && finite(m.knifeHover) && QLineF(paths.last().last(), m.knifeHover).length() > 1e-10)
		paths.last().append(m.knifeHover);
	for (const auto &path : paths)
		for (int i = 1; i < path.size(); ++i)
			edit.tool.knife.segments.append(ModelKnifeSegment{{path[i - 1].x(), path[i - 1].y()}, {path[i].x(), path[i].y()}});
	m.edit = edit;
	m.editReady = !edit.tool.knife.segments.isEmpty();
	m.valid = false;
	m.error.clear();
	if (!knifeContextUnchanged())
		m.error = Text::tr("The view, pose, mesh or visibility changed. Cancel this knife path and start it again.");
	else if (m.editReady)
	{
		auto copy = m_editor->m_document.mesh();
		ModelSelection selected;
		ModelEditReport report;
		const bool ready = m_editor->performWork(
			Text::tr("Previewing knife cuts…"),
			[&copy, &selected, &report, edit](ModelDocument &, QString *failure, const ModelWorkControl &control)
			{ return applyModelEdit(&copy, edit, &selected, failure, control, &report); }, &m.error, false,
			ModelEditorDialog::WorkKind::Preview);
		if (ready)
		{
			m.valid = true;
			m.preview = std::move(copy);
			m.previewSelection = selected;
			m.report = std::move(report);
			showPreview(m.preview, m.previewSelection, &m.report);
			m.previewShown = true;
		}
	}
	if (!m.valid && m.previewShown)
	{
		restoreDocumentView();
		m.previewShown = false;
	}
	QString summary = !m.editReady ? Text::tr("Knife · add points")
					  : !m.valid   ? Text::tr("Knife refused")
								   : Text::tr("Knife · %1 · %n segment(s)", nullptr, edit.tool.knife.segments.size())
									   .arg(m.knife.through ? Text::tr("Through") : Text::tr("Visible"));
	if (m.knife.selectedOnly)
		summary += Text::tr(" · selected");
	if (m.knifeAngle)
		summary += Text::tr(" · %1°").arg(m.knifeAngleStep);
	showBanner(summary, modalText(), LiveStatus::Update);
	m_viewport->setTransformGizmo(false, ModelTransformTool::Move);
	m_viewport->update();
}

bool ModelEditorTools::handleKnifeKey(QKeyEvent *event)
{
	const int key = event->key();
	const auto modifiers = event->modifiers();
	if (key == Qt::Key_Escape)
		finishModal(false);
	else if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space)
		finishModal(true);
	else if (key == Qt::Key_Insert)
		addKnifePoint(m_modal->lastActual, modifiers);
	else if (key == Qt::Key_N)
		newKnifePath();
	else if (key == Qt::Key_Backspace || (key == Qt::Key_Z && (modifiers & Qt::ControlModifier)))
		undoKnifePoint();
	else if (key == Qt::Key_C)
		setKnifeThrough(!m_modal->knife.through);
	else if (key == Qt::Key_S)
		setKnifeSelectedOnly(!m_modal->knife.selectedOnly);
	else if (key == Qt::Key_A)
		setKnifeAngleSnap(!m_modal->knifeAngle, m_modal->knifeAngleStep);
	else if (key >= Qt::Key_0 && key <= Qt::Key_9)
		typeModal(QString(QChar('0' + key - Qt::Key_0)));
	else if (key == Qt::Key_Period || key == Qt::Key_Comma)
		typeModal(QStringLiteral("."));
	else if (key == Qt::Key_Shift || key == Qt::Key_Control)
		moveKnife(m_modal->lastActual, modifiers);
	else if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down)
	{
		const int step = modifiers & Qt::AltModifier ? 1 : 10;
		moveKnife(m_modal->lastActual + QPointF(key == Qt::Key_Left	   ? -step
												: key == Qt::Key_Right ? step
																	   : 0,
												key == Qt::Key_Up	  ? -step
												: key == Qt::Key_Down ? step
																	  : 0),
				  modifiers);
	}
	return true;
}

void ModelEditorTools::paintKnife(QPainter &painter)
{
	if (!m_modal)
		return;
	const auto &m = *m_modal;
	const auto colour = m_viewport->palette().highlight().color();
	painter.setPen(QPen(colour, 2, m.valid || !m.editReady ? Qt::SolidLine : Qt::DashLine));
	painter.setBrush(m_viewport->palette().base());
	for (const auto &path : m.knifePaths)
	{
		QPolygonF projected;
		for (const auto &p : path)
			projected.append(screen(m_viewport, m.knife, p));
		painter.drawPolyline(projected);
		for (const auto &p : projected)
			painter.drawEllipse(p, 4, 4);
	}
	if (!m.knifePaths.isEmpty() && !m.knifePaths.last().isEmpty())
		painter.drawLine(screen(m_viewport, m.knife, m.knifePaths.last().last()), m.pointer);
	painter.drawLine(m.pointer + QPointF(-7, 0), m.pointer + QPointF(7, 0));
	painter.drawLine(m.pointer + QPointF(0, -7), m.pointer + QPointF(0, 7));
	if (m.snapped)
		painter.drawRect(QRectF(m.pointer - QPointF(5, 5), QSizeF(10, 10)));
}

void ModelEditorTools::remapHiddenAfterEdit(const ModelEdit &edit, const ModelEditReport &report)
{
	if (edit.kind != ModelEditKind::Knife)
		return;
	m_hiddenFaces = edit.tool.knife.hiddenFaces;
	for (auto i = report.sourceFaces.cbegin(); i != report.sourceFaces.cend(); ++i)
	{
		const auto original = m_hiddenFaces.value(i.key());
		if (original.isEmpty())
			continue;
		QSet<int> mapped;
		for (int f = 0; f < i.value().size(); ++f)
			if (original.contains(i.value()[f]))
				mapped.insert(f);
		m_hiddenFaces[i.key()] = std::move(mapped);
	}
	m_hiddenTriangleCounts.clear();
	const auto &surfaces = m_editor->m_document.mesh().surfaces;
	for (auto i = m_hiddenFaces.cbegin(); i != m_hiddenFaces.cend(); ++i)
		if (i.key() >= 0 && i.key() < surfaces.size())
			m_hiddenTriangleCounts[i.key()] = surfaces[i.key()].triangles.size();
	m_editor->m_presentedMeshRevision.clear();
	m_editor->refresh();
}
} // namespace vibestudio
