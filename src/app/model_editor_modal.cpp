#include "app/model_editor_tools.h"

#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "core/model_collision.h"
#include "core/model_geometric_topology.h"
#include "core/model_tags.h"
#include "core/model_transform_axes.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelTools)
};
using V = ModelVec3;
double dot(V a, V b)
{
	return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
}
V add(V a, V b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
V sub(V a, V b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
V scale(V a, double s)
{
	return {float(a.x * s), float(a.y * s), float(a.z * s)};
}
V cross(V a, V b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(V a)
{
	return std::sqrt(dot(a, a));
}
V unit(V a)
{
	const double l = length(a);
	return l > 1e-12 ? scale(a, 1 / l) : V{0, 0, 0};
}
V axisVector(int axis)
{
	return axis == 0 ? V{1, 0, 0} : axis == 1 ? V{0, 1, 0} : V{0, 0, 1};
}
// Parameter along the line (point, direction) closest to the ray.
bool closestOnLine(const ModelPickRay &ray, V point, V direction, double *t)
{
	const V w = sub(point, ray.origin);
	const double a = dot(direction, direction), b = dot(direction, ray.direction), c = dot(ray.direction, ray.direction);
	const double d = dot(direction, w), e = dot(ray.direction, w);
	const double denominator = a * c - b * b;
	if (std::abs(denominator) < 1e-12 * std::max(1.0, a * c))
		return false;
	*t = (b * e - c * d) / denominator;
	return std::isfinite(*t);
}
bool planeHit(const ModelPickRay &ray, V point, V normal, V *hit)
{
	const double denominator = dot(ray.direction, normal);
	if (std::abs(denominator) < 1e-12)
		return false;
	const double t = dot(sub(point, ray.origin), normal) / denominator;
	*hit = add(ray.origin, scale(ray.direction, t));
	return std::isfinite(hit->x) && std::isfinite(hit->y) && std::isfinite(hit->z);
}
double snapValue(double value, double step)
{
	return step > 0 ? std::round(value / step) * step : value;
}
// Custom-axes angles that turn the basis X axis onto `axis`.
V axisRotationFor(V axis)
{
	axis = unit(axis);
	const double pitch = -std::asin(std::clamp(double(axis.z), -1.0, 1.0)) * 180 / std::numbers::pi;
	const double yaw = std::atan2(axis.y, axis.x) * 180 / std::numbers::pi;
	return {0, float(pitch), float(yaw)};
}
const QColor axisColours[3]{QColor(226, 104, 104), QColor(126, 206, 132), QColor(122, 158, 240)};
} // namespace

ModelModalOperator ModelEditorTools::modalOperator() const
{
	return m_modal ? m_modal->op : ModelModalOperator::None;
}

bool ModelEditorTools::modalPreviewValid() const
{
	return m_modal && m_modal->valid;
}

const ModelMesh *ModelEditorTools::modalPreview() const
{
	return m_modal && m_modal->previewShown ? &m_modal->preview : nullptr;
}

double ModelEditorTools::worldPerPixel(const ModelVec3 &at)
{
	QPointF screen;
	if (!m_viewport->projectToView(at, &screen))
		screen = QPointF(m_viewport->width() / 2.0, m_viewport->height() / 2.0);
	const V forward = m_viewport->viewForward();
	V a, b;
	if (!planeHit(m_viewport->viewRay(screen), at, forward, &a) || !planeHit(m_viewport->viewRay(screen + QPointF(1, 0)), at, forward, &b))
		return 1;
	return std::max(length(sub(b, a)), 1e-6);
}

void ModelEditorTools::startModalAtPointer(ModelModalOperator op)
{
	beginModal(op, pointerInViewport());
}

bool ModelEditorTools::beginModal(ModelModalOperator op, const QPointF &pointer)
{
	if (m_modal || m_editor->m_working || op == ModelModalOperator::None)
		return false;
	const auto &mesh = m_editor->m_document.mesh();
	auto modal = std::make_unique<Modal>();
	modal->op = op;
	modal->start = modal->pointer = modal->lastActual = pointer;
	modal->selection = m_editor->m_document.selection();
	const auto &selection = modal->selection;
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, int(mesh.frames.size()) - 1);
	const bool components = !selection.faces.isEmpty() || !selection.vertices.isEmpty() || !selection.edges.isEmpty();
	const bool transform = op == ModelModalOperator::Move || op == ModelModalOperator::Rotate || op == ModelModalOperator::Scale;
	QString refusal;
	if (transform && !components && selection.tag.isEmpty() && selection.collision.isEmpty() && selection.surfaces.isEmpty())
		refusal = Text::tr("Select something to transform first.");
	if (transform && op == ModelModalOperator::Scale && !selection.tag.isEmpty())
		refusal = Text::tr("Attachment tags are rigid; they can be moved and rotated but not scaled.");
	if (op == ModelModalOperator::Extrude)
	{
		const auto faces = selectionFor(ModelEditKind::Extrude);
		if (!faces.faces.isEmpty())
			modal->selection = faces;
		else if (!selectionFor(ModelEditKind::ExtrudeEdges).edges.isEmpty())
		{
			modal->selection = selectionFor(ModelEditKind::ExtrudeEdges);
			modal->edges = true;
		}
		else
			refusal = Text::tr("Extrude needs selected faces or open border edges.");
	}
	if (op == ModelModalOperator::Inset || op == ModelModalOperator::Duplicate)
	{
		modal->selection = selectionFor(ModelEditKind::InsetFaces);
		if (modal->selection.faces.isEmpty())
			refusal = op == ModelModalOperator::Inset ? Text::tr("Inset needs selected faces.") : Text::tr("Duplicate needs selected faces.");
	}
	if (op == ModelModalOperator::ShrinkFatten && !components)
		refusal = Text::tr("Select the vertices, edges or faces to shrink or fatten.");
	if (op == ModelModalOperator::BevelVertices && !components)
		refusal = Text::tr("Select the vertices to bevel.");
	if (!refusal.isEmpty())
	{
		showBanner(refusal);
		return false;
	}
	// Pivot: the editor's pivot choice, or the selection's centre.
	V pivot{};
	const int pivotMode = m_editor->m_pivotMode->currentIndex();
	if (!selection.tag.isEmpty())
	{
		if (const auto tag = findModelTag(mesh, selection.tag, frame))
			pivot = tag->origin;
	}
	else if (!selection.collision.isEmpty())
	{
		ModelCollisionBox pose;
		if (const auto box = findModelCollisionBox(mesh, selection.collision); box && sampleModelCollisionBox(*box, frame, frame, 0, &pose))
			pivot = pose.centre;
	}
	else if (pivotMode == 2)
		pivot = {float(m_editor->m_pivot[0]->value()), float(m_editor->m_pivot[1]->value()), float(m_editor->m_pivot[2]->value())};
	else if (pivotMode == 1 || !transform)
	{
		if (!selectionCentre(&pivot))
			pivot = m_cursor;
	}
	modal->pivot = pivot;
	if (!m_viewport->projectToView(pivot, &modal->pivotScreen))
		modal->pivotScreen = QPointF(m_viewport->width() / 2.0, m_viewport->height() / 2.0);
	modal->startDistance = std::max(QLineF(pointer, modal->pivotScreen).length(), op == ModelModalOperator::Inset ? 48.0 : 1.0);
	const QPointF offset = pointer - modal->pivotScreen;
	modal->lastAngle = std::atan2(-offset.y(), offset.x());
	if (op == ModelModalOperator::Extrude && !modal->edges)
	{
		const auto &surface = mesh.surfaces[modal->selection.surface];
		V normal{};
		for (int face : modal->selection.faces)
		{
			const auto &t = surface.triangles[face];
			const auto &p = surface.frames[frame].positions;
			normal = add(normal, cross(sub(p[t.b], p[t.a]), sub(p[t.c], p[t.a])));
		}
		modal->normal = unit(normal);
		if (length(modal->normal) <= 0)
			modal->normal = {0, 0, 1};
	}
	const bool moveLike = op == ModelModalOperator::Move || op == ModelModalOperator::Duplicate || (op == ModelModalOperator::Extrude && modal->edges);
	if (moveLike && components && frame >= 0)
	{
		const int surfaceIndex = modal->selection.surface;
		const auto &active = mesh.surfaces[surfaceIndex];
		QSet<int> moving = modal->selection.vertices;
		for (auto edge : std::as_const(modal->selection.edges))
			moving << edge.first << edge.second;
		for (int face : std::as_const(modal->selection.faces))
		{
			const auto &t = active.triangles[face];
			moving << t.a << t.b << t.c;
		}
		ModelGeometricTopology topology;
		if (buildModelGeometricTopology(active, &topology))
		{
			QSet<int> expanded;
			for (int v : std::as_const(moving))
			{
				for (int copy : topology.copies.value(topology.group(v)))
					expanded.insert(copy);
			}
			for (int v : std::as_const(expanded))
				modal->selectedPoints.append(active.frames[frame].positions[v]);
			for (int s = 0; s < mesh.surfaces.size(); ++s)
			{
				const auto &positions = mesh.surfaces[s].frames[frame].positions;
				for (int v = 0; v < positions.size(); ++v)
				{
					if (s == surfaceIndex && expanded.contains(v))
						continue;
					QPointF screen;
					if (m_viewport->projectToView(positions[v], &screen))
					{
						modal->snapScreen.append(screen);
						modal->snapWorld.append(positions[v]);
					}
				}
			}
		}
	}
	m_modal = std::move(modal);
	m_viewport->setFocus(Qt::OtherFocusReason);
	updateModalPreview();
	return true;
}

QString ModelEditorTools::modalText() const
{
	if (!m_modal)
		return {};
	const auto &m = *m_modal;
	QString name;
	switch (m.op)
	{
	case ModelModalOperator::Move:
		name = Text::tr("Move");
		break;
	case ModelModalOperator::Rotate:
		name = Text::tr("Rotate");
		break;
	case ModelModalOperator::Scale:
		name = Text::tr("Scale");
		break;
	case ModelModalOperator::Extrude:
		name = m.edges ? Text::tr("Extrude Edges") : Text::tr("Extrude Faces");
		break;
	case ModelModalOperator::Inset:
		name = m.individual ? Text::tr("Inset Individual Faces") : Text::tr("Inset Faces");
		break;
	case ModelModalOperator::ShrinkFatten:
		name = Text::tr("Shrink/Fatten");
		break;
	case ModelModalOperator::Duplicate:
		name = Text::tr("Duplicate");
		break;
	case ModelModalOperator::LoopCut:
		name = Text::tr("Loop Cut");
		break;
	case ModelModalOperator::BevelVertices:
		name = Text::tr("Bevel Vertices");
		break;
	case ModelModalOperator::None:
		break;
	}
	QString value;
	const auto &e = m.edit;
	switch (m.op)
	{
	case ModelModalOperator::Move:
	case ModelModalOperator::Duplicate:
	case ModelModalOperator::Extrude:
		value = Text::tr("D: %1, %2, %3").arg(e.translation.x, 0, 'f', 3).arg(e.translation.y, 0, 'f', 3).arg(e.translation.z, 0, 'f', 3);
		break;
	case ModelModalOperator::Rotate:
		value = Text::tr("%1°").arg(e.rotation.x + e.rotation.y + e.rotation.z, 0, 'f', 2);
		break;
	case ModelModalOperator::Scale:
		value = Text::tr("Scale %1, %2, %3").arg(e.scale.x, 0, 'f', 3).arg(e.scale.y, 0, 'f', 3).arg(e.scale.z, 0, 'f', 3);
		break;
	case ModelModalOperator::Inset:
		value = Text::tr("Thickness %1").arg(e.tool.insetThickness, 0, 'f', 3);
		break;
	case ModelModalOperator::ShrinkFatten:
		value = Text::tr("Distance %1").arg(e.tool.offset, 0, 'f', 3);
		break;
	case ModelModalOperator::BevelVertices:
		value = Text::tr("Width %1").arg(e.tool.bevelWidth, 0, 'f', 3);
		break;
	case ModelModalOperator::LoopCut:
		value = m.hoverEdge.first >= 0 ? Text::tr("%n cut(s)", nullptr, m.cuts) : Text::tr("Point at an edge between quads");
		break;
	case ModelModalOperator::None:
		break;
	}
	QString constraint;
	if (m.axis >= 0)
	{
		const QString axis = QStringLiteral("XYZ").mid(m.axis, 1);
		constraint = m.plane ? (m.local ? Text::tr("plane without local %1").arg(axis) : Text::tr("plane without %1").arg(axis))
							 : (m.local ? Text::tr("along local %1").arg(axis) : Text::tr("along %1").arg(axis));
	}
	QStringList parts{name, value};
	if (!constraint.isEmpty())
		parts << constraint;
	if (!m.typed.isEmpty())
		parts << Text::tr("typed %1").arg(m.typed);
	if (m.snapped)
		parts << Text::tr("snapped to a vertex");
	if (m.edit.kind == ModelEditKind::WeightedTransform)
		parts << Text::tr("proportional %1").arg(m_radius->value());
	auto status = parts.join(QStringLiteral("  ·  "));
	if (!m.valid && !m.error.isEmpty())
		status += QStringLiteral("\n") + m.error;
	const QString hints = m.op == ModelModalOperator::LoopCut
							  ? Text::tr("Wheel or +/- sets the cuts · click confirms · Esc cancels")
							  : Text::tr("X/Y/Z constrain (Shift: plane) · type a value · Ctrl snaps · Shift is precise · Enter or click confirms · Esc cancels");
	return status + QStringLiteral("\n") + hints;
}

void ModelEditorTools::moveModal(const QPointF &pointer, Qt::KeyboardModifiers modifiers)
{
	if (!m_modal)
		return;
	auto &m = *m_modal;
	// Shift slows the pointer to a tenth, from wherever it was pressed.
	const QPointF delta = pointer - m.lastActual;
	m.lastActual = pointer;
	m.pointer += held(modifiers, m_controls.transform.precisionModifiers) ? delta * 0.1 : delta;
	m.modifiers = modifiers;
	if (m.op == ModelModalOperator::Rotate)
	{
		const QPointF offset = m.pointer - m.pivotScreen;
		if (QLineF(m.pointer, m.pivotScreen).length() > 2)
		{
			const double current = std::atan2(-offset.y(), offset.x());
			m.angle += std::remainder(current - m.lastAngle, 2 * std::numbers::pi);
			m.lastAngle = current;
		}
	}
	if (m.op == ModelModalOperator::LoopCut)
	{
		int surface = -1;
		const auto edge = edgeNear(pointer, &surface);
		if (edge != m.hoverEdge || surface != m.hoverSurface)
		{
			m.hoverEdge = edge;
			m.hoverSurface = surface;
		}
	}
	if (!m.pending)
	{
		m.pending = true;
		QTimer::singleShot(0, this,
						   [this]()
						   {
							   if (m_modal && m_modal->pending)
							   {
								   m_modal->pending = false;
								   updateModalPreview();
							   }
						   });
	}
}

void ModelEditorTools::constrainModal(int axis, bool plane)
{
	if (!m_modal || axis < 0 || axis > 2)
		return;
	auto &m = *m_modal;
	if (m.axis == axis && m.plane == plane)
	{
		// Second press: the selection's own axes; third: free again.
		if (!m.local)
			m.local = true;
		else
			m.axis = -1;
	}
	else
	{
		m.axis = axis;
		m.plane = plane;
		m.local = false;
	}
	if (m.local)
	{
		ModelEdit probe;
		probe.kind = ModelEditKind::Transform;
		probe.selection = m.selection;
		probe.transformSpace = ModelTransformSpace::Selection;
		probe.axesFrame = std::max(0, m_editor->m_frame->currentIndex());
		QString error;
		if (!resolveModelTransformAxes(m_editor->m_document.mesh(), probe, &m.basis, &error))
		{
			m.local = false;
			m.axis = -1;
			showBanner(error);
		}
	}
	updateModalPreview();
}

void ModelEditorTools::typeModal(const QString &text)
{
	if (!m_modal)
		return;
	auto &m = *m_modal;
	if (m.op == ModelModalOperator::LoopCut)
	{
		bool ok = false;
		const int cuts = text.toInt(&ok);
		if (ok && cuts > 0)
			m.cuts = std::clamp(cuts, 1, 64);
		updateModalPreview();
		return;
	}
	if (text == QStringLiteral("\b"))
		m.typed.chop(1);
	else if (text == QStringLiteral("-"))
		m.typed = m.typed.startsWith(QLatin1Char('-')) ? m.typed.mid(1) : QStringLiteral("-") + m.typed;
	else if (text == QStringLiteral(",") || text == QStringLiteral("."))
	{
		if (!m.typed.contains(QLatin1Char('.')))
			m.typed += QLatin1Char('.');
	}
	else
	{
		for (QChar c : text)
		{
			if (c.isDigit())
				m.typed += c;
		}
	}
	if (m.typed == QStringLiteral("-"))
		m.typed = QStringLiteral("-");
	updateModalPreview();
}

void ModelEditorTools::adjustModal(int steps)
{
	if (!m_modal || steps == 0)
		return;
	if (m_modal->op == ModelModalOperator::LoopCut)
		m_modal->cuts = std::clamp(m_modal->cuts + steps, 1, 64);
	else if (m_proportional->isChecked())
		m_radius->setValue(std::max(0.01, m_radius->value() * std::pow(1.1, steps)));
	updateModalPreview();
}

void ModelEditorTools::updateModalPreview()
{
	if (!m_modal)
		return;
	auto &m = *m_modal;
	const auto &mesh = m_editor->m_document.mesh();
	const int frame = std::clamp(m_editor->m_frame->currentIndex(), 0, int(mesh.frames.size()) - 1);
	const bool snapping = m_editor->m_snapTranslation->isChecked() != held(m.modifiers, m_controls.transform.snapModifiers);
	bool typedOk = false;
	const double typed = m.typed.toDouble(&typedOk);
	const bool hasTyped = typedOk && !m.typed.isEmpty() && m.typed != QStringLiteral("-");
	const auto ray = m_viewport->viewRay(m.pointer), startRay = m_viewport->viewRay(m.start);
	const V forward = m_viewport->viewForward();
	const auto axisDirection = [&](int axis) { return m.local ? m.basis.axes[axis] : axisVector(axis); };
	// The pointer's translation from its start, under the current constraint.
	const auto translation = [&](V freeNormal, bool *ok)
	{
		*ok = true;
		if (hasTyped)
		{
			const int axis = m.axis >= 0 ? (m.plane ? (m.axis + 1) % 3 : m.axis) : 0;
			return scale(axisDirection(axis), typed);
		}
		if (m.axis >= 0 && !m.plane)
		{
			double t0 = 0, t1 = 0;
			const V direction = axisDirection(m.axis);
			if (!closestOnLine(startRay, m.pivot, direction, &t0) || !closestOnLine(ray, m.pivot, direction, &t1))
			{
				*ok = false;
				return V{};
			}
			return scale(direction, t1 - t0);
		}
		const V normal = m.axis >= 0 ? axisDirection(m.axis) : freeNormal;
		V a, b;
		if (!planeHit(startRay, m.pivot, normal, &a) || !planeHit(ray, m.pivot, normal, &b))
		{
			*ok = false;
			return V{};
		}
		return sub(b, a);
	};
	// Vertex snapping: the moving point nearest the target lands on the vertex
	// under the pointer, within any axis or plane constraint.
	m.snapped = false;
	const auto snapToVertex = [&](V *delta)
	{
		if (!snapping || !m_snapElement || m_snapElement->currentIndex() != 1 || hasTyped || m.snapWorld.isEmpty() ||
			m.selectedPoints.isEmpty())
			return false;
		int best = -1;
		double bestDistance = 24;
		for (int i = 0; i < m.snapScreen.size(); ++i)
		{
			const double d = QLineF(m.snapScreen[i], m.lastActual).length();
			if (d < bestDistance)
			{
				bestDistance = d;
				best = i;
			}
		}
		if (best < 0)
			return false;
		const V target = m.snapWorld[best];
		V base = m.selectedPoints.first();
		double closest = INFINITY;
		for (const auto &p : std::as_const(m.selectedPoints))
		{
			const double d = length(sub(p, target));
			if (d < closest)
			{
				closest = d;
				base = p;
			}
		}
		V move = sub(target, base);
		if (m.axis >= 0)
		{
			const V direction = unit(axisDirection(m.axis));
			move = m.plane ? sub(move, scale(direction, dot(move, direction))) : scale(direction, dot(move, direction));
		}
		*delta = move;
		m.snapped = true;
		m.snapPoint = m.snapScreen[best];
		return true;
	};
	ModelEdit edit;
	m.editReady = false;
	m.error.clear();
	const double wpp = worldPerPixel(m.pivot);
	const double grid = m_editor->m_translationGrid->value();
	const bool components = !m.selection.faces.isEmpty() || !m.selection.vertices.isEmpty() || !m.selection.edges.isEmpty();
	const bool weighted = components && (m_proportional->isChecked() || m_mirror[0]->isChecked() || m_mirror[1]->isChecked() || m_mirror[2]->isChecked());
	switch (m.op)
	{
	case ModelModalOperator::Move:
	case ModelModalOperator::Rotate:
	case ModelModalOperator::Scale:
	{
		const auto kind = !m.selection.collision.isEmpty() ? ModelEditKind::TransformCollisionBox
						  : !m.selection.tag.isEmpty()	   ? ModelEditKind::TransformTag
						  : weighted					   ? ModelEditKind::WeightedTransform
														   : ModelEditKind::Transform;
		edit = toolEdit(kind == ModelEditKind::WeightedTransform ? ModelEditKind::WeightedTransform : ModelEditKind::Transform);
		edit.kind = kind;
		edit.selection = m.selection;
		edit.transformSpace = ModelTransformSpace::World;
		edit.axisRotation = {};
		edit.axesFrame = -1;
		edit.pivotMode = ModelTransformPivot::Custom;
		edit.pivot = m.pivot;
		if (kind == ModelEditKind::TransformCollisionBox)
		{
			const auto box = findModelCollisionBox(mesh, edit.selection.collision);
			edit.frame = box && box->framePoses.isEmpty() ? -1 : edit.frame;
		}
		if (kind == ModelEditKind::WeightedTransform)
		{
			edit.tool = {};
			edit.tool.referenceFrame = frame;
			if (m_proportional->isChecked())
			{
				edit.tool.proportionalRadius = m_radius->value();
				edit.tool.falloff = ModelFalloff(m_falloff->currentIndex());
				edit.tool.connectedOnly = m_connected->isChecked();
			}
			edit.tool.mirrorAxes = (m_mirror[0]->isChecked() ? 1 : 0) | (m_mirror[1]->isChecked() ? 2 : 0) | (m_mirror[2]->isChecked() ? 4 : 0);
		}
		if (m.local && m.axis >= 0)
		{
			edit.transformSpace = ModelTransformSpace::Selection;
			edit.axesFrame = frame;
		}
		if (m.op == ModelModalOperator::Move)
		{
			bool ok = false;
			V delta = translation(forward, &ok);
			if (!ok)
				break;
			const bool vertexSnap = snapToVertex(&delta);
			if (edit.transformSpace == ModelTransformSpace::Selection)
				delta = modelBasisFromWorld(delta, m.basis);
			edit.translation = delta;
			if (snapping && !vertexSnap)
				edit.translationGrid = grid;
			m.editReady = length(delta) > 1e-9;
		}
		else if (m.op == ModelModalOperator::Rotate)
		{
			double degrees = hasTyped ? typed : m.angle * 180 / std::numbers::pi;
			V axis = m.axis >= 0 && !m.plane ? axisDirection(m.axis) : scale(forward, -1);
			if (!hasTyped && dot(axis, forward) > 0)
				degrees = -degrees;
			if (snapping)
				degrees = snapValue(degrees, m_editor->m_rotationGrid->value());
			if (m.axis >= 0 && !m.plane && !m.local)
				(m.axis == 0 ? edit.rotation.x : m.axis == 1 ? edit.rotation.y : edit.rotation.z) = float(degrees);
			else if (m.axis >= 0 && !m.plane && m.local)
				(m.axis == 0 ? edit.rotation.x : m.axis == 1 ? edit.rotation.y : edit.rotation.z) = float(degrees);
			else
			{
				edit.transformSpace = ModelTransformSpace::Custom;
				edit.axesFrame = -1;
				edit.axisRotation = axisRotationFor(axis);
				edit.rotation = {float(degrees), 0, 0};
			}
			m.editReady = std::abs(degrees) > 1e-9;
		}
		else
		{
			double factor = hasTyped ? typed : QLineF(m.pointer, m.pivotScreen).length() / m.startDistance;
			if (snapping)
				factor = 1 + snapValue(factor - 1, m_editor->m_scaleGrid->value());
			factor = std::max(factor, 0.0001);
			V factors{float(factor), float(factor), float(factor)};
			if (m.axis >= 0)
			{
				for (int a = 0; a < 3; ++a)
				{
					if ((a == m.axis) == m.plane)
						(a == 0 ? factors.x : a == 1 ? factors.y : factors.z) = 1;
				}
			}
			edit.scale = factors;
			m.editReady = std::abs(factor - 1) > 1e-9;
		}
		break;
	}
	case ModelModalOperator::Extrude:
	case ModelModalOperator::Duplicate:
	{
		const auto kind = m.op == ModelModalOperator::Duplicate ? ModelEditKind::DuplicateFaces
						  : m.edges							 ? ModelEditKind::ExtrudeEdges
															 : ModelEditKind::Extrude;
		edit = toolEdit(kind);
		edit.selection = m.selection;
		edit.transformSpace = ModelTransformSpace::World;
		edit.axisRotation = {};
		edit.axesFrame = -1;
		V delta{};
		if (m.op == ModelModalOperator::Extrude && !m.edges && m.axis < 0)
		{
			double distance = 0;
			if (hasTyped)
				distance = typed;
			else
			{
				double t0 = 0, t1 = 0;
				if (!closestOnLine(startRay, m.pivot, m.normal, &t0) || !closestOnLine(ray, m.pivot, m.normal, &t1))
					break;
				distance = t1 - t0;
			}
			if (snapping)
				distance = snapValue(distance, grid);
			delta = scale(m.normal, distance);
		}
		else
		{
			bool ok = false;
			delta = translation(forward, &ok);
			if (!ok)
				break;
			if (!snapToVertex(&delta) && snapping)
				delta = {float(snapValue(delta.x, grid)), float(snapValue(delta.y, grid)), float(snapValue(delta.z, grid))};
		}
		edit.translation = delta;
		m.editReady = length(delta) > 1e-6;
		break;
	}
	case ModelModalOperator::Inset:
	{
		edit = toolEdit(ModelEditKind::InsetFaces);
		edit.selection = m.selection;
		double thickness = hasTyped ? typed : (m.startDistance - QLineF(m.pointer, m.pivotScreen).length()) * wpp;
		if (snapping)
			thickness = snapValue(thickness, grid);
		edit.tool.insetThickness = std::max(0.0, thickness);
		edit.tool.inset = m.individual ? ModelInsetMode::Individual : ModelInsetMode::Region;
		m.editReady = edit.tool.insetThickness > 1e-6;
		if (!m.editReady && !hasTyped)
			m.error = Text::tr("Move the pointer toward the selection's centre to inset.");
		break;
	}
	case ModelModalOperator::ShrinkFatten:
	{
		edit = toolEdit(ModelEditKind::ShrinkFatten);
		edit.selection = m.selection;
		double distance = hasTyped ? typed : (QLineF(m.pointer, m.pivotScreen).length() - m.startDistance) * wpp;
		if (snapping)
			distance = snapValue(distance, grid);
		edit.tool.offset = distance;
		m.editReady = std::abs(distance) > 1e-9;
		break;
	}
	case ModelModalOperator::BevelVertices:
	{
		edit = toolEdit(ModelEditKind::BevelVertices);
		edit.selection = m.selection;
		double width = hasTyped ? typed : (QLineF(m.pointer, m.pivotScreen).length() - m.startDistance) * wpp;
		if (snapping)
			width = snapValue(width, grid);
		edit.tool.bevelWidth = std::max(0.0, width);
		m.editReady = edit.tool.bevelWidth > 1e-6;
		if (!m.editReady && !hasTyped)
			m.error = Text::tr("Move the pointer away from the selection to widen the bevel.");
		break;
	}
	case ModelModalOperator::LoopCut:
	{
		m.guides.clear();
		edit = toolEdit(ModelEditKind::LoopCut);
		if (m.hoverEdge.first < 0 || m.hoverSurface < 0 || m.hoverSurface >= mesh.surfaces.size())
			break;
		edit.selection = {m.hoverSurface, {}, {}, {m.hoverEdge}};
		edit.tool.cuts = m.cuts;
		// Preview guides across each quad of the ring.
		const auto &surface = mesh.surfaces[m.hoverSurface];
		ModelQuadTopology quads;
		if (buildModelQuadTopology(surface, frame, edit.tool.maxAngle, &quads))
		{
			QVector<int> elements;
			const auto geometric = quads.geometry.geometricEdge(m.hoverEdge.first, m.hoverEdge.second);
			const auto ring = modelEdgeRing(quads, geometric, nullptr, &elements);
			const QSet<ModelEdge> ringEdges(ring.cbegin(), ring.cend());
			const auto &p = surface.frames[frame].positions;
			for (int element : std::as_const(elements))
			{
				const auto &c = quads.elements[element].corners;
				for (int side : {0, 1})
				{
					if (!ringEdges.contains(quads.geometry.geometricEdge(c[side], c[side + 1])) ||
						!ringEdges.contains(quads.geometry.geometricEdge(c[side + 2], c[(side + 3) % 4])))
						continue;
					// The opposite side runs backwards, so its points pair with this side's.
					for (int k = 1; k <= m.cuts; ++k)
					{
						const double s = double(k) / (m.cuts + 1);
						const V a = add(p[c[side]], scale(sub(p[c[side + 1]], p[c[side]]), s));
						const V b = add(p[c[(side + 3) % 4]], scale(sub(p[c[side + 2]], p[c[(side + 3) % 4]]), s));
						QPointF sa, sb;
						if (m_viewport->projectToView(a, &sa) && m_viewport->projectToView(b, &sb))
							m.guides.append(QLineF(sa, sb));
					}
				}
			}
		}
		m.editReady = !m.guides.isEmpty();
		if (!m.editReady)
			m.error = Text::tr("No quads run across this edge.");
		break;
	}
	case ModelModalOperator::None:
		break;
	}
	m.edit = edit;
	m.valid = false;
	if (m.op == ModelModalOperator::LoopCut)
	{
		// Loop cuts preview as guides; the mesh changes on confirmation.
		m.valid = m.editReady;
		if (m.previewShown)
			restoreDocumentView();
		m.previewShown = false;
	}
	else if (m.editReady)
	{
		auto copy = mesh;
		ModelSelection resulting;
		QString error;
		if (applyModelEdit(&copy, edit, &resulting, &error))
		{
			m.valid = true;
			m.preview = std::move(copy);
			m.previewSelection = resulting;
			showPreview(m.preview, m.previewSelection);
			m.previewShown = true;
		}
		else
			m.error = error;
	}
	else if (m.previewShown)
	{
		restoreDocumentView();
		m.previewShown = false;
	}
	showBanner(modalText());
	m_viewport->update();
}

void ModelEditorTools::showPreview(const ModelMesh &mesh, const ModelSelection &selection)
{
	m_viewport->setMesh(displayMesh(mesh), true);
	int base = 0;
	for (int s = 0; s < selection.surface && s < mesh.surfaces.size(); ++s)
		base += mesh.surfaces[s].triangles.size();
	QVector<int> highlighted;
	QSet<int> vertices = selection.vertices;
	if (selection.surface >= 0 && selection.surface < mesh.surfaces.size())
	{
		const auto &surface = mesh.surfaces[selection.surface];
		for (int face : selection.faces)
		{
			if (face < 0 || face >= surface.triangles.size())
				continue;
			highlighted.append(base + face);
			const auto &t = surface.triangles[face];
			vertices << t.a << t.b << t.c;
		}
		for (auto edge : selection.edges)
			vertices << edge.first << edge.second;
	}
	m_viewport->setHighlightedTriangles(highlighted);
	m_viewport->setHighlightedEdges(selection.surface, selection.edges);
	if (!selection.surfaces.isEmpty())
		m_viewport->setEditSurfaces(selection.surface, selection.surfaces);
	else
		m_viewport->setEditSelection(selection.surface, vertices);
}

void ModelEditorTools::restoreDocumentView()
{
	m_editor->m_presentedMeshRevision.clear();
	m_editor->m_highlightedRevision.clear();
	m_editor->refresh();
}

bool ModelEditorTools::finishModal(bool commit, QString *error)
{
	if (!m_modal)
		return false;
	auto modal = std::move(m_modal);
	m_modal.reset();
	m_viewport->update();
	if (!commit || !modal->valid || !modal->editReady)
	{
		if (modal->previewShown)
			restoreDocumentView();
		showBanner(commit && !modal->error.isEmpty() ? modal->error : QString());
		if (error && commit)
			*error = modal->error.isEmpty() ? Text::tr("Nothing to apply.") : modal->error;
		return false;
	}
	QString failure;
	const bool applied = runTool(modal->edit, &failure);
	if (!applied)
	{
		restoreDocumentView();
		if (error)
			*error = failure;
		return false;
	}
	if (modal->op == ModelModalOperator::LoopCut)
		setSelectionMode(2);
	return true;
}

bool ModelEditorTools::eventFilter(QObject *watched, QEvent *event)
{
	if (watched != m_viewport || m_forwarding)
		return false;
	switch (event->type())
	{
	case QEvent::Resize:
		placeOverlays();
		return false;
	case QEvent::ShortcutOverride:
		if (m_modal || m_circleMode || m_regionArmed)
		{
			event->accept();
			return true;
		}
		return false;
	case QEvent::KeyPress:
		return handleKey(static_cast<QKeyEvent *>(event));
	case QEvent::KeyRelease:
		if (m_modal)
		{
			const auto *key = static_cast<QKeyEvent *>(event);
			if (key->key() == Qt::Key_Shift || key->key() == Qt::Key_Control)
				moveModal(m_modal->lastActual, key->modifiers());
			return true;
		}
		return false;
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick:
	case QEvent::MouseMove:
		return handleMouse(static_cast<QMouseEvent *>(event));
	case QEvent::Wheel:
	{
		const auto *wheel = static_cast<QWheelEvent *>(event);
		const int steps = wheel->angleDelta().y() > 0 ? 1 : wheel->angleDelta().y() < 0 ? -1 : 0;
		if (m_modal)
		{
			adjustModal(steps);
			return true;
		}
		if (m_circleMode)
		{
			m_circleRadius = std::clamp(m_circleRadius * std::pow(1.15, steps), 4.0, 400.0);
			m_viewport->update();
			return true;
		}
		return false;
	}
	case QEvent::FocusOut:
		if (m_modal)
			finishModal(false);
		return false;
	default:
		return false;
	}
}

bool ModelEditorTools::handleKey(QKeyEvent *event)
{
	if (m_modal)
		return handleModalKey(event);
	if (m_circleMode)
	{
		if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_C)
		{
			m_circleMode = false;
			showBanner({});
			m_viewport->update();
		}
		return true;
	}
	if (m_regionArmed && event->key() == Qt::Key_Escape)
	{
		m_regionArmed = false;
		m_regionDragging = false;
		m_region = Region::None;
		showBanner({});
		m_viewport->update();
		return true;
	}
	return false;
}

