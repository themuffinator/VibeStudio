#include "app/material_graph_view.h"

#include "app/studio_theme.h"

#include <QAccessibleWidget>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QToolTip>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio {

namespace {

// Scene units: pixels at zoom 1 with text at 100%. Text is drawn at a fixed
// scene size so the core's layout holds; larger text scales the zoom.
constexpr double kNodeWidth = 196.0;
constexpr double kHeader = 26.0;
constexpr double kSubtitle = 18.0;
constexpr double kPortRow = 18.0;
constexpr double kThumbnail = 52.0;
constexpr double kPadding = 6.0;
constexpr int kFontPixels = 12;

double textZoom()
{
	return std::max(1.0, currentStudioTheme().textScalePercent / 100.0);
}

QColor portColor(MaterialGraphPortType type, const StudioThemeColors& colors)
{
	switch (type) {
	case MaterialGraphPortType::Image:
	case MaterialGraphPortType::Frame:
		return colors.info;
	case MaterialGraphPortType::Coordinates:
		return colors.warning;
	case MaterialGraphPortType::Color:
		return colors.success;
	case MaterialGraphPortType::Alpha:
		return colors.textMuted;
	case MaterialGraphPortType::Scalar:
		return colors.accent;
	case MaterialGraphPortType::Stage:
		return colors.text;
	case MaterialGraphPortType::Geometry:
		return colors.danger;
	}
	return colors.text;
}

QColor kindColor(const QString& kind, const StudioThemeColors& colors)
{
	static const QStringList images {QStringLiteral("image"), QStringLiteral("animmap"), QStringLiteral("lightmap"), QStringLiteral("video"),
		QStringLiteral("cube"), QStringLiteral("program"), QStringLiteral("frame"), QStringLiteral("patch")};
	static const QStringList coordinates {QStringLiteral("tcgen"), QStringLiteral("tcmod"), QStringLiteral("transform"), QStringLiteral("texgen")};
	static const QStringList colours {QStringLiteral("rgbgen"), QStringLiteral("alphagen"), QStringLiteral("color"), QStringLiteral("alphatest"),
		QStringLiteral("condition")};
	if (kind == QStringLiteral("output")) {
		return colors.accent;
	}
	if (kind == QStringLiteral("stage")) {
		return colors.text;
	}
	if (images.contains(kind)) {
		return colors.info;
	}
	if (coordinates.contains(kind)) {
		return colors.warning;
	}
	if (colours.contains(kind)) {
		return colors.success;
	}
	if (kind == QStringLiteral("deform")) {
		return colors.danger;
	}
	if (kind == QStringLiteral("expression")) {
		return colors.accentHover;
	}
	return colors.textMuted;
}

// Each port type has its own shape as well as its own colour.
void drawPort(QPainter& painter, const QPointF& centre, MaterialGraphPortType type, const QColor& color, const QColor& fill)
{
	const double r = 4.5;
	painter.setPen(QPen(color, 1.5));
	painter.setBrush(color);
	switch (type) {
	case MaterialGraphPortType::Image:
		painter.drawEllipse(centre, r, r);
		break;
	case MaterialGraphPortType::Frame:
		painter.setBrush(fill);
		painter.drawEllipse(centre, r, r);
		break;
	case MaterialGraphPortType::Coordinates: {
		const QPolygonF diamond {centre + QPointF(0, -r - 1), centre + QPointF(r + 1, 0), centre + QPointF(0, r + 1), centre + QPointF(-r - 1, 0)};
		painter.drawPolygon(diamond);
		break;
	}
	case MaterialGraphPortType::Color:
		painter.drawRect(QRectF(centre.x() - r, centre.y() - r, r * 2, r * 2));
		break;
	case MaterialGraphPortType::Alpha: {
		const QPolygonF triangle {centre + QPointF(0, -r - 1), centre + QPointF(r + 1, r), centre + QPointF(-r - 1, r)};
		painter.drawPolygon(triangle);
		break;
	}
	case MaterialGraphPortType::Scalar:
		painter.setBrush(fill);
		painter.drawEllipse(centre, r - 1, r - 1);
		break;
	case MaterialGraphPortType::Stage:
		painter.setBrush(fill);
		painter.drawRoundedRect(QRectF(centre.x() - r, centre.y() - r, r * 2, r * 2), 2, 2);
		break;
	case MaterialGraphPortType::Geometry: {
		QPolygonF hexagon;
		for (int corner = 0; corner < 6; ++corner) {
			const double angle = corner * std::numbers::pi / 3.0;
			hexagon << centre + QPointF(std::cos(angle) * (r + 1), std::sin(angle) * (r + 1));
		}
		painter.drawPolygon(hexagon);
		break;
	}
	}
}

QString portTypeName(MaterialGraphPortType type)
{
	switch (type) {
	case MaterialGraphPortType::Image:
		return MaterialGraphView::tr("image");
	case MaterialGraphPortType::Coordinates:
		return MaterialGraphView::tr("coordinates");
	case MaterialGraphPortType::Color:
		return MaterialGraphView::tr("colour");
	case MaterialGraphPortType::Alpha:
		return MaterialGraphView::tr("alpha");
	case MaterialGraphPortType::Scalar:
		return MaterialGraphView::tr("number");
	case MaterialGraphPortType::Stage:
		return MaterialGraphView::tr("stage");
	case MaterialGraphPortType::Geometry:
		return MaterialGraphView::tr("geometry");
	case MaterialGraphPortType::Frame:
		return MaterialGraphView::tr("frame");
	}
	return {};
}

// --- Accessibility ---------------------------------------------------------

// One node, read as a list item: its title, then its kind, subtitle and
// connections.
class MaterialGraphNodeAccessible final : public QAccessibleInterface {
public:
	MaterialGraphNodeAccessible(MaterialGraphView* view, int index)
		: m_view(view)
		, m_index(index)
	{
	}

