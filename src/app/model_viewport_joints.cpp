#include "app/model_viewport_p.h"
#include "core/model_joint_tools.h"
#include "core/model_skeleton.h"
#include "core/model_tags.h"

#include <QPainter>
#include <limits>

namespace vibestudio
{
namespace
{
using namespace model_viewport_detail;
QPointF missing()
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	return {nan, nan};
}
bool finite(QPointF point)
{
	return std::isfinite(point.x()) && std::isfinite(point.y());
}
QPointF project(const ModelViewport::RasterWork &work, ModelVec3 point)
{
	if (!work.perspective)
		return work.projectPoint(point, nullptr);
	double x, y, z;
	work.toView(point, &x, &y, &z);
	return z >= kNearPlane ? work.fromView(x, y, z) : missing();
}
bool visible(const ModelViewport::RasterWork &work, ModelVec3 point, QPointF screen)
{
	if (!finite(screen) || !QRectF(QPointF{}, work.logicalSize).contains(screen))
		return false;
	if (work.xrayJoints || work.wireframe)
		return true;
	if (!work.geometry || work.result.source.size() != qint64(work.size.width()) * work.size.height())
		return false;
	const int x = std::clamp(int(std::floor(screen.x() * work.size.width() / work.logicalSize.width())), 0, work.size.width() - 1);
	const int y = std::clamp(int(std::floor(screen.y() * work.size.height() / work.logicalSize.height())), 0, work.size.height() - 1);
	const int face = work.result.source[y * work.size.width() + x];
	if (face < 0)
		return true;
	if (face >= work.geometry->planes.size())
		return false;
	const auto plane = work.geometry->planes[face];
	const ModelVec3 normal{plane[0], plane[1], plane[2]};
	// The ID buffer identifies the front surface at this pixel. Intersect its
	// model-space plane with the joint's view ray; no OS depth/screen readback.
	const auto direction = work.perspective ? subtract(point, work.camera.position) : work.camera.eye;
	const double denominator = dot(normal, direction);
	if (std::abs(denominator) < 1e-12)
		return true;
	const double epsilon = std::max(1e-5, work.geometry->radius * 1e-6);
	if (work.perspective)
	{
		const double t = (plane[3] - dot(normal, work.camera.position)) / denominator;
		return t <= 0 || t >= 1 - epsilon / std::max(epsilon, std::sqrt(dot(direction, direction)));
	}
	return (plane[3] - dot(normal, point)) / denominator <= epsilon;
}
double segmentDistance(QPointF point, const QLineF &line)
{
	const auto direction = line.p2() - line.p1();
	const double length = QPointF::dotProduct(direction, direction);
	const double t = length > 0 ? std::clamp(QPointF::dotProduct(point - line.p1(), direction) / length, 0., 1.) : 0;
	return QLineF(point, line.p1() + direction * t).length();
}
} // namespace