bool ModelEditorTools::handleModalKey(QKeyEvent *event)
{
	const int key = event->key();
	const auto modifiers = event->modifiers();
	if (key == Qt::Key_Escape)
		finishModal(false);
	else if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space)
		finishModal(true);
	else if (key == Qt::Key_X || key == Qt::Key_Y || key == Qt::Key_Z)
		constrainModal(key - Qt::Key_X, modifiers & Qt::ShiftModifier);
	else if (key == Qt::Key_Backspace)
		typeModal(QStringLiteral("\b"));
	else if (key == Qt::Key_Minus && m_modal->op != ModelModalOperator::LoopCut)
		typeModal(QStringLiteral("-"));
	else if ((key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_PageUp) && m_modal)
		adjustModal(1);
	else if ((key == Qt::Key_Minus || key == Qt::Key_PageDown) && m_modal)
		adjustModal(-1);
	else if (key == Qt::Key_Period || key == Qt::Key_Comma)
		typeModal(QStringLiteral("."));
	else if (key >= Qt::Key_0 && key <= Qt::Key_9)
		typeModal(QString(QChar('0' + (key - Qt::Key_0))));
	else if (key == Qt::Key_I && m_modal->op == ModelModalOperator::Inset)
	{
		m_modal->individual = !m_modal->individual;
		updateModalPreview();
	}
	else if (key == Qt::Key_O && m_modal->op != ModelModalOperator::LoopCut)
	{
		m_proportional->toggle();
		updateModalPreview();
	}
	else if (key == Qt::Key_Shift || key == Qt::Key_Control)
		moveModal(m_modal->lastActual, modifiers);
	return true;
}

