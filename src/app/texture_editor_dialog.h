#pragma once

#include "core/texture_recovery.h"
#include "core/texture_export.h"
#include "core/level_map.h"

#include <QDialog>
#include <functional>
#include <memory>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;
class QToolBar;
class QTimer;

namespace vibestudio {
class TextureCanvas;
class PaletteSwatchView;
class TextureRecoveryWriter;
class TextureExportPanel;
struct TextureEditorTask;
struct TexturePreviewSource;

struct TextureEditorContext {
	QString packagePath;
	QString mapPath;
	bool canStage = false;
	bool canApply = false;
	QString mapTargetKey;
	QString packageTargetKey;
	QString wadMagic;
	LevelMapFormat mapFormat = LevelMapFormat::Unknown;
};

class TextureEditorDialog final : public QDialog {
	Q_OBJECT
public:
	explicit TextureEditorDialog(QWidget* parent = nullptr);
	~TextureEditorDialog() override;
	std::function<TextureEditorContext()> context;
	// Capture immutable source values on the UI thread; resolution runs on the worker.
	std::function<TexturePreviewSource()> paletteSource;
	std::function<bool(const TextureExportResult&, const TextureExportOptions&, const QString&, bool, bool, QString*)> handoff;
	[[nodiscard]] const TextureDocument& document() const { return m_document; }
	[[nodiscard]] const IdTechPaletteResolution& paletteResolution() const { return m_palette; }
	[[nodiscard]] bool isBusy() const { return m_busy; }
	[[nodiscard]] bool hasUnsavedChanges() const;
	bool setImage(const QImage& image, const QString& source, QString* error = nullptr);
	bool editImage(const QImage& image, const QString& source, const IdTechPaletteResolution& palette);
	bool editDecodedImage(const IdTechImageDecodeResult& decoded, const QString& source, const IdTechPaletteResolution& palette);
	void setPaletteResolution(const IdTechPaletteResolution& resolution, bool markChanged = true);
	void refreshPaletteSource(const QString& paletteId = {}, bool markChanged = true);
	void refreshContext();
	void applyOperations(const QJsonArray& operations);
	void saveToPath(const QString& path, bool overwrite, std::function<void()> completed = {});
	void saveExportToPath(const QString& path, bool overwrite, std::function<void()> completed = {});
	void previewExport();
	void setExportOptions(const TextureExportOptions& options);
	bool exportOptions(TextureExportOptions* options, QString* error = nullptr) const;
	void saveProjectToPath(const QString& path, bool overwrite, std::function<void()> completed = {});
	void openFromPath(const QString& path);
	void selectLayer(int index);
	void stage(bool applyToMap = false);
	void undo();
	void redo();
	void newTexture();
	void cancelPending();
	void checkpointRecovery();
	void refreshRecoveries();
	void restoreRecovery(const QString& path, const QByteArray& expectedRecordSha256 = {});
	[[nodiscard]] QString recoveryPath() const;
	[[nodiscard]] bool recoveryBusy() const;
	// Returns immediately for a clean/discarded document; resumes the caller
	// only after a deferred Save has succeeded and this dialog has closed.
	bool requestClose(std::function<void()> afterDeferredSave = {});

protected:
	void closeEvent(QCloseEvent* event) override;
	void reject() override;
	void changeEvent(QEvent* event) override;

private:
	void runJob(const QString& title, std::function<bool(TextureDocument&, QString*, const TextureProgress&)> operation, std::function<void()> completed = {}, bool cancellable = true);
	void refresh();
	void updateCanvasEnabled();
	void setStatus(const QString& text);
	bool confirmDiscard(std::function<void()> afterSave = {});
	bool approveClose(std::function<void()> afterDeferredSave = {});
	void openFile();
	void saveAs(std::function<void()> completed = {});
	void exportFile();
	void loadExportSettings(const QJsonObject& metadata);
	void importLayer();
	void updateLayerProperties();
	void refreshLayers();
	void refreshTransforms();
	void updateInspectorWidth();
	void retireRecovery();
	[[nodiscard]] QJsonObject projectMetadata() const;
	TextureDocument m_document;
	TextureProjectIdentity m_projectIdentity;
	QJsonObject m_extraMetadata, m_savedMetadata;
	IdTechPaletteResolution m_palette;
	QString m_source, m_savedPath, m_contextKey, m_packageTargetKey;
	bool m_busy = false, m_cancelled = false, m_cancellable = false;
	bool m_closeApproved = false, m_recoveryScanned = false;
	bool m_recoveredDraft = false;
	QString m_recoveryDirectory, m_recoveryId;
	TextureRecoveryWriter* m_recovery = nullptr;
	QTimer* m_recoveryTimer = nullptr;
	QTimer* m_recoveryProgressTimer = nullptr;
	QCheckBox* m_recoveryEnabled = nullptr;
	QListWidget* m_recoveryList = nullptr;
	QLabel* m_recoveryStatus = nullptr;
	QThread* m_worker = nullptr;
	std::shared_ptr<TextureEditorTask> m_task;
	QTimer* m_progressTimer = nullptr;
	TextureCanvas* m_canvas = nullptr;
	TextureExportPanel* m_export = nullptr;
	quint64 m_previewRevision = 0;
	QToolBar* m_tools = nullptr;
	QWidget* m_properties = nullptr;
	QComboBox* m_tool = nullptr;
	QComboBox* m_paletteChoice = nullptr;
	QListWidget* m_layers = nullptr;
	QLineEdit* m_layerName = nullptr;
	QCheckBox* m_layerVisible = nullptr;
	QCheckBox* m_layerLocked = nullptr;
	QSpinBox* m_layerOpacity = nullptr;
	QComboBox* m_layerBlend = nullptr;
	QLineEdit* m_color = nullptr;
	QLineEdit* m_virtualPath = nullptr;
	QCheckBox* m_replace = nullptr;
	QCheckBox* m_smooth = nullptr;
	QCheckBox* m_dither = nullptr;
	QSpinBox* m_width = nullptr;
	QSpinBox* m_height = nullptr;
	QComboBox* m_canvasAnchor = nullptr;
	QLabel* m_canvasSizeHint = nullptr;
	QPushButton* m_canvasSizeButton = nullptr;
	QSpinBox* m_selectionWidth = nullptr;
	QSpinBox* m_selectionHeight = nullptr;
	QComboBox* m_selectionAnchor = nullptr;
	QCheckBox* m_selectionSmooth = nullptr;
	QLabel* m_selectionHint = nullptr;
	QPushButton* m_resizeSelection = nullptr;
	QPushButton* m_rotateSelection = nullptr;
	QRect m_transformSelection;
	QSpinBox* m_crop[4]{};
	PaletteSwatchView* m_swatches = nullptr;
	QLabel* m_paletteLabel = nullptr;
	QLabel* m_status = nullptr;
	QLabel* m_contextLabel = nullptr;
	QLabel* m_pixelLabel = nullptr;
	QLabel* m_dimensions = nullptr;
	QProgressBar* m_progress = nullptr;
	QPushButton* m_cancel = nullptr;
	QAction* m_undo = nullptr;
	QAction* m_redo = nullptr;
	QAction* m_stage = nullptr;
	QAction* m_apply = nullptr;
};

} // namespace vibestudio