	bool isValid() const override { return m_view && m_index >= 0 && m_index < m_view->nodeCount(); }
	QObject* object() const override { return nullptr; }
	QWindow* window() const override { return m_view && m_view->window() ? m_view->window()->windowHandle() : nullptr; }
	QList<QPair<QAccessibleInterface*, QAccessible::Relation>> relations(QAccessible::Relation) const override { return {}; }
	QAccessibleInterface* focusChild() const override { return nullptr; }
	QAccessibleInterface* childAt(int, int) const override { return nullptr; }
	QAccessibleInterface* parent() const override { return m_view ? QAccessible::queryAccessibleInterface(m_view.data()) : nullptr; }
	QAccessibleInterface* child(int) const override { return nullptr; }
	int childCount() const override { return 0; }
	int indexOfChild(const QAccessibleInterface*) const override { return -1; }
	QString text(QAccessible::Text type) const override
	{
		if (!isValid()) {
			return {};
		}
		switch (type) {
		case QAccessible::Name:
			return m_view->nodeAccessibleName(m_index);
		case QAccessible::Description:
			return m_view->nodeAccessibleDescription(m_index);
		default:
			return {};
		}
	}
	void setText(QAccessible::Text, const QString&) override {}
	QRect rect() const override
	{
		if (!isValid()) {
			return {};
		}
		const QRect local = m_view->nodeRect(m_index);
		return QRect(m_view->mapToGlobal(local.topLeft()), local.size());
	}
	QAccessible::Role role() const override { return QAccessible::ListItem; }
	QAccessible::State state() const override
	{
		QAccessible::State state;
		state.selectable = true;
		state.focusable = true;
		if (isValid() && m_view->selectedIndex() == m_index) {
			state.selected = true;
			state.focused = m_view->hasFocus();
		}
		if (isValid() && !m_view->rect().intersects(m_view->nodeRect(m_index))) {
			state.offscreen = true;
		}
		return state;
	}

private:
	QPointer<MaterialGraphView> m_view;
	int m_index = -1;
};

class MaterialGraphAccessible final : public QAccessibleWidget {
public:
	explicit MaterialGraphAccessible(MaterialGraphView* view)
		: QAccessibleWidget(view, QAccessible::List)
	{
	}

	int childCount() const override { return view()->nodeCount(); }
	QAccessibleInterface* child(int index) const override { return view()->accessibleChild(index); }
	int indexOfChild(const QAccessibleInterface* child) const override { return view()->accessibleChildIndex(child); }
	QAccessibleInterface* focusChild() const override
	{
		const int selected = view()->selectedIndex();
		return selected >= 0 && view()->hasFocus() ? view()->accessibleChild(selected) : nullptr;
	}
	QAccessibleInterface* childAt(int x, int y) const override
	{
		const QPoint local = view()->mapFromGlobal(QPoint(x, y));
		for (int index = view()->nodeCount() - 1; index >= 0; --index) {
			if (view()->nodeRect(index).contains(local)) {
				return view()->accessibleChild(index);
			}
		}
		return nullptr;
	}

private:
	MaterialGraphView* view() const { return static_cast<MaterialGraphView*>(widget()); }
};

QAccessibleInterface* materialGraphAccessibleFactory(const QString&, QObject* object)
{
	if (auto* view = qobject_cast<MaterialGraphView*>(object)) {
		return new MaterialGraphAccessible(view);
	}
	return nullptr;
}

} // namespace

MaterialGraphView::MaterialGraphView(QWidget* parent)
	: QWidget(parent)
{
	static const bool installed = [] {
		QAccessible::installFactory(materialGraphAccessibleFactory);
		return true;
	}();
	Q_UNUSED(installed);
	setObjectName(QStringLiteral("materialGraph"));
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setMinimumSize(240, 160);
	setAccessibleName(tr("Material node graph"));
	setAccessibleDescription(tr("Arrow keys move between connected nodes; Enter edits the node's properties; Delete removes it; "
								"Alt+Up and Alt+Down change the draw order; the context menu adds nodes."));
	m_message = tr("Choose a material to see its nodes.");
}

MaterialGraphView::~MaterialGraphView()
{
	resetAccessibleChildren();
}

void MaterialGraphView::setGraph(const MaterialGraph& graph, const QString& layoutKey)
{
	const QString previous = selectedNode();
	const bool sameLayout = layoutKey == m_layoutKey && !m_graph.nodes.isEmpty();
	m_graph = graph;
	if (!sameLayout) {
		m_moved.clear();
		m_fitPending = true;
	} else {
		// Forget positions of nodes that went away.
		for (auto it = m_moved.begin(); it != m_moved.end();) {
			it = m_graph.indexOf(it.key()) < 0 ? m_moved.erase(it) : std::next(it);
		}
	}
	m_layoutKey = layoutKey;
	m_hover = -1;
	resetAccessibleChildren();
	m_selected = m_graph.indexOf(previous);
	if (m_selected < 0 && sameLayout && !m_graph.nodes.isEmpty()) {
		m_selected = m_graph.indexOf(QStringLiteral("material"));
	}
	if (m_fitPending && width() > 0 && height() > 0) {
		fitToView();
	}
	{
		QAccessibleEvent event(this, QAccessible::ObjectReorder);
		QAccessible::updateAccessibility(&event);
	}
	update();
	if (m_graph.indexOf(previous) < 0) {
		notifySelection();
	}
}