void ModelViewport::setShowJoints(bool show)
{
	if (m_showJoints == show)
		return;
	finishEditTransform(false);
	m_showJoints = show;
	invalidateRaster();
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setJointPicking(bool enabled)
{
	if (m_jointPicking == enabled)
		return;
	finishEditTransform(false);
	m_jointPicking = enabled;
	if (enabled)
		setShowJoints(true);
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setXrayJoints(bool enabled)
{
	if (m_xrayJoints == enabled)
		return;
	finishEditTransform(false);
	m_xrayJoints = enabled;
	invalidateRaster();
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setEditJoint(const QString &name)
{
	const auto valid = modelJointIndex(m_mesh.skeleton, name, Qt::CaseSensitive) < 0 ? QString{} : name;
	if (m_editJoint == valid)
		return;
	finishEditTransform(false);
	m_editJoint = valid;
	m_jointPreviewError.clear();
	if (!valid.isEmpty())
	{
		m_editVertices.clear();
		m_editSurfaces.clear();
		m_editTag.clear();
		m_editCollision.clear();
		invalidateVertexOverlay();
	}
	invalidateRaster();
	setAccessibleDescription(accessibleSummary());
	update();
}
void ModelViewport::setJointDescendants(bool enabled)
{
	if (m_jointDescendants == enabled)
		return;
	finishEditTransform(false);
	m_jointDescendants = enabled;
	invalidateRaster();
	setAccessibleDescription(accessibleSummary());
	update();
}
bool ModelViewport::jointPose(const QString &name, ModelJointMatrix *result) const
{
	const int index = modelJointIndex(m_mesh.skeleton, name, Qt::CaseSensitive);
	ModelJointFrame frame;
	if (!result || index < 0 || !modelJointFrame(m_mesh, m_frame, &frame))
		return false;
	if ((m_editMoveActive || m_mirroredJointPreview) && !m_editJoint.isEmpty())
	{
		ModelJointMatrix delta;
		if (!modelJointTransformDelta(m_editTransform, &delta) ||
			!applyModelJointDelta(&frame.pose, m_mesh.skeleton, modelJointIndex(m_mesh.skeleton, m_editJoint, Qt::CaseSensitive), delta,
								  m_jointDescendants))
			return false;
	}
	*result = frame.pose[index];
	return true;
}
QPointF ModelViewport::jointScreenPosition(const QString &name) const
{
	if (!m_showJoints || m_raster.image.isNull())
		return missing();
	for (const auto &joint : m_rasterJoints)
		if (joint.name == name)
			return {joint.centre.x() * width() / std::max(1, m_rasterLogicalSize.width()),
					joint.centre.y() * height() / std::max(1, m_rasterLogicalSize.height())};
	return missing();
}
bool ModelViewport::jointVisible(const QString &name) const
{
	if (!m_showJoints || m_raster.image.isNull())
		return false;
	for (const auto &joint : m_rasterJoints)
		if (joint.name == name)
			return joint.visible;
	return false;
}
QString ModelViewport::jointAt(const QPointF &point, double tolerance) const
{
	if (!m_showJoints || m_editMoveActive || isRendering() || m_raster.image.isNull() || !finite(point) || !std::isfinite(tolerance) ||
		tolerance < 0 || tolerance > 100)
		return {};
	const double sx = double(width()) / std::max(1, m_rasterLogicalSize.width());
	const double sy = double(height()) / std::max(1, m_rasterLogicalSize.height());
	const auto scaled = [sx, sy](QPointF p) { return QPointF(p.x() * sx, p.y() * sy); };
	// Joint origins take priority over bones. Coincident origins prefer the
	// closest depth; exact ties retain skeleton order for deterministic picks.
	for (int pass = 0; pass < 2; ++pass)
	{
		QString chosen;
		double best = tolerance, depth = -std::numeric_limits<double>::infinity();
		for (const auto &joint : m_rasterJoints)
		{
			double distance = std::numeric_limits<double>::infinity();
			if (pass == 0 && joint.visible)
				distance = QLineF(point, scaled(joint.centre)).length();
			if (pass == 1)
				for (const auto &line : joint.segments)
					distance = std::min(distance, segmentDistance(point, QLineF(scaled(line.p1()), scaled(line.p2()))));
			if (distance <= tolerance && (distance < best - 1e-6 || (std::abs(distance - best) <= 1e-6 && joint.depth > depth)))
			{
				chosen = joint.name;
				best = distance;
				depth = joint.depth;
			}
		}
		if (!chosen.isEmpty())
			return chosen;
	}
	return {};
}
QString ModelViewport::jointSummary() const
{
	if (!m_showJoints || m_mesh.skeleton.isEmpty())
		return {};
	QString text = tr("Joints are circles linked to their parents. The selected joint has a square outline. Choose a joint in the Skeleton "
					  "hierarchy for keyboard editing.");
	text += QLatin1Char(' ') + (m_xrayJoints || m_renderMode == ModelViewportRenderMode::Wireframe
									? tr("X-ray joints: hidden joints can be selected.")
									: tr("Only visible joints and bone segments can be selected."));
	if (!m_editJoint.isEmpty())
	{
		text += QLatin1Char(' ') +
				tr("Selected joint: %1. %2")
					.arg(m_editJoint, m_jointDescendants ? tr("Children follow the transform.") : tr("Children remain in place."));
		ModelJointFrame frame;
		QString error;
		text += QLatin1Char(' ') + (!modelJointFrame(m_mesh, m_frame, &frame, &error) ? error
									: frame.clip < 0 ? tr("Bind editing preserves the mesh and existing animation.")
													 : tr("Editing clip %1, pose %2 only.").arg(frame.clip).arg(frame.local));
	}
	if (!m_jointPreviewError.isEmpty())
		text += QLatin1Char(' ') + m_jointPreviewError;
	return text;
}

void ModelViewport::prepareJointPreview(RasterWork &work)
{
	if ((!work.showJoints && work.editJoint.isEmpty()) || work.cancelled.load())
		return;
	ModelJointFrame frame;
	if (!modelJointFrame(work.mesh, work.frame, &frame))
		return;
	work.jointPose = frame.pose;
	if (work.editMoveActive && !work.editJoint.isEmpty())
	{
		ModelJointMatrix delta;
		ModelJointPreview preview;
		ModelWorkControl control;
		control.cancelled = [&work] { return work.cancelled.load(); };
		if (!modelJointTransformDelta(work.editTransform, &delta, &work.jointError) ||
			!previewModelJointTransform(work.mesh, work.frame, work.editJoint, delta, work.jointDescendants, &preview, &work.jointError,
										control))
			return;
		work.jointPose = std::move(preview.pose);
		for (auto &tag : work.mesh.tags)
			if (tag.frameIndex == work.frame)
			{
				const auto updated =
					std::find_if(preview.tags.cbegin(), preview.tags.cend(), [&](const auto &t) { return t.name == tag.name; });
				if (updated != preview.tags.cend())
					tag = *updated;
			}
		for (int i = 0; i < preview.surfaces.size(); ++i)
			work.mesh.surfaces[i].frames[work.frame] = std::move(preview.surfaces[i]);
		// Keep attachment markers on the same skinned pose and camera snapshot.
		for (auto &overlay : work.tags)
		{
			const auto tag =
				std::find_if(preview.tags.cbegin(), preview.tags.cend(), [&](const auto &t) { return t.name == overlay.name; });
			if (tag == preview.tags.cend())
				continue;
			overlay.centre = project(work, tag->origin);
			double x = 0, y = 0, z = 1;
			if (work.perspective)
				work.toView(tag->origin, &x, &y, &z);
			const double scale = work.perspective ? work.camera.focal / std::max(kNearPlane, z) : work.camera.scale;
			for (int selected = 0; selected < 2; ++selected)
				for (int axis = 0; axis < 3; ++axis)
				{
					ModelVec3 point;
					float *values[]{&point.x, &point.y, &point.z};
					*values[axis] = float(72 / std::max(1e-9, scale) * (selected ? .55 : .3));
					overlay.tips[selected * 3 + axis] = project(work, modelTagPoint(*tag, point));
				}
		}
	}
	else if (work.frameBlend > 0)
	{
		ModelJointFrame next;
		if (!modelJointFrame(work.mesh, work.blendFrame, &next))
		{
			work.jointPose.clear();
			return;
		}
		// Baked vertex playback uses linear interpolation. Bone origins follow
		// the same interpolation rather than describing a different rotation arc.
		for (int i = 0; i < work.jointPose.size(); ++i)
			for (int j = 0; j < 12; ++j)
				work.jointPose[i].m[j] += float(work.frameBlend * (next.pose[i].m[j] - work.jointPose[i].m[j]));
	}
}
void ModelViewport::renderJointOverlays(RasterWork &work)
{
	if (!work.success || !work.showJoints || work.cancelled.load() || work.jointPose.isEmpty())
		return;
	QVector<bool> affected(work.jointPose.size(), false);
	const int selected = modelJointIndex(work.mesh.skeleton, work.editJoint, Qt::CaseSensitive);
	for (int index = 0; index < work.jointPose.size(); ++index)
	{
		if (work.cancelled.load())
			return;
		const auto &joint = work.mesh.skeleton.joints[index];
		const bool hasParent = joint.parent >= 0 && joint.parent < index;
		affected[index] = index == selected || (hasParent && work.jointDescendants && affected[joint.parent]);
		const auto origin = modelJointTranslation(work.jointPose[index]);
		JointOverlay overlay;
		overlay.name = joint.name;
		overlay.centre = project(work, origin);
		overlay.depth = dot(origin, work.camera.eye);
		overlay.visible = visible(work, origin, overlay.centre);
		overlay.affected = affected[index];
		if (hasParent)
		{
			const auto parent = modelJointTranslation(work.jointPose[joint.parent]);
			QPointF a, b;
			if (work.projectSegment(parent, origin, &a, &b) && finite(a) && finite(b))
			{
				// Bound both work and output for very long/offscreen bones. Sample
				// visibility along the world segment; only visible runs are pickable.
				const int steps = std::clamp(int(std::min(1024., std::ceil(QLineF(a, b).length() / 2))), 1, 1024);
				QPointF previous;
				bool wasVisible = false;
				for (int step = 0; step <= steps; ++step)
				{
					if ((step & 255) == 0 && work.cancelled.load())
						return;
					const auto position = interpolateModelPosition(parent, origin, double(step) / steps);
					const auto screen = project(work, position);
					const bool isVisible = visible(work, position, screen);
					if (wasVisible && isVisible)
					{
						if (!overlay.segments.isEmpty() && overlay.segments.last().p2() == previous)
							overlay.segments.last().setP2(screen);
						else
							overlay.segments.append(QLineF(previous, screen));
					}
					previous = screen;
					wasVisible = isVisible;
				}
			}
		}
		work.joints.append(std::move(overlay));
	}
}
void ModelViewport::paintJointOverlays(QPainter &painter)
{
	if (!m_showJoints || m_raster.image.isNull())
		return;
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);
	const QColor accent = palette().color(QPalette::Highlight);
	const double sx = double(width()) / std::max(1, m_rasterLogicalSize.width());
	const double sy = double(height()) / std::max(1, m_rasterLogicalSize.height());
	const auto scaled = [sx, sy](QPointF p) { return QPointF(p.x() * sx, p.y() * sy); };
	for (const auto &joint : m_rasterJoints)
	{
		const QColor colour = joint.affected ? accent : QColor(Qt::white);
		for (const auto &segment : joint.segments)
		{
			const QLineF line(scaled(segment.p1()), scaled(segment.p2()));
			painter.setPen(QPen(Qt::black, joint.affected ? 5 : 4));
			painter.drawLine(line);
			painter.setPen(QPen(colour, joint.affected ? 3 : 2));
			painter.drawLine(line);
		}
	}
	for (const auto &joint : m_rasterJoints)
	{
		if (!joint.visible)
			continue;
		const auto point = scaled(joint.centre);
		painter.setPen(QPen(Qt::black, 2));
		painter.setBrush(joint.affected ? accent : QColor(Qt::white));
		painter.drawEllipse(point, 4, 4);
		if (joint.name == m_editJoint)
		{
			painter.setBrush(Qt::NoBrush);
			painter.setPen(QPen(Qt::black, 4));
			painter.drawRect(QRectF(point - QPointF(8, 8), QSizeF(16, 16)));
			painter.setPen(QPen(accent, 2));
			painter.drawRect(QRectF(point - QPointF(8, 8), QSizeF(16, 16)));
		}
	}
	painter.restore();
}
} // namespace vibestudio
