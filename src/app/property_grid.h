#pragma once

#include <QTreeWidget>

namespace vibestudio {

// Metadata grids keep translated headings readable through font/theme changes,
// preserve a user's property-column width, and scroll when both cannot fit.
class PropertyGrid final : public QTreeWidget {
public:
	explicit PropertyGrid(QWidget* parent = nullptr);
protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
private:
	void scheduleHeaderMetrics();
	void updateHeaderMetrics();
	int m_fontHeight = 0;
	double m_preferredSectionEm = 0;
	bool m_updatingMetrics = false, m_metricsQueued = false;
};

} // namespace vibestudio