void MaterialGraphView::clearGraph(const QString& message)
{
	m_graph = MaterialGraph();
	m_message = message;
	m_selected = -1;
	m_hover = -1;
	m_layoutKey.clear();
	m_moved.clear();
	resetAccessibleChildren();
	QAccessibleEvent event(this, QAccessible::ObjectReorder);
	QAccessible::updateAccessibility(&event);
	update();
	Q_EMIT selectionChanged(QString());
}

void MaterialGraphView::setThumbnails(const QHash<QString, QImage>& thumbnails)
{
	m_thumbnails = thumbnails;
	update();
}

void MaterialGraphView::setNodeTemplates(const QVector<MaterialGraphNodeTemplate>& templates)
{
	m_templates = templates;
}

void MaterialGraphView::setReadOnly(bool readOnly)
{
	m_readOnly = readOnly;
}

QString MaterialGraphView::selectedNode() const
{
	return m_selected >= 0 && m_selected < m_graph.nodes.size() ? m_graph.nodes.at(m_selected).id : QString();
}

void MaterialGraphView::selectNode(const QString& nodeId)
{
	selectIndex(m_graph.indexOf(nodeId));
}

void MaterialGraphView::selectIndex(int index)
{
	if (index < -1 || index >= m_graph.nodes.size()) {
		return;
	}
	if (index == m_selected) {
		return;
	}
	m_selected = index;
	if (index >= 0) {
		ensureVisible(index);
	}
	update();
	notifySelection();
}

void MaterialGraphView::notifySelection()
{
	Q_EMIT selectionChanged(selectedNode());
	if (m_selected >= 0) {
		QAccessibleEvent selection(this, QAccessible::Selection);
		selection.setChild(m_selected);
		QAccessible::updateAccessibility(&selection);
		if (hasFocus()) {
			QAccessibleEvent focus(this, QAccessible::Focus);
			focus.setChild(m_selected);
			QAccessible::updateAccessibility(&focus);
		}
	}
}

QSizeF MaterialGraphView::nodeSize(int index) const
{
	const MaterialGraphNode& node = m_graph.nodes.at(index);
	double height = kHeader;
	if (!node.subtitle.isEmpty() || node.inactive) {
		height += kSubtitle;
	}
	height += std::max(node.inputs.size(), node.outputs.size()) * kPortRow;
	if (!node.imageReference.isEmpty() && m_thumbnails.contains(node.imageReference)) {
		height += kThumbnail;
	}
	height += kPadding;
	return {kNodeWidth, std::max(height, kHeader + kPadding * 2)};
}

QPointF MaterialGraphView::nodePosition(int index) const
{
	const MaterialGraphNode& node = m_graph.nodes.at(index);
	return m_moved.value(node.id, node.position);
}

QRectF MaterialGraphView::sceneRect(int index) const
{
	const QPointF position = nodePosition(index);
	const QSizeF size = nodeSize(index);
	// Right to left, the graph flows the other way: mirror the layout.
	if (rightToLeft()) {
		return QRectF(QPointF(-(position.x() + size.width()), position.y()), size);
	}
	return QRectF(position, size);
}

QPointF MaterialGraphView::portPoint(int index, int port, bool output) const
{
	const MaterialGraphNode& node = m_graph.nodes.at(index);
	const QRectF rect = sceneRect(index);
	const double top = rect.top() + kHeader + ((!node.subtitle.isEmpty() || node.inactive) ? kSubtitle : 0.0);
	const double y = top + std::max(0, port) * kPortRow + kPortRow / 2.0;
	const bool rightEdge = output != rightToLeft();
	return {rightEdge ? rect.right() : rect.left(), y};
}

int MaterialGraphView::portIndex(int node, const QString& portId, bool output) const
{
	const QVector<MaterialGraphPort>& ports = output ? m_graph.nodes.at(node).outputs : m_graph.nodes.at(node).inputs;
	for (int index = 0; index < ports.size(); ++index) {
		if (ports.at(index).id == portId) {
			return index;
		}
	}
	return ports.isEmpty() ? -1 : 0;
}

QPointF MaterialGraphView::toWidget(const QPointF& scene) const
{
	return scene * m_zoom + m_pan;
}

QPointF MaterialGraphView::toScene(const QPointF& widget) const
{
	return (widget - m_pan) / m_zoom;
}

QRect MaterialGraphView::nodeRect(int index) const
{
	if (index < 0 || index >= m_graph.nodes.size()) {
		return {};
	}
	const QRectF scene = sceneRect(index);
	return QRectF(toWidget(scene.topLeft()), toWidget(scene.bottomRight())).toAlignedRect();
}

int MaterialGraphView::nodeAt(const QPointF& widget) const
{
	const QPointF scene = toScene(widget);
	for (int index = static_cast<int>(m_graph.nodes.size()) - 1; index >= 0; --index) {
		if (sceneRect(index).contains(scene)) {
			return index;
		}
	}
	return -1;
}

QRectF MaterialGraphView::contentsRect() const
{
	QRectF bounds;
	for (int index = 0; index < m_graph.nodes.size(); ++index) {
		bounds = bounds.isNull() ? sceneRect(index) : bounds.united(sceneRect(index));
	}
	return bounds;
}

