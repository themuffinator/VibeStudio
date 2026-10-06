#pragma once

#include "core/texture_document.h"

#include <QWidget>
#include <functional>

namespace vibestudio {

// Input stays inside this focused widget. Automation calls the document API.
class TextureCanvas final : public QWidget {
	Q_OBJECT
public:
	enum class Tool { Pencil, Eraser, Fill, Pick, Select, Brush, Line, Rectangle, Ellipse };
	explicit TextureCanvas(TextureDocument* document, QWidget* parent = nullptr);
	std::function<void()> changed;
	std::function<void(QPoint)> fillRequested;
	std::function<void(TextureShape, QPoint, QPoint, const TextureBrush&, bool)> shapeRequested;
	std::function<void(QColor)> colorPicked;
	std::function<void(QRect)> selectionChanged;
	std::function<void(const QString&)> statusChanged;
	std::function<void(const QString&)> operationFailed;
	void setTool(Tool tool);
	void setColor(QColor color) { m_color = color; }
	void setBrushWidth(int width) { m_brushWidth = width; }
	void setBrushShape(TextureBrushShape shape) { finishStroke(); m_brushShape = shape; }
	void setPaintMode(TexturePaintMode mode) { finishStroke(); m_paintMode = mode; }
	void setWrappedPainting(bool wrap) { finishStroke(); m_wrap = wrap; }
	void setFilledShapes(bool filled) { finishStroke(); m_filledShapes = filled; }
	void setTiled(bool tiled) { finishStroke(); m_tiled = tiled; update(); }
	void setGrid(bool grid) { m_grid = grid; update(); }
	void setHighContrast(bool highContrast) { m_highContrast = highContrast; update(); }
	void setZoom(double zoom);
	void setZoomAt(double zoom, QPointF anchor);
	[[nodiscard]] QPointF imagePositionAt(QPointF point) const;
	void fit();
	void refresh();
	void finishStroke();
	[[nodiscard]] double zoom() const { return m_zoom; }
	[[nodiscard]] QRect selection() const { return m_selection; }
	[[nodiscard]] QPoint cursorPixel() const { return m_pixel; }
	void setCursorPixel(QPoint pixel);
	void setSelection(QRect rectangle);
	void applyAtCursor();
	// Shared gesture commands used by this widget's input and direct command tests.
	void beginGesture(QPoint point);
	void continueGesture(QPoint point);
	void endGesture();
	void cancelGesture();
	[[nodiscard]] bool hasPendingShape() const { return m_drawing; }
	QSize minimumSizeHint() const override { return {240, 240}; }
	QSize sizeHint() const override { return {650, 600}; }

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void wheelEvent(QWheelEvent*) override;
	void keyPressEvent(QKeyEvent*) override;
	void focusOutEvent(QFocusEvent*) override;

private:
	QRectF imageRect() const;
	QPoint pixelAt(QPointF point) const;
	void reportPixel();
	[[nodiscard]] TextureBrush currentBrush() const;
	[[nodiscard]] bool shapeTool() const;
	[[nodiscard]] QPoint boundedPoint(QPoint point) const;
	void submitShape();
	TextureDocument* m_document;
	Tool m_tool = Tool::Pencil;
	QColor m_color = Qt::white;
	int m_brushWidth = 1;
	TextureBrushShape m_brushShape = TextureBrushShape::Round;
	TexturePaintMode m_paintMode = TexturePaintMode::SourceOver;
	bool m_wrap = false, m_filledShapes = false, m_drawing = false;
	double m_zoom = 4;
	bool m_grid = true, m_tiled = false, m_highContrast = false, m_stroke = false, m_selecting = false, m_panning = false;
	QPoint m_pixel{0, 0}, m_selectionStart;
	QPoint m_shapeStart, m_shapeEnd;
	QRect m_selection;
	QPointF m_offset, m_panStart;
};

} // namespace vibestudio
