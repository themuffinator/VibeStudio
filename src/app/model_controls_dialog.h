#pragma once

// Customise Controls: edits a modeller profile's keys, mouse gestures,
// transform rules and layout on a copy, showing conflicts as they appear.
// The editor saves the result as overrides of the profile, so every setting
// the user leaves alone keeps following the profile.

#include "core/model_editor_controls.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace vibestudio
{
class ModelControlsDialog final : public QDialog
{
  public:
	explicit ModelControlsDialog(const ModelEditorControls &controls, QWidget *parent = nullptr);
	[[nodiscard]] const ModelEditorControls &controls() const { return m_controls; }
	// Sets the keys of one command, as the key field does.
	void setCommandKeys(const QString &commandId, const QStringList &keys);
	void resetToProfile();

  private:
	ModelEditorControls m_controls;
	QTableWidget *m_keys = nullptr;
	QLineEdit *m_filter = nullptr;
	QKeySequenceEdit *m_keyEdit = nullptr;
	QLabel *m_problems = nullptr;
	struct Gesture
	{
		QComboBox *button = nullptr;
		QComboBox *modifiers = nullptr;
	};
	Gesture m_orbit, m_planOrbit, m_pan, m_zoom, m_subtractDrag, m_lasso, m_cursor;
	QComboBox *m_extend = nullptr, *m_subtract = nullptr, *m_loop = nullptr, *m_ring = nullptr, *m_path = nullptr;
	QComboBox *m_leftPan = nullptr, *m_duplicateDrag = nullptr, *m_precision = nullptr, *m_snap = nullptr;
	QComboBox *m_style = nullptr, *m_layout = nullptr;
	QComboBox *m_panes[4]{};
	QCheckBox *m_extendToggles = nullptr, *m_boxSelect = nullptr, *m_doubleClickLoop = nullptr, *m_rightClickMenu = nullptr;
	QCheckBox *m_emulateMiddle = nullptr, *m_invertWheel = nullptr, *m_orbitLeavesOrtho = nullptr, *m_dragSelection = nullptr;
	QCheckBox *m_startPerspective = nullptr, *m_timeline = nullptr, *m_toolShelf = nullptr;
	bool m_loading = false;

	QWidget *buildKeys();
	QWidget *buildMouse();
	QWidget *buildTransforms();
	Gesture gestureField(const QString &name);
	QComboBox *modifierField(const QString &name);
	void load();
	void store();
	void refreshKeys();
	void refreshProblems();
	[[nodiscard]] QString selectedCommand() const;
};
} // namespace vibestudio
