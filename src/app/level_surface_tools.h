#pragma once

#include "core/level_face_flags.h"
#include "core/level_surface_clipboard.h"
#include <QCoreApplication>
#include <QScrollArea>

class QComboBox;
class QCheckBox;
class QBoxLayout;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QToolButton;

namespace vibestudio {
class SidebarSection;
class StudioCommandRegistry;

// The Surfaces tab: what to adjust (Target), the steps and buttons that
// adjust it (Adjust), the surface clipboard (Copy and Paste) and the brush
// faces' content and surface flags and value (Flags), each a sidebar section.
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
	// The Flags section shows `summary`, the flags of the targeted brush faces,
	// with a box per flag the map's game names: ticked on every face, partly
	// ticked on some. `editable` is false while nothing can take an edit.
	// A box, the Value field or Clear Face Flags reports its edit through
	// flagsEdited (core/level_face_flags.h).
	void setFaceFlags(LevelFaceFlagGame game, const LevelFaceFlagsSummary& summary, bool editable);
	std::function<void()> targetChanged;
	std::function<void()> cancelRequested;
	std::function<void(const LevelFaceFlagsEdit&)> flagsEdited;
private:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void updateRows();
	void rebuildFlagBoxes(LevelFaceFlagGame game);
	void layoutFlagBoxes();
	void applyFlagValue();
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
	// Flags section.
	QLabel* m_flagsSummary = nullptr;
	QWidget* m_flagsBody = nullptr;
	QGridLayout* m_surfaceGrid = nullptr;
	QGridLayout* m_contentGrid = nullptr;
	QVector<QCheckBox*> m_surfaceBoxes, m_contentBoxes;
	QLineEdit* m_flagValue = nullptr;
	QString m_flagValueShown;
	QLabel* m_otherBits = nullptr;
	QLabel* m_flagsNote = nullptr;
	LevelFaceFlagGame m_flagGame = LevelFaceFlagGame::None;
	bool m_flagBoxesBuilt = false;
	int m_flagColumns = 0;
};
} // namespace vibestudio
