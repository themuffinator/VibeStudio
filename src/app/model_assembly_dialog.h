#pragma once

#include "app/model_document_work.h"
#include "app/model_material_worker.h"
#include "core/model_assembly.h"
#include "core/model_assembly_animation.h"
#include "core/model_assembly_recovery.h"
#include "core/model_player_bundle.h"

#include <QDialog>
#include <QElapsedTimer>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QScrollArea;
class QSpinBox;
class QTimer;
class QToolBar;
class QTreeWidget;

namespace vibestudio
{
class ModelViewport;
class ModelAssemblyRecoveryWriter;

class ModelAssemblyDialog final : public QDialog
{
  public:
	explicit ModelAssemblyDialog(QWidget *parent = nullptr);
	~ModelAssemblyDialog() override;
	const ModelAssemblyDocument &document() const
	{
		return m_document;
	}
	const ModelAssemblyPose &pose() const
	{
		return m_pose;
	}
	double timeSeconds() const
	{
		return m_seconds;
	}
	bool operationBusy() const
	{
		return m_working;
	}
	bool previewReady() const
	{
		return !m_pose.mesh.surfaces.isEmpty();
	}
	bool recoveryBusy() const;
	QString recoveryPath() const;
	bool setAssembly(const ModelAssembly &assembly, const QString &directory, QString *error = nullptr);
	bool openSource(const QString &path, QString *error = nullptr);
	bool saveSource(const QString &path, bool overwrite = false, QString *error = nullptr);
	bool applyPart(const QString &previousId, const ModelAssemblyPart &part, QString *error = nullptr);
	bool applyQ3Animation(const std::optional<ModelAssemblyQ3Animation> &animation, QString *error = nullptr);
	bool exportQ3Animation(const QString &path, bool overwrite, QString *error = nullptr);
	bool preparePlayerBundle(const ModelPlayerBundleOptions &options, ModelPlayerBundle *result, QString *error = nullptr);
	bool exportPlayerBundle(const ModelPlayerBundle &bundle, const QString &path, bool overwrite,
		PackageWriteReport *result = nullptr, QString *error = nullptr);
	bool removeBranch(const QString &id, QString *error = nullptr);
	bool undo(QString *error = nullptr);
	bool redo(QString *error = nullptr);
	bool setTime(double seconds, QString *error = nullptr);
	bool reloadInputs(QString *error = nullptr);
	bool exportPose(const QString &path, bool overwrite, QString *error = nullptr);
	bool bakeAnimation(const ModelAssemblyAnimationOptions &options, ModelAssemblyAnimation *result, QString *error = nullptr);
	bool exportAnimation(const ModelAssemblyAnimationOptions &options, const QString &path, bool overwrite, QString *error = nullptr);
	bool restoreRecovery(const QString &path, const QByteArray &sha256, QString *error = nullptr);
	void checkpointRecovery();
	void selectPart(const QString &id);
	void setMaterialSource(ModelMaterialSource source);
	void setAccessibility(bool highContrast, bool reducedMotion);
	void cancelOperation();
	bool requestClose(std::function<void()> afterDeferredClose = {});
	// A value-only, explicit pose or animation bake enters the ordinary mesh document and
	// its existing material, package staging and level placement services.
	std::function<bool(const ModelMesh &, QString *)> editBakedPose;

  protected:
	void closeEvent(QCloseEvent *event) override;
	void reject() override;
	void changeEvent(QEvent *event) override;

  private:
	ModelAssemblyDocument m_document;
	ModelAssemblyResolved m_resolved;
	ModelAssemblyPose m_pose;
	ModelPlayerBundleOptions m_playerBundleOptions;
	ModelMaterialSource m_materialSource;
	ModelMaterialWorker *m_materialWorker = nullptr;
	LevelPreviewAssets m_materialAssets;
	QHash<int, QImage> m_materialImages;
	QToolBar *m_toolbar = nullptr;
	QTreeWidget *m_tree = nullptr;
	QWidget *m_inspector = nullptr, *m_timeline = nullptr;
	QScrollArea *m_inspectorScroll = nullptr;
	ModelViewport *m_preview = nullptr;
	QPlainTextEdit *m_details = nullptr;
	QLabel *m_status = nullptr, *m_materialStatus = nullptr;
	QLabel *m_recoveryStatus = nullptr;
	QCheckBox *m_recoveryEnabled = nullptr;
	QTimer *m_recoveryTimer = nullptr;
	ModelAssemblyRecoveryWriter *m_recoveryWriter = nullptr;
	QByteArray m_recoveredSourceHash;
	QLineEdit *m_id = nullptr, *m_source = nullptr;
	QLineEdit *m_skinSource = nullptr;
	QComboBox *m_skinKind = nullptr;
	QSpinBox *m_skinIndex = nullptr;
	QComboBox *m_kind = nullptr, *m_parent = nullptr, *m_tag = nullptr, *m_render = nullptr;
	QDoubleSpinBox *m_translation[3]{}, *m_rotation[3]{};
	QDoubleSpinBox *m_scale = nullptr, *m_fps = nullptr, *m_phase = nullptr, *m_time = nullptr;
	QSpinBox *m_first = nullptr, *m_last = nullptr;
	QCheckBox *m_loop = nullptr, *m_interpolate = nullptr;
	QAction *m_undo = nullptr, *m_redo = nullptr, *m_remove = nullptr, *m_bake = nullptr, *m_export = nullptr;
	QAction *m_play = nullptr;
	QAction *m_bakeAnimation = nullptr, *m_exportAnimation = nullptr;
	QTimer *m_timer = nullptr;
	QElapsedTimer m_clock;
	double m_seconds = 0, m_playStart = 0;
	QString m_editingId, m_previewError;
	bool m_refreshing = false, m_working = false, m_reducedMotion = false, m_contextPending = false;
	bool m_closeAfterWork = false, m_decidingClose = false, m_approvedClose = false;
	std::function<void()> m_cancelWork, m_afterDeferredClose;
	bool performWork(const QString &title, ModelTask task, QString *error, bool durableWrite = false);
	// A structurally valid recipe remains editable when an input is missing.
	// Resolution failure clears the preview and is reported in the status.
	bool adopt(ModelAssemblyDocument candidate, QString *error, bool replacement = false,
			   const ModelAssemblyRecoverySnapshot *recovery = nullptr, const QString &preservedCopy = {});
	void retireRecovery(const QString &preservedCopy = {});
	void chooseRecovery();
	void refresh(bool rebuildTree = true);
	void refreshInspector();
	void refreshTags(const QString &selected = {});
	void refreshSelection();
	void refreshDetails();
	void refreshMaterials();
	void applyMaterialImages();
	void newPart();
	void applyInspector();
	void play(bool playing);
	bool maybeSave();
	bool chooseSave(bool saveAs = false);
	void chooseOpen();
	void chooseExport();
	void bake();
	void chooseAnimationBake(bool exporting);
	void chooseQ3Animation();
	void choosePlayerBundle();
	void addSkinControls(QFormLayout *form);
	void refreshSkinControls(const ModelAssemblyPart &part);
	std::optional<ModelAssemblySkin> skinFromInspector() const;
	void report(const QString &message);
};
} // namespace vibestudio
