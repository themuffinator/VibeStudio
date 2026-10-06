#pragma once

#include "core/model_design.h"

#include <QDialog>
#include <functional>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

namespace vibestudio
{
class ModelViewport;

struct ModelDesignContext {
	QString packagePath;
	QString mapPath;
	bool canStage = false;
	bool canPlace = false;
	bool canPlaceCollision = false;
};

class ModelDesignDialog final : public QDialog
{
  public:
	explicit ModelDesignDialog(QWidget* parent = nullptr);
	std::function<ModelDesignContext()> context;
	std::function<bool(const ModelDesign&, const QString&, bool, const LevelMapVec3&, bool, QString*)> handoff;
	std::function<void(const QString&)> showMaterial;
	std::function<void(const ModelMesh&)> editMesh;
	[[nodiscard]] const ModelDesign& design() const { return m_design; }
	bool setDesign(const ModelDesign& design, QString* error = nullptr);
	void addPart(const QString& primitive);
	void duplicatePart();
	void removePart();
	void undo();
	void redo();
	void refreshContext();

  protected:
	void closeEvent(QCloseEvent* event) override;
	void reject() override;
	void changeEvent(QEvent* event) override;

  private:
	void commit(ModelDesign next, int selected, bool refreshFields = true);
	void refreshParts(int selected);
	void loadPart();
	void editPart(QObject* field);
	void refreshPreview();
	void refreshSelection();
	bool confirmDiscard();
	bool saveDesign();
	void openDesign();
	void exportDesign(const QString& format);
	void stage(bool place);
	QString uniqueName(const QString& prefix) const;
	ModelDesign m_design;
	struct HistoryEntry {
		ModelDesign design;
		int selected = 0;
	};
	QVector<HistoryEntry> m_undo, m_redo;
	QJsonObject m_savedDesign;
	QString m_path;
	QString m_contextKey;
	bool m_dirty = false, m_refreshing = false;
	QListWidget* m_parts = nullptr;
	ModelViewport* m_preview = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_contextLabel = nullptr;
	QLabel* m_selectionStatus = nullptr;
	QLineEdit* m_name = nullptr;
	QLineEdit* m_partName = nullptr;
	QLineEdit* m_material = nullptr;
	QLineEdit* m_virtualPath = nullptr;
	QComboBox* m_primitive = nullptr;
	QDoubleSpinBox* m_size[3]{};
	QDoubleSpinBox* m_origin[3]{};
	QDoubleSpinBox* m_placement[3]{};
	QDoubleSpinBox* m_yaw = nullptr;
	QDoubleSpinBox* m_pitch = nullptr;
	QDoubleSpinBox* m_roll = nullptr;
	QDoubleSpinBox* m_uvScale[2]{};
	QDoubleSpinBox* m_uvOffset[2]{};
	QDoubleSpinBox* m_uvRotation = nullptr;
	QSpinBox* m_segments = nullptr;
	QCheckBox* m_replace = nullptr;
	QAction* m_undoAction = nullptr;
	QAction* m_redoAction = nullptr;
	QAction* m_exportMd3 = nullptr;
	QAction* m_exportObj = nullptr;
	QAction* m_stageAction = nullptr;
	QAction* m_placeAction = nullptr;
};
} // namespace vibestudio
