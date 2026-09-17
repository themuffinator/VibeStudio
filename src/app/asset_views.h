#pragma once

// Graphical asset preview widgets: real pixels for textures and sprites, a real
// waveform for audio, and a palette swatch grid, all painted with QPainter so no
// extra Qt module is required.

#include "core/idtech_image.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace vibestudio {

// Zoomable, pannable image preview with checkerboard alpha, nearest-neighbour
// magnification for indexed art, and an optional pixel readout.
class ImagePreviewView final : public QWidget {
	Q_OBJECT

public:
	explicit ImagePreviewView(QWidget* parent = nullptr);

	void setImage(const QImage& image, const QString& title = QString());
	void setDecodeResult(const IdTechImageDecodeResult& result);
	void clearImage();
	[[nodiscard]] bool hasImage() const;
	[[nodiscard]] QImage image() const;

	void setMipLevel(int level);
	[[nodiscard]] int mipLevel() const;
	[[nodiscard]] int mipLevelCount() const;

	void setFrameIndex(int index);
	[[nodiscard]] int frameIndex() const;
	[[nodiscard]] int frameCount() const;

	void setShowCheckerboard(bool show);
	void setShowPixelGrid(bool show);
	void setHighContrast(bool enabled);
	void zoomToFit();
	void zoomToActualSize();
	void zoomIn();
	void zoomOut();
	[[nodiscard]] double zoom() const;

	[[nodiscard]] QString hoverSummary() const;
	[[nodiscard]] QStringList statusLines() const;
	[[nodiscard]] QString accessibleSummary() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void hoverChanged(const QString& summary);
	void viewChanged();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void wheelEvent(QWheelEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	[[nodiscard]] QImage activeImage() const;
	[[nodiscard]] QRectF imageRect() const;
	void clampOffset();

	IdTechImageDecodeResult m_result;
	QImage m_image;
	QString m_title;
	int m_mipLevel = 0;
	int m_frameIndex = 0;
	double m_zoom = 1.0;
	QPointF m_offset;
	QPoint m_hoverPixel {-1, -1};
	QString m_hoverSummary;
	bool m_showCheckerboard = true;
	bool m_showPixelGrid = false;
	bool m_highContrast = false;
	bool m_panning = false;
	QPointF m_panAnchor;
	QPointF m_panAnchorOffset;
};

// 16x16 indexed palette grid with index/RGB readout on hover.
class PaletteSwatchView final : public QWidget {
	Q_OBJECT

public:
	explicit PaletteSwatchView(QWidget* parent = nullptr);

	void setPalette(const IdTechPalette& palette);
	void clearPalette();
	[[nodiscard]] bool hasPalette() const;
	[[nodiscard]] int selectedIndex() const;
	void setSelectedIndex(int index);
	[[nodiscard]] QString hoverSummary() const;
	[[nodiscard]] QString accessibleSummary() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void indexSelected(int index);
	void hoverChanged(const QString& summary);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	[[nodiscard]] int indexAt(const QPoint& point) const;

	IdTechPalette m_palette;
	int m_selectedIndex = -1;
	int m_hoverIndex = -1;
	QString m_hoverSummary;
};

// Min/max envelope waveform drawn from decoded PCM peaks.
class WaveformView final : public QWidget {
	Q_OBJECT

public:
	explicit WaveformView(QWidget* parent = nullptr);

	// `peaks` holds interleaved min/max pairs in the range [-1, 1].
	void setPeaks(const QVector<float>& peaks, int channels, int sampleRate, qint64 durationMs);
	void clearPeaks();
	[[nodiscard]] bool hasPeaks() const;
	void setHighContrast(bool enabled);
	[[nodiscard]] QString accessibleSummary() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QVector<float> m_peaks;
	int m_channels = 0;
	int m_sampleRate = 0;
	qint64 m_durationMs = 0;
	bool m_highContrast = false;
};

} // namespace vibestudio
