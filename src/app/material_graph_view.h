#pragma once

// The node editor for a material: core/material_graph.h laid out as boxes
// and wires. Images and coordinate, colour and geometry nodes feed stages,
// which feed the material's output in draw order. Every edit made here is a
// MaterialGraphEdit the workbench turns into text edits, so the text and the
// graph never disagree.

#include "core/material_graph.h"

#include <QAccessible>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QWidget>

namespace vibestudio {

class MaterialGraphView final : public QWidget {
	Q_OBJECT

public:
	explicit MaterialGraphView(QWidget* parent = nullptr);
	~MaterialGraphView() override;

	// Shows a graph. Nodes the user dragged keep their place while
	// `layoutKey` (the material's name) stays the same.
	void setGraph(const MaterialGraph& graph, const QString& layoutKey);
	void clearGraph(const QString& message);
	[[nodiscard]] const MaterialGraph& graph() const { return m_graph; }
	// Small pictures for image nodes, by image reference.
	void setThumbnails(const QHash<QString, QImage>& thumbnails);
	// What the Add Node menu offers.
	void setNodeTemplates(const QVector<MaterialGraphNodeTemplate>& templates);
	void setReadOnly(bool readOnly);
	[[nodiscard]] bool isReadOnly() const { return m_readOnly; }

	[[nodiscard]] QString selectedNode() const;
	void selectNode(const QString& nodeId);
	void selectIndex(int index);
	[[nodiscard]] int selectedIndex() const { return m_selected; }
	void fitToView();
	void zoomBy(double factor);

	// For screen readers and tests.
	[[nodiscard]] int nodeCount() const { return static_cast<int>(m_graph.nodes.size()); }
	[[nodiscard]] QRect nodeRect(int index) const;
	[[nodiscard]] QString nodeAccessibleName(int index) const;
	[[nodiscard]] QString nodeAccessibleDescription(int index) const;
	QAccessibleInterface* accessibleChild(int index) const;
	[[nodiscard]] int accessibleChildIndex(const QAccessibleInterface* child) const;

Q_SIGNALS:
	void selectionChanged(const QString& nodeId);
	void editRequested(const MaterialGraphEdit& edit);
	// Enter on a node: the workbench moves focus to its properties.
	void nodeActivated(const QString& nodeId);
	void showImageRequested(const QString& reference);
	void showTextRequested(int line);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void changeEvent(QEvent* event) override;
	bool event(QEvent* event) override;

private:
	[[nodiscard]] QSizeF nodeSize(int index) const;
	[[nodiscard]] QPointF nodePosition(int index) const;
	[[nodiscard]] QRectF sceneRect(int index) const;
	[[nodiscard]] QPointF portPoint(int index, int port, bool output) const;
	[[nodiscard]] int portIndex(int node, const QString& portId, bool output) const;
	[[nodiscard]] QPointF toWidget(const QPointF& scene) const;
	[[nodiscard]] QPointF toScene(const QPointF& widget) const;
	[[nodiscard]] int nodeAt(const QPointF& widget) const;
	[[nodiscard]] QRectF contentsRect() const;
	[[nodiscard]] bool rightToLeft() const { return layoutDirection() == Qt::RightToLeft; }
	void ensureVisible(int index);
	void moveSelection(int key);
	void showMenu(const QPoint& globalPosition, int index);
	void requestEdit(MaterialGraphEditKind kind, int index, const QString& value = QString(), int step = 0, const QString& nodeTemplate = QString());
	void notifySelection();
	void resetAccessibleChildren();

	MaterialGraph m_graph;
	QString m_layoutKey;
	// Where the user dragged nodes, by node id, for the current layout key.
	QHash<QString, QPointF> m_moved;
	QHash<QString, QImage> m_thumbnails;
	QVector<MaterialGraphNodeTemplate> m_templates;
	QString m_message;
	bool m_readOnly = false;
	int m_selected = -1;
	int m_hover = -1;
	QPointF m_pan;
	double m_zoom = 1.0;
	bool m_fitPending = true;

	enum class Drag {
		None,
		Pan,
		Node,
	};
	Drag m_drag = Drag::None;
	QPointF m_dragOrigin;
	QPointF m_dragStart;
	bool m_dragMoved = false;

	mutable QHash<int, QAccessible::Id> m_accessibleChildren;
};

} // namespace vibestudio
