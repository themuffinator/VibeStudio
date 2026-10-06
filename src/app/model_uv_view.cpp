#include "app/model_uv_view.h"

#include "core/model_transform.h"

#include <QAccessibleWidget>
#include <QApplication>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
double squared(QPointF p) { return QPointF::dotProduct(p, p); }
QPointF unavailable() { return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()}; }
} // namespace
struct ModelUvView::Work
{
	ModelUvRenderRequest request;
	ModelUvRenderResult result;
	std::atomic_bool cancelled{false};
	quint64 revision = 0, generation = 0;
	QString error;
	bool success = false;
};
ModelUvView::ModelUvView(QWidget *parent) : QLabel(parent)
{
	static const bool installed = []
	{
		QAccessible::installFactory(
			[](const QString &, QObject *object) -> QAccessibleInterface *
			{
				if (auto *view = qobject_cast<ModelUvView *>(object))
				{
					return new QAccessibleWidget(view, QAccessible::Graphic);
				}
				return nullptr;
			});
		return true;
	}();
	Q_UNUSED(installed);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setAlignment(Qt::AlignCenter);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
	setMinimumSize(200, 180);
	setAccessibleName(QCoreApplication::translate("VibeStudioModelUvView", "UV layout"));
	announce();
}
ModelUvView::~ModelUvView()
{
	if (m_work)
	{
		m_work->cancelled.store(true);
	}
	if (m_thread)
	{
		m_thread->wait();
	}
}
void ModelUvView::setSource(const ModelSurface &surface, const ModelSelection &selection, const QImage &texture)
{
	const bool topologyChanged = !m_hasSource || m_surface.triangles.constData() != surface.triangles.constData() ||
								 m_surface.texCoords.constData() != surface.texCoords.constData() || m_surface.uvSeams != surface.uvSeams ||
								 m_surface.vertexCount != surface.vertexCount;
	const bool selectionChanged = m_selection.surface != selection.surface || m_selection.faces != selection.faces ||
								  m_selection.vertices != selection.vertices || m_selection.edges != selection.edges;
	const bool imageChanged = m_texture.cacheKey() != texture.cacheKey();
	if (!topologyChanged && !selectionChanged && !imageChanged && m_surface.name == surface.name)
	{
		return;
	}
	finishMove(false);
	if (topologyChanged || selectionChanged)
	{
		m_hasSelection = false;
	}
	if (!m_hasSource || m_selection.surface != selection.surface)
	{
		m_fit = ModelUvFit::All;
		m_resizeFit = ModelUvFit::All;
	}
	if (topologyChanged)
	{
		m_topology.reset();
	}
	if (imageChanged && m_hasSource && !texture.isNull() && !m_texture.isNull() && m_fit == ModelUvFit::None)
	{
		const double ratio = (double(texture.width()) / texture.height()) / (double(m_texture.width()) / m_texture.height());
		const auto centre = m_camera.inverted().map(QPointF(width() / 2.0, height() / 2.0));
		m_camera =
			QTransform(m_camera.m11() * ratio, 0, 0, m_camera.m22(), width() / 2.0 - centre.x() * m_camera.m11() * ratio, m_camera.dy());
		if (ratio != 1 && m_resizeFit != ModelUvFit::None)
		{
			m_fit = m_resizeFit;
		}
	}
	m_surface = surface;
	m_surface.frames.clear();
	m_surface.skinPaths.clear();
	m_surface.warnings.clear();
	m_selection = selection;
	m_texture = texture;
	m_hasSource = true;
	requestRender(topologyChanged || imageChanged);
}
void ModelUvView::setPickMode(int mode)
{
	if (mode < 0 || mode > 3 || mode == m_mode)
	{
		return;
	}
	finishMove(false);
	m_mode = mode;
	requestRender();
}
void ModelUvView::setPlaybackTexture(const QImage &texture)
{
	if (!m_hasSource || texture.isNull() || texture.size() != m_texture.size() || texture.cacheKey() == m_texture.cacheKey())
	{
		return;
	}
	m_texture = texture;
	requestRender();
}
void ModelUvView::setMoveEnabled(bool enabled, double grid)
{
	ModelVec3 checked;
	if (!snapModelTranslation({}, grid, &checked))
	{
		return;
	}
	if (enabled != m_moveEnabled || grid != m_grid)
	{
		finishMove(false);
		m_moveEnabled = enabled;
		m_grid = grid;
		announce();
		update();
	}
}
void ModelUvView::requestRender(bool retire)
{
	if (!m_hasSource)
	{
		return;
	}
	++m_revision;
	m_dirty = true;
	m_error.clear();
	if (retire)
	{
		++m_generation;
		if (m_work)
		{
			m_work->cancelled.store(true);
		}
		clear();
	}
	announce();
	update();
	QTimer::singleShot(0, this, &ModelUvView::startRender);
}
void ModelUvView::startRender()
{
	if (m_thread || !m_dirty || !m_hasSource || width() < 1 || height() < 1)
	{
		return;
	}
	auto work = std::make_shared<Work>();
	work->revision = m_revision;
	work->generation = m_generation;
	auto &request = work->request;
	request.surface = m_surface;
	request.selection = m_selection;
	request.texture = m_texture;
	request.topology = m_topology;
	request.logicalSize = size();
	request.pixelRatio = devicePixelRatioF();
	request.camera = m_camera;
	request.fit = m_fit;
	request.moving = m_moving;
	request.moveOffset = m_delta;
	request.showVertices = m_mode == 1;
	request.background = palette().color(QPalette::Base);
	request.foreground = palette().color(QPalette::Text);
	request.accent = palette().color(QPalette::Highlight);
	m_dirty = false;
	m_work = work;
	auto *thread = QThread::create(
		[work]
		{
			try
			{
				ModelWorkControl control;
				control.cancelled = [work] { return work->cancelled.load(); };
				work->success = renderModelUv(work->request, &work->result, &work->error, control);
			}
			catch (const std::bad_alloc &)
			{
				work->error = QCoreApplication::translate("VibeStudioModelUvView", "Unable to allocate the UV preview. Reduce its size.");
			}
		});
	thread->setParent(this);
	m_thread = thread;
	connect(thread, &QThread::finished, this,
			[this, thread, work]
			{
				m_thread = nullptr;
				m_work.reset();
				if (work->generation == m_generation && !work->cancelled.load())
				{
					if (work->success)
					{
						m_topology = work->result.topology;
						setPixmap(QPixmap::fromImage(work->result.image));
						if (work->revision == m_revision)
						{
							m_camera = work->result.camera;
							m_fit = ModelUvFit::None;
							m_selectionBounds = work->result.selectionBounds;
							m_hasSelection = work->result.hasSelection;
						}
					}
					else
					{
						m_error = work->error.isEmpty()
									  ? QCoreApplication::translate("VibeStudioModelUvView", "Unable to render this UV layout.")
									  : work->error;
						clear();
					}
					m_dirty = work->revision != m_revision;
				}
				thread->deleteLater();
				announce();
				update();
				Q_EMIT renderCompleted();
				if (m_dirty)
				{
					QTimer::singleShot(0, this, &ModelUvView::startRender);
				}
			});
	thread->start();
	announce();
}
bool ModelUvView::isRendering() const { return m_thread || m_dirty; }
const ModelUvTopology *ModelUvView::topology() const { return m_topology.get(); }
QString ModelUvView::summary() const
{
	if (!m_error.isEmpty())
	{
		return m_error;
	}
	if (!m_hasSource)
	{
		return QCoreApplication::translate("VibeStudioModelUvView", "No UV layout is open.");
	}
	QString text =
		QCoreApplication::translate(
			"VibeStudioModelUvView",
			"UV layout for %1. Islands: %2; marked seams: %3. Dotted edges mark seams; hatching, dashed edges and square points show "
			"selection. Middle-drag pans, wheel zooms, F frames the selection, Home frames all UVs. Drag the centre square to "
			"move UVs; Escape cancels. Numeric UV controls are in Surface.")
			.arg(m_surface.name)
			.arg(m_topology ? m_topology->islands.size() : 0)
			.arg(m_surface.uvSeams.size());
	if (isRendering())
	{
		text += QLatin1Char(' ') + QCoreApplication::translate("VibeStudioModelUvView", "Updating UV layout…");
	}
	if (m_moving)
	{
		text += QLatin1Char(' ') + QCoreApplication::translate("VibeStudioModelUvView", "UV move preview: U %1, V %2.")
									   .arg(m_delta.u, 0, 'g', 7)
									   .arg(m_delta.v, 0, 'g', 7);
	}
	return text;
}
void ModelUvView::announce() { setAccessibleDescription(summary()); }
void ModelUvView::frameAll()
{
	finishMove(false);
	m_fit = ModelUvFit::All;
	m_resizeFit = m_fit;
	requestRender();
}
void ModelUvView::frameSelection()
{
	finishMove(false);
	m_fit = ModelUvFit::Selection;
	m_resizeFit = m_fit;
	requestRender();
}
void ModelUvView::panView(QPointF delta)
{
	if (!finite(delta) || !m_topology || m_fit != ModelUvFit::None)
	{
		return;
	}
	finishMove(false);
	m_resizeFit = ModelUvFit::None;
	m_camera = QTransform(m_camera.m11(), 0, 0, m_camera.m22(), std::clamp(m_camera.dx() + delta.x(), -1e12, 1e12),
						  std::clamp(m_camera.dy() + delta.y(), -1e12, 1e12));
	requestRender();
}
void ModelUvView::zoomAt(QPointF point, double factor)
{
	if (!finite(point) || !std::isfinite(factor) || factor <= 0 || !m_topology || m_fit != ModelUvFit::None)
	{
		return;
	}
	finishMove(false);
	m_resizeFit = ModelUvFit::None;
	factor = std::clamp(factor, 0.1, 10.0);
	const double scale = std::clamp(m_camera.m22() * factor, 0.00001, 1000000.0);
	factor = scale / m_camera.m22();
	m_camera = QTransform(m_camera.m11() * factor, 0, 0, scale, point.x() + (m_camera.dx() - point.x()) * factor,
						  point.y() + (m_camera.dy() - point.y()) * factor);
	requestRender();
}
QPointF ModelUvView::uvToScreen(ModelTexCoord uv) const { return m_camera.map(QPointF(uv.u, uv.v)); }
ModelUvHit ModelUvView::hitAt(QPointF point, double tolerance) const
{
	if (!finite(point) || !std::isfinite(tolerance) || tolerance < 0 || tolerance > 64 || !rect().contains(point.toPoint()) ||
		isRendering() || m_moving || !m_topology)
	{
		return {};
	}
	ModelUvHit hit;
	double distance = tolerance * tolerance;
	if (m_mode == 1)
	{
		for (int vertex = 0; vertex < m_surface.vertexCount; ++vertex)
		{
			const double d = squared(uvToScreen(m_surface.texCoords[vertex]) - point);
			if (d < distance || (d == distance && hit.kind < 0))
			{
				distance = d;
				hit = {1, vertex, -1};
			}
		}
	}
	else if (m_mode == 2)
	{
		for (const auto &edge : m_topology->edges)
		{
			const auto a = uvToScreen(m_surface.texCoords[edge.vertices.first]), b = uvToScreen(m_surface.texCoords[edge.vertices.second]);
			const auto delta = b - a;
			const double length = squared(delta),
						 t = length > 1e-20 ? std::clamp(QPointF::dotProduct(point - a, delta) / length, 0.0, 1.0) : 0;
			const double d = squared(a + delta * t - point);
			if (d < distance || (d == distance && hit.kind < 0))
			{
				distance = d;
				hit = {2, edge.vertices.first, edge.vertices.second};
			}
		}
	}
	else
	{
		const auto uv = m_camera.inverted().map(point);
		for (int face = m_surface.triangles.size() - 1; face >= 0; --face)
		{
			const auto t = m_surface.triangles[face];
			QPolygonF polygon;
			for (int vertex : {t.a, t.b, t.c})
			{
				const auto p = m_surface.texCoords[vertex];
				polygon << QPointF(p.u, p.v);
			}
			if (polygon.containsPoint(uv, Qt::OddEvenFill))
			{
				return {m_mode, face, -1};
			}
		}
	}
	return hit;
}
QPointF ModelUvView::moveHandle() const
{
	if (!m_moveEnabled || !m_topology || m_fit != ModelUvFit::None)
	{
		return unavailable();
	}
	if (!m_hasSelection)
	{
		return unavailable();
	}
	const auto point = m_camera.map(m_selectionBounds.center() + QPointF(m_delta.u, m_delta.v));
	return rect().contains(point.toPoint()) ? point : unavailable();
}
bool ModelUvView::beginMove(QPointF point)
{
	const auto handle = moveHandle();
	if (!finite(point) || !finite(handle) || isRendering() || m_moving || squared(handle - point) > 100)
	{
		return false;
	}
	m_moving = true;
	m_travelled = false;
	m_press = point;
	m_delta = {};
	announce();
	update();
	Q_EMIT moveActiveChanged(true);
	return true;
}
bool ModelUvView::updateMove(QPointF point)
{
	if (!m_moving || !finite(point))
	{
		return false;
	}
	if (!m_travelled && std::sqrt(squared(point - m_press)) < QApplication::startDragDistance())
	{
		return true;
	}
	const auto inverse = m_camera.inverted();
	const auto delta = inverse.map(point) - inverse.map(m_press);
	ModelVec3 snapped;
	if (!snapModelTranslation({float(delta.x()), float(delta.y()), 0}, m_grid, &snapped))
	{
		return false;
	}
	m_travelled = true;
	if (m_delta.u == snapped.x && m_delta.v == snapped.y)
	{
		return true;
	}
	m_delta = {snapped.x, snapped.y};
	requestRender();
	Q_EMIT movePreviewChanged(snapped.x, snapped.y);
	return true;
}
void ModelUvView::finishMove(bool commit)
{
	if (!m_moving)
	{
		return;
	}
	const auto delta = m_delta;
	m_moving = false;
	m_travelled = false;
	m_delta = {};
	m_button = Qt::NoButton;
	unsetCursor();
	requestRender(true);
	Q_EMIT moveActiveChanged(false);
	if (commit && (delta.u != 0 || delta.v != 0))
	{
		Q_EMIT moveRequested(delta.u, delta.v);
	}
}
bool ModelUvView::moving() const { return m_moving; }
ModelTexCoord ModelUvView::moveDelta() const { return m_delta; }
void ModelUvView::paintEvent(QPaintEvent *event)
{
	QLabel::paintEvent(event);
	QPainter painter(this);
	const auto handle = moveHandle();
	if (finite(handle))
	{
		painter.setPen(QPen(Qt::black, 3));
		painter.setBrush(palette().highlight());
		painter.drawRect(QRectF(handle.x() - 5, handle.y() - 5, 10, 10));
		painter.setPen(Qt::white);
		painter.drawLine(handle + QPointF(-3, 0), handle + QPointF(3, 0));
		painter.drawLine(handle + QPointF(0, -3), handle + QPointF(0, 3));
	}
	const QString status = !m_error.isEmpty() ? m_error
						   : isRendering()	  ? QCoreApplication::translate("VibeStudioModelUvView", "Updating UV layout…")
						   : m_topology		  ? QCoreApplication::translate("VibeStudioModelUvView", "Islands: %1 · Seams: %2")
											  .arg(m_topology->islands.size())
											  .arg(m_surface.uvSeams.size())
										: QString();
	if (!status.isEmpty())
	{
		const QRectF box = QRectF(rect()).adjusted(8, 8, -8, -8);
		const auto textBox = painter.boundingRect(box, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, status).adjusted(-3, -2, 3, 2);
		painter.fillRect(textBox, palette().base());
		painter.setPen(palette().text().color());
		painter.drawText(box, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, status);
	}
	if (hasFocus())
	{
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(palette().highlight(), 2, Qt::DashLine));
		painter.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}