void MaterialGraphView::fitToView()
{
	m_fitPending = false;
	const QRectF bounds = contentsRect();
	if (bounds.isNull() || width() < 10 || height() < 10) {
		m_fitPending = bounds.isNull() ? m_fitPending : true;
		return;
	}
	const double margin = 24.0;
	const double zoomX = (width() - margin * 2) / std::max(1.0, bounds.width());
	const double zoomY = (height() - margin * 2) / std::max(1.0, bounds.height());
	m_zoom = std::clamp(std::min(zoomX, zoomY), 0.2, textZoom());
	m_pan = QPointF(width() / 2.0, height() / 2.0) - bounds.center() * m_zoom;
	update();
}

void MaterialGraphView::zoomBy(double factor)
{
	const QPointF centre(width() / 2.0, height() / 2.0);
	const QPointF scene = toScene(centre);
	m_zoom = std::clamp(m_zoom * factor, 0.2, 3.0 * textZoom());
	m_pan = centre - scene * m_zoom;
	update();
}

void MaterialGraphView::ensureVisible(int index)
{
	const QRect node = nodeRect(index);
	const QRect area = rect().adjusted(12, 12, -12, -12);
	if (area.contains(node) || node.isNull()) {
		return;
	}
	QPointF shift;
	if (node.left() < area.left()) {
		shift.rx() = area.left() - node.left();
	} else if (node.right() > area.right()) {
		shift.rx() = std::max(area.right() - node.right(), area.left() - node.left());
	}
	if (node.top() < area.top()) {
		shift.ry() = area.top() - node.top();
	} else if (node.bottom() > area.bottom()) {
		shift.ry() = std::max(area.bottom() - node.bottom(), area.top() - node.top());
	}
	m_pan += shift;
	update();
}

QString MaterialGraphView::nodeAccessibleName(int index) const
{
	if (index < 0 || index >= m_graph.nodes.size()) {
		return {};
	}
	return m_graph.nodes.at(index).title;
}

QString MaterialGraphView::nodeAccessibleDescription(int index) const
{
	if (index < 0 || index >= m_graph.nodes.size()) {
		return {};
	}
	const MaterialGraphNode& node = m_graph.nodes.at(index);
	QStringList parts;
	if (!node.subtitle.isEmpty()) {
		parts << node.subtitle;
	}
	if (node.inactive) {
		parts << tr("Not drawn by the engine.");
	}
	QStringList feeds;
	QStringList fedBy;
	for (const MaterialGraphLink& link : m_graph.links) {
		if (link.fromNode == node.id) {
			if (const MaterialGraphNode* target = m_graph.node(link.toNode)) {
				feeds << target->title;
			}
		} else if (link.toNode == node.id) {
			if (const MaterialGraphNode* source = m_graph.node(link.fromNode)) {
				fedBy << source->title;
			}
		}
	}
	if (!fedBy.isEmpty()) {
		parts << tr("Takes: %1.").arg(fedBy.join(QStringLiteral(", ")));
	}
	if (!feeds.isEmpty()) {
		parts << tr("Feeds: %1.").arg(feeds.join(QStringLiteral(", ")));
	}
	if (node.line > 0) {
		parts << tr("Line %1.").arg(node.line);
	}
	return parts.join(QLatin1Char(' '));
}

QAccessibleInterface* MaterialGraphView::accessibleChild(int index) const
{
	if (index < 0 || index >= m_graph.nodes.size()) {
		return nullptr;
	}
	const auto found = m_accessibleChildren.constFind(index);
	if (found != m_accessibleChildren.constEnd()) {
		if (QAccessibleInterface* existing = QAccessible::accessibleInterface(found.value())) {
			return existing;
		}
	}
	auto* child = new MaterialGraphNodeAccessible(const_cast<MaterialGraphView*>(this), index);
	m_accessibleChildren.insert(index, QAccessible::registerAccessibleInterface(child));
	return child;
}

int MaterialGraphView::accessibleChildIndex(const QAccessibleInterface* child) const
{
	for (auto it = m_accessibleChildren.constBegin(); it != m_accessibleChildren.constEnd(); ++it) {
		if (QAccessible::accessibleInterface(it.value()) == child) {
			return it.key();
		}
	}
	return -1;
}

void MaterialGraphView::resetAccessibleChildren()
{
	for (const QAccessible::Id id : std::as_const(m_accessibleChildren)) {
		QAccessible::deleteAccessibleInterface(id);
	}
	m_accessibleChildren.clear();
}

