#pragma once

// Interactive 2D map viewport.
//
// Draws real geometry for Doom-family maps (vertices, linedefs, sector fills,
// things) and orthographic brush/patch outlines for Quake-family maps, using
// the shared solver in core/map_geometry so the picture matches map statistics
// and the headless SVG renderer.

#include "core/level_map.h"
#include "core/map_geometry.h"

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace vibestudio {

enum class MapViewportProjection {
	TopXY,
	FrontXZ,
	SideZY,
};

struct MapViewportHit {
	LevelMapSelectionKind kind = LevelMapSelectionKind::None;
	int objectId = -1;
	QString label;
	double worldX = 0.0;
	double worldY = 0.0;
};

class MapViewport final : public QWidget {
	Q_OBJECT

public:
	explicit MapViewport(QWidget* parent = nullptr);
	~MapViewport() override;

	void setDocument(const LevelMapDocument& document);
	void clearDocument();
	[[nodiscard]] bool hasDocument() const;

	void setProjection(MapViewportProjection projection);
	[[nodiscard]] MapViewportProjection projection() const;

	void setSelection(LevelMapSelectionKind kind, int objectId);
	[[nodiscard]] LevelMapSelectionKind selectionKind() const;
	[[nodiscard]] int selectedObjectId() const;

	void setGridSize(int units);
	[[nodiscard]] int gridSize() const;
	void setShowGrid(bool show);
	void setShowThings(bool show);
	void setShowSectorFill(bool show);
	void setShowVertices(bool show);
	void setShowLabels(bool show);
	void setHighContrast(bool enabled);
	void setReducedMotion(bool enabled);

	void zoomToFit();
	void zoomIn();
	void zoomOut();
	void resetView();
	[[nodiscard]] double zoom() const;

	[[nodiscard]] QString hoverSummary() const;
	[[nodiscard]] QStringList statusLines() const;
	[[nodiscard]] QString accessibleSummary() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void selectionChanged(int selectionKind, int objectId);
	void hoverChanged(const QString& summary);
	void viewChanged();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	struct Palette;

	void rebuildGeometry();
	void updateWorldBounds();
	[[nodiscard]] QPointF worldToView(double x, double y) const;
	[[nodiscard]] QPointF viewToWorld(const QPointF& point) const;
	[[nodiscard]] QPointF projectPoint(const LevelMapVec3& point) const;
	[[nodiscard]] MapViewportHit hitTest(const QPointF& viewPoint) const;
	void paintGrid(QPainter& painter, const Palette& palette) const;
	void paintDoom(QPainter& painter, const Palette& palette) const;
	void paintQuake(QPainter& painter, const Palette& palette) const;
	void paintEmptyState(QPainter& painter, const Palette& palette) const;
	void paintOverlay(QPainter& painter, const Palette& palette) const;
	void announceSelection();

	LevelMapDocument m_document;
	bool m_hasDocument = false;
	QVector<MapBrushGeometry> m_brushGeometry;
	QVector<DoomSectorOutline> m_sectorOutlines;
	MapViewportProjection m_projection = MapViewportProjection::TopXY;
	LevelMapSelectionKind m_selectionKind = LevelMapSelectionKind::None;
	int m_selectedObjectId = -1;
	MapViewportHit m_hover;
	QRectF m_worldBounds;
	QPointF m_worldCenter;
	double m_zoom = 1.0;
	int m_gridSize = 64;
	bool m_showGrid = true;
	bool m_showThings = true;
	bool m_showSectorFill = true;
	bool m_showVertices = false;
	bool m_showLabels = false;
	bool m_highContrast = false;
	bool m_reducedMotion = false;
	bool m_panning = false;
	QPointF m_panAnchorView;
	QPointF m_panAnchorCenter;
};

QString mapViewportProjectionDisplayName(MapViewportProjection projection);

} // namespace vibestudio
