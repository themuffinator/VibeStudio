#pragma once

#include "core/level_surface_clipboard.h"
#include <QCoreApplication>
#include <QScrollArea>

class QComboBox;
class QCheckBox;
class QBoxLayout;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QToolButton;

namespace vibestudio {
class StudioCommandRegistry;

class LevelSurfaceTools final : public QScrollArea {
	Q_DECLARE_TR_FUNCTIONS(LevelSurfaceTools)
public:
	explicit LevelSurfaceTools(QWidget* parent = nullptr);
	static QStringList commandIds();
	LevelSurfaceRequest request(const QString& command) const;
	bool singleFace() const;
	void bindCommands(StudioCommandRegistry& commands);
	void setTargetSummary(int faces, LevelSurfaceFace inspected, int patches = 0);
	void setStatus(const QString& text, bool busy);
	void setClipboardSummary(const LevelSurfaceClipboard& clipboard);
	LevelSurfacePasteOptions pasteOptions() const;
	std::function<void()> targetChanged;
	std::function<void()> cancelRequested;
private:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void updateRows();
	QComboBox* m_target = nullptr;
	QComboBox* m_pasteMode = nullptr;
	QCheckBox* m_allowValve = nullptr;
	QCheckBox* m_mappingOnly = nullptr;
	QLabel* m_clipboard = nullptr;
	QDoubleSpinBox *m_shift = nullptr, *m_rotation = nullptr, *m_scale = nullptr;
	QLabel *m_summary = nullptr, *m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QToolButton* m_cancel = nullptr;
	QHash<QString, QToolButton*> m_buttons;
	QVector<QBoxLayout*> m_rows;
};
} // namespace vibestudio
