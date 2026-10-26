#pragma once

#include "core/level_surface.h"
#include <QCoreApplication>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace vibestudio
{
// Reset Textures: bobToolz's window, for the selection or the whole map. Each
// ticked value is set absolutely on the targeted faces, which can be limited
// to one texture and take a new name. The shell runs the reset on the surface
// worker as one undo step (ApplicationShell::startLevelTextureReset).
// Workflow reference: NRC contrib/bobtoolz DoResetTextureBox and
// DoResetTextures at 449778b3 (GPL-2.0-or-later). Original Qt dialog; see
// docs/CREDITS.md.
class LevelTextureResetDialog final : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelTextureResetDialog)
  public:
	// `selectionFaces` and `selectionPatches`: what the Selection scope holds;
	// `texture` fills the texture fields; `defaultScale` the scale fields.
	LevelTextureResetDialog(int selectionFaces, int selectionPatches, const QString &texture, double defaultScale, QWidget *parent = nullptr);
	// The scope, filter, name and ticked values, without targets.
	LevelTextureResetRequest request() const;

  private:
	void updateState();
	int m_selectionFaces = 0, m_selectionPatches = 0;
	QComboBox *m_scope = nullptr;
	QCheckBox *m_filter = nullptr, *m_rename = nullptr;
	QLineEdit *m_filterName = nullptr, *m_newName = nullptr;
	QCheckBox *m_setScaleU = nullptr, *m_setScaleV = nullptr, *m_setShiftU = nullptr, *m_setShiftV = nullptr, *m_setRotation = nullptr;
	QDoubleSpinBox *m_scaleU = nullptr, *m_scaleV = nullptr, *m_shiftU = nullptr, *m_shiftV = nullptr, *m_rotation = nullptr;
	QLabel *m_status = nullptr;
	QPushButton *m_apply = nullptr;
};
} // namespace vibestudio
