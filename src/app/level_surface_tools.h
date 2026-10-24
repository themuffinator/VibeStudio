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
class SidebarSection;
class StudioCommandRegistry;

// The Surfaces tab: what to adjust (Target), the steps and buttons that
// adjust it (Adjust), and the surface clipboard (Copy and Paste), each a
// sidebar section.
class LevelSurfaceTools final : public QScrollArea {
	Q_DECLARE_TR_FUNCTIONS(LevelSurfaceTools)
public:
	explicit LevelSurfaceTools(QWidget* parent = nullptr);
	static QStringList commandIds();
	LevelSurfaceRequest request(const QString& command) const;
	bool singleFace() const;
	// Target, Adjust and Copy and Paste, for the page to remember.
	[[nodiscard]] QVector<SidebarSection*> sections() const { return m_sections; }
	void bindCommands(StudioCommandRegistry& commands);
	void setTargetSummary(int faces, LevelSurfaceFace inspected, int patches = 0, bool components = false);
	void setStatus(const QString& text, bool busy);
	void setClipboardSummary(const LevelSurfaceClipboard& clipboard);
	LevelSurfacePasteOptions pasteOptions() const;
	// Map units per texel a Natural or Align to Face reset uses, from the
	// open map's game.
	void setDefaultScale(double scale) { m_defaultScale = scale; }
	// Rotate − and + turn about the texture origin rather than each face centre.
	[[nodiscard]] bool rotatesAboutOrigin() const;
	// The editor profile's Rotation step and Rotate about choice.
	void setProfileSteps(double rotationDegrees, bool aboutOrigin);
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
	QDoubleSpinBox *m_shift = nullptr, *m_rotation = nullptr, *m_scale = nullptr, *m_scaleStep = nullptr;
	QComboBox* m_rotationAnchor = nullptr;
	QLabel *m_summary = nullptr, *m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QToolButton* m_cancel = nullptr;
	QHash<QString, QToolButton*> m_buttons;
	QVector<QBoxLayout*> m_rows;
	QVector<SidebarSection*> m_sections;
	double m_defaultScale = 1.0;
};
} // namespace vibestudio
