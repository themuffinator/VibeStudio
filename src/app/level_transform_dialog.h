#pragma once
#include "core/level_map.h"
#include "core/level_placement.h"
#include <QCoreApplication>
#include <QDialog>
#include <array>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;
class QTimer;

namespace vibestudio
{
class ModelViewport;

// The modeless frame NetRadiant Custom's Arbitrary Rotation and Arbitrary
// Scale windows share: a value form the subclass fills, a pivot (selection
// centre, world origin or typed coordinates), texture options, a cancellable
// background preview of the edited map, and OK, Apply and Close. Apply commits
// and keeps the window open; the caller then hands it the edited map with
// setSource(), so the next Apply works on the refreshed selection. Workflow
// reference: NRC radiant/select.cpp RotateDialog and ScaleDialog at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
class LevelTransformDialog : public QDialog
{
	Q_DECLARE_TR_FUNCTIONS(LevelTransformDialog)
  public:
	~LevelTransformDialog() override;
	// Replaces the map the preview and the next Apply use. Typed values and
	// a typed pivot stay; the selection-centre pivot follows the selection.
	void setSource(const LevelMapDocument& source);
	[[nodiscard]] const LevelMapDocument& sourceDocument() const { return m_source; }
	[[nodiscard]] bool isReady() const { return m_ready; }
	[[nodiscard]] bool previewValid() const { return m_valid; }
	[[nodiscard]] const LevelMapDocument& previewDocument() const { return m_previewDocument; }
	// A non-empty reason keeps OK and Apply off and shows the reason, such as
	// component mode with nothing picked.
	void setBlockedReason(const QString& reason);
	// Rewrites the object placement the preview runs, such as into a transform
	// of the picked components; empty previews the objects. Takes effect at
	// the next preview (setSource() starts one).
	void setPlacementAdapter(std::function<LevelPlacementRequest(const LevelPlacementRequest&)> adapter);
	// A line shown under the ready status, such as what the edit applies to.
	void setStatusNote(const QString& note);
	// Commits the current values and keeps the window open. False, with the
	// reason shown, when nothing valid is ready or the commit is refused.
	bool apply();
	// OK: Apply, then close when it succeeded.
	void accept() override;

  protected:
	LevelTransformDialog(const LevelMapDocument& source, QWidget* parent);
	// Subclasses add their value rows here, then call finishSetup().
	[[nodiscard]] QFormLayout* valueForm() const { return m_values; }
	void finishSetup();
	// Starts a new preview after the values change.
	void schedule();
	// Invalid for the selection centre.
	[[nodiscard]] LevelMapVec3 pivot() const;
	void setPivot(const LevelMapVec3& pivot);
	[[nodiscard]] bool textureLock() const;
	[[nodiscard]] bool allowValve220() const;
	void setTextureOptions(bool lock, bool allowValve220);
	void setTextureLockText(const QString& text, const QString& toolTip);
	[[nodiscard]] bool updating() const { return m_updating; }
	void setUpdating(bool updating) { m_updating = updating; }
	// A spin box for typed numbers: left to right in every layout direction.
	QDoubleSpinBox* addValueField(const QString& objectName, const QString& label, const QString& accessibleName,
		const QString& description, double minimum, double maximum, double value, int decimals, double step);

	// The placement the preview and the commit share.
	[[nodiscard]] virtual LevelPlacementRequest placementRequest() const = 0;
	virtual bool commit(QString* error) = 0;
	// The first status line once a valid preview is ready.
	[[nodiscard]] virtual QString readyText() const = 0;
	// Called after the source changes, for format-dependent fields.
	virtual void sourceChanged() {}
	// Why the values cannot apply to anything, such as a zero factor; empty
	// when the placement service should decide.
	[[nodiscard]] virtual QString requestProblem() const { return {}; }

  private:
	struct Work;
	void startPreview();
	void refreshPivotFields();
	LevelMapDocument m_source, m_previewDocument;
	QFormLayout* m_values = nullptr;
	QComboBox* m_pivotMode = nullptr;
	std::array<QDoubleSpinBox*, 3> m_pivot{};
	QCheckBox* m_lock = nullptr;
	QCheckBox* m_allowValve = nullptr;
	QLabel* m_status = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_ok = nullptr;
	QPushButton* m_apply = nullptr;
	ModelViewport* m_preview = nullptr;
	QTimer* m_debounce = nullptr;
	QThread* m_thread = nullptr;
	QWidget* m_controls = nullptr;
	std::shared_ptr<Work> m_work;
	std::function<LevelPlacementRequest(const LevelPlacementRequest&)> m_adapter;
	QString m_blocked, m_note;
	quint64 m_generation = 0;
	bool m_ready = false, m_valid = false, m_updating = false, m_finished = false;
};
} // namespace vibestudio
