#pragma once

#include "core/model_assembly.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTreeWidget;

namespace vibestudio
{
class ModelQ3AnimationDialog final : public QDialog
{
  public:
	ModelQ3AnimationDialog(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, QWidget *parent = nullptr);
	std::optional<ModelAssemblyQ3Animation> animation() const;
	bool setConfigBytes(const QByteArray &bytes, QString *error = nullptr);
	bool validateDraft(QString *error = nullptr) const;
	bool exportRequested() const
	{
		return m_exportRequested;
	}

  private:
	ModelAssembly m_assembly;
	ModelAssemblyResolved m_resolved;
	ModelAssemblyQ3Animation m_binding;
	QCheckBox *m_enabled = nullptr, *m_reverse = nullptr, *m_fixedLegs = nullptr, *m_fixedTorso = nullptr;
	QComboBox *m_lower = nullptr, *m_upper = nullptr, *m_lowerClip = nullptr, *m_upperClip = nullptr;
	QComboBox *m_footsteps = nullptr, *m_sex = nullptr, *m_sourceClip = nullptr;
	QSpinBox *m_first = nullptr, *m_count = nullptr, *m_loop = nullptr;
	QDoubleSpinBox *m_fps = nullptr, *m_head[3]{};
	QTreeWidget *m_slots = nullptr;
	QLabel *m_modelRange = nullptr, *m_status = nullptr;
	int m_slot = 0;
	bool m_refreshing = false, m_exportRequested = false;
	void refresh();
	void refreshSlot();
	void updateClip();
	void updateStatus();
	void chooseImport();
	void useSourceClip();
	void finish(bool exporting);
};
} // namespace vibestudio