void ModelEditorTools::forwardClick(const QMouseEvent &event, Qt::KeyboardModifiers modifiers)
{
	m_forwarding = true;
	const QPointF point = m_pressPoint;
	const QPointF global = m_viewport->mapToGlobal(point);
	QMouseEvent press(QEvent::MouseButtonPress, point, global, event.button(), event.button(), modifiers);
	QCoreApplication::sendEvent(m_viewport, &press);
	QMouseEvent release(QEvent::MouseButtonRelease, point, global, event.button(), Qt::NoButton, modifiers);
	QCoreApplication::sendEvent(m_viewport, &release);
	m_forwarding = false;
}

bool ModelEditorTools::handleMouse(QMouseEvent *event)
{
	const QPointF point = event->position();
	const auto keys = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
	const ModelSelectionControls &selection = m_controls.selection;
	const ModelTransformControls &transform = m_controls.transform;
	m_pointer = point;
	if (m_modal)
	{
		if (event->type() == QEvent::MouseMove)
			moveModal(point, event->modifiers());
		else if (event->type() == QEvent::MouseButtonRelease && m_toolDragging && event->button() == Qt::LeftButton)
		{
			// A tool drag commits on release, as 3ds Max and MilkShape do.
			m_toolDragging = false;
			moveModal(point, event->modifiers());
			updateModalPreview();
			finishModal(true);
		}
		else if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::LeftButton && !m_toolDragging)
		{
			moveModal(point, event->modifiers());
			updateModalPreview();
			finishModal(true);
			// The confirming click's release must not select anything.
			m_swallowRelease = true;
			m_regionArmed = false;
		}
		else if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::RightButton)
		{
			m_toolDragging = false;
			finishModal(false);
			m_swallowRelease = true;
		}
		return true;
	}
	if (m_swallowRelease && event->type() == QEvent::MouseButtonRelease)
	{
		m_swallowRelease = false;
		return true;
	}
	if (m_circleMode)
	{
		if (event->type() == QEvent::MouseButtonPress && event->button() == Qt::RightButton)
		{
			m_circleMode = false;
			showBanner({});
		}
		else if ((event->buttons() & (Qt::LeftButton | Qt::MiddleButton)) &&
				 (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress))
			selectCircle(point, m_circleRadius, (keys & Qt::ShiftModifier) || (event->buttons() & Qt::MiddleButton));
		m_viewport->update();
		return true;
	}
	switch (event->type())
	{
	case QEvent::MouseButtonPress:
	{
		// Drag zoom belongs to the profile, whichever button it uses.
		if (event->button() == m_controls.navigation.zoomButton && keys == m_controls.navigation.zoomModifiers &&
			m_controls.navigation.zoomButton != Qt::NoButton)
		{
			m_zoomDragging = true;
			m_zoomLast = point;
			m_pressButton = event->button();
			return true;
		}
		if (event->button() == Qt::LeftButton)
		{
			bool positive = true;
			const int axis = m_showGizmo ? gizmoAxisAt(point, &positive) : -1;
			if (axis >= 0)
			{
				alignView(axis, positive);
				return true;
			}
			// Transform handles keep their own drag.
			if (m_viewport->transformGizmoAt(point) >= 0 && keys == Qt::NoModifier)
				return false;
			// Navigation drags the profile puts on the left button (MilkShape's
			// 3D view rotation, Ctrl+left pan) belong to the view.
			if (cameraNavigationDrag(m_viewport->cameraControls(), Qt::LeftButton, keys) != CameraNavigationDrag::None &&
				!(keys == Qt::AltModifier && m_controls.navigation.emulateMiddleButton))
				return false;
			// A lasting tool drags the selection it starts on.
			const bool toolDrag = transform.style == ModelTransformStyle::ToolMode && m_lastingTool != LastingTool::None &&
								  transform.dragSelectionTransforms &&
								  (keys == Qt::NoModifier ||
								   (m_lastingTool == LastingTool::Move && transform.duplicateDragModifiers != Qt::NoModifier &&
									keys == transform.duplicateDragModifiers)) &&
								  pointerOnSelection(point);
			m_pressPoint = point;
			m_pressButton = Qt::LeftButton;
			m_pressModifiers = keys;
			m_regionDragging = false;
			m_toolPressed = toolDrag;
			return true;
		}
		if (event->button() == selection.subtractDragButton && selection.subtractDragButton != Qt::NoButton &&
			keys == selection.subtractDragModifiers)
		{
			m_pressPoint = point;
			m_pressButton = event->button();
			m_pressModifiers = keys;
			m_subtractDragging = true;
			m_regionDragging = false;
			return true;
		}
		if (event->button() == selection.cursorButton && selection.cursorButton != Qt::NoButton && keys == selection.cursorModifiers &&
			selection.cursorModifiers != Qt::NoModifier)
		{
			placeCursor(point);
			m_pressButton = event->button();
			m_pressModifiers = keys;
			m_pressPoint = point;
			return true;
		}
		if (event->button() == selection.lassoButton && selection.lassoButton != Qt::NoButton && selection.lassoModifiers != Qt::NoModifier &&
			held(keys, selection.lassoModifiers))
		{
			m_pressPoint = point;
			m_pressButton = event->button();
			m_pressModifiers = keys;
			m_lasso = {point};
			m_region = Region::Lasso;
			return true;
		}
		return false;
	}
	case QEvent::MouseMove:
	{
		if (m_zoomDragging)
		{
			// Up zooms in: one notch per 24 pixels.
			const double notches = (m_zoomLast.y() - point.y()) / 24.0;
			if (std::abs(notches) > 1e-6)
				zoomBy(notches, m_pressPoint.isNull() ? point : m_zoomLast);
			m_zoomLast = point;
			return true;
		}
		if (m_pressButton == Qt::LeftButton && (event->buttons() & Qt::LeftButton))
		{
			if (!m_regionDragging && QLineF(point, m_pressPoint).length() >= 4)
			{
				if (m_toolPressed)
				{
					m_toolPressed = false;
					m_pressButton = Qt::NoButton;
					beginToolDrag(point, m_pressModifiers != Qt::NoModifier);
					return true;
				}
				if ((m_pressModifiers & Qt::AltModifier) && !m_regionArmed && m_controls.navigation.emulateMiddleButton)
				{
					// Alt+drag orbits, emulating a middle button (Blender's three-button emulation).
					m_regionDragging = false;
					m_pressButton = Qt::MiddleButton;
					m_forwarding = true;
					QMouseEvent press(QEvent::MouseButtonPress, m_pressPoint, m_viewport->mapToGlobal(m_pressPoint), Qt::MiddleButton,
									  Qt::MiddleButton, Qt::NoModifier);
					QCoreApplication::sendEvent(m_viewport, &press);
					m_forwarding = false;
				}
				else if (selection.emptyDragBoxSelects || m_regionArmed)
				{
					m_regionDragging = true;
					m_region = Region::Box;
				}
				else
				{
					// Without box selection a plain drag belongs to the view.
					m_pressButton = Qt::NoButton;
					m_forwarding = true;
					QMouseEvent press(QEvent::MouseButtonPress, m_pressPoint, m_viewport->mapToGlobal(m_pressPoint), Qt::LeftButton,
									  Qt::LeftButton, m_pressModifiers);
					QCoreApplication::sendEvent(m_viewport, &press);
					m_forwarding = false;
					return false;
				}
			}
			m_viewport->update();
			return true;
		}
		if (m_subtractDragging && (event->buttons() & m_pressButton))
		{
			if (!m_regionDragging && QLineF(point, m_pressPoint).length() >= 4)
			{
				m_regionDragging = true;
				m_region = Region::Box;
			}
			m_viewport->update();
			return true;
		}
		if (m_pressButton == Qt::MiddleButton && (event->buttons() & Qt::LeftButton))
		{
			m_forwarding = true;
			QMouseEvent move(QEvent::MouseMove, point, event->globalPosition(), Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
			QCoreApplication::sendEvent(m_viewport, &move);
			m_forwarding = false;
			return true;
		}
		if (m_pressButton == selection.lassoButton && m_region == Region::Lasso && (event->buttons() & selection.lassoButton))
		{
			if (m_lasso.isEmpty() || QLineF(m_lasso.last(), point).length() > 3)
				m_lasso << point;
			m_viewport->update();
			return true;
		}
		if (m_circleMode || m_modal || m_showGizmo)
			m_viewport->update();
		return false;
	}
	case QEvent::MouseButtonRelease:
	{
		if (m_zoomDragging && event->button() == m_pressButton)
		{
			m_zoomDragging = false;
			m_pressButton = Qt::NoButton;
			return true;
		}
		if (m_pressButton == Qt::MiddleButton && event->button() == Qt::LeftButton)
		{
			m_forwarding = true;
			QMouseEvent release(QEvent::MouseButtonRelease, point, event->globalPosition(), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
			QCoreApplication::sendEvent(m_viewport, &release);
			m_forwarding = false;
			m_pressButton = Qt::NoButton;
			return true;
		}
		if (m_subtractDragging && event->button() == m_pressButton)
		{
			m_subtractDragging = false;
			m_pressButton = Qt::NoButton;
			if (m_regionDragging)
			{
				m_regionDragging = false;
				m_region = Region::None;
				selectRegion(QPolygonF(QRectF(m_pressPoint, point).normalized()), false, true);
				m_viewport->update();
			}
			return true;
		}
		if (m_pressButton == Qt::LeftButton && event->button() == Qt::LeftButton)
		{
			m_pressButton = Qt::NoButton;
			m_toolPressed = false;
			const auto modifiers = m_pressModifiers;
			const bool extend = held(modifiers, selection.extendModifiers);
			const bool subtract = held(modifiers, selection.subtractModifiers);
			if (m_regionDragging)
			{
				m_regionDragging = false;
				m_regionArmed = false;
				m_region = Region::None;
				const QRectF rect = QRectF(m_pressPoint, point).normalized();
				selectRegion(QPolygonF(rect), extend && !subtract, subtract);
				showBanner({});
				m_viewport->update();
				return true;
			}
			if (m_regionArmed)
			{
				m_regionArmed = false;
				m_region = Region::None;
				showBanner({});
			}
			if (selection.ringModifiers != Qt::NoModifier && modifiers == selection.ringModifiers)
				pickLoop(point, true, false);
			else if (selection.loopModifiers != Qt::NoModifier && held(modifiers, selection.loopModifiers) &&
					 (modifiers & ~(selection.loopModifiers | selection.extendModifiers)) == Qt::NoModifier)
				pickLoop(point, false, extend);
			else if (selection.pathModifiers != Qt::NoModifier && modifiers == selection.pathModifiers)
				pickPath(point);
			else if (m_borderMode && selectionMode() == 2)
				pickLoop(point, false, extend);
			else if (m_elementMode && selectionMode() == 0)
			{
				if (!extend && !subtract)
					applySelection({m_editor->m_document.selection().surface, {}, {}});
				pickLinked(point, subtract);
			}
			else
				clickSelect(*event, modifiers);
			return true;
		}
		if (m_pressButton != Qt::NoButton && m_pressButton == selection.lassoButton && event->button() == selection.lassoButton)
		{
			m_pressButton = Qt::NoButton;
			if (m_region == Region::Lasso)
			{
				m_region = Region::None;
				if (m_lasso.size() >= 3)
					selectRegion(m_lasso, true, held(m_pressModifiers, Qt::ShiftModifier));
				m_lasso.clear();
				m_viewport->update();
			}
			return true;
		}
		if (m_pressButton != Qt::NoButton && m_pressButton == selection.cursorButton && event->button() == selection.cursorButton)
		{
			m_pressButton = Qt::NoButton;
			return true;
		}
		return false;
	}
	case QEvent::MouseButtonDblClick:
		if (event->button() == Qt::LeftButton && selection.doubleClickSelectsLoop && selectionMode() == 2)
		{
			// 3ds Max: a double-click on an edge selects its loop.
			pickLoop(point, false, held(keys, selection.extendModifiers));
			m_swallowRelease = true;
			return true;
		}
		return m_pressButton != Qt::NoButton;
	default:
		return false;
	}
}

QPointF ModelEditorTools::gizmoCentre() const
{
	return QPointF(m_viewport->width() - 52, 104);
}

int ModelEditorTools::gizmoAxisAt(const QPointF &point, bool *positive)
{
	if (!m_viewport->hasMesh())
		return -1;
	const auto forward = m_viewport->viewForward();
	const QPointF centre = gizmoCentre();
	QPointF origin;
	if (!m_viewport->projectToView(m_cursor, &origin))
		return -1;
	int best = -1;
	double bestDepth = 1e9;
	for (int axis = 0; axis < 3; ++axis)
	{
		QPointF tip;
		if (!m_viewport->projectToView(add(m_cursor, scale(axisVector(axis), worldPerPixel(m_cursor) * 40)), &tip))
			continue;
		QPointF direction = tip - origin;
		const double l = std::hypot(direction.x(), direction.y());
		direction = l > 1e-6 ? direction / 40.0 : QPointF();
		for (bool sign : {true, false})
		{
			const QPointF bubble = centre + direction * 34 * (sign ? 1 : -1);
			if (QLineF(bubble, point).length() <= 10)
			{
				const double depth = dot(axisVector(axis), forward) * (sign ? 1 : -1);
				if (depth < bestDepth)
				{
					bestDepth = depth;
					best = axis;
					*positive = sign;
				}
			}
		}
	}
	return best;
}

void ModelEditorTools::paintOverlay(QPainter &painter)
{
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QColor accent = m_viewport->palette().highlight().color();
	// Region selection gestures.
	if (m_regionDragging && m_pressButton == Qt::LeftButton)
	{
		QPen pen(accent, 1.5, Qt::DashLine);
		painter.setPen(pen);
		QColor fill = accent;
		fill.setAlpha(36);
		painter.setBrush(fill);
		painter.drawRect(QRectF(m_pressPoint, m_pointer).normalized());
	}
	if (m_region == Region::Lasso && m_lasso.size() > 1)
	{
		painter.setPen(QPen(accent, 1.5, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawPolyline(m_lasso);
	}
	if (m_circleMode)
	{
		painter.setPen(QPen(accent, 1.5, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawEllipse(m_pointer, m_circleRadius, m_circleRadius);
	}
	// The 3D cursor: a red and white ring with crosshairs.
	QPointF cursor;
	if (m_viewport->hasMesh() && m_viewport->projectToView(m_cursor, &cursor))
	{
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(Qt::white, 2));
		painter.drawEllipse(cursor, 9, 9);
		painter.setPen(QPen(QColor(220, 60, 60), 2, Qt::DashLine));
		painter.drawEllipse(cursor, 9, 9);
		painter.setPen(QPen(Qt::black, 1));
		painter.drawLine(cursor + QPointF(-15, 0), cursor + QPointF(-5, 0));
		painter.drawLine(cursor + QPointF(5, 0), cursor + QPointF(15, 0));
		painter.drawLine(cursor + QPointF(0, -15), cursor + QPointF(0, -5));
		painter.drawLine(cursor + QPointF(0, 5), cursor + QPointF(0, 15));
	}
	if (m_modal)
	{
		const auto &m = *m_modal;
		if (m.op == ModelModalOperator::LoopCut)
		{
			painter.setPen(QPen(QColor(250, 210, 80), 2));
			for (const auto &line : m.guides)
				painter.drawLine(line);
		}
		else
		{
			if (m.op != ModelModalOperator::Move && m.op != ModelModalOperator::Duplicate && m.op != ModelModalOperator::Extrude)
			{
				painter.setPen(QPen(m_viewport->palette().text().color(), 1, Qt::DashLine));
				painter.drawLine(m.pivotScreen, m.pointer);
			}
			if (m.snapped)
			{
				painter.setPen(QPen(m_viewport->palette().highlight().color(), 2));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(m.snapPoint, 7, 7);
				painter.drawLine(m.snapPoint + QPointF(-10, 0), m.snapPoint + QPointF(10, 0));
				painter.drawLine(m.snapPoint + QPointF(0, -10), m.snapPoint + QPointF(0, 10));
			}
			if (m.axis >= 0)
			{
				const V direction = m.local ? m.basis.axes[m.axis] : axisVector(m.axis);
				QPointF a, b;
				const double reach = worldPerPixel(m.pivot) * 4000;
				if (m_viewport->projectToView(add(m.pivot, scale(direction, -reach)), &a) &&
					m_viewport->projectToView(add(m.pivot, scale(direction, reach)), &b))
				{
					QColor colour = axisColours[m.axis];
					painter.setPen(QPen(colour, m.plane ? 1 : 2, m.plane ? Qt::DotLine : Qt::SolidLine));
					painter.drawLine(a, b);
				}
			}
			if (m.edit.kind == ModelEditKind::WeightedTransform && m.edit.tool.proportionalRadius > 0)
			{
				const double pixels = m.edit.tool.proportionalRadius / worldPerPixel(m.pivot);
				painter.setPen(QPen(m_viewport->palette().text().color(), 1, Qt::DotLine));
				painter.setBrush(Qt::NoBrush);
				painter.drawEllipse(m.pivotScreen, pixels, pixels);
			}
		}
	}
	// Navigation gizmo: click an axis to look along it.
	if (m_showGizmo && m_viewport->hasMesh())
	{
		const auto forward = m_viewport->viewForward();
		const QPointF centre = gizmoCentre();
		QPointF origin;
		if (m_viewport->projectToView(m_cursor, &origin))
		{
			struct Bubble
			{
				int axis;
				bool positive;
				QPointF at;
				double depth;
			};
			QVector<Bubble> bubbles;
			for (int axis = 0; axis < 3; ++axis)
			{
				QPointF tip;
				if (!m_viewport->projectToView(add(m_cursor, scale(axisVector(axis), worldPerPixel(m_cursor) * 40)), &tip))
					continue;
				const QPointF direction = (tip - origin) / 40.0;
				for (bool positive : {true, false})
					bubbles.append({axis, positive, centre + direction * 34 * (positive ? 1 : -1),
									dot(axisVector(axis), forward) * (positive ? 1 : -1)});
			}
			std::sort(bubbles.begin(), bubbles.end(), [](const Bubble &a, const Bubble &b) { return a.depth > b.depth; });
			QColor backdrop = m_viewport->palette().window().color();
			backdrop.setAlpha(90);
			painter.setPen(Qt::NoPen);
			painter.setBrush(backdrop);
			painter.drawEllipse(centre, 48, 48);
			for (const auto &bubble : bubbles)
			{
				QColor colour = axisColours[bubble.axis];
				if (bubble.positive)
				{
					painter.setPen(QPen(colour, 2));
					painter.drawLine(centre, bubble.at);
					painter.setPen(Qt::NoPen);
					painter.setBrush(colour);
					painter.drawEllipse(bubble.at, 9, 9);
					painter.setPen(Qt::black);
					QFont font = painter.font();
					font.setBold(true);
					font.setPixelSize(11);
					painter.setFont(font);
					painter.drawText(QRectF(bubble.at - QPointF(9, 9), QSizeF(18, 18)), Qt::AlignCenter, QStringLiteral("XYZ").mid(bubble.axis, 1));
				}
				else
				{
					colour.setAlpha(150);
					painter.setPen(QPen(colour, 1.5));
					painter.setBrush(colour.darker(160));
					painter.drawEllipse(bubble.at, 7, 7);
				}
			}
		}
	}
}
} // namespace vibestudio