void MaterialGraphView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	const StudioThemeTokens& tokens = currentStudioTheme();
	const StudioThemeColors& colors = tokens.colors;
	painter.fillRect(rect(), colors.input);
	if (m_graph.nodes.isEmpty()) {
		painter.setPen(colors.textMuted);
		painter.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_message);
	} else {
		// A dot grid, except in the high-visibility themes.
		const double spacing = 24.0 * m_zoom;
		if (!tokens.highContrast && spacing >= 10.0) {
			painter.setPen(QPen(colors.borderSubtle, 1.5));
			const double startX = std::fmod(m_pan.x(), spacing);
			const double startY = std::fmod(m_pan.y(), spacing);
			for (double x = startX; x < width(); x += spacing) {
				for (double y = startY; y < height(); y += spacing) {
					painter.drawPoint(QPointF(x, y));
				}
			}
		}
		painter.save();
		painter.translate(m_pan);
		painter.scale(m_zoom, m_zoom);
		const bool rtl = rightToLeft();

		// Wires first, under the nodes.
		for (const MaterialGraphLink& link : m_graph.links) {
			const int from = m_graph.indexOf(link.fromNode);
			const int to = m_graph.indexOf(link.toNode);
			if (from < 0 || to < 0) {
				continue;
			}
			const int fromPort = portIndex(from, link.fromPort, true);
			const int toPort = portIndex(to, link.toPort, false);
			const QPointF start = portPoint(from, fromPort, true);
			const QPointF end = portPoint(to, toPort, false);
			const double reach = std::max(40.0, std::abs(end.x() - start.x()) * 0.5);
			const double direction = rtl ? -1.0 : 1.0;
			QPainterPath path(start);
			path.cubicTo(start + QPointF(reach * direction, 0), end - QPointF(reach * direction, 0), end);
			const MaterialGraphPortType type =
				fromPort >= 0 ? m_graph.nodes.at(from).outputs.at(fromPort).type : MaterialGraphPortType::Scalar;
			QPen pen(portColor(type, colors), (from == m_selected || to == m_selected) ? 3.0 : 2.0);
			pen.setCosmetic(true);
			if (m_graph.nodes.at(from).inactive || m_graph.nodes.at(to).inactive) {
				pen.setStyle(Qt::DashLine);
			}
			painter.setPen(pen);
			painter.setBrush(Qt::NoBrush);
			painter.drawPath(path);
		}

		QFont body = font();
		body.setPixelSize(kFontPixels);
		QFont title = body;
		title.setBold(true);
		const QFontMetricsF bodyMetrics(body);
		const QFontMetricsF titleMetrics(title);
		// Sides are worked out here already: AlignAbsolute stops the painter
		// mirroring them again right to left.
		const Qt::Alignment leading = (rtl ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute;
		const Qt::Alignment trailing = (rtl ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignAbsolute;
		for (int index = 0; index < m_graph.nodes.size(); ++index) {
			const MaterialGraphNode& node = m_graph.nodes.at(index);
			const QRectF box = sceneRect(index);
			painter.setOpacity(node.inactive ? 0.6 : 1.0);
			QPen outline(index == m_selected ? colors.focus : (index == m_hover ? colors.borderStrong : colors.border),
				index == m_selected ? std::max(2.0, tokens.metrics.focusWidth * 2.0) : 1.0);
			outline.setCosmetic(true);
			if (node.inactive) {
				outline.setStyle(Qt::DashLine);
			}
			painter.setPen(outline);
			painter.setBrush(colors.panelRaised);
			painter.drawRoundedRect(box, 6, 6);
			// The kind's colour as a strip on the leading edge.
			painter.setPen(Qt::NoPen);
			painter.setBrush(kindColor(node.kind, colors));
			painter.drawRoundedRect(QRectF(rtl ? box.right() - 6 : box.left() + 2, box.top() + 5, 4, kHeader - 10), 2, 2);

			painter.setFont(title);
			painter.setPen(colors.text);
			const QRectF titleBox = box.adjusted(rtl ? 8 : 12, 0, rtl ? -12 : -8, 0);
			painter.drawText(QRectF(titleBox.left(), box.top(), titleBox.width(), kHeader), Qt::AlignVCenter | leading,
				titleMetrics.elidedText(node.title, Qt::ElideRight, titleBox.width()));
			double y = box.top() + kHeader;
			painter.setFont(body);
			if (!node.subtitle.isEmpty() || node.inactive) {
				QString subtitle = node.subtitle;
				if (node.inactive) {
					subtitle = subtitle.isEmpty() ? tr("Not drawn") : tr("Not drawn: %1").arg(subtitle);
				}
				painter.setPen(colors.textMuted);
				painter.drawText(QRectF(titleBox.left(), y - 4, titleBox.width(), kSubtitle), Qt::AlignVCenter | leading,
					bodyMetrics.elidedText(subtitle, Qt::ElideRight, titleBox.width()));
				y += kSubtitle;
			}
			const double half = (box.width() - 24) / 2.0;
			for (int port = 0; port < node.inputs.size(); ++port) {
				const QPointF point = portPoint(index, port, false);
				drawPort(painter, point, node.inputs.at(port).type, portColor(node.inputs.at(port).type, colors), colors.panelRaised);
				painter.setPen(colors.text);
				const QRectF label(rtl ? point.x() - 10 - half : point.x() + 10, point.y() - kPortRow / 2, half, kPortRow);
				painter.drawText(label, Qt::AlignVCenter | leading, bodyMetrics.elidedText(node.inputs.at(port).label, Qt::ElideRight, half));
			}
			for (int port = 0; port < node.outputs.size(); ++port) {
				const QPointF point = portPoint(index, port, true);
				drawPort(painter, point, node.outputs.at(port).type, portColor(node.outputs.at(port).type, colors), colors.panelRaised);
				painter.setPen(colors.text);
				const QRectF label(rtl ? point.x() + 10 : point.x() - 10 - half, point.y() - kPortRow / 2, half, kPortRow);
				painter.drawText(label, Qt::AlignVCenter | trailing, bodyMetrics.elidedText(node.outputs.at(port).label, Qt::ElideRight, half));
			}
			y += std::max(node.inputs.size(), node.outputs.size()) * kPortRow;
			const auto thumbnail = m_thumbnails.constFind(node.imageReference);
			if (!node.imageReference.isEmpty() && thumbnail != m_thumbnails.constEnd() && !thumbnail->isNull()) {
				const double side = kThumbnail - 6;
				const QRectF picture(box.center().x() - side / 2, y + 2, side, side);
				painter.drawImage(picture, *thumbnail);
				painter.setPen(QPen(colors.border, 1));
				painter.setBrush(Qt::NoBrush);
				painter.drawRect(picture);
			}
		}
		painter.setOpacity(1.0);
		painter.restore();
	}
	if (hasFocus()) {
		QPen pen(colors.focus, std::max(2, tokens.metrics.focusWidth * 2));
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(rect().adjusted(1, 1, -2, -2));
	}
}

void MaterialGraphView::mousePressEvent(QMouseEvent* event)
{
	setFocus(Qt::MouseFocusReason);
	const QPointF position = event->position();
	if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && nodeAt(position) < 0)) {
		m_drag = Drag::Pan;
		m_dragOrigin = position;
		m_dragStart = m_pan;
		m_dragMoved = false;
		if (event->button() == Qt::LeftButton) {
			selectIndex(-1);
		}
		setCursor(Qt::ClosedHandCursor);
		event->accept();
		return;
	}
	if (event->button() == Qt::LeftButton) {
		const int index = nodeAt(position);
		selectIndex(index);
		m_drag = Drag::Node;
		m_dragOrigin = position;
		m_dragStart = nodePosition(index);
		m_dragMoved = false;
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void MaterialGraphView::mouseMoveEvent(QMouseEvent* event)
{
	const QPointF position = event->position();
	if (m_drag == Drag::Pan) {
		m_pan = m_dragStart + (position - m_dragOrigin);
		m_dragMoved = true;
		update();
		event->accept();
		return;
	}
	if (m_drag == Drag::Node && m_selected >= 0) {
		QPointF delta = (position - m_dragOrigin) / m_zoom;
		if (!m_dragMoved && std::hypot(delta.x(), delta.y()) * m_zoom < 3.0) {
			return;
		}
		m_dragMoved = true;
		// Positions are kept left to right; mirror the drag back.
		if (rightToLeft()) {
			delta.rx() = -delta.x();
		}
		m_moved.insert(m_graph.nodes.at(m_selected).id, m_dragStart + delta);
		update();
		event->accept();
		return;
	}
	const int hover = nodeAt(position);
	if (hover != m_hover) {
		m_hover = hover;
		update();
	}
	QWidget::mouseMoveEvent(event);
}

void MaterialGraphView::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_drag != Drag::None) {
		m_drag = Drag::None;
		unsetCursor();
		event->accept();
		return;
	}
	QWidget::mouseReleaseEvent(event);
}

