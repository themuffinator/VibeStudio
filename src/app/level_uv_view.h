#pragma once

#include "core/level_uv_edit.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

class QMenu;

namespace vibestudio {

// The UV Editor's texture-space view: one brush face's winding or one
// patch's control grid drawn over its material image, tiled as the map
// repeats it. A face's points move its whole texture: dragging a point moves
// the texture so the point follows the pointer, and the point dragged last
// becomes the anchor, about which dragging any other point turns the
// texture, as NetRadiant Custom's TexTool does. A patch's points move one by
// one, or as a selection. Every change is checked by the shared service
// (core/level_uv_edit.h) and drawn as the map will read it.
//
// Mouse: left-drag a point (Shift snaps a turn to the turn step), left-drag
// inside a face to move it, left-drag across a patch to select points
// (Ctrl adds), right- or middle-drag to pan, the wheel to zoom; right-click
// for a menu. Keys: arrows nudge by the snap step (Shift eight times as far),
// Ctrl+arrows pan, [ and ] turn by the turn step ({ and } by one degree), +,
// - and Insert and Delete zoom, Home or F frames, N and Shift+N step through
// the points, A sets or clears the anchor at the current point, Ctrl+A
// selects every patch point, Ctrl+Z and Ctrl+Shift+Z undo and redo, Escape
// cancels a drag.
class LevelUvView final : public QWidget {
	Q_OBJECT
  public:
	explicit LevelUvView(QWidget* parent = nullptr);
	~LevelUvView() override;
	// Starts again from `layout`, with no edit. `image` may be null: a
	// checker then shows the repeats.
	void setSource(const LevelUvLayout& layout, const QImage& image);
	void setImage(const QImage& image);
	[[nodiscard]] bool hasSource() const { return m_hasSource; }
	[[nodiscard]] const LevelUvLayout& layout() const { return m_layout; }
	[[nodiscard]] const LevelUvEdit& edit() const { return m_edit; }
	[[nodiscard]] const LevelUvResult& result() const { return m_result; }
	// The last refused change, or empty.
	[[nodiscard]] QString error() const { return m_error; }
	// Whether the edit changes the surface.
	[[nodiscard]] bool changed() const { return m_result.fit != LevelUvFit::Unchanged; }
	void setAllowValve220(bool allow);
	// Texels a drag snaps to and a nudge moves; 0 turns snapping off (a nudge
	// then moves one texel).
	void setSnapStep(double texels);
	[[nodiscard]] double snapStep() const { return m_snap; }
	void setAngleStep(double degrees);
	[[nodiscard]] double angleStep() const { return m_angleStep; }

	[[nodiscard]] int anchor() const { return m_anchor; }
	// -1 clears the anchor.
	bool setAnchor(int point);
	[[nodiscard]] int currentPoint() const { return m_current; }
	void setCurrentPoint(int point);
	[[nodiscard]] QVector<int> selectedPoints() const { return m_selected; }
	void setSelectedPoints(const QVector<int>& points);

	// The edits the keys, buttons and menu make, each one undo step here.
	// A face moves or turns as a whole; a patch moves or turns its selected
	// points, or every point with none selected. A turn goes about the anchor,
	// or else the centre of what turns.
	bool nudge(QPointF texels);
	bool rotate(double clockwiseDegrees);
	// A face: its whole texture moves so `point` reads `texel`. A patch: the
	// point takes `texel`.
	bool movePointTo(int point, QPointF texel);
	bool resetEdit();
	bool undo();
	bool redo();
	[[nodiscard]] bool canUndo() const { return !m_undo.isEmpty(); }
	[[nodiscard]] bool canRedo() const { return !m_redo.isEmpty(); }

	void frameAll();
	// About `screenPoint`, or the middle of the view when it is not finite.
	void zoomBy(double factor, QPointF screenPoint);
	void zoomBy(double factor);
	void panBy(QPointF screenDelta);
	[[nodiscard]] double zoom() const { return m_zoom; }
	[[nodiscard]] QPointF texelToScreen(QPointF texel) const;
	[[nodiscard]] QPointF screenToTexel(QPointF screen) const;
	// The point under `screen`, nearest first, or -1.
	[[nodiscard]] int pointAt(QPointF screen, double tolerance = 8) const;
	[[nodiscard]] bool dragging() const { return m_drag != Drag::None; }
	// Plain text for the accessible description and the dialog's status.
	[[nodiscard]] QString summary() const;
	[[nodiscard]] QSize minimumSizeHint() const override;
	[[nodiscard]] QSize sizeHint() const override;
	// The menu a right-click opens, for the view's own actions.
	[[nodiscard]] QMenu* contextMenu() const { return m_menu; }

  Q_SIGNALS:
	// The edit or its result changed, including live during a drag.
	void editChanged();
	// The anchor, current point, selection or undo history changed.
	void stateChanged();

  protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;
	void changeEvent(QEvent* event) override;
	bool event(QEvent* event) override;

  private:
	enum class Drag { None, Point, Grab, Band, Pan };
	bool setWorkingEdit(const LevelUvEdit& edit);
	void record(const LevelUvEdit& before);
	void finishDrag(bool commit);
	void announce();
	void buildMenu();
	QVector<int> turningPoints() const;
	QPointF pivotFor(const QVector<int>& points) const;
	QRectF contentBounds() const;
	QSizeF tile() const;

	LevelUvLayout m_layout;
	bool m_hasSource = false;
	QImage m_image, m_checker;
	LevelUvEdit m_edit;
	LevelUvResult m_result;
	QString m_error;
	QVector<LevelUvEdit> m_undo, m_redo;
	int m_anchor = -1, m_current = -1;
	QVector<int> m_selected;
	double m_snap = 1, m_angleStep = 15;
	bool m_allowValve220 = false;
	double m_zoom = 1;
	QPointF m_centre;
	// The user zoomed or panned: resizes keep the camera instead of framing.
	bool m_userCamera = false;
	Drag m_drag = Drag::None;
	int m_dragPoint = -1;
	bool m_turning = false, m_moved = false, m_bandAdd = false;
	Qt::MouseButton m_button = Qt::NoButton;
	QPointF m_press, m_last;
	LevelUvEdit m_dragStartEdit;
	QVector<QPointF> m_dragStartUv;
	QMenu* m_menu = nullptr;
};

} // namespace vibestudio
