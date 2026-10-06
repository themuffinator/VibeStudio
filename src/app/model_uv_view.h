#pragma once

#include "app/model_uv_render.h"

#include <QLabel>
#include <QThread>

namespace vibestudio
{
struct ModelUvHit
{
	int kind = -1, a = -1, b = -1;
};
class ModelUvView final : public QLabel
{
	Q_OBJECT
  public:
	explicit ModelUvView(QWidget *parent = nullptr);
	~ModelUvView() override;
	void setSource(const ModelSurface &surface, const ModelSelection &selection, const QImage &texture);
	// An animated texture with unchanged dimensions keeps topology/camera and
	// coalesces behind the current raster, avoiding retirement on every tick.
	void setPlaybackTexture(const QImage &texture);
	void setPickMode(int mode);
	void setMoveEnabled(bool enabled, double grid = 0);
	void frameAll();
	void frameSelection();
	void panView(QPointF delta);
	void zoomAt(QPointF point, double factor);
	[[nodiscard]] bool isRendering() const;
	[[nodiscard]] QString summary() const;
	[[nodiscard]] const ModelUvTopology *topology() const;
	[[nodiscard]] QPointF uvToScreen(ModelTexCoord uv) const;
	[[nodiscard]] ModelUvHit hitAt(QPointF point, double tolerance = 8) const;
	[[nodiscard]] QPointF moveHandle() const;
	bool beginMove(QPointF point);
	bool updateMove(QPointF point);
	void finishMove(bool commit);
	[[nodiscard]] bool moving() const;
	[[nodiscard]] ModelTexCoord moveDelta() const;
  Q_SIGNALS:
	void renderCompleted();
	void componentPicked(int kind, int a, int b, bool toggle);
	void movePreviewChanged(double u, double v);
	void moveRequested(double u, double v);
	void moveActiveChanged(bool active);

  protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void changeEvent(QEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;
	bool event(QEvent *event) override;

  private:
	struct Work;
	ModelSurface m_surface;
	ModelSelection m_selection;
	QImage m_texture;
	std::shared_ptr<const ModelUvTopology> m_topology;
	std::shared_ptr<Work> m_work;
	QThread *m_thread = nullptr;
	QTransform m_camera;
	QRectF m_selectionBounds;
	bool m_hasSelection = false;
	ModelUvFit m_fit = ModelUvFit::All;
	ModelUvFit m_resizeFit = ModelUvFit::All;
	quint64 m_revision = 0, m_generation = 0;
	bool m_dirty = false, m_hasSource = false, m_moveEnabled = true, m_moving = false, m_travelled = false;
	double m_grid = 0;
	int m_mode = 0;
	QPointF m_press, m_last;
	Qt::MouseButton m_button = Qt::NoButton;
	ModelTexCoord m_delta;
	QString m_error;
	void requestRender(bool retire = false);
	void startRender();
	void announce();
};
} // namespace vibestudio