void MaterialGraphView::mouseDoubleClickEvent(QMouseEvent* event)
{
	const int index = nodeAt(event->position());
	if (index >= 0) {
		const MaterialGraphNode& node = m_graph.nodes.at(index);
		if (node.line > 0) {
			Q_EMIT showTextRequested(node.line);
		} else if (!node.imageReference.isEmpty()) {
			Q_EMIT showImageRequested(node.imageReference);
		}
		event->accept();
		return;
	}
	fitToView();
	event->accept();
}

void MaterialGraphView::wheelEvent(QWheelEvent* event)
{
	const double steps = event->angleDelta().y() / 120.0;
	if (steps == 0.0) {
		QWidget::wheelEvent(event);
		return;
	}
	const QPointF anchor = event->position();
	const QPointF scene = toScene(anchor);
	m_zoom = std::clamp(m_zoom * std::pow(1.12, steps), 0.2, 3.0 * textZoom());
	m_pan = anchor - scene * m_zoom;
	update();
	event->accept();
}

void MaterialGraphView::moveSelection(int key)
{
	if (m_graph.nodes.isEmpty()) {
		return;
	}
	if (m_selected < 0) {
		const int output = m_graph.indexOf(QStringLiteral("material"));
		selectIndex(output >= 0 ? output : 0);
		return;
	}
	const QRectF current = sceneRect(m_selected);
	const QString id = m_graph.nodes.at(m_selected).id;
	// Left and right follow the wires: towards what feeds this node, or what
	// it feeds. Right to left the flow is mirrored, and so are the keys.
	const bool upstream = (key == Qt::Key_Left) != rightToLeft();
	int best = -1;
	double bestDistance = std::numeric_limits<double>::max();
	if (key == Qt::Key_Left || key == Qt::Key_Right) {
		for (const MaterialGraphLink& link : m_graph.links) {
			const QString other = upstream ? (link.toNode == id ? link.fromNode : QString()) : (link.fromNode == id ? link.toNode : QString());
			const int index = m_graph.indexOf(other);
			if (index < 0) {
				continue;
			}
			const double distance = std::abs(sceneRect(index).center().y() - current.center().y());
			if (distance < bestDistance) {
				best = index;
				bestDistance = distance;
			}
		}
	}
	if (best < 0) {
		// Otherwise the nearest node that way.
		for (int index = 0; index < m_graph.nodes.size(); ++index) {
			if (index == m_selected) {
				continue;
			}
			const QPointF delta = sceneRect(index).center() - current.center();
			bool ahead = false;
			double along = 0.0;
			double across = 0.0;
			switch (key) {
			case Qt::Key_Left:
				ahead = delta.x() < -1.0;
				along = -delta.x();
				across = std::abs(delta.y());
				break;
			case Qt::Key_Right:
				ahead = delta.x() > 1.0;
				along = delta.x();
				across = std::abs(delta.y());
				break;
			case Qt::Key_Up:
				ahead = delta.y() < -1.0;
				along = -delta.y();
				across = std::abs(delta.x());
				break;
			case Qt::Key_Down:
				ahead = delta.y() > 1.0;
				along = delta.y();
				across = std::abs(delta.x());
				break;
			default:
				break;
			}
			if (!ahead) {
				continue;
			}
			const double distance = along + across * 2.0;
			if (distance < bestDistance) {
				best = index;
				bestDistance = distance;
			}
		}
	}
	if (best >= 0) {
		selectIndex(best);
	}
}

