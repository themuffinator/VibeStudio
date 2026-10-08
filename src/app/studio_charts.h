#pragma once

// Small QPainter chart widgets used to explain real project state: package
// composition, build pipeline stages, and the activity timeline.
//
// Every chart carries a non-color cue (pattern, glyph, or text) and an
// accessible text summary so nothing depends on colour alone.

#include "core/operation_state.h"

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace vibestudio {

struct StudioChartSlice {
	QString id;
	QString label;
	double value = 0.0;
	QString valueText;
	QString detail;
	OperationState state = OperationState::Completed;
	int patternIndex = 0;
};

// Horizontal stacked-proportion bar with a legend; used for package
// composition by type and by size. Both start from the leading side, so a
// right-to-left layout mirrors them.
class CompositionChart final : public QWidget {
	Q_OBJECT

public:
	explicit CompositionChart(QWidget* parent = nullptr);

	void setTitle(const QString& title);
	void setSlices(const QVector<StudioChartSlice>& slices);
	void setEmptyText(const QString& text);
	void setHighContrast(bool enabled);
	void clear();

	[[nodiscard]] QVector<StudioChartSlice> slices() const;
	[[nodiscard]] QString accessibleSummary() const;
	[[nodiscard]] QStringList summaryLines() const;

	[[nodiscard]] bool hasHeightForWidth() const override;
	[[nodiscard]] int heightForWidth(int width) const override;
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void sliceActivated(const QString& sliceId);
	void hoverChanged(const QString& summary);

protected:
	void changeEvent(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	[[nodiscard]] int sliceAt(const QPoint& point) const;

	QString m_title;
	QString m_emptyText;
	QVector<StudioChartSlice> m_slices;
	int m_hoverIndex = -1;
	bool m_highContrast = false;
};

struct PipelineStageNode {
	QString id;
	QString label;
	QString detail;
	OperationState state = OperationState::Idle;
	QString badgeText;
	bool optional = false;
};

// Stage graph in reading order: source -> stages -> artifacts, with per-stage
// state glyphs. It runs right to left in a right-to-left layout.
class PipelineChart final : public QWidget {
	Q_OBJECT

public:
	explicit PipelineChart(QWidget* parent = nullptr);

	void setTitle(const QString& title);
	void setStages(const QVector<PipelineStageNode>& stages);
	void setEmptyText(const QString& text);
	void setHighContrast(bool enabled);
	void setActiveStageId(const QString& stageId);
	void clear();

	[[nodiscard]] QString accessibleSummary() const;
	[[nodiscard]] QStringList summaryLines() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void stageActivated(const QString& stageId);
	void hoverChanged(const QString& summary);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	[[nodiscard]] int stageAt(const QPoint& point) const;

	QString m_title;
	QString m_emptyText;
	QString m_activeStageId;
	QVector<PipelineStageNode> m_stages;
	QVector<QRectF> m_stageRects;
	int m_hoverIndex = -1;
	bool m_highContrast = false;
};

struct TimelineEvent {
	QString id;
	QString label;
	QString detail;
	QString source;
	OperationState state = OperationState::Completed;
	qint64 startedMsSinceEpoch = 0;
	qint64 durationMs = 0;
};

// Compact recent-activity timeline with duration bars.
class ActivityTimelineChart final : public QWidget {
	Q_OBJECT

public:
	explicit ActivityTimelineChart(QWidget* parent = nullptr);

	void setEvents(const QVector<TimelineEvent>& events);
	void setEmptyText(const QString& text);
	void setHighContrast(bool enabled);
	void clear();

	[[nodiscard]] QString accessibleSummary() const;
	[[nodiscard]] QStringList summaryLines() const;

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void eventActivated(const QString& eventId);
	void hoverChanged(const QString& summary);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void leaveEvent(QEvent* event) override;

private:
	[[nodiscard]] int eventAt(const QPoint& point) const;

	QString m_emptyText;
	QVector<TimelineEvent> m_events;
	QVector<QRectF> m_eventRects;
	int m_hoverIndex = -1;
	bool m_highContrast = false;
};

// Shared colour/pattern tokens so every chart, chip, and state glyph agrees.
QColor studioStateColor(OperationState state, bool highContrast, bool lightTheme);
QString studioStateGlyph(OperationState state);

} // namespace vibestudio
