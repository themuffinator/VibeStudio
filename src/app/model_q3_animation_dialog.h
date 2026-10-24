#pragma once

#include "app/live_status_label.h"
#include "core/model_assembly.h"

#include <QDialog>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
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
	// Enables Import from Package… for this package snapshot.
	void setPackage(std::shared_ptr<const PackageArchiveReader> package);
	// Reads animation.cfg from a package entry into the draft, as Import…
	// does from a file: a unique path, or an exact entry index whose path
	// must also match (core/model_player_import.h).
	bool importPackageEntry(const QString &path, qsizetype entryIndex = -1, QString *error = nullptr);
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
	QLabel *m_modelRange = nullptr;
	LiveStatusLabel *m_status = nullptr;
	QPushButton *m_importPackage = nullptr;
	std::shared_ptr<const PackageArchiveReader> m_package;
	int m_slot = 0;
	bool m_refreshing = false, m_exportRequested = false;
	void refresh();
	void refreshSlot();
	void updateClip();
	void updateStatus();
	void chooseImport();
	void chooseImportPackage();
	void useSourceClip();
	void finish(bool exporting);
};
} // namespace vibestudio