void MaterialGraphView::requestEdit(MaterialGraphEditKind kind, int index, const QString& value, int step, const QString& nodeTemplate)
{
	if (m_readOnly) {
		return;
	}
	MaterialGraphEdit edit;
	edit.kind = kind;
	edit.node = index >= 0 && index < m_graph.nodes.size() ? m_graph.nodes.at(index).id : QStringLiteral("material");
	edit.value = value;
	edit.step = step;
	edit.nodeTemplate = nodeTemplate;
	Q_EMIT editRequested(edit);
}

void MaterialGraphView::keyPressEvent(QKeyEvent* event)
{
	const bool alt = event->modifiers() & Qt::AltModifier;
	const bool control = event->modifiers() & Qt::ControlModifier;
	const MaterialGraphNode* node = m_selected >= 0 ? &m_graph.nodes.at(m_selected) : nullptr;
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_Right:
		moveSelection(event->key());
		break;
	case Qt::Key_Up:
	case Qt::Key_Down:
		if (alt) {
			if (node && node->reorderable) {
				requestEdit(MaterialGraphEditKind::MoveNode, m_selected, QString(), event->key() == Qt::Key_Up ? -1 : 1);
			}
		} else {
			moveSelection(event->key());
		}
		break;
	case Qt::Key_Home:
		selectNode(QStringLiteral("material"));
		break;
	case Qt::Key_Delete:
	case Qt::Key_Backspace:
		if (node && node->removable) {
			requestEdit(MaterialGraphEditKind::RemoveNode, m_selected);
		}
		break;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (node) {
			Q_EMIT nodeActivated(node->id);
		}
		break;
	case Qt::Key_Menu: {
		const QRect box = nodeRect(m_selected);
		showMenu(mapToGlobal(box.isNull() ? rect().center() : box.center()), m_selected);
		break;
	}
	case Qt::Key_F10:
		if (event->modifiers() & Qt::ShiftModifier) {
			const QRect box = nodeRect(m_selected);
			showMenu(mapToGlobal(box.isNull() ? rect().center() : box.center()), m_selected);
		} else {
			QWidget::keyPressEvent(event);
			return;
		}
		break;
	case Qt::Key_0:
		if (control) {
			fitToView();
		} else {
			QWidget::keyPressEvent(event);
			return;
		}
		break;
	case Qt::Key_F:
		fitToView();
		break;
	case Qt::Key_Plus:
	case Qt::Key_Equal:
		zoomBy(1.15);
		break;
	case Qt::Key_Minus:
		zoomBy(1.0 / 1.15);
		break;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	event->accept();
}

void MaterialGraphView::contextMenuEvent(QContextMenuEvent* event)
{
	int index = event->reason() == QContextMenuEvent::Mouse ? nodeAt(event->pos()) : m_selected;
	if (event->reason() == QContextMenuEvent::Mouse) {
		selectIndex(index);
	}
	showMenu(event->globalPos(), index);
	event->accept();
}