void ModelUvView::resizeEvent(QResizeEvent *event)
{
	finishMove(false);
	if (m_resizeFit != ModelUvFit::None)
	{
		m_fit = m_resizeFit;
	}
	else if (event->oldSize().isValid())
	{
		const auto delta = (QSizeF(event->size()) - QSizeF(event->oldSize())) / 2;
		m_camera = QTransform(m_camera.m11(), 0, 0, m_camera.m22(), m_camera.dx() + delta.width(), m_camera.dy() + delta.height());
	}
	QLabel::resizeEvent(event);
	requestRender();
}
void ModelUvView::changeEvent(QEvent *event)
{
	QLabel::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
	{
		requestRender();
	}
}
void ModelUvView::mousePressEvent(QMouseEvent *event)
{
	m_press = m_last = event->position();
	m_button = event->button();
	if (m_button == Qt::LeftButton && event->modifiers() == Qt::NoModifier && beginMove(m_press))
	{
		setCursor(Qt::SizeAllCursor);
		event->accept();
		return;
	}
	if (m_button == Qt::MiddleButton)
	{
		setCursor(Qt::ClosedHandCursor);
		event->accept();
		return;
	}
	QLabel::mousePressEvent(event);
}
void ModelUvView::mouseMoveEvent(QMouseEvent *event)
{
	if (m_moving)
	{
		updateMove(event->position());
		event->accept();
		return;
	}
	if (m_button == Qt::MiddleButton)
	{
		panView(event->position() - m_last);
		m_last = event->position();
		event->accept();
		return;
	}
	QLabel::mouseMoveEvent(event);
}
void ModelUvView::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_moving && event->button() == Qt::LeftButton)
	{
		updateMove(event->position());
		finishMove(true);
		event->accept();
		return;
	}
	if (event->button() == Qt::LeftButton && m_button == Qt::LeftButton &&
		std::sqrt(squared(event->position() - m_press)) < QApplication::startDragDistance())
	{
		const auto hit = hitAt(event->position());
		if (hit.kind >= 0)
		{
			Q_EMIT componentPicked(hit.kind, hit.a, hit.b, event->modifiers().testFlag(Qt::ControlModifier));
		}
	}
	m_button = Qt::NoButton;
	unsetCursor();
	QLabel::mouseReleaseEvent(event);
}
void ModelUvView::wheelEvent(QWheelEvent *event)
{
	if (!m_moving)
	{
		zoomAt(event->position(), std::pow(1.2, event->angleDelta().y() / 120.0));
	}
	event->accept();
}
void ModelUvView::keyPressEvent(QKeyEvent *event)
{
	if (m_moving)
	{
		if (event->key() == Qt::Key_Escape)
		{
			finishMove(false);
		}
		event->accept();
		return;
	}
	if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_F)
	{
		frameSelection();
		event->accept();
		return;
	}
	if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_Home)
	{
		frameAll();
		event->accept();
		return;
	}
	QLabel::keyPressEvent(event);
}
void ModelUvView::focusOutEvent(QFocusEvent *event)
{
	finishMove(false);
	m_button = Qt::NoButton;
	unsetCursor();
	QLabel::focusOutEvent(event);
}
bool ModelUvView::event(QEvent *event)
{
	if (event->type() == QEvent::ShortcutOverride)
	{
		const auto *key = static_cast<QKeyEvent *>(event);
		if ((m_moving && key->key() == Qt::Key_Escape) ||
			(key->modifiers() == Qt::NoModifier && (key->key() == Qt::Key_F || key->key() == Qt::Key_Home)))
		{
			event->accept();
			return true;
		}
	}
	return QLabel::event(event);
}
} // namespace vibestudio
