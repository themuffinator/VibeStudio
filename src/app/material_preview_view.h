#pragma once

// The live preview: a material drawn by core/material_render.h the way its
// engine draws it, animated in real time, on a shape the user can orbit.

#include "core/material_eval.h"
#include "core/material_images.h"
#include "core/material_model.h"
#include "core/material_render.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QWidget>

#include <memory>

class QTimer;

namespace vibestudio {

class MaterialTaskLane;

class MaterialPreviewView final : public QWidget {
	Q_OBJECT

public:
	explicit MaterialPreviewView(QWidget* parent = nullptr);
	~MaterialPreviewView() override;

	void setMaterial(const MaterialDefinition& definition, std::shared_ptr<const MaterialImageSet> images,
		std::shared_ptr<const MaterialTableSet> tables);
	// Shows `message` instead of a material (nothing selected, loading).
	void clearMaterial(const QString& message);
	[[nodiscard]] bool hasMaterial() const { return m_hasMaterial; }
	[[nodiscard]] const MaterialDefinition& definition() const { return m_definition; }

	// Everything but time, size and camera, which the view owns.
	void setRenderOptions(const MaterialRenderOptions& options);
	[[nodiscard]] MaterialRenderOptions renderOptions() const { return m_options; }
	void setShape(MaterialPreviewShape shape);
	[[nodiscard]] MaterialPreviewShape shape() const { return m_options.shape; }

	void setPlaying(bool playing);
	[[nodiscard]] bool isPlaying() const { return m_playing; }
	void setTime(double seconds);
	[[nodiscard]] double time() const;
	void setSpeed(double speed);
	[[nodiscard]] double speed() const { return m_speed; }
	void stepTime(double seconds);
	void resetCamera();
	// Reduced motion: nothing starts playing by itself.
	void setReducedMotion(bool reduced);

	[[nodiscard]] QImage lastImage() const { return m_image; }
	[[nodiscard]] const MaterialRenderResult& lastResult() const { return m_result; }
	// A frame is being drawn or waits to be.
	[[nodiscard]] bool renderPending() const;
	// Draws again on the current 3D renderer, after it changed.
	void resetRendering();

Q_SIGNALS:
	void timeChanged(double seconds);
	void playingChanged(bool playing);
	void frameRendered();
	void cameraChanged();

protected:
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void tick();
	void requestRender(bool supersede);
	void restartClock();
	void refreshAccessibleDescription();

	MaterialDefinition m_definition;
	std::shared_ptr<const MaterialImageSet> m_images;
	std::shared_ptr<const MaterialTableSet> m_tables;
	bool m_hasMaterial = false;
	QString m_message;
	MaterialRenderOptions m_options;
	double m_defaultYaw = 25.0;
	double m_defaultPitch = 12.0;

	double m_time = 0.0;
	double m_clockBase = 0.0;
	double m_speed = 1.0;
	bool m_playing = false;
	bool m_reducedMotion = false;
	QElapsedTimer m_clock;
	QTimer* m_timer = nullptr;

	MaterialTaskLane* m_lane = nullptr;
	QImage m_image;
	MaterialRenderResult m_result;
	// Fraction of the widget's pixels drawn while playing, lowered when
	// frames take too long so playback keeps its pace.
	double m_playScale = 1.0;
	bool m_dirty = false;
	// renderBackendGeneration() of the last frame asked for: a renderer
	// change or restart draws again.
	quint64 m_renderGeneration = 0;

	QPoint m_dragOrigin;
	double m_dragYaw = 0.0;
	double m_dragPitch = 0.0;
	double m_dragLight = 0.0;
	bool m_dragging = false;
	bool m_draggingLight = false;
};

} // namespace vibestudio