void MaterialGraphView::showMenu(const QPoint& globalPosition, int index)
{
	QMenu menu(this);
	menu.setAccessibleName(tr("Node actions"));
	const MaterialGraphNode* node = index >= 0 && index < m_graph.nodes.size() ? &m_graph.nodes.at(index) : nullptr;
	if (!m_readOnly && !m_templates.isEmpty()) {
		QMenu* add = menu.addMenu(tr("Add Node"));
		QHash<QString, QMenu*> categories;
		for (const MaterialGraphNodeTemplate& each : std::as_const(m_templates)) {
			QMenu* target = categories.value(each.category);
			if (!target) {
				target = add->addMenu(each.category);
				categories.insert(each.category, target);
			}
			QAction* action = target->addAction(each.title);
			action->setToolTip(each.description);
			action->setStatusTip(each.description);
			const bool needsStage = each.needsStage;
			const int stage = node ? node->stage : -1;
			action->setEnabled(!needsStage || stage >= 0);
			const QString id = each.id;
			connect(action, &QAction::triggered, this, [this, needsStage, stage, id]() {
				if (m_readOnly) {
					return;
				}
				MaterialGraphEdit edit;
				edit.kind = MaterialGraphEditKind::AddNode;
				edit.nodeTemplate = id;
				edit.node = needsStage ? QStringLiteral("stage/%1").arg(stage) : QStringLiteral("material");
				Q_EMIT editRequested(edit);
			});
		}
		add->setToolTipsVisible(true);
		for (QMenu* each : std::as_const(categories)) {
			each->setToolTipsVisible(true);
		}
	}
	if (node && !m_readOnly) {
		if (node->removable) {
			QAction* remove = menu.addAction(tr("Remove"));
			remove->setShortcut(QKeySequence::Delete);
			connect(remove, &QAction::triggered, this, [this, index]() { requestEdit(MaterialGraphEditKind::RemoveNode, index); });
		}
		if (node->reorderable) {
			QAction* earlier = menu.addAction(tr("Move Earlier"));
			earlier->setShortcut(QKeySequence(Qt::AltModifier | Qt::Key_Up));
			connect(earlier, &QAction::triggered, this, [this, index]() { requestEdit(MaterialGraphEditKind::MoveNode, index, QString(), -1); });
			QAction* later = menu.addAction(tr("Move Later"));
			later->setShortcut(QKeySequence(Qt::AltModifier | Qt::Key_Down));
			connect(later, &QAction::triggered, this, [this, index]() { requestEdit(MaterialGraphEditKind::MoveNode, index, QString(), 1); });
		}
		if (node->kind == QStringLiteral("expression")) {
			QMenu* wrap = menu.addMenu(tr("Wrap In Operator"));
			const QVector<std::pair<QString, QString>> operators {{QStringLiteral("add"), tr("Add (+)")},
				{QStringLiteral("subtract"), tr("Subtract (-)")}, {QStringLiteral("multiply"), tr("Multiply (*)")},
				{QStringLiteral("divide"), tr("Divide (/)")}, {QStringLiteral("modulo"), tr("Modulo (%)")},
				{QStringLiteral("greater"), tr("Greater (>)")}, {QStringLiteral("less"), tr("Less (<)")},
				{QStringLiteral("equal"), tr("Equal (==)")}, {QStringLiteral("notEqual"), tr("Not Equal (!=)")},
				{QStringLiteral("and"), tr("And (&&)")}, {QStringLiteral("or"), tr("Or (||)")}};
			for (const auto& [id, label] : operators) {
				QAction* action = wrap->addAction(label);
				const QString op = id;
				connect(action, &QAction::triggered, this, [this, index, op]() { requestEdit(MaterialGraphEditKind::WrapExpression, index, op); });
			}
			QAction* unwrap = menu.addAction(tr("Unwrap"));
			connect(unwrap, &QAction::triggered, this, [this, index]() { requestEdit(MaterialGraphEditKind::UnwrapExpression, index); });
		}
		if (node->kind == QStringLiteral("stage") && node->property(QStringLiteral("shorthand"))) {
			QAction* expand = menu.addAction(tr("Expand to Stage Block"));
			connect(expand, &QAction::triggered, this, [this, index]() { requestEdit(MaterialGraphEditKind::ExpandShorthand, index); });
		}
	}
	if (node) {
		if (!menu.isEmpty()) {
			menu.addSeparator();
		}
		if (node->line > 0) {
			const int line = node->line;
			QAction* text = menu.addAction(tr("Show in Text"));
			connect(text, &QAction::triggered, this, [this, line]() { Q_EMIT showTextRequested(line); });
		}
		if (!node->imageReference.isEmpty()) {
			const QString reference = node->imageReference;
			QAction* image = menu.addAction(tr("Show Image in Textures"));
			connect(image, &QAction::triggered, this, [this, reference]() { Q_EMIT showImageRequested(reference); });
		}
	}
	if (!menu.isEmpty()) {
		menu.addSeparator();
	}
	QAction* fit = menu.addAction(tr("Fit Graph"));
	fit->setShortcut(QKeySequence(Qt::ControlModifier | Qt::Key_0));
	connect(fit, &QAction::triggered, this, &MaterialGraphView::fitToView);
	menu.exec(globalPosition);
}

void MaterialGraphView::focusInEvent(QFocusEvent* event)
{
	QWidget::focusInEvent(event);
	if (m_selected < 0 && !m_graph.nodes.isEmpty()) {
		const int output = m_graph.indexOf(QStringLiteral("material"));
		selectIndex(output >= 0 ? output : 0);
	} else if (m_selected >= 0) {
		QAccessibleEvent focus(this, QAccessible::Focus);
		focus.setChild(m_selected);
		QAccessible::updateAccessibility(&focus);
	}
	update();
}

void MaterialGraphView::focusOutEvent(QFocusEvent* event)
{
	QWidget::focusOutEvent(event);
	update();
}

void MaterialGraphView::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	if (m_fitPending) {
		fitToView();
	}
}

void MaterialGraphView::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	switch (event->type()) {
	case QEvent::LayoutDirectionChange:
		m_fitPending = true;
		fitToView();
		break;
	case QEvent::LanguageChange:
		setAccessibleName(tr("Material node graph"));
		setAccessibleDescription(tr("Arrow keys move between connected nodes; Enter edits the node's properties; Delete removes it; "
									"Alt+Up and Alt+Down change the draw order; the context menu adds nodes."));
		break;
	case QEvent::PaletteChange:
	case QEvent::StyleChange:
		update();
		break;
	default:
		break;
	}
}

bool MaterialGraphView::event(QEvent* event)
{
	if (event->type() == QEvent::ToolTip) {
		auto* help = static_cast<QHelpEvent*>(event);
		const int index = nodeAt(help->pos());
		if (index < 0) {
			QToolTip::hideText();
			event->ignore();
			return true;
		}
		const MaterialGraphNode& node = m_graph.nodes.at(index);
		const QPointF scene = toScene(help->pos());
		QString text;
		for (int port = 0; port < node.inputs.size() && text.isEmpty(); ++port) {
			if (QLineF(portPoint(index, port, false), scene).length() <= 8.0) {
				text = tr("Input %1 (%2)").arg(node.inputs.at(port).label, portTypeName(node.inputs.at(port).type));
			}
		}
		for (int port = 0; port < node.outputs.size() && text.isEmpty(); ++port) {
			if (QLineF(portPoint(index, port, true), scene).length() <= 8.0) {
				text = tr("Output %1 (%2)").arg(node.outputs.at(port).label, portTypeName(node.outputs.at(port).type));
			}
		}
		if (text.isEmpty()) {
			text = node.title;
			const QString description = nodeAccessibleDescription(index);
			if (!description.isEmpty()) {
				text += QLatin1Char('\n') + description;
			}
		}
		QToolTip::showText(help->globalPos(), text, this, nodeRect(index));
		return true;
	}
	return QWidget::event(event);
}

} // namespace vibestudio
